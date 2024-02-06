
#include <iostream>
#include <string>
#include <memory>
#include <chrono>
#include <thread>
#include <cstdio>
#include "gpio.h"
#include "operation.h"
#include "ini_parser.h"
#include "structuredata.h"
#include "db.h"
#include "lcd.h"
#include "log.h"
#include "udp.h"
#include "antenna.h"
#include "lcsc.h"
#include "dio.h"

operation* operation::operation_ = nullptr;

operation::operation()
{

}

operation* operation::getInstance()
{
    if (operation_ == nullptr)
    {
        operation_ = new operation();
    }
    return operation_;
}

void operation::OperationInit(io_context& ioContext)
{
    std::stringstream dbss;

    gtStation.iSID = std::stoi(IniParser::getInstance()->FnGetStationID());
    //
    int iRet = 0;
    m_db = db::getInstance();

    iRet = m_db->connectlocaldb("DSN={MariaDB-server};DRIVER={MariaDB ODBC 3.0 Driver};SERVER=127.0.0.1;PORT=3306;DATABASE=linux_pbs;UID=linuxpbs;PWD=SJ2001;",2,2,2);
    if (iRet != 1) {
	    dbss << "Unable to connect Local Db";
        Logger::getInstance()->FnLog(dbss.str(), "", "Opr");
        exit(0); 
    }
    iRet = m_db->loadParam();
    if (iRet != 0) {
        dbss << "Unable to load Parameter from  Local Db";
        Logger::getInstance()->FnLog(dbss.str(), "", "Opr");
        exit(0);
    }
    string m_connstring;
    m_connstring = "DSN=mssqlserver;DATABASE=" + tParas.gsCentralDBName + ";UID=sa;PWD=yzhh2007";
    m_db->connectcentraldb(m_connstring,tParas.gsCentralDBServer,2,2,2);
    // sync central time
    m_db->synccentraltime();
    //
    m_db->downloadledmessage();
    m_db->loadstationsetup();
    //m_db->loadmessage();
    m_db->loadParam();

    tParas.gsBroadCastIP = getIPAddress();

    try {
        m_udp = new udpclient(ioContext, tParas.gsBroadCastIP, 2001,2001);
        m_udp->socket_.set_option(socket_base::broadcast(true));
        m_udp->startsend("Create New UDP");
    }
    catch (const std::exception& e) {
        std::stringstream dbss;
	    dbss  << "Exception: " << e.what();
        Logger::getInstance()->FnLog(dbss.str(), "", "Opr");
    }

    Initdevice(ioContext);
    Setdefaultparameter();

}

void operation::LoopACome()
{
    std::stringstream dbss;
	dbss << "Loop A Come";
    Logger::getInstance()->FnLog(dbss.str(), "", "Opr");

    
    if (gtStation.iType == tientry) 
    {
         Clearentrytrans();
         Antenna::getInstance()->FnAntennaSendReadIUCmd();
         LCSCReader::getInstance()->FnSendGetCardIDCmd();
    }


}

void operation::LoopAGone()
{
    std::stringstream dbss;
	dbss << "Loop A End";
    Logger::getInstance()->FnLog(dbss.str(), "", "Opr");

}
void operation::LoopCCome()
{
    std::stringstream dbss;
	dbss << "Loop C Come";
    Logger::getInstance()->FnLog(dbss.str(), "", "Opr");


}
void operation::LoopCGone()
{
    std::stringstream dbss;
	dbss << "Loop C End";
    Logger::getInstance()->FnLog(dbss.str(), "", "Opr");


}
void operation::IUcome(string sIU)
{
    std::stringstream dbss;
	dbss << "received IU: " << sIU;
    Logger::getInstance()->FnLog(dbss.str(), "", "Opr");


    if (gtStation.iType == tientry) {


    } 

}

void operation::Openbarrier()
{
    std::stringstream dbss;
	dbss << "open barrier";
    Logger::getInstance()->FnLog(dbss.str(), "", "Opr");

    if (tParas.gsBarrierPulse == 0){tParas.gsBarrierPulse = 500;}

    DIO::getInstance()->FnSetOpenBarrier(1);
    //
    std::this_thread::sleep_for(std::chrono::milliseconds(tParas.gsBarrierPulse));
    //
    DIO::getInstance()->FnSetOpenBarrier(0);
}

void operation::Clearentrytrans()
{
    tEntry.esid ="";
	tEntry.sSerialNo = "";     //for ticket
	tEntry.sIUTKNo = "";
	tEntry.sEntryTime = "";
	tEntry.iTransType = 0;
	tEntry.iRateType = 0;
	tEntry.iStatus = 0;  //enter or reverse

	tEntry.sCardNo = "";
	tEntry.sFee = 0;
	tEntry.sPaidAmt = 0;
	tEntry.sGSTAmt = 0;
	tEntry.sReceiptNo = "";
	tEntry.iCardType=0;
    tEntry.sOweAmt= 0;

	tEntry.sLPN[0] = "";
    tEntry.sLPN[1] = "";
	tEntry.iVehcileType = 0;

}

void operation::Initdevice(io_context& ioContext)
{
    LCD::getInstance()->FnLCDInit();
    Antenna::getInstance()->FnAntennaInit(ioContext, 19200, "/dev/ttyCH9344USB5");
    LCSCReader::getInstance()->FnLCSCReaderInit(ioContext, 115200, "/dev/ttyCH9344USB4");
  //LED401 giCommportLED401
  //LED226 giCommPortLED
  //LCD
    GPIOManager::getInstance()->FnGPIOInit();
    DIO::getInstance()-> FnDIOInit();
    Openbarrier();
}

void operation::ShowLEDMsg(string LEDMsg, string LCDMsg)
{
    /// LED, LCD

}
void operation::PBSEntry(string sIU)
{

}

void operation::PBSExit(string sIU)
{

}

void operation:: Setdefaultparameter()

{
    tParas.gsDefaultIU = "1096000001";

}

string operation:: getIPAddress() 

{
    FILE* pipe = popen("ifconfig", "r");
    if (!pipe) {
        std::cerr << "Error in popen\n";
        return "";
    }

    char buffer[128];
    std::string result = "";
    int iRet = 0;

    // Read the output of the 'ifconfig' command
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result = buffer;
         if (iRet == 1) {
             size_t pos = result.find("Bcast:");
             result = result.substr(pos + 6);
             result.erase(result.find(' '));
            break;
         }

         if (result.find("eth0") != std::string::npos) {
             iRet = 1;
         }
    }
    
    pclose(pipe);

    // Output the result
    return result;
}