#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

class SysfsGPIO
{

public:
    explicit SysfsGPIO(int pinNumber);
    ~SysfsGPIO();

    SysfsGPIO(const SysfsGPIO&) = delete;
    SysfsGPIO& operator=(const SysfsGPIO&) = delete;
    SysfsGPIO(SysfsGPIO&&) = delete;
    SysfsGPIO& operator=(SysfsGPIO&&) = delete;

    bool FnSetDirection(const std::string& direction);
    bool FnSetValue(int value);
    int FnGetValue() const;
    std::string FnGetGPIOPath() const;
    bool FnIsReady() const noexcept;

private:
    bool FnExportGPIO();
    bool FnUnexportGPIO();

    int pinNumber_;
    std::string gpioPath_;
    mutable std::mutex gpioMutex_;
    bool available_;
    bool exportedByThisInstance_;
};


// GPIO Manager Class
class GPIOManager
{

public:
    const std::string GPIO_OUT             = "out";
    const std::string GPIO_IN              = "in";

    static constexpr int GPIO_HIGH             = 1;
    static constexpr int GPIO_LOW              = 0;

    // Define PINOUT
    static constexpr int PIN_DO1               = 413;
    static constexpr int PIN_DO2               = 412;
    static constexpr int PIN_DO3               = 378;
    static constexpr int PIN_DO4               = 367;
    static constexpr int PIN_DO5               = 366;
    static constexpr int PIN_DO6               = 365;

    // Define PINOUT J2 (Output 5v)
    static constexpr int PIN_DO7               = 415;   // J2 01
    static constexpr int PIN_DO8               = 416;   // J2 03
    static constexpr int PIN_DO9               = 417;   // J2 07
    //static constexpr int PIN_DO10              = 418;   // J2 09 (Disabled :Use for USB Hub Reset)

    // Define PININ
    static constexpr int PIN_DI1               = 373;
    static constexpr int PIN_DI2               = 376;
    static constexpr int PIN_DI3               = 377;
    static constexpr int PIN_DI4               = 357;
    static constexpr int PIN_DI5               = 358;
    static constexpr int PIN_DI6               = 372;
    static constexpr int PIN_DI7               = 343;
    static constexpr int PIN_DI8               = 344;
    static constexpr int PIN_DI9               = 345;
    static constexpr int PIN_DI10              = 340;
    static constexpr int PIN_DI11              = 341;
    static constexpr int PIN_DI12              = 342;

    // Define PININ J2 (Input 5v)
    static constexpr int PIN_DI13              = 414;   // J2 02
    static constexpr int PIN_DI14              = 420;   // J2 04
    static constexpr int PIN_DI15              = 419;   // J2 08
    static constexpr int PIN_DI16              = 421;   // J2 010

    static GPIOManager* getInstance();

    GPIOManager(const GPIOManager&) = delete;
    GPIOManager& operator=(const GPIOManager&) = delete;
    GPIOManager(GPIOManager&&) = delete;
    GPIOManager& operator=(GPIOManager&&) = delete;

    bool FnGPIOInit();
    SysfsGPIO* FnGetGPIO(int pinNumber);

private:
    GPIOManager() = default;
    ~GPIOManager() = default;

    bool FnInitSetGPIODirection(int pinNumber, const std::string& dir);

    std::unordered_map<int, std::unique_ptr<SysfsGPIO>> gpioPins_;
    std::mutex gpioPinsMutex_;
    bool initialized_{false};
};