#pragma once

#include <iostream>
#include <string>
#include <vector>
#include "boost/asio.hpp"
#include "boost/thread.hpp"
#include "boost/asio/serial_port.hpp"

class Antenna
{

public:

    struct ReadResult
    {
        std::vector<char> data;
        bool success;
    };

    enum class AntCmdID
    {
        NONE_CMD,
        MANUAL_CMD,
        FORCE_GET_IU_NO_CMD,
        USE_OF_ANTENNA_CMD,
        NO_USE_OF_ANTENNA_CMD,
        SET_ANTENNA_TIME_CMD,
        MACHINE_CHECK_CMD,
        SET_ANTENNA_DATA_CMD,
        GET_ANTENNA_DATA_CMD,
        REQ_IO_STATUS_CMD,
        SET_OUTPUT_CMD
    };

    // None Command
    static const char NONE_CATEGORY                 = 0x00;
    static const char NONE_COMMAND_NO               = 0x00;
    // Manual Command
    static const char MANUAL_CATEGORY               = 0x00;
    static const char MANUAL_COMMAND_NO             = 0x01;
    // Force Get IU Command
    static const char FORCE_GET_IU_CATEGORY         = 0x3F;
    static const char FORCE_GET_IU_COMMAND_NO       = 0x32;
    // Use Of Antenna Command
    static const char USE_OF_ANTENNA_CATEGORY       = 0x32;
    static const char USE_OF_ANTENNA_COMMAND_NO     = 0x33;
    // No Use Of Antenna Command
    static const char NO_USE_OF_ANTENNA_CATEGORY    = 0x32;
    static const char NO_USE_OF_ANTENNA_COMMAND_NO  = 0x32;
    // Set Antenna Time Command
    static const char SET_ANTENNA_TIME_CATEGORY     = 0x32;
    static const char SET_ANTENNA_TIME_COMMAND_NO   = 0x30;
    // Machine Check Command
    static const char MACHINE_CHECK_CATEGORY        = 0x31;
    static const char MACHINE_CHECK_COMMAND_NO      = 0x33;
    // Set Antenna Data Command
    static const char SET_ANTENNA_DATA_CATEGORY     = 0x32;
    static const char SET_ANTENNA_DATA_COMMAND_NO   = 0x40;
    // Get Antenna Data Command
    static const char GET_ANTENNA_DATA_CATEGORY     = 0x32;
    static const char GET_ANTENNA_DATA_COMMAND_NO   = 0x41;
    // Req IQ Status Command
    static const char REQ_IO_STATUS_CATEGORY        = 0x31;
    static const char REQ_IO_STATUS_COMMAND_NO      = 0x34;
    // Set Output Command
    static const char SET_OUTPUT_CATEGORY           = 0x32;
    static const char SET_OUTPUT_COMMAND_NO         = 0x31;

    enum class AntCmdRetCode
    {
        AntRecv_CmdNotFound = -2,
        AntRecv_NoResp      = -1,
        AntRecv_ACK         = 1,
        AntRecv_NAK         = 0,
        AntRecv_CRCErr      = 2
    };

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
    void FnAntennaInit(unsigned int baudRate, const std::string& comPortName);
    void FnAntennaSendReadIUCmd();

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
    boost::asio::io_context io_serial_context;
    std::unique_ptr<boost::asio::io_context::strand> pStrand_;
    std::unique_ptr<boost::asio::serial_port> pSerialPort_;
    int antennaCmdTimeoutInMillisec_;
    int antennaCmdMaxRetry_;
    int antennaIUCmdMinOKtimes_;
    int TxNum_;
    int RxNum_;
    RX_STATE rxState_;
    unsigned char txBuff[TX_BUF_SIZE];
    unsigned char rxBuff[RX_BUF_SIZE];
    unsigned char rxBuffData[RX_BUF_SIZE];
    std::string IUNumberPrev_;
    std::string IUNumber_;
    int successRecvIUCount_;
    bool successRecvIUFlag_;
    Antenna();
    int antennaInit();
    int antennaCmd(AntCmdID cmdID);
    std::vector<unsigned char> loadSetAntennaData(unsigned char destID, 
                                                unsigned char sourceID,
                                                unsigned char category,
                                                unsigned char command,
                                                unsigned char seqNo,
                                                unsigned char antennaID,
                                                int placeID,
                                                unsigned char parkingNo,
                                                unsigned char antennaMode,
                                                unsigned char enqTimer,
                                                int doubleCommTimer,
                                                int cmdWaitTimer);
    std::vector<unsigned char> loadGetAntennaData(unsigned char destID, unsigned char sourceID, unsigned char category, unsigned char command, unsigned char seqNo);
    std::vector<unsigned char> loadNoUseOfAntenna(unsigned char destID, unsigned char sourceID, unsigned char category, unsigned char command, unsigned char seqNo);
    std::vector<unsigned char> loadUseOfAntenna(unsigned char destID, unsigned char sourceID, unsigned char category, unsigned char command, unsigned char seqNo);
    std::vector<unsigned char> loadSetAntennaTime(unsigned char destID, unsigned char sourceID, unsigned char category, unsigned char command, unsigned char seqNo);
    std::vector<unsigned char> loadMachineCheck(unsigned char destID, unsigned char sourceID, unsigned char category, unsigned char command, unsigned char seqNo);
    std::vector<unsigned char> loadReqIOStatus(unsigned char destID, unsigned char sourceID, unsigned char category, unsigned char command, unsigned char seqNo);
    std::vector<unsigned char> loadSetOutput(unsigned char destID, unsigned char sourceID, unsigned char category, unsigned char command, unsigned char seqNo, unsigned char maskIO, unsigned char statusOfIO);
    std::vector<unsigned char> loadForceGetUINo(unsigned char destID, unsigned char sourceID, unsigned char category, unsigned char command, unsigned char seqNo);
    void antennaCmdSend(const std::vector<unsigned char>& dataBuff);
    ReadResult antennaReadWithTimeout(int milliseconds);
    AntCmdRetCode antennaHandleCmdResponse(AntCmdID cmd, const std::vector<char>& dataBuff);
    std::string antennaCmdIDToString(AntCmdID cmd);
    unsigned int CRC16R_Calculate(unsigned char* s, unsigned char len, unsigned int crc);
    unsigned char* getTxBuf();
    unsigned char* getRxBuf();
    int getTxNum();
    int getRxNum();
    int receiveRxDataByte(char c);
    void handleIgnoreState(char c);
    void handleEscape2StartState(char c);
    void handleReceivingState(char c);
    void handleEscapeState(char c);
    void handleRXCRC1State(char c, char &b);
    int handleRXCRC2State(char c, unsigned int &rxcrc, char b);
    void resetState();
};