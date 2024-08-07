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
#include "antenna.h"
#include "event_manager.h"
#include "event_handler.h"
#include "lcsc.h"
#include "db.h"
#include "structuredata.h"
#include "dio.h"

#include "boost/date_time/posix_time/posix_time.hpp"

Test* Test::test_ = nullptr;
std::mutex Test::mutex_;

Test::Test()
{

}

Test* Test::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (test_ == nullptr)
    {
        test_ = new Test();
    }
    return test_;
}

void Test::FnTest(char* argv)
{
    //iniparser_test();
    //common_test(argv);
    //systeminfo_test();
    //crc_test();
    //gpio_test();
    //upt_test();
    //lcd_test();
    //logger_test();
    //led614_test();
    //led216_test();
    //led226_test();
    //antenna_test();
    //event_queue_test();
    //lcsc_reader_test();
    //db_test();
    //dio_test();
}

void Test::db_test()
{
    /*  db *m_db;
    m_db=new db("DSN={MariaDB-server};DRIVER={MariaDB ODBC 3.0 Driver};SERVER=127.0.0.1;PORT=3306;DATABASE=linux_pbs;UID=linuxpbs;PWD=SJ2001;","DSN=mssqlserver;DATABASE=RF;UID=sa;PWD=yzhh2007","192.168.2.47",10,2,2,2);

    int ret = m_db->isvalidseason("1128436044");
    
    std::stringstream ss;

    if (ret != 1) {
        ss << "IU is not valid season" ;
        Logger::getInstance()->FnLog(ss.str(), "", "TEST");
    } else
    {
        ss << "IU is valid season" ;
        Logger::getInstance()->FnLog(ss.str(), "", "TEST");
    }

    m_db->insertbroadcasttrans("1","1128436045","1111900023458790","0","2");
    
    tEntryTrans_Struct tEntry; 
    
    tEntry.esid = "1";
    tEntry.sIUTKNo = "1122944109";
    tEntry.iCardType = 0;
    tEntry.iStatus = 0;
    tEntry.iTransType= 2;
    tEntry.sLPN[0]="SJP2716C";
    m_db->insertentrytrans(tEntry);

    m_db->synccentraltime();

  //  m_db->downloadseason();
  //  m_db->downloadvehicletype();
  //  m_db->downloadledmessage();
      m_db->downloadparameter();
      m_db->downloadstationsetup();

*/
}

void Test::common_test(char* argv)
{
    Common::getInstance()->FnLogExecutableInfo(argv);
}

void Test::systeminfo_test()
{
    SystemInfo::getInstance()->FnLogSysInfo();
}

void Test::iniparser_test()
{
    IniParser::getInstance()->FnReadIniFile();

    /*
    IniParser::getInstance()->FnPrintIniFile();
    std::cout << IniParser::getInstance()->FnGetStationID() << std::endl;
    std::cout << IniParser::getInstance()->FnGetLogFolder() << std::endl;
    std::cout << IniParser::getInstance()->FnGetLocalDB() << std::endl;
    std::cout << IniParser::getInstance()->FnGetLocalUDPPort() << std::endl;
    std::cout << IniParser::getInstance()->FnGetRemoteUDPPort() << std::endl;
    std::cout << IniParser::getInstance()->FnGetSeasonOnly() << std::endl;
    std::cout << IniParser::getInstance()->FnGetNotAllowHourly() << std::endl;
    std::cout << IniParser::getInstance()->FnGetLPRIP4Front() << std::endl;
    std::cout << IniParser::getInstance()->FnGetLPRIP4Rear() << std::endl;
    std::cout << IniParser::getInstance()->FnGetWaitLPRNoTime() << std::endl;
    std::cout << IniParser::getInstance()->FnGetLPRErrorTime() << std::endl;
    std::cout << IniParser::getInstance()->FnGetLPRErrorCount() << std::endl;
    std::cout << IniParser::getInstance()->FnGetBlockIUPrefix() << std::endl;
    std::cout << IniParser::getInstance()->FnGetLoopA() << std::endl;
    std::cout << IniParser::getInstance()->FnGetLoopC() << std::endl;
    std::cout << IniParser::getInstance()->FnGetLoopB() << std::endl;
    std::cout << IniParser::getInstance()->FnGetIntercom() << std::endl;
    std::cout << IniParser::getInstance()->FnGetStationDooropen() << std::endl;
    std::cout << IniParser::getInstance()->FnGetBarrierDooropen() << std::endl;
    std::cout << IniParser::getInstance()->FnGetBarrierStatus() << std::endl;
    std::cout << IniParser::getInstance()->FnGetOpenbarrier() << std::endl;
    std::cout << IniParser::getInstance()->FnGetLCDbacklight() << std::endl;
    */

}

void Test::crc_test()
{

}

void Test::gpio_test()
{
    /*
    GPIOManager::getInstance()->FnGPIOInit();
    GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DO1)->FnSetValue(GPIOManager::GPIO_HIGH);
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DO1)->FnGetValue() << std::endl;
    GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DO2)->FnSetValue(GPIOManager::GPIO_HIGH);
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DO2)->FnGetValue() << std::endl;
    GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DO3)->FnSetValue(GPIOManager::GPIO_HIGH);
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DO3)->FnGetValue() << std::endl;
    GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DO4)->FnSetValue(GPIOManager::GPIO_HIGH);
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DO4)->FnGetValue() << std::endl;
    GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DO5)->FnSetValue(GPIOManager::GPIO_HIGH);
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DO5)->FnGetValue() << std::endl;
    GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DO6)->FnSetValue(GPIOManager::GPIO_HIGH);
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DO6)->FnGetValue() << std::endl;
    
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DI1)->FnGetValue() << std::endl;
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DI2)->FnGetValue() << std::endl;
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DI3)->FnGetValue() << std::endl;
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DI4)->FnGetValue() << std::endl;
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DI5)->FnGetValue() << std::endl;
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DI6)->FnGetValue() << std::endl;
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DI7)->FnGetValue() << std::endl;
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DI8)->FnGetValue() << std::endl;
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DI9)->FnGetValue() << std::endl;
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DI11)->FnGetValue() << std::endl;
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DI12)->FnGetValue() << std::endl;
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DI13)->FnGetValue() << std::endl;
    
    GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DOJ1)->FnSetValue(GPIOManager::GPIO_HIGH);
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DOJ1)->FnGetValue() << std::endl;
    GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DOJ2)->FnSetValue(GPIOManager::GPIO_HIGH);
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DOJ2)->FnGetValue() << std::endl;
    GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DOJ3)->FnSetValue(GPIOManager::GPIO_HIGH);
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DOJ3)->FnGetValue() << std::endl;
    GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DOJ4)->FnSetValue(GPIOManager::GPIO_HIGH);
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DOJ4)->FnGetValue() << std::endl;
    GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DOJ7)->FnSetValue(GPIOManager::GPIO_HIGH);
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DOJ7)->FnGetValue() << std::endl;
    GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DOJ8)->FnSetValue(GPIOManager::GPIO_HIGH);
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DOJ8)->FnGetValue() << std::endl;
    GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DOJ9)->FnSetValue(GPIOManager::GPIO_HIGH);
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DOJ9)->FnGetValue() << std::endl;
    GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DOJ10)->FnSetValue(GPIOManager::GPIO_HIGH);
    std::cout << GPIOManager::getInstance()->FnGetGPIO(GPIOManager::PIN_DOJ10)->FnGetValue() << std::endl;
    */
}

void Test::upt_test()
{

}

void Test::lcd_test_ascii()
{
    int count = 1;
    int buf_size = 5;
    char buf[buf_size];
        
    for (int i = 16; i <= 255; i++)
    {
        LCD::getInstance()->FnLCDDisplayCharacter(i);
        
        if (count == 16)
        {
            count = 1;
            usleep(2000000);
            LCD::getInstance()->FnLCDClear();
            usleep(250000);
        }
        else
        {
            count++;
        }
    }
}

void Test::lcd_test_display_two_rows()
{
    char str1[] = "  CAR PARK SYSTEMS  ";
    char str2[] = "____SUN SINGAPORE___";
    
    LCD::getInstance()->FnLCDClear();

    LCD::getInstance()->FnLCDDisplayRow(1, str1);
    LCD::getInstance()->FnLCDDisplayRow(2, str2);
    usleep(2000000);
    LCD::getInstance()->FnLCDClear();
}

void Test::lcd_test_row_string()
{
    char test_str[50][100] = 
    {
        "10.30pm to 7am Season Onlys",
        "Others Pls Exit in 15 mins",
        "Pls Scan Your Ticket",
        "Transaction Cancelled",
        "Debit Failed",
        "NETS Payment Approved",
        "Authorised Vehicle^Please Proceed",
        "Card Error ^Press Intercom For Help",
        "Ticket Cancelled",
        "Pls Scan Your Ticket",
        "Please Insert Entry Cashcard",
        "Debit Certificate Problem. Please Refer to NETS with Cashcard and Receipt",
        "Debit Failed",
        "Please Wait... ^ ",
        "Paid Already. Use Exit Ticket ",
        "Card Reading Problem. Please Try Again",
        "Card Sold Out|Cashcard Sold Out. Please Try Again Later",
        "  ^ Please Proceed",
        "CarFull",
        "CP FULL Pls Exit^within 15 mins",
        "Card Error ^Press Intercom",
        "Please Wait......",
        "Paid Already. Use Exit Ticket",
        "Card Reading Problem. Please Try Again",
        "Card Sold Out|Cashcard Sold Out. Please Try Again Later",
        "   Please Proceed",
        "Please Validate at ESS",
        "Expired. Press Help",
        "Complimentary Parking",
        "Validation Failed, Please Try Again",
        "Complimentary Validation Success",
        "Database Error. Please Call for Help",
        "Default IU Insert ^ Card in Reader",
        "Card Jam. Please Call for Help",
        "Please Take Your Cashcard",
        "Dispenser Error|Card Dispenser Error. Please Try Again Later",
        "Please wait for Cashcard?",
        "Season Exit^Have A Nice Day",
        "Welcome to A87",
        "Have A Nice Day !",
        "Authorised Vehicle^Please Proceed",
        "Fee: 0.00^Have A Nice Day",
        "Car Park FULL ^Please Exit within 15 mins",
        "Please Key-in Your Pin Number Follow by Enter Key",
        "Season Exit^Have A Nice Day",
        "No IU Detected^Pls Insert/Tap Card",
        "Invalid Ticket, Press Help",
        "Fleet Card",
        "DefaultMsg",
        "Please Key-in Your Vehicle Number"
    };
    
    for (int i = 0; i < 50; i++)
    {
        test_str[i][strlen(test_str[i]) + 1] = '\0';
        printf("%s\n", test_str[i]);
        LCD::getInstance()->FnLCDClear();
        usleep(250000);
        LCD::getInstance()->FnLCDDisplayRow(1, test_str[i]);
        usleep(2000000);
    }
    LCD::getInstance()->FnLCDClear();
}

void Test::lcd_test_center_string()
{
    char test_str[6][20] = 
    {
        "Ticket Cancelled",
        "Debit Failed",
        "Please Wait... ^ ",
        "CarFull",
        "Fleet Card",
        "DefaultMsg",
    };
    
    for (int i = 0; i < 6; i++)
    {
        test_str[i][strlen(test_str[i]) + 1] = '\0';
        printf("%s\n", test_str[i]);
        LCD::getInstance()->FnLCDClear();
        usleep(250000);
        LCD::getInstance()->FnLCDDisplayStringCentered(1, test_str[i]);
        usleep(2000000);
    }
    LCD::getInstance()->FnLCDClear();
}
void Test::lcd_test_whole_string()
{
    char test_str[50][100] = 
    {
        "10.30pm to 7am Season Onlys",
        "Others Pls Exit in 15 mins",
        "Pls Scan Your Ticket",
        "Transaction Cancelled",
        "Debit Failed",
        "NETS Payment Approved",
        "Authorised Vehicle^Please Proceed",
        "Card Error ^Press Intercom For Help",
        "Ticket Cancelled",
        "Pls Scan Your Ticket",
        "Please Insert Entry Cashcard",
        "Debit Certificate Problem. Please Refer to NETS with Cashcard and Receipt",
        "Debit Failed",
        "Please Wait... ^ ",
        "Paid Already. Use Exit Ticket ",
        "Card Reading Problem. Please Try Again",
        "Card Sold Out|Cashcard Sold Out. Please Try Again Later",
        "  ^ Please Proceed",
        "CarFull",
        "CP FULL Pls Exit^within 15 mins",
        "Card Error ^Press Intercom",
        "Please Wait......",
        "Paid Already. Use Exit Ticket",
        "Card Reading Problem. Please Try Again",
        "Card Sold Out|Cashcard Sold Out. Please Try Again Later",
        "   Please Proceed",
        "Please Validate at ESS",
        "Expired. Press Help",
        "Complimentary Parking",
        "Validation Failed, Please Try Again",
        "Complimentary Validation Success",
        "Database Error. Please Call for Help",
        "Default IU Insert ^ Card in Reader",
        "Card Jam. Please Call for Help",
        "Please Take Your Cashcard",
        "Dispenser Error|Card Dispenser Error. Please Try Again Later",
        "Please wait for Cashcard?",
        "Season Exit^Have A Nice Day",
        "Welcome to A87",
        "Have A Nice Day !",
        "Authorised Vehicle^Please Proceed",
        "Fee: 0.00^Have A Nice Day",
        "Car Park FULL ^Please Exit within 15 mins",
        "Please Key-in Your Pin Number Follow by Enter Key",
        "Season Exit^Have A Nice Day",
        "No IU Detected^Pls Insert/Tap Card",
        "Invalid Ticket, Press Help",
        "Fleet Card",
        "DefaultMsg",
        "Please Key-in Your Vehicle Number"
    };
    
    for (int i = 0; i < 50; i++)
    {
        test_str[i][strlen(test_str[i]) + 1] = '\0';
        printf("%s\n", test_str[i]);
        LCD::getInstance()->FnLCDClear();
        usleep(250000);
        LCD::getInstance()->FnLCDDisplayScreen(test_str[i]);
        usleep(2000000);
    }
    LCD::getInstance()->FnLCDClear();
}

void Test::lcd_test()
{
    LCD::getInstance()->FnLCDInit();
    lcd_test_ascii();
    lcd_test_display_two_rows();
    lcd_test_row_string();
    lcd_test_center_string();
    lcd_test_whole_string();
}

void Test::logger_test()
{
    boost::asio::io_context io_context;
    boost::asio::io_context::strand strand(io_context);

    Logger::getInstance()->FnCreateLogFile("timer1");
    Logger::getInstance()->FnLog("Timer 1 created", "timer1", "Option");
    Logger::getInstance()->FnCreateLogFile("timer2");
    Logger::getInstance()->FnLog("Timer 2 created", "timer2", "Option");
    Logger::getInstance()->FnCreateLogFile("timer3");
    Logger::getInstance()->FnLog("Timer 3 created", "timer3", "Option");

    boost::asio::steady_timer timer1(io_context, boost::asio::chrono::seconds(10));
    boost::asio::steady_timer timer2(io_context, boost::asio::chrono::seconds(10));
    boost::asio::steady_timer timer3(io_context, boost::asio::chrono::seconds(10));

    timer1.async_wait(strand.wrap([&](const boost::system::error_code& /*error*/) {
        Logger::getInstance()->FnLog("Timer 1 expired", "timer1", "Option");
    }));

    timer2.async_wait(strand.wrap([&](const boost::system::error_code& /*error*/) {
        Logger::getInstance()->FnLog("Timer 2 expired", "timer2", "Option");
    }));

    timer3.async_wait(strand.wrap([&](const boost::system::error_code& /*error*/) {
        Logger::getInstance()->FnLog("Timer 3 expired", "timer3", "Option");
    }));

    io_context.run();
}

void Test::led614_test()
{
    boost::asio::io_context io_context;

    std::string ledId="***";
    std::string msg = "614";
    LEDManager::getInstance()->createLED(io_context, 9600, "/dev/ttyCH9344USB1", 4);
    if (LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1"))
    {
        std::stringstream ss;
        ss << "Serial Port baud rate : ";
        ss << std::to_string(LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnGetLEDBaudRate());
        ss << "Serial Port Name : ";
        ss << LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnGetLEDComPortName();
        ss << "Maximum Character per row : ";
        ss << std::to_string(LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnGetLEDMaxCharPerRow());
        Logger::getInstance()->FnLog(ss.str());

        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::LEFT);
        usleep(2000000);
        msg = "123";
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::RIGHT);
        usleep(2000000);
        msg = "88888";
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::LEFT);
        usleep(2000000);
        msg = "66666";
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::RIGHT);
        usleep(2000000);
        msg = "5566";
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::LEFT);
        usleep(2000000);
        msg = "7788";
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::RIGHT);
        usleep(2000000);
        msg = "ABCD";
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::LEFT);
        usleep(2000000);
        msg = "abcd";
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::RIGHT);
        usleep(2000000);
        msg = "FULL";
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::LEFT);
        usleep(2000000);
        msg = "";
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::LEFT);
        usleep(2000000);
    }

    io_context.run();
}

void Test::led216_test()
{
    boost::asio::io_context io_context;

    std::string ledId="***";
    std::string msg = "";
    LEDManager::getInstance()->createLED(io_context, 9600, "/dev/ttyCH9344USB1", 16);
    if (LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1"))
    {
        std::stringstream ss;
        ss << "Serial Port baud rate : ";
        ss << std::to_string(LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnGetLEDBaudRate());
        ss << "Serial Port Name : ";
        ss << LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnGetLEDComPortName();
        ss << "Maximum Character per row : ";
        ss << std::to_string(LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnGetLEDMaxCharPerRow());
        Logger::getInstance()->FnLog(ss.str());

        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::LEFT);
        usleep(2000000);
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::RIGHT);
        usleep(2000000);
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::CENTER);
        usleep(2000000);

        msg = "Hello World";
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::LEFT);
        usleep(2000000);
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::RIGHT);
        usleep(2000000);
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::CENTER);
        usleep(2000000);

        msg = "Hello World^Wow";
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::LEFT);
        usleep(2000000);
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::RIGHT);
        usleep(2000000);
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::CENTER);
        usleep(2000000);

        msg = "";
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::LEFT);
        usleep(2000000);
    }

    io_context.run();
}

void Test::led226_test()
{
    boost::asio::io_context io_context;

    std::string ledId="***";
    std::string msg = "";
    LEDManager::getInstance()->createLED(io_context, 9600, "/dev/ttyCH9344USB1", 26);
    if (LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1"))
    {
        std::stringstream ss;
        ss << "Serial Port baud rate : ";
        ss << std::to_string(LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnGetLEDBaudRate());
        ss << "Serial Port Name : ";
        ss << LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnGetLEDComPortName();
        ss << "Maximum Character per row : ";
        ss << std::to_string(LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnGetLEDMaxCharPerRow());
        Logger::getInstance()->FnLog(ss.str());

        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::LEFT);
        usleep(2000000);
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::RIGHT);
        usleep(2000000);
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::CENTER);
        usleep(2000000);

        msg = "Hello World";
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::LEFT);
        usleep(2000000);
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::RIGHT);
        usleep(2000000);
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::CENTER);
        usleep(2000000);

        msg = "Hello World^Wow";
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::LEFT);
        usleep(2000000);
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::RIGHT);
        usleep(2000000);
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::CENTER);
        usleep(2000000);

        msg = "";
        LEDManager::getInstance()->getLED("/dev/ttyCH9344USB1")->FnLEDSendLEDMsg(ledId, msg, LED::Alignment::LEFT);
        usleep(2000000);
    }

    io_context.run();
}

void print(const boost::system::error_code& /*e*/)
{
    Logger::getInstance()->FnLog("Hello World");
    Antenna::getInstance()->FnAntennaStopRead();
}

void Test::antenna_test()
{
    boost::asio::io_context io_context;

    IniParser::getInstance()->FnReadIniFile();
    EventManager::getInstance()->FnRegisterEvent(std::bind(&EventHandler::FnHandleEvents, EventHandler::getInstance(), std::placeholders::_1, std::placeholders::_2));
    EventManager::getInstance()->FnStartEventThread();
    Antenna::getInstance()->FnAntennaInit(io_context, 19200, "/dev/ttyCH9344USB5");
    
    std::cout << "start asynchronous operation." << std::endl;

    boost::asio::deadline_timer t(io_context, boost::posix_time::seconds(5));
    t.async_wait(print);

    // Create a thread to run io_context
    std::thread ioThread([&io_context]() {
        io_context.run();
    });

    Antenna::getInstance()->FnAntennaSendReadIUCmd();

    usleep(1000000);
    ioThread.join();
    EventManager::getInstance()->FnStopEventThread();
}

void Test::event_queue_test()
{
    IniParser::getInstance()->FnReadIniFile();
    EventManager::getInstance()->FnRegisterEvent(std::bind(&EventHandler::FnHandleEvents, EventHandler::getInstance(), std::placeholders::_1, std::placeholders::_2));
    EventManager::getInstance()->FnStartEventThread();
    EventManager::getInstance()->FnEnqueueEvent("Evt_AntennaFail", 0);
    EventManager::getInstance()->FnEnqueueEvent("Evt_AntennaPower", true);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    EventManager::getInstance()->FnStopEventThread();
}

void Test::lcsc_reader_test()
{
}

void Test::dio_test()
{
    boost::asio::io_context io_context;

    IniParser::getInstance()->FnReadIniFile();
    GPIOManager::getInstance()->FnGPIOInit();
    DIO::getInstance()->FnDIOInit();
    EventManager::getInstance()->FnRegisterEvent(std::bind(&EventHandler::FnHandleEvents, EventHandler::getInstance(), std::placeholders::_1, std::placeholders::_2));
    EventManager::getInstance()->FnStartEventThread();

    std::cout << "Get barrier status : " << DIO::getInstance()->FnGetOpenBarrier() << std::endl;
    DIO::getInstance()->FnSetOpenBarrier(1);
    std::cout << "Set barrier status 1" << std::endl;
    std::cout << "Get barrier status : " << DIO::getInstance()->FnGetOpenBarrier() << std::endl;
    usleep(1000000);
    DIO::getInstance()->FnSetOpenBarrier(0);
    std::cout << "Set barrier status 0" << std::endl;
    std::cout << "Get barrier status : " << DIO::getInstance()->FnGetOpenBarrier() << std::endl;

    std::cout << "Get LCD backlight status : " << DIO::getInstance()->FnGetLCDBacklight() << std::endl;
    DIO::getInstance()->FnSetLCDBacklight(1);
    std::cout << "Set LCD backlight status 1" << std::endl;
    std::cout << "Get LCD backlight status : " << DIO::getInstance()->FnGetLCDBacklight() << std::endl;
    usleep(1000000);
    DIO::getInstance()->FnSetLCDBacklight(0);
    std::cout << "Set LCD backlight status 0" << std::endl;
    std::cout << "Get LCD backlight status : " << DIO::getInstance()->FnGetLCDBacklight() << std::endl;

    DIO::getInstance()->FnStartDIOMonitoring();

    std::cout << "start asynchronous operation." << std::endl;

    boost::asio::deadline_timer t(io_context, boost::posix_time::seconds(60));
    t.async_wait(print);

    io_context.run();
    
    DIO::getInstance()->FnStopDIOMonitoring();
    EventManager::getInstance()->FnStopEventThread();
}