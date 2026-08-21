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

    const std::uint64_t applicationRuntimeSeconds = getApplicationRuntimeSeconds();

    std::ostringstream logStream;

    constexpr int kLabelWidth = 39;

    // Align continuation lines with the start of the first log message.
    const std::string lineIndent(33, ' ');

    const auto appendInfo =
        [&logStream, &lineIndent](
            const std::string& label,
            const auto& value)
        {
            logStream
                << lineIndent
                << "[*] "
                << std::right
                << std::setw(kLabelWidth)
                << label
                << " = "
                << value
                << '\n';
        };

    logStream << "*** Start display system information ***\n";

    if (identity.valid)
    {
        appendInfo("Hostname", identity.hostname);

        appendInfo("Kernel", identity.kernel);

        appendInfo("Architecture", identity.architecture);
    }

    if (runtime.valid)
    {
        const double uptimeDays =
            static_cast<double>(
                runtime.uptimeSeconds) /
            static_cast<double>(
                kSecondsPerDay);

        appendInfo("System uptime since boot (seconds)", runtime.uptimeSeconds);

        {
            std::ostringstream value;
            value
                << std::fixed
                << std::setprecision(2)
                << uptimeDays;

            appendInfo("System uptime since boot (days)", value.str());
        }
    }

    {
        const double applicationRuntimeDays =
            static_cast<double>(
                applicationRuntimeSeconds) /
            static_cast<double>(
                kSecondsPerDay);

        appendInfo("Application runtime (seconds)", applicationRuntimeSeconds);

        std::ostringstream value;
        value
            << std::fixed
            << std::setprecision(2)
            << applicationRuntimeDays;

        appendInfo("Application runtime (days)", value.str());
    }

    if (runtime.valid)
    {
        appendInfo("Number of processes running", runtime.processCount);
    }

    if (cpuCoreCount > 0)
    {
        appendInfo("CPU cores", cpuCoreCount);
    }

    if (loadAverage.valid)
    {
        std::ostringstream value;

        value
            << std::fixed
            << std::setprecision(2)
            << loadAverage.values[0]
            << " / "
            << loadAverage.values[1]
            << " / "
            << loadAverage.values[2];

        appendInfo("Load average (1m / 5m / 15m)", value.str());
    }

    if (memory.valid)
    {
        appendInfo("Total RAM memory", formatGiB(memory.totalBytes));

        appendInfo("Available RAM memory", formatGiB(memory.availableBytes));

        appendInfo("Used RAM memory", formatPercent(memory.usedPercent));

        appendInfo("Total SWAP", formatGiB(memory.swapTotalBytes));

        appendInfo("Free SWAP", formatGiB(memory.swapFreeBytes));

        appendInfo("Used SWAP", formatPercent(memory.swapUsedPercent));
    }

    if (rootDisk.valid)
    {
        appendInfo("Root disk total", formatGiB(rootDisk.totalBytes));

        appendInfo("Root disk available", formatGiB(rootDisk.availableBytes));

        appendInfo("Root disk used", formatPercent(rootDisk.usedPercent));
    }

    if (process.valid)
    {
        appendInfo("Application name", process.name);

        appendInfo("Application PID", process.pid);

        appendInfo("Application RSS", formatMiB(process.rssBytes));

        appendInfo("Application virtual memory", formatMiB(process.virtualMemoryBytes));

        appendInfo("Application threads", process.threadCount);

        appendInfo("Application open FDs", process.openFileDescriptors);
    }

    logStream << lineIndent << "*** End display system information ***";

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

std::uint64_t
SystemInfo::getApplicationRuntimeSeconds()
{
    std::ifstream file("/proc/self/stat");

    if (!file.is_open())
    {
        return 0;
    }

    std::string line;

    if (!std::getline(file, line))
    {
        return 0;
    }

    // /proc/self/stat field 2 is the process name enclosed
    // in parentheses and can contain spaces, so start parsing
    // after the final ')'.
    const std::size_t closingParenthesis = line.rfind(')');

    if (closingParenthesis == std::string::npos)
    {
        return 0;
    }

    std::istringstream stream(line.substr(closingParenthesis + 2));

    char processState{};

    if (!(stream >> processState))
    {
        return 0;
    }

    // We are currently at field 3 (state).
    // Skip fields 4 through 21.
    std::string ignored;

    for (int field = 4; field <= 21; ++field)
    {
        if (!(stream >> ignored))
        {
            return 0;
        }
    }

    // Field 22:
    // process start time in clock ticks since system boot.
    std::uint64_t startTimeTicks{0};

    if (!(stream >> startTimeTicks))
    {
        return 0;
    }

    const long clockTicksPerSecond = ::sysconf(_SC_CLK_TCK);

    if (clockTicksPerSecond <= 0)
    {
        return 0;
    }

    struct ::sysinfo info{};

    if (::sysinfo(&info) != 0)
    {
        return 0;
    }

    const std::uint64_t processStartSeconds =
        startTimeTicks /
        static_cast<std::uint64_t>(
            clockTicksPerSecond);

    const std::uint64_t systemUptimeSeconds =
        info.uptime > 0
            ? static_cast<std::uint64_t>(
                  info.uptime)
            : 0ULL;

    if (systemUptimeSeconds < processStartSeconds)
    {
        return 0;
    }

    return systemUptimeSeconds - processStartSeconds;
}
