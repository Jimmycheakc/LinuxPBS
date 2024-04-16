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
#include "test.h"
#include "upt.h"
#include "event_manager.h"
#include "event_handler.h"
#include "lcsc.h"
#include "db.h"
#include "odbc.h"
#include "structuredata.h"
#include "operation.h"
#include "udp.h"


void dailyProcessTimerHandler(const boost::system::error_code &ec, boost::asio::steady_timer * timer, boost::asio::io_context* io)
{
    auto start = std::chrono::steady_clock::now(); // Measure the start time of the handler execution

    //Sync PMS time counter
     operation:: getInstance()->tProcess.giSyncTimeCnt = operation:: getInstance()->tProcess.giSyncTimeCnt+ 1;
    //------ timer process start
    if (operation::getInstance()->FnIsOperationInitialized()) {

        if (operation::getInstance()->tProcess.gbLoopApresent == false) {
            if (operation::getInstance()->tProcess.giSystemOnline = 0 and operation::getInstance()->tProcess.glNoofOfflineData > 0) {
                db::getInstance()->moveOfflineTransToCentral();
            }
            // Sysnc time from PMS per hour
            if (operation::getInstance()->tProcess.giSyncTimeCnt > 6*60 ) {
                db::getInstance()->synccentraltime();
                operation::getInstance()->tProcess.giSyncTimeCnt = 0;
                //-----
                operation::getInstance()->CheckReader();
            } 
            // check central DB
            if (operation::getInstance()->tProcess.giSystemOnline != 0) {
                
                
            }
            LCSCReader::getInstance()->FnUploadLCSCCDFiles();
            //---- clear expired season
            if (operation::getInstance()->tProcess.giLastHousekeepingDate != Common::getInstance()->FnGetCurrentDay()){
                db::getInstance()->clearexpiredseason();
                operation::getInstance()->tProcess.giLastHousekeepingDate = Common::getInstance()->FnGetCurrentDay();
            }
        }
        // Send DateTime to Monitor
        operation::getInstance()->FnSendDateTimeToMonitor();
    } else {
        if (operation::getInstance()->tProcess.gbInitParamFail==1) {
            if (operation::getInstance()->LoadedparameterOK())
            {
                operation::getInstance()->tProcess.gbInitParamFail = 0;
                operation::getInstance()->Initdevice(*(operation::getInstance()->iCurrentContext));
                operation::getInstance()->isOperationInitialized_.store(true);
                operation::getInstance()->ShowLEDMsg(operation::getInstance()->tMsg.Msg_DefaultLED[0], operation::getInstance()->tMsg.Msg_DefaultLED[1]);
                operation::getInstance()->writelog("EPS in operation","OPR");
            }
        }
    }

    //--------
    auto end = std::chrono::steady_clock::now(); // Measure the end time of the handler execution
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start); // Calculate the duration of the handler execution

    timer->expires_at(timer->expiry() + boost::asio::chrono::seconds(10) + duration);
    timer->async_wait(boost::bind(dailyProcessTimerHandler, boost::asio::placeholders::error, timer, io));
}

void dailyLogHandler(const boost::system::error_code &ec, boost::asio::steady_timer * timer)
{
    auto start = std::chrono::steady_clock::now(); // Measure the start time of the handler execution

    // Get today's date
    auto today = std::chrono::system_clock::now();
    auto todayDate = std::chrono::system_clock::to_time_t(today);
    std::tm* localToday = std::localtime(&todayDate);

    // Check if it's past 12 AM (midnight)
    if (localToday->tm_hour == 0 && localToday->tm_min >= 0 && localToday->tm_min < 30)
    {
        std::string logFilePath = Logger::getInstance()->LOG_FILE_PATH;

        // Extract year, month and day
        std::ostringstream ossToday;
        ossToday << std::setw(2) << std::setfill('0') << (localToday->tm_year % 100);
        ossToday << std::setw(2) << std::setfill('0') << (localToday->tm_mon + 1);
        ossToday << std::setw(2) << std::setfill('0') << localToday->tm_mday;

        std::string todayDateStr = ossToday.str();

        // Iterate through the files in the log file path
        int foundNo_ = 0;
        for (const auto& entry : std::filesystem::directory_iterator(logFilePath))
        {
            if ((entry.path().filename().string().find(todayDateStr) == std::string::npos) &&
                (entry.path().extension() == ".log"))
            {
                foundNo_ ++;
            }
        }

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
            }

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
    }
    
    auto end = std::chrono::steady_clock::now(); // Measure the end time of the handler execution
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start); // Calculate the duration of the handler execution

    timer->expires_at(timer->expiry() + boost::asio::chrono::seconds(60) + duration);
    timer->async_wait(boost::bind(dailyLogHandler, boost::asio::placeholders::error, timer));
}

int main (int agrc, char* argv[])
{
    // Initialization
    boost::asio::io_context ioContext;
    boost::asio::io_context dailyLogIoContext;

    IniParser::getInstance()->FnReadIniFile();
    Logger::getInstance();
    Common::getInstance()->FnLogExecutableInfo(argv[0]);
    SystemInfo::getInstance()->FnLogSysInfo();
   //
    EventManager::getInstance()->FnRegisterEvent(std::bind(&EventHandler::FnHandleEvents, EventHandler::getInstance(), std::placeholders::_1, std::placeholders::_2));
    EventManager::getInstance()->FnStartEventThread();
    operation::getInstance()->OperationInit(ioContext);

    boost::asio::steady_timer dailyProcessTimer(ioContext, boost::asio::chrono::seconds(1));
    dailyProcessTimer.async_wait(boost::bind(dailyProcessTimerHandler, boost::asio::placeholders::error, &dailyProcessTimer, &ioContext));

    std::thread dailyLogThread([&dailyLogIoContext] ()
    {
        boost::asio::steady_timer dailyLogTimer(dailyLogIoContext, boost::asio::chrono::seconds(1));
        dailyLogTimer.async_wait(boost::bind(dailyLogHandler, boost::asio::placeholders::error, &dailyLogTimer));
        dailyLogIoContext.run();
    });

    ioContext.run();
    
#ifdef BUILD_TEST
    Test::getInstance()->FnTest(argv[0]);
    return 0;
#endif

    EventManager::getInstance()->FnStopEventThread();

    return 0;
}
