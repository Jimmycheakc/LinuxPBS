#include "ini_parser.h"
#include "lpr.h"
#include "log.h"
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <string>
#include <sstream>
#include "event_handler.h"
#include "event_manager.h"
#include "structuredata.h"
#include "operation.h"

Lpr* Lpr::lpr_ = nullptr;
std::mutex Lpr::mutex_;

Lpr::Lpr()
    : ioContext_(),
    strand_(boost::asio::make_strand(ioContext_)),
    workGuard_(boost::asio::make_work_guard(ioContext_)),
    periodicReconnectTimer_(ioContext_),
    periodicReconnectTimer2_(ioContext_),
    cameraNo_(0),
    lprIp4Front_(""),
    lprIp4Rear_(""),
    lprPort_(0),
    reconnTime_(0),
    reconnTime2_(0),
    stdID_(""),
    logFileName_("lpr"),
    commErrorTimeCriteria_(0),
    frontCameraInitialized_(false),
    rearCameraInitialized_(false),
    lastFrontCameraConnected_(false),
    lastRearCameraConnected_(false)
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

void Lpr::FnLprInit()
{
    try
    {
        cameraNo_ = 2;
        stdID_ = IniParser::getInstance()->FnGetStationID();
        lprIp4Front_ = IniParser::getInstance()->FnGetLPRIP4Front();
        lprIp4Rear_ = IniParser::getInstance()->FnGetLPRIP4Rear();
        /* Temp: Disabled for miniPC
        reconnTime_ = 10000;
        reconnTime2_ = 10000;
        */
        reconnTime_ = 2000;
        reconnTime2_ = 2000;
        commErrorTimeCriteria_ = std::stoi(IniParser::getInstance()->FnGetLPRErrorTime());
        transErrorCountCriteria_ = std::stoi(IniParser::getInstance()->FnGetLPRErrorCount());
        lprPort_ = std::stoi(IniParser::getInstance()->FnGetLPRPort());

        Logger::getInstance()->FnCreateLogFile(logFileName_);

        startIoContextThread();
        initFrontCamera(lprIp4Front_, lprPort_, "CH1");
        initRearCamera(lprIp4Rear_, lprPort_, "CH1");
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

void Lpr::startIoContextThread()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

    if (!ioContextThread_.joinable())
    {
        ioContextThread_ = std::thread([this] { ioContext_.run(); });
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

    workGuard_.reset();
    ioContext_.stop();
    if (ioContextThread_.joinable())
    {
        ioContextThread_.join();
    }
}

void Lpr::initFrontCamera(const std::string& cameraIP, int tcpPort, const std::string cameraCH)
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
        pFrontCamera_ = std::make_unique<AppTcpClient>(ioContext_, cameraIP, tcpPort);
        pFrontCamera_->setConnectHandler([this](bool success, const std::string& message) { handleFrontSocketConnect(success, message); });
        pFrontCamera_->setCloseHandler([this](bool success, const std::string& message) { handleFrontSocketClose(success, message); });
        pFrontCamera_->setReceiveHandler([this](bool success, const std::vector<uint8_t>& data) { handleReceiveFrontCameraData(success, data); });
        pFrontCamera_->setSendHandler([this](bool success, const std::string& message) { handleFrontSocketSend(success, message); });
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

void Lpr::handleReconnectTimerTimeout(const boost::system::error_code& error)
{
    if (error)
    {
        std::ostringstream oss;
        oss << "Reconnect Timer error: " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "LPR");
    }

    if (!pFrontCamera_->isConnected())
    {
        std::stringstream ss;
        ss << "Reconnect to Server @ Front Camera IP: " << lprIp4Front_ << ", Port: " << lprPort_;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");

        pFrontCamera_->connect();
    }

    startReconnectTimer();
}

void Lpr::startReconnectTimer()
{
    periodicReconnectTimer_.expires_after(std::chrono::milliseconds(reconnTime_));
    periodicReconnectTimer_.async_wait(boost::asio::bind_executor(strand_,
        std::bind(&Lpr::handleReconnectTimerTimeout, this, std::placeholders::_1)));
}

void Lpr::handleFrontSocketConnect(bool success, const std::string& message)
{
    if (success)
    {
        // state changed: ERROR → CONNECTED
        if (!lastFrontCameraConnected_)
        {
            // Temp: Will enable after EEP integration
            // operation::getInstance()->HandlePBSError(LPRNoError);
        }
        lastFrontCameraConnected_ = true;

        std::stringstream ss;
        ss << __func__ << "() Successfully connected to Server @ Front Camera IP : " << lprIp4Front_ << ", Port: " << lprPort_;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");

        /* Temp: Disabled for MiniPC
        pFrontCamera_->send("OK");
        std::stringstream okss;
        okss << "Front camera: Send OK";
        Logger::getInstance()->FnLog(okss.str(), logFileName_, "LPR");
        */
    }
    else
    {
        // state changed: CONNECTED → ERROR
        if (lastFrontCameraConnected_)
        {
            // Temp: Will enable after EEP integration
            // operation::getInstance()->HandlePBSError(LPRError);
        }
        lastFrontCameraConnected_ = false;

        std::stringstream ss;
        ss << __func__ << "() Failed to connect to Server @ Front Camera IP: " << lprIp4Front_ << ", Port: " << lprPort_ << ", Error: " << message;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
    }
}

void Lpr::handleFrontSocketClose(bool success, const std::string& message)
{
    if (success)
    {
        std::stringstream ss;
        ss << __func__ << "() Sucessfully closed the Server @ Front Camera IP : " << lprIp4Front_ << ", Port: " << lprPort_;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << "() Failed to close the Server @ Front Camera IP : " << lprIp4Front_ << ", Port: " << lprPort_ << ", Error: " << message;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
    }
}

void Lpr::handleFrontSocketSend(bool success, const std::string& message)
{
    if (success)
    {
        std::stringstream ss;
        ss << __func__ << "() Successfully sent to Server @ Front Camera IP : " << lprIp4Front_;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << "() Failed to send to Server @ Front Camera IP : " << lprIp4Front_ << ", Error: " << message;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
    }
}

void Lpr::handleReceiveFrontCameraData(bool success, const std::vector<uint8_t>& data)
{
    if (success)
    {
        std::stringstream ss;
        ss << "Receive TCP Data @ Front Camera, IP : " << lprIp4Front_ << " Data : ";
        ss.write(reinterpret_cast<const char*>(data.data()), data.size());
        ss << " , Length : " << data.size();
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");

        std::string receiveDataStr(reinterpret_cast<const char*>(data.data()), data.size());
        processData(receiveDataStr, CType::FRONT_CAMERA);
    }
    else
    {
        Logger::getInstance()->FnLog("Failed to receive TCP Data @ Front Camera. Likely socket read error.", logFileName_, "LPR");
    }
}

/********************************************************************/
/********************************************************************/

void Lpr::initRearCamera(const std::string& cameraIP, int tcpPort, const std::string cameraCH)
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
            pRearCamera_ = std::make_unique<AppTcpClient>(ioContext_, cameraIP, tcpPort);
            pRearCamera_->setConnectHandler([this](bool success, const std::string& message) { handleRearSocketConnect(success, message); });
            pRearCamera_->setCloseHandler([this](bool success, const std::string& message) { handleRearSocketClose(success, message); });
            pRearCamera_->setReceiveHandler([this](bool success, const std::vector<uint8_t>& data) { handleReceiveRearCameraData(success, data); });
            pRearCamera_->setSendHandler([this](bool success, const std::string& message) { handleRearSocketSend(success, message); });
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

void Lpr::handleReconnectTimer2Timeout(const boost::system::error_code& error)
{
    if (error)
    {
        std::ostringstream oss;
        oss << "Reconnect Timer error: " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "LPR");
    }

    if (!pRearCamera_->isConnected())
    {
        std::stringstream ss;
        ss << "Reconnect to Server @ Rear Camera IP: " << lprIp4Rear_ << ", Port: " << lprPort_;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");

        pRearCamera_->connect();
    }

    startReconnectTimer2();
}

void Lpr::startReconnectTimer2()
{
    periodicReconnectTimer2_.expires_after(std::chrono::milliseconds(reconnTime2_));
    periodicReconnectTimer2_.async_wait(boost::asio::bind_executor(strand_,
        std::bind(&Lpr::handleReconnectTimer2Timeout, this, std::placeholders::_1)));
}

void Lpr::handleRearSocketConnect(bool success, const std::string& message)
{
    if (success)
    {
        Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

        // state changed: ERROR → CONNECTED
        if (!lastRearCameraConnected_)
        {
            // Temp: Will enable after EEP integration
            // operation::getInstance()->HandlePBSError(LPRNoError);
        }
        lastRearCameraConnected_ = true;

        std::stringstream ss;
        ss << __func__ << "() Successfully connected to Server @ Rear Camera IP : " << lprIp4Rear_ << ", Port: " << lprPort_;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");

        /* Temp: Disabled for MiniPC
        pRearCamera_->send("OK");
        std::stringstream okss;
        okss << "Rear camera: Send OK";
        Logger::getInstance()->FnLog(okss.str(), logFileName_, "LPR");
        */
    }
    else
    {
        // state changed: CONNECTED → ERROR
        if (lastRearCameraConnected_)
        {
            // Temp: Will enable after EEP integration
            // operation::getInstance()->HandlePBSError(LPRError);
        }
        lastRearCameraConnected_ = false;

        std::stringstream ss;
        ss << __func__ << "() Failed to connect to Server @ Rear Camera IP: " << lprIp4Rear_ << ", Port: " << lprPort_ << ", Error: " << message;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
    }
}

void Lpr::handleRearSocketClose(bool success, const std::string& message)
{
    if (success)
    {
        Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

        std::stringstream ss;
        ss << __func__ << "() Sucessfully closed the Server @ Rear Camera IP : " << lprIp4Rear_ << ", Port: " << lprPort_;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << "() Failed to close the Server @ Rear Camera IP : " << lprIp4Rear_ << ", Port: " << lprPort_ << ", Error: " << message;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
    }
}

void Lpr::handleRearSocketSend(bool success, const std::string& message)
{
    if (success)
    {
        Logger::getInstance()->FnLog(__func__, logFileName_, "LPR");

        std::stringstream ss;
        ss << __func__ << "() Successfully sent to Server @ Rear Camera IP : " << lprIp4Rear_;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << "() Failed to send to Server @ Rear Camera IP : " << lprIp4Rear_ << ", Error: " << message;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");
    }
}

void Lpr::handleReceiveRearCameraData(bool success, const std::vector<uint8_t>& data)
{
    if (success)
    {
        std::stringstream ss;
        ss << "Receive TCP Data @ Rear Camera, IP : " << lprIp4Rear_ << " Data : ";
        ss.write(reinterpret_cast<const char*>(data.data()), data.size());
        ss << " , Length : " << data.size();
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");

        std::string receiveDataStr(reinterpret_cast<const char*>(data.data()), data.size());
        processData(receiveDataStr, CType::REAR_CAMERA);
    }
    else
    {
        Logger::getInstance()->FnLog("Failed to receive TCP Data @ Rear Camera. Likely socket read error.", logFileName_, "LPR");
    }
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
    if (pFrontCamera_->isConnected())
    {
        try
        {
            std::stringstream ss;
            ss << "Front camera, SendData : " << transID;
            Logger::getInstance()->FnLog(ss.str());
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");

            std::vector<uint8_t> data(transID.begin(), transID.end());
            pFrontCamera_->send(data);
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
    if (pRearCamera_->isConnected())
    {
        try
        {
            std::stringstream ss;
            ss << "Rear camera, SendData : " << transID;
            Logger::getInstance()->FnLog(ss.str());
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LPR");

            std::vector<uint8_t> data(transID.begin(), transID.end());
            pRearCamera_->send(data);
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

void Lpr::processData(const std::string& tcpData, CType camType)
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

            if (((tmpStr.size() == 7) && (tmpStr[1] == "LPRS_STX") && (tmpStr[5] == "LPRS_ETX"))
                || ((tmpStr.size() == 8) && (tmpStr[1] == "LPRS_STX") && (tmpStr[6] == "LPRS_ETX")))
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

    if ((tmpStr.size() == 7) || (tmpStr.size() == 8))
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