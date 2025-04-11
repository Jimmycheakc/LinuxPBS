#include <cctype>
#include <dirent.h>
#include <iostream>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sstream>
#include <libevdev-1.0/libevdev/libevdev.h>
#include <mutex>
#include <thread>
#include <unistd.h>
#include "barcode_reader.h"
#include "event_manager.h"
#include "log.h"

BARCODE_READER* BARCODE_READER::barcode_ = nullptr;
std::mutex BARCODE_READER::mutex_;

BARCODE_READER::BARCODE_READER()
    : isBarcodeMonitoringThreadRunning_(false)
{
    logFileName_ = "barcode";
}

BARCODE_READER* BARCODE_READER::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (barcode_ == nullptr)
    {
        barcode_ = new BARCODE_READER();
    }

    return barcode_;
}

void BARCODE_READER::destroyInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (barcode_)
    {
        delete barcode_;
        barcode_ = nullptr;
    }
}

void BARCODE_READER::FnBarcodeReaderInit()
{
    Logger::getInstance()->FnCreateLogFile(logFileName_);
    if (isDeviceAvailable(barcodeFilePath))
    {
        Logger::getInstance()->FnLog("Barcode Reader initialization completed.");
        Logger::getInstance()->FnLog("Barcode Reader initialization completed.", logFileName_, "BCODE");
    }
    else
    {
        Logger::getInstance()->FnLog("Barcode Reader initialization failed.");
        Logger::getInstance()->FnLog("Barcode Reader initialization failed.", logFileName_, "BCODE");
    }
}

void BARCODE_READER::FnBarcodeStartRead()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "BCODE");
    startBarcodeMonitoring();
}

void BARCODE_READER::FnBarcodeStopRead()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "BCODE");
    stopBarcodeMonitoring();
}

void BARCODE_READER::startBarcodeMonitoring()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "BCODE");

    if (!isBarcodeMonitoringThreadRunning_.load())
    {
        isBarcodeMonitoringThreadRunning_.store(true);
        barcodeMonitoringThread_ = std::thread(&BARCODE_READER::monitoringBarcodeThreadFunction, this);
    }
}

void BARCODE_READER::stopBarcodeMonitoring()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "BCODE");

    if (isBarcodeMonitoringThreadRunning_.load())
    {
        isBarcodeMonitoringThreadRunning_.store(false);
        
        if (barcodeMonitoringThread_.joinable())
        {
            barcodeMonitoringThread_.join();
            Logger::getInstance()->FnLog("thread joined", logFileName_, "BCODE");
        }
    }
}

std::string BARCODE_READER::readBarcode(const std::string& devicePath)
{
    struct libevdev* dev = nullptr;
    int fd = open(devicePath.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0)
    {
        std::stringstream logMsg;
        logMsg << "Failed to open device " << devicePath;
        Logger::getInstance()->FnLog(logMsg.str(), logFileName_, "BCODE");
        return "";
    }

    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0)
    {
        Logger::getInstance()->FnLog("Failed to initialize libevdev.", logFileName_, "BCODE");
        close(fd);
        return "";
    }

    std::string barcode;
    bool caps = false;

    struct input_event ev;
    while (isBarcodeMonitoringThreadRunning_.load())
    {
        rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        if (rc == 0)
        {
            if (ev.type == EV_KEY)
            {
                // Check for caps lock (scancode 42)
                if (ev.code == 42)
                {
                    caps = ev.value == 1;   // 1 is key down
                }

                // Key press (down event)
                if (ev.value == 1)
                {
                    int scancode = ev.code;
                    std::string key_lookup = scancodes[scancode];

                    // Toggle the case based on caps lock
                    if (caps && !key_lookup.empty())
                    {
                        key_lookup[0] = toupper(key_lookup[0]);
                    }

                    // Exclude shift (42) and enter (28)
                    if (scancode != 42 && scancode != 28)
                    {
                        barcode += key_lookup;
                    }

                    // Enter key (code 28)
                    if (scancode == 28)
                    {
                        break;  // Return the barcode on enter key press
                    }
                }
            }   
        }
        else if (rc == -ENODEV)
        {
            Logger::getInstance()->FnLog("Barcode device disconnected in running mode.", logFileName_, "BCODE");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    libevdev_free(dev);
    close(fd);
    return barcode;
}

bool BARCODE_READER::isDeviceAvailable(const std::string& devicePath)
{
    return (access(devicePath.c_str(), F_OK) == 0);
}

void BARCODE_READER::monitoringBarcodeThreadFunction()
{
    bool isBarcodeNotConnectedLogged = false;  // Flag to log retrying message
    bool isBarcodeRecovered = false;            // Flag to log recovery message

    while (isBarcodeMonitoringThreadRunning_.load())
    {
        if (isDeviceAvailable(barcodeFilePath))
        {
            // If the barcode device has been unavailable and it is now available
            if (!isBarcodeRecovered)
            {
                // Log the recovery message
                Logger::getInstance()->FnLog("Barcode device connected", logFileName_, "BCODE");
                isBarcodeRecovered = true;  // Mark that the barcode has recovered
                isBarcodeNotConnectedLogged = false;  // Reset the retry flag
            }

            std::string barcode = readBarcode(barcodeFilePath);
            if (!barcode.empty())
            {
                Logger::getInstance()->FnLog("INFO: Barcode Scanned | Barcode: " +  barcode, logFileName_, "BCODE");
                EventManager::getInstance()->FnEnqueueEvent("Evt_handleBarcodeReceived", barcode);
            }
        }
        else
        {
            // If the barcode device is unavailable, log retrying message only once
            if (!isBarcodeNotConnectedLogged)
            {
                Logger::getInstance()->FnLog("Barcode not connected, retrying...", logFileName_, "BCODE");
                isBarcodeNotConnectedLogged = true;  // Set the flag to avoid repeated messages
                isBarcodeRecovered = false;  // Mark that the barcode has not recovered
            }

            // Wait before retrying
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
}
