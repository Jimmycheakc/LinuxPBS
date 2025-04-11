#include <iostream>
#include <sstream>
#include "event_manager.h"
#include "log.h"

EventManager* EventManager::eventManager_ = nullptr;
std::mutex EventManager::mutex_;
const std::string eventLogFileName = "event";

EventManager::EventManager()
    : isEventThreadRunning_(false)
{
    logFileName_ = eventLogFileName;
    Logger::getInstance()->FnCreateLogFile(logFileName_);
}

EventManager* EventManager::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (eventManager_ == nullptr)
    {
        eventManager_ = new EventManager();
    }
    return eventManager_;
}

void EventManager::FnStartEventThread()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EVT");

    if (!isEventThreadRunning_)
    {
        isEventThreadRunning_ = true;
        eventThread_ = std::thread(&EventManager::processEventsFromQueue, this);
    }
}

void EventManager::FnStopEventThread()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EVT");

    if (isEventThreadRunning_)
    {
        isEventThreadRunning_ = false;
        condition_.notify_one();
        eventThread_.join();
    }
}

void EventManager::FnRegisterEvent(const EventSignal::slot_type& subscriber)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EVT");

    eventSignal_.connect(subscriber);
}

template <typename EventType>
void EventManager::FnEnqueueEvent(const std::string& eventName, EventType eventData)
{
    std::stringstream ss;
    ss << __func__ << " Event Name : " << eventName;
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "EVT");

    auto event = std::make_unique<Event<EventType>>(std::move(eventData));

    {
        std::unique_lock<std::mutex> lock(eventThreadMutex_);
        eventQueue.push_back(std::make_pair(eventName, std::move(event)));
    }
    
    condition_.notify_one();
}

void EventManager::processEventsFromQueue()
{
    while (isEventThreadRunning_)
    {
        std::unique_lock<std::mutex> lock(eventThreadMutex_);

        condition_.wait(lock, [this] { return !eventQueue.empty() || !isEventThreadRunning_;});

        while (!eventQueue.empty())
        {
            auto front = std::move(eventQueue.front());
            eventQueue.pop_front();
            lock.unlock();

            try
            {
                processEvent(front.first, front.second.get());
            }
            catch (const std::exception& e)
            {
                std::stringstream ss;
                ss << __func__ << ", Event Name:" << front.first << ", Exception: " << e.what();
                Logger::getInstance()->FnLogExceptionError(ss.str());
            }
            catch (...)
            {
                std::stringstream ss;
                ss << __func__ << ", Event Name:" << front.first << ", Exception: Unknown Exception";
                Logger::getInstance()->FnLogExceptionError(ss.str());
            }


            lock.lock();
        }
    }
}

void EventManager::processEvent(const std::string& eventName, BaseEvent* event)
{
    eventSignal_(eventName, event);
}

// Add other template specializations if needed
template void EventManager::FnEnqueueEvent<int>(const std::string&, int);
template void EventManager::FnEnqueueEvent<bool>(const std::string&, bool);
template void EventManager::FnEnqueueEvent<std::string>(const std::string&, std::string);