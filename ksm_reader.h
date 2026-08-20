#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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

class KSM_Reader
{
public:
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
        KSMReaderRecv_NAK         = 0,
        KSMReaderRecv_ACK         = 1,
        KSMReaderRecv_CRCErr      = 2
    };

    static KSM_Reader* getInstance();

    int FnKSMReaderInit(unsigned int baudRate, const std::string& comPortName);
    void FnKSMReaderClose();

    void FnKSMReaderEnable(bool enable);
    void FnKSMReaderReadCardInfo();
    void FnKSMReaderSendInit();
    void FnKSMReaderSendGetStatus();
    void FnKSMReaderStartGetStatus();
    void FnKSMReaderSendEjectToFront();

    std::string FnKSMReaderGetCardNum() const;
    bool FnKSMReaderGetCardExpired() const;
    int FnKSMReaderGetCardExpiryDate() const;
    long FnKSMReaderGetCardBalance() const;

private:

    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    struct CardData
    {
        std::string cardNum;
        int expiryYearMonth{0};
        bool expired{false};
        long balance{0};
    };

    struct ResponseReadResult
    {
        KSMReaderCmdRetCode result{KSMReaderCmdRetCode::KSMReaderRecv_NoResp};
        std::vector<char> frame;
    };

    struct TimeoutState
    {
        bool completed{false};
        bool timedOut{false};
    };

    static constexpr std::uint8_t STX = 0x02;
    static constexpr std::uint8_t ETX = 0x03;
    static constexpr std::uint8_t ACK = 0x06;
    static constexpr std::uint8_t NAK = 0x15;
    static constexpr std::uint8_t ENQ = 0x05;

    static constexpr std::size_t RX_FRAME_MAX_SIZE = 128;
    static constexpr auto MIN_WRITE_GAP = std::chrono::milliseconds(100);
    static constexpr auto WRITE_TIMEOUT = std::chrono::seconds(5);
    static constexpr auto ACK_TIMEOUT = std::chrono::seconds(1);
    static constexpr auto RESPONSE_TIMEOUT = std::chrono::seconds(2);

    KSM_Reader();
    ~KSM_Reader();

    KSM_Reader(const KSM_Reader&) = delete;
    KSM_Reader& operator=(const KSM_Reader&) = delete;
    KSM_Reader(KSM_Reader&&) = delete;
    KSM_Reader& operator=(KSM_Reader&&) = delete;

    void runIoContext();
    void shutdownFromDestructor();
    void requestStopOnIoThread();

    void postCommand(KSMReaderCmdID cmd);
    void enqueueCommandOnIoThread(KSMReaderCmdID cmd);
    void removeQueuedGetStatusCommands();
    bool hasQueuedCommand(KSMReaderCmdID cmd) const;

    boost::asio::awaitable<void> commandLoop();
    boost::asio::awaitable<KSMReaderCmdRetCode> executeCommand(KSMReaderCmdID cmd);
    boost::asio::awaitable<bool> writeBytes(const std::vector<std::uint8_t>& data, std::chrono::steady_clock::duration timeout, const char* description);
    boost::asio::awaitable<KSMReaderCmdRetCode> waitForAck(std::chrono::steady_clock::duration timeout);
    boost::asio::awaitable<ResponseReadResult> readResponseFrame(std::chrono::steady_clock::duration timeout);
    boost::asio::awaitable<void> waitForWriteGap();

    std::vector<std::uint8_t> buildCommandPayload(KSMReaderCmdID cmd) const;
    std::vector<std::uint8_t> buildCommandFrame(KSMReaderCmdID cmd) const;

    KSMReaderCmdRetCode handleCommandResponse(KSMReaderCmdID cmd, const std::vector<char>& dataBuff);
    void handleCommandFailure(KSMReaderCmdID cmd, KSMReaderCmdRetCode retCode);

    void updateCardInfo(const std::string& cardNum, int expiryYearMonth, bool expired);
    void updateCardBalance(long balance);

    static std::string KSMReaderCmdIDToString(KSMReaderCmdID cmdID);
    static const char* retCodeToString(KSMReaderCmdRetCode retCode);

    void ksmLogger(const std::string& logMsg, bool force = false);

    boost::asio::io_context ioContext_;
    std::optional<WorkGuard> workGuard_;
    std::thread ioThread_;
    std::unique_ptr<boost::asio::serial_port> serialPort_;
    boost::asio::steady_timer commandWakeTimer_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};

    // The following fields are owned by the single ioContext_ thread.
    bool stopRequested_{false};
    std::deque<KSMReaderCmdID> commandQueue_;
    KSMReaderCmdID currentCmd_{KSMReaderCmdID::INIT_CMD};
    bool cardPresented_{false};
    bool continueReadCard_{false};
    bool blockGetStatusCmdLog_{false};
    std::chrono::steady_clock::time_point lastSerialReadTime_;

    // Card result getters can be called by other module threads.
    mutable std::mutex cardDataMutex_;
    CardData cardData_;

    const std::string logFileName_{"ksmReader"};
};