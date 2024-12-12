#pragma once

#include <array>
#include <atomic>
#include <iostream>
#include <functional>
#include <boost/asio.hpp>

class TcpClient
{
public:
    TcpClient(boost::asio::io_context& io_context, const std::string& ipAddress, unsigned short port);

    void send(const std::string& message);
    void connect();
    void close();
    bool isConnected() const;
    void setConnectHandler(std::function<void()> handler);
    void setCloseHandler(std::function<void()> handler);
    void setReceiveHandler(std::function<void(const char* data, std::size_t length)> handler);
    void setErrorHandler(std::function<void(std::string error_message)> handler);
    bool isStatusGood();
    std::string getIPAddress() const;
    unsigned short getPort() const;

private:
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::ip::tcp::endpoint endpoint_;
    std::array<char, 1024> buffer_;
    std::atomic<bool> status_;
    std::function<void()> connectHandler_;
    std::function<void()> closeHandler_;
    std::function<void(const char* data, std::size_t length)> receiveHandler_;
    std::function<void(std::string error_message)> errorHandler_;
    void startAsyncReceive();
    void handleConnect();
    void handleClose();
    void handleReceivedData(const char* data, std::size_t length);
    void handleError(std::string error_message);
};