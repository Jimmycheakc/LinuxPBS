#pragma once

#include <boost/asio/thread_pool.hpp>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <queue>
#include <vector>
#include <unordered_map>
#include "tcp_client.h"
#include <boost/json.hpp>


class EEPClient
{
public:

    const std::string LOCAL_EEP_SETTLEMENT_FOLDER_PATH = "/home/root/carpark/EEP_SETTLEMENT";

    enum class PRIORITY : int
    {
        UNSOLICITED     = 0,
        START           = 1,
        WATCHDOG        = 2,
        NORMAL          = 3,
        HEALTHSTATUS    = 4
    };

    enum class MSG_STATUS : uint32_t
    {
        // General Status
        SUCCESS         = 0x00000000u,
        SEND_FAILED     = 0x00000001u,
        ACK_TIMEOUT     = 0x00000002u,
        RSP_TIMEOUT     = 0x00000003u,
        PARSE_FAILED    = 0x00000004u
    };

    enum class MESSAGE_CODE : uint8_t
    {
        ACK                                                 = 0x10,
        NAK                                                 = 0x11,
        HEALTH_STATUS_REQUEST                               = 0x72,
        HEALTH_STATUS_RESPONSE                              = 0x13,
        WATCHDOG_REQUEST                                    = 0x73,
        WATCHDOG_RESPONSE                                   = 0x15,
        START_REQUEST                                       = 0x40,
        START_RESPONSE                                      = 0x41,
        STOP_REQUEST                                        = 0x42,
        STOP_RESPONSE                                       = 0x43,
        DI_STATUS_NOTIFICATION                              = 0x60,
        DI_STATUS_REQUEST                                   = 0x61,
        DI_STATUS_RESPONSE                                  = 0x62,
        SET_DO_REQUEST                                      = 0x63,
        SET_DI_PORT_CONFIGURATION                           = 0x64,
        GET_OBU_INFORMATION_REQUEST                         = 0x20,
        OBU_INFORMATION_NOTIFICATION                        = 0x21,
        GET_OBU_INFORMATION_STOP                            = 0x22,
        DEDUCT_REQUEST                                      = 0x82,
        TRANSACTION_DATA                                    = 0xF4,
        DEDUCT_STOP_REQUEST                                 = 0x83,
        TRANSACTION_REQUEST                                 = 0x76,
        CPO_INFORMATION_DISPLAY_REQUEST                     = 0x30,
        CPO_INFORMATION_DISPLAY_RESULT                      = 0x31,
        CARPARK_PROCESS_COMPLETE_NOTIFICATION               = 0x32,
        CARPARK_PROCESS_COMPLETE_RESULT                     = 0x37,
        DSRC_PROCESS_COMPLETE_NOTIFICATION                  = 0x33,
        STOP_REQUEST_OF_RELATED_INFORMATION_DISTRIBUTION    = 0x34,
        DSRC_STATUS_REQUEST                                 = 0x35,
        DSRC_STATUS_RESPONSE                                = 0x36,
        TIME_CALIBRATION_REQUEST                            = 0x50,
        TIME_CALIBRATION_RESPONSE                           = 0x51,
        EEP_RESTART_INQUIRY                                 = 0x53,
        EEP_RESTART_INQUIRY_RESPONSE                        = 0x54,
        NOTIFICATION_LOG                                    = 0xF2,
        SET_PARKING_AVAILABLE                               = 0x55,
        CD_DOWNLOAD_REQUEST                                 = 0x93
    };
    
    enum class STATE
    {
        IDLE,
        CONNECTING,
        CONNECTED,
        WRITING_REQUEST,
        WAITING_FOR_RESPONSE,
        RECONNECT,
        STATE_COUNT
    };

    enum class EVENT
    {
        CONNECT,
        CONNECT_SUCCESS,
        CONNECT_FAIL,
        CHECK_COMMAND,
        WRITE_COMMAND,
        WRITE_COMPLETED,
        WRITE_TIMEOUT,
        ACK_TIMER_CANCELLED_ACK_RECEIVED,
        ACK_AS_RSP_RECEIVED,
        ACK_TIMEOUT,
        RESPONSE_TIMER_CANCELLED_RSP_RECEIVED,
        RESPONSE_TIMEOUT,
        RECONNECT_REQUEST,
        UNSOLICITED_REQUEST_DONE,
        EVENT_COUNT
    };

    struct EventTransition
    {
        EVENT event;
        void (EEPClient::*eventHandler)(EVENT);
        STATE nextState;
    };

    struct StateTransition
    {
        STATE stateName;
        std::vector<EventTransition> transitions;
    };

    struct CommandDataBase
    {
        virtual ~CommandDataBase() = default;
        virtual std::vector<uint8_t> serialize() const = 0;
    };

    struct AckData : CommandDataBase
    {
        uint16_t seqNo;
        uint8_t reqDatatypeCode;
        uint8_t rsv[3];

        std::vector<uint8_t> serialize() const override
        {
            std::vector<uint8_t> out;

            out.push_back(reqDatatypeCode);
            out.insert(out.end(), std::begin(rsv), std::end(rsv));

            return out;
        }

        uint16_t getSeqNo() const { return seqNo; }
    };

    struct NakData : CommandDataBase
    {
        uint16_t seqNo;
        uint8_t reqDatatypeCode;
        uint8_t reasonCode;
        uint8_t rsv[2];

        std::vector<uint8_t> serialize() const override
        {
            std::vector<uint8_t> out;

            out.push_back(reqDatatypeCode);
            out.push_back(reasonCode);
            out.insert(out.end(), std::begin(rsv), std::end(rsv));

            return out;
        }

        uint16_t getSeqNo() const { return seqNo; }
    };

    struct SetDIPortConfigData : CommandDataBase
    {
        uint8_t periodDebounceDI1[2];
        uint8_t periodDebounceDI2[2];
        uint8_t periodDebounceDI3[2];
        uint8_t periodDebounceDI4[2];
        uint8_t periodDebounceDI5[2];
        uint8_t rsv[2];

        std::vector<uint8_t> serialize() const override
        {
            std::vector<uint8_t> out;

            out.insert(out.end(), std::begin(periodDebounceDI1), std::end(periodDebounceDI1));
            out.insert(out.end(), std::begin(periodDebounceDI2), std::end(periodDebounceDI2));
            out.insert(out.end(), std::begin(periodDebounceDI3), std::end(periodDebounceDI3));
            out.insert(out.end(), std::begin(periodDebounceDI4), std::end(periodDebounceDI4));
            out.insert(out.end(), std::begin(periodDebounceDI5), std::end(periodDebounceDI5));
            out.insert(out.end(), std::begin(rsv), std::end(rsv));

            return out;
        }
    };

    struct SetDOPort : CommandDataBase
    {
        uint8_t do1;
        uint8_t do2;
        uint8_t do3;
        uint8_t do4;
        uint8_t do5;
        uint8_t do6;
        uint8_t rsv[2];

        std::vector<uint8_t> serialize() const override
        {
            std::vector<uint8_t> out;
            out.push_back(do1);
            out.push_back(do2);
            out.push_back(do3);
            out.push_back(do4);
            out.push_back(do5);
            out.push_back(do6);
            out.insert(out.end(), std::begin(rsv), std::end(rsv));

            return out;
        }
    };

    struct DeductData : CommandDataBase
    {
        uint8_t serialNum[2];
        uint8_t rsv[10];
        uint8_t obuLabel[5];
        uint8_t rsv1[3];
        uint8_t chargeAmt[2];
        uint8_t rsv2[2];
        uint8_t parkingStartDay;
        uint8_t parkingStartMonth;
        uint8_t parkingStartYear[2];
        uint8_t rsv3;
        uint8_t parkingStartSecond;
        uint8_t parkingStartMinute;
        uint8_t parkingStartHour;
        uint8_t parkingEndDay;
        uint8_t parkingEndMonth;
        uint8_t parkingEndYear[2];
        uint8_t rsv4;
        uint8_t parkingEndSecond;
        uint8_t parkingEndMinute;
        uint8_t parkingEndHour;

        std::vector<uint8_t> serialize() const override
        {
            std::vector<uint8_t> out;

            out.insert(out.end(), std::begin(serialNum), std::end(serialNum));
            out.insert(out.end(), std::begin(rsv), std::end(rsv));
            out.insert(out.end(), std::begin(obuLabel), std::end(obuLabel));
            out.insert(out.end(), std::begin(rsv1), std::end(rsv1));
            out.insert(out.end(), std::begin(chargeAmt), std::end(chargeAmt));
            out.insert(out.end(), std::begin(rsv2), std::end(rsv2));
            out.push_back(parkingStartDay);
            out.push_back(parkingStartMonth);
            out.insert(out.end(), std::begin(parkingStartYear), std::end(parkingStartYear));
            out.push_back(rsv3);
            out.push_back(parkingStartSecond);
            out.push_back(parkingStartMinute);
            out.push_back(parkingStartHour);
            out.push_back(parkingEndDay);
            out.push_back(parkingEndMonth);
            out.insert(out.end(), std::begin(parkingEndYear), std::end(parkingEndYear));
            out.push_back(rsv4);
            out.push_back(parkingEndSecond);
            out.push_back(parkingEndMinute);
            out.push_back(parkingEndHour);

            return out;
        }
    };

    struct DeductStopData : CommandDataBase
    {
        uint8_t serialNum[2];
        uint8_t rsv[10];
        uint8_t obuLabel[5];
        uint8_t rsv1[3];

        std::vector<uint8_t> serialize() const override
        {
            std::vector<uint8_t> out;

            out.insert(out.end(), std::begin(serialNum), std::end(serialNum));
            out.insert(out.end(), std::begin(rsv), std::end(rsv));
            out.insert(out.end(), std::begin(obuLabel), std::end(obuLabel));
            out.insert(out.end(), std::begin(rsv1), std::end(rsv1));

            return out;
        }
    };

    struct TransactionReqData : CommandDataBase
    {
        uint8_t serialNum[2];
        uint8_t rsv[10];
        uint8_t obuLabel[5];
        uint8_t rsv1[3];

        std::vector<uint8_t> serialize() const override
        {
            std::vector<uint8_t> out;

            out.insert(out.end(), std::begin(serialNum), std::end(serialNum));
            out.insert(out.end(), std::begin(rsv), std::end(rsv));
            out.insert(out.end(), std::begin(obuLabel), std::end(obuLabel));
            out.insert(out.end(), std::begin(rsv1), std::end(rsv1));

            return out;
        }
    };

    struct CPOInfoDisplayData : CommandDataBase
    {
        uint8_t rsv[8];
        uint8_t obuLabel[5];
        uint8_t rsv1[3];
        uint8_t dataType;
        uint8_t rsv2[3];
        uint8_t timeout[4];
        uint8_t dataLenOfStoredData[4];
        char dataString1[21];
        char dataString2[21];
        char dataString3[21];
        char dataString4[21];
        char dataString5[21];
        std::vector<uint8_t> storedData;

        std::vector<uint8_t> serialize() const override
        {
            std::vector<uint8_t> out;

            out.insert(out.end(), std::begin(rsv), std::end(rsv));
            out.insert(out.end(), std::begin(obuLabel), std::end(obuLabel));
            out.insert(out.end(), std::begin(rsv1), std::end(rsv1));
            out.push_back(dataType);
            out.insert(out.end(), std::begin(rsv2), std::end(rsv2));
            out.insert(out.end(), std::begin(timeout), std::end(timeout));
            out.insert(out.end(), std::begin(dataLenOfStoredData), std::end(dataLenOfStoredData));
            out.insert(out.end(), reinterpret_cast<const uint8_t*>(dataString1), reinterpret_cast<const uint8_t*>(dataString1 + sizeof(dataString1)));
            out.insert(out.end(), reinterpret_cast<const uint8_t*>(dataString2), reinterpret_cast<const uint8_t*>(dataString2 + sizeof(dataString2)));
            out.insert(out.end(), reinterpret_cast<const uint8_t*>(dataString3), reinterpret_cast<const uint8_t*>(dataString3 + sizeof(dataString3)));
            out.insert(out.end(), reinterpret_cast<const uint8_t*>(dataString4), reinterpret_cast<const uint8_t*>(dataString4 + sizeof(dataString4)));
            out.insert(out.end(), reinterpret_cast<const uint8_t*>(dataString5), reinterpret_cast<const uint8_t*>(dataString5 + sizeof(dataString5)));
            out.insert(out.end(), storedData.begin(), storedData.end());

            return out;
        }
    };

    struct CarparkProcessCompleteData : CommandDataBase
    {
        uint8_t rsv[8];
        uint8_t obuLabel[5];
        uint8_t rsv1[3];
        uint8_t processingResult;
        uint8_t rsv2;
        uint8_t amt[2];

        std::vector<uint8_t> serialize() const override
        {
            std::vector<uint8_t> out;

            out.insert(out.end(), std::begin(rsv), std::end(rsv));
            out.insert(out.end(), std::begin(obuLabel), std::end(obuLabel));
            out.insert(out.end(), std::begin(rsv1), std::end(rsv1));
            out.push_back(processingResult);
            out.push_back(rsv2);
            out.insert(out.end(), std::begin(amt), std::end(amt));

            return out;
        }
    };

    struct DSRCProcessCompleteData : CommandDataBase
    {
        uint8_t rsv[8];
        uint8_t obuLabel[5];
        uint8_t rsv1[3];

        std::vector<uint8_t> serialize() const override
        {
            std::vector<uint8_t> out;

            out.insert(out.end(), std::begin(rsv), std::end(rsv));
            out.insert(out.end(), std::begin(obuLabel), std::end(obuLabel));
            out.insert(out.end(), std::begin(rsv1), std::end(rsv1));

            return out;
        }
    };

    struct StopReqOfRelatedInfoData : CommandDataBase
    {
        uint8_t rsv[8];
        uint8_t obuLabel[5];
        uint8_t rsv1[3];

        std::vector<uint8_t> serialize() const override
        {
            std::vector<uint8_t> out;

            out.insert(out.end(), std::begin(rsv), std::end(rsv));
            out.insert(out.end(), std::begin(obuLabel), std::end(obuLabel));
            out.insert(out.end(), std::begin(rsv1), std::end(rsv1));

            return out;
        }
    };

    struct TimeCalibrationData : CommandDataBase
    {
        uint8_t day;
        uint8_t month;
        uint8_t year[2];
        uint8_t rsv;
        uint8_t second;
        uint8_t minute;
        uint8_t hour;

        std::vector<uint8_t> serialize() const override
        {
            std::vector<uint8_t> out;

            out.push_back(day);
            out.push_back(month);
            out.insert(out.end(), std::begin(year), std::end(year));
            out.push_back(rsv);
            out.push_back(second);
            out.push_back(minute);
            out.push_back(hour);

            return out;
        }
    };

    struct SetParkingAvailabilityData : CommandDataBase
    {
        uint8_t availableLots[2];
        uint8_t totalLots[2];
        uint8_t rsv[12];

        std::vector<uint8_t> serialize() const override
        {
            std::vector<uint8_t> out;

            out.insert(out.end(), std::begin(availableLots), std::end(availableLots));
            out.insert(out.end(), std::begin(totalLots), std::end(totalLots));
            out.insert(out.end(), std::begin(rsv), std::end(rsv));

            return out;
        }
    };

    struct RestartInquiryResponseData : CommandDataBase
    {
        uint8_t response;
        uint8_t rsv[3];

        std::vector<uint8_t> serialize() const override
        {
            std::vector<uint8_t> out;

            out.push_back(response);
            out.insert(out.end(), std::begin(rsv), std::end(rsv));

            return out;
        }
    };

    struct MessageHeader
    {
        uint8_t destinationID_;
        uint8_t sourceID_;
        uint8_t dataTypeCode_;
        uint8_t rsv_;
        uint8_t day_;
        uint8_t month_;
        uint16_t year_;
        uint8_t rsv1_;
        uint8_t second_;
        uint8_t minute_;
        uint8_t hour_;
        uint16_t seqNo_;
        uint16_t dataLen_;

        static constexpr std::size_t HEADER_SIZE = 16;

        bool deserialize(const std::vector<uint8_t>& data)
        {
            if (data.size() < HEADER_SIZE)
            {
                return false;
            }

            destinationID_ = data[0];
            sourceID_ = data[1];
            dataTypeCode_ = data[2];
            rsv_ = data[3];
            day_ = data[4];
            month_ = data[5];
            year_ = (data[7] << 8) | data[6];
            rsv1_ = data[8];
            second_ = data[9];
            minute_ = data[10];
            hour_ = data[11];
            seqNo_ = (data[13] << 8) | data[12];
            dataLen_ = (data[15] << 8) | data[14];
            return true;
        }
    };

    enum class CommandType : uint8_t
    {
        UNKNOWN_REQ_CMD,
        ACK,
        NAK,
        HEALTH_STATUS_REQ_CMD,
        WATCHDOG_REQ_CMD,
        START_REQ_CMD,
        STOP_REQ_CMD,
        DI_REQ_CMD,
        DO_REQ_CMD,
        SET_DI_PORT_CONFIG_CMD,
        GET_OBU_INFO_REQ_CMD,
        GET_OBU_INFO_STOP_REQ_CMD,
        DEDUCT_REQ_CMD,
        DEDUCT_STOP_REQ_CMD,
        TRANSACTION_REQ_CMD,
        CPO_INFO_DISPLAY_REQ_CMD,
        CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD,
        DSRC_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD,
        STOP_REQ_OF_RELATED_INFO_DISTRIBUTION_CMD,
        DSRC_STATUS_REQ_CMD,
        TIME_CALIBRATION_REQ_CMD,
        SET_CARPARK_AVAIL_REQ_CMD,
        CD_DOWNLOAD_REQ_CMD,
        EEP_RESTART_INQUIRY_REQ_CMD
    };

    struct Command
    {
        CommandType type;
        int priority;   // Lower number = higher priority
        uint64_t sequence;   // auto-increment sequence
        std::shared_ptr<CommandDataBase> data;

        Command() : type(CommandType::WATCHDOG_REQ_CMD), priority(1), sequence(0) {} // Default values
        Command(CommandType t, int prio, uint64_t seq, std::shared_ptr<CommandDataBase> d = nullptr)
            : type(t), priority(prio), sequence(seq), data(std::move(d)) {}
    };

    struct CompareCommand
    {
        bool operator() (const Command& a, const Command& b) const
        {
            if (a.priority == b.priority)
            {
                return a.sequence > b.sequence; // Smaller sequence first (FIFO)
            }
            return a.priority > b.priority; // Smaller priority first
        }
    };


    // Event Wrapper for all the response & notification
    struct EEPEventWrapper
    {
        uint8_t commandReqType;
        uint8_t messageCode;
        uint32_t messageStatus;
        std::string payload;

        // Serialize wrapper to JSON
        boost::json::value to_json() const
        {
            boost::json::object obj;
            obj["commandReqType"] = commandReqType;
            obj["messageCode"] = messageCode;
            obj["messageStatus"] = messageStatus;
            if (!payload.empty())
            {
                obj["payload"] = boost::json::parse(payload);
            }
            else 
            {
                obj["payload"] = nullptr;  // or just skip adding it
            }

            return obj;
        }

        // Deserialize wrapper from JSON
        static EEPEventWrapper from_json(const boost::json::value& jv)
        {
            const boost::json::object& obj = jv.as_object();
            EEPEventWrapper event;
            event.commandReqType = static_cast<uint8_t>(obj.at("commandReqType").as_int64());
            event.messageCode = static_cast<uint8_t>(obj.at("messageCode").as_int64());
            event.messageStatus = static_cast<uint32_t>(obj.at("messageStatus").as_int64());

            if (obj.if_contains("payload") && !obj.at("payload").is_null())
            {
                event.payload = boost::json::serialize(obj.at("payload"));
            }
            else
            {
                event.payload.clear();  // empty string
            }
            return event;
        }
    };



    // Json format for response data serialization and deserialization
    struct healthStatus
    {
        uint32_t subSystemLabel;
        uint8_t equipmentType;
        uint8_t EEPDSRCDeviceId;
        uint16_t carparkId;
        uint8_t lifeCycleMode;
        uint8_t maintenanceMode;
        uint8_t operationStatus;
        uint8_t esimStatus;
        uint8_t powerStatus;
        uint8_t internalBatteryStatus;
        uint8_t usbBusStatus;
        uint8_t dioStatus;
        uint8_t securityKeyStatus;
        uint8_t doorSwitchStatus;
        uint8_t temperatureStatus;
        uint8_t temperatureInHousing;
        uint8_t gnssSignalQuality;
        uint8_t numberOfGnssStatelites;
        uint8_t timeSyncStatus;
        uint8_t wwanConnectionStatus;
        uint8_t DSRCEmissionMode;
        uint8_t numberOfDSRCSession;
        uint8_t rsv;
        uint8_t rsv1;
        uint8_t cpuLoad;
        uint8_t storageSpace;
        uint16_t rsv2;
        std::vector<uint8_t> version;
        uint16_t cscBlocklistTable1;
        uint16_t cscBlocklistTable2;
        uint16_t cscBlocklistTable3;
        uint16_t cscBlocklistTable4;
        uint16_t cscIssuerListTable1;
        uint16_t cscIssuerListTable2;
        uint16_t cscIssuerListTable3;
        uint16_t cscIssuerListTable4;
        uint8_t cscConfigurationParamVersion;
        uint32_t rsv3;
        uint32_t backendPaymentTerminationListversion;
        uint8_t rsv4;

        // Serialize to JSON
        boost::json::value to_json() const
        {
            boost::json::object obj;
            obj["subSystemLabel"] = subSystemLabel;
            obj["equipmentType"] = equipmentType;
            obj["EEPDSRCDeviceId"] = EEPDSRCDeviceId;
            obj["carparkId"] = carparkId;
            obj["lifeCycleMode"] = lifeCycleMode;
            obj["maintenanceMode"] = maintenanceMode;
            obj["operationStatus"] = operationStatus;
            obj["esimStatus"] = esimStatus;
            obj["powerStatus"] = powerStatus;
            obj["internalBatteryStatus"] = internalBatteryStatus;
            obj["usbBusStatus"] = usbBusStatus;
            obj["dioStatus"] = dioStatus;
            obj["securityKeyStatus"] = securityKeyStatus;
            obj["doorSwitchStatus"] = doorSwitchStatus;
            obj["temperatureStatus"] = temperatureStatus;
            obj["temperatureInHousing"] = temperatureInHousing;
            obj["gnssSignalQuality"] = gnssSignalQuality;
            obj["numberOfGnssStatelites"] = numberOfGnssStatelites;
            obj["timeSyncStatus"] = timeSyncStatus;
            obj["wwanConnectionStatus"] = wwanConnectionStatus;
            obj["DSRCEmissionMode"] = DSRCEmissionMode;
            obj["numberOfDSRCSession"] = numberOfDSRCSession;
            obj["rsv"] = rsv;
            obj["rsv1"] = rsv1;
            obj["cpuLoad"] = cpuLoad;
            obj["storageSpace"] = storageSpace;
            obj["rsv2"] = rsv2;

            boost::json::array versionArray;
            for (auto v : version) versionArray.push_back(v);
            obj["version"] = versionArray;

            obj["cscBlocklistTable1"] = cscBlocklistTable1;
            obj["cscBlocklistTable2"] = cscBlocklistTable2;
            obj["cscBlocklistTable3"] = cscBlocklistTable3;
            obj["cscBlocklistTable4"] = cscBlocklistTable4;
            obj["cscIssuerListTable1"] = cscIssuerListTable1;
            obj["cscIssuerListTable2"] = cscIssuerListTable2;
            obj["cscIssuerListTable3"] = cscIssuerListTable3;
            obj["cscIssuerListTable4"] = cscIssuerListTable4;
            obj["cscConfigurationParamVersion"] = cscConfigurationParamVersion;
            obj["rsv3"] = rsv3;
            obj["backendPaymentTerminationListversion"] = backendPaymentTerminationListversion;
            obj["rsv4"] = rsv4;

            return obj;
        }

        // Deserialize from JSON
        static healthStatus from_json(const boost::json::value& jv)
        {
            healthStatus hs;
            const boost::json::object& obj = jv.as_object();

            hs.subSystemLabel = static_cast<uint32_t>(obj.at("subSystemLabel").as_int64());
            hs.equipmentType = static_cast<uint8_t>(obj.at("equipmentType").as_int64());
            hs.EEPDSRCDeviceId = static_cast<uint8_t>(obj.at("EEPDSRCDeviceId").as_int64());
            hs.carparkId = static_cast<uint16_t>(obj.at("carparkId").as_int64());
            hs.lifeCycleMode = static_cast<uint8_t>(obj.at("lifeCycleMode").as_int64());
            hs.maintenanceMode = static_cast<uint8_t>(obj.at("maintenanceMode").as_int64());
            hs.operationStatus = static_cast<uint8_t>(obj.at("operationStatus").as_int64());
            hs.esimStatus = static_cast<uint8_t>(obj.at("esimStatus").as_int64());
            hs.powerStatus = static_cast<uint8_t>(obj.at("powerStatus").as_int64());
            hs.internalBatteryStatus = static_cast<uint8_t>(obj.at("internalBatteryStatus").as_int64());
            hs.usbBusStatus = static_cast<uint8_t>(obj.at("usbBusStatus").as_int64());
            hs.dioStatus = static_cast<uint8_t>(obj.at("dioStatus").as_int64());
            hs.securityKeyStatus = static_cast<uint8_t>(obj.at("securityKeyStatus").as_int64());
            hs.doorSwitchStatus = static_cast<uint8_t>(obj.at("doorSwitchStatus").as_int64());
            hs.temperatureStatus = static_cast<uint8_t>(obj.at("temperatureStatus").as_int64());
            hs.temperatureInHousing = static_cast<uint8_t>(obj.at("temperatureInHousing").as_int64());
            hs.gnssSignalQuality = static_cast<uint8_t>(obj.at("gnssSignalQuality").as_int64());
            hs.numberOfGnssStatelites = static_cast<uint8_t>(obj.at("numberOfGnssStatelites").as_int64());
            hs.timeSyncStatus = static_cast<uint8_t>(obj.at("timeSyncStatus").as_int64());
            hs.wwanConnectionStatus = static_cast<uint8_t>(obj.at("wwanConnectionStatus").as_int64());
            hs.DSRCEmissionMode = static_cast<uint8_t>(obj.at("DSRCEmissionMode").as_int64());
            hs.numberOfDSRCSession = static_cast<uint8_t>(obj.at("numberOfDSRCSession").as_int64());
            hs.rsv = static_cast<uint8_t>(obj.at("rsv").as_int64());
            hs.rsv1 = static_cast<uint8_t>(obj.at("rsv1").as_int64());
            hs.cpuLoad = static_cast<uint8_t>(obj.at("cpuLoad").as_int64());
            hs.storageSpace = static_cast<uint8_t>(obj.at("storageSpace").as_int64());
            hs.rsv2 = static_cast<uint16_t>(obj.at("rsv2").as_int64());

            const boost::json::array& versionArray = obj.at("version").as_array();
            hs.version.clear();
            for (auto& v : versionArray)
                hs.version.push_back(static_cast<uint8_t>(v.as_int64()));

            hs.cscBlocklistTable1 = static_cast<uint16_t>(obj.at("cscBlocklistTable1").as_int64());
            hs.cscBlocklistTable2 = static_cast<uint16_t>(obj.at("cscBlocklistTable2").as_int64());
            hs.cscBlocklistTable3 = static_cast<uint16_t>(obj.at("cscBlocklistTable3").as_int64());
            hs.cscBlocklistTable4 = static_cast<uint16_t>(obj.at("cscBlocklistTable4").as_int64());
            hs.cscIssuerListTable1 = static_cast<uint16_t>(obj.at("cscIssuerListTable1").as_int64());
            hs.cscIssuerListTable2 = static_cast<uint16_t>(obj.at("cscIssuerListTable2").as_int64());
            hs.cscIssuerListTable3 = static_cast<uint16_t>(obj.at("cscIssuerListTable3").as_int64());
            hs.cscIssuerListTable4 = static_cast<uint16_t>(obj.at("cscIssuerListTable4").as_int64());
            hs.cscConfigurationParamVersion = static_cast<uint8_t>(obj.at("cscConfigurationParamVersion").as_int64());
            hs.rsv3 = static_cast<uint32_t>(obj.at("rsv3").as_int64());
            hs.backendPaymentTerminationListversion = static_cast<uint32_t>(obj.at("backendPaymentTerminationListversion").as_int64());
            hs.rsv4 = static_cast<uint8_t>(obj.at("rsv4").as_int64());

            return hs;
        }

        // Convert to string
        std::string to_string() const
        {
            std::ostringstream oss;
            oss << "healthStatus { "
                << "subSystemLabel=" << subSystemLabel
                << ", equipmentType=" << static_cast<int>(equipmentType)
                << ", EEPDSRCDeviceId=" << static_cast<int>(EEPDSRCDeviceId)
                << ", carparkId=" << carparkId
                << ", lifeCycleMode=" << static_cast<int>(lifeCycleMode)
                << ", maintenanceMode=" << static_cast<int>(maintenanceMode)
                << ", operationStatus=" << static_cast<int>(operationStatus)
                << ", esimStatus=" << static_cast<int>(esimStatus)
                << ", powerStatus=" << static_cast<int>(powerStatus)
                << ", internalBatteryStatus=" << static_cast<int>(internalBatteryStatus)
                << ", usbBusStatus=" << static_cast<int>(usbBusStatus)
                << ", dioStatus=" << static_cast<int>(dioStatus)
                << ", securityKeyStatus=" << static_cast<int>(securityKeyStatus)
                << ", doorSwitchStatus=" << static_cast<int>(doorSwitchStatus)
                << ", temperatureStatus=" << static_cast<int>(temperatureStatus)
                << ", temperatureInHousing=" << static_cast<int>(temperatureInHousing)
                << ", gnssSignalQuality=" << static_cast<int>(gnssSignalQuality)
                << ", numberOfGnssStatelites=" << static_cast<int>(numberOfGnssStatelites)
                << ", timeSyncStatus=" << static_cast<int>(timeSyncStatus)
                << ", wwanConnectionStatus=" << static_cast<int>(wwanConnectionStatus)
                << ", DSRCEmissionMode=" << static_cast<int>(DSRCEmissionMode)
                << ", numberOfDSRCSession=" << static_cast<int>(numberOfDSRCSession)
                << ", rsv=" << static_cast<int>(rsv)
                << ", rsv1=" << static_cast<int>(rsv1)
                << ", cpuLoad=" << static_cast<int>(cpuLoad)
                << ", storageSpace=" << static_cast<int>(storageSpace)
                << ", rsv2=" << rsv2
                << ", version=[";
            for (size_t i = 0; i < version.size(); ++i)
            {
                if (i != 0) oss << ",";
                oss << static_cast<int>(version[i]);
            }
            oss << "]"
                << ", cscBlocklistTable1=" << cscBlocklistTable1
                << ", cscBlocklistTable2=" << cscBlocklistTable2
                << ", cscBlocklistTable3=" << cscBlocklistTable3
                << ", cscBlocklistTable4=" << cscBlocklistTable4
                << ", cscIssuerListTable1=" << cscIssuerListTable1
                << ", cscIssuerListTable2=" << cscIssuerListTable2
                << ", cscIssuerListTable3=" << cscIssuerListTable3
                << ", cscIssuerListTable4=" << cscIssuerListTable4
                << ", cscConfigurationParamVersion=" << static_cast<int>(cscConfigurationParamVersion)
                << ", rsv3=" << rsv3
                << ", backendPaymentTerminationListversion=" << backendPaymentTerminationListversion
                << ", rsv4=" << static_cast<int>(rsv4)
                << " }";
            return oss.str();
        }
    };

    struct ack
    {
        uint8_t resultCode;
        uint32_t rsv;

        // Serialize to JSON
        boost::json::value to_json() const
        {
            boost::json::object obj;
            obj["resultCode"] = resultCode;
            obj["rsv"] = rsv;

            return obj;
        }

        // Deserialize from JSON
        static ack from_json(const boost::json::value& jv)
        {
            ack ackdata;
            const boost::json::object& obj = jv.as_object();

            ackdata.resultCode = static_cast<uint8_t>(obj.at("resultCode").as_int64());
            ackdata.rsv = static_cast<uint32_t>(obj.at("rsv").as_int64());

            return ackdata;
        }

        // Convert to string
        std::string to_string() const
        {
            std::ostringstream oss;
            oss << "ack { "
                << "resultCode=" << static_cast<int>(resultCode)
                << ", rsv=" << rsv
                << " }";
            return oss.str();
        }
    };

    struct ackDeduct
    {
        uint8_t resultCode;
        uint32_t rsv;
        uint16_t deductSerialNo;

        // Serialize to JSON
        boost::json::value to_json() const
        {
            boost::json::object obj;
            obj["resultCode"] = resultCode;
            obj["rsv"] = rsv;
            obj["deductSerialNo"] = deductSerialNo;

            return obj;
        }

        // Deserialize from JSON
        static ackDeduct from_json(const boost::json::value& jv)
        {
            ackDeduct ackdata;
            const boost::json::object& obj = jv.as_object();

            ackdata.resultCode = static_cast<uint8_t>(obj.at("resultCode").as_int64());
            ackdata.rsv = static_cast<uint32_t>(obj.at("rsv").as_int64());
            ackdata.deductSerialNo = static_cast<uint16_t>(obj.at("deductSerialNo").as_int64());

            return ackdata;
        }

        // Convert to string
        std::string to_string() const
        {
            std::ostringstream oss;
            oss << "ack { "
                << "resultCode=" << static_cast<int>(resultCode)
                << ", rsv=" << rsv
                << ", deductSerialNo=" << deductSerialNo
                << " }";
            return oss.str();
        }
    };

    struct nak
    {
        uint8_t requestDataTypeCode;
        uint8_t reasonCode;
        uint16_t rsv;

        // Serialize to JSON
        boost::json::value to_json() const
        {
            boost::json::object obj;
            obj["requestDataTypeCode"] = requestDataTypeCode;
            obj["reasonCode"] = reasonCode;
            obj["rsv"] = rsv;

            return obj;
        }

        // Deserialize from JSON
        static nak from_json(const boost::json::value& jv)
        {
            nak nakdata;
            const boost::json::object& obj = jv.as_object();

            nakdata.requestDataTypeCode = static_cast<uint8_t>(obj.at("requestDataTypeCode").as_int64());
            nakdata.reasonCode = static_cast<uint8_t>(obj.at("reasonCode").as_int64());
            nakdata.rsv = static_cast<uint16_t>(obj.at("rsv").as_int64());

            return nakdata;
        }

        // Convert to string
        std::string to_string() const
        {
            std::ostringstream oss;
            oss << "nak { "
                << "requestDataTypeCode=" << static_cast<int>(requestDataTypeCode)
                << ", reasonCode=" << static_cast<int>(reasonCode)
                << ", rsv=" << rsv
                << " }";
            return oss.str();
        }
    };

    struct startResponse
    {
        uint8_t resultCode;
        uint32_t rsv;

        // Serialize to JSON
        boost::json::value to_json() const
        {
            boost::json::object obj;
            obj["resultCode"] = resultCode;
            obj["rsv"] = rsv;

            return obj;
        }

        // Deserialize from JSON
        static startResponse from_json(const boost::json::value& jv)
        {
            startResponse sr;
            const boost::json::object& obj = jv.as_object();

            sr.resultCode = static_cast<uint8_t>(obj.at("resultCode").as_int64());
            sr.rsv = static_cast<uint32_t>(obj.at("rsv").as_int64());

            return sr;
        }

        // Convert to string
        std::string to_string() const
        {
            std::ostringstream oss;
            oss << "startResponse { "
                << "resultCode=" << static_cast<int>(resultCode)
                << ", rsv=" << rsv
                << " }";
            return oss.str();
        }
    };

    struct stopResponse
    {
        uint8_t resultCode;
        uint32_t rsv;

        // Serialize to JSON
        boost::json::value to_json() const
        {
            boost::json::object obj;
            obj["resultCode"] = resultCode;
            obj["rsv"] = rsv;

            return obj;
        }

        // Deserialize from JSON
        static stopResponse from_json(const boost::json::value& jv)
        {
            stopResponse sr;
            const boost::json::object& obj = jv.as_object();
            sr.resultCode = static_cast<uint8_t>(obj.at("resultCode").as_int64());
            sr.rsv = static_cast<uint32_t>(obj.at("rsv").as_int64());

            return sr;
        }

        // Convert to string
        std::string to_string() const
        {
            std::ostringstream oss;
            oss << "stopResponse { "
                << "resultCode=" << static_cast<int>(resultCode)
                << ", rsv=" << rsv
                << " }";
            return oss.str();
        }
    };

    struct diStatusResponse
    {
        uint32_t currDISignal;
        uint32_t prevDISignal;

        // Serialize to JSON
        boost::json::value to_json() const
        {
            boost::json::object obj;
            obj["currDISignal"] = currDISignal;
            obj["prevDISignal"] = prevDISignal;

            return obj;
        }

        // Deserialize from JSON
        static diStatusResponse from_json(const boost::json::value& jv)
        {
            diStatusResponse dsr;
            const boost::json::object& obj = jv.as_object();

            dsr.currDISignal = static_cast<uint32_t>(obj.at("currDISignal").as_int64());
            dsr.prevDISignal = static_cast<uint32_t>(obj.at("prevDISignal").as_int64());

            return dsr;
        }

        // Convert to string
        std::string to_string() const
        {
            std::ostringstream oss;
            oss << "diStatusResponse { "
                << "currDISignal=" << currDISignal
                << ", prevDISignal=" << prevDISignal
                << " }";
            return oss.str();
        }
    };

    struct dsrcStatusResponse
    {
        uint8_t dsrcEmissionStatus;
        uint8_t presenceLoopStatus;
        uint8_t dsrcEmissionControl;
        uint8_t noOfWaveConnection;
        uint64_t OBU1_rsv;
        uint64_t OBU1_obulabel;
        uint32_t OBU1_rsv1;
        uint32_t OBU1_elapsedTime;
        uint8_t OBU1_rssi;
        uint8_t OBU1_connectionStatus;
        uint16_t OBU1_rsv2;
        uint64_t OBU2_rsv;
        uint64_t OBU2_obulabel;
        uint32_t OBU2_rsv1;
        uint32_t OBU2_elapsedTime;
        uint8_t OBU2_rssi;
        uint8_t OBU2_connectionStatus;
        uint16_t OBU2_rsv2;
        uint64_t OBU3_rsv;
        uint64_t OBU3_obulabel;
        uint32_t OBU3_rsv1;
        uint32_t OBU3_elapsedTime;
        uint8_t OBU3_rssi;
        uint8_t OBU3_connectionStatus;
        uint16_t OBU3_rsv2;
        uint64_t OBU4_rsv;
        uint64_t OBU4_obulabel;
        uint32_t OBU4_rsv1;
        uint32_t OBU4_elapsedTime;
        uint8_t OBU4_rssi;
        uint8_t OBU4_connectionStatus;
        uint16_t OBU4_rsv2;

        // Serialize to JSON
        boost::json::value to_json() const
        {
            boost::json::object obj;
            obj["dsrcEmissionStatus"] = dsrcEmissionStatus;
            obj["presenceLoopStatus"] = presenceLoopStatus;
            obj["dsrcEmissionControl"] = dsrcEmissionControl;
            obj["noOfWaveConnection"] = noOfWaveConnection;
            obj["OBU1_rsv"] = std::to_string(OBU1_rsv);
            obj["OBU1_obulabel"] = std::to_string(OBU1_obulabel);
            obj["OBU1_rsv1"] = OBU1_rsv1;
            obj["OBU1_elapsedTime"] = OBU1_elapsedTime;
            obj["OBU1_rssi"] = OBU1_rssi;
            obj["OBU1_connectionStatus"] = OBU1_connectionStatus;
            obj["OBU1_rsv2"] = OBU1_rsv2;
            obj["OBU2_rsv"] = std::to_string(OBU2_rsv);
            obj["OBU2_obulabel"] = std::to_string(OBU2_obulabel);
            obj["OBU2_rsv1"] = OBU2_rsv1;
            obj["OBU2_elapsedTime"] = OBU2_elapsedTime;
            obj["OBU2_rssi"] = OBU2_rssi;
            obj["OBU2_connectionStatus"] = OBU2_connectionStatus;
            obj["OBU2_rsv2"] = OBU2_rsv2;
            obj["OBU3_rsv"] = std::to_string(OBU3_rsv);
            obj["OBU3_obulabel"] = std::to_string(OBU3_obulabel);
            obj["OBU3_rsv1"] = OBU3_rsv1;
            obj["OBU3_elapsedTime"] = OBU3_elapsedTime;
            obj["OBU3_rssi"] = OBU3_rssi;
            obj["OBU3_connectionStatus"] = OBU3_connectionStatus;
            obj["OBU3_rsv2"] = OBU3_rsv2;
            obj["OBU4_rsv"] = std::to_string(OBU4_rsv);
            obj["OBU4_obulabel"] = std::to_string(OBU4_obulabel);
            obj["OBU4_rsv1"] = OBU4_rsv1;
            obj["OBU4_elapsedTime"] = OBU4_elapsedTime;
            obj["OBU4_rssi"] = OBU4_rssi;
            obj["OBU4_connectionStatus"] = OBU4_connectionStatus;
            obj["OBU4_rsv2"] = OBU4_rsv2;

            return obj;
        }

        // Deserialize from JSON
        static dsrcStatusResponse from_json(const boost::json::value& jv)
        {
            dsrcStatusResponse dsr;
            const boost::json::object& obj = jv.as_object();

            dsr.dsrcEmissionStatus = static_cast<uint8_t>(obj.at("dsrcEmissionStatus").as_int64());
            dsr.presenceLoopStatus = static_cast<uint8_t>(obj.at("presenceLoopStatus").as_int64());
            dsr.dsrcEmissionControl = static_cast<uint8_t>(obj.at("dsrcEmissionControl").as_int64());
            dsr.noOfWaveConnection = static_cast<uint8_t>(obj.at("noOfWaveConnection").as_int64());
            dsr.OBU1_rsv = std::stoull(obj.at("OBU1_rsv").as_string().c_str());
            dsr.OBU1_obulabel = std::stoull(obj.at("OBU1_obulabel").as_string().c_str());
            dsr.OBU1_rsv1 = static_cast<uint32_t>(obj.at("OBU1_rsv1").as_int64());
            dsr.OBU1_elapsedTime = static_cast<uint32_t>(obj.at("OBU1_elapsedTime").as_int64());
            dsr.OBU1_rssi = static_cast<uint8_t>(obj.at("OBU1_rssi").as_int64());
            dsr.OBU1_connectionStatus = static_cast<uint8_t>(obj.at("OBU1_connectionStatus").as_int64());
            dsr.OBU1_rsv2 = static_cast<uint16_t>(obj.at("OBU1_rsv2").as_int64());
            dsr.OBU2_rsv = std::stoull(obj.at("OBU2_rsv").as_string().c_str());
            dsr.OBU2_obulabel = std::stoull(obj.at("OBU2_obulabel").as_string().c_str());
            dsr.OBU2_rsv1 = static_cast<uint32_t>(obj.at("OBU2_rsv1").as_int64());
            dsr.OBU2_elapsedTime = static_cast<uint32_t>(obj.at("OBU2_elapsedTime").as_int64());
            dsr.OBU2_rssi = static_cast<uint8_t>(obj.at("OBU2_rssi").as_int64());
            dsr.OBU2_connectionStatus = static_cast<uint8_t>(obj.at("OBU2_connectionStatus").as_int64());
            dsr.OBU2_rsv2 = static_cast<uint16_t>(obj.at("OBU2_rsv2").as_int64());
            dsr.OBU3_rsv = std::stoull(obj.at("OBU3_rsv").as_string().c_str());
            dsr.OBU3_obulabel = std::stoull(obj.at("OBU3_obulabel").as_string().c_str());
            dsr.OBU3_rsv1 = static_cast<uint32_t>(obj.at("OBU3_rsv1").as_int64());
            dsr.OBU3_elapsedTime = static_cast<uint32_t>(obj.at("OBU3_elapsedTime").as_int64());
            dsr.OBU3_rssi = static_cast<uint8_t>(obj.at("OBU3_rssi").as_int64());
            dsr.OBU3_connectionStatus = static_cast<uint8_t>(obj.at("OBU3_connectionStatus").as_int64());
            dsr.OBU3_rsv2 = static_cast<uint16_t>(obj.at("OBU3_rsv2").as_int64());
            dsr.OBU4_rsv = std::stoull(obj.at("OBU4_rsv").as_string().c_str());
            dsr.OBU4_obulabel = std::stoull(obj.at("OBU4_obulabel").as_string().c_str());
            dsr.OBU4_rsv1 = static_cast<uint32_t>(obj.at("OBU4_rsv1").as_int64());
            dsr.OBU4_elapsedTime = static_cast<uint32_t>(obj.at("OBU4_elapsedTime").as_int64());
            dsr.OBU4_rssi = static_cast<uint8_t>(obj.at("OBU4_rssi").as_int64());
            dsr.OBU4_connectionStatus = static_cast<uint8_t>(obj.at("OBU4_connectionStatus").as_int64());
            dsr.OBU4_rsv2 = static_cast<uint16_t>(obj.at("OBU4_rsv2").as_int64());

            return dsr;
        }

        // Convert to string
        std::string to_string() const
        {
            std::ostringstream oss;
            oss << "dsrcStatusResponse { "
                << "dsrcEmissionStatus=" << static_cast<int>(dsrcEmissionStatus)
                << ", presenceLoopStatus=" << static_cast<int>(presenceLoopStatus)
                << ", dsrcEmissionControl=" << static_cast<int>(dsrcEmissionControl)
                << ", noOfWaveConnection=" << static_cast<int>(noOfWaveConnection)
                << ", OBU1={rsv=" << OBU1_rsv << ", obulabel=" << OBU1_obulabel 
                << ", rsv1=" << OBU1_rsv1 << ", elapsedTime=" << OBU1_elapsedTime 
                << ", rssi=" << static_cast<int>(OBU1_rssi) 
                << ", connectionStatus=" << static_cast<int>(OBU1_connectionStatus)
                << ", rsv2=" << OBU1_rsv2 << "}"
                << ", OBU2={rsv=" << OBU2_rsv << ", obulabel=" << OBU2_obulabel 
                << ", rsv1=" << OBU2_rsv1 << ", elapsedTime=" << OBU2_elapsedTime 
                << ", rssi=" << static_cast<int>(OBU2_rssi) 
                << ", connectionStatus=" << static_cast<int>(OBU2_connectionStatus)
                << ", rsv2=" << OBU2_rsv2 << "}"
                << ", OBU3={rsv=" << OBU3_rsv << ", obulabel=" << OBU3_obulabel 
                << ", rsv1=" << OBU3_rsv1 << ", elapsedTime=" << OBU3_elapsedTime 
                << ", rssi=" << static_cast<int>(OBU3_rssi) 
                << ", connectionStatus=" << static_cast<int>(OBU3_connectionStatus)
                << ", rsv2=" << OBU3_rsv2 << "}"
                << ", OBU4={rsv=" << OBU4_rsv << ", obulabel=" << OBU4_obulabel 
                << ", rsv1=" << OBU4_rsv1 << ", elapsedTime=" << OBU4_elapsedTime 
                << ", rssi=" << static_cast<int>(OBU4_rssi) 
                << ", connectionStatus=" << static_cast<int>(OBU4_connectionStatus)
                << ", rsv2=" << OBU4_rsv2 << "}"
                << " }";
            return oss.str();
        }
    };

    struct timeCalibrationResponse
    {
        uint8_t result;
        uint32_t rsv;

        // Serialize to JSON
        boost::json::value to_json() const
        {
            boost::json::object obj;
            obj["result"] = result;
            obj["rsv"] = rsv;

            return obj;
        }

        // Deserialize from JSON
        static timeCalibrationResponse from_json(const boost::json::value& jv)
        {
            timeCalibrationResponse tcr;
            const boost::json::object& obj = jv.as_object();

            tcr.result = static_cast<uint8_t>(obj.at("result").as_int64());
            tcr.rsv = static_cast<uint32_t>(obj.at("rsv").as_int64());

            return tcr;
        }

        // Convert to string
        std::string to_string() const
        {
            std::ostringstream oss;
            oss << "timeCalibrationResponse { "
                << "result=" << static_cast<int>(result)
                << ", rsv=" << rsv
                << " }";
            return oss.str();
        }
    };

    struct diStatusNotification
    {
        uint32_t currDISignal;
        uint32_t prevDISignal;

        // Serialize to JSON
        boost::json::value to_json() const
        {
            boost::json::object obj;
            obj["currDISignal"] = currDISignal;
            obj["prevDISignal"] = prevDISignal;

            return obj;
        }

        // Deserialize from JSON
        static diStatusNotification from_json(const boost::json::value& jv)
        {
            diStatusNotification dsn;
            const boost::json::object& obj = jv.as_object();

            dsn.currDISignal = static_cast<uint32_t>(obj.at("currDISignal").as_int64());
            dsn.prevDISignal = static_cast<uint32_t>(obj.at("prevDISignal").as_int64());

            return dsn;
        }

        // Convert to string
        std::string to_string() const
        {
            std::ostringstream oss;
            oss << "diStatusNotification { "
                << "currDISignal=" << currDISignal
                << ", prevDISignal=" << prevDISignal
                << " }";
            return oss.str();
        }
    };

    struct obuInformationNotification
    {
        uint64_t rsv;
        uint64_t obulabel;
        uint8_t typeObu;
        uint16_t rsv1;
        uint32_t vcc;
        uint8_t rsv2;
        std::vector<uint8_t> vechicleNumber;
        uint32_t rsv3;
        uint8_t cardValidity;
        uint32_t rsv4;
        std::vector<uint8_t> can;
        uint32_t cardBalance;
        uint8_t backendAccount;
        uint8_t backendSetting;
        uint8_t businessFunctionStatus;
        uint8_t rsv5;

        // Serialize to JSON
        boost::json::value to_json() const
        {
            boost::json::object obj;
            obj["rsv"] = std::to_string(rsv);
            obj["obulabel"] = std::to_string(obulabel);
            obj["typeObu"] = typeObu;
            obj["rsv1"] = rsv1;
            obj["vcc"] = vcc;
            obj["rsv2"] = rsv2;

            boost::json::array vechicleNumberArray;
            for (auto v : vechicleNumber) vechicleNumberArray.push_back(v);
            obj["vechicleNumber"] = vechicleNumberArray;

            obj["rsv3"] = rsv3;
            obj["cardValidity"] = cardValidity;
            obj["rsv4"] = rsv4;

            boost::json::array canArray;
            for (auto c : can) canArray.push_back(c);
            obj["can"] = canArray;

            obj["cardBalance"] = cardBalance;
            obj["backendAccount"] = backendAccount;
            obj["backendSetting"] = backendSetting;
            obj["businessFunctionStatus"] = businessFunctionStatus;
            obj["rsv5"] = rsv5;

            return obj;
        }

        // Deserialize from JSON
        static obuInformationNotification from_json(const boost::json::value& jv)
        {
            obuInformationNotification obuin;
            const boost::json::object& obj = jv.as_object();

            obuin.rsv = std::stoull(obj.at("rsv").as_string().c_str());
            obuin.obulabel = std::stoull(obj.at("obulabel").as_string().c_str());
            obuin.typeObu = static_cast<uint8_t>(obj.at("typeObu").as_int64());
            obuin.rsv1 = static_cast<uint16_t>(obj.at("rsv1").as_int64());
            obuin.vcc = static_cast<uint32_t>(obj.at("vcc").as_int64());
            obuin.rsv2 = static_cast<uint8_t>(obj.at("rsv2").as_int64());

            const boost::json::array& vechicleNumberArray = obj.at("vechicleNumber").as_array();
            obuin.vechicleNumber.clear();
            for (auto& v : vechicleNumberArray)
                obuin.vechicleNumber.push_back(static_cast<uint8_t>(v.as_int64()));

            obuin.rsv3 = static_cast<uint32_t>(obj.at("rsv3").as_int64());
            obuin.cardValidity = static_cast<uint8_t>(obj.at("cardValidity").as_int64());
            obuin.rsv4 = static_cast<uint32_t>(obj.at("rsv4").as_int64());

            const boost::json::array& canArray = obj.at("can").as_array();
            obuin.can.clear();
            for (auto& c : canArray)
                obuin.can.push_back(static_cast<uint8_t>(c.as_int64()));

            obuin.cardBalance = static_cast<uint32_t>(obj.at("cardBalance").as_int64());
            obuin.backendAccount = static_cast<uint8_t>(obj.at("backendAccount").as_int64());
            obuin.backendSetting = static_cast<uint8_t>(obj.at("backendSetting").as_int64());
            obuin.businessFunctionStatus = static_cast<uint8_t>(obj.at("businessFunctionStatus").as_int64());
            obuin.rsv5 = static_cast<uint8_t>(obj.at("rsv5").as_int64());

            return obuin;
        }

        // Convert to string (debug-friendly)
        std::string to_string() const
        {
            auto vec_to_hex = [](const std::vector<uint8_t>& v) {
                std::ostringstream oss;
                oss << "[";
                for (size_t i = 0; i < v.size(); ++i)
                {
                    oss << std::hex << std::uppercase << static_cast<int>(v[i]);
                    if (i + 1 < v.size()) oss << " ";
                }
                oss << "]";
                return oss.str();
            };

            std::ostringstream oss;
            oss << "obuInformationNotification {"
                << " rsv=" << rsv
                << ", obulabel=" << obulabel
                << ", typeObu=" << static_cast<int>(typeObu)
                << ", rsv1=" << rsv1
                << ", vcc=" << vcc
                << ", rsv2=" << static_cast<int>(rsv2)
                << ", vechicleNumber=" << vec_to_hex(vechicleNumber)
                << ", rsv3=" << rsv3
                << ", cardValidity=" << static_cast<int>(cardValidity)
                << ", rsv4=" << rsv4
                << ", can=" << vec_to_hex(can)
                << ", cardBalance=" << cardBalance
                << ", backendAccount=" << static_cast<int>(backendAccount)
                << ", backendSetting=" << static_cast<int>(backendSetting)
                << ", businessFunctionStatus=" << static_cast<int>(businessFunctionStatus)
                << ", rsv5=" << static_cast<int>(rsv5)
                << " }";
            return oss.str();
        }
    };

    struct transactionData
    {
        uint16_t deductCommandSerialNum;
        uint8_t protocolVer;
        uint8_t resultDeduction;
        uint32_t subSystemLabel;
        uint64_t rsv;
        uint64_t obuLabel;
        uint32_t rsv1;
        std::vector<uint8_t> vechicleNumber;
        uint32_t rsv2;
        uint8_t transactionRoute;
        uint8_t rsv3;
        uint8_t frontendPaymentViolation;
        uint8_t backendPaymentViolation;
        uint8_t transactionType;
        uint8_t rsv4;
        uint8_t parkingStartDay;
        uint8_t parkingStartMonth;
        uint16_t parkingStartYear;
        uint8_t rsv5;
        uint8_t parkingStartSecond;
        uint8_t parkingStartMinute;
        uint8_t parkingStartHour;
        uint8_t parkingEndDay;
        uint8_t parkingEndMonth;
        uint16_t parkingEndYear;
        uint8_t rsv6;
        uint8_t parkingEndSecond;
        uint8_t parkingEndMinute;
        uint8_t parkingEndHour;
        uint32_t paymentFee;
        uint64_t fepTime;
        uint8_t rsv7;
        uint32_t trp;
        uint8_t indicationLastAutoLoad;
        uint32_t rsv8;
        std::vector<uint8_t> can;
        uint64_t lastCreditTransactionHeader;
        uint32_t lastCreditTransactionTRP;
        uint32_t purseBalanceBeforeTransaction;
        uint8_t badDebtCounter;
        uint8_t transactionStatus;
        uint8_t debitOption;
        uint8_t rsv9;
        uint32_t autoLoadAmount;
        uint64_t counterData;
        uint64_t signedCertificate;
        uint32_t purseBalanceAfterTransaction;
        uint8_t lastTransactionDebitOptionbyte;
        uint32_t rsv10;
        uint64_t previousTransactionHeader;
        uint32_t previousTRP;
        uint32_t previousPurseBalance;
        uint64_t previousCounterData;
        uint64_t previousTransactionSignedCertificate;
        uint8_t previousPurseStatus;
        uint32_t rsv11;
        uint16_t bepPaymentFeeAmount;
        uint16_t rsv12;
        std::vector<uint8_t> bepTimeOfReport;
        uint32_t rsv13;
        uint32_t chargeReportCounter;
        uint8_t bepKeyVersion;
        uint32_t rsv14;
        std::vector<uint8_t> bepCertificate;

        // Serialize to JSON
        boost::json::value to_json() const
        {
            boost::json::object obj;
            obj["deductCommandSerialNum"] = deductCommandSerialNum;
            obj["protocolVer"] = protocolVer;
            obj["resultDeduction"] = resultDeduction;
            obj["subSystemLabel"] = subSystemLabel;
            obj["rsv"] = std::to_string(rsv);
            obj["obuLabel"] = std::to_string(obuLabel);
            obj["rsv1"] = rsv1;

            boost::json::array vechicleNumberArray;
            for (auto v : vechicleNumber) vechicleNumberArray.push_back(v);
            obj["vechicleNumber"] = vechicleNumberArray;

            obj["rsv2"] = rsv2;
            obj["transactionRoute"] = transactionRoute;
            obj["rsv3"] = rsv3;
            obj["frontendPaymentViolation"] = frontendPaymentViolation;
            obj["backendPaymentViolation"] = backendPaymentViolation;
            obj["transactionType"] = transactionType;
            obj["rsv4"] = rsv4;
            obj["parkingStartDay"] = parkingStartDay;
            obj["parkingStartMonth"] = parkingStartMonth;
            obj["parkingStartYear"] = parkingStartYear;
            obj["rsv5"] = rsv5;
            obj["parkingStartSecond"] = parkingStartSecond;
            obj["parkingStartMinute"] = parkingStartMinute;
            obj["parkingStartHour"] = parkingStartHour;
            obj["parkingEndDay"] = parkingEndDay;
            obj["parkingEndMonth"] = parkingEndMonth;
            obj["parkingEndYear"] = parkingEndYear;
            obj["rsv6"] = rsv6;
            obj["parkingEndSecond"] = parkingEndSecond;
            obj["parkingEndMinute"] = parkingEndMinute;
            obj["parkingEndHour"] = parkingEndHour;
            obj["paymentFee"] = paymentFee;
            obj["fepTime"] = std::to_string(fepTime);
            obj["rsv7"] = rsv7;
            obj["trp"] = trp;
            obj["indicationLastAutoLoad"] = indicationLastAutoLoad;
            obj["rsv8"] = rsv8;

            boost::json::array canArray;
            for (auto c : can) canArray.push_back(c);
            obj["can"] = canArray;

            obj["lastCreditTransactionHeader"] = std::to_string(lastCreditTransactionHeader);
            obj["lastCreditTransactionTRP"] = lastCreditTransactionTRP;
            obj["purseBalanceBeforeTransaction"] = purseBalanceBeforeTransaction;
            obj["badDebtCounter"] = badDebtCounter;
            obj["transactionStatus"] = transactionStatus;
            obj["debitOption"] = debitOption;
            obj["rsv9"] = rsv9;
            obj["autoLoadAmount"] = autoLoadAmount;
            obj["counterData"] = std::to_string(counterData);
            obj["signedCertificate"] = std::to_string(signedCertificate);
            obj["purseBalanceAfterTransaction"] = purseBalanceAfterTransaction;
            obj["lastTransactionDebitOptionbyte"] = lastTransactionDebitOptionbyte;
            obj["rsv10"] = rsv10;
            obj["previousTransactionHeader"] = std::to_string(previousTransactionHeader);
            obj["previousTRP"] = previousTRP;
            obj["previousPurseBalance"] = previousPurseBalance;
            obj["previousCounterData"] = std::to_string(previousCounterData);
            obj["previousTransactionSignedCertificate"] = std::to_string(previousTransactionSignedCertificate);
            obj["previousPurseStatus"] = previousPurseStatus;
            obj["rsv11"] = rsv11;
            obj["bepPaymentFeeAmount"] = bepPaymentFeeAmount;
            obj["rsv12"] = rsv12;

            boost::json::array bepTimeOfReportArray;
            for (auto b : bepTimeOfReport) bepTimeOfReportArray.push_back(b);
            obj["bepTimeOfReport"] = bepTimeOfReportArray;

            obj["rsv13"] = rsv13;
            obj["chargeReportCounter"] = chargeReportCounter;
            obj["bepKeyVersion"] = bepKeyVersion;
            obj["rsv14"] = rsv14;

            boost::json::array bepCertificateArray;
            for (auto bc : bepCertificate) bepCertificateArray.push_back(bc);
            obj["bepCertificate"] = bepCertificateArray;

            return obj;
        }

        // Deserialize from JSON
        static transactionData from_json(const boost::json::value& jv)
        {
            transactionData td;
            const boost::json::object& obj = jv.as_object();

            td.deductCommandSerialNum = static_cast<uint16_t>(obj.at("deductCommandSerialNum").as_int64());
            td.protocolVer = static_cast<uint8_t>(obj.at("protocolVer").as_int64());
            td.resultDeduction = static_cast<uint8_t>(obj.at("resultDeduction").as_int64());
            td.subSystemLabel = static_cast<uint32_t>(obj.at("subSystemLabel").as_int64());
            td.rsv = std::stoull(obj.at("rsv").as_string().c_str());
            td.obuLabel = std::stoull(obj.at("obuLabel").as_string().c_str());
            td.rsv1 = static_cast<uint32_t>(obj.at("rsv1").as_int64());

            const boost::json::array& vechicleNumberArray = obj.at("vechicleNumber").as_array();
            td.vechicleNumber.clear();
            for (auto& v : vechicleNumberArray)
                td.vechicleNumber.push_back(static_cast<uint8_t>(v.as_int64()));

            td.rsv2 = static_cast<uint32_t>(obj.at("rsv2").as_int64());
            td.transactionRoute = static_cast<uint8_t>(obj.at("transactionRoute").as_int64());
            td.rsv3 = static_cast<uint8_t>(obj.at("rsv3").as_int64());
            td.frontendPaymentViolation = static_cast<uint8_t>(obj.at("frontendPaymentViolation").as_int64());
            td.backendPaymentViolation = static_cast<uint8_t>(obj.at("backendPaymentViolation").as_int64());
            td.transactionType = static_cast<uint8_t>(obj.at("transactionType").as_int64());
            td.rsv4 = static_cast<uint8_t>(obj.at("rsv4").as_int64());
            td.parkingStartDay = static_cast<uint8_t>(obj.at("parkingStartDay").as_int64());

            td.parkingStartMonth = static_cast<uint8_t>(obj.at("parkingStartMonth").as_int64());
            td.parkingStartYear = static_cast<uint16_t>(obj.at("parkingStartYear").as_int64());
            td.rsv5 = static_cast<uint8_t>(obj.at("rsv5").as_int64());
            td.parkingStartSecond = static_cast<uint8_t>(obj.at("parkingStartSecond").as_int64());
            td.parkingStartMinute = static_cast<uint8_t>(obj.at("parkingStartMinute").as_int64());
            td.parkingStartHour = static_cast<uint8_t>(obj.at("parkingStartHour").as_int64());
            td.parkingEndDay = static_cast<uint8_t>(obj.at("parkingEndDay").as_int64());
            td.parkingEndMonth = static_cast<uint8_t>(obj.at("parkingEndMonth").as_int64());
            td.parkingEndYear = static_cast<uint16_t>(obj.at("parkingEndYear").as_int64());
            td.rsv6 = static_cast<uint8_t>(obj.at("rsv6").as_int64());
            td.parkingEndSecond = static_cast<uint8_t>(obj.at("parkingEndSecond").as_int64());
            td.parkingEndMinute = static_cast<uint8_t>(obj.at("parkingEndMinute").as_int64());
            td.parkingEndHour = static_cast<uint8_t>(obj.at("parkingEndHour").as_int64());
            td.paymentFee = static_cast<uint32_t>(obj.at("paymentFee").as_int64());
            td.fepTime = std::stoull(obj.at("fepTime").as_string().c_str());
            td.rsv7 = static_cast<uint8_t>(obj.at("rsv7").as_int64());
            td.trp = static_cast<uint32_t>(obj.at("trp").as_int64());
            td.indicationLastAutoLoad = static_cast<uint8_t>(obj.at("indicationLastAutoLoad").as_int64());
            td.rsv8 = static_cast<uint32_t>(obj.at("rsv8").as_int64());

            const boost::json::array& canArray = obj.at("can").as_array();
            td.can.clear();
            for (auto& c : canArray)
                td.can.push_back(static_cast<uint8_t>(c.as_int64()));

            
            td.lastCreditTransactionHeader = std::stoull(obj.at("lastCreditTransactionHeader").as_string().c_str());
            td.lastCreditTransactionTRP = static_cast<uint32_t>(obj.at("lastCreditTransactionTRP").as_int64());
            td.purseBalanceBeforeTransaction = static_cast<uint32_t>(obj.at("purseBalanceBeforeTransaction").as_int64());
            td.badDebtCounter = static_cast<uint8_t>(obj.at("badDebtCounter").as_int64());
            td.transactionStatus = static_cast<uint8_t>(obj.at("transactionStatus").as_int64());
            td.debitOption = static_cast<uint8_t>(obj.at("debitOption").as_int64());

            td.rsv9 = static_cast<uint8_t>(obj.at("rsv9").as_int64());
            td.autoLoadAmount = static_cast<uint32_t>(obj.at("autoLoadAmount").as_int64());
            td.counterData = std::stoull(obj.at("counterData").as_string().c_str());
            td.signedCertificate = std::stoull(obj.at("signedCertificate").as_string().c_str());
            td.purseBalanceAfterTransaction = static_cast<uint32_t>(obj.at("purseBalanceAfterTransaction").as_int64());
            td.lastTransactionDebitOptionbyte = static_cast<uint8_t>(obj.at("lastTransactionDebitOptionbyte").as_int64());
            td.rsv10 = static_cast<uint32_t>(obj.at("rsv10").as_int64());
            td.previousTransactionHeader = std::stoull(obj.at("previousTransactionHeader").as_string().c_str());
            td.previousTRP = static_cast<uint32_t>(obj.at("previousTRP").as_int64());
            td.previousPurseBalance = static_cast<uint32_t>(obj.at("previousPurseBalance").as_int64());
            td.previousCounterData = std::stoull(obj.at("previousCounterData").as_string().c_str());
            td.previousTransactionSignedCertificate = std::stoull(obj.at("previousTransactionSignedCertificate").as_string().c_str());
            td.previousPurseStatus = static_cast<uint8_t>(obj.at("previousPurseStatus").as_int64());
            td.rsv11 = static_cast<uint32_t>(obj.at("rsv11").as_int64());
            td.bepPaymentFeeAmount = static_cast<uint16_t>(obj.at("bepPaymentFeeAmount").as_int64());
            td.rsv12 = static_cast<uint16_t>(obj.at("rsv12").as_int64());
            
            const boost::json::array& bepTimeOfReportArray = obj.at("bepTimeOfReport").as_array();
            td.bepTimeOfReport.clear();
            for (auto& b : bepTimeOfReportArray)
                td.bepTimeOfReport.push_back(static_cast<uint8_t>(b.as_int64()));

            td.rsv13 = static_cast<uint32_t>(obj.at("rsv13").as_int64());

            td.chargeReportCounter = static_cast<uint32_t>(obj.at("chargeReportCounter").as_int64());
            td.bepKeyVersion = static_cast<uint8_t>(obj.at("bepKeyVersion").as_int64());
            td.rsv14 = static_cast<uint32_t>(obj.at("rsv14").as_int64());
            
            const boost::json::array& bepCertificateArray = obj.at("bepCertificate").as_array();
            td.bepCertificate.clear();
            for (auto& bc : bepCertificateArray)
                td.bepCertificate.push_back(static_cast<uint8_t>(bc.as_int64()));

            return td;
        }

        // Convert to string (debug-friendly)
        std::string to_string() const
        {
            auto vec_to_hex = [](const std::vector<uint8_t>& v) {
                std::ostringstream oss;
                oss << "[";
                for (size_t i = 0; i < v.size(); ++i)
                {
                    oss << std::hex << std::uppercase << static_cast<int>(v[i]);
                    if (i + 1 < v.size()) oss << " ";
                }
                oss << "]";
                return oss.str();
            };

            std::ostringstream oss;
            oss << "transactionData {"
                << " deductCommandSerialNum=" << deductCommandSerialNum
                << ", protocolVer=" << static_cast<int>(protocolVer)
                << ", resultDeduction=" << static_cast<int>(resultDeduction)
                << ", subSystemLabel=" << subSystemLabel
                << ", rsv=" << rsv
                << ", obuLabel=" << obuLabel
                << ", rsv1=" << rsv1
                << ", vechicleNumber=" << vec_to_hex(vechicleNumber)
                << ", rsv2=" << rsv2
                << ", transactionRoute=" << static_cast<int>(transactionRoute)
                << ", rsv3=" << static_cast<int>(rsv3)
                << ", frontendPaymentViolation=" << static_cast<int>(frontendPaymentViolation)
                << ", backendPaymentViolation=" << static_cast<int>(backendPaymentViolation)
                << ", transactionType=" << static_cast<int>(transactionType)
                << ", rsv4=" << static_cast<int>(rsv4)
                << ", parkingStart=" << static_cast<int>(parkingStartDay) << "/"
                                    << static_cast<int>(parkingStartMonth) << "/"
                                    << parkingStartYear << " "
                                    << static_cast<int>(parkingStartHour) << ":"
                                    << static_cast<int>(parkingStartMinute) << ":"
                                    << static_cast<int>(parkingStartSecond)
                << ", parkingEnd=" << static_cast<int>(parkingEndDay) << "/"
                                << static_cast<int>(parkingEndMonth) << "/"
                                << parkingEndYear << " "
                                << static_cast<int>(parkingEndHour) << ":"
                                << static_cast<int>(parkingEndMinute) << ":"
                                << static_cast<int>(parkingEndSecond)
                << ", paymentFee=" << paymentFee
                << ", fepTime=" << fepTime
                << ", trp=" << trp
                << ", indicationLastAutoLoad=" << static_cast<int>(indicationLastAutoLoad)
                << ", can=" << vec_to_hex(can)
                << ", lastCreditTransactionHeader=" << lastCreditTransactionHeader
                << ", lastCreditTransactionTRP=" << lastCreditTransactionTRP
                << ", purseBalanceBeforeTransaction=" << purseBalanceBeforeTransaction
                << ", badDebtCounter=" << static_cast<int>(badDebtCounter)
                << ", transactionStatus=" << static_cast<int>(transactionStatus)
                << ", debitOption=" << static_cast<int>(debitOption)
                << ", autoLoadAmount=" << autoLoadAmount
                << ", counterData=" << counterData
                << ", signedCertificate=" << signedCertificate
                << ", purseBalanceAfterTransaction=" << purseBalanceAfterTransaction
                << ", lastTransactionDebitOptionbyte=" << static_cast<int>(lastTransactionDebitOptionbyte)
                << ", previousTransactionHeader=" << previousTransactionHeader
                << ", previousTRP=" << previousTRP
                << ", previousPurseBalance=" << previousPurseBalance
                << ", previousCounterData=" << previousCounterData
                << ", previousTransactionSignedCertificate=" << previousTransactionSignedCertificate
                << ", previousPurseStatus=" << static_cast<int>(previousPurseStatus)
                << ", bepPaymentFeeAmount=" << bepPaymentFeeAmount
                << ", bepTimeOfReport=" << vec_to_hex(bepTimeOfReport)
                << ", chargeReportCounter=" << chargeReportCounter
                << ", bepKeyVersion=" << static_cast<int>(bepKeyVersion)
                << ", bepCertificate=" << vec_to_hex(bepCertificate)
                << " }";
            return oss.str();
        }
    };

    struct cpoInformationDisplayResult
    {
        uint16_t sequenceNoOfRequestMsg;
        uint8_t result;
        uint32_t rsv_lsb;
        uint64_t rsv_msb;
        uint64_t obuLabel;
        uint32_t rsv1;

        // Serialize to JSON
        boost::json::value to_json() const
        {
            boost::json::object obj;
            obj["sequenceNoOfRequestMsg"] = sequenceNoOfRequestMsg;
            obj["result"] = result;
            obj["rsv_lsb"] = rsv_lsb;
            obj["rsv_msb"] = std::to_string(rsv_msb);
            obj["obuLabel"] = std::to_string(obuLabel);
            obj["rsv1"] = rsv1;

            return obj;
        }

        // Deserialize from JSON
        static cpoInformationDisplayResult from_json(const boost::json::value& jv)
        {
            cpoInformationDisplayResult cpoids;
            const boost::json::object& obj = jv.as_object();

            cpoids.sequenceNoOfRequestMsg = static_cast<uint16_t>(obj.at("sequenceNoOfRequestMsg").as_int64());
            cpoids.result = static_cast<uint8_t>(obj.at("result").as_int64());
            cpoids.rsv_lsb = static_cast<uint32_t>(obj.at("rsv_lsb").as_int64());
            cpoids.rsv_msb = std::stoull(obj.at("rsv_msb").as_string().c_str());
            cpoids.obuLabel = std::stoull(obj.at("obuLabel").as_string().c_str());
            cpoids.rsv1 = static_cast<uint32_t>(obj.at("rsv1").as_int64());

            return cpoids;
        }

         // Convert to string (debug-friendly)
        std::string to_string() const
        {
            std::ostringstream oss;
            oss << "cpoInformationDisplayResult {"
                << " sequenceNoOfRequestMsg=" << sequenceNoOfRequestMsg
                << ", result=" << static_cast<int>(result)
                << ", rsv_lsb=" << rsv_lsb
                << ", rsv_msb=" << rsv_msb
                << ", obuLabel=" << obuLabel
                << ", rsv1=" << rsv1
                << " }";
            return oss.str();
        }
    };

    struct carparkProcessCompleteResult
    {
        uint8_t result;
        uint32_t rsv_lsb;
        uint64_t rsv_msb;
        uint64_t obuLabel;
        uint32_t rsv1;

        // Serialize to JSON
        boost::json::value to_json() const
        {
            boost::json::object obj;
            obj["result"] = result;
            obj["rsv_lsb"] = rsv_lsb;
            obj["rsv_msb"] = std::to_string(rsv_msb);
            obj["obuLabel"] = std::to_string(obuLabel);
            obj["rsv1"] = rsv1;

            return obj;
        }

        // Deserialize from JSON
        static carparkProcessCompleteResult from_json(const boost::json::value& jv)
        {
            carparkProcessCompleteResult cpcr;
            const boost::json::object& obj = jv.as_object();

            cpcr.result = static_cast<uint8_t>(obj.at("result").as_int64());
            cpcr.rsv_lsb = static_cast<uint32_t>(obj.at("rsv_lsb").as_int64());
            cpcr.rsv_msb = std::stoull(obj.at("rsv_msb").as_string().c_str());
            cpcr.obuLabel = std::stoull(obj.at("obuLabel").as_string().c_str());
            cpcr.rsv1 = static_cast<uint32_t>(obj.at("rsv1").as_int64());

            return cpcr;
        }

        // Convert to string (debug-friendly)
        std::string to_string() const
        {
            std::ostringstream oss;
            oss << "carparkProcessCompleteResult {"
                << " result=" << static_cast<int>(result)
                << ", rsv_lsb=" << rsv_lsb
                << ", rsv_msb=" << rsv_msb
                << ", obuLabel=" << obuLabel
                << ", rsv1=" << rsv1
                << " }";
            return oss.str();
        }
    };

    struct eepRestartInquiry
    {
        uint8_t typeOfRestart;
        uint8_t responseDeadline;
        uint8_t maxRetryCount;
        uint8_t retryCounter;

        // Serialize to JSON
        boost::json::value to_json() const
        {
            boost::json::object obj;
            obj["typeOfRestart"] = typeOfRestart;
            obj["responseDeadline"] = responseDeadline;
            obj["maxRetryCount"] = maxRetryCount;
            obj["retryCounter"] = retryCounter;

            return obj;
        }

        // Deserialize from JSON
        static eepRestartInquiry from_json(const boost::json::value& jv)
        {
            eepRestartInquiry eepri;
            const boost::json::object& obj = jv.as_object();

            eepri.typeOfRestart = static_cast<uint8_t>(obj.at("typeOfRestart").as_int64());
            eepri.responseDeadline = static_cast<uint8_t>(obj.at("responseDeadline").as_int64());
            eepri.maxRetryCount = static_cast<uint8_t>(obj.at("maxRetryCount").as_int64());
            eepri.retryCounter = static_cast<uint8_t>(obj.at("retryCounter").as_int64());

            return eepri;
        }

        // Convert to string
        std::string to_string() const
        {
            std::ostringstream oss;
            oss << "eepRestartInquiry {"
                << " typeOfRestart=" << static_cast<int>(typeOfRestart)
                << ", responseDeadline=" << static_cast<int>(responseDeadline)
                << ", maxRetryCount=" << static_cast<int>(maxRetryCount)
                << ", retryCounter=" << static_cast<int>(retryCounter)
                << " }";
            return oss.str();
        }
    };

    struct notificationLog
    {
        uint8_t day;
        uint8_t month;
        uint16_t year;
        uint8_t rsv;
        uint8_t second;
        uint8_t minute;
        uint8_t hour;
        uint8_t notificationType;
        uint8_t errorCode;
        uint16_t rsv1;

        // Serialize to JSON
        boost::json::value to_json() const
        {
            boost::json::object obj;
            obj["day"] = day;
            obj["month"] = month;
            obj["year"] = year;
            obj["rsv"] = rsv;
            obj["second"] = second;
            obj["minute"] = minute;
            obj["hour"] = hour;
            obj["notificationType"] = notificationType;
            obj["errorCode"] = errorCode;
            obj["rsv1"] = rsv1;

            return obj;
        }

        // Deserialize from JSON
        static notificationLog from_json(const boost::json::value& jv)
        {
            notificationLog nl;
            const boost::json::object& obj = jv.as_object();

            nl.day = static_cast<uint8_t>(obj.at("day").as_int64());
            nl.month = static_cast<uint8_t>(obj.at("month").as_int64());
            nl.year = static_cast<uint16_t>(obj.at("year").as_int64());
            nl.rsv = static_cast<uint8_t>(obj.at("rsv").as_int64());
            nl.second = static_cast<uint8_t>(obj.at("second").as_int64());
            nl.minute = static_cast<uint8_t>(obj.at("minute").as_int64());
            nl.hour = static_cast<uint8_t>(obj.at("hour").as_int64());
            nl.notificationType = static_cast<uint8_t>(obj.at("notificationType").as_int64());
            nl.errorCode = static_cast<uint8_t>(obj.at("errorCode").as_int64());
            nl.rsv1 = static_cast<uint16_t>(obj.at("rsv1").as_int64());

            return nl;
        }

        // Convert to printable string
        std::string to_string() const
        {
            std::ostringstream oss;
            oss << "notificationLog {"
                << " day=" << static_cast<int>(day)
                << ", month=" << static_cast<int>(month)
                << ", year=" << year
                << ", time=" << static_cast<int>(hour) << ":"
                            << static_cast<int>(minute) << ":"
                            << static_cast<int>(second)
                << ", notificationType=" << static_cast<int>(notificationType)
                << ", errorCode=" << static_cast<int>(errorCode)
                << ", rsv=" << static_cast<int>(rsv)
                << ", rsv1=" << rsv1
                << " }";
            return oss.str();
        }
    };

    static EEPClient* getInstance();
    void FnEEPClientInit(const std::string& serverIP, unsigned short serverPort, const std::string& stationID);
    void FnSendAck(uint16_t seqNo_, uint8_t reqDataTypeCode_);
    void FnSendNak(uint16_t seqNo_, uint8_t reqDataTypeCode_, uint8_t reasonCode_);
    void FnSendHealthStatusReq();
    void FnSendWatchdogReq();
    void FnSendStartReq();
    void FnSendStopReq();
    void FnSendDIReq();
    void FnSendDOReq(uint8_t do1, uint8_t do2, uint8_t do3, uint8_t do4, uint8_t do5, uint8_t do6);
    void FnSendSetDIPortConfigReq(uint16_t periodDebounceDi1_, uint16_t periodDebounceDi2_, uint16_t periodDebounceDi3_, uint16_t periodDebounceDi4_, uint16_t periodDebounceDi5_);
    void FnSendGetOBUInfoReq();
    void FnSendGetOBUInfoStopReq();
    void FnSendDeductReq(const std::string& obuLabel_, const std::string& fee_, const std::string& entryTime_, const std::string& exitTime_);
    void FnSendDeductStopReq(const std::string& obuLabel_);
    void FnSendTransactionReq(const std::string& obuLabel_);
    void FnSendCPOInfoDisplayReq(const std::string& obuLabel_, const std::string& dataType_, const std::string& line1_, const std::string& line2_, const std::string& line3_, const std::string& line4_, const std::string& line5_);
    void FnSendCarparkProcessCompleteNotificationReq(const std::string& obuLabel_, const std::string& processingResult_, const std::string& fee);
    void FnSendDSRCProcessCompleteNotificationReq(const std::string& obuLabel_);
    void FnSendStopReqOfRelatedInfoDistributionReq(const std::string& obuLabel_);
    void FnSendDSRCStatusReq();
    void FnSendTimeCalibrationReq();
    void FnSendSetCarparkAvailabilityReq(const std::string& availLots_, const std::string& totalLots_);
    void FnSendCDDownloadReq();
    void FnSendRestartInquiryResponseReq(uint8_t response);
    void FnEEPClientClose();

    /**
     * Singleton EEPClient should not be cloneable
     */
    EEPClient(EEPClient& eep) = delete;

    /**
     * Singleton EEPClient should not be assignable
     */
    void operator=(const EEPClient&) = delete;

private:
    static EEPClient* eepClient_;
    static std::mutex mutex_;
    boost::asio::thread_pool filePool_;  // background worker threads
    boost::asio::io_context ioContext_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::steady_timer reconnectTimer_;
    boost::asio::steady_timer connectTimer_;
    boost::asio::steady_timer sendTimer_;
    boost::asio::steady_timer responseTimer_;
    boost::asio::steady_timer ackTimer_;
    boost::asio::steady_timer watchdogTimer_;
    boost::asio::steady_timer healthStatusTimer_;
    std::unique_ptr<AppTcpClient> client_;
    std::string logFileName_;
    std::thread ioContextThread_;
    static const StateTransition stateTransitionTable[static_cast<int>(STATE::STATE_COUNT)];
    STATE currentState_;
    std::mutex cmdQueueMutex_;
    static std::mutex currentCmdMutex_;
    static std::mutex currentCmdRequestedMutex_;
    std::priority_queue<Command, std::vector<Command>, CompareCommand> commandQueue_;
    uint64_t commandSequence_;
    Command currentCmd;
    static uint16_t sequenceNo_;
    static std::mutex sequenceNoMutex_;
    int iStationID_;
    std::string serverIP_;
    unsigned short serverPort_;
    int eepSourceId_;
    int eepDestinationId_;
    int eepCarparkID_;
    static uint16_t deductCmdSerialNo_;
    static uint16_t lastDeductCmdSerialNo_;
    static std::mutex deductCmdSerialNoMutex_;
    int watchdogMissedRspCount_;
    bool lastConnectionState_;
    EEPClient();
    void startIoContextThread();
    void handleConnect(bool success, const std::string& message);
    void handleSend(bool success, const std::string& message);
    void handleClose(bool success, const std::string& message);
    void handleReceivedData(bool success, const std::vector<uint8_t>& data);
    void eepClientConnect();
    void eepClientSend(const std::vector<uint8_t>& message);
    void eepClientClose();
    std::string messageCodeToString(MESSAGE_CODE code);
    std::string eventToString(EVENT event);
    std::string stateToString(STATE state);
    void processEvent(EVENT event);
    void checkCommandQueue();
    void enqueueCommand(CommandType type, int priority, std::shared_ptr<CommandDataBase> data);
    void popFromCommandQueueAndEnqueueWrite();
    void clearCommandQueue();
    std::string getCommandString(CommandType cmd);
    void setCurrentCmd(Command cmd);
    Command getCurrentCmd();
    void setCurrentCmdRequested(Command cmd);
    Command getCurrentCmdRequested();
    void incrementSequenceNo();
    uint16_t getSequenceNo();
    void incrementDeductCmdSerialNo();
    uint16_t getLastDeductCmdSerialNo();
    uint16_t getDeductCmdSerialNo();
    void appendMessageHeader(std::vector<uint8_t>& msg, uint8_t messageCode, uint16_t seqNo, uint16_t length);
    uint32_t calculateChecksumNoPadding(const std::vector<uint8_t>& data);
    std::pair<std::vector<uint8_t>, bool> prepareCmd(Command cmd);
    void startReconnectTimer();
    void startConnectTimer();
    void handleConnectTimerTimeout(const boost::system::error_code& error);
    void startSendTimer();
    void handleSendTimerTimeout(const boost::system::error_code& error);
    void startResponseTimer();
    void handleResponseTimeout(const boost::system::error_code& error);
    void startAckTimer();
    void handleAckTimeout(const boost::system::error_code& error);
    void startWatchdogTimer();
    void handleWatchdogTimeout(const boost::system::error_code& error);
    void startHealthStatusTimer();
    void handleHealthStatusTimeout(const boost::system::error_code& error);
    void handleIdleState(EVENT event);
    void handleConnectingState(EVENT event);
    void handleConnectedState(EVENT event);
    void handleWritingRequestState(EVENT event);
    void handleWaitingForResponseState(EVENT event);
    void handleCommandErrorOrTimeout(Command cmd, MSG_STATUS msgStatus);
    bool isValidCheckSum(const std::vector<uint8_t>& data);
    bool parseMessage(const std::vector<uint8_t>& data, MessageHeader& header, std::vector<uint8_t>& body);
    void printField(std::ostringstream& oss, const std::string& label, uint64_t value, int hexWidth, const std::string& remark);
    void printFieldHex(std::ostringstream& oss, const std::string& label, uint64_t value, int hexWidth, const std::string& remark);
    void printFieldChar(std::ostringstream& oss, const std::string& label, const std::vector<uint8_t>& data, const std::string& remark);
    void printFieldHexChar(std::ostringstream& oss, const std::string& label, const std::vector<uint8_t>& data, const std::string& remark);
    bool isValidSourceDestination(uint8_t source, uint8_t destination);
    std::string getFieldDescription(uint8_t value, const std::unordered_map<uint8_t, std::string>& map);
    void showParsedMessage(const MessageHeader& header, const std::vector<uint8_t>& body);
    void handleParsedResponseMessage(const MessageHeader& header, const std::vector<uint8_t>& body, std::string& eventMsg);
    void handleParsedNotificationMessage(const MessageHeader& header, const std::vector<uint8_t>& body, std::string& eventMsg);
    void handleInvalidMessage(const std::vector<uint8_t>& data, uint8_t reasonCode_);
    bool isResponseMatchedDataTypeCode(Command cmd, const uint8_t& dataTypeCode_, const std::vector<uint8_t>& msgBody);
    bool isNotificationReceived(const uint8_t& dataTypeCode_);
    bool doesCmdRequireNotification(Command cmd);
    bool isResponseComplete(Command cmd, const uint8_t& dataTypeCode_);
    bool isResponseNotificationComplete(Command cmd, const uint8_t& dataTypeCode_);
    void notifyConnectionState(bool connected);

    // DSRC transaction record write to file
    void processDSRCFeTx(const MessageHeader& header, const transactionData& txData);
    void processDSRCBeTx(const MessageHeader& header, const transactionData& txData);
    void writeDSRCFeOrBeTxToCollFile(bool isFrontendTx, const std::vector<uint8_t>& data);
    void copyAndRemoveBEFile(const std::string& settlementfilepath);
};