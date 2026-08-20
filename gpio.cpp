#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

#include "gpio.h"
#include "log.h"

namespace
{
constexpr auto kExportWait = std::chrono::milliseconds(50);
}

SysfsGPIO::SysfsGPIO(int pinNumber)
    : pinNumber_(pinNumber),
      gpioPath_("/sys/class/gpio/gpio" + std::to_string(pinNumber_) + "/"),
      available_(false),
      exportedByThisInstance_(false)
{
    available_ = FnExportGPIO();
}

SysfsGPIO::~SysfsGPIO()
{
    if (exportedByThisInstance_)
    {
        FnUnexportGPIO();
    }
}

bool SysfsGPIO::FnExportGPIO()
{
    try
    {
        // If the GPIO is already exported by the system or another owner,
        // reuse it instead of exporting it again. In this case we must not
        // unexport it from our destructor because we do not own that export.
        if (std::filesystem::exists(gpioPath_))
        {
            exportedByThisInstance_ = false;
            return true;
        }

        std::ofstream exportFile("/sys/class/gpio/export");
        if (!exportFile.is_open())
        {
            Logger::getInstance()->FnLog("Failed to open /sys/class/gpio/export for GPIO " + std::to_string(pinNumber_));
            return false;
        }

        exportFile << pinNumber_;
        exportFile.flush();

        if (!exportFile.good())
        {
            Logger::getInstance()->FnLog("Failed to export GPIO " + std::to_string(pinNumber_));
            return false;
        }

        exportedByThisInstance_ = true;

        // GPIO sysfs creation is asynchronous in the kernel. This is only
        // initialization code, so a short synchronous wait is acceptable.
        std::this_thread::sleep_for(kExportWait);

        if (!std::filesystem::exists(gpioPath_))
        {
            Logger::getInstance()->FnLog("GPIO path was not created after export: " + gpioPath_);
            return false;
        }

        return true;
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", GPIO: " << pinNumber_ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return false;
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", GPIO: " << pinNumber_ << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return false;
    }
}

bool SysfsGPIO::FnUnexportGPIO()
{
    try
    {
        if (!exportedByThisInstance_)
        {
            return true;
        }

        std::ofstream unexportFile("/sys/class/gpio/unexport");
        if (!unexportFile.is_open())
        {
            Logger::getInstance()->FnLog("Failed to open /sys/class/gpio/unexport for GPIO " + std::to_string(pinNumber_));
            return false;
        }

        unexportFile << pinNumber_;
        unexportFile.flush();

        if (!unexportFile.good())
        {
            Logger::getInstance()->FnLog("Failed to unexport GPIO " + std::to_string(pinNumber_));
            return false;
        }

        exportedByThisInstance_ = false;
        available_ = false;
        return true;
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", GPIO: " << pinNumber_ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return false;
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", GPIO: " << pinNumber_ << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return false;
    }
}

bool SysfsGPIO::FnSetDirection(const std::string& direction)
{
    std::lock_guard<std::mutex> lock(gpioMutex_);

    try
    {
        if (!available_)
        {
            return false;
        }

        std::ofstream directionFile(gpioPath_ + "direction");
        if (!directionFile.is_open())
        {
            Logger::getInstance()->FnLog("Failed to set GPIO direction for pin " + std::to_string(pinNumber_));
            return false;
        }

        directionFile << direction;
        directionFile.flush();

        if (!directionFile.good())
        {
            Logger::getInstance()->FnLog("Failed to write GPIO direction for pin " + std::to_string(pinNumber_));
            return false;
        }

        return true;
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", GPIO: " << pinNumber_ << ", Direction: " << direction << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return false;
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", GPIO: " << pinNumber_ << ", Direction: " << direction << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return false;
    }
}

bool SysfsGPIO::FnSetValue(int value)
{
    std::lock_guard<std::mutex> lock(gpioMutex_);

    try
    {
        if (!available_)
        {
            return false;
        }

        std::ofstream valueFile(gpioPath_ + "value");
        if (!valueFile.is_open())
        {
            Logger::getInstance()->FnLog("Failed to set GPIO value for pin " + std::to_string(pinNumber_));
            return false;
        }

        valueFile << value;
        valueFile.flush();

        if (!valueFile.good())
        {
            Logger::getInstance()->FnLog("Failed to write GPIO value for pin " + std::to_string(pinNumber_));
            return false;
        }

        return true;
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", GPIO: " << pinNumber_ << ", Value: " << value << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return false;
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", GPIO: " << pinNumber_ << ", Value: " << value << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return false;
    }
}

int SysfsGPIO::FnGetValue() const
{
    std::lock_guard<std::mutex> lock(gpioMutex_);

    try
    {
        if (!available_)
        {
            return -1;
        }

        std::ifstream valueFile(gpioPath_ + "value");
        if (!valueFile.is_open())
        {
            Logger::getInstance()->FnLog("Failed to get GPIO value for pin " + std::to_string(pinNumber_));
            return -1;
        }

        int value = -1;
        valueFile >> value;

        if (valueFile.fail())
        {
            Logger::getInstance()->FnLog("Failed to read GPIO value for pin " + std::to_string(pinNumber_));
            return -1;
        }

        return value;
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", GPIO: " << pinNumber_ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return -1;
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", GPIO: " << pinNumber_ << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return -1;
    }
}

std::string SysfsGPIO::FnGetGPIOPath() const
{
    return gpioPath_;
}

bool SysfsGPIO::FnIsReady() const noexcept
{
    return available_;
}


// GPIO Manager Code
GPIOManager* GPIOManager::getInstance()
{
    static GPIOManager instance;
    return &instance;
}

bool GPIOManager::FnGPIOInit()
{
    std::lock_guard<std::mutex> lock(gpioPinsMutex_);

    if (initialized_)
    {
        return true;
    }

    const std::vector<int> outputPins = {
        PIN_DO1, PIN_DO2, PIN_DO3, PIN_DO4, PIN_DO5, PIN_DO6,
        PIN_DO7, PIN_DO8, PIN_DO9
    };

    const std::vector<int> inputPins = {
        PIN_DI1, PIN_DI2, PIN_DI3, PIN_DI4, PIN_DI5, PIN_DI6,
        PIN_DI7, PIN_DI8, PIN_DI9, PIN_DI10, PIN_DI11, PIN_DI12,
        PIN_DI13, PIN_DI14, PIN_DI15, PIN_DI16
    };

    for (int pin : outputPins)
    {
        if (!FnInitSetGPIODirection(pin, GPIO_OUT))
        {
            Logger::getInstance()->FnLog(
                "Failed to initialize output pin: " + std::to_string(pin));
            gpioPins_.clear();
            return false;
        }
    }

    for (int pin : inputPins)
    {
        if (!FnInitSetGPIODirection(pin, GPIO_IN))
        {
            Logger::getInstance()->FnLog(
                "Failed to initialize input pin: " + std::to_string(pin));
            gpioPins_.clear();
            return false;
        }
    }

    initialized_ = true;
    return true;
}

SysfsGPIO* GPIOManager::FnGetGPIO(int pinNumber)
{
    std::lock_guard<std::mutex> lock(gpioPinsMutex_);

    const auto it = gpioPins_.find(pinNumber);
    return (it != gpioPins_.end()) ? it->second.get() : nullptr;
}

bool GPIOManager::FnInitSetGPIODirection(int pinNumber, const std::string& dir)
{
    auto gpio = std::make_unique<SysfsGPIO>(pinNumber);

    if (!gpio->FnIsReady())
    {
        Logger::getInstance()->FnLog("GPIO is not available for pin " + std::to_string(pinNumber));
        return false;
    }

    bool success = false;

    if (dir == GPIO_IN)
    {
        success = gpio->FnSetDirection(GPIO_IN);
    }
    else if (dir == GPIO_OUT)
    {
        success = gpio->FnSetDirection(GPIO_OUT) && gpio->FnSetValue(GPIO_LOW);
    }

    if (!success)
    {
        std::stringstream ss;
        ss << "Failed to configure pin " << pinNumber << " as " << dir;
        Logger::getInstance()->FnLog(ss.str());
        return false;
    }

    gpioPins_.emplace(pinNumber, std::move(gpio));
    return true;
}