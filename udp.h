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

    void FnSetMonitorStatus(bool status);
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
