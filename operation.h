#pragma once

#include <stdio.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <variant>
#include <iostream>
#include "lcsc.h"
#include "structuredata.h"
#include "db.h"
#include "udp.h"
#include "lpr.h"
#include "upt.h"
#include "eep_client.h"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

typedef enum : unsigned int
{
    FreeParking        = 0,
    SeasonParking      = 1,
    GracePeriod        = 2,
    DeductionOK        = 3,
    Complimentary      = 4,
    AXSParking         = 5,
    EZPayParking       = 6,
    DeductionFail      = 7,
    Manualopen         = 8,
    BalanceChange      = 9,
    RejectCode7        = 10,
    UpdateCHUTrans     = 11,
   
} TransType;

typedef enum : unsigned int
{
    // device type : 0 = PMS(EntryTime), 1 = Ant, 2 = CHU, 3 = EEP, 4 = LCSC, 5 = UPOS
    PMSEntrytime        = 0,
    Ant                 = 1,
    CHU                 = 2,
    EEP                 = 3,
    LCSC                = 4,
    UPOS                = 5
} DeviceType;

// =========================================================
// Thread-safe Operation shared-state DTOs.
//
// OperationSharedData is a snapshot copy of Operation's private OP_IO-owned
// business state. External modules never receive references/pointers to the
// real state.
//
// OperationSharedDataUpdate is a structure-level update request. Only
// structures whose std::optional contains a value are replaced on OP_IO.
// Unspecified structures are left exactly as they are.
// =========================================================
struct OperationSharedData
{
    bool isOperationInitialized{false};

    tstation_struct gtStation{};
    tEntryTrans_Struct tEntry{};
    tExitTrans_Struct tExit{};
    tExitTrans_Struct tExit1{};
    tProcess_Struct tProcess{};
    tParas_Struct tParas{};
    tMsg_Struct tMsg{};
    tExitMsg_struct tExitMsg{};
    std::array<tPBSError_struct, 20> tPBSError{};
    tseason_struct tSeason{};
    std::vector<tVType_Struct> tVType;
    std::vector<tTR_struc> tTR;
};

struct OperationSharedDataUpdate
{
    std::optional<tstation_struct> gtStation;
    std::optional<tEntryTrans_Struct> tEntry;
    std::optional<tExitTrans_Struct> tExit;
    std::optional<tExitTrans_Struct> tExit1;
    std::optional<tProcess_Struct> tProcess;
    std::optional<tParas_Struct> tParas;
    std::optional<tMsg_Struct> tMsg;
    std::optional<tExitMsg_struct> tExitMsg;
    std::optional<std::array<tPBSError_struct, 20>> tPBSError;
    std::optional<tseason_struct> tSeason;
    std::optional<std::vector<tVType_Struct>> tVType;
    std::optional<std::vector<tTR_struc>> tTR;
};

// =========================================================
// EventHandler -> Operation event boundary.
//
// EventHandler converts the short-lived BaseEvent payload into this owning
// envelope before crossing the asynchronous OP_IO boundary.
// =========================================================
enum class OperationEventType
{
    // Antenna
    AntennaFail,
    AntennaPower,
    AntennaIUCome,

    // LCSC
    LcscReaderStatus,
    LcscReaderLogin,
    LcscReaderLogout,
    LcscReaderGetCardID,
    LcscReaderGetCardBalance,
    LcscReaderGetCardDeduct,
    LcscReaderGetCardRecord,
    LcscReaderGetCardFlush,
    LcscReaderGetTime,
    LcscReaderSetTime,
    LcscReaderUploadCFGFile,
    LcscReaderUploadCILFile,
    LcscReaderUploadBLFile,

    // DIO
    DioEvent,

    // KSM Reader
    KsmReaderInit,
    KsmReaderGetStatus,
    KsmReaderEjectToFront,
    KsmReaderCardAllowed,
    KsmReaderCardProhibited,
    KsmReaderCardOnIc,
    KsmReaderIcPowerOn,
    KsmReaderWarmReset,
    KsmReaderSelectFile1,
    KsmReaderSelectFile2,
    KsmReaderReadCardInfo,
    KsmReaderReadCardBalance,
    KsmReaderIcPowerOff,
    KsmReaderCardIn,
    KsmReaderCardOut,
    KsmReaderCardTakeAway,
    KsmReaderCardInfo,

    // LPR
    LprReceive,

    // UPT
    UptCardDetect,
    UptPaymentAuto,
    UptDeviceSettlement,
    UptRetrieveLastSettlement,
    UptDeviceLogon,
    UptDeviceStatus,
    UptDeviceTimeSync,
    UptDeviceTMS,
    UptDeviceReset,
    UptCommandCancel,

    // Printer / Barcode
    PrinterStatus,
    BarcodeReceived,

    // EEP
    EepClientResponse,
    EepClientConnectionState,

    // CHU
    ChuReceived,
    ChuClientConnectionState
};

using OperationEventData = std::variant<std::monostate, int, bool, std::string>;

struct OperationEvent
{
    OperationEventType type{};
    OperationEventData data{};
};

class operation
{
public:
    static operation* getInstance();

    bool FnOperationInit();
    void FnStopDailyProcessTimer();
    bool FnIsOperationInitialized() const;

    // Thread-safe shared-state boundary.
    // FnGetSharedData() returns a snapshot copy; no internal reference escapes.
    // FnUpdateSharedData() applies only populated field-level patches on OP_IO.
    std::optional<OperationSharedData> FnGetSharedData();
    bool FnUpdateSharedData(OperationSharedDataUpdate update);

    // =========================================================
    // Thread-safe EventHandler entry point.
    //
    // This is the only EventHandler -> Operation public API.
    // The event owns its copied payload. FnOnEvent() posts exactly once to
    // Operation's OP_IO thread; all event-specific handling happens there.
    // =========================================================
    bool FnOnEvent(OperationEvent event);

    // =========================================================
    // Thread-safe UDP packet entry points.
    //
    // udpclient only receives bytes and identifies the packet source.
    // All PMS/Monitor protocol and business processing is serialized on OP_IO.
    // =========================================================
    bool FnOnPmsUdpPacket(std::string senderIp, std::string packet);
    bool FnOnMonitorUdpPacket(std::string senderIp, std::string packet);
    
    void FnSendDIOInputStatusToMonitor(int pinNum, int pinValue);
    void FnSendLogMessageToMonitor(std::string msg);
    void FnSendLEDMessageToMonitor(std::string line1TextMsg, std::string line2TextMsg);

    void FnSendMsg2Server(std::string cmdCode, std::string data);
    
    float GfeeFormat(float value); // db module

    void FnClose();

    operation(const operation&) = delete;
    operation& operator=(const operation&) = delete;
    operation(operation&&) = delete;
    operation& operator=(operation&&) = delete;
    
private:
    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    operation();
    ~operation();

    boost::asio::io_context ioContext_;
    std::optional<WorkGuard> workGuard_;
    std::thread ioContextThread_;
    std::mutex lifecycleMutex_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};

    // =========================================================
    // OP_IO-owned business state.
    //
    // These objects are private. External modules must use the thread-safe
    // public APIs and may never keep references/pointers to this state.
    // =========================================================
    std::atomic<bool> isOperationInitialized_{false};
    tstation_struct gtStation{};
    tEntryTrans_Struct tEntry{};
    tExitTrans_Struct tExit{};
    tExitTrans_Struct tExit1{};
    tProcess_Struct tProcess{};
    tParas_Struct tParas{};
    tMsg_Struct tMsg{};
    tExitMsg_struct tExitMsg{};
    static const int Errsize = 17;
    tPBSError_struct tPBSError[20]{};
    tseason_struct tSeason{};
    std::vector<tVType_Struct> tVType;
    std::vector<tTR_struc> tTR;

    std::unique_ptr<udpclient> pmsUdpClient_;
    std::unique_ptr<udpclient> monitorUdpClient_;

    std::unique_ptr<boost::asio::steady_timer> pLCDIdleTimer_;
    std::unique_ptr<boost::asio::steady_timer> pLoopATimer_;
    std::unique_ptr<boost::asio::steady_timer> pDailyProcessTimer_;
    
    std::chrono::steady_clock::time_point lastActionTimeAfterLoopA_;
    std::chrono::steady_clock::time_point lastDailyProcessSyncTime_;
    std::string lastUPTSettleTime_;

    bool startIoContextThread();
    bool initializeOnIoThread();
    void initDeviceOnIoThread();
    void shutdownOnIoThread();

    void scheduleLcdIdleTimer();
    void handleLcdIdleTimer(const boost::system::error_code& ec);

    void startDailyProcessTimer();
    void scheduleDailyProcessTimer(std::chrono::seconds delay);
    void handleDailyProcessTimer(const boost::system::error_code& ec);

    // Event dispatch helper. Executes inline only when already on OP_IO;
    // otherwise posts to Operation's io_context.
    bool postEvent(std::function<void()> handler);


    enum class KsmFailureAction
    {
        EnableError,
        CardReadError
    };

    // EventHandler event dispatcher. OP_IO only.
    void handleEventOnIo(OperationEvent event);
    void handleKsmResultOnIo(
        bool success,
        const char* description,
        KsmFailureAction failureAction);
    void handleLprReceiveOnIo(std::string eventData);

    // Shared-state helpers. OP_IO only.
    OperationSharedData makeSharedDataSnapshotOnIo() const;
    void applySharedDataUpdateOnIo(OperationSharedDataUpdate update);

    // Event business handlers. OP_IO only.
    void handleAntennaFailOnIo(int errorCode);
    void handleAntennaPowerOnIo(bool poweredOn);
    void handleAntennaIUComeOnIo(std::string iuNo);
    void handleDioEventOnIo(int eventValue);
    void handleKsmReaderInitOnIo(bool success);
    void handlePrinterStatusOnIo(int status);
    void handleEepConnectionStateOnIo(bool connected);
    void handleChuConnectionStateOnIo(const std::string& status);

    // UDP protocol/business processing. OP_IO only.
    void processPmsUdpPacketOnIo(const std::string& senderIp, const std::string& packet);
    void processMonitorUdpPacketOnIo(const std::string& senderIp, const std::string& packet);

    std::string getSerialPort(const std::string& key);
    bool copyFiles(const std::string& mountPoint, const std::string& sharedFolderPath, 
                    const std::string& username, const std::string& password, const std::string& outputFolderPath);
    void startLoopAPeriodicTimer();
    void stopLoopAPeriodicTimer();
    void handleLoopAPeriodicTimerTimeout(const boost::system::error_code &ec);

    // Internal operation function
    void LoopACome();
    void LoopAGone();
    void LoopCCome();
    void LoopCGone();
    void VehicleCome(const std::string& sNo);
    void ShowLEDMsg(string LEDMsg, string LCDMsg);
    void HandlePBSError(EPSError iEPSErr, int iErrCode=0);
    void EnableCashcard(bool bEnable);
    void ProcessLCSC(const std::string& eventData);
    void ProcessBarcodeData(string sBarcodedata);
    void KSM_CardIn();
    void KSM_CardInfo(const std::string& sKSMCardNo, long sKSMCardBal, bool sKSMCardExpired);
    void KSM_CardTakeAway();
    void handleKSM_EnableError();
    void handleKSM_CardReadError();
    void ReceivedLPR(
            Lpr::CType CType,
            const std::string& LPN,
            const std::string& sTransid,
            const std::string& sImageLocation);
    void processUPT(Upt::UPT_CMD cmd, const std::string& eventData);
    void PrintReceipt();
    void PBSEntry(string sIU);
    void Sendmystatus();
    void sendMyStatusToMonitor();
    void syncCentralDBTime();
    void sendCmdDownloadParamAckToMonitor(bool success);
    void sendCmdDownloadIniAckToMonitor(bool success);
    void sendCmdGetStationCurrLogToMonitor();
    bool CopyIniFile(const std::string& serverIpAddress, const std::string& stationID);
    void SendMsg2Monitor(const std::string& cmdcode, const std::string& dstr);
    void ShowTotalLots(const std::string& totallots, const std::string& LEDId = "***");
    void ManualOpenBarrier(bool bPMS);
    void ManualCloseBarrier();
    void ReceivedEntryRecord();
    void continueOpenBarrier();
    void SendMsg2Server(const std::string& cmdcode, const std::string& dstr);
    void UpdateExit();
    float CalFeeRAM(string eTime, string payTime,int iTransType, bool bCheckGT = false);
    void PBSExit(string sIU,DeviceType iDevicetype,string sCardNo = "", int sCardType = 0,float sCardBal = 0);
    void debitfromReader(string CardNo, float sFee, DeviceType iDevicetype,int sCardType = 0,float sCardBal = 0);
    void CheckIUorCardStatus(string sCheckNo, DeviceType iDevicetype,string sCardNo = "",int sCardType = 0,float sCardBal = 0);
    void Setdefaultparameter();
    string getIPAddress();
    void sendDateTimeToMonitor();
    int CheckSeason(const std::string& sIU, int iInOutt);
    void writelog(const std::string& sMsg, const std::string& soption);
    int GetVTypeFromLoop();
    void SaveEntry();
    void SaveExit();
    void CloseExitOperation(TransType iStatus);
    void FormatSeasonMsg(
            int iReturn,
            const std::string& sNo,
            std::string sMsg,
            std::string sLCD,
            int iExpires = -1);
    bool LoadParameter();
    bool LoadedparameterOK();
    int GetSeasonTransType(int VehicleType, int SeasonType, int TransType);
    void EnableLCSC(bool bEnable);
    void EnableKDE(bool bEnable);
    void EnableUPOS(bool bEnable);
    bool AntennaOK();
    void CheckReader();
    void PrintTR(bool bForSeason = false);
    void DebitOK(const std::string& sIUNO, const std::string& sCardNo, 
                const std::string& sPaidAmt = "", const std::string& sBal = "",
                int iCardType = 0, const std::string& sTopupAmt = "",
                DeviceType iDevicetype = Ant, const std::string& sTransTime = "");
    std::string GetVTypeStr(int iVType);
    void ticketScan(std::string skeyedNo);
    void ticketOK();
    void showFee2User(bool bPaying = false);
    void RedeemTime2Amt();
    void closeBarrier();
    void Openbarrier(int iReason = 0);

    void lcdIdleTimerTimeoutHandler();
    void loopATimeoutHandler();
    void setLastActionTimeAfterLoopA();
    std::chrono::steady_clock::time_point getLastActionTimeAfterLoopA();

    void ProcessOUBInformation(EEPClient::obuInformationNotification OBUInfo);
    void processEEPTransData(EEPClient::transactionData transData);

    void EndEEPprocess(int ProcessingResult = 0);
    void EEPInq(int delay = 0);
    void EEPDebit(string OBU, float lFee, string entryTime, string exitTime);
    void SendMsg2OBU(std::string OBU, int DType, std::string line1,std::string line2, std::string line3, std::string line4, std::string line5);

    void CHUInq(int delay = 0);
    void CHUDebit(string OBU, float lFee,string sCardNo,float sCardBal);
    void SendMsg2CHU(eCHUCmd sCmd, string sData = "");

    void processEEP(const std::string& eventData);

    void processCHU(const std::string& eventData);
 //   void ProcessCHUInformation(const std::string& CHUInfoData);
 //   void processCHUTransData(const std::string& CHUTransData);

    void Clearme();
    void RetryLCSCLastCommand();
};
