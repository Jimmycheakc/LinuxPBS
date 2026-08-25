#include "event_handler.h"

#include <map>
#include <sstream>
#include <string>
#include <utility>

#include "log.h"
#include "operation.h"


namespace
{

enum class EventPayloadType
{
    Int,
    Bool,
    String
};

struct EventRoute
{
    OperationEventType operationType;
    EventPayloadType payloadType;
};

// EventHandler contains routing/type metadata only. There is intentionally no
// station/device business decision in this table.
const std::map<std::string, EventRoute> eventRoutes =
{
    // Antenna Event
    { "Evt_AntennaFail",                      { OperationEventType::AntennaFail,                    EventPayloadType::Int    } },
    { "Evt_AntennaPower",                     { OperationEventType::AntennaPower,                   EventPayloadType::Bool   } },
    { "Evt_AntennaIUCome",                    { OperationEventType::AntennaIUCome,                  EventPayloadType::String } },

    // LCSC Event
    { "Evt_LcscReaderStatus",                 { OperationEventType::LcscReaderStatus,               EventPayloadType::String } },
    { "Evt_LcscReaderLogin",                  { OperationEventType::LcscReaderLogin,                EventPayloadType::String } },
    { "Evt_handleLcscReaderLogout",           { OperationEventType::LcscReaderLogout,               EventPayloadType::String } },
    { "Evt_handleLcscReaderGetCardID",        { OperationEventType::LcscReaderGetCardID,            EventPayloadType::String } },
    { "Evt_handleLcscReaderGetCardBalance",   { OperationEventType::LcscReaderGetCardBalance,       EventPayloadType::String } },
    { "Evt_handleLcscReaderGetCardDeduct",    { OperationEventType::LcscReaderGetCardDeduct,        EventPayloadType::String } },
    { "Evt_handleLcscReaderGetCardRecord",    { OperationEventType::LcscReaderGetCardRecord,        EventPayloadType::String } },
    { "Evt_handleLcscReaderGetCardFlush",     { OperationEventType::LcscReaderGetCardFlush,         EventPayloadType::String } },
    { "Evt_handleLcscReaderGetTime",          { OperationEventType::LcscReaderGetTime,              EventPayloadType::String } },
    { "Evt_handleLcscReaderSetTime",          { OperationEventType::LcscReaderSetTime,              EventPayloadType::String } },
    { "Evt_handleLcscReaderUploadCFGFile",    { OperationEventType::LcscReaderUploadCFGFile,        EventPayloadType::String } },
    { "Evt_handleLcscReaderUploadCILFile",    { OperationEventType::LcscReaderUploadCILFile,        EventPayloadType::String } },
    { "Evt_handleLcscReaderUploadBLFile",     { OperationEventType::LcscReaderUploadBLFile,         EventPayloadType::String } },

    // DIO Event
    { "Evt_handleDIOEvent",                   { OperationEventType::DioEvent,                       EventPayloadType::Int    } },

    // KSM Reader Event
    { "Evt_handleKSMReaderInit",              { OperationEventType::KsmReaderInit,                  EventPayloadType::Bool   } },
    { "Evt_handleKSMReaderGetStatus",         { OperationEventType::KsmReaderGetStatus,             EventPayloadType::Bool   } },
    { "Evt_handleKSMReaderEjectToFront",      { OperationEventType::KsmReaderEjectToFront,          EventPayloadType::Bool   } },
    { "Evt_handleKSMReaderCardAllowed",       { OperationEventType::KsmReaderCardAllowed,           EventPayloadType::Bool   } },
    { "Evt_handleKSMReaderCardProhibited",    { OperationEventType::KsmReaderCardProhibited,        EventPayloadType::Bool   } },
    { "Evt_handleKSMReaderCardOnIc",          { OperationEventType::KsmReaderCardOnIc,              EventPayloadType::Bool   } },
    { "Evt_handleKSMReaderIcPowerOn",         { OperationEventType::KsmReaderIcPowerOn,             EventPayloadType::Bool   } },
    { "Evt_handleKSMReaderWarmReset",         { OperationEventType::KsmReaderWarmReset,             EventPayloadType::Bool   } },
    { "Evt_handleKSMReaderSelectFile1",       { OperationEventType::KsmReaderSelectFile1,           EventPayloadType::Bool   } },
    { "Evt_handleKSMReaderSelectFile2",       { OperationEventType::KsmReaderSelectFile2,           EventPayloadType::Bool   } },
    { "Evt_handleKSMReaderReadCardInfo",      { OperationEventType::KsmReaderReadCardInfo,          EventPayloadType::Bool   } },
    { "Evt_handleKSMReaderReadCardBalance",   { OperationEventType::KsmReaderReadCardBalance,       EventPayloadType::Bool   } },
    { "Evt_handleKSMReaderIcPowerOff",        { OperationEventType::KsmReaderIcPowerOff,            EventPayloadType::Bool   } },
    { "Evt_handleKSMReaderCardIn",            { OperationEventType::KsmReaderCardIn,                EventPayloadType::Bool   } },
    { "Evt_handleKSMReaderCardOut",           { OperationEventType::KsmReaderCardOut,               EventPayloadType::Bool   } },
    { "Evt_handleKSMReaderCardTakeAway",      { OperationEventType::KsmReaderCardTakeAway,          EventPayloadType::Bool   } },
    { "Evt_handleKSMReaderCardInfo",          { OperationEventType::KsmReaderCardInfo,              EventPayloadType::Bool   } },

    // LPR Event - keep the serialized string here; Operation deserializes it.
    { "Evt_handleLPRReceive",                 { OperationEventType::LprReceive,                     EventPayloadType::String } },

    // Upos Terminal Event - Operation maps the semantic event to the UPT cmd.
    { "Evt_handleUPTCardDetect",              { OperationEventType::UptCardDetect,                  EventPayloadType::String } },
    { "Evt_handleUPTPaymentAuto",             { OperationEventType::UptPaymentAuto,                 EventPayloadType::String } },
    { "Evt_handleUPTDeviceSettlement",        { OperationEventType::UptDeviceSettlement,            EventPayloadType::String } },
    { "Evt_handleUPTRetrieveLastSettlement",  { OperationEventType::UptRetrieveLastSettlement,      EventPayloadType::String } },
    { "Evt_handleUPTDeviceLogon",             { OperationEventType::UptDeviceLogon,                 EventPayloadType::String } },
    { "Evt_handleUPTDeviceStatus",            { OperationEventType::UptDeviceStatus,                EventPayloadType::String } },
    { "Evt_handleUPTDeviceTimeSync",          { OperationEventType::UptDeviceTimeSync,              EventPayloadType::String } },
    { "Evt_handleUPTDeviceTMS",               { OperationEventType::UptDeviceTMS,                   EventPayloadType::String } },
    { "Evt_handleUPTDeviceReset",             { OperationEventType::UptDeviceReset,                 EventPayloadType::String } },
    { "Evt_handleUPTCommandCancel",           { OperationEventType::UptCommandCancel,               EventPayloadType::String } },

    // Printer / Barcode
    { "Evt_handlePrinterStatus",              { OperationEventType::PrinterStatus,                  EventPayloadType::Int    } },
    { "Evt_handleBarcodeReceived",            { OperationEventType::BarcodeReceived,                EventPayloadType::String } },

    // EEP Client Event
    { "Evt_handleEEPClientResponse",          { OperationEventType::EepClientResponse,              EventPayloadType::String } },
    { "Evt_handleEEPClientConnectionState",   { OperationEventType::EepClientConnectionState,       EventPayloadType::Bool   } },

    // CHU Client Event
    { "Evt_handleCHUReceived",                { OperationEventType::ChuReceived,                     EventPayloadType::String } },
    { "Evt_handleCHUClientConnectionState",   { OperationEventType::ChuClientConnectionState,        EventPayloadType::String } }
};

void logInvalidEvent(const std::string& eventName)
{
    std::stringstream ss;
    ss << "[FORWARD] " << eventName << " | Invalid event data type";
    Logger::getInstance()->FnLog(ss.str());
    Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
}

template <typename T>
bool forwardTypedEvent(
    const BaseEvent* event,
    const std::string& eventName,
    OperationEventType operationType)
{
    const auto* typedEvent = dynamic_cast<const Event<T>*>(event);

    if (typedEvent == nullptr)
    {
        logInvalidEvent(eventName);
        return false;
    }

    // The BaseEvent belongs to EventManager and may be destroyed after this
    // callback returns. Copy the payload into an owning OperationEvent before
    // Operation posts it asynchronously to OP_IO.
    T payload = typedEvent->data;

    std::stringstream ss;
    ss << "[FORWARD] " << eventName << " | " << payload;
    Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

    return operation::getInstance()->FnOnEvent(
        OperationEvent{operationType, std::move(payload)});
}

bool forwardEvent(
    const EventRoute& route,
    const BaseEvent* event,
    const std::string& eventName)
{
    switch (route.payloadType)
    {
        case EventPayloadType::Int:
            return forwardTypedEvent<int>(event, eventName, route.operationType);

        case EventPayloadType::Bool:
            return forwardTypedEvent<bool>(event, eventName, route.operationType);

        case EventPayloadType::String:
            return forwardTypedEvent<std::string>(event, eventName, route.operationType);
    }

    return false;
}

} // namespace


EventHandler* EventHandler::getInstance()
{
    static EventHandler instance;
    return &instance;
}

void EventHandler::FnHandleEvents(
    uint64_t eventId,
    const std::string& eventName,
    const BaseEvent* event)
{
    const auto it = eventRoutes.find(eventName);

    if (it == eventRoutes.end())
    {
        std::stringstream ss;
        ss << "[UNKNOWN] #" << eventId << " " << eventName;
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        return;
    }

    try
    {
        const bool success = forwardEvent(it->second, event, eventName);

        std::stringstream ss;
        ss << "[DONE] #" << eventId << " " << eventName
           << " | " << (success ? "OK" : "FAILED");
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
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
}
