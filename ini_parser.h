#pragma once

#include <memory>
#include <string>

class IniParser
{

public:
    const std::string INI_FILE = "LinuxPBS.ini";

    static IniParser* getInstance();
    void FnReadIniFile();
    void FnPrintIniFile();
    std::string FnGetStationID() const;
    std::string FnGetHasInternalLink() const;
    std::string FnGetInternalDefaultLEDMsg() const;
    std::string FnGetInternalNoNightParkingMsg1() const;
    std::string FnGetInternalNoNightParkingMsg2() const;
    std::string FnGetHasInternalCarpark() const;
    std::string FnGetAttachedDBServer() const;
    std::string FnGetAttachedDBName() const;
    std::string FnGetAttachedExitUDPPort() const;
    std::string FnGetAttachedExitID() const;
    std::string FnGetIsOldPass() const;
    std::string FnGetHDBLotAdjustTimer() const;
    std::string FnGetIsLEDBlinking() const;
    std::string FnGetSQLServerPort() const;
    std::string FnGetLastIUTimeout() const;
    std::string FnGetLogFolder() const;
    std::string FnGetLocalDB() const;
    std::string FnGetLastSerialNo() const;
    std::string FnGetLastRedemptNo() const;
    std::string FnGetTicketStatus() const;
    std::string FnGetDisplayType() const;
    std::string FnGetCommPortDisplay() const;
    std::string FnGetCommPortIU() const;
    std::string FnGetEPS() const;
    std::string FnGetTaxiControl() const;
    std::string FnGetSTID2() const;
    std::string FnGetLoadingBay() const;
    std::string FnGetLBLockTime() const;
    std::string FnGetEPSWithReader() const;
    std::string FnGetLocalUDPPort() const;
    std::string FnGetRemoteUDPPort() const;
    std::string FnGetEntryDebit() const;
    std::string FnGetShowTime() const;
    std::string FnGetLEDShowTime() const;
    std::string FnGetSeasonOnly() const;
    std::string FnGetNotAllowHourly() const;
    std::string FnGetSaveSeason2Entry() const;
    std::string FnGetWithTicket() const;
    std::string FnGetNotRecordMotorcycle() const;
    std::string FnGetStartupOption() const;
    std::string FnGetLastErrorTime() const;
    std::string FnGetSecondID() const;
    std::string FnGetERP2Server() const;
    std::string FnGetERP2Port() const;
    std::string FnGetForceShiftPartialEPS() const;
    std::string FnGetIsChinaBReader() const;
    std::string FnGetCommPortAnt1() const;
    std::string FnGetCommPortAnt2() const;
    std::string FnGetCommPortAnt3() const;
    std::string FnGetCommPortAnt4() const;
    std::string FnGetCHUTermID() const;
    std::string FnGetDIOPort4Barrier() const;
    std::string FnGetDIOPort4StationDoorOpen() const;
    std::string FnGetDIOPort4BarrierDoorOpen() const;
    std::string FnGetLPRIP4Front() const;
    std::string FnGetLPRIP4Rear() const;
    std::string FnGetWaitLPRNoTime() const;
    std::string FnGetDIOPort4TrafficLight() const;
    std::string FnGetDIOPort4EnableLoopA() const;
    std::string FnGetStationIDDIOPort4EnableLoopD() const;
    std::string FnGetDIOPort4Switch() const;
    std::string FnGetNexpaDBServer() const;
    std::string FnGetNexpaDBName() const;
    std::string FnGetHasPremiumParking() const;
    std::string FnGetIntervalloopCIU() const;
    std::string FnGetTGDServer() const;
    std::string FnGetIsTesting() const;
    std::string FnGetTimeWaitForSecondID() const;
    std::string FnGetWaitForPressHandicapButtonTime() const;
    std::string FnGetDIOPort4LoopD() const;
    std::string FnGetCommPortControllerIO() const;
    std::string FnGetLPRErrorTime() const;
    std::string FnGetLPRErrorCount() const;
    std::string FnGetLocation() const;
    std::string FnGetUVSSEnable() const;
    std::string FnGetJPRMCServer() const;
    std::string FnGetJPRMCPort() const;
    std::string FnGetBlockIUPrefix() const;
    std::string FnGetQ584PrinterComPort() const;
    std::string FnGetLPRIP4Container() const;

    // Confirm
    int FnGetAntennaId() const;
    int FnGetWaitIUNoTime() const;
    int FnGetAntennaMaxRetry() const;
    int FnGetAntennaInqTO() const;
    int FnGetAntennaMinOKtimes() const;

    /**
     * Singleton IniParser should not be cloneable.
     */
    IniParser(IniParser &iniparser) = delete;

    /**
     * Singleton IniParser should not be assignable.
     */
    void operator=(const IniParser &) = delete;

private:
    static IniParser* iniParser_;
    IniParser();

    std::string StationID_;
    std::string HasInternalLink_;
    std::string InternalDefaultLEDMsg_;
    std::string InternalNoNightParkingMsg1_;
    std::string InternalNoNightParkingMsg2_;
    std::string HasInternalCarpark_;
    std::string AttachedDBServer_;
    std::string AttachedDBName_;
    std::string AttachedExitUDPPort_;
    std::string AttachedExitID_;
    std::string IsOldPass_;
    std::string HDBLotAdjustTimer_;
    std::string IsLEDBlinking_;
    std::string SQLServerPort_;
    std::string LastIUTimeout_;
    std::string LogFolder_;
    std::string LocalDB_;
    std::string LastSerialNo_;
    std::string LastRedemptNo_;
    std::string TicketStatus_;
    std::string DisplayType_;
    std::string CommPortDisplay_;
    std::string CommPortIU_;
    std::string EPS_;
    std::string TaxiControl_;
    std::string STID2_;
    std::string LoadingBay_;
    std::string LBLockTime_;
    std::string EPSWithReader_;
    std::string LocalUDPPort_;
    std::string RemoteUDPPort_;
    std::string EntryDebit_;
    std::string ShowTime_;
    std::string LEDShowTime_;
    std::string SeasonOnly_;
    std::string NotAllowHourly_;
    std::string SaveSeason2Entry_;
    std::string WithTicket_;
    std::string NotRecordMotorcycle_;
    std::string StartupOption_;
    std::string LastErrorTime_;
    std::string SecondID_;
    std::string ERP2Server_;
    std::string ERP2Port_;
    std::string ForceShiftPartialEPS_;
    std::string IsChinaBReader_;
    std::string CommPortAnt1_;
    std::string CommPortAnt2_;
    std::string CommPortAnt3_;
    std::string CommPortAnt4_;
    std::string CHUTermID_;
    std::string DIOPort4Barrier_;
    std::string DIOPort4StationDoorOpen_;
    std::string DIOPort4BarrierDoorOpen_;
    std::string LPRIP4Front_;
    std::string LPRIP4Rear_;
    std::string WaitLPRNoTime_;
    std::string DIOPort4TrafficLight_;
    std::string DIOPort4EnableLoopA_;
    std::string DIOPort4EnableLoopD_;
    std::string DIOPort4Switch_;
    std::string NexpaDBServer_;
    std::string NexpaDBName_;
    std::string HasPremiumParking_;
    std::string IntervalloopCIU_;
    std::string TGDServer_;
    std::string IsTesting_;
    std::string TimeWaitForSecondID_;
    std::string WaitForPressHandicapButtonTime_;
    std::string DIOPort4LoopD_;
    std::string CommPortControllerIO_;
    std::string LPRErrorTime_;
    std::string LPRErrorCount_;
    std::string Location_;
    std::string UVSSEnable_;
    std::string JPRMCServer_;
    std::string JPRMCPort_;
    std::string BlockIUPrefix_;
    std::string Q584PrinterComPort_;
    std::string LPRIP4Container_;

    // Confirm
    int AntennaID_;
    int WaitIUNoTime_;
    int AntennaMaxRetry_;
    int AntennaInqTO_;
    int AntennaMinOKtimes_;
};