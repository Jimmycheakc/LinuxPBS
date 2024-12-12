#pragma once

#include <boost/asio.hpp>
#include <iostream>
#include <mutex>
#include <string>
#include "tcp.h"

class CHU_CLIENT
{
public:
    enum class CHUCmd
    {
        ENTRY_INQ       = 100,
        RES_ENTRY_INQ   = 101,
        DEBIT           = 102,
        RES_DEBIT       = 103,
        RES_COMM        = 105
    };

    static CHU_CLIENT* getInstance();
    void FnChuClientInit(boost::asio::io_context& mainIOContext, const std::string& chuIP, unsigned short chuPort);
    void FnChuClientClose();
    void FnConnectCHU();
    void FnCloseCHU();
    void FnSendDataToCHU(const std::string& data);
    void FnSetCHUConnectHandler(std::function<void()> connectHandler);
    void FnSetCHUCloseHandler(std::function<void()> closeHandler);
    void FnSetCHUReceiveHandler(std::function<void(const char* data, std::size_t length)> receiveHandler);
    void FnSetCHUErrorHandler(std::function<void(std::string error_message)> errorHandler);
    void FnRetryConnectionAndCBToSendData(int timeoutMs, int intervalMs, std::function<void(CHU_CLIENT::CHUCmd, const std::string&)> onRetryAndConnectedCallBack_, const CHU_CLIENT::CHUCmd& cmd, const std::string& data);

    std::string FnGetCHUIP() const;
    unsigned short FnGetCHUPort() const;
    bool FnGetCHUStatus() const;

    /**
     * Singleton CHU_CLIENT should not be cloneable
     */
    CHU_CLIENT(CHU_CLIENT& chu) = delete;

    /**
     * Singleton CHU_CLIENT should not be assignable
     */
    void operator=(const CHU_CLIENT&) = delete;

private:
    static CHU_CLIENT* chu_client_;
    static std::mutex mutex_;
    std::unique_ptr<boost::asio::io_context::strand> pStrand_;
    std::string logFileName_;
    std::string CHUIP_;
    unsigned short CHUPort_;
    std::unique_ptr<TcpClient> pCHUClient_;
    std::unique_ptr<boost::asio::deadline_timer> pCHUReconnectTimer_;
    boost::posix_time::ptime CHUReconnectTimeout_;
    std::function<void(CHU_CLIENT::CHUCmd, const std::string&)> onRetryAndConnectedCallBack_;
    CHU_CLIENT();
    void CheckConnectionAndCBHandler(int intervalMs, const CHU_CLIENT::CHUCmd& cmd, const std::string& data);
};