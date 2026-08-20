#include <sstream>
#include <utility>

#if defined(__linux__)
#include <pthread.h>
#endif

#include "event_manager.h"
#include "log.h"

const std::string eventLogFileName = "event";

EventManager::EventManager()
    : logFileName_(eventLogFileName)
{
    Logger::getInstance()->FnCreateLogFile(logFileName_);
}

EventManager::~EventManager()
{
    shutdownFromDestructor();
}

EventManager* EventManager::getInstance()
{
    static EventManager instance;
    return &instance;
}

void EventManager::FnStartEventThread()
{
    Logger::getInstance()->FnLog("[START] EventManager thread started", logFileName_, "EVT");

    bool expected = false;

    if (!isEventThreadRunning_.compare_exchange_strong(expected, true))
    {
        return;
    }

    // A previous self-stop may have left a finished std::thread object
    // waiting to be joined before the module can be started again.
    if (eventThread_.joinable())
    {
        if (std::this_thread::get_id() == eventThread_.get_id())
        {
            isEventThreadRunning_.store(false);

            Logger::getInstance()->FnLog("Unable to restart EventManager from its own event thread.", logFileName_, "EVT");
            return;
        }

        eventThread_.join();
    }

    stopRequested_.store(false);

    ioContext_.restart();

    workGuard_.emplace(boost::asio::make_work_guard(ioContext_));

    try
    {
        eventThread_ =
            std::thread(
                [this]()
                {
#if defined(__linux__)
                    ::pthread_setname_np(::pthread_self(), "EVT_MANAGER_IO");
#endif
                    runEventLoop();
                });
    }
    catch (...)
    {
        workGuard_.reset();
        isEventThreadRunning_.store(false);
        throw;
    }
}

void EventManager::FnStopEventThread()
{
    Logger::getInstance()->FnLog("[STOP] EventManager thread stopped", logFileName_, "EVT");

    const bool wasRunning = isEventThreadRunning_.exchange(false);

    if (!wasRunning)
    {
        if (eventThread_.joinable() &&
            std::this_thread::get_id() != eventThread_.get_id())
        {
            eventThread_.join();
        }

        return;
    }

    // Stop accepting new events while the current queued work is drained.
    stopRequested_.store(true);

    workGuard_.reset();

    // Never join the EventManager thread from itself.
    if (std::this_thread::get_id() == eventThread_.get_id())
    {
        return;
    }

    if (eventThread_.joinable())
    {
        eventThread_.join();
    }

    // Once fully stopped, allow events to be queued before a later restart,
    // matching the old queue-based behaviour.
    stopRequested_.store(false);
}

void EventManager::FnRegisterEvent(const EventSignal::slot_type& subscriber)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EVT");

    eventSignal_.connect(subscriber);
}

void EventManager::enqueueEvent(std::string eventName, std::unique_ptr<BaseEvent> event)
{
    if (stopRequested_.load())
    {
        std::stringstream ss;
        ss << "[IGNORED] " << eventName << " | EventManager stopping";
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "EVT");
        return;
    }

    const uint64_t eventId = nextEventId_.fetch_add(1);
    Logger::getInstance()->FnLog("[QUEUE] #" + std::to_string(eventId) + " " + eventName, logFileName_, "EVT");

    boost::asio::post(
        ioContext_,
        [this,
         eventId,
         eventName = std::move(eventName),
         event = std::move(event)]() mutable
        {
            try
            {
                processEvent(eventId, eventName, event.get());
            }
            catch (const std::exception& e)
            {
                std::stringstream ss;
                ss << "[ERROR] #" << eventId << " " << eventName << " | " << e.what();
                Logger::getInstance()->FnLogExceptionError(ss.str());
            }
            catch (...)
            {
                std::stringstream ss;
                ss << "[ERROR] #" << eventId << " " << eventName << " | Unknown exception";
                Logger::getInstance()->FnLogExceptionError(ss.str());
            }
        });
}

void EventManager::processEvent(uint64_t eventId, const std::string& eventName, BaseEvent* event)
{
    eventSignal_(eventId, eventName, event);
}


void EventManager::runEventLoop()
{
    Logger::getInstance()->FnLog("[THREAD] Event loop started", logFileName_, "EVT");

    try
    {
        ioContext_.run();
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", EventManager io_context exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        Logger::getInstance()->FnLogExceptionError("EventManager io_context unknown exception.");
    }

    Logger::getInstance()->FnLog("[THREAD] Event loop exited", logFileName_, "EVT");

    isEventThreadRunning_.store(false);
    stopRequested_.store(false);
}

void EventManager::shutdownFromDestructor()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EVT");

    // Destructor is only a final safety fallback. Normal application
    // shutdown should explicitly call FnStopEventThread().
    stopRequested_.store(true);
    isEventThreadRunning_.store(false);

    workGuard_.reset();

    // Do not dispatch queued business events during static destruction.
    // This avoids callbacks into other singleton objects that may already
    // have been destroyed.
    ioContext_.stop();

    if (eventThread_.joinable() &&
        std::this_thread::get_id() != eventThread_.get_id())
    {
        try
        {
            eventThread_.join();
        }
        catch (...)
        {
            // Destructors must not allow exceptions to escape.
        }
    }
}