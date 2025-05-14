#include <functional>
#include <sstream>
#include <thread>
#include "common.h"
#include "dio.h"
#include "event_manager.h"
#include "gpio.h"
#include "ini_parser.h"
#include "log.h"
#include "operation.h"

DIO* DIO::dio_ = nullptr;
std::mutex DIO::mutex_;

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
    print_receipt_di_(0),
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
    print_receipt_di_last_val_(0),
    iBarrierOpenTooLongTime_(0),
    bIsBarrierOpenTooLongTime_(false),
    isDIOMonitoringThreadRunning_(false),
    isGPIOInitialized_(false)
{
    logFileName_ = "dio";
}

DIO* DIO::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (dio_ == nullptr)
    {
        dio_ = new DIO();
    }

    return dio_;
}

void DIO::FnDIOInit()
{
    iBarrierOpenTooLongTime_ = operation::getInstance()->tParas.giBarrierOpenTooLongTime;

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
    print_receipt_di_ = getInputPinNum(IniParser::getInstance()->FnGetPrintReceipt());
    open_barrier_do_ = getOutputPinNum(IniParser::getInstance()->FnGetOpenbarrier());
    lcd_backlight_do_ = getOutputPinNum(IniParser::getInstance()->FnGetLCDbacklight());
    close_barrier_do_ = getOutputPinNum(IniParser::getInstance()->FnGetclosebarrier());

    Logger::getInstance()->FnCreateLogFile(logFileName_);

    if (GPIOManager::getInstance()->FnGPIOInit())
    {
        Logger::getInstance()->FnLog("DIO initialization completed.");
        Logger::getInstance()->FnLog("DIO initialization completed.", logFileName_, "DIO");
        isGPIOInitialized_ = true;
    }
    else
    {
        Logger::getInstance()->FnLog("DIO initialization failed.");
        Logger::getInstance()->FnLog("DIO initialization failed.", logFileName_, "DIO");
        isGPIOInitialized_ = false;
    }
}

void DIO::FnStartDIOMonitoring()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    if (isGPIOInitialized_)
    {
        if (!isDIOMonitoringThreadRunning_.load())
        {
            isDIOMonitoringThreadRunning_.store(true);
            dioMonitoringThread_ = std::thread(&DIO::monitoringDIOChangeThreadFunction, this);
        }
    }
    else
    {
        Logger::getInstance()->FnLog("Failed to start DIO monitoring as DIO initialization failed.", logFileName_, "DIO");
    }
}

void DIO::FnStopDIOMonitoring()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    if (isDIOMonitoringThreadRunning_.load())
    {
        isDIOMonitoringThreadRunning_.store(false);
        dioMonitoringThread_.join();
    }
}

void DIO::monitoringDIOChangeThreadFunction()
{
    while (isDIOMonitoringThreadRunning_.load())
    {
        static std::string barrier_open_time_ = "";

        int loop_a_curr_val = (GPIOManager::getInstance()->FnGetGPIO(loop_a_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(loop_a_di_)->FnGetValue() : 0;
        int loop_b_curr_val = (GPIOManager::getInstance()->FnGetGPIO(loop_b_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(loop_b_di_)->FnGetValue() : 0;
        int loop_c_curr_val = (GPIOManager::getInstance()->FnGetGPIO(loop_c_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(loop_c_di_)->FnGetValue() : 0;
        int intercom_curr_val = (GPIOManager::getInstance()->FnGetGPIO(intercom_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(intercom_di_)->FnGetValue() : 0;
        int station_door_open_curr_val = (GPIOManager::getInstance()->FnGetGPIO(station_door_open_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(station_door_open_di_)->FnGetValue() : 0;
        int barrier_door_open_curr_val = (GPIOManager::getInstance()->FnGetGPIO(barrier_door_open_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(barrier_door_open_di_)->FnGetValue() : 0;
        int barrier_status_curr_value = (GPIOManager::getInstance()->FnGetGPIO(barrier_status_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(barrier_status_di_)->FnGetValue() : 0;
        int manual_open_barrier_status_curr_value = (GPIOManager::getInstance()->FnGetGPIO(manual_open_barrier_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(manual_open_barrier_di_)->FnGetValue() : 0;
        int lorry_sensor_curr_val = (GPIOManager::getInstance()->FnGetGPIO(lorry_sensor_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(lorry_sensor_di_)->FnGetValue() : 0;
        int arm_broken_curr_val = (GPIOManager::getInstance()->FnGetGPIO(arm_broken_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(arm_broken_di_)->FnGetValue() : 0;
        int print_receipt_curr_val = (GPIOManager::getInstance()->FnGetGPIO(print_receipt_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(print_receipt_di_)->FnGetValue() : 0;
        
        // Case : Loop A on, Loop B no change 
        if ((loop_a_curr_val == GPIOManager::GPIO_HIGH && loop_a_di_last_val_ == GPIOManager::GPIO_LOW)
            && (loop_b_curr_val == GPIOManager::GPIO_LOW && loop_b_di_last_val_ == GPIOManager::GPIO_LOW))
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_A_ON_EVENT));
        }
        // Case : Loop B on, Loop A no change
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

        // Case : Loop A off, Loop B no change
        if ((loop_a_curr_val == GPIOManager::GPIO_LOW && loop_a_di_last_val_ == GPIOManager::GPIO_HIGH)
            && (loop_b_curr_val == GPIOManager::GPIO_LOW && loop_b_di_last_val_ == GPIOManager::GPIO_LOW))
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_A_OFF_EVENT));
        }
        // Case : Loop B off, Loop A no change
        else if ((loop_b_curr_val == GPIOManager::GPIO_LOW && loop_b_di_last_val_ == GPIOManager::GPIO_HIGH)
                && (loop_a_curr_val == GPIOManager::GPIO_LOW && loop_a_di_last_val_ == GPIOManager::GPIO_LOW))
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_A_OFF_EVENT));
        }
        // Case : Loop A off, Loop B off
        else if ((loop_a_curr_val == GPIOManager::GPIO_LOW && loop_a_di_last_val_ == GPIOManager::GPIO_HIGH)
            && (loop_b_curr_val == GPIOManager::GPIO_LOW && loop_b_di_last_val_ == GPIOManager::GPIO_HIGH))
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

            // Set the barrier open time
            barrier_open_time_ = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
        }
        else if (barrier_status_curr_value == GPIOManager::GPIO_LOW && barrier_status_di_last_val_ == GPIOManager::GPIO_HIGH)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::BARRIER_STATUS_OFF_EVENT));
            
            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetBarrierStatus(), 0);

            // Clear the barrier open time
            barrier_open_time_.clear();

            if (bIsBarrierOpenTooLongTime_ == true)
            {
                bIsBarrierOpenTooLongTime_ = false;
                EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::BARRIER_OPEN_TOO_LONG_OFF_EVENT));
            }
        }

        if (manual_open_barrier_status_curr_value == GPIOManager::GPIO_HIGH && manual_open_barrier_di_last_val_ == GPIOManager::GPIO_LOW)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::MANUAL_OPEN_BARRIED_ON_EVENT));
            FnSetManualOpenBarrierStatusFlag(1);
            
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
            operation::getInstance()->tProcess.gbLorrySensorIsOn = true;
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LORRY_SENSOR_ON_EVENT));

            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetLorrysensor(), 1);
        }
        else if (lorry_sensor_curr_val == GPIOManager::GPIO_LOW && lorry_sensor_di_last_val_ == GPIOManager::GPIO_HIGH)
        {
            operation::getInstance()->tProcess.gbLorrySensorIsOn = false;
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

        if (print_receipt_curr_val == GPIOManager::GPIO_HIGH && print_receipt_di_last_val_ == GPIOManager::GPIO_LOW)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::PRINT_RECEIPT_ON_EVENT));

            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetPrintReceipt(), 1);
        }
        else if (print_receipt_curr_val == GPIOManager::GPIO_LOW && print_receipt_di_last_val_ == GPIOManager::GPIO_HIGH)
        {
            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::PRINT_RECEIPT_OFF_EVENT));

            // Send to Input Pin Status to Monitor
            operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetPrintReceipt(), 0);
        }


        // Handle if barrier open too long
        if ((iBarrierOpenTooLongTime_ > 0) && (!barrier_open_time_.empty()))
        {
            int64_t duration_sec = 0;
            // Compare with now() to get the differnece in seconds
            duration_sec = Common::getInstance()->FnGetDateDiffInSeconds(barrier_open_time_);

            if (duration_sec > iBarrierOpenTooLongTime_)
            {
                bIsBarrierOpenTooLongTime_ = true;
                EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::BARRIER_OPEN_TOO_LONG_ON_EVENT));
                barrier_open_time_.clear();
            }
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
        print_receipt_di_last_val_ = print_receipt_curr_val;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void DIO::FnSetOpenBarrier(int value)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    if (GPIOManager::getInstance()->FnGetGPIO(open_barrier_do_) != nullptr)
    {
        GPIOManager::getInstance()->FnGetGPIO(open_barrier_do_)->FnSetValue(value);
    }
    else
    {
        std::stringstream ss;
        ss << "Nullptr, Unable to set open barrier DO pin : " << open_barrier_do_;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "DIO");
    }
}

void DIO::FnSetCloseBarrier(int value)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    if (GPIOManager::getInstance()->FnGetGPIO(close_barrier_do_) != nullptr)
    {
        GPIOManager::getInstance()->FnGetGPIO(close_barrier_do_)->FnSetValue(value);
    }
    else
    {
        std::stringstream ss;
        ss << "Nullptr, Unable to set close barrier DO pin : " << close_barrier_do_;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "DIO");
    }
}

int DIO::FnGetOpenBarrier() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return (GPIOManager::getInstance()->FnGetGPIO(open_barrier_do_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(open_barrier_do_)->FnGetValue() : 0;
}

void DIO::FnSetLCDBacklight(int value)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    if (GPIOManager::getInstance()->FnGetGPIO(lcd_backlight_do_) != nullptr)
    {
        GPIOManager::getInstance()->FnGetGPIO(lcd_backlight_do_)->FnSetValue(value);
    }
    else
    {
        std::stringstream ss;
        ss << "Nullptr, Unable to set backlight DO pin : " << lcd_backlight_do_;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "DIO");
    }
}

int DIO::FnGetLCDBacklight() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return (GPIOManager::getInstance()->FnGetGPIO(lcd_backlight_do_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(lcd_backlight_do_)->FnGetValue() : 0;
}

int DIO::FnGetLoopAStatus() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return (GPIOManager::getInstance()->FnGetGPIO(loop_a_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(loop_a_di_)->FnGetValue() : 0;
}

int DIO::FnGetLoopBStatus() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return (GPIOManager::getInstance()->FnGetGPIO(loop_b_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(loop_b_di_)->FnGetValue() : 0;
}

int DIO::FnGetLoopCStatus() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return (GPIOManager::getInstance()->FnGetGPIO(loop_c_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(loop_c_di_)->FnGetValue() : 0;
}

int DIO::FnGetIntercomStatus() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return (GPIOManager::getInstance()->FnGetGPIO(intercom_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(intercom_di_)->FnGetValue() : 0;
}

int DIO::FnGetStationDoorStatus() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return (GPIOManager::getInstance()->FnGetGPIO(station_door_open_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(station_door_open_di_)->FnGetValue() : 0;
}

int DIO::FnGetBarrierDoorStatus() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return (GPIOManager::getInstance()->FnGetGPIO(barrier_door_open_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(barrier_door_open_di_)->FnGetValue() : 0;
}

int DIO::FnGetBarrierStatus() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return (GPIOManager::getInstance()->FnGetGPIO(barrier_status_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(barrier_status_di_)->FnGetValue() : 0;
}

int DIO::FnGetManualOpenBarrierStatus() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return (GPIOManager::getInstance()->FnGetGPIO(manual_open_barrier_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(manual_open_barrier_di_)->FnGetValue() : 0;
}

int DIO::FnGetLorrySensor() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return (GPIOManager::getInstance()->FnGetGPIO(lorry_sensor_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(lorry_sensor_di_)->FnGetValue() : 0;
}

int DIO::FnGetArmbroken() const
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    return (GPIOManager::getInstance()->FnGetGPIO(arm_broken_di_) != nullptr) ? GPIOManager::getInstance()->FnGetGPIO(arm_broken_di_)->FnGetValue() : 0;
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
        // (Disabled :Use for USB Hub Reset)
        /*
        case 10:
            ret = GPIOManager::PIN_DO10;
            break;
        */
        default:
            ret = 0;
            break;
    }

    return ret;
}

void DIO::FnSetManualOpenBarrierStatusFlag(int flag)
{
    std::lock_guard<std::mutex> lock(manual_open_barrier_status_flag_mutex_);
    manual_open_barrier_status_flag_ = 1;
}

int DIO::FnGetManualOpenBarrierStatusFlag()
{
    std::lock_guard<std::mutex> lock(manual_open_barrier_status_flag_mutex_);
    return manual_open_barrier_status_flag_;
}