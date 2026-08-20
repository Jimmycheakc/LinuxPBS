#pragma once

#include <shared_mutex>
#include <string>

class IniParser
{

public:
    // Keep these names public for compatibility with existing code.
    inline static const std::string INI_FILE_PATH = "/home/root/carpark/Ini";
    inline static const std::string INI_FILE = "/home/root/carpark/Ini/LinuxPBS.ini";

    static IniParser* getInstance();

    void FnReadIniFile();
    void FnPrintIniFile() const;

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

    // [EEP]
    std::string FnGetEEPClientIp() const;
    int FnGetEEPClientPort() const;

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

private:
    struct Settings
    {
        // [setting]
        std::string stationID;
        std::string logFolder;
        std::string localDB;
        std::string centralDBName;
        std::string centralDBServer;
        std::string centralUsername;
        std::string centralPassword;
        std::string localUDPPort;
        std::string remoteUDPPort;
        std::string seasonOnly;
        std::string notAllowHourly;
        std::string lprIP4Front{"1.1.1.1"};
        std::string lprIP4Rear{"1.1.1.1"};
        std::string lprPort;
        std::string waitLPRNoTime;
        std::string lprErrorTime;
        std::string lprErrorCount;
        bool showTime{false};
        std::string blockIUPrefix;

        // [EEP]
        std::string eepClientIp;
        int eepClientPort{0};

        // [DI]
        int loopA{0};
        int loopC{0};
        int loopB{0};
        int intercom{0};
        int stationDoorOpen{0};
        int barrierDoorOpen{0};
        int barrierStatus{0};
        int manualOpenBarrier{0};
        int lorrySensor{0};
        int armBroken{0};
        int printReceipt{0};

        // [DO]
        int openBarrier{0};
        int lcdBacklight{0};
        int closeBarrier{0};
    };

    IniParser() = default;
    ~IniParser() = default;

    IniParser(const IniParser&) = delete;
    IniParser& operator=(const IniParser&) = delete;
    IniParser(IniParser&&) = delete;
    IniParser& operator=(IniParser&&) = delete;

    template <typename T>
    T getSetting(T Settings::* member) const
    {
        std::shared_lock lock(settingsMutex_);
        return settings_.*member;
    }

    mutable std::shared_mutex settingsMutex_;
    Settings settings_;
};