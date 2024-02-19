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
    LogFolder_                      = pt.get<std::string>("setting.LogFolder", "");
    LocalDB_                        = pt.get<std::string>("setting.LocalDB", "");
    CentralDBName_                  = pt.get<std::string>("setting.CentralDBName", "");
    CentralDBServer_                = pt.get<std::string>("setting.CentralDBServer_", "");
    LocalUDPPort_                   = pt.get<std::string>("setting.LocalUDPPort", "");
    RemoteUDPPort_                  = pt.get<std::string>("setting.RemoteUDPPort", "");
    SeasonOnly_                     = pt.get<std::string>("setting.SeasonOnly", "");
    NotAllowHourly_                 = pt.get<std::string>("setting.NotAllowHourly", "");
    LPRIP4Front_                    = pt.get<std::string>("setting.LPRIP4Front", "");
    LPRIP4Rear_                     = pt.get<std::string>("setting.LPRIP4Rear", "");
    WaitLPRNoTime_                  = pt.get<std::string>("setting.WaitLPRNoTime", "");
    LPRErrorTime_                   = pt.get<std::string>("setting.LPRErrorTime", "");
    LPRErrorCount_                  = pt.get<std::string>("setting.LPRErrorCount", "");
    BlockIUPrefix_                  = pt.get<std::string>("setting.BlockIUPrefix", "");
    
    // Confirm [setting]
    AntennaID_                      = pt.get<int>("setting.AntennaId");
    AntennaMaxRetry_                = pt.get<int>("setting.AntennaMaxRetry");
    AntennaInqTO_                   = pt.get<int>("setting.AntennaInqTO");
    AntennaMinOKtimes_              = pt.get<int>("setting.AntennaMinOKtimes");

    // Confirm [DI]
    LoopA_                          = pt.get<int>("DI.LoopA");
    LoopC_                          = pt.get<int>("DI.LoopC");
    LoopB_                          = pt.get<int>("DI.LoopB");
    Intercom_                       = pt.get<int>("DI.Intercom");
    StationDooropen_                = pt.get<int>("DI.StationDooropen");
    BarrierDooropen_                = pt.get<int>("DI.BarrierDooropen");
    BarrierStatus_                  = pt.get<int>("DI.BarrierStatus");

    // Confirm [DO]
    Openbarrier_                    = pt.get<int>("DO.Openbarrier");
    LCDbacklight_                   = pt.get<int>("DO.LCDbacklight");
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

std::string IniParser::FnGetLogFolder() const
{
    return LogFolder_;
}

std::string IniParser::FnGetLocalDB() const
{
    return LocalDB_;
}

std::string IniParser::FnGetCentralDBName() const
{
    return CentralDBName_;
}

std::string IniParser::FnGetCentralDBServer() const
{
    return CentralDBServer_;
}

std::string IniParser::FnGetLocalUDPPort() const
{
    return LocalUDPPort_;
}

std::string IniParser::FnGetRemoteUDPPort() const
{
    return RemoteUDPPort_;
}

std::string IniParser::FnGetSeasonOnly() const
{
    return SeasonOnly_;
}

std::string IniParser::FnGetNotAllowHourly() const
{
    return NotAllowHourly_;
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

std::string IniParser::FnGetLPRErrorTime() const
{
    return LPRErrorTime_;
}

std::string IniParser::FnGetLPRErrorCount() const
{
    return LPRErrorCount_;
}

std::string IniParser::FnGetBlockIUPrefix() const
{
    return BlockIUPrefix_;
}

// Confirm [setting]
int IniParser::FnGetAntennaId() const
{
    return AntennaID_;
}


int IniParser::FnGetAntennaMaxRetry() const
{
    return AntennaMaxRetry_;
}

int IniParser::FnGetAntennaInqTO() const
{
    return AntennaInqTO_;
}

int IniParser::FnGetAntennaMinOKtimes() const
{
    return AntennaMinOKtimes_;
}

// Confirm [DI]
int IniParser::FnGetLoopA() const
{
    return LoopA_;
}

int IniParser::FnGetLoopC() const
{
    return LoopC_;
}

int IniParser::FnGetLoopB() const
{
    return LoopB_;
}

int IniParser::FnGetIntercom() const
{
    return Intercom_;
}

int IniParser::FnGetStationDooropen() const
{
    return StationDooropen_;
}

int IniParser::FnGetBarrierDooropen() const
{
    return BarrierDooropen_;
}

int IniParser::FnGetBarrierStatus() const
{
    return BarrierStatus_;
}

// Confirm [DO]
int IniParser::FnGetOpenbarrier() const
{
    return Openbarrier_;
}

int IniParser::FnGetLCDbacklight() const
{
    return LCDbacklight_;
}