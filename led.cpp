#include "led.h"
#include "log.h"
#include "operation.h"

const char LED::STX1 = 0x02;
const char LED::ETX1 = 0x0D;
const char LED::ETX2 = 0x0A;

LED::LED(boost::asio::io_context& io_context, unsigned int baudRate, const std::string& comPortName, int maxCharacterPerRow)
    : serialPort_(io_context, comPortName),
    baudRate_(baudRate),
    comPortName_(comPortName),
    maxCharPerRow_(maxCharacterPerRow)
{
    try
    {
        serialPort_.set_option(boost::asio::serial_port_base::baud_rate(baudRate));
        serialPort_.set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
        serialPort_.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
        serialPort_.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
        serialPort_.set_option(boost::asio::serial_port_base::character_size(8));

        if (serialPort_.is_open())
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
    }
    catch(const std::exception& e)
    {
        std::stringstream ss;
        ss << "Exception during LED Initialization: " << e.what() << std::endl;
        Logger::getInstance()->FnLog(ss.str());
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
                boost::asio::write(serialPort_, boost::asio::buffer(msg_line1.data(), msg_line1.size()));

                std::vector<char> msg_line2;
                FnFormatDisplayMsg(LedId, LED::Line::SECOND, Line2Text, align, msg_line2);
                boost::asio::write(serialPort_, boost::asio::buffer(msg_line2.data(), msg_line2.size()));

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
                boost::asio::write(serialPort_, boost::asio::buffer(msg.data(), msg.size()));
            }
        }
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << "Exception during LED write : " << e.what() << std::endl;
        Logger::getInstance()->FnLog(ss.str());
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
    formattedText.resize(maxCharPerRow_, 0x20);
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
    result.push_back(0x00);
}

// LED Manager
LEDManager* LEDManager::ledManager_ = nullptr;

LEDManager::LEDManager()
{

}

LEDManager* LEDManager::getInstance()
{
    if (ledManager_ == nullptr)
    {
        ledManager_ = new LEDManager();
    }

    return ledManager_;
}

void LEDManager::createLED(boost::asio::io_context& io_context, unsigned int baudRate, const std::string& comPortName, int maxCharacterPerRow)
{
    leds_.push_back(std::make_unique<LED>(io_context, baudRate, comPortName, maxCharacterPerRow));
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
