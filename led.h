#pragma once

#include <iostream>
#include <string>
#include <memory>
#include <mutex>
#include "boost/asio.hpp"
#include "boost/asio/serial_port.hpp"

class LED
{

public:
    static const char STX1;
    static const char ETX1;
    static const char ETX2;

    static const int LED614_MAX_CHAR_PER_ROW = 4;
    static const int LED216_MAX_CHAR_PER_ROW = 16;
    static const int LED226_MAX_CHAR_PER_ROW = 26;
    
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

    LED(boost::asio::io_context& io_context, unsigned int baudRate, const std::string& comPortName, int maxCharacterPerRow);
    ~LED();
    void FnLEDSendLEDMsg(std::string LedId, std::string text, LED::Alignment align);
    unsigned int FnGetLEDBaudRate() const;
    std::string FnGetLEDComPortName() const;
    int FnGetLEDMaxCharPerRow() const;

private:
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::serial_port serialPort_;
    unsigned int baudRate_;
    std::string comPortName_;
    int maxCharPerRow_;
    std::string logFileName_;

    void FnFormatDisplayMsg(std::string LedId, LED::Line lineNo, std::string text, LED::Alignment align, std::vector<char>& result);
};


// LED Manager
class LEDManager
{

public:
    static LEDManager* getInstance();
    static std::mutex mutex_;
    void createLED(boost::asio::io_context& io_context, unsigned int baudRate, const std::string& comPortName, int maxCharacterPerRow);
    LED* getLED(const std::string& ledComPort);

    /**
     * Singleton LEDManager should not be cloneable.
     */
    LEDManager(LEDManager& ledManager) = delete;

    /**
     * Singleton LEDManager should not be assignable.
     */
    void operator=(const LEDManager&) = delete;

private:
    static LEDManager* ledManager_;
    std::vector<std::unique_ptr<LED>> leds_;
    LEDManager();

};