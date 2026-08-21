#pragma once

#include <sql.h>
#include <sqlext.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "ce_time.h"

class ReaderItem final
{
public:
    ReaderItem() = default;
    ~ReaderItem() = default;

    void appendData(std::string value);
    unsigned long getDataSize() const;
    std::string GetDataItem(unsigned long index) const;

private:
    std::vector<std::string> data_;
};


// Passive synchronous ODBC wrapper.
//
// This class deliberately owns no io_context, thread, strand, work guard,
// timer, or coroutine. Every public operation runs synchronously on the
// caller's thread. If a caller is an active Asio module, invoke blocking DB
// work from that module's existing blocking/thread pool rather than its sole
// I/O thread.
class odbc final
{
public:
    odbc(
        unsigned int ConnTO,
        unsigned int queryTO,
        float pingTO,
        std::string IP,
        std::string conn);

    ~odbc();

    odbc(const odbc&) = delete;
    odbc& operator=(const odbc&) = delete;
    odbc(odbc&&) = delete;
    odbc& operator=(odbc&&) = delete;

    int SQLSelect(
        std::string statement,
        std::vector<ReaderItem>* result,
        bool FullResult);

    int SQLExecutNoneQuery(std::string statement);

    int Connect();
    int Disconnect();
    int IsConnected();

    // Preserved for source compatibility with existing callers.
    std::atomic<long> NumberOfRowsAffected{0};

    int isValidSeason(
        const std::string& sSeasonNo,
        BYTE iInOut,
        unsigned int iZoneID,
        std::string& sSerialNo,
        short int& iRateType,
        float& sFee,
        float& sAdminFee,
        float& sAppFee,
        short int& iExpireDays,
        short int& iRedeemTime,
        float& sRedeemAmt,
        std::string& AllowedHolderType,
        std::string& dtValidTo,
        std::string& dtValidFrom);

private:
    bool initializeHandles();
    void releaseHandles();

    int connectUnlocked();
    int disconnectUnlocked();
    int isConnectedUnlocked();
    bool ensureConnectedUnlocked();

    bool setQueryTimeout(SQLHSTMT stmt);
    bool getColumnText(
        SQLHSTMT stmt,
        SQLUSMALLINT column,
        std::string& value);

    std::vector<std::string> GetError(
        const char* fn,
        SQLHANDLE handle,
        SQLSMALLINT type);

    void logSimpleError(const std::string& message) const;

    std::string m_IP;
    std::string m_connString;

    unsigned int ConnTimeOutVal{0};
    unsigned int queryTimeOut{0};
    float pingTimeOut{0.0F};

    SQLHENV env{SQL_NULL_HENV};
    SQLHDBC dbc{SQL_NULL_HDBC};

    bool initialized_{false};

    // ODBC connection handles and NumberOfRowsAffected are serialized per
    // odbc instance. Internal helpers ending in Unlocked must only be called
    // while mutex_ is held (or during construction/destruction).
    mutable std::mutex mutex_;
};
