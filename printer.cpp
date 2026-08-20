#include "printer.h"

#include "event_manager.h"
#include "log.h"

#include <algorithm>
#include <future>
#include <iomanip>
#include <sstream>
#include <utility>

#if defined(__linux__)
#include <pthread.h>
#endif

Printer::Printer()
    : serialPort_(ioContext_),
      selfTestTimer_(ioContext_),
      monitorStatusTimer_(ioContext_)
{
    cbmFonts_[1]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x00'});
    cbmFonts_[2]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x08'});
    cbmFonts_[3]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x10'});
    cbmFonts_[4]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x20'});
    cbmFonts_[5]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x30'});
    cbmFonts_[6]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x18'});
    cbmFonts_[7]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x28'});
    cbmFonts_[8]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x38'});
    cbmFonts_[9]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x01'});
    cbmFonts_[10] = std::string({ASCII::ESC, ASCII::EXCLAM, '\x09'});
    cbmFonts_[11] = std::string({ASCII::ESC, ASCII::EXCLAM, '\x11'});
    cbmFonts_[12] = std::string({ASCII::ESC, ASCII::EXCLAM, '\x21'});
    cbmFonts_[13] = std::string({ASCII::ESC, ASCII::EXCLAM, '\x31'});
    cbmFonts_[14] = std::string({ASCII::ESC, ASCII::EXCLAM, '\x19'});
    cbmFonts_[15] = std::string({ASCII::ESC, ASCII::EXCLAM, '\x29'});
    cbmFonts_[16] = std::string({ASCII::ESC, ASCII::EXCLAM, '\x39'});

    ftpFonts_[1]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x00'});
    ftpFonts_[2]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x01'});
    ftpFonts_[3]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x02'});
    ftpFonts_[4]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x03'});
    ftpFonts_[5]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x12'});
    ftpFonts_[6]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x13'});
    ftpFonts_[7]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x20'});
    ftpFonts_[8]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x21'});
    ftpFonts_[9]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x22'});
    ftpFonts_[10] = std::string({ASCII::ESC, ASCII::EXCLAM, '\x23'});
    ftpFonts_[11] = std::string({ASCII::ESC, ASCII::EXCLAM, '\x32'});
    ftpFonts_[12] = std::string({ASCII::ESC, ASCII::EXCLAM, '\x33'});

    alignCommands_[0] = "";
    alignCommands_[1] = std::string({ASCII::ESC, ASCII::a, '\x00'});
    alignCommands_[2] = std::string({ASCII::ESC, ASCII::a, '\x01'});
    alignCommands_[3] = std::string({ASCII::ESC, ASCII::a, '\x02'});
}

Printer::~Printer()
{
    acceptingWork_.store(false);
    stopping_.store(true);

    try
    {
        if (ioThread_.joinable())
        {
            if (running_.load())
            {
                boost::asio::post(
                    ioContext_,
                    [this]()
                    {
                        shutdownOnIoThread();
                    });
            }

            workGuard_.reset();

            if (std::this_thread::get_id() != ioThread_.get_id())
            {
                ioThread_.join();
            }
            else
            {
                // Emergency fallback only. Normal shutdown must call
                // FnPrinterClose() from outside the Printer I/O thread.
                ioContext_.stop();
            }
        }
    }
    catch (...)
    {
        ioContext_.stop();
    }
}

Printer* Printer::getInstance()
{
    static Printer instance;
    return &instance;
}

bool Printer::FnPrinterInit(unsigned int baudRate, const std::string& comPortName)
{
    Logger::getInstance()->FnCreateLogFile(logFileName_);

    std::unique_lock<std::mutex> lock(lifecycleMutex_);

    if (initialized_.load())
    {
        log("[INIT] Already initialized");
        return true;
    }

    if (ioThread_.joinable() && !running_.load())
    {
        ioThread_.join();
    }

    if (ioThread_.joinable())
    {
        log("[INIT] Failed | I/O thread is already running");
        return false;
    }

    log("[INIT] Starting | Port=" + comPortName + " | Baud=" + std::to_string(baudRate));

    acceptingWork_.store(false);
    stopping_.store(false);
    initialized_.store(false);

    if (!startIoThreadLocked())
    {
        EventManager::getInstance()->FnEnqueueEvent("Evt_handlePrinterStatus", static_cast<int>(PRINTER_STATUS::ERROR));

        return false;
    }

    auto initPromise = std::make_shared<std::promise<bool>>();
    std::future<bool> initFuture = initPromise->get_future();

    boost::asio::post(
        ioContext_,
        [this,
         baudRate,
         comPortName,
         initPromise]()
        {
            bool success = false;

            try
            {
                success = initOnIoThread(baudRate, comPortName);
            }
            catch (const std::exception& exception)
            {
                logException(__func__, exception);
                success = false;
            }
            catch (...)
            {
                Logger::getInstance()->FnLogExceptionError("Printer::FnPrinterInit task | Unknown exception");
                success = false;
            }

            initPromise->set_value(success);
        });

    const bool success = initFuture.get();

    if (!success)
    {
        acceptingWork_.store(false);
        stopping_.store(true);

        boost::asio::post(
            ioContext_,
            [this]()
            {
                shutdownOnIoThread();
            });

        workGuard_.reset();

        if (ioThread_.joinable())
        {
            ioThread_.join();
        }

        running_.store(false);
        initialized_.store(false);
        stopping_.store(false);

        log("[INIT] Failed");

        EventManager::getInstance()->FnEnqueueEvent("Evt_handlePrinterStatus", static_cast<int>(PRINTER_STATUS::ERROR));

        return false;
    }

    initialized_.store(true);
    acceptingWork_.store(true);

    log(std::string("[INIT] Completed | Type=") + printerTypeName(printerType_.load()));

    EventManager::getInstance()->FnEnqueueEvent("Evt_handlePrinterStatus", static_cast<int>(PRINTER_STATUS::IDLE));

    return true;
}

void Printer::FnPrinterClose()
{
    std::unique_lock<std::mutex> lock(lifecycleMutex_);

    if (!ioThread_.joinable())
    {
        acceptingWork_.store(false);
        initialized_.store(false);
        running_.store(false);
        return;
    }

    log("[SHUTDOWN] Starting");

    acceptingWork_.store(false);
    stopping_.store(true);

    if (std::this_thread::get_id() == ioThread_.get_id())
    {
        shutdownOnIoThread();
        workGuard_.reset();

        log( "[SHUTDOWN] Requested from I/O thread | thread will exit after pending work drains");
        return;
    }

    boost::asio::post(
        ioContext_,
        [this]()
        {
            shutdownOnIoThread();
        });

    workGuard_.reset();

    ioThread_.join();

    running_.store(false);
    initialized_.store(false);
    stopping_.store(false);

    log("[SHUTDOWN] Completed");
}

void Printer::FnSetPrintMode(int mode)
{
    printMode_.store(mode);
}

int Printer::FnGetPrintMode() const
{
    return printMode_.load();
}

void Printer::FnSetDefaultAlign(Printer::CBM_ALIGN align)
{
    const int value = static_cast<int>(align);

    if (value < static_cast<int>(CBM_ALIGN::CBM_LEFT) ||
        value > static_cast<int>(CBM_ALIGN::CBM_RIGHT))
    {
        return;
    }

    defaultAlign_.store(value);
}

Printer::CBM_ALIGN Printer::FnGetDefaultAlign() const
{
    return static_cast<CBM_ALIGN>(defaultAlign_.load());
}

void Printer::FnSetDefaultFont(int font)
{
    defaultFont_.store(font);
}

int Printer::FnGetDefaultFont() const
{
    return defaultFont_.load();
}

void Printer::FnSetLeftMargin(int leftMargin)
{
    int normalizedMargin = std::max(0, leftMargin);

    if (normalizedMargin > 10)
    {
        normalizedMargin /= 10;
    }

    leftMargin_.store(normalizedMargin);

    if (!acceptingWork_.load())
    {
        return;
    }

    boost::asio::post(
        ioContext_,
        [this]()
        {
            if (!stopping_.load())
            {
                configurePrinterCommandsOnIoThread(false);
            }
        });
}

int Printer::FnGetLeftMargin() const
{
    return leftMargin_.load();
}

void Printer::FnSetLineSpace(int space)
{
    lineSpace_.store(space);
}

int Printer::FnGetLineSpace() const
{
    return lineSpace_.load();
}

void Printer::FnSetSelfTestInterval(int interval)
{
    selfTestInterval_.store(std::max(0, interval));
}

int Printer::FnGetSelfTestInterval() const
{
    return selfTestInterval_.load();
}

void Printer::FnSetSiteID(int id)
{
    siteID_.store(id);
}

int Printer::FnGetSiteID() const
{
    return siteID_.load();
}

void Printer::FnSetPrinterType(Printer::PRINTER_TYPE type)
{
    switch (type)
    {
        case PRINTER_TYPE::CBM:
        case PRINTER_TYPE::FTP:
        case PRINTER_TYPE::CBM1000:
            break;

        default:
            return;
    }

    printerType_.store(type);

    if (!acceptingWork_.load())
    {
        return;
    }

    boost::asio::post(
        ioContext_,
        [this]()
        {
            if (!stopping_.load())
            {
                configurePrinterCommandsOnIoThread(false);
            }
        });
}

Printer::PRINTER_TYPE Printer::FnGetPrinterType() const
{
    return printerType_.load();
}

void Printer::FnPrintLine(const std::string& text, int font, int align, bool underline, int font2)
{
    if (!acceptingWork_.load())
    {
        log("[PRINT] Ignored | Printer is not initialized");
        return;
    }

    boost::asio::post(
        ioContext_,
        [this, text, font, align, underline, font2]()
        {
            if (stopping_.load() || !initialized_.load())
            {
                return;
            }

            printLineOnIoThread(text, font, align, underline, font2);
        });
}

void Printer::FnFullCut(int bottom)
{
    if (!acceptingWork_.load())
    {
        log("[CUT] Ignored | Printer is not initialized");
        return;
    }

    boost::asio::post(
        ioContext_,
        [this, bottom]()
        {
            if (stopping_.load() || !initialized_.load())
            {
                return;
            }

            fullCutOnIoThread(bottom);
        });
}

void Printer::FnGetAllFonts()
{
    if (!acceptingWork_.load())
    {
        log("[PRINT] GetAllFonts ignored | Printer is not initialized");
        return;
    }

    boost::asio::post(
        ioContext_,
        [this]()
        {
            if (stopping_.load() || !initialized_.load())
            {
                return;
            }

            getAllFontsOnIoThread();
        });
}

void Printer::FnPrintBarCode(const std::string& text, int height, int width, int fontSetting)
{
    if (!acceptingWork_.load())
    {
        log("[BARCODE] Ignored | Printer is not initialized");
        return;
    }

    boost::asio::post(
        ioContext_,
        [this, text, height, width, fontSetting]()
        {
            if (stopping_.load() || !initialized_.load())
            {
                return;
            }

            printBarcodeOnIoThread(text, height, width, fontSetting);
        });
}

void Printer::FnFeedLine(int line)
{
    if (!acceptingWork_.load())
    {
        log("[FEED] Ignored | Printer is not initialized");
        return;
    }

    boost::asio::post(
        ioContext_,
        [this, line]()
        {
            if (stopping_.load() || !initialized_.load())
            {
                return;
            }

            feedLineOnIoThread(line);
        });
}

bool Printer::startIoThreadLocked()
{
    try
    {
        ioContext_.restart();
        workGuard_.emplace(boost::asio::make_work_guard(ioContext_));

        running_.store(true);

        ioThread_ = std::thread(
            [this]()
            {
#if defined(__linux__)
                ::pthread_setname_np(::pthread_self(), "PRINTER_IO");
#endif
                log("[THREAD] io_context started");

                try
                {
                    ioContext_.run();
                }
                catch (const std::exception& exception)
                {
                    logException("io_context::run", exception);
                }
                catch (...)
                {
                    Logger::getInstance()->FnLogExceptionError("Printer::io_context::run | Unknown exception");
                }

                running_.store(false);
                log("[THREAD] io_context stopped");
            });

        return true;
    }
    catch (const std::exception& exception)
    {
        running_.store(false);
        workGuard_.reset();
        logException(__func__, exception);
        return false;
    }
}

bool Printer::initOnIoThread(unsigned int baudRate, const std::string& comPortName)
{
    boost::system::error_code error;

    if (serialPort_.is_open())
    {
        serialPort_.cancel(error);
        error.clear();
        serialPort_.close(error);
        error.clear();
    }

    serialPort_.open(comPortName, error);

    if (error)
    {
        log(
            "[INIT] Serial open failed | Port=" +
            comPortName +
            " | Error=" +
            error.message());
        return false;
    }

    const auto setOption =
        [this, &error](const auto& option) -> bool
        {
            error.clear();
            serialPort_.set_option(option, error);
            return !error;
        };

    if (!setOption(boost::asio::serial_port_base::baud_rate(baudRate)) ||
        !setOption(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none)) ||
        !setOption(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none)) ||
        !setOption(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one)) ||
        !setOption(boost::asio::serial_port_base::character_size(8)))
    {
        log("[INIT] Serial configuration failed | Error=" + error.message());

        serialPort_.close(error);
        return false;
    }

    writeQueue_.clear();
    writeInProgress_ = false;
    currentStatus_ = PRINTER_STATUS::IDLE;
    lastAlign_ = defaultAlign_.load();

    configurePrinterCommandsOnIoThread(true);
    startReadOnIoThread();

    if (printerType_.load() != PRINTER_TYPE::FTP)
    {
        startSelfTestTimerOnIoThread(selfTestInterval_.load());
    }

    startMonitorStatusTimerOnIoThread();

    return true;
}

void Printer::shutdownOnIoThread()
{
    boost::system::error_code ignored;

    selfTestTimer_.cancel(ignored);
    monitorStatusTimer_.cancel(ignored);

    if (serialPort_.is_open())
    {
        serialPort_.cancel(ignored);
        serialPort_.close(ignored);
    }

    writeQueue_.clear();
    writeInProgress_ = false;

    initialized_.store(false);
    acceptingWork_.store(false);
}

void Printer::configurePrinterCommandsOnIoThread(bool sendFtpSetup)
{
    cmdLeftMargin_.clear();
    cmdCut_.clear();
    activeFonts_.fill({});

    const int leftMargin = leftMargin_.load();
    const PRINTER_TYPE type = printerType_.load();

    switch (type)
    {
        case PRINTER_TYPE::CBM:
        {
            const int n1 = leftMargin % 256;
            const int n2 = leftMargin / 256;

            std::ostringstream margin;
            margin
                << ASCII::ESC
                << '$'
                << static_cast<char>(n1)
                << static_cast<char>(n2);

            cmdLeftMargin_ = margin.str();
            cmdCut_ = std::string({ASCII::ESC, 'i'});
            activeFonts_ = cbmFonts_;
            break;
        }

        case PRINTER_TYPE::FTP:
        {
            std::ostringstream margin;

            // Preserved from the legacy protocol implementation.
            // Verify with the FTP printer protocol whether the literal "to"
            // is intentional before changing it.
            margin
                << ASCII::ESC
                << 'D'
                << static_cast<char>(leftMargin)
                << "to"
                << static_cast<char>(1)
                << static_cast<char>(0)
                << ASCII::TAB;

            cmdLeftMargin_ = margin.str();

            std::ostringstream cut;
            cut
                << ASCII::GS
                << 'V'
                << static_cast<char>(0);
            cmdCut_ = cut.str();

            for (std::size_t index = 0; index < ftpFonts_.size(); ++index)
            {
                activeFonts_[index] = ftpFonts_[index];
            }

            if (sendFtpSetup)
            {
                std::ostringstream detection;
                detection
                    << ASCII::FS
                    << '9'
                    << static_cast<char>(111);

                std::ostringstream statusTransmission;
                statusTransmission
                    << ASCII::GS
                    << 'a'
                    << static_cast<char>(14);

                std::ostringstream lineSpace;
                lineSpace
                    << ASCII::ESC
                    << 'A'
                    << static_cast<char>(lineSpace_.load());

                enqueueWriteOnIoThread(toBytes(detection.str()));
                enqueueWriteOnIoThread(toBytes(statusTransmission.str()));
                enqueueWriteOnIoThread(toBytes(lineSpace.str()));
            }

            break;
        }

        case PRINTER_TYPE::CBM1000:
        {
            std::ostringstream margin;
            margin
                << ASCII::ESC
                << 'D'
                << static_cast<char>(leftMargin)
                << static_cast<char>(1)
                << static_cast<char>(0)
                << ASCII::TAB;

            cmdLeftMargin_ = margin.str();

            std::ostringstream cut;
            cut
                << ASCII::GS
                << 'V'
                << static_cast<char>(1);
            cmdCut_ = cut.str();

            activeFonts_ = cbmFonts_;
            break;
        }
    }
}

void Printer::startReadOnIoThread()
{
    if (stopping_.load() || !serialPort_.is_open())
    {
        return;
    }

    serialPort_.async_read_some(
        boost::asio::buffer(readBuffer_),
        [this](
            const boost::system::error_code& error,
            std::size_t bytesTransferred)
        {
            handleReadOnIoThread(error, bytesTransferred);
        });
}

void Printer::handleReadOnIoThread(const boost::system::error_code& error, std::size_t bytesTransferred)
{
    if (error)
    {
        if (error == boost::asio::error::operation_aborted ||
            stopping_.load())
        {
            return;
        }

        log("[RX] Failed | Error=" + error.message());

        publishStatusOnIoThread(PRINTER_STATUS::ERROR);

        return;
    }

    std::vector<std::uint8_t> data(readBuffer_.begin(), readBuffer_.begin() + static_cast<std::ptrdiff_t>(bytesTransferred));

    log("[RX] Bytes=" + std::to_string(bytesTransferred) + " | Data=" + toHex(data));

    handleCmdResponseOnIoThread(data);

    startReadOnIoThread();
}

void Printer::enqueueWriteOnIoThread(std::vector<std::uint8_t> data)
{
    if (stopping_.load() || !serialPort_.is_open())
    {
        return;
    }

    if (data.empty())
    {
        return;
    }

    writeQueue_.push_back(std::move(data));

    if (!writeInProgress_)
    {
        startWriteOnIoThread();
    }
}

void Printer::startWriteOnIoThread()
{
    if (stopping_.load() ||
        writeInProgress_ ||
        writeQueue_.empty() ||
        !serialPort_.is_open())
    {
        return;
    }

    writeInProgress_ = true;

    const auto& data = writeQueue_.front();

    log("[TX] Bytes=" + std::to_string(data.size()) + " | Data=" + toHex(data));

    boost::asio::async_write(
        serialPort_,
        boost::asio::buffer(data),
        [this](
            const boost::system::error_code& error,
            std::size_t bytesTransferred)
        {
            handleWriteOnIoThread(error, bytesTransferred);
        });
}

void Printer::handleWriteOnIoThread(const boost::system::error_code& error, std::size_t bytesTransferred)
{
    if (error)
    {
        if (error == boost::asio::error::operation_aborted ||
            stopping_.load())
        {
            return;
        }

        log("[TX] Failed | Error=" + error.message());

        publishStatusOnIoThread(PRINTER_STATUS::ERROR);
    }
    else
    {
        log("[TX] Completed | Bytes=" + std::to_string(bytesTransferred));
    }

    if (!writeQueue_.empty())
    {
        writeQueue_.pop_front();
    }

    writeInProgress_ = false;

    if (!writeQueue_.empty() &&
        !stopping_.load())
    {
        startWriteOnIoThread();
    }
}

void Printer::handleCmdResponseOnIoThread(const std::vector<std::uint8_t>& response)
{
    if (response.empty())
    {
        return;
    }

    if (printerType_.load() != PRINTER_TYPE::FTP)
    {
        boost::system::error_code ignored;
        selfTestTimer_.cancel(ignored);

        const std::uint8_t status = response.front();

        if (static_cast<char>(status) == ASCII::NUL)
        {
            if (currentStatus_ != PRINTER_STATUS::IDLE)
            {
                publishStatusOnIoThread(PRINTER_STATUS::IDLE);
            }

            return;
        }

        if ((status & (1U << 5U)) != 0U)
        {
            publishStatusOnIoThread(PRINTER_STATUS::NO_PAPER);
        }
        else if (((status & (1U << 4U)) != 0U) &&
                 ((status & (1U << 1U)) != 0U))
        {
            publishStatusOnIoThread(PRINTER_STATUS::IDLE);
        }

        return;
    }

    if ((response.size() % 4U) != 0U)
    {
        log("[STATUS] Invalid FTP response length | Bytes=" + std::to_string(response.size()));
        return;
    }

    for (std::size_t offset = 0; offset < response.size(); offset += 4U)
    {
        const std::uint8_t byte0 = response[offset];
        const std::uint8_t byte1 = response[offset + 1U];
        const std::uint8_t byte2 = response[offset + 2U];

        if (byte0 == 8U)
        {
            publishStatusOnIoThread(PRINTER_STATUS::ERROR);
        }
        else if (byte2 == 1U)
        {
            publishStatusOnIoThread(PRINTER_STATUS::NO_PAPER);
        }
        else if (byte1 > 1U)
        {
            publishStatusOnIoThread(PRINTER_STATUS::ERROR);
        }
        else
        {
            publishStatusOnIoThread(PRINTER_STATUS::IDLE);
        }
    }
}

void Printer::startMonitorStatusTimerOnIoThread()
{
    if (stopping_.load())
    {
        return;
    }

    monitorStatusTimer_.expires_after(kMonitorStatusInterval);

    monitorStatusTimer_.async_wait(
        [this](const boost::system::error_code& error)
        {
            handleMonitorStatusTimeoutOnIoThread(error);
        });
}

void Printer::handleMonitorStatusTimeoutOnIoThread(const boost::system::error_code& error)
{
    if (error == boost::asio::error::operation_aborted ||
        stopping_.load())
    {
        return;
    }

    if (error)
    {
        log("[MONITOR] Timer failed | Error=" + error.message());
    }
    else if (currentStatus_ != PRINTER_STATUS::IDLE && printerType_.load() != PRINTER_TYPE::FTP)
    {
        log("[MONITOR] Requesting printer status | Current=" + std::string(statusName(currentStatus_)));

        inquireStatusOnIoThread();
    }

    startMonitorStatusTimerOnIoThread();
}

void Printer::startSelfTestTimerOnIoThread(int milliseconds)
{
    if (stopping_.load() || printerType_.load() == PRINTER_TYPE::FTP)
    {
        return;
    }

    boost::system::error_code ignored;
    selfTestTimer_.cancel(ignored);

    inquireStatusOnIoThread();

    selfTestTimer_.expires_after(std::chrono::milliseconds(std::max(0, milliseconds)));

    selfTestTimer_.async_wait(
        [this](const boost::system::error_code& error)
        {
            handleSelfTestTimeoutOnIoThread(error);
        });
}

void Printer::handleSelfTestTimeoutOnIoThread(const boost::system::error_code& error)
{
    if (error == boost::asio::error::operation_aborted || stopping_.load())
    {
        return;
    }

    if (error)
    {
        log("[SELFTEST] Timer failed | Error=" + error.message());
        return;
    }

    if (currentStatus_ == PRINTER_STATUS::IDLE)
    {
        log("[SELFTEST] Status response timeout");
        publishStatusOnIoThread(PRINTER_STATUS::ERROR);
    }
}

void Printer::inquireStatusOnIoThread()
{
    std::ostringstream command;
    command
        << ASCII::DLE
        << ASCII::EOT
        << ASCII::STX;

    enqueueWriteOnIoThread(toBytes(command.str()));
}

void Printer::publishStatusOnIoThread(PRINTER_STATUS status, bool force)
{
    if (!force && currentStatus_ == status)
    {
        return;
    }

    currentStatus_ = status;

    log("[STATUS] " + std::string(statusName(status)));

    EventManager::getInstance()->FnEnqueueEvent("Evt_handlePrinterStatus", static_cast<int>(status));
}

void Printer::printLineOnIoThread(const std::string& text, int font, int align, bool underline, int font2)
{
    const PRINTER_TYPE type = printerType_.load();

    if (font2 > 0)
    {
        const std::size_t separator = text.find(':');

        if (separator != std::string::npos)
        {
            if (font2 >= static_cast<int>(activeFonts_.size()) ||
                activeFonts_[static_cast<std::size_t>(font2)].empty())
            {
                log("[PRINT] Invalid secondary font | Font=" + std::to_string(font2));
                return;
            }

            if (font == 0)
            {
                font = defaultFont_.load();
            }

            if (font < 0 ||
                font >= static_cast<int>(activeFonts_.size()) ||
                activeFonts_[static_cast<std::size_t>(font)].empty())
            {
                log("[PRINT] Invalid font | Font=" + std::to_string(font));
                return;
            }

            const std::string left = text.substr(0, separator + 1U);
            const std::string right = text.substr(separator + 1U);

            std::ostringstream output;
            output
                << cmdLeftMargin_
                << activeFonts_[static_cast<std::size_t>(font2)]
                << left
                << activeFonts_[static_cast<std::size_t>(font)]
                << right
                << ASCII::LF;

            enqueueWriteOnIoThread(toBytes(output.str()));
            return;
        }
    }

    if (font == 99 && type != PRINTER_TYPE::FTP)
    {
        enqueueWriteOnIoThread(toBytes(std::string({ASCII::ESC, '@'})));
        return;
    }

    if (font == 0)
    {
        font = defaultFont_.load();
    }

    std::string underlineOn;
    std::string underlineOff;

    if (type != PRINTER_TYPE::FTP)
    {
        if (align == 0)
        {
            align = defaultAlign_.load();
        }

        if (align < static_cast<int>(CBM_ALIGN::CBM_LEFT) ||
            align > static_cast<int>(CBM_ALIGN::CBM_RIGHT))
        {
            log("[PRINT] Invalid alignment | Align=" + std::to_string(align));
            return;
        }

        if (underline)
        {
            underlineOn = std::string({ASCII::ESC, '-', '\x01'});
            underlineOff = std::string({ASCII::ESC, '-', '\x00'});
        }
    }
    else
    {
        align = 0;

        if (font > 12)
        {
            font -= 12;
        }
    }

    if (font < 0 ||
        font >= static_cast<int>(activeFonts_.size()) ||
        activeFonts_[static_cast<std::size_t>(font)].empty())
    {
        log("[PRINT] Invalid font | Font=" + std::to_string(font));
        return;
    }

    std::ostringstream output;
    output
        << cmdLeftMargin_
        << alignCommands_[static_cast<std::size_t>(align)]
        << activeFonts_[static_cast<std::size_t>(font)]
        << underlineOn
        << text
        << underlineOff
        << ASCII::LF;

    enqueueWriteOnIoThread(toBytes(output.str()));

    lastAlign_ = align;
}

void Printer::fullCutOnIoThread(int bottom)
{
    bottom = std::clamp(bottom < 4 ? 5 : bottom, 0, 255);

    std::ostringstream output;

    if (printMode_.load() != 1)
    {
        output
            << ASCII::ESC
            << 'd'
            << static_cast<char>(bottom)
            << cmdCut_;
    }
    else
    {
        output
            << ASCII::GS
            << static_cast<char>(0x0C);
    }

    enqueueWriteOnIoThread(toBytes(output.str()));

    if (printerType_.load() != PRINTER_TYPE::FTP)
    {
        startSelfTestTimerOnIoThread(selfTestInterval_.load());
    }
}

void Printer::getAllFontsOnIoThread()
{
    for (std::size_t index = 1; index < activeFonts_.size(); ++index)
    {
        if (activeFonts_[index].empty())
        {
            continue;
        }

        std::ostringstream output;
        output
            << activeFonts_[index]
            << index
            << ", abcdefghijklmnopqrstuvwxyz"
            << ASCII::LF;

        enqueueWriteOnIoThread(toBytes(output.str()));
    }

    fullCutOnIoThread(0);
}

void Printer::printBarcodeOnIoThread(const std::string& text, int height, int width, int fontSetting)
{
    const PRINTER_TYPE type = printerType_.load();

    if (type == PRINTER_TYPE::FTP)
    {
        return;
    }

    height = height == 0
        ? 80
        : std::clamp(height, 1, 255);

    width = width == 0
        ? 3
        : std::clamp(width, 1, 255);

    if (printMode_.load() == 1)
    {
        width = 2;
    }

    if (type == PRINTER_TYPE::CBM1000 && text.size() > 255U)
    {
        log("[BARCODE] Text too long for CBM1000 | Length=" + std::to_string(text.size()));
        return;
    }

    std::ostringstream widthCommand;
    widthCommand
        << ASCII::GS
        << 'w'
        << static_cast<char>(width);

    std::ostringstream heightCommand;
    heightCommand
        << ASCII::GS
        << 'h'
        << static_cast<char>(height);

    std::ostringstream positionCommand;

    if (fontSetting != 0)
    {
        const int position = fontSetting / 10;
        const int font = fontSetting % 10;

        positionCommand
            << ASCII::GS
            << 'H'
            << static_cast<char>(position)
            << ASCII::GS
            << 'f'
            << static_cast<char>(font);
    }

    std::ostringstream alignCommand;

    switch (static_cast<CBM_ALIGN>(lastAlign_))
    {
        case CBM_ALIGN::CBM_LEFT:
            alignCommand
                << ASCII::ESC
                << '$'
                << static_cast<char>(80)
                << static_cast<char>(0);
            break;

        case CBM_ALIGN::CBM_CENTER:
        case CBM_ALIGN::CBM_RIGHT:
            alignCommand
                << ASCII::ESC
                << '$'
                << static_cast<char>(0)
                << static_cast<char>(0);
            break;

        default:
            alignCommand
                << ASCII::ESC
                << '$'
                << static_cast<char>(80)
                << static_cast<char>(0);
            break;
    }

    std::ostringstream barcodeCommand;

    if (type == PRINTER_TYPE::CBM1000)
    {
        barcodeCommand
            << ASCII::GS
            << 'k'
            << static_cast<char>(72)
            << static_cast<char>(text.size())
            << text
            << ASCII::LF;
    }
    else
    {
        barcodeCommand
            << ASCII::GS
            << 'k'
            << static_cast<char>(7)
            << 'A'
            << text
            << static_cast<char>(0)
            << ASCII::LF;
    }

    const std::string output =
        alignCommand.str() +
        heightCommand.str() +
        widthCommand.str() +
        positionCommand.str() +
        barcodeCommand.str();

    enqueueWriteOnIoThread(toBytes(output));
}

void Printer::feedLineOnIoThread(int line)
{
    line = std::clamp(line, 0, 255);

    std::ostringstream command;
    command
        << ASCII::ESC
        << 'J'
        << static_cast<char>(line);

    enqueueWriteOnIoThread(toBytes(command.str()));
}

void Printer::log(const std::string& message) const
{
    Logger::getInstance()->FnLog(message, logFileName_, "PRINTER");
}

void Printer::logException(const std::string& functionName, const std::exception& exception) const
{
    Logger::getInstance()->FnLogExceptionError("Printer::" + functionName + " | Exception: " + exception.what());
}

std::vector<std::uint8_t> Printer::toBytes(const std::string& value)
{
    std::vector<std::uint8_t> result;
    result.reserve(value.size());

    for (const unsigned char character : value)
    {
        result.push_back(static_cast<std::uint8_t>(character));
    }

    return result;
}

std::string Printer::toHex(const std::vector<std::uint8_t>& data)
{
    std::ostringstream stream;
    stream << std::hex << std::uppercase << std::setfill('0');

    for (std::size_t index = 0; index < data.size(); ++index)
    {
        if (index != 0)
        {
            stream << ' ';
        }

        stream << std::setw(2) << static_cast<unsigned int>(data[index]);
    }

    return stream.str();
}

const char* Printer::printerTypeName(PRINTER_TYPE type)
{
    switch (type)
    {
        case PRINTER_TYPE::CBM:
            return "CBM";

        case PRINTER_TYPE::FTP:
            return "FTP";

        case PRINTER_TYPE::CBM1000:
            return "CBM1000";
    }

    return "UNKNOWN";
}

const char* Printer::statusName(PRINTER_STATUS status)
{
    switch (status)
    {
        case PRINTER_STATUS::ERROR:
            return "ERROR";

        case PRINTER_STATUS::IDLE:
            return "IDLE";

        case PRINTER_STATUS::NO_PAPER:
            return "NO_PAPER";
    }

    return "UNKNOWN";
}
