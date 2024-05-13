#pragma once

#include <iostream>
#include <string>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <typeinfo>
#include "boost/signals2.hpp"

extern const std::string eventLogFileName;

class BaseEvent
{

public:
    virtual ~BaseEvent() = default;
};

template <typename EventType>
class Event : public BaseEvent
{

public:
    EventType data;

    Event(EventType eventData) : data(std::move(eventData)) {};
};

class EventManager
{

public:
    using EventSignal = boost::signals2::signal<void(const std::string&, BaseEvent*)>;

    void FnStartEventThread();
    void FnStopEventThread();
    void FnRegisterEvent(const EventSignal::slot_type& subscriber);

    template <typename EventType>
    void FnEnqueueEvent(const std::string& eventName, EventType eventData);

    static EventManager* getInstance();

    /**
     * Singleton EventManager should not be cloneable.
     */
    EventManager(EventManager& eventManager) = delete;

    /**
     * Singleton EventManager should not be assignable.
     */
    void operator=(const EventManager&) = delete;

private:
    static EventManager* eventManager_;
    static std::mutex mutex_;
    EventSignal eventSignal_;
    std::deque<std::pair<std::string, std::unique_ptr<BaseEvent>>> eventQueue;
    std::mutex eventThreadMutex_;
    std::condition_variable condition_;
    bool isEventThreadRunning_;
    std::thread eventThread_;
    std::string logFileName_;
    EventManager();
    void processEventsFromQueue();
    void processEvent(const std::string& eventName, BaseEvent* event);
};