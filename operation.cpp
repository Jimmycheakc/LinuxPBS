
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
   
    gtStation.iSID = std::stoi(IniParser::getInstance()->FnGetStationID());
    //
    Setdefaultparameter();
    //
    int iRet = 0;
    m_db = db::getInstance();

    iRet = m_db->connectlocaldb("DSN={MariaDB-server};DRIVER={MariaDB ODBC 3.0 Driver};SERVER=127.0.0.1;PORT=3306;DATABASE=linux_pbs;UID=linuxpbs;PWD=SJ2001;",2,2,2);
    if (iRet != 1) {
        writelog ("Unable to connect local DB.","opr");
        exit(0); 
    }
    iRet = m_db->loadParam();
    if (iRet != 0) {
        writelog ("Unable to load Parameter from  Local DB","opr");
        exit(0);
    }
    string m_connstring;
    m_connstring = "DSN=mssqlserver;DATABASE=" + tParas.gsCentralDBName + ";UID=sa;PWD=yzhh2007";
    m_db->connectcentraldb(m_connstring,tParas.gsCentralDBServer,2,2,2);
    // sync central time
    m_db->synccentraltime();
    //
    m_db->loadstationsetup();
    m_db->loadmessage();
    m_db->loadParam();
    m_db->loadvehicletype();
    //
    tProcess.gsBroadCastIP = getIPAddress();

    try {
        m_udp = new udpclient(ioContext, tProcess.gsBroadCastIP, 2001,2001);
        m_udp->socket_.set_option(socket_base::broadcast(true));
    }
    catch (const std::exception& e) {
        std::string cppString(e.what());
        writelog ("Exception: "+ cppString,"opr");
    }
   
    Initdevice(ioContext);
    
    ShowLEDMsg("EPS in OPeration^have a nice day!","EPS in OPeration^have a nice day!");

    iRet= m_db->FnGetVehicleType("001");

    writelog ("Return Vehicle Type = "+ std::to_string(iRet), "opr");

    m_db->moveOfflineTransToCentral();
   
}

void operation::LoopACome()
{
    writelog ("Loop A Come.","opr");

    //if (tParas.giFullAction = iNoAct or tProcess.gbcarparkfull = 0)
    //{
        Clearme();
        Antenna::getInstance()->FnAntennaSendReadIUCmd();
        LCSCReader::getInstance()->FnSendGetCardIDCmd();
    //}

}

void operation::LoopAGone()
{
    writelog ("Loop A End.","opr");

}
void operation::LoopCCome()
{
    writelog ("Loop C Come.","opr");


}
void operation::LoopCGone()
{
    writelog ("Loop C End.","opr");


}
void operation::VehicleCome(string sNo)
{

     writelog ("Received IU: "+sNo,"opr");

    if (sNo.length()==10 and sNo == tProcess.gsDefaultIU) {

        SendMsg2Server ("90",sNo+",,,,,Default IU");
        writelog ("Default IU: "+sNo,"opr");
        //---------
        LCSCReader::getInstance()->FnSendGetCardIDCmd();
        return;
    } 
    if (gtStation.iType == tientry) {
        PBSEntry (sNo);
    } 
    else{
        PBSExit(sNo);
    }
    return;
}

void operation::Openbarrier()
{
    writelog ("Station Open Barrier.","opr");

    if (tParas.gsBarrierPulse == 0){tParas.gsBarrierPulse = 500;}

    DIO::getInstance()->FnSetOpenBarrier(1);
    //
    std::this_thread::sleep_for(std::chrono::milliseconds(tParas.gsBarrierPulse));
    //
    DIO::getInstance()->FnSetOpenBarrier(0);
}

void operation::Clearme()
{
    if (gtStation.iType == tientry)
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
    else
    {

    }

}

void operation::Initdevice(io_context& ioContext)
{
    LCD::getInstance()->FnLCDInit();
    Antenna::getInstance()->FnAntennaInit(ioContext, 19200, "/dev/ttyCH9344USB5");
    LCSCReader::getInstance()->FnLCSCReaderInit(ioContext, 115200, "/dev/ttyCH9344USB4");
  //LED401 giCommportLED401
  //LED226 giCommPortLED
  //LCD
    LCD::getInstance()->FnLCDInit();
    GPIOManager::getInstance()->FnGPIOInit();
    DIO::getInstance()-> FnDIOInit();
}

void operation::ShowLEDMsg(string LEDMsg, string LCDMsg)
{
    
    writelog ("LED Message:" + LEDMsg,"opr");
    //-------
    writelog ("LCD Message:" + LCDMsg,"opr");

    char* sLCDMsg = const_cast<char*>(LCDMsg.data());
    LCD::getInstance()->FnLCDDisplayScreen(sLCDMsg);

}

void operation::PBSEntry(string sIU)
{
    int iRet;

    if(sIU.length()==10) 
	    tEntry.iTransType= db::getInstance()->FnGetVehicleType(sIU.substr(0,3));
	else {
        tEntry.iTransType=GetVTypeFromLoop();
    }
    
    iRet = CheckSeason(sIU,1);

   

    if (tProcess.gbcarparkfull ==1 and tEntry.iTransType != 9)
    {

    } 
    SaveEntry();
    Openbarrier();

}

void operation::PBSExit(string sIU)
{
    int iRet;
    iRet = CheckSeason(sIU,2);

}

void operation:: Setdefaultparameter()

{
    tProcess.gsDefaultIU = "1096000001";
    //clear error msg
     for (int i= 0; i< Errsize; ++i){
        tPBSError[i].ErrNo = 0;
        tPBSError[i].ErrCode =0;
	    tPBSError[i].ErrMsg = "";
    }

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

void operation:: Sendmystatus()
{
  //EPS error index:  0=antenna,     1=printer,   2=DB, 3=Reader, 4=UPOS
  //5=Param error, 6=DIO,7=Loop A hang,8=CHU, 9=ups, 10=NCSC, 11= LCSC
  //12= station door status, 13 = barrier door status, 14= TGD controll status
  //15 = TGD sensor status 16=Arm drop status,17=barrier status,18=ticket status d DateTime
    CE_Time dt;
	string str="";
    //-----
    for (int i= 0; i< Errsize; ++i){
        str += std::to_string(tPBSError[i].ErrNo) + ",";
    }
	str+="0;0,"+dt.DateTimeNumberOnlyString()+",";
	SendMsg2Server("00",str);
	
}

void operation::SendMsg2Server(string cmdcode,string dstr)
{
	string str="["+ gtStation.sName+"|"+to_string(gtStation.iSID)+"|"+cmdcode+"|";
	str+=dstr+"|]";
	m_udp->startsend(str);
    //----
    writelog ("Message to PMS: " + str,"opr");
}

int operation::CheckSeason(string sIU,int iInOut)
{
   int iRet;
   iRet = db::getInstance()->isvalidseason(sIU,iInOut,gtStation.iZoneID);
   //showLED message
   return iRet;
}

void operation::writelog(string sMsg, string soption)
{
    std::stringstream dbss;
	dbss << sMsg;
    Logger::getInstance()->FnLog(dbss.str(), "", soption);

}

void operation::HandlePBSError(EPSError iEPSErr, int iErrCode=0)
{

    string sErrMsg = "";
    string sCmd = "";
    
    switch(iEPSErr){
        case AntennaNoError:
            if (tPBSError[iAntenna].ErrNo < 0) {
                sCmd = "03";
                sErrMsg = "Antenna OK";
            }
            tPBSError[iAntenna].ErrNo = 0;
            break;
        case AntennaError:
            tPBSError[iAntenna].ErrNo = -1;
            tPBSError[iAntenna].ErrCode = iErrCode;
            tPBSError[iAntenna].ErrMsg = "Antenna Error: " + iErrCode;
            sErrMsg = tPBSError[iAntenna].ErrMsg;
            sCmd = "03";
            break;
        case AntennaPowerOnOff:
            if (iErrCode = 1) {
                tPBSError[iAntenna].ErrNo = 0;
                tPBSError[iAntenna].ErrCode = 1;
                tPBSError[iAntenna].ErrMsg = "Antenna: Power ON";
            }
            else{
                tPBSError[iAntenna].ErrNo = -2;
                tPBSError[iAntenna].ErrCode = 0;
                tPBSError[iAntenna].ErrMsg = "Antenna Error: Power OFF";
            
            }
            sErrMsg = tPBSError[iAntenna].ErrMsg;
            sCmd = "03";
            break;
        case PrinterNoError:      
            if (tPBSError[1].ErrNo < 0){
                sCmd = "04";
                sErrMsg = "Printer OK";
            }
            tPBSError[1].ErrNo = 0;
            break;
        case PrinterError:
            tPBSError[1].ErrNo = -1;
            tPBSError[1].ErrMsg = "Printer Error";
            sCmd = "04";
            sErrMsg = tPBSError[1].ErrMsg;
            break;
        case PrinterNoPaper:
            tPBSError[1].ErrNo = -2;
            tPBSError[1].ErrMsg = "Printer Paper Low";
            sCmd = "04";
            sErrMsg = tPBSError[1].ErrMsg;
            break;
        case DBNoError:
            tPBSError[2].ErrNo = 0;
            break;
        case DBFailed:
            tPBSError[2].ErrNo = -1;
            break;
        case DBUpdateFail:    
            tPBSError[2].ErrNo = -2;
            break;
        case UPOSNoError:
            if (tPBSError[4].ErrNo < 0){
                sCmd = "06";
                sErrMsg = "UPOS OK";
            }
            tPBSError[4].ErrNo = 0;
            break;
        case UPOSError:
            tPBSError[4].ErrNo = -1;
            tPBSError[4].ErrMsg = "UPOS Error";
            sCmd = "06";
            sErrMsg = tPBSError[4].ErrMsg;
            break;
        case TariffError:
            tPBSError[5].ErrNo = -1;     
            sCmd = "08";
            sErrMsg = "5Tariff Error";
            break;
        case TariffOk:
            tPBSError[5].ErrNo = 0 ;    
            sCmd = "08";
            sErrMsg = "5Tariff OK";
            break;
        case HolidayError:
            tPBSError[5].ErrNo = -2;
            sCmd = "08";
            sErrMsg = "5No Holiday Set";
            break;
        case HolidayOk:
            tPBSError[5].ErrNo = 0;    
            sCmd = "08";
            sErrMsg = "5Holiday OK";
            break;
        case DIOError:
            tPBSError[6].ErrNo = -1;
            sCmd = "08";
            sErrMsg = "6DIO Error";
            break;
        case DIOOk:
            tPBSError[6].ErrNo = 0;
            sCmd = "08";
            sErrMsg = "6DIO OK";
            break;
        case LoopAHang:
            tPBSError[7].ErrNo = -1;
            sCmd = "08";
            sErrMsg = "7Loop A Hang";
            break;
        case LoopAOk:
            tPBSError[7].ErrNo = 0;
            sCmd = "08";
            sErrMsg = "7Loop A OK";
            break;
        case LCSCNoError:
            if (tPBSError[10].ErrNo < 0){
                sCmd = "70";
                sErrMsg = "LCSC OK";
            }
            tPBSError[10].ErrNo = 0;
            break;
        case LCSCError:
            tPBSError[10].ErrNo = -1;
            tPBSError[10].ErrMsg = "LCSC Error";
            sCmd = "70";
            sErrMsg = tPBSError[10].ErrMsg;
            break;
        case SDoorError:
           sCmd = "71";
            sErrMsg = tPBSError[11].ErrMsg;
            break;
        case BDoorError:
            sCmd = "72";
            sErrMsg = tPBSError[12].ErrMsg;
            break;
        default:
            break;
    }
    
    if (sErrMsg != "" ){
        writelog (sErrMsg, "PBS");
        SendMsg2Server(sCmd, sErrMsg);    
    }

}

int operation::GetVTypeFromLoop()
{
    
   // 0: car  2: M/C  1: Lorry 
    
    return 0;

}

void operation::SaveEntry()
{
    
    if (tEntry.sIUTKNo== "") return;

    SendMsg2Server("90","Entry OK");

    db::getInstance()->insertentrytrans(tEntry);


}

void operation::ShowTotalLots(string totallots)
{

}


