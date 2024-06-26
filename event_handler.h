#pragma once

#include <iostream>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include "event_manager.h"

class EventHandler
{

public:
    typedef std::function<bool(const BaseEvent*)> EventFunction;

    static std::map<std::string, EventHandler::EventFunction> eventMap;

    void FnHandleEvents(const std::string& eventName, const BaseEvent* event);
    static EventHandler* getInstance();

    /**
     * Singleton EventHandler should not be cloneable.
     */
    EventHandler(EventHandler& eventHandler) = delete;

    /**
     * Singleton EventHandler should not be assignable.
     */
    void operator=(const EventHandler&) = delete;

private:
    static EventHandler* eventHandler_;
    static std::mutex mutex_;
    EventHandler();
    bool handleAntennaFail(const BaseEvent* event);
    bool handleAntennaPower(const BaseEvent* event);
    bool handleAntennaIUCome(const BaseEvent* event);
    bool handleLcscReaderStatus(const BaseEvent* event);
    bool handleLcscReaderLogin(const BaseEvent* event);
    bool handleLcscReaderLogout(const BaseEvent* event);
    bool handleLcscReaderGetCardID(const BaseEvent* event);
    bool handleLcscReaderGetCardBalance(const BaseEvent* event);
    bool handleLcscReaderGetTime(const BaseEvent* event);
    bool handleLcscReaderSetTime(const BaseEvent* event);
    bool handleLcscReaderUploadCFGFile(const BaseEvent* event);
    bool handleLcscReaderUploadCILFile(const BaseEvent* event);
    bool handleLcscReaderUploadBLFile(const BaseEvent* event);
    bool handleDIOEvent(const BaseEvent* event);
    bool handleKSMReaderCardIn(const BaseEvent* event);
    bool handleKSMReaderCardOut(const BaseEvent* event);
    bool handleKSMReaderCardTakeAway(const BaseEvent* event);
    bool handleKSMReaderCardInfo(const BaseEvent* event);
    bool handleLPRReceive(const BaseEvent* event);
};