#include "lpr.h"

#include <exception>
#include <future>
#include <sstream>
#include <stdexcept>
#include <utility>

#if defined(__linux__)
#include <pthread.h>
#endif

#include <boost/algorithm/string.hpp>

#include "event_manager.h"
#include "ini_parser.h"
#include "log.h"

namespace
{
constexpr const char* kDisabledCameraIp = "1.1.1.1";
constexpr const char* kDefaultCameraChannel = "CH1";
}

Lpr::Lpr()
    : logFileName_("lpr"),
      lprIp4Front_(),
      lprIp4Rear_(),
      lprPort_(0),
      frontCamCH_(),
      rearCamCH_(),
      ioContext_(),
      frontReconnectTimer_(ioContext_),
      rearReconnectTimer_(ioContext_),
      workGuard_(),
      ioThread_(),
      pFrontCamera_(),
      pRearCamera_(),
      frontCameraInitialized_(false),
      rearCameraInitialized_(false),
      frontCameraConnected_(false),
      rearCameraConnected_(false),
      frontReconnectAttempt_(0),
      rearReconnectAttempt_(0),
      running_(false),
      stopping_(false),
      initialized_(false),
      acceptingWork_(false)
{
}

Lpr::~Lpr()
{
    // Emergency fallback only. Normal application shutdown should call
    // FnLprClose() before Logger::FnShutdown(). Do not log from this destructor
    // because static destruction order across translation units is undefined.
    acceptingWork_.store(false);
    stopping_.store(true);

    if (ioThread_.joinable())
    {
        ioContext_.stop();

        if (std::this_thread::get_id() != ioThread_.get_id())
        {
            ioThread_.join();
        }
    }
}

Lpr* Lpr::getInstance()
{
    static Lpr instance;
    return &instance;
}

void Lpr::FnLprInit()
{
    std::unique_lock<std::mutex> lock(lifecycleMutex_);

    if (initialized_.load())
    {
        return;
    }

    try
    {
        Logger::getInstance()->FnCreateLogFile(logFileName_);
        logModule("[INIT] Starting");

        // Read configuration before starting the LPR thread. At this point no
        // LPR async work is running, so committing these values is safe.
        const std::string frontIp = IniParser::getInstance()->FnGetLPRIP4Front();
        const std::string rearIp = IniParser::getInstance()->FnGetLPRIP4Rear();
        const int port = std::stoi(IniParser::getInstance()->FnGetLPRPort());

        lprIp4Front_ = frontIp;
        lprIp4Rear_ = rearIp;
        lprPort_ = port;

        {
            std::ostringstream oss;
            oss << "[INIT] Configuration" << " | FrontIP=" << lprIp4Front_ << " | RearIP=" << lprIp4Rear_ << " | Port=" << lprPort_ << " | Reconnect=" << kReconnectInterval.count() << "ms";
            logModule(oss.str());
        }

        ioContext_.restart();
        stopping_.store(false);
        acceptingWork_.store(false);

        if (!workGuard_)
        {
            workGuard_.emplace(boost::asio::make_work_guard(ioContext_));
        }

        auto resultPromise = std::make_shared<std::promise<bool>>();
        auto resultFuture = resultPromise->get_future();

        boost::asio::post(
            ioContext_,
            [this, resultPromise]()
            {
                bool success = false;

                try
                {
                    success = initializeOnIoThread();
                }
                catch (const std::exception& e)
                {
                    std::ostringstream oss;
                    oss << "LPR initialization exception: " << e.what();
                    Logger::getInstance()->FnLogExceptionError(oss.str());
                }
                catch (...)
                {
                    Logger::getInstance()->FnLogExceptionError(
                        "LPR initialization unknown exception");
                }

                resultPromise->set_value(success);
            });

        startModuleThread();

        const bool success = resultFuture.get();

        if (!success)
        {
            acceptingWork_.store(false);
            stopping_.store(true);

            boost::asio::post(
                ioContext_,
                [this]()
                {
                    shutdownOnIoThread();
                });

            if (workGuard_)
            {
                workGuard_->reset();
            }

            lock.unlock();

            if (ioThread_.joinable())
            {
                ioThread_.join();
            }

            lock.lock();

            workGuard_.reset();
            pFrontCamera_.reset();
            pRearCamera_.reset();
            running_.store(false);
            initialized_.store(false);
            stopping_.store(false);

            logModule("[INIT] Failed");
            return;
        }

        initialized_.store(true);
        acceptingWork_.store(true);
        logModule("[INIT] Completed");
    }
    catch (const std::exception& e)
    {
        acceptingWork_.store(false);
        initialized_.store(false);

        std::ostringstream oss;
        oss << __func__ << " | Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(oss.str());
    }
    catch (...)
    {
        acceptingWork_.store(false);
        initialized_.store(false);
        Logger::getInstance()->FnLogExceptionError(std::string(__func__) + " | Unknown exception");
    }
}

void Lpr::startModuleThread()
{
    if (ioThread_.joinable())
    {
        return;
    }

    running_.store(true);

    ioThread_ = std::thread(
        [this]()
        {
#if defined(__linux__)
            ::pthread_setname_np(::pthread_self(), "LPR_IO");
#endif
            logModule("[THREAD] io_context started");

            try
            {
                ioContext_.run();
            }
            catch (const std::exception& e)
            {
                std::ostringstream oss;
                oss << "LPR io_context exception: " << e.what();
                Logger::getInstance()->FnLogExceptionError(oss.str());
            }
            catch (...)
            {
                Logger::getInstance()->FnLogExceptionError("LPR io_context unknown exception");
            }

            running_.store(false);
            logModule("[THREAD] io_context stopped");
        });
}

bool Lpr::initializeOnIoThread()
{
    frontCameraInitialized_ = false;
    rearCameraInitialized_ = false;
    frontCameraConnected_ = false;
    rearCameraConnected_ = false;
    frontReconnectAttempt_ = 0;
    rearReconnectAttempt_ = 0;

    const bool frontReady = initCameraOnIoThread( CType::FRONT_CAMERA, lprIp4Front_, lprPort_, kDefaultCameraChannel);

    const bool rearReady = initCameraOnIoThread(CType::REAR_CAMERA, lprIp4Rear_, lprPort_, kDefaultCameraChannel);

    if (frontReady)
    {
        startReconnectLoopOnIoThread(CType::FRONT_CAMERA);
    }

    if (rearReady)
    {
        startReconnectLoopOnIoThread(CType::REAR_CAMERA);
    }

    // A camera may intentionally be disabled using the configured sentinel IP.
    // Therefore module initialization itself is successful even when one or both
    // cameras are not enabled.
    return true;
}

bool Lpr::initCameraOnIoThread(CType cameraType, const std::string& cameraIP, int tcpPort, const std::string& cameraCH)
{
    const char* name = cameraName(cameraType);

    if (cameraIP.empty())
    {
        std::ostringstream oss;
        oss << "[CAMERA] " << name << " | Initialization skipped" << " | Reason=Blank IP";
        logMainAndModule(oss.str());
        return false;
    }

    if (cameraIP == kDisabledCameraIp)
    {
        std::ostringstream oss;
        oss << "[CAMERA] " << name << " | Disabled" << " | IP=" << cameraIP;
        logMainAndModule(oss.str());
        return false;
    }

    try
    {
        std::unique_ptr<AppTcpClient>& camera = (cameraType == CType::FRONT_CAMERA) ? pFrontCamera_ : pRearCamera_;

        std::string& channel = (cameraType == CType::FRONT_CAMERA) ? frontCamCH_ : rearCamCH_;

        channel = cameraCH;
        camera = std::make_unique<AppTcpClient>(ioContext_, cameraIP, tcpPort);

        // AppTcpClient callbacks are posted back onto this module's io_context.
        // This keeps all mutable LPR state owned by the single LPR I/O thread,
        // even if AppTcpClient changes its callback execution context later.
        camera->setConnectHandler(
            [this, cameraType](bool success, const std::string& message)
            {
                boost::asio::post(
                    ioContext_,
                    [this, cameraType, success, message]()
                    {
                        handleCameraConnect(cameraType, success, message);
                    });
            });

        camera->setCloseHandler(
            [this, cameraType](bool success, const std::string& message)
            {
                boost::asio::post(
                    ioContext_,
                    [this, cameraType, success, message]()
                    {
                        handleCameraClose(cameraType, success, message);
                    });
            });

        camera->setReceiveHandler(
            [this, cameraType](bool success, const std::vector<std::uint8_t>& data)
            {
                boost::asio::post(
                    ioContext_,
                    [this, cameraType, success, data]()
                    {
                        handleCameraReceive(cameraType, success, data);
                    });
            });

        camera->setSendHandler(
            [this, cameraType](bool success, const std::string& message)
            {
                boost::asio::post(
                    ioContext_,
                    [this, cameraType, success, message]()
                    {
                        handleCameraSend(cameraType, success, message);
                    });
            });

        cameraInitializedRef(cameraType) = true;
        camera->connect();

        std::ostringstream oss;
        oss << "[CAMERA] " << name
            << " | Initialized"
            << " | IP=" << cameraIP
            << " | Port=" << tcpPort
            << " | Channel=" << cameraCH;
        logMainAndModule(oss.str());

        return true;
    }
    catch (const std::exception& e)
    {
        cameraInitializedRef(cameraType) = false;

        std::ostringstream oss;
        oss << "[CAMERA] " << name << " | Initialization failed" << " | Error=" << e.what();
        logModule(oss.str());
        Logger::getInstance()->FnLogExceptionError(oss.str());
        return false;
    }
    catch (...)
    {
        cameraInitializedRef(cameraType) = false;

        std::ostringstream oss;
        oss << "[CAMERA] " << name << " | Initialization failed" << " | Unknown error";
        logModule(oss.str());
        Logger::getInstance()->FnLogExceptionError(oss.str());
        return false;
    }
}

void Lpr::startReconnectLoopOnIoThread(CType cameraType)
{
    boost::asio::co_spawn(
        ioContext_,
        reconnectLoopAsync(cameraType),
        [this, cameraType](std::exception_ptr ep)
        {
            if (!ep)
            {
                return;
            }

            try
            {
                std::rethrow_exception(ep);
            }
            catch (const std::exception& e)
            {
                std::ostringstream oss;
                oss << "LPR " << cameraName(cameraType) << " reconnect coroutine exception: " << e.what();
                Logger::getInstance()->FnLogExceptionError(oss.str());
            }
            catch (...)
            {
                std::ostringstream oss;
                oss << "LPR " << cameraName(cameraType) << " reconnect coroutine unknown exception";
                Logger::getInstance()->FnLogExceptionError(oss.str());
            }
        });
}

boost::asio::awaitable<void> Lpr::reconnectLoopAsync(CType cameraType)
{
    auto& timer = reconnectTimer(cameraType);
    auto& attempt = reconnectAttemptRef(cameraType);

    while (!stopping_.load() && cameraInitialized(cameraType))
    {
        timer.expires_after(kReconnectInterval);

        boost::system::error_code ec;
        co_await timer.async_wait(
            boost::asio::redirect_error(
                boost::asio::use_awaitable,
                ec));

        if (ec == boost::asio::error::operation_aborted)
        {
            if (stopping_.load())
            {
                break;
            }

            continue;
        }

        if (ec)
        {
            std::ostringstream oss;
            oss << "[RECONNECT] " << cameraName(cameraType) << " | Timer error=" << ec.message();
            logModule(oss.str());
            break;
        }

        if (stopping_.load() || !cameraInitialized(cameraType))
        {
            break;
        }

        AppTcpClient* camera = cameraOnIoThread(cameraType);

        if (camera == nullptr)
        {
            break;
        }

        if (!camera->isConnected())
        {
            ++attempt;

            // Avoid filling the log every two seconds during a long outage.
            // Log the first attempt and then every tenth attempt.
            if (attempt == 1 || (attempt % 10) == 0)
            {
                std::ostringstream oss;
                oss << "[RECONNECT] " << cameraName(cameraType)
                    << " | Attempt=" << attempt
                    << " | IP=" << cameraIp(cameraType)
                    << " | Port=" << lprPort_;
                logModule(oss.str());
            }

            camera->connect();
        }
    }

    co_return;
}

void Lpr::handleCameraConnect(CType cameraType, bool success, const std::string& message)
{
    if (stopping_.load())
    {
        return;
    }

    bool& connected = cameraConnectedRef(cameraType);
    std::uint64_t& attempt = reconnectAttemptRef(cameraType);

    if (success)
    {
        const bool wasConnected = connected;
        connected = true;
        attempt = 0;

        if (!wasConnected)
        {
            std::ostringstream oss;
            oss << "[CONNECT] " << cameraName(cameraType)
                << " | Connected"
                << " | IP=" << cameraIp(cameraType)
                << " | Port=" << lprPort_;
            logModule(oss.str());
        }

        return;
    }

    const bool wasConnected = connected;
    connected = false;

    // Log the initial failure, a state change from connected to failed, and
    // periodic failures during a prolonged reconnect sequence.
    if (wasConnected || attempt <= 1 || (attempt % 10) == 0)
    {
        std::ostringstream oss;
        oss << "[CONNECT] " << cameraName(cameraType)
            << " | Failed"
            << " | IP=" << cameraIp(cameraType)
            << " | Port=" << lprPort_;

        if (!message.empty())
        {
            oss << " | Error=" << message;
        }

        logModule(oss.str());
    }
}

void Lpr::handleCameraClose(CType cameraType, bool success, const std::string& message)
{
    cameraConnectedRef(cameraType) = false;

    if (stopping_.load())
    {
        return;
    }

    std::ostringstream oss;
    oss << "[CONNECT] " << cameraName(cameraType)
        << " | Socket closed"
        << " | " << (success ? "OK" : "FAILED");

    if (!message.empty())
    {
        oss << " | Detail=" << message;
    }

    logModule(oss.str());
}

void Lpr::handleCameraSend(CType cameraType, bool success, const std::string& message)
{
    if (stopping_.load())
    {
        return;
    }

    if (success)
    {
        return;
    }

    std::ostringstream oss;
    oss << "[TX] " << cameraName(cameraType)
        << " | Failed";

    if (!message.empty())
    {
        oss << " | Error=" << message;
    }

    logModule(oss.str());
}

void Lpr::handleCameraReceive(CType cameraType, bool success, const std::vector<std::uint8_t>& data)
{
    if (stopping_.load())
    {
        return;
    }

    if (!success)
    {
        std::ostringstream oss;
        oss << "[RX] " << cameraName(cameraType)
            << " | Failed"
            << " | Likely socket read error";
        logModule(oss.str());
        return;
    }

    const std::string receiveData(reinterpret_cast<const char*>(data.data()), data.size());

    {
        std::ostringstream oss;
        oss << "[RX] " << cameraName(cameraType)
            << " | IP=" << cameraIp(cameraType)
            << " | Length=" << data.size()
            << " | Data=" << receiveData;
        logMainAndModule(oss.str());
    }

    processData(receiveData, cameraType);
}

void Lpr::FnSendTransIDToLPR(const std::string& transID, bool useFrontCamera)
{
    if (!acceptingWork_.load())
    {
        return;
    }

    const std::string request = buildLprRequest(transID);
    const CType cameraType = useFrontCamera ? CType::FRONT_CAMERA : CType::REAR_CAMERA;

    boost::asio::post(
        ioContext_,
        [this, request, cameraType]()
        {
            sendTransIDOnIoThread(request, cameraType);
        });
}

void Lpr::sendTransIDOnIoThread(const std::string& request, CType cameraType)
{
    if (stopping_.load())
    {
        return;
    }

    if (!cameraInitialized(cameraType))
    {
        std::ostringstream oss;
        oss << "[TX] " << cameraName(cameraType)
            << " | Skipped"
            << " | Camera not initialized";
        logModule(oss.str());
        return;
    }

    AppTcpClient* camera = cameraOnIoThread(cameraType);

    if (camera == nullptr || !camera->isConnected())
    {
        std::ostringstream oss;
        oss << "[TX] " << cameraName(cameraType)
            << " | Failed"
            << " | Camera not connected";
        logMainAndModule(oss.str());
        return;
    }

    try
    {
        {
            std::ostringstream oss;
            oss << "[TX] " << cameraName(cameraType) << " | Data=" << request;
            logMainAndModule(oss.str());
        }

        const std::vector<std::uint8_t> data(request.begin(), request.end());

        camera->send(data);
    }
    catch (const std::exception& e)
    {
        std::ostringstream oss;
        oss << "[TX] " << cameraName(cameraType) << " | Exception=" << e.what();
        Logger::getInstance()->FnLogExceptionError(oss.str());
    }
    catch (...)
    {
        std::ostringstream oss;
        oss << "[TX] " << cameraName(cameraType) << " | Unknown exception";
        Logger::getInstance()->FnLogExceptionError(oss.str());
    }
}

std::string Lpr::buildLprRequest(const std::string& transID)
{
    std::vector<std::string> parts;
    boost::algorithm::split(parts, transID, boost::algorithm::is_any_of("-"));

    std::string dateTime;

    if (parts.size() == 3)
    {
        dateTime = parts[2];
    }

    std::ostringstream oss;
    oss << "#LPRS_STX#"
        << transID
        << "#"
        << dateTime
        << "#LPRS_ETX#";

    return oss.str();
}

void Lpr::processData(const std::string& tcpData, CType cameraType)
{
    try
    {
        const std::size_t dataLen = tcpData.size();
        const std::string cameraLabel = cameraName(cameraType);

        if (dataLen > 25)
        {
            std::vector<std::string> parts;
            boost::algorithm::split(parts, tcpData, boost::algorithm::is_any_of("#"));

            const bool normalLprFrame =
                ((parts.size() == 7) &&
                 (parts[1] == "LPRS_STX") &&
                 (parts[5] == "LPRS_ETX")) ||
                ((parts.size() == 8) &&
                 (parts[1] == "LPRS_STX") &&
                 (parts[6] == "LPRS_ETX"));

            const bool cascadedLprFrame =
                (parts.size() == 12) &&
                (parts[7] == "LPRS_STX") &&
                (parts[11] == "LPRS_ETX");

            if (normalLprFrame || cascadedLprFrame)
            {
                extractLPRData(tcpData, cameraType);
                return;
            }

            const std::string receivedStx = extractSTX(tcpData);
            const std::string receivedEtx = extractETX(tcpData);
            const std::string expectedStx = (cameraType == CType::FRONT_CAMERA) ? frontCamCH_ : rearCamCH_;

            if (boost::algorithm::to_upper_copy(receivedStx) != boost::algorithm::to_upper_copy(expectedStx))
            {
                std::ostringstream oss;
                oss << "[PARSE] " << cameraLabel
                    << " | Invalid STX"
                    << " | Expected=" << expectedStx
                    << " | Received=" << receivedStx;
                logModule(oss.str());
            }

            if (boost::algorithm::to_lower_copy(receivedEtx) != ".jpg")
            {
                std::ostringstream oss;
                oss << "[PARSE] " << cameraLabel
                    << " | Invalid ETX"
                    << " | Expected=.jpg"
                    << " | Received=" << receivedEtx;
                logModule(oss.str());
            }

            return;
        }

        if (dataLen == 5)
        {
            const std::string upper = boost::algorithm::to_upper_copy(tcpData);

            if (upper == "LPR_R")
            {
                std::ostringstream oss;
                oss << "[STATUS] " << cameraLabel << " | NP1400 running";
                logModule(oss.str());
            }
            else if (upper == "LPR_N")
            {
                std::ostringstream oss;
                oss << "[STATUS] " << cameraLabel << " | NP1400 not running";
                logModule(oss.str());
            }
        }
        else if (dataLen == 2 && boost::algorithm::to_upper_copy(tcpData) == "OK")
        {
            std::ostringstream oss;
            oss << "[RX] " << cameraLabel << " | OK";
            logModule(oss.str());
        }
    }
    catch (const std::exception& e)
    {
        std::ostringstream oss;
        oss << __func__ << " | Data=" << tcpData << " | Exception=" << e.what();
        Logger::getInstance()->FnLogExceptionError(oss.str());
    }
    catch (...)
    {
        std::ostringstream oss;
        oss << __func__ << " | Data=" << tcpData << " | Unknown exception";
        Logger::getInstance()->FnLogExceptionError(oss.str());
    }
}

std::string Lpr::extractSTX(const std::string& message)
{
    const std::size_t separatorPos = message.find('#');

    if (separatorPos == std::string::npos || separatorPos < 3)
    {
        return {};
    }

    return message.substr(separatorPos - 3, 3);
}

std::string Lpr::extractETX(const std::string& message)
{
    const std::size_t dotPos = message.rfind('.');

    if (dotPos == std::string::npos ||
        dotPos + 4 > message.size())
    {
        return {};
    }

    return message.substr(dotPos, 4);
}

void Lpr::extractLPRData(const std::string& tcpData, CType cameraType)
{
    std::vector<std::string> parts;
    boost::algorithm::split(parts, tcpData, boost::algorithm::is_any_of("#"));

    if (parts.size() == 7 || parts.size() == 8)
    {
        enqueueLprEvent(cameraType, parts[3], parts[2], parts[4]);
        return;
    }

    if (parts.size() == 12)
    {
        if (parts[7] != "LPRS_STX")
        {
            std::ostringstream oss;
            oss << "[PARSE] " << cameraName(cameraType) << " | Cascaded frame invalid STX" << " | Received=" << parts[7];
            logModule(oss.str());
            return;
        }

        if (parts[11] != "LPRS_ETX")
        {
            std::ostringstream oss;
            oss << "[PARSE] " << cameraName(cameraType) << " | Cascaded frame invalid ETX" << " | Received=" << parts[11];
            logModule(oss.str());
            return;
        }

        enqueueLprEvent(cameraType, parts[9], parts[8], parts[10]);
        return;
    }

    std::ostringstream oss;
    oss << "[PARSE] " << cameraName(cameraType) << " | Invalid LPR format" << " | TokenCount=" << parts.size();
    logModule(oss.str());
}

void Lpr::enqueueLprEvent(CType cameraType, const std::string& lpn, const std::string& transID, const std::string& imagePath)
{
    LPREventData data;
    data.camType = cameraType;
    data.LPN = lpn;
    data.TransID = transID;
    data.imagePath = imagePath;

    EventManager::getInstance()->FnEnqueueEvent("Evt_handleLPRReceive", serializeEventData(data));

    std::ostringstream oss;
    oss << "[EVENT] LPRReceive"
        << " | Camera=" << cameraName(cameraType)
        << " | LPN=" << lpn
        << " | TransID=" << transID
        << " | ImagePath=" << imagePath;

    if (lpn == "0000000000")
    {
        oss << " | Recognition=NOT_RECOGNIZED";
    }
    else
    {
        oss << " | Recognition=OK";
    }

    logModule(oss.str());
}

std::string Lpr::serializeEventData(const LPREventData& eventData)
{
    std::ostringstream oss;
    oss << static_cast<int>(eventData.camType)
        << ","
        << eventData.LPN
        << ","
        << eventData.TransID
        << ","
        << eventData.imagePath;
    return oss.str();
}

Lpr::LPREventData Lpr::deserializeEventData(
    const std::string& serializeData) const
{
    LPREventData eventData{};

    try
    {
        std::stringstream ss(serializeData);
        std::string token;

        if (!std::getline(ss, token, ','))
        {
            throw std::runtime_error("missing camera type");
        }

        const int cameraTypeValue = std::stoi(token);

        if (cameraTypeValue != static_cast<int>(CType::FRONT_CAMERA) &&
            cameraTypeValue != static_cast<int>(CType::REAR_CAMERA))
        {
            throw std::runtime_error("invalid camera type");
        }

        eventData.camType = static_cast<CType>(cameraTypeValue);

        if (!std::getline(ss, eventData.LPN, ','))
        {
            throw std::runtime_error("missing LPN");
        }

        if (!std::getline(ss, eventData.TransID, ','))
        {
            throw std::runtime_error("missing transaction ID");
        }

        // Read the remainder so an image path containing a comma is preserved.
        if (!std::getline(ss, eventData.imagePath))
        {
            throw std::runtime_error("missing image path");
        }

        return eventData;
    }
    catch (const std::exception& e)
    {
        std::ostringstream oss;
        oss << __func__ << " | Data=" << serializeData << " | Exception=" << e.what();
        Logger::getInstance()->FnLogExceptionError(oss.str());
        return {};
    }
    catch (...)
    {
        std::ostringstream oss;
        oss << __func__ << " | Data=" << serializeData << " | Unknown exception";
        Logger::getInstance()->FnLogExceptionError(oss.str());
        return {};
    }
}

void Lpr::FnLprClose()
{
    std::unique_lock<std::mutex> lock(lifecycleMutex_);

    acceptingWork_.store(false);
    initialized_.store(false);

    const bool calledFromIoThread = ioThread_.joinable() && std::this_thread::get_id() == ioThread_.get_id();

    if (!running_.load() && !ioThread_.joinable())
    {
        return;
    }

    logModule("[SHUTDOWN] Starting");
    stopping_.store(true);

    if (running_.load())
    {
        if (calledFromIoThread)
        {
            shutdownOnIoThread();
        }
        else
        {
            boost::asio::post(
                ioContext_,
                [this]()
                {
                    shutdownOnIoThread();
                });
        }

        if (workGuard_)
        {
            workGuard_->reset();
        }
    }

    if (calledFromIoThread)
    {
        // A thread cannot join itself. The posted/cancelled work will drain and
        // io_context::run() will return. A later external close call or the
        // singleton destructor can then join the thread.
        return;
    }

    lock.unlock();

    if (ioThread_.joinable())
    {
        ioThread_.join();
    }

    lock.lock();

    // At this point io_context::run() has returned, so no camera callback can be
    // executing concurrently. It is safe to destroy the client objects here.
    pFrontCamera_.reset();
    pRearCamera_.reset();

    workGuard_.reset();
    running_.store(false);
    stopping_.store(false);

    frontCameraInitialized_ = false;
    rearCameraInitialized_ = false;
    frontCameraConnected_ = false;
    rearCameraConnected_ = false;
    frontReconnectAttempt_ = 0;
    rearReconnectAttempt_ = 0;

    logModule("[SHUTDOWN] Completed");
}

void Lpr::shutdownOnIoThread()
{
    boost::system::error_code ignored;
    frontReconnectTimer_.cancel(ignored);
    rearReconnectTimer_.cancel(ignored);

    frontCameraInitialized_ = false;
    rearCameraInitialized_ = false;
    frontCameraConnected_ = false;
    rearCameraConnected_ = false;

    try
    {
        if (pFrontCamera_)
        {
            pFrontCamera_->close();
        }
    }
    catch (...)
    {
        // Shutdown must continue even if a client close operation fails.
    }

    try
    {
        if (pRearCamera_)
        {
            pRearCamera_->close();
        }
    }
    catch (...)
    {
        // Shutdown must continue even if a client close operation fails.
    }
}

AppTcpClient* Lpr::cameraOnIoThread(CType cameraType)
{
    return cameraType == CType::FRONT_CAMERA ? pFrontCamera_.get() : pRearCamera_.get();
}

const std::string& Lpr::cameraIp(CType cameraType) const
{
    return cameraType == CType::FRONT_CAMERA ? lprIp4Front_ : lprIp4Rear_;
}

bool Lpr::cameraInitialized(CType cameraType) const
{
    return cameraType == CType::FRONT_CAMERA ? frontCameraInitialized_ : rearCameraInitialized_;
}

bool& Lpr::cameraInitializedRef(CType cameraType)
{
    return cameraType == CType::FRONT_CAMERA ? frontCameraInitialized_ : rearCameraInitialized_;
}

bool& Lpr::cameraConnectedRef(CType cameraType)
{
    return cameraType == CType::FRONT_CAMERA ? frontCameraConnected_ : rearCameraConnected_;
}

std::uint64_t& Lpr::reconnectAttemptRef(CType cameraType)
{
    return cameraType == CType::FRONT_CAMERA ? frontReconnectAttempt_ : rearReconnectAttempt_;
}

boost::asio::steady_timer& Lpr::reconnectTimer(CType cameraType)
{
    return cameraType == CType::FRONT_CAMERA ? frontReconnectTimer_ : rearReconnectTimer_;
}

const char* Lpr::cameraName(CType cameraType)
{
    return cameraType == CType::FRONT_CAMERA ? "Front Camera" : "Rear Camera";
}

void Lpr::logModule(const std::string& message) const
{
    Logger::getInstance()->FnLog(message, logFileName_, "LPR");
}

void Lpr::logMainAndModule(const std::string& message) const
{
    Logger::getInstance()->FnLog(message);
    logModule(message);
}