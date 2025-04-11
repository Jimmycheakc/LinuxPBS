#include <iostream>
#include <string>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include "ini_parser.h"
#include "log.h"

IniParser* IniParser::iniParser_;
std::mutex IniParser::mutex_;

IniParser::IniParser()
{

}

IniParser* IniParser::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (iniParser_ == nullptr)
    {
        iniParser_ = new IniParser();
    }
    return iniParser_;
}

void IniParser::FnReadIniFile()
{

    // Create local INI folder
    try
    {
        if (!(boost::filesystem::exists(INI_FILE_PATH)))
        {
            if (!(boost::filesystem::create_directories(INI_FILE_PATH)))
            {
                std::stringstream ss;
                ss << __func__ << ", Failed to create directory: " << INI_FILE_PATH;
                Logger::getInstance()->FnLogExceptionError(ss.str());
            }
        }

        if (!boost::filesystem::exists(INI_FILE))
        {
            Logger::getInstance()->FnLogExceptionError("INI file not found: " + INI_FILE);
            return; // or throw if you prefer
        }

        boost::property_tree::ptree pt;
        boost::property_tree::ini_parser::read_ini(INI_FILE, pt);

        // Temp: Revisit and implement storing to private variable function
        StationID_                      = pt.get<std::string>("setting.StationID", "");
        LogFolder_                      = pt.get<std::string>("setting.LogFolder", "");
        LocalDB_                        = pt.get<std::string>("setting.LocalDB", "");
        CentralDBName_                  = pt.get<std::string>("setting.CentralDBName", "");
        CentralDBServer_                = pt.get<std::string>("setting.CentralDBServer", "");
        CentralUsername_                = pt.get<std::string>("setting.CentralUsername", "");
        CentralPassword_                = pt.get<std::string>("setting.CentralPassword", "");
        LocalUDPPort_                   = pt.get<std::string>("setting.LocalUDPPort", "");
        RemoteUDPPort_                  = pt.get<std::string>("setting.RemoteUDPPort", "");
        SeasonOnly_                     = pt.get<std::string>("setting.SeasonOnly", "");
        NotAllowHourly_                 = pt.get<std::string>("setting.NotAllowHourly", "");
        LPRIP4Front_                    = pt.get<std::string>("setting.LPRIP4Front", "");
        LPRIP4Rear_                     = pt.get<std::string>("setting.LPRIP4Rear", "");
        LPRPort_                        = pt.get<std::string>("setting.LPRPort", "");
        WaitLPRNoTime_                  = pt.get<std::string>("setting.WaitLPRNoTime", "");
        LPRErrorTime_                   = pt.get<std::string>("setting.LPRErrorTime", "");
        LPRErrorCount_                  = pt.get<std::string>("setting.LPRErrorCount", "");
        ShowTime_                       = (std::stoi(pt.get<std::string>("setting.ShowTime", "")) == 1) ? true : false;
        BlockIUPrefix_                  = pt.get<std::string>("setting.BlockIUPrefix", "");

        // Confirm [DI]
        LoopA_                          = pt.get<int>("DI.LoopA");
        LoopC_                          = pt.get<int>("DI.LoopC");
        LoopB_                          = pt.get<int>("DI.LoopB");
        Intercom_                       = pt.get<int>("DI.Intercom");
        StationDooropen_                = pt.get<int>("DI.StationDooropen");
        BarrierDooropen_                = pt.get<int>("DI.BarrierDooropen");
        BarrierStatus_                  = pt.get<int>("DI.BarrierStatus");
        ManualOpenBarrier_              = pt.get<int>("DI.ManualOpenBarrier");
        Lorrysensor_                    = pt.get<int>("DI.Lorrysensor");
        Armbroken_                      = pt.get<int>("DI.Armbroken");
        PrintReceipt_                   = pt.get<int>("DI.PrintReceipt");

        // Confirm [DO]
        Openbarrier_                    = pt.get<int>("DO.Openbarrier");
        LCDbacklight_                   = pt.get<int>("DO.LCDbacklight");
        closebarrier_                   = pt.get<int>("DO.closebarrier");
    }
    catch (const boost::filesystem::filesystem_error& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Boost Asio Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (const std::exception &e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
}

void IniParser::FnPrintIniFile()
{
    try
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
    catch (const boost::property_tree::ini_parser_error& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Boost Asio Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
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

std::string IniParser::FnGetCentralUsername() const
{
    return CentralUsername_;
}

std::string IniParser::FnGetCentralPassword() const
{
    return CentralPassword_;
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

std::string IniParser::FnGetLPRPort() const
{
    return LPRPort_;
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

bool IniParser::FnGetShowTime() const
{
    return ShowTime_;
}

std::string IniParser::FnGetBlockIUPrefix() const
{
    return BlockIUPrefix_;
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

int IniParser::FnGetManualOpenBarrier() const
{
    return ManualOpenBarrier_;
}

int IniParser::FnGetLorrysensor() const
{
    return Lorrysensor_;
}

int IniParser::FnGetArmbroken() const
{
    return Armbroken_;
}

int IniParser::FnGetPrintReceipt() const
{
    return PrintReceipt_;
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

int IniParser::FnGetclosebarrier() const
{
    return closebarrier_;
}