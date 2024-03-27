#pragma once

#include <chrono>
#include <iostream>
#include <string>
#include <memory>
#include "tcp.h"

class Lpr
{
public:
    enum class CType
    {
        FRONT_CAMERA = 0,
        REAR_CAMERA = 1
    };

    static Lpr* getInstance();

    void LprInit(boost::asio::io_context& mainIOContext);

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
    std::unique_ptr<TcpClient> pFrontCamera_;
    std::unique_ptr<TcpClient> pRearCamera_;
    std::unique_ptr<boost::asio::deadline_timer> periodicReconnectTimer_;
    std::unique_ptr<boost::asio::deadline_timer> periodicReconnectTimer2_;
    std::unique_ptr<boost::asio::deadline_timer> periodicStatusTimer_;
    std::unique_ptr<boost::asio::deadline_timer> periodicStatusTimer2_;
    Lpr();
    bool initFrontCamera(boost::asio::io_context& mainIOContext, const std::string& cameraIP, int tcpPort, const std::string cameraCH);
    bool initRearCamera(boost::asio::io_context& mainIOContext, const std::string& cameraIP, int tcpPort, const std::string cameraCH);
    void sendTransIDToLPR(const std::string& transID);
    void startReconnectTimer();
    void handleReconnectTimerTimeout();
    void startReconnectTimer2();
    void handleReconnectTimer2Timeout();
    void handleReceiveFrontCameraData(const char* data, std::size_t length);
    void handleFrontSocketConnect();
    void handleFrontSocketClose();
    void handleFrontSocketError(std::string error_msg);
    void handleReceiveRearCameraData(const char* data, std::size_t length);
    void handleRearSocketConnect();
    void handleRearSocketClose();
    void handleRearSocketError(std::string error_msg);

    void SendTransIDToLPR(const std::string& transID);
    void SendTransIDToLPR_Front(const std::string& transID);
    void SendTransIDToLPR_Rear(const std::string& transID);

    void processData(const std::string tcpData, CType camType);
    std::string extractSTX(const std::string& sMsg);
    std::string extractETX(const std::string& sMsg);
    void extractLPRData(const std::string& tcpData, CType camType);
};