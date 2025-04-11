#include "ini_parser.h"
#include "lpr.h"
#include "log.h"
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <string>
#include <sstream>
#include "event_handler.h"
#include "event_manager.h"

Lpr* Lpr::lpr_ = nullptr;
std::mutex Lpr::mutex_;

Lpr::Lpr()
    : cameraNo_(0),
    lprIp4Front_(""),
    lprIp4Rear_(""),
    lprPort_(0),
    reconnTime_(0),
    reconnTime2_(0),
    stdID_(""),
    logFileName_("lpr"),
    commErrorTimeCriteria_(0),
    frontCameraInitialized_(false),
    rearCameraInitialized_(false)
{
}

Lpr* Lpr::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (lpr_ == nullptr)
    {
        lpr_ = new Lpr();
    }

    return lpr_;
}

void Lpr::FnLprInit(boost::asio::io_context& mainIOContext)
{
    try
    {
        cameraNo_ = 2;
        stdID_ = IniParser::getInstance()->FnGetStationID();
        lprIp4Front_ = IniParser::getInstance()->FnGetLPRIP4Front();
        lprIp4Rear_ = IniParser::getInstance()->FnGetLPRIP4Rear();
        reconnTime_ = 10000;
        reconnTime2_ = 10000;
        commErrorTimeCriteria_ = std::stoi(IniParser::getInstance()->FnGetLPRErrorTime());
        transErrorCountCriteria_ = std::stoi(IniParser::getInstance()->FnGetLPRErrorCount());
        lprPort_ = std::stoi(IniParser::getInstance()->FnGetLPRPort());

        Logger::getInstance()->FnCreateLogFile(logFileName_);

        initFrontCamera (mainIOContext, lprIp4Front_, lprPort_, "CH1");
        initRearCamera(mainIOContext, lprIp4Rear_, lprPort_, "CH1");
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

void Lpr::FnLprClose()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

    if (pFrontCamera_)
    {
        pFrontCamera_->close();
        pFrontCamera_.reset();
    }

    if (pRearCamera_)
    {
        pRearCamera_->close();
        pRearCamera_.reset();
    }
}

void Lpr::initFrontCamera(boost::asio::io_context& mainIOContext, const std::string& cameraIP, int tcpPort, const std::string cameraCH)
{
    if (cameraIP.empty())
    {
        Logger::getInstance()->FnLog("Front camera:: TCP init error, blank camera IP. Please check the setting.");
        Logger::getInstance()->FnLog("Front camera:: TCP init error, blank camera IP. Please check the setting.", logFileName_, "LPR");
        return;
    }

    if (cameraIP == "1.1.1.1")
    {
        std::stringstream ss;
        ss << "Front Camera IP : " << cameraIP << ", Not In Use.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
        return;
    }

    try
    {
        frontCamCH_ = cameraCH;
        pFrontCamera_ = std::make_unique<TcpClient>(mainIOContext, cameraIP, tcpPort);
        pFrontCamera_->setConnectHandler([this]() { handleFrontSocketConnect(); });
        pFrontCamera_->setCloseHandler([this]() { handleFrontSocketClose(); });
        pFrontCamera_->setReceiveHandler([this](const char* data, std::size_t length) { handleReceiveFrontCameraData(data, length); });
        pFrontCamera_->setErrorHandler([this](std::string error_message) { handleFrontSocketError(error_message); });
        pFrontCamera_->connect();

        startReconnectTimer();

        Logger::getInstance()->FnLog("Front camera:: TCP initialization completed.");
        Logger::getInstance()->FnLog("Front camera:: TCP initialization completed.", logFileName_, "LPR");
        frontCameraInitialized_ = true;
    }
    catch (const boost::system::system_error& e) // Catch Boost.Asio system errors
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

void Lpr::handleReconnectTimerTimeout()
{
    //Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

    if (!pFrontCamera_->isStatusGood())
    {
        std::stringstream ss;
        ss << "Reconnect to server IP : " << lprIp4Front_;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");

        pFrontCamera_->connect();
    }

    startReconnectTimer();
}

void Lpr::startReconnectTimer()
{
    //Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

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

    pFrontCamera_->send("OK");
    std::stringstream okss;
    okss << "Front camera: Send OK";
    Logger::getInstance()->FnLog(okss.str(), logFileName_, "LPR");
}

void Lpr::handleFrontSocketClose()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

    std::stringstream ss;
    ss << "Server @ Front Camera IP : " << lprIp4Front_ << " close connection.";
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
}

void Lpr::handleFrontSocketError(std::string error_msg)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

    std::stringstream ss;
    ss << "Front camera: TCP Socket Error: " << error_msg;
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
}

void Lpr::handleReceiveFrontCameraData(const char* data, std::size_t length)
{
    std::stringstream ss;
    ss << "Receive TCP Data @ Front Camera, IP : " << lprIp4Front_ << " Data : ";
    ss.write(data, length);
    ss << " , Length : " << length;
    Logger::getInstance()->FnLog(ss.str());
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");

    std::string receiveDataStr(data, length);
    processData(receiveDataStr, CType::FRONT_CAMERA);
}

/********************************************************************/
/********************************************************************/

void Lpr::initRearCamera(boost::asio::io_context& mainIOContext, const std::string& cameraIP, int tcpPort, const std::string cameraCH)
{
    if (cameraNo_ == 2)
    {
        if (cameraIP.empty())
        {
            Logger::getInstance()->FnLog("Rear camera:: TCP init error, blank camera IP. Please check the setting.");
            Logger::getInstance()->FnLog("Rear camera:: TCP init error, blank camera IP. Please check the setting.", logFileName_, "LPR");
            return;
        }

        if (cameraIP == "1.1.1.1")
        {
            std::stringstream ss;
            ss << "Rear Camera IP : " << cameraIP << ", Not In Use.";
            Logger::getInstance()->FnLog(ss.str());
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
            return;
        }

        try
        {
            rearCamCH_ = cameraCH;
            pRearCamera_ = std::make_unique<TcpClient>(mainIOContext, cameraIP, tcpPort);
            pRearCamera_->setConnectHandler([this]() { handleRearSocketConnect(); });
            pRearCamera_->setCloseHandler([this]() { handleRearSocketClose(); });
            pRearCamera_->setReceiveHandler([this](const char* data, std::size_t length) { handleReceiveRearCameraData(data, length); });
            pRearCamera_->setErrorHandler([this](std::string error_message) { handleRearSocketError(error_message); });
            pRearCamera_->connect();

            startReconnectTimer2();

            Logger::getInstance()->FnLog("Rear camera:: TCP initialization completed.");
            Logger::getInstance()->FnLog("Rear camera:: TCP initialization completed.", logFileName_, "LPR");

            rearCameraInitialized_ = true;
        }
        catch (const boost::system::system_error& e) // Catch Boost.Asio system errors
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
}

void Lpr::handleReconnectTimer2Timeout()
{
    //Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

    if (!pRearCamera_->isStatusGood())
    {
        std::stringstream ss;
        ss << "Reconnect to server IP : " << lprIp4Rear_;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");

        pRearCamera_->connect();
    }

    startReconnectTimer2();
}

void Lpr::startReconnectTimer2()
{
    //Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

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

    pRearCamera_->send("OK");
    std::stringstream okss;
    okss << "Rear camera: Send OK";
    Logger::getInstance()->FnLog(okss.str(), logFileName_, "LPR");
}

void Lpr::handleRearSocketClose()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

    std::stringstream ss;
    ss << "Server @ Rear Camera IP : " << lprIp4Rear_ << " close connection.";
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
}

void Lpr::handleRearSocketError(std::string error_msg)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

    std::stringstream ss;
    ss << "Rear camera: TCP Socket Error: " << error_msg;
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
}

void Lpr::handleReceiveRearCameraData(const char* data, std::size_t length)
{
    std::stringstream ss;
    ss << "Receive TCP Data @ Rear Camera, IP : " << lprIp4Rear_ << " Data : ";
    ss.write(data, length);
    ss << " , Length : " << length;
    Logger::getInstance()->FnLog(ss.str());
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");

    std::string receiveDataStr(data, length);
    processData(receiveDataStr, CType::REAR_CAMERA);
}

/********************************************************************/
/********************************************************************/

void Lpr::FnSendTransIDToLPR(const std::string& transID, bool useFrontCamera)
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

    if (useFrontCamera == true)
    {
        if (frontCameraInitialized_ == true)
        {
            SendTransIDToLPR_Front(sSend.str());
        }
    }
    else
    {
        if (rearCameraInitialized_ == true)
        {
            SendTransIDToLPR_Rear(sSend.str());
        }
    }
}

void Lpr::SendTransIDToLPR_Front(const std::string& transID)
{
    if (pFrontCamera_->isConnected() && pFrontCamera_->isStatusGood())
    {
        try
        {
            std::stringstream ss;
            ss << "Front camera, SendData : " << transID;
            Logger::getInstance()->FnLog(ss.str());
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");

            pFrontCamera_->send(transID);
        }
        catch (const boost::system::system_error& e) // Catch Boost.Asio system errors
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
    else
    {
        Logger::getInstance()->FnLog("Front Camera connection problem, send data failed.");
        Logger::getInstance()->FnLog("Front Camera connection problem, send data failed.", logFileName_, "LPR");
    }
}

void Lpr::SendTransIDToLPR_Rear(const std::string& transID)
{
    if (pRearCamera_->isConnected() && pRearCamera_->isStatusGood())
    {
        try
        {
            std::stringstream ss;
            ss << "Rear camera, SendData : " << transID;
            Logger::getInstance()->FnLog(ss.str());
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");

            pRearCamera_->send(transID);
        }
        catch (const boost::system::system_error& e) // Catch Boost.Asio system errors
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
    else
    {
        Logger::getInstance()->FnLog("Rear Camera connection problem, send data failed.");
        Logger::getInstance()->FnLog("Rear Camera connection problem, send data failed.", logFileName_, "LPR");
    }
}

void Lpr::processData(const std::string tcpData, CType camType)
{
    try
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
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", tcpData: " << tcpData << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", tcpData: " << tcpData << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
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
            struct LPREventData data;
            data.camType = camType;
            data.LPN = LPN;
            data.TransID = TransID;
            data.imagePath = imagePath;

            std::string serializedData = serializeEventData(data);
            EventManager::getInstance()->FnEnqueueEvent("Evt_handleLPRReceive", serializedData);

            std::stringstream ss;
            ss << "Raise LPRReceive. LPN Not recognized. @ " << m_CType;
            ss << " , LPN : " << LPN << " , TransID : " << TransID;
            ss << " , imagePath : " << imagePath;
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
        }
        else
        {
            struct LPREventData data;
            data.camType = camType;
            data.LPN = LPN;
            data.TransID = TransID;
            data.imagePath = imagePath;

            std::string serializedData = serializeEventData(data);
            EventManager::getInstance()->FnEnqueueEvent("Evt_handleLPRReceive", serializedData);

            Logger::getInstance()->FnLog("data.TransID : " + data.TransID);

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
                    struct LPREventData data;
                    data.camType = camType;
                    data.LPN = LPN;
                    data.TransID = TransID;
                    data.imagePath = imagePath;

                    std::string serializedData = serializeEventData(data);
                    EventManager::getInstance()->FnEnqueueEvent("Evt_handleLPRReceive", serializedData);

                    std::stringstream ss;
                    ss << "Raise LPRReceive. LPN Not recognized. @ " << m_CType;
                    ss << " , LPN : " << LPN << " , TransID : " << TransID;
                    ss << " , imagePath : " << imagePath;
                    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
                }
                else
                {
                    struct LPREventData data;
                    data.camType = camType;
                    data.LPN = LPN;
                    data.TransID = TransID;
                    data.imagePath = imagePath;

                    std::string serializedData = serializeEventData(data);
                    EventManager::getInstance()->FnEnqueueEvent("Evt_handleLPRReceive", serializedData);

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

std::string Lpr::serializeEventData(const LPREventData& eventData)
{
    std::stringstream ss;
    ss << static_cast<int>(eventData.camType) << "," << eventData.LPN << "," << eventData.TransID << "," << eventData.imagePath;
    return ss.str();
}

struct Lpr::LPREventData Lpr::deserializeEventData(const std::string& serializeData)
{
    struct LPREventData eventData{};

    try
    {
        std::stringstream ss(serializeData);
        std::string token;

        std::getline(ss, token, ',');
        int camTypeValue = std::stoi(token);
        eventData.camType = static_cast<Lpr::CType>(camTypeValue);

        std::getline(ss, token, ',');
        eventData.LPN = token;

        std::getline(ss, token, ',');
        eventData.TransID = token;

        std::getline(ss, token, ',');
        eventData.imagePath = token;

        return eventData;
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Data: " << serializeData << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return {};
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", Data: " << serializeData << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return {};
    }
}