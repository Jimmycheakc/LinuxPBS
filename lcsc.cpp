#include <cstdint>
#include <iostream>
#include <iomanip>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <string>
#include <sstream>
#include <vector>
#include "common.h"
#include "event_manager.h"
#include "lcsc.h"
#include "boost/asio.hpp"
#include "boost/asio/serial_port.hpp"
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
    card_application_num_("")
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

void LCSCReader::FnLCSCReaderInit(unsigned int baudRate, const std::string& comPortName)
{
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

void LCSCReader::FnSendGetCardIDCmd()
{
    int ret = lcscCmd(LcscCmd::GET_CARD_ID);

    EventManager::getInstance()->FnEnqueueEvent("Evt_handleLcscReaderLogout", ret);
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

int LCSCReader::lcscCmd(LCSCReader::LcscCmd cmd)
{
    int ret;
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

    std::stringstream dataMsg;
    dataMsg << "Data buffer to be sent in hex: ";
    for (const auto& value : dataBuff) {
        dataMsg << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(value) << " ";
    }
    Logger::getInstance()->FnLog(dataMsg.str(), logFileName_, "LCSC");

    for (int j = 0; j < dataBuff.size(); j++)
    {
        txBuff[i++] = dataBuff[j];
    }

    txCrc = CRC16_CCITT(dataBuff.data(), dataBuff.size());
    txBuff[i++] = (txCrc >> 8) & 0xFF;
    txBuff[i++] = txCrc & 0xFF;
    TxNum_ = i;

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