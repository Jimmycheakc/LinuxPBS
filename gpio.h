#pragma once

#include <iostream>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

class SysfsGPIO
{

public:
    SysfsGPIO(int pinNumber);
    ~SysfsGPIO();
    bool FnSetDirection(const std::string& direction);
    bool FnSetValue(int value);
    int FnGetValue() const;
    std::string FnGetGPIOPath() const;

private:
    int pinNumber_;
    std::string gpioPath_;

    bool FnExportGPIO();
    bool FnUnexportGPIO();
};


// GPIO Manager Class
class GPIOManager
{

public:
    const std::string GPIO_OUT             = "out";
    const std::string GPIO_IN              = "in";
    static const int GPIO_HIGH             = 1;
    static const int GPIO_LOW              = 0;

    // Define PINOUT
    static const int PIN_DO1               = 510;
    static const int PIN_DO2               = 511;
    static const int PIN_DO3               = 367;
    static const int PIN_DO4               = 356;
    static const int PIN_DO5               = 355;
    static const int PIN_DO6               = 354;

    // Define PINOUT J2 (Output 5v)
    static const int PIN_DO7               = 507;   // J2 01
    static const int PIN_DO8               = 506;   // J2 03
    static const int PIN_DO9               = 505;   // J2 07
    //static const int PIN_DO10              = 504;   // J2 09 (Disabled :Use for USB Hub Reset)

    // Define PININ
    static const int PIN_DI1               = 362;
    static const int PIN_DI2               = 365;
    static const int PIN_DI3               = 366;
    static const int PIN_DI4               = 346;
    static const int PIN_DI5               = 347;
    static const int PIN_DI6               = 361;
    static const int PIN_DI7               = 332;
    static const int PIN_DI8               = 333;
    static const int PIN_DI9               = 334;
    static const int PIN_DI10              = 329;
    static const int PIN_DI11              = 330;
    static const int PIN_DI12              = 331;

    // Define PININ J2 (Input 5v)
    static const int PIN_DI13              = 508;   // J2 02
    static const int PIN_DI14              = 502;   // J2 04
    static const int PIN_DI15              = 503;   // J2 08
    static const int PIN_DI16              = 501;   // J2 010

    static GPIOManager* getInstance();
    void FnGPIOInit();
    SysfsGPIO* FnGetGPIO(int pinNumber);

    /**
     * Singleton GPIOManager should not be cloneable.
     */
    GPIOManager(GPIOManager& gpioManager) = delete;

    /**
     * Singleton GPIOManager should not be assignable.
     */
    void operator=(const GPIOManager &) = delete;

private:
    static GPIOManager* GPIOManager_;
    static std::mutex mutex_;
    std::unordered_map<int, std::unique_ptr<SysfsGPIO>> gpioPins_;
    GPIOManager();
    ~GPIOManager();
    void FnInitSetGPIODirection(int pinNumber, const std::string& dir);
};