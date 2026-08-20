#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include <boost/asio.hpp>
#include <boost/signals2.hpp>

extern const std::string eventLogFileName;

class BaseEvent
{

public:
    virtual ~BaseEvent() = default;
};

template <typename EventType>
class Event final : public BaseEvent
{
public:
    explicit Event(EventType eventData)
        : data(std::move(eventData))
    {
    }

    EventType data;
};

class EventManager
{

public:
    using EventSignal = boost::signals2::signal<void(uint64_t, const std::string&, BaseEvent*)>;

    static EventManager* getInstance();

    void FnStartEventThread();
    void FnStopEventThread();
    void FnRegisterEvent(const EventSignal::slot_type& subscriber);

    template <typename EventType>
    void FnEnqueueEvent(const std::string& eventName, EventType eventData)
    {
        using StoredType = std::decay_t<EventType>;

        auto event = std::make_unique<Event<StoredType>>(std::move(eventData));

        enqueueEvent(eventName, std::move(event));
    }

    EventManager(const EventManager&) = delete;
    EventManager& operator=(const EventManager&) = delete;
    EventManager(EventManager&&) = delete;
    EventManager& operator=(EventManager&&) = delete;

private:
    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
    
    EventManager();
    ~EventManager();

    void enqueueEvent(std::string eventName, std::unique_ptr<BaseEvent> event);

    void processEvent(uint64_t eventId, const std::string& eventName, BaseEvent* event);

    void runEventLoop();
    void shutdownFromDestructor();

    boost::asio::io_context ioContext_;
    std::optional<WorkGuard> workGuard_;
    EventSignal eventSignal_;

    std::atomic<bool> isEventThreadRunning_{false};
    std::atomic<bool> stopRequested_{false};

    // Generate unique ID for every queued event.
    std::atomic<uint64_t> nextEventId_{1};

    std::thread eventThread_;
    std::string logFileName_;
};