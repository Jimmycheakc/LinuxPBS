#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include "ini_parser.h"
#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/sinks/basic_file_sink.h"


class Logger
{
public:
    const std::string LOG_FILE_PATH = "/home/root/LinuxPBSSourceCode/LatestSourceCode/carpark/log";

    static Logger* getInstance();
    void FnCreateLogFile(std::string filename="");
    void FnLog(std::string sMsg="", std::string filename="", std::string sOption="PBS");

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
    Logger();
    ~Logger();
};