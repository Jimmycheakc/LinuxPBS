#pragma once

#include <iostream>
#include <boost/asio.hpp>
#include <thread>
#include "log.h"

class Timer
{
public:
    Timer() : ioContext_(), work_(ioContext_), timer_(ioContext_), interval_(0), running_(false)
    {
        ioThread_ = std::thread([this]() { ioContext_.run(); });
    }
    
    ~Timer()
    {
        stop();
        ioContext_.stop();
        if (ioThread_.joinable())
        {
            ioThread_.join();
        }
    }

    void start(int interval, std::function<void()> callback)
    {
        interval_ = interval;
        callback_ = callback;
        running_ = true;
        startTimer();
    }

    void stop()
    {
        if (running_)
        {
            running_ = false;
            ioContext_.post([this]() { timer_.cancel(); });
        }
    }

private:
    boost::asio::io_context ioContext_;
    boost::asio::io_context::work work_;
    std::thread ioThread_;
    boost::asio::deadline_timer timer_;
    int interval_;
    std::function<void()> callback_;
    bool running_;

    void startTimer()
    {
        timer_.expires_from_now(boost::posix_time::milliseconds(interval_));
        timer_.async_wait([this](const boost::system::error_code& error) {
            handleTimer(error);
        });
    }

    void handleTimer(const boost::system::error_code& error)
    {
        if (!error && running_)
        {
            callback_();
            //startTimer();
            // Restart the timer asynchronously
            ioContext_.post([this]() {
                timer_.expires_at(timer_.expires_at() + boost::posix_time::milliseconds(interval_));
                timer_.async_wait([this](const boost::system::error_code& error) {
                    handleTimer(error); // Callback for the new timer expiration
                });
            });
        }
        else
        {
            std::stringstream ss;
            ss << "Timer error :" << error.message();
            Logger::getInstance()->FnLog(ss.str());
        }
    }
};