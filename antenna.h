#pragma once

#include <iostream>
#include <string>
#include "boost/asio.hpp"
#include "boost/thread.hpp"
#include "boost/asio/serial_port.hpp"

class Antenna
{

public:
    static const int MIN_SUCCESS_TIME = 2;
    static const int MAX_RETRY_TIME = 10;
    static const int TX_BUF_SIZE = 128;
    static const int RX_BUF_SIZE = 128;
    static const char STX = 0x02;
    static const char ETX = 0x03;
    static const char DLE = 0x10;
    static unsigned char SEQUENCE_NO;

    enum class RX_STATE
    {
        IGNORE,
        RECEIVING,
        ESCAPE,
        ESCAPE2START,
        RXCRC1,
        RXCRC2
    };

    static Antenna* getInstance();
    void FnAntennaInit(boost::asio::io_context& io_context, unsigned int baudRate, const std::string& comPortName);
    void FnAntennaSendReadIUCmd();
    void FnAntennaCheck();
    void FnAntennaStopAsyncRead();
    void FnAntennaCmdResponseTimerStart();
    void FnAntennaCmdResponseTimerStop();

    /**
     * Singleton Antenna should not be cloneable.
     */
    Antenna(Antenna& antenna) = delete;

    /**
     * Singleton Antenna should not be assignable. 
     */
    void operator=(const Antenna&) = delete;

private:
    boost::mutex mutex_;
    static Antenna* antenna_;
    std::unique_ptr<boost::asio::io_context::strand> pStrand_;
    std::unique_ptr<boost::asio::serial_port> pSerialPort_;
    std::unique_ptr<boost::asio::steady_timer> pAntennaCmdResponseTimer_;
    int TxNum_;
    int RxNum_;
    RX_STATE rxState_;
    unsigned char txBuff[TX_BUF_SIZE];
    unsigned char rxBuff[RX_BUF_SIZE];
    unsigned char rxBuffData[RX_BUF_SIZE];
    std::string IUNumberPrev_;
    std::string IUNumber_;
    int retryCount_;
    int successRecvIUCount_;
    Antenna();
    void setTxAntennaData(unsigned char Aid, unsigned char Hid, unsigned char seqNo);
    unsigned int CRC16R_Calculate(unsigned char* s, unsigned char len, unsigned int crc);
    unsigned char* getTxBuf();
    unsigned char* getRxBuf();
    int getTxNum();
    int getRxNum();
    void handleOnReceive(const boost::system::error_code& ec, size_t bytes_transferred);
    int receiveRxDataByte(char c);
    void handleIgnoreState(char c);
    void handleEscape2StartState(char c);
    void handleReceivingState(char c);
    void handleEscapeState(char c);
    void handleRXCRC1State(char c, char &b);
    int handleRXCRC2State(char c, unsigned int &rxcrc, char b);
    void resetState();
    void handleOnCmdResponseTimeout(const boost::system::error_code& ec);
};