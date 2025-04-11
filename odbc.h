
#pragma once

#include <stdio.h>
#include <sql.h>
#include <sqlext.h>
#include <string.h>
#include <string>
#include <vector>
#include "ce_time.h"
#include "ping.h"

class ReaderItem
{
    public:
        ReaderItem();
        virtual ~ReaderItem();
        unsigned long getDataSize();
        void appendData(std::string s);
        std::string GetDataItem(unsigned long index);
    protected:

    private:
        std::vector<std::string> data;

};


class odbc {
public:
  odbc(unsigned int ConnTO,unsigned int queryTO, float pingTO,
           std::string IP, std::string conn);
  ~odbc();


  int SQLSelect(std::string statement, std::vector<ReaderItem> *result,bool FullResult);
  int SQLExecutNoneQuery(std::string statement);
  int Connect();

  int Disconnect();
  int IsConnected();
  long NumberOfRowsAffected;


int isValidSeason(const std::string & sSeasonNo,
                  BYTE  iInOut,unsigned int iZoneID,std::string  &sSerialNo, short int &iRateType,
                  float &sFee, float &sAdminFee, float &sAppFee,
                  short int &iExpireDays, short int &iRedeemTime, float &sRedeemAmt,
                  std::string &AllowedHolderType,
                  std::string &dtValidTo,
                  std::string &dtValidFrom);
private:
 
  std::string m_IP;

  std::string m_connString;

  unsigned int ConnTimeOutVal;
  unsigned int queryTimeOut;
  float pingTimeOut;
  SQLHENV env; //environment handle
  SQLHDBC dbc; //connection handle
  //SQLHSTMT stmt; // statement handle
  std::vector<std::string> GetError(char const *fn,SQLHANDLE handle,SQLSMALLINT type);
  char* To_CharArray(const std::string &Text);
  unsigned char* To_UnsignCharArray(const std::string &Text);
  bool vPing(string IP,float timeOut);

};


