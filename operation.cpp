
#include <iostream>
#include <string>
#include <memory>
#include <chrono>
#include <thread>
#include <cstdio>
#include "common.h"
#include "gpio.h"
#include "operation.h"
#include "ini_parser.h"
#include "structuredata.h"
#include "db.h"
#include "led.h"
#include "lcd.h"
#include "log.h"
#include "udp.h"
#include "antenna.h"
#include "lcsc.h"
#include "dio.h"

operation* operation::operation_ = nullptr;

operation::operation()
{
    isOperationInitialized_.store(false);
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
    tParas.gsCentralDBName = IniParser::getInstance()->FnGetCentralDBName();
    tParas.gsCentralDBServer = IniParser::getInstance()->FnGetCentralDBServer();
    //
    Setdefaultparameter();
    //
    writelog ("Exception: 1","OPR");
    tProcess.gsBroadCastIP = getIPAddress();
    try {
        m_udp = new udpclient(ioContext, tProcess.gsBroadCastIP, 2001,2001);
        m_udp->socket_.set_option(socket_base::broadcast(true));
    }
    catch (const std::exception& e) {
        std::string cppString(e.what());
        writelog ("Exception: "+ cppString,"OPR");
    }
    writelog ("Exception: "+tParas.gsCentralDBServer ,"OPR");
    try {
        m_Monitorudp = new udpclient(ioContext, tParas.gsCentralDBServer, 2008,2008);
    }
    catch (const std::exception& e) {
        std::string cppString1(e.what());
        writelog ("Exception: "+ cppString1,"OPR");
    }
    //
    int iRet = 0;
    m_db = db::getInstance();

    iRet = m_db->connectlocaldb("DSN={MariaDB-server};DRIVER={MariaDB ODBC 3.0 Driver};SERVER=127.0.0.1;PORT=3306;DATABASE=linux_pbs;UID=linuxpbs;PWD=SJ2001;",2,2,2);
    if (iRet != 1) {
        writelog ("Unable to connect local DB.","OPR");
        exit(0); 
    }
    string m_connstring;
    writelog ("Connect Central DB:"+tParas.gsCentralDBName,"OPR");
    for (int i = 0 ; i < 5; ++i ) {
        m_connstring = "DSN=mssqlserver;DATABASE=" + tParas.gsCentralDBName + ";UID=sa;PWD=yzhh2007";
        iRet = m_db->connectcentraldb(m_connstring,tParas.gsCentralDBServer,2,2,2);
        if (iRet == 1) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    // sync central time
    if (iRet == 1) {
        m_db->synccentraltime();
        m_db->downloadseason();
    }
    //
    m_db->loadstationsetup();
    m_db->loadmessage();
    m_db->loadParam();
    m_db->loadvehicletype();
    //
    Initdevice(ioContext);

    isOperationInitialized_.store(true);
    
    ShowLEDMsg(tMsg.MsgEntry_DefaultLED[0], tMsg.MsgEntry_DefaultLED[1]);

   // PBSEntry ("1122944019");

}

bool operation::FnIsOperationInitialized() const
{
    return isOperationInitialized_.load();
}

void operation::LoopACome()
{
    writelog ("Loop A Come.","OPR");
    ShowLEDMsg(tMsg.MsgEntry_LoopA[0], tMsg.MsgEntry_LoopA[1]);
    Clearme();
    Antenna::getInstance()->FnAntennaSendReadIUCmd();
    LCSCReader::getInstance()->FnSendGetCardIDCmd();

}

void operation::LoopAGone()
{
    writelog ("Loop A End.","OPR");

}
void operation::LoopCCome()
{
    writelog ("Loop C Come.","OPR");


}
void operation::LoopCGone()
{
    writelog ("Loop C End.","OPR");


}
void operation::VehicleCome(string sNo)
{

     writelog ("Received IU: "+sNo,"OPR");

    if (sNo.length()==10 and sNo == tProcess.gsDefaultIU) {

        SendMsg2Server ("90",sNo+",,,,,Default IU");
        writelog ("Default IU: "+sNo,"OPR");
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
    writelog ("Station Open Barrier.","OPR");

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

std::string operation::getSerialPort(const std::string& key)
{
    std::map<std::string, std::string> serial_port_map = 
    {
        {"1", "/dev/ttyCH9344USB7"}, // J3
        {"2", "/dev/ttyCH9344USB6"}, // J4
        {"3", "/dev/ttyCH9344USB5"}, // J5
        {"4", "/dev/ttyCH9344USB4"}, // J6
        {"5", "/dev/ttyCH9344USB3"}, // J7
        {"6", "/dev/ttyCH9344USB2"}, // J8
        {"7", "/dev/ttyCH9344USB1"}, // J9
        {"8", "/dev/ttyCH9344USB0"}  // J10
    };

    auto it = serial_port_map.find(key);

    if (it != serial_port_map.end())
    {
        return it->second;
    }
    else
    {
        return "";
    }
}

void operation::Initdevice(io_context& ioContext)
{
    LCD::getInstance()->FnLCDInit();

    if (tParas.giCommPortAntenna > 0)
    {
        Antenna::getInstance()->FnAntennaInit(ioContext, 19200, getSerialPort(std::to_string(tParas.giCommPortAntenna)));
    }

    if (tParas.giCommPortLCSC > 0)
    {
        LCSCReader::getInstance()->FnLCSCReaderInit(ioContext, 115200, getSerialPort(std::to_string(tParas.giCommPortLCSC)));
    }

    if (tParas.giCommPortLED > 0)
    {
        int max_char_per_row = 0;

        if (tParas.giLEDMaxChar < 20)
        {
            max_char_per_row =  LED::LED216_MAX_CHAR_PER_ROW;
        }
        else
        {
            max_char_per_row =  LED::LED226_MAX_CHAR_PER_ROW;
        }
        LEDManager::getInstance()->createLED(ioContext, 9600, getSerialPort(std::to_string(tParas.giCommPortLED)), max_char_per_row);
    }

    if (tParas.giCommportLED401 > 0)
    {
        LEDManager::getInstance()->createLED(ioContext, 9600, getSerialPort(std::to_string(tParas.giCommportLED401)), LED::LED614_MAX_CHAR_PER_ROW);
    }

    LCD::getInstance()->FnLCDInit();
    GPIOManager::getInstance()->FnGPIOInit();
    DIO::getInstance()->FnDIOInit();
    DIO::getInstance()->FnStartDIOMonitoring();
}

void operation::ShowLEDMsg(string LEDMsg, string LCDMsg)
{
    
    writelog ("LED Message:" + LEDMsg,"OPR");
    //-------
    writelog ("LCD Message:" + LCDMsg,"OPR");

    char* sLCDMsg = const_cast<char*>(LCDMsg.data());
    LCD::getInstance()->FnLCDDisplayScreen(sLCDMsg);

    if (LEDManager::getInstance()->getLED(getSerialPort(std::to_string(tParas.giCommPortLED))) != nullptr)
    {
        LEDManager::getInstance()->getLED(getSerialPort(std::to_string(tParas.giCommPortLED)))->FnLEDSendLEDMsg("***", LEDMsg, LED::Alignment::CENTER);
    }
}

void operation::PBSEntry(string sIU)
{
    int iRet;
    int iEntryOK=0;
    CE_Time dt;
	string dtStr=dt.DateString()+" "+dt.TimeWithMsString();

    tEntry.sIUTKNo = sIU;
    tEntry.sEntryTime=dtStr;
    if (sIU != "")
    {
        //check blacklist
        iRet = m_db->IsBlackListIU(sIU);
        if (iRet >= 0){
            ShowLEDMsg(tMsg.MsgBlackList[0], tMsg.MsgBlackList[1]);
            SendMsg2Server("90",sIU+",,,,,Blacklist IU");
            if(iRet ==0) return;
        }
        //check block 
        string gsBlockIUPrefix = IniParser::getInstance()->FnGetBlockIUPrefix();
        writelog ("blockIUprfix =" +gsBlockIUPrefix, "OPR");
        if(gsBlockIUPrefix.find(sIU.substr(0,3)) !=std::string::npos and sIU.length() == 10)
        {
            ShowLEDMsg("Lorry No Entry^Pls Reverse","Lorry No Entry^Pls Reverse");
            SendMsg2Server("90",sIU+",,,,,Block IU");
            return;
        }

    }

    if(sIU.length()==10) 
	    tEntry.iTransType= db::getInstance()->FnGetVehicleType(sIU.substr(0,3));
	else {
        tEntry.iTransType=GetVTypeFromLoop();
    }
    if (tEntry.iTransType == 9) {
        ShowLEDMsg(tMsg.MsgEntry_authorizedvehicle[0],tMsg.MsgEntry_authorizedvehicle[1]);
        tEntry.iStatus = 0;
        SaveEntry();
        Openbarrier();
        return;
    }
    if (tProcess.gbcarparkfull ==1 and tParas.giFullAction == iLock)
    {   
        ShowLEDMsg(tMsg.MsgEntry_LockStation[0], tMsg.MsgEntry_LockStation[1]);
        writelog("Loop A while station Locked","OPR");
        return;
    } 
    iRet = CheckSeason(sIU,1);

    if (tProcess.gbcarparkfull ==1 and iRet == 1 and (std::stoi(tSeason.rate_type) !=0) and tParas.giFullAction ==iNoPartial )
    {   
        writelog ("VIP Season Only.", "OPR");
        ShowLEDMsg("VIP Season Only", "VIP Season Only");
        return;
    } 
    if (tProcess.gbcarparkfull ==1 and iRet != 1 )
    {   
        writelog ("Season Only.", "OPR");
        ShowLEDMsg(tMsg.MsgEntry_SeasonOnly[0], tMsg.MsgEntry_SeasonOnly[1]);
        return;
    } 
    if (iRet == 6 and sIU.length()>10)
    {
        writelog ("season passback","OPR");
        SendMsg2Server("90",sIU+",,,,,Season Passback");
        ShowLEDMsg(tMsg.MsgEntry_SeasonPassback[0],tMsg.MsgEntry_SeasonPassback[1]);
        return;
    }
    if (iRet == 10) {
        ShowLEDMsg(tMsg.MsgEntry_SeasonAsHourly[0],tMsg.MsgEntry_SeasonAsHourly[1]);
    } else if (iRet == 8)  ShowLEDMsg(tMsg.MsgEntry_WithIU[0],tMsg.MsgEntry_WithIU[1]);

        tEntry.iStatus = 0; 
        //---------
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
    tProcess.gbLoopApresent = false;
    tProcess.gbLoopAIsOn = false;
    tProcess.gbLoopBIsOn = false;
    tProcess.gbLoopCIsOn = false;
    //--------
    tProcess.giSyncTimeCnt = 0;

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
             size_t pos = result.find("inet addr:");
             result = result.substr(pos + 10);
             result.erase(result.find(' '));
            break;
         }

         if (result.find("eth0") != std::string::npos) {
             iRet = 1;
         }
    }
    
    pclose(pipe);

    size_t lastDotPosition = result.find_last_of('.');
    result = result.substr(0, lastDotPosition + 1)+ "255";
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

void operation::FnSendMyStatusToMonitor()
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
	SendMsg2Monitor("00",str);
}

void operation::FnSyncCentralDBTime()
{
    m_db->synccentraltime();
}

void operation::FnSendDIOInputStatusToMonitor(int pinNum, int pinValue)
{
    std::string str = std::to_string(pinNum) + "," + std::to_string(pinValue);
    SendMsg2Monitor("302", str);
}

void operation::FnSendDateTimeToMonitor()
{
    std::string str = Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmm();
    SendMsg2Monitor("304", str);
}

void operation::FnSendLogMessageToMonitor(std::string msg)
{
    std::string str = "[" + gtStation.sName + "|" + std::to_string(gtStation.iSID) + "|" + "305" + "|" + msg + "|]";
    m_Monitorudp->startsend(str);
}

void operation::FnSendLEDMessageToMonitor(std::string line1TextMsg, std::string line2TextMsg)
{
    std::string str = line1TextMsg + "," + line2TextMsg;
    SendMsg2Monitor("306", str);
}

void operation::SendMsg2Monitor(string cmdcode,string dstr)
{
	string str="["+ gtStation.sName+"|"+to_string(gtStation.iSID)+"|"+cmdcode+"|";
	str+=dstr+"|]";
	m_Monitorudp->startsend(str);
    //----
    writelog ("Message to Monitor: " + str,"OPR");
}

void operation::SendMsg2Server(string cmdcode,string dstr)
{
	string str="["+ gtStation.sName+"|"+to_string(gtStation.iSID)+"|"+cmdcode+"|";
	str+=dstr+"|]";
	m_udp->startsend(str);
    //----
    writelog ("Message to PMS: " + str,"OPR");
}

int operation::CheckSeason(string sIU,int iInOut)
{
   int iRet;
   string sMsg;
   string sLCD;
   iRet = db::getInstance()->isvalidseason(sIU,iInOut,gtStation.iZoneID);
   //showLED message
   if (iRet != 8 ) {
        FormatSeasonMsg(iRet, sIU, sMsg, sLCD);
   } 
   return iRet;
}

void operation::writelog(string sMsg, string soption)
{
    std::stringstream dbss;
	dbss << sMsg;
    Logger::getInstance()->FnLog(dbss.str(), "", soption);

}

void operation::HandlePBSError(EPSError iEPSErr, int iErrCode)
{

    string sErrMsg = "";
    string sCmd = "";
    
    switch(iEPSErr){
        case AntennaNoError:
        {
            if (tPBSError[iAntenna].ErrNo < 0) {
                sCmd = "03";
                sErrMsg = "Antenna OK";
            }
            tPBSError[iAntenna].ErrNo = 0;
            break;
        }
        case AntennaError:
        {
            tPBSError[iAntenna].ErrNo = -1;
            tPBSError[iAntenna].ErrCode = iErrCode;
            tPBSError[iAntenna].ErrMsg = "Antenna Error: " + iErrCode;
            sErrMsg = tPBSError[iAntenna].ErrMsg;
            sCmd = "03";
            break;
        }
        case AntennaPowerOnOff:
        {
            if (iErrCode == 1) {
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
        }
        case PrinterNoError:
        {   
            if (tPBSError[1].ErrNo < 0){
                sCmd = "04";
                sErrMsg = "Printer OK";
            }
            tPBSError[1].ErrNo = 0;
            break;
        }
        case PrinterError:
        {
            tPBSError[1].ErrNo = -1;
            tPBSError[1].ErrMsg = "Printer Error";
            sCmd = "04";
            sErrMsg = tPBSError[1].ErrMsg;
            break;
        }
        case PrinterNoPaper:
        {
            tPBSError[1].ErrNo = -2;
            tPBSError[1].ErrMsg = "Printer Paper Low";
            sCmd = "04";
            sErrMsg = tPBSError[1].ErrMsg;
            break;
        }
        case DBNoError:
        {
            tPBSError[2].ErrNo = 0;
            break;
        }
        case DBFailed:
        {
            tPBSError[2].ErrNo = -1;
            break;
        }
        case DBUpdateFail:
        {
            tPBSError[2].ErrNo = -2;
            break;
        }
        case UPOSNoError:
        {
            if (tPBSError[4].ErrNo < 0){
                sCmd = "06";
                sErrMsg = "UPOS OK";
            }
            tPBSError[4].ErrNo = 0;
            break;
        }
        case UPOSError:
        {
            tPBSError[4].ErrNo = -1;
            tPBSError[4].ErrMsg = "UPOS Error";
            sCmd = "06";
            sErrMsg = tPBSError[4].ErrMsg;
            break;
        }
        case TariffError:
        {
            tPBSError[5].ErrNo = -1;     
            sCmd = "08";
            sErrMsg = "5Tariff Error";
            break;
        }
        case TariffOk:
        {
            tPBSError[5].ErrNo = 0 ;    
            sCmd = "08";
            sErrMsg = "5Tariff OK";
            break;
        }
        case HolidayError:
        {
            tPBSError[5].ErrNo = -2;
            sCmd = "08";
            sErrMsg = "5No Holiday Set";
            break;
        }
        case HolidayOk:
        {
            tPBSError[5].ErrNo = 0;    
            sCmd = "08";
            sErrMsg = "5Holiday OK";
            break;
        }
        case DIOError:
        {
            tPBSError[6].ErrNo = -1;
            sCmd = "08";
            sErrMsg = "6DIO Error";
            break;
        }
        case DIOOk:
        {
            tPBSError[6].ErrNo = 0;
            sCmd = "08";
            sErrMsg = "6DIO OK";
            break;
        }
        case LoopAHang:
        {
            tPBSError[7].ErrNo = -1;
            sCmd = "08";
            sErrMsg = "7Loop A Hang";
            break;
        }
        case LoopAOk:
        {
            tPBSError[7].ErrNo = 0;
            sCmd = "08";
            sErrMsg = "7Loop A OK";
            break;
        }
        case LCSCNoError:
        {
            if (tPBSError[10].ErrNo < 0){
                sCmd = "70";
                sErrMsg = "LCSC OK";
            }
            tPBSError[10].ErrNo = 0;
            break;
        }
        case LCSCError:
        {
            tPBSError[10].ErrNo = -1;
            tPBSError[10].ErrMsg = "LCSC Error";
            sCmd = "70";
            sErrMsg = tPBSError[10].ErrMsg;
            break;
        }
        case SDoorError:
        {
           sCmd = "71";
            sErrMsg = tPBSError[11].ErrMsg;
            break;
        }
        case BDoorError:
        {
            sCmd = "72";
            sErrMsg = tPBSError[12].ErrMsg;
            break;
        }
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
    //car: 1 M/C: 7 Lorry: 4
    int ret = 1;
    if (operation::getInstance()->tProcess.gbLoopAIsOn == true && operation::getInstance()->tProcess.gbLoopBIsOn == true)
    {
        ret = 1;
    }
    else if (operation::getInstance()->tProcess.gbLoopAIsOn == true || operation::getInstance()->tProcess.gbLoopBIsOn == true)
    {
        ret = 7;
    }
   
    return ret;

}

void operation::SaveEntry()
{
    int iRet;
    
    if (tEntry.sIUTKNo== "") return;

    iRet = db::getInstance()->insertentrytrans(tEntry);

    tPBSError[iDB].ErrNo = (iRet == iDBSuccess) ? 0 : (iRet == iCentralFail) ? -1 : -2;

    std::string sMsg2Send = (iRet == iDBSuccess) ? "Entry OK" : (iRet == iCentralFail) ? "Entry Central Failed" : "Entry Local Failed";

    sMsg2Send = tEntry.sIUTKNo + ",,,,,"+sMsg2Send;

    if (tEntry.iStatus == 0) {
        SendMsg2Server("90", sMsg2Send);
    }

}

void operation::ShowTotalLots(std::string totallots, std::string LEDId)
{
    if (LEDManager::getInstance()->getLED(getSerialPort(std::to_string(tParas.giCommportLED401))) != nullptr)
    {
        writelog ("Comm Port:"+std::to_string(tParas.giCommportLED401),"OPR");
        writelog ("Total Lot:"+ totallots,"OPR");
       
        LEDManager::getInstance()->getLED(getSerialPort(std::to_string(tParas.giCommportLED401)))->FnLEDSendLEDMsg(LEDId, totallots, LED::Alignment::RIGHT);
    }
}

void operation::FormatSeasonMsg(int iReturn, string sNo, string sMsg, string sLCD, int iExpires)
 {
    
    std::string sExp;
    int i;

    std::string sMsgPartialSeason;

    if (gtStation.iType == tientry && tEntry.iTransType > 49) {
        sMsgPartialSeason = db::getInstance()->GetPartialSeasonMsg(tEntry.iTransType);
    } 

    if (sMsgPartialSeason.empty()) {
        sMsgPartialSeason = "Season";
    }

    writelog("partial season msg:" + sMsgPartialSeason + ", trans type: " + std::to_string(tEntry.iTransType),"OPR");

        switch (iReturn) {
            case -1: 
                writelog("DB error when Check season", "OPR");
                break;
            case 0:  
                sMsg = tMsg.MsgEntry_SeasonInvalid[0];
                sLCD = tMsg.MsgEntry_SeasonInvalid[1];
                break;
            case 1:  
                if (gtStation.iType == tientry) {
                    sMsg = tMsg.MsgEntry_ValidSeason[0];
                    sLCD = tMsg.MsgEntry_ValidSeason[1];
                } 
                break;
            case 2: 
                sMsg = tMsg.MsgEntry_SeasonExpired[0];
                sLCD = tMsg.MsgEntry_SeasonExpired[1];
                writelog("Season Expired", "OPR");
                break;
            case 3: 
                sMsg = tMsg.MsgEntry_SeasonTerminated[0];
                sLCD = tMsg.MsgEntry_SeasonTerminated[1];
                writelog ("Season terminated", "OPR");
                break;
            case 4:  
                sMsg = tMsg.MsgEntry_SeasonBlocked[0];
                sLCD = tMsg.MsgEntry_SeasonBlocked[1];
                writelog ("Season Blocked", "OPR");
                break;
            case 5:  
                sMsg = tMsg.MsgEntry_SeasonInvalid[0];
                sLCD = tMsg.MsgEntry_SeasonInvalid[1];
                writelog ("Season Lost", "OPR");
                break;
            case 6:
                sMsg = tMsg.MsgEntry_SeasonPassback[0];
                sLCD = tMsg.MsgEntry_SeasonPassback[1];
                writelog ("Season Passback", "OPR");
                break;
            case 7:  
                sMsg = tMsg.MsgEntry_SeasonNotStart[0];
                sLCD = tMsg.MsgEntry_SeasonNotStart[1];
                writelog ("Season Not Start", "OPR");
                break;
            case 8: 
                sMsg = "Wrong Season Type";
                sLCD = "Wrong Season Type";
                writelog ("Wrong Season Type", "OPR");
                break;
            case 10:     
                sMsg = tMsg.MsgEntry_SeasonAsHourly[0];
                sLCD = tMsg.MsgEntry_SeasonAsHourly[1];
                writelog ("Season As Hourly", "OPR");
                break;
            case 11:     
                sMsg = tMsg.MsgEntry_ESeasonWithinAllowance[0];
                sLCD = tMsg.MsgEntry_ESeasonWithinAllowance[1];
                writelog ("Season within allowance", "OPR");
                break;
            case 12:
               // sMsg = gsMsgMasterSeason
               // sLCD = gscMsgMasterSeason
                writelog ("Master Season", "OPR");
                break;
            case 13:     
                sMsg = tMsg.MsgEntry_WholeDayParking[0];
                sLCD = tMsg.MsgEntry_WholeDayParking[1];
                writelog ("Whole Day Season", "OPR");
                break;
            default:
                break;
        }

        size_t pos = sMsg.find("Season");
        if (pos != std::string::npos) sMsg.replace(pos, 6, sMsgPartialSeason);
        pos=sLCD.find("Season");
        if (pos != std::string::npos) sLCD.replace(pos, 6, sMsgPartialSeason);
        
        ShowLEDMsg(sMsg,sLCD);

}

void operation::ManualOpenBarrier()
{
     writelog ("Manual open barrier.", "OPR");
     if (gtStation.iType == tientry){
        CE_Time dt;
	    string dtStr=dt.DateString()+" "+dt.TimeWithMsString();
	    tEntry.sEntryTime=dtStr;
        tEntry.iStatus = 4;
        //---------
        m_db->AddRemoteControl(std::to_string(gtStation.iSID),"Manual open barrier","Auto save for IU:"+tEntry.sIUTKNo);
        SaveEntry();
        Openbarrier();
     }
}


