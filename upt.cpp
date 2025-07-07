#include <boost/asio.hpp>
#include <endian.h>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>
#include "common.h"
#include "crc.h"
#include "event_manager.h"
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
    clear();
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

void MessageHeader::clear()
{
    length_ = 0;
    integrityCRC32_ = 0;
    msgVersion_ = 0;
    msgDirection_ = 0;
    msgTime_ = 0;
    msgSequence_ = 0;
    msgClass_ = 0;
    msgType_ = 0;
    msgCode_ = 0;
    msgCompletion_ = 0;
    msgNotification_ = 0;
    msgStatus_ = 0;
    deviceProvider_ = 0;
    deviceType_ = 0;
    deviceNumber_ = 0;
    encryptionAlgorithm_ = 0;
    encryptionKeyIndex_ = 0;
    encryptionMAC_.clear();
}

template<typename T>
void MessageHeader::appendToBuffer(std::vector<uint8_t>& buffer, T value) const
{
    uint8_t* bytePtr = reinterpret_cast<uint8_t*>(&value);
    buffer.insert(buffer.end(), bytePtr, bytePtr + sizeof(T));
}

std::vector<uint8_t> MessageHeader::toByteArray() const
{
    std::vector<uint8_t> buffer;

    appendToBuffer(buffer, length_);
    appendToBuffer(buffer, integrityCRC32_);
    appendToBuffer(buffer, msgVersion_);
    appendToBuffer(buffer, msgDirection_);
    appendToBuffer(buffer, msgTime_);
    appendToBuffer(buffer, msgSequence_);
    appendToBuffer(buffer, msgClass_);
    appendToBuffer(buffer, msgType_);
    appendToBuffer(buffer, msgCode_);
    appendToBuffer(buffer, msgCompletion_);
    appendToBuffer(buffer, msgNotification_);
    appendToBuffer(buffer, msgStatus_);
    appendToBuffer(buffer, deviceProvider_);
    appendToBuffer(buffer, deviceType_);
    appendToBuffer(buffer, deviceNumber_);
    appendToBuffer(buffer, encryptionAlgorithm_);
    appendToBuffer(buffer, encryptionKeyIndex_);
    
    buffer.insert(buffer.end(), encryptionMAC_.begin(), encryptionMAC_.end());

    return buffer;
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
    clear();
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

void PayloadField::clear()
{
    payloadFieldLength_ = 0;
    payloadFieldId_ = 0;
    fieldReserve_ = 0;
    fieldEncoding_ = 0;
    fieldData_.clear();
}

template<typename T>
void PayloadField::appendToBuffer(std::vector<uint8_t>& buffer, T value) const
{
    uint8_t* bytePtr = reinterpret_cast<uint8_t*>(&value);
    buffer.insert(buffer.end(), bytePtr, bytePtr + sizeof(T));
}

std::vector<uint8_t> PayloadField::toByteArray() const
{
    std::vector<uint8_t> buffer;

    appendToBuffer(buffer, payloadFieldLength_);
    appendToBuffer(buffer, payloadFieldId_);
    appendToBuffer(buffer, fieldReserve_);
    appendToBuffer(buffer, fieldEncoding_);

    buffer.insert(buffer.end(), fieldData_.begin(), fieldData_.end());

    return buffer;
}


// UPOS Message Class
Message::Message()
    : header()
{
    payloads.clear();
}

Message::~Message()
{
    clear();
}

void Message::setHeaderLength(uint32_t length)
{
    header.setLength(length);
}

uint32_t Message::getHeaderLength() const
{
    return header.getLength();
}

void Message::setHeaderIntegrityCRC32(uint32_t integrityCRC32)
{
    header.setIntegrityCRC32(integrityCRC32);
}

uint32_t Message::getHeaderIntegrityCRC32() const
{
    return header.getIntegrityCRC32();
}

void Message::setHeaderMsgVersion(uint8_t msgVersion)
{
    header.setMsgVersion(msgVersion);
}

uint8_t Message::getHeaderMsgVersion() const
{
    return header.getMsgVersion();
}

void Message::setHeaderMsgDirection(uint8_t msgDirection)
{
    header.setMsgDirection(msgDirection);
}

uint8_t Message::getHeaderMsgDirection() const
{
    return header.getMsgDirection();
}

void Message::setHeaderMsgTime(uint64_t msgTime)
{
    header.setMsgTime(msgTime);
}

uint64_t Message::getHeaderMsgTime() const
{
    return header.getMsgTime();
}

void Message::setHeaderMsgSequence(uint32_t msgSequence)
{
    header.setMsgSequence(msgSequence);
}

uint32_t Message::getHeaderMsgSequence() const
{
    return header.getMsgSequence();
}

void Message::setHeaderMsgClass(uint16_t msgClass)
{
    header.setMsgClass(msgClass);
}

uint16_t Message::getHeaderMsgClass() const
{
    return header.getMsgClass();
}

void Message::setHeaderMsgType(uint32_t msgType)
{
    header.setMsgType(msgType);
}

uint32_t Message::getHeaderMsgType() const
{
    return header.getMsgType();
}

void Message::setHeaderMsgCode(uint32_t msgCode)
{
    header.setMsgCode(msgCode);
}

uint32_t Message::getHeaderMsgCode() const
{
    return header.getMsgCode();
}

void Message::setHeaderMsgCompletion(uint8_t msgCompletion)
{
    header.setMsgCompletion(msgCompletion);
}

uint8_t Message::getHeaderMsgCompletion() const
{
    return header.getMsgCompletion();
}

void Message::setHeaderMsgNotification(uint8_t msgNotification)
{
    header.setMsgNotification(msgNotification);
}

uint8_t Message::getHeaderMsgNotification() const
{
    return header.getMsgNotification();
}

void Message::setHeaderMsgStatus(uint32_t msgStatus)
{
    header.setMsgStatus(msgStatus);
}

uint32_t Message::getHeaderMsgStatus() const
{
    return header.getMsgStatus();
}

void Message::setHeaderDeviceProvider(uint16_t deviceProvider)
{
    header.setDeviceProvider(deviceProvider);
}

uint16_t Message::getHeaderDeviceProvider() const
{
    return header.getDeviceProvider();
}

void Message::setHeaderDeviceType(uint16_t deviceType)
{
    header.setDeviceType(deviceType);
}

uint16_t Message::getHeaderDeviceType() const
{
    return header.getDeviceType();
}

void Message::setHeaderDeviceNumber(uint32_t deviceNumber)
{
    header.setDeviceNumber(deviceNumber);
}

uint32_t Message::getHeaderDeviceNumber() const
{
    return header.getDeviceNumber();
}

void Message::setHeaderEncryptionAlgorithm(uint8_t encryptionAlgorithm)
{
    header.setEncryptionAlgorithm(encryptionAlgorithm);
}

uint8_t Message::getHeaderEncryptionAlgorithm() const
{
    return header.getEncryptionAlgorithm();
}

void Message::setHeaderEncryptionKeyIndex(uint8_t encryptionKeyIndex)
{
    header.setEncryptionKeyIndex(encryptionKeyIndex);
}

uint8_t Message::getHeaderEncryptionKeyIndex() const
{
    return header.getEncryptionKeyIndex();
}

void Message::setHeaderEncryptionMAC(const std::vector<uint8_t>& encryptionMAC)
{
    header.setEncryptionMAC(encryptionMAC);
}

std::vector<uint8_t> Message::getHeaderEncryptionMAC() const
{
    return header.getEncryptionMAC();
}

std::vector<uint8_t> Message::getHeaderMsgVector() const
{
    return header.toByteArray();
}

void Message::addPayload(const PayloadField& payload)
{
    payloads.push_back(payload);
}

const std::vector<PayloadField>& Message::getPayloads() const
{
    return payloads;
}

std::vector<uint8_t> Message::getPayloadMsgVector() const
{
    std::vector<uint8_t> buffer;

    for (auto& payload : payloads)
    {
        std::vector<uint8_t> payloadBuffer = payload.toByteArray();
        buffer.insert(buffer.end(), payloadBuffer.begin(), payloadBuffer.end());
    }

    return buffer;
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

    CRC32 crc32;
    crc32.Init();
    crc32.Update(dataBuffer.data(), totalLength);
    uint32_t value = crc32.Value();

    return value;
}

std::vector<uint8_t> Message::FnAddDataTransparency(const std::vector<uint8_t>& input)
{
    std::vector<uint8_t> output;

    for (std::size_t i = 0; i < input.size(); i++)
    {
        if (input[i] == 0x02)
        {
            output.push_back(0x10);
            output.push_back(0x00);
        }
        else if (input[i] == 0x04)
        {
            output.push_back(0x10);
            output.push_back(0x01);
        }
        else if (input[i] == 0x10)
        {
            output.push_back(0x10);
            output.push_back(0x10);
        }
        else
        {
            output.push_back(input[i]);
        }
    }

    // STX
    output.insert(output.begin(), 0x02);
    // ETX
    output.push_back(0x04);

    return output;
}

uint32_t Message::FnRemoveDataTransparency(const std::vector<uint8_t>& input, std::vector<uint8_t>& output)
{
    uint32_t retMsgStatus = static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS);
    output.clear();

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
                    retMsgStatus = static_cast<uint32_t>(Upt::MSG_STATUS::MSG_DATA_TRANSPARENCY_ERROR);
                    break;
                }
            }
        }
        else
        {
            // Exclude the STX and ETX
            if (input[i] != 0x02 && input[i] != 0x04)
            {
                output.push_back(input[i]);
            }
        }

        if (retMsgStatus == static_cast<uint32_t>(Upt::MSG_STATUS::MSG_DATA_TRANSPARENCY_ERROR))
        {
            output.clear();
            break;
        }
    }

    return retMsgStatus;
}

uint32_t Message::FnParseMsgData(const std::vector<uint8_t>& msgData)
{
    uint32_t retMsgStatus = static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS);
    clear();

    // Remove the Data Transparency
    std::vector<uint8_t> payload;
    uint32_t retRemoveDataTransparency = FnRemoveDataTransparency(msgData, payload);

    if (retRemoveDataTransparency == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS)) // If data transparency successfully
    {
        if (isValidHeaderSize(payload.size()))
        {
            // Check payload length is match with rx payload length
            uint32_t length = Common::getInstance()->FnConvertToUint32(Common::getInstance()->FnExtractSubVector(payload, Upt::MESSAGE_LENGTH_OFFSET, Upt::MESSAGE_LENGTH_SIZE));

            if (isMatchHeaderSize(length, payload.size()))
            {
                uint32_t calculatedCRC32;
                std::vector<uint8_t> payloadCalculateCRC32(payload.begin() + Upt::MESSAGE_VERSION_OFFSET, payload.end());
                CRC32 crc32;
                crc32.Init();
                crc32.Update(payloadCalculateCRC32.data(), payloadCalculateCRC32.size());
                calculatedCRC32 = crc32.Value();

                uint32_t payloadIntegrityCRC32 = Common::getInstance()->FnConvertToUint32(Common::getInstance()->FnExtractSubVector(payload, Upt::MESSAGE_INTEGRITY_CRC32_OFFSET, Upt::MESSAGE_INTEGRITY_CRC32_SIZE));

                if (isMatchCRC(calculatedCRC32, payloadIntegrityCRC32))
                {
                    // Extract the header
                    header.setLength(length);
                    header.setIntegrityCRC32(payloadIntegrityCRC32);
                    header.setMsgVersion(Common::getInstance()->FnConvertToUint8(Common::getInstance()->FnExtractSubVector(payload, Upt::MESSAGE_VERSION_OFFSET, Upt::MESSAGE_VERSION_SIZE)));
                    header.setMsgDirection(Common::getInstance()->FnConvertToUint8(Common::getInstance()->FnExtractSubVector(payload, Upt::MESSAGE_DIRECTION_OFFSET, Upt::MESSAGE_DIRECTION_SIZE)));
                    header.setMsgTime(Common::getInstance()->FnConvertToUint64(Common::getInstance()->FnExtractSubVector(payload, Upt::MESSAGE_TIME_OFFSET, Upt::MESSAGE_TIME_SIZE)));
                    header.setMsgSequence(Common::getInstance()->FnConvertToUint32(Common::getInstance()->FnExtractSubVector(payload, Upt::MESSAGE_SEQUENCE_OFFSET, Upt::MESSAGE_SEQUENCE_SIZE)));
                    header.setMsgClass(Common::getInstance()->FnConvertToUint16(Common::getInstance()->FnExtractSubVector(payload, Upt::MESSAGE_CLASS_OFFSET, Upt::MESSAGE_CLASS_SIZE)));
                    header.setMsgType(Common::getInstance()->FnConvertToUint32(Common::getInstance()->FnExtractSubVector(payload, Upt::MESSAGE_TYPE_OFFSET, Upt::MESSAGE_TYPE_SIZE)));
                    header.setMsgCode(Common::getInstance()->FnConvertToUint32(Common::getInstance()->FnExtractSubVector(payload, Upt::MESSAGE_CODE_OFFSET, Upt::MESSAGE_CODE_SIZE)));
                    header.setMsgCompletion(Common::getInstance()->FnConvertToUint8(Common::getInstance()->FnExtractSubVector(payload, Upt::MESSAGE_COMPLETION_OFFSET, Upt::MESSAGE_COMPLETION_SIZE)));
                    header.setMsgNotification(Common::getInstance()->FnConvertToUint8(Common::getInstance()->FnExtractSubVector(payload, Upt::MESSAGE_NOTIFICATION_OFFSET, Upt::MESSAGE_NOTIFICATION_SIZE)));
                    header.setMsgStatus(Common::getInstance()->FnConvertToUint32(Common::getInstance()->FnExtractSubVector(payload, Upt::MESSAGE_STATUS_OFFSET, Upt::MESSAGE_STATUS_SIZE)));
                    header.setDeviceProvider(Common::getInstance()->FnConvertToUint16(Common::getInstance()->FnExtractSubVector(payload, Upt::DEVICE_PROVIDER_OFFSET, Upt::DEVICE_PROVIDER_SIZE)));
                    header.setDeviceType(Common::getInstance()->FnConvertToUint16(Common::getInstance()->FnExtractSubVector(payload, Upt::DEVICE_TYPE_OFFSET, Upt::DEVICE_TYPE_SIZE)));
                    header.setDeviceNumber(Common::getInstance()->FnConvertToUint32(Common::getInstance()->FnExtractSubVector(payload, Upt::DEVICE_NUMBER_OFFSET, Upt::DEVICE_NUMBER_SIZE)));
                    header.setEncryptionAlgorithm(Common::getInstance()->FnConvertToUint8(Common::getInstance()->FnExtractSubVector(payload, Upt::ENCRYPTION_ALGORITHM_OFFSET, Upt::ENCRYPTION_ALGORITHM_SIZE)));
                    header.setEncryptionKeyIndex(Common::getInstance()->FnConvertToUint8(Common::getInstance()->FnExtractSubVector(payload, Upt::ENCRYPTION_KEY_INDEX_OFFSET, Upt::ENCRYPTION_KEY_INDEX_SIZE)));
                    header.setEncryptionMAC(Common::getInstance()->FnExtractSubVector(payload, Upt::ENCRYPTION_MAC_OFFSET, Upt::ENCRYPTION_MAC_SIZE));

                    // Extract payload field
                    std::size_t payloadStartIndex = Upt::PAYLOAD_OFFSET;

                    while ((payloadStartIndex + 4) <= length)
                    {
                        // Extract payload field length
                        uint32_t payloadFieldHeaderLength = Common::getInstance()->FnConvertToUint32(Common::getInstance()->FnExtractSubVector(payload, payloadStartIndex, Upt::PAYLOAD_FIELD_LENGTH_SIZE));

                        if ((payloadStartIndex + payloadFieldHeaderLength) <= length)
                        {
                            std::vector<uint8_t> payloadFieldData(payload.begin() + payloadStartIndex, payload.begin() + payloadStartIndex + payloadFieldHeaderLength);

                            PayloadField field;
                            field.setPayloadFieldLength(payloadFieldHeaderLength);
                            field.setPayloadFieldId(Common::getInstance()->FnConvertToUint16(Common::getInstance()->FnExtractSubVector(payloadFieldData, 4, Upt::PAYLOAD_FIELD_ID_SIZE)));
                            field.setFieldReserve(Common::getInstance()->FnConvertToUint8(Common::getInstance()->FnExtractSubVector(payloadFieldData, 6, Upt::PAYLOAD_FIELD_RESERVE_SIZE)));
                            field.setFieldEnconding(Common::getInstance()->FnConvertToUint8(Common::getInstance()->FnExtractSubVector(payloadFieldData, 7, Upt::PAYLOAD_FIELD_ENCODING_SIZE)));
                            field.setFieldData(Common::getInstance()->FnExtractSubVector(payloadFieldData, 8, (payloadFieldHeaderLength - 8)));

                            payloads.push_back(field);

                            payloadStartIndex += payloadFieldHeaderLength;
                        }
                        else
                        {
                            return static_cast<uint32_t>(Upt::MSG_STATUS::FIELD_LENGTH_INVALID);
                        }
                    }
                }
                else
                {
                    return static_cast<uint32_t>(Upt::MSG_STATUS::MSG_INTEGRITY_FAILED);
                }
            }
            else
            {
                return static_cast<uint32_t>(Upt::MSG_STATUS::MSG_LENGTH_MISMATCH);
            }
        }
        else
        {
            if (isInvalidHeaderSizeLessThan64(payload.size()))
            {
                return static_cast<uint32_t>(Upt::MSG_STATUS::MSG_LENGTH_MINIMUM);
            }
            else
            {
                return static_cast<uint32_t>(Upt::MSG_STATUS::MSG_LENGTH_MAXIMUM);
            }
        }
    }
    else
    {
        return static_cast<uint32_t>(Upt::MSG_STATUS::MSG_DATA_TRANSPARENCY_ERROR);
    }

    return retMsgStatus;
}

std::string Message::FnGetMsgOutputLogString(const std::vector<uint8_t>& msgData)
{
    std::ostringstream oss;

    Message msg;
    uint32_t msgParseResult = msg.FnParseMsgData(msgData);

    if (msgParseResult == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
    {
        std::vector<uint8_t> dataWithTransparency = msgData;
        std::vector<uint8_t> dataWithoutTransparency;
        msg.FnRemoveDataTransparency(dataWithTransparency, dataWithoutTransparency);

        oss << Upt::getInstance()->getCommandTitleString(msg.getHeaderMsgDirection(), msg.getHeaderMsgClass(), msg.getHeaderMsgCode(), msg.getHeaderMsgType()) << std::endl;
        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "--------------------------------------------------" << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "MSG           - ECR (WITH TRANSPARENCY)                         [";
        oss << std::setw(4) << std::setfill('0') << dataWithTransparency.size() << "] [H] ";
        oss << Common::getInstance()->FnGetDisplayVectorCharToHexString(dataWithTransparency) << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "MSG           - ECR (NO TRANSPARENCY)                           [";
        oss << std::setw(4) << std::setfill('0') << dataWithoutTransparency.size() << "] [H] ";
        oss << Common::getInstance()->FnGetDisplayVectorCharToHexString(dataWithoutTransparency) << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "MSG           - LENGTH                                          [";
        oss << std::setw(4) << std::setfill('0') << 4 << "] [D] ";
        oss << msg.getHeaderLength() << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "MSG           - INTEGRITY (CRC32)                               [";
        oss << std::setw(4) << std::setfill('0') << 4 << "] [H] ";
        oss << std::setw(8) << std::setfill('0') << std::hex << msg.getHeaderIntegrityCRC32() << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "MSG           - VERSION                                         [";
        oss << std::setw(4) << std::setfill('0') << 1 << "] [H] ";
        oss << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(msg.getHeaderMsgVersion()) << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "MSG           - DIRECTION                                       [";
        oss << std::setw(4) << std::setfill('0') << 1 << "] [H] ";
        oss << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(msg.getHeaderMsgDirection());
        oss << std::setw(6) << std::setfill(' ') << "";
        oss << std::setw(10) << std::setfill(' ') << "";
        oss << Upt::getInstance()->getMsgDirectionString(msg.getHeaderMsgDirection()) << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "MSG           - TIME                                            [";
        oss << std::setw(4) << std::setfill('0') << 8 << "] [D] ";
        oss << msg.getHeaderMsgTime();
        oss << std::setw(10) << std::setfill(' ') << "";
        oss << Common::getInstance()->FnConvertSecondsSince1January0000ToDateTime(msg.getHeaderMsgTime()) << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "MSG           - SEQUENCE                                        [";
        oss << std::setw(4) << std::setfill('0') << 4 << "] [D] ";
        oss << msg.getHeaderMsgSequence() << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "MSG           - CLASS                                           [";
        oss << std::setw(4) << std::setfill('0') << 2 << "] [H] ";
        oss << std::setw(4) << std::setfill('0') << std::hex << static_cast<int>(msg.getHeaderMsgClass());
        oss << std::setw(4) << std::setfill(' ') << "";
        oss << std::setw(10) << std::setfill(' ') << "";
        oss << Upt::getInstance()->getMsgClassString(msg.getHeaderMsgClass()) << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "MSG           - TYPE                                            [";
        oss << std::setw(4) << std::setfill('0') << 4 << "] [H] ";
        oss << std::setw(8) << std::setfill('0') << std::hex << msg.getHeaderMsgType();
        oss << std::setw(10) << std::setfill(' ') << "";
        oss << Upt::getInstance()->getMsgTypeString(msg.getHeaderMsgType()) << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "MSG           - CODE                                            [";
        oss << std::setw(4) << std::setfill('0') << 4 << "] [H] ";
        oss << std::setw(8) << std::setfill('0') << std::hex << msg.getHeaderMsgCode();
        oss << std::setw(10) << std::setfill(' ') << "";
        oss << Upt::getInstance()->getMsgCodeString(msg.getHeaderMsgCode(), msg.getHeaderMsgType()) << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "MSG           - COMPLETION                                      [";
        oss << std::setw(4) << std::setfill('0') << 1 << "] [H] ";
        oss << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(msg.getHeaderMsgCompletion()) << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "MSG           - NOTIFICATION                                    [";
        oss << std::setw(4) << std::setfill('0') << 1 << "] [H] ";
        oss << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(msg.getHeaderMsgNotification()) << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "MSG           - STATUS                                          [";
        oss << std::setw(4) << std::setfill('0') << 4 << "] [H] ";
        oss << std::setw(8) << std::setfill('0') << std::hex << msg.getHeaderMsgStatus();
        oss << std::setw(10) << std::setfill(' ') << "";
        oss << Upt::getInstance()->getMsgStatusString(msg.getHeaderMsgStatus()) << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "DEVICE        - PROVIDER                                        [";
        oss << std::setw(4) << std::setfill('0') << 2 << "] [D] ";
        oss << msg.getHeaderDeviceProvider() << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "DEVICE        - TYPE                                            [";
        oss << std::setw(4) << std::setfill('0') << 2 << "] [D] ";
        oss << msg.getHeaderDeviceType() << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "DEVICE        - NUMBER                                          [";
        oss << std::setw(4) << std::setfill('0') << 4 << "] [D] ";
        oss << msg.getHeaderDeviceNumber() << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "ENCRYPTION    - ALGORITHM                                       [";
        oss << std::setw(4) << std::setfill('0') << 1 << "] [H] ";
        oss << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(msg.getHeaderEncryptionAlgorithm());
        oss << std::setw(6) << std::setfill(' ') << "";
        oss << std::setw(10) << std::setfill(' ') << "";
        oss << Upt::getInstance()->getEncryptionAlgorithmString(msg.getHeaderEncryptionAlgorithm()) << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "ENCRYPTION    - KEY INDEX                                       [";
        oss << std::setw(4) << std::setfill('0') << 1 << "] [H] ";
        oss << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(msg.getHeaderEncryptionKeyIndex()) << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "ENCRYPTION    - MAC                                             [";
        oss << std::setw(4) << std::setfill('0') << 16 << "] [H] ";
        oss << Common::getInstance()->FnGetDisplayVectorCharToHexString(msg.getHeaderEncryptionMAC()) << std::endl;

        oss << std::setw(32) << std::setfill(' ') << "";
        oss << "PAYLOAD       - FIELD TOTAL     [D] " << std::setw(4) << std::setfill('0') << msg.getPayloads().size() << "                        [";
        oss << std::setw(4) << std::setfill('0') << std::hex << (dataWithoutTransparency.size() - 64) << "] [H] ";
        std::vector<uint8_t> payloadField(dataWithoutTransparency.begin() + 64, dataWithoutTransparency.end());
        oss << Common::getInstance()->FnGetDisplayVectorCharToHexString(payloadField) << std::endl << std::endl;

        int i = 0;
        for (const auto& payload : msg.getPayloads())
        {
            i++;
            oss << std::setw(32) << std::setfill(' ') << "";
            oss << "FIELD " << std::setw(4) << std::setfill('0') << i << "    - [" << Upt::getInstance()->getFieldEncodingChar(payload.getFieldEncoding()) << "] ";
            oss << std::setw(11) << std::setfill(' ') << std::left << Upt::getInstance()->getFieldEncodingTypeString(payload.getFieldEncoding()) << std::right;
            oss << " [H] " << std::setw(4) << std::setfill('0') << std::hex << payload.getPayloadFieldId() << ": ";
            oss << std::setw(21) << std::setfill(' ') << std::left << Upt::getInstance()->getFieldIDString(payload.getPayloadFieldId()) << std::right;
            oss << " [" << std::setw(4) << std::setfill('0') << std::hex << payload.getPayloadFieldLength() << "] [H] " << Common::getInstance()->FnGetDisplayVectorCharToHexString(payload.toByteArray()) << std::endl;
            oss << std::setw(32) << std::setfill(' ') << "";
            oss << std::setw(64) << std::setfill(' ') << "";
            oss << "[" << std::setw(4) << std::setfill('0') << payload.getFieldData().size() << "] [H] " << Common::getInstance()->FnGetDisplayVectorCharToHexString(payload.getFieldData()) << std::endl << std::endl;
        }

    }
    else
    {
        oss << "Message parse failed. Unable to print out the message.";
    }

    return oss.str();
}

bool Message::isValidHeaderSize(uint32_t size)
{
    const uint32_t MAX_SIZE = 4294967295;   // Maximum value for 4 bytes;
    return (size >= 64 && size <= MAX_SIZE);
}

bool Message::isInvalidHeaderSizeLessThan64(uint32_t size)
{
    return (size < 64);
}

bool Message::isMatchHeaderSize(uint32_t size1, uint32_t size2)
{
    return (size1 == size2);
}

bool Message::isMatchCRC(uint32_t crc1, uint32_t crc2)
{
    return (crc1 == crc2);
}

void Message::clear()
{
    header.clear();
    payloads.clear();
}


// Upos Terminal Class
Upt* Upt::upt_ = nullptr;
std::mutex Upt::mutex_;
uint32_t Upt::sequenceNo_ = 0;
std::mutex Upt::sequenceNoMutex_;
std::mutex Upt::currentCmdMutex_;

Upt::Upt()
    : ioContext_(),
    strand_(boost::asio::make_strand(ioContext_)),
    workGuard_(boost::asio::make_work_guard(ioContext_)),
    ackTimer_(ioContext_),
    rspTimer_(ioContext_),
    serialWriteDelayTimer_(ioContext_),
    serialWriteTimer_(ioContext_),
    ackRecv_(false),
    rspRecv_(false),
    pendingRspRecv_(false),
    currentCmd(Upt::UPT_CMD::DEVICE_STATUS_REQUEST),
    write_in_progress_(false),
    rxState_(Upt::RX_STATE::RX_START),
    currentState_(Upt::STATE::IDLE),
    lastSerialReadTime_(std::chrono::steady_clock::now()),
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
            FnUptSendDeviceResetSequenceNumberRequest();
            FnUptSendDeviceLogonRequest();
        }
        else
        {
            ss << "UPOS Terminal initialization failed.";
        }
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), logFileName_, "UPT");
    }
    catch (const boost::system::system_error& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Boost Asio Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
}

void Upt::FnUptClose()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "UPT");

    workGuard_.reset();
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

void Upt::incrementSequenceNo()
{
    std::unique_lock<std::mutex> lock(sequenceNoMutex_);

    if (sequenceNo_ == 0xFFFFFFFF)
    {
        sequenceNo_ = 1;
    }
    else
    {
        ++sequenceNo_;
    }
}

void Upt::setSequenceNo(uint32_t sequenceNo)
{
    std::unique_lock<std::mutex> lock(sequenceNoMutex_);

    sequenceNo_ = sequenceNo;
}

uint32_t Upt::getSequenceNo()
{
    std::unique_lock<std::mutex> lock(sequenceNoMutex_);

    return sequenceNo_;
}

void Upt::setCurrentCmd(Upt::UPT_CMD cmd)
{
    std::unique_lock<std::mutex> lock(currentCmdMutex_);

    currentCmd = cmd;
}

Upt::UPT_CMD Upt::getCurrentCmd()
{
    std::unique_lock<std::mutex> lock(currentCmdMutex_);

    return currentCmd;
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

std::string Upt::getMsgStatusString(uint32_t msgStatus)
{
    std::string msgStatusStr = "";

    switch (static_cast<MSG_STATUS>(msgStatus))
    {
        case MSG_STATUS::SUCCESS:
        {
            msgStatusStr = "SUCCESS";
            break;
        }
        case MSG_STATUS::PENDING:
        {
            msgStatusStr = "PENDING";
            break;
        }
        case MSG_STATUS::TIMEOUT:
        {
            msgStatusStr = "TIMEOUT";
            break;
        }
        case MSG_STATUS::INVALID_PARAMETER:
        {
            msgStatusStr = "INVALID_PARAMETER";
            break;
        }
        case MSG_STATUS::INCORRECT_FLOW:
        {
            msgStatusStr = "INCORRECT_FLOW";
            break;
        }
        case MSG_STATUS::MSG_INTEGRITY_FAILED:
        {
            msgStatusStr = "MSG_INTEGRITY_FAILED";
            break;
        }
        case MSG_STATUS::MSG_TYPE_NOT_SUPPORTED:
        {
            msgStatusStr = "MSG_TYPE_NOT_SUPPORTED";
            break;
        }
        case MSG_STATUS::MSG_CODE_NOT_SUPPORTED:
        {
            msgStatusStr = "MSG_CODE_NOT_SUPPORTED";
            break;
        }
        case MSG_STATUS::MSG_AUTHENTICATION_REQUIRED:
        {
            msgStatusStr = "MSG_AUTHENTICATION_REQUIRED";
            break;
        }
        case MSG_STATUS::MSG_AUTHENTICATION_FAILED:
        {
            msgStatusStr = "MSG_AUTHENTICATION_FAILED";
            break;
        }
        case MSG_STATUS::MSG_MAC_FAILED:
        {
            msgStatusStr = "MSG_MAC_FAILED";
            break;
        }
        case MSG_STATUS::MSG_LENGTH_MISMATCH:
        {
            msgStatusStr = "MSG_LENGTH_MISMATCH";
            break;
        }
        case MSG_STATUS::MSG_LENGTH_MINIMUM:
        {
            msgStatusStr = "MSG_LENGTH_MINIMUM";
            break;
        }
        case MSG_STATUS::MSG_LENGTH_MAXIMUM:
        {
            msgStatusStr = "MSG_LENGTH_MAXIMUM";
            break;
        }
        case MSG_STATUS::MSG_VERSION_INVALID:
        {
            msgStatusStr = "MSG_VERSION_INVALID";
            break;
        }
        case MSG_STATUS::MSG_CLASS_INVALID:
        {
            msgStatusStr = "MSG_CLASS_INVALID";
            break;
        }
        case MSG_STATUS::MSG_STATUS_INVALID:
        {
            msgStatusStr = "MSG_STATUS_INVALID";
            break;
        }
        case MSG_STATUS::MSG_ALGORITHM_INVALID:
        {
            msgStatusStr = "MSG_ALGORITHM_INVALID";
            break;
        }
        case MSG_STATUS::MSG_ALGORITHM_IS_MANDATORY:
        {
            msgStatusStr = "MSG_ALGORITHM_IS_MANDATORY";
            break;
        }
        case MSG_STATUS::MSG_KEY_INDEX_INVALID:
        {
            msgStatusStr = "MSG_KEY_INDEX_INVALID";
            break;
        }
        case MSG_STATUS::MSG_NOTIFICATION_INVALID:
        {
            msgStatusStr = "MSG_NOTIFICATION_INVALID";
            break;
        }
        case MSG_STATUS::MSG_COMPLETION_INVALID:
        {
            msgStatusStr = "MSG_COMPLETION_INVALID";
            break;
        }
        case MSG_STATUS::MSG_DATA_TRANSPARENCY_ERROR:
        {
            msgStatusStr = "MSG_DATA_TRANSPARENCY_ERROR";
            break;
        }
        case MSG_STATUS::MSG_INCOMPLETE:
        {
            msgStatusStr = "MSG_INCOMPLETE";
            break;
        }
        case MSG_STATUS::FIELD_MANDATORY_MISSING:
        {
            msgStatusStr = "FIELD_MANDATORY_MISSING";
            break;
        }
        case MSG_STATUS::FIELD_LENGTH_MINIMUM:
        {
            msgStatusStr = "FIELD_LENGTH_MINIMUM";
            break;
        }
        case MSG_STATUS::FIELD_LENGTH_INVALID:
        {
            msgStatusStr = "FIELD_LENGTH_INVALID";
            break;
        }
        case MSG_STATUS::FIELD_TYPE_INVALID:
        {
            msgStatusStr = "FIELD_TYPE_INVALID";
            break;
        }
        case MSG_STATUS::FIELD_ENCODING_INVALID:
        {
            msgStatusStr = "FIELD_ENCODING_INVALID";
            break;
        }
        case MSG_STATUS::FIELD_DATA_INVALID:
        {
            msgStatusStr = "FIELD_DATA_INVALID";
            break;
        }
        case MSG_STATUS::FIELD_PARSING_ERROR:
        {
            msgStatusStr = "FIELD_PARSING_ERROR";
            break;
        }
        case MSG_STATUS::CARD_NOT_DETECTED:
        {
            msgStatusStr = "CARD_NOT_DETECTED";
            break;
        }
        case MSG_STATUS::CARD_ERROR:
        {
            msgStatusStr = "CARD_ERROR";
            break;
        }
        case MSG_STATUS::CARD_BLACKLISTED:
        {
            msgStatusStr = "CARD_BLACKLISTED";
            break;
        }
        case MSG_STATUS::CARD_BLOCKED:
        {
            msgStatusStr = "CARD_BLOCKED";
            break;
        }
        case MSG_STATUS::CARD_EXPIRED:
        {
            msgStatusStr = "CARD_EXPIRED";
            break;
        }
        case MSG_STATUS::CARD_INVALID_ISSUER_ID:
        {
            msgStatusStr = "CARD_INVALID_ISSUER_ID";
            break;
        }
        case MSG_STATUS::CARD_INVALID_PURSE_VALUE:
        {
            msgStatusStr = "CARD_INVALID_PURSE_VALUE";
            break;
        }
        case MSG_STATUS::CARD_CREDIT_NOT_ALLOWED:
        {
            msgStatusStr = "CARD_CREDIT_NOT_ALLOWED";
            break;
        }
        case MSG_STATUS::CARD_DEBIT_NOT_ALLOWED:
        {
            msgStatusStr = "CARD_DEBIT_NOT_ALLOWED";
            break;
        }
        case MSG_STATUS::CARD_INSUFFICIENT_FUND:
        {
            msgStatusStr = "CARD_INSUFFICIENT_FUND";
            break;
        }
        case MSG_STATUS::CARD_EXCEEDED_PURSE_VALUE:
        {
            msgStatusStr = "CARD_EXCEEDED_PURSE_VALUE";
            break;
        }
        case MSG_STATUS::CARD_CREDIT_FAILED:
        {
            msgStatusStr = "CARD_CREDIT_FAILED";
            break;
        }
        case MSG_STATUS::CARD_CREDIT_UNCONFIRMED:
        {
            msgStatusStr = "CARD_CREDIT_UNCONFIRMED";
            break;
        }
        case MSG_STATUS::CARD_DEBIT_FAILED:
        {
            msgStatusStr = "CARD_DEBIT_FAILED";
            break;
        }
        case MSG_STATUS::CARD_DEBIT_UNCONFIRMED:
        {
            msgStatusStr = "CARD_DEBIT_UNCONFIRMED";
            break;
        }
        case MSG_STATUS::COMMUNICATION_NO_RESPONSE:
        {
            msgStatusStr = "COMMUNICATION_NO_RESPONSE";
            break;
        }
        case MSG_STATUS::COMMUNICATION_ERROR:
        {
            msgStatusStr = "COMMUNICATION_ERROR";
            break;
        }
        case MSG_STATUS::SOF_INVALID_CARD:
        {
            msgStatusStr = "SOF_INVALID_CARD";
            break;
        }
        case MSG_STATUS::SOF_INCORRECT_PIN:
        {
            msgStatusStr = "SOF_INCORRECT_PIN";
            break;
        }
        case MSG_STATUS::SOF_INSUFFICIENT_FUND:
        {
            msgStatusStr = "SOF_INSUFFICIENT_FUND";
            break;
        }
        case MSG_STATUS::SOF_CLOSED:
        {
            msgStatusStr = "SOF_CLOSED";
            break;
        }
        case MSG_STATUS::SOF_BLOCKED:
        {
            msgStatusStr = "SOF_BLOCKED";
            break;
        }
        case MSG_STATUS::SOF_REFER_TO_BANK:
        {
            msgStatusStr = "SOF_REFER_TO_BANK";
            break;
        }
        case MSG_STATUS::SOF_CANCEL:
        {
            msgStatusStr = "SOF_CANCEL";
            break;
        }
        case MSG_STATUS::SOF_HOST_RESPONSE_DECLINE:
        {
            msgStatusStr = "SOF_HOST_RESPONSE_DECLINE";
            break;
        }
        case MSG_STATUS::SOF_LOGON_REQUIRED:
        {
            msgStatusStr = "SOF_LOGON_REQUIRED";
            break;
        }
        default:
        {
            msgStatusStr = "Unknow Message Status.";
            break;
        }
    }

    return msgStatusStr;
}

std::string Upt::getMsgDirectionString(uint8_t msgDirection)
{
    std::string msgDirectionStr = "";

    switch (static_cast<MSG_DIRECTION>(msgDirection))
    {
        case MSG_DIRECTION::MSG_DIRECTION_CONTROLLER_TO_DEVICE:
        {
            msgDirectionStr = "MSG_DIRECTION_CONTROLLER_TO_DEVICE";
            break;
        }
        case MSG_DIRECTION::MSG_DIRECTION_DEVICE_TO_CONTROLLER:
        {
            msgDirectionStr = "MSG_DIRECTION_DEVICE_TO_CONTROLLER";
            break;
        }
    }

    return msgDirectionStr;
}

std::string Upt::getMsgClassString(uint16_t msgClass)
{
    std::string msgClassStr = "";

    switch (static_cast<MSG_CLASS>(msgClass))
    {
        case MSG_CLASS::MSG_CLASS_NONE:
        {
            msgClassStr = "MSG_CLASS_NONE";
            break;
        }
        case MSG_CLASS::MSG_CLASS_REQ:
        {
            msgClassStr = "MSG_CLASS_REQ";
            break;
        }
        case MSG_CLASS::MSG_CLASS_ACK:
        {
            msgClassStr = "MSG_CLASS_ACK";
            break;
        }
        case MSG_CLASS::MSG_CLASS_RSP:
        {
            msgClassStr = "MSG_CLASS_RSP";
            break;
        }
    }

    return msgClassStr;
}

std::string Upt::getMsgTypeString(uint32_t msgType)
{
    std::string msgTypeStr = "";

    switch (static_cast<MSG_TYPE>(msgType))
    {
        case MSG_TYPE::MSG_TYPE_DEVICE:
        {
            msgTypeStr = "MSG_TYPE_DEVICE";
            break;
        }
        case MSG_TYPE::MSG_TYPE_AUTHENTICATION:
        {
            msgTypeStr = "MSG_TYPE_AUTHENTICATION";
            break;
        }
        case MSG_TYPE::MSG_TYPE_CARD:
        {
            msgTypeStr = "MSG_TYPE_CARD";
            break;
        }
        case MSG_TYPE::MSG_TYPE_PAYMENT:
        {
            msgTypeStr = "MSG_TYPE_PAYMENT";
            break;
        }
        case MSG_TYPE::MSG_TYPE_CANCELLATION:
        {
            msgTypeStr = "MSG_TYPE_CANCELLATION";
            break;
        }
        case MSG_TYPE::MSG_TYPE_TOPUP:
        {
            msgTypeStr = "MSG_TYPE_TOPUP";
            break;
        }
        case MSG_TYPE::MSG_TYPE_RECORD:
        {
            msgTypeStr = "MSG_TYPE_RECORD";
            break;
        }
        case MSG_TYPE::MSG_TYPE_PASSTHROUGH:
        {
            msgTypeStr = "MSG_TYPE_PASSTHROUGH";
            break;
        }
        case MSG_TYPE::MSG_TYPE_OTHER:
        {
            msgTypeStr = "MSG_TYPE_OTHER";
            break;
        }
    }

    return msgTypeStr;
}

std::string Upt::getMsgCodeString(uint32_t msgCode, uint32_t msgType)
{
    std::string msgCodeStr = "";

    switch (static_cast<MSG_TYPE>(msgType))
    {
        case MSG_TYPE::MSG_TYPE_DEVICE:
        {
            switch (static_cast<MSG_CODE>(msgCode))
            {
                case MSG_CODE::MSG_CODE_DEVICE_STATUS:
                {
                    msgCodeStr = "MSG_CODE_DEVICE_STATUS";
                    break;
                }
                case MSG_CODE::MSG_CODE_DEVICE_RESET:
                {
                    msgCodeStr = "MSG_CODE_DEVICE_RESET";
                    break;
                }
                case MSG_CODE::MSG_CODE_DEVICE_RESET_SEQUENCE_NUMBER:
                {
                    msgCodeStr = "MSG_CODE_DEVICE_RESET_SEQUENCE_NUMBER";
                    break;
                }
                case MSG_CODE::MSG_CODE_DEVICE_TIME_SYNC:
                {
                    msgCodeStr = "MSG_CODE_DEVICE_TIME_SYNC";
                    break;
                }
                case MSG_CODE::MSG_CODE_DEVICE_PROFILE:
                {
                    msgCodeStr = "MSG_CODE_DEVICE_PROFILE";
                    break;
                }
                case MSG_CODE::MSG_CODE_DEVICE_SOF_LIST:
                {
                    msgCodeStr = "MSG_CODE_DEVICE_SOF_LIST";
                    break;
                }
                case MSG_CODE::MSG_CODE_DEVICE_SOF_SET_PRIORISATION:
                {
                    msgCodeStr = "MSG_CODE_DEVICE_SOF_SET_PRIORISATION";
                    break;
                }
                case MSG_CODE::MSG_CODE_DEVICE_LOGON:
                {
                    msgCodeStr = "MSG_CODE_DEVICE_LOGON";
                    break;
                }
                case MSG_CODE::MSG_CODE_DEVICE_TMS:
                {
                    msgCodeStr = "MSG_CODE_DEVICE_TMS";
                    break;
                }
                case MSG_CODE::MSG_CODE_DEVICE_SETTLEMENT:
                {
                    msgCodeStr = "MSG_CODE_DEVICE_SETTLEMENT";
                    break;
                }
                case MSG_CODE::MSG_CODE_DEVICE_SETTLEMENT_PRE:
                {
                    msgCodeStr = "MSG_CODE_DEVICE_SETTLEMENT_PRE";
                    break;
                }
                case MSG_CODE::MSG_CODE_DEVICE_RETRIEVE_LAST_TRANSACTION_STATUS:
                {
                    msgCodeStr = "MSG_CODE_DEVICE_RETRIEVE_LAST_TRANSACTION_STATUS";
                    break;
                }
                case MSG_CODE::MSG_CODE_DEVICE_RETRIEVE_LAST_SETTLEMENT:
                {
                    msgCodeStr = "MSG_CODE_DEVICE_RETRIEVE_LAST_SETTLEMENT";
                    break;
                }
            }
            break;
        }
        case MSG_TYPE::MSG_TYPE_AUTHENTICATION:
        {
            switch (static_cast<MSG_CODE>(msgCode))
            {
                case MSG_CODE::MSG_CODE_AUTH_MUTUAL_STEP_01:
                {
                    msgCodeStr = "MSG_CODE_AUTH_MUTUAL_STEP_01";
                    break;
                }
                case MSG_CODE::MSG_CODE_AUTH_MUTUAL_STEP_02:
                {
                    msgCodeStr = "MSG_CODE_AUTH_MUTUAL_STEP_02";
                    break;
                }
            }
            break;
        }
        case MSG_TYPE::MSG_TYPE_CARD:
        {
            switch (static_cast<MSG_CODE>(msgCode))
            {
                case MSG_CODE::MSG_CODE_CARD_DETECT:
                {
                    msgCodeStr = "MSG_CODE_CARD_DETECT";
                    break;
                }
                case MSG_CODE::MSG_CODE_CARD_READ_PURSE:
                {
                    msgCodeStr = "MSG_CODE_CARD_READ_PURSE";
                    break;
                }
                case MSG_CODE::MSG_CODE_CARD_READ_HISTORICAL_LOG:
                {
                    msgCodeStr = "MSG_CODE_CARD_READ_HISTORICAL_LOG";
                    break;
                }
                case MSG_CODE::MSG_CODE_CARD_READ_RSVP:
                {
                    msgCodeStr = "MSG_CODE_CARD_READ_RSVP";
                    break;
                }
            }
            break;
        }
        case MSG_TYPE::MSG_TYPE_PAYMENT:
        {
            switch (static_cast<MSG_CODE>(msgCode))
            {
                case MSG_CODE::MSG_CODE_PAYMENT_AUTO:
                {
                    msgCodeStr = "MSG_CODE_PAYMENT_AUTO";
                    break;
                }
                case MSG_CODE::MSG_CODE_PAYMENT_EFT:
                {
                    msgCodeStr = "MSG_CODE_PAYMENT_EFT";
                    break;
                }
                case MSG_CODE::MSG_CODE_PAYMENT_NCC:
                {
                    msgCodeStr = "MSG_CODE_PAYMENT_NCC";
                    break;
                }
                case MSG_CODE::MSG_CODE_PAYMENT_NFP:
                {
                    msgCodeStr = "MSG_CODE_PAYMENT_NFP";
                    break;
                }
                case MSG_CODE::MSG_CODE_PAYMENT_RSVP:
                {
                    msgCodeStr = "MSG_CODE_PAYMENT_RSVP";
                    break;
                }
                case MSG_CODE::MSG_CODE_PAYMENT_CRD:
                {
                    msgCodeStr = "MSG_CODE_PAYMENT_CRD";
                    break;
                }
                case MSG_CODE::MSG_CODE_PAYMENT_DEB:
                {
                    msgCodeStr = "MSG_CODE_PAYMENT_DEB";
                    break;
                }
                case MSG_CODE::MSG_CODE_PAYMENT_BCA:
                {
                    msgCodeStr = "MSG_CODE_PAYMENT_BCA";
                    break;
                }
                case MSG_CODE::MSG_CODE_PAYMENT_EZL:
                {
                    msgCodeStr = "MSG_CODE_PAYMENT_EZL";
                    break;
                }
                case MSG_CODE::MSG_CODE_PRE_AUTH:
                {
                    msgCodeStr = "MSG_CODE_PRE_AUTH";
                    break;
                }
                case MSG_CODE::MSG_CODE_PRE_AUTH_COMPLETION:
                {
                    msgCodeStr = "MSG_CODE_PRE_AUTH_COMPLETION";
                    break;
                }
                case MSG_CODE::MSG_CODE_PAYMENT_HOST:
                {
                    msgCodeStr = "MSG_CODE_PAYMENT_HOST";
                    break;
                }
            }
            break;
        }
        case MSG_TYPE::MSG_TYPE_CANCELLATION:
        {
            switch (static_cast<MSG_CODE>(msgCode))
            {
                case MSG_CODE::MSG_CODE_CANCELLATION_VOID:
                {
                    msgCodeStr = "MSG_CODE_CANCELLATION_VOID";
                    break;
                }
                case MSG_CODE::MSG_CODE_CANCELLATION_REFUND:
                {
                    msgCodeStr = "MSG_CODE_CANCELLATION_REFUND";
                    break;
                }
                case MSG_CODE::MSG_CODE_CANCELLATION_CANCEL:
                {
                    msgCodeStr = "MSG_CODE_CANCELLATION_CANCEL";
                    break;
                }
            }
            break;
        }
        case MSG_TYPE::MSG_TYPE_TOPUP:
        {
            switch (static_cast<MSG_CODE>(msgCode))
            {
                case MSG_CODE::MSG_CODE_TOPUP_NCC:
                {
                    msgCodeStr = "MSG_CODE_TOPUP_NCC";
                    break;
                }
                case MSG_CODE::MSG_CODE_TOPUP_NFP:
                {
                    msgCodeStr = "MSG_CODE_TOPUP_NFP";
                    break;
                }
                case MSG_CODE::MSG_CODE_TOPUP_RSVP:
                {
                    msgCodeStr = "MSG_CODE_TOPUP_RSVP";
                    break;
                }
            }
            break;
        }
        case MSG_TYPE::MSG_TYPE_RECORD:
        {
            switch (static_cast<MSG_CODE>(msgCode))
            {
                case MSG_CODE::MSG_CODE_RECORD_SUMMARY:
                {
                    msgCodeStr = "MSG_CODE_RECORD_SUMMARY";
                    break;
                }
                case MSG_CODE::MSG_CODE_RECORD_UPLOAD:
                {
                    msgCodeStr = "MSG_CODE_RECORD_UPLOAD";
                    break;
                }
                case MSG_CODE::MSG_CODE_RECORD_CLEAR:
                {
                    msgCodeStr = "MSG_CODE_RECORD_CLEAR";
                    break;
                }
            }
            break;
        }
        case MSG_TYPE::MSG_TYPE_PASSTHROUGH:
        {
            switch (static_cast<MSG_CODE>(msgCode))
            {
                case MSG_CODE::MSG_CODE_PASSTHROUGH_UOB:
                {
                    msgCodeStr = "MSG_CODE_PASSTHROUGH_UOB";
                    break;
                }
            }
            break;
        }
        case MSG_TYPE::MSG_TYPE_OTHER:
        {
            switch (static_cast<MSG_CODE>(msgCode))
            {
                case MSG_CODE::MSG_CODE_OTHER_CASH_DEPOSIT:
                {
                    msgCodeStr = "MSG_CODE_OTHER_CASH_DEPOSIT";
                    break;
                }
            }
            break;
        }
    }

    return msgCodeStr;
}

std::string Upt::getEncryptionAlgorithmString(uint8_t encryptionAlgorithm)
{
    std::string encryptionAlgorithmStr = "";

    switch (static_cast<ENCRYPTION_ALGORITHM>(encryptionAlgorithm))
    {
        case ENCRYPTION_ALGORITHM::ENCRYPTION_ALGORITHM_NONE:
        {
            encryptionAlgorithmStr = "ENCRYPTION_ALGORITHM_NONE";
            break;
        }
        case ENCRYPTION_ALGORITHM::ENCRYPTION_ALGORITHM_AES128:
        {
            encryptionAlgorithmStr = "ENCRYPTION_ALGORITHM_AES128";
            break;
        }
        case ENCRYPTION_ALGORITHM::ENCRYPTION_ALGORITHM_AES192:
        {
            encryptionAlgorithmStr = "ENCRYPTION_ALGORITHM_AES192";
            break;
        }
        case ENCRYPTION_ALGORITHM::ENCRYPTION_ALGORITHM_AES256:
        {
            encryptionAlgorithmStr = "ENCRYPTION_ALGORITHM_AES256";
            break;
        }
        case ENCRYPTION_ALGORITHM::ENCRYPTION_ALGORITHM_DES1_1KEY:
        {
            encryptionAlgorithmStr = "ENCRYPTION_ALGORITHM_DES1_1KEY";
            break;
        }
        case ENCRYPTION_ALGORITHM::ENCRYPTION_ALGORITHM_DES3_2KEY:
        {
            encryptionAlgorithmStr = "ENCRYPTION_ALGORITHM_DES3_2KEY";
            break;
        }
        case ENCRYPTION_ALGORITHM::ENCRYPTION_ALGORITHM_DES3_3KEY:
        {
            encryptionAlgorithmStr = "ENCRYPTION_ALGORITHM_DES3_3KEY";
            break;
        }
    }

    return encryptionAlgorithmStr;
}

std::string Upt::getCommandTitleString(uint8_t msgDirection, uint16_t msgClass, uint32_t msgCode, uint32_t msgType)
{
    std::string titleStr = "";
    std::string msgDirectionStr = "";
    std::string msgClassStr = "";
    std::string msgCodeStr = "";

    // Msg Direction
    switch (static_cast<MSG_DIRECTION>(msgDirection))
    {
        case MSG_DIRECTION::MSG_DIRECTION_CONTROLLER_TO_DEVICE:
        {
            msgDirectionStr = "CONTROLLER";
            break;
        }
        case MSG_DIRECTION::MSG_DIRECTION_DEVICE_TO_CONTROLLER:
        {
            msgDirectionStr = "DEVICE";
            break;
        }
    }

    // Msg Class
    switch (static_cast<MSG_CLASS>(msgClass))
    {
        case MSG_CLASS::MSG_CLASS_NONE:
        {
            msgClassStr = "NONE";
            break;
        }
        case MSG_CLASS::MSG_CLASS_REQ:
        {
            msgClassStr = "REQ";
            break;
        }
        case MSG_CLASS::MSG_CLASS_ACK:
        {
            msgClassStr = "ACK";
            break;
        }
        case MSG_CLASS::MSG_CLASS_RSP:
        {
            msgClassStr = "RSP";
            break;
        }
    }

    // Msg Code
    std::string tmpCodeStr = getMsgCodeString(msgCode, msgType);

    if (!tmpCodeStr.empty())
    {
        std::size_t pos = tmpCodeStr.find("MSG_CODE_");
        if (pos != std::string::npos)
        {
            msgCodeStr = tmpCodeStr.substr(pos + std::string("MSG_CODE_").length());
        }
        else
        {
            msgCodeStr = tmpCodeStr;
        }
    }

    titleStr += msgDirectionStr + " (" + msgClassStr + ") - " + msgCodeStr;
    return titleStr;
}

char Upt::getFieldEncodingChar(uint8_t fieldEncoding)
{
    char fieldChar;

    switch (static_cast<FIELD_ENCODING>(fieldEncoding))
    {
        case FIELD_ENCODING::FIELD_ENCODING_NONE:
        {
            fieldChar = 'N';
            break;
        }
        case FIELD_ENCODING::FIELD_ENCODING_ARRAY_ASCII:
        case FIELD_ENCODING::FIELD_ENCODING_ARRAY_ASCII_HEX:
        case FIELD_ENCODING::FIELD_ENCODING_ARRAY_HEX:
        {
            fieldChar = 'A';
            break;
        }
        case FIELD_ENCODING::FIELD_ENCODING_VALUE_ASCII:
        case FIELD_ENCODING::FIELD_ENCODING_VALUE_ASCII_HEX:
        case FIELD_ENCODING::FIELD_ENCODING_VALUE_BCD:
        case FIELD_ENCODING::FIELD_ENCODING_VALUE_HEX_BIG:
        case FIELD_ENCODING::FIELD_ENCODING_VALUE_HEX_LITTLE:
        {
            fieldChar = 'V';
            break;
        }
    }

    return fieldChar;
}

std::string Upt::getFieldEncodingTypeString(uint8_t fieldEncoding)
{
    std::string fieldStr = "";

    switch (static_cast<FIELD_ENCODING>(fieldEncoding))
    {
        case FIELD_ENCODING::FIELD_ENCODING_NONE:
        {
            fieldStr = "NONE";
            break;
        }
        case FIELD_ENCODING::FIELD_ENCODING_ARRAY_ASCII:
        {
            fieldStr = "ARR_ASC";
            break;
        }
        case FIELD_ENCODING::FIELD_ENCODING_ARRAY_ASCII_HEX:
        {
            fieldStr = "ARR_ASC_HEX";
            break;
        }
        case FIELD_ENCODING::FIELD_ENCODING_ARRAY_HEX:
        {
            fieldStr = "ARR_HEX";
            break;
        }
        case FIELD_ENCODING::FIELD_ENCODING_VALUE_ASCII:
        {
            fieldStr = "VAL_ASC";
            break;
        }
        case FIELD_ENCODING::FIELD_ENCODING_VALUE_ASCII_HEX:
        {
            fieldStr = "VAL_ASC_HEX";
            break;
        }
        case FIELD_ENCODING::FIELD_ENCODING_VALUE_BCD:
        {
            fieldStr = "VAL_BCD";
            break;
        }
        case FIELD_ENCODING::FIELD_ENCODING_VALUE_HEX_BIG:
        {
            fieldStr = "VAL_HEX_BIG";
            break;
        }
        case FIELD_ENCODING::FIELD_ENCODING_VALUE_HEX_LITTLE:
        {
            fieldStr = "VAL_HEX_LIT";
            break;
        }
    }

    return fieldStr;
}

std::string Upt::getFieldIDString(uint16_t fieldID)
{
    std::string fieldIDStr = "";

    switch (static_cast<FIELD_ID>(fieldID))
    {
        case FIELD_ID::ID_PADDING:
        {
            fieldIDStr = "ID_PADDING";
            break;
        }
        case FIELD_ID::ID_DEVICE_DATE:
        {
            fieldIDStr = "ID_DEVICE_DATE";
            break;
        }
        case FIELD_ID::ID_DEVICE_TIME:
        {
            fieldIDStr = "ID_DEVICE_TIME";
            break;
        }
        case FIELD_ID::ID_DEVICE_MID:
        {
            fieldIDStr = "ID_DEVICE_MID";
            break;
        }
        case FIELD_ID::ID_DEVICE_RID:
        {
            fieldIDStr = "ID_DEVICE_RID";
            break;
        }
        case FIELD_ID::ID_DEVICE_TID:
        {
            fieldIDStr = "ID_DEVICE_TID";
            break;
        }
        case FIELD_ID::ID_DEVICE_MER_CODE:
        {
            fieldIDStr = "ID_DEVICE_MER_CODE";
            break;
        }
        case FIELD_ID::ID_DEVICE_MER_NAME:
        {
            fieldIDStr = "ID_DEVICE_MER_NAME";
            break;
        }
        case FIELD_ID::ID_DEVICE_MER_ADDRESS:
        {
            fieldIDStr = "ID_DEVICE_MER_ADDRESS";
            break;
        }
        case FIELD_ID::ID_DEVICE_MER_ADDRESS2:
        {
            fieldIDStr = "ID_DEVICE_MER_ADDRESS2";
            break;
        }
        case FIELD_ID::ID_SOF_TYPE:
        {
            fieldIDStr = "ID_SOF_TYPE";
            break;
        }
        case FIELD_ID::ID_SOF_DESCRIPTION:
        {
            fieldIDStr = "ID_SOF_DESCRIPTION";
            break;
        }
        case FIELD_ID::ID_SOF_PRIORITY:
        {
            fieldIDStr = "ID_SOF_PRIORITY";
            break;
        }
        case FIELD_ID::ID_SOF_ACQUIRER:
        {
            fieldIDStr = "ID_SOF_ACQUIRER";
            break;
        }
        case FIELD_ID::ID_SOF_NAME:
        {
            fieldIDStr = "ID_SOF_NAME";
            break;
        }
        case FIELD_ID::ID_SOF_SALE_COUNT:
        {
            fieldIDStr = "ID_SOF_SALE_COUNT";
            break;
        }
        case FIELD_ID::ID_SOF_SALE_TOTAL:
        {
            fieldIDStr = "ID_SOF_SALE_TOTAL";
            break;
        }
        case FIELD_ID::ID_SOF_REFUND_COUNT:
        {
            fieldIDStr = "ID_SOF_REFUND_COUNT";
            break;
        }
        case FIELD_ID::ID_SOF_REFUND_TOTAL:
        {
            fieldIDStr = "ID_SOF_REFUND_TOTAL";
            break;
        }
        case FIELD_ID::ID_SOF_VOID_COUNT:
        {
            fieldIDStr = "ID_SOF_VOID_COUNT";
            break;
        }
        case FIELD_ID::ID_SOF_VOID_TOTAL:
        {
            fieldIDStr = "ID_SOF_VOID_TOTAL";
            break;
        }
        case FIELD_ID::ID_SOF_PRE_AUTH_COMPLETE_COUNT:
        {
            fieldIDStr = "ID_SOF_PRE_AUTH_COMPLETE_COUNT";
            break;
        }
        case FIELD_ID::ID_SOF_PRE_AUTH_COMPLETE_TOTAL:
        {
            fieldIDStr = "ID_SOF_PRE_AUTH_COMPLETE_TOTAL";
            break;
        }
        case FIELD_ID::ID_SOF_PRE_AUTH_COMPLETION_VOID_COUNT:
        {
            fieldIDStr = "ID_SOF_PRE_AUTH_COMPLETION_VOID_COUNT";
            break;
        }
        case FIELD_ID::ID_SOF_PRE_AUTH_COMPLETION_VOID_TOTAL:
        {
            fieldIDStr = "ID_SOF_PRE_AUTH_COMPLETION_VOID_TOTAL";
            break;
        }
        case FIELD_ID::ID_SOF_CASHBACK_COUNT:
        {
            fieldIDStr = "ID_SOF_CASHBACK_COUNT";
            break;
        }
        case FIELD_ID::ID_SOF_CASHBACK_TOTAL:
        {
            fieldIDStr = "ID_SOF_CASHBACK_TOTAL";
            break;
        }
        case FIELD_ID::ID_SOF_TOPUP_COUNT:
        {
            fieldIDStr = "ID_SOF_TOPUP_COUNT";
            break;
        }
        case FIELD_ID::ID_SOF_TOPUP_TOTAL:
        {
            fieldIDStr = "ID_SOF_TOPUP_TOTAL";
            break;
        }
        case FIELD_ID::ID_AUTH_DATA_1:
        {
            fieldIDStr = "ID_AUTH_DATA_1";
            break;
        }
        case FIELD_ID::ID_AUTH_DATA_2:
        {
            fieldIDStr = "ID_AUTH_DATA_2";
            break;
        }
        case FIELD_ID::ID_AUTH_DATA_3:
        {
            fieldIDStr = "ID_AUTH_DATA_3";
            break;
        }
        case FIELD_ID::ID_CARD_TYPE:
        {
            fieldIDStr = "ID_CARD_TYPE";
            break;
        }
        case FIELD_ID::ID_CARD_CAN:
        {
            fieldIDStr = "ID_CARD_CAN";
            break;
        }
        case FIELD_ID::ID_CARD_CSN:
        {
            fieldIDStr = "ID_CARD_CSN";
            break;
        }
        case FIELD_ID::ID_CARD_BALANCE:
        {
            fieldIDStr = "ID_CARD_BALANCE";
            break;
        }
        case FIELD_ID::ID_CARD_COUNTER:
        {
            fieldIDStr = "ID_CARD_COUNTER";
            break;
        }
        case FIELD_ID::ID_CARD_EXPIRY:
        {
            fieldIDStr = "ID_CARD_EXPIRY";
            break;
        }
        case FIELD_ID::ID_CARD_CERT:
        {
            fieldIDStr = "ID_CARD_CERT";
            break;
        }
        case FIELD_ID::ID_CARD_PREVIOUS_BAL:
        {
            fieldIDStr = "ID_CARD_PREVIOUS_BAL";
            break;
        }
        case FIELD_ID::ID_CARD_PURSE_STATUS:
        {
            fieldIDStr = "ID_CARD_PURSE_STATUS";
            break;
        }
        case FIELD_ID::ID_CARD_ATU_STATUS:
        {
            fieldIDStr = "ID_CARD_ATU_STATUS";
            break;
        }
        case FIELD_ID::ID_CARD_NUM_MASKED:
        {
            fieldIDStr = "ID_CARD_NUM_MASKED";
            break;
        }
        case FIELD_ID::ID_CARD_HOLDER_NAME:
        {
            fieldIDStr = "ID_CARD_HOLDER_NAME";
            break;
        }
        case FIELD_ID::ID_CARD_SCHEME_NAME:
        {
            fieldIDStr = "ID_CARD_SCHEME_NAME";
            break;
        }
        case FIELD_ID::ID_CARD_AID:
        {
            fieldIDStr = "ID_CARD_AID";
            break;
        }
        case FIELD_ID::ID_CARD_CEPAS_VER:
        {
            fieldIDStr = "ID_CARD_CEPAS_VER";
            break;
        }
        case FIELD_ID::ID_TXN_TYPE:
        {
            fieldIDStr = "ID_TXN_TYPE";
            break;
        }
        case FIELD_ID::ID_TXN_AMOUNT:
        {
            fieldIDStr = "ID_TXN_AMOUNT";
            break;
        }
        case FIELD_ID::ID_TXN_CASHBACK_AMOUNT:
        {
            fieldIDStr = "ID_TXN_CASHBACK_AMOUNT";
            break;
        }
        case FIELD_ID::ID_TXN_DATE:
        {
            fieldIDStr = "ID_TXN_DATE";
            break;
        }
        case FIELD_ID::ID_TXN_TIME:
        {
            fieldIDStr = "ID_TXN_TIME";
            break;
        }
        case FIELD_ID::ID_TXN_BATCH:
        {
            fieldIDStr = "ID_TXN_BATCH";
            break;
        }
        case FIELD_ID::ID_TXN_CERT:
        {
            fieldIDStr = "ID_TXN_CERT";
            break;
        }
        case FIELD_ID::ID_TXN_CHECKSUM:
        {
            fieldIDStr = "ID_TXN_CHECKSUM";
            break;
        }
        case FIELD_ID::ID_TXN_DATA:
        {
            fieldIDStr = "ID_TXN_DATA";
            break;
        }
        case FIELD_ID::ID_TXN_STAN:
        {
            fieldIDStr = "ID_TXN_STAN";
            break;
        }
        case FIELD_ID::ID_TXN_MER:
        {
            fieldIDStr = "ID_TXN_MER";
            break;
        }
        case FIELD_ID::ID_TXN_MER_REF_NUM:
        {
            fieldIDStr = "ID_TXN_MER_REF_NUM";
            break;
        }
        case FIELD_ID::ID_TXN_RESPONSE_TEXT:
        {
            fieldIDStr = "ID_TXN_RESPONSE_TEXT";
            break;
        }
        case FIELD_ID::ID_TXN_MER_NAME:
        {
            fieldIDStr = "ID_TXN_MER_NAME";
            break;
        }
        case FIELD_ID::ID_TXN_MER_ADDRESS:
        {
            fieldIDStr = "ID_TXN_MER_ADDRESS";
            break;
        }
        case FIELD_ID::ID_TXN_TID:
        {
            fieldIDStr = "ID_TXN_TID";
            break;
        }
        case FIELD_ID::ID_TXN_MID:
        {
            fieldIDStr = "ID_TXN_MID";
            break;
        }
        case FIELD_ID::ID_TXN_APPROV_CODE:
        {
            fieldIDStr = "ID_TXN_APPROV_CODE";
            break;
        }
        case FIELD_ID::ID_TXN_RRN:
        {
            fieldIDStr = "ID_TXN_RRN";
            break;
        }
        case FIELD_ID::ID_TXN_CARD_NAME:
        {
            fieldIDStr = "ID_TXN_CARD_NAME";
            break;
        }
        case FIELD_ID::ID_TXN_SERVICE_FEE:
        {
            fieldIDStr = "ID_TXN_SERVICE_FEE";
            break;
        }
        case FIELD_ID::ID_TXN_MARKETING_MSG:
        {
            fieldIDStr = "ID_TXN_MARKETING_MSG";
            break;
        }
        case FIELD_ID::ID_TXN_HOST_RESP_MSG_1:
        {
            fieldIDStr = "ID_TXN_HOST_RESP_MSG_1";
            break;
        }
        case FIELD_ID::ID_TXN_HOST_RESP_MSG_2:
        {
            fieldIDStr = "ID_TXN_HOST_RESP_MSG_2";
            break;
        }
        case FIELD_ID::ID_TXN_HOST_RESP_CODE:
        {
            fieldIDStr = "ID_TXN_HOST_RESP_CODE";
            break;
        }
        case FIELD_ID::ID_TXN_CARD_ENTRY_MODE:
        {
            fieldIDStr = "ID_TXN_CARD_ENTRY_MODE";
            break;
        }
        case FIELD_ID::ID_TXN_RECEIPT:
        {
            fieldIDStr = "ID_TXN_RECEIPT";
            break;
        }
        case FIELD_ID::ID_TXN_AUTOLOAD_AMOUT:
        {
            fieldIDStr = "ID_TXN_AUTOLOAD_AMOUT";
            break;
        }
        case FIELD_ID::ID_TXN_INV_NUM:
        {
            fieldIDStr = "ID_TXN_INV_NUM";
            break;
        }
        case FIELD_ID::ID_TXN_TC:
        {
            fieldIDStr = "ID_TXN_TC";
            break;
        }
        case FIELD_ID::ID_TXN_FOREIGN_AMOUNT:
        {
            fieldIDStr = "ID_TXN_FOREIGN_AMOUNT";
            break;
        }
        case FIELD_ID::ID_TXN_FOREIGN_MID:
        {
            fieldIDStr = "ID_TXN_FOREIGN_MID";
            break;
        }
        case FIELD_ID::ID_FOREIGN_CUR_NAME:
        {
            fieldIDStr = "ID_FOREIGN_CUR_NAME";
            break;
        }
        case FIELD_ID::ID_TXN_POSID:
        {
            fieldIDStr = "ID_TXN_POSID";
            break;
        }
        case FIELD_ID::ID_TXN_ACQUIRER:
        {
            fieldIDStr = "ID_TXN_ACQUIRER";
            break;
        }
        case FIELD_ID::ID_TXN_HOST:
        {
            fieldIDStr = "ID_TXN_HOST";
            break;
        }
        case FIELD_ID::ID_TXN_LAST_HEADER:
        {
            fieldIDStr = "ID_TXN_LAST_HEADER";
            break;
        }
        case FIELD_ID::ID_TXN_LAST_PAYLOAD:
        {
            fieldIDStr = "ID_TXN_LAST_PAYLOAD";
            break;
        }
        case FIELD_ID::ID_TXN_RECEIPT_REQUIRED:
        {
            fieldIDStr = "ID_TXN_RECEIPT_REQUIRED";
            break;
        }
        case FIELD_ID::ID_TXN_FEE_DUE_TO_MERCHANT:
        {
            fieldIDStr = "ID_TXN_FEE_DUE_TO_MERCHANT";
            break;
        }
        case FIELD_ID::ID_TXN_FEE_DUE_FROM_MERCHANT:
        {
            fieldIDStr = "ID_TXN_FEE_DUE_FROM_MERCHANT";
            break;
        }
        case FIELD_ID::ID_VEHICLE_LICENSE_PLATE:
        {
            fieldIDStr = "ID_VEHICLE_LICENSE_PLATE";
            break;
        }
        case FIELD_ID::ID_VEHICLE_DEVICE_TYPE:
        {
            fieldIDStr = "ID_VEHICLE_DEVICE_TYPE";
            break;
        }
        case FIELD_ID::ID_VEHICLE_DEVICE_ID:
        {
            fieldIDStr = "ID_VEHICLE_DEVICE_ID";
            break;
        }
    }

    return fieldIDStr;
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

void Upt::enqueueCommand(Upt::UPT_CMD cmd, std::shared_ptr<void> data)
{
    if (!pSerialPort_ || !(pSerialPort_->is_open()))
    {
        Logger::getInstance()->FnLog("Serial Port not open, unable to enqueue command.", logFileName_, "UPT");
        return;
    }

    std::stringstream ss;
    ss << "Sending Upt Command to queue: " << getCommandString(cmd);
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "UPT");

    {
        std::unique_lock<std::mutex> lock(cmdQueueMutex_);
        commandQueue_.emplace_back(cmd, data);

        if ((getSequenceNo() + 1) == 0xFFFFFFFF)
        {
            commandQueue_.emplace_front(UPT_CMD::DEVICE_RESET_SEQUENCE_NUMBER_REQUEST, nullptr);
        }
    }

    boost::asio::post(strand_, [this]() {
        checkCommandQueue();
    });
}

void Upt::enqueueCommandToFront(Upt::UPT_CMD cmd, std::shared_ptr<void> data)
{
    if (!pSerialPort_ || !(pSerialPort_->is_open()))
    {
        Logger::getInstance()->FnLog("Serial Port not open, unable to enqueue command.", logFileName_, "UPT");
        return;
    }

    std::stringstream ss;
    ss << "Sending Upt Command to the front of the queue: " << getCommandString(cmd);
    Logger::getInstance()->FnLog(ss.str(), logFileName_, "UPT");

    {
        std::unique_lock<std::mutex> lock(cmdQueueMutex_);
        commandQueue_.emplace_front(cmd, data);

        if ((getSequenceNo() + 1) == 0xFFFFFFFF)
        {
            commandQueue_.emplace_front(UPT_CMD::DEVICE_RESET_SEQUENCE_NUMBER_REQUEST, nullptr);
        }
    }

    boost::asio::post(strand_, [this]() {
        checkCommandQueue();
    });
}

void Upt::FnUptSendDeviceStatusRequest()
{
    enqueueCommand(UPT_CMD::DEVICE_STATUS_REQUEST);
}

void Upt::FnUptSendDeviceResetRequest()
{
    enqueueCommand(UPT_CMD::DEVICE_RESET_REQUEST);
}

void Upt::FnUptSendDeviceTimeSyncRequest()
{
    enqueueCommand(UPT_CMD::DEVICE_TIME_SYNC_REQUEST);
}

void Upt::FnUptSendDeviceLogonRequest()
{
    enqueueCommand(UPT_CMD::DEVICE_LOGON_REQUEST);
}

void Upt::FnUptSendDeviceTMSRequest()
{
    enqueueCommand(UPT_CMD::DEVICE_TMS_REQUEST);
}

void Upt::FnUptSendDeviceSettlementNETSRequest()
{
    std::shared_ptr<void> req_data = std::make_shared<CommandSettlementRequestData>(CommandSettlementRequestData{HOST_ID::NETS});
    enqueueCommand(UPT_CMD::DEVICE_SETTLEMENT_REQUEST, req_data);
}

void Upt::FnUptSendDeviceResetSequenceNumberRequest()
{
    enqueueCommand(UPT_CMD::DEVICE_RESET_SEQUENCE_NUMBER_REQUEST);
}

void Upt::FnUptSendDeviceRetrieveNETSLastTransactionStatusRequest()
{
    std::shared_ptr<void> req_data = std::make_shared<CommandRetrieveLastTransactionStatusRequestData>(CommandRetrieveLastTransactionStatusRequestData{HOST_ID::NETS});
    enqueueCommand(UPT_CMD::DEVICE_RETRIEVE_LAST_TRANSACTION_STATUS_REQUEST, req_data);
}

void Upt::FnUptSendDeviceRetrieveLastSettlementRequest()
{
    enqueueCommand(UPT_CMD::DEVICE_RETRIEVE_LAST_SETTLEMENT_REQUEST);
}

void Upt::FnUptSendCardDetectRequest()
{
    enqueueCommand(UPT_CMD::CARD_DETECT_REQUEST);
}

void Upt::FnUptSendDeviceAutoPaymentRequest(uint32_t amount, const std::string& mer_ref_num)
{
    std::shared_ptr<void> req_data = std::make_shared<CommandPaymentRequestData>(CommandPaymentRequestData{amount, mer_ref_num});
    enqueueCommand(UPT_CMD::PAYMENT_MODE_AUTO_REQUEST, req_data);
}

void Upt::FnUptSendDeviceNCCTopUpCommandRequest(uint32_t amount, const std::string& mer_ref_num)
{
    std::shared_ptr<void> req_data = std::make_shared<CommandTopUpRequestData>(CommandTopUpRequestData{amount, mer_ref_num});
    enqueueCommand(UPT_CMD::TOP_UP_NETS_NCC_BY_NETS_EFT_REQUEST, req_data);
}

void Upt::FnUptSendDeviceNFPTopUpCommandRequest(uint32_t amount, const std::string& mer_ref_num)
{
    std::shared_ptr<void> req_data = std::make_shared<CommandTopUpRequestData>(CommandTopUpRequestData{amount, mer_ref_num});
    enqueueCommand(UPT_CMD::TOP_UP_NETS_NFP_BY_NETS_EFT_REQUEST, req_data);
}

void Upt::FnUptSendDeviceCancelCommandRequest()
{
    processEvent(EVENT::CANCEL_COMMAND);
}

const Upt::StateTransition Upt::stateTransitionTable[static_cast<int>(STATE::STATE_COUNT)] = 
{
    {STATE::IDLE,
    {
        {EVENT::COMMAND_ENQUEUED                                , &Upt::handleIdleState                         , STATE::SENDING_REQUEST_ASYNC              },
        {EVENT::CANCEL_COMMAND                                  , &Upt::handleIdleState                         , STATE::IDLE                               }
    }},
    {STATE::SENDING_REQUEST_ASYNC,
    {
        {EVENT::WRITE_COMPLETED                                 , &Upt::handleSendingRequestAsyncState          , STATE::WAITING_FOR_ACK                    },
        {EVENT::WRITE_FAILED                                    , &Upt::handleSendingRequestAsyncState          , STATE::IDLE                               },
        {EVENT::CANCEL_COMMAND                                  , &Upt::handleSendingRequestAsyncState          , STATE::CANCEL_COMMAND_REQUEST             },
        {EVENT::WRITE_TIMEOUT                                   , &Upt::handleSendingRequestAsyncState          , STATE::IDLE                               }
    }},
    {STATE::WAITING_FOR_ACK,
    {
        {EVENT::ACK_TIMER_CANCELLED_ACK_RECEIVED                , &Upt::handleWaitingForAckState                , STATE::WAITING_FOR_RESPONSE               },
        {EVENT::ACK_TIMEOUT                                     , &Upt::handleWaitingForAckState                , STATE::IDLE                               },
        {EVENT::CANCEL_COMMAND                                  , &Upt::handleWaitingForAckState                , STATE::CANCEL_COMMAND_REQUEST             }
    }},
    {STATE::WAITING_FOR_RESPONSE,
    {
        {EVENT::RESPONSE_TIMER_CANCELLED_RSP_RECEIVED           , &Upt::handleWaitingForResponseState           , STATE::IDLE                               },
        {EVENT::RESPONSE_TIMEOUT                                , &Upt::handleWaitingForResponseState           , STATE::IDLE                               },
        {EVENT::CANCEL_COMMAND                                  , &Upt::handleWaitingForResponseState           , STATE::CANCEL_COMMAND_REQUEST             }
    }},
    {STATE::CANCEL_COMMAND_REQUEST,
    {
        {EVENT::CANCEL_COMMAND_CLEAN_UP_AND_ENQUEUE             , &Upt::handleCancelCommandRequestState         , STATE::IDLE                               }
    }}
};

std::string Upt::eventToString(EVENT event)
{
    std::string eventStr = "Unknown Event";

    switch (event)
    {
        case EVENT::COMMAND_ENQUEUED:
        {
            eventStr = "COMMAND_ENQUEUED";
            break;
        }
        case EVENT::WRITE_COMPLETED:
        {
            eventStr = "WRITE_COMPLETED";
            break;
        }
        case EVENT::WRITE_FAILED:
        {
            eventStr = "WRITE_FAILED";
            break;
        }
        case EVENT::ACK_TIMEOUT:
        {
            eventStr = "ACK_TIMEOUT";
            break;
        }
        case EVENT::ACK_TIMER_CANCELLED_ACK_RECEIVED:
        {
            eventStr = "ACK_TIMER_CANCELLED_ACK_RECEIVED";
            break;
        }
        case EVENT::RESPONSE_TIMEOUT:
        {
            eventStr = "RESPONSE_TIMEOUT";
            break;
        }
        case EVENT::RESPONSE_TIMER_CANCELLED_RSP_RECEIVED:
        {
            eventStr = "RESPONSE_TIMER_CANCELLED_RSP_RECEIVED";
            break;
        }
        case EVENT::START_PENDING_RESPONSE_TIMER:
        {
            eventStr = "START_PENDING_RESPONSE_TIMER";
            break;
        }
        case EVENT::PENDING_RESPONSE_TIMER_CANCELLED_RSP_RECEIVED:
        {
            eventStr = "PENDING_RESPONSE_TIMER_CANCELLED_RSP_RECEIVED";
            break;
        }
        case EVENT::PENDING_RESPONSE_TIMER_TIMEOUT:
        {
            eventStr = "PENDING_RESPONSE_TIMER_TIMEOUT";
            break;
        }
        case EVENT::CANCEL_COMMAND:
        {
            eventStr = "CANCEL_COMMAND";
            break;
        }
        case EVENT::WRITE_TIMEOUT:
        {
            eventStr = "WRITE_TIMEOUT";
            break;
        }
        case EVENT::CANCEL_COMMAND_CLEAN_UP_AND_ENQUEUE:
        {
            eventStr = "CANCEL_COMMAND_CLEAN_UP_AND_ENQUEUE";
            break;
        }
    }

    return eventStr;
}

std::string Upt::stateToString(STATE state)
{
    std::string stateStr = "Unknow State";

    switch (state)
    {
        case STATE::IDLE:
        {
            stateStr = "IDLE";
            break;
        }
        case STATE::SENDING_REQUEST_ASYNC:
        {
            stateStr = "SENDING_REQUEST_ASYNC";
            break;
        }
        case STATE::WAITING_FOR_ACK:
        {
            stateStr = "WAITING_FOR_ACK";
            break;
        }
        case STATE::WAITING_FOR_RESPONSE:
        {
            stateStr = "WAITING_FOR_RESPONSE";
            break;
        }
        case STATE::CANCEL_COMMAND_REQUEST:
        {
            stateStr = "CANCEL_COMMAND_REQUEST";
            break;
        }
    }

    return stateStr;
}

void Upt::processEvent(EVENT event)
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
                Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

                if (transition.eventHandler != nullptr)
                {
                    (this->*transition.eventHandler)(event);
                }
                currentState_ = transition.nextState;

                if (currentState_ == STATE::IDLE)
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
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");
        }
    });
}

void Upt::checkCommandQueue()
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
        processEvent(EVENT::COMMAND_ENQUEUED);
    }
}

void Upt::popFromCommandQueueAndEnqueueWrite()
{
    std::unique_lock<std::mutex> lock(cmdQueueMutex_);
    
    std::ostringstream oss;
    oss << "Command queue size: " << commandQueue_.size() << std::endl;
    if (!commandQueue_.empty())
    {
        oss << "Commands in queue: " << std::endl;
        for (const auto& cmdData : commandQueue_)
        {
            oss << "[Cmd: " << getCommandString(cmdData.cmd) << "]" << std::endl;
        }
    }
    else
    {
        oss << "Command queue is empty." << std::endl;
    }
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

    if (!commandQueue_.empty())
    {
        ackRecv_.store(false);
        rspRecv_.store(false);
        pendingRspRecv_.store(false);
        CommandWithData cmdData = commandQueue_.front();
        commandQueue_.pop_front();
        setCurrentCmd(cmdData.cmd);
        enqueueWrite(prepareCmd(cmdData.cmd, cmdData.data));
    }
}

void Upt::handleIdleState(EVENT event)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "UPT");

    if (event == EVENT::COMMAND_ENQUEUED)
    {
        Logger::getInstance()->FnLog("Pop from command queue and write.", logFileName_, "UPT");
        popFromCommandQueueAndEnqueueWrite();
        startSerialWriteTimer();
    }
    else if (event == EVENT::CANCEL_COMMAND)
    {
        Logger::getInstance()->FnLog("Received cancel command event in Idle State.", logFileName_, "UPT");
        enqueueCommandToFront(UPT_CMD::CANCEL_COMMAND_REQUEST);
    }
}

void Upt::handleSendingRequestAsyncState(EVENT event)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "UPT");

    if (event == EVENT::WRITE_COMPLETED)
    {
        Logger::getInstance()->FnLog("Write completed. Start receiving ack.", logFileName_, "UPT");
        serialWriteTimer_.cancel();
        startAckTimer();
    }
    else if (event == EVENT::WRITE_FAILED)
    {
        Logger::getInstance()->FnLog("Write failed.", logFileName_, "UPT");
        serialWriteTimer_.cancel();
        handleCmdErrorOrTimeout(getCurrentCmd(), MSG_STATUS::SEND_FAILED);
    }
    else if (event == EVENT::CANCEL_COMMAND)
    {
        Logger::getInstance()->FnLog("Received cancel command event in Sending Request Async State", logFileName_, "UPT");
        processEvent(EVENT::CANCEL_COMMAND_CLEAN_UP_AND_ENQUEUE);
    }
    else if (event == EVENT::WRITE_TIMEOUT)
    {
        Logger::getInstance()->FnLog("Received serial write timeout event in Sending Request Async State.", logFileName_, "UPT");
        handleCmdErrorOrTimeout(getCurrentCmd(), MSG_STATUS::SEND_FAILED);
    }
}

void Upt::handleWaitingForAckState(EVENT event)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "UPT");

    if (event == EVENT::ACK_TIMER_CANCELLED_ACK_RECEIVED)
    {
        Logger::getInstance()->FnLog("ACK Received. Start receiving response.", logFileName_, "UPT");
        startResponseTimer();
    }
    else if (event == EVENT::ACK_TIMEOUT)
    {
        Logger::getInstance()->FnLog("ACK Timeout.", logFileName_, "UPT");
        handleCmdErrorOrTimeout(getCurrentCmd(), MSG_STATUS::ACK_TIMEOUT);
    }
    else if (event == EVENT::CANCEL_COMMAND)
    {
        Logger::getInstance()->FnLog("Received cancel command event in Waiting For Ack State.", logFileName_, "UPT");
        processEvent(EVENT::CANCEL_COMMAND_CLEAN_UP_AND_ENQUEUE);
    }
}

void Upt::handleWaitingForResponseState(EVENT event)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "UPT");

    if (event == EVENT::RESPONSE_TIMER_CANCELLED_RSP_RECEIVED)
    {
        Logger::getInstance()->FnLog("Response Received.", logFileName_, "UPT");
    }
    else if (event == EVENT::RESPONSE_TIMEOUT)
    {
        Logger::getInstance()->FnLog("Response Timer Timeout.", logFileName_, "UPT");
        handleCmdErrorOrTimeout(getCurrentCmd(), MSG_STATUS::RSP_TIMEOUT);
    }
    else if (event == EVENT::CANCEL_COMMAND)
    {
        Logger::getInstance()->FnLog("Received cancel command event in Waiting For Response State.", logFileName_, "UPT");
        processEvent(EVENT::CANCEL_COMMAND_CLEAN_UP_AND_ENQUEUE);
    }
}

void Upt::handleCancelCommandRequestState(EVENT event)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "UPT");

    if (event == EVENT::CANCEL_COMMAND_CLEAN_UP_AND_ENQUEUE)
    {
        Logger::getInstance()->FnLog("Received cancel command event.", logFileName_, "UPT");
        ackTimer_.cancel();
        rspTimer_.cancel();
        serialWriteTimer_.cancel();
        stopSerialWriteDelayTimer();
        processEvent(EVENT::CANCEL_COMMAND);
    }
}

void Upt::startSerialWriteTimer()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "UPT");

    serialWriteTimer_.expires_from_now(boost::posix_time::seconds(10));
    serialWriteTimer_.async_wait(boost::asio::bind_executor(strand_,
        std::bind(&Upt::handleSerialWriteTimeout, this, std::placeholders::_1)));
}

void Upt::handleSerialWriteTimeout(const boost::system::error_code& error)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "UPT");

    if (!error)
    {
        Logger::getInstance()->FnLog("Serial Write timeout occurred.", logFileName_, "UPT");
        write_in_progress_ = false;  // Reset flag to allow next write
        processEvent(EVENT::WRITE_TIMEOUT);
    }
    else if (error == boost::asio::error::operation_aborted)
    {
        Logger::getInstance()->FnLog("Serial Write Timer was cancelled (likely because write completed in time).", logFileName_, "UPT");
        return;
    }
    else
    {
        std::ostringstream oss;
        oss << "Serial Write Timer error: " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

        write_in_progress_ = false; // Reset flag to allow retry
        processEvent(EVENT::WRITE_TIMEOUT);
    }
}

void Upt::startAckTimer()
{
    ackTimer_.expires_from_now(boost::posix_time::seconds(8));
    ackTimer_.async_wait(boost::asio::bind_executor(strand_,
        std::bind(&Upt::handleAckTimeout, this, std::placeholders::_1)));
}

void Upt::handleAckTimeout(const boost::system::error_code& error)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "UPT");

    if (!error || (error == boost::asio::error::operation_aborted))
    {
        if (ackRecv_.load())
        {
            ackRecv_.store(false);
            processEvent(EVENT::ACK_TIMER_CANCELLED_ACK_RECEIVED);
        }
        else
        {
            processEvent(EVENT::ACK_TIMEOUT);
        }
    }
    else
    {
        std::ostringstream oss;
        oss << "Ack Timer error: " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");
        processEvent(EVENT::ACK_TIMEOUT);
    }
}

void Upt::startResponseTimer()
{
    rspTimer_.expires_from_now(boost::posix_time::seconds(180));
    rspTimer_.async_wait(boost::asio::bind_executor(strand_,
        std::bind(&Upt::handleCmdResponseTimeout, this, std::placeholders::_1)));
}

void Upt::handleCmdResponseTimeout(const boost::system::error_code& error)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "UPT");

    if (!error || (error == boost::asio::error::operation_aborted))
    {
        if (rspRecv_.load())
        {
            rspRecv_.store(false);
            processEvent(EVENT::RESPONSE_TIMER_CANCELLED_RSP_RECEIVED);
        }
        else
        {
            processEvent(EVENT::RESPONSE_TIMEOUT);
        }
    }
    else
    {
        std::ostringstream oss;
        oss << "Response Timer error: " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");
        processEvent(EVENT::RESPONSE_TIMEOUT);
    }
}

std::vector<uint8_t> Upt::prepareCmd(Upt::UPT_CMD cmd, std::shared_ptr<void> payloadData)
{
    std::vector<uint8_t> data;

    Message msg;

    // Set the common msg header
    msg.setHeaderMsgVersion(0x00);
    msg.setHeaderMsgDirection(static_cast<uint8_t>(MSG_DIRECTION::MSG_DIRECTION_CONTROLLER_TO_DEVICE));
    msg.setHeaderMsgTime(htole64(Common::getInstance()->FnGetSecondsSince1January0000()));
    //msg.setHeaderMsgTime(htole64(0x0000000ED6646B66));
    if (cmd != UPT_CMD::DEVICE_RESET_SEQUENCE_NUMBER_REQUEST)
    {
        incrementSequenceNo();
        msg.setHeaderMsgSequence(htole32(getSequenceNo()));
    }
    //msg.setHeaderMsgSequence(htole32(0x00000002));
    msg.setHeaderMsgClass(htole16(static_cast<uint16_t>(MSG_CLASS::MSG_CLASS_REQ)));
    msg.setHeaderMsgCompletion(0x01);
    msg.setHeaderMsgNotification(0x00);
    msg.setHeaderMsgStatus(htole32(static_cast<uint32_t>(MSG_STATUS::SUCCESS)));
    msg.setHeaderDeviceProvider(htole16(0x0457)); // [Decimal] : 1111
    msg.setHeaderDeviceType(htole16(0x0001));
    msg.setHeaderDeviceNumber(htole32(0x0001B207)); // [Decimal] : 111111
    msg.setHeaderEncryptionAlgorithm(static_cast<uint8_t>(ENCRYPTION_ALGORITHM::ENCRYPTION_ALGORITHM_NONE));
    msg.setHeaderEncryptionKeyIndex(0x00);
    std::vector<uint8_t> mac = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    msg.setHeaderEncryptionMAC(Common::getInstance()->FnConvertToLittleEndian(mac));

    switch (cmd)
    {
        // DEVICE API
        case UPT_CMD::DEVICE_STATUS_REQUEST:
        {
            msg.setHeaderMsgType(htole32(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_DEVICE)));
            msg.setHeaderMsgCode(htole32(static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_STATUS)));
            msg.setHeaderIntegrityCRC32(htole32(msg.FnCalculateIntegrityCRC32()));
            msg.setHeaderLength(htole32(64));
            break;
        }
        case UPT_CMD::DEVICE_RESET_REQUEST:
        {
            msg.setHeaderMsgType(htole32(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_DEVICE)));
            msg.setHeaderMsgCode(htole32(static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_RESET)));
            msg.setHeaderIntegrityCRC32(htole32(msg.FnCalculateIntegrityCRC32()));
            msg.setHeaderLength(htole32(64));
            break;
        }
        case UPT_CMD::DEVICE_TIME_SYNC_REQUEST:
        {
            msg.setHeaderMsgType(htole32(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_DEVICE)));
            msg.setHeaderMsgCode(htole32(static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_TIME_SYNC)));

            // Payload
            msg.addPayload(createPayload(0x00000010, static_cast<uint16_t>(FIELD_ID::ID_DEVICE_DATE), 0x31, 0x31, Common::getInstance()->FnGetDateInArrayBytes()));
            msg.addPayload(createPayload(0x0000000E, static_cast<uint16_t>(FIELD_ID::ID_DEVICE_TIME), 0x31, 0x31, Common::getInstance()->FnGetTimeInArrayBytes()));
            std::vector<uint8_t> paddingBytes(26, 0x00);
            msg.addPayload(createPayload(0x00000022, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x31, 0x33, paddingBytes));
            // End of Payload

            msg.setHeaderIntegrityCRC32(htole32(msg.FnCalculateIntegrityCRC32()));
            msg.setHeaderLength(htole32(128));
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
            msg.setHeaderMsgType(htole32(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_DEVICE)));
            msg.setHeaderMsgCode(htole32(static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_LOGON)));

            // Payload
            std::vector<uint8_t> data = {0x10, 0x00};   // [NETS = 0x1000]
            msg.addPayload(createPayload(0x0000000A, static_cast<uint16_t>(FIELD_ID::ID_TXN_ACQUIRER), 0x39, 0x38, data));
            std::vector<uint8_t> paddingBytes(14, 0x00);
            msg.addPayload(createPayload(0x00000016, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x31, 0x33, paddingBytes));
            // End of Payload

            msg.setHeaderIntegrityCRC32(htole32(msg.FnCalculateIntegrityCRC32()));
            msg.setHeaderLength(htole32(96));
            break;
        }
        case UPT_CMD::DEVICE_TMS_REQUEST:
        {
            msg.setHeaderMsgType(htole32(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_DEVICE)));
            msg.setHeaderMsgCode(htole32(static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_TMS)));

            // Payload
            std::vector<uint8_t> data = {0x10, 0x00};   // [NETS = 0x1000]
            msg.addPayload(createPayload(0x0000000A, static_cast<uint16_t>(FIELD_ID::ID_TXN_ACQUIRER), 0x39, 0x38, data));
            std::vector<uint8_t> paddingBytes(14, 0x00);
            msg.addPayload(createPayload(0x00000016, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x31, 0x33, paddingBytes));
            // End of Payload

            msg.setHeaderIntegrityCRC32(htole32(msg.FnCalculateIntegrityCRC32()));
            msg.setHeaderLength(htole32(96));
            break;
        }
        case UPT_CMD::DEVICE_SETTLEMENT_REQUEST:
        {
            msg.setHeaderMsgType(htole32(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_DEVICE)));
            msg.setHeaderMsgCode(htole32(static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_SETTLEMENT)));

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
            msg.addPayload(createPayload(0x0000000c, static_cast<uint16_t>(FIELD_ID::ID_TXN_HOST), 0x00, 0x31, data));
            
            std::vector<uint8_t> receipt = {0x01};
            msg.addPayload(createPayload(0x00000009, static_cast<uint16_t>(FIELD_ID::ID_TXN_RECEIPT_REQUIRED), 0x00, 0x38, receipt));
            std::vector<uint8_t> paddingBytes(3, 0x00);
            msg.addPayload(createPayload(0x0000000B, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x31, 0x33, paddingBytes));
            // End of Payload

            msg.setHeaderIntegrityCRC32(htole32(msg.FnCalculateIntegrityCRC32()));
            msg.setHeaderLength(htole32(96));
            break;
        }
        case UPT_CMD::DEVICE_PRE_SETTLEMENT_REQUEST:
        {
            // To be Implement
            break;
        }
        case UPT_CMD::DEVICE_RESET_SEQUENCE_NUMBER_REQUEST:
        {
            setSequenceNo(0xFFFFFFFF);
            msg.setHeaderMsgSequence(htole32(getSequenceNo()));
            msg.setHeaderMsgType(htole32(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_DEVICE)));
            msg.setHeaderMsgCode(htole32(static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_RESET_SEQUENCE_NUMBER)));
            msg.setHeaderIntegrityCRC32(htole32(msg.FnCalculateIntegrityCRC32()));
            msg.setHeaderLength(htole32(64));
            break;
        }
        case UPT_CMD::DEVICE_RETRIEVE_LAST_TRANSACTION_STATUS_REQUEST:
        {
            msg.setHeaderMsgType(htole32(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_DEVICE)));
            msg.setHeaderMsgCode(htole32(static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_RETRIEVE_LAST_TRANSACTION_STATUS)));

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
            msg.addPayload(createPayload(0x0000000c, static_cast<uint16_t>(FIELD_ID::ID_TXN_HOST), 0x00, 0x31, data));
            std::vector<uint8_t> paddingBytes(12, 0x00);
            msg.addPayload(createPayload(0x00000014, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x31, 0x33, paddingBytes));
            //End of Payload

            msg.setHeaderIntegrityCRC32(htole32(msg.FnCalculateIntegrityCRC32()));
            msg.setHeaderLength(htole32(96));
            break;
        }
        case UPT_CMD::DEVICE_RETRIEVE_LAST_SETTLEMENT_REQUEST:
        {
            msg.setHeaderMsgType(htole32(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_DEVICE)));
            msg.setHeaderMsgCode(htole32(static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_RETRIEVE_LAST_SETTLEMENT)));
            msg.setHeaderIntegrityCRC32(htole32(msg.FnCalculateIntegrityCRC32()));
            msg.setHeaderLength(htole32(64));
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
            msg.setHeaderMsgType(htole32(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_CARD)));
            msg.setHeaderMsgCode(htole32(static_cast<uint32_t>(MSG_CODE::MSG_CODE_CARD_DETECT)));
            msg.setHeaderIntegrityCRC32(htole32(msg.FnCalculateIntegrityCRC32()));
            msg.setHeaderLength(htole32(64));
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
            msg.setHeaderMsgType(htole32(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_PAYMENT)));
            msg.setHeaderMsgCode(htole32(static_cast<uint32_t>(MSG_CODE::MSG_CODE_PAYMENT_AUTO)));

            // Payload
            std::vector<uint8_t> paymentType(2, 0x00); // Payment [Automatic Detect]
            msg.addPayload(createPayload(0x0000000A, static_cast<uint16_t>(FIELD_ID::ID_TXN_TYPE), 0x00, 0x38, paymentType));

            auto fieldData = std::static_pointer_cast<CommandPaymentRequestData>(payloadData);

            std::vector<uint8_t> amount(4, 0x00);
            if (fieldData)
            {
                amount = Common::getInstance()->FnConvertUint32ToVector(fieldData->amount);
            }
            msg.addPayload(createPayload(0x0000000C, static_cast<uint16_t>(FIELD_ID::ID_TXN_AMOUNT), 0x00, 0x38, amount));

            std::vector<uint8_t> mer_ref_num(12, 0x00);
            if (fieldData)
            {
                mer_ref_num = Common::getInstance()->FnConvertAsciiToUint8Vector(fieldData->mer_ref_num);
            }

            msg.addPayload(createPayload(0x00000014, static_cast<uint16_t>(FIELD_ID::ID_TXN_MER_REF_NUM), 0x00, 0x31, mer_ref_num));

            std::vector<uint8_t> receipt = {0x01};
            msg.addPayload(createPayload(0x00000009, static_cast<uint16_t>(FIELD_ID::ID_TXN_RECEIPT_REQUIRED), 0x00, 0x38, receipt));

            std::vector<uint8_t> paddingBytes(5, 0x00);
            msg.addPayload(createPayload(0x0000000D, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x00, 0x33, paddingBytes));
            // End of Payload

            msg.setHeaderIntegrityCRC32(htole32(msg.FnCalculateIntegrityCRC32()));
            msg.setHeaderLength(htole32(128));
            break;
        }
        case UPT_CMD::PAYMENT_MODE_EFT_REQUEST:
        {
            // To be Implement
            break;
        }
        case UPT_CMD::PAYMENT_MODE_EFT_NETS_QR_REQUEST:
        {
            // To be Implement
            break;
        }
        case UPT_CMD::PAYMENT_MODE_BCA_REQUEST:
        {
            // To be Implement
            break;
        }
        case UPT_CMD::PAYMENT_MODE_CREDIT_CARD_REQUEST:
        {
            // To be Implement
            break;
        }
        case UPT_CMD::PAYMENT_MODE_NCC_REQUEST:
        {
            // To be Implement
            break;
        }
        case UPT_CMD::PAYMENT_MODE_NFP_REQUEST:
        {
            // To be Implement
            break;
        }
        case UPT_CMD::PAYMENT_MODE_EZ_LINK_REQUEST:
        {
            // To be Implement
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
            msg.setHeaderMsgType(htole32(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_CANCELLATION)));
            msg.setHeaderMsgCode(htole32(static_cast<uint32_t>(MSG_CODE::MSG_CODE_CANCELLATION_CANCEL)));
            msg.setHeaderIntegrityCRC32(htole32(msg.FnCalculateIntegrityCRC32()));
            msg.setHeaderLength(64);
            break;
        }

        // TOP-UP API
        case UPT_CMD::TOP_UP_NETS_NCC_BY_NETS_EFT_REQUEST:
        {
            msg.setHeaderMsgType(htole32(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_TOPUP)));
            msg.setHeaderMsgCode(htole32(static_cast<uint32_t>(MSG_CODE::MSG_CODE_TOPUP_NCC)));

            // Payload
            std::vector<uint8_t> paymentType = {0x80, 0x00};   // Top Up [Top up NETS NCC by NETS EFT]
            msg.addPayload(createPayload(0x0000000A, static_cast<uint16_t>(FIELD_ID::ID_TXN_TYPE), 0x00, 0x38, paymentType));

            auto fieldData = std::static_pointer_cast<CommandTopUpRequestData>(payloadData);

            std::vector<uint8_t> amount(4, 0x00);
            if (fieldData)
            {
                amount = Common::getInstance()->FnConvertUint32ToVector(fieldData->amount);
            }
            msg.addPayload(createPayload(0x0000000C, static_cast<uint16_t>(FIELD_ID::ID_TXN_AMOUNT), 0x00, 0x38, amount));

            std::vector<uint8_t> mer_ref_num(12, 0x00);
            if (fieldData)
            {
                mer_ref_num = Common::getInstance()->FnConvertAsciiToUint8Vector(fieldData->mer_ref_num);
            }

            msg.addPayload(createPayload(0x00000014, static_cast<uint16_t>(FIELD_ID::ID_TXN_MER_REF_NUM), 0x00, 0x31, mer_ref_num));

            std::vector<uint8_t> receipt = {0x01};
            msg.addPayload(createPayload(0x00000009, static_cast<uint16_t>(FIELD_ID::ID_TXN_RECEIPT_REQUIRED), 0x00, 0x38, receipt));

            std::vector<uint8_t> paddingBytes(5, 0x00);
            msg.addPayload(createPayload(0x0000000D, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x00, 0x33, paddingBytes));
            // End of Payload

            msg.setHeaderIntegrityCRC32(htole32(msg.FnCalculateIntegrityCRC32()));
            msg.setHeaderLength(htole32(128));
            break;
        }
        case UPT_CMD::TOP_UP_NETS_NFP_BY_NETS_EFT_REQUEST:
        {
            msg.setHeaderMsgType(htole32(static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_TOPUP)));
            msg.setHeaderMsgCode(htole32(static_cast<uint32_t>(MSG_CODE::MSG_CODE_TOPUP_NFP)));

            // Payload
            std::vector<uint8_t> paymentType = {0x81, 0x00};   // Top Up [Top up NETS NFP by NETS EFT]
            msg.addPayload(createPayload(0x0000000A, static_cast<uint16_t>(FIELD_ID::ID_TXN_TYPE), 0x00, 0x38, paymentType));

            auto fieldData = std::static_pointer_cast<CommandTopUpRequestData>(payloadData);

            std::vector<uint8_t> amount(4, 0x00);
            if (fieldData)
            {
                amount = Common::getInstance()->FnConvertUint32ToVector(fieldData->amount);
            }
            msg.addPayload(createPayload(0x0000000C, static_cast<uint16_t>(FIELD_ID::ID_TXN_AMOUNT), 0x00, 0x38, amount));

            std::vector<uint8_t> mer_ref_num(12, 0x00);
            if (fieldData)
            {
                mer_ref_num = Common::getInstance()->FnConvertAsciiToUint8Vector(fieldData->mer_ref_num);
            }

            msg.addPayload(createPayload(0x00000014, static_cast<uint16_t>(FIELD_ID::ID_TXN_MER_REF_NUM), 0x00, 0x31, mer_ref_num));

            std::vector<uint8_t> receipt = {0x01};
            msg.addPayload(createPayload(0x00000009, static_cast<uint16_t>(FIELD_ID::ID_TXN_RECEIPT_REQUIRED), 0x00, 0x38, receipt));

            std::vector<uint8_t> paddingBytes(5, 0x00);
            msg.addPayload(createPayload(0x0000000D, static_cast<uint16_t>(FIELD_ID::ID_PADDING), 0x00, 0x33, paddingBytes));
            // End of Payload

            msg.setHeaderIntegrityCRC32(htole32(msg.FnCalculateIntegrityCRC32()));
            msg.setHeaderLength(htole32(128));
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
    
    std::vector<uint8_t> completeMsg;
    std::vector<uint8_t> msgHeaderVector = msg.getHeaderMsgVector();
    completeMsg.insert(completeMsg.end(), msgHeaderVector.begin(), msgHeaderVector.end());
    std::vector<uint8_t> msgPayloadVector = msg.getPayloadMsgVector();
    completeMsg.insert(completeMsg.end(), msgPayloadVector.begin(), msgPayloadVector.end());

    data = msg.FnAddDataTransparency(completeMsg);

    Logger::getInstance()->FnLog(msg.FnGetMsgOutputLogString(data), logFileName_, "UPT");

    return data;
}

PayloadField Upt::createPayload(uint32_t length, uint16_t payloadFieldId, uint8_t fieldReserve, uint8_t fieldEncoding, const std::vector<uint8_t>& fieldData)
{
    PayloadField payload;

    payload.setPayloadFieldLength(htole32(length));
    payload.setPayloadFieldId(htole16(payloadFieldId));
    payload.setFieldReserve(fieldReserve);
    payload.setFieldEnconding(fieldEncoding);

    std::vector<uint8_t> fieldData_ = fieldData;
    if (fieldEncoding == static_cast<uint8_t>(Upt::FIELD_ENCODING::FIELD_ENCODING_VALUE_HEX_LITTLE))
    {
        std::reverse(fieldData_.begin(), fieldData_.end());
    }
    payload.setFieldData(fieldData_);

    return payload;
}

void Upt::startRead()
{
    boost::asio::post(strand_, [this]() {
        pSerialPort_->async_read_some(
            boost::asio::buffer(readBuffer_, readBuffer_.size()),
            boost::asio::bind_executor(strand_,
                                        std::bind(&Upt::readEnd, this,
                                        std::placeholders::_1,
                                        std::placeholders::_2)));
    });
}

void Upt::readEnd(const boost::system::error_code& error, std::size_t bytesTransferred)
{
    lastSerialReadTime_ = std::chrono::steady_clock::now();

    if (!error)
    {
        std::vector<uint8_t> data(readBuffer_.begin(), readBuffer_.begin() + bytesTransferred);
        if (isRxResponseComplete(data))
        {
            handleReceivedCmd(getRxBuffer());
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

bool Upt::isMsgStatusValid(uint32_t msgStatus)
{
    bool ret = true;

    switch (static_cast<MSG_STATUS>(msgStatus))
    {
        case MSG_STATUS::MSG_INTEGRITY_FAILED:
        case MSG_STATUS::MSG_TYPE_NOT_SUPPORTED:
        case MSG_STATUS::MSG_CODE_NOT_SUPPORTED:
        case MSG_STATUS::MSG_AUTHENTICATION_REQUIRED:
        case MSG_STATUS::MSG_AUTHENTICATION_FAILED:
        case MSG_STATUS::MSG_MAC_FAILED:
        case MSG_STATUS::MSG_LENGTH_MISMATCH:
        case MSG_STATUS::MSG_LENGTH_MINIMUM:
        case MSG_STATUS::MSG_LENGTH_MAXIMUM:
        case MSG_STATUS::MSG_VERSION_INVALID:
        case MSG_STATUS::MSG_CLASS_INVALID:
        case MSG_STATUS::MSG_STATUS_INVALID:
        case MSG_STATUS::MSG_ALGORITHM_INVALID:
        case MSG_STATUS::MSG_ALGORITHM_IS_MANDATORY:
        case MSG_STATUS::MSG_KEY_INDEX_INVALID:
        case MSG_STATUS::MSG_NOTIFICATION_INVALID:
        case MSG_STATUS::MSG_COMPLETION_INVALID:
        case MSG_STATUS::MSG_DATA_TRANSPARENCY_ERROR:
        case MSG_STATUS::MSG_INCOMPLETE:
        case MSG_STATUS::FIELD_MANDATORY_MISSING:
        case MSG_STATUS::FIELD_LENGTH_MINIMUM:
        case MSG_STATUS::FIELD_LENGTH_INVALID:
        case MSG_STATUS::FIELD_TYPE_INVALID:
        case MSG_STATUS::FIELD_ENCODING_INVALID:
        case MSG_STATUS::FIELD_DATA_INVALID:
        case MSG_STATUS::FIELD_PARSING_ERROR:
        {
            ret = false;
            break;
        }
    }

    return ret;
}

void Upt::enqueueWrite(const std::vector<uint8_t>& data)
{
    boost::asio::dispatch(strand_, [this, data = std::move(data)]() {
        bool wasWriting_ = this->write_in_progress_;
        writeQueue_.push(std::move(data));
        if (!wasWriting_)
        {
            startWrite();
        }
    });
}

void Upt::startWrite()
{
    if (writeQueue_.empty())
    {
        Logger::getInstance()->FnLog(__func__ + std::string(" Write Queue is empty."), logFileName_, "UPT");
        write_in_progress_ = false;  // Ensure flag is reset when queue is empty
        return;
    }

    write_in_progress_ = true;  // Set this immediately to prevent duplicate writes

    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastRead = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSerialReadTime_).count();

    // Check if less than 2 seconds in milliseconds
    if (timeSinceLastRead < 2000)
    {
        auto boostTime = boost::posix_time::milliseconds(2000 - timeSinceLastRead);
        serialWriteDelayTimer_.expires_from_now(boostTime);

        // Add debug logging to check if the timer is being set properly
        std::ostringstream oss;
        oss << "Setting delay timer for " << (2000 - timeSinceLastRead) << " ms";
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

        serialWriteDelayTimer_.async_wait(boost::asio::bind_executor(strand_, 
                [this](const boost::system::error_code& /*e*/) {
                    Logger::getInstance()->FnLog("Timer expired", logFileName_, "UPT");
                    boost::asio::post(strand_, [this]() { startWrite(); });
            }));
        
        return;
    }

    const auto& data = writeQueue_.front();
    std::ostringstream oss;
    oss << "Data sent : " << Common::getInstance()->FnGetDisplayVectorCharToHexString(data);
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");
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
        processEvent(EVENT::WRITE_COMPLETED);
    }
    else
    {
        std::ostringstream oss;
        oss << "Serial Write error: " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");
        processEvent(EVENT::WRITE_FAILED);
    }
    write_in_progress_ = false;
}

void Upt::stopSerialWriteDelayTimer()
{
    // Convert boost::posix_time::ptime to std::chrono::steady_clock::time_point
    auto timerExpirationTime = serialWriteDelayTimer_.expires_at();
    auto timerExpirationSteadyClock = std::chrono::steady_clock::time_point(std::chrono::milliseconds(timerExpirationTime.time_of_day().total_milliseconds()));

    if (timerExpirationSteadyClock > std::chrono::steady_clock::now())
    {
        serialWriteDelayTimer_.cancel();
        // Log the cancellation
        std::ostringstream oss;
        oss << "Cancel command received, serial write delay timer canceled.";
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

        // Pop the data from the queue as we want to skip it
        if (!writeQueue_.empty())
        {
            writeQueue_.pop();  // Remove the current data that is being delayed
            Logger::getInstance()->FnLog("Current command popped from the queue due to cancel.", logFileName_, "UPT");
        }
    }
}

std::vector<Upt::SettlementPayloadRow> Upt::findReceivedSettlementPayloadData(const std::vector<PayloadField>& payloads)
{
    std::vector<SettlementPayloadRow> rows;
    SettlementPayloadRow currentRow;
    int count = 0;
    std::string result = "";

    for (const auto& payload : payloads)
    {
        result = "";

        if ((payload.getPayloadFieldId() == static_cast<uint16_t>(FIELD_ID::ID_SOF_ACQUIRER))
            || (payload.getPayloadFieldId() == static_cast<uint16_t>(FIELD_ID::ID_SOF_NAME))
            || (payload.getPayloadFieldId() == static_cast<uint16_t>(FIELD_ID::ID_SOF_PRE_AUTH_COMPLETE_COUNT))
            || (payload.getPayloadFieldId() == static_cast<uint16_t>(FIELD_ID::ID_SOF_PRE_AUTH_COMPLETE_TOTAL))
            || (payload.getPayloadFieldId() == static_cast<uint16_t>(FIELD_ID::ID_SOF_SALE_COUNT))
            || (payload.getPayloadFieldId() == static_cast<uint16_t>(FIELD_ID::ID_SOF_SALE_TOTAL)))
        {
            std::vector<uint8_t> payloadFieldData = payload.getFieldData();

            if ((payload.getFieldEncoding() == static_cast<uint8_t>(FIELD_ENCODING::FIELD_ENCODING_ARRAY_ASCII))
                || (payload.getFieldEncoding() == static_cast<uint8_t>(FIELD_ENCODING::FIELD_ENCODING_ARRAY_ASCII_HEX))
                || (payload.getFieldEncoding() == static_cast<uint8_t>(FIELD_ENCODING::FIELD_ENCODING_VALUE_ASCII))
                || (payload.getFieldEncoding() == static_cast<uint8_t>(FIELD_ENCODING::FIELD_ENCODING_VALUE_ASCII_HEX)))
            {
                std::string str(payloadFieldData.begin(), payloadFieldData.end());
                result = str;
                count++;
            }
            else if ((payload.getFieldEncoding() == static_cast<uint8_t>(FIELD_ENCODING::FIELD_ENCODING_VALUE_HEX_LITTLE)))
            {
                result = Common::getInstance()->FnConvertVectorUint8ToHexString(payloadFieldData, true);
                count++;
            }
            else if (payload.getFieldEncoding() == static_cast<uint8_t>(FIELD_ENCODING::FIELD_ENCODING_VALUE_BCD))
            {
                result = Common::getInstance()->FnConvertVectorUint8ToBcdString(payloadFieldData);
                count++;
            }
            else if ((payload.getFieldEncoding() == static_cast<uint8_t>(FIELD_ENCODING::FIELD_ENCODING_NONE))
                || (payload.getFieldEncoding() == static_cast<uint8_t>(FIELD_ENCODING::FIELD_ENCODING_ARRAY_HEX))
                || (payload.getFieldEncoding() == static_cast<uint8_t>(FIELD_ENCODING::FIELD_ENCODING_VALUE_HEX_BIG)))
            {
                result = Common::getInstance()->FnConvertVectorUint8ToHexString(payloadFieldData);
                count++;
            }
        }
        
        if ((payload.getPayloadFieldId() == static_cast<uint16_t>(FIELD_ID::ID_SOF_ACQUIRER)))
        {
            currentRow.acquirer = result;
        }
        else if ((payload.getPayloadFieldId() == static_cast<uint16_t>(FIELD_ID::ID_SOF_NAME)))
        {
            currentRow.name = result;
        }
        else if ((payload.getPayloadFieldId() == static_cast<uint16_t>(FIELD_ID::ID_SOF_PRE_AUTH_COMPLETE_COUNT)))
        {
            currentRow.preAuthCompleteCount = result;
        }
        else if ((payload.getPayloadFieldId() == static_cast<uint16_t>(FIELD_ID::ID_SOF_PRE_AUTH_COMPLETE_TOTAL)))
        {
            currentRow.preAuthCompleteTotal = result;
        }
        else if ((payload.getPayloadFieldId() == static_cast<uint16_t>(FIELD_ID::ID_SOF_SALE_COUNT)))
        {
            currentRow.saleCount = result;
        }
        else if ((payload.getPayloadFieldId() == static_cast<uint16_t>(FIELD_ID::ID_SOF_SALE_TOTAL)))
        {
            currentRow.saleTotal = result;
        }

        if (count == 6)
        {
            rows.push_back(currentRow);
            currentRow = SettlementPayloadRow();
            count = 0;
        }
    }

    return rows;
}

std::string Upt::findReceivedPayloadData(const std::vector<PayloadField>& payloads, uint16_t payloadFieldId)
{
    std::string result = "";
    uint64_t totalSale = 0;
    uint64_t totalCount = 0;

    for (auto const& payload : payloads)
    {
        if (payload.getPayloadFieldId() == payloadFieldId)
        {
            std::vector<uint8_t> payloadFieldData = payload.getFieldData();

            if ((payload.getFieldEncoding() == static_cast<uint8_t>(FIELD_ENCODING::FIELD_ENCODING_ARRAY_ASCII))
                || (payload.getFieldEncoding() == static_cast<uint8_t>(FIELD_ENCODING::FIELD_ENCODING_ARRAY_ASCII_HEX))
                || (payload.getFieldEncoding() == static_cast<uint8_t>(FIELD_ENCODING::FIELD_ENCODING_VALUE_ASCII))
                || (payload.getFieldEncoding() == static_cast<uint8_t>(FIELD_ENCODING::FIELD_ENCODING_VALUE_ASCII_HEX)))
            {
                std::string str(payloadFieldData.begin(), payloadFieldData.end());
                result += str;
                break;
            }
            else if ((payload.getFieldEncoding() == static_cast<uint8_t>(FIELD_ENCODING::FIELD_ENCODING_VALUE_HEX_LITTLE)))
            {
                result += Common::getInstance()->FnConvertVectorUint8ToHexString(payloadFieldData, true);
                break;
            }
            else if (payload.getFieldEncoding() == static_cast<uint8_t>(FIELD_ENCODING::FIELD_ENCODING_VALUE_BCD))
            {
                result += Common::getInstance()->FnConvertVectorUint8ToBcdString(payloadFieldData);
                break;
            }
            else if ((payload.getFieldEncoding() == static_cast<uint8_t>(FIELD_ENCODING::FIELD_ENCODING_NONE))
                || (payload.getFieldEncoding() == static_cast<uint8_t>(FIELD_ENCODING::FIELD_ENCODING_ARRAY_HEX))
                || (payload.getFieldEncoding() == static_cast<uint8_t>(FIELD_ENCODING::FIELD_ENCODING_VALUE_HEX_BIG)))
            {
                result += Common::getInstance()->FnConvertVectorUint8ToHexString(payloadFieldData);
                break;
            }
            else
            {
                break;
            }
        }
    }

    return result;
}

void Upt::handleCmdResponse(const Message& msg)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "UPT");

    if (msg.getHeaderMsgType() == static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_CARD))
    {
        if (msg.getHeaderMsgCode() == static_cast<uint32_t>(MSG_CODE::MSG_CODE_CARD_DETECT))
        {
            Logger::getInstance()->FnLog("Handle MSG_CODE_CARD_DETECT", logFileName_, "UPT");

            std::string msgRetCode = "";
            std::string cardTypeStr = "";
            std::string cardCanStr = "";
            std::string cardBalanceStr = "";

            msgRetCode = Common::getInstance()->FnUint32ToString(msg.getHeaderMsgStatus());
            cardTypeStr = findReceivedPayloadData(msg.getPayloads(), static_cast<uint16_t>(FIELD_ID::ID_CARD_TYPE));
            cardCanStr = findReceivedPayloadData(msg.getPayloads(), static_cast<uint16_t>(FIELD_ID::ID_CARD_CAN));
            cardBalanceStr = findReceivedPayloadData(msg.getPayloads(), static_cast<uint16_t>(FIELD_ID::ID_CARD_BALANCE));

            try
            {
                // Need to change to decimal from hex string
                cardBalanceStr = std::to_string(std::stoi(cardBalanceStr, nullptr, 16));
            }
            catch(const std::exception& ex)
            {
                std::ostringstream oss;
                oss << "Exception : " << ex.what();
                Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");
            }

            // Need to reverse the card type as the field encoding not matched
            cardTypeStr = Common::getInstance()->FnReverseByPair(cardTypeStr);

            // Cash Card Type
            if (cardTypeStr == "1001")
            {
                cardTypeStr.clear();
                cardTypeStr = "0";
            }
            // NETS Flashpay Card Type
            else if (cardTypeStr == "1002")
            {
                cardTypeStr.clear();
                cardTypeStr = "1";
            }
            // NETS EFT Card Type (ATM Card)
            else if (cardTypeStr == "1000")
            {
                cardTypeStr.clear();
                cardTypeStr = "2";
            }
            // EzLink Card Type
            else if (cardTypeStr == "7000")
            {
                cardTypeStr.clear();
                cardTypeStr = "3";
            }
            // Scheme Credit
            else if (cardTypeStr == "8000")
            {
                cardTypeStr.clear();
                cardTypeStr = "4";
            }
            // Scheme Debit
            else if (cardTypeStr == "9000")
            {
                cardTypeStr.clear();
                cardTypeStr = "5";
            }

            std::ostringstream oss;
            oss << "msgStatus=" << msgRetCode;
            oss << ",cardType=" << cardTypeStr;
            oss << ",cardCan=" << cardCanStr;
            oss << ",cardBalance=" << cardBalanceStr;
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleUPTCardDetect", oss.str());
        }
    }
    else if (msg.getHeaderMsgType() == static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_PAYMENT))
    {
        if (msg.getHeaderMsgCode() == static_cast<uint32_t>(MSG_CODE::MSG_CODE_PAYMENT_AUTO))
        {
            Logger::getInstance()->FnLog("Handle MSG_CODE_PAYMENT_AUTO", logFileName_, "UPT");

            std::string msgRetCode = "";
            std::string cardCanStr = "";
            std::string cardDeductFeeStr = "";
            std::string cardBalanceStr = "";
            std::string cardReferenceNumberStr = "";
            std::string batchNoStr = "";
            std::string cardTypeStr = "";

            msgRetCode = Common::getInstance()->FnUint32ToString(msg.getHeaderMsgStatus());
            cardCanStr = findReceivedPayloadData(msg.getPayloads(), static_cast<uint16_t>(FIELD_ID::ID_CARD_CAN));
            cardDeductFeeStr = findReceivedPayloadData(msg.getPayloads(), static_cast<uint16_t>(FIELD_ID::ID_TXN_AMOUNT));
            cardBalanceStr = findReceivedPayloadData(msg.getPayloads(), static_cast<uint16_t>(FIELD_ID::ID_CARD_BALANCE));
            cardReferenceNumberStr = findReceivedPayloadData(msg.getPayloads(), static_cast<uint16_t>(FIELD_ID::ID_TXN_MER_REF_NUM));
            batchNoStr = findReceivedPayloadData(msg.getPayloads(), static_cast<uint16_t>(FIELD_ID::ID_TXN_BATCH));
            cardTypeStr = findReceivedPayloadData(msg.getPayloads(), static_cast<uint16_t>(FIELD_ID::ID_TXN_TYPE));

            try
            {
                // Need to change to decimal from hex string
                cardBalanceStr = std::to_string(std::stoi(cardBalanceStr, nullptr, 16));
                cardDeductFeeStr = std::to_string(std::stoi(cardDeductFeeStr, nullptr, 16)); 
            }
            catch (const std::exception& ex)
            {
                std::ostringstream oss;
                oss << "Exception : " << ex.what();
                Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");
            }

            // Cash Card Type
            if (cardTypeStr == "1100")
            {
                cardTypeStr.clear();
                cardTypeStr = "0";
            }
            // NETS Flashpay Card Type
            else if (cardTypeStr == "1200")
            {
                cardTypeStr.clear();
                cardTypeStr = "1";
            }
            // NETS EFT Card Type (ATM Card)
            else if (cardTypeStr == "1000")
            {
                cardTypeStr.clear();
                cardTypeStr = "2";
            }
            // EzLink Card Type
            else if (cardTypeStr == "1700")
            {
                cardTypeStr.clear();
                cardTypeStr = "3";
            }
            // Scheme Credit
            else if (cardTypeStr == "2000")
            {
                cardTypeStr.clear();
                cardTypeStr = "4";
            }
            // Scheme Debit
            else if (cardTypeStr == "3000")
            {
                cardTypeStr.clear();
                cardTypeStr = "5";
            }

            std::ostringstream oss;
            oss << "msgStatus=" << msgRetCode;
            oss << ",cardCan=" << cardCanStr;
            oss << ",cardFee=" << cardDeductFeeStr;
            oss << ",cardBalance=" << cardBalanceStr;
            oss << ",cardReferenceNo=" << cardReferenceNumberStr;
            oss << ",cardBatchNo=" << batchNoStr;
            oss << ",cardType=" << cardTypeStr;
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleUPTPaymentAuto", oss.str());
        }
    }
    else if (msg.getHeaderMsgType() == static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_DEVICE))
    {
        if (msg.getHeaderMsgCode() == static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_SETTLEMENT))
        {
            Logger::getInstance()->FnLog("Handle MSG_CODE_DEVICE_SETTLEMENT", logFileName_, "UPT");

            std::string msgRetCode = "";
            uint64_t netsAmount = 0;
            uint64_t netsCount = 0;
            uint64_t nfpAmount = 0;
            uint64_t nfpCount = 0;
            uint64_t nccAmount = 0;
            uint64_t nccCount = 0;
            std::string totalAmount = "";
            std::string totalTransCount = "";
            std::string TID = "";
            std::string MID = "";

            msgRetCode = Common::getInstance()->FnUint32ToString(msg.getHeaderMsgStatus());
            std::vector<SettlementPayloadRow> settlementData = findReceivedSettlementPayloadData(msg.getPayloads());
            for (const auto& data : settlementData)
            {
                try
                {
                    if (data.name == "NETS")
                    {
                        netsAmount = std::stoull(data.saleTotal, nullptr, 16);
                        netsCount = std::stoull(data.saleCount, nullptr, 16);
                    }
                    else if (data.name == "NFP")
                    {
                        nfpAmount = std::stoull(data.saleTotal, nullptr, 16);
                        nfpCount = std::stoull(data.saleCount, nullptr, 16);
                    }
                    else if (data.name == "NCC")
                    {
                        nccAmount = std::stoull(data.saleTotal, nullptr, 16);
                        nccCount = std::stoull(data.saleCount, nullptr, 16);
                    }
                }
                catch (const std::exception& ex)
                {
                    std::ostringstream oss;
                    oss << "Exception : " << ex.what();
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");
                }
            }

            totalAmount = std::to_string(netsAmount + nfpAmount + nccAmount);
            totalTransCount = std::to_string(netsCount + nfpCount + nccCount);

            // NETS Document change these fields to TID and MID respectively
            TID = findReceivedPayloadData(msg.getPayloads(), static_cast<uint16_t>(FIELD_ID::ID_SOF_PRE_AUTH_COMPLETE_COUNT));
            MID = findReceivedPayloadData(msg.getPayloads(), static_cast<uint16_t>(FIELD_ID::ID_SOF_PRE_AUTH_COMPLETE_TOTAL));

            std::ostringstream oss;
            oss << "msg status : " << msgRetCode;
            oss << ", total amount : " << totalAmount;
            oss << " , total trans count : " << totalTransCount;
            oss << " , NETS(ATM, NFP, NCC) amount : " << netsAmount << ", " << nfpAmount << " , " << nccAmount;
            oss << " , NETS(ATM, NFP, NCC) count : " << netsCount << " , " << nfpCount << " , " << nccCount;
            oss << " , TID : " << TID;
            oss << " , MID : " << MID;
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

            std::ostringstream oss2;
            oss2 << "msgStatus=" << msgRetCode;
            oss2 << ",totalAmount=" << totalAmount;
            oss2 << ",totalTransCount=" << totalTransCount;
            oss2 << ",TID=" << TID;
            oss2 << ",MID=" << MID;
            Logger::getInstance()->FnLog(oss2.str(), logFileName_, "UPT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleUPTDeviceSettlement", oss2.str());
        }
        else if (msg.getHeaderMsgCode() == static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_RETRIEVE_LAST_SETTLEMENT))
        {
            Logger::getInstance()->FnLog("Handle MSG_CODE_DEVICE_RETRIEVE_LAST_SETTLEMENT", logFileName_, "UPT");

            std::string msgRetCode = "";
            uint64_t netsAmount = 0;
            uint64_t netsCount = 0;
            uint64_t nfpAmount = 0;
            uint64_t nfpCount = 0;
            uint64_t nccAmount = 0;
            uint64_t nccCount = 0;
            std::string totalAmount = "";
            std::string totalTransCount = "";

            msgRetCode = Common::getInstance()->FnUint32ToString(msg.getHeaderMsgStatus());

            std::vector<SettlementPayloadRow> settlementData = findReceivedSettlementPayloadData(msg.getPayloads());
            for (const auto& data : settlementData)
            {
                try
                {
                    if (data.name == "NETS")
                    {
                        netsAmount = std::stoi(data.saleTotal, nullptr, 16);
                        netsCount = std::stoi(data.saleCount, nullptr, 16);
                    }
                    else if (data.name == "NFP")
                    {
                        nfpAmount = std::stoi(data.saleTotal, nullptr, 16);
                        nfpCount = std::stoi(data.saleCount, nullptr, 16);
                    }
                    else if (data.name == "NCC")
                    {
                        nccAmount = std::stoi(data.saleTotal, nullptr, 16);
                        nccCount = std::stoi(data.saleCount, nullptr, 16);
                    }
                }
                catch (const std::exception& ex)
                {
                    std::ostringstream oss;
                    oss << "Exception : " << ex.what();
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");
                }
            }

            totalAmount = std::to_string(netsAmount + nfpAmount + nccAmount);
            totalTransCount = std::to_string(netsCount + nfpCount + nccCount);

            std::ostringstream oss;
            oss << "msg status : " << msgRetCode;
            oss << ", total amount : " << totalAmount;
            oss << " , total trans count : " << totalTransCount;
            oss << " , NETS(ATM, NFP, NCC) amount : " << netsAmount << ", " << nfpAmount << " , " << nccAmount;
            oss << " , NETS(ATM, NFP, NCC) count : " << netsCount << " , " << nfpCount << " , " << nccCount;
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

            std::ostringstream oss2;
            oss2 << "msgStatus=" << msgRetCode;
            oss2 << ",totalAmount=" << totalAmount;
            oss2 << ",totalTransCount=" << totalTransCount;
            Logger::getInstance()->FnLog(oss2.str(), logFileName_, "UPT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleUPTRetrieveLastSettlement", oss2.str());
        }
        else if (msg.getHeaderMsgCode() == static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_LOGON))
        {
            Logger::getInstance()->FnLog("Handle MSG_CODE_DEVICE_LOGON", logFileName_, "UPT");

            std::string msgRetCode = "";
            msgRetCode = Common::getInstance()->FnUint32ToString(msg.getHeaderMsgStatus());

            std::ostringstream oss;
            oss << "msgStatus=" << msgRetCode;
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleUPTDeviceLogon", oss.str());
        }
        else if (msg.getHeaderMsgCode() == static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_STATUS))
        {
            Logger::getInstance()->FnLog("Handle MSG_CODE_DEVICE_STATUS", logFileName_, "UPT");

            std::string msgRetCode = "";
            msgRetCode = Common::getInstance()->FnUint32ToString(msg.getHeaderMsgStatus());

            std::ostringstream oss;
            oss << "msgStatus=" << msgRetCode;
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleUPTDeviceStatus", oss.str());
        }
        else if (msg.getHeaderMsgCode() == static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_TIME_SYNC))
        {
            Logger::getInstance()->FnLog("Handle MSG_CODE_DEVICE_TIME_SYNC", logFileName_, "UPT");

            std::string msgRetCode = "";
            msgRetCode = Common::getInstance()->FnUint32ToString(msg.getHeaderMsgStatus());

            std::ostringstream oss;
            oss << "msgStatus=" << msgRetCode;
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleUPTDeviceTimeSync", oss.str());
        }
        else if (msg.getHeaderMsgCode() == static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_TMS))
        {
            Logger::getInstance()->FnLog("Handle MSG_CODE_DEVICE_TMS", logFileName_, "UPT");

            std::string msgRetCode = "";
            msgRetCode = Common::getInstance()->FnUint32ToString(msg.getHeaderMsgStatus());

            std::ostringstream oss;
            oss << "msgStatus=" << msgRetCode;
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleUPTDeviceTMS", oss.str());
        }
        else if (msg.getHeaderMsgCode() == static_cast<uint32_t>(MSG_CODE::MSG_CODE_DEVICE_RESET))
        {
            Logger::getInstance()->FnLog("Handle MSG_CODE_DEVICE_RESET", logFileName_, "UPT");

            std::string msgRetCode = "";
            msgRetCode = Common::getInstance()->FnUint32ToString(msg.getHeaderMsgStatus());

            std::ostringstream oss;
            oss << "msgStatus=" << msgRetCode;
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleUPTDeviceReset", oss.str());
        }
    }
    else if (msg.getHeaderMsgType() == static_cast<uint32_t>(MSG_TYPE::MSG_TYPE_CANCELLATION))
    {
        if (msg.getHeaderMsgCode() == static_cast<uint32_t>(MSG_CODE::MSG_CODE_CANCELLATION_CANCEL))
        {
            Logger::getInstance()->FnLog("Handle MSG_CODE_CANCELLATION_CANCEL", logFileName_, "UPT");

            std::string msgRetCode = "";
            msgRetCode = Common::getInstance()->FnUint32ToString(msg.getHeaderMsgStatus());

            std::ostringstream oss;
            oss << "msgStatus=" << msgRetCode;
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleUPTCommandCancel", oss.str());
        }
    }
}

void Upt::handleReceivedCmd(const std::vector<uint8_t>& msgDataBuff)
{
    std::stringstream receivedRespStream;

    receivedRespStream << "Received MSG data buffer: " << Common::getInstance()->FnGetDisplayVectorCharToHexString(msgDataBuff);
    Logger::getInstance()->FnLog(receivedRespStream.str(), logFileName_, "UPT");
    
    // Parse the msg data after removing STX and ETX
    Message msg;
    uint32_t msgParseStatus = msg.FnParseMsgData(msgDataBuff);

    std::ostringstream oss;
    oss << "Rx message parsing status: " << getMsgStatusString(msgParseStatus);
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

    if (msgParseStatus == static_cast<uint32_t>(MSG_STATUS::SUCCESS))
    {
        // Check Message sequence Number
        if (msg.getHeaderMsgSequence() == getSequenceNo())
        {
            if (msg.getHeaderMsgClass() == static_cast<uint16_t>(MSG_CLASS::MSG_CLASS_ACK))
            {
                // Log the response if received ACK
                Logger::getInstance()->FnLog(msg.FnGetMsgOutputLogString(msgDataBuff), logFileName_, "UPT");

                // Check Message Status
                if (isMsgStatusValid(msg.getHeaderMsgStatus()))
                {
                    ackRecv_.store(true);
                }
                else
                {
                    ackRecv_.store(false);
                }
                ackTimer_.cancel();
            }
            else if (msg.getHeaderMsgClass() == static_cast<uint16_t>(MSG_CLASS::MSG_CLASS_RSP))
            {
                // Log the response if received the response
                Logger::getInstance()->FnLog(msg.FnGetMsgOutputLogString(msgDataBuff), logFileName_, "UPT");

                handleCmdResponse(msg);
                rspRecv_.store(true);
                rspTimer_.cancel();
            }

            // Check Msg Status to see whether reset sequence number is required or not
            if (msg.getHeaderMsgStatus() == static_cast<uint32_t>(MSG_STATUS::INVALID_PARAMETER))
            {
                FnUptSendDeviceResetSequenceNumberRequest();
            }

            // Check Msg Status to see whether logon is required or not
            if (msg.getHeaderMsgStatus() == static_cast<uint32_t>(MSG_STATUS::SOF_LOGON_REQUIRED))
            {
                FnUptSendDeviceLogonRequest();
            }
        }
        else
        {
            Logger::getInstance()->FnLog("Ack or Rsp message sequence number incorrect.", logFileName_, "UPT");
        }
    }
}

void Upt::handleCmdErrorOrTimeout(Upt::UPT_CMD cmd, Upt::MSG_STATUS msgStatus)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "UPT");

    switch (cmd)
    {
        case UPT_CMD::DEVICE_STATUS_REQUEST:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << Common::getInstance()->FnUint32ToString(static_cast<uint32_t>(msgStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleUPTDeviceStatus", oss.str());
            break;
        }
        case UPT_CMD::DEVICE_RESET_REQUEST:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << Common::getInstance()->FnUint32ToString(static_cast<uint32_t>(msgStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleUPTDeviceReset", oss.str());
            break;
        }
        case UPT_CMD::DEVICE_TIME_SYNC_REQUEST:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << Common::getInstance()->FnUint32ToString(static_cast<uint32_t>(msgStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleUPTDeviceTimeSync", oss.str());
            break;
        }
        case UPT_CMD::DEVICE_PROFILE_REQUEST:
        {
            // To be implemented
            break;
        }
        case UPT_CMD::DEVICE_SOF_LIST_REQUEST:
        {
            // To be implemented
            break;
        }
        case UPT_CMD::DEVICE_SOF_SET_PRIORITY_REQUEST:
        {
            // To be implemented
            break;
        }
        case UPT_CMD::DEVICE_LOGON_REQUEST:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << Common::getInstance()->FnUint32ToString(static_cast<uint32_t>(msgStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleUPTDeviceLogon", oss.str());
            break;
        }
        case UPT_CMD::DEVICE_TMS_REQUEST:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << Common::getInstance()->FnUint32ToString(static_cast<uint32_t>(msgStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleUPTDeviceTMS", oss.str());
            break;
        }
        case UPT_CMD::DEVICE_SETTLEMENT_REQUEST:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << Common::getInstance()->FnUint32ToString(static_cast<uint32_t>(msgStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleUPTDeviceSettlement", oss.str());
            break;
        }
        case UPT_CMD::DEVICE_PRE_SETTLEMENT_REQUEST:
        {
            // To be implemented
            break;
        }
        case UPT_CMD::DEVICE_RESET_SEQUENCE_NUMBER_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::DEVICE_RETRIEVE_LAST_TRANSACTION_STATUS_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::DEVICE_RETRIEVE_LAST_SETTLEMENT_REQUEST:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << Common::getInstance()->FnUint32ToString(static_cast<uint32_t>(msgStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleUPTRetrieveLastSettlement", oss.str());
            break;
        }
        case UPT_CMD::MUTUAL_AUTHENTICATION_STEP_1_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::MUTUAL_AUTHENTICATION_STEP_2_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::CARD_DETECT_REQUEST:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << Common::getInstance()->FnUint32ToString(static_cast<uint32_t>(msgStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleUPTCardDetect", oss.str());
            break;
        }
        case UPT_CMD::CARD_DETAIL_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::CARD_HISTORICAL_LOG_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::PAYMENT_MODE_AUTO_REQUEST:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << Common::getInstance()->FnUint32ToString(static_cast<uint32_t>(msgStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleUPTPaymentAuto", oss.str());
            break;
        }
        case UPT_CMD::PAYMENT_MODE_EFT_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::PAYMENT_MODE_EFT_NETS_QR_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::PAYMENT_MODE_BCA_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::PAYMENT_MODE_CREDIT_CARD_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::PAYMENT_MODE_NCC_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::PAYMENT_MODE_NFP_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::PAYMENT_MODE_EZ_LINK_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::PRE_AUTHORIZATION_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::PRE_AUTHORIZATION_COMPLETION_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::INSTALLATION_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::VOID_PAYMENT_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::REFUND_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::CANCEL_COMMAND_REQUEST:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << Common::getInstance()->FnUint32ToString(static_cast<uint32_t>(msgStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "UPT");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleUPTCommandCancel", oss.str());
            break;
        }
        case UPT_CMD::TOP_UP_NETS_NCC_BY_NETS_EFT_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::TOP_UP_NETS_NFP_BY_NETS_EFT_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::CASE_DEPOSIT_REQUEST:
        {
           // To be implemented
            break;
        }
        case UPT_CMD::UOB_REQUEST:
        {
           // To be implemented
            break;
        }
    }
}
