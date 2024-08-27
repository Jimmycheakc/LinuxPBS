
#include <sys/mount.h>
#include <iostream>
#include <string>
#include <memory>
#include <chrono>
#include <thread>
#include <filesystem>
#include <cstdlib>
#include <fstream>
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
#include "ksm_reader.h"
#include "lpr.h"
#include "printer.h"
#include "upt.h"

operation* operation::operation_ = nullptr;
std::mutex operation::mutex_;

operation::operation()
{
    isOperationInitialized_.store(false);
}

operation* operation::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
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
    iCurrentContext = &ioContext;
    //
    Setdefaultparameter();
    //--- broad cast UDP
    tProcess.gsBroadCastIP = getIPAddress();
    try
    {
        m_udp = new udpclient(ioContext, tProcess.gsBroadCastIP, 2001,2001);
        m_udp->socket_.set_option(socket_base::broadcast(true));
    }
    catch (const boost::system::system_error& e) // Catch Boost.Asio system errors
    {
        std::string cppString(e.what());
        writelog ("Boost.Asio Exception during PMS UDP initialization: "+ cppString,"OPR");
    }
    catch (const std::exception& e) {
        std::string cppString(e.what());
        writelog ("Exception during PMS UDP initialization: "+ cppString,"OPR");
    }
    catch (...)
    {
        writelog ("Unknown Exception during PMS UDP initialization.","OPR");
    }
    // monitor UDP
    try
    {
        m_Monitorudp = new udpclient(ioContext, tParas.gsCentralDBServer, 2008,2008);
    }
    catch (const boost::system::system_error& e) // Catch Boost.Asio system errors
    {
        std::string cppString1(e.what());
        writelog ("Boost.Asio Exception during Monitor UDP initialization: "+ cppString1,"OPR");
    }
    catch (const std::exception& e) {
        std::string cppString1(e.what());
        writelog ("Exception during Monitor UDP initialization: "+ cppString1,"OPR");
    }
    catch (...)
    {
        writelog ("Unknown Exception during Monitor UDP initialization.","OPR");
    }
    //
    int iRet = 0;
    m_db = db::getInstance();

    //iRet = m_db->connectlocaldb("DSN={MariaDB-server};DRIVER={MariaDB ODBC 3.0 Driver};SERVER=127.0.0.1;PORT=3306;DATABASE=linux_pbs;UID=linuxpbs;PWD=SJ2001;",2,2,1);
    iRet = m_db->connectlocaldb("DRIVER={MariaDB ODBC 3.0 Driver};SERVER=localhost;PORT=3306;DATABASE=linux_pbs;UID=linuxpbs;PWD=SJ2001;",2,2,1);
    if (iRet != 1) {
        writelog ("Unable to connect local DB.","OPR");
        exit(0); 
    }
    string m_connstring;
    writelog ("Connect Central (" +tParas.gsCentralDBServer +  ") DB:"+tParas.gsCentralDBName,"OPR");
    for (int i = 0 ; i < 5; ++i ) {
      //  m_connstring = "DSN=mssqlserver;DATABASE=" + tParas.gsCentralDBName + ";UID=sa;PWD=yzhh2007";
        m_connstring = "DRIVER=FreeTDS;SERVER=" + tParas.gsCentralDBServer + ";PORT=1433;DATABASE=" + tParas.gsCentralDBName + ";UID=sa;PWD=yzhh2007";
        iRet = m_db->connectcentraldb(m_connstring,tParas.gsCentralDBServer,2,2,1);
        if (iRet == 1) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    // sync central time
    if (iRet == 1) {
        m_db->synccentraltime();
    } else
    {
        tProcess.giSystemOnline = 1;
    }
    //
    if (LoadParameter()) {
        Initdevice(ioContext);
        //----
        writelog("EPS in operation","OPR");
        //------
        tProcess.setIdleMsg(0, operation::getInstance()->tMsg.Msg_DefaultLED[0]);
        tProcess.setIdleMsg(1, operation::getInstance()->tMsg.Msg_Idle[1]);
        //-------
        Clearme();
        CheckReader();
        isOperationInitialized_.store(true);
        DIO::getInstance()->FnStartDIOMonitoring();
        SendMsg2Server("90",",,,,,Starting OK");
        //-----
        m_db->downloadseason();
        m_db->moveOfflineTransToCentral();
       
    }else {
        tProcess.gbInitParamFail = 1;
        writelog("Unable to load parameter, Please download or check!", "OPR");
        if (iRet == 1)
        {
            m_db->downloadstationsetup();
            m_db->loadstationsetup();
        }
    }
}

bool operation::LoadParameter()
{
    DBError iReturn;
    bool gbLoadParameter = true;
    //------
    iReturn = m_db->loadstationsetup();
    if (iReturn != 0) {
        if (iReturn ==1) {
            writelog ("No data for station setup table", "OPR");
        }else{
            writelog ("Error for loading station setup", "OPR");
        }
        gbLoadParameter = false; 
    } 
    iReturn = m_db->loadmessage();
    if (iReturn != 0) {
        if (iReturn ==1) {
            writelog ("No data for LED message table", "OPR");
        }else{
            writelog ("Error for loading LED message", "OPR");
        }
        gbLoadParameter = false; 
    }
    iReturn = m_db->loadParam();
    if (iReturn != 0) {
        if (iReturn ==1) {
            writelog ("No data for Parameter table", "OPR");
        }else{
            writelog ("Error for loading parameter", "OPR");
        }
        gbLoadParameter = false; 
    }
    iReturn = m_db->loadvehicletype();
    if (iReturn != 0) {
        if (iReturn ==1) {
            writelog ("No data for vehicle type table", "OPR");
        }else{
            writelog ("Error for loading vehicle type", "OPR");
        }
       gbLoadParameter = false; 
    }
    iReturn = m_db->loadTR();
    if (iReturn != 0)
    {
        if (iReturn == 1)
        {
            writelog("No data for TR table", "OPR");
        }
        else
        {
            writelog("Error for loading TR", "OPR");
        }
        gbLoadParameter = false;
    }

    //
    return gbLoadParameter;
}

bool operation:: LoadedparameterOK()
{
    if (tProcess.gbloadedLEDMsg == true and tProcess.gbloadedParam==true and tProcess.gbloadedStnSetup==true and tProcess.gbloadedVehtype ==true) return true;
    else return false;
}

bool operation::FnIsOperationInitialized() const
{
    return isOperationInitialized_.load();
}

void operation::FnLoopATimeoutHandler()
{
    Logger::getInstance()->FnLog("Loop A Timeout handler.", "", "OPR");
    LoopACome();
}

void operation::LoopACome()
{
    //--------
    writelog ("Loop A Come","OPR");

    // Loop A timer - To prevent loop A hang
    pLoopATimer_->expires_from_now(boost::posix_time::seconds(operation::getInstance()->tParas.giOperationTO));
    pLoopATimer_->async_wait([this] (const boost::system::error_code &ec)
    {
        if (!ec)
        {
            this->FnLoopATimeoutHandler();
        }
        else if (ec == boost::asio::error::operation_aborted)
        {
            Logger::getInstance()->FnLog("Loop A timer cancelled.", "", "OPR");
        }
        else
        {
            std::stringstream ss;
            ss << "Loop A timer timeout error :" << ec.message();
            Logger::getInstance()->FnLog(ss.str(), "", "OPR");
        }
    });

    ShowLEDMsg(tMsg.Msg_LoopA[0], tMsg.Msg_LoopA[1]);
    Clearme();
    DIO::getInstance()->FnSetLCDBacklight(1);
    //----
    tEntry.gsTransID = tParas.gscarparkcode + "-" + std::to_string (gtStation.iSID) + "-" + Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmmss();
    Lpr::getInstance()->FnSendTransIDToLPR(tEntry.gsTransID);

    //----
    if (AntennaOK() == true) Antenna::getInstance()->FnAntennaSendReadIUCmd();
    ShowLEDMsg("Reading IU...^Please wait", "Reading IU...^Please wait");
 //   operation::getInstance()->EnableCashcard(true);
}

void operation::LoopAGone()
{
    writelog ("Loop A End","OPR");

    // Cancel the loop A timer 
    pLoopATimer_->cancel();

    //------
    DIO::getInstance()->FnSetLCDBacklight(0);
     //
    if (tEntry.sIUTKNo == "") {
        EnableCashcard(false);
        Antenna::getInstance()->FnAntennaStopRead();
    }
}
void operation::LoopCCome()
{
    writelog ("Loop C Come","OPR");


}
void operation::LoopCGone()
{
    writelog ("Loop C End","OPR");
    if (tProcess.gbLoopApresent.load() == true)
    {
        LoopACome();
    }
}
void operation::VehicleCome(string sNo)
{

    if (sNo.length() == 10) {
        writelog ("Received IU: "+sNo,"OPR");
    }
    else {
        writelog ("Received Card: "+sNo,"OPR");
        Antenna::getInstance()->FnAntennaStopRead();
    }

    if (sNo.length()==10 and sNo == tProcess.gsDefaultIU) {
        SendMsg2Server ("90",sNo+",,,,,Default IU");
        writelog ("Default IU: "+sNo,"OPR");
        //---------
        return;
    }
    //-----
    if (sNo.length() == 10) EnableCashcard(false);
    //----
    if (gtStation.iType == tientry) {

        if(tEntry.gbEntryOK == false) PBSEntry (sNo);
    } 
    else{
        PBSExit(sNo);
    }
    return;
}

void operation::Openbarrier()
{
    if (tProcess.giCardIsIn == 1) {
        ShowLEDMsg ("Please Take^CashCard.","Please Take^Cashcard.");
        return;
    }
    writelog ("Open Barrier","OPR");

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
        tEntry.esid = to_string(gtStation.iSID);
        tEntry.sSerialNo = "";     
        tEntry.sIUTKNo = "";
        tEntry.sEntryTime = "";
        tEntry.iTransType = 1;
        tEntry.iRateType = 0;
        tEntry.iStatus = 0;      

        tEntry.sCardNo = "";
        tEntry.sFee = 0.00;
        tEntry.sPaidAmt = 0.00;
        tEntry.sGSTAmt = 0.00;
        tEntry.sReceiptNo = "";
        tEntry.iCardType=0;
        tEntry.sOweAmt= 0.00;

        tEntry.sLPN[0] = "";
        tEntry.sLPN[1] = "";
        tEntry.iVehcileType = 0;
        tEntry.gbEntryOK = false;
        tEntry.sEnableReader = false;
        tEntry.giShowType = 1;
        tEntry.gsTransID = "";
        tProcess.giCardIsIn = 0;
        tProcess.gbsavedtrans = false;
       
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
    if (tParas.giCommPortAntenna > 0)
    {
        Antenna::getInstance()->FnAntennaInit(ioContext, 19200, getSerialPort(std::to_string(tParas.giCommPortAntenna)));
    }

    if (tParas.giCommPortLCSC > 0)
    {
        int iRet = LCSCReader::getInstance()->FnLCSCReaderInit(115200, getSerialPort(std::to_string(tParas.giCommPortLCSC)));
        if (iRet == -35) { 
            tPBSError[iLCSC].ErrNo = -4;
            HandlePBSError(LCSCError);
        }
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
    
    if (tParas.giCommPortKDEReader > 0)
    {
        int iRet = KSM_Reader::getInstance()->FnKSMReaderInit(ioContext, 9600, getSerialPort(std::to_string(tParas.giCommPortKDEReader)));
        if (iRet != 1)
        {  
            HandlePBSError(ReaderError);
        }
        //  -4 KDE comm port error
        if (iRet == -4) { tPBSError[iReader].ErrNo = -4;}
    }

    if (tParas.giCommPortUPOS > 0)
    {
        Upt::getInstance()->FnUptInit(115200, getSerialPort(std::to_string(tParas.giCommPortUPOS)));
    }

    if (LCD::getInstance()->FnLCDInit())
    {
        pLCDIdleTimer_ = std::make_unique<Timer>();
        auto callback = std::bind(&operation::LcdIdleTimerTimeoutHandler, this);
        pLCDIdleTimer_->start(1000, callback);
    }

    if (tParas.giCommPortPrinter > 0)
    {
        Printer::getInstance()->FnSetPrintMode(2);
        Printer::getInstance()->FnSetDefaultAlign(Printer::CBM_ALIGN::CBM_LEFT);
        Printer::getInstance()->FnSetDefaultFont(2);
        Printer::getInstance()->FnSetLeftMargin(300);
        Printer::getInstance()->FnSetSelfTestInterval(2000);
        Printer::getInstance()->FnSetSiteID(10);
        Printer::getInstance()->FnSetPrinterType(Printer::PRINTER_TYPE::CBM1000);
        Printer::getInstance()->FnPrinterInit(9600, getSerialPort(std::to_string(tParas.giCommPortPrinter)));
    }

    GPIOManager::getInstance()->FnGPIOInit();
    DIO::getInstance()->FnDIOInit();
    Lpr::getInstance()->FnLprInit(ioContext);

    // Loop A timer
    pLoopATimer_ = std::make_unique<boost::asio::deadline_timer>(ioContext);
}

void operation::LcdIdleTimerTimeoutHandler()
{
    if ((tProcess.gbcarparkfull.load() == false) && (tProcess.gbLoopApresent.load() == false))
    {
        ShowLEDMsg(tProcess.getIdleMsg(0), tProcess.getIdleMsg(1));
        std::string LCDMsg = Common::getInstance()->FnGetDateTimeFormat_ddmmyyy_hhmmss();
        char * sLCDMsg = const_cast<char*>(LCDMsg.data());
        LCD::getInstance()->FnLCDDisplayRow(2, sLCDMsg);
    }
    else if ((tProcess.gbcarparkfull.load() == true) && (tProcess.gbLoopApresent.load() == false))
    {
        ShowLEDMsg(tProcess.getIdleMsg(0), tProcess.getIdleMsg(1));
    }
}

void operation::ShowLEDMsg(string LEDMsg, string LCDMsg)
{
    static std::string sLastLEDMsg;
    static std::string sLastLCDMsg;

    if (sLastLEDMsg != LEDMsg)
    {
        sLastLEDMsg = LEDMsg;
        writelog ("LED Message:" + LEDMsg,"OPR");

        if (LEDManager::getInstance()->getLED(getSerialPort(std::to_string(tParas.giCommPortLED))) != nullptr)
        {
            LEDManager::getInstance()->getLED(getSerialPort(std::to_string(tParas.giCommPortLED)))->FnLEDSendLEDMsg("***", LEDMsg, LED::Alignment::CENTER);
        }
    }

    if (sLastLCDMsg != LCDMsg)
    {
        sLastLCDMsg = LCDMsg;
        writelog ("LCD Message:" + LCDMsg,"OPR");

        char* sLCDMsg = const_cast<char*>(LCDMsg.data());
        LCD::getInstance()->FnLCDDisplayScreen(sLCDMsg);
    }
}

void operation::PBSEntry(string sIU)
{
    int iRet;

    tEntry.sIUTKNo = sIU;
    tEntry.sEntryTime= Common::getInstance()->FeGetDateTimeFormat_VehicleTrans();

    if (sIU == "") return;
    //check blacklist
    SendMsg2Server("90",sIU+",,,,,PMS_DVR");
    iRet = m_db->IsBlackListIU(sIU);
    if (iRet >= 0){
        ShowLEDMsg(tMsg.MsgBlackList[0], tMsg.MsgBlackList[1]);
        SendMsg2Server("90",sIU+",,,,,Blacklist IU");
        if(iRet ==0) return;
    }
    //check block 
    string gsBlockIUPrefix = IniParser::getInstance()->FnGetBlockIUPrefix();
   // writelog ("blockIUprfix =" +gsBlockIUPrefix, "OPR");
    if(gsBlockIUPrefix.find(sIU.substr(0,3)) !=std::string::npos and sIU.length() == 10)
    {
        ShowLEDMsg("Lorry No Entry^Pls Reverse","Lorry No Entry^Pls Reverse");
        SendMsg2Server("90",sIU+",,,,,Block IU");
        return;
    }

    if(sIU.length()==10) 
	    tEntry.iTransType= db::getInstance()->FnGetVehicleType(sIU.substr(0,3));
	else {
        tEntry.iTransType=GetVTypeFromLoop();
    }
    if (tEntry.iTransType == 9) {
        ShowLEDMsg(tMsg.Msg_authorizedvehicle[0],tMsg.Msg_authorizedvehicle[1]);
        tEntry.iStatus = 0;
        SaveEntry();
        Openbarrier();
        return;
    }
    if (tProcess.gbcarparkfull.load() == true and tParas.giFullAction == iLock)
    {   
        ShowLEDMsg(tMsg.Msg_LockStation[0], tMsg.Msg_LockStation[1]);
        writelog("Loop A while station Locked","OPR");
        return;
    }
    tEntry.iVehcileType = (tEntry.iTransType -1 )/3;
    iRet = CheckSeason(sIU,1);

    if (tProcess.gbcarparkfull.load() == true and iRet == 1 and (std::stoi(tSeason.rate_type) !=0) and tParas.giFullAction ==iNoPartial )
    {   
        writelog ("VIP Season Only.", "OPR");
        ShowLEDMsg("Carpark Full!^VIP Season Only", "Carpark Full!^VIP Season Only");
        return;
    } 
    if (tProcess.gbcarparkfull.load() == true and iRet != 1 )
    {   
        writelog ("Season Only.", "OPR");
        ShowLEDMsg("Carpark Full!^Season only", "Carpark Full!^Season only");
        return;
    } 
    if (iRet == 6 and sIU.length()>10)
    {
        writelog ("season passback","OPR");
        SendMsg2Server("90",sIU+",,,,,Season Passback");
        ShowLEDMsg(tMsg.Msg_SeasonPassback[0],tMsg.Msg_SeasonPassback[1]);
        return;
    }
    if (iRet ==1 or iRet == 4 or iRet == 6) {
        tEntry.iTransType = GetSeasonTransType(tEntry.iVehcileType,std::stoi(tSeason.rate_type), tEntry.iTransType);
        tEntry.giShowType = 0;
    }

    if (iRet == 10) {
        ShowLEDMsg(tMsg.Msg_SeasonAsHourly[0],tMsg.Msg_SeasonAsHourly[1]);
        tEntry.giShowType = 2;
    }
    if (iRet != 1) {
        ShowLEDMsg(tMsg.Msg_WithIU[0],tMsg.Msg_WithIU[1]);
    }
        //---------
        SaveEntry();
        tEntry.gbEntryOK = true;
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
    tProcess.glNoofOfflineData = 0;
    tProcess.giSystemOnline = -1;
    //clear error msg
     for (int i= 0; i< Errsize; ++i){
        tPBSError[i].ErrNo = 0;
        tPBSError[i].ErrCode =0;
	    tPBSError[i].ErrMsg = "";
    }
    tProcess.gbcarparkfull.store(false);
    tProcess.gbLoopApresent.store(false);
    tProcess.gbLoopAIsOn = false;
    tProcess.gbLoopBIsOn = false;
    tProcess.gbLoopCIsOn = false;
    tProcess.gbLorrySensorIsOn = false;
    //--------
    tProcess.giSyncTimeCnt = 0;
    tProcess.gbloadedParam = false;
	tProcess.gbloadedVehtype = false;
	tProcess.gbloadedLEDMsg = false;
	tProcess.gbloadedStnSetup = false;
    //-------
    tProcess.gbInitParamFail = 0;
    tProcess.giCardIsIn = 0;
    //-------
    tProcess.giLastHousekeepingDate = 0;
    tProcess.setLastIUNo("");
    tProcess.setLastIUEntryTime(std::chrono::steady_clock::now());
    //---

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
  //5=Param error, 6=DIO,7=Loop A hang,8=CHU, 9=ups, 10= LCSC
  //11= station door status, 12 = barrier door status, 13= TGD controll status
  //14 = TGD sensor status 15=Arm drop status,16=barrier status,17=ticket status d DateTime
    CE_Time dt;
	string str="";
    //----
    if (tProcess.gbInitParamFail != 1) {
        tPBSError[iBarrierStauts].ErrNo = DIO::getInstance()->FnGetBarrierStatus();
        if (DIO::getInstance()->FnGetArmbroken() == 1) tPBSError[iBarrierStauts].ErrNo=3;
    }
    //------
    for (int i= 0; i< Errsize; ++i){
        str += std::to_string(tPBSError[i].ErrNo) + ",";
    }
	str+="0;0,"+dt.DateTimeNumberOnlyString()+",";
	SendMsg2Server("00",str);
	
}

void operation::FnSendMyStatusToMonitor()
{
    //EPS error index:  0=antenna,     1=printer,   2=DB, 3=Reader, 4=UPOS
    //5=Param error, 6=DIO,7=Loop A hang,8=CHU, 9=ups, 10= LCSC
    //11= station door status, 12 = barrier door status, 13= TGD controll status
    //14 = TGD sensor status 15=Arm drop status,16=barrier status,17=ticket status d DateTime
    CE_Time dt;
	string str="";
    //-----
    for (int i= 0; i< Errsize; ++i){
        str += std::to_string(tPBSError[i].ErrNo) + ",";
    }
	str+="0;0,"+dt.DateTimeNumberOnlyString()+",";
	SendMsg2Monitor("300",str);
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
    if (m_Monitorudp->FnGetMonitorStatus())
    {
        std::string str = "[" + gtStation.sPCName + "|" + std::to_string(gtStation.iSID) + "|" + "305" + "|" + msg + "|]";
        m_Monitorudp->startsend(str);
    }
}

void operation::FnSendLEDMessageToMonitor(std::string line1TextMsg, std::string line2TextMsg)
{
    std::string str = line1TextMsg + "," + line2TextMsg;
    SendMsg2Monitor("306", str);
}

void operation::FnSendCmdDownloadParamAckToMonitor(bool success)
{
    std::string str = "99";

    if (!success)
    {
        str = "98";
    }

    SendMsg2Monitor("310", str);
}

void operation::FnSendCmdDownloadIniAckToMonitor(bool success)
{
    std::string str = "99";

    if (!success)
    {
        str = "98";
    }

    SendMsg2Monitor("309", str);
}

void operation::FnSendCmdGetStationCurrLogToMonitor()
{
    try
    {
        // Get today's date
        auto today = std::chrono::system_clock::now();
        auto todayDate = std::chrono::system_clock::to_time_t(today);
        std::tm* localToday = std::localtime(&todayDate);

        std::string logFilePath = Logger::getInstance()->LOG_FILE_PATH;

        // Extract year, month and day
        std::ostringstream ossToday;
        ossToday << std::setw(2) << std::setfill('0') << (localToday->tm_year % 100);
        ossToday << std::setw(2) << std::setfill('0') << (localToday->tm_mon + 1);
        ossToday << std::setw(2) << std::setfill('0') << localToday->tm_mday;

        std::string todayDateStr = ossToday.str();

        // Iterate through the files in the log file path
        int foundNo_ = 0;
        for (const auto& entry : std::filesystem::directory_iterator(logFilePath))
        {
            if ((entry.path().filename().string().find(todayDateStr) != std::string::npos) &&
                (entry.path().extension() == ".log"))
            {
                foundNo_ ++;
            }
        }

        bool copyFileFail = false;
        if (foundNo_ > 0)
        {
            std::stringstream ss;
            ss << "Found " << foundNo_ << " log files.";
            Logger::getInstance()->FnLog(ss.str(), "", "OPR");

            // Create the mount poin directory if doesn't exist
            std::string mountPoint = "/mnt/logbackup";
            std::string sharedFolderPath = operation::getInstance()->tParas.gsLogBackFolder;
            std::replace(sharedFolderPath.begin(), sharedFolderPath.end(), '\\', '/');
            std::string username = IniParser::getInstance()->FnGetCentralUsername();
            std::string password = IniParser::getInstance()->FnGetCentralPassword();

            if (!std::filesystem::exists(mountPoint))
            {
                std::error_code ec;
                if (!std::filesystem::create_directories(mountPoint, ec))
                {
                    Logger::getInstance()->FnLog(("Failed to create " + mountPoint + " directory : " + ec.message()), "", "OPR");
                    throw std::runtime_error(("Failed to create " + mountPoint + " directory : " + ec.message()));
                }
                else
                {
                    Logger::getInstance()->FnLog(("Successfully to create " + mountPoint + " directory."), "", "OPR");
                }
            }
            else
            {
                Logger::getInstance()->FnLog(("Mount point directory: " + mountPoint + " exists."), "", "OPR");
            }

            // Mount the shared folder
            std::string mountCommand = "sudo mount -t cifs " + sharedFolderPath + " " + mountPoint +
                                        " -o username=" + username + ",password=" + password;
            int mountStatus = std::system(mountCommand.c_str());
            if (mountStatus != 0)
            {
                Logger::getInstance()->FnLog(("Failed to mount " + mountPoint), "", "OPR");
                throw std::runtime_error("Failed to mount " + mountPoint);
            }
            else
            {
                Logger::getInstance()->FnLog(("Successfully to mount " + mountPoint), "", "OPR");
            }

            // Copy files to mount folder
            for (const auto& entry : std::filesystem::directory_iterator(logFilePath))
            {
                if ((entry.path().filename().string().find(todayDateStr) != std::string::npos) &&
                    (entry.path().extension() == ".log"))
                {
                    std::error_code ec;
                    std::filesystem::copy(entry.path(), mountPoint / entry.path().filename(), std::filesystem::copy_options::overwrite_existing, ec);
                    
                    if (!ec)
                    {
                        std::stringstream ss;
                        ss << "Copy file : " << entry.path() << " successfully.";
                        Logger::getInstance()->FnLog(ss.str(), "", "OPR");
                    }
                    else
                    {
                        std::stringstream ss;
                        ss << "Failed to copy log file : " << entry.path();
                        Logger::getInstance()->FnLog(ss.str(), "", "OPR");
                        copyFileFail = true;
                        break;
                    }
                }
            }

            // Unmount the shared folder
            std::string unmountCommand = "sudo umount " + mountPoint;
            int unmountStatus = std::system(unmountCommand.c_str());
            if (unmountStatus != 0)
            {
                Logger::getInstance()->FnLog(("Failed to unmount " + mountPoint), "", "OPR");
                throw std::runtime_error("Failed to unmount " + mountPoint);
            }
            else
            {
                Logger::getInstance()->FnLog(("Successfully to unmount " + mountPoint), "", "OPR");
            }
        }
        else
        {
            Logger::getInstance()->FnLog("No Log files to upload.", "", "OPR");
            throw std::runtime_error("No Log files to upload.");
        }

        if (!copyFileFail)
        {
            SendMsg2Monitor("314", "99");
        }
        else
        {
            SendMsg2Monitor("314", "98");
        }
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << " Exception: " << e.what();
        Logger::getInstance()->FnLog(ss.str(), "", "OPR");
        SendMsg2Monitor("314", "98");
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << " Unknown Exception.";
        Logger::getInstance()->FnLog(ss.str(), "", "OPR");
        SendMsg2Monitor("314", "98");
    }
}

bool operation::copyFiles(const std::string& mountPoint, const std::string& sharedFolderPath, 
                        const std::string& username, const std::string& password, const std::string& outputFolderPath)
{
    // Create the mount poin directory if doesn't exist
    if (!std::filesystem::exists(mountPoint))
    {
        std::error_code ec;
        if (!std::filesystem::create_directories(mountPoint, ec))
        {
            Logger::getInstance()->FnLog(("Failed to create " + mountPoint + " directory : " + ec.message()), "", "OPR");
            return false;
        }
        else
        {
            Logger::getInstance()->FnLog(("Successfully to create " + mountPoint + " directory."), "", "OPR");
        }
    }
    else
    {
        Logger::getInstance()->FnLog(("Mount point directory: " + mountPoint + " exists."), "", "OPR");
    }

    // Mount the shared folder
    std::string mountCommand = "sudo mount -t cifs " + sharedFolderPath + " " + mountPoint +
                                " -o username=" + username + ",password=" + password;
    int mountStatus = std::system(mountCommand.c_str());
    if (mountStatus != 0)
    {
        Logger::getInstance()->FnLog(("Failed to mount " + mountPoint), "", "OPR");
        return false;
    }
    else
    {
        Logger::getInstance()->FnLog(("Successfully to mount " + mountPoint), "", "OPR");
    }

    // Create the output folder if it doesn't exist
    if (!std::filesystem::exists(outputFolderPath))
    {
        std::error_code ec;
        if (!std::filesystem::create_directories(outputFolderPath, ec))
        {
            Logger::getInstance()->FnLog(("Failed to create " + outputFolderPath + " directory : " + ec.message()), "", "OPR");
            umount(mountPoint.c_str()); // Unmount if folder creation fails
            return false;
        }
        else
        {
            Logger::getInstance()->FnLog(("Successfully to create " + outputFolderPath + " directory."), "", "OPR");
        }
    }
    else
    {
        Logger::getInstance()->FnLog(("Output folder directory : " + outputFolderPath + " exists."), "", "OPR");
    }

    // Copy files to mount point
    bool foundIni = false;
    std::filesystem::path folder(mountPoint);
    if (std::filesystem::exists(folder) && std::filesystem::is_directory(folder))
    {
        for (const auto& entry : std::filesystem::directory_iterator(folder))
        {
            std::string filename = entry.path().filename().string();

            if (std::filesystem::is_regular_file(entry)
                && (filename.size() >= 4) && (filename == "LinuxPBS.ini"))
            {
                foundIni = true;
                std::filesystem::path dest_file = outputFolderPath / entry.path().filename();
                std::filesystem::copy(entry.path(), dest_file, std::filesystem::copy_options::overwrite_existing);

                std::stringstream ss;
                ss << "Copy " << entry.path() << " to " << dest_file << " successfully";
                Logger::getInstance()->FnLog(ss.str(), "", "OPR");
            }
        }
    }
    else
    {
        Logger::getInstance()->FnLog("Folder doesn't exist or is not a directory.", "", "OPR");
        umount(mountPoint.c_str());
        return false;
    }

    // Unmount the shared folder
    std::string unmountCommand = "sudo umount " + mountPoint;
    int unmountStatus = std::system(unmountCommand.c_str());
    if (unmountStatus != 0)
    {
        Logger::getInstance()->FnLog(("Failed to unmount " + mountPoint), "", "OPR");
        return false;
    }
    else
    {
        Logger::getInstance()->FnLog(("Successfully to unmount " + mountPoint), "", "OPR");
    }

    if (!foundIni)
    {
        Logger::getInstance()->FnLog("Ini file not found.", "", "OPR");
        return false;
    }

    return true;
}

bool operation::CopyIniFile(const std::string& serverIpAddress, const std::string& stationID)
{
    if ((!serverIpAddress.empty()) && (!stationID.empty()))
    {
        std::string sharedFilePath = "//" + serverIpAddress + "/carpark/LinuxPBS/Ini/Stn" + stationID;

        std::stringstream ss;
        ss << "Ini Shared File Path : " << sharedFilePath;
        Logger::getInstance()->FnLog(ss.str(), "", "OPR");

        return copyFiles("/mnt/ini", sharedFilePath, IniParser::getInstance()->FnGetCentralUsername(), IniParser::getInstance()->FnGetCentralPassword(), "/home/root/carpark/Ini");
    }
    else
    {
        Logger::getInstance()->FnLog("Server IP address or station ID empty.", "", "OPR");
        return false;
    }
}

void operation::SendMsg2Monitor(string cmdcode,string dstr)
{
    if (m_Monitorudp->FnGetMonitorStatus())
    {
        string str="["+ gtStation.sPCName +"|"+to_string(gtStation.iSID)+"|"+cmdcode+"|";
        str+=dstr+"|]";
        m_Monitorudp->startsend(str);
        //----
        writelog ("Message to Monitor: " + str,"OPR");
    }
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
            tPBSError[iAntenna].ErrMsg = "Antenna Error: " + std::to_string(iErrCode);
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
            tPBSError[11].ErrNo = -1;
            tPBSError[11].ErrMsg= "Station door open";
            sCmd = "71";
            sErrMsg = tPBSError[11].ErrMsg;
            break;
        }
        case SDoorNoError:
        {
            tPBSError[11].ErrNo = 0;
            sCmd = "71";
            sErrMsg = "Station door close";
            break;
        }
        case BDoorError:
        {
            tPBSError[12].ErrNo = -1;
            tPBSError[12].ErrMsg= "barrier door open";
            sCmd = "72";
            sErrMsg = tPBSError[12].ErrMsg;
            break;
        }
        case BDoorNoError:
        {
            tPBSError[12].ErrNo = 0;
            tPBSError[12].ErrMsg= "barrier door close";
            sCmd = "72";
            sErrMsg = tPBSError[12].ErrMsg;
            break;
        }
        case ReaderNoError:
        {
            tPBSError[iReader].ErrNo = 0;
            sCmd= "05";
            tPBSError[iReader].ErrMsg= "Card Reader OK";
            break;
        }
        case ReaderError:
        {
            tPBSError[iReader].ErrNo = -1;
            sCmd = "05";
            tPBSError[iReader].ErrMsg= "Card Reader Error";
            break;
        }
        default:
            break;
    }
    
    if (sErrMsg != "" ){
        writelog (sErrMsg, "OPR");
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
    else if (operation::getInstance()->tProcess.gbLoopAIsOn == true && operation::getInstance()->tProcess.gbLorrySensorIsOn == true)
    {
        ret = 4;
    }
    return ret;

}

void operation::SaveEntry()
{
    int iRet;
    std::string sLPRNo = "";
    
    if (tEntry.sIUTKNo== "") return;
    writelog ("Save Entry trans:"+ tEntry.sIUTKNo, "OPR");

    iRet = db::getInstance()->insertentrytrans(tEntry);
    //----
    if (iRet == iDBSuccess)
    {
        tProcess.setLastIUNo(tEntry.sIUTKNo);
        tProcess.setLastIUEntryTime(std::chrono::steady_clock::now());
    }
    //-------
    tPBSError[iDB].ErrNo = (iRet == iDBSuccess) ? 0 : (iRet == iCentralFail) ? -1 : -2;

    if ((tEntry.sLPN[0] != "")|| (tEntry.sLPN[1] != ""))
	{
		if((tEntry.iTransType == 7) || (tEntry.iTransType == 8) || (tEntry.iTransType == 22))
		{
			sLPRNo = tEntry.sLPN[1];
		}
		else
		{
			sLPRNo = tEntry.sLPN[0];
		}
	}

    std::string sMsg2Send = (iRet == iDBSuccess) ? "Entry OK" : (iRet == iCentralFail) ? "Entry Central Failed" : "Entry Local Failed";

    sMsg2Send = tEntry.sIUTKNo + ",,," + sLPRNo + "," + std::to_string(tEntry.giShowType) + "," + sMsg2Send;

    if (tEntry.iStatus == 0) {
        SendMsg2Server("90", sMsg2Send);
    }
    tProcess.gbsavedtrans = true;
}

void operation::ShowTotalLots(std::string totallots, std::string LEDId)
{
    if (LEDManager::getInstance()->getLED(getSerialPort(std::to_string(tParas.giCommportLED401))) != nullptr)
    {
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
                sMsg = tMsg.Msg_SeasonInvalid[0];
                sLCD = tMsg.Msg_SeasonInvalid[1];
                break;
            case 1:  
                if (gtStation.iType == tientry) {
                    sMsg = tMsg.Msg_ValidSeason[0];
                    sLCD = tMsg.Msg_ValidSeason[1];
                } 
                break;
            case 2: 
                sMsg = tMsg.Msg_SeasonExpired[0];
                sLCD = tMsg.Msg_SeasonExpired[1];
                writelog("Season Expired", "OPR");
                break;
            case 3: 
                sMsg = tMsg.Msg_SeasonTerminated[0];
                sLCD = tMsg.Msg_SeasonTerminated[1];
                writelog ("Season terminated", "OPR");
                break;
            case 4:  
                sMsg = tMsg.Msg_SeasonBlocked[0];
                sLCD = tMsg.Msg_SeasonBlocked[1];
                writelog ("Season Blocked", "OPR");
                break;
            case 5:  
                sMsg = tMsg.Msg_SeasonInvalid[0];
                sLCD = tMsg.Msg_SeasonInvalid[1];
                writelog ("Season Lost", "OPR");
                break;
            case 6:
                sMsg = tMsg.Msg_SeasonPassback[0];
                sLCD = tMsg.Msg_SeasonPassback[1];
                writelog ("Season Passback", "OPR");
                break;
            case 7:  
                sMsg = tMsg.Msg_SeasonNotStart[0];
                sLCD = tMsg.Msg_SeasonNotStart[1];
                writelog ("Season Not Start", "OPR");
                break;
            case 8: 
                sMsg = "Wrong Season Type";
                sLCD = "Wrong Season Type";
                writelog ("Wrong Season Type", "OPR");
                break;
            case 10:     
                sMsg = tMsg.Msg_SeasonAsHourly[0];
                sLCD = tMsg.Msg_SeasonAsHourly[1];
                writelog ("Season As Hourly", "OPR");
                break;
            case 11:     
                sMsg = tMsg.Msg_ESeasonWithinAllowance[0];
                sLCD = tMsg.Msg_ESeasonWithinAllowance[1];
                writelog ("Season within allowance", "OPR");
                break;
            case 12:
               // sMsg = gsMsgMasterSeason
               // sLCD = gscMsgMasterSeason
                writelog ("Master Season", "OPR");
                break;
            case 13:     
                sMsg = tMsg.Msg_WholeDayParking[0];
                sLCD = tMsg.Msg_WholeDayParking[1];
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
        
        if (iReturn != 1) std::this_thread::sleep_for(std::chrono::milliseconds(200));

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

void operation::ManualCloseBarrier()
{
    writelog ("Manual Close barrier.", "OPR");
    m_db->AddRemoteControl(std::to_string(gtStation.iSID),"Manual close barrier","");
    
    if (tParas.gsBarrierPulse == 0){tParas.gsBarrierPulse = 500;}

    DIO::getInstance()->FnSetCloseBarrier(1);
    //
    std::this_thread::sleep_for(std::chrono::milliseconds(tParas.gsBarrierPulse));
    //
    DIO::getInstance()->FnSetCloseBarrier(0);
}

int operation:: GetSeasonTransType(int VehicleType, int SeasonType, int TransType)
 {
    // VehicleType: 0=car, 1=lorry, 2=motorcycle
    // SeasonType(rate_type of season_mst): 3=Day, 4=Night, 5=GRO, 6=Park & Ride
    // TransType: TransType for Wholeday season

    //writelog ("Season Rate Type:"+std::to_string(SeasonType),"OPR");

    if (SeasonType == 3) {
        if (VehicleType == 0) {
            return 50; // car day season
        } else if (VehicleType == 1) {
            return 51; // Lorry day season
        } else if (VehicleType == 2) {
            return 52; // motorcycle day season
        }
    } else if (SeasonType == 4) {
        if (VehicleType == 0) {
            return 53; // car night season
        } else if (VehicleType == 1) {
            return 54; // Lorry night season
        } else if (VehicleType == 2) {
            return 55; // motorcycle night season
        }
    } else if (SeasonType == 5) {
        if (VehicleType == 0) {
            return 56; // car GRO season
        } else if (VehicleType == 1) {
            return 57; // Lorry GRO season
        } else if (VehicleType == 2) {
            return 58; // motorcycle GRO season
        }
    } else if (SeasonType == 6) {
        if (VehicleType == 0) {
            return 59; // Car Park Ride Season
        } else if (VehicleType == 1) {
            return 60; // Lorry Park Ride season
        } else if (VehicleType == 2) {
            return 61; // motorcycle Park Ride Season
        }
    } else if (SeasonType == 7) {
        if (VehicleType == 0 || VehicleType == 14) {
            return 62; // Car Handicapped Season
        } else if (VehicleType == 1) {
            return 63; // Lorry Handicapped season
        } else if (VehicleType == 2) {
            return 64; // motorcycle Handicapped Season
        }
    } else if (SeasonType == 8) {
        if (VehicleType == 0) {
            return 65; // Staff A Car
        } else if (VehicleType == 1) {
            return 66; // staff A Lorry
        } else if (VehicleType == 2) {
            return 67; // Staff A Motorcycle
        }
    } else if (SeasonType == 9) {
        if (VehicleType == 0) {
            return 68; // Staff B Car
        } else if (VehicleType == 1) {
            return 69; // staff B Lorry
        } else if (VehicleType == 2) {
            return 70; // Staff B Motorcycle
        }
    } else if (SeasonType == 10) {
        if (VehicleType == 0) {
            return 71; // Staff C Car
        } else if (VehicleType == 1) {
            return 72; // staff C Lorry
        } else if (VehicleType == 2) {
            return 73; // Staff C Motorcycle
        }
    } else if (SeasonType == 11) {
        if (VehicleType == 0) {
            return 74; // Staff D Car
        } else if (VehicleType == 1) {
            return 75; // staff D Lorry
        } else if (VehicleType == 2) {
            return 76; // Staff D Motorcycle
        }
    } else if (SeasonType == 12) {
        if (VehicleType == 0) {
            return 77; // Staff E Car
        } else if (VehicleType == 1) {
            return 78; // staff E Lorry
        } else if (VehicleType == 2) {
            return 79; // Staff E Motorcycle
        }
    } else if (SeasonType == 13) {
        if (VehicleType == 0) {
            return 80; // Staff F Car
        } else if (VehicleType == 1) {
            return 81; // staff F Lorry
        } else if (VehicleType == 2) {
            return 82; // Staff F Motorcycle
        }
    } else if (SeasonType == 14) {
        if (VehicleType == 0) {
            return 83; // Staff G Car
        } else if (VehicleType == 1) {
            return 84; // staff G Lorry
        } else if (VehicleType == 2) {
            return 85; // Staff G Motorcycle
        }
    } else if (SeasonType == 15) {
        if (VehicleType == 0) {
            return 86; // Staff H Car
        } else if (VehicleType == 1) {
            return 87; // staff H Lorry
        } else if (VehicleType == 2) {
            return 88; // Staff H Motorcycle
        }
    } else if (SeasonType == 16) {
        if (VehicleType == 0) {
            return 89; // Staff I Car
        } else if (VehicleType == 1) {
            return 90; // staff I Lorry
        } else if (VehicleType == 2) {
            return 91; // Staff I Motorcycle
        }
    } else if (SeasonType == 17) {
        if (VehicleType == 0) {
            return 92; // Staff J Car
        } else if (VehicleType == 1) {
            return 93; // staff J Lorry
        } else if (VehicleType == 2) {
            return 94; // Staff J Motorcycle
        }
    } else if (SeasonType == 18) {
        if (VehicleType == 0) {
            return 95; // Staff K Car
        } else if (VehicleType == 1) {
            return 96; // staff K Lorry
        } else if (VehicleType == 2) {
            return 97; // Staff K Motorcycle
        }
    } else if (TransType == 9) {
        return 9;
    } 
    return TransType + 1; // Whole day season
}

void operation:: EnableCashcard(bool bEnable)
{
    if (bEnable == tEntry.sEnableReader) return;
    //-------
    tEntry.sEnableReader = bEnable;
    if (tParas.giCommPortLCSC > 0) EnableLCSC (bEnable);
    if (tParas.giCommPortKDEReader>0) EnableKDE(bEnable);
    if (tParas.giCommPortUPOS) EnableUPOS(bEnable);


}

void operation:: CheckReader()
{
    if (tPBSError[iReader].ErrNo == -1) {
        writelog("Check KDE Status ...", "OPR");
        if (KSM_Reader::getInstance()->FnKSMReaderSendInit() == 1)
        {
            HandlePBSError(ReaderNoError);
        }else{
            writelog ("KDE Reader Error","OPR");
        }
    }
    if (tParas.giCommPortLCSC > 0 && tPBSError[iLCSC].ErrNo != -4)
    {
        writelog("Check LCSC Status...", "OPR");
        LCSCReader::getInstance()->FnSendGetStatusCmd();
        if (tPBSError[iLCSC].ErrNo == -1)
        {
            tPBSError[iLCSC].ErrNo = 0;
        }
        LCSCReader::getInstance()->FnSendSetTime();
    }
}

void operation:: EnableLCSC(bool bEnable)
{
    int iRet;
    if (tPBSError[iLCSC].ErrNo != 0)
    {
        return;
    }

    if (bEnable) 
    {
        LCSCReader::getInstance()->FnSendGetCardIDCmd();
        writelog("Start LCSC to read...", "OPR");
    }
    else 
    {
        LCSCReader::getInstance()->FnLCSCReaderStopRead();
        writelog("Stop LCSC to Read...", "OPR");
    }
}



void operation::EnableKDE(bool bEnable)
{
    if (tPBSError[iReader].ErrNo != 0) return;
    //-----
    if (bEnable == false)
    {
            //------
        if (tProcess.giCardIsIn == 1 )
        {
            KSM_Reader::getInstance()->FnKSMReaderSendEjectToFront();
        }
        else
        {
            writelog ("Disable KDE Reader", "OPR");
            KSM_Reader::getInstance()->FnKSMReaderEnable(bEnable);
        }
    }
    else
    {
        writelog ("Enable KDE Reader", "OPR");
        if (KSM_Reader::getInstance()->FnKSMReaderEnable(bEnable) == -1)
        {
            if (tPBSError[iReader].ErrNo != -1)
            {
                HandlePBSError(ReaderError);
            }
        }
    }
}

void operation::EnableUPOS(bool bEnable)
{


}

void operation::ProcessLCSC(const std::string& eventData)
{
    int msg_status = static_cast<int>(LCSCReader::mCSCEvents::sWrongCmd);

    try
    {
        std::vector<std::string> subVector = Common::getInstance()->FnParseString(eventData, ',');
        for (unsigned int i = 0; i < subVector.size(); i++)
        {
            std::string pair = subVector[i];
            std::string param = Common::getInstance()->FnBiteString(pair, '=');
            std::string value = pair;

            if (param == "msgStatus")
            {
                msg_status = std::stoi(value);
            }
        }
    }
    catch (const std::exception& ex)
    {
        std::ostringstream oss;
        oss << "Exception : " << ex.what();
        writelog(oss.str(), "OPR");
    }

    switch (static_cast<LCSCReader::mCSCEvents>(msg_status))
    {
        case LCSCReader::mCSCEvents::sGetStatusOK:
        {
            int reader_mode = 0;
            try
            {
                std::vector<std::string> subVector = Common::getInstance()->FnParseString(eventData, ',');
                for (unsigned int i = 0; i < subVector.size(); i++)
                {
                    std::string pair = subVector[i];
                    std::string param = Common::getInstance()->FnBiteString(pair, '=');
                    std::string value = pair;

                    if (param == "readerMode")
                    {
                        reader_mode = std::stoi(value);
                    }
                }
            }
            catch (const std::exception& ex)
            {
                std::ostringstream oss;
                oss << "Exception : " << ex.what();
                writelog(oss.str(), "OPR");
            }

            if (reader_mode != 1)
            {
                LCSCReader::getInstance()->FnSendGetLoginCmd();
                writelog ("LCSC Reader Error","OPR");
                HandlePBSError(LCSCError);
            }

            break;
        }
        case LCSCReader::mCSCEvents::sLoginSuccess:
        {
            break;
        }
        case LCSCReader::mCSCEvents::sLogoutSuccess:
        {
            break;
        }
        case LCSCReader::mCSCEvents::sGetIDSuccess:
        {
            writelog ("event LCSC got card ID.","OPR");
            HandlePBSError (LCSCNoError);

            std::string sCardNo = "";
            
            try
            {
                std::vector<std::string> subVector = Common::getInstance()->FnParseString(eventData, ',');
                for (unsigned int i = 0; i < subVector.size(); i++)
                {
                    std::string pair = subVector[i];
                    std::string param = Common::getInstance()->FnBiteString(pair, '=');
                    std::string value = pair;

                    if (param == "CAN")
                    {
                        sCardNo = value;
                    }
                }
            }
            catch (const std::exception& ex)
            {
                std::ostringstream oss;
                oss << "Exception : " << ex.what();
                writelog(oss.str(), "OPR");
            }

            writelog ("LCSC card: " + sCardNo, "OPR");

            if (sCardNo.length() != 16)
            {
                writelog ("Wrong Card No: "+sCardNo, "OPR");
                ShowLEDMsg(tMsg.Msg_CardReadingError[0], tMsg.Msg_CardReadingError[1]);
                SendMsg2Server ("90", sCardNo + ",,,,,Wrong Card No");
                EnableLCSC(true);
            }
            else
            {
                if (tEntry.sIUTKNo == "")
                {
                    EnableCashcard(false);
                    VehicleCome(sCardNo);
                }
            }

            break;
        }
        case LCSCReader::mCSCEvents::sGetBlcSuccess:
        {
            break;
        }
        case LCSCReader::mCSCEvents::sGetTimeSuccess:
        {
            break;
        }
        case LCSCReader::mCSCEvents::sGetDeductSuccess:
        {
            break;
        }
        case LCSCReader::mCSCEvents::sGetCardRecord:
        {
            break;
        }
        case LCSCReader::mCSCEvents::sCardFlushed:
        {
            break;
        }
        case LCSCReader::mCSCEvents::sSetTimeSuccess:
        {
            break;
        }
        case LCSCReader::mCSCEvents::sLogin1Success:
        {
            break;
        }
        case LCSCReader::mCSCEvents::sRSAUploadSuccess:
        {
            break;
        }
        case LCSCReader::mCSCEvents::sFWUploadSuccess:
        {
            break;
        }
        case LCSCReader::mCSCEvents::sBLUploadSuccess:
        case LCSCReader::mCSCEvents::sCILUploadSuccess:
        case LCSCReader::mCSCEvents::sCFGUploadSuccess:
        case LCSCReader::mCSCEvents::sBLUploadCorrupt:
        case LCSCReader::mCSCEvents::sCILUploadCorrupt:
        case LCSCReader::mCSCEvents::sCFGUploadCorrupt:
        {
            writelog("Received ACK from LCSC.", "OPR");
            break;
        }
        case LCSCReader::mCSCEvents::iFailWriteSettle:
        {
            writelog("Write LCSC settle file failed.","OPR");
            break;
        }
        case LCSCReader::mCSCEvents::sNoCard:
        {
            if (tEntry.gbEntryOK == false)
            {
                writelog("Received No card Event", "OPR");
            }
            break;
        }
        default:
        {
            std::string sCardNo = "";
            writelog ("Received Error Event" + std::to_string(static_cast<int>(msg_status)), "OPR");
            if (tProcess.gbLoopApresent.load() == true && tEntry.gbEntryOK == false)
            {
                ShowLEDMsg(tMsg.Msg_CardReadingError[0], tMsg.Msg_CardReadingError[1]);
                SendMsg2Server ("90", sCardNo + ",,,,,Wrong Card No");
                EnableLCSC(true);
            }
            else
            {
                EnableCashcard(false);
            }
            break;
        }
    }
}

void operation:: KSM_CardIn()
{
    int iRet;
    //--------
     ShowLEDMsg ("Card In^Please Wait ...", "Card In^Please Wait ...");
    //--------
    if (tPBSError[iReader].ErrNo != 0) {HandlePBSError(ReaderNoError);}
    //---------
    tProcess.giCardIsIn = 1;
    iRet =  KSM_Reader::getInstance()->FnKSMReaderReadCardInfo();
    if (iRet != 1) {
        ShowLEDMsg (tMsg.Msg_CardReadingError[0], tMsg.Msg_CardReadingError[1]);
        SendMsg2Server ("90", ",,,,,Wrong Card Insertion");
        KSM_Reader::getInstance()->FnKSMReaderSendEjectToFront();
    }
}

void operation:: KSM_CardInfo(string sKSMCardNo, long sKSMCardBal, bool sKSMCardExpired)
 {  
    
    writelog ("Cashcard: " + sKSMCardNo, "OPR");
    //------
    tPBSError[iReader].ErrNo = 0;
    if (sKSMCardNo == "" || sKSMCardNo.length()!= 16 || sKSMCardNo.substr(5,4) == "0005") {
        ShowLEDMsg (tMsg.Msg_CardReadingError[0], tMsg.Msg_CardReadingError[1]);
        SendMsg2Server ("90", ",,,,,Wrong Card Insertion");
        KSM_Reader::getInstance()->FnKSMReaderSendEjectToFront();
        return;
    }
    else { 
        if (tEntry.sIUTKNo == "") VehicleCome(sKSMCardNo);
        else  KSM_Reader::getInstance()->FnKSMReaderSendEjectToFront();
    }
 }

 void operation:: KSM_CardTakeAway()
{
    writelog ("Card Take Away ", "PBS");
    tProcess.giCardIsIn = 2;

    if (tEntry.gbEntryOK == true)
    {
        ShowLEDMsg(tMsg.Msg_CardTaken[0],tMsg.Msg_CardTaken[1]);
    }
    else
    {
        if (tProcess.gbcarparkfull.load() == true && tEntry.sIUTKNo != "" ) return;
        ShowLEDMsg(tMsg.Msg_InsertCashcard[0], tMsg.Msg_InsertCashcard[1]);   
    }                    
    //--------
    if (gtStation.iType == tientry)
    {
        if (tEntry.gbEntryOK == true)
        {
            Openbarrier();
            EnableCashcard(false);
        }
    }
}

bool operation::AntennaOK() {
    
    if (tParas.giEPS == 0) {
        writelog ("Antenna: Non-EPS", "OPR");
        return false;
    } else {
        if (tParas.giCommPortAntenna == 0) {
            writelog("Antenna: Commport Not Set", "OPR");
            return false;
        } else {
            if (tPBSError[iAntenna].ErrNo == 0) {
                writelog("Antenna: OK", "OPR");
                return true;
            } else {
                writelog("Antenna: Error=" + tPBSError[iAntenna].ErrMsg, "OPR");
                return false;
            }
        }
    }
}

void operation::ReceivedLPR(Lpr::CType CType,string LPN, string sTransid, string sImageLocation)
{
    writelog ("Received Trans ID: "+sTransid + " LPN: "+ LPN ,"OPR");
    writelog ("Send Trans ID: "+ tEntry.gsTransID, "OPR");

    int i = static_cast<int>(CType);

    if (tEntry.gsTransID == sTransid && tProcess.gbLoopApresent.load() == true && tProcess.gbsavedtrans == false)
    {
       if (gtStation.iType == tientry)  tEntry.sLPN[i]=LPN;
    }
    else
    {
        if (gtStation.iType == tientry) db::getInstance()->updateEntryTrans(LPN,sTransid);
    }
}

void operation::processUPT(Upt::UPT_CMD cmd, const std::string& eventData)
{
    uint32_t msg_status = static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED);

    try
    {
        std::vector<std::string> subVector = Common::getInstance()->FnParseString(eventData, ',');
        for (unsigned int i = 0; i < subVector.size(); i++)
        {
            std::string pair = subVector[i];
            std::string param = Common::getInstance()->FnBiteString(pair, '=');
            std::string value = pair;

            if (param == "msgStatus")
            {
                msg_status = static_cast<uint32_t>(std::stoul(value));
            }
        }
    }
    catch (const std::exception& ex)
    {
        std::ostringstream oss;
        oss << "Exception : " << ex.what();
        writelog(oss.str(), "OPR");
    }

    switch (cmd)
    {
        case Upt::UPT_CMD::DEVICE_STATUS_REQUEST:
        {
            if (msg_status != static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED))
            {
                std::ostringstream oss;
                oss << "DEVICE_STATUS_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status);

                if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
                {
                    // Handle the cmd request response succeed
                }
                else
                {
                    // Handle the cmd request response failed
                }

                writelog(oss.str(), "OPR");
            }
            else
            {
                std::ostringstream oss;
                oss << "DEVICE_STATUS_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status) << " (PARSE_FAILED)";
                writelog(oss.str(), "OPR");
            }
            break;
        }
        case Upt::UPT_CMD::DEVICE_RESET_REQUEST:
        {
            if (msg_status != static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED))
            {
                std::ostringstream oss;
                oss << "DEVICE_RESET_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status);

                if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
                {
                    // Handle the cmd request response succeed
                }
                else
                {
                    // Handle the cmd request response failed
                }

                writelog(oss.str(), "OPR");
            }
            else
            {
                std::ostringstream oss;
                oss << "DEVICE_RESET_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status) << " (PARSE_FAILED)";
                writelog(oss.str(), "OPR");
            }
            break;
        }
        case Upt::UPT_CMD::DEVICE_TIME_SYNC_REQUEST:
        {
            if (msg_status != static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED))
            {
                std::ostringstream oss;
                oss << "DEVICE_TIME_SYNC_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status);

                if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
                {
                    // Handle the cmd request response succeed
                }
                else
                {
                    // Handle the cmd request response failed
                }

                writelog(oss.str(), "OPR");
            }
            else
            {
                std::ostringstream oss;
                oss << "DEVICE_TIME_SYNC_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status) << " (PARSE_FAILED)";
                writelog(oss.str(), "OPR");
            }
            break;
        }
        case Upt::UPT_CMD::DEVICE_LOGON_REQUEST:
        {
            if (msg_status != static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED))
            {
                std::ostringstream oss;
                oss << "DEVICE_LOGON_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status);

                if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
                {
                    // Handle the cmd request response succeed
                }
                else
                {
                    // Handle the cmd request response failed
                }

                writelog(oss.str(), "OPR");
            }
            else
            {
                std::ostringstream oss;
                oss << "DEVICE_LOGON_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status) << " (PARSE_FAILED)";
                writelog(oss.str(), "OPR");
            }
            break;
        }
        case Upt::UPT_CMD::DEVICE_TMS_REQUEST:
        {
            if (msg_status != static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED))
            {
                std::ostringstream oss;
                oss << "DEVICE_TMS_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status);

                if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
                {
                    // Handle the cmd request response succeed
                }
                else
                {
                    // Handle the cmd request response failed
                }

                writelog(oss.str(), "OPR");
            }
            else
            {
                std::ostringstream oss;
                oss << "DEVICE_TMS_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status) << " (PARSE_FAILED)";
                writelog(oss.str(), "OPR");
            }
            break;
        }
        case Upt::UPT_CMD::DEVICE_SETTLEMENT_REQUEST:
        {
            if (msg_status != static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED))
            {
                std::ostringstream oss;
                oss << "DEVICE_SETTLEMENT_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status);

                if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
                {
                    // Handle the cmd request response succeed
                    uint64_t total_amount = 0;
                    uint64_t total_trans_count = 0;
                    std::string TID = "";
                    std::string MID = "";

                    try
                    {
                        std::vector<std::string> subVector = Common::getInstance()->FnParseString(eventData, ',');
                        for (unsigned int i = 0; i < subVector.size(); i++)
                        {
                            std::string pair = subVector[i];
                            std::string param = Common::getInstance()->FnBiteString(pair, '=');
                            std::string value = pair;

                            if (param == "totalAmount")
                            {
                                total_amount = std::stoull(value);
                            }
                            else if (param == "totalTransCount")
                            {
                                total_trans_count = std::stoull(value);
                            }
                            else if (param == "TID")
                            {
                                TID = value;
                            }
                            else if (param == "MID")
                            {
                                MID = value;
                            }
                        }

                        oss  << " | total amount : " << total_amount << " | total trans count : " << total_trans_count << " | TID : " << TID << " | MID : " << MID;
                    }
                    catch (const std::exception& ex)
                    {
                        oss << " | Exception : " << ex.what();
                    }
                }
                else
                {
                    // Handle the cmd request response failed
                }

                writelog(oss.str(), "OPR");
            }
            else
            {
                std::ostringstream oss;
                oss << "DEVICE_SETTLEMENT_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status) << " (PARSE_FAILED)";
                writelog(oss.str(), "OPR");
            }
            break;
        }
        case Upt::UPT_CMD::DEVICE_RETRIEVE_LAST_SETTLEMENT_REQUEST:
        {
            if (msg_status != static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED))
            {
                std::ostringstream oss;
                oss << "DEVICE_RETRIEVE_LAST_SETTLEMENT_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status);

                if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
                {
                    // Handle the cmd request response succeed
                    uint64_t total_amount = 0;
                    uint64_t total_trans_count = 0;

                    try
                    {
                        std::vector<std::string> subVector = Common::getInstance()->FnParseString(eventData, ',');
                        for (unsigned int i = 0; i < subVector.size(); i++)
                        {
                            std::string pair = subVector[i];
                            std::string param = Common::getInstance()->FnBiteString(pair, '=');
                            std::string value = pair;

                            if (param == "totalAmount")
                            {
                                total_amount = std::stoull(value);
                            }
                            else if (param == "totalTransCount")
                            {
                                total_trans_count = std::stoull(value);
                            }
                        }

                        oss << " | total amount : " << total_amount << " | total trans count : " << total_trans_count;
                    }
                    catch (const std::exception& ex)
                    {
                        oss << " | Exception : " << ex.what();
                    }
                }
                else
                {
                    // Handle the cmd request response failed
                }

                writelog(oss.str(), "OPR");
            }
            else
            {
                std::ostringstream oss;
                oss << "DEVICE_RETRIEVE_LAST_SETTLEMENT_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status) << " (PARSE_FAILED)";
                writelog(oss.str(), "OPR");
            }
            break;
        }
        case Upt::UPT_CMD::CARD_DETECT_REQUEST:
        {
            if (msg_status != static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED))
            {
                std::ostringstream oss;
                oss << "CARD_DETECT_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status);

                if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
                {
                    // Handle the cmd request response succeed
                    std::string card_type = "";
                    std::string card_can = "";
                    uint64_t card_balance = 0;

                    try
                    {
                        std::vector<std::string> subVector = Common::getInstance()->FnParseString(eventData, ',');
                        for (unsigned int i = 0; i < subVector.size(); i++)
                        {
                            std::string pair = subVector[i];
                            std::string param = Common::getInstance()->FnBiteString(pair, '=');
                            std::string value = pair;

                            if (param == "cardType")
                            {
                                card_type = value;
                            }
                            else if (param == "cardCan")
                            {
                                card_can = value;
                            }
                            else if (param == "cardBalance")
                            {
                                card_balance = std::stoull(value);
                            }
                        }

                        oss << " | card type : " << card_type << " | card can : " << card_can << " | card balance : " << card_balance;
                    }
                    catch (const std::exception& ex)
                    {
                        oss << " | Exception : " << ex.what();
                    }
                }
                else
                {
                    // Handle the cmd request response failed
                }

                writelog(oss.str(), "OPR");
            }
            else
            {
                std::ostringstream oss;
                oss << "CARD_DETECT_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status) << " (PARSE_FAILED)";
                writelog(oss.str(), "OPR");
            }
            break;
        }
        case Upt::UPT_CMD::PAYMENT_MODE_AUTO_REQUEST:
        {
            if (msg_status != static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED))
            {
                std::ostringstream oss;
                oss << "PAYMENT_MODE_AUTO_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status);

                if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
                {
                    // Handle the cmd request response succeed
                    std::string card_can = "";
                    uint64_t card_fee = 0;
                    uint64_t card_balance = 0;
                    std::string card_reference_no = "";
                    std::string card_batch_no = "";

                    try
                    {
                        std::vector<std::string> subVector = Common::getInstance()->FnParseString(eventData, ',');
                        for (unsigned int i = 0; i < subVector.size(); i++)
                        {
                            std::string pair = subVector[i];
                            std::string param = Common::getInstance()->FnBiteString(pair, '=');
                            std::string value = pair;

                            if (param == "cardCan")
                            {
                                card_can = value;
                            }
                            else if (param == "cardFee")
                            {
                                card_fee = std::stoull(value);
                            }
                            else if (param == "cardBalance")
                            {
                                card_balance = std::stoull(value);
                            }
                            else if (param == "cardReferenceNo")
                            {
                                card_reference_no = value;
                            }
                            else if (param == "cardBatchNo")
                            {
                                card_batch_no = value;
                            }
                        }

                        oss << " | card can : " << card_can << " | card fee : " << card_fee << " | card balance : " << card_balance << " | card reference no : " << card_reference_no << " | card batch no : " << card_batch_no;
                    }
                    catch (const std::exception& ex)
                    {
                        oss << " | Exception : " << ex.what();
                    }
                }
                else
                {
                    // Handle the cmd request response failed
                }

                writelog(oss.str(), "OPR");
            }
            else
            {
                std::ostringstream oss;
                oss << "PAYMENT_MODE_AUTO_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status) << " (PARSE_FAILED)";
                writelog(oss.str(), "OPR");
            }
            break;
        }
        case Upt::UPT_CMD::CANCEL_COMMAND_REQUEST:
        {
            if (msg_status != static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED))
            {
                std::ostringstream oss;
                oss << "CANCEL_COMMAND_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status);

                if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
                {
                    // Handle the cmd request response succeed
                }
                else
                {
                    // Handle the cmd request response failed
                }

                writelog(oss.str(), "OPR");
            }
            else
            {
                std::ostringstream oss;
                oss << "CANCEL_COMMAND_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status) << " (PARSE_FAILED)";
                writelog(oss.str(), "OPR");
            }
            break;
        }
        case Upt::UPT_CMD::TOP_UP_NETS_NCC_BY_NETS_EFT_REQUEST:
        case Upt::UPT_CMD::TOP_UP_NETS_NFP_BY_NETS_EFT_REQUEST:
        case Upt::UPT_CMD::DEVICE_RETRIEVE_LAST_TRANSACTION_STATUS_REQUEST:
        case Upt::UPT_CMD::DEVICE_RESET_SEQUENCE_NUMBER_REQUEST:
        case Upt::UPT_CMD::DEVICE_PROFILE_REQUEST:
        case Upt::UPT_CMD::DEVICE_SOF_LIST_REQUEST:
        case Upt::UPT_CMD::DEVICE_SOF_SET_PRIORITY_REQUEST:
        case Upt::UPT_CMD::DEVICE_PRE_SETTLEMENT_REQUEST:
        case Upt::UPT_CMD::MUTUAL_AUTHENTICATION_STEP_1_REQUEST:
        case Upt::UPT_CMD::MUTUAL_AUTHENTICATION_STEP_2_REQUEST:
        case Upt::UPT_CMD::CARD_DETAIL_REQUEST:
        case Upt::UPT_CMD::CARD_HISTORICAL_LOG_REQUEST:
        case Upt::UPT_CMD::PAYMENT_MODE_EFT_REQUEST:
        case Upt::UPT_CMD::PAYMENT_MODE_EFT_NETS_QR_REQUEST:
        case Upt::UPT_CMD::PAYMENT_MODE_BCA_REQUEST:
        case Upt::UPT_CMD::PAYMENT_MODE_CREDIT_CARD_REQUEST:
        case Upt::UPT_CMD::PAYMENT_MODE_NCC_REQUEST:
        case Upt::UPT_CMD::PAYMENT_MODE_NFP_REQUEST:
        case Upt::UPT_CMD::PAYMENT_MODE_EZ_LINK_REQUEST:
        case Upt::UPT_CMD::PRE_AUTHORIZATION_REQUEST:
        case Upt::UPT_CMD::PRE_AUTHORIZATION_COMPLETION_REQUEST:
        case Upt::UPT_CMD::INSTALLATION_REQUEST:
        case Upt::UPT_CMD::VOID_PAYMENT_REQUEST:
        case Upt::UPT_CMD::REFUND_REQUEST:
        case Upt::UPT_CMD::CASE_DEPOSIT_REQUEST:
        case Upt::UPT_CMD::UOB_REQUEST:
        {
            // Note: Not implemeted. Possible implement in future.
            break;
        }
    }
}
