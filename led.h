#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/serial_port.hpp>

class LED
{

public:
    inline static constexpr char STX1 = 0x02;
    inline static constexpr char ETX1 = 0x0D;
    inline static constexpr char ETX2 = 0x0A;

    inline static constexpr int LED614_MAX_CHAR_PER_ROW = 4;
    inline static constexpr int LED216_MAX_CHAR_PER_ROW = 16;
    inline static constexpr int LED226_MAX_CHAR_PER_ROW = 26;
    
    enum class FontSize
    {
        SMALL_FONT,
        BIG_FONT
    };

    enum class Line
    {
        FIRST,
        SECOND
    };

    enum class Alignment
    {
        LEFT,
        RIGHT,
        CENTER
    };

    LED(unsigned int baudRate, const std::string& comPortName, int maxCharacterPerRow);
    ~LED();

    LED(const LED&) = delete;
    LED& operator=(const LED&) = delete;
    LED(LED&&) = delete;
    LED& operator=(LED&&) = delete;

    bool FnLEDInit();
    void FnLEDClose();

    void FnLEDSendLEDMsg(const std::string& ledId, const std::string& text, Alignment align);
    unsigned int FnGetLEDBaudRate() const;
    std::string FnGetLEDComPortName() const;
    int FnGetLEDMaxCharPerRow() const;
    bool FnIsLEDInitialized() const;

private:
    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    boost::asio::io_context ioContext_;
    std::optional<WorkGuard> workGuard_;
    std::thread ioThread_;
    boost::asio::serial_port serialPort_;

    const unsigned int baudRate_;
    const std::string comPortName_;
    const int maxCharPerRow_;
    std::string logFileName_;
    std::string ledType_;

    std::atomic<bool> initialized_{false};
    std::atomic<bool> acceptingWork_{false};
    std::atomic<bool> running_{false};
    std::mutex lifecycleMutex_;

    // Owned exclusively by ioThread_. No mutex is required.
    std::deque<std::vector<char>> writeQueue_;
    bool writeInProgress_{false};
    bool stopRequested_{false};

    void startIoContextThread();
    void runIoContext();
    bool openSerialPort();
    void requestStopOnIoThread();

    void handleSendMessageOnIoThread(const std::string& ledId, const std::string& text, Alignment align);

    std::vector<char> formatDisplayMsg(const std::string& ledId, Line lineNo, const std::string& text, Alignment align) const;

    void enqueueWriteOnIoThread(std::vector<char> data);
    void startNextWriteOnIoThread();
    void handleWriteComplete(const boost::system::error_code& ec, std::size_t bytesTransferred);

    bool isSupportedDisplayWidth() const;
    void log(const std::string& message) const;
};


// LED Manager
class LEDManager
{

public:
    static LEDManager* getInstance();
    
    void createLED(unsigned int baudRate, const std::string& comPortName, int maxCharacterPerRow);

    LED* getLED(const std::string& ledComPort);

private:
    LEDManager() = default;
    ~LEDManager() = default;

    LEDManager(const LEDManager&) = delete;
    LEDManager& operator=(const LEDManager&) = delete;
    LEDManager(LEDManager&&) = delete;
    LEDManager& operator=(LEDManager&&) = delete;

    std::mutex ledsMutex_;
    std::vector<std::unique_ptr<LED>> leds_;

};