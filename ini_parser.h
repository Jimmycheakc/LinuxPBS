#pragma once

#include <memory>
#include <mutex>
#include <string>

class IniParser
{

public:
    const std::string INI_FILE_PATH = "/home/root/carpark/Ini";
    const std::string INI_FILE = "/home/root/carpark/Ini/LinuxPBS.ini";

    static IniParser* getInstance();
    void FnReadIniFile();
    void FnPrintIniFile();
    std::string FnGetStationID() const;
    std::string FnGetLogFolder() const;
    std::string FnGetLocalDB() const;
    std::string FnGetCentralDBName() const;
    std::string FnGetCentralDBServer() const;
    std::string FnGetCentralUsername() const;
    std::string FnGetCentralPassword() const;
    std::string FnGetLocalUDPPort() const;
    std::string FnGetRemoteUDPPort() const;
    std::string FnGetSeasonOnly() const;
    std::string FnGetNotAllowHourly() const;
    std::string FnGetLPRIP4Front() const;
    std::string FnGetLPRIP4Rear() const;
    std::string FnGetLPRPort() const;
    std::string FnGetWaitLPRNoTime() const;
    std::string FnGetLPRErrorTime() const;
    std::string FnGetLPRErrorCount() const;
    bool FnGetShowTime() const;
    std::string FnGetBlockIUPrefix() const;

    // Confirm [DI]
    int FnGetLoopA() const;
    int FnGetLoopC() const;
    int FnGetLoopB() const;
    int FnGetIntercom() const;
    int FnGetStationDooropen() const;
    int FnGetBarrierDooropen() const;
    int FnGetBarrierStatus() const;
    int FnGetManualOpenBarrier() const;
    int FnGetLorrysensor() const;
    int FnGetArmbroken() const;
    int FnGetPrintReceipt() const;

    // Confirm [DO]
    int FnGetOpenbarrier() const;
    int FnGetLCDbacklight() const;
    int FnGetclosebarrier() const;

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
    static std::mutex mutex_;
    IniParser();

    std::string StationID_;
    std::string LogFolder_;
    std::string LocalDB_;
    std::string CentralDBName_;
    std::string CentralDBServer_;
    std::string CentralUsername_;
    std::string CentralPassword_;
    std::string LocalUDPPort_;
    std::string RemoteUDPPort_;
    std::string SeasonOnly_;
    std::string NotAllowHourly_;
    std::string LPRIP4Front_;
    std::string LPRIP4Rear_;
    std::string LPRPort_;
    std::string WaitLPRNoTime_;
    std::string LPRErrorTime_;
    std::string LPRErrorCount_;
    bool ShowTime_;
    std::string BlockIUPrefix_;

    // Confirm [DI]
    int LoopA_;
    int LoopC_;
    int LoopB_;
    int Intercom_;
    int StationDooropen_;
    int BarrierDooropen_;
    int BarrierStatus_;
    int ManualOpenBarrier_;
    int Lorrysensor_;
    int Armbroken_;
    int PrintReceipt_;

    // Confirm [DO]
    int Openbarrier_;
    int LCDbacklight_;
    int closebarrier_;
};