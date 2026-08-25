#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "antenna.h"
#include "boost/asio.hpp"
#include "barcode_reader.h"
#include "crc.h"
#include "common.h"
#include "dio.h"
#include "gpio.h"
#include "ini_parser.h"
#include "lcd.h"
#include "led.h"
#include "log.h"
#include "mount.h"
#include "system_info.h"
#include "upt.h"
#include "event_manager.h"
#include "event_handler.h"
#include "lcsc.h"
#include "db.h"
#include "odbc.h"
#include "structuredata.h"
#include "operation.h"
#include "printer.h"
#include "udp.h"
#include "ksm_reader.h"
#include "eep_client.h"
#include "chu_client.h"
#include "shutdown_manager.h"
#include "ping.h"
#include "thread_pool_helper.h"

#if defined(__linux__)
#include <pthread.h>
#endif

#define SHUTDOWN_STEP(expr)                                      \
    do                                                           \
    {                                                            \
        Logger::getInstance()->FnLog(                             \
            std::string("[SHUTDOWN] Begin: ") + #expr);          \
                                                                 \
        expr;                                                    \
                                                                 \
        Logger::getInstance()->FnLog(                             \
            std::string("[SHUTDOWN] Done : ") + #expr);          \
    } while (false)

namespace
{

void setCurrentThreadName(const std::string& name)
{
#if defined(__linux__)
    const int result =
        ::pthread_setname_np(
            ::pthread_self(),
            name.c_str());

    if (result != 0)
    {
        std::cerr
            << "Failed to set thread name: "
            << name
            << " | error="
            << result
            << std::endl;
    }
#else
    (void)name;
#endif
}

} // namespace

void runDailyBackupWork()
{
    namespace fs = std::filesystem;

    // This function runs only on BACKUP_FILE. It may perform blocking
    // filesystem, Ping and CIFS mount/unmount work without blocking CORE_IO.
    if (ShutdownManager::getInstance()->FnIsShutdownRequested())
    {
        return;
    }

    try
    {
        // =====================================================
        // Current local date/time
        // =====================================================
        const auto now = std::chrono::system_clock::now();

        const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

        std::tm localNow{};
        localtime_r(&nowTime, &localNow);

        const int currentYear = localNow.tm_year + 1900;
        const int currentDayOfYear = localNow.tm_yday;

        // =====================================================
        // Daily SystemInfo logging
        // =====================================================
        static int lastSystemInfoYear = currentYear;
        static int lastSystemInfoDay = currentDayOfYear;

        if (lastSystemInfoYear != currentYear ||
            lastSystemInfoDay != currentDayOfYear)
        {
            SystemInfo::getInstance()->FnLogSysInfo();

            lastSystemInfoYear = currentYear;
            lastSystemInfoDay = currentDayOfYear;
        }

        // =====================================================
        // Determine whether backup should run
        // =====================================================
        static bool firstRun = true;
        const bool runOnStartup = firstRun;
        firstRun = false;

        // Run daily backup between:
        //
        // 00:01 - 00:29
        //
        // The lastBackup* guard prevents the backup from running
        // every minute throughout the complete window after a
        // successful backup.
        const bool isMidnightWindow = localNow.tm_hour == 0 && localNow.tm_min >= 1 && localNow.tm_min < 30;

        static int lastBackupYear = -1;
        static int lastBackupDay = -1;

        const bool alreadyBackedUpToday =
            lastBackupYear == currentYear &&
            lastBackupDay == currentDayOfYear;
        
        const bool shouldRunBackup =
            runOnStartup ||
            (isMidnightWindow &&
             !alreadyBackedUpToday);

        if (!shouldRunBackup)
        {
            return;
        }

        // =====================================================
        // Date strings used for file filtering
        // =====================================================
        std::ostringstream logDateSs;

        logDateSs
            << std::setw(2)
            << std::setfill('0')
            << (localNow.tm_year % 100)
            << std::setw(2)
            << std::setfill('0')
            << (localNow.tm_mon + 1)
            << std::setw(2)
            << std::setfill('0')
            << localNow.tm_mday;

        const std::string todayLogDate = logDateSs.str();

        // YYYY-MM-DD
        std::ostringstream lprDateSs;

        lprDateSs
            << std::setw(4)
            << std::setfill('0')
            << currentYear
            << "-"
            << std::setw(2)
            << std::setfill('0')
            << (localNow.tm_mon + 1)
            << "-"
            << std::setw(2)
            << std::setfill('0')
            << localNow.tm_mday;

        const std::string todayLprDate = lprDateSs.str();

        // YYYYMMDD
        std::ostringstream settlementDateSs;

        settlementDateSs
            << std::setw(4)
            << std::setfill('0')
            << currentYear
            << std::setw(2)
            << std::setfill('0')
            << (localNow.tm_mon + 1)
            << std::setw(2)
            << std::setfill('0')
            << localNow.tm_mday;

        const std::string todaySettlementDate = settlementDateSs.str();

        // =====================================================
        // Local source paths
        // =====================================================

        const fs::path logFilePath = Logger::getInstance()->LOG_FILE_PATH;
        const fs::path lprDbFilePath = "/home/root/evas_web/db_files";
        const fs::path lcscSettlementPath = LCSCReader::getInstance()->LOCAL_LCSC_SETTLEMENT_FOLDER_PATH;
        const fs::path eepSettlementPath = EEPClient::getInstance()->LOCAL_EEP_SETTLEMENT_FOLDER_PATH;

        // =====================================================
        // Helper: safely collect files from directory
        // =====================================================
        const auto collectFiles =
            [](
                const fs::path& directory,
                auto&& predicate,
                const std::string& description)
            {
                std::vector<fs::path> files;

                std::error_code dirEc;

                if (!fs::exists(directory, dirEc))
                {
                    Logger::getInstance()->FnLog(
                        description +
                            " directory does not exist: " +
                            directory.string());

                    return files;
                }

                if (dirEc || !fs::is_directory( directory, dirEc))
                {
                    Logger::getInstance()->FnLog(
                        "Unable to access " +
                            description +
                            " directory: " +
                            directory.string() +
                            (dirEc
                                ? " | Error=" +
                                    dirEc.message()
                                : ""));

                    return files;
                }

                fs::directory_iterator iterator(directory, dirEc);

                const fs::directory_iterator end;

                if (dirEc)
                {
                    Logger::getInstance()->FnLog(
                        "Unable to enumerate " +
                            description +
                            " directory: " +
                            directory.string() +
                            " | Error=" +
                            dirEc.message());

                    return files;
                }

                while (iterator != end)
                {
                    if (predicate(iterator->path()))
                    {
                        files.emplace_back(iterator->path());
                    }

                    iterator.increment(dirEc);

                    if (dirEc)
                    {
                        Logger::getInstance()->FnLog(
                            "Directory iteration error: " +
                                directory.string() +
                                " | Error=" +
                                dirEc.message());

                        break;
                    }
                }

                return files;
            };
        
        // =====================================================
        // Helper: copy file then remove source
        // =====================================================
        const auto copyAndRemoveFile =
            [](
                const fs::path& source,
                const fs::path& destination)
            {
                std::error_code fileEc;

                const fs::path parent = destination.parent_path();

                if (!parent.empty())
                {
                    fs::create_directories(parent, fileEc);

                    if (fileEc)
                    {
                        Logger::getInstance()->FnLog(
                            "Backup directory creation failed"
                            " | Directory=" +
                                parent.string() +
                                " | Error=" +
                                fileEc.message());

                        return false;
                    }
                }

                fileEc.clear();

                fs::copy_file(source, destination, fs::copy_options::overwrite_existing, fileEc);

                if (fileEc)
                {
                    Logger::getInstance()->FnLog(
                        "Backup copy failed"
                        " | Source=" +
                            source.string() +
                            " | Destination=" +
                            destination.string() +
                            " | Error=" +
                            fileEc.message());

                    return false;
                }

                fileEc.clear();

                fs::remove(source, fileEc);

                if (fileEc)
                {
                    Logger::getInstance()->FnLog(
                        "Backup copied but source removal failed"
                        " | Source=" +
                            source.string() +
                            " | Error=" +
                            fileEc.message(),
                        "",
                        "MAIN");

                    return false;
                }

                Logger::getInstance()->FnLog(
                    "Backup completed"
                    " | Source=" +
                        source.string() +
                        " | Destination=" +
                        destination.string(),
                    "",
                    "MAIN");

                return true;
            };

        // =====================================================
        // Helper: normalize Windows-style UNC path
        // =====================================================
        const auto normalizeSharePath =
            [](
                std::string path)
            {
                std::replace(path.begin(), path.end(), '\\', '/');
                return path;
            };

        // =====================================================
        // Helper: acquire mount using MountManager
        // =====================================================
        const std::string username = IniParser::getInstance()->FnGetCentralUsername();
        const std::string password = IniParser::getInstance()->FnGetCentralPassword();

        const auto withMountedShare =
            [&](
                const std::string& sharedFolderPath,
                const std::string& mountPoint,
                auto&& work)
            {
                MountManager mount(
                    sharedFolderPath,
                    mountPoint,
                    username,
                    password,
                    "",
                    "OPR");

                if (!mount.isMounted())
                {
                    Logger::getInstance()->FnLog(
                        "Backup mount failed"
                        " | Share=" +
                            sharedFolderPath +
                            " | MountPoint=" +
                            mountPoint);

                    return false;
                }

                // MountManager destructor automatically
                // releases/unmounts when this function returns.
                return work(fs::path(mountPoint));
            };

        // =====================================================
        // Collect application log files
        // =====================================================
        const auto logFiles =
            collectFiles(
                logFilePath,
                [&](const fs::path& path)
                {
                    const std::string filename = path.filename().string();

                    return
                        path.extension() == ".log" &&
                        filename.find(todayLogDate) == std::string::npos;
                },
                "application log");

        // =====================================================
        // Collect old LPR DB files
        // =====================================================
        const auto lprFiles =
            collectFiles(
                lprDbFilePath,
                [&](const fs::path& path)
                {
                    const std::string filename = path.filename().string();

                    return
                        path.extension() == ".csv" &&
                        filename.find(todayLprDate) == std::string::npos;
                },
                "LPR database");

        // =====================================================
        // Collect old LCSC settlement files
        // =====================================================
        const auto lcscFiles =
            collectFiles(
                lcscSettlementPath,
                [&](const fs::path& path)
                {
                    const std::string filename = path.filename().string();

                    return
                        path.extension() == ".lcs" &&
                        filename.find(todaySettlementDate) == std::string::npos;
                },
                "LCSC settlement");

        // =====================================================
        // Collect old DSRC FE/BE files
        // =====================================================
        const auto dsrcFiles =
            collectFiles(
                eepSettlementPath,
                [&](const fs::path& path)
                {
                    const std::string filename = path.filename().string();

                    if (filename.find(todaySettlementDate) != std::string::npos)
                    {
                        return false;
                    }

                    return
                        filename.find("FE_") != std::string::npos ||
                        filename.find("BE_") != std::string::npos;
                },
                "DSRC settlement");

        const bool hasBackupWork =
            !logFiles.empty() ||
            !lprFiles.empty() ||
            !lcscFiles.empty() ||
            !dsrcFiles.empty();

        if (!hasBackupWork)
        {
            Logger::getInstance()->FnLog("Daily backup check completed. No files require backup.");

            lastBackupYear = currentYear;

            lastBackupDay = currentDayOfYear;

            return;
        }

        Logger::getInstance()->FnLog("==================== DAILY BACKUP STARTED ====================");

        Logger::getInstance()->FnLog(
            "Backup candidates"
            " | Logs=" +
                std::to_string(
                    logFiles.size()) +
                " | LPR=" +
                std::to_string(
                    lprFiles.size()) +
                " | LCSC=" +
                std::to_string(
                    lcscFiles.size()) +
                " | DSRC=" +
                std::to_string(
                    dsrcFiles.size()));
        
        // =====================================================
        // Check central server connectivity
        // =====================================================
        std::string pingDetails;

        const bool serverOnline =
            PingWithTimeOut(
                IniParser::getInstance()->FnGetCentralDBServer(),
                1.0F,
                pingDetails);

        if (!serverOnline)
        {
            Logger::getInstance()->FnLog(
                "Daily backup postponed because central server is unreachable"
                " | Details=" +
                    pingDetails);

            // Do not set lastBackupDay here.
            //
            // During the midnight window this allows the timer
            // to retry again on the next cycle.

            return;
        }

        const auto sharedData =
            operation::getInstance()->FnGetSharedData();

        if (!sharedData)
        {
            Logger::getInstance()->FnLog(
                "Daily backup postponed because Operation shared data is unavailable");
            return;
        }

        bool backupSucceeded = true;

        std::string logBackupShare =
            normalizeSharePath(
                sharedData->tParas.gsLogBackFolder);

        std::string lcscBackupShare =
            normalizeSharePath(
                sharedData->tParas.gsRemoteLCSC);

        // =====================================================
        // Backup application logs
        // =====================================================
        if (!logFiles.empty())
        {
            const bool result =
                withMountedShare(
                    logBackupShare,
                    "/mnt/logbackup",
                    [&](const fs::path& mountRoot)
                    {
                        bool success = true;

                        for (const auto& source : logFiles)
                        {
                            if (!copyAndRemoveFile(
                                    source,
                                    mountRoot /
                                        source.filename()))
                            {
                                success = false;
                            }
                        }

                        return success;
                    });

            if (!result)
            {
                backupSucceeded = false;
            }
        }

        // =====================================================
        // Backup LPR database CSV files
        // =====================================================
        if (!lprFiles.empty())
        {
            std::string lprBackupShare = logBackupShare;

            const std::size_t lastSlash = lprBackupShare.find_last_of('/');

            if (lastSlash == std::string::npos)
            {
                Logger::getInstance()->FnLog(
                    "Unable to determine LPR backup share"
                    " from log backup path: " +
                        lprBackupShare);

                backupSucceeded = false;
            }
            else
            {
                lprBackupShare.erase(lastSlash);

                const bool result =
                    withMountedShare(
                        lprBackupShare,
                        "/mnt/dbfilesbackup",
                        [&](const fs::path& mountRoot)
                        {
                            bool success = true;

                            for (const auto& source : lprFiles)
                            {
                                const std::string filename =
                                    source.filename().string();

                                const std::size_t underscore =
                                    filename.find_last_of('_');

                                if (underscore == std::string::npos)
                                {
                                    Logger::getInstance()->FnLog(
                                        "Unable to parse LPR backup filename: " +
                                            filename);

                                    success = false;
                                    continue;
                                }

                                int year = 0;
                                int month = 0;
                                int day = 0;

                                if (std::sscanf(
                                        filename.c_str() +
                                            underscore + 1,
                                        "%4d-%2d-%2d.csv",
                                        &year,
                                        &month,
                                        &day) != 3)
                                {
                                    Logger::getInstance()->FnLog(
                                        "Unable to parse LPR backup date: " +
                                            filename);

                                    success = false;
                                    continue;
                                }

                                std::ostringstream yearSs;
                                std::ostringstream monthSs;

                                yearSs
                                    << std::setw(4)
                                    << std::setfill('0')
                                    << year;

                                monthSs
                                    << std::setw(2)
                                    << std::setfill('0')
                                    << month;

                                const fs::path destination =
                                    mountRoot /
                                    "Database" /
                                    "LPN" /
                                    yearSs.str() /
                                    monthSs.str() /
                                    source.filename();

                                if (!copyAndRemoveFile(
                                        source,
                                        destination))
                                {
                                    success = false;
                                }
                            }

                            return success;
                        });

                if (!result)
                {
                    backupSucceeded = false;
                }
            }
        }

        // =====================================================
        // Backup LCSC settlement files
        // =====================================================
        if (!lcscFiles.empty())
        {
            const bool result =
                withMountedShare(
                    lcscBackupShare,
                    "/mnt/lcscsettlementfiles",
                    [&](const fs::path& mountRoot)
                    {
                        bool success = true;

                        for (const auto& source : lcscFiles)
                        {
                            if (!copyAndRemoveFile(
                                    source,
                                    mountRoot /
                                        source.filename()))
                            {
                                success = false;
                            }
                        }

                        return success;
                    });

            if (!result)
            {
                backupSucceeded = false;
            }
        }

        // =====================================================
        // Backup DSRC FE / BE settlement files
        // =====================================================
        if (!dsrcFiles.empty())
        {
            const std::string dsrcBackupShare =
                "//" +
                IniParser::getInstance()->FnGetCentralDBServer() +
                "/Carpark/EEPSettle";

            const bool result =
                withMountedShare(
                    dsrcBackupShare,
                    "/mnt/dsrcsettlementfiles",
                    [&](const fs::path& mountRoot)
                    {
                        bool success = true;

                        for (const auto& source : dsrcFiles)
                        {
                            const std::string filename = source.filename().string();

                            fs::path destination;

                            if (filename.find("FE_") != std::string::npos)
                            {
                                destination =
                                    mountRoot /
                                    "DSRCFE" /
                                    "Raw" /
                                    source.filename();
                            }
                            else if (filename.find("BE_") != std::string::npos)
                            {
                                destination =
                                    mountRoot /
                                    "DSRCBE" /
                                    "Raw" /
                                    source.filename();
                            }
                            else
                            {
                                continue;
                            }

                            if (!copyAndRemoveFile(
                                    source,
                                    destination))
                            {
                                success = false;
                            }
                        }

                        return success;
                    });

            if (!result)
            {
                backupSucceeded = false;
            }
        }

        // =====================================================
        // Final backup result
        // =====================================================
        if (backupSucceeded)
        {
            lastBackupYear = currentYear;

            lastBackupDay = currentDayOfYear;

            Logger::getInstance()->FnLog("==================== DAILY BACKUP COMPLETED ==================");
        }
        else
        {
            Logger::getInstance()->FnLog("==================== DAILY BACKUP COMPLETED WITH ERRORS ======");

            // Do not update lastBackupDay.
            //
            // During the midnight window the next 60-second
            // check can retry failed files.
        }
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        Logger::getInstance()->FnLogExceptionError(std::string("runDailyBackupWork filesystem exception: ") + e.what());
    }
    catch (const std::exception& e)
    {
        Logger::getInstance()->FnLogExceptionError(std::string("runDailyBackupWork exception: ") + e.what());
    }
    catch (...)
    {
        Logger::getInstance()->FnLogExceptionError("runDailyBackupWork unknown exception.");
    }


}

void dailyBackupTimerHandler(
    const boost::system::error_code& ec,
    boost::asio::steady_timer* timer,
    boost::asio::thread_pool* filePool)
{
    // =========================================================
    // Timer cancellation / error
    // =========================================================
    if (ec == boost::asio::error::operation_aborted)
    {
        return;
    }

    if (ec)
    {
        Logger::getInstance()->FnLog(
            "Daily backup timer error: " + ec.message(),
            "",
            "MAIN");
        return;
    }

    if (ShutdownManager::getInstance()->FnIsShutdownRequested())
    {
        return;
    }

    if (timer == nullptr || filePool == nullptr)
    {
        Logger::getInstance()->FnLog(
            "Daily backup timer stopped | Reason=Invalid timer or file pool",
            "",
            "MAIN");
        return;
    }

    // Do not arm the next timer yet. The next 60-second wait is armed only
    // after BACKUP_FILE has completed this cycle. Therefore backup cycles
    // cannot overlap and the existing finish-then-wait timing is preserved.
    boost::asio::post(
        *filePool,
        [timer, filePool]()
        {
            if (!ShutdownManager::getInstance()->FnIsShutdownRequested())
            {
                runDailyBackupWork();
            }

            // steady_timer belongs to dailyBackupStrand / main io_context.
            // Return to that executor before changing/arming the timer.
            boost::asio::post(
                timer->get_executor(),
                [timer, filePool]()
                {
                    if (ShutdownManager::getInstance()->FnIsShutdownRequested())
                    {
                        return;
                    }

                    timer->expires_after(std::chrono::seconds(60));
                    timer->async_wait(
                        [timer, filePool](
                            const boost::system::error_code& waitEc)
                        {
                            dailyBackupTimerHandler(
                                waitEc,
                                timer,
                                filePool);
                        });
                });
        });
}

void signalHandler(const boost::system::error_code& ec, int signal)
{
    if (ec == boost::asio::error::operation_aborted)
    {
        return;
    }

    if (ec)
    {
        Logger::getInstance()->FnLog("Signal handler error: " + ec.message());
        return;
    }

    Logger::getInstance()->FnLog("Terminal signal received: " + std::to_string(signal) + ". Shutdown requested.");

    // Keep the Asio signal callback lightweight.
    // The main/lifecycle thread performs the actual shutdown sequence.
    ShutdownManager::getInstance()->FnRequestShutdown();
}

int main (int argc, char* argv[])
{
    (void)argc;

    if (argc > 0 && argv != nullptr && argv[0] != nullptr)
    {
        SystemInfo::getInstance()->FnSetExecutablePath(argv[0]);
    }

    setCurrentThreadName("linuxpbs");

    IniParser::getInstance()->FnReadIniFile();
    Logger::getInstance()->FnCreateLogFile();

    Logger::getInstance()->FnLog("==================== STATION PROGRAM STARTING ====================");

    boost::asio::io_context ioContext;
    auto workGuard = boost::asio::make_work_guard(ioContext);

    // The backup timer remains on the shared Main io_context and therefore
    // uses its own strand while multiple CORE_IO threads run this context.
    auto dailyBackupStrand = boost::asio::make_strand(ioContext);

    boost::asio::signal_set signals(ioContext, SIGINT, SIGTERM);

    signals.async_wait(
        [](const boost::system::error_code& ec, int signal)
        {
            signalHandler(ec, signal);
        });

    SystemInfo::getInstance()->FnLogSysInfo();

    EventManager::getInstance()->FnRegisterEvent(
        [](uint64_t eventId,
           const std::string& eventName,
           BaseEvent* event)
        {
            EventHandler::getInstance()->FnHandleEvents(
                eventId,
                eventName,
                event);
        });
    EventManager::getInstance()->FnStartEventThread();

    operation::getInstance()->FnOperationInit();

    // Start daily log timer
    // Dedicated worker for blocking backup filesystem / Ping / CIFS work.
    // The timer itself remains on dailyBackupStrand / main io_context.
    auto backupFilePool = ThreadPoolHelper::create(1, "BACKUP_FILE");

    // Start daily backup timer.
    boost::asio::steady_timer dailyBackupTimer(
        dailyBackupStrand,
        std::chrono::seconds(1));

    dailyBackupTimer.async_wait(
        [&dailyBackupTimer, filePool = backupFilePool.get()](
            const boost::system::error_code& ec)
        {
            dailyBackupTimerHandler(
                ec,
                &dailyBackupTimer,
                filePool);
        });

    // Create a pool of threads to run the io_context
    constexpr int kCoreIoThreadCount = 1;

    std::vector<std::thread> threadPool;
    threadPool.reserve(kCoreIoThreadCount);

    for (int i = 0; i < kCoreIoThreadCount; ++i)
    {
        threadPool.emplace_back(
            [&ioContext, i]()
            {
                const std::string threadName =
                    "CORE_IO_" +
                    std::to_string(i + 1);

                setCurrentThreadName(threadName);

                ioContext.run();
            });
    }

    // Main owns application lifetime. Do not join the io_context workers yet;
    // they must remain alive while modules are asked to close and drain.
    ShutdownManager::getInstance()->FnWaitForShutdown();

    Logger::getInstance()->FnLog("==================== STATION PROGRAM STOPPING ====================");
    
    operation::getInstance()->FnSendMsg2Server("09", "11Stopping...");
    std::string lcdLine1 = ">>> STN STOPPED <<< ";
    std::string lcdLine2 = Common::getInstance()->FnGetDateTimeFormat_ddmmyyy_hhmmss();

    LCD::getInstance()->FnLCDClearDisplayRow(1);
    LCD::getInstance()->FnLCDClearDisplayRow(2);
    LCD::getInstance()->FnLCDDisplayRow(1, lcdLine1.data());
    LCD::getInstance()->FnLCDDisplayRow(2, lcdLine2.data());

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Prevent recurring main timers and signal waits from creating new work.
    boost::system::error_code ignoredEc;
    signals.cancel(ignoredEc);
    dailyBackupTimer.cancel();

    // Prevent queued backup jobs that have not started from running during
    // shutdown. An already-running backup job is allowed to finish.
    if (backupFilePool)
    {
        backupFilePool->stop();
    }

    // Stop event producers / active modules while the shared io_context is
    // still running so their asynchronous close/cancel handlers can drain.
    SHUTDOWN_STEP(Upt::getInstance()->FnUptClose());
    SHUTDOWN_STEP(KSM_Reader::getInstance()->FnKSMReaderClose());
    SHUTDOWN_STEP(LCSCReader::getInstance()->FnLCSCReaderClose());
    SHUTDOWN_STEP(Printer::getInstance()->FnPrinterClose());
    SHUTDOWN_STEP(Lpr::getInstance()->FnLprClose());
    SHUTDOWN_STEP(CHUClient::getInstance()->FnCHUClose());
    SHUTDOWN_STEP(EEPClient::getInstance()->FnEEPClientClose());
    SHUTDOWN_STEP(Antenna::getInstance()->FnAntennaShutdown());
    SHUTDOWN_STEP(DIO::getInstance()->FnDIOShutdown());
    SHUTDOWN_STEP(BARCODE_READER::getInstance()->FnBarcodeStopRead());

    // The backup worker may have been inside a blocking Ping/MountManager call
    // when shutdown was requested. Wait for that already-running job before
    // Operation/config ownership is closed. Queued jobs were discarded by
    // backupFilePool->stop() above.
    if (backupFilePool)
    {
        Logger::getInstance()->FnLog(
            "[SHUTDOWN] Waiting for BACKUP_FILE");

        backupFilePool->join();
        backupFilePool.reset();

        Logger::getInstance()->FnLog(
            "[SHUTDOWN] BACKUP_FILE joined");
    }

    SHUTDOWN_STEP(EventManager::getInstance()->FnStopEventThread());
    SHUTDOWN_STEP(operation::getInstance()->FnClose());
    SHUTDOWN_STEP(db::getInstance()->FnClose());
    
    // Normal shutdown: release the work guard and let pending cancellation/
    // close completions drain naturally. Avoid ioContext.stop().
    Logger::getInstance()->FnLog("[SHUTDOWN] Resetting MAIN work guard");

    workGuard.reset();

    Logger::getInstance()->FnLog("[SHUTDOWN] Waiting for CORE_IO threads");

    for (std::size_t i = 0; i < threadPool.size(); ++i)
    {
        Logger::getInstance()->FnLog("[SHUTDOWN] Joining CORE_IO_" + std::to_string(i + 1));

        if (threadPool[i].joinable())
        {
            threadPool[i].join();
        }

        Logger::getInstance()->FnLog("[SHUTDOWN] Joined CORE_IO_" + std::to_string(i + 1));
    }

    Logger::getInstance()->FnLog( "==================== STATION PROGRAM STOPPED =====================");

    // Logger must be the final subsystem to shut down.
    Logger::getInstance()->FnShutdown();

    return 0;
}
