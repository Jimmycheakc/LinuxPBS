#include <functional>
#include <sstream>
#include <thread>
#include "dio.h"
#include "event_manager.h"
#include "gpio.h"
#include "ini_parser.h"
#include "log.h"
#include "operation.h"

DIO* DIO::dio_ = nullptr;

DIO::DIO()
    : loop_a_di_(0),
    loop_b_di_(0),
    loop_c_di_(0),
    intercom_di_(0),
    station_door_open_di_(0),
    barrier_door_open_di_(0),
    barrier_status_di_(0),
    manual_open_barrier_di_(0),
    lorry_sensor_di_(0),
    arm_broken_di_(0),
    open_barrier_do_(0),
    lcd_backlight_do_(0),
    close_barrier_do_(0),
    loop_a_di_last_val_(0),
    loop_b_di_last_val_(0),
    loop_c_di_last_val_(0),
    intercom_di_last_val_(0),
    station_door_open_di_last_val_(0),
    barrier_door_open_di_last_val_(0),
    barrier_status_di_last_val_(0),
    manual_open_barrier_di_last_val_(0),
    lorry_sensor_di_last_val_(0),
    arm_broken_di_last_val_(0),
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
    manual_open_barrier_di_ = getInputPinNum(IniParser::getInstance()->FnGetManualOpenBarrier());
    lorry_sensor_di_ = getInputPinNum(IniParser::getInstance()->FnGetLorrysensor());
    arm_broken_di_ = getInputPinNum(IniParser::getInstance()->FnGetArmbroken());
    open_barrier_do_ = getOutputPinNum(IniParser::getInstance()->FnGetOpenbarrier());
    lcd_backlight_do_ = getOutputPinNum(IniParser::getInstance()->FnGetLCDbacklight());
    close_barrier_do_ = getOutputPinNum(IniParser::getInstance()->FnGetclosebarrier());

    Logger::getInstance()->FnCreateLogFile(logFileName_);
    Logger::getInstance()->FnLog("DIO initialization completed.");
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
        int manual_open_barrier_status_curr_value = GPIOManager::getInstance()->FnGetGPIO(manual_open_barrier_di_)->FnGetValue();
        int lorry_sensor_curr_val = GPIOManager::getInstance()->FnGetGPIO(lorry_sensor_di_)->FnGetValue();
        int arm_broken_curr_val = GPIOManager::getInstance()->FnGetGPIO(arm_broken_di_)->FnGetValue();
        
        // Case : Loop A on, Loop B off 
        if ((loop_a_curr_val == GPIOManager::GPIO_HIGH && loop_a_di_last_val_ == GPIOManager::GPIO_LOW)
            && (loop_b_curr_val == GPIOManager::GPIO_LOW && loop_b_di_last_val_ == GPIOManager::GPIO_LOW))
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_A_ON_EVENT));
        }
        // Case : Loop B on, Loop A off
        else if ((loop_b_curr_val == GPIOManager::GPIO_HIGH && loop_b_di_last_val_ == GPIOManager::GPIO_LOW) 
                && (loop_a_curr_val == GPIOManager::GPIO_LOW && loop_a_di_last_val_ == GPIOManager::GPIO_LOW))
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_A_ON_EVENT));
        }
        // Case : Loop A on, Loop B on
        else if ((loop_a_curr_val == GPIOManager::GPIO_HIGH && loop_a_di_last_val_ == GPIOManager::GPIO_LOW)
                && (loop_b_curr_val == GPIOManager::GPIO_HIGH && loop_b_di_last_val_ == GPIOManager::GPIO_LOW))
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_A_ON_EVENT));
        }

        // Case : Loop A off, Loop B off
        if ((loop_a_curr_val == GPIOManager::GPIO_LOW && loop_a_di_last_val_ == GPIOManager::GPIO_HIGH)
            && (loop_b_curr_val == GPIOManager::GPIO_LOW && loop_b_di_last_val_ == GPIOManager::GPIO_LOW))
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_A_OFF_EVENT));
        }
        // Case : Loop B off, Loop A off
        else if ((loop_b_curr_val == GPIOManager::GPIO_LOW && loop_b_di_last_val_ == GPIOManager::GPIO_HIGH)
                && (loop_a_curr_val == GPIOManager::GPIO_LOW && loop_a_di_last_val_ == GPIOManager::GPIO_LOW))
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_A_OFF_EVENT));
        }

        // Start -- Check the input pin status for Loop A and Loop B and send to Monitor
        if (loop_a_curr_val == GPIOManager::GPIO_HIGH && loop_a_di_last_val_ == GPIOManager::GPIO_LOW)
        {
            operation::getInstance()->tProcess.gbLoopAIsOn = true;
            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetLoopA(), 1);
        }
        else if (loop_a_curr_val == GPIOManager::GPIO_LOW && loop_a_di_last_val_ == GPIOManager::GPIO_HIGH)
        {
            operation::getInstance()->tProcess.gbLoopAIsOn = false;
            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetLoopA(), 0);
        }

        if (loop_b_curr_val == GPIOManager::GPIO_HIGH && loop_b_di_last_val_ == GPIOManager::GPIO_LOW)
        {
            operation::getInstance()->tProcess.gbLoopBIsOn = true;
            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetLoopB(), 1);
        }
        else if (loop_b_curr_val == GPIOManager::GPIO_LOW && loop_b_di_last_val_ == GPIOManager::GPIO_HIGH)
        {
            operation::getInstance()->tProcess.gbLoopBIsOn = false;
            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetLoopB(), 0);
        }
        // End -- Check the input pin status for Loop A and Loop B and send to Monitor
        
        if (loop_c_curr_val == GPIOManager::GPIO_HIGH && loop_c_di_last_val_ == GPIOManager::GPIO_LOW)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_C_ON_EVENT));
            
            operation::getInstance()->tProcess.gbLoopCIsOn = true;
            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetLoopC(), 1);
        }
        else if (loop_c_curr_val == GPIOManager::GPIO_LOW && loop_c_di_last_val_ == GPIOManager::GPIO_HIGH)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_C_OFF_EVENT));
            
            operation::getInstance()->tProcess.gbLoopCIsOn = false;
            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetLoopC(), 0);
        }
        
        if (intercom_curr_val == GPIOManager::GPIO_HIGH && intercom_di_last_val_ == GPIOManager::GPIO_LOW)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::INTERCOM_ON_EVENT));
            
            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetIntercom(), 1);
        }
        else if (intercom_curr_val == GPIOManager::GPIO_LOW && intercom_di_last_val_ == GPIOManager::GPIO_HIGH)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::INTERCOM_OFF_EVENT));
            
            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetIntercom(), 0);
        }
        
        if (station_door_open_curr_val == GPIOManager::GPIO_HIGH && station_door_open_di_last_val_ == GPIOManager::GPIO_LOW)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::STATION_DOOR_OPEN_EVENT));
            
            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetStationDooropen(), 1);
        }
        else if (station_door_open_curr_val == GPIOManager::GPIO_LOW && station_door_open_di_last_val_ == GPIOManager::GPIO_HIGH)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::STATION_DOOR_CLOSE_EVENT));
            
            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetStationDooropen(), 0);
        }
        
        if (barrier_door_open_curr_val == GPIOManager::GPIO_HIGH && barrier_door_open_di_last_val_ == GPIOManager::GPIO_LOW)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::BARRIER_DOOR_OPEN_EVENT));
            
            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetBarrierDooropen(), 1);
        }
        else if (barrier_door_open_curr_val == GPIOManager::GPIO_LOW && barrier_door_open_di_last_val_ == GPIOManager::GPIO_HIGH)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::BARRIER_DOOR_CLOSE_EVENT));
            
            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetBarrierDooropen(), 0);
        }
        
        if (barrier_status_curr_value == GPIOManager::GPIO_HIGH && barrier_status_di_last_val_ == GPIOManager::GPIO_LOW)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::BARRIER_STATUS_ON_EVENT));
            
            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetBarrierStatus(), 1);
        }
        else if (barrier_status_curr_value == GPIOManager::GPIO_LOW && barrier_status_di_last_val_ == GPIOManager::GPIO_HIGH)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::BARRIER_STATUS_OFF_EVENT));
            
            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetBarrierStatus(), 0);
        }

        if (manual_open_barrier_status_curr_value == GPIOManager::GPIO_HIGH && manual_open_barrier_di_last_val_ == GPIOManager::GPIO_LOW)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::MANUAL_OPEN_BARRIED_ON_EVENT));
            
            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetManualOpenBarrier(), 1);
        }
        else if (manual_open_barrier_status_curr_value == GPIOManager::GPIO_LOW && manual_open_barrier_di_last_val_ == GPIOManager::GPIO_HIGH)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::MANUAL_OPEN_BARRIED_OFF_EVENT));
            
            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetManualOpenBarrier(), 0);
        }

        if (lorry_sensor_curr_val == GPIOManager::GPIO_HIGH && lorry_sensor_di_last_val_ == GPIOManager::GPIO_LOW)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LORRY_SENSOR_ON_EVENT));

            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetLorrysensor(), 1);
        }
        else if (lorry_sensor_curr_val == GPIOManager::GPIO_LOW && lorry_sensor_di_last_val_ == GPIOManager::GPIO_HIGH)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LORRY_SENSOR_OFF_EVENT));

            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetLorrysensor(), 0);
        }

        if (arm_broken_curr_val == GPIOManager::GPIO_HIGH && arm_broken_di_last_val_ == GPIOManager::GPIO_LOW)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::ARM_BROKEN_ON_EVENT));

            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetArmbroken(), 1);
        }
        else if (arm_broken_curr_val == GPIOManager::GPIO_LOW && arm_broken_di_last_val_ == GPIOManager::GPIO_HIGH)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::ARM_BROKEN_OFF_EVENT));

            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetArmbroken(), 0);
        }

        loop_a_di_last_val_ = loop_a_curr_val;
        loop_b_di_last_val_ = loop_b_curr_val;
        loop_c_di_last_val_ = loop_c_curr_val;
        intercom_di_last_val_ = intercom_curr_val;
        station_door_open_di_last_val_ = station_door_open_curr_val;
        barrier_door_open_di_last_val_ = barrier_door_open_curr_val;
        barrier_status_di_last_val_ = barrier_status_curr_value;
        manual_open_barrier_di_last_val_ = manual_open_barrier_status_curr_value;
        lorry_sensor_di_last_val_ = lorry_sensor_curr_val;
        arm_broken_di_last_val_ = arm_broken_curr_val;

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void DIO::FnSetOpenBarrier(int value)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    GPIOManager::getInstance()->FnGetGPIO(open_barrier_do_)->FnSetValue(value);
}

void DIO::FnSetCloseBarrier(int value)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    GPIOManager::getInstance()->FnGetGPIO(close_barrier_do_)->FnSetValue(value);
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

int DIO::FnGetLoopAStatus() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return GPIOManager::getInstance()->FnGetGPIO(loop_a_di_)->FnGetValue();
}

int DIO::FnGetLoopBStatus() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return GPIOManager::getInstance()->FnGetGPIO(loop_b_di_)->FnGetValue();
}

int DIO::FnGetLoopCStatus() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return GPIOManager::getInstance()->FnGetGPIO(loop_c_di_)->FnGetValue();
}

int DIO::FnGetIntercomStatus() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return GPIOManager::getInstance()->FnGetGPIO(intercom_di_)->FnGetValue();
}

int DIO::FnGetStationDoorStatus() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return GPIOManager::getInstance()->FnGetGPIO(station_door_open_di_)->FnGetValue();
}

int DIO::FnGetBarrierDoorStatus() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return GPIOManager::getInstance()->FnGetGPIO(barrier_door_open_di_)->FnGetValue();
}

int DIO::FnGetBarrierStatus() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return GPIOManager::getInstance()->FnGetGPIO(barrier_status_di_)->FnGetValue();
}

int DIO::FnGetManualOpenBarrierStatus() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return GPIOManager::getInstance()->FnGetGPIO(manual_open_barrier_di_)->FnGetValue();
}

int DIO::FnGetLorrySensor() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return GPIOManager::getInstance()->FnGetGPIO(lorry_sensor_di_)->FnGetValue();
}

int DIO::FnGetArmbroken() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return GPIOManager::getInstance()->FnGetGPIO(arm_broken_di_)->FnGetValue();
}

int DIO::FnGetOutputPinNum(int pinNum)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return getOutputPinNum(pinNum);
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