#pragma once

#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "ini_parser.h"
#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/sinks/basic_file_sink.h"


class Logger
{
public:
    const std::string LOG_FILE_PATH = "/home/root/carpark/Log";

    static Logger* getInstance();
    void FnCreateLogFile(std::string filename="");
    void FnLog(std::string sMsg="", std::string filename="", std::string sOption="PBS");
    void FnCreateExceptionLogFile();
    void FnLogExceptionError(const std::string& errorMsg);

    /**
     * Singleton Logger should not be cloneable.
     */
    Logger(Logger &logger) = delete;

    /**
     * Singleton Logger should not be assignable.
     */
    void operator=(const Logger &) = delete;

private:
    static Logger* logger_;
    static std::mutex mutex_;
    Logger();
    ~Logger();
};