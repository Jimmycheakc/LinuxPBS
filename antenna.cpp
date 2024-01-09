#include <iostream>
#include <sstream>
#include "antenna.h"
#include "common.h"
#include "ini_parser.h"
#include "log.h"

Antenna* Antenna::antenna_ = nullptr;
unsigned char Antenna::SEQUENCE_NO = 0;

Antenna::Antenna()
    : TxNum_(0),
    RxNum_(0),
    rxState_(Antenna::RX_STATE::IGNORE),
    retryCount_(0),
    successRecvIUCount_(0)
{
    memset(txBuff, 0 , sizeof(txBuff));
    memset(rxBuff, 0, sizeof(rxBuff));
    memset(rxBuffData, 0, sizeof(rxBuffData));
    IUNumber_ = "";
    IUNumberPrev_ = "";
}

Antenna* Antenna::getInstance()
{
    if (antenna_ == nullptr)
    {
        antenna_ = new Antenna();
    }
    return antenna_;
}

void Antenna::FnAntennaInit(boost::asio::io_context& io_context, unsigned int baudRate, const std::string& comPortName)
{
    pStrand_ = std::make_unique<boost::asio::io_context::strand>(io_context);
    pSerialPort_ = std::make_unique<boost::asio::serial_port>(io_context, comPortName);
    pAntennaCmdResponseTimer_ = std::make_unique<boost::asio::steady_timer>(io_context);

    pSerialPort_->set_option(boost::asio::serial_port_base::baud_rate(baudRate));
    pSerialPort_->set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
    pSerialPort_->set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::even));
    pSerialPort_->set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
    pSerialPort_->set_option(boost::asio::serial_port_base::character_size(8));

    std::stringstream ss;
    if (pSerialPort_->is_open())
    {
        ss << "Successfully open serial port for Antenna Communication: " << comPortName;
    }
    else
    {
        ss << "Failed to open serial port for Antenna Communication: " << comPortName;
    }
    Logger::getInstance()->FnLog(ss.str());
}

void Antenna::handleIgnoreState(char c)
{
    if (c == Antenna::DLE)
    {
        rxState_ = RX_STATE::ESCAPE2START;
    }
}

void Antenna::handleEscape2StartState(char c)
{
    if (c == Antenna::STX)
    {
        rxState_ = RX_STATE::RECEIVING;
        RxNum_ = 0;
        memset(rxBuff, 0, sizeof(rxBuff));
    }
    else
    {
        resetState();
    }
}

void Antenna::handleReceivingState(char c)
{
    if (c == Antenna::DLE)
    {
        rxState_ = RX_STATE::ESCAPE;
    }
    else
    {
        rxBuff[RxNum_++] = c;
    }
}

void Antenna::handleEscapeState(char c)
{
    if (c == Antenna::STX)
    {
        rxState_= RX_STATE::RECEIVING;
        RxNum_ = 0;
        memset(rxBuff, 0, sizeof(rxBuff));
    }
    else if (c == Antenna::ETX)
    {
        rxBuff[RxNum_++] = c;
        rxState_ = RX_STATE::RXCRC1;
    }
    else if (c == Antenna::DLE)
    {
        rxBuff[RxNum_++] = c;
        rxState_ = RX_STATE::RECEIVING;
    }
    else
    {
        resetState();
    }
}

void Antenna::handleRXCRC1State(char c, char &b)
{
    b = c;
    rxState_ = RX_STATE::RXCRC2;
}

int Antenna::handleRXCRC2State(char c, unsigned int &rxcrc, char b)
{
    rxState_ = RX_STATE::IGNORE;
    unsigned int crc = ( (static_cast<unsigned int>(c) << 8) | (b & 0xFF) ) & 0xFFFF;
    rxcrc = CRC16R_Calculate(rxBuff, RxNum_, rxcrc);
    
    if (rxcrc == crc)
    {
        return RxNum_;
    }
    else
    {
        resetState();
        return 0;
    }
}

void Antenna::resetState()
{
    rxState_ = RX_STATE::IGNORE;
    RxNum_ = 0;
    memset(rxBuff, 0, sizeof(rxBuff));
}

int Antenna::receiveRxDataByte(char c)
{
    static char b;
    unsigned int rxcrc = 0xFFFF;
    int ret = 0;

    switch (rxState_)
    {
        case RX_STATE::IGNORE:
            handleIgnoreState(c);
            break;
        case RX_STATE::ESCAPE2START:
            handleEscape2StartState(c);
            break;
        case RX_STATE::RECEIVING:
            handleReceivingState(c);
            break;
        case RX_STATE::ESCAPE:
            handleEscapeState(c);
            break;
        case RX_STATE::RXCRC1:
            handleRXCRC1State(c, b);
            break;
        case RX_STATE::RXCRC2:
            ret = handleRXCRC2State(c, rxcrc, b);
            break;
    }

    return ret;
}

void Antenna::handleOnReceive(const boost::system::error_code& ec, size_t bytes_transferred)
{
    Logger::getInstance()->FnLog(__func__);

    if (!pSerialPort_->is_open())
    {
        return;
    }

    if (ec)
    {
        if (ec == boost::asio::error::operation_aborted)
        {
            // Handle cancellation, if needed
            Logger::getInstance()->FnLog("Asynchronous read operation canceled.");
        }
        else
        {
            std::stringstream ss;
            ss << __func__ << " Antenna receive IU error: " << ec.what();
            Logger::getInstance()->FnLog(ss.str());
        }
        return;
    }

    for (unsigned int i = 0; i < bytes_transferred; i++)
    {
        char c = rxBuffData[i];
        int len = receiveRxDataByte(c);

        switch(len)
        {
            case 13:
            {
                std::stringstream ss;
                ss << "Antenna Raw Data: " << Common::getInstance()->FnGetUCharArrayToHexString(getRxBuf(), 13);
                Logger::getInstance()->FnLog(ss.str());
                
                if (successRecvIUCount_ < MIN_SUCCESS_TIME)
                {
                    std::string IUNumberCurr = Common::getInstance()->FnGetLittelEndianUCharArrayToHexString(getRxBuf(), 7, 5);

                    if (IUNumberPrev_.empty())
                    {
                        IUNumberPrev_ = IUNumberCurr;
                        successRecvIUCount_++;
                    }
                    else
                    {
                        if (IUNumberPrev_ == IUNumberCurr)
                        {
                            successRecvIUCount_++;
                        }
                    }
                    FnAntennaCheck();
                }
                else
                {
                    IUNumber_ = IUNumberPrev_;
                    std::stringstream ss;
                    ss << "Antenna IU : " << IUNumber_;
                    Logger::getInstance()->FnLog(ss.str());

                    successRecvIUCount_ = 0;
                    IUNumberPrev_ = "";
                    FnAntennaCmdResponseTimerStop();
                    FnAntennaStopAsyncRead();
                }
                break;
            }
            case 8:
            {
                unsigned char* tmpBuff = getRxBuf();

                if (tmpBuff[3] == 0x82)
                {
                    Logger::getInstance()->FnLog("Antenna : NAK received");
                }
                FnAntennaCheck();
                break;
            }
            default:
                break;
        }

    }
}

void Antenna::FnAntennaCheck()
{
    Logger::getInstance()->FnLog(__func__);

    if (!pSerialPort_->is_open())
    {
        return;
    }

    pSerialPort_->async_read_some(
        boost::asio::buffer(rxBuffData, RX_BUF_SIZE),
        boost::asio::bind_executor(*pStrand_,
            [this](const boost::system::error_code& ec, size_t byte_transferred){
                handleOnReceive(ec, byte_transferred);
            }));
}

unsigned int Antenna::CRC16R_Calculate(unsigned char* s, unsigned char len, unsigned int crc)
{
    // CRC order: 16
    // CCITT(recommandation) : F(x) = x16 + x12 + x5 + 1
    // CRC Poly: 0x8408 <=> 0x1021
    // Operational initial value: 0xFFFF
    // Final xor value: 0xFFFF
    unsigned char i, j;
    for (i = 0; i < len; i++, s++)
    {
        crc ^= ((unsigned int)(*s)) & 0xFF;
        for (j = 0; j < 8; j++)
        {
            if (crc & 0x0001)
            {
                crc = (crc >> 1) ^ 0x8408;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    crc ^= 0xFFFF;
    return crc;
}

void Antenna::setTxAntennaData(unsigned char Aid, unsigned char Hid, unsigned char seqNo)
{
    unsigned int txCrc = 0xFFFF;
    unsigned char tempChar;
    int i = 0, tempBuffSize = 8;
    unsigned char tempBuff[tempBuffSize];
    memset(tempBuff, 0, sizeof(tempBuff));
    memset(txBuff, 0, sizeof(txBuff));

    tempBuff[0] = Aid;
    tempBuff[1] = Hid;
    tempBuff[2] = 0x3F;
    tempBuff[3] = 0x32;
    tempBuff[4] = seqNo;
    tempBuff[5] = 0x00;
    tempBuff[6] = 0x00;

    // Construct Tx Frame
    // Start of frame
    txBuff[i++] = Antenna::DLE;
    txBuff[i++] = Antenna::STX;
    // Data of frame, exclude ETX
    for (int j = 0; j < (tempBuffSize - 1) ; j++)
    {
        tempChar = tempBuff[j];
        if (tempChar == Antenna::DLE)
        {
            txBuff[i++] = Antenna::DLE;
        }
        txBuff[i++] = tempChar;
    }
    // End of Frame
    txBuff[i++] = Antenna::DLE;
    txBuff[i++] = Antenna::ETX;
    
    // Include ETX in CRC Calculation
    tempBuff[7] = Antenna::ETX;
    txCrc = CRC16R_Calculate(tempBuff, tempBuffSize, txCrc);
    txBuff[i++] = txCrc & 0xFF;
    txBuff[i++] = (txCrc >> 8) & 0xFF;
    TxNum_ = i;
}

unsigned char* Antenna::getTxBuf()
{
    return txBuff;
}

unsigned char* Antenna::getRxBuf()
{
    return rxBuff;
}

int Antenna::getTxNum()
{
    return TxNum_;
}

int Antenna::getRxNum()
{
    return RxNum_;
}

void Antenna::FnAntennaSendReadIUCmd()
{
    Logger::getInstance()->FnLog(__func__);

    if (!pSerialPort_->is_open())
    {
        return;
    }

    unsigned char antennaID = 0xB0 + IniParser::getInstance()->FnGetAntennaId();
    unsigned char hostID = 0x00;

    if ((++SEQUENCE_NO) == 0)
    {
        // For sequence number of 1 to 255
        ++SEQUENCE_NO;
    }

    /* Set Tx Data buffer */
    setTxAntennaData(antennaID, hostID, SEQUENCE_NO);

    /* Send Tx Data buffer via serial */
    boost::asio::write(*pSerialPort_, boost::asio::buffer(getTxBuf(), getTxNum()));
}

void Antenna::FnAntennaStopAsyncRead()
{
    Logger::getInstance()->FnLog(__func__);

    if (!pSerialPort_->is_open())
    {
        return;
    }

    // Stop any async serial operations within the strand
    pStrand_->post([this]() {
        pSerialPort_->cancel();
    });
}

void Antenna::handleOnCmdResponseTimeout(const boost::system::error_code& ec)
{
    Logger::getInstance()->FnLog(__func__);

    if (!pSerialPort_->is_open())
    {
        return;
    }

    if (ec)
    {
        if (ec == boost::asio::error::operation_aborted)
        {
            Logger::getInstance()->FnLog("Timer cancelled.");
        }
        else
        {
            std::stringstream ss;
            ss << __func__ << " Antenna Response Timeout Error: " << ec.what();
            Logger::getInstance()->FnLog(ss.str());
        }
        return;
    }

    // Repeat the timer every second
    if (retryCount_ < MAX_RETRY_TIME)
    {
        retryCount_++;
        FnAntennaSendReadIUCmd();
        FnAntennaCmdResponseTimerStart();
    }
    else
    {
        retryCount_ = 0;
        FnAntennaCmdResponseTimerStop();
        FnAntennaStopAsyncRead();
    }
}

void Antenna::FnAntennaCmdResponseTimerStart()
{
    Logger::getInstance()->FnLog(__func__);

    // Start timer 1 second
    pAntennaCmdResponseTimer_->expires_from_now(std::chrono::seconds(IniParser::getInstance()->FnGetWaitIUNoTime()));

    pAntennaCmdResponseTimer_->async_wait(boost::asio::bind_executor(*pStrand_, 
        [this](const boost::system::error_code& ec) {
        handleOnCmdResponseTimeout(ec);
    }));
}

void Antenna::FnAntennaCmdResponseTimerStop()
{
    Logger::getInstance()->FnLog(__func__);

    // Stop any async timer operations within the strand
    pStrand_->post([this]() {
        pAntennaCmdResponseTimer_->cancel();
    });
}