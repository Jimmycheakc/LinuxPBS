#include "tcp.h"
#include "log.h"

TcpClient::TcpClient(boost::asio::io_context& io_context, const std::string& ipAddress, unsigned short port)
    :   strand_(boost::asio::make_strand(io_context)),
        socket_(strand_),
        endpoint_(boost::asio::ip::address::from_string(ipAddress), port),
        buffer_(),
        status_(false)
{

}

void TcpClient::send(const std::string& message)
{
    // Make a copy of the message on the heap to ensure lifetime safety
    auto message_ptr = std::make_shared<std::string>(message);

    boost::asio::async_write(socket_, boost::asio::buffer(*message_ptr),
        boost::asio::bind_executor(strand_,
            [this, message_ptr](boost::system::error_code ec, std::size_t bytes_transferred)
            {
                if (ec)
                {
                    handleError(ec.message());
                }
                else
                {
                    // Optional: handle success or log bytes_transferred
                }
            }
        )
    );
}

bool TcpClient::isConnected() const
{
    return socket_.is_open();
}

void TcpClient::close()
{
    if (socket_.is_open())
    {
        // Cancel any ongoing async operations
        boost::system::error_code ec;
        socket_.cancel(ec); // This ensures that any pending operations are aborted.

        if (ec)
        {
            handleError(ec.message());
        }

        socket_.close(ec);  // Handle any error that occurs while canceling.

        if (!ec)
        {
            handleClose();
        }
        else
        {
            handleError(ec.message());
        }
    }

    // Reset status
    status_.store(false);
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

    socket_.async_connect(endpoint_, boost::asio::bind_executor(strand_, [this](const boost::system::error_code& ec) {
        if (ec == boost::asio::error::operation_aborted)
        {
            status_.store(false);
            handleError("Connection aborted during async_connect.");
        }
        else if (!ec)
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
    }));
}

bool TcpClient::isStatusGood()
{
    return status_.load();
}

void TcpClient::startAsyncReceive()
{
    socket_.async_read_some(boost::asio::buffer(buffer_),
        boost::asio::bind_executor(strand_, [this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
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
        }));
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

std::string TcpClient::getIPAddress() const
{
    // Check if address is valid
    if (endpoint_.address() == boost::asio::ip::address{})
    {
        // Return empty string or error message if address is not valid
        return "";
    }

    return endpoint_.address().to_string();
}

unsigned short TcpClient::getPort() const
{
    // Check if the port is valid
    if (endpoint_.port() == 0)
    {
        // Return 0 or handle the case where the port is invalid
        return 0;
    }

    return endpoint_.port();
}
