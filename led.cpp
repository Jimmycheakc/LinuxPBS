#include "led.h"
#include "log.h"
#include "operation.h"

const char LED::STX1 = 0x02;
const char LED::ETX1 = 0x0D;
const char LED::ETX2 = 0x0A;

LED::LED(boost::asio::io_context& io_context, unsigned int baudRate, const std::string& comPortName, int maxCharacterPerRow)
    : strand_(boost::asio::make_strand(io_context)),
      serialPort_(io_context),
      baudRate_(baudRate),
      comPortName_(comPortName),
      maxCharPerRow_(maxCharacterPerRow)
{
    std::string ledType = "";
    logFileName_ = "led";
    if (maxCharacterPerRow == LED614_MAX_CHAR_PER_ROW)
    {
        ledType = "LED 614";
        logFileName_ = "led614";
    }
    else if (maxCharacterPerRow == LED216_MAX_CHAR_PER_ROW)
    {
        ledType = "LED 216";
        logFileName_ = "led216";
    }
    else if (maxCharacterPerRow == LED226_MAX_CHAR_PER_ROW)
    {
        ledType = "LED 226";
        logFileName_ = "led226";
    }

    Logger::getInstance()->FnCreateLogFile(logFileName_);

    try
    {
        serialPort_.open(comPortName);
        serialPort_.set_option(boost::asio::serial_port_base::baud_rate(baudRate));
        serialPort_.set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
        serialPort_.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
        serialPort_.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
        serialPort_.set_option(boost::asio::serial_port_base::character_size(8));

        if (serialPort_.is_open())
        {
            std::stringstream ss;
            ss << "Successfully open serial port: " << comPortName;
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LED");

            FnLEDSendLEDMsg("***", "", LED::Alignment::LEFT);

            Logger::getInstance()->FnLog(ledType + " initialization completed.");
            Logger::getInstance()->FnLog(ledType + " initialization completed.", logFileName_, "LED");
        }
        else
        {
            std::stringstream ss;
            ss << "Failed to open serial port: " << comPortName;
            Logger::getInstance()->FnLog(ss.str());
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LED");
        }
    }
    catch (const boost::system::system_error& e) // Catch Boost.Asio system errors
    {
        std::stringstream ss;
        ss << __func__ << ", Boost Asio Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
}

LED::~LED()
{
    serialPort_.close();
}

unsigned int LED::FnGetLEDBaudRate() const
{
    return baudRate_;
}

std::string LED::FnGetLEDComPortName() const
{
    return comPortName_;
}

int LED::FnGetLEDMaxCharPerRow() const
{
    return maxCharPerRow_;
}

void LED::FnLEDSendLEDMsg(std::string LedId, std::string text, LED::Alignment align)
{
    if (!serialPort_.is_open())
    {
        return;
    }

    try
    {
        if (LedId.empty())
        {
            LedId = "***";
        }

        if (LedId.length() == 3)
        {
            if (maxCharPerRow_ == LED216_MAX_CHAR_PER_ROW || maxCharPerRow_ == LED226_MAX_CHAR_PER_ROW)
            {
                std::string Line1Text, Line2Text;

                std::size_t found = text.find("^");
                if (found != std::string::npos)
                {
                    Line1Text = text.substr(0, found);
                    Line2Text = text.substr(found + 1, (text.length() - found - 1));
                }
                else
                {
                    Line1Text = text;
                    Line2Text = "";
                }

                std::vector<char> msg_line1;
                FnFormatDisplayMsg(LedId, LED::Line::FIRST, Line1Text, align, msg_line1);
                boost::asio::async_write(serialPort_, boost::asio::buffer(msg_line1.data(), msg_line1.size()),
                    boost::asio::bind_executor(strand_, 
                        [this](boost::system::error_code ec, std::size_t bytes_transferred) {
                            if (ec) 
                            {
                                std::stringstream ss;
                                ss << __func__ << "Failed to write LED msg: " << ec.what();
                                Logger::getInstance()->FnLogExceptionError(ss.str());
                            }
                }));

                std::vector<char> msg_line2;
                FnFormatDisplayMsg(LedId, LED::Line::SECOND, Line2Text, align, msg_line2);
                boost::asio::async_write(serialPort_, boost::asio::buffer(msg_line2.data(), msg_line2.size()),
                    boost::asio::bind_executor(strand_, 
                        [this](boost::system::error_code ec, std::size_t bytes_transferred) {
                            if (ec) 
                            {
                                std::stringstream ss;
                                ss << __func__ << "Failed to write LED msg: " << ec.what();
                                Logger::getInstance()->FnLogExceptionError(ss.str());
                            }
                }));

                // Send LED Messages to Monitor
                if (operation::getInstance()->FnIsOperationInitialized())
                {
                    operation::getInstance()->FnSendLEDMessageToMonitor(Line1Text, Line2Text);
                }
            }
            else if (maxCharPerRow_ == LED614_MAX_CHAR_PER_ROW)
            {
                std::vector<char> msg;
                FnFormatDisplayMsg(LedId, LED::Line::FIRST, text, align, msg);
                boost::asio::async_write(serialPort_, boost::asio::buffer(msg.data(), msg.size()),
                    boost::asio::bind_executor(strand_, 
                        [this](boost::system::error_code ec, std::size_t bytes_transferred) {
                            if (ec) 
                            {
                                std::stringstream ss;
                                ss << __func__ << "Failed to write LED msg: " << ec.what();
                                Logger::getInstance()->FnLogExceptionError(ss.str());
                            }
                }));
            }
        }
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", text: " << text << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", text: " << text << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
}

void LED::FnFormatDisplayMsg(std::string LedId, LED::Line lineNo, std::string text, LED::Alignment align, std::vector<char>& result)
{   
    result.clear();

    // Msg Header
    result.push_back(LED::STX1);
    result.push_back(LED::STX1);
    result.insert(result.end(), LedId.begin(), LedId.end());
    result.push_back(0x5E);
    if (lineNo == LED::Line::FIRST)
    {
        result.push_back(0x31);
    }
    else if (lineNo == LED::Line::SECOND)
    {
        result.push_back(0x32);
    }

    // Msg Text
    std::string formattedText;
    int replaceTextIdx = 0;
    if (maxCharPerRow_ == LED614_MAX_CHAR_PER_ROW)
    {
        formattedText.resize(maxCharPerRow_ + 1, 0x20);
    }
    else
    {
        formattedText.resize(maxCharPerRow_, 0x20);
    }

    if (text.length() > maxCharPerRow_)
    {
        text.resize(maxCharPerRow_);
    }

    switch (align)
    {
        case LED::Alignment::LEFT:
            replaceTextIdx = 0;
            break;
        case LED::Alignment::RIGHT:
            replaceTextIdx = maxCharPerRow_ - text.length();
            break;
        case LED::Alignment::CENTER:
            if (maxCharPerRow_ != LED614_MAX_CHAR_PER_ROW)
            {
                replaceTextIdx = (maxCharPerRow_ - text.length()) / 2;
            }
            break;
    }
    formattedText.replace(replaceTextIdx, text.length(), text);
    result.insert(result.end(), formattedText.begin(), formattedText.end());

    // Msg Tail
    result.push_back(LED::ETX1);
    result.push_back(LED::ETX2);
}

// LED Manager
LEDManager* LEDManager::ledManager_ = nullptr;
std::mutex LEDManager::mutex_;

LEDManager::LEDManager()
{

}

LEDManager* LEDManager::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (ledManager_ == nullptr)
    {
        ledManager_ = new LEDManager();
    }

    return ledManager_;
}

void LEDManager::createLED(boost::asio::io_context& io_context, unsigned int baudRate, const std::string& comPortName, int maxCharacterPerRow)
{
    try
    {
        leds_.push_back(std::make_unique<LED>(io_context, baudRate, comPortName, maxCharacterPerRow));
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", baudRate: " << baudRate << ", comPortName: " << comPortName << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", baudRate: " << baudRate << ", comPortName: " << comPortName << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }

}

LED* LEDManager::getLED(const std::string& ledComPort)
{
    for (auto& led : leds_)
    {
        if (led != nullptr && led->FnGetLEDComPortName() == ledComPort)
        {
            return led.get();
        }
    }
    return nullptr;
}
