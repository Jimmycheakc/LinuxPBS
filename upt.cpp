#include "log.h"
#include "upt.h"

Upt* Upt::upt_;

Upt::Upt()
    : initialized(false)
{

}

Upt* Upt::getInstance()
{
    if (upt_ == nullptr)
    {
        upt_ = new Upt();
    }
    return upt_;
}

void Upt::FnUptInit(boost::asio::io_context& io_context, unsigned int baudRate, const std::string& comPortName)
{
    if (!initialized)
    {
        pStrand_.reset(new boost::asio::io_context::strand(io_context));
        pSerialPort_.reset(new boost::asio::serial_port(io_context, comPortName));

        pSerialPort_->set_option(boost::asio::serial_port_base::baud_rate(baudRate));
        pSerialPort_->set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
        pSerialPort_->set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
        pSerialPort_->set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
        pSerialPort_->set_option(boost::asio::serial_port_base::character_size(8));

        if (pSerialPort_->is_open())
        {
            std::stringstream ss;
            ss << "Successfully open serial port: " << comPortName << std::endl;
            Logger::getInstance()->FnLog(ss.str());
        }
        else
        {
            std::stringstream ss;
            ss << "Failed to open serial port: " << comPortName << std::endl;
            Logger::getInstance()->FnLog(ss.str());
        }
        initialized = true;
    }
}

void Upt::FnUptCleanUp()
{
    if (initialized)
    {
        pSerialPort_->close();
        initialized = false;
    }
}


void Upt::FnUptSerialWrite(const std::string& data, std::size_t nBytesWrite)
{
    // Temp: Need to revisit
}

void Upt::UptSerialWriteHandle(const boost::system::error_code& ec, std::size_t bytes_transferred)
{
    // Temp: Need to revisit
}

void Upt::FnUptSerialRead(std::string& data, std::size_t& nBytesRead)
{
    // Temp: Need to revisit
}

void Upt::UptSerialReadHandle(const boost::system::error_code& ec, std::size_t bytes_received)
{
    // Temp: Need to revisit
}