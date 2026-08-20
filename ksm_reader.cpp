#include "ksm_reader.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <string_view>

#if defined(__linux__)
#include <pthread.h>
#endif

#include "common.h"
#include "event_manager.h"
#include "log.h"

KSM_Reader::KSM_Reader()
    : commandWakeTimer_(ioContext_),
      lastSerialReadTime_(std::chrono::steady_clock::now())
{
}

KSM_Reader::~KSM_Reader()
{
    shutdownFromDestructor();
}

KSM_Reader* KSM_Reader::getInstance()
{
    static KSM_Reader instance;
    return &instance;
}

int KSM_Reader::FnKSMReaderInit(unsigned int baudRate, const std::string& comPortName)
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true))
    {
        Logger::getInstance()->FnLog("KSM Reader module is already running.", logFileName_, "KSM");
        return 1;
    }

    // Lifecycle operations are expected to be serialized by the application.
    // If an earlier run has finished but was not joined yet, join it now.
    if (ioThread_.joinable())
    {
        if (std::this_thread::get_id() == ioThread_.get_id())
        {
            running_.store(false);
            Logger::getInstance()->FnLogExceptionError("FnKSMReaderInit cannot restart KSM Reader from its own io thread.");
            return static_cast<int>(KSMReaderCmdRetCode::KSMReaderComm_Error);
        }

        ioThread_.join();
    }

    try
    {
        Logger::getInstance()->FnCreateLogFile(logFileName_);

        stopping_.store(false);
        stopRequested_ = false;
        commandQueue_.clear();
        continueReadCard_ = false;
        blockGetStatusCmdLog_ = false;
        cardPresented_ = false;
        currentCmd_ = KSMReaderCmdID::INIT_CMD;
        lastSerialReadTime_ = std::chrono::steady_clock::now();

        ioContext_.restart();
        workGuard_.emplace(boost::asio::make_work_guard(ioContext_));

        serialPort_ = std::make_unique<boost::asio::serial_port>(ioContext_);
        serialPort_->open(comPortName);
        serialPort_->set_option(boost::asio::serial_port_base::baud_rate(baudRate));
        serialPort_->set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
        serialPort_->set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
        serialPort_->set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
        serialPort_->set_option(boost::asio::serial_port_base::character_size(8));

        std::stringstream ss;
        ss << "[START] KSM Reader | Port=" << comPortName << " | BaudRate=" << baudRate;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "KSM");

        boost::asio::co_spawn(
            ioContext_,
            commandLoop(),
            [this](std::exception_ptr ep)
            {
                if (ep)
                {
                    try
                    {
                        std::rethrow_exception(ep);
                    }
                    catch (const std::exception& e)
                    {
                        std::stringstream ss;
                        ss << "commandLoop exception: " << e.what();
                        Logger::getInstance()->FnLogExceptionError(ss.str());
                    }
                    catch (...)
                    {
                        Logger::getInstance()->FnLogExceptionError("commandLoop exception: Unknown exception");
                    }
                }
            });

        ioThread_ = std::thread([this]()
        {
#if defined(__linux__)
            ::pthread_setname_np(::pthread_self(), "KSM_READER_IO");
#endif
            runIoContext();
        });

        postCommand(KSMReaderCmdID::INIT_CMD);
        return 1;
    }
    catch (const boost::system::system_error& e)
    {
        std::stringstream ss;
        ss << __func__ << " | Boost.Asio exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << " | Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        Logger::getInstance()->FnLogExceptionError(std::string(__func__) + " | Unknown exception");
    }

    running_.store(false);
    stopping_.store(false);

    if (serialPort_)
    {
        boost::system::error_code ignored;
        serialPort_->cancel(ignored);
        serialPort_->close(ignored);
        serialPort_.reset();
    }

    workGuard_.reset();
    ioContext_.stop();

    if (ioThread_.joinable() &&
        std::this_thread::get_id() != ioThread_.get_id())
    {
        ioThread_.join();
    }

    return static_cast<int>(KSMReaderCmdRetCode::KSMReaderComm_Error);
}

void KSM_Reader::FnKSMReaderClose()
{
    const bool wasRunning = running_.exchange(false);
    stopping_.store(true);

    if (wasRunning || ioThread_.joinable())
    {
        boost::asio::post(ioContext_, [this]()
        {
            requestStopOnIoThread();
        });

        // Normal shutdown: after cancellation is posted, release the guard.
        // Existing handlers/coroutines drain and run() returns naturally.
        workGuard_.reset();
    }

    if (ioThread_.joinable())
    {
        if (std::this_thread::get_id() == ioThread_.get_id())
        {
            return;
        }

        ioThread_.join();
    }

    stopping_.store(false);
    Logger::getInstance()->FnLog("[STOP] KSM Reader stopped.", logFileName_, "KSM");
}


void KSM_Reader::runIoContext()
{
    try
    {
        ioContext_.run();
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << " | Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        Logger::getInstance()->FnLogExceptionError(std::string(__func__) + " | Unknown exception");
    }

    running_.store(false);
}

void KSM_Reader::requestStopOnIoThread()
{
    stopRequested_ = true;
    continueReadCard_ = false;
    commandQueue_.clear();

    boost::system::error_code ignored;
    commandWakeTimer_.cancel(ignored);

    if (serialPort_)
    {
        serialPort_->cancel(ignored);
        serialPort_->close(ignored);
    }
}

void KSM_Reader::shutdownFromDestructor()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "KSM");

    try
    {
        stopping_.store(true);
        running_.store(false);

        // Emergency destructor fallback. Normal application shutdown should
        // use FnKSMReaderClose(), which performs graceful cancellation/drain.
        workGuard_.reset();
        ioContext_.stop();

        if (ioThread_.joinable() &&
            std::this_thread::get_id() != ioThread_.get_id())
        {
            ioThread_.join();
        }

        // The io thread is no longer touching the serial object here.
        if (serialPort_)
        {
            boost::system::error_code ignored;
            serialPort_->close(ignored);
        }
    }
    catch (...)
    {
        // Destructor must not throw.
    }
}

void KSM_Reader::FnKSMReaderEnable(bool enable)
{
    if (!running_.load() || stopping_.load())
    {
        return;
    }

    boost::asio::post(ioContext_, [this, enable]()
    {
        if (stopRequested_)
        {
            return;
        }

        if (enable)
        {
            continueReadCard_ = true;
            enqueueCommandOnIoThread(KSMReaderCmdID::CARD_ALLOWED_CMD);
            enqueueCommandOnIoThread(KSMReaderCmdID::GET_STATUS_CMD);
        }
        else
        {
            continueReadCard_ = false;
            removeQueuedGetStatusCommands();
            enqueueCommandOnIoThread(KSMReaderCmdID::CARD_PROHIBITED_CMD);
        }
    });
}

void KSM_Reader::FnKSMReaderReadCardInfo()
{
    postCommand(KSMReaderCmdID::CARD_ON_IC_CMD);
}

void KSM_Reader::FnKSMReaderSendInit()
{
    postCommand(KSMReaderCmdID::INIT_CMD);
}

void KSM_Reader::FnKSMReaderSendGetStatus()
{
    postCommand(KSMReaderCmdID::GET_STATUS_CMD);
}

void KSM_Reader::FnKSMReaderStartGetStatus()
{
    if (!running_.load() || stopping_.load())
    {
        return;
    }

    boost::asio::post(ioContext_, [this]()
    {
        if (stopRequested_)
        {
            return;
        }

        continueReadCard_ = true;
        enqueueCommandOnIoThread(KSMReaderCmdID::GET_STATUS_CMD);
    });
}

void KSM_Reader::FnKSMReaderSendEjectToFront()
{
    postCommand(KSMReaderCmdID::EJECT_TO_FRONT_CMD);
}

void KSM_Reader::postCommand(KSMReaderCmdID cmd)
{
    if (!running_.load() || stopping_.load())
    {
        return;
    }

    boost::asio::post(ioContext_, [this, cmd]()
    {
        enqueueCommandOnIoThread(cmd);
    });
}

void KSM_Reader::enqueueCommandOnIoThread(KSMReaderCmdID cmd)
{
    if (stopRequested_ || !serialPort_ || !serialPort_->is_open())
    {
        ksmLogger("Unable to enqueue command: serial port is not open.", true);
        return;
    }

    // Continuous status polling should never build up duplicate GET_STATUS
    // commands while another status command is already queued/executing.
    if (cmd == KSMReaderCmdID::GET_STATUS_CMD)
    {
        if (hasQueuedCommand(cmd))
        {
            return;
        }
    }

    commandQueue_.push_back(cmd);

    if (cmd != KSMReaderCmdID::GET_STATUS_CMD || !continueReadCard_)
    {
        std::stringstream ss;
        ss << "[QUEUE] " << KSMReaderCmdIDToString(cmd) << " | Size=" << commandQueue_.size();
        ksmLogger(ss.str());
    }

    boost::system::error_code ignored;
    commandWakeTimer_.cancel(ignored);
}

void KSM_Reader::removeQueuedGetStatusCommands()
{
    commandQueue_.erase(
        std::remove(
            commandQueue_.begin(),
            commandQueue_.end(),
            KSMReaderCmdID::GET_STATUS_CMD),
        commandQueue_.end());
}

bool KSM_Reader::hasQueuedCommand(KSMReaderCmdID cmd) const
{
    return std::find(commandQueue_.begin(), commandQueue_.end(), cmd) != commandQueue_.end();
}

boost::asio::awaitable<void> KSM_Reader::commandLoop()
{
    while (!stopRequested_)
    {
        if (commandQueue_.empty())
        {
            commandWakeTimer_.expires_at(std::chrono::steady_clock::time_point::max());

            boost::system::error_code waitError;
            co_await commandWakeTimer_.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, waitError));

            if (stopRequested_)
            {
                break;
            }

            continue;
        }

        currentCmd_ = commandQueue_.front();
        commandQueue_.pop_front();
        const KSMReaderCmdID cmd = currentCmd_;

        KSMReaderCmdRetCode result = KSMReaderCmdRetCode::KSMReaderComm_Error;

        try
        {
            result = co_await executeCommand(cmd);
        }
        catch (const std::exception& e)
        {
            std::stringstream ss;
            ss << "[ERROR] " << KSMReaderCmdIDToString(cmd) << " | " << e.what();
            ksmLogger(ss.str(), true);
            result = KSMReaderCmdRetCode::KSMReaderComm_Error;
        }
        catch (...)
        {
            std::stringstream ss;
            ss << "[ERROR] " << KSMReaderCmdIDToString(cmd) << " | Unknown exception";
            ksmLogger(ss.str(), true);
            result = KSMReaderCmdRetCode::KSMReaderComm_Error;
        }


        if (stopRequested_)
        {
            break;
        }

        if (result != KSMReaderCmdRetCode::KSMReaderRecv_ACK)
        {
            handleCommandFailure(cmd, result);
        }
    }

    co_return;
}

boost::asio::awaitable<KSM_Reader::KSMReaderCmdRetCode> KSM_Reader::executeCommand(KSMReaderCmdID cmd)
{
    const auto commandFrame = buildCommandFrame(cmd);

    if (commandFrame.empty())
    {
        co_return KSMReaderCmdRetCode::KSMReaderRecv_CmdNotFound;
    }

    if (cmd != KSMReaderCmdID::GET_STATUS_CMD || !continueReadCard_)
    {
        std::stringstream ss;
        ss << "[CMD] " << KSMReaderCmdIDToString(cmd);
        ksmLogger(ss.str());
    }

    // 1. Send command
    if (!co_await writeBytes(commandFrame, WRITE_TIMEOUT, "COMMAND"))
    {
        co_return KSMReaderCmdRetCode::KSMReaderSend_Failed;
    }

    // 2. Wait ACK / NAK
    const auto ackResult = co_await waitForAck(ACK_TIMEOUT);
    if (ackResult != KSMReaderCmdRetCode::KSMReaderRecv_ACK)
    {
        co_return ackResult;
    }

    // 3. Send ENQ
    const std::vector<std::uint8_t> enqData{ENQ};

    if (!co_await writeBytes(enqData, WRITE_TIMEOUT, "ENQ"))
    {
        co_return KSMReaderCmdRetCode::KSMReaderSend_Failed;
    }

    // 4. Wait response
    auto response = co_await readResponseFrame(RESPONSE_TIMEOUT);
    if (response.result != KSMReaderCmdRetCode::KSMReaderRecv_ACK)
    {
        co_return response.result;
    }

    co_return handleCommandResponse(cmd, response.frame);
}

boost::asio::awaitable<void> KSM_Reader::waitForWriteGap()
{
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = now - lastSerialReadTime_;

    if (elapsed >= MIN_WRITE_GAP)
    {
        co_return;
    }

    boost::asio::steady_timer gapTimer(ioContext_);
    gapTimer.expires_after(MIN_WRITE_GAP - elapsed);

    boost::system::error_code ec;
    co_await gapTimer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ec));

    co_return;
}

boost::asio::awaitable<bool> KSM_Reader::writeBytes(
    const std::vector<std::uint8_t>& data,
    std::chrono::steady_clock::duration timeout,
    const char* description)
{
    if (stopRequested_ || !serialPort_ || !serialPort_->is_open())
    {
        co_return false;
    }

    co_await waitForWriteGap();

    if (stopRequested_ || !serialPort_ || !serialPort_->is_open())
    {
        co_return false;
    }

    if (currentCmd_ != KSMReaderCmdID::GET_STATUS_CMD || !continueReadCard_)
    {
        std::stringstream ss;
        ss << "[TX] " << description << " | Bytes=" << data.size() << " | Data="
           << Common::getInstance()->FnGetDisplayVectorCharToHexString(data);
        ksmLogger(ss.str());
    }

    auto timeoutState = std::make_shared<TimeoutState>();
    boost::asio::steady_timer timeoutTimer(ioContext_);
    timeoutTimer.expires_after(timeout);

    timeoutTimer.async_wait(
        [this, timeoutState](const boost::system::error_code& ec)
        {
            if (!ec && !timeoutState->completed)
            {
                timeoutState->timedOut = true;

                if (serialPort_ && serialPort_->is_open())
                {
                    boost::system::error_code ignored;
                    serialPort_->cancel(ignored);
                }
            }
        });

    boost::system::error_code writeError;
    const std::size_t bytesTransferred =
        co_await boost::asio::async_write(
            *serialPort_,
            boost::asio::buffer(data),
            boost::asio::redirect_error(boost::asio::use_awaitable, writeError));

    timeoutState->completed = true;
    boost::system::error_code ignored;
    timeoutTimer.cancel(ignored);

    if (timeoutState->timedOut)
    {
        std::stringstream ss;
        ss << "[TIMEOUT] Serial write | " << description;
        ksmLogger(ss.str(), true);
        co_return false;
    }

    if (writeError)
    {
        if (!(stopRequested_ &&
              writeError == boost::asio::error::operation_aborted))
        {
            std::stringstream ss;
            ss << "[ERROR] Serial write | " << description << " | " << writeError.message();
            ksmLogger(ss.str(), true);
        }
        co_return false;
    }

    if (bytesTransferred != data.size())
    {
        std::stringstream ss;
        ss << "[ERROR] Partial serial write | " << description
           << " | Written=" << bytesTransferred
           << " | Expected=" << data.size();
        ksmLogger(ss.str(), true);
        co_return false;
    }

    co_return true;
}

boost::asio::awaitable<KSM_Reader::KSMReaderCmdRetCode>
KSM_Reader::waitForAck(std::chrono::steady_clock::duration timeout)
{
    if (stopRequested_ || !serialPort_ || !serialPort_->is_open())
    {
        co_return KSMReaderCmdRetCode::KSMReaderComm_Error;
    }

    auto timeoutState = std::make_shared<TimeoutState>();
    boost::asio::steady_timer timeoutTimer(ioContext_);
    timeoutTimer.expires_after(timeout);

    timeoutTimer.async_wait(
        [this, timeoutState](const boost::system::error_code& ec)
        {
            if (!ec && !timeoutState->completed)
            {
                timeoutState->timedOut = true;

                if (serialPort_ && serialPort_->is_open())
                {
                    boost::system::error_code ignored;
                    serialPort_->cancel(ignored);
                }
            }
        });

    for (;;)
    {
        std::uint8_t byte = 0;
        boost::system::error_code readError;

        co_await boost::asio::async_read(
            *serialPort_,
            boost::asio::buffer(&byte, 1),
            boost::asio::redirect_error(boost::asio::use_awaitable, readError));

        if (!readError)
        {
            lastSerialReadTime_ = std::chrono::steady_clock::now();

            if (byte == ACK)
            {
                timeoutState->completed = true;
                boost::system::error_code ignored;
                timeoutTimer.cancel(ignored);

                if (currentCmd_ != KSMReaderCmdID::GET_STATUS_CMD ||
                    !continueReadCard_)
                {
                    ksmLogger("[RX] ACK");
                }

                co_return KSMReaderCmdRetCode::KSMReaderRecv_ACK;
            }

            if (byte == NAK)
            {
                timeoutState->completed = true;
                boost::system::error_code ignored;
                timeoutTimer.cancel(ignored);
                ksmLogger("[RX] NAK", true);
                co_return KSMReaderCmdRetCode::KSMReaderRecv_NAK;
            }

            // Ignore unrelated bytes while waiting for ACK/NAK, but keep the
            // original overall deadline instead of restarting the timeout.
            continue;
        }

        timeoutState->completed = true;
        boost::system::error_code ignored;
        timeoutTimer.cancel(ignored);

        if (timeoutState->timedOut)
        {
            ksmLogger("[TIMEOUT] Waiting for ACK", true);
            co_return KSMReaderCmdRetCode::KSMReaderRecv_NAK;
        }

        if (stopRequested_ && readError == boost::asio::error::operation_aborted)
        {
            co_return KSMReaderCmdRetCode::KSMReaderComm_Error;
        }

        std::stringstream ss;
        ss << "[ERROR] Waiting for ACK | " << readError.message();
        ksmLogger(ss.str(), true);
        co_return KSMReaderCmdRetCode::KSMReaderComm_Error;
    }
}

boost::asio::awaitable<KSM_Reader::ResponseReadResult>
KSM_Reader::readResponseFrame(std::chrono::steady_clock::duration timeout)
{
    ResponseReadResult result;

    if (stopRequested_ || !serialPort_ || !serialPort_->is_open())
    {
        result.result = KSMReaderCmdRetCode::KSMReaderComm_Error;
        co_return result;
    }

    auto timeoutState = std::make_shared<TimeoutState>();
    boost::asio::steady_timer timeoutTimer(ioContext_);
    timeoutTimer.expires_after(timeout);

    timeoutTimer.async_wait(
        [this, timeoutState](const boost::system::error_code& ec)
        {
            if (!ec && !timeoutState->completed)
            {
                timeoutState->timedOut = true;

                if (serialPort_ && serialPort_->is_open())
                {
                    boost::system::error_code ignored;
                    serialPort_->cancel(ignored);
                }
            }
        });

    bool receivingFrame = false;
    bool waitingForBcc = false;
    std::uint8_t calculatedBcc = 0;

    for (;;)
    {
        std::uint8_t byte = 0;
        boost::system::error_code readError;

        co_await boost::asio::async_read(
            *serialPort_,
            boost::asio::buffer(&byte, 1),
            boost::asio::redirect_error(boost::asio::use_awaitable, readError));

        if (readError)
        {
            timeoutState->completed = true;
            boost::system::error_code ignored;
            timeoutTimer.cancel(ignored);

            if (timeoutState->timedOut)
            {
                result.result = KSMReaderCmdRetCode::KSMReaderRecv_NoResp;
                ksmLogger("[TIMEOUT] Waiting for response", true);
                co_return result;
            }

            if (stopRequested_ && readError == boost::asio::error::operation_aborted)
            {
                result.result = KSMReaderCmdRetCode::KSMReaderComm_Error;
                co_return result;
            }

            std::stringstream ss;
            ss << "[ERROR] Reading response | " << readError.message();
            ksmLogger(ss.str(), true);
            result.result = KSMReaderCmdRetCode::KSMReaderComm_Error;
            co_return result;
        }

        lastSerialReadTime_ = std::chrono::steady_clock::now();

        if (!receivingFrame)
        {
            if (byte != STX)
            {
                continue;
            }

            receivingFrame = true;
            calculatedBcc = STX;
            result.frame.clear();
            result.frame.push_back(static_cast<char>(byte));
            continue;
        }

        if (waitingForBcc)
        {
            timeoutState->completed = true;
            boost::system::error_code ignored;
            timeoutTimer.cancel(ignored);

            if (byte != calculatedBcc)
            {
                std::stringstream ss;
                ss << "[ERROR] Response BCC mismatch"
                   << " | Expected=" << static_cast<int>(calculatedBcc)
                   << " | Received=" << static_cast<int>(byte);
                ksmLogger(ss.str(), true);
                result.result = KSMReaderCmdRetCode::KSMReaderRecv_CRCErr;
                co_return result;
            }

            if (currentCmd_ != KSMReaderCmdID::GET_STATUS_CMD ||
                !continueReadCard_)
            {
                std::stringstream ss;
                ss << "[RX] RESPONSE"
                   << " | Bytes=" << result.frame.size()
                   << " | Data="
                   << Common::getInstance()->FnGetVectorCharToHexString(
                          result.frame);
                ksmLogger(ss.str());
            }

            result.result = KSMReaderCmdRetCode::KSMReaderRecv_ACK;
            co_return result;
        }

        if (result.frame.size() >= RX_FRAME_MAX_SIZE)
        {
            timeoutState->completed = true;
            boost::system::error_code ignored;
            timeoutTimer.cancel(ignored);

            ksmLogger("[ERROR] Response frame exceeds maximum size", true);
            result.result = KSMReaderCmdRetCode::KSMReaderRecv_CRCErr;
            co_return result;
        }

        result.frame.push_back(static_cast<char>(byte));
        calculatedBcc ^= byte;

        if (byte == ETX)
        {
            waitingForBcc = true;
        }
    }
}

std::vector<std::uint8_t>
KSM_Reader::buildCommandPayload(KSMReaderCmdID cmd) const
{
    switch (cmd)
    {
        case KSMReaderCmdID::INIT_CMD:
            return {0x00, 0x02, 0x30, 0x31};

        case KSMReaderCmdID::CARD_PROHIBITED_CMD:
            return {0x00, 0x03, 0x2F, 0x31, 0x31};

        case KSMReaderCmdID::CARD_ALLOWED_CMD:
            return {0x00, 0x03, 0x2F, 0x33, 0x30};

        case KSMReaderCmdID::CARD_ON_IC_CMD:
            return {0x00, 0x02, 0x32, 0x2F};

        case KSMReaderCmdID::IC_POWER_ON_CMD:
            return {0x00, 0x02, 0x33, 0x30};

        case KSMReaderCmdID::WARM_RESET_CMD:
            return {0x00, 0x02, 0x37, 0x2F};

        case KSMReaderCmdID::SELECT_FILE1_CMD:
            return {
                0x00, 0x0B, 0x37, 0x31,
                0x00, 0x07, 0x00, 0xA4,
                0x01, 0x00, 0x02, 0x01, 0x18};

        case KSMReaderCmdID::SELECT_FILE2_CMD:
            return {
                0x00, 0x0B, 0x37, 0x31,
                0x00, 0x07, 0x00, 0xA4,
                0x02, 0x00, 0x02, 0x00, 0x01};

        case KSMReaderCmdID::READ_CARD_INFO_CMD:
            return {
                0x00, 0x09, 0x37, 0x31,
                0x00, 0x05, 0x00, 0xB0,
                0x00, 0x00, 0x10};

        case KSMReaderCmdID::READ_CARD_BALANCE_CMD:
            return {
                0x00, 0x0D, 0x37, 0x31,
                0x00, 0x09, 0x80, 0x32,
                0x00, 0x03, 0x04, 0x00,
                0x00, 0x00, 0x00};

        case KSMReaderCmdID::IC_POWER_OFF_CMD:
            return {0x00, 0x02, 0x33, 0x31};

        case KSMReaderCmdID::EJECT_TO_FRONT_CMD:
            return {0x00, 0x02, 0x32, 0x31};

        case KSMReaderCmdID::GET_STATUS_CMD:
            return {0x00, 0x02, 0x31, 0x30};
    }

    return {};
}

std::vector<std::uint8_t>
KSM_Reader::buildCommandFrame(KSMReaderCmdID cmd) const
{
    const auto payload = buildCommandPayload(cmd);
    if (payload.empty())
    {
        return {};
    }

    std::vector<std::uint8_t> frame;
    frame.reserve(payload.size() + 3);

    std::uint8_t bcc = STX;
    frame.push_back(STX);

    for (const auto byte : payload)
    {
        frame.push_back(byte);
        bcc ^= byte;
    }

    frame.push_back(ETX);
    bcc ^= ETX;
    frame.push_back(bcc);

    return frame;
}

KSM_Reader::KSMReaderCmdRetCode
KSM_Reader::handleCommandResponse(KSMReaderCmdID cmd, const std::vector<char>& dataBuff)
{
    if (dataBuff.size() < 6)
    {
        ksmLogger("[ERROR] Response frame too short", true);
        return KSMReaderCmdRetCode::KSMReaderRecv_CmdNotFound;
    }

    if (static_cast<std::uint8_t>(dataBuff.front()) != STX)
    {
        ksmLogger("[ERROR] Response STX mismatch", true);
        return KSMReaderCmdRetCode::KSMReaderRecv_CRCErr;
    }

    if (static_cast<std::uint8_t>(dataBuff.back()) != ETX)
    {
        ksmLogger("[ERROR] Response ETX mismatch", true);
        return KSMReaderCmdRetCode::KSMReaderRecv_CRCErr;
    }

    const auto start = dataBuff.begin() + 1;
    const auto end = dataBuff.end() - 1;
    const std::vector<char> recvCmd(start, end);

    if (recvCmd.size() < 4)
    {
        ksmLogger("[ERROR] Response command payload too short", true);
        return KSMReaderCmdRetCode::KSMReaderRecv_CmdNotFound;
    }

    const auto cmdCodeMSB = static_cast<std::uint8_t>(recvCmd[2]);
    const auto cmdCodeLSB = static_cast<std::uint8_t>(recvCmd[3]);

    if (cmdCodeMSB == 0x2F)
    {
        if (cmdCodeLSB == 0x31)
        {
            ksmLogger("[RSP] CARD_PROHIBITED_CMD | ACK");
            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderCardProhibited", true);
            return KSMReaderCmdRetCode::KSMReaderRecv_ACK;
        }

        if (cmdCodeLSB == 0x33)
        {
            ksmLogger("[RSP] CARD_ALLOWED_CMD | ACK");
            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderCardAllowed", true);
            return KSMReaderCmdRetCode::KSMReaderRecv_ACK;
        }
    }
    else if (cmdCodeMSB == 0x30)
    {
        if (cmdCodeLSB == 0x31)
        {
            ksmLogger("[RSP] INIT_CMD | ACK");
            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderInit", true);
            return KSMReaderCmdRetCode::KSMReaderRecv_ACK;
        }

        ksmLogger("[RSP] INIT_CMD | NAK", true);
        return KSMReaderCmdRetCode::KSMReaderRecv_NAK;
    }
    else if (cmdCodeMSB == 0x31 && cmdCodeLSB == 0x30)
    {
        if (recvCmd.size() < 5)
        {
            ksmLogger("[ERROR] GET_STATUS response too short", true);
            return KSMReaderCmdRetCode::KSMReaderRecv_CmdNotFound;
        }

        const auto status = static_cast<std::uint8_t>(recvCmd[4]);

        if (status == 0x4A || status == 0x4B)
        {
            if (!cardPresented_)
            {
                cardPresented_ = true;
                continueReadCard_ = false;
                removeQueuedGetStatusCommands();

                EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderCardIn", true);
                ksmLogger("[RSP] GET_STATUS_CMD | Card In", true);
            }

            return KSMReaderCmdRetCode::KSMReaderRecv_ACK;
        }

        if (status == 0x4E)
        {
            if (cardPresented_)
            {
                cardPresented_ = false;
                EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderCardTakeAway", true);
                ksmLogger("[RSP] GET_STATUS_CMD | Card Out", true);
            }
            else if (continueReadCard_)
            {
                enqueueCommandOnIoThread(KSMReaderCmdID::GET_STATUS_CMD);
            }

            return KSMReaderCmdRetCode::KSMReaderRecv_ACK;
        }

        if (continueReadCard_)
        {
            enqueueCommandOnIoThread(KSMReaderCmdID::GET_STATUS_CMD);
        }

        return KSMReaderCmdRetCode::KSMReaderRecv_ACK;
    }
    else if (cmdCodeMSB == 0x32)
    {
        if (cmdCodeLSB == 0x2F)
        {
            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderCardOnIc", true);
            ksmLogger("[RSP] CARD_ON_IC_CMD | ACK");
            enqueueCommandOnIoThread(KSMReaderCmdID::IC_POWER_ON_CMD);
            return KSMReaderCmdRetCode::KSMReaderRecv_ACK;
        }

        if (cmdCodeLSB == 0x31)
        {
            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderEjectToFront", true);
            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderCardOut", true);
            ksmLogger("[RSP] EJECT_TO_FRONT_CMD | ACK");
            return KSMReaderCmdRetCode::KSMReaderRecv_ACK;
        }
    }
    else if (cmdCodeMSB == 0x33)
    {
        if (cmdCodeLSB == 0x30)
        {
            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderIcPowerOn", true);
            ksmLogger("[RSP] IC_POWER_ON_CMD | ACK");
            enqueueCommandOnIoThread(KSMReaderCmdID::WARM_RESET_CMD);
            return KSMReaderCmdRetCode::KSMReaderRecv_ACK;
        }

        if (cmdCodeLSB == 0x31)
        {
            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderIcPowerOff", true);
            ksmLogger("[RSP] IC_POWER_OFF_CMD | ACK");
            enqueueCommandOnIoThread(KSMReaderCmdID::EJECT_TO_FRONT_CMD);
            return KSMReaderCmdRetCode::KSMReaderRecv_ACK;
        }
    }
    else if (cmdCodeMSB == 0x37)
    {
        if (cmdCodeLSB == 0x2F)
        {
            EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderWarmReset", true);
            ksmLogger("[RSP] WARM_RESET_CMD | ACK");
            enqueueCommandOnIoThread(KSMReaderCmdID::SELECT_FILE1_CMD);
            return KSMReaderCmdRetCode::KSMReaderRecv_ACK;
        }

        if (cmdCodeLSB == 0x31)
        {
            if (recvCmd.size() < 5)
            {
                ksmLogger("[ERROR] 0x37/0x31 response too short", true);
                return KSMReaderCmdRetCode::KSMReaderRecv_CmdNotFound;
            }

            const auto status = static_cast<std::uint8_t>(recvCmd[4]);
            if (status == 0x4E)
            {
                ksmLogger("[RSP] Card read command | ERROR", true);
                return KSMReaderCmdRetCode::KSMReaderRecv_NoResp;
            }

            if (cmd == KSMReaderCmdID::SELECT_FILE1_CMD)
            {
                EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderSelectFile1", true);
                ksmLogger("[RSP] SELECT_FILE1_CMD | ACK");
                enqueueCommandOnIoThread(KSMReaderCmdID::SELECT_FILE2_CMD);
                return KSMReaderCmdRetCode::KSMReaderRecv_ACK;
            }

            if (cmd == KSMReaderCmdID::SELECT_FILE2_CMD)
            {
                EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderSelectFile2", true);
                ksmLogger("[RSP] SELECT_FILE2_CMD | ACK");
                enqueueCommandOnIoThread(KSMReaderCmdID::READ_CARD_INFO_CMD);
                return KSMReaderCmdRetCode::KSMReaderRecv_ACK;
            }

            if (cmd == KSMReaderCmdID::READ_CARD_INFO_CMD)
            {
                // Original protocol parsing uses indexes [8..15] for card no.
                // and [19..21] for expiry data inside recvCmd.
                if (recvCmd.size() < 22)
                {
                    ksmLogger("[ERROR] READ_CARD_INFO response too short", true);
                    return KSMReaderCmdRetCode::KSMReaderRecv_CmdNotFound;
                }

                const std::vector<char> cardNumber(recvCmd.begin() + 8, recvCmd.begin() + 16);
                const std::string cardNum = Common::getInstance()->FnGetVectorCharToHexString(cardNumber);

                const std::vector<char> expireDate(recvCmd.begin() + 19, recvCmd.begin() + 22);
                const std::string expireDateStr = Common::getInstance()->FnGetVectorCharToHexString(expireDate);

                if (expireDateStr.size() < 6)
                {
                    ksmLogger("[ERROR] Invalid card expiry data", true);
                    return KSMReaderCmdRetCode::KSMReaderRecv_CmdNotFound;
                }

                const int year2 = std::atoi(expireDateStr.substr(0, 2).c_str());
                const int month = std::atoi(expireDateStr.substr(2, 2).c_str());
                int expiryMonths = std::atoi(expireDateStr.substr(4, 2).c_str());

                const int fullYear = (year2 > 90) ? 1900 + year2 : 2000 + year2;

                // Preserve the original application's expiry calculation.
                expiryMonths += month;
                const int expiryYear = fullYear + (expiryMonths / 12);
                const int expiryMonth = expiryMonths % 12;
                const int expiryYearMonth = (expiryYear * 100) + expiryMonth;

                const bool expired = std::to_string(expiryYearMonth) <= Common::getInstance()->FnGetDateTimeFormat_yyyymm();

                updateCardInfo(cardNum, expiryYearMonth, expired);

                EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderReadCardInfo", true);

                std::stringstream ss;
                ss << "[RSP] READ_CARD_INFO_CMD | ACK" << " | CardNo=" << cardNum << " | Expiry=" << expiryYearMonth;
                ksmLogger(ss.str());

                enqueueCommandOnIoThread(KSMReaderCmdID::READ_CARD_BALANCE_CMD);
                return KSMReaderCmdRetCode::KSMReaderRecv_ACK;
            }

            if (cmd == KSMReaderCmdID::READ_CARD_BALANCE_CMD)
            {
                if (recvCmd.size() < 11)
                {
                    ksmLogger("[ERROR] READ_CARD_BALANCE response too short", true);
                    return KSMReaderCmdRetCode::KSMReaderRecv_CmdNotFound;
                }

                const std::vector<char> cardBalanceBytes(recvCmd.begin() + 8, recvCmd.begin() + 11);
                const std::string cardBalanceStr = Common::getInstance()->FnGetVectorCharToHexString(cardBalanceBytes);

                long balance = std::atol(cardBalanceStr.c_str());
                if (balance < 0)
                {
                    balance = 65536 + balance;
                }

                updateCardBalance(balance);

                EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderReadCardBalance", true);
                EventManager::getInstance()->FnEnqueueEvent<bool>("Evt_handleKSMReaderCardInfo", true);

                std::stringstream ss;
                ss << "[RSP] READ_CARD_BALANCE_CMD | ACK" << " | Balance=" << balance;
                ksmLogger(ss.str());

                enqueueCommandOnIoThread(KSMReaderCmdID::IC_POWER_OFF_CMD);
                return KSMReaderCmdRetCode::KSMReaderRecv_ACK;
            }
        }
    }

    std::stringstream ss;
    ss << "[RSP] " << KSMReaderCmdIDToString(cmd)
       << " | Unknown response"
       << " | CmdCode="
       << std::hex
       << static_cast<int>(cmdCodeMSB)
       << static_cast<int>(cmdCodeLSB);
    ksmLogger(ss.str(), true);

    return KSMReaderCmdRetCode::KSMReaderRecv_CmdNotFound;
}

void KSM_Reader::handleCommandFailure(KSMReaderCmdID cmd, KSMReaderCmdRetCode retCode)
{
    std::stringstream ss;
    ss << "[FAILED] " << KSMReaderCmdIDToString(cmd)
       << " | Result=" << retCodeToString(retCode)
       << "(" << static_cast<int>(retCode) << ")";
    ksmLogger(ss.str(), true);

    const char* eventName = nullptr;

    switch (cmd)
    {
        case KSMReaderCmdID::INIT_CMD:
            eventName = "Evt_handleKSMReaderInit";
            break;
        case KSMReaderCmdID::CARD_PROHIBITED_CMD:
            eventName = "Evt_handleKSMReaderCardProhibited";
            break;
        case KSMReaderCmdID::CARD_ALLOWED_CMD:
            eventName = "Evt_handleKSMReaderCardAllowed";
            break;
        case KSMReaderCmdID::CARD_ON_IC_CMD:
            eventName = "Evt_handleKSMReaderCardOnIc";
            break;
        case KSMReaderCmdID::IC_POWER_ON_CMD:
            eventName = "Evt_handleKSMReaderIcPowerOn";
            break;
        case KSMReaderCmdID::WARM_RESET_CMD:
            eventName = "Evt_handleKSMReaderWarmReset";
            break;
        case KSMReaderCmdID::SELECT_FILE1_CMD:
            eventName = "Evt_handleKSMReaderSelectFile1";
            break;
        case KSMReaderCmdID::SELECT_FILE2_CMD:
            eventName = "Evt_handleKSMReaderSelectFile2";
            break;
        case KSMReaderCmdID::READ_CARD_INFO_CMD:
            eventName = "Evt_handleKSMReaderReadCardInfo";
            break;
        case KSMReaderCmdID::READ_CARD_BALANCE_CMD:
            eventName = "Evt_handleKSMReaderReadCardBalance";
            break;
        case KSMReaderCmdID::IC_POWER_OFF_CMD:
            eventName = "Evt_handleKSMReaderIcPowerOff";
            break;
        case KSMReaderCmdID::EJECT_TO_FRONT_CMD:
            eventName = "Evt_handleKSMReaderEjectToFront";
            break;
        case KSMReaderCmdID::GET_STATUS_CMD:
            eventName = "Evt_handleKSMReaderGetStatus";
            break;
    }

    if (eventName != nullptr)
    {
        EventManager::getInstance()->FnEnqueueEvent<bool>(eventName, false);
    }

    if (cmd == KSMReaderCmdID::GET_STATUS_CMD && continueReadCard_)
    {
        enqueueCommandOnIoThread(KSMReaderCmdID::GET_STATUS_CMD);
    }
}

void KSM_Reader::updateCardInfo(const std::string& cardNum, int expiryYearMonth, bool expired)
{
    std::lock_guard<std::mutex> lock(cardDataMutex_);
    cardData_.cardNum = cardNum;
    cardData_.expiryYearMonth = expiryYearMonth;
    cardData_.expired = expired;
}

void KSM_Reader::updateCardBalance(long balance)
{
    std::lock_guard<std::mutex> lock(cardDataMutex_);
    cardData_.balance = balance;
}

std::string KSM_Reader::FnKSMReaderGetCardNum() const
{
    std::lock_guard<std::mutex> lock(cardDataMutex_);
    return cardData_.cardNum;
}

bool KSM_Reader::FnKSMReaderGetCardExpired() const
{
    std::lock_guard<std::mutex> lock(cardDataMutex_);
    return cardData_.expired;
}

int KSM_Reader::FnKSMReaderGetCardExpiryDate() const
{
    std::lock_guard<std::mutex> lock(cardDataMutex_);
    return cardData_.expiryYearMonth;
}

long KSM_Reader::FnKSMReaderGetCardBalance() const
{
    std::lock_guard<std::mutex> lock(cardDataMutex_);
    return cardData_.balance;
}

std::string KSM_Reader::KSMReaderCmdIDToString(KSMReaderCmdID cmdID)
{
    switch (cmdID)
    {
        case KSMReaderCmdID::INIT_CMD:
            return "INIT_CMD";
        case KSMReaderCmdID::CARD_PROHIBITED_CMD:
            return "CARD_PROHIBITED_CMD";
        case KSMReaderCmdID::CARD_ALLOWED_CMD:
            return "CARD_ALLOWED_CMD";
        case KSMReaderCmdID::CARD_ON_IC_CMD:
            return "CARD_ON_IC_CMD";
        case KSMReaderCmdID::IC_POWER_ON_CMD:
            return "IC_POWER_ON_CMD";
        case KSMReaderCmdID::WARM_RESET_CMD:
            return "WARM_RESET_CMD";
        case KSMReaderCmdID::SELECT_FILE1_CMD:
            return "SELECT_FILE1_CMD";
        case KSMReaderCmdID::SELECT_FILE2_CMD:
            return "SELECT_FILE2_CMD";
        case KSMReaderCmdID::READ_CARD_INFO_CMD:
            return "READ_CARD_INFO_CMD";
        case KSMReaderCmdID::READ_CARD_BALANCE_CMD:
            return "READ_CARD_BALANCE_CMD";
        case KSMReaderCmdID::IC_POWER_OFF_CMD:
            return "IC_POWER_OFF_CMD";
        case KSMReaderCmdID::EJECT_TO_FRONT_CMD:
            return "EJECT_TO_FRONT_CMD";
        case KSMReaderCmdID::GET_STATUS_CMD:
            return "GET_STATUS_CMD";
    }

    return "UNKNOWN_CMD";
}

const char* KSM_Reader::retCodeToString(KSMReaderCmdRetCode retCode)
{
    switch (retCode)
    {
        case KSMReaderCmdRetCode::KSMReaderComm_Error:
            return "COMM_ERROR";
        case KSMReaderCmdRetCode::KSMReaderSend_Failed:
            return "SEND_FAILED";
        case KSMReaderCmdRetCode::KSMReaderRecv_CmdNotFound:
            return "CMD_NOT_FOUND";
        case KSMReaderCmdRetCode::KSMReaderRecv_NoResp:
            return "NO_RESPONSE";
        case KSMReaderCmdRetCode::KSMReaderRecv_NAK:
            return "NAK";
        case KSMReaderCmdRetCode::KSMReaderRecv_ACK:
            return "ACK";
        case KSMReaderCmdRetCode::KSMReaderRecv_CRCErr:
            return "CRC_ERROR";
    }

    return "UNKNOWN";
}


void KSM_Reader::ksmLogger(const std::string& logMsg, bool force)
{
    if (!force &&
        continueReadCard_ &&
        currentCmd_ == KSMReaderCmdID::GET_STATUS_CMD)
    {
        if (!blockGetStatusCmdLog_)
        {
            blockGetStatusCmdLog_ = true;
            Logger::getInstance()->FnLog(
                "### Running consecutive Get Status Command... [Time]: " +
                    Common::getInstance()->FnGetDateTime(),
                logFileName_,
                "KSM");
        }
        return;
    }

    if ((!continueReadCard_ ||
         currentCmd_ != KSMReaderCmdID::GET_STATUS_CMD) &&
        blockGetStatusCmdLog_)
    {
        blockGetStatusCmdLog_ = false;
    }

    Logger::getInstance()->FnLog(logMsg, logFileName_, "KSM");
}
