

#ifndef STRUCTUREDATA_H
#define STRUCTUREDATA_H

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

struct  tStation_Struct
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
	string sPwd;      //password, for ticket
	string sTag;
	string sCardNo;
	float sFee=0;
	float sPaidAmt=0;
	float sGSTAmt;
	string sReceiptNo;
	int iCardType=0;
	string sCHUDebitCode; //Entry debit from CHU
	string sCHUDebitTime;
	float sOweAmt=0;
	bool sEnableReader;
	string sLPN[2];
	int iVehcileType;
	string sGreeting;
	string sRPLPN;
	string sPaidtime;


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
	int m_enable_read_antenna_timer;//2019.07.15 QC
	int AntTOFlag;//2019.07.15 QC
	int m_en_AntTO_Tmr;
	int m_AntTOCnt;
	bool wrNAKlogFlag;
	int CardTOFlag;//2019.07.15 QC
	int canUseCard;//2019.07.15 QC
	int rotate;
	int combi_state;
	int iu_readed;
	int waitcard_readed;
	int bar_open=0;
	int bar_open_ok=0;
	int save_trans=0;
	int processflag=0;
	bool gbcarparkfull;
	int giStationID;//2019.07.29 QC
	long glNoofOfflineData;//2019.07.30 QC, counter for offline trans
	string fsLastIUNo;
	string CardNo;
	int readksm1_state;
	int readflag;
	int is_season;
	int online_status;
	int offline_status;
	int Balance=0;
	int trs_fee=0;
	string firstcan;
	int first_balance;
	int process_debited=0;
	int fiLastCHUCmd;
	int fiReadIUfromAnt;
	int fiCHUStatus;
	int fiNoIURetried=0;
	string fsLastPaidIU;
	string fsLastReadCard;
	float fsCardBal;
	int fiCardType;
	int fiCHUDebitRetried;
	int fiIsDoingDebitOk;
	int ficepasdebit;
	string sCHUDebitTime;
};




class tVType
{

public:
	string iIUCode;
	int iType;

};



struct  tParas_Struct
{
	int giCommPortAntenna;
	int giCommPortReader;
	int giCommPortPrinter;

	int giCommPortLCSC;
	string gsLocalLCSC;
	string gsRemoteLCSC;
	string gsRemoteLCSCBack;
	string gsCSCRcdfFolder;
	string gsCSCRcdackFolder;
	string gsCPOID;
	string gsCPID;
	int giUPSWithDryContact;
	int giCommPortCPT;
	int giCommPortProximity;
	int giCommPortLED;
	int giCommPortLCD;
	int giCommPortLED2;
	int giCommportNCSC;
	int giCommportOEM75;
	int giCommportEltraReader;
	int giProximityReader;
	int giBitLoopA;
	int giBitLoopB;
	int giBitLoopC;
	int giBitMOpenBarrier;
	int giBitButton;
	int giBitTicket;
	int giBitResetReader;
	int giBitUPS;
	int giBitOpenBarrier;
	int giBitCloseBarrier;
	int giBitCarparkFull;
	int giBitLButton;
	int giBitLReader;
	int giBitEnableSeason;
	int giBitOpenShutter;
	int giBitCloseShutter;
	string gsSite;
	string gsCompany;
	string gsZIP;
	string gsGSTNo;
	string gsAddress;
	string gsTel;
	string gsDBServer;
	string gsCentralDBName;
	string gsCHUIP;
	bool gbEnableVoice;
	int giVoice;
	int giHasMCycle;
	float gsMotorRate;
	int giMaxMotorTimes;
	int giMCEntryGraceTime;
	int giHasTicket;
	int giHasThreeWheelMC;
	int giTicketSiteID;
	int giUseTicketIfOffline;
	int giCPTMaxRetry;
	int giCPTInqTO;
	int giCPTTestInterval;
	string gsLocalSettle;
	string gsRemoteSettle;
	string gsSettleFileHeader;
	int giSettleFileHeaderLength;
	string gsNCSCLocalSettle;
	string gsNCSCRemoteSettle;
	string gsNCSCRemoteBackup;
	string gsNCSCSettleFileHeader;
	int giNCSCSettleFileHeaderLength;
	string gsNCSCRemoteCdfFolder;
	string gsNCSCSettleFileHeader2;
	string gsNCSCRemoteCepas2CollFolder;
	int giBatchFull;
	bool mbOvernightCharge;
	string msOvernightTime;
	int giTariffFeeMode;
	int giTariffGTMode;
	int giFirstHourMode;
	int giFirstHour;
	int giPEAllowance;
	int giHr2PEAllowance;
	int giDataKeepDays;
	int giMaxDebitDays;
	int giUseLastNoPay;
	int giProcessReversedCMD;
	int giExitGraceTime;
	float gsLostTicketCharge;
	float gsLowBalValue;
	bool gbAllowLowBalIn;
	float gsBarrierPulse;
	float gsShutterPulse;
	int giSeasonCharge;
	bool gbSeasonAsNormal;
	int giSeasonPayStart;
	int giSeasonAllowance;
	eCheckSeason giCheckSeason;
	int giShowSeasonExpireDays;
	int giShowExpiredTime;
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
	int giEntryIUMode;
	int giMaxDiffIU;
	int giAllowEventWhenWait4GW;
	int giAntMaxRetry;
	int giAntMinOKTimes;
	int giAntInqTO;
	bool gbAntiIURepetition;
	string giVideoType;
	string gsVideoDefault;
	string gsVideoInsertCard;
	string gsVideoTakeCard;
	string gsVideoWait;
	string gsVideoOK;
	string gsHdTk;
	string gsHdRec;
	int giPrinterType;
	int giPrintMode;
	int giCommportLED401;
	int giAllowMultiEntry; //not in current central DB
	int giMCAllowMultiEntry; //not in current central DB
	int giHasHolidayEve;
	int giIsHDBSite;
	string gsAllowedHolderType;
	int giDefaultLEDMode;
	int giDefaultLEDSubmode;
	int giLEDMode;
	int giLEDSubmode;
	int giLEDType;
	int giDefaultLEDMode2;
	int giDefaultLEDSubmode2;
	int giDefaultLEDSeconds2;
	int giLEDMinWaitTime;
	int giLEDMaxChar;
	int giDefaultLEDSeconds;
	int giLEDSeconds;
	bool gbSynTimeHourly;
	bool gbAlwaysTryOnline;
	bool gbAutoDebitNoEntry;
	int giTryTimes4NE;
	int giHoldTicketTime;
	int giLoopAHangTime;
	int giOperationTO;
	int giKSMOperationTO;
	bool gbLoopAFirst;
	int giCloseBarrierIfTO;
	int giPrintReceiptTO;
	int giTransactionTO;
	int giRefreshTime;
	int giUploadBLTO;
	int giLoopC2OpenBarWT;
	eFullAction giFullAction;
	bool gbHasRedemption;
	float msOvernightAmt;
	int giHasLorry;
	int giHasContainer;
	int giLorryRelayTime;
	int giMaxTransInterval;
	int giMOBRelayTimes;
	string gsLogBackFolder;
	int giLogKeepDays;
	string gsDBBackupFolder;
	int giUpdateAfterExit;
	int giLoopC2MOBTime;
	int giEntryWithScanner;
	int giOfflineCheckAllCardSeason;
	int giShowScreenTime;
	int giBitLightPE;
	int giNoTicketTaken;
	int giUseCommIO;
	int giExitTicketRedemption;
	int giMaxSendOfflineNo;
	long glMaxLocalDBSize;
	int giDBNetLib;
	int giSettleFileKeepMonths;
	int giWithGSTField;
	float gsGSTRate;
	int giGWQLength;
	int giMaxLogErrorTimes;
	int giMCyclePerDay;
	int giNeedCard4Complimentary;
	int giV3TransType;
	int giV4TransType;
	int giV5TransType;
	int giUseMagCard;
	int giBarrierOpenTooLongTime;
	int giBitBarrierArmBroken;
	long glMaxRunningHours;
	string gsAutoRestartPeriod;
	int giMinAvailRAM;
	int giMinFreeDiskSpace;
	bool gbShowFreeParking;
	int giUPSTripAction;
	int giTime2WaitB4ConsiderTripped;
	int giMCControlAction;
	int giCommportUPS;
	int giTime2WaitB4ShutdownUPS;
	int giUPSInquiryInterval;
	float giMinRedeemAmt;
	int giMinRedeemTime;
	bool gbLockBarrier;
	string Licensekey;
	int gcommportiu;
	int gwaitiunotime;
	int diostation_dooropen;
	int diobarrier_dooropen;
	int dio_barrier;
	int showtime;
	int hasinternal_link;
	string internaldefault_msg;
	string internalnonight_msg1;
	string internalnonight_msg2;
	int is_ledblinking;
	int display_type;
	int led_showtime;
	string lprip_front;
	string lprip_rear;
	int wait_lpr_notime;
	string nexpa_dbserver;
	string nexpa_dbname;
	int sid;
	int has_internal;
	string attached_dbname;
	string attached_dbserver;
	string dbname;
	string dbserver;
	int attachedexit_udpport;
	int attachedexit_id;
	int hdblost_adj_timer;
	int lastiu_timeout;
	int last_serial_no;
	int last_redem_no;
	int taxi_control;
	int loading_bay;
	int local_udpport;
	int remote_udpport;
	int entry_debit;
	int not_allow_hourly;
	int force_partial_eps;
	int eps;
	int interval_loopc_iu;
	string tgd_server;
	int cepas_enable;

};

struct  tMsg_Struct
{
	string Msg_cancelticket;
	string Msg_carderror;
	string Msg_cardin;
	string Msg_cardpaid;
	string Msg_cardtaken;
	string Msg_compexpired;
	string Msg_expcard;
	string Msg_graceperiod;
	string Msg_idle;
	string Msg_invalidticket;
	string Msg_lockstation;
	string Msg_loopa;
	string Msg_loopafull;
	string Msg_lowbal;
	string Msg_noentry;
	string Msg_noentry1;
	string Msg_offline;
	string Msg_printererror;
	string Msg_seasonblocked;
	string Msg_seasonexpired;
	string Msg_seasoninvalid;
	string Msg_seasonlost;
	string Msg_seasonnotstart;
	string Msg_seasonpassback;
	string Msg_seasonterminated;
	string Msg_seasonwrongtype;
	string Msg_systemerror;
	string Msg_takecard;
	string Msg_taketicket;
	string Msg_ticketbycashier;
	string Msg_ticketexpired;
	string Msg_ticketlocked;
	string Msg_usedticket;
	string Msg_validseason;
	string Msg_wrongcard;
	string Msg_wrongticket;
	string Msg_xcardagain;
	string Msg_xloopa;
	string Msg_xlowbal;
	string Msg_xvalidseason;
	string Msg_noiu;
	string Msg_fleetcard;
	string Msg_nocard;
	string Msg_xnoiu;
	string Msg_xnocard;
	string Msg_complimentary;
	string Msg_blacklist;
	string Msg_nochu;
	string Msg_redemptionticket;
	string Msg_ticketnotfound;
	string Msg_defaultled;
	string Msg_redeemfail;
	string Msg_redeemok;
	string Msg_redemption;
	string Msg_getreceipt;
	string Msg_norecharge;
	string Msg_xnochu;
	string Msg_defaultiu;
	string Msg_xdefaultiu;
	string Msg_withiu;
	string Msg_freeparking;
	string Msg_iuproblem;
	string Msg_xidle;
	string Msg_apsidle;
	string Msg_xdefaultled;
	string Msg_seasonregnoiu;
	string Msg_seasonregok;
	string Msg_debitnak;
	string Msg_fullled;
	string Msg_debitfail;
	string Msg_defaultled2;
	string Msg_flashled;
	string Msg_expiringseason;
	string Msg_xcardtaken;
	string Msg_xdefaultled2;
	string Msg_xexpiringseason;
	string Msg_redemptionexpired;
	string Msg_seasonashourly;
	string Msg_seasonwithinallownace;
	string Msg_comp2val;
	string Msg_compvalfail;
	string Msg_comvalok;
	string Msg_scanentryticket;
	string Msg_scanvalticket;
	string Msg_insertatm;
	string Msg_takeatm;
	string Msg_insertcashcard;
	string Msg_selecttopupvalue;
	string Msg_keyinlpn;
	string Msg_processing;
	string Msg_essok;
	string Msg_keyinpwd;
	string Msg_rekeyinpwd;
	string Msg_maxpinretried;
	string Msg_atmdebitfail;
	string Msg_topupfail;
	string Msg_vmtimeout;
	string Msg_seasonnotfound;
	string Msg_seasonmultifound;
	string Msg_seasonnotvalid;
	string Msg_setperiod;
	string Msg_atmdebitok;
	string Msg_seasonrenewok;
	string Msg_seasonrenewfail;
	string Msg_dispensecardfail;
	string Msg_dispensecardok;
	string Msg_dispensingcard;
	string Msg_cardsoldout;
	string Msg_dispensererror;
	string Msg_vmcommerror;
	string Msg_vmhostproblem;
	string Msg_vmlineproblem;
	string Msg_vmerror;
	string Msg_readererror;
	string Msg_cardreadingerror;
	string Msg_vmlogon;
	string Msg_vmlogoncard;
	string Msg_esscancel;
	string Msg_wrongatm;
	string Msg_topupok;
	string Msg_dberror;
	string Msg_carddebitfail;
	string Msg_printingreceipt;
	string Msg_takereceipt;
	string Msg_carddebitdb;
	string Msg_topupexceedmax;
	string Msg_atmcancelpin;
	string Msg_readercommerror;
	string Msg_vvip;
	string Msg_masterseason;
	string Msg_entrydebit;
	string Msg_ticketnotstart;
	string Msg_card4complimentary;
	string Msg_seasononly;
	string Msg_wholedayparking;
	string Msg_cexpiringseason;
	string Msg_cfull_led;
	string Msg_cidle;
	string Msg_c_lockstation;
	string Msg_c_loopa;
	string Msg_c_loopafull;
	string Msg_c_seasonexpired;
	string Msg_c_seasonwithinallownace;
	string Msg_cx_expringseason;
	string Msg_cx_idle;
	string Msg_cx_loopa;
	string Msg_c_defaultled;
	string Msg_c_defaultled2;
	string Msg_cx_defaultled;
	string Msg_cx_defaultled2;
	string Msg_c_apsidle;
	string Msg_c_atmcancelpin;
	string Msg_c_atmdebitfail;
	string Msg_c_atmdebitok;
	string Msg_c_blacklist;
	string Msg_c_cancelledticket;
	string Msg_c_card4complimentary;
	string Msg_c_carddebitdb;
	string Msg_c_carddebitfail;
	string Msg_c_carderror;
	string Msg_c_cardin;
	string Msg_c_cardpaid;
	string Msg_c_cardreadingerror;
	string Msg_c_cardsoldout;
	string Msg_c_cardtaken;
	string Msg_c_comp2val;
	string Msg_c_compexpired;
	string Msg_c_complimentary;
	string Msg_c_compvalfail;
	string Msg_c_compvalok;
	string Msg_c_dberror;
	string Msg_c_debitfail;
	string Msg_c_debitnak;
	string Msg_c_defaultiu;
	string Msg_c_dispensecardfail;
	string Msg_c_dispensecardok;
	string Msg_c_dispensererror;
	string Msg_c_dispensingcard;
	string Msg_c_entrydebit;
	string Msg_c_esscancel;
	string Msg_c_essok;
	string Msg_c_expcard;
	string Msg_c_flashled;
	string Msg_c_fleetcard;
	string Msg_c_freeparking;
	string Msg_c_getreceipt;
	string Msg_c_graceperiod;
	string Msg_c_insertatm;
	string Msg_c_insertcashcard;
	string Msg_c_invalidticket;
	string Msg_c_iuproblem;
	string Msg_c_keyinlpn;
	string Msg_c_keyinpwd;
	string Msg_c_lowbal;
	string Msg_c_masterseason;
	string Msg_c_maxpinretried;
	string Msg_c_nocard;
	string Msg_c_nochu;
	string Msg_c_noentry;
	string Msg_c_noentry1;
	string Msg_c_noiu;
	string Msg_c_norecharge;
	string Msg_c_offline;
	string Msg_c_printererror;
	string Msg_c_printingreceipt;
	string Msg_c_processing;
	string Msg_c_readercommerror;
	string Msg_c_readererror;
	string Msg_c_redeemfail;
	string Msg_c_redeemok;
	string Msg_c_redemption;
	string Msg_c_redemptionexpired;
	string Msg_c_redemptionticket;
	string Msg_c_rekeyinpwd;
	string Msg_c_scanentryticket;
	string Msg_c_scanvalticket;
	string Msg_c_seasonashourly;
	string Msg_c_seasonblocked;
	string Msg_c_seasoninvalid;
	string Msg_c_seasonlost;
	string Msg_c_seasonmultifound;
	string Msg_c_seasonnotfound;
	string Msg_c_seasonnotstart;
	string Msg_c_seasonnotvalid;
	string Msg_c_seasononly;
	string Msg_c_seasonpassback;
	string Msg_c_seasonregiu;
	string Msg_c_seasonregok;
	string Msg_c_seasonrenewfail;
	string Msg_c_seasonrenewok;
	string Msg_c_seasonterminated;
	string Msg_c_seasonwrongtype;
	string Msg_c_selecttopupvalue;
	string Msg_c_setperiod;
	string Msg_c_systemerror;
	string Msg_c_takeatm;
	string Msg_c_takecard;
	string Msg_c_takereceipt;
	string Msg_c_taketicket;
	string Msg_c_ticketbycashier;
	string Msg_c_ticketexpired;
	string Msg_c_ticketlocked;
	string Msg_c_ticketnotfound;
	string Msg_c_topupexceedmax;
	string Msg_c_touupfail;
	string Msg_c_topupok;
	string Msg_c_usedticket;
	string Msg_c_validseason;
	string Msg_c_vmcommerror;
	string Msg_c_vmerror;
	string Msg_c_vmhostproblem;
	string Msg_c_vmlineproblem;
	string Msg_c_vmlogon;
	string Msg_c_vmlogoncard;
	string Msg_c_vmtimeout;
	string Msg_c_vvip;
	string Msg_c_wholedayparking;
	string Msg_c_withiu;
	string Msg_c_wrongatm;
	string Msg_c_wrongcard;
	string Msg_c_wrongticket;
	string Msg_cx_cardagain;
	string Msg_cx_cardtaken;
	string Msg_cx_defaultiu;
	string Msg_cx_lowbal;
	string Msg_cx_nocard;
	string Msg_cx_nochu;
	string Msg_cx_noiu;
	string Msg_cx_validseason;
	string Msg_altdefaultled;
	string Msg_altdefaultled2;
	string Msg_authorizedvehicle;
	string Msg_carfull_led;
	string Msg_carparkfull2led;
	string Msg_ce_seasonwithinallowance;
	string Msg_c_samelastiu;
	string Msg_cx_samelastiu;
	string Msg_cx_seasonwithinallowance;
	string Msg_defaultmsg2led;
	string Msg_defaultmsgled;
	string Msg_eseasonwithinallowance;
	string Msg_espt3parking;
	string Msg_evipholderparking;
	string Msg_lorryfull_led;
	string Msg_lotadjustmentmsg;
	string Msg_nonoghtparking2led;
	string Msg_samelastiu;
	string Msg_x_samelastiu;
	string Msg_x_seasonwithinallowance;
	string Msg_x_spt3parking;
	string Msg_x_vipholderparking;
	string Msg_altdefaultled3;
	string Msg_altdefaultled4;
	string Msg_e1_enhancedmcparking;
	string Msg_e_enhancedmcparking;
	string Msg_x1_enhancedmcparking;
	string Msg_x_enhancedmcparking;
};

struct vehicle_struct {

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



#endif





