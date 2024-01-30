#pragma once

#include <iostream>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <vector>
#include "boost/asio.hpp"
#include "boost/asio/posix/stream_descriptor.hpp"

class DIO
{
public:
    enum class DIO_EVENT
    {
        LOOP_A_EVENT = 0,
        LOOP_B_EVENT = 1,
        LOOP_C_EVENT = 2,
        INTERCOM_EVENT = 3,
        STATION_DOOR_OPEN_EVENT = 4,
        BARRIER_DOOR_OPEN_EVENT = 5,
        BARRIER_STATUS_EVENT = 6
    };

    static DIO* getInstance();
    void FnDIOInit();
    void FnStartDIOMonitoring();
    void FnStopDIOMonitoring();
    void FnSetOpenBarrier(int value);
    int FnGetOpenBarrier() const;
    void FnSetLCDBacklight(int value);
    int FnGetLCDBacklight() const;

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
    std::string logFileName_;
    int loop_a_di_;
    int loop_b_di_;
    int loop_c_di_;
    int intercom_di_;
    int station_door_open_di_;
    int barrier_door_open_di_;
    int barrier_status_di_;
    int loop_a_di_last_val_;
    int loop_b_di_last_val_;
    int loop_c_di_last_val_;
    int intercom_di_last_val_;
    int station_door_open_di_last_val_;
    int barrier_door_open_di_last_val_;
    int barrier_status_di_last_val_;
    int open_barrier_do_;
    int lcd_backlight_do_;
    bool isDIOMonitoringThreadRunning_;
    std::thread dioMonitoringThread_;
    DIO();
    int getInputPinNum(int pinNum);
    int getOutputPinNum(int pinNum);
    void monitoringDIOChangeThreadFunction();
};