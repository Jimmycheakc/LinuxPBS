#include <iostream>
#include <sstream>
#include <vector>
#include "antenna.h"
#include "common.h"
#include "event_manager.h"
#include "event_handler.h"
#include "ini_parser.h"
#include "log.h"
#include "operation.h"

Antenna* Antenna::antenna_ = nullptr;
unsigned char Antenna::SEQUENCE_NO = 0;

Antenna::Antenna()
    : io_serial_context(),
    TxNum_(0),
    RxNum_(0),
    rxState_(Antenna::RX_STATE::IGNORE),
    successRecvIUCount_(0),
    successRecvIUFlag_(false),
    continueReadFlag_(false),
    isCmdExecuting_(false),
    antIUCmdSendCount_(0)
{
    memset(txBuff, 0 , sizeof(txBuff));
    memset(rxBuff, 0, sizeof(rxBuff));
    memset(rxBuffData, 0, sizeof(rxBuffData));
    IUNumber_ = "";
    IUNumberPrev_ = "";
    antennaCmdTimeoutInMillisec_ = operation::getInstance()->tParas.giAntInqTO;//IniParser::getInstance()->FnGetAntennaInqTO();
    antennaCmdMaxRetry_ = operation::getInstance()->tParas.giAntMaxRetry;//IniParser::getInstance()->FnGetAntennaMaxRetry();
    antennaIUCmdMinOKtimes_ = operation::getInstance()->tParas.giAntMinOKTimes;//IniParser::getInstance()->FnGetAntennaMinOKtimes();
    logFileName_ = "antenna";
}

Antenna* Antenna::getInstance()
{
    if (antenna_ == nullptr)
    {
        antenna_ = new Antenna();
    }
    return antenna_;
}

void Antenna::FnAntennaInit(boost::asio::io_context& mainIOContext, unsigned int baudRate, const std::string& comPortName)
{
    pMainIOContext_ = &mainIOContext;
    pStrand_ = std::make_unique<boost::asio::io_context::strand>(io_serial_context);
    pSerialPort_ = std::make_unique<boost::asio::serial_port>(io_serial_context, comPortName);

    pSerialPort_->set_option(boost::asio::serial_port_base::baud_rate(baudRate));
    pSerialPort_->set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
    pSerialPort_->set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::even));
    pSerialPort_->set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
    pSerialPort_->set_option(boost::asio::serial_port_base::character_size(8));

    Logger::getInstance()->FnCreateLogFile(logFileName_);

    std::stringstream ss;
    if (pSerialPort_->is_open())
    {
        ss << "Successfully open serial port for Antenna Communication: " << comPortName;
    }
    else
    {
        ss << "Failed to open serial port for Antenna Communication: " << comPortName;
        Logger::getInstance()->FnLog(ss.str());
    }
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "ANT");
    
    int result = antennaInit();
    if (result == 1)
    {
        // raise antenna power event success
        EventManager::getInstance()->FnEnqueueEvent("Evt_AntennaPower", true);
        Logger::getInstance()->FnLog("Antenna initialization completed.");
        Logger::getInstance()->FnLog("Antenna initialization completed.", logFileName_, "ANT");
    }
    else
    {
        // raise antenna power event failed
        EventManager::getInstance()->FnEnqueueEvent("Evt_AntennaPower", false);
        Logger::getInstance()->FnLog("Antenna initialization failed.");
        Logger::getInstance()->FnLog("Antenna initialization failed.", logFileName_, "ANT");
    }
}

int Antenna::antennaInit()
{
    int ret;

    ret = antennaCmd(AntCmdID::SET_ANTENNA_DATA_CMD);
    if (ret != 1)
    {
        return ret;
    }

    ret = antennaCmd(AntCmdID::NO_USE_OF_ANTENNA_CMD);
    if (ret != 1)
    {
        return ret;
    }

    ret = antennaCmd(AntCmdID::SET_ANTENNA_TIME_CMD);
    if (ret != 1)
    {
        return ret;
    }

    ret = antennaCmd(AntCmdID::MACHINE_CHECK_CMD);
    if (ret != 1)
    {
        return ret;
    }

    return ret;
}

std::string Antenna::antennaCmdIDToString(Antenna::AntCmdID cmd)
{
    switch (cmd)
    {
        case AntCmdID::NONE_CMD:
            return "NONE_CMD";
            break;
        case AntCmdID::MANUAL_CMD:
            return "MANUAL_CMD";
            break;
        case AntCmdID::FORCE_GET_IU_NO_CMD:
            return "FORCE_GET_IU_NO_CMD";
            break;
        case AntCmdID::USE_OF_ANTENNA_CMD:
            return "USE_OF_ANTENNA_CMD";
            break;
        case AntCmdID::NO_USE_OF_ANTENNA_CMD:
            return "NO_USE_OF_ANTENNA_CMD";
            break;
        case AntCmdID::SET_ANTENNA_TIME_CMD:
            return "SET_ANTENNA_TIME_CMD";
            break;
        case AntCmdID::MACHINE_CHECK_CMD:
            return "MACHINE_CHECK_CMD";
            break;
        case AntCmdID::SET_ANTENNA_DATA_CMD:
            return "SET_ANTENNA_DATA_CMD";
            break;
        case AntCmdID::GET_ANTENNA_DATA_CMD:
            return "GET_ANTENNA_DATA_CMD";
            break;
        case AntCmdID::REQ_IO_STATUS_CMD:
            return "REQ_IO_STATUS_CMD";
            break;
        case AntCmdID::SET_OUTPUT_CMD:
            return "SET_OUTPUT_CMD";
            break;
        default:
            return "UNKNOWN_CMD";
    }
}

bool Antenna::FnGetIsCmdExecuting() const
{
    return isCmdExecuting_.load();
}

int Antenna::antennaCmd(AntCmdID cmdID)
{
    int ret;
    AntCmdRetCode retCode;
    if (!isCmdExecuting_.exchange(true))
    {
        std::stringstream ss;
        ss << __func__ << " Command ID : " << antennaCmdIDToString(cmdID);
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "ANT");

        if (!pSerialPort_->is_open())
        {
            return -1;
        }

        int antennaCmdTimeoutInMs = 1000;
        std::vector<unsigned char> dataBuffer;
        unsigned char antennaID = operation::getInstance()->gtStation.iAntID;//IniParser::getInstance()->FnGetAntennaId();
        unsigned char destID = 0xB0 + antennaID;
        unsigned char sourceID = 0x00;

        if ((++SEQUENCE_NO) == 0)
        {
            // For sequence number of 1 to 255
            ++SEQUENCE_NO;
        }

        switch(cmdID)
        {
            case AntCmdID::MANUAL_CMD:
            {
                break;
            }
            case AntCmdID::SET_ANTENNA_DATA_CMD:
            {
                antennaCmdTimeoutInMs = 1000;
                dataBuffer = loadSetAntennaData(destID, 
                                            sourceID, 
                                            SET_ANTENNA_DATA_CATEGORY, 
                                            SET_ANTENNA_DATA_COMMAND_NO,
                                            SEQUENCE_NO,
                                            antennaID,
                                            0x01,
                                            0x01,
                                            0x01,
                                            0xE1,
                                            0x12C,
                                            0x3C);
                break;
            }
            case AntCmdID::GET_ANTENNA_DATA_CMD:
            {
                dataBuffer = loadGetAntennaData(destID, sourceID, GET_ANTENNA_DATA_CATEGORY, GET_ANTENNA_DATA_COMMAND_NO, SEQUENCE_NO);
                break;
            }
            case AntCmdID::NO_USE_OF_ANTENNA_CMD:
            {
                dataBuffer = loadNoUseOfAntenna(destID, sourceID, NO_USE_OF_ANTENNA_CATEGORY, NO_USE_OF_ANTENNA_COMMAND_NO, SEQUENCE_NO);
                break;
            }
            case AntCmdID::USE_OF_ANTENNA_CMD:
            {
                dataBuffer = loadUseOfAntenna(destID, sourceID, USE_OF_ANTENNA_CATEGORY, USE_OF_ANTENNA_COMMAND_NO, SEQUENCE_NO);
                break;
            }
            case AntCmdID::SET_ANTENNA_TIME_CMD:
            {
                dataBuffer = loadSetAntennaTime(destID, sourceID, SET_ANTENNA_TIME_CATEGORY, SET_ANTENNA_TIME_COMMAND_NO, SEQUENCE_NO);
                break;
            }
            case AntCmdID::MACHINE_CHECK_CMD:
            {
                dataBuffer = loadMachineCheck(destID, sourceID, MACHINE_CHECK_CATEGORY, MACHINE_CHECK_COMMAND_NO, SEQUENCE_NO);
                break;
            }
            case AntCmdID::REQ_IO_STATUS_CMD:
            {
                dataBuffer = loadReqIOStatus(destID, sourceID, REQ_IO_STATUS_CATEGORY, REQ_IO_STATUS_COMMAND_NO, SEQUENCE_NO);
                break;
            }
            case AntCmdID::SET_OUTPUT_CMD:
            {
                dataBuffer = loadSetOutput(destID, sourceID, SET_OUTPUT_CATEGORY, SET_OUTPUT_COMMAND_NO, SEQUENCE_NO, 0x01, 0x01);
                break;
            }
            case AntCmdID::FORCE_GET_IU_NO_CMD:
            {
                antennaCmdTimeoutInMs = antennaCmdTimeoutInMillisec_;
                dataBuffer = loadForceGetUINo(destID, sourceID, FORCE_GET_IU_CATEGORY, FORCE_GET_IU_COMMAND_NO, SEQUENCE_NO);
                break;
            }
            default:
            {
                std::stringstream ss;
                ss << __func__ << " : Command not found.";
                Logger::getInstance()->FnLog(ss.str(), logFileName_, "ANT");

                return static_cast<int>(AntCmdRetCode::AntRecv_CmdNotFound);
                break;
            }
        }

        int retry = 0;
        Antenna::ReadResult result;
        retCode = AntCmdRetCode::AntRecv_NoResp;

        do
        {
            result.data = {};
            result.success = false;

            antennaCmdSend(dataBuffer);
            result = antennaReadWithTimeout(antennaCmdTimeoutInMs);

            if (result.success)
            {
                retCode = antennaHandleCmdResponse(cmdID, result.data);
                break;
            }
            else
            {
                retry++;
                std::stringstream ss;
                ss << "Retry send command, retry times : " << retry;
                Logger::getInstance()->FnLog(ss.str(), logFileName_, "ANT");
            }
        } while(retry <= antennaCmdMaxRetry_);

        if (retry >= antennaCmdMaxRetry_)
        {
            EventManager::getInstance()->FnEnqueueEvent("Evt_AntennaFail", 1);
        }

        isCmdExecuting_.store(false);
    }
    else
    {
        Logger::getInstance()->FnLog("Cmd cannot send, there is already one Cmd executing.", logFileName_, "ANT");
        retCode = AntCmdRetCode::AntSend_Failed;
    }

    return static_cast<int>(retCode);
}

Antenna::ReadResult Antenna::antennaReadWithTimeout(int milliseconds)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "ANT");

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
                    Logger::getInstance()->FnLog(ss.str(), logFileName_, "ANT");
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
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "ANT");
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
    /*
    pSerialPort_->async_read_some(boost::asio::buffer(buffer),
        [&](const boost::system::error_code& error, std::size_t transferred){
            if (!error)
            {
                bytes_transferred = transferred;
                success = true;
            }
            else
            {
                std::stringstream ss;
                ss << __func__ << " Async Read error : " << error.message();
                Logger::getInstance()->FnLog(ss.str(), logFileName_, "ANT");
                success = false;
            }
            timer.cancel();
        });
    
    timer.async_wait([&](const boost::system::error_code& error){
        if (!bytes_transferred && !error)
        {
            std::stringstream ss;
            ss << __func__ << " Async Read Timeout Occurred.";
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "ANT");
            pSerialPort_->cancel();
            success = false;
        }
        
        io_serial_context.post([this]() {
            io_serial_context.stop();
        });
    });

    io_serial_context.run();

    io_serial_context.reset();
    //io_serial_context.get_executor().context().stop();
    //pSerialPort_->cancel();

    buffer.resize(bytes_transferred);
    return {buffer, success};
    */
    return {retBuff, success};
}

void Antenna::resetRxBuffer()
{
    memset(rxBuff, 0, sizeof(rxBuff));
    RxNum_ = 0;
}

bool Antenna::responseIsComplete(const std::vector<char>& buffer, std::size_t bytesTransferred)
{
    bool ret = false;
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

Antenna::AntCmdRetCode Antenna::antennaHandleCmdResponse(AntCmdID cmd, const std::vector<char>& dataBuff)
{
    int numBytes = 0;
    AntCmdRetCode retCode = AntCmdRetCode::AntRecv_NoResp;

    /*
    for (const auto &character : dataBuff)
    {
        numBytes = receiveRxDataByte(character);
    }

    if (numBytes == getRxNum() && numBytes >= 8)
    */
    if (dataBuff.size() >= 8)
    {
        std::stringstream receivedRespStream;
        receivedRespStream << "Received Buffer Data : " << Common::getInstance()->FnGetVectorCharToHexString(dataBuff);
        Logger::getInstance()->FnLog(receivedRespStream.str(), logFileName_, "ANT");
        std::stringstream receivedRespNumBytesStream;
        receivedRespNumBytesStream << "Received Buffer size : " << getRxNum();
        Logger::getInstance()->FnLog(receivedRespNumBytesStream.str(), logFileName_, "ANT");

        std::string cmdID = Common::getInstance()->FnGetUCharArrayToHexString(getRxBuf()+2, 2);

        if (cmdID == "3281")
        {
            std::stringstream logMsg;

            switch (cmd)
            {
                case AntCmdID::USE_OF_ANTENNA_CMD:
                {
                    logMsg << "USE_OF_ANTENNA_CMD response : OK !";
                    break;
                }
                case AntCmdID::NO_USE_OF_ANTENNA_CMD:
                {
                    logMsg << "NO_USE_OF_ANTENNA_CMD response : OK !";
                    break;
                }
                case AntCmdID::SET_ANTENNA_TIME_CMD:
                {
                    logMsg << "SET_ANTENNA_TIME_CMD response : OK !";
                    break;
                }
                case AntCmdID::SET_ANTENNA_DATA_CMD:
                {
                    logMsg << "SET_ANTENNA_DATA_CMD response : OK !";
                    break;
                }
                case AntCmdID::SET_OUTPUT_CMD:
                {
                    logMsg << "SET_OUTPUT_CMD response : OK !";
                    break;
                }
            }

            Logger::getInstance()->FnLog(logMsg.str(), logFileName_, "ANT");
            retCode = AntCmdRetCode::AntRecv_ACK;
        }
        else if (cmdID == "32c1")
        {
            if (cmd == AntCmdID::GET_ANTENNA_DATA_CMD)
            {
            Logger::getInstance()->FnLog("GET_ANTENNA_DATA_CMD response : OK !", logFileName_, "ANT");
            }
            retCode = AntCmdRetCode::AntRecv_ACK;
        }
        else if (cmdID == "3282")
        {
            std::stringstream logMsg;
            switch (cmd)
            {
                case AntCmdID::USE_OF_ANTENNA_CMD:
                {
                    logMsg << "USE_OF_ANTENNA_CMD response : NACK response";
                    break;
                }
                case AntCmdID::SET_ANTENNA_TIME_CMD:
                {
                    logMsg << "SET_ANTENNA_TIME_CMD response : NACK response";
                    break;
                }
                case AntCmdID::SET_ANTENNA_DATA_CMD:
                {
                    logMsg << "SET_ANTENNA_DATA_CMD response : NACK response";
                    break;
                }
                case AntCmdID::GET_ANTENNA_DATA_CMD:
                {
                    logMsg << "GET_ANTENNA_DATA_CMD response : NACK response";
                    break;
                }
            }
            Logger::getInstance()->FnLog(logMsg.str(), logFileName_, "ANT");
            retCode = AntCmdRetCode::AntRecv_NAK;
        }
        else if (cmdID == "3283")
        {
            if (cmd == AntCmdID::SET_ANTENNA_DATA_CMD)
            {
            Logger::getInstance()->FnLog("SET_ANTENNA_DATA_CMD response : BUSY response", logFileName_, "ANT");
            }
            retCode = AntCmdRetCode::AntRecv_NAK;
        }
        // Force Get IU Cmd Response
        else if (cmdID == "3fb2")
        {
            std::vector<char> IUNumberCurrOri;
            IUNumberCurrOri.assign(getRxBuf()+ 7, getRxBuf() + 12);
            std::vector<char> IUNumberCurr(IUNumberCurrOri.rbegin(), IUNumberCurrOri.rend());
            std::string IUNumberStr = Common::getInstance()->FnGetVectorCharToHexString(IUNumberCurr);

            if (Common::getInstance()->FnIsStringNumeric(IUNumberStr))
            {
                if (IUNumberPrev_ != IUNumberStr)
                {
                    IUNumberPrev_ = IUNumberStr;
                }
                else if (IUNumberPrev_ == IUNumberStr)
                {
                    successRecvIUCount_++;

                    if (successRecvIUCount_ == antennaIUCmdMinOKtimes_)
                    {
                        successRecvIUFlag_ = true;
                        successRecvIUCount_ = 0;
                        IUNumber_ = IUNumberPrev_;
                        IUNumberPrev_ = "";
                        // Need to send IU event

                        std::stringstream ss;
                        ss << "IU No : " << IUNumber_;
                        Logger::getInstance()->FnLog(ss.str(), logFileName_, "ANT");
                    }
                }
                retCode = AntCmdRetCode::AntRecv_ACK;
            }
            else
            {
                std::stringstream ss;
                ss << "Unknow IU, IU No : " << IUNumberStr << " IU No. is not numeric)";
                Logger::getInstance()->FnLog(ss.str(), logFileName_, "ANT");

                retCode = AntCmdRetCode::AntRecv_NAK;
            }
        }
        else if (cmdID == "3f82")
        {
            if (cmd == AntCmdID::FORCE_GET_IU_NO_CMD)
            {
                Logger::getInstance()->FnLog("FORCE_GET_IU_NO_CMD response : NACK response", logFileName_, "ANT");
            }
            retCode = AntCmdRetCode::AntRecv_NAK;
        }
        else if (cmdID == "31b3")
        {
            if (cmd == AntCmdID::MACHINE_CHECK_CMD)
            {
                Logger::getInstance()->FnLog("MACHINE_CHECK_CMD response : OK !", logFileName_, "ANT");
            }
            retCode = AntCmdRetCode::AntRecv_ACK;
        }
        else if (cmdID == "31b4")
        {
            if (cmd == AntCmdID::REQ_IO_STATUS_CMD)
            {
                Logger::getInstance()->FnLog("REQ_IO_STATUS_CMD response : OK !", logFileName_, "ANT");
            }
            retCode = AntCmdRetCode::AntRecv_ACK;
        }
    }

    std::stringstream retCodeStream;
    retCodeStream << "Command Response Return Code : " << static_cast<int>(retCode);
    Logger::getInstance()->FnLog(retCodeStream.str(), logFileName_, "ANT");

    return retCode;
}

std::vector<unsigned char> Antenna::loadSetAntennaData(unsigned char destID, 
                                                    unsigned char sourceID,
                                                    unsigned char category,
                                                    unsigned char command,
                                                    unsigned char seqNo,
                                                    unsigned char antennaID,
                                                    int placeID,
                                                    unsigned char parkingNo,
                                                    unsigned char antennaMode,
                                                    unsigned char enqTimer,
                                                    int doubleCommTimer,
                                                    int cmdWaitTimer)
{
    std::vector<unsigned char> dataBuf(18, 0);

    dataBuf[0] = destID;
    dataBuf[1] = sourceID;
    dataBuf[2] = category;
    dataBuf[3] = command;
    dataBuf[4] = seqNo;
    dataBuf[5] = 0x00;           // RFU = 0
    dataBuf[6] = 0x0B;           // Len = 11
    // Data Start
    dataBuf[7] = antennaID;
    dataBuf[8] = static_cast<unsigned char>(placeID & 0x00FF);
    dataBuf[9] = static_cast<unsigned char>(placeID & 0xFF00);
    dataBuf[10] = parkingNo;
    dataBuf[11] = antennaMode;
    dataBuf[12] = 0x00;
    dataBuf[13] = enqTimer;
    dataBuf[14] = static_cast<unsigned char>(doubleCommTimer & 0x00FF);
    dataBuf[15] = static_cast<unsigned char>((doubleCommTimer & 0xFF00) / 256);
    dataBuf[16] = static_cast<unsigned char>(cmdWaitTimer & 0x00FF);
    dataBuf[17] = static_cast<unsigned char>((cmdWaitTimer & 0xFF00) / 256);
    // Data End

    return dataBuf;
}

std::vector<unsigned char> Antenna::loadGetAntennaData(unsigned char destID, unsigned char sourceID, unsigned char category, unsigned char command, unsigned char seqNo)
{
    std::vector<unsigned char> dataBuf(7, 0);

    dataBuf[0] = destID;
    dataBuf[1] = sourceID;
    dataBuf[2] = category;
    dataBuf[3] = command;
    dataBuf[4] = seqNo;
    dataBuf[5] = 0;          // RFU = 0
    dataBuf[6] = 0;          // Len = 0

    return dataBuf;
}

std::vector<unsigned char> Antenna::loadNoUseOfAntenna(unsigned char destID, unsigned char sourceID, unsigned char category, unsigned char command, unsigned char seqNo)
{
    std::vector<unsigned char> dataBuf(7, 0);

    dataBuf[0] = destID;
    dataBuf[1] = sourceID;
    dataBuf[2] = category;
    dataBuf[3] = command;
    dataBuf[4] = seqNo;
    dataBuf[5] = 0;          // RFU = 0
    dataBuf[6] = 0;          // Len = 0

    return dataBuf;
}

std::vector<unsigned char> Antenna::loadUseOfAntenna(unsigned char destID, unsigned char sourceID, unsigned char category, unsigned char command, unsigned char seqNo)
{
    std::vector<unsigned char> dataBuf(7, 0);

    dataBuf[0] = destID;
    dataBuf[1] = sourceID;
    dataBuf[2] = category;
    dataBuf[3] = command;
    dataBuf[4] = seqNo;
    dataBuf[5] = 0;          // RFU = 0
    dataBuf[6] = 0;          // Len = 0

    return dataBuf;
}

std::vector<unsigned char> Antenna::loadSetAntennaTime(unsigned char destID, unsigned char sourceID, unsigned char category, unsigned char command, unsigned char seqNo)
{
    std::vector<unsigned char> dataBuf(15, 0);

    dataBuf[0] = destID;
    dataBuf[1] = sourceID;
    dataBuf[2] = category;
    dataBuf[3] = command;
    dataBuf[4] = seqNo;
    dataBuf[5] = 0;          // RFU = 0
    dataBuf[6] = 8;          // Len = 0
    // Data Start
    auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm* localTime = std::localtime(&time);

    dataBuf[7] = localTime->tm_mday;
    dataBuf[8] = localTime->tm_mon + 1;
    dataBuf[9] = (localTime->tm_year + 1900) % 256;
    dataBuf[10] = (localTime->tm_year + 1900) / 256;
    dataBuf[11] = 0;    // RFU
    dataBuf[12] = localTime->tm_sec;
    dataBuf[13] = localTime->tm_min;
    dataBuf[14] = localTime->tm_hour;
    // Data End

    return dataBuf;
}

std::vector<unsigned char> Antenna::loadMachineCheck(unsigned char destID, unsigned char sourceID, unsigned char category, unsigned char command, unsigned char seqNo)
{
    std::vector<unsigned char> dataBuf(7, 0);

    dataBuf[0] = destID;
    dataBuf[1] = sourceID;
    dataBuf[2] = category;
    dataBuf[3] = command;
    dataBuf[4] = seqNo;
    dataBuf[5] = 0;          // RFU = 0
    dataBuf[6] = 0;          // Len = 0

    return dataBuf;
}

std::vector<unsigned char> Antenna::loadReqIOStatus(unsigned char destID, unsigned char sourceID, unsigned char category, unsigned char command, unsigned char seqNo)
{
    std::vector<unsigned char> dataBuf(7, 0);

    dataBuf[0] = destID;
    dataBuf[1] = sourceID;
    dataBuf[2] = category;
    dataBuf[3] = command;
    dataBuf[4] = seqNo;
    dataBuf[5] = 0;          // RFU = 0
    dataBuf[6] = 0;          // Len = 0

    return dataBuf;
}

std::vector<unsigned char> Antenna::loadSetOutput(unsigned char destID, unsigned char sourceID, unsigned char category, unsigned char command, unsigned char seqNo, unsigned char maskIO, unsigned char statusOfIO)
{
    std::vector<unsigned char> dataBuf(9, 0);

    dataBuf[0] = destID;
    dataBuf[1] = sourceID;
    dataBuf[2] = category;
    dataBuf[3] = command;
    dataBuf[4] = seqNo;
    dataBuf[5] = 0;          // RFU = 0
    dataBuf[6] = 0;          // Len = 0
    dataBuf[7] = maskIO;
    dataBuf[8] = statusOfIO;

    return dataBuf;
}

std::vector<unsigned char> Antenna::loadForceGetUINo(unsigned char destID, unsigned char sourceID, unsigned char category, unsigned char command, unsigned char seqNo)
{
    std::vector<unsigned char> dataBuf(7, 0);

    dataBuf[0] = destID;
    dataBuf[1] = sourceID;
    dataBuf[2] = category;
    dataBuf[3] = command;
    dataBuf[4] = seqNo;
    dataBuf[5] = 0;          // RFU = 0
    dataBuf[6] = 0;          // Len = 0

    return dataBuf;
}

void Antenna::handleIgnoreState(char c)
{
    if (c == Antenna::DLE)
    {
        rxState_ = RX_STATE::ESCAPE2START;
    }
}

void Antenna::handleEscape2StartState(char c)
{
    if (c == Antenna::STX)
    {
        rxState_ = RX_STATE::RECEIVING;
        RxNum_ = 0;
        memset(rxBuff, 0, sizeof(rxBuff));
    }
    else
    {
        resetState();
    }
}

void Antenna::handleReceivingState(char c)
{
    if (c == Antenna::DLE)
    {
        rxState_ = RX_STATE::ESCAPE;
    }
    else
    {
        rxBuff[RxNum_++] = c;
    }
}

void Antenna::handleEscapeState(char c)
{
    if (c == Antenna::STX)
    {
        rxState_= RX_STATE::RECEIVING;
        RxNum_ = 0;
        memset(rxBuff, 0, sizeof(rxBuff));
    }
    else if (c == Antenna::ETX)
    {
        rxBuff[RxNum_++] = c;
        rxState_ = RX_STATE::RXCRC1;
    }
    else if (c == Antenna::DLE)
    {
        rxBuff[RxNum_++] = c;
        rxState_ = RX_STATE::RECEIVING;
    }
    else
    {
        resetState();
    }
}

void Antenna::handleRXCRC1State(char c, char &b)
{
    b = c;
    rxState_ = RX_STATE::RXCRC2;
}

int Antenna::handleRXCRC2State(char c, unsigned int &rxcrc, char b)
{
    rxState_ = RX_STATE::IGNORE;
    unsigned int crc = ( (static_cast<unsigned int>(c) << 8) | (b & 0xFF) ) & 0xFFFF;
    rxcrc = CRC16R_Calculate(rxBuff, RxNum_, rxcrc);
    
    if (rxcrc == crc)
    {
        return RxNum_;
    }
    else
    {
        resetState();
        return 0;
    }
}

void Antenna::resetState()
{
    rxState_ = RX_STATE::IGNORE;
    RxNum_ = 0;
    memset(rxBuff, 0, sizeof(rxBuff));
}

int Antenna::receiveRxDataByte(char c)
{
    static char b;
    unsigned int rxcrc = 0xFFFF;
    int ret = 0;

    switch (rxState_)
    {
        case RX_STATE::IGNORE:
            handleIgnoreState(c);
            break;
        case RX_STATE::ESCAPE2START:
            handleEscape2StartState(c);
            break;
        case RX_STATE::RECEIVING:
            handleReceivingState(c);
            break;
        case RX_STATE::ESCAPE:
            handleEscapeState(c);
            break;
        case RX_STATE::RXCRC1:
            handleRXCRC1State(c, b);
            break;
        case RX_STATE::RXCRC2:
            ret = handleRXCRC2State(c, rxcrc, b);
            break;
    }

    return ret;
}

unsigned int Antenna::CRC16R_Calculate(unsigned char* s, unsigned char len, unsigned int crc)
{
    // CRC order: 16
    // CCITT(recommandation) : F(x) = x16 + x12 + x5 + 1
    // CRC Poly: 0x8408 <=> 0x1021
    // Operational initial value: 0xFFFF
    // Final xor value: 0xFFFF
    unsigned char i, j;
    for (i = 0; i < len; i++, s++)
    {
        crc ^= ((unsigned int)(*s)) & 0xFF;
        for (j = 0; j < 8; j++)
        {
            if (crc & 0x0001)
            {
                crc = (crc >> 1) ^ 0x8408;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    crc ^= 0xFFFF;
    return crc;
}

void Antenna::antennaCmdSend(const std::vector<unsigned char>& dataBuff)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "ANT");

    if (!pSerialPort_->is_open())
    {
        return;
    }

    unsigned int txCrc = 0xFFFF;
    unsigned char tempChar;
    int i = 0;
    memset(txBuff, 0, sizeof(txBuff));
    std::vector<unsigned char> tempBuffData(dataBuff.begin(), dataBuff.end());

    // Construct Tx Frame
    // Start of frame
    txBuff[i++] = Antenna::DLE;
    txBuff[i++] = Antenna::STX;
    // Data of frame, exclude ETX
    for (int j = 0; j < tempBuffData.size() ; j++)
    {
        tempChar = tempBuffData[j];
        if (tempChar == Antenna::DLE)
        {
            txBuff[i++] = Antenna::DLE;
        }
        txBuff[i++] = tempChar;
    }
    // End of Frame
    txBuff[i++] = Antenna::DLE;
    txBuff[i++] = Antenna::ETX;
    
    // Include ETX in CRC Calculation
    tempBuffData.push_back(static_cast<unsigned char>(Antenna::ETX));
    txCrc = CRC16R_Calculate(tempBuffData.data(), tempBuffData.size(), txCrc);
    txBuff[i++] = txCrc & 0xFF;
    txBuff[i++] = (txCrc >> 8) & 0xFF;
    TxNum_ = i;

    /* Send Tx Data buffer via serial */
    boost::asio::write(*pSerialPort_, boost::asio::buffer(getTxBuf(), getTxNum()));
}

unsigned char* Antenna::getTxBuf()
{
    return txBuff;
}

unsigned char* Antenna::getRxBuf()
{
    return rxBuff;
}

int Antenna::getTxNum()
{
    return TxNum_;
}

int Antenna::getRxNum()
{
    return RxNum_;
}

void Antenna::handleReadIUTimerExpiration()
{
    int ret = 0;
    ret = antennaCmd(AntCmdID::FORCE_GET_IU_NO_CMD);
    antIUCmdSendCount_++;

    if (!successRecvIUFlag_ && continueReadFlag_.load())
    {
        startSendReadIUCmdTimer(200);
        if (antIUCmdSendCount_ > 10) {
            EventManager::getInstance()->FnEnqueueEvent("Evt_AntennaFail", 2);
            antIUCmdSendCount_ = 0;
        }
    }
    else
    {
        continueReadFlag_.store(false);
        if (successRecvIUFlag_)
        {
            successRecvIUFlag_ = false;
            // raise iucome event
            EventManager::getInstance()->FnEnqueueEvent("Evt_AntennaIUCome", IUNumber_);
        }
        else
        {
            // raise antenna fail
            EventManager::getInstance()->FnEnqueueEvent("Evt_AntennaFail", 3);
        }
    }
}

void Antenna::startSendReadIUCmdTimer(int milliseconds)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "ANT");

    std::unique_ptr<boost::asio::io_context> timerIOContext_ = std::make_unique<boost::asio::io_context>();
    std::thread timerThread([this, milliseconds, timerIOContext_ = std::move(timerIOContext_)]() mutable {
        std::unique_ptr<boost::asio::deadline_timer> periodicSendReadIUCmdTimer_ = std::make_unique<boost::asio::deadline_timer>(*timerIOContext_);
        periodicSendReadIUCmdTimer_->expires_from_now(boost::posix_time::milliseconds(milliseconds));
        periodicSendReadIUCmdTimer_->async_wait([this](const boost::system::error_code& error) {
                if (!error) {
                    handleReadIUTimerExpiration();
                }
        });

        timerIOContext_->run();
    });
    timerThread.detach();
}

void Antenna::FnAntennaSendReadIUCmd()
{
    antIUCmdSendCount_ = 0;
    continueReadFlag_.store(true);
    startSendReadIUCmdTimer(100);
}

void Antenna::FnAntennaStopRead()
{
    continueReadFlag_.store(false);
}

int Antenna::FnAntennaGetIUCmdSendCount()
{
    return antIUCmdSendCount_;
}