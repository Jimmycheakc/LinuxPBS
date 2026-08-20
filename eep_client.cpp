#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>

#if defined(__linux__)
#include <pthread.h>
#endif

#include <boost/asio/post.hpp>
#include <boost/filesystem.hpp>

#include "common.h"
#include "eep_client.h"
#include "event_manager.h"
#include "ini_parser.h"
#include "log.h"
#include "mount.h"
#include "operation.h"
#include "thread_pool_helper.h"


namespace
{
std::string bytesToHexString(const std::vector<std::uint8_t>& data)
{
    std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setfill('0');

    for (const std::uint8_t byte : data)
    {
        oss << std::setw(2) << static_cast<unsigned int>(byte);
    }

    return oss.str();
}
}

EEPClient::EEPClient()
    : reconnectTimer_(ioContext_),
      connectTimer_(ioContext_),
      sendTimer_(ioContext_),
      responseTimer_(ioContext_),
      ackTimer_(ioContext_),
      watchdogTimer_(ioContext_),
      healthStatusTimer_(ioContext_),
      logFileName_("eep")
{
}

EEPClient::~EEPClient()
{
    // Normal shutdown should happen through FnEEPClientClose(). This is only
    // an emergency fallback for process/static destruction.
    acceptingWork_.store(false);
    stopping_.store(true);
    workGuard_.reset();
    ioContext_.stop();

    if (ioContextThread_.joinable() &&
        ioContextThread_.get_id() != std::this_thread::get_id())
    {
        ioContextThread_.join();
    }

    if (filePool_)
    {
        filePool_->join();
        filePool_.reset();
    }

    client_.reset();
}

EEPClient* EEPClient::getInstance()
{
    static EEPClient instance;
    return &instance;
}

void EEPClient::FnEEPClientInit(const std::string& serverIP, unsigned short serverPort, const std::string& stationID)
{
std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);

    Logger::getInstance()->FnCreateLogFile(logFileName_);

    if (moduleRunning_.load() || ioContextThread_.joinable())
    {
        Logger::getInstance()->FnLog(
            "EEP: [INIT] Ignored | Reason=Already running",
            logFileName_,
            "EEP");
        return;
    }

    int stationId = 0;
    try
    {
        stationId = std::stoi(stationID);
    }
    catch (const std::exception& e)
    {
        Logger::getInstance()->FnLog(
            std::string("EEP: [INIT] Failed | InvalidStationID=") +
                stationID + " | Error=" + e.what(),
            logFileName_,
            "EEP");
        return;
    }

    resetRuntimeState();

    ioContext_.restart();
    workGuard_.emplace(ioContext_.get_executor());
    filePool_ = ThreadPoolHelper::create(2, "EEP_FILE");

    iStationID_ = stationId;
    serverIP_ = serverIP;
    serverPort_ = serverPort;
    eepSourceId_ = 96 + iStationID_;
    eepDestinationId_ = 32 + iStationID_;

    try
    {
        client_ = std::make_unique<AppTcpClient>(
            ioContext_,
            serverIP_,
            serverPort_);
    }
    catch (const std::exception& e)
    {
        workGuard_.reset();
        filePool_->join();
        filePool_.reset();

        Logger::getInstance()->FnLog(
            std::string("EEP: [INIT] Failed | TCP client creation | Error=") +
                e.what(),
            logFileName_,
            "EEP");
        return;
    }

    client_->setConnectHandler(
        [this](bool success, const std::string& message)
        {
            // AppTcpClient is bound to this same io_context. Deferring the
            // callback avoids state-machine re-entrancy while preserving the
            // single-thread ownership model.
            boost::asio::post(
                ioContext_,
                [this, success, message]()
                {
                    if (!stopping_.load())
                    {
                        handleConnect(success, message);
                    }
                });
        });

    client_->setCloseHandler(
        [this](bool success, const std::string& message)
        {
            boost::asio::post(
                ioContext_,
                [this, success, message]()
                {
                    handleClose(success, message);
                });
        });

    client_->setReceiveHandler(
        [this](bool success, const std::vector<std::uint8_t>& data)
        {
            boost::asio::post(
                ioContext_,
                [this, success, data]()
                {
                    if (!stopping_.load())
                    {
                        handleReceivedData(success, data);
                    }
                });
        });

    client_->setSendHandler(
        [this](bool success, const std::string& message)
        {
            boost::asio::post(
                ioContext_,
                [this, success, message]()
                {
                    if (!stopping_.load())
                    {
                        handleSend(success, message);
                    }
                });
        });

    stopping_.store(false);
    acceptingWork_.store(true);

    if (!startIoContextThread())
    {
        acceptingWork_.store(false);
        stopping_.store(true);
        workGuard_.reset();
        client_.reset();

        if (filePool_)
        {
            filePool_->join();
            filePool_.reset();
        }

        Logger::getInstance()->FnLog(
            "EEP: [INIT] Failed | Unable to start io_context thread",
            logFileName_,
            "EEP");
        return;
    }

    Logger::getInstance()->FnLog(
        "EEP: [INIT] Started | Server=" + serverIP_ +
            " | Port=" + std::to_string(serverPort_) +
            " | Station=" + std::to_string(iStationID_),
        logFileName_,
        "EEP");

    processEvent(EVENT::CONNECT);
}

void EEPClient::FnEEPClientClose()
{
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);

    if (ioContext_.get_executor().running_in_this_thread())
    {
        Logger::getInstance()->FnLog(
            "EEP: [SHUTDOWN] Rejected | Called from EEP I/O thread",
            logFileName_,
            "EEP");
        return;
    }

    acceptingWork_.store(false);

    if (!ioContextThread_.joinable())
    {
        if (filePool_)
        {
            filePool_->join();
            filePool_.reset();
        }
        client_.reset();
        return;
    }

    Logger::getInstance()->FnLog(
        "EEP: [SHUTDOWN] Begin",
        logFileName_,
        "EEP");

    stopping_.store(true);

    auto shutdownPromise = std::make_shared<std::promise<void>>();
    auto shutdownFuture = shutdownPromise->get_future();

    boost::asio::post(
        ioContext_,
        [this, shutdownPromise]()
        {
            shutdownOnIoThread();
            shutdownPromise->set_value();
        });

    shutdownFuture.wait();

    // No new work will be accepted. Let cancellation/close handlers already
    // queued in the context drain naturally.
    workGuard_.reset();

    if (ioContextThread_.joinable())
    {
        ioContextThread_.join();
    }

    // No more I/O-thread code can post settlement jobs after this point.
    if (filePool_)
    {
        filePool_->join();
        filePool_.reset();
    }

    client_.reset();
    resetRuntimeState();

    moduleRunning_.store(false);
    stopping_.store(false);

    Logger::getInstance()->FnLog(
        "EEP: [SHUTDOWN] Completed",
        logFileName_,
        "EEP");
}

void EEPClient::FnSendAck(uint16_t seqNo_, uint8_t reqDataTypeCode_)
{
    // Data Format in little-Endian
    auto reqData = std::make_shared<AckData>();

    reqData->seqNo = seqNo_;
    reqData->reqDatatypeCode = reqDataTypeCode_;
    std::fill(std::begin(reqData->rsv), std::end(reqData->rsv), 0x00);

    enqueueCommand(CommandType::ACK, static_cast<int>(PRIORITY::UNSOLICITED), reqData);
}

void EEPClient::FnSendNak(uint16_t seqNo_, uint8_t reqDataTypeCode_, uint8_t reasonCode_)
{
    // Data Format in little-Endian
    auto reqData = std::make_shared<NakData>();

    reqData->seqNo = seqNo_;
    reqData->reqDatatypeCode = reqDataTypeCode_;
    reqData->reasonCode = reasonCode_;
    std::fill(std::begin(reqData->rsv), std::end(reqData->rsv), 0x00);

    enqueueCommand(CommandType::NAK, static_cast<int>(PRIORITY::UNSOLICITED), reqData);
}

void EEPClient::FnSendHealthStatusReq()
{
    enqueueCommand(CommandType::HEALTH_STATUS_REQ_CMD, static_cast<int>(PRIORITY::HEALTHSTATUS), nullptr);
}

void EEPClient::FnSendWatchdogReq()
{
    enqueueCommand(CommandType::WATCHDOG_REQ_CMD, static_cast<int>(PRIORITY::WATCHDOG), nullptr);
}

void EEPClient::FnSendStartReq()
{
    enqueueCommand(CommandType::START_REQ_CMD, static_cast<int>(PRIORITY::START), nullptr);
}

void EEPClient::FnSendStopReq()
{
    enqueueCommand(CommandType::STOP_REQ_CMD, static_cast<int>(PRIORITY::NORMAL), nullptr);
}

void EEPClient::FnSendDIReq()
{
    enqueueCommand(CommandType::DI_REQ_CMD, static_cast<int>(PRIORITY::NORMAL), nullptr);
}

void EEPClient::FnSendDOReq(uint8_t do1, uint8_t do2, uint8_t do3, uint8_t do4, uint8_t do5, uint8_t do6)
{
    // Data Format in little-Endian
    auto reqData = std::make_shared<SetDOPort>();

    reqData->do1 = do1;
    reqData->do2 = do2;
    reqData->do3 = do3;
    reqData->do4 = do4;
    reqData->do5 = do5;
    reqData->do6 = do6;
    std::fill(std::begin(reqData->rsv), std::end(reqData->rsv), 0x00);

    enqueueCommand(CommandType::DO_REQ_CMD, static_cast<int>(PRIORITY::NORMAL), reqData);
}

void EEPClient::FnSendSetDIPortConfigReq(uint16_t periodDebounceDI1_, uint16_t periodDebounceDI2_, uint16_t periodDebounceDI3_, uint16_t periodDebounceDI4_, uint16_t periodDebounceDI5_)
{
    // Data Format in little-Endian
    auto reqData = std::make_shared<SetDIPortConfigData>();

    reqData->periodDebounceDI1[0] = static_cast<uint8_t>(periodDebounceDI1_ & 0xFF);
    reqData->periodDebounceDI1[1] = static_cast<uint8_t>((periodDebounceDI1_ >> 8) & 0xFF);
    reqData->periodDebounceDI2[0] = static_cast<uint8_t>(periodDebounceDI2_ & 0xFF);
    reqData->periodDebounceDI2[1] = static_cast<uint8_t>((periodDebounceDI2_ >> 8) & 0xFF);
    reqData->periodDebounceDI3[0] = static_cast<uint8_t>(periodDebounceDI3_ & 0xFF);
    reqData->periodDebounceDI3[1] = static_cast<uint8_t>((periodDebounceDI3_ >> 8) & 0xFF);
    reqData->periodDebounceDI4[0] = static_cast<uint8_t>(periodDebounceDI4_ & 0xFF);
    reqData->periodDebounceDI4[1] = static_cast<uint8_t>((periodDebounceDI4_ >> 8) & 0xFF);
    reqData->periodDebounceDI5[0] = static_cast<uint8_t>(periodDebounceDI5_ & 0xFF);
    reqData->periodDebounceDI5[1] = static_cast<uint8_t>((periodDebounceDI5_ >> 8) & 0xFF);
    std::fill(std::begin(reqData->rsv), std::end(reqData->rsv), 0x00);

    enqueueCommand(CommandType::SET_DI_PORT_CONFIG_CMD, static_cast<int>(PRIORITY::NORMAL), reqData);
}

void EEPClient::FnSendGetOBUInfoReq()
{
    enqueueCommand(CommandType::GET_OBU_INFO_REQ_CMD, static_cast<int>(PRIORITY::NORMAL), nullptr);
}

void EEPClient::FnSendGetOBUInfoStopReq()
{
    enqueueCommand(CommandType::GET_OBU_INFO_STOP_REQ_CMD, static_cast<int>(PRIORITY::NORMAL), nullptr);
}

void EEPClient::FnSendDeductReq(const std::string& obuLabel_, const std::string& fee_, const std::string& entryTime_, const std::string& exitTime_)
{
    // Data Format in little-Endian
    auto reqData = std::make_shared<DeductData>();

    uint8_t obuLabelArr[5] = {0x12, 0x20, 0x02, 0x41, 0x93};
    bool obuParseSuccess = Common::getInstance()->FnConvertHexStringToByteArray(obuLabel_, obuLabelArr, 5);
    uint8_t feeArr[2];
    bool feeParseSuccess = Common::getInstance()->FnDecimalStringToTwoBytes(fee_, feeArr, true);
    std::tm parsedEntryDateTime = {};
    bool entryDateTimeParseSuccess = Common::getInstance()->FnParseDateTimeString(entryTime_, parsedEntryDateTime);
    std::tm parsedExitDateTime = {};
    bool exitDateTimeParseSuccess = Common::getInstance()->FnParseDateTimeString(exitTime_, parsedExitDateTime);

    if (obuParseSuccess && feeParseSuccess && entryDateTimeParseSuccess && exitDateTimeParseSuccess)
    {
        const std::uint16_t serialNum = allocateDeductCmdSerialNo();

        uint16_t entryYear = static_cast<uint16_t>(parsedEntryDateTime.tm_year + 1900);
        uint8_t entryMonth = static_cast<uint8_t>(parsedEntryDateTime.tm_mon + 1);
        uint8_t entryDay = static_cast<uint8_t>(parsedEntryDateTime.tm_mday);
        uint8_t entryHour = static_cast<uint8_t>(parsedEntryDateTime.tm_hour);
        uint8_t entryMinute = static_cast<uint8_t>(parsedEntryDateTime.tm_min);
        uint8_t entrySecond = static_cast<uint8_t>(parsedEntryDateTime.tm_sec);
        uint16_t exitYear = static_cast<uint16_t>(parsedExitDateTime.tm_year + 1900);
        uint8_t exitMonth = static_cast<uint8_t>(parsedExitDateTime.tm_mon + 1);
        uint8_t exitDay = static_cast<uint8_t>(parsedExitDateTime.tm_mday);
        uint8_t exitHour = static_cast<uint8_t>(parsedExitDateTime.tm_hour);
        uint8_t exitMinute = static_cast<uint8_t>(parsedExitDateTime.tm_min);
        uint8_t exitSecond = static_cast<uint8_t>(parsedExitDateTime.tm_sec);

        reqData->serialNum[0] = static_cast<uint8_t>(serialNum & 0xFF);
        reqData->serialNum[1] = static_cast<uint8_t>((serialNum >> 8) & 0xFF);
        std::fill(std::begin(reqData->rsv), std::end(reqData->rsv), 0x00);
        std::copy(obuLabelArr, obuLabelArr + 5, reqData->obuLabel);
        std::fill(std::begin(reqData->rsv1), std::end(reqData->rsv1), 0x00);
        std::copy(feeArr, feeArr + 2, reqData->chargeAmt);
        std::fill(std::begin(reqData->rsv2), std::end(reqData->rsv2), 0x00);
        reqData->parkingStartDay = entryDay;
        reqData->parkingStartMonth = entryMonth;
        reqData->parkingStartYear[0] = static_cast<uint8_t>(entryYear & 0xFF);
        reqData->parkingStartYear[1] = static_cast<uint8_t>((entryYear >> 8) & 0xFF);
        reqData->rsv3 = 0x00;
        reqData->parkingStartSecond = entrySecond;
        reqData->parkingStartMinute = entryMinute;
        reqData->parkingStartHour = entryHour;
        reqData->parkingEndDay = exitDay;
        reqData->parkingEndMonth = exitMonth;
        reqData->parkingEndYear[0] = static_cast<uint8_t>(exitYear & 0xFF);
        reqData->parkingEndYear[1] = static_cast<uint8_t>((exitYear >> 8) & 0xFF);
        reqData->rsv4 = 0x00;
        reqData->parkingEndSecond = exitSecond;
        reqData->parkingEndMinute = exitMinute;
        reqData->parkingEndHour = exitHour;
        enqueueCommand(CommandType::DEDUCT_REQ_CMD, static_cast<int>(PRIORITY::NORMAL), reqData);
    }
    else
    {
        std::ostringstream oss;
        oss << "EEP: [CMD] Rejected | Reason=Invalid parameter | ";
        oss << "ObuLabelValid=" << obuParseSuccess << " | ObuLabel=" << obuLabel_;
        oss << " | FeeValid=" << feeParseSuccess << " | Fee=" << fee_;
        oss << " | EntryTimeValid=" << entryDateTimeParseSuccess << " | EntryTime=" << entryTime_;
        oss << " | ExitTimeValid=" << exitDateTimeParseSuccess << " | ExitTime=" << exitTime_;
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

        postCommandFailure(CommandType::DEDUCT_REQ_CMD, MSG_STATUS::SEND_FAILED);
    }
}

void EEPClient::FnSendDeductStopReq(const std::string& obuLabel_, uint16_t serialNum_)
{
    // Data Format in little-Endian
    auto reqData = std::make_shared<DeductStopData>();

    uint8_t obuLabelArr[5];
    bool obuParseSuccess = Common::getInstance()->FnConvertHexStringToByteArray(obuLabel_, obuLabelArr, 5);

    if (obuParseSuccess)
    {
        reqData->serialNum[0] = static_cast<uint8_t>(serialNum_ & 0xFF);
        reqData->serialNum[1] = static_cast<uint8_t>((serialNum_ >> 8) & 0xFF);;
        std::fill(std::begin(reqData->rsv), std::end(reqData->rsv), 0x00);
        std::copy(obuLabelArr, obuLabelArr + 5, reqData->obuLabel);
        std::fill(std::begin(reqData->rsv1), std::end(reqData->rsv1), 0x00);
        enqueueCommand(CommandType::DEDUCT_STOP_REQ_CMD, static_cast<int>(PRIORITY::NORMAL), reqData);
    }
    else
    {
        std::ostringstream oss;
        oss << "EEP: [CMD] Rejected | Reason=Invalid parameter | ";
        oss << "ObuLabelValid=" << obuParseSuccess << " | ObuLabel=" << obuLabel_;
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

        postCommandFailure(CommandType::DEDUCT_STOP_REQ_CMD, MSG_STATUS::SEND_FAILED);
    }
}

void EEPClient::FnSendTransactionReq(const std::string& obuLabel_, uint16_t serialNum_)
{
    // Data Format in little-Endian
    auto reqData = std::make_shared<TransactionReqData>();

    uint8_t obuLabelArr[5];
    bool obuParseSuccess = Common::getInstance()->FnConvertHexStringToByteArray(obuLabel_, obuLabelArr, 5);

    if (obuParseSuccess)
    {
        reqData->serialNum[0] = static_cast<uint8_t>(serialNum_ & 0xFF);
        reqData->serialNum[1] = static_cast<uint8_t>((serialNum_ >> 8) & 0xFF);;
        std::fill(std::begin(reqData->rsv), std::end(reqData->rsv), 0x00);
        std::copy(obuLabelArr, obuLabelArr + 5, reqData->obuLabel);
        std::fill(std::begin(reqData->rsv1), std::end(reqData->rsv1), 0x00);
        enqueueCommand(CommandType::TRANSACTION_REQ_CMD, static_cast<int>(PRIORITY::NORMAL), reqData);
    }
    else
    {
        std::ostringstream oss;
        oss << "EEP: [CMD] Rejected | Reason=Invalid parameter | ";
        oss << "ObuLabelValid=" << obuParseSuccess << " | ObuLabel=" << obuLabel_;
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

        postCommandFailure(CommandType::TRANSACTION_REQ_CMD, MSG_STATUS::SEND_FAILED);
    }
}

void EEPClient::FnSendCPOInfoDisplayReq(const std::string& obuLabel_, const std::string& dataType_, const std::string& line1_, const std::string& line2_, const std::string& line3_, const std::string& line4_, const std::string& line5_)
{
    // Data Format in little-Endian
    auto reqData = std::make_shared<CPOInfoDisplayData>();

    uint8_t obuLabelArr[5];
    bool obuParseSuccess = Common::getInstance()->FnConvertHexStringToByteArray(obuLabel_, obuLabelArr, 5);
    bool dataTypeParseSuccess;
    uint8_t parsedDataType_;
    try
    {
        parsedDataType_ = static_cast<uint8_t>(std::stoi(dataType_));
        dataTypeParseSuccess = true;
    }
    catch (...)
    {
        dataTypeParseSuccess = false;
    }

    if (obuParseSuccess && dataTypeParseSuccess)
    {
        std::fill(std::begin(reqData->rsv), std::end(reqData->rsv), 0x00);
        std::copy(obuLabelArr, obuLabelArr + 5, reqData->obuLabel);
        std::fill(std::begin(reqData->rsv1), std::end(reqData->rsv1), 0x00);
        reqData->dataType = parsedDataType_;
        std::fill(std::begin(reqData->rsv2), std::end(reqData->rsv2), 0x00);
        // Hardcoded 2seconds timeout
        reqData->timeout[0] = 0xD0;
        reqData->timeout[1] = 0x07;
        reqData->timeout[2] = 0x00;
        reqData->timeout[3] = 0x00;
        std::fill(std::begin(reqData->dataLenOfStoredData), std::end(reqData->dataLenOfStoredData), 0x00);
        std::memset(reqData->dataString1, ' ', sizeof(reqData->dataString1));
        std::memcpy(reqData->dataString1, line1_.c_str(), std::min(line1_.size(), sizeof(reqData->dataString1)));
        std::memset(reqData->dataString2, ' ', sizeof(reqData->dataString2));
        std::memcpy(reqData->dataString2, line2_.c_str(), std::min(line2_.size(), sizeof(reqData->dataString2)));
        std::memset(reqData->dataString3, ' ', sizeof(reqData->dataString3));
        std::memcpy(reqData->dataString3, line3_.c_str(), std::min(line3_.size(), sizeof(reqData->dataString3)));
        std::memset(reqData->dataString4, ' ', sizeof(reqData->dataString4));
        std::memcpy(reqData->dataString4, line4_.c_str(), std::min(line4_.size(), sizeof(reqData->dataString4)));
        std::memset(reqData->dataString5, ' ', sizeof(reqData->dataString5));
        std::memcpy(reqData->dataString5, line5_.c_str(), std::min(line5_.size(), sizeof(reqData->dataString5)));
        enqueueCommand(CommandType::CPO_INFO_DISPLAY_REQ_CMD, static_cast<int>(PRIORITY::NORMAL), reqData);
    }
    else
    {
        std::ostringstream oss;
        oss << "EEP: [CMD] Rejected | Reason=Invalid parameter | ";
        oss << "ObuLabelValid=" << obuParseSuccess << " | ObuLabel=" << obuLabel_;
        oss << " | DataTypeValid=" << dataTypeParseSuccess << " | DataType=" << parsedDataType_;
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

        postCommandFailure(CommandType::CPO_INFO_DISPLAY_REQ_CMD, MSG_STATUS::SEND_FAILED);
    }
}

void EEPClient::FnSendCarparkProcessCompleteNotificationReq(const std::string& obuLabel_, const std::string& processingResult_, const std::string& fee_)
{
    // Data Format in little-Endian
    auto reqData = std::make_shared<CarparkProcessCompleteData>();

    uint8_t obuLabelArr[5];
    bool obuParseSuccess = Common::getInstance()->FnConvertHexStringToByteArray(obuLabel_, obuLabelArr, 5);
    bool resultParseSuccess;
    uint8_t parsedResult_;
    try
    {
        parsedResult_ = static_cast<uint8_t>(std::stoi(processingResult_));
        resultParseSuccess = true;
    }
    catch (...)
    {
        resultParseSuccess = false;
    }

    uint8_t feeArr[2];
    bool feeParseSuccess = Common::getInstance()->FnDecimalStringToTwoBytes(fee_, feeArr, true);

    if (obuParseSuccess && resultParseSuccess && feeParseSuccess)
    {
        std::fill(std::begin(reqData->rsv), std::end(reqData->rsv), 0x00);
        std::copy(obuLabelArr, obuLabelArr + 5, reqData->obuLabel);
        std::fill(std::begin(reqData->rsv1), std::end(reqData->rsv1), 0x00);
        reqData->processingResult = parsedResult_;
        reqData->rsv2 = 0x00;
        std::copy(feeArr, feeArr + 2, reqData->amt);
        enqueueCommand(CommandType::CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD, static_cast<int>(PRIORITY::NORMAL), reqData);
    }
    else
    {
        std::ostringstream oss;
        oss << "EEP: [CMD] Rejected | Reason=Invalid parameter | ";
        oss << "ObuLabelValid=" << obuParseSuccess << " | ObuLabel=" << obuLabel_;
        oss << " | ProcessingResultValid=" << resultParseSuccess << " | ProcessingResult=" << processingResult_;
        oss << " | FeeValid=" << feeParseSuccess << " | Fee=" << fee_;
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

        postCommandFailure(CommandType::CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD, MSG_STATUS::SEND_FAILED);
    }
}

void EEPClient::FnSendDSRCProcessCompleteNotificationReq(const std::string& obuLabel_)
{
    // Data Format in little-Endian
    auto reqData = std::make_shared<DSRCProcessCompleteData>();

    uint8_t obuLabelArr[5];
    bool obuParseSuccess = Common::getInstance()->FnConvertHexStringToByteArray(obuLabel_, obuLabelArr, 5);

    if (obuParseSuccess)
    {
        std::fill(std::begin(reqData->rsv), std::end(reqData->rsv), 0x00);
        std::copy(obuLabelArr, obuLabelArr + 5, reqData->obuLabel);
        std::fill(std::begin(reqData->rsv1), std::end(reqData->rsv1), 0x00);
        enqueueCommand(CommandType::DSRC_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD, static_cast<int>(PRIORITY::NORMAL), reqData);
    }
    else
    {
        std::ostringstream oss;
        oss << "EEP: [CMD] Rejected | Reason=Invalid parameter | ";
        oss << "ObuLabelValid=" << obuParseSuccess << " | ObuLabel=" << obuLabel_;
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

        postCommandFailure(CommandType::DSRC_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD, MSG_STATUS::SEND_FAILED);
    }
}

void EEPClient::FnSendStopReqOfRelatedInfoDistributionReq(const std::string& obuLabel_)
{
    // Data Format in little-Endian
    auto reqData = std::make_shared<StopReqOfRelatedInfoData>();

    uint8_t obuLabelArr[5];
    bool obuParseSuccess = Common::getInstance()->FnConvertHexStringToByteArray(obuLabel_, obuLabelArr, 5);

    if (obuParseSuccess)
    {
        std::fill(std::begin(reqData->rsv), std::end(reqData->rsv), 0x00);
        std::copy(obuLabelArr, obuLabelArr + 5, reqData->obuLabel);
        std::fill(std::begin(reqData->rsv1), std::end(reqData->rsv1), 0x00);
        enqueueCommand(CommandType::STOP_REQ_OF_RELATED_INFO_DISTRIBUTION_CMD, static_cast<int>(PRIORITY::NORMAL), reqData);
    }
    else
    {
        std::ostringstream oss;
        oss << "EEP: [CMD] Rejected | Reason=Invalid parameter | ";
        oss << "ObuLabelValid=" << obuParseSuccess << " | ObuLabel=" << obuLabel_;
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

        postCommandFailure(CommandType::STOP_REQ_OF_RELATED_INFO_DISTRIBUTION_CMD, MSG_STATUS::SEND_FAILED);
    }
}

void EEPClient::FnSendDSRCStatusReq()
{
    enqueueCommand(CommandType::DSRC_STATUS_REQ_CMD, static_cast<int>(PRIORITY::NORMAL), nullptr);
}

void EEPClient::FnSendTimeCalibrationReq()
{
    // Data Format in little-Endian
    auto reqData = std::make_shared<TimeCalibrationData>();

    auto now = std::chrono::system_clock::now();
    auto timer = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo = {};
    localtime_r(&timer, &timeinfo);

    reqData->day = static_cast<uint8_t>(timeinfo.tm_mday);
    reqData->month = static_cast<uint8_t>(timeinfo.tm_mon + 1);
    uint16_t parsedYear = static_cast<uint16_t>(timeinfo.tm_year + 1900);
    reqData->year[0] = static_cast<uint8_t>(parsedYear & 0xFF);
    reqData->year[1] = static_cast<uint8_t>((parsedYear >> 8) & 0xFF);
    reqData->rsv = 0x00;
    reqData->second = static_cast<uint8_t>(timeinfo.tm_sec);
    reqData->minute = static_cast<uint8_t>(timeinfo.tm_min);
    reqData->hour = static_cast<uint8_t>(timeinfo.tm_hour);
    enqueueCommand(CommandType::TIME_CALIBRATION_REQ_CMD, static_cast<int>(PRIORITY::NORMAL), reqData);
}

void EEPClient::FnSendSetCarparkAvailabilityReq(const std::string& availLots_, const std::string& totalLots_)
{
    // Data Format in little-Endian
    auto reqData = std::make_shared<SetParkingAvailabilityData>();

    uint8_t availLotsArr[2];
    bool availLotsParseSuccess = Common::getInstance()->FnDecimalStringToTwoBytes(availLots_, availLotsArr, true);
    uint8_t totalLotsArr[2];
    bool totalLotsParseSuccess = Common::getInstance()->FnDecimalStringToTwoBytes(totalLots_, totalLotsArr, true);
    
    if (availLotsParseSuccess && totalLotsParseSuccess)
    {
        std::copy(availLotsArr, availLotsArr + 2, reqData->availableLots);
        std::copy(totalLotsArr, totalLotsArr + 2, reqData->totalLots);
        std::fill(std::begin(reqData->rsv), std::end(reqData->rsv), 0x00);
        enqueueCommand(CommandType::SET_CARPARK_AVAIL_REQ_CMD, static_cast<int>(PRIORITY::NORMAL), reqData);
    }
    else
    {
        std::ostringstream oss;
        oss << "EEP: [CMD] Rejected | Reason=Invalid parameter | ";
        oss << "AvailableLotsValid=" << availLotsParseSuccess << " | AvailableLots=" << availLots_;
        oss << " | TotalLotsValid=" << totalLotsParseSuccess << " | TotalLots=" << totalLots_;
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

        postCommandFailure(CommandType::SET_CARPARK_AVAIL_REQ_CMD, MSG_STATUS::SEND_FAILED);
    }
}

void EEPClient::FnSendCDDownloadReq()
{
    enqueueCommand(CommandType::CD_DOWNLOAD_REQ_CMD, static_cast<int>(PRIORITY::NORMAL), nullptr);
}

void EEPClient::FnSendRestartInquiryResponseReq(uint8_t response)
{
    // Data Format in little-Endian
    auto reqData = std::make_shared<RestartInquiryResponseData>();

    reqData->response = response;
    std::fill(std::begin(reqData->rsv), std::end(reqData->rsv), 0x00);

    enqueueCommand(CommandType::EEP_RESTART_INQUIRY_REQ_CMD, static_cast<int>(PRIORITY::NORMAL), reqData);
}

bool EEPClient::startIoContextThread()
{
   if (ioContextThread_.joinable())
    {
        return true;
    }

    moduleRunning_.store(true);

    try
    {
        ioContextThread_ = std::thread(
            [this]()
            {
#if defined(__linux__)
                ::pthread_setname_np(::pthread_self(), "EEP_IO");
#endif
                Logger::getInstance()->FnLog(
                    "EEP: [THREAD] io_context started",
                    logFileName_,
                    "EEP");

                for (;;)
                {
                    try
                    {
                        ioContext_.run();
                        break;
                    }
                    catch (const std::exception& e)
                    {
                        Logger::getInstance()->FnLog(
                            std::string("EEP: [THREAD] Handler exception | Error=") +
                                e.what(),
                            logFileName_,
                            "EEP");

                        if (stopping_.load())
                        {
                            break;
                        }
                    }
                    catch (...)
                    {
                        Logger::getInstance()->FnLog(
                            "EEP: [THREAD] Handler exception | Error=Unknown",
                            logFileName_,
                            "EEP");

                        if (stopping_.load())
                        {
                            break;
                        }
                    }
                }

                moduleRunning_.store(false);

                Logger::getInstance()->FnLog(
                    "EEP: [THREAD] io_context stopped",
                    logFileName_,
                    "EEP");
            });
    }
    catch (...)
    {
        moduleRunning_.store(false);
        return false;
    }

    return true;
}

void EEPClient::shutdownOnIoThread()
{
    boost::system::error_code ec;

    reconnectTimer_.cancel(ec);
    connectTimer_.cancel(ec);
    sendTimer_.cancel(ec);
    responseTimer_.cancel(ec);
    ackTimer_.cancel(ec);
    watchdogTimer_.cancel(ec);
    healthStatusTimer_.cancel(ec);

    while (!commandQueue_.empty())
    {
        commandQueue_.pop();
    }

    if (client_)
    {
        try
        {
            client_->close();
        }
        catch (...)
        {
            // Best-effort shutdown. The client object will be destroyed only
            // after the io_context thread has drained and joined.
        }
    }
}

void EEPClient::resetRuntimeState()
{
    currentState_ = STATE::IDLE;
    commandSequence_ = 0;
    currentCmd_ = Command{};
    currentCmdRequested_ = Command{};
    sequenceNo_ = 0;
    expectedResponseSeqNo_.reset();
    watchdogMissedRspCount_ = 0;
    lastConnectionState_ = false;
    EEPData_In.store(0);

    while (!commandQueue_.empty())
    {
        commandQueue_.pop();
    }

    {
        std::lock_guard<std::mutex> lock(statusDataMutex_);
        status_data_.clear();
    }
}

void EEPClient::handleConnect(bool success, const std::string& message)
{
    boost::system::error_code ec;
    connectTimer_.cancel(ec);

    if (success)
    {
        Logger::getInstance()->FnLog(
            "EEP: [CONNECT] Success | Server=" + serverIP_ +
                " | Port=" + std::to_string(serverPort_),
            logFileName_,
            "EEP");
        processEvent(EVENT::CONNECT_SUCCESS);
        return;
    }

    Logger::getInstance()->FnLog(
        "EEP: [CONNECT] Failed | Server=" + serverIP_ +
            " | Port=" + std::to_string(serverPort_) +
            " | Error=" + message,
        logFileName_,
        "EEP");
    processEvent(EVENT::CONNECT_FAIL);
}

void EEPClient::handleSend(bool success, const std::string& message)
{
    boost::system::error_code ec;
    sendTimer_.cancel(ec);

    if (success)
    {
        Logger::getInstance()->FnLog(
            "EEP: [TX] Completed | Cmd=" + getCommandString(currentCmd_.type),
            logFileName_,
            "EEP");
        processEvent(EVENT::WRITE_COMPLETED);
        return;
    }

    Logger::getInstance()->FnLog(
        "EEP: [TX] Failed | Cmd=" + getCommandString(currentCmd_.type) +
            " | Error=" + message,
        logFileName_,
        "EEP");
    processEvent(EVENT::WRITE_TIMEOUT);
}

void EEPClient::handleClose(bool success, const std::string& message)
{
    if (success)
    {
        Logger::getInstance()->FnLog(
            "EEP: [CONNECT] Closed | Server=" + serverIP_ +
                " | Port=" + std::to_string(serverPort_),
            logFileName_,
            "EEP");
        return;
    }

    Logger::getInstance()->FnLog(
        "EEP: [CONNECT] Close failed | Server=" + serverIP_ +
            " | Port=" + std::to_string(serverPort_) +
            " | Error=" + message,
        logFileName_,
        "EEP");
}

bool EEPClient::isValidCheckSum(const std::vector<uint8_t>& data)
{
    // not enough data for checksum
    if (data.size() < 4)
    {
        return false;
    }

    // Big Endian
    uint32_t dataCheckSum = (static_cast<uint32_t>(data[data.size() - 1]) << 24) |
                            (static_cast<uint32_t>(data[data.size() - 2]) << 16) |
                            (static_cast<uint32_t>(data[data.size() - 3]) << 8) |
                            static_cast<uint32_t>(data[data.size() - 4]);
    // Calculate checksum for data excluding last 4 checksum bytes
    std::vector<uint8_t> dataWithoutChecksum(data.begin(), data.end() - 4);
    uint32_t calculatedCheckSum = calculateChecksumNoPadding(dataWithoutChecksum);

    return (dataCheckSum == calculatedCheckSum);
}

bool EEPClient::parseMessage(const std::vector<uint8_t>& data, MessageHeader& header, std::vector<uint8_t>& body)
{
    // Validate minimum size
    if (data.size() < MessageHeader::HEADER_SIZE)
    {
        Logger::getInstance()->FnLog("EEP: [RX] Parse failed | Reason=Header too short | Bytes=" + std::to_string(data.size()) + " | Required=" + std::to_string(MessageHeader::HEADER_SIZE), logFileName_, "EEP");
        return false;
    }

    // Deserialize header
    if (!header.deserialize(data))
    {
        Logger::getInstance()->FnLog("EEP: [RX] Parse failed | Reason=Header deserialize failed", logFileName_, "EEP");
        return false;
    }

    std::size_t expectedTotalSize = MessageHeader::HEADER_SIZE + header.dataLen_;
    if (data.size() < expectedTotalSize)
    {
        Logger::getInstance()->FnLog("EEP: [RX] Parse failed | Reason=Frame shorter than declared length | Bytes=" + std::to_string(data.size()) + " | Expected=" + std::to_string(expectedTotalSize), logFileName_, "EEP");
        return false;
    }

    // Extract body
    body.assign(data.begin() + MessageHeader::HEADER_SIZE, data.begin() + expectedTotalSize);

    return true;
}

void EEPClient::printField(std::ostringstream& oss, const std::string& label, uint64_t value, int hexWidth, const std::string& remark = "")
{
    std::ostringstream hexStream;
    hexStream << "0x"
              << std::setw(hexWidth) << std::setfill('0') << std::uppercase << std::hex << value;

    std::ostringstream decStream;
    decStream << "(" << std::dec << value << ")";

    oss << std::setw(32) << std::setfill(' ') << ""                    // indent
        << std::left << std::setw(50) << label                         // label left-aligned
        << ": "
        << std::right << std::setw(25) << hexStream.str()              // hex right-aligned
        << " "
        << std::right << std::setw(15) << decStream.str();             // dec right-aligned

    if (!remark.empty()) {
        oss << "  " << remark;
    }

    oss << "\n";
}

void EEPClient::printFieldHex(std::ostringstream& oss, const std::string& label, uint64_t value, int hexWidth, const std::string& remark = "")
{
    std::ostringstream hexStream;
    hexStream << "0x"
              << std::setw(hexWidth) << std::setfill('0') << std::uppercase << std::hex << value;

    std::ostringstream hexStreamWithoutPadding;
    hexStreamWithoutPadding << "\"" << std::hex << value << "\"";

    oss << std::setw(32) << std::setfill(' ') << ""                    // indent
        << std::left << std::setw(50) << label                         // label left-aligned
        << ": "
        << std::right << std::setw(25) << hexStream.str()              // hex right-aligned
        << " "
        << std::right << std::setw(15) << hexStreamWithoutPadding.str(); // hex right-aligned

    if (!remark.empty()) {
        oss << "  " << remark;
    }

    oss << "\n";
}

void EEPClient::printFieldChar(std::ostringstream& oss, const std::string& label, const std::vector<uint8_t>& data, const std::string& remark = "")
{
    std::ostringstream hexStream;
    std::ostringstream asciiStream;

    for (auto byte : data) {
        hexStream << std::setw(2) << std::setfill('0') << std::uppercase << std::hex << static_cast<int>(byte);
        char c = static_cast<char>(byte);
        asciiStream << (std::isprint(static_cast<unsigned char>(c)) ? c : '.');
    }

    // Format string consistently
    const int indentWidth = 32;
    const int labelWidth = 50;
    //const int hexWidth   = 20 + static_cast<int>(data.size()) * 2;  // allow for longer hex + spacing
    const int hexWidth   = 25;  // allow for longer hex + spacing
    //const int asciiWidth = 12 + static_cast<int>(data.size());      // flexible width for ASCII
    const int asciiWidth = 15;      // flexible width for ASCII

    oss << std::setw(indentWidth) << std::setfill(' ') << ""
        << std::left << std::setw(labelWidth) << label
        << ": "
        << std::right << std::setw(hexWidth) << ("0x" + hexStream.str())
        << "  "
        << std::left << std::setw(asciiWidth) << ("\"" + asciiStream.str() + "\"");

    if (!remark.empty()) {
        oss << "  " << remark;
    }

    oss << "\n";
}

void EEPClient::printFieldHexChar(std::ostringstream& oss, const std::string& label, const std::vector<uint8_t>& data, const std::string& remark = "")
{
    std::ostringstream hexStream;

    for (auto byte : data) {
        hexStream << std::setw(2) << std::setfill('0') << std::uppercase << std::hex << static_cast<int>(byte);
    }

    // Format string consistently
    const int indentWidth = 32;
    const int labelWidth = 50;
    //const int hexWidth   = 20 + static_cast<int>(data.size()) * 2;  // allow for longer hex + spacing
    const int hexWidth   = 25;  // allow for longer hex + spacing

    oss << std::setw(indentWidth) << std::setfill(' ') << ""
        << std::left << std::setw(labelWidth) << label
        << ": "
        << std::right << std::setw(hexWidth) << ("0x" + hexStream.str())
        << "  "
        << std::left << std::setw(hexWidth) << ("\"" + hexStream.str() + "\"");

    if (!remark.empty()) {
        oss << "  " << remark;
    }

    oss << "\n";
}



std::string EEPClient::getFieldDescription(uint8_t value, const std::unordered_map<uint8_t, std::string>& map)
{
    auto it = map.find(value);
    return it != map.end() ? it->second : "Unknown";
}

void EEPClient::showParsedMessage(const MessageHeader& header, const std::vector<uint8_t>& body)
{
    std::ostringstream oss;

    MESSAGE_CODE code = static_cast<MESSAGE_CODE>(header.dataTypeCode_);

    oss << "EEP: [RX] Parsed"
        << " | Code=" << messageCodeToString(code)
        << " | Seq=" << header.seqNo_
        << " | Src=" << static_cast<unsigned int>(header.sourceID_)
        << " | Dst=" << static_cast<unsigned int>(header.destinationID_)
        << " | DataLen=" << header.dataLen_
        << '\n';
    oss << "  Header\n";
    oss << std::setw(32) << std::setfill(' ') << "" << "----------------------------------\n";
    printField(oss, "Destination ID", header.destinationID_, 2);
    printField(oss, "Source ID", header.sourceID_, 2);
    printField(oss, "Data Type Code", header.dataTypeCode_, 2, messageCodeToString(code));
    printField(oss, "Reserved", header.rsv_, 2);
    printField(oss, "Day", header.day_, 2);
    printField(oss, "Month", header.month_, 2);
    printField(oss, "Year", header.year_, 4);
    printField(oss, "Reserved1", header.rsv1_, 2);
    printField(oss, "Second", header.second_, 2);
    printField(oss, "Minute", header.minute_, 2);
    printField(oss, "Hour", header.hour_, 2);
    printField(oss, "Sequence Number", header.seqNo_, 4);
    printField(oss, "Data Length", header.dataLen_, 4);

    if (!body.empty() && (body.size() == header.dataLen_))
    {
        oss << std::setw(32) << std::setfill(' ') << "" << "----------------------------------\n";
        switch (code)
        {
            case MESSAGE_CODE::ACK:
            {
                if (body.size() < 4)
                {
                    Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=ACK | Reason=Body too short", logFileName_, "EEP");
                    break;
                }

                uint8_t requestDataTypeCode = body[0];
                uint32_t rsv = Common::getInstance()->FnReadUint24LE(body, 1);

                printField(oss, "Request Data Type Code", requestDataTypeCode, 4, messageCodeToString(static_cast<MESSAGE_CODE>(requestDataTypeCode)));
                printField(oss, "Rsv", rsv, 6);
                break;
            }
            case MESSAGE_CODE::NAK:
            {
                if (body.size() < 4)
                {
                    Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=NAK | Reason=Body too short", logFileName_, "EEP");
                    break;
                }

                uint8_t requestDataTypeCode = body[0];
                uint8_t reasonCode = body[1];
                uint16_t rsv = Common::getInstance()->FnReadUint16LE(body, 2);

                const std::unordered_map<uint8_t, std::string> reasonCodeMap = {
                    {0x01, "Unsupported Data Type Code"},
                    {0x02, "Data Length Error"},
                    {0x03, "Check Code Mismatch"},
                    {0x04, "Others"},
                };
                
                printField(oss, "Request Data Type Code", requestDataTypeCode, 4, messageCodeToString(static_cast<MESSAGE_CODE>(requestDataTypeCode)));
                printField(oss, "Reason Code", reasonCode, 2, getFieldDescription(reasonCode, reasonCodeMap));
                printField(oss, "Rsv", rsv, 4);
                break;
            }
            case MESSAGE_CODE::WATCHDOG_RESPONSE:
            {
                break;
            }
            case MESSAGE_CODE::HEALTH_STATUS_RESPONSE:
            {
                if (body.size() < 64)
                {
                    Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=HEALTH_STATUS_RESPONSE | Reason=Body too short", logFileName_, "EEP");
                    break;
                }

                uint32_t subSystemLabel = Common::getInstance()->FnReadUint32LE(body, 0);
                uint8_t equipmentType = body[4];
                uint8_t EEPDSRCDeviceId = body[5];
                uint16_t carparkId = Common::getInstance()->FnReadUint16LE(body, 6);
                uint8_t lifeCycleMode = body[8];
                uint8_t maintenanceMode = body[9];
                uint8_t operationStatus = body[10];
                uint8_t esimStatus = body[11];
                uint8_t powerStatus = body[12];
                uint8_t internalBatteryStatus = body[13];
                uint8_t usbBusStatus = body[14];
                uint8_t dioStatus = body[15];
                uint8_t securityKeyStatus = body[16];
                uint8_t doorSwitchStatus = body[17];
                uint8_t temperatureStatus = body[18];
                uint8_t temperatureInHousing = body[19];
                uint8_t gnssSignalQuality = body[20];
                uint8_t numberOfGnssStatelites = body[21];
                uint8_t timeSyncStatus = body[22];
                uint8_t wwanConnectionStatus = body[23];
                uint8_t DSRCEmissionMode = body[24];
                uint8_t numberOfDSRCSession = body[25];
                uint8_t rsv = body[26];
                uint8_t rsv1 = body[27];
                uint8_t cpuLoad = body[28];
                uint8_t storageSpace = body[29];
                uint16_t rsv2 = Common::getInstance()->FnReadUint16LE(body, 30);
                // version is CHAR data type based on protocol, so already big endian, no need to reverse
                std::vector<uint8_t> version(body.begin() + 32, body.begin() + 40);
                uint16_t cscBlocklistTable1 = Common::getInstance()->FnReadUint32LE(body, 40);
                uint16_t cscBlocklistTable2 = Common::getInstance()->FnReadUint32LE(body, 42);
                uint16_t cscBlocklistTable3 = Common::getInstance()->FnReadUint32LE(body, 44);
                uint16_t cscBlocklistTable4 = Common::getInstance()->FnReadUint32LE(body, 46);
                uint16_t cscIssuerListTable1 = Common::getInstance()->FnReadUint32LE(body, 48);
                uint16_t cscIssuerListTable2 = Common::getInstance()->FnReadUint32LE(body, 50);
                uint16_t cscIssuerListTable3 = Common::getInstance()->FnReadUint32LE(body, 52);
                uint16_t cscIssuerListTable4 = Common::getInstance()->FnReadUint32LE(body, 54);
                uint8_t cscConfigurationParamVersion = body[56];
                uint32_t rsv3 = Common::getInstance()->FnReadUint24LE(body, 57);
                uint32_t backendPaymentTerminationListversion = Common::getInstance()->FnReadUint24LE(body, 60);
                uint8_t rsv4 = body[63];

                const std::unordered_map<uint8_t, std::string> equipmentTypeMap = {
                    {0x29, "Entry"},
                    {0x2A, "Exit"},
                    {0x2B, "Inter Link"},
                    {0x2C, "Entry( Barrier-less)"},
                    {0x2D, "Exit (Barrier-less)"},
                    {0x2E, "Inter Link (Barrier-less)"}
                };

                const std::unordered_map<uint8_t, std::string> lifeCycleModeMap = {
                    {0x00, "Logistics Mode"},
                    {0x01, "Ready for car park Operation"}
                };

                const std::unordered_map<uint8_t, std::string> maintenanceModeMap = {
                    {0x00, "Not Maintenance Mode"},
                    {0x01, "Maintenance Mode (Operator login from Maintenance PC)"}
                };

                const std::unordered_map<uint8_t, std::string> operationStatusMap = {
                    {0x00, "Out-Operation"},
                    {0x01, "In-Operation"}
                };

                const std::unordered_map<uint8_t, std::string> esimStatusMap = {
                    {0x00, "Not activated"},
                    {0x01, "Activated"}
                };

                const std::unordered_map<uint8_t, std::string> powerStatusMap = {
                    {0x00, "Power is normal"},
                    {0x01, "Power is low voltage"}
                };

                const std::unordered_map<uint8_t, std::string> internalBatteryStatusMap = {
                    {0x00, "Normal"},
                    {0x01, "Low battery"}
                };

                const std::unordered_map<uint8_t, std::string> usbBusStatusMap = {
                    {0x00, "Normal"},
                    {0x01, "Error"}
                };

                const std::unordered_map<uint8_t, std::string> dioStatusMap = {
                    {0x00, "Normal"},
                    {0x01, "Error"}
                };

                const std::unordered_map<uint8_t, std::string> securityKeyStatusMap = {
                    {0x00, "All key is valid"},
                    {0x01, "Some keys are invalid"}
                };

                const std::unordered_map<uint8_t, std::string> doorSwitchStatusMap = {
                    {0x00, "Closed"},
                    {0x01, "Opened"}
                };

                const std::unordered_map<uint8_t, std::string> temperatureStatusMap = {
                    {0x00, "Normal"},
                    {0x01, "High temperature"}
                };

                const std::unordered_map<uint8_t, std::string> gnssSignalQualityMap = {
                    {0x00, "Normal"},
                    {0x01, "GNSS quality wrong"}
                };

                const std::unordered_map<uint8_t, std::string> timeSyncStatusMap = {
                    {0x00, "Time synchronization has been performed within the last 24 hours"},
                    {0x01, "Time synchronization has not been performed within the last 24 hours."}
                };

                const std::unordered_map<uint8_t, std::string> wwanConnectionStatusMap = {
                    {0x00, "WWAN Connected"},
                    {0x01, "WWAN Disconnected"}
                };

                const std::unordered_map<uint8_t, std::string> DSRCEmissionModeMap = {
                    {0x00, "Loop triggered emission mode"},
                    {0x01, "Continuous emission mode"}
                };

                const std::unordered_map<uint8_t, std::string> storageSpaceMap = {
                    {0x00, "Normal"},
                    {0x01, "Out of storage space"}
                };

                printField(oss, "Sub System Label", subSystemLabel, 8);
                printField(oss, "Equipment Type", equipmentType, 2, getFieldDescription(equipmentType, equipmentTypeMap));
                printField(oss, "EEP DSRC Device ID", EEPDSRCDeviceId, 2);
                printField(oss, "CarparkID", carparkId, 4);
                printField(oss, "LifecycleMode", lifeCycleMode, 2, getFieldDescription(lifeCycleMode, lifeCycleModeMap));
                printField(oss, "MaintenanceMode", maintenanceMode, 2, getFieldDescription(maintenanceMode, maintenanceModeMap));
                printField(oss, "OperationStatus", operationStatus, 2, getFieldDescription(operationStatus, operationStatusMap));
                printField(oss, "E-SIM Status", esimStatus, 2, getFieldDescription(esimStatus, esimStatusMap));
                printField(oss, "Power Status", powerStatus, 2, getFieldDescription(powerStatus, powerStatusMap));
                printField(oss, "Internal Battery Status", internalBatteryStatus, 2, getFieldDescription(internalBatteryStatus, internalBatteryStatusMap));
                printField(oss, "USB Bus Status", usbBusStatus, 2, getFieldDescription(usbBusStatus, usbBusStatusMap));
                printField(oss, "DIO Status", dioStatus, 2, getFieldDescription(dioStatus, dioStatusMap));
                printField(oss, "Security Key Status", securityKeyStatus, 2, getFieldDescription(securityKeyStatus, securityKeyStatusMap));
                printField(oss, "Door Switch Status", doorSwitchStatus, 2, getFieldDescription(doorSwitchStatus, doorSwitchStatusMap));
                printField(oss, "Temperature Status", temperatureStatus, 2, getFieldDescription(temperatureStatus, temperatureStatusMap));
                printField(oss, "Temperature Status", temperatureInHousing, 2, "Degree Celsius");
                printField(oss, "GNSS Signal Quality", gnssSignalQuality, 2, getFieldDescription(gnssSignalQuality, gnssSignalQualityMap));
                printField(oss, "Number of GNSS Satellites", numberOfGnssStatelites, 2);
                printField(oss, "Time Sync Status", timeSyncStatus, 2, getFieldDescription(timeSyncStatus, timeSyncStatusMap));
                printField(oss, "WWANConnectionStatus", wwanConnectionStatus, 2, getFieldDescription(wwanConnectionStatus, wwanConnectionStatusMap));
                printField(oss, "DSRC Emission Mode", DSRCEmissionMode, 2, getFieldDescription(DSRCEmissionMode, DSRCEmissionModeMap));
                printField(oss, "Number of DSRC Session", numberOfDSRCSession, 2);
                printField(oss, "RSV", rsv, 2);
                printField(oss, "RSV1", rsv1, 2);
                printField(oss, "CPU Load", cpuLoad, 2, "%");
                printField(oss, "Storage Space", storageSpace, 2, getFieldDescription(storageSpace, storageSpaceMap));
                printField(oss, "RSV2", rsv2, 2);
                printFieldChar(oss, "Version", version);
                printField(oss, "CSC Blocklist Table Version[1]", cscBlocklistTable1, 4);
                printField(oss, "CSC Blocklist Table Version[2]", cscBlocklistTable2, 4);
                printField(oss, "CSC Blocklist Table Version[3]", cscBlocklistTable3, 4);
                printField(oss, "CSC Blocklist Table Version[4]", cscBlocklistTable4, 4);
                printField(oss, "CSC Issuer list Table Version[1]", cscIssuerListTable1, 4);
                printField(oss, "CSC Issuer list Table Version[2]", cscIssuerListTable2, 4);
                printField(oss, "CSC Issuer list Table Version[3]", cscIssuerListTable3, 4);
                printField(oss, "CSC Issuer list Table Version[4]", cscIssuerListTable4, 4);
                printField(oss, "CSC configuration parameter version", cscConfigurationParamVersion, 4);
                printField(oss, "RSV3", rsv3, 6);
                printField(oss, "Backend payment termination list version", backendPaymentTerminationListversion, 6);
                printField(oss, "RSV4", rsv4, 2);

                break;
            }
            case MESSAGE_CODE::START_RESPONSE:
            {
                if (body.size() < 4)
                {
                    Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=START_RESPONSE | Reason=Body too short", logFileName_, "EEP");
                    break;
                }

                uint8_t resultCode = body[0];
                uint32_t rsv = Common::getInstance()->FnReadUint24LE(body, 1);

                const std::unordered_map<uint8_t, std::string> resultCodeMap = {
                    {0x01, "EEP successes to start operation. State is 'In-Operation'"},
                    {0x02, "EEP DSRC Device has been 'In-Operation' state already"},
                    {0x03, "EEP fails to start operation due to EEP DSRC Device internal problem. State is 'Out-Operation'"},
                    {0x04, "EEP fails to start operation due to EEP DSRC Device is 'In maintenance mode'. State is 'Out-Operation'"},
                    {0x05, "EEP fails to start operation due to EEP DSRC Device has no effective Device.cfg file. State is 'Out-Operation'"},
                    {0x06, "EEP fails to start operation due to Next CPID is not configured in interlink carpark. State is 'Out-Operation'"}
                };

                printField(oss, "Result Code", resultCode, 2, getFieldDescription(resultCode, resultCodeMap));
                printField(oss, "RSV", rsv, 6);
                break;
            }
            case MESSAGE_CODE::STOP_RESPONSE:
            {
                if (body.size() < 4)
                {
                    Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=STOP_RESPONSE | Reason=Body too short", logFileName_, "EEP");
                    break;
                }

                uint8_t resultCode = body[0];
                uint32_t rsv = Common::getInstance()->FnReadUint24LE(body, 1);

                const std::unordered_map<uint8_t, std::string> resultCodeMap = {
                    {0x01, "EEP DSRC Device successes to stop operation. State is 'Out-Operation'"},
                    {0x02, "EEP DSRC Device has been 'Out-Operation' state already"},
                    {0x03, "EEP DSRC Device fails to stop operation. State is 'In-Operation'"}
                };

                printField(oss, "Result Code", resultCode, 2, getFieldDescription(resultCode, resultCodeMap));
                printField(oss, "RSV", rsv, 6);
                break;
            }
            case MESSAGE_CODE::DI_STATUS_NOTIFICATION:
            case MESSAGE_CODE::DI_STATUS_RESPONSE:
            {
                if (body.size() < 8)
                {
                    Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=DI_STATUS | Reason=Body too short", logFileName_, "EEP");
                    break;
                }

                uint32_t currDISignal = Common::getInstance()->FnReadUint32LE(body, 0);
                uint32_t prevDISignal = Common::getInstance()->FnReadUint32LE(body, 4);

                std::ostringstream currDISignalStr;
                currDISignalStr << "0bit : " << ((currDISignal & 0x00000001u) ? "1" : "0") << ", "
                                << "1bit : " << ((currDISignal & 0x00000002u) ? "1" : "0") << ", "
                                << "2bit : " << ((currDISignal & 0x00000004u) ? "1" : "0") << ", "
                                << "3bit : " << ((currDISignal & 0x00000008u) ? "1" : "0") << ", "
                                << "4bit : " << ((currDISignal & 0x00000010u) ? "1" : "0");

                std::ostringstream prevDISignalStr;
                prevDISignalStr << "0bit : " << ((prevDISignal & 0x00000001u) ? "1" : "0") << ", "
                                << "1bit : " << ((prevDISignal & 0x00000002u) ? "1" : "0") << ", "
                                << "2bit : " << ((prevDISignal & 0x00000004u) ? "1" : "0") << ", "
                                << "3bit : " << ((prevDISignal & 0x00000008u) ? "1" : "0") << ", "
                                << "4bit : " << ((prevDISignal & 0x00000010u) ? "1" : "0");

                printField(oss, "Current DI Signal", currDISignal, 8, currDISignalStr.str());
                printField(oss, "Previous DI Signal", prevDISignal, 8, prevDISignalStr.str());
                break;
            }
            case MESSAGE_CODE::OBU_INFORMATION_NOTIFICATION:
            {
                if (body.size() < 56)
                {
                    Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=OBU_INFORMATION_NOTIFICATION | Reason=Body too short", logFileName_, "EEP");
                    break;
                }

                uint64_t rsv = Common::getInstance()->FnReadUint64LE(body, 0);
                uint64_t obulabel = Common::getInstance()->FnReadUint40BE(body, 8);
                uint8_t typeObu = body[13];
                uint16_t rsv1 = Common::getInstance()->FnReadUint16LE(body, 14);
                uint32_t vcc = Common::getInstance()->FnReadUint24LE(body, 16);
                uint8_t rsv2 = body[19];
                // Vehicle number is CHAR data type based on protocol, so already big endian, no need to reverse
                std::vector<uint8_t> vechicleNumber(body.begin() + 20, body.begin() + 33);
                uint32_t rsv3 = Common::getInstance()->FnReadUint24LE(body, 33);
                uint8_t cardValidity = body[36];
                uint32_t rsv4 = Common::getInstance()->FnReadUint24LE(body, 37);
                // CAN is CHAR data type based on protocol, so already big endian, no need to reverse
                std::vector<uint8_t> can(body.begin() + 40, body.begin() + 48);
                uint32_t cardBalance = Common::getInstance()->FnReadUint32LE(body, 48);
                uint8_t backendAccount = body[52];
                uint8_t backendSetting = body[53];
                uint8_t businessFunctionStatus = body[54];
                uint8_t rsv5 = body[55];

                const std::unordered_map<uint8_t, std::string> typeObuMap = {
                    {0x00, "OBU Type A"},
                    {0x01, "OBU Type B"}
                };

                const std::unordered_map<uint8_t, std::string> cardValidityMap = {
                    {0x00, "Valid"},
                    {0x01, "No card"},
                    {0x02, "Invalid card"},
                    {0x03, "Card issuer error"},
                    {0x04, "Blacklisted card"}
                };

                const std::unordered_map<uint8_t, std::string> backendAccountMap = {
                    {0x00, "Invalid or no account"},
                    {0x01, "Valid"},
                    {0x02, "Unknown"}
                };

                const std::unordered_map<uint8_t, std::string> backendSettingMap = {
                    {0x00, "OFF"},
                    {0x01, "ON"}
                };

                printField(oss, "RSV", rsv, 16);
                printFieldHex(oss, "OBU Label", obulabel, 10);
                printField(oss, "Type of OBU", typeObu, 2, getFieldDescription(typeObu, typeObuMap));
                printField(oss, "RSV1", rsv1, 4);
                printField(oss, "VCC", vcc, 6);
                printField(oss, "RSV2", rsv2, 2);
                printFieldChar(oss, "Vehicle Number", vechicleNumber);
                printField(oss, "RSV3", rsv3, 6);
                printField(oss, "Card Validity", cardValidity, 2, getFieldDescription(cardValidity, cardValidityMap));
                printField(oss, "RSV4", rsv4, 6);
                printFieldHexChar(oss, "CAN", can);
                printField(oss, "Card Balance", cardBalance, 8, " Unit is cent");
                printField(oss, "Backend Account", backendAccount, 2, getFieldDescription(backendAccount, backendAccountMap));
                printField(oss, "Backend Setting", backendSetting, 2, getFieldDescription(backendSetting, backendSettingMap));
                printField(oss, "Business function status", businessFunctionStatus, 2);
                printField(oss, "RSV5", rsv5, 2);
                break;
            }
            case MESSAGE_CODE::TRANSACTION_DATA:
            {
                if (body.size() < 340)
                {
                    Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=TRANSACTION_DATA | Reason=Body too short", logFileName_, "EEP");
                    break;
                }

                uint16_t deductCommandSerialNum = Common::getInstance()->FnReadUint16LE(body, 0);
                uint8_t protocolVer = body[2];
                uint8_t resultDeduction = body[3];
                uint32_t subSystemLabel = Common::getInstance()->FnReadUint32LE(body, 4);
                uint64_t rsv = Common::getInstance()->FnReadUint64LE(body, 8);
                uint64_t obuLabel = Common::getInstance()->FnReadUint40BE(body, 16);
                uint32_t rsv1 = Common::getInstance()->FnReadUint24LE(body, 21);
                // Vehicle number is CHAR data type based on protocol, so already big endian, no need to reverse
                std::vector<uint8_t> vechicleNumber(body.begin() + 24, body.begin() + 37);
                uint32_t rsv2 = Common::getInstance()->FnReadUint24LE(body, 37);
                uint8_t transactionRoute = body[40];
                uint8_t rsv3 = body[41];
                uint8_t frontendPaymentViolation = body[42];
                uint8_t backendPaymentViolation = body[43];
                uint8_t transactionType = body[44];
                uint8_t rsv4 = Common::getInstance()->FnReadUint24LE(body, 45);
                uint8_t parkingStartDay = body[48];
                uint8_t parkingStartMonth = body[49];
                uint16_t parkingStartYear = Common::getInstance()->FnReadUint16LE(body, 50);
                uint8_t rsv5 = body[52];
                uint8_t parkingStartSecond = body[53];
                uint8_t parkingStartMinute = body[54];
                uint8_t parkingStartHour = body[55];
                uint8_t parkingEndDay = body[56];
                uint8_t parkingEndMonth = body[57];
                uint16_t parkingEndYear = Common::getInstance()->FnReadUint16LE(body, 58);
                uint8_t rsv6 = body[60];
                uint8_t parkingEndSecond = body[61];
                uint8_t parkingEndMinute = body[62];
                uint8_t parkingEndHour = body[63];
                uint32_t paymentFee = Common::getInstance()->FnReadUint32LE(body, 64);
                uint64_t fepTime = Common::getInstance()->FnReadUint56BE(body, 68);
                uint8_t rsv7 = body[75];
                uint32_t trp = Common::getInstance()->FnReadUint32BE(body, 76);
                uint8_t indicationLastAutoLoad = body[80];
                uint32_t rsv8 = Common::getInstance()->FnReadUint24LE(body, 81);
                // CAN is CHAR data type based on protocol, so already big endian, no need to reverse
                std::vector<uint8_t> can(body.begin() + 84, body.begin() + 92);
                uint64_t lastCreditTransactionHeader = Common::getInstance()->FnReadUint64BE(body, 92);
                uint32_t lastCreditTransactionTRP = Common::getInstance()->FnReadUint32BE(body, 100);
                uint32_t purseBalanceBeforeTransaction = Common::getInstance()->FnReadUint32LE(body, 104);
                uint8_t badDebtCounter = body[108];
                uint8_t transactionStatus = body[109];
                uint8_t debitOption = body[110];
                uint8_t rsv9 = body[111];
                uint32_t autoLoadAmount = Common::getInstance()->FnReadUint32LE(body, 112);
                uint64_t counterData = Common::getInstance()->FnReadUint64BE(body, 116);
                uint64_t signedCertificate = Common::getInstance()->FnReadUint64BE(body, 124);
                uint32_t purseBalanceAfterTransaction = Common::getInstance()->FnReadUint32LE(body, 132);
                uint8_t lastTransactionDebitOptionbyte = body[136];
                uint32_t rsv10 = Common::getInstance()->FnReadUint24LE(body, 137);
                uint64_t previousTransactionHeader = Common::getInstance()->FnReadUint64BE(body, 140);
                uint32_t previousTRP = Common::getInstance()->FnReadUint32BE(body, 148);
                uint32_t previousPurseBalance = Common::getInstance()->FnReadUint32LE(body, 152);
                uint64_t previousCounterData = Common::getInstance()->FnReadUint64BE(body, 156);
                uint64_t previousTransactionSignedCertificate = Common::getInstance()->FnReadUint64BE(body, 164);
                uint8_t previousPurseStatus = body[172];
                uint32_t rsv11 = Common::getInstance()->FnReadUint24LE(body, 173);
                uint16_t bepPaymentFeeAmount = Common::getInstance()->FnReadUint16LE(body, 176);
                uint16_t rsv12 = Common::getInstance()->FnReadUint16LE(body, 178);
                // Bep Time Of Report is CHAR data type based on protocol, so already big endian, no need to reverse
                std::vector<uint8_t> bepTimeOfReport(body.begin() + 180, body.begin() + 189);
                uint32_t rsv13 = Common::getInstance()->FnReadUint24LE(body, 189);
                uint32_t chargeReportCounter = Common::getInstance()->FnReadUint32LE(body, 192);
                uint8_t bepKeyVersion = body[196];
                uint32_t rsv14 = Common::getInstance()->FnReadUint24LE(body, 197);
                uint8_t lenOfBepCertificate = body[200];
                std::vector<uint8_t> bepCertificate(body.begin() + 201, body.begin() + 340);

                const std::unordered_map<std::uint8_t, std::string> resultDeductionMap = {
                    {0x00, "No deduction is performed"},
                    {0x01, "Deduction success (FE-Pay)"},
                    {0x02, "Deduction success (BE-Pay)"},
                    {0x03, "Reserve for future use"},
                    {0x04, "Reserve for future use"},
                    {0x05, "Deduction result is unknown due to DSRC disconnection"},
                    {0x06, "Reserve for future use"},
                    {0x07, "Deduction is rejected because double deduction is suspected"},
                    {0x08, "Deduction success. Payment transaction will be sent later via Nexgen ERP CCS"},
                    {0x09, "Deduction is rejected due to DSRC failure"}
                };

                const std::unordered_map<std::uint8_t, std::string> transactionRouteMap = {
                    {0x00, "EP receives Transaction from OBU via DSRC"},
                    {0x01, "EEP receives Transaction from OBU via WWAN"}
                };

                const std::unordered_map<std::uint8_t, std::string> frontendPaymentViolationMap = {
                    {0x00, "No violation"},
                    {0x01, "Blacklisted Card"},
                    {0x02, "Expired stored value Card"},
                    {0x03, "Invalid Card"},
                    {0x04, "Card Issuer ID Error"},
                    {0x05, "No card for frontend payment mode"},
                    {0x06, "Blocked Card"},
                    {0x07, "Faulty store value card"},
                    {0x08, "Insufficient stored value card"},
                    {0x09, "Unknown card validity"},
                    {0x0A, "Wrong stored value card debit certificate"},
                    {0x0B, "NFC access error"}
                };

                const std::unordered_map<std::uint8_t, std::string> backendPaymentViolationMap = {
                    {0x00, "No violation"},
                    {0x01, "Backend Payment Account Invalid"}
                };

                const std::unordered_map<std::uint8_t, std::string> indicationLastAutoLoadMap = {
                    {0x00, "Autoload did not occur"},
                    {0x01, "Autoload occurred"}
                };

                printField(oss, "Deduct command serial number", deductCommandSerialNum, 4);
                printField(oss, "Protocol Version", protocolVer, 2);
                printField(oss, "Result of Deduction", resultDeduction, 2, getFieldDescription(resultDeduction, resultDeductionMap));
                printField(oss, "Sub System Label", subSystemLabel, 8);
                printField(oss, "RSV", rsv, 16);
                printFieldHex(oss, "ObuLabel", obuLabel, 10);
                printField(oss, "RSV1", rsv1, 6);
                printFieldChar(oss, "Vehicle Number", vechicleNumber);
                printField(oss, "RSV2", rsv2, 6);
                printField(oss, "Transaction Route", transactionRoute, 2, getFieldDescription(transactionRoute, transactionRouteMap));
                printField(oss, "RSV3", rsv3, 2);
                printField(oss, "Frontend Payment Violation", frontendPaymentViolation, 2, getFieldDescription(frontendPaymentViolation, frontendPaymentViolationMap));
                printField(oss, "Backend Payment Violation", backendPaymentViolation, 2, getFieldDescription(backendPaymentViolation, backendPaymentViolationMap));
                printField(oss, "Transaction type", transactionType, 2);
                printField(oss, "RSV4", rsv4, 6);
                printField(oss, "parkingStartDay", parkingStartDay, 2);
                printField(oss, "parkingStartMonth", parkingStartMonth, 2);
                printField(oss, "parkingStartYear", parkingStartYear, 4);
                printField(oss, "RSV5", rsv5, 2);
                printField(oss, "parkingStartSecond", parkingStartSecond, 2);
                printField(oss, "parkingStartMinute", parkingStartMinute, 2);
                printField(oss, "parkingStartHour", parkingStartHour, 2);
                printField(oss, "parkingEndDay", parkingEndDay, 2);
                printField(oss, "parkingEndMonth", parkingEndMonth, 2);
                printField(oss, "parkingEndYear", parkingEndYear, 4);
                printField(oss, "RSV6", rsv6, 2);
                printField(oss, "parkingEndSecond", parkingEndSecond, 2);
                printField(oss, "parkingEndMinute", parkingEndMinute, 2);
                printField(oss, "parkingEndHour", parkingEndHour, 2);
                printField(oss, "Payment fee", paymentFee, 8, " Unit is cent");
                oss << std::setw(32) << std::setfill(' ') << "" << "----------------------------------\n";
                oss << std::setw(32) << std::setfill(' ') << "" << "Debiting result of Frontend Payment\n";
                oss << std::setw(32) << std::setfill(' ') << "" << "----------------------------------\n";
                printField(oss, "FepTime", fepTime, 14);
                printField(oss, "RSV7", rsv7, 2);
                printField(oss, "TRP(Terminal Resource Parameter)", trp, 8);
                printField(oss, "Indication of Last AutoLoad", indicationLastAutoLoad, 2, getFieldDescription(indicationLastAutoLoad, indicationLastAutoLoadMap));
                printField(oss, "RSV8", rsv8, 6);
                printFieldHexChar(oss, "CAN", can);
                printField(oss, "Last Credit Transaction Header", lastCreditTransactionHeader, 16);
                printField(oss, "Last Credit Transaction TRP", lastCreditTransactionTRP, 8);
                printField(oss, "Purse Balance Before Transaction", purseBalanceBeforeTransaction, 8, " Unit is cent");
                printField(oss, "Bad Debt Counter", badDebtCounter, 2, " Bit 0 to 1: BadDebtCounter");
                printField(oss, "Transaction Status", transactionStatus, 2, " Bit 0 to 4 : Transaction Status");
                printField(oss, "Debit Option", debitOption, 2);
                printField(oss, "RSV9", rsv9, 2);
                printField(oss, "AutoLoad Amount", autoLoadAmount, 8);
                printField(oss, "Counter Data", counterData, 16);
                printField(oss, "Signed Certificate", signedCertificate, 16);
                printField(oss, "Purse Balance after transaction", purseBalanceAfterTransaction, 8, " Unit is cent");
                printField(oss, "Last Transaction Debit Option byte", lastTransactionDebitOptionbyte, 2);
                printField(oss, "RSV10", rsv10, 6);
                printField(oss, "Previous Transaction Header", previousTransactionHeader, 16);
                printField(oss, "Previous TRP", previousTRP, 8);
                printField(oss, "Previous Purse balance", previousPurseBalance, 8, " Unit is cent");
                printField(oss, "Previous Counter Data", previousCounterData, 16);
                printField(oss, "Previous Transaction Signed Certificate", previousTransactionSignedCertificate, 16);
                printField(oss, "Previous Purse Status", previousPurseStatus, 2);
                printField(oss, "RSV11", rsv11, 6);
                oss << std::setw(32) << std::setfill(' ') << "" << "----------------------------------\n";
                oss << std::setw(32) << std::setfill(' ') << "" << "Debiting result of Backend Payment\n";
                oss << std::setw(32) << std::setfill(' ') << "" << "----------------------------------\n";
                printField(oss, "BepPaymentFeeAmount", bepPaymentFeeAmount, 4);
                printField(oss, "RSV12", rsv12, 4);
                printFieldChar(oss, "BepTimeOfReport", bepTimeOfReport);
                printField(oss, "RSV13", rsv13, 6);
                printField(oss, "chargeReportCounter", chargeReportCounter, 8);
                printField(oss, "BepKeyVersion", bepKeyVersion, 2);
                printField(oss, "RSV14", rsv14, 6);
                printField(oss, "LengthOfBepCertificate", lenOfBepCertificate, 2);
                printFieldChar(oss, "BepCertificate", bepCertificate);
                break;
            }
            case MESSAGE_CODE::CPO_INFORMATION_DISPLAY_RESULT:
            {
                if (body.size() < 20)
                {
                    Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=CPO_INFORMATION_DISPLAY_RESULT | Reason=Body too short", logFileName_, "EEP");
                    break;
                }

                uint16_t sequenceNoOfRequestMsg = Common::getInstance()->FnReadUint16LE(body, 0);
                uint8_t result = body[2];
                uint32_t rsv_lsb = Common::getInstance()->FnReadUint32LE(body, 3);
                uint64_t rsv_msb = Common::getInstance()->FnReadUint64LE(body, 7);
                uint64_t obuLabel = Common::getInstance()->FnReadUint40BE(body, 12);
                uint32_t rsv1 = Common::getInstance()->FnReadUint24LE(body, 17);

                const std::unordered_map<uint8_t, std::string> resultMap = {
                    {0x00, "When CPO / EEP information transmission is not successful due to connection timeout"},
                    {0x01, "When CPO / EEP information transmission is successful (normal termination is returned from DSRC module)"},
                    {0x02, "When the parameter of the received CPO information display request is incorrect. E.g. Undefined Data Type"},
                    {0x03, "In the DSRC sequence, when the CPO information display request is received before the Carpark process completion notice is received."},
                };

                printField(oss, "SequenceNumber of Request message", sequenceNoOfRequestMsg, 4);
                printField(oss, "Result", result, 2, getFieldDescription(result, resultMap));
                printField(oss, "RSV_lsb", rsv_lsb, 8);
                printField(oss, "RSV_msb", rsv_msb, 10);
                printFieldHex(oss, "ObuLabel", obuLabel, 10);
                printField(oss, "RSV1", rsv1, 6);
                break;
            }
            case MESSAGE_CODE::CARPARK_PROCESS_COMPLETE_RESULT:
            {
                if (body.size() < 20)
                {
                    Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=CARPARK_PROCESS_COMPLETE_RESULT | Reason=Body too short", logFileName_, "EEP");
                    break;
                }

                uint8_t result = body[0];
                uint32_t rsv_lsb = Common::getInstance()->FnReadUint32LE(body, 1);
                uint64_t rsv_msb = Common::getInstance()->FnReadUint56LE(body, 5);
                uint64_t obuLabel = Common::getInstance()->FnReadUint40BE(body, 12);
                uint32_t rsv1 = Common::getInstance()->FnReadUint24LE(body, 17);

                const std::unordered_map<uint8_t, std::string> resultMap = {
                    {0x00, "'Carpark Process Complete Notification' transmission is not successful due to connection timeout."},
                    {0x01, "'Carpark Process Complete Notification' transmission is successful"},
                    {0x02, "Parameter of the received 'Carpark Process Complete Notification' is incorrect. E.g Undefined 'Entry / Exit / Interlink processing Result'."}
                };

                printField(oss, "Result", result, 2, getFieldDescription(result, resultMap));
                printField(oss, "RSV_lsb", rsv_lsb, 8);
                printField(oss, "RSV_msb", rsv_msb, 14);
                printFieldHex(oss, "ObuLabel", obuLabel, 10);
                printField(oss, "RSV1", rsv1, 6);
                break;
            }
            case MESSAGE_CODE::DSRC_STATUS_RESPONSE:
            {
                if (body.size() < 100)
                {
                    Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=DSRC_STATUS_RESPONSE | Reason=Body too short", logFileName_, "EEP");
                    break;
                }

                uint8_t dsrcEmissionStatus = body[0];
                uint8_t presenceLoopStatus = body[1];
                uint8_t dsrcEmissionControl = body[2];
                uint8_t noOfWaveConnection = body[3];
                // OBU Communication status 1
                uint64_t OBU1_rsv = Common::getInstance()->FnReadUint64LE(body, 4);
                uint64_t OBU1_obulabel = Common::getInstance()->FnReadUint40BE(body, 12);
                uint32_t OBU1_rsv1 = Common::getInstance()->FnReadUint24LE(body, 17);
                uint32_t OBU1_elapsedTime = Common::getInstance()->FnReadUint32LE(body, 20);
                uint8_t OBU1_rssi = body[24];
                uint8_t OBU1_connectionStatus = body[25];
                uint16_t OBU1_rsv2 = Common::getInstance()->FnReadUint16LE(body, 26);
                // OBU Communication status 2
                uint64_t OBU2_rsv = Common::getInstance()->FnReadUint64LE(body, 28);
                uint64_t OBU2_obulabel = Common::getInstance()->FnReadUint40BE(body, 36);
                uint32_t OBU2_rsv1 = Common::getInstance()->FnReadUint24LE(body, 41);
                uint32_t OBU2_elapsedTime = Common::getInstance()->FnReadUint32LE(body, 44);
                uint8_t OBU2_rssi = body[48];
                uint8_t OBU2_connectionStatus = body[49];
                uint16_t OBU2_rsv2 = Common::getInstance()->FnReadUint16LE(body, 50);
                // OBU Communication status 3
                uint64_t OBU3_rsv = Common::getInstance()->FnReadUint64LE(body, 52);
                uint64_t OBU3_obulabel = Common::getInstance()->FnReadUint40BE(body, 60);
                uint32_t OBU3_rsv1 = Common::getInstance()->FnReadUint24LE(body, 65);
                uint32_t OBU3_elapsedTime = Common::getInstance()->FnReadUint32LE(body, 68);
                uint8_t OBU3_rssi = body[72];
                uint8_t OBU3_connectionStatus = body[73];
                uint16_t OBU3_rsv2 = Common::getInstance()->FnReadUint16LE(body, 74);
                // OBU Communication status 4
                uint64_t OBU4_rsv = Common::getInstance()->FnReadUint64LE(body, 76);
                uint64_t OBU4_obulabel = Common::getInstance()->FnReadUint40BE(body, 84);
                uint32_t OBU4_rsv1 = Common::getInstance()->FnReadUint24LE(body, 89);
                uint32_t OBU4_elapsedTime = Common::getInstance()->FnReadUint32LE(body, 92);
                uint8_t OBU4_rssi = body[96];
                uint8_t OBU4_connectionStatus = body[97];
                uint16_t OBU4_rsv2 = Common::getInstance()->FnReadUint16LE(body, 98);

                const std::unordered_map<uint8_t, std::string> dsrcEmissionStatusMap = {
                    {0x00, "EEP is not transmitting"},
                    {0x01, "EEP is transmitting"}
                };

                const std::unordered_map<uint8_t, std::string> presenceLoopStatusMap = {
                    {0x00, "Presence Loop Off"},
                    {0x01, "Presence Loop ON"}
                };

                const std::unordered_map<uint8_t, std::string> dsrcEmissionControlMap = {
                    {0x00, "Loop triggered mode"},
                    {0x01, "Continuous emission mode"}
                };

                const std::unordered_map<uint8_t, std::string> connectionStatusMap = {
                    {0x00, "No connection"},
                    {0x01, "Connected"},
                    {0x02, "Connected and after completion notice"},
                    {0x03, "Connected and EEP is distributing related data"},
                    {0x04, "Connected and EEP is suspending to distributing the related data due to multi connection"},
                };

                printField(oss, "DSRC emission status", dsrcEmissionStatus, 2, getFieldDescription(dsrcEmissionStatus, dsrcEmissionStatusMap));
                printField(oss, "Presence Loop status", presenceLoopStatus, 2, getFieldDescription(presenceLoopStatus, presenceLoopStatusMap));
                printField(oss, "DSRC emission control", dsrcEmissionControl, 2, getFieldDescription(dsrcEmissionControl, dsrcEmissionControlMap));
                printField(oss, "Number of WAVE connection", noOfWaveConnection, 2, " Value range is 0 to 4");
                
                printField(oss, "OBU1_RSV", OBU1_rsv, 16);
                printFieldHex(oss, "OBU1_ObuLabel", OBU1_obulabel, 10);
                printField(oss, "OBU1_RSV1", OBU1_rsv1, 6);
                printField(oss, "OBU1_Elapsed time since connection established", OBU1_elapsedTime, 8, " milliseconds");
                printField(oss, "OBU1_RSSI", OBU1_rssi, 2, " RSSI value [dBm] = (RSSI) - 100");
                printField(oss, "OBU1_Connection status", OBU1_connectionStatus, 2, getFieldDescription(OBU1_connectionStatus, connectionStatusMap));
                printField(oss, "OBU1_RSV2", OBU1_rsv1, 6);

                printField(oss, "OBU2_RSV", OBU2_rsv, 16);
                printFieldHex(oss, "OBU2_ObuLabel", OBU2_obulabel, 10);
                printField(oss, "OBU2_RSV1", OBU2_rsv1, 6);
                printField(oss, "OBU2_Elapsed time since connection established", OBU2_elapsedTime, 8, " milliseconds");
                printField(oss, "OBU2_RSSI", OBU2_rssi, 2, " RSSI value [dBm] = (RSSI) - 100");
                printField(oss, "OBU2_Connection status", OBU2_connectionStatus, 2, getFieldDescription(OBU1_connectionStatus, connectionStatusMap));
                printField(oss, "OBU2_RSV2", OBU2_rsv1, 6);

                printField(oss, "OBU3_RSV", OBU3_rsv, 16);
                printFieldHex(oss, "OBU3_ObuLabel", OBU3_obulabel, 10);
                printField(oss, "OBU3_RSV1", OBU3_rsv1, 6);
                printField(oss, "OBU3_Elapsed time since connection established", OBU3_elapsedTime, 8, " milliseconds");
                printField(oss, "OBU3_RSSI", OBU3_rssi, 2, " RSSI value [dBm] = (RSSI) - 100");
                printField(oss, "OBU3_Connection status", OBU3_connectionStatus, 2, getFieldDescription(OBU1_connectionStatus, connectionStatusMap));
                printField(oss, "OBU3_RSV2", OBU3_rsv1, 6);

                printField(oss, "OBU4_RSV", OBU4_rsv, 16);
                printFieldHex(oss, "OBU4_ObuLabel", OBU4_obulabel, 10);
                printField(oss, "OBU4_RSV1", OBU4_rsv1, 6);
                printField(oss, "OBU4_Elapsed time since connection established", OBU4_elapsedTime, 8, " milliseconds");
                printField(oss, "OBU4_RSSI", OBU4_rssi, 2, " RSSI value [dBm] = (RSSI) - 100");
                printField(oss, "OBU4_Connection status", OBU4_connectionStatus, 2, getFieldDescription(OBU1_connectionStatus, connectionStatusMap));
                printField(oss, "OBU4_RSV2", OBU4_rsv1, 6);
                break;
            }
            case MESSAGE_CODE::TIME_CALIBRATION_RESPONSE:
            {
                if (body.size() < 4)
                {
                    Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=TIME_CALIBRATION_RESPONSE | Reason=Body too short", logFileName_, "EEP");
                    break;
                }

                uint8_t result = body[0];
                uint32_t rsv = Common::getInstance()->FnReadUint24LE(body, 1);

                const std::unordered_map<uint8_t, std::string> resultMap = {
                    {0x00, "EEP Time calibration successes"},
                    {0x01, "EEP Time calibration fails due to time calibration which triggered by OPC/TS is not allowed by EEP configuration parameter"},
                    {0x02, "EEP Time calibration fails due to EEP is under critical processing such as deduct process"},
                };

                printField(oss, "Result", result, 2, getFieldDescription(result, resultMap));
                printField(oss, "RSV", rsv, 6);
                break;
            }
            case MESSAGE_CODE::EEP_RESTART_INQUIRY:
            {
                if (body.size() < 4)
                {
                    Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=EEP_RESTART_INQUIRY_RESPONSE | Reason=Body too short", logFileName_, "EEP");
                    break;
                }

                uint8_t typeOfRestart = body[0];
                uint8_t responseDeadline = body[1];
                uint8_t maxRetryCount = body[2];
                uint8_t retryCounter = body[3];

                const std::unordered_map<uint8_t, std::string> typeOfRestartMap = {
                    {0x00, "EEP restarts forcibly"},
                    {0x01, "EEP restarts after OPC/TS notify permission of restart by 'EEP restart inquiry response' message"}
                };

                printField(oss, "Type of Restart", typeOfRestart, 2, getFieldDescription(typeOfRestart, typeOfRestartMap));
                printField(oss, "Response Deadline", responseDeadline, 2, " Range of value : 1 - 10 [Sec]");
                printField(oss, "Max Retry Count", maxRetryCount, 2);
                printField(oss, "Retry Counter", retryCounter, 2);
                break;
            }
            case MESSAGE_CODE::NOTIFICATION_LOG:
            {
                if (body.size() < 12)
                {
                    Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=NOTIFICATION_LOG | Reason=Body too short", logFileName_, "EEP");
                    break;
                }

                uint8_t day = body[0];
                uint8_t month = body[1];
                uint16_t year = Common::getInstance()->FnReadUint16LE(body, 2);
                uint8_t rsv = body[4];
                uint8_t second = body[5];
                uint8_t minute = body[6];
                uint8_t hour = body[7];
                uint8_t notificationType = body[8];
                uint8_t errorCode = body[9];
                uint16_t rsv1 = Common::getInstance()->FnReadUint16LE(body, 10);

                const std::unordered_map<uint8_t, std::string> notificationTypeMap = {
                    {0x00, "Error Occurrence"},
                    {0x01, "Error Recovery"}
                };

                const std::unordered_map<uint8_t, std::string> errorCodeMap = {
                    {0x01, "Main power down"},
                    {0x02, "Main power is low voltage"},
                    {0x03, "Low Internal battery"},
                    {0x10, "Abnormal temperature in housing"},
                    {0x11, "Forced shutdown due to abnormal temperature"},
                    {0x12, "Housing is open"},
                    {0x20, "DSRC is unavailable"},
                    {0x21, "Cellular network failure"},
                    {0x22, "USB bus failure"},
                    {0x23, "IO board failure"},
                    {0x24, "Ethernet failure"},
                    {0x25, "RS-232C failure"},
                    {0x26, "No GNSS signal"},
                    {0x40, "Out of storage space"},
                    {0x41, "Table Error"}
                };

                printField(oss, "Day", day, 2);
                printField(oss, "Month", month, 2);
                printField(oss, "Year", year, 4);
                printField(oss, "RSV", rsv, 2);
                printField(oss, "Second", second, 2);
                printField(oss, "Minute", minute, 2);
                printField(oss, "Hour", hour, 2);
                printField(oss, "Notification Type", notificationType, 2, getFieldDescription(notificationType, notificationTypeMap));
                printField(oss, "Error Code", errorCode, 2, getFieldDescription(errorCode, errorCodeMap));
                printField(oss, "RSV1", rsv1, 4);
                break;
            }
        }
    }
    else if (body.size() != header.dataLen_)
    {
        oss << "  ParseWarning | Reason=Body length mismatch | HeaderDataLen=" << header.dataLen_
            << " | ActualBodyLen=" << body.size() << '\n';
    }

    Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");
}

void EEPClient::handleParsedResponseMessage(const MessageHeader& header, const std::vector<uint8_t>& body, std::string& eventMsg, const std::vector<uint8_t>& data)
{
    std::ostringstream oss;
    MESSAGE_CODE code = static_cast<MESSAGE_CODE>(header.dataTypeCode_);
    EEPEventWrapper eepEvt;
    bool ret = true;

    switch (code)
    {
        case MESSAGE_CODE::ACK:
        {
            if (body.size() < 4)
            {
                Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=ACK | Reason=Body too short", logFileName_, "EEP");
                break;
            }

            // Special handle for deduct req ack, need extra payload for deduct serial number
            if (getCurrentCmdRequested().type == CommandType::DEDUCT_REQ_CMD)
            {
                ackDeduct ackDeductData{};

                ackDeductData.resultCode = body[0];
                ackDeductData.rsv = Common::getInstance()->FnReadUint24LE(body, 1);
                ackDeductData.deductSerialNo = getDeductSerialFromCurrentRequest();

                // Serialization
                boost::json::value jv = ackDeductData.to_json();

                eepEvt.commandReqType = static_cast<uint8_t>(getCurrentCmdRequested().type);
                eepEvt.messageCode = static_cast<uint8_t>(MESSAGE_CODE::ACK);
                eepEvt.messageStatus = static_cast<uint32_t>(MSG_STATUS::SUCCESS);
                eepEvt.payload = boost::json::serialize(jv);
            }
            else
            {
                ack ackdata{};

                ackdata.resultCode = body[0];
                ackdata.rsv = Common::getInstance()->FnReadUint24LE(body, 1);

                // Serialization
                boost::json::value jv = ackdata.to_json();

                eepEvt.commandReqType = static_cast<uint8_t>(getCurrentCmdRequested().type);
                eepEvt.messageCode = static_cast<uint8_t>(MESSAGE_CODE::ACK);
                eepEvt.messageStatus = static_cast<uint32_t>(MSG_STATUS::SUCCESS);
                eepEvt.payload = boost::json::serialize(jv);
            }

            break;
        }
        case MESSAGE_CODE::NAK:
        {
            if (body.size() < 4)
            {
                Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=NAK | Reason=Body too short", logFileName_, "EEP");
                break;
            }

            nak nakdata{};

            nakdata.requestDataTypeCode = body[0];
            nakdata.reasonCode = body[1];
            nakdata.rsv = Common::getInstance()->FnReadUint16LE(body, 2);

            // Serialization
            boost::json::value jv = nakdata.to_json();

            eepEvt.commandReqType = static_cast<uint8_t>(getCurrentCmdRequested().type);
            eepEvt.messageCode = static_cast<uint8_t>(MESSAGE_CODE::NAK);
            eepEvt.messageStatus = static_cast<uint32_t>(MSG_STATUS::SUCCESS);
            eepEvt.payload = boost::json::serialize(jv);

            break;
        }
        case MESSAGE_CODE::WATCHDOG_RESPONSE:
        {
            // Reset the missed watchdog response count
            watchdogMissedRspCount_ = 0;

            eepEvt.commandReqType = static_cast<uint8_t>(getCurrentCmdRequested().type);
            eepEvt.messageCode = static_cast<uint8_t>(MESSAGE_CODE::WATCHDOG_RESPONSE);
            eepEvt.messageStatus = static_cast<uint32_t>(MSG_STATUS::SUCCESS);
            eepEvt.payload = "{}";

            // No need to raise event
            ret = false;

            break;
        }
        case MESSAGE_CODE::HEALTH_STATUS_RESPONSE:
        {
            if (body.size() < 64)
            {
                Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=HEALTH_STATUS_RESPONSE | Reason=Body too short", logFileName_, "EEP");
                break;
            }

            healthStatus hs{};

            hs.subSystemLabel = Common::getInstance()->FnReadUint32LE(body, 0);
            hs.equipmentType = body[4];
            hs.EEPDSRCDeviceId = body[5];
            hs.carparkId = Common::getInstance()->FnReadUint16LE(body, 6);
            hs.lifeCycleMode = body[8];
            hs.maintenanceMode = body[9];
            hs.operationStatus = body[10];
            hs.esimStatus = body[11];
            hs.powerStatus = body[12];
            hs.internalBatteryStatus = body[13];
            hs.usbBusStatus = body[14];
            hs.dioStatus = body[15];
            hs.securityKeyStatus = body[16];
            hs.doorSwitchStatus = body[17];
            hs.temperatureStatus = body[18];
            hs.temperatureInHousing = body[19];
            hs.gnssSignalQuality = body[20];
            hs.numberOfGnssStatelites = body[21];
            hs.timeSyncStatus = body[22];
            hs.wwanConnectionStatus = body[23];
            hs.DSRCEmissionMode = body[24];
            hs.numberOfDSRCSession = body[25];
            hs.rsv = body[26];
            hs.rsv1 = body[27];
            hs.cpuLoad = body[28];
            hs.storageSpace = body[29];
            hs.rsv2 = Common::getInstance()->FnReadUint16LE(body, 30);
            // version is CHAR data type based on protocol, so already big endian, no need to reverse
            hs.version.assign(body.begin() + 32, body.begin() + 40);
            hs.cscBlocklistTable1 = Common::getInstance()->FnReadUint32LE(body, 40);
            hs.cscBlocklistTable2 = Common::getInstance()->FnReadUint32LE(body, 42);
            hs.cscBlocklistTable3 = Common::getInstance()->FnReadUint32LE(body, 44);
            hs.cscBlocklistTable4 = Common::getInstance()->FnReadUint32LE(body, 46);
            hs.cscIssuerListTable1 = Common::getInstance()->FnReadUint32LE(body, 48);
            hs.cscIssuerListTable2 = Common::getInstance()->FnReadUint32LE(body, 50);
            hs.cscIssuerListTable3 = Common::getInstance()->FnReadUint32LE(body, 52);
            hs.cscIssuerListTable4 = Common::getInstance()->FnReadUint32LE(body, 54);
            hs.cscConfigurationParamVersion = body[56];
            hs.rsv3 = Common::getInstance()->FnReadUint24LE(body, 57);
            hs.backendPaymentTerminationListversion = Common::getInstance()->FnReadUint24LE(body, 60);
            hs.rsv4 = body[63];

            // Assign the carpark ID
            if (hs.carparkId != eepCarparkID_)
            {
                eepCarparkID_ = hs.carparkId;
            }

            // Serialization
            boost::json::value jv = hs.to_json();

            eepEvt.commandReqType = static_cast<uint8_t>(getCurrentCmdRequested().type);
            eepEvt.messageCode = static_cast<uint8_t>(MESSAGE_CODE::HEALTH_STATUS_RESPONSE);
            eepEvt.messageStatus = static_cast<uint32_t>(MSG_STATUS::SUCCESS);
            eepEvt.payload = boost::json::serialize(jv);

            // No need to raise event
            ret = false;

            // Store the health status inside the local vector and pending getter function to return
            {
                std::lock_guard<std::mutex> lock(statusDataMutex_);
                status_data_ = data;
            }

            break;
        }
        case MESSAGE_CODE::START_RESPONSE:
        {
            if (body.size() < 4)
            {
                Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=START_RESPONSE | Reason=Body too short", logFileName_, "EEP");
                break;
            }

            startResponse sr;

            sr.resultCode = body[0];
            sr.rsv = Common::getInstance()->FnReadUint24LE(body, 1);

            // Serialization
            boost::json::value jv = sr.to_json();

            eepEvt.commandReqType = static_cast<uint8_t>(getCurrentCmdRequested().type);
            eepEvt.messageCode = static_cast<uint8_t>(MESSAGE_CODE::START_RESPONSE);
            eepEvt.messageStatus = static_cast<uint32_t>(MSG_STATUS::SUCCESS);
            eepEvt.payload = boost::json::serialize(jv);

            break;
        }
        case MESSAGE_CODE::STOP_RESPONSE:
        {
            if (body.size() < 4)
            {
                Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=STOP_RESPONSE | Reason=Body too short", logFileName_, "EEP");
                break;
            }

            stopResponse sr{};

            sr.resultCode = body[0];
            sr.rsv = Common::getInstance()->FnReadUint24LE(body, 1);

            // Serialization
            boost::json::value jv = sr.to_json();

            eepEvt.commandReqType = static_cast<uint8_t>(getCurrentCmdRequested().type);
            eepEvt.messageCode = static_cast<uint8_t>(MESSAGE_CODE::STOP_RESPONSE);
            eepEvt.messageStatus = static_cast<uint32_t>(MSG_STATUS::SUCCESS);
            eepEvt.payload = boost::json::serialize(jv);

            break;
        }
        case MESSAGE_CODE::DI_STATUS_RESPONSE:
        {
            if (body.size() < 8)
            {
                Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=DI_STATUS | Reason=Body too short", logFileName_, "EEP");
                break;
            }

            diStatusResponse dsr{};

            dsr.currDISignal = Common::getInstance()->FnReadUint32LE(body, 0);
            dsr.prevDISignal = Common::getInstance()->FnReadUint32LE(body, 4);

            // Serialization
            boost::json::value jv = dsr.to_json();

            eepEvt.commandReqType = static_cast<uint8_t>(getCurrentCmdRequested().type);
            eepEvt.messageCode = static_cast<uint8_t>(MESSAGE_CODE::DI_STATUS_RESPONSE);
            eepEvt.messageStatus = static_cast<uint32_t>(MSG_STATUS::SUCCESS);
            eepEvt.payload = boost::json::serialize(jv);

            break;
        }
        case MESSAGE_CODE::DSRC_STATUS_RESPONSE:
        {
            if (body.size() < 100)
            {
                Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=DSRC_STATUS_RESPONSE | Reason=Body too short", logFileName_, "EEP");
                break;
            }

            dsrcStatusResponse dsr{};

            dsr.dsrcEmissionStatus = body[0];
            dsr.presenceLoopStatus = body[1];
            dsr.dsrcEmissionControl = body[2];
            dsr.noOfWaveConnection = body[3];
            // OBU Communication status 1
            dsr.OBU1_rsv = Common::getInstance()->FnReadUint64LE(body, 4);
            dsr.OBU1_obulabel = Common::getInstance()->FnReadUint40BE(body, 12);
            dsr.OBU1_rsv1 = Common::getInstance()->FnReadUint24LE(body, 17);
            dsr.OBU1_elapsedTime = Common::getInstance()->FnReadUint32LE(body, 20);
            dsr.OBU1_rssi = body[24];
            dsr.OBU1_connectionStatus = body[25];
            dsr.OBU1_rsv2 = Common::getInstance()->FnReadUint16LE(body, 26);
            // OBU Communication status 2
            dsr.OBU2_obulabel = Common::getInstance()->FnReadUint40BE(body, 36);
            dsr.OBU2_rsv1 = Common::getInstance()->FnReadUint24LE(body, 41);
            dsr.OBU2_elapsedTime = Common::getInstance()->FnReadUint32LE(body, 44);
            dsr.OBU2_rssi = body[48];
            dsr.OBU2_connectionStatus = body[49];
            dsr.OBU2_rsv2 = Common::getInstance()->FnReadUint16LE(body, 50);
            // OBU Communication status 3
            dsr.OBU3_rsv = Common::getInstance()->FnReadUint64LE(body, 52);
            dsr.OBU3_obulabel = Common::getInstance()->FnReadUint40BE(body, 60);
            dsr.OBU3_rsv1 = Common::getInstance()->FnReadUint24LE(body, 65);
            dsr.OBU3_elapsedTime = Common::getInstance()->FnReadUint32LE(body, 68);
            dsr.OBU3_rssi = body[72];
            dsr.OBU3_connectionStatus = body[73];
            dsr.OBU3_rsv2 = Common::getInstance()->FnReadUint16LE(body, 74);
            // OBU Communication status 4
            dsr.OBU4_rsv = Common::getInstance()->FnReadUint64LE(body, 76);
            dsr.OBU4_obulabel = Common::getInstance()->FnReadUint40BE(body, 84);
            dsr.OBU4_rsv1 = Common::getInstance()->FnReadUint24LE(body, 89);
            dsr.OBU4_elapsedTime = Common::getInstance()->FnReadUint32LE(body, 92);
            dsr.OBU4_rssi = body[96];
            dsr.OBU4_connectionStatus = body[97];
            dsr.OBU4_rsv2 = Common::getInstance()->FnReadUint16LE(body, 98);

            // Serialization
            boost::json::value jv = dsr.to_json();

            eepEvt.commandReqType = static_cast<uint8_t>(getCurrentCmdRequested().type);
            eepEvt.messageCode = static_cast<uint8_t>(MESSAGE_CODE::DSRC_STATUS_RESPONSE);
            eepEvt.messageStatus = static_cast<uint32_t>(MSG_STATUS::SUCCESS);
            eepEvt.payload = boost::json::serialize(jv);

            break;
        }
        case MESSAGE_CODE::TIME_CALIBRATION_RESPONSE:
        {
            if (body.size() < 4)
            {
                Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=TIME_CALIBRATION_RESPONSE | Reason=Body too short", logFileName_, "EEP");
                break;
            }

            timeCalibrationResponse tcr{};

            tcr.result = body[0];
            tcr.rsv = Common::getInstance()->FnReadUint24LE(body, 1);

            // Serialization
            boost::json::value jv = tcr.to_json();

            eepEvt.commandReqType = static_cast<uint8_t>(getCurrentCmdRequested().type);
            eepEvt.messageCode = static_cast<uint8_t>(MESSAGE_CODE::TIME_CALIBRATION_RESPONSE);
            eepEvt.messageStatus = static_cast<uint32_t>(MSG_STATUS::SUCCESS);
            eepEvt.payload = boost::json::serialize(jv);

            break;
        }
        default:
        {
            ret = false;
            Logger::getInstance()->FnLog("EEP: [RX] Parse failed | Reason=Unsupported data type", logFileName_, "EEP");
            break;
        }
    }

    if (ret)
    {
        eventMsg = boost::json::serialize(eepEvt.to_json());
    }
}

void EEPClient::handleParsedNotificationMessage(const MessageHeader& header, const std::vector<uint8_t>& body, std::string& eventMsg)
{
    std::ostringstream oss;
    MESSAGE_CODE code = static_cast<MESSAGE_CODE>(header.dataTypeCode_);
    EEPEventWrapper eepEvt;
    bool ret = true;

    switch (code)
    {
        case MESSAGE_CODE::DI_STATUS_NOTIFICATION:
        {
            if (body.size() < 8)
            {
                Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=DI_STATUS | Reason=Body too short", logFileName_, "EEP");
                break;
            }

            diStatusNotification dsn{};

            dsn.currDISignal = Common::getInstance()->FnReadUint32LE(body, 0);
            dsn.prevDISignal = Common::getInstance()->FnReadUint32LE(body, 4);

            // Serialization
            boost::json::value jv = dsn.to_json();

            eepEvt.commandReqType = static_cast<uint8_t>(CommandType::UNKNOWN_REQ_CMD);
            eepEvt.messageCode = static_cast<uint8_t>(MESSAGE_CODE::DI_STATUS_NOTIFICATION);
            eepEvt.messageStatus = static_cast<uint32_t>(MSG_STATUS::SUCCESS);
            eepEvt.payload = boost::json::serialize(jv);

            break;
        }
        case MESSAGE_CODE::OBU_INFORMATION_NOTIFICATION:
        {
            if (body.size() < 56)
            {
                Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=OBU_INFORMATION_NOTIFICATION | Reason=Body too short", logFileName_, "EEP");
                break;
            }

            obuInformationNotification obuin{};

            obuin.rsv = Common::getInstance()->FnReadUint64LE(body, 0);
            obuin.obulabel = Common::getInstance()->FnReadUint40BE(body, 8);
            obuin.typeObu = body[13];
            obuin.rsv1 = Common::getInstance()->FnReadUint16LE(body, 14);
            obuin.vcc = Common::getInstance()->FnReadUint24BE(body, 16);
            obuin.rsv2 = body[19];
            // Vehicle number is CHAR data type based on protocol, so already big endian, no need to reverse
            obuin.vechicleNumber.assign(body.begin() + 20, body.begin() + 33);
            obuin.rsv3 = Common::getInstance()->FnReadUint24LE(body, 33);
            obuin.cardValidity = body[36];
            obuin.rsv4 = Common::getInstance()->FnReadUint24LE(body, 37);
            // CAN is CHAR data type based on protocol, so already big endian, no need to reverse
            obuin.can.assign(body.begin() + 40, body.begin() + 48);
            obuin.cardBalance = Common::getInstance()->FnReadUint32LE(body, 48);
            obuin.backendAccount = body[52];
            obuin.backendSetting = body[53];
            obuin.businessFunctionStatus = body[54];
            obuin.rsv5 = body[55];

            // Serialization
            boost::json::value jv = obuin.to_json();

            auto currentCmd = getCurrentCmdRequested();
            CommandType cmdType = 
                (currentCmd.type == CommandType::GET_OBU_INFO_REQ_CMD)
                    ? currentCmd.type 
                    : CommandType::UNKNOWN_REQ_CMD;
            eepEvt.commandReqType = static_cast<uint8_t>(cmdType);
            eepEvt.messageCode = static_cast<uint8_t>(MESSAGE_CODE::OBU_INFORMATION_NOTIFICATION);
            eepEvt.messageStatus = static_cast<uint32_t>(MSG_STATUS::SUCCESS);
            eepEvt.payload = boost::json::serialize(jv);
           
            break;
        }
        case MESSAGE_CODE::TRANSACTION_DATA:
        {
            if (body.size() < 340)
            {
                Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=TRANSACTION_DATA | Reason=Body too short", logFileName_, "EEP");
                break;
            }

            transactionData td{};

            td.deductCommandSerialNum = Common::getInstance()->FnReadUint16LE(body, 0);
            td.protocolVer = body[2];
            td.resultDeduction = body[3];
            td.subSystemLabel = Common::getInstance()->FnReadUint32LE(body, 4);
            td.rsv = Common::getInstance()->FnReadUint64LE(body, 8);
            td.obuLabel = Common::getInstance()->FnReadUint40BE(body, 16);
            td.rsv1 = Common::getInstance()->FnReadUint24LE(body, 21);
            // Vehicle number is CHAR data type based on protocol, so already big endian, no need to reverse
            td.vechicleNumber.assign(body.begin() + 24, body.begin() + 37);
            td.rsv2 = Common::getInstance()->FnReadUint24LE(body, 37);
            td.transactionRoute = body[40];
            td.rsv3 = body[41];
            td.frontendPaymentViolation = body[42];
            td.backendPaymentViolation = body[43];
            td.transactionType = body[44];
            td.rsv4 = Common::getInstance()->FnReadUint24LE(body, 45);
            td.parkingStartDay = body[48];
            td.parkingStartMonth = body[49];
            td.parkingStartYear = Common::getInstance()->FnReadUint16LE(body, 50);
            td.rsv5 = body[52];
            td.parkingStartSecond = body[53];
            td.parkingStartMinute = body[54];
            td.parkingStartHour = body[55];
            td.parkingEndDay = body[56];
            td.parkingEndMonth = body[57];
            td.parkingEndYear = Common::getInstance()->FnReadUint16LE(body, 58);
            td.rsv6 = body[60];
            td.parkingEndSecond = body[61];
            td.parkingEndMinute = body[62];
            td.parkingEndHour = body[63];
            td.paymentFee = Common::getInstance()->FnReadUint32LE(body, 64);
            td.fepTime = Common::getInstance()->FnReadUint56BE(body, 68);
            td.rsv7 = body[75];
            td.trp = Common::getInstance()->FnReadUint32BE(body, 76);
            td.indicationLastAutoLoad = body[80];
            td.rsv8 = Common::getInstance()->FnReadUint24LE(body, 81);
            // CAN is CHAR data type based on protocol, so already big endian, no need to reverse
            td.can.assign(body.begin() + 84, body.begin() + 92);
            td.lastCreditTransactionHeader = Common::getInstance()->FnReadUint64BE(body, 92);
            td.lastCreditTransactionTRP = Common::getInstance()->FnReadUint32BE(body, 100);
            td.purseBalanceBeforeTransaction = Common::getInstance()->FnReadUint32LE(body, 104);
            td.badDebtCounter = body[108];
            td.transactionStatus = body[109];
            td.debitOption = body[110];
            td.rsv9 = body[111];
            td.autoLoadAmount = Common::getInstance()->FnReadUint32LE(body, 112);
            td.counterData = Common::getInstance()->FnReadUint64BE(body, 116);
            td.signedCertificate = Common::getInstance()->FnReadUint64BE(body, 124);
            td.purseBalanceAfterTransaction = Common::getInstance()->FnReadUint32LE(body, 132);
            td.lastTransactionDebitOptionbyte = body[136];
            td.rsv10 = Common::getInstance()->FnReadUint24LE(body, 137);
            td.previousTransactionHeader = Common::getInstance()->FnReadUint64BE(body, 140);
            td.previousTRP = Common::getInstance()->FnReadUint32BE(body, 148);
            td.previousPurseBalance = Common::getInstance()->FnReadUint32LE(body, 152);
            td.previousCounterData = Common::getInstance()->FnReadUint64BE(body, 156);
            td.previousTransactionSignedCertificate = Common::getInstance()->FnReadUint64BE(body, 164);
            td.previousPurseStatus = body[172];
            td.rsv11 = Common::getInstance()->FnReadUint24LE(body, 173);
            td.bepPaymentFeeAmount = Common::getInstance()->FnReadUint16LE(body, 176);
            td.rsv12 = Common::getInstance()->FnReadUint16LE(body, 178);
            // Bed Time Of Report is CHAR data type based on protocol, so already big endian, no need to reverse
            td.bepTimeOfReport.assign(body.begin() + 180, body.begin() + 189);
            td.rsv13 = Common::getInstance()->FnReadUint24LE(body, 189);
            td.chargeReportCounter = Common::getInstance()->FnReadUint32LE(body, 192);
            td.bepKeyVersion = body[196];
            td.rsv14 = Common::getInstance()->FnReadUint24LE(body, 197);
            td.lenOfBepCertificate = body[200];
            size_t safeLen = std::min((int)td.lenOfBepCertificate, 139);
            size_t endIndex = 201 + safeLen;
            std::vector<uint8_t> tempBepCert(safeLen, 0);
            std::string tempHexBepCert = "";
            std::copy(body.begin() + 201, body.begin() + endIndex, tempBepCert.begin());
            tempHexBepCert = Common::getInstance()->FnConvertVectorUint8ToUpperCaseHexString(tempBepCert);
            td.bepCertificate.assign(tempHexBepCert.begin(), tempHexBepCert.end());

            // Serialization
            boost::json::value jv = td.to_json();

            auto currentCmd = getCurrentCmdRequested();
            CommandType cmdType = 
                (currentCmd.type == CommandType::DEDUCT_REQ_CMD || 
                currentCmd.type == CommandType::TRANSACTION_REQ_CMD)
                    ? currentCmd.type 
                    : CommandType::UNKNOWN_REQ_CMD;
            eepEvt.commandReqType = static_cast<uint8_t>(cmdType);
            eepEvt.messageCode = static_cast<uint8_t>(MESSAGE_CODE::TRANSACTION_DATA);
            eepEvt.messageStatus = static_cast<uint32_t>(MSG_STATUS::SUCCESS);
            eepEvt.payload = boost::json::serialize(jv);

            // Process frontend transaction
            if (td.resultDeduction == 0x01)
            {
                processDSRCFeTx(header, td);
            }
            // Process backend transaction
            else if (td.resultDeduction == 0x02)
            {
                processDSRCBeTx(header, td);
            }

            break;
        }
        case MESSAGE_CODE::CPO_INFORMATION_DISPLAY_RESULT:
        {
            if (body.size() < 20)
            {
                Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=CPO_INFORMATION_DISPLAY_RESULT | Reason=Body too short", logFileName_, "EEP");
                break;
            }

            cpoInformationDisplayResult cpoids{};

            cpoids.sequenceNoOfRequestMsg = Common::getInstance()->FnReadUint16LE(body, 0);
            cpoids.result = body[2];
            cpoids.rsv_lsb = Common::getInstance()->FnReadUint32LE(body, 3);
            cpoids.rsv_msb = Common::getInstance()->FnReadUint64LE(body, 7);
            cpoids.obuLabel = Common::getInstance()->FnReadUint40BE(body, 12);
            cpoids.rsv1 = Common::getInstance()->FnReadUint24LE(body, 17);

            // Serialization
            boost::json::value jv = cpoids.to_json();

            auto currentCmd = getCurrentCmdRequested();
            CommandType cmdType = 
                (currentCmd.type == CommandType::CPO_INFO_DISPLAY_REQ_CMD)
                    ? currentCmd.type 
                    : CommandType::UNKNOWN_REQ_CMD;
            eepEvt.commandReqType = static_cast<uint8_t>(cmdType);
            eepEvt.messageCode = static_cast<uint8_t>(MESSAGE_CODE::CPO_INFORMATION_DISPLAY_RESULT);
            eepEvt.messageStatus = static_cast<uint32_t>(MSG_STATUS::SUCCESS);
            eepEvt.payload = boost::json::serialize(jv);

            break;
        }
        case MESSAGE_CODE::CARPARK_PROCESS_COMPLETE_RESULT:
        {
            if (body.size() < 20)
            {
                Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=CARPARK_PROCESS_COMPLETE_RESULT | Reason=Body too short", logFileName_, "EEP");
                break;
            }

            carparkProcessCompleteResult cpcr{};

            cpcr.result = body[0];
            cpcr.rsv_lsb = Common::getInstance()->FnReadUint32LE(body, 1);
            cpcr.rsv_msb = Common::getInstance()->FnReadUint56LE(body, 5);
            cpcr.obuLabel = Common::getInstance()->FnReadUint40BE(body, 12);
            cpcr.rsv1 = Common::getInstance()->FnReadUint24LE(body, 17);

            // Serialization
            boost::json::value jv = cpcr.to_json();

            auto currentCmd = getCurrentCmdRequested();
            CommandType cmdType = 
                (currentCmd.type == CommandType::CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD)
                    ? currentCmd.type 
                    : CommandType::UNKNOWN_REQ_CMD;
            eepEvt.commandReqType = static_cast<uint8_t>(cmdType);
            eepEvt.messageCode = static_cast<uint8_t>(MESSAGE_CODE::CARPARK_PROCESS_COMPLETE_RESULT);
            eepEvt.messageStatus = static_cast<uint32_t>(MSG_STATUS::SUCCESS);
            eepEvt.payload = boost::json::serialize(jv);

            break;
        }
        
        case MESSAGE_CODE::EEP_RESTART_INQUIRY:
        {
            if (body.size() < 4)
            {
                Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=EEP_RESTART_INQUIRY | Reason=Body too short", logFileName_, "EEP");
                break;
            }

            eepRestartInquiry eepri{};

            eepri.typeOfRestart = body[0];
            eepri.responseDeadline = body[1];
            eepri.maxRetryCount = body[2];
            eepri.retryCounter = body[3];

            // Serialization
            boost::json::value jv = eepri.to_json();

            eepEvt.commandReqType = static_cast<uint8_t>(CommandType::UNKNOWN_REQ_CMD);
            eepEvt.messageCode = static_cast<uint8_t>(MESSAGE_CODE::EEP_RESTART_INQUIRY);
            eepEvt.messageStatus = static_cast<uint32_t>(MSG_STATUS::SUCCESS);
            eepEvt.payload = boost::json::serialize(jv);

            break;
        }
        case MESSAGE_CODE::NOTIFICATION_LOG:
        {
            if (body.size() < 12)
            {
                Logger::getInstance()->FnLog("EEP: [RX] Invalid payload | Code=NOTIFICATION_LOG | Reason=Body too short", logFileName_, "EEP");
                break;
            }

            notificationLog nl;

            nl.day = body[0];
            nl.month = body[1];
            nl.year = Common::getInstance()->FnReadUint16LE(body, 2);
            nl.rsv = body[4];
            nl.second = body[5];
            nl.minute = body[6];
            nl.hour = body[7];
            nl.notificationType = body[8];
            nl.errorCode = body[9];
            nl.rsv1 = Common::getInstance()->FnReadUint16LE(body, 10);

            // Serialization
            boost::json::value jv = nl.to_json();

            eepEvt.commandReqType = static_cast<uint8_t>(CommandType::UNKNOWN_REQ_CMD);
            eepEvt.messageCode = static_cast<uint8_t>(MESSAGE_CODE::NOTIFICATION_LOG);
            eepEvt.messageStatus = static_cast<uint32_t>(MSG_STATUS::SUCCESS);
            eepEvt.payload = boost::json::serialize(jv);

            break;
        }
        default:
        {
            ret = false;
            Logger::getInstance()->FnLog("EEP: [RX] Parse failed | Reason=Unsupported data type", logFileName_, "EEP");
            break;
        }
    }

    if (ret)
    {
        eventMsg = boost::json::serialize(eepEvt.to_json());
    }
}

bool EEPClient::isValidSourceDestination(uint8_t source, uint8_t destination)
{
    return ((source == eepDestinationId_) && (destination == eepSourceId_));
}

bool EEPClient::isResponseMatchedDataTypeCode(Command cmd, const uint8_t& dataTypeCode_, const std::vector<uint8_t>& msgBody)
{
    static const std::unordered_map<CommandType, std::vector<uint8_t>> validResponses = {
        { CommandType::HEALTH_STATUS_REQ_CMD,                            {static_cast<uint8_t>(MESSAGE_CODE::HEALTH_STATUS_RESPONSE), static_cast<uint8_t>(MESSAGE_CODE::NAK)}       },
        { CommandType::WATCHDOG_REQ_CMD,                                 {static_cast<uint8_t>(MESSAGE_CODE::WATCHDOG_RESPONSE), static_cast<uint8_t>(MESSAGE_CODE::NAK)}            },
        { CommandType::START_REQ_CMD,                                    {static_cast<uint8_t>(MESSAGE_CODE::START_RESPONSE), static_cast<uint8_t>(MESSAGE_CODE::NAK)}               },
        { CommandType::STOP_REQ_CMD,                                     {static_cast<uint8_t>(MESSAGE_CODE::STOP_RESPONSE), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                },
        { CommandType::DI_REQ_CMD,                                       {static_cast<uint8_t>(MESSAGE_CODE::DI_STATUS_RESPONSE), static_cast<uint8_t>(MESSAGE_CODE::NAK)}           },
        { CommandType::DO_REQ_CMD,                                       {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::SET_DI_PORT_CONFIG_CMD,                           {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::GET_OBU_INFO_REQ_CMD,                             {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::GET_OBU_INFO_STOP_REQ_CMD,                        {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::DEDUCT_REQ_CMD,                                   {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::DEDUCT_STOP_REQ_CMD,                              {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::TRANSACTION_REQ_CMD,                              {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::CPO_INFO_DISPLAY_REQ_CMD,                         {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD,    {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::DSRC_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD,       {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::STOP_REQ_OF_RELATED_INFO_DISTRIBUTION_CMD,        {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::DSRC_STATUS_REQ_CMD,                              {static_cast<uint8_t>(MESSAGE_CODE::DSRC_STATUS_RESPONSE), static_cast<uint8_t>(MESSAGE_CODE::NAK)}         },
        { CommandType::TIME_CALIBRATION_REQ_CMD,                         {static_cast<uint8_t>(MESSAGE_CODE::TIME_CALIBRATION_RESPONSE), static_cast<uint8_t>(MESSAGE_CODE::NAK)}    },
        { CommandType::SET_CARPARK_AVAIL_REQ_CMD,                        {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::CD_DOWNLOAD_REQ_CMD,                              {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::EEP_RESTART_INQUIRY_REQ_CMD,                      {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          }
    };

    auto it = validResponses.find(cmd.type);
    if (it == validResponses.end())
    {
        return false;
    }

    // Step 1: is the top-level dataTypeCode_ valid for this command?
    if (std::find(it->second.begin(), it->second.end(), dataTypeCode_) == it->second.end())
    {
        return false;
    }

    static const std::unordered_map<CommandType, uint8_t> validAckNakRspRequestedDataType = {
        { CommandType::HEALTH_STATUS_REQ_CMD,                            static_cast<uint8_t>(MESSAGE_CODE::HEALTH_STATUS_REQUEST)                              },
        { CommandType::WATCHDOG_REQ_CMD,                                 static_cast<uint8_t>(MESSAGE_CODE::WATCHDOG_REQUEST)                                   },
        { CommandType::START_REQ_CMD,                                    static_cast<uint8_t>(MESSAGE_CODE::START_REQUEST)                                      },
        { CommandType::STOP_REQ_CMD,                                     static_cast<uint8_t>(MESSAGE_CODE::STOP_REQUEST)                                       },
        { CommandType::DI_REQ_CMD,                                       static_cast<uint8_t>(MESSAGE_CODE::DI_STATUS_REQUEST)                                  },
        { CommandType::DO_REQ_CMD,                                       static_cast<uint8_t>(MESSAGE_CODE::SET_DO_REQUEST)                                     },
        { CommandType::SET_DI_PORT_CONFIG_CMD,                           static_cast<uint8_t>(MESSAGE_CODE::SET_DI_PORT_CONFIGURATION)                          },
        { CommandType::GET_OBU_INFO_REQ_CMD,                             static_cast<uint8_t>(MESSAGE_CODE::GET_OBU_INFORMATION_REQUEST)                        },
        { CommandType::GET_OBU_INFO_STOP_REQ_CMD,                        static_cast<uint8_t>(MESSAGE_CODE::GET_OBU_INFORMATION_STOP)                           },
        { CommandType::DEDUCT_REQ_CMD,                                   static_cast<uint8_t>(MESSAGE_CODE::DEDUCT_REQUEST)                                     },
        { CommandType::DEDUCT_STOP_REQ_CMD,                              static_cast<uint8_t>(MESSAGE_CODE::DEDUCT_STOP_REQUEST)                                },
        { CommandType::TRANSACTION_REQ_CMD,                              static_cast<uint8_t>(MESSAGE_CODE::TRANSACTION_REQUEST)                                },
        { CommandType::CPO_INFO_DISPLAY_REQ_CMD,                         static_cast<uint8_t>(MESSAGE_CODE::CPO_INFORMATION_DISPLAY_REQUEST)                    },
        { CommandType::CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD,    static_cast<uint8_t>(MESSAGE_CODE::CARPARK_PROCESS_COMPLETE_NOTIFICATION)              },
        { CommandType::DSRC_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD,       static_cast<uint8_t>(MESSAGE_CODE::DSRC_PROCESS_COMPLETE_NOTIFICATION)                 },
        { CommandType::STOP_REQ_OF_RELATED_INFO_DISTRIBUTION_CMD,        static_cast<uint8_t>(MESSAGE_CODE::STOP_REQUEST_OF_RELATED_INFORMATION_DISTRIBUTION)   },
        { CommandType::DSRC_STATUS_REQ_CMD,                              static_cast<uint8_t>(MESSAGE_CODE::DSRC_STATUS_REQUEST)                                },
        { CommandType::TIME_CALIBRATION_REQ_CMD,                         static_cast<uint8_t>(MESSAGE_CODE::TIME_CALIBRATION_REQUEST)                           },
        { CommandType::SET_CARPARK_AVAIL_REQ_CMD,                        static_cast<uint8_t>(MESSAGE_CODE::SET_PARKING_AVAILABLE)                              },
        { CommandType::CD_DOWNLOAD_REQ_CMD,                              static_cast<uint8_t>(MESSAGE_CODE::CD_DOWNLOAD_REQUEST)                                },
        { CommandType::EEP_RESTART_INQUIRY_REQ_CMD,                      static_cast<uint8_t>(MESSAGE_CODE::EEP_RESTART_INQUIRY_RESPONSE)                       }
    };


    // Step 2: ACK/NAK must also carry correct requested type
    if (dataTypeCode_ == static_cast<uint8_t>(MESSAGE_CODE::ACK) || dataTypeCode_ == static_cast<uint8_t>(MESSAGE_CODE::NAK))
    {
        if (msgBody.empty())
        {
            return false;
        }

        auto it2 = validAckNakRspRequestedDataType.find(cmd.type);
        if (it2 == validAckNakRspRequestedDataType.end())
        {
            return false;
        }

        if (msgBody[0] != it2->second)
        {
            return false; // wrong referenced request inside ACK/NAK
        }
    }

    return true;
}

bool EEPClient::doesCmdRequireNotification(Command cmd)
{
    switch (cmd.type)
    {
        case CommandType::GET_OBU_INFO_REQ_CMD:
        case CommandType::DEDUCT_REQ_CMD:
        case CommandType::TRANSACTION_REQ_CMD:
        case CommandType::CPO_INFO_DISPLAY_REQ_CMD:
        case CommandType::CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD:
        {
            return true;
        }
        default:
        {
            return false;
        }
    }
}

bool EEPClient::isResponseComplete(Command cmd, const uint8_t& dataTypeCode_)
{
    static const std::unordered_map<CommandType, std::vector<uint8_t>> validResponses = {
        { CommandType::HEALTH_STATUS_REQ_CMD,                            {static_cast<uint8_t>(MESSAGE_CODE::HEALTH_STATUS_RESPONSE), static_cast<uint8_t>(MESSAGE_CODE::NAK)}       },
        { CommandType::WATCHDOG_REQ_CMD,                                 {static_cast<uint8_t>(MESSAGE_CODE::WATCHDOG_RESPONSE), static_cast<uint8_t>(MESSAGE_CODE::NAK)}            },
        { CommandType::START_REQ_CMD,                                    {static_cast<uint8_t>(MESSAGE_CODE::START_RESPONSE), static_cast<uint8_t>(MESSAGE_CODE::NAK)}               },
        { CommandType::STOP_REQ_CMD,                                     {static_cast<uint8_t>(MESSAGE_CODE::STOP_RESPONSE), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                },
        { CommandType::DI_REQ_CMD,                                       {static_cast<uint8_t>(MESSAGE_CODE::DI_STATUS_RESPONSE), static_cast<uint8_t>(MESSAGE_CODE::NAK)}           },
        { CommandType::DO_REQ_CMD,                                       {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::SET_DI_PORT_CONFIG_CMD,                           {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::GET_OBU_INFO_REQ_CMD,                             {static_cast<uint8_t>(MESSAGE_CODE::NAK)}                                                                   },
        { CommandType::GET_OBU_INFO_STOP_REQ_CMD,                        {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::DEDUCT_REQ_CMD,                                   {static_cast<uint8_t>(MESSAGE_CODE::NAK)}                                                                   },
        { CommandType::DEDUCT_STOP_REQ_CMD,                              {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::TRANSACTION_REQ_CMD,                              {static_cast<uint8_t>(MESSAGE_CODE::NAK)}                                                                   },
        { CommandType::CPO_INFO_DISPLAY_REQ_CMD,                         {static_cast<uint8_t>(MESSAGE_CODE::NAK)}                                                                   },
        { CommandType::CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD,    {static_cast<uint8_t>(MESSAGE_CODE::NAK)}                                                                   },
        { CommandType::DSRC_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD,       {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::STOP_REQ_OF_RELATED_INFO_DISTRIBUTION_CMD,        {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::DSRC_STATUS_REQ_CMD,                              {static_cast<uint8_t>(MESSAGE_CODE::DSRC_STATUS_RESPONSE), static_cast<uint8_t>(MESSAGE_CODE::NAK)}         },
        { CommandType::TIME_CALIBRATION_REQ_CMD,                         {static_cast<uint8_t>(MESSAGE_CODE::TIME_CALIBRATION_RESPONSE), static_cast<uint8_t>(MESSAGE_CODE::NAK)}    },
        { CommandType::SET_CARPARK_AVAIL_REQ_CMD,                        {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::CD_DOWNLOAD_REQ_CMD,                              {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          },
        { CommandType::EEP_RESTART_INQUIRY_REQ_CMD,                      {static_cast<uint8_t>(MESSAGE_CODE::ACK), static_cast<uint8_t>(MESSAGE_CODE::NAK)}                          }
    };

    auto it = validResponses.find(cmd.type);
    if (it == validResponses.end())
    {
        return false;
    }

    return std::find(it->second.begin(), it->second.end(), dataTypeCode_) != it->second.end();
}

bool EEPClient::isNotificationReceived(const uint8_t& dataTypeCode_)
{
    static const std::unordered_set<uint8_t> validNotifications = {
        static_cast<uint8_t>(MESSAGE_CODE::DI_STATUS_NOTIFICATION),
        static_cast<uint8_t>(MESSAGE_CODE::OBU_INFORMATION_NOTIFICATION),
        static_cast<uint8_t>(MESSAGE_CODE::TRANSACTION_DATA),
        static_cast<uint8_t>(MESSAGE_CODE::CPO_INFORMATION_DISPLAY_RESULT),
        static_cast<uint8_t>(MESSAGE_CODE::CARPARK_PROCESS_COMPLETE_RESULT),
        static_cast<uint8_t>(MESSAGE_CODE::EEP_RESTART_INQUIRY),
        static_cast<uint8_t>(MESSAGE_CODE::NOTIFICATION_LOG)
    };

    return validNotifications.find(dataTypeCode_) != validNotifications.end();
}

bool EEPClient::isResponseNotificationComplete(Command cmd, const uint8_t& dataTypeCode_)
{
    static const std::unordered_map<CommandType, uint8_t> validAckNakRspRequestedDataType = {
        { CommandType::GET_OBU_INFO_REQ_CMD,                             static_cast<uint8_t>(MESSAGE_CODE::OBU_INFORMATION_NOTIFICATION)                       },
        { CommandType::DEDUCT_REQ_CMD,                                   static_cast<uint8_t>(MESSAGE_CODE::TRANSACTION_DATA)                                   },
        { CommandType::TRANSACTION_REQ_CMD,                              static_cast<uint8_t>(MESSAGE_CODE::TRANSACTION_DATA)                                   },
        { CommandType::CPO_INFO_DISPLAY_REQ_CMD,                         static_cast<uint8_t>(MESSAGE_CODE::CPO_INFORMATION_DISPLAY_RESULT)                     },
        { CommandType::CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD,    static_cast<uint8_t>(MESSAGE_CODE::CARPARK_PROCESS_COMPLETE_RESULT)                    }
    };

    auto it = validAckNakRspRequestedDataType.find(cmd.type);
    if (it == validAckNakRspRequestedDataType.end())
    {
        return false;
    }

    return it->second == dataTypeCode_;
}

void EEPClient::handleInvalidMessage(const std::vector<uint8_t>& data, uint8_t reasonCode_)
{
    if (data.size() >= MessageHeader::HEADER_SIZE)
    {
        uint16_t seqNo_ = (data[13] << 8) | data[12];
        FnSendNak(seqNo_, data[2], reasonCode_);
    }
    else
    {
        Logger::getInstance()->FnLog("EEP: [RX] NAK skipped | Reason=Frame shorter than header | Bytes=" + std::to_string(data.size()), logFileName_, "EEP");
    }
}

void EEPClient::handleReceivedData(bool success, const std::vector<uint8_t>& data)
{
    try
    {
        if (!success)
        {
            Logger::getInstance()->FnLog(
                "EEP: [RX] Failed | Reason=TCP read failed",
                logFileName_,
                "EEP");

            if (!stopping_.load())
            {
                processEvent(EVENT::RECONNECT_REQUEST);
            }
            return;
        }

        if (!isValidCheckSum(data))
        {
            Logger::getInstance()->FnLog(
                "EEP: [RX] Rejected | Reason=Checksum mismatch | Bytes=" +
                    std::to_string(data.size()) +
                    " | Hex=" + bytesToHexString(data),
                logFileName_,
                "EEP");

            // NAK reason: Check Code Mismatch.
            handleInvalidMessage(data, 0x03);
            return;
        }

        const std::vector<uint8_t> dataWithoutCheckSum(data.begin(), data.end() - 4);
        MessageHeader msgHeader{};
        std::vector<uint8_t> msgBody;

        if (!parseMessage(dataWithoutCheckSum, msgHeader, msgBody))
        {
            Logger::getInstance()->FnLog(
                "EEP: [RX] Rejected | Reason=Message parse failed | Bytes=" +
                    std::to_string(data.size()) +
                    " | Hex=" + bytesToHexString(data),
                logFileName_,
                "EEP");

            // NAK reason: Data Length Error.
            handleInvalidMessage(data, 0x02);
            return;
        }

        if (!isValidSourceDestination(msgHeader.sourceID_, msgHeader.destinationID_))
        {
            Logger::getInstance()->FnLog(
                "EEP: [RX] Rejected | Reason=Invalid endpoint"
                " | Src=" + std::to_string(msgHeader.sourceID_) +
                " | Dst=" + std::to_string(msgHeader.destinationID_) +
                " | ExpectedSrc=" + std::to_string(eepDestinationId_) +
                " | ExpectedDst=" + std::to_string(eepSourceId_) +
                " | Seq=" + std::to_string(msgHeader.seqNo_),
                logFileName_,
                "EEP");

            // NAK reason: Others.
            handleInvalidMessage(data, 0x04);
            return;
        }

        // Valid RX packets are logged as decoded protocol fields, not as a raw
        // byte dump. Raw hex is kept for rejected frames above where it is most
        // useful for diagnostics.
        showParsedMessage(msgHeader, msgBody);

        // Handle for normal request cmd from station and response from EEP
        if (isResponseMatchedDataTypeCode(getCurrentCmdRequested(), msgHeader.dataTypeCode_, msgBody))
        {
            if (!expectedResponseSeqNo_ || msgHeader.seqNo_ != *expectedResponseSeqNo_)
            {
                Logger::getInstance()->FnLog(
                    "EEP: [RX] Rejected | Reason=Sequence mismatch"
                    " | Code=" + messageCodeToString(
                        static_cast<MESSAGE_CODE>(msgHeader.dataTypeCode_)) +
                    " | Expected=" +
                        (expectedResponseSeqNo_
                            ? std::to_string(*expectedResponseSeqNo_)
                            : std::string("None")) +
                    " | Received=" + std::to_string(msgHeader.seqNo_),
                    logFileName_,
                    "EEP");
                return;
            }

            boost::system::error_code timerEc;
            ackTimer_.cancel(timerEc);

            std::string eventMsg;
            handleParsedResponseMessage(msgHeader, msgBody, eventMsg, data);

            if (!eventMsg.empty())
            {
                EEPData_In.store(1);
                EventManager::getInstance()->FnEnqueueEvent("Evt_handleEEPClientResponse", eventMsg);
                Logger::getInstance()->FnLog(
                    "EEP: [EVENT] Queued | Name=Evt_handleEEPClientResponse"
                    " | Source=Response"
                    " | Code=" + messageCodeToString(
                        static_cast<MESSAGE_CODE>(msgHeader.dataTypeCode_)) +
                    " | Seq=" + std::to_string(msgHeader.seqNo_),
                    logFileName_,
                    "EEP");
            }

            const auto responseCode = static_cast<MESSAGE_CODE>(msgHeader.dataTypeCode_);
            expectedResponseSeqNo_.reset();

            // Only a positive ACK for commands that explicitly require
            // a follow-up notification should keep the FSM waiting.
            // NAK and direct response messages complete the request.
            if (responseCode == MESSAGE_CODE::ACK &&
                doesCmdRequireNotification(getCurrentCmdRequested()))
            {
                processEvent(EVENT::ACK_TIMER_CANCELLED_ACK_RECEIVED);
            }
            else
            {
                processEvent(EVENT::ACK_AS_RSP_RECEIVED);
            }
            return;
        }

        // Unsolicited EEP notification.
        if (isNotificationReceived(msgHeader.dataTypeCode_))
        {
            const bool completesOutstandingRequest =
                isResponseNotificationComplete(getCurrentCmdRequested(), msgHeader.dataTypeCode_);
            
            if (completesOutstandingRequest)
            {
                Logger::getInstance()->FnLog(
                    "EEP: [RSP] Follow-up received"
                    " | Cmd=" + getCommandString(currentCmdRequested_.type) +
                    " | Code=" + messageCodeToString(
                        static_cast<MESSAGE_CODE>(msgHeader.dataTypeCode_)) +
                    " | Seq=" + std::to_string(msgHeader.seqNo_),
                    logFileName_,
                    "EEP");

                boost::system::error_code timerEc;
                responseTimer_.cancel(timerEc);
                processEvent(EVENT::RESPONSE_TIMER_CANCELLED_RSP_RECEIVED);
            }
            else
            {
                Logger::getInstance()->FnLog(
                    "EEP: [RX] Notification"
                    " | Code=" + messageCodeToString(
                        static_cast<MESSAGE_CODE>(msgHeader.dataTypeCode_)) +
                    " | Seq=" + std::to_string(msgHeader.seqNo_) +
                    " | Action=Send ACK",
                    logFileName_,
                    "EEP");
            }

            // Send ACK with requested data type code
            std::string eventMsg = "";
            handleParsedNotificationMessage(msgHeader, msgBody, eventMsg);
            FnSendAck(msgHeader.seqNo_, msgHeader.dataTypeCode_);

            if (!eventMsg.empty())
            {
                EventManager::getInstance()->FnEnqueueEvent("Evt_handleEEPClientResponse", eventMsg);
                Logger::getInstance()->FnLog(
                    "EEP: [EVENT] Queued | Name=Evt_handleEEPClientResponse"
                    " | Source=Notification"
                    " | Code=" + messageCodeToString(
                        static_cast<MESSAGE_CODE>(msgHeader.dataTypeCode_)) +
                    " | Seq=" + std::to_string(msgHeader.seqNo_),
                    logFileName_,
                    "EEP");
            }
            return;
        }

        Logger::getInstance()->FnLog(
            "EEP: [RX] Rejected | Reason=Unsupported data type"
            " | Code=0x" + [&msgHeader]()
            {
                std::ostringstream oss;
                oss << std::uppercase << std::hex << std::setw(2)
                    << std::setfill('0')
                    << static_cast<unsigned int>(msgHeader.dataTypeCode_);
                return oss.str();
            }() +
            " | Seq=" + std::to_string(msgHeader.seqNo_),
            logFileName_,
            "EEP");

        // Send NAK with reason code : Unsupported Data Type Code
        handleInvalidMessage(data, 0x01);
    }
    catch (const std::exception& ex)
    {
        Logger::getInstance()->FnLog("EEP: [RX] Exception | Error=" + std::string(ex.what()), logFileName_, "EEP");
    }
    catch (...)
    {
        Logger::getInstance()->FnLog("EEP: [RX] Exception | Error=Unknown", logFileName_, "EEP");
    }
}

void EEPClient::eepClientConnect()
{
    if (!client_ || stopping_.load())
    {
        return;
    }

    client_->connect();
}

void EEPClient::eepClientSend(const std::vector<uint8_t>& message)
{
    if (!client_ || stopping_.load())
    {
        return;
    }

    std::uint16_t seqNo = 0;
    std::uint8_t dataTypeCode = 0;

    if (message.size() >= MessageHeader::HEADER_SIZE)
    {
        dataTypeCode = message[2];
        seqNo = static_cast<std::uint16_t>(message[12]) |
                (static_cast<std::uint16_t>(message[13]) << 8);
    }

    Logger::getInstance()->FnLog(
        "EEP: [TX] Frame"
        " | Cmd=" + getCommandString(currentCmd_.type) +
        " | Code=" + messageCodeToString(
            static_cast<MESSAGE_CODE>(dataTypeCode)) +
        " | Seq=" + std::to_string(seqNo) +
        " | Bytes=" + std::to_string(message.size()) +
        " | Hex=" + bytesToHexString(message),
        logFileName_,
        "EEP");

    client_->send(message);
}

void EEPClient::eepClientClose()
{
    if (client_)
    {
        client_->close();
    }
}

const EEPClient::StateTransition EEPClient::stateTransitionTable[static_cast<int>(STATE::STATE_COUNT)] = 
{
    {STATE::IDLE,
    {
        {EVENT::CONNECT                                         , &EEPClient::handleIdleState                   , STATE::CONNECTING             }
    }},
    {STATE::CONNECTING,
    {
        {EVENT::CONNECT_SUCCESS                                 , &EEPClient::handleConnectingState             , STATE::CONNECTED              },
        {EVENT::CONNECT_FAIL                                    , &EEPClient::handleConnectingState             , STATE::IDLE                   }
    }},
    {STATE::CONNECTED,
    {
        {EVENT::CHECK_COMMAND                                   , &EEPClient::handleConnectedState              , STATE::CONNECTED              },
        {EVENT::WRITE_COMMAND                                   , &EEPClient::handleConnectedState              , STATE::WRITING_REQUEST        },
        {EVENT::RECONNECT_REQUEST                               , &EEPClient::handleConnectedState              , STATE::IDLE                   }
    }},
    {STATE::WRITING_REQUEST,
    {
        {EVENT::WRITE_COMPLETED                                 , &EEPClient::handleWritingRequestState         , STATE::WAITING_FOR_RESPONSE   },
        {EVENT::WRITE_TIMEOUT                                   , &EEPClient::handleWritingRequestState         , STATE::CONNECTED              },

        {EVENT::RECONNECT_REQUEST                               , &EEPClient::handleWritingRequestState         , STATE::IDLE                   }
    }},
    {STATE::WAITING_FOR_RESPONSE,
    {
        {EVENT::ACK_TIMER_CANCELLED_ACK_RECEIVED                , &EEPClient::handleWaitingForResponseState     , STATE::WAITING_FOR_RESPONSE  },
        {EVENT::ACK_AS_RSP_RECEIVED                             , &EEPClient::handleWaitingForResponseState     , STATE::CONNECTED             },
        {EVENT::ACK_TIMEOUT                                     , &EEPClient::handleWaitingForResponseState     , STATE::CONNECTED             },
        {EVENT::RESPONSE_TIMER_CANCELLED_RSP_RECEIVED           , &EEPClient::handleWaitingForResponseState     , STATE::CONNECTED             },
        {EVENT::RESPONSE_TIMEOUT                                , &EEPClient::handleWaitingForResponseState     , STATE::CONNECTED             },
        {EVENT::RECONNECT_REQUEST                               , &EEPClient::handleWaitingForResponseState     , STATE::IDLE                  },

        {EVENT::UNSOLICITED_REQUEST_DONE                        , &EEPClient::handleWaitingForResponseState     , STATE::CONNECTED             }
    }}
};

std::string EEPClient::messageCodeToString(MESSAGE_CODE code)
{
    std::string codeStr = "Unknown Message Code";

    switch (code)
    {
        case MESSAGE_CODE::ACK:
        {
            codeStr = "ACK";
            break;
        }
        case MESSAGE_CODE::NAK:
        {
            codeStr = "NAK";
            break;
        }
        case MESSAGE_CODE::HEALTH_STATUS_REQUEST:
        {
            codeStr = "HEALTH_STATUS_REQUEST";
            break;
        }
        case MESSAGE_CODE::HEALTH_STATUS_RESPONSE:
        {
            codeStr = "HEALTH_STATUS_RESPONSE";
            break;
        }
        case MESSAGE_CODE::WATCHDOG_REQUEST:
        {
            codeStr = "WATCHDOG_REQUEST";
            break;
        }
        case MESSAGE_CODE::WATCHDOG_RESPONSE:
        {
            codeStr = "WATCHDOG_RESPONSE";
            break;
        }
        case MESSAGE_CODE::START_REQUEST:
        {
            codeStr = "START_REQUEST";
            break;
        }
        case MESSAGE_CODE::START_RESPONSE:
        {
            codeStr = "START_RESPONSE";
            break;
        }
        case MESSAGE_CODE::STOP_REQUEST:
        {
            codeStr = "STOP_REQUEST";
            break;
        }
        case MESSAGE_CODE::STOP_RESPONSE:
        {
            codeStr = "STOP_RESPONSE";
            break;
        }
        case MESSAGE_CODE::DI_STATUS_NOTIFICATION:
        {
            codeStr = "DI_STATUS_NOTIFICATION";
            break;
        }
        case MESSAGE_CODE::DI_STATUS_REQUEST:
        {
            codeStr = "DI_STATUS_REQUEST";
            break;
        }
        case MESSAGE_CODE::DI_STATUS_RESPONSE:
        {
            codeStr = "DI_STATUS_RESPONSE";
            break;
        }
        case MESSAGE_CODE::SET_DO_REQUEST:
        {
            codeStr = "SET_DO_REQUEST";
            break;
        }
        case MESSAGE_CODE::SET_DI_PORT_CONFIGURATION:
        {
            codeStr = "SET_DI_PORT_CONFIGURATION";
            break;
        }
        case MESSAGE_CODE::GET_OBU_INFORMATION_REQUEST:
        {
            codeStr = "GET_OBU_INFORMATION_REQUEST";
            break;
        }
        case MESSAGE_CODE::OBU_INFORMATION_NOTIFICATION:
        {
            codeStr = "OBU_INFORMATION_NOTIFICATION";
            break;
        }
        case MESSAGE_CODE::GET_OBU_INFORMATION_STOP:
        {
            codeStr = "GET_OBU_INFORMATION_STOP";
            break;
        }
        case MESSAGE_CODE::DEDUCT_REQUEST:
        {
            codeStr = "DEDUCT_REQUEST";
            break;
        }
        case MESSAGE_CODE::TRANSACTION_DATA:
        {
            codeStr = "TRANSACTION_DATA";
            break;
        }
        case MESSAGE_CODE::DEDUCT_STOP_REQUEST:
        {
            codeStr = "DEDUCT_STOP_REQUEST";
            break;
        }
        case MESSAGE_CODE::TRANSACTION_REQUEST:
        {
            codeStr = "TRANSACTION_REQUEST";
            break;
        }
        case MESSAGE_CODE::CPO_INFORMATION_DISPLAY_REQUEST:
        {
            codeStr = "CPO_INFORMATION_DISPLAY_REQUEST";
            break;
        }
        case MESSAGE_CODE::CPO_INFORMATION_DISPLAY_RESULT:
        {
            codeStr = "CPO_INFORMATION_DISPLAY_RESULT";
            break;
        }
        case MESSAGE_CODE::CARPARK_PROCESS_COMPLETE_NOTIFICATION:
        {
            codeStr = "CARPARK_PROCESS_COMPLETE_NOTIFICATION";
            break;
        }
        case MESSAGE_CODE::CARPARK_PROCESS_COMPLETE_RESULT:
        {
            codeStr = "CARPARK_PROCESS_COMPLETE_RESULT";
            break;
        }
        case MESSAGE_CODE::DSRC_PROCESS_COMPLETE_NOTIFICATION:
        {
            codeStr = "DSRC_PROCESS_COMPLETE_NOTIFICATION";
            break;
        }
        case MESSAGE_CODE::STOP_REQUEST_OF_RELATED_INFORMATION_DISTRIBUTION:
        {
            codeStr = "STOP_REQUEST_OF_RELATED_INFORMATION_DISTRIBUTION";
            break;
        }
        case MESSAGE_CODE::DSRC_STATUS_REQUEST:
        {
            codeStr = "DSRC_STATUS_REQUEST";
            break;
        }
        case MESSAGE_CODE::DSRC_STATUS_RESPONSE:
        {
            codeStr = "DSRC_STATUS_RESPONSE";
            break;
        }
        case MESSAGE_CODE::TIME_CALIBRATION_REQUEST:
        {
            codeStr = "TIME_CALIBRATION_REQUEST";
            break;
        }
        case MESSAGE_CODE::TIME_CALIBRATION_RESPONSE:
        {
            codeStr = "TIME_CALIBRATION_RESPONSE";
            break;
        }
        case MESSAGE_CODE::EEP_RESTART_INQUIRY:
        {
            codeStr = "EEP_RESTART_INQUIRY";
            break;
        }
        case MESSAGE_CODE::EEP_RESTART_INQUIRY_RESPONSE:
        {
            codeStr = "EEP_RESTART_INQUIRY_RESPONSE";
            break;
        }
        case MESSAGE_CODE::NOTIFICATION_LOG:
        {
            codeStr = "NOTIFICATION_LOG";
            break;
        }
        case MESSAGE_CODE::SET_PARKING_AVAILABLE:
        {
            codeStr = "SET_PARKING_AVAILABLE";
            break;
        }
        case MESSAGE_CODE::CD_DOWNLOAD_REQUEST:
        {
            codeStr = "CD_DOWNLOAD_REQUEST";
            break;
        }
    }

    return codeStr;
}

std::string EEPClient::eventToString(EVENT event)
{
    std::string eventStr = "Unknown Event";

    switch (event)
    {
        case EVENT::CONNECT:
        {
            eventStr = "CONNECT";
            break;
        }
        case EVENT::CONNECT_SUCCESS:
        {
            eventStr = "CONNECT_SUCCESS";
            break;
        }
        case EVENT::CONNECT_FAIL:
        {
            eventStr = "CONNECT_FAIL";
            break;
        }
        case EVENT::CHECK_COMMAND:
        {
            eventStr = "CHECK_COMMAND";
            break;
        }
        case EVENT::WRITE_COMMAND:
        {
            eventStr = "WRITE_COMMAND";
            break;
        }
        case EVENT::WRITE_COMPLETED:
        {
            eventStr = "WRITE_COMPLETED";
            break;
        }
        case EVENT::WRITE_TIMEOUT:
        {
            eventStr = "WRITE_TIMEOUT";
            break;
        }
        case EVENT::ACK_TIMER_CANCELLED_ACK_RECEIVED:
        {
            eventStr = "ACK_TIMER_CANCELLED_ACK_RECEIVED";
            break;
        }
        case EVENT::ACK_AS_RSP_RECEIVED:
        {
            eventStr = "ACK_AS_RSP_RECEIVED";
            break;
        }
        case EVENT::ACK_TIMEOUT:
        {
            eventStr = "ACK_TIMEOUT";
            break;
        }
        case EVENT::RESPONSE_TIMER_CANCELLED_RSP_RECEIVED:
        {
            eventStr = "RESPONSE_TIMER_CANCELLED_RSP_RECEIVED";
            break;
        }
        case EVENT::RESPONSE_TIMEOUT:
        {
            eventStr = "RESPONSE_TIMEOUT";
            break;
        }
        case EVENT::RECONNECT_REQUEST:
        {
            eventStr = "RECONNECT_REQUEST";
            break;
        }
        case EVENT::UNSOLICITED_REQUEST_DONE:
        {
            eventStr = "UNSOLICITED_REQUEST_DONE";
            break;
        }
    }

    return eventStr;
}

std::string EEPClient::stateToString(STATE state)
{
    std::string stateStr = "Unknown State";

    switch (state)
    {
        case STATE::IDLE:
        {
            stateStr = "IDLE";
            break;
        }
        case STATE::CONNECTING:
        {
            stateStr = "CONNECTING";
            break;
        }
        case STATE::CONNECTED:
        {
            stateStr = "CONNECTED";
            break;
        }
        case STATE::WRITING_REQUEST:
        {
            stateStr = "WRITING_REQUEST";
            break;
        }
        case STATE::WAITING_FOR_RESPONSE:
        {
            stateStr = "WAITING_FOR_RESPONSE";
            break;
        }
    }

    return stateStr;
}

std::string EEPClient::getCommandString(CommandType cmd)
{
    std::string cmdStr = "Unknown Command";

    switch (cmd)
    {
        case CommandType::UNKNOWN_REQ_CMD:
        {
            cmdStr = "UNKNOWN_REQ_CMD";
            break;
        }
        case CommandType::ACK:
        {
            cmdStr = "ACK";
            break;
        }
        case CommandType::NAK:
        {
            cmdStr = "NAK";
            break;
        }
        case CommandType::HEALTH_STATUS_REQ_CMD:
        {
            cmdStr = "HEALTH_STATUS_REQ_CMD";
            break;
        }
        case CommandType::WATCHDOG_REQ_CMD:
        {
            cmdStr = "WATCHDOG_REQ_CMD";
            break;
        }
        case CommandType::START_REQ_CMD:
        {
            cmdStr = "START_REQ_CMD";
            break;
        }
        case CommandType::STOP_REQ_CMD:
        {
            cmdStr = "STOP_REQ_CMD";
            break;
        }
        case CommandType::DI_REQ_CMD:
        {
            cmdStr = "DI_REQ_CMD";
            break;
        }
        case CommandType::DO_REQ_CMD:
        {
            cmdStr = "DO_REQ_CMD";
            break;
        }
        case CommandType::SET_DI_PORT_CONFIG_CMD:
        {
            cmdStr = "SET_DI_PORT_CONFIG_CMD";
            break;
        }
        case CommandType::GET_OBU_INFO_REQ_CMD:
        {
            cmdStr = "GET_OBU_INFO_REQ_CMD";
            break;
        }
        case CommandType::GET_OBU_INFO_STOP_REQ_CMD:
        {
            cmdStr = "GET_OBU_INFO_STOP_REQ_CMD";
            break;
        }
        case CommandType::DEDUCT_REQ_CMD:
        {
            cmdStr = "DEDUCT_REQ_CMD";
            break;
        }
        case CommandType::DEDUCT_STOP_REQ_CMD:
        {
            cmdStr = "DEDUCT_STOP_REQ_CMD";
            break;
        }
        case CommandType::TRANSACTION_REQ_CMD:
        {
            cmdStr = "TRANSACTION_REQ_CMD";
            break;
        }
        case CommandType::CPO_INFO_DISPLAY_REQ_CMD:
        {
            cmdStr = "CPO_INFO_DISPLAY_REQ_CMD";
            break;
        }
        case CommandType::CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD:
        {
            cmdStr = "CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD";
            break;
        }
        case CommandType::DSRC_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD:
        {
            cmdStr = "DSRC_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD";
            break;
        }
        case CommandType::STOP_REQ_OF_RELATED_INFO_DISTRIBUTION_CMD:
        {
            cmdStr = "STOP_REQ_OF_RELATED_INFO_DISTRIBUTION_CMD";
            break;
        }
        case CommandType::DSRC_STATUS_REQ_CMD:
        {
            cmdStr = "DSRC_STATUS_REQ_CMD";
            break;
        }
        case CommandType::TIME_CALIBRATION_REQ_CMD:
        {
            cmdStr = "TIME_CALIBRATION_REQ_CMD";
            break;
        }
        case CommandType::SET_CARPARK_AVAIL_REQ_CMD:
        {
            cmdStr = "SET_CARPARK_AVAIL_REQ_CMD";
            break;
        }
        case CommandType::CD_DOWNLOAD_REQ_CMD:
        {
            cmdStr = "CD_DOWNLOAD_REQ_CMD";
            break;
        }
        case CommandType::EEP_RESTART_INQUIRY_REQ_CMD:
        {
            cmdStr = "EEP_RESTART_INQUIRY_REQ_CMD";
            break;
        }
    }

    return cmdStr;
}

void EEPClient::processEvent(EVENT event)
{
    boost::asio::post(
        ioContext_,
        [this, event]()
        {
            if (stopping_.load())
            {
                return;
            }

            const auto stateIndex = static_cast<std::size_t>(currentState_);
            if (stateIndex >= static_cast<std::size_t>(STATE::STATE_COUNT))
            {
                Logger::getInstance()->FnLog(
                    "EEP: [FSM] Invalid state | Recovery=IDLE",
                    logFileName_,
                    "EEP");
                currentState_ = STATE::IDLE;
                return;
            }

            const auto& transitions = stateTransitionTable[stateIndex].transitions;

            for (const auto& transition : transitions)
            {
                if (transition.event != event)
                {
                    continue;
                }

                Logger::getInstance()->FnLog(
                    "EEP: [FSM] Transition | From=" + stateToString(currentState_) +
                        " | Event=" + eventToString(event) +
                        " | To=" + stateToString(transition.nextState),
                    logFileName_,
                    "EEP");

                if (transition.eventHandler != nullptr)
                {
                    (this->*transition.eventHandler)(event);
                }

                currentState_ = transition.nextState;

                if (currentState_ == STATE::CONNECTED)
                {
                    boost::asio::post(
                        ioContext_,
                        [this]()
                        {
                            checkCommandQueue();
                        });
                }
                return;
            }

            Logger::getInstance()->FnLog(
                "EEP: [FSM] Event ignored | State=" + stateToString(currentState_) +
                    " | Event=" + eventToString(event),
                logFileName_,
                "EEP");
        });
}

void EEPClient::checkCommandQueue()
{
    if (stopping_.load() || currentState_ != STATE::CONNECTED)
    {
        return;
    }

    if (!commandQueue_.empty())
    {
        processEvent(EVENT::WRITE_COMMAND);
    }
}

void EEPClient::enqueueCommand(CommandType type, int priority, std::shared_ptr<CommandDataBase> data)
{
    if (!acceptingWork_.load())
    {
        return;
    }

    boost::asio::post(
        ioContext_,
        [this, type, priority, data = std::move(data)]() mutable
        {
            if (stopping_.load())
            {
                return;
            }

            Command command{type, priority, commandSequence_++, std::move(data)};

            if (!client_ || !client_->isConnected())
            {
                Logger::getInstance()->FnLog("EEP: [QUEUE] Rejected | Cmd=" + getCommandString(type) + " | Reason=Disconnected", logFileName_, "EEP");
                handleCommandErrorOrTimeout(command, MSG_STATUS::SEND_FAILED);
                return;
            }

            commandQueue_.push(std::move(command));

            Logger::getInstance()->FnLog(
                "EEP: [QUEUE] Enqueued | Cmd=" + getCommandString(type) +
                    " | Priority=" + std::to_string(priority) +
                    " | Size=" + std::to_string(commandQueue_.size()),
                logFileName_,
                "EEP");

            checkCommandQueue();
        });
}

void EEPClient::postCommandFailure(CommandType type, MSG_STATUS status)
{
    if (!moduleRunning_.load() || stopping_.load())
    {
        return;
    }

    boost::asio::post(
        ioContext_,
        [this, type, status]()
        {
            if (stopping_.load())
            {
                return;
            }

            Command cmd{};
            cmd.type = type;
            handleCommandErrorOrTimeout(cmd, status);
        });
}

void EEPClient::popFromCommandQueueAndEnqueueWrite()
{
    if (commandQueue_.empty())
    {
        return;
    }

    Command cmd = commandQueue_.top();
    commandQueue_.pop();

    setCurrentCmd(cmd);

    // ACK/NAK are unsolicited replies. They must not overwrite the request
    // whose ACK/notification we are still correlating.
    if (cmd.type != CommandType::ACK && cmd.type != CommandType::NAK)
    {
        setCurrentCmdRequested(cmd);
    }

    Logger::getInstance()->FnLog(
        "EEP: [QUEUE] Dequeued | Cmd=" + getCommandString(cmd.type) +
            " | Priority=" + std::to_string(cmd.priority) +
            " | Remaining=" + std::to_string(commandQueue_.size()),
        logFileName_,
        "EEP");

    auto [packet, ok] = prepareCmd(cmd);
    if (!ok)
    {
        Logger::getInstance()->FnLog(
            "EEP: [TX] Prepare failed | Cmd=" + getCommandString(cmd.type),
            logFileName_,
            "EEP");
        processEvent(EVENT::WRITE_TIMEOUT);
        return;
    }

    eepClientSend(packet);
}

void EEPClient::clearCommandQueue()
{
    const auto queueSize = commandQueue_.size();

    while (!commandQueue_.empty())
    {
        Command cmd = commandQueue_.top();
        commandQueue_.pop();
        handleCommandErrorOrTimeout(cmd, MSG_STATUS::SEND_FAILED);
    }

    Logger::getInstance()->FnLog(
        "EEP: [QUEUE] Cleared | Count=" + std::to_string(queueSize),
        logFileName_,
        "EEP");
}

void EEPClient::setCurrentCmd(Command cmd)
{
    currentCmd_ = std::move(cmd);
}

EEPClient::Command EEPClient::getCurrentCmd() const
{
    return currentCmd_;
}

void EEPClient::setCurrentCmdRequested(Command cmd)
{
    currentCmdRequested_ = std::move(cmd);
}

EEPClient::Command EEPClient::getCurrentCmdRequested() const
{
    return currentCmdRequested_;
}

void EEPClient::incrementSequenceNo()
{
    ++sequenceNo_;
}

uint16_t EEPClient::getSequenceNo() const
{
    return sequenceNo_;
}

std::uint16_t EEPClient::allocateDeductCmdSerialNo()
{
    return deductCmdSerialNo_.fetch_add(1);
}

std::uint16_t EEPClient::getDeductSerialFromCurrentRequest() const
{
    if (currentCmdRequested_.type != CommandType::DEDUCT_REQ_CMD ||
        !currentCmdRequested_.data)
    {
        return 0;
    }

    const auto deductData =
        std::dynamic_pointer_cast<DeductData>(currentCmdRequested_.data);

    if (!deductData)
    {
        return 0;
    }

    return static_cast<std::uint16_t>(deductData->serialNum[0]) |
           (static_cast<std::uint16_t>(deductData->serialNum[1]) << 8);
}

void EEPClient::appendMessageHeader(std::vector<uint8_t>& msg, uint8_t messageCode, uint16_t seqNo, uint16_t length)
{
    auto now = std::chrono::system_clock::now();
    auto timer = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo = {};
    localtime_r(&timer, &timeinfo); // Thread-safe version of localtime()

    uint8_t day = static_cast<uint8_t>(timeinfo.tm_mday);
    uint8_t month = static_cast<uint8_t>(timeinfo.tm_mon + 1);  // 0-based
    uint16_t year = static_cast<uint16_t>(timeinfo.tm_year + 1900);
    uint8_t hour = static_cast<uint8_t>(timeinfo.tm_hour);
    uint8_t minute = static_cast<uint8_t>(timeinfo.tm_min);
    uint8_t second = static_cast<uint8_t>(timeinfo.tm_sec);

    msg.push_back(static_cast<uint8_t>(eepDestinationId_));
    msg.push_back(static_cast<uint8_t>(eepSourceId_));
    msg.push_back(messageCode);
    msg.push_back(0x00);
    msg.push_back(day);
    msg.push_back(month);
    Common::getInstance()->FnAppendUint16LE(msg, year);
    msg.push_back(0x00);
    msg.push_back(second);
    msg.push_back(minute);
    msg.push_back(hour);
    Common::getInstance()->FnAppendUint16LE(msg, seqNo);
    Common::getInstance()->FnAppendUint16LE(msg, length);
}

uint32_t EEPClient::calculateChecksumNoPadding(const std::vector<uint8_t>& data)
{
    uint32_t sum = 0;

    size_t i = 0;
    // Sum full 4-byte words
    while (i + 3 < data.size())
    {
        uint32_t word = (static_cast<uint32_t>(data[i])) |
                        (static_cast<uint32_t>(data[i + 1] << 8)) |
                        (static_cast<uint32_t>(data[i + 2] << 16)) |
                        (static_cast<uint32_t>(data[i + 3] << 24));
        
        sum += word;
        i += 4;
    }

    // Add leftover bytes as uint8_t
    while (i < data.size())
    {
        sum += static_cast<uint8_t>(data[i]);
        ++i;
    }

    return static_cast<uint32_t>(sum);
}

std::pair<std::vector<uint8_t>, bool> EEPClient::prepareCmd(Command cmd)
{
    std::vector<uint8_t> msg;
    bool success = true;

    const bool usesControllerSequence =
        cmd.type != CommandType::ACK && cmd.type != CommandType::NAK;

    const std::uint16_t seqNo = usesControllerSequence
        ? getSequenceNo()
        : 0;

    switch (cmd.type)
    {
        case CommandType::ACK:
        {
            // Need to set the same sequence number in request packet
            auto data = std::dynamic_pointer_cast<AckData>(cmd.data);
            if (data)
            {
                // Message Header
                appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::ACK), data->getSeqNo(), 0x0004);

                // Message Content
                auto payload = data->serialize();
                msg.insert(msg.end(), payload.begin(), payload.end());
            }
            else
            {
                Logger::getInstance()->FnLog("EEP: [TX] Prepare failed | Cmd=ACK | Reason=Missing payload", logFileName_, "EEP");
                success = false;
            }
            break;
        }
        case CommandType::NAK:
        {
            // Need to set the same sequence number in request packet
            auto data = std::dynamic_pointer_cast<NakData>(cmd.data);
            if (data)
            {
                // Message Header
                appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::NAK), data->getSeqNo(), 0x0004);

                // Message Content
                auto payload = data->serialize();
                msg.insert(msg.end(), payload.begin(), payload.end());
            }
            else
            {
                Logger::getInstance()->FnLog("EEP: [TX] Prepare failed | Cmd=NAK | Reason=Missing payload", logFileName_, "EEP");
                success = false;
            }
            break;
        }
        case CommandType::HEALTH_STATUS_REQ_CMD:
        {
            // Message Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::HEALTH_STATUS_REQUEST), seqNo, 0x0000);
            break;
        }
        case CommandType::WATCHDOG_REQ_CMD:
        {
            // Message Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::WATCHDOG_REQUEST), seqNo, 0x0004);

            // Message Content
            if (eepCarparkID_ != 0)
            {
                Common::getInstance()->FnAppendUint16LE(msg, eepCarparkID_);
                msg.push_back(0x00);
                msg.push_back(0x00);
            }
            else
            {
                Logger::getInstance()->FnLog("EEP: [TX] Prepare failed | Cmd=WATCHDOG_REQ_CMD | Reason=Missing CarparkID", logFileName_, "EEP");
                success = false;
            }
            break;
        }
        case CommandType::START_REQ_CMD:
        {
            // Message Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::START_REQUEST), seqNo, 0x0000);
            break;
        }
        case CommandType::STOP_REQ_CMD:
        {
            // Message Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::STOP_REQUEST), seqNo, 0x0000);
            break;
        }
        case CommandType::DI_REQ_CMD:
        {
            // Message Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::DI_STATUS_REQUEST), seqNo, 0x0000);
            break;
        }
        case CommandType::DO_REQ_CMD:
        {
            // Message Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::SET_DO_REQUEST), seqNo, 0x0008);

            // Message Content
            if (cmd.data)
            {
                auto data = cmd.data->serialize();
                msg.insert(msg.end(), data.begin(), data.end());
            }
            else
            {
                Logger::getInstance()->FnLog("EEP: [TX] Prepare failed | Cmd=DO_REQ_CMD | Reason=Missing payload", logFileName_, "EEP");
                success = false;
            }
            break;
        }
        case CommandType::SET_DI_PORT_CONFIG_CMD:
        {
            // Message Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::SET_DI_PORT_CONFIGURATION), seqNo, 0x000C);

            // Message Content
            if (cmd.data)
            {
                auto data = cmd.data->serialize();
                msg.insert(msg.end(), data.begin(), data.end());
            }
            else
            {
                Logger::getInstance()->FnLog("EEP: [TX] Prepare failed | Cmd=SET_DI_PORT_CONFIG_CMD | Reason=Missing payload", logFileName_, "EEP");
                success = false;
            }
            break;
        }
        case CommandType::GET_OBU_INFO_REQ_CMD:
        {
            // Message Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::GET_OBU_INFORMATION_REQUEST), seqNo, 0x0000);
            break;
        }
        case CommandType::GET_OBU_INFO_STOP_REQ_CMD:
        {
            // Message Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::GET_OBU_INFORMATION_STOP), seqNo, 0x0000);
            break;
        }
        case CommandType::DEDUCT_REQ_CMD:
        {
            // Message Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::DEDUCT_REQUEST), seqNo, 0x0028);
            
            // Message Content
            if (cmd.data)
            {
                auto data = cmd.data->serialize();
                msg.insert(msg.end(), data.begin(), data.end());
            }
            else
            {
                Logger::getInstance()->FnLog("EEP: [TX] Prepare failed | Cmd=DEDUCT_REQ_CMD | Reason=Missing payload", logFileName_, "EEP");
                success = false;
            }
            break;
        }
        case CommandType::DEDUCT_STOP_REQ_CMD:
        {
            // Message Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::DEDUCT_STOP_REQUEST), seqNo, 0x0014);
            
            // Message Content
            if (cmd.data)
            {
                auto data = cmd.data->serialize();
                msg.insert(msg.end(), data.begin(), data.end());
            }
            else
            {
                Logger::getInstance()->FnLog("EEP: [TX] Prepare failed | Cmd=DEDUCT_STOP_REQ_CMD | Reason=Missing payload", logFileName_, "EEP");
                success = false;
            }
            break;
        }
        case CommandType::TRANSACTION_REQ_CMD:
        {
            // Message Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::TRANSACTION_REQUEST), seqNo, 0x0014);

            // Message Content
            if (cmd.data)
            {
                auto data = cmd.data->serialize();
                msg.insert(msg.end(), data.begin(), data.end());
            }
            else
            {
                Logger::getInstance()->FnLog("EEP: [TX] Prepare failed | Cmd=TRANSACTION_REQ_CMD | Reason=Missing payload", logFileName_, "EEP");
                success = false;
            }
            break;
        }
        case CommandType::CPO_INFO_DISPLAY_REQ_CMD:
        {
            // Message Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::CPO_INFORMATION_DISPLAY_REQUEST), seqNo, 0x0085);
            
            // Message Content
            if (cmd.data)
            {
                auto data = cmd.data->serialize();
                msg.insert(msg.end(), data.begin(), data.end());
            }
            else
            {
                Logger::getInstance()->FnLog("EEP: [TX] Prepare failed | Cmd=CPO_INFO_DISPLAY_REQ_CMD | Reason=Missing payload", logFileName_, "EEP");
                success = false;
            }
            break;
        }
        case CommandType::CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD:
        {
            // Message Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::CARPARK_PROCESS_COMPLETE_NOTIFICATION), seqNo, 0x0014);
            
            // Message Content
            if (cmd.data)
            {
                auto data = cmd.data->serialize();
                msg.insert(msg.end(), data.begin(), data.end());
            }
            else
            {
                Logger::getInstance()->FnLog("EEP: [TX] Prepare failed | Cmd=CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD | Reason=Missing payload", logFileName_, "EEP");
                success = false;
            }
            break;
        }
        case CommandType::DSRC_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD:
        {
            // Message Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::DSRC_PROCESS_COMPLETE_NOTIFICATION), seqNo, 0x0010);
            
            // Message Content
            if (cmd.data)
            {
                auto data = cmd.data->serialize();
                msg.insert(msg.end(), data.begin(), data.end());
            }
            else
            {
                Logger::getInstance()->FnLog("EEP: [TX] Prepare failed | Cmd=DSRC_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD | Reason=Missing payload", logFileName_, "EEP");
                success = false;
            }
            break;
        }
        case CommandType::STOP_REQ_OF_RELATED_INFO_DISTRIBUTION_CMD:
        {
            // Message Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::STOP_REQUEST_OF_RELATED_INFORMATION_DISTRIBUTION), seqNo, 0x0010);
            
            // Message Content
            if (cmd.data)
            {
                auto data = cmd.data->serialize();
                msg.insert(msg.end(), data.begin(), data.end());
            }
            else
            {
                Logger::getInstance()->FnLog("EEP: [TX] Prepare failed | Cmd=STOP_REQ_OF_RELATED_INFO_DISTRIBUTION_CMD | Reason=Missing payload", logFileName_, "EEP");
                success = false;
            }
            break;
        }
        case CommandType::DSRC_STATUS_REQ_CMD:
        {
            // Message Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::DSRC_STATUS_REQUEST), seqNo, 0x0000);
            break;
        }
        case CommandType::TIME_CALIBRATION_REQ_CMD:
        {
            // Message Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::TIME_CALIBRATION_REQUEST), seqNo, 0x0008);
            
            // Message Content
            if (cmd.data)
            {
                auto data = cmd.data->serialize();
                msg.insert(msg.end(), data.begin(), data.end());
            }
            else
            {
                Logger::getInstance()->FnLog("EEP: [TX] Prepare failed | Cmd=TIME_CALIBRATION_REQ_CMD | Reason=Missing payload", logFileName_, "EEP");
                success = false;
            }
            break;
        }
        case CommandType::SET_CARPARK_AVAIL_REQ_CMD:
        {
            // Message Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::SET_PARKING_AVAILABLE), seqNo, 0x0010);
            
            // Message Content
            if (cmd.data)
            {
                auto data = cmd.data->serialize();
                msg.insert(msg.end(), data.begin(), data.end());
            }
            else
            {
                Logger::getInstance()->FnLog("EEP: [TX] Prepare failed | Cmd=SET_CARPARK_AVAIL_REQ_CMD | Reason=Missing payload", logFileName_, "EEP");
                success = false;
            }
            break;
        }
        case CommandType::CD_DOWNLOAD_REQ_CMD:
        {
            // Message Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::CD_DOWNLOAD_REQUEST), seqNo, 0x0000);
            break;
        }
        case CommandType::EEP_RESTART_INQUIRY_REQ_CMD:
        {
            // Messahe Header
            appendMessageHeader(msg, static_cast<uint8_t>(MESSAGE_CODE::EEP_RESTART_INQUIRY_RESPONSE), seqNo, 0x0004);

            // Message Content
            if (cmd.data)
            {
                auto data = cmd.data->serialize();
                msg.insert(msg.end(), data.begin(), data.end());
            }
            else
            {
                Logger::getInstance()->FnLog("EEP: [TX] Prepare failed | Cmd=EEP_RESTART_INQUIRY_REQ_CMD | Reason=Missing payload", logFileName_, "EEP");
                success = false;
            }
            break;
        }
        default:
        {
            Logger::getInstance()->FnLog("EEP: [TX] Prepare failed | Reason=Unsupported command type", logFileName_, "EEP");
            success = false;
            break;
        }
    }

    if (success && !msg.empty())
    {
        // Append checkcode
        uint32_t checkcode = calculateChecksumNoPadding(msg);
        Common::getInstance()->FnAppendUint32LE(msg, checkcode);

        // ACK/NAK echo the peer sequence number and must not consume a
        // controller request sequence number.
        if (usesControllerSequence)
        {
            expectedResponseSeqNo_ = seqNo;
            incrementSequenceNo();
        }
    }

    return { msg, success };
}

void EEPClient::startReconnectTimer()
{
    if (stopping_.load())
    {
        return;
    }

    reconnectTimer_.expires_after(std::chrono::seconds(3));
    reconnectTimer_.async_wait(
        [this](const boost::system::error_code& ec)
        {
            if (ec == boost::asio::error::operation_aborted || stopping_.load())
            {
                return;
            }

            if (ec)
            {
                Logger::getInstance()->FnLog("EEP: [RECONNECT] Timer error | Error=" + ec.message(), logFileName_, "EEP");
                return;
            }

            Logger::getInstance()->FnLog("EEP: [RECONNECT] Timer expired | Action=Connect", logFileName_, "EEP");
            processEvent(EVENT::CONNECT);
        });
}

void EEPClient::startConnectTimer()
{
    if (stopping_.load())
    {
        return;
    }

    connectTimer_.expires_after(std::chrono::seconds(5));
    connectTimer_.async_wait(
        [this](const boost::system::error_code& error)
        {
            handleConnectTimerTimeout(error);
        });
}

void EEPClient::handleConnectTimerTimeout(const boost::system::error_code& error)
{
    if (error == boost::asio::error::operation_aborted || stopping_.load())
    {
        return;
    }

    if (!error)
    {
        Logger::getInstance()->FnLog(
            "EEP: [CONNECT] Timeout | Server=" + serverIP_ +
                " | Port=" + std::to_string(serverPort_),
            logFileName_,
            "EEP");
        processEvent(EVENT::CONNECT_FAIL);
        return;
    }

    Logger::getInstance()->FnLog(
        "EEP: [CONNECT] Timer error | Error=" + error.message(),
        logFileName_,
        "EEP");
    processEvent(EVENT::CONNECT_FAIL);
}

void EEPClient::startSendTimer()
{
    if (stopping_.load())
    {
        return;
    }

    sendTimer_.expires_after(std::chrono::seconds(2));
    sendTimer_.async_wait(
        [this](const boost::system::error_code& error)
        {
            handleSendTimerTimeout(error);
        });
}

void EEPClient::handleSendTimerTimeout(const boost::system::error_code& error)
{
    if (error == boost::asio::error::operation_aborted || stopping_.load())
    {
        return;
    }

    if (!error)
    {
        Logger::getInstance()->FnLog(
            "EEP: [TX] Timeout | Cmd=" + getCommandString(currentCmd_.type),
            logFileName_,
            "EEP");
        processEvent(EVENT::WRITE_TIMEOUT);
        return;
    }

    Logger::getInstance()->FnLog(
        "EEP: [TX] Timer error | Cmd=" + getCommandString(currentCmd_.type) +
            " | Error=" + error.message(),
        logFileName_,
        "EEP");
    processEvent(EVENT::WRITE_TIMEOUT);
}

void EEPClient::startResponseTimer()
{
    if (stopping_.load())
    {
        return;
    }

    responseTimer_.expires_after(std::chrono::seconds(6));
    responseTimer_.async_wait(
        [this](const boost::system::error_code& error)
        {
            handleResponseTimeout(error);
        });
}

void EEPClient::handleResponseTimeout(const boost::system::error_code& error)
{
    if (error == boost::asio::error::operation_aborted || stopping_.load())
    {
        return;
    }

    if (!error)
    {
        Logger::getInstance()->FnLog(
            "EEP: [RSP] Timeout | Cmd=" + getCommandString(currentCmdRequested_.type),
            logFileName_,
            "EEP");
        processEvent(EVENT::RESPONSE_TIMEOUT);
        return;
    }

    Logger::getInstance()->FnLog(
        "EEP: [RSP] Timer error | Cmd=" + getCommandString(currentCmdRequested_.type) +
            " | Error=" + error.message(),
        logFileName_,
        "EEP");
    processEvent(EVENT::RESPONSE_TIMEOUT);
}

void EEPClient::startAckTimer()
{
    if (stopping_.load())
    {
        return;
    }

    ackTimer_.expires_after(std::chrono::seconds(1));
    ackTimer_.async_wait(
        [this](const boost::system::error_code& error)
        {
            handleAckTimeout(error);
        });
}

void EEPClient::handleAckTimeout(const boost::system::error_code& error)
{
    if (error == boost::asio::error::operation_aborted || stopping_.load())
    {
        return;
    }

    if (!error)
    {
        Logger::getInstance()->FnLog(
            "EEP: [ACK] Timeout | Cmd=" + getCommandString(currentCmdRequested_.type),
            logFileName_,
            "EEP");
        processEvent(EVENT::ACK_TIMEOUT);
        return;
    }

    Logger::getInstance()->FnLog(
        "EEP: [ACK] Timer error | Cmd=" + getCommandString(currentCmdRequested_.type) +
            " | Error=" + error.message(),
        logFileName_,
        "EEP");
    processEvent(EVENT::ACK_TIMEOUT);
}

void EEPClient::startWatchdogTimer()
{
    if (stopping_.load())
    {
        return;
    }

    boost::system::error_code ec;
    watchdogTimer_.cancel(ec);

    FnSendWatchdogReq();

    watchdogTimer_.expires_after(std::chrono::seconds(10));
    watchdogTimer_.async_wait(
        [this](const boost::system::error_code& error)
        {
            handleWatchdogTimeout(error);
        });
}

void EEPClient::handleWatchdogTimeout(const boost::system::error_code& error)
{
    if (error == boost::asio::error::operation_aborted || stopping_.load())
    {
        return;
    }

    if (error)
    {
        Logger::getInstance()->FnLog(
            "EEP: [WATCHDOG] Timer error | Error=" + error.message(),
            logFileName_,
            "EEP");
        return;
    }

    startWatchdogTimer();
}

void EEPClient::startHealthStatusTimer()
{
    if (stopping_.load())
    {
        return;
    }

    boost::system::error_code ec;
    healthStatusTimer_.cancel(ec);

    FnSendHealthStatusReq();

    healthStatusTimer_.expires_after(std::chrono::seconds(60));
    healthStatusTimer_.async_wait(
        [this](const boost::system::error_code& error)
        {
            handleHealthStatusTimeout(error);
        });
}

void EEPClient::handleHealthStatusTimeout(const boost::system::error_code& error)
{
    if (error == boost::asio::error::operation_aborted || stopping_.load())
    {
        return;
    }

    if (error)
    {
        Logger::getInstance()->FnLog(
            "EEP: [HEALTH] Timer error | Error=" + error.message(),
            logFileName_,
            "EEP");
        return;
    }

    startHealthStatusTimer();
}

void EEPClient::handleIdleState(EVENT event)
{
    if (event == EVENT::CONNECT)
    {
        Logger::getInstance()->FnLog("EEP: [CONNECT] Attempt | Server=" + serverIP_ + " | Port=" + std::to_string(serverPort_), logFileName_, "EEP");
        eepClientConnect();
        startConnectTimer();
    }
}

void EEPClient::handleConnectingState(EVENT event)
{
    if (event == EVENT::CONNECT_SUCCESS)
    {
        Logger::getInstance()->FnLog("EEP: [CONNECT] Ready | Action=Start protocol timers", logFileName_, "EEP");
        notifyConnectionState(true);
        FnSendStartReq();
        startHealthStatusTimer();
        startWatchdogTimer();
        processEvent(EVENT::CHECK_COMMAND);
    }
    else if (event == EVENT::CONNECT_FAIL)
    {
        Logger::getInstance()->FnLog("EEP: [CONNECT] Unavailable | Action=Schedule reconnect", logFileName_, "EEP");
        notifyConnectionState(false);
        startReconnectTimer();
    }
}

void EEPClient::handleConnectedState(EVENT event)
{
    if (event == EVENT::CHECK_COMMAND)
    {
        // If connection loss, then raise event
        if (!client_->isConnected())
        {
            Logger::getInstance()->FnLog("EEP: [CONNECT] Lost | Action=Reconnect", logFileName_, "EEP");
            processEvent(EVENT::RECONNECT_REQUEST);
            return;
        }

        Logger::getInstance()->FnLog("EEP: [QUEUE] Check | Size=" + std::to_string(commandQueue_.size()), logFileName_, "EEP");
        checkCommandQueue();
    }
    else if (event == EVENT::WRITE_COMMAND)
    {
        // If connection loss, then raise event
        if (!client_->isConnected())
        {
            Logger::getInstance()->FnLog("EEP: [CONNECT] Lost | Action=Reconnect", logFileName_, "EEP");
            processEvent(EVENT::RECONNECT_REQUEST);
            return;
        }

        Logger::getInstance()->FnLog("EEP: [QUEUE] Dispatch | Size=" + std::to_string(commandQueue_.size()), logFileName_, "EEP");
        popFromCommandQueueAndEnqueueWrite();
        startSendTimer();
    }
    else if (event == EVENT::RECONNECT_REQUEST)
    {
        beginReconnect(false);
    }
}

void EEPClient::handleWritingRequestState(EVENT event)
{
    if (event == EVENT::WRITE_COMPLETED)
    {
        if (getCurrentCmd().type == CommandType::ACK || getCurrentCmd().type == CommandType::NAK)
        {
            Logger::getInstance()->FnLog("EEP: [TX] Control response sent | Cmd=" + getCommandString(currentCmd_.type), logFileName_, "EEP");
            processEvent(EVENT::UNSOLICITED_REQUEST_DONE);
        }
        else
        {
            Logger::getInstance()->FnLog("EEP: [TX] Request sent | Cmd=" + getCommandString(currentCmdRequested_.type) + " | Action=Wait ACK", logFileName_, "EEP");
            startAckTimer();
        }
    }
    else if (event == EVENT::WRITE_TIMEOUT)
    {
        Logger::getInstance()->FnLog("EEP: [TX] Failed | Cmd=" + getCommandString(currentCmdRequested_.type) + " | Reason=Write timeout", logFileName_, "EEP");
        if (!client_->isConnected())
        {
            Logger::getInstance()->FnLog("EEP: [CONNECT] Lost | Action=Reconnect", logFileName_, "EEP");
            processEvent(EVENT::RECONNECT_REQUEST);
        }
        handleCommandErrorOrTimeout(getCurrentCmdRequested(), MSG_STATUS::SEND_FAILED);
    }
    else if (event == EVENT::RECONNECT_REQUEST)
    {
        beginReconnect(true);
    }
}

void EEPClient::handleWaitingForResponseState(EVENT event)
{
    if (event == EVENT::ACK_TIMER_CANCELLED_ACK_RECEIVED)
    {
        // This event is emitted only for a positive ACK belonging to a
        // command that requires a follow-up notification.
        startResponseTimer();
    }
    else if (event == EVENT::ACK_AS_RSP_RECEIVED)
    {
        Logger::getInstance()->FnLog("EEP: [RSP] Request completed | Cmd=" + getCommandString(currentCmdRequested_.type) + " | Completion=ACK/NAK/direct response", logFileName_, "EEP");
    }
    else if (event == EVENT::ACK_TIMEOUT)
    {
        expectedResponseSeqNo_.reset();
        Logger::getInstance()->FnLog("EEP: [ACK] Timeout | Cmd=" + getCommandString(currentCmdRequested_.type), logFileName_, "EEP");
        handleCommandErrorOrTimeout(getCurrentCmdRequested(), MSG_STATUS::ACK_TIMEOUT);
    }
    else if (event == EVENT::RESPONSE_TIMER_CANCELLED_RSP_RECEIVED)
    {
        Logger::getInstance()->FnLog("EEP: [RSP] Follow-up completed | Cmd=" + getCommandString(currentCmdRequested_.type), logFileName_, "EEP");
    }
    else if (event == EVENT::RESPONSE_TIMEOUT)
    {
        expectedResponseSeqNo_.reset();
        Logger::getInstance()->FnLog("EEP: [RSP] Timeout | Cmd=" + getCommandString(currentCmdRequested_.type), logFileName_, "EEP");
        if (!client_->isConnected())
        {
            Logger::getInstance()->FnLog("EEP: [CONNECT] Lost | Action=Reconnect", logFileName_, "EEP");
            processEvent(EVENT::RECONNECT_REQUEST);
        }
        handleCommandErrorOrTimeout(getCurrentCmdRequested(), MSG_STATUS::RSP_TIMEOUT);
    }
    else if (event == EVENT::RECONNECT_REQUEST)
    {
        beginReconnect(true);
    }
    else if (event == EVENT::UNSOLICITED_REQUEST_DONE)
    {
        Logger::getInstance()->FnLog("EEP: [TX] Unsolicited control response completed", logFileName_, "EEP");
    }
}

void EEPClient::beginReconnect(bool failActiveRequest)
{
    Logger::getInstance()->FnLog(
        "EEP: [RECONNECT] Begin | FailActiveRequest=" +
            std::string(failActiveRequest ? "true" : "false") +
            " | State=" + stateToString(currentState_),
        logFileName_,
        "EEP");

    boost::system::error_code ec;
    connectTimer_.cancel(ec);
    sendTimer_.cancel(ec);
    ackTimer_.cancel(ec);
    responseTimer_.cancel(ec);
    watchdogTimer_.cancel(ec);
    healthStatusTimer_.cancel(ec);

    expectedResponseSeqNo_.reset();

    if (failActiveRequest &&
        currentCmdRequested_.type != CommandType::ACK &&
        currentCmdRequested_.type != CommandType::NAK)
    {
        handleCommandErrorOrTimeout(currentCmdRequested_, MSG_STATUS::SEND_FAILED);
    }

    clearCommandQueue();
    notifyConnectionState(false);
    eepClientClose();
    startReconnectTimer();
}

void EEPClient::handleCommandErrorOrTimeout(Command cmd, MSG_STATUS msgStatus)
{
    EEPEventWrapper eepEvt;
    std::string eventMsg = "";

    switch (cmd.type)
    {
        case CommandType::ACK:
        {
            Logger::getInstance()->FnLog("EEP: [TX] ACK failed | Action=Ignored", logFileName_, "EEP");
            break;
        }
        case CommandType::NAK:
        {
            Logger::getInstance()->FnLog("EEP: [TX] NAK failed | Action=Ignored", logFileName_, "EEP");
            break;
        }
        case CommandType::START_REQ_CMD:
        case CommandType::STOP_REQ_CMD:
        case CommandType::DI_REQ_CMD:
        case CommandType::DO_REQ_CMD:
        case CommandType::SET_DI_PORT_CONFIG_CMD:
        case CommandType::GET_OBU_INFO_REQ_CMD:
        case CommandType::GET_OBU_INFO_STOP_REQ_CMD:
        case CommandType::DEDUCT_REQ_CMD:
        case CommandType::DEDUCT_STOP_REQ_CMD:
        case CommandType::TRANSACTION_REQ_CMD:
        case CommandType::CPO_INFO_DISPLAY_REQ_CMD:
        case CommandType::CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD:
        case CommandType::DSRC_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD:
        case CommandType::STOP_REQ_OF_RELATED_INFO_DISTRIBUTION_CMD:
        case CommandType::DSRC_STATUS_REQ_CMD:
        case CommandType::TIME_CALIBRATION_REQ_CMD:
        case CommandType::SET_CARPARK_AVAIL_REQ_CMD:
        case CommandType::CD_DOWNLOAD_REQ_CMD:
        case CommandType::EEP_RESTART_INQUIRY_REQ_CMD:
        {
            eepEvt.commandReqType = static_cast<uint8_t>(cmd.type);
            eepEvt.messageCode = static_cast<uint8_t>(MESSAGE_CODE::NAK);
            eepEvt.messageStatus = static_cast<uint32_t>(msgStatus);
            eepEvt.payload = "{}";

            eventMsg = boost::json::serialize(eepEvt.to_json());
            break;
        }
        case CommandType::WATCHDOG_REQ_CMD:
        {
            // If no watchdog response > 4 times (40s), need to re-establish the TCP connection
            watchdogMissedRspCount_++;
            if (watchdogMissedRspCount_ >= 4)
            {
                watchdogMissedRspCount_ = 0;
                processEvent(EVENT::RECONNECT_REQUEST);
            }
            break;
        }
        default:
        {
            Logger::getInstance()->FnLog("EEP: [CMD] Failure not mapped | Cmd=" + getCommandString(cmd.type), logFileName_, "EEP");
            break;
        }
    }

    if (!eventMsg.empty())
    {
        EventManager::getInstance()->FnEnqueueEvent("Evt_handleEEPClientResponse", eventMsg);
        Logger::getInstance()->FnLog("EEP: [EVENT] Queued | Name=Evt_handleEEPClientResponse", logFileName_, "EEP");
    }
}

void EEPClient::notifyConnectionState(bool connected)
{
    if (lastConnectionState_ != connected)
    {
        lastConnectionState_ = connected;
        EventManager::getInstance()->FnEnqueueEvent("Evt_handleEEPClientConnectionState", connected);
        Logger::getInstance()->FnLog("EEP: [EVENT] Queued | Name=Evt_handleEEPClientConnectionState | Connected=" + std::string(connected ? "true" : "false"), logFileName_, "EEP");
    }
}

void EEPClient::processDSRCFeTx(const MessageHeader& header, const transactionData& txData)
{
    std::vector<uint8_t> txRec;

    auto toBCD = [](uint8_t value) -> uint8_t {
        return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
    };

    // *** Detail Record ***
    // Record Type
    txRec.push_back('D');
    // Destination ID
    txRec.push_back(header.destinationID_);
    // Source ID
    txRec.push_back(header.sourceID_);
    // Data Type Code
    txRec.push_back(header.dataTypeCode_);
    // Date Time
    txRec.push_back(((header.year_ / 1000) << 4) | ((header.year_ / 100) % 10));
    txRec.push_back(((header.year_ / 10) % 10 << 4) | ((header.year_ % 10)));
    txRec.push_back(toBCD(header.month_));
    txRec.push_back(toBCD(header.day_));
    txRec.push_back(toBCD(header.hour_));
    txRec.push_back(toBCD(header.minute_));
    txRec.push_back(toBCD(header.second_));
    txRec.push_back(0x00);
    txRec.push_back(0x00);
    // Sequence Number
    txRec.push_back((header.seqNo_ >> 8) & 0xFF);
    txRec.push_back(header.seqNo_ & 0xFF);
    // Data Length
    txRec.push_back(((header.dataLen_ / 1000) << 4) | ((header.dataLen_ / 100) % 10));
    txRec.push_back(((header.dataLen_ / 10) % 10 << 4) | ((header.dataLen_ % 10)));
    // Deduct command serial number
    txRec.push_back((txData.deductCommandSerialNum >> 8) & 0xFF);
    txRec.push_back(txData.deductCommandSerialNum & 0xFF);
    // Protocol Version
    txRec.push_back(txData.protocolVer);
    // Result of Deduction
    txRec.push_back(txData.resultDeduction);
    // SubSystem Label
    txRec.push_back(((txData.subSystemLabel / 10000000) << 4) | ((txData.subSystemLabel / 1000000) % 10));
    txRec.push_back(((txData.subSystemLabel / 100000) % 10 << 4) | ((txData.subSystemLabel / 10000) % 10));
    txRec.push_back(((txData.subSystemLabel / 1000) % 10 << 4) | ((txData.subSystemLabel / 100) % 10));
    txRec.push_back(((txData.subSystemLabel / 10) % 10 << 4) | (txData.subSystemLabel % 10));
    // OBU Label
    // LTA Document Error - Not 10 ASCII, is 5 Numeric
    txRec.push_back((txData.obuLabel >> 32) & 0xFF);
    txRec.push_back((txData.obuLabel >> 24) & 0xFF);
    txRec.push_back((txData.obuLabel >> 16) & 0xFF);
    txRec.push_back((txData.obuLabel >> 8) & 0xFF);
    txRec.push_back(txData.obuLabel & 0xFF);
    /*
    for (int shift = 36; shift >= 0; shift-=4)
    {
        uint8_t nibble = (txData.obuLabel >> shift) & 0x0F;

        // Convert nibble to ASCII hex
        char asciiChar;
        if (nibble < 10)
            asciiChar = '0' + nibble;
        else
            asciiChar = 'A' + (nibble - 10);

        txRec.push_back(static_cast<uint8_t>(asciiChar));
    }
    */
    // Vehicle Number
    constexpr size_t VEHICLE_NUM_LEN = 13;

    if (!txData.vechicleNumber.empty())
    {
        std::vector<uint8_t> tempVecNum(txData.vechicleNumber.begin(), txData.vechicleNumber.end());
        std::replace(tempVecNum.begin(), tempVecNum.end(), static_cast<uint8_t>(0x00), static_cast<uint8_t>(0x20));
        // pad if shorter
        if (tempVecNum.size() < VEHICLE_NUM_LEN)
        {
            tempVecNum.resize(VEHICLE_NUM_LEN, 0x20);
        }
        txRec.insert(txRec.end(), tempVecNum.begin(), tempVecNum.end());
    }
    else
    {
        txRec.insert(txRec.end(), VEHICLE_NUM_LEN, 0x20);
    }
    // Transaction Route
    txRec.push_back(txData.transactionRoute);
    // Frontend Payment Violation
    txRec.push_back(txData.frontendPaymentViolation);
    // Transaction Type
    txRec.push_back(txData.transactionType);
    // Parking Start Date Time
    txRec.push_back(((txData.parkingStartYear / 1000) << 4) | ((txData.parkingStartYear / 100) % 10));
    txRec.push_back(((txData.parkingStartYear / 10) % 10 << 4) | ((txData.parkingStartYear % 10)));
    txRec.push_back(toBCD(txData.parkingStartMonth));
    txRec.push_back(toBCD(txData.parkingStartDay));
    txRec.push_back(toBCD(txData.parkingStartHour));
    txRec.push_back(toBCD(txData.parkingStartMinute));
    txRec.push_back(toBCD(txData.parkingStartSecond));
    txRec.push_back(0x00);
    txRec.push_back(0x00);
    // Parking End Date Time
    txRec.push_back(((txData.parkingEndYear / 1000) << 4) | ((txData.parkingEndYear / 100) % 10));
    txRec.push_back(((txData.parkingEndYear / 10) % 10 << 4) | ((txData.parkingEndYear % 10)));
    txRec.push_back(toBCD(txData.parkingEndMonth));
    txRec.push_back(toBCD(txData.parkingEndDay));
    txRec.push_back(toBCD(txData.parkingEndHour));
    txRec.push_back(toBCD(txData.parkingEndMinute));
    txRec.push_back(toBCD(txData.parkingEndSecond));
    txRec.push_back(0x00);
    txRec.push_back(0x00);
    // Payment Fee
    txRec.push_back(0x00);
    txRec.push_back((txData.paymentFee / 1000000000) << 4 | ((txData.paymentFee / 100000000) % 10));
    txRec.push_back((txData.paymentFee / 10000000) % 10 << 4 | ((txData.paymentFee / 1000000) % 10));
    txRec.push_back((txData.paymentFee / 100000) % 10 << 4 | ((txData.paymentFee / 10000) % 10));
    txRec.push_back((txData.paymentFee / 1000) % 10 << 4 | ((txData.paymentFee / 100) % 10));
    txRec.push_back((txData.paymentFee / 10) % 10 << 4 | (txData.paymentFee % 10));
    // FEP Date Time
    txRec.push_back((txData.fepTime >> 48) & 0xFF);
    txRec.push_back((txData.fepTime >> 40) & 0xFF);
    txRec.push_back((txData.fepTime >> 32) & 0xFF);
    txRec.push_back((txData.fepTime >> 24) & 0xFF);
    txRec.push_back((txData.fepTime >> 16) & 0xFF);
    txRec.push_back((txData.fepTime >> 8) & 0xFF);
    txRec.push_back(txData.fepTime & 0xFF);
    txRec.push_back(0x00);
    txRec.push_back(0x00);
    // TRP
    txRec.push_back((txData.trp >> 24) & 0xFF);
    txRec.push_back((txData.trp >> 16) & 0xFF);
    txRec.push_back((txData.trp >> 8) & 0xFF);
    txRec.push_back(txData.trp & 0xFF);
    // Inidication of Last AutoLoad
    txRec.push_back(txData.indicationLastAutoLoad);
    // CAN
    constexpr size_t CAN_LEN = 8; // <-- adjust to spec

    if (!txData.can.empty())
    {
        std::vector<uint8_t> tempCan(txData.can.begin(), txData.can.end());
        if (tempCan.size() < CAN_LEN)
        {
            tempCan.resize(CAN_LEN, 0x20);
        }
        txRec.insert(txRec.end(), tempCan.begin(), tempCan.end());
    }
    else
    {
        txRec.insert(txRec.end(), CAN_LEN, 0x20);
    }
    // Last Credit Transaction Header
    txRec.push_back((txData.lastCreditTransactionHeader >> 56) & 0xFF);
    txRec.push_back((txData.lastCreditTransactionHeader >> 48) & 0xFF);
    txRec.push_back((txData.lastCreditTransactionHeader >> 40) & 0xFF);
    txRec.push_back((txData.lastCreditTransactionHeader >> 32) & 0xFF);
    txRec.push_back((txData.lastCreditTransactionHeader >> 24) & 0xFF);
    txRec.push_back((txData.lastCreditTransactionHeader >> 16) & 0xFF);
    txRec.push_back((txData.lastCreditTransactionHeader >> 8) & 0xFF);
    txRec.push_back(txData.lastCreditTransactionHeader & 0xFF);
    // Last Credit Transaction TRP
    txRec.push_back((txData.lastCreditTransactionTRP >> 24) & 0xFF);
    txRec.push_back((txData.lastCreditTransactionTRP >> 16) & 0xFF);
    txRec.push_back((txData.lastCreditTransactionTRP >> 8) & 0xFF);
    txRec.push_back(txData.lastCreditTransactionTRP & 0xFF);
    // Purse Balance Before Transaction
    txRec.push_back(0x00);
    txRec.push_back((txData.purseBalanceBeforeTransaction / 1000000000) << 4 | ((txData.purseBalanceBeforeTransaction / 100000000) % 10));
    txRec.push_back((txData.purseBalanceBeforeTransaction / 10000000) % 10 << 4 | ((txData.purseBalanceBeforeTransaction / 1000000) % 10));
    txRec.push_back((txData.purseBalanceBeforeTransaction / 100000) % 10 << 4 | ((txData.purseBalanceBeforeTransaction / 10000) % 10));
    txRec.push_back((txData.purseBalanceBeforeTransaction / 1000) % 10 << 4 | ((txData.purseBalanceBeforeTransaction / 100) % 10));
    txRec.push_back((txData.purseBalanceBeforeTransaction / 10) % 10 << 4 | (txData.purseBalanceBeforeTransaction % 10));
    // Bad Debt Counter
    txRec.push_back(txData.badDebtCounter);
    // Transaction Status
    txRec.push_back(txData.transactionStatus);
    // Debit Option
    txRec.push_back(txData.debitOption);
    // AutoLoad Amount
    txRec.push_back(0x00);
    txRec.push_back((txData.autoLoadAmount / 1000000000) << 4 | ((txData.autoLoadAmount / 100000000) % 10));
    txRec.push_back((txData.autoLoadAmount / 10000000) % 10 << 4 | ((txData.autoLoadAmount / 1000000) % 10));
    txRec.push_back((txData.autoLoadAmount / 100000) % 10 << 4 | ((txData.autoLoadAmount / 10000) % 10));
    txRec.push_back((txData.autoLoadAmount / 1000) % 10 << 4 | ((txData.autoLoadAmount / 100) % 10));
    txRec.push_back((txData.autoLoadAmount / 10) % 10 << 4 | (txData.autoLoadAmount % 10));
    // Counter Data
    txRec.push_back((txData.counterData >> 56) & 0xFF);
    txRec.push_back((txData.counterData >> 48) & 0xFF);
    txRec.push_back((txData.counterData >> 40) & 0xFF);
    txRec.push_back((txData.counterData >> 32) & 0xFF);
    txRec.push_back((txData.counterData >> 24) & 0xFF);
    txRec.push_back((txData.counterData >> 16) & 0xFF);
    txRec.push_back((txData.counterData >> 8) & 0xFF);
    txRec.push_back(txData.counterData & 0xFF);
    // Signed Certificate
    txRec.push_back((txData.signedCertificate >> 56) & 0xFF);
    txRec.push_back((txData.signedCertificate >> 48) & 0xFF);
    txRec.push_back((txData.signedCertificate >> 40) & 0xFF);
    txRec.push_back((txData.signedCertificate >> 32) & 0xFF);
    txRec.push_back((txData.signedCertificate >> 24) & 0xFF);
    txRec.push_back((txData.signedCertificate >> 16) & 0xFF);
    txRec.push_back((txData.signedCertificate >> 8) & 0xFF);
    txRec.push_back(txData.signedCertificate & 0xFF);
    // Purse Balance after transaction
    txRec.push_back(0x00);
    txRec.push_back((txData.purseBalanceAfterTransaction / 1000000000) << 4 | ((txData.purseBalanceAfterTransaction / 100000000) % 10));
    txRec.push_back((txData.purseBalanceAfterTransaction / 10000000) % 10 << 4 | ((txData.purseBalanceAfterTransaction / 1000000) % 10));
    txRec.push_back((txData.purseBalanceAfterTransaction / 100000) % 10 << 4 | ((txData.purseBalanceAfterTransaction / 10000) % 10));
    txRec.push_back((txData.purseBalanceAfterTransaction / 1000) % 10 << 4 | ((txData.purseBalanceAfterTransaction / 100) % 10));
    txRec.push_back((txData.purseBalanceAfterTransaction / 10) % 10 << 4 | (txData.purseBalanceAfterTransaction % 10));
    // Last Transaction Debit Option
    txRec.push_back(txData.lastTransactionDebitOptionbyte);
    // Previous Transaction Header
    txRec.push_back((txData.previousTransactionHeader >> 56) & 0xFF);
    txRec.push_back((txData.previousTransactionHeader >> 48) & 0xFF);
    txRec.push_back((txData.previousTransactionHeader >> 40) & 0xFF);
    txRec.push_back((txData.previousTransactionHeader >> 32) & 0xFF);
    txRec.push_back((txData.previousTransactionHeader >> 24) & 0xFF);
    txRec.push_back((txData.previousTransactionHeader >> 16) & 0xFF);
    txRec.push_back((txData.previousTransactionHeader >> 8) & 0xFF);
    txRec.push_back(txData.previousTransactionHeader & 0xFF);
    // Previous TRP
    txRec.push_back((txData.previousTRP >> 24) & 0xFF);
    txRec.push_back((txData.previousTRP >> 16) & 0xFF);
    txRec.push_back((txData.previousTRP >> 8) & 0xFF);
    txRec.push_back(txData.previousTRP & 0xFF);
    // Previous Purse balance
    txRec.push_back(0x00);
    txRec.push_back((txData.previousPurseBalance / 1000000000) << 4 | ((txData.previousPurseBalance / 100000000) % 10));
    txRec.push_back((txData.previousPurseBalance / 10000000) % 10 << 4 | ((txData.previousPurseBalance / 1000000) % 10));
    txRec.push_back((txData.previousPurseBalance / 100000) % 10 << 4 | ((txData.previousPurseBalance / 10000) % 10));
    txRec.push_back((txData.previousPurseBalance / 1000) % 10 << 4 | ((txData.previousPurseBalance / 100) % 10));
    txRec.push_back((txData.previousPurseBalance / 10) % 10 << 4 | (txData.previousPurseBalance % 10));
    // Previous Counter Data
    txRec.push_back((txData.previousCounterData >> 56) & 0xFF);
    txRec.push_back((txData.previousCounterData >> 48) & 0xFF);
    txRec.push_back((txData.previousCounterData >> 40) & 0xFF);
    txRec.push_back((txData.previousCounterData >> 32) & 0xFF);
    txRec.push_back((txData.previousCounterData >> 24) & 0xFF);
    txRec.push_back((txData.previousCounterData >> 16) & 0xFF);
    txRec.push_back((txData.previousCounterData >> 8) & 0xFF);
    txRec.push_back(txData.previousCounterData & 0xFF);
    // Previous Transaction Signed Certificate
    txRec.push_back((txData.previousTransactionSignedCertificate >> 56) & 0xFF);
    txRec.push_back((txData.previousTransactionSignedCertificate >> 48) & 0xFF);
    txRec.push_back((txData.previousTransactionSignedCertificate >> 40) & 0xFF);
    txRec.push_back((txData.previousTransactionSignedCertificate >> 32) & 0xFF);
    txRec.push_back((txData.previousTransactionSignedCertificate >> 24) & 0xFF);
    txRec.push_back((txData.previousTransactionSignedCertificate >> 16) & 0xFF);
    txRec.push_back((txData.previousTransactionSignedCertificate >> 8) & 0xFF);
    txRec.push_back(txData.previousTransactionSignedCertificate & 0xFF);
    // Previous Purse Status
    txRec.push_back(txData.previousPurseStatus);
    // RFU
    txRec.insert(txRec.end(), 31, static_cast<uint8_t>(' '));

    /*
    Logger::getInstance()->FnLog("In Hex (len=" + std::to_string(txRec.size()) + "): ", logFileName_, "EEP");
    std::ostringstream oss;
    for (uint8_t b : txRec)
    {
        oss << std::hex << std::uppercase << std::setw(2) 
              << std::setfill('0') << static_cast<int>(b) << " ";
    }
    oss << std::endl;
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

    Logger::getInstance()->FnLog("In String (len=" + std::to_string(txRec.size()) + "): ", logFileName_, "EEP");
    std::string str(txRec.begin(), txRec.end());
    Logger::getInstance()->FnLog(str, logFileName_, "EEP");
    */
    if (filePool_)
    {
        const std::string cpoId = operation::getInstance()->tParas.gsCPOID;
        const std::string carparkId = operation::getInstance()->tParas.gsCPID;

        boost::asio::post(
            *filePool_,
            [this, record = std::move(txRec), cpoId, carparkId]() mutable
            {
                writeDSRCFeOrBeTxToCollFile(true, record, cpoId, carparkId);
            });
    }
}

void EEPClient::processDSRCBeTx(const MessageHeader& header, const transactionData& txData)
{
    std::vector<uint8_t> BeTxRec;

    auto toBCD = [](uint8_t value) -> uint8_t {
        return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
    };

    // *** Detail Record ***
    // Record Type
    BeTxRec.push_back('D');
    // Destination ID
    BeTxRec.push_back(header.destinationID_);
    // Source ID
    BeTxRec.push_back(header.sourceID_);
    // Data Type Code
    BeTxRec.push_back(header.dataTypeCode_);
    // Date Time
    BeTxRec.push_back(((header.year_ / 1000) << 4) | ((header.year_ / 100) % 10));
    BeTxRec.push_back(((header.year_ / 10) % 10 << 4) | ((header.year_ % 10)));
    BeTxRec.push_back(toBCD(header.month_));
    BeTxRec.push_back(toBCD(header.day_));
    BeTxRec.push_back(toBCD(header.hour_));
    BeTxRec.push_back(toBCD(header.minute_));
    BeTxRec.push_back(toBCD(header.second_));
    BeTxRec.push_back(0x00);
    BeTxRec.push_back(0x00);
    // Sequence Number
    BeTxRec.push_back((header.seqNo_ >> 8) & 0xFF);
    BeTxRec.push_back(header.seqNo_ & 0xFF);
    // Data Length
    BeTxRec.push_back(((header.dataLen_ / 1000) << 4) | ((header.dataLen_ / 100) % 10));
    BeTxRec.push_back(((header.dataLen_ / 10) % 10 << 4) | ((header.dataLen_ % 10)));
    // Deduct command serial number
    BeTxRec.push_back((txData.deductCommandSerialNum >> 8) & 0xFF);
    BeTxRec.push_back(txData.deductCommandSerialNum & 0xFF);
    // Protocol Version
    BeTxRec.push_back(txData.protocolVer);
    // Result of Deduction
    BeTxRec.push_back(txData.resultDeduction);
    // SubSystem Label
    BeTxRec.push_back(((txData.subSystemLabel / 10000000) << 4) | ((txData.subSystemLabel / 1000000) % 10));
    BeTxRec.push_back(((txData.subSystemLabel / 100000) % 10 << 4) | ((txData.subSystemLabel / 10000) % 10));
    BeTxRec.push_back(((txData.subSystemLabel / 1000) % 10 << 4) | ((txData.subSystemLabel / 100) % 10));
    BeTxRec.push_back(((txData.subSystemLabel / 10) % 10 << 4) | (txData.subSystemLabel % 10));
    // OBU Label
    // LTA Document Error - Not 10 ASCII, is 5 Numeric
    BeTxRec.push_back((txData.obuLabel >> 32) & 0xFF);
    BeTxRec.push_back((txData.obuLabel >> 24) & 0xFF);
    BeTxRec.push_back((txData.obuLabel >> 16) & 0xFF);
    BeTxRec.push_back((txData.obuLabel >> 8) & 0xFF);
    BeTxRec.push_back(txData.obuLabel & 0xFF);
    /* 
    for (int shift = 36; shift >= 0; shift-=4)
    {
        uint8_t nibble = (txData.obuLabel >> shift) & 0x0F;

        // Convert nibble to ASCII hex
        char asciiChar;
        if (nibble < 10)
            asciiChar = '0' + nibble;
        else
            asciiChar = 'A' + (nibble - 10);

        BeTxRec.push_back(static_cast<uint8_t>(asciiChar));
    }
    */
    // Vehicle Number
    constexpr size_t VEHICLE_NUM_LEN = 13;

    if (!txData.vechicleNumber.empty())
    {
        std::vector<uint8_t> tempVecNum(txData.vechicleNumber.begin(), txData.vechicleNumber.end());
        std::replace(tempVecNum.begin(), tempVecNum.end(), static_cast<uint8_t>(0x00), static_cast<uint8_t>(0x20));
        // pad if shorter
        if (tempVecNum.size() < VEHICLE_NUM_LEN)
        {
            tempVecNum.resize(VEHICLE_NUM_LEN, 0x20);
        }
        BeTxRec.insert(BeTxRec.end(), tempVecNum.begin(), tempVecNum.end());
    }
    else
    {
        BeTxRec.insert(BeTxRec.end(), VEHICLE_NUM_LEN, 0x20);
    }
    // Transaction Route
    BeTxRec.push_back(txData.transactionRoute);
    // Backend Payment Violation
    BeTxRec.push_back(txData.backendPaymentViolation);
    // Transaction Type
    BeTxRec.push_back(txData.transactionType);
    // Parking Start Date Time
    BeTxRec.push_back(((txData.parkingStartYear / 1000) << 4) | ((txData.parkingStartYear / 100) % 10));
    BeTxRec.push_back(((txData.parkingStartYear / 10) % 10 << 4) | ((txData.parkingStartYear % 10)));
    BeTxRec.push_back(toBCD(txData.parkingStartMonth));
    BeTxRec.push_back(toBCD(txData.parkingStartDay));
    BeTxRec.push_back(toBCD(txData.parkingStartHour));
    BeTxRec.push_back(toBCD(txData.parkingStartMinute));
    BeTxRec.push_back(toBCD(txData.parkingStartSecond));
    BeTxRec.push_back(0x00);
    BeTxRec.push_back(0x00);
    // Parking End Date Time
    BeTxRec.push_back(((txData.parkingEndYear / 1000) << 4) | ((txData.parkingEndYear / 100) % 10));
    BeTxRec.push_back(((txData.parkingEndYear / 10) % 10 << 4) | ((txData.parkingEndYear % 10)));
    BeTxRec.push_back(toBCD(txData.parkingEndMonth));
    BeTxRec.push_back(toBCD(txData.parkingEndDay));
    BeTxRec.push_back(toBCD(txData.parkingEndHour));
    BeTxRec.push_back(toBCD(txData.parkingEndMinute));
    BeTxRec.push_back(toBCD(txData.parkingEndSecond));
    BeTxRec.push_back(0x00);
    BeTxRec.push_back(0x00);
    // Payment Fee
    BeTxRec.push_back(0x00);
    BeTxRec.push_back((txData.paymentFee / 1000000000) << 4 | ((txData.paymentFee / 100000000) % 10));
    BeTxRec.push_back((txData.paymentFee / 10000000) % 10 << 4 | ((txData.paymentFee / 1000000) % 10));
    BeTxRec.push_back((txData.paymentFee / 100000) % 10 << 4 | ((txData.paymentFee / 10000) % 10));
    BeTxRec.push_back((txData.paymentFee / 1000) % 10 << 4 | ((txData.paymentFee / 100) % 10));
    BeTxRec.push_back((txData.paymentFee / 10) % 10 << 4 | (txData.paymentFee % 10));
    // BepPaymentFeeAmount
    BeTxRec.push_back(0x00);
    BeTxRec.push_back(0x00);
    BeTxRec.push_back(0x00);
    BeTxRec.push_back((txData.paymentFee / 100000) % 10 << 4 | ((txData.paymentFee / 10000) % 10));
    BeTxRec.push_back((txData.paymentFee / 1000) % 10 << 4 | ((txData.paymentFee / 100) % 10));
    BeTxRec.push_back((txData.paymentFee / 10) % 10 << 4 | (txData.paymentFee % 10));
    // BepTimeOfReport
    constexpr size_t BEPTIME_LEN = 9;

    std::vector<uint8_t> tempBepTime(BEPTIME_LEN, 0x00); // pre-fill with 0x00
    size_t copyStart = BEPTIME_LEN - txData.bepTimeOfReport.size();
    std::copy(txData.bepTimeOfReport.begin(), txData.bepTimeOfReport.end(), tempBepTime.begin() + copyStart);
    BeTxRec.insert(BeTxRec.end(), tempBepTime.begin(), tempBepTime.end());
    // chargeReportCounter
    BeTxRec.push_back(((txData.chargeReportCounter / 10000000) << 4) | ((txData.chargeReportCounter / 1000000) % 10));
    BeTxRec.push_back(((txData.chargeReportCounter / 100000) % 10 << 4) | ((txData.chargeReportCounter / 10000) % 10));
    BeTxRec.push_back(((txData.chargeReportCounter / 1000) % 10 << 4) | ((txData.chargeReportCounter / 100) % 10));
    BeTxRec.push_back(((txData.chargeReportCounter / 10) % 10 << 4) | (txData.chargeReportCounter % 10));
    // BepKeyVersion
    BeTxRec.push_back(txData.bepKeyVersion);
    // BepCertificate
    constexpr size_t BEP_CERT_LEN = 280;

    if (!txData.bepCertificate.empty())
    {
        std::vector<uint8_t> tempBepCert(txData.bepCertificate.begin(), txData.bepCertificate.end());
        // pad if shorter
        if (tempBepCert.size() < BEP_CERT_LEN)
        {
            tempBepCert.resize(BEP_CERT_LEN, 0x20);
        }
        BeTxRec.insert(BeTxRec.end(), tempBepCert.begin(), tempBepCert.end());
    }
    else
    {
        BeTxRec.insert(BeTxRec.end(), BEP_CERT_LEN, 0x20);
    }
    // RFU
    BeTxRec.insert(BeTxRec.end(), 42, static_cast<uint8_t>(' '));

    /*
    Logger::getInstance()->FnLog("In Hex (len=" + std::to_string(BeTxRec.size()) + "): ", logFileName_, "EEP");
    std::ostringstream oss;
    for (uint8_t b : BeTxRec)
    {
        oss << std::hex << std::uppercase << std::setw(2) 
              << std::setfill('0') << static_cast<int>(b) << " ";
    }
    oss << std::endl;
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

    Logger::getInstance()->FnLog("In String (len=" + std::to_string(BeTxRec.size()) + "): ", logFileName_, "EEP");
    std::string str(BeTxRec.begin(), BeTxRec.end());
    Logger::getInstance()->FnLog(str, logFileName_, "EEP");
    */
    if (filePool_)
    {
        const std::string cpoId = operation::getInstance()->tParas.gsCPOID;
        const std::string carparkId = operation::getInstance()->tParas.gsCPID;

        boost::asio::post(
            *filePool_,
            [this, record = std::move(BeTxRec), cpoId, carparkId]() mutable
            {
                writeDSRCFeOrBeTxToCollFile(false, record, cpoId, carparkId);
            });
    }
}

void EEPClient::writeDSRCFeOrBeTxToCollFile(bool isFrontendTx, const std::vector<uint8_t>& data, const std::string& cpoId, const std::string& carparkId)
{
    const std::string recordHex = bytesToHexString(data);

    try
    {
        std::string settleFile = "";
        std::string settleFileName = "";

        if (!boost::filesystem::exists(LOCAL_EEP_SETTLEMENT_FOLDER_PATH))
        {
            std::ostringstream oss;
            oss << "EEP: [FILE] Directory missing | Path=" << LOCAL_EEP_SETTLEMENT_FOLDER_PATH << " | Action=Create";
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

            if (!(boost::filesystem::create_directories(LOCAL_EEP_SETTLEMENT_FOLDER_PATH)))
            {
                std::ostringstream oss;
                oss << "EEP: [FILE] Create directory failed | Path=" << LOCAL_EEP_SETTLEMENT_FOLDER_PATH;
                Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");
                Logger::getInstance()->FnLog("EEP: [FILE] Record | Hex=" + recordHex, logFileName_, "EEP");
                return;
            }
        }

        std::ostringstream ossFilename;
        auto toMax16 = [](const std::string& s)
        {
            return (s.size() >= 16) ? s.substr(0, 16) : s;
        };

        if (isFrontendTx)
        {
            ossFilename << "EEP_" << toMax16(cpoId)
                        << "_" << std::setw(5) << std::setfill('0') << carparkId
                        << "_FE_" << Common::getInstance()->FnGetDateTimeFormat_yyyymmdd()
                        << "_" << std::setw(2) << std::setfill('0') << std::dec << iStationID_
                        << Common::getInstance()->FnGetDateTimeFormat_hh() << ".dsr";
        }
        else
        {
            ossFilename << "EEP_" << toMax16(cpoId)
                        << "_" << std::setw(5) << std::setfill('0') << carparkId
                        << "_BE_"
                        << std::setw(2) << std::setfill('0') << std::dec << iStationID_
                        << "_" << Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmmss() << ".dsr";   
        }
        settleFileName = ossFilename.str();
        settleFile = LOCAL_EEP_SETTLEMENT_FOLDER_PATH + "/" + ossFilename.str();

        // Write data to local
        Logger::getInstance()->FnLog("EEP: [FILE] Write | Type=" + std::string(isFrontendTx ? "FE" : "BE") + " | Path=" + settleFile + " | Bytes=" + std::to_string(data.size()), logFileName_, "EEP");

        std::ofstream ofs;
        if (!boost::filesystem::exists(settleFile))
        {
            ofs.open(settleFile, std::ios::binary | std::ios::out);

            auto toBCD = [](uint8_t value) -> uint8_t {
                return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
            };

            // Header Record
            std::vector<uint8_t> header;
            // Record Type
            header.push_back('H');
            // EEP Car Park ID
            std::ostringstream tempCarParkIDoss;
            tempCarParkIDoss << std::setw(5) << std::setfill('0') << carparkId;
            std::string tempCarParkID = tempCarParkIDoss.str();
            header.insert(header.end(), tempCarParkID.begin(), tempCarParkID.end());
            // Date and Time
            auto now = std::chrono::system_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

            std::time_t timer = std::chrono::system_clock::to_time_t(now);
            struct tm timeinfo = {};
            localtime_r(&timer, &timeinfo);

            uint16_t year           = timeinfo.tm_year + 1900;
            uint8_t month           = timeinfo.tm_mon + 1;
            uint8_t day             = timeinfo.tm_mday;
            uint8_t hour            = timeinfo.tm_hour;
            uint8_t minute          = timeinfo.tm_min;
            uint8_t second          = timeinfo.tm_sec;
            uint16_t millisecond    = static_cast<uint16_t>(ms.count());
            uint8_t ms_hundreds = (millisecond / 100) % 10;
            uint8_t ms_tens     = (millisecond / 10) % 10;
            uint8_t ms_ones     = millisecond % 10;

            header.push_back(((year / 1000) << 4) | ((year / 100) % 10));
            header.push_back(((year / 10) % 10 << 4) | ((year % 10)));
            header.push_back(toBCD(month));
            header.push_back(toBCD(day));
            header.push_back(toBCD(hour));
            header.push_back(toBCD(minute));
            header.push_back(toBCD(second));
            header.push_back((ms_hundreds << 4) | ms_tens);
            header.push_back(ms_ones << 4);
            // Collection File Name
            auto padded = [&](const std::string& s, std::size_t width)
            {
                std::ostringstream oss;
                oss << std::left << std::setw(width) << std::setfill(' ') << s;
                return oss.str();
            };

            const std::string fname = ossFilename.str();
            const std::size_t fnameLen = fname.length();
            std::string finalName = "";
            if (isFrontendTx)
            {
                finalName = padded(fname, 48);
            }
            else
            {
                finalName = (fnameLen > 48) ? padded(fname, fnameLen) : padded(fname, 48);
            }
            header.insert(header.end(), finalName.begin(), finalName.end());
            // RFU
            if (isFrontendTx)
            {
                header.insert(header.end(), 145, static_cast<uint8_t>(' '));
            }
            else
            {
                std::size_t rfuLen = 349 - ((fnameLen > 48) ? (fnameLen - 48) : 0);
                header.insert(header.end(), rfuLen, static_cast<uint8_t>(' '));
            }

            ofs.write(reinterpret_cast<const char*>(header.data()), header.size());
        }
        else
        {
            ofs.open(settleFile, std::ios::binary | std::ios::out | std::ios::app);
        }

        if (!ofs.is_open())
        {
            Logger::getInstance()->FnLog("EEP: [FILE] Open failed | Path=" + settleFile, logFileName_, "EEP");
            Logger::getInstance()->FnLog("EEP: [FILE] Record | Hex=" + recordHex, logFileName_, "EEP");
            return;
        }

        ofs.write(reinterpret_cast<const char*>(data.data()), data.size());

        if (!ofs)
        {
            Logger::getInstance()->FnLog("EEP: [FILE] Write failed | Path=" + settleFile + " | Bytes=" + std::to_string(data.size()), logFileName_, "EEP");
            Logger::getInstance()->FnLog("EEP: [FILE] Record | Hex=" + recordHex, logFileName_, "EEP");
        }
        ofs.close();

        if (!isFrontendTx)
        {
            copyAndRemoveBEFile(settleFile);
        }

    }
    catch (const std::exception& e)
    {
        Logger::getInstance()->FnLogExceptionError("EEP: [FILE] Write exception | Error=" + std::string(e.what()));
        Logger::getInstance()->FnLog("EEP: [FILE] Write exception | Error=" + std::string(e.what()) + " | RecordHex=" + recordHex, logFileName_, "EEP");
    }
    catch (...)
    {
        Logger::getInstance()->FnLogExceptionError("EEP: [FILE] Write exception | Error=Unknown");
        Logger::getInstance()->FnLog("EEP: [FILE] Write exception | Error=Unknown | RecordHex=" + recordHex, logFileName_, "EEP");
    }
}

void EEPClient::copyAndRemoveBEFile(const std::string& settlementfilepath)
{
    const std::string mountPoint = "/mnt/dsrcsettlementfiles";
    const std::string sharedFolderPath =
        "//" + IniParser::getInstance()->FnGetCentralDBServer() +
        "/Carpark/EEPSettle";

    const std::string username = IniParser::getInstance()->FnGetCentralUsername();

    const std::string password = IniParser::getInstance()->FnGetCentralPassword();

    try
    {
        // MountManager is a synchronous RAII utility. This function is invoked
        // from filePool_, so mount/retry/filesystem work does not block EEP_IO.
        MountManager mountManager(
            sharedFolderPath,
            mountPoint,
            username,
            password,
            logFileName_,
            "EEP");

        if (!mountManager.isMounted())
        {
            Logger::getInstance()->FnLog(
                "EEP: [FILE] BE settlement transfer failed"
                " | Reason=Mount unavailable"
                " | Share=" + sharedFolderPath +
                " | MountPoint=" + mountPoint,
                logFileName_,
                "EEP");
            return;
        }

        const std::filesystem::path sourceFile(settlementfilepath);

        std::error_code ec;
        if (!std::filesystem::exists(sourceFile, ec) ||
            ec ||
            !std::filesystem::is_regular_file(sourceFile, ec) ||
            ec)
        {
            Logger::getInstance()->FnLog(
                "EEP: [FILE] BE settlement transfer failed"
                " | Reason=Source file unavailable"
                " | Path=" + sourceFile.string() +
                (ec ? " | Error=" + ec.message() : ""),
                logFileName_,
                "EEP");
            return;
        }

        const std::filesystem::path destinationFile =
            std::filesystem::path(mountPoint) /
            "DSRCBE" /
            "Raw" /
            sourceFile.filename();

        std::filesystem::create_directories(destinationFile.parent_path(), ec);

        if (ec)
        {
            Logger::getInstance()->FnLog(
                "EEP: [FILE] Destination directory create failed"
                " | Path=" + destinationFile.parent_path().string() +
                " | Error=" + ec.message(),
                logFileName_,
                "EEP");
            return;
        }

        ec.clear();
        std::filesystem::copy(
            sourceFile,
            destinationFile,
            std::filesystem::copy_options::overwrite_existing,
            ec);

        if (ec)
        {
            Logger::getInstance()->FnLog(
                "EEP: [FILE] BE settlement copy failed"
                " | Source=" + sourceFile.string() +
                " | Destination=" + destinationFile.string() +
                " | Error=" + ec.message(),
                logFileName_,
                "EEP");
            return;
        }

        Logger::getInstance()->FnLog(
            "EEP: [FILE] BE settlement copied"
            " | Source=" + sourceFile.string() +
            " | Destination=" + destinationFile.string(),
            logFileName_,
            "EEP");

        ec.clear();
        const bool removed = std::filesystem::remove(sourceFile, ec);

        if (ec)
        {
            Logger::getInstance()->FnLog(
                "EEP: [FILE] Local BE settlement remove failed"
                " | Path=" + sourceFile.string() +
                " | Error=" + ec.message(),
                logFileName_,
                "EEP");
            return;
        }

        Logger::getInstance()->FnLog(
            "EEP: [FILE] Local BE settlement removed"
            " | Path=" + sourceFile.string() +
            " | Removed=" + std::string(removed ? "true" : "false"),
            logFileName_,
            "EEP");

        // mountManager goes out of scope here. Its destructor releases the
        // mount lease and only unmounts when the final process lease is gone.
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        const std::string message =
            std::string("EEP: [FILE] BE settlement transfer exception"
                        " | Type=Filesystem"
                        " | Error=") +
            e.what();

        Logger::getInstance()->FnLogExceptionError(message);
        Logger::getInstance()->FnLog(message, logFileName_, "EEP");
    }
    catch (const std::exception& e)
    {
        const std::string message =
            std::string("EEP: [FILE] BE settlement transfer exception"
                        " | Error=") +
            e.what();

        Logger::getInstance()->FnLogExceptionError(message);
        Logger::getInstance()->FnLog(message, logFileName_, "EEP");
    }
    catch (...)
    {
        const std::string message =
            "EEP: [FILE] BE settlement transfer exception"
            " | Error=Unknown";

        Logger::getInstance()->FnLogExceptionError(message);
        Logger::getInstance()->FnLog(message, logFileName_, "EEP");
    }
}

std::string EEPClient::FnGetStatusData()
{
    std::vector<std::uint8_t> snapshot;
    {
        std::lock_guard<std::mutex> lock(statusDataMutex_);
        snapshot = status_data_;
    }

    std::ostringstream stream;
    stream << std::hex << std::setfill('0');

    for (std::uint8_t byte : snapshot)
    {
        stream << std::setw(2)
               << static_cast<unsigned int>(byte);
    }

    return stream.str();
}
