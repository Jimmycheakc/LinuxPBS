#pragma once

#include <iostream>
#include "boost/asio.hpp"
#include "boost/asio/serial_port.hpp"

class KSM_Reader
{
public:
    struct ReadResult
    {
        std::vector<char> data;
        bool success;
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
        KSMReaderSend_Failed      = -3,
        KSMReaderRecv_CmdNotFound = -2,
        KSMReaderRecv_NoResp      = -1,
        KSMReaderRecv_ACK         = 1,
        KSMReaderRecv_NAK         = 0,
        KSMReaderRecv_CRCErr      = 2
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
    void FnKSMReaderInit(boost::asio::io_context& mainIOContext, unsigned int baudRate, const std::string& comPortName);
    bool FnGetIsCmdExecuting() const;
    int FnKSMReaderEnable(bool enable);
    int FnKSMReaderReadCardInfo();

    int FnKSMReaderSendGetStatus();
    int FnKSMReaderSendEjectToFront();

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
    boost::asio::io_context* pMainIOContext_;
    boost::asio::io_context io_serial_context;
    std::unique_ptr<boost::asio::io_context::strand> pStrand_;
    std::unique_ptr<boost::asio::serial_port> pSerialPort_;
    std::unique_ptr<boost::asio::deadline_timer> periodicSendReadCardCmdTimer_;
    std::atomic<bool> continueReadFlag_;
    std::atomic<bool> isCmdExecuting_;
    std::string logFileName_;
    unsigned char txBuff[TX_BUF_SIZE];
    unsigned char rxBuff[RX_BUF_SIZE];
    int TxNum_;
    int RxNum_;
    char recvbcc_;
    bool cardPresented_;
    std::string cardNum_;
    int cardExpiryYearMonth_;
    bool cardExpired_;
    long cardBalance_;
    KSM_Reader();
    void resetRxBuffer();
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
    int ksmReaderCmd(KSMReaderCmdID cmdID);
    ReadResult ksmReaderReadWithTimeout(int milliseconds);
    bool responseIsComplete(const std::vector<char>& buffer, std::size_t bytesTransferred);
    int receiveRxDataByte(char c);
    void sendEnq();
    KSMReaderCmdRetCode ksmReaderHandleCmdResponse(KSMReaderCmdID cmd, const std::vector<char>& dataBuff);
    void handleReadCardStatusTimerExpiration();
    void stopReadCardStatusTimer();
    void startReadCardStatusTimer(int milliseconds);
};