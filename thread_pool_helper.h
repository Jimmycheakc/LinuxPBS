#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include <boost/asio/thread_pool.hpp>

class ThreadPoolHelper final
{
public:
    static std::unique_ptr<boost::asio::thread_pool> create(std::size_t threadCount, const std::string& baseName);

private:
    ThreadPoolHelper() = delete;
};