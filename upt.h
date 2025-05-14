#pragma once

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <queue>
#include <vector>
#include "boost/asio.hpp"
#include "boost/asio/serial_port.hpp"


// UPOS Message Header
class MessageHeader
{
public:
    MessageHeader();
    ~MessageHeader();
    void setLength(uint32_t length);
    uint32_t getLength() const;
    void setIntegrityCRC32(uint32_t integrityCRC32);
    uint32_t getIntegrityCRC32() const;
    void setMsgVersion(uint8_t msgVersion);
    uint8_t getMsgVersion() const;
    void setMsgDirection(uint8_t msgDirection);
    uint8_t getMsgDirection() const;
    void setMsgTime(uint64_t msgTime);
    uint64_t getMsgTime() const;
    void setMsgSequence(uint32_t msgSequence);
    uint32_t getMsgSequence() const;
    void setMsgClass(uint16_t msgClass);
    uint16_t getMsgClass() const;
    void setMsgType(uint32_t msgType);
    uint32_t getMsgType() const;
    void setMsgCode(uint32_t msgCode);
    uint32_t getMsgCode() const;
    void setMsgCompletion(uint8_t msgCompletion);
    uint8_t getMsgCompletion() const;
    void setMsgNotification(uint8_t msgNotification);
    uint8_t getMsgNotification() const;
    void setMsgStatus(uint32_t msgStatus);
    uint32_t getMsgStatus() const;
    void setDeviceProvider(uint16_t deviceProvider);
    uint16_t getDeviceProvider() const;
    void setDeviceType(uint16_t deviceType);
    uint16_t getDeviceType() const;
    void setDeviceNumber(uint32_t deviceNumber);
    uint32_t getDeviceNumber() const;
    void setEncryptionAlgorithm(uint8_t encryptionAlgorithm);
    uint8_t getEncryptionAlgorithm() const;
    void setEncryptionKeyIndex(uint8_t encryptionKeyIndex);
    uint8_t getEncryptionKeyIndex() const;
    void setEncryptionMAC(const std::vector<uint8_t>& encryptionMAC);
    std::vector<uint8_t> getEncryptionMAC() const;

    void clear();

    std::vector<uint8_t> toByteArray() const;

private:
    // UPOS Message Header Fields
    uint32_t length_;
    uint32_t integrityCRC32_;
    uint8_t msgVersion_;
    uint8_t msgDirection_;
    uint64_t msgTime_;
    uint32_t msgSequence_;
    uint16_t msgClass_;
    uint32_t msgType_;
    uint32_t msgCode_;
    uint8_t msgCompletion_;
    uint8_t msgNotification_;
    uint32_t msgStatus_;
    uint16_t deviceProvider_;
    uint16_t deviceType_;
    uint32_t deviceNumber_;
    uint8_t encryptionAlgorithm_;
    uint8_t encryptionKeyIndex_;
    std::vector<uint8_t> encryptionMAC_;

    template<typename T>
    void appendToBuffer(std::vector<uint8_t>& buffer, T value) const;
};

// UPOS Message Payload Fields
class PayloadField
{
public:
    PayloadField();
    ~PayloadField();
    void setPayloadFieldLength(uint32_t length);
    uint32_t getPayloadFieldLength() const;
    void setPayloadFieldId(uint16_t payloadFieldId);
    uint16_t getPayloadFieldId() const;
    void setFieldReserve(uint8_t fieldReserve);
    uint8_t getFieldReserve() const;
    void setFieldEnconding(uint8_t fieldEncoding);
    uint8_t getFieldEncoding() const;
    void setFieldData(const std::vector<uint8_t>& fieldData);
    std::vector<uint8_t> getFieldData() const;

    void clear();

    std::vector<uint8_t> toByteArray() const;

private:
    // Payload fields
    uint32_t payloadFieldLength_;
    uint16_t payloadFieldId_;
    uint8_t fieldReserve_;
    uint8_t fieldEncoding_;
    std::vector<uint8_t> fieldData_;

    template<typename T>
    void appendToBuffer(std::vector<uint8_t>& buffer, T value) const;
};


// UPOS Message Class
class Message
{
public:
    Message();
    ~Message();
    void setHeaderLength(uint32_t length);
    uint32_t getHeaderLength() const;
    void setHeaderIntegrityCRC32(uint32_t integrityCRC32);
    uint32_t getHeaderIntegrityCRC32() const;
    void setHeaderMsgVersion(uint8_t msgVersion);
    uint8_t getHeaderMsgVersion() const;
    void setHeaderMsgDirection(uint8_t msgDirection);
    uint8_t getHeaderMsgDirection() const;
    void setHeaderMsgTime(uint64_t msgTime);
    uint64_t getHeaderMsgTime() const;
    void setHeaderMsgSequence(uint32_t msgSequence);
    uint32_t getHeaderMsgSequence() const;
    void setHeaderMsgClass(uint16_t msgClass);
    uint16_t getHeaderMsgClass() const;
    void setHeaderMsgType(uint32_t msgType);
    uint32_t getHeaderMsgType() const;
    void setHeaderMsgCode(uint32_t msgCode);
    uint32_t getHeaderMsgCode() const;
    void setHeaderMsgCompletion(uint8_t msgCompletion);
    uint8_t getHeaderMsgCompletion() const;
    void setHeaderMsgNotification(uint8_t msgNotification);
    uint8_t getHeaderMsgNotification() const;
    void setHeaderMsgStatus(uint32_t msgStatus);
    uint32_t getHeaderMsgStatus() const;
    void setHeaderDeviceProvider(uint16_t deviceProvider);
    uint16_t getHeaderDeviceProvider() const;
    void setHeaderDeviceType(uint16_t deviceType);
    uint16_t getHeaderDeviceType() const;
    void setHeaderDeviceNumber(uint32_t deviceNumber);
    uint32_t getHeaderDeviceNumber() const;
    void setHeaderEncryptionAlgorithm(uint8_t encryptionAlgorithm);
    uint8_t getHeaderEncryptionAlgorithm() const;
    void setHeaderEncryptionKeyIndex(uint8_t encryptionKeyIndex);
    uint8_t getHeaderEncryptionKeyIndex() const;
    void setHeaderEncryptionMAC(const std::vector<uint8_t>& encryptionMAC);
    std::vector<uint8_t> getHeaderEncryptionMAC() const;
    std::vector<uint8_t> getHeaderMsgVector() const;
    void addPayload(const PayloadField& payload);
    const std::vector<PayloadField>& getPayloads() const;
    std::vector<uint8_t> getPayloadMsgVector() const;

    uint32_t FnCalculateIntegrityCRC32();
    std::vector<uint8_t> FnAddDataTransparency(const std::vector<uint8_t>& input);
    uint32_t FnRemoveDataTransparency(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);
    uint32_t FnParseMsgData(const std::vector<uint8_t>& msgData);
    std::string FnGetMsgOutputLogString(const std::vector<uint8_t>& msgData);

    void clear();

private:
    MessageHeader header;
    std::vector<PayloadField> payloads;
    bool isValidHeaderSize(uint32_t size);
    bool isInvalidHeaderSizeLessThan64(uint32_t size);
    bool isMatchHeaderSize(uint32_t size1, uint32_t size2);
    bool isMatchCRC(uint32_t crc1, uint32_t crc2);
};


// Upos Terminal Class
class Upt
{

public:
    // Define constant for Data Transparency
    static const uint8_t STX = 0x02;
    static const uint8_t DLE = 0x10;
    static const uint8_t ETX = 0x04;

    // Define message header's field offset and size
    static const uint8_t MESSAGE_LENGTH_OFFSET          = 0;
    static const uint8_t MESSAGE_LENGTH_SIZE            = 4;
    static const uint8_t MESSAGE_INTEGRITY_CRC32_OFFSET = 4;
    static const uint8_t MESSAGE_INTEGRITY_CRC32_SIZE   = 4;
    static const uint8_t MESSAGE_VERSION_OFFSET         = 8;
    static const uint8_t MESSAGE_VERSION_SIZE           = 1;
    static const uint8_t MESSAGE_DIRECTION_OFFSET       = 9;
    static const uint8_t MESSAGE_DIRECTION_SIZE         = 1;
    static const uint8_t MESSAGE_TIME_OFFSET            = 10;
    static const uint8_t MESSAGE_TIME_SIZE              = 8;
    static const uint8_t MESSAGE_SEQUENCE_OFFSET        = 18;
    static const uint8_t MESSAGE_SEQUENCE_SIZE          = 4;
    static const uint8_t MESSAGE_CLASS_OFFSET           = 22;
    static const uint8_t MESSAGE_CLASS_SIZE             = 2;
    static const uint8_t MESSAGE_TYPE_OFFSET            = 24;
    static const uint8_t MESSAGE_TYPE_SIZE              = 4;
    static const uint8_t MESSAGE_CODE_OFFSET            = 28;
    static const uint8_t MESSAGE_CODE_SIZE              = 4;
    static const uint8_t MESSAGE_COMPLETION_OFFSET      = 32;
    static const uint8_t MESSAGE_COMPLETION_SIZE        = 1;
    static const uint8_t MESSAGE_NOTIFICATION_OFFSET    = 33;
    static const uint8_t MESSAGE_NOTIFICATION_SIZE      = 1;
    static const uint8_t MESSAGE_STATUS_OFFSET          = 34;
    static const uint8_t MESSAGE_STATUS_SIZE            = 4;
    static const uint8_t DEVICE_PROVIDER_OFFSET         = 38;
    static const uint8_t DEVICE_PROVIDER_SIZE           = 2;
    static const uint8_t DEVICE_TYPE_OFFSET             = 40;
    static const uint8_t DEVICE_TYPE_SIZE               = 2;
    static const uint8_t DEVICE_NUMBER_OFFSET           = 42;
    static const uint8_t DEVICE_NUMBER_SIZE             = 4;
    static const uint8_t ENCRYPTION_ALGORITHM_OFFSET    = 46;
    static const uint8_t ENCRYPTION_ALGORITHM_SIZE      = 1;
    static const uint8_t ENCRYPTION_KEY_INDEX_OFFSET    = 47;
    static const uint8_t ENCRYPTION_KEY_INDEX_SIZE      = 1;
    static const uint8_t ENCRYPTION_MAC_OFFSET          = 48;
    static const uint8_t ENCRYPTION_MAC_SIZE            = 16;
    static const uint8_t PAYLOAD_OFFSET                 = 64;
    static const uint8_t PAYLOAD_FIELD_LENGTH_SIZE      = 4;
    static const uint8_t PAYLOAD_FIELD_ID_SIZE          = 2;
    static const uint8_t PAYLOAD_FIELD_RESERVE_SIZE     = 1;
    static const uint8_t PAYLOAD_FIELD_ENCODING_SIZE    = 1;

    enum class UPT_CMD
    {
        // DEVICE API
        DEVICE_STATUS_REQUEST,
        DEVICE_RESET_REQUEST,
        DEVICE_TIME_SYNC_REQUEST,
        DEVICE_PROFILE_REQUEST,
        DEVICE_SOF_LIST_REQUEST,
        DEVICE_SOF_SET_PRIORITY_REQUEST,
        DEVICE_LOGON_REQUEST,
        DEVICE_TMS_REQUEST,
        DEVICE_SETTLEMENT_REQUEST,
        DEVICE_PRE_SETTLEMENT_REQUEST,
        DEVICE_RESET_SEQUENCE_NUMBER_REQUEST,
        DEVICE_RETRIEVE_LAST_TRANSACTION_STATUS_REQUEST,
        DEVICE_RETRIEVE_LAST_SETTLEMENT_REQUEST,

        // AUTHENTICATION API
        MUTUAL_AUTHENTICATION_STEP_1_REQUEST,
        MUTUAL_AUTHENTICATION_STEP_2_REQUEST,

        // CARD API
        CARD_DETECT_REQUEST,
        CARD_DETAIL_REQUEST,
        CARD_HISTORICAL_LOG_REQUEST,

        // PAYMENT API
        PAYMENT_MODE_AUTO_REQUEST,
        PAYMENT_MODE_EFT_REQUEST,
        PAYMENT_MODE_EFT_NETS_QR_REQUEST,
        PAYMENT_MODE_BCA_REQUEST,
        PAYMENT_MODE_CREDIT_CARD_REQUEST,
        PAYMENT_MODE_NCC_REQUEST,
        PAYMENT_MODE_NFP_REQUEST,
        PAYMENT_MODE_EZ_LINK_REQUEST,
        PRE_AUTHORIZATION_REQUEST,
        PRE_AUTHORIZATION_COMPLETION_REQUEST,
        INSTALLATION_REQUEST,

        // CANCELLATION API
        VOID_PAYMENT_REQUEST,
        REFUND_REQUEST,
        CANCEL_COMMAND_REQUEST,

        // TOP-UP API
        TOP_UP_NETS_NCC_BY_NETS_EFT_REQUEST,
        TOP_UP_NETS_NFP_BY_NETS_EFT_REQUEST,

        // OTHER API
        CASE_DEPOSIT_REQUEST,

        // PASSTHROUGH
        UOB_REQUEST
    };

    // Define enum class for message direction
    enum class MSG_DIRECTION : uint8_t
    {
        MSG_DIRECTION_CONTROLLER_TO_DEVICE  = 0x00,
        MSG_DIRECTION_DEVICE_TO_CONTROLLER  = 0x01
    };

    // Define enum class for message class
    enum class MSG_CLASS : uint16_t
    {
        MSG_CLASS_NONE              = 0x0000,
        MSG_CLASS_REQ               = 0x0001,
        MSG_CLASS_ACK               = 0x0002,
        MSG_CLASS_RSP               = 0x0003
    };

    // Define enum class for message type
    enum class MSG_TYPE : uint32_t
    {
        MSG_TYPE_DEVICE             = 0x10000000u,
        MSG_TYPE_AUTHENTICATION     = 0x20000000u,
        MSG_TYPE_CARD               = 0x30000000u,
        MSG_TYPE_PAYMENT            = 0x40000000u,
        MSG_TYPE_CANCELLATION       = 0x50000000u,
        MSG_TYPE_TOPUP              = 0x60000000u,
        MSG_TYPE_RECORD             = 0x70000000u,
        MSG_TYPE_PASSTHROUGH        = 0x80000000u,
        MSG_TYPE_OTHER              = 0xf0000000u
    };

    // Define enum class for message code
    enum class MSG_CODE : uint32_t
    {
        // MSG_TYPE_DEVICE
        MSG_CODE_DEVICE_STATUS                              = 0x10000000u,
        MSG_CODE_DEVICE_RESET                               = 0x20000000u,
        MSG_CODE_DEVICE_RESET_SEQUENCE_NUMBER               = 0x20000001u,
        MSG_CODE_DEVICE_TIME_SYNC                           = 0x30000000u,
        MSG_CODE_DEVICE_PROFILE                             = 0x40000000u,
        MSG_CODE_DEVICE_SOF_LIST                            = 0x50000000u,
        MSG_CODE_DEVICE_SOF_SET_PRIORISATION                = 0x50000001u,
        MSG_CODE_DEVICE_LOGON                               = 0x60000000u,
        MSG_CODE_DEVICE_TMS                                 = 0x60000001u,
        MSG_CODE_DEVICE_SETTLEMENT                          = 0x70000000u,
        MSG_CODE_DEVICE_SETTLEMENT_PRE                      = 0x70000001u,
        MSG_CODE_DEVICE_RETRIEVE_LAST_TRANSACTION_STATUS    = 0x70000002u,
        MSG_CODE_DEVICE_RETRIEVE_LAST_SETTLEMENT            = 0x70000003u,

        // MSG_AUTH
        MSG_CODE_AUTH_MUTUAL_STEP_01                        = 0x10000000u,
        MSG_CODE_AUTH_MUTUAL_STEP_02                        = 0x20000000u,

        // MSG_TYPE_CARD
        MSG_CODE_CARD_DETECT                                = 0x10000000u,
        MSG_CODE_CARD_READ_PURSE                            = 0x20000000u,
        MSG_CODE_CARD_READ_HISTORICAL_LOG                   = 0x30000000u,
        MSG_CODE_CARD_READ_RSVP                             = 0x40000000u,

        // MSG_TYPE_PAYMENT
        MSG_CODE_PAYMENT_AUTO                               = 0x10000000u,
        MSG_CODE_PAYMENT_EFT                                = 0x20000000u,
        MSG_CODE_PAYMENT_NCC                                = 0x30000000u,
        MSG_CODE_PAYMENT_NFP                                = 0x40000000u,
        MSG_CODE_PAYMENT_RSVP                               = 0x50000000u,
        MSG_CODE_PAYMENT_CRD                                = 0x60000000u,
        MSG_CODE_PAYMENT_DEB                                = 0x70000000u,
        MSG_CODE_PAYMENT_BCA                                = 0x80000000u,
        MSG_CODE_PAYMENT_EZL                                = 0x90000000u,
        MSG_CODE_PRE_AUTH                                   = 0xA0000000u,
        MSG_CODE_PRE_AUTH_COMPLETION                        = 0xA0000001u,
        MSG_CODE_PAYMENT_HOST                               = 0xF0000000u,

        // MSG_TYPE_CANCELLATION
        MSG_CODE_CANCELLATION_VOID                          = 0x10000000u,
        MSG_CODE_CANCELLATION_REFUND                        = 0x20000000u,
        MSG_CODE_CANCELLATION_CANCEL                        = 0x30000000u,

        // MSG_TYPE_TOPUP
        MSG_CODE_TOPUP_NCC                                  = 0x10000000u,
        MSG_CODE_TOPUP_NFP                                  = 0x20000000u,
        MSG_CODE_TOPUP_RSVP                                 = 0x30000000u,

        // MSG_TYPE_RECORD
        MSG_CODE_RECORD_SUMMARY                             = 0x10000000u,
        MSG_CODE_RECORD_UPLOAD                              = 0x20000000u,
        MSG_CODE_RECORD_CLEAR                               = 0x30000000u,

        // MSG_TYPE_PASSTHROUGH
        MSG_CODE_PASSTHROUGH_UOB                            = 0x10000000u,

        // MSG_TYPE_OTHER
        MSG_CODE_OTHER_CASH_DEPOSIT                         = 0x10000000u
    };

    // Define enum class for message status
    enum class MSG_STATUS : uint32_t
    {
        // General Status
        SUCCESS                     = 0x00000000u,
        PENDING                     = 0x00000001u,
        TIMEOUT                     = 0x00000002u,
        INVALID_PARAMETER           = 0x00000003u,
        INCORRECT_FLOW              = 0x00000004u,

        // Self define MSG_STATUS
        SEND_FAILED                 = 0x00000005u,
        ACK_TIMEOUT                 = 0x00000006u,
        RSP_TIMEOUT                 = 0x00000007u,
        PARSE_FAILED                = 0x00000008u,

        // Msg Status
        MSG_INTEGRITY_FAILED        = 0x10000000u,
        MSG_TYPE_NOT_SUPPORTED      = 0x10000001u,
        MSG_CODE_NOT_SUPPORTED      = 0x10000002u,
        MSG_AUTHENTICATION_REQUIRED = 0x10000003u,
        MSG_AUTHENTICATION_FAILED   = 0x10000004u,
        MSG_MAC_FAILED              = 0x10000005u,
        MSG_LENGTH_MISMATCH         = 0x10000006u,
        MSG_LENGTH_MINIMUM          = 0x10000007u,
        MSG_LENGTH_MAXIMUM          = 0x10000008u,
        MSG_VERSION_INVALID         = 0x10000009u,
        MSG_CLASS_INVALID           = 0x1000000Au,
        MSG_STATUS_INVALID          = 0x1000000Bu,
        MSG_ALGORITHM_INVALID       = 0x1000000Cu,
        MSG_ALGORITHM_IS_MANDATORY  = 0x1000000Du,
        MSG_KEY_INDEX_INVALID       = 0x1000000Eu,
        MSG_NOTIFICATION_INVALID    = 0x1000000Fu,
        MSG_COMPLETION_INVALID      = 0x10000010u,
        MSG_DATA_TRANSPARENCY_ERROR = 0x10000011u,
        MSG_INCOMPLETE              = 0x10000012u,
        FIELD_MANDATORY_MISSING     = 0x11000001u,
        FIELD_LENGTH_MINIMUM        = 0x11000002u,
        FIELD_LENGTH_INVALID        = 0x11000003u,
        FIELD_TYPE_INVALID          = 0x11000004u,
        FIELD_ENCODING_INVALID      = 0x11000005u,
        FIELD_DATA_INVALID          = 0x11000006u,
        FIELD_PARSING_ERROR         = 0x11000007u,

        // Card Status
        CARD_NOT_DETECTED           = 0x20000000u,
        CARD_ERROR                  = 0x20000001u,
        CARD_BLACKLISTED            = 0x20000002u,
        CARD_BLOCKED                = 0x20000003u,
        CARD_EXPIRED                = 0x20000004u,
        CARD_INVALID_ISSUER_ID      = 0x20000005u,
        CARD_INVALID_PURSE_VALUE    = 0x20000006u,
        CARD_CREDIT_NOT_ALLOWED     = 0x20000007u,
        CARD_DEBIT_NOT_ALLOWED      = 0x20000008u,
        CARD_INSUFFICIENT_FUND      = 0x20000009u,
        CARD_EXCEEDED_PURSE_VALUE   = 0x2000000Au,
        CARD_CREDIT_FAILED          = 0x2000000Bu,
        CARD_CREDIT_UNCONFIRMED     = 0x2000000Cu,
        CARD_DEBIT_FAILED           = 0x2000000Du,
        CARD_DEBIT_UNCONFIRMED      = 0x2000000Eu,

        // Host Status
        COMMUNICATION_NO_RESPONSE   = 0x30000000u,
        COMMUNICATION_ERROR         = 0x30000001u,

        // Source of Fund (SOF)
        SOF_INVALID_CARD            = 0x40000000u,
        SOF_INCORRECT_PIN           = 0x40000001u,
        SOF_INSUFFICIENT_FUND       = 0x40000002u,
        SOF_CLOSED                  = 0x40000003u,
        SOF_BLOCKED                 = 0x40000004u,
        SOF_REFER_TO_BANK           = 0x40000005u,
        SOF_CANCEL                  = 0x40000006u,
        SOF_HOST_RESPONSE_DECLINE   = 0x40000007u,
        SOF_LOGON_REQUIRED          = 0x40000008u
    };

    // Define enum class for Encryption Algorithm
    enum class ENCRYPTION_ALGORITHM : uint8_t
    {
        ENCRYPTION_ALGORITHM_NONE       = 0x00,
        ENCRYPTION_ALGORITHM_AES128     = 0x10,
        ENCRYPTION_ALGORITHM_AES192     = 0x11,
        ENCRYPTION_ALGORITHM_AES256     = 0x12,
        ENCRYPTION_ALGORITHM_DES1_1KEY  = 0x20,
        ENCRYPTION_ALGORITHM_DES3_2KEY  = 0x21,
        ENCRYPTION_ALGORITHM_DES3_3KEY  = 0x22
    };

    // Define enum class for field ID
    enum class FIELD_ID : uint16_t
    {
        ID_PADDING                              = 0x0000u,
        ID_DEVICE_DATE                          = 0x1000u,
        ID_DEVICE_TIME                          = 0x1001u,
        ID_DEVICE_MID                           = 0x1002u,
        ID_DEVICE_RID                           = 0x1003u,
        ID_DEVICE_TID                           = 0x1004u,
        ID_DEVICE_MER_CODE                      = 0x1005u,
        ID_DEVICE_MER_NAME                      = 0x1006u,
        ID_DEVICE_MER_ADDRESS                   = 0x1007u,
        ID_DEVICE_MER_ADDRESS2                  = 0x1008u,
        ID_SOF_TYPE                             = 0x2000u,
        ID_SOF_DESCRIPTION                      = 0x2001u,
        ID_SOF_PRIORITY                         = 0x2002u,
        ID_SOF_ACQUIRER                         = 0x2003u,
        ID_SOF_NAME                             = 0x2004u,
        ID_SOF_SALE_COUNT                       = 0x2005u,
        ID_SOF_SALE_TOTAL                       = 0x2006u,
        ID_SOF_REFUND_COUNT                     = 0x2007u,
        ID_SOF_REFUND_TOTAL                     = 0x2008u,
        ID_SOF_VOID_COUNT                       = 0x2009u,
        ID_SOF_VOID_TOTAL                       = 0x200Au,
        ID_SOF_PRE_AUTH_COMPLETE_COUNT          = 0x200Bu,
        ID_SOF_PRE_AUTH_COMPLETE_TOTAL          = 0x200Cu,
        ID_SOF_PRE_AUTH_COMPLETION_VOID_COUNT   = 0x200Du,
        ID_SOF_PRE_AUTH_COMPLETION_VOID_TOTAL   = 0x200Eu,
        ID_SOF_CASHBACK_COUNT                   = 0x200Fu,
        ID_SOF_CASHBACK_TOTAL                   = 0x2010u,
        ID_SOF_TOPUP_COUNT                      = 0x2011u,
        ID_SOF_TOPUP_TOTAL                      = 0x2012u,
        ID_AUTH_DATA_1                          = 0x3000u,
        ID_AUTH_DATA_2                          = 0x3001u,
        ID_AUTH_DATA_3                          = 0x3002u,
        ID_CARD_TYPE                            = 0x4000u,
        ID_CARD_CAN                             = 0x4001u,
        ID_CARD_CSN                             = 0x4002u,
        ID_CARD_BALANCE                         = 0x4003u,
        ID_CARD_COUNTER                         = 0x4004u,
        ID_CARD_EXPIRY                          = 0x4005u,
        ID_CARD_CERT                            = 0x4006u,
        ID_CARD_PREVIOUS_BAL                    = 0x4007u,
        ID_CARD_PURSE_STATUS                    = 0x4008u,
        ID_CARD_ATU_STATUS                      = 0x4009u,
        ID_CARD_NUM_MASKED                      = 0x400Au,
        ID_CARD_HOLDER_NAME                     = 0x400Bu,
        ID_CARD_SCHEME_NAME                     = 0x400Cu,
        ID_CARD_AID                             = 0x400Du,
        ID_CARD_CEPAS_VER                       = 0x400Eu,
        ID_TXN_TYPE                             = 0x5000u,
        ID_TXN_AMOUNT                           = 0x5001u,
        ID_TXN_CASHBACK_AMOUNT                  = 0x5002u,
        ID_TXN_DATE                             = 0x5003u,
        ID_TXN_TIME                             = 0x5004u,
        ID_TXN_BATCH                            = 0x5005u,
        ID_TXN_CERT                             = 0x5006u,
        ID_TXN_CHECKSUM                         = 0x5007u,
        ID_TXN_DATA                             = 0x5008u,
        ID_TXN_STAN                             = 0x5009u,
        ID_TXN_MER                              = 0x500Au,
        ID_TXN_MER_REF_NUM                      = 0x500Bu,
        ID_TXN_RESPONSE_TEXT                    = 0x500Cu,
        ID_TXN_MER_NAME                         = 0x500Du,
        ID_TXN_MER_ADDRESS                      = 0x500Eu,
        ID_TXN_TID                              = 0x500Fu,
        ID_TXN_MID                              = 0x5010u,
        ID_TXN_APPROV_CODE                      = 0x5011u,
        ID_TXN_RRN                              = 0x5012u,
        ID_TXN_CARD_NAME                        = 0x5013u,
        ID_TXN_SERVICE_FEE                      = 0x5014u,
        ID_TXN_MARKETING_MSG                    = 0x5015u,
        ID_TXN_HOST_RESP_MSG_1                  = 0x5016u,
        ID_TXN_HOST_RESP_MSG_2                  = 0x5017u,
        ID_TXN_HOST_RESP_CODE                   = 0x5018u,
        ID_TXN_CARD_ENTRY_MODE                  = 0x5019u,
        ID_TXN_RECEIPT                          = 0x501Au,
        ID_TXN_AUTOLOAD_AMOUT                   = 0x501Bu,
        ID_TXN_INV_NUM                          = 0x501Cu,
        ID_TXN_TC                               = 0x501Du,
        ID_TXN_FOREIGN_AMOUNT                   = 0x501Eu,
        ID_TXN_FOREIGN_MID                      = 0x501Fu,
        ID_FOREIGN_CUR_NAME                     = 0x5020u,
        ID_TXN_POSID                            = 0x5021u,
        ID_TXN_ACQUIRER                         = 0x5022u,
        ID_TXN_HOST                             = 0x5023u,
        ID_TXN_LAST_HEADER                      = 0x5024u,
        ID_TXN_LAST_PAYLOAD                     = 0x5025u,
        ID_TXN_RECEIPT_REQUIRED                 = 0x5026u,
        ID_TXN_FEE_DUE_TO_MERCHANT              = 0x5027u,
        ID_TXN_FEE_DUE_FROM_MERCHANT            = 0x5028u,
        ID_VEHICLE_LICENSE_PLATE                = 0x6000u,
        ID_VEHICLE_DEVICE_TYPE                  = 0x6001u,
        ID_VEHICLE_DEVICE_ID                    = 0x6002u
    };

    // Define enum class for field encoding
    enum class FIELD_ENCODING : uint8_t
    {
        FIELD_ENCODING_NONE             = 0x30u,
        FIELD_ENCODING_ARRAY_ASCII      = 0x31u,
        FIELD_ENCODING_ARRAY_ASCII_HEX  = 0x32u,
        FIELD_ENCODING_ARRAY_HEX        = 0x33u,
        FIELD_ENCODING_VALUE_ASCII      = 0x34u,
        FIELD_ENCODING_VALUE_ASCII_HEX  = 0x35u,
        FIELD_ENCODING_VALUE_BCD        = 0x36u,
        FIELD_ENCODING_VALUE_HEX_BIG    = 0x37u,
        FIELD_ENCODING_VALUE_HEX_LITTLE = 0x38u
    };

    struct CommandWithData
    {
        UPT_CMD cmd;
        std::shared_ptr<void> data;

        CommandWithData(UPT_CMD c, std::shared_ptr<void> d = nullptr) : cmd(c), data(d) {}
    };

    enum class HOST_ID
    {
        NETS,
        UPOS
    };

    struct CommandSettlementRequestData
    {
        HOST_ID hostIdValue;
    };

    struct CommandRetrieveLastTransactionStatusRequestData
    {
        HOST_ID hostIdValue;
    };

    struct CommandPaymentRequestData
    {
        uint32_t amount;
        std::string mer_ref_num;
    };

    enum class PaymentType
    {
        NETS_EFT,
        NETS_NCC,
        NETS_NFP,
        NETS_QR,
        EZL,
        SCHEME_CREDIT,
    };

    enum class CardType
    {
        CASH_CARD = 0,
        NETS_FLASH_PAY_CARD = 1,
        ATM_CARD = 2,
        EZLINK_CARD = 3,
        CREDIT_CARD = 4,
        DEBIT_CARD = 5,
        OTHERS = 6
    };

    struct CommandTopUpRequestData
    {
        uint32_t amount;
        std::string mer_ref_num;
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
        WAITING_FOR_ACK,
        WAITING_FOR_RESPONSE,
        CANCEL_COMMAND_REQUEST,
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
        START_PENDING_RESPONSE_TIMER,
        PENDING_RESPONSE_TIMER_CANCELLED_RSP_RECEIVED,
        PENDING_RESPONSE_TIMER_TIMEOUT,
        CANCEL_COMMAND,
        CANCEL_COMMAND_CLEAN_UP_AND_ENQUEUE,
        WRITE_TIMEOUT,
        EVENT_COUNT
    };

    struct EventTransition
    {
        EVENT event;
        void (Upt::*eventHandler)(EVENT);
        STATE nextState;
    };

    struct StateTransition
    {
        STATE stateName;
        std::vector<EventTransition> transitions;
    };

    struct SettlementPayloadRow
    {
        std::string acquirer;
        std::string name;
        std::string preAuthCompleteCount;
        std::string preAuthCompleteTotal;
        std::string saleCount;
        std::string saleTotal;
    };

    static Upt* getInstance();
    void FnUptInit(unsigned int baudRate, const std::string& comPortName);
    void FnUptSendDeviceResetSequenceNumberRequest();
    void FnUptSendCardDetectRequest();
    void FnUptSendDeviceAutoPaymentRequest(uint32_t amount, const std::string& mer_ref_num);
    void FnUptSendDeviceTimeSyncRequest();
    void FnUptSendDeviceLogonRequest();
    void FnUptSendDeviceStatusRequest();
    void FnUptSendDeviceSettlementNETSRequest();
    void FnUptSendDeviceRetrieveLastSettlementRequest();
    void FnUptSendDeviceTMSRequest();
    void FnUptSendDeviceResetRequest();
    void FnUptSendDeviceCancelCommandRequest();
    void FnUptSendDeviceRetrieveNETSLastTransactionStatusRequest();

    void FnUptSendDeviceNCCTopUpCommandRequest(uint32_t amount, const std::string& mer_ref_num);
    void FnUptSendDeviceNFPTopUpCommandRequest(uint32_t amount, const std::string& mer_ref_num);

    void FnUptClose();


    std::string getCommandString(UPT_CMD cmd);
    std::string getMsgStatusString(uint32_t msgStatus);
    std::string getMsgDirectionString(uint8_t msgDirection);
    std::string getMsgClassString(uint16_t msgClass);
    std::string getMsgTypeString(uint32_t msgType);
    std::string getMsgCodeString(uint32_t msgCode, uint32_t msgType);
    std::string getEncryptionAlgorithmString(uint8_t encryptionAlgorithm);
    std::string getCommandTitleString(uint8_t msgDirection, uint16_t msgClass, uint32_t msgCode, uint32_t msgType);
    char getFieldEncodingChar(uint8_t fieldEncoding);
    std::string getFieldEncodingTypeString(uint8_t fieldEncoding);
    std::string getFieldIDString(uint16_t fieldID);

    /**
     * Singleton Upt should not be cloneable.
     */
    Upt(Upt& upt) = delete;

    /**
     * Singleton Upt should not be assignable.
     */
    void operator=(const Upt&) = delete;

private:
    static Upt* upt_;
    static std::mutex mutex_;
    boost::asio::io_context ioContext_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    std::unique_ptr<boost::asio::serial_port> pSerialPort_;
    boost::asio::deadline_timer ackTimer_;
    boost::asio::deadline_timer rspTimer_;
    boost::asio::deadline_timer serialWriteDelayTimer_;
    boost::asio::deadline_timer serialWriteTimer_;
    std::atomic<bool> ackRecv_;
    std::atomic<bool> rspRecv_;
    std::atomic<bool> pendingRspRecv_;
    std::string logFileName_;
    std::thread ioContextThread_;
    std::mutex cmdQueueMutex_;
    std::deque<CommandWithData> commandQueue_;
    UPT_CMD currentCmd;
    static uint32_t sequenceNo_;
    static std::mutex sequenceNoMutex_;
    static std::mutex currentCmdMutex_;
    std::array<uint8_t, 1024> readBuffer_;
    std::queue<std::vector<uint8_t>> writeQueue_;
    bool write_in_progress_;
    std::array<uint8_t, 65535> rxBuffer_;
    int rxNum_;
    RX_STATE rxState_;
    static const StateTransition stateTransitionTable[static_cast<int>(STATE::STATE_COUNT)];
    STATE currentState_;
    std::chrono::steady_clock::time_point lastSerialReadTime_;
    Upt();
    void startIoContextThread();
    void incrementSequenceNo();
    void setSequenceNo(uint32_t sequenceNo);
    uint32_t getSequenceNo();
    void setCurrentCmd(UPT_CMD cmd);
    UPT_CMD getCurrentCmd();
    void enqueueCommand(UPT_CMD cmd, std::shared_ptr<void> data = nullptr);
    void enqueueCommandToFront(Upt::UPT_CMD cmd, std::shared_ptr<void> data = nullptr);
    void popFromCommandQueueAndEnqueueWrite();
    std::string eventToString(EVENT event);
    std::string stateToString(STATE state);
    void processEvent(EVENT event);
    void checkCommandQueue();
    void handleIdleState(EVENT event);
    void handleSendingRequestAsyncState(EVENT event);
    void handleWaitingForAckState(EVENT event);
    void handleWaitingForResponseState(EVENT event);
    void handleCancelCommandRequestState(EVENT event);
    void startSerialWriteTimer();
    void startAckTimer();
    void startResponseTimer();
    bool checkCmd(UPT_CMD cmd);
    std::vector<uint8_t> prepareCmd(UPT_CMD cmd, std::shared_ptr<void> payloadData);
    PayloadField createPayload(uint32_t length, uint16_t payloadFieldId, uint8_t fieldReserve, uint8_t fieldEncoding, const std::vector<uint8_t>& fieldData);
    void handleSerialWriteTimeout(const boost::system::error_code& error);
    void handleAckTimeout(const boost::system::error_code& error);
    void handleCmdResponseTimeout(const boost::system::error_code& error);
    void handleReceivedCmd(const std::vector<uint8_t>& dataBuff);
    void handleCmdResponse(const Message& msg);
    std::vector<SettlementPayloadRow> findReceivedSettlementPayloadData(const std::vector<PayloadField>& payloads);
    std::string findReceivedPayloadData(const std::vector<PayloadField>& payloads, uint16_t payloadFieldId);
    bool isRxResponseComplete(const std::vector<uint8_t>& dataBuff);
    bool isMsgStatusValid(uint32_t msgStatus);
    void handleCmdErrorOrTimeout(UPT_CMD cmd, MSG_STATUS msgStatus);

    // Serial read and write
    void resetRxBuffer();
    std::vector<uint8_t> getRxBuffer() const;
    void startRead();
    void readEnd(const boost::system::error_code& error, std::size_t bytesTransferred);
    void enqueueWrite(const std::vector<uint8_t>& data);
    void startWrite();
    void writeEnd(const boost::system::error_code& error, std::size_t bytesTransferred);
    void stopSerialWriteDelayTimer();
};
