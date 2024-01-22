#include <iostream>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <sys/time.h>
#include "db.h"
#include "log.h"


db::db(string localStr,string CentralStr,
string cent_IP,int LocalSQLTimeOut,
int CentralSQLTimeOut, int SP_SQLTimeOut,float mPingTimeOut)
{

	localConnStr=localStr;
	CentralConnStr=CentralStr;
	central_IP=cent_IP;

	CentralDB_TimeOut=CentralSQLTimeOut;
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
	centraldb=new odbc(SP_TimeOut,1,PingTimeOut,central_IP,CentralConnStr);
   
    std::stringstream dbss;
	if (centraldb->Connect()==0) {
		dbss << "Central DB is connected!" ;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}
	else {
		dbss << "unable to connect Central DB" ;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}
	
	dbss.str("");  // Set the underlying string to an empty string
    dbss.clear();   // Clear the state of the stream

	localdb=new odbc(LocalDB_TimeOut,1,PingTimeOut,"127.0.0.1",localConnStr);

	if (localdb->Connect()==0) {
		dbss << "Local DB is connected!" ;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}
	else{
		dbss << "unable to connect Local DB" ;
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
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

int db::local_isvalidseason(string L_sSeasonNo)
{
	std::string sqlStmt;
	std::string tbName="season_mst";
	std::string sValue;
	vector<ReaderItem> selResult;
	int r,j,k,i;
	unsigned int lZID=0;

	try
	{


		sqlStmt="Select season_type,s_status,date_from,date_to,vehicle_no,rate_type,multi_season_no,zone_id,redeem_amt,redeem_time,holder_type,sub_zone_id ";
		sqlStmt= sqlStmt +  " FROM " + tbName + " where season_no='";
		sqlStmt=sqlStmt + L_sSeasonNo;
		sqlStmt=sqlStmt + "' AND date_from < now() AND date_to >= now()";

		//r=clsObj.SelectQueryLocal(localConnStr,sqlStmt,&selResult ,true);

		r=localdb->SQLSelect(sqlStmt,&selResult,false);
		if (r!=0) return iLocalFail;

		if (selResult.size()>0){
			//if (m->gtStation.iType==tientry)
			//{
				//m->tEntry.iTransType=std::stoi(selResult[0].GetDataItem(0));
				//m->tEntry.iStatus=std::stoi(selResult[0].GetDataItem(1));
			//	m->tEntry.iRateType=std::stoi(selResult[0].GetDataItem(5));
			//	printf("entry trans type %d and rate type %d date %s\n",m->tEntry.iTransType,m->tEntry.iRateType,m->tExit.sWDFrom.c_str());
			//}				
			//else 
			//{
				//m->tExit.iTransType=std::stoi(selResult[0].GetDataItem(0));
				//m->tExit.iStatus=std::stoi(selResult[0].GetDataItem(1));
				// m->tExit.sWDFrom=selResult[0].GetDataItem(2);
				// m->tExit.sWDTo=selResult[0].GetDataItem(3);
				// m->tExit.iRateType=std::stoi(selResult[0].GetDataItem(5));
				// m->tExit.sRedeemAmt=std::stof(selResult[0].GetDataItem(8));
				// m->tExit.iRedeemTime=std::stoi(selResult[0].GetDataItem(9));
			//};
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

int db::entry_query(string m_sSeasonNo)
{
	string sSerialNo="";
	float sFee=0;
	short int iRateType=0;
	short int m_iExpireDays=0, m_iRedeemTime=0;
	float m_sAdminFee=0,m_sAppFee=0, m_sRedeemAmt=0;
	std::string  m_dtValidTo="", m_dtValidFrom="";
	
	//unsigned int mZID=2;
	std::string  m_AllowedHolderType="";

	//const std::string m_sSeasonNo="1060000000";//
	const std::string m_sBlank="";
	int retcode;
	string msg;
    std::stringstream dbss;
	dbss << "Check Season on Central DB for IU/card: " << m_sSeasonNo;
    Logger::getInstance()->FnLog(dbss.str(), "", "DB");

	retcode=sp_isvalidseason(m_sSeasonNo, 1,1,sSerialNo,iRateType,
	sFee,m_sAdminFee,m_sAppFee,m_iExpireDays,m_iRedeemTime, m_sRedeemAmt,
	m_AllowedHolderType,m_dtValidTo,m_dtValidFrom );

	dbss.str("");  // Set the underlying string to an empty string
    dbss.clear();   // Clear the state of the stream
	dbss << "Check Season Ret =" << retcode;
    Logger::getInstance()->FnLog(dbss.str(), "", "DB");

	if(retcode!=-1)
	{
		//printf("retcode %d",retcode);
		//msg= std::to_string(retcode);
		//printf("String: %s\n", m_dtValidFrom);
		//printf("String: %s\n", m_dtValidTo);
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
		}
		return(retcode);
	}
	else
	{
		//int l_ret=local_isvalidseason(m_sSeasonNo);
		//if(l_ret==iDBSuccess) return(1);
		//else 
		return(0);
	}

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
			return iCentralFail;
			//goto processLocal;
		}
	}

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

	//sqlStmt = sqlStmt + ",card_no,paid_amt,parking_fee";
	//sqlStmt = sqlStmt + ",gst_amt";
	if (sLPRNo!="") sqlStmt = sqlStmt + ",lpn";

	sqlStmt = sqlStmt + ") Values ('" + tEntry.esid + "',convert(datetime,'" + tEntry.sEntryTime+ "',120),'" + tEntry.sIUTKNo;
	sqlStmt = sqlStmt +  "','" + std::to_string(tEntry.iTransType);
	sqlStmt = sqlStmt + "','" + std::to_string(tEntry.iStatus) + "','" + tEntry.sSerialNo;
	sqlStmt = sqlStmt + "','" + std::to_string(tEntry.iCardType)+"'";
	//sqlStmt = sqlStmt + ",'" + tEntry.sCardNo + "','" + std::to_string(tEntry.sPaidAmt) + "','" + std::to_string(tEntry.sFee);
	//sqlStmt = sqlStmt + "','" + std::to_string(tEntry.sGSTAmt)+"'";

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
			dbss << "Insert Entry_trans to Central: fail1" ;
    		Logger::getInstance()->FnLog(dbss.str(), "", "DB");
			return iLocalFail;
		}
	}

	sqlStmt= "Insert into Entry_Trans ";
	sqlStmt=sqlStmt + "(Station_ID,Entry_Time,iu_tk_No,";
	sqlStmt=sqlStmt + "trans_type,status";
	sqlStmt=sqlStmt + ",TK_SerialNo";

	sqlStmt=sqlStmt + ",Card_Type";
	//sqlStmt=sqlStmt + ",card_no,paid_amt,parking_fee";
	//sqlStmt=sqlStmt +  ",gst_amt";
	if (sLPRNo!="") sqlStmt = sqlStmt + ",lpn";

	sqlStmt = sqlStmt + ") Values ('" + tEntry.esid+ "','" + tEntry.sEntryTime+ "','" + tEntry.sIUTKNo;
	sqlStmt = sqlStmt +  "','" + std::to_string(tEntry.iTransType);
	sqlStmt = sqlStmt + "','" + std::to_string(tEntry.iStatus) + "','" + tEntry.sSerialNo;
	sqlStmt = sqlStmt + "','" + std::to_string(tEntry.iCardType) + "'";
	//sqlStmt = sqlStmt + ",'" + m->tEntry.sCardNo + "','" + std::to_string(m->tEntry.sPaidAmt) + "','" + std::to_string(m->tEntry.sFee);
	//sqlStmt = sqlStmt + "','" + std::to_string(m->tEntry.sGSTAmt)+"'";

	if (sLPRNo!="") sqlStmt = sqlStmt + ",'" + sLPRNo + "'";

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
	vehicle_struct v;
	int r=0;
	string seasonno;
	std::string sqlStmt;
	int w=-1;
	std::stringstream dbss;

	sqlStmt="SELECT SUM(A) FROM (";
	sqlStmt=sqlStmt+ "SELECT count(season_no) as A FROM season_mst WHERE s"+to_string(1)+"_fetched = 0 ";
	sqlStmt=sqlStmt + ") as B ";

	r=centraldb->SQLSelect(sqlStmt,&tResult,false);
	if(r!=0) m_remote_db_err_flag=1;
	else 
	{
		m_remote_db_err_flag=0;
		dbss << "Total: " << std::string (tResult[0].GetDataItem(0)) << " Seasons to be download.";
    	Logger::getInstance()->FnLog(dbss.str(), "", "DB");
	}

	r=centraldb->SQLSelect("SELECT TOP 10 * FROM season_mst WHERE s"+to_string(1)+"_fetched = 0 ",&selResult,true);
	if(r!=0) m_remote_db_err_flag=1;
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
					dbss << "set central season failed.";
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

int db::writeseason2local(vehicle_struct& v)
{

	time_t vfdate,vtdate;
	CE_Time vfdt,vtdt;
	int r=-1;// success flag
	int debug=0;
	std::string sqlStmt;
	vector<ReaderItem> selResult;
	std::stringstream dbss;
	
	try 
	{
		r=localdb->SQLSelect("SELECT season_type FROM season_mst Where season_no= '" + v.season_no + "'",&selResult,false);
		if(r!=0)  m_local_db_err_flag=1;
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
				printf("update season failed \n");
			}
			else  
			{
				m_local_db_err_flag=0;
				printf("update season success \n");
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
				dbss << "Insert season to local failed. ";
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






