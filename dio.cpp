#include <functional>
#include <sstream>
#include <thread>
#include "dio.h"
#include "event_manager.h"
#include "gpio.h"
#include "ini_parser.h"
#include "log.h"

DIO* DIO::dio_ = nullptr;

DIO::DIO()
    : loop_a_di_(0),
    loop_b_di_(0),
    loop_c_di_(0),
    intercom_di_(0),
    station_door_open_di_(0),
    barrier_door_open_di_(0),
    barrier_status_di_(0),
    open_barrier_do_(0),
    lcd_backlight_do_(0),
    loop_a_di_last_val_(0),
    loop_b_di_last_val_(0),
    loop_c_di_last_val_(0),
    intercom_di_last_val_(0),
    station_door_open_di_last_val_(0),
    barrier_door_open_di_last_val_(0),
    barrier_status_di_last_val_(0),
    isDIOMonitoringThreadRunning_(false)
{
    logFileName_ = "dio";
}

DIO* DIO::getInstance()
{
    if (dio_ == nullptr)
    {
        dio_ = new DIO();
    }

    return dio_;
}

void DIO::FnDIOInit()
{
    loop_a_di_ = getInputPinNum(IniParser::getInstance()->FnGetLoopA());
    loop_b_di_ = getInputPinNum(IniParser::getInstance()->FnGetLoopB());
    loop_c_di_ = getInputPinNum(IniParser::getInstance()->FnGetLoopC());
    intercom_di_ = getInputPinNum(IniParser::getInstance()->FnGetIntercom());
    station_door_open_di_ = getInputPinNum(IniParser::getInstance()->FnGetStationDooropen());
    barrier_door_open_di_ = getInputPinNum(IniParser::getInstance()->FnGetBarrierDooropen());
    barrier_status_di_ = getInputPinNum(IniParser::getInstance()->FnGetBarrierStatus());
    open_barrier_do_ = getOutputPinNum(IniParser::getInstance()->FnGetOpenbarrier());
    lcd_backlight_do_ = getOutputPinNum(IniParser::getInstance()->FnGetLCDbacklight());

    Logger::getInstance()->FnCreateLogFile(logFileName_);
    Logger::getInstance()->FnLog("DIO initialization completed.", logFileName_, "DIO");
}

void DIO::FnStartDIOMonitoring()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    if (!isDIOMonitoringThreadRunning_)
    {
        isDIOMonitoringThreadRunning_ = true;
        dioMonitoringThread_ = std::thread(&DIO::monitoringDIOChangeThreadFunction, this);
    }
}

void DIO::FnStopDIOMonitoring()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    if (isDIOMonitoringThreadRunning_)
    {
        isDIOMonitoringThreadRunning_ = false;
        dioMonitoringThread_.join();
    }
}

void DIO::monitoringDIOChangeThreadFunction()
{
    while (isDIOMonitoringThreadRunning_)
    {
        int loop_a_curr_val = GPIOManager::getInstance()->FnGetGPIO(loop_a_di_)->FnGetValue();
        int loop_b_curr_val = GPIOManager::getInstance()->FnGetGPIO(loop_b_di_)->FnGetValue();
        int loop_c_curr_val = GPIOManager::getInstance()->FnGetGPIO(loop_c_di_)->FnGetValue();
        int intercom_curr_val = GPIOManager::getInstance()->FnGetGPIO(intercom_di_)->FnGetValue();
        int station_door_open_curr_val = GPIOManager::getInstance()->FnGetGPIO(station_door_open_di_)->FnGetValue();
        int barrier_door_open_curr_val = GPIOManager::getInstance()->FnGetGPIO(barrier_door_open_di_)->FnGetValue();
        int barrier_status_curr_value = GPIOManager::getInstance()->FnGetGPIO(barrier_status_di_)->FnGetValue();
        
        if (loop_a_curr_val == GPIOManager::GPIO_HIGH && loop_a_di_last_val_ == GPIOManager::GPIO_LOW)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_A_EVENT));
        }

        if (loop_b_curr_val == GPIOManager::GPIO_HIGH && loop_b_di_last_val_ == GPIOManager::GPIO_LOW)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_B_EVENT));
        }
        
        if (loop_c_curr_val == GPIOManager::GPIO_HIGH && loop_c_di_last_val_ == GPIOManager::GPIO_LOW)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_C_EVENT));
        }
        
        if (intercom_curr_val == GPIOManager::GPIO_HIGH && intercom_di_last_val_ == GPIOManager::GPIO_LOW)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::INTERCOM_EVENT));
        }
        
        if (station_door_open_curr_val == GPIOManager::GPIO_HIGH && station_door_open_di_last_val_ == GPIOManager::GPIO_LOW)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::STATION_DOOR_OPEN_EVENT));
        }
        
        if (barrier_door_open_curr_val == GPIOManager::GPIO_HIGH && barrier_door_open_di_last_val_ == GPIOManager::GPIO_LOW)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::BARRIER_DOOR_OPEN_EVENT));
        }
        
        if (barrier_status_curr_value == GPIOManager::GPIO_HIGH && barrier_status_di_last_val_ == GPIOManager::GPIO_LOW)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::BARRIER_STATUS_EVENT));
        }

        loop_a_di_last_val_ = loop_a_curr_val;
        loop_b_di_last_val_ = loop_b_curr_val;
        loop_c_di_last_val_ = loop_c_curr_val;
        intercom_di_last_val_ = intercom_curr_val;
        station_door_open_di_last_val_ = station_door_open_curr_val;
        barrier_door_open_di_last_val_ = barrier_door_open_curr_val;
        barrier_status_di_last_val_ = barrier_status_curr_value;

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void DIO::FnSetOpenBarrier(int value)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    GPIOManager::getInstance()->FnGetGPIO(open_barrier_do_)->FnSetValue(value);
}

int DIO::FnGetOpenBarrier() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return GPIOManager::getInstance()->FnGetGPIO(open_barrier_do_)->FnGetValue();
}

void DIO::FnSetLCDBacklight(int value)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    GPIOManager::getInstance()->FnGetGPIO(lcd_backlight_do_)->FnSetValue(value);
}

int DIO::FnGetLCDBacklight() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return GPIOManager::getInstance()->FnGetGPIO(lcd_backlight_do_)->FnGetValue();
}

int DIO::getInputPinNum(int pinNum)
{
    int ret = 0;

    switch (pinNum)
    {
        case 1:
            ret = GPIOManager::PIN_DI1;
            break;
        case 2:
            ret = GPIOManager::PIN_DI2;
            break;
        case 3:
            ret = GPIOManager::PIN_DI3;
            break;
        case 4:
            ret = GPIOManager::PIN_DI4;
            break;
        case 5:
            ret = GPIOManager::PIN_DI5;
            break;
        case 6:
            ret = GPIOManager::PIN_DI6;
            break;
        case 7:
            ret = GPIOManager::PIN_DI7;
            break;
        case 8:
            ret = GPIOManager::PIN_DI8;
            break;
        case 9:
            ret = GPIOManager::PIN_DI9;
            break;
        case 10:
            ret = GPIOManager::PIN_DI10;
            break;
        case 11:
            ret = GPIOManager::PIN_DI11;
            break;
        case 12:
            ret = GPIOManager::PIN_DI12;
            break;
        default:
            ret = 0;
            break;
    }

    return ret;
}

int DIO::getOutputPinNum(int pinNum)
{
    int ret = 0;

    switch (pinNum)
    {
        case 1:
            ret = GPIOManager::PIN_DO1;
            break;
        case 2:
            ret = GPIOManager::PIN_DO2;
            break;
        case 3:
            ret = GPIOManager::PIN_DO3;
            break;
        case 4:
            ret = GPIOManager::PIN_DO4;
            break;
        case 5:
            ret = GPIOManager::PIN_DO5;
            break;
        case 6:
            ret = GPIOManager::PIN_DO6;
            break;
        case 7:
            ret = GPIOManager::PIN_DO7;
            break;
        case 8:
            ret = GPIOManager::PIN_DO8;
            break;
        case 9:
            ret = GPIOManager::PIN_DO9;
            break;
        case 10:
            ret = GPIOManager::PIN_DO10;
            break;
        default:
            ret = 0;
            break;
    }

    return ret;
}