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


     void OperationInit(io_service& ioService);
     void LoopACome();
     void LoopAGone();
     void LoopCCome();
     void LoopCGone();
     void dooperation(string sIU);

     /**
     * Singleton LCD should not be cloneable.
     */
    operation (operation&) = delete;

    /**
     * Singleton LCD should not be assignable.
     */
    void operator=(const operation&) = delete;

    
private:
    
    static operation* operation_;
    operation();
};
