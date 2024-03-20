#pragma once

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
    std::unique_ptr<TcpClient> pFrontCamera_;
    Lpr();
    void initFrontCamera(const std::string& cameraIP, int tcpPort, const std::string cameraCH);
    void initRearCamera(const std::string& cameraIP, int tcpPort, const std::string cameraCH);
    void sendTransIDToLPR(const std::string& transID);
};