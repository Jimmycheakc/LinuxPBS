#include "tcp_client.h"
#include "log.h"

AppTcpClient::AppTcpClient(boost::asio::io_context& io_context, const std::string& ipAddress, unsigned short port)
    :   strand_(boost::asio::make_strand(io_context)),
        socket_(strand_),
        endpoint_(boost::asio::ip::address::from_string(ipAddress), port),
        buffer_(2048),
        isConnected_(false)
{

}

void AppTcpClient::send(const std::vector<uint8_t>& message)
{
    if (!isConnected_.load())
    {
        if (sendHandler_)
        {
            sendHandler_(false, "Not connected");
        }
        return;
    }

    // Make a copy of the message on the heap to ensure lifetime safety
    auto message_ptr = std::make_shared<std::vector<uint8_t>>(message);

    boost::asio::async_write(socket_, boost::asio::buffer(message_ptr->data(), message_ptr->size()),
        boost::asio::bind_executor(strand_,
            [this, message_ptr](boost::system::error_code ec, std::size_t bytes_transferred)
            {
                if (sendHandler_)
                {
                    sendHandler_(!ec, ec ? ec.message() : "");
                }
            }
        )
    );
}

bool AppTcpClient::isConnected() const
{
    return isConnected_.load();
}

void AppTcpClient::close()
{
    if (!socket_.is_open())
    {
        return;
    }

    // Cancel any ongoing async operations
    boost::system::error_code ec;
    socket_.cancel(ec); // This ensures that any pending operations are aborted.
    socket_.close(ec);  // Handle any error that occurs while canceling.

    isConnected_.store(false);

    if (closeHandler_)
    {
        closeHandler_(!ec, ec ? ec.message() : "");
    }
}

void AppTcpClient::setConnectHandler(std::function<void(bool success, const std::string& message)> handler)
{
    connectHandler_ = std::move(handler);
}

void AppTcpClient::setSendHandler(std::function<void(bool success, const std::string& message)> handler)
{
    sendHandler_ = std::move(handler);
}

void AppTcpClient::setCloseHandler(std::function<void(bool success, const std::string& message)> handler)
{
    closeHandler_ = std::move(handler);
}

void AppTcpClient::setReceiveHandler(std::function<void(bool success, const std::vector<uint8_t>& data)> handler)
{
    receiveHandler_ = std::move(handler);
}

void AppTcpClient::connect()
{
    socket_.async_connect(endpoint_, boost::asio::bind_executor(strand_, [this](const boost::system::error_code& ec) {
        if (ec == boost::asio::error::operation_aborted)
        {
            if (connectHandler_)
            {
                connectHandler_(false, ec.message());
            }
            return;
        }
        
        if (!ec)
        {
            isConnected_.store(true);
            if (connectHandler_)
            {
                connectHandler_(true, "");
            }
            startAsyncReceive();
        }
        else
        {
            isConnected_.store(false);
            close();
            if (connectHandler_)
            {
                connectHandler_(false, ec.message());
            }
        }
    }));
}

void AppTcpClient::startAsyncReceive()
{
    socket_.async_read_some(boost::asio::buffer(buffer_.data(), buffer_.size()),
        boost::asio::bind_executor(strand_, [this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (!ec)
            {
                if (receiveHandler_)
                {
                    receiveHandler_(true, std::vector<uint8_t>(buffer_.begin(), buffer_.begin() + bytes_transferred));
                }
                startAsyncReceive();
            }
            else
            {
                std::cerr << "Read error: " << ec.message() << " (" << ec.value() << ")\n";
                isConnected_.store(false);
                if (receiveHandler_)
                {
                    receiveHandler_(false, {});
                }
                close();
            }
        }));
}
