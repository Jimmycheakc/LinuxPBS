#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/steady_timer.hpp>


struct input_event;
struct libevdev;

class BARCODE_READER
{
public:

    const std::string barcodeFilePath = "/dev/input/barcode_scanner";

    // Scancode mappings
    const std::vector<std::string> scancodes = {
        "",             // 0: KEY_RESERVED
        "ESC",          // 1: KEY_ESC
        "1",            // 2: KEY_1
        "2",            // 3: KEY_2
        "3",            // 4: KEY_3
        "4",            // 5: KEY_4
        "5",            // 6: KEY_5
        "6",            // 7: KEY_6
        "7",            // 8: KEY_7
        "8",            // 9: KEY_8
        "9",            // 10: KEY_9
        "0",            // 11: KEY_0
        "-",            // 12: KEY_MINUS
        "=",            // 13: KEY_EQUAL
        "BACKSPACE",    // 14: KEY_BACKSPACE
        "TAB",          // 15: KEY_TAB
        "q",            // 16: KEY_Q
        "w",            // 17: KEY_W
        "e",            // 18: KEY_E
        "r",            // 19: KEY_R
        "t",            // 20: KEY_T
        "y",            // 21: KEY_Y
        "u",            // 22: KEY_U
        "i",            // 23: KEY_I
        "o",            // 24: KEY_O
        "p",            // 25: KEY_P
        "[",            // 26: KEY_LEFTBRACE
        "]",            // 27: KEY_RIGHTBRACE
        "ENTER",        // 28: KEY_ENTER
        "LEFTCTRL",     // 29: KEY_LEFTCTRL
        "a",            // 30: KEY_A
        "s",            // 31: KEY_S
        "d",            // 32: KEY_D
        "f",            // 33: KEY_F
        "g",            // 34: KEY_G
        "h",            // 35: KEY_H
        "j",            // 36: KEY_J
        "k",            // 37: KEY_K
        "l",            // 38: KEY_L
        ";",            // 39: KEY_SEMICOLON
        "'",            // 40: KEY_APOSTROPHE
        "`",            // 41: KEY_GRAVE
        "LEFTSHIFT",    // 42: KEY_LEFTSHIFT
        "\\",           // 43: KEY_BACKSLASH
        "z",            // 44: KEY_Z
        "x",            // 45: KEY_X
        "c",            // 46: KEY_C
        "v",            // 47: KEY_V
        "b",            // 48: KEY_B
        "n",            // 49: KEY_N
        "m",            // 50: KEY_M
        ",",            // 51: KEY_COMMA
        ".",            // 52: KEY_DOT
        "/",            // 53: KEY_SLASH
        "RIGHTSHIFT",   // 54: KEY_RIGHTSHIFT
        "*",            // 55: KEY_KPASTERISK
        "LEFTALT",      // 56: KEY_LEFTALT
        "SPACE",        // 57: KEY_SPACE
        "CAPSLOCK"      // 58: KEY_CAPSLOCK
    };

    static BARCODE_READER* getInstance();

    void FnBarcodeReaderInit();
    void FnBarcodeStartRead();
    void FnBarcodeStopRead();
    void FnBarcodeReaderShutdown();

    bool FnIsBarcodeReaderRunning() const;
    bool FnIsBarcodeReading() const;

    // Preserved public member for compatibility. It is now atomic because it
    // is written by the Barcode module thread and may be read elsewhere.
    std::atomic<int> Ticket_In{0};

    BARCODE_READER(const BARCODE_READER&) = delete;
    BARCODE_READER& operator=(const BARCODE_READER&) = delete;
    BARCODE_READER(BARCODE_READER&&) = delete;
    BARCODE_READER& operator=(BARCODE_READER&&) = delete;

    ~BARCODE_READER();

private:
    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    enum class DeviceReadResult
    {
        Ready,
        Disconnected,
        Error
    };

    BARCODE_READER();

    // Module lifecycle / executor ownership.
    boost::asio::io_context ioContext_;
    boost::asio::steady_timer reconnectTimer_;
    boost::asio::posix::stream_descriptor inputDescriptor_;
    std::optional<WorkGuard> workGuard_;
    std::thread ioThread_;

    // These flags are visible from external threads.
    std::atomic<bool> moduleRunning_;
    std::atomic<bool> stopping_;
    std::atomic<bool> readRequested_;

    // The following state is confined to the Barcode module thread.
    bool monitoringLoopRunning_;
    bool deviceConnected_;
    bool deviceUnavailableLogged_;
    bool leftShiftPressed_;
    bool rightShiftPressed_;
    int deviceFd_;
    libevdev* evdev_;
    std::string barcodeBuffer_;
    std::string logFileName_;

    boost::asio::awaitable<void> barcodeModuleInitAsync();
    boost::asio::awaitable<void> monitoringLoopAsync();
    boost::asio::awaitable<bool> waitForReconnectAsync();

    bool openDeviceOnIoThread();
    void closeDeviceOnIoThread();
    void shutdownOnIoThread();

    DeviceReadResult drainAvailableEvents();
    void processInputEvent(const input_event& ev);
    void emitCompletedBarcode();

    bool isDeviceAvailable(const std::string& devicePath) const;
    std::string keyFromScancode(unsigned int scancode) const;
};