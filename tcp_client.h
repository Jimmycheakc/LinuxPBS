#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include <boost/asio.hpp>

class AppTcpClient
{
public:

    using ConnectHandler = std::function<void(bool success, const std::string& message)>;

    using SendHandler = std::function<void(bool success, const std::string& message)>;

    using CloseHandler = std::function<void(bool success, const std::string& message)>;

    using ReceiveHandler = std::function<void(bool success, const std::vector<std::uint8_t>& data)>;

    AppTcpClient(boost::asio::io_context& io_context, const std::string& ipAddress, unsigned short port);

    ~AppTcpClient() = default;

    AppTcpClient(const AppTcpClient&) = delete;
    AppTcpClient& operator=(const AppTcpClient&) = delete;
    AppTcpClient(AppTcpClient&&) = delete;
    AppTcpClient& operator=(AppTcpClient&&) = delete;

    // Thread-safe entry points. Socket state is changed only on ioContext_.
    void connect();
    void send(const std::vector<uint8_t>& message);
    void close();

    // Synchronous cross-thread snapshot.
    bool isConnected() const;

    // Configure handlers before starting normal client activity.
    void setConnectHandler(ConnectHandler handler);
    void setSendHandler(SendHandler handler);
    void setCloseHandler(CloseHandler handler);
    void setReceiveHandler(ReceiveHandler handler);

private:
    static constexpr std::size_t kReceiveBufferSize = 2048;

    // The io_context is owned and run by the parent module. AppTcpClient does
    // not own a thread, work guard, strand, timer, or nested run() loop.
    boost::asio::io_context& ioContext_;
    boost::asio::ip::tcp::socket socket_;
    const boost::asio::ip::tcp::endpoint endpoint_;

    std::array<std::uint8_t, kReceiveBufferSize> receiveBuffer_{};

    // All members below, except connectedSnapshot_, are owned by the single
    // ioContext_ thread after client activity starts.
    std::deque<std::vector<std::uint8_t>> writeQueue_;
    bool connecting_{false};
    bool closing_{false};
    bool writeInProgress_{false};
    std::uint64_t connectionGeneration_{0};

    std::atomic<bool> connectedSnapshot_{false};

    ConnectHandler connectHandler_;
    SendHandler sendHandler_;
    CloseHandler closeHandler_;
    ReceiveHandler receiveHandler_;

    void connectOnIoThread();
    void enqueueSendOnIoThread(std::vector<std::uint8_t> message);
    void startNextWriteOnIoThread(std::uint64_t generation);
    void startAsyncReceiveOnIoThread(std::uint64_t generation);

    void closeOnIoThread(bool notifyHandler);
    void closeSocketNoThrow();

    void handleTransportFailureOnIoThread(const boost::system::error_code& error, bool notifyReceiveHandler);
};