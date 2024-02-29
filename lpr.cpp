#include "lpr.h"

Lpr* Lpr::lpr_ = nullptr;

Lpr::Lpr()
    : cameraNo_(0),
    reconnTime_(0),
    reconnTime2_(0),
    stdID_(""),
    logFileName_("LPR")
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
    
}