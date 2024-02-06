
#include <iostream>
#include <string>
#include <memory>
#include "operation.h"
#include "ini_parser.h"
#include "structuredata.h"
#include "db.h"
#include "log.h"
#include "udp.h"

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

void operation::OperationInit(io_service& ioService)
{
    gtStation.iSID = std::stoi(IniParser::getInstance()->FnGetStationID());
    m_db=new db("DSN={MariaDB-server};DRIVER={MariaDB ODBC 3.0 Driver};SERVER=127.0.0.1;PORT=3306;DATABASE=linux_pbs;UID=linuxpbs;PWD=SJ2001;","DSN=mssqlserver;DATABASE=RF;UID=sa;PWD=yzhh2007","192.168.2.47",10,2,2,2);
    m_db->synccentraltime();
    //m_db->loadstationsetup();
    //m_db->downloadledmessage();
    //m_db->downloadvehicletype();
    //m_db->downloadparameter();
    //m_db->loadParam();
    //m_db->loadvehicletype();
    //m_db->loadmessage();
    // create new UDP
    try {
        m_udp = new udpclient(ioService, "192.168.2.47", 2001,2001);
        m_udp->startsend("Create New UDP");
    }
    catch (const std::exception& e) {
        // Handle exceptions (e.g., std::bad_alloc) here
        std::stringstream dbss;
	    dbss  << "Exception: " << e.what();
        Logger::getInstance()->FnLog(dbss.str(), "", "Opr");
        // You might want to do additional cleanup here, depending on your application's requirements
    }

}

void operation::LoopACome()
{
    std::stringstream dbss;
	dbss << "Loop A Come";
    Logger::getInstance()->FnLog(dbss.str(), "", "Opr");


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
void operation::dooperation(string sIU)
{
    std::stringstream dbss;
	dbss << "received IU: " << sIU;
    Logger::getInstance()->FnLog(dbss.str(), "", "Opr");

    if (gtStation.iType = tientry) {

    } 


}