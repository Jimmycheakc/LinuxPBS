#include <iostream>
#include <memory>
#include <string>
#include <sstream>
#include "event_handler.h"
#include "lcsc.h"
#include "log.h"

EventHandler* EventHandler::eventHandler_ = nullptr;

std::map<std::string, EventHandler::EventFunction> EventHandler::eventMap = 
{
    {   "Evt_AntennaFail"               ,std::bind(&EventHandler::handleAntennaFail,            eventHandler_, std::placeholders::_1) },
    {   "Evt_AntennaPower"              ,std::bind(&EventHandler::handleAntennaPower,           eventHandler_, std::placeholders::_1) },
    {   "Evt_AntennaIUCome"             ,std::bind(&EventHandler::handleAntennaIUCome,          eventHandler_, std::placeholders::_1) },
    {   "Evt_LcscReaderStatus"          ,std::bind(&EventHandler::handleLcscReaderStatus,       eventHandler_, std::placeholders::_1) },
    {   "Evt_LcscReaderLogin"           ,std::bind(&EventHandler::handleLcscReaderLogin,        eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderLogout"    ,std::bind(&EventHandler::handleLcscReaderLogout,       eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderGetCardID" ,std::bind(&EventHandler::handleLcscReaderGetCardID,    eventHandler_, std::placeholders::_1) }
};

EventHandler::EventHandler()
{
}

EventHandler* EventHandler::getInstance()
{
    if (eventHandler_ == nullptr)
    {
        eventHandler_ = new EventHandler();
    }
    return eventHandler_;
}

void EventHandler::FnHandleEvents(const std::string& eventName, const BaseEvent* event)
{
    auto it = eventMap.find(eventName);

    if (it != eventMap.end())
    {
        EventFunction& handler = it->second;

        bool ret = handler(event);

        std::stringstream ss;
        ss << __func__ << " Event handle return : " << ret;
        Logger::getInstance()->FnLog(ss.str()); 
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event not found : " << eventName;
        Logger::getInstance()->FnLog(ss.str());
    }
}

bool EventHandler::handleAntennaFail(const BaseEvent* event)
{
    bool ret = true;

    const Event<int>* intEvent = dynamic_cast<const Event<int>*>(event);

    if (intEvent != nullptr)
    {
        int value = intEvent->data;
        // Temp: Add handling in future
        
        std::stringstream ss;
        ss << __func__ << " Successfully, Data : " << value;
        Logger::getInstance()->FnLog(ss.str());
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        ret = false;
    }

    return ret;
}

bool EventHandler::handleAntennaPower(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;
        // Temp: Add handling in future

        std::stringstream ss;
        ss << __func__ << " Successfully, Data : " << value;
        Logger::getInstance()->FnLog(ss.str());
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        ret = false;
    }

    return ret;
}

bool EventHandler::handleAntennaIUCome(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* boolEvent = dynamic_cast<const Event<std::string>*>(event);

    if (boolEvent != nullptr)
    {
        std::string value = boolEvent->data;
        // Temp: Add handling in future

        std::stringstream ss;
        ss << __func__ << " Successfully, Data : " << value;
        Logger::getInstance()->FnLog(ss.str());
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        ret = false;
    }

    return ret;
}

bool EventHandler::handleLcscReaderStatus(const BaseEvent* event)
{
    bool ret = true;

    const Event<int>* intEvent = dynamic_cast<const Event<int>*>(event);

    if (intEvent != nullptr)
    {
        LCSCReader::mCSCEvents value = static_cast<LCSCReader::mCSCEvents>(intEvent->data);
        // Temp: Add handling in future

        std::stringstream ss;
        ss << __func__ << " Successfully, Data : " << static_cast<int>(value);
        Logger::getInstance()->FnLog(ss.str());
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        ret = false;
    }

    return ret;
}

bool EventHandler::handleLcscReaderLogin(const BaseEvent* event)
{
    bool ret = true;

    const Event<int>* intEvent = dynamic_cast<const Event<int>*>(event);

    if (intEvent != nullptr)
    {
        LCSCReader::mCSCEvents value = static_cast<LCSCReader::mCSCEvents>(intEvent->data);
        // Temp: Add handling in future

        std::stringstream ss;
        ss << __func__ << " Successfully, Data : " << static_cast<int>(value);
        Logger::getInstance()->FnLog(ss.str());
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        ret = false;
    }

    return ret;
}

bool EventHandler::handleLcscReaderLogout(const BaseEvent* event)
{
    bool ret = true;

    const Event<int>* intEvent = dynamic_cast<const Event<int>*>(event);

    if (intEvent != nullptr)
    {
        LCSCReader::mCSCEvents value = static_cast<LCSCReader::mCSCEvents>(intEvent->data);
        // Temp: Add handling in future

        std::stringstream ss;
        ss << __func__ << " Successfully, Data : " << static_cast<int>(value);
        Logger::getInstance()->FnLog(ss.str());
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        ret = false;
    }

    return ret;
}

bool EventHandler::handleLcscReaderGetCardID(const BaseEvent* event)
{
    bool ret = true;

    const Event<int>* intEvent = dynamic_cast<const Event<int>*>(event);

    if (intEvent != nullptr)
    {
        LCSCReader::mCSCEvents value = static_cast<LCSCReader::mCSCEvents>(intEvent->data);
        // Temp: Add handling in future

        std::stringstream ss;
        ss << __func__ << " Successfully, Data : " << static_cast<int>(value);
        Logger::getInstance()->FnLog(ss.str());
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        ret = false;
    }

    return ret;
}
