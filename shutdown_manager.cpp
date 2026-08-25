#include "shutdown_manager.h"

ShutdownManager* ShutdownManager::getInstance()
{
    static ShutdownManager instance;
    return &instance;
}

void ShutdownManager::FnRequestShutdown()
{
    const bool alreadyRequested = shutdownRequested_.exchange(true);

    if (alreadyRequested)
    {
        return;
    }

    waitCondition_.notify_all();
}

void ShutdownManager::FnWaitForShutdown()
{
    std::unique_lock<std::mutex> lock(waitMutex_);

    waitCondition_.wait(
        lock,
        [this]()
        {
            return shutdownRequested_.load();
        });
}

bool ShutdownManager::FnIsShutdownRequested() const
{
    return shutdownRequested_.load();
}
