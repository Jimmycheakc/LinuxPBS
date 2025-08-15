#pragma once

#include <stdio.h>
#include <string>
#include <sstream>
#include <iostream>
#include <mutex>
#include "lcsc.h"
#include "structuredata.h"
#include "db.h"
#include "udp.h"
#include "lpr.h"
#include "upt.h"

typedef enum : unsigned int
{
    FreeParking        = 0,
    SeasonParking      = 1,
    GracePeriod        = 2,
    DeductionOK        = 3,
    DeductionFail      = 4,
    Manualopen         = 5,
    BalanceChange      = 6
} TransType;

typedef enum : unsigned int
{
    // device type : 0 = PMS(EntryTime), 1 = Ant, 2 = LCSC, 3 = UPOS, 4 = CHU
    PMSEntrytime        = 0,
    Ant                 = 1,
    LCSC                = 2,
    UPOS                = 3,
    CHU                 = 4
} DeviceType;

class operation
{
public:
    static operation* getInstance();
    db *m_db;
    udpclient *m_udp;
    udpclient *m_Monitorudp;
    boost::asio::io_context* iCurrentContext;
    std::atomic<bool> isOperationInitialized_;
    struct  tstation_struct gtStation;
    struct  tEntryTrans_Struct tEntry; 
    struct  tExitTrans_Struct tExit;
    struct  tProcess_Struct tProcess;
    struct  tParas_Struct tParas;
	struct  tMsg_Struct tMsg;
    struct tExitMsg_struct tExitMsg;
    static const int Errsize = 17;
    struct  tPBSError_struct tPBSError[20];
    struct  tseason_struct tSeason;
    std::vector<struct tVType_Struct> tVType;
    std::vector<struct tTR_struc> tTR;


    void OperationInit(io_context& ioContext);
    bool FnIsOperationInitialized() const;
    void LoopACome();
    void LoopAGone();
    void LoopCCome();
    void LoopCGone();
    void VehicleCome(string sNo);
    void Initdevice(io_context& ioContext);
    void ShowLEDMsg(string LEDMsg, string LCDMsg);
    void PBSEntry(string sIU);
    void PBSExit(string sIU,DeviceType iDevicetype,string sCardNo = "", int sCardType = 0,float sCardBal = 0);
    void debitfromReader(string CardNo, float sFee, DeviceType iDevicetype,int sCardType = 0,float sCardBal = 0);
    void CheckIUorCardStatus(string sCheckNo, DeviceType iDevicetype,string sCardNo = "",int sCardType = 0,float sCardBal = 0);
    float CalFeeRAM(string eTime, string payTime,int iTransType, bool bCheckGT = false);
    void Setdefaultparameter();
    string getIPAddress(); 
    void Sendmystatus();
    void FnSendMyStatusToMonitor();
    void FnSyncCentralDBTime();
    void FnSendDIOInputStatusToMonitor(int pinNum, int pinValue);
    void FnSendDateTimeToMonitor();
    void FnSendLogMessageToMonitor(std::string msg);
    void FnSendLEDMessageToMonitor(std::string line1TextMsg, std::string line2TextMsg);
    void FnSendCmdDownloadParamAckToMonitor(bool success);
    void FnSendCmdDownloadIniAckToMonitor(bool success);
    void FnSendCmdGetStationCurrLogToMonitor();
    bool CopyIniFile(const std::string& serverIpAddress, const std::string& stationID);
    void SendMsg2Monitor(string cmdcode,string dstr);
    void SendMsg2Server(string cmdcode,string dstr);
    int  CheckSeason(string sIU,int iInOut);
    void writelog(string sMsg, string soption);
    void HandlePBSError(EPSError iEPSErr, int iErrCode=0);
    int  GetVTypeFromLoop();
    void SaveEntry();
    void SaveExit();
    void CloseExitOperation(TransType iStatus);
    void ShowTotalLots(std::string totallots, std::string LEDId = "***");
    void FormatSeasonMsg(int iReturn, string sNo, string sMsg, string sLCD, int iExpires=-1);
    void ManualOpenBarrier(bool bPMS);
    void ManualCloseBarrier();
    bool LoadParameter();
    bool LoadedparameterOK();
    int  GetSeasonTransType(int VehicleType, int SeasonType, int TransType);
    void EnableCashcard(bool bEnable);
    void EnableLCSC(bool bEnable);
    void EnableKDE(bool bEnable);
    void EnableUPOS(bool bEnable);
    void ProcessLCSC(const std::string& eventData);
    void ProcessBarcodeData(string sBarcodedata);
    void KSM_CardIn();
    void KSM_CardInfo(string sKSMCardNo, long sKSMCardBal, bool sKSMCardExpired);
    void KSM_CardTakeAway();
    void handleKSM_EnableError();
    void handleKSM_CardReadError();
    bool AntennaOK();
    void CheckReader();
    void ReceivedLPR(Lpr::CType CType,string LPN, string sTransid, string sImageLocation);
    void processUPT(Upt::UPT_CMD cmd, const std::string& eventData);
    void PrintTR(bool bForSeason = false);
    void PrintReceipt();
    void RetryEntryInq(int iRetryType);
    void DebitOK(const std::string& sIUNO, const std::string& sCardNo, 
                const std::string& sPaidAmt = "", const std::string& sBal = "",
                int iCardType = 0, const std::string& sTopupAmt = "",
                DeviceType iDevicetype = Ant, const std::string& sTransTime = "");
    std::string GetVTypeStr(int iVType);
    void ticketScan(std::string skeyedNo);
    void ticketOK();
    void showFee2User(bool bPaying = false);
    
    float GfeeFormat(float value);
    void  RedeemTime2Amt();
    void  ReceivedEntryRecord();
    
    void Openbarrier();
    void closeBarrier();
    void continueOpenBarrier();

    void LcdIdleTimerTimeoutHandler();
    void FnLoopATimeoutHandler();
    void FnSetLastActionTimeAfterLoopA();
    std::chrono::steady_clock::time_point FnGetLastActionTimeAfterLoopA();

    void Clearme();
    void RetryLCSCLastCommand();

     /**
     * Singleton opertation should not be cloneable.
     */
    operation (operation&) = delete;

    /**
     * Singleton operation should not be assignable.
     */
    void operator=(const operation&) = delete;

    
private:
    
    static operation* operation_;
    static std::mutex mutex_;
    std::unique_ptr<boost::asio::io_context::strand> operationStrand_;
    std::unique_ptr<boost::asio::steady_timer> pLCDIdleTimer_;
    std::unique_ptr<boost::asio::steady_timer> pLoopATimer_;
    std::chrono::steady_clock::time_point lastActionTimeAfterLoopA_;
    operation();
    ~operation() {
        delete m_udp;
        delete m_db;
        delete operation_;
    };
    std::string getSerialPort(const std::string& key);
    bool copyFiles(const std::string& mountPoint, const std::string& sharedFolderPath, 
                    const std::string& username, const std::string& password, const std::string& outputFolderPath);
    void startLoopAPeriodicTimer();
    void handleLoopAPeriodicTimerTimeout(const boost::system::error_code &ec);
};
