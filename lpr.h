#pragma once

#include <chrono>
#include <iostream>
#include <string>
#include <memory>
#include <mutex>
#include "tcp_client.h"

class Lpr
{
public:
    enum class CType
    {
        FRONT_CAMERA = 0,
        REAR_CAMERA = 1
    };

    struct LPREventData
    {
        CType camType;
        std::string LPN;
        std::string TransID;
        std::string imagePath;
    };

    static Lpr* getInstance();

    void FnLprInit();
    void FnSendTransIDToLPR(const std::string& transID, bool useFrontCamera);
    struct LPREventData deserializeEventData(const std::string& serializeData);
    void FnLprClose();

    /**
     * Singleton Lpr should not be cloneable.
     */
    Lpr(Lpr& lpr) = delete;

    /**
     * Singleton Lpr should not be assignable. 
     */
    void operator=(const Lpr&) = delete;

private:
    static Lpr* lpr_;
    static std::mutex mutex_;
    boost::asio::io_context ioContext_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    std::thread ioContextThread_;
    int cameraNo_;
    std::string lprIp4Front_;
    std::string lprIp4Rear_;
    int reconnTime_;
    int reconnTime2_;
    std::string stdID_;
    std::string logFileName_;
    int commErrorTimeCriteria_;
    int transErrorCountCriteria_;
    std::string frontCamCH_;
    std::string rearCamCH_;
    std::unique_ptr<AppTcpClient> pFrontCamera_;
    std::unique_ptr<AppTcpClient> pRearCamera_;
    int lprPort_;
    boost::asio::steady_timer periodicReconnectTimer_;
    boost::asio::steady_timer periodicReconnectTimer2_;
    bool frontCameraInitialized_;
    bool rearCameraInitialized_;
    bool lastFrontCameraConnected_;
    bool lastRearCameraConnected_;
    Lpr();
    void startIoContextThread();
    void initFrontCamera(const std::string& cameraIP, int tcpPort, const std::string cameraCH);
    void initRearCamera(const std::string& cameraIP, int tcpPort, const std::string cameraCH);
    void startReconnectTimer();
    void handleReconnectTimerTimeout(const boost::system::error_code& error);
    void startReconnectTimer2();
    void handleReconnectTimer2Timeout(const boost::system::error_code& error);
    void handleReceiveFrontCameraData(bool success, const std::vector<uint8_t>& data);
    void handleFrontSocketConnect(bool success, const std::string& message);
    void handleFrontSocketClose(bool success, const std::string& message);
    void handleFrontSocketSend(bool success, const std::string& message);
    void handleReceiveRearCameraData(bool success, const std::vector<uint8_t>& data);
    void handleRearSocketConnect(bool success, const std::string& message);
    void handleRearSocketClose(bool success, const std::string& message);
    void handleRearSocketSend(bool success, const std::string& message);

    void SendTransIDToLPR_Front(const std::string& transID);
    void SendTransIDToLPR_Rear(const std::string& transID);

    void processData(const std::string& tcpData, CType camType);
    std::string extractSTX(const std::string& sMsg);
    std::string extractETX(const std::string& sMsg);
    void extractLPRData(const std::string& tcpData, CType camType);
    std::string serializeEventData(const struct LPREventData& eventData);
};