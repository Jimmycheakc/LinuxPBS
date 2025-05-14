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
    try
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
        usleep(50000); // Sleep for 100ms to allow the kernel to export the GPIO

        return true;
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", GPIO: " << std::to_string(pinNumber_) << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return false;
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", GPIO: " << std::to_string(pinNumber_) << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return false;
    }
}

bool SysfsGPIO::FnUnexportGPIO()
{
    try
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
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", GPIO: " << std::to_string(pinNumber_) << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return false;
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", GPIO: " << std::to_string(pinNumber_) << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return false;
    }
}

bool SysfsGPIO::FnSetDirection(const std::string& direction)
{
    std::lock_guard<std::mutex> lock(gpioMutex_);
    try
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
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", GPIO: " << std::to_string(pinNumber_) << ", Direction: " << direction << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return false;
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", GPIO: " << std::to_string(pinNumber_) << ", Direction: " << direction << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return false;
    }
}

bool SysfsGPIO::FnSetValue(int value)
{
    std::lock_guard<std::mutex> lock(gpioMutex_);
    try
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
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", GPIO: " << std::to_string(pinNumber_) << " ,Value: " << std::to_string(value) << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return false;
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", GPIO: " << std::to_string(pinNumber_) << " ,Value: " << std::to_string(value) << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return false;
    }
}

int SysfsGPIO::FnGetValue() const
{
    std::lock_guard<std::mutex> lock(gpioMutex_);
    try
    {
        std::ifstream valueFile(gpioPath_ + "value");
        if (!valueFile.is_open())
        {
            std::stringstream ss;
            ss << "Failed to get GPIO value for pin " << std::to_string(pinNumber_) << std::endl;
            Logger::getInstance()->FnLog(ss.str());
            return -1;
        }

        int value;
        valueFile >> value;

        valueFile.close();

        return value;
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", GPIO: " << std::to_string(pinNumber_) << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return -1;
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", GPIO: " << std::to_string(pinNumber_) << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return -1;
    }
}

std::string SysfsGPIO::FnGetGPIOPath() const
{
    return gpioPath_;
}


// GPIO Manager Code
GPIOManager* GPIOManager::GPIOManager_;
std::mutex GPIOManager::mutex_;

GPIOManager::GPIOManager()
{

}

GPIOManager::~GPIOManager()
{
    gpioPins_.clear();
}

GPIOManager* GPIOManager::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (GPIOManager_ == nullptr)
    {
        GPIOManager_ = new GPIOManager();
    }
    return GPIOManager_;
}

bool GPIOManager::FnGPIOInit()
{
    // List all output pins
    const std::vector<int> outputPins = {
        PIN_DO1, PIN_DO2, PIN_DO3, PIN_DO4, PIN_DO5, PIN_DO6,
        PIN_DO7, PIN_DO8, PIN_DO9
        // PIN_DO10 (Disabled: Use for USB Hub Reset)
    };

    // List all input pins
    const std::vector<int> inputPins = {
        PIN_DI1, PIN_DI2, PIN_DI3, PIN_DI4, PIN_DI5, PIN_DI6,
        PIN_DI7, PIN_DI8, PIN_DI9, PIN_DI10, PIN_DI11, PIN_DI12,
        PIN_DI13, PIN_DI14, PIN_DI15, PIN_DI16
    };

    // Initialize output pins
    for (int pin : outputPins)
    {
        if (!FnInitSetGPIODirection(pin, GPIO_OUT))
        {
            Logger::getInstance()->FnLog("Failed to initialize output pin: " + std::to_string(pin));
            return false;
        }
    }

    // Initialize input pins
    for (int pin : inputPins)
    {
        if (!FnInitSetGPIODirection(pin, GPIO_IN))
        {
            Logger::getInstance()->FnLog("Failed to initialize input pin: " + std::to_string(pin));
            return false;
        }
    }

    return true;
}

SysfsGPIO* GPIOManager::FnGetGPIO(int pinNumber)
{
    auto it = gpioPins_.find(pinNumber);
    return (it != gpioPins_.end()) ? it->second.get() : nullptr;
}

bool GPIOManager::FnInitSetGPIODirection(int pinNumber, const std::string& dir)
{
    bool success = false;
    std::unique_ptr<SysfsGPIO> gpio = std::make_unique<SysfsGPIO>(pinNumber);
    if (dir == GPIO_IN)
    {
        success = gpio->FnSetDirection(GPIO_IN);
    }
    else if (dir == GPIO_OUT)
    {
        success = gpio->FnSetDirection(GPIO_OUT) && gpio->FnSetValue(GPIO_LOW);
    }

    if (success)
    {
        gpioPins_[pinNumber] = std::move(gpio);
    }
    else
    {
        std::stringstream ss;
        ss << "Failed to configure pin " << std::to_string(pinNumber) + " as " << dir << std::endl;
        Logger::getInstance()->FnLog(ss.str());
    }

    return success;
}