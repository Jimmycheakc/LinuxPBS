#include <sstream>
#include <string>
#include "chu_client.h"
#include "log.h"
#include "operation.h"

CHU_CLIENT* CHU_CLIENT::chu_client_ = nullptr;
std::mutex CHU_CLIENT::mutex_;

CHU_CLIENT::CHU_CLIENT()
    : logFileName_("chu"),
    CHUIP_(""),
    CHUPort_(0)
{

}

CHU_CLIENT* CHU_CLIENT::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (chu_client_ == nullptr)
    {
        chu_client_ = new CHU_CLIENT();
    }
    return chu_client_;
}

void CHU_CLIENT::FnChuClientInit(boost::asio::io_context& mainIOContext, const std::string& chuIP, unsigned short chuPort)
{
    try
    {
        CHUIP_ = chuIP;
        CHUPort_ = chuPort;
        
        Logger::getInstance()->FnCreateLogFile(logFileName_);

        pStrand_ = std::make_unique<boost::asio::io_context::strand>(mainIOContext);
        pCHUClient_ = std::make_unique<TcpClient>(mainIOContext, CHUIP_, CHUPort_);
        pCHUReconnectTimer_ = std::make_unique<boost::asio::deadline_timer>(mainIOContext);

        Logger::getInstance()->FnLog("CHU Client: TCP initialization completed.");
        Logger::getInstance()->FnLog("CHU Client: TCP initialization completed.", logFileName_, "CHU");
    }
    catch (const boost::system::system_error& e) // Catch Boost.Asio system errors
    {
        std::stringstream ss;
        ss << "CHU Client: TCP init error. Boost.Asio Error Exception: " << e.what();
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "CHU");
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << "CHU Client: TCP init error. Error Exception: " << e.what();
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "CHU");
    }
    catch (...)
    {
        std::stringstream ss;
        ss << "CHU Client: TCP init error. Unknown Exception.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "CHU");
    }
}

void CHU_CLIENT::FnChuClientClose()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "CHU");

    if (pCHUClient_)
    {
        pCHUClient_->close();
        pCHUClient_.reset();
    }
}

void CHU_CLIENT::FnConnectCHU()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "CHU");

    if (pCHUClient_)
    {
        pCHUClient_->connect();
    }
}

void CHU_CLIENT::FnCloseCHU()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "CHU");

    if (pCHUClient_)
    {
        pCHUClient_->close();
    }
}

void CHU_CLIENT::FnSendDataToCHU(const std::string& data)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "CHU");

    if (pCHUClient_)
    {
        pCHUClient_->send(data);
    }
}

void CHU_CLIENT::FnSetCHUConnectHandler(std::function<void()> connectHandler)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "CHU");

    if (pCHUClient_)
    {
        pCHUClient_->setConnectHandler(connectHandler);
    }
}

void CHU_CLIENT::FnSetCHUCloseHandler(std::function<void()> closeHandler)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "CHU");

    if (pCHUClient_)
    {
        pCHUClient_->setCloseHandler(closeHandler);
    }
}

void CHU_CLIENT::FnSetCHUReceiveHandler(std::function<void(const char* data, std::size_t length)> receiveHandler)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "CHU");

    if (pCHUClient_)
    {
        pCHUClient_->setReceiveHandler(receiveHandler);
    }
}

void CHU_CLIENT::FnSetCHUErrorHandler(std::function<void(std::string error_message)> errorHandler)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "CHU");

    if (pCHUClient_)
    {
        pCHUClient_->setErrorHandler(errorHandler);
    }
}

void CHU_CLIENT::CheckConnectionAndCBHandler(int intervalMs, const CHU_CLIENT::CHUCmd& cmd, const std::string& data)
{
    if (pCHUClient_ && pCHUReconnectTimer_)
    {
        if (FnGetCHUStatus() == true)
        {
            if (onRetryAndConnectedCallBack_)
            {
                Logger::getInstance()->FnLog("Call onRetryAndConnectedCallBack_ function.", logFileName_, "CHU");
                onRetryAndConnectedCallBack_(cmd, data);
            }
            return;
        }

        if (boost::posix_time::microsec_clock::universal_time() > CHUReconnectTimeout_)
        {
            Logger::getInstance()->FnLog("Connection attempt timed out.", logFileName_, "CHU");
            return;
        }

        pCHUReconnectTimer_->expires_from_now(boost::posix_time::milliseconds(intervalMs));
        pCHUReconnectTimer_->async_wait(boost::asio::bind_executor(*pStrand_, [this, intervalMs, cmd, data] (const boost::system::error_code& ec) {
            if (ec != boost::asio::error::operation_aborted)
            {
                CheckConnectionAndCBHandler(intervalMs, cmd, data);
            }
        }));
    }
    else
    {
        Logger::getInstance()->FnLog("Unable to connect due to missing CHU client OR reconnect timer uninitialized.", logFileName_, "CHU");
    }
}

void CHU_CLIENT::FnRetryConnectionAndCBToSendData(int timeoutMs, int intervalMs, std::function<void(CHU_CLIENT::CHUCmd, const std::string&)> onRetryAndConnectedCallBack, const CHU_CLIENT::CHUCmd& cmd, const std::string& data)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "CHU");

    CHUReconnectTimeout_ = boost::posix_time::microsec_clock::universal_time() + boost::posix_time::milliseconds(timeoutMs);
    onRetryAndConnectedCallBack_ = onRetryAndConnectedCallBack;
    boost::asio::post(*pStrand_, [this, intervalMs, cmd, data]() {
        CheckConnectionAndCBHandler(intervalMs, cmd, data);
    });
}

std::string CHU_CLIENT::FnGetCHUIP() const
{
    if (pCHUClient_)
    {
        return pCHUClient_->getIPAddress();
    }
    return "";
}

unsigned short CHU_CLIENT::FnGetCHUPort() const
{
    if (pCHUClient_)
    {
        return pCHUClient_->getPort();
    }
    return 0;
}

bool CHU_CLIENT::FnGetCHUStatus() const
{
    if (pCHUClient_)
    {
        return (pCHUClient_->isConnected() && pCHUClient_->isStatusGood());
    }
    return false;
}
