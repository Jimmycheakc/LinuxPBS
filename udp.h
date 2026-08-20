#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <string>
#include <string_view>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

enum UdpRxCommand : unsigned int
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
    CmdSetDioOutput         = 303,
    CmdEEPStatus            = 800
};

enum MonitorUdpRxCommand : unsigned int
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
};


// Passive asynchronous UDP transport/dispatcher.
//
// Concurrency is owned by the supplied io_context. This class does not create
// an io_context, thread, or work guard. It owns a strand because the
// supplied io_context may be run by multiple threads.
//
// Lifecycle requirement:
//   1. start()
//   2. use send()/receive processing
//   3. close()
//   4. allow the owning io_context to drain
//   5. destroy udpclient
class udpclient 
{
public:
    udpclient(
        boost::asio::io_context& ioContext,
        const std::string& serverAddress,
        unsigned short serverPort,
        unsigned short localPort,
        bool broadcast = false);

    ~udpclient();

    udpclient(const udpclient&) = delete;
    udpclient& operator=(const udpclient&) = delete;
    udpclient(udpclient&&) = delete;
    udpclient& operator=(udpclient&&) = delete;

    void start();
    void close();

    void send(std::string message);

    bool FnGetMonitorStatus() const;

 private:
    using Strand = boost::asio::strand<boost::asio::io_context::executor_type>;

    static constexpr std::size_t kMaxDatagramSize = 1024;

    void startOnStrand();
    void closeOnStrand();

    void startReceiveOnStrand();
    void handleReceiveOnStrand(const boost::system::error_code& error, std::size_t bytesReceived);

    void enqueueSendOnStrand(std::string message);
    void startSendOnStrand();
    void handleSendOnStrand(const boost::system::error_code& error, std::size_t bytesSent);

    void processData(std::string_view packet);
    void processMonitorData(std::string_view packet);

    void logTransportError(const std::string& message) const;

    Strand strand_;
    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint serverEndpoint_;
    boost::asio::ip::udp::endpoint senderEndpoint_;

    const unsigned short localPort_;
    const bool isBroadcast_;

    std::array<char, kMaxDatagramSize> receiveBuffer_{};
    std::deque<std::string> sendQueue_;

    bool started_{false};
    bool stopping_{false};
    bool sendInProgress_{false};

    std::atomic<bool> acceptingWork_{false};
    std::atomic<bool> monitorStatus_{false};
};


// Passive periodic UDP heartbeat sender.
// Uses the owner's io_context and owns its own strand so socket/timer state
// remains serialized even when multiple threads call io_context::run().
// It owns no io_context, thread, or work guard.
class HeartbeatUdpServer
{
public:
    HeartbeatUdpServer(
        boost::asio::io_context& ioContext,
        const std::string& serverAddress,
        unsigned short serverPort);

    ~HeartbeatUdpServer();

    HeartbeatUdpServer(const HeartbeatUdpServer&) = delete;
    HeartbeatUdpServer& operator=(const HeartbeatUdpServer&) = delete;
    HeartbeatUdpServer(HeartbeatUdpServer&&) = delete;
    HeartbeatUdpServer& operator=(HeartbeatUdpServer&&) = delete;

    void start();
    void stop();

private:
    using Strand = boost::asio::strand<boost::asio::io_context::executor_type>;

    static constexpr auto kHeartbeatInterval = std::chrono::minutes{1};
    static constexpr char kHeartbeatMessage[] = "Heartbeat";

    void startOnStrand();
    void stopOnStrand();

    void sendHeartbeatOnStrand();
    void scheduleNextHeartbeatOnStrand();

    void logError(const std::string& message) const;

    Strand strand_;
    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint remoteEndpoint_;
    boost::asio::steady_timer timer_;

    bool running_{false};
    bool stopping_{false};
};