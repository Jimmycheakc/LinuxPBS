#pragma once

#include <iostream>
#include <filesystem>
#include <string>
#include "log.h"

class MountManager
{

public:
    MountManager(const std::string& sharedFolderPath, const std::string& mountPoint, const std::string& username, const std::string& password, const std::string& logFileName, const std::string& sOption)
    : mountPoint_(mountPoint), logFileName_(logFileName), sOption_(sOption), isMounted_(false)
    {
        try
        {
            if (!std::filesystem::exists(mountPoint))
            {
                std::error_code ec;
                if (!std::filesystem::create_directories(mountPoint, ec))
                {
                    Logger::getInstance()->FnLog("Failed to create " + mountPoint + " directory: " + ec.message(), logFileName, sOption);
                    return;
                }
                else
                {
                    Logger::getInstance()->FnLog("Successfully to create " + mountPoint + " directory.", logFileName, sOption);
                }
            }
            else
            {
                Logger::getInstance()->FnLog("Mount point directory: " + mountPoint + " exists.", logFileName, sOption);
            }

            std::string mountCommand = "sudo mount -t cifs " + sharedFolderPath + " " + mountPoint +
                                        " -o username=" + username + ",password=" + password;
            if (std::system(mountCommand.c_str()) == 0)
            {
                Logger::getInstance()->FnLog("Mount cmd: " + mountCommand, logFileName, sOption);
                Logger::getInstance()->FnLog("Successfully to mount " + mountPoint, logFileName, sOption);
                isMounted_ = true;
            }
            else
            {
                Logger::getInstance()->FnLog("Failed to mount " + mountPoint, logFileName, sOption);
            }
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            std::stringstream ss;
            ss << __func__ << ", Filesystem Exception: " << e.what();
            Logger::getInstance()->FnLogExceptionError(ss.str());
        }
        catch (const std::exception& e)
        {
            std::stringstream ss;
            ss << __func__ << ", Exception: " << e.what();
            Logger::getInstance()->FnLogExceptionError(ss.str());
        }
        catch (...)
        {
            std::stringstream ss;
            ss << __func__ << ", Exception: Unknown Exception";
            Logger::getInstance()->FnLogExceptionError(ss.str());
        }
    }

    ~MountManager()
    {
        try
        {
            if (isMounted_)
            {
                std::string unmountCommand = "sudo umount " + mountPoint_;
                if (std::system(unmountCommand.c_str()) == 0)
                {
                    Logger::getInstance()->FnLog("Successfully unmounted " + mountPoint_, logFileName_, sOption_);
                }
                else
                {
                    Logger::getInstance()->FnLog("Failed to unmount " + mountPoint_, logFileName_, sOption_);
                }
            }
        }
        catch (const std::exception& e)
        {
            std::stringstream ss;
            ss << __func__ << ", Exception: " << e.what();
            Logger::getInstance()->FnLogExceptionError(ss.str());
        }
        catch (...)
        {
            std::stringstream ss;
            ss << __func__ << ", Exception: Unknown Exception";
            Logger::getInstance()->FnLogExceptionError(ss.str());
        }
    }

    bool isMounted() const
    {
        return isMounted_;
    }

private:
    std::string mountPoint_;
    std::string logFileName_;
    std::string sOption_;
    bool isMounted_;
};
