#include <iostream>
#include <string>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include "ini_parser.h"

IniParser* IniParser::iniParser_;

IniParser::IniParser()
{

}

IniParser* IniParser::getInstance()
{
    if (iniParser_ == nullptr)
    {
        iniParser_ = new IniParser();
    }
    return iniParser_;
}

void IniParser::FnReadIniFile()
{
    boost::property_tree::ptree pt;
    boost::property_tree::ini_parser::read_ini(INI_FILE, pt);

    // Temp: Revisit and implement storing to private variable function
    StationID_                      = pt.get<std::string>("setting.StationID", "");
    HasInternalLink_                = pt.get<std::string>("setting.HasInternalLink", "");
    InternalDefaultLEDMsg_          = pt.get<std::string>("setting.InternalDefaultLEDMsg", "");
    InternalNoNightParkingMsg1_     = pt.get<std::string>("setting.InternalNoNightParkingMsg1", "");
    InternalNoNightParkingMsg2_     = pt.get<std::string>("setting.InternalNoNightParkingMsg2", "");
    HasInternalCarpark_             = pt.get<std::string>("setting.HasInternalCarpark", "");
    AttachedDBServer_               = pt.get<std::string>("setting.AttachedDBServer", "");
    AttachedDBName_                 = pt.get<std::string>("setting.AttachedDBName", "");
    AttachedExitUDPPort_            = pt.get<std::string>("setting.AttachedExitUDPPort", "");
    AttachedExitID_                 = pt.get<std::string>("setting.AttachedExitID", "");
    IsOldPass_                      = pt.get<std::string>("setting.IsOldPass", "");
    HDBLotAdjustTimer_              = pt.get<std::string>("setting.HDBLotAdjustTimer", "");
    IsLEDBlinking_                  = pt.get<std::string>("setting.IsLEDBlinking", "");
    SQLServerPort_                  = pt.get<std::string>("setting.SQLServerPort", "");
    LastIUTimeout_                  = pt.get<std::string>("setting.LastIUTimeout", "");
    LogFolder_                      = pt.get<std::string>("setting.LogFolder", "");
    LocalDB_                        = pt.get<std::string>("setting.LocalDB", "");
    LastSerialNo_                   = pt.get<std::string>("setting.LastSerialNo", "");
    LastRedemptNo_                  = pt.get<std::string>("setting.LastRedemptNo", "");
    TicketStatus_                   = pt.get<std::string>("setting.TicketStatus", "");
    DisplayType_                    = pt.get<std::string>("setting.DisplayType", "");
    CommPortDisplay_                = pt.get<std::string>("setting.CommPortDisplay", "");
    CommPortIU_                     = pt.get<std::string>("setting.CommPortIU", "");
    EPS_                            = pt.get<std::string>("setting.EPS", "");
    TaxiControl_                    = pt.get<std::string>("setting.TaxiControl", "");
    STID2_                          = pt.get<std::string>("setting.STID2", "");
    LoadingBay_                     = pt.get<std::string>("setting.LoadingBay", "");
    LBLockTime_                     = pt.get<std::string>("setting.LBLockTime", "");
    EPSWithReader_                  = pt.get<std::string>("setting.EPSWithReader", "");
    LocalUDPPort_                   = pt.get<std::string>("setting.LocalUDPPort", "");
    RemoteUDPPort_                  = pt.get<std::string>("setting.RemoteUDPPort", "");
    EntryDebit_                     = pt.get<std::string>("setting.EntryDebit", "");
    ShowTime_                       = pt.get<std::string>("setting.ShowTime", "");
    LEDShowTime_                    = pt.get<std::string>("setting.LEDShowTime", "");
    SeasonOnly_                     = pt.get<std::string>("setting.SeasonOnly", "");
    NotAllowHourly_                 = pt.get<std::string>("setting.NotAllowHourly", "");
    SaveSeason2Entry_               = pt.get<std::string>("setting.SaveSeason2Entry", "");
    WithTicket_                     = pt.get<std::string>("setting.WithTicket", "");
    NotRecordMotorcycle_            = pt.get<std::string>("setting.NotRecordMotorcycle", "");
    StartupOption_                  = pt.get<std::string>("setting.StartupOption", "");
    LastErrorTime_                  = pt.get<std::string>("setting.LastErrorTime", "");
    SecondID_                       = pt.get<std::string>("setting.SecondID", "");
    ERP2Server_                     = pt.get<std::string>("setting.ERP2Server", "");
    ERP2Port_                       = pt.get<std::string>("setting.ERP2Port", "");
    ForceShiftPartialEPS_           = pt.get<std::string>("setting.ForceShiftPartialEPS", "");
    IsChinaBReader_                 = pt.get<std::string>("setting.IsChinaBReader", "");
    CommPortAnt1_                   = pt.get<std::string>("setting.CommPortAnt1", "");
    CommPortAnt2_                   = pt.get<std::string>("setting.CommPortAnt2", "");
    CommPortAnt3_                   = pt.get<std::string>("setting.CommPortAnt3", "");
    CommPortAnt4_                   = pt.get<std::string>("setting.CommPortAnt4", "");
    CHUTermID_                      = pt.get<std::string>("setting.CHUTermID", "");
    DIOPort4Barrier_                = pt.get<std::string>("setting.DIOPort4Barrier", "");
    DIOPort4StationDoorOpen_        = pt.get<std::string>("setting.DIOPort4StationDoorOpen", "");
    DIOPort4BarrierDoorOpen_        = pt.get<std::string>("setting.DIOPort4BarrierDoorOpen", "");
    LPRIP4Front_                    = pt.get<std::string>("setting.LPRIP4Front", "");
    LPRIP4Rear_                     = pt.get<std::string>("setting.LPRIP4Rear", "");
    WaitLPRNoTime_                  = pt.get<std::string>("setting.WaitLPRNoTime", "");
    DIOPort4TrafficLight_           = pt.get<std::string>("setting.DIOPort4TrafficLight", "");
    DIOPort4EnableLoopA_            = pt.get<std::string>("setting.DIOPort4EnableLoopA", "");
    DIOPort4EnableLoopD_            = pt.get<std::string>("setting.DIOPort4EnableLoopD", "");
    DIOPort4Switch_                 = pt.get<std::string>("setting.DIOPort4Switch", "");
    NexpaDBServer_                  = pt.get<std::string>("setting.NexpaDBServer", "");
    NexpaDBName_                    = pt.get<std::string>("setting.NexpaDBName", "");
    HasPremiumParking_              = pt.get<std::string>("setting.HasPremiumParking", "");
    IntervalloopCIU_                = pt.get<std::string>("setting.IntervalloopCIU", "");
    TGDServer_                      = pt.get<std::string>("setting.TGDServer", "");
    IsTesting_                      = pt.get<std::string>("setting.IsTesting", "");
    TimeWaitForSecondID_            = pt.get<std::string>("setting.TimeWaitForSecondID", "");
    WaitForPressHandicapButtonTime_ = pt.get<std::string>("setting.WaitForPressHandicapButtonTime", "");
    DIOPort4LoopD_                  = pt.get<std::string>("setting.DIOPort4LoopD", "");
    CommPortControllerIO_           = pt.get<std::string>("setting.CommPortControllerIO", "");
    LPRErrorTime_                   = pt.get<std::string>("setting.LPRErrorTime", "");
    LPRErrorCount_                  = pt.get<std::string>("setting.LPRErrorCount", "");
    Location_                       = pt.get<std::string>("setting.Location", "");
    UVSSEnable_                     = pt.get<std::string>("setting.UVSSEnable", "");
    JPRMCServer_                    = pt.get<std::string>("setting.JPRMCServer", "");
    JPRMCPort_                      = pt.get<std::string>("setting.JPRMCPort", "");
    BlockIUPrefix_                  = pt.get<std::string>("setting.BlockIUPrefix", "");
    Q584PrinterComPort_             = pt.get<std::string>("setting.Q584PrinterComPort", "");
    LPRIP4Container_                = pt.get<std::string>("setting.LPRIP4Container", "");

    // Confirm
    AntennaID_                      = pt.get<int>("setting.AntennaId");
    WaitIUNoTime_                   = pt.get<int>("setting.WaitIUNoTime");
}

void IniParser::FnPrintIniFile()
{
    boost::property_tree::ptree pt;
    boost::property_tree::ini_parser::read_ini(INI_FILE, pt);

    for (const auto&section : pt)
    {
        const auto& section_name = section.first;
        const auto& section_properties = section.second;

        std::cout << "Section: " << section_name << std::endl;

        for (const auto& key : section_properties)
        {
            const auto& key_name = key.first;
            const auto& key_value = key.second.get_value<std::string>();

            std::cout << " Key: " << key_name << ", Value: "<< key_value << std::endl;
        }
    }
}

std::string IniParser::FnGetStationID() const
{
    return StationID_;
}

std::string IniParser::FnGetHasInternalLink() const
{
    return HasInternalLink_;
}

std::string IniParser::FnGetInternalDefaultLEDMsg() const
{
    return InternalDefaultLEDMsg_;
}

std::string IniParser::FnGetInternalNoNightParkingMsg1() const
{
    return InternalNoNightParkingMsg1_;
}

std::string IniParser::FnGetInternalNoNightParkingMsg2() const
{
    return InternalNoNightParkingMsg2_;
}

std::string IniParser::FnGetHasInternalCarpark() const
{
    return HasInternalCarpark_;
}

std::string IniParser::FnGetAttachedDBServer() const
{
    return AttachedDBServer_;
}

std::string IniParser::FnGetAttachedDBName() const
{
    return AttachedDBName_;
}

std::string IniParser::FnGetAttachedExitUDPPort() const
{
    return AttachedExitUDPPort_;
}

std::string IniParser::FnGetAttachedExitID() const
{
    return AttachedExitID_;
}

std::string IniParser::FnGetIsOldPass() const
{
    return IsOldPass_;
}

std::string IniParser::FnGetHDBLotAdjustTimer() const
{
    return HDBLotAdjustTimer_;
}

std::string IniParser::FnGetIsLEDBlinking() const
{
    return IsLEDBlinking_;
}

std::string IniParser::FnGetSQLServerPort() const
{
    return SQLServerPort_;
}

std::string IniParser::FnGetLastIUTimeout() const
{
    return LastIUTimeout_;
}

std::string IniParser::FnGetLogFolder() const
{
    return LogFolder_;
}

std::string IniParser::FnGetLocalDB() const
{
    return LocalDB_;
}

std::string IniParser::FnGetLastSerialNo() const
{
    return LastSerialNo_;
}

std::string IniParser::FnGetLastRedemptNo() const
{
    return LastRedemptNo_;
}

std::string IniParser::FnGetTicketStatus() const
{
    return TicketStatus_;
}

std::string IniParser::FnGetDisplayType() const
{
    return DisplayType_;
}

std::string IniParser::FnGetCommPortDisplay() const
{
    return CommPortDisplay_;
}

std::string IniParser::FnGetCommPortIU() const
{
    return CommPortIU_;
}

std::string IniParser::FnGetEPS() const
{
    return EPS_;
}

std::string IniParser::FnGetTaxiControl() const
{
    return TaxiControl_;
}

std::string IniParser::FnGetSTID2() const
{
    return STID2_;
}

std::string IniParser::FnGetLoadingBay() const
{
    return LoadingBay_;
}

std::string IniParser::FnGetLBLockTime() const
{
    return LBLockTime_;
}

std::string IniParser::FnGetEPSWithReader() const
{
    return EPSWithReader_;
}

std::string IniParser::FnGetLocalUDPPort() const
{
    return LocalUDPPort_;
}

std::string IniParser::FnGetRemoteUDPPort() const
{
    return RemoteUDPPort_;
}

std::string IniParser::FnGetEntryDebit() const
{
    return EntryDebit_;
}

std::string IniParser::FnGetShowTime() const
{
    return ShowTime_;
}

std::string IniParser::FnGetLEDShowTime() const
{
    return LEDShowTime_;
}

std::string IniParser::FnGetSeasonOnly() const
{
    return SeasonOnly_;
}

std::string IniParser::FnGetNotAllowHourly() const
{
    return NotAllowHourly_;
}

std::string IniParser::FnGetSaveSeason2Entry() const
{
    return SaveSeason2Entry_;
}

std::string IniParser::FnGetWithTicket() const
{
    return WithTicket_;
}

std::string IniParser::FnGetNotRecordMotorcycle() const
{
    return NotRecordMotorcycle_;
}

std::string IniParser::FnGetStartupOption() const
{
    return StartupOption_;
}

std::string IniParser::FnGetLastErrorTime() const
{
    return LastErrorTime_;
}

std::string IniParser::FnGetSecondID() const
{
    return SecondID_;
}

std::string IniParser::FnGetERP2Server() const
{
    return ERP2Server_;
}

std::string IniParser::FnGetERP2Port() const
{
    return ERP2Port_;
}

std::string IniParser::FnGetForceShiftPartialEPS() const
{
    return ForceShiftPartialEPS_;
}

std::string IniParser::FnGetIsChinaBReader() const
{
    return IsChinaBReader_;
}

std::string IniParser::FnGetCommPortAnt1() const
{
    return CommPortAnt1_;
}

std::string IniParser::FnGetCommPortAnt2() const
{
    return CommPortAnt2_;
}

std::string IniParser::FnGetCommPortAnt3() const
{
    return CommPortAnt3_;
}

std::string IniParser::FnGetCommPortAnt4() const
{
    return CommPortAnt4_;
}

std::string IniParser::FnGetCHUTermID() const
{
    return CHUTermID_;
}

std::string IniParser::FnGetDIOPort4Barrier() const
{
    return DIOPort4Barrier_;
}

std::string IniParser::FnGetDIOPort4StationDoorOpen() const
{
    return DIOPort4StationDoorOpen_;
}

std::string IniParser::FnGetDIOPort4BarrierDoorOpen() const
{
    return DIOPort4BarrierDoorOpen_;
}

std::string IniParser::FnGetLPRIP4Front() const
{
    return LPRIP4Front_;
}

std::string IniParser::FnGetLPRIP4Rear() const
{
    return LPRIP4Rear_;
}

std::string IniParser::FnGetWaitLPRNoTime() const
{
    return WaitLPRNoTime_;
}

std::string IniParser::FnGetDIOPort4TrafficLight() const
{
    return DIOPort4TrafficLight_;
}

std::string IniParser::FnGetDIOPort4EnableLoopA() const
{
    return DIOPort4EnableLoopA_;
}

std::string IniParser::FnGetStationIDDIOPort4EnableLoopD() const
{
    return DIOPort4EnableLoopD_;
}

std::string IniParser::FnGetDIOPort4Switch() const
{
    return DIOPort4Switch_;
}

std::string IniParser::FnGetNexpaDBServer() const
{
    return NexpaDBServer_;
}

std::string IniParser::FnGetNexpaDBName() const
{
    return NexpaDBName_;
}

int IniParser::FnGetWaitIUNoTime() const
{
    return WaitIUNoTime_;
}

std::string IniParser::FnGetHasPremiumParking() const
{
    return HasPremiumParking_;
}

std::string IniParser::FnGetIntervalloopCIU() const
{
    return IntervalloopCIU_;
}

std::string IniParser::FnGetTGDServer() const
{
    return TGDServer_;
}

std::string IniParser::FnGetIsTesting() const
{
    return IsTesting_;
}

std::string IniParser::FnGetTimeWaitForSecondID() const
{
    return TimeWaitForSecondID_;
}

std::string IniParser::FnGetWaitForPressHandicapButtonTime() const
{
    return WaitForPressHandicapButtonTime_;
}

std::string IniParser::FnGetDIOPort4LoopD() const
{
    return DIOPort4LoopD_;
}

std::string IniParser::FnGetCommPortControllerIO() const
{
    return CommPortControllerIO_;
}

std::string IniParser::FnGetLPRErrorTime() const
{
    return LPRErrorTime_;
}

std::string IniParser::FnGetLPRErrorCount() const
{
    return LPRErrorCount_;
}

std::string IniParser::FnGetLocation() const
{
    return Location_;
}

std::string IniParser::FnGetUVSSEnable() const
{
    return UVSSEnable_;
}

std::string IniParser::FnGetJPRMCServer() const
{
    return JPRMCServer_;
}

std::string IniParser::FnGetJPRMCPort() const
{
    return JPRMCPort_;
}

std::string IniParser::FnGetBlockIUPrefix() const
{
    return BlockIUPrefix_;
}

std::string IniParser::FnGetQ584PrinterComPort() const
{
    return Q584PrinterComPort_;
}

std::string IniParser::FnGetLPRIP4Container() const
{
    return LPRIP4Container_;
}

int IniParser::FnGetAntennaId() const
{
    return AntennaID_;
}