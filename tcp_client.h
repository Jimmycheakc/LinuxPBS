#pragma once

#include <array>
#include <atomic>
#include <iostream>
#include <functional>
#include <boost/asio.hpp>
#include <vector>

class AppTcpClient
{
public:
    AppTcpClient(boost::asio::io_context& io_context, const std::string& ipAddress, unsigned short port);

    void send(const std::vector<uint8_t>& message);
    void connect();
    void close();
    bool isConnected() const;

    void setConnectHandler(std::function<void(bool success, const std::string& message)> handler);
    void setSendHandler(std::function<void(bool success, const std::string& message)> handler);
    void setCloseHandler(std::function<void(bool success, const std::string& message)> handler);
    void setReceiveHandler(std::function<void(bool success, const std::vector<uint8_t>& data)> handler);

private:
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::ip::tcp::endpoint endpoint_;
    std::vector<uint8_t> buffer_;
    std::atomic<bool> isConnected_;

    std::function<void(bool success, const std::string& message)> connectHandler_;
    std::function<void(bool success, const std::string& message)> sendHandler_;
    std::function<void(bool success, const std::string& message)> closeHandler_;
    std::function<void(bool success, const std::vector<uint8_t>& data)> receiveHandler_;
    void startAsyncReceive();
};