#include "tcp.h"

TcpClient::TcpClient(boost::asio::io_context& io_context, const std::string& ipAddress, unsigned short port)
    :   socket_(io_context),
        endpoint_(boost::asio::ip::address::from_string(ipAddress), port)
{
    boost::asio::connect(socket_, &endpoint_);
}

void TcpClient::send(const std::string& message)
{
    boost::asio::write(socket_, boost::asio::buffer(message));
}

void TcpClient::asyncReceive(void(*handler)(const boost::system::error_code& ec, std::size_t bytes_transferred))
{
    boost::asio::async_read(socket_, buffer_,
        [this, handler](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            handler(ec, bytes_transferred);
    });
}