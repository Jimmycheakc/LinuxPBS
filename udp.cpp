#include "udp.h"

#include "common.h"
#include "db.h"
#include "dio.h"
#include "gpio.h"
#include "ini_parser.h"
#include "log.h"
#include "operation.h"
#include "parsedata.h"
#include "shutdown_manager.h"

#include <charconv>
#include <cctype>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>


namespace
{

std::string_view trim(std::string_view text) noexcept
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


std::optional<int> parseInt(std::string_view text) noexcept
{
    text = trim(text);

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


std::vector<std::string> splitCsv(std::string_view text)
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


void logInvalidPacket(const std::string& reason)
{
    Logger::getInstance()->FnLog("UDP: [RX] Invalid packet | " + reason, "", "UDP");
}


void logReceivedPacket(std::string_view packet)
{
    operation::getInstance()->writelog("Received data:" + std::string(packet), "UDP");
}

} // namespace

udpclient::udpclient(
    boost::asio::io_context& ioContext,
    const std::string& serverAddress,
    unsigned short serverPort,
    unsigned short localPort,
    bool broadcast)
    : strand_(boost::asio::make_strand(ioContext)),
      socket_(strand_),
      serverEndpoint_(
          boost::asio::ip::make_address(serverAddress),
          serverPort),
      localPort_(localPort),
      isBroadcast_(broadcast)
{
}

udpclient::~udpclient()
{
    // The owner should call close(), drain the io_context, and only then
    // destroy this object. This direct close is an emergency fallback.
    boost::system::error_code ignored;
    socket_.cancel(ignored);
    socket_.close(ignored);
}

void udpclient::start()
{
    acceptingWork_.store(true);

    boost::asio::post(
        strand_,
        [this]()
        {
            startOnStrand();
        });
}

void udpclient::close()
{
    acceptingWork_.store(false);

    boost::asio::post(
        strand_,
        [this]()
        {
            closeOnStrand();
        });
}

void udpclient::send(std::string message)
{
    if (!acceptingWork_.load())
    {
        return;
    }

    boost::asio::post(
        strand_,
        [this, message = std::move(message)]() mutable
        {
            if (stopping_ || !started_)
            {
                return;
            }

            enqueueSendOnStrand(std::move(message));
        });
}

bool udpclient::FnGetMonitorStatus() const
{
    return monitorStatus_.load();
}

void udpclient::startOnStrand()
{
    if (started_)
    {
        return;
    }

    stopping_ = false;
    sendInProgress_ = false;
    sendQueue_.clear();

    boost::system::error_code error;

    socket_.open(boost::asio::ip::udp::v4(), error);
    if (error)
    {
        logTransportError("[START] Failed to open socket | Error=" + error.message());
        return;
    }

    socket_.bind(
        boost::asio::ip::udp::endpoint(
            boost::asio::ip::udp::v4(),
            localPort_),
        error);

    if (error)
    {
        logTransportError(
            "[START] Failed to bind socket | Port=" +
            std::to_string(localPort_) +
            " | Error=" +
            error.message());

        socket_.close(error);
        return;
    }

    if (isBroadcast_)
    {
        socket_.set_option(boost::asio::socket_base::broadcast(true), error);

        if (error)
        {
            logTransportError("[START] Failed to enable broadcast | Error=" + error.message());

            socket_.close(error);
            return;
        }
    }

    started_ = true;

    Logger::getInstance()->FnLog(
        "UDP: [START] Listening | LocalPort=" +
            std::to_string(localPort_) +
            " | Remote=" +
            serverEndpoint_.address().to_string() +
            ":" +
            std::to_string(serverEndpoint_.port()),
        "",
        "UDP");

    startReceiveOnStrand();
}

void udpclient::closeOnStrand()
{
    if (stopping_)
    {
        return;
    }

    stopping_ = true;
    monitorStatus_.store(false);

    // If an async_send_to() is outstanding, its buffer points to the current
    // queue front. Keep that front alive until the completion handler runs.
    // Pending datagrams behind it can be discarded immediately.
    if (sendInProgress_)
    {
        while (sendQueue_.size() > 1)
        {
            sendQueue_.pop_back();
        }
    }
    else
    {
        sendQueue_.clear();
    }

    boost::system::error_code ignored;

    socket_.cancel(ignored);
    socket_.close(ignored);

    started_ = false;
}

void udpclient::startReceiveOnStrand()
{
    if (stopping_ || !started_ || !socket_.is_open())
    {
        return;
    }

    socket_.async_receive_from(
        boost::asio::buffer(receiveBuffer_),
        senderEndpoint_,
        [this](
            const boost::system::error_code& error,
            std::size_t bytesReceived)
        {
            handleReceiveOnStrand(error, bytesReceived);
        });
}

void udpclient::handleReceiveOnStrand(const boost::system::error_code& error, std::size_t bytesReceived)
{
    if (error)
    {
        if (error == boost::asio::error::operation_aborted || stopping_)
        {
            return;
        }

        logTransportError("[RX] Receive failed | Error=" + error.message());

        startReceiveOnStrand();
        return;
    }

    try
    {
        const std::string senderIp = senderEndpoint_.address().to_string();

        // Ignore datagrams sent by this station itself.
        if (senderIp != operation::getInstance()->tParas.gsLocalIP)
        {
            if (localPort_ == 2008)
            {
                processMonitorData(std::string_view(receiveBuffer_.data(), bytesReceived));
            }
            else
            {
                const auto configuredLocalPort = parseInt(IniParser::getInstance()->FnGetLocalUDPPort());

                if (configuredLocalPort &&
                    *configuredLocalPort >= 0 &&
                    *configuredLocalPort <= 65535 &&
                    localPort_ ==
                        static_cast<unsigned short>(*configuredLocalPort))
                {
                    processData(std::string_view(receiveBuffer_.data(), bytesReceived));
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        Logger::getInstance()->FnLogExceptionError(std::string("udpclient::handleReceiveOnStrand | Exception: ") + e.what());
    }
    catch (...)
    {
        Logger::getInstance()->FnLogExceptionError("udpclient::handleReceiveOnStrand | Unknown exception");
    }

    startReceiveOnStrand();
}

void udpclient::enqueueSendOnStrand(std::string message)
{
    if (message.empty())
    {
        return;
    }

    sendQueue_.push_back(std::move(message));

    if (!sendInProgress_)
    {
        startSendOnStrand();
    }
}

void udpclient::startSendOnStrand()
{
    if (stopping_ ||
        !started_ ||
        !socket_.is_open() ||
        sendQueue_.empty() ||
        sendInProgress_)
    {
        return;
    }

    sendInProgress_ = true;

    // The deque front stays alive until the completion handler runs.
    socket_.async_send_to(
        boost::asio::buffer(sendQueue_.front()),
        serverEndpoint_,
        [this](
            const boost::system::error_code& error,
            std::size_t bytesSent)
        {
            handleSendOnStrand(error, bytesSent);
        });
}

void udpclient::handleSendOnStrand(const boost::system::error_code& error, std::size_t /*bytesSent*/)
{
    sendInProgress_ = false;

    if (!sendQueue_.empty())
    {
        sendQueue_.pop_front();
    }

    if (error)
    {
        if (error == boost::asio::error::operation_aborted || stopping_)
        {
            return;
        }

        logTransportError("[TX] Send failed | Error=" + error.message());
    }

    startSendOnStrand();
}

void udpclient::processMonitorData(std::string_view packet)
{
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
            logInvalidPacket("Monitor field count < 4");
            return;
        }

		const auto command = parseInt(fields.FieldView(2));

		if (!command)
        {
            logInvalidPacket("Invalid monitor command");
            return;
        }

		auto* op = operation::getInstance();

		switch (*command)
        {
            case CmdMonitorStatus:
            {
                logReceivedPacket(packet);

                const auto status = parseInt(fields.FieldView(3));
                if (!status)
                {
                    logInvalidPacket("Invalid monitor status");
                    break;
                }

                monitorStatus_.store(*status == 1);
                break;
            }

            case CmdMonitorEnquiry:
            {
                logReceivedPacket(packet);
                op->FnSendMyStatusToMonitor();
                break;
            }

            case CmdMonitorFeeTest:
            {
                logReceivedPacket(packet);
                break;
            }

            case CmdMonitorOutput:
            {
                logReceivedPacket(packet);

                const auto tokens = splitCsv(fields.FieldView(3));
                if (tokens.size() != 2)
                {
                    logInvalidPacket("Monitor output requires pin,value");
                    break;
                }

                const auto pinNumber = parseInt(tokens[0]);
                const auto pinValue = parseInt(tokens[1]);

                if (!pinNumber || !pinValue || (*pinValue != 0 && *pinValue != 1))
                {
                    op->writelog("Invalid DIO", "UDP");
                    break;
                }

                const int actualPin = DIO::getInstance()->FnGetOutputPinNum(*pinNumber);

                if (actualPin == 0)
                {
                    op->writelog("Invalid DIO", "UDP");
                    break;
                }

                auto* gpio = GPIOManager::getInstance()->FnGetGPIO(actualPin);

                if (gpio == nullptr)
                {
                    op->writelog("Nullptr, Invalid DIO", "UDP");
                    break;
                }

                gpio->FnSetValue(*pinValue);
                break;
            }

            case CmdDownloadIni:
            {
                logReceivedPacket(packet);
                op->writelog("download INI file", "UDP");

                const bool success = op->CopyIniFile(fields.Field(0), fields.Field(3));

                op->FnSendCmdDownloadIniAckToMonitor(success);
                break;
            }

            case CmdDownloadParam:
            {
                logReceivedPacket(packet);
                op->writelog("download Parameter", "UDP");

                op->m_db->downloadparameter();

                if (db::getInstance()->FnGetDatabaseErrorFlag() == 0)
                {
                    op->m_db->loadParam();
                    op->FnSendCmdDownloadParamAckToMonitor(true);
                }
                else
                {
                    op->FnSendCmdDownloadParamAckToMonitor(false);
                }

                break;
            }

            case CmdMonitorSyncTime:
            {
                logReceivedPacket(packet);
                op->FnSyncCentralDBTime();
                break;
            }

            case CmdStopStationSoftware:
            {
                logReceivedPacket(packet);
                op->SendMsg2Monitor("11", "99");

                // Use the application shutdown path instead of std::exit().
                ShutdownManager::getInstance()->gracefulShutdown();
                break;
            }

            case CmdMonitorStationVersion:
            {
                logReceivedPacket(packet);
                op->SendMsg2Monitor("313", SW_VERSION);
                break;
            }

            case CmdMonitorGetStationCurrLog:
            {
                logReceivedPacket(packet);
                op->FnSendCmdGetStationCurrLogToMonitor();
                break;
            }

            default:
                break;
        }
	}
	catch (const std::exception& e)
    {
        Logger::getInstance()->FnLogExceptionError(std::string("udpclient::processMonitorData | Exception: ") + e.what());
    }
    catch (...)
    {
        Logger::getInstance()->FnLogExceptionError("udpclient::processMonitorData | Unknown exception");
    }
}

void udpclient::processData(std::string_view packet)
{
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
            logInvalidPacket("PMS field count < 4");
            return;
        }

        const auto command = parseInt(fields.FieldView(2));

        if (!command)
        {
            logInvalidPacket("Invalid PMS command");
            return;
        }

        auto* op = operation::getInstance();

        switch (*command)
        {
            case CmdStopStationSoftware:
            {
                logReceivedPacket(packet);
                op->SendMsg2Server("09", "11Stopping...");
                op->writelog("Exit by PMS", "UDP");
                ShutdownManager::getInstance()->gracefulShutdown();
                break;
            }

            case CmdStatusEnquiry:
            {
                logReceivedPacket(packet);
                op->Sendmystatus();
                break;
            }

            case CmdStatusOnline:
            {
                logReceivedPacket(packet);

                if (op->tProcess.giSystemOnline != 0)
                {
                    std::stringstream stream;
                    stream
                        << "Status: "
                        << (op->tProcess.giSystemOnline == 0
                                ? "Online"
                                : "Offline");

                    op->writelog(stream.str(), "UDP");

                    op->tProcess.giSystemOnline = 0;

                    if (op->tProcess.glNoofOfflineData > 0)
                    {
                        db::getInstance()->moveOfflineTransToCentral();
                    }
                }

                op->SendMsg2Server("99", "");
                break;
            }

            case CmdUpdateSeason:
            {
                logReceivedPacket(packet);

                const int result = op->m_db->downloadseason();
                if (result > 0)
                {
                    std::stringstream stream;
                    stream << "Download " << result << " Season";
                    op->SendMsg2Server("99", stream.str());
                }
                break;
            }

            case CmdDownloadMsg:
            {
                logReceivedPacket(packet);
                op->writelog("download LED message", "UDP");

                const int result = op->m_db->downloadledmessage();
                if (result > 0)
                {
                    std::stringstream stream;
                    stream << "Download " << result << " Messages";
                    op->SendMsg2Server("99", stream.str());
                }

                op->m_db->loadmessage();
                op->m_db->loadExitmessage();

                if (!op->tProcess.gbcarparkfull.load())
                {
                    if (op->gtStation.iType == tientry)
                    {
                        op->tProcess.setIdleMsg(0, op->tMsg.Msg_DefaultLED[0]);
                        op->tProcess.setIdleMsg(1, op->tMsg.Msg_Idle[1]);
                    }
                    else
                    {
                        op->tProcess.setIdleMsg(0, op->tExitMsg.MsgExit_XDefaultLED[0]);
                        op->tProcess.setIdleMsg(1, op->tExitMsg.MsgExit_XIdle[1]);
                    }
                }
                break;
            }

            case CmdFeeTest:
            {
                logReceivedPacket(packet);
                op->writelog("Fee test command", "UDP");

                const auto tokens = splitCsv(fields.FieldView(3));
                if (tokens.size() < 3)
                {
                    logInvalidPacket("Fee test requires at least 3 values");
                    break;
                }

                const auto feeType = parseInt(tokens[2]);
                if (!feeType)
                {
                    logInvalidPacket("Fee test has invalid fee type");
                    break;
                }

                const float parkingFee = op->m_db->CalFeeRAM2G(tokens[0], tokens[1], *feeType);

                if (parkingFee >= 0)
                {
                    std::string response = fields.Field(3);
                    response += "," + Common::getInstance()->SetFeeFormat(parkingFee) + ", Fee OK";

                    op->SendMsg2Server("302", response);
                }
                break;
            }

            case CmdDownloadXTariff:
            {
                logReceivedPacket(packet);
                op->writelog("download XTariff", "UDP");

                const int result = op->m_db->downloadxtariff( op->tParas.giGroupID, op->tParas.giSite, 0);

                if (result > 0)
                {
                    std::stringstream stream;
                    stream << "Download " << result << " XTariff";
                    op->SendMsg2Server("99", stream.str());
                    op->m_db->LoadXTariff();
                }
                break;
            }

            case CmdDownloadTariff:
            {
                logReceivedPacket(packet);

                op->writelog("download Tariff Type Info", "UDP");

                int result = op->m_db->downloadtarifftypeinfo();
                if (result > 0)
                {
                    op->m_db->LoadTariffTypeInfo();
                }

                op->writelog("download Tariff", "UDP");

                result = op->m_db->downloadtariffsetup(op->tParas.giGroupID, op->tParas.giSite, 0);

                if (result > 0)
                {
                    std::stringstream stream;
                    stream << "Download " << result << " Tariff";
                    op->SendMsg2Server("99", stream.str());
                    op->m_db->LoadTariff();
                }
                break;
            }

            case CmdDownloadHoliday:
            {
                logReceivedPacket(packet);
                op->writelog("download Holiday", "UDP");

                const int result = op->m_db->downloadholidaymst(1);
                if (result > 0)
                {
                    std::stringstream stream;
                    stream << "Download " << result << " Holiday";
                    op->SendMsg2Server("99", stream.str());
                }

                op->m_db->LoadHoliday();
                break;
            }

            case CmdUpdateParam:
            {
                logReceivedPacket(packet);
                op->writelog("download Parameter", "UDP");

                const int result = op->m_db->downloadparameter();
                if (result > 0)
                {
                    std::stringstream stream;
                    stream << "Download " << result << " Parameter";
                    op->SendMsg2Server("99", stream.str());
                }

                op->m_db->loadParam();
                break;
            }

            case CmdDownloadtype:
            {
                logReceivedPacket(packet);
                op->writelog("download Vehicle Type", "UDP");

                const int result = op->m_db->downloadvehicletype();
                if (result > 0)
                {
                    std::stringstream stream;
                    stream << "Download " << result << " Vehicle Type";
                    op->SendMsg2Server("99", stream.str());
                }

                op->m_db->loadvehicletype();
                break;
            }

            case CmdOpenBarrier:
            {
                logReceivedPacket(packet);
                op->writelog("open barrier from PMS", "UDP");
                op->ManualOpenBarrier(true);
                break;
            }

            case CmdCloseBarrier:
            {
                logReceivedPacket(packet);
                op->writelog("Close barrier from PMS", "UDP");

                op->ManualCloseBarrier();

                if (db::getInstance()->writeparameter2local("LockBarrier", "0") == 0)
                {
                    op->writelog("Update the parameter 'LockBarrier' successfully", "UDP");
                    op->tParas.gbLockBarrier = false;
                }

                op->SendMsg2Server("99", "Close Barrier");
                break;
            }

            case CmdContinueOpenBarrier:
            {
                logReceivedPacket(packet);
                op->writelog("Continue open barrier from PMS", "UDP");

                op->continueOpenBarrier();

                if (db::getInstance()->writeparameter2local("LockBarrier", "1") == 0)
                {
                    op->writelog("Update the parameter 'LockBarrier' successfully", "UDP");
                    op->tParas.gbLockBarrier = true;
                }

                op->SendMsg2Server("99", "Continue Open Barrier");
                break;
            }

            case CmdSetTime:
            {
                logReceivedPacket(packet);
                op->FnSyncCentralDBTime();
                break;
            }

            case CmdCarparkfull:
            {
                logReceivedPacket(packet);

                const auto rawValue = parseInt(fields.FieldView(3));
                if (!rawValue)
                {
                    logInvalidPacket("Invalid carpark-full value");
                    break;
                }

                const bool carparkFull = (*rawValue != 0);

                if (carparkFull != op->tProcess.gbcarparkfull.load())
                {
                    op->tProcess.gbcarparkfull.store(carparkFull);

                    if (!carparkFull)
                    {
                        const std::string iuNumber = op->tEntry.sIUTKNo;

                        if (op->tProcess.gbLoopApresent.load() && !iuNumber.empty())
                        {
                            op->PBSEntry(iuNumber);
                        }

                        if (op->gtStation.iType == tientry)
                        {
                            op->tProcess.setIdleMsg(0, op->tMsg.Msg_DefaultLED[0]);
                            op->tProcess.setIdleMsg(1, op->tMsg.Msg_Idle[1]);
                        }
                        else
                        {
                            op->tProcess.setIdleMsg(0, op->tExitMsg.MsgExit_XDefaultLED[0]);
                            op->tProcess.setIdleMsg(1, op->tExitMsg.MsgExit_XIdle[1]);
                        }
                    }
                    else
                    {
                        op->tProcess.setIdleMsg(0, op->tMsg.Msg_CarParkFull2LED[0]);
                        op->tProcess.setIdleMsg(1, op->tMsg.Msg_CarParkFull2LED[1]);
                    }
                }
                break;
            }

            case CmdClearSeason:
            {
                logReceivedPacket(packet);
                op->writelog("Clear Local season.", "UDP");
                op->m_db->clearseason();
                break;
            }

            case CmdUpdateSetting:
            {
                logReceivedPacket(packet);
                op->writelog("download station set up", "UDP");

                const int result = op->m_db->downloadstationsetup();
                if (result > 0)
                {
                    std::stringstream stream;
                    stream << "Download " << result << " Station Setup";
                    op->SendMsg2Server("99", stream.str());
                }

                op->m_db->loadstationsetup();
                break;
            }

            case CmdDownloadTR:
            {
                logReceivedPacket(packet);
                op->writelog("download TR", "UDP");

                const int result = op->m_db->downloadTR();
                if (result > 0)
                {
                    std::stringstream stream;
                    stream << "Download " << result << " TR";
                    op->SendMsg2Server("99", stream.str());
                }

                op->m_db->loadTR();
                break;
            }

            case CmdTimeForNoEntry:
            {
                logReceivedPacket(packet);

                if (op->tProcess.gbLoopApresent.load())
                {
                    const auto tokens = splitCsv(fields.FieldView(3));
                    if (tokens.size() < 2)
                    {
                        logInvalidPacket("Time-for-no-entry requires at least 2 values");
                        break;
                    }

                    op->tExit.sEntryTime = tokens[1];
                    op->writelog("Received Entry time: " + op->tExit.sEntryTime + " from PMS.", "UDP");

                    op->tExit.bNoEntryRecord = 0;
                    op->ReceivedEntryRecord();
                }
                else
                {
                    op->writelog("No Vehicle on the Loop.", "DB");
                }
                break;
            }

            case CmdSetLotCount:
                break;

            case CmdAvailableLots:
            {
                logReceivedPacket(packet);
                op->ShowTotalLots(fields.Field(3));
                break;
            }

            case CmdSetDioOutput:
            {
                logReceivedPacket(packet);

                // This command accesses Field(4), unlike most 4-field
                // commands, so validate the extra field explicitly.
                if (fieldCount < 5)
                {
                    logInvalidPacket("Set-DIO-output requires 5 fields");
                    break;
                }

                const auto pinNumber = parseInt(fields.FieldView(3));
                const auto pinValue = parseInt(fields.FieldView(4));

                if (!pinNumber || !pinValue || (*pinValue != 0 && *pinValue != 1))
                {
                    op->writelog("Invalid DIO", "UDP");
                    break;
                }

                const int actualPin = DIO::getInstance()->FnGetOutputPinNum(*pinNumber);

                if (actualPin == 0)
                {
                    op->writelog("Invalid DIO", "UDP");
                    break;
                }

                auto* gpio = GPIOManager::getInstance()->FnGetGPIO(actualPin);

                if (gpio == nullptr)
                {
                    op->writelog("Nullptr, Invalid DIO", "UDP");
                    break;
                }

                gpio->FnSetValue(*pinValue);
                break;
            }

            case CmdBroadcastSaveTrans:
            {
                const std::string fieldData = fields.Field(3);

                if (fieldData.find("Entry OK") != std::string::npos)
                {
                    logReceivedPacket(packet);

                    const std::string stationId = "," + fields.Field(1) + ",";

                    const std::string zoneEntries = op->tParas.gsZoneEntries;

                    if (zoneEntries.find(stationId) != std::string::npos)
                    {
                        const auto tokens = splitCsv(fieldData);
                        if (tokens.empty())
                        {
                            logInvalidPacket("Entry broadcast has no data");
                            break;
                        }

                        db::getInstance()->insertbroadcasttrans(fields.Field(1), tokens[0]);
                    }
                }
                else if (fieldData.find("Exit OK") != std::string::npos)
                {
                    logReceivedPacket(packet);

                    const auto tokens = splitCsv(fieldData);
                    if (tokens.empty())
                    {
                        logInvalidPacket("Exit broadcast has no data");
                        break;
                    }

                    db::getInstance()->UpdateLocalEntry(tokens[0]);
                }
                break;
            }

            case CmdEEPStatus:
            {
                logReceivedPacket(packet);
                op->SendMsg2Server("801", EEPClient::getInstance()->FnGetStatusData());
                break;
            }

            default:
                break;
        }
    }
    catch (const std::exception& e)
    {
        Logger::getInstance()->FnLogExceptionError(std::string("udpclient::processData | Exception: ") + e.what());
    }
    catch (...)
    {
        Logger::getInstance()->FnLogExceptionError("udpclient::processData | Unknown exception");
    }
}

void udpclient::logTransportError(const std::string& message) const
{
    Logger::getInstance()->FnLog("UDP: " + message, "", "UDP");
}

HeartbeatUdpServer::HeartbeatUdpServer(
    boost::asio::io_context& ioContext,
    const std::string& serverAddress,
    unsigned short serverPort)
    : strand_(boost::asio::make_strand(ioContext)),
      socket_(strand_),
      remoteEndpoint_(
          boost::asio::ip::make_address(serverAddress),
          serverPort),
      timer_(strand_)
{
}

HeartbeatUdpServer::~HeartbeatUdpServer()
{
    // Owner should stop(), drain the io_context, then destroy this object.
    boost::system::error_code ignored;
    timer_.cancel(ignored);
    socket_.cancel(ignored);
    socket_.close(ignored);
}

void HeartbeatUdpServer::start()
{
    boost::asio::post(
        strand_,
        [this]()
        {
            startOnStrand();
        });
}

void HeartbeatUdpServer::stop()
{
    boost::asio::post(
        strand_,
        [this]()
        {
            stopOnStrand();
        });
}

void HeartbeatUdpServer::startOnStrand()
{
    if (running_)
    {
        return;
    }

    stopping_ = false;

    boost::system::error_code error;

    socket_.open(boost::asio::ip::udp::v4(), error);
    if (error)
    {
        logError("[HEARTBEAT] Failed to open socket | Error=" + error.message());
        return;
    }

    // Bind to any local IPv4 interface and an ephemeral port.
    // The legacy code bound the local endpoint to serverAddress, which is
    // normally the remote address and can fail if it is not a local address.
    socket_.bind(
        boost::asio::ip::udp::endpoint(
            boost::asio::ip::udp::v4(),
            0),
        error);

    if (error)
    {
        logError("[HEARTBEAT] Failed to bind socket | Error=" + error.message());
        socket_.close(error);
        return;
    }

    running_ = true;

    sendHeartbeatOnStrand();
    scheduleNextHeartbeatOnStrand();
}

void HeartbeatUdpServer::stopOnStrand()
{
    if (stopping_)
    {
        return;
    }

    stopping_ = true;
    running_ = false;

    boost::system::error_code ignored;

    timer_.cancel(ignored);
    socket_.cancel(ignored);
    socket_.close(ignored);
}

void HeartbeatUdpServer::sendHeartbeatOnStrand()
{
    if (stopping_ || !running_ || !socket_.is_open())
    {
        return;
    }

    // Static storage duration guarantees buffer lifetime for async_send_to().
    socket_.async_send_to(
        boost::asio::buffer(
            kHeartbeatMessage,
            sizeof(kHeartbeatMessage) - 1),
        remoteEndpoint_,
        [this](
            const boost::system::error_code& error,
            std::size_t /*bytesTransferred*/)
        {
            if (!error)
            {
                return;
            }

            if (error == boost::asio::error::operation_aborted || stopping_)
            {
                return;
            }

            logError("[HEARTBEAT] Send failed | Error=" + error.message());
        });
}

void HeartbeatUdpServer::scheduleNextHeartbeatOnStrand()
{
    if (stopping_ || !running_)
    {
        return;
    }

    timer_.expires_after(kHeartbeatInterval);

    timer_.async_wait(
        [this](const boost::system::error_code& error)
        {
            if (error)
            {
                if (error == boost::asio::error::operation_aborted ||
                    stopping_)
                {
                    return;
                }

                logError("[HEARTBEAT] Timer failed | Error=" + error.message());

                // An unexpected timer error does not recursively restart from
                // inside an error handler. Stop this heartbeat cycle cleanly.
                running_ = false;
                return;
            }

            sendHeartbeatOnStrand();
            scheduleNextHeartbeatOnStrand();
        });
}

void HeartbeatUdpServer::logError(const std::string& message) const
{
    Logger::getInstance()->FnLog("UDP: " + message, "", "UDP");
}
