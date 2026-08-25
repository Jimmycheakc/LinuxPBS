#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>

// Passive shutdown coordinator.
//
// Responsibilities:
//   - record a shutdown request exactly once
//   - wake the main/lifecycle thread
//
// Non-responsibilities:
//   - does not own an io_context
//   - does not own a thread
//   - does not stop io_context directly
//   - does not close hardware/modules
//   - does not perform blocking work
class ShutdownManager final
{
public:
    static ShutdownManager* getInstance();

    // Thread-safe. May be called from signal handlers dispatched by Asio,
    // UDP/module threads, or other application threads.
    void FnRequestShutdown();

    // Blocks the caller until shutdown has been requested.
    // Intended for the main/lifecycle thread only.
    void FnWaitForShutdown();

    bool FnIsShutdownRequested() const;

    ShutdownManager(const ShutdownManager&) = delete;
    ShutdownManager& operator=(const ShutdownManager&) = delete;
    ShutdownManager(ShutdownManager&&) = delete;
    ShutdownManager& operator=(ShutdownManager&&) = delete;

private:
    ShutdownManager() = default;
    ~ShutdownManager() = default;

    std::atomic<bool> shutdownRequested_{false};

    mutable std::mutex waitMutex_;
    std::condition_variable waitCondition_;
};
