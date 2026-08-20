#include "system_info.h"

#include "log.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>


namespace
{
constexpr std::uint64_t kBytesPerKiB = 1024ULL;
constexpr double kBytesPerMiB = 1024.0 * 1024.0;
constexpr double kBytesPerGiB = 1024.0 * 1024.0 * 1024.0;
constexpr std::uint64_t kSecondsPerDay = 24ULL * 60ULL * 60ULL;

std::string trimLeft(std::string value)
{
    const auto first = value.find_first_not_of(" \t");

    if (first == std::string::npos)
    {
        return {};
    }

    value.erase(0, first);
    return value;
}

bool parseProcStatusKiB(const std::string& line, const std::string& prefix, std::uint64_t& outputBytes)
{
    if (!line.starts_with(prefix))
    {
        return false;
    }

    std::istringstream stream(line.substr(prefix.size()));
    std::uint64_t valueKiB{0};

    if (!(stream >> valueKiB))
    {
        return false;
    }

    outputBytes = valueKiB * kBytesPerKiB;
    return true;
}

bool parseProcStatusInteger(const std::string& line, const std::string& prefix, std::uint64_t& outputValue)
{
    if (!line.starts_with(prefix))
    {
        return false;
    }

    std::istringstream stream(line.substr(prefix.size()));
    return static_cast<bool>(stream >> outputValue);
}

} // namespace

SystemInfo* SystemInfo::getInstance()
{
    static SystemInfo instance;
    return &instance;
}

void SystemInfo::FnLogSysInfo() const
{
    const SystemIdentity identity = getSystemIdentity();
    const SystemRuntimeInfo runtime = getSystemRuntimeInfo();
    const long cpuCoreCount = getCpuCoreCount();
    const LoadAverage loadAverage = getLoadAverage();
    const MemoryInfo memory = getMemoryInfo();
    const DiskInfo rootDisk = getDiskInfo("/");
    const ProcessInfo process = getProcessInfo();

    std::ostringstream logStream;

    logStream << "SYSTEM: [INFO]";

    if (identity.valid)
    {
        logStream
            << " Host=" << identity.hostname
            << " | Kernel=" << identity.kernel
            << " | Arch=" << identity.architecture;
    }

    if (runtime.valid)
    {
        const double uptimeDays =
            static_cast<double>(runtime.uptimeSeconds) /
            static_cast<double>(kSecondsPerDay);

        logStream
            << " | Uptime=" << runtime.uptimeSeconds << "s"
            << " (" << std::fixed << std::setprecision(2)
            << uptimeDays << "d)"
            << " | SystemProcesses=" << runtime.processCount;
    }

    if (cpuCoreCount > 0)
    {
        logStream << " | CPU Cores=" << cpuCoreCount;
    }

    if (loadAverage.valid)
    {
        logStream
            << " | Load="
            << std::fixed << std::setprecision(2)
            << loadAverage.values[0] << ","
            << loadAverage.values[1] << ","
            << loadAverage.values[2];
    }

    if (memory.valid)
    {
        logStream
            << " | RAM Total=" << formatGiB(memory.totalBytes)
            << " | RAM Available=" << formatGiB(memory.availableBytes)
            << " | RAM Used=" << formatPercent(memory.usedPercent)
            << " | SWAP Total=" << formatGiB(memory.swapTotalBytes)
            << " | SWAP Free=" << formatGiB(memory.swapFreeBytes)
            << " | SWAP Used=" << formatPercent(memory.swapUsedPercent);
    }

    if (rootDisk.valid)
    {
        logStream
            << " | Disk(/) Total=" << formatGiB(rootDisk.totalBytes)
            << " | Disk(/) Available=" << formatGiB(rootDisk.availableBytes)
            << " | Disk(/) Used=" << formatPercent(rootDisk.usedPercent);
    }

    if (process.valid)
    {
        logStream
            << " | App=" << process.name
            << " | PID=" << process.pid
            << " | App RSS=" << formatMiB(process.rssBytes)
            << " | App VmSize=" << formatMiB(process.virtualMemoryBytes)
            << " | App Threads=" << process.threadCount
            << " | App FDs=" << process.openFileDescriptors;
    }

    Logger::getInstance()->FnLog(logStream.str());
}

SystemInfo::MemoryInfo SystemInfo::getMemoryInfo()
{
    MemoryInfo result;

    std::ifstream file("/proc/meminfo");

    if (!file.is_open())
    {
        return result;
    }

    std::uint64_t memTotalKiB{0};
    std::uint64_t memAvailableKiB{0};
    std::uint64_t memFreeKiB{0};
    std::uint64_t buffersKiB{0};
    std::uint64_t cachedKiB{0};
    std::uint64_t swapTotalKiB{0};
    std::uint64_t swapFreeKiB{0};
    bool hasMemAvailable{false};

    std::string line;

    while (std::getline(file, line))
    {
        const auto colon = line.find(':');

        if (colon == std::string::npos)
        {
            continue;
        }

        const std::string key = line.substr(0, colon);
        std::istringstream valueStream(line.substr(colon + 1));

        std::uint64_t valueKiB{0};

        if (!(valueStream >> valueKiB))
        {
            continue;
        }

        if (key == "MemTotal")
        {
            memTotalKiB = valueKiB;
        }
        else if (key == "MemAvailable")
        {
            memAvailableKiB = valueKiB;
            hasMemAvailable = true;
        }
        else if (key == "MemFree")
        {
            memFreeKiB = valueKiB;
        }
        else if (key == "Buffers")
        {
            buffersKiB = valueKiB;
        }
        else if (key == "Cached")
        {
            cachedKiB = valueKiB;
        }
        else if (key == "SwapTotal")
        {
            swapTotalKiB = valueKiB;
        }
        else if (key == "SwapFree")
        {
            swapFreeKiB = valueKiB;
        }
    }

    if (memTotalKiB == 0)
    {
        return result;
    }

    result.totalBytes = memTotalKiB * kBytesPerKiB;

    if (hasMemAvailable)
    {
        result.availableBytes = memAvailableKiB * kBytesPerKiB;
    }
    else
    {
        const std::uint64_t estimatedAvailableKiB =
            std::min(
                memTotalKiB,
                memFreeKiB + buffersKiB + cachedKiB);

        result.availableBytes =
            estimatedAvailableKiB * kBytesPerKiB;
    }

    result.usedBytes =
        result.totalBytes >= result.availableBytes
            ? result.totalBytes - result.availableBytes
            : 0ULL;

    result.swapTotalBytes = swapTotalKiB * kBytesPerKiB;
    result.swapFreeBytes = swapFreeKiB * kBytesPerKiB;
    result.swapUsedBytes =
        result.swapTotalBytes >= result.swapFreeBytes
            ? result.swapTotalBytes - result.swapFreeBytes
            : 0ULL;

    result.usedPercent = percentage(
        result.usedBytes,
        result.totalBytes);

    result.swapUsedPercent = percentage(
        result.swapUsedBytes,
        result.swapTotalBytes);

    result.valid = true;
    return result;
}

SystemInfo::DiskInfo SystemInfo::getDiskInfo(const std::filesystem::path& path)
{
    DiskInfo result;

    struct statvfs info{};

    if (::statvfs(path.c_str(), &info) != 0)
    {
        return result;
    }

    const std::uint64_t blockSize =
        static_cast<std::uint64_t>(info.f_frsize);

    result.totalBytes =
        static_cast<std::uint64_t>(info.f_blocks) * blockSize;

    const std::uint64_t freeBytes =
        static_cast<std::uint64_t>(info.f_bfree) * blockSize;

    result.availableBytes =
        static_cast<std::uint64_t>(info.f_bavail) * blockSize;

    result.usedBytes =
        result.totalBytes >= freeBytes
            ? result.totalBytes - freeBytes
            : 0ULL;

    result.usedPercent = percentage(
        result.usedBytes,
        result.totalBytes);

    result.valid = result.totalBytes > 0;
    return result;
}

SystemInfo::ProcessInfo SystemInfo::getProcessInfo()
{
    ProcessInfo result;
    result.pid = static_cast<int>(::getpid());
    result.openFileDescriptors = getOpenFileDescriptorCount();

    std::ifstream file("/proc/self/status");

    if (!file.is_open())
    {
        return result;
    }

    std::string line;

    while (std::getline(file, line))
    {
        if (line.starts_with("Name:"))
        {
            result.name = trimLeft(line.substr(5));
            continue;
        }

        if (parseProcStatusKiB(
                line,
                "VmRSS:",
                result.rssBytes))
        {
            continue;
        }

        if (parseProcStatusKiB(
                line,
                "VmSize:",
                result.virtualMemoryBytes))
        {
            continue;
        }

        parseProcStatusInteger(
            line,
            "Threads:",
            result.threadCount);
    }

    if (result.name.empty())
    {
        result.name = "unknown";
    }

    result.valid = true;
    return result;
}

SystemInfo::SystemIdentity SystemInfo::getSystemIdentity()
{
    SystemIdentity result;

    struct utsname info{};

    if (::uname(&info) != 0)
    {
        return result;
    }

    result.hostname = info.nodename;
    result.kernel = info.release;
    result.architecture = info.machine;
    result.valid = true;

    return result;
}

SystemInfo::SystemRuntimeInfo SystemInfo::getSystemRuntimeInfo()
{
    SystemRuntimeInfo result;

    struct ::sysinfo info{};

    if (::sysinfo(&info) != 0)
    {
        return result;
    }

    result.uptimeSeconds =
        info.uptime > 0
            ? static_cast<std::uint64_t>(info.uptime)
            : 0ULL;

    result.processCount =
        static_cast<std::uint64_t>(info.procs);

    result.valid = true;
    return result;
}

SystemInfo::LoadAverage SystemInfo::getLoadAverage()
{
    LoadAverage result;

    std::ifstream file("/proc/loadavg");

    if (!file.is_open())
    {
        return result;
    }

    if (file >> result.values[0]
             >> result.values[1]
             >> result.values[2])
    {
        result.valid = true;
    }

    return result;
}

long SystemInfo::getCpuCoreCount()
{
    const long count = ::sysconf(_SC_NPROCESSORS_ONLN);

    return count > 0 ? count : 0;
}

std::uint64_t SystemInfo::getOpenFileDescriptorCount()
{
    DIR* directory = ::opendir("/proc/self/fd");

    if (directory == nullptr)
    {
        return 0;
    }

    const int directoryFd = ::dirfd(directory);
    std::uint64_t count{0};

    while (const dirent* entry = ::readdir(directory))
    {
        if (entry->d_name[0] == '.')
        {
            continue;
        }

        char* end = nullptr;
        errno = 0;

        const long fd =
            std::strtol(entry->d_name, &end, 10);

        if (errno != 0 ||
            end == entry->d_name ||
            *end != '\0')
        {
            continue;
        }

        if (fd == directoryFd)
        {
            // Do not count the descriptor temporarily opened by
            // opendir() for this inspection itself.
            continue;
        }

        ++count;
    }

    ::closedir(directory);
    return count;
}

double SystemInfo::percentage(std::uint64_t value, std::uint64_t total)
{
    if (total == 0)
    {
        return 0.0;
    }

    return
        static_cast<double>(value) /
        static_cast<double>(total) *
        100.0;
}

std::string SystemInfo::formatGiB(std::uint64_t bytes)
{
    std::ostringstream stream;

    stream
        << std::fixed
        << std::setprecision(2)
        << static_cast<double>(bytes) / kBytesPerGiB
        << "GiB";

    return stream.str();
}

std::string SystemInfo::formatMiB(std::uint64_t bytes)
{
    std::ostringstream stream;

    stream
        << std::fixed
        << std::setprecision(2)
        << static_cast<double>(bytes) / kBytesPerMiB
        << "MiB";

    return stream.str();
}

std::string SystemInfo::formatPercent(double value)
{
    std::ostringstream stream;

    stream
        << std::fixed
        << std::setprecision(2)
        << value
        << "%";

    return stream.str();
}
