#include <chrono>
#include <ctime>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include "common.h"
#include "event_handler.h"
#include "event_manager.h"
#include "ksm_reader.h"
#include "log.h"


KSM_Reader* KSM_Reader::ksm_reader_ = nullptr;

KSM_Reader::KSM_Reader()
    : io_serial_context(),
    TxNum_(0),
    RxNum_(0),
    continueReadFlag_(false),
    isCmdExecuting_(false),
    recvbcc_(0),
    cardPresented_(false),
    cardNum_(""),
    cardExpiryYearMonth_(0),
    cardExpired_(false),
    cardBalance_(0)
{
    memset(txBuff, 0, sizeof(txBuff));
    memset(rxBuff, 0, sizeof(rxBuff));
    logFileName_ = "ksmReader";
}

KSM_Reader* KSM_Reader::getInstance()
{
    if (ksm_reader_ == nullptr)
    {
        ksm_reader_ = new KSM_Reader();
    }
    return ksm_reader_;
}

void KSM_Reader::FnKSMReaderInit(boost::asio::io_context& mainIOContext, unsigned int baudRate, const std::string& comPortName)
{
    pMainIOContext_ = &mainIOContext;
    pStrand_ = std::make_unique<boost::asio::io_context::strand>(io_serial_context);
    pSerialPort_ = std::make_unique<boost::asio::serial_port>(io_serial_context, comPortName);

    try 
    {
        pSerialPort_->set_option(boost::asio::serial_port_base::baud_rate(baudRate));
        pSerialPort_->set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
        pSerialPort_->set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
        pSerialPort_->set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
        pSerialPort_->set_option(boost::asio::serial_port_base::character_size(8));
    } catch (const boost::system::system_error& e) {
    // Handle error
    std::cerr << "Error setting serial port options: " << e.what() << std::endl;
    // You may choose to throw the error again to propagate it to the caller or handle it differently
    }

    Logger::getInstance()->FnCreateLogFile(logFileName_);

    std::stringstream ss;
    if (pSerialPort_->is_open())
    {
        ss << "Successfully open serial port for KSM Reader Communication: " << comPortName;
    }
    else
    {
        ss << "Failed to open serial port for KSM Reader Communication: " << comPortName;
        Logger::getInstance()->FnLog(ss.str());
    }
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "KSM");

    std::stringstream initSS;
    int ret = ksmReaderCmd(KSMReaderCmdID::INIT_CMD);
    if (ret == 1)
    {
        initSS << "KSM Reader initialization completed.";
    }
    else
    {
        initSS << "KSM Reader initialization failed.";
    }
    Logger::getInstance()->FnLog(initSS.str());
    Logger::getInstance()->FnLog(initSS.str(), logFileName_, "KSM");
}

bool KSM_Reader::FnGetIsCmdExecuting() const
{
    return isCmdExecuting_.load();
}


void KSM_Reader::sendEnq()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "KSM");

    unsigned char data[1] = { KSM_Reader::ENQ };
    boost::asio::write(*pSerialPort_, boost::asio::buffer(data, 1));
}

int KSM_Reader::FnKSMReaderSendGetStatus()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "KSM");

    return ksmReaderCmd(KSMReaderCmdID::GET_STATUS_CMD);
}

int KSM_Reader::FnKSMReaderSendEjectToFront()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "KSM");

    return ksmReaderCmd(KSMReaderCmdID::EJECT_TO_FRONT_CMD);
}

std::string KSM_Reader::KSMReaderCmdIDToString(KSM_Reader::KSMReaderCmdID cmdID)
{
    switch (cmdID)
    {
        case KSMReaderCmdID::INIT_CMD:
            return "INIT_CMD";
            break;
        case KSMReaderCmdID::CARD_PROHIBITED_CMD:
            return "CARD_PROHIBITED_CMD";
            break;
        case KSMReaderCmdID::CARD_ALLOWED_CMD:
            return "CARD_ALLOWED_CMD";
            break;
        case KSMReaderCmdID::CARD_ON_IC_CMD:
            return "CARD_ON_IC_CMD";
            break;
        case KSMReaderCmdID::IC_POWER_ON_CMD:
            return "IC_POWER_ON_CMD";
            break;
        case KSMReaderCmdID::WARM_RESET_CMD:
            return "WARM_RESET_CMD";
            break;
        case KSMReaderCmdID::SELECT_FILE1_CMD:
            return "SELECT_FILE1_CMD";
            break;
        case KSMReaderCmdID::SELECT_FILE2_CMD:
            return "SELECT_FILE2_CMD";
            break;
        case KSMReaderCmdID::READ_CARD_INFO_CMD:
            return "READ_CARD_INFO_CMD";
            break;
        case KSMReaderCmdID::READ_CARD_BALANCE_CMD:
            return "READ_CARD_BALANCE_CMD";
            break;
        case KSMReaderCmdID::IC_POWER_OFF_CMD:
            return "IC_POWER_OFF_CMD";
            break;
        case KSMReaderCmdID::EJECT_TO_FRONT_CMD:
            return "EJECT_TO_FRONT_CMD";
            break;
        case KSMReaderCmdID::GET_STATUS_CMD:
            return "GET_STATUS_CMD";
            break;
        default:
            return "UNKNOWN_CMD";
    }
}

int KSM_Reader::ksmReaderCmd(KSM_Reader::KSMReaderCmdID cmdID)
{
    int ret;

    if (!isCmdExecuting_.exchange(true))
    {
        std::stringstream ss;
        ss << __func__ << " Command ID : " << KSMReaderCmdIDToString(cmdID);
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "KSM");

        if (!pSerialPort_->is_open())
        {
            return -1;
        }

        std::vector<unsigned char> dataBuffer;

        switch (cmdID)
        {
            case KSMReaderCmdID::INIT_CMD:
            {
                dataBuffer = loadInitReader();
                break;
            }
            case KSMReaderCmdID::CARD_PROHIBITED_CMD:
            {
                dataBuffer = loadCardProhibited();
                break;
            }
            case KSMReaderCmdID::CARD_ALLOWED_CMD:
            {
                dataBuffer = loadCardAllowed();
                break;
            }
            case KSMReaderCmdID::CARD_ON_IC_CMD:
            {
                dataBuffer = loadCardOnIc();
                break;
            }
            case KSMReaderCmdID::IC_POWER_ON_CMD:
            {
                dataBuffer = loadICPowerOn();
                break;
            }
            case KSMReaderCmdID::WARM_RESET_CMD:
            {
                dataBuffer = loadWarmReset();
                break;
            }
            case KSMReaderCmdID::SELECT_FILE1_CMD:
            {
                dataBuffer = loadSelectFile1();
                break;
            }
            case KSMReaderCmdID::SELECT_FILE2_CMD:
            {
                dataBuffer = loadSelectFile2();
                break;
            }
            case KSMReaderCmdID::READ_CARD_INFO_CMD:
            {
                dataBuffer = loadReadCardInfo();
                break;
            }
            case KSMReaderCmdID::READ_CARD_BALANCE_CMD:
            {
                dataBuffer = loadReadCardBalance();
                break;
            }
            case KSMReaderCmdID::IC_POWER_OFF_CMD:
            {
                dataBuffer = loadICPowerOff();
                break;
            }
            case KSMReaderCmdID::EJECT_TO_FRONT_CMD:
            {
                dataBuffer = loadEjectToFront();
                break;
            }
            case KSMReaderCmdID::GET_STATUS_CMD:
            {
                dataBuffer = loadGetStatus();
                break;
            }
            default:
            {
                std::stringstream ss;
                ss << __func__ << " : Command not found.";
                Logger::getInstance()->FnLog(ss.str());
                Logger::getInstance()->FnLog(ss.str(), logFileName_, "KSM");

                return static_cast<int>(KSMReaderCmdRetCode::KSMReaderRecv_CmdNotFound);
                break;
            }
        }

        // Command to send and read back
        KSM_Reader::ReadResult result;
        ret = static_cast<int>(KSMReaderCmdRetCode::KSMReaderRecv_NoResp);

        result.data = {};
        result.success = false;
        
        readerCmdSend(dataBuffer);
        result = ksmReaderReadWithTimeout(2000);

        resetRxBuffer();

        isCmdExecuting_.store(false);

        if (result.success)
        {
            ret = static_cast<int>(ksmReaderHandleCmdResponse(cmdID, result.data));
        }
    }
    else
    {
        Logger::getInstance()->FnLog("Cmd cannot send, there is already one Cmd executing.", logFileName_, "KSM");
        ret = static_cast<int>(KSMReaderCmdRetCode::KSMReaderSend_Failed);
    }

    return ret;
}

KSM_Reader::KSMReaderCmdRetCode KSM_Reader::ksmReaderHandleCmdResponse(KSM_Reader::KSMReaderCmdID cmd, const std::vector<char>& dataBuff)
{
    KSMReaderCmdRetCode retCode = KSMReaderCmdRetCode::KSMReaderRecv_NoResp;

    std::stringstream receivedRespStream;
    receivedRespStream << "Received Buffer Data : " << Common::getInstance()->FnGetVectorCharToHexString(dataBuff);
    Logger::getInstance()->FnLog(receivedRespStream.str(), logFileName_, "KSM");
    std::stringstream receivedRespNumBytesStream;
    receivedRespNumBytesStream << "Received Buffer size : " << dataBuff.size();
    Logger::getInstance()->FnLog(receivedRespNumBytesStream.str(), logFileName_, "KSM");

    if (dataBuff.size() > 3)
    {
        if (dataBuff[0] != KSM_Reader::STX)
        {
            Logger::getInstance()->FnLog("Rx Frame : STX error.", logFileName_, "KSM");
            return retCode;
        }

        if (dataBuff[dataBuff.size() - 1] != KSM_Reader::ETX)
        {
            Logger::getInstance()->FnLog("Rx Frame : ETX error.", logFileName_, "KSM");
            return retCode;
        }

        std::size_t startIdx = 1;
        std::size_t endIdx = dataBuff.size() - 1;
        std::size_t len = endIdx - startIdx;

        std::vector<char> recvCmd(len);
        std::copy(dataBuff.begin() + startIdx, dataBuff.begin() + endIdx, recvCmd.begin());

        std::stringstream rxCmdSS;
        rxCmdSS << "Rx command : " << Common::getInstance()->FnGetVectorCharToHexString(recvCmd);
        Logger::getInstance()->FnLog(rxCmdSS.str(), logFileName_, "KSM");
        char cmdCodeMSB = recvCmd[2];
        char cmdCodeLSB = recvCmd[3];

        if (cmdCodeMSB == 0x2F)
        {
            std::stringstream logMsg;
            if (cmdCodeLSB == 0x31)
            {
                logMsg << "Rx Response : Card prohibited.";
                retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
            }
            else if (cmdCodeLSB == 0x33)
            {
                logMsg << "Rx Response : Card Allowed.";
                retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
            }
            Logger::getInstance()->FnLog(logMsg.str(), logFileName_, "KSM");
        }
        else if (cmdCodeMSB == 0x30)
        {
            std::stringstream logMsg;
            if (cmdCodeLSB == 0x31)
            {
                logMsg << "Rx Response : Init.";
                retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
            }
            Logger::getInstance()->FnLog(logMsg.str(), logFileName_, "KSM");
        }
        else if (cmdCodeMSB == 0x31)
        {
            if (cmdCodeLSB == 0x30)
            {
                std::stringstream logMsg;
                char status = recvCmd[4];

                if (status == 0x4A || status == 0x4B)
                {
                    if (cardPresented_ == false)
                    {
                        cardPresented_ = true;
                        // Temp: raise event Card In
                        EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderCardIn", true);
                        logMsg << "Rx Response : Get Status : Card In";
                    }
                    retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                }
                else if (status == 0x4E)
                {
                    if (cardPresented_ == true)
                    {
                        cardPresented_ = false;
                        // Temp: raise event Card Take Away
                        EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderCardTakeAway", true);
                        logMsg << "Rx Response : Get Status : Card Out";
                    }
                    retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                }
                else
                {
                    logMsg << "Rx Response : Card Occupied.";
                }

                Logger::getInstance()->FnLog(logMsg.str(), logFileName_, "KSM");
            }
        }
        else if (cmdCodeMSB == 0x32)
        {
            std::stringstream logMsg;

            if (cmdCodeLSB == 0x2F)
            {
                retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                logMsg << "Rx Response : Card On.";
            }
            else if (cmdCodeLSB == 0x31)
            {
                // Temp: Raise event card out
                EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderCardOut", true);
                retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                logMsg << "Rx Response : Eject to front.";
            }

            Logger::getInstance()->FnLog(logMsg.str(), logFileName_, "KSM");
        }
        else if (cmdCodeMSB == 0x33)
        {
            std::stringstream logMsg;

            if (cmdCodeLSB == 0x30)
            {
                retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                logMsg << "Rx Response : IC Power On.";
            }
            else if (cmdCodeLSB == 0x31)
            {
                retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                logMsg << "Rx Response : IC Power Off.";
            }

            Logger::getInstance()->FnLog(logMsg.str(), logFileName_, "KSM");
        }
        else if (cmdCodeMSB == 0x37)
        {
            std::stringstream logMsg;

            if (cmdCodeLSB == 0x2F)
            {
                retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                logMsg << "Rx Response : Warm Reset.";
            }
            else if (cmdCodeLSB == 0x31)
            {
                char status = recvCmd[4];
                if (status == 0x4E)
                {
                    retCode = KSMReaderCmdRetCode::KSMReaderRecv_NoResp;
                    logMsg << "Rx Response : Card Read Error.";
                }
                else if (cmd == KSMReaderCmdID::SELECT_FILE1_CMD)
                {
                    retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                    logMsg << "Rx Response : Select File 1.";
                }
                else if (cmd == KSMReaderCmdID::SELECT_FILE2_CMD)
                {
                    retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                    logMsg << "Rx Response : Select File 2.";
                }
                else if (cmd == KSMReaderCmdID::READ_CARD_INFO_CMD)
                {
                    std::vector<char>::const_iterator startPos;
                    std::vector<char>::const_iterator endPos;
                    
                    // Get Card Number
                    startPos = recvCmd.begin() + 8;
                    endPos = startPos + 8;
                    std::vector<char> cardNumber(startPos, endPos);
                    cardNum_ = Common::getInstance()->FnGetVectorCharToHexString(cardNumber);

                    // Get Expiry Date
                    startPos = recvCmd.begin() + 19;
                    endPos = startPos + 3;
                    std::vector<char> expireDate(startPos, endPos);
                    std::string expireDateStr = Common::getInstance()->FnGetVectorCharToHexString(expireDate);
                    std::string year = expireDateStr.substr(0, 2);
                    std::string month = expireDateStr.substr(2, 2);
                    std::string expmonth = expireDateStr.substr(4, 2);

                    int intYear = std::atoi(year.c_str());
                    if (intYear > 90)
                    {
                        intYear = 1900 + intYear;
                    }
                    else
                    {
                        intYear = 2000 + intYear;
                    }

                    // Calculate the expiry year and month provided by expmonth from card info
                    int intMonth = std::atoi(month.c_str());
                    int intExpMonth = std::atoi(expmonth.c_str());

                    intExpMonth = intExpMonth + intMonth;
                    int intExpYear = intYear + static_cast<int>(intExpMonth / 12);
                    intExpMonth = intExpMonth % 12;

                    // Concatenate year and month ,eg (2024 * 100) + 3 = 202400 + 3 = 202403
                    cardExpiryYearMonth_ = (intExpYear * 100) + intExpMonth;

                    if (std::to_string(cardExpiryYearMonth_) > Common::getInstance()->FnGetDateTimeFormat_yyyymm())
                    {
                        cardExpired_ = false;
                    }
                    else
                    {
                        cardExpired_ = true;
                    }

                    retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                    logMsg << "Rx Response : Card Read Info status.";
                }
                else if (cmd == KSMReaderCmdID::READ_CARD_BALANCE_CMD)
                {
                    std::vector<char>::const_iterator startPos = recvCmd.begin() + 8;
                    std::vector<char>::const_iterator endPos = startPos + 3;
                    
                    std::vector<char> cardBalance(startPos, endPos);
                    std::string cardBalanceStr = Common::getInstance()->FnGetVectorCharToHexString(cardBalance);

                    cardBalance_ = std::atoi(cardBalanceStr.c_str());
                    if (cardBalance_ < 0)
                    {
                        cardBalance_ = 65536 + cardBalance_;
                    }

                    retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                    logMsg << "Rx Response : Card Read Balance status.";
                    // Temp: raise event card info
                    EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderCardInfo", true);
                }
                Logger::getInstance()->FnLog(logMsg.str(), logFileName_, "KSM");
            }
        }
        // Unknown Command Response
        else
        {
            Logger::getInstance()->FnLog("Rx Response : Unknown Command.", logFileName_, "KSM");
        }
        
    }
    else
    {
        Logger::getInstance()->FnLog("Error : Received data buffer size LESS than 3.", logFileName_, "KSM");
    }

    return retCode;
}

KSM_Reader::ReadResult KSM_Reader::ksmReaderReadWithTimeout(int milliseconds)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "KSM");

    std::vector<char> buffer(1024);
    //std::size_t bytes_transferred = 0;
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
                    ss << __func__ << "Async Read error : " << error.message();
                    Logger::getInstance()->FnLog(ss.str(), logFileName_, "KSM");
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
            ss << __func__ << "Async Read Timeout Occurred.";
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "KSM");
            pSerialPort_->cancel();
            resetRxBuffer();
            success = false;
        }
        else
        {
            if (responseIsComplete(buffer, total_bytes_transferred))
            {
                std::stringstream ss;
                ss << __func__ << "Async Read Timeout Occurred - Rx Completed.";
                Logger::getInstance()->FnLog(ss.str(), logFileName_, "KSM");
                pSerialPort_->cancel();
                success = true;
            }
            else
            {
                std::stringstream ss;
                ss << __func__ << "Async Read Timeout Occurred - Rx Not Completed.";
                Logger::getInstance()->FnLog(ss.str(), logFileName_, "KSM");
                pSerialPort_->cancel();
                resetRxBuffer();
                success = false;
            }
        }

        io_serial_context.post([this]() {
            io_serial_context.stop();
        });
    });

    io_serial_context.run();

    io_serial_context.reset();

    std::vector<char> retBuff(getRxBuff(), getRxBuff() + getRxNum());

    return {retBuff, success};
}

bool KSM_Reader::responseIsComplete(const std::vector<char>& buffer, std::size_t bytesTransferred)
{
    bool ret;
    int ret_bytes;

    for (int i = 0; i < bytesTransferred; i++)
    {
        ret_bytes = receiveRxDataByte(buffer[i]);

        if (ret_bytes != 0)
        {
            ret = true;
            break;
        }
        else
        {
            ret = false;
        }
    }

    return ret;

}

int KSM_Reader::receiveRxDataByte(char c)
{
    int ret = 0;

    if (c == KSM_Reader::ACK)
    {
        sendEnq();
    }
    else if ((rxBuff[RxNum_ - 1] == KSM_Reader::ETX) && (c == recvbcc_))
    {
        ret = RxNum_;
        recvbcc_ = 0;
    }
    else
    {
        RxNum_ %= KSM_Reader::RX_BUF_SIZE;
        rxBuff[RxNum_++] = c;
        recvbcc_ ^= c;
    }

    return ret;
}

std::vector<unsigned char> KSM_Reader::loadInitReader()
{
    std::vector<unsigned char> dataBuff(4, 0);

    dataBuff[0] = 0x00;
    dataBuff[1] = 0x02;
    dataBuff[2] = 0x30;
    dataBuff[3] = 0x31;

    return dataBuff;
}

std::vector<unsigned char> KSM_Reader::loadCardProhibited()
{
    std::vector<unsigned char> dataBuff(5, 0);

    dataBuff[0] = 0x00;
    dataBuff[1] = 0x03;
    dataBuff[2] = 0x2F;
    dataBuff[3] = 0x31;
    dataBuff[4] = 0x31;

    return dataBuff;
}

std::vector<unsigned char> KSM_Reader::loadCardAllowed()
{
    std::vector<unsigned char> dataBuff(5, 0);

    dataBuff[0] = 0x00;
    dataBuff[1] = 0x03;
    dataBuff[2] = 0x2F;
    dataBuff[3] = 0x33;
    dataBuff[4] = 0x30;

    return dataBuff;
}

std::vector<unsigned char> KSM_Reader::loadCardOnIc()
{
    std::vector<unsigned char> dataBuff(4, 0);

    dataBuff[0] = 0x00;
    dataBuff[1] = 0x02;
    dataBuff[2] = 0x32;
    dataBuff[3] = 0x2F;

    return dataBuff;
}

std::vector<unsigned char> KSM_Reader::loadICPowerOn()
{
    std::vector<unsigned char> dataBuff(4, 0);

    dataBuff[0] = 0x00;
    dataBuff[1] = 0x02;
    dataBuff[2] = 0x33;
    dataBuff[3] = 0x30;

    return dataBuff;
}

std::vector<unsigned char> KSM_Reader::loadWarmReset()
{
    std::vector<unsigned char> dataBuff(4, 0);

    dataBuff[0] = 0x00;
    dataBuff[1] = 0x02;
    dataBuff[2] = 0x37;
    dataBuff[3] = 0x2F;

    return dataBuff;
}

std::vector<unsigned char> KSM_Reader::loadSelectFile1()
{
    std::vector<unsigned char> dataBuff(13, 0);

    dataBuff[0] = 0x00;
    dataBuff[1] = 0x0B;
    dataBuff[2] = 0x37;
    dataBuff[3] = 0x31;
    dataBuff[4] = 0x00;
    dataBuff[5] = 0x07;
    dataBuff[6] = 0x00;
    dataBuff[7] = 0xA4;
    dataBuff[8] = 0x01;
    dataBuff[9] = 0x00;
    dataBuff[10] = 0x02;
    dataBuff[11] = 0x01;
    dataBuff[12] = 0x18;

    return dataBuff;
}

std::vector<unsigned char> KSM_Reader::loadSelectFile2()
{
    std::vector<unsigned char> dataBuff(13, 0);

    dataBuff[0] = 0x00;
    dataBuff[1] = 0x0B;
    dataBuff[2] = 0x37;
    dataBuff[3] = 0x31;
    dataBuff[4] = 0x00;
    dataBuff[5] = 0x07;
    dataBuff[6] = 0x00;
    dataBuff[7] = 0xA4;
    dataBuff[8] = 0x02;
    dataBuff[9] = 0x00;
    dataBuff[10] = 0x02;
    dataBuff[11] = 0x00;
    dataBuff[12] = 0x01;

    return dataBuff;
}

std::vector<unsigned char> KSM_Reader::loadReadCardInfo()
{
    std::vector<unsigned char> dataBuff(11, 0);

    dataBuff[0] = 0x00;
    dataBuff[1] = 0x09;
    dataBuff[2] = 0x37;
    dataBuff[3] = 0x31;
    dataBuff[4] = 0x00;
    dataBuff[5] = 0x05;
    dataBuff[6] = 0x00;
    dataBuff[7] = 0xB0;
    dataBuff[8] = 0x00;
    dataBuff[9] = 0x00;
    dataBuff[10] = 0x10;

    return dataBuff;
}

std::vector<unsigned char> KSM_Reader::loadReadCardBalance()
{
    std::vector<unsigned char> dataBuff(15, 0);

    dataBuff[0] = 0x00;
    dataBuff[1] = 0x0D;
    dataBuff[2] = 0x37;
    dataBuff[3] = 0x31;
    dataBuff[4] = 0x00;
    dataBuff[5] = 0x09;
    dataBuff[6] = 0x80;
    dataBuff[7] = 0x32;
    dataBuff[8] = 0x00;
    dataBuff[9] = 0x03;
    dataBuff[10] = 0x04;
    dataBuff[11] = 0x00;
    dataBuff[12] = 0x00;
    dataBuff[13] = 0x00;
    dataBuff[14] = 0x00;

    return dataBuff;
}

std::vector<unsigned char> KSM_Reader::loadICPowerOff()
{
    std::vector<unsigned char> dataBuff(4, 0);

    dataBuff[0] = 0x00;
    dataBuff[1] = 0x02;
    dataBuff[2] = 0x33;
    dataBuff[3] = 0x31;

    return dataBuff;
}

std::vector<unsigned char> KSM_Reader::loadEjectToFront()
{
    std::vector<unsigned char> dataBuff(4, 0);

    dataBuff[0] = 0x00;
    dataBuff[1] = 0x02;
    dataBuff[2] = 0x32;
    dataBuff[3] = 0x31;

    return dataBuff;
}

std::vector<unsigned char> KSM_Reader::loadGetStatus()
{
    std::vector<unsigned char> dataBuff(4, 0);

    dataBuff[0] = 0x00;
    dataBuff[1] = 0x02;
    dataBuff[2] = 0x31;
    dataBuff[3] = 0x30;

    return dataBuff;
}

void KSM_Reader::readerCmdSend(const std::vector<unsigned char>& dataBuff)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "KSM");

    if (!pSerialPort_->is_open())
    {
        return;
    }

    unsigned char txBCC;
    unsigned char tempChar;
    int i = 0;
    memset(txBuff, 0, sizeof(txBuff));

    txBuff[i++] = KSM_Reader::STX;
    txBCC = KSM_Reader::STX;

    for (int j = 0; j < dataBuff.size(); j++)
    {
        tempChar = dataBuff[j];

        txBCC ^= tempChar;
        txBuff[i++] = tempChar;
    }

    txBuff[i++] = KSM_Reader::ETX;
    txBCC ^= KSM_Reader::ETX;

    txBuff[i++] = txBCC;
    TxNum_ = i;

    std::stringstream txStream;
    txStream << "TX Buffer Data : " << Common::getInstance()->FnGetUCharArrayToHexString(getTxBuff(), getTxNum());
    Logger::getInstance()->FnLog(txStream.str(), logFileName_, "KSM");
    std::stringstream txStreamBytes;
    txStreamBytes << "TX Buffer size : " << getTxNum();
    Logger::getInstance()->FnLog(txStreamBytes.str(), logFileName_, "KSM");
    /* Send Tx Data buffer via serial */
    boost::asio::write(*pSerialPort_, boost::asio::buffer(getTxBuff(), getTxNum()));
}

void KSM_Reader::resetRxBuffer()
{
    memset(rxBuff, 0, sizeof(rxBuff));
    RxNum_ = 0;
}

unsigned char* KSM_Reader::getTxBuff()
{
    return txBuff;
}

unsigned char* KSM_Reader::getRxBuff()
{
    return rxBuff;
}

int KSM_Reader::getTxNum()
{
    return TxNum_;
}

int KSM_Reader::getRxNum()
{
    return RxNum_;
}

void KSM_Reader::handleReadCardStatusTimerExpiration()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "KSM");

    if (continueReadFlag_.load())
    {
        int iRet = FnKSMReaderSendGetStatus();

        startReadCardStatusTimer(2000);
    }
}

void KSM_Reader::startReadCardStatusTimer(int milliseconds)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "KSM");
    
    std::unique_ptr<boost::asio::io_context> timerIOContext_ = std::make_unique<boost::asio::io_context>();
    std::thread timerThread([this, milliseconds, timerIOContext_ = std::move(timerIOContext_)]() mutable {
        std::unique_ptr<boost::asio::deadline_timer> periodicSendReadCardCmdTimer_ = std::make_unique<boost::asio::deadline_timer>(*timerIOContext_);
        periodicSendReadCardCmdTimer_->expires_from_now(boost::posix_time::milliseconds(milliseconds));
        periodicSendReadCardCmdTimer_->async_wait([this](const boost::system::error_code& error) {
                if (!error) {
                    handleReadCardStatusTimerExpiration();
                }
        });

        timerIOContext_->run();
    });
    timerThread.detach();
}

int KSM_Reader::FnKSMReaderReadCardInfo()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "KSM");

    int retCode = 1;
    continueReadFlag_.store(false);

    if (ksmReaderCmd(KSMReaderCmdID::CARD_ON_IC_CMD) != 1)
    {
        retCode = -1;
        goto cleanup;
    }

    if (ksmReaderCmd(KSMReaderCmdID::IC_POWER_ON_CMD) != 1)
    {
        retCode = -1;
        goto cleanup;
    }

    if (ksmReaderCmd(KSMReaderCmdID::WARM_RESET_CMD) != 1)
    {
        retCode = -1;
        goto cleanup;
    }

    if (ksmReaderCmd(KSMReaderCmdID::SELECT_FILE1_CMD) != 1)
    {
        retCode = -1;
        goto cleanup;
    }

    if (ksmReaderCmd(KSMReaderCmdID::SELECT_FILE2_CMD) != 1)
    {
        retCode = -1;
        goto cleanup;
    }

    if (ksmReaderCmd(KSMReaderCmdID::READ_CARD_INFO_CMD) != 1)
    {
        retCode = -1;
        goto cleanup;
    }

    if (ksmReaderCmd(KSMReaderCmdID::READ_CARD_BALANCE_CMD) != 1)
    {
        retCode = -1;
        goto cleanup;
    }

    if (ksmReaderCmd(KSMReaderCmdID::IC_POWER_OFF_CMD) != 1)
    {
        retCode = -1;
        goto cleanup;
    }

    if (ksmReaderCmd(KSMReaderCmdID::EJECT_TO_FRONT_CMD) != 1)
    {
        retCode = -1;
        goto cleanup;
    }

cleanup:
    return retCode;
}

int KSM_Reader::FnKSMReaderEnable(bool enable)
{
    std::stringstream ss;
    ss << __func__ << " : " << enable;
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "KSM");

    int iRet = -1;

    if (enable)
    {
        continueReadFlag_.store(true);
        iRet = ksmReaderCmd(KSMReaderCmdID::CARD_ALLOWED_CMD);
        
        // Start timer to check card status
        startReadCardStatusTimer(100);
    }
    else
    {
        continueReadFlag_.store(false);
        iRet = ksmReaderCmd(KSMReaderCmdID::CARD_PROHIBITED_CMD);
    }

    return iRet;
}

std::string KSM_Reader::FnKSMReaderGetCardNum()
{
    return cardNum_;
}

int KSM_Reader::FnKSMReaderGetCardExpiryDate()
{
    return cardExpiryYearMonth_;
}

long KSM_Reader::FnKSMReaderGetCardBalance()
{
    return cardBalance_;
}

bool KSM_Reader::FnKSMReaderGetCardExpired()
{
    return cardExpired_;
}

void KSM_Reader::FnKSMReaderStartGetStatus()
{
    continueReadFlag_.store(true);
    startReadCardStatusTimer(100);
}
