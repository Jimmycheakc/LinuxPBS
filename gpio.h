#pragma once

#include <iostream>
#include <fstream>
#include <memory>
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
    static const int PIN_DO1               = 354;
    static const int PIN_DO2               = 355;
    static const int PIN_DO3               = 356;
    static const int PIN_DO4               = 367;
    static const int PIN_DO5               = 511;
    static const int PIN_DO6               = 510;

    // Define PININ
    static const int PIN_DI1               = 329;
    static const int PIN_DI2               = 330;
    static const int PIN_DI3               = 331;
    static const int PIN_DI4               = 332;
    static const int PIN_DI5               = 333;
    static const int PIN_DI6               = 334;
    static const int PIN_DI7               = 346;
    static const int PIN_DI8               = 347;
    static const int PIN_DI9               = 361;
    static const int PIN_DI11              = 362;
    static const int PIN_DI12              = 365;
    static const int PIN_DI13              = 366;

    // Define PINOUT J2 (Output 5v)
    static const int PIN_DOJ1              = 507;
    static const int PIN_DOJ2              = 508;
    static const int PIN_DOJ3              = 505;
    static const int PIN_DOJ4              = 506;
    static const int PIN_DOJ7              = 504;
    static const int PIN_DOJ8              = 503;
    static const int PIN_DOJ9              = 502;
    static const int PIN_DOJ10             = 501;

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
    std::unordered_map<int, std::unique_ptr<SysfsGPIO>> gpioPins_;
    GPIOManager();
    ~GPIOManager();
    void FnInitSetGPIODirection(int pinNumber, const std::string& dir);
};