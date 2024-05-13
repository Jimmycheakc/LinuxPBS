#pragma once

#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include "boost/asio.hpp"
#include "boost/asio/serial_port.hpp"

class Upt
{

public:
    static Upt* getInstance();
    void FnUptInit(boost::asio::io_context& io_context, unsigned int baudRate, const std::string& comPortName);
    void FnUptCleanUp();
    void FnUptSerialWrite(const std::string& data, std::size_t nBytesWrite);
    void FnUptSerialRead(std::string& data, std::size_t& nBytesRead);

    /**
     * Singleton Upt should not be cloneable.
     */
    Upt(Upt& upt) = delete;

    /**
     * Singleton Upt should not be assignable.
     */
    void operator=(const Upt&) = delete;

private:
    static Upt* upt_;
    static std::mutex mutex_;
    std::unique_ptr<boost::asio::io_context::strand> pStrand_;
    std::unique_ptr<boost::asio::serial_port> pSerialPort_;
    bool initialized;
    Upt();

    void UptSerialWriteHandle(const boost::system::error_code& ec, std::size_t bytes_transferred);
    void UptSerialReadHandle(const boost::system::error_code& ec, std::size_t bytes_received);
};