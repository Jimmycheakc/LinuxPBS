#pragma once

#include <atomic>
#include <iostream>
#include <filesystem>
#include <string>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include "boost/asio.hpp"
#include "boost/asio/serial_port.hpp"

class CscPacket
{

public:
    CscPacket();
    ~CscPacket();
    void setAttentionCode(uint8_t attn);
    uint8_t getAttentionCode() const;
    void setCode(uint8_t code);
    uint8_t getCode() const;
    void setType(uint8_t type);
    uint8_t getType() const;
    void setLength(uint16_t len);
    uint16_t getLength() const;
    void setPayload(const std::vector<uint8_t>& payload);
    std::vector<uint8_t> getPayload() const;
    void setCrc(uint16_t crc);
    uint16_t getCrc();
    std::vector<uint8_t> serializeWithoutCRC() const;
    std::vector<uint8_t> serialize() const;
    void deserialize(const std::vector<uint8_t>& data);
    std::string getMsgCscPacketOutput() const;

    void clear();

private:
    uint8_t attn;
    bool code;
    uint8_t type;
    uint16_t len;
    std::vector<uint8_t> payload;
    uint16_t crc;
};

class LCSCReader
{

public:
    const std::string LOCAL_LCSC_FOLDER_PATH = "/home/root/carpark/LTA";
    const std::string LOCAL_LCSC_SETTLEMENT_FOLDER_PATH = "/home/root/carpark/LTA_SETTLEMENT";

    static const int NORMAL_CMD_WRITE_TIMEOUT = 10;
    static const int CHUNK_CMD_WRITE_TIMEOUT  = 60;

    enum class mCSCEvents : int
    {
        sGetStatusOK        = 0,
        sLoginSuccess       = 1,
        sLogoutSuccess      = 2,
        sGetIDSuccess       = 3,
        sGetBlcSuccess      = 4,
        sGetTimeSuccess     = 5,
        sGetDeductSuccess   = 6,
        sGetCardRecord      = 7,
        sCardFlushed        = 8,
        sSetTimeSuccess     = 9,
        sLogin1Success      = 10,
        sBLUploadSuccess    = 11,
        sCILUploadSuccess   = 12,
        sCFGUploadSuccess   = 13,
        sRSAUploadSuccess   = 14,
        sFWUploadSuccess    = 15,

        iWrongCommPort      = -1,
        sCorruptedCmd       = -2,
        sIncompleteCmd      = -3,
        sUnknown            = -4,
        rCorruptedCmd       = -5,
        sUnsupportedCmd     = -6,
        sUnsupportedMode    = -7,
        sLoginAuthFail      = -8,
        sNoCard             = -9,
        sCardError          = -10,
        sRFError            = -11,
        sMultiCard          = -12,
        sCardnotinlist      = -13,
        sCardAuthError      = -14,
        sLockedCard         = -15,
        sInadequatePurse    = -16,
        sCryptoError        = -17,
        sExpiredCard        = -18,
        sCardParaError      = -19,
        sChangedCard        = -20,
        sBlackCard          = -21,
        sRecordNotFlush     = -22,
        sNoLastTrans        = -23,
        sNoNeedFlush        = -24,
        sWrongSeed          = -25,
        iWrongAESKey        = -26,
        iFailWriteSettle    = -27,
        sLoginAlready       = -28,
        sNotLoadCfgFile     = -29,
        sIncorrectIndex     = -30,
        sBLUploadCorrupt    = -31,
        sCILUploadCorrupt   = -32,
        sCFGUploadCorrupt   = -33,
        sWrongCDfileSize    = -34,
        iCommPortError      = -35,

        sIni                = 100,
        sTimeout            = -100,
        sWrongCmd           = -101,
        sNoSeedForFlush     = -102,
        sSendcmdfail        = -103,
        rCRCError           = -104,
        rNotRespCmd         = -105
    };

    enum class LCSC_CMD
    {
        GET_STATUS_CMD,
        LOGIN_1,
        LOGIN_2,
        LOGOUT,
        GET_CARD_ID,
        CARD_BALANCE,
        CARD_DEDUCT,
        CARD_RECORD,
        CARD_FLUSH,
        GET_TIME,
        SET_TIME,
        UPLOAD_CFG_FILE,
        UPLOAD_CIL_FILE,
        UPLOAD_BL_FILE
    };

    enum class LCSC_CMD_CODE
    {
        COMMAND     = 0x00,
        RESPONSE    = 0x01
    };

    enum class LCSC_CMD_TYPE
    {
        // Authentication
        AUTH_LOGIN1     = 0x10,
        AUTH_LOGIN2     = 0x11,
        AUTH_LOGOUT     = 0x12,

        // Card Operations
        CARD_ID         = 0x20,
        CARD_BALANCE    = 0x21,
        CARD_DEDUCT     = 0x22,
        CARD_RECORD     = 0x23,
        CARD_FLUSH      = 0x24,

        // CD Upload
        BL_UPLOAD       = 0x30,
        CIL_UPLOAD      = 0x31,
        CFG_UPLOAD      = 0x32,
        RSA_UPLOAD      = 0x33,

        // Settings
        CLK_SET         = 0x41,
        CLK_GET         = 0x42,

        // Log Retrieval
        LOG_READ        = 0x50,

        // Firmware Update
        FW_UPDATE       = 0x60,

        // Status
        GET_STATUS      = 0x00
    };

    enum class RX_STATE
    {
        RX_START,
        RX_RECEIVING
    };

    enum class STATE
    {
        IDLE,
        SENDING_REQUEST_ASYNC,
        WAITING_FOR_RESPONSE,
        SENDING_CHUNK_COMMAND_REQUEST_ASYNC,
        WAITING_FOR_CHUNK_COMMAND_RESPONSE,
        STATE_COUNT
    };

    enum class EVENT
    {
        COMMAND_ENQUEUED,
        CHUNK_COMMAND_ENQUEUED,
        WRITE_COMPLETED,
        WRITE_FAILED,
        RESPONSE_TIMEOUT,
        RESPONSE_RECEIVED,
        SEND_NEXT_CHUNK_COMMAND,
        ALL_CHUNK_COMMAND_COMPLETED,
        CHUNK_COMMAND_ERROR,
        WRITE_TIMEOUT,
        EVENT_COUNT
    };

    enum class UPLOAD_LCSC_FILES_STATE
    {
        IDLE,
        DOWNLOAD_CDFILES,
        UPLOAD_CDFILES,
        GENERATE_CDACKFILES,
        MOVE_CDACKFILES,
        STATE_COUNT
    };

    enum class UPLOAD_LCSC_FILES_EVENT
    {
        CHECK_CONDITION,
        ALLOW_DOWNLOAD,
        CONTINUE_UPLOAD,
        CDFILES_DOWNLOADED,
        NO_CDFILES_DOWNLOADED,
        CDFILES_UPLOADING,
        GET_LCSC_DEVICE_STATUS,
        CDFILE_UPLOADED,
        CDFILE_UPLOAD_FAILED,
        GET_LCSC_DEVICE_STATUS_OK,
        GET_LCSC_DEVICE_STATUS_FAILED,
        EVENT_COUNT
    };

    struct CommandWithData
    {
        LCSC_CMD cmd;
        std::shared_ptr<void> data;

        CommandWithData(LCSC_CMD c, std::shared_ptr<void> d = nullptr) : cmd(c), data(d) {}
    };

    struct EventTransition
    {
        EVENT event;
        void (LCSCReader::*eventHandler)(EVENT);
        STATE nextState;
    };

    struct StateTransition
    {
        STATE stateName;
        std::vector<EventTransition> transitions;
    };

    struct UploadLcscEventTransition
    {
        UPLOAD_LCSC_FILES_EVENT event;
        void (LCSCReader::*lcscEventHandler)(UPLOAD_LCSC_FILES_EVENT, const std::string&);
        UPLOAD_LCSC_FILES_STATE nextState;
    };

    struct UploadLcscStateTransition
    {
        UPLOAD_LCSC_FILES_STATE stateName;
        std::vector<UploadLcscEventTransition> transitions;
    };

    static LCSCReader* getInstance();
    int FnLCSCReaderInit(unsigned int baudRate, const std::string& comPortName);
    void FnLCSCReaderClose();

    void FnLCSCReaderStopRead();
    void FnSendGetStatusCmd();
    void FnSendGetLoginCmd();
    void FnSendGetLogoutCmd();
    void FnSendGetCardIDCmd();
    void FnSendGetCardBalance();
    void FnSendCardDeduct(uint32_t amount);
    void FnSendCardRecord();
    void FnSendCardFlush(uint32_t seed);
    void FnSendGetTime();
    void FnSendSetTime();
    int FnSendUploadCFGFile(const std::string& path);
    int FnSendUploadCILFile(const std::string& path);
    int FnSendUploadBLFile(const std::string& path);

    void FnUploadLCSCCDFiles();

    std::string getCommandString(LCSC_CMD cmd);
    std::string getCommandTypeString(uint8_t type);

    /**
     * Singleton LCSCReader should not be cloneable.
     */
    LCSCReader(LCSCReader& lcscReader) = delete;

    /**
     * Singleton LCSCReader should not be assignable.
     */
    void operator=(const LCSCReader&) = delete;

private:
    static LCSCReader* lcscReader_;
    static std::mutex mutex_;
    boost::asio::io_context ioContext_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    std::unique_ptr<boost::asio::serial_port> pSerialPort_;
    boost::asio::deadline_timer rspTimer_;
    boost::asio::deadline_timer serialWriteDelayTimer_;
    boost::asio::deadline_timer serialWriteTimer_;
    std::string logFileName_;
    std::thread ioContextThread_;
    std::mutex commandQueueMutex_;
    std::deque<CommandWithData> commandQueue_;
    std::mutex chunkCommandQueueMutex_;
    std::deque<CommandWithData> chunkCommandQueue_;
    static std::mutex currentCmdMutex_;
    LCSC_CMD currentCmd;
    static const StateTransition stateTransitionTable[static_cast<int>(STATE::STATE_COUNT)];
    STATE currentState_;
    std::chrono::steady_clock::time_point lastSerialReadTime_;
    std::array<uint8_t, 1024> readBuffer_;
    std::queue<std::vector<uint8_t>> writeQueue_;
    bool write_in_progress_;
    std::array<uint8_t, 1024> rxBuffer_;
    int rxNum_;
    RX_STATE rxState_;
    std::vector<uint8_t> aes_key;
    std::atomic<bool> continueReadFlag_;
    static const UploadLcscStateTransition UploadLcscStateTransitionTable[static_cast<int>(UPLOAD_LCSC_FILES_STATE::STATE_COUNT)];
    UPLOAD_LCSC_FILES_STATE currentUploadLcscFilesState_;
    bool HasCDFileToUpload_;
    int LastCDUploadDate_;
    int LastCDUploadTime_;
    std::string uploadLcscFileName_;
    std::string lastDebitTime_;
    LCSCReader();
    void startIoContextThread();
    void enqueueCommand(LCSC_CMD cmd, std::shared_ptr<void> data = nullptr);
    void enqueueCommandToFront(LCSC_CMD cmd, std::shared_ptr<void> data = nullptr);
    void enqueueChunkCommand(LCSC_CMD cmd, std::shared_ptr<void> data = nullptr);
    void checkCommandQueue();
    std::string eventToString(EVENT event);
    std::string stateToString(STATE state);
    std::string getEventStringFromResponseCmdType(uint8_t respType);
    void processEvent(EVENT event);
    void handleIdleState(EVENT event);
    void handleSendingRequestAsyncState(EVENT event);
    void handleWaitingForResponseState(EVENT event);
    void handleSendingChunkCommandRequestAsyncState(EVENT event);
    void handleWaitingForChunkCommandResponseState(EVENT event);
    void startSerialWriteTimer(int seconds);
    void startResponseTimer();
    void handleCmdResponseTimeout(const boost::system::error_code& error);
    void handleSerialWriteTimeout(const boost::system::error_code& error);
    bool isRxResponseComplete(const std::vector<uint8_t>& dataBuff);
    void handleReceivedCmd(const std::vector<uint8_t>& msgDataBuff);
    void popFromCommandQueueAndEnqueueWrite();
    void popFromChunkCommandQueueAndEnqueueWrite();
    bool isChunkedCommand(LCSC_CMD cmd);
    bool isChunkedCommandType(LCSC_CMD_TYPE cmd);
    void sendNextChunkCommandData();
    void clearChunkCommandQueue();
    std::vector<uint8_t> prepareCmd(LCSC_CMD cmd, std::shared_ptr<void> payloadData);
    uint16_t CRC16_CCITT(const uint8_t* inStr, size_t length);
    std::string handleCmdResponse(const CscPacket& msg);
    void encryptAES256(const std::vector<uint8_t>& key, const std::vector<uint8_t>& challenge, std::vector<uint8_t>& encryptedChallenge);
    std::vector<uint8_t> readFile(const std::filesystem::path& filePath);
    std::vector<std::vector<uint8_t>> chunkData(const std::vector<uint8_t>& data, std::size_t chunkSize);

    void handleUploadLcscIdleState(UPLOAD_LCSC_FILES_EVENT event, const std::string& str = "");
    void handleDownloadCDFilesState(UPLOAD_LCSC_FILES_EVENT event, const std::string& str = "");
    void handleUploadCDFilesState(UPLOAD_LCSC_FILES_EVENT event, const std::string& str = "");
    void handleGenerateCDAckFilesState(UPLOAD_LCSC_FILES_EVENT event, const std::string& str = "");
    std::string uploadLcscFilesEventToString(UPLOAD_LCSC_FILES_EVENT event);
    std::string uploadLcscFilesStateToString(UPLOAD_LCSC_FILES_STATE state);
    void processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT event, const std::string& str = "");
    std::string calculateSHA256(const std::string& data);
    bool FnGenerateCDAckFile(const std::string& serialNum, const std::string& fwVer, const std::string& bl1Ver,
                            const std::string& bl2Ver, const std::string& bl3Ver, const std::string& cil1Ver,
                            const std::string& cil2Ver, const std::string& cil3Ver, const std::string& cfgVer);
    bool FnMoveCDAckFile();
    bool FnDownloadCDFiles();
    void FnUploadCDFile2(std::string path);
    void handleUploadLcscFilesCmdResponse(const CscPacket& msg, const std::string& msgRsp);
    void handleUploadLcscGetStatusCmdResponse(const CscPacket& msg, const std::string& msgRsp);

    void handleCmdErrorOrTimeout(LCSC_CMD cmd, mCSCEvents eventStatus);
    void setCurrentCmd(LCSC_CMD cmd);
    LCSC_CMD getCurrentCmd();
    void processTrans(const std::vector<uint8_t>& payload);
    void writeLCSCTrans(const std::string& data);
    bool isCurrentCmdResponse(LCSC_CMD currCmd, uint8_t respType);

    // Serial read and write
    void resetRxBuffer();
    std::vector<uint8_t> getRxBuffer() const;
    void startRead();
    void readEnd(const boost::system::error_code& error, std::size_t bytesTransferred);
    void enqueueWrite(const std::vector<uint8_t>& data);
    void startWrite();
    void writeEnd(const boost::system::error_code& error, std::size_t bytesTransferred);
};
