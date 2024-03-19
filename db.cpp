#include <iostream>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <sys/time.h>
#include <boost/algorithm/string.hpp>
#include "db.h"
#include "log.h"
#include "operation.h"

db* db::db_ = nullptr;

db::db()
{

}

db* db::getInstance()
{
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
		sqlStmt= sqlStmt +  " FROM " + tbName + " where season_no='";
		sqlStmt=sqlStmt + L_sSeasonNo;
		sqlStmt=sqlStmt + "' AND date_from < now() AND date_to >= now() AND zone_id = "+ to_string(iZoneID);

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
	sqlStmt = sqlStmt + ",gst_amt";
	if (sLPRNo!="") sqlStmt = sqlStmt + ",lpr";

	sqlStmt = sqlStmt + ") Values ('" + tEntry.esid + "',convert(datetime,'" + tEntry.sEntryTime+ "',120),'" + tEntry.sIUTKNo;
	sqlStmt = sqlStmt +  "','" + std::to_string(tEntry.iTransType);
	sqlStmt = sqlStmt + "','" + std::to_string(tEntry.iStatus) + "','" + tEntry.sSerialNo;
	sqlStmt = sqlStmt + "','" + std::to_string(tEntry.iCardType)+"'";
	sqlStmt = sqlStmt + ",'" + tEntry.sCardNo + "','" + std::to_string(tEntry.sPaidAmt) + "','" + std::to_string(tEntry.sFee);
	sqlStmt = sqlStmt + "','" + std::to_string(tEntry.sGSTAmt)+"'";

	if (sLPRNo!="") sqlStmt = sqlStmt + ",'" + sLPRNo + "'";

	sqlStmt = sqlStmt +  ")";

	r=centraldb->SQLExecutNoneQuery(sqlStmt);
	
	dbss.str("");  // Set the underlying string to an empty string
    dbss.clear();   // Clear the state of the stream
	if (r!=0)
	{
       // dbss << "Insert Entry_trans to Central: fail2";
		dbss << sqlStmt;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
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
	sqlStmt=sqlStmt +  ",gst_amt";
	sqlStmt = sqlStmt + ",lpr";

	sqlStmt = sqlStmt + ") Values ('" + tEntry.esid+ "','" + tEntry.sEntryTime+ "','" + tEntry.sIUTKNo;
	sqlStmt = sqlStmt +  "','" + std::to_string(tEntry.iTransType);
	sqlStmt = sqlStmt + "','" + std::to_string(tEntry.iStatus) + "','" + tEntry.sSerialNo;
	sqlStmt = sqlStmt + "','" + std::to_string(tEntry.iCardType) + "'";
	sqlStmt = sqlStmt + ",'" + tEntry.sCardNo + "','" + std::to_string(tEntry.sPaidAmt) + "','" + std::to_string(tEntry.sFee);
	sqlStmt = sqlStmt + "','" + std::to_string(tEntry.sGSTAmt)+"'";

	sqlStmt = sqlStmt + ",'" + sLPRNo + "'";

	sqlStmt = sqlStmt +  ")";

	r=localdb->SQLExecutNoneQuery(sqlStmt);

	dbss.str("");  // Set the underlying string to an empty string
    dbss.clear();   // Clear the state of the stream
	
	if (r!=0)
	{
       // dbss << "Insert Entry_trans to Local: fail";
		dbss << sqlStmt;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		return iLocalFail;

	}
	else
	{
		dbss << "Insert Entry_trans to Local: success";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
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
			if (settimeofday(&newTime, nullptr) == 0) {
				dbss << "Time set successfully.";
    			Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			} else {
				dbss << "Error setting time.";
    			Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
					return;
		}			
	}
	return;
}

void db::downloadseason()
{
	unsigned long j,k;
	vector<ReaderItem> tResult;
	vector<ReaderItem> selResult;
	tseason_struct v;
	int r=0;
	string seasonno;
	std::string sqlStmt;
	int w=-1;
	std::stringstream dbss;
	int giStnid;
	giStnid = operation::getInstance()->gtStation.iSID;

	sqlStmt="SELECT SUM(A) FROM (";
	sqlStmt=sqlStmt+ "SELECT count(season_no) as A FROM season_mst WHERE s"+to_string(giStnid)+"_fetched = 0 ";
	sqlStmt=sqlStmt + ") as B ";

	r=centraldb->SQLSelect(sqlStmt,&tResult,false);
	if(r!=0) {m_remote_db_err_flag=1; return;}
	else 
	{
		if (std::stoi(tResult[0].GetDataItem(0)) == 0){return;}
		m_remote_db_err_flag=0;
		dbss << "Total: " << std::string (tResult[0].GetDataItem(0)) << " Seasons to be download.";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}
	
	r=centraldb->SQLSelect("SELECT TOP 10 * FROM season_mst WHERE s"+to_string(1)+"_fetched = 0 ",&selResult,true);
	if(r!=0) {m_remote_db_err_flag=1; return;}
	else m_remote_db_err_flag=0;

	if (selResult.size()>0){

		//--------------------------------------------------------
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading " << std::to_string (selResult.size()) << " Records: Started";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");

		for(j=0;j<selResult.size();j++){
			v.season_no = selResult[j].GetDataItem(1);
			v.SeasonType = selResult[j].GetDataItem(2);  
			seasonno= v.season_no;          
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

			w=writeseason2local(v);
			
			if (w==0) 
			{
				dbss.str("");  // Set the underlying string to an empty string
    			dbss.clear();   // Clear the state of the stream
				dbss << "Download season: " << seasonno;
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");

				//update to central DB
				r=centraldb->SQLExecutNoneQuery("UPDATE season_mst SET s"+
				to_string(1)+"_fetched = '1' WHERE season_no = '"+ seasonno + "'");
				dbss.str("");  // Set the underlying string to an empty string
    			dbss.clear();   // Clear the state of the stream
				if(r!=0) 
				{
					m_remote_db_err_flag=2;
					dbss.str("");  // Set the underlying string to an empty string
    				dbss.clear();   // Clear the state of the stream
					dbss << "update central season status failed.";
					Logger::getInstance()->FnLog(dbss.str(), "", "DB");
				}
				else 
				{
					m_remote_db_err_flag=0;
					//dbss << "set central season success.";
					//Logger::getInstance()->FnLog(dbss.str(), "", "DB");
				}
			}
			

			//---------------------------------------
			
		}
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading Records: End";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}
	if (selResult.size()<10){season_update_flag=0;}
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
		r=localdb->SQLSelect("SELECT season_type FROM season_mst Where season_no= '" + v.season_no + "'",&selResult,false);
		if(r!=0) {
			 m_local_db_err_flag=1;
			 operation::getInstance()->writelog("update season failed.", "DB");
			 return r;
		}
		else  m_local_db_err_flag=0;

		if (selResult.size()>0)
		{
			v.found=1;

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
			
			r=localdb->SQLExecutNoneQuery (sqlStmt);

			if(r!=0)  
			{
				m_local_db_err_flag=1;
				operation::getInstance()->writelog("update season failed.", "DB");
			}
			else  
			{
				m_local_db_err_flag=0;
			}
		}
		else
		{
			v.found=0;
			//insert season records
			sqlStmt="INSERT INTO season_mst ";
			sqlStmt=sqlStmt+ " (season_no,season_type,s_status,date_from,date_to,vehicle_no,rate_type, ";
			sqlStmt=sqlStmt+ " pay_to,pay_date,multi_season_no,zone_id,redeem_amt,redeem_time,holder_type,sub_zone_id)";
			sqlStmt=sqlStmt+ " VALUES ('" + v.season_no+ "'";
			sqlStmt=sqlStmt+ ",'" + v.SeasonType  + "'";
			sqlStmt=sqlStmt+ ",'" + v.s_status + "'";
			sqlStmt=sqlStmt+ ",'" + v.date_from   + "'";
			sqlStmt=sqlStmt+ ",'" + v.date_to  + "'";
			sqlStmt=sqlStmt+ ",'" + v.vehicle_no   + "'";
			sqlStmt=sqlStmt+ ",'" + v.rate_type    + "'";
			sqlStmt=sqlStmt+ ",'" + v.pay_to + "'";
			sqlStmt=sqlStmt+ ",'" + v.pay_date    + "'";
			sqlStmt=sqlStmt+ ",'" + v.multi_season_no   + "'";
			sqlStmt=sqlStmt+ ",'" + v.zone_id   + "'";
			sqlStmt=sqlStmt+ ",'" + v.redeem_amt   + "'";
			sqlStmt=sqlStmt+ ",'" + v.redeem_time   + "'";
			sqlStmt=sqlStmt+ ",'" + v.holder_type   + "'";
			sqlStmt=sqlStmt+ ",'" + v.sub_zone_id  + "') ";

			r=localdb->SQLExecutNoneQuery (sqlStmt);

			if(r!=0)  
			{
				m_local_db_err_flag=1;
				dbss.str("");  // Set the underlying string to an empty string
    			dbss.clear();   // Clear the state of the stream
				//dbss << "Insert season to local failed. ";
				dbss << sqlStmt;
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
					}
			else  
			{
				m_local_db_err_flag=0;
				dbss.str("");  // Set the underlying string to an empty string
    			dbss.clear();   // Clear the state of the stream
				dbss << "Insert season to local success. ";
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}


		}
		
	}
	catch (const std::exception &e)
	{
		r=-1;// success flag
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "DB: local db error in writing rec: " << std::string(e.what());
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		m_local_db_err_flag=1;
	} 
	return r;

}

void db::downloadvehicletype()
{
	unsigned long j;
	vector<ReaderItem> tResult;
	vector<ReaderItem> selResult;
	int r=0;
	string iu_code;
	string iu_type;
	std::string sqlStmt;
	int w=-1;
	std::stringstream dbss;

	//write log for total seasons

	sqlStmt="SELECT SUM(A) FROM (";
	sqlStmt=sqlStmt+ "SELECT count(IUCode) as A FROM Vehicle_type";
	sqlStmt=sqlStmt + ") as B ";

	//dbss << sqlStmt;
    //Logger::getInstance()->FnLog(dbss.str(), "", "DB");

	r=centraldb->SQLSelect(sqlStmt,&tResult,false);
	if(r!=0) {
		m_remote_db_err_flag=1; 
		operation::getInstance()->writelog("download vehicle type fail.", "DB");
		return;}
	else 
	{
		m_remote_db_err_flag=0;
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Total " << std::string (tResult[0].GetDataItem(0)) << " vehicle type to be download.";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		
	}

	r=centraldb->SQLSelect("SELECT  * FROM Vehicle_type",&selResult,true);
	if(r!=0) m_remote_db_err_flag=1;
	else m_remote_db_err_flag=0;

	if (selResult.size()>0){

		//--------------------------------------------------------
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading " << std::to_string (selResult.size()) << " Records: Started";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		

		for(j=0;j<selResult.size();j++){
			// std::cout<<"Rec "<<j<<std::endl;
			// for (k=0;k<selResult[j].getDataSize();k++){
			//     std::cout<<"item["<<k<< "]= " << selResult[j].GetDataItem(k)<<std::endl;                
			// }  
			iu_code = selResult[j].GetDataItem(1);
			iu_type = selResult[j].GetDataItem(2); 

			w=writevehicletype2local(iu_code,iu_type);
			//---------------------------------------
			
		}
		//m->initStnParameters();
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading vehicle type Records: End";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}
	return;

}

int db::writevehicletype2local(string iucode,string iutype)
{

	int r=-1;// success flag
	int debug=0;
	std::string sqlStmt;
	vector<ReaderItem> selResult;
	std::stringstream dbss;
	
	try 
	{
		r=localdb->SQLSelect("SELECT IUCode FROM Vehicle_type Where IUCode= '" + iucode + "'",&selResult,false);
		if(r!=0) {
		   operation::getInstance()->writelog("update vehicle type to local fail.", "DB");
		   m_local_db_err_flag=1;
		   return r;
		}
		else  m_local_db_err_flag=0;

		if (selResult.size()>0)
		{

			//update param records
			sqlStmt="Update Vehicle_type SET ";
			sqlStmt= sqlStmt+ "TransType='" + iutype + "'";
			sqlStmt=sqlStmt + " Where IUCode= '" + iucode + "'";
			
			r=localdb->SQLExecutNoneQuery (sqlStmt);

			if(r!=0)  
			{
				m_local_db_err_flag=1;
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
			sqlStmt="INSERT INTO Vehicle_type";
			sqlStmt=sqlStmt+ " (IUCode,TransType) ";
			
			sqlStmt=sqlStmt+ " VALUES ('" +iucode+ "'";
			sqlStmt=sqlStmt+ ",'" + iutype + "')";

			r=localdb->SQLExecutNoneQuery (sqlStmt);

			if(r!=0)  
			{
				m_local_db_err_flag=1;
				dbss.str("");  // Set the underlying string to an empty string
				dbss.clear();   // Clear the state of the stream
				dbss << "insert vehicle type to local failed";
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
			else  
			{
				m_local_db_err_flag=0;
			}
		}

	}
	catch (const std::exception &e)
	{
		r=-1;// success flag
		// cout << "ERROR: " << err << endl;
		dbss.str("");  // Set the underlying string to an empty string
		dbss.clear();   // Clear the state of the stream
		dbss << "DB: local db error in writing rec: " << std::string(e.what());
		Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		m_local_db_err_flag=1;
	} 
	return r;
}

void db::downloadledmessage()
{
	unsigned long j,k;
	vector<ReaderItem> tResult;
	vector<ReaderItem> selResult;
	int r=-1;
	string msg_id;
	string msg_body;
	string msg_status;
	std::string sqlStmt;
	int w=-1;
	std::stringstream dbss;
	int giStnid;
	giStnid = operation::getInstance()->gtStation.iSID;

	//write log for total seasons

	sqlStmt="SELECT SUM(A) FROM (";
	sqlStmt=sqlStmt+ "SELECT count(msg_id) as A FROM message_mst WHERE s"+to_string(giStnid)+"_fetched = 0";
	sqlStmt=sqlStmt + ") as B ";

	//dbss << sqlStmt;
    //Logger::getInstance()->FnLog(dbss.str(), "", "DB");

	r=centraldb->SQLSelect(sqlStmt,&tResult,false);
	if(r!=0) {
		m_remote_db_err_flag=1;
		operation::getInstance()->writelog("download LED message fail.", "DB");
		return;
	}
	else 
	{
		m_remote_db_err_flag=0;
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Total " << std::string (tResult[0].GetDataItem(0)) << " message to be download.";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}

	r=centraldb->SQLSelect("SELECT  * FROM message_mst WHERE s"+to_string(giStnid)+"_fetched = 0",&selResult,true);
	if(r!=0) {
		m_remote_db_err_flag=1;
		operation::getInstance()->writelog("download LED message fail.", "DB");
		return;
	}
	else m_remote_db_err_flag=0;

	if (selResult.size()>0){

		//--------------------------------------------------------
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading message " << std::to_string (selResult.size()) << " Records: Started";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");

		for(j=0;j<selResult.size();j++){
			// std::cout<<"Rec "<<j<<std::endl;
			// for (k=0;k<selResult[j].getDataSize();k++){
			//     std::cout<<"item["<<k<< "]= " << selResult[j].GetDataItem(k)<<std::endl;                
			// }  
			msg_id = selResult[j].GetDataItem(0);
			msg_body = selResult[j].GetDataItem(2);
			msg_status =  selResult[j].GetDataItem(3); 
			
			w=writeledmessage2local(msg_id,msg_body,msg_status);
			
			if (w==0) 
			{
				//update to central DB
				r=centraldb->SQLExecutNoneQuery("UPDATE message_mst SET s"+
				to_string(giStnid)+"_fetched = '1' WHERE msg_id = '"+msg_id+ "'");
				if(r!=0) 
				{
					m_remote_db_err_flag=2;
					dbss.str("");  // Set the underlying string to an empty string
    				dbss.clear();   // Clear the state of the stream
					dbss << "update central message status failed.";
					Logger::getInstance()->FnLog(dbss.str(), "", "DB");
				}
				else 
				{
					m_remote_db_err_flag=0;
					//printf("update central message success \n");
				}
			}
			

			//---------------------------------------
			
		}
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading Msg Records: End";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}
	return;

}

int db::writeledmessage2local(string m_id,string m_body, string m_status)
{

	int r=-1;// success flag
	int debug=0;
	std::string sqlStmt;
	vector<ReaderItem> selResult;
	std::stringstream dbss;

	try 
	{
		r=localdb->SQLSelect("SELECT msg_id FROM message_mst Where msg_id= '" + m_id + "'",&selResult,false);
		if(r!=0) {
		 m_local_db_err_flag=1;
		 operation::getInstance()->writelog("update local LED Message failed.", "DB");
		 return r;
		}
		else  m_local_db_err_flag=0;

		if (selResult.size()>0)
		{

			//update param records
			sqlStmt="Update message_mst SET ";
			sqlStmt= sqlStmt+ "msg_body='" + m_body + "'";
			sqlStmt=sqlStmt + ", m_status= '" + m_status + "'";
			sqlStmt=sqlStmt + " Where msg_id= '" + m_id + "'";
			
			r=localdb->SQLExecutNoneQuery (sqlStmt);

			if(r!=0)  
			{
				m_local_db_err_flag=1;
				dbss.str("");  // Set the underlying string to an empty string
				dbss.clear();   // Clear the state of the stream
				dbss << "update local message failed";
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
			else  
			{
				m_local_db_err_flag=0;
			//	printf("update local msg success \n");
			}
			
		}
		else
		{
			sqlStmt="INSERT INTO message_mst";
			sqlStmt=sqlStmt+ " (msg_id,msg_body,m_status) ";
			
			sqlStmt=sqlStmt+ " VALUES ('" +m_id+ "'";
			sqlStmt=sqlStmt+ ",'" + m_body + "'";
			sqlStmt=sqlStmt+ ",'" + m_status + "')";

			r=localdb->SQLExecutNoneQuery (sqlStmt);

			if(r!=0)  
			{
				m_local_db_err_flag=1;
				dbss.str("");  // Set the underlying string to an empty string
				dbss.clear();   // Clear the state of the stream
				dbss << "insert message to local failed";
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
			else  
			{
				m_local_db_err_flag=0;
			//	printf("set local msg success \n");
			}
		}

	}
	catch (const std::exception &e)
	{
		r=-1;// success flag
		// cout << "ERROR: " << err << endl;
		dbss.str("");  // Set the underlying string to an empty string
		dbss.clear();   // Clear the state of the stream
		dbss << "DB: local db error in writing rec: " << std::string(e.what());
		Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		m_local_db_err_flag=1;
	} 
	return r;

}

void db::downloadparameter()
{
	unsigned long j,k;
	vector<ReaderItem> tResult;
	vector<ReaderItem> selResult;
	int r=0;
	string param_name;
	string param_value;
	std::string sqlStmt;
	std::stringstream dbss;
	int w=-1;
	int giStnid;
	giStnid = operation::getInstance()->gtStation.iSID;

	//write log for total seasons

	sqlStmt="SELECT SUM(A) FROM (";
	sqlStmt=sqlStmt+ "SELECT count(name) as A FROM parameter_mst WHERE s"+to_string(giStnid)+"_fetched = 0 and for_station=1";
	sqlStmt=sqlStmt + ") as B ";


	r=centraldb->SQLSelect(sqlStmt,&tResult,false);
	if(r!=0) {
		m_remote_db_err_flag=1;
		operation::getInstance()->writelog("download parameter fail.", "DB");
		return;
	}
	else 
	{
		m_remote_db_err_flag=0;
		dbss << "Total " << std::string (tResult[0].GetDataItem(0)) << " parameter to be download.";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}

	r=centraldb->SQLSelect("SELECT  * FROM parameter_mst WHERE s"+to_string(giStnid)+"_fetched = 0 and for_station=1",&selResult,true);
	if(r!=0) {
		m_remote_db_err_flag=1;
		operation::getInstance()->writelog("update parameter failed.", "DB");
		return;
	}
	else m_remote_db_err_flag=0;

	if (selResult.size()>0){

		//--------------------------------------------------------
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading parameter " << std::to_string (selResult.size()) << " Records: Started";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");

		for(j=0;j<selResult.size();j++){

			param_name = selResult[j].GetDataItem(0);
			param_value = selResult[j].GetDataItem(49+giStnid);      
			dbss.str("");  // Set the underlying string to an empty string
			dbss.clear();   // Clear the state of the stream
			dbss << param_name << " = "  << param_value;
			Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			param_update_count++;
			w=writeparameter2local(param_name,param_value);
			
			if (w==0) 
			{
				//update to central DB
				r=centraldb->SQLExecutNoneQuery("UPDATE parameter_mst SET s"+
				to_string(giStnid)+"_fetched = '1' WHERE name = '"+param_name+ "'");
				if(r!=0) 
				{
					m_remote_db_err_flag=2;
					dbss.str("");  // Set the underlying string to an empty string
    				dbss.clear();   // Clear the state of the stream
					dbss << "update central parameter status failed.";
					Logger::getInstance()->FnLog(dbss.str(), "", "DB");
				}
				else 
				{
					m_remote_db_err_flag=0;
				//	printf("set central parameter success \n");
				}
			}			
			
		}
		//LoadParam();
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Downloading Parameter Records: End";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}
	else param_update_flag=0;
}


int db::writeparameter2local(string name,string value)
{

	
	int r=-1;// success flag
	std::string sqlStmt;
	vector<ReaderItem> selResult;
	std::stringstream dbss;
	
	try 
	{
		
		r=localdb->SQLSelect("SELECT ParamName FROM Param_mst Where ParamName= '" + name + "'",&selResult,false);
		if(r!=0)  {
			m_local_db_err_flag=1;
			operation::getInstance()->writelog("update parameter fail.", "DB");
			return r;
		}
		else  m_local_db_err_flag=0;

		if (selResult.size()>0)
		{

			//update param records
			sqlStmt="Update Param_mst SET ";
			sqlStmt= sqlStmt+ "ParamValue='" + value + "'";
			sqlStmt=sqlStmt + " Where ParamName= '" + name + "'";
			
			r=localdb->SQLExecutNoneQuery (sqlStmt);

			if(r!=0)  
			{
				m_local_db_err_flag=1;
				dbss.str("");  // Set the underlying string to an empty string
				dbss.clear();   // Clear the state of the stream
				dbss << "update parameter to local failed";
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
			else  
			{
				m_local_db_err_flag=0;
			//	printf("update Param success \n");
			}
			
		}
		else
		{
			sqlStmt="INSERT INTO Param_mst";
			sqlStmt=sqlStmt+ " (ParamName,ParamValue) ";
			
			sqlStmt=sqlStmt+ " VALUES ('" +name+ "'";
			sqlStmt=sqlStmt+ ",'" + value  + "')";

			r=localdb->SQLExecutNoneQuery (sqlStmt);

			if(r!=0)  
			{
				m_local_db_err_flag=1;
				dbss.str("");  // Set the underlying string to an empty string
				dbss.clear();   // Clear the state of the stream
				dbss << "insert parameter to local failed";
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
			else  
			{
				m_local_db_err_flag=0;
				//printf("set Param success \n");
			}
		}

	}
	catch (const std::exception &e)
	{
		r=-1;// success flag
		// cout << "ERROR: " << err << endl;
		dbss.str("");  // Set the underlying string to an empty string
		dbss.clear();   // Clear the state of the stream
		dbss << "DB: local db error in writing rec: " << std::string(e.what());
		Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		m_local_db_err_flag=1;
	} 
	return r;

}

void db::downloadstationsetup()
{
	tstation_struct v;
	vector<ReaderItem> tResult;
	vector<ReaderItem> selResult;
	std::string sqlStmt;
	std::stringstream dbss;
	int r=-1;
	int w=-1;
	
	//write log for total seasons

	sqlStmt="SELECT SUM(A) FROM (";
	sqlStmt=sqlStmt+ "SELECT count(station_id) as A FROM station_setup";
	sqlStmt=sqlStmt + ") as B ";

	//dbss << sqlStmt;
    //Logger::getInstance()->FnLog(dbss.str(), "", "DB");

	r=centraldb->SQLSelect(sqlStmt,&tResult,false);
	if(r!=0) {
		m_remote_db_err_flag=1;
		operation::getInstance()->writelog("download station setup fail.", "DB");
		return;
	}
	else 
	{
		m_remote_db_err_flag=0;
		dbss.str("");  // Set the underlying string to an empty string
    	dbss.clear();   // Clear the state of the stream
		dbss << "Total " << std::string (tResult[0].GetDataItem(0)) << " station setup to be download.";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		
	}

	r=centraldb->SQLSelect("SELECT  * FROM station_setup",&selResult,true);
	if(r!=0) {
		m_remote_db_err_flag=1;
		operation::getInstance()->writelog("download station setup fail.", "DB");
		return;
	}
	else m_remote_db_err_flag=0;

	if (selResult.size()>0)
	{
		for(int j=0;j<selResult.size();j++){
			v.iGroupID=std::stoi(selResult[j].GetDataItem(0));
			v.iSID= std::stoi(selResult[j].GetDataItem(2));
			v.sName= selResult[j].GetDataItem(3);
			switch(std::stoi(selResult[j].GetDataItem(5)))
			{
			case 1:
				v.iType=tientry;
				break;
			case 2:
				v.iType=tiExit;
				break;
			default:
				break;
			};
			v.iStatus= std::stoi(selResult[j].GetDataItem(6));
			v.sPCName= selResult[j].GetDataItem(4);
			v.iCHUPort= std::stoi(selResult[j].GetDataItem(17));
			v.iAntID= std::stoi(selResult[j].GetDataItem(18));
			v.iZoneID= std::stoi(selResult[j].GetDataItem(19));
			v.iIsVirtual= std::stoi(selResult[j].GetDataItem(20));
			switch(std::stoi(selResult[j].GetDataItem(22)))
			{
			case 0:
				v.iSubType=iNormal;
				break;
			case 1:
				v.iSubType=iXwithVENoPay;
				break;
			case 2:
				v.iSubType=iXwithVEPay;
				break;
			default:
				break;
			};
			v.iVirtualID= std::stoi(selResult[j].GetDataItem(21));
			
			w=writestationsetup2local(v);
			
			if (w==0) 
			{
			//	printf("set station setup ok \n");
			}
			//m->initStnParameters();
		}
		
	}
	if (selResult.size()<1){
		//no record
		r=-1;
	}
	return;
}


int db::writestationsetup2local(tstation_struct& v)
{
	
	int r=-1;// success flag
	int debug=0;
	std::string sqlStmt;
	vector<ReaderItem> selResult;
	std::stringstream dbss;
	
	try 
	{ 
		r=localdb->SQLSelect("SELECT StationType FROM Station_Setup Where StationID= '" + std::to_string(v.iSID) + "'",&selResult,false);
		if(r!=0){
			m_local_db_err_flag=1;
			operation::getInstance()->writelog("update station setup fail.", "DB");
			return r;
		}
		else  m_local_db_err_flag=0;

		if (selResult.size()>0)
		{
			sqlStmt="Update Station_Setup SET ";
			//sqlStmt= sqlStmt+ "groupid='" + std::to_string(v.iGroupID) + "',";
			sqlStmt= sqlStmt+ "StationID='" + std::to_string(v.iSID) + "',";
			sqlStmt= sqlStmt+ "StationName='" +v.sName + "',";
			sqlStmt= sqlStmt+ "StationType='" + std::to_string(v.iType) + "',";
			sqlStmt= sqlStmt+ "Status='" + std::to_string(v.iStatus) + "',";
			sqlStmt= sqlStmt+ "PCName='" + v.sPCName + "',";
			sqlStmt= sqlStmt+ "CHUPort='" + std::to_string(v.iCHUPort) + "',";
			sqlStmt= sqlStmt+ "AntID='" + std::to_string(v.iAntID) + "',";
			sqlStmt= sqlStmt+ "ZoneID='" + std::to_string(v.iZoneID) + "',";
			sqlStmt= sqlStmt+ "IsVirtual='" + std::to_string(v.iIsVirtual) + "',";
			sqlStmt= sqlStmt+ "SubType='" + std::to_string(v.iSubType) + "',";
			sqlStmt= sqlStmt+ "VirtualID='" + std::to_string(v.iVirtualID) + "'";
			sqlStmt= sqlStmt+ " WHERE StationID='" +std::to_string(v.iSID) + "'";
			r=localdb->SQLExecutNoneQuery (sqlStmt);

			if(r!=0)  
			{
				m_local_db_err_flag=1;
				dbss.str("");  // Set the underlying string to an empty string
				dbss.clear();   // Clear the state of the stream
				dbss << " Update local station set up failed";
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
			else  
			{
				m_local_db_err_flag=0;
			//	printf("update Station_Setup success \n");
			}
		}
		else
		{
			sqlStmt="INSERT INTO Station_Setup";
			sqlStmt=sqlStmt+ " (StationID,StationName,StationType,Status,PCName,CHUPort,AntID, ";
			sqlStmt=sqlStmt+ " ZoneID,IsVirtual,SubType,VirtualID)";
			sqlStmt=sqlStmt+ " VALUES ('" + std::to_string(v.iSID)+ "'";
			sqlStmt=sqlStmt+ ",'" + v.sName + "'";
			sqlStmt=sqlStmt+ ",'" + std::to_string(v.iType) + "'";
			sqlStmt=sqlStmt+ ",'" + std::to_string(v.iStatus) + "'";
			sqlStmt=sqlStmt+ ",'" + v.sPCName + "'";
			sqlStmt=sqlStmt+ ",'" + std::to_string(v.iCHUPort) + "'";
			sqlStmt=sqlStmt+ ",'" + std::to_string(v.iAntID) + "'";
			sqlStmt=sqlStmt+ ",'" + std::to_string(v.iZoneID) + "'";
			sqlStmt=sqlStmt+ ",'" +  std::to_string(v.iIsVirtual) + "'";
			sqlStmt=sqlStmt+ ",'" + std::to_string(v.iSubType) + "'";
			sqlStmt=sqlStmt+ ",'" + std::to_string(v.iVirtualID) + "')";
			//sqlStmt=sqlStmt+ ",'" + std::to_string(v.iGroupID) + "') ";

			r=localdb->SQLExecutNoneQuery (sqlStmt);

			if(r!=0)  
			{
				m_local_db_err_flag=1;
				dbss.str("");  // Set the underlying string to an empty string
				dbss.clear();   // Clear the state of the stream
				dbss << "insert parameter to local failed";
				Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			}
			else  
			{
				m_local_db_err_flag=0;
			//	printf("station_setup insert sucess\n");
			}	               
		}
	}
	catch (const std::exception &e)
	{
		r=-1;// success flag
		// cout << "ERROR: " << err << endl;
		dbss.str("");  // Set the underlying string to an empty string
		dbss.clear();   // Clear the state of the stream
		dbss << "DB: local db error in writing rec: " << std::string(e.what());
		Logger::getInstance()->FnLog(dbss.str(), "", "DB");
		m_local_db_err_flag=1;
	} 
	return r;
}

DBError db::loadstationsetup()
{
	vector<ReaderItem> selResult;
	int r=-1;
	int giStnid;
	giStnid = operation::getInstance()->gtStation.iSID;

	r=localdb->SQLSelect("SELECT * FROM Station_Setup WHERE StationId = '"+
	std::to_string(giStnid)+"'",&selResult,false);

	if (r!=0)
	{
		operation::getInstance()->writelog("get station set up fail.", "DB");
		return iLocalFail;
	}

	if (selResult.size()>0)
	{            
		//Set configuration
		operation::getInstance()->gtStation.iSID= std::stoi(selResult[0].GetDataItem(0));
		operation::getInstance()->gtStation.sName= selResult[0].GetDataItem(1);
		switch(std::stoi(selResult[0].GetDataItem(2)))
		{
		case 1:
			operation::getInstance()->gtStation.iType=tientry;
			break;
		case 2:
			operation::getInstance()->gtStation.iType=tiExit;
			break;
		default:
			break;
		};
		operation::getInstance()->gtStation.iStatus= std::stoi(selResult[0].GetDataItem(3));
		operation::getInstance()->gtStation.sPCName= selResult[0].GetDataItem(4);
		operation::getInstance()->gtStation.iCHUPort= std::stoi(selResult[0].GetDataItem(5));
		operation::getInstance()->gtStation.iAntID= std::stoi(selResult[0].GetDataItem(6));
		operation::getInstance()->gtStation.iZoneID= std::stoi(selResult[0].GetDataItem(7));
		operation::getInstance()->gtStation.iIsVirtual= std::stoi(selResult[0].GetDataItem(8));
		switch(std::stoi(selResult[0].GetDataItem(9)))
		{
		case 0:
			operation::getInstance()->gtStation.iSubType=iNormal;
			break;
		case 1:
			operation::getInstance()->gtStation.iSubType=iXwithVENoPay;
			break;
		case 2:
			operation::getInstance()->gtStation.iSubType=iXwithVEPay;
			break;
		default:
			break;
		};
		operation::getInstance()->gtStation.iVirtualID= std::stoi(selResult[0].GetDataItem(10));
		operation::getInstance()->gtStation.iGroupID= std::stoi(selResult[0].GetDataItem(11));
		//m->gtStation.sZoneName= selResult[0].GetDataItem(11);
		//m->gtStation.iVExitID= std::stoi(selResult[0].GetDataItem(12));
		operation::getInstance()->tProcess.gbloadedStnSetup = true;
		return iDBSuccess;
	}
	return iNoData; 
};

DBError db::loadcentralDBinfo()
{
	vector<ReaderItem> selResult;
	int r=-1;
	int giStnid;
	giStnid = operation::getInstance()->gtStation.iSID;

	r=localdb->SQLSelect("SELECT * FROM Station_Setup WHERE StationId = '"+
	std::to_string(giStnid)+"'",&selResult,false);

	if (r!=0)
	{
		//m_log->WriteAndPrint("Get Station Setup: fail");
		return iLocalFail;
	}

	if (selResult.size()>0)
	{            
		//Set configuration
		operation::getInstance()->gtStation.iSID= std::stoi(selResult[0].GetDataItem(0));
		operation::getInstance()->gtStation.sName= selResult[0].GetDataItem(1);
		switch(std::stoi(selResult[0].GetDataItem(2)))
		{
		case 1:
			operation::getInstance()->gtStation.iType=tientry;
			break;
		case 2:
			operation::getInstance()->gtStation.iType=tiExit;
			break;
		default:
			break;
		};
		operation::getInstance()->gtStation.iStatus= std::stoi(selResult[0].GetDataItem(3));
		operation::getInstance()->gtStation.sPCName= selResult[0].GetDataItem(4);
		operation::getInstance()->gtStation.iCHUPort= std::stoi(selResult[0].GetDataItem(5));
		operation::getInstance()->gtStation.iAntID= std::stoi(selResult[0].GetDataItem(6));
		operation::getInstance()->gtStation.iZoneID= std::stoi(selResult[0].GetDataItem(7));
		operation::getInstance()->gtStation.iIsVirtual= std::stoi(selResult[0].GetDataItem(8));
		switch(std::stoi(selResult[0].GetDataItem(9)))
		{
		case 0:
			operation::getInstance()->gtStation.iSubType=iNormal;
			break;
		case 1:
			operation::getInstance()->gtStation.iSubType=iXwithVENoPay;
			break;
		case 2:
			operation::getInstance()->gtStation.iSubType=iXwithVEPay;
			break;
		default:
			break;
		};
		operation::getInstance()->gtStation.iVirtualID= std::stoi(selResult[0].GetDataItem(10));
		operation::getInstance()->gtStation.iGroupID= std::stoi(selResult[0].GetDataItem(11));
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

	if (selResult.size()>0)
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

				if (readerItem.GetDataItem(0) == "EPS")
				{
					operation::getInstance()->tParas.giEPS = std::stoi(readerItem.GetDataItem(1));
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

	if (selResult.size()>0)
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
	int ret = -1;

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
			sqlStmt = sqlStmt + ",lpr";
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
			sqlStmt = sqlStmt + ",lpr";
			
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
	sqstr=sqstr + ",gst_amt,lpr";
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

	//operation::getInstance()->writelog(sqstr,"DB");
	
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
