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
    struct  tPBSError_struct tPBSError[13];
    std::vector<struct tVType_Struct> tVType;


     void OperationInit(io_context& ioContext);
     void LoopACome();
     void LoopAGone();
     void LoopCCome();
     void LoopCGone();
     void IUcome(string sIU);
     void Initdevice(io_context& ioContext);
     void ShowLEDMsg(string LEDMsg, string LCDMsg);
     void PBSEntry(string sIU);
     void PBSExit(string sIU);
     void Setdefaultparameter();
     string getIPAddress(); 


     void Openbarrier();

     void Clearentrytrans();

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
    }

};
