#pragma once

#include <iostream>
#include <boost/asio.hpp>

class TcpClient
{
public:
    TcpClient(boost::asio::io_context& io_context, const std::string& ipAddress, unsigned short port);

    void send(const std::string& message);
    void asyncReceive(void(*handler)(const boost::system::error_code& ec, std::size_t bytes_transferred));

private:
    boost::asio::ip::tcp::socket socket_;
    boost::asio::ip::tcp::endpoint endpoint_;
    boost::asio::streambuf buffer_;
};