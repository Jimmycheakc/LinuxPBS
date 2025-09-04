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
    // Antenna Event Handler
    bool handleAntennaFail(const BaseEvent* event);
    bool handleAntennaPower(const BaseEvent* event);
    bool handleAntennaIUCome(const BaseEvent* event);

    // LCSC Event Handler
    bool handleLcscReaderStatus(const BaseEvent* event);
    bool handleLcscReaderLogin(const BaseEvent* event);
    bool handleLcscReaderLogout(const BaseEvent* event);
    bool handleLcscReaderGetCardID(const BaseEvent* event);
    bool handleLcscReaderGetCardBalance(const BaseEvent* event);
    bool handleLcscReaderGetCardDeduct(const BaseEvent* event);
    bool handleLcscReaderGetCardRecord(const BaseEvent* event);
    bool handleLcscReaderGetCardFlush(const BaseEvent* event);
    bool handleLcscReaderGetTime(const BaseEvent* event);
    bool handleLcscReaderSetTime(const BaseEvent* event);
    bool handleLcscReaderUploadCFGFile(const BaseEvent* event);
    bool handleLcscReaderUploadCILFile(const BaseEvent* event);
    bool handleLcscReaderUploadBLFile(const BaseEvent* event);

    // DIO Event Handler
    bool handleDIOEvent(const BaseEvent* event);

    // KSM Reader Event Handler
    bool handleKSMReaderInit(const BaseEvent* event);
    bool handleKSMReaderGetStatus(const BaseEvent* event);
    bool handleKSMReaderEjectToFront(const BaseEvent* event);
    bool handleKSMReaderCardAllowed(const BaseEvent* event);
    bool handleKSMReaderCardProhibited(const BaseEvent* event);
    bool handleKSMReaderCardOnIc(const BaseEvent* event);
    bool handleKSMReaderIcPowerOn(const BaseEvent* event);
    bool handleKSMReaderWarmReset(const BaseEvent* event);
    bool handleKSMReaderSelectFile1(const BaseEvent* event);
    bool handleKSMReaderSelectFile2(const BaseEvent* event);
    bool handleKSMReaderReadCardInfo(const BaseEvent* event);
    bool handleKSMReaderReadCardBalance(const BaseEvent* event);
    bool handleKSMReaderIcPowerOff(const BaseEvent* event);
    bool handleKSMReaderCardIn(const BaseEvent* event);
    bool handleKSMReaderCardOut(const BaseEvent* event);
    bool handleKSMReaderCardTakeAway(const BaseEvent* event);
    bool handleKSMReaderCardInfo(const BaseEvent* event);

    // LPR Event Handler
    bool handleLPRReceive(const BaseEvent* event);

    // Upos Terminal Event Handler
    bool handleUPTCardDetect(const BaseEvent* event);
    bool handleUPTPaymentAuto(const BaseEvent* event);
    bool handleUPTDeviceSettlement(const BaseEvent* event);
    bool handleUPTRetrieveLastSettlement(const BaseEvent* event);
    bool handleUPTDeviceLogon(const BaseEvent* event);
    bool handleUPTDeviceStatus(const BaseEvent* event);
    bool handleUPTDeviceTimeSync(const BaseEvent* event);
    bool handleUPTDeviceTMS(const BaseEvent* event);
    bool handleUPTDeviceReset(const BaseEvent* event);
    bool handleUPTCommandCancel(const BaseEvent* event);

    // Printer Event Handler
    bool handlePrinterStatus(const BaseEvent* event);

    // Barcode Scanner Event Handler
    bool handleBarcodeReceived(const BaseEvent* event);

    // EEP Client Event Handler
    bool handleEEPClientResponse(const BaseEvent* event);
    bool handleEEPClientConnectionState(const BaseEvent* event);
};