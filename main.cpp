#include <csignal>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include "antenna.h"
#include "boost/asio.hpp"
#include "crc.h"
#include "common.h"
#include "dio.h"
#include "gpio.h"
#include "ini_parser.h"
#include "lcd.h"
#include "led.h"
#include "log.h"
#include "system_info.h"
#include "upt.h"
#include "event_manager.h"
#include "event_handler.h"
#include "lcsc.h"
#include "db.h"
#include "odbc.h"
#include "structuredata.h"
#include "operation.h"
#include "printer.h"
#include "udp.h"
#include "ksm_reader.h"
#include "eep_client.h"


void dailyProcessTimerHandler(const boost::system::error_code &ec, boost::asio::steady_timer * timer, boost::asio::strand<boost::asio::io_context::executor_type>* strand_)
{
    auto start = std::chrono::steady_clock::now(); // Measure the start time of the handler execution

    // Print the start time in HH:MM:SS format
    //auto startTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    //std::cout << "Start Time: " << std::put_time(std::localtime(&startTime), "%T") << std::endl;

    //Sync PMS time duration
    static auto lastSyncTime = std::chrono::steady_clock::now();
    auto durationSinceSync = std::chrono::duration_cast<std::chrono::hours>(start - lastSyncTime);

    // Set the last UPOS settlement time
    static std::string sLastUPTSettleTime = Common::getInstance()->FnGetDate();

    //------ timer process start
    if (operation::getInstance()->FnIsOperationInitialized())
    {
        if (operation::getInstance()->tProcess.gbLoopApresent.load() == false) 
        {
            std::string details;
            if (operation::getInstance()->tProcess.giSystemOnline == 1)
            {
                if (PingWithTimeOut(IniParser::getInstance()->FnGetCentralDBServer(), 1, details) == true)
                {
                    operation::getInstance()->tProcess.giSystemOnline = 0;
                }
            }

            if (operation::getInstance()->tProcess.giSystemOnline == 0 && operation::getInstance()->tProcess.glNoofOfflineData > 0)
            {
                db::getInstance()->moveOfflineTransToCentral();
            }

            // Sysnc time from PMS per hour
            if (durationSinceSync >= std::chrono::hours(1))
            {
                db::getInstance()->synccentraltime();
                lastSyncTime = start;
                //-----
                operation::getInstance()->CheckReader();
            }

            // Check the LCSC CD files- download and upload
            LCSCReader::getInstance()->FnUploadLCSCCDFiles();

            // Clear expired season
            if (operation::getInstance()->tProcess.giLastHousekeepingDate != Common::getInstance()->FnGetCurrentDay())
            {
                db::getInstance()->HouseKeeping();
                operation::getInstance()->tProcess.giLastHousekeepingDate = Common::getInstance()->FnGetCurrentDay();
            }

            // Check the UPOS last settlement date
            std::string sCurrentDate = Common::getInstance()->FnGetDate();
            if (operation::getInstance()->gtStation.iType == tiExit)
            {
                if (sLastUPTSettleTime != sCurrentDate)
                {
                    Upt::getInstance()->FnUptSendDeviceRetrieveLastSettlementRequest();
                    sLastUPTSettleTime = sCurrentDate;
                }
            }
        }
        // Send DateTime to Monitor
        operation::getInstance()->FnSendDateTimeToMonitor();
    }
    else
    {
        if (operation::getInstance()->tProcess.gbInitParamFail == 1)
        {
            if (operation::getInstance()->LoadedparameterOK())
            {
                operation::getInstance()->tProcess.gbInitParamFail = 0;
                operation::getInstance()->Initdevice(*(operation::getInstance()->iCurrentContext));
                operation::getInstance()->isOperationInitialized_.store(true);
                operation::getInstance()->tProcess.setIdleMsg(0, operation::getInstance()->tMsg.Msg_DefaultLED[0]);
                operation::getInstance()->tProcess.setIdleMsg(1, operation::getInstance()->tMsg.Msg_Idle[1]);
                operation::getInstance()->writelog("EPS in operation","OPR");
            }
        }
    }

    //--------
    auto end = std::chrono::steady_clock::now(); // Measure the end time of the handler execution

    // Print the end time in HH:MM:SS format
    //auto endTimeConverted = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    //std::cout << "End Time: " << std::put_time(std::localtime(&endTimeConverted), "%T") << std::endl;

    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start); // Calculate the duration of the handler execution

    timer->expires_at(timer->expiry() + boost::asio::chrono::seconds(5) + duration);
    boost::asio::post(*strand_, [timer, strand_]() {
        timer->async_wait(boost::bind(dailyProcessTimerHandler, boost::asio::placeholders::error, timer, strand_));
    });
}

void dailyLogHandler(const boost::system::error_code &ec, boost::asio::steady_timer * timer, boost::asio::strand<boost::asio::io_context::executor_type>* logStrand_)
{
    auto start = std::chrono::steady_clock::now(); // Measure the start time of the handler execution

    // Get today's date
    auto today = std::chrono::system_clock::now();
    auto todayDate = std::chrono::system_clock::to_time_t(today);
    std::tm localToday;
    localtime_r(&todayDate, &localToday);

    static int lastLoggedDayOfYear = localToday.tm_yday;
    if (localToday.tm_yday != lastLoggedDayOfYear)
    {
        SystemInfo::getInstance()->FnLogSysInfo();
        lastLoggedDayOfYear = localToday.tm_yday;
    }

    // Check if it's past 12 AM (midnight)
    if (localToday.tm_hour == 0 && localToday.tm_min >= 1 && localToday.tm_min < 30)
    {
        std::string logFilePath = Logger::getInstance()->LOG_FILE_PATH;
        std::string LPRDbLogFilePath = "/home/root/evas_web/db_files";
        std::string LCSCSettleFilePath = LCSCReader::getInstance()->LOCAL_LCSC_SETTLEMENT_FOLDER_PATH;

        // Extract year, month and day
        std::ostringstream ossToday;
        ossToday << std::setw(2) << std::setfill('0') << (localToday.tm_year % 100);
        ossToday << std::setw(2) << std::setfill('0') << (localToday.tm_mon + 1);
        ossToday << std::setw(2) << std::setfill('0') << localToday.tm_mday;

        std::string todayDateStr = ossToday.str();

        std::ostringstream dbLogoss;
        dbLogoss << std::setw(4) << std::setfill('0') << (localToday.tm_year + 1900) << "-"
            << std::setw(2) << std::setfill('0') << (localToday.tm_mon + 1) << "-"
            << std::setw(2) << std::setfill('0') << localToday.tm_mday;

        std::string LPRDbFormattedDate = dbLogoss.str();

        std::ostringstream lcscSettleoss;
        lcscSettleoss << std::setw(4) << std::setfill('0') << (localToday.tm_year + 1900)
            << std::setw(2) << std::setfill('0') << (localToday.tm_mon + 1)
            << std::setw(2) << std::setfill('0') << localToday.tm_mday;

        std::string LCSCSettleFormattedDate = lcscSettleoss.str();

        // Iterate through the files in the log file path
        int foundNo_ = 0;
        if (std::filesystem::exists(logFilePath) && std::filesystem::is_directory(logFilePath))
        {
            for (const auto& entry : std::filesystem::directory_iterator(logFilePath))
            {
                if ((entry.path().filename().string().find(todayDateStr) == std::string::npos) &&
                    (entry.path().extension() == ".log"))
                {
                    foundNo_ ++;
                }
            }
        }
        else
        {
            Logger::getInstance()->FnLog("Log directory does not exist: " + logFilePath, "", "OPR");
        }

        int foundLPRDbLog_  = 0;
        if (std::filesystem::exists(LPRDbLogFilePath) && std::filesystem::is_directory(LPRDbLogFilePath))
        {
            for (const auto& entry : std::filesystem::directory_iterator(LPRDbLogFilePath))
            {
                if ((entry.path().filename().string().find(LPRDbFormattedDate) == std::string::npos) &&
                    (entry.path().extension() == ".csv"))
                {
                    foundLPRDbLog_ ++;
                }
            }
        }
        else
        {
            Logger::getInstance()->FnLog("LPR DB log directory does not exist: " + LPRDbLogFilePath, "", "OPR");
        }

        int foundLCSCSettleFile_ = 0;
        if (std::filesystem::exists(LCSCSettleFilePath) && std::filesystem::is_directory(LCSCSettleFilePath))
        {
            for (const auto& entry : std::filesystem::directory_iterator(LCSCSettleFilePath))
            {
                if ((entry.path().filename().string().find(LCSCSettleFormattedDate) == std::string::npos) &&
                    (entry.path().extension() == ".lcs"))
                {
                    foundLCSCSettleFile_ ++;
                }
            }
        }
        else
        {
            Logger::getInstance()->FnLog("LCSC settlement directory does not exist: " + LCSCSettleFilePath, "", "OPR");
        }

        std::string details;
        if (PingWithTimeOut(IniParser::getInstance()->FnGetCentralDBServer(), 1, details) == true)
        {
            if (foundNo_ > 0)
            {
                std::stringstream ss;
                ss << "Found " << foundNo_ << " log files.";
                Logger::getInstance()->FnLog(ss.str(), "", "OPR");

                // Create the mount poin directory if doesn't exist
                std::string mountPoint = "/mnt/logbackup";
                std::string sharedFolderPath = operation::getInstance()->tParas.gsLogBackFolder;
                std::replace(sharedFolderPath.begin(), sharedFolderPath.end(), '\\', '/');
                std::string username = IniParser::getInstance()->FnGetCentralUsername();
                std::string password = IniParser::getInstance()->FnGetCentralPassword();

                try
                {
                    if (!std::filesystem::exists(mountPoint))
                    {
                        std::error_code ec;
                        if (!std::filesystem::create_directories(mountPoint, ec))
                        {
                            Logger::getInstance()->FnLog(("Failed to create " + mountPoint + " directory : " + ec.message()), "", "OPR");
                        }
                        else
                        {
                            Logger::getInstance()->FnLog(("Successfully to create " + mountPoint + " directory."), "", "OPR");
                        }
                    }
                    else
                    {
                        Logger::getInstance()->FnLog(("Mount point directory: " + mountPoint + " exists."), "", "OPR");
                    }

                    // Mount the shared folder
                    std::string mountCommand = "sudo mount -t cifs " + sharedFolderPath + " " + mountPoint +
                                                " -o username=" + username + ",password=" + password;
                    int mountStatus = std::system(mountCommand.c_str());
                    if (mountStatus != 0)
                    {
                        Logger::getInstance()->FnLog(("Failed to mount " + mountPoint), "", "OPR");
                    }
                    else
                    {
                        Logger::getInstance()->FnLog(("Successfully to mount " + mountPoint), "", "OPR");

                        // Copy files to mount folder
                        for (const auto& entry : std::filesystem::directory_iterator(logFilePath))
                        {
                            if ((entry.path().filename().string().find(todayDateStr) == std::string::npos) &&
                                (entry.path().extension() == ".log"))
                            {
                                std::error_code ec;
                                std::filesystem::copy(entry.path(), mountPoint / entry.path().filename(), std::filesystem::copy_options::overwrite_existing, ec);

                                if (!ec)
                                {
                                    std::stringstream ss;
                                    ss << "Copy file : " << entry.path() << " successfully.";
                                    Logger::getInstance()->FnLog(ss.str(), "", "OPR");
                                    
                                    std::filesystem::remove(entry.path());
                                    ss.str("");
                                    ss << "Removed log file : " << entry.path() << " successfully";
                                    Logger::getInstance()->FnLog(ss.str(), "", "OPR");
                                }
                                else
                                {
                                    std::stringstream ss;
                                    ss << "Failed to copy log file : " << entry.path();
                                    Logger::getInstance()->FnLog(ss.str(), "", "OPR");
                                }
                            }
                        }
                    }
                }
                catch (const std::filesystem::filesystem_error& e)
                {
                    std::stringstream ss;
                    ss << __func__ << ", Exception: " << e.what();
                    Logger::getInstance()->FnLogExceptionError(ss.str());
                }
                catch (const std::exception& e)
                {
                    std::stringstream ss;
                    ss << __func__ << ", Exception: " << e.what();
                    Logger::getInstance()->FnLogExceptionError(ss.str());
                }
                catch (...)
                {
                    std::stringstream ss;
                    ss << __func__ << ", Exception: Unknown Exception";
                    Logger::getInstance()->FnLogExceptionError(ss.str());
                }

                try
                {
                    // Unmount the shared folder
                    std::string unmountCommand = "sudo umount " + mountPoint;
                    int unmountStatus = std::system(unmountCommand.c_str());
                    if (unmountStatus != 0)
                    {
                        Logger::getInstance()->FnLog(("Failed to unmount " + mountPoint), "", "OPR");
                    }
                    else
                    {
                        Logger::getInstance()->FnLog(("Successfully to unmount " + mountPoint), "", "OPR");
                    }
                }
                catch (const std::filesystem::filesystem_error& e)
                {
                    std::stringstream ss;
                    ss << __func__ << ", Unmount Exception: " << e.what();
                    Logger::getInstance()->FnLogExceptionError(ss.str());
                }
                catch (const std::exception& e)
                {
                    std::stringstream ss;
                    ss << __func__ << ", Unmount Exception: " << e.what();
                    Logger::getInstance()->FnLogExceptionError(ss.str());
                }
                catch (...)
                {
                    std::stringstream ss;
                    ss << __func__ << ", Unmount Exception: Unknown Exception";
                    Logger::getInstance()->FnLogExceptionError(ss.str());
                }
            }
            
            if (foundLPRDbLog_ > 0)
            {

                std::stringstream ss;
                ss << "Found " << foundLPRDbLog_ << " lpn database files.";
                Logger::getInstance()->FnLog(ss.str(), "", "OPR");

                // Create the mount poin directory if doesn't exist
                std::string mountPoint = "/mnt/dbfilesbackup";
                std::string sharedFolderPath = operation::getInstance()->tParas.gsLogBackFolder;
                std::replace(sharedFolderPath.begin(), sharedFolderPath.end(), '\\', '/');
                // Find the last slash
                // Replace from "//192.168.2.141/Carpark/Log" to "//192.168.2.141/Carpark"
                std::size_t pos = sharedFolderPath.find_last_of('/');
                if (pos != std::string::npos)
                {
                    sharedFolderPath = sharedFolderPath.substr(0, pos);
                }
                std::string username = IniParser::getInstance()->FnGetCentralUsername();
                std::string password = IniParser::getInstance()->FnGetCentralPassword();

                try
                {
                    if (!std::filesystem::exists(mountPoint))
                    {
                        std::error_code ec;
                        if (!std::filesystem::create_directories(mountPoint, ec))
                        {
                            Logger::getInstance()->FnLog(("Failed to create " + mountPoint + " directory : " + ec.message()), "", "OPR");
                        }
                        else
                        {
                            Logger::getInstance()->FnLog(("Successfully to create " + mountPoint + " directory."), "", "OPR");
                        }
                    }
                    else
                    {
                        Logger::getInstance()->FnLog(("Mount point directory: " + mountPoint + " exists."), "", "OPR");
                    }

                    // Mount the shared folder
                    std::string mountCommand = "sudo mount -t cifs " + sharedFolderPath + " " + mountPoint +
                                                " -o username=" + username + ",password=" + password;
                    int mountStatus = std::system(mountCommand.c_str());
                    if (mountStatus != 0)
                    {
                        Logger::getInstance()->FnLog(("Failed to mount " + mountPoint), "", "OPR");
                    }
                    else
                    {
                        Logger::getInstance()->FnLog(("Successfully to mount " + mountPoint), "", "OPR");

                        // Get current year and and month
                        auto now = std::chrono::system_clock::now();
                        std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
                        std::tm localNow;
                        localtime_r(&nowTime, &localNow);

                        std::ostringstream yearFolderSS, monthFolderSS;
                        yearFolderSS << std::setw(4) << std::setfill('0') << (localNow.tm_year + 1900);
                        monthFolderSS << std::setw(2) << std::setfill('0') << (localNow.tm_mon + 1);

                       std::filesystem::path targetFolder = std::filesystem::path(mountPoint) / "Database/LPN" / yearFolderSS.str() / monthFolderSS.str();

                        // Create folder if they do not exist
                        std::error_code ec;
                        if (!std::filesystem::exists(targetFolder))
                        {
                            if (!std::filesystem::create_directories(targetFolder, ec))
                            {
                                Logger::getInstance()->FnLog("Failed to create target folder: " + targetFolder.string() + " | " + ec.message(), "", "OPR");
                            }
                            else
                            {
                                Logger::getInstance()->FnLog(("Successfully to create " + targetFolder.string() + " directory."), "", "OPR");
                            }
                        }
                        else
                        {
                            Logger::getInstance()->FnLog(("Target folder directory: " + targetFolder.string() + " exists."), "", "OPR");
                        }

                        // Copy files to mount folder
                        for (const auto& entry : std::filesystem::directory_iterator(LPRDbLogFilePath))
                        {
                            if ((entry.path().filename().string().find(LPRDbFormattedDate) == std::string::npos) &&
                                (entry.path().extension() == ".csv"))
                            {
                                std::string filename = entry.path().filename().string();
                                const char* lastUnderScore = strrchr(filename.c_str(), '_');

                                if (lastUnderScore)
                                {
                                    int year, month, day;
                                    if (std::sscanf(lastUnderScore + 1, "%4d-%2d-%2d.csv", &year, &month, &day) == 3)
                                    {
                                        std::ostringstream checkYearFolderSS, checkMonthFolderSS;
                                        checkYearFolderSS << std::setw(4) << std::setfill('0') << year;
                                        checkMonthFolderSS << std::setw(2) << std::setfill('0') << month;

                                        std::filesystem::path checkTargetFolder = std::filesystem::path(mountPoint) / "Database/LPN" / checkYearFolderSS.str() / checkMonthFolderSS.str();

                                        std::error_code ec;
                                        std::filesystem::create_directories(checkTargetFolder, ec);
                                        if (ec)
                                        {
                                            Logger::getInstance()->FnLog("Failed to create folder: " + checkTargetFolder.string() + " | " + ec.message(), "", "OPR");
                                            continue;
                                        }

                                        std::filesystem::copy(entry.path(), checkTargetFolder / entry.path().filename(), std::filesystem::copy_options::overwrite_existing, ec);

                                        if (!ec)
                                        {
                                            std::stringstream ss;
                                            ss << "Copy file : " << entry.path() << " successfully.";
                                            Logger::getInstance()->FnLog(ss.str(), "", "OPR");
                                            
                                            std::filesystem::remove(entry.path());
                                            ss.str("");
                                            ss << "Removed log file : " << entry.path() << " successfully";
                                            Logger::getInstance()->FnLog(ss.str(), "", "OPR");
                                        }
                                        else
                                        {
                                            std::stringstream ss;
                                            ss << "Failed to copy log file : " << entry.path();
                                            Logger::getInstance()->FnLog(ss.str(), "", "OPR");
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                catch (const std::filesystem::filesystem_error& e)
                {
                    std::stringstream ss;
                    ss << __func__ << ", Exception: " << e.what();
                    Logger::getInstance()->FnLogExceptionError(ss.str());
                }
                catch (const std::exception& e)
                {
                    std::stringstream ss;
                    ss << __func__ << ", Exception: " << e.what();
                    Logger::getInstance()->FnLogExceptionError(ss.str());
                }
                catch (...)
                {
                    std::stringstream ss;
                    ss << __func__ << ", Exception: Unknown Exception";
                    Logger::getInstance()->FnLogExceptionError(ss.str());
                }

                try
                {
                    // Unmount the shared folder
                    std::string unmountCommand = "sudo umount " + mountPoint;
                    int unmountStatus = std::system(unmountCommand.c_str());
                    if (unmountStatus != 0)
                    {
                        Logger::getInstance()->FnLog(("Failed to unmount " + mountPoint), "", "OPR");
                    }
                    else
                    {
                        Logger::getInstance()->FnLog(("Successfully to unmount " + mountPoint), "", "OPR");
                    }
                }
                catch (const std::filesystem::filesystem_error& e)
                {
                    std::stringstream ss;
                    ss << __func__ << ", Unmount Exception: " << e.what();
                    Logger::getInstance()->FnLogExceptionError(ss.str());
                }
                catch (const std::exception& e)
                {
                    std::stringstream ss;
                    ss << __func__ << ", Unmount Exception: " << e.what();
                    Logger::getInstance()->FnLogExceptionError(ss.str());
                }
                catch (...)
                {
                    std::stringstream ss;
                    ss << __func__ << ", Unmount Exception: Unknown Exception";
                    Logger::getInstance()->FnLogExceptionError(ss.str());
                }
            }

            
            if (foundLCSCSettleFile_ > 0)
            {

                std::stringstream ss;
                ss << "Found " << foundLCSCSettleFile_ << " lcsc settlement files.";
                Logger::getInstance()->FnLog(ss.str(), "", "OPR");

                // Create the mount poin directory if doesn't exist
                std::string mountPoint = "/mnt/lcscsettlementfiles";
                std::string sharedFolderPath = operation::getInstance()->tParas.gsRemoteLCSC;
                std::replace(sharedFolderPath.begin(), sharedFolderPath.end(), '\\', '/');

                std::string username = IniParser::getInstance()->FnGetCentralUsername();
                std::string password = IniParser::getInstance()->FnGetCentralPassword();

                try
                {
                    if (!std::filesystem::exists(mountPoint))
                    {
                        std::error_code ec;
                        if (!std::filesystem::create_directories(mountPoint, ec))
                        {
                            Logger::getInstance()->FnLog(("Failed to create " + mountPoint + " directory : " + ec.message()), "", "OPR");
                        }
                        else
                        {
                            Logger::getInstance()->FnLog(("Successfully to create " + mountPoint + " directory."), "", "OPR");
                        }
                    }
                    else
                    {
                        Logger::getInstance()->FnLog(("Mount point directory: " + mountPoint + " exists."), "", "OPR");
                    }

                    // Mount the shared folder
                    std::string mountCommand = "sudo mount -t cifs " + sharedFolderPath + " " + mountPoint +
                                                " -o username=" + username + ",password=" + password;
                    int mountStatus = std::system(mountCommand.c_str());
                    if (mountStatus != 0)
                    {
                        Logger::getInstance()->FnLog(("Failed to mount " + mountPoint), "", "OPR");
                    }
                    else
                    {
                        Logger::getInstance()->FnLog(("Successfully to mount " + mountPoint), "", "OPR");

                        // Copy files to mount folder
                        for (const auto& entry : std::filesystem::directory_iterator(LCSCSettleFilePath))
                        {
                            if ((entry.path().filename().string().find(LCSCSettleFormattedDate) == std::string::npos) &&
                                (entry.path().extension() == ".lcs"))
                            {
                                std::error_code ec;
                                std::filesystem::copy(entry.path(), mountPoint / entry.path().filename(), std::filesystem::copy_options::overwrite_existing, ec);

                                if (!ec)
                                {
                                    std::stringstream ss;
                                    ss << "Copy file : " << entry.path() << " successfully.";
                                    Logger::getInstance()->FnLog(ss.str(), "", "OPR");
                                    
                                    std::filesystem::remove(entry.path());
                                    ss.str("");
                                    ss << "Removed log file : " << entry.path() << " successfully";
                                    Logger::getInstance()->FnLog(ss.str(), "", "OPR");
                                }
                                else
                                {
                                    std::stringstream ss;
                                    ss << "Failed to copy log file : " << entry.path();
                                    Logger::getInstance()->FnLog(ss.str(), "", "OPR");
                                }
                            }
                        }
                    }
                }
                catch (const std::filesystem::filesystem_error& e)
                {
                    std::stringstream ss;
                    ss << __func__ << ", Exception: " << e.what();
                    Logger::getInstance()->FnLogExceptionError(ss.str());
                }
                catch (const std::exception& e)
                {
                    std::stringstream ss;
                    ss << __func__ << ", Exception: " << e.what();
                    Logger::getInstance()->FnLogExceptionError(ss.str());
                }
                catch (...)
                {
                    std::stringstream ss;
                    ss << __func__ << ", Exception: Unknown Exception";
                    Logger::getInstance()->FnLogExceptionError(ss.str());
                }

                try
                {
                    // Unmount the shared folder
                    std::string unmountCommand = "sudo umount " + mountPoint;
                    int unmountStatus = std::system(unmountCommand.c_str());
                    if (unmountStatus != 0)
                    {
                        Logger::getInstance()->FnLog(("Failed to unmount " + mountPoint), "", "OPR");
                    }
                    else
                    {
                        Logger::getInstance()->FnLog(("Successfully to unmount " + mountPoint), "", "OPR");
                    }
                }
                catch (const std::filesystem::filesystem_error& e)
                {
                    std::stringstream ss;
                    ss << __func__ << ", Unmount Exception: " << e.what();
                    Logger::getInstance()->FnLogExceptionError(ss.str());
                }
                catch (const std::exception& e)
                {
                    std::stringstream ss;
                    ss << __func__ << ", Unmount Exception: " << e.what();
                    Logger::getInstance()->FnLogExceptionError(ss.str());
                }
                catch (...)
                {
                    std::stringstream ss;
                    ss << __func__ << ", Unmount Exception: Unknown Exception";
                    Logger::getInstance()->FnLogExceptionError(ss.str());
                }
            }
        }
        else
        {
            Logger::getInstance()->FnLog("Log files failed to upload due to ping failed.", "", "OPR");
        }
    }
    auto end = std::chrono::steady_clock::now(); // Measure the end time of the handler execution
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start); // Calculate the duration of the handler execution

    timer->expires_at(timer->expiry() + boost::asio::chrono::seconds(60) + duration);
    boost::asio::post(*logStrand_, [timer, logStrand_]() {
        timer->async_wait(boost::bind(dailyLogHandler, boost::asio::placeholders::error, timer, logStrand_));
    });
}

void signalHandler(const boost::system::error_code& ec, int signal, boost::asio::io_context& ioContext, boost::asio::signal_set& signals, boost::asio::executor_work_guard<boost::asio::io_context::executor_type>& workGuard)
{
    std::cout << __func__ << std::endl;
    if (!ec)
    {
        Logger::getInstance()->FnLog("Terminal signal received. Station Program terminated.");
        operation::getInstance()->SendMsg2Server("09","11Stopping...");

        // Display Station Stopped on LCD
        std::string LCDLine1Msg = ">>> STN STOPPED <<< ";
        std::string LCDLine2Msg = Common::getInstance()->FnGetDateTimeFormat_ddmmyyy_hhmmss();
        char* sLCDLine1Msg = const_cast<char*>(LCDLine1Msg.data());
        char* sLCDLine2Msg = const_cast<char*>(LCDLine2Msg.data());
        LCD::getInstance()->FnLCDClearDisplayRow(1);
        LCD::getInstance()->FnLCDClearDisplayRow(2);
        LCD::getInstance()->FnLCDDisplayRow(1, sLCDLine1Msg);
        LCD::getInstance()->FnLCDDisplayRow(2, sLCDLine2Msg);
        usleep(500000);

        // Release the work guard to allow io_context to exit
        workGuard.reset();

        // Stop the io_context to allow the run() loop to exit
        ioContext.stop();
    }
}

int main (int agrc, char* argv[])
{
    // Initialization
    boost::asio::io_context ioContext;
    auto workGuard = boost::asio::make_work_guard(ioContext);

    boost::asio::strand<boost::asio::io_context::executor_type> strand_ = boost::asio::make_strand(ioContext);
    boost::asio::strand<boost::asio::io_context::executor_type> logStrand_ = boost::asio::make_strand(ioContext);

    // Create a signal set to handle SIGINT and SIGTERM
    boost::asio::signal_set signals(ioContext, SIGINT, SIGTERM);
    signals.async_wait([&ioContext, &signals, &workGuard] (const boost::system::error_code& ec, int signal) {
        signalHandler(ec, signal, ioContext, signals, workGuard);
    });

    IniParser::getInstance()->FnReadIniFile();
    Logger::getInstance()->FnCreateLogFile();

    // Start heartbeat
    HeartbeatUdpServer heartbeatUdpServer_(ioContext, "127.0.0.1", 6000);
    heartbeatUdpServer_.start();

    Common::getInstance()->FnLogExecutableInfo(argv[0]);
    SystemInfo::getInstance()->FnLogSysInfo();
    EventManager::getInstance()->FnRegisterEvent(std::bind(&EventHandler::FnHandleEvents, EventHandler::getInstance(), std::placeholders::_1, std::placeholders::_2));
    EventManager::getInstance()->FnStartEventThread();
    operation::getInstance()->OperationInit(ioContext);

    // Start daily process timer
    boost::asio::steady_timer dailyProcessTimer(strand_, boost::asio::chrono::seconds(1));
    dailyProcessTimer.async_wait(boost::bind(dailyProcessTimerHandler, boost::asio::placeholders::error, &dailyProcessTimer, &strand_));

    // Start daily log timer
    boost::asio::steady_timer dailyLogTimer(logStrand_, boost::asio::chrono::seconds(1));
    dailyLogTimer.async_wait(boost::bind(dailyLogHandler, boost::asio::placeholders::error, &dailyLogTimer, &logStrand_));

    // Create a pool of threads to run the io_context
    std::vector<std::thread> threadPool;
    const int numThreads = 6;

    for (int i = 0; i < numThreads; i++)
    {
        threadPool.emplace_back([&ioContext]() {
            ioContext.run();
        });
    }

    // Join all threads
    for (auto& thread: threadPool)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    // Perform cleanup actions after all threads have joined
    EventManager::getInstance()->FnStopEventThread();
    Upt::getInstance()->FnUptClose();
    KSM_Reader::getInstance()->FnKSMReaderClose();
    LCSCReader::getInstance()->FnLCSCReaderClose();
    Printer::getInstance()->FnPrinterClose();
    Lpr::getInstance()->FnLprClose();
    //EEPClient::getInstance()->FnEEPClientClose();

    return 0;
}
