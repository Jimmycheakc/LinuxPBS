#include "chu_client.h"

#include <chrono>
#include <future>
#include <utility>

#if defined(__linux__)
#include <pthread.h>
#endif

#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>

#include "event_manager.h"
#include "log.h"


CHUClient::CHUClient()
    : reconnectTimer_(ioContext_)
{
}

CHUClient::~CHUClient()
{
    // Normal shutdown should be performed through FnCHUClose().
    // This is only an emergency fallback for process/static destruction.
    acceptingWork_.store(false);
    stopping_.store(true);
    shutting_down.store(true);

    workGuard_.reset();
    ioContext_.stop();

    if (ioContextThread_.joinable() &&
        ioContextThread_.get_id() != std::this_thread::get_id())
    {
        ioContextThread_.join();
    }

    client_.reset();
}

CHUClient* CHUClient::getInstance()
{
    static CHUClient instance;
    return &instance;
}

void CHUClient::FnCHUClientInit(const std::string& serverIP, unsigned short serverPort)
{
    
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);

    Logger::getInstance()->FnCreateLogFile(logFileName_);

    if (moduleRunning_.load() || ioContextThread_.joinable())
    {
        Logger::getInstance()->FnLog("CHU: [INIT] Ignored | Reason=Already running", logFileName_, "CHU");
        return;
    }

    resetRuntimeState();

    serverIP_ = serverIP;
    serverPort_ = serverPort;

    ioContext_.restart();
    workGuard_.emplace(ioContext_.get_executor());

    try
    {
        client_ = std::make_unique<AppTcpClient>(ioContext_, serverIP_, serverPort_);
    }
    catch (const std::exception& e)
    {
        workGuard_.reset();

        Logger::getInstance()->FnLog(
            std::string("CHU: [INIT] Failed | Reason=TCP client creation | Error=") +
                e.what(),
            logFileName_,
            "CHU");
        return;
    }

    // AppTcpClient is passive and bound to this same io_context.
    // Because CHU owns exactly one run() thread, these callbacks already execute
    // on the CHU I/O thread. Posting them again is unnecessary.
    client_->setConnectHandler(
        [this](bool success, const std::string& message)
        {
            handleConnect(success, message);
        });

    client_->setCloseHandler(
        [this](bool success, const std::string& message)
        {
            handleClose(success, message);
        });

    client_->setReceiveHandler(
        [this](bool success, const std::vector<std::uint8_t>& data)
        {
            handleReceivedData(success, data);
        });

    client_->setSendHandler(
        [this](bool success, const std::string& message)
        {
            handleSend(success, message);
        });

    stopping_.store(false);
    shutting_down.store(false);
    acceptingWork_.store(true);

    if (!startIoContextThread())
    {
        acceptingWork_.store(false);
        stopping_.store(true);
        shutting_down.store(true);

        workGuard_.reset();
        client_.reset();

        Logger::getInstance()->FnLog("CHU: [INIT] Failed | Reason=Unable to start io_context thread", logFileName_, "CHU");
        return;
    }

    Logger::getInstance()->FnLog(
        "CHU: [INIT] Started | Server=" + serverIP_ +
            " | Port=" + std::to_string(serverPort_),
        logFileName_,
        "CHU");

    boost::asio::post(
        ioContext_,
        [this]()
        {
            if (stopping_.load() || shutting_down.load())
            {
                return;
            }

            gbCHUstatus_ = "connecting";
            requestConnectOnIoThread(false);
        });
}

bool CHUClient::startIoContextThread()
{
    if (ioContextThread_.joinable())
    {
        return true;
    }

    try
    {
        ioContextThread_ =
            std::thread(
                [this]()
                {
#if defined(__linux__)
                    ::pthread_setname_np(::pthread_self(), "CHU_IO");
#endif
                    ioContext_.run();
                });

        moduleRunning_.store(true);
        return true;
    }
    catch (const std::exception& e)
    {
        Logger::getInstance()->FnLog(
            std::string("CHU: [THREAD] Start failed | Error=") +
                e.what(),
            logFileName_,
            "CHU");

        moduleRunning_.store(false);
        return false;
    }
}

void CHUClient::FnSendMsgToCHU(const std::string& sMsg)
{
    if (!acceptingWork_.load() || shutting_down.load())
    {
        Logger::getInstance()->FnLog("CHU: [TX] Rejected | Reason=Module not accepting work", logFileName_, "CHU");
        return;
    }

    boost::asio::post(
        ioContext_,
        [this, message = sMsg]()
        {
            if (stopping_.load() || shutting_down.load())
            {
                return;
            }

            if (!client_)
            {
                Logger::getInstance()->FnLog("CHU: [TX] Rejected | Reason=TCP client unavailable", logFileName_, "CHU");
                return;
            }

            if (!client_->isConnected())
            {
                Logger::getInstance()->FnLog(
                    "CHU: [TX] Rejected | Reason=Not connected"
                    " | Bytes=" + std::to_string(message.size()),
                    logFileName_,
                    "CHU");

                setConnectionStatusOnIoThread("lost");
                scheduleReconnectOnIoThread();
                return;
            }

            Logger::getInstance()->FnLog(
                "CHU: [TX] Queued | Bytes=" +
                    std::to_string(message.size()),
                logFileName_,
                "CHU");

            client_->send(std::vector<std::uint8_t>(message.begin(), message.end()));
        });
}

void CHUClient::requestConnectOnIoThread(bool reconnectAttempt)
{
    if (stopping_.load() || shutting_down.load() || !client_)
    {
        return;
    }

    if (client_->isConnected())
    {
        cancelReconnectTimerOnIoThread();
        setConnectionStatusOnIoThread("connected");
        return;
    }

    reconnectScheduled_ = false;
    gbCHUstatus_ = "connecting";

    Logger::getInstance()->FnLog(
        std::string("CHU: [CONNECT] Attempt") +
            (reconnectAttempt ? " | Type=Reconnect" : " | Type=Initial") +
            " | Server=" + serverIP_ +
            " | Port=" + std::to_string(serverPort_),
        logFileName_,
        "CHU");

    client_->connect();
}

void CHUClient::scheduleReconnectOnIoThread()
{
    if (stopping_.load() ||
        shutting_down.load() ||
        !client_ ||
        client_->isConnected() ||
        reconnectScheduled_)
    {
        return;
    }

    reconnectScheduled_ = true;

    boost::system::error_code ec;
    reconnectTimer_.cancel(ec);
    reconnectTimer_.expires_after(kReconnectDelay);

    Logger::getInstance()->FnLog(
        "CHU: [RECONNECT] Scheduled | DelaySec=" +
            std::to_string(kReconnectDelay.count()),
        logFileName_,
        "CHU");

    reconnectTimer_.async_wait(
        [this](const boost::system::error_code& error)
        {
            handleReconnectTimerTimeout(error);
        });
}

void CHUClient::cancelReconnectTimerOnIoThread()
{
    reconnectScheduled_ = false;

    boost::system::error_code ec;
    reconnectTimer_.cancel(ec);
}

void CHUClient::handleReconnectTimerTimeout(const boost::system::error_code& error)
{
    if (error == boost::asio::error::operation_aborted ||
        stopping_.load() ||
        shutting_down.load())
    {
        return;
    }

    reconnectScheduled_ = false;

    if (error)
    {
        Logger::getInstance()->FnLog(
            "CHU: [RECONNECT] Timer error | Error=" +
                error.message(),
            logFileName_,
            "CHU");

        scheduleReconnectOnIoThread();
        return;
    }

    requestConnectOnIoThread(true);
}

void CHUClient::setConnectionStatusOnIoThread(const std::string& status)
{
    if (gbCHUstatus_ == status)
    {
        return;
    }

    gbCHUstatus_ = status;

    Logger::getInstance()->FnLog(
        "CHU: [STATE] Changed | State=" + status,
        logFileName_,
        "CHU");

    // Preserve the original external event contract: consumers receive
    // "connected" / "lost". "connecting" remains internal.
    if (status == "connected" || status == "lost")
    {
        EventManager::getInstance()->FnEnqueueEvent("Evt_handleCHUClientConnectionState", status);
    }
}

void CHUClient::handleConnect(bool success, const std::string& message)
{
    if (stopping_.load() || shutting_down.load())
    {
        return;
    }

    if (success)
    {
        cancelReconnectTimerOnIoThread();
        setConnectionStatusOnIoThread("connected");

        Logger::getInstance()->FnLog(
            "CHU: [CONNECT] Success | Server=" + serverIP_ +
                " | Port=" + std::to_string(serverPort_),
            logFileName_,
            "CHU");
        return;
    }

    setConnectionStatusOnIoThread("lost");

    Logger::getInstance()->FnLog(
        "CHU: [CONNECT] Failed | Server=" + serverIP_ +
            " | Port=" + std::to_string(serverPort_) +
            (message.empty() ? "" : " | Error=" + message),
        logFileName_,
        "CHU");

    scheduleReconnectOnIoThread();
}

void CHUClient::handleSend(bool success, const std::string& message)
{
    if (stopping_.load() || shutting_down.load())
    {
        return;
    }

    if (success)
    {
        Logger::getInstance()->FnLog("CHU: [TX] Completed", logFileName_, "CHU");
        return;
    }

    Logger::getInstance()->FnLog(
        "CHU: [TX] Failed" +
            (message.empty() ? "" : " | Error=" + message),
        logFileName_,
        "CHU");

    if (!client_ || !client_->isConnected())
    {
        setConnectionStatusOnIoThread("lost");
        scheduleReconnectOnIoThread();
    }
}

void CHUClient::handleClose(bool success, const std::string& message)
{
    if (stopping_.load() || shutting_down.load())
    {
        return;
    }

    setConnectionStatusOnIoThread("lost");

    Logger::getInstance()->FnLog(
        std::string("CHU: [CONNECTION] Closed") +
            " | Result=" + (success ? "Success" : "Failure") +
            (message.empty() ? "" : " | Error=" + message),
        logFileName_,
        "CHU");

    scheduleReconnectOnIoThread();
}

void CHUClient::handleReceivedData(bool success, const std::vector<std::uint8_t>& data)
{
    if (stopping_.load() || shutting_down.load())
    {
        return;
    }

    if (!success)
    {
        Logger::getInstance()->FnLog("CHU: [RX] Failed", logFileName_, "CHU");
        return;
    }

    Logger::getInstance()->FnLog(
        "CHU: [RX] Received | Bytes=" +
            std::to_string(data.size()),
        logFileName_,
        "CHU");

    const std::string receiveDataStr(reinterpret_cast<const char*>(data.data()), data.size());

    EventManager::getInstance()->FnEnqueueEvent("Evt_handleCHUReceived", receiveDataStr);
}

void CHUClient::FnCHUClose()
{
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);

    if (ioContext_.get_executor().running_in_this_thread())
    {
        Logger::getInstance()->FnLog(
            "CHU: [SHUTDOWN] Rejected | Reason=Called from CHU I/O thread",
            logFileName_,
            "CHU");
        return;
    }

    acceptingWork_.store(false);
    stopping_.store(true);
    shutting_down.store(true);

    if (!ioContextThread_.joinable())
    {
        workGuard_.reset();
        client_.reset();

        resetRuntimeState();
        moduleRunning_.store(false);
        stopping_.store(false);
        shutting_down.store(false);
        return;
    }

    Logger::getInstance()->FnLog("CHU: [SHUTDOWN] Begin", logFileName_, "CHU");

    auto shutdownPromise = std::make_shared<std::promise<void>>();
    auto shutdownFuture = shutdownPromise->get_future();

    boost::asio::post(
        ioContext_,
        [this, shutdownPromise]()
        {
            shutdownOnIoThread();
            shutdownPromise->set_value();
        });

    shutdownFuture.wait();

    // The close() request above is queued onto the same context. Release the
    // guard only after shutdown was initiated, then let pending cancellation /
    // close handlers drain before destroying AppTcpClient.
    workGuard_.reset();

    if (ioContextThread_.joinable())
    {
        ioContextThread_.join();
    }

    client_.reset();
    resetRuntimeState();

    moduleRunning_.store(false);
    stopping_.store(false);
    shutting_down.store(false);

    Logger::getInstance()->FnLog("CHU: [SHUTDOWN] Completed", logFileName_, "CHU");
}

void CHUClient::shutdownOnIoThread()
{
    cancelReconnectTimerOnIoThread();

    if (client_)
    {
        client_->close();
    }

    gbCHUstatus_.clear();
}

void CHUClient::resetRuntimeState()
{
    reconnectScheduled_ = false;
    gbCHUstatus_.clear();
    serverIP_.clear();
    serverPort_ = 0;
}
