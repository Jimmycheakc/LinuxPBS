#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include "antenna.h"
#include "boost/asio.hpp"
#include "crc.h"
#include "common.h"
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
#include "dio.h"

void timerHandler(const boost::system::error_code& /*ec*/, boost::asio::steady_timer& timer) {
    
    std::cout << "15 Minutes Timer expired!" << std::endl;
    
    operation::getInstance()->m_db->insertbroadcasttrans("1","1128436045","1111900023458790","0","2");
    
    operation::getInstance()->tEntry.esid = "1";
    operation::getInstance()->tEntry.sIUTKNo = "1122944109";
    operation::getInstance()->tEntry.iCardType = 0;
    operation::getInstance()->tEntry.iStatus = 0;
    operation::getInstance()->tEntry.iTransType= 2;
    operation::getInstance()->tEntry.sLPN[0]="SJP2716C";
    operation::getInstance()->m_db->insertentrytrans(operation::getInstance()->tEntry);


    // Restart the timer for the next expiration
    timer.expires_from_now(boost::asio::chrono::minutes(15));
    timer.async_wait([&timer](const boost::system::error_code& ec) {
        timerHandler(ec, timer);
    });
}

int main (int agrc, char* argv[])
{
    // Initialization
    //boost::asio::io_service ioService;

    //IniParser::getInstance()->FnReadIniFile();
    //Logger::getInstance();
    //SystemInfo::getInstance()->FnLogSysInfo();
    
    //GPIOManager::getInstance()->FnGPIOInit();
    //LCD::getInstance()->FnLCDInit();

    // get central DB info from local DB

    // init operation 
    //operation::getInstance()->OperationInit(ioService);

    //boost::asio::steady_timer timer(ioService, boost::asio::chrono::minutes(15));
    //timer.async_wait([&timer](const boost::system::error_code& ec) {
    //    timerHandler(ec, timer);
    //});
    //EventManager::getInstance()->FnRegisterEvent(std::bind(&EventHandler::FnHandleEvents, EventHandler::getInstance(), std::placeholders::_1, std::placeholders::_2));
    //EventManager::getInstance()->FnStartEventThread();
    //DIO::getInstance()->FnDIOInit();
    //DIO::getInstance()->FnStartDIOMonitoring();
    //Antenna::getInstance()->FnAntennaInit(19200, "/dev/ttyCH9344USB5");
    //LCSCReader::getInstance()->FnLCSCReaderInit(115200, "/dev/ttyCH9344USB4");
    //ioService.run();
    
#ifdef BUILD_TEST
    Test::getInstance()->FnTest(argv[0]);
    return 0;
#endif

    //EventManager::getInstance()->FnStopEventThread();

    return 0;
}