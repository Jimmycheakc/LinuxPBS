#include <chrono>
#include <exception>
#include <optional>
#include <sstream>

#if defined(__linux__)
#include <pthread.h>
#endif

#include "dio.h"
#include "event_manager.h"
#include "gpio.h"
#include "ini_parser.h"
#include "log.h"
#include "operation.h"


DIO::DIO()
    : logFileName_("dio"),
      loop_a_di_(0),
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
      loop_a_di_last_val_(GPIOManager::GPIO_LOW),
      loop_b_di_last_val_(GPIOManager::GPIO_LOW),
      loop_c_di_last_val_(GPIOManager::GPIO_LOW),
      intercom_di_last_val_(GPIOManager::GPIO_LOW),
      station_door_open_di_last_val_(GPIOManager::GPIO_LOW),
      barrier_door_open_di_last_val_(GPIOManager::GPIO_LOW),
      barrier_status_di_last_val_(GPIOManager::GPIO_LOW),
      manual_open_barrier_di_last_val_(GPIOManager::GPIO_LOW),
      lorry_sensor_di_last_val_(GPIOManager::GPIO_LOW),
      arm_broken_di_last_val_(GPIOManager::GPIO_LOW),
      print_receipt_di_last_val_(GPIOManager::GPIO_LOW),
      manual_open_barrier_status_flag_(0),
      iBarrierOpenTooLongTime_(0),
      bIsBarrierOpenTooLongTime_(false),
      ioContext_(),
      pollTimer_(ioContext_),
      moduleRunning_(false),
      stopping_(false),
      isGPIOInitialized_(false),
      monitoringEnabled_(false),
      monitoringCoroutineActive_(false)
{
}

DIO::~DIO()
{
    FnDIOShutdown();
}

DIO* DIO::getInstance()
{
    static DIO instance;
    return &instance;
}

std::string DIO::gpioValueToString(int value) const
{
    switch (value)
    {
        case GPIOManager::GPIO_LOW:
            return "LOW";

        case GPIOManager::GPIO_HIGH:
            return "HIGH";

        default:
            return "UNKNOWN(" + std::to_string(value) + ")";
    }
}

void DIO::logDIChange(const char* name, int pinNumber, int oldValue, int newValue) const
{
    if (oldValue == newValue)
    {
        return;
    }

    std::ostringstream oss;
    oss << "[DI] " << name
        << " | " << gpioValueToString(oldValue)
        << " -> " << gpioValueToString(newValue)
        << " | Pin=" << pinNumber;

    Logger::getInstance()->FnLog(oss.str(), logFileName_, "DIO");
}

void DIO::FnDIOInit(int barrierOpenTooLongTime)
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);

    if (isGPIOInitialized_.load())
    {
        Logger::getInstance()->FnLog("[INIT] Already initialized", logFileName_, "DIO");
        return;
    }

    Logger::getInstance()->FnCreateLogFile(logFileName_);
    Logger::getInstance()->FnLog("[INIT] Starting", logFileName_, "DIO");

    iBarrierOpenTooLongTime_ = barrierOpenTooLongTime;

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

    std::ostringstream oss;
        oss << "[INIT] GPIO configuration"
            << " | LoopA=" << loop_a_di_
            << " | LoopB=" << loop_b_di_
            << " | LoopC=" << loop_c_di_
            << " | Intercom=" << intercom_di_
            << " | StationDoor=" << station_door_open_di_
            << " | BarrierDoor=" << barrier_door_open_di_
            << " | BarrierStatus=" << barrier_status_di_
            << " | ManualOpenBarrier=" << manual_open_barrier_di_
            << " | LorrySensor=" << lorry_sensor_di_
            << " | ArmBroken=" << arm_broken_di_
            << " | PrintReceipt=" << print_receipt_di_
            << " | OpenBarrierDO=" << open_barrier_do_
            << " | CloseBarrierDO=" << close_barrier_do_
            << " | LCDBacklightDO=" << lcd_backlight_do_
            << " | BarrierOpenLimit=" << iBarrierOpenTooLongTime_ << "s";

    Logger::getInstance()->FnLog(oss.str(), logFileName_, "DIO");

    if (!GPIOManager::getInstance()->FnGPIOInit())
    {
        isGPIOInitialized_.store(false);
        Logger::getInstance()->FnLog("DIO initialization failed.");
        Logger::getInstance()->FnLog("[INIT] Failed | GPIOManager initialization failed", logFileName_, "DIO");
        return;
    }

    loop_a_di_last_val_ = GPIOManager::GPIO_LOW;
    loop_b_di_last_val_ = GPIOManager::GPIO_LOW;
    loop_c_di_last_val_ = GPIOManager::GPIO_LOW;
    intercom_di_last_val_ = GPIOManager::GPIO_LOW;
    station_door_open_di_last_val_ = GPIOManager::GPIO_LOW;
    barrier_door_open_di_last_val_ = GPIOManager::GPIO_LOW;
    barrier_status_di_last_val_ = GPIOManager::GPIO_LOW;
    manual_open_barrier_di_last_val_ = GPIOManager::GPIO_LOW;
    lorry_sensor_di_last_val_ = GPIOManager::GPIO_LOW;
    arm_broken_di_last_val_ = GPIOManager::GPIO_LOW;
    print_receipt_di_last_val_ = GPIOManager::GPIO_LOW;

    manual_open_barrier_status_flag_.store(0);
    bIsBarrierOpenTooLongTime_ = false;
    barrierOpenSince_.reset();

    try
    {
        startModuleThread();
        isGPIOInitialized_.store(true);

        Logger::getInstance()->FnLog("DIO initialization completed.");
        Logger::getInstance()->FnLog("[INIT] Completed", logFileName_, "DIO");
    }
    catch (const std::exception& e)
    {
        isGPIOInitialized_.store(false);
        moduleRunning_.store(false);
        stopping_.store(true);

        std::stringstream ss;
        oss << "[INIT] Exception | " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "DIO");
    }
    catch (...)
    {
        isGPIOInitialized_.store(false);
        moduleRunning_.store(false);
        stopping_.store(true);

        const std::string message = "[INIT] Exception | Unknown exception";

        Logger::getInstance()->FnLogExceptionError(message);
        Logger::getInstance()->FnLog(message, logFileName_, "DIO");
    }
}

void DIO::startModuleThread()
{
    if (moduleRunning_.load())
    {
        return;
    }

    ioContext_.restart();
    stopping_.store(false);
    monitoringEnabled_ = false;
    monitoringCoroutineActive_ = false;

    workGuard_.emplace(boost::asio::make_work_guard(ioContext_));

    ioThread_ = std::thread([this]()
    {
        Logger::getInstance()->FnLog("[THREAD] io_context started", logFileName_, "DIO");
        try
        {
#if defined(__linux__)
            ::pthread_setname_np(::pthread_self(), "DIO_IO");
#endif
            ioContext_.run();
        }
        catch (const std::exception& e)
        {
            std::stringstream ss;
            ss << "[THREAD] io_context exception | " << e.what();
            Logger::getInstance()->FnLogExceptionError(ss.str());
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "DIO");
        }
        catch (...)
        {
            const std::string message = "[THREAD] io_context exception | Unknown exception";

            Logger::getInstance()->FnLogExceptionError(message);
            Logger::getInstance()->FnLog(message, logFileName_, "DIO");
        }

        moduleRunning_.store(false);

        Logger::getInstance()->FnLog("[THREAD] io_context stopped", logFileName_, "DIO");
    });

    moduleRunning_.store(true);
}

void DIO::FnDIOShutdown()
{
    std::unique_lock<std::mutex> lock(lifecycleMutex_);

    if (!moduleRunning_.load() && !ioThread_.joinable())
    {
        isGPIOInitialized_.store(false);
        return;
    }

    Logger::getInstance()->FnLog("[SHUTDOWN] Starting", logFileName_, "DIO");
    
    if (moduleRunning_.load())
    {
        stopping_.store(true);

        boost::asio::post(ioContext_, [this]()
        {
            stopMonitoringOnIOThread();
        });

        if (workGuard_)
        {
            workGuard_->reset();
        }
    }

    const bool calledFromIOThread = ioThread_.joinable() && std::this_thread::get_id() == ioThread_.get_id();

    if (calledFromIOThread)
    {
        // A thread cannot join itself. Leave the std::thread joinable so a
        // later shutdown call from another thread (normally application
        // shutdown / the singleton destructor) can join it safely.
        isGPIOInitialized_.store(false);

        Logger::getInstance()->FnLog("[SHUTDOWN] Requested from DIO io_context thread", logFileName_, "DIO");
        return;
    }

    lock.unlock();

    if (ioThread_.joinable())
    {
        ioThread_.join();
    }

    lock.lock();

    workGuard_.reset();
    moduleRunning_.store(false);
    isGPIOInitialized_.store(false);
    stopping_.store(false);
    monitoringEnabled_ = false;
    monitoringCoroutineActive_ = false;

    Logger::getInstance()->FnLog("[SHUTDOWN] Completed", logFileName_, "DIO");
}

void DIO::FnStartDIOMonitoring()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    if (!isGPIOInitialized_.load() || !moduleRunning_.load())
    {
        Logger::getInstance()->FnLog("[MONITOR] Start failed | DIO not initialized", logFileName_, "DIO");
        return;
    }

    boost::asio::post(ioContext_, [this]()
    {
        startMonitoringOnIOThread();
    });
}

void DIO::FnStopDIOMonitoring()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "DIO");

    if (!moduleRunning_.load())
    {
        return;
    }

    boost::asio::post(ioContext_, [this]()
    {
        stopMonitoringOnIOThread();
    });
}

void DIO::startMonitoringOnIOThread()
{
    if (stopping_.load())
    {
        Logger::getInstance()->FnLog("[MONITOR] Start ignored | Module is stopping", logFileName_, "DIO");
        return;
    }

    monitoringEnabled_ = true;

    if (monitoringCoroutineActive_)
    {
        return;
    }

    monitoringCoroutineActive_ = true;

    {
        std::ostringstream oss;
        oss << "[MONITOR] Started" << " | Interval=" << kPollInterval.count() << "ms";

        Logger::getInstance()->FnLog(oss.str(), logFileName_, "DIO");
    }

    boost::asio::co_spawn(
        ioContext_,
        monitoringDIOLoopAsync(),
        [this](std::exception_ptr ep)
        {
            monitoringCoroutineActive_ = false;

            if (ep)
            {
                try
                {
                    std::rethrow_exception(ep);
                }
                catch (const std::exception& e)
                {
                    std::stringstream ss;
                    ss << "[MONITOR] Coroutine exception | " << e.what();
                    Logger::getInstance()->FnLogExceptionError(ss.str());
                    Logger::getInstance()->FnLog(ss.str(), logFileName_, "DIO");
                }
                catch (...)
                {
                    const std::string message = "[MONITOR] Coroutine exception | Unknown exception";

                    Logger::getInstance()->FnLogExceptionError(message);
                    Logger::getInstance()->FnLog(message, logFileName_, "DIO");
                }
            }
        });
}

void DIO::stopMonitoringOnIOThread()
{
    if (!monitoringEnabled_)
    {
        return;
    }

    monitoringEnabled_ = false;

    Logger::getInstance()->FnLog("[MONITOR] Stopped", logFileName_, "DIO");

    boost::system::error_code ec;
    pollTimer_.cancel(ec);

    if (ec)
    {
        std::ostringstream oss;
        oss << "[MONITOR] Timer cancel failed | " << ec.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "DIO");
    }
}

boost::asio::awaitable<void> DIO::monitoringDIOLoopAsync()
{
    while (monitoringEnabled_ && !stopping_.load())
    {
        const InputSnapshot current = readInputSnapshot();
        processDIOChanges(current);
        updateLastSnapshot(current);

        if (!monitoringEnabled_ || stopping_.load())
        {
            break;
        }

        pollTimer_.expires_after(kPollInterval);

        boost::system::error_code ec;
        co_await pollTimer_.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, ec));

        if (ec == boost::asio::error::operation_aborted)
        {
            if (!monitoringEnabled_ || stopping_.load())
            {
                break;
            }
            continue;
        }

        if (ec)
        {
            std::stringstream ss;
            ss << "[MONITOR] Timer error | " << ec.message();
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "DIO");
            break;
        }
    }

    co_return;
}

DIO::InputSnapshot DIO::readInputSnapshot() const
{
    InputSnapshot current;
    current.loopA = readGPIOValue(loop_a_di_);
    current.loopB = readGPIOValue(loop_b_di_);
    current.loopC = readGPIOValue(loop_c_di_);
    current.intercom = readGPIOValue(intercom_di_);
    current.stationDoorOpen = readGPIOValue(station_door_open_di_);
    current.barrierDoorOpen = readGPIOValue(barrier_door_open_di_);
    current.barrierStatus = readGPIOValue(barrier_status_di_);
    current.manualOpenBarrier = readGPIOValue(manual_open_barrier_di_);
    current.lorrySensor = readGPIOValue(lorry_sensor_di_);
    current.armBroken = readGPIOValue(arm_broken_di_);
    current.printReceipt = readGPIOValue(print_receipt_di_);
    return current;
}

void DIO::processDIOChanges(const InputSnapshot& current)
{
    const int loop_a_curr_val = current.loopA;
    const int loop_b_curr_val = current.loopB;
    const int loop_c_curr_val = current.loopC;
    const int intercom_curr_val = current.intercom;
    const int station_door_open_curr_val = current.stationDoorOpen;
    const int barrier_door_open_curr_val = current.barrierDoorOpen;
    const int barrier_status_curr_value = current.barrierStatus;
    const int manual_open_barrier_status_curr_value = current.manualOpenBarrier;
    const int lorry_sensor_curr_val = current.lorrySensor;
    const int arm_broken_curr_val = current.armBroken;
    const int print_receipt_curr_val = current.printReceipt;

    // Log only physical input changes. This avoids flooding the log on every poll.
    logDIChange("Loop A", loop_a_di_, loop_a_di_last_val_, loop_a_curr_val);
    logDIChange("Loop B", loop_b_di_, loop_b_di_last_val_, loop_b_curr_val);
    logDIChange("Loop C", loop_c_di_, loop_c_di_last_val_, loop_c_curr_val);
    logDIChange("Intercom", intercom_di_, intercom_di_last_val_, intercom_curr_val);
    logDIChange("Station Door", station_door_open_di_, station_door_open_di_last_val_, station_door_open_curr_val);
    logDIChange("Barrier Door", barrier_door_open_di_, barrier_door_open_di_last_val_, barrier_door_open_curr_val);
    logDIChange("Barrier Status", barrier_status_di_, barrier_status_di_last_val_, barrier_status_curr_value);
    logDIChange("Manual Open Barrier", manual_open_barrier_di_, manual_open_barrier_di_last_val_, manual_open_barrier_status_curr_value);
    logDIChange("Lorry Sensor", lorry_sensor_di_, lorry_sensor_di_last_val_, lorry_sensor_curr_val);
    logDIChange("Arm Broken", arm_broken_di_, arm_broken_di_last_val_, arm_broken_curr_val);
    logDIChange("Print Receipt", print_receipt_di_, print_receipt_di_last_val_, print_receipt_curr_val);

    // Case : Loop A on, Loop B no change 
    if ((loop_a_curr_val == GPIOManager::GPIO_HIGH && loop_a_di_last_val_ == GPIOManager::GPIO_LOW)
        && (loop_b_curr_val == GPIOManager::GPIO_LOW && loop_b_di_last_val_ == GPIOManager::GPIO_LOW))
    {
        operation::getInstance()->FnSetLoopAPresent(true);
        EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_A_ON_EVENT));
    }
    // Case : Loop B on, Loop A no change
    else if ((loop_b_curr_val == GPIOManager::GPIO_HIGH && loop_b_di_last_val_ == GPIOManager::GPIO_LOW) 
            && (loop_a_curr_val == GPIOManager::GPIO_LOW && loop_a_di_last_val_ == GPIOManager::GPIO_LOW))
    {
        operation::getInstance()->FnSetLoopAPresent(true);
        EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_A_ON_EVENT));
    }
    // Case : Loop A on, Loop B on
    else if ((loop_a_curr_val == GPIOManager::GPIO_HIGH && loop_a_di_last_val_ == GPIOManager::GPIO_LOW)
            && (loop_b_curr_val == GPIOManager::GPIO_HIGH && loop_b_di_last_val_ == GPIOManager::GPIO_LOW))
    {
        operation::getInstance()->FnSetLoopAPresent(true);
        EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_A_ON_EVENT));
    }

    // Case : Loop A off, Loop B no change
    if ((loop_a_curr_val == GPIOManager::GPIO_LOW && loop_a_di_last_val_ == GPIOManager::GPIO_HIGH)
        && (loop_b_curr_val == GPIOManager::GPIO_LOW && loop_b_di_last_val_ == GPIOManager::GPIO_LOW))
    {
        operation::getInstance()->FnSetLoopAPresent(false);
        EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_A_OFF_EVENT));
    }
    // Case : Loop B off, Loop A no change
    else if ((loop_b_curr_val == GPIOManager::GPIO_LOW && loop_b_di_last_val_ == GPIOManager::GPIO_HIGH)
            && (loop_a_curr_val == GPIOManager::GPIO_LOW && loop_a_di_last_val_ == GPIOManager::GPIO_LOW))
    {
        operation::getInstance()->FnSetLoopAPresent(false);
        EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_A_OFF_EVENT));
    }
    // Case : Loop A off, Loop B off
    else if ((loop_a_curr_val == GPIOManager::GPIO_LOW && loop_a_di_last_val_ == GPIOManager::GPIO_HIGH)
        && (loop_b_curr_val == GPIOManager::GPIO_LOW && loop_b_di_last_val_ == GPIOManager::GPIO_HIGH))
    {
        operation::getInstance()->FnSetLoopAPresent(false);
        EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_A_OFF_EVENT));
    }

    // Start -- Check the input pin status for Loop A and Loop B and send to Monitor
    if (loop_a_curr_val == GPIOManager::GPIO_HIGH && loop_a_di_last_val_ == GPIOManager::GPIO_LOW)
    {
        // Send to Input Pin Status to Monitor
        operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetLoopA(), 1);
    }
    else if (loop_a_curr_val == GPIOManager::GPIO_LOW && loop_a_di_last_val_ == GPIOManager::GPIO_HIGH)
    {
        // Send to Input Pin Status to Monitor
        operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetLoopA(), 0);
    }

    if (loop_b_curr_val == GPIOManager::GPIO_HIGH && loop_b_di_last_val_ == GPIOManager::GPIO_LOW)
    {
        // Send to Input Pin Status to Monitor
        operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetLoopB(), 1);
    }
    else if (loop_b_curr_val == GPIOManager::GPIO_LOW && loop_b_di_last_val_ == GPIOManager::GPIO_HIGH)
    {
        // Send to Input Pin Status to Monitor
        operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetLoopB(), 0);
    }
    // End -- Check the input pin status for Loop A and Loop B and send to Monitor
    
    if (loop_c_curr_val == GPIOManager::GPIO_HIGH && loop_c_di_last_val_ == GPIOManager::GPIO_LOW)
    {
        EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_C_ON_EVENT));
        
        // Send to Input Pin Status to Monitor
        operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetLoopC(), 1);
    }
    else if (loop_c_curr_val == GPIOManager::GPIO_LOW && loop_c_di_last_val_ == GPIOManager::GPIO_HIGH)
    {
        EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::LOOP_C_OFF_EVENT));
        
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
        barrierOpenSince_ = std::chrono::steady_clock::now();
    }
    else if (barrier_status_curr_value == GPIOManager::GPIO_LOW && barrier_status_di_last_val_ == GPIOManager::GPIO_HIGH)
    {
        EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::BARRIER_STATUS_OFF_EVENT));
        
        // Send to Input Pin Status to Monitor
        operation::getInstance()->FnSendDIOInputStatusToMonitor(IniParser::getInstance()->FnGetBarrierStatus(), 0);

        // Clear the barrier open time
        barrierOpenSince_.reset();

        if (bIsBarrierOpenTooLongTime_ == true)
        {
            bIsBarrierOpenTooLongTime_ = false;
            Logger::getInstance()->FnLog("[ALARM] Barrier open-too-long condition cleared", logFileName_, "DIO");
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
    if ((iBarrierOpenTooLongTime_ > 0) && barrierOpenSince_.has_value())
    {
        const auto duration_sec = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - *barrierOpenSince_).count();

        if (duration_sec > iBarrierOpenTooLongTime_)
        {
            bIsBarrierOpenTooLongTime_ = true;

            std::ostringstream oss;
            oss << "[ALARM] Barrier open too long"
                << " | Duration=" << duration_sec << "s"
                << " | Limit=" << iBarrierOpenTooLongTime_ << "s";
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "DIO");

            EventManager::getInstance()->FnEnqueueEvent<int>("Evt_handleDIOEvent", static_cast<int>(DIO_EVENT::BARRIER_OPEN_TOO_LONG_ON_EVENT));
            barrierOpenSince_.reset();
        }
    }
}

void DIO::updateLastSnapshot(const InputSnapshot& current)
{
    loop_a_di_last_val_ = current.loopA;
    loop_b_di_last_val_ = current.loopB;
    loop_c_di_last_val_ = current.loopC;
    intercom_di_last_val_ = current.intercom;
    station_door_open_di_last_val_ = current.stationDoorOpen;
    barrier_door_open_di_last_val_ = current.barrierDoorOpen;
    barrier_status_di_last_val_ = current.barrierStatus;
    manual_open_barrier_di_last_val_ = current.manualOpenBarrier;
    lorry_sensor_di_last_val_ = current.lorrySensor;
    arm_broken_di_last_val_ = current.armBroken;
    print_receipt_di_last_val_ = current.printReceipt;
}

int DIO::readGPIOValue(int pinNumber) const
{
    if (pinNumber == 0)
    {
        return GPIOManager::GPIO_LOW;
    }

    SysfsGPIO* gpio = GPIOManager::getInstance()->FnGetGPIO(pinNumber);
    return (gpio != nullptr) ? gpio->FnGetValue() : GPIOManager::GPIO_LOW;
}

bool DIO::writeGPIOValue(int pinNumber, int value, const char* description)
{
    if (pinNumber == 0)
    {
        std::ostringstream oss;
        oss << "[DO] " << description << " | FAILED" << " | Invalid pin";
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "DIO");
        return false;
    }

    SysfsGPIO* gpio = GPIOManager::getInstance()->FnGetGPIO(pinNumber);

    if (gpio == nullptr)
    {
        std::ostringstream oss;
        oss << "[DO] " << description << " | FAILED" << " | GPIO unavailable" << " | Pin=" << pinNumber;
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "DIO");
        return false;
    }

    const bool success = gpio->FnSetValue(value);

    std::ostringstream oss;
    oss << "[DO] " << description
        << " | Value=" << gpioValueToString(value)
        << " | Pin=" << pinNumber
        << " | " << (success ? "OK" : "FAILED");
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "DIO");

    return success;
}

void DIO::FnSetOpenBarrier(int value)
{
    writeGPIOValue(open_barrier_do_, value, "Open Barrier");
}

void DIO::FnSetCloseBarrier(int value)
{
    writeGPIOValue(close_barrier_do_, value, "Close Barrier");
}

int DIO::FnGetOpenBarrier() const
{
    return readGPIOValue(open_barrier_do_);
}

void DIO::FnSetLCDBacklight(int value)
{
    writeGPIOValue(lcd_backlight_do_, value, "LCD Backlight");
}

int DIO::FnGetLCDBacklight() const
{
    return readGPIOValue(lcd_backlight_do_);
}

int DIO::FnGetLoopAStatus() const
{
    return readGPIOValue(loop_a_di_);
}

int DIO::FnGetLoopBStatus() const
{
    return readGPIOValue(loop_b_di_);
}

int DIO::FnGetLoopCStatus() const
{
    return readGPIOValue(loop_c_di_);
}

int DIO::FnGetIntercomStatus() const
{
    return readGPIOValue(intercom_di_);
}

int DIO::FnGetStationDoorStatus() const
{
    return readGPIOValue(station_door_open_di_);
}

int DIO::FnGetBarrierDoorStatus() const
{
    return readGPIOValue(barrier_door_open_di_);
}

int DIO::FnGetBarrierStatus() const
{
    return readGPIOValue(barrier_status_di_);
}

int DIO::FnGetManualOpenBarrierStatus() const
{
    return readGPIOValue(manual_open_barrier_di_);
}

int DIO::FnGetLorrySensor() const
{
    return readGPIOValue(lorry_sensor_di_);
}

int DIO::FnGetArmbroken() const
{
    return readGPIOValue(arm_broken_di_);
}

int DIO::FnGetOutputPinNum(int pinNum)
{
    return getOutputPinNum(pinNum);
}

int DIO::getInputPinNum(int pinNum) const
{
    switch (pinNum)
    {
        case 1: return GPIOManager::PIN_DI1;
        case 2: return GPIOManager::PIN_DI2;
        case 3: return GPIOManager::PIN_DI3;
        case 4: return GPIOManager::PIN_DI4;
        case 5: return GPIOManager::PIN_DI5;
        case 6: return GPIOManager::PIN_DI6;
        case 7: return GPIOManager::PIN_DI7;
        case 8: return GPIOManager::PIN_DI8;
        case 9: return GPIOManager::PIN_DI9;
        case 10: return GPIOManager::PIN_DI10;
        case 11: return GPIOManager::PIN_DI11;
        case 12: return GPIOManager::PIN_DI12;
        default: return 0;
    }
}

int DIO::getOutputPinNum(int pinNum) const
{
    switch (pinNum)
    {
        case 1: return GPIOManager::PIN_DO1;
        case 2: return GPIOManager::PIN_DO2;
        case 3: return GPIOManager::PIN_DO3;
        case 4: return GPIOManager::PIN_DO4;
        case 5: return GPIOManager::PIN_DO5;
        case 6: return GPIOManager::PIN_DO6;
        case 7: return GPIOManager::PIN_DO7;
        case 8: return GPIOManager::PIN_DO8;
        case 9: return GPIOManager::PIN_DO9;
        // (Disabled :Use for USB Hub Reset)
        // case 10: return GPIOManager::PIN_D10;
        default: return 0;
    }
}

void DIO::FnSetManualOpenBarrierStatusFlag(int flag)
{
    manual_open_barrier_status_flag_.store(flag);
}

int DIO::FnGetManualOpenBarrierStatusFlag() const
{
    return manual_open_barrier_status_flag_.load();
}