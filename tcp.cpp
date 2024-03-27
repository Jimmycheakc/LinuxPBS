#include "tcp.h"
#include "log.h"

TcpClient::TcpClient(boost::asio::io_context& io_context, const std::string& ipAddress, unsigned short port)
    :   socket_(io_context),
        endpoint_(boost::asio::ip::address::from_string(ipAddress), port),
        buffer_(),
        status_(false)
{
    connect();
}

void TcpClient::send(const std::string& message)
{
    boost::asio::write(socket_, boost::asio::buffer(message));
}

bool TcpClient::isConnected() const
{
    return socket_.is_open();
}

void TcpClient::close()
{
    if (socket_.is_open())
    {
        boost::system::error_code ec;
        socket_.close(ec);

        if (!ec)
        {
            handleClose();
        }
        else
        {
            handleError(ec.message());
        }
    }
}

void TcpClient::setConnectHandler(std::function<void()> handler)
{
    connectHandler_ = handler;
}

void TcpClient::setCloseHandler(std::function<void()> handler)
{
    closeHandler_ = handler;
}

void TcpClient::setReceiveHandler(std::function<void(const char* data, std::size_t length)> handler)
{
    receiveHandler_ = handler;
}

void TcpClient::setErrorHandler(std::function<void(std::string error_message)> handler)
{
    errorHandler_ = handler;
}

void TcpClient::connect()
{
    // Check if already connected or connecting
    if (socket_.is_open() || status_.load()) {
        return;
    }

    socket_.async_connect(endpoint_, [this](const boost::system::error_code& ec) {
        if (!ec)
        {
            status_.store(true);
            handleConnect();
            startAsyncReceive();
        }
        else
        {
            status_.store(false);
            close();
            handleError(ec.message());
        }
    });
}

bool TcpClient::isStatusGood()
{
    return status_.load();
}

void TcpClient::startAsyncReceive()
{
    socket_.async_read_some(boost::asio::buffer(buffer_),
        [this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (!ec)
            {
                handleReceivedData(buffer_.data(), bytes_transferred);
                startAsyncReceive();
            }
            else
            {
                status_.store(false);
                close();
                handleError(ec.message());
            }
        });
}

void TcpClient::handleConnect()
{
    if (connectHandler_)
    {
        connectHandler_();
    }
}

void TcpClient::handleClose()
{
    if (closeHandler_)
    {
        closeHandler_();
    }
}

void TcpClient::handleReceivedData(const char* data, std::size_t length)
{
    if (receiveHandler_)
    {
        receiveHandler_(data, length);
    }
}

void TcpClient::handleError(std::string error_message)
{
    if (errorHandler_)
    {
        errorHandler_(error_message);
    }
}
