#pragma once

#include <stdio.h>
#include <string>
#include <sstream>
#include <iostream>
#include "structuredata.h"
#include "db.h"
#include "udp.h"

class operation
{
public:
    static operation* getInstance();
    db *m_db;
    udpclient *m_udp;
    struct  tstation_struct gtStation;
    struct  tEntryTrans_Struct tEntry; 
    struct  tProcess_Struct tProcess;
    struct  tParas_Struct tParas;
	struct  tMsg_Struct tMsg;
    static const int Errsize = 17;
    struct  tPBSError_struct tPBSError[20];
    struct  tseason_struct tSeason;
    std::vector<struct tVType_Struct> tVType;


     void OperationInit(io_context& ioContext);
     void LoopACome();
     void LoopAGone();
     void LoopCCome();
     void LoopCGone();
     void VehicleCome(string sNo);
     void Initdevice(io_context& ioContext);
     void ShowLEDMsg(string LEDMsg, string LCDMsg);
     void PBSEntry(string sIU);
     void PBSExit(string sIU);
     void Setdefaultparameter();
     string getIPAddress(); 
     void Sendmystatus();
     void SendMsg2Server(string cmdcode,string dstr);
     int  CheckSeason(string sIU,int iInOut);
     void writelog(string sMsg, string soption);
     void HandlePBSError(EPSError iEPSErr, int iErrCode);
     int  GetVTypeFromLoop();
     void SaveEntry();
     void ShowTotalLots(string totallots);


     void Openbarrier();

     void Clearme();

     /**
     * Singleton opertation should not be cloneable.
     */
    operation (operation&) = delete;

    /**
     * Singleton operation should not be assignable.
     */
    void operator=(const operation&) = delete;

    
private:
    
    static operation* operation_;
    operation();
    ~operation() {
        delete m_udp;
        delete m_db;
        delete operation_;
    };
    std::string getSerialPort(const std::string& key);

};
