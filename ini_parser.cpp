#include "ini_parser.h"

#include <filesystem>
#include <iostream>
#include <mutex>
#include <sstream>
#include <utility>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "log.h"

IniParser* IniParser::getInstance()
{
    static IniParser instance;
    return &instance;
}

void IniParser::FnReadIniFile()
{
    try
    {
        // This module is passive: no private thread/io_context is required.
        // Parse everything into a temporary snapshot first. Only publish the
        // new configuration after the entire INI file has been read successfully.
        std::filesystem::create_directories(INI_FILE_PATH);

        if (!std::filesystem::exists(INI_FILE))
        {
            std::stringstream ss;
            ss << "[INI] File not found | File=" << INI_FILE;
            Logger::getInstance()->FnLogExceptionError(ss.str());
            return;
        }

        boost::property_tree::ptree pt;
        boost::property_tree::ini_parser::read_ini(INI_FILE, pt);

        Settings newSettings;

        // [setting]
        newSettings.stationID        = pt.get<std::string>("setting.StationID", "");
        newSettings.logFolder        = pt.get<std::string>("setting.LogFolder", "");
        newSettings.localDB          = pt.get<std::string>("setting.LocalDB", "");
        newSettings.centralDBName    = pt.get<std::string>("setting.CentralDBName", "");
        newSettings.centralDBServer  = pt.get<std::string>("setting.CentralDBServer", "");
        newSettings.centralUsername  = pt.get<std::string>("setting.CentralUsername", "");
        newSettings.centralPassword  = pt.get<std::string>("setting.CentralPassword", "");
        newSettings.localUDPPort     = pt.get<std::string>("setting.LocalUDPPort", "");
        newSettings.remoteUDPPort    = pt.get<std::string>("setting.RemoteUDPPort", "");
        newSettings.seasonOnly       = pt.get<std::string>("setting.SeasonOnly", "");
        newSettings.notAllowHourly   = pt.get<std::string>("setting.NotAllowHourly", "");
        newSettings.lprIP4Front      = pt.get<std::string>("setting.LPRIP4Front", "1.1.1.1");
        newSettings.lprIP4Rear       = pt.get<std::string>("setting.LPRIP4Rear", "1.1.1.1");
        newSettings.lprPort          = pt.get<std::string>("setting.LPRPort", "");
        newSettings.waitLPRNoTime    = pt.get<std::string>("setting.WaitLPRNoTime", "");
        newSettings.lprErrorTime     = pt.get<std::string>("setting.LPRErrorTime", "");
        newSettings.lprErrorCount    = pt.get<std::string>("setting.LPRErrorCount", "");

        // Preserve the old behaviour: ShowTime is required to contain a valid
        // integer and only the value 1 means true.
        newSettings.showTime         = (pt.get<int>("setting.ShowTime") == 1);
        newSettings.blockIUPrefix    = pt.get<std::string>("setting.BlockIUPrefix", "");

        // [EEP] - required fields, matching the original behaviour.
        newSettings.eepClientIp      = pt.get<std::string>("EEP.EEPClientIp", "");
        newSettings.eepClientPort    = pt.get<int>("EEP.EEPClientPort");

        // [DI] - required fields, matching the original behaviour.
        newSettings.loopA            = pt.get<int>("DI.LoopA");
        newSettings.loopC            = pt.get<int>("DI.LoopC");
        newSettings.loopB            = pt.get<int>("DI.LoopB");
        newSettings.intercom         = pt.get<int>("DI.Intercom");
        newSettings.stationDoorOpen  = pt.get<int>("DI.StationDooropen");
        newSettings.barrierDoorOpen  = pt.get<int>("DI.BarrierDooropen");
        newSettings.barrierStatus    = pt.get<int>("DI.BarrierStatus");
        newSettings.manualOpenBarrier = pt.get<int>("DI.ManualOpenBarrier");
        newSettings.lorrySensor      = pt.get<int>("DI.Lorrysensor");
        newSettings.armBroken        = pt.get<int>("DI.Armbroken");
        newSettings.printReceipt     = pt.get<int>("DI.PrintReceipt");

        // [DO] - required fields, matching the original behaviour.
        newSettings.openBarrier      = pt.get<int>("DO.Openbarrier");
        newSettings.lcdBacklight     = pt.get<int>("DO.LCDbacklight");
        newSettings.closeBarrier     = pt.get<int>("DO.closebarrier");

        // Publish one complete configuration snapshot. Getters can never see
        // a half-loaded configuration if parsing fails midway.
        {
            std::unique_lock lock(settingsMutex_);
            settings_ = std::move(newSettings);
        }

        std::stringstream ss;
        ss << "[INI] Loaded | File=" << INI_FILE;
        Logger::getInstance()->FnLog(ss.str());
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        std::stringstream ss;
        ss << "[INI] Filesystem error | File=" << INI_FILE << " | Exception=" << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (const boost::property_tree::ini_parser_error& e)
    {
        std::stringstream ss;
        ss << "[INI] Parse error | File=" << INI_FILE << " | Exception=" << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << "[INI] Read failed | File=" << INI_FILE << " | Exception=" << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        std::stringstream ss;
        ss << "[INI] Read failed | File=" << INI_FILE << " | Exception=Unknown exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
}

void IniParser::FnPrintIniFile() const
{
    try
    {
        boost::property_tree::ptree pt;
        boost::property_tree::ini_parser::read_ini(INI_FILE, pt);

        for (const auto& section : pt)
        {
            const auto& sectionName = section.first;
            const auto& sectionProperties = section.second;

            std::cout << "Section: " << sectionName << '\n';

            for (const auto& key : sectionProperties)
            {
                const auto& keyName = key.first;
                const auto keyValue = key.second.get_value<std::string>();

                // Do not print credentials to the console/log output.
                const bool sensitive = (keyName == "CentralPassword");

                std::cout << " Key: " << keyName
                          << ", Value: " << (sensitive ? "******" : keyValue)
                          << '\n';
            }
        }
    }
    catch (const boost::property_tree::ini_parser_error& e)
    {
        std::stringstream ss;
        ss << "[INI] Print parse error | File=" << INI_FILE << " | Exception=" << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << "[INI] Print failed | File=" << INI_FILE << " | Exception=" << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        std::stringstream ss;
        ss << "[INI] Print failed | File=" << INI_FILE << " | Exception=Unknown exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
}

std::string IniParser::FnGetStationID() const
{
    return getSetting(&Settings::stationID);
}


std::string IniParser::FnGetLogFolder() const
{
    return getSetting(&Settings::logFolder);
}


std::string IniParser::FnGetLocalDB() const
{
    return getSetting(&Settings::localDB);
}


std::string IniParser::FnGetCentralDBName() const
{
    return getSetting(&Settings::centralDBName);
}


std::string IniParser::FnGetCentralDBServer() const
{
    return getSetting(&Settings::centralDBServer);
}


std::string IniParser::FnGetCentralUsername() const
{
    return getSetting(&Settings::centralUsername);
}


std::string IniParser::FnGetCentralPassword() const
{
    return getSetting(&Settings::centralPassword);
}


std::string IniParser::FnGetLocalUDPPort() const
{
    return getSetting(&Settings::localUDPPort);
}


std::string IniParser::FnGetRemoteUDPPort() const
{
    return getSetting(&Settings::remoteUDPPort);
}


std::string IniParser::FnGetSeasonOnly() const
{
    return getSetting(&Settings::seasonOnly);
}


std::string IniParser::FnGetNotAllowHourly() const
{
    return getSetting(&Settings::notAllowHourly);
}


std::string IniParser::FnGetLPRIP4Front() const
{
    return getSetting(&Settings::lprIP4Front);
}


std::string IniParser::FnGetLPRIP4Rear() const
{
    return getSetting(&Settings::lprIP4Rear);
}


std::string IniParser::FnGetLPRPort() const
{
    return getSetting(&Settings::lprPort);
}


std::string IniParser::FnGetWaitLPRNoTime() const
{
    return getSetting(&Settings::waitLPRNoTime);
}


std::string IniParser::FnGetLPRErrorTime() const
{
    return getSetting(&Settings::lprErrorTime);
}


std::string IniParser::FnGetLPRErrorCount() const
{
    return getSetting(&Settings::lprErrorCount);
}


bool IniParser::FnGetShowTime() const
{
    return getSetting(&Settings::showTime);
}


std::string IniParser::FnGetBlockIUPrefix() const
{
    return getSetting(&Settings::blockIUPrefix);
}


std::string IniParser::FnGetEEPClientIp() const
{
    return getSetting(&Settings::eepClientIp);
}


int IniParser::FnGetEEPClientPort() const
{
    return getSetting(&Settings::eepClientPort);
}


int IniParser::FnGetLoopA() const
{
    return getSetting(&Settings::loopA);
}


int IniParser::FnGetLoopC() const
{
    return getSetting(&Settings::loopC);
}


int IniParser::FnGetLoopB() const
{
    return getSetting(&Settings::loopB);
}


int IniParser::FnGetIntercom() const
{
    return getSetting(&Settings::intercom);
}


int IniParser::FnGetStationDooropen() const
{
    return getSetting(&Settings::stationDoorOpen);
}


int IniParser::FnGetBarrierDooropen() const
{
    return getSetting(&Settings::barrierDoorOpen);
}


int IniParser::FnGetBarrierStatus() const
{
    return getSetting(&Settings::barrierStatus);
}


int IniParser::FnGetManualOpenBarrier() const
{
    return getSetting(&Settings::manualOpenBarrier);
}


int IniParser::FnGetLorrysensor() const
{
    return getSetting(&Settings::lorrySensor);
}


int IniParser::FnGetArmbroken() const
{
    return getSetting(&Settings::armBroken);
}


int IniParser::FnGetPrintReceipt() const
{
    return getSetting(&Settings::printReceipt);
}


int IniParser::FnGetOpenbarrier() const
{
    return getSetting(&Settings::openBarrier);
}


int IniParser::FnGetLCDbacklight() const
{
    return getSetting(&Settings::lcdBacklight);
}


int IniParser::FnGetclosebarrier() const
{
    return getSetting(&Settings::closeBarrier);
}
