#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <optional>
#include <utility>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/serial_port.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

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
        AntSend_Failed      = -3,
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

    // Starts Antenna's OWN io_context and dedicated worker thread.
    // All serial/timer/coroutine work is executed by that one thread.
    void FnAntennaInit(unsigned int baudRate, const std::string& comPortName);

    // Gracefully cancels outstanding Antenna work and joins the worker thread.
    void FnAntennaShutdown();

    // Thread-safe public entry points. Work is posted into Antenna's io_context.
    void FnAntennaSendReadIUCmd();
    void FnAntennaStopRead();

    // These snapshots may be read from another thread.
    bool FnGetIsCmdExecuting() const;
    int FnAntennaGetIUCmdSendCount();

    /**
     * Singleton Antenna should not be cloneable.
     */
    Antenna(Antenna& antenna) = delete;

    /**
     * Singleton Antenna should not be assignable. 
     */
    void operator=(const Antenna&) = delete;

private:

    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    // Antenna owns its event loop and exactly one thread runs it.
    boost::asio::io_context ioContext_;
    std::optional<WorkGuard> workGuard_;
    std::thread ioThread_;

    std::unique_ptr<boost::asio::serial_port> pSerialPort_;
    std::unique_ptr<boost::asio::steady_timer> periodicSendReadIUCmdTimer_;

    std::string logFileName_;

    // Atomic only because public getters/start/stop can be called from outside
    // the io_context thread. All protocol state below is owned by one io thread.
    std::atomic<bool> moduleRunning_;
    std::atomic<bool> stopping_;
    std::atomic<bool> continueReadFlag_;
    std::atomic<bool> isCmdExecuting_;
    std::atomic<int> antIUCmdSendCount_;

    bool iuLoopRunning_;
    bool initializationCompleted_;

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
    ~Antenna();

    void shutdownOnIoThread();

    // C++20 coroutine entry points.
    boost::asio::awaitable<void> antennaModuleInitAsync(unsigned int baudRate, std::string comPortName);
    boost::asio::awaitable<int> antennaInitAsync();
    boost::asio::awaitable<AntCmdRetCode> antennaCmdAsync(AntCmdID cmdID);
    boost::asio::awaitable<bool> antennaCmdSendAsync(const std::vector<unsigned char>& dataBuff);
    boost::asio::awaitable<ReadResult> antennaReadWithTimeoutAsync(int milliseconds);
    boost::asio::awaitable<void> readIULoopAsync();

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
    void resetRxBuffer();
    bool responseIsComplete(const std::vector<char>& buffer, std::size_t bytesTransferred);
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