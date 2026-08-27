#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#endif

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>

#include "antenna.h"
#include "common.h"
#include "event_manager.h"
#include "event_handler.h"
#include "ini_parser.h"
#include "log.h"
#include "operation.h"

unsigned char Antenna::SEQUENCE_NO = 0;

Antenna::Antenna()
    : ioContext_(),
      moduleRunning_(false),
      stopping_(false),
      continueReadFlag_(false),
      isCmdExecuting_(false),
      antIUCmdSendCount_(0),
      iuLoopRunning_(false),
      initializationCompleted_(false),
      TxNum_(0),
      RxNum_(0),
      rxState_(Antenna::RX_STATE::IGNORE),
      successRecvIUCount_(0),
      successRecvIUFlag_(false)
{
    memset(txBuff, 0 , sizeof(txBuff));
    memset(rxBuff, 0, sizeof(rxBuff));
    memset(rxBuffData, 0, sizeof(rxBuffData));
    
    IUNumber_.clear();
    IUNumberPrev_.clear();
    
    antennaId_ = 0;
    antennaCmdTimeoutInMillisec_ = 0;
    antennaCmdMaxRetry_ = 3;
    antennaIUCmdMinOKtimes_ = 0;
    logFileName_ = "antenna";
}

Antenna::~Antenna()
{
    FnAntennaShutdown();
}

Antenna* Antenna::getInstance()
{
    // C++11+ guarantees thread-safe construction of function-local statics.
    static Antenna instance;
    return &instance;
}

void Antenna::FnAntennaInit(
    unsigned int baudRate,
    const std::string& comPortName,
    int antennaId,
    int antennaInqTO,
    int antennaMinOkTimes,
    int eps)
{
    eps_ = eps;
    antennaId_ = antennaId;
    antennaCmdTimeoutInMillisec_ = antennaInqTO;
    antennaIUCmdMinOKtimes_ = antennaMinOkTimes;

    // If a previous Antenna run already exited, make sure its std::thread
    // object is joined before assigning a new worker thread.
    if (!moduleRunning_.load() && ioThread_.joinable())
    {
        if (std::this_thread::get_id() == ioThread_.get_id())
            return;
        ioThread_.join();
    }

    bool expected = false;
    if (!moduleRunning_.compare_exchange_strong(expected, true))
    {
        Logger::getInstance()->FnLog("Antenna module is already running.", logFileName_, "ANT");
        return;
    }

    stopping_.store(false);
    continueReadFlag_.store(false);
    isCmdExecuting_.store(false);
    antIUCmdSendCount_.store(0);

    // io_context enters stopped state after a previous run() exits.
    // restart() makes this module restartable after FnAntennaShutdown().
    ioContext_.restart();
    workGuard_.emplace(boost::asio::make_work_guard(ioContext_));

    boost::asio::co_spawn(
        ioContext_,
        antennaModuleInitAsync(baudRate, comPortName),
        [this](std::exception_ptr ep)
        {
            if (!ep)
                return;

            try
            {
                std::rethrow_exception(ep);
            }
            catch (const std::exception& e)
            {
                std::stringstream ss;
                ss << "antennaModuleInitAsync exception: " << e.what();
                Logger::getInstance()->FnLogExceptionError(ss.str());
            }
        });
    
    // Exactly ONE thread calls ioContext_.run().
    ioThread_ = std::thread([this]()
    {
#if defined(__linux__)
        ::pthread_setname_np(::pthread_self(), "ANTENNA_IO");
#endif
        // Boost.Asio permits run() to be entered again after a handler throws.
        // Keep the Antenna event loop alive unless shutdown was requested.
        while (!stopping_.load())
        {
            try
            {
                ioContext_.run();
                break;
            }
            catch (const std::exception& e)
            {
                std::stringstream ss;
                ss << "Antenna io_context handler exception: " << e.what();
                Logger::getInstance()->FnLogExceptionError(ss.str());
            }
            catch (...)
            {
                Logger::getInstance()->FnLogExceptionError(
                    "Antenna io_context handler unknown exception.");
            }
        }
    });
}

void Antenna::shutdownOnIoThread()
{
    continueReadFlag_.store(false);

    if (periodicSendReadIUCmdTimer_)
    {
        boost::system::error_code ignored;
        periodicSendReadIUCmdTimer_->cancel(ignored);
    }

    if (pSerialPort_)
    {
        boost::system::error_code ignored;
        pSerialPort_->cancel(ignored);
        pSerialPort_->close(ignored);
    }

    // Allow run() to exit after cancelled async operations/coroutines finish.
    workGuard_.reset();
}

void Antenna::FnAntennaShutdown()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "ANT");

    const bool wasRunning = moduleRunning_.exchange(false);

    if (wasRunning)
    {
        stopping_.store(true);
        continueReadFlag_.store(false);

        if (ioThread_.joinable() &&
            std::this_thread::get_id() == ioThread_.get_id())
        {
            // Called from the Antenna thread itself: request shutdown now,
            // but never join the current thread. A later external shutdown or
            // the singleton destructor will join it.
            shutdownOnIoThread();
            return;
        }

        boost::asio::post(ioContext_, [this]()
        {
            shutdownOnIoThread();
        });
    }

    // Join even when wasRunning == false. This covers the rare case where an
    // earlier shutdown request originated from the Antenna thread itself.
    if (ioThread_.joinable() &&
        std::this_thread::get_id() != ioThread_.get_id())
    {
        ioThread_.join();
    }

    // No handlers are running after join(), so these can now be destroyed.
    pSerialPort_.reset();
    periodicSendReadIUCmdTimer_.reset();
    workGuard_.reset();

    stopping_.store(false);
    isCmdExecuting_.store(false);
    iuLoopRunning_ = false;
    initializationCompleted_ = false;
}

boost::asio::awaitable<void> Antenna::antennaModuleInitAsync(unsigned int baudRate, std::string comPortName)
{
    try
    {
        Logger::getInstance()->FnCreateLogFile(logFileName_);

        // Both objects belong exclusively to Antenna's dedicated io_context.
        periodicSendReadIUCmdTimer_ = std::make_unique<boost::asio::steady_timer>(ioContext_);

        pSerialPort_ = std::make_unique<boost::asio::serial_port>(ioContext_);

        pSerialPort_->open(comPortName);
        pSerialPort_->set_option(boost::asio::serial_port_base::baud_rate(baudRate));
        pSerialPort_->set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
        pSerialPort_->set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::even));
        pSerialPort_->set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
        pSerialPort_->set_option(boost::asio::serial_port_base::character_size(8));

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

        const int result = co_await antennaInitAsync();

        if (result == 1 ||
            (eps_ == 2 && result == 0))
        {
            initializationCompleted_ = true;
            EventManager::getInstance()->FnEnqueueEvent("Evt_AntennaPower", true);
            Logger::getInstance()->FnLog("Antenna initialization completed.");
            Logger::getInstance()->FnLog("Antenna initialization completed.", logFileName_, "ANT");
        }
        else
        {
            initializationCompleted_ = false;
            EventManager::getInstance()->FnEnqueueEvent("Evt_AntennaPower", false);
            Logger::getInstance()->FnLog("Antenna initialization failed.");
            Logger::getInstance()->FnLog("Antenna initialization failed.", logFileName_, "ANT");
        }
    }
    catch (const boost::system::system_error& e)
    {
        initializationCompleted_ = false;
        std::stringstream ss;
        ss << __func__ << ", Boost.Asio Exception: " << e.what();
        EventManager::getInstance()->FnEnqueueEvent("Evt_AntennaPower", false);
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (const std::exception& e)
    {
        initializationCompleted_ = false;
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        EventManager::getInstance()->FnEnqueueEvent("Evt_AntennaPower", false);
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        initializationCompleted_ = false;
        std::stringstream ss;
        ss << __func__ << ", Exception: Unknown Exception";
        EventManager::getInstance()->FnEnqueueEvent("Evt_AntennaPower", false);
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }

    co_return;
}

boost::asio::awaitable<int> Antenna::antennaInitAsync()
{
    AntCmdRetCode ret = co_await antennaCmdAsync(AntCmdID::SET_ANTENNA_DATA_CMD);
    if (ret != AntCmdRetCode::AntRecv_ACK)
        co_return static_cast<int>(ret);

    ret = co_await antennaCmdAsync(AntCmdID::NO_USE_OF_ANTENNA_CMD);
    if (ret != AntCmdRetCode::AntRecv_ACK)
        co_return static_cast<int>(ret);

    ret = co_await antennaCmdAsync(AntCmdID::SET_ANTENNA_TIME_CMD);
    if (ret != AntCmdRetCode::AntRecv_ACK)
        co_return static_cast<int>(ret);

    ret = co_await antennaCmdAsync(AntCmdID::MACHINE_CHECK_CMD);
    co_return static_cast<int>(ret);
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

boost::asio::awaitable<Antenna::AntCmdRetCode> Antenna::antennaCmdAsync(AntCmdID cmdID)
{
    if (stopping_.load())
        co_return AntCmdRetCode::AntRecv_NoResp;

    // Even on one OS thread, multiple coroutines may interleave at co_await points.
    // Keep exactly one request/response transaction active on the serial port.
    if (isCmdExecuting_.exchange(true))
    {
        Logger::getInstance()->FnLog("Cmd cannot send, there is already one Cmd executing.", logFileName_, "ANT");
        co_return AntCmdRetCode::AntSend_Failed;
    }

    struct CommandExecutionGuard
    {
        std::atomic<bool>& flag;
        ~CommandExecutionGuard() { flag.store(false); }
    } guard{isCmdExecuting_};

    std::stringstream ss;
    ss << __func__ << " Command ID : " << antennaCmdIDToString(cmdID);
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "ANT");

    if (!pSerialPort_ || !pSerialPort_->is_open())
        co_return AntCmdRetCode::AntRecv_NoResp;

    int antennaCmdTimeoutInMs = 1000;
    std::vector<unsigned char> dataBuffer;

    const unsigned char antennaID = antennaId_;
    const unsigned char destID = static_cast<unsigned char>(0xB0 + antennaID);
    const unsigned char sourceID = 0x00;

    if ((++SEQUENCE_NO) == 0)
        ++SEQUENCE_NO; // sequence number is 1..255

    switch (cmdID)
    {
        case AntCmdID::MANUAL_CMD:
            break;

        case AntCmdID::SET_ANTENNA_DATA_CMD:
            antennaCmdTimeoutInMs = 1000;
            dataBuffer = loadSetAntennaData(
                destID, sourceID,
                SET_ANTENNA_DATA_CATEGORY, SET_ANTENNA_DATA_COMMAND_NO,
                SEQUENCE_NO, antennaID, 0x0001, 0x01, 0x01, 0xE1,
                0x012C, 0x003C);
            break;

        case AntCmdID::GET_ANTENNA_DATA_CMD:
            dataBuffer = loadGetAntennaData(
                destID, sourceID,
                GET_ANTENNA_DATA_CATEGORY, GET_ANTENNA_DATA_COMMAND_NO,
                SEQUENCE_NO);
            break;

        case AntCmdID::NO_USE_OF_ANTENNA_CMD:
            dataBuffer = loadNoUseOfAntenna(
                destID, sourceID,
                NO_USE_OF_ANTENNA_CATEGORY, NO_USE_OF_ANTENNA_COMMAND_NO,
                SEQUENCE_NO);
            break;

        case AntCmdID::USE_OF_ANTENNA_CMD:
            dataBuffer = loadUseOfAntenna(
                destID, sourceID,
                USE_OF_ANTENNA_CATEGORY, USE_OF_ANTENNA_COMMAND_NO,
                SEQUENCE_NO);
            break;

        case AntCmdID::SET_ANTENNA_TIME_CMD:
            dataBuffer = loadSetAntennaTime(
                destID, sourceID,
                SET_ANTENNA_TIME_CATEGORY, SET_ANTENNA_TIME_COMMAND_NO,
                SEQUENCE_NO);
            break;

        case AntCmdID::MACHINE_CHECK_CMD:
            dataBuffer = loadMachineCheck(
                destID, sourceID,
                MACHINE_CHECK_CATEGORY, MACHINE_CHECK_COMMAND_NO,
                SEQUENCE_NO);
            break;

        case AntCmdID::REQ_IO_STATUS_CMD:
            dataBuffer = loadReqIOStatus(
                destID, sourceID,
                REQ_IO_STATUS_CATEGORY, REQ_IO_STATUS_COMMAND_NO,
                SEQUENCE_NO);
            break;

        case AntCmdID::SET_OUTPUT_CMD:
            dataBuffer = loadSetOutput(
                destID, sourceID,
                SET_OUTPUT_CATEGORY, SET_OUTPUT_COMMAND_NO,
                SEQUENCE_NO, 0x01, 0x01);
            break;

        case AntCmdID::FORCE_GET_IU_NO_CMD:
            // If desired, switch this back to antennaCmdTimeoutInMillisec_.
            // antennaCmdTimeoutInMs = antennaCmdTimeoutInMillisec_;
            dataBuffer = loadForceGetUINo(
                destID, sourceID,
                FORCE_GET_IU_CATEGORY, FORCE_GET_IU_COMMAND_NO,
                SEQUENCE_NO);
            break;

        default:
        {
            std::stringstream errorStream;
            errorStream << __func__ << " : Command not found.";
            Logger::getInstance()->FnLog(errorStream.str(), logFileName_, "ANT");
            co_return AntCmdRetCode::AntRecv_CmdNotFound;
        }
    }

    AntCmdRetCode retCode = AntCmdRetCode::AntRecv_NoResp;

    // antennaCmdMaxRetry_ means number of retries AFTER the first attempt.
    // Example: maxRetry = 3 => at most 4 total attempts.
    for (int attempt = 0; attempt <= antennaCmdMaxRetry_; ++attempt)
    {
        if (stopping_.load())
            co_return AntCmdRetCode::AntRecv_NoResp;

        resetRxBuffer();
        resetState();

        const bool sendOk = co_await antennaCmdSendAsync(dataBuffer);
        if (!sendOk)
        {
            retCode = AntCmdRetCode::AntSend_Failed;
        }
        else
        {
            ReadResult result = co_await antennaReadWithTimeoutAsync(antennaCmdTimeoutInMs);

            if (result.success)
            {
                retCode = antennaHandleCmdResponse(cmdID, result.data);
                co_return retCode;
            }

            retCode = AntCmdRetCode::AntRecv_NoResp;
        }

        if (stopping_.load())
            co_return retCode;

        if (attempt < antennaCmdMaxRetry_)
        {
            std::stringstream retryStream;
            retryStream << "Retry send command, retry times : " << (attempt + 1);
            Logger::getInstance()->FnLog(retryStream.str(), logFileName_, "ANT");
        }
    }

    // Only report failure when ALL attempts actually failed.
    if (!stopping_.load())
        EventManager::getInstance()->FnEnqueueEvent("Evt_AntennaFail", 1);
    co_return retCode;
}

boost::asio::awaitable<Antenna::ReadResult> Antenna::antennaReadWithTimeoutAsync(int milliseconds)
{
    struct TimeoutState
    {
        bool timedOut = false;
    };

    std::vector<char> buffer(1024);
    std::size_t totalBytesTransferred = 0;

    auto timeoutState = std::make_shared<TimeoutState>();
    auto executor = co_await boost::asio::this_coro::executor;
    boost::asio::steady_timer timeoutTimer(executor);
    timeoutTimer.expires_after(std::chrono::milliseconds(milliseconds));

    // The timer and serial read are both serviced by the same io_context thread.
    // On timeout, cancel() wakes the suspended async_read_some co_await.
    timeoutTimer.async_wait(
        [this, timeoutState](const boost::system::error_code& ec)
        {
            if (ec)
                return; // timer was cancelled because the response completed

            timeoutState->timedOut = true;

            if (pSerialPort_ && pSerialPort_->is_open())
            {
                boost::system::error_code ignored;
                pSerialPort_->cancel(ignored);
            }
        });

    while (true)
    {
        boost::system::error_code ec;
        const std::size_t transferred =
            co_await pSerialPort_->async_read_some(
                boost::asio::buffer(buffer),
                boost::asio::redirect_error(
                    boost::asio::use_awaitable, ec));

        if (ec)
        {
            boost::system::error_code ignored;
            timeoutTimer.cancel(ignored);

            if (timeoutState->timedOut &&
                ec == boost::asio::error::operation_aborted)
            {
                std::stringstream timeoutStream;
                if (totalBytesTransferred == 0)
                    timeoutStream << __func__ << " Async Read Timeout Occurred.";
                else
                    timeoutStream << __func__ << " Async Read Timeout Occurred - Rx Not Completed.";

                Logger::getInstance()->FnLog(
                    timeoutStream.str(), logFileName_, "ANT");
            }
            else
            {
                std::stringstream errorStream;
                errorStream << __func__
                            << " Async Read error : " << ec.message();
                Logger::getInstance()->FnLog(
                    errorStream.str(), logFileName_, "ANT");
            }

            resetRxBuffer();
            resetState();
            co_return ReadResult{{}, false};
        }

        totalBytesTransferred += transferred;

        if (responseIsComplete(buffer, transferred))
        {
            boost::system::error_code ignored;
            timeoutTimer.cancel(ignored);

            std::vector<char> retBuff(getRxBuf(), getRxBuf() + getRxNum());
            co_return ReadResult{std::move(retBuff), true};
        }
    }
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

    std::stringstream responseLog;

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
        receivedRespStream << "Received Buffer Data : " << Common::getInstance()->FnGetVectorCharToHexString(dataBuff) << ", Received Buffer size : " << getRxNum();
        Logger::getInstance()->FnLog(receivedRespStream.str(), logFileName_, "ANT");

        std::string cmdID = Common::getInstance()->FnGetUCharArrayToHexString(getRxBuf()+2, 2);

        if (cmdID == "3281")
        {
            switch (cmd)
            {
                case AntCmdID::USE_OF_ANTENNA_CMD:
                {
                    responseLog << "USE_OF_ANTENNA_CMD | Response=ACK";
                    break;
                }
                case AntCmdID::NO_USE_OF_ANTENNA_CMD:
                {
                    responseLog << "NO_USE_OF_ANTENNA_CMD | Response=ACK";
                    break;
                }
                case AntCmdID::SET_ANTENNA_TIME_CMD:
                {
                    responseLog << "SET_ANTENNA_TIME_CMD | Response=ACK";
                    break;
                }
                case AntCmdID::SET_ANTENNA_DATA_CMD:
                {
                    responseLog << "SET_ANTENNA_DATA_CMD | Response=ACK";
                    break;
                }
                case AntCmdID::SET_OUTPUT_CMD:
                {
                    responseLog << "SET_OUTPUT_CMD | Response=ACK";
                    break;
                }
                default:
                {
                    responseLog << "CmdID=" << cmdID << " | Response=ACK";
                    break;
                }
            }

            retCode = AntCmdRetCode::AntRecv_ACK;
        }
        else if (cmdID == "32c1")
        {
            if (cmd == AntCmdID::GET_ANTENNA_DATA_CMD)
            {
                responseLog << "GET_ANTENNA_DATA_CMD" << " | Response=ACK";
            }
            retCode = AntCmdRetCode::AntRecv_ACK;
        }
        else if (cmdID == "3282")
        {
            switch (cmd)
            {
                case AntCmdID::USE_OF_ANTENNA_CMD:
                {
                    responseLog << "USE_OF_ANTENNA_CMD | Response=NACK";
                    break;
                }
                case AntCmdID::SET_ANTENNA_TIME_CMD:
                {
                    responseLog << "SET_ANTENNA_TIME_CMD | Response=NACK";
                    break;
                }
                case AntCmdID::SET_ANTENNA_DATA_CMD:
                {
                    responseLog << "SET_ANTENNA_DATA_CMD | Response=NACK";
                    break;
                }
                case AntCmdID::GET_ANTENNA_DATA_CMD:
                {
                    responseLog << "GET_ANTENNA_DATA_CMD | Response=NACK";
                    break;
                }
                default:
                {
                    responseLog << "CmdID=" << cmdID << " | Response=NACK";
                    break;
                }
            }
            retCode = AntCmdRetCode::AntRecv_NAK;
        }
        else if (cmdID == "3283")
        {
            if (cmd == AntCmdID::SET_ANTENNA_DATA_CMD)
            {
                responseLog << "SET_ANTENNA_DATA_CMD" << " | Response=BUSY";
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
                    successRecvIUCount_ = 1;
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

                        responseLog << "FORCE_GET_IU_NO_CMD" << " | Response=ACK" << " | IU=" << IUNumberStr;
                    }
                }
                retCode = AntCmdRetCode::AntRecv_ACK;
            }
            else
            {
                responseLog << "FORCE_GET_IU_NO_CMD" << " | Response=NACK" << " | Invalid IU=" << IUNumberStr;

                retCode = AntCmdRetCode::AntRecv_NAK;
            }
        }
        else if (cmdID == "3f82")
        {
            if (cmd == AntCmdID::FORCE_GET_IU_NO_CMD)
            {
                responseLog << "FORCE_GET_IU_NO_CMD" << " | Response=NACK";
            }
            retCode = AntCmdRetCode::AntRecv_NAK;
        }
        else if (cmdID == "31b3")
        {
            if (cmd == AntCmdID::MACHINE_CHECK_CMD)
            {
                responseLog << "MACHINE_CHECK_CMD" << " | Response=ACK";
            }
            retCode = AntCmdRetCode::AntRecv_ACK;
        }
        else if (cmdID == "31b4")
        {
            if (cmd == AntCmdID::REQ_IO_STATUS_CMD)
            {
                responseLog << "REQ_IO_STATUS_CMD" << " | Response=ACK";
            }
            retCode = AntCmdRetCode::AntRecv_ACK;
        }
    }
    else
    {
        responseLog << "Invalid Response" << " | Size=" << dataBuff.size();
    }

     responseLog << " | ReturnCode=" << static_cast<int>(retCode);

    Logger::getInstance()->FnLog(responseLog.str(), logFileName_, "ANT");

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
    dataBuf[9] = static_cast<unsigned char>((placeID >> 8) & 0xFF);
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

boost::asio::awaitable<bool> Antenna::antennaCmdSendAsync(const std::vector<unsigned char>& dataBuff)
{
    if (!pSerialPort_ || !pSerialPort_->is_open())
        co_return false;

    unsigned int txCrc = 0xFFFF;
    unsigned char tempChar = 0;
    int i = 0;

    std::memset(txBuff, 0, sizeof(txBuff));
    std::vector<unsigned char> tempBuffData(dataBuff.begin(), dataBuff.end());

    auto putByte = [this, &i](unsigned char value) -> bool
    {
        if (i >= TX_BUF_SIZE)
            return false;
        txBuff[i++] = value;
        return true;
    };

    // Start of frame.
    if (!putByte(static_cast<unsigned char>(Antenna::DLE)) ||
        !putByte(static_cast<unsigned char>(Antenna::STX)))
    {
        co_return false;
    }

    // Data, with DLE byte stuffing.
    for (unsigned char value : tempBuffData)
    {
        tempChar = value;
        if (tempChar == static_cast<unsigned char>(Antenna::DLE))
        {
            if (!putByte(static_cast<unsigned char>(Antenna::DLE)))
                co_return false;
        }

        if (!putByte(tempChar))
            co_return false;
    }

    // End of frame.
    if (!putByte(static_cast<unsigned char>(Antenna::DLE)) ||
        !putByte(static_cast<unsigned char>(Antenna::ETX)))
    {
        co_return false;
    }

    // ETX participates in CRC calculation.
    tempBuffData.push_back(static_cast<unsigned char>(Antenna::ETX));
    txCrc = CRC16R_Calculate(
        tempBuffData.data(),
        static_cast<unsigned char>(tempBuffData.size()),
        txCrc);

    if (!putByte(static_cast<unsigned char>(txCrc & 0xFF)) ||
        !putByte(static_cast<unsigned char>((txCrc >> 8) & 0xFF)))
    {
        co_return false;
    }

    TxNum_ = i;

    boost::system::error_code ec;
    const std::size_t bytesTransferred =
        co_await boost::asio::async_write(
            *pSerialPort_,
            boost::asio::buffer(txBuff, static_cast<std::size_t>(TxNum_)),
            boost::asio::redirect_error(
                boost::asio::use_awaitable, ec));

    if (ec)
    {
        std::stringstream errorStream;
        errorStream << "Error send the data to Ant serial port: "
                    << ec.message();
        Logger::getInstance()->FnLog(
            errorStream.str(), logFileName_, "ANT");
        co_return false;
    }

    co_return bytesTransferred == static_cast<std::size_t>(TxNum_);
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

boost::asio::awaitable<void> Antenna::readIULoopAsync()
{
    if (!periodicSendReadIUCmdTimer_)
    {
        continueReadFlag_.store(false);
        co_return;
    }

    antIUCmdSendCount_.store(0);

    while (continueReadFlag_.load() && !stopping_.load())
    {
        // Same behaviour as your original 100 ms periodic timer, but expressed
        // as a linear coroutine instead of callback recursion.
        periodicSendReadIUCmdTimer_->expires_after(std::chrono::milliseconds(100));

        boost::system::error_code timerEc;
        co_await periodicSendReadIUCmdTimer_->async_wait(
            boost::asio::redirect_error(
                boost::asio::use_awaitable, timerEc));

        if (!continueReadFlag_.load())
            break;

        if (timerEc == boost::asio::error::operation_aborted)
            continue;

        if (timerEc)
        {
            std::stringstream ss;
            ss << __func__ << " timer error : " << timerEc.message();
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "ANT");
            break;
        }

        // Initialization or another serial transaction may still be active.
        // Since this is one io thread, simply skip this tick instead of starting
        // a second request/response transaction.
        if (isCmdExecuting_.load())
            continue;

        const AntCmdRetCode ret = co_await antennaCmdAsync(AntCmdID::FORCE_GET_IU_NO_CMD);
        (void)ret;

        const int count = antIUCmdSendCount_.fetch_add(1) + 1;

        if (successRecvIUFlag_)
        {
            continueReadFlag_.store(false);
            successRecvIUFlag_ = false;

            EventManager::getInstance()->FnEnqueueEvent("Evt_AntennaIUCome", IUNumber_);
            break;
        }

        if (count >= 20)
        {
            std::stringstream countStream;
            countStream << "antIUCmdSendCount_: " << count;
            Logger::getInstance()->FnLog(countStream.str(), logFileName_, "ANT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_AntennaFail", 2);
            antIUCmdSendCount_.store(0);
        }
    }

    iuLoopRunning_ = false;
    co_return;
}

void Antenna::FnAntennaSendReadIUCmd()
{
    if (!moduleRunning_.load() || stopping_.load())
        return;

    bool expected = false;
    if (!continueReadFlag_.compare_exchange_strong(expected, true))
        return; // already requested/running

    boost::asio::post(
        ioContext_,
        [this]()
        {
            if (!pSerialPort_ || !pSerialPort_->is_open())
            {
                continueReadFlag_.store(false);
                return;
            }

            if (iuLoopRunning_)
                return;

            iuLoopRunning_ = true;

            boost::asio::co_spawn(
                ioContext_,
                readIULoopAsync(),
                [this](std::exception_ptr ep)
                {
                    iuLoopRunning_ = false;

                    if (!ep)
                        return;

                    continueReadFlag_.store(false);

                    try
                    {
                        std::rethrow_exception(ep);
                    }
                    catch (const std::exception& e)
                    {
                        std::stringstream ss;
                        ss << "readIULoopAsync exception: " << e.what();
                        Logger::getInstance()->FnLogExceptionError(ss.str());
                    }
                });
        });
}


void Antenna::FnAntennaStopRead()
{
    continueReadFlag_.store(false);

    if (!moduleRunning_.load())
        return;

    // Do not touch the timer directly from a foreign thread. Serialize the
    // cancellation onto Antenna's dedicated io_context thread.
    boost::asio::post(
        ioContext_,
        [this]()
        {
            if (periodicSendReadIUCmdTimer_)
            {
                boost::system::error_code ignored;
                periodicSendReadIUCmdTimer_->cancel(ignored);
            }
        });
}

int Antenna::FnAntennaGetIUCmdSendCount()
{
    return antIUCmdSendCount_.load();
}