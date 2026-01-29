#include <algorithm>
#include <boost/asio/thread_pool.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <memory>
#include <sstream>
#include "common.h"
#include "chu_client.h"
#include "log.h"
#include <unordered_set>
#include "event_manager.h"
#include "operation.h"


CHUClient* CHUClient::CHUClient_ = nullptr;
std::mutex CHUClient::mutex_;


CHUClient::CHUClient()
    : ioContext_(),
    strand_(boost::asio::make_strand(ioContext_)),
    workGuard_(boost::asio::make_work_guard(ioContext_)),
    serverIP_(""),
    serverPort_(0),
    connectTimer_(ioContext_)
{
}

CHUClient* CHUClient::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (CHUClient_ == nullptr)
    {
        CHUClient_ = new CHUClient();
    }
    return CHUClient_;
}

void CHUClient::FnCHUClientInit(const std::string& serverIP, unsigned short serverPort)
{
    
    client_ = std::make_unique<AppTcpClient>(ioContext_, serverIP, serverPort);
    serverIP_ = serverIP;
    serverPort_ = serverPort;
    //-------
    if (client_ )
    {
        client_->setConnectHandler([this](bool success, const std::string& message) {
            boost::asio::post(ioContext_, [this, success, message]() {
                handleConnect(success, message);
                });
        });
        client_->setCloseHandler([this](bool success, const std::string& message) { 
            boost::asio::post(ioContext_, [this, success, message]() {
                handleClose(success, message);
            });
        });
        client_->setReceiveHandler([this](bool success, const std::vector<uint8_t>& data) { 
            boost::asio::post(ioContext_, [this, success, data]() {
                handleReceivedData(success, data); 
            });
        });
        client_->setSendHandler([this](bool success, const std::string& message) { 
            boost::asio::post(ioContext_, [this, success, message]() {
                handleSend(success, message);
            });
        });

        startIoContextThread();
        client_->connect();
        startReConnectTimer();
    }
    else
    {
    
    }
}

void CHUClient::startIoContextThread()
{

    if (!ioContextThread_.joinable())
    {
        ioContextThread_ = std::thread([this]() { ioContext_.run(); });
    }
    
}

void CHUClient::startReConnectTimer()
{
    connectTimer_.expires_after(std::chrono::seconds(5));
    connectTimer_.async_wait(boost::asio::bind_executor(strand_,
        std::bind(&CHUClient::handleConnectTimerTimeout, this, std::placeholders::_1)));
}

void CHUClient::handleConnectTimerTimeout(const boost::system::error_code& error)
{
    if (error)
    {
        
    }

    if (!client_->isConnected())
    {
        client_->connect();
    }

    startReConnectTimer();
    
}

void CHUClient::handleConnect(bool success, const std::string& message)
{
    if (success)
    {
       operation::getInstance()->writelog("Successfully connected to CHUGateWay at IP: " + serverIP_ + ", Port: " + std::to_string(serverPort_), "CHU");
    }
    else
    {
       operation::getInstance()->writelog("Failed to connect to CHUGateWay at IP: " + serverIP_ + ", Port: " + std::to_string(serverPort_), "CHU");
    }
}

void CHUClient::handleSend(bool success, const std::string& message)
{
    if (success)
    {
       
    }
    else
    {
        operation::getInstance()->writelog("Failed to send to CHUGateWay", "CHU");
        
    }
}

void CHUClient::handleClose(bool success, const std::string& message)
{
    if (success)
    {
       operation::getInstance()->writelog("Successfully closed CHUGateWay at IP: " + serverIP_ + ", Port: " + std::to_string(serverPort_), "CHU");
    }
    else
    {
       operation::getInstance()->writelog("Failed to close CHUGateWay at IP: " + serverIP_ + ", Port: " + std::to_string(serverPort_), "CHU");
    }
}

void CHUClient::handleReceivedData(bool success, const std::vector<uint8_t>& data)
{
   
}

void CHUClient::FnSendMsgToCHU(const std::string& sMsg)
{
    if (client_->isConnected())
    {
        try
        {
            std::vector<uint8_t> data(sMsg.begin(), sMsg.end());
            client_->send(data);
        }
        catch (const boost::system::system_error& e) // Catch Boost.Asio system errors
        {
            std::string error = e.what();
            operation::getInstance()->writelog("Error for Send Msg to CHU: " + error, "CHU"); 
        }
        catch (const std::exception& e)
        {
            std::string error = e.what();
            operation::getInstance()->writelog("Error for Send Msg to CHU: " + error, "CHU"); 
        }
        catch (...)
        {
            operation::getInstance()->writelog ("Unknown Error, send data to CHU failed.", "CHU");
        }
    }
    else
    {
        operation::getInstance()->writelog ("CHU connection problem, send data failed.", "CHU");
    }

}

void CHUClient::FnCHUClose()
{
   
    if (client_)
    {
        client_->close();
        client_.reset();
    }

    workGuard_.reset();
    ioContext_.stop();
    if (ioContextThread_.joinable())
    {
        ioContextThread_.join();
    }
}


