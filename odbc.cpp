
#include <stdio.h>
#include "odbc.h"
#include "log.h"

ReaderItem::ReaderItem()
{
    //ctor
}
ReaderItem::~ReaderItem()
{
    //dtor
}
void ReaderItem::appendData(std::string s)
{
    data.push_back(s);
}

unsigned long ReaderItem::getDataSize()
{
    return data.size();
}
std::string ReaderItem::GetDataItem(unsigned long index)
{
    if ((data.size()>0)&& (index<data.size()))
    {
        return data[index];
    }
    else
    {
        //return empty string for particular index is
        //data size =0 or
        //index> data size
        return "";
    }
}


// constructor
// allocate environment handle and connection handle
odbc::odbc(unsigned int ConnTO,unsigned int queryTO, float pingTO,
            std::string IP,string conn)
{
    SQLRETURN ret; //return status
    NumberOfRowsAffected=0;
    try
    {
        ConnTimeOutVal=ConnTO;
        m_IP=IP;
        m_connString=conn;
        queryTimeOut=queryTO;
        pingTimeOut=pingTO;

        ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env); //allocate environment handle
        if (!SQL_SUCCEEDED(ret)) {
          GetError("SQLAllocHandle", env, SQL_HANDLE_ENV);
          return;
        }
        SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (void *) SQL_OV_ODBC3, 0); // ODBC version 3
      
        ret = SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc); // allocate connection handle
        if (!SQL_SUCCEEDED(ret)) {
          GetError("SQLAllocHandle", dbc, SQL_HANDLE_DBC);
          return;
        } 

        ret=SQLSetConnectAttr(dbc, SQL_ATTR_CONNECTION_TIMEOUT, (SQLPOINTER)(intptr_t)ConnTimeOutVal, 0);
        if (!SQL_SUCCEEDED(ret))
        {
          GetError("SQLSetConnectAttr(SQL_CONNECTION_TIMEOUT)", dbc, SQL_HANDLE_DBC);

          return;
        }
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
}

// destructor
// free up connection handle and environment handle
odbc::~odbc()
{
    // free up allocated handles
    SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    SQLFreeHandle(SQL_HANDLE_ENV, env);    
}

// connect to datasource

int odbc::Connect()
{
    SQLRETURN ret; //return status
    SQLCHAR outstr[1024]; // output string
    SQLSMALLINT outstrlen; // output string length
    
    try
    {
        if (vPing(m_IP,pingTimeOut)==false) return -1;
        // Connect to a DSN
        //SQLCHAR* connStr = (SQLCHAR*)"DSN=mssqlserver;DATABASE=RF;UID=sa;PWD=yzhh2007";
        std::string constr=m_connString;
        SQLCHAR* connStr = (SQLCHAR*) constr.c_str();
        ret = SQLDriverConnect(dbc, NULL, 
          connStr, SQL_NTS,
          outstr, sizeof(outstr), &outstrlen,
          SQL_DRIVER_NOPROMPT);
        if (!SQL_SUCCEEDED(ret)) {
          GetError("SQLDriverConnect", dbc, SQL_HANDLE_DBC);
          return -1;
        }
        if (ret == SQL_SUCCESS_WITH_INFO) {
          GetError("SQLDriverConnect", dbc, SQL_HANDLE_DBC);
        }
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return -1;
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return -1;
    }
    return 0;  
}


 int odbc::SQLSelect(std::string statement, std::vector<ReaderItem> *result,bool FullResult)
 {
    int ret; //return status
    int row=0;
  
    SQLSMALLINT columns; // number of columns
    SQLLEN rows; // number of rows
    //std::vector<std::vector<std::string>> ds;
    SQLHSTMT stmt = SQL_NULL_HSTMT;

    std::vector<ReaderItem> mList;

    try
    {
        if (IsConnected()!=1)
        {
            Disconnect();
            if (Connect() != 0) {return -1;}
        }

        ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt); // allocate statement handle
        if (!SQL_SUCCEEDED(ret)) {
          GetError("SQLAllocHandle", stmt, SQL_HANDLE_STMT);
          return -1;
        }

        SQLSetStmtAttr(stmt, SQL_QUERY_TIMEOUT, (SQLPOINTER)(intptr_t) queryTimeOut, SQL_IS_UINTEGER);

        ret = SQLExecDirect(stmt, (SQLCHAR*)statement.c_str(), SQL_NTS);
        if (!SQL_SUCCEEDED(ret)) {
          GetError("SQLExecDirect", stmt, SQL_HANDLE_STMT);
          SQLFreeHandle(SQL_HANDLE_STMT, stmt);
          return -1;
        }    
        SQLNumResultCols(stmt, &columns);//get numbers of columns
        SQLRowCount(stmt, &rows); // get number of rows affected for UPDATE, INSERT, DELETE statements
        //printf("Number of rows affected: %ld \n",(long int)rows);
        NumberOfRowsAffected=(long int)rows;
        
        while (SQL_SUCCEEDED(ret= SQLFetch(stmt))) {
            //cout<<"Enter..."<<endl;
            SQLUSMALLINT i;
            ReaderItem ri;


            //printf("Row %d\n", row);
            row++;
            //ri.rowNum=row;
            // Loop through the columns
            for (i = 1; i <= columns; i++) {
                //SQLINTEGER indicator;
          SQLLEN indicator;
                char buf[512];
                //retrieve column data as a string
                ret = SQLGetData(stmt, i, SQL_C_CHAR,buf, sizeof(buf), &indicator);

                if (!(ret==SQL_SUCCESS||ret==SQL_SUCCESS_WITH_INFO))
                {
                  GetError("SQLGetData", stmt, SQL_HANDLE_STMT);

                return -1;
                break;
                }
                if (SQL_SUCCEEDED(ret)) {
                    // Handle null columns
                    if (indicator == SQL_NULL_DATA) strcpy(buf, "NULL");//strcpy(buf, "NULL");
                    //printf("  Column %u : %s\n", i, buf);

                    //ri.data.push_back(buf);
                    ri.appendData(buf);
                }
            }
          if (columns>=1) mList.push_back(ri);

          if(FullResult==false)  break;
        }   

        *result=mList;

        SQLFreeHandle(SQL_HANDLE_STMT, stmt); 
        return 0;
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        if (stmt != SQL_NULL_HSTMT) {  // Check if handle is valid
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
            stmt = SQL_NULL_HSTMT;   // Nullify after freeing
        }
        return -1;
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
        if (stmt != SQL_NULL_HSTMT) {  // Check if handle is valid
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
            stmt = SQL_NULL_HSTMT;   // Nullify after freeing
        }
        return -1;
    }
}

int odbc::SQLExecutNoneQuery(std::string statement)
{
    SQLSMALLINT columns; // number of columns
    SQLLEN rows; // number of rows
    int ret=-1;
    NumberOfRowsAffected=0;
    SQLHSTMT stmt = SQL_NULL_HSTMT;

  try
  {
      if (IsConnected()!=1)
      {
          Disconnect();
          if (Connect() != 0){return ret;}
      }

      ret = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt); // allocate statement handle
      if (!SQL_SUCCEEDED(ret)) {
        GetError("SQLAllocHandle", stmt, SQL_HANDLE_STMT);
        return ret;
      }

      SQLSetStmtAttr(stmt, SQL_QUERY_TIMEOUT, (SQLPOINTER)(intptr_t)queryTimeOut, SQL_IS_UINTEGER);

      ret = SQLExecDirect(stmt, (SQLCHAR*)statement.c_str(), SQL_NTS);
      if (!SQL_SUCCEEDED(ret)) {
        GetError("SQLExecDirect", stmt, SQL_HANDLE_STMT);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return ret;
      }    
      //SQLNumResultCols(stmt, &columns);//get numbers of columns
      ret = SQLRowCount(stmt, &rows); // get number of rows affected for UPDATE, INSERT, DELETE statements
      if (!SQL_SUCCEEDED(ret))
      {
          GetError("SQLRowCount", stmt, SQL_HANDLE_STMT);
      }
    // printf("Number of rows affected: %ld \n",(long int)rows);
      NumberOfRowsAffected=(long int)rows;
      ret=0;
      
      SQLFreeHandle(SQL_HANDLE_STMT, stmt);    
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        if (stmt != SQL_NULL_HSTMT) {  // Check if handle is valid
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
            stmt = SQL_NULL_HSTMT;   // Nullify after freeing
        }
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
        if (stmt != SQL_NULL_HSTMT) {  // Check if handle is valid
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
            stmt = SQL_NULL_HSTMT;   // Nullify after freeing
        }
    }
    return ret;
}

int odbc::Disconnect()
{
    SQLRETURN ret; //return status
    try
    {
      SQLDisconnect(dbc); // disconnect    
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return -1;
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return -1;
    }
    return 0;
}

std::vector<std::string> odbc::GetError(char const *fn,SQLHANDLE handle,SQLSMALLINT type)
{
    SQLINTEGER   i = 0;
    SQLINTEGER   native;
    SQLCHAR      state[7];
    SQLCHAR      text[256];
    SQLSMALLINT  len;
    SQLRETURN    ret;
    std::vector<std::string> emes;
	  std::string logMsg="";
    emes.push_back(fn);
    do { 
        ret = SQLGetDiagRec(type, handle, ++i, state, &native, text,sizeof(text), &len);
        if (SQL_SUCCEEDED(ret)) { 
          //printf("%s:%ld:%ld:%s\n", state, (long int)i, (long int)native, text); 

			
			  logMsg= std::string((char *)state ) + ":" +std::to_string(i)+ ":" ;
			  logMsg=logMsg +std::to_string(native) + ":" + std::string((char *)text);
        std::stringstream ss;
        ss << logMsg ;
        Logger::getInstance()->FnLog(ss.str(), "", "ODBC");

        emes.push_back((char*)text);
        }            
    } while( ret == SQL_SUCCESS );
    return emes;
}

// check connection status
// 1 : connected
// 0 : disconnected
// -1: error, connection doesn't exist
int odbc::IsConnected()
{
  SQLRETURN    ret;
  SQLUINTEGER	uIntVal; // Unsigned int attribute values


//  if (vPing(m_IP,pingTimeOut)==false) return -1;

  ret = SQLGetConnectAttr(dbc,SQL_ATTR_CONNECTION_DEAD,(SQLPOINTER)&uIntVal,(SQLINTEGER) sizeof(uIntVal),NULL);
  if (!SQL_SUCCEEDED(ret)) {
    GetError("SQLGetConnectAttr(SQL_ATTR_CONNECTION_DEAD)", dbc, SQL_HANDLE_DBC);
    return 0;
  }
  if (uIntVal==SQL_CD_FALSE) return 1; // The connection is open and available for statement processing.
  if (uIntVal==SQL_CD_TRUE) return 0; // The connection to the server has been lost.
  
  return 0;
}

char* odbc::To_CharArray(const std::string &Text)
{
            int size = Text.size();
            char *a = new char[size + 1];
            a[Text.size()] = 0;
            memcpy(a, Text.c_str(), size);
            return a;
}

unsigned char* odbc::To_UnsignCharArray(const std::string &Text)
{

            int size = Text.size();
            unsigned char *a = new unsigned char[size + 1];
            a[Text.size()] = 0;
            memcpy(a, Text.c_str(), size);
            return a;

}



//@input Params: sSeasonNo, iInOut, iZoneID, AllowedHolderType

//@Output Params:returnStatus (function return value), sSerialNo, iRateType, sFee, sAdminFee, sAppFee
//               iExpireDays, iRedeemTime, sRedeemAmt, dtValidTo, dtValidFrom

//note function return value is the output paras returnStatus
int odbc::isValidSeason(const std::string & sSeasonNo,
                          BYTE  iInOut,unsigned int iZoneID,std::string  &sSerialNo, short int &iRateType,
                          float &sFee, float &sAdminFee, float &sAppFee,
                          short int &iExpireDays, short int &iRedeemTime, float &sRedeemAmt,
                          std::string &AllowedHolderType,
                          std::string &dtValidTo,
                          std::string &dtValidFrom)
{


    int nRet=-1;
    bool debugFlag=false;
    SQLHSTMT stmt = SQL_NULL_HSTMT;


try
{


  char* connectionStr;
 	SQLCHAR outstr[1024];
	SQLSMALLINT outstrlen;


    CE_Time tmpDt,tmpDt2;

    char* strCallSP   = To_CharArray("{CALL sp_IsValidSeason (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)}");

    //1st ?: input Params
    //2nd ?: input Params
    //10th ?: input params
    //13th ?: input params

    //the rest ?: output params

    // SQLBindParameter output variables.
     int returnStatus=0;
    SQLCHAR tmpSerialNo[5]={0,};
    SQLCHAR tmpValidTo[24]={0,};
    SQLCHAR tmpValidFrom[24]={0,};

    //SQL len
    SQLLEN len0=0,lenNTS=SQL_NTS;
    SQLLEN lenNULL=SQL_NULL_DATA;

    //SQLCHAR NullParam[16]={'N','U','L','L',0};

    //clear all by by ref paras
    sSerialNo="";
    iRateType=0;
    sFee=0;
    sAdminFee=0;
    sAppFee=0;
    iExpireDays=0;
    iRedeemTime=0;
    sRedeemAmt=0;
    dtValidTo="";
    dtValidFrom="";
    //------------------------------

    if (IsConnected()!=1)
    {
        Disconnect();
        if (Connect()!=0 ){return -1;} 
    }


//    std::cout<< "#Input paras#"<<std::endl;
//
//    std::cout<< "Season No= " << sSeasonNo<< std::endl;
//
//    std::cout<< "iZoneID= " << iZoneID<< std::endl;
//
//    printf("iInOut: %d\n", iInOut);
//    std::cout<< "AllowedHolderType= " <<AllowedHolderType<<std::endl;
//
//    std::cout<< "---------------------------------"<<std::endl;




	nRet= SQLAllocHandle( SQL_HANDLE_STMT, dbc, &stmt);
    if (!SQL_SUCCEEDED(nRet))
    {
      GetError("SQLAllocHandle(SQL_HANDLE_STMT)",  stmt, SQL_HANDLE_STMT);
      //exit(1);
      //throw(20);
      //goto exit;
      return -1;
    }

    //(SQLPOINTER)(intptr_t)TimeOutVal
SQLSetStmtAttr(stmt, SQL_QUERY_TIMEOUT, (SQLPOINTER)(intptr_t) queryTimeOut, SQL_IS_UINTEGER);

    //m_log->WriteAndPrint("Set connect DB diff: "+
                         //std::to_string(m_log->getDiffLogTimeinS()));

//--------------------------------------------------------------------------------
// Bind input parameter1


    if (sSeasonNo=="")
    {
       nRet=  SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR,
                               SQL_VARCHAR, 16, 0, (void*)To_UnsignCharArray(sSeasonNo),
                               16, &lenNULL);
    }
    else
    {


           //sSeasonNo must convert to char* first
         nRet=  SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR,
                               SQL_VARCHAR, 16, 0, (void*)To_UnsignCharArray(sSeasonNo),
                               16, &lenNTS);

    }

    if (!SQL_SUCCEEDED(nRet))
    {
      GetError("SQLBindParameter(SQL_PARAM_INPUT1)", stmt, SQL_HANDLE_STMT);
      //goto exit;
      return -1;
    }
    //-----------------------------------------------------------------------------

// Bind input parameter2


    if (iInOut==SQL_NULL_DATA)
    {
       nRet=  SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_TINYINT,
                               SQL_TINYINT, len0, 0, &iInOut, len0, &lenNULL);

    }
    else
    {

       nRet=  SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_TINYINT,
                               SQL_TINYINT, len0, 0, &iInOut, len0, NULL); //NULL means cannot have null value pass in
    }

    if (!SQL_SUCCEEDED(nRet))
    {
      GetError("SQLBindParameter(SQL_PARAM_INPUT2)", stmt, SQL_HANDLE_STMT);
      //exit(1);
      //throw(20);
      //goto exit;
      return -1;
    }
    //----------------------------------


    // Bind the output parameter3 to variable RetStatus.
    nRet= SQLBindParameter(stmt, 3, SQL_PARAM_OUTPUT, SQL_C_TINYINT,
                               SQL_TINYINT, len0, 0, &returnStatus, len0, &lenNTS);

    if (!SQL_SUCCEEDED(nRet))
    {
      GetError("SQLBindParameter(SQL_PARAM_OUTPUT3)", stmt, SQL_HANDLE_STMT);
      //goto exit;
      return -1;
    }
    //-----------------------------------------------------------------------------



     // Bind the output parameter4 to variable SerialNo.
    nRet= SQLBindParameter(stmt, 4, SQL_PARAM_OUTPUT, SQL_C_CHAR,
                               SQL_VARCHAR, 5, 0, (SQLCHAR*)&tmpSerialNo, 5, &lenNTS);

    if (!SQL_SUCCEEDED(nRet))
    {
      GetError("SQLBindParameter(SQL_PARAM_OUTPUT4)", stmt, SQL_HANDLE_STMT);
      //goto exit;
      return -1;
    }
    //-----------------------------------------------------------------------------


    // Bind the output parameter5 to variable RateType.
    nRet= SQLBindParameter(stmt, 5, SQL_PARAM_OUTPUT, SQL_C_SSHORT,
                               SQL_SMALLINT, len0, 0, &iRateType, len0, &lenNTS);

    if (!SQL_SUCCEEDED(nRet))
    {
      GetError("SQLBindParameter(SQL_PARAM_OUTPUT5)", stmt, SQL_HANDLE_STMT);
      //goto exit;
      return -1;
    }
    //-----------------------------------------------------------------------------

   // Bind the output parameter6 to variable SeasonFee.
    nRet= SQLBindParameter(stmt, 6, SQL_PARAM_OUTPUT, SQL_C_FLOAT,
                               SQL_DECIMAL, 5, 2, &sFee, len0, &lenNTS);

    if (!SQL_SUCCEEDED(nRet))
    {
      GetError("SQLBindParameter(SQL_PARAM_OUTPUT6)", stmt, SQL_HANDLE_STMT);
      //goto exit;
      return -1;
    }
    //-----------------------------------------------------------------------------


    // Bind the output parameter7 to variable adminFee.
    nRet= SQLBindParameter(stmt, 7, SQL_PARAM_OUTPUT, SQL_C_FLOAT,
                               SQL_DECIMAL, 5, 2, &sAdminFee, len0, &lenNTS);

    if (!SQL_SUCCEEDED(nRet))
    {
      GetError("SQLBindParameter(SQL_PARAM_OUTPUT7)", stmt, SQL_HANDLE_STMT);
      //goto exit;
      return -1;
    }
    //-----------------------------------------------------------------------------


     // Bind the output parameter8 to variable AppFee.
    nRet= SQLBindParameter(stmt, 8, SQL_PARAM_OUTPUT, SQL_C_FLOAT,
                               SQL_DECIMAL, 5, 2, &sAppFee, len0, &lenNTS);

    if (!SQL_SUCCEEDED(nRet))
    {
      GetError("SQLBindParameter(SQL_PARAM_OUTPUT8)", stmt, SQL_HANDLE_STMT);
      //goto exit;
      return -1;
    }
    //-----------------------------------------------------------------------------


     // Bind the output parameter9 to variable ExpireDays
    nRet= SQLBindParameter(stmt, 9, SQL_PARAM_OUTPUT, SQL_C_SSHORT,
                               SQL_SMALLINT, len0, 0, &iExpireDays, len0, &lenNTS);

    if (!SQL_SUCCEEDED(nRet))
    {
      GetError("SQLBindParameter(SQL_PARAM_OUTPUT9)", stmt, SQL_HANDLE_STMT);
      //goto exit;
      return -1;
    }
    //-----------------------------------------------------------------------------


    // Bind the input parameter10 to variable ZoneID
    nRet= SQLBindParameter(stmt, 10, SQL_PARAM_INPUT, SQL_C_TINYINT,
                               SQL_TINYINT, len0, 0, &iZoneID, len0, &lenNTS);

    if (!SQL_SUCCEEDED(nRet))
    {
      GetError("SQLBindParameter(SQL_PARAM_OUTPUT10)", stmt, SQL_HANDLE_STMT);
      //goto exit;
      return -1;
    }
    //-----------------------------------------------------------------------------


     // Bind the output parameter11 to variable RedeemTime
    nRet= SQLBindParameter(stmt, 11, SQL_PARAM_OUTPUT, SQL_C_SSHORT,
                               SQL_SMALLINT, len0, 0, &iRedeemTime, len0, &lenNTS);

    if (!SQL_SUCCEEDED(nRet))
    {
      GetError("SQLBindParameter(SQL_PARAM_OUTPUT11)", stmt, SQL_HANDLE_STMT);
      //goto exit;
      return -1;
    }
    //-----------------------------------------------------------------------------


     // Bind the output parameter12 to variable RedeemAmt
    nRet= SQLBindParameter(stmt, 12, SQL_PARAM_OUTPUT, SQL_C_FLOAT,
                               SQL_DECIMAL, 5, 2, &sRedeemAmt, len0, &lenNTS);

    if (!SQL_SUCCEEDED(nRet))
    {
      GetError("SQLBindParameter(SQL_PARAM_OUTPUT12)", stmt, SQL_HANDLE_STMT);
      //goto exit;
      return -1;
    }
    //-----------------------------------------------------------------------------


    // Bind input parameter13

    if (AllowedHolderType=="")
    {
       nRet=  SQLBindParameter(stmt, 13, SQL_PARAM_INPUT, SQL_C_CHAR,
                               SQL_VARCHAR, 20, 0, (void*)To_UnsignCharArray(AllowedHolderType), 20, &lenNULL);

    }
    else
    {
         nRet=  SQLBindParameter(stmt, 13, SQL_PARAM_INPUT, SQL_C_CHAR,
                               SQL_VARCHAR, 20, 0, (void*)To_UnsignCharArray(AllowedHolderType), 20, &lenNTS);

    }

    if (!SQL_SUCCEEDED(nRet))
    {
      GetError("SQLBindParameter(SQL_PARAM_INPUT13)", stmt, SQL_HANDLE_STMT);
      //goto exit;
      return -1;
    }
    //-----------------------------------------------------------------------------



     // Bind the output parameter14 to variable ValidTo
        nRet= SQLBindParameter(stmt, 14, SQL_PARAM_OUTPUT, SQL_C_CHAR,
                               SQL_TIMESTAMP, sizeof(tmpValidTo), 0, (SQLCHAR*)&tmpValidTo,
                               sizeof(tmpValidTo), &lenNTS);


        if (!SQL_SUCCEEDED(nRet))
        {
          GetError("SQLBindParameter(SQL_PARAM_OUTPUT14)", stmt, SQL_HANDLE_STMT);
          //goto exit;
          return -1;
        }
        //-----------------------------------------------------------------------------


        // Bind the output parameter15 to variable ValidFrom
        nRet= SQLBindParameter(stmt, 15, SQL_PARAM_OUTPUT, SQL_C_CHAR,
                               SQL_TIMESTAMP, sizeof(tmpValidFrom), 0, (SQLCHAR*)&tmpValidFrom,
                               sizeof(tmpValidFrom), &lenNTS);


        if (!SQL_SUCCEEDED(nRet))
        {
          GetError("SQLBindParameter(SQL_PARAM_OUTPUT15)", stmt, SQL_HANDLE_STMT);
          //goto exit;
          return -1;
        }
        //-----------------------------------------------------------------------------
         
    nRet= SQLPrepare (stmt, (SQLCHAR*)strCallSP, SQL_NTS);

     if (!SQL_SUCCEEDED(nRet))
    {
      GetError("SQLPrepare(SQL_HANDLE_STMT)", stmt, SQL_HANDLE_STMT);
      //exit(1);
      //throw(20);
      //goto exit;
      return -1;
    }

 //-----------------------------------------------------------------------------
    //if (IsConnected()!=1) return -1;

    nRet=SQLExecute (stmt);

    if (!SQL_SUCCEEDED(nRet))
    {
      GetError("SQLExecute(SQL_HANDLE_STMT)", stmt, SQL_HANDLE_STMT);
      //exit(1);
      //throw(20);
      //goto exit;
      return -1;
    }

 //-----------------------------------------------------------------------------

    // Clear any result sets generated.

      
   while ( ( nRet= SQLMoreResults(stmt) ) != SQL_NO_DATA )
    ;

        //printf("Return Parameter  : %d\n", RetParam);
        //printf("Number of Records move to MovementRecord: %d\n", OutParam);

        //if (strlen((char*)tmpValidTo)==0) strcpy((char*)&tmpValidTo, (char*)&NullParam);
        //if (strlen((char*)tmpValidFrom)==0) strcpy((char*)&tmpValidFrom, (char*)&NullParam);
        //if (strlen((char*)tmpSerialNo)==0) strcpy((char*)&tmpSerialNo, (char*)&NullParam);

        //Empty string get from ValidTo output paras
        if (strlen((char*)tmpValidTo)==0)
        {
            time_t now;

            time(&now);

            tm * ltm=localtime(&now);

            //must add 1900 to year and add 1 to month
            //to get exact current date time
            tmpDt.SetTime(1900+ltm->tm_year,ltm->tm_mon+1,ltm->tm_mday,
            ltm->tm_hour,ltm->tm_min,ltm->tm_sec);

            //result need to - 4 years
            tmpDt.SetTime(tmpDt.Year()-4,tmpDt.Month(),tmpDt.Day(),
            tmpDt.Hour(),tmpDt.Minute(),tmpDt.Second());

            dtValidTo=tmpDt.DateTimeString();
        }
        else //add 1 day
        {


            tmpDt.SetTime(std::string((char*)tmpValidTo));
			      tmpDt.SetTime(tmpDt.GetUnixTimestamp()+86400);

            //tmpDt.SetTime(tmpDt.Year(),tmpDt.Month(),tmpDt.Day()+1,
           // tmpDt.Hour(),tmpDt.Minute(),tmpDt.Second());

            //std::cout<< "tmpDt= "<< tmpDt.DateTimeString()<<std::endl;
            dtValidTo=tmpDt.DateTimeString();
        }


        if (strlen((char*)tmpValidFrom)==0)
        {

            time_t now;

            time(&now);

            tm * ltm=localtime(&now);

            //must add 1900 to year and add 1 to month
            //to get exact current date
            //time fixed to 00:00:00
            tmpDt2.SetTime(1900+ltm->tm_year,ltm->tm_mon+1,ltm->tm_mday,
            0,0,0);

             dtValidFrom= tmpDt2.DateTimeString();

        }
        else
        {

            dtValidFrom= std::string((char*)tmpValidFrom);
        }

        /*std::cout<< "#Output paras#"<<std::endl;
        std::cout<< "SerialNo= " << tmpSerialNo<< std::endl;
        std::cout<< "returnStatus= " << returnStatus <<std::endl;
        std::cout<< "iRateType= " << iRateType <<std::endl;
        std::cout<< "iExpireDays= " << iExpireDays<<std::endl;
        std::cout<< "sRedeemAmt= " << sRedeemAmt <<std::endl;


        printf("sFee= %.2f\n", sFee);
        printf("sAdminFee= %.2f\n", sAdminFee);


        printf("sRedeemAmt= %.2f\n", sRedeemAmt);*/


         
        nRet=returnStatus;



}
/*catch(const std::exception &e){
    m_log->WriteAndPrint("SPisValid Season: exception");
    }*/
catch (const std::exception& e)
{
    std::stringstream ss;
    ss << __func__ << ", Exception: " << e.what();
    Logger::getInstance()->FnLogExceptionError(ss.str());
    nRet=-1;
}
catch (...)
{
    std::stringstream ss;
    ss << __func__ << ", Exception: Unknown Exception";
    Logger::getInstance()->FnLogExceptionError(ss.str());
    nRet=-1;
}




    return nRet;

}

 bool odbc::vPing(string IP,float timeOut)
 {

 	std::string details;

  return PingWithTimeOut(std::string(IP),timeOut, details);
 }
