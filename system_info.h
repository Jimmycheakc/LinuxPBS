#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

class SystemInfo final
{
public:
    static SystemInfo* getInstance();

    SystemInfo(const SystemInfo&) = delete;
    SystemInfo& operator=(const SystemInfo&) = delete;
    SystemInfo(SystemInfo&&) = delete;
    SystemInfo& operator=(SystemInfo&&) = delete;

    void FnLogSysInfo() const;

private:
    struct MemoryInfo
    {
        std::uint64_t totalBytes{0};
        std::uint64_t availableBytes{0};
        std::uint64_t usedBytes{0};
        std::uint64_t swapTotalBytes{0};
        std::uint64_t swapFreeBytes{0};
        std::uint64_t swapUsedBytes{0};
        double usedPercent{0.0};
        double swapUsedPercent{0.0};
        bool valid{false};
    };

    struct DiskInfo
    {
        std::uint64_t totalBytes{0};
        std::uint64_t availableBytes{0};
        std::uint64_t usedBytes{0};
        double usedPercent{0.0};
        bool valid{false};
    };

    struct ProcessInfo
    {
        std::string name;
        int pid{0};
        std::uint64_t rssBytes{0};
        std::uint64_t virtualMemoryBytes{0};
        std::uint64_t threadCount{0};
        std::uint64_t openFileDescriptors{0};
        bool valid{false};
    };

    struct SystemIdentity
    {
        std::string hostname;
        std::string kernel;
        std::string architecture;
        bool valid{false};
    };

    struct SystemRuntimeInfo
    {
        std::uint64_t uptimeSeconds{0};
        std::uint64_t processCount{0};
        bool valid{false};
    };

    struct LoadAverage
    {
        std::array<double, 3> values{0.0, 0.0, 0.0};
        bool valid{false};
    };

    SystemInfo() = default;
    ~SystemInfo() = default;

    static MemoryInfo getMemoryInfo();
    static DiskInfo getDiskInfo(const std::filesystem::path& path);
    static ProcessInfo getProcessInfo();
    static SystemIdentity getSystemIdentity();
    static SystemRuntimeInfo getSystemRuntimeInfo();
    static LoadAverage getLoadAverage();
    static long getCpuCoreCount();
    static std::uint64_t getOpenFileDescriptorCount();
    static std::uint64_t getApplicationRuntimeSeconds();

    static double percentage(std::uint64_t value, std::uint64_t total);

    static std::string formatGiB(std::uint64_t bytes);
    static std::string formatMiB(std::uint64_t bytes);
    static std::string formatPercent(double value);
};
