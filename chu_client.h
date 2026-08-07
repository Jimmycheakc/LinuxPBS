#pragma once

#include <boost/asio/thread_pool.hpp>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <queue>
#include <vector>
#include <unordered_map>
#include "tcp_client.h"
#include <boost/json.hpp>
#include <atomic>

class CHUClient
{
public:

    static CHUClient* getInstance();
    void FnCHUClientInit(const std::string& serverIP, unsigned short serverPort);
    void FnSendMsgToCHU(const std::string& sMsg);
    void FnCHUClose();
    std::atomic<bool> shutting_down{false};
   
private:
        static CHUClient* CHUClient_;
        static std::mutex mutex_;
        boost::asio::io_context ioContext_;
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard_;
        boost::asio::strand<boost::asio::io_context::executor_type> strand_;
        std::thread ioContextThread_;
        std::unique_ptr<AppTcpClient> client_;
        boost::asio::steady_timer ReConnectTimer_;
        std::string serverIP_;
        unsigned short serverPort_;
        std::string gbCHUstatus;

        CHUClient();
        void startReConnectTimer();
        void handleReConnectTimerTimeout(const boost::system::error_code& error);
        void startIoContextThread();
        void handleConnect(bool success, const std::string& message);
        void handleSend(bool success, const std::string& message);
        void handleClose(bool success, const std::string& message);
        void handleReceivedData(bool success, const std::vector<uint8_t>& data);
};