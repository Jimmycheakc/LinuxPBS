
#include <iostream>
#include <cstdlib>
#include <ctime>
#include "boost/asio.hpp"

using namespace boost::asio;
using ip::udp;

typedef enum {
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
	CmdUpdateSetting=51,
	CmdSetLotCount=65,
	CmdLockupbarrier=67,
	CmdAvailableLots=68,
    CmdBroadcastSaveTrans=90,
	CmdFeeTest=120
} udp_rx_command;



void udpinit(const std::string ServerIP, unsigned short RemotePort, unsigned short LocalPort);

class udpclient {
public:
    udp::socket socket_;
    udp::endpoint serverEndpoint_;
    udp::endpoint senderEndpoint_;
    enum { max_length = 1024 };
    char data_[max_length];
    void processdata(const char*data, std::size_t length);

public:
    udpclient(io_service& ioService, const std::string& serverAddress, unsigned short serverPort,unsigned short LocalPort)
        : socket_(ioService, udp::endpoint(udp::v4(), LocalPort)), serverEndpoint_(ip::address::from_string(serverAddress), serverPort) {
        startreceive();
    }
    void startsend(const std::string& message) {
        socket_.async_send_to(buffer(message), serverEndpoint_, [this](const boost::system::error_code& error, std::size_t /*bytes_sent*/) {
            if (!error) {
                std::cout << "Message sent successfully." << std::endl;
            } else {
                std::cerr << "Error sending message: " << error.message() << std::endl;
            }
        });
    }
 private:
    void startreceive() {
        socket_.async_receive_from(buffer(data_, max_length), senderEndpoint_, [this](const boost::system::error_code& error, std::size_t bytes_received) {
            if (!error) {
                std::cout << "Received response from " << senderEndpoint_ << ": " << std::string(data_, bytes_received) << std::endl;
                processdata(data_, bytes_received);
            } else {
                std::cerr << "Error receiving message: " << error.message() << std::endl;
            }
            startreceive();  // Continue with the next receive operation
        });
    } 

};
