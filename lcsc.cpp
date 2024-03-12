#include <sys/mount.h>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <string>
#include <sstream>
#include <vector>
#include "common.h"
#include "event_manager.h"
#include "lcsc.h"
#include "boost/asio.hpp"
#include "boost/asio/serial_port.hpp"
#include "boost/algorithm/string.hpp"
#include "operation.h"
#include "log.h"

LCSCReader* LCSCReader::lcscReader_ = nullptr;

LCSCReader::LCSCReader()
    : io_serial_context(),
    logFileName_("LCSC"),
    TxNum_(0),
    RxNum_(0),
    lcscCmdTimeoutInMillisec_(2000),
    lcscCmdMaxRetry_(2),
    rxState_(RX_STATE::RX_START),
    serial_num_(""),
    reader_mode_(0),
    bl1_version_(""),
    bl2_version_(""),
    bl3_version_(""),
    bl4_version_(""),
    cil1_version_(""),
    cil2_version_(""),
    cil3_version_(""),
    cil4_version_(""),
    cfg_version_(""),
    firmware_version_(""),
    reader_challenge_({}),
    card_serial_num_(""),
    card_application_num_(""),
    card_balance_(0.00),
    reader_time_(""),
    continueReadFlag_(false),
    isCmdExecuting_(false),
    HasCDFileToUpload_(false),
    LastCDUploadDate_(0),
    LastCDUploadTime_(0),
    CDUploadTimeOut_(0)
{
    memset(txBuff, 0, sizeof(txBuff));
    memset(rxBuff, 0, sizeof(rxBuff));

    aes_key = 
    {
        0x92, 0xCE, 0xE9, 0x2D, 0x81, 0xE5, 0x4A, 0xEB,
        0xB4, 0x1F, 0x7F, 0x56, 0x2A, 0xF2, 0x7A, 0xDF,
        0x34, 0x65, 0xAA, 0xF4, 0x27, 0x43, 0xF4, 0x3D,
        0xDE, 0x97, 0xE1, 0x86, 0x78, 0x39, 0xEC, 0x0B
    };
}

LCSCReader* LCSCReader::getInstance()
{
    if (lcscReader_ == nullptr)
    {
        lcscReader_ = new LCSCReader();
    }
    return lcscReader_;
}

void LCSCReader::FnLCSCReaderInit(boost::asio::io_context& mainIOContext, unsigned int baudRate, const std::string& comPortName)
{
    pMainIOContext_ = &mainIOContext;
    uploadCDFilesTimer_ = std::make_unique<boost::asio::deadline_timer>(mainIOContext);
    pStrand_ = std::make_unique<boost::asio::io_context::strand>(io_serial_context);
    pSerialPort_ = std::make_unique<boost::asio::serial_port>(io_serial_context, comPortName);

    pSerialPort_->set_option(boost::asio::serial_port_base::baud_rate(baudRate));
    pSerialPort_->set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
    pSerialPort_->set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
    pSerialPort_->set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
    pSerialPort_->set_option(boost::asio::serial_port_base::character_size(8));

    Logger::getInstance()->FnCreateLogFile(logFileName_);

    std::stringstream ss;
    if (pSerialPort_->is_open())
    {
        ss << "Successfully open serial port for LCSC Reader Communication: " << comPortName;
    }
    else
    {
        ss << "Failed to open serial port for LCSC Reader Communication: " << comPortName;
    }
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");
}

void LCSCReader::FnSendGetStatusCmd()
{
    int ret = lcscCmd(LcscCmd::GET_STATUS_CMD);
    EventManager::getInstance()->FnEnqueueEvent("Evt_LcscReaderStatus", ret);
}

void LCSCReader::FnSendGetLoginCmd()
{
    int ret;
    reader_challenge_ = {};

    ret = lcscCmd(LcscCmd::LOGIN_1);

    if (ret == static_cast<int>(mCSCEvents::sLogin1Success))
    {
        ret = lcscCmd(LcscCmd::LOGIN_2);
    }

    EventManager::getInstance()->FnEnqueueEvent("Evt_LcscReaderLogin", ret);
}

void LCSCReader::FnSendGetLogoutCmd()
{
    int ret = lcscCmd(LcscCmd::LOGOUT);

    EventManager::getInstance()->FnEnqueueEvent("Evt_handleLcscReaderLogout", ret);
}

void LCSCReader::handleGetCardIDTimerExpiration()
{
    int ret;
    ret = lcscCmd(LcscCmd::GET_CARD_ID);

    if (ret == static_cast<int>(mCSCEvents::sNoCard) && continueReadFlag_.load())
    {
        startGetCardIDTimer(100);
    }
    else
    {
        continueReadFlag_.store(false);
        EventManager::getInstance()->FnEnqueueEvent("Evt_handleLcscReaderGetCardID", ret);
    }
}

void LCSCReader::startGetCardIDTimer(int milliseconds)
{
    boost::asio::io_context* timerIOContext_ = new boost::asio::io_context();
    std::thread timerThread([this, milliseconds, timerIOContext_]() {
        periodicSendGetCardIDCmdTimer_ = std::make_unique<boost::asio::deadline_timer>(*timerIOContext_);
        periodicSendGetCardIDCmdTimer_->expires_from_now(boost::posix_time::milliseconds(milliseconds));
        periodicSendGetCardIDCmdTimer_->async_wait([this](const boost::system::error_code& error) {
                if (!error) {
                    handleGetCardIDTimerExpiration();
                }
        });

        timerIOContext_->run();
        delete timerIOContext_;
    });
    timerThread.detach();
}

void LCSCReader::FnSendGetCardIDCmd()
{
    continueReadFlag_.store(true);
    startGetCardIDTimer(100);
}

void LCSCReader::handleGetCardBalanceTimerExpiration()
{
    int ret;
    ret = lcscCmd(LcscCmd::CARD_BALANCE);

    if (ret == static_cast<int>(mCSCEvents::sNoCard) && continueReadFlag_.load())
    {
        startGetCardBalanceTimer(100);
    }
    else
    {
        continueReadFlag_.store(false);
        EventManager::getInstance()->FnEnqueueEvent("Evt_handleLcscReaderGetCardBalance", ret);
    }
}

void LCSCReader::startGetCardBalanceTimer(int milliseconds)
{
    boost::asio::io_context* timerIOContext_ = new boost::asio::io_context();
    std::thread timerThread([this, milliseconds, timerIOContext_]() {
        periodicSendGetCardBalanceCmdTimer_ = std::make_unique<boost::asio::deadline_timer>(*timerIOContext_);
        periodicSendGetCardBalanceCmdTimer_->expires_from_now(boost::posix_time::milliseconds(milliseconds));
        periodicSendGetCardBalanceCmdTimer_->async_wait([this](const boost::system::error_code& error) {
                if (!error) {
                    handleGetCardBalanceTimerExpiration();
                }
        });

        timerIOContext_->run();
        delete timerIOContext_;
    });
    timerThread.detach();
}

void LCSCReader::FnSendGetCardBalance()
{
    continueReadFlag_.store(true);
    startGetCardBalanceTimer(100);
}

void LCSCReader::FnSendGetTime()
{
    int ret = lcscCmd(LcscCmd::GET_TIME);

    EventManager::getInstance()->FnEnqueueEvent("Evt_handleLcscReaderGetTime", ret);
}

void LCSCReader::FnSendSetTime()
{
    int ret = lcscCmd(LcscCmd::SET_TIME);

    EventManager::getInstance()->FnEnqueueEvent("Evt_handleLcscReaderSetTime", ret);
}

int LCSCReader::FnSendUploadCFGFile(const std::string& path)
{
    int ret;
    const std::filesystem::path filePath(path);

    if (std::filesystem::exists(filePath) && std::filesystem::is_regular_file(filePath))
    {
        std::vector<unsigned char> fileData = readFile(filePath);

        if (!fileData.empty())
        {
            std::size_t chunkSize = 256;
            std::vector<std::vector<unsigned char>> chunks = chunkData(fileData, chunkSize);

            for (const auto& chunk : chunks)
            {
                ret = uploadCFGSub(chunk);
                if (ret != static_cast<int>(mCSCEvents::sCFGUploadSuccess))
                {
                    break;
                }
            }

            ret = uploadCFGComplete();
        }
        else
        {
            ret = static_cast<int>(mCSCEvents::sCFGUploadCorrupt);

            std::stringstream ss;
            ss << "File not found or not a regular file: " << filePath;
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LSCS");
        }
    }
    else
    {
        ret = static_cast<int>(mCSCEvents::sSendcmdfail);

        std::stringstream ss;
        ss << "File not found or not a regular file: " << filePath;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LSCS");
    }

    EventManager::getInstance()->FnEnqueueEvent("Evt_handleLcscReaderUploadCFGFile", ret);
    return ret;
}

std::vector<unsigned char> LCSCReader::readFile(const std::filesystem::path& filePath)
{
    std::ifstream file(filePath.string(), std::ios::binary | std::ios::ate);
    
    if (!file.is_open())
    {
        std::stringstream ss;
        ss << __func__ << " Error opening file: " << filePath;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LSCS");
        return {};
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<unsigned char> fileData(fileSize);
    if (file.read(reinterpret_cast<char*>(fileData.data()), fileSize))
    {
        return fileData;
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Error reading file: " << filePath;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");
        return {};
    }
}

std::vector<std::vector<unsigned char>> LCSCReader::chunkData(const std::vector<unsigned char>& data, std::size_t chunkSize)
{
    std::vector<std::vector<unsigned char>> chunks;

    for (std::size_t i = 0; i < data.size(); i += chunkSize)
    {
        auto begin = data.begin() + i;
        auto end = (i + chunkSize < data.size()) ? begin + chunkSize : data.end();
        chunks.push_back(std::vector<unsigned char>(begin, end));
    }

    return chunks;
}

int LCSCReader::uploadCFGSub(const std::vector<unsigned char>& data)
{
    int ret;
    std::size_t size = data.size();
    // 8 = First CFG_UPLOAD cmd (Byte[0 - 5]) + CRC (Byte[last 2])
    std::size_t length = data.size() + 8;
    std::vector<unsigned char> dataBuf(length - 2, 0);  // without CRC

    dataBuf[0] = 0xA5;
    dataBuf[1] = 0x32;
    dataBuf[2] = (length >> 8) & 0xFF;
    dataBuf[3] = length & 0xFF;
    dataBuf[4] = (size >> 8) & 0xFF;
    dataBuf[5] = size & 0xFF;

    for (int i = 0; i < size; i++)
    {
        dataBuf[i + 6] = data[i];
    }
    
    std::stringstream ss;
    ss << __func__ <<  " Command : UPLOAD_CFG_FILE";
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");

    bool cmdSendSuccess = lcscCmdSend(dataBuf);

    if (cmdSendSuccess)
    {
        LCSCReader::ReadResult result;
        result.data = {};
        result.success = false;

        result = lcscReadWithTimeout(lcscCmdTimeoutInMillisec_);

        if (result.success)
        {
            ret = static_cast<int>(lcscHandleCmdResponse(LcscCmd::UPLOAD_CFG_FILE, result.data));
        }
        else
        {
            ret = static_cast<int>(mCSCEvents::sTimeout);
        }
    }
    else
    {
        ret = static_cast<int>(mCSCEvents::sSendcmdfail);
    }

    return ret;
}

int LCSCReader::uploadCFGComplete()
{
    int ret;

    std::vector<unsigned char> dataBuf(6, 0);

    dataBuf[0] = 0xA5;
    dataBuf[1] = 0x32;
    dataBuf[2] = 0x00;
    dataBuf[3] = 0x08;
    dataBuf[4] = 0x00;
    dataBuf[5] = 0x00;

    std::stringstream ss;
    ss << __func__ <<  " Command : UPLOAD_CFG_FILE";
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");

    bool cmdSendSuccess = lcscCmdSend(dataBuf);

    if (cmdSendSuccess)
    {
        LCSCReader::ReadResult result;
        result.data = {};
        result.success = false;

        result = lcscReadWithTimeout(lcscCmdTimeoutInMillisec_);

        if (result.success)
        {
            ret = static_cast<int>(lcscHandleCmdResponse(LcscCmd::UPLOAD_CFG_FILE, result.data));
        }
        else
        {
            ret = static_cast<int>(mCSCEvents::sTimeout);
        }
    }
    else
    {
        ret = static_cast<int>(mCSCEvents::sSendcmdfail);
    }

    return ret;
}

int LCSCReader::FnSendUploadCILFile(const std::string& path)
{
    int ret;
    const std::filesystem::path filePath(path);

    if (std::filesystem::exists(filePath) && std::filesystem::is_regular_file(filePath))
    {
        std::vector<unsigned char> fileData = readFile(filePath);

        if (!fileData.empty() && (fileData.size() > 6))
        {
            std::size_t chunkSize = 256;
            std::vector<std::vector<unsigned char>> chunks = chunkData(fileData, chunkSize);

            bool isSubSequence = false;
            for (const auto& chunk : chunks)
            {
                ret = uploadCILSub(chunk, isSubSequence);
                if (ret != static_cast<int>(mCSCEvents::sCFGUploadSuccess))
                {
                    break;
                }
                isSubSequence = true;
            }

            ret = uploadCILComplete();
        }
        else
        {
            ret = static_cast<int>(mCSCEvents::sCILUploadCorrupt);

            std::stringstream ss;
            ss << "File not found or not a regular file: " << filePath;
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LSCS");
        }
    }
    else
    {
        ret = static_cast<int>(mCSCEvents::sSendcmdfail);

        std::stringstream ss;
        ss << "File not found or not a regular file: " << filePath;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LSCS");
    }

    EventManager::getInstance()->FnEnqueueEvent("Evt_handleLcscReaderUploadCILFile", ret);
    return ret;
}

int LCSCReader::uploadCILSub(const std::vector<unsigned char>& data, bool isSubSequence)
{
    int ret;
    std::size_t size = data.size();
    // 8 = First CFG_UPLOAD cmd (Byte[0 - 5]) + CRC (Byte[last 2])
    std::size_t length = data.size() + 9;
    std::vector<unsigned char> dataBuf(length - 2, 0);  // without CRC

    dataBuf[0] = 0xA5;
    dataBuf[1] = 0x31;
    dataBuf[2] = (length >> 8) & 0xFF;
    dataBuf[3] = length & 0xFF;
    dataBuf[4] = (!isSubSequence) ? (static_cast<int>(data[5]) - 143) : 0;
    dataBuf[5] = (size >> 8) & 0xFF;
    dataBuf[6] = size & 0xFF;

    for (int i = 0; i < size; i++)
    {
        dataBuf[i + 7] = data[i];
    }

    std::stringstream ss;
    ss << __func__ <<  " Command : UPLOAD_CIL_FILE";
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");

    bool cmdSendSuccess = lcscCmdSend(dataBuf);

    if (cmdSendSuccess)
    {
        LCSCReader::ReadResult result;
        result.data = {};
        result.success = false;

        result = lcscReadWithTimeout(lcscCmdTimeoutInMillisec_);

        if (result.success)
        {
            ret = static_cast<int>(lcscHandleCmdResponse(LcscCmd::UPLOAD_CIL_FILE, result.data));
        }
        else
        {
            ret = static_cast<int>(mCSCEvents::sTimeout);
        }
    }
    else
    {
        ret = static_cast<int>(mCSCEvents::sSendcmdfail);
    }

    return ret;
}

int LCSCReader::uploadCILComplete()
{
    int ret;

    std::vector<unsigned char> dataBuf(7, 0);

    dataBuf[0] = 0xA5;
    dataBuf[1] = 0x31;
    dataBuf[2] = 0x00;
    dataBuf[3] = 0x09;
    dataBuf[4] = 0x00;
    dataBuf[5] = 0x00;
    dataBuf[6] = 0x00;

    std::stringstream ss;
    ss << __func__ <<  " Command : UPLOAD_CIL_FILE";
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");

    bool cmdSendSuccess = lcscCmdSend(dataBuf);

    if (cmdSendSuccess)
    {
        LCSCReader::ReadResult result;
        result.data = {};
        result.success = false;

        result = lcscReadWithTimeout(lcscCmdTimeoutInMillisec_);

        if (result.success)
        {
            ret = static_cast<int>(lcscHandleCmdResponse(LcscCmd::UPLOAD_CIL_FILE, result.data));
        }
        else
        {
            ret = static_cast<int>(mCSCEvents::sTimeout);
        }
    }
    else
    {
        ret = static_cast<int>(mCSCEvents::sSendcmdfail);
    }

    return ret;
}

int LCSCReader::FnSendUploadBLFile(const std::string& path)
{
    int ret;
    const std::filesystem::path filePath(path);

    if (std::filesystem::exists(filePath) && std::filesystem::is_regular_file(filePath))
    {
        std::vector<unsigned char> fileData = readFile(filePath);

        if (!fileData.empty() && (fileData.size() > 6))
        {
            std::size_t chunkSize = 256;
            std::vector<std::vector<unsigned char>> chunks = chunkData(fileData, chunkSize);

            bool isSubSequence = false;
            for (const auto& chunk : chunks)
            {
                ret = uploadBLSub(chunk, isSubSequence);
                if (ret != static_cast<int>(mCSCEvents::sBLUploadSuccess))
                {
                    break;
                }
                isSubSequence = true;
            }

            ret = uploadBLComplete();
        }
        else
        {
            ret = static_cast<int>(mCSCEvents::sBLUploadCorrupt);

            std::stringstream ss;
            ss << "File not found or not a regular file: " << filePath;
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LSCS");
        }
    }
    else
    {
        ret = static_cast<int>(mCSCEvents::sSendcmdfail);

        std::stringstream ss;
        ss << "File not found or not a regular file: " << filePath;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LSCS");
    }

    EventManager::getInstance()->FnEnqueueEvent("Evt_handleLcscReaderUploadBLFile", ret);
    return ret;
}

int LCSCReader::uploadBLSub(const std::vector<unsigned char>& data, bool isSubSequence)
{
   int ret;
    std::size_t size = data.size();
    // 8 = First CFG_UPLOAD cmd (Byte[0 - 5]) + CRC (Byte[last 2])
    std::size_t length = data.size() + 9;
    std::vector<unsigned char> dataBuf(length - 2, 0);  // without CRC

    dataBuf[0] = 0xA5;
    dataBuf[1] = 0x30;
    dataBuf[2] = (length >> 8) & 0xFF;
    dataBuf[3] = length & 0xFF;
    dataBuf[4] = (!isSubSequence) ? (static_cast<int>(data[5]) - 147) : 0;
    dataBuf[5] = (size >> 8) & 0xFF;
    dataBuf[6] = size & 0xFF;

    for (int i = 0; i < size; i++)
    {
        dataBuf[i + 7] = data[i];
    }

    std::stringstream ss;
    ss << __func__ <<  " Command : UPLOAD_BL_FILE";
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");

    bool cmdSendSuccess = lcscCmdSend(dataBuf);

    if (cmdSendSuccess)
    {
        LCSCReader::ReadResult result;
        result.data = {};
        result.success = false;

        result = lcscReadWithTimeout(lcscCmdTimeoutInMillisec_);

        if (result.success)
        {
            ret = static_cast<int>(lcscHandleCmdResponse(LcscCmd::UPLOAD_BL_FILE, result.data));
        }
        else
        {
            ret = static_cast<int>(mCSCEvents::sTimeout);
        }
    }
    else
    {
        ret = static_cast<int>(mCSCEvents::sSendcmdfail);
    }

    return ret;
}

int LCSCReader::uploadBLComplete()
{
    int ret;

    std::vector<unsigned char> dataBuf(7, 0);

    dataBuf[0] = 0xA5;
    dataBuf[1] = 0x30;
    dataBuf[2] = 0x00;
    dataBuf[3] = 0x09;
    dataBuf[4] = 0x00;
    dataBuf[5] = 0x00;
    dataBuf[6] = 0x00;

    std::stringstream ss;
    ss << __func__ <<  " Command : UPLOAD_BL_FILE";
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");

    bool cmdSendSuccess = lcscCmdSend(dataBuf);

    if (cmdSendSuccess)
    {
        LCSCReader::ReadResult result;
        result.data = {};
        result.success = false;

        result = lcscReadWithTimeout(lcscCmdTimeoutInMillisec_);

        if (result.success)
        {
            ret = static_cast<int>(lcscHandleCmdResponse(LcscCmd::UPLOAD_BL_FILE, result.data));
        }
        else
        {
            ret = static_cast<int>(mCSCEvents::sTimeout);
        }
    }
    else
    {
        ret = static_cast<int>(mCSCEvents::sSendcmdfail);
    }

    return ret;
}

std::string LCSCReader::lcscCmdToString(LCSCReader::LcscCmd cmd)
{
    switch (cmd)
    {
        case LcscCmd::GET_STATUS_CMD:
            return "GET_STATUS_CMD";
            break;
        case LcscCmd::LOGIN_1:
            return "LOGIN_1";
            break;
        case LcscCmd::LOGIN_2:
            return "LOGIN_2";
            break;
        case LcscCmd::LOGOUT:
            return "LOGOUT";
            break;
        case LcscCmd::GET_CARD_ID:
            return "GET_CARD_ID";
            break;
        case LcscCmd::CARD_BALANCE:
            return "CARD_BALANCE";
            break;
        case LcscCmd::CARD_DEDUCT:
            return "CARD_DEDUCT";
            break;
        case LcscCmd::CARD_RECORD:
            return "CARD_RECORD";
            break;
        case LcscCmd::CARD_FLUSH:
            return "CARD_FLUSH";
            break;
        case LcscCmd::GET_TIME:
            return "GET_TIME";
            break;
        case LcscCmd::SET_TIME:
            return "SET_TIME";
            break;
        default:
            return "UNKNOWN_CMD";
    }
}

bool LCSCReader::FnGetIsCmdExecuting() const
{
    return isCmdExecuting_.load();
}

int LCSCReader::lcscCmd(LCSCReader::LcscCmd cmd)
{
    int ret;
    if (!isCmdExecuting_.exchange(true))
    {
        std::stringstream ss;
        ss << __func__ <<  " Command : " << lcscCmdToString(cmd);
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");

        if (!pSerialPort_->is_open())
        {
            return -1;
        }

        std::vector<unsigned char> dataBuffer;

        switch (cmd)
        {
            case LcscCmd::GET_STATUS_CMD:
            {
                dataBuffer = loadGetStatus();
                break;
            }
            case LcscCmd::LOGIN_1:
            {
                dataBuffer = loadLogin1();
                break;
            }
            case LcscCmd::LOGIN_2:
            {
                dataBuffer = loadLogin2();
                break;
            }
            case LcscCmd::LOGOUT:
            {
                dataBuffer = loadLogout();
                break;
            }
            case LcscCmd::GET_CARD_ID:
            {
                dataBuffer = loadGetCardID();
                break;
            }
            case LcscCmd::CARD_BALANCE:
            {
                dataBuffer = loadGetCardBalance();
                break;
            }
            case LcscCmd::GET_TIME:
            {
                dataBuffer = loadGetTime();
                break;
            }
            case LcscCmd::SET_TIME:
            {
                dataBuffer = loadSetTime();
                break;
            }
            default:
            {
                std::stringstream ss;
                ss << __func__ << " : Command not found.";
                Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");

                return static_cast<int>(mCSCEvents::sWrongCmd);
            }
        }

        bool cmdSendSuccess = lcscCmdSend(dataBuffer);

        if (cmdSendSuccess)
        {
            LCSCReader::ReadResult result;
            result.data = {};
            result.success = false;

            result = lcscReadWithTimeout(lcscCmdTimeoutInMillisec_);

            if (result.success)
            {
                ret = static_cast<int>(lcscHandleCmdResponse(cmd, result.data));
            }
            else
            {
                ret = static_cast<int>(mCSCEvents::sTimeout);
            }
        }
        else
        {
            ret = static_cast<int>(mCSCEvents::sSendcmdfail);
        }

        isCmdExecuting_.store(false);
    }
    else
    {
        Logger::getInstance()->FnLog("Cmd cannot send, there is already one Cmd executing.", logFileName_, "LCSC");
        ret = static_cast<int>(mCSCEvents::sSendcmdfail);
    }

    return ret;
}

std::vector<unsigned char> LCSCReader::loadGetStatus()
{
    std::vector<unsigned char> dataBuf(4, 0);

    dataBuf[0] = 0xA5;
    dataBuf[1] = 0x00;
    dataBuf[2] = 0x00;  // 2 Bytes for len
    dataBuf[3] = 0x06;
    
    return dataBuf;
}

std::vector<unsigned char> LCSCReader::loadLogin1()
{
    std::vector<unsigned char> dataBuf(21, 0);

    dataBuf[0] = 0xA5;
    dataBuf[1] = 0x10;
    dataBuf[2] = 0x00;
    dataBuf[3] = 0x17;
    dataBuf[4] = 0x01;

    for (int i = 0; i < 16; i++)
    {
        dataBuf[i + 5] = static_cast<unsigned char>(i);
    }

    return dataBuf;
}

std::vector<unsigned char> LCSCReader::loadLogin2()
{
    std::vector<unsigned char> dataBuf(20, 0);

    dataBuf[0] = 0xA5;
    dataBuf[1] = 0x11;
    dataBuf[2] = 0x00;
    dataBuf[3] = 0x16;

    std::vector<uint8_t> encryptedChallenge(16);
    encryptAES256(aes_key, reader_challenge_, encryptedChallenge);
    std::stringstream encryptedChallengeMsg;
    encryptedChallengeMsg << "Encrypted Challenge : " << Common::getInstance()->FnGetDisplayVectorCharToHexString(encryptedChallenge);
    Logger::getInstance()->FnLog(encryptedChallengeMsg.str(), logFileName_, "LCSC");
    for (int i = 0; i < 16; i++)
    {
        dataBuf[i + 4] = encryptedChallenge[i];
    }

    return dataBuf;
}

std::vector<unsigned char> LCSCReader::loadLogout()
{
    std::vector<unsigned char> dataBuf(4, 0);

    dataBuf[0] = 0xA5;
    dataBuf[1] = 0x12;
    dataBuf[2] = 0x00;
    dataBuf[3] = 0x06;

    return dataBuf;
}

std::vector<unsigned char> LCSCReader::loadGetCardID()
{
    std::vector<unsigned char> dataBuf(5, 0);

    dataBuf[0] = 0xA5;
    dataBuf[1] = 0x20;
    dataBuf[2] = 0x00;
    dataBuf[3] = 0x07;
    dataBuf[4] = 0x01;  // Timeout 1s

    return dataBuf;
}

std::vector<unsigned char> LCSCReader::loadGetCardBalance()
{
    std::vector<unsigned char> dataBuf(5, 0);

    dataBuf[0] = 0xA5;
    dataBuf[1] = 0x21;
    dataBuf[2] = 0x00;
    dataBuf[3] = 0x07;
    dataBuf[4] = 0x01;  // Timeout 1s

    return dataBuf;
}

std::vector<unsigned char> LCSCReader::loadGetTime()
{
    std::vector<unsigned char> dataBuf(4, 0);

    dataBuf[0] = 0xA5;
    dataBuf[1] = 0x42;
    dataBuf[2] = 0x00;
    dataBuf[3] = 0x06;

    return dataBuf;
}

std::vector<unsigned char> LCSCReader::loadSetTime()
{
    std::vector<unsigned char> dataBuf(8, 0);

    dataBuf[0] = 0xA5;
    dataBuf[1] = 0x41;
    dataBuf[2] = 0x00;
    dataBuf[3] = 0x0A;

    std::time_t epochSeconds = Common::getInstance()->FnGetEpochSeconds();
    std::cout << "set time : " << epochSeconds << std::endl;
    dataBuf[4] = (epochSeconds >> 24) & 0xFF;
    dataBuf[5] = (epochSeconds >> 16) & 0xFF;
    dataBuf[6] = (epochSeconds >> 8) & 0xFF;
    dataBuf[7] = epochSeconds & 0xFF;

    return dataBuf;
}

bool LCSCReader::lcscCmdSend(const std::vector<unsigned char>& dataBuff)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    if (!pSerialPort_->is_open())
    {
        return false;
    }

    bool ret;
    int i = 0;
    memset(txBuff, 0, sizeof(txBuff));
    uint16_t txCrc = 0xFFFF;

    for (int j = 0; j < dataBuff.size(); j++)
    {
        txBuff[i++] = dataBuff[j];
    }

    txCrc = CRC16_CCITT(dataBuff.data(), dataBuff.size());
    txBuff[i++] = (txCrc >> 8) & 0xFF;
    txBuff[i++] = txCrc & 0xFF;
    TxNum_ = i;

    std::stringstream dataMsg;
    //dataMsg << "Data buffer to be sent in hex: ";
    for (std::size_t k = 0; k < TxNum_ ; k++)
    {
        dataMsg << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(txBuff[k]) << ' ';
    }
    Logger::getInstance()->FnLog(dataMsg.str(), logFileName_, "LCSC");

    int retry = 0;
    boost::system::error_code error;

    do
    {
        std::size_t bytesTransferred = boost::asio::write(*pSerialPort_, boost::asio::buffer(getTxBuf(), getTxNum()), error);

        std::stringstream ss;
        if (!error)
        {
            ss << "Successfully write the command, bytes : " << bytesTransferred;
            ret = true;
        }
        else
        {
            ss << "Write failed, error : " << error.message();
            ret = false;
        }
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");
        
        retry++;
    } while(!ret && (retry <= lcscCmdMaxRetry_));

    return ret;
}

LCSCReader::ReadResult LCSCReader::lcscReadWithTimeout(int milliseconds)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    std::vector<char> buffer(1024);
    std::size_t total_bytes_transferred = 0;
    bool success = false;

    io_serial_context.reset();
    boost::asio::deadline_timer timer(io_serial_context, boost::posix_time::milliseconds(milliseconds));

    std::function<void()> initiateRead = [&]() {
        pSerialPort_->async_read_some(boost::asio::buffer(buffer),
            [&](const boost::system::error_code& error, std::size_t transferred){
                if (!error)
                {
                    total_bytes_transferred += transferred;

                    if (responseIsComplete(buffer, transferred))
                    {
                        success = true;
                        timer.cancel();
                    }
                    else
                    {
                        initiateRead();
                        success = false;
                    }
                }
                else
                {
                    std::stringstream ss;
                    ss << __func__ << " Async Read error : " << error.message();
                    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");
                    success = false;
                    timer.cancel();
                }
            });
    };

    initiateRead();
    
    timer.async_wait([&](const boost::system::error_code& error){
        if (!total_bytes_transferred && !error)
        {
            std::stringstream ss;
            ss << __func__ << " Async Read Timeout Occurred.";
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");
            pSerialPort_->cancel();
            resetRxBuffer();
            success = false;
        }

        
        io_serial_context.post([this]() {
            io_serial_context.stop();
        });
    });

    io_serial_context.run();

    io_serial_context.reset();

    std::vector<char> retBuff(getRxBuf(), getRxBuf() + getRxNum());
    return {retBuff, success};
}

void LCSCReader::resetRxBuffer()
{
    memset(rxBuff, 0, sizeof(rxBuff));
    RxNum_ = 0;
}

bool LCSCReader::responseIsComplete(const std::vector<char>& buffer, std::size_t bytesTransferred)
{
    bool ret = false;

    for (int i = 0; i < bytesTransferred; i++)
    {
        switch (rxState_)
        {
            case RX_STATE::RX_START:
            {
                if (buffer[i] == 0xa5)
                {
                    resetRxBuffer();
                    rxBuff[RxNum_++] = buffer[i];
                    rxState_ = RX_STATE::RX_RECEIVING;
                }
                ret = false;
                break;
            }
            case RX_STATE::RX_RECEIVING:
            {
                rxBuff[RxNum_++] = buffer[i];

                if (RxNum_ > 4)
                {
                    uint16_t dataLen = (static_cast<uint16_t>(static_cast<unsigned char>(rxBuff[2])) << 8) | (static_cast<uint16_t>(static_cast<unsigned char>(rxBuff[3])));

                    if (RxNum_ == dataLen)
                    {
                        rxState_ = RX_STATE::RX_START;
                        ret = true;
                    }
                    else
                    {
                        ret = false;
                    }
                }
                else
                {
                    ret = false;
                }
                break;
            }
        }
    }

    return ret;
}

bool LCSCReader::parseCscPacketData(LCSCReader::CscPacket& packet, const std::vector<char>& data)
{
    bool ret = true;

    if (data.size() >= 7)
    {
        CscPacket tmpPacket;
        const uint8_t* rawData = reinterpret_cast<const uint8_t*>(data.data());
        
        tmpPacket.attn = rawData[0];
        tmpPacket.code = (rawData[1] & 0x80) != 0;
        tmpPacket.type = rawData[1] & 0x7F;
        tmpPacket.len = (rawData[2] << 8) | rawData[3];
        tmpPacket.payload.assign(rawData + 4, rawData + tmpPacket.len - 2); // 2 bytes CRC, 1 byte length (start from 0)
        tmpPacket.crc = (rawData[tmpPacket.len - 2] << 8) | rawData[tmpPacket.len - 1];

        uint16_t crc_result = CRC16_CCITT(reinterpret_cast<const unsigned char*>(data.data()), (data.size() - 2));
        
        if (crc_result == tmpPacket.crc)
        {
            std::stringstream ss;
            ss << "Attention Code : " << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(tmpPacket.attn);
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");
            ss.str("");
            ss.clear();
            ss << "Code: " << tmpPacket.code;
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");
            ss.str("");
            ss.clear();
            ss << "Type: " << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(tmpPacket.type);
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");
            ss.str("");
            ss.clear();
            ss << "Len: " << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(tmpPacket.len);
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");
            ss.str("");
            ss.clear();
            ss << "Payload: ";
            for (uint8_t byte : tmpPacket.payload)
            {
                ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
            }
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");
            ss.str("");
            ss.clear();
            ss << "CRC: " << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(tmpPacket.crc);
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");

            packet.attn = tmpPacket.attn;
            packet.code = tmpPacket.code;
            packet.type = tmpPacket.type;
            packet.len = tmpPacket.len;
            packet.payload = tmpPacket.payload;
            packet.crc = tmpPacket.crc;

            ret = true;
        }
        else
        {
            std::stringstream ss;
            ss << "CRC check failed. Discarding the packet.";
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");

            ret = false;
        }
    }
    else
    {
        std::stringstream ss;
        ss << "Received incomplete packet.";
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");

        ret = false;
    }

    return ret;
}

LCSCReader::mCSCEvents LCSCReader::lcscHandleCmdResponse(LCSCReader::LcscCmd cmd, const std::vector<char>& dataBuff)
{
    mCSCEvents retEvent;
    std::stringstream receivedRespStream;
    CscPacket packet;

    receivedRespStream << "Received Buffer Data : " << Common::getInstance()->FnGetDisplayVectorCharToHexString(dataBuff);
    Logger::getInstance()->FnLog(receivedRespStream.str(), logFileName_, "LCSC");
    receivedRespStream.str("");
    receivedRespStream.clear();
    receivedRespStream << "Received Buffer Data in Bytes : " << getRxNum();
    Logger::getInstance()->FnLog(receivedRespStream.str(), logFileName_, "LCSC");

    int parseSuccess = parseCscPacketData(packet, dataBuff);

    if (parseSuccess) // Parse success
    {
        if (packet.code) // Response command
        {
            switch (packet.type)
            {
                // Get Status Cmd
                case 0x00:
                {
                    switch (packet.payload[0])
                    {
                        case 0x00: // Result cmd success
                        {
                            // Format like .assign(begin() + startPos, .begin() + startPos + length) 
                            serial_num_.assign(packet.payload.begin() + 1, packet.payload.begin() + 1 + 32);
                            reader_mode_ = packet.payload[33];
                            
                            bl1_version_ = Common::getInstance()->FnGetVectorCharToHexString(packet.payload, 37, 2);
                            bl2_version_ = Common::getInstance()->FnGetVectorCharToHexString(packet.payload, 39, 2);
                            bl3_version_ = Common::getInstance()->FnGetVectorCharToHexString(packet.payload, 41, 2);
                            bl4_version_ = Common::getInstance()->FnGetVectorCharToHexString(packet.payload, 43, 2);
                            cil1_version_ = Common::getInstance()->FnGetVectorCharToHexString(packet.payload, 45, 2);
                            cil2_version_ = Common::getInstance()->FnGetVectorCharToHexString(packet.payload, 47, 2);
                            cil3_version_ = Common::getInstance()->FnGetVectorCharToHexString(packet.payload, 49, 2);
                            cil4_version_ = Common::getInstance()->FnGetVectorCharToHexString(packet.payload, 51, 2);
                            cfg_version_ = Common::getInstance()->FnGetVectorCharToHexString(packet.payload, 53, 2);
                            firmware_version_ = Common::getInstance()->FnGetVectorCharToHexString(packet.payload, 55, 3);

                            Logger::getInstance()->FnLog("Cmd Response : sGetStatusOK", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sGetStatusOK;
                            break;
                        }
                        case 0x01: // Result corrupted cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sCorruptedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sCorruptedCmd;
                            break;
                        }
                        case 0x02: // Result incomplete cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sIncompleteCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sIncompleteCmd;
                            break;
                        }
                        case 0x1c: // Result unknown error
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sUnknown", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sUnknown;
                            break;
                        }
                    }
                    break;
                }
                case 0x10:  // AUTH_LOGIN1
                {
                    switch (packet.payload[0])
                    {
                        case 0x00: // Result cmd success
                        {
                            // Format like .assign(begin() + startPos, .begin() + startPos + length) 
                            reader_challenge_.assign(packet.payload.begin() + 17, packet.payload.begin() + 17 + 16);
                            std::stringstream challengeMsg;
                            challengeMsg << "Reader challenge : " << Common::getInstance()->FnGetDisplayVectorCharToHexString(reader_challenge_);
                            Logger::getInstance()->FnLog(challengeMsg.str(), logFileName_, "LCSC");
                            Logger::getInstance()->FnLog("Cmd Response : sLogin1Success", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sLogin1Success;
                            break;
                        }
                        case 0x01:  // Result corrupted cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sCorruptedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sCorruptedCmd;
                            break;
                        }
                        case 0x02:  // Result incomplete cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sIncompleteCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sIncompleteCmd;
                            break;
                        }
                        case 0x03:  // Result unsupported cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sUnsupportedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sUnsupportedCmd;
                            break;
                        }
                        case 0x04:  // Result unsupported mode
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sUnsupportedMode", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sUnsupportedMode;
                            break;
                        }
                        case 0x1D:  // Result reader already authenticated
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sLoginAlready", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sLoginAlready;
                            break;
                        }
                    }
                    break;
                }
                case 0x11: // AUTH_LOGIN2
                {
                    switch (packet.payload[0])
                    {
                        case 0x00:  // Result cmd success
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sLoginSuccess", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sLoginSuccess;
                            break;
                        }
                        case 0x01:  // Result corrupted cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sCorruptedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sCorruptedCmd;
                            break;
                        }
                        case 0x02:  // Result incomplete cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sIncompleteCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sIncompleteCmd;
                            break;
                        }
                        case 0x03:  // Result unsupported cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sUnsupportedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sUnsupportedCmd;
                            break;
                        }
                        case 0x04:  // Result unsupported mode
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sUnsupportedMode", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sUnsupportedMode;
                            break;
                        }
                        case 0x05:  // Result reader authentication error
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sLoginAuthFail", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sLoginAuthFail;
                            break;
                        }
                    }
                    break;
                }
                case 0x12:  // // AUTH_LOGOUT
                {
                    switch (packet.payload[0])
                    {
                        case 0x00:  // Result cmd success
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sLogoutSuccess", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sLogoutSuccess;
                            break;
                        }
                        case 0x01:  // Result corrupted cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sCorruptedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sCorruptedCmd;
                            break;
                        }
                        case 0x02:  // Result incomplete cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sIncompleteCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sIncompleteCmd;
                            break;
                        }
                        case 0x03:  // Result unsupported cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sUnsupportedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sUnsupportedCmd;
                            break;
                        }
                        case 0x13:  // Result last transaction record not flushed
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sRecordNotFlush", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sRecordNotFlush;
                            break;
                        }
                    }
                    break;
                }
                case 0x20:  // CARD ID
                {
                    switch (packet.payload[0])
                    {
                        case 0x00:  // Result cmd success
                        {
                            // Format like .assign(begin() + startPos, .begin() + startPos + length)
                            card_serial_num_ = Common::getInstance()->FnGetVectorCharToHexString(packet.payload, 1, 8);
                            card_application_num_ = Common::getInstance()->FnGetVectorCharToHexString(packet.payload, 9, 8);

                            Logger::getInstance()->FnLog("Cmd Response : sGetIDSuccess", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sGetIDSuccess;
                            break;
                        }
                        case 0x01:  // Result corrupted cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sCorruptedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sCorruptedCmd;
                            break;
                        }
                        case 0x02:  // Result incomplete cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sIncompleteCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sIncompleteCmd;
                            break;
                        }
                        case 0x03:  // Result unsupported cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sUnsupportedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sUnsupportedCmd;
                            break;
                        }
                        case 0x06:  // Result no card
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sNoCard", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sNoCard;
                            break;
                        }
                        case 0x07:  // Result card error
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sCardError", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sCardError;
                            break;
                        }
                        case 0x08:  // Result RF error
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sRFError", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sRFError;
                            break;
                        }
                        case 0x10:  // Result multiple cards detected
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sMultiCard", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sMultiCard;
                            break;
                        }
                        case 0x12:  // Result card not on card issuer list
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sCardnotinlist", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sCardnotinlist;
                            break;
                        }
                    }
                    break;
                }
                case 0x21:  // Get Card Balance
                {
                    switch (packet.payload[0])
                    {
                        case 0x00:  // Result success cmd
                        {
                            // Format like .assign(begin() + startPos, .begin() + startPos + length)
                            card_serial_num_ = Common::getInstance()->FnGetVectorCharToHexString(packet.payload, 1, 8);
                            card_application_num_ = Common::getInstance()->FnGetVectorCharToHexString(packet.payload, 9, 8);
                            uint32_t temp_card_balance = (packet.payload[17] << 16) | (packet.payload[18] << 8) | packet.payload[19];
                            card_balance_ = static_cast<double>(temp_card_balance) / 100.00;

                            Logger::getInstance()->FnLog("Cmd Response : sGetBlcSuccess", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sGetBlcSuccess;
                            break;
                        }
                        case 0x01:  // Result corrupted cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sCorruptedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sCorruptedCmd;
                            break;
                        }
                        case 0x02:  // Result incomplete cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sIncompleteCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sIncompleteCmd;
                            break;
                        }
                        case 0x03:  // Result unsupported cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sUnsupportedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sUnsupportedCmd;
                            break;
                        }
                        case 0x06:  // Result no card
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sNoCard", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sNoCard;
                            break;
                        }
                        case 0x07:  // Result card error
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sCardError", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sCardError;
                            break;
                        }
                        case 0x08:  // Result RF error
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sRFError", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sRFError;
                            break;
                        }
                        case 0x10:  // Result multiple cards detected
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sMultiCard", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sMultiCard;
                            break;
                        }
                        case 0x12:  // Result card not on card issuer list
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sCardnotinlist", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sCardnotinlist;
                            break;
                        }
                    }
                    break;
                }
                case 0x42:  // Get Time cmd
                {
                    switch (packet.payload[0])
                    {
                        case 0x00:  // Result success cmd
                        {
                            uint32_t epochSeconds = (packet.payload[1] << 24) | (packet.payload[2] << 16) | (packet.payload[3] << 8) | packet.payload[4];
                            reader_time_ = Common::getInstance()->FnConvertDateTime(epochSeconds);
                            Logger::getInstance()->FnLog("Cmd Response : sGetTimeSuccess", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sGetTimeSuccess;
                            break;
                        }
                        case 0x01:  // Result corrupted cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : rCorruptedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::rCorruptedCmd;
                            break;
                        }
                        case 0x02:  // Result incomplete cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sIncompleteCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sIncompleteCmd;
                            break;
                        }
                        case 0x03:  // Result unsupported cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sUnsupportedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sUnsupportedCmd;
                            break;
                        }
                        case 0x1c:  // Result unknown error
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sUnknown", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sUnknown;
                            break;
                        }
                    }
                    break;
                }
                case 0x41:  // Set Time cmd
                {
                    switch (packet.payload[0])
                    {
                        case 0x00:  // Result success cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sSetTimeSuccess", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sSetTimeSuccess;
                            break;
                        }
                        case 0x01:  // Result corrupted cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sCorruptedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sCorruptedCmd;
                            break;
                        }
                        case 0x02:  // Result incomplete cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sIncompleteCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sIncompleteCmd;
                            break;
                        }
                        case 0x03:  // Result unsupported cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sUnsupportedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sUnsupportedCmd;
                            break;
                        }
                        case 0x1c:  // Result unknown error
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sUnknown", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sUnknown;
                            break;
                        }
                    }
                    break;
                }
                case 0x32:  // CFG_UPLOAD cmd
                {
                    switch (packet.payload[0])
                    {
                        case 0x00:  // Result success cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sCFGUploadSuccess", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sCFGUploadSuccess;
                            break;
                        }
                        case 0x01:  // Result corrupted cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sCorruptedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sCorruptedCmd;
                            break;
                        }
                        case 0x02:  // Result incomplete cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sIncompleteCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sIncompleteCmd;
                            break;
                        }
                        case 0x03:  // Result unsupported cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sUnsupportedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sUnsupportedCmd;
                            break;
                        }
                        case 0x13:  // Result last transaction record not flushed
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sRecordNotFlush", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sRecordNotFlush;
                            break;
                        }
                        case 0x1b:  // Result configuration file upload corrupted
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sCFGUploadCorrupt", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sCFGUploadCorrupt;
                            break;
                        }
                        case 0x1c:  // Result unknown error
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sUnknown", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sUnknown;
                            break;
                        }
                    }
                    break;
                }
                case 0x31:  // Upload CIL cmd
                {
                    switch (packet.payload[0])
                    {
                        case 0x00:  // Result success cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sCILUploadSuccess", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sCILUploadSuccess;
                            break;
                        }
                        case 0x01:  // Result corrupted cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sCorruptedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sCorruptedCmd;
                            break;
                        }
                        case 0x02:  // Result incomplete cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sIncompleteCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sIncompleteCmd;
                            break;
                        }
                        case 0x03:  // Result unsupported cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sUnsupportedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sUnsupportedCmd;
                            break;
                        }
                        case 0x17:  // Result incorrect list index
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sIncorrectIndex", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sIncorrectIndex;
                            break;
                        }
                        case 0x1a:  // Result card issuer list upload corrupted
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sCILUploadCorrupt", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sCILUploadCorrupt;
                            break;
                        }
                        case 0x1c:  // Result unknown error
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sUnknown", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sUnknown;
                            break;
                        }
                    }
                    break;
                }
                case 0x30:  // Upload BL cmd
                {
                    switch (packet.payload[0])
                    {
                        case 0x00:  // Result success cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sBLUploadSuccess", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sBLUploadSuccess;
                            break;
                        }
                        case 0x01:  // Result corrupted cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sCorruptedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sCorruptedCmd;
                            break;
                        }
                        case 0x02:  // Result incomplete cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sIncompleteCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sIncompleteCmd;
                            break;
                        }
                        case 0x03:  // Result unsupported cmd
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sUnsupportedCmd", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sUnsupportedCmd;
                            break;
                        }
                        case 0x17:  // Result incorrect list index
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sIncorrectIndex", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sIncorrectIndex;
                            break;
                        }
                        case 0x19:  // Result blacklist upload corrupted
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sBLUploadCorrupt", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sBLUploadCorrupt;
                            break;
                        }
                        case 0x1c:  // Result unknown error
                        {
                            Logger::getInstance()->FnLog("Cmd Response : sUnknown", logFileName_, "LCSC");
                            retEvent = mCSCEvents::sUnknown;
                            break;
                        }
                    }
                    break;
                }
            }
        }
        else
        {
            retEvent = mCSCEvents::rNotRespCmd;
        }
    }
    else
    {
        retEvent = mCSCEvents::rCRCError;
    }

    return retEvent;
}

unsigned char* LCSCReader::getTxBuf()
{
    return txBuff;
}

unsigned char* LCSCReader::getRxBuf()
{
    return rxBuff;
}

int LCSCReader::getTxNum()
{
    return TxNum_;
}

int LCSCReader::getRxNum()
{
    return RxNum_;
}

uint16_t LCSCReader::CRC16_CCITT(const unsigned char* inStr, size_t length)
{
    unsigned char CL = 0xFF;
    unsigned char Ch = 0xFF;

    unsigned char HB1, LB1;

    for (size_t i = 0; i < length; ++i)
    {
        Ch ^= inStr[i];

        for (int j = 0; j < 8; ++j)
        {
            HB1 = Ch & 0x80;
            LB1 = CL & 0x80;

            Ch = (Ch & 0x7F) * 2;
            CL = (CL & 0x7F) * 2;

            if (LB1 == 0x80)
            {
                Ch |= 0x01;
            }

            if (HB1 == 0x80)
            {
                Ch ^= 0x10;
                CL ^= 0x21;
            }
        }
    }

    return (static_cast<uint16_t>(Ch) << 8) | static_cast<uint16_t>(CL);
}

std::string LCSCReader::FnGetSerialNumber()
{
    return serial_num_;
}

int LCSCReader::FnGetReaderMode()
{
    return reader_mode_;
}

std::string LCSCReader::FnGetBL1Version()
{
    return bl1_version_;
}

std::string LCSCReader::FnGetBL2Version()
{
    return bl2_version_;
}

std::string LCSCReader::FnGetBL3Version()
{
    return bl3_version_;
}

std::string LCSCReader::FnGetBL4Version()
{
    return bl4_version_;
}

std::string LCSCReader::FnGetCIL1Version()
{
    return cil1_version_;
}

std::string LCSCReader::FnGetCIL2Version()
{
    return cil2_version_;
}

std::string LCSCReader::FnGetCIL3Version()
{
    return cil3_version_;
}

std::string LCSCReader::FnGetCIL4Version()
{
    return cil4_version_;
}

std::string LCSCReader::FnGetCFGVersion()
{
    return cfg_version_;
}

std::string LCSCReader::FnGetFirmwareVersion()
{
    return firmware_version_;
}

void LCSCReader::encryptAES256(const std::vector<uint8_t>& key, const std::vector<uint8_t>& challenge, std::vector<uint8_t>& encryptedChallenge)
{
    AES_KEY aesKey;
    AES_set_encrypt_key(key.data(), 256, &aesKey);
    // Assuming that the challenge length is a multiple of AES_BLOCK_SIZE (16 bytes)
    for (size_t i = 0; i < challenge.size(); i += AES_BLOCK_SIZE) {
        AES_encrypt(challenge.data() + i, encryptedChallenge.data() + i, &aesKey);
    }
}

std::string LCSCReader::FnGetCardSerialNumber()
{
    return card_serial_num_;
}

std::string LCSCReader::FnGetCardApplicationNumber()
{
    return card_application_num_;
}

double LCSCReader::FnGetCardBalance()
{
    return card_balance_;
}

std::string LCSCReader::FnGetReaderTime()
{
    return reader_time_;
}

void LCSCReader::FnLCSCReaderStopRead()
{
    continueReadFlag_.store(false);
}

std::string LCSCReader::calculateSHA256(const std::string& data)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data.c_str(), data.length());
    SHA256_Final(hash, &sha256);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
    {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

bool LCSCReader::FnMoveCDAckFile()
{
    std::string folderPath = operation::getInstance()->tParas.gsCSCRcdackFolder;
    std::replace(folderPath.begin(), folderPath.end(), '\\', '/');

    std::string mountPoint = "/mnt/cdack";
    std::string sharedFolderPath = folderPath;
    std::string username = "sunpark";
    std::string password = "Tdxh638*";
    std::string cdAckFilePath = operation::getInstance()->tParas.gsLocalLCSC;

    // Create the mount point directory if doesn't exist
    if (!std::filesystem::exists(mountPoint))
    {
        std::error_code ec;
        if (!std::filesystem::create_directories(mountPoint, ec))
        {
            Logger::getInstance()->FnLog(("Failed to create " + mountPoint + " directory : " + ec.message()), logFileName_, "LCSC");
            return false;
        }
        else
        {
            Logger::getInstance()->FnLog(("Successfully to create " + mountPoint + " directory."), logFileName_, "LCSC");
        }
    }
    else
    {
        Logger::getInstance()->FnLog(("Mount point directory: " + mountPoint + " exists."), logFileName_, "LCSC");
    }

    // Mount the shared folder
    std::string mountCommand = "sudo mount -t cifs " + sharedFolderPath + " " + mountPoint +
                                " -o username=" + username + ",password=" + password;
    int mountStatus = std::system(mountCommand.c_str());
    if (mountStatus != 0)
    {
        Logger::getInstance()->FnLog(("Failed to mount " + mountPoint), logFileName_, "LCSC");
        return false;
    }
    else
    {
        Logger::getInstance()->FnLog(("Successfully to mount " + mountPoint), logFileName_, "LCSC");
    }
    
    // Copy files to mount point
    std::filesystem::path folder(cdAckFilePath);
    if (std::filesystem::exists(folder) && std::filesystem::is_directory(folder))
    {
        for (const auto& entry : std::filesystem::directory_iterator(folder))
        {
            std::string filename = entry.path().filename().string();

            if (std::filesystem::is_regular_file(entry)
                && (filename.size() >= 6) && (filename.substr(filename.size() - 6) == ".cdack"))
            {
                std::filesystem::path dest_file = mountPoint / entry.path().filename();
                std::filesystem::copy(entry.path(), dest_file, std::filesystem::copy_options::overwrite_existing);
                std::filesystem::remove(entry.path());

                std::stringstream ss;
                ss << "Move " << entry.path() << " to " << dest_file << " successfully";
                Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");
            }
        }
    }
    else
    {
        Logger::getInstance()->FnLog("Folder doesn't exist or is not a directory.", logFileName_, "LCSC");
        umount(mountPoint.c_str());
        return false;
    }

    // Unmount the shared folder
    std::string unmountCommand = "sudo umount " + mountPoint;
    int unmountStatus = std::system(unmountCommand.c_str());
    if (unmountStatus != 0)
    {
        Logger::getInstance()->FnLog(("Failed to unmount " + mountPoint), logFileName_, "LCSC");
        return false;
    }
    else
    {
        Logger::getInstance()->FnLog(("Successfully to unmount " + mountPoint), logFileName_, "LCSC");
    }

    return true;
}

bool LCSCReader::FnGenerateCDAckFile()
{
    FnSendGetStatusCmd();

    std::string cdAckFilePath = operation::getInstance()->tParas.gsLocalLCSC;

    // Create cd ack file path
    if (!std::filesystem::exists(cdAckFilePath))
    {
        std::error_code ec;
        if (!std::filesystem::create_directories(cdAckFilePath, ec))
        {
            Logger::getInstance()->FnLog(("Failed to create " + cdAckFilePath + " directory : " + ec.message()), logFileName_, "LCSC");
            return false;
        }
        else
        {
            Logger::getInstance()->FnLog(("Successfully to create " + cdAckFilePath + " directory."), logFileName_, "LCSC");
        }
    }
    else
    {
        Logger::getInstance()->FnLog(("CD Ack directory: " + cdAckFilePath + " exists."), logFileName_, "LCSC");
    }

    // Construct cd ack file name
    std::string terminalID;
    std::size_t terminalIDLen = FnGetSerialNumber().length();
    if (terminalIDLen < 6)
    {
        terminalID = "000000";
    }
    else
    {
        terminalID = FnGetSerialNumber().substr(terminalIDLen - 6);
    }

    std::string ackFileName = boost::algorithm::trim_copy(operation::getInstance()->tParas.gsCPOID) + "_" + operation::getInstance()->tParas.gsCPID + "_CR"
                                + "0" + terminalID + "_" + Common::getInstance()->FnGetDateTimeFormat_yyyymmdd_hhmmss() + ".cdack";
    std::string sAckFile = cdAckFilePath + "/" + ackFileName;
    
    // Construct data contents
    std::string sHeader;
    std::string sData;
    std::string sDataO;

    sHeader = "H" + Common::getInstance()->FnPadRightSpace(53, ackFileName) + Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmmss() + FnGetFirmwareVersion();
    sDataO = sHeader;
    sData = sHeader + '\n';

    std::string tmp;
    int count = 0;

    tmp = "";
    if (FnGetBL1Version() != "FFFF")
    {
        tmp = std::string("C") + "$" + "0" + terminalID + "," + boost::algorithm::to_lower_copy(std::string("netscsc2.blk")) + ",$" + FnGetBL1Version();
        sDataO = sDataO + tmp;
        sData = sData + tmp + '\n';
        count = count + 1;
    }

    tmp = "";
    if (FnGetBL2Version() != "FFFF")
    {
        tmp = std::string("C") + "$" + "0" + terminalID + "," + boost::algorithm::to_lower_copy(std::string("ezlkcsc2.blk")) + ",$" + FnGetBL2Version();
        sDataO = sDataO + tmp;
        sData = sData + tmp + '\n';
        count = count + 1;
    }

    tmp = "";
    if (FnGetBL3Version() != "FFFF")
    {
        tmp = std::string("C") + "$" + "0" + terminalID + "," + boost::algorithm::to_lower_copy(std::string("fut3csc2.blk")) + ",$" + FnGetBL3Version();
        sDataO = sDataO + tmp;
        sData = sData + tmp + '\n';
        count = count + 1;
    }

    tmp = "";
    if (FnGetCIL1Version() != "FFFF")
    {
        tmp = std::string("C") + "$" + "0" + terminalID + "," + boost::algorithm::to_lower_copy(std::string("netsiss2.sys")) + ",$" + FnGetCIL1Version();
        sDataO = sDataO + tmp;
        sData = sData + tmp + '\n';
        count = count + 1;
    }

    tmp = "";
    if (FnGetCIL2Version() != "FFFF")
    {
        tmp = std::string("C") + "$" + "0" + terminalID + "," + boost::algorithm::to_lower_copy(std::string("ezlkiss2.sys")) + ",$" + FnGetCIL2Version();
        sDataO = sDataO + tmp;
        sData = sData + tmp + '\n';
        count = count + 1;
    }

    tmp = "";
    if (FnGetCIL3Version() != "FFFF")
    {
        tmp = std::string("C") + "$" + "0" + terminalID + "," + boost::algorithm::to_lower_copy(std::string("fut3iss2.sys")) + ",$" + FnGetCIL3Version();
        sDataO = sDataO + tmp;
        sData = sData + tmp + '\n';
        count = count + 1;
    }

    tmp = "";
    if (FnGetCFGVersion() != "FFFF")
    {
        tmp = std::string("D") + "$" + "0" + terminalID + "," + boost::algorithm::to_lower_copy(std::string("device.zip")) + ",$" + FnGetCFGVersion();
        sDataO = sDataO + tmp;
        sData = sData + tmp + '\n';
        count = count + 1;
    }

    // Open ack file and write binary data to it
    std::ofstream outFile(sAckFile, std::ios::binary);

    sData = sData + "T" + Common::getInstance()->FnPadLeft0(6, count);

    std::string hash = calculateSHA256(sDataO);
    sData = sData + hash + '\n';

    if (!(outFile << sData))
    {
        Logger::getInstance()->FnLog(("Failed to write data into " + sAckFile), logFileName_, "LCSC");
        outFile.close();
        return false;
    }
    else
    {
        Logger::getInstance()->FnLog(("Successfully write data into " + sAckFile), logFileName_, "LCSC");
    }

    outFile.close();

    return true;
}

bool LCSCReader::FnDownloadCDFiles()
{
    std::string folderPath = operation::getInstance()->tParas.gsCSCRcdfFolder;
    std::replace(folderPath.begin(), folderPath.end(), '\\', '/');

    std::string mountPoint = "/mnt/cd";
    std::string sharedFolderPath = folderPath;
    std::string username = "sunpark";
    std::string password = "Tdxh638*";
    std::string outputFolderPath = operation::getInstance()->tParas.gsLocalLCSC;

    // Create the mount point directory if doesn't exist
    if (!std::filesystem::exists(mountPoint))
    {
        std::error_code ec;
        if (!std::filesystem::create_directories(mountPoint, ec))
        {
            Logger::getInstance()->FnLog(("Failed to create " + mountPoint + " directory : " + ec.message()), logFileName_, "LCSC");
            return false;
        }
        else
        {
            Logger::getInstance()->FnLog(("Successfully to create " + mountPoint + " directory."), logFileName_, "LCSC");
        }
    }
    else
    {
        Logger::getInstance()->FnLog(("Mount point directory: " + mountPoint + " exists."), logFileName_, "LCSC");
    }

    // Mount the shared folder
    std::string mountCommand = "sudo mount -t cifs " + sharedFolderPath + " " + mountPoint +
                                " -o username=" + username + ",password=" + password;
    int mountStatus = std::system(mountCommand.c_str());
    if (mountStatus != 0)
    {
        Logger::getInstance()->FnLog(("Failed to mount " + mountPoint), logFileName_, "LCSC");
        return false;
    }
    else
    {
        Logger::getInstance()->FnLog(("Successfully to mount " + mountPoint), logFileName_, "LCSC");
    }

    // Check Folder exist or not
    std::filesystem::path folder(mountPoint);

    int fileCount = 0;
    if (std::filesystem::exists(folder) && std::filesystem::is_directory(folder))
    {
        for (const auto& entry : std::filesystem::directory_iterator(folder))
        {
            if (std::filesystem::is_regular_file(entry))
            {
                fileCount++;
            }
        }
    }

    if (fileCount == 0)
    {
        Logger::getInstance()->FnLog("No CD file to download.", logFileName_, "LCSC");
        umount(mountPoint.c_str());
        return false;
    }

    int downloadTotal = 0;
    // Create the output folder if it doesn't exist
    if (!std::filesystem::exists(outputFolderPath))
    {
        std::error_code ec;
        if (!std::filesystem::create_directories(outputFolderPath, ec))
        {
            Logger::getInstance()->FnLog(("Failed to create " + outputFolderPath + " directory : " + ec.message()), logFileName_, "LCSC");
            umount(mountPoint.c_str()); // Unmount if folder creation fails
            return false;
        }
        else
        {
            Logger::getInstance()->FnLog(("Successfully to create " + outputFolderPath + " directory."), logFileName_, "LCSC");
        }
    }
    else
    {
        Logger::getInstance()->FnLog(("Output folder directory : " + outputFolderPath + " exists."), logFileName_, "LCSC");
    }

    if (std::filesystem::exists(folder) && std::filesystem::is_directory(folder))
    {
        for (const auto& entry : std::filesystem::directory_iterator(folder))
        {
            if (std::filesystem::is_regular_file(entry))
            {
                std::filesystem::path dest_file = outputFolderPath / entry.path().filename();
                std::filesystem::copy(entry.path(), dest_file, std::filesystem::copy_options::overwrite_existing);
                std::filesystem::remove(entry.path());
                downloadTotal++;

                std::stringstream ss;
                ss << "Download " << entry.path() << " to " << dest_file << " successfully";
                Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");
            }
        }
    }
    else
    {
        Logger::getInstance()->FnLog("Folder doesn't exist or is not a directory.", logFileName_, "LCSC");
        umount(mountPoint.c_str());
        return false;
    }

    std::stringstream ss;
    ss << "Total " << downloadTotal << " cd files downloaded";
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");

    // Unmount the shared folder
    std::string unmountCommand = "sudo umount " + mountPoint;
    int unmountStatus = std::system(unmountCommand.c_str());
    if (unmountStatus != 0)
    {
        Logger::getInstance()->FnLog(("Failed to unmount " + mountPoint), logFileName_, "LCSC");
        return false;
    }
    else
    {
        Logger::getInstance()->FnLog(("Successfully to unmount " + mountPoint), logFileName_, "LCSC");
    }

    return true;
}

void LCSCReader::FnUploadLCSCCDFiles()
{
    if ((operation::getInstance()->tParas.giCommPortLCSC > 0)
        && (!operation::getInstance()->tProcess.gbLoopApresent)
        && (operation::getInstance()->tProcess.WaitForLCSCReturn == false)
        && (Common::getInstance()->FnGetCurrentHour() < 20))
    {
        if ((HasCDFileToUpload_ == false)
            && (LastCDUploadDate_ != Common::getInstance()->FnGetCurrentDay())
            && (LastCDUploadTime_ != Common::getInstance()->FnGetCurrentHour()))
        {
            LastCDUploadTime_ = Common::getInstance()->FnGetCurrentHour();
            if (FnDownloadCDFiles())
            {
                HasCDFileToUpload_ = true;
            }
            else
            {
                return;
            }
        }

        if ((HasCDFileToUpload_ == true) || (LastCDUploadDate_ == 0))
        {
            std::string localLCSCFolder = operation::getInstance()->tParas.gsLocalLCSC;
            std::filesystem::path folder(localLCSCFolder);
            LastCDUploadDate_ = 1;
            bool uploadCDFilesRet = false;
            int fileCount = 0;
            std::string cdFileName;

            for (const auto& entry : std::filesystem::directory_iterator(folder))
            {
                std::string filename = entry.path().filename().string();

                if (((filename.size() >= 4) && (filename.substr(filename.size() - 4) != ".lcs"))
                    && ((filename.size() >= 6) && (filename.substr(filename.size() - 6) != ".cdack"))
                    && ((filename.size() >= 4) && (filename.substr(filename.size() - 4) != ".cfg")))
                {
                    fileCount = fileCount + 1;
                    cdFileName = entry.path();
                }
            }

            if (fileCount > 0)
            {
                HasCDFileToUpload_ = true;
                uploadCDFilesRet = FnUploadCDFile2(cdFileName);
                operation::getInstance()->tProcess.WaitForLCSCReturn = true;
                CDUploadTimeOut_ = 0;

                std::stringstream ss;
                ss << "Call upload CD file: " << cdFileName;
                Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");
            }
            else
            {
                if (HasCDFileToUpload_ == true)
                {
                    if (FnGenerateCDAckFile() == true)
                    {
                        HasCDFileToUpload_ = false;
                        Logger::getInstance()->FnLog("Generate CD Ack file successfully", logFileName_, "LCSC");
                        std::string localLCSCFolder = operation::getInstance()->tParas.gsLocalLCSC;
                        std::filesystem::path folder(localLCSCFolder);
                        LastCDUploadDate_ = 0;

                        for (const auto& entry : std::filesystem::directory_iterator(folder))
                        {
                            std::string filename = entry.path().filename().string();

                            if (((filename.size() >= 4) && (filename.substr(filename.size() - 4) != ".lcs"))
                                && ((filename.size() >= 6) && (filename.substr(filename.size() - 6) != ".cdack")))
                            {
                                std::filesystem::remove(entry.path());
                                std::stringstream ss;
                                ss << "File " << filename << " deleted";
                                Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");
                            }

                            if ((filename.size() >= 6) && (filename.substr(filename.size() - 6) == ".cdack"))
                            {
                                if (FnMoveCDAckFile())
                                {
                                    LastCDUploadDate_ = Common::getInstance()->FnGetCurrentDay();
                                }
                            }
                        }
                    }
                    else
                    {
                        Logger::getInstance()->FnLog("Generate CD Ack file failed", logFileName_, "LCSC");
                    }
                }
                else
                {
                    std::string localLCSCFolder = operation::getInstance()->tParas.gsLocalLCSC;
                    std::filesystem::path folder(localLCSCFolder);

                    for (const auto& entry : std::filesystem::directory_iterator(folder))
                    {
                        std::string filename = entry.path().filename().string();

                        if (((filename.size() >= 4) && (filename.substr(filename.size() - 4) != ".lcs"))
                            && ((filename.size() >= 6) && (filename.substr(filename.size() - 6) != ".cdack")))
                        {
                            std::filesystem::remove(entry.path());
                            std::stringstream ss;
                            ss << "File " << filename << " deleted";
                            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");
                        }

                        if ((filename.size() >= 6) && (filename.substr(filename.size() - 6) == ".cdack"))
                        {
                            if (FnMoveCDAckFile())
                            {
                                LastCDUploadDate_ = Common::getInstance()->FnGetCurrentDay();
                            }
                        }
                    }
                }
            }
        }
    }

    if (operation::getInstance()->tProcess.WaitForLCSCReturn == true)
    {
        if (CDUploadTimeOut_ > 6)
        {
            operation::getInstance()->tProcess.WaitForLCSCReturn = false;
        }
        else
        {
            CDUploadTimeOut_ = CDUploadTimeOut_ + 1;
        }
    }

    if ((Common::getInstance()->FnGetCurrentHour() > 20)
        && (HasCDFileToUpload_ || operation::getInstance()->tProcess.WaitForLCSCReturn))
    {
        std::string localLCSCFolder = operation::getInstance()->tParas.gsLocalLCSC;
        std::filesystem::path folder(localLCSCFolder);

        for (const auto& entry : std::filesystem::directory_iterator(folder))
        {
            std::string filename = entry.path().filename().string();

            if (((filename.size() >= 4) && (filename.substr(filename.size() - 4) != ".lcs"))
                && ((filename.size() >= 6) && (filename.substr(filename.size() - 6) != ".cdack")))
            {
                std::filesystem::remove(entry.path());
                std::stringstream ss;
                ss << "File " << filename << " deleted";
                Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");
            }
        }
        operation::getInstance()->tProcess.WaitForLCSCReturn = false;
        HasCDFileToUpload_ = false;
    }
}

void LCSCReader::handleUploadCDFilesTimerTimeout()
{
    if (!CDFPath_.empty())
    {
        int ret = 0;
        if ((CDFPath_.size() >= 4) && (CDFPath_.substr(CDFPath_.size() - 4) == ".zip"))
        {
            ret = FnSendUploadCFGFile(CDFPath_);
            if ((ret == static_cast<int>(mCSCEvents::sCFGUploadSuccess)) || (ret == static_cast<int>(mCSCEvents::sWrongCDfileSize)))
            {
                std::filesystem::remove(CDFPath_.c_str());
            }
        }
        else if ((CDFPath_.size() >= 4) && (CDFPath_.substr(CDFPath_.size() - 4) == ".sys"))
        {
            ret = FnSendUploadCILFile(CDFPath_);
            if ((ret == static_cast<int>(mCSCEvents::sCILUploadSuccess)) || (ret == static_cast<int>(mCSCEvents::sWrongCDfileSize)))
            {
                std::filesystem::remove(CDFPath_.c_str());
            }
        }
        else if ((CDFPath_.size() >= 4) && (CDFPath_.substr(CDFPath_.size() - 4) == ".blk"))
        {
            ret = FnSendUploadBLFile(CDFPath_);
            if ((ret == static_cast<int>(mCSCEvents::sBLUploadSuccess)) || (ret == static_cast<int>(mCSCEvents::sWrongCDfileSize)))
            {
                std::filesystem::remove(CDFPath_.c_str());
            }
        }
        else if ((CDFPath_.size() >= 4) && (CDFPath_.substr(CDFPath_.size() - 4) == ".lbs"))
        {
            // Temp: Need to implement update FW
        }
        
        CDFPath_ = "";

        if (ret == static_cast<int>(mCSCEvents::sUnsupportedCmd)) // Re-login
        {
            FnSendGetLoginCmd();
        }
    }
}

bool LCSCReader::FnUploadCDFile2(std::string path)
{
    CDFPath_ = path;
    uploadCDFilesTimer_->expires_from_now(boost::posix_time::milliseconds(10));
    uploadCDFilesTimer_->async_wait([this](const boost::system::error_code& error) {
            if (!error) {
                handleUploadCDFilesTimerTimeout();
            }
    });

    return true;
}
