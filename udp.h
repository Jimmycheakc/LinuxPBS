#pragma once

#include <iostream>
#include <cstdlib>
#include <ctime>
#include "boost/asio.hpp"
#include <boost/algorithm/string.hpp>
#include "log.h"

using namespace boost::asio;
using ip::udp;

typedef enum : unsigned int
{
    CmdStopStationSoftware  = 11,
    CmdStatusEnquiry        = 13,
    CmdUpdateSeason         = 20,
    CmdDownloadMsg          = 22,
    CmdUpdateParam          = 23,
    CmdCarparkfull          = 24,
    CmdOpenBarrier          = 27,
    CmdStatusOnline         = 28,
    CmdContinueOpenBarrier  = 30,
    CmdDownloadTariff       = 32,
    CmdDownloadHoliday      = 33,
    CmdSetTime              = 35,
    CmdTimeForNoEntry       = 36,
    CmdClearSeason          = 37,
    CmdDownloadtype         = 41,
    CmdCloseBarrier         = 42,
    CmdDownloadXTariff      = 45,
    CmdDownloadTR           = 49,
    CmdUpdateSetting        = 51,
    CmdSetLotCount          = 65,
    CmdLockupbarrier        = 67,
    CmdAvailableLots        = 68,
    CmdBroadcastSaveTrans   = 90,
    CmdFeeTest              = 301,
    CmdSetDioOutput         = 303
} udp_rx_command;

typedef enum : unsigned int
{
    CmdMonitorEnquiry           = 300,
    CmdMonitorFeeTest           = 301,
    CmdMonitorOutput            = 303,
    CmdDownloadIni              = 309,
    CmdDownloadParam            = 310,
    CmdMonitorSyncTime          = 311,
    CmdMonitorStatus            = 312,
    CmdMonitorStationVersion    = 313,
    CmdMonitorGetStationCurrLog = 314
} monitorudp_rx_command;

class udpclient 
{
public:
    void processdata(const char*data, std::size_t length);
    void processmonitordata(const char*data, std::size_t length);
    void udpinit(const std::string ServerIP, unsigned short RemotePort, unsigned short LocalPort);
    bool FnGetMonitorStatus();

    udpclient(io_context& ioContext, const std::string& serverAddress, unsigned short serverPort, unsigned short LocalPort, bool broadcast = false)
        : strand_(boost::asio::make_strand(ioContext)),
          monitorStatus_(false),
          isBroadcast_(broadcast),
          socket_(strand_, udp::endpoint(udp::v4(), LocalPort)), serverEndpoint_(ip::address::from_string(serverAddress), serverPort)
    {
        if (isBroadcast_)
        {
            // Enable the socket to send broadcast messages
            boost::system::error_code ec;
            socket_.set_option(boost::asio::socket_base::broadcast(true), ec);
            if (ec)
            {
                std::stringstream ss;
                ss << "Error setting broadcast option: " << ec.message();
                Logger::getInstance()->FnLog(ss.str(), "", "UDP");
            }
        }

        startreceive();
    }

    void send(const std::string& message)
    {
        startsend(message);
    }

 private:
    bool monitorStatus_;
    bool isBroadcast_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    udp::socket socket_;
    udp::endpoint serverEndpoint_;
    udp::endpoint senderEndpoint_;
    enum { max_length = 1024 };
    char data_[max_length];
    void startreceive();
    void startsend(const std::string& message)
    {     
        socket_.async_send_to(buffer(message), serverEndpoint_, boost::asio::bind_executor(strand_, [this](const boost::system::error_code& error, std::size_t /*bytes_sent*/)
        {
            if (!error)
            {
              //  std::cout << "Message sent successfully." << std::endl;
            }
            else
            {
                std::stringstream dbss;
                dbss << "Error for sending message: " << error.message() ;
                Logger::getInstance()->FnLog(dbss.str(), "", "UDP");
            }
        }));
    }
};


class HeartbeatUdpServer
{
public:
    HeartbeatUdpServer(boost::asio::io_context& io_context, const std::string& serverAddress, int serverPort)
        : strand_(boost::asio::make_strand(io_context)),
          socket_(strand_, boost::asio::ip::udp::endpoint(boost::asio::ip::make_address(serverAddress), 0)),
          remoteEndpoint_(boost::asio::ip::make_address(serverAddress), serverPort),
          timer_(strand_)
    {

    }

    void start()
    {
        sendHeartbeat();
    }

private:
    void sendHeartbeat()
    {
        std::string message = "Heartbeat";
        socket_.async_send_to(boost::asio::buffer(message), remoteEndpoint_,
            boost::asio::bind_executor(strand_, [&](const boost::system::error_code& error, std::size_t /*bytes_transferred*/)
            {
                if (!error)
                {
                    //std::cout << "Heartbeat sent." << std::endl;
                }
                else
                {
                    std::stringstream ss;
                    ss << "Error sending heartbeat: " << error.message();
                    Logger::getInstance()->FnLog(ss.str(), "", "UDP");
                }
            }));
        
        // Schedule the next heartbeat
        timer_.expires_after(std::chrono::minutes(1));
        timer_.async_wait(boost::asio::bind_executor(strand_, [this](const boost::system::error_code& error)
        {
            if (!error)
            {
                sendHeartbeat();
            }
            else
            {
                handleTimerError(error);
            }
        }));
    }

    void handleTimerError(const boost::system::error_code& error)
    {
        std::stringstream ss;
        ss << "Heartbeat Timer error: " << error.message();
        Logger::getInstance()->FnLog(ss.str(), "", "UDP");

        try
        {
            sendHeartbeat();
        }
        catch (const std::exception& e)
        {
            std::stringstream ss;
            ss << __func__ << ", Heartbeat Timer Exception: " << e.what();
            Logger::getInstance()->FnLogExceptionError(ss.str());
        }
        catch (...)
        {
            std::stringstream ss;
            ss << __func__ << ", Heartbeat Timer Exception: Unknown Exception";
            Logger::getInstance()->FnLogExceptionError(ss.str());
        }
    }

    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint remoteEndpoint_;
    boost::asio::steady_timer timer_;
};