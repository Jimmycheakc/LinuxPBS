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
#include "timer.h"
#include "upt.h"

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
    void PBSExit(string sIU);
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
    void ShowTotalLots(std::string totallots, std::string LEDId = "***");
    void FormatSeasonMsg(int iReturn, string sNo, string sMsg, string sLCD, int iExpires=-1);
    void ManualOpenBarrier();
    void ManualCloseBarrier();
    bool LoadParameter();
    bool LoadedparameterOK();
    int  GetSeasonTransType(int VehicleType, int SeasonType, int TransType);
    void EnableCashcard(bool bEnable);
    void EnableLCSC(bool bEnable);
    void EnableKDE(bool bEnable);
    void EnableUPOS(bool bEnable);
    void ProcessLCSC(const std::string& eventData);
    void KSM_CardIn();
    void KSM_CardInfo(string sKSMCardNo, long sKSMCardBal, bool sKSMCardExpired);
    void KSM_CardTakeAway();
    bool AntennaOK();
    void CheckReader();
    void ReceivedLPR(Lpr::CType CType,string LPN, string sTransid, string sImageLocation);
    void processUPT(Upt::UPT_CMD cmd, const std::string& eventData);
    void PrintTR(bool bForSeason = false);

    void Openbarrier();

    void LcdIdleTimerTimeoutHandler();
    void FnLoopATimeoutHandler();

    void Clearme();

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
    std::unique_ptr<Timer> pLCDIdleTimer_;
    std::unique_ptr<boost::asio::deadline_timer> pLoopATimer_;
    operation();
    ~operation() {
        delete m_udp;
        delete m_db;
        delete operation_;
    };
    std::string getSerialPort(const std::string& key);
    bool copyFiles(const std::string& mountPoint, const std::string& sharedFolderPath, 
                    const std::string& username, const std::string& password, const std::string& outputFolderPath);
};
