#pragma once

#include <ctime>
#include <stdio.h>
#include <string>
#include <sstream>
#include <iostream>
#include <list>

#include <math.h>
#include "structuredata.h"
#include "odbc.h"
#include "udp.h"


//using namespace std;

typedef enum
{
    iCentralFail = -2,
    iLocalFail = -1,
    iDBSuccess = 0,
    iNoData = 1,     //no relevant data such as rate etc.
    iUpdateFail = 2,
    iNoentry = 3,    //no entry record
    iOpen = 5,
    iClose = 6,
    iCentralSuccess = 7,
    iLocalSuccess = 8

}DBError;


class db {
public:
    static db* getInstance();
    int connectlocaldb(string connectstr,int LocalSQLTimeOut,int SP_SQLTimeOut,float mPingTimeOut);
    int connectcentraldb(string connectStr,string connectIP,int CentralSQLTimeOut, int SP_SQLTimeOut,float mPingTimeOut);
    virtual ~db();

    int sp_isvalidseason(const std::string & sSeasonNo,
                      BYTE  iInOut,unsigned int iZoneID,std::string  &sSerialNo, short int &iRateType,
                      float &sFee, float &sAdminFee, float &sAppFee,
                      short int &iExpireDays, short int &iRedeemTime, float &sRedeemAmt,
                      std::string &AllowedHolderType,
                      std::string &dtValidTo,
                      std::string &dtValidFrom);

	int local_isvalidseason(string L_sSeasonNo,unsigned int iZoneID);

	int isvalidseason(string m_sSeasonNo,BYTE iInOut, unsigned int iZoneID);
    void synccentraltime ();
    int downloadseason();
    int writeseason2local(tseason_struct& v);
    int downloadvehicletype();
    int writevehicletype2local(string iucode,string iutype);
    int downloadledmessage();
    int writeledmessage2local(string m_id,string m_body, string m_status);
    int downloadparameter();
    int writeparameter2local(string name,string value);
    int downloadstationsetup();
    int writestationsetup2local(tstation_struct& v);
    int downloadTR();
    int writetr2local(int tr_type, int line_no, int enabled, std::string line_text, std::string line_var, int line_font, int line_align);
    int downloadtariffsetup(int iGrpID = 0, int iSiteID = 1, int iCheckStatus = 0);
    int writetariffsetup2local(tariff_struct& tariff);
    int downloadtarifftypeinfo();
    int writetarifftypeinfo2local(tariff_type_info_struct& tariff_type);
    int downloadxtariff(int iGrpID, int iSiteID, int iCheckStatus = 0);
    int writextariff2local(x_tariff_struct& x_tariff);
    int downloadholidaymst(int iCheckStatus = 0);
    int writeholidaymst2local(std::string holiday_date, std::string descrip);
    int download3tariffinfo();
    int write3tariffinfo2local(tariff_info_struct& tariff_info);
    int downloadratefreeinfo(int iCheckStatus = 0);
    int writeratefreeinfo2local(rate_free_info_struct& rate_free_info);
    int downloadspecialdaymst(int iCheckStatus = 0);
    int writespecialday2local(std::string special_date, std::string rate_type, std::string day_code);
	int downloadratetypeinfo(int iCheckStatus = 0);
    int writeratetypeinfo2local(rate_type_info_struct rate_type_info);
    int downloadratemaxinfo(int iCheckStatus = 0);
    int writeratemaxinfo2local(rate_max_info_struct rate_max_info);
    int WriteTariff2RAM(tariff_struct t);
    int GetDayType(CE_Time curr_date);
    int GetDayTypeNoPE(CE_Time curr_date);
    int GetDayTypeWithHE(CE_Time curr_date);
    float HasPaidWithinPeriod(string sTimeFrom, string sTimeTo);
    float RoundIt(float val, int giTariffFeeMode);
    float CalFeeRAM2GR(string eTime, string payTime,int iTransType, bool bNoGT = false);
    float CalFeeRAM2G(string eTime, string payTime,int iTransType, bool bNoGT = false);
    int GetXTariff(int &iAutoDebit, float &sAmt, int iVType = 0);
    string CalParkedTime(long lpt);

    DBError insertentrytrans(tEntryTrans_Struct& tEntry);
	DBError insertexittrans(tExitTrans_Struct& tExit);
    DBError updatemovementtrans(tExitTrans_Struct& tExit);
    DBError DeleteBeforeInsertMT(tExitTrans_Struct& tExit); 
    DBError insert2movementtrans(tExitTrans_Struct& tExit); 
	DBError insertbroadcasttrans(string sid,string iu_No,string cardno = "",string paidamt = "0.00",string itype = "1");
    DBError UpdateLocalEntry(string IUTkNo);
	DBError loadmessage();
    DBError loadExitmessage();
	DBError loadParam();
    DBError loadparamfromCentral();
	DBError loadstationsetup();
    DBError loadZoneEntriesfromLocal();
    DBError loadvehicletype();
    DBError loadTR(int iType = 0);
    DBError LoadTariff();
    DBError LoadHoliday();
    DBError ClearHoliday();
    DBError LoadTariffTypeInfo();
    DBError LoadXTariff();

    int FnGetVehicleType(std::string IUCode);
    string GetPartialSeasonMsg(int iTransType);
    int FetchEntryinfo(string sIUNo);

    void moveOfflineTransToCentral();
	int insertTransToCentralEntryTransTmp(tEntryTrans_Struct ter);
	int insertTransToCentralExitTransTmp(const tExitTrans_Struct& tex);
	int deleteLocalTrans(string iuno,string trantime,Ctrl_Type ctrl);
    int clearseason();
    int IsBlackListIU(string sIU);
    int CheckCardOK(string sCardNo);
    int AddRemoteControl(string sTID,string sAction, string sRemarks);
    int AddSysEvent(string sEvent);

    int FnGetDatabaseErrorFlag();
    int HouseKeeping();
    int clearexpiredseason();
    int updateEntryTrans(string lpn, string sTransID);
    int updateExitTrans(string lpn, string sTransID);
    int updateExitReceiptNo(string sReceiptNo, string StnID); 
    int isValidBarCodeTicket(bool isRedemptionTicket, std::string sBarcodeTicket, std::tm& dtExpireTime, double& gbRedeemAmt, int& giRedeemTime);
    DBError update99PaymentTrans();
    DBError insertUPTFileSummaryLastSettlement(const std::string& sSettleDate, const std::string& sSettleName, int iSettleType, uint64_t lTotalTrans, double dTotalAmt, int iSendFlag, const std::string& sSendDate);
    DBError insertUPTFileSummary(const std::string& sSettleDate, const std::string& sSettleName, int iSettleType, uint64_t lTotalTrans, double dTotalAmt, int iSendFlag, const std::string& sSendDate);

    long  glToalRowAffed;


    /**
     * Singleton db should not be cloneable.
     */
     db (db&) = delete;

    /**
     * Singleton db should not be assignable.
     */
    void operator=(const db&) = delete;

private:

    string localConnStr;
    string CentralConnStr;
    string central_IP;
    float PingTimeOut;
   

    template <typename T>
        string ToString(T a);

    DBError loadEntrymessage(std::vector<ReaderItem>& selResult);
    DBError loadExitLcdAndLedMessage(std::vector<ReaderItem>& selResult);

    
    int season_update_flag;
    int season_update_count;
	int param_update_flag;  
	int param_update_count;
	int param_save_flag;

	int Alive;
	int timeOutVal;
	bool initialFlag;

	PMS_Comm onlineState;
	//------------------------
	int CentralDB_TimeOut;
	int LocalDB_TimeOut;
	int SP_TimeOut;
	int m_local_db_err_flag; // 0 -ok, 1 -error, 2 - update fail
	int m_remote_db_err_flag; // 0 -ok, 1 -error, 2 -update fail

	odbc *centraldb;
	odbc *localdb;

    static db* db_;
    static std::mutex mutex_;
    db();
    //-----------------------
    struct  tariff_struct gtariff[300][10];
    struct  tariff_type_info_struct  gtarifftypeinfo[2];
    //---------------
    std::vector<std::string> msholiday;
    std::vector<std::string> mspecialday;
    std::vector<struct XTariff_Struct> msxtariff;

};


