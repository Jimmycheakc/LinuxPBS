#include "log.h"

#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

#if defined(__linux__)
#include <pthread.h>
#endif

#include "common.h"
#include "ini_parser.h"
#include "operation.h"

#include "spdlog/async.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/spdlog.h"

namespace
{
constexpr const char* EXCEPTION_LOGGER_NAME = "EXCEPTION_LOGGER";
}

Logger::Logger()
{
    constexpr std::size_t kQueueSize = 8192;
    constexpr std::size_t kWorkerThreads = 1;

#if defined(__linux__)
    spdlog::init_thread_pool(
        kQueueSize,
        kWorkerThreads,
        []()
        {
            const int result =
                ::pthread_setname_np(
                    ::pthread_self(),
                    "LOG_IO");

            if (result != 0)
            {
                std::cerr
                    << "[LOGGER] Failed to name async thread"
                    << " | Error="
                    << result
                    << std::endl;
            }
        });
#else
    spdlog::init_thread_pool(
        kQueueSize,
        kWorkerThreads);
#endif
}

void Logger::FnShutdown()
{
    try
    {
        spdlog::shutdown();
    }
    catch (const std::exception& e)
    {
        std::cerr << "[LOGGER] Shutdown failed: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "[LOGGER] Unknown error during shutdown." << std::endl;
    }
}

Logger* Logger::getInstance()
{
    static Logger instance;
    return &instance;
}

void Logger::ensureLogDirectory() const
{
    const std::filesystem::path dirPath(LOG_FILE_PATH);

    if (std::filesystem::exists(dirPath))
    {
        return;
    }

    if (!std::filesystem::create_directories(dirPath) &&
        !std::filesystem::exists(dirPath))
    {
        throw std::runtime_error("Unable to create log directory: " + dirPath.string());
    }
}

std::string Logger::getCurrentDateYYMMDD()
{
    const std::time_t now = std::time(nullptr);
    std::tm timeInfo{};
    localtime_r(&now, &timeInfo);

    std::ostringstream oss;
    oss << std::put_time(&timeInfo, "%y%m%d");
    return oss.str();
}

std::string Logger::getCurrentTimestamp()
{
    const std::time_t now = std::time(nullptr);
    std::tm timeInfo{};
    localtime_r(&now, &timeInfo);

    std::ostringstream oss;
    oss << std::put_time(&timeInfo, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string Logger::formatLogMessage(const std::string& message, const std::string& option)
{
    std::ostringstream oss;

    std::string formattedOption = option;
    formattedOption += ':';

    oss << Common::getInstance()->FnGetDateTime();
    oss << std::setw(3) << std::setfill(' ') << "";
    oss << std::setw(8) << std::left << formattedOption;
    oss << message;

    return oss.str();
}

std::shared_ptr<spdlog::logger> Logger::getOrCreateFileLogger(const std::string& filename, const std::string& stationId, const std::string& dateStr)
{
    std::lock_guard<std::mutex> lock(loggerMutex_);

    ensureLogDirectory();

    const bool isMainLogger = filename.empty();

    // Preserve the project's existing logger registry naming convention.
    const std::string loggerName = isMainLogger ? stationId + dateStr : filename + dateStr;

    const std::string activeKey =
        isMainLogger ? "MAIN:" + stationId
                     : "EXTRA:" + stationId + ':' + filename;

    auto activeIt = activeLoggerDates_.find(activeKey);
    if (activeIt != activeLoggerDates_.end() &&
        activeIt->second != dateStr)
    {
        const std::string oldLoggerName =
            isMainLogger ? stationId + activeIt->second
                         : filename + activeIt->second;

        spdlog::drop(oldLoggerName);
        activeLoggerDates_.erase(activeIt);
    }

    auto logger = spdlog::get(loggerName);
    if (!logger)
    {
        const std::filesystem::path filePath =
            std::filesystem::path(LOG_FILE_PATH) /
            (stationId + filename + dateStr + ".log");

        logger = spdlog::basic_logger_mt<spdlog::async_factory>(loggerName, filePath.string());

        logger->set_pattern("%v");
        logger->set_level(spdlog::level::info);

        // Preserve the existing behaviour: each INFO record is flushed.
        // This prioritizes log durability over maximum throughput.
        logger->flush_on(spdlog::level::info);
    }

    activeLoggerDates_[activeKey] = dateStr;
    return logger;
}

std::shared_ptr<spdlog::logger> Logger::getOrCreateExceptionLogger(const std::string& dateStr)
{
    std::lock_guard<std::mutex> lock(loggerMutex_);

    ensureLogDirectory();

    // The old implementation kept one fixed EXCEPTION_LOGGER forever, which
    // meant a process running across midnight continued writing to yesterday's
    // exception file. Rotate it when the date changes.
    if (!exceptionLoggerDate_.empty() &&
        exceptionLoggerDate_ != dateStr)
    {
        spdlog::drop(EXCEPTION_LOGGER_NAME);
    }

    auto logger = spdlog::get(EXCEPTION_LOGGER_NAME);
    if (!logger)
    {
        const std::filesystem::path filePath =
            std::filesystem::path(LOG_FILE_PATH) /
            ("exception_" + dateStr + ".log");

        logger = spdlog::basic_logger_mt<spdlog::async_factory>(EXCEPTION_LOGGER_NAME, filePath.string());

        logger->set_pattern("%v");
        logger->set_level(spdlog::level::err);
        logger->flush_on(spdlog::level::err);
    }

    exceptionLoggerDate_ = dateStr;
    return logger;
}

void Logger::FnCreateLogFile(const std::string& filename)
{
    try
    {
        const std::string stationId = IniParser::getInstance()->FnGetStationID();

        const std::string dateStr = getCurrentDateYYMMDD();

        (void)getOrCreateFileLogger(filename, stationId, dateStr);
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        std::cerr << "[LOGGER] Filesystem error while creating log file: " << e.what() << std::endl;
    }
    catch (const spdlog::spdlog_ex& e)
    {
        std::cerr << "[LOGGER] spdlog initialization failed: " << e.what() << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[LOGGER] Exception while creating log file: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "[LOGGER] Unknown exception while creating log file." << std::endl;
    }
}

void Logger::forwardLogToMonitor(const std::string& logMessage)
{
    thread_local bool forwardingToMonitor = false;

    // Prevent recursion on this thread.
    //
    // Example:
    // FnLog()
    //   -> FnSendLogMessageToMonitor()
    //       -> FnLog()
    //           -> do not forward again
    if (forwardingToMonitor)
    {
        return;
    }

    struct ForwardingGuard
    {
        explicit ForwardingGuard(bool& flag)
            : flag_(flag)
        {
            flag_ = true;
        }

        ~ForwardingGuard()
        {
            flag_ = false;
        }

        bool& flag_;
    };

    /*
     * Set the guard BEFORE calling anything in operation.
     *
     * Even FnIsOperationInitialized() could theoretically log,
     * so recursion protection should already be active.
     */
    ForwardingGuard guard(forwardingToMonitor);

    try
    {
        auto* operationInstance = operation::getInstance();

        if (!operationInstance->FnIsOperationInitialized())
        {
            return;
        }

        operationInstance->FnSendLogMessageToMonitor(logMessage);
    }
    catch (const std::exception& e)
    {
        /*
         * Do NOT use Logger::FnLog() here.
         * We are already inside the logging path.
         */
        std::cerr << "[LOGGER] Failed to forward log to monitor: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "[LOGGER] Unknown error while forwarding log to monitor." << std::endl;
    }
}

void Logger::FnLog(const std::string& sMsg, const std::string& filename, const std::string& sOption)
{
    try
    {
        const std::string logMessage = formatLogMessage(sMsg, sOption);

        const std::string stationId = IniParser::getInstance()->FnGetStationID();

        const std::string dateStr = getCurrentDateYYMMDD();

        auto logger = getOrCreateFileLogger(filename, stationId, dateStr);

        if (!logger)
        {
            std::cerr << "[LOGGER] Failed to obtain logger instance." << std::endl;
            return;
        }

        logger->info(logMessage);

        /*
         * Only the main PBS log is forwarded
         * to the monitor.
         */
        if (filename.empty())
        {
            forwardLogToMonitor(logMessage);

#ifdef CONSOLE_LOG_ENABLE
            std::cout << logMessage << std::endl;
#endif
        }
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        std::cerr << "[LOGGER] Filesystem error while writing log: " << e.what() << std::endl;
    }
    catch (const spdlog::spdlog_ex& e)
    {
        std::cerr << "[LOGGER] spdlog error while writing log: " << e.what() << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[LOGGER] Exception while writing log: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "[LOGGER] Unknown exception while writing log." << std::endl;
    }
}

void Logger::FnCreateExceptionLogFile()
{
    try
    {
        const std::string dateStr = getCurrentDateYYMMDD();
        (void)getOrCreateExceptionLogger(dateStr);
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        std::cerr << "[LOGGER] Filesystem error while creating exception log: " << e.what() << std::endl;
    }
    catch (const spdlog::spdlog_ex& e)
    {
        std::cerr << "[LOGGER] spdlog error while creating exception log: " << e.what() << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[LOGGER] Exception while creating exception log: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "[LOGGER] Unknown exception while creating exception log." << std::endl;
    }
}

void Logger::FnLogExceptionError(const std::string& errorMsg)
{
    try
    {
        const std::string dateStr = getCurrentDateYYMMDD();
        auto logger = getOrCreateExceptionLogger(dateStr);

        if (!logger)
        {
            std::cerr << "[LOGGER] Exception logger is not available." << std::endl;
            return;
        }

        std::ostringstream oss;
        oss << '[' << getCurrentTimestamp() << "] Exception: " << errorMsg;
        logger->error(oss.str());
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        std::cerr << "[LOGGER] Filesystem error while writing exception log: " << e.what() << std::endl;
    }
    catch (const spdlog::spdlog_ex& e)
    {
        std::cerr << "[LOGGER] spdlog error while writing exception log: " << e.what() << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[LOGGER] Exception while writing exception log: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "[LOGGER] Unknown exception while writing exception log." << std::endl;
    }
}

void Logger::PrintActiveLoggerDates()
{
    std::lock_guard<std::mutex> lock(loggerMutex_);

    std::cout << "[Active Logger Dates]" << std::endl;

    if (activeLoggerDates_.empty())
    {
        std::cout << "  (none)" << std::endl;
    }
    else
    {
        for (const auto& [loggerName, date] : activeLoggerDates_)
        {
            std::cout << "  Logger Name: " << loggerName << " | Date: " << date << std::endl;
        }
    }

    if (!exceptionLoggerDate_.empty())
    {
        std::cout << "  Logger Name: EXCEPTION" << " | Date: " << exceptionLoggerDate_ << std::endl;
    }
}
