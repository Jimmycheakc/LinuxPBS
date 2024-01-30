#include <unistd.h>
#include <sstream>
#include "gpio.h"
#include "log.h"

SysfsGPIO::SysfsGPIO(int pinNumber) : pinNumber_(pinNumber)
{
    gpioPath_ = "/sys/class/gpio/gpio" + std::to_string(pinNumber_) + "/";
    FnExportGPIO();
}

SysfsGPIO::~SysfsGPIO()
{
    FnUnexportGPIO();
}

bool SysfsGPIO::FnExportGPIO()
{
    std::ofstream exportFile("/sys/class/gpio/export");
    if (!exportFile.is_open())
    {
        std::stringstream ss;
        ss << "Failed to export GPIO " << std::to_string(pinNumber_) << std::endl;
        Logger::getInstance()->FnLog(ss.str());
        return false;
    }

    exportFile << pinNumber_;
    exportFile.close();
    usleep(100000); // Sleep for 100ms to allow the kernel to export the GPIO

    return true;
}

bool SysfsGPIO::FnUnexportGPIO()
{
    std::ofstream unexportFile("/sys/class/gpio/unexport");
    if (!unexportFile.is_open())
    {
        std::stringstream ss;
        ss << "Failed to unexport GPIO " << std::to_string(pinNumber_) << std::endl;
        Logger::getInstance()->FnLog(ss.str());
        return false;
    }

    unexportFile << pinNumber_;
    unexportFile.close();

    return true;
}

bool SysfsGPIO::FnSetDirection(const std::string& direction)
{
    std::ofstream directionFile(gpioPath_ + "direction");
    if (!directionFile.is_open())
    {
        std::stringstream ss;
        ss << "Failed to set GPIO direction for pin " << std::to_string(pinNumber_) << std::endl;
        Logger::getInstance()->FnLog(ss.str());
        return false;
    }

    directionFile << direction;
    directionFile.close();

    return true;
}

bool SysfsGPIO::FnSetValue(int value)
{
    std::ofstream valueFile(gpioPath_ + "value");
    if (!valueFile.is_open())
    {
        std::stringstream ss;
        ss << "Failed to set GPIO value for pin " << std::to_string(pinNumber_) << std::endl;
        Logger::getInstance()->FnLog(ss.str());
        return false;
    }

    valueFile << value;
    valueFile.close();

    return true;
}

int SysfsGPIO::FnGetValue() const
{
    std::ifstream valueFile(gpioPath_ + "value");
    if (!valueFile.is_open())
    {
        std::stringstream ss;
        ss << "Failed to get GPIO value for pin " << std::to_string(pinNumber_) << std::endl;
        Logger::getInstance()->FnLog(ss.str());
        return false;
    }

    int value;
    valueFile >> value;

    valueFile.close();

    return value;
}

std::string SysfsGPIO::FnGetGPIOPath() const
{
    return gpioPath_;
}


// GPIO Manager Code
GPIOManager* GPIOManager::GPIOManager_;

GPIOManager::GPIOManager()
{

}

GPIOManager::~GPIOManager()
{
    gpioPins_.clear();
}

GPIOManager* GPIOManager::getInstance()
{
    if (GPIOManager_ == nullptr)
    {
        GPIOManager_ = new GPIOManager();
    }
    return GPIOManager_;
}

void GPIOManager::FnGPIOInit()
{
    FnInitSetGPIODirection(PIN_DO1, GPIO_OUT);
    FnInitSetGPIODirection(PIN_DO2, GPIO_OUT);
    FnInitSetGPIODirection(PIN_DO3, GPIO_OUT);
    FnInitSetGPIODirection(PIN_DO4, GPIO_OUT);
    FnInitSetGPIODirection(PIN_DO5, GPIO_OUT);
    FnInitSetGPIODirection(PIN_DO6, GPIO_OUT);

    FnInitSetGPIODirection(PIN_DO7, GPIO_OUT);
    FnInitSetGPIODirection(PIN_DO8, GPIO_OUT);
    FnInitSetGPIODirection(PIN_DO9, GPIO_OUT);
    FnInitSetGPIODirection(PIN_DO10, GPIO_OUT);

    FnInitSetGPIODirection(PIN_DI1, GPIO_IN);
    FnInitSetGPIODirection(PIN_DI2, GPIO_IN);
    FnInitSetGPIODirection(PIN_DI3, GPIO_IN);
    FnInitSetGPIODirection(PIN_DI4, GPIO_IN);
    FnInitSetGPIODirection(PIN_DI5, GPIO_IN);
    FnInitSetGPIODirection(PIN_DI6, GPIO_IN);
    FnInitSetGPIODirection(PIN_DI7, GPIO_IN);
    FnInitSetGPIODirection(PIN_DI8, GPIO_IN);
    FnInitSetGPIODirection(PIN_DI9, GPIO_IN);
    FnInitSetGPIODirection(PIN_DI10, GPIO_IN);
    FnInitSetGPIODirection(PIN_DI11, GPIO_IN);
    FnInitSetGPIODirection(PIN_DI12, GPIO_IN);

    FnInitSetGPIODirection(PIN_DI13, GPIO_IN);
    FnInitSetGPIODirection(PIN_DI14, GPIO_IN);
    FnInitSetGPIODirection(PIN_DI15, GPIO_IN);
    FnInitSetGPIODirection(PIN_DI16, GPIO_IN);
}

SysfsGPIO* GPIOManager::FnGetGPIO(int pinNumber)
{
    auto it = gpioPins_.find(pinNumber);
    return (it != gpioPins_.end()) ? it->second.get() : nullptr;
}

void GPIOManager::FnInitSetGPIODirection(int pinNumber, const std::string& dir)
{
    std::unique_ptr<SysfsGPIO> gpio = std::make_unique<SysfsGPIO>(pinNumber);
    if (dir == GPIO_IN)
    {
        gpio->FnSetDirection(GPIO_IN);
    }
    else if (dir == GPIO_OUT)
    {
        gpio->FnSetDirection(GPIO_OUT);
        gpio->FnSetValue(GPIO_LOW);
    }
    gpioPins_[pinNumber] = std::move(gpio);
}