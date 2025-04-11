#include <ctime>
#include <sstream>
#include <iomanip>
#include <boost/filesystem.hpp>
#include "common.h"
#include "ini_parser.h"
#include "operation.h"
#include "log.h"

Logger* Logger::logger_ = nullptr;
std::mutex Logger::mutex_;

Logger::Logger()
{

}

Logger::~Logger()
{
    spdlog::drop_all();
    spdlog::shutdown();
}

Logger* Logger::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (logger_ == nullptr)
    {
        logger_ = new Logger();
    }
    return logger_;
}

void Logger::FnCreateLogFile(std::string filename)
{
    try
    {
        const boost::filesystem::path dirPath(LOG_FILE_PATH);

        if (!(boost::filesystem::exists(dirPath)))
        {
            if (!(boost::filesystem::create_directories(dirPath)))
            {
                std::cerr << "Failed to create directory: " << dirPath << std::endl;
            }
        }
    }
    catch (const boost::filesystem::filesystem_error& e)
    {
        std::cerr << "Boost.Filesystem Exception during creating log file: " << e.what() << std::endl;
        return;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception during creating log file: " << e.what() << std::endl;
        return;
    }
    catch (...)
    {
        std::cerr << "Unknown Exception during creating log file." << std::endl;
        return;
    }

    // Temp: need to get the station_ID from file
    std::string sStationID = IniParser::getInstance()->FnGetStationID();

    time_t timer = time(0);
    struct tm timeinfo = {};
    localtime_r(&timer, &timeinfo);

    std::stringstream ssYearMonthDay;
    ssYearMonthDay << std::put_time(&timeinfo, "%y%m%d");

    std::string absoluteFilePath = LOG_FILE_PATH + std::string("/") + sStationID + filename + ssYearMonthDay.str() + std::string(".log");
    std::string logger = (filename == "") ? sStationID : filename;

    try
    {
        auto asyncFile = spdlog::basic_logger_mt<spdlog::async_factory>(logger, absoluteFilePath);
        asyncFile->set_pattern("%v");
        asyncFile->set_level(spdlog::level::info);
        asyncFile->flush_on(spdlog::level::info);
    }
    catch(const spdlog::spdlog_ex &e)
    {
        std::cerr << "SPDLog init failed: " << e.what() << std::endl;
        return;
    }
}

void Logger::FnLog(std::string sMsg, std::string filename, std::string sOption)
{
    std::stringstream sLogMsg;

    sOption += ":";
    sLogMsg << Common::getInstance()->FnGetDateTime();
    sLogMsg << std::setw(3) << std::setfill(' ') << "";
    sLogMsg << std::setw(8) << std::left << sOption;
    sLogMsg << sMsg;

    // Check whether file exists or not, if not exists, then create a new file
    // Temp: need to get the station_ID from file
    std::string sStationID = IniParser::getInstance()->FnGetStationID();

    time_t timer = time(0);
    struct tm timeinfo = {};
    localtime_r(&timer, &timeinfo);

    std::stringstream ssYearMonthDay;
    ssYearMonthDay << std::put_time(&timeinfo, "%y%m%d");

    std::string loggerName  = (filename == "") ? sStationID : filename;
    std::string absoluteFilePath = LOG_FILE_PATH + std::string("/") + sStationID + filename + ssYearMonthDay.str() + std::string(".log");
    std::string absoluteMainFilePath = LOG_FILE_PATH + std::string("/") + sStationID + ssYearMonthDay.str() + std::string(".log");

    if (!(boost::filesystem::exists(absoluteFilePath)))
    {
        spdlog::drop(loggerName);
        FnCreateLogFile(filename);
    }

    if (!(boost::filesystem::exists(absoluteMainFilePath)))
    {
        spdlog::drop(sStationID);
        FnCreateLogFile();
    }

    if (!filename.empty())
    {
        auto logger = spdlog::get(loggerName);
        if (logger)
        {
            logger->info(sLogMsg.str());
            logger->flush();

        }
    }
    else
    {
        auto mainLogger = spdlog::get(sStationID);
        if (mainLogger)
        {
            mainLogger->info(sLogMsg.str());
            mainLogger->flush();

            // Send to Log Message to Monitor
            if (operation::getInstance()->FnIsOperationInitialized())
            {
                operation::getInstance()->FnSendLogMessageToMonitor(sLogMsg.str());
            }
        }

#ifdef CONSOLE_LOG_ENABLE
        {
            std::cout << sLogMsg.str() << std::endl;
        }
#endif
    }
}

void Logger::FnCreateExceptionLogFile()
{
    // Check if the logger has already been initialized
    if (spdlog::get("EXCEPTION_LOGGER") != nullptr)
    {
        // If the logger is already initialized, no need to recreate it
        return;
    }

    try
    {
        boost::filesystem::path dirPath(LOG_FILE_PATH);
        if (!boost::filesystem::exists(dirPath))
        {
            if (!boost::filesystem::create_directories(dirPath))
            {
                std::cerr << "Failed to create exception log directory: " << dirPath << std::endl;
                return;
            }
        }

        time_t timer = time(0);
        struct tm timeinfo = {};
        localtime_r(&timer, &timeinfo);

        std::stringstream ssDate;
        ssDate << std::put_time(&timeinfo, "%y%m%d");

        std::string exceptionFile = LOG_FILE_PATH + "/exception_" + ssDate.str() + ".log";

        // Create async file logger
        auto exLogger = spdlog::basic_logger_mt<spdlog::async_factory>("EXCEPTION_LOGGER", exceptionFile);
        exLogger->set_pattern("%v");
        exLogger->set_level(spdlog::level::err);
        exLogger->flush_on(spdlog::level::err);
    }
    catch (const spdlog::spdlog_ex& e)
    {
        std::cerr << "Failed to create exception logger: " << e.what() << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to create exception log file: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Unknown Exception during creating exception log file." << std::endl;
    }
}

void Logger::FnLogExceptionError(const std::string& errorMsg)
{
    try
    {
        // Ensure the exception log file is created only once
        FnCreateExceptionLogFile();

        // Get the current timestamp
        time_t timer = time(0);
        struct tm timeinfo = {};
        localtime_r(&timer, &timeinfo);

        std::stringstream ssDate;
        ssDate << std::put_time(&timeinfo, "%y%m%d");

        // Create log message with timestamp
        std::stringstream logMsg;
        logMsg << "[" << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S") << "] ";
        logMsg << "Exception: " << errorMsg;

        // Log the exception error message
        auto exLogger = spdlog::get("EXCEPTION_LOGGER");
        if (exLogger)
        {
            exLogger->error(logMsg.str());
            exLogger->flush();
        }
        else
        {
            std::cerr << "Failed to log exception: Logger not initialized" << std::endl;
        }
    }
    catch (const spdlog::spdlog_ex& e)
    {
        std::cerr << "Failed to create exception logger: " << e.what() << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to create exception log file: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Unknown Exception during creating exception log file." << std::endl;
    } 
}
