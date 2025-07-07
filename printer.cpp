#include <boost/asio.hpp>
#include <iostream>
#include <sstream>
#include <vector>
#include "common.h"
#include "event_manager.h"
#include "printer.h"
#include "log.h"

Printer* Printer::printer_ = nullptr;
std::mutex Printer::mutex_;

Printer::Printer()
    : ioContext_(),
    strand_(boost::asio::make_strand(ioContext_)),
    workGuard_(boost::asio::make_work_guard(ioContext_)),
    logFileName_("printer"),
    defaultFont_(2),
    defaultAlign_(static_cast<int>(CBM_ALIGN::CBM_LEFT)),
    lineSpace_(6),
    leftMargin_(0),
    printMode_(0),
    printerType_(PRINTER_TYPE::CBM1000),
    siteID_(0),
    cmdLeftMargin_(""),
    cmdCut_(""),
    isPrinterError_(false),
    selfTestInterval_(0),
    selfTestTimer_(ioContext_),
    monitorStatusTimer_(ioContext_)
{
    // Initialize fronts
    FC_[1]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x00'});
    FC_[2]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x08'});
    FC_[3]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x10'});
    FC_[4]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x20'});
    FC_[5]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x30'});
    FC_[6]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x18'});
    FC_[7]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x28'});
    FC_[8]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x38'});
    FC_[9]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x01'});
    FC_[10]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x09'});
    FC_[11] = std::string({ASCII::ESC, ASCII::EXCLAM, '\x11'});
    FC_[12] = std::string({ASCII::ESC, ASCII::EXCLAM, '\x21'});
    FC_[13] = std::string({ASCII::ESC, ASCII::EXCLAM, '\x31'});
    FC_[14] = std::string({ASCII::ESC, ASCII::EXCLAM, '\x19'});
    FC_[15] = std::string({ASCII::ESC, ASCII::EXCLAM, '\x29'});
    FC_[16] = std::string({ASCII::ESC, ASCII::EXCLAM, '\x39'});

    // Initialize FTP fronts
    FF_[1]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x00'});
    FF_[2]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x01'});
    FF_[3]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x02'});
    FF_[4]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x03'});
    FF_[5]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x12'});
    FF_[6]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x13'});
    FF_[7]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x20'});
    FF_[8]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x21'});
    FF_[9]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x22'});
    FF_[10]  = std::string({ASCII::ESC, ASCII::EXCLAM, '\x23'});
    FF_[11] = std::string({ASCII::ESC, ASCII::EXCLAM, '\x32'});
    FF_[12] = std::string({ASCII::ESC, ASCII::EXCLAM, '\x33'});

    // Initialize align
    Align_[0] = "";
    Align_[1] = std::string({ASCII::ESC, ASCII::a, '\x00'});
    Align_[2] = std::string({ASCII::ESC, ASCII::a, '\x01'});
    Align_[3] = std::string({ASCII::ESC, ASCII::a, '\x02'});
}

Printer* Printer::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (printer_ == nullptr)
    {
        printer_ = new Printer();
    }

    return printer_;
}

bool Printer::FnPrinterInit(unsigned int baudRate, const std::string& comPortName)
{
    int ret = false;
    pSerialPort_ = std::make_unique<boost::asio::serial_port>(ioContext_, comPortName);

    try
    {
        pSerialPort_->set_option(boost::asio::serial_port_base::baud_rate(baudRate));
        pSerialPort_->set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
        pSerialPort_->set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
        pSerialPort_->set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
        pSerialPort_->set_option(boost::asio::serial_port_base::character_size(8));

        Logger::getInstance()->FnCreateLogFile(logFileName_);

        std::ostringstream oss;
        if (pSerialPort_->is_open())
        {
            oss << "Printer device initialization completed.";
            startIoContextThread();
            startRead();
            setPrinterSetting(printerType_, defaultAlign_, defaultFont_, siteID_, leftMargin_, selfTestInterval_);
            if (printerType_ != PRINTER_TYPE::FTP)
            {
                startSelfTestTimer(selfTestInterval_);
            }
            startMonitorStatusTimer();
            ret = true;
        }
        else
        {
            oss << "Printer device initialization failed.";
        }
        Logger::getInstance()->FnLog(oss.str());
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "PRINTER");
    }
    catch (const boost::system::system_error& e)
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

    if (ret)
    {
        EventManager::getInstance()->FnEnqueueEvent("Evt_handlePrinterStatus", static_cast<int>(PRINTER_STATUS::IDLE));
    }
    else
    {
        EventManager::getInstance()->FnEnqueueEvent("Evt_handlePrinterStatus", static_cast<int>(PRINTER_STATUS::ERROR));
    }

    return ret;
}

void Printer::FnPrinterClose()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "PRINTER");

    workGuard_.reset();
    ioContext_.stop();
    if (ioContextThread_.joinable())
    {
        ioContextThread_.join();
    }
}

void Printer::FnSetPrintMode(int mode)
{
    printMode_ = mode;
}

int Printer::FnGetPrintMode()
{
    return printMode_;
}

void Printer::FnSetDefaultAlign(Printer::CBM_ALIGN align)
{
    defaultAlign_ = static_cast<int>(align);
}

Printer::CBM_ALIGN Printer::FnGetDefaultAlign()
{
    return static_cast<CBM_ALIGN>(defaultAlign_);
}

void Printer::FnSetDefaultFont(int font)
{
    defaultFont_ = font;
}

int Printer::FnGetDefaultFont()
{
    return defaultFont_;
}

void Printer::FnSetLeftMargin(int leftMargin)
{
    leftMargin_ = leftMargin;

    if (leftMargin_ > 10)
    {
        leftMargin_ = leftMargin_ / 10;
    }

    switch (printerType_)
    {
        case PRINTER_TYPE::CBM:
        {
            int n1, n2;
            n1 = leftMargin_ % 256;
            n2 = leftMargin_ / 256;
            
            std::stringstream cmdLeftMarginSS_;
            cmdLeftMarginSS_ << ASCII::ESC << '$' << static_cast<char>(n1) << static_cast<char>(n2);
            cmdLeftMargin_ = "";
            cmdLeftMargin_.clear();
            cmdLeftMargin_ = cmdLeftMarginSS_.str();
            break;
        }
        case PRINTER_TYPE::FTP:
        {
            std::stringstream cmdLeftMarginSS_;
            cmdLeftMarginSS_ << ASCII::ESC << 'D' << static_cast<char>(leftMargin_) << "to" << static_cast<char>(1) << static_cast<char>(0) << ASCII::TAB;
            cmdLeftMargin_ = "";
            cmdLeftMargin_.clear();
            cmdLeftMargin_ = cmdLeftMarginSS_.str();
            break;
        }
        case PRINTER_TYPE::CBM1000:
        {
            std::stringstream cmdLeftMarginSS_;
            cmdLeftMarginSS_ << ASCII::ESC << 'D' << static_cast<char>(leftMargin_) << static_cast<char>(1) << static_cast<char>(0) << ASCII::TAB;
            cmdLeftMargin_ = "";
            cmdLeftMargin_.clear();
            cmdLeftMargin_ = cmdLeftMarginSS_.str();
            break;
        }
    }
}

void Printer::FnSetLineSpace(int space)
{
    lineSpace_ = space;
}

int Printer::FnGetLineSpace()
{
    return lineSpace_;
}

int Printer::FnGetLeftMargin()
{
    return leftMargin_;
}

void Printer::FnSetSelfTestInterval(int interval)
{
    selfTestInterval_ = interval;
}

int Printer::FnGetSelfTestInterval()
{
    return selfTestInterval_;
}

void Printer::FnSetSiteID(int id)
{
    siteID_ = id;
}

int Printer::FnGetSiteID()
{
    return siteID_;
}

void Printer::FnSetPrinterType(Printer::PRINTER_TYPE type)
{
    printerType_ = type;
}

Printer::PRINTER_TYPE Printer::FnGetPrinterType()
{
    return printerType_;
}

void Printer::startIoContextThread()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "PRINTER");

    if (!ioContextThread_.joinable())
    {
        ioContextThread_ = std::thread([this]() { ioContext_.run(); });
    }
}

// Serial read and write
void Printer::startRead()
{
    boost::asio::post(strand_, [this]() {
        pSerialPort_->async_read_some(
            boost::asio::buffer(readBuffer_, readBuffer_.size()),
            boost::asio::bind_executor(strand_,
                                        std::bind(&Printer::readEnd, this,
                                        std::placeholders::_1,
                                        std::placeholders::_2)));
    });
}

void Printer::readEnd(const boost::system::error_code& error, std::size_t bytesTransferred)
{
    if (!error)
    {
        std::vector<uint8_t> data(readBuffer_.begin(), readBuffer_.begin() + bytesTransferred);
        handleCmdResponse(data);
    }
    else
    {
        std::ostringstream oss;
        oss << "Serial Read error: " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "PRINTER");
    }

    startRead();
}

void Printer::enqueueWrite(const std::vector<uint8_t>& data)
{
    if (pSerialPort_ && pSerialPort_->is_open())
    {
        boost::asio::post(strand_, [this, data]() {
            bool write_in_progress_ = !writeQueue_.empty();
            writeQueue_.push(data);
            if (!write_in_progress_)
            {
                startWrite();
            }
        });
    }
    else
    {
        Logger::getInstance()->FnLog("Serial port is not initialized or not open.", logFileName_, "PRINTER");
    }
}

void Printer::startWrite()
{
    if (writeQueue_.empty())
    {
        return;
    }

    const auto& data = writeQueue_.front();
    std::ostringstream oss;
    oss << "Data sent : " << Common::getInstance()->FnGetDisplayVectorCharToHexString(data);
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "PRINTER");
    boost::asio::async_write(*pSerialPort_,
                            boost::asio::buffer(data),
                            boost::asio::bind_executor(strand_,
                                                        std::bind(&Printer::writeEnd, this,
                                                                    std::placeholders::_1,
                                                                    std::placeholders::_2)));
}

void Printer::writeEnd(const boost::system::error_code& error, std::size_t bytesTransferred)
{
    if (!error)
    {
        writeQueue_.pop();

        // Check if there's more data to write
        if (!writeQueue_.empty())
        {
            startWrite();
        }
    }
    else
    {
        std::ostringstream oss;
        oss << "Serial Write error: " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "PRINTER");
    }
}

void Printer::handleCmdResponse(const std::vector<uint8_t>& rsp)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "PRINTER");

    std::stringstream ss;
    ss << __func__ << " Response: ";
    for (uint8_t byte : rsp)
    {
        ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
    }
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "PRINTER");

    if (rsp.size() > 0)
    {
        if (printerType_ != PRINTER_TYPE::FTP)
        {
            selfTestTimer_.cancel();
            if (static_cast<char>(rsp[0]) == ASCII::NUL)
            {
                if (isPrinterError_ == true)
                {
                    EventManager::getInstance()->FnEnqueueEvent("Evt_handlePrinterStatus", static_cast<int>(PRINTER_STATUS::IDLE));
                    isPrinterError_ = false;
                }
            }
            else
            {
                if ((rsp[0] & (1 << 5)) != 0)
                {
                    if (isPrinterError_ == false)
                    {
                        isPrinterError_ = true;
                        EventManager::getInstance()->FnEnqueueEvent("Evt_handlePrinterStatus", static_cast<int>(PRINTER_STATUS::NO_PAPER));
                    }
                }
                else if (((rsp[0] & (1 << 4)) != 0) && ((rsp[0] & (1 << 1)) != 0))
                {
                    if (isPrinterError_ == true)
                    {
                        EventManager::getInstance()->FnEnqueueEvent("Evt_handlePrinterStatus", static_cast<int>(PRINTER_STATUS::IDLE));
                        isPrinterError_ = false;
                    }
                }
            }
        }
        else
        {
            if ((rsp.size() % 4) == 0)
            {
                for (int i = 0; i < rsp.size() / 4; i++)
                {
                    std::vector<uint8_t> cmd(rsp.begin() + i * 4, rsp.begin() + (i + 1) * 4);

                    if (cmd[0] == 8)
                    {
                        isPrinterError_ = true;
                        EventManager::getInstance()->FnEnqueueEvent("Evt_handlePrinterStatus", static_cast<int>(PRINTER_STATUS::ERROR));
                    }
                    else if (cmd[2] == 1)
                    {
                        isPrinterError_ = true;
                        EventManager::getInstance()->FnEnqueueEvent("Evt_handlePrinterStatus", static_cast<int>(PRINTER_STATUS::NO_PAPER));
                    }
                    else if (cmd[1] > 1)
                    {
                        isPrinterError_ = true;
                        EventManager::getInstance()->FnEnqueueEvent("Evt_handlePrinterStatus", static_cast<int>(PRINTER_STATUS::ERROR));
                    }
                    else
                    {
                        if (isPrinterError_ == true)
                        {
                            std::stringstream outputSS1_, outputSS2_, outputSS3_;
                            outputSS1_ << ASCII::FS << '9' << static_cast<char>(111);
                            outputSS2_ << ASCII::GS << 'a' << static_cast<char>(22);
                            outputSS3_ << ASCII::ESC << 'A' << static_cast<char>(lineSpace_);
                            EventManager::getInstance()->FnEnqueueEvent("Evt_handlePrinterStatus", static_cast<int>(PRINTER_STATUS::IDLE));
                            isPrinterError_ = false;
                        }
                    }
                }
            }
        }
    }
}

void Printer::startMonitorStatusTimer()
{
    monitorStatusTimer_.expires_from_now(boost::posix_time::seconds(10));
    monitorStatusTimer_.async_wait(boost::asio::bind_executor(strand_,
        std::bind(&Printer::handleMonitorStatusTimeout, this, std::placeholders::_1)));
}

void Printer::handleMonitorStatusTimeout(const boost::system::error_code& error)
{   
    if (!error)
    {
        if (isPrinterError_ == true)
        {
            if (printerType_ != PRINTER_TYPE::FTP)
            {
                Logger::getInstance()->FnLog(std::string(__func__) + " ,Send inquire status.", logFileName_, "PRINTER");
                inqStatus();    // Send inquire status if there's an error
            }
        }
    }
    else
    {
        std::ostringstream oss;
        oss << "Monitor Status Timer error : " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "PRINTER");
    }

    // Restart the timer to check status again after 10 seconds
    startMonitorStatusTimer();
}

void Printer::startSelfTestTimer(int milliseconds)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "PRINTER");

    inqStatus();

    selfTestTimer_.expires_from_now(boost::posix_time::milliseconds(milliseconds));
    selfTestTimer_.async_wait(boost::asio::bind_executor(strand_,
        std::bind(&Printer::handleSelfTestTimerTimeout, this, std::placeholders::_1)));
}

void Printer::handleSelfTestTimerTimeout(const boost::system::error_code& error)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "PRINTER");

    if (!error)
    {
        Logger::getInstance()->FnLog("Self Test Timer Timeout.", logFileName_, "PRINTER");

        if (isPrinterError_ == false)
        {
            isPrinterError_ = true;
            EventManager::getInstance()->FnEnqueueEvent("Evt_handlePrinterStatus", static_cast<int>(PRINTER_STATUS::ERROR));
        }
    }
    else if (error == boost::asio::error::operation_aborted)
    {
        Logger::getInstance()->FnLog("Self Test Timer Cancelled.", logFileName_, "PRINTER");
    }
    else
    {
        std::ostringstream oss;
        oss << "Self Test Timer error : " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "PRINTER");
    }
}

void Printer::inqStatus()
{
    std::stringstream outputSS_;
    // This is a command to request the printer status (but not applicable for CBM1000)
    //outputSS_ << ASCII::ESC << 'v';
    outputSS_ << ASCII::DLE << ASCII::EOT << ASCII::STX;
    enqueueWrite(Common::getInstance()->FnConvertStringToVector(outputSS_.str()));
}

void Printer::setPrinterSetting(Printer::PRINTER_TYPE type, int align, int font, int siteID, int leftMargin, int milliseconds)
{
    printerType_ = type;
    defaultAlign_ = align;
    defaultFont_ = font;
    siteID_ = siteID;
    selfTestInterval_ = milliseconds;
    
    if (leftMargin > 10)
    {
        leftMargin_ = leftMargin / 10;
    }

    switch (printerType_)
    {
        case PRINTER_TYPE::CBM:
        {
            int n1, n2;
            n1 = leftMargin_ % 256;
            n2 = leftMargin_ / 256;
            
            std::stringstream cmdLeftMarginSS_, cmdCutSS_;
            cmdLeftMarginSS_ << ASCII::ESC << '$' << static_cast<char>(n1) << static_cast<char>(n2);
            cmdLeftMargin_ = "";
            cmdLeftMargin_.clear();
            cmdLeftMargin_ = cmdLeftMarginSS_.str();
            cmdCutSS_ << ASCII::ESC << 'i';
            cmdCut_ = cmdCutSS_.str();
            for (int i = 0; i < 17; i++)
            {
                Font_[i] = FC_[i];
            }
            break;
        }
        case PRINTER_TYPE::FTP:
        {
            std::stringstream cmdLeftMarginSS_, cmdCutSS_;
            cmdLeftMarginSS_ << ASCII::ESC << 'D' << static_cast<char>(leftMargin_) << "to" << static_cast<char>(1) << static_cast<char>(0) << ASCII::TAB;
            cmdLeftMargin_ = "";
            cmdLeftMargin_.clear();
            cmdLeftMargin_ = cmdLeftMarginSS_.str();
            cmdCutSS_ << ASCII::GS << std::string("V") << static_cast<char>(0);
            cmdCut_ = cmdCutSS_.str();
            for (int i = 0; i < 13; i++)
            {
                Font_[i] = FF_[i];
            }

            std::stringstream detectionSS_, statusTransmissionSS_, spaceSS_;
            // Enable detection
            detectionSS_ << ASCII::FS << '9' << static_cast<char>(111);
            // Enable status transmission
            statusTransmissionSS_ << ASCII::GS << 'a' << static_cast<char>(14);
            // Set line space
            spaceSS_ << ASCII::ESC << 'A' << static_cast<char>(lineSpace_);

            enqueueWrite(Common::getInstance()->FnConvertStringToVector(detectionSS_.str()));
            enqueueWrite(Common::getInstance()->FnConvertStringToVector(statusTransmissionSS_.str()));
            enqueueWrite(Common::getInstance()->FnConvertStringToVector(spaceSS_.str()));
            break;
        }
        case PRINTER_TYPE::CBM1000:
        {
            std::stringstream cmdLeftMarginSS_;

            if (cmdLeftMargin_.empty())
            {
                cmdLeftMarginSS_ << ASCII::ESC << 'D' << static_cast<char>(leftMargin_) << static_cast<char>(1) << static_cast<char>(0) << ASCII::TAB;
                cmdLeftMargin_ = "";
                cmdLeftMargin_.clear();
                cmdLeftMargin_ = cmdLeftMarginSS_.str();
            }
            std::stringstream cmdCutSS_;
            cmdCutSS_ << ASCII::GS << 'V' << static_cast<char>(1);
            cmdCut_ = cmdCutSS_.str();
            for (int i = 0; i < 17; i++)
            {
                Font_[i] = FC_[i];
            }
            break;
        }
    }
}

void Printer::FnPrintLine(const std::string& text, int font, int align, bool underline, int font2)
{
    std::string UL, UL0;

    if (font2 > 0)
    {
        std::size_t pos = text.find(':');
        if (pos != std::string::npos)
        {
            UL = text.substr(0, pos + 1) + "";
            UL0 = text.substr(pos + 1);
            std::stringstream outputSS_;
            outputSS_ << cmdLeftMargin_ << Font_[font2] << UL << Font_[font] << UL0 << ASCII::LF;
            enqueueWrite(Common::getInstance()->FnConvertStringToVector(outputSS_.str()));
            return;
        }
    }

    if ((font == 99) && (printerType_ != PRINTER_TYPE::FTP))
    {
        std::stringstream outputSS_;
        outputSS_ << ASCII::ESC << '@';
        enqueueWrite(Common::getInstance()->FnConvertStringToVector(outputSS_.str()));
        return;
    }

    if (font == 0)
    {
        font = defaultFont_;
    }

    if (printerType_ != PRINTER_TYPE::FTP)
    {
        if (align == 0)
        {
            align = defaultAlign_;
        }

        if (underline == true)
        {
            std::stringstream ULSS_, UL0SS_;
            ULSS_ << ASCII::ESC << '-' << static_cast<char>(1);
            UL = ULSS_.str();
            UL0SS_ << ASCII::ESC << '-' << static_cast<char>(0);
            UL0 = UL0SS_.str();
        }
        else
        {
            UL = "";
            UL0 = "";
        }
    }
    else
    {
        align = 0;
        UL = "";
        UL0 = "";

        if (font > 12)
        {
            font = font - 12;
        }
    }

    std::stringstream outputSS_;
    outputSS_ << cmdLeftMargin_ << Align_[align] << Font_[font] << UL << text << UL0 << ASCII::LF;
    enqueueWrite(Common::getInstance()->FnConvertStringToVector(outputSS_.str()));

    lastAlign_ = align;
}

void Printer::FnFullCut(int bottom)
{
    if (bottom < 4)
    {
        bottom = 5;
    }

    std::stringstream outputSS_;
    if (printMode_ != 1)
    {
        outputSS_ << ASCII::ESC << 'd' << static_cast<char>(bottom) << cmdCut_;
    }
    else
    {
        outputSS_ << ASCII::GS << static_cast<char>(0x0C);
    }
    enqueueWrite(Common::getInstance()->FnConvertStringToVector(outputSS_.str()));

    if (pSerialPort_ && printerType_ != PRINTER_TYPE::FTP)
    {
        startSelfTestTimer(selfTestInterval_);
    }
}

void Printer::FnGetAllFonts()
{
    std::stringstream outputSS_;
    for (int i = 1; i < (sizeof(Font_)/sizeof(Font_[0])); i++)
    {
        outputSS_.str("");
        outputSS_.clear();
        outputSS_ << Font_[i] << i << ", abcdefghijklmnopqrstuvwxyz" << ASCII::LF;
        enqueueWrite(Common::getInstance()->FnConvertStringToVector(outputSS_.str()));
    }
    FnFullCut();
}

void Printer::FnPrintBarCode(const std::string& text, int height, int width, int fontSetting)
{
    std::stringstream cmdW_SS_, cmdH_SS_, cmdP_SS_; // Position
    std::stringstream cmdB_SS_; // Barcode command
    std::stringstream cmdA_SS_; // Alignment command
    int P, F;   // Font position and type

    if (printerType_ == PRINTER_TYPE::FTP)
    {
        return;
    }

    if (height == 0)
    {
        height = 80;
    }

    if (width == 0)
    {
        width = 3;
    }

    if (printMode_ == 1)
    {
        width = 2;
    }

    // Set width and height for barcode
    cmdW_SS_ << ASCII::GS << 'w' << static_cast<char>(width);
    cmdH_SS_ << ASCII::GS << 'h' << static_cast<char>(height);

    if (fontSetting == 0)
    {
        cmdP_SS_ << "";
    }
    else
    {
        P = fontSetting / 10;
        F = fontSetting % 10;
        cmdP_SS_ << ASCII::GS << 'H' << static_cast<char>(P) << ASCII::GS << 'f' << static_cast<char>(F);
    }

    switch (static_cast<CBM_ALIGN>(lastAlign_))
    {
        case CBM_ALIGN::CBM_LEFT:
        {
            cmdA_SS_ << ASCII::ESC << '$' << static_cast<char>(80) << static_cast<char>(0);
            break;
        }
        case CBM_ALIGN::CBM_CENTER:
        case CBM_ALIGN::CBM_RIGHT:
        {
            cmdA_SS_ << ASCII::ESC << '$' << static_cast<char>(0) << static_cast<char>(0);
            break;
        }
    }

    int len = text.length();

    if (printerType_ == PRINTER_TYPE::CBM1000)
    {
        cmdB_SS_ << ASCII::GS << 'k' << static_cast<char>(72) << static_cast<char>(len) << text << ASCII::LF;
    }
    else
    {
        cmdB_SS_ << ASCII::GS << 'k' << static_cast<char>(7) << 'A' << text << static_cast<char>(0) << ASCII::LF;
    }

    std::string output = cmdA_SS_.str() + cmdH_SS_.str() + cmdW_SS_.str() + cmdP_SS_.str() + cmdB_SS_.str();
    enqueueWrite(Common::getInstance()->FnConvertStringToVector(output));
}

void Printer::FnFeedLine(int line)
{
    std::stringstream lineSS_;
    lineSS_ << ASCII::ESC << 'J' << static_cast<char>(line);
    enqueueWrite(Common::getInstance()->FnConvertStringToVector(lineSS_.str()));
}

