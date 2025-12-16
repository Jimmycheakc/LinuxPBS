#include <algorithm>
#include <boost/asio/thread_pool.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <memory>
#include <sstream>
#include "common.h"
#include "eep_client.h"
#include "log.h"
#include <unordered_set>
#include "event_manager.h"
#include "operation.h"


EEPClient* EEPClient::eepClient_ = nullptr;
std::mutex EEPClient::mutex_;
std::mutex EEPClient::currentCmdMutex_;
std::mutex EEPClient::currentCmdRequestedMutex_;
uint16_t EEPClient::sequenceNo_ = 0;
std::mutex EEPClient::sequenceNoMutex_;
uint16_t EEPClient::lastDeductCmdSerialNo_ = 0;
uint16_t EEPClient::deductCmdSerialNo_ = 0;
std::mutex EEPClient::deductCmdSerialNoMutex_;

EEPClient::EEPClient()
    : filePool_(2),
    ioContext_(),
    strand_(boost::asio::make_strand(ioContext_)),
    workGuard_(boost::asio::make_work_guard(ioContext_)),
    connectTimer_(ioContext_),
    sendTimer_(ioContext_),
    responseTimer_(ioContext_),
    ackTimer_(ioContext_),
    watchdogTimer_(ioContext_),
    healthStatusTimer_(ioContext_),
    reconnectTimer_(ioContext_),
    currentState_(EEPClient::STATE::IDLE),
    watchdogMissedRspCount_(0),
    iStationID_(0),
    serverIP_(""),
    serverPort_(0),
    eepSourceId_(0),
    eepDestinationId_(0),
    eepCarparkID_(0),
    commandSequence_(0),
    lastConnectionState_(false),
    logFileName_("eep")
{

}

EEPClient* EEPClient::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (eepClient_ == nullptr)
    {
        eepClient_ = new EEPClient();
    }
    return eepClient_;
}

void EEPClient::FnEEPClientInit(const std::string& serverIP, unsigned short serverPort, const std::string& stationID)
{

    Logger::getInstance()->FnCreateLogFile(logFileName_);

    bool exceptionFound = false;

    try
    {
        iStationID_ = std::stoi(stationID);
    }
    catch(const std::exception& e)
    {
        exceptionFound = true;
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }

    client_ = std::make_unique<AppTcpClient>(ioContext_, serverIP, serverPort);
    serverIP_ = serverIP;
    serverPort_ = serverPort;
    // Source ID = TS (Start from 0x60h - 9Fh) + station id
    eepSourceId_ = 96 + iStationID_;
    // Destination ID = EEP DSRC Device (0x20h - 0x5Fh) + station id
    eepDestinationId_ = 32 + iStationID_;

    if (client_ && !exceptionFound)
    {
        client_->setConnectHandler([this](bool success, const std::string& message) {
            boost::asio::post(ioContext_, [this, success, message]() {
                handleConnect(success, message);
                });
        });
        client_->setCloseHandler([this](bool success, const std::string& message) { 
            boost::asio::post(ioContext_, [this, success, message]() {
                handleClose(success, message);
            });
        });
        client_->setReceiveHandler([this](bool success, const std::vector<uint8_t>& data) { 
            boost::asio::post(ioContext_, [this, success, data]() {
                handleReceivedData(success, data); 
            });
        });
        client_->setSendHandler([this](bool success, const std::string& message) { 
            boost::asio::post(ioContext_, [this, success, message]() {
                handleSend(success, message);
            });
        });

        startIoContextThread();
        processEvent(EVENT::CONNECT);
    }
    else
    {
        Logger::getInstance()->FnLog("Failed to create EEP Client.", logFileName_, "EEP");
    }
}

void EEPClient::FnEEPClientClose()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EEP");

    eepClientClose();

    filePool_.join();  // Wait for all background jobs to finish
    workGuard_.reset();
    ioContext_.stop();
    if (ioContextThread_.joinable())
    {
        ioContextThread_.join();
    }
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

    uint8_t obuLabelArr[5];
    bool obuParseSuccess = Common::getInstance()->FnConvertDecimalStringToByteArray(obuLabel_, obuLabelArr, 5);
    uint8_t feeArr[2];
    bool feeParseSuccess = Common::getInstance()->FnDecimalStringToTwoBytes(fee_, feeArr, true);
    std::tm parsedEntryDateTime = {};
    bool entryDateTimeParseSuccess = Common::getInstance()->FnParseDateTimeString(entryTime_, parsedEntryDateTime);
    std::tm parsedExitDateTime = {};
    bool exitDateTimeParseSuccess = Common::getInstance()->FnParseDateTimeString(exitTime_, parsedExitDateTime);

    if (obuParseSuccess && feeParseSuccess && entryDateTimeParseSuccess && exitDateTimeParseSuccess)
    {
        uint16_t serialNum = getDeductCmdSerialNo();
        incrementDeductCmdSerialNo();

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
        oss << __func__ << " failed to send. ";
        oss << "Result of Obu label parse: " << obuParseSuccess << ", Obu param value: " << obuLabel_;
        oss << "Result of Fee parse: " << feeParseSuccess << ", Fee param value: " << fee_;
        oss << "Result of Entry datetime parse: " << entryDateTimeParseSuccess << ", Entry datetime param value: " << entryTime_;
        oss << "Result of Exit datetime parse: " << exitDateTimeParseSuccess << ", Exit datetime param value: " << exitTime_;
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

        struct Command cmdData = {};
        cmdData.type = CommandType::DEDUCT_REQ_CMD;
        handleCommandErrorOrTimeout(cmdData, MSG_STATUS::SEND_FAILED);
    }
}

void EEPClient::FnSendDeductStopReq(const std::string& obuLabel_, uint16_t serialNum_)
{
    // Data Format in little-Endian
    auto reqData = std::make_shared<DeductStopData>();

    uint8_t obuLabelArr[5];
    bool obuParseSuccess = Common::getInstance()->FnConvertDecimalStringToByteArray(obuLabel_, obuLabelArr, 5);

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
        oss << __func__ << " failed to send. ";
        oss << "Result of Obu label parse: " << obuParseSuccess << ", Obu param value: " << obuLabel_;
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

        struct Command cmdData = {};
        cmdData.type = CommandType::DEDUCT_STOP_REQ_CMD;
        handleCommandErrorOrTimeout(cmdData, MSG_STATUS::SEND_FAILED);
    }
}

void EEPClient::FnSendTransactionReq(const std::string& obuLabel_, uint16_t serialNum_)
{
    // Data Format in little-Endian
    auto reqData = std::make_shared<TransactionReqData>();

    uint8_t obuLabelArr[5];
    bool obuParseSuccess = Common::getInstance()->FnConvertDecimalStringToByteArray(obuLabel_, obuLabelArr, 5);

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
        oss << __func__ << " failed to send. ";
        oss << "Result of Obu label parse: " << obuParseSuccess << ", Obu param value: " << obuLabel_;
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

        struct Command cmdData = {};
        cmdData.type = CommandType::TRANSACTION_REQ_CMD;
        handleCommandErrorOrTimeout(cmdData, MSG_STATUS::SEND_FAILED);
    }
}

void EEPClient::FnSendCPOInfoDisplayReq(const std::string& obuLabel_, const std::string& dataType_, const std::string& line1_, const std::string& line2_, const std::string& line3_, const std::string& line4_, const std::string& line5_)
{
    // Data Format in little-Endian
    auto reqData = std::make_shared<CPOInfoDisplayData>();

    uint8_t obuLabelArr[5];
    bool obuParseSuccess = Common::getInstance()->FnConvertDecimalStringToByteArray(obuLabel_, obuLabelArr, 5);
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
        oss << __func__ << " failed to send. ";
        oss << "Result of Obu label parse: " << obuParseSuccess << ", Obu param value: " << obuLabel_;
        oss << "Result of data type parse: " << dataTypeParseSuccess << ", Data Type param value: " << parsedDataType_;
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

        struct Command cmdData = {};
        cmdData.type = CommandType::CPO_INFO_DISPLAY_REQ_CMD;
        handleCommandErrorOrTimeout(cmdData, MSG_STATUS::SEND_FAILED);
    }
}

void EEPClient::FnSendCarparkProcessCompleteNotificationReq(const std::string& obuLabel_, const std::string& processingResult_, const std::string& fee_)
{
    // Data Format in little-Endian
    auto reqData = std::make_shared<CarparkProcessCompleteData>();

    uint8_t obuLabelArr[5];
    bool obuParseSuccess = Common::getInstance()->FnConvertDecimalStringToByteArray(obuLabel_, obuLabelArr, 5);
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
        oss << __func__ << " failed to send. ";
        oss << "Result of Obu label parse: " << obuParseSuccess << ", Obu param value: " << obuLabel_;
        oss << " ,Result of processingResult parse: " << resultParseSuccess << ", processingResult param value: " << processingResult_;
        oss << " ,Result of fee parse: " << feeParseSuccess << ", fee param value: " << fee_;
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

        struct Command cmdData = {};
        cmdData.type = CommandType::CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD;
        handleCommandErrorOrTimeout(cmdData, MSG_STATUS::SEND_FAILED);
    }
}

void EEPClient::FnSendDSRCProcessCompleteNotificationReq(const std::string& obuLabel_)
{
    // Data Format in little-Endian
    auto reqData = std::make_shared<DSRCProcessCompleteData>();

    uint8_t obuLabelArr[5];
    bool obuParseSuccess = Common::getInstance()->FnConvertDecimalStringToByteArray(obuLabel_, obuLabelArr, 5);

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
        oss << __func__ << " failed to send. ";
        oss << "Result of Obu label parse: " << obuParseSuccess << ", Obu param value: " << obuLabel_;
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

        struct Command cmdData = {};
        cmdData.type = CommandType::DSRC_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD;
        handleCommandErrorOrTimeout(cmdData, MSG_STATUS::SEND_FAILED);
    }
}

void EEPClient::FnSendStopReqOfRelatedInfoDistributionReq(const std::string& obuLabel_)
{
    // Data Format in little-Endian
    auto reqData = std::make_shared<StopReqOfRelatedInfoData>();

    uint8_t obuLabelArr[5];
    bool obuParseSuccess = Common::getInstance()->FnConvertDecimalStringToByteArray(obuLabel_, obuLabelArr, 5);

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
        oss << __func__ << " failed to send. ";
        oss << "Result of Obu label parse: " << obuParseSuccess << ", Obu param value: " << obuLabel_;
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

        struct Command cmdData = {};
        cmdData.type = CommandType::STOP_REQ_OF_RELATED_INFO_DISTRIBUTION_CMD;
        handleCommandErrorOrTimeout(cmdData, MSG_STATUS::SEND_FAILED);
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
        oss << __func__ << " failed to send. ";
        oss << "Result of available lots parse: " << availLotsParseSuccess << ", available lots param value: " << availLots_;
        oss << "Result of total lots parse: " << totalLotsParseSuccess << ", total lots param value: " << totalLots_;
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

        struct Command cmdData = {};
        cmdData.type = CommandType::SET_CARPARK_AVAIL_REQ_CMD;
        handleCommandErrorOrTimeout(cmdData, MSG_STATUS::SEND_FAILED);
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

void EEPClient::startIoContextThread()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EEP");

    if (!ioContextThread_.joinable())
    {
        ioContextThread_ = std::thread([this]() { ioContext_.run(); });
    }
}

void EEPClient::handleConnect(bool success, const std::string& message)
{
    if (success)
    {
        std::stringstream ss;
        ss << __func__ << "() Successfully connected to EEP server at IP: " << serverIP_ << ", Port: " << serverPort_;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "EEP");

        connectTimer_.cancel();
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << "() Failed to connect to EEP server at IP: " << serverIP_ << ", Port: " << serverPort_ << ", Error: " << message;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "EEP");
    }
}

void EEPClient::handleSend(bool success, const std::string& message)
{
    if (success)
    {
        std::stringstream ss;
        ss << __func__ << "() Successfully sent to EEP server.";
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "EEP");

        sendTimer_.cancel();
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << "() Failed to send to EEP server, Error: " << message;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "EEP");
    }
}

void EEPClient::handleClose(bool success, const std::string& message)
{
    if (success)
    {
        std::stringstream ss;
        ss << __func__ << "() Successfully closed the EEP server at IP: " << serverIP_ << ", Port: " << serverPort_;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "EEP");
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << "() Failed to close the EEP server at IP: " << serverIP_ << ", Port: " << serverPort_ << " ,Error: " << message;
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "EEP");
    }
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
        Logger::getInstance()->FnLog("Parse message failed due to invalid header size.", logFileName_, "EEP");
        return false;
    }

    // Deserialize header
    if (!header.deserialize(data))
    {
        Logger::getInstance()->FnLog("Parse message failed due to header deserialize failed.", logFileName_, "EEP");
        return false;
    }

    std::size_t expectedTotalSize = MessageHeader::HEADER_SIZE + header.dataLen_;
    if (data.size() < expectedTotalSize)
    {
        Logger::getInstance()->FnLog("Parse message failed due to actual doesn't match expected data size.", logFileName_, "EEP");
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

    oss << "(" << messageCodeToString(code) << ") - Message Header\n";
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
                    Logger::getInstance()->FnLog("Invalid ACK message body size.",  logFileName_, "EEP");
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
                    Logger::getInstance()->FnLog("Invalid NAK message body size.",  logFileName_, "EEP");
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
                    Logger::getInstance()->FnLog("Invalid Health Status Response message body size.",  logFileName_, "EEP");
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
                    Logger::getInstance()->FnLog("Invalid Start Response message body size.",  logFileName_, "EEP");
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
                    Logger::getInstance()->FnLog("Invalid Stop Response message body size.",  logFileName_, "EEP");
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
                    Logger::getInstance()->FnLog("Invalid DI Status Notification/Response message body size.",  logFileName_, "EEP");
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
                    Logger::getInstance()->FnLog("Invalid OBU Information Notification message body size.",  logFileName_, "EEP");
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
                    Logger::getInstance()->FnLog("Invalid Transaction Data message body size.",  logFileName_, "EEP");
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
                uint64_t lastCreditTransactionHeader = Common::getInstance()->FnReadUint64LE(body, 92);
                uint32_t lastCreditTransactionTRP = Common::getInstance()->FnReadUint32LE(body, 100);
                uint32_t purseBalanceBeforeTransaction = Common::getInstance()->FnReadUint32LE(body, 104);
                uint8_t badDebtCounter = body[108];
                uint8_t transactionStatus = body[109];
                uint8_t debitOption = body[110];
                uint8_t rsv9 = body[111];
                uint32_t autoLoadAmount = Common::getInstance()->FnReadUint32LE(body, 112);
                uint64_t counterData = Common::getInstance()->FnReadUint64LE(body, 116);
                uint64_t signedCertificate = Common::getInstance()->FnReadUint64LE(body, 124);
                uint32_t purseBalanceAfterTransaction = Common::getInstance()->FnReadUint32LE(body, 132);
                uint8_t lastTransactionDebitOptionbyte = body[136];
                uint32_t rsv10 = Common::getInstance()->FnReadUint24LE(body, 137);
                uint64_t previousTransactionHeader = Common::getInstance()->FnReadUint64LE(body, 140);
                uint32_t previousTRP = Common::getInstance()->FnReadUint32LE(body, 148);
                uint32_t previousPurseBalance = Common::getInstance()->FnReadUint32LE(body, 152);
                uint64_t previousCounterData = Common::getInstance()->FnReadUint64LE(body, 156);
                uint64_t previousTransactionSignedCertificate = Common::getInstance()->FnReadUint64LE(body, 164);
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
                std::vector<uint8_t> bepCertificate(body.begin() + 200, body.begin() + 340);
                std::reverse(bepCertificate.begin(), bepCertificate.end());

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
                printField(oss, "BepPaymentFeeAmount", bepPaymentFeeAmount, 4);
                printField(oss, "RSV12", rsv12, 4);
                printFieldChar(oss, "BepTimeOfReport", bepTimeOfReport);
                printField(oss, "RSV13", rsv13, 6);
                printField(oss, "chargeReportCounter", chargeReportCounter, 8);
                printField(oss, "BepKeyVersion", bepKeyVersion, 2);
                printField(oss, "RSV14", rsv14, 6);
                printFieldChar(oss, "BepCertificate", bepCertificate);
                break;
            }
            case MESSAGE_CODE::CPO_INFORMATION_DISPLAY_RESULT:
            {
                if (body.size() < 20)
                {
                    Logger::getInstance()->FnLog("Invalid CPO Information Display Result message body size.",  logFileName_, "EEP");
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
                    Logger::getInstance()->FnLog("Invalid Carpark Process Complete Result message body size.",  logFileName_, "EEP");
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
                    Logger::getInstance()->FnLog("Invalid DSRC Status Response message body size.",  logFileName_, "EEP");
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
                    Logger::getInstance()->FnLog("Invalid Time Calibration Response message body size.",  logFileName_, "EEP");
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
                    Logger::getInstance()->FnLog("Invalid EEP Restart Inquiry Response message body size.",  logFileName_, "EEP");
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
                    Logger::getInstance()->FnLog("Invalid Notification Log message body size.",  logFileName_, "EEP");
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
        oss << "The actual message body length does not match the data length specified in the header.";
    }

    Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");
}

void EEPClient::handleParsedResponseMessage(const MessageHeader& header, const std::vector<uint8_t>& body, std::string& eventMsg)
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
                Logger::getInstance()->FnLog("Invalid ACK message body size.",  logFileName_, "EEP");
                break;
            }

            // Special handle for deduct req ack, need extra payload for deduct serial number
            if (getCurrentCmdRequested().type == CommandType::DEDUCT_REQ_CMD)
            {
                ackDeduct ackDeductData{};

                ackDeductData.resultCode = body[0];
                ackDeductData.rsv = Common::getInstance()->FnReadUint24LE(body, 1);
                ackDeductData.deductSerialNo = getLastDeductCmdSerialNo();

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
                Logger::getInstance()->FnLog("Invalid NAK message body size.",  logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Invalid Health Status Response message body size.",  logFileName_, "EEP");
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

            break;
        }
        case MESSAGE_CODE::START_RESPONSE:
        {
            if (body.size() < 4)
            {
                Logger::getInstance()->FnLog("Invalid Start Response message body size.",  logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Invalid Stop Response message body size.",  logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Invalid DI Status Notification/Response message body size.",  logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Invalid DSRC Status Response message body size.",  logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Invalid Time Calibration Response message body size.",  logFileName_, "EEP");
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
            Logger::getInstance()->FnLog("Invalid data type code.",  logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Invalid DI Status Notification/Response message body size.",  logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Invalid OBU Information Notification message body size.",  logFileName_, "EEP");
                break;
            }

            obuInformationNotification obuin{};

            obuin.rsv = Common::getInstance()->FnReadUint64LE(body, 0);
            obuin.obulabel = Common::getInstance()->FnReadUint40BE(body, 8);
            obuin.typeObu = body[13];
            obuin.rsv1 = Common::getInstance()->FnReadUint16LE(body, 14);
            obuin.vcc = Common::getInstance()->FnReadUint24LE(body, 16);
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
                Logger::getInstance()->FnLog("Invalid Transaction Data message body size.",  logFileName_, "EEP");
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
            td.lastCreditTransactionHeader = Common::getInstance()->FnReadUint64LE(body, 92);
            td.lastCreditTransactionTRP = Common::getInstance()->FnReadUint32LE(body, 100);
            td.purseBalanceBeforeTransaction = Common::getInstance()->FnReadUint32LE(body, 104);
            td.badDebtCounter = body[108];
            td.transactionStatus = body[109];
            td.debitOption = body[110];
            td.rsv9 = body[111];
            td.autoLoadAmount = Common::getInstance()->FnReadUint32LE(body, 112);
            td.counterData = Common::getInstance()->FnReadUint64LE(body, 116);
            td.signedCertificate = Common::getInstance()->FnReadUint64LE(body, 124);
            td.purseBalanceAfterTransaction = Common::getInstance()->FnReadUint32LE(body, 132);
            td.lastTransactionDebitOptionbyte = body[136];
            td.rsv10 = Common::getInstance()->FnReadUint24LE(body, 137);
            td.previousTransactionHeader = Common::getInstance()->FnReadUint64LE(body, 140);
            td.previousTRP = Common::getInstance()->FnReadUint32LE(body, 148);
            td.previousPurseBalance = Common::getInstance()->FnReadUint32LE(body, 152);
            td.previousCounterData = Common::getInstance()->FnReadUint64LE(body, 156);
            td.previousTransactionSignedCertificate = Common::getInstance()->FnReadUint64LE(body, 164);
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
            td.bepCertificate.assign(body.begin() + 200, body.begin() + 340);
            std::reverse(td.bepCertificate.begin(), td.bepCertificate.end());

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
                Logger::getInstance()->FnLog("Invalid CPO Information Display Result message body size.",  logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Invalid Carpark Process Complete Result message body size.",  logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Invalid EEP Restart Inquiry message body size.",  logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Invalid Notification Log message body size.",  logFileName_, "EEP");
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
            Logger::getInstance()->FnLog("Invalid data type code.",  logFileName_, "EEP");
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
        Logger::getInstance()->FnLog("Unable to send the ACK/NAK due to received data size less than message header length.", logFileName_, "EEP");
    }
}

void EEPClient::handleReceivedData(bool success, const std::vector<uint8_t>& data)
{
    try
    {
        if (success)
        {
            if (isValidCheckSum(data))
            {
                std::stringstream ss;
                ss << __func__ << "() Received EEP Data: ";
                for (uint8_t byte : data)
                {
                    ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
                }

                ss << std::dec << ", Length: " << data.size();
                Logger::getInstance()->FnLog(ss.str(), logFileName_, "EEP");

                std::vector<uint8_t> dataWithoutCheckSum(data.begin(), data.end() - 4);
                MessageHeader msgHeader{};
                std::vector<uint8_t> msgBody;

                if (parseMessage(dataWithoutCheckSum, msgHeader, msgBody))
                {
                    if (isValidSourceDestination(msgHeader.sourceID_, msgHeader.destinationID_))
                    {
                        showParsedMessage(msgHeader, msgBody);

                        // Handle for normal request cmd from station and response from EEP
                        if (isResponseMatchedDataTypeCode(getCurrentCmdRequested(), msgHeader.dataTypeCode_, msgBody))
                        {
                            if (msgHeader.seqNo_ == (getSequenceNo() - 1))
                            {
                                // Cancelled ACK timer
                                ackTimer_.cancel();

                                std::string eventMsg = "";
                                handleParsedResponseMessage(msgHeader, msgBody, eventMsg);
                                
                                if (!eventMsg.empty())
                                {
                                    EventManager::getInstance()->FnEnqueueEvent("Evt_handleEEPClientResponse", eventMsg);
                                    Logger::getInstance()->FnLog("Raise event Evt_handleEEPClientResponse.", logFileName_, "EEP");
                                }
                            }
                            else
                            {
                                Logger::getInstance()->FnLog("Invalid sequence number after parsing the message.", logFileName_, "EEP");
                            }
                        }
                        // Handle for those notification received from EEP
                        else if (isNotificationReceived(msgHeader.dataTypeCode_))
                        {
                            if (isResponseNotificationComplete(getCurrentCmdRequested(), msgHeader.dataTypeCode_))
                            {
                                Logger::getInstance()->FnLog("Valid notification response received. Cancelling response timer.", logFileName_, "EEP");
                                responseTimer_.cancel();
                            }
                            else
                            {
                                Logger::getInstance()->FnLog("Notification received, send ACK as response.", logFileName_, "EEP");
                            }

                            // Send ACK with requested data type code
                            std::string eventMsg = "";
                            handleParsedNotificationMessage(msgHeader, msgBody, eventMsg);
                            FnSendAck(msgHeader.seqNo_, msgHeader.dataTypeCode_);

                            if (!eventMsg.empty())
                            {
                                EventManager::getInstance()->FnEnqueueEvent("Evt_handleEEPClientResponse", eventMsg);
                                Logger::getInstance()->FnLog("Raise event Evt_handleEEPClientResponse.", logFileName_, "EEP");
                            }
                        }
                        else
                        {
                            Logger::getInstance()->FnLog("Invalid data type code after parsing the message.", logFileName_, "EEP");
                            // Send NAK with reason code : Unsupported Data Type Code
                            handleInvalidMessage(data, 0x01);
                        }
                    }
                    else
                    {
                        Logger::getInstance()->FnLog("Invalid message source/destination.", logFileName_, "EEP");
                        // Send NAK with reason code : Others
                        handleInvalidMessage(data, 0x04);
                    }
                }
                else
                {
                    Logger::getInstance()->FnLog("Invalid message length, parsed failed.", logFileName_, "EEP");
                    // Send NAK with reason code : Data Length Error
                    handleInvalidMessage(data, 0x02);
                }
            }
            else
            {
                Logger::getInstance()->FnLog("Invalid checksum.", logFileName_, "EEP");
                // Send NAK with reason code : Check Code Mismatch
                handleInvalidMessage(data, 0x03);
            }
        }
        else
        {
            Logger::getInstance()->FnLog("Failed to receive EEP Data. Likely socket read error.", logFileName_, "EEP");
        }
    }
    catch (const std::exception& ex)
    {
        Logger::getInstance()->FnLog("Exception in handleParsedResponseMessage: " + std::string(ex.what()), logFileName_, "EEP");
    }
    catch (...)
    {
        Logger::getInstance()->FnLog("Unknown Exception in handleParsedResponseMessage", logFileName_, "EEP");
    }
}

void EEPClient::eepClientConnect()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EEP");

    boost::asio::dispatch(strand_, [this]() {

        if (client_)
        {
            client_->connect();
        }
    });
}

void EEPClient::eepClientSend(const std::vector<uint8_t>& message)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EEP");

    boost::asio::dispatch(strand_, [this, message]() {

        if (client_)
        {
            std::stringstream ss;
            ss << "Sent EEP data: ";
            for (uint8_t byte : message)
            {
                ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
            }
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "EEP");
            client_->send(message);
        }
    });
}

void EEPClient::eepClientClose()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EEP");

    boost::asio::dispatch(strand_, [this]() {

        if (client_)
        {
            client_->close();
        }
    });
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
    boost::asio::post(strand_, [this, event]() {
        int currentStateIndex_ = static_cast<int>(currentState_);
        const auto& stateTransitions = stateTransitionTable[currentStateIndex_].transitions;

        bool eventHandled = false;
        for (const auto& transition : stateTransitions)
        {
            if (transition.event == event)
            {
                eventHandled = true;
                
                std::ostringstream oss;
                oss << "Current State : " << stateToString(currentState_);
                oss << " , Event : " << eventToString(event);
                oss << " , Event Handler : " << (transition.eventHandler ? "YES" : "NO");
                oss << " , Next State : " << stateToString(transition.nextState);
                Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

                if (transition.eventHandler != nullptr)
                {
                    (this->*transition.eventHandler)(event);
                }
                currentState_ = transition.nextState;

                if (currentState_ == STATE::CONNECTED)
                {
                    boost::asio::post(strand_, [this]() {
                        checkCommandQueue();
                    });
                }
                return;
            }
        }

        if (!eventHandled)
        {
            std::ostringstream oss;
            oss << "Event '" << eventToString(event) << "' not handled in state '" << stateToString(currentState_) << "'";
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");
        }
    });
}

void EEPClient::checkCommandQueue()
{
    bool hasCommand = false;

    {
        std::unique_lock<std::mutex> lock(cmdQueueMutex_);
        if (!commandQueue_.empty())
        {
            hasCommand = true;
        }
    }

    if (hasCommand)
    {
        processEvent(EVENT::WRITE_COMMAND);
    }
}

void EEPClient::enqueueCommand(CommandType type, int priority, std::shared_ptr<CommandDataBase> data)
{
    if (client_->isConnected())
    {
        boost::asio::post(strand_, [this, type, priority, data] () {
            {
                std::unique_lock<std::mutex> lock(cmdQueueMutex_);

                std::ostringstream oss;
                oss << "Sending EEP Command to queue: " << getCommandString(type);
                Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

                commandQueue_.emplace(type, priority, commandSequence_++, data);
            }
            checkCommandQueue();
        });
    }
    else
    {
        struct Command cmdData = {};
        cmdData.type = type;
        handleCommandErrorOrTimeout(cmdData, MSG_STATUS::SEND_FAILED);
    }
}

void EEPClient::popFromCommandQueueAndEnqueueWrite()
{
    std::unique_lock<std::mutex> lock(cmdQueueMutex_);

    std::ostringstream oss;
    oss << "Command queue size: " << commandQueue_.size() << std::endl;
    if (!commandQueue_.empty())
    {
        oss << "Commands in queue: " << std::endl;
        // Copy the queue for safe iteration
        auto tempQueue = commandQueue_;
        while (!tempQueue.empty())
        {
            const Command& cmdData = tempQueue.top();
            oss << "[Cmd: " << getCommandString(cmdData.type)
                << " , Priority: " << cmdData.priority
                << " , Cmd Sequence: " << cmdData.sequence << "]" << std::endl;
            tempQueue.pop();
        }
    }
    else
    {
        oss << "Command queue is empty." << std::endl;
    }
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

    if (!commandQueue_.empty())
    {
        Command cmd = commandQueue_.top();
        commandQueue_.pop();
        setCurrentCmd(cmd);
        if (cmd.type != CommandType::ACK || cmd.type != CommandType::NAK)
        {
            setCurrentCmdRequested(cmd);
        }
        auto [packet, ok] = prepareCmd(cmd);
        
        if (ok)
        {
            eepClientSend(packet);
        }
        else
        {
            Logger::getInstance()->FnLog("PrepareCmd failed, waiting for send failed.", logFileName_, "EEP");
        }
    }
}

void EEPClient::clearCommandQueue()
{
    std::unique_lock<std::mutex> lock(cmdQueueMutex_);
    std::ostringstream oss;
    oss << "Clearing command queue. Current size: " << commandQueue_.size() << std::endl;

    while (!commandQueue_.empty())
    {
        const Command& cmdData = commandQueue_.top();
        oss << "Removing command: [Cmd: " << getCommandString(cmdData.type)
            << " , Priority: " << cmdData.priority
            << " , Cmd Sequence: " << cmdData.sequence << "]" << std::endl;

        handleCommandErrorOrTimeout(cmdData, MSG_STATUS::SEND_FAILED);
        commandQueue_.pop();
    }

    Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");
}

void EEPClient::setCurrentCmd(Command cmd)
{
    std::unique_lock<std::mutex> lock(currentCmdMutex_);

    currentCmd = cmd;
}

EEPClient::Command EEPClient::getCurrentCmd()
{
    std::unique_lock<std::mutex> lock(currentCmdMutex_);

    return currentCmd;
}

void EEPClient::setCurrentCmdRequested(Command cmd)
{
    std::unique_lock<std::mutex> lock(currentCmdRequestedMutex_);

    currentCmd = cmd;
}

EEPClient::Command EEPClient::getCurrentCmdRequested()
{
    std::unique_lock<std::mutex> lock(currentCmdRequestedMutex_);

    return currentCmd;
}

void EEPClient::incrementSequenceNo()
{
    std::unique_lock<std::mutex> lock(sequenceNoMutex_);

    ++sequenceNo_;
}

uint16_t EEPClient::getSequenceNo()
{
    std::unique_lock<std::mutex> lock(sequenceNoMutex_);

    return sequenceNo_;
}

void EEPClient::incrementDeductCmdSerialNo()
{
    std::unique_lock<std::mutex> lock(deductCmdSerialNoMutex_);

    lastDeductCmdSerialNo_ = deductCmdSerialNo_;
    ++deductCmdSerialNo_;
}

uint16_t EEPClient::getDeductCmdSerialNo()
{
    std::unique_lock<std::mutex> lock(deductCmdSerialNoMutex_);

    return deductCmdSerialNo_;
}

uint16_t EEPClient::getLastDeductCmdSerialNo()
{
    std::unique_lock<std::mutex> lock(deductCmdSerialNoMutex_);

    return lastDeductCmdSerialNo_;
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

    uint16_t seqNo = getSequenceNo();
    incrementSequenceNo();

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
                Logger::getInstance()->FnLog("Empty ACK message to be sent.", logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Empty NAK message to be sent.", logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Empty Watchdog message to be sent.", logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Empty DI Port Configuration message to be sent.", logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Empty DI Port Configuration message to be sent.", logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Empty deduct message to be sent.", logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Empty deduct stop message to be sent.", logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Empty transaction message to be sent.", logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Empty CPO info display message to be sent.", logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Empty carpark process complete notification message to be sent.", logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Empty DSRC process complete notification message to be sent.", logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Empty Stop request of related info distribution message to be sent.", logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Empty Time calibration message to be sent.", logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Empty Set carpark avaliable message to be sent.", logFileName_, "EEP");
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
                Logger::getInstance()->FnLog("Empty Restart inquiry response request message to be sent.", logFileName_, "EEP");
                success = false;
            }
            break;
        }
        default:
        {
            Logger::getInstance()->FnLog("Invalid command type, unable to prepare message to be sent.", logFileName_, "EEP");
            success = false;
            break;
        }
    }

    if (success && !msg.empty())
    {
        // Append checkcode
        uint32_t checkcode = calculateChecksumNoPadding(msg);
        Common::getInstance()->FnAppendUint32LE(msg, checkcode);
    }

    return { msg, success };
}

void EEPClient::startReconnectTimer()
{
    reconnectTimer_.expires_after(std::chrono::seconds(3));
    reconnectTimer_.async_wait(boost::asio::bind_executor(strand_,
        [this](const boost::system::error_code& ec) {
            if (ec)
            {
                Logger::getInstance()->FnLog("Reconnect timer error: " + ec.message(), logFileName_, "EEP");

                if (ec != boost::asio::error::operation_aborted)
                {
                    Logger::getInstance()->FnLog("Retrying reconnect timer after error...", logFileName_, "EEP");
                    startReconnectTimer();  // Retry on error
                }
                return;
            }

            Logger::getInstance()->FnLog("Reconnect timer expired. Triggering CONNECT event.", logFileName_, "EEP");
            processEvent(EVENT::CONNECT);
        }));
}

void EEPClient::startConnectTimer()
{
    connectTimer_.expires_after(std::chrono::seconds(5));
    connectTimer_.async_wait(boost::asio::bind_executor(strand_,
        std::bind(&EEPClient::handleConnectTimerTimeout, this, std::placeholders::_1)));
}

void EEPClient::handleConnectTimerTimeout(const boost::system::error_code& error)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EEP");

    if (error == boost::asio::error::operation_aborted)
    {
        Logger::getInstance()->FnLog("Connect Timer cancelled.", logFileName_, "EEP");
        processEvent(EVENT::CONNECT_SUCCESS);
    }
    else if (!error)
    {
        Logger::getInstance()->FnLog("Connect Timer timeout.", logFileName_, "EEP");
        processEvent(EVENT::CONNECT_FAIL);
    }
    else
    {
        std::ostringstream oss;
        oss << "Connect Timer error: " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");
        processEvent(EVENT::CONNECT_FAIL);
    }
}

void EEPClient::startSendTimer()
{
    sendTimer_.expires_after(std::chrono::seconds(2));
    sendTimer_.async_wait(boost::asio::bind_executor(strand_,
        std::bind(&EEPClient::handleSendTimerTimeout, this, std::placeholders::_1)));
}

void EEPClient::handleSendTimerTimeout(const boost::system::error_code& error)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EEP");

    if (error == boost::asio::error::operation_aborted)
    {
        Logger::getInstance()->FnLog("Send Timer cancelled.", logFileName_, "EEP");
        processEvent(EVENT::WRITE_COMPLETED);
    }
    else if (!error)
    {
        Logger::getInstance()->FnLog("Send Timer timeout.", logFileName_, "EEP");
        processEvent(EVENT::WRITE_TIMEOUT);
    }
    else
    {
        std::ostringstream oss;
        oss << "Send Timer error: " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");
        processEvent(EVENT::WRITE_TIMEOUT);
    }
}

void EEPClient::startResponseTimer()
{
    responseTimer_.expires_after(std::chrono::seconds(4));
    responseTimer_.async_wait(boost::asio::bind_executor(strand_,
        std::bind(&EEPClient::handleResponseTimeout, this, std::placeholders::_1)));
}

void EEPClient::handleResponseTimeout(const boost::system::error_code& error)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EEP");

    if (error == boost::asio::error::operation_aborted)
    {
        Logger::getInstance()->FnLog("Response Timer cancelled.", logFileName_, "EEP");
        processEvent(EVENT::RESPONSE_TIMER_CANCELLED_RSP_RECEIVED);
    }
    else if (!error)
    {
        Logger::getInstance()->FnLog("Response Timer timeout.", logFileName_, "EEP");
        processEvent(EVENT::RESPONSE_TIMEOUT);
    }
    else
    {
        std::ostringstream oss;
        oss << "Response Timer error: " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");
        processEvent(EVENT::RESPONSE_TIMEOUT);
    }
}

void EEPClient::startAckTimer()
{
    ackTimer_.expires_after(std::chrono::seconds(1));
    ackTimer_.async_wait(boost::asio::bind_executor(strand_,
        std::bind(&EEPClient::handleAckTimeout, this, std::placeholders::_1)));
}

void EEPClient::handleAckTimeout(const boost::system::error_code& error)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EEP");

    if (error == boost::asio::error::operation_aborted)
    {
        Logger::getInstance()->FnLog("Ack Timer cancelled.", logFileName_, "EEP");
        processEvent(EVENT::ACK_TIMER_CANCELLED_ACK_RECEIVED);
    }
    else if (!error)
    {
        Logger::getInstance()->FnLog("Ack Timer timeout.", logFileName_, "EEP");
        processEvent(EVENT::ACK_TIMEOUT);
    }
    else
    {
        std::ostringstream oss;
        oss << "Ack Timer error: " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");
        processEvent(EVENT::ACK_TIMEOUT);
    }
}

void EEPClient::startWatchdogTimer()
{
    boost::system::error_code ec;
    watchdogTimer_.cancel(ec); // cancel any previous timer
    if (ec)
    {
        Logger::getInstance()->FnLog("Failed to cancel watchdog timer: " + ec.message(), logFileName_, "EEP");
    }

    FnSendWatchdogReq();
    watchdogTimer_.expires_after(std::chrono::seconds(10));
    watchdogTimer_.async_wait(boost::asio::bind_executor(strand_,
        std::bind(&EEPClient::handleWatchdogTimeout, this, std::placeholders::_1)));
}

void EEPClient::handleWatchdogTimeout(const boost::system::error_code& error)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EEP");

    if (error == boost::asio::error::operation_aborted)
    {
        Logger::getInstance()->FnLog("Watchdog Timer was canceled before expiration.", logFileName_, "EEP");
        return;
    }
    else if (error)
    {
        std::ostringstream oss;
        oss << "Watchdog Timer error: " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");
    }

    Logger::getInstance()->FnLog("Start watchdog Timer.", logFileName_, "EEP");
    startWatchdogTimer();
}

void EEPClient::startHealthStatusTimer()
{
    boost::system::error_code ec;
    healthStatusTimer_.cancel(ec);
    if (ec)
    {
        Logger::getInstance()->FnLog("Failed to cancel health status timer: " + ec.message(), logFileName_, "EEP");
    }

    FnSendHealthStatusReq();
    healthStatusTimer_.expires_after(std::chrono::seconds(60));
    healthStatusTimer_.async_wait(boost::asio::bind_executor(strand_, 
        std::bind(&EEPClient::handleHealthStatusTimeout, this, std::placeholders::_1)));
}

void EEPClient::handleHealthStatusTimeout(const boost::system::error_code& error)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EEP");

    if (error == boost::asio::error::operation_aborted)
    {
        Logger::getInstance()->FnLog("Health Status Timer was canceled before expiration.", logFileName_, "EEP");
        return;
    }
    else if (error)
    {
        std::ostringstream oss;
        oss << "Health Status Timer error: " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");
    }

    Logger::getInstance()->FnLog("Start Health Status Timer.", logFileName_, "EEP");
    startHealthStatusTimer();
}

void EEPClient::handleIdleState(EVENT event)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EEP");

    if (event == EVENT::CONNECT)
    {
        Logger::getInstance()->FnLog("Connect EEP via TCP.", logFileName_, "EEP");
        eepClientConnect();
        startConnectTimer();
    }
}

void EEPClient::handleConnectingState(EVENT event)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EEP");

    if (event == EVENT::CONNECT_SUCCESS)
    {
        Logger::getInstance()->FnLog("Successfully connected EEP via TCP.", logFileName_, "EEP");
        notifyConnectionState(true);
        FnSendStartReq();
        startHealthStatusTimer();
        startWatchdogTimer();
        processEvent(EVENT::CHECK_COMMAND);
    }
    else if (event == EVENT::CONNECT_FAIL)
    {
        Logger::getInstance()->FnLog("Failed to connect EEP via TCP.", logFileName_, "EEP");
        notifyConnectionState(false);
        startReconnectTimer();
    }
}

void EEPClient::handleConnectedState(EVENT event)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EEP");

    if (event == EVENT::CHECK_COMMAND)
    {
        // If connection loss, then raise event
        if (!client_->isConnected())
        {
            Logger::getInstance()->FnLog("Server connection closed.", logFileName_, "EEP");
            processEvent(EVENT::RECONNECT_REQUEST);
            return;
        }

        Logger::getInstance()->FnLog("Check the command queue.", logFileName_, "EEP");
        checkCommandQueue();
    }
    else if (event == EVENT::WRITE_COMMAND)
    {
        // If connection loss, then raise event
        if (!client_->isConnected())
        {
            Logger::getInstance()->FnLog("Server connection closed.", logFileName_, "EEP");
            processEvent(EVENT::RECONNECT_REQUEST);
            return;
        }

        Logger::getInstance()->FnLog("Pop from command queue and write.", logFileName_, "EEP");
        popFromCommandQueueAndEnqueueWrite();
        startSendTimer();
    }
    else if (event == EVENT::RECONNECT_REQUEST)
    {
        Logger::getInstance()->FnLog("Reconnect request.", logFileName_, "EEP");
        clearCommandQueue();
        eepClientClose();
        startReconnectTimer();
    }
}

void EEPClient::handleWritingRequestState(EVENT event)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EEP");

    if (event == EVENT::WRITE_COMPLETED)
    {
        if (getCurrentCmd().type == CommandType::ACK || getCurrentCmd().type == CommandType::NAK)
        {
            Logger::getInstance()->FnLog("Send the ACK/NAK successfully.", logFileName_, "EEP");
            processEvent(EVENT::UNSOLICITED_REQUEST_DONE);
        }
        else
        {
            Logger::getInstance()->FnLog("Send the request data successfully. Start the Ack Timer.", logFileName_, "EEP");
            startAckTimer();
        }
    }
    else if (event == EVENT::WRITE_TIMEOUT)
    {
        Logger::getInstance()->FnLog("Send the request data failed due to write timer timeout.", logFileName_, "EEP");
        if (!client_->isConnected())
        {
            Logger::getInstance()->FnLog("Server connection closed.", logFileName_, "EEP");
            processEvent(EVENT::RECONNECT_REQUEST);
        }
        handleCommandErrorOrTimeout(getCurrentCmdRequested(), MSG_STATUS::SEND_FAILED);
    }
    else if (event == EVENT::RECONNECT_REQUEST)
    {
        Logger::getInstance()->FnLog("Reconnect request.", logFileName_, "EEP");
        clearCommandQueue();
        eepClientClose();
        startReconnectTimer();
    }
}

void EEPClient::handleWaitingForResponseState(EVENT event)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EEP");

    if (event == EVENT::ACK_TIMER_CANCELLED_ACK_RECEIVED)
    {
        Logger::getInstance()->FnLog("Cancelled ACK timer, received the ACK successfully.", logFileName_, "EEP");

        // Check whether to start the response timer based on the command requested
        if (doesCmdRequireNotification(getCurrentCmdRequested()))
        {
            Logger::getInstance()->FnLog("Cmd requires notification as response, start response timer.", logFileName_, "EEP");
            startResponseTimer();
        }
        else
        {
            processEvent(EVENT::ACK_AS_RSP_RECEIVED);
        }
    }
    else if (event == EVENT::ACK_AS_RSP_RECEIVED)
    {
        Logger::getInstance()->FnLog("Received the ACK as response successfully.", logFileName_, "EEP");
    }
    else if (event == EVENT::ACK_TIMEOUT)
    {
        Logger::getInstance()->FnLog("Received the ACK timeout.", logFileName_, "EEP");
        handleCommandErrorOrTimeout(getCurrentCmdRequested(), MSG_STATUS::ACK_TIMEOUT);
    }
    else if (event == EVENT::RESPONSE_TIMER_CANCELLED_RSP_RECEIVED)
    {
        Logger::getInstance()->FnLog("Received the response successfully.", logFileName_, "EEP");
    }
    else if (event == EVENT::RESPONSE_TIMEOUT)
    {
        Logger::getInstance()->FnLog("Received the response timeout.", logFileName_, "EEP");
        if (!client_->isConnected())
        {
            Logger::getInstance()->FnLog("Server connection closed.", logFileName_, "EEP");
            processEvent(EVENT::RECONNECT_REQUEST);
        }
        handleCommandErrorOrTimeout(getCurrentCmdRequested(), MSG_STATUS::RSP_TIMEOUT);
    }
    else if (event == EVENT::UNSOLICITED_REQUEST_DONE)
    {
        Logger::getInstance()->FnLog("Unsolicited response sent.", logFileName_, "EEP");
    }
}

void EEPClient::handleCommandErrorOrTimeout(Command cmd, MSG_STATUS msgStatus)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EEP");
    EEPEventWrapper eepEvt;
    std::string eventMsg = "";

    switch (cmd.type)
    {
        case CommandType::ACK:
        {
            Logger::getInstance()->FnLog("Ignored send the ACK failed.", logFileName_, "EEP");
            break;
        }
        case CommandType::NAK:
        {
            Logger::getInstance()->FnLog("Ignored send the NAK failed.", logFileName_, "EEP");
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
            Logger::getInstance()->FnLog("Unhandled CommandType in handleCommandErrorOrTimeout", logFileName_, "EEP");
            break;
        }
    }

    if (!eventMsg.empty())
    {
        EventManager::getInstance()->FnEnqueueEvent("Evt_handleEEPClientResponse", eventMsg);
        Logger::getInstance()->FnLog("Raise event Evt_handleEEPClientResponse.", logFileName_, "EEP");
    }
}

void EEPClient::notifyConnectionState(bool connected)
{
    if (lastConnectionState_ != connected)
    {
        lastConnectionState_ = connected;
        EventManager::getInstance()->FnEnqueueEvent("Evt_handleEEPClientConnectionState", connected);
        Logger::getInstance()->FnLog("Raise event Evt_handleEEPClientConnectionState.", logFileName_, "EEP");
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
    writeDSRCFeOrBeTxToCollFile(true, txRec);
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
    writeDSRCFeOrBeTxToCollFile(false, BeTxRec);
}

void EEPClient::writeDSRCFeOrBeTxToCollFile(bool isFrontendTx, const std::vector<uint8_t>& data)
{
    std::string dataStr(data.begin(), data.end());

    try
    {
        Logger::getInstance()->FnLog(__func__, logFileName_, "EEP");

        std::string settleFile = "";
        std::string settleFileName = "";

        if (!boost::filesystem::exists(LOCAL_EEP_SETTLEMENT_FOLDER_PATH))
        {
            std::ostringstream oss;
            oss << "EEP Settle folder: " << LOCAL_EEP_SETTLEMENT_FOLDER_PATH << " Not Found, Create it.";
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");

            if (!(boost::filesystem::create_directories(LOCAL_EEP_SETTLEMENT_FOLDER_PATH)))
            {
                std::ostringstream oss;
                oss << "Failed to create directory: " << LOCAL_EEP_SETTLEMENT_FOLDER_PATH;
                Logger::getInstance()->FnLog(oss.str(), logFileName_, "EEP");
                Logger::getInstance()->FnLog("Settlement Data: " + std::string(data.begin(), data.end()), logFileName_, "EEP");
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
            ossFilename << "EEP_" << toMax16(operation::getInstance()->tParas.gsCPOID)
                        << "_" << std::setw(5) << std::setfill('0') << operation::getInstance()->tParas.gsCPID
                        << "_FE_" << Common::getInstance()->FnGetDateTimeFormat_yyyymmdd()
                        << "_" << std::setw(2) << std::setfill('0') << std::dec << iStationID_
                        << Common::getInstance()->FnGetDateTimeFormat_hh() << ".dsr";
        }
        else
        {
            ossFilename << "EEP_" << toMax16(operation::getInstance()->tParas.gsCPOID)
                        << "_" << std::setw(5) << std::setfill('0') << operation::getInstance()->tParas.gsCPID
                        << "_BE_"
                        << std::setw(2) << std::setfill('0') << std::dec << iStationID_
                        << "_" << Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmmss() << ".dsr";   
        }
        settleFileName = ossFilename.str();
        settleFile = LOCAL_EEP_SETTLEMENT_FOLDER_PATH + "/" + ossFilename.str();

        // Write data to local
        Logger::getInstance()->FnLog("Write EEP settlement to local.", logFileName_, "EEP");

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
            tempCarParkIDoss << std::setw(5) << std::setfill('0') << operation::getInstance()->tParas.gsCPID;
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
            Logger::getInstance()->FnLog("Error opening file for writing settlement to" + settleFile, logFileName_, "EEP");
            Logger::getInstance()->FnLog("Settlement Data: " + dataStr, logFileName_, "EEP");
            return;
        }

        ofs.write(reinterpret_cast<const char*>(data.data()), data.size());

        if (!ofs)
        {
            Logger::getInstance()->FnLog("Write failed for " + settleFile, logFileName_, "EEP");
            Logger::getInstance()->FnLog("Settlement Data: " + dataStr, logFileName_, "EEP");
        }
        ofs.close();

        if (!isFrontendTx)
        {
            boost::asio::post(filePool_, [this, settleFile]() {
                    copyAndRemoveBEFile(settleFile);
            });
        }

    }
    catch (const std::exception& e)
    {
        Logger::getInstance()->FnLogExceptionError(std::string(__func__) + ", Exception: " + e.what());
        Logger::getInstance()->FnLog("Settlement Data: " + dataStr, logFileName_, "EEP");
    }
    catch (...)
    {
        Logger::getInstance()->FnLogExceptionError(std::string(__func__) + ", Exception: Unknown Exception");
        Logger::getInstance()->FnLog("Settlement Data: " + dataStr, logFileName_, "EEP");
    }
}

void EEPClient::copyAndRemoveBEFile(const std::string& settlementfilepath)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "EEP");

    // Create the mount poin directory if doesn't exist
    std::string mountPoint = "/mnt/dsrcsettlementfiles";
    std::string sharedFolderPath = "//" + IniParser::getInstance()->FnGetCentralDBServer() + "/Carpark/EEPSettle";

    std::string username = IniParser::getInstance()->FnGetCentralUsername();
    std::string password = IniParser::getInstance()->FnGetCentralPassword();

    try
    {
        if (!std::filesystem::exists(mountPoint))
        {
            std::error_code ec;
            if (!std::filesystem::create_directories(mountPoint, ec))
            {
                Logger::getInstance()->FnLog(("Failed to create " + mountPoint + " directory : " + ec.message()), logFileName_, "EEP");
            }
            else
            {
                Logger::getInstance()->FnLog(("Successfully to create " + mountPoint + " directory."), logFileName_, "EEP");
            }
        }
        else
        {
            Logger::getInstance()->FnLog(("Mount point directory: " + mountPoint + " exists."), logFileName_, "EEP");
        }

        // Mount the shared folder
        std::string mountCommand = "sudo mount -t cifs " + sharedFolderPath + " " + mountPoint +
                                    " -o username=" + username + ",password=" + password;
        std::cout << "Mount cmd: " << mountCommand << std::endl;
        int mountStatus = std::system(mountCommand.c_str());
        if (mountStatus != 0)
        {
            Logger::getInstance()->FnLog(("Failed to mount " + mountPoint), logFileName_, "EEP");
        }
        else
        {
            Logger::getInstance()->FnLog(("Successfully to mount " + mountPoint), logFileName_, "EEP");

            // File copy/remove lambda
            auto copyAndRemove = [&](const std::filesystem::path& src, const std::string& subdir) {
                std::filesystem::path destFilePath = std::filesystem::path(mountPoint) / subdir / "Raw" / src.filename();

                // Ensure the parent directories exist
                std::error_code ec;
                std::filesystem::create_directories(destFilePath.parent_path(), ec);

                if (ec)
                {
                    Logger::getInstance()->FnLog("Failed to create directory: " + destFilePath.parent_path().string() +
                                                " - " + ec.message(), logFileName_, "EEP");
                }
                else
                {
                    std::filesystem::copy(src, destFilePath, std::filesystem::copy_options::overwrite_existing, ec);

                    if (!ec)
                    {
                        Logger::getInstance()->FnLog("Copied file: " + src.string(), logFileName_, "EEP");
                        std::filesystem::remove(src, ec);
                        if (!ec)
                            Logger::getInstance()->FnLog("Removed file: " + src.string(), logFileName_, "EEP");
                    }
                    else
                    {
                        Logger::getInstance()->FnLog("Failed to copy file: " + src.string(), logFileName_, "EEP");
                    }
                }
            };

            copyAndRemove(settlementfilepath, "DSRCBE");
        }
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "EEP");
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "EEP");
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "EEP");
    }

    try
    {
        // Unmount the shared folder
        std::string unmountCommand = "sudo umount " + mountPoint;
        int unmountStatus = std::system(unmountCommand.c_str());
        if (unmountStatus != 0)
        {
            Logger::getInstance()->FnLog(("Failed to unmount " + mountPoint), logFileName_, "EEP");
        }
        else
        {
            Logger::getInstance()->FnLog(("Successfully to unmount " + mountPoint), logFileName_, "EEP");
        }
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Unmount Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "EEP");
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Unmount Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "EEP");
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", Unmount Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "EEP");
    }
}
