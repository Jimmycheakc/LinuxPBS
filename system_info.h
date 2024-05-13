#pragma once

#include <memory>
#include <mutex>

class SystemInfo
{

public:
    static SystemInfo* getInstance();
    void FnLogSysInfo();

    /**
     * Singleton SystemInfo should not be cloneable.
     */
    SystemInfo(SystemInfo &systeminfo) = delete;

    /**
     * Singleton SystemInfo should not be assignable.
     */
    void operator=(const SystemInfo &) = delete;

private:
    static SystemInfo* systeminfo_;
    static std::mutex mutex_;
    SystemInfo();
};