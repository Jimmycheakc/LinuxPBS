#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
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
	Init = 0,
	Login = 1,  	
	Enable = 2,      
	Disable = 3,
	ReadCardTimeout = 4      
	
}eUPOSStatus;

typedef enum : unsigned int
{
    init					= 0,
	WaitingCard   			= 1,
	Doingdeduction 			= 2,
	DeductionSuccessed     	= 3,
	Deductionfailed			= 4,
	CardExpired 			= 5,
	CardFault				= 6
} eProcessStatus;

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
	tiAPS = 6
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
  iBarrierStatus = 16,
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
	BDoorNoError = 28,
	BarrierStatus = 29
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

struct tariff_struct
{
	std::string tariff_id;
	std::string day_index;
	std::string day_type;
	std::string start_time[9];
	std::string end_time[9];
	std::string rate_type[9];
	std::string charge_time_block[9];
	std::string charge_rate[9];
	std::string grace_time[9];
	std::string first_free[9];
	std::string first_add[9];
	std::string second_free[9];
	std::string second_add[9];
	std::string third_free[9];
	std::string third_add[9];
	std::string allowance[9];
	std::string min_charge[9];
	std::string max_charge[9];
	std::string zone_cutoff;
	std::string day_cutoff;
	std::string whole_day_max;
	std::string whole_day_min;
	int dtype;
};

struct tariff_type_info_struct
{
	std::string tariff_type;
	std::string start_time;
	std::string end_time;
};

struct x_tariff_struct
{
	std::string day_index;
	std::string auto0;
	std::string fee0;
	std::string time1;
	std::string auto1;
	std::string fee1;
	std::string time2;
	std::string auto2;
	std::string fee2;
	std::string time3;
	std::string auto3;
	std::string fee3;
	std::string time4;
	std::string auto4;
	std::string fee4;
};


struct tariff_info_struct
{
	std::string rate_type;
	std::string day_type;
	std::string time_from;
	std::string time_till;
	std::string t3_start;
	std::string t3_block;
	std::string t3_rate;
};

struct rate_free_info_struct
{
	std::string rate_type;
	std::string day_type;
	std::string init_free;
	std::string free_beg;
	std::string free_end;
	std::string free_time;
};

struct rate_type_info_struct
{
	std::string rate_type;
	std::string has_holiday;
	std::string has_holiday_eve;
	std::string has_special_day;
	std::string has_init_free;
	std::string has_3tariff;
	std::string has_zone_max;
	std::string has_firstentry_rate;
};

struct rate_max_info_struct
{
	std::string rate_type;
	std::string day_type;
	std::string start_time;
	std::string end_time;
	std::string max_fee;
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
	string sLPN[2];
	int iVehicleType;
//	string sGreeting;
//	string sRPLPN;
//	string sPaidtime;
	bool gbEntryOK;
	string sTag;
	string sCHUDebitCode;
};

struct  tExitTrans_Struct
{
	string xsid;
	string sIUNo;
	string sCardNo;
	//------
	int iTransType;
	int iCardType;
	int iVehicleType;
	//----
	int iEntryID;
	string sEntryTime;
	//-----
	string sExitNo;
	string sExitTime;
	//-----
	string sCalFeeTime;
	short  iRateType;
	long lParkedTime;
	float sFee;
	float sPaidAmt=0;
	float sOweAmt;
	float sGSTAmt;
	float sPrePaid;
	string sReceiptNo;
	int    iflag4Receipt;
	//------
	int iStatus;
	float sRedeemAmt;
	short iRedeemTime;
	string sRedeemNo;
	float sRebateAmt;
	float sRebateBalance;
	string sRebateDate;
	//------
	string sCHUDebitCode;
	float sTopupAmt;
	string uposbatchno;
	string feefrom;
	string lpn;
	//--------
	int iPartialseason;
	string dtValidTo;
	string dtValidFrom;
	//-----
	int iWholeDay;
	int iUseMultiCard;
	int iNeedSendIUtoEntry;
	string sRegisterCard;
	int iAttachedTransType;
	int bNoEntryRecord;
	//-------
	string sLPN[2];
	string entry_lpn;
	string video_location;
	string video1_location;
	string sRPLPN;
	//-----
	eProcessStatus giDeductionStatus;        // 0: init    1: waiting card   2: doing deduction  3: deduction succeed 4: deduction failed
	std::atomic<bool> gbPaid;
	std::atomic<bool> bPayByEZPay;
	string sTag;
};


struct  tProcess_Struct
{
	
	bool gbsavedtrans;
	std::atomic<bool> gbcarparkfull;
	long glNoofOfflineData;
	int giIsSeason;            //0: none season, 1: wholedayseason, 2: partialseason 
	int online_status;
	int offline_status;
	int giSystemOnline;
	bool sEnableReader;
	//------ for fee calculation 
	string gsLastPaidTrans;
	string gsLastCardNo;
	float gfLastCardBal;
	std::atomic<bool> gbLastPaidStatus;
	eUPOSStatus gbUPOSStatus;
	int giUPOSLoginCnt;
	//------
	std::string gsLastDebitFailTime;
	int giShowType;
	string gsDefaultIU; 
	string gsBroadCastIP;
	std::atomic<bool> gbLoopApresent;
	string gsTransID;
	bool gbLoopAIsOn;
	bool gbLoopBIsOn;
	bool gbLoopCIsOn;
	bool gbLorrySensorIsOn;
	int giSyncTimeCnt;
	bool gbloadedParam;
	bool gbloadedVehtype;
	bool gbloadedLEDMsg;
	bool gbloadedLEDExitMsg;
	bool gbloadedStnSetup;
	int gbInitParamFail;
	int giCardIsIn;
	int giLastHousekeepingDate;
	int giEntryDebit;
	long glLastSerialNo;
	int giBarrierContinueOpened;
//	bool gbwaitLoopA;
	std::atomic<bool> fbReadIUfromAnt;
	int fiLastCHUCmd;
	int giCardType;
	std::chrono::time_point<std::chrono::steady_clock> lastTransTime;
	std::mutex lastTransTimeMutex;

	std::string IdleMsg[2];
	std::mutex idleMsgMutex;
	std::string gsLastIUNo;
	std::mutex gsLastIUNoMutex;
	std::mutex gsLastPaidIUMutex;
	std::chrono::time_point<std::chrono::steady_clock> lastIUEntryTime;
	std::mutex lastIUEntryTimeMutex;
	bool gbBarrierOpened;

	void setIdleMsg(int index, const std::string& msg)
	{
		std::lock_guard<std::mutex> lock(idleMsgMutex);
		if (index >=0 && index < 2)
		{
			IdleMsg[index] = msg;
		}
	}

	std::string getIdleMsg(int index)
	{
		std::lock_guard<std::mutex> lock(idleMsgMutex);
		if (index >=0 && index < 2)
		{
			return IdleMsg[index];
		}
		return "";
	}

	void setLastIUNo(const std::string& msg)
	{
		std::lock_guard<std::mutex> lock(gsLastIUNoMutex);
		gsLastIUNo = msg;
	}

	void setLastPaidTrans(const std::string& msg)
	{
		std::lock_guard<std::mutex> lock(gsLastPaidIUMutex);
		gsLastPaidTrans = msg;
	}

	std::string getLastIUNo()
	{
		std::lock_guard<std::mutex> lock(gsLastIUNoMutex);
		return gsLastIUNo;
	}

	void setLastIUEntryTime(const std::chrono::time_point<std::chrono::steady_clock>& time)
	{
		std::lock_guard<std::mutex> lock(lastIUEntryTimeMutex);
		lastIUEntryTime = time;
	}

	std::chrono::time_point<std::chrono::steady_clock> getLastIUEntryTime()
	{
		std::lock_guard<std::mutex> lock(lastIUEntryTimeMutex);
		return lastIUEntryTime;
	}

	void setLastTransTime(const std::chrono::time_point<std::chrono::steady_clock>& time)
	{
		std::lock_guard<std::mutex> lock(lastTransTimeMutex);
		lastTransTime = time;
	}

	std::chrono::time_point<std::chrono::steady_clock> getLastTransTime()
	{
		std::lock_guard<std::mutex> lock(lastTransTimeMutex);
		return lastTransTime;
	}
};




struct tVType_Struct
{
	int iIUCode;
	int iType;
};



struct  tParas_Struct
{
	//----site Info
	int giGroupID;    
	int giSite;
	string gsCompany;			// DB Loaded: Company
	string gsZIP;				// *DB Loaded: ZIP, but not use
	string gsGSTNo;				// *DB Loaded: GSTNo, but not use
	string gsSite;				// *DB Loaded: giSite, but not use
	string gsAddress;			// *DB Loaded: gsAddress, but not use
	string gsTel;				// *DB Loaded: Tel, but not use
	string gsCentralDBName;
	string gsCentralDBServer;
	string gsLocalIP;
	int local_udpport; 
	int remote_udpport; 
	int giTicketSiteID;			// DB Loaded: TicketSiteID
	int giEPS;

	//-----comport 
	int giCommPortAntenna;			// DB Loaded: CommPortAntenna
	int giCommPortLCSC;				// DB Loaded: commportlcsc
	int giCommPortKDEReader;		// DB Loaded: CommPortReader
	int giCommPortUPOS;
	int giCommPortPrinter;			// DB Loaded: CommPortPrinter
	int giCommPortLED;				// DB Loaded: CommPortLED
	int giCommportLED401;			// DB Loaded: commportled401
	int giCommPortLED2;				// *DB Loaded: commportled2, but not use

	//--- Ant
	int giAntMaxRetry;				// *DB Loaded: AntMaxRetry, but hardcoded to 3
	int giAntMinOKTimes;			// DB Loaded: AntMinOKTimes
	bool gbAntiIURepetition;		// *DB Loaded: AntiIURepetition, but not use
	int giAntInqTO;					// DB Loaded: AntInqTO

	//--- LCSC 
	string gsLocalLCSC;
	string gsRemoteLCSC;
	string gsRemoteLCSCBack;
	string gsCSCRcdfFolder;
	string gsCSCRcdackFolder;
	string gsCPOID;					// DB Loaded: CPOID
	string gsCPID;					// DB Loaded: CPID
	string gscarparkcode;

	//--- LED
	int giLEDMaxChar;				// DB Loaded: LEDMaxChar
	
	//----LPR(ini)
	string lprip_front; 
	string lprip_rear; 
	int wait_lpr_notime; 
	string nexpa_dbserver;
	string nexpa_dbname; 
	int sid;	

	//---- CHU
	string gsCHUIP;						// DB Loaded: CHUIP
	int giCHUCnTO;						// DB Loaded: CHUCnTO
	int giCHUComTO;
	int giCHUComRetry;
	int giNoIURetry;					// *DB Loaded: NoIURetry, but not use
	int giNoCardRetry;
	int giCHUDebitRetry;
	int giNoIUWT;
	int giNoCardWT;
	int giDebitNakWT;
	int giCardProblemWT;
	int giDebitFailWT;
	int giCHUDebitWT;
	int giNoIUAutoDebitWT;
	int giMaxDiffIU;					// *DB Loaded: MaxDiffIU, but not use
	int giTryTimes4NE;					// *DB Loaded: TryTimes4NE, but not use
	eFullAction giFullAction;			// DB Loaded: FullAction

	//----special function
	int giHasMCycle;					// *DB Loaded: HasMCycle, but not use
	int giAllowMultiEntry; 
	int giMCAllowMultiEntry; 
	bool gbAutoDebitNoEntry;			// *DB Loaded: AutoDebitNoEntry, but not use
	int giIsHDBSite;					// *DB Loaded: IsHDBSite, but not use
	int giHasHolidayEve;				// DB Loaded: HasHolidayEve
	int giProcessReversedCMD;			// *DB Loaded: processreversedcmd, but not use
	bool gbHasRedemption;				// *DB Loaded: HasRedemption, but not use

	//-----I/O 
	int giLoopAHangTime;				// *DB Loaded: LoopAHangTime, but not use
	int giBarrierOpenTooLongTime;		// *DB Loaded: barrieropentoolongtime, but no use
	int giBitBarrierArmBroken;
	int gsBarrierPulse;
	bool gbLockBarrier;					// *DB Loaded: LockBarrier, but not use
	float gsShutterPulse;

	//----system info
	int giOperationTO;					// DB Loaded: OperationTO
	int giDataKeepDays;					// *DB Loaded: DataKeepDays, but not use
	string gsLogBackFolder;
	int giLogKeepDays;					// *DB Loaded: LogKeepDays, but not use
	int giMaxTransInterval;				// DB Loaded: MaxTransInterval
	int giMaxDebitDays;					// *DB Loaded: MaxDebitDays, but not use
	bool gbAlwaysTryOnline;
	int giMaxSendOfflineNo;				// *DB Loaded: MaxSendOfflineNo, but not use
	long glMaxLocalDBSize;
	string gsDBBackupFolder;
	string gsZoneEntries;

	//--- tariff 
	int giTariffFeeMode;				// DB Loaded: TariffFeeMode
	int giTariffGTMode;					// *DB Loaded: TariffGTmode, but not use
	int giFirstHourMode;				// DB Loaded: FirstHourMode
	int giFirstHour;					// DB Loaded: FirstHour
	int giPEAllowance;					// DB Loaded: PEAllowance
	int giHr2PEAllowance;				// DB Loaded: Hr2PEAllowance
	int giV3TransType;					// *DB Loaded: V3TransType, but not use
	int giV4TransType;					// *DB Loaded: V4TransType, but not use
	int giV5TransType;					// *DB Loaded: V5TransType, but not use

	//---- car park with carpark 
	int gihasinternal_link;
	int gihas_internal;
	string gsattached_dbname;
	string gsattached_dbserver;
	int giattachedexit_udpport;
	int giattachedexit_id;

	//----season 
	string gsAllowedHolderType;
	int giSeasonCharge;					
	bool gbSeasonAsNormal;
	int giSeasonPayStart;
	int giSeasonAllowance;
	int giShowSeasonExpireDays;			// *DB Loaded: ShowSeasonExpireDays, but not use
	int giShowExpiredTime;				// *DB Loaded: ShowExpiredTime, but not use
	bool gbseasonOnly;

	//----- M/C 
	float gfMotorRate;
	int giMCEntryGraceTime;
	int giMCyclePerDay;					// DB Loaded: MCyclePerDay
	int giHasThreeWheelMC;				// *DB Loaded: hasthreewheelmc, but not use
	int giMCControlAction;

	//----- Others
	float gfGSTRate;					// DB Loaded: GSTRate
	std::string gsHdRec;				// DB Loaded: HdRec
	std::string gsHdTk;					// DB Loaded: HdTk
	int giNeedCard4Complimentary;		// DB Loaded: needcard4complimentary
	int giExitTicketRedemption;			// DB Loaded: ExitTicketRedemption
};

struct  tMsg_Struct
{
	// Message Entry - index 0 : LED, index 1 : LCD
	std::string Msg_AltDefaultLED[2];
	std::string Msg_AltDefaultLED2[2];
	std::string Msg_AltDefaultLED3[2];
	std::string Msg_AltDefaultLED4[2];

	std::string Msg_authorizedvehicle[2];
	std::string Msg_CardReadingError[2];
	std::string Msg_CardTaken[2];
	std::string Msg_CarFullLED[2];
	std::string Msg_CarParkFull2LED[2];
	std::string Msg_DBError[2];
	std::string Msg_DefaultIU[2];
	std::string Msg_DefaultLED[2];
	std::string Msg_DefaultLED2[2];
	std::string Msg_DefaultMsg2LED[2];
	std::string Msg_DefaultMsgLED[2];
	std::string Msg_EenhancedMCParking[2];
	std::string Msg_ESeasonWithinAllowance[2];
	std::string Msg_ESPT3Parking[2];
	std::string Msg_EVIPHolderParking[2];
	std::string Msg_ExpiringSeason[2];
	std::string Msg_FullLED[2];
	std::string Msg_Idle[2];
	std::string Msg_InsertCashcard[2];
	std::string Msg_IUProblem[2];
	std::string Msg_LockStation[2];
	std::string Msg_LoopA[2];
	std::string Msg_LoopAFull[2];
	std::string Msg_LorryFullLED[2];
	std::string Msg_LotAdjustmentMsg[2];
	std::string Msg_LowBal[2];
	std::string Msg_NoIU[2];
	std::string Msg_NoNightParking2LED[2];
	std::string Msg_Offline[2];
	std::string Msg_PrinterError[2];
	std::string Msg_PrintingReceipt[2];
	std::string Msg_Processing[2];
	std::string Msg_ReaderCommError[2];
	std::string Msg_ReaderError[2];
	std::string Msg_SameLastIU[2];
	std::string Msg_ScanEntryTicket[2];
	std::string Msg_ScanValTicket[2];
	std::string Msg_SeasonAsHourly[2];
	std::string Msg_SeasonBlocked[2];
	std::string Msg_SeasonExpired[2];
	std::string Msg_SeasonInvalid[2];
	std::string Msg_SeasonMultiFound[2];
	std::string Msg_SeasonNotFound[2];
	std::string Msg_SeasonNotStart[2];
	std::string Msg_SeasonTerminated[2];
	std::string Msg_SeasonNotValid[2];
	std::string Msg_SeasonOnly[2];
	std::string Msg_SeasonPassback[2];
	
	std::string Msg_SystemError[2];
	std::string Msg_ValidSeason[2];
	std::string Msg_VVIP[2];
	std::string Msg_WholeDayParking[2];
	std::string Msg_WithIU[2];
	std::string Msg_E1enhancedMCParking[2];
	std::string MsgBlackList[2];
};

struct tExitMsg_struct
{
	// Message Exit - index 0 : LED, index 1 : LCD
	std::string MsgExit_BlackList[2];
	std::string MsgExit_CardError[2];
	std::string MsgExit_CardIn[2];
	std::string MsgExit_Comp2Val[2];
	std::string MsgExit_CompExpired[2];
	std::string MsgExit_Complimentary[2];
	std::string MsgExit_DebitFail[2];
	std::string MsgExit_DebitNak[2];
	std::string MsgExit_EntryDebit[2];
	std::string MsgExit_ExpCard[2];
	std::string MsgExit_FleetCard[2];
	std::string MsgExit_FreeParking[2];
	std::string MsgExit_GracePeriod[2];
	std::string MsgExit_InvalidTicket[2];
	std::string MsgExit_WrongTicket[2];
	std::string MsgExit_RedemptionExpired[2];
	std::string MsgExit_IUProblem[2];
	std::string MsgExit_MasterSeason[2];
	std::string MsgExit_NoEntry[2];
	std::string MsgExit_PrinterError[2];
	std::string MsgExit_RedemptionTicket[2];
	std::string MsgExit_SeasonBlocked[2];
	std::string MsgExit_SeasonExpired[2];
	std::string MsgExit_SeasonInvalid[2];
	std::string MsgExit_SeasonNotStart[2];
	std::string MsgExit_SeasonOnly[2];
	std::string MsgExit_SeasonPassback[2];
	std::string MsgExit_SeasonRegNoIU[2];
	std::string MsgExit_SeasonRegOK[2];
	std::string MsgExit_SeasonTerminated[2];
	std::string MsgExit_SystemError[2];
	std::string MsgExit_TakeCard[2];
	std::string MsgExit_TicketExpired[2];
	std::string MsgExit_TicketNotFound[2];
	std::string MsgExit_UsedTicket[2];
	std::string MsgExit_WrongCard[2];
	std::string MsgExit_XCardAgain[2];
	std::string MsgExit_XCardTaken[2];
	std::string MsgExit_XDefaultIU[2];
	std::string MsgExit_XDefaultLED[2];
	std::string MsgExit_XDefaultLED2[2];
	std::string MsgExit_XExpiringSeason[2];
	std::string MsgExit_XIdle[2];
	std::string MsgExit_XLoopA[2];
	std::string MsgExit_XLowBal[2];
	std::string MsgExit_XNoCard[2];
	std::string MsgExit_XNoCHU[2];
	std::string MsgExit_XNoIU[2];
	std::string MsgExit_XSameLastIU[2];
	std::string MsgExit_XSeasonWithinAllowance[2];
	std::string MsgExit_XValidSeason[2];
	std::string MsgExit_X1enhancedMCParking[2];
	std::string MsgExit_XenhancedMCParking[2];
	std::string MsgExit_XSPT3Parking[2];
	std::string MsgExit_XVIPHolderParking[2];
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


struct tTR_struc {
	std::string gsTR0;
	std::string gsTR1;
	int giTRF;
	int giTRA;
};

struct XTariff_Struct
{
	std::string day_index;
	std::string autocharge[5];
	std::string fee[5];
	std::string time[5];
};


