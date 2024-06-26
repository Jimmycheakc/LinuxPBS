#include <boost/asio.hpp>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>
#include "common.h"
#include "crc.h"
#include "log.h"
#include "upt.h"

// UPOS Message Header
MessageHeader::MessageHeader()
    : length_(0),
    integrityCRC32_(0),
    msgVersion_(0),
    msgDirection_(0),
    msgTime_(0),
    msgSequence_(0),
    msgClass_(0),
    msgType_(0x10000000u),
    msgCode_(0x10000000u),
    msgCompletion_(0),
    msgNotification_(0),
    msgStatus_(0x00000000u),
    deviceProvider_(0),
    deviceType_(0),
    deviceNumber_(0),
    encryptionAlgorithm_(0),
    encryptionKeyIndex_(0)
{
    for (int i = 0; i < Upt::ENCRYPTION_MAC_SIZE; i++)
    {
        encryptionMAC_.push_back(0);
    }
};

MessageHeader::~MessageHeader()
{

}

void MessageHeader::setLength(uint32_t length)
{
    length_ = length;
}

uint32_t MessageHeader::getLength() const
{
    return length_;
}

void MessageHeader::setIntegrityCRC32(uint32_t integrityCRC32)
{
    integrityCRC32_ = integrityCRC32;
}

uint32_t MessageHeader::getIntegrityCRC32() const
{
    return integrityCRC32_;
}

void MessageHeader::setMsgVersion(uint8_t msgVersion)
{
    msgVersion_ = msgVersion;
}

uint8_t MessageHeader::getMsgVersion() const
{
    return msgVersion_;
}

void MessageHeader::setMsgDirection(uint8_t msgDirection)
{
    msgDirection_ = msgDirection;
}

uint8_t MessageHeader::getMsgDirection() const
{
    return msgDirection_;
}

void MessageHeader::setMsgTime(uint64_t msgTime)
{
    msgTime_ = msgTime;
}

uint64_t MessageHeader::getMsgTime() const
{
    return msgTime_;
}

void MessageHeader::setMsgSequence(uint32_t msgSequence)
{
    msgSequence_ = msgSequence;
}

uint32_t MessageHeader::getMsgSequence() const
{
    return msgSequence_;
}

void MessageHeader::setMsgClass(uint16_t msgClass)
{
    msgClass_ = msgClass;
}

uint16_t MessageHeader::getMsgClass() const
{
    return msgClass_;
}

void MessageHeader::setMsgType(uint32_t msgType)
{
    msgType_ = msgType;
}

uint32_t MessageHeader::getMsgType() const
{
    return msgType_;
}

void MessageHeader::setMsgCode(uint32_t msgCode)
{
    msgCode_ = msgCode;
}

uint32_t MessageHeader::getMsgCode() const
{
    return msgCode_;
}

void MessageHeader::setMsgCompletion(uint8_t msgCompletion)
{
    msgCompletion_ = msgCompletion;
}

uint8_t MessageHeader::getMsgCompletion() const
{
    return msgCompletion_;
}

void MessageHeader::setMsgNotification(uint8_t msgNotification)
{
    msgNotification_ = msgNotification;
}

uint8_t MessageHeader::getMsgNotification() const
{
    return msgNotification_;
}

void MessageHeader::setMsgStatus(uint32_t msgStatus)
{
    msgStatus_ = msgStatus;
}

uint32_t MessageHeader::getMsgStatus() const
{
    return msgStatus_;
}

void MessageHeader::setDeviceProvider(uint16_t deviceProvider)
{
    deviceProvider_ = deviceProvider;
}

uint16_t MessageHeader::getDeviceProvider() const
{
    return deviceProvider_;
}

void MessageHeader::setDeviceType(uint16_t deviceType)
{
    deviceType_ = deviceType;
}

uint16_t MessageHeader::getDeviceType() const
{
    return deviceType_;
}

void MessageHeader::setDeviceNumber(uint32_t deviceNumber)
{
    deviceNumber_ = deviceNumber;
}

uint32_t MessageHeader::getDeviceNumber() const
{
    return deviceNumber_;
}

void MessageHeader::setEncryptionAlgorithm(uint8_t encryptionAlgorithm)
{
    encryptionAlgorithm_ = encryptionAlgorithm;
}

uint8_t MessageHeader::getEncryptionAlgorithm() const
{
    return encryptionAlgorithm_;
}

void MessageHeader::setEncryptionKeyIndex(uint8_t encryptionKeyIndex)
{
    encryptionKeyIndex_ = encryptionKeyIndex;
}

uint8_t MessageHeader::getEncryptionKeyIndex() const
{
    return encryptionKeyIndex_;
}

void MessageHeader::setEncryptionMAC(const std::vector<uint8_t>& encryptionMAC)
{
    if (encryptionMAC.size() == Upt::ENCRYPTION_MAC_SIZE)
    {
        encryptionMAC_ = encryptionMAC;
    }
}

std::vector<uint8_t> MessageHeader::getEncryptionMAC() const
{
    return encryptionMAC_;
}

// UPOS Message Payload Fields
PayloadField::PayloadField()
    : payloadFieldLength_(0),
    payloadFieldId_(0x0000u),
    fieldReserve_(0),
    fieldEncoding_(0x30u),
    fieldData_()
{

}

PayloadField::~PayloadField()
{

}

void PayloadField::setPayloadFieldLength(uint32_t length)
{
    payloadFieldLength_ = length;
}

uint32_t PayloadField::getPayloadFieldLength() const
{
    return payloadFieldLength_;
}

void PayloadField::setPayloadFieldId(uint16_t payloadFieldId)
{
    payloadFieldId_ = payloadFieldId;
}

uint16_t PayloadField::getPayloadFieldId() const
{
    return payloadFieldId_;
}

void PayloadField::setFieldReserve(uint8_t fieldReserve)
{
    fieldReserve_ = fieldReserve;
}

uint8_t PayloadField::getFieldReserve() const
{
    return fieldReserve_;
}

void PayloadField::setFieldEnconding(uint8_t fieldEncoding)
{
    fieldEncoding_ = fieldEncoding;
}

uint8_t PayloadField::getFieldEncoding() const
{
    return fieldEncoding_;
}

void PayloadField::setFieldData(const std::vector<uint8_t>& fieldData)
{
    if (fieldData.size() == (payloadFieldLength_ - 8))
    {
        fieldData_ = fieldData;
    }
}

std::vector<uint8_t> PayloadField::getFieldData() const
{
    return fieldData_;
}


// Upos Terminal Class
Upt* Upt::upt_;
std::mutex Upt::mutex_;
uint32_t Upt::sequenceNo_ = 1;

Upt::Upt()
    : ioContext_(),
    strand_(boost::asio::make_strand(ioContext_)),
    work_(ioContext_),
    ackTimer_(ioContext_),
    rspTimer_(ioContext_),
    write_in_progress_(false),
    rxState_(Upt::RX_STATE::RX_START),
    logFileName_("upos")
{
    resetRxBuffer();
}

Upt* Upt::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (upt_ == nullptr)
    {
        upt_ = new Upt();
    }
    return upt_;
}

void Upt::FnUptInit(unsigned int baudRate, const std::string& comPortName)
{
    pSerialPort_ = std::make_unique<boost::asio::serial_port>(ioContext_, comPortName);

    try
    {
        pSerialPort_->set_option(boost::asio::serial_port_base::baud_rate(baudRate));
        pSerialPort_->set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
        pSerialPort_->set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
        pSerialPort_->set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
        pSerialPort_->set_option(boost::asio::serial_port_base::character_size(8));

        Logger::getInstance()->FnCreateLogFile(logFileName_);

        std::stringstream ss;
        if (pSerialPort_->is_open())
        {
            ss << "UPOS Terminal initialization completed.";
            startIoContextThread();
            startRead();
        }
        else
        {
            ss << "UPOS Terminal initialization failed.";
        }
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "UPT");
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << "Exception during UPOS Terminal initialization: " << e.what();
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "UPT");
    }
}

void Upt::FnUptClose()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "UPT");

    ioContext_.stop();
    if (ioContextThread_.joinable())
    {
        ioContextThread_.join();
    }
}

void Upt::startIoContextThread()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "UPT");

    if (!ioContextThread_.joinable())
    {
        ioContextThread_ = std::thread([this]() { ioContext_.run(); });
    }
}

std::string Upt::getCommandString(Upt::UPT_CMD cmd)
{
    std::string cmdStr;

    switch (cmd)
    {
        // DEVICE API
        case UPT_CMD::DEVICE_STATUS_REQUEST:
        {
            cmdStr = "DEVICE_STATUS_REQUEST";
            break;
        }
        case UPT_CMD::DEVICE_RESET_REQUEST:
        {
            cmdStr = "DEVICE_RESET_REQUEST";
            break;
        }
        case UPT_CMD::DEVICE_TIME_SYNC_REQUEST:
        {
            cmdStr = "DEVICE_TIME_SYNC_REQUEST";
            break;
        }
        case UPT_CMD::DEVICE_PROFILE_REQUEST:
        {
            cmdStr = "DEVICE_PROFILE_REQUEST";
            break;
        }
        case UPT_CMD::DEVICE_SOF_LIST_REQUEST:
        {
            cmdStr = "DEVICE_SOF_LIST_REQUEST";
            break;
        }
        case UPT_CMD::DEVICE_SOF_SET_PRIORITY_REQUEST:
        {
            cmdStr = "DEVICE_SOF_SET_PRIORITY_REQUEST";
            break;
        }
        case UPT_CMD::DEVICE_LOGON_REQUEST:
        {
            cmdStr = "DEVICE_LOGON_REQUEST";
            break;
        }
        case UPT_CMD::DEVICE_TMS_REQUEST:
        {
            cmdStr = "DEVICE_TMS_REQUEST";
            break;
        }
        case UPT_CMD::DEVICE_SETTLEMENT_REQUEST:
        {
            cmdStr = "DEVICE_SETTLEMENT_REQUEST";
            break;
        }
        case UPT_CMD::DEVICE_PRE_SETTLEMENT_REQUEST:
        {
            cmdStr = "DEVICE_PRE_SETTLEMENT_REQUEST";
            break;
        }
        case UPT_CMD::DEVICE_RESET_SEQUENCE_NUMBER_REQUEST:
        {
            cmdStr = "DEVICE_RESET_SEQUENCE_NUMBER_REQUEST";
            break;
        }
        case UPT_CMD::DEVICE_RETRIEVE_LAST_TRANSACTION_STATUS_REQUEST:
        {
            cmdStr = "DEVICE_RETRIEVE_LAST_TRANSACTION_STATUS_REQUEST";
            break;
        }
        case UPT_CMD::DEVICE_RETRIEVE_LAST_SETTLEMENT_REQUEST:
        {
            cmdStr = "DEVICE_RETRIEVE_LAST_SETTLEMENT_REQUEST";
            break;
        }

        // AUTHENTICATION API
        case UPT_CMD::MUTUAL_AUTHENTICATION_STEP_1_REQUEST:
        {
            cmdStr = "MUTUAL_AUTHENTICATION_STEP_1_REQUEST";
            break;
        }
        case UPT_CMD::MUTUAL_AUTHENTICATION_STEP_2_REQUEST:
        {
            cmdStr = "MUTUAL_AUTHENTICATION_STEP_2_REQUEST";
            break;
        }

        // CARD API
        case UPT_CMD::CARD_DETECT_REQUEST:
        {
            cmdStr = "CARD_DETECT_REQUEST";
            break;
        }
        case UPT_CMD::CARD_DETAIL_REQUEST:
        {
            cmdStr = "CARD_DETAIL_REQUEST";
            break;
        }
        case UPT_CMD::CARD_HISTORICAL_LOG_REQUEST:
        {
            cmdStr = "CARD_HISTORICAL_LOG_REQUEST";
            break;
        }

        // PAYMENT API
        case UPT_CMD::PAYMENT_MODE_AUTO_REQUEST:
        {
            cmdStr = "PAYMENT_MODE_AUTO_REQUEST";
            break;
        }
        case UPT_CMD::PAYMENT_MODE_EFT_REQUEST:
        {
            cmdStr = "PAYMENT_MODE_EFT_REQUEST";
            break;
        }
        case UPT_CMD::PAYMENT_MODE_EFT_NETS_QR_REQUEST:
        {
            cmdStr = "PAYMENT_MODE_EFT_NETS_QR_REQUEST";
            break;
        }
        case UPT_CMD::PAYMENT_MODE_BCA_REQUEST:
        {
            cmdStr = "PAYMENT_MODE_BCA_REQUEST";
            break;
        }
        case UPT_CMD::PAYMENT_MODE_CREDIT_CARD_REQUEST:
        {
            cmdStr = "PAYMENT_MODE_CREDIT_CARD_REQUEST";
            break;
        }
        case UPT_CMD::PAYMENT_MODE_NCC_REQUEST:
        {
            cmdStr = "PAYMENT_MODE_NCC_REQUEST";
            break;
        }
        case UPT_CMD::PAYMENT_MODE_NFP_REQUEST:
        {
            cmdStr = "PAYMENT_MODE_NFP_REQUEST";
            break;
        }
        case UPT_CMD::PAYMENT_MODE_EZ_LINK_REQUEST:
        {
            cmdStr = "PAYMENT_MODE_EZ_LINK_REQUEST";
            break;
        }
        case UPT_CMD::PRE_AUTHORIZATION_REQUEST:
        {
            cmdStr = "PRE_AUTHORIZATION_REQUEST";
            break;
        }
        case UPT_CMD::PRE_AUTHORIZATION_COMPLETION_REQUEST:
        {
            cmdStr = "PRE_AUTHORIZATION_COMPLETION_REQUEST";
            break;
        }
        case UPT_CMD::INSTALLATION_REQUEST:
        {
            cmdStr = "INSTALLATION_REQUEST";
            break;
        }

        // CANCELLATION API
        case UPT_CMD::VOID_PAYMENT_REQUEST:
        {
            cmdStr = "VOID_PAYMENT_REQUEST";
            break;
        }
        case UPT_CMD::REFUND_REQUEST:
        {
            cmdStr = "REFUND_REQUEST";
            break;
        }
        case UPT_CMD::CANCEL_COMMAND_REQUEST:
        {
            cmdStr = "CANCEL_COMMAND_REQUEST";
            break;
        }

        // TOP-UP API
        case UPT_CMD::TOP_UP_NETS_NCC_BY_NETS_EFT_REQUEST:
        {
            cmdStr = "TOP_UP_NETS_NCC_BY_NETS_EFT_REQUEST";
            break;
        }
        case UPT_CMD::TOP_UP_NETS_NFP_BY_NETS_EFT_REQUEST:
        {
            cmdStr = "TOP_UP_NETS_NFP_BY_NETS_EFT_REQUEST";
            break;
        }

        // OTHER API
        case UPT_CMD::CASE_DEPOSIT_REQUEST:
        {
            cmdStr = "CASE_DEPOSIT_REQUEST";
            break;
        }

        // PASSTHROUGH
        case UPT_CMD::UOB_REQUEST:
        {
            cmdStr = "UOB_REQUEST";
            break;
        }

        default:
        {
            cmdStr = "Unknow Command";
            break;
        }
    }

    return cmdStr;
}

bool Upt::checkCmd(Upt::UPT_CMD cmd)
{
    bool ret = false;

    switch (cmd)
    {
        // DEVICE API
        case UPT_CMD::DEVICE_STATUS_REQUEST:
        case UPT_CMD::DEVICE_RESET_REQUEST:
        case UPT_CMD::DEVICE_TIME_SYNC_REQUEST:
        case UPT_CMD::DEVICE_PROFILE_REQUEST:
        case UPT_CMD::DEVICE_SOF_LIST_REQUEST:
        case UPT_CMD::DEVICE_SOF_SET_PRIORITY_REQUEST:
        case UPT_CMD::DEVICE_LOGON_REQUEST:
        case UPT_CMD::DEVICE_TMS_REQUEST:
        case UPT_CMD::DEVICE_SETTLEMENT_REQUEST:
        case UPT_CMD::DEVICE_PRE_SETTLEMENT_REQUEST:
        case UPT_CMD::DEVICE_RESET_SEQUENCE_NUMBER_REQUEST:
        case UPT_CMD::DEVICE_RETRIEVE_LAST_TRANSACTION_STATUS_REQUEST:
        case UPT_CMD::DEVICE_RETRIEVE_LAST_SETTLEMENT_REQUEST:
        // AUTHENTICATION API
        case UPT_CMD::MUTUAL_AUTHENTICATION_STEP_1_REQUEST:
        case UPT_CMD::MUTUAL_AUTHENTICATION_STEP_2_REQUEST:
        // CARD API
        case UPT_CMD::CARD_DETECT_REQUEST:
        case UPT_CMD::CARD_DETAIL_REQUEST:
        case UPT_CMD::CARD_HISTORICAL_LOG_REQUEST:
        // PAYMENT API
        case UPT_CMD::PAYMENT_MODE_AUTO_REQUEST:
        case UPT_CMD::PAYMENT_MODE_EFT_REQUEST:
        case UPT_CMD::PAYMENT_MODE_EFT_NETS_QR_REQUEST:
        case UPT_CMD::PAYMENT_MODE_BCA_REQUEST:
        case UPT_CMD::PAYMENT_MODE_CREDIT_CARD_REQUEST:
        case UPT_CMD::PAYMENT_MODE_NCC_REQUEST:
        case UPT_CMD::PAYMENT_MODE_NFP_REQUEST:
        case UPT_CMD::PAYMENT_MODE_EZ_LINK_REQUEST:
        case UPT_CMD::PRE_AUTHORIZATION_REQUEST:
        case UPT_CMD::PRE_AUTHORIZATION_COMPLETION_REQUEST:
        case UPT_CMD::INSTALLATION_REQUEST:
        // CANCELLATION API
        case UPT_CMD::VOID_PAYMENT_REQUEST:
        case UPT_CMD::REFUND_REQUEST:
        case UPT_CMD::CANCEL_COMMAND_REQUEST:
        // TOP-UP API
        case UPT_CMD::TOP_UP_NETS_NCC_BY_NETS_EFT_REQUEST:
        case UPT_CMD::TOP_UP_NETS_NFP_BY_NETS_EFT_REQUEST:
        // OTHER API
        case UPT_CMD::CASE_DEPOSIT_REQUEST:
        // PASSTHROUGH
        case UPT_CMD::UOB_REQUEST:
        {
            ret = true;
            break;
        }
    }

    return ret;
}

void Upt::FnUptEnqueueCommand(Upt::UPT_CMD cmd, std::shared_ptr<void> data)
{
    std::stringstream ss;
    ss << "Sending Upt Command to queue: " << getCommandString(cmd);
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "UPT");

    std::unique_lock<std::mutex> lock(cmdQueueMutex_);
    //commandQueue_.emplace(cmd, data);
    enqueueWrite(prepareCmd(cmd, data));

}

void Upt::FnUptSendDeviceStatusRequest()
{
    FnUptEnqueueCommand(UPT_CMD::DEVICE_STATUS_REQUEST);
}

void Upt::FnUptSendDeviceResetRequest()
{
    FnUptEnqueueCommand(UPT_CMD::DEVICE_RESET_REQUEST);
}

void Upt::FnUptSendDeviceTimeSyncRequest()
{
    FnUptEnqueueCommand(UPT_CMD::DEVICE_TIME_SYNC_REQUEST);
}

void Upt::FnUptSendDeviceLogonRequest()
{
    FnUptEnqueueCommand(UPT_CMD::DEVICE_LOGON_REQUEST);
}

void Upt::FnUptSendDeviceTMSRequest()
{
    FnUptEnqueueCommand(UPT_CMD::DEVICE_TMS_REQUEST);
}

void Upt::FnUptSendDeviceSettlementNETSRequest()
{
    std::shared_ptr<void> req_data = std::make_shared<CommandSettlementRequestData>(CommandSettlementRequestData{HOST_ID::NETS});
    FnUptEnqueueCommand(UPT_CMD::DEVICE_SETTLEMENT_REQUEST, req_data);
}

void Upt::FnUptSendDeviceSettlementUPOSRequest()
{
    std::shared_ptr<void> req_data = std::make_shared<CommandSettlementRequestData>(CommandSettlementRequestData{HOST_ID::UPOS});
    FnUptEnqueueCommand(UPT_CMD::DEVICE_SETTLEMENT_REQUEST, req_data);
}

void Upt::FnUptSendDeviceResetSequenceNumberRequest()
{
    FnUptEnqueueCommand(UPT_CMD::DEVICE_RESET_SEQUENCE_NUMBER_REQUEST);
}

void Upt::FnUptSendDeviceRetrieveNETSLastTransactionStatusRequest()
{
    std::shared_ptr<void> req_data = std::make_shared<CommandRetrieveLastTransactionStatusRequestData>(CommandRetrieveLastTransactionStatusRequestData{HOST_ID::UPOS});
    FnUptEnqueueCommand(UPT_CMD::DEVICE_RETRIEVE_LAST_TRANSACTION_STATUS_REQUEST, req_data);
}

void Upt::FnUptSendDeviceRetrieveUPOSLastTransactionStatusRequest()
{
    std::shared_ptr<void> req_data = std::make_shared<CommandRetrieveLastTransactionStatusRequestData>(CommandRetrieveLastTransactionStatusRequestData{HOST_ID::UPOS});
    FnUptEnqueueCommand(UPT_CMD::DEVICE_RETRIEVE_LAST_TRANSACTION_STATUS_REQUEST, req_data);
}

void Upt::FnUptSendDeviceRetrieveLastSettlementRequest()
{
    FnUptEnqueueCommand(UPT_CMD::DEVICE_RETRIEVE_LAST_SETTLEMENT_REQUEST);
}

void Upt::FnUptSendCardDetectRequest()
{
    FnUptEnqueueCommand(UPT_CMD::CARD_DETECT_REQUEST);
}

void Upt::FnUptSendDeviceAutoPaymentRequest(uint32_t amount, const std::string& mer_ref_num)
{
    std::shared_ptr<void> req_data = std::make_shared<CommandPaymentRequestData>(CommandPaymentRequestData{amount, mer_ref_num});
    FnUptEnqueueCommand(UPT_CMD::PAYMENT_MODE_AUTO_REQUEST, req_data);
}

void Upt::FnUptSendDevicePaymentRequest(uint32_t amount, const std::string& mer_ref_num, Upt::PaymentType type)
{
    switch (type)
    {
        case PaymentType::NETS_EFT:
        {
            std::shared_ptr<void> req_data = std::make_shared<CommandPaymentRequestData>(CommandPaymentRequestData{amount, mer_ref_num});
            FnUptEnqueueCommand(UPT_CMD::PAYMENT_MODE_EFT_REQUEST, req_data);
            break;
        }
        case PaymentType::NETS_NCC:
        {
            std::shared_ptr<void> req_data = std::make_shared<CommandPaymentRequestData>(CommandPaymentRequestData{amount, mer_ref_num});
            FnUptEnqueueCommand(UPT_CMD::PAYMENT_MODE_NCC_REQUEST, req_data);
            break;
        }
        case PaymentType::NETS_NFP:
        {
            std::shared_ptr<void> req_data = std::make_shared<CommandPaymentRequestData>(CommandPaymentRequestData{amount, mer_ref_num});
            FnUptEnqueueCommand(UPT_CMD::PAYMENT_MODE_NFP_REQUEST, req_data);
            break;
        }
        case PaymentType::NETS_QR:
        {
            std::shared_ptr<void> req_data = std::make_shared<CommandPaymentRequestData>(CommandPaymentRequestData{amount, mer_ref_num});
            FnUptEnqueueCommand(UPT_CMD::PAYMENT_MODE_EFT_NETS_QR_REQUEST, req_data);
            break;
        }
        case PaymentType::EZL:
        {
            std::shared_ptr<void> req_data = std::make_shared<CommandPaymentRequestData>(CommandPaymentRequestData{amount, mer_ref_num});
            FnUptEnqueueCommand(UPT_CMD::PAYMENT_MODE_EZ_LINK_REQUEST, req_data);
            break;
        }
        case PaymentType::SCHEME_CREDIT:
        {
            std::shared_ptr<void> req_data = std::make_shared<CommandPaymentRequestData>(CommandPaymentRequestData{amount, mer_ref_num});
            FnUptEnqueueCommand(UPT_CMD::PAYMENT_MODE_CREDIT_CARD_REQUEST, req_data);
            break;
        }
        default:
        {
            std::shared_ptr<void> req_data = std::make_shared<CommandPaymentRequestData>(CommandPaymentRequestData{amount, mer_ref_num});
            FnUptEnqueueCommand(UPT_CMD::PAYMENT_MODE_AUTO_REQUEST, req_data);
            break;
        }
    }
}

void Upt::FnUptSendDeviceCancelCommandRequest()
{
    FnUptEnqueueCommand(UPT_CMD::CANCEL_COMMAND_REQUEST);
}

void Upt::FnUptSendDeviceNCCTopUpCommandRequest(uint32_t amount, const std::string& mer_ref_num)
{
    std::shared_ptr<void> req_data = std::make_shared<CommandTopUpRequestData>(CommandTopUpRequestData{amount, mer_ref_num});
    FnUptEnqueueCommand(UPT_CMD::TOP_UP_NETS_NCC_BY_NETS_EFT_REQUEST);
}

void Upt::FnUptSendDeviceNFPTopUpCommandRequest(uint32_t amount, const std::string& mer_ref_num)
{
    std::shared_ptr<void> req_data = std::make_shared<CommandTopUpRequestData>(CommandTopUpRequestData{amount, mer_ref_num});
    FnUptEnqueueCommand(UPT_CMD::TOP_UP_NETS_NFP_BY_NETS_EFT_REQUEST);
}

void Upt::startAckTimeout()
{
    ackTimer_.expires_from_now(boost::posix_time::seconds(8));
    ackTimer_.async_wait(boost::asio::bind_executor(strand_,
        std::bind(&Upt::handleAckTimeout, this, std::placeholders::_1)));
}

void Upt::handleAckTimeout(const boost::system::error_code& error)
{
    if (!error)
    {
        Logger::getInstance()->FnLog("Ack Response Timeout.", logFileName_, "UPT");
    }
    else
    {
        std::stringstream ss;
        ss << "Ack Response Timer error: " << error.message();
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "UPT");
    }
}

void Upt::startResponseTimeout()
{
    rspTimer_.expires_from_now(boost::posix_time::seconds(180));
    rspTimer_.async_wait(boost::asio::bind_executor(strand_,
        std::bind(&Upt::handleCmdResponseTimeout, this, std::placeholders::_1)));
}

void Upt::handleCmdResponseTimeout(const boost::system::error_code& error)
{
    if (!error)
    {
        Logger::getInstance()->FnLog("Command Response Timeout.", logFileName_, "UPT");
    }
}


std::vector<uint8_t> Upt::prepareCmd(Upt::UPT_CMD cmd, std::shared_ptr<void> payloadData)
{
    std::vector<uint8_t> data;

    Message msg;

    // Set the common msg header
    msg.header.setMsgVersion(0x00);
    msg.header.setMsgDirection(0x00);
    //msg.header.setMsgTime(Common::getInstance()->FnGetSecondsSince1January0000());
    msg.header.setMsgTime(0x0000000ED6646B5E);
    //msg.header.setMsgSequence(sequenceNo_++);
    msg.header.setMsgSequence(1);
    msg.header.setMsgClass(0x0001);
    msg.header.setMsgCompletion(0x01);
    msg.header.setMsgNotification(0x00);
    msg.header.setMsgStatus(static_cast<uint32_t>(MSG_STATUS::SUCCESS));
    msg.header.setDeviceProvider(0x0457); // [Decimal] : 1111
    msg.header.setDeviceType(0x0001);
    msg.header.setDeviceNumber(0x01B207); // [Decimal] : 111111
    msg.header.setEncryptionAlgorithm(0x00);
    msg.header.setEncryptionKeyIndex(0x00);
    std::vector<uint8_t> mac = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    msg.header.setEncryptionMAC(mac);

    switch (cmd)
    {
        // DEVICE API
        case UPT_CMD::DEVICE_STATUS_REQUEST:
        {
            msg.header.setMsgType(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_DEVICE));
            msg.header.setMsgCode(static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_STATUS));
            msg.header.setIntegrityCRC32(msg.FnCalculateIntegrityCRC32());
            msg.header.setLength(64);
            break;
        }
        case UPT_CMD::DEVICE_RESET_REQUEST:
        {
            msg.header.setMsgType(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_DEVICE));
            msg.header.setMsgCode(static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_RESET));
            msg.header.setIntegrityCRC32(msg.FnCalculateIntegrityCRC32());
            msg.header.setLength(64);
            break;
        }
        case UPT_CMD::DEVICE_TIME_SYNC_REQUEST:
        {
            msg.header.setMsgType(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_DEVICE));
            msg.header.setMsgCode(static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_TIME_SYNC));

            // Payload
            msg.payloads.push_back(createPayload(0x00000010, static_cast<uint16_t>(FIELD_ID::ID_DEVICE_DATE), 0x31, 0x31, Common::getInstance()->FnGetDateInArrayBytes()));
            msg.payloads.push_back(createPayload(0x0000000E, static_cast<uint16_t>(FIELD_ID::ID_DEVICE_TIME), 0x31, 0x31, Common::getInstance()->FnGetTimeInArrayBytes()));
            std::vector<uint8_t> paddingBytes(26, 0x00);
            msg.payloads.push_back(createPayload(0x00000022, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x31, 0x33, paddingBytes));
            // End of Payload

            msg.header.setIntegrityCRC32(msg.FnCalculateIntegrityCRC32());
            msg.header.setLength(128);
            break;
        }
        case UPT_CMD::DEVICE_PROFILE_REQUEST:
        {
            // To be Implement
            break;
        }
        case UPT_CMD::DEVICE_SOF_LIST_REQUEST:
        {
            // To be Implement
            break;
        }
        case UPT_CMD::DEVICE_SOF_SET_PRIORITY_REQUEST:
        {
            // To be Implement
            break;
        }
        case UPT_CMD::DEVICE_LOGON_REQUEST:
        {
            msg.header.setMsgType(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_DEVICE));
            msg.header.setMsgCode(static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_LOGON));

            // Payload
            std::vector<uint8_t> data = {0x10, 0x00};   // [NETS = 0x1000]
            msg.payloads.push_back(createPayload(0x0000000A, static_cast<uint16_t>(FIELD_ID::ID_TXN_ACQUIRER), 0x39, 0x38, data));
            std::vector<uint8_t> paddingBytes(14, 0x00);
            msg.payloads.push_back(createPayload(0x00000016, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x31, 0x33, paddingBytes));
            // End of Payload

            msg.header.setIntegrityCRC32(msg.FnCalculateIntegrityCRC32());
            msg.header.setLength(96);
            break;
        }
        case UPT_CMD::DEVICE_TMS_REQUEST:
        {
            msg.header.setMsgType(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_DEVICE));
            msg.header.setMsgCode(static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_TMS));

            // Payload
            std::vector<uint8_t> data = {0x10, 0x00};   // [NETS = 0x1000]
            msg.payloads.push_back(createPayload(0x0000000A, static_cast<uint16_t>(FIELD_ID::ID_TXN_ACQUIRER), 0x39, 0x38, data));
            std::vector<uint8_t> paddingBytes(14, 0x00);
            msg.payloads.push_back(createPayload(0x00000016, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x31, 0x33, paddingBytes));
            // End of Payload

            msg.header.setIntegrityCRC32(msg.FnCalculateIntegrityCRC32());
            msg.header.setLength(96);
            break;
        }
        case UPT_CMD::DEVICE_SETTLEMENT_REQUEST:
        {
            msg.header.setMsgType(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_DEVICE));
            msg.header.setMsgCode(static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_SETTLEMENT));

            // Payload
            auto fieldHostIdData = std::static_pointer_cast<CommandSettlementRequestData>(payloadData);

            std::vector<uint8_t> data = {0x4E, 0x45, 0x54, 0x53};   // NETS in Ascii Hex;
            if (fieldHostIdData)
            {
                if (fieldHostIdData->hostIdValue == HOST_ID::UPOS)
                {
                    data = {0x55, 0x50, 0x4F, 0x53}; // UPOS in Ascii Hex
                }
            }
            msg.payloads.push_back(createPayload(0x0000000c, static_cast<uint16_t>(FIELD_ID::ID_TXN_HOST), 0x00, 0x31, data));
            
            std::vector<uint8_t> receipt = {0x01};
            msg.payloads.push_back(createPayload(0x00000009, static_cast<uint16_t>(FIELD_ID::ID_TXN_RECEIPT_REQUIRED), 0x00, 0x38, receipt));
            std::vector<uint8_t> paddingBytes(3, 0x00);
            msg.payloads.push_back(createPayload(0x0000000B, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x31, 0x33, paddingBytes));
            // End of Payload

            msg.header.setIntegrityCRC32(msg.FnCalculateIntegrityCRC32());
            msg.header.setLength(96);
            break;
        }
        case UPT_CMD::DEVICE_PRE_SETTLEMENT_REQUEST:
        {
            // To be Implement
            break;
        }
        case UPT_CMD::DEVICE_RESET_SEQUENCE_NUMBER_REQUEST:
        {
            msg.header.setMsgType(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_DEVICE));
            msg.header.setMsgCode(static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_RESET_SEQUENCE_NUMBER));
            msg.header.setIntegrityCRC32(msg.FnCalculateIntegrityCRC32());
            msg.header.setLength(64);
            break;
        }
        case UPT_CMD::DEVICE_RETRIEVE_LAST_TRANSACTION_STATUS_REQUEST:
        {
            msg.header.setMsgType(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_DEVICE));
            msg.header.setMsgCode(static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_RETRIEVE_LAST_TRANSACTION_STATUS));

            // Payload
            auto fieldHostIdData = std::static_pointer_cast<CommandRetrieveLastTransactionStatusRequestData>(payloadData);

            std::vector<uint8_t> data = {0x4E, 0x45, 0x54, 0x53};   // NETS in Ascii Hex;
            if (fieldHostIdData)
            {
                if (fieldHostIdData->hostIdValue == HOST_ID::UPOS)
                {
                    data = {0x55, 0x50, 0x4F, 0x53}; // UPOS in Ascii Hex
                }
            }
            msg.payloads.push_back(createPayload(0x0000000c, static_cast<uint16_t>(FIELD_ID::ID_TXN_HOST), 0x00, 0x31, data));
            std::vector<uint8_t> paddingBytes(12, 0x00);
            msg.payloads.push_back(createPayload(0x00000014, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x31, 0x33, paddingBytes));
            //End of Payload

            msg.header.setIntegrityCRC32(msg.FnCalculateIntegrityCRC32());
            msg.header.setLength(96);
            break;
        }
        case UPT_CMD::DEVICE_RETRIEVE_LAST_SETTLEMENT_REQUEST:
        {
            msg.header.setMsgType(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_DEVICE));
            msg.header.setMsgCode(static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_RETRIEVE_LAST_SETTLEMENT));
            msg.header.setIntegrityCRC32(msg.FnCalculateIntegrityCRC32());
            msg.header.setLength(64);
            break;
        }

        // AUTHENTICATION API
        case UPT_CMD::MUTUAL_AUTHENTICATION_STEP_1_REQUEST:
        {
            // To be Implement
            break;
        }
        case UPT_CMD::MUTUAL_AUTHENTICATION_STEP_2_REQUEST:
        {
            // To be Implement
            break;
        }

        // CARD API
        case UPT_CMD::CARD_DETECT_REQUEST:
        {
            msg.header.setMsgType(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_CARD));
            msg.header.setMsgCode(static_cast<uint32_t>(MSG_CODE::MSG_CODE_CARD_DETECT));
            msg.header.setIntegrityCRC32(msg.FnCalculateIntegrityCRC32());
            msg.header.setLength(64);
            break;
        }
        case UPT_CMD::CARD_DETAIL_REQUEST:
        {
            // To be Implement
            break;
        }
        case UPT_CMD::CARD_HISTORICAL_LOG_REQUEST:
        {
            // To be Implement
            break;
        }

        // PAYMENT API
        case UPT_CMD::PAYMENT_MODE_AUTO_REQUEST:
        {
            msg.header.setMsgType(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_PAYTMENT));
            msg.header.setMsgCode(static_cast<uint32_t>(MSG_CODE::MSG_CODE_PAYMENT_AUTO));

            // Payload
            std::vector<uint8_t> paymentType(2, 0x00); // Payment [Automatic Detect]
            msg.payloads.push_back(createPayload(0x0000000A, static_cast<uint16_t>(FIELD_ID::ID_TXN_TYPE), 0x00, 0x38, paymentType));

            auto fieldData = std::static_pointer_cast<CommandPaymentRequestData>(payloadData);

            std::vector<uint8_t> amount(4, 0x00);
            if (fieldData)
            {
                amount = Common::getInstance()->FnConvertToLittleEndian(Common::getInstance()->FnConvertUint32ToVector(fieldData->amount));
            }
            msg.payloads.push_back(createPayload(0x0000000C, static_cast<uint16_t>(FIELD_ID::ID_TXN_AMOUNT), 0x00, 0x38, amount));

            std::vector<uint8_t> mer_ref_num(12, 0x00);
            if (fieldData)
            {
                mer_ref_num = Common::getInstance()->FnConvertAsciiToUint8Vector(fieldData->mer_ref_num);
            }

            msg.payloads.push_back(createPayload(0x00000014, static_cast<uint16_t>(FIELD_ID::ID_TXN_MER_REF_NUM), 0x00, 0x31, mer_ref_num));

            std::vector<uint8_t> receipt = {0x01};
            msg.payloads.push_back(createPayload(0x00000009, static_cast<uint16_t>(FIELD_ID::ID_TXN_RECEIPT_REQUIRED), 0x00, 0x38, receipt));

            std::vector<uint8_t> paddingBytes(5, 0x00);
            msg.payloads.push_back(createPayload(0x0000000D, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x00, 0x33, paddingBytes));
            // End of Payload

            msg.header.setIntegrityCRC32(msg.FnCalculateIntegrityCRC32());
            msg.header.setLength(128);
            break;
        }
        case UPT_CMD::PAYMENT_MODE_EFT_REQUEST:
        {
            msg.header.setMsgType(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_PAYTMENT));
            msg.header.setMsgCode(static_cast<uint32_t>(MSG_CODE::MSG_CODE_PAYMENT_EFT));

            // Payload
            std::vector<uint8_t> paymentType = {0x10, 0x00};   // Payment [Payment by NETS EFT]
            msg.payloads.push_back(createPayload(0x0000000A, static_cast<uint16_t>(FIELD_ID::ID_TXN_TYPE), 0x00, 0x38, paymentType));

            auto fieldData = std::static_pointer_cast<CommandPaymentRequestData>(payloadData);

            std::vector<uint8_t> amount(4, 0x00);
            if (fieldData)
            {
                amount = Common::getInstance()->FnConvertToLittleEndian(Common::getInstance()->FnConvertUint32ToVector(fieldData->amount));
            }
            msg.payloads.push_back(createPayload(0x0000000C, static_cast<uint16_t>(FIELD_ID::ID_TXN_AMOUNT), 0x00, 0x38, amount));

            std::vector<uint8_t> mer_ref_num(12, 0x00);
            if (fieldData)
            {
                mer_ref_num = Common::getInstance()->FnConvertAsciiToUint8Vector(fieldData->mer_ref_num);
            }

            msg.payloads.push_back(createPayload(0x00000014, static_cast<uint16_t>(FIELD_ID::ID_TXN_MER_REF_NUM), 0x00, 0x31, mer_ref_num));

            std::vector<uint8_t> receipt = {0x01};
            msg.payloads.push_back(createPayload(0x00000009, static_cast<uint16_t>(FIELD_ID::ID_TXN_RECEIPT_REQUIRED), 0x00, 0x38, receipt));

            std::vector<uint8_t> paddingBytes(5, 0x00);
            msg.payloads.push_back(createPayload(0x0000000D, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x00, 0x33, paddingBytes));
            // End of Payload

            msg.header.setIntegrityCRC32(msg.FnCalculateIntegrityCRC32());
            msg.header.setLength(128);
            break;
        }
        case UPT_CMD::PAYMENT_MODE_EFT_NETS_QR_REQUEST:
        {
            msg.header.setMsgType(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_PAYTMENT));
            msg.header.setMsgCode(static_cast<uint32_t>(MSG_CODE::MSG_CODE_PAYMENT_EFT));

            // Payload
            std::vector<uint8_t> paymentType = {0x14, 0x00};   // Payment [Payment by NETS QR]
            msg.payloads.push_back(createPayload(0x0000000A, static_cast<uint16_t>(FIELD_ID::ID_TXN_TYPE), 0x00, 0x38, paymentType));

            auto fieldData = std::static_pointer_cast<CommandPaymentRequestData>(payloadData);

            std::vector<uint8_t> amount(4, 0x00);
            if (fieldData)
            {
                amount = Common::getInstance()->FnConvertToLittleEndian(Common::getInstance()->FnConvertUint32ToVector(fieldData->amount));
            }
            msg.payloads.push_back(createPayload(0x0000000C, static_cast<uint16_t>(FIELD_ID::ID_TXN_AMOUNT), 0x00, 0x38, amount));

            std::vector<uint8_t> mer_ref_num(12, 0x00);
            if (fieldData)
            {
                mer_ref_num = Common::getInstance()->FnConvertAsciiToUint8Vector(fieldData->mer_ref_num);
            }

            msg.payloads.push_back(createPayload(0x00000014, static_cast<uint16_t>(FIELD_ID::ID_TXN_MER_REF_NUM), 0x00, 0x31, mer_ref_num));

            std::vector<uint8_t> receipt = {0x01};
            msg.payloads.push_back(createPayload(0x00000009, static_cast<uint16_t>(FIELD_ID::ID_TXN_RECEIPT_REQUIRED), 0x00, 0x38, receipt));

            std::vector<uint8_t> paddingBytes(5, 0x00);
            msg.payloads.push_back(createPayload(0x0000000D, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x00, 0x33, paddingBytes));
            // End of Payload

            msg.header.setIntegrityCRC32(msg.FnCalculateIntegrityCRC32());
            msg.header.setLength(128);
            break;
        }
        case UPT_CMD::PAYMENT_MODE_BCA_REQUEST:
        {
            // To be Implement
            break;
        }
        case UPT_CMD::PAYMENT_MODE_CREDIT_CARD_REQUEST:
        {
            msg.header.setMsgType(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_PAYTMENT));
            msg.header.setMsgCode(static_cast<uint32_t>(MSG_CODE::MSG_CODE_PAYMENT_CRD));

            // Payload
            std::vector<uint8_t> paymentType = {0x20, 0x00};   // Payment [Payment by Scheme Credit]
            msg.payloads.push_back(createPayload(0x0000000A, static_cast<uint16_t>(FIELD_ID::ID_TXN_TYPE), 0x00, 0x38, paymentType));

            auto fieldData = std::static_pointer_cast<CommandPaymentRequestData>(payloadData);

            std::vector<uint8_t> amount(4, 0x00);
            if (fieldData)
            {
                amount = Common::getInstance()->FnConvertToLittleEndian(Common::getInstance()->FnConvertUint32ToVector(fieldData->amount));
            }
            msg.payloads.push_back(createPayload(0x0000000C, static_cast<uint16_t>(FIELD_ID::ID_TXN_AMOUNT), 0x00, 0x38, amount));

            std::vector<uint8_t> mer_ref_num(12, 0x00);
            if (fieldData)
            {
                mer_ref_num = Common::getInstance()->FnConvertAsciiToUint8Vector(fieldData->mer_ref_num);
            }

            msg.payloads.push_back(createPayload(0x00000014, static_cast<uint16_t>(FIELD_ID::ID_TXN_MER_REF_NUM), 0x00, 0x31, mer_ref_num));

            std::vector<uint8_t> receipt = {0x01};
            msg.payloads.push_back(createPayload(0x00000009, static_cast<uint16_t>(FIELD_ID::ID_TXN_RECEIPT_REQUIRED), 0x00, 0x38, receipt));

            std::vector<uint8_t> paddingBytes(5, 0x00);
            msg.payloads.push_back(createPayload(0x0000000D, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x00, 0x33, paddingBytes));
            // End of Payload

            msg.header.setIntegrityCRC32(msg.FnCalculateIntegrityCRC32());
            msg.header.setLength(128);
            break;
        }
        case UPT_CMD::PAYMENT_MODE_NCC_REQUEST:
        {
            msg.header.setMsgType(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_PAYTMENT));
            msg.header.setMsgCode(static_cast<uint32_t>(MSG_CODE::MSG_CODE_PAYMENT_NCC));

            // Payload
            std::vector<uint8_t> paymentType = {0x11, 0x00};   // Payment [Payment by NETS NCC]
            msg.payloads.push_back(createPayload(0x0000000A, static_cast<uint16_t>(FIELD_ID::ID_TXN_TYPE), 0x00, 0x38, paymentType));

            auto fieldData = std::static_pointer_cast<CommandPaymentRequestData>(payloadData);

            std::vector<uint8_t> amount(4, 0x00);
            if (fieldData)
            {
                amount = Common::getInstance()->FnConvertToLittleEndian(Common::getInstance()->FnConvertUint32ToVector(fieldData->amount));
            }
            msg.payloads.push_back(createPayload(0x0000000C, static_cast<uint16_t>(FIELD_ID::ID_TXN_AMOUNT), 0x00, 0x38, amount));

            std::vector<uint8_t> mer_ref_num(12, 0x00);
            if (fieldData)
            {
                mer_ref_num = Common::getInstance()->FnConvertAsciiToUint8Vector(fieldData->mer_ref_num);
            }

            msg.payloads.push_back(createPayload(0x00000014, static_cast<uint16_t>(FIELD_ID::ID_TXN_MER_REF_NUM), 0x00, 0x31, mer_ref_num));

            std::vector<uint8_t> receipt = {0x01};
            msg.payloads.push_back(createPayload(0x00000009, static_cast<uint16_t>(FIELD_ID::ID_TXN_RECEIPT_REQUIRED), 0x00, 0x38, receipt));

            std::vector<uint8_t> paddingBytes(5, 0x00);
            msg.payloads.push_back(createPayload(0x0000000D, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x00, 0x33, paddingBytes));
            // End of Payload

            msg.header.setIntegrityCRC32(msg.FnCalculateIntegrityCRC32());
            msg.header.setLength(128);
            break;
        }
        case UPT_CMD::PAYMENT_MODE_NFP_REQUEST:
        {
            msg.header.setMsgType(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_PAYTMENT));
            msg.header.setMsgCode(static_cast<uint32_t>(MSG_CODE::MSG_CODE_PAYMENT_NFP));

            // Payload
            std::vector<uint8_t> paymentType = {0x12, 0x00};   // Payment [Payment by NETS NFP]
            msg.payloads.push_back(createPayload(0x0000000A, static_cast<uint16_t>(FIELD_ID::ID_TXN_TYPE), 0x00, 0x38, paymentType));

            auto fieldData = std::static_pointer_cast<CommandPaymentRequestData>(payloadData);

            std::vector<uint8_t> amount(4, 0x00);
            if (fieldData)
            {
                amount = Common::getInstance()->FnConvertToLittleEndian(Common::getInstance()->FnConvertUint32ToVector(fieldData->amount));
            }
            msg.payloads.push_back(createPayload(0x0000000C, static_cast<uint16_t>(FIELD_ID::ID_TXN_AMOUNT), 0x00, 0x38, amount));

            std::vector<uint8_t> mer_ref_num(12, 0x00);
            if (fieldData)
            {
                mer_ref_num = Common::getInstance()->FnConvertAsciiToUint8Vector(fieldData->mer_ref_num);
            }

            msg.payloads.push_back(createPayload(0x00000014, static_cast<uint16_t>(FIELD_ID::ID_TXN_MER_REF_NUM), 0x00, 0x31, mer_ref_num));

            std::vector<uint8_t> receipt = {0x01};
            msg.payloads.push_back(createPayload(0x00000009, static_cast<uint16_t>(FIELD_ID::ID_TXN_RECEIPT_REQUIRED), 0x00, 0x38, receipt));

            std::vector<uint8_t> paddingBytes(5, 0x00);
            msg.payloads.push_back(createPayload(0x0000000D, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x00, 0x33, paddingBytes));
            // End of Payload

            msg.header.setIntegrityCRC32(msg.FnCalculateIntegrityCRC32());
            msg.header.setLength(128);
            break;
        }
        case UPT_CMD::PAYMENT_MODE_EZ_LINK_REQUEST:
        {
            msg.header.setMsgType(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_PAYTMENT));
            msg.header.setMsgCode(static_cast<uint32_t>(MSG_CODE::MSG_CODE_PAYMENT_EZL));

            // Payload
            std::vector<uint8_t> paymentType = {0x17, 0x00};   // Payment [Payment by EzLink]
            msg.payloads.push_back(createPayload(0x0000000A, static_cast<uint16_t>(FIELD_ID::ID_TXN_TYPE), 0x00, 0x38, paymentType));

            auto fieldData = std::static_pointer_cast<CommandPaymentRequestData>(payloadData);

            std::vector<uint8_t> amount(4, 0x00);
            if (fieldData)
            {
                amount = Common::getInstance()->FnConvertToLittleEndian(Common::getInstance()->FnConvertUint32ToVector(fieldData->amount));
            }
            msg.payloads.push_back(createPayload(0x0000000C, static_cast<uint16_t>(FIELD_ID::ID_TXN_AMOUNT), 0x00, 0x38, amount));

            std::vector<uint8_t> mer_ref_num(12, 0x00);
            if (fieldData)
            {
                mer_ref_num = Common::getInstance()->FnConvertAsciiToUint8Vector(fieldData->mer_ref_num);
            }

            msg.payloads.push_back(createPayload(0x00000014, static_cast<uint16_t>(FIELD_ID::ID_TXN_MER_REF_NUM), 0x00, 0x31, mer_ref_num));

            std::vector<uint8_t> receipt = {0x01};
            msg.payloads.push_back(createPayload(0x00000009, static_cast<uint16_t>(FIELD_ID::ID_TXN_RECEIPT_REQUIRED), 0x00, 0x38, receipt));

            std::vector<uint8_t> paddingBytes(5, 0x00);
            msg.payloads.push_back(createPayload(0x0000000D, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x00, 0x33, paddingBytes));
            // End of Payload

            msg.header.setIntegrityCRC32(msg.FnCalculateIntegrityCRC32());
            msg.header.setLength(128);
            break;
        }
        case UPT_CMD::PRE_AUTHORIZATION_REQUEST:
        {
            // To be Implement
            break;
        }
        case UPT_CMD::PRE_AUTHORIZATION_COMPLETION_REQUEST:
        {
            // To be Implement
            break;
        }
        case UPT_CMD::INSTALLATION_REQUEST:
        {
            // To be Implement
            break;
        }

        // CANCELLATION API
        case UPT_CMD::VOID_PAYMENT_REQUEST:
        {
            // To be Implement
            break;
        }
        case UPT_CMD::REFUND_REQUEST:
        {
            // To be Implement
            break;
        }
        case UPT_CMD::CANCEL_COMMAND_REQUEST:
        {
            msg.header.setMsgType(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_CANCELLATION));
            msg.header.setMsgCode(static_cast<uint32_t>(MSG_CODE::MSG_CODE_CANCELLATION_CANCEL));
            msg.header.setIntegrityCRC32(msg.FnCalculateIntegrityCRC32());
            msg.header.setLength(64);
            break;
        }

        // TOP-UP API
        case UPT_CMD::TOP_UP_NETS_NCC_BY_NETS_EFT_REQUEST:
        {
            msg.header.setMsgType(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_TOPUP));
            msg.header.setMsgCode(static_cast<uint32_t>(MSG_CODE::MSG_CODE_TOPUP_NCC));

            // Payload
            std::vector<uint8_t> paymentType = {0x80, 0x00};   // Top Up [Top up NETS NCC by NETS EFT]
            msg.payloads.push_back(createPayload(0x0000000A, static_cast<uint16_t>(FIELD_ID::ID_TXN_TYPE), 0x00, 0x38, paymentType));

            auto fieldData = std::static_pointer_cast<CommandPaymentRequestData>(payloadData);

            std::vector<uint8_t> amount(4, 0x00);
            if (fieldData)
            {
                amount = Common::getInstance()->FnConvertToLittleEndian(Common::getInstance()->FnConvertUint32ToVector(fieldData->amount));
            }
            msg.payloads.push_back(createPayload(0x0000000C, static_cast<uint16_t>(FIELD_ID::ID_TXN_AMOUNT), 0x00, 0x38, amount));

            std::vector<uint8_t> mer_ref_num(12, 0x00);
            if (fieldData)
            {
                mer_ref_num = Common::getInstance()->FnConvertAsciiToUint8Vector(fieldData->mer_ref_num);
            }

            msg.payloads.push_back(createPayload(0x00000014, static_cast<uint16_t>(FIELD_ID::ID_TXN_MER_REF_NUM), 0x00, 0x31, mer_ref_num));

            std::vector<uint8_t> receipt = {0x01};
            msg.payloads.push_back(createPayload(0x00000009, static_cast<uint16_t>(FIELD_ID::ID_TXN_RECEIPT_REQUIRED), 0x00, 0x38, receipt));

            std::vector<uint8_t> paddingBytes(5, 0x00);
            msg.payloads.push_back(createPayload(0x0000000D, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x00, 0x33, paddingBytes));
            // End of Payload

            msg.header.setIntegrityCRC32(msg.FnCalculateIntegrityCRC32());
            msg.header.setLength(128);
            break;
        }
        case UPT_CMD::TOP_UP_NETS_NFP_BY_NETS_EFT_REQUEST:
        {
            msg.header.setMsgType(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_TOPUP));
            msg.header.setMsgCode(static_cast<uint32_t>(MSG_CODE::MSG_CODE_TOPUP_NFP));

            // Payload
            std::vector<uint8_t> paymentType = {0x81, 0x00};   // Top Up [Top up NETS NFP by NETS EFT]
            msg.payloads.push_back(createPayload(0x0000000A, static_cast<uint16_t>(FIELD_ID::ID_TXN_TYPE), 0x00, 0x38, paymentType));

            auto fieldData = std::static_pointer_cast<CommandPaymentRequestData>(payloadData);

            std::vector<uint8_t> amount(4, 0x00);
            if (fieldData)
            {
                amount = Common::getInstance()->FnConvertToLittleEndian(Common::getInstance()->FnConvertUint32ToVector(fieldData->amount));
            }
            msg.payloads.push_back(createPayload(0x0000000C, static_cast<uint16_t>(FIELD_ID::ID_TXN_AMOUNT), 0x00, 0x38, amount));

            std::vector<uint8_t> mer_ref_num(12, 0x00);
            if (fieldData)
            {
                mer_ref_num = Common::getInstance()->FnConvertAsciiToUint8Vector(fieldData->mer_ref_num);
            }

            msg.payloads.push_back(createPayload(0x00000014, static_cast<uint16_t>(FIELD_ID::ID_TXN_MER_REF_NUM), 0x00, 0x31, mer_ref_num));

            std::vector<uint8_t> receipt = {0x01};
            msg.payloads.push_back(createPayload(0x00000009, static_cast<uint16_t>(FIELD_ID::ID_TXN_RECEIPT_REQUIRED), 0x00, 0x38, receipt));

            std::vector<uint8_t> paddingBytes(5, 0x00);
            msg.payloads.push_back(createPayload(0x0000000D, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x00, 0x33, paddingBytes));
            // End of Payload

            msg.header.setIntegrityCRC32(msg.FnCalculateIntegrityCRC32());
            msg.header.setLength(128);
            break;
        }

        // OTHER API
        case UPT_CMD::CASE_DEPOSIT_REQUEST:
        {
            // To be Implement
            break;
        }

        // PASSTHROUGH
        case UPT_CMD::UOB_REQUEST:
        {
            // To be Implement
            break;
        }
    }

    std::string temp = msg.FnSerializeWithDataTransparency();
    std::string result;
    for (int i = 0; i < temp.size(); i += 2)
    {
        result += temp.substr(i, 2);
        if (i + 2 < temp.size())
        {
            result += " ";
        }
    }

    std::cout << "Result : " << result << std::endl;
    
    data = Common::getInstance()->FnConvertStringToUint8Vector(temp);

    return data;
}

PayloadField Upt::createPayload(uint32_t length, uint16_t payloadFieldId, uint8_t fieldReserve, uint8_t fieldEncoding, const std::vector<uint8_t>& fieldData)
{
    PayloadField payload;

    payload.setPayloadFieldLength(length);
    payload.setPayloadFieldId(payloadFieldId);
    payload.setFieldReserve(fieldReserve);
    payload.setFieldEnconding(fieldEncoding);
    payload.setFieldData(fieldData);

    return payload;
}

void Upt::startRead()
{
    pSerialPort_->async_read_some(
        boost::asio::buffer(readBuffer_, readBuffer_.size()),
        boost::asio::bind_executor(strand_,
                                    std::bind(&Upt::readEnd, this,
                                    std::placeholders::_1,
                                    std::placeholders::_2)));
}

void Upt::readEnd(const boost::system::error_code& error, std::size_t bytesTransferred)
{
    if (!error)
    {
        std::vector<uint8_t> data(readBuffer_.begin(), readBuffer_.begin() + bytesTransferred);
        if (isRxResponseComplete(data))
        {
            handleCmdResponse(getRxBuffer());
            resetRxBuffer();
        }
    }
    else
    {
        std::ostringstream oss;
        oss << "Serial Read error: " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");
    }

    startRead();
}

std::vector<uint8_t> Upt::getRxBuffer() const
{
    return std::vector<uint8_t>(rxBuffer_.begin(), rxBuffer_.begin() + rxNum_);
}

void Upt::resetRxBuffer()
{
    rxBuffer_.fill(0);
    rxNum_ = 0;
}

bool Upt::isRxResponseComplete(const std::vector<uint8_t>& dataBuff)
{
    int ret = false;

    for (const auto& data : dataBuff)
    {
        switch (rxState_)
        {
            case RX_STATE::RX_START:
            {
                if (data == STX)
                {
                    resetRxBuffer();
                    rxBuffer_[rxNum_++] = data;
                    rxState_ = RX_STATE::RX_RECEIVING;
                }
                break;
            }
            case RX_STATE::RX_RECEIVING:
            {
                if (data == STX)
                {
                    resetRxBuffer();
                    rxBuffer_[rxNum_++] = data;
                }
                else if (data == ETX)
                {
                    rxBuffer_[rxNum_++] = data;
                    rxState_ = RX_STATE::RX_START;
                    ret = true;
                }
                else
                {
                    rxBuffer_[rxNum_++] = data;
                }
                break;
            }
        }

        if (ret)
        {
            break;
        }
    }

    return ret;
}

void Upt::enqueueWrite(const std::vector<uint8_t>& data)
{
    bool write_in_progress_ = !writeQueue_.empty();
    writeQueue_.push(data);
    if (!write_in_progress_)
    {
        startWrite();
    }
}

void Upt::startWrite()
{
    if (writeQueue_.empty())
    {
        return;
    }

    write_in_progress_ = true;
    const auto& data = writeQueue_.front();
    boost::asio::async_write(*pSerialPort_,
                            boost::asio::buffer(data),
                            boost::asio::bind_executor(strand_,
                                                        std::bind(&Upt::writeEnd, this,
                                                                    std::placeholders::_1,
                                                                    std::placeholders::_2)));
}

void Upt::writeEnd(const boost::system::error_code& error, std::size_t bytesTransferred)
{
    if (!error)
    {
        writeQueue_.pop();
        if (!writeQueue_.empty())
        {
            startWrite();
        }
        else
        {
            write_in_progress_ = false;
        }
    }
    else
    {
        std::ostringstream oss;
        oss << "Serial Write error: " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");
        write_in_progress_ = false;
    }
}

void Upt::handleCmdResponse(const std::vector<uint8_t>& msgDataBuff)
{
    std::stringstream receivedRespStream;

    receivedRespStream << "Received MSG data buffer: " << Common::getInstance()->FnGetDisplayVectorCharToHexString(msgDataBuff);
    Logger::getInstance()->FnLog(receivedRespStream.str(), logFileName_, "UPT");

    // Remove the STX & ETX
    std::vector<uint8_t> dataRemoveSTXandETX(msgDataBuff.begin() + 1, msgDataBuff.end() - 1);
    
    // Remove the data transparency
    Message msg;
    std::vector<uint8_t> payload = msg.FnRemoveDataTransparency(dataRemoveSTXandETX);

    if (payload.size() >= 64) // if equal or greater than 64 bytes, valid
    {
        // Check payload length is match with rx payload length
        uint32_t length = Common::getInstance()->FnConvertToUint32(Common::getInstance()->FnExtractSubVector(payload, MESSAGE_LENGTH_OFFSET, MESSAGE_LENGTH_SIZE));
        if (length == payload.size())
        {
            uint32_t calculatedCRC32;
            std::vector<uint8_t> payloadCalculateCRC32(payload.begin() + MESSAGE_VERSION_OFFSET, payload.end());
            CRC32::getInstance()->Clear();
            CRC32::getInstance()->Update(payloadCalculateCRC32.data(), payloadCalculateCRC32.size());
            calculatedCRC32 = CRC32::getInstance()->Value();

            uint32_t payloadIntegrityCRC32 = Common::getInstance()->FnConvertToUint32(Common::getInstance()->FnExtractSubVector(payload, MESSAGE_INTEGRITY_CRC32_OFFSET, MESSAGE_INTEGRITY_CRC32_SIZE));

            if (calculatedCRC32 == payloadIntegrityCRC32)
            {
                // Extract the header
                msg.header.setLength(length);
                msg.header.setIntegrityCRC32(payloadIntegrityCRC32);
                msg.header.setMsgVersion(Common::getInstance()->FnConvertToUint8(Common::getInstance()->FnExtractSubVector(payload, MESSAGE_VERSION_OFFSET, MESSAGE_VERSION_SIZE)));
                msg.header.setMsgDirection(Common::getInstance()->FnConvertToUint8(Common::getInstance()->FnExtractSubVector(payload, MESSAGE_DIRECTION_OFFSET, MESSAGE_DIRECTION_SIZE)));
                msg.header.setMsgTime(Common::getInstance()->FnConvertToUint64(Common::getInstance()->FnExtractSubVector(payload, MESSAGE_TIME_OFFSET, MESSAGE_TIME_SIZE)));
                msg.header.setMsgSequence(Common::getInstance()->FnConvertToUint32(Common::getInstance()->FnExtractSubVector(payload, MESSAGE_SEQUENCE_OFFSET, MESSAGE_SEQUENCE_SIZE)));
                msg.header.setMsgClass(Common::getInstance()->FnConvertToUint16(Common::getInstance()->FnExtractSubVector(payload, MESSAGE_CLASS_OFFSET, MESSAGE_CLASS_SIZE)));
                msg.header.setMsgType(Common::getInstance()->FnConvertToUint32(Common::getInstance()->FnExtractSubVector(payload, MESSAGE_TYPE_OFFSET, MESSAGE_TYPE_SIZE)));
                msg.header.setMsgCode(Common::getInstance()->FnConvertToUint32(Common::getInstance()->FnExtractSubVector(payload, MESSAGE_CODE_OFFSET, MESSAGE_CODE_SIZE)));
                msg.header.setMsgCompletion(Common::getInstance()->FnConvertToUint8(Common::getInstance()->FnExtractSubVector(payload, MESSAGE_COMPLETION_OFFSET, MESSAGE_COMPLETION_SIZE)));
                msg.header.setMsgNotification(Common::getInstance()->FnConvertToUint8(Common::getInstance()->FnExtractSubVector(payload, MESSAGE_NOTIFICATION_OFFSET, MESSAGE_NOTIFICATION_SIZE)));
                msg.header.setMsgStatus(Common::getInstance()->FnConvertToUint32(Common::getInstance()->FnExtractSubVector(payload, MESSAGE_STATUS_OFFSET, MESSAGE_STATUS_SIZE)));
                msg.header.setDeviceProvider(Common::getInstance()->FnConvertToUint16(Common::getInstance()->FnExtractSubVector(payload, DEVICE_PROVIDER_OFFSET, DEVICE_PROVIDER_SIZE)));
                msg.header.setDeviceType(Common::getInstance()->FnConvertToUint16(Common::getInstance()->FnExtractSubVector(payload, DEVICE_TYPE_OFFSET, DEVICE_TYPE_SIZE)));
                msg.header.setDeviceNumber(Common::getInstance()->FnConvertToUint32(Common::getInstance()->FnExtractSubVector(payload, DEVICE_NUMBER_OFFSET, DEVICE_NUMBER_SIZE)));
                msg.header.setEncryptionAlgorithm(Common::getInstance()->FnConvertToUint8(Common::getInstance()->FnExtractSubVector(payload, ENCRYPTION_ALGORITHM_OFFSET, ENCRYPTION_ALGORITHM_SIZE)));
                msg.header.setEncryptionKeyIndex(Common::getInstance()->FnConvertToUint8(Common::getInstance()->FnExtractSubVector(payload, ENCRYPTION_KEY_INDEX_OFFSET, ENCRYPTION_KEY_INDEX_SIZE)));
                msg.header.setEncryptionMAC(Common::getInstance()->FnExtractSubVector(payload, ENCRYPTION_MAC_OFFSET, ENCRYPTION_MAC_SIZE));

                // Extract payload field
                std::size_t payloadStartIndex = PAYLOAD_OFFSET;

                while ((payloadStartIndex + 4) <= length)
                {
                    // Extract payload field length
                    uint32_t payloadFieldHeaderLength = Common::getInstance()->FnConvertToUint32(Common::getInstance()->FnExtractSubVector(payload, payloadStartIndex, PAYLOAD_FIELD_LENGTH_SIZE));

                    if ((payloadStartIndex + payloadFieldHeaderLength) <= length)
                    {
                        std::vector<uint8_t> payloadFieldData(payload.begin() + payloadStartIndex, payload.begin() + payloadStartIndex + payloadFieldHeaderLength);

                        PayloadField field;
                        field.setPayloadFieldLength(payloadFieldHeaderLength);
                        field.setPayloadFieldId(Common::getInstance()->FnConvertToUint16(Common::getInstance()->FnExtractSubVector(payloadFieldData, 4, PAYLOAD_FIELD_ID_SIZE)));
                        field.setFieldReserve(Common::getInstance()->FnConvertToUint16(Common::getInstance()->FnExtractSubVector(payloadFieldData, 6, PAYLOAD_FIELD_RESERVE_SIZE)));
                        field.setFieldEnconding(Common::getInstance()->FnConvertToUint16(Common::getInstance()->FnExtractSubVector(payloadFieldData, 7, PAYLOAD_FIELD_ENCODING_SIZE)));
                        field.setFieldData(Common::getInstance()->FnExtractSubVector(payloadFieldData, 8, (payloadFieldHeaderLength - 8)));

                        msg.payloads.push_back(field);

                        payloadStartIndex += payloadFieldHeaderLength;
                    }
                    else
                    {
                        std::ostringstream oss;
                        oss << "Invalid payload field length.";
                        Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");
                        break;
                    }
                }
            }
            else
            {
                std::ostringstream oss;
                oss << "Invalid CRC.";
                Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");
            }

            
            // Can print out the msg
        }
        else
        {
            std::ostringstream oss;
            oss << "Invalid data length, the payload data received and data length mismatched.";
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");
        }
    }
    else
    {
        std::ostringstream oss;
        oss << "Invalid data length, the payload data received are lesser than 64 bytes.";
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");
    }
}


// UPOS Message Class
Message::Message()
{

}

Message::~Message()
{

}

uint32_t Message::FnCalculateIntegrityCRC32()
{
    uint32_t totalLength = 0;

    totalLength += sizeof(header.getMsgVersion());
    totalLength += sizeof(header.getMsgDirection());
    totalLength += sizeof(header.getMsgTime());
    totalLength += sizeof(header.getMsgSequence());
    totalLength += sizeof(header.getMsgClass());
    totalLength += sizeof(header.getMsgType());
    totalLength += sizeof(header.getMsgCode());
    totalLength += sizeof(header.getMsgCompletion());
    totalLength += sizeof(header.getMsgNotification());
    totalLength += sizeof(header.getMsgStatus());
    totalLength += sizeof(header.getDeviceProvider());
    totalLength += sizeof(header.getDeviceType());
    totalLength += sizeof(header.getDeviceNumber());
    totalLength += sizeof(header.getEncryptionAlgorithm());
    totalLength += sizeof(header.getEncryptionKeyIndex());

    std::vector<uint8_t> encryptionMAC = header.getEncryptionMAC();
    totalLength += (encryptionMAC.size() * sizeof(uint8_t));

    for (const auto& payload : payloads)
    {
        totalLength += sizeof(payload.getPayloadFieldLength());
        totalLength += sizeof(payload.getPayloadFieldId());
        totalLength += sizeof(payload.getFieldReserve());
        totalLength += sizeof(payload.getFieldEncoding());

        std::vector<uint8_t> fieldData = payload.getFieldData();
        totalLength += (fieldData.size() * sizeof(uint8_t));

    }

    // Create a buffer to hold all the field data
    std::vector<uint8_t> dataBuffer(totalLength);

    // Copy header fields to dataBuffer
    uint32_t offset = 0;
    uint8_t msgVersion = header.getMsgVersion();
    memcpy(dataBuffer.data() + offset, &msgVersion, sizeof(msgVersion));
    offset += sizeof(msgVersion);

    uint8_t msgDirection = header.getMsgDirection();
    memcpy(dataBuffer.data() + offset, &msgDirection, sizeof(msgDirection));
    offset += sizeof(msgDirection);

    uint64_t msgTime = header.getMsgTime();
    memcpy(dataBuffer.data() + offset, &msgTime, sizeof(msgTime));
    offset += sizeof(msgTime);

    uint32_t msgSequence = header.getMsgSequence();
    memcpy(dataBuffer.data() + offset, &msgSequence, sizeof(msgSequence));
    offset += sizeof(msgSequence);

    uint16_t msgClass = header.getMsgClass();
    memcpy(dataBuffer.data() + offset, &msgClass, sizeof(msgClass));
    offset += sizeof(msgClass);

    uint32_t msgType = header.getMsgType();
    memcpy(dataBuffer.data() + offset, &msgType, sizeof(msgType));
    offset += sizeof(msgType);

    uint32_t msgCode = header.getMsgCode();
    memcpy(dataBuffer.data() + offset, &msgCode, sizeof(msgCode));
    offset += sizeof(msgCode);

    uint8_t msgCompletion = header.getMsgCompletion();
    memcpy(dataBuffer.data() + offset, &msgCompletion, sizeof(msgCompletion));
    offset += sizeof(msgCompletion);

    uint8_t msgNotification = header.getMsgNotification();
    memcpy(dataBuffer.data() + offset, &msgNotification, sizeof(msgNotification));
    offset += sizeof(msgNotification);

    uint32_t msgStatus = header.getMsgStatus();
    memcpy(dataBuffer.data() + offset, &msgStatus, sizeof(msgStatus));
    offset += sizeof(msgStatus);

    uint16_t deviceProvider = header.getDeviceProvider();
    memcpy(dataBuffer.data() + offset, &deviceProvider, sizeof(deviceProvider));
    offset += sizeof(deviceProvider);

    uint16_t deviceType = header.getDeviceType();
    memcpy(dataBuffer.data() + offset, &deviceType, sizeof(deviceType));
    offset += sizeof(deviceType);

    uint32_t deviceNumber = header.getDeviceNumber();
    memcpy(dataBuffer.data() + offset, &deviceNumber, sizeof(deviceNumber));
    offset += sizeof(deviceNumber);

    uint8_t encryptionAlgorithm = header.getEncryptionAlgorithm();
    memcpy(dataBuffer.data() + offset, &encryptionAlgorithm, sizeof(encryptionAlgorithm));
    offset += sizeof(encryptionAlgorithm);

    uint8_t encryptionKeyIndex = header.getEncryptionKeyIndex();
    memcpy(dataBuffer.data() + offset, &encryptionKeyIndex, sizeof(encryptionKeyIndex));
    offset += sizeof(encryptionKeyIndex);

    // Copy encryptionMAC to dataBuffer
    memcpy(dataBuffer.data() + offset, encryptionMAC.data(), encryptionMAC.size() * sizeof(uint8_t));
    offset += encryptionMAC.size() * sizeof(uint8_t);

    // Copy payload fields to databuffer
    for (const auto& payload : payloads)
    {
        uint32_t payloadFieldLength = payload.getPayloadFieldLength();
        memcpy(dataBuffer.data() + offset, &payloadFieldLength, sizeof(payloadFieldLength));
        offset += sizeof(payloadFieldLength);

        uint16_t fieldID = payload.getPayloadFieldId();
        memcpy(dataBuffer.data() + offset, &fieldID, sizeof(fieldID));
        offset += sizeof(fieldID);

        uint8_t fieldReserve = payload.getFieldReserve();
        memcpy(dataBuffer.data() + offset, &fieldReserve, sizeof(fieldReserve));
        offset += sizeof(fieldReserve);

        uint8_t fieldEncoding = payload.getFieldEncoding();
        memcpy(dataBuffer.data() + offset, &fieldEncoding, sizeof(fieldEncoding));
        offset += sizeof(fieldEncoding);

        std::vector<uint8_t> fieldData = payload.getFieldData();
        memcpy(dataBuffer.data() + offset, fieldData.data(), fieldData.size() * sizeof(uint8_t));
        offset += fieldData.size() * sizeof(uint8_t);
    }

    CRC32::getInstance()->Clear();
    CRC32::getInstance()->Update(dataBuffer.data(), totalLength);

    return CRC32::getInstance()->Value();
}

std::string Message::serialize()
{
    std::string str;
    str += Common::getInstance()->FnToHexLittleEndianStringWithZeros(header.getLength()) +
        Common::getInstance()->FnToHexLittleEndianStringWithZeros(header.getIntegrityCRC32()) +
        Common::getInstance()->FnToHexLittleEndianStringWithZeros(header.getMsgVersion()) +
        Common::getInstance()->FnToHexLittleEndianStringWithZeros(header.getMsgDirection()) +
        Common::getInstance()->FnToHexLittleEndianStringWithZeros(header.getMsgTime()) +
        Common::getInstance()->FnToHexLittleEndianStringWithZeros(header.getMsgSequence()) +
        Common::getInstance()->FnToHexLittleEndianStringWithZeros(header.getMsgClass()) +
        Common::getInstance()->FnToHexLittleEndianStringWithZeros(header.getMsgType()) +
        Common::getInstance()->FnToHexLittleEndianStringWithZeros(header.getMsgCode()) +
        Common::getInstance()->FnToHexLittleEndianStringWithZeros(header.getMsgCompletion()) +
        Common::getInstance()->FnToHexLittleEndianStringWithZeros(header.getMsgNotification()) +
        Common::getInstance()->FnToHexLittleEndianStringWithZeros(header.getMsgStatus()) +
        Common::getInstance()->FnToHexLittleEndianStringWithZeros(header.getDeviceProvider()) +
        Common::getInstance()->FnToHexLittleEndianStringWithZeros(header.getDeviceType()) +
        Common::getInstance()->FnToHexLittleEndianStringWithZeros(header.getDeviceNumber()) +
        Common::getInstance()->FnToHexLittleEndianStringWithZeros(header.getEncryptionAlgorithm()) +
        Common::getInstance()->FnToHexLittleEndianStringWithZeros(header.getEncryptionKeyIndex());

    // Convert encryption MAC vector into little endian
    std::vector<uint8_t> encryptionMAC = header.getEncryptionMAC();
    std::reverse(encryptionMAC.begin(), encryptionMAC.end());
    for (const auto encryptMAC : encryptionMAC)
    {
        str += Common::getInstance()->FnToHexLittleEndianStringWithZeros(encryptMAC);
    }

    for (const auto payload : payloads)
    {
        str += Common::getInstance()->FnToHexLittleEndianStringWithZeros(payload.getPayloadFieldLength()) +
            Common::getInstance()->FnToHexLittleEndianStringWithZeros(payload.getPayloadFieldId()) +
            Common::getInstance()->FnToHexLittleEndianStringWithZeros(payload.getFieldReserve()) +
            Common::getInstance()->FnToHexLittleEndianStringWithZeros(payload.getFieldEncoding());

        // Convert payload data vector into little endian
        std::vector<uint8_t> payloadFieldData = payload.getFieldData();
        if (static_cast<Upt::FIELD_ENCODING>(payload.getFieldEncoding()) == Upt::FIELD_ENCODING::FIELD_ENCODING_VALUE_HEX_LITTLE)
        {
            std::reverse(payloadFieldData.begin(), payloadFieldData.end());
            for (const auto data : payloadFieldData)
            {
                str += Common::getInstance()->FnToHexLittleEndianStringWithZeros(data);
            }
        }
        else
        {
            for (const auto data : payloadFieldData)
            {             
                str += Common::getInstance()->FnConvertuint8ToHexString(data);
            }
        }
    }

    return str;
}

std::string Message::FnSerializeWithDataTransparency()
{
    std::string dataString = serialize();

    std::string result;
    int count = 0;
    for (int i = 0; i < dataString.size(); i += 2)
    {
        result += dataString.substr(i, 2);
        if (i + 2 < dataString.size())
        {
            result += " ";
        }
        count++;
    }
    std::cout << "Serialize size : " << count << std::endl;
    std::cout << "Serialize Result : " << result << std::endl;

    std::string replacements[] = {"02", "04", "10"};
    std::string newValues[] = {"1000", "1001", "1010"};

    std::size_t pos = 0;
    while (pos < dataString.size() - 1)
    {
        std::string pair = dataString.substr(pos, 2);

        for (int i = 0; i < 3; i++)
        {
            if (pair == replacements[i])
            {
                dataString.replace(pos, replacements[i].length(), newValues[i]);
                pos += 2;
                break;
            }
        }
        pos += 2;
    }
    dataString.insert(0, Common::getInstance()->FnToHexLittleEndianStringWithZeros(static_cast<uint8_t>(Upt::STX)));
    dataString.insert(dataString.length(), Common::getInstance()->FnToHexLittleEndianStringWithZeros(static_cast<uint8_t>(Upt::ETX)));

    return dataString;
}

std::vector<uint8_t> Message::FnRemoveDataTransparency(const std::vector<uint8_t>& input)
{
    std::vector<uint8_t> output;
    for (std::size_t i = 0; i < input.size(); i++)
    {
        if (input[i] == 0x10 && ((i + 1) < input.size()))
        {
            switch (input[i + 1])
            {
                case 0x00:
                {
                    output.push_back(0x02);
                    ++i;
                    break;
                }
                case 0x01:
                {
                    output.push_back(0x04);
                    ++i;
                    break;
                }
                case 0x10:
                {
                    output.push_back(0x10);
                    ++i;
                    break;
                }
                default:
                {
                    break;
                }
            }
        }
        else
        {
            output.push_back(input[i]);
        }
    }

    return output;
}


void Message::FnParseMsgData(const std::vector<uint8_t>& msgData, Message* msg)
{
    // Remove STX and ETX

    // remove data transparency

    // extract the msg length

    // Check CRC

    // 
}

void Message::printMessage()
{

}

