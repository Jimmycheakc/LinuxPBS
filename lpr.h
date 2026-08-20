#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio.hpp>

#include "tcp_client.h"

class Lpr
{
public:
    enum class CType
    {
        FRONT_CAMERA = 0,
        REAR_CAMERA = 1
    };

    struct LPREventData
    {
        CType camType{CType::FRONT_CAMERA};
        std::string LPN;
        std::string TransID;
        std::string imagePath;
    };

    static Lpr* getInstance();

    Lpr(const Lpr&) = delete;
    Lpr& operator=(const Lpr&) = delete;
    Lpr(Lpr&&) = delete;
    Lpr& operator=(Lpr&&) = delete;

    void FnLprInit();
    void FnLprClose();

    void FnSendTransIDToLPR(const std::string& transID, bool useFrontCamera);

    LPREventData deserializeEventData(const std::string& serializeData) const;

private:
    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    Lpr();
    ~Lpr();

    void startModuleThread();
    bool initializeOnIoThread();
    void shutdownOnIoThread();

    bool initCameraOnIoThread(CType cameraType, const std::string& cameraIP, int tcpPort, const std::string& cameraCH);

    void startReconnectLoopOnIoThread(CType cameraType);
    boost::asio::awaitable<void> reconnectLoopAsync(CType cameraType);

    void handleCameraConnect(CType cameraType, bool success, const std::string& message);

    void handleCameraClose(CType cameraType, bool success, const std::string& message);

    void handleCameraSend(CType cameraType, bool success, const std::string& message);

    void handleCameraReceive(CType cameraType, bool success, const std::vector<std::uint8_t>& data);

    void sendTransIDOnIoThread(const std::string& request, CType cameraType);

    AppTcpClient* cameraOnIoThread(CType cameraType);
    const std::string& cameraIp(CType cameraType) const;
    bool cameraInitialized(CType cameraType) const;
    bool& cameraInitializedRef(CType cameraType);
    bool& cameraConnectedRef(CType cameraType);
    std::uint64_t& reconnectAttemptRef(CType cameraType);
    boost::asio::steady_timer& reconnectTimer(CType cameraType);

    static const char* cameraName(CType cameraType);
    static std::string buildLprRequest(const std::string& transID);

    void processData(const std::string& tcpData, CType cameraType);
    static std::string extractSTX(const std::string& message);
    static std::string extractETX(const std::string& message);
    void extractLPRData(const std::string& tcpData, CType cameraType);
    void enqueueLprEvent(CType cameraType, const std::string& lpn, const std::string& transID, const std::string& imagePath);
    static std::string serializeEventData(const LPREventData& eventData);

    void logModule(const std::string& message) const;
    void logMainAndModule(const std::string& message) const;

    std::string logFileName_;
    std::string lprIp4Front_;
    std::string lprIp4Rear_;
    int lprPort_;
    std::string frontCamCH_;
    std::string rearCamCH_;

    boost::asio::io_context ioContext_;
    boost::asio::steady_timer frontReconnectTimer_;
    boost::asio::steady_timer rearReconnectTimer_;
    std::optional<WorkGuard> workGuard_;
    std::thread ioThread_;

    std::unique_ptr<AppTcpClient> pFrontCamera_;
    std::unique_ptr<AppTcpClient> pRearCamera_;

    // The following mutable camera state is owned by the single LPR io_context
    // thread. No strand/mutex is required for it.
    bool frontCameraInitialized_;
    bool rearCameraInitialized_;
    bool frontCameraConnected_;
    bool rearCameraConnected_;
    std::uint64_t frontReconnectAttempt_;
    std::uint64_t rearReconnectAttempt_;

    // Lifecycle state can be observed from external threads.
    std::atomic<bool> running_;
    std::atomic<bool> stopping_;
    std::atomic<bool> initialized_;
    std::atomic<bool> acceptingWork_;
    mutable std::mutex lifecycleMutex_;

    static constexpr std::chrono::milliseconds kReconnectInterval{2000};
};