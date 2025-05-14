#pragma once

#include <cstdint>
#include <mutex>
#include <thread>
#include "boost/asio.hpp"
#include "boost/asio/serial_port.hpp"

class LCD
{

public:
    static const int MAXIMUM_CHARACTER_PER_ROW  = 20;
    static const int MAXIMUM_LCD_LINES          = 2;
    static const char BLOCK                     = 219;

    static LCD* getInstance();
    bool FnLCDInit();
    void FnLCDClear();
    void FnLCDHome();
    void FnLCDDisplayCharacter(char aChar);
    void FnLCDDisplayString(std::uint8_t row, std::uint8_t col, char* str);
    void FnLCDDisplayStringCentered(std::uint8_t row, char* str);
    void FnLCDClearDisplayRow(std::uint8_t row);
    void FnLCDDisplayRow(std::uint8_t row, char* str);
    void FnLCDDisplayScreen(char* str);
    void FnLCDWipeOnLR(char* str);
    void FnLCDWipeOnRL(char* str);
    void FnLCDWipeOffLR();
    void FnLCDWipeOffRL();
    void FnLCDCursorLeft();
    void FnLCDCursorRight();
    void FnLCDCursorOn();
    void FnLCDCursorOff();
    void FnLCDDisplayOff();
    void FnLCDDisplayOn();
    void FnLCDCursorReset();
    void FnLCDCursor(std::uint8_t row, std::uint8_t col);
    void FnLCDClose();

    /**
     * Singleton LCD should not be cloneable.
     */
    LCD (LCD& lcd) = delete;

    /**
     * Singleton LCD should not be assignable.
     */
    void operator=(const LCD&) = delete;

private:
    static LCD* lcd_;
    static std::mutex mutex_;
    int lcdFd_;
    bool lcdInitialized_;
    boost::asio::io_context ioContext_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    std::thread ioContextThread_;
    LCD();
    void FnLCDInitDriver();
    void FnLCDDeinitDriver();
    void startIoContextThread();
    void sendCommandDataToDriver(int fd, char* data, bool value);
};