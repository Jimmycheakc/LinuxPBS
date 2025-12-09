#pragma once

#include <iostream>
#include <mutex>
#include "boost/asio.hpp"

class ShutdownManager
{

public:

    static ShutdownManager* getInstance();

    void set(boost::asio::io_context* io, boost::asio::executor_work_guard<boost::asio::io_context::executor_type>* guard);
    boost::asio::io_context* getIoContext();
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>* getWorkGuard();
    
    // Graceful shutdown callable from anywhere
    void gracefulShutdown();
    
    /**
     * Singleton ShutdownManager should not be cloneable.
     */
    ShutdownManager (ShutdownManager& shutdownManager) = delete;

    /**
     * Singleton ShutdownManager should not be assignable.
     */
    void operator=(const ShutdownManager&) = delete;

private:
    static ShutdownManager* shutdownManager_;
    static std::mutex mutex_;
    boost::asio::io_context* ioContext_ = nullptr;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>* workGuard_ = nullptr;
    std::atomic<bool> shutdownInitiated_{false};
    
    ShutdownManager();
};