#include <iostream>
#include <memory>
#include <string>
#include <sstream>
#include "event_handler.h"
#include "log.h"

EventHandler* EventHandler::eventHandler_ = nullptr;

std::map<std::string, EventHandler::EventFunction> EventHandler::eventMap = 
{
    {   "Evt_AntennaFail"      ,std::bind(&EventHandler::handleAntennaFail, eventHandler_, std::placeholders::_1)   },
    {   "Evt_AntennaPower"     ,std::bind(&EventHandler::handleAntennaPower, eventHandler_, std::placeholders::_1)  }
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
