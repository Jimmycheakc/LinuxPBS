#pragma once

#include <iostream>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <mutex>
#include <vector>
#include "boost/asio.hpp"
#include "boost/asio/posix/stream_descriptor.hpp"

class DIO
{
public:
    enum class DIO_EVENT
    {
        LOOP_A_ON_EVENT = 0,
        LOOP_A_OFF_EVENT = 1,
        LOOP_B_ON_EVENT = 2,
        LOOP_B_OFF_EVENT = 3,
        LOOP_C_ON_EVENT = 4,
        LOOP_C_OFF_EVENT = 5,
        INTERCOM_ON_EVENT = 6,
        INTERCOM_OFF_EVENT = 7,
        STATION_DOOR_OPEN_EVENT = 8,
        STATION_DOOR_CLOSE_EVENT = 9,
        BARRIER_DOOR_OPEN_EVENT = 10,
        BARRIER_DOOR_CLOSE_EVENT = 11,
        BARRIER_STATUS_ON_EVENT = 12,
        BARRIER_STATUS_OFF_EVENT = 13,
        MANUAL_OPEN_BARRIED_ON_EVENT = 14,
        MANUAL_OPEN_BARRIED_OFF_EVENT = 15,
        LORRY_SENSOR_ON_EVENT = 16,
        LORRY_SENSOR_OFF_EVENT = 17,
        ARM_BROKEN_ON_EVENT = 18,
        ARM_BROKEN_OFF_EVENT = 19,
        PRINT_RECEIPT_ON_EVENT = 20,
        PRINT_RECEIPT_OFF_EVENT = 21,
        BARRIER_OPEN_TOO_LONG_ON_EVENT = 22,
        BARRIER_OPEN_TOO_LONG_OFF_EVENT = 23
    };

    static DIO* getInstance();
    void FnDIOInit();
    void FnStartDIOMonitoring();
    void FnStopDIOMonitoring();
    void FnSetOpenBarrier(int value);
    int FnGetOpenBarrier() const;
    void FnSetLCDBacklight(int value);
    void FnSetCloseBarrier(int value);
    int FnGetLCDBacklight() const;
    int FnGetLoopAStatus() const;
    int FnGetLoopBStatus() const;
    int FnGetLoopCStatus() const;
    int FnGetIntercomStatus() const;
    int FnGetStationDoorStatus() const;
    int FnGetBarrierDoorStatus() const;
    int FnGetBarrierStatus() const;
    int FnGetManualOpenBarrierStatus() const;
    int FnGetLorrySensor() const;
    int FnGetArmbroken() const;

    int FnGetOutputPinNum(int pinNum);

    void FnSetManualOpenBarrierStatusFlag(int flag);
    int FnGetManualOpenBarrierStatusFlag();

    /**
     * Singleton DIO should not be cloneable.
     */
    DIO(DIO& dio) = delete;

    /**
     * Singleton DIO should not be assignable. 
     */
    void operator=(const DIO&) = delete;

private:
    static DIO* dio_;
    static std::mutex mutex_;
    std::string logFileName_;
    int loop_a_di_;
    int loop_b_di_;
    int loop_c_di_;
    int intercom_di_;
    int station_door_open_di_;
    int barrier_door_open_di_;
    int barrier_status_di_;
    int manual_open_barrier_di_;
    int lorry_sensor_di_;
    int arm_broken_di_;
    int print_receipt_di_;
    int loop_a_di_last_val_;
    int loop_b_di_last_val_;
    int loop_c_di_last_val_;
    int intercom_di_last_val_;
    int station_door_open_di_last_val_;
    int barrier_door_open_di_last_val_;
    int barrier_status_di_last_val_;
    int manual_open_barrier_di_last_val_;
    int lorry_sensor_di_last_val_;
    int arm_broken_di_last_val_;
    int print_receipt_di_last_val_;
    int open_barrier_do_;
    int lcd_backlight_do_;
    int close_barrier_do_;
    std::atomic<bool> isDIOMonitoringThreadRunning_;
    bool isGPIOInitialized_;
    std::thread dioMonitoringThread_;
    int manual_open_barrier_status_flag_;
    std::mutex manual_open_barrier_status_flag_mutex_;
    int64_t iBarrierOpenTooLongTime_;
    bool bIsBarrierOpenTooLongTime_;
    DIO();
    int getInputPinNum(int pinNum);
    int getOutputPinNum(int pinNum);
    void monitoringDIOChangeThreadFunction();
};