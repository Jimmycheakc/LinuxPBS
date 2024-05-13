#include <iomanip>
#include <sstream>
#include "common.h"
#include "system_info.h"
#include "log.h"

#include <sys/sysinfo.h>

SystemInfo* SystemInfo::systeminfo_;
std::mutex SystemInfo::mutex_;

SystemInfo::SystemInfo()
{

}

SystemInfo* SystemInfo::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (systeminfo_ == nullptr)
    {
        systeminfo_ = new SystemInfo();
    }
    return systeminfo_;
}

void SystemInfo::FnLogSysInfo()
{
    struct sysinfo info;
    
    int ret = sysinfo(&info);
    if (ret == 0)
    {
        // 1 Gigabyte = 1024 megabytes = 1024 * 1024 kbytes = 1024 * 1024 * 1024 bytes;
        constexpr double factor = 1024 * 1024 * 1024;
        constexpr std::uint64_t one_day_to_seconds = 24 * 60 * 60;

        std::stringstream ssinfo;
        ssinfo << "*** Start display system information ***\n"
               << Common::getInstance()->FnGetDateTimeSpace()
               << "[*] System uptime since boot (seconds) = " << info.uptime << "\n"
               << Common::getInstance()->FnGetDateTimeSpace()
               << "[*] System uptime since boot    (days) = " << info.uptime / one_day_to_seconds << "\n"
               << Common::getInstance()->FnGetDateTimeSpace()
               << "[*]              Total RAM memory (Gb) = " << info.totalram / factor << "\n"
               << Common::getInstance()->FnGetDateTimeSpace()
               << "[*]               Free RAM memory (Gb) = " << info.freeram / factor << "\n"
               << Common::getInstance()->FnGetDateTimeSpace()
               << "[*] Percentage of used RAM memory ( %) = " << (((info.totalram - info.freeram) / static_cast<double>(info.totalram)) * 100.0) << "\n"
               << Common::getInstance()->FnGetDateTimeSpace()
               << "[*]                    Total SWAP (Gb) = " << info.totalswap / factor << "\n"
               << Common::getInstance()->FnGetDateTimeSpace()
               << "[*]                     Free SWAP (Gb) = " << info.freeswap / factor << "\n"
               << Common::getInstance()->FnGetDateTimeSpace()
               << "[*]        Number of processes running = " << info.procs << "\n"
               << Common::getInstance()->FnGetDateTimeSpace()
               << "*** End display system information ***\n";

        Logger::getInstance()->FnLog(ssinfo.str());
    }
    else
    {
        std::string errorMsg = "Failed to call sysinfo()\n";
        Logger::getInstance()->FnLog(errorMsg);
    }
}

