#pragma once

#include <cstdint>
#include <string>

#include "event_manager.h"


// Passive EventManager -> Operation adapter.
//
// EventHandler does not contain station/device business logic. It only:
//   1. identifies the event route,
//   2. validates/copies the BaseEvent payload while it is still alive, and
//   3. forwards an owning OperationEvent to operation::FnOnEvent().
//
// All event-specific decisions and business handling run inside Operation on
// the OP_IO thread.
class EventHandler
{
public:
    static EventHandler* getInstance();

    void FnHandleEvents(
        uint64_t eventId,
        const std::string& eventName,
        const BaseEvent* event);

    EventHandler(const EventHandler&) = delete;
    EventHandler& operator=(const EventHandler&) = delete;
    EventHandler(EventHandler&&) = delete;
    EventHandler& operator=(EventHandler&&) = delete;

private:
    EventHandler() = default;
    ~EventHandler() = default;
};
