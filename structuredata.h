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
	bool sEnableReader;
	string sLPN[2];
	int iVehcileType;
//	string sGreeting;
//	string sRPLPN;
//	string sPaidtime;
	bool gbEntryOK;
	int giShowType;
	string gsTransID;
	
};



struct  tExitTrans_Struct
{
	string xsid;
	string sExitTime;
	string sIUNo;
	string sCardNo;
	int iTransType;
	long lParkedTime;
	float sFee;
	float sPaidAmt=0;
	string sReceiptNo;
	int iStatus;
	float sRedeemAmt;
	short iRedeemTime;
	string sRedeemNo;
	float sGSTAmt;
	string sCHUDebitCode;
	int iCardType;
	float sTopupAmt;
	string uposbatchno;
	string feedrom;
	string lpn;
	
	short  iRateType;
	string sEntryTime;
	string sPwd;
	string sTag;
	float sPrePaid;
	int iWholeDay;
	string sWDFrom;
	string sWDTo;
	CE_Time dtValidTo;
	CE_Time dtValidFrom;
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
	
	bool gbsavedtrans;
	atomic<bool> gbcarparkfull;
	long glNoofOfflineData;
	string fsLastCardNo;
	int is_season;
	int online_status;
	int offline_status;
	int giSystemOnline;
	string fsLastPaidIU;
	string fsLastReadCard;
	string gsDefaultIU; 
	string gsBroadCastIP;
	atomic<bool> gbLoopApresent;
	bool gbLoopAIsOn;
	bool gbLoopBIsOn;
	bool gbLoopCIsOn;
	bool gbLorrySensorIsOn;
	int giSyncTimeCnt;
	bool gbloadedParam;
	bool gbloadedVehtype;
	bool gbloadedLEDMsg;
	bool gbloadedStnSetup;
	int gbInitParamFail;
	int giCardIsIn;
	int giLastHousekeepingDate;
//	bool gbwaitLoopA;

	std::string IdleMsg[2];
	std::mutex idleMsgMutex;
	std::string fsLastIUNo;
	std::mutex fsLastIUNoMutex;
	std::chrono::time_point<std::chrono::steady_clock> lastIUEntryTime;
	std::mutex lastIUEntryTimeMutex;

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
		std::lock_guard<std::mutex> lock(fsLastIUNoMutex);
		fsLastIUNo = msg;
	}

	std::string getLastIUNo()
	{
		std::lock_guard<std::mutex> lock(fsLastIUNoMutex);
		return fsLastIUNo;
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
	string gsCompany;
	string gsZIP;
	string gsGSTNo;
	string gsAddress;
	string gsTel;
	string gsCentralDBName;
	string gsCentralDBServer;
	string gsLocalIP;
	int local_udpport; 
	int remote_udpport; 
	int giTicketSiteID;
	int giEPS;

	//-----comport 
	int giCommPortAntenna;
	int giCommPortLCSC;
	int giCommPortKDEReader;
	int giCommPortUPOS;
	int giCommPortPrinter;
	int giCommPortLED;
	int giCommportLED401;
	int giCommPortLED2;

	//--- Ant
	int giAntMaxRetry;
	int giAntMinOKTimes;
	bool gbAntiIURepetition;
	int giAntInqTO;

	//--- LCSC 
	string gsLocalLCSC;
	string gsRemoteLCSC;
	string gsRemoteLCSCBack;
	string gsCSCRcdfFolder;
	string gsCSCRcdackFolder;
	string gsCPOID;
	string gsCPID;
	string gscarparkcode;

	//--- LED
	int giLEDMaxChar;
	
	//----LPR(ini)
	string lprip_front; 
	string lprip_rear; 
	int wait_lpr_notime; 
	string nexpa_dbserver;
	string nexpa_dbname; 
	int sid;	

	//---- CHU
	string gsCHUIP;
	int giCHUCnTO;
	int giCHUComTO;
	int giCHUComRetry;
	int giNoIURetry;
	int giNoCardRetry;
	int giCHUDebitRetry;
	int giNoIUWT;
	int giNoCardWT;
	int giDebitNakWT;
	int giCardProblemWT;
	int giDebitFailWT;
	int giCHUDebitWT;
	int giNoIUAutoDebitWT;
	int giMaxDiffIU;
	int giTryTimes4NE;
	eFullAction giFullAction;

	//----special function
	int giHasMCycle;
	int giAllowMultiEntry; 
	int giMCAllowMultiEntry; 
	bool gbAutoDebitNoEntry;
	int giIsHDBSite;
	bool gbseasonOnly;
	int giHasHolidayEve;
	int giProcessReversedCMD;
	bool gbHasRedemption;

	//-----I/O 
	int giLoopAHangTime;
	int giBarrierOpenTooLongTime;
	int giBitBarrierArmBroken;
	int gsBarrierPulse;
	bool gbLockBarrier;
	float gsShutterPulse;

	//----system info
	int giOperationTO;
	int giDataKeepDays;
	string gsLogBackFolder;
	int giLogKeepDays;
	int giMaxTransInterval;
	int giMaxDebitDays;
	bool gbAlwaysTryOnline;
	int giMaxSendOfflineNo;
	long glMaxLocalDBSize;
	string gsDBBackupFolder;

	//--- tariff 
	int giTariffFeeMode;
	int giTariffGTMode;
	int giFirstHourMode;
	int giFirstHour;
	int giPEAllowance;
	int giHr2PEAllowance;
	int giV3TransType;
	int giV4TransType;
	int giV5TransType;

	//---- car park with carpark 
	int hasinternal_link;
	int has_internal;
	string attached_dbname;
	string attached_dbserver;
	int attachedexit_udpport;
	int attachedexit_id;

	//----season 
	string gsAllowedHolderType;
	int giSeasonCharge;
	bool gbSeasonAsNormal;
	int giSeasonPayStart;
	int giSeasonAllowance;
	int giShowSeasonExpireDays;
	int giShowExpiredTime;

	//----- M/C 
	float gsMotorRate;
	int giMCEntryGraceTime;
	int giMCyclePerDay;
	int giHasThreeWheelMC;
	int giMCControlAction;	

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

