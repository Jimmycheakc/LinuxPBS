#include "ping.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <optional>
#include <poll.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

extern char** environ;

namespace
{

struct ProcessResult
{
    bool launched{false};
    bool timedOut{false};
    int exitCode{-1};
    std::string output;
};

bool isValidAddress(const std::string& address)
{
    if (address.empty())
    {
        return false;
    }

    // We call ping directly without a shell, so shell injection is not possible.
    // Still reject a leading '-' so an address can never be interpreted as a
    // command-line option by the ping executable.
    if (address.front() == '-')
    {
        return false;
    }

    // Embedded NUL cannot be represented safely in exec argv.
    return address.find('\0') == std::string::npos;
}

void appendDiagnostic(std::string& output, const std::string& diagnostic)
{
    if (!output.empty() && output.back() != '\n')
    {
        output.push_back('\n');
    }

    output += diagnostic;
}

bool setNonBlocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0)
    {
        return false;
    }

    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void readAvailableOutput(int fd, std::string& output, bool& pipeClosed)
{
    char buffer[1024];

    while (!pipeClosed)
    {
        const ssize_t bytesRead = ::read(fd, buffer, sizeof(buffer));

        if (bytesRead > 0)
        {
            output.append(buffer, static_cast<std::size_t>(bytesRead));
            continue;
        }

        if (bytesRead == 0)
        {
            pipeClosed = true;
            return;
        }

        if (errno == EINTR)
        {
            continue;
        }

        if (errno == EAGAIN ||
            errno == EWOULDBLOCK)
        {
            return;
        }

        pipeClosed = true;
        return;
    }
}

int decodeWaitStatus(int status)
{
    if (WIFEXITED(status))
    {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status))
    {
        return 128 + WTERMSIG(status);
    }

    return -1;
}

void terminateChild(pid_t pid, int& status)
{
    if (::kill(pid, SIGTERM) != 0 && errno != ESRCH)
    {
        // Best effort only. The process may already have exited.
    }

    constexpr auto kTerminateGrace = std::chrono::milliseconds(100);

    const auto graceDeadline = std::chrono::steady_clock::now() + kTerminateGrace;

    while (std::chrono::steady_clock::now() < graceDeadline)
    {
        const pid_t waitResult = ::waitpid(pid, &status, WNOHANG);

        if (waitResult == pid)
        {
            return;
        }

        if (waitResult < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            if (errno == ECHILD)
            {
                return;
            }

            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (::kill(pid, SIGKILL) != 0 && errno != ESRCH)
    {
        // Best effort only.
    }

    while (::waitpid(pid, &status, 0) < 0)
    {
        if (errno == EINTR)
        {
            continue;
        }

        break;
    }
}

ProcessResult runProcess(const std::vector<std::string>& arguments, std::optional<std::chrono::milliseconds> timeout)
{
    ProcessResult result;

    if (arguments.empty())
    {
        result.output = "Unable to start process: no executable specified";
        return result;
    }

    int pipeFds[2]{-1, -1};

    if (::pipe(pipeFds) != 0)
    {
        result.output = std::string("Unable to create process pipe: ") + std::strerror(errno);
        return result;
    }

    const auto closePipeFds =
        [&pipeFds]()
        {
            if (pipeFds[0] >= 0)
            {
                ::close(pipeFds[0]);
                pipeFds[0] = -1;
            }

            if (pipeFds[1] >= 0)
            {
                ::close(pipeFds[1]);
                pipeFds[1] = -1;
            }
        };

    posix_spawn_file_actions_t actions;

    int spawnError = ::posix_spawn_file_actions_init(&actions);

    if (spawnError != 0)
    {
        result.output = std::string("Unable to initialize process actions: ") + std::strerror(spawnError);
        closePipeFds();
        return result;
    }

    bool actionsInitialized = true;

    auto destroyActions =
        [&actions, &actionsInitialized]()
        {
            if (actionsInitialized)
            {
                ::posix_spawn_file_actions_destroy(&actions);
                actionsInitialized = false;
            }
        };

    auto addAction =
        [&result](int error, const char* actionName)
        {
            if (error == 0)
            {
                return true;
            }

            result.output = std::string("Unable to configure process ") + actionName + ": " + std::strerror(error);

            return false;
        };

    if (!addAction(
            ::posix_spawn_file_actions_adddup2(
                &actions,
                pipeFds[1],
                STDOUT_FILENO),
            "stdout redirect") ||
        !addAction(
            ::posix_spawn_file_actions_adddup2(
                &actions,
                pipeFds[1],
                STDERR_FILENO),
            "stderr redirect") ||
        !addAction(
            ::posix_spawn_file_actions_addclose(
                &actions,
                pipeFds[0]),
            "read-pipe close") ||
        !addAction(
            ::posix_spawn_file_actions_addclose(
                &actions,
                pipeFds[1]),
            "write-pipe close"))
    {
        destroyActions();
        closePipeFds();
        return result;
    }

    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);

    for (const std::string& argument : arguments)
    {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }

    argv.push_back(nullptr);

    pid_t childPid = -1;

    spawnError =
        ::posix_spawnp(
            &childPid,
            arguments.front().c_str(),
            &actions,
            nullptr,
            argv.data(),
            environ);

    destroyActions();

    // Parent does not write to the child's stdout/stderr pipe.
    ::close(pipeFds[1]);
    pipeFds[1] = -1;

    if (spawnError != 0)
    {
        result.output = std::string("Unable to start ping process: ") + std::strerror(spawnError);

        closePipeFds();
        return result;
    }

    result.launched = true;

    if (!setNonBlocking(pipeFds[0]))
    {
        appendDiagnostic(
            result.output,
            std::string("Unable to make ping output pipe non-blocking: ") +
                std::strerror(errno));
    }

    const auto startTime = std::chrono::steady_clock::now();

    const auto deadline =
        timeout.has_value()
            ? std::optional<std::chrono::steady_clock::time_point>(
                  startTime + *timeout)
            : std::nullopt;

    bool childExited = false;
    bool pipeClosed = false;
    int waitStatus = 0;

    while (!childExited || !pipeClosed)
    {
        readAvailableOutput(pipeFds[0], result.output, pipeClosed);

        if (!childExited)
        {
            const pid_t waitResult = ::waitpid(childPid, &waitStatus, WNOHANG);

            if (waitResult == childPid)
            {
                childExited = true;
                result.exitCode = decodeWaitStatus(waitStatus);
            }
            else if (waitResult < 0)
            {
                if (errno != EINTR)
                {
                    childExited = true;

                    appendDiagnostic( result.output, std::string("waitpid failed: ") + std::strerror(errno));
                }
            }
        }

        if (!childExited &&
            deadline.has_value() &&
            std::chrono::steady_clock::now() >=
                *deadline)
        {
            result.timedOut = true;

            terminateChild(childPid, waitStatus);

            childExited = true;
            result.exitCode = decodeWaitStatus(waitStatus);

            appendDiagnostic(result.output, "Ping timed out");
        }

        if (childExited && pipeClosed)
        {
            break;
        }

        int pollTimeoutMs = 50;

        if (!childExited && deadline.has_value())
        {
            const auto now = std::chrono::steady_clock::now();

            if (now < *deadline)
            {
                const auto remaining =
                    std::chrono::duration_cast<
                        std::chrono::milliseconds>(
                            *deadline - now);

                if (remaining.count() < pollTimeoutMs)
                {
                    pollTimeoutMs = static_cast<int>(remaining.count());
                }

                if (pollTimeoutMs < 0)
                {
                    pollTimeoutMs = 0;
                }
            }
            else
            {
                pollTimeoutMs = 0;
            }
        }

        if (!pipeClosed)
        {
            pollfd descriptor{};
            descriptor.fd = pipeFds[0];
            descriptor.events = POLLIN | POLLHUP | POLLERR;

            const int pollResult =
                ::poll(&descriptor, 1, pollTimeoutMs);

            if (pollResult < 0 && errno != EINTR)
            {
                appendDiagnostic(result.output, std::string("poll failed: ") + std::strerror(errno));

                pipeClosed = true;
            }
        }
        else if (!childExited)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    closePipeFds();

    return result;
}

bool pingInternal(
    const std::string& address,
    int attempts,
    std::optional<std::chrono::milliseconds> timeout,
    std::string& details)
{
    details.clear();

    if (!isValidAddress(address))
    {
        details = "Invalid ping address";
        return false;
    }

    if (attempts <= 0)
    {
        details = "Invalid ping attempt count";
        return false;
    }

    const std::vector<std::string> arguments{
        "ping",
        "-c",
        std::to_string(attempts),
        address};

    ProcessResult result = runProcess(arguments, timeout);

    details = std::move(result.output);

    return result.launched &&
           !result.timedOut &&
           result.exitCode == 0;
}

} // namespace

bool Ping(
    const std::string& address,
    const int& max_attempts,
    std::string& details)
{
    // Intentionally synchronous. The calling thread remains blocked until the
    // ping process exits.
    return pingInternal(
        address,
        max_attempts,
        std::nullopt,
        details);
}

bool PingWithTimeOut(
    const std::string& address,
    const float& max_TimeOutInSeconds,
    std::string& details)
{
    // Intentionally synchronous. The timeout only bounds how long this
    // blocking call may wait; it does not create a background timer/thread.
    if (max_TimeOutInSeconds <= 0.0F)
    {
        details = "Invalid ping timeout";
        return false;
    }

    const auto timeout =
        std::chrono::duration_cast<
            std::chrono::milliseconds>(
                std::chrono::duration<float>(
                    max_TimeOutInSeconds));

    if (timeout.count() <= 0)
    {
        details = "Invalid ping timeout";
        return false;
    }

    return pingInternal(address, 1, timeout, details);
}