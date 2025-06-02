#pragma once

#include <iostream>
#include <mutex>
#include <thread>
#include <queue>
#include <deque>
#include <vector>
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <cstdint>
#include "boost/asio.hpp"
#include "boost/asio/serial_port.hpp"
#include "boost/asio/deadline_timer.hpp"

class KSM_Reader
{
public:
    struct ReadResult
    {
        std::vector<char> data;
        bool success;
    };

    enum class RX_STATE
    {
        IDLE,
        RECEIVING
    };

    enum class KSMReaderCmdID
    {
        INIT_CMD,
        CARD_PROHIBITED_CMD,
        CARD_ALLOWED_CMD,
        CARD_ON_IC_CMD,
        IC_POWER_ON_CMD,
        WARM_RESET_CMD,
        SELECT_FILE1_CMD,
        SELECT_FILE2_CMD,
        READ_CARD_INFO_CMD,
        READ_CARD_BALANCE_CMD,
        IC_POWER_OFF_CMD,
        EJECT_TO_FRONT_CMD,
        GET_STATUS_CMD
    };

    enum class KSMReaderCmdRetCode
    {
        KSMReaderComm_Error       = -4,
        KSMReaderSend_Failed      = -3,
        KSMReaderRecv_CmdNotFound = -2,
        KSMReaderRecv_NoResp      = -1,
        KSMReaderRecv_ACK         = 1,
        KSMReaderRecv_NAK         = 0,
        KSMReaderRecv_CRCErr      = 2
    };

    enum class STATE
    {
        IDLE,
        SENDING_REQUEST_ASYNC,
        WAITING_FOR_ACK,
        WAITING_FOR_RESPONSE,
        STATE_COUNT
    };

    enum class EVENT
    {
        COMMAND_ENQUEUED,
        WRITE_COMPLETED,
        WRITE_FAILED,
        ACK_TIMEOUT,
        ACK_TIMER_CANCELLED_ACK_RECEIVED,
        RESPONSE_TIMEOUT,
        RESPONSE_TIMER_CANCELLED_RSP_RECEIVED,
        WRITE_TIMEOUT,
        EVENT_COUNT
    };

    struct EventTransition
    {
        EVENT event;
        void (KSM_Reader::*eventHandler)(EVENT);
        STATE nextState;
    };

    struct StateTransition
    {
        STATE stateName;
        std::vector<EventTransition> transitions;
    };

    struct CommandWithData
    {
        KSMReaderCmdID cmd;

        explicit CommandWithData(KSMReaderCmdID c) : cmd(c) {}
    };

    static const char STX = 0x02;
    static const char ETX = 0x03;
    static const char DLE = 0x10;
    static const char LF  = 0x0A;
    static const char CR  = 0x0D;
    static const char ACK = 0x06;
    static const char NAK = 0x15;
    static const char ENQ = 0x05;
    static const int TX_BUF_SIZE = 128;
    static const int RX_BUF_SIZE = 128;

    static KSM_Reader* getInstance();
    int FnKSMReaderInit(unsigned int baudRate, const std::string& comPortName);
    void FnKSMReaderClose();
    void FnKSMReaderEnable(bool enable);
    void FnKSMReaderReadCardInfo();

    void FnKSMReaderSendInit();
    void FnKSMReaderSendGetStatus();
    void FnKSMReaderStartGetStatus();
    void FnKSMReaderSendEjectToFront();

    std::string FnKSMReaderGetCardNum();
    bool FnKSMReaderGetCardExpired();
    int FnKSMReaderGetCardExpiryDate();
    long FnKSMReaderGetCardBalance();

    /**
     * Singleton KSM_Reader should not be cloneable.
     */
    KSM_Reader(KSM_Reader &ksm_reader) = delete;

    /**
     * Singleton KSM_Reader should not be assignable.
     */
    void operator=(const KSM_Reader &) = delete;

private:

    static KSM_Reader* ksm_reader_;
    static std::mutex mutex_;
    boost::asio::io_context ioContext_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    std::thread ioContextThread_;
    std::unique_ptr<boost::asio::serial_port> pSerialPort_;
    std::string logFileName_;
    unsigned char txBuff[TX_BUF_SIZE];
    unsigned char rxBuff[RX_BUF_SIZE];
    int TxNum_;
    int RxNum_;
    unsigned char recvbcc_;
    RX_STATE rxState_;
    std::array<uint8_t, 1024> readBuffer_;
    bool write_in_progress_;
    std::queue<std::vector<uint8_t>> writeQueue_;
    bool cardPresented_;
    std::string cardNum_;
    int cardExpiryYearMonth_;
    bool cardExpired_;
    long cardBalance_;
    static const StateTransition stateTransitionTable[static_cast<int>(STATE::STATE_COUNT)];
    STATE currentState_;
    std::mutex cmdQueueMutex_;
    std::deque<CommandWithData> commandQueue_;
    static std::mutex currentCmdMutex_;
    KSMReaderCmdID currentCmd;
    std::atomic<bool> ackRecv_;
    std::atomic<bool> rspRecv_;
    boost::asio::deadline_timer ackTimer_;
    boost::asio::deadline_timer rspTimer_;
    boost::asio::deadline_timer serialWriteTimer_;
    std::atomic<bool> continueReadCardFlag_;
    std::atomic<bool> blockGetStatusCmdLogFlag_;
    std::chrono::steady_clock::time_point lastSerialReadTime_;
    boost::asio::deadline_timer serialWriteDelayTimer_;
    KSM_Reader();
    void startIoContextThread();
    void sendEnq();
    void resetRxState();
    unsigned char* getTxBuff();
    unsigned char* getRxBuff();
    int getTxNum();
    int getRxNum();
    void readerCmdSend(const std::vector<unsigned char>& dataBuff);
    std::vector<unsigned char> loadInitReader();
    std::vector<unsigned char> loadCardProhibited();
    std::vector<unsigned char> loadCardAllowed();
    std::vector<unsigned char> loadCardOnIc();
    std::vector<unsigned char> loadICPowerOn();
    std::vector<unsigned char> loadWarmReset();
    std::vector<unsigned char> loadSelectFile1();
    std::vector<unsigned char> loadSelectFile2();
    std::vector<unsigned char> loadReadCardInfo();
    std::vector<unsigned char> loadReadCardBalance();
    std::vector<unsigned char> loadICPowerOff();
    std::vector<unsigned char> loadEjectToFront();
    std::vector<unsigned char> loadGetStatus();
    std::string KSMReaderCmdIDToString(KSMReaderCmdID cmdID);
    void ksmReaderCmd(KSMReaderCmdID cmdID);
    KSMReaderCmdRetCode ksmReaderHandleCmdResponse(KSMReaderCmdID cmd, const std::vector<char>& dataBuff);
    bool responseIsComplete(const std::vector<char>& buffer, std::size_t bytesTransferred);
    int receiveRxDataByte(char c);
    void enqueueWrite(const std::vector<uint8_t>& data);
    void startWrite();
    void writeEnd(const boost::system::error_code& error, std::size_t bytesTransferred);
    void startRead();
    void readEnd(const boost::system::error_code& error, std::size_t bytesTransferred);
    std::string stateToString(STATE state);
    std::string eventToString(EVENT event);
    void processEvent(EVENT event);
    void checkCommandQueue();
    void enqueueCommand(KSMReaderCmdID cmd);
    void popFromCommandQueueAndEnqueueWrite();
    void handleIdleState(EVENT event);
    void handleSendingRequestAsyncState(EVENT event);
    void handleWaitingForAckState(EVENT event);
    void handleWaitingForResponseState(EVENT event);
    void handleSerialWriteTimeout(const boost::system::error_code& error);
    void handleAckTimeout(const boost::system::error_code& error);
    void handleCmdResponseTimeout(const boost::system::error_code& error);
    void handleCmdErrorOrTimeout(KSMReaderCmdID cmd, KSMReaderCmdRetCode retCode);
    void setCurrentCmd(KSMReaderCmdID cmd);
    KSMReaderCmdID getCurrentCmd();
    void startAckTimer();
    void startResponseTimer();
    void startSerialWriteTimer();
    void ksmLogger(const std::string& logMsg);
};