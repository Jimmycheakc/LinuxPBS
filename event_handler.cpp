#include <iostream>
#include <memory>
#include <string>
#include <sstream>
#include "antenna.h"
#include "dio.h"
#include "event_handler.h"
#include "lcsc.h"
#include "log.h"
#include "operation.h"
#include "ksm_reader.h"

EventHandler* EventHandler::eventHandler_ = nullptr;

std::map<std::string, EventHandler::EventFunction> EventHandler::eventMap = 
{
    {   "Evt_AntennaFail"                       ,std::bind(&EventHandler::handleAntennaFail,                eventHandler_, std::placeholders::_1) },
    {   "Evt_AntennaPower"                      ,std::bind(&EventHandler::handleAntennaPower,               eventHandler_, std::placeholders::_1) },
    {   "Evt_AntennaIUCome"                     ,std::bind(&EventHandler::handleAntennaIUCome,              eventHandler_, std::placeholders::_1) },
    {   "Evt_LcscReaderStatus"                  ,std::bind(&EventHandler::handleLcscReaderStatus,           eventHandler_, std::placeholders::_1) },
    {   "Evt_LcscReaderLogin"                   ,std::bind(&EventHandler::handleLcscReaderLogin,            eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderLogout"            ,std::bind(&EventHandler::handleLcscReaderLogout,           eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderGetCardID"         ,std::bind(&EventHandler::handleLcscReaderGetCardID,        eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderGetCardBalance"    ,std::bind(&EventHandler::handleLcscReaderGetCardBalance,   eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderGetTime"           ,std::bind(&EventHandler::handleLcscReaderGetTime,          eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderSetTime"           ,std::bind(&EventHandler::handleLcscReaderSetTime,          eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderUploadCFGFile"     ,std::bind(&EventHandler::handleLcscReaderUploadCFGFile,    eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderUploadCILFile"     ,std::bind(&EventHandler::handleLcscReaderUploadCILFile,    eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderUploadBLFile"      ,std::bind(&EventHandler::handleLcscReaderUploadBLFile,     eventHandler_, std::placeholders::_1) },
    {   "Evt_handleDIOEvent"                    ,std::bind(&EventHandler::handleDIOEvent,                   eventHandler_, std::placeholders::_1) },
    {   "Evt_handleKSMReaderCardIn"             ,std::bind(&EventHandler::handleKSMReaderCardIn,            eventHandler_, std::placeholders::_1) },
    {   "Evt_handleKSMReaderCardOut"            ,std::bind(&EventHandler::handleKSMReaderCardOut,           eventHandler_, std::placeholders::_1) },
    {   "Evt_handleKSMReaderCardTakeAway"       ,std::bind(&EventHandler::handleKSMReaderCardTakeAway,      eventHandler_, std::placeholders::_1) },
    {   "Evt_handleKSMReaderCardInfo"           ,std::bind(&EventHandler::handleKSMReaderCardInfo,          eventHandler_, std::placeholders::_1) }
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
        
        //std::stringstream ss;
        //ss << __func__ << " Successfully, Data : " << value;
        //Logger::getInstance()->FnLog(ss.str());
        operation:: getInstance()->HandlePBSError(AntennaError,value);
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

        //std::stringstream ss;
        //ss << __func__ << " Successfully, Data : " << value;
        //Logger::getInstance()->FnLog(ss.str());
        operation::getInstance()->HandlePBSError(AntennaPowerOnOff,int(boolEvent->data));
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
        operation::getInstance()->VehicleCome(value);
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        ret = false;
    }

    if (operation::getInstance()->tPBSError[iAntenna].ErrNo != 0) operation:: getInstance()->HandlePBSError(AntennaNoError);

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

        operation::getInstance()->ProcessLCSC(value);
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

bool EventHandler::handleLcscReaderGetCardBalance(const BaseEvent* event)
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

bool EventHandler::handleLcscReaderGetTime(const BaseEvent* event)
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

bool EventHandler::handleLcscReaderSetTime(const BaseEvent* event)
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

bool EventHandler::handleLcscReaderUploadCFGFile(const BaseEvent* event)
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

bool EventHandler::handleLcscReaderUploadCILFile(const BaseEvent* event)
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

bool EventHandler::handleLcscReaderUploadBLFile(const BaseEvent* event)
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

bool EventHandler::handleDIOEvent(const BaseEvent* event)
{
    bool ret = true;

    const Event<int>* intEvent = dynamic_cast<const Event<int>*>(event);

    if (intEvent != nullptr)
    {
        DIO::DIO_EVENT dioEvent = static_cast<DIO::DIO_EVENT>(intEvent->data);
        // Temp: Add handling in future

        switch (dioEvent)
        {
            case DIO::DIO_EVENT::LOOP_A_ON_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::LOOP_A_ON_EVENT");
                operation::getInstance()->tProcess.gbLoopApresent = true;
                operation::getInstance()->LoopACome();
                break;
            }
            case DIO::DIO_EVENT::LOOP_A_OFF_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::LOOP_A_OFF_EVENT");
                operation::getInstance()->tProcess.gbLoopApresent = false;
                operation::getInstance()->LoopAGone();
                break;
            }
            case DIO::DIO_EVENT::LOOP_B_ON_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::LOOP_B_ON_EVENT");
                break;
            }
            case DIO::DIO_EVENT::LOOP_B_OFF_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::LOOP_B_OFF_EVENT");
                break;
            }
            case DIO::DIO_EVENT::LOOP_C_ON_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::LOOP_C_ON_EVENT");
                operation::getInstance()->LoopCCome();
                break;
            }
            case DIO::DIO_EVENT::LOOP_C_OFF_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::LOOP_C_OFF_EVENT");
                operation::getInstance()->LoopCGone();
                break;
            }
            case DIO::DIO_EVENT::INTERCOM_ON_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::INTERCOM_ON_EVENT");
                break;
            }
            case DIO::DIO_EVENT::INTERCOM_OFF_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::INTERCOM_OFF_EVENT");
                break;
            }
            case DIO::DIO_EVENT::STATION_DOOR_OPEN_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::STATION_DOOR_OPEN_EVENT");
                operation:: getInstance()->HandlePBSError(SDoorError);
                break;
            }
            case DIO::DIO_EVENT::STATION_DOOR_CLOSE_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::STATION_DOOR_CLOSE_EVENT");
                operation:: getInstance()->HandlePBSError(SDoorNoError);
                break;
            }
            case DIO::DIO_EVENT::BARRIER_DOOR_OPEN_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::BARRIER_DOOR_OPEN_EVENT");
                operation:: getInstance()->HandlePBSError(BDoorError);
                break;
            }
            case DIO::DIO_EVENT::BARRIER_DOOR_CLOSE_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::BARRIER_DOOR_CLOSE_EVENT");
                operation:: getInstance()->HandlePBSError(BDoorNoError);
                break;
            }
            case DIO::DIO_EVENT::BARRIER_STATUS_ON_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::BARRIER_STATUS_ON_EVENT");
                db::getInstance()->AddSysEvent("Barrier up");
                break;
            }
            case DIO::DIO_EVENT::BARRIER_STATUS_OFF_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::BARRIER_STATUS_OFF_EVENT");
                break;
            }
            case DIO::DIO_EVENT::MANUAL_OPEN_BARRIED_ON_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::MANUAL_OPEN_BARRIED_ON_EVENT");

                operation::getInstance()->ManualOpenBarrier();
                break;
            }
            case DIO::DIO_EVENT::MANUAL_OPEN_BARRIED_OFF_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::MANUAL_OPEN_BARRIED_OFF_EVENT");
                break;
            }
            default:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::UNKNOWN_EVENT");
                break;
            }
        }
        std::stringstream ss;
        ss << __func__ << " Successfully, Data : " << static_cast<int>(dioEvent);
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

bool EventHandler::handleKSMReaderCardIn(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Data : " << value;
        Logger::getInstance()->FnLog(ss.str());

        operation::getInstance()->KSM_CardIn();
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

bool EventHandler::handleKSMReaderCardOut(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Data : " << value;
        Logger::getInstance()->FnLog(ss.str());
        operation::getInstance()->writelog("card out", "OPR");
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

bool EventHandler::handleKSMReaderCardTakeAway(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Data : " << value;
        Logger::getInstance()->FnLog(ss.str());

        operation::getInstance()->KSM_CardTakeAway();
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

bool EventHandler::handleKSMReaderCardInfo(const BaseEvent* event)
{
    bool ret = true;
    string sCardNo;
    long glcardbal;
    bool gbcardExpired;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Data : " << value;
        Logger::getInstance()->FnLog(ss.str());

        //---------
        sCardNo = KSM_Reader::getInstance()->FnKSMReaderGetCardNum();
        glcardbal = KSM_Reader::getInstance()->FnKSMReaderGetCardBalance();
        gbcardExpired = KSM_Reader::getInstance()->FnKSMReaderGetCardExpired();
        //--------
        //std::cout << "Card Number : " << KSM_Reader::getInstance()->FnKSMReaderGetCardNum() << std::endl;
        //std::cout << "Card Expiry Date : " << KSM_Reader::getInstance()->FnKSMReaderGetCardExpiryDate() << std::endl;
        //std::cout << "Card Balance : " << KSM_Reader::getInstance()->FnKSMReaderGetCardBalance() << std::endl;
        //std::cout << "Card Expired : " << KSM_Reader::getInstance()->FnKSMReaderGetCardExpired() << std::endl;
        //------
        operation::getInstance()->KSM_CardInfo(sCardNo,glcardbal,gbcardExpired);
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