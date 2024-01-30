#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
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
#include "antenna.h"
#include "event_manager.h"
#include "event_handler.h"
#include "lcsc.h"
#include "db.h"
#include "odbc.h"
#include "structuredata.h"
#include "operation.h"
#include "udp.h"

int main (int agrc, char* argv[])
{
    // Initialization
    io_service ioService;

    IniParser::getInstance()->FnReadIniFile();
    Logger::getInstance();
    SystemInfo::getInstance()->FnLogSysInfo();
    
    GPIOManager::getInstance()->FnGPIOInit();
    LCD::getInstance()->FnLCDInit();

    // get central DB info from local DB

    // init operation 
    operation::getInstance()->OperationInit(ioService);
   
    EventManager::getInstance()->FnRegisterEvent(std::bind(&EventHandler::FnHandleEvents, EventHandler::getInstance(), std::placeholders::_1, std::placeholders::_2));
    EventManager::getInstance()->FnStartEventThread();
    //Antenna::getInstance()->FnAntennaInit(19200, "/dev/ttyCH9344USB5");
    //LCSCReader::getInstance()->FnLCSCReaderInit(115200, "/dev/ttyCH9344USB4");
    ioService.run();
    
#ifdef BUILD_TEST
    Test::getInstance()->FnTest(argv[0]);
    return 0;
#endif

    EventManager::getInstance()->FnStopEventThread();

    return 0;
}