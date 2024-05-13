#pragma once

#include <atomic>
#include <iostream>
#include <filesystem>
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include "boost/asio.hpp"
#include "boost/asio/serial_port.hpp"

class LCSCReader
{

public:
    const std::string LOCAL_LCSC_FOLDER_PATH = "/home/root/carpark/LTA";

    struct CscPacket
    {
        uint8_t attn;
        bool code;
        uint8_t type;
        uint16_t len;
        std::vector<uint8_t> payload;
        uint16_t crc;
    };

    struct ReadResult
    {
        std::vector<char> data;
        bool success;
    };

    enum class RX_STATE
    {
        RX_START,
        RX_RECEIVING
    };

    enum class mCSCEvents
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

    enum class LcscCmd
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

    static const int TX_BUF_SIZE = 1024;
    static const int RX_BUF_SIZE = 1024;

    static LCSCReader* getInstance();
    int FnLCSCReaderInit(boost::asio::io_context& mainIOContext, unsigned int baudRate, const std::string& comPortName);
    void FnLCSCReaderStopRead();
    void FnSendGetStatusCmd();
    int FnSendGetLoginCmd();
    void FnSendGetLogoutCmd();
    void FnSendGetCardIDCmd();
    void FnSendGetCardBalance();
    void FnSendGetTime();
    void FnSendSetTime();
    int FnSendUploadCFGFile(const std::string& path);
    int FnSendUploadCILFile(const std::string& path);
    int FnSendUploadBLFile(const std::string& path);
    bool FnGetIsCmdExecuting() const;

    std::string FnGetSerialNumber();
    int FnGetReaderMode();
    std::string FnGetBL1Version();
    std::string FnGetBL2Version();
    std::string FnGetBL3Version();
    std::string FnGetBL4Version();
    std::string FnGetCIL1Version();
    std::string FnGetCIL2Version();
    std::string FnGetCIL3Version();
    std::string FnGetCIL4Version();
    std::string FnGetCFGVersion();
    std::string FnGetFirmwareVersion();
    std::string FnGetCardSerialNumber();
    std::string FnGetCardApplicationNumber();
    double FnGetCardBalance();
    std::string FnGetReaderTime();

    bool FnMoveCDAckFile();
    bool FnGenerateCDAckFile();
    bool FnDownloadCDFiles();
    void FnUploadLCSCCDFiles();
    bool FnUploadCDFile2(std::string path);

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
    boost::asio::io_context* pMainIOContext_;
    std::unique_ptr<boost::asio::deadline_timer> uploadCDFilesTimer_;
    boost::asio::io_context io_serial_context;
    std::unique_ptr<boost::asio::io_context::strand> pStrand_;
    std::unique_ptr<boost::asio::serial_port> pSerialPort_;
    std::atomic<bool> continueReadFlag_;
    std::atomic<bool> isCmdExecuting_;
    std::string logFileName_;
    int lcscCmdTimeoutInMillisec_;
    int lcscCmdMaxRetry_;
    int TxNum_;
    int RxNum_;
    RX_STATE rxState_;
    unsigned char txBuff[TX_BUF_SIZE];
    unsigned char rxBuff[RX_BUF_SIZE];
    std::string serial_num_;
    int reader_mode_;
    std::string bl1_version_;
    std::string bl2_version_;
    std::string bl3_version_;
    std::string bl4_version_;
    std::string cil1_version_;
    std::string cil2_version_;
    std::string cil3_version_;
    std::string cil4_version_;
    std::string cfg_version_;
    std::string firmware_version_;
    std::vector<uint8_t> reader_challenge_;
    std::vector<uint8_t> aes_key;
    std::string card_serial_num_;
    std::string card_application_num_;
    double card_balance_;
    std::string reader_time_;
    bool HasCDFileToUpload_;
    int LastCDUploadDate_;
    int LastCDUploadTime_;
    int CDUploadTimeOut_;
    std::string CDFPath_;
    LCSCReader();
    std::string lcscCmdToString(LcscCmd cmd);
    int lcscCmd(LcscCmd cmd);
    bool lcscCmdSend(const std::vector<unsigned char>& dataBuff);
    bool parseCscPacketData(LCSCReader::CscPacket& packet, const std::vector<char>& data);
    std::vector<unsigned char> loadGetStatus();
    std::vector<unsigned char> loadLogin1();
    std::vector<unsigned char> loadLogin2();
    std::vector<unsigned char> loadLogout();
    std::vector<unsigned char> loadGetCardID();
    void handleGetCardIDTimerExpiration();
    std::vector<unsigned char> loadGetCardBalance();
    void handleGetCardBalanceTimerExpiration();
    std::vector<unsigned char> loadGetTime();
    std::vector<unsigned char> loadSetTime();
    int uploadCFGSub(const std::vector<unsigned char>& data);
    std::vector<unsigned char> readFile(const std::filesystem::path& filePath);
    std::vector<std::vector<unsigned char>> chunkData(const std::vector<unsigned char>& data, std::size_t chunkSize);
    int uploadCFGComplete();
    int uploadCILSub(const std::vector<unsigned char>& data, bool isSubSequence);
    int uploadCILComplete();
    int uploadBLSub(const std::vector<unsigned char>& data, bool isSubSequence);
    int uploadBLComplete();
    ReadResult lcscReadWithTimeout(int milliseconds);
    void resetRxBuffer();
    bool responseIsComplete(const std::vector<char>& buffer, std::size_t bytesTransferred);
    mCSCEvents lcscHandleCmdResponse(LcscCmd cmd, const std::vector<char>& dataBuff);
    uint16_t CRC16_CCITT(const unsigned char* inStr, size_t length);
    unsigned char* getTxBuf();
    unsigned char* getRxBuf();
    int getTxNum();
    int getRxNum();
    void encryptAES256(const std::vector<uint8_t>& key, const std::vector<uint8_t>& challenge, std::vector<uint8_t>& encryptedChallenge);
    std::string calculateSHA256(const std::string& data);
    void startGetCardIDTimer(int milliseconds);
    void startGetCardBalanceTimer(int milliseconds);
};