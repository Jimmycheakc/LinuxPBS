#include <iostream>
#include <ctime>
#include <iomanip>
#include <stdlib.h>
#include <sstream>
#include <sys/time.h>
#include <boost/algorithm/string.hpp>
#include "db.h"
#include "log.h"
#include "operation.h"

db* db::db_ = nullptr;
std::mutex db::mutex_;

db::db()
{

}

db* db::getInstance()
{
	std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr)
    {
        db_ = new db();
    }
    return db_;
}


int db::connectcentraldb(string connectStr,string connectIP,int CentralSQLTimeOut, int SP_SQLTimeOut,float mPingTimeOut)
{

	CentralConnStr=connectStr;
	
	central_IP=connectIP;

	CentralDB_TimeOut=CentralSQLTimeOut;

	SP_TimeOut=SP_SQLTimeOut;

	PingTimeOut=mPingTimeOut;

	season_update_flag=0;
	season_update_count=0;
	param_update_flag=0;

	Alive=0;
	timeOutVal=100; // PMS offline timeOut=10s

	onlineState=_Online;
	initialFlag=false;
	//---------------------------------
	centraldb=new odbc(SP_TimeOut,1,mPingTimeOut,central_IP,CentralConnStr);
   
    std::stringstream dbss;
	if (centraldb->Connect()==0) {
		dbss << "Central DB is connected!" ;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		operation::getInstance()->tProcess.giSystemOnline = 0;
		return 1;
	}
	else {
		dbss << "unable to connect Central DB" ;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		return 0;
	}
}

int db::connectlocaldb(string connectstr,int LocalSQLTimeOut,int SP_SQLTimeOut,float mPingTimeOut)
{

	std::stringstream dbss;
	localConnStr=connectstr;
	LocalDB_TimeOut=LocalSQLTimeOut;
	SP_TimeOut=SP_SQLTimeOut;

	PingTimeOut=mPingTimeOut;

	season_update_flag=0;
	season_update_count=0;
	param_update_flag=0;

	Alive=0;
	timeOutVal=100; // PMS offline timeOut=10s

	onlineState=_Online;
	initialFlag=false;
	//---------------------------------
	localdb=new odbc(LocalDB_TimeOut,1,PingTimeOut,"127.0.0.1",localConnStr);

	if (localdb->Connect()==0) {
		dbss << "Local DB is connected!" ;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		return 1;
	}
	else{
		dbss << "unable to connect Local DB" ;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		return 0;
	}

}

db::~db()
{
	centraldb->Disconnect();
	//localdb->Disconnect();
}

//Return: 0=invalid season, 1=valid, -1=db error
//            2=Expired,3=Terminated,4=blocked
//            5=lost, 6=passback, 7=not start, 8=not found
//            9=complimentary

int db::sp_isvalidseason(const std::string & sSeasonNo,
BYTE  iInOut,unsigned int iZoneID,std::string  &sSerialNo, short int &iRateType,
float &sFee, float &sAdminFee, float &sAppFee,
short int &iExpireDays, short int &iRedeemTime, float &sRedeemAmt,
std::string &AllowedHolderType,
std::string &dtValidTo,
std::string &dtValidFrom)
{


	//std::string sqlStmt;
	//std::string tbName="season_mst";
	//ClsDB clsObj(CentralDB_TimeOut,SP_TimeOut,m_log,central_IP,PingTimeOut);

	//return clsObj.isValidSeason(CentralConnStr,sSeasonNo, iInOut,iZoneID, sSerialNo,iRateType,
	//                     sFee,sAdminFee,sAppFee,iExpireDays,iRedeemTime, sRedeemAmt,
	//                     AllowedHolderType,dtValidTo,dtValidFrom);

	return centraldb->isValidSeason(sSeasonNo, iInOut,iZoneID, sSerialNo,iRateType,
	sFee,sAdminFee,sAppFee,iExpireDays,iRedeemTime, sRedeemAmt,
	AllowedHolderType,dtValidTo,dtValidFrom);

}

int db::local_isvalidseason(string L_sSeasonNo,unsigned int iZoneID)
{
	std::string sqlStmt;
	std::string tbName="season_mst";
	std::string sValue;
	vector<ReaderItem> selResult;
	int r,j,k,i;

	try
	{


		sqlStmt="Select season_type,s_status,date_from,date_to,vehicle_no,rate_type,multi_season_no,zone_id,redeem_amt,redeem_time,holder_type,sub_zone_id ";
		sqlStmt= sqlStmt +  " FROM " + tbName + " where (season_no='";
		sqlStmt=sqlStmt + L_sSeasonNo;
		sqlStmt=sqlStmt + "' or instr(multi_season_no," + "'" + L_sSeasonNo + "') >0 ) ";
		sqlStmt=sqlStmt + "AND date_from <= now() AND DATE(date_to) >= DATE(now()) AND (zone_id = '0' or zone_id = "+ to_string(iZoneID)+ ")";

		//operation::getInstance()->writelog(sqlStmt,"DB");

		r=localdb->SQLSelect(sqlStmt,&selResult,false);
		if (r!=0) return iLocalFail;

		if (selResult.size()>0){
				operation::getInstance()->tSeason.SeasonType =selResult[0].GetDataItem(0);
				operation::getInstance()->tSeason.s_status=selResult[0].GetDataItem(1);
				operation::getInstance()->tSeason.date_from=selResult[0].GetDataItem(2);
				operation::getInstance()->tSeason.date_to=selResult[0].GetDataItem(3);
				operation::getInstance()->tSeason.rate_type=selResult[0].GetDataItem(5);
				operation::getInstance()->tSeason.redeem_amt=selResult[0].GetDataItem(8);
				operation::getInstance()->tSeason.redeem_time=selResult[0].GetDataItem(9);
			return iDBSuccess;
		}
		else
		{
			return iNoData;
		}
	}
	catch(const std::exception &e)
	{
		return iLocalFail;
	}

}

int db::isvalidseason(string m_sSeasonNo,BYTE iInOut, unsigned int iZoneID)
{
	string sSerialNo="";
	float sFee=0;
	short int iRateType=0;
	short int m_iExpireDays=0, m_iRedeemTime=0;
	float m_sAdminFee=0,m_sAppFee=0, m_sRedeemAmt=0;
	std::string  m_dtValidTo="", m_dtValidFrom="";
	
	std::string  m_AllowedHolderType="";

	const std::string m_sBlank="";
	int retcode;
	string msg;
    std::stringstream dbss;
	dbss << "Check Season on Central DB for IU/card: " << m_sSeasonNo;
    Logger::getInstance()->FnLog(dbss.str(), "", "DB");

	retcode=sp_isvalidseason(m_sSeasonNo,iInOut,iZoneID,sSerialNo,iRateType,
	sFee,m_sAdminFee,m_sAppFee,m_iExpireDays,m_iRedeemTime, m_sRedeemAmt,
	m_AllowedHolderType,m_dtValidTo,m_dtValidFrom );

	dbss.str("");  // Set the underlying string to an empty string
    dbss.clear();   // Clear the state of the stream
	dbss << "Check Season Ret =" << retcode;
    Logger::getInstance()->FnLog(dbss.str(), "", "DB");

	if(retcode!=-1)
	{
		if (retcode != 8) 
		{
			dbss.str("");  // Set the underlying string to an empty string
    		dbss.clear();   // Clear the state of the stream
			dbss << "ValidFrom = " << m_dtValidFrom;
    		Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			dbss.str("");  // Set the underlying string to an empty string
    		dbss.clear();   // Clear the state of the stream
			dbss << "ValidTo = " << m_dtValidTo;
    		Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			//----
			operation::getInstance()->tSeason.date_from=m_dtValidFrom;
			operation::getInstance()->tSeason.date_to=m_dtValidTo;
			operation::getInstance()->tSeason.rate_type=std::to_string(iRateType);
			operation::getInstance()->tSeason.redeem_amt=std::to_string(m_sRedeemAmt);
			operation::getInstance()->tSeason.redeem_time=std::to_string(m_iRedeemTime);
		}
	}
	else
	{
		int l_ret=local_isvalidseason(m_sSeasonNo,iZoneID);
		if(l_ret==iDBSuccess) retcode = 1;
		else 
		retcode = 8;
		operation::getInstance()->writelog ("Check Local Season Return = "+ std::to_string(retcode), "DB");
	}
	return(retcode);
}

DBError db::insertbroadcasttrans(string sid,string iu_No,string S_cardno,string S_paidamt,string S_itype)
{

	std::string sqlStmt;
	string sLPRNo="";
	int r;
	int sNo;

	CE_Time dt;
	string dtStr=dt.DateString()+" "+dt.TimeString();

	sqlStmt= "Insert into Entry_Trans ";
	sqlStmt=sqlStmt + "(Station_ID,Entry_Time,iu_tk_No,";
	sqlStmt=sqlStmt + "trans_type";
	sqlStmt=sqlStmt + ",card_no,paid_amt";

	sqlStmt = sqlStmt + ") Values ('" + sid+ "','" +dtStr+ "','" + iu_No;
	sqlStmt = sqlStmt +  "','" + S_itype;
	sqlStmt = sqlStmt + "','" + S_cardno + "','" +S_paidamt+"'";
	sqlStmt = sqlStmt +  ")";

	r=localdb->SQLExecutNoneQuery(sqlStmt);

	std::stringstream dbss;
	if (r!=0)
	{
        dbss << "Insert Broadcast Entry_trans to Local: fail";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		return iLocalFail;

	}
	else
	{
		dbss << "Insert Broadcast Entry_trans to Local: success";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		return iDBSuccess;
	}

};

DBError db::insertentrytrans(tEntryTrans_Struct& tEntry)
{

	std::string sqlStmt;
	string sLPRNo="";
	int r;
	int sNo;
	std::stringstream dbss;

	//TK_Serialno is an integer type in DB
	//make sure the value must be the integer
	//to avoid exception
	CE_Time dt;
	tEntry.sEntryTime = dt.DateString()+" "+dt.TimeString();
	
	if (tEntry.sSerialNo=="") tEntry.sSerialNo="0";
	else{

		try
		{
			sNo=std::stoi(tEntry.sSerialNo);
		}
		catch(const std::exception &e)
		{
			tEntry.sSerialNo="0";
		}


	}

	//std::cout<<"m->tEntry.sSerialNo: " <<m->tEntry.sSerialNo<<std::endl;

	if(centraldb->IsConnected()!=1)
	{
		centraldb->Disconnect();
		if (centraldb->Connect() != 0)
		{
			dbss << "Insert Entry_trans to Central: fail1" ;
    		Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			operation::getInstance()->tProcess.giSystemOnline = 1;
			goto processLocal;
		}
	}

	operation::getInstance()->tProcess.giSystemOnline = 0;

	if ((tEntry.sLPN[0] != "")|| (tEntry.sLPN[1] !=""))
	{
		if(tEntry.iTransType==7 || tEntry.iTransType==8||tEntry.iTransType==22)
		{
			sLPRNo = tEntry.sLPN[1];

		}
		else
		{
			sLPRNo = tEntry.sLPN[0];
		}

	}

	sqlStmt= "Insert into Entry_Trans_tmp (Station_ID,Entry_Time,IU_Tk_No,trans_type,status,TK_Serialno,Card_Type";

	sqlStmt = sqlStmt + ",card_no,paid_amt,parking_fee";
	sqlStmt = sqlStmt + ",gst_amt,entry_lpn_SID";
	if (sLPRNo!="") sqlStmt = sqlStmt + ",lpn";

	sqlStmt = sqlStmt + ") Values ('" + tEntry.esid + "',convert(datetime,'" + tEntry.sEntryTime+ "',120),'" + tEntry.sIUTKNo;
	sqlStmt = sqlStmt +  "','" + std::to_string(tEntry.iTransType);
	sqlStmt = sqlStmt + "','" + std::to_string(tEntry.iStatus) + "','" + tEntry.sSerialNo;
	sqlStmt = sqlStmt + "','" + std::to_string(tEntry.iCardType)+"'";
	sqlStmt = sqlStmt + ",'" + tEntry.sCardNo + "','" + std::to_string(tEntry.sPaidAmt) + "','" + std::to_string(tEntry.sFee);
	sqlStmt = sqlStmt + "','" + std::to_string(tEntry.sGSTAmt);
	sqlStmt = sqlStmt + "','" + tEntry.gsTransID +"'";
	if (sLPRNo!="") sqlStmt = sqlStmt + ",'" + sLPRNo + "'";

	sqlStmt = sqlStmt +  ")";

	r = centraldb->SQLExecutNoneQuery(sqlStmt);
	if (r != 0)
	{
    	Logger::getInstance()->FnLog(sqlStmt, "", "DB");
        Logger::getInstance()->FnLog("Insert Entry_trans to Central: fail.", "", "DB");
		return iCentralFail;

	}
	else
	{
		dbss << "Insert Entry_trans to Central: success";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		return iDBSuccess;
	}

processLocal:

	if(localdb->IsConnected()!=1)
	{
		localdb->Disconnect();
		if (localdb->Connect() != 0)
		{
			dbss.str("");  // Set the underlying string to an empty string
        	dbss.clear();   // Clear the state of the stream
			dbss << "Insert Entry_trans to Local: fail1" ;
    		Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			return iLocalFail;
		}
	}

	sqlStmt= "Insert into Entry_Trans ";
	sqlStmt=sqlStmt + "(Station_id,Entry_Time,iu_tk_no,";
	sqlStmt=sqlStmt + "trans_type,status";
	sqlStmt=sqlStmt + ",TK_SerialNo";

	sqlStmt=sqlStmt + ",Card_Type";
	sqlStmt=sqlStmt + ",card_no,paid_amt,parking_fee";
	sqlStmt=sqlStmt +  ",gst_amt,entry_lpn_SID";
	sqlStmt = sqlStmt + ",lpn";

	sqlStmt = sqlStmt + ") Values ('" + tEntry.esid+ "','" + tEntry.sEntryTime+ "','" + tEntry.sIUTKNo;
	sqlStmt = sqlStmt +  "','" + std::to_string(tEntry.iTransType);
	sqlStmt = sqlStmt + "','" + std::to_string(tEntry.iStatus) + "','" + tEntry.sSerialNo;
	sqlStmt = sqlStmt + "','" + std::to_string(tEntry.iCardType) + "'";
	sqlStmt = sqlStmt + ",'" + tEntry.sCardNo + "','" + std::to_string(tEntry.sPaidAmt) + "','" + std::to_string(tEntry.sFee);
	sqlStmt = sqlStmt + "','" + std::to_string(tEntry.sGSTAmt);
	sqlStmt = sqlStmt + "','" + tEntry.gsTransID +"'";

	sqlStmt = sqlStmt + ",'" + sLPRNo + "'";

	sqlStmt = sqlStmt +  ")";

	r = localdb->SQLExecutNoneQuery(sqlStmt);
	if (r != 0)
	{
        Logger::getInstance()->FnLog(sqlStmt, "", "DB");
    	Logger::getInstance()->FnLog("Insert Entry_trans to Local: fail", "", "DB");
		return iLocalFail;

	}
	else
	{
    	Logger::getInstance()->FnLog("Insert Entry_trans to Local: success", "", "DB");
		operation::getInstance()->tProcess.glNoofOfflineData = operation::getInstance()->tProcess.glNoofOfflineData + 1;
		return iDBSuccess;
	}

}

void db::synccentraltime()
{

	std::string sqlStmt;
	vector<ReaderItem> selResult;
	std::stringstream dbss;
	int r;
	int sNo;
	std:string dt;

	if(centraldb->IsConnected()!=1)
	{
		centraldb->Disconnect();
		if(centraldb->Connect() != 0) { return;}
	}
	
	sqlStmt= "SELECT GETDATE() AS CurrentTime";
	r=centraldb->SQLSelect(sqlStmt,&selResult,false);
	if (r!=0)
	{
		dbss << "Unable to retrieve Central DB time";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");

	}
	else
	{
		if (selResult.size()>0)
		{
			dt=selResult[0].GetDataItem(0);
			dbss << "Central DB time: " << dt;
    		Logger::getInstance()->FnLog(dbss.str(), "", "DB");

			struct tm tmTime = {};

			std::istringstream ss(dt);
			ss >> std::get_time(&tmTime, "%Y-%m-%d %H:%M:%S");
			if (ss.fail()) 
			{
				dbss.str("");  // Set the underlying string to an empty string
    			dbss.clear();   // Clear the state of the stream
				dbss << "Failed to parse the time string.";
    			Logger::getInstance()->FnLog(dbss.str(), "", "DB");
				return ;
			}

			// Convert tm structure to seconds since epoch
			time_t epochTime = mktime(&tmTime);

			// Create a timeval structure
			struct timeval newTime;
			newTime.tv_sec = epochTime;
			newTime.tv_usec = 0;

			// Set the new time
			dbss.str("");  // Set the underlying string to an empty string
    		dbss.clear();   // Clear the state of the stream
			if (settimeofday(&newTime, nullptr) == 0)
			{
				dbss << "Time set successfully.";
    			Logger::getInstance()->FnLog(dbss.str(), "", "DB");

				if (std::system("hwclock --systohc") != 0)
				{
					Logger::getInstance()->FnLog("Sync error.", "", "DB");
				}
				else
				{
					Logger::getInstance()->FnLog("Sync successfully.", "", "DB");
				}
			}
			else
			{
				dbss << "Error setting time.";
    			Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
					return;
		}			
	}
	return;
}

int db::downloadseason()
{
	int ret = -1;
	unsigned long j,k;
	vector<ReaderItem> tResult;
	vector<ReaderItem> selResult;
	tseason_struct v;
	int r = 0;
	string seasonno;
	std::string sqlStmt;
	int w = -1;
	std::stringstream dbss;
	int giStnid;
	giStnid = operation::getInstance()->gtStation.iSID;

	sqlStmt = "SELECT SUM(A) FROM (";
	sqlStmt = sqlStmt + "SELECT count(season_no) as A FROM season_mst WHERE s" + to_string(giStnid) + "_fetched = 0 ";
	sqlStmt = sqlStmt + ") as B ";

	r = centraldb->SQLSelect(sqlStmt, &tResult, false);
	if(r != 0)
	{
		m_remote_db_err_flag = 1;
		return ret;
	}
	else 
	{
		if (std::stoi(tResult[0].GetDataItem(0)) == 0)
		{
			return ret;
		}
		m_remote_db_err_flag = 0;
		dbss << "Total: " << std::string (tResult[0].GetDataItem(0)) << " Seasons to be download.";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}
	
	r = centraldb->SQLSelect("SELECT TOP 10 * FROM season_mst WHERE s" + to_string(1) + "_fetched = 0 ", &selResult, true);
	if(r != 0)
	{
		m_remote_db_err_flag = 1;
		return ret;
	}
	else
	{
		m_remote_db_err_flag = 0;
	}

	int downloadCount = 0;
	if (selResult.size() > 0)
	{
		//--------------------------------------------------------
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading " << std::to_string (selResult.size()) << " Records: Started";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");

		for(j = 0; j < selResult.size(); j++)
		{
			v.season_no = selResult[j].GetDataItem(1);
			v.SeasonType = selResult[j].GetDataItem(2);  
			seasonno = v.season_no;          
			v.s_status = selResult[j].GetDataItem(3);   
			v.date_from = selResult[j].GetDataItem(4);     
			v.date_to = selResult[j].GetDataItem(5);    
			v.vehicle_no = selResult[j].GetDataItem(8);
			v.rate_type = selResult[j].GetDataItem(58);
			v.pay_to = selResult[j].GetDataItem(66);
			v.pay_date = selResult[j].GetDataItem(67);
			v.multi_season_no=selResult[j].GetDataItem(72);
			v.zone_id = selResult[j].GetDataItem(77);
			v.redeem_time = selResult[j].GetDataItem(78);
			v.redeem_amt = selResult[j].GetDataItem(79);
			v.holder_type = selResult[j].GetDataItem(6);
			v.sub_zone_id = selResult[j].GetDataItem(80);

			season_update_count++;

			w = writeseason2local(v);
			
			if (w == 0) 
			{
				dbss.str("");  // Set the underlying string to an empty string
    			dbss.clear();   // Clear the state of the stream
				dbss << "Download season: " << seasonno;
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");

				//update to central DB
				r = centraldb->SQLExecutNoneQuery("UPDATE season_mst SET s" + to_string(1) + "_fetched = '1' WHERE season_no = '" + seasonno + "'");
				dbss.str("");  // Set the underlying string to an empty string
    			dbss.clear();   // Clear the state of the stream
				if (r != 0) 
				{
					m_remote_db_err_flag = 2;
					dbss.str("");  // Set the underlying string to an empty string
    				dbss.clear();   // Clear the state of the stream
					dbss << "update central season status failed.";
					Logger::getInstance()->FnLog(dbss.str(), "", "DB");
				}
				else 
				{
					downloadCount++;
					m_remote_db_err_flag = 0;
					//dbss << "set central season success.";
					//Logger::getInstance()->FnLog(dbss.str(), "", "DB");
				}
			}
			//---------------------------------------
		}
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading Records: End, Total Record :" << selResult.size() << " ,Downloaded Record :" << downloadCount;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}

	if (selResult.size() < 10)
	{
		season_update_flag = 0;
	}

	ret = downloadCount;

	return ret;
}

int db::writeseason2local(tseason_struct& v)
{
	int r=-1;// success flag
	int debug=0;
	std::string sqlStmt;
	vector<ReaderItem> selResult;
	std::stringstream dbss;
	
	try 
	{
		r = localdb->SQLSelect("SELECT season_type FROM season_mst Where season_no= '" + v.season_no + "'", &selResult, false);
		if (r != 0)
		{
			m_local_db_err_flag=1;
			operation::getInstance()->writelog("update season failed.", "DB");
			return r;
		}
		else
		{
			m_local_db_err_flag = 0;
		}

		if (selResult.size() > 0)
		{
			v.found = 1;

			//update season records
			sqlStmt="Update season_mst SET ";
			sqlStmt= sqlStmt+ "season_no='" + v.season_no + "',";
			sqlStmt= sqlStmt+ "season_type='" + v.SeasonType + "',";
			sqlStmt= sqlStmt+ "s_status='" + v.s_status  + "',";
			sqlStmt= sqlStmt+ "date_from='" + v.date_from + "',";
			sqlStmt= sqlStmt+ "date_to='" + v.date_to  + "',";
			sqlStmt= sqlStmt+ "vehicle_no='" + v.vehicle_no  + "',";
			sqlStmt= sqlStmt+ "rate_type='" + v.rate_type  + "',";
			sqlStmt= sqlStmt+ "pay_to='" + v.pay_to  + "',";
			sqlStmt= sqlStmt+ "pay_date='" + v.pay_date  + "',";
			sqlStmt= sqlStmt+ "multi_Season_no='" + v.multi_season_no  + "',";
			sqlStmt= sqlStmt+ "zone_id='" + v.zone_id + "',";
			sqlStmt= sqlStmt+ "redeem_amt='" + v.redeem_amt + "',";
			sqlStmt= sqlStmt+ " redeem_time='" + v.redeem_time + "',";
			sqlStmt= sqlStmt+ " holder_type='" + v.holder_type + "',";
			sqlStmt= sqlStmt+ " sub_zone_id='" + v.sub_zone_id + "'";
			sqlStmt= sqlStmt+ " WHERE season_no='" +v.season_no + "'";
			
			r = localdb->SQLExecutNoneQuery (sqlStmt);

			if (r != 0)  
			{
				m_local_db_err_flag = 1;
				operation::getInstance()->writelog("update season failed.", "DB");
			}
			else  
			{
				m_local_db_err_flag = 0;
			}
		}
		else
		{
			v.found = 0;
			//insert season records
			sqlStmt ="INSERT INTO season_mst ";
			sqlStmt = sqlStmt + " (season_no,season_type,s_status,date_from,date_to,vehicle_no,rate_type, ";
			sqlStmt = sqlStmt + " pay_to,pay_date,multi_season_no,zone_id,redeem_amt,redeem_time,holder_type,sub_zone_id)";
			sqlStmt = sqlStmt + " VALUES ('" + v.season_no+ "'";
			sqlStmt = sqlStmt + ",'" + v.SeasonType  + "'";
			sqlStmt = sqlStmt + ",'" + v.s_status + "'";
			sqlStmt = sqlStmt + ",'" + v.date_from   + "'";
			sqlStmt = sqlStmt + ",'" + v.date_to  + "'";
			sqlStmt = sqlStmt + ",'" + v.vehicle_no   + "'";
			sqlStmt = sqlStmt + ",'" + v.rate_type    + "'";
			sqlStmt = sqlStmt + ",'" + v.pay_to + "'";
			sqlStmt = sqlStmt + ",'" + v.pay_date    + "'";
			sqlStmt = sqlStmt + ",'" + v.multi_season_no   + "'";
			sqlStmt = sqlStmt + ",'" + v.zone_id   + "'";
			sqlStmt = sqlStmt + ",'" + v.redeem_amt   + "'";
			sqlStmt = sqlStmt + ",'" + v.redeem_time   + "'";
			sqlStmt = sqlStmt + ",'" + v.holder_type   + "'";
			sqlStmt = sqlStmt + ",'" + v.sub_zone_id  + "') ";

			r = localdb->SQLExecutNoneQuery(sqlStmt);

			if (r != 0)  
			{
				m_local_db_err_flag = 1;
				dbss.str("");  // Set the underlying string to an empty string
    			dbss.clear();   // Clear the state of the stream
				//dbss << "Insert season to local failed. ";
				dbss << sqlStmt;
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
			else  
			{
				m_local_db_err_flag = 0;
				dbss.str("");  // Set the underlying string to an empty string
    			dbss.clear();   // Clear the state of the stream
				dbss << "Insert season to local success. ";
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
		}
	}
	catch (const std::exception &e)
	{
		r = -1;// success flag
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "DB: local db error in writing rec: " << std::string(e.what());
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		m_local_db_err_flag = 1;
	}

	return r;
}

int db::downloadvehicletype()
{
	int ret = -1;
	unsigned long j;
	vector<ReaderItem> tResult;
	vector<ReaderItem> selResult;
	int r=0;
	string iu_code;
	string iu_type;
	std::string sqlStmt;
	int w = -1;
	std::stringstream dbss;

	//write log for total seasons

	sqlStmt = "SELECT SUM(A) FROM (";
	sqlStmt = sqlStmt + "SELECT count(IUCode) as A FROM Vehicle_type";
	sqlStmt = sqlStmt + ") as B ";

	//dbss << sqlStmt;
    //Logger::getInstance()->FnLog(dbss.str(), "", "DB");

	r = centraldb->SQLSelect(sqlStmt,&tResult,false);
	if (r != 0)
	{
		m_remote_db_err_flag = 1; 
		operation::getInstance()->writelog("download vehicle type fail.", "DB");
		return ret;
	}
	else 
	{
		m_remote_db_err_flag = 0;
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Total " << std::string (tResult[0].GetDataItem(0)) << " vehicle type to be download.";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}

	r = centraldb->SQLSelect("SELECT  * FROM Vehicle_type", &selResult, true);
	if (r != 0)
	{
		m_remote_db_err_flag = 1;
		return ret;
	}
	else
	{
		m_remote_db_err_flag = 0;
	}

	int downloadCount = 0;
	if (selResult.size() > 0)
	{
		//--------------------------------------------------------
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading " << std::to_string (selResult.size()) << " Records: Started";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		

		for(j = 0; j < selResult.size(); j++){
			// std::cout<<"Rec "<<j<<std::endl;
			// for (k=0;k<selResult[j].getDataSize();k++){
			//     std::cout<<"item["<<k<< "]= " << selResult[j].GetDataItem(k)<<std::endl;                
			// }  
			iu_code = selResult[j].GetDataItem(1);
			iu_type = selResult[j].GetDataItem(2); 

			w = writevehicletype2local(iu_code, iu_type);
			if (w == 0)
			{
				downloadCount++;
			}
			//---------------------------------------
			
		}
		//m->initStnParameters();
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading vehicle type Records: End, Total Record :" << selResult.size() << " ,Downloaded Record :" << downloadCount;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}

	ret = downloadCount;

	return ret;
}

int db::writevehicletype2local(string iucode,string iutype)
{

	int r = -1;// success flag
	int debug=0;
	std::string sqlStmt;
	vector<ReaderItem> selResult;
	std::stringstream dbss;
	
	try 
	{
		r = localdb->SQLSelect("SELECT IUCode FROM Vehicle_type Where IUCode= '" + iucode + "'", &selResult, false);
		if(r != 0)
		{
			operation::getInstance()->writelog("update vehicle type to local fail.", "DB");
			m_local_db_err_flag = 1;
			return r;
		}
		else
		{
			m_local_db_err_flag = 0;
		}

		if (selResult.size() > 0)
		{

			//update param records
			sqlStmt = "Update Vehicle_type SET ";
			sqlStmt = sqlStmt + "TransType='" + iutype + "'";
			sqlStmt = sqlStmt + " Where IUCode= '" + iucode + "'";
			
			r = localdb->SQLExecutNoneQuery(sqlStmt);

			if (r != 0)  
			{
				m_local_db_err_flag = 1;
				dbss.str("");  // Set the underlying string to an empty string
				dbss.clear();   // Clear the state of the stream
				dbss << "update local vehicle type failed";
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
			else  
			{
				m_local_db_err_flag=0;
				//dbss.str("");  // Set the underlying string to an empty string
				//dbss.clear();   // Clear the state of the stream
				//dbss << "update local vehicle type success ";
				//Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
			
		}
		else
		{
			sqlStmt = "INSERT INTO Vehicle_type";
			sqlStmt = sqlStmt + " (IUCode,TransType) ";
			sqlStmt = sqlStmt + " VALUES ('" +iucode+ "'";
			sqlStmt = sqlStmt + ",'" + iutype + "')";

			r = localdb->SQLExecutNoneQuery(sqlStmt);

			if (r != 0)  
			{
				m_local_db_err_flag = 1;
				dbss.str("");  // Set the underlying string to an empty string
				dbss.clear();   // Clear the state of the stream
				dbss << "insert vehicle type to local failed";
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
			else  
			{
				m_local_db_err_flag = 0;
			}
		}

	}
	catch (const std::exception &e)
	{
		r = -1;// success flag
		// cout << "ERROR: " << err << endl;
		dbss.str("");  // Set the underlying string to an empty string
		dbss.clear();   // Clear the state of the stream
		dbss << "DB: local db error in writing rec: " << std::string(e.what());
		Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		m_local_db_err_flag = 1;
	}

	return r;
}

int db::downloadledmessage()
{
	int ret = -1;
	unsigned long j, k;
	vector<ReaderItem> tResult;
	vector<ReaderItem> selResult;
	int r = -1;
	string msg_id;
	string msg_body;
	string msg_status;
	std::string sqlStmt;
	int w = -1;
	std::stringstream dbss;
	int giStnid;
	giStnid = operation::getInstance()->gtStation.iSID;

	//write log for total seasons

	sqlStmt="SELECT SUM(A) FROM (";
	sqlStmt = sqlStmt + "SELECT count(msg_id) as A FROM message_mst WHERE s" + to_string(giStnid) + "_fetched = 0";
	sqlStmt = sqlStmt + ") as B ";

	//dbss << sqlStmt;
    //Logger::getInstance()->FnLog(dbss.str(), "", "DB");

	r = centraldb->SQLSelect(sqlStmt, &tResult, false);
	if (r != 0)
	{
		m_remote_db_err_flag = 1;
		operation::getInstance()->writelog("download LED message fail.", "DB");
		return ret;
	}
	else 
	{
		m_remote_db_err_flag = 0;
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Total " << std::string (tResult[0].GetDataItem(0)) << " message to be download.";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}

	r = centraldb->SQLSelect("SELECT  * FROM message_mst WHERE s" + to_string(giStnid) + "_fetched = 0", &selResult, true);
	if(r != 0)
	{
		m_remote_db_err_flag = 1;
		operation::getInstance()->writelog("download LED message fail.", "DB");
		return ret;
	}
	else
	{
		m_remote_db_err_flag = 0;
	}

	int downloadCount = 0;
	if (selResult.size() > 0)
	{
		//--------------------------------------------------------
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading message " << std::to_string (selResult.size()) << " Records: Started";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");

		for(j = 0; j < selResult.size(); j++)
		{
			// std::cout<<"Rec "<<j<<std::endl;
			// for (k=0;k<selResult[j].getDataSize();k++){
			//     std::cout<<"item["<<k<< "]= " << selResult[j].GetDataItem(k)<<std::endl;                
			// }  
			msg_id = selResult[j].GetDataItem(0);
			msg_body = selResult[j].GetDataItem(2);
			msg_status =  selResult[j].GetDataItem(3); 
			
			w = writeledmessage2local(msg_id,msg_body,msg_status);
			
			if (w == 0) 
			{
				//update to central DB
				r = centraldb->SQLExecutNoneQuery("UPDATE message_mst SET s" + to_string(giStnid) + "_fetched = '1' WHERE msg_id = '" + msg_id + "'");
				if(r != 0) 
				{
					m_remote_db_err_flag = 2;
					dbss.str("");  // Set the underlying string to an empty string
    				dbss.clear();   // Clear the state of the stream
					dbss << "update central message status failed.";
					Logger::getInstance()->FnLog(dbss.str(), "", "DB");
				}
				else 
				{
					downloadCount++;
					m_remote_db_err_flag = 0;
					//printf("update central message success \n");
				}
			}
			//---------------------------------------
		}
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading Msg Records: End, Total Record :" << selResult.size() << " ,Downloaded Record :" << downloadCount;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}

	ret = downloadCount;

	return ret;
}

int db::writeledmessage2local(string m_id,string m_body, string m_status)
{

	int r = -1;// success flag
	int debug = 0;
	std::string sqlStmt;
	vector<ReaderItem> selResult;
	std::stringstream dbss;

	try 
	{
		r = localdb->SQLSelect("SELECT msg_id FROM message_mst Where msg_id= '" + m_id + "'", &selResult, false);
		if(r != 0)
		{
			m_local_db_err_flag = 1;
			operation::getInstance()->writelog("update local LED Message failed.", "DB");
			return r;
		}
		else
		{
			m_local_db_err_flag = 0;
		}

		if (selResult.size() > 0)
		{

			//update param records
			sqlStmt = "Update message_mst SET ";
			sqlStmt = sqlStmt + "msg_body='" + m_body + "'";
			sqlStmt = sqlStmt + ", m_status= '" + m_status + "'";
			sqlStmt = sqlStmt + " Where msg_id= '" + m_id + "'";
			
			r = localdb->SQLExecutNoneQuery(sqlStmt);

			if (r != 0)  
			{
				m_local_db_err_flag = 1;
				dbss.str("");  // Set the underlying string to an empty string
				dbss.clear();   // Clear the state of the stream
				dbss << "update local message failed";
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
			else  
			{
				m_local_db_err_flag = 0;
			//	printf("update local msg success \n");
			}
		}
		else
		{
			sqlStmt = "INSERT INTO message_mst";
			sqlStmt = sqlStmt + " (msg_id,msg_body,m_status) ";
			sqlStmt = sqlStmt + " VALUES ('" + m_id+ "'";
			sqlStmt = sqlStmt + ",'" + m_body + "'";
			sqlStmt = sqlStmt + ",'" + m_status + "')";

			r = localdb->SQLExecutNoneQuery(sqlStmt);

			if (r != 0)  
			{
				m_local_db_err_flag = 1;
				dbss.str("");  // Set the underlying string to an empty string
				dbss.clear();   // Clear the state of the stream
				dbss << "insert message to local failed";
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
			else  
			{
				m_local_db_err_flag = 0;
			//	printf("set local msg success \n");
			}
		}

	}
	catch (const std::exception &e)
	{
		r = -1;// success flag
		// cout << "ERROR: " << err << endl;
		dbss.str("");  // Set the underlying string to an empty string
		dbss.clear();   // Clear the state of the stream
		dbss << "DB: local db error in writing rec: " << std::string(e.what());
		Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		m_local_db_err_flag = 1;
	}

	return r;
}

int db::downloadparameter()
{
	int ret = -1;
	unsigned long j, k;
	vector<ReaderItem> tResult;
	vector<ReaderItem> selResult;
	int r = 0;
	string param_name;
	string param_value;
	std::string sqlStmt;
	std::stringstream dbss;
	int w = -1;
	int giStnid;
	giStnid = operation::getInstance()->gtStation.iSID;

	//write log for total seasons

	sqlStmt = "SELECT SUM(A) FROM (";
	sqlStmt = sqlStmt + "SELECT count(name) as A FROM parameter_mst WHERE s" + to_string(giStnid) + "_fetched = 0 and for_station=1";
	sqlStmt = sqlStmt + ") as B ";


	r = centraldb->SQLSelect(sqlStmt, &tResult, false);
	if (r != 0)
	{
		m_remote_db_err_flag = 1;
		operation::getInstance()->writelog("download parameter fail.", "DB");
		return ret;
	}
	else 
	{
		m_remote_db_err_flag = 0;
		dbss << "Total " << std::string (tResult[0].GetDataItem(0)) << " parameter to be download.";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}

	r = centraldb->SQLSelect("SELECT  * FROM parameter_mst WHERE s" + to_string(giStnid) + "_fetched = 0 and for_station=1", &selResult, true);
	if (r != 0)
	{
		m_remote_db_err_flag = 1;
		operation::getInstance()->writelog("update parameter failed.", "DB");
		return ret;
	}
	else
	{
		m_remote_db_err_flag = 0;
	}

	int downloadCount = 0;
	if (selResult.size() > 0)
	{
		//--------------------------------------------------------
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading parameter " << std::to_string (selResult.size()) << " Records: Started";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");

		for(j = 0; j < selResult.size(); j++)
		{
			param_name = selResult[j].GetDataItem(0);
			param_value = selResult[j].GetDataItem(49+giStnid);      
			dbss.str("");  // Set the underlying string to an empty string
			dbss.clear();   // Clear the state of the stream
			dbss << param_name << " = "  << param_value;
			Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			param_update_count++;
			w = writeparameter2local(param_name,param_value);
			
			if (w == 0) 
			{
				//update to central DB
				r = centraldb->SQLExecutNoneQuery("UPDATE parameter_mst SET s" + to_string(giStnid) + "_fetched = '1' WHERE name = '" + param_name + "'");
				if(r != 0) 
				{
					m_remote_db_err_flag = 2;
					dbss.str("");  // Set the underlying string to an empty string
    				dbss.clear();   // Clear the state of the stream
					dbss << "update central parameter status failed.";
					Logger::getInstance()->FnLog(dbss.str(), "", "DB");
				}
				else 
				{
					downloadCount++;
					m_remote_db_err_flag = 0;
				//	printf("set central parameter success \n");
				}
			}			
			
		}
		//LoadParam();
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading Parameter Records: End, Total Record :" << selResult.size() << " ,Downloaded Record :" << downloadCount;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}
	else
	{
		param_update_flag = 0;
	}

	ret = downloadCount;

	return ret;
}


int db::writeparameter2local(string name,string value)
{
	int r = -1;// success flag
	std::string sqlStmt;
	vector<ReaderItem> selResult;
	std::stringstream dbss;
	
	try 
	{
		r = localdb->SQLSelect("SELECT ParamName FROM Param_mst Where ParamName= '" + name + "'", &selResult, false);
		if (r != 0)
		{
			m_local_db_err_flag = 1;
			operation::getInstance()->writelog("update parameter fail.", "DB");
			return r;
		}
		else
		{
			m_local_db_err_flag = 0;
		}

		if (selResult.size() > 0)
		{

			//update param records
			sqlStmt = "Update Param_mst SET ";
			sqlStmt = sqlStmt + "ParamValue='" + value + "'";
			sqlStmt = sqlStmt + " Where ParamName= '" + name + "'";
			
			r = localdb->SQLExecutNoneQuery(sqlStmt);

			if (r != 0)  
			{
				m_local_db_err_flag = 1;
				dbss.str("");  // Set the underlying string to an empty string
				dbss.clear();   // Clear the state of the stream
				dbss << "update parameter to local failed";
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
			else  
			{
				m_local_db_err_flag = 0;
			//	printf("update Param success \n");
			}
			
		}
		else
		{
			sqlStmt = "INSERT INTO Param_mst";
			sqlStmt = sqlStmt + " (ParamName,ParamValue) ";
			sqlStmt = sqlStmt + " VALUES ('" + name + "'";
			sqlStmt = sqlStmt + ",'" + value  + "')";

			r = localdb->SQLExecutNoneQuery(sqlStmt);

			if (r != 0)  
			{
				m_local_db_err_flag = 1;
				dbss.str("");  // Set the underlying string to an empty string
				dbss.clear();   // Clear the state of the stream
				dbss << "insert parameter to local failed";
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
			else  
			{
				m_local_db_err_flag = 0;
				//printf("set Param success \n");
			}
		}

	}
	catch (const std::exception &e)
	{
		r = -1;// success flag
		// cout << "ERROR: " << err << endl;
		dbss.str("");  // Set the underlying string to an empty string
		dbss.clear();   // Clear the state of the stream
		dbss << "DB: local db error in writing rec: " << std::string(e.what());
		Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		m_local_db_err_flag = 1;
	}

	return r;
}

int db::downloadstationsetup()
{
	int ret = -1;
	tstation_struct v;
	vector<ReaderItem> tResult;
	vector<ReaderItem> selResult;
	std::string sqlStmt;
	std::stringstream dbss;
	int r = -1;
	int w = -1;
	
	//write log for total seasons

	sqlStmt = "SELECT SUM(A) FROM (";
	sqlStmt = sqlStmt + "SELECT count(station_id) as A FROM station_setup";
	sqlStmt = sqlStmt + ") as B ";

	//dbss << sqlStmt;
    //Logger::getInstance()->FnLog(dbss.str(), "", "DB");

	r = centraldb->SQLSelect(sqlStmt, &tResult, false);
	if (r != 0)
	{
		m_remote_db_err_flag = 1;
		operation::getInstance()->writelog("download station setup fail.", "DB");
		return ret;
	}
	else 
	{
		m_remote_db_err_flag = 0;
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Total " << std::string (tResult[0].GetDataItem(0)) << " station setup to be download.";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		
	}

	r = centraldb->SQLSelect("SELECT  * FROM station_setup",&selResult,true);
	if (r != 0)
	{
		m_remote_db_err_flag = 1;
		operation::getInstance()->writelog("download station setup fail.", "DB");
		return ret;
	}
	else
	{
		m_remote_db_err_flag = 0;
	}

	int downloadCount = 0;
	if (selResult.size() > 0)
	{
		for(int j = 0; j < selResult.size(); j++)
		{
			v.iGroupID = std::stoi(selResult[j].GetDataItem(0));
			v.iSID = std::stoi(selResult[j].GetDataItem(2));
			v.sName = selResult[j].GetDataItem(3);

			switch(std::stoi(selResult[j].GetDataItem(5)))
			{
			case 1:
				v.iType = tientry;
				break;
			case 2:
				v.iType = tiExit;
				break;
			default:
				break;
			};
			v.iStatus = std::stoi(selResult[j].GetDataItem(6));
			v.sPCName = selResult[j].GetDataItem(4);
			v.iCHUPort = std::stoi(selResult[j].GetDataItem(17));
			v.iAntID = std::stoi(selResult[j].GetDataItem(18));
			v.iZoneID = std::stoi(selResult[j].GetDataItem(19));
			v.iIsVirtual = std::stoi(selResult[j].GetDataItem(20));
			switch(std::stoi(selResult[j].GetDataItem(22)))
			{
			case 0:
				v.iSubType = iNormal;
				break;
			case 1:
				v.iSubType = iXwithVENoPay;
				break;
			case 2:
				v.iSubType = iXwithVEPay;
				break;
			default:
				break;
			};
			v.iVirtualID = std::stoi(selResult[j].GetDataItem(21));
			
			w = writestationsetup2local(v);
			
			if (w == 0) 
			{
				downloadCount++;
			//	printf("set station setup ok \n");
			}
			//m->initStnParameters();
		}

		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading Station Setup Records: End, Total Record :" << selResult.size() << " ,Downloaded Record :" << downloadCount;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		
	}
	if (selResult.size() < 1){
		//no record
		r = -1;
	}

	ret = downloadCount;

	return ret;
}


int db::writestationsetup2local(tstation_struct& v)
{
	
	int r = -1;// success flag
	int debug = 0;
	std::string sqlStmt;
	vector<ReaderItem> selResult;
	std::stringstream dbss;
	
	try 
	{ 
		r = localdb->SQLSelect("SELECT StationType FROM Station_Setup Where StationID= '" + std::to_string(v.iSID) + "'", &selResult, false);
		if(r != 0)
		{
			m_local_db_err_flag = 1;
			operation::getInstance()->writelog("update station setup fail.", "DB");
			return r;
		}
		else
		{
			m_local_db_err_flag = 0;
		}

		if (selResult.size() > 0)
		{
			sqlStmt = "Update Station_Setup SET ";
			//sqlStmt= sqlStmt+ "groupid='" + std::to_string(v.iGroupID) + "',";
			sqlStmt = sqlStmt + "StationID='" + std::to_string(v.iSID) + "',";
			sqlStmt = sqlStmt + "StationName='" +v.sName + "',";
			sqlStmt = sqlStmt + "StationType='" + std::to_string(v.iType) + "',";
			sqlStmt = sqlStmt + "Status='" + std::to_string(v.iStatus) + "',";
			sqlStmt = sqlStmt + "PCName='" + v.sPCName + "',";
			sqlStmt = sqlStmt + "CHUPort='" + std::to_string(v.iCHUPort) + "',";
			sqlStmt = sqlStmt + "AntID='" + std::to_string(v.iAntID) + "',";
			sqlStmt = sqlStmt + "ZoneID='" + std::to_string(v.iZoneID) + "',";
			sqlStmt = sqlStmt + "IsVirtual='" + std::to_string(v.iIsVirtual) + "',";
			sqlStmt = sqlStmt + "SubType='" + std::to_string(v.iSubType) + "',";
			sqlStmt = sqlStmt + "VirtualID='" + std::to_string(v.iVirtualID) + "'";
			sqlStmt = sqlStmt + " WHERE StationID='" +std::to_string(v.iSID) + "'";
			r = localdb->SQLExecutNoneQuery(sqlStmt);

			if (r != 0)  
			{
				m_local_db_err_flag = 1;
				dbss.str("");  // Set the underlying string to an empty string
				dbss.clear();   // Clear the state of the stream
				dbss << " Update local station set up failed";
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
			else  
			{
				m_local_db_err_flag = 0;
			//	printf("update Station_Setup success \n");
			}
		}
		else
		{
			sqlStmt = "INSERT INTO Station_Setup";
			sqlStmt = sqlStmt + " (StationID,StationName,StationType,Status,PCName,CHUPort,AntID, ";
			sqlStmt = sqlStmt + " ZoneID,IsVirtual,SubType,VirtualID)";
			sqlStmt = sqlStmt + " VALUES ('" + std::to_string(v.iSID)+ "'";
			sqlStmt = sqlStmt + ",'" + v.sName + "'";
			sqlStmt = sqlStmt + ",'" + std::to_string(v.iType) + "'";
			sqlStmt = sqlStmt + ",'" + std::to_string(v.iStatus) + "'";
			sqlStmt = sqlStmt + ",'" + v.sPCName + "'";
			sqlStmt = sqlStmt + ",'" + std::to_string(v.iCHUPort) + "'";
			sqlStmt = sqlStmt + ",'" + std::to_string(v.iAntID) + "'";
			sqlStmt = sqlStmt + ",'" + std::to_string(v.iZoneID) + "'";
			sqlStmt = sqlStmt + ",'" +  std::to_string(v.iIsVirtual) + "'";
			sqlStmt = sqlStmt + ",'" + std::to_string(v.iSubType) + "'";
			sqlStmt = sqlStmt + ",'" + std::to_string(v.iVirtualID) + "')";
			//sqlStmt=sqlStmt+ ",'" + std::to_string(v.iGroupID) + "') ";

			r = localdb->SQLExecutNoneQuery(sqlStmt);

			if (r != 0)  
			{
				m_local_db_err_flag = 1;
				dbss.str("");  // Set the underlying string to an empty string
				dbss.clear();   // Clear the state of the stream
				dbss << "insert parameter to local failed";
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
			else  
			{
				m_local_db_err_flag = 0;
			//	printf("station_setup insert sucess\n");
			}	               
		}
	}
	catch (const std::exception &e)
	{
		r = -1;// success flag
		// cout << "ERROR: " << err << endl;
		dbss.str("");  // Set the underlying string to an empty string
		dbss.clear();   // Clear the state of the stream
		dbss << "DB: local db error in writing rec: " << std::string(e.what());
		Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		m_local_db_err_flag = 1;
	}

	return r;
}

int db::downloadtariffsetup(int iGrpID, int iSiteID, int iCheckStatus)
{
	int ret = -1;
	std::vector<ReaderItem> tResult;
	std::vector<ReaderItem> selResult;
	std::string sqlStmt;
    std::stringstream dbss;
	int r = -1;
	int w = -1;
	int iZoneID;

	if (iCheckStatus == 1)
	{
		// Check tariff is downloaded or not
		sqlStmt = "SELECT name FROM parameter_mst ";
		sqlStmt = sqlStmt + "WHERE name='DownloadTariff' AND s" + std::to_string(operation::getInstance()->gtStation.iSID) + "_fetched=0";

		r = centraldb->SQLSelect(sqlStmt, &tResult, true);
		if (r != 0)
		{
			m_remote_db_err_flag = 1;
			operation::getInstance()->writelog("Download tariff_setup failed.", "DB");
			return ret;
		}
		else if (tResult.size() == 0)
		{
			m_remote_db_err_flag = 0;
			operation::getInstance()->writelog("Tariff download already.", "DB");
			return -3;
		}
	}

	r = localdb->SQLExecutNoneQuery("DELETE FROM tariff_setup");
	if (r != 0)
	{
		m_local_db_err_flag = 1;
		operation::getInstance()->writelog("Delete tariff_setup from local failed.", "DB");
		return -1;
	}
	else
	{
		m_local_db_err_flag = 0;
	}

	if (operation::getInstance()->gtStation.iZoneID > 0)
	{
		iZoneID = operation::getInstance()->gtStation.iZoneID;
	}
	else
	{
		iZoneID = 1;
	}

	operation::getInstance()->writelog("Download tariff_setup for group " + std::to_string(iGrpID) + ", zone " + std::to_string(iZoneID), "DB");
	sqlStmt = "";
	sqlStmt = "SELECT * FROM tariff_setup";
	if (iGrpID > 0)
	{
		sqlStmt = sqlStmt + " WHERE group_id=" + std::to_string(iGrpID) + " AND Zone_id=" + std::to_string(iZoneID);
	}

	r = centraldb->SQLSelect(sqlStmt, &selResult, true);
	if (r != 0)
	{
		m_remote_db_err_flag = 1;
		operation::getInstance()->writelog("Download tariff_setup failed.", "DB");
		return ret;
	}
	else
	{
		m_remote_db_err_flag = 0;
	}

	int downloadCount = 0;
	if (selResult.size() > 0)
	{
		for (int j = 0; j < selResult.size(); j++)
		{
			tariff_struct tariff;
			tariff.tariff_id = selResult[j].GetDataItem(0);
			tariff.day_index = selResult[j].GetDataItem(4);
			tariff.day_type = selResult[j].GetDataItem(5);

			int idx = 6;
			for (int k = 0; k < 9; k++)
			{
				tariff.start_time[k] = selResult[j].GetDataItem(idx++);
				tariff.end_time[k] = selResult[j].GetDataItem(idx++);
				tariff.rate_type[k] = selResult[j].GetDataItem(idx++);
				tariff.charge_time_block[k] = selResult[j].GetDataItem(idx++);
				tariff.charge_rate[k] = selResult[j].GetDataItem(idx++);
				tariff.grace_time[k] = selResult[j].GetDataItem(idx++);
				tariff.first_free[k] = selResult[j].GetDataItem(idx++);
				tariff.first_add[k] = selResult[j].GetDataItem(idx++);
				tariff.second_free[k] = selResult[j].GetDataItem(idx++);
				tariff.second_add[k] = selResult[j].GetDataItem(idx++);
				tariff.third_free[k] = selResult[j].GetDataItem(idx++);
				tariff.third_add[k] = selResult[j].GetDataItem(idx++);
				tariff.allowance[k] = selResult[j].GetDataItem(idx++);
				tariff.min_charge[k] = selResult[j].GetDataItem(idx++);
				tariff.max_charge[k] = selResult[j].GetDataItem(idx++);
			}
			tariff.zone_cutoff = selResult[j].GetDataItem(idx++);
			tariff.day_cutoff = selResult[j].GetDataItem(idx++);
			tariff.whole_day_max = selResult[j].GetDataItem(idx++);
			tariff.whole_day_min = selResult[j].GetDataItem(idx++);

            w = writetariffsetup2local(tariff);

            if (w == 0)
            {
                downloadCount++;
            }
		}

        dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading tariff_setup Records: End, Total Record :" << selResult.size() << " ,Downloaded Record :" << downloadCount;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}

    if (iCheckStatus == 1)
    {
        sqlStmt = "";
        sqlStmt = "UPDATE parameter_mst set s" + std::to_string(operation::getInstance()->gtStation.iSID) + "_fetched=1";
        sqlStmt = sqlStmt + " WHERE name='DownloadTariff'";
        
        r = centraldb->SQLExecutNoneQuery(sqlStmt);
        if (r != 0)
        {
            m_remote_db_err_flag = 1;
            operation::getInstance()->writelog("Set DownloadTariff fetched=1 failed.", "DB");
        }
    }

    if (selResult.size() < 1)
    {
        // no record
        ret = -1;
    }
    else
    {
        ret = downloadCount;
    }

	return ret;
}

int db::writetariffsetup2local(tariff_struct& tariff)
{
    int r = -1;
    std::string sqlStmt;
    std::stringstream dbss;

    try
    {
        sqlStmt = "INSERT INTO tariff_setup";
        sqlStmt = sqlStmt + " (tariff_id, day_index, day_type";
        sqlStmt = sqlStmt + ", start_time1, end_time1, rate_type1, charge_time_block1";
        sqlStmt = sqlStmt + ", charge_rate1, grace_time1, min_charge1, max_charge1";
        sqlStmt = sqlStmt + ", first_free1, first_add1, second_free1, second_add1";
        sqlStmt = sqlStmt + ", third_free1, third_add1, allowance1";
        sqlStmt = sqlStmt + ", start_time2, end_time2, rate_type2, charge_time_block2";
        sqlStmt = sqlStmt + ", charge_rate2, grace_time2, min_charge2, max_charge2";
        sqlStmt = sqlStmt + ", first_free2, first_add2, second_free2, second_add2";
        sqlStmt = sqlStmt + ", third_free2, third_add2, allowance2";
        sqlStmt = sqlStmt + ", start_time3, end_time3, rate_type3, charge_time_block3";
        sqlStmt = sqlStmt + ", charge_rate3, grace_time3, min_charge3, max_charge3";
        sqlStmt = sqlStmt + ", first_free3, first_add3, second_free3, second_add3";
        sqlStmt = sqlStmt + ", third_free3, third_add3, allowance3";
        sqlStmt = sqlStmt + ", start_time4, end_time4, rate_type4, charge_time_block4";
        sqlStmt = sqlStmt + ", charge_rate4, grace_time4, min_charge4, max_charge4";
        sqlStmt = sqlStmt + ", first_free4, first_add4, second_free4, second_add4";
        sqlStmt = sqlStmt + ", third_free4, third_add4, allowance4";
        sqlStmt = sqlStmt + ", start_time5, end_time5, rate_type5, charge_time_block5";
        sqlStmt = sqlStmt + ", charge_rate5, grace_time5, min_charge5, max_charge5";
        sqlStmt = sqlStmt + ", first_free5, first_add5, second_free5, second_add5";
        sqlStmt = sqlStmt + ", third_free5, third_add5, allowance5";
        sqlStmt = sqlStmt + ", start_time6, end_time6, rate_type6, charge_time_block6";
        sqlStmt = sqlStmt + ", charge_rate6, grace_time6, min_charge6, max_charge6";
        sqlStmt = sqlStmt + ", first_free6, first_add6, second_free6, second_add6";
        sqlStmt = sqlStmt + ", third_free6, third_add6, allowance6";
        sqlStmt = sqlStmt + ", start_time7, end_time7, rate_type7, charge_time_block7";
        sqlStmt = sqlStmt + ", charge_rate7, grace_time7, min_charge7, max_charge7";
        sqlStmt = sqlStmt + ", first_free7, first_add7, second_free7, second_add7";
        sqlStmt = sqlStmt + ", third_free7, third_add7, allowance7";
        sqlStmt = sqlStmt + ", start_time8, end_time8, rate_type8, charge_time_block8";
        sqlStmt = sqlStmt + ", charge_rate8, grace_time8, min_charge8, max_charge8";
        sqlStmt = sqlStmt + ", first_free8, first_add8, second_free8, second_add8";
        sqlStmt = sqlStmt + ", third_free8, third_add8, allowance8";
        sqlStmt = sqlStmt + ", start_time9, end_time9, rate_type9, charge_time_block9";
        sqlStmt = sqlStmt + ", charge_rate9, grace_time9, min_charge9, max_charge9";
        sqlStmt = sqlStmt + ", first_free9, first_add9, second_free9, second_add9";
        sqlStmt = sqlStmt + ", third_free9, third_add9, allowance9";
        sqlStmt = sqlStmt + ", zone_cutoff, day_cutoff, whole_day_max, whole_day_min";
        sqlStmt = sqlStmt + ")";
        sqlStmt = sqlStmt + " VALUES (" + tariff.tariff_id;
        sqlStmt = sqlStmt + ", " + tariff.day_index;
        sqlStmt = sqlStmt + ", '" + tariff.day_type + "'";

        for (int i = 0; i < 9; i ++)
        {
            sqlStmt = sqlStmt + ", '" + tariff.start_time[i] + "'";
            sqlStmt = sqlStmt + ", '" + tariff.end_time[i] + "'";
            sqlStmt = sqlStmt + ", " + tariff.rate_type[i];
            sqlStmt = sqlStmt + ", " + tariff.charge_time_block[i];
            sqlStmt = sqlStmt + ", '" + tariff.charge_rate[i] + "'";
            sqlStmt = sqlStmt + ", " + tariff.grace_time[i];
            sqlStmt = sqlStmt + ", " + tariff.first_free[i];
            sqlStmt = sqlStmt + ", '" + tariff.first_add[i] + "'";
            sqlStmt = sqlStmt + ", " + tariff.second_free[i];
            sqlStmt = sqlStmt + ", '" + tariff.second_add[i] + "'";
            sqlStmt = sqlStmt + ", " + tariff.third_free[i];
            sqlStmt = sqlStmt + ", '" + tariff.third_add[i] + "'";
            sqlStmt = sqlStmt + ", " + tariff.allowance[i];
            sqlStmt = sqlStmt + ", '" + tariff.min_charge[i] + "'";
            sqlStmt = sqlStmt + ", '" + tariff.max_charge[i] + "'";
        }

        sqlStmt = sqlStmt + ", " + tariff.zone_cutoff;
        sqlStmt = sqlStmt + ", " + tariff.day_cutoff;
        sqlStmt = sqlStmt + ", '" + tariff.whole_day_max + "'";
        sqlStmt = sqlStmt + ", '" + tariff.whole_day_min + "'";
        sqlStmt = sqlStmt + ")";

        r = localdb->SQLExecutNoneQuery(sqlStmt);
        if (r != 0)
        {
            m_local_db_err_flag = 1;
            dbss.str("");
            dbss.clear();
            dbss << "Insert tariff setup to local failed.";
            Logger::getInstance()->FnLog(dbss.str(), "", "DB");
        }
        else
        {
            m_local_db_err_flag = 0;
        }
    }
    catch (const std::exception& e)
    {
        r = -1;
        dbss.str("");
        dbss.clear();
        dbss << "DB: local db error in writing rec: " << std::string(e.what());
        Logger::getInstance()->FnLog(dbss.str(), "", "DB");
        m_local_db_err_flag = 1;
    }

    return r;
}

int db::downloadtarifftypeinfo()
{
    int ret = -1;
    std::vector<ReaderItem> selResult;
    std::string sqlStmt;
    std::stringstream dbss;
    int r = -1;
    int w = -1;

    r = localdb->SQLExecutNoneQuery("DELETE FROM tariff_type_info");
    if (r != 0)
    {
        m_local_db_err_flag = 1;
        operation::getInstance()->writelog("Delete tariff_type_info from local failed.", "DB");
        return ret;
    }
    else
    {
        m_local_db_err_flag = 0;
    }

    operation::getInstance()->writelog("Download tariff_type_info.", "DB");
    r = centraldb->SQLSelect("SELECT * FROM tariff_type_info", &selResult, true);
    if (r != 0)
    {
        m_remote_db_err_flag = 1;
        operation::getInstance()->writelog("Download tariff_type_info failed.", "DB");
        return ret;
    
    }
    else
    {
        m_remote_db_err_flag = 0;
    }

    int downloadCount = 0;
    if (selResult.size() > 0)
    {
        for (int j = 0; j < selResult.size(); j++)
        {
            tariff_type_info_struct tariff_type;
            tariff_type.tariff_type = selResult[j].GetDataItem(0);
            tariff_type.start_time = selResult[j].GetDataItem(1);
            tariff_type.end_time = selResult[j].GetDataItem(2);

            w = writetarifftypeinfo2local(tariff_type);

            if (w == 0)
            {
                downloadCount++;
            }
        }

        dbss.str("");   // Set the underlying string to an empty string
        dbss.clear();   // Clear the state of the stream
        dbss << "Downloading tariff_type_info Records: End, Total Record :" << selResult.size() << " ,Downloaded Record :" << downloadCount;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
    }

    if (selResult.size() < 1)
    {
        ret = -1;
    }
    else
    {
        ret = downloadCount;
    }

    return ret;
}

int db::writetarifftypeinfo2local(tariff_type_info_struct& tariff_type)
{
    int r = -1;
    std::string sqlStmt;
    std::stringstream dbss;

    try
    {
        sqlStmt = "INSERT INTO tariff_type_info";
        sqlStmt = sqlStmt + " (tariff_type, start_time, end_time)";
        sqlStmt = sqlStmt + " VALUES (" + tariff_type.tariff_type;
        sqlStmt = sqlStmt + ", '" + tariff_type.start_time + "'";
        sqlStmt = sqlStmt + ", '" + tariff_type.end_time + "'";
        sqlStmt = sqlStmt + ")";

        r = localdb->SQLExecutNoneQuery(sqlStmt);
        if (r != 0)
        {
            m_local_db_err_flag = 1;
            dbss.str("");
            dbss.clear();
            dbss << "Insert tariff type info to local failed.";
            Logger::getInstance()->FnLog(dbss.str(), "", "DB");
        }
        else
        {
            m_local_db_err_flag = 0;
        }
    }
    catch (const std::exception& e)
    {
        r = -1;
        dbss.str("");
        dbss.clear();
        dbss << "DB: local db error in writing rec: " << std::string(e.what());
        Logger::getInstance()->FnLog(dbss.str(), "", "DB");
        m_local_db_err_flag = 1;
    }

    return r;
}

int db::downloadxtariff(int iGrpID, int iSiteID, int iCheckStatus)
{
    int ret = -1;
    std::vector<ReaderItem> tResult;
    std::vector<ReaderItem> selResult;
    std::string sqlStmt;
    std::stringstream dbss;
    int r = -1;
    int w = -1;

    if (iCheckStatus == 1)
    {
        // Check x tariff is downloaded or not
        sqlStmt = "SELECT name FROM parameter_mst ";
        sqlStmt = sqlStmt + "WHERE name = 'DownloadXTariff' AND s" + std::to_string(operation::getInstance()->gtStation.iSID) + "_fetched=0";

        r = centraldb->SQLSelect(sqlStmt, &tResult, true);
        if (r != 0)
        {
            m_remote_db_err_flag = 1;
            operation::getInstance()->writelog("Download X_Tariff failed.", "DB");
            return ret;
        }
        else if (tResult.size() == 0)
        {
            m_remote_db_err_flag = 0;
            operation::getInstance()->writelog("X_Tariff download already.", "DB");
            return -3;
        }
    }

    r = localdb->SQLExecutNoneQuery("DELETE FROM X_Tariff");
    if (r != 0)
    {
        m_local_db_err_flag = 1;
        operation::getInstance()->writelog("Delete X_Tariff from local failed.", "DB");
        return -1;
    }
    else
    {
        m_local_db_err_flag = 0;
    }

    operation::getInstance()->writelog("Download X_Tariff for group" + std::to_string(iGrpID) + ", site " + std::to_string(iSiteID), "DB");
    sqlStmt = "";
    sqlStmt = "SELECT * FROM X_Tariff";
    sqlStmt = sqlStmt + " WHERE group_id=" + std::to_string(iGrpID) + " AND site_id=" + std::to_string(iSiteID);

    r = centraldb->SQLSelect(sqlStmt, &selResult, true);
    if (r != 0)
    {
        m_remote_db_err_flag = 1;
        operation::getInstance()->writelog("Download X_Tariff failed.", "DB");
        return ret;
    }
    else
    {
        m_remote_db_err_flag = 0;
    }

    int downloadCount = 0;
    if (selResult.size() > 0)
    {
        for (int j = 0; j < selResult.size(); j++)
        {
            x_tariff_struct x_tariff;
            x_tariff.day_index = selResult[j].GetDataItem(2);
            x_tariff.auto0 = selResult[j].GetDataItem(3);
            x_tariff.fee0 = selResult[j].GetDataItem(4);
            x_tariff.time1 = selResult[j].GetDataItem(5);
            x_tariff.auto1 = selResult[j].GetDataItem(6);
            x_tariff.fee1 = selResult[j].GetDataItem(7);
            x_tariff.time2 = selResult[j].GetDataItem(8);
            x_tariff.auto2 = selResult[j].GetDataItem(9);
            x_tariff.fee2 = selResult[j].GetDataItem(10);
            x_tariff.time3 = selResult[j].GetDataItem(11);
            x_tariff.auto3 = selResult[j].GetDataItem(12);
            x_tariff.fee3 = selResult[j].GetDataItem(13);
            x_tariff.time4 = selResult[j].GetDataItem(14);
            x_tariff.auto4 = selResult[j].GetDataItem(15);
            x_tariff.fee4 = selResult[j].GetDataItem(16);

            w = writextariff2local(x_tariff);

            if (w == 0)
            {
                downloadCount++;
            }
        }

        dbss.str("");  // Set the underlying string to an empty string
        dbss.clear();   // Clear the state of the stream
        dbss << "Downloading x_tariff Records: End, Total Record :" << selResult.size() << " ,Downloaded Record :" << downloadCount;
        Logger::getInstance()->FnLog(dbss.str(), "", "DB");
    }

    if (iCheckStatus == 1)
    {
        sqlStmt = "";
        sqlStmt = "UPDATE parameter_mst set s" + std::to_string(operation::getInstance()->gtStation.iSID) + "_fetched=1";
        sqlStmt = sqlStmt + " WHERE name='DownloadXTariff'";
        
        r = centraldb->SQLExecutNoneQuery(sqlStmt);
        if (r != 0)
        {
            m_remote_db_err_flag = 1;
            operation::getInstance()->writelog("Set DownloadXTariff fetched=1 failed.", "DB");
        }
    }

    if (selResult.size() < 1)
    {
        ret = -1;
    }
    else
    {
        ret = downloadCount;
    }

    return ret;
}

int db::writextariff2local(x_tariff_struct& x_tariff)
{
    int r = -1;
    std::string sqlStmt;
    std::stringstream dbss;

    try
    {
        sqlStmt = "INSERT INTO X_Tariff";
        sqlStmt = sqlStmt + "(day_index, auto0, fee0, time1, auto1, fee1";
        sqlStmt = sqlStmt + ", time2, auto2, fee2, time3, auto3, fee3, time4, auto4, fee4";
        sqlStmt = sqlStmt + ")";
        sqlStmt = sqlStmt + " VALUES ('" + x_tariff.day_index + "'";
        sqlStmt = sqlStmt + ", " + x_tariff.auto0;
        sqlStmt = sqlStmt + ", " + x_tariff.fee0;
        sqlStmt = sqlStmt + ", '" + x_tariff.time1 + "'";
        sqlStmt = sqlStmt + ", " + x_tariff.auto1;
        sqlStmt = sqlStmt + ", " + x_tariff.fee1;
        sqlStmt = sqlStmt + ", '" + x_tariff.time2 + "'";
        sqlStmt = sqlStmt + ", " + x_tariff.auto2;
        sqlStmt = sqlStmt + ", " + x_tariff.fee2;
        sqlStmt = sqlStmt + ", '" + x_tariff.time3 + "'";
        sqlStmt = sqlStmt + ", " + x_tariff.auto3;
        sqlStmt = sqlStmt + ", " + x_tariff.fee3;
        sqlStmt = sqlStmt + ", '" + x_tariff.time4 + "'";
        sqlStmt = sqlStmt + ", " + x_tariff.auto4;
        sqlStmt = sqlStmt + ", " + x_tariff.fee4;
        sqlStmt = sqlStmt + ")";

        r = localdb->SQLExecutNoneQuery(sqlStmt);
        if (r != 0)
        {
            m_local_db_err_flag = 1;
            dbss.str("");
            dbss.clear();
            dbss << "Insert X_Tariff to local failed.";
            Logger::getInstance()->FnLog(dbss.str(), "", "DB");
        }
        else
        {
            m_local_db_err_flag = 0;
        }
    }
    catch (const std::exception& e)
    {
        r = -1;
        dbss.str("");
        dbss.clear();
        dbss << "DB: local db error in writing rec: " << std::string(e.what());
        Logger::getInstance()->FnLog(dbss.str(), "", "DB");
        m_local_db_err_flag = 1;
    }

    return r;
}

int db::downloadholidaymst(int iCheckStatus)
{
    int ret = -1;
    std::vector<ReaderItem> tResult;
    std::vector<ReaderItem> selResult;
    std::string sqlStmt;
    std::stringstream dbss;
    int r = -1;
    int w = -1;

    if (iCheckStatus == 1)
    {
        // Check Whether holiday mst is downloaded or not
        sqlStmt = "SELECT name FROM parameter_mst ";
        sqlStmt = sqlStmt + "WHERE name='DownloadHoliday' AND s" + std::to_string(operation::getInstance()->gtStation.iSID) + "_fetched=0";
        
        r = centraldb->SQLSelect(sqlStmt, &tResult, true);
        if (r != 0)
        {
            m_remote_db_err_flag = 1;
            operation::getInstance()->writelog("Download holiday_mst failed.", "DB");
            return ret;
        }
        else if (tResult.size() == 0)
        {
            m_remote_db_err_flag = 0;
            operation::getInstance()->writelog("holiday_mst download already.", "DB");
            return -3;
        }
    }

    r = localdb->SQLExecutNoneQuery("DELETE FROM holiday_mst");
    if (r != 0)
    {
        m_local_db_err_flag = 1;
        operation::getInstance()->writelog("Delete holiday_mst from local failed.", "DB");
        return -1;
    }
    else
    {
        m_local_db_err_flag = 0;
    }

    operation::getInstance()->writelog("Download holiday_mst.", "DB");
    sqlStmt = "";
    sqlStmt = "SELECT holiday_date, descrip";
    sqlStmt = sqlStmt + " FROM holiday_mst ";
    sqlStmt = sqlStmt + "WHERE holiday_date > GETDATE() - 30";

    r = centraldb->SQLSelect(sqlStmt, &selResult, true);
    if (r != 0)
    {
        m_remote_db_err_flag = 1;
        operation::getInstance()->writelog("Download holiday_mst failed.", "DB");
        return ret;
    }
    else
    {
        m_remote_db_err_flag = 0;
    }

    int downloadCount = 0;
    if (selResult.size() > 0)
    {
        for (int j = 0; j < selResult.size(); j++)
        {
            w = writeholidaymst2local(selResult[j].GetDataItem(0), selResult[j].GetDataItem(1));

            if (w == 0)
            {
                downloadCount++;
            }
        }

        dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading holiday_mst Records: End, Total Record :" << selResult.size() << " ,Downloaded Record :" << downloadCount;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
    }

    if (iCheckStatus == 1)
    {
        sqlStmt = "";
        sqlStmt = "UPDATE parameter_mst set s" + std::to_string(operation::getInstance()->gtStation.iSID) + "_fetched=1";
        sqlStmt = sqlStmt + " WHERE name='DownloadHoliday'";

        r = centraldb->SQLExecutNoneQuery(sqlStmt);
        if (r != 0)
        {
            m_remote_db_err_flag = 1;
            operation::getInstance()->writelog("Set DownloadHoliday fetched=1 failed.", "DB");
        }
    }

    if (selResult.size() < 1)
    {
        ret = -1;
    }
    else
    {
        ret = downloadCount;
    }

    return ret;
}

int db::writeholidaymst2local(std::string holiday_date, std::string descrip)
{
    int r = -1;
    std::string sqlStmt;
    std::stringstream dbss;

    try
    {
        sqlStmt = "INSERT INTO holiday_mst";
        sqlStmt = sqlStmt + " (holiday_date, descrip)";
        sqlStmt = sqlStmt + " VALUES ('" + holiday_date + "'";
        sqlStmt = sqlStmt + ", '" + descrip + "')";

        r = localdb->SQLExecutNoneQuery(sqlStmt);
        if (r != 0)
        {
            m_local_db_err_flag = 1;
            dbss.str("");
            dbss.clear();
            dbss << "Insert holiday to local failed.";
            Logger::getInstance()->FnLog(dbss.str(), "", "DB");
        }
        else
        {
            m_local_db_err_flag = 0;
        }
    }
    catch (const std::exception& e)
    {
        r = -1;
        dbss.str("");
        dbss.clear();
        dbss << "DB: local db error in writing rec: " << std::string(e.what());
        Logger::getInstance()->FnLog(dbss.str(), "", "DB");
        m_local_db_err_flag = 1;
    }

    return r;
}

int db::download3tariffinfo()
{
    int ret = -1;
    std::vector<ReaderItem> selResult;
    std::string sqlStmt;
    std::stringstream dbss;
    int r = -1;
    int w = -1;

    r = localdb->SQLExecutNoneQuery("DELETE FROM 3Tariff_Info");
    if (r != 0)
    {
        m_local_db_err_flag = 1;
        operation::getInstance()->writelog("Delete 3Tariff_Info from local failed.", "DB");
        return -1;
    }
    else
    {
        m_local_db_err_flag = 0;
    }

    int zone_id = operation::getInstance()->gtStation.iZoneID;
    operation::getInstance()->writelog("Download 3Tariff_Info.", "DB");
    sqlStmt = "";
    sqlStmt = "SELECT * FROM [3Tariff_Info]";
    sqlStmt = sqlStmt + " WHERE Zone_ID='" + std::to_string(zone_id) + "'";
    sqlStmt = sqlStmt + " OR Zone_ID LIKE '" + std::to_string(zone_id) + ",%'";
    sqlStmt = sqlStmt + " OR Zone_ID LIKE '%," + std::to_string(zone_id) + ",%'";
    sqlStmt = sqlStmt + " OR Zone_ID LIKE '%," + std::to_string(zone_id) + "'";

    r = centraldb->SQLSelect(sqlStmt, &selResult, true);
    if (r != 0)
    {
        m_remote_db_err_flag = 1;
        operation::getInstance()->writelog("Download 3Tariff_Info failed.", "DB");
        return ret;
    }
    else
    {
        m_remote_db_err_flag = 0;
    }

    int downloadCount = 0;
    if (selResult.size() > 0)
    {
        for (int j = 0; j < selResult.size(); j++)
        {
            tariff_info_struct tariff_info;
            tariff_info.rate_type = selResult[j].GetDataItem(1);
            tariff_info.day_type = selResult[j].GetDataItem(2);
            tariff_info.time_from = selResult[j].GetDataItem(3);
            tariff_info.time_till = selResult[j].GetDataItem(4);
            tariff_info.t3_start = selResult[j].GetDataItem(5);
            tariff_info.t3_block = selResult[j].GetDataItem(6);
            tariff_info.t3_rate = selResult[j].GetDataItem(7);

            w = write3tariffinfo2local(tariff_info);

            if (w == 0)
            {
                downloadCount++;
            }
        }

        dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading 3Tariff_Info Records: End, Total Record :" << selResult.size() << " ,Downloaded Record :" << downloadCount;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
    }

    if (selResult.size() < 1)
    {
        ret = -1;
    }
    else
    {
        ret = downloadCount;
    }

    return ret;
}

int db::write3tariffinfo2local(tariff_info_struct& tariff_info)
{
    int r = -1;
    std::string sqlStmt;
    std::stringstream dbss;

    try
    {
        sqlStmt = "INSERT INTO 3Tariff_Info";
        sqlStmt = sqlStmt + " (Rate_Type, Day_Type, Time_From, Time_Till, T3_Start, T3_Block, T3_Rate)";
        sqlStmt = sqlStmt + " VALUES(" + tariff_info.rate_type;
        sqlStmt = sqlStmt + ", '" + tariff_info.day_type + "'";
        sqlStmt = sqlStmt + ", '" + tariff_info.time_from + "'";
        sqlStmt = sqlStmt + ", '" + tariff_info.time_till + "'";
        sqlStmt = sqlStmt + ", " + tariff_info.t3_start;
        sqlStmt = sqlStmt + ", " + tariff_info.t3_block;
        sqlStmt = sqlStmt + ", " + tariff_info.t3_rate + ")";

        r = localdb->SQLExecutNoneQuery(sqlStmt);
        if (r != 0)
        {
            m_local_db_err_flag = 1;
            dbss.str("");
            dbss.clear();
            dbss << "Insert 3Tariff_Info to local failed.";
            Logger::getInstance()->FnLog(dbss.str(), "", "DB");
        }
        else
        {
            m_local_db_err_flag = 0;
        }
    }
    catch (const std::exception& e)
    {
        r = -1;
        dbss.str("");
        dbss.clear();
        dbss << "DB: local db error in writing rec: " << std::string(e.what());
        Logger::getInstance()->FnLog(dbss.str(), "", "DB");
        m_local_db_err_flag = 1;
    }

    return r;
}

int db::downloadratefreeinfo(int iCheckStatus)
{
    int ret = -1;
    std::vector<ReaderItem> tResult;
    std::vector<ReaderItem> selResult;
    std::string sqlStmt;
    std::stringstream dbss;
    int r = -1;
    int w = -1;

    if (iCheckStatus == 1)
    {
        // Check Whether rate free info is downloaded or not
        sqlStmt = "SELECT name FROM parameter_mst";
        sqlStmt = sqlStmt + " WHERE name='DownloadRateFreeInfo' AND s" + std::to_string(operation::getInstance()->gtStation.iSID) + "_fetched=0";

        r = centraldb->SQLSelect(sqlStmt, &tResult, true);
        if (r != 0)
        {
            m_remote_db_err_flag = 1;
            operation::getInstance()->writelog("Download Rate_Free_Info failed.", "DB");
            return ret;
        }
        else if (tResult.size() == 0)
        {
            m_remote_db_err_flag = 0;
            operation::getInstance()->writelog("Rate_Free_Info download already.", "DB");
            return -3;
        }
    }

    r = localdb->SQLExecutNoneQuery("DELETE FROM Rate_Free_Info");
    if (r != 0)
    {
        m_local_db_err_flag = 1;
        operation::getInstance()->writelog("Delete Rate_Free_Info from local failed.", "DB");
        return -1;
    }
    else
    {
        m_local_db_err_flag = 0;
    }

    int zone_id = operation::getInstance()->gtStation.iZoneID;
    operation::getInstance()->writelog("Download Rate_Free_Info.", "DB");
    sqlStmt = "";
    sqlStmt = "SELECT Rate_Type, Day_Type, Init_Free, Free_Beg, Free_End, Free_Time FROM Rate_Free_Info";
    sqlStmt = sqlStmt + " WHERE Zone_ID='" + std::to_string(zone_id) + "'";
    sqlStmt = sqlStmt + " OR Zone_ID LIKE '" + std::to_string(zone_id) + ",%'";
    sqlStmt = sqlStmt + " OR Zone_ID LIKE '%," + std::to_string(zone_id) + ",%'";
    sqlStmt = sqlStmt + " OR Zone_ID LIKE '%," + std::to_string(zone_id) + "'";

    r = centraldb->SQLSelect(sqlStmt, &selResult, true);
    if (r != 0)
    {
        m_remote_db_err_flag = 1;
        operation::getInstance()->writelog("Download Rate_Free_Info failed.", "DB");
        return ret;
    }
    else
    {
        m_remote_db_err_flag = 0;
    }

    int downloadCount = 0;
    if (selResult.size() > 0)
    {
        for (int j = 0; j < selResult.size(); j++)
        {
            rate_free_info_struct rate_free_info;
            rate_free_info.rate_type = selResult[j].GetDataItem(0);
            rate_free_info.day_type = selResult[j].GetDataItem(1);
            rate_free_info.init_free = selResult[j].GetDataItem(2);
            rate_free_info.free_beg = selResult[j].GetDataItem(3);
            rate_free_info.free_end = selResult[j].GetDataItem(4);
            rate_free_info.free_time = selResult[j].GetDataItem(5);

            w = writeratefreeinfo2local(rate_free_info);

            if (w == 0)
            {
                downloadCount++;
            }
        }

        dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading Rate_Free_Info Records: End, Total Record :" << selResult.size() << " ,Downloaded Record :" << downloadCount;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
    }

    if (iCheckStatus == 1)
    {
        sqlStmt = "";
        sqlStmt = "UPDATE parameter_mst set s" + std::to_string(operation::getInstance()->gtStation.iSID) + "_fetched=1";
        sqlStmt = sqlStmt + " WHERE name='DownloadRateFreeInfo'";

        r = centraldb->SQLExecutNoneQuery(sqlStmt);
        if (r != 0)
        {
            m_remote_db_err_flag = 1;
            operation::getInstance()->writelog("Set DownloadRateFreeInfo fetched = 1 failed.", "DB");
        }
    }

    if (selResult.size() < 1)
    {
        ret = -1;
    }
    else
    {
        ret = downloadCount;
    }

    return ret;
}

int db::writeratefreeinfo2local(rate_free_info_struct& rate_free_info)
{
    int r = -1;
    std::string sqlStmt;
    std::stringstream dbss;

    try
    {
        sqlStmt = "INSERT INTO Rate_Free_Info";
        sqlStmt = sqlStmt + " (Rate_Type, Day_Type, Init_Free, Free_Beg, Free_End, Free_Time)";
        sqlStmt = sqlStmt + " VALUES (" + rate_free_info.rate_type;
        sqlStmt = sqlStmt + ", '" + rate_free_info.day_type + "'";
        sqlStmt = sqlStmt + ", " + rate_free_info.init_free;
        sqlStmt = sqlStmt + ", '" + rate_free_info.free_beg + "'";
        sqlStmt = sqlStmt + ", '" + rate_free_info.free_end + "'";
        sqlStmt = sqlStmt + ", " + rate_free_info.free_time + ")";

        r = localdb->SQLExecutNoneQuery(sqlStmt);
        if (r != 0)
        {
            m_local_db_err_flag = 1;
            dbss.str("");
            dbss.clear();
            dbss << "Insert Rate_Free_Info to local failed.";
            Logger::getInstance()->FnLog(dbss.str(), "", "DB");
        }
        else
        {
            m_local_db_err_flag = 0;
        }
    }
    catch (const std::exception& e)
    {
        r = -1;
        dbss.str("");
        dbss.clear();
        dbss << "DB: local db error in writing rec: " << std::string(e.what());
        Logger::getInstance()->FnLog(dbss.str(), "", "DB");
        m_local_db_err_flag = 1;
    }

    return r;
}

int db::downloadspecialdaymst(int iCheckStatus)
{
    int ret = -1;
    std::vector<ReaderItem> tResult;
    std::vector<ReaderItem> selResult;
    std::string sqlStmt;
    std::stringstream dbss;
    int r = -1;
    int w = -1;

    if (iCheckStatus == 1)
    {
        // Check Whether special day mst is downloaded or not
        sqlStmt = "SELECT name FROM parameter_mst";
        sqlStmt = sqlStmt + " WHERE name='DownloadSpecialDay' AND s" + std::to_string(operation::getInstance()->gtStation.iSID) + "_fetched=0";

        r = centraldb->SQLSelect(sqlStmt, &tResult, true);
        if (r != 0)
        {
            m_remote_db_err_flag = 1;
            operation::getInstance()->writelog("Download Special_Day_mst failed.", "DB");
            return ret;
        }
        else if (tResult.size() == 0)
        {
            m_remote_db_err_flag = 0;
            operation::getInstance()->writelog("Special_Day_mst download already.", "DB");
            return -3;
        }
    }

    r = localdb->SQLExecutNoneQuery("DELETE FROM Special_Day_mst");
    if (r != 0)
    {
        m_local_db_err_flag = 1;
        operation::getInstance()->writelog("Delete Special_Day_mst from local failed.", "DB");
        return -1;
    }
    else
    {
        m_local_db_err_flag = 0;
    }

    int zone_id = operation::getInstance()->gtStation.iZoneID;
    operation::getInstance()->writelog("Download Special_Day_mst.", "DB");
    sqlStmt = "";
    sqlStmt = "SELECT convert(char(10),Special_Date,103), Rate_Type, Day_Code FROM Special_Day_mst";
    sqlStmt = sqlStmt + " WHERE Special_Date > getdate()-1 AND (";
	sqlStmt = sqlStmt + " Zone_ID='" + std::to_string(zone_id) + "'";
    sqlStmt = sqlStmt + " OR Zone_ID LIKE '" + std::to_string(zone_id) + ",%'";
    sqlStmt = sqlStmt + " OR Zone_ID LIKE '%," + std::to_string(zone_id) + ",%'";
    sqlStmt = sqlStmt + " OR Zone_ID LIKE '%," + std::to_string(zone_id) + "'";
	sqlStmt = sqlStmt + ")";

    r = centraldb->SQLSelect(sqlStmt, &selResult, true);
    if (r != 0)
    {
        m_remote_db_err_flag = 1;
        operation::getInstance()->writelog("Download Special_Day_mst failed.", "DB");
        return ret;
    }
    else
    {
        m_remote_db_err_flag = 0;
    }

    int downloadCount = 0;
    if (selResult.size() > 0)
    {
        for (int j = 0; j < selResult.size(); j++)
        {
            w = writespecialday2local(selResult[j].GetDataItem(0), selResult[j].GetDataItem(1), selResult[j].GetDataItem(2));

            if (w == 0)
            {
                downloadCount++;
            }
        }

        dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading Special_Day_mst Records: End, Total Record :" << selResult.size() << " ,Downloaded Record :" << downloadCount;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
    }

    if (iCheckStatus == 1)
    {
        sqlStmt = "";
        sqlStmt = "UPDATE parameter_mst set s" + std::to_string(operation::getInstance()->gtStation.iSID) + "_fetched=1";
        sqlStmt = sqlStmt + " WHERE name='DownloadSpecialDay'";

        r = centraldb->SQLExecutNoneQuery(sqlStmt);
        if (r != 0)
        {
            m_remote_db_err_flag = 1;
            operation::getInstance()->writelog("Set DownloadSpecialDay fetched=1 failed.", "DB");
        }
    }

    if (selResult.size() < 1)
    {
        ret = -1;
    }
    else
    {
        ret = downloadCount;
    }

    return ret;
}

int db::writespecialday2local(std::string special_date, std::string rate_type, std::string day_code)
{
    int r = -1;
    std::string sqlStmt;
    std::stringstream dbss;

    try
    {
        sqlStmt = "INSERT INTO Special_Day_mst";
        sqlStmt = sqlStmt + " (Special_Date, Rate_Type, Day_Code)";
        sqlStmt = sqlStmt + " VALUES('" + special_date + "'";
        sqlStmt = sqlStmt + ", " + rate_type;
        sqlStmt = sqlStmt + ", '" + day_code + "')";

        r = localdb->SQLExecutNoneQuery(sqlStmt);
        if (r != 0)
        {
            m_local_db_err_flag = 1;
            dbss.str("");
            dbss.clear();
            dbss << "Insert Special_Day_mst to local failed.";
            Logger::getInstance()->FnLog(dbss.str(), "", "DB");
        }
        else
        {
            m_local_db_err_flag = 0;
        }
    }
    catch (const std::exception& e)
    {
        r = -1;
        dbss.str("");
        dbss.clear();
        dbss << "DB: local db error in writing rec: " << std::string(e.what());
        Logger::getInstance()->FnLog(dbss.str(), "", "DB");
        m_local_db_err_flag = 1;
    }

    return r;
}

int db::downloadratetypeinfo(int iCheckStatus)
{
    int ret = -1;
    std::vector<ReaderItem> tResult;
    std::vector<ReaderItem> selResult;
    std::string sqlStmt;
    std::stringstream dbss;
    int r = -1;
    int w = -1;

    if (iCheckStatus == 1)
    {
        // Check Whether rate type infor is downloaded or not
        sqlStmt = "SELECT name FROM parameter_mst";
        sqlStmt = sqlStmt + " WHERE name='DownloadRateTypeInfo' AND s" + std::to_string(operation::getInstance()->gtStation.iSID) + "_fetched=0";

        r = centraldb->SQLSelect(sqlStmt, &selResult, true);
        if (r != 0)
        {
            m_remote_db_err_flag = 1;
            operation::getInstance()->writelog("Download Rate_Type_Info failed.", "DB");
            return ret;
        }
        else if (selResult.size() == 0)
        {
            m_remote_db_err_flag = 0;
            operation::getInstance()->writelog("Rate_Type_Info download already.", "DB");
            return -3;
        }
    }

    r = localdb->SQLExecutNoneQuery("DELETE FROM Rate_Type_Info");
    if (r != 0)
    {
        m_local_db_err_flag = 1;
        operation::getInstance()->writelog("Delete Rate_Type_Info from local failed.", "DB");
        return -1;
    }
    else
    {
        m_local_db_err_flag = 0;
    }

    int zone_id = operation::getInstance()->gtStation.iZoneID;
    operation::getInstance()->writelog("Download Rate_Type_Info.", "DB");
    sqlStmt = "";
    sqlStmt = "SELECT Rate_Type, Has_Holiday, Has_Holiday_Eve, Has_Special_Day, Has_Init_Free, Has_3Tariff, Has_Zone_Max FROM Rate_Type_Info";
    sqlStmt = sqlStmt + " WHERE Zone_ID='" + std::to_string(zone_id) + "'";
    sqlStmt = sqlStmt + " OR Zone_ID LIKE '" + std::to_string(zone_id) + ",%'";
    sqlStmt = sqlStmt + " OR Zone_ID LIKE '%," + std::to_string(zone_id) + ",%'";
    sqlStmt = sqlStmt + " OR Zone_ID LIKE '%," + std::to_string(zone_id) + "'";

    r = centraldb->SQLSelect(sqlStmt, &selResult, true);
    if (r != 0)
    {
        m_remote_db_err_flag = 1;
        operation::getInstance()->writelog("Donwload Rate_Type_Info failed.", "DB");
        return ret;
    }
    else
    {
        m_remote_db_err_flag = 0;
    }

    int downloadCount = 0;
    if (selResult.size() > 0)
    {
        for (int j = 0; j < selResult.size(); j++)
        {
            rate_type_info_struct rate_type_info;
            rate_type_info.rate_type = selResult[j].GetDataItem(0);
            rate_type_info.has_holiday = selResult[j].GetDataItem(1);
            rate_type_info.has_holiday_eve = selResult[j].GetDataItem(2);
            rate_type_info.has_special_day = selResult[j].GetDataItem(3);
            rate_type_info.has_init_free = selResult[j].GetDataItem(4);
            rate_type_info.has_3tariff = selResult[j].GetDataItem(5);
            rate_type_info.has_zone_max = selResult[j].GetDataItem(6);
            //rate_type_info.has_firstentry_rate = selResult[j].GetDataItem(7);

            w = writeratetypeinfo2local(rate_type_info);

            if (w == 0)
            {
                downloadCount++;
            }
        }

        dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading Rate_Type_Info Records: End, Total Record :" << selResult.size() << " ,Downloaded Record :" << downloadCount;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
    }

    if (iCheckStatus == 1)
    {
        sqlStmt = "";
        sqlStmt = "UPDATE parameter_mst set s" + std::to_string(operation::getInstance()->gtStation.iSID) + "_fetched=1";
        sqlStmt = sqlStmt + " WHERE name='DownloadRateTypeInfo'";

        r = centraldb->SQLExecutNoneQuery(sqlStmt);
        if (r != 0)
        {
            m_remote_db_err_flag = 1;
            operation::getInstance()->writelog("Set DownloadRateTypeInfo fetched = 1 failed.", "DB");
        }
    }

    if (selResult.size() < 1)
    {
        ret = -1;
    }
    else
    {
        ret = downloadCount;
    }

    return ret;
}

int db::writeratetypeinfo2local(rate_type_info_struct rate_type_info)
{
    int r = -1;
    std::string sqlStmt;
    std::stringstream dbss;

    try
    {
        sqlStmt = "INSERT INTO Rate_Type_Info";
        sqlStmt = sqlStmt + " (Rate_Type, Has_Holiday, Has_Holiday_Eve, Has_Special_Day, Has_Init_Free, Has_3Tariff, Has_Zone_Max)";
        sqlStmt = sqlStmt + " VALUES(" + rate_type_info.rate_type;
        sqlStmt = sqlStmt + ", " + rate_type_info.has_holiday;
        sqlStmt = sqlStmt + ", " + rate_type_info.has_holiday_eve;
        sqlStmt = sqlStmt + ", " + rate_type_info.has_special_day;
        sqlStmt = sqlStmt + ", " + rate_type_info.has_init_free;
        sqlStmt = sqlStmt + ", " + rate_type_info.has_3tariff;
        sqlStmt = sqlStmt + ", " + rate_type_info.has_zone_max;
        sqlStmt = sqlStmt + ")";

        r = localdb->SQLExecutNoneQuery(sqlStmt);
        if (r != 0)
        {
            m_local_db_err_flag = 1;
            dbss.str();
            dbss.clear();
            dbss << "Insert Rate_Type_Info to local failed.";
            Logger::getInstance()->FnLog(dbss.str(), "", "DB");
        }
        else
        {
            m_local_db_err_flag = 0;
        }
    }
    catch (const std::exception& e)
    {
        r = -1;
        dbss.str("");
        dbss.clear();
        dbss << "DB: local db error in writing rec: " << std::string(e.what());
        Logger::getInstance()->FnLog(dbss.str(), "", "DB");
        m_local_db_err_flag = 1;
    }

    return r;
}

int db::downloadratemaxinfo(int iCheckStatus)
{
    int ret = -1;
    std::vector<ReaderItem> tResult;
    std::vector<ReaderItem> selResult;
    std::string sqlStmt;
    std::stringstream dbss;
    int r = -1;
    int w = -1;

    if (iCheckStatus == 1)
    {
        // Check Whether rate max info is downloaded or not
        sqlStmt = "SELECT name FROM parameter_mst";
        sqlStmt = sqlStmt + " WHERE name='DownloadRateMaxInfo' AND s" + std::to_string(operation::getInstance()->gtStation.iSID) + "_fetched=0";

        r = centraldb->SQLSelect(sqlStmt, &selResult, true);
        if (r != 0)
        {
            m_remote_db_err_flag = 1;
            operation::getInstance()->writelog("Download Rate_Max_Info failed.", "DB");
            return ret;
        }
        else if (selResult.size() == 0)
        {
            m_remote_db_err_flag = 0;
            operation::getInstance()->writelog("Rate_Max_Info download already.", "DB");
            return -3;
        }
    }

    r = localdb->SQLExecutNoneQuery("DELETE FROM Rate_Max_Info");
    if (r != 0)
    {
        m_local_db_err_flag = 1;
        operation::getInstance()->writelog("Delete Rate_Max_Info from local failed.", "DB");
        return -1;
    }
    else
    {
        m_local_db_err_flag = 0;
    }

    int zone_id = operation::getInstance()->gtStation.iZoneID;
    operation::getInstance()->writelog("Download Rate_Max_Info.", "DB");
    sqlStmt = "";
    sqlStmt = "Select Rate_Type, Day_Type, Start_Time, End_Time, Max_Fee FROM Rate_Max_Info";
    sqlStmt = sqlStmt + " WHERE Zone_ID='" + std::to_string(zone_id) + "'";
    sqlStmt = sqlStmt + " OR Zone_ID LIKE '" + std::to_string(zone_id) + ",%'";
    sqlStmt = sqlStmt + " OR Zone_ID LIKE '%," + std::to_string(zone_id) + ",%'";
    sqlStmt = sqlStmt + " OR Zone_ID LIKE '%," + std::to_string(zone_id) + "'";

    r = centraldb->SQLSelect(sqlStmt, &selResult, true);
    if (r != 0)
    {
        m_remote_db_err_flag = 1;
        operation::getInstance()->writelog("Donwload Rate_Max_Info failed.", "DB");
        return ret;
    }
    else
    {
        m_remote_db_err_flag = 0;
    }

    int downloadCount = 0;
    if (selResult.size() > 0)
    {
        for (int j = 0; j < selResult.size(); j++)
        {
            rate_max_info_struct rate_max_info;
            rate_max_info.rate_type = selResult[j].GetDataItem(0);
            rate_max_info.day_type = selResult[j].GetDataItem(1);
            rate_max_info.start_time = selResult[j].GetDataItem(2);
            rate_max_info.end_time = selResult[j].GetDataItem(3);
            rate_max_info.max_fee = selResult[j].GetDataItem(4);

            w = writeratemaxinfo2local(rate_max_info);

            if (w == 0)
            {
                downloadCount++;
            }
        }

        dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading Rate_Max_Info Records: End, Total Record :" << selResult.size() << " ,Downloaded Record :" << downloadCount;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
    }

    if (iCheckStatus == 1)
    {
        sqlStmt = "";
        sqlStmt = "UPDATE parameter_mst set s" + std::to_string(operation::getInstance()->gtStation.iSID) + "_fetched=1";
        sqlStmt = sqlStmt + " WHERE name='DownloadRateMaxInfo'";

        r = centraldb->SQLExecutNoneQuery(sqlStmt);
        if (r != 0)
        {
            m_remote_db_err_flag = 1;
            operation::getInstance()->writelog("Set DownloadRateMaxInfo fetched = 1 failed.", "DB");
        }
    }

    if (selResult.size() < 1)
    {
        ret = -1;
    }
    else
    {
        ret = downloadCount;
    }

    return ret;
}

int db::writeratemaxinfo2local(rate_max_info_struct rate_max_info)
{
    int r = -1;
    std::string sqlStmt;
    std::stringstream dbss;

    try
    {
        sqlStmt = "INSERT INTO Rate_Max_Info";
        sqlStmt = sqlStmt + " (Rate_Type, Day_Type, Start_Time, End_Time, Max_Fee)";
        sqlStmt = sqlStmt + " VALUES(" + rate_max_info.rate_type;
        sqlStmt = sqlStmt + ", '" + rate_max_info.day_type + "'";
        sqlStmt = sqlStmt + ", '" + rate_max_info.start_time + "'";
        sqlStmt = sqlStmt + ", '" + rate_max_info.end_time + "'";
        sqlStmt = sqlStmt + ", " + rate_max_info.max_fee;
        sqlStmt = sqlStmt + ")";

        r = localdb->SQLExecutNoneQuery(sqlStmt);
        if (r != 0)
        {
            m_local_db_err_flag = 1;
            dbss.str();
            dbss.clear();
            dbss << "Insert Rate_Type_Info to local failed.";
            Logger::getInstance()->FnLog(dbss.str(), "", "DB");
        }
        else
        {
            m_local_db_err_flag = 0;
        }
    }
    catch (const std::exception& e)
    {
        r = -1;
        dbss.str("");
        dbss.clear();
        dbss << "DB: local db error in writing rec: " << std::string(e.what());
        Logger::getInstance()->FnLog(dbss.str(), "", "DB");
        m_local_db_err_flag = 1;
    }

    return r;
}

DBError db::loadstationsetup()
{
	vector<ReaderItem> selResult;
	int r = -1;
	int giStnid;
	giStnid = operation::getInstance()->gtStation.iSID;

	r = localdb->SQLSelect("SELECT * FROM Station_Setup WHERE StationId = '" + std::to_string(giStnid) + "'", &selResult, false);

	if (r != 0)
	{
		operation::getInstance()->writelog("get station set up fail.", "DB");
		return iLocalFail;
	}

	if (selResult.size() > 0)
	{            
		//Set configuration
		operation::getInstance()->gtStation.iSID = std::stoi(selResult[0].GetDataItem(0));
		operation::getInstance()->gtStation.sName = selResult[0].GetDataItem(1);
		switch (std::stoi(selResult[0].GetDataItem(2)))
		{
		case 1:
			operation::getInstance()->gtStation.iType = tientry;
			break;
		case 2:
			operation::getInstance()->gtStation.iType = tiExit;
			break;
		default:
			break;
		};
		operation::getInstance()->gtStation.iStatus = std::stoi(selResult[0].GetDataItem(3));
		operation::getInstance()->gtStation.sPCName = selResult[0].GetDataItem(4);
		operation::getInstance()->gtStation.iCHUPort = std::stoi(selResult[0].GetDataItem(5));
		operation::getInstance()->gtStation.iAntID = std::stoi(selResult[0].GetDataItem(6));
		operation::getInstance()->gtStation.iZoneID = std::stoi(selResult[0].GetDataItem(7));
		operation::getInstance()->gtStation.iIsVirtual = std::stoi(selResult[0].GetDataItem(8));
		switch(std::stoi(selResult[0].GetDataItem(9)))
		{
		case 0:
			operation::getInstance()->gtStation.iSubType = iNormal;
			break;
		case 1:
			operation::getInstance()->gtStation.iSubType = iXwithVENoPay;
			break;
		case 2:
			operation::getInstance()->gtStation.iSubType = iXwithVEPay;
			break;
		default:
			break;
		};
		operation::getInstance()->gtStation.iVirtualID = std::stoi(selResult[0].GetDataItem(10));
		operation::getInstance()->gtStation.iGroupID = std::stoi(selResult[0].GetDataItem(11));
		//m->gtStation.sZoneName= selResult[0].GetDataItem(11);
		//m->gtStation.iVExitID= std::stoi(selResult[0].GetDataItem(12));
		operation::getInstance()->tProcess.gbloadedStnSetup = true;
		return iDBSuccess;
	}

	return iNoData; 
};

int db::downloadTR()
{
	int ret = -1;
	int r = -1;
	int w = -1;
	std::vector<ReaderItem> tResult;
	std::vector<ReaderItem> selResult;
	std::string sqlStmt;
	std::stringstream dbss;
	int tr_type;
	int line_no;
	int enabled;
	std::string line_text;
	std::string line_var;
	int line_font;
	int line_align;

	sqlStmt = "SELECT COUNT(*) from TR_mst ";
	sqlStmt = sqlStmt + "where TRType = 1 or TRType = 2 or TRType = 6 or TRType = 11 or TRType = 12";

	r = centraldb->SQLSelect(sqlStmt, &tResult, false);
	if (r != 0)
	{
		m_remote_db_err_flag = 1;
		operation::getInstance()->writelog("download TR fail.", "DB");
		return ret;
	}
	else
	{
		m_remote_db_err_flag = 0;
		dbss.str("");
		dbss.clear();
		dbss << "Total " << std::string(tResult[0].GetDataItem(0)) << " TR type to be download.";
		Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}

	sqlStmt = "";
	sqlStmt.clear();
	sqlStmt = "SELECT TRType, Line_no, Enabled, LineText, LineVar, LineFont, LineAlign from TR_mst ";
	sqlStmt = sqlStmt + "where TRType = 1 or TRType = 2 or TRType = 6 or TRType = 11 or TRType = 12";
	r = centraldb->SQLSelect(sqlStmt, &selResult, true);
	if (r != 0)
	{
		m_remote_db_err_flag = 1;
		operation::getInstance()->writelog("download TR faile.", "DB");
		return ret;
	}
	else
	{
		m_remote_db_err_flag = 0;
	}

	int downloadCount = 0;
	if (selResult.size() > 0)
	{
		dbss.str("");
		dbss.clear();
		dbss << "Downloading TR " << std::to_string(selResult.size()) << " Records: Started";
		Logger::getInstance()->FnLog(dbss.str(), "", "DB");

		for (int j = 0; j < selResult.size(); j++)
		{
			tr_type = std::stoi(selResult[j].GetDataItem(0));
			line_no = std::stoi(selResult[j].GetDataItem(1));
			enabled = std::stoi(selResult[j].GetDataItem(2));
			line_text = selResult[j].GetDataItem(3);
			line_var = selResult[j].GetDataItem(4);
			line_font = std::stoi(selResult[j].GetDataItem(5));
			line_align = std::stoi(selResult[j].GetDataItem(6));

			w = writetr2local(tr_type, line_no, enabled, line_text, line_var, line_font, line_align);

			if (w == 0)
			{
				downloadCount++;
				m_remote_db_err_flag = 0;
			}
		}
		dbss.str("");
		dbss.clear();
		dbss << "Downloading TR Records, End, Total Record :" << selResult.size() << " , Downloaded Record :" << downloadCount;
		Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}

	ret = downloadCount;

	return ret;
}

int db::writetr2local(int tr_type, int line_no, int enabled, std::string line_text, std::string line_var, int line_font, int line_align)
{
	int r = -1;
	int debug = 0;
	std::string sqlStmt;
	vector<ReaderItem> selResult;
	std::stringstream dbss;

	try
	{
		r = localdb->SQLSelect("SELECT * FROM TR_mst where TRType =" + std::to_string(tr_type) + " and Line_no = " + std::to_string(line_no), &selResult, false);
		if (r != 0)
		{
			m_local_db_err_flag = 1;
			operation::getInstance()->writelog("Update local TR failed.", "DB");
			return r;
		}
		else
		{
			m_local_db_err_flag = 0;
		}

		if (selResult.size() > 0)
		{
			// Update param records
			std::string sqlStmt = "UPDATE TR_mst SET ";
			sqlStmt += "TRType = " + std::to_string(tr_type) + ", ";
			sqlStmt += "Line_no = " + std::to_string(line_no) + ", ";
			sqlStmt += "Enabled = " + std::to_string(enabled) + ", ";
			sqlStmt += "LineText = '" + line_text + "', ";
			sqlStmt += "LineVar = '" + line_var + "', ";
			sqlStmt += "LineFont = " + std::to_string(line_font) + ", ";
			sqlStmt += "LineAlign = " + std::to_string(line_align) + " ";
			sqlStmt += "WHERE TRType = " + std::to_string(tr_type) + " AND ";
			sqlStmt += "Line_no = " + std::to_string(line_no);

			r = localdb->SQLExecutNoneQuery(sqlStmt);
			if (r != 0)
			{
				m_local_db_err_flag = 1;
				dbss.str("");
				dbss.clear();
				dbss << "update local TR failed.";
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
			else
			{
				m_local_db_err_flag = 0;
			}
		}
		else
		{
			sqlStmt = "INSERT INTO TR_mst";
			sqlStmt = sqlStmt + " (TRType, Line_no, Enabled, LineText, LineVar, LineFont, LineAlign)";
			sqlStmt = sqlStmt + " VALUES (" + std::to_string(tr_type);
			sqlStmt = sqlStmt + "," + std::to_string(line_no);
			sqlStmt = sqlStmt + "," + std::to_string(enabled);
			sqlStmt = sqlStmt + ",'" + line_text + "'";
			sqlStmt = sqlStmt + ",'" + line_var + "'";
			sqlStmt = sqlStmt + "," + std::to_string(line_font);
			sqlStmt = sqlStmt + "," + std::to_string(line_align) + ")";

			r = localdb->SQLExecutNoneQuery(sqlStmt);
			if (r != 0)
			{
				m_local_db_err_flag = 1;
				dbss.str("");
				dbss.clear();
				dbss << "insert TR to local failed";
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
			else
			{
				m_local_db_err_flag = 0;
			}
		}
	}
	catch (const std::exception & e)
	{
		r = -1;
		dbss.str("");
		dbss.clear();
		dbss << "DB: local db error in writing rec: " << std::string(e.what());
		Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		m_local_db_err_flag = 1;
	}

	return r;
}

DBError db::loadcentralDBinfo()
{
	vector<ReaderItem> selResult;
	int r = -1;
	int giStnid;
	giStnid = operation::getInstance()->gtStation.iSID;

	r = localdb->SQLSelect("SELECT * FROM Station_Setup WHERE StationId = '" + std::to_string(giStnid) + "'", &selResult, false);

	if (r != 0)
	{
		//m_log->WriteAndPrint("Get Station Setup: fail");
		return iLocalFail;
	}

	if (selResult.size() > 0)
	{            
		//Set configuration
		operation::getInstance()->gtStation.iSID = std::stoi(selResult[0].GetDataItem(0));
		operation::getInstance()->gtStation.sName = selResult[0].GetDataItem(1);
		switch(std::stoi(selResult[0].GetDataItem(2)))
		{
		case 1:
			operation::getInstance()->gtStation.iType = tientry;
			break;
		case 2:
			operation::getInstance()->gtStation.iType = tiExit;
			break;
		default:
			break;
		};
		operation::getInstance()->gtStation.iStatus = std::stoi(selResult[0].GetDataItem(3));
		operation::getInstance()->gtStation.sPCName = selResult[0].GetDataItem(4);
		operation::getInstance()->gtStation.iCHUPort = std::stoi(selResult[0].GetDataItem(5));
		operation::getInstance()->gtStation.iAntID = std::stoi(selResult[0].GetDataItem(6));
		operation::getInstance()->gtStation.iZoneID = std::stoi(selResult[0].GetDataItem(7));
		operation::getInstance()->gtStation.iIsVirtual = std::stoi(selResult[0].GetDataItem(8));
		switch(std::stoi(selResult[0].GetDataItem(9)))
		{
		case 0:
			operation::getInstance()->gtStation.iSubType = iNormal;
			break;
		case 1:
			operation::getInstance()->gtStation.iSubType = iXwithVENoPay;
			break;
		case 2:
			operation::getInstance()->gtStation.iSubType = iXwithVEPay;
			break;
		default:
			break;
		};
		operation::getInstance()->gtStation.iVirtualID = std::stoi(selResult[0].GetDataItem(10));
		operation::getInstance()->gtStation.iGroupID = std::stoi(selResult[0].GetDataItem(11));
		//m->gtStation.sZoneName= selResult[0].GetDataItem(11);
		//m->gtStation.iVExitID= std::stoi(selResult[0].GetDataItem(12));
		return iDBSuccess;
	}

	return iNoData; 
};

DBError db::loadParam()
{
	int r = -1;
	vector<ReaderItem> selResult;

	r = localdb->SQLSelect("SELECT ParamName, ParamValue FROM Param_mst", &selResult, true);
	if (r != 0)
	{
		operation::getInstance()->writelog("load parameter failed.", "DB");
		return iLocalFail;
	}

	if (selResult.size() > 0)
	{
		for (auto &readerItem : selResult)
		{
			if (readerItem.getDataSize() == 2)
			{
				if (readerItem.GetDataItem(0) == "CommPortAntenna")
				{
					operation::getInstance()->tParas.giCommPortAntenna = std::stoi(readerItem.GetDataItem(1));
				}

				if (readerItem.GetDataItem(0) == "commportlcsc")
				{
					operation::getInstance()->tParas.giCommPortLCSC = std::stoi(readerItem.GetDataItem(1));
				}

				if (readerItem.GetDataItem(0) == "commportprinter")
				{
					operation::getInstance()->tParas.giCommPortPrinter = std::stoi(readerItem.GetDataItem(1));
				}

				if (readerItem.GetDataItem(0) == "EPS")
				{
					operation::getInstance()->tParas.giEPS = std::stoi(readerItem.GetDataItem(1));
				}

				if (readerItem.GetDataItem(0) == "carparkcode")
				{
					operation::getInstance()->tParas.gscarparkcode = readerItem.GetDataItem(1);
				}

				if (readerItem.GetDataItem(0) == "locallcsc")
				{
				    operation::getInstance()->tParas.gsLocalLCSC = readerItem.GetDataItem(1);
				}

                if (readerItem.GetDataItem(0) == "remotelcsc")
				{
				    operation::getInstance()->tParas.gsRemoteLCSC = readerItem.GetDataItem(1);
				}

                if (readerItem.GetDataItem(0) == "remotelcscback")
				{
				    operation::getInstance()->tParas.gsRemoteLCSCBack = readerItem.GetDataItem(1);
				}

                if (readerItem.GetDataItem(0) == "CSCRcdfFolder")
				{
				    operation::getInstance()->tParas.gsCSCRcdfFolder = readerItem.GetDataItem(1);
				}

                if (readerItem.GetDataItem(0) == "CSCRcdackFolder")
				{
				    operation::getInstance()->tParas.gsCSCRcdackFolder = readerItem.GetDataItem(1);
				}

                if (readerItem.GetDataItem(0) == "CPOID")
				{
				    operation::getInstance()->tParas.gsCPOID = readerItem.GetDataItem(1);
				}

                if (readerItem.GetDataItem(0) == "CPID")
				{
				    operation::getInstance()->tParas.gsCPID = readerItem.GetDataItem(1);
				}

                if (readerItem.GetDataItem(0) == "CommPortLED")
				{
				    operation::getInstance()->tParas.giCommPortLED = std::stoi(readerItem.GetDataItem(1));
				}

                if (readerItem.GetDataItem(0) == "HasMCycle")
				{
				    operation::getInstance()->tParas.giHasMCycle = std::stoi(readerItem.GetDataItem(1));
				}

                if (readerItem.GetDataItem(0) == "TicketSiteID")
				{
				    operation::getInstance()->tParas.giTicketSiteID = std::stoi(readerItem.GetDataItem(1));
				}

                if (readerItem.GetDataItem(0) == "DataKeepDays")
				{
				    operation::getInstance()->tParas.giDataKeepDays = std::stoi(readerItem.GetDataItem(1));
				}

                if (readerItem.GetDataItem(0) == "BarrierPulse")
				{
				    operation::getInstance()->tParas.gsBarrierPulse = std::stoi(readerItem.GetDataItem(1));
				}

                if (readerItem.GetDataItem(0) == "AntMaxRetry")
				{
				    operation::getInstance()->tParas.giAntMaxRetry = std::stoi(readerItem.GetDataItem(1));
				}

                if (readerItem.GetDataItem(0) == "AntMinOKTimes")
				{
				    operation::getInstance()->tParas.giAntMinOKTimes = std::stoi(readerItem.GetDataItem(1));
				}

                if (readerItem.GetDataItem(0) == "AntInqTO")
				{
				    operation::getInstance()->tParas.giAntInqTO = std::stoi(readerItem.GetDataItem(1));
				}

                if (readerItem.GetDataItem(0) == "AntiIURepetition")
				{
				    operation::getInstance()->tParas.gbAntiIURepetition = (std::stoi(readerItem.GetDataItem(1)) == 1) ? true : false;
				}

                if (readerItem.GetDataItem(0) == "commportled401")
				{
				    operation::getInstance()->tParas.giCommportLED401 = std::stoi(readerItem.GetDataItem(1));
				}

				if (readerItem.GetDataItem(0) == "commportreader")
				{
				    operation::getInstance()->tParas.giCommPortKDEReader = std::stoi(readerItem.GetDataItem(1));
				}

				if (readerItem.GetDataItem(0) == "CommPortCPT")
				{
				    operation::getInstance()->tParas.giCommPortUPOS = std::stoi(readerItem.GetDataItem(1));
				}

                if (readerItem.GetDataItem(0) == "IsHDBSite")
				{
				    operation::getInstance()->tParas.giIsHDBSite = std::stoi(readerItem.GetDataItem(1));
				}

                if (readerItem.GetDataItem(0) == "allowedholdertype")
				{
				    operation::getInstance()->tParas.gsAllowedHolderType = readerItem.GetDataItem(1);
				}

                if (readerItem.GetDataItem(0) == "LEDMaxChar")
				{
				    operation::getInstance()->tParas.giLEDMaxChar = std::stoi(readerItem.GetDataItem(1));
				}

                if (readerItem.GetDataItem(0) == "AlwaysTryOnline")
				{
				    operation::getInstance()->tParas.gbAlwaysTryOnline = (std::stoi(readerItem.GetDataItem(1)) == 1) ? true : false;
				}

                if (readerItem.GetDataItem(0) == "AutoDebitNoEntry")
				{
				    operation::getInstance()->tParas.gbAutoDebitNoEntry = (std::stoi(readerItem.GetDataItem(1)) == 1) ? true : false;
				}

                if (readerItem.GetDataItem(0) == "LoopAHangTime")
				{
				    operation::getInstance()->tParas.giLoopAHangTime = std::stoi(readerItem.GetDataItem(1));
				}

                if (readerItem.GetDataItem(0) == "OperationTO")
				{
				    operation::getInstance()->tParas.giOperationTO = std::stoi(readerItem.GetDataItem(1));
				}

                if (readerItem.GetDataItem(0) == "FullAction")
				{
				    operation::getInstance()->tParas.giFullAction = static_cast<eFullAction>(std::stoi(readerItem.GetDataItem(1)));
				}

                if (readerItem.GetDataItem(0) == "barrieropentoolongtime")
				{
				    operation::getInstance()->tParas.giBarrierOpenTooLongTime = std::stoi(readerItem.GetDataItem(1));
				}

                if (readerItem.GetDataItem(0) == "bitbarrierarmbroken")
				{
				    operation::getInstance()->tParas.giBitBarrierArmBroken = std::stoi(readerItem.GetDataItem(1));
				}

                if (readerItem.GetDataItem(0) == "mccontrolaction")
				{
				    operation::getInstance()->tParas.giMCControlAction = std::stoi(readerItem.GetDataItem(1));
				}

                if (readerItem.GetDataItem(0) == "LogBackFolder")
				{
				    operation::getInstance()->tParas.gsLogBackFolder = readerItem.GetDataItem(1);
				}

                if (readerItem.GetDataItem(0) == "LogKeepDays")
				{
				    operation::getInstance()->tParas.giLogKeepDays = std::stoi(readerItem.GetDataItem(1));
				}

                if (readerItem.GetDataItem(0) == "DBBackupFolder")
				{
				    operation::getInstance()->tParas.gsDBBackupFolder = readerItem.GetDataItem(1);
				}

                if (readerItem.GetDataItem(0) == "maxsendofflineno")
				{
				    operation::getInstance()->tParas.giMaxSendOfflineNo = std::stoi(readerItem.GetDataItem(1));
				}

                if (readerItem.GetDataItem(0) == "maxlocaldbsize")
				{
				    operation::getInstance()->tParas.glMaxLocalDBSize = std::stol(readerItem.GetDataItem(1));
				}

				if (readerItem.GetDataItem(0) == "MaxTransInterval")
				{
				    operation::getInstance()->tParas.giMaxTransInterval = std::stol(readerItem.GetDataItem(1));
				}
			}
		}
		operation::getInstance()->tProcess.gbloadedParam = true;
        return iDBSuccess;
	}

	return iNoData;
}


DBError db::loadvehicletype()
{
	int r = -1;
	vector<ReaderItem> selResult;

	r = localdb->SQLSelect("SELECT IUCode, TransType FROM Vehicle_type", &selResult, true);
	if (r != 0)
	{
		operation::getInstance()->writelog("load Trans Type failed.", "DB");
		return iLocalFail;
	}

	if (selResult.size() > 0)
	{
		for (auto &readerItem : selResult)
		{
			if (readerItem.getDataSize() == 2)
			{
				operation::getInstance()->tVType.push_back({std::stoi(readerItem.GetDataItem(0)), std::stoi(readerItem.GetDataItem(1))});
			}
		}
		operation::getInstance()->tProcess.gbloadedVehtype = true;
		return iDBSuccess;
	}
	return iNoData;
}

int db::FnGetVehicleType(std::string IUCode)
{
	int ret = 1;

	for (auto &item : operation::getInstance()->tVType)
	{
		if (item.iIUCode == std::stoi(IUCode))
		{
			ret = item.iType;
			break;
		}
	}

	return ret;
}

DBError db::loadEntrymessage(std::vector<ReaderItem>& selResult)
{
	if (selResult.size()>0)
	{
		for (auto &readerItem : selResult)
		{
			if (readerItem.getDataSize() == 2)
			{
				if (readerItem.GetDataItem(0) == "AltDefaultLED")
				{
					operation::getInstance()->tMsg.Msg_AltDefaultLED[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_AltDefaultLED[1] = readerItem.GetDataItem(1);
				}

				if (readerItem.GetDataItem(0) == "AltDefaultLED2")
				{
					operation::getInstance()->tMsg.Msg_AltDefaultLED2[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_AltDefaultLED2[1] = readerItem.GetDataItem(1);
				}

				if (readerItem.GetDataItem(0) == "AltDefaultLED3")
				{
					operation::getInstance()->tMsg.Msg_AltDefaultLED3[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_AltDefaultLED3[1] = readerItem.GetDataItem(1);
				}

				if (readerItem.GetDataItem(0) == "AltDefaultLED4")
				{
					operation::getInstance()->tMsg.Msg_AltDefaultLED4[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_AltDefaultLED4[1] = readerItem.GetDataItem(1);
				}


				if (readerItem.GetDataItem(0) == "authorizedvehicle")
				{
					operation::getInstance()->tMsg.Msg_authorizedvehicle[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_authorizedvehicle[1] = readerItem.GetDataItem(1);
				}


				if (readerItem.GetDataItem(0) == "CardReadingError")
				{
					operation::getInstance()->tMsg.Msg_CardReadingError[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_CardReadingError[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CCardReadingError")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_CardReadingError[1].clear();
						operation::getInstance()->tMsg.Msg_CardReadingError[1] = readerItem.GetDataItem(1);
					}
				}

			
				if (readerItem.GetDataItem(0) == "CardTaken")
				{
					operation::getInstance()->tMsg.Msg_CardTaken[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_CardTaken[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CCardTaken")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_CardTaken[1].clear();
						operation::getInstance()->tMsg.Msg_CardTaken[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "CarFullLED")
				{
					operation::getInstance()->tMsg.Msg_CarFullLED[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_CarFullLED[1] = readerItem.GetDataItem(1);
				}

				if (readerItem.GetDataItem(0) == "CarParkFull2LED")
				{
					operation::getInstance()->tMsg.Msg_CarParkFull2LED[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_CarParkFull2LED[1] = readerItem.GetDataItem(1);
				}

				if (readerItem.GetDataItem(0) == "DBError")
				{
					operation::getInstance()->tMsg.Msg_DBError[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_DBError[1] = readerItem.GetDataItem(1);
				}


				if (readerItem.GetDataItem(0) == "DefaultIU")
				{
					operation::getInstance()->tMsg.Msg_DefaultIU[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_DefaultIU[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CDefaultIU")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_DefaultIU[1].clear();
						operation::getInstance()->tMsg.Msg_DefaultIU[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "DefaultLED")
				{
					operation::getInstance()->tMsg.Msg_DefaultLED[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_DefaultLED[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CDefaultLED")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_DefaultLED[1].clear();
						operation::getInstance()->tMsg.Msg_DefaultLED[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "DefaultLED2")
				{
					operation::getInstance()->tMsg.Msg_DefaultLED2[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_DefaultLED2[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CDefaultLED2")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_DefaultLED2[1].clear();
						operation::getInstance()->tMsg.Msg_DefaultLED2[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "DefaultMsg2LED")
				{
					operation::getInstance()->tMsg.Msg_DefaultMsg2LED[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_DefaultMsg2LED[1] = readerItem.GetDataItem(1);
				}

				if (readerItem.GetDataItem(0) == "DefaultMsgLED")
				{
					operation::getInstance()->tMsg.Msg_DefaultMsgLED[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_DefaultMsgLED[1] = readerItem.GetDataItem(1);
				}

				if (readerItem.GetDataItem(0) == "EenhancedMCParking")
				{
					operation::getInstance()->tMsg.Msg_EenhancedMCParking[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_EenhancedMCParking[1] = readerItem.GetDataItem(1);
				}

				if (readerItem.GetDataItem(0) == "ESeasonWithinAllowance")
				{
					operation::getInstance()->tMsg.Msg_ESeasonWithinAllowance[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_ESeasonWithinAllowance[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CESeasonWithinAllowance")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_ESeasonWithinAllowance[1].clear();
						operation::getInstance()->tMsg.Msg_ESeasonWithinAllowance[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "ESPT3Parking")
				{
					operation::getInstance()->tMsg.Msg_ESPT3Parking[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_ESPT3Parking[1] = readerItem.GetDataItem(1);
				}

				if (readerItem.GetDataItem(0) == "EVIPHolderParking")
				{
					operation::getInstance()->tMsg.Msg_EVIPHolderParking[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_EVIPHolderParking[1] = readerItem.GetDataItem(1);
				}

				if (readerItem.GetDataItem(0) == "ExpiringSeason")
				{
					operation::getInstance()->tMsg.Msg_ExpiringSeason[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_ExpiringSeason[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CExpiringSeason")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_ExpiringSeason[1].clear();
						operation::getInstance()->tMsg.Msg_ExpiringSeason[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "FullLED")
				{
					operation::getInstance()->tMsg.Msg_FullLED[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_FullLED[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CFullLED")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_FullLED[1].clear();
						operation::getInstance()->tMsg.Msg_FullLED[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "Idle")
				{
					operation::getInstance()->tMsg.Msg_Idle[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_Idle[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CIdle")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_Idle[1].clear();
						operation::getInstance()->tMsg.Msg_Idle[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "InsertCashcard")
				{
					operation::getInstance()->tMsg.Msg_InsertCashcard[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_InsertCashcard[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CInsertCashcard")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_InsertCashcard[1].clear();
						operation::getInstance()->tMsg.Msg_InsertCashcard[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "IUProblem")
				{
					operation::getInstance()->tMsg.Msg_IUProblem[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_IUProblem[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CIUProblem")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_IUProblem[1].clear();
						operation::getInstance()->tMsg.Msg_IUProblem[1] = readerItem.GetDataItem(1);
					}
				}
				
				if (readerItem.GetDataItem(0) == "LockStation")
				{
					operation::getInstance()->tMsg.Msg_LockStation[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_LockStation[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CLockStation")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_LockStation[1].clear();
						operation::getInstance()->tMsg.Msg_LockStation[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "LoopA")
				{
					operation::getInstance()->tMsg.Msg_LoopA[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_LoopA[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CLoopA")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_LoopA[1].clear();
						operation::getInstance()->tMsg.Msg_LoopA[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "LoopAFull")
				{
					operation::getInstance()->tMsg.Msg_LoopAFull[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_LoopAFull[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CLoopAFull")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_LoopAFull[1].clear();
						operation::getInstance()->tMsg.Msg_LoopAFull[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "LorryFullLED")
				{
					operation::getInstance()->tMsg.Msg_LorryFullLED[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_LorryFullLED[1] = readerItem.GetDataItem(1);
				}

				if (readerItem.GetDataItem(0) == "LotAdjustmentMsg")
				{
					operation::getInstance()->tMsg.Msg_LotAdjustmentMsg[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_LotAdjustmentMsg[1] = readerItem.GetDataItem(1);
				}

				if (readerItem.GetDataItem(0) == "LowBal")
				{
					operation::getInstance()->tMsg.Msg_LowBal[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_LowBal[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CLowBal")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_LowBal[1].clear();
						operation::getInstance()->tMsg.Msg_LowBal[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "NoIU")
				{
					operation::getInstance()->tMsg.Msg_NoIU[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_NoIU[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CNoIU")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_NoIU[1].clear();
						operation::getInstance()->tMsg.Msg_NoIU[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "NoNightParking2LED")
				{
					operation::getInstance()->tMsg.Msg_NoNightParking2LED[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_NoNightParking2LED[1] = readerItem.GetDataItem(1);
				}

				if (readerItem.GetDataItem(0) == "Offline")
				{
					operation::getInstance()->tMsg.Msg_Offline[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_Offline[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "COffline")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_Offline[1].clear();
						operation::getInstance()->tMsg.Msg_Offline[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "PrinterError")
				{
					operation::getInstance()->tMsg.Msg_PrinterError[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_PrinterError[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CPrinterError")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_PrinterError[1].clear();
						operation::getInstance()->tMsg.Msg_PrinterError[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "PrintingReceipt")
				{
					operation::getInstance()->tMsg.Msg_PrintingReceipt[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_PrintingReceipt[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CPrintingReceipt")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_PrintingReceipt[1].clear();
						operation::getInstance()->tMsg.Msg_PrintingReceipt[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "Processing")
				{
					operation::getInstance()->tMsg.Msg_Processing[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_Processing[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CProcessing")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_Processing[1].clear();
						operation::getInstance()->tMsg.Msg_Processing[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "ReaderCommError")
				{
					operation::getInstance()->tMsg.Msg_ReaderCommError[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_ReaderCommError[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CReaderCommError")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_ReaderCommError[1].clear();
						operation::getInstance()->tMsg.Msg_ReaderCommError[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "ReaderError")
				{
					operation::getInstance()->tMsg.Msg_ReaderError[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_ReaderError[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CReaderError")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_ReaderError[1].clear();
						operation::getInstance()->tMsg.Msg_ReaderError[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SameLastIU")
				{
					operation::getInstance()->tMsg.Msg_SameLastIU[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_SameLastIU[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSameLastIU")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_SameLastIU[1].clear();
						operation::getInstance()->tMsg.Msg_SameLastIU[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "ScanEntryTicket")
				{
					operation::getInstance()->tMsg.Msg_ScanEntryTicket[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_ScanEntryTicket[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CScanEntryTicket")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_ScanEntryTicket[1].clear();
						operation::getInstance()->tMsg.Msg_ScanEntryTicket[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "ScanValTicket")
				{
					operation::getInstance()->tMsg.Msg_ScanValTicket[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_ScanValTicket[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CScanValTicket")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_ScanValTicket[1].clear();
						operation::getInstance()->tMsg.Msg_ScanValTicket[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SeasonAsHourly")
				{
					operation::getInstance()->tMsg.Msg_SeasonAsHourly[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_SeasonAsHourly[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSeasonAsHourly")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_SeasonAsHourly[1].clear();
						operation::getInstance()->tMsg.Msg_SeasonAsHourly[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SeasonBlocked")
				{
					operation::getInstance()->tMsg.Msg_SeasonBlocked[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_SeasonBlocked[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSeasonBlocked")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_SeasonBlocked[1].clear();
						operation::getInstance()->tMsg.Msg_SeasonBlocked[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SeasonExpired")
				{
					operation::getInstance()->tMsg.Msg_SeasonExpired[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_SeasonExpired[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSeasonExpired")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_SeasonExpired[1].clear();
						operation::getInstance()->tMsg.Msg_SeasonExpired[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SeasonInvalid")
				{
					operation::getInstance()->tMsg.Msg_SeasonInvalid[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_SeasonInvalid[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSeasonInvalid")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_SeasonInvalid[1].clear();
						operation::getInstance()->tMsg.Msg_SeasonInvalid[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SeasonMultiFound")
				{
					operation::getInstance()->tMsg.Msg_SeasonMultiFound[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_SeasonMultiFound[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSeasonMultiFound")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_SeasonMultiFound[1].clear();
						operation::getInstance()->tMsg.Msg_SeasonMultiFound[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SeasonNotFound")
				{
					operation::getInstance()->tMsg.Msg_SeasonNotFound[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_SeasonNotFound[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSeasonNotFound")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_SeasonNotFound[1].clear();
						operation::getInstance()->tMsg.Msg_SeasonNotFound[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SeasonNotStart")
				{
					operation::getInstance()->tMsg.Msg_SeasonNotStart[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_SeasonNotStart[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSeasonNotStart")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_SeasonNotStart[1].clear();
						operation::getInstance()->tMsg.Msg_SeasonNotStart[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SeasonNotValid")
				{
					operation::getInstance()->tMsg.Msg_SeasonNotValid[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_SeasonNotValid[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSeasonNotValid")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_SeasonNotValid[1].clear();
						operation::getInstance()->tMsg.Msg_SeasonNotValid[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SeasonOnly")
				{
					operation::getInstance()->tMsg.Msg_SeasonOnly[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_SeasonOnly[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSeasonOnly")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_SeasonOnly[1].clear();
						operation::getInstance()->tMsg.Msg_SeasonOnly[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SeasonPassback")
				{
					operation::getInstance()->tMsg.Msg_SeasonPassback[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_SeasonPassback[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSeasonPassback")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_SeasonPassback[1].clear();
						operation::getInstance()->tMsg.Msg_SeasonPassback[1] = readerItem.GetDataItem(1);
					}
				}

				
				if (readerItem.GetDataItem(0) == "SeasonTerminated")
				{
					operation::getInstance()->tMsg.Msg_SeasonTerminated[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_SeasonTerminated[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSeasonTerminated")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_SeasonTerminated[1].clear();
						operation::getInstance()->tMsg.Msg_SeasonTerminated[1] = readerItem.GetDataItem(1);
					}
				}

				
				if (readerItem.GetDataItem(0) == "SystemError")
				{
					operation::getInstance()->tMsg.Msg_SystemError[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_SystemError[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSystemError")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_SystemError[1].clear();
						operation::getInstance()->tMsg.Msg_SystemError[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "ValidSeason")
				{
					operation::getInstance()->tMsg.Msg_ValidSeason[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_ValidSeason[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CValidSeason")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_ValidSeason[1].clear();
						operation::getInstance()->tMsg.Msg_ValidSeason[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "VVIP")
				{
					operation::getInstance()->tMsg.Msg_VVIP[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_VVIP[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CVVIP")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_VVIP[1].clear();
						operation::getInstance()->tMsg.Msg_VVIP[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "WholeDayParking")
				{
					operation::getInstance()->tMsg.Msg_WholeDayParking[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_WholeDayParking[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CWholeDayParking")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_WholeDayParking[1].clear();
						operation::getInstance()->tMsg.Msg_WholeDayParking[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "WithIU")
				{
					operation::getInstance()->tMsg.Msg_WithIU[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_WithIU[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CWithIU")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.Msg_WithIU[1].clear();
						operation::getInstance()->tMsg.Msg_WithIU[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "BlackList")
				{
					operation::getInstance()->tMsg.MsgBlackList[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.MsgBlackList[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CBlackList")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tMsg.MsgBlackList[1].clear();
						operation::getInstance()->tMsg.MsgBlackList[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "E1enhancedMCParking")
				{
					operation::getInstance()->tMsg.Msg_E1enhancedMCParking[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tMsg.Msg_E1enhancedMCParking[1] = readerItem.GetDataItem(1);
				}
			}
		}
		operation::getInstance()->tProcess.gbloadedLEDMsg = true;
		return iDBSuccess;
	}
	return iNoData;
}

DBError db::loadmessage()
{
	int r = -1;
	DBError retErr;
	vector<ReaderItem> ledSelResult;
	vector<ReaderItem> lcdSelResult;

	r = localdb->SQLSelect("select msg_id, msg_body from message_mst", &ledSelResult, true);
	if (r != 0)
	{
		operation::getInstance()->writelog("load LED message failed.", "DB");
		return iLocalFail;
	}

	retErr = loadEntrymessage(ledSelResult);
	if (retErr == DBError::iNoData)
	{
		return retErr;
	}

	r = localdb->SQLSelect("select msg_id, msg_body from message_mst where m_status >= 10", &lcdSelResult, true);
	if (r != 0)
	{
		operation::getInstance()->writelog("load LCD message failed.", "DB");
		return iLocalFail;
	}

	retErr = loadEntrymessage(lcdSelResult);
	if (retErr == DBError::iNoData)
	{
		return retErr;
	}

	return retErr;
}

DBError db::loadTR()
{
	int r = -1;
	DBError retErr;
	vector<ReaderItem> trSelResult;
	std::string sqlStmt;

	sqlStmt = "SELECT LineText, LineVar, LineFont, LineAlign from TR_mst";
	sqlStmt = sqlStmt + " WHERE TRType=2" + " AND Enabled = 1 ORDER BY Line_no";

	r = localdb->SQLSelect(sqlStmt, &trSelResult, true);
	if (r != 0)
	{
		operation::getInstance()->writelog("load LTR failed.", "DB");
		return iLocalFail;
	}

	if (trSelResult.size() > 0)
	{
		int i = 0;
		for (int j = 0; j < trSelResult.size(); j++)
		{
			struct tTR_struc tr;
			tr.gsTR0 = trSelResult[j].GetDataItem(0);
			tr.gsTR1 = trSelResult[j].GetDataItem(1);
			tr.giTRF = std::stoi(trSelResult[j].GetDataItem(2));
			tr.giTRA = std::stoi(trSelResult[j].GetDataItem(3));
			operation::getInstance()->tTR.push_back(tr);
			i++;
		}
		operation::getInstance()->writelog("Load " + std::to_string(i) + " Ticket/Receipt Format.", "DB");
		return iDBSuccess;
	}
	else
	{
		operation::getInstance()->writelog("No Ticket/Receipt to load.", "DB");
	}

	return iNoData;
}

string  db::GetPartialSeasonMsg(int iTransType)
{
    int r = -1;
	vector<ReaderItem> tResult;

	r = localdb->SQLSelect("SELECT Description FROM trans_type where trans_type =" + iTransType, &tResult, true);
	if (r != 0)
	{
		return "";
	}

	if (tResult.size()>0)
	{
		return std::string (tResult[0].GetDataItem(0));
	}
	return "";
}

void db::moveOfflineTransToCentral()
{
	time_t vfdate,vtdate;
	CE_Time vfdt,vtdt;
	int r=-1;// success flag
	std::string tableNm="";
	tEntryTrans_Struct ter;
	tExitTrans_Struct tex;
	int s=-1;
	Ctrl_Type ctrl;
	vector<ReaderItem> selResult;
	vector<ReaderItem> tResult;
	std::string sqlStmt;
	int j;
	int d=-1;
	int k;

	operation::getInstance()->tProcess.offline_status=0;
	
	if(operation::getInstance()->gtStation.iType==tientry)
	{
		r = localdb->SQLSelect("SELECT count(iu_tk_no) FROM Entry_Trans",&tResult,false);
		if(r!=0) {
			m_local_db_err_flag=1;
			operation::getInstance()->writelog("move offline data fail.","DB");
			return;
		}
		else 
		{
			m_local_db_err_flag=0;
			k=std::stoi(tResult[0].GetDataItem(0));
			if (k>0){
				operation::getInstance()->writelog("Total " + std::string (tResult[0].GetDataItem(0)) + " Entry trans to be upload.","DB");
				operation::getInstance()->tProcess.offline_status=1;
			}
		}
	}
	else if(operation::getInstance()->gtStation.iType==tiExit)
	{
		
		r= localdb->SQLSelect("SELECT count(iu_tk_no) FROM Exit_Trans",&tResult,false);
		if(r!=0) m_local_db_err_flag=1;
		else 
		{
			m_local_db_err_flag=0;
			k=std::stoi(tResult[0].GetDataItem(0));
			if(k > 0){
				operation::getInstance()->writelog("Total " + std::string (tResult[0].GetDataItem(0)) + " Enxit trans to be upload.", "DB");
			  	operation::getInstance()->tProcess.offline_status=1;
			}
		}
	}
	
	if (operation::getInstance()->tProcess.offline_status ==0) {return;}

	try {
		if(operation::getInstance()->gtStation.iType==tientry)
		{
			tableNm="Entry_Trans";
			ctrl=s_Entry;
			std::string dtFormat= std::string(1, '"')+ "%Y-%m-%d %H:%i:%s" + std::string(1, '"') ;
			sqlStmt= "SELECT Station_ID,Entry_Time,iu_tk_No,";
			sqlStmt=sqlStmt + "trans_type,Status";
			sqlStmt=sqlStmt + ",TK_SerialNo";
			sqlStmt=sqlStmt + ",Card_Type";
			sqlStmt=sqlStmt + ",card_no,paid_amt,parking_fee";
			sqlStmt=sqlStmt +  ",gst_amt";
			sqlStmt = sqlStmt + ",lpn";
			sqlStmt= sqlStmt+ " FROM " + tableNm  + " ORDER by Entry_Time desc";
		}
		else if(operation::getInstance()->gtStation.iType==tiExit)
		{
			tableNm="Exit_Trans";
			ctrl=s_Exit;
			std::string dtFormat= std::string(1, '"')+ "%Y-%m-%d %H:%i:%s" + std::string(1, '"') ;

			sqlStmt= "SELECT Station_ID,Exit_Time,iu_tk_No,";
			sqlStmt=sqlStmt + "card_mc_no,trans_type,status,";
			sqlStmt=sqlStmt + "parked_time,Parking_Fee,Paid_Amt,Receipt_No,";
			sqlStmt=sqlStmt + "Redeem_amt,Redeem_time,Redeem_no";
			sqlStmt=sqlStmt +  ",gst_amt,chu_debit_code,Card_Type,Top_Up_Amt";
			sqlStmt = sqlStmt + ",lpn";
			
			sqlStmt= sqlStmt+ " FROM " + tableNm  + " ORDER by Exit_Time desc";
		}
		
		r = localdb->SQLSelect(sqlStmt,&selResult,true);
		if(r!=0) m_local_db_err_flag=1;
		else  m_local_db_err_flag=0;

		if (selResult.size()>0){
			operation::getInstance()->writelog("uploading  " + std::to_string (selResult.size()) + " Records: Started", "DB");
			for(j=0;j<selResult.size();j++){
				
				if(operation::getInstance()->gtStation.iType==tientry)
				{
					ter.esid=selResult[j].GetDataItem(0);
					ter.sEntryTime=selResult[j].GetDataItem(1);
					ter.sIUTKNo=selResult[j].GetDataItem(2);
					ter.iTransType=std::stoi(selResult[j].GetDataItem(3));
					ter.iStatus=std::stoi(selResult[j].GetDataItem(4));
					ter.sSerialNo=selResult[j].GetDataItem(5);
					ter.iCardType=std::stoi(selResult[j].GetDataItem(6));
					ter.sCardNo=selResult[j].GetDataItem(7);
					ter.sPaidAmt=std::stof(selResult[j].GetDataItem(8));
					ter.sFee=std::stof(selResult[j].GetDataItem(9));
					ter.sGSTAmt=std::stof(selResult[j].GetDataItem(10));
					ter.sLPN[0] = selResult[j].GetDataItem(11);
					
					s=insertTransToCentralEntryTransTmp(ter);
				}
				else if(operation::getInstance()->gtStation.iType==tiExit) 
				{
					tex.xsid=selResult[j].GetDataItem(0);
					tex.sExitTime=selResult[j].GetDataItem(1);
					tex.sIUNo=selResult[j].GetDataItem(2);
					tex.sCardNo=selResult[j].GetDataItem(3);
					tex.iTransType=std::stoi(selResult[j].GetDataItem(4));
					tex.iStatus=std::stoi(selResult[j].GetDataItem(5));
					tex.lParkedTime=std::stoi(selResult[j].GetDataItem(6));
					tex.sFee=std::stof(selResult[j].GetDataItem(7));
					tex.sPaidAmt=std::stof(selResult[j].GetDataItem(8));
					tex.sReceiptNo=selResult[j].GetDataItem(9);
					tex.sRedeemAmt=std::stof(selResult[j].GetDataItem(10));
					tex.iRedeemTime=std::stoi(selResult[j].GetDataItem(11));
					tex.sRedeemNo=selResult[j].GetDataItem(12);
					tex.sGSTAmt=std::stof(selResult[j].GetDataItem(13));
					tex.sCHUDebitCode=selResult[j].GetDataItem(14);
					tex.iCardType=std::stoi(selResult[j].GetDataItem(15));
					tex.sTopupAmt=std::stof(selResult[j].GetDataItem(16));
					
					s=insertTransToCentralExitTransTmp(tex);
				}
				
				//insert record into central DB

				if (s==0)
				{
					if(operation::getInstance()->gtStation.iType==tientry)
					d=deleteLocalTrans (ter.sIUTKNo,ter.sEntryTime,ctrl);
					else if(operation::getInstance()->gtStation.iType==tientry)
					d=deleteLocalTrans (tex.sIUNo,tex.sExitTime,ctrl);
					if (d==0) m_local_db_err_flag=0;
					else m_local_db_err_flag=1;


					//-----------------------
					m_remote_db_err_flag=0;
				}
				else m_remote_db_err_flag=1;        
			}
			operation::getInstance()->writelog("uploading trans Records: End","DB");
		}       
	}
	catch (const std::exception &e)
	{
		r=-1;// success flag
		// cout << "ERROR: " << err << endl;
		operation::getInstance()->writelog("DB: moveOfflineTransToCentral error: " + std::string(e.what()),"DB");
		m_local_db_err_flag=1;
	} 

}

int db::insertTransToCentralEntryTransTmp(tEntryTrans_Struct ter)
{

	int r=0;
	CE_Time dt;
	string sqstr="";
	string tbName="";
	tbName="Entry_Trans_tmp";
	
	//operation::getInstance()->writelog("Central DB: INSERT IUNo= " + ter.sIUTKNo +" and EntryTime="+ ter.sEntryTime  + " INTO "+ tbName +" : Started","DB");

	// insert into Central trans tmp table
	sqstr="INSERT INTO " + tbName +" (Station_ID,Entry_Time,IU_Tk_No,trans_type,status,TK_Serialno,Card_Type";
	sqstr=sqstr + ",card_no,paid_amt,parking_fee";        
	sqstr=sqstr + ",gst_amt,lpn";
	sqstr=sqstr + ") Values ('" + ter.esid+ "',convert(datetime,'" + ter.sEntryTime+ "',120),'" + ter.sIUTKNo;
	sqstr = sqstr +  "','" + std::to_string(ter.iTransType);
	sqstr = sqstr + "','" + std::to_string(ter.iStatus) + "','" + ter.sSerialNo;
	sqstr = sqstr + "','" + std::to_string(ter.iCardType);
	sqstr = sqstr + "','" + ter.sCardNo + "','" + std::to_string(ter.sPaidAmt) + "','" + std::to_string(ter.sFee);
	sqstr = sqstr + "','" + std::to_string(ter.sGSTAmt)+"','"+ter.sLPN[0]+"'";
	sqstr = sqstr +  ")";

	r = centraldb->SQLExecutNoneQuery(sqstr);

	//operation::getInstance()->writelog(sqstr,"DB");
	
	if (r==0) operation::getInstance()->writelog("Central DB: INSERT IUNo= " + ter.sIUTKNo +" and EntryTime="+ ter.sEntryTime   + " INTO "+ tbName +" : Success","DB");
	else operation::getInstance()->writelog("Central DB: INSERT IUNo= " + ter.sIUTKNo +" and EntryTime="+ ter.sEntryTime  + " INTO "+ tbName +" : Fail","DB");


	if(r!=0) m_remote_db_err_flag=1;
	else m_remote_db_err_flag=0;

	return r;
}


int db::insertTransToCentralExitTransTmp(tExitTrans_Struct tex)
{

	int r=0;
	CE_Time dt;
	string sqstr="";
	string tbName="";
	tbName="Exit_Trans_tmp";
	
	
	operation::getInstance()->writelog("Central DB: INSERT IUNo= " + tex.sIUNo +" and ExitTime="+ tex.sExitTime  + " INTO "+ tbName +" : Started","DB");

	// insert into Central trans tmp table
	sqstr="INSERT INTO " + tbName +" (Station_ID,Exit_Time,IU_Tk_No,card_mc_no,trans_type,parked_time,parking_fee,paid_amt,receipt_no,status";
	sqstr=sqstr + ",redeem_amt,redeem_time,redeem_no";        
	sqstr=sqstr + ",gst_amt,chu_debit_code,card_type,top_up_amt";
	sqstr=sqstr + ") Values ('" + tex.xsid+ "',convert(datetime,'" + tex.sExitTime+ "',120),'" + tex.sIUNo;
	sqstr = sqstr +  "','" +tex.sCardNo+  "','" +std::to_string(tex.iTransType);
	sqstr = sqstr +  "','" +std::to_string(tex.lParkedTime) + "','"+ std::to_string(tex.sFee);
	sqstr = sqstr +  "','" +std::to_string(tex.sPaidAmt) + "','"+tex.sReceiptNo +  "','" + std::to_string(tex.iStatus);
	sqstr = sqstr +  "','" +std::to_string(tex.sRedeemAmt) + "','"+std::to_string(tex.iRedeemTime) + "','"+tex.sRedeemNo;
	sqstr = sqstr +  "','" +std::to_string(tex.sGSTAmt) + "','"+tex.sCHUDebitCode + "','"+std::to_string(tex.iCardType);
	sqstr = sqstr +  "','" +std::to_string(tex.sTopupAmt)+"'";
	sqstr = sqstr +  ")";

	r = centraldb->SQLExecutNoneQuery(sqstr);
	
	if (r==0) operation::getInstance()->writelog("Central DB: INSERT IUNo= " + tex.sIUNo +" and ExitTime="+ tex.sExitTime  + " INTO "+ tbName +" : Success","DB");
	else operation::getInstance()->writelog("Central DB: INSERT IUNo= " +  tex.sIUNo +" and ExitTime="+ tex.sExitTime  + " INTO "+ tbName  +" : Fail","DB");


	if(r!=0) m_remote_db_err_flag=1;
	else m_remote_db_err_flag=0;

	return r;
}

int db:: deleteLocalTrans(string iuno,string trantime,Ctrl_Type ctrl)
{

	std::string sqlStmt;
	std::string tbName="";
	vector<ReaderItem> selResult;
	int r=-1;// success flag

	try {

		switch (ctrl)
		{
		case s_Entry:
			tbName="Entry_Trans";
			//operation::getInstance()->writelog("Local DB: Delete IUNo= " +iuno + " AND TrxTime="  + trantime + " From Entry_Trans: Started","DB");
			sqlStmt="Delete from " +tbName +" WHERE iu_tk_no='"+ iuno + "' AND Entry_Time='" + trantime  +"'" ;
			break;
		case s_Exit:
			tbName="Exit_Trans";
			operation::getInstance()->writelog("Local DB: Delete IUNo= " +iuno + " AND TrxTime="  + trantime + " From Exit_Trans: Started","DB");
			sqlStmt="Delete from " +tbName +" WHERE iu_tk_no='"+ iuno + "' AND exit_time='" + trantime  +"'" ;
			break;

		}
		
		r= localdb->SQLExecutNoneQuery(sqlStmt);
		//----------------------------------------------

		if(r==0)
		{
			m_local_db_err_flag=0;
			operation::getInstance()->writelog("Local DB: Delete IUNo= " +iuno + " AND TrxTime="  + trantime + " From " + tbName + ": Success","DB");
		}

		else
		{
			m_local_db_err_flag=1;
			operation::getInstance()->writelog("Local DB: Delete IUNo= " +iuno + " AND TrxTime="  + trantime + " From " + tbName + ": Fail","DB");
		}
		
		

	}
	catch (const std::exception &e) //const mysqlx::Error &err
	{
		
		switch (ctrl)
		{
		case s_Entry:
			
			operation::getInstance()->writelog("Local DB: Delete IUNo= " +iuno + " AND TrxTime="  + trantime + " From Entry_Trans: Fail","DB");
			break;
		case s_Exit:
			
			operation::getInstance()->writelog("Local DB: Delete IUNo= " +iuno + " AND TrxTime="  + trantime + " From Exit_Trans: Fail","DB");
			break;

		}
		
		operation::getInstance()->writelog("Local DB: deleteLocalTrans error: " + std::string(e.what()),"DB"); 
		r=-1;
		m_local_db_err_flag=1;
	} 
	return r;
}

int db::clearseason()
{
	std::string tableNm="season_mst";
	
	std::string sqlStmt;
	
	int r=-1;// success flag
	try {

		sqlStmt="truncate table " + tableNm;

		r=localdb->SQLExecutNoneQuery(sqlStmt);

		if(r==0) m_local_db_err_flag=0;
		else m_local_db_err_flag=1;
	}
	catch (const std::exception &e)
	{
		operation::getInstance()->writelog("DB: local db error in clearing: " + std::string(e.what()),"DB");
		r=-1;
		m_local_db_err_flag=1;
	} 
	return r;
}

int db::IsBlackListIU(string sIU)
{
	int r = -1;
	vector<ReaderItem> tResult;

	r = centraldb->SQLSelect("SELECT type FROM BlackList where status = 0 and CAN =" + sIU, &tResult, true);
	if (r != 0)
	{
		return -1;
	}

	if (tResult.size()>0)
	{
		return std::stoi(tResult[0].GetDataItem(0));
	}
	return -1;

}

int db::AddRemoteControl(string sTID,string sAction, string sRemarks) 
{

	int r=0;
	CE_Time dt;
	string sqstr="";
	string tbName="";
	tbName="remote_control_history";
	
	// insert into Central trans tmp table
	sqstr="INSERT INTO " + tbName +" (station_id,action_dt,action_name,operator,remarks)";
	sqstr=sqstr + " Values ('" + sTID + "',getdate(),'" + sAction + "','auto','" +sRemarks + "')";
	
	r = centraldb->SQLExecutNoneQuery(sqstr);

//	operation::getInstance()->writelog(sqstr,"DB");
	
	if (r==0) {
		operation::getInstance()->writelog("Success insert: " + sAction,"DB");
		m_remote_db_err_flag=0;
	}
	else {
		operation::getInstance()->writelog("fail to insert: " + sAction,"DB");
		 m_remote_db_err_flag=1;
	}
	return r;
}

int db::AddSysEvent(string sEvent) 
{

	int r=0;
	CE_Time dt;
	string sqstr="";
	string tbName="";
	tbName="sys_event_log";
	string giStationID = std::to_string(operation::getInstance()->gtStation.iSID);
	
	// insert into Central trans tmp table
	sqstr="Insert into sys_event_log (station_id,event)";
	sqstr=sqstr + " Values ('" + giStationID + "','" + sEvent + "')";
	
	r = centraldb->SQLExecutNoneQuery(sqstr);

	
	if (r==0) {
		operation::getInstance()->writelog("Success insert sys event log: " + sEvent,"DB");
		m_remote_db_err_flag=0;
	}
	else {
		operation::getInstance()->writelog("fail to insert sys event log: " + sEvent,"DB");
		 m_remote_db_err_flag=1;
	}
	return r;
}

int db::FnGetDatabaseErrorFlag()
{
	return m_remote_db_err_flag;
}

int db::clearexpiredseason()
{
	std::string tableNm="season_mst";
	
	std::string sqlStmt;
	
	int r=-1;// success flag
	try {

		sqlStmt="Delete FROM season_mst WHERE TIMESTAMPDIFF(HOUR, date_to, NOW()) >= 30*24";

		r=localdb->SQLExecutNoneQuery(sqlStmt);

		if(r==0) m_local_db_err_flag=0;
		else m_local_db_err_flag=1;
	}
	catch (const std::exception &e)
	{
		operation::getInstance()->writelog("DB: local db error in clearing: " + std::string(e.what()),"DB");
		r=-1;
		m_local_db_err_flag=1;
	} 
	return r;
}

int db::updateEntryTrans(string lpn, string sTransID) 
{

	int r=0;
	string sqstr="";

	// insert into Central trans tmp table

	sqstr="UPDATE Entry_trans_tmp set lpn = '"+ lpn + "' WHERE entry_lpn_sid = '"+ sTransID + "'";
	
	r = centraldb->SQLExecutNoneQuery(sqstr);

	if (r==0) 
	{
		if (centraldb->NumberOfRowsAffected > 0){
			operation::getInstance()->writelog("Success update LPR to Entry_Trans_Tmp","DB");
		}else
		{
			sqstr="UPDATE Entry_trans set lpn = '"+ lpn + "' WHERE entry_lpn_sid = '"+ sTransID + "'";
			r = centraldb->SQLExecutNoneQuery(sqstr);

			if (r==0) {
				if (centraldb->NumberOfRowsAffected > 0)
				{
					operation::getInstance()->writelog("Success update LPR to Entry_Trans","DB");
					m_remote_db_err_flag=0;
				} else operation::getInstance()->writelog("No TransID for update","DB");
			} else
			{
			operation::getInstance()->writelog("fail to update LPR to Entry_trans","DB");
		 	m_remote_db_err_flag=1;
			}
		}
	}
	else {
		operation::getInstance()->writelog("fail to update LPR to Entry_trans_Tmp","DB");
		 m_remote_db_err_flag=1;
		
	}
	return r;
}

DBError db::insertexittrans(tExitTrans_Struct& tExit)
{
    std::string sqlStmt;
    int r;
    std::stringstream dbss;

    if (centraldb->IsConnected() != -1)
    {
        centraldb->Disconnect();
        if (centraldb->Connect() != 0)
        {
            dbss << "Unable to connect to central DB while inserting exit_trans table.";
            Logger::getInstance()->FnLog(dbss.str(), "", "DB");
            operation::getInstance()->tProcess.giSystemOnline = 1;
            goto processLocal;
        }
    }

    operation::getInstance()->tProcess.giSystemOnline = 0;

    sqlStmt = "INSERT INTO exit_trans (station_id, exit_time, iu_tk_no, card_mc_no, trans_type, parked_time";
    sqlStmt = sqlStmt + ", parking_fee, paid_amt, receipt_no, status, redeem_amt, redeem_time, redeem_no";
    sqlStmt = sqlStmt + ", gst_amt, chu_debit_code, card_type, top_up_amt, uposbatchno, feefrom, lpn";
    sqlStmt = sqlStmt + ") VALUES (" + tExit.xsid;
    sqlStmt = sqlStmt + ", '" + tExit.sExitTime + "'";
    sqlStmt = sqlStmt + ", '" + tExit.sIUNo + "'";
    sqlStmt = sqlStmt + ", '" + tExit.sCardNo + "'";
    sqlStmt = sqlStmt + ", " + std::to_string(tExit.iTransType);
    sqlStmt = sqlStmt + ", " + std::to_string(tExit.lParkedTime);
    sqlStmt = sqlStmt + ", " + std::to_string(tExit.sFee);
    sqlStmt = sqlStmt + ", " + std::to_string(tExit.sPaidAmt);
    sqlStmt = sqlStmt + ", '" + tExit.sReceiptNo + "'";
    sqlStmt = sqlStmt + ", " + std::to_string(tExit.iStatus);
    sqlStmt = sqlStmt + ", " + std::to_string(tExit.sRedeemAmt);
    sqlStmt = sqlStmt + ", " + std::to_string(tExit.iRedeemTime);
    sqlStmt = sqlStmt + ", '" + tExit.sRedeemNo + "'";
    sqlStmt = sqlStmt + ", " + std::to_string(tExit.sGSTAmt);
    sqlStmt = sqlStmt + ", '" + tExit.sCHUDebitCode + "'";
    sqlStmt = sqlStmt + ", " + std::to_string(tExit.iCardType);
    sqlStmt = sqlStmt + ", " + std::to_string(tExit.sTopupAmt);
    sqlStmt = sqlStmt + ", '" + tExit.uposbatchno + "'";
    sqlStmt = sqlStmt + ", '" + tExit.feedrom + "'";
    sqlStmt = sqlStmt + ", '" + tExit.lpn + "'";
    sqlStmt = sqlStmt + ")";

    r = centraldb->SQLExecutNoneQuery(sqlStmt);
    if (r != 0)
    {
        Logger::getInstance()->FnLog(sqlStmt, "", "DB");
        Logger::getInstance()->FnLog("Insert exit_trans to Central: fail.", "", "DB");
        return iCentralFail;
    }
    else
    {
        Logger::getInstance()->FnLog("Insert exit_trans to Central: success.", "", "DB");
        return iDBSuccess;
    }

processLocal:

    if (localdb->IsConnected() != 1)
    {
        localdb->Disconnect();
        if (localdb->Connect() != 0)
        {
            dbss.str("");
            dbss.clear();
            dbss << "Unable to connect to local DB while inserting Exit_Trans table.";
            Logger::getInstance()->FnLog(dbss.str(), "", "DB");
            return iLocalFail;
        }
    }

    sqlStmt = "INSERT INTO Exit_Trans (Station_ID, exit_time, iu_tk_no, card_mc_no, trans_type, parked_time";
    sqlStmt = sqlStmt + ", Parking_Fee, Paid_Amt, Receipt_No, Status, Redeem_amt, Redeem_time, Redeem_no";
    sqlStmt = sqlStmt + ", gst_amt, chu_debit_code, Card_Type, Top_Up_Amt, uposbatchno, feefrom, lpn";
    sqlStmt = sqlStmt + ") VALUES (" + tExit.xsid;
    sqlStmt = sqlStmt + ", '" + tExit.sExitTime + "'";
    sqlStmt = sqlStmt + ", '" + tExit.sIUNo + "'";
    sqlStmt = sqlStmt + ", '" + tExit.sCardNo + "'";
    sqlStmt = sqlStmt + ", " + std::to_string(tExit.iTransType);
    sqlStmt = sqlStmt + ", " + std::to_string(tExit.lParkedTime);
    sqlStmt = sqlStmt + ", " + std::to_string(tExit.sFee);
    sqlStmt = sqlStmt + ", " + std::to_string(tExit.sPaidAmt);
    sqlStmt = sqlStmt + ", '" + tExit.sReceiptNo + "'";
    sqlStmt = sqlStmt + ", " + std::to_string(tExit.iStatus);
    sqlStmt = sqlStmt + ", " + std::to_string(tExit.sRedeemAmt);
    sqlStmt = sqlStmt + ", " + std::to_string(tExit.iRedeemTime);
    sqlStmt = sqlStmt + ", '" + tExit.sRedeemNo + "'";
    sqlStmt = sqlStmt + ", " + std::to_string(tExit.sGSTAmt);
    sqlStmt = sqlStmt + ", '" + tExit.sCHUDebitCode + "'";
    sqlStmt = sqlStmt + ", " + std::to_string(tExit.iCardType);
    sqlStmt = sqlStmt + ", " + std::to_string(tExit.sTopupAmt);
    sqlStmt = sqlStmt + ", '" + tExit.uposbatchno + "'";
    sqlStmt = sqlStmt + ", '" + tExit.feedrom + "'";
    sqlStmt = sqlStmt + ", '" + tExit.lpn + "'";
    sqlStmt = sqlStmt + ")";

    r = localdb->SQLExecutNoneQuery(sqlStmt);
    if (r != 0)
    {
        Logger::getInstance()->FnLog(sqlStmt, "", "DB");
        Logger::getInstance()->FnLog("Insert Exit_Trans to local: fail.", "", "DB");
        return iLocalFail;
    }
    else
    {
        Logger::getInstance()->FnLog("Insert Exit_Trans to Local: success", "", "DB");
        operation::getInstance()->tProcess.glNoofOfflineData = operation::getInstance()->tProcess.glNoofOfflineData + 1;
        return iDBSuccess;
    }
}