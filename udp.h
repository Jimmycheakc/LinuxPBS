#pragma once

#include <iostream>
#include <cstdlib>
#include <ctime>
#include "boost/asio.hpp"
#include "log.h"

using namespace boost::asio;
using ip::udp;

typedef enum : unsigned int{
	CmdStopStationSoftware=11,
	CmdStatusEnquiry=13,
	CmdUpdateSeason=20,
	CmdDownloadMsg=22,
	CmdUpdateParam=23,
	CmdCarparkfull=24,
	CmdOpenBarrier=27,
	CmdStatusOnline=28,
	CmdDownloadTariff=32,
	CmdDownloadHoliday=33,
	CmdSetTime=35,
    CmdClearSeason=37,
	CmdDownloadtype=41,
    CmdCloseBarrier=42,
	CmdUpdateSetting=51,
	CmdSetLotCount=65,
	CmdLockupbarrier=67,
	CmdAvailableLots=68,
    CmdBroadcastSaveTrans=90,
	CmdFeeTest=120,
    CmdSetDioOutput=303
} udp_rx_command;

typedef enum : unsigned int{
	CmdMonitorEnquiry = 300,
    CmdMonitorFeeTest = 301,
	CmdMonitorOutput = 303,
	CmdDownloadIni = 309,
	CmdDownloadParam = 310,
    CmdMonitorSyncTime = 311,
    CmdMonitorStatus = 312
} monitorudp_rx_command;

class udpclient {
public:
    udp::socket socket_;
    udp::endpoint serverEndpoint_;
    udp::endpoint senderEndpoint_;
    enum { max_length = 1024 };
    char data_[max_length];
    void processdata(const char*data, std::size_t length);
    void processmonitordata(const char*data, std::size_t length);
    void udpinit(const std::string ServerIP, unsigned short RemotePort, unsigned short LocalPort);
    bool FnGetMonitorStatus();

public:
    udpclient(io_context& ioContext, const std::string& serverAddress, unsigned short serverPort,unsigned short LocalPort)
        : monitorStatus_(false),
        socket_(ioContext, udp::endpoint(udp::v4(), LocalPort)), serverEndpoint_(ip::address::from_string(serverAddress), serverPort) {
        startreceive();
    }
    void startsend(const std::string& message) {
        socket_.async_send_to(buffer(message), serverEndpoint_, [this](const boost::system::error_code& error, std::size_t /*bytes_sent*/) {
            if (!error) {
              //  std::cout << "Message sent successfully." << std::endl;
            } else {
                    std::stringstream dbss;
                    dbss << "Error for sending message: " << error.message() ;
                    Logger::getInstance()->FnLog(dbss.str(), "", "UDP");
            }
        });
    }
 private:
    bool monitorStatus_;
    void startreceive() {
        socket_.async_receive_from(buffer(data_, max_length), senderEndpoint_, [this](const boost::system::error_code& error, std::size_t bytes_received) {
            if (!error) {
                 if (socket_.local_endpoint().port() == 2001) {
                    processdata(data_, bytes_received);
                } else if (socket_.local_endpoint().port() == 2008) {
                    processmonitordata(data_, bytes_received);
                }
            } else {
                std::stringstream dbss;
                dbss << "Error receiving message: " << error.message() ;
                Logger::getInstance()->FnLog(dbss.str(), "", "UDP");
            }
            startreceive();  // Continue with the next receive operation
        });
    } 

};
