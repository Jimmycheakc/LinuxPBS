#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/mount.h>

#include <cstring>
#include <iostream>
#include <string>
#include <memory>
#include <chrono>
#include <thread>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <utility>
#include <future>
#include <cstdio>
#include <map>
#include <charconv>
#include <cctype>
#include <optional>
#include <string_view>
#include <vector>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>

#if defined(__linux__)
#include <pthread.h>
#endif
#include "common.h"
#include "gpio.h"
#include "operation.h"
#include "ini_parser.h"
#include "structuredata.h"
#include "parsedata.h"
#include "db.h"
#include "led.h"
#include "lcd.h"
#include "log.h"
#include "udp.h"
#include "udp_protocol.h"
#include "antenna.h"
#include "lcsc.h"
#include "dio.h"
#include "ksm_reader.h"
#include "lpr.h"
#include "printer.h"
#include "upt.h"
#include "barcode_reader.h"
#include "boost/algorithm/string.hpp"
#include "eep_client.h"
#include "chu_client.h"
#include "ping.h"
#include "shutdown_manager.h"
#include "mount.h"


namespace
{

std::string_view trimUdpField(std::string_view text) noexcept
{
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.front())) != 0)
    {
        text.remove_prefix(1);
    }

    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.back())) != 0)
    {
        text.remove_suffix(1);
    }

    return text;
}

std::optional<int> parseUdpInt(std::string_view text) noexcept
{
    text = trimUdpField(text);

    if (text.empty())
    {
        return std::nullopt;
    }

    int value{};

    const auto [ptr, ec] =
        std::from_chars(text.data(), text.data() + text.size(), value);

    if (ec != std::errc{} || ptr != text.data() + text.size())
    {
        return std::nullopt;
    }

    return value;
}

std::vector<std::string> splitUdpCsv(std::string_view text)
{
    std::vector<std::string> tokens;
    std::size_t tokenBegin = 0;

    while (tokenBegin <= text.size())
    {
        const std::size_t separatorPos = text.find(',', tokenBegin);

        if (separatorPos == std::string_view::npos)
        {
            tokens.emplace_back(text.substr(tokenBegin));
            break;
        }

        tokens.emplace_back(text.substr(tokenBegin, separatorPos - tokenBegin));
        tokenBegin = separatorPos + 1;

        if (tokenBegin == text.size())
        {
            tokens.emplace_back();
            break;
        }
    }

    return tokens;
}

void logInvalidUdpPacket(const std::string& reason)
{
    Logger::getInstance()->FnLog("[RX] Invalid packet | " + reason, "", "UDP");
}

void logReceivedUdpPacket(std::string_view packet)
{
    Logger::getInstance()->FnLog("Received data:" + std::string(packet), "", "UDP");
}

template <typename T>
T* getOperationEventData(OperationEvent& event)
{
    return std::get_if<T>(&event.data);
}

template <typename T>
const T* getOperationEventData(const OperationEvent& event)
{
    return std::get_if<T>(&event.data);
}

void logInvalidOperationEventData(OperationEventType type)
{
    std::stringstream ss;
    ss << "operation::handleEventOnIo | Invalid payload for event type " << static_cast<int>(type);
    Logger::getInstance()->FnLogExceptionError(ss.str());
}

std::string_view dioEventToString(DIO::DIO_EVENT event)
{
    switch (event)
    {
        case DIO::DIO_EVENT::LOOP_A_ON_EVENT:                 return "LOOP_A_ON";
        case DIO::DIO_EVENT::LOOP_A_OFF_EVENT:                return "LOOP_A_OFF";
        case DIO::DIO_EVENT::LOOP_B_ON_EVENT:                 return "LOOP_B_ON";
        case DIO::DIO_EVENT::LOOP_B_OFF_EVENT:                return "LOOP_B_OFF";
        case DIO::DIO_EVENT::LOOP_C_ON_EVENT:                 return "LOOP_C_ON";
        case DIO::DIO_EVENT::LOOP_C_OFF_EVENT:                return "LOOP_C_OFF";
        case DIO::DIO_EVENT::INTERCOM_ON_EVENT:               return "INTERCOM_ON";
        case DIO::DIO_EVENT::INTERCOM_OFF_EVENT:              return "INTERCOM_OFF";
        case DIO::DIO_EVENT::STATION_DOOR_OPEN_EVENT:         return "STATION_DOOR_OPEN";
        case DIO::DIO_EVENT::STATION_DOOR_CLOSE_EVENT:        return "STATION_DOOR_CLOSE";
        case DIO::DIO_EVENT::BARRIER_DOOR_OPEN_EVENT:         return "BARRIER_DOOR_OPEN";
        case DIO::DIO_EVENT::BARRIER_DOOR_CLOSE_EVENT:        return "BARRIER_DOOR_CLOSE";
        case DIO::DIO_EVENT::BARRIER_STATUS_ON_EVENT:         return "BARRIER_STATUS_ON";
        case DIO::DIO_EVENT::BARRIER_STATUS_OFF_EVENT:        return "BARRIER_STATUS_OFF";
        case DIO::DIO_EVENT::MANUAL_OPEN_BARRIED_ON_EVENT:    return "MANUAL_OPEN_BARRIER_ON";
        case DIO::DIO_EVENT::MANUAL_OPEN_BARRIED_OFF_EVENT:   return "MANUAL_OPEN_BARRIER_OFF";
        case DIO::DIO_EVENT::LORRY_SENSOR_ON_EVENT:           return "LORRY_SENSOR_ON";
        case DIO::DIO_EVENT::LORRY_SENSOR_OFF_EVENT:          return "LORRY_SENSOR_OFF";
        case DIO::DIO_EVENT::ARM_BROKEN_ON_EVENT:             return "ARM_BROKEN_ON";
        case DIO::DIO_EVENT::ARM_BROKEN_OFF_EVENT:            return "ARM_BROKEN_OFF";
        case DIO::DIO_EVENT::PRINT_RECEIPT_ON_EVENT:          return "PRINT_RECEIPT_ON";
        case DIO::DIO_EVENT::PRINT_RECEIPT_OFF_EVENT:         return "PRINT_RECEIPT_OFF";
        case DIO::DIO_EVENT::BARRIER_OPEN_TOO_LONG_ON_EVENT:  return "BARRIER_OPEN_TOO_LONG_ON";
        case DIO::DIO_EVENT::BARRIER_OPEN_TOO_LONG_OFF_EVENT: return "BARRIER_OPEN_TOO_LONG_OFF";
    }

    return "UNKNOWN";
}

} // namespace


operation::operation()
{
    isOperationInitialized_.store(false);
    lastActionTimeAfterLoopA_ = std::chrono::steady_clock::now();
}

operation::~operation()
{
    // Normal shutdown must be performed through FnClose(). This destructor is
    // only an emergency/static-destruction fallback.
    stopping_.store(true);
    workGuard_.reset();
    ioContext_.stop();

    if (ioContextThread_.joinable() &&
        ioContextThread_.get_id() != std::this_thread::get_id())
    {
        ioContextThread_.join();
    }

    pLCDIdleTimer_.reset();
    pLoopATimer_.reset();
    pDailyProcessTimer_.reset();

    monitorUdpClient_.reset();
    pmsUdpClient_.reset();
}

operation* operation::getInstance()
{
    static operation instance;
    return &instance;
}

bool operation::postEvent(std::function<void()> handler)
{
    if (!handler || stopping_.load() || !running_.load())
    {
        return false;
    }

    // Internal OP_IO callers may execute immediately. External callers are
    // always serialized by posting to Operation's single io_context thread.
    if (ioContext_.get_executor().running_in_this_thread())
    {
        if (!stopping_.load())
        {
            handler();
            return true;
        }

        return false;
    }

    boost::asio::post(
        ioContext_,
        [this, handler = std::move(handler)]() mutable
        {
            if (stopping_.load())
            {
                return;
            }

            handler();
        });

    return true;
}

bool operation::dispatchSharedDataUpdate(std::function<void()> handler)
{
    if (!handler || stopping_.load() || !running_.load())
    {
        return false;
    }

    boost::asio::dispatch(
        ioContext_,
        [this, handler = std::move(handler)]() mutable
        {
            if (stopping_.load())
            {
                return;
            }

            handler();
        });

    return true;
}

std::optional<OperationSharedData> operation::FnGetSharedData()
{
    // Never wait for OP_IO from OP_IO itself.
    if (ioContext_.get_executor().running_in_this_thread())
    {
        return makeSharedDataSnapshotOnIo();
    }

    // Serialize against FnOperationInit()/FnClose() so the context cannot be
    // torn down while this synchronous snapshot request is waiting.
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);

    if (!running_.load() || stopping_.load())
    {
        return std::nullopt;
    }

    auto snapshotTask =
        std::make_shared<std::packaged_task<OperationSharedData()>>(
            [this]()
            {
                return makeSharedDataSnapshotOnIo();
            });

    auto snapshotFuture = snapshotTask->get_future();

    boost::asio::dispatch(
        ioContext_,
        [snapshotTask]() mutable
        {
            (*snapshotTask)();
        });

    try
    {
        return snapshotFuture.get();
    }
    catch (const std::exception& e)
    {
        Logger::getInstance()->FnLogExceptionError(std::string("operation::FnGetSharedData | Exception: ") + e.what());
    }
    catch (...)
    {
        Logger::getInstance()->FnLogExceptionError("operation::FnGetSharedData | Unknown exception");
    }

    return std::nullopt;
}

bool operation::FnGetLoopAPresent() const
{
    return tProcess.gbLoopApresent.load();
}

void operation::FnSetLoopAPresent(bool present)
{
    tProcess.gbLoopApresent.store(present);
}

bool operation::FnUpdateStation(std::function<void(tstation_struct&)> modifier)
{
    if (!modifier)
    {
        return false;
    }

    return dispatchSharedDataUpdate(
        [this, modifier = std::move(modifier)]() mutable
        {
            modifier(gtStation);
        });
}

bool operation::FnUpdateEntry(std::function<void(tEntryTrans_Struct&)> modifier)
{
    if (!modifier)
    {
        return false;
    }

    return dispatchSharedDataUpdate(
        [this, modifier = std::move(modifier)]() mutable
        {
            modifier(tEntry);
        });
}

bool operation::FnUpdateExit(std::function<void(tExitTrans_Struct&)> modifier)
{
    if (!modifier)
    {
        return false;
    }

    return dispatchSharedDataUpdate(
        [this, modifier = std::move(modifier)]() mutable
        {
            modifier(tExit);
        });
}

bool operation::FnUpdateExit1(std::function<void(tExitTrans_Struct&)> modifier)
{
    if (!modifier)
    {
        return false;
    }

    return dispatchSharedDataUpdate(
        [this, modifier = std::move(modifier)]() mutable
        {
            modifier(tExit1);
        });
}

bool operation::FnUpdateProcess(std::function<void(tProcess_Struct&)> modifier)
{
    if (!modifier)
    {
        return false;
    }

    return dispatchSharedDataUpdate(
        [this, modifier = std::move(modifier)]() mutable
        {
            modifier(tProcess);
        });
}

bool operation::FnUpdateParas(std::function<void(tParas_Struct&)> modifier)
{
    if (!modifier)
    {
        return false;
    }

    return dispatchSharedDataUpdate(
        [this, modifier = std::move(modifier)]() mutable
        {
            modifier(tParas);
        });
}

bool operation::FnUpdateMessage(std::function<void(tMsg_Struct&)> modifier)
{
    if (!modifier)
    {
        return false;
    }

    return dispatchSharedDataUpdate(
        [this, modifier = std::move(modifier)]() mutable
        {
            modifier(tMsg);
        });
}

bool operation::FnUpdateExitMessage(std::function<void(tExitMsg_struct&)> modifier)
{
    if (!modifier)
    {
        return false;
    }

    return dispatchSharedDataUpdate(
        [this, modifier = std::move(modifier)]() mutable
        {
            modifier(tExitMsg);
        });
}

bool operation::FnUpdateSeason(std::function<void(tseason_struct&)> modifier)
{
    if (!modifier)
    {
        return false;
    }

    return dispatchSharedDataUpdate(
        [this, modifier = std::move(modifier)]() mutable
        {
            modifier(tSeason);
        });
}

bool operation::FnUpdateVehicleTypes(std::function<void(std::vector<tVType_Struct>&)> modifier)
{
    if (!modifier)
    {
        return false;
    }

    return dispatchSharedDataUpdate(
        [this, modifier = std::move(modifier)]() mutable
        {
            modifier(tVType);
        });
}

bool operation::FnUpdateTR(std::function<void(std::vector<tTR_struc>&)> modifier)
{
    if (!modifier)
    {
        return false;
    }

    return dispatchSharedDataUpdate(
        [this, modifier = std::move(modifier)]() mutable
        {
            modifier(tTR);
        });
}

OperationSharedData operation::makeSharedDataSnapshotOnIo() const
{
    OperationSharedData data;

    data.isOperationInitialized = isOperationInitialized_.load();
    data.gtStation = gtStation;
    data.tEntry = tEntry;
    data.tExit = tExit;
    data.tExit1 = tExit1;
    data.tProcess = tProcess;
    data.tParas = tParas;
    data.tMsg = tMsg;
    data.tExitMsg = tExitMsg;
    data.tSeason = tSeason;
    data.tVType = tVType;
    data.tTR = tTR;

    for (std::size_t i = 0; i < data.tPBSError.size(); ++i)
    {
        data.tPBSError[i] = tPBSError[i];
    }

    return data;
}

bool operation::FnOnEvent(OperationEvent event)
{
    return postEvent(
        [this, event = std::move(event)]() mutable
        {
            handleEventOnIo(std::move(event));
        });
}

void operation::handleEventOnIo(OperationEvent event)
{
    // This function always runs on OP_IO. EventHandler has already copied the
    // short-lived BaseEvent payload into OperationEvent.
    switch (event.type)
    {
        // -----------------------------------------------------
        // Antenna
        // -----------------------------------------------------
        case OperationEventType::AntennaFail:
        {
            const auto* value = getOperationEventData<int>(event);
            if (value == nullptr) { logInvalidOperationEventData(event.type); break; }
            handleAntennaFailOnIo(*value);
            break;
        }

        case OperationEventType::AntennaPower:
        {
            const auto* value = getOperationEventData<bool>(event);
            if (value == nullptr) { logInvalidOperationEventData(event.type); break; }
            handleAntennaPowerOnIo(*value);
            break;
        }

        case OperationEventType::AntennaIUCome:
        {
            auto* value = getOperationEventData<std::string>(event);
            if (value == nullptr) { logInvalidOperationEventData(event.type); break; }
            handleAntennaIUComeOnIo(std::move(*value));
            break;
        }

        // -----------------------------------------------------
        // LCSC
        // -----------------------------------------------------
        case OperationEventType::LcscReaderStatus:
        case OperationEventType::LcscReaderGetCardID:
        case OperationEventType::LcscReaderGetCardBalance:
        case OperationEventType::LcscReaderGetCardDeduct:
        case OperationEventType::LcscReaderGetCardRecord:
        case OperationEventType::LcscReaderGetCardFlush:
        {
            const auto* value = getOperationEventData<std::string>(event);
            if (value == nullptr) { logInvalidOperationEventData(event.type); break; }
            ProcessLCSC(*value);
            break;
        }

        case OperationEventType::LcscReaderLogin:
        case OperationEventType::LcscReaderLogout:
        case OperationEventType::LcscReaderGetTime:
        case OperationEventType::LcscReaderSetTime:
        case OperationEventType::LcscReaderUploadCFGFile:
        case OperationEventType::LcscReaderUploadCILFile:
        case OperationEventType::LcscReaderUploadBLFile:
        {
            // These events were log-only in the previous EventHandler design.
            // No Operation business action is currently required.
            if (getOperationEventData<std::string>(event) == nullptr)
            {
                logInvalidOperationEventData(event.type);
            }
            break;
        }

        // -----------------------------------------------------
        // DIO
        // -----------------------------------------------------
        case OperationEventType::DioEvent:
        {
            const auto* value = getOperationEventData<int>(event);
            if (value == nullptr) { logInvalidOperationEventData(event.type); break; }
            handleDioEventOnIo(*value);
            break;
        }

        // -----------------------------------------------------
        // KSM Reader
        // -----------------------------------------------------
        case OperationEventType::KsmReaderInit:
        {
            const auto* success = getOperationEventData<bool>(event);
            if (success == nullptr) { logInvalidOperationEventData(event.type); break; }
            handleKsmReaderInitOnIo(*success);
            break;
        }

        case OperationEventType::KsmReaderGetStatus:
        {
            const auto* success = getOperationEventData<bool>(event);
            if (success == nullptr) { logInvalidOperationEventData(event.type); break; }
            handleKsmResultOnIo(*success, "KSM Reader Get Status", KsmFailureAction::EnableError);
            break;
        }

        case OperationEventType::KsmReaderEjectToFront:
        {
            const auto* success = getOperationEventData<bool>(event);
            if (success == nullptr) { logInvalidOperationEventData(event.type); break; }
            handleKsmResultOnIo(*success, "KSM Reader Eject To Front", KsmFailureAction::EnableError);
            break;
        }

        case OperationEventType::KsmReaderCardAllowed:
        {
            const auto* success = getOperationEventData<bool>(event);
            if (success == nullptr) { logInvalidOperationEventData(event.type); break; }
            handleKsmResultOnIo(*success, "KSM Reader Card Allowed", KsmFailureAction::EnableError);
            break;
        }

        case OperationEventType::KsmReaderCardProhibited:
        {
            const auto* success = getOperationEventData<bool>(event);
            if (success == nullptr) { logInvalidOperationEventData(event.type); break; }
            handleKsmResultOnIo(*success, "KSM Reader Card Prohibited", KsmFailureAction::EnableError);
            break;
        }

        case OperationEventType::KsmReaderCardOnIc:
        {
            const auto* success = getOperationEventData<bool>(event);
            if (success == nullptr) { logInvalidOperationEventData(event.type); break; }
            handleKsmResultOnIo(*success, "KSM Reader Card On Ic", KsmFailureAction::CardReadError);
            break;
        }

        case OperationEventType::KsmReaderIcPowerOn:
        {
            const auto* success = getOperationEventData<bool>(event);
            if (success == nullptr) { logInvalidOperationEventData(event.type); break; }
            handleKsmResultOnIo(*success, "KSM Reader Ic Power On", KsmFailureAction::CardReadError);
            break;
        }

        case OperationEventType::KsmReaderWarmReset:
        {
            const auto* success = getOperationEventData<bool>(event);
            if (success == nullptr) { logInvalidOperationEventData(event.type); break; }
            handleKsmResultOnIo(*success, "KSM Reader Warm Reset", KsmFailureAction::CardReadError);
            break;
        }

        case OperationEventType::KsmReaderSelectFile1:
        {
            const auto* success = getOperationEventData<bool>(event);
            if (success == nullptr) { logInvalidOperationEventData(event.type); break; }
            handleKsmResultOnIo(*success, "KSM Reader Select File 1", KsmFailureAction::CardReadError);
            break;
        }

        case OperationEventType::KsmReaderSelectFile2:
        {
            const auto* success = getOperationEventData<bool>(event);
            if (success == nullptr) { logInvalidOperationEventData(event.type); break; }
            handleKsmResultOnIo(*success, "KSM Reader Select File 2", KsmFailureAction::CardReadError);
            break;
        }

        case OperationEventType::KsmReaderReadCardInfo:
        {
            const auto* success = getOperationEventData<bool>(event);
            if (success == nullptr) { logInvalidOperationEventData(event.type); break; }
            handleKsmResultOnIo(*success, "KSM Reader Read Card Info", KsmFailureAction::CardReadError);
            break;
        }

        case OperationEventType::KsmReaderReadCardBalance:
        {
            const auto* success = getOperationEventData<bool>(event);
            if (success == nullptr) { logInvalidOperationEventData(event.type); break; }
            handleKsmResultOnIo(*success, "KSM Reader Read Card Balance", KsmFailureAction::CardReadError);
            break;
        }

        case OperationEventType::KsmReaderIcPowerOff:
        {
            const auto* success = getOperationEventData<bool>(event);
            if (success == nullptr) { logInvalidOperationEventData(event.type); break; }
            handleKsmResultOnIo(*success, "KSM Reader Ic Power Off", KsmFailureAction::CardReadError);
            break;
        }

        case OperationEventType::KsmReaderCardIn:
        {
            if (getOperationEventData<bool>(event) == nullptr)
            {
                logInvalidOperationEventData(event.type);
                break;
            }

            // Preserve previous behavior: the completion flag was ignored.
            KSM_CardIn();
            break;
        }

        case OperationEventType::KsmReaderCardOut:
        {
            if (getOperationEventData<bool>(event) == nullptr)
            {
                logInvalidOperationEventData(event.type);
                break;
            }

            writelog("card out", "OPR");
            KSM_Reader::getInstance()->FnKSMReaderStartGetStatus();
            break;
        }

        case OperationEventType::KsmReaderCardTakeAway:
        {
            if (getOperationEventData<bool>(event) == nullptr)
            {
                logInvalidOperationEventData(event.type);
                break;
            }

            KSM_CardTakeAway();
            break;
        }

        case OperationEventType::KsmReaderCardInfo:
        {
            if (getOperationEventData<bool>(event) == nullptr)
            {
                logInvalidOperationEventData(event.type);
                break;
            }

            const std::string cardNo = KSM_Reader::getInstance()->FnKSMReaderGetCardNum();
            const long cardBalance = KSM_Reader::getInstance()->FnKSMReaderGetCardBalance();
            const bool cardExpired = KSM_Reader::getInstance()->FnKSMReaderGetCardExpired();

            KSM_CardInfo(cardNo, cardBalance, cardExpired);
            break;
        }

        // -----------------------------------------------------
        // LPR
        // -----------------------------------------------------
        case OperationEventType::LprReceive:
        {
            auto* value = getOperationEventData<std::string>(event);
            if (value == nullptr) { logInvalidOperationEventData(event.type); break; }
            handleLprReceiveOnIo(std::move(*value));
            break;
        }

        // -----------------------------------------------------
        // UPT
        // -----------------------------------------------------
        case OperationEventType::UptCardDetect:
        case OperationEventType::UptPaymentAuto:
        case OperationEventType::UptDeviceSettlement:
        case OperationEventType::UptRetrieveLastSettlement:
        case OperationEventType::UptDeviceLogon:
        case OperationEventType::UptDeviceStatus:
        case OperationEventType::UptDeviceTimeSync:
        case OperationEventType::UptDeviceTMS:
        case OperationEventType::UptDeviceReset:
        case OperationEventType::UptCommandCancel:
        {
            const auto* value = getOperationEventData<std::string>(event);
            if (value == nullptr) { logInvalidOperationEventData(event.type); break; }

            Upt::UPT_CMD command{};

            switch (event.type)
            {
                case OperationEventType::UptCardDetect:
                    command = Upt::UPT_CMD::CARD_DETECT_REQUEST;
                    break;
                case OperationEventType::UptPaymentAuto:
                    command = Upt::UPT_CMD::PAYMENT_MODE_AUTO_REQUEST;
                    break;
                case OperationEventType::UptDeviceSettlement:
                    command = Upt::UPT_CMD::DEVICE_SETTLEMENT_REQUEST;
                    break;
                case OperationEventType::UptRetrieveLastSettlement:
                    command = Upt::UPT_CMD::DEVICE_RETRIEVE_LAST_SETTLEMENT_REQUEST;
                    break;
                case OperationEventType::UptDeviceLogon:
                    command = Upt::UPT_CMD::DEVICE_LOGON_REQUEST;
                    break;
                case OperationEventType::UptDeviceStatus:
                    command = Upt::UPT_CMD::DEVICE_STATUS_REQUEST;
                    break;
                case OperationEventType::UptDeviceTimeSync:
                    command = Upt::UPT_CMD::DEVICE_TIME_SYNC_REQUEST;
                    break;
                case OperationEventType::UptDeviceTMS:
                    command = Upt::UPT_CMD::DEVICE_TMS_REQUEST;
                    break;
                case OperationEventType::UptDeviceReset:
                    command = Upt::UPT_CMD::DEVICE_RESET_REQUEST;
                    break;
                case OperationEventType::UptCommandCancel:
                    command = Upt::UPT_CMD::CANCEL_COMMAND_REQUEST;
                    break;
                default:
                    break;
            }

            processUPT(command, *value);
            break;
        }

        // -----------------------------------------------------
        // Printer / Barcode
        // -----------------------------------------------------
        case OperationEventType::PrinterStatus:
        {
            const auto* value = getOperationEventData<int>(event);
            if (value == nullptr) { logInvalidOperationEventData(event.type); break; }
            handlePrinterStatusOnIo(*value);
            break;
        }

        case OperationEventType::BarcodeReceived:
        {
            auto* value = getOperationEventData<std::string>(event);
            if (value == nullptr) { logInvalidOperationEventData(event.type); break; }
            ProcessBarcodeData(std::move(*value));
            break;
        }

        // -----------------------------------------------------
        // EEP
        // -----------------------------------------------------
        case OperationEventType::EepClientResponse:
        {
            const auto* value = getOperationEventData<std::string>(event);
            if (value == nullptr) { logInvalidOperationEventData(event.type); break; }
            processEEP(*value);
            break;
        }

        case OperationEventType::EepClientConnectionState:
        {
            const auto* value = getOperationEventData<bool>(event);
            if (value == nullptr) { logInvalidOperationEventData(event.type); break; }
            handleEepConnectionStateOnIo(*value);
            break;
        }

        // -----------------------------------------------------
        // CHU
        // -----------------------------------------------------
        case OperationEventType::ChuReceived:
        {
            const auto* value = getOperationEventData<std::string>(event);
            if (value == nullptr) { logInvalidOperationEventData(event.type); break; }
            processCHU(*value);
            break;
        }

        case OperationEventType::ChuClientConnectionState:
        {
            const auto* value = getOperationEventData<std::string>(event);
            if (value == nullptr) { logInvalidOperationEventData(event.type); break; }
            handleChuConnectionStateOnIo(*value);
            break;
        }
    }
}

void operation::handleKsmResultOnIo(
    bool success,
    const char* description,
    KsmFailureAction failureAction)
{
    writelog(std::string(description) + (success ? " : Ok." : " : Error."), "OPR");

    if (success)
    {
        return;
    }

    switch (failureAction)
    {
        case KsmFailureAction::EnableError:
            handleKSM_EnableError();
            break;

        case KsmFailureAction::CardReadError:
            handleKSM_CardReadError();
            break;
    }
}

void operation::handleLprReceiveOnIo(std::string eventData)
{
    const Lpr::LPREventData parsedEvent = Lpr::getInstance()->deserializeEventData(eventData);

    std::stringstream ss;
    ss << "[OP_IO] LPR Receive"
       << " | camType : " << static_cast<int>(parsedEvent.camType)
       << ", LPN : " << parsedEvent.LPN
       << ", TransID : " << parsedEvent.TransID
       << ", imagePath : " << parsedEvent.imagePath;

    writelog(ss.str(), "OPR");

    ReceivedLPR(
        parsedEvent.camType,
        parsedEvent.LPN,
        parsedEvent.TransID,
        parsedEvent.imagePath);
}

bool operation::FnOnPmsUdpPacket(std::string senderIp, std::string packet)
{
    if (packet.empty())
    {
        return false;
    }

    return postEvent(
        [this,
         senderIp = std::move(senderIp),
         packet = std::move(packet)]()
        {
            processPmsUdpPacketOnIo(senderIp, packet);
        });
}

bool operation::FnOnMonitorUdpPacket(std::string senderIp, std::string packet)
{
    if (packet.empty())
    {
        return false;
    }

    return postEvent(
        [this,
         senderIp = std::move(senderIp),
         packet = std::move(packet)]()
        {
            processMonitorUdpPacketOnIo(senderIp, packet);
        });
}



void operation::processMonitorUdpPacketOnIo(const std::string& senderIp, const std::string& packet)
{
    if (senderIp == tParas.gsLocalIP)
    {
        return;
    }

	if (packet.empty())
    {
        return;
    }

	try
    {
		// ParseData accepts std::string_view, so the UDP datagram can be
        // parsed directly without requiring a null terminator or temporary
        // std::string copy. Parsed fields are owned by ParseData.
        ParseData fields('[', ']', '|');
        const std::size_t fieldCount = fields.Parse(packet);

		if (fieldCount < 4)
        {
            logInvalidUdpPacket("Monitor field count < 4");
            return;
        }

		const auto commandValue = parseUdpInt(fields.FieldView(2));

		if (!commandValue)
        {
            logInvalidUdpPacket("Invalid monitor command");
            return;
        }

        const auto command = static_cast<MonitorUdpRxCommand>(*commandValue);

		switch (command)
        {
            case MonitorUdpRxCommand::MonitorStatus:
            {
                logReceivedUdpPacket(packet);

                const auto status = parseUdpInt(fields.FieldView(3));
                if (!status)
                {
                    logInvalidUdpPacket("Invalid monitor status");
                    break;
                }

                if (monitorUdpClient_ != nullptr)
                {
                    monitorUdpClient_->FnSetMonitorStatus(*status == 1);
                }
                break;
            }

            case MonitorUdpRxCommand::MonitorEnquiry:
            {
                logReceivedUdpPacket(packet);
                sendMyStatusToMonitor();
                break;
            }

            case MonitorUdpRxCommand::MonitorFeeTest:
            {
                logReceivedUdpPacket(packet);
                break;
            }

            case MonitorUdpRxCommand::MonitorOutput:
            {
                logReceivedUdpPacket(packet);

                const auto tokens = splitUdpCsv(fields.FieldView(3));
                if (tokens.size() != 2)
                {
                    logInvalidUdpPacket("Monitor output requires pin,value");
                    break;
                }

                const auto pinNumber = parseUdpInt(tokens[0]);
                const auto pinValue = parseUdpInt(tokens[1]);

                if (!pinNumber || !pinValue || (*pinValue != 0 && *pinValue != 1))
                {
                    writelog("Invalid DIO", "UDP");
                    break;
                }

                const int actualPin = DIO::getInstance()->FnGetOutputPinNum(*pinNumber);

                if (actualPin == 0)
                {
                    writelog("Invalid DIO", "UDP");
                    break;
                }

                auto* gpio = GPIOManager::getInstance()->FnGetGPIO(actualPin);

                if (gpio == nullptr)
                {
                    writelog("Nullptr, Invalid DIO", "UDP");
                    break;
                }

                gpio->FnSetValue(*pinValue);
                break;
            }

            case MonitorUdpRxCommand::DownloadIni:
            {
                logReceivedUdpPacket(packet);
                writelog("download INI file", "UDP");

                const bool success = CopyIniFile(fields.Field(0), fields.Field(3));

                sendCmdDownloadIniAckToMonitor(success);
                break;
            }

            case MonitorUdpRxCommand::DownloadParam:
            {
                logReceivedUdpPacket(packet);
                writelog("download Parameter", "UDP");

                db::getInstance()->downloadparameter();

                if (db::getInstance()->FnGetDatabaseErrorFlag() == 0)
                {
                    db::getInstance()->loadParam();
                    sendCmdDownloadParamAckToMonitor(true);
                }
                else
                {
                    sendCmdDownloadParamAckToMonitor(false);
                }

                break;
            }

            case MonitorUdpRxCommand::MonitorSyncTime:
            {
                logReceivedUdpPacket(packet);
                syncCentralDBTime();
                break;
            }

            case MonitorUdpRxCommand::StopStationSoftware:
            {
                logReceivedUdpPacket(packet);
                SendMsg2Monitor("11", "99");

                // Use the application shutdown path instead of std::exit().
                ShutdownManager::getInstance()->FnRequestShutdown();
                break;
            }

            case MonitorUdpRxCommand::MonitorStationVersion:
            {
                logReceivedUdpPacket(packet);
                SendMsg2Monitor("313", SW_VERSION);
                break;
            }

            case MonitorUdpRxCommand::MonitorGetStationCurrLog:
            {
                logReceivedUdpPacket(packet);
                sendCmdGetStationCurrLogToMonitor();
                break;
            }

            default:
                break;
        }
	}
	catch (const std::exception& e)
    {
        Logger::getInstance()->FnLogExceptionError(std::string("operation::processMonitorUdpPacketOnIo | Exception: ") + e.what());
    }
    catch (...)
    {
        Logger::getInstance()->FnLogExceptionError("operation::processMonitorUdpPacketOnIo | Unknown exception");
    }
}

void operation::processPmsUdpPacketOnIo(const std::string& senderIp, const std::string& packet)
{
    if (senderIp == tParas.gsLocalIP)
    {
        return;
    }

    if (packet.empty())
    {
        return;
    }

    try
    {
        ParseData fields('[', ']', '|');
        const std::size_t fieldCount = fields.Parse(packet);

        if (fieldCount < 4)
        {
            logInvalidUdpPacket("PMS field count < 4");
            return;
        }

        const auto commandValue = parseUdpInt(fields.FieldView(2));

        if (!commandValue)
        {
            logInvalidUdpPacket("Invalid PMS command");
            return;
        }

        const auto command = static_cast<UdpRxCommand>(*commandValue);

        switch (command)
        {
            case UdpRxCommand::StopStationSoftware:
            {
                logReceivedUdpPacket(packet);
                SendMsg2Server("09", "11Stopping...");
                writelog("Exit by PMS", "UDP");
                ShutdownManager::getInstance()->FnRequestShutdown();
                break;
            }

            case UdpRxCommand::StatusEnquiry:
            {
                logReceivedUdpPacket(packet);
                Sendmystatus();
                break;
            }

            case UdpRxCommand::StatusOnline:
            {
                logReceivedUdpPacket(packet);

                if (tProcess.giSystemOnline != 0)
                {
                    std::stringstream stream;
                    stream
                        << "Status: "
                        << (tProcess.giSystemOnline == 0
                                ? "Online"
                                : "Offline");

                    writelog(stream.str(), "UDP");

                    tProcess.giSystemOnline = 0;

                    if (tProcess.glNoofOfflineData > 0)
                    {
                        db::getInstance()->moveOfflineTransToCentral();
                    }
                }

                SendMsg2Server("99", "");
                break;
            }

            case UdpRxCommand::UpdateSeason:
            {
                logReceivedUdpPacket(packet);

                const int result = db::getInstance()->downloadseason();
                if (result > 0)
                {
                    std::stringstream stream;
                    stream << "Download " << result << " Season";
                    SendMsg2Server("99", stream.str());
                }
                break;
            }

            case UdpRxCommand::DownloadMsg:
            {
                logReceivedUdpPacket(packet);
                writelog("download LED message", "UDP");

                const int result = db::getInstance()->downloadledmessage();
                if (result > 0)
                {
                    std::stringstream stream;
                    stream << "Download " << result << " Messages";
                    SendMsg2Server("99", stream.str());
                }

                db::getInstance()->loadmessage();
                db::getInstance()->loadExitmessage();

                if (!tProcess.gbcarparkfull)
                {
                    if (gtStation.iType == tientry)
                    {
                        tProcess.IdleMsg[0] = tMsg.Msg_DefaultLED[0];
                        tProcess.IdleMsg[1] = tMsg.Msg_Idle[1];
                    }
                    else
                    {
                        tProcess.IdleMsg[0] = tExitMsg.MsgExit_XDefaultLED[0];
                        tProcess.IdleMsg[1] = tExitMsg.MsgExit_XIdle[1];
                    }
                }
                break;
            }

            case UdpRxCommand::FeeTest:
            {
                logReceivedUdpPacket(packet);
                writelog("Fee test command", "UDP");

                const auto tokens = splitUdpCsv(fields.FieldView(3));
                if (tokens.size() < 3)
                {
                    logInvalidUdpPacket("Fee test requires at least 3 values");
                    break;
                }

                const auto feeType = parseUdpInt(tokens[2]);
                if (!feeType)
                {
                    logInvalidUdpPacket("Fee test has invalid fee type");
                    break;
                }

                const float parkingFee = db::getInstance()->CalFeeRAM2G(tokens[0], tokens[1], *feeType);

                if (parkingFee >= 0)
                {
                    std::string response = fields.Field(3);
                    response += "," + Common::getInstance()->SetFeeFormat(parkingFee) + ", Fee OK";

                    SendMsg2Server("302", response);
                }
                break;
            }

            case UdpRxCommand::DownloadXTariff:
            {
                logReceivedUdpPacket(packet);
                writelog("download XTariff", "UDP");

                const int result = db::getInstance()->downloadxtariff( tParas.giGroupID, tParas.giSite, 0);

                if (result > 0)
                {
                    std::stringstream stream;
                    stream << "Download " << result << " XTariff";
                    SendMsg2Server("99", stream.str());
                    db::getInstance()->LoadXTariff();
                }
                break;
            }

            case UdpRxCommand::DownloadTariff:
            {
                logReceivedUdpPacket(packet);

                writelog("download Tariff Type Info", "UDP");

                int result = db::getInstance()->downloadtarifftypeinfo();
                if (result > 0)
                {
                    db::getInstance()->LoadTariffTypeInfo();
                }

                writelog("download Tariff", "UDP");

                result = db::getInstance()->downloadtariffsetup(tParas.giGroupID, tParas.giSite, 0);

                if (result > 0)
                {
                    std::stringstream stream;
                    stream << "Download " << result << " Tariff";
                    SendMsg2Server("99", stream.str());
                    db::getInstance()->LoadTariff();
                }
                break;
            }

            case UdpRxCommand::DownloadHoliday:
            {
                logReceivedUdpPacket(packet);
                writelog("download Holiday", "UDP");

                const int result = db::getInstance()->downloadholidaymst(1);
                if (result > 0)
                {
                    std::stringstream stream;
                    stream << "Download " << result << " Holiday";
                    SendMsg2Server("99", stream.str());
                }

                db::getInstance()->LoadHoliday();
                break;
            }

            case UdpRxCommand::UpdateParam:
            {
                logReceivedUdpPacket(packet);
                writelog("download Parameter", "UDP");

                const int result = db::getInstance()->downloadparameter();
                if (result > 0)
                {
                    std::stringstream stream;
                    stream << "Download " << result << " Parameter";
                    SendMsg2Server("99", stream.str());
                }

                db::getInstance()->loadParam();
                break;
            }

            case UdpRxCommand::DownloadType:
            {
                logReceivedUdpPacket(packet);
                writelog("download Vehicle Type", "UDP");

                const int result = db::getInstance()->downloadvehicletype();
                if (result > 0)
                {
                    std::stringstream stream;
                    stream << "Download " << result << " Vehicle Type";
                    SendMsg2Server("99", stream.str());
                }

                db::getInstance()->loadvehicletype();
                break;
            }

            case UdpRxCommand::OpenBarrier:
            {
                logReceivedUdpPacket(packet);
                writelog("open barrier from PMS", "UDP");
                ManualOpenBarrier(true);
                break;
            }

            case UdpRxCommand::CloseBarrier:
            {
                logReceivedUdpPacket(packet);
                writelog("Close barrier from PMS", "UDP");

                ManualCloseBarrier();

                if (db::getInstance()->writeparameter2local("LockBarrier", "0") == 0)
                {
                    writelog("Update the parameter 'LockBarrier' successfully", "UDP");
                    tParas.gbLockBarrier = false;
                }

                SendMsg2Server("99", "Close Barrier");
                break;
            }

            case UdpRxCommand::ContinueOpenBarrier:
            {
                logReceivedUdpPacket(packet);
                writelog("Continue open barrier from PMS", "UDP");

                continueOpenBarrier();

                if (db::getInstance()->writeparameter2local("LockBarrier", "1") == 0)
                {
                    writelog("Update the parameter 'LockBarrier' successfully", "UDP");
                    tParas.gbLockBarrier = true;
                }

                SendMsg2Server("99", "Continue Open Barrier");
                break;
            }

            case UdpRxCommand::SetTime:
            {
                logReceivedUdpPacket(packet);
                syncCentralDBTime();
                break;
            }

            case UdpRxCommand::CarparkFull:
            {
                logReceivedUdpPacket(packet);

                const auto rawValue = parseUdpInt(fields.FieldView(3));
                if (!rawValue)
                {
                    logInvalidUdpPacket("Invalid carpark-full value");
                    break;
                }

                const bool carparkFull = (*rawValue != 0);

                if (carparkFull != tProcess.gbcarparkfull)
                {
                    tProcess.gbcarparkfull = carparkFull;

                    if (!carparkFull)
                    {
                        const std::string iuNumber = tEntry.sIUTKNo;

                        if (tProcess.gbLoopApresent && !iuNumber.empty())
                        {
                            PBSEntry(iuNumber);
                        }

                        if (gtStation.iType == tientry)
                        {
                            tProcess.IdleMsg[0] = tMsg.Msg_DefaultLED[0];
                            tProcess.IdleMsg[1] = tMsg.Msg_Idle[1];
                        }
                        else
                        {
                            tProcess.IdleMsg[0] = tExitMsg.MsgExit_XDefaultLED[0];
                            tProcess.IdleMsg[1] = tExitMsg.MsgExit_XIdle[1];
                        }
                    }
                    else
                    {
                        tProcess.IdleMsg[0] = tMsg.Msg_CarParkFull2LED[0];
                        tProcess.IdleMsg[1] = tMsg.Msg_CarParkFull2LED[1];
                    }
                }
                break;
            }

            case UdpRxCommand::ClearSeason:
            {
                logReceivedUdpPacket(packet);
                writelog("Clear Local season.", "UDP");
                db::getInstance()->clearseason();
                break;
            }

            case UdpRxCommand::UpdateSetting:
            {
                logReceivedUdpPacket(packet);
                writelog("download station set up", "UDP");

                const int result = db::getInstance()->downloadstationsetup();
                if (result > 0)
                {
                    std::stringstream stream;
                    stream << "Download " << result << " Station Setup";
                    SendMsg2Server("99", stream.str());
                }

                db::getInstance()->loadstationsetup();
                break;
            }

            case UdpRxCommand::DownloadTR:
            {
                logReceivedUdpPacket(packet);
                writelog("download TR", "UDP");

                const int result = db::getInstance()->downloadTR();
                if (result > 0)
                {
                    std::stringstream stream;
                    stream << "Download " << result << " TR";
                    SendMsg2Server("99", stream.str());
                }

                db::getInstance()->loadTR();
                break;
            }

            case UdpRxCommand::TimeForNoEntry:
            {
                logReceivedUdpPacket(packet);

                if (tProcess.gbLoopApresent)
                {
                    const auto tokens = splitUdpCsv(fields.FieldView(3));
                    if (tokens.size() < 2)
                    {
                        logInvalidUdpPacket("Time-for-no-entry requires at least 2 values");
                        break;
                    }

                    tExit.sEntryTime = tokens[1];
                    writelog("Received Entry time: " + tExit.sEntryTime + " from PMS.", "UDP");

                    tExit.bNoEntryRecord = 0;
                    ReceivedEntryRecord();
                }
                else
                {
                    writelog("No Vehicle on the Loop.", "DB");
                }
                break;
            }

            case UdpRxCommand::SetLotCount:
                break;

            case UdpRxCommand::AvailableLots:
            {
                logReceivedUdpPacket(packet);
                ShowTotalLots(fields.Field(3));
                break;
            }

            case UdpRxCommand::SetDioOutput:
            {
                logReceivedUdpPacket(packet);

                // This command accesses Field(4), unlike most 4-field
                // commands, so validate the extra field explicitly.
                if (fieldCount < 5)
                {
                    logInvalidUdpPacket("Set-DIO-output requires 5 fields");
                    break;
                }

                const auto pinNumber = parseUdpInt(fields.FieldView(3));
                const auto pinValue = parseUdpInt(fields.FieldView(4));

                if (!pinNumber || !pinValue || (*pinValue != 0 && *pinValue != 1))
                {
                    writelog("Invalid DIO", "UDP");
                    break;
                }

                const int actualPin = DIO::getInstance()->FnGetOutputPinNum(*pinNumber);

                if (actualPin == 0)
                {
                    writelog("Invalid DIO", "UDP");
                    break;
                }

                auto* gpio = GPIOManager::getInstance()->FnGetGPIO(actualPin);

                if (gpio == nullptr)
                {
                    writelog("Nullptr, Invalid DIO", "UDP");
                    break;
                }

                gpio->FnSetValue(*pinValue);
                break;
            }

            case UdpRxCommand::BroadcastSaveTrans:
            {
                const std::string fieldData = fields.Field(3);

                if (fieldData.find("Entry OK") != std::string::npos)
                {
                    logReceivedUdpPacket(packet);

                    const std::string stationId = "," + fields.Field(1) + ",";

                    const std::string zoneEntries = tParas.gsZoneEntries;

                    if (zoneEntries.find(stationId) != std::string::npos)
                    {
                        const auto tokens = splitUdpCsv(fieldData);
                        if (tokens.empty())
                        {
                            logInvalidUdpPacket("Entry broadcast has no data");
                            break;
                        }

                        db::getInstance()->insertbroadcasttrans(fields.Field(1), tokens[0]);
                    }
                }
                else if (fieldData.find("Exit OK") != std::string::npos)
                {
                    logReceivedUdpPacket(packet);

                    const auto tokens = splitUdpCsv(fieldData);
                    if (tokens.empty())
                    {
                        logInvalidUdpPacket("Exit broadcast has no data");
                        break;
                    }

                    db::getInstance()->UpdateLocalEntry(tokens[0]);
                }
                break;
            }

            case UdpRxCommand::EEPStatus:
            {
                logReceivedUdpPacket(packet);
                SendMsg2Server("801", EEPClient::getInstance()->FnGetStatusData());
                break;
            }

            default:
                break;
        }
    }
    catch (const std::exception& e)
    {
        Logger::getInstance()->FnLogExceptionError(std::string("operation::processPmsUdpPacketOnIo | Exception: ") + e.what());
    }
    catch (...)
    {
        Logger::getInstance()->FnLogExceptionError("operation::processPmsUdpPacketOnIo | Unknown exception");
    }
}

void operation::handleAntennaFailOnIo(int errorCode)
{
    if (errorCode == 2 && tProcess.gbLoopApresent)
    {
        if (tProcess.sEnableReader == false)
        {
            writelog("No IU detected!", "OPR");
            ShowLEDMsg("No IU Detected!^Insert/Tap Card", "No IU Detected!^Insert/Tap Card");
            EnableCashcard(true);
        }

        return;
    }

    if (tProcess.gbLoopApresent)
    {
        HandlePBSError(AntennaError, errorCode);
        Antenna::getInstance()->FnAntennaStopRead();

        writelog("No IU detected! Antenna Error", "OPR");

        ShowLEDMsg("Antenna Error!^Insert/Tap Card", "Antenna Error!^Insert/Tap Card");

        EnableCashcard(true);
    }
}

void operation::handleAntennaPowerOnIo(bool poweredOn)
{
    HandlePBSError(AntennaPowerOnOff, poweredOn ? 1 : 0);
}

void operation::handleAntennaIUComeOnIo(std::string iuNo)
{
    const auto sameAsLastIUDuration =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() -
            tProcess.lastIUEntryTime);

    if (iuNo.length() == 10 &&
        tProcess.gsLastIUNo == iuNo &&
        sameAsLastIUDuration.count() <= tParas.giMaxTransInterval &&
        gtStation.iType == tientry)
    {
        std::stringstream ss;
        ss << "Same as last IU, duration :"
           << sameAsLastIUDuration.count()
           << " less than Maximum interval: "
           << tParas.giMaxTransInterval;

        writelog(ss.str(), "OPR");

        ShowLEDMsg("Same as last IU^Please Proceed", "Same as last IU^Please Proceed");

        Openbarrier();
    }
    else
    {
        VehicleCome(std::move(iuNo));
    }

    if (tPBSError[iAntenna].ErrNo != 0)
    {
        HandlePBSError(AntennaNoError);
    }
}

void operation::handleDioEventOnIo(int eventValue)
{
    const auto dioEvent = static_cast<DIO::DIO_EVENT>(eventValue);

    // Log every received DIO event once.
    {
        std::stringstream ss;
        ss << "DIO Event::"
           << dioEventToString(dioEvent)
           << "(" << eventValue << ")";

       // writelog(ss.str(), "OPR");
    }

    auto updateBarrierStatus =
        [this](int errorCode, const char* message)
        {
            HandlePBSError(BarrierStatus, errorCode);
            db::getInstance()->AddSysEvent(message);
        };

    switch (dioEvent)
    {
        case DIO::DIO_EVENT::LOOP_A_ON_EVENT:
            LoopACome();
            break;

        case DIO::DIO_EVENT::LOOP_A_OFF_EVENT:
            LoopAGone();
            break;

        case DIO::DIO_EVENT::LOOP_C_ON_EVENT:
            LoopCCome();
            break;

        case DIO::DIO_EVENT::LOOP_C_OFF_EVENT:
            LoopCGone();
            break;

        case DIO::DIO_EVENT::STATION_DOOR_OPEN_EVENT:
            HandlePBSError(SDoorError);
            break;

        case DIO::DIO_EVENT::STATION_DOOR_CLOSE_EVENT:
            HandlePBSError(SDoorNoError);
            break;

        case DIO::DIO_EVENT::BARRIER_DOOR_OPEN_EVENT:
            HandlePBSError(BDoorError);
            break;

        case DIO::DIO_EVENT::BARRIER_DOOR_CLOSE_EVENT:
            HandlePBSError(BDoorNoError);
            break;

        case DIO::DIO_EVENT::BARRIER_STATUS_ON_EVENT:
        {
            if (tProcess.gbBarrierOpened)
            {
                break;
            }

            if (DIO::getInstance()->FnGetManualOpenBarrierStatusFlag() == 1)
            {
                DIO::getInstance()->FnSetManualOpenBarrierStatusFlag(0);
            }

            db::getInstance()->AddSysEvent("Barrier up");
            ManualOpenBarrier(false);
            break;
        }

        case DIO::DIO_EVENT::MANUAL_OPEN_BARRIED_ON_EVENT:
            writelog("Open barrier action(by operator)", "OPR");
            break;

        case DIO::DIO_EVENT::ARM_BROKEN_ON_EVENT:
            updateBarrierStatus(3, "Arm failure detected.");
            break;

        case DIO::DIO_EVENT::ARM_BROKEN_OFF_EVENT:
            updateBarrierStatus(0, "Arm recovered successfully.");
            break;

        case DIO::DIO_EVENT::PRINT_RECEIPT_ON_EVENT:
            tExit.iflag4Receipt = 1;
            PrintReceipt();
            break;

        case DIO::DIO_EVENT::BARRIER_OPEN_TOO_LONG_ON_EVENT:
            updateBarrierStatus(2, "Barrier open too long detected.");
            break;

        case DIO::DIO_EVENT::BARRIER_OPEN_TOO_LONG_OFF_EVENT:
            updateBarrierStatus(0, "Barrier open too long - closed successfully.");
            break;

        // Currently no business action required.
        case DIO::DIO_EVENT::LOOP_B_ON_EVENT:
        case DIO::DIO_EVENT::LOOP_B_OFF_EVENT:
        case DIO::DIO_EVENT::INTERCOM_ON_EVENT:
        case DIO::DIO_EVENT::INTERCOM_OFF_EVENT:
        case DIO::DIO_EVENT::BARRIER_STATUS_OFF_EVENT:
        case DIO::DIO_EVENT::MANUAL_OPEN_BARRIED_OFF_EVENT:
        case DIO::DIO_EVENT::LORRY_SENSOR_ON_EVENT:
        case DIO::DIO_EVENT::LORRY_SENSOR_OFF_EVENT:
        case DIO::DIO_EVENT::PRINT_RECEIPT_OFF_EVENT:
        default:
            break;
    }
}

void operation::handleKsmReaderInitOnIo(bool success)
{
    writelog(success ? "KSM Reader Init : Ok." : "KSM Reader Init : Error.", "OPR");

    if (!success)
    {
        handleKSM_EnableError();
        return;
    }

    HandlePBSError(ReaderNoError);
}

void operation::handlePrinterStatusOnIo(int status)
{
    switch (status)
    {
        case 0:
            HandlePBSError(PrinterNoError);
            break;

        case 1:
            HandlePBSError(PrinterNoPaper);
            break;

        case -1:
        default:
            HandlePBSError(PrinterError);
            break;
    }
}

void operation::handleEepConnectionStateOnIo(bool connected)
{
    if (connected)
    {
        if (tPBSError[0].ErrNo == -1)
        {
            tPBSError[0].ErrNo = 0;
            Sendmystatus();

            writelog("DSRC is connected!", "OPR");
        }

        return;
    }

    if (tPBSError[0].ErrNo == 0)
    {
        tPBSError[0].ErrNo = -1;
        Sendmystatus();

        writelog("DSRC connection is lost!", "OPR");
    }
}

void operation::handleChuConnectionStateOnIo(const std::string& status)
{
    if (status == "connected")
    {
        if (tPBSError[8].ErrNo == -1)
        {
            tPBSError[8].ErrNo = 0;
            Sendmystatus();

            writelog("CHU Gateway is connected!", "OPR");
        }

        return;
    }

    if (tPBSError[8].ErrNo == 0)
    {
        tPBSError[8].ErrNo = -1;
        Sendmystatus();

        writelog("CHU Gateway connection is lost", "OPR");
    }
}

bool operation::startIoContextThread()
{
    if (ioContextThread_.joinable())
    {
        return true;
    }

    try
    {
        ioContextThread_ =
            std::thread(
                [this]()
                {
#if defined(__linux__)
                    ::pthread_setname_np(::pthread_self(), "OP_IO");
#endif
                    ioContext_.run();
                });

        running_.store(true);
        return true;
    }
    catch (const std::exception& e)
    {
        writelog(std::string("OPERATION: [THREAD] Start failed | Error=") + e.what(), "OPR");
        return false;
    }
}

bool operation::FnOperationInit()
{
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);

    if (running_.load() || ioContextThread_.joinable())
    {
        writelog("OPERATION: [INIT] Ignored | Reason=Already running", "OPR");
        return true;
    }

    stopping_.store(false);
    isOperationInitialized_.store(false);

    ioContext_.restart();
    workGuard_.emplace(ioContext_.get_executor());

    if (!startIoContextThread())
    {
        workGuard_.reset();
        return false;
    }

    auto initPromise = std::make_shared<std::promise<bool>>();
    auto initFuture = initPromise->get_future();

    boost::asio::post(
        ioContext_,
        [this, initPromise]()
        {
            bool success = false;

            try
            {
                success = initializeOnIoThread();
            }
            catch (const std::exception& e)
            {
                writelog(std::string("OPERATION: [INIT] Exception | Error=") + e.what(), "OPR");
            }
            catch (...)
            {
                writelog("OPERATION: [INIT] Exception | Error=Unknown exception", "OPR");
            }

            initPromise->set_value(success);
        });

    const bool initialized = initFuture.get();

    if (!initialized)
    {
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
        workGuard_.reset();

        if (ioContextThread_.joinable())
        {
            ioContextThread_.join();
        }

        pLCDIdleTimer_.reset();
        pLoopATimer_.reset();
        pDailyProcessTimer_.reset();
        monitorUdpClient_.reset();
        pmsUdpClient_.reset();
        running_.store(false);
        stopping_.store(false);

        return false;
    }

    writelog("OPERATION: [INIT] OP_IO started", "OPR");

    return true;
}

bool operation::initializeOnIoThread()
{
    Setdefaultparameter();

    gtStation.iSID = std::stoi(IniParser::getInstance()->FnGetStationID());
    tParas.gsCentralDBName = IniParser::getInstance()->FnGetCentralDBName();
    tParas.gsCentralDBServer = IniParser::getInstance()->FnGetCentralDBServer();

    // Broadcast UDP and monitor UDP are passive async transports bound to the
    // Operation-owned io_context. Exactly one OP_IO thread runs this context.
    tProcess.gsBroadCastIP = getIPAddress();

    if (!tProcess.gsBroadCastIP.empty())
    {
        try
        {
            const unsigned short remoteUDPPort = static_cast<unsigned short>(std::stoi(IniParser::getInstance()->FnGetRemoteUDPPort()));
            const unsigned short localUDPPort = static_cast<unsigned short>(std::stoi(IniParser::getInstance()->FnGetLocalUDPPort()));

            pmsUdpClient_ = std::make_unique<udpclient>(ioContext_, tProcess.gsBroadCastIP, remoteUDPPort, localUDPPort, true);
            pmsUdpClient_->start();
        }
        catch (const boost::system::system_error& e)
        {
            writelog("Boost.Asio Exception during PMS UDP initialization: " + std::string(e.what()), "OPR");
        }
        catch (const std::exception& e)
        {
            writelog("Exception during PMS UDP initialization: " + std::string(e.what()), "OPR");
        }
        catch (...)
        {
            writelog("Unknown Exception during PMS UDP initialization.", "OPR");
        }

        try
        {
            monitorUdpClient_ = std::make_unique<udpclient>(ioContext_, tParas.gsCentralDBServer, 2008, 2008);
            monitorUdpClient_->start();
        }
        catch (const boost::system::system_error& e)
        {
            writelog("Boost.Asio Exception during Monitor UDP initialization: " + std::string(e.what()), "OPR");
        }
        catch (const std::exception& e)
        {
            writelog("Exception during Monitor UDP initialization: " + std::string(e.what()), "OPR");
        }
        catch (...)
        {
            writelog("Unknown Exception during Monitor UDP initialization.", "OPR");
        }
    }

    int iRet = 0;

    iRet = db::getInstance()->connectlocaldb("DRIVER={MariaDB ODBC 3.0 Driver};SERVER=localhost;PORT=3306;DATABASE=linux_pbs;UID=linuxpbs;PWD=SJ2001;", 2, 2, 1);
    if (iRet != 1)
    {
        writelog("Unable to connect local DB.", "OPR");
        return false;
    }

    string m_connstring;

    writelog("Connect Central (" + tParas.gsCentralDBServer + ") DB:" + tParas.gsCentralDBName, "OPR");

    for (int i = 0; i < 5; ++i)
    {
        m_connstring = "DRIVER=FreeTDS;SERVER=" + tParas.gsCentralDBServer + ";PORT=1433;DATABASE=" + tParas.gsCentralDBName + ";UID=sa;PWD=yzhh2007";

        iRet = db::getInstance()->connectcentraldb(m_connstring, tParas.gsCentralDBServer, 2, 2, 1);
        if (iRet == 1)
        {
            break;
        }

        // Startup-only blocking retry. This remains intentionally unchanged in
        // Phase 1 and will move to DB_WORK in a later phase.
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    if (iRet == 1)
    {
        db::getInstance()->synccentraltime();
        db::getInstance()->FnUpdateStationSwVersion(IniParser::getInstance()->FnGetStationID());
    }
    else
    {
        tProcess.giSystemOnline = 1;
    }

    if (LoadParameter())
    {
        initDeviceOnIoThread();
        HandlePBSError(ParamOk);

        writelog ("**** EPS: " + std::to_string(tParas.giEPS) + " in operation ****", "OPR");

        if (gtStation.iType == tientry)
        {
            tProcess.IdleMsg[0] = tMsg.Msg_DefaultLED[0];
            tProcess.IdleMsg[1] = tMsg.Msg_Idle[1];
        }
        else
        {
            tProcess.IdleMsg[0] = tExitMsg.MsgExit_XDefaultLED[0];
            tProcess.IdleMsg[1] = tExitMsg.MsgExit_XIdle[1];
        }

        Clearme();
        CheckReader();
        isOperationInitialized_.store(true);

        DIO::getInstance()->FnStartDIOMonitoring();
        SendMsg2Server("90", ",,,,,Starting OK");

        db::getInstance()->downloadseason();
        db::getInstance()->moveOfflineTransToCentral();

        writelog("Check barrier", "OPR");

        if (tParas.gbLockBarrier == true)
        {
            writelog("Startup: continue open barrier", "OPR");
            continueOpenBarrier();
        }
    }
    else
    {
        tProcess.gbInitParamFail = 1;
        HandlePBSError(ParamError);
        writelog("Unable to load parameter, Please download or check!", "OPR");

        if (iRet == 1)
        {
            db::getInstance()->downloadstationsetup();
            db::getInstance()->loadstationsetup();
        }
    }

    // This maintenance timer belongs to Operation and runs on the single
    // OP_IO thread. The first tick preserves the old 1-second startup delay.
    startDailyProcessTimer();

    return true;
}

void operation::startDailyProcessTimer()
{
    if (stopping_.load())
    {
        return;
    }

    if (pDailyProcessTimer_)
    {
        boost::system::error_code ec;
        pDailyProcessTimer_->cancel(ec);
    }

    pDailyProcessTimer_ = std::make_unique<boost::asio::steady_timer>(ioContext_);

    // Preserve the previous Main timer semantics.
    lastDailyProcessSyncTime_ = std::chrono::steady_clock::now();
    lastUPTSettleTime_ = Common::getInstance()->FnGetDate();

    scheduleDailyProcessTimer(std::chrono::seconds(1));
}

void operation::scheduleDailyProcessTimer(std::chrono::seconds delay)
{
    if (stopping_.load() || !pDailyProcessTimer_)
    {
        return;
    }

    pDailyProcessTimer_->expires_after(delay);
    pDailyProcessTimer_->async_wait(
        [this](const boost::system::error_code& ec)
        {
            handleDailyProcessTimer(ec);
        });
}

void operation::handleDailyProcessTimer(const boost::system::error_code& ec)
{
    if (ec == boost::asio::error::operation_aborted ||
        stopping_.load())
    {
        return;
    }

    if (ec)
    {
        writelog("Daily process timer error: " + ec.message(), "OPR");
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto durationSinceSync = std::chrono::duration_cast<std::chrono::hours>(now - lastDailyProcessSyncTime_);

    // Transitional Phase 1 behaviour: Ping and DB calls below are still
    // synchronous, so they can temporarily block OP_IO. They should move to
    // DB_WORK / a blocking worker in the next refactoring phase.
    if (FnIsOperationInitialized())
    {
        if (tProcess.gbLoopApresent == false)
        {
            std::string details;

            if (tProcess.giSystemOnline == 1)
            {
                if (PingWithTimeOut(IniParser::getInstance()->FnGetCentralDBServer(), 1, details))
                {
                    tProcess.giSystemOnline = 0;
                }
            }

            if (db::getInstance()->FnGetDatabaseErrorFlag() == 0)
            {
                if (!tProcess.gbLastDBConnected)
                {
                    HandlePBSError(DBNoError);
                }

                tProcess.gbLastDBConnected = true;
            }
            else
            {
                if (tProcess.gbLastDBConnected)
                {
                    HandlePBSError(DBFailed);
                }

                tProcess.gbLastDBConnected = false;
            }

            if (tProcess.giSystemOnline == 0 && tProcess.glNoofOfflineData > 0)
            {
                db::getInstance()->moveOfflineTransToCentral();
            }

            if (durationSinceSync >= std::chrono::hours(1))
            {
                db::getInstance()->synccentraltime();
                lastDailyProcessSyncTime_ = now;
                CheckReader();
            }

            const int currentDay = Common::getInstance()->FnGetCurrentDay();

            if (tProcess.giLastHousekeepingDate != currentDay)
            {
                db::getInstance()->HouseKeeping();
                tProcess.giLastHousekeepingDate = currentDay;
            }

            // LCSC performs its blocking CD file work on its own file worker.
            LCSCReader::getInstance()->FnUploadLCSCCDFiles();

            const std::string currentDate = Common::getInstance()->FnGetDate();

            if (gtStation.iType == tiExit && lastUPTSettleTime_ != currentDate)
            {
                Upt::getInstance()->FnUptSendDeviceRetrieveLastSettlementRequest();
                lastUPTSettleTime_ = currentDate;
            }
        }

        sendDateTimeToMonitor();
    }
    else if (tProcess.gbInitParamFail == 1 && LoadedparameterOK())
    {
        tProcess.gbInitParamFail = 0;

        initDeviceOnIoThread();
        isOperationInitialized_.store(true);

        if (gtStation.iType == tientry)
        {
            tProcess.IdleMsg[0] =  tMsg.Msg_DefaultLED[0];
            tProcess.IdleMsg[1] = tMsg.Msg_Idle[1];
        }
        else
        {
            tProcess.IdleMsg[0] = tExitMsg.MsgExit_XDefaultLED[0];
            tProcess.IdleMsg[1] = tExitMsg.MsgExit_XIdle[1];
        }

        writelog("EPS in operation", "OPR");
    }

    if (!stopping_.load())
    {
        // Same behaviour as before: five seconds after this handler finishes.
        scheduleDailyProcessTimer(std::chrono::seconds(5));
    }
}

bool operation::LoadParameter()
{
    auto* database = db::getInstance();

    bool loadParameterOk = true;

    //------
    const auto checkLoadResult =
        [this, &loadParameterOk](
            DBError result,
            const std::string& noDataMessage,
            const std::string& errorMessage)
        {
            if (result == iDBSuccess)
            {
                return;
            }

            if (result == iNoData)
            {
                writelog(noDataMessage, "OPR");
            }
            else
            {
                writelog(errorMessage, "OPR");
            }

            loadParameterOk = false;
        };

    checkLoadResult(
        database->loadstationsetup(),
        "No data for station setup table",
        "Error for loading station setup");

    checkLoadResult(
        database->loadmessage(),
        "No data for LED message table",
        "Error for loading LED message");

    checkLoadResult(
        database->loadExitmessage(),
        "No data for LED Exit message table",
        "Error for loading LED Exit message");

    checkLoadResult(
        database->loadParam(),
        "No data for Parameter table",
        "Error for loading parameter");

    checkLoadResult(
        database->loadvehicletype(),
        "No data for vehicle type table",
        "Error for loading vehicle type");

    checkLoadResult(
        database->loadTR(2),
        "No data for TR table",
        "Error for loading TR");

    checkLoadResult(
        database->LoadTariffTypeInfo(),
        "No data for Tariff Type Info table",
        "Error for loading Tariff Type Info");

    checkLoadResult(
        database->LoadHoliday(),
        "No data for holiday table",
        "Error for loading holiday");

    checkLoadResult(
        database->LoadTariff(),
        "No data for Tariff table",
        "Error for loading Tariff");

    checkLoadResult(
        database->LoadXTariff(),
        "No data for XTariff table",
        "Error for loading XTariff");

    return loadParameterOk;
}

bool operation::LoadedparameterOK()
{
    return tProcess.gbloadedLEDMsg &&
           tProcess.gbloadedLEDExitMsg &&
           tProcess.gbloadedParam &&
           tProcess.gbloadedStnSetup &&
           tProcess.gbloadedVehtype;
}

void operation::FnStopDailyProcessTimer()
{
    if (ioContext_.get_executor().running_in_this_thread())
    {
        if (pDailyProcessTimer_)
        {
            boost::system::error_code ec;
            pDailyProcessTimer_->cancel(ec);
        }
        return;
    }

    if (!ioContextThread_.joinable())
    {
        return;
    }

    // Use a small lifecycle barrier: when this function returns, any currently
    // executing daily-process handler has finished and the timer has been
    // cancelled on OP_IO. Main can then safely begin closing device modules.
    auto stopPromise = std::make_shared<std::promise<void>>();
    auto stopFuture = stopPromise->get_future();

    boost::asio::post(
        ioContext_,
        [this, stopPromise]()
        {
            if (pDailyProcessTimer_)
            {
                boost::system::error_code ec;
                pDailyProcessTimer_->cancel(ec);

                if (ec)
                {
                    writelog("Daily process timer cancel error: " + ec.message(), "OPR");
                }
            }

            stopPromise->set_value();
        });

    stopFuture.wait();
}

bool operation::FnIsOperationInitialized() const
{
    return isOperationInitialized_.load();
}

void operation::setLastActionTimeAfterLoopA()
{
    lastActionTimeAfterLoopA_ = std::chrono::steady_clock::now();
}

std::chrono::steady_clock::time_point operation::getLastActionTimeAfterLoopA()
{
    return lastActionTimeAfterLoopA_;
}

void operation::stopLoopAPeriodicTimer()
{
    writelog(__func__, "OPR");

    if (!pLoopATimer_)
    {
        writelog("Unable to stop Loop A periodic timer due to pLoopATimer is nullptr.", "OPR");
        return;
    }

    boost::system::error_code ec;
    pLoopATimer_->cancel(ec);

    if (ec)
    {
        writelog("Loop A timer cancel error: " + ec.message(), "OPR");
    }
}

void operation::loopATimeoutHandler()
{
    writelog("Loop A Operation Timeout handler.", "OPR");
    Antenna::getInstance()->FnAntennaStopRead();
    EnableCashcard(false);

    if (tProcess.gbLoopApresent)
    {
        LoopACome();
    }
}

void operation::handleLoopAPeriodicTimerTimeout(const boost::system::error_code &ec)
{
    if (ec == boost::asio::error::operation_aborted ||
        stopping_.load())
    {
        return;
    }

    if (ec)
    {
        writelog("Loop A timer error: " + ec.message(), "OPR");
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto lastAction = getLastActionTimeAfterLoopA();
    const auto timeout = std::chrono::seconds(tParas.giOperationTO);

    if ((now - lastAction) > timeout)
    {
        loopATimeoutHandler();
        return;
    }

    if (stopping_.load() || !pLoopATimer_)
    {
        return;
    }

    pLoopATimer_->expires_after(std::chrono::seconds(1));
    pLoopATimer_->async_wait(
        [this](const boost::system::error_code& waitEc)
        {
            handleLoopAPeriodicTimerTimeout(waitEc);
        });
}

void operation::startLoopAPeriodicTimer()
{
    if (stopping_.load())
    {
        return;
    }

    writelog(__func__, "OPR");
    if (!pLoopATimer_)
    {
        writelog("Unable to start Loop A periodic timer due to pLoopATimer is nullptr.", "OPR");
        return;
    }

    pLoopATimer_->expires_after(std::chrono::seconds(1));
    pLoopATimer_->async_wait(
        [this](const boost::system::error_code& waitEc)
        {
            handleLoopAPeriodicTimerTimeout(waitEc);
        });
}

void operation::LoopACome()
{
    //--------
    writelog ("Loop A Come","OPR");

    // Loop A timer - To prevent loop A hang
    startLoopAPeriodicTimer();
    setLastActionTimeAfterLoopA();

    if (gtStation.iType == tientry)
    {
        ShowLEDMsg(tMsg.Msg_LoopA[0], tMsg.Msg_LoopA[1]);
    }
    else
    {
        ShowLEDMsg(tExitMsg.MsgExit_XLoopA[0], tExitMsg.MsgExit_XLoopA[1]);
    }

    Clearme();

    //---- added on 02/03/2026
    tProcess.gsTailgateOBU = "";
    DIO::getInstance()->FnSetLCDBacklight(1);
    //----
    int vechicleType = 7;
    for (int i = 0; i < 50; ++i)
    {
        if (DIO::getInstance()->FnGetLoopBStatus() && DIO::getInstance()->FnGetLoopAStatus())
        {
            vechicleType = 1;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::string transID = "";
    bool useFrontCamera = false;
    /* Temp: Disable for MiniPC Testing
    // Motorcycle - use rear camera
    if (vechicleType == 7)
    {
        // For EdgeBox AI
        useFrontCamera = true;
        transID = tParas.gscarparkcode + "-" + std::to_string (gtStation.iSID) + "B-" + Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmmss();
    }
    // Car or Lorry - use front camera
    else
    {
        useFrontCamera = true;
        transID = tParas.gscarparkcode + "-" + std::to_string (gtStation.iSID) + "F-" + Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmmss();
    }
    */
    // For miniPC testing
    //if (IniParser::getInstance()->FnGetLPRIP4Front() != "1.1.1.1" && IniParser::getInstance()->FnGetLPRIP4Rear() != "1.1.1.1") 
    {
        if (IniParser::getInstance()->FnGetLPRIP4Front() != "1.1.1.1") useFrontCamera = true;
        transID = tParas.gscarparkcode + "-" + std::to_string (gtStation.iSID) + "-" + Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmmss();
        tProcess.gsTransID = transID;
        Lpr::getInstance()->FnSendTransIDToLPR(tProcess.gsTransID, useFrontCamera);
    }

    if (tParas.giEPS == 3) {
        EEPInq();
        ShowLEDMsg("Reading OBU...^Please wait", "Reading OBU...^Please wait");
    }
    else if (tParas.giEPS == 0)
    {
        EnableCashcard(true);
        ShowLEDMsg("Insert/Tap Card", "Insert/Tap Card");
    }
    else if (AntennaOK() == true)
    {
        Antenna::getInstance()->FnAntennaSendReadIUCmd();
        ShowLEDMsg("Reading IU...^Please wait", "Reading IU...^Please wait");
    }
    else
    {
        EnableCashcard(true);
        ShowLEDMsg("Antenna Error!^Insert/Tap Card", "Antenna Error!^Insert/Tap Card");
    }
}

void operation::LoopAGone()
{
    writelog ("Loop A End","OPR");

    stopLoopAPeriodicTimer();

    //------
    DIO::getInstance()->FnSetLCDBacklight(0);
     //
    if (gtStation.iType == tientry)
    {
        if (tEntry.sIUTKNo.empty())
        {
            Antenna::getInstance()->FnAntennaStopRead();
        }
    }
    else
    {
        if (tExit.sIUNo.empty())
        {
            Antenna::getInstance()->FnAntennaStopRead();
        }

        //------
        if (tExit.sRedeemAmt == 0 && !tProcess.gbsavedtrans)
        {
            if (tExit.bPayByEZPay)
            {
                EnableCashcard(false);
                CloseExitOperation(EZPayParking);
            }
            else if (tExit.bPayByAXS)
            {
                EnableCashcard(false);
                CloseExitOperation(AXSParking);
            }
        }
    }

    EnableCashcard(false);

    //------- added on 01/12/2025
    if (tParas.giEPS == 3)
    {
        writelog ("Send OBU information stop request" ,"OPR");
        EEPClient::getInstance()->FnSendGetOBUInfoStopReq();
    }
}

void operation::LoopCCome()
{
    writelog ("Loop C Come","OPR");
}

void operation::LoopCGone()
{
    writelog ("Loop C End","OPR");

    if (!tProcess.gbsavedtrans && tProcess.gbLoopApresent)
    {
        return;
    }
    //-----
    Clearme();
    //------
    if (tProcess.gbLoopApresent)
    {
       writelog("Loop C gone, Loop A come", "OPR");
       if(tParas.giEPS == 3) 
       {
            for (int i = 0; i < 50; ++i)
            {
                if (tProcess.fiLastEEPCmd == EEPClient::CommandType::EEP_idle)
                {
                    break;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            writelog("Send stop Request of related information distribution", "OPR");

            EEPClient::getInstance()->FnSendStopReqOfRelatedInfoDistributionReq(tProcess.gsLastIUNo);
            for (int i = 0; i < 50; ++i)
            {
                if (tProcess.fiLastEEPCmd == EEPClient::CommandType::EEP_idle)
                {
                    break;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
       }
       
       LoopACome();
    }
    else
    {
        //shwo default Msg
    }
}

void operation::VehicleCome(const std::string& sNo)
{
    if (sNo.empty())
    {
        return;
    }

    const bool isIU = (sNo.length() == 10);
    //--------------------
    if (isIU)
    {
        writelog("Received IU: " + sNo, "OPR");
    }
    else
    {
        writelog("Received Card: " + sNo, "OPR");
        Antenna::getInstance()->FnAntennaStopRead();
    }

    if (isIU && sNo == tProcess.gsDefaultIU)
    {
        SendMsg2Server ("90",sNo+",,,,,Default IU");
        writelog ("Default IU: "+sNo,"OPR");
        //---------
        return;
    }

    if (isIU)
    {
        EnableCashcard(false);
    }

    //----
    if (gtStation.iType == tientry)
    {
        const auto sameAsLastIUDuration =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - tProcess.lastIUEntryTime);

        if (isIU &&
            tProcess.gsLastIUNo == sNo &&
            sameAsLastIUDuration.count() <= tParas.giMaxTransInterval)
        {
            writelog ("Same as last OBU", "OPR");
            ShowLEDMsg("Same as last OBU^Please Proceed", "Same as last OBU^Please Proceed");
            Openbarrier(2);
        }
        else
        {
            if (tEntry.gbEntryOK == false)
            {
                PBSEntry(sNo);
            }
        }
    }
    else
    {
        //check IU or card status
        CheckIUorCardStatus(sNo, Ant);
    }
}

//---- default 0, 2-- same as last card
void operation::Openbarrier(int iReason)
{
    stopLoopAPeriodicTimer();
    
    if (tProcess.giCardIsIn == 1)
    {
        ShowLEDMsg ("Please Take^CashCard.", "Please Take^Cashcard.");
        return;
    }

    if (tProcess.giBarrierContinueOpened == 1)
    {
        return;
    }

    writelog("Open Barrier","OPR");

    tProcess.gbBarrierOpened = true;

    if (tParas.gsBarrierPulse <= 0)
    {
        tParas.gsBarrierPulse = 500;
    }

    DIO::getInstance()->FnSetOpenBarrier(1);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(tParas.gsBarrierPulse));
    
    DIO::getInstance()->FnSetOpenBarrier(0);
    //------ added on 08/01/2025
    if (tParas.giEPS == 3)
    {
        EndEEPprocess(iReason);
    }
}

void operation::closeBarrier()
{
    writelog("Close barrier.", "OPR");
    
    if (tParas.gsBarrierPulse <= 0)
    {
        tParas.gsBarrierPulse = 500;
    }

    // Reset the continue open barrier
    DIO::getInstance()->FnSetOpenBarrier(0);

    DIO::getInstance()->FnSetCloseBarrier(1);
    //
    std::this_thread::sleep_for(std::chrono::milliseconds(tParas.gsBarrierPulse));
    //
    DIO::getInstance()->FnSetCloseBarrier(0);

    tProcess.giBarrierContinueOpened = 0;
}

void operation::continueOpenBarrier()
{
    writelog("Continue Open Barrier", "OPR");

    DIO::getInstance()->FnSetOpenBarrier(1);

    tProcess.giBarrierContinueOpened = 1;
}

void operation::Clearme()
{
    tProcess.giShowType = 1;
    tProcess.giIsSeason = 0;
    tProcess.giCardIsIn = 0;
    tProcess.gbsavedtrans = false;
    tProcess.sEnableReader = false;
    tProcess.gbBarrierOpened = false;
    tProcess.fiLastEEPCmd = EEPClient:: CommandType::EEP_idle;
    tProcess.fiLastCHUCmd = iInit; 
    tProcess.gsTransID = ""; 

    //---
    if (gtStation.iType == tientry)
    {
        tEntry.esid = to_string(gtStation.iSID);
        tEntry.sSerialNo = "";     
        tEntry.sIUTKNo = "";
        tEntry.sEntryTime = "";
        tEntry.iTransType = 1;
        tEntry.iRateType = 0;
        tEntry.iStatus = 0;      

        tEntry.sCardNo = "";
        tEntry.sFee = 0.00;
        tEntry.sPaidAmt = 0.00;
        tEntry.sGSTAmt = 0.00;
        tEntry.sReceiptNo = "";
        tEntry.iCardType=0;
        tEntry.sOweAmt= 0.00;

        tEntry.sLPN[0] = "";
        tEntry.sLPN[1] = "";
        tEntry.iVehicleType = 0;
        tEntry.gbEntryOK = false;
        tEntry.VCC = "";
    }
    else
    {
        tExit.xsid = to_string(gtStation.iSID);
	    tExit.sExitTime = "";
	    tExit.sIUNo = "";
	    tExit.sCardNo = "";
	    tExit.iTransType = 0;
	    tExit.lParkedTime = 0;
	    tExit.sFee = 0.00 ;
	    tExit.sPaidAmt = 0.00;
	    tExit.sReceiptNo = "";
        tExit.iflag4Receipt= 0;
	    tExit.iStatus = 0;
        tExit.sOweAmt = 0.00;
        tExit.sPrePaid = 0.00;
	    tExit.sRedeemAmt = 0.00;
	    tExit.iRedeemTime = 0;
	    tExit.sRedeemNo = "";
	    tExit.sGSTAmt = 0.00;
	    tExit.sCHUDebitCode = "";
	    tExit.iCardType = 0;
	    tExit.sTopupAmt = 0;
	    tExit.uposbatchno = "";
	    tExit.feefrom = "EPS";
	    tExit.lpn = "";
	
	    tExit.iRateType = 0;
	    tExit.sEntryTime = "";
	
        tExit.dtValidTo = "";
	    tExit.dtValidFrom = "";

	    tExit.iEntryID =0;
	    tExit.sExitNo = "";
	    tExit.iUseMultiCard = 0;
	    tExit.iNeedSendIUtoEntry = 0;
	    tExit.iPartialseason = 0;
	    tExit.sRegisterCard = "";
	    tExit.iAttachedTransType = 0;

	    tExit.sCalFeeTime = "";
	    tExit.sRebateAmt = 0.00;
	    tExit.sRebateBalance = 0.00;
	    tExit.sRebateDate = "";
	    tExit.sLPN[0] = "";
        tExit.sLPN[1] = "";
	    tExit.iVehicleType = 0;

	    tExit.entry_lpn = "";
	    tExit.video_location = "";
	    tExit.video1_location = "";

	    tExit.giDeductionStatus = init;
        tExit.gbPaid = false;
        tExit.bNoEntryRecord = -1;
        tExit.bPayByEZPay = false;
        tExit.bPayByAXS = false;
        //----- added on 08/12/2025
        tExit.iCardStatus = 1 ;          //default: 1: No card, 0: valid card, other: invalid card    
	    tExit.iBackendAccount = 0;
	    tExit.iBackendSetting = 0;
	    tExit.iBFunctionStatus = 0;
	    tExit.iOBUType = 0;
	    tExit.sDSerialNo = "";
	    tExit.iEEPTransRoute = 0;
	    tExit.iEEPPaymentResult = 0;
	    tExit.sEEPpaymentTime = "";
        tExit.VCC = "";
        //-------
        tExit.iUsedTicketBy = 0;
        tExit1 = tExit;
    }

}

std::string operation::getSerialPort(const std::string& key)
{
    std::map<std::string, std::string> serial_port_map = 
    {
        {"1", "/dev/ttyCH9344USB7"}, // J3
        {"2", "/dev/ttyCH9344USB6"}, // J4
        {"3", "/dev/ttyCH9344USB5"}, // J5
        {"4", "/dev/ttyCH9344USB4"}, // J6
        {"5", "/dev/ttyCH9344USB3"}, // J7
        {"6", "/dev/ttyCH9344USB2"}, // J8
        {"7", "/dev/ttyCH9344USB1"}, // J9
        {"8", "/dev/ttyCH9344USB0"}  // J10
    };

    auto it = serial_port_map.find(key);

    if (it != serial_port_map.end())
    {
        return it->second;
    }
    else
    {
        return "";
    }
}

void operation::initDeviceOnIoThread()
{
    if (tParas.giCommPortAntenna > 0 && tParas.giEPS > 0 && tParas.giEPS < 3)
    {
        Antenna::getInstance()->FnAntennaInit(
                                    19200,
                                    getSerialPort(std::to_string(tParas.giCommPortAntenna)),
                                    gtStation.iAntID,
                                    tParas.giAntInqTO,
                                    tParas.giAntMinOKTimes,
                                    tParas.giEPS);
    }

    if (tParas.giCommPortLCSC > 0)
    {
        const int iRet = LCSCReader::getInstance()->FnLCSCReaderInit(
                                                        115200,
                                                        getSerialPort(std::to_string(tParas.giCommPortLCSC)),
                                                        tParas.giCommPortLCSC,
                                                        gtStation.iSID,
                                                        tParas.gsCPOID,
                                                        tParas.gsCPID,
                                                        tParas.giEPS,
                                                        tParas.gsCSCRcdackFolder,
                                                        tParas.gsCSCRcdfFolder);

        if (iRet == -35)
        {
            tPBSError[iLCSC].ErrNo = -4;
            HandlePBSError(LCSCError);
        }
    }

    if (tParas.giCommPortLED > 0)
    {
        int maxCharPerRow = 0;

        if (tParas.giLEDMaxChar < 20)
        {
            maxCharPerRow = LED::LED216_MAX_CHAR_PER_ROW;
        }
        else
        {
            maxCharPerRow = LED::LED226_MAX_CHAR_PER_ROW;
        }

        LEDManager::getInstance()->createLED(9600, getSerialPort(std::to_string(tParas.giCommPortLED)), maxCharPerRow);
    }

    if (tParas.giCommportLED401 > 0)
    {
        LEDManager::getInstance()->createLED(9600, getSerialPort(std::to_string(tParas.giCommportLED401)), LED::LED614_MAX_CHAR_PER_ROW);
    }

    if (tParas.giCommPortKDEReader > 0 && gtStation.iType == tientry)
    {
        const int iRet = KSM_Reader::getInstance()->FnKSMReaderInit(9600, getSerialPort(std::to_string(tParas.giCommPortKDEReader)));

        if (iRet == -4)
        {
            tPBSError[iReader].ErrNo = -4;
        }
    }

    if (tParas.giCommPortUPOS > 0 && gtStation.iType == tiExit)
    {
        Upt::getInstance()->FnUptInit(115200, getSerialPort(std::to_string(tParas.giCommPortUPOS)));

        Upt::getInstance()->FnUptSendDeviceTimeSyncRequest();
    }

    if (LCD::getInstance()->FnLCDInit())
    {
        if (pLCDIdleTimer_)
        {
            boost::system::error_code ec;
            pLCDIdleTimer_->cancel(ec);
        }

        pLCDIdleTimer_ = std::make_unique<boost::asio::steady_timer>(ioContext_);

        scheduleLcdIdleTimer();
    }

    if (tParas.giCommPortPrinter > 0 && gtStation.iType == tiExit)
    {
        Printer::getInstance()->FnSetPrintMode(2);
        Printer::getInstance()->FnSetDefaultAlign(Printer::CBM_ALIGN::CBM_LEFT);
        Printer::getInstance()->FnSetDefaultFont(2);
        Printer::getInstance()->FnSetSelfTestInterval(2000);
        Printer::getInstance()->FnSetSiteID(10);
        Printer::getInstance()->FnSetPrinterType(Printer::PRINTER_TYPE::CBM1000);
        Printer::getInstance()->FnPrinterInit(9600, getSerialPort(std::to_string(tParas.giCommPortPrinter)));
    }

    DIO::getInstance()->FnDIOInit(tParas.giBarrierOpenTooLongTime);
    Lpr::getInstance()->FnLprInit();

    if (gtStation.iType == tiExit)
    {
        BARCODE_READER::getInstance()->FnBarcodeReaderInit();
    }

    if (tParas.giEPS == 3)
    {
        EEPClient::getInstance()->FnEEPClientInit(
                                    IniParser::getInstance()->FnGetEEPClientIp(),
                                    IniParser::getInstance()->FnGetEEPClientPort(),
                                    IniParser::getInstance()->FnGetStationID(),
                                    tParas.gsCPOID,
                                    tParas.gsCPID);
    }
    else if (tParas.giEPS == 2)
    {
        CHUClient::getInstance()->FnCHUClientInit(tParas.gsCHUIP, gtStation.iCHUPort);
    }

    if (pLoopATimer_)
    {
        boost::system::error_code ec;
        pLoopATimer_->cancel(ec);
    }

    pLoopATimer_ = std::make_unique<boost::asio::steady_timer>(ioContext_);
}

void operation::scheduleLcdIdleTimer()
{
    if (stopping_.load() || !pLCDIdleTimer_)
    {
        return;
    }

    pLCDIdleTimer_->expires_after(std::chrono::seconds(1));
    pLCDIdleTimer_->async_wait(
        [this](const boost::system::error_code& ec)
        {
            handleLcdIdleTimer(ec);
        });
}

void operation::handleLcdIdleTimer(const boost::system::error_code& ec)
{
    if (ec == boost::asio::error::operation_aborted ||
        stopping_.load())
    {
        return;
    }

    if (ec)
    {
        writelog("LCD Idle timer timeout error: " + ec.message(), "OPR");
        return;
    }

    lcdIdleTimerTimeoutHandler();

    if (!stopping_.load())
    {
        scheduleLcdIdleTimer();
    }
}

void operation::lcdIdleTimerTimeoutHandler()
{
    if (stopping_.load())
    {
        return;
    }

    if (!tProcess.gbcarparkfull && !tProcess.gbLoopApresent)
    {
        ShowLEDMsg(tProcess.IdleMsg[0], tProcess.IdleMsg[1]);
        
        std::string lcdMsg;
        
        if (IniParser::getInstance()->FnGetShowTime())
        {
            lcdMsg = Common::getInstance()->FnGetDateTimeFormat_ddmmyyy_hhmmss();
        }
        else
        {
            lcdMsg = tParas.gsCompany;
        }
        LCD::getInstance()->FnLCDDisplayRow(2, lcdMsg.data());
    }
    else if (tProcess.gbcarparkfull && tProcess.gbLoopApresent)
    {
        ShowLEDMsg(tProcess.IdleMsg[0], tProcess.IdleMsg[1]);
    }
}

void operation::ShowLEDMsg(string LEDMsg, string LCDMsg)
{
    static std::string sLastLEDMsg;
    static std::string sLastLCDMsg;

    if (sLastLEDMsg != LEDMsg)
    {
        writelog("LED Message:" + LEDMsg,"OPR");

        auto* led = LEDManager::getInstance()->getLED(getSerialPort(std::to_string(tParas.giCommPortLED)));
        if (led != nullptr)
        {
            led->FnLEDSendLEDMsg("***", LEDMsg, LED::Alignment::CENTER);
            sLastLEDMsg = LEDMsg;
        }
    }

    if (sLastLCDMsg != LCDMsg)
    {
        writelog ("LCD Message:" + LCDMsg,"OPR");

        std::string lcdMessage = LCDMsg;
        LCD::getInstance()->FnLCDDisplayScreen(lcdMessage.data());
        sLastLCDMsg = LCDMsg;
    }
}

void operation::PBSEntry(string sIU)
{
    int iRet;

    tEntry.sIUTKNo = sIU;
    tEntry.sEntryTime= Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();

    if (sIU == "") return;
    //check blacklist
    SendMsg2Server("90",","+sIU+",,"+tEntry.sLPN[0]+ ",,PMS_DVR");
    iRet = db::getInstance()->IsBlackListIU(sIU);
    if (iRet >= 0){
        ShowLEDMsg(tMsg.MsgBlackList[0], tMsg.MsgBlackList[1]);
        SendMsg2Server("90",sIU+",,,,,Blacklist IU");
        if(iRet ==0) return;
    }
    //check block 
    string gsBlockIUPrefix = IniParser::getInstance()->FnGetBlockIUPrefix();
   // writelog ("blockIUprfix =" +gsBlockIUPrefix, "OPR");
    if(gsBlockIUPrefix.find(sIU.substr(0,3)) !=std::string::npos && sIU.length() == 10)
    {
        ShowLEDMsg("Lorry No Entry^Pls Reverse","Lorry No Entry^Pls Reverse");
        SendMsg2Server("90",sIU+",,,,,Block IU");
        return;
    }

    if(sIU.length()==10) {
        if (tParas.giEPS == 3 and tEntry.VCC != "") {
             tExit.iTransType= db::getInstance()->FnGetVehicleType(tEntry.VCC.substr(0,3));
        }else {
	         tEntry.iTransType= db::getInstance()->FnGetVehicleType(sIU.substr(0,3));
        }
    }
	else {
        tEntry.iTransType=GetVTypeFromLoop();
    }
    if (tEntry.iTransType == 9) {
        ShowLEDMsg(tMsg.Msg_authorizedvehicle[0],tMsg.Msg_authorizedvehicle[1]);
        tEntry.iStatus = 0;
        SaveEntry();
        Openbarrier();
        return;
    }
    if (tProcess.gbcarparkfull && tParas.giFullAction == iLock)
    {   
        ShowLEDMsg(tMsg.Msg_LockStation[0], tMsg.Msg_LockStation[1]);
        writelog("Loop A while station Locked","OPR");
        return;
    }
    tEntry.iVehicleType = (tEntry.iTransType -1 )/3;
    iRet = CheckSeason(sIU,1);
    //----- added on 18/08/2026
     if (iRet != 1 && std::stoi(IniParser::getInstance()->FnGetNotAllowHourly()) == 1) {
        writelog ("Season Only.", "OPR");
        ShowLEDMsg("Season Parking Only", "Season Parking Only");
        if (tParas.giEPS == 3) SendMsg2OBU(tExit.sIUNo,0,"Season", "Parking Only","","","");
        return;
     }

    if (tProcess.gbcarparkfull && iRet == 1 && (std::stoi(tSeason.rate_type) !=0) && tParas.giFullAction ==iNoPartial )
    {   
        writelog ("VIP Season Only.", "OPR");
        ShowLEDMsg("Carpark Full!^VIP Season Only", "Carpark Full!^VIP Season Only");
        if (tParas.giEPS == 3) SendMsg2OBU(tExit.sIUNo,0,"Car Park Full", "","","","");
        return;
    } 
    if (tProcess.gbcarparkfull && iRet != 1 )
    {   
        writelog ("Season Only.", "OPR");
        ShowLEDMsg("Carpark Full!^Season only", "Carpark Full!^Season only");
        if (tParas.giEPS == 3) SendMsg2OBU(tExit.sIUNo,0,"Car Park Full", "","","","");
        return;
    } 
    if (iRet == 6 && sIU.length()>10)
    {
        writelog ("season passback","OPR");
        SendMsg2Server("90",sIU+",,,,,Season Passback");
        ShowLEDMsg(tMsg.Msg_SeasonPassback[0],tMsg.Msg_SeasonPassback[1]);
        return;
    }
    if (iRet ==1 or iRet == 4 or iRet == 6) {
        // Duplicate due to FormatSeasonMsg() is called by CheckSeason() 
        //tEntry.iTransType = GetSeasonTransType(tEntry.iVehicleType,std::stoi(tSeason.rate_type), tEntry.iTransType);
        tProcess.giShowType = 0;
    }

    if (iRet == 10) {
        ShowLEDMsg(tMsg.Msg_SeasonAsHourly[0],tMsg.Msg_SeasonAsHourly[1]);
        tProcess.giShowType = 2;
    }
    if (iRet != 1) {
        if (tParas.giEPS != 3) {
            writelog ("Check EZ-Link Member","OPR");
            if (db::getInstance()->HasEZpay(sIU) == 1) ShowLEDMsg("EZ-Link Motoring^Service Nember","EZ-Link Motoring^Service Nember");
            else {
                if (db::getInstance()->HasAXS(sIU) == 1) ShowLEDMsg("AXS Drive Member^Please Proceed","AXS Drive Member^Please Proceed");
                else ShowLEDMsg(tMsg.Msg_WithIU[0],tMsg.Msg_WithIU[1]);
            }
        }
        ShowLEDMsg(tMsg.Msg_WithIU[0],tMsg.Msg_WithIU[1]);
        tEntry.iTransType = 1;
        tProcess.giShowType = 1;
    }
        //---------
    SaveEntry();
    tEntry.gbEntryOK = true;
    Openbarrier();
    //-------added on 03/12/2025
    //writelog ("testing eep1" + tParas.giEPS,"OPR");
}

void operation:: Setdefaultparameter()
{
    tParas = {0};

    tProcess.gsDefaultIU = "1096000001";
    tProcess.glNoofOfflineData = 0;
    tProcess.giSystemOnline = -1;
    tProcess.gbLastDBConnected = true;
    //clear error msg
     for (int i= 0; i< Errsize; ++i){
        tPBSError[i].ErrNo = 0;
        tPBSError[i].ErrCode =0;
	    tPBSError[i].ErrMsg = "";
    }
    tProcess.gbcarparkfull = false;
    tProcess.gbLoopApresent = false;
    tProcess.gbLoopAIsOn = false;
    tProcess.gbLoopBIsOn = false;
    tProcess.gbLoopCIsOn = false;
    tProcess.gbLorrySensorIsOn = false;
    //--------
    tProcess.giSyncTimeCnt = 0;
    tProcess.gbloadedParam = false;
	tProcess.gbloadedVehtype = false;
	tProcess.gbloadedLEDMsg = false;
    tProcess.gbloadedLEDExitMsg = false;
	tProcess.gbloadedStnSetup = false;
    //-------
    tProcess.gbInitParamFail = 0;
    tProcess.giCardIsIn = 0;
    //-------
    tProcess.giLastHousekeepingDate = 0;
    tProcess.gsLastIUNo = "";

    tProcess.lastIUEntryTime = std::chrono::steady_clock::now();
    tProcess.lastTransTime = std::chrono::steady_clock::now();
    //---
    tProcess.fbReadIUfromAnt = false;
    tProcess.fiLastEEPCmd = EEPClient:: CommandType::EEP_idle;
    tProcess.gsLastDebitFailTime = "";
    tProcess.gsLastPaidTrans = "";
	tProcess.gsLastCardNo = "";
	tProcess.gfLastCardBal = 0;
    tProcess.gbLastPaidStatus = false;
    tProcess.glLastSerialNo = 0;
    //----
    tProcess.gbUPOSStatus = Init;
    tProcess.giUPOSLoginCnt = 0;
    tProcess.giBarrierContinueOpened = 0;
}

std::string operation::getIPAddress()
{
    struct ifaddrs* ifAddrList = nullptr;

    std::string result;
    std::string broadcastIP;

    if (getifaddrs(&ifAddrList) == -1)
    {
        std::cerr << "Error in getifaddrs\n";
        return "";
    }

    // Read the output of the 'ifconfig' command
    // Legacy comment retained. Network information is now obtained using getifaddrs().
    for (struct ifaddrs* ifAddr = ifAddrList; ifAddr != nullptr; ifAddr = ifAddr->ifa_next)
    {
        if (ifAddr->ifa_addr == nullptr)
        {
            continue;
        }

        if (ifAddr->ifa_addr->sa_family != AF_INET)
        {
            continue;
        }

        if (std::strcmp(ifAddr->ifa_name, "eth0") != 0)
        {
            continue;
        }

        const auto* ipv4Address = reinterpret_cast<const struct sockaddr_in*>(ifAddr->ifa_addr);

        char ipBuffer[INET_ADDRSTRLEN] = {};

        if (inet_ntop(
                AF_INET,
                &(ipv4Address->sin_addr),
                ipBuffer,
                sizeof(ipBuffer)) == nullptr)
        {
            continue;
        }

        result = ipBuffer;

        if ((ifAddr->ifa_flags & IFF_BROADCAST) != 0 &&
            ifAddr->ifa_broadaddr != nullptr)
        {
            const auto* broadcastAddress = reinterpret_cast<const struct sockaddr_in*>(ifAddr->ifa_broadaddr);

            char broadcastBuffer[INET_ADDRSTRLEN] = {};

            if (inet_ntop(
                    AF_INET,
                    &(broadcastAddress->sin_addr),
                    broadcastBuffer,
                    sizeof(broadcastBuffer)) != nullptr)
            {
                broadcastIP = broadcastBuffer;
            }
        }

        if (broadcastIP.empty() &&
            ifAddr->ifa_netmask != nullptr)
        {
            const auto* netmaskAddress = reinterpret_cast<const struct sockaddr_in*>(ifAddr->ifa_netmask);
            const uint32_t ip = ntohl(ipv4Address->sin_addr.s_addr);
            const uint32_t netmask = ntohl(netmaskAddress->sin_addr.s_addr);

            struct in_addr calculatedBroadcast{};
            calculatedBroadcast.s_addr = htonl(ip | ~netmask);

            char broadcastBuffer[INET_ADDRSTRLEN] = {};

            if (inet_ntop(
                    AF_INET,
                    &calculatedBroadcast,
                    broadcastBuffer,
                    sizeof(broadcastBuffer)) != nullptr)
            {
                broadcastIP = broadcastBuffer;
            }
        }

        break;
    }

    freeifaddrs(ifAddrList);

    //------
    tParas.gsLocalIP = result;
    writelog ("local IP address: " + result, "OPR");

    //-----
    if (result.empty())
    {
        return "";
    }

    // Output the result
    return broadcastIP;
}

void operation:: Sendmystatus()
{
    //EPS error index:  0=antenna,     1=printer,   2=DB, 3=Reader, 4=UPOS
    //5=Param error, 6=DIO,7=Loop A hang,8=CHU, 9=ups, 10= LCSC
    //11= station door status, 12 = barrier door status, 13= TGD controll status
    //14 = TGD sensor status 15=Arm drop status,16=barrier status,17=ticket status d DateTime
    CE_Time dt;
	string str="";

    for (int i = 0; i < Errsize; ++i)
    {
        str += std::to_string(tPBSError[i].ErrNo) + ",";
    }

    str += "0;0," + dt.DateTimeNumberOnlyString() + ",";

	SendMsg2Server("00",str);
	
}

void operation::sendMyStatusToMonitor()
{
    //EPS error index:  0=antenna,     1=printer,   2=DB, 3=Reader, 4=UPOS
    //5=Param error, 6=DIO,7=Loop A hang,8=CHU, 9=ups, 10= LCSC
    //11= station door status, 12 = barrier door status, 13= TGD controll status
    //14 = TGD sensor status 15=Arm drop status,16=barrier status,17=ticket status d DateTime
    CE_Time dt;
	string str="";
    //-----
    for (int i = 0; i < Errsize; ++i)
    {
        str += std::to_string(tPBSError[i].ErrNo) + ",";
    }

    str += "0;0," + dt.DateTimeNumberOnlyString() + ",";

    SendMsg2Monitor("300", str);
}

void operation::syncCentralDBTime()
{
    db::getInstance()->synccentraltime();
}

void operation::FnSendDIOInputStatusToMonitor(int pinNum, int pinValue)
{
    postEvent(
        [this, pinNum, pinValue]()
        {
            const std::string data =
                std::to_string(pinNum) + "," +
                std::to_string(pinValue);

            SendMsg2Monitor("302", data);
        });
}

void operation::sendDateTimeToMonitor()
{
    std::string str = Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmm();
    SendMsg2Monitor("304", str);
}

void operation::FnSendLogMessageToMonitor(std::string msg)
{
    postEvent(
        [this, msg = std::move(msg)]()
        {
            if (monitorUdpClient_ == nullptr)
            {
                return;
            }

            if (!monitorUdpClient_->FnGetMonitorStatus())
            {
                return;
            }

            const std::string str =
                "[" +
                gtStation.sPCName + "|" +
                std::to_string(gtStation.iSID) + "|" +
                "305" + "|" +
                msg + "|]";

            monitorUdpClient_->send(str);
        });
}

void operation::FnSendMsg2Server(std::string cmdCode, std::string data)
{
    postEvent(
        [this,
         cmdCode = std::move(cmdCode),
         data = std::move(data)]() mutable
        {
            SendMsg2Server(std::move(cmdCode), std::move(data));
        });
}

void operation::FnSendLEDMessageToMonitor(std::string line1TextMsg, std::string line2TextMsg)
{
    postEvent(
        [this,
         line1TextMsg = std::move(line1TextMsg),
         line2TextMsg = std::move(line2TextMsg)]()
        {
            const std::string str = line1TextMsg + "," + line2TextMsg;
            SendMsg2Monitor("306", str);
        });
}

void operation::sendCmdDownloadParamAckToMonitor(bool success)
{
    std::string str = "99";

    if (!success)
    {
        str = "98";
    }

    SendMsg2Monitor("310", str);
}

void operation::sendCmdDownloadIniAckToMonitor(bool success)
{
    std::string str = "99";

    if (!success)
    {
        str = "98";
    }

    SendMsg2Monitor("309", str);
}

void operation::sendCmdGetStationCurrLogToMonitor()
{
    try
    {
        // Get today's date
        auto today = std::chrono::system_clock::now();
        auto todayDate = std::chrono::system_clock::to_time_t(today);
        std::tm* localToday = std::localtime(&todayDate);

        if (localToday == nullptr)
        {
            throw std::runtime_error("Failed to get local time.");
        }

        std::string logFilePath = Logger::getInstance()->LOG_FILE_PATH;

        // Extract year, month and day
        std::ostringstream ossToday;
        ossToday << std::setw(2) << std::setfill('0') << (localToday->tm_year % 100);
        ossToday << std::setw(2) << std::setfill('0') << (localToday->tm_mon + 1);
        ossToday << std::setw(2) << std::setfill('0') << localToday->tm_mday;

        std::string todayDateStr = ossToday.str();

        // Iterate through the files in the log file path
        int foundNo_ = 0;

        for (const auto& entry : std::filesystem::directory_iterator(logFilePath))
        {
            if ((entry.path().filename().string().find(todayDateStr) != std::string::npos) &&
                (entry.path().extension() == ".log"))
            {
                foundNo_++;
            }
        }

        bool copyFileFail = false;
        std::string details;

        if (PingWithTimeOut(IniParser::getInstance()->FnGetCentralDBServer(), 1, details) == true)
        {
            if (foundNo_ > 0)
            {
                std::stringstream ss;
                ss << "Found " << foundNo_ << " log files.";
                writelog(ss.str(), "OPR");

                // Create the mount poin directory if doesn't exist
                std::string mountPoint = "/mnt/logbackup";
                std::string sharedFolderPath = tParas.gsLogBackFolder;
                std::replace(sharedFolderPath.begin(), sharedFolderPath.end(), '\\', '/');
                std::string username = IniParser::getInstance()->FnGetCentralUsername();
                std::string password = IniParser::getInstance()->FnGetCentralPassword();

                if (!std::filesystem::exists(mountPoint))
                {
                    std::error_code ec;

                    if (!std::filesystem::create_directories(mountPoint, ec))
                    {
                        writelog(("Failed to create " + mountPoint + " directory : " + ec.message()), "OPR");
                        SendMsg2Monitor("314", "98");
                        return;
                    }
                    else
                    {
                        writelog(("Successfully to create " + mountPoint + " directory."), "OPR");
                    }
                }
                else
                {
                    writelog(("Mount point directory: " + mountPoint + " exists."), "OPR");
                }

                // Mount the shared folder
                {
                    MountManager mountManager(
                        sharedFolderPath,
                        mountPoint,
                        username,
                        password,
                        "",
                        "OPR");

                    if (!mountManager.isMounted())
                    {
                        writelog(("Failed to mount " + mountPoint), "OPR");
                        SendMsg2Monitor("314", "98");
                        return;
                    }
                    else
                    {
                        writelog(("Successfully to mount " + mountPoint), "OPR");
                    }

                    // Copy files to mount folder
                    for (const auto& entry : std::filesystem::directory_iterator(logFilePath))
                    {
                        if ((entry.path().filename().string().find(todayDateStr) != std::string::npos) &&
                            (entry.path().extension() == ".log"))
                        {
                            std::error_code ec;

                            std::filesystem::copy(
                                entry.path(),
                                std::filesystem::path(mountPoint) / entry.path().filename(),
                                std::filesystem::copy_options::overwrite_existing,
                                ec);

                            if (!ec)
                            {
                                std::stringstream ss;
                                ss << "Copy file : " << entry.path() << " successfully.";
                                writelog(ss.str(), "OPR");
                            }
                            else
                            {
                                std::stringstream ss;
                                ss << "Failed to copy log file : " << entry.path();
                                writelog(ss.str(), "OPR");

                                copyFileFail = true;
                                break;
                            }
                        }
                    }

                    // Unmount the shared folder
                    // MountManager will release the mount automatically.
                    // Unmount failure is logged by MountManager and does not
                    // change the log upload result.
                }

                if (copyFileFail)
                {
                    SendMsg2Monitor("314", "98");
                }
                else
                {
                    SendMsg2Monitor("314", "99");
                }
            }
            else
            {
                writelog("No Log files to upload.", "OPR");
                SendMsg2Monitor("314", "98");
            }
        }
        else
        {
            writelog("Log files failed to upload due to ping failed.", "OPR");
            SendMsg2Monitor("314", "98");
        }
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << " Exception: " << e.what();
        writelog(ss.str(), "OPR");
        SendMsg2Monitor("314", "98");
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << " Unknown Exception.";
        writelog(ss.str(), "OPR");
        SendMsg2Monitor("314", "98");
    }
}

bool operation::copyFiles(
    const std::string& mountPoint,
    const std::string& sharedFolderPath,
    const std::string& username,
    const std::string& password,
    const std::string& outputFolderPath)
{
    // Create the mount poin directory if doesn't exist
    if (!std::filesystem::exists(mountPoint))
    {
        std::error_code ec;

        if (!std::filesystem::create_directories(mountPoint, ec))
        {
            writelog(("Failed to create " + mountPoint + " directory : " + ec.message()), "OPR");
            return false;
        }
        else
        {
            writelog(("Successfully to create " + mountPoint + " directory."), "OPR");
        }
    }
    else
    {
        writelog(("Mount point directory: " + mountPoint + " exists."), "OPR");
    }

    // Mount the shared folder
    MountManager mountManager(
        sharedFolderPath,
        mountPoint,
        username,
        password,
        "",
        "OPR");

    if (!mountManager.isMounted())
    {
        writelog(("Failed to mount " + mountPoint), "OPR");
        return false;
    }
    else
    {
        writelog(("Successfully to mount " + mountPoint), "OPR");
    }

    // Create the output folder if it doesn't exist
    if (!std::filesystem::exists(outputFolderPath))
    {
        std::error_code ec;

        if (!std::filesystem::create_directories(outputFolderPath, ec))
        {
            writelog(("Failed to create " + outputFolderPath + " directory : " + ec.message()), "OPR");
            return false;
        }
        else
        {
            writelog(("Successfully to create " + outputFolderPath + " directory."), "OPR");
        }
    }
    else
    {
        writelog(("Output folder directory : " + outputFolderPath + " exists."), "OPR");
    }

    // Copy files to mount point
    bool foundIni = false;

    const std::filesystem::path folder(mountPoint);

    if (std::filesystem::exists(folder) &&
        std::filesystem::is_directory(folder))
    {
        for (const auto& entry : std::filesystem::directory_iterator(folder))
        {
            const std::string filename =
                entry.path().filename().string();

            if (std::filesystem::is_regular_file(entry)
                && (filename.size() >= 4) && (filename == "LinuxPBS.ini"))
            {
                foundIni = true;

                const std::filesystem::path dest_file =
                    std::filesystem::path(outputFolderPath) /
                    entry.path().filename();

                std::filesystem::copy(
                    entry.path(),
                    dest_file,
                    std::filesystem::copy_options::overwrite_existing);

                std::stringstream ss;
                ss << "Copy " << entry.path() << " to " << dest_file << " successfully";
                writelog(ss.str(), "OPR");
            }
        }
    }
    else
    {
        writelog("Folder doesn't exist or is not a directory.", "OPR");
        return false;
    }

    // Unmount the shared folder
    // MountManager will release the mount automatically when leaving this function.

    if (!foundIni)
    {
        writelog("Ini file not found.", "OPR");
        return false;
    }

    return true;
}

bool operation::CopyIniFile(
    const std::string& serverIpAddress,
    const std::string& stationID)
{
    if ((!serverIpAddress.empty()) && (!stationID.empty()))
    {
        std::string details;

        if (PingWithTimeOut(serverIpAddress, 1, details) == true)
        {
            std::string sharedFilePath =
                "//" +
                serverIpAddress +
                "/carpark/LinuxPBS/Ini/Stn" +
                stationID;

            std::stringstream ss;
            ss << "Ini Shared File Path : " << sharedFilePath;
            writelog(ss.str(), "OPR");

            return copyFiles(
                "/mnt/ini",
                sharedFilePath,
                IniParser::getInstance()->FnGetCentralUsername(),
                IniParser::getInstance()->FnGetCentralPassword(),
                "/home/root/carpark/Ini");
        }
        else
        {
            writelog("Failed to ping to Server IP address.", "OPR");
            return false;
        }
    }
    else
    {
        writelog("Server IP address or station ID empty.", "OPR");
        return false;
    }
}

void operation::SendMsg2Monitor(const std::string& cmdcode, const std::string& dstr)
{
    if (monitorUdpClient_ == nullptr)
    {
        return;
    }

    if (!monitorUdpClient_->FnGetMonitorStatus())
    {
        return;
    }

    std::string str =
        "[" +
        gtStation.sPCName +
        "|" +
        std::to_string(gtStation.iSID) +
        "|" +
        cmdcode +
        "|" +
        dstr +
        "|]";

    monitorUdpClient_->send(str);

    //----
    writelog("Message to Monitor: " + str,"OPR");
}

void operation::SendMsg2Server(const std::string& cmdcode, const std::string& dstr)
{
    std::string str =
        "[" +
        gtStation.sName +
        "|" +
        std::to_string(gtStation.iSID) +
        "|" +
        cmdcode +
        "|" +
        dstr +
        "|]";

    if (pmsUdpClient_ == nullptr)
    {
        return;
    }

    pmsUdpClient_->send(str);

    //----
    writelog ("Message to PMS: " + str,"OPR");
}

int operation::CheckSeason(const std::string& sIU, int iInOut)
{
    std::string sMsg;
    std::string sLCD;

    const int iRet = db::getInstance()->isvalidseason(sIU, iInOut, gtStation.iZoneID);

    //showLED message
    if (iRet != 8)
    {
        FormatSeasonMsg(iRet, sIU, sMsg, sLCD);
    }

    return iRet;
}

void operation::writelog(const std::string& sMsg, const std::string& soption)
{
    Logger::getInstance()->FnLog(sMsg, "", soption);
}

void operation::HandlePBSError(EPSError iEPSErr, int iErrCode)
{
    std::string sErrMsg;
    std::string sCmd;

    switch (iEPSErr)
    {
        case ParamOk:
        {
            if (tPBSError[iParam].ErrNo < 0)
            {
                sCmd = "200";
                sErrMsg = "Parameter OK";
            }

            tPBSError[iParam].ErrNo = 0;
            break;
        }

        case ParamError:
        {
            tPBSError[iParam].ErrNo = -1;
            tPBSError[iParam].ErrMsg = "Parameter Error";
            sCmd = "200";
            sErrMsg = tPBSError[iParam].ErrMsg;
            break;
        }

        case AntennaNoError:
        {
            if (tPBSError[iAntenna].ErrNo < 0)
            {
                sCmd = "03";
                sErrMsg = "Antenna OK";
            }

            tPBSError[iAntenna].ErrNo = 0;
            break;
        }

        case AntennaError:
        {
            tPBSError[iAntenna].ErrNo = -1;
            tPBSError[iAntenna].ErrCode = iErrCode;
            tPBSError[iAntenna].ErrMsg = "Antenna Error: " + std::to_string(iErrCode);
            sErrMsg = tPBSError[iAntenna].ErrMsg;
            sCmd = "03";
            break;
        }

        case AntennaPowerOnOff:
        {
            if (iErrCode == 1)
            {
                tPBSError[iAntenna].ErrNo = 0;
                tPBSError[iAntenna].ErrCode = 1;
                tPBSError[iAntenna].ErrMsg = "Antenna: Power ON";
            }
            else
            {
                tPBSError[iAntenna].ErrNo = -2;
                tPBSError[iAntenna].ErrCode = 0;
                tPBSError[iAntenna].ErrMsg = "Antenna Error: Power OFF";
            }

            sErrMsg = tPBSError[iAntenna].ErrMsg;
            sCmd = "03";
            break;
        }

        case PrinterNoError:
        {
            if (tPBSError[1].ErrNo < 0)
            {
                sCmd = "04";
                sErrMsg = "Printer OK";
            }

            tPBSError[1].ErrNo = 0;
            break;
        }

        case PrinterError:
        {
            tPBSError[1].ErrNo = -1;
            tPBSError[1].ErrMsg = "Printer Error";
            sCmd = "04";
            sErrMsg = tPBSError[1].ErrMsg;
            break;
        }

        case PrinterNoPaper:
        {
            tPBSError[1].ErrNo = -2;
            tPBSError[1].ErrMsg = "Printer Paper Low";
            sCmd = "04";
            sErrMsg = tPBSError[1].ErrMsg;
            break;
        }

        case DBNoError:
        {
            tPBSError[2].ErrNo = 0;
            tPBSError[2].ErrMsg = "DB OK";
            sErrMsg = tPBSError[2].ErrMsg;
            sCmd = "201";
            Sendmystatus();
            break;
        }

        case DBFailed:
        {
            tPBSError[2].ErrNo = -1;
            tPBSError[2].ErrMsg = "DB Error";
            sErrMsg = tPBSError[2].ErrMsg;
            sCmd = "201";
            Sendmystatus();
            break;
        }

        case DBUpdateFail:
        {
            tPBSError[2].ErrNo = -2;
            tPBSError[2].ErrMsg = "DB Update Error";
            sErrMsg = tPBSError[2].ErrMsg;
            sCmd = "201";
            Sendmystatus();
            break;
        }

        case UPOSNoError:
        {
            if (tPBSError[4].ErrNo < 0)
            {
                sCmd = "06";
                sErrMsg = "UPOS OK";
            }

            tPBSError[4].ErrNo = 0;
            break;
        }

        case UPOSError:
        {
            tPBSError[4].ErrNo = -1;
            tPBSError[4].ErrMsg = "UPOS Error";
            sCmd = "06";
            sErrMsg = tPBSError[4].ErrMsg;
            break;
        }

        case TariffError:
        {
            tPBSError[5].ErrNo = -1;
            sCmd = "08";
            sErrMsg = "5Tariff Error";
            break;
        }

        case TariffOk:
        {
            tPBSError[5].ErrNo = 0;
            sCmd = "08";
            sErrMsg = "5Tariff OK";
            break;
        }

        case HolidayError:
        {
            tPBSError[5].ErrNo = -2;
            sCmd = "08";
            sErrMsg = "5No Holiday Set";
            break;
        }

        case HolidayOk:
        {
            tPBSError[5].ErrNo = 0;
            sCmd = "08";
            sErrMsg = "5Holiday OK";
            break;
        }

        case DIOError:
        {
            tPBSError[6].ErrNo = -1;
            sCmd = "08";
            sErrMsg = "6DIO Error";
            break;
        }

        case DIOOk:
        {
            tPBSError[6].ErrNo = 0;
            sCmd = "08";
            sErrMsg = "6DIO OK";
            break;
        }

        case LoopAHang:
        {
            tPBSError[7].ErrNo = -1;
            sCmd = "08";
            sErrMsg = "7Loop A Hang";
            break;
        }

        case LoopAOk:
        {
            tPBSError[7].ErrNo = 0;
            sCmd = "08";
            sErrMsg = "7Loop A OK";
            break;
        }

        case LCSCNoError:
        {
            if (tPBSError[10].ErrNo < 0)
            {
                sCmd = "70";
                sErrMsg = "LCSC OK";
            }

            tPBSError[10].ErrNo = 0;
            break;
        }

        case LCSCError:
        {
            tPBSError[10].ErrNo = -1;
            tPBSError[10].ErrMsg = "LCSC Error";
            sCmd = "70";
            sErrMsg = tPBSError[10].ErrMsg;
            break;
        }

        case SDoorError:
        {
            tPBSError[11].ErrNo = -1;
            tPBSError[11].ErrMsg = "Station door open";
            sCmd = "71";
            sErrMsg = tPBSError[11].ErrMsg;
            break;
        }

        case SDoorNoError:
        {
            tPBSError[11].ErrNo = 0;
            sCmd = "71";
            sErrMsg = "Station door close";
            break;
        }

        case BDoorError:
        {
            tPBSError[12].ErrNo = -1;
            tPBSError[12].ErrMsg = "barrier door open";
            sCmd = "72";
            sErrMsg = tPBSError[12].ErrMsg;
            break;
        }

        case BDoorNoError:
        {
            tPBSError[12].ErrNo = 0;
            tPBSError[12].ErrMsg = "barrier door close";
            sCmd = "72";
            sErrMsg = tPBSError[12].ErrMsg;
            break;
        }

        case ReaderNoError:
        {
            tPBSError[iReader].ErrNo = 0;
            sCmd = "05";
            tPBSError[iReader].ErrMsg = "Card Reader OK";
            break;
        }

        case ReaderError:
        {
            tPBSError[iReader].ErrNo = -1;
            sCmd = "05";
            tPBSError[iReader].ErrMsg = "Card Reader Error";
            break;
        }

        case BarrierStatus:
        {
            tPBSError[iBarrierStatus].ErrNo = iErrCode;
            sCmd = "08";

            switch (iErrCode)
            {
                case 0:
                {
                    tPBSError[iBarrierStatus].ErrMsg = "Barrier Status: Closed";
                    sErrMsg = "9Barrier Status: Closed";
                    break;
                }

                case 1:
                {
                    tPBSError[iBarrierStatus].ErrMsg = "Barrier Status: Open";
                    sErrMsg = "9Barrier Status: Open";
                    break;
                }

                case 2:
                {
                    tPBSError[iBarrierStatus].ErrMsg = "Barrier Status: Open Too Long";
                    sErrMsg = "9Barrier Status: Open Too Long";
                    break;
                }

                case 3:
                {
                    tPBSError[iBarrierStatus].ErrMsg = "Barrier Status: Arm Drop Down";
                    sErrMsg = "9Barrier Status: Arm Drop Down";
                    SendMsg2Server("90", ",,,,,barrierarmdrop");
                    break;
                }

                case 4:
                {
                    tPBSError[iBarrierStatus].ErrMsg = "Barrier Status: Fail To Open";
                    sErrMsg = "9Barrier Status: Fail To Open";
                    break;
                }

                case 5:
                {
                    tPBSError[iBarrierStatus].ErrMsg = "Barrier Status: Fail To Close";
                    sErrMsg = "9Barrier Status: Fail To Close";
                    break;
                }

                default:
                {
                    tPBSError[iBarrierStatus].ErrMsg = "Barrier Status: Unknown";
                    sErrMsg = "9Barrier Status: Unknown";
                    break;
                }
            }

            break;
        }

        default:
            break;
    }

    if (!sErrMsg.empty())
    {
        writelog (sErrMsg, "OPR");
        SendMsg2Server(sCmd, sErrMsg);
        //-------
        Sendmystatus();
    }
}

int operation::GetVTypeFromLoop()
{
    //car: 1 M/C: 7 Lorry: 4
    int ret = 1;
    if (DIO::getInstance()->FnGetLorrySensor())
    {
        writelog ("Vehicle Type is Lorry.", "OPR");
        ret = 4;
    }
    else
    {
        if (DIO::getInstance()->FnGetLoopBStatus() && DIO::getInstance()->FnGetLoopAStatus())
        {
            writelog ("Vehicle Type is car.", "OPR");
            ret = 1;
        }
        else
        {
            writelog ("Vehicle Type is M/C.", "OPR");
            ret = 7;
        }
    }
    return ret;

}

void operation::SaveEntry()
{
    int iRet;
    std::string sLPRNo = "";
    
    if (tEntry.sIUTKNo== "") return;
    writelog ("Save Entry trans:"+ tEntry.sIUTKNo, "OPR");

    iRet = db::getInstance()->insertentrytrans(tEntry);
    //----
    if (iRet == iDBSuccess)
    {
        tProcess.gsLastIUNo = tEntry.sIUTKNo;
        tProcess.lastIUEntryTime = std::chrono::steady_clock::now();
    }
    //-------
    tPBSError[iDB].ErrNo = (iRet == iDBSuccess) ? 0 : (iRet == iCentralFail) ? -1 : -2;

    if ((tEntry.sLPN[0] != "")|| (tEntry.sLPN[1] != ""))
	{
		if((tEntry.iTransType == 7) || (tEntry.iTransType == 8) || (tEntry.iTransType == 22))
		{
			sLPRNo = tEntry.sLPN[1];
		}
		else
		{
			sLPRNo = tEntry.sLPN[0];
		}
	}

    std::string sMsg2Send = (iRet == iDBSuccess) ? "Entry OK" : (iRet == iCentralFail) ? "Entry Central Failed" : "Entry Local Failed";

    sMsg2Send = tEntry.sIUTKNo + ",,," + sLPRNo + "," + std::to_string(tProcess.giShowType) + "," + sMsg2Send;

    if (tEntry.iStatus == 0) {
        SendMsg2Server("90", sMsg2Send);
    }
    tProcess.gbsavedtrans = true;
}

void operation::ShowTotalLots(const std::string& totallots, const std::string& LEDId)
{
    auto* led = LEDManager::getInstance()->getLED(getSerialPort(std::to_string(tParas.giCommportLED401)));

    if (led != nullptr)
    {
        writelog ("Total Lot:"+ totallots,"OPR");

        led->FnLEDSendLEDMsg(LEDId, totallots, LED::Alignment::RIGHT);
    }

    if (tParas.giEPS == 3) 
    {
        EEPClient::getInstance()->FnSendSetCarparkAvailabilityReq(totallots, std::to_string(gtStation.iZoneLots));
    }
}

void operation::FormatSeasonMsg(
    int iReturn,
    const std::string& sNo,
    std::string sMsg,
    std::string sLCD,
    int iExpires)
{
    std::string sMsgPartialSeason;

    const int seasonRateType = std::stoi(tSeason.rate_type);

    if (gtStation.iType == tientry)
    {
        tEntry.iTransType = GetSeasonTransType(tEntry.iVehicleType, seasonRateType, tEntry.iTransType);
        const int giSeasonTransType = tEntry.iTransType;

        if (giSeasonTransType > 49)
        {
            sMsgPartialSeason = db::getInstance()->GetPartialSeasonMsg(giSeasonTransType);

            writelog("partial season msg:" + sMsgPartialSeason + ", trans type: " + std::to_string(giSeasonTransType), "OPR");
        }
    }
    else
    {
        tExit.iTransType = GetSeasonTransType(tExit.iVehicleType, seasonRateType, tExit.iTransType);
        const int giSeasonTransType = tExit.iTransType;

        if (giSeasonTransType > 49)
        {
            sMsgPartialSeason = db::getInstance()->GetPartialSeasonMsg(giSeasonTransType);

            writelog("partial season msg:" + sMsgPartialSeason + ", trans type: " + std::to_string(giSeasonTransType), "OPR");
        }
    }

    tProcess.giShowType = 0;

    if (sMsgPartialSeason.empty())
    {
        sMsgPartialSeason = "Season";
    }

    switch (iReturn)
    {
        case -1:
        {
            writelog("DB error when Check season", "OPR");
            break;
        }

        case 0:
        {
            if (gtStation.iType == tientry)
            {
                sMsg = tMsg.Msg_SeasonInvalid[0];
                sLCD = tMsg.Msg_SeasonInvalid[1];
            }
            else
            {
                sMsg = tExitMsg.MsgExit_SeasonInvalid[0];
                sLCD = tExitMsg.MsgExit_SeasonInvalid[1];
            }
            break;
        }

        case 1:
        {
            if (gtStation.iType == tientry)
            {
                sMsg = tMsg.Msg_ValidSeason[0];
                sLCD = tMsg.Msg_ValidSeason[1];
            }
            else
            {
                sMsg = tExitMsg.MsgExit_XValidSeason[0];
                sLCD = tExitMsg.MsgExit_XValidSeason[1];
            }
            break;
        }

        case 2:
        {
            if (gtStation.iType == tientry)
            {
                sMsg = tMsg.Msg_SeasonExpired[0];
                sLCD = tMsg.Msg_SeasonExpired[1];
            }
            else
            {
                sMsg = tExitMsg.MsgExit_SeasonExpired[0];
                sLCD = tExitMsg.MsgExit_SeasonExpired[1];
            }

            writelog("Season Expired", "OPR");
            break;
        }

        case 3:
        {
            if (gtStation.iType == tientry)
            {
                sMsg = tMsg.Msg_SeasonTerminated[0];
                sLCD = tMsg.Msg_SeasonTerminated[1];
            }
            else
            {
                sMsg = tExitMsg.MsgExit_SeasonTerminated[0];
                sLCD = tExitMsg.MsgExit_SeasonTerminated[1];
            }

            writelog("Season terminated", "OPR");
            break;
        }

        case 4:
        {
            if (gtStation.iType == tientry)
            {
                sMsg = tMsg.Msg_SeasonBlocked[0];
                sLCD = tMsg.Msg_SeasonBlocked[1];
            }
            else
            {
                sMsg = tExitMsg.MsgExit_SeasonBlocked[0];
                sLCD = tExitMsg.MsgExit_SeasonBlocked[1];
            }

            writelog("Season Blocked", "OPR");
            break;
        }

        case 5:
        {
            if (gtStation.iType == tientry)
            {
                sMsg = tMsg.Msg_SeasonInvalid[0];
                sLCD = tMsg.Msg_SeasonInvalid[1];
            }
            else
            {
                sMsg = tExitMsg.MsgExit_SeasonInvalid[0];
                sLCD = tExitMsg.MsgExit_SeasonInvalid[1];
            }

            writelog("Season Lost", "OPR");
            break;
        }

        case 6:
        {
            if (gtStation.iType == tientry)
            {
                sMsg = tMsg.Msg_SeasonPassback[0];
                sLCD = tMsg.Msg_SeasonPassback[1];
            }
            else
            {
                sMsg = tExitMsg.MsgExit_SeasonBlocked[0];
                sLCD = tExitMsg.MsgExit_SeasonBlocked[1];
            }

            writelog("Season Passback", "OPR");
            break;
        }

        case 7:
        {
            if (gtStation.iType == tientry)
            {
                sMsg = tMsg.Msg_SeasonNotStart[0];
                sLCD = tMsg.Msg_SeasonNotStart[1];
            }
            else
            {
                sMsg = tExitMsg.MsgExit_SeasonNotStart[0];
                sLCD = tExitMsg.MsgExit_SeasonNotStart[1];
            }

            writelog("Season Not Start", "OPR");
            break;
        }

        case 8:
        {
            sMsg = "Wrong Season Type";
            sLCD = "Wrong Season Type";
            writelog("Wrong Season Type", "OPR");
            break;
        }

        case 9:
        {
            sMsg = "Complimentary";
            sLCD = "Complimentary";
            writelog("Complimentary!", "OPR");
            break;
        }

        case 10:
        {
            sMsg = tMsg.Msg_SeasonAsHourly[0];
            sLCD = tMsg.Msg_SeasonAsHourly[1];
            writelog("Season As Hourly", "OPR");
            break;
        }

        case 11:
        {
            if (gtStation.iType == tientry)
            {
                sMsg = tMsg.Msg_ESeasonWithinAllowance[0];
                sLCD = tMsg.Msg_ESeasonWithinAllowance[1];
            }
            else
            {
                sMsg = tExitMsg.MsgExit_XSeasonWithinAllowance[0];
                sLCD = tExitMsg.MsgExit_XSeasonWithinAllowance[1];
            }

            writelog("Season within allowance", "OPR");
            break;
        }

        case 12:
        {
            sMsg = tExitMsg.MsgExit_MasterSeason[0];
            sLCD = tExitMsg.MsgExit_MasterSeason[1];
            writelog("Master Season", "OPR");
            break;
        }

        case 13:
        {
            sMsg = tMsg.Msg_WholeDayParking[0];
            sLCD = tMsg.Msg_WholeDayParking[1];
            writelog("Whole Day Season", "OPR");
            break;
        }

        default:
            break;
    }

    size_t pos = sMsg.find("Season");

    if (pos != std::string::npos)
    {
        sMsg.replace(pos, 6, sMsgPartialSeason);
    }

    pos = sLCD.find("Season");

    if (pos != std::string::npos)
    {
        sLCD.replace(pos, 6, sMsgPartialSeason);
    }

    if (iReturn == 1 && seasonRateType != 0)
    {
        sMsg = sMsgPartialSeason;
        sLCD = sMsgPartialSeason;
    }

    ShowLEDMsg(sMsg, sLCD);

    if (iReturn != 1 || seasonRateType != 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void operation::ManualOpenBarrier(bool bPMS)
{
    auto* common = Common::getInstance();
    auto* database = db::getInstance();

    if (bPMS)
    {
        writelog ("Manual open barrier by PMS", "OPR");
    }
    else
    {
        writelog ("Manual open barrier by operator", "OPR");
    }

    //------------
    if (gtStation.iType == tientry
        && !tEntry.sIUTKNo.empty()
        && tProcess.gbLoopApresent)
    {
        tEntry.sEntryTime = common->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
        tEntry.iStatus = 4;

        //---------
        database->AddRemoteControl(std::to_string(gtStation.iSID), "Manual open barrier", "Auto save for IU:" + tEntry.sIUTKNo);

        SaveEntry();
        Openbarrier();
        return;
    }
    else
    {
        if (!tEntry.sIUTKNo.empty() && tProcess.gbLoopApresent)
        {
            if (tExit.sExitTime.empty())
            {
                tExit.sExitTime = common->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
            }

            database->AddRemoteControl(std::to_string(gtStation.iSID), "Manual open barrier", "Auto save for IU:" + tExit.sIUNo);

            CloseExitOperation(Manualopen);
            return;
        }
    }
    Openbarrier();
}

void operation::ManualCloseBarrier()
{
    writelog ("Manual Close barrier.", "OPR");
    db::getInstance()->AddRemoteControl(std::to_string(gtStation.iSID), "Manual close barrier", "");
    
    closeBarrier();
}

int operation:: GetSeasonTransType(int VehicleType, int SeasonType, int TransType)
 {
    // VehicleType: 0=car, 1=lorry, 2=motorcycle
    // SeasonType(rate_type of season_mst): 3=Day, 4=Night, 5=GRO, 6=Park & Ride
    // TransType: TransType for Wholeday season

    //writelog ("Season Rate Type:"+std::to_string(SeasonType),"OPR");

    if (SeasonType == 3) {
        if (VehicleType == 0) {
            return 50; // car day season
        } else if (VehicleType == 1) {
            return 51; // Lorry day season
        } else if (VehicleType == 2) {
            return 52; // motorcycle day season
        }
    } else if (SeasonType == 4) {
        if (VehicleType == 0) {
            return 53; // car night season
        } else if (VehicleType == 1) {
            return 54; // Lorry night season
        } else if (VehicleType == 2) {
            return 55; // motorcycle night season
        }
    } else if (SeasonType == 5) {
        if (VehicleType == 0) {
            return 56; // car GRO season
        } else if (VehicleType == 1) {
            return 57; // Lorry GRO season
        } else if (VehicleType == 2) {
            return 58; // motorcycle GRO season
        }
    } else if (SeasonType == 6) {
        if (VehicleType == 0) {
            return 59; // Car Park Ride Season
        } else if (VehicleType == 1) {
            return 60; // Lorry Park Ride season
        } else if (VehicleType == 2) {
            return 61; // motorcycle Park Ride Season
        }
    } else if (SeasonType == 7) {
        if (VehicleType == 0 || VehicleType == 14) {
            return 62; // Car Handicapped Season
        } else if (VehicleType == 1) {
            return 63; // Lorry Handicapped season
        } else if (VehicleType == 2) {
            return 64; // motorcycle Handicapped Season
        }
    } else if (SeasonType == 8) {
        if (VehicleType == 0) {
            return 65; // Staff A Car
        } else if (VehicleType == 1) {
            return 66; // staff A Lorry
        } else if (VehicleType == 2) {
            return 67; // Staff A Motorcycle
        }
    } else if (SeasonType == 9) {
        if (VehicleType == 0) {
            return 68; // Staff B Car
        } else if (VehicleType == 1) {
            return 69; // staff B Lorry
        } else if (VehicleType == 2) {
            return 70; // Staff B Motorcycle
        }
    } else if (SeasonType == 10) {
        if (VehicleType == 0) {
            return 71; // Staff C Car
        } else if (VehicleType == 1) {
            return 72; // staff C Lorry
        } else if (VehicleType == 2) {
            return 73; // Staff C Motorcycle
        }
    } else if (SeasonType == 11) {
        if (VehicleType == 0) {
            return 74; // Staff D Car
        } else if (VehicleType == 1) {
            return 75; // staff D Lorry
        } else if (VehicleType == 2) {
            return 76; // Staff D Motorcycle
        }
    } else if (SeasonType == 12) {
        if (VehicleType == 0) {
            return 77; // Staff E Car
        } else if (VehicleType == 1) {
            return 78; // staff E Lorry
        } else if (VehicleType == 2) {
            return 79; // Staff E Motorcycle
        }
    } else if (SeasonType == 13) {
        if (VehicleType == 0) {
            return 80; // Staff F Car
        } else if (VehicleType == 1) {
            return 81; // staff F Lorry
        } else if (VehicleType == 2) {
            return 82; // Staff F Motorcycle
        }
    } else if (SeasonType == 14) {
        if (VehicleType == 0) {
            return 83; // Staff G Car
        } else if (VehicleType == 1) {
            return 84; // staff G Lorry
        } else if (VehicleType == 2) {
            return 85; // Staff G Motorcycle
        }
    } else if (SeasonType == 15) {
        if (VehicleType == 0) {
            return 86; // Staff H Car
        } else if (VehicleType == 1) {
            return 87; // staff H Lorry
        } else if (VehicleType == 2) {
            return 88; // Staff H Motorcycle
        }
    } else if (SeasonType == 16) {
        if (VehicleType == 0) {
            return 89; // Staff I Car
        } else if (VehicleType == 1) {
            return 90; // staff I Lorry
        } else if (VehicleType == 2) {
            return 91; // Staff I Motorcycle
        }
    } else if (SeasonType == 17) {
        if (VehicleType == 0) {
            return 92; // Staff J Car
        } else if (VehicleType == 1) {
            return 93; // staff J Lorry
        } else if (VehicleType == 2) {
            return 94; // Staff J Motorcycle
        }
    } else if (SeasonType == 18) {
        if (VehicleType == 0) {
            return 95; // Staff K Car
        } else if (VehicleType == 1) {
            return 96; // staff K Lorry
        } else if (VehicleType == 2) {
            return 97; // Staff K Motorcycle
        }
    } else if (TransType == 9) {
        return 9;
    } 
    return TransType + 1; // Whole day season
}

void operation::EnableCashcard(bool bEnable)
{
    if (bEnable == tProcess.sEnableReader)
    {
        return;
    }

    tProcess.sEnableReader = bEnable;

    //------ added on 18/08/2026
    if (!tProcess.gsTransID.empty() &&
        (gtStation.iType == tientry || tExit.sIUNo.empty()))
    {
        std::string sLPN;
        std::string sIU;

        if (gtStation.iType == tientry)
        {
            sLPN = tEntry.sLPN[0];
        }
        else
        {
            sLPN = tExit.sLPN[0];
        }

        if (!sLPN.empty() && sLPN != "0000000000")
        {
            sIU = db::getInstance()->GetIUByLPN(sLPN);
        }

        if (!sIU.empty() && sIU.length() == 10)
        {
            writelog ("Get IU from DB : " + sIU + " based on LPR: " + sLPN, "OPR");

            VehicleCome(sIU);
            return;
        }
    }

    //------------
    if (!tExit.bPayByAXS &&
        !tExit.bPayByEZPay)
    {
        EnableLCSC(bEnable);
        EnableKDE(bEnable);
        EnableUPOS(bEnable);
    }

    if (bEnable)
    {
        BARCODE_READER::getInstance()->FnBarcodeStartRead();
    }
    else
    {
        BARCODE_READER::getInstance()->FnBarcodeStopRead();
    }
}

void operation::CheckReader()
{
    auto* ksmReader = KSM_Reader::getInstance();
    auto* lcscReader = LCSCReader::getInstance();
    auto* upt = Upt::getInstance();

    if (tPBSError[iReader].ErrNo == -1)
    {
        writelog("Check KDE Status ...", "OPR");
        ksmReader->FnKSMReaderSendInit();
    }

    if (tParas.giCommPortLCSC > 0 &&
        tPBSError[iLCSC].ErrNo != -4)
    {
        writelog("Check LCSC Status...", "OPR");

        lcscReader->FnSendGetStatusCmd();

        if (tPBSError[iLCSC].ErrNo == -1)
        {
            tPBSError[iLCSC].ErrNo = 0;
        }

        lcscReader->FnSendSetTime();
    }

    if (tParas.giCommPortUPOS &&
        tProcess.gbUPOSStatus != Init)
    {
        writelog("Check UPOS Status...", "OPR");

        upt->FnUptSendDeviceStatusRequest();

        if (tPBSError[iUPOS].ErrNo == -1)
        {
            tPBSError[iUPOS].ErrNo = 0;
        }
    }
}

void operation::EnableLCSC(bool bEnable)
{
    auto* lcscReader = LCSCReader::getInstance();

    if (tParas.giCommPortLCSC == 0)
    {
        return;
    }

    //------ added on 15/07/2026
    lcscReader->LCSCCard_In = 0;
    //-----

    if (tPBSError[iLCSC].ErrNo != 0)
    {
        return;
    }

    if (bEnable)
    {
        lcscReader->FnSendGetCardIDCmd();
        writelog("Start LCSC to read...", "OPR");
    }
    else
    {
        lcscReader->FnLCSCReaderStopRead();
        writelog("Stop LCSC to Read...", "OPR");
    }
}

void operation::EnableKDE(bool bEnable)
{
    auto* ksmReader = KSM_Reader::getInstance();

    if (tParas.giCommPortKDEReader == 0)
    {
        return;
    }

    if (tPBSError[iReader].ErrNo != 0)
    {
        return;
    }

    //-----
    if (!bEnable)
    {
        //------
        if (tProcess.giCardIsIn == 1)
        {
            ksmReader->FnKSMReaderSendEjectToFront();
        }
        else
        {
            writelog ("Disable KDE Reader", "OPR");
            ksmReader->FnKSMReaderEnable(bEnable);
        }
    }
    else
    {
        writelog ("Enable KDE Reader", "OPR");
        ksmReader->FnKSMReaderEnable(bEnable);
    }
}

void operation::EnableUPOS(bool bEnable)
{
    auto* upt = Upt::getInstance();

    if (tParas.giCommPortUPOS == 0)
    {
        return;
    }

    if (tProcess.gbUPOSStatus == Init)
    {
        writelog("Wating for UPOS log on", "OPR");
        return;
    }

    //--------
    if (bEnable)
    {
        if (tProcess.gbUPOSStatus == Enable)
        {
            return;
        }

        if (tProcess.gbUPOSStatus != ReadCardTimeout)
        {
            writelog("Send Card Detect Request to UPOS", "OPR");
        }

        upt->FnUptSendCardDetectRequest();

        tProcess.gbUPOSStatus = Enable;
        upt->UOPSCard_In = 0;
    }
    else
    {
        if (tProcess.gbUPOSStatus == Disable)
        {
            return;
        }

        writelog ("Disable UPOS Reader", "OPR");

        upt->FnUptSendDeviceCancelCommandRequest();

        tProcess.gbUPOSStatus = Disable;
    }
}

void operation::ProcessBarcodeData(string sBarcodedata)
{
    ticketScan(sBarcodedata);
    setLastActionTimeAfterLoopA();
}

void operation::ProcessLCSC(const std::string& eventData)
{
    //writelog ("Received Command from LCSC:" + eventData, "LCSC");

    int msg_status = static_cast<int>(LCSCReader::mCSCEvents::sWrongCmd);

    try
    {
        std::vector<std::string> subVector = Common::getInstance()->FnParseString(eventData, ',');
        for (unsigned int i = 0; i < subVector.size(); i++)
        {
            std::string pair = subVector[i];
            std::string param = Common::getInstance()->FnBiteString(pair, '=');
            std::string value = pair;

            if (param == "msgStatus")
            {
                msg_status = std::stoi(value);
            }
        }
    }
    catch (const std::exception& ex)
    {
        std::ostringstream oss;
        oss << "Exception : " << ex.what();
        writelog(oss.str(), "OPR");
    }

    switch (static_cast<LCSCReader::mCSCEvents>(msg_status))
    {
        case LCSCReader::mCSCEvents::sGetStatusOK:
        {
            int reader_mode = 0;
            try
            {
                std::vector<std::string> subVector = Common::getInstance()->FnParseString(eventData, ',');
                for (unsigned int i = 0; i < subVector.size(); i++)
                {
                    std::string pair = subVector[i];
                    std::string param = Common::getInstance()->FnBiteString(pair, '=');
                    std::string value = pair;

                    if (param == "readerMode")
                    {
                        reader_mode = std::stoi(value);
                    }
                }
            }
            catch (const std::exception& ex)
            {
                std::ostringstream oss;
                oss << "Exception : " << ex.what();
                writelog(oss.str(), "OPR");
            }

            if (reader_mode != 1)
            {
                LCSCReader::getInstance()->FnSendGetLoginCmd();
                writelog ("LCSC Reader Error","OPR");
                HandlePBSError(LCSCError);
            }

            break;
        }
        case LCSCReader::mCSCEvents::sLoginSuccess:
        {
            break;
        }
        case LCSCReader::mCSCEvents::sLogoutSuccess:
        {
            break;
        }
        case LCSCReader::mCSCEvents::sGetIDSuccess:
        {
            if (gtStation.iType == tiExit)  break;

            writelog ("event LCSC got card ID.","OPR");
            HandlePBSError (LCSCNoError);
            setLastActionTimeAfterLoopA();

            std::string sCardNo = "";
            
            try
            {
                std::vector<std::string> subVector = Common::getInstance()->FnParseString(eventData, ',');
                for (unsigned int i = 0; i < subVector.size(); i++)
                {
                    std::string pair = subVector[i];
                    std::string param = Common::getInstance()->FnBiteString(pair, '=');
                    std::string value = pair;

                    if (param == "CAN")
                    {
                        sCardNo = value;
                    }
                }
            }
            catch (const std::exception& ex)
            {
                std::ostringstream oss;
                oss << "Exception : " << ex.what();
                writelog(oss.str(), "OPR");
            }

            writelog ("LCSC card: " + sCardNo, "OPR");

            if (sCardNo.length() != 16)
            {
                writelog ("Wrong Card No: "+sCardNo, "OPR");
                ShowLEDMsg(tMsg.Msg_CardReadingError[0], tMsg.Msg_CardReadingError[1]);
                SendMsg2Server ("90", sCardNo + ",,,,,Wrong Card No");
                EnableLCSC(true);
            }
            else
            {
                if (tEntry.sIUTKNo == "") 
                {
                    EnableCashcard(false);
                    VehicleCome(sCardNo);
                }

            }
            break;
        }
        case LCSCReader::mCSCEvents::sGetBlcSuccess:
        {
            
            if (gtStation.iType == tientry)  break;

            writelog ("event LCSC got ID and balance.","OPR");
            HandlePBSError (LCSCNoError);
            setLastActionTimeAfterLoopA();

            std::string card_serial_num = "";
            std::string sCardNo = "";
            std::string sBal = "";
            
            try
            {
                std::vector<std::string> subVector = Common::getInstance()->FnParseString(eventData, ',');
                for (unsigned int i = 0; i < subVector.size(); i++)
                {
                    std::string pair = subVector[i];
                    std::string param = Common::getInstance()->FnBiteString(pair, '=');
                    std::string value = pair;

                    if (param == "CSN")
                    {
                        card_serial_num = value;
                    }

                    if (param == "CAN")
                    {
                        sCardNo = value;
                    }

                    if (param == "cardBalance")
                    {
                        sBal = value;
                    }
                }
            }
            catch (const std::exception& ex)
            {
                std::ostringstream oss;
                oss << "Exception : " << ex.what();
                writelog(oss.str(), "OPR");
            }

            writelog ("LCSC card: " + sCardNo + ", Bal=$" + Common::getInstance()->FnFormatToFloatString(sBal), "OPR");

            if(sCardNo.length() != 16)
            {
                writelog ("Wrong Card No: "+sCardNo, "OPR");
                ShowLEDMsg(tMsg.Msg_CardReadingError[0], tMsg.Msg_CardReadingError[1]);
                SendMsg2Server ("90", sCardNo + ",,,,,Wrong Card No");
                EnableLCSC(true);
            }
            else
            {
                EnableCashcard(false);
                Antenna::getInstance()->FnAntennaStopRead();
                tExit.iCardStatus = 0;
                CheckIUorCardStatus(sCardNo, LCSC, sCardNo,1, std::stof(sBal)/100);
            }

            break;
        }
        case LCSCReader::mCSCEvents::sGetTimeSuccess:
        {
            break;
        }
        case LCSCReader::mCSCEvents::sGetDeductSuccess:
        {
            if (gtStation.iType == tientry)  break;

            writelog ("event LCSC get deduction success.","OPR");
            HandlePBSError (LCSCNoError);
            setLastActionTimeAfterLoopA();

            std::string seed = "";
            std::string card_serial_num = "";
            std::string sCardNo = "";
            std::string sBalanceAfterTrans = "";
            
            try
            {
                std::vector<std::string> subVector = Common::getInstance()->FnParseString(eventData, ',');
                for (unsigned int i = 0; i < subVector.size(); i++)
                {
                    std::string pair = subVector[i];
                    std::string param = Common::getInstance()->FnBiteString(pair, '=');
                    std::string value = pair;

                    if (param == "seed")
                    {
                        seed = value;
                    }

                    if (param == "CSN")
                    {
                        card_serial_num = value;
                    }

                    if (param == "CAN")
                    {
                        sCardNo = value;
                    }

                    if (param == "BalanceAfterTrans")
                    {
                        sBalanceAfterTrans = value;
                    }
                }
            }
            catch (const std::exception& ex)
            {
                std::ostringstream oss;
                oss << "Exception : " << ex.what();
                writelog(oss.str(), "OPR");
            }

            std::ostringstream oss;
            oss << "LCSC deduct successfully: Card No: " << sCardNo << ", Balance After Deduction: $" << Common::getInstance()->FnFormatToFloatString(sBalanceAfterTrans);
            writelog(oss.str(), "OPR");
            //-------
            DebitOK("", sCardNo, "", Common::getInstance()->FnFormatToFloatString(sBalanceAfterTrans), 1, "", LCSC, "");
            break;
        }
        case LCSCReader::mCSCEvents::sGetCardRecord:
        {
            writelog ("event LCSC get card record.","OPR");
            HandlePBSError (LCSCNoError);

            std::string sSeed = "";
            uint32_t iSeed = 0;
            
            try
            {
                std::vector<std::string> subVector = Common::getInstance()->FnParseString(eventData, ',');
                for (unsigned int i = 0; i < subVector.size(); i++)
                {
                    std::string pair = subVector[i];
                    std::string param = Common::getInstance()->FnBiteString(pair, '=');
                    std::string value = pair;

                    if (param == "seed")
                    {
                        sSeed = value;
                    }
                }

                iSeed = std::stoul(sSeed, nullptr, 16);
                LCSCReader::getInstance()->FnSendCardFlush(iSeed);
            }
            catch (const std::exception& ex)
            {
                std::ostringstream oss;
                oss << "Exception : " << ex.what();
                writelog(oss.str(), "OPR");
            }
            break;
        }
        case LCSCReader::mCSCEvents::sCardFlushed:
        {
            if (tProcess.gbBarrierOpened == false)
            {
                writelog("LCSC command CardFlushed.","OPR");
                //--- handle no card
                RetryLCSCLastCommand();
            }
           
            break;
        }

        case LCSCReader::mCSCEvents::sSetTimeSuccess:
        {
            break;
        }
        case LCSCReader::mCSCEvents::sLogin1Success:
        {
            break;
        }
        case LCSCReader::mCSCEvents::sRSAUploadSuccess:
        {
            break;
        }
        case LCSCReader::mCSCEvents::sFWUploadSuccess:
        {
            break;
        }
        case LCSCReader::mCSCEvents::sBLUploadSuccess:
        case LCSCReader::mCSCEvents::sCILUploadSuccess:
        case LCSCReader::mCSCEvents::sCFGUploadSuccess:
        case LCSCReader::mCSCEvents::sBLUploadCorrupt:
        case LCSCReader::mCSCEvents::sCILUploadCorrupt:
        case LCSCReader::mCSCEvents::sCFGUploadCorrupt:
        {
            writelog("Received ACK from LCSC.", "OPR");
            break;
        }
        case LCSCReader::mCSCEvents::iFailWriteSettle:
        {
            writelog("Write LCSC settle file failed.","OPR");
            break;
        }
        case LCSCReader::mCSCEvents::sNoCard:
        {
            writelog("Received No card Event", "OPR");
            RetryLCSCLastCommand();
            break;
        }
        case LCSCReader::mCSCEvents::sTimeout:
        {
            writelog("LCSC command timeout.","OPR");
            //--- handle no card
            RetryLCSCLastCommand();
            break;
        }
        case LCSCReader::mCSCEvents::sSendcmdfail:
        {
            writelog("LCSC send command failed.","OPR");
            RetryLCSCLastCommand();
            break;
        }
        case LCSCReader::mCSCEvents::rNotRespCmd:
        {
            writelog("Not the LCSC command response.","OPR");
            RetryLCSCLastCommand();
            break;
        }
        case LCSCReader::mCSCEvents::sRecordNotFlush:
        {
            writelog("LCSC record not flushed. Sending get Card Record.", "OPR");
            LCSCReader::getInstance()->FnSendCardRecord();
            break;
        }
        case LCSCReader::mCSCEvents::sExpiredCard:
        {
            writelog("Card Expired.", "OPR");
            tExit.giDeductionStatus = CardExpired;
            ShowLEDMsg("Card Expired!", "Card Expired!");
            SendMsg2Server ("90", tProcess.gsLastCardNo + ",,,,,Card Expired");
            EnableCashcard(true);
            setLastActionTimeAfterLoopA();
            break;
        }
        default:
        {
            writelog ("Received Error Event" + std::to_string(static_cast<int>(msg_status)), "OPR");
            RetryLCSCLastCommand();
            break;
        }
    }
}

void operation::RetryLCSCLastCommand()
{
    std::string sMsg;

    if (tProcess.gbLoopApresent)
    {
        if (tExit.giDeductionStatus == Doingdeduction)
        {
            sMsg = "Deduction Error";
        }
        else
        {
            sMsg = "Reading Card^Error";
        }

        ShowLEDMsg(sMsg, sMsg);
        SendMsg2Server("90", tProcess.gsLastCardNo + ",,,,," + sMsg);

        //--------
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        //-------
        tExit.giDeductionStatus = WaitingCard;
        //---
        if (sMsg == "Deduction Error")
        {
            showFee2User();
        }

        EnableCashcard(true);

        if (tParas.giEPS == 3)
        {
            EEPInq(3);
        }
        else if (tParas.giEPS == 2)
        {
            CHUInq(3);
        }
        //-----------
    }
    else
    {
        EnableCashcard(false);
    }
}

void operation::KSM_CardIn()
{
    //--------
    ShowLEDMsg ("Card In^Please Wait ...", "Card In^Please Wait ...");
    setLastActionTimeAfterLoopA();

    //--------
    if (tPBSError[iReader].ErrNo != 0)
    {
        HandlePBSError(ReaderNoError);
    }
    //---------
    tProcess.giCardIsIn = 1;
    KSM_Reader::getInstance()->FnKSMReaderReadCardInfo();
}

void operation::handleKSM_EnableError()
{
    writelog (__func__, "OPR");

    if (tPBSError[iReader].ErrNo != -1)
    {
        HandlePBSError(ReaderError);
    }
}

void operation::handleKSM_CardReadError()
{
    writelog (__func__, "OPR");

    ShowLEDMsg(tMsg.Msg_CardReadingError[0], tMsg.Msg_CardReadingError[1]);
    SendMsg2Server("90", ",,,,,Wrong Card Insertion");
    KSM_Reader::getInstance()->FnKSMReaderSendEjectToFront();
}

void operation::KSM_CardInfo(const std::string& sKSMCardNo, long sKSMCardBal, bool sKSMCardExpired)
{
    writelog ("Cashcard: " + sKSMCardNo, "OPR");
    setLastActionTimeAfterLoopA();
    //------
    tPBSError[iReader].ErrNo = 0;

    if (sKSMCardNo.empty() ||
        sKSMCardNo.length() != 16 ||
        sKSMCardNo.substr(5, 4) == "0005")
    {
        ShowLEDMsg (tMsg.Msg_CardReadingError[0], tMsg.Msg_CardReadingError[1]);
        SendMsg2Server ("90", ",,,,,Wrong Card Insertion");
        KSM_Reader::getInstance()->FnKSMReaderSendEjectToFront();
    }
    else
    {
        if (tEntry.sIUTKNo.empty())
        {
            VehicleCome(sKSMCardNo);
        }
        else
        {
            KSM_Reader::getInstance()->FnKSMReaderSendEjectToFront();
        }
    }
}

void operation::KSM_CardTakeAway()
{
    writelog ("Card Take Away ", "OPR");
    setLastActionTimeAfterLoopA();
    tProcess.giCardIsIn = 2;

    if (tEntry.gbEntryOK)
    {
        ShowLEDMsg(tMsg.Msg_CardTaken[0],tMsg.Msg_CardTaken[1]);
    }
    else
    {
        if (tProcess.gbcarparkfull &&
            !tEntry.sIUTKNo.empty())
        {
            return;
        }

        ShowLEDMsg(tMsg.Msg_InsertCashcard[0], tMsg.Msg_InsertCashcard[1]);
    }

    //--------
    if (gtStation.iType == tientry &&
        tEntry.gbEntryOK)
    {
        Openbarrier();
        EnableCashcard(false);
    }
}

bool operation::AntennaOK()
{
    if (tParas.giEPS == 0 || tParas.giEPS == 3)
    {
        writelog ("Antenna: Non-EPS", "OPR");
        return false;
    }

    if (tParas.giCommPortAntenna == 0)
    {
        writelog("Antenna: Commport Not Set", "OPR");
        return false;
    }

    if (tPBSError[iAntenna].ErrNo == 0)
    {
        writelog("Antenna: OK", "OPR");
        return true;
    }

    writelog("Antenna: Error=" + tPBSError[iAntenna].ErrMsg, "OPR");
    return true;
}

void operation::ReceivedLPR(
    Lpr::CType CType,
    const std::string& LPN,
    const std::string& sTransid,
    const std::string& sImageLocation)
{
    writelog ("Received Trans ID: "+sTransid + " LPN: "+ LPN ,"OPR");
    writelog ("Send Trans ID: "+ tProcess.gsTransID, "OPR");

    if (tProcess.gsTransID == sTransid &&
        tProcess.gbLoopApresent &&
        !tProcess.gbsavedtrans)
    {
        if (gtStation.iType == tientry)
        {
            // For EdgeBox
            tEntry.sLPN[0] = LPN;
            tEntry.sLPN[1] = LPN;
        }
        else
        {
            tExit.sLPN[0] = LPN;
            tExit.sLPN[1] = LPN;
        }
    }
    else
    {
        if (gtStation.iType == tientry)
        {
            db::getInstance()->updateEntryTrans(LPN,sTransid);
        }
        else
        {
            db::getInstance()->updateExitTrans(LPN,sTransid);
        }
    }

    if (!tEntry.sIUTKNo.empty())
    {
        SendMsg2Server("90",tEntry.sIUTKNo+",,,"+LPN+ ",,Entry OK");
    }
}

void operation::processUPT(Upt::UPT_CMD cmd, const std::string& eventData)
{
    //writelog ("Received Command from UPT:" + eventData, "LPT");
    uint32_t msg_status = static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED);

    try
    {
        std::vector<std::string> subVector = Common::getInstance()->FnParseString(eventData, ',');
        for (unsigned int i = 0; i < subVector.size(); i++)
        {
            std::string pair = subVector[i];
            std::string param = Common::getInstance()->FnBiteString(pair, '=');
            std::string value = pair;

            if (param == "msgStatus")
            {
                msg_status = static_cast<uint32_t>(std::stoul(value));
            }
        }
    }
    catch (const std::exception& ex)
    {
        std::ostringstream oss;
        oss << "Exception : " << ex.what();
        writelog(oss.str(), "OPR");
    }

    switch (cmd)
    {
        case Upt::UPT_CMD::DEVICE_STATUS_REQUEST:
        {
            if (msg_status != static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED))
            {
                std::ostringstream oss;
                oss << "DEVICE_STATUS_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status);

                if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
                {
                    // Handle the cmd request response succeed
                }
                else
                {
                    // Handle the cmd request response failed
                }

                writelog(oss.str(), "OPR");
            }
            else
            {
                std::ostringstream oss;
                oss << "DEVICE_STATUS_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status) << " (PARSE_FAILED)";
                writelog(oss.str(), "OPR");
            }
            break;
        }
        case Upt::UPT_CMD::DEVICE_RESET_REQUEST:
        {
            if (msg_status != static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED))
            {
                std::ostringstream oss;
                oss << "DEVICE_RESET_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status);

                if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
                {
                    // Handle the cmd request response succeed
                }
                else
                {
                    // Handle the cmd request response failed
                }

                writelog(oss.str(), "OPR");
            }
            else
            {
                std::ostringstream oss;
                oss << "DEVICE_RESET_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status) << " (PARSE_FAILED)";
                writelog(oss.str(), "OPR");
            }
            break;
        }
        case Upt::UPT_CMD::DEVICE_TIME_SYNC_REQUEST:
        {
            if (msg_status != static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED))
            {
                std::ostringstream oss;
                oss << "DEVICE_TIME_SYNC_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status);

                if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
                {
                    // Handle the cmd request response succeed
                }
                else
                {
                    // Handle the cmd request response failed
                }

                writelog(oss.str(), "OPR");
            }
            else
            {
                std::ostringstream oss;
                oss << "DEVICE_TIME_SYNC_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status) << " (PARSE_FAILED)";
                writelog(oss.str(), "OPR");
            }
            break;
        }
        case Upt::UPT_CMD::DEVICE_LOGON_REQUEST:
        {
            if (msg_status != static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED))
            {
                std::ostringstream oss;
                oss << "DEVICE_LOGON_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status);

                if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
                {
                    // Handle the cmd request response succeed
                     tProcess.gbUPOSStatus = Login;
                }
                else
                {
                    // Handle the cmd request response failed
                   if  ( tProcess.giUPOSLoginCnt > 3) {
                        HandlePBSError(UPOSError);
                   }else{
                        tProcess.giUPOSLoginCnt = tProcess.giUPOSLoginCnt  + 1;
                        Upt::getInstance()->FnUptSendDeviceLogonRequest();
                   }
             
                }

                writelog(oss.str(), "OPR");
            }
            else
            {
                std::ostringstream oss;
                oss << "DEVICE_LOGON_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status) << " (PARSE_FAILED)";
                writelog(oss.str(), "OPR");
                //--------
                if  ( tProcess.giUPOSLoginCnt > 3) {
                    HandlePBSError(UPOSError);
                }else{
                    tProcess.giUPOSLoginCnt = tProcess.giUPOSLoginCnt  + 1;
                    Upt::getInstance()->FnUptSendDeviceLogonRequest();
                }
            }
            break;
        }
        case Upt::UPT_CMD::DEVICE_TMS_REQUEST:
        {
            if (msg_status != static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED))
            {
                std::ostringstream oss;
                oss << "DEVICE_TMS_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status);

                if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
                {
                    // Handle the cmd request response succeed
                }
                else
                {
                    // Handle the cmd request response failed
                }

                writelog(oss.str(), "OPR");
            }
            else
            {
                std::ostringstream oss;
                oss << "DEVICE_TMS_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status) << " (PARSE_FAILED)";
                writelog(oss.str(), "OPR");
            }
            break;
        }
        case Upt::UPT_CMD::DEVICE_SETTLEMENT_REQUEST:
        {
            if (msg_status != static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED))
            {
                std::ostringstream oss;
                oss << "DEVICE_SETTLEMENT_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status);

                if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
                {
                    // Handle the cmd request response succeed
                    uint64_t total_amount = 0;
                    uint64_t total_trans_count = 0;
                    std::string TID = "";
                    std::string MID = "";

                    try
                    {
                        std::vector<std::string> subVector = Common::getInstance()->FnParseString(eventData, ',');
                        for (unsigned int i = 0; i < subVector.size(); i++)
                        {
                            std::string pair = subVector[i];
                            std::string param = Common::getInstance()->FnBiteString(pair, '=');
                            std::string value = pair;

                            if (param == "totalAmount")
                            {
                                total_amount = std::stoull(value);
                            }
                            else if (param == "totalTransCount")
                            {
                                total_trans_count = std::stoull(value);
                            }
                            else if (param == "TID")
                            {
                                TID = value;
                            }
                            else if (param == "MID")
                            {
                                MID = value;
                            }
                        }

                        oss  << " | total amount : " << total_amount << " | total trans count : " << total_trans_count << " | TID : " << TID << " | MID : " << MID;
                    }
                    catch (const std::exception& ex)
                    {
                        oss << " | Exception : " << ex.what();
                    }

                    double dSettleTotalGrand = total_amount / 100.00f;
                    std::string dtNow = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
                    std::string dtNow2 = Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmmss();
                    std::string sSettleName = "UPT" + dtNow2 + Common::getInstance()->FnPadLeft0(2, gtStation.iSID);

                    db::getInstance()->insertUPTFileSummary(dtNow, sSettleName, 2, total_trans_count, dSettleTotalGrand, 1, dtNow);
                }
                else
                {
                    // Handle the cmd request response failed
                }

                writelog(oss.str(), "OPR");
            }
            else
            {
                std::ostringstream oss;
                oss << "DEVICE_SETTLEMENT_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status) << " (PARSE_FAILED)";
                writelog(oss.str(), "OPR");
            }
            break;
        }
        case Upt::UPT_CMD::DEVICE_RETRIEVE_LAST_SETTLEMENT_REQUEST:
        {
            if (msg_status != static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED))
            {
                std::ostringstream oss;
                oss << "DEVICE_RETRIEVE_LAST_SETTLEMENT_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status);

                if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
                {
                    // Handle the cmd request response succeed
                    uint64_t total_amount = 0;
                    uint64_t total_trans_count = 0;

                    try
                    {
                        std::vector<std::string> subVector = Common::getInstance()->FnParseString(eventData, ',');
                        for (unsigned int i = 0; i < subVector.size(); i++)
                        {
                            std::string pair = subVector[i];
                            std::string param = Common::getInstance()->FnBiteString(pair, '=');
                            std::string value = pair;

                            if (param == "totalAmount")
                            {
                                total_amount = std::stoull(value);
                            }
                            else if (param == "totalTransCount")
                            {
                                total_trans_count = std::stoull(value);
                            }
                        }

                        oss << " | total amount : " << total_amount << " | total trans count : " << total_trans_count;
                    }
                    catch (const std::exception& ex)
                    {
                        oss << " | Exception : " << ex.what();
                    }

                    double dSettleTotalGrand = total_amount / 100.00f;
                    std::string dtNow = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
                    std::string dtNow2 = Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmmss();
                    std::string sSettleName = "LastUPT" + dtNow2 + Common::getInstance()->FnPadLeft0(2, gtStation.iSID);

                    db::getInstance()->insertUPTFileSummaryLastSettlement(dtNow, sSettleName, 1, total_trans_count, dSettleTotalGrand, 1, dtNow);
                    Upt::getInstance()->FnUptSendDeviceSettlementNETSRequest();
                }
                else
                {
                    // Handle the cmd request response failed
                }

                writelog(oss.str(), "OPR");
            }
            else
            {
                std::ostringstream oss;
                oss << "DEVICE_RETRIEVE_LAST_SETTLEMENT_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status) << " (PARSE_FAILED)";
                writelog(oss.str(), "OPR");
            }
            break;
        }
        case Upt::UPT_CMD::CARD_DETECT_REQUEST:
        {
            tProcess.gbUPOSStatus = Disable;
            //----------
            if (msg_status != static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED))
            {
                std::ostringstream oss;
                oss << "CARD_DETECT_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status);

                if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
                {
                    // Handle the cmd request response succeed
                    std::string card_type = "";
                    std::string card_can = "";
                    float card_balance = 0;

                    try
                    {
                        std::vector<std::string> subVector = Common::getInstance()->FnParseString(eventData, ',');
                        for (unsigned int i = 0; i < subVector.size(); i++)
                        {
                            std::string pair = subVector[i];
                            std::string param = Common::getInstance()->FnBiteString(pair, '=');
                            std::string value = pair;

                            if (param == "cardType")
                            {
                                card_type = value;
                            }
                            else if (param == "cardCan")
                            {
                                card_can = value;
                            }
                            else if (param == "cardBalance")
                            {
                                card_balance = std::stof(value);
                            }
                        }

                        oss << " | card type : " << card_type << " | card can : " << card_can << " | card balance : " << std::fixed << std::setprecision(2) << (card_balance/100.0);
                         writelog(oss.str(), "OPR");
                        //---------
                        EnableLCSC(false);
                        Antenna::getInstance()->FnAntennaStopRead();
                        tExit.iCardStatus = 0;
                        CheckIUorCardStatus(card_can,UPOS,card_can,std::stoi(card_type) + 6, std::round(card_balance)/100);
                        setLastActionTimeAfterLoopA();

                    }
                    catch (const std::exception& ex)
                    {
                        oss << " | Exception : " << ex.what();
                        writelog(oss.str(), "OPR");
                    }
                }
                else if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::TIMEOUT))
                {
                    //Handle the cmd = 00000002 request response timeout
                   // writelog("UPOS Reader read card timeout.", "OPR");
                    if (tProcess.gbLoopApresent && tExit.gbPaid == false){
                        tProcess.gbUPOSStatus = ReadCardTimeout;
                        EnableUPOS(true);
                    }
                }
                else if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SOF_INVALID_CARD ))
                {
                    //Handle the cmd = 40000000 request response timeout
                    writelog("Received Response code = 40000000", "OPR");
                    if (tProcess.gbLoopApresent && tExit.gbPaid == false && tExit.sPaidAmt > 0 && tExit.giDeductionStatus == WaitingCard){
                        debitfromReader("", tExit.sPaidAmt , UPOS);
                        setLastActionTimeAfterLoopA();
                    }
                }
                else
                {
                   
                }
            }
            else
            {
                std::ostringstream oss;
                oss << "CARD_DETECT_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status) << " (PARSE_FAILED)";
                writelog(oss.str(), "OPR");
            }
            break;
        }
        case Upt::UPT_CMD::PAYMENT_MODE_AUTO_REQUEST:
        {
            tProcess.gbUPOSStatus = Disable;
            if (msg_status != static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED))
            {
                std::ostringstream oss;
                oss << "PAYMENT_MODE_AUTO_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status);

                if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
                {
                    // Handle the cmd request response succeed
                    std::string card_can = "";
                    uint64_t card_fee = 0;
                    uint64_t card_balance = 0;
                    std::string card_reference_no = "";
                    std::string card_batch_no = "";
                    std::string card_type = "";

                    try
                    {
                        std::vector<std::string> subVector = Common::getInstance()->FnParseString(eventData, ',');
                        for (unsigned int i = 0; i < subVector.size(); i++)
                        {
                            std::string pair = subVector[i];
                            std::string param = Common::getInstance()->FnBiteString(pair, '=');
                            std::string value = pair;

                            if (param == "cardCan")
                            {
                                card_can = value;
                            }
                            else if (param == "cardFee")
                            {
                                if (value != "")
                                {
                                    card_fee = std::stoull(value);
                                }
                            }
                            else if (param == "cardBalance")
                            {
                                if (value != "")
                                {
                                    card_balance = std::stoull(value);
                                }
                            }
                            else if (param == "cardReferenceNo")
                            {
                                card_reference_no = value;

                            }
                            else if (param == "cardBatchNo")
                            {
                                card_batch_no = value;
                            }
                            else if (param == "cardType")
                            {
                                card_type = value;
                            }
                        }
                        if (card_can == "") {
                            card_can = Common::getInstance()->FnPadLeft0(2, gtStation.iSID) + "20" + card_reference_no;
                            card_fee = tExit.sPaidAmt * 100;
                        }
                        oss << " | card type : " << card_type << " | card can : " << card_can << " | card fee : " << std::fixed << std::setprecision(2) << (card_fee / 100.0) << " | card balance : " << std::fixed << std::setprecision(2) << (card_balance / 100.0) << " | card reference no : " << card_reference_no << " | card batch no : " << card_batch_no;
                        writelog(oss.str(), "OPR");
                        DebitOK("", card_can, Common::getInstance()->SetFeeFormat(card_fee / 100.0), Common::getInstance()->SetFeeFormat(card_balance / 100.0), std::stoi(card_type) + 6, "", UPOS, "");
                        setLastActionTimeAfterLoopA();
                    }
                    catch (const std::exception& ex)
                    {
                        oss << " | Exception : " << ex.what();
                        writelog(oss.str(), "OPR");
                    }
                }
                else if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::TIMEOUT))
                {
                    //Handle the cmd = 00000002 request response timeout
                    writelog("UPOS deduction timeout.", "OPR");
                    if (tProcess.gbLoopApresent && (tExit.gbPaid == false || tExit.giDeductionStatus == Doingdeduction))
                    {
                       
                        tExit.giDeductionStatus = WaitingCard;
                        EnableCashcard(true);
                    }
                    
                }
                else if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::CARD_EXPIRED))
                {
                    //Handle the cmd = 00000002 request response timeout
                    writelog("Card Expired.", "OPR");
                    tExit.giDeductionStatus = CardExpired;
                    ShowLEDMsg("Card Expired!", "Card Expired!");
                    SendMsg2Server ("90", tProcess.gsLastCardNo + ",,,,,Card Expired");
                    EnableCashcard(true);
                    setLastActionTimeAfterLoopA();
                    break;
                    
                }
                else if ((msg_status > static_cast<uint32_t>(Upt::MSG_STATUS::CARD_NOT_DETECTED)) 
                        && (msg_status <= static_cast<uint32_t>(Upt::MSG_STATUS::CARD_DEBIT_UNCONFIRMED)))
                {
                        writelog("Card Fault.", "OPR");
                        tExit.giDeductionStatus = CardFault;
                        ShowLEDMsg("Card Fault!", "Card Fault!");
                        SendMsg2Server ("90", tProcess.gsLastCardNo + ",,,,,Card Fault");
                        EnableCashcard(true);
                        setLastActionTimeAfterLoopA();
                        break;
                }
                else {
                        writelog("UPT Deduction Error.", "OPR");
                        tExit.giDeductionStatus = WaitingCard;
                        ShowLEDMsg("Deduction Error!", "Deduction Error!");
                        SendMsg2Server ("90", tProcess.gsLastCardNo + ",,,,,Deduction Error");
                        EnableCashcard(true);
                    }

            }
            else
            {
                std::ostringstream oss;
                oss << "PAYMENT_MODE_AUTO_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status) << " (PARSE_FAILED)";
                writelog(oss.str(), "OPR");
                //-------
                writelog("UPT Deduction Error.", "OPR");
                tExit.giDeductionStatus = WaitingCard;
                tProcess.gbUPOSStatus = Disable;
                ShowLEDMsg("Deduction Error!", "Deduction Error!");
                SendMsg2Server ("90", tProcess.gsLastCardNo + ",,,,,Deduction Error");
                EnableCashcard(true);
            }
            break;
        }
        case Upt::UPT_CMD::CANCEL_COMMAND_REQUEST:
        {
            if (msg_status != static_cast<uint32_t>(Upt::MSG_STATUS::PARSE_FAILED))
            {
                std::ostringstream oss;
                oss << "CANCEL_COMMAND_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status);

                if (msg_status == static_cast<uint32_t>(Upt::MSG_STATUS::SUCCESS))
                {
                    // Handle the cmd request response succeed
                }
                else
                {
                    // Handle the cmd request response failed
                }

                writelog(oss.str(), "OPR");
            }
            else
            {
                std::ostringstream oss;
                oss << "CANCEL_COMMAND_REQUEST (UPOS CMD) | ";
                oss << "msg status : " << std::to_string(msg_status) << " (PARSE_FAILED)";
                writelog(oss.str(), "OPR");
            }
            break;
        }
        case Upt::UPT_CMD::TOP_UP_NETS_NCC_BY_NETS_EFT_REQUEST:
        case Upt::UPT_CMD::TOP_UP_NETS_NFP_BY_NETS_EFT_REQUEST:
        case Upt::UPT_CMD::DEVICE_RETRIEVE_LAST_TRANSACTION_STATUS_REQUEST:
        case Upt::UPT_CMD::DEVICE_RESET_SEQUENCE_NUMBER_REQUEST:
        case Upt::UPT_CMD::DEVICE_PROFILE_REQUEST:
        case Upt::UPT_CMD::DEVICE_SOF_LIST_REQUEST:
        case Upt::UPT_CMD::DEVICE_SOF_SET_PRIORITY_REQUEST:
        case Upt::UPT_CMD::DEVICE_PRE_SETTLEMENT_REQUEST:
        case Upt::UPT_CMD::MUTUAL_AUTHENTICATION_STEP_1_REQUEST:
        case Upt::UPT_CMD::MUTUAL_AUTHENTICATION_STEP_2_REQUEST:
        case Upt::UPT_CMD::CARD_DETAIL_REQUEST:
        case Upt::UPT_CMD::CARD_HISTORICAL_LOG_REQUEST:
        case Upt::UPT_CMD::PAYMENT_MODE_EFT_REQUEST:
        case Upt::UPT_CMD::PAYMENT_MODE_EFT_NETS_QR_REQUEST:
        case Upt::UPT_CMD::PAYMENT_MODE_BCA_REQUEST:
        case Upt::UPT_CMD::PAYMENT_MODE_CREDIT_CARD_REQUEST:
        case Upt::UPT_CMD::PAYMENT_MODE_NCC_REQUEST:
        case Upt::UPT_CMD::PAYMENT_MODE_NFP_REQUEST:
        case Upt::UPT_CMD::PAYMENT_MODE_EZ_LINK_REQUEST:
        case Upt::UPT_CMD::PRE_AUTHORIZATION_REQUEST:
        case Upt::UPT_CMD::PRE_AUTHORIZATION_COMPLETION_REQUEST:
        case Upt::UPT_CMD::INSTALLATION_REQUEST:
        case Upt::UPT_CMD::VOID_PAYMENT_REQUEST:
        case Upt::UPT_CMD::REFUND_REQUEST:
        case Upt::UPT_CMD::CASE_DEPOSIT_REQUEST:
        case Upt::UPT_CMD::UOB_REQUEST:
        {
            // Note: Not implemeted. Possible implement in future.
            break;
        }
    }
}

void operation::PrintTR(bool bForSeason)
{
    
    std::string sSerialNo;

    tProcess.glLastSerialNo = tProcess.glLastSerialNo + 1;
    std::string combinedString = std::to_string(tProcess.glLastSerialNo) + std::to_string(gtStation.iSID);
    std::ostringstream formattedString;
    formattedString << std::setw(9) << std::setfill('0') << combinedString;
    sSerialNo = formattedString.str();
    // Temp: will do in future - SaveSerialNo

    if (gtStation.iType == tientry)
    {
        if (tProcess.giEntryDebit < 2)
        {
            tEntry.sSerialNo = tParas.gsHdTk + sSerialNo;
            tEntry.sEntryTime = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
            // Temp: will do in future - i = CheckVType
            // Temp: will do in future - tEntry.sIUTKNo = cPrinter.bEncode(Format(Now, "yyyymmddHHmmss"), giStationID, i)
            // Temp: will do in future - tEntry.iTransType = i * 3  '0=car, 3=lorry, 6=M\cycle
        }
        else
        {
            tEntry.sReceiptNo = tParas.gsHdRec + sSerialNo;
        }
    }
    else
    {
       
        tExit.sReceiptNo = tParas.gsHdRec + sSerialNo;
        
    }

    std::string gsSite = tParas.gsSite;
    std::string gsCompany = tParas.gsCompany;
    std::string gsAddress = tParas.gsAddress;
    std::string gsZIP = tParas.gsZIP;
    std::string gsGSTNo = tParas.gsGSTNo;
    std::string gsTel = tParas.gsTel;
    std::string exitReceiptNo = "";
    std::string entrySerialNo = sSerialNo;
    std::string vehicleType = "";
    std::string iuNo = "";
    std::string cardNo = "";
    std::string entryTime = "";
    std::string exitTime = "";
    std::string parkTime = "";
    std::string amt = "";
    std::string cardBal = "";
    std::string owefee = "";
    std::string fee = "";
    std::string pm = "";
    std::string admin = "";
    std::string app = "";
    std::string tamt = "";
    std::string rdmamt = "";
    std::string rebateamt = "";
    std::string rebatebal = "";
    std::string rebatedate = "";
    std::string gstamt = "";

    std::vector<std::string> gsTR(tTR.size());
    for (std::size_t i = 0; i < tTR.size(); i++)
    {
        std::string gsTR_lowercase = tTR[i].gsTR1.empty() ? "" : boost::algorithm::to_lower_copy(tTR[i].gsTR1);

        if (gsTR_lowercase == "site")
        {
            gsTR[i] = tTR[i].gsTR0 + gsSite;
        }
        else if (gsTR_lowercase == "comp")
        {
            gsTR[i] = tTR[i].gsTR0 + gsCompany;
        }
        else if (gsTR_lowercase == "addr")
        {
            gsTR[i] = tTR[i].gsTR0 + gsAddress;
        }
        else if (gsTR_lowercase == "zip")
        {
            gsTR[i] = tTR[i].gsTR0 + gsZIP;
        }
        else if (gsTR_lowercase == "gstno")
        {
            gsTR[i] = tTR[i].gsTR0 + gsGSTNo;
        }
        else if (gsTR_lowercase == "gsTel")
        {
            gsTR[i] = tTR[i].gsTR0 + gsTel;
        }
        else if (gsTR_lowercase == "rno")
        {
            if ((tProcess.giEntryDebit == 2) && (gtStation.iType == tientry))
            {
                exitReceiptNo = tEntry.sReceiptNo;
            }
            else
            {
                exitReceiptNo = tExit.sReceiptNo;
            }
            gsTR[i] = tTR[i].gsTR0 + " " + exitReceiptNo;
        }
        else if (gsTR_lowercase == "tno")
        {
            gsTR[i] = tTR[i].gsTR0 + " " + entrySerialNo;
        }
        else if (gsTR_lowercase == "vtype")
        {
            if (gtStation.iType == tientry)
            {
                vehicleType = GetVTypeStr(tEntry.iTransType);
            }
            else
            {
                vehicleType = GetVTypeStr(tExit.iTransType);
            }
            gsTR[i] = tTR[i].gsTR0 + " " + vehicleType;
        }
        else if (gsTR_lowercase == "itno")
        {
            if (gtStation.iType == tientry)
            {
                iuNo = tEntry.sIUTKNo;
            }
            else
            {
                iuNo = tExit.sIUNo;
                
            }
            gsTR[i] = tTR[i].gsTR0 + " " + iuNo;
        }
        else if (gsTR_lowercase == "card")
        {
            if (tExit.bPayByEZPay == false && tExit.bPayByAXS == false)
            {
                
                if ((tProcess.giEntryDebit == 2) && (gtStation.iType == tientry))
                {
                    cardNo = tEntry.sCardNo;
                }
                else
                {
                    if (tExit.sPaidAmt > 0)
                    {
                        cardNo = tExit.sCardNo;
                    }
                }
                gsTR[i] = tTR[i].gsTR0 + " " + cardNo;
            }
           
        }
        else if (gsTR_lowercase == "et")
        {
            if (gtStation.iType == tientry)
            {
                entryTime = Common::getInstance()->FnFormatDateTime(tEntry.sEntryTime, "%Y-%m-%d %H:%M:%S", "%d/%m/%Y %H:%M:%S");
            }
            else
            {
                if (tExit.sEntryTime == "")
                {
                    entryTime = " N/A";
                }
                else
                {
                    entryTime = Common::getInstance()->FnFormatDateTime(tExit.sEntryTime, "%Y-%m-%d %H:%M:%S", "%d/%m/%Y %H:%M:%S");
                }
            }
            gsTR[i] = tTR[i].gsTR0 + " " + entryTime;
        }
        else if (gsTR_lowercase == "pt")
        {
            exitTime = Common::getInstance()->FnFormatDateTime(tExit.sExitTime, "%Y-%m-%d %H:%M:%S", "%d/%m/%Y %H:%M:%S");
            gsTR[i] = tTR[i].gsTR0 + " " + exitTime;
        }
        else if (gsTR_lowercase == "pkt")
        {
            if (tExit.lParkedTime > 0) {
                parkTime = db::getInstance()->CalParkedTime(tExit.lParkedTime);
                gsTR[i] = tTR[i].gsTR0 + " " + parkTime;
            }else  gsTR[i] = tTR[i].gsTR0 + " " + "N/A";
        }
        else if (gsTR_lowercase == "amt")
        {
            std::string payType = "";
            try
            {
                std::ostringstream formattedStream;
                formattedStream << std::fixed << std::setprecision(2) << tExit.sPaidAmt;

                float formattedAmt = std::stof(formattedStream.str());

                if (formattedAmt > 0)
                {
                    if (tProcess.giEntryDebit == 0)
                    {
                        //amt = std::to_string(formattedAmt);
                        amt = Common::getInstance()->SetFeeFormat(tExit.sPaidAmt);
                    }
                    else
                    {
                        float sumAmt = tExit.sPaidAmt + tExit.sPrePaid;

                        std::ostringstream formattedSumAmtStream;
                        formattedSumAmtStream << std::fixed << std::setprecision(2) << sumAmt;

                        float formattedSumAmt = std::stof(formattedSumAmtStream.str());

                           // amt = std::to_string(formattedSumAmt);
                        amt = Common::getInstance()->SetFeeFormat(sumAmt);
                    }
                }

                if (tExit.bPayByEZPay == true)
                {
                    payType = " (by EZPay)";
                }
                if (tExit.bPayByAXS == true) {
                    payType = " (by AXS)";
                }
                
            }
            catch (const std::exception& ex)
            {
                writelog(std::string("String to float exception error: ") + ex.what(), "OPR");
            }
            gsTR[i] = tTR[i].gsTR0 + " $" + amt + payType;
        }
        else if (gsTR_lowercase == "ezlink"){
            gsTR[i] = "";
            if (tExit.bPayByEZPay == true) gsTR[i] = tTR[i].gsTR0 ;
        }
        else if (gsTR_lowercase == "bal")
        {
            if (tExit.sPaidAmt > 0)
            {
               gsTR[i] = "";
                if ((tExit.bPayByEZPay == false) && (tProcess.gfLastCardBal > 0))
                {
                    std::stringstream ss;
                    ss << "card type: " << tExit.iCardType << ", fsCardBal: " << tProcess.gfLastCardBal;
                    writelog(ss.str(), "OPR");
                        
                    std::ostringstream formattedCardBalStream;
                    formattedCardBalStream << std::fixed << std::setprecision(2) << tProcess.gfLastCardBal;

                    cardBal = formattedCardBalStream.str();
                   gsTR[i] = tTR[i].gsTR0 + " $" + cardBal; 
                }
            }
        }
        else if (gsTR_lowercase == "owefee")
        {
            if (gtStation.iSubType == iXwithVEPay)
            {
                if (tExit.sOweAmt >= 0)
                {
                    // Temp: will do in futue - gsTR(i) = "(" & gtStations(gtStations(tExit.iEntryID).iVExitID).sZoneName & ") " & gsTR0(i) & " $" & Format(tExit.sOweAmt, "0.00")
                }
                else
                {
                    // Temp: will do in futue - gsTR(i) = "(" & gtStations(gtStations(tExit.iEntryID).iVExitID).sZoneName & ") " & gsTR0(i) & " $" & Format(0, "0.00")
                }
            }
            gsTR[i] = tTR[i].gsTR0 + " $" + owefee;
        }
        else if (gsTR_lowercase == "fee")
        {
            if (tProcess.giEntryDebit == 0)
            {
                std::stringstream ss;
                ss << "tExit.sFee: " << tExit.sFee << ", tExit.sPrePaid: " << tExit.sPrePaid; 
                writelog(ss.str(), "OPR");

                if (gtStation.iSubType == iXwithVEPay)
                {
                    float sumFee = tExit.sFee + tExit.sPrePaid;
                    std::ostringstream formattedSumFeeStream;
                    formattedSumFeeStream << std::fixed << std::setprecision(2) << sumFee;
                    fee = formattedSumFeeStream.str();
                    gsTR[i] = "(" + gtStation.sZoneName + ")" + tTR[i].gsTR0 + " $" + fee;
                }
                else
                {
                    std::ostringstream formattedFeeStream;
                    formattedFeeStream << std::fixed << std::setprecision(2) << tExit.sFee;
                    fee = formattedFeeStream.str();
                    gsTR[i] = tTR[i].gsTR0 + " $" + fee;
                }
            }
            else
            {
                float sumFee = tExit.sFee + tExit.sPrePaid;
                std::ostringstream formattedSumFeeStream;
                formattedSumFeeStream << std::fixed << std::setprecision(2) << sumFee;
                fee = formattedSumFeeStream.str();
                gsTR[i] = tTR[i].gsTR0 + " $" + fee;
            }
        }
        else if (gsTR_lowercase == "pm")
        {
            // Temp: will do in futue - pm = tSeason.sPaidMth
            gsTR[i] = tTR[i].gsTR0 + pm;
        }
        else if (gsTR_lowercase == "admin")
        {
            // Temp: will do in futue - admin = Format(tSeason.sAdminFee, "0.00")
            gsTR[i] = tTR[i].gsTR0 + admin;
        }
        else if (gsTR_lowercase == "app")
        {
            // Temp: will do in futue - app = Format(tSeason.sAppFee, "0.00")
            gsTR[i] = tTR[i].gsTR0 + app;
        }
        else if (gsTR_lowercase == "tamt")
        {
            // Temp: will do in futue - tamt = Format(tSeason.sPaidAmt + tSeason.sAdminFee + tSeason.sAdminFee, "0.00")
            gsTR[i] = tTR[i].gsTR0 + tamt;
        }
        else if (gsTR_lowercase == "rdmamt")
        {
            if (tExit.sRedeemAmt > 0)
            {
                std::ostringstream formattedRdmamtStream;
                formattedRdmamtStream << std::fixed << std::setprecision(2) << tExit.sRedeemAmt;
                rdmamt = formattedRdmamtStream.str();
                gsTR[i] = tTR[i].gsTR0 + " $" + rdmamt;
            }
            else
            {
                gsTR[i] = tTR[i].gsTR0 + " N/A";
            }
        }
        else if (gsTR_lowercase == "rebateamt")
        {
            if (tExit.sRebateAmt > 0)
            {
                std::ostringstream formattedRebateAmtStream;
                formattedRebateAmtStream << std::fixed << std::setprecision(2) << tExit.sRebateAmt;
                rebateamt = formattedRebateAmtStream.str();
                gsTR[i] = tTR[i].gsTR0 + " $" + rebateamt;
            }
            else
            {
                gsTR[i] = tTR[i].gsTR0 + " N/A";
            }
        }
        else if (gsTR_lowercase == "rebatebal")
        {
            if (tExit.sRebateAmt > 0)
            {
                std::ostringstream formattedRebateBalStream;
                formattedRebateBalStream << std::fixed << std::setprecision(2) << tExit.sRebateAmt;
                rebatebal = formattedRebateBalStream.str();
                gsTR[i] = tTR[i].gsTR0 + " $" + rebatebal;
            }
            else
            {
                gsTR[i] = tTR[i].gsTR0 + " N/A";
            }
        }
        else if (gsTR_lowercase == "rebatedate")
        {
            if (tExit.sRebateAmt > 0)
            {
                // Temp: will do in futue - gsTR(i) = gsTR0(i) & Format(tExit.sRebateDate, "dd/mm/yyyy")
            }
            else
            {
                gsTR[i] = tTR[i].gsTR0 + " N/A";
            }
        }
        else if (gsTR_lowercase == "gstamt")
        {
            if (tExit.sGSTAmt == 0)
            {
                if ((gtStation.iType == tientry) && (tProcess.giEntryDebit == 2))
                {
                    std::ostringstream formattedGstAmtStream;
                    formattedGstAmtStream << std::fixed << std::setprecision(2) << tEntry.sGSTAmt;
                    gstamt = formattedGstAmtStream.str();
                }
                else
                {
                    if (tExit.sPaidAmt > 0)
                    {
                        std::ostringstream formattedGstAmtStream;
                        formattedGstAmtStream << std::fixed << std::setprecision(2) << tExit.sGSTAmt;
                        gstamt = formattedGstAmtStream.str();
                    }
                    else
                    {
                        float sumGst = tExit.sPrePaid * tParas.gfGSTRate / (1 + tParas.gfGSTRate);
                        std::ostringstream formattedSumGstAmtStream;
                        formattedSumGstAmtStream << std::fixed << std::setprecision(2) << sumGst;
                        gstamt = formattedSumGstAmtStream.str();
                    }
                }
            }
            else
            {
                std::ostringstream formattedGstAmtStream;
                formattedGstAmtStream << std::fixed << std::setprecision(2) << tExit.sGSTAmt;
                gstamt = formattedGstAmtStream.str();
            }
            gsTR[i] = tTR[i].gsTR0 + " " + std::to_string(tParas.gfGSTRate * 100) + "% GST $" + gstamt;
        }
        else
        {
            gsTR[i] = tTR[i].gsTR0;
        }
    }

    Printer::getInstance()->FnPrintLine("Clear Buffer", 99);

    for (std::size_t i = 0; i < gsTR.size(); i++)
    {
        //std::cout << gsTR[i] << std::endl;
        char F = gsTR[i][0];

        if (F == '@')
        {
            int lines = std::stoi(gsTR[i].substr(1));
            Printer::getInstance()->FnFeedLine(lines);
        }
        else if (F == '*')
        {
            std::string barcode = gsTR[i].substr(1);
            Printer::getInstance()->FnPrintBarCode(barcode, tTR[i].giTRF, tTR[i].giTRA, 20);
        }
        else
        {
            if (!gsTR[i].empty() && gsTR[i].find("N/A") == std::string::npos)
            {
                Printer::getInstance()->FnPrintLine(gsTR[i], tTR[i].giTRF, tTR[i].giTRA);
            }
        }
    }

    Printer::getInstance()->FnFullCut();
    //---- update receipt No
    db::getInstance()->updateExitReceiptNo(sSerialNo,std::to_string(gtStation.iSID)); 
}

void operation::DebitOK(const std::string& sIUNO, const std::string& sCardNo, 
                const std::string& sPaidAmt, const std::string& sBal,
                int iCardType, const std::string& sTopupAmt,
                DeviceType iDevicetype, const std::string& sTransTime)
{

    //---------
    tExit.gbPaid = true;
    tExit.sCardNo = sCardNo;
    if (sPaidAmt != "") tExit.sPaidAmt = GfeeFormat(std::stof(sPaidAmt));
    tExit.iCardType = iCardType;
    if (sTopupAmt != "") tExit.sTopupAmt = GfeeFormat(std::stof(sTopupAmt));
    //--------
    tProcess.gsLastPaidTrans = tExit.sIUNo;
    tProcess.gbLastPaidStatus = true;
    tProcess.gsLastCardNo = sCardNo;
    if (sBal != "") tProcess.gfLastCardBal= GfeeFormat(std::stof(sBal));
    else tProcess.gfLastCardBal = 0;
    //-------
    tExit.giDeductionStatus = DeductionSuccessed;

    if (sIUNO != "" && tExit.sIUNo != sIUNO && iDevicetype == CHU){
        tExit.sIUNo = sIUNO;
        CloseExitOperation(UpdateCHUTrans);
    }
    else{
        CloseExitOperation(DeductionOK);
    }
    

}

std::string operation::GetVTypeStr(int iVType)
{
    if (iVType < 3 || iVType == 20)
    {
        return "Car";
    }

    if (iVType < 6 || iVType == 21)
    {
        return "Lorry";
    }

    if (iVType < 9 || iVType == 22)
    {
        return "M/Cycle";
    }

    if (iVType == 33)
    {
        return "Container";
    }

    return "Undefined Vehicle Type";
}

 void operation::CheckIUorCardStatus(string sCheckNo, DeviceType iDevicetype,string sCardNo, int sCardType, float sCardBal)
 {
    // device type : 0 = PMS(EntryTime), 1 = Ant, 2 = CHU, 3 = EEP, 4 = LCSC, 5 = UPOS

    string gsCompareNo;
    int iRet;
    string sMsg;
    //--------
    writelog ("Enter check IU/Card status","OPR");
    if (tExit.giDeductionStatus == CardExpired || tExit.giDeductionStatus == CardFault || tExit.giDeductionStatus == InsufficientBalance) {
        if( tProcess.gsLastCardNo == sCheckNo) {
            if (tExit.giDeductionStatus == InsufficientBalance){
                writelog ("Insufficient Balance! Change another", "OPR");
                ShowLEDMsg("Insufficient Bal","Insufficient Bal");
            }else {
                writelog ("Fault card! Change another", "OPR");
                ShowLEDMsg("Card Fault!","Card Fault!");
            }
            EnableCashcard(true);
            EEPInq(2);
            return;
        }
        tExit.giDeductionStatus = WaitingCard;
    }
    //-------- check paid status
    if (tExit.gbPaid == true ) {
        writelog ("Paid already!","OPR");
        ShowLEDMsg("Paid already^Have a nice Day!","Paid already^Have a nice day!");
        EnableCashcard(false);
        Openbarrier(2);
        return;
    }
    //--------check balance
    if (sCardNo != "" && GfeeFormat(sCardBal) != GfeeFormat(tProcess.gfLastCardBal) && sCardNo == tProcess.gsLastCardNo && tProcess.gbLastPaidStatus == false ) {
        tProcess.gfLastCardBal= GfeeFormat(sCardBal);
        EnableCashcard(false);
        writelog ("balance change case.","OPR");
        if (tExit.sPaidAmt == 0){
            //----Loop A come again, no need save trans
            Openbarrier(2);
        }else {
            CloseExitOperation(BalanceChange);
         }
        return;
    }
    //--------------
    if (tExit.giDeductionStatus == WaitingCard  && tExit.iCardStatus != 00 ) {
        if (tExit.iCardStatus == 1) sMsg = "Fee:$" + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "^Tap/Insert Card";
        else sMsg = "Fee:$" + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "^Invalid Card!";
        ShowLEDMsg(sMsg,sMsg);
        EnableCashcard(true);
        if (iDevicetype == EEP) EEPInq(2);
        else if (iDevicetype == CHU) CHUInq(2);
        return;
    }
    //--------
    if (tExit.giDeductionStatus == WaitingCard) 
    {
        //CheckCardOK 
        writelog ("check card OK!", "OPR");
        iRet = db::getInstance()->CheckCardOK(sCardNo);
        if (iRet > 0) {
            if (iRet == 5) {
                tExit.iTransType = 2;
                ShowLEDMsg("Master Card!", "Master Card!");
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            else {
                tExit.iTransType = 10;
                ShowLEDMsg("Complimentary!", "Complimentary!");
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            tExit.sCardNo = sCardNo;
            tExit.iCardType = sCardType;
            tExit.sPaidAmt = 0;
            CloseExitOperation(FreeParking);
            return;
        } 

        if (tExit.iCardStatus == 00 && sCardNo != "") 
        {
            if (iDevicetype == EEP) {
                tExit.sCardNo = sCardNo;
                tProcess.gsLastCardNo = sCardNo;
                tProcess.gfLastCardBal= GfeeFormat(sCardBal);
                //-----
                EEPDebit(tExit.sIUNo,tExit.sPaidAmt, tExit.sEntryTime, tExit.sExitTime);
                return;
            }
            if (iDevicetype == CHU && sCardType == 1) {
                CHUDebit(tExit.sIUNo, tExit.sPaidAmt,sCardNo,sCardBal);
                return;
            }
            if (GfeeFormat(tExit.sPaidAmt) <= GfeeFormat(sCardBal))
            {
                if (iDevicetype == CHU) CHUDebit(tExit.sIUNo, tExit.sPaidAmt,sCardNo,sCardBal);
                else debitfromReader(tExit.sIUNo, tExit.sPaidAmt, iDevicetype,sCardType,sCardBal);
            }
            else
            {
                ShowLEDMsg("Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +"^Insufficient Bal","Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +"^Insufficient Bal");
                writelog("insufficient balance", "OPR");
                EnableCashcard(true);
                return;
            }
        }
        else
        {
            if (tExit.iCardStatus == 01) writelog ("No card","OPR");
            else writelog ("Invalid card","OPR");
            if (iDevicetype == EEP) EEPInq(1);
            else CHUInq(1);
            EnableCashcard(true);
        }

    }
    else {
        gsCompareNo = tProcess.gsLastPaidTrans;
        auto sameAsLastIUDuration = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - tProcess.lastTransTime);
        if (sCheckNo == gsCompareNo && sameAsLastIUDuration.count() <= tParas.giMaxTransInterval) {
            writelog("last trans No: " + gsCompareNo, "OPR");
            writelog("Same as last IU, duration :" + std::to_string(sameAsLastIUDuration.count()) + " less than Maximum interval: " + std::to_string(tParas.giMaxTransInterval), "OPR");
            if (tProcess.gbLastPaidStatus == true) {
                if (iDevicetype == Ant) sMsg = "Same as last^Paid IU" ;
                else sMsg = "Same as last^Paid Card";
                //--------
                ShowLEDMsg(sMsg,sMsg);
                EnableCashcard(false);
                Openbarrier(2);
                ShowLEDMsg("Have A Nice Day","Have A Nice Day");
            }else
            {
                if (tExit.sPaidAmt > 0) {
                    if (iDevicetype == Ant) 
                    {   writelog("Enable full EPS for deduciton", "OPR");
                        CHUDebit(tExit.sIUNo, tExit.sPaidAmt,sCardNo,sCardBal);
                    }
                    else 
                    {
                        if (iDevicetype == EEP) {
                            if (tExit.iBFunctionStatus == 01 )
                            {
                                if (tExit.iBackendAccount == 1 || (tExit.sCardNo != "" && tExit.iCardStatus == 0 ))
                                {
                                    if (sCardNo != "") {
                                        tProcess.gsLastCardNo = sCardNo;
                                        tProcess.gfLastCardBal= GfeeFormat(sCardBal);
                                    }
                                    EEPDebit(tExit.sIUNo, tExit.sPaidAmt, tExit.sEntryTime, tExit.sExitTime);
                                }else
                                {
                                    if (tExit.iCardStatus == 01) writelog ("No card","OPR");
                                    else writelog ("Invalid card","OPR");
                                    tExit.giDeductionStatus = WaitingCard;
                                    EEPInq(1);
                                    EnableCashcard(true);
                                }
                            } else
                            {   
                                writelog("Business Function Status is stopping.(OBU cannot debit) ", "OPR");
                                tExit.giDeductionStatus = WaitingCard;
                                EnableCashcard(true);
                            }
                        }else {
                            if (iDevicetype == CHU){
                                if (sCardNo == ""){
                                    CHUInq(1);
                                }else CHUDebit(tExit.sIUNo, tExit.sPaidAmt,sCardNo,sCardBal);
                            } 
                            else debitfromReader(tExit.sIUNo, tExit.sPaidAmt, iDevicetype,sCardType,sCardBal);
                        }
                    }
                } 
                else PBSExit (sCheckNo,iDevicetype,sCardNo,sCardType,sCardBal);   
            }
        }else{
            PBSExit (sCheckNo,iDevicetype,sCardNo,sCardType,sCardBal);
        }
    }
 }

 void operation::PBSExit(string sIU, DeviceType iDevicetype, string sCardNo, int sCardType,float sCardBal)
{
    int iRet;
    CE_Time pt,pd,calTime;
  
    if (sIU == tExit.sIUNo) return;
    tExit.sIUNo = sIU;

    //check blacklist
    iRet = db::getInstance()->IsBlackListIU(sIU);
    if (iRet >= 0){
        ShowLEDMsg(tExitMsg.MsgExit_BlackList[0], tExitMsg.MsgExit_BlackList[1]);
        SendMsg2Server("90",sIU+",,,,,Blacklist IU");
        if(iRet ==0) return;
    }
    //check block 
    string gsBlockIUPrefix = IniParser::getInstance()->FnGetBlockIUPrefix();
   // writelog ("blockIUprfix =" +gsBlockIUPrefix, "OPR");
    if(gsBlockIUPrefix.find(sIU.substr(0,3)) !=std::string::npos && sIU.length() == 10)
    {
        ShowLEDMsg("Lorry No Exit^Pls Reverse","Lorry No Exit^Pls Reverse");
        SendMsg2Server("90",sIU+",,,,,Block IU");
        return;
    }
    //---Get Entry time
    if (tExit.bNoEntryRecord == -1) {
        iRet = db::getInstance()->FetchEntryinfo(sIU);
        if (tExit.sEntryTime == "") {
             tExit.bNoEntryRecord = 1;
             tExit.lParkedTime = -1;
        } else {
            writelog("Get Entry Time: " + tExit.sEntryTime, "OPR");
            tExit.bNoEntryRecord = 0;
        }
    } 
    //-----
    if(sIU.length()==10) {
        if (tParas.giEPS == 3 and tExit.VCC != "") {
             tExit.iTransType= db::getInstance()->FnGetVehicleType(tExit.VCC.substr(0,3));
        }else {
            tExit.iTransType= db::getInstance()->FnGetVehicleType(sIU.substr(0,3));
        }
    }else {
        tExit.iTransType=GetVTypeFromLoop();
    }

    if (tExit.iTransType == 9) {
        ShowLEDMsg(tMsg.Msg_authorizedvehicle[0],tMsg.Msg_authorizedvehicle[1]);
        CloseExitOperation(FreeParking);
        return;
    }

    tExit.iVehicleType = (tExit.iTransType - 1 )/3;

    iRet = CheckSeason(sIU,2);

    if (iRet == 1 or iRet == 12 or iRet == 9)
    {   
        if (tParas.giSeasonCharge == 0 || tExit.bNoEntryRecord == 1) {
            if (iRet == 9){tExit.iTransType = 10;}
            tExit.sFee = 0;
            tExit.sPaidAmt = 0;
            tExit.sExitTime = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
            CloseExitOperation(SeasonParking);
            return;
        }
    }
    if (tExit.bNoEntryRecord == 1){
          //---- complimentary Ticket
         if(tExit.sRedeemNo != "" && tExit.sRedeemAmt == 0 ){  
            tExit.iTransType = 10;
            tExit.sPaidAmt = 0;
            tExit.sCardNo = tExit.sRedeemNo;
            ShowLEDMsg("No Entry Record^Compl Ticket","No Entry Record^Compl Ticket");
            CloseExitOperation(FreeParking);
            EnableCashcard(false);
            return;
        }     
        //----------------------------
        int iAutoDebit;
		float sAmt;
        writelog("No Entry, Check Auto Debit.", "OPR");
		db::getInstance()->GetXTariff(iAutoDebit, sAmt, 0);
		writelog("Autocharge:"+std::to_string(iAutoDebit), "OPR");
		writelog("ChargeAmt:"+ Common::getInstance()->SetFeeFormat(sAmt), "OPR");
        if (iAutoDebit > 0) {
            tExit.sFee = GfeeFormat(sAmt);
            tExit.sExitTime = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
        }else{
            SendMsg2Server("07",sIU);
            ShowLEDMsg("No Entry Record^Press Intercom","No Entry Record^Press Intercom");
            //----- added on 03/08/2026
            if(tParas.giEPS == 3) {
                if (tExit.iOBUType == 0)  SendMsg2OBU(tExit.sIUNo,0,"No Entry Record","Pls Press Intercom", "For Assist","","");
                else SendMsg2OBU(tExit.sIUNo,0,"Press Intercom", "For Assist","","","");
            }
            return;
        }
    }else
    {
        tExit.sExitTime = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
        writelog("Cal Fee Time: " + tExit.sExitTime, "OPR");
        //-------
        pt.SetTime(tExit.sEntryTime);
        pd.SetTime(tExit.sExitTime);
        tExit.lParkedTime = calTime.diffmin(pt.GetUnixTimestamp(), pd.GetUnixTimestamp());
        //-------
        writelog("parked time: " + std::to_string(tExit.lParkedTime) + " Mins", "OPR");
        //---------
        if(iRet == 1)
        {  
            if (std::stoi(tSeason.rate_type) != 0) {
                // Show partial season fee 
                writelog ("Cal fee for partial season. RateType: " + tSeason.rate_type, "OPR");
                tExit.sFee = CalFeeRAM(tExit.sEntryTime, tExit.sExitTime, std::stoi(tSeason.rate_type));

            }else{
                if (tExit.sEntryTime < tSeason.date_from) {
                    writelog ("Season EntryTime early than date from, Cal fee for entry ~ date from", "OPR");
                    iRet = 8;
                    tExit.sFee = CalFeeRAM(tExit.sEntryTime, tSeason.date_from, tExit.iVehicleType);
                    ShowLEDMsg("Season Start On^" + tSeason.date_from.substr(0,16),"Season Start On^" + tSeason.date_from.substr(0,16));
                } else
                {
                    tExit.sFee = CalFeeRAM(tExit.sEntryTime, tExit.sExitTime, tExit.iVehicleType);
                }
            }
        }
        else
        {
            if(iRet == 2 && tSeason.date_to > tExit.sEntryTime) {
                writelog ("Season expired. EntryTime early than date to, cal fee for date to ~ exit", "OPR");
                tExit.sFee = CalFeeRAM(tSeason.date_to, tExit.sExitTime, tExit.iVehicleType);
                ShowLEDMsg("Season Expire On^" + tSeason.date_to.substr(0,16) ,"Season Expire On^" + tSeason.date_to.substr(0,16));
            } else
            {
                tExit.sFee = CalFeeRAM(tExit.sEntryTime, tExit.sExitTime, tExit.iVehicleType);
            }
        }
        //------ whole day season 
        if ((iRet == 1 && std::stoi(tSeason.rate_type) == 0) or iRet == 9 or iRet == 12)
        {
            if (iRet == 9) { tExit.iTransType = 10;}
            tExit.sPaidAmt = 0;
            CloseExitOperation(SeasonParking);
            return;
        }
        //-------- eComplimentary ticket
        if (tExit.iTransType == 10) {
            CloseExitOperation(Complimentary);
            return;
        }
    }
    //-------
    if (tExit.iRedeemTime > 0) RedeemTime2Amt();
    //-----
    tExit.sPaidAmt = GfeeFormat(tExit.sFee - tExit.sRebateAmt - tExit.sRedeemAmt + tExit.sOweAmt);
    if (tExit.sPaidAmt < 0)  tExit.sPaidAmt = 0;
    //------
    writelog("Total paid Amt: " + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt), "OPR");
    //------
    showFee2User();
    //-------------
   // ShowLEDMsg("Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +"^Please Wait...","Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +"^Please Wait...");
   if(tExit.sRedeemNo != "" && tExit.sRedeemAmt == 0 ){
        //---- complimentary Ticket
        tExit.iTransType = 10;
        tExit.sPaidAmt = 0;
        tExit.sCardNo = tExit.sRedeemNo;
        ShowLEDMsg("Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +"^Compl Ticket","Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +"^Compl Ticket");
        CloseExitOperation(FreeParking);
        EnableCashcard(false);
        return;
   } 
    if (tExit.sPaidAmt > 0) 
    {
        //----- added on 05/08/2026
        if (tParas.giEPS != 3 && tExit.sIUNo.length() == 10 ) {
            if (db::getInstance()->HasEZpay(tExit.sIUNo) == 1) {
                if (tExit.sPaidAmt > 500){
                    ShowLEDMsg("Press Intercom^To Verify Fee","Press Intercom^To Verify Fee");
                    SendMsg2Server("90", tExit.sIUNo + ",," + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + ",,,Fee To High");
                    return;
                }
                 tExit.bPayByEZPay = true;
                if (tExit.sRedeemAmt == 0){
                    ShowLEDMsg("Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + " EZL^Motoring Service","Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + " EZL^Motoring Service");
                      //------- delay 
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                    //-------
                    ShowLEDMsg("Pls Insert Tix^Complimentary","Pls Insert Tix^Complimentary");
                    EnableCashcard(true);
                }else{
                    ShowLEDMsg("Redeemed: $"+ Common::getInstance()->SetFeeFormat(tExit.sRedeemAmt) + "^ Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt), "Redeemed: $"+ Common::getInstance()->SetFeeFormat(tExit.sRedeemAmt) + "^ Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt));
                    CloseExitOperation(EZPayParking);
                }
                Openbarrier();
                return;
            }
            if (db::getInstance()->HasAXS(tExit.sIUNo) == 1) {
                if (tExit.sPaidAmt > 500) {
                    ShowLEDMsg("Press Intercom^To Verify Fee","Press Intercom^To Verify Fee");
                    SendMsg2Server("90", tExit.sIUNo + ",," + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + ",,,Fee To High");
                    return;
                }
                tExit.bPayByAXS = true;
                if (tExit.sRedeemAmt == 0) {
                    ShowLEDMsg("Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "^AXS Drive","Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "^AXS Drive");
                     //------- delay 
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                    //-------
                    ShowLEDMsg("Fee Paid.^OR Scan Ticket","Fee Paid.^OR Scan Ticket");
                    EnableCashcard(true);
                }else {
                    ShowLEDMsg("Redeemed: $"+ Common::getInstance()->SetFeeFormat(tExit.sRedeemAmt) + "^ Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt), "Redeemed: $"+ Common::getInstance()->SetFeeFormat(tExit.sRedeemAmt) + "^ Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt));
                    CloseExitOperation(AXSParking);
                }
                Openbarrier();
                return;
            }
        } 
        if (iDevicetype == Ant) {
            writelog("Enable full EPS for deduciton", "OPR");
            CHUDebit(tExit.sIUNo, tExit.sPaidAmt,sCardNo,sCardBal);
            return;
        }
        if (iDevicetype == EEP) {
            if (tExit.iBFunctionStatus == 01 )
            {
                if (tExit.iBackendAccount == 1 || (tExit.sCardNo != "" && tExit.iCardStatus == 0 ))
                {
                    if (sCardNo != "") {
                        tProcess.gsLastCardNo = sCardNo;
                        tProcess.gfLastCardBal= GfeeFormat(sCardBal);
                    }
                    EEPDebit(tExit.sIUNo, tExit.sPaidAmt, tExit.sEntryTime, tExit.sExitTime);
                }else
                {
                    tExit.giDeductionStatus = WaitingCard;
                    ShowLEDMsg("Fee:$" + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "^Tap/Insert Card", "Fee:$" + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "^Tap/Insert Card");
                    EEPInq(1);
                }
            } else
            {
                 writelog("Business Function Status is stopping.(OBU cannot debit) ", "OPR");
                 tExit.giDeductionStatus = WaitingCard;
                 ShowLEDMsg("OBU Cannot Debit^Tap/Insert Card","OBU Cannot Debit^Tap/Insert Card");
                 EnableCashcard(true);
            }
            return;
        }
        if (iDevicetype == LCSC || iDevicetype == UPOS ) 
        {
            if (GfeeFormat(tExit.sPaidAmt) <= GfeeFormat(sCardBal))
            {
                debitfromReader(tExit.sIUNo, tExit.sPaidAmt, iDevicetype,sCardType,sCardBal);
            }else
            {
                tExit.giDeductionStatus = WaitingCard;
                writelog ("Insufficient balance", "OPR");
                writelog("Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "Bal: $" + Common::getInstance()->SetFeeFormat(sCardBal) , "OPR");
                ShowLEDMsg("Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +"^Insufficient Balance","Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +"^Insufficient Balance");
                EnableCashcard(true);
            }
            return;
        }
    }
    else
    {
       CloseExitOperation(FreeParking);
    } 
    return;
}

void operation::debitfromReader(string CardNo, float sFee,DeviceType iDevicetype,int sCardType, float sCardBal)
{
    long glDebitAmt;
    //---
    if (tExit.giDeductionStatus == Doingdeduction) {
        writelog ("Doing deduction while send Deduction to Reader", "OPR");
        return;
    }
    //---------
    tExit.giDeductionStatus = Doingdeduction;
    tProcess.gbLastPaidStatus = false;
    tProcess.gsLastCardNo = CardNo;
    tProcess.gfLastCardBal= GfeeFormat(sCardBal);
    tExit.sCardNo = CardNo;
    tExit.iCardType = sCardType;
    //------
    glDebitAmt = sFee * 100;

    //ShowLEDMsg("Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +"^Please Wait...","Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +"^Please Wait...");

    if(iDevicetype == UPOS) {
        writelog("Send deduction Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +  " to UPOS. ", "OPR");
        writelog("UPOS batch number: " + Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmmss(), "OPR");
        Upt::getInstance()->FnUptSendDeviceAutoPaymentRequest(glDebitAmt, Common::getInstance()->FnGetDateTimeFormat_yymmddhhmmss());
        tProcess.gbUPOSStatus = Enable;
        //------ stop LCSC
        EnableLCSC(false);
    } 
    else{
        //----- send Cancel to UPOS
        Upt::getInstance()->FnUptSendDeviceCancelCommandRequest();
        tProcess.gbUPOSStatus = Disable;
        writelog("Send deduction Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt)+ " to LCSC.", "OPR");
        LCSCReader::getInstance()->FnSendCardDeduct(glDebitAmt);
        
    }

}

float operation::CalFeeRAM(string eTime, string payTime,int iTransType, bool bNoGT)
{
    
    float parkingfee;
    
    parkingfee = db::getInstance()->CalFeeRAM2G(eTime,payTime,iTransType);
    //----- added on 29/07/2026
    if (parkingfee >= 0.01 and tExit.sRedeemAmt == 0 and tExit.iRedeemTime == 0)
    {
        int iRet;
        if (tExit.sIUNo.length() == 10) {
            writelog ("check valid ticket for IU No: " + tExit.sIUNo, "OPR");
            iRet = db::getInstance()->HasValidTicket(tExit.sIUNo, "");
            if (iRet == 0) {
                tExit.iUsedTicketBy = 2;
            } else{
                if (tExit.sLPN[0] != "" && tExit.sLPN[0] != "0000000000")
                {
                    iRet = db::getInstance()->HasValidTicket("",tExit.sLPN[0]);
                    if (iRet == 0) tExit.iUsedTicketBy = 3;
                }
            }    
        } 
    }
   
    return parkingfee;
}

void operation::SaveExit()
{
    int iRet;
    std::string sLPRNo = "";
    CE_Time pt,pd,calTime;
    
    if (tExit.sIUNo== "") return;
    writelog ("Save Exit trans:"+ tExit.sIUNo, "OPR");
    //----
    if (GfeeFormat(tExit.sRedeemAmt) > GfeeFormat(tExit.sFee - tExit.sRebateAmt + tExit.sOweAmt)) tExit.sRedeemAmt = GfeeFormat(tExit.sFee - tExit.sRebateAmt + tExit.sOweAmt);
     tExit.sPaidAmt = GfeeFormat(tExit.sFee - tExit.sRebateAmt - tExit.sRedeemAmt + tExit.sOweAmt);
    //----
    if (tExit.lParkedTime <= 0 && tExit.sEntryTime != "") {
        pt.SetTime(tExit.sEntryTime);
        pd.SetTime(tExit.sExitTime);
        tExit.lParkedTime = calTime.diffmin(pt.GetUnixTimestamp(), pd.GetUnixTimestamp());
    }
    //---- added on 22/12/2025
    tExit.sGSTAmt = GfeeFormat(tExit.sPaidAmt * tParas.gfGSTRate / (1 + tParas.gfGSTRate));
    //---
    iRet = db::getInstance()->insertexittrans(tExit);
    if (iRet == iCentralSuccess){
        if (tExit.bNoEntryRecord == 0 ) {
            iRet = db::getInstance()->updatemovementtrans(tExit);
        }else  {
            iRet = db::getInstance()->insert2movementtrans(tExit);
        }
        //------- update used complimentary/Redemption ticket 
        if (tExit.sRedeemAmt > 0 or tExit.iTransType == 10){
            iRet = db::getInstance()->updateUsedTicket(tExit);
        }
    }
    //------ delete Local Entry
    db::getInstance()->UpdateLocalEntry(tExit.sIUNo);
    //----
    if (iRet == iCentralSuccess or iRet == iLocalSuccess)
    {
        tProcess.gsLastIUNo = tExit.sIUNo;
        tProcess.gsLastPaidTrans = tExit.sIUNo;
        tProcess.lastTransTime = std::chrono::steady_clock::now();
    }
    //-------
    tPBSError[iDB].ErrNo = (iRet == iCentralSuccess or iRet == iLocalSuccess) ? 0 : (iRet == iCentralFail) ? -1 : -2;

    if ((tExit.sLPN[0] != "") or (tExit.sLPN[1] != ""))
	{
		if((tExit.iTransType == 7) or (tExit.iTransType == 8) or (tExit.iTransType == 22))
		{
			sLPRNo = tExit.sLPN[1];
		}
		else
		{
			sLPRNo = tExit.sLPN[0];
		}
	}

    std::string sMsg2Send = (iRet == iCentralSuccess or iRet == iLocalSuccess) ? "Exit OK" : (iRet == iCentralFail) ? "Exit Central Failed" : "Exit Local Failed";

    sMsg2Send = tExit.sIUNo + "," + tExit.sCardNo + "," + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "," + sLPRNo + "," + std::to_string(tProcess.giShowType) + "," + sMsg2Send;

    if (tExit.iStatus == 0) {
        SendMsg2Server("90", sMsg2Send);
    }
    tProcess.gbsavedtrans = true;
    tExit.gbPaid = true;
    if (tExit.sPaidAmt == 0) {
        tProcess.gsLastCardNo = tExit.sCardNo;
        tProcess.gbLastPaidStatus = true;
    }
    //-------
    PrintReceipt();
}

void operation::PrintReceipt()
{
    // gbflag4Receipt:   0: default   1: button press  2: Receipt printed

    if (tExit.iflag4Receipt == 0) return;
    //--------
    if (tPBSError[iPrinter].ErrNo != 0) {
        writelog ("Printer Error while Print Receipt", "OPR");
        ShowLEDMsg("Printer Error^Press Intercom ", "Printer Error^Press Intercom");
        return;
    }
    //-------
   if (tExit.iflag4Receipt == 1 && tExit.gbPaid == true && tExit.sPaidAmt > 0) {
        
        PrintTR(false);
        tExit.iflag4Receipt = 2;
 
   } 
}

float operation::GfeeFormat(float value) {
    return std::round(value * 100.0f) / 100.0f;
}

void operation::CloseExitOperation(TransType iStatus)
{
   
    string sLEDMsg = "";
    string sLCDMsg = "";
    //-----
    if (iStatus < 7){
        tExit.iStatus = 0;
    }
    switch (iStatus){
        case FreeParking:
        {
            //---- LED MSg
            sLEDMsg = "Fee = $0.00 ^ Have A Nice Day!";
            sLCDMsg = "Fee = $0.00 ^ Have A Nice Day!";
            break;
        }
        case SeasonParking:
        {
            sLEDMsg = "Season Parking ^ Have A Nice Day!";
            sLCDMsg = "Season parking ^ Have A Nice Day!";
            break;
        }
        case GracePeriod:
        {
            //---- LED MSg
            sLEDMsg = "Grace Period ^ Have A Nice Day!";
            sLCDMsg = "Grace period ^ Have A Nice Day!";
            break;
        }
        case DeductionOK:
        {
            //---- LED MSg
            sLEDMsg = "Paid Amt: $" + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "^ Have A Nice Day!";
            sLCDMsg = "Paid Amt: $" + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "^ Have A Nice Day!";
            break;
        }
        case DeductionFail:
        {
            //---- LED MSg
            break;
        }
        case BalanceChange:
        {
            tExit.iStatus = 3;
            sLEDMsg = "Paid Amt: $" + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "^ Have A Nice Day!";
            sLCDMsg = "Paid Amt: $" + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "^ Have A Nice Day!";
            break;
        }
        case RejectCode7:
        {
            tExit.iStatus = 3;
            sLEDMsg = "Have A Nice Day!";
            sLCDMsg = "Have A Nice Day!";
            break;
        }
        case Manualopen:
        {
            tExit.iStatus = 4;
            sLEDMsg = "Please process^ Have A Nice Day!";
            sLCDMsg = "Please Process^ Have A Nice Day!";
            break;
        }
        case Complimentary:
        {
            tExit.iStatus = 0;
            sLEDMsg = "Complimentary Ticket^ Have A Nice Day!";
            sLCDMsg = "Complimentary Ticket ^ Have A Nice Day!";
            break; 
        }
        case EZPayParking:
        {
            tExit.iCardType = 5;
        }
        case AXSParking:
        {
            tExit.iCardType = 4;
        }
        default:
			break;

    }

    if (sLEDMsg != "") ShowLEDMsg(sLEDMsg,sLCDMsg);

    writelog ("Enter close Exit for: " + tExit.sIUNo, "OPR");
    SaveExit();
    if (iStatus != UpdateCHUTrans && iStatus != EZPayParking && iStatus != AXSParking) Openbarrier();
}

void operation::RedeemTime2Amt() 
{
    string sTmpTime;
    CE_Time pt;
    CE_Time pd;
    CE_Time calTime;
    float sAddFee;

    //--------
    if (tExit.sRedeemAmt > 0) {
        writelog ("Already has Redeem Amt: "+ Common::getInstance()->SetFeeFormat(tExit.sRedeemAmt), "OPR");
        return;
    }
    //
    if (tExit.iRedeemTime > 0)
    {
        if (tExit.sExitTime == "")
            sTmpTime = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
        else
            sTmpTime = tExit.sExitTime;
        //-------
        if (tExit.bNoEntryRecord != 0)
        {
            pt.SetTime(sTmpTime);
            pt.SetTime(pt.GetUnixTimestamp() - tExit.iRedeemTime*60);
            tExit.sRedeemAmt = CalFeeRAM(pt.DateTimeString(), sTmpTime, tExit.iVehicleType);
        }
        else
        {
            if (tExit.lParkedTime == 0){
                pt.SetTime(tExit.sEntryTime);
                pd.SetTime(sTmpTime);
                tExit.lParkedTime = calTime.diffmin(pt.GetUnixTimestamp(), pd.GetUnixTimestamp());
            }

            if (tExit.iRedeemTime >= tExit.lParkedTime){
                tExit.sRedeemAmt = tExit.sFee;
            }else{
                pt.SetTime(tExit.sEntryTime);
                pt.SetTime(pt.GetUnixTimestamp() + tExit.iRedeemTime*60);
                tExit.sRedeemAmt = CalFeeRAM(tExit.sEntryTime, pt.DateTimeString(), tExit.iVehicleType);
                if (GfeeFormat(tExit.sFee - tExit.sRedeemAmt) == 0)  
                //---- handle same block, perentry, grace 
                {
                    sAddFee = CalFeeRAM(pt.DateTimeString(), sTmpTime, tExit.iVehicleType);
                    if (sAddFee > 0) {
                        tExit.sRedeemAmt = GfeeFormat(tExit.sRedeemAmt - sAddFee);
                        if (tExit.sRedeemAmt < 0) tExit.sRedeemAmt = 0;
                    }
                }
            }

        }
        if (tExit.sRedeemAmt > 0 ) {
            writelog ("Redemption Time to Amt: $" + Common::getInstance()->SetFeeFormat(tExit.sRedeemAmt), "OPR");
        }else
        {
            writelog ("Redeeming time for per entry or same block is not useful.", "OPR");
        }
    }  

}

void operation::ticketScan(std::string skeyedNo)
{
    int iRet = 0;
    std::string sCardTkNo = "";
    std::tm dtExpireTime;
    float gbRedeemAmt = 0.00;
    int giRedeemTime = 0;
    std::string sMsg = "";
    long lParkTime = 0;
    std::string TT = "";
    bool isRedemptionTicket = false;

    //--------
    if (tExit.sRedeemAmt > 0)
    {
        if (tExit.sFee >= 0.01)
        {
            if (tExit.sFee - tExit.sRedeemAmt - tExit.sRebateAmt + tExit.sOweAmt <= 0.00f)
            {
                ticketOK();
            }
            else
            {
                ShowLEDMsg("Amount Redeem,^Pls Pay Bal!", "Amount Redeem^Pls Pay Bal!");
                writelog("Amount Redeem, Pls Pay Bal.","OPR");
                showFee2User();
            }
        }
        else
        {
            ShowLEDMsg("Amount Redeem,^Insert/Tap Card", "Amount Redeem^Insert/Tap Card");
            showFee2User();
        }
        return;
    }

    EnableCashcard(false);

    //--------
    if (skeyedNo.length() >= 12)
    {
        // Check if ticket barcode is 12 digits
        if ((Common::getInstance()->FnToUpper(skeyedNo.substr(0, 2)) == "SC") or (Common::getInstance()->FnToUpper(skeyedNo.substr(0, 2)) == "SR"))
        {
            if  (Common::getInstance()->FnToUpper(skeyedNo.substr(0, 2)) == "SR"){
                isRedemptionTicket = true;
            }
            sCardTkNo = Common::getInstance()->FnToUpper(skeyedNo.substr(0, 12));
        }
        // Check if ticket barcode is 9 digits
        else
        {
            if (skeyedNo.length() == 15)
            {
                sCardTkNo = Common::getInstance()->FnToUpper(skeyedNo.substr(0, 15));

                TT = sCardTkNo.substr(7, 1);
                if ((TT == "V") or (TT == "W") or (TT == "U") or (TT == "Z"))
                {
                    if ((TT == "V") or (TT == "W") )
                    {
                        isRedemptionTicket = true;
                    }

                    if (((TT == "V") or (TT == "W")) && (tParas.giExitTicketRedemption == 0))
                    {
                        writelog("Redemption ticket but redemption disabled: " + sCardTkNo, "OPR");
                        ShowLEDMsg(tExitMsg.MsgExit_RedemptionTicket[0], tExitMsg.MsgExit_RedemptionTicket[1]);
                        goto Exit_Sub;
                    }
                }

              //  ShowLEDMsg(tExitMsg.MsgExit_CardIn[0], tExitMsg.MsgExit_CardIn[1]);
            }
        }
    }
    else
    {
        if (skeyedNo.length() == 9)
        {
            sCardTkNo = Common::getInstance()->FnToUpper(skeyedNo.substr(0, 9));

            TT = sCardTkNo.substr(7, 1);
            if ((TT == "V") or (TT == "W") or (TT == "U") or (TT == "Z"))
            {
                if ((TT == "V") or (TT == "W"))
                {
                    isRedemptionTicket = true;
                }

                if (((TT == "V") or (TT == "W")) && (tParas.giExitTicketRedemption == 0))
                {
                    writelog("Redemption ticket but redemption disabled: " + sCardTkNo, "OPR");
                    ShowLEDMsg(tExitMsg.MsgExit_RedemptionTicket[0], tExitMsg.MsgExit_RedemptionTicket[1]);
                    goto Exit_Sub;
                }
            }

         //   ShowLEDMsg(tExitMsg.MsgExit_CardIn[0], tExitMsg.MsgExit_CardIn[1]);  remove on 03/07/2026
        }
    }
    
    if (sCardTkNo.length() != 9 && sCardTkNo.length() != 12 && sCardTkNo.length() != 15)
    {
        writelog("Invalid Ticket (Wrong Len): " + skeyedNo, "OPR");
        SendMsg2Server("90",skeyedNo + ",,,,,Invalid Ticket (Wrong Len)");
        ShowLEDMsg(tExitMsg.MsgExit_InvalidTicket[0], tExitMsg.MsgExit_InvalidTicket[1]);
        goto Exit_Sub;
    }

    // Ret : 0 = Expired, 1 = Valid, 2 = Used, 6 = Not Started, -1 = DB Error, 4 = Not Found
   iRet = db::getInstance()->isValidBarCodeTicket(isRedemptionTicket, skeyedNo, dtExpireTime, gbRedeemAmt, giRedeemTime);

    if (iRet != 1)
    {
        if (tParas.giEPS == 3) {
            if (tExit.iOBUType == 0) SendMsg2OBU(tExit.sIUNo,0,"Invalid Ticket", "Pls Present","Valid Payment","","");
            else SendMsg2OBU(tExit.sIUNo,0,"Invalid Ticket","","","",""); 
        }
        //--------
        sMsg = "Invalid Ticket";
        ShowLEDMsg(sMsg, sMsg);
    }
    switch (iRet)
    {
        // DB Error
        case -1:
        {
            writelog("Central DB Error", "OPR");
            ShowLEDMsg(tExitMsg.MsgExit_SystemError[0], tExitMsg.MsgExit_SystemError[1]);
            SendMsg2Server("90", sCardTkNo + ",,,,,Central DB Error");
            goto Exit_Sub;
            break;
        }
        // Expired
        case 0:
        {
            writelog("Ticket Expired: " + sCardTkNo, "OPR");
            if (isRedemptionTicket == true)
            {
              //  ShowLEDMsg(tExitMsg.MsgExit_RedemptionExpired[0], tExitMsg.MsgExit_RedemptionExpired[1]);
                SendMsg2Server("90", sCardTkNo + ",,,,,Redemption Expired");
            }
            else
            {
             //   ShowLEDMsg(tExitMsg.MsgExit_CompExpired[0], tExitMsg.MsgExit_CompExpired[1]);
                SendMsg2Server("90", sCardTkNo + ",,,,,Complimentary Expired");
            }
            goto Exit_Sub;
            break;
        }
        // Valid
        case 1:
        {
            tExit.sRedeemNo = sCardTkNo;
            //--------
            if (isRedemptionTicket == true)
            {
                writelog("Redemption Ticket: " + sCardTkNo + ", Expire: " + Common::getInstance()->FnFormatDateTime(dtExpireTime, "%Y-%m-%d %H:%M:%S"), "OPR");
                
                if (giRedeemTime > 0)
                {
                    tExit.iRedeemTime = giRedeemTime;
                    sMsg = "Redemption: ^" + std::to_string(tExit.iRedeemTime) + " Mins";
                    if(tExit.sPaidAmt > 0) {
                        writelog ("Call Redeemtime2Amt", "OPR");
                        RedeemTime2Amt();
                    }
                }
                else
                {
                    tExit.sRedeemAmt = GfeeFormat(gbRedeemAmt);
                    sMsg = "Redemption: ^$" + Common::getInstance()->SetFeeFormat(tExit.sRedeemAmt);
                }
                writelog(sMsg, "OPR");
                ShowLEDMsg(sMsg, sMsg);
                //-------- added on 06/08/2026
                if (tExit.bPayByAXS == true or tExit.bPayByEZPay == true){
                    if (GfeeFormat(tExit.sFee) < GfeeFormat(tExit.sRedeemAmt + tExit.sRebateAmt))  tExit.sRedeemAmt = GfeeFormat(tExit.sFee - tExit.sRebateAmt);
                    if (tExit.bPayByAXS == true){
                        ShowLEDMsg("Redemption^Redeemed: $"+ Common::getInstance()->SetFeeFormat(tExit.sRedeemAmt), "Redemption^Redeemed: $"+ Common::getInstance()->SetFeeFormat(tExit.sRedeemAmt));
                        CloseExitOperation(AXSParking);
                    }else{
                         ShowLEDMsg("Redeemed: $"+ Common::getInstance()->SetFeeFormat(tExit.sRedeemAmt) + "^Refunded to EZpay", "Redeemed: $"+ Common::getInstance()->SetFeeFormat(tExit.sRedeemAmt) + "^Refunded to EZpay");
                         CloseExitOperation(EZPayParking);
                    }
                    return;
                }
                //---------
                if (tExit.sIUNo == "" && tExit.sCardNo == "")
                {
                    SendMsg2Server("90", sCardTkNo + ",,,,,Redemption. Ticket Wait for Card");
                    EnableCashcard(true);
                    return;
                }
                showFee2User();
                float fee = 0.00f;
                try
                {
                    fee = std::stof(Common::getInstance()->SetFeeFormat((tExit.sFee - tExit.sRedeemAmt - tExit.sRebateAmt + tExit.sOweAmt)));
                }
                catch (const std::exception& e)
                {
                    writelog("Exception :" + std::string(e.what()), "OPR");
                }
                    
                if (fee <= 0.00f)
                {
                    ticketOK();
                    return;
                }
                else
                {
                    //----- added on 03/08/2026
                    if (tParas.giEPS == 3) {
                        if (tExit.iOBUType == 0) SendMsg2OBU(tExit.sIUNo,0,"Ticket Accepted.", "Fee Paid:$" + Common::getInstance()->SetFeeFormat(tExit.sRedeemAmt),"Present Payment","","");
                        else SendMsg2OBU(tExit.sIUNo,0,"Fee Paid","$" + Common::getInstance()->SetFeeFormat(tExit.sRedeemAmt),"","","");
                    }
                    EnableCashcard(true);
                    //----- added on 14/07/2026
                    sMsg = "Fee Paid $" + Common::getInstance()->SetFeeFormat(tExit.sRedeemAmt) + "^ Present Payment";
                    ShowLEDMsg(sMsg, sMsg);
                    //--------
                    if (tParas.giEPS == 3) EEPInq(2);
                    else if (tParas.giEPS == 2) CHUInq(1);
                    return;
                }

            }
                // Complimentary
            else
            {
                writelog("Complimentary Ticket: " + sCardTkNo + ", Expire: " + Common::getInstance()->FnFormatDateTime(dtExpireTime, "%Y-%m-%d %H:%M:%S"), "OPR");

                //-------- added on 06/08/2026
                if (tExit.bPayByAXS == true or tExit.bPayByEZPay == true){
                    tExit.sRedeemAmt = GfeeFormat(tExit.sFee - tExit.sRebateAmt);
                    tExit.iTransType = 10;
                    tExit.sCardNo = tExit.sRedeemNo;
                    if (tExit.bPayByAXS == true){
                        ShowLEDMsg("Complimentary^Redeemed: $"+ Common::getInstance()->SetFeeFormat(tExit.sFee), "Complimentary^Redeemed: $"+ Common::getInstance()->SetFeeFormat(tExit.sFee));
                        CloseExitOperation(AXSParking);
                    }else{
                         ShowLEDMsg("Complimentary^Refunded","Complimentary^Refunded" );
                         CloseExitOperation(EZPayParking);
                    }
                    return;
                } 
                if (tParas.giNeedCard4Complimentary  == 0)
                {
                    ShowLEDMsg(tExitMsg.MsgExit_Complimentary[0], tExitMsg.MsgExit_Complimentary[1]);
                    if (tExit.sIUNo == "")
                    {
                        tExit.sIUNo = "N/A";
                    }
                }
                else
                {
                    if (tExit.sIUNo == "")
                    {
                        ShowLEDMsg(tExitMsg.MsgExit_Complimentary[0], tExitMsg.MsgExit_Complimentary[1]);
                        SendMsg2Server("90", sCardTkNo + ",,,,,Compl. Ticket Wait for Card");
                        EnableCashcard(true);
                        return;
                    }
                }
               ticketOK();
               return;
            }
            break;
        }
        // Used
        case 2:
        {
            writelog("Used Ticket: " + sCardTkNo, "OPR");
           // ShowLEDMsg(tExitMsg.MsgExit_UsedTicket[0], tExitMsg.MsgExit_UsedTicket[1]);
            SendMsg2Server("90", sCardTkNo + ",,,,,Used Ticket");
            goto Exit_Sub;
            break;
        }
        // Not found
        case 4:
        {
            writelog("Ticket Not Found " + sCardTkNo, "OPR");
            //ShowLEDMsg("Ticket Not Found", "Ticket Not Found");
            SendMsg2Server("90", sCardTkNo + ",,,,,Ticket Not Found");
            goto Exit_Sub;
            break;
        }
        // Not started
        case 6:
        {
            writelog("Ticket Not Start: " + sCardTkNo, "OPR");
           // ShowLEDMsg("Ticket Not Start", "Ticket Not Start");
            SendMsg2Server("90", sCardTkNo + ",,,,,Ticket Not Start");
            goto Exit_Sub;
            break;
        }
        default:
        {
            writelog("Unable to find the return value from barcode ticket.", "OPR");
            break;
        }
    }

Exit_Sub:
    //------ added on 14/07/2026
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    //--------
    showFee2User();

    if (tExit.bPayByEZPay == true)
    {
        ShowLEDMsg("You may scan^Compl Ticket", "");
    }
    
    EnableCashcard(true);
    //----- added on 14/07/2026
    if (tParas.giEPS == 3) EEPInq(2);
    else if (tParas.giEPS == 2) CHUInq(1);

    return;
}

void operation::ticketOK()
{
    tExit.sPaidAmt = 0;
    if (tExit.sRedeemAmt == 0 && !tExit.sRedeemNo.empty())
    {
        tExit.iTransType = 10;
        tExit.sCardNo = tExit.sRedeemNo;
    }
    CloseExitOperation(FreeParking);
}

void operation::showFee2User(bool bPaying)
{
    std::string sPT = "";
    float sFee2Pay = 0.00f;
    std::string sUpp = "";
    std::string sLow = "";

    sFee2Pay = GfeeFormat(tExit.sFee - tExit.sRedeemAmt - tExit.sRebateAmt + tExit.sOweAmt);

    if (sFee2Pay < 0)
    {
        sFee2Pay = 0;
    }

    sPT = db::getInstance()->CalParkedTime(tExit.lParkedTime);

    if (sFee2Pay > 0)
    {
        sUpp = "Fee:$" + Common::getInstance()->SetFeeFormat(sFee2Pay);
        //sLow = tExitMsg.MsgExit_XNoCard[0];
        sLow = "Please Wait....";
    }
    else
    {
        // "Grace Period, Free!"
        sUpp = "Free parking";
        sLow = "Have a nice day!";
    }

    std::string sMsg = sUpp + "^" + sLow;
    
   // std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    ShowLEDMsg(sMsg, sMsg);

    std::string exit_entry_time = "00:00";
    try
    {
        if (tExit.sEntryTime != "")
        {
            exit_entry_time = Common::getInstance()->FnFormatDateTime(tExit.sEntryTime, "%Y-%m-%d %H:%M:%S", "%H:%M");
        }
    }
    catch (const std::exception& e)
    {
        writelog("Exception :" + std::string(e.what()), "OPR");
    }

    std::ostringstream ss;
    ss << tExit.sIUNo << ", E=" << exit_entry_time << "~T=" << sPT << ",";
    ss << Common::getInstance()->SetFeeFormat(sFee2Pay) << "," << ",";
    ss << tProcess.giShowType << "," << "Fee OK";
    SendMsg2Server("90", ss.str());
    //------ added on 27/07/2026
    if (tParas.giEPS == 3) SendMsg2OBU(tExit.sIUNo,0,"Parking Fee" , "$"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt), "","","");
    //------
    tExit.sPaidAmt = GfeeFormat(sFee2Pay);
}

void operation::ReceivedEntryRecord()
{
    CE_Time pt,pd,calTime;

    tExit.sExitTime = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();

    writelog("Cal Fee Time: " + tExit.sExitTime, "OPR");
    
    tExit.sFee = CalFeeRAM(tExit.sEntryTime, tExit.sExitTime, tExit.iVehicleType);
    //--------------
    if (tExit.iRedeemTime > 0) RedeemTime2Amt();
    //-----
    tExit.sPaidAmt = GfeeFormat(tExit.sFee - tExit.sRebateAmt - tExit.sRedeemAmt + tExit.sOweAmt);

    if (tExit.sPaidAmt < 0)  tExit.sPaidAmt = 0;
    //------
    writelog("Total paid Amt: " + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt), "OPR");
    //------
    showFee2User();
    //-------------
    if (tExit.sPaidAmt > 0) 
    {
        tExit.giDeductionStatus = WaitingCard;
        EnableCashcard(true);
    }
    else
    {
       CloseExitOperation(FreeParking);
    } 
}

void operation::processEEP(const std::string& eventData)
{
    //writelog(__func__, "OPR");

    try
    {
        // Parse the string event data
        boost::json::value parsed = boost::json::parse(eventData);
        struct EEPClient::EEPEventWrapper eventParsed = EEPClient::EEPEventWrapper::from_json(parsed);

        // Lambda to parse and log a payload
        auto parsePayload = [&](auto& outputField, const std::string& payload, const std::string& label)
        {
            typedef typename std::remove_reference<decltype(outputField)>::type PayloadType;
            boost::json::value payloadParsed = boost::json::parse(payload);
            outputField = PayloadType::from_json(payloadParsed);

            std::ostringstream oss;
            oss << "EEP Payload [" << label << "] " << outputField.to_string();
       //     writelog(oss.str(), "OPR");
        };

        auto toString = [](uint32_t value) -> std::string {
            switch (value)
            {
                case static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS):      return "SUCCESS";
                case static_cast<uint32_t>(EEPClient::MSG_STATUS::SEND_FAILED):  return "SEND_FAILED";
                case static_cast<uint32_t>(EEPClient::MSG_STATUS::ACK_TIMEOUT):  return "ACK_TIMEOUT";
                case static_cast<uint32_t>(EEPClient::MSG_STATUS::RSP_TIMEOUT):  return "RSP_TIMEOUT";
                case static_cast<uint32_t>(EEPClient::MSG_STATUS::PARSE_FAILED): return "PARSE_FAILED";
                default:                                              return "UNKNOWN";
            }
        };

        // Handle UNKNOWN_REQ_CMD notifications
        if (eventParsed.commandReqType == static_cast<uint32_t>(EEPClient::CommandType::UNKNOWN_REQ_CMD))
        {
            if (eventParsed.messageStatus != static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS))
            {
                return;
            }

            switch (static_cast<EEPClient::MESSAGE_CODE>(eventParsed.messageCode))
            {
                case EEPClient::MESSAGE_CODE::NOTIFICATION_LOG:
                {
                    EEPClient::notificationLog NotifLog;
                    parsePayload(NotifLog, eventParsed.payload, "NOTIFICATION_LOG");

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

                    auto getFieldDescription = [](uint8_t value, const std::unordered_map<uint8_t, std::string>& map) -> std::string
                    {
                        auto it = map.find(value);
                        return it != map.end() ? it->second : "Unknown";
                    };
                    //---- added on 23/03/2026  
                    string sEvent;

                    std::ostringstream ossEventTime;
                    ossEventTime << std::setfill('0')
                                << std::setw(4) << static_cast<int>(NotifLog.year) << "-"
                                << std::setw(2) << static_cast<int>(NotifLog.month) << "-"
                                << std::setw(2) << static_cast<int>(NotifLog.day) << " "
                                << std::setw(2) << static_cast<int>(NotifLog.hour) << ":"
                                << std::setw(2) << static_cast<int>(NotifLog.minute) << ":"
                                << std::setw(2) << static_cast<int>(NotifLog.second);
                    std::string sEventTime = ossEventTime.str();

                
                    if (NotifLog.notificationType == 0x00){
                        sEvent = getFieldDescription(NotifLog.errorCode, errorCodeMap);
                        db::getInstance()->AddSysEvent(sEvent,NotifLog.errorCode, sEventTime);
                        if (NotifLog.errorCode == 0x01 || NotifLog.errorCode == 0x20 || NotifLog.errorCode == 0x22 || NotifLog.errorCode == 0x23 || NotifLog.errorCode == 0x24)
                        {
                            if (tPBSError[0].ErrNo == 0 ) {
                                tPBSError[0].ErrNo = -1;
                                Sendmystatus();
                                writelog("Received Alert from DSRC", "OPR");
                            }

                        }
                    }
                    else
                    {
                        db::getInstance()->UpdateSysEvent(sEvent,NotifLog.errorCode, sEventTime);
                       
                        if (db::getInstance()->HasAlertNotification() == false)
                        {
                           if ( tPBSError[0].ErrNo == -1 ) {
                                tPBSError[0].ErrNo = 0;
                                Sendmystatus();
                                writelog("Clear DSRC Alert", "OPR");
                            }
                        }
                    }
                    
                    break;
                }
                case EEPClient::MESSAGE_CODE::DI_STATUS_NOTIFICATION:
                {
                    EEPClient::diStatusNotification diStatusNotif;
                    parsePayload(diStatusNotif, eventParsed.payload, "DI_STATUS_NOTIFICATION");
                    break;
                }
                case EEPClient::MESSAGE_CODE::EEP_RESTART_INQUIRY:
                {
                    EEPClient::eepRestartInquiry eepRestartInqNotif;
                    parsePayload(eepRestartInqNotif, eventParsed.payload, "EEP_RESTART_INQUIRY");
                    break;
                }
                case EEPClient::MESSAGE_CODE::TRANSACTION_DATA:
                {
                    EEPClient::transactionData transDataNotif;
                    parsePayload(transDataNotif, eventParsed.payload, "TRANSACTION_DATA");
                    //------ added on 17/12/2025
                    processEEPTransData(transDataNotif);
                    break;
                }
                case EEPClient::MESSAGE_CODE::OBU_INFORMATION_NOTIFICATION:
                {
                    EEPClient::obuInformationNotification obuInfoNotif;
                    parsePayload(obuInfoNotif, eventParsed.payload, "OBU_INFORMATION_NOTIFICATION");
                    //----- added on 02/03/2026
                    if (tProcess.gbLoopApresent == true && tProcess.gsTailgateOBU == "")
                    {
                        tProcess.gsTailgateOBU = Common::getInstance()->longToHex(obuInfoNotif.obulabel);
                        writelog ("Set Tailgate OBU: " + tProcess.gsTailgateOBU, "OPR");
                    } 
                    else
                    {
                        if (tProcess.gsTailgateOBU == Common::getInstance()->longToHex(obuInfoNotif.obulabel)) 
                        {
                            ProcessOUBInformation(obuInfoNotif);
                        }
                    } 

                    break;
                }
                case EEPClient::MESSAGE_CODE::CPO_INFORMATION_DISPLAY_RESULT:
                {
                    EEPClient::cpoInformationDisplayResult cpoInfoDisplayResultNotif;
                    parsePayload(cpoInfoDisplayResultNotif, eventParsed.payload, "CPO_INFORMATION_DISPLAY_RESULT");
                    break;
                }
                case EEPClient::MESSAGE_CODE::CARPARK_PROCESS_COMPLETE_RESULT:
                {
                    EEPClient::carparkProcessCompleteResult carparkProcessCompleteResultNotif;
                    parsePayload(carparkProcessCompleteResultNotif, eventParsed.payload, "CARPARK_PROCESS_COMPLETE_RESULT");
                    break;
                }
                default:
                {
                    writelog("EEP Notification: UNKNOWN.", "OPR");
                    break;
                }
            }
            return;
        }

        // Handle request commands
        switch (static_cast<EEPClient::CommandType>(eventParsed.commandReqType))
        {
            case EEPClient::CommandType::START_REQ_CMD:
            {
                if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS))
                {
                    switch (static_cast<EEPClient::MESSAGE_CODE>(eventParsed.messageCode))
                    {
                        case EEPClient::MESSAGE_CODE::START_RESPONSE:
                        {
                            EEPClient::startResponse startResp;
                            parsePayload(startResp, eventParsed.payload, "START_RESPONSE");
                            //------ added on 24/03/2026
                            if (startResp.resultCode == 0x01 || startResp.resultCode == 0x02)
                            {
                                 if ( tPBSError[0].ErrNo == -1 ) {
                                    tPBSError[0].ErrNo = 0;
                                    Sendmystatus();
                                }
                                 writelog("DSRC state is In-Operation", "OPR");

                            }else
                            {
                                if ( tPBSError[0].ErrNo == 0 ) {
                                    tPBSError[0].ErrNo = -1;
                                    Sendmystatus();
                                }
                                writelog("DSRC state is Out-Operation", "OPR");
                            }
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::NAK:
                        {
                            EEPClient::nak startRespNak;
                            parsePayload(startRespNak, eventParsed.payload, "START_RESPONSE_NAK");
                            break;
                        }
                        default:
                        {
                            writelog("EEP Request Cmd: START_REQ_CMD, unknown response.", "OPR");
                            break;
                        }
                    }
                }
                else
                {
                    writelog("EEP Request Cmd: START_REQ_CMD, " + toString(eventParsed.messageStatus), "OPR");
                }
                break;
            }
            case EEPClient::CommandType::STOP_REQ_CMD:
            {
                if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS))
                {
                    switch (static_cast<EEPClient::MESSAGE_CODE>(eventParsed.messageCode))
                    {
                        case EEPClient::MESSAGE_CODE::STOP_RESPONSE:
                        {
                            EEPClient::stopResponse stopResp;
                            parsePayload(stopResp, eventParsed.payload, "STOP_RESPONSE");
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::NAK:
                        {
                            EEPClient::nak stopRespNak;
                            parsePayload(stopRespNak, eventParsed.payload, "STOP_RESPONSE_NAK");
                            break;
                        }
                        default:
                        {
                            writelog("EEP Request Cmd: STOP_REQ_CMD, unknown response.", "OPR");
                            break;
                        }
                    }
                }
                else
                {
                    writelog("EEP Request Cmd: STOP_REQ_CMD, " + toString(eventParsed.messageStatus), "OPR");
                }
                break;
            }
            case EEPClient::CommandType::DI_REQ_CMD:
            {
                if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS))
                {
                    switch (static_cast<EEPClient::MESSAGE_CODE>(eventParsed.messageCode))
                    {
                        case EEPClient::MESSAGE_CODE::DI_STATUS_RESPONSE:
                        {
                            EEPClient::diStatusResponse diStatusResp;
                            parsePayload(diStatusResp, eventParsed.payload, "DI_STATUS_RESPONSE");
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::NAK:
                        {
                            EEPClient::nak diStatusNak;
                            parsePayload(diStatusNak, eventParsed.payload, "DI_REQ_NAK");
                            break;
                        }
                        default:
                        {
                            writelog("EEP Request Cmd: DI_REQ_CMD, unknown response.", "OPR");
                            break;
                        }
                    }
                }
                else
                {
                    writelog("EEP Request Cmd: DI_REQ_CMD, " + toString(eventParsed.messageStatus), "OPR");
                }
                break;
            }
            case EEPClient::CommandType::DO_REQ_CMD:
            {
                if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS))
                {
                    switch (static_cast<EEPClient::MESSAGE_CODE>(eventParsed.messageCode))
                    {
                        case EEPClient::MESSAGE_CODE::ACK:
                        {
                            EEPClient::ack setDoReqAck;
                            parsePayload(setDoReqAck, eventParsed.payload, "DO_REQ_ACK");
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::NAK:
                        {
                            EEPClient::nak setDoReqNak;
                            parsePayload(setDoReqNak, eventParsed.payload, "DO_REQ_NAK");
                            break;
                        }
                        default:
                        {
                            writelog("EEP Request Cmd: DO_REQ_CMD, unknown response.", "OPR");
                            break;
                        }
                    }
                }
                else
                {
                    writelog("EEP Request Cmd: DO_REQ_CMD, " + toString(eventParsed.messageStatus), "OPR");
                }
                break;
            }
            case EEPClient::CommandType::SET_DI_PORT_CONFIG_CMD:
            {
                if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS))
                {
                    switch (static_cast<EEPClient::MESSAGE_CODE>(eventParsed.messageCode))
                    {
                        case EEPClient::MESSAGE_CODE::ACK:
                        {
                            EEPClient::ack setDiPortConfigAck;
                            parsePayload(setDiPortConfigAck, eventParsed.payload, "SET_DI_PORT_CONFIG_ACK");
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::NAK:
                        {
                            EEPClient::nak setDiPortConfigNak;
                            parsePayload(setDiPortConfigNak, eventParsed.payload, "SET_DI_PORT_CONFIG_NAK");
                            break;
                        }
                        default:
                        {
                            writelog("EEP Request Cmd: SET_DI_PORT_CONFIG_CMD, unknown response.", "OPR");
                            break;
                        }
                    }
                }
                else
                {
                    writelog("EEP Request Cmd: SET_DI_PORT_CONFIG_CMD, " + toString(eventParsed.messageStatus), "OPR");
                }
                break;
            }
            case EEPClient::CommandType::GET_OBU_INFO_REQ_CMD:
            {
                if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS))
                {
                    switch (static_cast<EEPClient::MESSAGE_CODE>(eventParsed.messageCode))
                    {
                        case EEPClient::MESSAGE_CODE::OBU_INFORMATION_NOTIFICATION:
                        {
                            EEPClient::obuInformationNotification OBUInfoNotif;
                            parsePayload(OBUInfoNotif, eventParsed.payload, "OBU_INFORMATION_NOTIFICATION");
                            //----- added on 01/12/2025
                            EnableCashcard(false);
                            if (tProcess.fiLastEEPCmd == EEPClient::CommandType::GET_OBU_INFO_REQ_CMD) tProcess.fiLastEEPCmd = EEPClient:: CommandType::EEP_idle;
                            if (tProcess.gbLoopApresent == true) ProcessOUBInformation(OBUInfoNotif);
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::ACK:
                        {
                            EEPClient::ack OBUInfoReqAck;
                            parsePayload(OBUInfoReqAck, eventParsed.payload, "GET_OBU_INFO_REQ_ACK");
                            //------ added on 10/12/2025
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::NAK:
                        {
                            EEPClient::nak OBUInfoReqNak;
                            parsePayload(OBUInfoReqNak, eventParsed.payload, "GET_OBU_INFO_REQ_NAK");    
                            break;
                        }
                        default:
                        {
                            writelog("EEP Request Cmd: GET_OBU_INFO_REQ_CMD, unknown response.", "OPR");
                            break;
                        }
                    }
                }
                else
                {
                    writelog("EEP Request Cmd: GET_OBU_INFO_REQ_CMD, " + toString(eventParsed.messageStatus), "OPR");
                    //------ added on 19/12/2025 ?? need check with KC??
                   // if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::RSP_TIMEOUT)){
                         if (tProcess.gbLoopApresent == true)
                         {
                            writelog ("No OBU detected!", "OPR");
                            SendMsg2Server("90",",,,,,No OBU Detected");
                            if (gtStation.iType == tiExit) ShowLEDMsg("No OBU. Present^Valid Payment", "No OBU. Present^Valid Payment");
                            else ShowLEDMsg("No OBU. Present^Card For Entry", "No OBU. Present^Card For Entry");
                            EnableCashcard(true);
                         }
                  //  }
                }
                break;
            }
            case EEPClient::CommandType::GET_OBU_INFO_STOP_REQ_CMD:
            {
                if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS))
                {
                    switch (static_cast<EEPClient::MESSAGE_CODE>(eventParsed.messageCode))
                    {
                        case EEPClient::MESSAGE_CODE::ACK:
                        {
                            EEPClient::ack getOBUInfoStopReqAck;
                            parsePayload(getOBUInfoStopReqAck, eventParsed.payload, "GET_OBU_INFO_STOP_REQ_ACK");
                            if (tProcess.fiLastEEPCmd == EEPClient::CommandType::GET_OBU_INFO_STOP_REQ_CMD) tProcess.fiLastEEPCmd = EEPClient:: CommandType::EEP_idle;
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::NAK:
                        {
                            EEPClient::nak getOBUInfoStopReqNak;
                            parsePayload(getOBUInfoStopReqNak, eventParsed.payload, "GET_OBU_INFO_STOP_REQ_NAK");
                            break;
                        }
                        default:
                        {
                            writelog("EEP Request Cmd: GET_OBU_INFO_STOP_REQ_CMD, unknown response.", "OPR");
                            break;
                        }
                    }
                }
                else
                {
                    writelog("EEP Request Cmd: GET_OBU_INFO_STOP_REQ_CMD, " + toString(eventParsed.messageStatus), "OPR");
                }
                break;
            }
            case EEPClient::CommandType::DEDUCT_REQ_CMD:
            {
                if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS))
                {
                    switch (static_cast<EEPClient::MESSAGE_CODE>(eventParsed.messageCode))
                    {
                        case EEPClient::MESSAGE_CODE::TRANSACTION_DATA:
                        {
                            EEPClient::transactionData transData;
                            parsePayload(transData, eventParsed.payload, "TRANSACTION_DATA");
                            //----- added on 01/12/2025
                            if (tProcess.fiLastEEPCmd == EEPClient::CommandType::DEDUCT_REQ_CMD) tProcess.fiLastEEPCmd = EEPClient:: CommandType::EEP_idle;
                            processEEPTransData(transData);
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::ACK:
                        {
                            EEPClient::ackDeduct deductReqAck;
                            parsePayload(deductReqAck, eventParsed.payload, "DEDUCT_REQ_ACK");
                            //----- added on 17/12/2025
                            tExit.sDSerialNo = std::to_string(deductReqAck.deductSerialNo);
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::NAK:
                        {
                            EEPClient::nak deductReqNak;
                            parsePayload(deductReqNak, eventParsed.payload, "DEDUCT_REQ_NAK");
                            //----- added on 08/12/2025
                            break;
                        }
                        default:
                        {
                            writelog("EEP Request Cmd: DEDUCT_REQ_CMD, unknown response.", "OPR");
                            break;
                        }
                    }
                }
                else
                {
                    writelog("EEP Request Cmd: DEDUCT_REQ_CMD, " + toString(eventParsed.messageStatus), "OPR");
                    //------ added on 19/12/2025
                    if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::RSP_TIMEOUT)){
                        if (tProcess.gbLoopApresent == true && (tExit.gbPaid == false || tExit.giDeductionStatus == Doingdeduction))
                        {
                            writelog ("unable to make a deduction via OBU", "OPR");
                            ShowLEDMsg("Insert/Tap Card", "Insert/Tap Card");
                            tExit.giDeductionStatus = WaitingCard;
                            EnableCashcard(true);
                        }
                    }
                }
                break;
            }
            case EEPClient::CommandType::DEDUCT_STOP_REQ_CMD:
            {
                if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS))
                {
                    switch (static_cast<EEPClient::MESSAGE_CODE>(eventParsed.messageCode))
                    {
                        case EEPClient::MESSAGE_CODE::ACK:
                        {
                            EEPClient::ack deductStopReqAck;
                            parsePayload(deductStopReqAck, eventParsed.payload, "DEDUCT_STOP_REQ_ACK");
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::NAK:
                        {
                            EEPClient::nak deductStopReqNak;
                            parsePayload(deductStopReqNak, eventParsed.payload, "DEDUCT_STOP_REQ_NAK");
                            break;
                        }
                        default:
                        {
                            writelog("EEP Request Cmd: DEDUCT_STOP_REQ_CMD, unknown response.", "OPR");
                            break;
                        }
                    }
                }
                else
                {
                    writelog("EEP Request Cmd: DEDUCT_STOP_REQ_CMD, " + toString(eventParsed.messageStatus), "OPR");
                }
                break;
            }
            case EEPClient::CommandType::TRANSACTION_REQ_CMD:
            {
                if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS))
                {
                    switch (static_cast<EEPClient::MESSAGE_CODE>(eventParsed.messageCode))
                    {
                        case EEPClient::MESSAGE_CODE::TRANSACTION_DATA:
                        {
                            EEPClient::transactionData transData;
                            parsePayload(transData, eventParsed.payload, "TRANSACTION_DATA");
                            //----- added on 01/12/2025
                            if (tProcess.fiLastEEPCmd == EEPClient::CommandType::TRANSACTION_REQ_CMD) tProcess.fiLastEEPCmd = EEPClient:: CommandType::EEP_idle;
                            processEEPTransData(transData);
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::ACK:
                        {
                            EEPClient::ack transReqAck;
                            parsePayload(transReqAck, eventParsed.payload, "TRANSACTION_REQ_ACK");
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::NAK:
                        {
                            EEPClient::nak transReqNak;
                            parsePayload(transReqNak, eventParsed.payload, "TRANSACTION_REQ_NAK");
                            //------ added on 08/12/2025
                            break;
                        }
                        default:
                        {
                            writelog("EEP Request Cmd: TRANSACTION_REQ_CMD, unknown response.", "OPR");
                            break;
                        }
                    }
                }
                else
                {
                    writelog("EEP Request Cmd: TRANSACTION_REQ_CMD, " + toString(eventParsed.messageStatus), "OPR");
                }
                break;
            }
            case EEPClient::CommandType::CPO_INFO_DISPLAY_REQ_CMD:
            {
                if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS))
                {
                    switch (static_cast<EEPClient::MESSAGE_CODE>(eventParsed.messageCode))
                    {
                        case EEPClient::MESSAGE_CODE::CPO_INFORMATION_DISPLAY_RESULT:
                        {
                            EEPClient::cpoInformationDisplayResult cpoInfoDisplayResult;
                            parsePayload(cpoInfoDisplayResult, eventParsed.payload, "CPO_INFORMATION_DISPLAY_RESULT");
                            //---- added on 08/12/2025
                            if (tProcess.fiLastEEPCmd == EEPClient::CommandType::CPO_INFO_DISPLAY_REQ_CMD) tProcess.fiLastEEPCmd = EEPClient:: CommandType::EEP_idle;
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::ACK:
                        {
                            EEPClient::ack cpoInformationDisplayAck;
                            parsePayload(cpoInformationDisplayAck, eventParsed.payload, "CPO_INFO_DISPLAY_REQ_ACK");
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::NAK:
                        {
                            EEPClient::nak cpoInformationDisplayNak;
                            parsePayload(cpoInformationDisplayNak, eventParsed.payload, "CPO_INFO_DISPLAY_REQ_NAK");
                            //----- added on 08/12/2025
                            break;
                        }
                        default:
                        {
                            writelog("EEP Request Cmd: CPO_INFO_DISPLAY_REQ_CMD, unknown response.", "OPR");
                            break;
                        }
                    }
                }
                else
                {
                    writelog("EEP Request Cmd: CPO_INFO_DISPLAY_REQ_CMD, " + toString(eventParsed.messageStatus), "OPR");
                }
                break;
            }
            case EEPClient::CommandType::CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD:
            {
                if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS))
                {
                    switch (static_cast<EEPClient::MESSAGE_CODE>(eventParsed.messageCode))
                    {
                        case EEPClient::MESSAGE_CODE::CARPARK_PROCESS_COMPLETE_RESULT:
                        {
                            EEPClient::carparkProcessCompleteResult processCompleteResult;
                            parsePayload(processCompleteResult, eventParsed.payload, "CARPARK_PROCESS_COMPLETE_RESULT");
                            //----- added on 10/12/2025
                            if (tProcess.fiLastEEPCmd == EEPClient::CommandType::CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD) tProcess.fiLastEEPCmd = EEPClient:: CommandType::EEP_idle;
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::ACK:
                        {
                            EEPClient::ack processCompleteResultAck;
                            parsePayload(processCompleteResultAck, eventParsed.payload, "CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_ACK");
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::NAK:
                        {
                            EEPClient::nak processCompleteResultNak;
                            parsePayload(processCompleteResultNak, eventParsed.payload, "CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_NAK");
                            break;
                        }
                        default:
                        {
                            writelog("EEP Request Cmd: CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD, unknown response.", "OPR");
                            break;
                        }
                    }
                }
                else
                {
                    writelog("EEP Request Cmd: CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD, " + toString(eventParsed.messageStatus), "OPR");
                }
                break;
            }
            case EEPClient::CommandType::DSRC_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD:
            {
                if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS))
                {
                    switch (static_cast<EEPClient::MESSAGE_CODE>(eventParsed.messageCode))
                    {
                        case EEPClient::MESSAGE_CODE::ACK:
                        {
                            EEPClient::ack dsrcProcessCompleteNotifAck;
                            parsePayload(dsrcProcessCompleteNotifAck, eventParsed.payload, "DSRC_PROCESS_COMPLETE_NOTIFICATION_REQ_ACK");
                             //---- added on 10/12/2025
                            if (tProcess.fiLastEEPCmd == EEPClient::CommandType::DSRC_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD) tProcess.fiLastEEPCmd = EEPClient:: CommandType::EEP_idle;
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::NAK:
                        {
                            EEPClient::nak dsrcProcessCompleteNotifNak;
                            parsePayload(dsrcProcessCompleteNotifNak, eventParsed.payload, "DSRC_PROCESS_COMPLETE_NOTIFICATION_REQ_NAK");
                            break;
                        }
                        default:
                        {
                            writelog("EEP Request Cmd: DSRC_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD, unknown response.", "OPR");
                            break;
                        }
                    }
                }
                else
                {
                    writelog("EEP Request Cmd: DSRC_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD, " + toString(eventParsed.messageStatus), "OPR");
                }
                break;
            }
            case EEPClient::CommandType::STOP_REQ_OF_RELATED_INFO_DISTRIBUTION_CMD:
            {
                if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS))
                {
                    switch (static_cast<EEPClient::MESSAGE_CODE>(eventParsed.messageCode))
                    {
                        case EEPClient::MESSAGE_CODE::ACK:
                        {
                            EEPClient::ack stopReqOfRelatedInfoDistAck;
                            parsePayload(stopReqOfRelatedInfoDistAck, eventParsed.payload, "STOP_REQ_OF_RELATED_INFO_DISTRIBUTION_ACK");
                             //---- added on 10/12/2025
                            if (tProcess.fiLastEEPCmd == EEPClient::CommandType::STOP_REQ_OF_RELATED_INFO_DISTRIBUTION_CMD) tProcess.fiLastEEPCmd = EEPClient:: CommandType::EEP_idle;
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::NAK:
                        {
                            EEPClient::nak stopReqOfRelatedInfoDistNak;
                            parsePayload(stopReqOfRelatedInfoDistNak, eventParsed.payload, "STOP_REQ_OF_RELATED_INFO_DISTRIBUTION_NAK");
                            break;
                        }
                        default:
                        {
                            writelog("EEP Request Cmd: STOP_REQ_OF_RELATED_INFO_DISTRIBUTION_CMD, unknown response.", "OPR");
                            break;
                        }
                    }
                }
                else
                {
                    writelog("EEP Request Cmd: STOP_REQ_OF_RELATED_INFO_DISTRIBUTION_CMD, " + toString(eventParsed.messageStatus), "OPR");
                }
                break;
            }
            case EEPClient::CommandType::DSRC_STATUS_REQ_CMD:
            {
                if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS))
                {
                    switch (static_cast<EEPClient::MESSAGE_CODE>(eventParsed.messageCode))
                    {
                        case EEPClient::MESSAGE_CODE::DSRC_STATUS_RESPONSE:
                        {
                            EEPClient::dsrcStatusResponse dsrcStatusResp;
                            parsePayload(dsrcStatusResp, eventParsed.payload, "DSRC_STATUS_RESPONSE");
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::NAK:
                        {
                            EEPClient::nak dsrcStatusReqNak;
                            parsePayload(dsrcStatusReqNak, eventParsed.payload, "DSRC_STATUS_REQ_NAK");
                            break;
                        }
                        default:
                        {
                            writelog("EEP Request Cmd: DSRC_STATUS_REQ_CMD, unknown response.", "OPR");
                            break;
                        }
                    }
                }
                else
                {
                    writelog("EEP Request Cmd: DSRC_STATUS_REQ_CMD, " + toString(eventParsed.messageStatus), "OPR");
                }
                break;
            }
            case EEPClient::CommandType::TIME_CALIBRATION_REQ_CMD:
            {
                if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS))
                {
                    switch (static_cast<EEPClient::MESSAGE_CODE>(eventParsed.messageCode))
                    {
                        case EEPClient::MESSAGE_CODE::TIME_CALIBRATION_RESPONSE:
                        {
                            EEPClient::timeCalibrationResponse timeCalResp;
                            parsePayload(timeCalResp, eventParsed.payload, "TIME_CALIBRATION_RESPONSE");
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::NAK:
                        {
                            EEPClient::nak timeCalReqNak;
                            parsePayload(timeCalReqNak, eventParsed.payload, "TIME_CALIBRATION_REQ_NAK");
                            break;
                        }
                        default:
                        {
                            writelog("EEP Request Cmd: TIME_CALIBRATION_REQ_CMD, unknown response.", "OPR");
                            break;
                        }
                    }
                }
                else
                {
                    writelog("EEP Request Cmd: TIME_CALIBRATION_REQ_CMD, " + toString(eventParsed.messageStatus), "OPR");
                }
                break;
            }
            case EEPClient::CommandType::SET_CARPARK_AVAIL_REQ_CMD:
            {
                if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS))
                {
                    switch (static_cast<EEPClient::MESSAGE_CODE>(eventParsed.messageCode))
                    {
                        case EEPClient::MESSAGE_CODE::ACK:
                        {
                            EEPClient::ack setCarparkAvailReqAck;
                            parsePayload(setCarparkAvailReqAck, eventParsed.payload, "SET_CARPARK_AVAIL_REQ_ACK");
                             //---- added on 10/12/2025
                            if (tProcess.fiLastEEPCmd == EEPClient::CommandType::SET_CARPARK_AVAIL_REQ_CMD) tProcess.fiLastEEPCmd = EEPClient:: CommandType::EEP_idle;
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::NAK:
                        {
                            EEPClient::nak setCarparkAvailReqNak;
                            parsePayload(setCarparkAvailReqNak, eventParsed.payload, "SET_CARPARK_AVAIL_REQ_NAK");
                            break;
                        }
                        default:
                        {
                            writelog("EEP Request Cmd: SET_CARPARK_AVAIL_REQ_CMD, unknown response.", "OPR");
                            break;
                        }
                    }
                }
                else
                {
                    writelog("EEP Request Cmd: SET_CARPARK_AVAIL_REQ_CMD, " + toString(eventParsed.messageStatus), "OPR");
                }
                break;
            }
            case EEPClient::CommandType::CD_DOWNLOAD_REQ_CMD:
            {
                if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS))
                {
                    switch (static_cast<EEPClient::MESSAGE_CODE>(eventParsed.messageCode))
                    {
                        case EEPClient::MESSAGE_CODE::ACK:
                        {
                            EEPClient::ack cdDownloadReqAck;
                            parsePayload(cdDownloadReqAck, eventParsed.payload, "CD_DOWNLOAD_REQ_ACK");
                            //---- added on 10/12/2025
                            if (tProcess.fiLastEEPCmd == EEPClient::CommandType::CD_DOWNLOAD_REQ_CMD) tProcess.fiLastEEPCmd = EEPClient:: CommandType::EEP_idle;
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::NAK:
                        {
                            EEPClient::nak cdDownloadReqNak;
                            parsePayload(cdDownloadReqNak, eventParsed.payload, "CD_DOWNLOAD_REQ_NAK");
                            break;
                        }
                        default:
                        {
                            writelog("EEP Request Cmd: CD_DOWNLOAD_REQ_CMD, unknown response.", "OPR");
                            break;
                        }
                    }
                }
                else
                {
                    writelog("EEP Request Cmd: CD_DOWNLOAD_REQ_CMD, " + toString(eventParsed.messageStatus), "OPR");
                }
                break;
            }
            case EEPClient::CommandType::EEP_RESTART_INQUIRY_REQ_CMD:
            {
                if (eventParsed.messageStatus == static_cast<uint32_t>(EEPClient::MSG_STATUS::SUCCESS))
                {
                    switch (static_cast<EEPClient::MESSAGE_CODE>(eventParsed.messageCode))
                    {
                        case EEPClient::MESSAGE_CODE::ACK:
                        {
                            EEPClient::ack eepRestartInqReqAck;
                            parsePayload(eepRestartInqReqAck, eventParsed.payload, "EEP_RESTART_INQUIRY_REQ_ACK");
                            break;
                        }
                        case EEPClient::MESSAGE_CODE::NAK:
                        {
                            EEPClient::nak eepRestartInqReqNak;
                            parsePayload(eepRestartInqReqNak, eventParsed.payload, "EEP_RESTART_INQUIRY_REQ_NAK");
                            break;
                        }
                        default:
                        {
                            writelog("EEP Request Cmd: EEP_RESTART_INQUIRY_REQ_CMD, unknown response.", "OPR");
                            break;
                        }
                    }
                }
                else
                {
                    writelog("EEP Request Cmd: EEP_RESTART_INQUIRY_REQ_CMD, " + toString(eventParsed.messageStatus), "OPR");
                }
                break;
            }
        }
    }
    catch (const std::exception& ex)
    {
        writelog("Exception in " + std::string(__func__) + ": " + std::string(ex.what()), "EEP");
    }
}

void operation::EEPInq(int delay)
{
    //---- added on 03/07/2026
    BARCODE_READER::getInstance()->Ticket_In = 0;
    //---------
    if (tProcess.gbLoopApresent == false) 
    {
        writelog ("No Loop A while OBU Inq", "OPR");
        return;
    }
    if (tExit.giDeductionStatus == Doingdeduction) {
        writelog ("Doing deduction while send Inq to EEP", "OPR");
        return;
    }
    if  (tProcess.fiLastEEPCmd == EEPClient:: CommandType::GET_OBU_INFO_REQ_CMD)
    {
        writelog("last OBU Inq not return. Don't Inq Again.", "OPR");
        return;
    }
    //------- added on 14/07/2026
    if (delay > 0) {
        for (int i = 0; i < delay * 100; ++i)
        {
           // writelog ("Card in status:" + to_string(BARCODE_READER::getInstance()->Ticket_In), "OPR");
            if (BARCODE_READER::getInstance()->Ticket_In == 1 or LCSCReader::getInstance()->LCSCCard_In == 1 or Upt::getInstance()->UOPSCard_In == 1) {
                writelog("Ticket/Card detected. Skip sending GET_OBU_INFO_REQ.", "OPR");
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    //---------
    writelog ("Send Inq to EEP", "OPR");
    EEPClient::getInstance()->FnSendGetOBUInfoReq();
    tProcess.fiLastEEPCmd = EEPClient:: CommandType::GET_OBU_INFO_REQ_CMD;
}

void operation::EEPDebit(string OBU, float lFee, string entryTime, string exitTime)
{
    if (tProcess.gbLoopApresent == false)
    {
        writelog ("No loop A while send Deduction", "OPR");
        return;
    }
    if (tExit.giDeductionStatus == Doingdeduction) {
        writelog ("Doing deduction while send Deduction to EEP", "OPR");
        return;
    }
    EnableCashcard(false);
    //-------
    tExit.giDeductionStatus = Doingdeduction;
    tProcess.gbLastPaidStatus = false;
    //--------
    if (entryTime == "")  entryTime = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss(); 
    //-------
    int ideductAmt = lFee* 100;
    //------
    writelog ("Send EEP Deduction. OBU:" + OBU + " fee:" + std::to_string(ideductAmt), "OPR");
    //-----
    EEPClient::getInstance()->FnSendDeductReq(OBU,std::to_string(ideductAmt), entryTime, exitTime);
    
    tProcess.fiLastEEPCmd = EEPClient:: CommandType::DEDUCT_REQ_CMD; 
}

void operation::SendMsg2OBU(std::string OBU, int DType, std::string line1,std::string line2, std::string line3, std::string line4, std::string line5)
{

    EEPClient::getInstance()->FnSendCPOInfoDisplayReq(OBU, std::to_string(DType), line1,line2,line3,line4,line5);
    tProcess.fiLastEEPCmd = EEPClient:: CommandType::CPO_INFO_DISPLAY_REQ_CMD;  
}

void operation::ProcessOUBInformation(EEPClient::obuInformationNotification OBUInfo)
{
    std::string gsLogMsg;
    std::string gsOBU = Common::getInstance()->longToHex(OBUInfo.obulabel);
    std::string gsVCC;
    Common::getInstance()->FnUint32ToByteString(OBUInfo.vcc, gsVCC, 3);
    //-------
    writelog ("Receive EEP Information","OPR");
    if (tExit.giDeductionStatus == Doingdeduction) {
        writelog ("Doing Deduction, ignore it","OPR");
        return;
    }
    //------added on 02/03/2026
    if (tProcess.gsTailgateOBU == gsOBU) tProcess.gsTailgateOBU = "";
    //-------
    gsLogMsg = "OBU: " + gsOBU + ", LPN: " + Common::getInstance()->FnConvertVectorUint8ToString(OBUInfo.vechicleNumber);
    if (Common::getInstance()->ConvertVectorUint8ToHex(OBUInfo.can) == "0000000000000000")
    {
        gsLogMsg = gsLogMsg + ", CardNo: , CardValidity: ";
    }else {
        gsLogMsg = gsLogMsg + ", CardNo: " + Common::getInstance()->ConvertVectorUint8ToHex(OBUInfo.can) + ", CardValidity: ";
    }
    switch (OBUInfo.cardValidity) {
        case 0:
        {
            gsLogMsg = gsLogMsg + "Vaid Card, Card Balance: $" + Common::getInstance()->SetFeeFormat(((float)OBUInfo.cardBalance / 100.0f));
            break;
        }
        case 1:
        {
             gsLogMsg = gsLogMsg + "No Card";
             break;
        }
        case 2:
        {
             gsLogMsg = gsLogMsg + "InVaid Card";
             break;
        }
        case 3:
        {
            gsLogMsg = gsLogMsg + "Card issuer error";
            break;
        }
        case 4:
        {
            gsLogMsg = gsLogMsg + "Blacklist card";
            break;
        }
        default:
        {
            break;
        }
    }
    gsLogMsg = gsLogMsg + ", VCC: " + gsVCC;
    writelog (gsLogMsg, "OPR");
    //----------
    gsLogMsg = "Backend Account: " + std::string((OBUInfo.backendAccount == 1) ? "Valid" : "Invalid");
    gsLogMsg = gsLogMsg + ", Backend Setting: " + std::string((OBUInfo.backendSetting == 1) ? "ON" : "OFF");
    gsLogMsg = gsLogMsg + ", OBU Business Function: " + std::string((OBUInfo.businessFunctionStatus == 1) ? "Normal" : "OBU cannot debit");
    
    writelog (gsLogMsg, "OPR");
    //----------
    if (gtStation.iType == tientry){

        tEntry.sLPN[0]=Common::getInstance()->FnConvertVectorUint8ToString(OBUInfo.vechicleNumber);
        tEntry.sLPN[1]=Common::getInstance()->FnConvertVectorUint8ToString(OBUInfo.vechicleNumber);
        tEntry.VCC = gsVCC;
        VehicleCome(gsOBU);
    }else{
        tExit.iBackendAccount = OBUInfo.backendAccount;
        tExit.iBackendSetting = OBUInfo.backendSetting;
	    tExit.iBFunctionStatus= OBUInfo.businessFunctionStatus;
	    tExit.iOBUType = OBUInfo.typeObu;
        tExit.lpn = Common::getInstance()->FnConvertVectorUint8ToString(OBUInfo.vechicleNumber);
        tExit.sLPN[0] = tExit.lpn;
        tExit.sLPN[1] = tExit.lpn;
        tExit.VCC = gsVCC;

        if (Common::getInstance()->ConvertVectorUint8ToHex(OBUInfo.can) != "0000000000000000"){
             tExit.sCardNo =Common::getInstance()->ConvertVectorUint8ToHex(OBUInfo.can);
        }else tExit.sCardNo = "";
        tExit.iCardStatus = OBUInfo.cardValidity;
        if (tExit.iEEPPaymentResult == 5 && tExit.sDSerialNo != "") {
             EEPClient::getInstance()->FnSendTransactionReq(gsOBU, Common::getInstance()->FnStringToUint16(tExit.sDSerialNo));
        } else CheckIUorCardStatus(gsOBU,EEP,tExit.sCardNo,1,float(OBUInfo.cardBalance));
    }

}

void operation::processEEPTransData(EEPClient::transactionData transData)
{
    writelog ("Receive EEP Trans Data","OPR");

    std::string sObuLabel = Common::getInstance()->FnDecimalIntToHexString(transData.obuLabel, 5);
    std::string sCan = Common::getInstance()->FnConvertVectorUint8ToHexString(transData.can);
    //-----------
    if (transData.resultDeduction == 1 || transData.resultDeduction == 2 ||transData.resultDeduction == 8)
    {
        if (sObuLabel == tExit.sIUNo && tProcess.gbLoopApresent == true)
        {
            writelog ("Deduction successful", "OPR");
            if (sCan != "0000000000000000" ) {
                tExit.sCardNo = sCan;
                writelog ("Card Number: " + tExit.sCardNo, "OPR");
            }else tExit.sCardNo = "";
            tExit.sPaidAmt = float(transData.paymentFee) / 100.0f;
            tExit.sDSerialNo = std::to_string(transData.deductCommandSerialNum);
            tExit.iEEPPaymentResult = transData.resultDeduction;
            if(transData.indicationLastAutoLoad > 0) tExit.sTopupAmt = float(transData.autoLoadAmount) / 100.0f;
            tExit.iEEPTransRoute = transData.transactionRoute;
            //------
            if (transData.resultDeduction == 1) {
                tExit.sEEPpaymentTime = Common::getInstance()->longToHex(transData.fepTime);
                writelog ("Paid Amt: $" + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "(FE-Pay)", "OPR");
                writelog ("Card Number: " + tExit.sCardNo, "OPR");
                writelog ("Payment time: " + tExit.sEEPpaymentTime, "OPR");
            }else
            {
                if (transData.resultDeduction == 2) {
                    tExit.sEEPpaymentTime = Common::getInstance()->ConvertVectorUint8ToHex(transData.bepTimeOfReport);
                    writelog ("Paid Amt: $" + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "(BE-Pay)", "OPR");
                    writelog ("Payment time: " + tExit.sEEPpaymentTime, "OPR");
                }
                else{
                    tExit.sEEPpaymentTime = "";
                    writelog ("Paid Amt: $" + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "(later)", "OPR");
                }
            }
            if (tExit.sEEPpaymentTime != "")
            {
               tExit.sEEPpaymentTime = Common::getInstance()->FnStringConvertDBTime(tExit.sEEPpaymentTime);
            }
            DebitOK(tExit.sIUNo, tExit.sCardNo,Common::getInstance()->SetFeeFormat(tExit.sPaidAmt), Common::getInstance()->SetFeeFormat(float(transData.purseBalanceAfterTransaction / 100.0)), 1, Common::getInstance()->SetFeeFormat(tExit.sTopupAmt), EEP, tExit.sEEPpaymentTime);
        } else
        {
           // updateEEPtrans
          writelog ("Update Successful deduction trans", "OPR");
          db::getInstance()->UpdateEEPExitTrans(sObuLabel, std::to_string(transData.deductCommandSerialNum),sCan,float(transData.paymentFee) / 100.0f,float(transData.autoLoadAmount) / 100.0f,transData.transactionRoute,transData.resultDeduction); 
        }
        return;
    }

    if (transData.resultDeduction == 7){
        writelog ("No Deduction for double deduction is suspected", "OPR");
        tExit.sIUNo = sObuLabel;
        tExit.iEEPPaymentResult = transData.resultDeduction;
        if (sCan != "0000000000000000" ) {
                tExit.sCardNo = sCan;
                writelog ("Card Number: " + tExit.sCardNo, "OPR");
                tExit.iCardType = 1;
        }else tExit.sCardNo = "";
        //--------
        CloseExitOperation(RejectCode7);
        return;
    } 

    if (transData.resultDeduction == 9){
        writelog ("No Deduction for DSRC failure", "OPR");
        EnableCashcard(true);
        EEPInq(2);
        return;
    } 

    if (transData.resultDeduction == 5){
        writelog ("Unknown due to  DSRC disconnection", "OPR");
        tExit.iEEPPaymentResult = 5;
        tProcess.fiLastEEPCmd = EEPClient:: CommandType::GET_OBU_INFO_REQ_CMD;
        return;
    } 
    if (transData.resultDeduction == 0){
        if (transData.frontendPaymentViolation != 0 )
        {   
            if (transData.frontendPaymentViolation == 8) 
            {
                writelog("No deduction for Insufficient Balance","OPR");
                SendMsg2Server ("90", tExit.sIUNo + ",,,,,Insufficient Balance");
                ShowLEDMsg("Insufficient Bal^Pls Top Up", "Insufficient Bal^Pls Top Up");
                 if (tExit.iOBUType == 1 ) SendMsg2OBU(tExit.sIUNo,0,"Bal $" + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt),"Pls Top UP","","","");
                 else SendMsg2OBU(tExit.sIUNo,0,"Card Bal $" + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt),"Insufficient Bal","Pls Top UP","","");
                 tExit.giDeductionStatus = InsufficientBalance;
            }
            else
            {
                writelog("No deduction for Invalid Card","OPR");
                SendMsg2Server ("90", tExit.sIUNo + ",,,,,Invalid card");
                ShowLEDMsg("Invalid Card ^ Pls Present Valid Payment", "Invalid Card ^ Pls Present Valid Payment");
                if (tExit.iOBUType == 1 ) SendMsg2OBU(tExit.sIUNo,0,"Invalid Card","","","","");
                else SendMsg2OBU(tExit.sIUNo,0,"Invalid Card","Pls Present","Valid Payment","","");
                tExit.giDeductionStatus = CardFault;
            }
            EnableCashcard(true);
            EEPInq(2);
            return;
        }
        if (transData.backendPaymentViolation != 0) 
        {
           writelog("No deduction for invalid Backend Payment","OPR");
           SendMsg2Server ("90", tExit.sIUNo + ",,,,,Invalid Backend Payment");
           ShowLEDMsg("Fee: $" + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "pls^Present Valid Payment", "Fee: $" + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "pls^Present Valid Payment");
           if (tExit.iOBUType == 1 ) SendMsg2OBU(tExit.sIUNo,0,"Pls Present","Valid Payment","","","");
           else SendMsg2OBU(tExit.sIUNo,0,"Fee: $" + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt),"Pls Present", "Valid Payment","","");
           tExit.giDeductionStatus = WaitingCard;
           EnableCashcard(true);
           EEPInq(2);
           return;
        }
    }

}
//--------
// Processing Result :   0 = allow enter  1 = block enter  2 = same as lastIU
void operation::EndEEPprocess(int ProcessingResult)
{
    std::string gsIUNo;
    std::string sFee;
    std::string s1,s2,s3,s4,s5;
    int iRet;
    //----------
    tProcess.fbEEPEndProcessing = true;
    //--------
    if (gtStation.iType == tientry) {
        gsIUNo = tEntry.sIUTKNo;
        sFee = "00";
    }else {
        gsIUNo = tExit.sIUNo;
        sFee = std::to_string(tExit.sPaidAmt * 100);
    }
    //------
    writelog("send carpark process complete Notification","OPR");
    tProcess.fiLastEEPCmd = EEPClient:: CommandType::CARPARK_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD;
    //------ added on 15/07/2026
    EEPClient::getInstance()->EEPData_In = 0;
    EEPClient::getInstance()->FnSendCarparkProcessCompleteNotificationReq(gsIUNo,std::to_string(ProcessingResult),sFee);
    //--------
    for (int i = 0; i < 50; ++i)
    {
        if (EEPClient::getInstance()->EEPData_In == 1) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    //-------
    writelog ("send Display message to OBU","OPR");
    //------ added on 15/07/2026
    EEPClient::getInstance()->EEPData_In = 0;
    //------
    if (gtStation.iType == tientry) SendMsg2OBU(gsIUNo,0, "Welcome To EEP","","","","");
    else{
        if (ProcessingResult < 2) {
            if (ProcessingResult == 0)
            {
                if (tExit.iTransType != 1 && tExit.iTransType!= 4 && tExit.iTransType != 7 && tExit.sPaidAmt == 0 && tExit.iTransType != 10 ) 
                {
                    iRet = db::getInstance()->GetSeasonHolder(gsIUNo);
                    if (iRet == 11 ||iRet == 9 || iRet == 1) {
                        s1= "Authorised";
                        s2= "Parking";

                    }else{
                        s1 = "Season Parking";
                    }
                }
                else 
                {
                    if (tExit.sPaidAmt == 0) 
                    {
                        if (tExit.sFee == 0) s1 = "Grace Period";
                        else {
                            if (tExit.iEEPPaymentResult == 7) s1 = "Have a Nice Day!";
                            else{
                                if (tExit.iTransType == 10) s1= "Complimentary";
                                else{
                                    if (tExit.iOBUType == 0) {
                                        s1 = "Ticket Accepted.";
                                        s2 = "Fee Paid $" + sFee;
                                    }
                                    else{
                                        s1 = "Fee Paid";
                                        s2 = "$" + sFee;
                                    }
                                }
                            }      
                        }
                    }
                    else{
                        if (tExit.iOBUType == 1) {
                            s1 = "Have A";
                            s2 = "Nice Day";
                        }
                        else {
                            //s1 = "Entry Time : " + tExit.sEntryTime;
                            //s2 = "Paid Amt: $" + std::to_string(tExit.sPaidAmt);
                            s1 = "Have A Nice Day";
                        }
                    } 
                }
            }else 
            {
                s1 = "No Deduction, Pls pay by OBU";
            }

            SendMsg2OBU(gsIUNo,0, s1,s2,s3,s4,s5);
        }
    }
    //------
     for (int i = 0; i < 50; ++i)
    {
        if (EEPClient::getInstance()->EEPData_In == 1) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    //--------
    writelog ("send DSRC Process complete Notification", "OPR");
    tProcess.fiLastEEPCmd = EEPClient:: CommandType::DSRC_PROCESS_COMPLETE_NOTIFICATION_REQ_CMD;
    EEPClient::getInstance()->FnSendDSRCProcessCompleteNotificationReq(gsIUNo);
    //------added on 02/03/2026
    if (tProcess.gsTailgateOBU !="")
    {
        Clearme();
        writelog("Clear me for Enq Tailgate OBU", "OPR");
        EEPInq();
    }
    tProcess.fbEEPEndProcessing = false;

}

void operation::CHUInq(int delay)
{
    //------ add on 14/07/2026
    BARCODE_READER::getInstance()->Ticket_In = 0;
    //-------
    if  (tProcess.fiLastCHUCmd == iEntryInq )
    {
        writelog("last OBU Inq not return. Don't Inq Again.", "OPR");
        return;
    }
    if (delay > 0) {
        for (int i = 0; i < delay * 100; ++i)
        {
            if (BARCODE_READER::getInstance()->Ticket_In == 1) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    //---------
    SendMsg2CHU(iEntryInq, std::to_string(gtStation.iAntID));
    
}

void operation::CHUDebit(string OBU, float lFee, string sCardNo,float sCardBal)
{
    long lAmt;
    std::string sData;

    if (tExit.giDeductionStatus == Doingdeduction) {
        writelog ("Doing deduction while send Msg to CHU", "OPR");
        return;
    }

    if (lFee == 0){
        lAmt = (tExit.sFee - tExit.sRedeemAmt) * 100;
    }else{
        lAmt = lFee * 100;
    }
    if (lAmt == 0) {
        writelog ("Exit fee not set","OPR");
        return;
    }
    
    tExit.giDeductionStatus = Doingdeduction;
    tExit.sCardNo = sCardNo;
    tProcess.gbLastPaidStatus = false;
    tProcess.gsLastCardNo = sCardNo;
    tProcess.gfLastCardBal= GfeeFormat(sCardBal);

    sData = std::to_string(gtStation.iAntID) + "," + OBU + "," + std::to_string(lAmt);

    SendMsg2CHU(iDebit,sData);

}

void operation::SendMsg2CHU(eCHUCmd sCmd, string sData)
{
    std::string sMsg;

    if (tProcess.gbLoopApresent == false) 
    {
        writelog ("No Loop A while send Msg to CHU", "OPR");
        return;
    }

    sMsg = "["+ gtStation.sName+"|"+to_string(gtStation.iSID)+"|" + std::to_string(sCmd) + "|"+ sData +"|]";

    CHUClient::getInstance()->FnSendMsgToCHU(sMsg);

    writelog("Send to CHU: " + sMsg, "OPR");

    tProcess.fiLastCHUCmd = sCmd;
}

void operation::processCHU(const std::string& eventData)
{
    writelog ("Received CHU Data:" + eventData, "OPR");
    //--------
    if (tProcess.gbBarrierOpened == true) return;
    //---------
    int rxcmd;
	int n,i;
    int iRC;
    string sCHUDebitTime;
    string R4;
    string L4;
    string sMsg;
	//-------

	ParseData pField('[',']','|');
	n=pField.Parse(eventData.c_str());
		
	if(n < 4){
		writelog("Invalid data format", "OPR");
		return;
	}
		
	rxcmd = std::stoi(pField.Field(2));
    std::vector<std::string> tmpStr;
	boost::algorithm::split(tmpStr, pField.Field(3), boost::algorithm::is_any_of(","));
    iRC = std::stoi(tmpStr[0]);
    if (iRC == ChuRC_CNNBroken) {
        tProcess.fiLastCHUCmd = iInit;
        SendMsg2Server("90",",,,,,CHU Connection Down");
        return;
    }
    if ( tProcess.fiLastCHUCmd + 1 == rxcmd ) 
    {
        tProcess.fiLastCHUCmd = iInit;
        switch(rxcmd)
        {
            case iRes_EntryInq:
            {
                if (tExit.giDeductionStatus == Doingdeduction) {
                    writelog ("Doing Deduction, ignore it","OPR");
                    return;
                }
                //--------
                if (iRC == ChuRC_EntryWithCashcard || iRC == ChuRC_EntryWithoutCashcard)
                {   
                    if (std::stoi(tmpStr[1]) != gtStation.iAntID)
                    {
                        writelog ("Wrong Antenna ID", "OPR");
                        tProcess.fiLastCHUCmd = iInit;
                        CHUInq(3);
                        return;
                    }
                    tExit.sIUNo = tmpStr[2];
                    if (iRC == ChuRC_EntryWithCashcard) {
                        tExit.sCardNo = tmpStr[3];
                        tExit.iCardStatus = 00;
                    }
                    else {
                        tExit.sCardNo = "";
                        tExit.iCardStatus = 01;
                    }
                    CheckIUorCardStatus(tExit.sIUNo,CHU,tExit.sCardNo,std::stoi(tmpStr[5]),std::stof(tmpStr[4])/100);
                }else
                {
                    if (iRC == ChuRC_NAK) writelog("Received NACK!", "OPR");
                    else {
                        if (iRC == ChuRC_ACK) writelog("Received ACK!", "OPR");
                        else writelog ("Not valid Response", "OPR");
                    }
                    tProcess.fiLastCHUCmd = iInit;
                    CHUInq(2);
                    return;
                }
                break;
            }
            case iRes_Debit:
            {
               if(iRC == ChuRC_DebitOk)
               {
                    if (std::stoi(tmpStr[1]) != gtStation.iAntID || tExit.sIUNo != tmpStr[2])
                    {
                        if (std::stoi(tmpStr[1]) != gtStation.iAntID) writelog ("Wrong Antenna ID or IU ", "OPR");
                        else writelog ("Wrong IU when debit ", "OPR");
                        tProcess.fiLastCHUCmd = iInit;
                        CHUInq(2);
                        return;
                    }
                    sCHUDebitTime = Common::getInstance()->FnStringConvertDBTime(tmpStr[9]);
                    DebitOK(tmpStr[2], tmpStr[4],Common::getInstance()->SetFeeFormat(float(std::stoi(tmpStr[3])/100.0)), Common::getInstance()->SetFeeFormat(float(std::stoi(tmpStr[5]) / 100.0)), std::stoi(tmpStr[7]), Common::getInstance()->SetFeeFormat(float(std::stoi(tmpStr[8])/100.0)), CHU, sCHUDebitTime);
                    return;
               }
               if (iRC == ChuRC_DebitFail)
               {
                    //----- added on 12/03/2026
                    if (tmpStr[6] == "00000001") {
                        sMsg = "Fee:$" + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "^Tap/Insert Card";
                        ShowLEDMsg(sMsg, sMsg);
                        tExit.giDeductionStatus = WaitingCard;
                        EnableCashcard(true);
                        CHUInq(2);
                        return;
                    }
                    tExit.sCHUDebitCode = tmpStr[6];
                    writelog ("CHU Debit Fail, Error Code: "+ tmpStr[6], "OPR");
                    SendMsg2Server ("90",tmpStr[2] + "," + tmpStr[4] + ",,,1, Debit Fail: " + tmpStr[6]);
                    tExit.giDeductionStatus = WaitingCard;
                    //----- added on 03/03/2026
                    L4 = tmpStr[6].substr(1,4);
                    R4 = tmpStr[6].substr(5,8);
                    if (R4 == "0004") {
                        writelog ("Reversed Command Sequence.", "OPR");
                        if (tParas.giProcessReversedCMD == 0)
                        {
                            DebitOK(tmpStr[2], tmpStr[4],"", "", std::stoi(tmpStr[7]), "", CHU, sCHUDebitTime);
                            return;
                        }
                    }
                    else if (R4.substr(R4.length() - 1, 1) == "8"){
                        writelog ("CHU Return Low Balance.", "OPR");
                        ShowLEDMsg("Deduction Error^Check bal By TS","Deduction Error^Check bal By TS");
                    }
                    else if(L4 == "0004" || R4 == "0010" )
                    {
                        writelog ("CHU return Insufficient balance.", "OPR");
                        tExit.giDeductionStatus = InsufficientBalance;
                        tProcess.gsLastCardNo = "";
                        ShowLEDMsg("Deduction Error^Check bal By TS","Deduction Error^Check bal By TS");
                    }
                    else if (R4 == "0020" || R4 == "0040" || R4 == "00C0" || L4 > "0400")
                    {
                        writelog("IU problem.", "OPR");
                        ShowLEDMsg("IU Problem^Insert/Tap Card","IU Problem^Insert/Tap Card");
                    }
                    else 
                    {
                        ShowLEDMsg("Deduction Error^Check bal By TS","Deduction Error^Check bal By TS");
                    }
                    EnableCashcard(true);
                    return;
                }
                if (iRC == ChuRC_NAK)
                {
                    writelog("NAK,Try again.","OPR");
                    //ShowLEDMsg(tExitMsg.MsgExit_DebitNak[0], tExitMsg.MsgExit_DebitNak[1]);
                    tExit.giDeductionStatus = WaitingCard;
                    EnableCashcard(true);
                    CHUInq(3);
                }else
                {
                    writelog("unknown error,Check balance by TS", "OPR");
                    ShowLEDMsg("Deduction Error^Check bal By TS","Deduction Error^Check bal By TS");
                    tExit.giDeductionStatus = WaitingCard;
                    EnableCashcard(true);
                }
                break;
            }
            default:
                break;
        }
    } 
    else
    {
        writelog("Wrong Response", "OPR");
        if (rxcmd == iRes_Debit && iRC == ChuRC_DebitOk){
            sCHUDebitTime = Common::getInstance()->FnStringConvertDBTime(tmpStr[9]);
            if (tExit.sIUNo == tmpStr[2]) 
            {
                DebitOK(tmpStr[2], tmpStr[4],Common::getInstance()->SetFeeFormat(float(std::stoi(tmpStr[3])/100.0)), Common::getInstance()->SetFeeFormat(float(std::stoi(tmpStr[5]) / 100.0)), std::stoi(tmpStr[7]), Common::getInstance()->SetFeeFormat(float(std::stoi(tmpStr[8])/100.0)), CHU, sCHUDebitTime);
            }
            else
            {
                tExit1.sIUNo = tmpStr[2];
                tExit1.sCardNo = tmpStr[4];
                tExit1.sPaidAmt = float(std::stoi(tmpStr[3])/100.0);
                tExit1.sFee = tExit1.sPaidAmt;
                tExit1.iCardType = std::stoi(tmpStr[7]);
                tExit1.sTopupAmt = float(std::stoi(tmpStr[8])/100.0);
                tExit1.sExitTime = sCHUDebitTime;
                tExit1.sEntryTime = sCHUDebitTime;
                //------
                UpdateExit();
                //----
                writelog ("Update CHU return Trans", "OPR");
                tProcess.fiLastCHUCmd = iInit;
                CHUInq(2);
            }
        }
        return;
    }
    
}

void operation::UpdateExit()
{
    int iRet;
    //------- for CHU outstanding trans 
    if (tExit1.sIUNo == "") return;
    writelog ("Update Exit trans:"+ tExit1.sIUNo, "OPR");
    //--------
    tExit1.sGSTAmt = tExit1.sPaidAmt * tParas.gfGSTRate / (1 + tParas.gfGSTRate);
    //---
    iRet = db::getInstance()->insertexittrans(tExit1);
    // if (iRet == iCentralSuccess){
    //     iRet = db::getInstance()->updatemovementtrans(tExit1);
    // }
    //------ delete Local Entry
    // db::getInstance()->UpdateLocalEntry(tExit1.sIUNo);
    //----
    return;
}

void operation::shutdownOnIoThread()
{
    boost::system::error_code ec;

    if (pLCDIdleTimer_)
    {
        pLCDIdleTimer_->cancel(ec);
    }

    ec.clear();

    if (pLoopATimer_)
    {
        pLoopATimer_->cancel(ec);
    }

    ec.clear();

    if (pDailyProcessTimer_)
    {
        pDailyProcessTimer_->cancel(ec);
    }

    // udpclient::close() is asynchronous and bound to this same io_context.
    // Keep the work guard alive until these close requests have been issued,
    // then let queued cancellation/close completions drain naturally.
    if (pmsUdpClient_ != nullptr)
    {
        pmsUdpClient_->close();
    }

    if (monitorUdpClient_ != nullptr)
    {
        monitorUdpClient_->close();
    }

    writelog("OPERATION: [SHUTDOWN] Daily/LCD/LoopA timers and transports close requested", "OPR");
}

void operation::FnClose()
{
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);

    if (ioContext_.get_executor().running_in_this_thread())
    {
        writelog("OPERATION: [SHUTDOWN] Rejected | Reason=Called from OP_IO", "OPR");
        return;
    }

    if (!ioContextThread_.joinable())
    {
        stopping_.store(true);
        workGuard_.reset();

        pLCDIdleTimer_.reset();
        pLoopATimer_.reset();
        pDailyProcessTimer_.reset();
        monitorUdpClient_.reset();
        pmsUdpClient_.reset();

        isOperationInitialized_.store(false);
        running_.store(false);
        stopping_.store(false);
        return;
    }

    if (stopping_.exchange(true))
    {
        return;
    }

    writelog("OPERATION: [SHUTDOWN] Begin", "OPR");

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

    // Cancellation/close has now been issued on OP_IO. Releasing the guard
    // allows run() to return once already-queued completions have drained.
    workGuard_.reset();

    if (ioContextThread_.joinable())
    {
        ioContextThread_.join();
    }

    // No handler can touch these objects after OP_IO has joined.
    pLCDIdleTimer_.reset();
    pLoopATimer_.reset();
    pDailyProcessTimer_.reset();

    monitorUdpClient_.reset();
    pmsUdpClient_.reset();

    isOperationInitialized_.store(false);
    running_.store(false);
    stopping_.store(false);

    writelog("OPERATION: [SHUTDOWN] Completed", "OPR");
}
