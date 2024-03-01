#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
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
           
          //  db::getInstance()->moveOfflineTransToCentral();
           
            // Sysnc time from PMS per hour
            if (operation::getInstance()->tProcess.giSyncTimeCnt > 6 * 60 ) {
                db::getInstance()->synccentraltime();
                operation::getInstance()->tProcess.giSyncTimeCnt = 0;
            } 
        }             

        LCSCReader::getInstance()->FnUploadLCSCCDFiles();
        // Send DateTime to Monitor
        operation::getInstance()->FnSendDateTimeToMonitor();
    }

    //--------
    auto end = std::chrono::steady_clock::now(); // Measure the end time of the handler execution
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start); // Calculate the duration of the handler execution

    timer->expires_at(timer->expiry() + boost::asio::chrono::seconds(10) + duration);
    timer->async_wait(boost::bind(dailyProcessTimerHandler, boost::asio::placeholders::error, timer, io));
}

int main (int agrc, char* argv[])
{
    // Initialization
    boost::asio::io_context ioContext;

    IniParser::getInstance()->FnReadIniFile();
    Logger::getInstance();
    SystemInfo::getInstance()->FnLogSysInfo();
   //
    EventManager::getInstance()->FnRegisterEvent(std::bind(&EventHandler::FnHandleEvents, EventHandler::getInstance(), std::placeholders::_1, std::placeholders::_2));
    EventManager::getInstance()->FnStartEventThread();
    operation::getInstance()->OperationInit(ioContext);

    boost::asio::steady_timer dailyProcessTimer(ioContext, boost::asio::chrono::seconds(1));
    dailyProcessTimer.async_wait(boost::bind(dailyProcessTimerHandler, boost::asio::placeholders::error, &dailyProcessTimer, &ioContext));

    ioContext.run();
    
#ifdef BUILD_TEST
    Test::getInstance()->FnTest(argv[0]);
    return 0;
#endif

    EventManager::getInstance()->FnStopEventThread();

    return 0;
}
