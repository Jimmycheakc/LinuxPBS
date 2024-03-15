#pragma once

#include "ce_time.h"

typedef enum {
	_Online,
	_Offline

}PMS_Comm;

typedef enum {
	s_Entry,
	s_Exit

}Ctrl_Type;

//action when carpark full
typedef enum {
	iNoAct = 0,
	iallowseason = 1,
	iNoPartial = 2,
	iLock = 3

}eFullAction;



//check season method
typedef enum {
	iSOnline = 0,  //online, then offline
	iSDB = 1,      //offline, in DB
	iSRAM = 2      //offline, in RAM

}eCheckSeason;

typedef enum {
	ChuRC_None = 0,                     // None
    ChuRC_NoResp = -1,                  // "No response !"
    ChuRC_ACK = 1,                      // ACK
    ChuRC_NAK = 2,                      // NACK
    ChuRC_OK = 3,                       // Execute Ok
    ChuRC_SendOK = 4,                   // Send Ok
    ChuRC_Fail = 5,                     // Send Err
    ChuRC_SendErr = 6,                  // Send Err
    ChuRC_LenErr = 7,                   // Len error
    ChuRC_Unknown = 8,                  // Unknown response from CHU
    ChuRC_TcpConnected = 9,             // Connect to Tcp line
    ChuRC_TcpSckErr = 10,               // Tcp socks error
    ChuRC_Tcptimeout = 11,              // Connect to Tcp line timeout
    ChuRC_DebitOk = 12,                 // Debit OK
    ChuRC_DebitFail = 13,               // Debit fail
    ChuRC_EntryWithCashcard = 14,       // Found IU, No Cashcard
    ChuRC_EntryWithoutCashcard = 15,    // Found IU & Cashcard
    ChuRC_Processing = 16,              // Processing
    ChuRC_AntBusy = 17,                 // Ant busy
    
    ChuRC_AntStopErr = 18,              // Ant Stop Error
    ChuRC_AntStartErr = 19,             // Ant Start Error
    ChuRC_WDReqErr = 20,                // Watchdog request error
    ChuRC_HDDiagErr = 21,               // Hardware diag error
    ChuRC_StatusReqErr = 22,            // Request CHU Status error
    ChuRC_TimeCaliErr = 23,             // Calibrate CHU date&time error

    ChuRC_CNNBroken = 24,              // CNN broken
    ChuRC_InvalidCmd = 25,              // Invalid CMD
    ChuRC_UnknownCmd = 26              // Unknown CMD

}eChuRC;


typedef enum {
	tientry = 1,
	tiExit = 2,
}tStationType;

typedef enum {
	iNormal = 0,
	iXwithVENoPay = 1,
	iXwithVEPay = 2
}eSubType;

typedef enum {
  iAntenna = 0,     
  iPrinter = 1,   
  iDB = 2,
  iReader = 3, 
  iUPOS = 4,
  iParam = 5,
  iDIO = 6,
  iLOOPAhang = 7,
  iCHU = 8,
  iUPS = 9,
  iLCSC = 10,
  iStatinonDoor = 11,
  iBarrierDoor = 12,
  iTGDController = 13,
  iTGDSensor = 14,
  iArmDrop = 15,
  iBarrierStauts = 16,
  iTicketStatus = 17

}eErrorDevice;

typedef enum 
{
	NoError = 0,
    AntennaNoError = 1,
    AntennaError = 2,
    PrinterNoError = 3,
    PrinterError = 4,
    PrinterNoPaper = 5,
    DBNoError = 6,
    DBFailed = 7,
    DBUpdateFail = 8,
    ReaderNoError = 9,
    ReaderError = 10,
    UPOSNoError = 11,
    UPOSError = 12,
    AntennaPowerOnOff = 14,
    TariffError = 15,
    TariffOk = 16,
    HolidayError = 17,
    HolidayOk = 18,
    DIOError = 19,
    DIOOk = 20,
    LoopAHang = 21,
    LoopAOk = 22,
    LCSCNoError = 23,
    LCSCError = 24,
    SDoorError = 25,
	SDoorNoError = 26,
    BDoorError = 27,
	BDoorNoError = 28
} EPSError;  

class trans_info {
public:
	string TrxTime;
	string Operation;//stn type
	string StnID;
    string IUNo;
    string VehicleNo;
    string SeaType;
    string UnitNo;

};

struct  tstation_struct
{
	int iSID;
	string sName;
	tStationType iType;
	int iStatus;
	string sPCName;
	int iCHUPort;
	int iAntID;

	int iZoneID;
	int iGroupID;
	int iIsVirtual;
	eSubType iSubType;
	int iVirtualID;
	string sZoneName;
	int iVExitID;
};

struct tariff_Struct{
	public:
	string tariff_id;
	string day_index;
	string day_type;
	string start_time[9];
	string end_time[9];
	string rate_type[9];
	string charge_time_block[9];
	string charge_rate[9];
	string grace_time[9];
	string first_free[9];
	string first_add[9];
	string second_free[9];
	string second_add[9];
	string third_free[9];
	string third_add[9];
	string allowance[9];
	string min_charge[9];
	string max_charge[9];
	string zone_cutoff;
	string day_cutoff;
	string whole_day_max;
	string whole_day_min;
};


//struct  tStation_Struct g_sts;

struct  tEntryTrans_Struct
{
	string esid;
	string sSerialNo;     //for ticket
	string sIUTKNo;
	string sEntryTime;
	int iTransType;
	short iRateType;
	int iStatus;  //enter or reverse
	string sCardNo;
	float sFee=0;
	float sPaidAmt=0;
	float sGSTAmt;
	string sReceiptNo;
	int iCardType=0;
	float sOweAmt=0;
//	bool sEnableReader;
	string sLPN[2];
	int iVehcileType;
//	string sGreeting;
//	string sRPLPN;
//	string sPaidtime;
	bool gbEntryOK;

};



struct  tExitTrans_Struct
{

	string xsid;
	string sIUNo;
	int iTransType;
	short  iRateType;
	string sCardNo;
	string sEntryTime;
	string sExitTime;
	string sReceiptNo;
	long lParkedTime;
	float sFee;
	float sPaidAmt=0;
	string sPwd;
	int iStatus;
	float sRedeemAmt;
	// long iRedeemTime;
	short iRedeemTime;
	string sTag;
	float sPrePaid;
	string sRedeemNo;
	float sGSTAmt;
	int iWholeDay;
	string sWDFrom;
	string sWDTo;

	string sCHUDebitCode;

	CE_Time dtValidTo;
	CE_Time dtValidFrom;
	int iCardType;
	float sTopupAmt;
	float sOweAmt;
	int iEntryID;
	string sExitNo;
	int iUseMultiCard;
	int iNeedSendIUtoEntry;
	int iPartialseason;
	string sRegisterCard;
	int iAttachedTransType;

	string sCalFeeTime;
	float sRebateAmt;
	float sRebateBalance;
	CE_Time sRebateDate;
	string sLPN[2];
	int iVehcielType;
	string sIUNo1;

	string entry_lpn;
	string video_location;
	string video1_location;
	string sRPLPN;


};


struct  tProcess_Struct
{
	
	int save_trans=0;
	bool gbcarparkfull;
	long glNoofOfflineData;
	string fsLastIUNo;
	string fsLastCardNo;
	int is_season;
	int online_status;
	int offline_status;
	int giSystemOnline;
	string fsLastPaidIU;
	string fsLastReadCard;
	string gsDefaultIU; 
	string gsBroadCastIP;
	bool gbLoopApresent;
	bool gbLoopAIsOn;
	bool gbLoopBIsOn;
	bool gbLoopCIsOn;
	int giSyncTimeCnt;
	bool gbloadedParam;
	bool gbloadedVehtype;
	bool gbloadedLEDMsg;
	bool gbloadedStnSetup;
	int gbInitParamFail;
	bool WaitForLCSCReturn;
	int giCardIsIn;
	int giLastHousekeepingDate;
};




struct tVType_Struct
{
	int iIUCode;
	int iType;
};



struct  tParas_Struct
{
	string gsCentralDBName;
	string gsCentralDBServer;
	int giCommPortAntenna;
	int giCommPortLCSC;
	int giCommPortKDEReader;
	int giCommPortUPOS;
	string gsLocalLCSC;
	string gsRemoteLCSC;
	string gsRemoteLCSCBack;
	string gsCSCRcdfFolder;
	string gsCSCRcdackFolder;
	string gsCPOID;
	string gsCPID;
	int giUploadBLTO;	//not in current central DB
	int giCommPortLED;
	int giHasMCycle;
	int giTicketSiteID;

	int giDataKeepDays;
	int gsBarrierPulse;

	int giAntMaxRetry;
	int giAntMinOKTimes;
	int giAntInqTO;
	bool gbAntiIURepetition;

	int giCommportLED401;
	int giAllowMultiEntry; //not in current central DB
	int giMCAllowMultiEntry; //not in current central DB

	int giIsHDBSite;
	string gsAllowedHolderType;
	
	int giLEDMaxChar;
	
	bool gbAlwaysTryOnline;
	bool gbAutoDebitNoEntry;
	
	int giLoopAHangTime;
	int giOperationTO;

	eFullAction giFullAction;
	int giBarrierOpenTooLongTime;
	int giBitBarrierArmBroken;
	int giMCControlAction;
	
	bool gbLockBarrier;	//not in current central DB

	string gsLogBackFolder;
	int giLogKeepDays;
	string gsDBBackupFolder;
	int giMaxSendOfflineNo;
	long glMaxLocalDBSize;

	string lprip_front; //not in current central DB
	string lprip_rear; //not in current central DB
	int wait_lpr_notime; //not in current central DB
	string nexpa_dbserver; //not in current central DB
	string nexpa_dbname; //not in current central DB
	int sid;	//not in current central DB

	int taxi_control; //not in current central DB
	int local_udpport; //not in current central DB
	int remote_udpport; //not in current central DB
	int not_allow_hourly; //not in current central DB
	int cepas_enable; //not in current central DB


	//int giCommPortPrinter;
	//int giCommPortLED2;
	//string gsSite;
	//string gsCompany;
	//string gsZIP;
	//string gsGSTNo;
	//string gsAddress;
	//string gsTel;
	
	//string gsCHUIP;
	//bool gbEnableVoice;
	//int giVoice;
	
	//float gsMotorRate;
	//int giMCEntryGraceTime;
	//int giHasThreeWheelMC;
	

	//bool mbOvernightCharge;
	//string msOvernightTime;
	//int giTariffFeeMode;
	//int giTariffGTMode;
	//int giFirstHourMode;
	//int giFirstHour;
	//int giPEAllowance;
	//int giHr2PEAllowance;
	
	//int giMaxDebitDays;
	//int giHasHolidayEve;
	
	//int giProcessReversedCMD;
	//int giExitGraceTime;
	
	//float gsShutterPulse;
	//int giSeasonCharge;
	//bool gbSeasonAsNormal;
	//int giSeasonPayStart;
	//int giSeasonAllowance;
	//eCheckSeason giCheckSeason;
	//int giShowSeasonExpireDays;
	//int giShowExpiredTime;
	//int giCHUCnTO;
	//int giCHUComTO;
	//int giCHUComRetry;
	//int giNoIURetry;
	//int giNoCardRetry;
	//int giCHUDebitRetry;
	//int giNoIUWT;
	//int giNoCardWT;
	//int giDebitNakWT;
	//int giCardProblemWT;
	//int giDebitFailWT;
	//nt giCHUDebitWT;
	//int giNoIUAutoDebitWT;
	//int giMaxDiffIU;
	//int giTryTimes4NE;
	
	
	//int giPrinterType;
	//int giPrintMode;
	
	
	//bool gbLoopAFirst;
	//int giCloseBarrierIfTO;
	//int giPrintReceiptTO;
	//int giTransactionTO;
	//int giRefreshTime;
	
	//int giLoopC2OpenBarWT;
	
	//bool gbHasRedemption;
	//float msOvernightAmt;
	//int giHasLorry;
	//int giHasContainer;
	//int giLorryRelayTime;
	//int giMaxTransInterval;
	//int giMOBRelayTimes;
	
	//int giUpdateAfterExit;
	
	

	//int giV3TransType;
	//int giV4TransType;
	//int giV5TransType;
	//int giUseMagCard;
	
	
	//int showtime;
	//int hasinternal_link;
	
	//int has_internal;
	//string attached_dbname;
	//string attached_dbserver;
	
	//int attachedexit_udpport;
	//int attachedexit_id;

};

struct  tMsg_Struct
{
	// Message Entry - index 0 : LED, index 1 : LCD
	std::string MsgEntry_AltDefaultLED[2];
	std::string MsgEntry_AltDefaultLED2[2];
	std::string MsgEntry_AltDefaultLED3[2];
	std::string MsgEntry_AltDefaultLED4[2];

	std::string MsgEntry_authorizedvehicle[2];
	std::string MsgEntry_CardReadingError[2];
	std::string MsgEntry_CardTaken[2];
	std::string MsgEntry_CarFullLED[2];
	std::string MsgEntry_CarParkFull2LED[2];
	std::string MsgEntry_DBError[2];
	std::string MsgEntry_DefaultIU[2];
	std::string MsgEntry_DefaultLED[2];
	std::string MsgEntry_DefaultLED2[2];
	std::string MsgEntry_DefaultMsg2LED[2];
	std::string MsgEntry_DefaultMsgLED[2];
	std::string MsgEntry_EenhancedMCParking[2];
	std::string MsgEntry_ESeasonWithinAllowance[2];
	std::string MsgEntry_ESPT3Parking[2];
	std::string MsgEntry_EVIPHolderParking[2];
	std::string MsgEntry_ExpiringSeason[2];
	std::string MsgEntry_FullLED[2];
	std::string MsgEntry_Idle[2];
	std::string MsgEntry_InsertCashcard[2];
	std::string MsgEntry_IUProblem[2];
	std::string MsgEntry_LockStation[2];
	std::string MsgEntry_LoopA[2];
	std::string MsgEntry_LoopAFull[2];
	std::string MsgEntry_LorryFullLED[2];
	std::string MsgEntry_LotAdjustmentMsg[2];
	std::string MsgEntry_LowBal[2];
	std::string MsgEntry_NoIU[2];
	std::string MsgEntry_NoNightParking2LED[2];
	std::string MsgEntry_Offline[2];
	std::string MsgEntry_PrinterError[2];
	std::string MsgEntry_PrintingReceipt[2];
	std::string MsgEntry_Processing[2];
	std::string MsgEntry_ReaderCommError[2];
	std::string MsgEntry_ReaderError[2];
	std::string MsgEntry_SameLastIU[2];
	std::string MsgEntry_ScanEntryTicket[2];
	std::string MsgEntry_ScanValTicket[2];
	std::string MsgEntry_SeasonAsHourly[2];
	std::string MsgEntry_SeasonBlocked[2];
	std::string MsgEntry_SeasonExpired[2];
	std::string MsgEntry_SeasonInvalid[2];
	std::string MsgEntry_SeasonMultiFound[2];
	std::string MsgEntry_SeasonNotFound[2];
	std::string MsgEntry_SeasonNotStart[2];
	std::string MsgEntry_SeasonTerminated[2];
	std::string MsgEntry_SeasonNotValid[2];
	std::string MsgEntry_SeasonOnly[2];
	std::string MsgEntry_SeasonPassback[2];
	
	std::string MsgEntry_SystemError[2];
	std::string MsgEntry_ValidSeason[2];
	std::string MsgEntry_VVIP[2];
	std::string MsgEntry_WholeDayParking[2];
	std::string MsgEntry_WithIU[2];
	std::string MsgEntry_E1enhancedMCParking[2];
	std::string MsgBlackList[2];
};

struct tseason_struct {

    string season_no;
    string SeasonType;
	string s_status;
    string date_from;
    string date_to;
    string vehicle_no;
    string rate_type;
    string pay_to;
    string pay_date;
    string multi_season_no;
	string zone_id;
    string redeem_amt;
	string redeem_time;
	string holder_type;
	string sub_zone_id;
	int found;
};

struct tPBSError_struct {
	int ErrNo;
	int ErrCode;
	string ErrMsg;
};


