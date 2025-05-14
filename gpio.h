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
    mutable std::mutex gpioMutex_;

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
    static const int PIN_DO1               = 413;
    static const int PIN_DO2               = 412;
    static const int PIN_DO3               = 378;
    static const int PIN_DO4               = 367;
    static const int PIN_DO5               = 366;
    static const int PIN_DO6               = 365;

    // Define PINOUT J2 (Output 5v)
    static const int PIN_DO7               = 415;   // J2 01
    static const int PIN_DO8               = 416;   // J2 03
    static const int PIN_DO9               = 417;   // J2 07
    //static const int PIN_DO10              = 418;   // J2 09 (Disabled :Use for USB Hub Reset)

    // Define PININ
    static const int PIN_DI1               = 373;
    static const int PIN_DI2               = 376;
    static const int PIN_DI3               = 377;
    static const int PIN_DI4               = 357;
    static const int PIN_DI5               = 358;
    static const int PIN_DI6               = 372;
    static const int PIN_DI7               = 343;
    static const int PIN_DI8               = 344;
    static const int PIN_DI9               = 345;
    static const int PIN_DI10              = 340;
    static const int PIN_DI11              = 341;
    static const int PIN_DI12              = 342;

    // Define PININ J2 (Input 5v)
    static const int PIN_DI13              = 414;   // J2 02
    static const int PIN_DI14              = 420;   // J2 04
    static const int PIN_DI15              = 419;   // J2 08
    static const int PIN_DI16              = 421;   // J2 010

    static GPIOManager* getInstance();
    bool FnGPIOInit();
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
    bool FnInitSetGPIODirection(int pinNumber, const std::string& dir);
};