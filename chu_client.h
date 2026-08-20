#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include "tcp_client.h"

class CHUClient final
{
public:

    static CHUClient* getInstance();

    void FnCHUClientInit(const std::string& serverIP, unsigned short serverPort);
    void FnSendMsgToCHU(const std::string& sMsg);
    void FnCHUClose();

    // Backward-compatible cross-thread shutdown snapshot.
    std::atomic<bool> shutting_down{false};

    CHUClient(const CHUClient&) = delete;
    CHUClient& operator=(const CHUClient&) = delete;
    CHUClient(CHUClient&&) = delete;
    CHUClient& operator=(CHUClient&&) = delete;
   
private:

    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    static constexpr std::chrono::seconds kReconnectDelay{5};

    // Active module:
    // - one module-owned io_context
    // - exactly one dedicated run() thread
    // - no strand required
    boost::asio::io_context ioContext_;
    std::optional<WorkGuard> workGuard_;
    boost::asio::steady_timer reconnectTimer_;
    std::thread ioContextThread_;

    // Passive transport bound to the CHU io_context.
    std::unique_ptr<AppTcpClient> client_;

    mutable std::mutex lifecycleMutex_;

    std::atomic<bool> moduleRunning_{false};
    std::atomic<bool> acceptingWork_{false};
    std::atomic<bool> stopping_{false};

    // I/O-thread-owned state below.
    std::string serverIP_;
    unsigned short serverPort_{0};
    std::string gbCHUstatus_;
    bool reconnectScheduled_{false};

    const std::string logFileName_{"chu"};

    CHUClient();
    ~CHUClient();

    bool startIoContextThread();
    void shutdownOnIoThread();
    void resetRuntimeState();

    void requestConnectOnIoThread(bool reconnectAttempt);
    void scheduleReconnectOnIoThread();
    void cancelReconnectTimerOnIoThread();
    void handleReconnectTimerTimeout(const boost::system::error_code& error);

    void setConnectionStatusOnIoThread(const std::string& status);

    void handleConnect(bool success, const std::string& message);
    void handleSend(bool success, const std::string& message);
    void handleClose(bool success, const std::string& message);
    void handleReceivedData(bool success, const std::vector<std::uint8_t>& data);
};