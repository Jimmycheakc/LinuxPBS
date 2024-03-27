#include "ini_parser.h"
#include "lpr.h"
#include "log.h"
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <string>
#include <sstream>

Lpr* Lpr::lpr_ = nullptr;

Lpr::Lpr()
    : cameraNo_(0),
    lprIp4Front_(""),
    lprIp4Rear_(""),
    reconnTime_(0),
    reconnTime2_(0),
    stdID_(""),
    logFileName_("lpr"),
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
    stdID_ = IniParser::getInstance()->FnGetStationID();
    lprIp4Front_ = IniParser::getInstance()->FnGetLPRIP4Front();
    lprIp4Rear_ = IniParser::getInstance()->FnGetLPRIP4Rear();
    reconnTime_ = 10000;
    reconnTime2_ = 10000;
    commErrorTimeCriteria_ = std::stoi(IniParser::getInstance()->FnGetLPRErrorTime());
    transErrorCountCriteria_ = std::stoi(IniParser::getInstance()->FnGetLPRErrorCount());

    std::cout << "cameraNo : " << cameraNo_ << std::endl;
    std::cout << "lprIp4Front_ : " << lprIp4Front_ << std::endl;
    std::cout << "lprIp4Rear_ : " << lprIp4Rear_ << std::endl;
    std::cout << "reconnTime_ : " << reconnTime_ << std::endl;
    std::cout << "reconnTime2_ : " << reconnTime2_ << std::endl;
    std::cout << "commErrorTimeCriteria_ : " << commErrorTimeCriteria_ << std::endl;
    std::cout << "transErrorCountCriteria_ : " << transErrorCountCriteria_ << std::endl; 

    Logger::getInstance()->FnCreateLogFile(logFileName_);

    bool initFrontRet = false;
    bool initRearRet = false;
    initFrontRet = initFrontCamera(mainIOContext, lprIp4Front_, 5272, "CH1");
    initRearRet = initRearCamera(mainIOContext, lprIp4Rear_, 5272, "CH1");

    std::stringstream logMsg;
    if ((cameraNo_ == 2 && initFrontRet == true && initRearRet == true)
        || (cameraNo_ == 1 && initFrontRet == true))
    {
        logMsg << "LPR initialization completed.";
    }
    else
    {
        logMsg << "LPR initialization failed.";
    }
    Logger::getInstance()->FnLog(logMsg.str());
    Logger::getInstance()->FnLog(logMsg.str(), logFileName_, "LPR");
}

bool Lpr::initFrontCamera(boost::asio::io_context& mainIOContext, const std::string& cameraIP, int tcpPort, const std::string cameraCH)
{
    if (cameraIP.empty())
    {
        Logger::getInstance()->FnLog("Front camera:: TCP init error, blank camera IP. Please check the setting.", logFileName_, "LPR");
        return false;
    }

    if (cameraIP == "1.1.1.1")
    {
        std::stringstream ss;
        ss << "Front Camera IP : " << cameraIP << ", Not In Use.";
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
        return false;
    }

    try
    {
        frontCamCH_ = cameraCH;
        pFrontCamera_ = std::make_unique<TcpClient>(mainIOContext, cameraIP, tcpPort);
        pFrontCamera_->setConnectHandler([this]() { handleFrontSocketConnect(); });
        pFrontCamera_->setCloseHandler([this]() { handleFrontSocketClose(); });
        pFrontCamera_->setReceiveHandler([this](const char* data, std::size_t length) { handleReceiveFrontCameraData(data, length); });
        pFrontCamera_->setErrorHandler([this](std::string error_message) { handleFrontSocketError(error_message); });

        startReconnectTimer();

        return true;
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << "Front camera: TCP init error. Error Exception: " << e.what();
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
        
        return false;
    }
}

void Lpr::handleReconnectTimerTimeout()
{
    std::cout << __func__ << std::endl;
    Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

    if (!pFrontCamera_->isStatusGood())
    {
        std::stringstream ss;
        ss << "Reconnect to server IP : " << lprIp4Front_;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
        std::cout << __func__ << ss.str() << std::endl;

        pFrontCamera_->connect();
    }

    startReconnectTimer();
}

void Lpr::startReconnectTimer()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

    int milliseconds = reconnTime_; 
    std::unique_ptr<boost::asio::io_context> timerIOContext_ = std::make_unique<boost::asio::io_context>();
    std::thread timerThread([this, milliseconds, timerIOContext_ = std::move(timerIOContext_)]() mutable {
        std::unique_ptr<boost::asio::deadline_timer> periodicReconnectTimer_ = std::make_unique<boost::asio::deadline_timer>(*timerIOContext_);
        periodicReconnectTimer_->expires_from_now(boost::posix_time::milliseconds(milliseconds));
        periodicReconnectTimer_->async_wait([this](const boost::system::error_code& error) {
                if (!error) {
                    handleReconnectTimerTimeout();
                }
        });

        timerIOContext_->run();
    });
    timerThread.detach();
}

void Lpr::handleFrontSocketConnect()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

    std::stringstream ss;
    ss << "Connected to server IP : " << lprIp4Front_;
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
    std::cout << ss.str() << std::endl;

    pFrontCamera_->send("OK");
    std::stringstream okss;
    okss << "Front camera: Send OK";
    Logger::getInstance()->FnLog(okss.str(), logFileName_, "LPR");
    std::cout << okss.str() << std::endl;
}

void Lpr::handleFrontSocketClose()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

    std::stringstream ss;
    ss << "Server @ Front Camera IP : " << lprIp4Front_ << " close connection.";
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
    std::cout << ss.str() << std::endl;
}

void Lpr::handleFrontSocketError(std::string error_msg)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

    std::stringstream ss;
    ss << "Front camera: TCP Socket Error: " << error_msg;
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
    std::cout << ss.str() << std::endl;
}

void Lpr::handleReceiveFrontCameraData(const char* data, std::size_t length)
{
    std::stringstream ss;
    ss << "Receive TCP Data @ Front Camera, IP : " << lprIp4Front_ << " Data : ";
    ss.write(data, length);
    ss << " , Length : " << length;
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
    std::cout << ss.str() << std::endl;

    std::string receiveDataStr(data, length);
    processData(receiveDataStr, CType::FRONT_CAMERA);
}

/********************************************************************/
/********************************************************************/

bool Lpr::initRearCamera(boost::asio::io_context& mainIOContext, const std::string& cameraIP, int tcpPort, const std::string cameraCH)
{
    if (cameraNo_ == 2)
    {
        if (cameraIP.empty())
        {
            Logger::getInstance()->FnLog("Rear camera:: TCP init error, blank camera IP. Please check the setting.", logFileName_, "LPR");
            return false;
        }

        if (cameraIP == "1.1.1.1")
        {
            std::stringstream ss;
            ss << "Rear Camera IP : " << cameraIP << ", Not In Use.";
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
            return false;
        }

        try
        {
            rearCamCH_ = cameraCH;
            pRearCamera_ = std::make_unique<TcpClient>(mainIOContext, cameraIP, tcpPort);
            pRearCamera_->setConnectHandler([this]() { handleRearSocketConnect(); });
            pRearCamera_->setCloseHandler([this]() { handleRearSocketClose(); });
            pRearCamera_->setReceiveHandler([this](const char* data, std::size_t length) { handleReceiveRearCameraData(data, length); });
            pRearCamera_->setErrorHandler([this](std::string error_message) { handleRearSocketError(error_message); });

            startReconnectTimer2();

            return true;
        }
        catch (const std::exception& e)
        {
            std::stringstream ss;
            ss << "Rear camera: TCP init error. Error Exception: " << e.what();
            Logger::getInstance()->FnLog(ss.str());
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
            
            return false;
        }
    }
    else
    {
        return true;
    }
}

void Lpr::handleReconnectTimer2Timeout()
{
    std::cout << __func__ << std::endl;
    Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

    if (!pRearCamera_->isStatusGood())
    {
        std::stringstream ss;
        ss << "Reconnect to server IP : " << lprIp4Rear_;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
        std::cout << __func__ << ss.str() << std::endl;

        pRearCamera_->connect();
    }

    startReconnectTimer2();
}

void Lpr::startReconnectTimer2()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

    int milliseconds = reconnTime2_; 
    std::unique_ptr<boost::asio::io_context> timerIOContext_ = std::make_unique<boost::asio::io_context>();
    std::thread timerThread([this, milliseconds, timerIOContext_ = std::move(timerIOContext_)]() mutable {
        std::unique_ptr<boost::asio::deadline_timer> periodicReconnectTimer2_ = std::make_unique<boost::asio::deadline_timer>(*timerIOContext_);
        periodicReconnectTimer2_->expires_from_now(boost::posix_time::milliseconds(milliseconds));
        periodicReconnectTimer2_->async_wait([this](const boost::system::error_code& error) {
                if (!error) {
                    handleReconnectTimer2Timeout();
                }
        });

        timerIOContext_->run();
    });
    timerThread.detach();
}

void Lpr::handleRearSocketConnect()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

    std::stringstream ss;
    ss << "Connected to server IP : " << lprIp4Rear_;
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
    std::cout << ss.str() << std::endl;

    pRearCamera_->send("OK");
    std::stringstream okss;
    okss << "Rear camera: Send OK";
    Logger::getInstance()->FnLog(okss.str(), logFileName_, "LPR");
    std::cout << okss.str() << std::endl;
}

void Lpr::handleRearSocketClose()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

    std::stringstream ss;
    ss << "Server @ Rear Camera IP : " << lprIp4Rear_ << " close connection.";
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
    std::cout << ss.str() << std::endl;
}

void Lpr::handleRearSocketError(std::string error_msg)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

    std::stringstream ss;
    ss << "Rear camera: TCP Socket Error: " << error_msg;
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
    std::cout << ss.str() << std::endl;
}

void Lpr::handleReceiveRearCameraData(const char* data, std::size_t length)
{
    std::stringstream ss;
    ss << "Receive TCP Data @ Rear Camera, IP : " << lprIp4Rear_ << " Data : ";
    ss.write(data, length);
    ss << " , Length : " << length;
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
    std::cout << ss.str() << std::endl;

    std::string receiveDataStr(data, length);
    processData(receiveDataStr, CType::REAR_CAMERA);
}

/********************************************************************/
/********************************************************************/

void Lpr::SendTransIDToLPR(const std::string& transID)
{
    std::stringstream sSend;
    std::vector<std::string> tmpStr;
    std::string sDateTime;

    boost::algorithm::split(tmpStr, transID, boost::algorithm::is_any_of("-"));

    if (tmpStr.size() == 3)
    {
        sDateTime = tmpStr[2];
    }

    sSend << "#LPRS_STX#" << transID << "#" << sDateTime << "#LPRS_ETX#";

    SendTransIDToLPR_Front(sSend.str());
    SendTransIDToLPR_Rear(sSend.str());
}

void Lpr::SendTransIDToLPR_Front(const std::string& transID)
{
    if (pFrontCamera_->isConnected() && pFrontCamera_->isStatusGood())
    {
        try
        {
            std::stringstream ss;
            ss << "Front camera, SendData : " << transID;
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");

            pFrontCamera_->send(transID);
        }
        catch (const std::exception& e)
        {
            std::stringstream ss;
            ss << "Front camera. " << __func__ << " Error : " << e.what();
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
        }
    }
}

void Lpr::SendTransIDToLPR_Rear(const std::string& transID)
{
    if (pRearCamera_->isConnected() && pFrontCamera_->isStatusGood())
    {
        try
        {
            std::stringstream ss;
            ss << "Rear camera, SendData : " << transID;
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");

            pRearCamera_->send(transID);
        }
        catch (const std::exception& e)
        {
            std::stringstream ss;
            ss << "Rear camera. " << __func__ << " Error : " << e.what();
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
        }
    }
}

void Lpr::processData(const std::string tcpData, CType camType)
{
    std::string rcvETX;
    int dataLen;
    std::string rcvSTX;
    std::string STX;
    std::string ETX;
    std::vector<std::string> tmpStr;

    std::string m_CType;
    std::string CameraChannel;

    STX = "CH1";
    ETX = ".jpg";

    if (camType == CType::FRONT_CAMERA)
    {
        m_CType = "Front Camera";
        CameraChannel = frontCamCH_;
    }
    else if (camType == CType::REAR_CAMERA)
    {
        m_CType = "Rear Camera";
        CameraChannel = rearCamCH_;
    }

    dataLen = tcpData.length();

    if (dataLen > 25)
    {
        boost::algorithm::split(tmpStr, tcpData, boost::algorithm::is_any_of("#"));

        rcvSTX = extractSTX(tcpData);

        rcvETX = extractETX(tcpData);

        if ((tmpStr.size() >= 5) && (tmpStr[1] == "LPRS_STX") && (tmpStr[5] == "LPRS_ETX"))
        {
            extractLPRData(tcpData, camType);
        }
        else
        {
            if (boost::algorithm::to_upper_copy(rcvSTX) != STX)
            {
                std::stringstream ss;
                ss << "Receive TCP Data @ : " << m_CType << " Wrong STX : " << rcvSTX;
                Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
            }

            if (boost::algorithm::to_lower_copy(rcvETX) != ETX)
            {
                std::stringstream ss;
                ss << "Receive TCP Data @ : " << m_CType << " Wrong ETX : " << rcvETX;
                Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
            }
        }
    }
    else
    {
        if (dataLen == 5)
        {
            if (boost::algorithm::to_upper_copy(tcpData) == "LPR_R")
            {
                std::stringstream ss;
                ss << "Receive TCP Data @ : " << m_CType << " , NP1400 program currently is running";
                Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
            }
            else if (boost::algorithm::to_upper_copy(tcpData) == "LPR_N")
            {
                std::stringstream ss;
                ss << "Receive TCP Data @ : " << m_CType << " , NP1400 program currently is not running";
                Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
            }
        }
        else if (dataLen == 2)
        {
            if (boost::algorithm::to_upper_copy(tcpData) == "OK")
            {
                Logger::getInstance()->FnLog("Receive TCP Data @ : OK", logFileName_, "LPR");
            }
        }
    }
}

std::string Lpr::extractSTX(const std::string& sMsg)
{
    std::string result;
    std::size_t textPos = sMsg.find("#");

    if (textPos != std::string::npos && textPos > 0)
    {
        std::size_t startPos = textPos - 3;

        if (startPos > 0)
        {
            result = sMsg.substr(startPos, 3);
        }
    }

    return result;
}

std::string Lpr::extractETX(const std::string& sMsg)
{
    std::string result;
    std::size_t textPos = sMsg.rfind(".");

    if (textPos != std::string::npos && textPos > 0)
    {
        if ((textPos + 3) <= sMsg.length())
        {
            result = sMsg.substr(textPos, 4);
        }
    }

    return result;
}

void Lpr::extractLPRData(const std::string& tcpData, CType camType)
{
    std::vector<std::string> tmpStr;
    std::string LPN = "";
    std::string TransID = "";
    std::string imagePath = "";
    std::string m_CType = "";

    if (camType == CType::FRONT_CAMERA)
    {
        m_CType = "Front Camera";
    }
    else if (camType == CType::REAR_CAMERA)
    {
        m_CType = "Rear Camera";
    }

    boost::algorithm::split(tmpStr, tcpData, boost::algorithm::is_any_of("#"));

    if (tmpStr.size() == 7)
    {
        TransID = tmpStr[2];
        LPN = tmpStr[3];
        imagePath = tmpStr[4];

        // Blank LPR check
        if (LPN == "0000000000")
        {
            //RaiseEvent LPReceive(camType, LPN, TransID, imagePath)

            std::stringstream ss;
            ss << "Raise LPRReceive. LPN Not recognized. @ " << m_CType;
            ss << " , LPN : " << LPN << " , TransID : " << TransID;
            ss << " , imagePath : " << imagePath;
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
        }
        else
        {
            //RaiseEvent LPReceive(camType, LPN, TransID, imagePath)

            std::stringstream ss;
            ss << "Raise LPRReceive. @ " << m_CType;
            ss << " , LPN : " << LPN << " , TransID : " << TransID;
            ss << " , imagePath : " << imagePath;
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
        }
    }
    else
    {
        if (tmpStr.size() == 12)
        {
            if ((tmpStr[7] == "LPRS_STX") && (tmpStr[11] == "LPRS_ETX"))
            {
                TransID = tmpStr[8];
                LPN = tmpStr[9];
                imagePath = tmpStr[10];

                // blank LPR check
                if (LPN == "0000000000")
                {
                    // RaiseEvent LPReceive(camType, LPN, TransID, imagePath)

                    std::stringstream ss;
                    ss << "Raise LPRReceive. LPN Not recognized. @ " << m_CType;
                    ss << " , LPN : " << LPN << " , TransID : " << TransID;
                    ss << " , imagePath : " << imagePath;
                    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
                }
                else
                {
                    // RaiseEvent LPReceive(camType, LPN, TransID, imagePath)

                    std::stringstream ss;
                    ss << "Raise LPRReceive. @ " << m_CType;
                    ss << " , LPN : " << LPN << " , TransID : " << TransID;
                    ss << " , imagePath : " << imagePath;
                    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
                }
            }
            else
            {
                if (tmpStr[7] != "LPRS_STX")
                {
                    std::stringstream ss;
                    ss << "Receive TCP cascaded Data @ : " << m_CType << " Wrong STX : " << tmpStr[7];
                    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
                }

                if (tmpStr[11] != "LPRS_ETX")
                {
                    std::stringstream ss;
                    ss << "Receive TCP cascaded Data @ : " << m_CType << " Wrong ETX : " << tmpStr[7];
                    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
                }
            }
        }
        else
        {
            std::stringstream ss;
            ss << "Invalid LPN format: Receive @ " << m_CType;
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
        }
    }
}