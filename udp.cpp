#include "udp.h"

#include "ini_parser.h"
#include "log.h"
#include "operation.h"

#include <charconv>
#include <cctype>
#include <optional>
#include <string_view>
#include <utility>

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>


namespace
{

std::string_view trim(std::string_view text) noexcept
{
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.front())) != 0)
    {
        text.remove_prefix(1);
    }

    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.back())) != 0)
    {
        text.remove_suffix(1);
    }

    return text;
}

std::optional<int> parseInt(std::string_view text) noexcept
{
    text = trim(text);

    if (text.empty())
    {
        return std::nullopt;
    }

    int value{};

    const auto [ptr, ec] =
        std::from_chars(text.data(), text.data() + text.size(), value);

    if (ec != std::errc{} || ptr != text.data() + text.size())
    {
        return std::nullopt;
    }

    return value;
}

} // namespace

udpclient::udpclient(
    boost::asio::io_context& ioContext,
    const std::string& serverAddress,
    unsigned short serverPort,
    unsigned short localPort,
    bool broadcast)
    : strand_(boost::asio::make_strand(ioContext)),
      socket_(strand_),
      serverEndpoint_(
          boost::asio::ip::make_address(serverAddress),
          serverPort),
      localPort_(localPort),
      isBroadcast_(broadcast)
{
}

udpclient::~udpclient()
{
    // The owner should call close(), drain the io_context, and only then
    // destroy this object. This direct close is an emergency fallback.
    boost::system::error_code ignored;
    socket_.cancel(ignored);
    socket_.close(ignored);
}

void udpclient::start()
{
    acceptingWork_.store(true);

    boost::asio::post(
        strand_,
        [this]()
        {
            startOnStrand();
        });
}

void udpclient::close()
{
    acceptingWork_.store(false);

    boost::asio::post(
        strand_,
        [this]()
        {
            closeOnStrand();
        });
}

void udpclient::send(std::string message)
{
    if (!acceptingWork_.load())
    {
        return;
    }

    boost::asio::post(
        strand_,
        [this, message = std::move(message)]() mutable
        {
            if (stopping_ || !started_)
            {
                return;
            }

            enqueueSendOnStrand(std::move(message));
        });
}

bool udpclient::FnGetMonitorStatus() const
{
    return monitorStatus_.load();
}

void udpclient::FnSetMonitorStatus(bool status)
{
    monitorStatus_.store(status);
}

void udpclient::startOnStrand()
{
    if (started_)
    {
        return;
    }

    stopping_ = false;
    sendInProgress_ = false;
    sendQueue_.clear();

    boost::system::error_code error;

    socket_.open(boost::asio::ip::udp::v4(), error);
    if (error)
    {
        logTransportError("[START] Failed to open socket | Error=" + error.message());
        return;
    }

    socket_.bind(
        boost::asio::ip::udp::endpoint(
            boost::asio::ip::udp::v4(),
            localPort_),
        error);

    if (error)
    {
        logTransportError(
            "[START] Failed to bind socket | Port=" +
            std::to_string(localPort_) +
            " | Error=" +
            error.message());

        socket_.close(error);
        return;
    }

    if (isBroadcast_)
    {
        socket_.set_option(boost::asio::socket_base::broadcast(true), error);

        if (error)
        {
            logTransportError("[START] Failed to enable broadcast | Error=" + error.message());

            socket_.close(error);
            return;
        }
    }

    started_ = true;

    Logger::getInstance()->FnLog(
        "[START] Listening | LocalPort=" +
            std::to_string(localPort_) +
            " | Remote=" +
            serverEndpoint_.address().to_string() +
            ":" +
            std::to_string(serverEndpoint_.port()),
        "",
        "UDP");

    startReceiveOnStrand();
}

void udpclient::closeOnStrand()
{
    if (stopping_)
    {
        return;
    }

    stopping_ = true;
    monitorStatus_.store(false);

    // If an async_send_to() is outstanding, its buffer points to the current
    // queue front. Keep that front alive until the completion handler runs.
    // Pending datagrams behind it can be discarded immediately.
    if (sendInProgress_)
    {
        while (sendQueue_.size() > 1)
        {
            sendQueue_.pop_back();
        }
    }
    else
    {
        sendQueue_.clear();
    }

    boost::system::error_code ignored;

    socket_.cancel(ignored);
    socket_.close(ignored);

    started_ = false;
}

void udpclient::startReceiveOnStrand()
{
    if (stopping_ || !started_ || !socket_.is_open())
    {
        return;
    }

    socket_.async_receive_from(
        boost::asio::buffer(receiveBuffer_),
        senderEndpoint_,
        [this](
            const boost::system::error_code& error,
            std::size_t bytesReceived)
        {
            handleReceiveOnStrand(error, bytesReceived);
        });
}

void udpclient::handleReceiveOnStrand(const boost::system::error_code& error, std::size_t bytesReceived)
{
    if (error)
    {
        if (error == boost::asio::error::operation_aborted || stopping_)
        {
            return;
        }

        logTransportError("[RX] Receive failed | Error=" + error.message());

        startReceiveOnStrand();
        return;
    }

    try
    {
        const std::string senderIp = senderEndpoint_.address().to_string();
        std::string packet(receiveBuffer_.data(), bytesReceived);

        if (localPort_ == 2008)
        {
            operation::getInstance()->FnOnMonitorUdpPacket(std::move(senderIp), std::move(packet));
        }
        else
        {
            const auto configuredLocalPort = parseInt(IniParser::getInstance()->FnGetLocalUDPPort());

            if (configuredLocalPort &&
                *configuredLocalPort >= 0 &&
                *configuredLocalPort <= 65535 &&
                localPort_ == static_cast<unsigned short>(*configuredLocalPort))
            {
                operation::getInstance()->FnOnPmsUdpPacket(std::move(senderIp), std::move(packet));
            }
        }
    }
    catch (const std::exception& e)
    {
        Logger::getInstance()->FnLogExceptionError(std::string("udpclient::handleReceiveOnStrand | Exception: ") + e.what());
    }
    catch (...)
    {
        Logger::getInstance()->FnLogExceptionError("udpclient::handleReceiveOnStrand | Unknown exception");
    }

    startReceiveOnStrand();
}

void udpclient::enqueueSendOnStrand(std::string message)
{
    if (message.empty())
    {
        return;
    }

    sendQueue_.push_back(std::move(message));

    if (!sendInProgress_)
    {
        startSendOnStrand();
    }
}

void udpclient::startSendOnStrand()
{
    if (stopping_ ||
        !started_ ||
        !socket_.is_open() ||
        sendQueue_.empty() ||
        sendInProgress_)
    {
        return;
    }

    sendInProgress_ = true;

    // The deque front stays alive until the completion handler runs.
    socket_.async_send_to(
        boost::asio::buffer(sendQueue_.front()),
        serverEndpoint_,
        [this](
            const boost::system::error_code& error,
            std::size_t bytesSent)
        {
            handleSendOnStrand(error, bytesSent);
        });
}

void udpclient::handleSendOnStrand(const boost::system::error_code& error, std::size_t /*bytesSent*/)
{
    sendInProgress_ = false;

    if (!sendQueue_.empty())
    {
        sendQueue_.pop_front();
    }

    if (error)
    {
        if (error == boost::asio::error::operation_aborted || stopping_)
        {
            return;
        }

        logTransportError("[TX] Send failed | Error=" + error.message());
    }

    startSendOnStrand();
}

void udpclient::logTransportError(const std::string& message) const
{
    Logger::getInstance()->FnLog(message, "", "UDP");
}
