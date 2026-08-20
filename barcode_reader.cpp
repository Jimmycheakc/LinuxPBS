#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <utility>
#include <unistd.h>

#include <libevdev-1.0/libevdev/libevdev.h>
#include <linux/input-event-codes.h>

#if defined(__linux__)
#include <pthread.h>
#endif

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>

#include "barcode_reader.h"
#include "event_manager.h"
#include "log.h"

namespace
{
constexpr auto BARCODE_RECONNECT_INTERVAL = std::chrono::seconds(5);
}

BARCODE_READER::BARCODE_READER()
    : ioContext_(),
      reconnectTimer_(ioContext_),
      inputDescriptor_(ioContext_),
      moduleRunning_(false),
      stopping_(false),
      readRequested_(false),
      monitoringLoopRunning_(false),
      deviceConnected_(false),
      deviceUnavailableLogged_(false),
      leftShiftPressed_(false),
      rightShiftPressed_(false),
      deviceFd_(-1),
      evdev_(nullptr),
      logFileName_("barcode")
{
}

BARCODE_READER::~BARCODE_READER()
{
    FnBarcodeReaderShutdown();
}

BARCODE_READER* BARCODE_READER::getInstance()
{
    // C++11+ guarantees thread-safe construction of a function-local static.
    static BARCODE_READER instance;
    return &instance;
}

void BARCODE_READER::FnBarcodeReaderInit()
{
    // If a previous run has already exited, join the old std::thread before
    // assigning a new one.
    if (!moduleRunning_.load() && ioThread_.joinable())
    {
        if (std::this_thread::get_id() == ioThread_.get_id())
            return;

        ioThread_.join();
    }

    bool expected = false;
    if (!moduleRunning_.compare_exchange_strong(expected, true))
    {
        Logger::getInstance()->FnLog("Barcode Reader module is already running.", logFileName_, "BCODE");
        return;
    }

    stopping_.store(false);
    readRequested_.store(false);
    Ticket_In.store(0);

    ioContext_.restart();
    workGuard_.emplace(boost::asio::make_work_guard(ioContext_));

    boost::asio::co_spawn(
        ioContext_,
        barcodeModuleInitAsync(),
        [this](std::exception_ptr ep)
        {
            if (!ep)
                return;

            try
            {
                std::rethrow_exception(ep);
            }
            catch (const std::exception& e)
            {
                std::stringstream ss;
                ss << "barcodeModuleInitAsync exception: " << e.what();
                Logger::getInstance()->FnLogExceptionError(ss.str());
            }
            catch (...)
            {
                Logger::getInstance()->FnLogExceptionError(
                    "barcodeModuleInitAsync unknown exception.");
            }
        });

    // Exactly one thread owns and drives the Barcode module io_context.
    ioThread_ = std::thread([this]()
    {
#if defined(__linux__)
        ::pthread_setname_np(::pthread_self(), "BARCODE_IO");
#endif
        while (!stopping_.load())
        {
            try
            {
                ioContext_.run();
                break;
            }
            catch (const std::exception& e)
            {
                std::stringstream ss;
                ss << "Barcode io_context handler exception: " << e.what();
                Logger::getInstance()->FnLogExceptionError(ss.str());
            }
            catch (...)
            {
                Logger::getInstance()->FnLogExceptionError(
                    "Barcode io_context handler unknown exception.");
            }
        }
    });
}

boost::asio::awaitable<void> BARCODE_READER::barcodeModuleInitAsync()
{
    Logger::getInstance()->FnCreateLogFile(logFileName_);

    // Preserve the original initialisation behaviour: report whether the
    // configured device path exists now. StartRead() will still retry later if
    // the device is currently absent.
    if (isDeviceAvailable(barcodeFilePath))
    {
        Logger::getInstance()->FnLog("Barcode Reader initialization completed.");
        Logger::getInstance()->FnLog("Barcode Reader initialization completed.", logFileName_, "BCODE");
    }
    else
    {
        Logger::getInstance()->FnLog("Barcode Reader initialization failed.");
        Logger::getInstance()->FnLog("Barcode Reader initialization failed.", logFileName_, "BCODE");
    }

    co_return;

}

void BARCODE_READER::FnBarcodeStartRead()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "BCODE");
    
    if (!moduleRunning_.load() || stopping_.load())
    {
        Logger::getInstance()->FnLog("Barcode Reader module is not running.", logFileName_, "BCODE");
        return;
    }

    bool expected = false;
    if (!readRequested_.compare_exchange_strong(expected, true))
        return; // already requested/running

    boost::asio::post(
        ioContext_,
        [this]()
        {
            if (stopping_.load())
            {
                readRequested_.store(false);
                return;
            }

            if (monitoringLoopRunning_)
                return;

            monitoringLoopRunning_ = true;

            boost::asio::co_spawn(
                ioContext_,
                monitoringLoopAsync(),
                [this](std::exception_ptr ep)
                {
                    monitoringLoopRunning_ = false;

                    if (!ep)
                        return;

                    readRequested_.store(false);

                    try
                    {
                        std::rethrow_exception(ep);
                    }
                    catch (const std::exception& e)
                    {
                        std::stringstream ss;
                        ss << "monitoringLoopAsync exception: " << e.what();
                        Logger::getInstance()->FnLogExceptionError(ss.str());
                    }
                    catch (...)
                    {
                        Logger::getInstance()->FnLogExceptionError("monitoringLoopAsync unknown exception.");
                    }
                });
        });
}

void BARCODE_READER::FnBarcodeStopRead()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "BCODE");

    readRequested_.store(false);

    if (!moduleRunning_.load())
        return;

    // Do not cancel/close Asio objects directly from a foreign thread.
    boost::asio::post(
        ioContext_,
        [this]()
        {
            boost::system::error_code ignored;
            reconnectTimer_.cancel(ignored);

            if (inputDescriptor_.is_open())
                inputDescriptor_.cancel(ignored);
        });
}

void BARCODE_READER::FnBarcodeReaderShutdown()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "BCODE");

    const bool wasRunning = moduleRunning_.exchange(false);

    if (wasRunning)
    {
        stopping_.store(true);
        readRequested_.store(false);

        if (ioThread_.joinable() &&
            std::this_thread::get_id() == ioThread_.get_id())
        {
            // Never join the current thread.
            shutdownOnIoThread();
            return;
        }

        boost::asio::post(
            ioContext_,
            [this]()
            {
                shutdownOnIoThread();
            });
    }

    if (ioThread_.joinable() &&
        std::this_thread::get_id() != ioThread_.get_id())
    {
        ioThread_.join();
    }

    // No handlers are executing after join().
    workGuard_.reset();
    stopping_.store(false);
    readRequested_.store(false);
    monitoringLoopRunning_ = false;
}

void BARCODE_READER::shutdownOnIoThread()
{
    readRequested_.store(false);

    boost::system::error_code ignored;
    reconnectTimer_.cancel(ignored);

    if (inputDescriptor_.is_open())
        inputDescriptor_.cancel(ignored);

    closeDeviceOnIoThread();

    // Let run() return after all cancelled operation completions/coroutines
    // have been dispatched.
    workGuard_.reset();
}

bool BARCODE_READER::FnIsBarcodeReaderRunning() const
{
    return moduleRunning_.load();
}

bool BARCODE_READER::FnIsBarcodeReading() const
{
    return readRequested_.load();
}

boost::asio::awaitable<void> BARCODE_READER::monitoringLoopAsync()
{
    deviceUnavailableLogged_ = false;

    while (readRequested_.load() && !stopping_.load())
    {
        if (!deviceConnected_)
        {
            if (!openDeviceOnIoThread())
            {
                if (!deviceUnavailableLogged_)
                {
                    Logger::getInstance()->FnLog("Barcode not connected, retrying...", logFileName_, "BCODE");
                    deviceUnavailableLogged_ = true;
                }

                if (!co_await waitForReconnectAsync())
                    break;

                continue;
            }

            Logger::getInstance()->FnLog("Barcode device connected", logFileName_, "BCODE");
            deviceUnavailableLogged_ = false;
        }

        boost::system::error_code waitEc;
        co_await inputDescriptor_.async_wait(
            boost::asio::posix::stream_descriptor::wait_read,
            boost::asio::redirect_error(
                boost::asio::use_awaitable,
                waitEc));

        if (!readRequested_.load() || stopping_.load())
            break;

        if (waitEc == boost::asio::error::operation_aborted)
            continue;

        if (waitEc)
        {
            std::stringstream ss;
            ss << "Barcode descriptor wait error: " << waitEc.message();
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "BCODE");

            closeDeviceOnIoThread();

            if (!co_await waitForReconnectAsync())
                break;

            continue;
        }

        const DeviceReadResult readResult = drainAvailableEvents();

        if (readResult == DeviceReadResult::Disconnected)
        {
            Logger::getInstance()->FnLog("Barcode device disconnected in running mode.", logFileName_, "BCODE");

            closeDeviceOnIoThread();
            deviceUnavailableLogged_ = false;

            if (!co_await waitForReconnectAsync())
                break;
        }
        else if (readResult == DeviceReadResult::Error)
        {
            closeDeviceOnIoThread();

            if (!co_await waitForReconnectAsync())
                break;
        }
    }

    closeDeviceOnIoThread();
    monitoringLoopRunning_ = false;
    co_return;
}

boost::asio::awaitable<bool> BARCODE_READER::waitForReconnectAsync()
{
    if (!readRequested_.load() || stopping_.load())
        co_return false;

    reconnectTimer_.expires_after(BARCODE_RECONNECT_INTERVAL);

    boost::system::error_code ec;
    co_await reconnectTimer_.async_wait(
        boost::asio::redirect_error(
            boost::asio::use_awaitable,
            ec));
    
    if (!readRequested_.load() || stopping_.load())
        co_return false;

    if (ec == boost::asio::error::operation_aborted)
        co_return false;

    if (ec)
    {
        std::stringstream ss;
        ss << "Barcode reconnect timer error: " << ec.message();
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "BCODE");
        co_return false;
    }

    co_return true;
}

bool BARCODE_READER::openDeviceOnIoThread()
{
    closeDeviceOnIoThread();

    const int fd = ::open(barcodeFilePath.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);

    if (fd < 0)
        return false;

    libevdev* newDev = nullptr;
    const int rc = libevdev_new_from_fd(fd, &newDev);
    if (rc < 0)
    {
        std::stringstream ss;
        ss << "Failed to initialize libevdev: " << std::strerror(-rc);
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "BCODE");
        ::close(fd);
        return false;
    }

    boost::system::error_code assignEc;
    inputDescriptor_.assign(fd, assignEc);
    if (assignEc)
    {
        std::stringstream ss;
        ss << "Failed to assign barcode fd to Boost.Asio: " << assignEc.message();
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "BCODE");

        libevdev_free(newDev);
        ::close(fd);
        return false;
    }

    // From this point inputDescriptor_ owns fd. libevdev uses the same fd but
    // does not own its lifetime in this module.
    deviceFd_ = fd;
    evdev_ = newDev;
    deviceConnected_ = true;
    barcodeBuffer_.clear();
    leftShiftPressed_ = false;
    rightShiftPressed_ = false;

    return true;
}


void BARCODE_READER::closeDeviceOnIoThread()
{
    // Free libevdev while the underlying fd is still valid.
    if (evdev_ != nullptr)
    {
        libevdev_free(evdev_);
        evdev_ = nullptr;
    }

    if (inputDescriptor_.is_open())
    {
        boost::system::error_code ignored;
        inputDescriptor_.cancel(ignored);
        inputDescriptor_.close(ignored); // closes the fd assigned to it
    }

    deviceFd_ = -1;
    deviceConnected_ = false;
    barcodeBuffer_.clear();
    leftShiftPressed_ = false;
    rightShiftPressed_ = false;
}

BARCODE_READER::DeviceReadResult BARCODE_READER::drainAvailableEvents()
{
    if (evdev_ == nullptr || deviceFd_ < 0)
        return DeviceReadResult::Error;

    input_event ev{};

    for (;;)
    {
        const int rc = libevdev_next_event(evdev_, LIBEVDEV_READ_FLAG_NORMAL, &ev);

        if (rc == LIBEVDEV_READ_STATUS_SUCCESS)
        {
            processInputEvent(ev);
            continue;
        }

        if (rc == LIBEVDEV_READ_STATUS_SYNC)
        {
            // libevdev reports that events were dropped. Drain its sync stream
            // so its internal state catches up, processing key events along the
            // way.
            processInputEvent(ev);

            for (;;)
            {
                const int syncRc = libevdev_next_event(evdev_, LIBEVDEV_READ_FLAG_SYNC, &ev);

                if (syncRc == LIBEVDEV_READ_STATUS_SYNC)
                {
                    processInputEvent(ev);
                    continue;
                }

                if (syncRc == -EAGAIN)
                    break;

                if (syncRc == -ENODEV)
                    return DeviceReadResult::Disconnected;

                if (syncRc < 0)
                {
                    std::stringstream ss;
                    ss << "libevdev sync read error: " << std::strerror(-syncRc);
                    Logger::getInstance()->FnLog(ss.str(), logFileName_, "BCODE");
                    return DeviceReadResult::Error;
                }
            }

            continue;
        }

        if (rc == -EAGAIN)
            return DeviceReadResult::Ready;

        if (rc == -ENODEV)
            return DeviceReadResult::Disconnected;

        if (rc < 0)
        {
            std::stringstream ss;
            ss << "libevdev read error: " << std::strerror(-rc);
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "BCODE");
            return DeviceReadResult::Error;
        }
    }
}

void BARCODE_READER::processInputEvent(const input_event& ev)
{
    if (ev.type != EV_KEY)
        return;

    if (ev.code == KEY_LEFTSHIFT)
    {
        leftShiftPressed_ = (ev.value != 0);
        return;
    }

    if (ev.code == KEY_RIGHTSHIFT)
    {
        rightShiftPressed_ = (ev.value != 0);
        return;
    }

    // Preserve the old behaviour: consume only key-down events, not key-up or
    // auto-repeat events.
    if (ev.value != 1)
        return;

    if (ev.code == KEY_ENTER)
    {
        emitCompletedBarcode();
        return;
    }

    std::string key = keyFromScancode(ev.code);
    if (key.empty())
        return;

    const bool shiftPressed = leftShiftPressed_ || rightShiftPressed_;

    // Preserve the old mapping behaviour for letters while correctly handling
    // either Shift key. We intentionally do not invent shifted punctuation
    // mappings that the old module did not support.
    if (shiftPressed && key.size() == 1)
    {
        const unsigned char ch = static_cast<unsigned char>(key[0]);
        if (std::isalpha(ch))
            key[0] = static_cast<char>(std::toupper(ch));
    }

    barcodeBuffer_ += key;
}

void BARCODE_READER::emitCompletedBarcode()
{
    if (barcodeBuffer_.empty())
        return;

    std::string barcode = std::move(barcodeBuffer_);
    barcodeBuffer_.clear();

    Logger::getInstance()->FnLog("INFO: Barcode Scanned | Barcode: " + barcode, logFileName_, "BCODE");

    Ticket_In.store(1);

    // This executes on the Barcode module thread, just as the previous module
    // emitted the event from its monitoring thread. EventManager must therefore
    // provide a thread-safe enqueue operation.
    EventManager::getInstance()->FnEnqueueEvent("Evt_handleBarcodeReceived", barcode);
}

bool BARCODE_READER::isDeviceAvailable(const std::string& devicePath) const
{
    return (::access(devicePath.c_str(), F_OK) == 0);
}

std::string BARCODE_READER::keyFromScancode(unsigned int scancode) const
{
    if (scancode >= scancodes.size())
    {
        std::stringstream ss;
        ss << "Ignoring unsupported barcode scancode: " << scancode;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "BCODE");
        return {};
    }

    return scancodes[scancode];
}
