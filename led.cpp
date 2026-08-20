#include "led.h"

#include <algorithm>
#include <sstream>
#include <utility>

#if defined(__linux__)
#include <pthread.h>
#endif

#include <boost/asio/write.hpp>

#include "log.h"
#include "operation.h"

LED::LED(unsigned int baudRate, const std::string& comPortName, int maxCharacterPerRow)
    : ioContext_(),
      serialPort_(ioContext_),
      baudRate_(baudRate),
      comPortName_(comPortName),
      maxCharPerRow_(maxCharacterPerRow),
      logFileName_("led"),
      ledType_("LED")
{
    if (maxCharPerRow_ == LED614_MAX_CHAR_PER_ROW)
    {
        ledType_ = "LED 614";
        logFileName_ = "led614";
    }
    else if (maxCharPerRow_ == LED216_MAX_CHAR_PER_ROW)
    {
        ledType_ = "LED 216";
        logFileName_ = "led216";
    }
    else if (maxCharPerRow_ == LED226_MAX_CHAR_PER_ROW)
    {
        ledType_ = "LED 226";
        logFileName_ = "led226";
    }

    Logger::getInstance()->FnCreateLogFile(logFileName_);

    // Preserve the original class behaviour: constructing an LED also
    // initializes its serial connection.
    FnLEDInit();
}

LED::~LED()
{
    FnLEDClose();

    // Emergency fallback only. Normal shutdown should let run() return
    // naturally after cancellation and work-guard removal.
    if (ioThread_.joinable())
    {
        ioContext_.stop();
        ioThread_.join();
    }
}

bool LED::FnLEDInit()
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);

    if (initialized_.load())
    {
        return true;
    }

    if (!isSupportedDisplayWidth())
    {
        std::ostringstream ss;
        ss << __func__ << " | Unsupported max characters per row: " << maxCharPerRow_;
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return false;
    }

    // Allows the same LED object to be started again after a clean close.
    ioContext_.restart();

    if (!workGuard_)
    {
        workGuard_.emplace(boost::asio::make_work_guard(ioContext_));
    }

    stopRequested_ = false;
    writeInProgress_ = false;
    writeQueue_.clear();

    if (!openSerialPort())
    {
        acceptingWork_.store(false);
        initialized_.store(false);
        workGuard_.reset();
        return false;
    }

    acceptingWork_.store(true);
    initialized_.store(true);

    startIoContextThread();

    std::ostringstream ss;
    ss << "Successfully open serial port: " << comPortName_;
    log(ss.str());

    // Preserve original startup behaviour: clear/default the LED display.
    FnLEDSendLEDMsg("***", "", Alignment::LEFT);

    Logger::getInstance()->FnLog(ledType_ + " initialization completed.");
    log(ledType_ + " initialization completed.");

    return true;
}

void LED::FnLEDClose()
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);

    if (!running_.load())
    {
        boost::system::error_code ec;
        if (serialPort_.is_open())
        {
            serialPort_.cancel(ec);
            serialPort_.close(ec);
        }

        acceptingWork_.store(false);
        initialized_.store(false);
        workGuard_.reset();
        return;
    }

    // Lifecycle APIs are expected to be called from outside the LED io thread.
    if (std::this_thread::get_id() == ioThread_.get_id())
    {
        Logger::getInstance()->FnLogExceptionError(std::string(__func__) + " | Cannot synchronously close LED from its own io thread");
        return;
    }

    acceptingWork_.store(false);

    boost::asio::post(
        ioContext_,
        [this]()
        {
            requestStopOnIoThread();
        });

    // Do not use ioContext_.stop() for normal shutdown. The posted shutdown
    // handler cancels the serial operation; once cancellation handlers drain
    // and the work guard is removed, run() returns naturally.
    workGuard_.reset();

    if (ioThread_.joinable())
    {
        ioThread_.join();
    }

    initialized_.store(false);
}

void LED::startIoContextThread()
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
            ::pthread_setname_np(::pthread_self(), "LED_IO");
#endif
            runIoContext();
        });
}

void LED::runIoContext()
{
    try
    {
        ioContext_.run();
    }
    catch (const std::exception& e)
    {
        std::ostringstream ss;
        ss << __func__ << " | Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        Logger::getInstance()->FnLogExceptionError(std::string(__func__) + " | Unknown exception");
    }

    acceptingWork_.store(false);
    running_.store(false);
}

bool LED::openSerialPort()
{
    try
    {
        boost::system::error_code ec;
        if (serialPort_.is_open())
        {
            serialPort_.cancel(ec);
            serialPort_.close(ec);
        }

        serialPort_.open(comPortName_);
        serialPort_.set_option(boost::asio::serial_port_base::baud_rate(baudRate_));
        serialPort_.set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
        serialPort_.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
        serialPort_.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
        serialPort_.set_option(boost::asio::serial_port_base::character_size(8));

        return serialPort_.is_open();
    }
    catch (const boost::system::system_error& e)
    {
        std::ostringstream ss;
        ss << __func__ << " | Boost.Asio exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (const std::exception& e)
    {
        std::ostringstream ss;
        ss << __func__ << " | Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        Logger::getInstance()->FnLogExceptionError(std::string(__func__) + " | Unknown exception");
    }

    boost::system::error_code ec;
    if (serialPort_.is_open())
    {
        serialPort_.close(ec);
    }

    return false;
}

void LED::requestStopOnIoThread()
{
    stopRequested_ = true;

    boost::system::error_code ec;
    if (serialPort_.is_open())
    {
        serialPort_.cancel(ec);
        serialPort_.close(ec);
    }

    // If no write is active, nothing owns these queued frames anymore.
    // If a write is active, its completion handler will clear the queue.
    if (!writeInProgress_)
    {
        writeQueue_.clear();
    }
}

unsigned int LED::FnGetLEDBaudRate() const
{
    return baudRate_;
}

std::string LED::FnGetLEDComPortName() const
{
    return comPortName_;
}

int LED::FnGetLEDMaxCharPerRow() const
{
    return maxCharPerRow_;
}

bool LED::FnIsLEDInitialized() const
{
    return initialized_.load();
}

void LED::FnLEDSendLEDMsg(const std::string& ledId, const std::string& text, Alignment align)
{
    if (!acceptingWork_.load())
    {
        return;
    }

    // ledId and text are copied into the handler before this function returns,
    // so caller-owned buffers/lifetimes are irrelevant after post().
    boost::asio::post(
        ioContext_,
        [this, ledId, text, align]()
        {
            handleSendMessageOnIoThread(ledId, text, align);
        });
}

void LED::handleSendMessageOnIoThread(const std::string& ledId, const std::string& text, Alignment align)
{
    if (stopRequested_ || !serialPort_.is_open())
    {
        return;
    }

    try
    {
        const std::string actualLedId = ledId.empty() ? "***" : ledId;

        if (actualLedId.size() != 3)
        {
            std::ostringstream ss;
            ss << __func__ << " | Invalid LED ID: " << actualLedId;
            log(ss.str());
            return;
        }

        if (maxCharPerRow_ == LED216_MAX_CHAR_PER_ROW ||
            maxCharPerRow_ == LED226_MAX_CHAR_PER_ROW)
        {
            std::string line1Text;
            std::string line2Text;

            const std::size_t separator = text.find('^');
            if (separator != std::string::npos)
            {
                line1Text = text.substr(0, separator);
                line2Text = text.substr(separator + 1);
            }
            else
            {
                line1Text = text;
            }

            // Queue the two frames. Only one async_write is active at a time,
            // so line 2 cannot overlap line 1 on the serial port.
            enqueueWriteOnIoThread(formatDisplayMsg(actualLedId, Line::FIRST, line1Text, align));

            enqueueWriteOnIoThread(formatDisplayMsg(actualLedId, Line::SECOND, line2Text, align));

            if (operation::getInstance()->FnIsOperationInitialized())
            {
                operation::getInstance()->FnSendLEDMessageToMonitor(line1Text, line2Text);
            }
        }
        else if (maxCharPerRow_ == LED614_MAX_CHAR_PER_ROW)
        {
            enqueueWriteOnIoThread(formatDisplayMsg(actualLedId, Line::FIRST, text, align));
        }
    }
    catch (const std::exception& e)
    {
        std::ostringstream ss;
        ss << __func__ << " | text=" << text << " | Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        std::ostringstream ss;
        ss << __func__ << " | text=" << text << " | Unknown exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
}

std::vector<char> LED::formatDisplayMsg(const std::string& ledId,
                                        Line lineNo,
                                        const std::string& text,
                                        Alignment align) const
{
    std::string displayText = text;

    if (displayText.size() > static_cast<std::size_t>(maxCharPerRow_))
    {
        displayText.resize(static_cast<std::size_t>(maxCharPerRow_));
    }

    // LED614 has one additional padded character.
    const std::size_t payloadWidth =
        static_cast<std::size_t>(
            maxCharPerRow_ == LED614_MAX_CHAR_PER_ROW
                ? maxCharPerRow_ + 1
                : maxCharPerRow_);

    std::string formattedText(payloadWidth, ' ');

    std::size_t replaceIndex = 0;

    switch (align)
    {
        case Alignment::LEFT:
        {
            replaceIndex = 0;
            break;
        }

        case Alignment::RIGHT:
        {
            replaceIndex = static_cast<std::size_t>(maxCharPerRow_) - displayText.size();
            break;
        }

        case Alignment::CENTER:
        {
            if (maxCharPerRow_ != LED614_MAX_CHAR_PER_ROW)
            {
                replaceIndex = (static_cast<std::size_t>(maxCharPerRow_) - displayText.size()) / 2;
            }

            break;
        }
    }

    formattedText.replace(replaceIndex, displayText.size(), displayText);

    std::vector<char> result;

    /*
     * Complete frame:
     *
     * STX STX
     * LED ID
     * ^
     * LINE
     * DISPLAY DATA
     * CR LF
     */
    const std::size_t frameSize =
        2 +                 // STX STX
        ledId.size() +
        1 +                 // ^
        1 +                 // line number
        payloadWidth +
        2;                  // CR LF

    result.reserve(frameSize);

    // Header
    result.push_back(STX1);
    result.push_back(STX1);

    result.insert(result.end(), ledId.begin(), ledId.end());

    result.push_back('^');

    switch (lineNo)
    {
        case Line::FIRST:
            result.push_back('1');
            break;

        case Line::SECOND:
            result.push_back('2');
            break;
    }

    // Payload
    result.insert(result.end(), formattedText.begin(), formattedText.end());

    // Tail
    result.push_back(ETX1);
    result.push_back(ETX2);

    return result;
}

void LED::enqueueWriteOnIoThread(std::vector<char> data)
{
    if (stopRequested_ || data.empty())
    {
        return;
    }

    writeQueue_.push_back(std::move(data));

    if (!writeInProgress_)
    {
        startNextWriteOnIoThread();
    }
}

void LED::startNextWriteOnIoThread()
{
    if (stopRequested_)
    {
        writeQueue_.clear();
        writeInProgress_ = false;
        return;
    }

    if (writeQueue_.empty())
    {
        writeInProgress_ = false;
        return;
    }

    if (!serialPort_.is_open())
    {
        log("Serial port is closed; dropping queued LED message.");
        writeQueue_.clear();
        writeInProgress_ = false;
        return;
    }

    writeInProgress_ = true;

    const auto& data = writeQueue_.front();

    boost::asio::async_write(
        serialPort_,
        boost::asio::buffer(data.data(), data.size()),
        [this](const boost::system::error_code& ec,
               std::size_t bytesTransferred)
        {
            handleWriteComplete(ec, bytesTransferred);
        });
}

void LED::handleWriteComplete(const boost::system::error_code& ec,
                              std::size_t bytesTransferred)
{
    if (!writeQueue_.empty())
    {
        writeQueue_.pop_front();
    }

    writeInProgress_ = false;

    if (ec)
    {
        if (ec != boost::asio::error::operation_aborted)
        {
            std::ostringstream ss;
            ss << __func__ << " | Failed to write LED message" << " | bytesTransferred=" << bytesTransferred << " | error=" << ec.message();
            Logger::getInstance()->FnLogExceptionError(ss.str());
        }

        if (stopRequested_)
        {
            writeQueue_.clear();
            return;
        }
    }

    if (stopRequested_)
    {
        writeQueue_.clear();
        return;
    }

    startNextWriteOnIoThread();
}

bool LED::isSupportedDisplayWidth() const
{
    return maxCharPerRow_ == LED614_MAX_CHAR_PER_ROW ||
           maxCharPerRow_ == LED216_MAX_CHAR_PER_ROW ||
           maxCharPerRow_ == LED226_MAX_CHAR_PER_ROW;
}

void LED::log(const std::string& message) const
{
    Logger::getInstance()->FnLog(message, logFileName_, "LED");
}


// LED Manager
LEDManager* LEDManager::getInstance()
{
    static LEDManager instance;
    return &instance;
}

void LEDManager::createLED(unsigned int baudRate,
                           const std::string& comPortName,
                           int maxCharacterPerRow)
{
    try
    {
        auto led = std::make_unique<LED>(baudRate, comPortName, maxCharacterPerRow);

        std::lock_guard<std::mutex> lock(ledsMutex_);
        leds_.push_back(std::move(led));
    }
    catch (const std::exception& e)
    {
        std::ostringstream ss;
        ss << __func__ << " | baudRate=" << baudRate << " | comPortName=" << comPortName << " | Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        std::ostringstream ss;
        ss << __func__ << " | baudRate=" << baudRate << " | comPortName=" << comPortName << " | Unknown exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
}

LED* LEDManager::getLED(const std::string& ledComPort)
{
    std::lock_guard<std::mutex> lock(ledsMutex_);

    for (auto& led : leds_)
    {
        if (led != nullptr &&
            led->FnGetLEDComPortName() == ledComPort)
        {
            return led.get();
        }
    }

    return nullptr;
}
