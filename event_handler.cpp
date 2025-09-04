#include <iostream>
#include <memory>
#include <string>
#include <sstream>
#include "antenna.h"
#include "common.h"
#include "dio.h"
#include "event_handler.h"
#include "lcsc.h"
#include "log.h"
#include "operation.h"
#include "ksm_reader.h"
#include "lpr.h"
#include "upt.h"
#include "eep_client.h"
#include <boost/json.hpp>

EventHandler* EventHandler::eventHandler_ = nullptr;
std::mutex EventHandler::mutex_;

std::map<std::string, EventHandler::EventFunction> EventHandler::eventMap = 
{
    // Antenna Event
    {   "Evt_AntennaFail"                       ,std::bind(&EventHandler::handleAntennaFail                ,eventHandler_, std::placeholders::_1) },
    {   "Evt_AntennaPower"                      ,std::bind(&EventHandler::handleAntennaPower               ,eventHandler_, std::placeholders::_1) },
    {   "Evt_AntennaIUCome"                     ,std::bind(&EventHandler::handleAntennaIUCome              ,eventHandler_, std::placeholders::_1) },

    // LCSC Event
    {   "Evt_LcscReaderStatus"                  ,std::bind(&EventHandler::handleLcscReaderStatus           ,eventHandler_, std::placeholders::_1) },
    {   "Evt_LcscReaderLogin"                   ,std::bind(&EventHandler::handleLcscReaderLogin            ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderLogout"            ,std::bind(&EventHandler::handleLcscReaderLogout           ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderGetCardID"         ,std::bind(&EventHandler::handleLcscReaderGetCardID        ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderGetCardBalance"    ,std::bind(&EventHandler::handleLcscReaderGetCardBalance   ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderGetCardDeduct"     ,std::bind(&EventHandler::handleLcscReaderGetCardDeduct    ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderGetCardRecord"     ,std::bind(&EventHandler::handleLcscReaderGetCardRecord    ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderGetCardFlush"      ,std::bind(&EventHandler::handleLcscReaderGetCardFlush     ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderGetTime"           ,std::bind(&EventHandler::handleLcscReaderGetTime          ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderSetTime"           ,std::bind(&EventHandler::handleLcscReaderSetTime          ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderUploadCFGFile"     ,std::bind(&EventHandler::handleLcscReaderUploadCFGFile    ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderUploadCILFile"     ,std::bind(&EventHandler::handleLcscReaderUploadCILFile    ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleLcscReaderUploadBLFile"      ,std::bind(&EventHandler::handleLcscReaderUploadBLFile     ,eventHandler_, std::placeholders::_1) },

    // DIO Event
    {   "Evt_handleDIOEvent"                    ,std::bind(&EventHandler::handleDIOEvent                   ,eventHandler_, std::placeholders::_1) },

    // KSM Reader Event
    {   "Evt_handleKSMReaderInit"               ,std::bind(&EventHandler::handleKSMReaderInit              ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleKSMReaderGetStatus"          ,std::bind(&EventHandler::handleKSMReaderGetStatus         ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleKSMReaderEjectToFront"       ,std::bind(&EventHandler::handleKSMReaderEjectToFront      ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleKSMReaderCardAllowed"        ,std::bind(&EventHandler::handleKSMReaderCardAllowed       ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleKSMReaderCardProhibited"     ,std::bind(&EventHandler::handleKSMReaderCardProhibited    ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleKSMReaderCardOnIc"           ,std::bind(&EventHandler::handleKSMReaderCardOnIc          ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleKSMReaderIcPowerOn"          ,std::bind(&EventHandler::handleKSMReaderIcPowerOn         ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleKSMReaderWarmReset"          ,std::bind(&EventHandler::handleKSMReaderWarmReset         ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleKSMReaderSelectFile1"        ,std::bind(&EventHandler::handleKSMReaderSelectFile1       ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleKSMReaderSelectFile2"        ,std::bind(&EventHandler::handleKSMReaderSelectFile2       ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleKSMReaderReadCardInfo"       ,std::bind(&EventHandler::handleKSMReaderReadCardInfo      ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleKSMReaderReadCardBalance"    ,std::bind(&EventHandler::handleKSMReaderReadCardBalance   ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleKSMReaderIcPowerOff"         ,std::bind(&EventHandler::handleKSMReaderIcPowerOff        ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleKSMReaderCardIn"             ,std::bind(&EventHandler::handleKSMReaderCardIn            ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleKSMReaderCardOut"            ,std::bind(&EventHandler::handleKSMReaderCardOut           ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleKSMReaderCardTakeAway"       ,std::bind(&EventHandler::handleKSMReaderCardTakeAway      ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleKSMReaderCardInfo"           ,std::bind(&EventHandler::handleKSMReaderCardInfo          ,eventHandler_, std::placeholders::_1) },

    // LPR Event
    {   "Evt_handleLPRReceive"                  ,std::bind(&EventHandler::handleLPRReceive                 ,eventHandler_, std::placeholders::_1) },

    // Upos Terminal Event
    {   "Evt_handleUPTCardDetect"               ,std::bind(&EventHandler::handleUPTCardDetect              ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleUPTPaymentAuto"              ,std::bind(&EventHandler::handleUPTPaymentAuto             ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleUPTDeviceSettlement"         ,std::bind(&EventHandler::handleUPTDeviceSettlement        ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleUPTRetrieveLastSettlement"   ,std::bind(&EventHandler::handleUPTRetrieveLastSettlement  ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleUPTDeviceLogon"              ,std::bind(&EventHandler::handleUPTDeviceLogon             ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleUPTDeviceStatus"             ,std::bind(&EventHandler::handleUPTDeviceStatus            ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleUPTDeviceTimeSync"           ,std::bind(&EventHandler::handleUPTDeviceTimeSync          ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleUPTDeviceTMS"                ,std::bind(&EventHandler::handleUPTDeviceTMS               ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleUPTDeviceReset"              ,std::bind(&EventHandler::handleUPTDeviceReset             ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleUPTCommandCancel"            ,std::bind(&EventHandler::handleUPTCommandCancel           ,eventHandler_, std::placeholders::_1) },

    // Printer Event
    {   "Evt_handlePrinterStatus"               ,std::bind(&EventHandler::handlePrinterStatus              ,eventHandler_, std::placeholders::_1) },

    // Barcode Scanner Event
    {   "Evt_handleBarcodeReceived"             ,std::bind(&EventHandler::handleBarcodeReceived            ,eventHandler_, std::placeholders::_1) },

    // EEP Client Event
    {   "Evt_handleEEPClientResponse"           ,std::bind(&EventHandler::handleEEPClientResponse          ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleEEPClientConnectionState"    ,std::bind(&EventHandler::handleEEPClientConnectionState   ,eventHandler_, std::placeholders::_1) }
};

EventHandler::EventHandler()
{
}

EventHandler* EventHandler::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
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

        bool ret = false;
        try
        {
            ret = handler(event);
        }
        catch (const std::exception& e)
        {
            std::stringstream ss;
            ss << __func__ << ", Exception: " << e.what();
            Logger::getInstance()->FnLogExceptionError(ss.str());
        }
        catch (...)
        {
            std::stringstream ss;
            ss << __func__ << ", Exception: Unknown Exception";
            Logger::getInstance()->FnLogExceptionError(ss.str());
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event not found : " << eventName;
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
    }
}

bool EventHandler::handleAntennaFail(const BaseEvent* event)
{
    bool ret = true;

    const Event<int>* intEvent = dynamic_cast<const Event<int>*>(event);

    if (intEvent != nullptr)
    {
        int value = intEvent->data;
        
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        if (value == 2 && operation::getInstance()->tProcess.gbLoopApresent.load()) 
        { 
            if (operation::getInstance()->tProcess.sEnableReader == false)
           {
                operation::getInstance()->writelog("No IU detected!", "OPR");
                operation::getInstance()->ShowLEDMsg("No IU Detected!^Insert/Tap Card", "No IU Detected!^Insert/Tap Card");
                operation::getInstance()->EnableCashcard(true);
           //     Antenna::getInstance()->FnAntennaStopRead();

            }
        }
        else  
        {
            operation:: getInstance()->HandlePBSError(AntennaError,value);

            if (operation:: getInstance()->tProcess.gbLoopApresent.load() == true)
            {
                Antenna::getInstance()->FnAntennaStopRead();
                operation::getInstance()->writelog("No IU detected! Antenna Error", "OPR");
                operation::getInstance()->ShowLEDMsg("Antenna Error!^Insert/Tap Card", "Antenna Error!^Insert/Tap Card");
                operation::getInstance()->EnableCashcard(true);
            }
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
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

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        operation::getInstance()->HandlePBSError(AntennaPowerOnOff,int(boolEvent->data));
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
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

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        auto sameAsLastIUDuration = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - operation::getInstance()->tProcess.getLastIUEntryTime());

        if ((value.length() == 10) && (operation::getInstance()->tProcess.getLastIUNo() == value)
            && (sameAsLastIUDuration.count() <= operation::getInstance()->tParas.giMaxTransInterval) && (operation::getInstance()->gtStation.iType == tientry))
        {
            std::stringstream ss;
            ss << "Same as last IU, duration :" << sameAsLastIUDuration.count() << " less than Maximum interval: " << operation::getInstance()->tParas.giMaxTransInterval;
            Logger::getInstance()->FnLog(ss.str());
            operation::getInstance()->ShowLEDMsg("Same as last IU^Please Proceed","Same as last IU^Please Proceed");
            operation::getInstance()->Openbarrier();
        }
        else
        {
            operation::getInstance()->VehicleCome(value);
        }

        if (operation::getInstance()->tPBSError[iAntenna].ErrNo != 0)
        {
            operation:: getInstance()->HandlePBSError(AntennaNoError);
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleLcscReaderStatus(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::string value = strEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        operation::getInstance()->ProcessLCSC(value);
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleLcscReaderLogin(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::string value = strEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleLcscReaderLogout(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::string value = strEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleLcscReaderGetCardID(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::string value = strEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        operation::getInstance()->ProcessLCSC(value);
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleLcscReaderGetCardBalance(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::string value = strEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        operation::getInstance()->ProcessLCSC(value);
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleLcscReaderGetCardDeduct(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::string value = strEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        
        operation::getInstance()->ProcessLCSC(value);
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleLcscReaderGetCardRecord(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::string value = strEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        operation::getInstance()->ProcessLCSC(value);
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleLcscReaderGetCardFlush(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::string value = strEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleLcscReaderGetTime(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::string value = strEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleLcscReaderSetTime(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::string value = strEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleLcscReaderUploadCFGFile(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::string value = strEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleLcscReaderUploadCILFile(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::string value = strEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleLcscReaderUploadBLFile(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::string value = strEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
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

        switch (dioEvent)
        {
            case DIO::DIO_EVENT::LOOP_A_ON_EVENT:
            {
              //  Logger::getInstance()->FnLog("DIO::DIO_EVENT::LOOP_A_ON_EVENT");
                operation::getInstance()->tProcess.gbLoopApresent.store(true);
                operation::getInstance()->LoopACome();
                break;
            }
            case DIO::DIO_EVENT::LOOP_A_OFF_EVENT:
            {
             //   Logger::getInstance()->FnLog("DIO::DIO_EVENT::LOOP_A_OFF_EVENT");
                operation::getInstance()->tProcess.gbLoopApresent.store(false);
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
           //     Logger::getInstance()->FnLog("DIO::DIO_EVENT::LOOP_C_ON_EVENT");
                operation::getInstance()->LoopCCome();
                break;
            }
            case DIO::DIO_EVENT::LOOP_C_OFF_EVENT:
            {
            //    Logger::getInstance()->FnLog("DIO::DIO_EVENT::LOOP_C_OFF_EVENT");
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
                operation::getInstance()->HandlePBSError(SDoorError);
                break;
            }
            case DIO::DIO_EVENT::STATION_DOOR_CLOSE_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::STATION_DOOR_CLOSE_EVENT");
                operation::getInstance()->HandlePBSError(SDoorNoError);
                break;
            }
            case DIO::DIO_EVENT::BARRIER_DOOR_OPEN_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::BARRIER_DOOR_OPEN_EVENT");
                operation::getInstance()->HandlePBSError(BDoorError);
                break;
            }
            case DIO::DIO_EVENT::BARRIER_DOOR_CLOSE_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::BARRIER_DOOR_CLOSE_EVENT");
                operation::getInstance()->HandlePBSError(BDoorNoError);
                break;
            }
            case DIO::DIO_EVENT::BARRIER_STATUS_ON_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::BARRIER_STATUS_ON_EVENT");
                if (operation::getInstance()->tProcess.gbBarrierOpened == false)
                {
                    if (DIO::getInstance()->FnGetManualOpenBarrierStatusFlag() == 1)
                    {
                        DIO::getInstance()->FnSetManualOpenBarrierStatusFlag(0);
                    }
                    db::getInstance()->AddSysEvent("Barrier up");
                    //----- add manual open barrier(by operator)
                    operation::getInstance()->ManualOpenBarrier(false);
                }
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

                operation::getInstance()->writelog("Open barrier action(by operator)", "OPR");
                break;
            }
            case DIO::DIO_EVENT::MANUAL_OPEN_BARRIED_OFF_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::MANUAL_OPEN_BARRIED_OFF_EVENT");
                break;
            }
            case DIO::DIO_EVENT::LORRY_SENSOR_ON_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::LORRY_SENSOR_ON_EVENT");
                break;
            }
            case DIO::DIO_EVENT::LORRY_SENSOR_OFF_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::LORRY_SENSOR_OFF_EVENT");
                break;
            }
            case DIO::DIO_EVENT::ARM_BROKEN_ON_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::ARM_BROKEN_ON_EVENT");
                operation::getInstance()->HandlePBSError(BarrierStatus, 3);
                db::getInstance()->AddSysEvent("Arm failure detected.");
                break;
            }
            case DIO::DIO_EVENT::ARM_BROKEN_OFF_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::ARM_BROKEN_OFF_EVENT");
                operation::getInstance()->HandlePBSError(BarrierStatus, 0);
                db::getInstance()->AddSysEvent("Arm recovered successfully.");
                break;
            }
            case DIO::DIO_EVENT::PRINT_RECEIPT_ON_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::PRINT_RECEIPT_ON_EVENT");
                if (operation::getInstance()->tExit.iflag4Receipt == 0 ) operation::getInstance()->tExit.iflag4Receipt = 1;
                operation::getInstance()->PrintReceipt();
                break;
            }
            case DIO::DIO_EVENT::PRINT_RECEIPT_OFF_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::PRINT_RECEIPT_OFF_EVENT");
                break;
            }
            case DIO::DIO_EVENT::BARRIER_OPEN_TOO_LONG_ON_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::BARRIER_OPEN_TOO_LONG_ON_EVENT");
                operation::getInstance()->HandlePBSError(BarrierStatus, 2);
                db::getInstance()->AddSysEvent("Barrier open too long detected.");
                break;
            }
            case DIO::DIO_EVENT::BARRIER_OPEN_TOO_LONG_OFF_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::BARRIER_OPEN_TOO_LONG_OFF_EVENT");
                operation::getInstance()->HandlePBSError(BarrierStatus, 0);
                db::getInstance()->AddSysEvent("Barrier open too long - closed successfully.");
                break;
            }
            default:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::UNKNOWN_EVENT");
                break;
            }
        }
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << static_cast<int>(dioEvent);
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleKSMReaderInit(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        if (value == false)
        {
            Logger::getInstance()->FnLog("KSM Reader Init : Error.", eventLogFileName, "EVT");
            operation::getInstance()->handleKSM_EnableError();
        }
        else
        {
            Logger::getInstance()->FnLog("KSM Reader Init : Ok.", eventLogFileName, "EVT");
            operation::getInstance()->HandlePBSError(ReaderNoError);
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleKSMReaderGetStatus(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        if (value == false)
        {
            Logger::getInstance()->FnLog("KSM Reader Get Status : Error.", eventLogFileName, "EVT");
            operation::getInstance()->handleKSM_EnableError();
        }
        else
        {
            Logger::getInstance()->FnLog("KSM Reader Get Status : Ok.", eventLogFileName, "EVT");
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleKSMReaderEjectToFront(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        if (value == false)
        {
            Logger::getInstance()->FnLog("KSM Reader Eject To Front : Error.", eventLogFileName, "EVT");
            operation::getInstance()->handleKSM_EnableError();
        }
        else
        {
            Logger::getInstance()->FnLog("KSM Reader Eject To Front : Ok.", eventLogFileName, "EVT");
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleKSMReaderCardAllowed(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        if (value == false)
        {
            Logger::getInstance()->FnLog("KSM Reader Card Allowed : Error.", eventLogFileName, "EVT");
            operation::getInstance()->handleKSM_EnableError();
        }
        else
        {
            Logger::getInstance()->FnLog("KSM Reader Card Allowed : Ok.", eventLogFileName, "EVT");
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleKSMReaderCardProhibited(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        if (value == false)
        {
            Logger::getInstance()->FnLog("KSM Reader Card Prohibited : Error.", eventLogFileName, "EVT");
            operation::getInstance()->handleKSM_EnableError();
        }
        else
        {
            Logger::getInstance()->FnLog("KSM Reader Card Prohibited : Ok.", eventLogFileName, "EVT");
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleKSMReaderCardOnIc(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        if (value == false)
        {
            Logger::getInstance()->FnLog("KSM Reader Card On Ic : Error.", eventLogFileName, "EVT");

            operation::getInstance()->handleKSM_CardReadError();
        }
        else
        {
            Logger::getInstance()->FnLog("KSM Reader Card On Ic : Ok.", eventLogFileName, "EVT");
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleKSMReaderIcPowerOn(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        if (value == false)
        {
            Logger::getInstance()->FnLog("KSM Reader Ic Power On : Error.", eventLogFileName, "EVT");

            operation::getInstance()->handleKSM_CardReadError();
        }
        else
        {
            Logger::getInstance()->FnLog("KSM Reader Ic Power On : Ok.", eventLogFileName, "EVT");
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleKSMReaderWarmReset(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        if (value == false)
        {
            Logger::getInstance()->FnLog("KSM Reader Warm Reset : Error.", eventLogFileName, "EVT");

            operation::getInstance()->handleKSM_CardReadError();
        }
        else
        {
            Logger::getInstance()->FnLog("KSM Reader Warm Reset : Ok.", eventLogFileName, "EVT");
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleKSMReaderSelectFile1(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        if (value == false)
        {
            Logger::getInstance()->FnLog("KSM Reader Select File 1 : Error.", eventLogFileName, "EVT");

            operation::getInstance()->handleKSM_CardReadError();
        }
        else
        {
            Logger::getInstance()->FnLog("KSM Reader Select File 1 : Ok.", eventLogFileName, "EVT");
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleKSMReaderSelectFile2(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        if (value == false)
        {
            Logger::getInstance()->FnLog("KSM Reader Select File 2 : Error.", eventLogFileName, "EVT");

            operation::getInstance()->handleKSM_CardReadError();
        }
        else
        {
            Logger::getInstance()->FnLog("KSM Reader Select File 2 : Ok.", eventLogFileName, "EVT");
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleKSMReaderReadCardInfo(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        if (value == false)
        {
            Logger::getInstance()->FnLog("KSM Reader Read Card Info : Error.", eventLogFileName, "EVT");

            operation::getInstance()->handleKSM_CardReadError();
        }
        else
        {
            Logger::getInstance()->FnLog("KSM Reader Read Card Info : Ok.", eventLogFileName, "EVT");
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleKSMReaderReadCardBalance(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        if (value == false)
        {
            Logger::getInstance()->FnLog("KSM Reader Read Card Balance : Error.", eventLogFileName, "EVT");

            operation::getInstance()->handleKSM_CardReadError();
        }
        else
        {
            Logger::getInstance()->FnLog("KSM Reader Read Card Balance : Ok.", eventLogFileName, "EVT");
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleKSMReaderIcPowerOff(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        if (value == false)
        {
            Logger::getInstance()->FnLog("KSM Reader Ic Power Off : Error.", eventLogFileName, "EVT");

            operation::getInstance()->handleKSM_CardReadError();
        }
        else
        {
            Logger::getInstance()->FnLog("KSM Reader Ic Power Off : Ok.", eventLogFileName, "EVT");
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
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
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        operation::getInstance()->KSM_CardIn();
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
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
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        operation::getInstance()->writelog("card out", "OPR");
        KSM_Reader::getInstance()->FnKSMReaderStartGetStatus();
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
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
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        operation::getInstance()->KSM_CardTakeAway();
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
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
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

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
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleLPRReceive(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        struct Lpr::LPREventData eventData = Lpr::getInstance()->deserializeEventData(strEvent->data);
        
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << "camType : " << static_cast<int>(eventData.camType);
        ss << ", LPN : " << eventData.LPN;
        ss << ", TransID : " << eventData.TransID;
        ss << ", imagePath : " << eventData.imagePath;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        Lpr::CType camType = eventData.camType;
        std::string LPN = eventData.LPN;
        std::string TransID = eventData.TransID;
        std::string imagePath = eventData.imagePath;
        operation::getInstance()->ReceivedLPR(camType,LPN, TransID, imagePath);
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }
    
    return ret;
}

bool EventHandler::handleUPTCardDetect(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << strEvent->data;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        operation::getInstance()->processUPT(Upt::UPT_CMD::CARD_DETECT_REQUEST, strEvent->data);
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleUPTPaymentAuto(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << strEvent->data;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        operation::getInstance()->processUPT(Upt::UPT_CMD::PAYMENT_MODE_AUTO_REQUEST, strEvent->data);
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleUPTDeviceSettlement(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << strEvent->data;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        operation::getInstance()->processUPT(Upt::UPT_CMD::DEVICE_SETTLEMENT_REQUEST, strEvent->data);
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleUPTRetrieveLastSettlement(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << strEvent->data;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        operation::getInstance()->processUPT(Upt::UPT_CMD::DEVICE_RETRIEVE_LAST_SETTLEMENT_REQUEST, strEvent->data);
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleUPTDeviceLogon(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << strEvent->data;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        operation::getInstance()->processUPT(Upt::UPT_CMD::DEVICE_LOGON_REQUEST, strEvent->data);
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleUPTDeviceStatus(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << strEvent->data;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        operation::getInstance()->processUPT(Upt::UPT_CMD::DEVICE_STATUS_REQUEST, strEvent->data);
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleUPTDeviceTimeSync(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << strEvent->data;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        operation::getInstance()->processUPT(Upt::UPT_CMD::DEVICE_TIME_SYNC_REQUEST, strEvent->data);
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleUPTDeviceTMS(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << strEvent->data;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        operation::getInstance()->processUPT(Upt::UPT_CMD::DEVICE_TMS_REQUEST, strEvent->data);
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleUPTDeviceReset(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << strEvent->data;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        operation::getInstance()->processUPT(Upt::UPT_CMD::DEVICE_RESET_REQUEST, strEvent->data);
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleUPTCommandCancel(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << strEvent->data;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        operation::getInstance()->processUPT(Upt::UPT_CMD::CANCEL_COMMAND_REQUEST, strEvent->data);
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handlePrinterStatus(const BaseEvent* event)
{
    bool ret = true;

    const Event<int>* intEvent = dynamic_cast<const Event<int>*>(event);

    if (intEvent != nullptr)
    {
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << intEvent->data;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        switch (intEvent->data)
        {
            case 0:
            {
                operation::getInstance()->HandlePBSError(PrinterNoError);
                break;
            }
            case 1:
            {
                operation::getInstance()->HandlePBSError(PrinterNoPaper);
                break;
            }
            case -1:
            {
                operation::getInstance()->HandlePBSError(PrinterError);
                break;
            }
            default:
            {
                operation::getInstance()->HandlePBSError(PrinterError);
                break;
            }
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleBarcodeReceived(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << strEvent->data;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        // process barcode data
        operation::getInstance()->ProcessBarcodeData(strEvent->data);

    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleEEPClientResponse(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << strEvent->data;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        
        // process EEP Client data
        operation::getInstance()->processEEP(strEvent->data);
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleEEPClientConnectionState(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        // process EEP Client Connection State
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}