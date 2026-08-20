#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace spdlog
{
class logger;
}

class Logger
{
public:
    const std::string LOG_FILE_PATH = "/home/root/carpark/Log";

    static Logger* getInstance();

    void FnShutdown();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    void FnCreateLogFile(const std::string& filename = "");
    void FnLog(const std::string& sMsg = "", const std::string& filename = "", const std::string& sOption = "PBS");

    void FnCreateExceptionLogFile();
    void FnLogExceptionError(const std::string& errorMsg);

    void PrintActiveLoggerDates();

private:
    Logger();
    ~Logger() = default;

    void ensureLogDirectory() const;

    std::shared_ptr<spdlog::logger> getOrCreateFileLogger(const std::string& filename, const std::string& stationId, const std::string& dateStr);

    std::shared_ptr<spdlog::logger> getOrCreateExceptionLogger(const std::string& dateStr);

    void forwardLogToMonitor(const std::string& logMessage);

    static std::string getCurrentDateYYMMDD();
    static std::string getCurrentTimestamp();
    static std::string formatLogMessage(const std::string& message, const std::string& option);

    mutable std::mutex loggerMutex_;

    // Key -> active YYMMDD. Keys are prefixed with MAIN:/EXTRA: to avoid
    // collisions between the main logger and module-specific loggers.
    std::unordered_map<std::string, std::string> activeLoggerDates_;

    std::string exceptionLoggerDate_;
};