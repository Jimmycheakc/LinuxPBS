#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/serial_port.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/thread_pool.hpp>

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
    uint16_t getCrc() const;
    std::vector<uint8_t> serializeWithoutCRC() const;
    std::vector<uint8_t> serialize() const;
    void deserialize(const std::vector<uint8_t>& data);
    std::string getMsgCscPacketOutput() const;

    void clear();

private:
    uint8_t attn{0};
    bool code{false};
    uint8_t type{0};
    uint16_t len{0};
    std::vector<uint8_t> payload;
    uint16_t crc{0};
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
        RESPONSE_HANDLED,
        RESPONSE_REJECTED,
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
        CDFILE_CLEANUP_COMPLETED,
        CDFILE_CLEANUP_FAILED,
        GET_LCSC_DEVICE_STATUS_OK,
        GET_LCSC_DEVICE_STATUS_FAILED,
        CDACK_FINALIZE_COMPLETED,
        CDACK_FINALIZE_FAILED,
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
    int FnLCSCReaderInit(
            unsigned int baudRate,
            const std::string& comPortName,
            int commPortLCSC,
            int stationId,
            const std::string& cpoId,
            const std::string& carparkId,
            int eps,
            const std::string& cscrCdackFolder,
            const std::string& cscrCdfFolder);
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

    LCSCReader(const LCSCReader&) = delete;
    LCSCReader& operator=(const LCSCReader&) = delete;
    LCSCReader(LCSCReader&&) = delete;
    LCSCReader& operator=(LCSCReader&&) = delete;

    // Cross-thread status snapshot kept public for backward compatibility.
    std::atomic<int> LCSCCard_In{0};

private:
    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    // Active module: exactly one dedicated thread calls ioContext_.run().
    // Therefore all async protocol state is confined to that one thread and
    // no strand is required.
    boost::asio::io_context ioContext_;
    std::optional<WorkGuard> workGuard_;

    // Blocking filesystem / mount / ping work is isolated from the single
    // LCSC I/O thread. This is recreated for every module start.
    std::unique_ptr<boost::asio::thread_pool> filePool_;

    // filePool_ has multiple workers. Settlement records for the same hourly
    // file must still be serialized so header/create/append cannot race.
    std::mutex settlementFileMutex_;

    std::unique_ptr<boost::asio::serial_port> pSerialPort_;
    boost::asio::steady_timer rspTimer_;
    boost::asio::steady_timer serialWriteDelayTimer_;
    boost::asio::steady_timer serialWriteTimer_;

    std::string logFileName_;
    std::thread ioContextThread_;
    mutable std::mutex lifecycleMutex_;

    std::atomic<bool> moduleRunning_{false};
    std::atomic<bool> acceptingWork_{false};
    std::atomic<bool> stopping_{false};

    int commPortLCSC_{0};
    int stationId_{0};
    std::string cpoId_;
    std::string carparkId_;
    int eps_{0};
    std::string cscrCdackFolder_;
    std::string cscrCdfFolder_;

    // I/O-thread-owned command/FSM state.
    std::deque<CommandWithData> commandQueue_;
    std::deque<CommandWithData> chunkCommandQueue_;
    LCSC_CMD currentCmd_{LCSC_CMD::GET_STATUS_CMD};
    static const StateTransition stateTransitionTable[static_cast<int>(STATE::STATE_COUNT)];
    STATE currentState_{STATE::IDLE};

    std::chrono::steady_clock::time_point lastSerialReadTime_{std::chrono::steady_clock::now()};
    std::array<uint8_t, 1024> readBuffer_{};
    std::queue<std::vector<uint8_t>> writeQueue_;
    bool writeInProgress_{false};
    bool writeTimedOut_{false};

    std::array<uint8_t, 1024> rxBuffer_{};
    std::size_t rxNum_{0};
    RX_STATE rxState_{RX_STATE::RX_START};

    std::vector<uint8_t> aes_key;
    std::atomic<bool> continueReadFlag_{false};

    static const UploadLcscStateTransition UploadLcscStateTransitionTable[static_cast<int>(UPLOAD_LCSC_FILES_STATE::STATE_COUNT)];
    UPLOAD_LCSC_FILES_STATE currentUploadLcscFilesState_{UPLOAD_LCSC_FILES_STATE::IDLE};

    // These fields belong to the upload workflow and are only touched from
    // the LCSC I/O thread via processUploadLcscFilesEvent().
    bool HasCDFileToUpload_{false};
    int LastCDUploadDate_{0};
    int LastCDUploadTime_{0};
    std::string uploadLcscFileName_;
    std::string lastDebitTime_;

    LCSCReader();
    ~LCSCReader();

    bool startIoContextThread();
    void shutdownOnIoThread();
    void resetRuntimeState();
    void enqueueCommand(LCSC_CMD cmd, std::shared_ptr<void> data = nullptr);
    void enqueueCommandToFront(LCSC_CMD cmd, std::shared_ptr<void> data = nullptr);
    void enqueueChunkCommand(LCSC_CMD cmd, std::shared_ptr<void> data = nullptr);
    void enqueueCommandOnIoThread(LCSC_CMD cmd, std::shared_ptr<void> data);
    void enqueueCommandToFrontOnIoThread(LCSC_CMD cmd, std::shared_ptr<void> data);
    void enqueueChunkCommandOnIoThread(LCSC_CMD cmd, std::shared_ptr<void> data);
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
    uint16_t CRC16_CCITT(const uint8_t* inStr, std::size_t length);
    std::string handleCmdResponse(const CscPacket& msg);
    void encryptAES256(const std::vector<uint8_t>& key, const std::vector<uint8_t>& challenge, std::vector<uint8_t>& encryptedChallenge);
    std::vector<uint8_t> readFile(const std::filesystem::path& filePath);
    std::vector<std::vector<uint8_t>> chunkData(const std::vector<uint8_t>& data, std::size_t chunkSize);

    void handleUploadLcscIdleState(UPLOAD_LCSC_FILES_EVENT event, const std::string& str = "");
    void handleDownloadCDFilesState(UPLOAD_LCSC_FILES_EVENT event, const std::string& str = "");
    void handleUploadCDFilesState(UPLOAD_LCSC_FILES_EVENT event, const std::string& str = "");
    void handleGenerateCDAckFilesState(UPLOAD_LCSC_FILES_EVENT event, const std::string& str = "");
    void handleMoveCDAckFilesState(UPLOAD_LCSC_FILES_EVENT event, const std::string& str = "");
    void startDownloadCdFilesJob();
    void startScanDownloadedCdFilesJob();
    void startUploadCdFileJob(std::string path);
    void startCleanupCdFileJob(std::string path);
    void startFinalizeCdAckFilesJob(const std::string& serialNum,
                                    const std::string& fwVer,
                                    const std::string& bl1Ver,
                                    const std::string& bl2Ver,
                                    const std::string& bl3Ver,
                                    const std::string& cil1Ver,
                                    const std::string& cil2Ver,
                                    const std::string& cil3Ver,
                                    const std::string& cfgVer);
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
    LCSC_CMD getCurrentCmd() const;
    void processTrans(const std::vector<uint8_t>& payload);
    void writeLCSCTrans(const std::string& data);
    void writeLCSCTransBlocking(std::string settleFile, std::string header, std::string detail);
    bool isCurrentCmdResponse(LCSC_CMD currCmd, uint8_t respType);

    // Serial read and write
    void resetRxBuffer();
    std::vector<uint8_t> getRxBuffer() const;
    void startRead();
    void readEnd(const boost::system::error_code& error, std::size_t bytesTransferred);
    void enqueueWrite(const std::vector<uint8_t>& data);
    void startWrite();
    void writeEnd(const boost::system::error_code& error, std::size_t bytesTransferred);
    std::string toHexString(const std::vector<uint8_t>& data) const;
};
