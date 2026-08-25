#include "mount.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mntent.h>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "log.h"

namespace
{
constexpr int kMaxMountAttempts = 2;
constexpr int kMaxUnmountAttempts = 1;

struct MountRegistryEntry
{
    std::string source;
    std::size_t leaseCount{0};
    bool mountedByProcess{false};
};

struct CommandResult
{
    int exitCode{-1};
    std::string output;
};

struct MountedInfo
{
    std::string source;
    std::string fileSystemType;
};

std::mutex gMountRegistryMutex;
std::unordered_map<std::string, MountRegistryEntry> gMountRegistry;

std::string trimTrailingSlash(std::string value)
{
    while (value.size() > 1 && value.back() == '/')
    {
        value.pop_back();
    }
    return value;
}

std::string normalizeSharePath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return trimTrailingSlash(std::move(path));
}

std::string normalizeMountPoint(const std::string& path)
{
    std::error_code ec;
    const auto normalized = std::filesystem::path(path).lexically_normal();
    (void)ec;
    return trimTrailingSlash(normalized.string());
}

std::string sanitizeOutput(std::string output)
{
    while (!output.empty() &&
           (output.back() == '\n' || output.back() == '\r'))
    {
        output.pop_back();
    }

    // Keep one log record reasonably small if mount.cifs is very verbose.
    constexpr std::size_t kMaxLogOutput = 2048;
    if (output.size() > kMaxLogOutput)
    {
        output.resize(kMaxLogOutput);
        output += "...";
    }

    return output;
}

std::string resolveExecutable(const char* primary,
                              const char* secondary,
                              const char* fallback)
{
    if (::access(primary, X_OK) == 0)
    {
        return primary;
    }

    if (::access(secondary, X_OK) == 0)
    {
        return secondary;
    }

    return fallback;
}

CommandResult runCommand(const std::string& executable,
                         const std::vector<std::string>& arguments)
{
    CommandResult result;

    int pipeFd[2]{};
    if (::pipe(pipeFd) != 0)
    {
        result.output = std::string("pipe() failed: ") + std::strerror(errno);
        return result;
    }

    const pid_t pid = ::fork();
    if (pid < 0)
    {
        result.output = std::string("fork() failed: ") + std::strerror(errno);
        ::close(pipeFd[0]);
        ::close(pipeFd[1]);
        return result;
    }

    if (pid == 0)
    {
        ::close(pipeFd[0]);

        (void)::dup2(pipeFd[1], STDOUT_FILENO);
        (void)::dup2(pipeFd[1], STDERR_FILENO);
        ::close(pipeFd[1]);

        std::vector<char*> argv;
        argv.reserve(arguments.size() + 2);
        argv.push_back(const_cast<char*>(executable.c_str()));
        for (const auto& arg : arguments)
        {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        if (executable.find('/') != std::string::npos)
        {
            ::execv(executable.c_str(), argv.data());
        }
        else
        {
            ::execvp(executable.c_str(), argv.data());
        }

        const std::string error =
            std::string("exec failed: ") + std::strerror(errno) + "\n";

        const char* writePtr = error.data();
        std::size_t bytesRemaining = error.size();

        while (bytesRemaining > 0)
        {
            const ssize_t bytesWritten =
                ::write(STDERR_FILENO, writePtr, bytesRemaining);

            if (bytesWritten > 0)
            {
                writePtr += bytesWritten;

                bytesRemaining -= static_cast<std::size_t>(bytesWritten);

                continue;
            }

            if (bytesWritten < 0 && errno == EINTR)
            {
                continue;
            }

            break;
        }

        _exit(127);
    }

    ::close(pipeFd[1]);

    std::array<char, 512> buffer{};
    for (;;)
    {
        const ssize_t bytesRead = ::read(pipeFd[0], buffer.data(), buffer.size());
        if (bytesRead > 0)
        {
            result.output.append(buffer.data(), static_cast<std::size_t>(bytesRead));
            continue;
        }

        if (bytesRead < 0 && errno == EINTR)
        {
            continue;
        }

        break;
    }

    ::close(pipeFd[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0)
    {
        if (errno == EINTR)
        {
            continue;
        }

        result.output += std::string("; waitpid() failed: ") + std::strerror(errno);
        return result;
    }

    if (WIFEXITED(status))
    {
        result.exitCode = WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status))
    {
        result.exitCode = 128 + WTERMSIG(status);
    }

    result.output = sanitizeOutput(std::move(result.output));
    return result;
}

std::optional<MountedInfo> queryMountPoint(const std::string& mountPoint)
{
    FILE* mounts = ::setmntent("/proc/mounts", "r");
    if (mounts == nullptr)
    {
        return std::nullopt;
    }

    std::optional<MountedInfo> result;
    mntent* entry = nullptr;

    while ((entry = ::getmntent(mounts)) != nullptr)
    {
        if (entry->mnt_dir != nullptr && mountPoint == entry->mnt_dir)
        {
            result = MountedInfo{
                entry->mnt_fsname != nullptr ? entry->mnt_fsname : "",
                entry->mnt_type != nullptr ? entry->mnt_type : ""};
            break;
        }
    }

    ::endmntent(mounts);
    return result;
}

bool mountMatchesRequest(const MountedInfo& info,
                         const std::string& requestedShare)
{
    return normalizeSharePath(info.source) == normalizeSharePath(requestedShare) &&
           info.fileSystemType == "cifs";
}

bool ensureMountDirectory(const std::string& mountPoint,
                          std::string& errorMessage)
{
    std::error_code ec;

    if (std::filesystem::exists(mountPoint, ec))
    {
        if (ec)
        {
            errorMessage = ec.message();
            return false;
        }

        if (!std::filesystem::is_directory(mountPoint, ec))
        {
            errorMessage = ec ? ec.message() : "mount point exists but is not a directory";
            return false;
        }

        return true;
    }

    if (ec)
    {
        errorMessage = ec.message();
        return false;
    }

    if (!std::filesystem::create_directories(mountPoint, ec))
    {
        errorMessage = ec ? ec.message() : "unable to create mount point directory";
        return false;
    }

    return true;
}

class CredentialsFile final
{
public:
    CredentialsFile(const std::string& username,
                    const std::string& password)
    {
        if (username.find_first_of("\r\n") != std::string::npos ||
            password.find_first_of("\r\n") != std::string::npos)
        {
            error_ = "username/password contains an unsupported newline";
            return;
        }

        std::array<char, 64> pattern{};
        const std::string prefix = "/tmp/carpark-cifs-XXXXXX";
        if (prefix.size() + 1 > pattern.size())
        {
            error_ = "temporary credential path is too long";
            return;
        }

        std::copy(prefix.begin(), prefix.end(), pattern.begin());
        pattern[prefix.size()] = '\0';

        const int fd = ::mkstemp(pattern.data());
        if (fd < 0)
        {
            error_ = std::string("mkstemp() failed: ") + std::strerror(errno);
            return;
        }

        path_ = pattern.data();
        (void)::fchmod(fd, S_IRUSR | S_IWUSR);

        const std::string content =
            "username=" + username + "\npassword=" + password + "\n";

        std::size_t totalWritten = 0;
        while (totalWritten < content.size())
        {
            const ssize_t written =
                ::write(fd,
                        content.data() + totalWritten,
                        content.size() - totalWritten);

            if (written > 0)
            {
                totalWritten += static_cast<std::size_t>(written);
                continue;
            }

            if (written < 0 && errno == EINTR)
            {
                continue;
            }

            error_ = std::string("credential write failed: ") + std::strerror(errno);
            break;
        }

        (void)::fsync(fd);
        ::close(fd);

        if (!error_.empty())
        {
            remove();
        }
    }

    ~CredentialsFile()
    {
        remove();
    }

    CredentialsFile(const CredentialsFile&) = delete;
    CredentialsFile& operator=(const CredentialsFile&) = delete;

    bool valid() const
    {
        return !path_.empty() && error_.empty();
    }

    const std::string& path() const
    {
        return path_;
    }

    const std::string& error() const
    {
        return error_;
    }

private:
    void remove()
    {
        if (!path_.empty())
        {
            (void)::unlink(path_.c_str());
            path_.clear();
        }
    }

    std::string path_;
    std::string error_;
};

void logMessage(const std::string& message,
                const std::string& logFileName,
                const std::string& logOption)
{
    Logger::getInstance()->FnLog(message, logFileName, logOption);
}

bool performUnmount(const std::string& mountPoint,
                    const std::string& logFileName,
                    const std::string& logOption)
{
    const std::string umountExecutable =
        resolveExecutable("/bin/umount", "/usr/bin/umount", "umount");

    for (int attempt = 1; attempt <= kMaxUnmountAttempts; ++attempt)
    {
        const CommandResult result =
            runCommand(umountExecutable, {mountPoint});

        if (result.exitCode == 0 && !queryMountPoint(mountPoint).has_value())
        {
            logMessage(
                "MOUNT: [UNMOUNT] Success | MountPoint=" + mountPoint +
                    " | Attempt=" + std::to_string(attempt),
                logFileName,
                logOption);
            return true;
        }

        logMessage(
            "MOUNT: [UNMOUNT] Failed | MountPoint=" + mountPoint +
                " | Attempt=" + std::to_string(attempt) +
                " | ExitCode=" + std::to_string(result.exitCode) +
                (result.output.empty() ? "" : " | Error=" + result.output),
            logFileName,
            logOption);

        if (attempt < kMaxUnmountAttempts)
        {
            std::this_thread::sleep_for(std::chrono::seconds(attempt));
        }
    }

    return false;
}

} // namespace

MountManager::MountManager(const std::string& sharedFolderPath,
                           const std::string& mountPoint,
                           const std::string& username,
                           const std::string& password,
                           const std::string& logFileName,
                           const std::string& logOption,
                           const std::string& extraMountOptions)
    : sharedFolderPath_(normalizeSharePath(sharedFolderPath)),
      mountPoint_(normalizeMountPoint(mountPoint)),
      logFileName_(logFileName),
      logOption_(logOption)
{
    try
    {
        mounted_ = acquireMount(username, password, extraMountOptions);
    }
    catch (const std::exception& e)
    {
        logMessage(
            std::string("MOUNT: [ACQUIRE] Exception | MountPoint=") +
                mountPoint_ + " | Error=" + e.what(),
            logFileName_,
            logOption_);
    }
    catch (...)
    {
        logMessage(
            "MOUNT: [ACQUIRE] Exception | MountPoint=" + mountPoint_ +
                " | Error=Unknown exception",
            logFileName_,
            logOption_);
    }
}

MountManager::~MountManager()
{
    releaseMount();
}

bool MountManager::isMounted() const
{
    return mounted_;
}

bool MountManager::acquireMount(const std::string& username,
                                const std::string& password,
                                const std::string& extraMountOptions)
{
    if (sharedFolderPath_.empty() || mountPoint_.empty())
    {
        logMessage(
            "MOUNT: [ACQUIRE] Failed | Reason=Empty share or mount point",
            logFileName_,
            logOption_);
        return false;
    }

    std::lock_guard<std::mutex> lock(gMountRegistryMutex);

    if (auto registryIt = gMountRegistry.find(mountPoint_);
        registryIt != gMountRegistry.end())
    {
        auto& entry = registryIt->second;
        const auto mountedInfo = queryMountPoint(mountPoint_);

        if (mountedInfo.has_value() &&
            mountMatchesRequest(*mountedInfo, sharedFolderPath_) &&
            normalizeSharePath(entry.source) == sharedFolderPath_)
        {
            ++entry.leaseCount;
            leaseAcquired_ = true;

            logMessage(
                "MOUNT: [ACQUIRE] Reused | Share=" + sharedFolderPath_ +
                    " | MountPoint=" + mountPoint_ +
                    " | Leases=" + std::to_string(entry.leaseCount),
                logFileName_,
                logOption_);
            return true;
        }

        // A stale registry entry can remain after a failed unmount. If nobody
        // currently holds a lease, try to clean it up before mounting again.
        if (entry.leaseCount == 0 && mountedInfo.has_value())
        {
            if (!performUnmount(mountPoint_, logFileName_, logOption_))
            {
                logMessage(
                    "MOUNT: [ACQUIRE] Failed | MountPoint=" + mountPoint_ +
                        " | Reason=Existing mount could not be released",
                    logFileName_,
                    logOption_);
                return false;
            }

            gMountRegistry.erase(registryIt);
        }
        else if (entry.leaseCount != 0)
        {
            logMessage(
                "MOUNT: [ACQUIRE] Failed | MountPoint=" + mountPoint_ +
                    " | Reason=Mount point is already leased for another share",
                logFileName_,
                logOption_);
            return false;
        }
        else
        {
            gMountRegistry.erase(registryIt);
        }
    }

    if (const auto mountedInfo = queryMountPoint(mountPoint_);
        mountedInfo.has_value())
    {
        if (!mountMatchesRequest(*mountedInfo, sharedFolderPath_))
        {
            logMessage(
                "MOUNT: [ACQUIRE] Failed | MountPoint=" + mountPoint_ +
                    " | Reason=Already mounted to a different source" +
                    " | ExistingSource=" + mountedInfo->source +
                    " | RequestedSource=" + sharedFolderPath_,
                logFileName_,
                logOption_);
            return false;
        }

        gMountRegistry.emplace(
            mountPoint_,
            MountRegistryEntry{sharedFolderPath_, 1, false});

        leaseAcquired_ = true;

        logMessage(
            "MOUNT: [ACQUIRE] Existing mount reused | Share=" +
                sharedFolderPath_ + " | MountPoint=" + mountPoint_,
            logFileName_,
            logOption_);
        return true;
    }

    std::string directoryError;
    if (!ensureMountDirectory(mountPoint_, directoryError))
    {
        logMessage(
            "MOUNT: [ACQUIRE] Failed | MountPoint=" + mountPoint_ +
                " | Reason=Cannot prepare mount directory | Error=" +
                directoryError,
            logFileName_,
            logOption_);
        return false;
    }

    CredentialsFile credentials(username, password);
    if (!credentials.valid())
    {
        logMessage(
            "MOUNT: [ACQUIRE] Failed | MountPoint=" + mountPoint_ +
                " | Reason=Cannot create credentials file | Error=" +
                credentials.error(),
            logFileName_,
            logOption_);
        return false;
    }

    const std::string mountExecutable =
        resolveExecutable("/bin/mount", "/usr/bin/mount", "mount");

    std::string options = "credentials=" + credentials.path();
    if (!extraMountOptions.empty())
    {
        options += "," + extraMountOptions;
    }

    for (int attempt = 1; attempt <= kMaxMountAttempts; ++attempt)
    {
        logMessage(
            "MOUNT: [ACQUIRE] Attempt | Share=" + sharedFolderPath_ +
                " | MountPoint=" + mountPoint_ +
                " | Attempt=" + std::to_string(attempt),
            logFileName_,
            logOption_);

        const CommandResult result =
            runCommand(
                mountExecutable,
                {"-t", "cifs", sharedFolderPath_, mountPoint_, "-o", options});

        const auto mountedInfo = queryMountPoint(mountPoint_);

        if (result.exitCode == 0 &&
            mountedInfo.has_value() &&
            mountMatchesRequest(*mountedInfo, sharedFolderPath_))
        {
            gMountRegistry.emplace(
                mountPoint_,
                MountRegistryEntry{sharedFolderPath_, 1, true});

            leaseAcquired_ = true;

            logMessage(
                "MOUNT: [ACQUIRE] Success | Share=" + sharedFolderPath_ +
                    " | MountPoint=" + mountPoint_ +
                    " | Attempt=" + std::to_string(attempt),
                logFileName_,
                logOption_);
            return true;
        }

        logMessage(
            "MOUNT: [ACQUIRE] Failed | Share=" + sharedFolderPath_ +
                " | MountPoint=" + mountPoint_ +
                " | Attempt=" + std::to_string(attempt) +
                " | ExitCode=" + std::to_string(result.exitCode) +
                (result.output.empty() ? "" : " | Error=" + result.output),
            logFileName_,
            logOption_);

        // A failed mount can occasionally leave a partial/stale mount behind.
        // Only unmount it if it matches the share we attempted to mount.
        if (mountedInfo.has_value() &&
            mountMatchesRequest(*mountedInfo, sharedFolderPath_))
        {
            (void)performUnmount(mountPoint_, logFileName_, logOption_);
        }

        if (attempt < kMaxMountAttempts)
        {
            std::this_thread::sleep_for(std::chrono::seconds(attempt));
        }
    }

    return false;
}

void MountManager::releaseMount()
{
    if (!leaseAcquired_)
    {
        return;
    }

    try
    {
        std::lock_guard<std::mutex> lock(gMountRegistryMutex);

        const auto registryIt = gMountRegistry.find(mountPoint_);
        if (registryIt == gMountRegistry.end())
        {
            leaseAcquired_ = false;
            mounted_ = false;
            return;
        }

        auto& entry = registryIt->second;
        if (entry.leaseCount > 0)
        {
            --entry.leaseCount;
        }

        logMessage(
            "MOUNT: [RELEASE] Lease released | MountPoint=" + mountPoint_ +
                " | RemainingLeases=" + std::to_string(entry.leaseCount),
            logFileName_,
            logOption_);

        if (entry.leaseCount == 0)
        {
            if (!entry.mountedByProcess)
            {
                // The mount existed before this process acquired it, therefore
                // this process must not unmount somebody else's mount.
                gMountRegistry.erase(registryIt);
            }
            else if (performUnmount(mountPoint_, logFileName_, logOption_))
            {
                gMountRegistry.erase(registryIt);
            }
            // On unmount failure keep the zero-lease entry. A future acquire
            // can reuse it and the next final release will retry unmounting.
        }
    }
    catch (...)
    {
        // Destructors must never throw.
    }

    leaseAcquired_ = false;
    mounted_ = false;
}
