#pragma once

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
    iClose = 6

}DBError;


class db {
public:
    db(string localStr,string CentralStr,
       string cent_IP,int LocalSQLTimeOut,
       int CentralSQLTimeOut, int SP_SQLTimeOut, float mPingTimeOut);
    virtual ~db();

    int sp_isvalidseason(const std::string & sSeasonNo,
                      BYTE  iInOut,unsigned int iZoneID,std::string  &sSerialNo, short int &iRateType,
                      float &sFee, float &sAdminFee, float &sAppFee,
                      short int &iExpireDays, short int &iRedeemTime, float &sRedeemAmt,
                      std::string &AllowedHolderType,
                      std::string &dtValidTo,
                      std::string &dtValidFrom);

	int local_isvalidseason(string L_sSeasonNo);

    DBError RefreshTypeinRAM(std::list<tVType_Struct> *vlist);

	int isvalidseason(string m_sSeasonNo);
    void synccentraltime ();
    void downloadseason();
    int writeseason2local(season_struct& v);
    void downloadvehicletype();
    int writevehicletype2local(string iucode,string iutype);
    void downloadledmessage();
    int writeledmessage2local(string m_id,string m_body, string m_status);
    void downloadparameter();
    int writeparameter2local(string name,string value);
    void downloadstationsetup();
    int writestationsetup2local(tstation_struct& v);

	
     DBError insertentrytrans(tEntryTrans_Struct& tEntry);
	 DBError insertexittrans();
	 DBError insertbroadcasttrans(string sid,string iu_No,string S_cardno,string S_paidamt,string S_itype);
	 DBError loadmessage();
	 DBError loadParam();
	 DBError loadstationsetup();
     DBError loadvehicletype();

    int FnGetVehicleType(std::string IUCode);

private:

    string localConnStr;
    string CentralConnStr;
    string central_IP;
    float PingTimeOut;
    int clearseason();

    template <typename T>
        string ToString(T a);

    void insertToVTypeList(tVType_Struct v, std::list<tVType_Struct> *tmpList);
    DBError loadEntrymessage(std::vector<ReaderItem>& selResult);

    
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

};


