#include <iostream>
#include <ctime>
#include <iomanip>
#include <stdlib.h>
#include <sstream>
#include <iomanip>  // For std::setprecision
#include <sys/time.h>
#include <boost/algorithm/string.hpp>
#include "db.h"
#include "log.h"
#include "operation.h"
#include "common.h"

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
	localdb->Disconnect();

	delete centraldb;
	delete localdb;
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

DBError db::insertbroadcasttrans(string sid,string iu_No,string cardno,string paidamt,string itype)
{

	std::string sqlStmt;
	string sLPRNo="";
	int r;
	int sNo;

	r = localdb->SQLExecutNoneQuery("DELETE FROM Entry_Trans where iu_tk_No = '" + iu_No + "'");
    //------
	CE_Time dt;
	string dtStr=dt.DateString()+" "+dt.TimeString();

	sqlStmt= "Insert into Entry_Trans ";
	sqlStmt=sqlStmt + "(Station_ID,Entry_Time,iu_tk_No,";
	sqlStmt=sqlStmt + "trans_type";
	sqlStmt=sqlStmt + ",card_no,paid_amt";

	sqlStmt = sqlStmt + ") Values ('" + sid+ "','" +dtStr+ "','" + iu_No;
	sqlStmt = sqlStmt +  "','" + itype;
	sqlStmt = sqlStmt + "','" + cardno + "','" + paidamt+"'";
	sqlStmt = sqlStmt +  ")";

	//operation::getInstance()->writelog (sqlStmt, "DB");

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
		//dbss << "Insert Broadcast Entry_trans to Local: success";
    	//Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		return iDBSuccess;
	}

}

DBError db::UpdateLocalEntry(string IUTkNo)
{
	int r=0;
	string sqstr="";

	// insert into Central trans tmp table

	sqstr="UPDATE Entry_Trans set Status = 3 WHERE iu_tk_no = '"+ IUTkNo + "'";
	
	r = localdb->SQLExecutNoneQuery(sqstr);

	if (r==0) 
	{
		
	}
	else {
		operation::getInstance()->writelog("fail to update Local Entry_Trans","DB");
		return iLocalFail;
	}
	
	return iDBSuccess;
}

DBError db::insertentrytrans(tEntryTrans_Struct& tEntry)
{

	std::string sqlStmt;
	string sLPRNo="";
	int r;
	int sNo;
	std::stringstream dbss;
	std::string gsTransID;

	//TK_Serialno is an integer type in DB
	//make sure the value must be the integer
	//to avoid exception
	tEntry.sEntryTime = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
	
	gsTransID = operation::getInstance()->tProcess.gsTransID;

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

	//std::cout<<"operation::getInstance()->tEntry.sSerialNo: " <<operation::getInstance()->tEntry.sSerialNo<<std::endl;

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
	sqlStmt = sqlStmt + "','" + gsTransID +"'";
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
	sqlStmt = sqlStmt + "','" + gsTransID +"'";

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
	
	r = centraldb->SQLSelect("SELECT TOP 10 * FROM season_mst WHERE s" + to_string(giStnid) + "_fetched = 0 ", &selResult, true);
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
		//operation::getInstance()->initStnParameters();
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
			//operation::getInstance()->initStnParameters();
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
        sqlStmt = sqlStmt + " (tariff_id, day_index";
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
        sqlStmt = sqlStmt + ", zone_cutoff, day_cutoff, whole_day_max, whole_day_min, day_type";
        sqlStmt = sqlStmt + ")";
        sqlStmt = sqlStmt + " VALUES (" + tariff.tariff_id;
        sqlStmt = sqlStmt + ", " + tariff.day_index;

        for (int i = 0; i < 9; i ++)
        {
            sqlStmt = sqlStmt + ", '" + tariff.start_time[i] + "'";
            sqlStmt = sqlStmt + ", '" + tariff.end_time[i] + "'";
            sqlStmt = sqlStmt + ", " + tariff.rate_type[i];
            sqlStmt = sqlStmt + ", " + tariff.charge_time_block[i];
            sqlStmt = sqlStmt + ", '" + tariff.charge_rate[i] + "'";
            sqlStmt = sqlStmt + ", " + tariff.grace_time[i];
			sqlStmt = sqlStmt + ", '" + tariff.min_charge[i] + "'";
            sqlStmt = sqlStmt + ", '" + tariff.max_charge[i] + "'";
            sqlStmt = sqlStmt + ", " + tariff.first_free[i];
            sqlStmt = sqlStmt + ", '" + tariff.first_add[i] + "'";
            sqlStmt = sqlStmt + ", " + tariff.second_free[i];
            sqlStmt = sqlStmt + ", '" + tariff.second_add[i] + "'";
            sqlStmt = sqlStmt + ", " + tariff.third_free[i];
            sqlStmt = sqlStmt + ", '" + tariff.third_add[i] + "'";
            sqlStmt = sqlStmt + ", " + tariff.allowance[i];
        }

        sqlStmt = sqlStmt + ", " + tariff.zone_cutoff;
        sqlStmt = sqlStmt + ", " + tariff.day_cutoff;
        sqlStmt = sqlStmt + ", '" + tariff.whole_day_max + "'";
        sqlStmt = sqlStmt + ", '" + tariff.whole_day_min + "'";
        sqlStmt = sqlStmt + ", '" + tariff.day_type + "'";
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

    operation::getInstance()->writelog("Download X_Tariff for group: " + std::to_string(iGrpID) + ", site: " + std::to_string(iSiteID), "DB");
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
       	ClearHoliday();

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

DBError db::loadZoneEntriesfromLocal()
{
	vector<ReaderItem> selResult;
	int r = -1;
	int giStnZoneid;
	int j = 0;
	giStnZoneid = operation::getInstance()->gtStation.iZoneID;

	r = localdb->SQLSelect("SELECT StationID FROM Station_Setup WHERE StationType = 1 and ZoneID = '" + std::to_string(giStnZoneid) + "'", &selResult, false);

	if (r != 0)
	{
		//m_log->WriteAndPrint("Get Station Setup: fail");
		return iLocalFail;
	}

	if (selResult.size() > 0)
	{            
			operation::getInstance()->tParas.gsZoneEntries = ",";
			
			for(j=0;j<selResult.size();j++){
				
			 operation::getInstance()->tParas.gsZoneEntries =  operation::getInstance()->tParas.gsZoneEntries + selResult[j].GetDataItem(0) + ",";

			}
		operation::getInstance()->writelog("Load ZoneEntries from Local: " + operation::getInstance()->tParas.gsZoneEntries , "DB");
		return iDBSuccess;
	}

	return iNoData; 
};

DBError db::loadParam()
{
	int r = -1;
	vector<ReaderItem> selResult;
	//------
	loadZoneEntriesfromLocal();
	//------
	loadparamfromCentral();

	operation::getInstance()->tParas.giTariffFeeMode = 0;   // for tesing, please help to load late 
	//------
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
				try
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

					if (readerItem.GetDataItem(0) == "NoIURetry")
					{
						operation::getInstance()->tParas.giNoIURetry = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "MaxDiffIU")
					{
						operation::getInstance()->tParas.giMaxDiffIU = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "LockBarrier")
					{
						operation::getInstance()->tParas.gbLockBarrier = (std::stoi(readerItem.GetDataItem(1)) == 1) ? true : false;
					}

					if (readerItem.GetDataItem(0) == "commportled2")
					{
						operation::getInstance()->tParas.giCommPortLED2 = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "CHUIP")
					{
						operation::getInstance()->tParas.gsCHUIP = readerItem.GetDataItem(1);
					}

					if (readerItem.GetDataItem(0) == "Site")
					{
						operation::getInstance()->tParas.gsSite = readerItem.GetDataItem(1);
					}

					if (readerItem.GetDataItem(0) == "Address")
					{
						operation::getInstance()->tParas.gsAddress = readerItem.GetDataItem(1);
					}

					if (readerItem.GetDataItem(0) == "TryTimes4NE")
					{
						operation::getInstance()->tParas.giTryTimes4NE = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "processreversedcmd")
					{
						operation::getInstance()->tParas.giProcessReversedCMD = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "hasthreewheelmc")
					{
						operation::getInstance()->tParas.giHasThreeWheelMC = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "MaxDebitDays")
					{
						operation::getInstance()->tParas.giMaxDebitDays = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "FirstHourMode")
					{
						operation::getInstance()->tParas.giFirstHourMode = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "PEAllowance")
					{
						operation::getInstance()->tParas.giPEAllowance = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "TariffFeeMode")
					{
						operation::getInstance()->tParas.giTariffFeeMode = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "TariffGTmode")
					{
						operation::getInstance()->tParas.giTariffGTMode = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "Hr2PEAllowance")
					{
						operation::getInstance()->tParas.giHr2PEAllowance = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "SeasonCharge")
					{
						operation::getInstance()->tParas.giSeasonCharge = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "ShowSeasonExpireDays")
					{
						operation::getInstance()->tParas.giShowSeasonExpireDays = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "ShowExpiredTime")
					{
						operation::getInstance()->tParas.giShowExpiredTime = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "MCyclePerDay")
					{
						operation::getInstance()->tParas.giMCyclePerDay = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "V3TransType")
					{
						operation::getInstance()->tParas.giV3TransType = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "V4TransType")
					{
						operation::getInstance()->tParas.giV4TransType = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "V5TransType")
					{
						operation::getInstance()->tParas.giV5TransType = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "FirstHour")
					{
						operation::getInstance()->tParas.giFirstHour = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "HasHolidayEve")
					{
						operation::getInstance()->tParas.giHasHolidayEve = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "HasRedemption")
					{
						operation::getInstance()->tParas.gbHasRedemption = (std::stoi(readerItem.GetDataItem(1)) == 1) ? true : false;
					}

					if (readerItem.GetDataItem(0) == "Company")
					{
						operation::getInstance()->tParas.gsCompany = readerItem.GetDataItem(1);
					}

					if (readerItem.GetDataItem(0) == "GSTNo")
					{
						operation::getInstance()->tParas.gsGSTNo = readerItem.GetDataItem(1);
					}

					if (readerItem.GetDataItem(0) == "Tel")
					{
						operation::getInstance()->tParas.gsTel = readerItem.GetDataItem(1);
					}

					if (readerItem.GetDataItem(0) == "ZIP")
					{
						operation::getInstance()->tParas.gsZIP = readerItem.GetDataItem(1);
					}

					if (readerItem.GetDataItem(0) == "GSTRate")
					{
						operation::getInstance()->tParas.gfGSTRate = std::stof(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "CHUCnTO")
					{
						operation::getInstance()->tParas.giCHUCnTO = std::stof(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "HdRec")
					{
						operation::getInstance()->tParas.gsHdRec = readerItem.GetDataItem(1);
					}

					if (readerItem.GetDataItem(0) == "HdTk")
					{
						operation::getInstance()->tParas.gsHdTk = readerItem.GetDataItem(1);
					}

					if (readerItem.GetDataItem(0) == "needcard4complimentary")
					{
						operation::getInstance()->tParas.giNeedCard4Complimentary = std::stoi(readerItem.GetDataItem(1));
					}

					if (readerItem.GetDataItem(0) == "ExitTicketRedemption")
					{
						operation::getInstance()->tParas.giExitTicketRedemption = std::stoi(readerItem.GetDataItem(1));
					}
				}
				catch (const std::invalid_argument &e)
				{
					std::stringstream ss;
					ss << "Invalid argument for key '" << readerItem.GetDataItem(0)
						<< "' with value '" << readerItem.GetDataItem(1) << "': " << e.what();
					Logger::getInstance()->FnLog(ss.str(), "", "DB");
				}
				catch (const std::out_of_range &e)
				{
					std::stringstream ss;
					ss << "Out of range for key '" << readerItem.GetDataItem(0)
						<< "' with value '" << readerItem.GetDataItem(1) << "': " << e.what();
					Logger::getInstance()->FnLog(ss.str(), "", "DB");
				}
				catch (const std::exception &e)
				{
					std::stringstream ss;
					ss << "Error processing key '" << readerItem.GetDataItem(0)
						<< "': " << e.what();
					Logger::getInstance()->FnLog(ss.str(), "", "DB");
				}
			}
		}
		operation::getInstance()->tProcess.gbloadedParam = true;
        return iDBSuccess;
	}

	return iNoData;
}

DBError db::loadparamfromCentral()
{
	int r;
	vector<ReaderItem> tResult;

	operation::getInstance()->writelog ("Load parameter from Centaral DB", "DB");
	
	r = centraldb->SQLSelect("SELECT group_id,site_id FROM site_setup", &tResult, true);
	if (r != 0)
	{
		return iCentralFail;
	}


	if (tResult.size()>0)
	{
		operation::getInstance()->tParas.giGroupID = std::stoi(tResult[0].GetDataItem(0));
		operation::getInstance()->tParas.giSite = std::stoi(tResult[0].GetDataItem(1));
		operation::getInstance()->writelog ("Load Group ID: " + std::to_string(operation:: getInstance()->tParas.giGroupID), "DB");
		operation::getInstance()->writelog ("Load Site ID: " + std::to_string(operation:: getInstance()->tParas.giSite), "DB");
	}
	
	r = centraldb->SQLSelect("SELECT entry_station FROM counter_definition where zone_id = " + to_string(operation::getInstance()->gtStation.iZoneID) , &tResult, true);
	if (r != 0)
	{
		return iCentralFail;
	}

	if (tResult.size()>0)
	{
		operation::getInstance()->tParas.gsZoneEntries = "," + tResult[0].GetDataItem(0) + ",";
		operation::getInstance()->writelog ("Load zone for entry: " + operation:: getInstance()->tParas.gsZoneEntries, "DB");
	}

	r = centraldb->SQLSelect("SELECT Max(receipt_no) FROM exit_trans where station_id  = " + to_string(operation::getInstance()->gtStation.iSID) , &tResult, true);
	if (r != 0)
	{
		return iCentralFail;
	}

	if (tResult.size()>0)
	{
		int l; 
		string A;
		long k;
		A = std::to_string(operation::getInstance()->gtStation.iSID);
		l = tResult[0].GetDataItem(0).length() - A.length();
		A = tResult[0].GetDataItem(0).substr(0,l);

		operation::getInstance()->writelog ("Load Last Receipt No: " + A, "DB");
		try
		{
			operation::getInstance()->tProcess.glLastSerialNo = std::stol(A);
		}
		catch (const std::exception& e)
		{
			operation::getInstance()->writelog ("Exception: " + std::string(e.what()), "DB");
		}
	
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

DBError db::loadExitLcdAndLedMessage(std::vector<ReaderItem>& selResult)
{
	if (selResult.size() > 0)
	{
		for (auto &readerItem : selResult)
		{
			if (readerItem.getDataSize() == 2)
			{
				if (readerItem.GetDataItem(0) == "BlackList")
				{
					operation::getInstance()->tExitMsg.MsgExit_BlackList[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_BlackList[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CBlackList")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_BlackList[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_BlackList[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "CardError")
				{
					operation::getInstance()->tExitMsg.MsgExit_CardError[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_CardError[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CCardError")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_CardError[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_CardError[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "CardIn")
				{
					operation::getInstance()->tExitMsg.MsgExit_CardIn[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_CardIn[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CCardIn")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_CardIn[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_CardIn[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "Comp2Val")
				{
					operation::getInstance()->tExitMsg.MsgExit_Comp2Val[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_Comp2Val[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CComp2Val")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_Comp2Val[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_Comp2Val[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "CompExpired")
				{
					operation::getInstance()->tExitMsg.MsgExit_CompExpired[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_CompExpired[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CCompExpired")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_CompExpired[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_CompExpired[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "Complimentary")
				{
					operation::getInstance()->tExitMsg.MsgExit_Complimentary[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_Complimentary[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CComplimentary")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_Complimentary[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_Complimentary[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "DebitFail")
				{
					operation::getInstance()->tExitMsg.MsgExit_DebitFail[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_DebitFail[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CDebitFail")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_DebitFail[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_DebitFail[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "DebitNak")
				{
					operation::getInstance()->tExitMsg.MsgExit_DebitNak[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_DebitNak[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CDebitNak")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_DebitNak[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_DebitNak[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "EntryDebit")
				{
					operation::getInstance()->tExitMsg.MsgExit_EntryDebit[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_EntryDebit[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CEntryDebit")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_EntryDebit[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_EntryDebit[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "ExpCard")
				{
					operation::getInstance()->tExitMsg.MsgExit_ExpCard[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_ExpCard[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CExpCard")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_ExpCard[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_ExpCard[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "FleetCard")
				{
					operation::getInstance()->tExitMsg.MsgExit_FleetCard[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_FleetCard[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CFleetCard")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_FleetCard[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_FleetCard[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "FreeParking")
				{
					operation::getInstance()->tExitMsg.MsgExit_FreeParking[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_FreeParking[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CFreeParking")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_FreeParking[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_FreeParking[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "GracePeriod")
				{
					operation::getInstance()->tExitMsg.MsgExit_GracePeriod[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_GracePeriod[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CGracePeriod")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_GracePeriod[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_GracePeriod[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "InvalidTicket")
				{
					operation::getInstance()->tExitMsg.MsgExit_InvalidTicket[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_InvalidTicket[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CInvalidTicket")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_InvalidTicket[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_InvalidTicket[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "WrongTicket")
				{
					operation::getInstance()->tExitMsg.MsgExit_WrongTicket[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_WrongTicket[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CWrongTicket")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_WrongTicket[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_WrongTicket[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "RedemptionExpired")
				{
					operation::getInstance()->tExitMsg.MsgExit_RedemptionExpired[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_RedemptionExpired[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CRedemptionExpired")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_RedemptionExpired[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_RedemptionExpired[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "IUProblem")
				{
					operation::getInstance()->tExitMsg.MsgExit_IUProblem[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_IUProblem[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CIUProblem")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_IUProblem[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_IUProblem[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "MasterSeason")
				{
					operation::getInstance()->tExitMsg.MsgExit_MasterSeason[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_MasterSeason[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CMasterSeason")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_MasterSeason[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_MasterSeason[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "NoEntry")
				{
					operation::getInstance()->tExitMsg.MsgExit_NoEntry[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_NoEntry[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CNoEntry")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_NoEntry[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_NoEntry[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "PrinterError")
				{
					operation::getInstance()->tExitMsg.MsgExit_PrinterError[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_PrinterError[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CPrinterError")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_PrinterError[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_PrinterError[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "RedemptionTicket")
				{
					operation::getInstance()->tExitMsg.MsgExit_RedemptionTicket[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_RedemptionTicket[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CRedemptionTicket")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_RedemptionTicket[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_RedemptionTicket[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SeasonBlocked")
				{
					operation::getInstance()->tExitMsg.MsgExit_SeasonBlocked[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_SeasonBlocked[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSeasonBlocked")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_SeasonBlocked[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_SeasonBlocked[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SeasonExpired")
				{
					operation::getInstance()->tExitMsg.MsgExit_SeasonExpired[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_SeasonExpired[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSeasonExpired")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_SeasonExpired[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_SeasonExpired[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SeasonInvalid")
				{
					operation::getInstance()->tExitMsg.MsgExit_SeasonInvalid[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_SeasonInvalid[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSeasonInvalid")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_SeasonInvalid[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_SeasonInvalid[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SeasonNotStart")
				{
					operation::getInstance()->tExitMsg.MsgExit_SeasonNotStart[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_SeasonNotStart[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSeasonNotStart")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_SeasonNotStart[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_SeasonNotStart[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SeasonOnly")
				{
					operation::getInstance()->tExitMsg.MsgExit_SeasonOnly[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_SeasonOnly[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSeasonOnly")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_SeasonOnly[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_SeasonOnly[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SeasonPassback")
				{
					operation::getInstance()->tExitMsg.MsgExit_SeasonPassback[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_SeasonPassback[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSeasonPassback")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_SeasonPassback[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_SeasonPassback[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SeasonRegNoIU")
				{
					operation::getInstance()->tExitMsg.MsgExit_SeasonRegNoIU[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_SeasonRegNoIU[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSeasonRegNoIU")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_SeasonRegNoIU[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_SeasonRegNoIU[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SeasonRegOK")
				{
					operation::getInstance()->tExitMsg.MsgExit_SeasonRegOK[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_SeasonRegOK[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSeasonRegOK")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_SeasonRegOK[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_SeasonRegOK[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SeasonTerminated")
				{
					operation::getInstance()->tExitMsg.MsgExit_SeasonTerminated[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_SeasonTerminated[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSeasonTerminated")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_SeasonTerminated[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_SeasonTerminated[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "SystemError")
				{
					operation::getInstance()->tExitMsg.MsgExit_SystemError[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_SystemError[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CSystemErrord")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_SystemError[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_SystemError[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "TakeCard")
				{
					operation::getInstance()->tExitMsg.MsgExit_TakeCard[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_TakeCard[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CTakeCard")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_TakeCard[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_TakeCard[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "TicketExpired")
				{
					operation::getInstance()->tExitMsg.MsgExit_TicketExpired[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_TicketExpired[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CTicketExpired")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_TicketExpired[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_TicketExpired[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "TicketNotFound")
				{
					operation::getInstance()->tExitMsg.MsgExit_TicketNotFound[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_TicketNotFound[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CTicketNotFound")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_TicketNotFound[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_TicketNotFound[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "UsedTicket")
				{
					operation::getInstance()->tExitMsg.MsgExit_UsedTicket[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_UsedTicket[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CUsedTicket")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_UsedTicket[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_UsedTicket[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "WrongCard")
				{
					operation::getInstance()->tExitMsg.MsgExit_WrongCard[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_WrongCard[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CWrongCard")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_WrongCard[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_WrongCard[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "XCardAgain")
				{
					operation::getInstance()->tExitMsg.MsgExit_XCardAgain[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_XCardAgain[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CXCardAgain")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_XCardAgain[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_XCardAgain[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "XCardTaken")
				{
					operation::getInstance()->tExitMsg.MsgExit_XCardTaken[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_XCardTaken[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CXCardTaken")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_XCardTaken[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_XCardTaken[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "XDefaultIU")
				{
					operation::getInstance()->tExitMsg.MsgExit_XDefaultIU[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_XDefaultIU[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CXDefaultIU")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_XDefaultIU[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_XDefaultIU[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "XDefaultLED")
				{
					operation::getInstance()->tExitMsg.MsgExit_XDefaultLED[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_XDefaultLED[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CXDefaultLED")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_XDefaultLED[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_XDefaultLED[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "XDefaultLED2")
				{
					operation::getInstance()->tExitMsg.MsgExit_XDefaultLED2[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_XDefaultLED2[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CXDefaultLED2")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_XDefaultLED2[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_XDefaultLED2[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "XExpiringSeason")
				{
					operation::getInstance()->tExitMsg.MsgExit_XExpiringSeason[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_XExpiringSeason[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CXExpiringSeason")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_XExpiringSeason[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_XExpiringSeason[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "XIdle")
				{
					operation::getInstance()->tExitMsg.MsgExit_XIdle[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_XIdle[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CXIdle")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_XIdle[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_XIdle[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "XLoopA")
				{
					operation::getInstance()->tExitMsg.MsgExit_XLoopA[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_XLoopA[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CXLoopA")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_XLoopA[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_XLoopA[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "XLowBal")
				{
					operation::getInstance()->tExitMsg.MsgExit_XLowBal[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_XLowBal[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CXLowBal")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_XLowBal[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_XLowBal[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "XNoCard")
				{
					operation::getInstance()->tExitMsg.MsgExit_XNoCard[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_XNoCard[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CXNoCard")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_XNoCard[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_XNoCard[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "XNoCHU")
				{
					operation::getInstance()->tExitMsg.MsgExit_XNoCHU[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_XNoCHU[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CXNoCHU")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_XNoCHU[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_XNoCHU[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "XNoIU")
				{
					operation::getInstance()->tExitMsg.MsgExit_XNoIU[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_XNoIU[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CXNoIU")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_XNoIU[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_XNoIU[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "XSameLastIU")
				{
					operation::getInstance()->tExitMsg.MsgExit_XSameLastIU[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_XSameLastIU[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CXSameLastIU")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_XSameLastIU[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_XSameLastIU[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "XSeasonWithinAllowance")
				{
					operation::getInstance()->tExitMsg.MsgExit_XSeasonWithinAllowance[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_XSeasonWithinAllowance[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CXSeasonWithinAllowance")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_XSeasonWithinAllowance[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_XSeasonWithinAllowance[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "XValidSeason")
				{
					operation::getInstance()->tExitMsg.MsgExit_XValidSeason[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_XValidSeason[1] = readerItem.GetDataItem(1);
				}

				// Update LCD Message
				if (readerItem.GetDataItem(0) == "CXValidSeason")
				{
					if ((!readerItem.GetDataItem(1).empty()) && (boost::algorithm::to_lower_copy(readerItem.GetDataItem(1)) != "null"))
					{
						operation::getInstance()->tExitMsg.MsgExit_XValidSeason[1].clear();
						operation::getInstance()->tExitMsg.MsgExit_XValidSeason[1] = readerItem.GetDataItem(1);
					}
				}

				if (readerItem.GetDataItem(0) == "X1enhancedMCParking")
				{
					operation::getInstance()->tExitMsg.MsgExit_X1enhancedMCParking[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_X1enhancedMCParking[1] = readerItem.GetDataItem(1);
				}

				if (readerItem.GetDataItem(0) == "XenhancedMCParking")
				{
					operation::getInstance()->tExitMsg.MsgExit_XenhancedMCParking[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_XenhancedMCParking[1] = readerItem.GetDataItem(1);
				}

				if (readerItem.GetDataItem(0) == "XSPT3Parking")
				{
					operation::getInstance()->tExitMsg.MsgExit_XSPT3Parking[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_XSPT3Parking[1] = readerItem.GetDataItem(1);
				}

				if (readerItem.GetDataItem(0) == "XVIPHolderParking")
				{
					operation::getInstance()->tExitMsg.MsgExit_XVIPHolderParking[0] = readerItem.GetDataItem(1);
					operation::getInstance()->tExitMsg.MsgExit_XVIPHolderParking[1] = readerItem.GetDataItem(1);
				}
			}
		}
		operation::getInstance()->tProcess.gbloadedLEDExitMsg = true;
		return iDBSuccess;
	}

	return iNoData;
}

DBError db::loadExitmessage()
{
	int r = -1;
	DBError retErr;
	vector<ReaderItem> exitLedSelResult;
	vector<ReaderItem> exitLcdSelResult;

	r = localdb->SQLSelect("select msg_id, msg_body from message_mst", &exitLedSelResult, true);
	if (r != 0)
	{
		operation::getInstance()->writelog("load Exit LED message failed.", "DB");
		return iLocalFail;
	}

	retErr = loadExitLcdAndLedMessage(exitLedSelResult);
	if (retErr == DBError::iNoData)
	{
		return retErr;
	}

	r = localdb->SQLSelect("select msg_id, msg_body from message_mst where m_status >= 10", &exitLcdSelResult, true);
	if (r != 0)
	{
		operation::getInstance()->writelog("load Exit LCD message failed.", "DB");
		return iLocalFail;
	}

	retErr = loadExitLcdAndLedMessage(exitLcdSelResult);
	if (retErr == DBError::iNoData)
	{
		return retErr;
	}

	return retErr;
}

DBError db::loadTR(int iType)
{
	int r = -1;
	DBError retErr;
	vector<ReaderItem> trSelResult;
	std::string sqlStmt;
	//----
	iType = 2;
	//----
	sqlStmt = "SELECT LineText, LineVar, LineFont, LineAlign from TR_mst";
	sqlStmt = sqlStmt + " WHERE TRType=" + std::to_string(iType) + " AND Enabled = 1 ORDER BY Line_no";

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

	r = centraldb->SQLSelect("SELECT Description FROM trans_type where trans_type =" + std::to_string(iTransType), &tResult, true);
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
	int s1=-1;
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
			sqlStmt = sqlStmt + ",lpn,Entry_ID, entry_time";
			
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
					tex.lpn = selResult[j].GetDataItem(17);
					tex.iEntryID = std::stoi(selResult[j].GetDataItem(18));
					tex.sEntryTime = selResult[j].GetDataItem(19);
					
					s=insertTransToCentralExitTransTmp(tex);
					
				}
				
				//insert record into central DB

				if (s==0)
				{
			
					if(operation::getInstance()->gtStation.iType==tientry){
						d=deleteLocalTrans (ter.sIUTKNo,ter.sEntryTime,ctrl);
					}
					else{
						s1=DeleteBeforeInsertMT(tex);
						s1=insert2movementtrans(tex);
						d=deleteLocalTrans (tex.sIUNo,tex.sExitTime,ctrl);
					}
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


int db::insertTransToCentralExitTransTmp(const tExitTrans_Struct& tex)
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

	r = centraldb->SQLSelect("SELECT type FROM BlackList where status = 0 and CAN = '" + sIU + "'", &tResult, true);
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

int db::HouseKeeping()
{
	std::string sqlStmt;
	int r=-1;// success flag
	//-------
	clearexpiredseason();
	//---------
	if(operation::getInstance()->gtStation.iType==tientry)
	{
		try {
			sqlStmt="Delete FROM Entry_Trans WHERE send_status = true or TIMESTAMPDIFF(HOUR, entry_time, NOW()) >=" + std::to_string(operation::getInstance()->tParas.giDataKeepDays *24);

			r=localdb->SQLExecutNoneQuery(sqlStmt);

			if(r==0) m_local_db_err_flag=0;
			else m_local_db_err_flag=1;
		}
		catch (const std::exception &e)
		{	
			operation::getInstance()->writelog("DB: local db error in housekeeping(Entry_Trans): " + std::string(e.what()),"DB");
			r=-1;
			m_local_db_err_flag=1;
		} 
	}else{

		try {
			sqlStmt="Delete FROM Exit_Trans WHERE send_status = true or TIMESTAMPDIFF(HOUR, exit_time, NOW()) >= " + std::to_string(operation::getInstance()->tParas.giDataKeepDays *24);

			r=localdb->SQLExecutNoneQuery(sqlStmt);

			if(r==0) m_local_db_err_flag=0;
			else m_local_db_err_flag=1;
		}
		catch (const std::exception &e)
		{	
			operation::getInstance()->writelog("DB: local db error in housekeeping(Exit_Trans): " + std::string(e.what()),"DB");
			r=-1;
			m_local_db_err_flag=1;
		} 

		try {
			sqlStmt="Delete FROM Entry_Trans WHERE TIMESTAMPDIFF(HOUR, entry_time, NOW()) >= " + std::to_string(operation::getInstance()->tParas.giDataKeepDays *24);

			r=localdb->SQLExecutNoneQuery(sqlStmt);

			if(r==0) m_local_db_err_flag=0;
			else m_local_db_err_flag=1;
		}
		catch (const std::exception &e)
		{	
			operation::getInstance()->writelog("DB: local db error in housekeeping(XEntry_Trans): " + std::string(e.what()),"DB");
			r=-1;
			m_local_db_err_flag=1;
		} 

	}

	return 0;
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

int db::updateExitTrans(string lpn, string sTransID) 
{

	int r=0;
	string sqstr="";

	// insert into Central trans tmp table

	sqstr="UPDATE Exit_trans_tmp set lpn = '"+ lpn + "' WHERE exit_lpn_sid = '"+ sTransID + "'";
	
	r = centraldb->SQLExecutNoneQuery(sqstr);

	if (r==0) 
	{
		if (centraldb->NumberOfRowsAffected > 0){
			operation::getInstance()->writelog("Success update LPR to Exit_Trans_Tmp","DB");
		}else
		{
			sqstr="UPDATE exit_trans set lpn = '"+ lpn + "' WHERE exit_lpn_sid = '"+ sTransID + "'";
			r = centraldb->SQLExecutNoneQuery(sqstr);

			if (r==0) {
				if (centraldb->NumberOfRowsAffected > 0)
				{
					operation::getInstance()->writelog("Success update LPR to Exit_Trans","DB");
					m_remote_db_err_flag=0;
				} else operation::getInstance()->writelog("No TransID for update","DB");
			} else
			{
			operation::getInstance()->writelog("fail to update LPR to Exit_trans","DB");
		 	m_remote_db_err_flag=1;
			}
		}
	}
	else {
		operation::getInstance()->writelog("fail to update LPR to Exit_trans_Tmp","DB");
		 m_remote_db_err_flag=1;
		
	}
	return r;
}

DBError db::insertexittrans(tExitTrans_Struct& tExit)
{
    std::string sqlStmt;
    int r;
    std::stringstream dbss;

	std::string gsTransID;
	gsTransID = operation::getInstance()->tProcess.gsTransID;
	//---------Add commentMore actions
	tExit.sFee = operation::getInstance()->GfeeFormat(tExit.sFee);
	tExit.sPaidAmt = operation::getInstance()->GfeeFormat(tExit.sPaidAmt);
	tExit.sRedeemAmt = operation::getInstance()->GfeeFormat(tExit.sRedeemAmt);
	tExit.sGSTAmt = operation::getInstance()->GfeeFormat(tExit.sGSTAmt);

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

    sqlStmt = "INSERT INTO exit_trans_tmp (station_id, exit_time, iu_tk_no, card_mc_no, trans_type, parked_time";
    sqlStmt = sqlStmt + ", parking_fee, paid_amt, receipt_no, status, redeem_amt, redeem_time, redeem_no";
    sqlStmt = sqlStmt + ", gst_amt, chu_debit_code, card_type, top_up_amt, uposbatchno, feefrom, lpn, exit_lpn_SID";
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
    sqlStmt = sqlStmt + ", '" + tExit.feefrom + "'";
    sqlStmt = sqlStmt + ", '" + tExit.lpn + "'";
	sqlStmt = sqlStmt + ", '" + gsTransID + "'";
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
        return iCentralSuccess;
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
    sqlStmt = sqlStmt + ", gst_amt, chu_debit_code, Card_Type, Top_Up_Amt, uposbatchno, feefrom, lpn, Entry_ID,entry_time, exit_lpn_SID";
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
    sqlStmt = sqlStmt + ", '" + tExit.feefrom + "'";
    sqlStmt = sqlStmt + ", '" + tExit.lpn + "'";
	sqlStmt = sqlStmt + "," + std::to_string(tExit.iEntryID) ;
	sqlStmt = sqlStmt + ", '" + tExit.sEntryTime + "'";
	sqlStmt = sqlStmt + ", '" + gsTransID + "'";
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
        return iLocalSuccess;
    }
}

int db::updateExitReceiptNo(string sReceiptNo, string StnID) 
{

	int r=0;
	string sqstr="";

	// insert into Central trans tmp table

	sqstr="UPDATE Exit_trans_tmp set receipt_no = '"+ sReceiptNo + "' WHERE station_id = '"+ StnID + "' and Exit_time = '"+ operation::getInstance()->tExit.sExitTime + "'";
	
	r = centraldb->SQLExecutNoneQuery(sqstr);

	if (r==0) 
	{
		if (centraldb->NumberOfRowsAffected > 0){
			operation::getInstance()->writelog("Success update Receipt No to Exit_Trans_Tmp","DB");
		}else
		{
			sqstr="UPDATE exit_trans set receipt_no = '"+ sReceiptNo + "' WHERE station_id = '"+ StnID + "' and Exit_time = '"+ operation::getInstance()->tExit.sExitTime + "'";
			r = centraldb->SQLExecutNoneQuery(sqstr);

			if (r==0) {
				if (centraldb->NumberOfRowsAffected > 0)
				{
					operation::getInstance()->writelog("Success update Receipt No to Exit_Trans","DB");
					m_remote_db_err_flag=0;
				} else operation::getInstance()->writelog("No Receipt for update","DB");
			} else
			{
			operation::getInstance()->writelog("fail to update Receipt to Exit_trans","DB");
		 	m_remote_db_err_flag=1;
			}
		}
	}
	else {
		operation::getInstance()->writelog("fail to update Receipt to Exit_trans_Tmp","DB");
		 m_remote_db_err_flag=1;
		
	}
	return r;
}

DBError db::LoadTariff()
{

	std::string sqlStmt;
	std::string tbName="tariff_setup";

	vector<ReaderItem> selResult;
	
	std::string sValue;
	int r,j,k,i;
	tariff_struct t;
	int w=-1;
	int bTried=0;

	try
	{

		operation::getInstance()->writelog("Load tariff_setup: Started","DB");

		Try_Again:
		sqlStmt="select * ";
		sqlStmt= sqlStmt +  "from " + tbName + " order by day_index";

	//	operation::getInstance()->writelog(sqlStmt, "DB");

		r=localdb->SQLSelect(sqlStmt,&selResult,true);
		if (r!=0) return iLocalFail;
		if (selResult.size()>0){
			for(j=0;j<selResult.size();j++){
				int idx = 2;
	//			operation::getInstance()->writelog("loading"+ std::to_string(j), "DB");
				t.tariff_id = selResult[j].GetDataItem(0);
				t.day_index = selResult[j].GetDataItem(1);
				for(int k=0;k<9;k++){
					t.start_time[k]=selResult[j].GetDataItem(idx++);
					if (t.start_time[k]=="NULL") t.start_time[k]="";
					t.end_time[k]=selResult[j].GetDataItem(idx++);
					if (t.end_time[k]=="NULL") t.end_time[k]="";
					t.rate_type[k]=selResult[j].GetDataItem(idx++);
					t.charge_time_block[k]=selResult[j].GetDataItem(idx++);
					t.charge_rate[k]=selResult[j].GetDataItem(idx++);
					t.grace_time[k]=selResult[j].GetDataItem(idx++);
					t.min_charge[k]=selResult[j].GetDataItem(idx++);
					t.max_charge[k]=selResult[j].GetDataItem(idx++);
					t.first_free[k]=selResult[j].GetDataItem(idx++);
					t.first_add[k]=selResult[j].GetDataItem(idx++);
					t.second_free[k]=selResult[j].GetDataItem(idx++);
					t.second_add[k]=selResult[j].GetDataItem(idx++);
					t.third_free[k]=selResult[j].GetDataItem(idx++);
					t.third_add[k]=selResult[j].GetDataItem(idx++);
					t.allowance[k]=selResult[j].GetDataItem(idx++);
				}
				t.whole_day_max= selResult[j].GetDataItem(idx++);
				t.whole_day_min= selResult[j].GetDataItem(idx++);
				t.zone_cutoff= selResult[j].GetDataItem(idx++);
				t.day_cutoff= selResult[j].GetDataItem(idx++);
				t.day_type	= selResult[j].GetDataItem(idx++);

				std::vector<std::string> tmpStr;
		
				boost::algorithm::split(tmpStr, t.day_type, boost::algorithm::is_any_of(","));

				operation::getInstance()->writelog("loading Tday_Type = "+ t.day_type, "DB");

				for (std::size_t i = 0; i < tmpStr.size() - 1; i++)
				{
					t.dtype= std::stoi(tmpStr[i]);
				//	operation::getInstance()->writelog("loading day_Type = "+ std::to_string(t.dtype), "DB");
					w=WriteTariff2RAM(t);
				//	operation::getInstance()->writelog("loadingA next", "DB");	
				}
				//operation::getInstance()->writelog("loading next", "DB");	
			}

			operation::getInstance()->writelog("Load tariff parameters: success","DB");

			return iDBSuccess;
		}
		else{
			if(bTried==0)
			{
				downloadtariffsetup();
				bTried=1;
				goto Try_Again;
				
			}

			return iNoData;
			operation::getInstance()->writelog("Load tariff parameters error: no data in local DB","DB");

		}
	}
	catch(const std::exception &e)
	{
		operation::getInstance()->writelog("Load tariff parameters error: local DB error","DB");
		return iLocalFail;
	}

}

int db::WriteTariff2RAM(tariff_struct t)
{

	time_t vfdate,vtdate;
	CE_Time vfdt,vtdt;
	int r=-1;// success flag
	int debug=0;
	int a,b;

	a=t.dtype/8;
	b=(t.dtype%8);
	if(b==0)
	{
		b=8;
		a=a-1;
		if(a<0)a=0;
	}
	
	gtariff[a][b].day_type=std::to_string(t.dtype);

	gtariff[a][b].tariff_id =t.tariff_id;
	gtariff[a][b].day_index =t.day_index;		
	for(int k=0;k<9;k++){
		gtariff[a][b].start_time[k] =t.start_time[k];
		gtariff[a][b].end_time[k] =t.end_time[k];
		gtariff[a][b].rate_type[k] =t.rate_type[k];
		gtariff[a][b].charge_time_block[k] =t.charge_time_block[k];
		gtariff[a][b].charge_rate[k] =t.charge_rate[k];
		gtariff[a][b].grace_time[k] =t.grace_time[k];
		gtariff[a][b].first_free[k] =t.first_free[k];
		gtariff[a][b].first_add[k] =t.first_add[k];
		gtariff[a][b].second_free[k] =t.second_free[k];
		gtariff[a][b].second_add[k] =t.second_add[k];
		gtariff[a][b].third_free[k] =t.third_free[k];
		gtariff[a][b].third_add[k] =t.third_add[k];
		gtariff[a][b].allowance[k] =t.allowance[k];
		gtariff[a][b].min_charge[k] =t.min_charge[k];
		gtariff[a][b].max_charge[k] =t.max_charge[k];
	}
	gtariff[a][b].zone_cutoff =t.zone_cutoff;
	gtariff[a][b].day_cutoff =t.day_cutoff;
	gtariff[a][b].whole_day_max =t.whole_day_max;
	gtariff[a][b].whole_day_min =t.whole_day_min;
	
	return(1);
}

DBError db::LoadHoliday()
{

	std::string sqlStmt;
	std::string tbName="holiday_mst";

	vector<ReaderItem> selResult;

	std::string sValue;
	int r,j;
	int w=-1;
	int bTried=0;

	try
	{

		operation::getInstance()->writelog ("Load holiday: Started", "DB");

		Try_Again:
		sqlStmt="Select date_format(holiday_date,'%Y-%m-%d') as YourDateAsString ";
		sqlStmt= sqlStmt +  " FROM " + tbName + " Order by holiday_date";

		r=localdb->SQLSelect(sqlStmt,&selResult,true);
		if (r!=0) return iLocalFail;

		if (selResult.size()>0){
		
			msholiday.clear();
		
			for(j=0;j<selResult.size();j++){
				
				sValue=selResult[j].GetDataItem(0);
				msholiday.push_back(sValue);

			}

			operation::getInstance()->writelog("Load holiday: success", "DB");

			return iDBSuccess;
		}
		else{
			if(bTried==0)
			{
				downloadholidaymst();
				bTried=1;
				goto Try_Again;
				
			}

			return iNoData;
			operation::getInstance()->writelog("Load holiday error: no data in local DB", "DB");

		}

	}
	catch(const std::exception &e)
	{
		operation::getInstance()->writelog("Load holiday error: local DB error", "DB");
		return iLocalFail;
	}

}

DBError db::ClearHoliday()
{
	std::string tableNm="holiday_mst";
	
	std::string sqlStmt;
	
	int r=-1;      // success flag
	try {

		sqlStmt="truncate table " + tableNm;

		r=localdb->SQLExecutNoneQuery(sqlStmt);

		if(r==0) m_local_db_err_flag=0;
		else m_local_db_err_flag=1;
	}
	catch (const std::exception &e)
	{
		operation::getInstance()->writelog("DB: local db error in clearing: " + std::string(e.what()), "DB");
		r=-1;
		m_local_db_err_flag=1;
	} 
	if (r= 0) return iDBSuccess;
	return iLocalFail;
}


int db::GetDayTypeWithHE(CE_Time curr_date)
{
	int iRet;
	CE_Time next_date;
	iRet=curr_date.getweekday();

	//operation::getInstance()->writelog("day Type is: " + std::to_string(iRet), "DB");

	next_date.SetTime(curr_date.GetUnixTimestamp()+86400);    // 86400 one day
	//operation::getInstance()->writelog("next date time is: " + next_date.DateString(), "DB");
	
	if(iRet!=6)
	{
		if(iRet!=7)
		{
			for(int i=0; i<msholiday.size();i++){
				if(next_date.DateString().compare(msholiday[i])==0)
				{
					iRet=8;
					break;
				}
			}
		}
	}
	
	for(int i=0; i<msholiday.size();i++){
		if(curr_date.DateString().compare(msholiday[i])==0)
		{
			iRet=7;
			return(iRet);
		}
	}
	return(iRet);
}

int db::GetDayTypeNoPE(CE_Time curr_date)
{
	int iRet;
//	operation::getInstance()->writelog("check day type, no holiday eve", "DB");
//	operation::getInstance()->writelog("Current day: " + curr_date.DateString(), "DB");

	iRet=curr_date.getweekday();
	for(int i=0; i<msholiday.size();i++){

//		operation::getInstance()->writelog("holiday: " + msholiday[i], "DB");

		if(curr_date.DateString().compare(msholiday[i])==0)
		{
			iRet=8;
			return(iRet);
		}
	}
	return(iRet);
}

int db::GetDayType(CE_Time curr_date)
{
	int Ret;
	if(operation::getInstance()->tParas.giHasHolidayEve==1)
		Ret=GetDayTypeWithHE(curr_date);//PH is 7, EvePH is 8
	else
		Ret=GetDayTypeNoPE(curr_date);
	return(Ret);
	
}

float db::HasPaidWithinPeriod(string sTimeFrom, string sTimeTo)
{
	string msCurrentIU;
	std::string sqlStmt;
	vector<ReaderItem> selResult;

	if(operation::getInstance()->gtStation.iType==tientry)
	msCurrentIU=operation::getInstance()->tEntry.sIUTKNo;
	else
	msCurrentIU=operation::getInstance()->tExit.sIUNo;

	if((msCurrentIU.length()==10)||((operation::getInstance()->tParas.giMCyclePerDay==2)&&(msCurrentIU.length()==16)))
	{
		//ok
	}
	else
	return(0);

	int r=-1;
	float w=-1;
	
	sqlStmt= "Select paid_amt From exit_trans ";
	sqlStmt=sqlStmt + "WHERE iu_tk_no = '"+msCurrentIU;
	sqlStmt=sqlStmt + "'and paid_amt >0";
	
	if(msCurrentIU.length()==16)
	sqlStmt=sqlStmt + " and (trans_type=7 or trans_type=22)";
	sqlStmt=sqlStmt + " And convert(char(19),exit_time,120) > '"+ sTimeFrom+ "'";

	r = centraldb->SQLSelect(sqlStmt, &selResult, true);
	
	if(r!=0) 
	{
		m_remote_db_err_flag=1;
		goto processLocal;
	}
	else m_remote_db_err_flag=0;

	if (selResult.size()>0)
	{
		w=std::stof(selResult[0].GetDataItem(0));
		operation::getInstance()->writelog("payfee is: " + std::to_string(w), "DB");
		return w;
	}
	if (selResult.size()<1){
		r=-1; //should raise error
		goto processLocal;
	}
	
processLocal:

	sqlStmt= "Select paid_amt From entry_trans ";
	sqlStmt=sqlStmt + " Where iu_tk_no='"+ msCurrentIU + "' and paid_amt>0";

	r = centraldb->SQLSelect(sqlStmt, &selResult, true);

	if(r!=0) m_local_db_err_flag=1;
	else  m_local_db_err_flag=0;

	if (selResult.size()>0)
	{
		w=std::stof(selResult[0].GetDataItem(0));
		operation::getInstance()->writelog("payfee is: " + std::to_string(w), "DB");
		return w;
	}
	return w;

}

float db::RoundIt(float val, int giTariffFeeMode)
{
	float iRet;
	
	if(giTariffFeeMode==1) iRet=ceilf(val); // rounded up
	else if(giTariffFeeMode==2) iRet=floorf(val); //rounded down
	else if(giTariffFeeMode==3) iRet=roundf(val); //normal(.5 to 1)
	return(iRet);
	
};

float db::CalFeeRAM2G(string eTime, string payTime,int iTransType, bool bNoGT) 
{
	float iRet=0;
	bool bUsedTariff[2];
	int giGT,timediff;
	float tempfee = 0;
	int giTransType;
	CE_Time calTime;
	CE_Time payET,payDT;
	payET.SetTime(eTime);
	payDT.SetTime(payTime);
	//-------
	for(int i=0; i<2;i++){
		bUsedTariff[i] = false;
		if (eTime > gtarifftypeinfo[i].start_time) {
			if (gtarifftypeinfo[i].end_time > payTime) {
					bUsedTariff[i] = true;
					break;
			} else{
				if (eTime < gtarifftypeinfo[i].end_time ) {
					if (i == 0){
						bUsedTariff[i] = true;
						bUsedTariff[i+1] = true;
					}else{
						operation::getInstance()->writelog("Tariff type info Error", "DB");
						return(-4);
					}
				}
			} 
		}
	}

	if (bUsedTariff[0] == true && bUsedTariff[1] == true) {
		// cross two zone,need get grace time
		giGT = std::stoi(gtariff[iTransType][1].grace_time[0]);

		timediff=calTime.diffmin(payET.GetUnixTimestamp(), payDT.GetUnixTimestamp());
		if (timediff> giGT) {
			giTransType = std::stoi(gtarifftypeinfo[0].tariff_type) *40;
			iRet = CalFeeRAM2GR(eTime,gtarifftypeinfo[0].end_time,iTransType+ giTransType, true);
			operation::getInstance()->writelog("Fee For Early Tariff: " + Common::getInstance()->SetFeeFormat(iRet),"DB");
			giTransType = std::stoi(gtarifftypeinfo[1].tariff_type) *40;
			tempfee = CalFeeRAM2GR(gtarifftypeinfo[1].start_time,payTime,iTransType + giTransType, true);
			operation::getInstance()->writelog("Fee For Current Tariff: " + Common::getInstance()->SetFeeFormat(tempfee),"DB");
			iRet = iRet + tempfee;
		} else{
			operation::getInstance()->writelog("within grace period","DB");
			return(0);
		}
	} else{
		if (bUsedTariff[0] == true) {
			operation::getInstance()->writelog("Use Tariff Type:" + gtarifftypeinfo[0].tariff_type, "DB");
			giTransType = std::stoi(gtarifftypeinfo[0].tariff_type) *40;
			tempfee = CalFeeRAM2GR(eTime,payTime,iTransType + giTransType, bNoGT);
		}else{
			operation::getInstance()->writelog("Use Tariff Type:" + gtarifftypeinfo[1].tariff_type, "DB");
			giTransType = std::stoi(gtarifftypeinfo[1].tariff_type) *40;
			tempfee = CalFeeRAM2GR(eTime,payTime,iTransType + giTransType, bNoGT);
		}
		iRet = tempfee;
	}
	operation::getInstance()->writelog("Total Parking Fee: " + Common::getInstance()->SetFeeFormat(iRet), "DB");

	return iRet;

}

float db::CalFeeRAM2GR(string eTime, string payTime,int iTransType, bool bNoGT) 
{
	CE_Time entryTime;
	CE_Time payDT;
	CE_Time calTime;
	CE_Time currentTime;
	CE_Time PD,pt;
	CE_Time Lpd,Npd;
	entryTime.SetTime(eTime);
	payDT.SetTime(payTime);
	float dayFee=0, zoneFee=0;
	int iDayType;
	bool bFirstFreed = false; // for first free time, only once
	bool bGotDayInfo = false; // for get day info, only once for a day
	bool b24HourBlock = false;
	//bool bGT = false; //for grace time, only valid for 1st zone
	bool bMCPerDayChecked= false;
	int i24HourBlocks;
	int s24HourFee=0, s24HourCharges=0;
	string dtStr;
	CE_Time zt;

	string sT[9],eT[9];
	int rateType[10],GT[10];
	float chargeRate[9];
	int CTB[9];
	float zoneMin[9],zoneMax[9];
	float firstAdd[9],secondAdd[9],thirdAdd[9];
	int firstFree[9],secondFree[9],thirdFree[9];
	int iAllowance[9];
	int maxZone, zoneRateType;
	int iZoneCutoff,iDayCutoff;
	float dayMin, dayMax;
	int iNextGT,iNextRT;
	CE_Time zoneTime[10];
	
	int currentZone, currentRateType;
	int currentCTB, currentGT;
	float currentRate=0, currentMin=0, currentMax=0;
	float currentAdd, currentAdd2, currentAdd3;
	int currentFree, currentFree2, currentFree3;
	int currentAllowance;
	float charge=0;
	int timediff;
	float haspaid;
	float iRet=0;

	if(bNoGT== true) 
		operation::getInstance()->writelog( "Calculate parking fee with no grace .... ", "DB");
	else 
		operation::getInstance()->writelog("Calculate parking fee for rate type: "+ to_string(iTransType), "DB");
	//operation::getInstance()->writelog("Entry Time: " + entryTime.DateTimeString(), "DB");
	//operation::getInstance()->writelog("Pay Time: " + payDT.DateTimeString(), "DB");
	
	timediff = calTime.diffmin(entryTime.GetUnixTimestamp(), payDT.GetUnixTimestamp());

	if(timediff<0) return(0);

	long a,b;

	a=entryTime.Second();
	b=payDT.Second();
	
	if (a<b)
	{
		payDT.SetTime(payDT.GetUnixTimestamp()+60);
		//operation::getInstance()->writelog("add one minutes to PayTime: " + payDT.DateTimeStringNoS(), "DB");

	}
	currentTime.SetTime(payDT.GetUnixTimestamp());

	PD.SetTime(entryTime.Year(),entryTime.Month(),entryTime.Day(),0,0,0);
	pt.SetTime(entryTime.GetUnixTimestamp());
	
	operation::getInstance()->writelog("Cal fee time: " + pt.DateTimeStringNoS() + " ~ " + currentTime.DateTimeStringNoS() , "DB");
	
	while(1)
	{
		timediff=calTime.diffday(PD.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
		if(timediff<0)
		break;
		if(bGotDayInfo==false)
		{
			dayFee=0;
			Lpd.SetTime(PD.GetUnixTimestamp()-86400);
			iDayType=GetDayType(Lpd);
			//operation:: getInstance()->writelog("last day Type:" + to_string(iDayType), "DB");
			if(gtariff[iTransType][iDayType].start_time[0].empty())
			{
				//operation::getInstance()->writelog("No Tariff defined for DayType: "+to_string(iDayType), "DB");
				return(-2); //No Tariff defined for DayType
			}
			maxZone=0;
			for(int k=1;k<10;k++){

				if(gtariff[iTransType][iDayType].end_time[k-1].compare(gtariff[iTransType][iDayType].start_time[0])==0)
				{
					maxZone=k;
					//operation::getInstance()->writelog("Max zone for last day is: " + to_string(maxZone), "DB");
					break;
				}
			};
			
			if(maxZone==0) return(-4); //time zone wrong
			// put last zone of last day in array(0)
			rateType[0]=std::stoi(gtariff[iTransType][iDayType].rate_type[maxZone-1]);
			chargeRate[0]=std::stod(gtariff[iTransType][iDayType].charge_rate[maxZone-1]);
			CTB[0]=std::stoi(gtariff[iTransType][iDayType].charge_time_block[maxZone-1]);
			zoneMin[0]=std::stod(gtariff[iTransType][iDayType].min_charge[maxZone-1]);
			zoneMax[0]=std::stod(gtariff[iTransType][iDayType].max_charge[maxZone-1]);
			GT[0]=std::stoi(gtariff[iTransType][iDayType].grace_time[maxZone-1]);
			firstAdd[0]=std::stod(gtariff[iTransType][iDayType].first_add[maxZone-1]);
			firstFree[0]=std::stoi(gtariff[iTransType][iDayType].first_free[maxZone-1]);
			secondAdd[0]=std::stod(gtariff[iTransType][iDayType].second_add[maxZone-1]);
			secondFree[0]=std::stoi(gtariff[iTransType][iDayType].second_free[maxZone-1]);
			thirdAdd[0]=std::stod(gtariff[iTransType][iDayType].third_add[maxZone-1]);
			thirdFree[0]=std::stoi(gtariff[iTransType][iDayType].third_free[maxZone-1]);
			iAllowance[0]=std::stoi(gtariff[iTransType][iDayType].allowance[maxZone-1]);
			zt.SetTime(gtariff[iTransType][iDayType].start_time[maxZone-1]);
			
			dtStr=Lpd.DateString()+" "+ zt.TimeString();

			zoneTime[0].SetTime(dtStr);
			//operation::getInstance()->writelog ("lastest Zone time for last day start:" + zoneTime[0].DateTimeString(), "DB");
			//get day of PD
			iDayType=GetDayType(PD);
			//operation::getInstance()->writelog("Current day Type: "+ std::to_string(iDayType), "DB");
			if(gtariff[iTransType][iDayType].start_time[0].empty())
			{
				operation::getInstance()->writelog ("No Tariff defined for Daytype: "+ to_string(iDayType), "DB");
				return(-2); //No Tariff defined for DayType
			}

			maxZone=0;
			for(int k=1;k<10;k++){
				if(gtariff[iTransType][iDayType].end_time[k-1].compare(gtariff[iTransType][iDayType].start_time[0])==0)
				{
					maxZone=k;
					//operation::getInstance()->writelog ("max zone for current day is: "+ std:: to_string(maxZone),"DB");
					break;
				}
			}
			//---------------------
			if(maxZone==0) return(-4); //time zone wrong
			for(int k=1;k<=maxZone;k++){
				rateType[k]=std::stoi(gtariff[iTransType][iDayType].rate_type[k-1]);
				chargeRate[k]=std::stod(gtariff[iTransType][iDayType].charge_rate[k-1]);
				CTB[k]=std::stoi(gtariff[iTransType][iDayType].charge_time_block[k-1]);
				zoneMin[k]=std::stod(gtariff[iTransType][iDayType].min_charge[k-1]);
				zoneMax[k]=std::stod(gtariff[iTransType][iDayType].max_charge[k-1]);
				GT[k]=std::stoi(gtariff[iTransType][iDayType].grace_time[k-1]);
				firstAdd[k]=std::stod(gtariff[iTransType][iDayType].first_add[k-1]);
				firstFree[k]=std::stoi(gtariff[iTransType][iDayType].first_free[k-1]);
				secondAdd[k]=std::stod(gtariff[iTransType][iDayType].second_add[k-1]);
				secondFree[k]=std::stoi(gtariff[iTransType][iDayType].second_free[k-1]);
				thirdAdd[k]=std::stod(gtariff[iTransType][iDayType].third_add[k-1]);
				thirdFree[k]=std::stoi(gtariff[iTransType][iDayType].third_free[k-1]);
				iAllowance[k]=std::stoi(gtariff[iTransType][iDayType].allowance[k-1]);
				
				//operation::getInstance()->writelog("Zone time from db is: " + gtariff[iTransType][iDayType].start_time[k-1], "DB");
				zt.SetTime(gtariff[iTransType][iDayType].start_time[k-1]);
				//operation::getInstance()->writelog("Zone time convert is: " + zt.DateTimeString(), "DB");
				dtStr=PD.DateString()+" "+zt.TimeString();
				
				//operation::getInstance()->writelog("Zone time plus pd date is: " + dtStr, "DB");
				zoneTime[k].SetTime(dtStr);
				//operation::getInstance()->writelog("time zone" + std::to_string(k) + " start: " + zoneTime[k].DateTimeString(), "DB");
			}
			dayMin = std::stod(gtariff[iTransType][iDayType].whole_day_min);
			dayMax = std::stod(gtariff[iTransType][iDayType].whole_day_max);
			iZoneCutoff = std::stoi(gtariff[iTransType][iDayType].zone_cutoff);
			iDayCutoff = std::stoi(gtariff[iTransType][iDayType].day_cutoff);

			//operation::getInstance()->writelog("daymin =" + Common::getInstance()->SetFeeFormat(dayMin), "DB");
			//operation::getInstance()->writelog("dayMax = "+ Common::getInstance()->SetFeeFormat(dayMax), "DB");
			//get firstzone for next day
			Npd.SetTime(PD.GetUnixTimestamp()+86400);      // add one day
			iDayType=GetDayType(Npd);
			//operation::getInstance()->writelog("Next day type: "+ to_string(iDayType), "DB");
			if(gtariff[iTransType][iDayType].start_time[0].empty())
			{
				operation::getInstance()->writelog ("No tariff defined for DayType: " + to_string(iDayType), "DB");
				return(-2); //No Tariff defined for DayType
			}
			iNextGT = stoi(gtariff[iTransType][iDayType].grace_time[0]);
			iNextRT = stoi(gtariff[iTransType][iDayType].rate_type[0]);
			zt.SetTime(gtariff[iTransType][iDayType].start_time[0]);
			if(iDayCutoff==1)
				dtStr=Npd.DateString()+" 00:00:00";
			else
				dtStr=Npd.DateString()+" "+zt.TimeString();
			//operation::getInstance()->writelog("zone time is:" + dtStr,"DB");
			zoneTime[maxZone+1].SetTime(dtStr);
			//operation::getInstance()->writelog("Next day zone time start: " + zoneTime[maxZone+1].DateTimeString(), "DB");
			GT[maxZone+1]=iNextGT;
			rateType[maxZone+1]=iNextRT;
			bGotDayInfo = true;
			
			if((dayMin==dayMax)&&(dayMin>0))
			{
				//operation::getInstance()->writelog ("In 24hrmode","DB");
				b24HourBlock= true;
				s24HourFee=dayMin;
				dayMin=0;
				dayMax=0;
			}
			else
			{
			//	operation::getInstance()->writelog ("Out 24hrmode","DB");
				b24HourBlock= false;
				i24HourBlocks=0;
			}
			
		}       // end bGotDayInfo==false
		//if defined 24 hour block, and blocks>0
		if(b24HourBlock == true)
		{
			timediff=calTime.diffhour(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
			i24HourBlocks=timediff/24;
		}
		else
			i24HourBlocks=0;
		
		if(i24HourBlocks>0)
		{
			s24HourCharges=s24HourCharges+s24HourFee;
			pt.SetTime(pt.GetUnixTimestamp()+86400);
			currentRateType=0;
			bNoGT= true;
			//operation::getInstance()->writelog("24hr charge: " + s24HourCharges, "DB");
		}
		else
		{
			for(int k=1;k<=maxZone+1;k++)
			{
				timediff=calTime.diffmin(pt.GetUnixTimestamp(), zoneTime[k].GetUnixTimestamp());
				
				if(timediff>0)
				{
					currentZone=k-1;
					//operation::getInstance()->writelog("Enter to zone" + std::to_string(k-1) , "DB");
					break;
				}
			};
			//operation::getInstance()->writelog("current zone is: " + std::to_string(currentZone), "DB");
			
			currentRateType = rateType[currentZone];
			currentRate = chargeRate[currentZone];
			currentCTB = CTB[currentZone];
			currentMin = zoneMin[currentZone];
			currentMax = zoneMax[currentZone];
			currentGT = GT[currentZone];
			currentAdd = firstAdd[currentZone];
			currentFree = firstFree[currentZone];

			currentAdd2 = secondAdd[currentZone];
			currentFree2 = secondFree[currentZone];
			currentAdd3 = thirdAdd[currentZone];
			currentFree3 = thirdFree[currentZone];

			currentAllowance = iAllowance[currentZone];
			
			if((currentRateType<1)||(currentRateType>2))
			return(-3);
			//operation::getInstance()->writelog("currentRateType: " + std::to_string(currentRateType), "DB");
			//operation::getInstance()->writelog("current Rate: "+ Common::getInstance()->SetFeeFormat(currentRate),"DB");
			//operation::getInstance()->writelog("currentCTB: "+ std::to_string(currentCTB),"DB");
			//operation::getInstance()->writelog("currentFree: " + Common::getInstance()->SetFeeFormat(currentFree),"DB");
			//operation::getInstance()->writelog("currentAdd: " + Common::getInstance()->SetFeeFormat(currentAdd),"DB");
			//operation::getInstance()->writelog("currentFree2: " + Common::getInstance()->SetFeeFormat(currentFree2),"DB");
			//operation::getInstance()->writelog("currentAdd2: " + Common::getInstance()->SetFeeFormat(currentAdd2),"DB");
			//operation::getInstance()->writelog("currentFree3: " + Common::getInstance()->SetFeeFormat(currentFree3),"DB");
			//operation::getInstance()->writelog("currentAdd3: " + Common::getInstance()->SetFeeFormat(currentAdd3),"DB");
			//operation::getInstance()->writelog("currentGT: "+ std::to_string(currentGT),"DB");
			//operation::getInstance()->writelog("currentMax: "+ Common::getInstance()->SetFeeFormat(currentMax),"DB");
			//operation::getInstance()->writelog("currentMin: "+ Common::getInstance()->SetFeeFormat(currentMin),"DB");
			//operation::getInstance()->writelog("currentAllowance: "+ Common::getInstance()->SetFeeFormat(currentAllowance),"DB");
		}
		
		// check grace time
		
		if(bNoGT==false)
		{
			if(currentGT>0)
			{
				//operation::getInstance()->writelog("fee start time: " + pt.DateTimeString(),"DB");
				//operation::getInstance()->writelog("cal fee time: " + currentTime.DateTimeString(),"DB");
				//---------
				timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
				//operation::getInstance()->writelog("parked time = " + std::to_string(timediff),"DB");
				if(timediff>currentGT)
				{
				//	operation::getInstance()->writelog("No grace time","DB");
					bNoGT=true;
				}
				else
				{
					//operation::getInstance()->writelog("within grace period","DB");
					return(0);
				}
			}
			else
					bNoGT=true;
		}
		
		// push time, get zone fee
		zoneFee =0;

		if(currentRateType==1)         // hourly charge
		{
			if (currentCTB==0)
				return(-3);
			if(currentAllowance>0)
			{
				timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
				//operation::getInstance()->writelog("time diff for allowance is "+ timediff,"DB");
				if(((charge>0)||(dayFee>0))&&(timediff<=currentAllowance))
				{
					//operation::getInstance()->writelog("within allowance, no change","DB");
					break;
				}
			};
			
			if(currentFree>0)
			{
				if(((operation::getInstance()->tParas.giFirstHourMode==0)||((dayFee==0)&&(charge==0)))&& (bFirstFreed==false))
				{
					//operation::getInstance()->writelog("gifirsthour: "+ std::to_string(operation::getInstance()->tParas.giFirstHour),"DB");

					if(operation::getInstance()->tParas.giFirstHour>0)
					{
						timediff=calTime.diffmin(pt.GetUnixTimestamp(), zoneTime[currentZone+1].GetUnixTimestamp());
						if(timediff<=currentFree)
						{
							if(iZoneCutoff==1)
								pt.SetTime(zoneTime[currentZone+1].DateTimeString());
							else
								pt.SetTime(pt.GetUnixTimestamp()+(currentFree*60));

							//operation::getInstance()->writelog("New pt time in first mode: "+ pt.DateTimeString(),"DB");
							zoneFee = currentAdd;
							//--- add for change per mins charge
							timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
							if(timediff<=0)
							{
								dayFee=dayFee + zoneFee;
								break;
							}
							else
							{
								if((operation::getInstance()->tParas.giFirstHour>1)&&(iZoneCutoff==0))
								{
									
									timediff=calTime.diffmin(pt.GetUnixTimestamp(), zoneTime[currentZone+1].GetUnixTimestamp());
									if((timediff<=0)&&(rateType[currentZone+1]==2))
									{
										zoneFee= currentAdd;
										goto SettleFirstFree1;
									}
									pt.SetTime(pt.GetUnixTimestamp()+(currentFree2*60));
									zoneFee = currentAdd + currentAdd2;
									timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
									if(timediff<=0)
									{
										dayFee=dayFee + zoneFee;
										break;
									}
									else
									{
										if(operation::getInstance()->tParas.giFirstHour>2)
										{
											timediff=calTime.diffmin(pt.GetUnixTimestamp(), zoneTime[currentZone+1].GetUnixTimestamp());
											if((timediff<=0)&& (rateType[currentZone+1]==2))
											{
												zoneFee = currentAdd + currentAdd2;
												goto SettleFirstFree1;
											}
											pt.SetTime(pt.GetUnixTimestamp()+(currentFree3*60));
											zoneFee = currentAdd + currentAdd2 + currentAdd3;
											timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
											if(timediff<=0)
											{
												dayFee=dayFee + zoneFee;
												break;
											}
										}
									}
								}

SettleFirstFree1:
								if(operation::getInstance()->tParas.giFirstHourMode==1)
								bFirstFreed=true;
							}
						}
						else
						{
							zoneFee = currentAdd;
							pt.SetTime(pt.GetUnixTimestamp()+(currentFree*60));
							timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
							if(timediff<=0)
							{
								dayFee=dayFee + zoneFee;
								break;
							}
							else
							{
								if(operation::getInstance()->tParas.giFirstHour>1)
								{
									timediff=calTime.diffmin(pt.GetUnixTimestamp(), zoneTime[currentZone+1].GetUnixTimestamp());
									if(timediff<= currentFree2)
									{
										if(iZoneCutoff==1)
										pt.SetTime(zoneTime[currentZone+1].DateTimeString());
										else
										pt.SetTime(pt.GetUnixTimestamp()+(currentFree2*60));
										//operation::getInstance()->writelog( "New pt time in first mode 2 "+pt.DateTimeString(),"DB");
										zoneFee = currentAdd + currentAdd2;
										timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
										if(timediff<=0)
										{
											dayFee=dayFee + zoneFee;
											break;
										}
										else
										{
											if((operation::getInstance()->tParas.giFirstHour>2)&&(iZoneCutoff==0))
											{
												timediff=calTime.diffmin(pt.GetUnixTimestamp(), zoneTime[currentZone+1].GetUnixTimestamp());
												if((timediff<=0)&& (rateType[currentZone+1]==2))
												{
													zoneFee = currentAdd + currentAdd2;
													goto SettleFirstFree2;
												}
												pt.SetTime(pt.GetUnixTimestamp()+(currentFree3*60));
												zoneFee = currentAdd + currentAdd2 + currentAdd3;
												timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
												if(timediff<=0)
												{
													dayFee=dayFee + zoneFee;
													break;
												}
											}
SettleFirstFree2:
											if(operation::getInstance()->tParas.giFirstHourMode==1)
											bFirstFreed=true;
										}
									}
									else
									{
										zoneFee = currentAdd + currentAdd2;
										pt.SetTime(pt.GetUnixTimestamp()+(currentFree2*60));
										//operation::getInstance()->writelog( "2nd New pt time"+pt.DateTimeString(),"DB");
										timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
										if(timediff<=0)
										{
											dayFee=dayFee + zoneFee;
											break;
										}
										else
										{
											if(operation::getInstance()->tParas.giFirstHour>2)
											{
												timediff=calTime.diffmin(pt.GetUnixTimestamp(), zoneTime[currentZone+1].GetUnixTimestamp());
												if(timediff<= currentFree3)
												{
													if(iZoneCutoff==1)
													pt.SetTime(zoneTime[currentZone+1].DateTimeString());
													else
													pt.SetTime(pt.GetUnixTimestamp()+(currentFree3*60));
													//operation::getInstance()->writelog( "New pt time in first mode 3 "+pt.DateTimeString(),"DB");
													zoneFee = currentAdd + currentAdd2 + currentAdd3;
													timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
													if(timediff<=0)
													{
														dayFee=dayFee + zoneFee;
														break;
													}
													else
													{
														if(operation::getInstance()->tParas.giFirstHourMode==1)
														bFirstFreed=true;
													}
												}
												else
												{
													
													zoneFee = currentAdd + currentAdd2 + currentAdd3;
													pt.SetTime(pt.GetUnixTimestamp()+(currentFree3*60));
													//operation::getInstance()->writelog( "3rd New pt time"+pt.DateTimeString(),"DB");
													timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
													if(timediff<=0)
													{
														dayFee=dayFee + zoneFee;
														break;
													}
													else
													{
														if(operation::getInstance()->tParas.giFirstHourMode==1)
														bFirstFreed = true;
													}
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
			//operation::getInstance()->writelog("Zone Fee after first hour mode is "+Common::getInstance()->SetFeeFormat(zoneFee),"DB");
			//  one time zone  while 
			while(1)
			{
				timediff=calTime.diffmin(pt.GetUnixTimestamp(), zoneTime[currentZone+1].GetUnixTimestamp());
				
				if(timediff<=0) break;
				
				if(currentAllowance>0)
				{
					timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
					if(((charge>0)||(dayFee>0)||(zoneFee>0))&&(timediff<=currentAllowance))
					{
						//operation::getInstance()->writelog("within allowance, no change","DB");
					}
					else
					zoneFee = zoneFee + currentRate;
				}
				else
				zoneFee = zoneFee + currentRate;
				
				if((currentMax>0)&&(zoneFee>=currentMax))
				{
					pt.SetTime(zoneTime[currentZone+1].DateTimeString());
					break;
				}
				pt.SetTime(pt.GetUnixTimestamp()+(currentCTB*60));
				timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
				if(timediff<=0) break;
			}   // end while for one time zone
			
			if(iZoneCutoff==1)
			{
				timediff=calTime.diffmin(pt.GetUnixTimestamp(), zoneTime[currentZone+1].GetUnixTimestamp());
				if(timediff<0)
				{
					if((operation::getInstance()->tParas.giHr2PEAllowance>0)&& (rateType[currentZone+1]==2))
					{
						timediff=calTime.diffmin(entryTime.GetUnixTimestamp(), payDT.GetUnixTimestamp());
						if(timediff>operation::getInstance()->tParas.giHr2PEAllowance)
						{
							pt.SetTime(zoneTime[currentZone+1].DateTimeString());
						}
					}
					else
					{
						pt.SetTime(zoneTime[currentZone+1].DateTimeString());
					}
				}
			};
			//operation::getInstance()->writelog("zone Fee is: "+ Common::getInstance()->SetFeeFormat(zoneFee),"DB");
			
		};	
		
		if(currentRateType==2)      //per entry charge
		{
			//operation::getInstance()->writelog("zone Fee before enter currentRateType is: "+ Common::getInstance()->SetFeeFormat(zoneFee),"DB");
			if((operation::getInstance()->tParas.giMCyclePerDay>0)&&(iTransType== 2))
			{
				if((bMCPerDayChecked== false)&&(currentRate>0))
				{
					haspaid= HasPaidWithinPeriod(zoneTime[currentZone].DateTimeString(),zoneTime[currentZone+1].DateTimeString());
					if(haspaid>0)
					{
						currentRate=0;
					}
				}
				bMCPerDayChecked=true;
			}
			if(currentAllowance>0)
			{
				timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
				
				if(((charge>0)||(dayFee>0))&&(timediff<=currentAllowance))
				{
					operation::getInstance()->writelog("within allowance, no change","DB");
				}
				else
				zoneFee = zoneFee + currentRate;
			}
			else
			zoneFee = zoneFee + currentRate;
			
			//operation::getInstance()->writelog("PEallowance = "+ std::to_string(operation::getInstance()->tParas.giPEAllowance),"DB");
			
			if(operation::getInstance()->tParas.giPEAllowance>0)
			{
				//operation::getInstance()->writelog("pt before gi Allowance is : "+pt.DateTimeString(),"DB");
				timediff=calTime.diffmin(pt.GetUnixTimestamp(), zoneTime[currentZone+1].GetUnixTimestamp());
				//operation::getInstance()->writelog("time diff in gi is "+std::to_string(timediff),"DB");
				if(timediff<operation::getInstance()->tParas.giPEAllowance)
				{
					pt.SetTime(pt.GetUnixTimestamp()+(operation::getInstance()->tParas.giPEAllowance*60));
				//	operation::getInstance()->writelog("pt in gi Allowance 1 is : "+pt.DateTimeString(),"DB");
				}
				else
				{
					pt.SetTime(zoneTime[currentZone+1].DateTimeString());
				//	operation::getInstance()->writelog("pt in gi Allowance 2 is : "+pt.DateTimeString(),"DB");
				}
			//operation::getInstance()->writelog("pt in gi Allowance is : "+pt.DateTimeString(),"DB");
			}
			else
			pt.SetTime(zoneTime[currentZone+1].DateTimeString());
		//	operation::getInstance()->writelog("pre entry zone Fee is: "+ Common::getInstance()->SetFeeFormat(zoneFee),"DB");
		};
		//operation::getInstance()->writelog("day Fee before current zone is: "+ Common::getInstance()->SetFeeFormat(dayFee),"DB");
		//operation::getInstance()->writelog("zone Fee before current zone is: "+Common::getInstance()->SetFeeFormat(zoneFee),"DB");
		//operation::getInstance()->writelog("currentMin Fee is: "+ Common::getInstance()->SetFeeFormat(currentMin),"DB");
		//operation::getInstance()->writelog("currentMax Fee is: "+ Common::getInstance()->SetFeeFormat(currentMax),"DB");
		if((currentMin>0)&&(zoneFee<currentMin))
		zoneFee=currentMin;
		if((currentMax>0)&&(zoneFee>currentMax))
		zoneFee=currentMax;
		dayFee = dayFee + zoneFee;
		//operation::getInstance()->writelog("PD = "+PD.DateTimeString(),"DB");
		//operation::getInstance()->writelog("pt = "+pt.DateTimeString(),"DB");
		//operation::getInstance()->writelog("zone Fee is: "+ Common::getInstance()->SetFeeFormat(zoneFee),"DB");
		//operation::getInstance()->writelog("day Fee is: "+ Common::getInstance()->SetFeeFormat(dayFee),"DB");
		//operation::getInstance()->writelog(std::to_string(PD.GetUnixTimestamp()),"DB");
		//operation::getInstance()->writelog(std::to_string(pt.GetUnixTimestamp()),"DB");
		timediff=calTime.diffday(PD.GetUnixTimestamp(),pt.GetUnixTimestamp()); 
		//operation::getInstance()->writelog("diff day is: "+ std::to_string(timediff),"DB");
		
		if(timediff>0)
		{
		//	operation::getInstance()->writelog("day Feeq is: " + Common::getInstance()->SetFeeFormat(dayFee),"DB");
		//	operation::getInstance()->writelog("charge Feeq is: " + Common::getInstance()->SetFeeFormat(charge),"DB");
			if((dayMin>0)&&(dayFee<dayMin))
			dayFee=dayMin;
			if((dayMax>0)&&(dayFee>dayMax))
			dayFee=dayMax;
			charge=charge+dayFee;
			dayFee=0;
			bGotDayInfo= false;
			PD.SetTime(PD.GetUnixTimestamp()+86400);
			//operation::getInstance()->writelog("next day PD is:"+PD.DateTimeString(),"DB");
			//operation::getInstance()->writelog("day Feeb is: "+ Common::getInstance()->SetFeeFormat(dayFee),"DB");
			//operation::getInstance()->writelog("charge Feeb is: "+ Common::getInstance()->SetFeeFormat(charge),"DB");
		}
		timediff=calTime.diffmin(pt.GetUnixTimestamp(),currentTime.GetUnixTimestamp());
		if(timediff<=0) break;
		//operation::getInstance()->writelog("loop to start calcuation fee again","DB");
	}        //end while(1) 
	
	if(dayFee>0) // day fee have not add into charge
	{
		//operation::getInstance()->writelog("day Fee0 is: " + Common::getInstance()->SetFeeFormat(dayFee),"DB");
		//operation::getInstance()->writelog("charge Fee0 is: " + Common::getInstance()->SetFeeFormat(charge),"DB");
		if((dayMin>0)&&(dayFee<dayMin))
		dayFee=dayMin;
		if((dayMax>0)&&(dayFee>dayMax))
		dayFee=dayMax;
		charge=charge+dayFee;
		//operation::getInstance()->writelog("day Fee1 is: " + Common::getInstance()->SetFeeFormat(dayFee),"DB");
		//operation::getInstance()->writelog("charge Fee1 is: " + Common::getInstance()->SetFeeFormat(charge),"DB");
	}
	//operation::getInstance()->writelog("After loop 24hr block ="+ std::to_string(b24HourBlock),"DB");
	
	if(b24HourBlock==true)
	{
	//	operation::getInstance()->writelog("b 24hr block ","DB");
	//	operation::getInstance()->writelog("b 24hr charge: "+ Common::getInstance()->SetFeeFormat(s24HourCharges),"DB");
		if(charge> s24HourFee) charge=s24HourFee;
	//	operation::getInstance()->writelog("b charge: "+ Common::getInstance()->SetFeeFormat(charge),"DB");
		charge= s24HourCharges+charge;
	}
	
	if(operation::getInstance()->tParas.giTariffFeeMode>0)
	{
		iRet=RoundIt(charge, operation::getInstance()->tParas.giTariffFeeMode)/100;
	}
	else iRet=charge;
	
	//operation::getInstance()->writelog("Total fee is: " + Common::getInstance()->SetFeeFormat(iRet),"DB");
	//operation::getInstance()->writelog("pt time is: "+pt.DateTimeString(),"DB");
	
	return(iRet);
	

}

DBError db::LoadTariffTypeInfo()
{

	std::string sqlStmt;
	std::string tbName="tariff_type_info";

	vector<ReaderItem> selResult;

	std::string sValue;
	int r,j;
	int w=-1;
	int bTried=0;

	try
	{

		operation::getInstance()->writelog ("Load Tariff Type Info: Started", "DB");

		Try_Again:
		sqlStmt="Select tariff_type, start_time, end_time ";
		sqlStmt= sqlStmt +  " FROM " + tbName + " Order by start_time ASC";

		r=localdb->SQLSelect(sqlStmt,&selResult,true);
		if (r!=0) return iLocalFail;

		if (selResult.size()>0){
		
			for(j=0;j<selResult.size();j++){
				
				gtarifftypeinfo[j].tariff_type = selResult[j].GetDataItem(0);
				gtarifftypeinfo[j].start_time = selResult[j].GetDataItem(1);
				gtarifftypeinfo[j].end_time = selResult[j].GetDataItem(2);

			}

			operation::getInstance()->writelog("Load Tariff Type Info: success", "DB");

			return iDBSuccess;
		}
		else{
			if(bTried==0)
			{
				downloadtarifftypeinfo();
				bTried=1;
				goto Try_Again;
				
			}

			return iNoData;
			operation::getInstance()->writelog("Load Tariff Type Info error: no data in local DB", "DB");

		}

	}
	catch(const std::exception &e)
	{
		operation::getInstance()->writelog("Load Tariff Type Info error: local DB error", "DB");
		return iLocalFail;
	}

}


string db::CalParkedTime(long lpt)
{
	long pt;
	int dt,hr,mt,rem1,rem2;
	//string hr,mt;
	string ret;
	
	if(lpt<0) ret="N/A";
	else
	{
		pt=lpt;
		dt=lpt/1440;
		rem1=lpt%1440;
		rem2=rem1%60;
		hr=rem1/60;
		mt=rem2;
		if(dt>0)
		{
			if(hr>10)
			{
				if(mt>10)
				ret= to_string(dt)+"D "+ to_string(hr)+":"+ to_string(mt);
				else
				ret= to_string(dt)+"D "+ to_string(hr)+":0"+ to_string(mt);
			}
			else
			{
				if(mt>10)
				ret= to_string(dt)+"D 0"+ to_string(hr)+":"+ to_string(mt);
				else
				ret= to_string(dt)+"D 0"+ to_string(hr)+":0"+ to_string(mt);
			}
		}
		else
		{
			if(hr>10)
			{
				if(mt>10)
				ret= to_string(hr)+":"+ to_string(mt);
				else
				ret= to_string(hr)+":0"+ to_string(mt);
			}
			else
			{
				if(mt>10)
				ret= to_string(hr)+":"+ to_string(mt);
				else
				ret= to_string(hr)+":0"+ to_string(mt);
			}
		}
	}
	return(ret);
}

DBError db::LoadXTariff()
{

	std::string sqlStmt;
	std::string tbName="X_Tariff";
	struct  XTariff_Struct xtariff;

	vector<ReaderItem> selResult;

	std::string sValue;
	int r,i,j;
	int w=-1;
	int bTried=0;

	try
	{
		operation::getInstance()->writelog ("Load XTariff: Started", "DB");

		Try_Again:
		sqlStmt="Select * ";
		sqlStmt= sqlStmt +  " FROM " + tbName;

		r=localdb->SQLSelect(sqlStmt,&selResult,true);
		if (r!=0) return iLocalFail;

		if (selResult.size()>0){
			msxtariff.clear();
			for(j=0;j<selResult.size();j++){
				xtariff.day_index = selResult[j].GetDataItem(0);
            	xtariff.autocharge[0] = selResult[j].GetDataItem(1);
				//operation::getInstance()->writelog("autocharger0: "+ xtariff.autocharge[0], "DB");
				xtariff.fee[0] = selResult[j].GetDataItem(2);
				for (i=1; i<5; i++) {
					xtariff.time[i] = selResult[j].GetDataItem(3+(i-1)*3);
					if (xtariff.time[i] == "") {xtariff.time[i] = "23:59";}
					xtariff.autocharge[i] = selResult[j].GetDataItem(4+(i-1) *3);
					xtariff.fee[i] = selResult[j].GetDataItem(5+(i-1)*3);
				}
				msxtariff.push_back(xtariff);
			}
			operation::getInstance()->writelog("Load XTariff: success", "DB");

			return iDBSuccess;
		}
		else{
			if(bTried==0)
			{
				downloadxtariff(operation::getInstance()->tParas.giGroupID,operation::getInstance()->tParas.giSite, 0);
				bTried=1;
				goto Try_Again;
				
			}

			return iNoData;
			operation::getInstance()->writelog("Load xtariff error: no data in local DB", "DB");

		}

	}
	catch(const std::exception &e)
	{
		operation::getInstance()->writelog("Load xtariff error: local DB error", "DB");
		return iLocalFail;
	}

}

int db::GetXTariff(int &iAutoDebit, float &sAmt, int iVType)
{
	int iDayIdx;
  	string sDayIdx,sDayIndex;
	CE_Time pDt;
	int i,j;
	bool gbfound = false;
	//------
	pDt.SetTime();
	//operation::getInstance()->writelog("PD is:"+pDt.DateTimeString(),"DB");
  	iDayIdx = GetDayType(pDt);
    iDayIdx = iDayIdx + iVType * 3;
    sDayIdx = "," + std::to_string(iDayIdx) + ",";
	//------
	for(i=0; i<msxtariff.size();i++){
		sDayIndex= ","+ msxtariff[i].day_index + ",";	
		if (sDayIndex.find(sDayIdx) != std::string::npos) {
			gbfound = true;
			break;
		}
	}
	if (gbfound == true) {
		//check time now
		//operation::getInstance()->writelog("Day Index Record: "+ std::to_string(i), "DB");
		//operation::getInstance()->writelog("Current Time: "+ pDt.HMTimeString(), "DB");
		for(j=0; j< 5; j++) {
        	if (pDt.HMTimeString() <= msxtariff[i].time[j + 1]) {
            	iAutoDebit = std:: stoi(msxtariff[i].autocharge[j]);
           	 	sAmt = std::stod(msxtariff[i].fee[j]);
				//operation::getInstance()->writelog("Autocharge: "+ std::to_string(iAutoDebit), "DB");
				//operation::getInstance()->writelog("chargeAmt: "+ std::to_string(sAmt), "DB");
				return(1);
			}
        }
	}
   	return(0);
}


int db::FetchEntryinfo(string sIUNo)
{
	// Return: -1=cannot connect to db
    // 0=Ok, found, 1=paid, 2=lost card,
    // 3=no entry, 4=no sale
    // 5=complimentary

	std::string sqlStmt;
	vector<ReaderItem> selResult;
	vector<ReaderItem> selResult2;
	std::string sValue;
	int r,i,j;
	int w=-1;
	int bTried=0;
	string gsZoneEntries = operation::getInstance()->tParas.gsZoneEntries;

	sqlStmt = "SELECT Entry_time, trans_type,parking_fee,paid_amt, owe_amt, entry_station FROM Movement_trans_tmp where (iu_tk_no = '"+ sIUNo + "'";
	if (sIUNo.length() == 16) {
		sqlStmt = sqlStmt + "or card_mc_no = '" + sIUNo + "')";
	}else
	{
		sqlStmt = sqlStmt + "or entry_lpn = '" + sIUNo + "')";
	}
	sqlStmt = sqlStmt + " and exit_time is null and charindex(','+cast(entry_station as varchar(2))+',','" + gsZoneEntries + "')>0 ";
	sqlStmt = sqlStmt + "order by entry_time desc ";

	r = centraldb->SQLSelect(sqlStmt, &selResult, true);
	if (r != 0)
	{
		m_remote_db_err_flag = 1;
		goto processLocal;
	}
	else
	{
		m_remote_db_err_flag = 0;
	}

	if (selResult.size()>0)
	{
		operation::getInstance()->tExit.sEntryTime=selResult[0].GetDataItem(0);
		operation::getInstance()->tExit.iTransType=std::stoi(selResult[0].GetDataItem(1));
		operation::getInstance()->tExit.sOweAmt=std::stof(selResult[0].GetDataItem(4));
		operation::getInstance()->tExit.iEntryID=std::stoi(selResult[0].GetDataItem(5));	
		operation::getInstance()->writelog("Fetch Entry time from central:  " + operation::getInstance()->tExit.sEntryTime, "DB");
		return r;
	}
	else{
		//no record
		operation::getInstance()->writelog("No Entry record in Central DB.", "DB");
		return 3;
	}
	
processLocal:
	sqlStmt = "Select Entry_time,trans_type,paid_amt, Owe_Amt, Station_id From Entry_Trans where Status = 0 and iu_tk_no = '"+ sIUNo +"' order by entry_time desc";
	
	r=localdb->SQLSelect(sqlStmt,&selResult2,true);

	//operation::getInstance()->writelog(sqlStmt, "DB");
	if (r!=0)
	{
		operation::getInstance()->writelog("Fetch local entry time failed.", "DB");
		return r;
	}
	
	if (selResult2.size()>0)
	{            
		operation::getInstance()->tExit.sEntryTime=selResult2[0].GetDataItem(0);
		operation::getInstance()->tExit.iTransType=std::stoi(selResult2[0].GetDataItem(1));
		operation::getInstance()->tExit.sOweAmt=std::stof(selResult2[0].GetDataItem(3));
		operation::getInstance()->tExit.iEntryID=std::stoi(selResult2[0].GetDataItem(4));	
	}
	else
	{
		operation::getInstance()->writelog("No entry record in local DB","DB");
		return 3;
	}
	operation::getInstance()->writelog("Fetch Entry time from Local:  " + operation::getInstance()->tExit.sEntryTime, "DB");
	return r;
}

int db::CheckCardOK(string sCardNo)
{
	//Return: 0=Normal, nothing, 
	//		1=Blacklist, -1=db error
    //      2=complimentary
	//		3=master card
	//		4=validation complimentary 
	//		5=master season
	//		6=vvip	

	int r;
	std::string sqlStmt;
	vector<ReaderItem> tResult;

	string gsZoneID = std::to_string(operation::getInstance()->gtStation.iZoneID);

	sqlStmt = "SELECT date_from, date_to FROM Season_mst where season_No = '" + sCardNo + "' and (zone_id='0' or charindex(',' + cast(" + gsZoneID + " as varchar(2)) + ',', ',' + zone_id + ',') >0 ) and s_status=1 and (season_type=1 or season_type = 9)" ;
	//------
	//operation::getInstance()->writelog(sqlStmt, "DB");
	//----
	r = centraldb->SQLSelect(sqlStmt, &tResult, true);
	//------
	if (r != 0) return 0;

	if (tResult.size()>0)
	{
		
		if (Common::getInstance()->FnGetDateDiffInSeconds(tResult[0].GetDataItem(0)) > 0 && Common::getInstance()->FnGetDateDiffInSeconds(tResult[0].GetDataItem(1)) < 0)
		{
			operation::getInstance()->writelog("Master Card!", "DB");
			return  5;
		}	
	}
	//---------
	sqlStmt = "select Complimentary_no from Complimentary where Complimentary_no='" + sCardNo + "'and exit_time is null";
	
	r = centraldb->SQLSelect(sqlStmt, &tResult, true);

	if (r != 0)  return 0;

	if (tResult.size()>0){
		operation::getInstance()->writelog("Complimentary Card!", "DB");
		return 2;
	}
		
	return 0;	
}

DBError db::updatemovementtrans(tExitTrans_Struct& tExit) 
{
	int r;
	string sqstr="";
	string sLPRNo = "";
	string gsZoneEntries = operation::getInstance()->tParas.gsZoneEntries;
	string gsUpdateTime = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
	//---------
	tExit.sFee = operation::getInstance()->GfeeFormat(tExit.sFee);
	tExit.sPaidAmt = operation::getInstance()->GfeeFormat(tExit.sPaidAmt);
	tExit.sRedeemAmt = operation::getInstance()->GfeeFormat(tExit.sRedeemAmt);
	tExit.sGSTAmt = operation::getInstance()->GfeeFormat(tExit.sGSTAmt);

	if ((tExit.sLPN[0] != "")|| (tExit.sLPN[1] !=""))
	{
		if(tExit.iTransType==7 || tExit.iTransType==8||tExit.iTransType==22)
		{
			sLPRNo = tExit.sLPN[1];

		}
		else
		{
			sLPRNo = tExit.sLPN[0];
		}

	}
	//------
	sqstr="UPDATE movement_trans_tmp set exit_lpn = '"+ sLPRNo + "',";
	sqstr= sqstr + "exit_station = '" + tExit.xsid + "',";
	sqstr= sqstr + " exit_time = '" + tExit.sExitTime + "',";
	sqstr= sqstr + " trans_type = '" + std::to_string(tExit.iTransType) + "',";
	sqstr= sqstr + " card_mc_no = '" + tExit.sCardNo + "',";
	sqstr= sqstr + " parking_fee = '" + std::to_string(tExit.sFee) + "',";
	sqstr= sqstr + " paid_amt = '" + std::to_string(tExit.sPaidAmt) + "',";
	sqstr= sqstr + " Parked_time = '" + std::to_string(tExit.lParkedTime) + "',";
	sqstr= sqstr + " receipt_no = '" + tExit.sReceiptNo + "',";
	sqstr= sqstr + " redeem_amt = '" + std::to_string(tExit.sRedeemAmt) + "',";
	sqstr= sqstr + " redeem_time = '" + std::to_string(tExit.iRedeemTime) + "',";
	sqstr= sqstr + " Card_Type = '" + std::to_string(tExit.iCardType) + "',";
	sqstr= sqstr + " top_up_amt = '" + std::to_string(tExit.sTopupAmt) + "', ";
	sqstr= sqstr + " update_dt = '" + gsUpdateTime + "'";
	sqstr= sqstr + "where iu_tk_no = '" + tExit.sIUNo + "' and entry_time = '"+ tExit.sEntryTime + "' and exit_time is null and charindex(','+cast(entry_station as varchar(2))+',','" + gsZoneEntries + "')>0" ;
	//------
	//operation::getInstance()->writelog(sqstr,"DB");
	//-----
	r = centraldb->SQLExecutNoneQuery(sqstr);

	if (r==0) 
	{
		operation::getInstance()->writelog("Success matching MovementTrans_Tmp","DB");
		return iCentralSuccess;
	}
	else {
		operation::getInstance()->writelog("fail to match Movementtrans_Tmp","DB");
		 m_remote_db_err_flag=1;
	}
	return iCentralFail;
}

DBError db::insert2movementtrans(tExitTrans_Struct& tExit) 
{
	int r;
	string sqstr="";
	string sLPRNo = "";
	//------
	tExit.sFee = operation::getInstance()->GfeeFormat(tExit.sFee);
	tExit.sPaidAmt = operation::getInstance()->GfeeFormat(tExit.sPaidAmt);
	tExit.sRedeemAmt = operation::getInstance()->GfeeFormat(tExit.sRedeemAmt);
	tExit.sGSTAmt = operation::getInstance()->GfeeFormat(tExit.sGSTAmt);
	//-----
	if ((tExit.sLPN[0] != "")|| (tExit.sLPN[1] !=""))
	{
		if(tExit.iTransType==7 || tExit.iTransType==8||tExit.iTransType==22)
		{
			sLPRNo = tExit.sLPN[1];

		}
		else
		{
			sLPRNo = tExit.sLPN[0];
		}

	}
	sqstr = "INSERT INTO movement_trans_tmp (exit_lpn, exit_station, exit_time, trans_type, card_mc_no, iu_tk_no, parking_fee, paid_amt, Parked_time, receipt_no, redeem_amt, redeem_time, Card_Type, top_up_amt";
	if (tExit.sEntryTime == "") sqstr = sqstr + ")";
	else sqstr = sqstr + ", entry_time, entry_station)";
	sqstr = sqstr + " VALUES ('" + sLPRNo + "'," + tExit.xsid + ",'" + tExit.sExitTime + "'," + std::to_string(tExit.iTransType) + ",'" + tExit.sCardNo + "','" + tExit.sIUNo + "'";
	sqstr = sqstr + "," + std::to_string(tExit.sFee) + "," + std::to_string(tExit.sPaidAmt) + "," + std::to_string(tExit.lParkedTime) + ",'" + tExit.sReceiptNo + "'";
	sqstr = sqstr + "," + std::to_string(tExit.sRedeemAmt) + "," + std::to_string(tExit.iRedeemTime) + "," + std::to_string(tExit.iCardType) + "," + std::to_string(tExit.sTopupAmt) ;
	if (tExit.sEntryTime == "") sqstr = sqstr + ")";
	else sqstr = sqstr + ",'" + tExit.sEntryTime + "'," + std::to_string(tExit.iEntryID) + ")";
	//------

	//std::cout << __func__ << " : " << sqstr << std::endl;

	r = centraldb->SQLExecutNoneQuery(sqstr);
	if (r == 0) 
	{
		operation::getInstance()->writelog("Success insert into movement_trans_tmp", "DB");
		return iCentralSuccess;
	}
	else {
		operation::getInstance()->writelog("fail to insert into movement_trans_tmp", "DB");
		m_remote_db_err_flag=1;
	}
	return iCentralFail;
}

int db::isValidBarCodeTicket(bool isRedemptionTicket, std::string sBarcodeTicket, std::tm& dtExpireTime, double& gbRedeemAmt, int& giRedeemTime)
{
	// Valid
	int iRet = 1;
	int r;
	std::string sqlStmt;
	vector<ReaderItem> selResult;

	if (isRedemptionTicket == true)
	{
		sqlStmt = "SELECT Valid_from, Valid_to, redeem_dt, redeem_amt, redeem_time from Redemption WHERE redeem_no='" + sBarcodeTicket + "'";
	}
	else
	{
		sqlStmt = "SELECT Valid_from, Valid_to, exit_time FROM Complimentary WHERE complimentary_no='" + sBarcodeTicket + "'";
	}

	r = centraldb->SQLSelect(sqlStmt, &selResult, true);
	if (r != 0)
	{
		m_remote_db_err_flag = 1;
		// DB Error
		iRet = -1;
		return iRet;
	}
	else
	{
		m_remote_db_err_flag = 0;
	}

	if (selResult.size() > 0)
	{
		try
		{
			if (selResult[0].GetDataItem(2) == "NULL" || selResult[0].GetDataItem(2) == "")
			{
				if (Common::getInstance()->FnGetDateDiffInSeconds(selResult[0].GetDataItem(0)) < 0)
				{
					// Not yet valid
					iRet = 6;
				}
				else
				{
					if (Common::getInstance()->FnGetDateDiffInSeconds(selResult[0].GetDataItem(1)) > 0)
					{
						// Expired
						iRet = 0;
					}
				}
			}
			else
			{
				// Used
				iRet = 2;
			}

			auto tp = Common::getInstance()->FnParseDateTime(selResult[0].GetDataItem(1));
			std::time_t tt = std::chrono::system_clock::to_time_t(tp);
			dtExpireTime = *std::localtime(&tt);
			// Valid
			if ((iRet == 1) && (isRedemptionTicket == true))
			{
				gbRedeemAmt = std::stod(selResult[0].GetDataItem(3));
				giRedeemTime = std::stoi(selResult[0].GetDataItem(4));
			}
		}
		catch (const std::exception& e)
		{
			operation::getInstance()->writelog("Date time format Exception: " + std::string(e.what()), "DB");
		}
	}
	else
	{
		// Not found
		iRet = 4;
		operation::getInstance()->writelog("No barcode ticket found in central DB","DB");
	}

	return iRet;
}

DBError db::update99PaymentTrans()
{
	int r;
	std::string sqlStmt="";

	sqlStmt = "UPDATE Exit_trans_tmp set status=0";

	if (operation::getInstance()->tExit.sRebateAmt > 0)
	{
		sqlStmt = sqlStmt + ",redeem_amt=" + std::to_string(operation::getInstance()->tExit.sRebateAmt);
		sqlStmt = sqlStmt + ",paid_amt=" + std::to_string(operation::getInstance()->tExit.sPaidAmt);
		sqlStmt = sqlStmt + ",gst_amt=" + std::to_string(operation::getInstance()->tExit.sGSTAmt);
		sqlStmt = sqlStmt + ",Trans_Type=" + std::to_string(operation::getInstance()->tExit.iTransType);
		sqlStmt = sqlStmt + ",Card_mc_no='" + operation::getInstance()->tExit.sCardNo + "'";
		sqlStmt = sqlStmt + ",redeem_no='" + operation::getInstance()->tExit.sRedeemNo + "'";
	}

	sqlStmt = sqlStmt + " WHERE iu_tk_no='" + operation::getInstance()->tExit.sIUNo + "' and status = 99 AND Station_ID=" + std::to_string(operation::getInstance()->gtStation.iSID);

	r = centraldb->SQLExecutNoneQuery(sqlStmt);
	if (r == 0)
	{
		operation::getInstance()->writelog("Update 99 Trans to valid for EZpay/VCC","DB");
		return iDBSuccess;
	}
	else
	{
		operation::getInstance()->writelog("Failed to updated 99 Trans for EZPay/VCC","DB");
		m_remote_db_err_flag=1;
	}
	return iCentralFail;
}

DBError db::insertUPTFileSummaryLastSettlement(const std::string& sSettleDate, const std::string& sSettleName, int iSettleType, uint64_t lTotalTrans, double dTotalAmt, int iSendFlag, const std::string& sSendDate)
{
	int r;
	std::string sqlStmt = "";

	sqlStmt = "INSERT INTO UPT_File_Summary (settle_date, settle_file, settle_type, last_total_trans, last_total_amt, send_flag, send_dt)";
	sqlStmt = sqlStmt + " VALUES ('" + sSettleDate + "','" + sSettleName + "','" + std::to_string(iSettleType) + "','" + std::to_string(lTotalTrans) + "','" + std::to_string(dTotalAmt) + "','" + std::to_string(iSendFlag) + "','" + sSendDate + "')";

	r = centraldb->SQLExecutNoneQuery(sqlStmt);
	if (r == 0)
	{
		operation::getInstance()->writelog("Success to insert into UPT_File_Summary for: " + sSettleName, "DB");
		return iCentralSuccess;
	}
	else
	{
		operation::getInstance()->writelog("Failed to insert into UPT_File_Summary", "DB");
		m_remote_db_err_flag=1;
	}
	return iCentralFail;
}

DBError db::insertUPTFileSummary(const std::string& sSettleDate, const std::string& sSettleName, int iSettleType, uint64_t lTotalTrans, double dTotalAmt, int iSendFlag, const std::string& sSendDate)
{
	int r;
	std::string sqlStmt = "";

	sqlStmt = "INSERT INTO UPT_File_Summary (settle_date, settle_file, settle_type, total_trans, total_amt, send_flag, send_dt)";
	sqlStmt = sqlStmt + " VALUES ('" + sSettleDate + "','" + sSettleName + "','" + std::to_string(iSettleType) + "','" + std::to_string(lTotalTrans) + "','" + std::to_string(dTotalAmt) + "','" + std::to_string(iSendFlag) + "','" + sSendDate + "')";

	r = centraldb->SQLExecutNoneQuery(sqlStmt);
	if (r == 0)
	{
		operation::getInstance()->writelog("Success to insert into UPT_File_Summary for: " + sSettleName, "DB");
		return iCentralSuccess;
	}
	else
	{
		operation::getInstance()->writelog("Failed to insert into UPT_File_Summary", "DB");
		m_remote_db_err_flag=1;
	}
	return iCentralFail;
}

DBError db::DeleteBeforeInsertMT(tExitTrans_Struct& tExit) 
{
	int r;
	std::string sqstr = "";

	sqstr= "Delete from movement_trans_tmp where iu_tk_no = '" + tExit.sIUNo + "' and entry_time = '"+ tExit.sEntryTime + "' and exit_time is null" ;

	r = centraldb->SQLExecutNoneQuery(sqstr);

	if (r == 0)
	{
		return iCentralSuccess;
	}
	return iCentralFail;
	
}

