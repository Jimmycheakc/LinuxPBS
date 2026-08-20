#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <boost/asio.hpp>

class LCD
{

public:
    static constexpr int MAXIMUM_CHARACTER_PER_ROW = 20;
    static constexpr int MAXIMUM_LCD_LINES = 2;
    static constexpr char BLOCK = static_cast<char>(219);

    static LCD* getInstance();

    bool FnLCDInit();
    void FnLCDClose();

    void FnLCDClear();
    void FnLCDHome();
    void FnLCDDisplayCharacter(char aChar);
    void FnLCDDisplayString(std::uint8_t row, std::uint8_t col, const char* str);
    void FnLCDDisplayStringCentered(std::uint8_t row, const char* str);
    void FnLCDClearDisplayRow(std::uint8_t row);
    void FnLCDDisplayRow(std::uint8_t row, const char* str);
    void FnLCDDisplayScreen(const char* str);
    void FnLCDWipeOnLR(const char* str);
    void FnLCDWipeOnRL(const char* str);
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

private:
    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
    
    LCD();
    ~LCD();

    LCD(const LCD&) = delete;
    LCD& operator=(const LCD&) = delete;
    LCD(LCD&&) = delete;
    LCD& operator=(LCD&&) = delete;

    // Lifecycle
    void startIoContextThread();
    void runIoContext();
    bool initDriverOnIoThread();
    void deinitDriverOnIoThread();
    void postOperation(std::function<void()> operation);

    // Driver helpers. These run only on ioThread_.
    void sendDriverRawOnIoThread(std::string rawData, bool isData);
    void sendCommandOnIoThread(std::string_view command);
    void sendDataOnIoThread(std::string_view data);

    // LCD helpers. These run only on ioThread_.
    static std::string toHexString(std::string_view rawData);
    static bool isValidRow(std::uint8_t row);
    static bool isValidPosition(std::uint8_t row, std::uint8_t col);

    void setCursorOnIoThread(std::uint8_t row, std::uint8_t col);
    void clearDisplayRowOnIoThread(std::uint8_t row);
    void displayStringOnIoThread(std::uint8_t row, std::uint8_t col, std::string_view text);
    void displayStringCenteredOnIoThread(std::uint8_t row, std::string_view text);
    void displayScreenOnIoThread(std::string_view text);

    // Lifecycle state visible across threads.
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    std::atomic<bool> acceptingWork_{false};
    std::mutex lifecycleMutex_;

    // Owned by the LCD io thread after initialization starts.
    int lcdFd_{-1};

    boost::asio::io_context ioContext_;
    std::optional<WorkGuard> workGuard_;
    std::thread ioThread_;
};