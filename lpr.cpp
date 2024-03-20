#include "ini_parser.h"
#include "lpr.h"
#include "log.h"

Lpr* Lpr::lpr_ = nullptr;

Lpr::Lpr()
    : cameraNo_(0),
    lprIp4Front_(""),
    lprIp4Rear_(""),
    reconnTime_(0),
    reconnTime2_(0),
    stdID_(""),
    logFileName_("LPR"),
    commErrorTimeCriteria_(0)
{

}

Lpr* Lpr::getInstance()
{
    if (lpr_ == nullptr)
    {
        lpr_ = new Lpr();
    }

    return lpr_;
}

void Lpr::LprInit(boost::asio::io_context& mainIOContext)
{
    cameraNo_ = 2;
    lprIp4Front_ = IniParser::getInstance()->FnGetLPRIP4Front();
    lprIp4Rear_ = IniParser::getInstance()->FnGetLPRIP4Rear();
    reconnTime_ = 5000;
    reconnTime2_ = 5000;
    commErrorTimeCriteria_ = std::stoi(IniParser::getInstance()->FnGetLPRErrorTime());
    transErrorCountCriteria_ = std::stoi(IniParser::getInstance()->FnGetLPRErrorCount());

    initFrontCamera(lprIp4Front_, 56107, "CH1");
    initRearCamera(lprIp4Rear_, 56107, "CH1");


    Logger::getInstance()->FnLog("LPR Initialization completed.");
}

void Lpr::initFrontCamera(const std::string& cameraIP, int tcpPort, const std::string cameraCH)
{

}

void Lpr::initRearCamera(const std::string& cameraIP, int tcpPort, const std::string cameraCH)
{
    
}