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
    int reconnTime_;
    int reconnTime2_;
    std::string stdID_;
    std::string logFileName_;
    Lpr();
    void iniFrontCamera(const std::string& cameraIP, int tcpPort, const std::string cameraCH);
    void iniRearCamera(const std::string& cameraIP, int tcpPort, const std::string cameraCH);
    void sendTransIDToLPR(const std::string& transID);
};