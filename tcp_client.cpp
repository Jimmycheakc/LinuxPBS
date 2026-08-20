#include "tcp_client.h"

#include <utility>

AppTcpClient::AppTcpClient(boost::asio::io_context& ioContext, const std::string& ipAddress, unsigned short port)
    : ioContext_(ioContext),
      socket_(ioContext_),
      endpoint_(boost::asio::ip::make_address(ipAddress), port)
{
}


void AppTcpClient::connect()
{
    boost::asio::post(
        ioContext_,
        [this]()
        {
            connectOnIoThread();
        });
}

void AppTcpClient::send(const std::vector<std::uint8_t>& message)
{
    // Copy before posting so the caller's buffer may be destroyed immediately
    // after send() returns.
    boost::asio::post(
        ioContext_,
        [this, message]() mutable
        {
            enqueueSendOnIoThread(std::move(message));
        });
}

void AppTcpClient::close()
{
    boost::asio::post(
        ioContext_,
        [this]()
        {
            closeOnIoThread(true);
        });
}

bool AppTcpClient::isConnected() const
{
    return connectedSnapshot_.load();
}

void AppTcpClient::setConnectHandler(ConnectHandler handler)
{
    connectHandler_ = std::move(handler);
}

void AppTcpClient::setSendHandler(SendHandler handler)
{
    sendHandler_ = std::move(handler);
}

void AppTcpClient::setCloseHandler(CloseHandler handler)
{
    closeHandler_ = std::move(handler);
}

void AppTcpClient::setReceiveHandler(ReceiveHandler handler)
{
    receiveHandler_ = std::move(handler);
}

void AppTcpClient::connectOnIoThread()
{
    if (connectedSnapshot_.load() || connecting_)
    {
        return;
    }

    // A new generation invalidates callbacks left over from an older socket
    // session. This is important when close() and reconnect happen quickly.
    const std::uint64_t generation = ++connectionGeneration_;

    closing_ = false;
    connecting_ = true;
    writeInProgress_ = false;
    writeQueue_.clear();
    connectedSnapshot_.store(false);

    // Always start a reconnect attempt with a clean socket.
    closeSocketNoThrow();

    socket_.async_connect(
        endpoint_,
        [this, generation](const boost::system::error_code& error)
        {
            // Ignore stale completion from an older socket generation.
            if (generation != connectionGeneration_)
            {
                return;
            }

            connecting_ = false;

            if (error)
            {
                connectedSnapshot_.store(false);
                closeSocketNoThrow();

                // operation_aborted is expected when an explicit close()
                // cancels a pending connect. It is not a communication fault.
                if (error == boost::asio::error::operation_aborted && closing_)
                {
                    return;
                }

                if (connectHandler_)
                {
                    connectHandler_(false, error.message());
                }

                return;
            }

            connectedSnapshot_.store(true);

            if (connectHandler_)
            {
                connectHandler_(true, "");
            }

            startAsyncReceiveOnIoThread(generation);
        });
}

void AppTcpClient::enqueueSendOnIoThread(std::vector<std::uint8_t> message)
{
    if (!connectedSnapshot_.load() ||
        closing_ ||
        !socket_.is_open())
    {
        if (sendHandler_)
        {
            sendHandler_(false, "Not connected");
        }

        return;
    }

    writeQueue_.push_back(std::move(message));

    if (!writeInProgress_)
    {
        startNextWriteOnIoThread(connectionGeneration_);
    }
}

void AppTcpClient::startNextWriteOnIoThread(std::uint64_t generation)
{
    if (generation != connectionGeneration_ ||
        closing_ ||
        writeInProgress_ ||
        writeQueue_.empty() ||
        !connectedSnapshot_.load() ||
        !socket_.is_open())
    {
        return;
    }

    writeInProgress_ = true;

    // The front vector remains in writeQueue_ until async_write completes, so
    // the buffer passed to Asio remains valid for the full async operation.
    boost::asio::async_write(
        socket_,
        boost::asio::buffer(writeQueue_.front()),
        [this, generation](
            const boost::system::error_code& error,
            std::size_t /*bytesTransferred*/)
        {
            if (generation != connectionGeneration_)
            {
                return;
            }

            writeInProgress_ = false;

            if (error)
            {
                if (error == boost::asio::error::operation_aborted && closing_)
                {
                    return;
                }

                if (sendHandler_)
                {
                    sendHandler_(false, error.message());
                }

                handleTransportFailureOnIoThread(error, false);
                return;
            }

            if (!writeQueue_.empty())
            {
                writeQueue_.pop_front();
            }

            if (sendHandler_)
            {
                sendHandler_(true, "");
            }

            startNextWriteOnIoThread(generation);
        });
}

void AppTcpClient::startAsyncReceiveOnIoThread(std::uint64_t generation)
{
    if (generation != connectionGeneration_ ||
        closing_ ||
        !connectedSnapshot_.load() ||
        !socket_.is_open())
    {
        return;
    }

    socket_.async_read_some(
        boost::asio::buffer(receiveBuffer_),
        [this, generation](
            const boost::system::error_code& error,
            std::size_t bytesTransferred)
        {
            if (generation != connectionGeneration_)
            {
                return;
            }

            if (error)
            {
                // Expected result of cancel()/close(). Do not report
                // "Read error: Operation canceled (125)" as a real fault.
                if (error == boost::asio::error::operation_aborted)
                {
                    return;
                }

                handleTransportFailureOnIoThread(error, true);
                return;
            }

            if (bytesTransferred > 0 && receiveHandler_)
            {
                const std::vector<std::uint8_t> data(receiveBuffer_.begin(), receiveBuffer_.begin() + static_cast<std::ptrdiff_t>(bytesTransferred));

                receiveHandler_(true, data);
            }

            // The receive callback may have indirectly requested close().
            // Because public close() is posted, this continuation is still safe;
            // generation/state checks prevent another read after close executes.
            startAsyncReceiveOnIoThread(generation);
        });
}

void AppTcpClient::closeOnIoThread(bool notifyHandler)
{
    const bool hadActivity =
        socket_.is_open() ||
        connecting_ ||
        connectedSnapshot_.load();

    // Invalidate all callbacks from the current socket session before canceling
    // the socket. Their operation_aborted completions will then be ignored.
    ++connectionGeneration_;

    closing_ = true;
    connecting_ = false;
    connectedSnapshot_.store(false);

    writeQueue_.clear();
    writeInProgress_ = false;

    boost::system::error_code cancelError;
    boost::system::error_code shutdownError;
    boost::system::error_code closeError;

    if (socket_.is_open())
    {
        socket_.cancel(cancelError);

        socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, shutdownError);

        socket_.close(closeError);
    }

    closing_ = false;

    if (notifyHandler && hadActivity && closeHandler_)
    {
        // shutdown() commonly reports not_connected during teardown, while the
        // actual close still succeeds. Treat closeError as the close result.
        closeHandler_(!closeError, closeError ? closeError.message() : "");
    }
}

void AppTcpClient::closeSocketNoThrow()
{
    boost::system::error_code ignored;

    if (socket_.is_open())
    {
        socket_.cancel(ignored);
        socket_.close(ignored);
    }
}

void AppTcpClient::handleTransportFailureOnIoThread(const boost::system::error_code& error, bool notifyReceiveHandler)
{
    // Only the first failure for a connection generation should own teardown.
    ++connectionGeneration_;

    connecting_ = false;
    closing_ = false;
    connectedSnapshot_.store(false);

    writeQueue_.clear();
    writeInProgress_ = false;

    closeSocketNoThrow();

    if (notifyReceiveHandler && receiveHandler_)
    {
        receiveHandler_(false, {});
    }

    if (closeHandler_)
    {
        closeHandler_(false, error.message());
    }
}
