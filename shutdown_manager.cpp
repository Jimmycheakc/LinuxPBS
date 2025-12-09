#include "shutdown_manager.h"
#include "common.h"
#include "lcd.h"
#include "log.h"

ShutdownManager* ShutdownManager::shutdownManager_ = nullptr;
std::mutex ShutdownManager::mutex_;

ShutdownManager::ShutdownManager()
{

}

ShutdownManager* ShutdownManager::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdownManager_ == nullptr)
    {
        shutdownManager_ = new ShutdownManager();
    }
    return shutdownManager_;
}

void ShutdownManager::set(boost::asio::io_context* io, boost::asio::executor_work_guard<boost::asio::io_context::executor_type>* guard)
{
    ioContext_ = io;
    workGuard_ = guard;
}

boost::asio::io_context* ShutdownManager::getIoContext()
{
    return ioContext_;
}

boost::asio::executor_work_guard<boost::asio::io_context::executor_type>* ShutdownManager::getWorkGuard()
{
    return workGuard_;
}

void ShutdownManager::gracefulShutdown()
{
    if (!ioContext_ || !workGuard_)
        return;

    // Prevent multiple shutdowns
    if (shutdownInitiated_.exchange(true))
        return;

    Logger::getInstance()->FnLog("Station Program terminating gracefully.");

    std::string LCDLine1Msg = ">>> STN STOPPED <<< ";
    std::string LCDLine2Msg = Common::getInstance()->FnGetDateTimeFormat_ddmmyyy_hhmmss();
    LCD::getInstance()->FnLCDClearDisplayRow(1);
    LCD::getInstance()->FnLCDClearDisplayRow(2);
    LCD::getInstance()->FnLCDDisplayRow(1, const_cast<char*>(LCDLine1Msg.c_str()));
    LCD::getInstance()->FnLCDDisplayRow(2, const_cast<char*>(LCDLine2Msg.c_str()));

    usleep(500000);

    // Release work guard and stop io_context
    workGuard_->reset();
    ioContext_->stop();
}