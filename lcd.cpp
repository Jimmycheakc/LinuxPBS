#include "lcd.h"

#include <algorithm>
#include <future>
#include <iomanip>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#endif

#include "ch341_lib.h"
#include "log.h"
#include "ps_par.h"

namespace
{
constexpr const char* LCD_DEVICE = "/dev/ch34x_pis0";
}

LCD::LCD()
    : workGuard_(boost::asio::make_work_guard(ioContext_))
{
}

LCD::~LCD()
{
    acceptingWork_.store(false);

    // Emergency/destructor fallback only. Normal shutdown uses FnLCDClose(),
    // which lets queued LCD work drain before io_context::run() returns.
    if (ioThread_.joinable())
    {
        ioContext_.stop();
        ioThread_.join();
    }

    if (lcdFd_ >= 0)
    {
        CH34xCloseDevice(lcdFd_);
        lcdFd_ = -1;
    }
}

LCD* LCD::getInstance()
{
    static LCD instance;
    return &instance;
}

bool LCD::FnLCDInit()
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);

    if (initialized_.load())
    {
        return true;
    }

    // Allow the io_context to be started again after a previous clean close.
    ioContext_.restart();

    if (!workGuard_)
    {
        workGuard_.emplace(boost::asio::make_work_guard(ioContext_));
    }

    auto resultPromise = std::make_shared<std::promise<bool>>();
    auto resultFuture = resultPromise->get_future();

    boost::asio::post(
        ioContext_,
        [this, resultPromise]()
        {
            bool success = false;

            try
            {
                success = initDriverOnIoThread();
            }
            catch (const std::exception& e)
            {
                std::stringstream ss;
                ss << __func__ << " | Exception: " << e.what();
                Logger::getInstance()->FnLogExceptionError(ss.str());
            }
            catch (...)
            {
                Logger::getInstance()->FnLogExceptionError(std::string(__func__) + " | Unknown exception");
            }

            initialized_.store(success);
            acceptingWork_.store(success);
            resultPromise->set_value(success);
        });

    startIoContextThread();

    const bool success = resultFuture.get();

    if (!success)
    {
        acceptingWork_.store(false);
        workGuard_.reset();

        if (ioThread_.joinable())
        {
            ioThread_.join();
        }
    }

    Logger::getInstance()->FnLog(success ? "LCD initialization completed." : "LCD initialization failed.");

    return success;
}

void LCD::FnLCDClose()
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);

    if (!ioThread_.joinable())
    {
        initialized_.store(false);
        acceptingWork_.store(false);
        return;
    }

    // Reject new display work first. Operations already queued are allowed
    // to run before this close handler because normal shutdown is graceful.
    acceptingWork_.store(false);

    boost::asio::post(
        ioContext_,
        [this]()
        {
            deinitDriverOnIoThread();
            initialized_.store(false);
        });

    // Do not call ioContext_.stop() during normal shutdown. Once previously
    // queued work and the close handler finish, run() may return naturally.
    workGuard_.reset();

    if (ioThread_.joinable() &&
        std::this_thread::get_id() != ioThread_.get_id())
    {
        ioThread_.join();
    }
}

void LCD::startIoContextThread()
{
    if (ioThread_.joinable())
    {
        return;
    }

    running_.store(true);

    ioThread_ = std::thread(
        [this]()
        {
#if defined(__linux__)
            ::pthread_setname_np(::pthread_self(), "LCD_IO");
#endif
            runIoContext();
        });
}

void LCD::runIoContext()
{
    try
    {
        ioContext_.run();
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << " | Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        Logger::getInstance()->FnLogExceptionError(std::string(__func__) + " | Unknown exception");
    }

    running_.store(false);
}

bool LCD::initDriverOnIoThread()
{
    if (lcdFd_ >= 0)
    {
        return true;
    }

    lcdFd_ = CH34xOpenDevice(LCD_DEVICE);

    if (lcdFd_ < 0)
    {
        std::stringstream ss;
        ss << "Failed to open CH34x LCD driver" << " | Device=" << LCD_DEVICE << " | fd=" << lcdFd_;
        Logger::getInstance()->FnLog(ss.str());
        return false;
    }

    // Execute initialization synchronously on the single LCD I/O thread.
    // This guarantees ordering and removes the old usleep(250000), which was
    // only needed to give asynchronously posted initialization writes time
    // to finish before FnLCDInit() returned.
    sendCommandOnIoThread("0x38");
    sendCommandOnIoThread("0x06");
    sendCommandOnIoThread("0x0C");
    sendCommandOnIoThread("0x01");
    sendCommandOnIoThread("0x0C");
    setCursorOnIoThread(1, 1);

    return true;
}

void LCD::deinitDriverOnIoThread()
{
    if (lcdFd_ < 0)
    {
        return;
    }

    if (!CH34xCloseDevice(lcdFd_))
    {
        std::stringstream ss;
        ss << "Failed to close LCD device" << " | fd=" << lcdFd_;
        Logger::getInstance()->FnLog(ss.str());
    }

    lcdFd_ = -1;
}

void LCD::postOperation(std::function<void()> operation)
{
    if (!operation ||
        !acceptingWork_.load() ||
        !running_.load())
    {
        return;
    }

    boost::asio::post(ioContext_, std::move(operation));
}

void LCD::sendDriverRawOnIoThread(std::string rawData, bool isData)
{
    if (lcdFd_ < 0 || rawData.empty())
    {
        return;
    }

    // ps_par() requires a writable, null-terminated char buffer.
    std::vector<char> buffer(rawData.begin(), rawData.end());
    buffer.push_back('\0');

    ps_par(lcdFd_, buffer.data(), isData);
}

void LCD::sendCommandOnIoThread(std::string_view command)
{
    sendDriverRawOnIoThread(std::string(command), false);
}

void LCD::sendDataOnIoThread(std::string_view data)
{
    const std::string hexData = toHexString(data);

    if (!hexData.empty())
    {
        sendDriverRawOnIoThread(hexData, true);
    }
}

std::string LCD::toHexString(std::string_view rawData)
{
    if (rawData.empty())
    {
        return {};
    }

    std::ostringstream ss;
    ss << "0x" << std::hex << std::setfill('0');

    for (const unsigned char value : rawData)
    {
        ss << std::setw(2) << static_cast<unsigned int>(value);
    }

    return ss.str();
}

bool LCD::isValidRow(std::uint8_t row)
{
    return row >= 1 && row <= MAXIMUM_LCD_LINES;
}

bool LCD::isValidPosition(std::uint8_t row, std::uint8_t col)
{
    return isValidRow(row) &&
           col >= 1 &&
           col <= MAXIMUM_CHARACTER_PER_ROW;
}

void LCD::setCursorOnIoThread(std::uint8_t row, std::uint8_t col)
{
    if (!isValidPosition(row, col))
    {
        return;
    }

    std::uint8_t value = 0;

    switch (row)
    {
        case 1:
            value = static_cast<std::uint8_t>(0x80 + col - 1);
            break;

        case 2:
            value = static_cast<std::uint8_t>(0xC0 + col - 1);
            break;

        case 3:
            value = static_cast<std::uint8_t>(0x94 + col - 1);
            break;

        case 4:
            value = static_cast<std::uint8_t>(0xD4 + col - 1);
            break;

        default:
            return;
    }

    std::ostringstream ss;
    ss << "0x" << std::hex << static_cast<unsigned int>(value);

    sendCommandOnIoThread(ss.str());
}

void LCD::clearDisplayRowOnIoThread(std::uint8_t row)
{
    if (!isValidRow(row))
    {
        return;
    }

    setCursorOnIoThread(row, 1);
    sendDataOnIoThread(std::string(MAXIMUM_CHARACTER_PER_ROW, ' '));
}

void LCD::displayStringOnIoThread(std::uint8_t row, std::uint8_t col, std::string_view text)
{
    if (!isValidPosition(row, col))
    {
        return;
    }

    const std::size_t available = static_cast<std::size_t>(MAXIMUM_CHARACTER_PER_ROW - col + 1);

    const std::size_t length = std::min(text.size(), available);

    setCursorOnIoThread(row, col);
    sendDataOnIoThread(text.substr(0, length));
}

void LCD::displayStringCenteredOnIoThread(std::uint8_t row, std::string_view text)
{
    if (!isValidRow(row))
    {
        return;
    }

    const std::size_t length = std::min<std::size_t>(text.size(), MAXIMUM_CHARACTER_PER_ROW);

    clearDisplayRowOnIoThread(row);

    const std::uint8_t col = static_cast<std::uint8_t>(((MAXIMUM_CHARACTER_PER_ROW - length) / 2) + 1);

    displayStringOnIoThread(row, col, text.substr(0, length));
}

void LCD::displayScreenOnIoThread(std::string_view text)
{
    // Preserve the existing '^' convention for two-line screens.
    const std::size_t separator = text.find('^');

    if (separator != std::string_view::npos)
    {
        displayStringCenteredOnIoThread(1, text.substr(0, separator));

        if (MAXIMUM_LCD_LINES >= 2)
        {
            displayStringCenteredOnIoThread(2, text.substr(separator + 1));
        }

        return;
    }

    // Without '^', split naturally by LCD row width. This also makes the
    // function behave correctly if MAXIMUM_LCD_LINES is changed to 4 later.
    for (int row = 1; row <= MAXIMUM_LCD_LINES; ++row)
    {
        const std::size_t offset = static_cast<std::size_t>(row - 1) * MAXIMUM_CHARACTER_PER_ROW;

        if (offset >= text.size())
        {
            clearDisplayRowOnIoThread(static_cast<std::uint8_t>(row));
            continue;
        }

        displayStringCenteredOnIoThread(static_cast<std::uint8_t>(row), text.substr(offset, MAXIMUM_CHARACTER_PER_ROW));
    }
}

void LCD::FnLCDClear()
{
    postOperation(
        [this]()
        {
            sendCommandOnIoThread("0x01");
        });
}

void LCD::FnLCDHome()
{
    postOperation(
        [this]()
        {
            setCursorOnIoThread(1, 1);
        });
}

void LCD::FnLCDDisplayCharacter(char aChar)
{
    postOperation(
        [this, aChar]()
        {
            const std::string data(1, aChar);
            sendDataOnIoThread(data);
        });
}

void LCD::FnLCDDisplayString(std::uint8_t row, std::uint8_t col, const char* str)
{
    if (str == nullptr)
    {
        return;
    }

    std::string text(str);

    postOperation(
        [this, row, col, text = std::move(text)]()
        {
            displayStringOnIoThread(row, col, text);
        });
}

void LCD::FnLCDDisplayStringCentered(std::uint8_t row, const char* str)
{
    if (str == nullptr)
    {
        return;
    }

    std::string text(str);

    postOperation(
        [this, row, text = std::move(text)]()
        {
            displayStringCenteredOnIoThread(row, text);
        });
}

void LCD::FnLCDClearDisplayRow(std::uint8_t row)
{
    postOperation(
        [this, row]()
        {
            clearDisplayRowOnIoThread(row);
        });
}

void LCD::FnLCDDisplayRow(std::uint8_t row, const char* str)
{
    if (str == nullptr)
    {
        return;
    }

    std::string text(str);

    postOperation(
        [this, row, text = std::move(text)]()
        {
            displayStringOnIoThread(row, 1, text);
        });
}

void LCD::FnLCDDisplayScreen(const char* str)
{
    if (str == nullptr)
    {
        return;
    }

    std::string text(str);

    postOperation(
        [this, text = std::move(text)]()
        {
            displayScreenOnIoThread(text);
        });
}

void LCD::FnLCDWipeOnLR(const char* str)
{
    if (str == nullptr)
    {
        return;
    }

    std::string text(str);

    postOperation(
        [this, text = std::move(text)]()
        {
            for (int col = 1; col <= MAXIMUM_CHARACTER_PER_ROW; ++col)
            {
                const std::size_t index = static_cast<std::size_t>(col - 1);

                for (int row = 1; row <= MAXIMUM_LCD_LINES; ++row)
                {
                    const std::size_t textIndex = static_cast<std::size_t>(row - 1) * MAXIMUM_CHARACTER_PER_ROW + index;

                    setCursorOnIoThread(static_cast<std::uint8_t>(row), static_cast<std::uint8_t>(col));

                    const char value = textIndex < text.size() ? text[textIndex] : ' ';

                    sendDataOnIoThread(std::string(1, value));
                }
            }
        });
}

void LCD::FnLCDWipeOnRL(const char* str)
{
    if (str == nullptr)
    {
        return;
    }

    std::string text(str);

    postOperation(
        [this, text = std::move(text)]()
        {
            for (int col = MAXIMUM_CHARACTER_PER_ROW; col >= 1; --col)
            {
                const std::size_t index = static_cast<std::size_t>(col - 1);

                for (int row = 1; row <= MAXIMUM_LCD_LINES; ++row)
                {
                    const std::size_t textIndex = static_cast<std::size_t>(row - 1) * MAXIMUM_CHARACTER_PER_ROW + index;

                    setCursorOnIoThread(static_cast<std::uint8_t>(row), static_cast<std::uint8_t>(col));

                    const char value = textIndex < text.size() ? text[textIndex] : ' ';

                    sendDataOnIoThread(std::string(1, value));
                }
            }
        });
}

void LCD::FnLCDWipeOffLR()
{
    postOperation(
        [this]()
        {
            for (int col = 1; col <= MAXIMUM_CHARACTER_PER_ROW; ++col)
            {
                for (int row = 1; row <= MAXIMUM_LCD_LINES; ++row)
                {
                    setCursorOnIoThread(static_cast<std::uint8_t>(row), static_cast<std::uint8_t>(col));

                    sendDataOnIoThread(std::string(1, BLOCK));
                }
            }
        });
}

void LCD::FnLCDWipeOffRL()
{
    postOperation(
        [this]()
        {
            for (int col = MAXIMUM_CHARACTER_PER_ROW; col >= 1; --col)
            {
                for (int row = 1; row <= MAXIMUM_LCD_LINES; ++row)
                {
                    setCursorOnIoThread(static_cast<std::uint8_t>(row), static_cast<std::uint8_t>(col));

                    sendDataOnIoThread(std::string(1, BLOCK));
                }
            }
        });
}

void LCD::FnLCDCursorLeft()
{
    postOperation(
        [this]()
        {
            sendCommandOnIoThread("0x10");
        });
}

void LCD::FnLCDCursorRight()
{
    postOperation(
        [this]()
        {
            sendCommandOnIoThread("0x14");
        });
}

void LCD::FnLCDCursorOn()
{
    postOperation(
        [this]()
        {
            sendCommandOnIoThread("0x0D");
        });
}

void LCD::FnLCDCursorOff()
{
    postOperation(
        [this]()
        {
            sendCommandOnIoThread("0x0C");
        });
}

void LCD::FnLCDDisplayOff()
{
    postOperation(
        [this]()
        {
            sendCommandOnIoThread("0x08");
        });
}

void LCD::FnLCDDisplayOn()
{
    postOperation(
        [this]()
        {
            sendCommandOnIoThread("0x0C");
        });
}

void LCD::FnLCDCursorReset()
{
    postOperation(
        [this]()
        {
            sendCommandOnIoThread("0x02");
        });
}

void LCD::FnLCDCursor(std::uint8_t row, std::uint8_t col)
{
    postOperation(
        [this, row, col]()
        {
            setCursorOnIoThread(row, col);
        });
}
