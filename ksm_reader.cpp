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
#include <iomanip>

KSM_Reader* KSM_Reader::ksm_reader_ = nullptr;
std::mutex KSM_Reader::mutex_;
std::mutex KSM_Reader::currentCmdMutex_;

KSM_Reader::KSM_Reader()
    : ioContext_(),
    strand_(boost::asio::make_strand(ioContext_)),
    workGuard_(boost::asio::make_work_guard(ioContext_)),
    write_in_progress_(false),
    currentState_(KSM_Reader::STATE::IDLE),
    currentCmd(KSM_Reader::KSMReaderCmdID::INIT_CMD),
    ackRecv_(false),
    rspRecv_(false),
    ackTimer_(ioContext_),
    rspTimer_(ioContext_),
    serialWriteTimer_(ioContext_),
    TxNum_(0),
    RxNum_(0),
    cardPresented_(false),
    cardNum_(""),
    cardExpiryYearMonth_(0),
    cardExpired_(false),
    cardBalance_(0),
    continueReadCardFlag_(false),
    blockGetStatusCmdLogFlag_(false),
    lastSerialReadTime_(std::chrono::steady_clock::now()),
    serialWriteDelayTimer_(ioContext_)
{
    logFileName_ = "ksmReader";
    resetRxState();
    memset(txBuff, 0, sizeof(txBuff));
}

KSM_Reader* KSM_Reader::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (ksm_reader_ == nullptr)
    {
        ksm_reader_ = new KSM_Reader();
    }
    return ksm_reader_;
}

int KSM_Reader::FnKSMReaderInit(unsigned int baudRate, const std::string& comPortName)
{
    int ret = static_cast<int>(KSMReaderCmdRetCode::KSMReaderComm_Error);

    pSerialPort_ = std::make_unique<boost::asio::serial_port>(ioContext_, comPortName);

    try
    {   
        pSerialPort_->set_option(boost::asio::serial_port_base::baud_rate(baudRate));
        pSerialPort_->set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
        pSerialPort_->set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
        pSerialPort_->set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
        pSerialPort_->set_option(boost::asio::serial_port_base::character_size(8));

        Logger::getInstance()->FnCreateLogFile(logFileName_);

        if (pSerialPort_->is_open())
        {
            std::stringstream ss;
            ss << "Successfully open serial port for KSM Reader Communication: " << comPortName;
            Logger::getInstance()->FnLog(ss.str());
            ksmLogger(__func__);
            startIoContextThread();
            startRead();
            enqueueCommand(KSMReaderCmdID::INIT_CMD);
            ret = 1;
        }
        else
        {
            std::stringstream ss;
            ss << "Failed to open serial port for KSM Reader Communication: " << comPortName;
            Logger::getInstance()->FnLog(ss.str());
            ksmLogger(__func__);
        }
    }
    catch (const boost::system::system_error& e) // Catch Boost.Asio system errors
    {
        std::stringstream ss;
        ss << __func__ << ", Boost Asio Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());;
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }

    return ret;
}

void KSM_Reader::startIoContextThread()
{
    if (!ioContextThread_.joinable())
    {
        ioContextThread_ = std::thread([this]() { 
            ioContext_.run(); 
        });
    }
}

void KSM_Reader::FnKSMReaderClose()
{
    ksmLogger(__func__);

    workGuard_.reset();
    ioContext_.stop();
    if (ioContextThread_.joinable())
    {
        ioContextThread_.join();
    }
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

void KSM_Reader::ksmReaderCmd(KSMReaderCmdID cmdID)
{
    std::stringstream ss;
    ss << __func__ << " Command ID : ( " << KSMReaderCmdIDToString(cmdID) << " )";
    ksmLogger(ss.str());

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
            ksmLogger(ss.str());
            break;
        }
    }

    if (!dataBuffer.empty())
    {
        readerCmdSend(dataBuffer);
    }
}

void KSM_Reader::readerCmdSend(const std::vector<unsigned char>& dataBuff)
{
    ksmLogger(__func__);

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
    
    // Send the txBuff data to the enqueueWrite function
    std::vector<uint8_t> txData(getTxBuff(), getTxBuff() + getTxNum());
    enqueueWrite(txData);
}

void KSM_Reader::enqueueWrite(const std::vector<uint8_t>& data)
{
    boost::asio::dispatch(strand_, [this, data = std::move(data)]() {
        bool wasWriting_ = this->write_in_progress_;
        writeQueue_.push(std::move(data));
        if (!wasWriting_)
        {
            startWrite();
        }
    });
}

void KSM_Reader::startWrite()
{
    if (writeQueue_.empty())
    {
        ksmLogger(__func__ + std::string(" Write Queue is empty."));
        write_in_progress_ = false;  // Ensure flag is reset when queue is empty
        return;
    }

    write_in_progress_ = true;

    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastRead = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSerialReadTime_).count();

    // Check if less than 100 milliseconds
    if (timeSinceLastRead < 100)
    {
        auto boostTime = boost::posix_time::milliseconds(100 - timeSinceLastRead);
        serialWriteDelayTimer_.expires_from_now(boostTime);

        // Add debug logging to check if the timer is being set properly
        std::ostringstream oss;
        oss << "Setting delay timer for " << (100 - timeSinceLastRead) << " ms";
        ksmLogger(oss.str());

        serialWriteDelayTimer_.async_wait(boost::asio::bind_executor(strand_, 
                [this](const boost::system::error_code& /*e*/) {
                    ksmLogger("Timer expired");
                    boost::asio::post(strand_, [this]() { startWrite(); });
            }));
        
        return;
    }

    const auto& data = writeQueue_.front();
    std::ostringstream oss;
    oss << "Data sent (" << data.size() << " bytes): " << Common::getInstance()->FnGetDisplayVectorCharToHexString(data);
    ksmLogger(oss.str());
    boost::asio::async_write(*pSerialPort_,
                            boost::asio::buffer(data),
                            boost::asio::bind_executor(strand_,
                                                        std::bind(&KSM_Reader::writeEnd, this,
                                                                    std::placeholders::_1,
                                                                    std::placeholders::_2)));
}

void KSM_Reader::writeEnd(const boost::system::error_code& error, std::size_t bytesTransferred)
{
    if (!error)
    {
        writeQueue_.pop();
        processEvent(EVENT::WRITE_COMPLETED);
    }
    else
    {
        std::ostringstream oss;
        oss << "Serial Write error: " << error.message();
        ksmLogger(oss.str());
        processEvent(EVENT::WRITE_FAILED);
    }
    write_in_progress_ = false;
}

void KSM_Reader::startRead()
{
    boost::asio::post(strand_, [this]() {
        pSerialPort_->async_read_some(
            boost::asio::buffer(readBuffer_, readBuffer_.size()),
            boost::asio::bind_executor(strand_,
                                        std::bind(&KSM_Reader::readEnd, this,
                                        std::placeholders::_1,
                                        std::placeholders::_2)));
    });
}

void KSM_Reader::readEnd(const boost::system::error_code& error, std::size_t bytesTransferred)
{
    lastSerialReadTime_ = std::chrono::steady_clock::now();

    if (!error)
    {
        std::vector<char> data(readBuffer_.begin(), readBuffer_.begin() + bytesTransferred);

        if (responseIsComplete(data, bytesTransferred))
        {
            rspRecv_.store(true);
            rspTimer_.cancel();
            // Discard last element BCC from received data buffer
            if (!data.empty())
            {
                data.pop_back();
            }
            ksmReaderHandleCmdResponse(getCurrentCmd(), data);
            resetRxState();
        }
    }
    else
    {
        std::ostringstream oss;
        oss << "Serial Read error: " << error.message();
        ksmLogger(oss.str());
    }

    startRead();
}

void KSM_Reader::resetRxState()
{
    rxState_ = RX_STATE::IDLE;
    memset(rxBuff, 0, sizeof(rxBuff));
    RxNum_ = 0;
    recvbcc_ = 0;
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

KSM_Reader::KSMReaderCmdRetCode KSM_Reader::ksmReaderHandleCmdResponse(KSM_Reader::KSMReaderCmdID cmd, const std::vector<char>& dataBuff)
{
    KSMReaderCmdRetCode retCode = KSMReaderCmdRetCode::KSMReaderRecv_NoResp;

    if (dataBuff.size() > 3)
    {
        if (dataBuff[0] != KSM_Reader::STX)
        {
            ksmLogger("Rx Frame : STX error.");
            return retCode;
        }

        if (dataBuff[dataBuff.size() - 1] != KSM_Reader::ETX)
        {
            ksmLogger("Rx Frame : ETX error.");
            return retCode;
        }

        std::size_t startIdx = 1;
        std::size_t endIdx = dataBuff.size() - 1;
        std::size_t len = endIdx - startIdx;

        std::vector<char> recvCmd(len);
        std::copy(dataBuff.begin() + startIdx, dataBuff.begin() + endIdx, recvCmd.begin());

        std::stringstream rxCmdSS;
        rxCmdSS << "Rx command (" << recvCmd.size() << " bytes) :" << Common::getInstance()->FnGetVectorCharToHexString(recvCmd);
        ksmLogger(rxCmdSS.str());
        char cmdCodeMSB = recvCmd[2];
        char cmdCodeLSB = recvCmd[3];

        if (cmdCodeMSB == 0x2F)
        {
            std::stringstream logMsg;
            if (cmdCodeLSB == 0x31)
            {
                logMsg << "Rx Response : Card prohibited.";
                retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderCardProhibited", true);
            }
            else if (cmdCodeLSB == 0x33)
            {
                logMsg << "Rx Response : Card Allowed.";
                retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderCardAllowed", true);
            }
            ksmLogger(logMsg.str());
        }
        else if (cmdCodeMSB == 0x30)
        {
            std::stringstream logMsg;
            if (cmdCodeLSB == 0x31)
            {
                logMsg << "Rx Response : Init.";
                retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderInit", true);
            }
            else
            {
                logMsg << "Rx Response : Init Error.";
                EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderInit", false);
            }
            ksmLogger(logMsg.str());
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
                        continueReadCardFlag_.store(false);
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
                        EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderCardTakeAway", true);
                        logMsg << "Rx Response : Get Status : Card Out";
                    }
                    else
                    {
                        logMsg << "Rx Response : Get Status : No Card In.";
                        if (continueReadCardFlag_.load() == true)
                        {
                            enqueueCommand(KSMReaderCmdID::GET_STATUS_CMD);
                            logMsg << " Continue detect card.";
                        }
                        else
                        {
                            logMsg << " Stop detect card.";
                        }
                    }
                    retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                }
                else
                {
                    logMsg << "Rx Response : Card Occupied.";
                    if (continueReadCardFlag_.load() == true)
                    {
                        enqueueCommand(KSMReaderCmdID::GET_STATUS_CMD);
                        logMsg << " Continue detect card.";
                    }
                    else
                    {
                        logMsg << " Stop detect card.";
                    }
                    retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                }

                ksmLogger(logMsg.str());
            }
        }
        else if (cmdCodeMSB == 0x32)
        {
            std::stringstream logMsg;

            if (cmdCodeLSB == 0x2F)
            {
                EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderCardOnIc", true);
                retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                logMsg << "Rx Response : Card On.";

                enqueueCommand(KSMReaderCmdID::IC_POWER_ON_CMD);
            }
            else if (cmdCodeLSB == 0x31)
            {
                // Temp: Raise event card out
                EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderEjectToFront", true);
                EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderCardOut", true);
                retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                logMsg << "Rx Response : Eject to front.";
            }

            ksmLogger(logMsg.str());
        }
        else if (cmdCodeMSB == 0x33)
        {
            std::stringstream logMsg;

            if (cmdCodeLSB == 0x30)
            {
                EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderIcPowerOn", true);
                retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                logMsg << "Rx Response : IC Power On.";

                enqueueCommand(KSMReaderCmdID::WARM_RESET_CMD);
            }
            else if (cmdCodeLSB == 0x31)
            {
                EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderIcPowerOff", true);
                retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                logMsg << "Rx Response : IC Power Off.";

                enqueueCommand(KSMReaderCmdID::EJECT_TO_FRONT_CMD);
            }

            ksmLogger(logMsg.str());
        }
        else if (cmdCodeMSB == 0x37)
        {
            std::stringstream logMsg;

            if (cmdCodeLSB == 0x2F)
            {
                EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderWarmReset", true);
                retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                logMsg << "Rx Response : Warm Reset.";

                enqueueCommand(KSMReaderCmdID::SELECT_FILE1_CMD);
            }
            else if (cmdCodeLSB == 0x31)
            {
                char status = recvCmd[4];
                if (status == 0x4E)
                {
                    retCode = KSMReaderCmdRetCode::KSMReaderRecv_NoResp;
                    logMsg << "Rx Response : Card Read Error.";

                    handleCmdErrorOrTimeout(getCurrentCmd(), KSMReaderCmdRetCode::KSMReaderRecv_NoResp);
                }
                else if (cmd == KSMReaderCmdID::SELECT_FILE1_CMD)
                {
                    EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderSelectFile1", true);
                    retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                    logMsg << "Rx Response : Select File 1.";

                    enqueueCommand(KSMReaderCmdID::SELECT_FILE2_CMD);
                }
                else if (cmd == KSMReaderCmdID::SELECT_FILE2_CMD)
                {
                    EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderSelectFile2", true);
                    retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                    logMsg << "Rx Response : Select File 2.";

                    enqueueCommand(KSMReaderCmdID::READ_CARD_INFO_CMD);
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

                    EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderReadCardInfo", true);
                    retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                    logMsg << "Rx Response : Card Read Info status.";

                    enqueueCommand(KSMReaderCmdID::READ_CARD_BALANCE_CMD);
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

                    EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderReadCardBalance", true);
                    retCode = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
                    logMsg << "Rx Response : Card Read Balance status.";
                    EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderCardInfo", true);

                    enqueueCommand(KSMReaderCmdID::IC_POWER_OFF_CMD);
                }
            }
            ksmLogger(logMsg.str());
        }
        // Unknown Command Response
        else
        {
            ksmLogger("Rx Response : Unknown Command.");
        }
        
    }
    else
    {
        ksmLogger("Error : Received data buffer size LESS than 3.");
    }

    return retCode;
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

    switch (rxState_)
    {
        case KSM_Reader::RX_STATE::IDLE:
        {
            if (c == KSM_Reader::ACK)
            {
                ackRecv_.store(true);
                ackTimer_.cancel();
                sendEnq();
                rxState_ = KSM_Reader::RX_STATE::RECEIVING;
            }
            break;
        }
        case KSM_Reader::RX_STATE::RECEIVING:
        {
            if ((RxNum_ > 0 && rxBuff[RxNum_ - 1] == KSM_Reader::ETX) && (c == static_cast<unsigned char>(recvbcc_)))
            {
                rxBuff[RxNum_++] = static_cast<unsigned char>(c);
                ret = RxNum_;
                recvbcc_ = 0;
                KSM_Reader::RX_STATE::IDLE;
            }
            else
            {
                RxNum_ %= KSM_Reader::RX_BUF_SIZE;
                rxBuff[RxNum_++] = static_cast<unsigned char>(c);
                recvbcc_ ^= static_cast<unsigned char>(c);
            }
            break;
        }
    }

    return ret;
}

void KSM_Reader::sendEnq()
{
    ksmLogger(__func__);

    std::vector<uint8_t> data = { static_cast<uint8_t>(KSM_Reader::ENQ) };
    enqueueWrite(data);
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

std::string KSM_Reader::eventToString(EVENT event)
{
    std::string eventStr = "Unknown Event";

    switch (event)
    {
        case EVENT::COMMAND_ENQUEUED:
        {
            eventStr = "COMMAND_ENQUEUED";
            break;
        }
        case EVENT::WRITE_COMPLETED:
        {
            eventStr = "WRITE_COMPLETED";
            break;
        }
        case EVENT::WRITE_FAILED:
        {
            eventStr = "WRITE_FAILED";
            break;
        }
        case EVENT::ACK_TIMEOUT:
        {
            eventStr = "ACK_TIMEOUT";
            break;
        }
        case EVENT::ACK_TIMER_CANCELLED_ACK_RECEIVED:
        {
            eventStr = "ACK_TIMER_CANCELLED_ACK_RECEIVED";
            break;
        }
        case EVENT::RESPONSE_TIMEOUT:
        {
            eventStr = "RESPONSE_TIMEOUT";
            break;
        }
        case EVENT::RESPONSE_TIMER_CANCELLED_RSP_RECEIVED:
        {
            eventStr = "RESPONSE_TIMER_CANCELLED_RSP_RECEIVED";
            break;
        }
        case EVENT::WRITE_TIMEOUT:
        {
            eventStr = "WRITE_TIMEOUT";
            break;
        }
    }

    return eventStr;
}

std::string KSM_Reader::stateToString(STATE state)
{
    std::string stateStr = "Unknow State";

    switch (state)
    {
        case STATE::IDLE:
        {
            stateStr = "IDLE";
            break;
        }
        case STATE::SENDING_REQUEST_ASYNC:
        {
            stateStr = "SENDING_REQUEST_ASYNC";
            break;
        }
        case STATE::WAITING_FOR_ACK:
        {
            stateStr = "WAITING_FOR_ACK";
            break;
        }
        case STATE::WAITING_FOR_RESPONSE:
        {
            stateStr = "WAITING_FOR_RESPONSE";
            break;
        }
    }

    return stateStr;
}

const KSM_Reader::StateTransition KSM_Reader::stateTransitionTable[static_cast<int>(STATE::STATE_COUNT)] = 
{
    {STATE::IDLE,
    {
        {EVENT::COMMAND_ENQUEUED                                , &KSM_Reader::handleIdleState                         , STATE::SENDING_REQUEST_ASYNC              }
    }},
    {STATE::SENDING_REQUEST_ASYNC,
    {
        {EVENT::WRITE_COMPLETED                                 , &KSM_Reader::handleSendingRequestAsyncState          , STATE::WAITING_FOR_ACK                    },
        {EVENT::WRITE_FAILED                                    , &KSM_Reader::handleSendingRequestAsyncState          , STATE::IDLE                               },
        {EVENT::WRITE_TIMEOUT                                   , &KSM_Reader::handleSendingRequestAsyncState          , STATE::IDLE                               }
    }},
    {STATE::WAITING_FOR_ACK,
    {
        {EVENT::ACK_TIMER_CANCELLED_ACK_RECEIVED                , &KSM_Reader::handleWaitingForAckState                , STATE::WAITING_FOR_ACK                    },
        {EVENT::ACK_TIMEOUT                                     , &KSM_Reader::handleWaitingForAckState                , STATE::IDLE                               },
        {EVENT::WRITE_COMPLETED                                 , &KSM_Reader::handleWaitingForAckState                , STATE::WAITING_FOR_RESPONSE               },
        {EVENT::WRITE_FAILED                                    , &KSM_Reader::handleWaitingForAckState                , STATE::IDLE                               },
        {EVENT::WRITE_TIMEOUT                                   , &KSM_Reader::handleWaitingForAckState                , STATE::IDLE                               }
    }},
    {STATE::WAITING_FOR_RESPONSE,
    {
        {EVENT::RESPONSE_TIMER_CANCELLED_RSP_RECEIVED           , &KSM_Reader::handleWaitingForResponseState           , STATE::IDLE                               },
        {EVENT::RESPONSE_TIMEOUT                                , &KSM_Reader::handleWaitingForResponseState           , STATE::IDLE                               }
    }}
};

void KSM_Reader::processEvent(EVENT event)
{
    boost::asio::post(strand_, [this, event]() {
        int currentStateIndex_ = static_cast<int>(currentState_);
        const auto& stateTransitions = stateTransitionTable[currentStateIndex_].transitions;

        bool eventHandled = false;
        for (const auto& transition : stateTransitions)
        {
            if (transition.event == event)
            {
                eventHandled = true;

                std::ostringstream oss;
                oss << "Current State : " << stateToString(currentState_);
                oss << " , Event : " << eventToString(event);
                oss << " , Event Handler : " << (transition.eventHandler ? "YES" : "NO");
                oss << " , Next State : " << stateToString(transition.nextState);
                ksmLogger(oss.str());

                if (transition.eventHandler != nullptr)
                {
                    (this->*transition.eventHandler)(event);
                }
                currentState_ = transition.nextState;

                if (currentState_ == STATE::IDLE)
                {
                    boost::asio::post(strand_, [this]() {
                        checkCommandQueue();
                    });
                }
                return;
            }
        }

        if (!eventHandled)
        {
            std::ostringstream oss;
            oss << "Event '" << eventToString(event) << "' not handled in state '" << stateToString(currentState_) << "'";
            ksmLogger(oss.str());
        }
    });
}

void KSM_Reader::checkCommandQueue()
{
    bool hasCommand = false;

    {
        std::unique_lock<std::mutex> lock(cmdQueueMutex_);
        if (!commandQueue_.empty())
        {
            hasCommand = true;
        }
    }

    if (hasCommand)
    {
        processEvent(EVENT::COMMAND_ENQUEUED);
    }
}

void KSM_Reader::enqueueCommand(KSM_Reader::KSMReaderCmdID cmd)
{
    if (!pSerialPort_ || (!pSerialPort_->is_open()))
    {
        ksmLogger("Serial Port not open, unable to enqueue command.");
        return;
    }

    {
        std::unique_lock<std::mutex> lock(cmdQueueMutex_);
        commandQueue_.emplace_back(cmd);
    }

    std::stringstream ss;
    ss << "Sending KSM Command to queue: " << KSMReaderCmdIDToString(cmd) << "(" << commandQueue_.size() << ")";
    ksmLogger(ss.str());


    boost::asio::post(strand_, [this]() {
        checkCommandQueue();
    });
}

void KSM_Reader::setCurrentCmd(KSM_Reader::KSMReaderCmdID cmd)
{
    std::unique_lock<std::mutex> lock(currentCmdMutex_);

    currentCmd = cmd;
}

KSM_Reader::KSMReaderCmdID KSM_Reader::getCurrentCmd()
{
    std::unique_lock<std::mutex> lock(currentCmdMutex_);

    return currentCmd;
}

void KSM_Reader::popFromCommandQueueAndEnqueueWrite()
{
    std::unique_lock<std::mutex> lock(cmdQueueMutex_);

    std::ostringstream oss;
    oss << "Command queue size: " << commandQueue_.size() << std::endl;
    if (!commandQueue_.empty())
    {
        oss << "Commands in queue: " << std::endl;
        for (const auto& cmdData : commandQueue_)
        {
            oss << "[Cmd: " << KSMReaderCmdIDToString(cmdData.cmd) << "]" << std::endl;
        }
    }
    else
    {
        oss << "Command queue is empty." << std::endl;
    }
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

    if (!commandQueue_.empty())
    {
        ackRecv_.store(false);
        rspRecv_.store(false);
        CommandWithData cmdData = commandQueue_.front();
        commandQueue_.pop_front();
        setCurrentCmd(cmdData.cmd);
        ksmReaderCmd(cmdData.cmd);
    }
}

void KSM_Reader::handleIdleState(EVENT event)
{
    ksmLogger(__func__);

    if (event == EVENT::COMMAND_ENQUEUED)
    {
        ksmLogger("Pop from command queue and write.");
        popFromCommandQueueAndEnqueueWrite();
        startSerialWriteTimer();
    }
}

void KSM_Reader::handleSendingRequestAsyncState(EVENT event)
{
    ksmLogger(__func__);

    if (event == EVENT::WRITE_COMPLETED)
    {
        ksmLogger("Write completed. Start receiving ack.");
        startAckTimer();
        serialWriteTimer_.cancel();
    }
    else if (event == EVENT::WRITE_FAILED)
    {
        ksmLogger("Write failed.");
        serialWriteTimer_.cancel();
        handleCmdErrorOrTimeout(getCurrentCmd(), KSMReaderCmdRetCode::KSMReaderSend_Failed);
    }
    else if (event == EVENT::WRITE_TIMEOUT)
    {
        ksmLogger("Received serial write timeout event in Sending Request Async State.");
        handleCmdErrorOrTimeout(getCurrentCmd(), KSMReaderCmdRetCode::KSMReaderSend_Failed);
    }
}

void KSM_Reader::handleWaitingForAckState(EVENT event)
{
    ksmLogger(__func__);

    if (event == EVENT::ACK_TIMER_CANCELLED_ACK_RECEIVED)
    {
        ksmLogger("ACK Received. Start sending sendEnq.");
        startSerialWriteTimer();
    }
    else if (event == EVENT::ACK_TIMEOUT)
    {
        ksmLogger("ACK Timeout.");
        handleCmdErrorOrTimeout(getCurrentCmd(), KSMReaderCmdRetCode::KSMReaderRecv_NAK);
    }
    else if (event == EVENT::WRITE_COMPLETED)
    {
        ksmLogger("SendEnq - Write completed. Start receiving response.");
        serialWriteTimer_.cancel();
        startResponseTimer();
    }
    else if (event == EVENT::WRITE_FAILED)
    {
        ksmLogger("SendEnq - Write failed.");
        serialWriteTimer_.cancel();
        handleCmdErrorOrTimeout(getCurrentCmd(), KSMReaderCmdRetCode::KSMReaderSend_Failed);
    }
    else if (event == EVENT::WRITE_TIMEOUT)
    {
        ksmLogger("Received serial write timeout event in Waiting For Ack State.");
        handleCmdErrorOrTimeout(getCurrentCmd(), KSMReaderCmdRetCode::KSMReaderSend_Failed);
    }
}

void KSM_Reader::handleWaitingForResponseState(EVENT event)
{
    ksmLogger(__func__);

    if (event == EVENT::RESPONSE_TIMER_CANCELLED_RSP_RECEIVED)
    {
        ksmLogger("Response Received.");
    }
    else if (event == EVENT::RESPONSE_TIMEOUT)
    {
        ksmLogger("Response Timer Timeout.");
        handleCmdErrorOrTimeout(getCurrentCmd(), KSMReaderCmdRetCode::KSMReaderRecv_NoResp);
    }
}

void KSM_Reader::startSerialWriteTimer()
{
    ksmLogger(__func__);
    serialWriteTimer_.expires_from_now(boost::posix_time::seconds(5));
    serialWriteTimer_.async_wait(boost::asio::bind_executor(strand_,
        std::bind(&KSM_Reader::handleSerialWriteTimeout, this, std::placeholders::_1)));
}

void KSM_Reader::handleSerialWriteTimeout(const boost::system::error_code& error)
{
    ksmLogger(__func__);

    if (!error)
    {
        ksmLogger("Serial Timer Timeout.");
        write_in_progress_ = false;  // Reset flag to allow next write
        processEvent(EVENT::WRITE_TIMEOUT);
    }
    else if (error == boost::asio::error::operation_aborted)
    {
        ksmLogger("Serial Write Timer was cancelled (likely because write completed in time).");
        return;
    }
    else
    {
        std::ostringstream oss;
        oss << "Serial Write Timer error: " << error.message();
        ksmLogger(oss.str());

        write_in_progress_ = false; // Reset flag to allow retry
        processEvent(EVENT::WRITE_TIMEOUT);
    }
}

void KSM_Reader::handleAckTimeout(const boost::system::error_code& error)
{
    ksmLogger(__func__);

    if (!error || (error == boost::asio::error::operation_aborted))
    {
        if (ackRecv_.load())
        {
            ackRecv_.store(false);
            processEvent(EVENT::ACK_TIMER_CANCELLED_ACK_RECEIVED);
        }
        else
        {
            processEvent(EVENT::ACK_TIMEOUT);
        }
    }
    else
    {
        std::ostringstream oss;
        oss << "Ack Timer error: " << error.message();
        ksmLogger(oss.str());
        processEvent(EVENT::ACK_TIMEOUT);
    }
}

void KSM_Reader::startAckTimer()
{
    ackTimer_.expires_from_now(boost::posix_time::seconds(1));
    ackTimer_.async_wait(boost::asio::bind_executor(strand_,
        std::bind(&KSM_Reader::handleAckTimeout, this, std::placeholders::_1)));
}


void KSM_Reader::handleCmdErrorOrTimeout(KSM_Reader::KSMReaderCmdID cmd, KSM_Reader::KSMReaderCmdRetCode retCode)
{
    ksmLogger(__func__);

    switch (cmd)
    {
        case KSMReaderCmdID::INIT_CMD:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << static_cast<int>(retCode);
            ksmLogger(oss.str());

            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderInit", false);
            break;
        }
        case KSMReaderCmdID::CARD_PROHIBITED_CMD:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << static_cast<int>(retCode);
            ksmLogger(oss.str());

            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderCardProhibited", false);
            break;
        }
        case KSMReaderCmdID::CARD_ALLOWED_CMD:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << static_cast<int>(retCode);
            ksmLogger(oss.str());

            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderCardAllowed", false);
            break;
        }
        case KSMReaderCmdID::CARD_ON_IC_CMD:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << static_cast<int>(retCode);
            ksmLogger(oss.str());

            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderCardOnIc", false);
            break;
        }
        case KSMReaderCmdID::IC_POWER_ON_CMD:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << static_cast<int>(retCode);
            ksmLogger(oss.str());

            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderIcPowerOn", false);
            break;
        }
        case KSMReaderCmdID::WARM_RESET_CMD:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << static_cast<int>(retCode);
            ksmLogger(oss.str());

            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderWarmReset", false);
            break;
        }
        case KSMReaderCmdID::SELECT_FILE1_CMD:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << static_cast<int>(retCode);
            ksmLogger(oss.str());

            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderSelectFile1", false);
            break;
        }
        case KSMReaderCmdID::SELECT_FILE2_CMD:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << static_cast<int>(retCode);
            ksmLogger(oss.str());

            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderSelectFile2", false);
            break;
        }
        case KSMReaderCmdID::READ_CARD_INFO_CMD:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << static_cast<int>(retCode);
            ksmLogger(oss.str());

            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderReadCardInfo", false);
            break;
        }
        case KSMReaderCmdID::READ_CARD_BALANCE_CMD:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << static_cast<int>(retCode);
            ksmLogger(oss.str());

            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderReadCardBalance", false);
            break;
        }
        case KSMReaderCmdID::IC_POWER_OFF_CMD:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << static_cast<int>(retCode);
            ksmLogger(oss.str());

            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderIcPowerOff", false);
            break;
        }
        case KSMReaderCmdID::EJECT_TO_FRONT_CMD:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << static_cast<int>(retCode);
            ksmLogger(oss.str());

            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderEjectToFront", false);
            break;
        }
        case KSMReaderCmdID::GET_STATUS_CMD:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << static_cast<int>(retCode);
            ksmLogger(oss.str());

            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderGetStatus", false);

            if (continueReadCardFlag_.load() == true)
            {
                enqueueCommand(KSMReaderCmdID::GET_STATUS_CMD);
            }
            break;
        }
    }
}

void KSM_Reader::startResponseTimer()
{
    rspTimer_.expires_from_now(boost::posix_time::seconds(2));
    rspTimer_.async_wait(boost::asio::bind_executor(strand_,
        std::bind(&KSM_Reader::handleCmdResponseTimeout, this, std::placeholders::_1)));
}

void KSM_Reader::handleCmdResponseTimeout(const boost::system::error_code& error)
{
    ksmLogger(__func__);

    if (!error || (error == boost::asio::error::operation_aborted))
    {
        if (rspRecv_.load())
        {
            rspRecv_.store(false);
            processEvent(EVENT::RESPONSE_TIMER_CANCELLED_RSP_RECEIVED);
        }
        else
        {
            processEvent(EVENT::RESPONSE_TIMEOUT);
        }
    }
    else
    {
        std::ostringstream oss;
        oss << "Response Timer error: " << error.message();
        ksmLogger(oss.str());
        processEvent(EVENT::RESPONSE_TIMEOUT);
    }
}

void KSM_Reader::FnKSMReaderEnable(bool enable)
{
    ksmLogger(__func__);

    if (enable)
    {
        continueReadCardFlag_.store(true);
        enqueueCommand(KSMReaderCmdID::CARD_ALLOWED_CMD);
        enqueueCommand(KSMReaderCmdID::GET_STATUS_CMD);
    }
    else
    {
        continueReadCardFlag_.store(false);
        enqueueCommand(KSMReaderCmdID::CARD_PROHIBITED_CMD);
    }
}

void KSM_Reader::FnKSMReaderReadCardInfo()
{
    ksmLogger(__func__);

    enqueueCommand(KSMReaderCmdID::CARD_ON_IC_CMD);
}

void KSM_Reader::FnKSMReaderSendInit()
{
    ksmLogger(__func__);

    enqueueCommand(KSMReaderCmdID::INIT_CMD);
}

void KSM_Reader::FnKSMReaderSendGetStatus()
{
    ksmLogger(__func__);

    enqueueCommand(KSMReaderCmdID::GET_STATUS_CMD);
}

void KSM_Reader::FnKSMReaderStartGetStatus()
{
    ksmLogger(__func__);

    continueReadCardFlag_.store(true);
    FnKSMReaderSendGetStatus();
}

void KSM_Reader::FnKSMReaderSendEjectToFront()
{
    ksmLogger(__func__);

    enqueueCommand(KSMReaderCmdID::EJECT_TO_FRONT_CMD);
}

void KSM_Reader::ksmLogger(const std::string& logMsg)
{
    if (continueReadCardFlag_.load() == true && getCurrentCmd() == KSMReaderCmdID::GET_STATUS_CMD && blockGetStatusCmdLogFlag_.load() == false)
    {
        if (blockGetStatusCmdLogFlag_.load() == false)
        {
            blockGetStatusCmdLogFlag_.store(true);
            Logger::getInstance()->FnLog("### Running consecutive Get Status Command... [\u23F0 Time]: " + Common::getInstance()->FnGetDateTime(), logFileName_, "KSM");
        }
        return;
    }
    else if (continueReadCardFlag_.load() == true && getCurrentCmd() == KSMReaderCmdID::GET_STATUS_CMD && blockGetStatusCmdLogFlag_.load() == true)
    {
        return;
    }
    else if (continueReadCardFlag_.load() == false && getCurrentCmd() == KSMReaderCmdID::GET_STATUS_CMD && blockGetStatusCmdLogFlag_.load() == true)
    {
        blockGetStatusCmdLogFlag_.store(false);
    }

    Logger::getInstance()->FnLog(logMsg, logFileName_, "KSM");
}
