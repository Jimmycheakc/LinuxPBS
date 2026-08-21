#include "odbc.h"

#include <array>
#include <chrono>
#include <cstring>
#include <ctime>
#include <sstream>
#include <utility>

#include "log.h"
#include "ping.h"

namespace
{

class StatementHandle final
{
public:
    StatementHandle() = default;

    ~StatementHandle()
    {
        reset();
    }

    StatementHandle(const StatementHandle&) = delete;
    StatementHandle& operator=(const StatementHandle&) = delete;
    StatementHandle(StatementHandle&&) = delete;
    StatementHandle& operator=(StatementHandle&&) = delete;

    SQLHSTMT get() const
    {
        return handle_;
    }

    SQLHSTMT* put()
    {
        reset();
        return &handle_;
    }

    void reset()
    {
        if (handle_ != SQL_NULL_HSTMT)
        {
            (void)::SQLFreeStmt(handle_, SQL_CLOSE);
            (void)::SQLFreeHandle(SQL_HANDLE_STMT, handle_);
            handle_ = SQL_NULL_HSTMT;
        }
    }

private:
    SQLHSTMT handle_{SQL_NULL_HSTMT};
};

SQLCHAR* asSqlChar(std::string& value)
{
    return reinterpret_cast<SQLCHAR*>(value.data());
}

SQLCHAR* asSqlChar(const std::string& value)
{
    return reinterpret_cast<SQLCHAR*>(
        const_cast<char*>(value.c_str()));
}

} // namespace


void ReaderItem::appendData(std::string value)
{
    data_.push_back(std::move(value));
}

unsigned long ReaderItem::getDataSize() const
{
    return static_cast<unsigned long>(data_.size());
}

std::string ReaderItem::GetDataItem(unsigned long index) const
{
    if (index >= data_.size())
    {
        return {};
    }

    return data_[index];
}


odbc::odbc(
    unsigned int ConnTO,
    unsigned int queryTO,
    float pingTO,
    std::string IP,
    std::string conn)
    : m_IP(std::move(IP)),
      m_connString(std::move(conn)),
      ConnTimeOutVal(ConnTO),
      queryTimeOut(queryTO),
      pingTimeOut(pingTO)
{
    try
    {
        initialized_ = initializeHandles();

        if (!initialized_)
        {
            releaseHandles();
        }
    }
    catch (const std::exception& e)
    {
        logSimpleError(std::string(__func__) + ", Exception: " + e.what());

        releaseHandles();
        initialized_ = false;
    }
    catch (...)
    {
        logSimpleError(std::string(__func__) + ", Exception: Unknown Exception");

        releaseHandles();
        initialized_ = false;
    }
}

odbc::~odbc()
{
    releaseHandles();
}

bool odbc::initializeHandles()
{
    SQLRETURN ret = ::SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
    if (!SQL_SUCCEEDED(ret))
    {
        logSimpleError("SQLAllocHandle(SQL_HANDLE_ENV) failed");
        env = SQL_NULL_HENV;
        return false;
    }

    ret = ::SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);
    if (!SQL_SUCCEEDED(ret))
    {
        (void)GetError("SQLSetEnvAttr(SQL_ATTR_ODBC_VERSION)", env, SQL_HANDLE_ENV);
        return false;
    }

    ret = ::SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);
    if (!SQL_SUCCEEDED(ret))
    {
        // For SQLAllocHandle failure, diagnostics belong to the input handle.
        (void)GetError("SQLAllocHandle(SQL_HANDLE_DBC)", env, SQL_HANDLE_ENV);
        dbc = SQL_NULL_HDBC;
        return false;
    }

    ret = ::SQLSetConnectAttr(dbc, SQL_ATTR_CONNECTION_TIMEOUT, reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(ConnTimeOutVal)), 0);
    if (!SQL_SUCCEEDED(ret))
    {
        (void)GetError("SQLSetConnectAttr(SQL_ATTR_CONNECTION_TIMEOUT)", dbc, SQL_HANDLE_DBC);
        return false;
    }

    return true;
}

void odbc::releaseHandles()
{
    if (dbc != SQL_NULL_HDBC)
    {
        (void)::SQLDisconnect(dbc);
        (void)::SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        dbc = SQL_NULL_HDBC;
    }

    if (env != SQL_NULL_HENV)
    {
        (void)::SQLFreeHandle(SQL_HANDLE_ENV, env);
        env = SQL_NULL_HENV;
    }

    initialized_ = false;
}

int odbc::Connect()
{
    std::lock_guard<std::mutex> lock(mutex_);

    try
    {
        return connectUnlocked();
    }
    catch (const std::exception& e)
    {
        logSimpleError(std::string(__func__) + ", Exception: " + e.what());
        return -1;
    }
    catch (...)
    {
        logSimpleError(std::string(__func__) + ", Exception: Unknown Exception");
        return -1;
    }
}

int odbc::connectUnlocked()
{
    if (!initialized_ || dbc == SQL_NULL_HDBC)
    {
        logSimpleError("ODBC connection handle is not initialized");
        return -1;
    }

    if (isConnectedUnlocked() == 1)
    {
        return 0;
    }

    // Keep the existing synchronous ping gate. The calling thread remains
    // blocked until PingWithTimeOut() completes.
    std::string pingDetails;
    if (!PingWithTimeOut(m_IP, pingTimeOut, pingDetails))
    {
        return -1;
    }

    std::array<SQLCHAR, 1024> outString{};
    SQLSMALLINT outLength = 0;

    SQLRETURN ret =
        ::SQLDriverConnect(
            dbc,
            nullptr,
            asSqlChar(m_connString),
            SQL_NTS,
            outString.data(),
            static_cast<SQLSMALLINT>(outString.size()),
            &outLength,
            SQL_DRIVER_NOPROMPT);

    if (!SQL_SUCCEEDED(ret))
    {
        (void)GetError("SQLDriverConnect", dbc, SQL_HANDLE_DBC);
        return -1;
    }

    if (ret == SQL_SUCCESS_WITH_INFO)
    {
        (void)GetError("SQLDriverConnect", dbc, SQL_HANDLE_DBC);
    }

    return 0;
}

int odbc::Disconnect()
{
    std::lock_guard<std::mutex> lock(mutex_);

    try
    {
        return disconnectUnlocked();
    }
    catch (const std::exception& e)
    {
        logSimpleError(std::string(__func__) + ", Exception: " + e.what());
        return -1;
    }
    catch (...)
    {
        logSimpleError(std::string(__func__) + ", Exception: Unknown Exception");
        return -1;
    }
}

int odbc::disconnectUnlocked()
{
    if (!initialized_ || dbc == SQL_NULL_HDBC)
    {
        return 0;
    }

    const SQLRETURN ret = ::SQLDisconnect(dbc);

    if (ret == SQL_SUCCESS ||
        ret == SQL_SUCCESS_WITH_INFO ||
        ret == SQL_ERROR)
    {
        // SQL_ERROR can mean the connection was already unusable. Keep the
        // legacy Disconnect() behaviour non-fatal, but record diagnostics.
        if (ret == SQL_SUCCESS_WITH_INFO || ret == SQL_ERROR)
        {
            (void)GetError("SQLDisconnect", dbc, SQL_HANDLE_DBC);
        }

        return ret == SQL_ERROR ? -1 : 0;
    }

    return 0;
}

int odbc::IsConnected()
{
    std::lock_guard<std::mutex> lock(mutex_);

    try
    {
        return isConnectedUnlocked();
    }
    catch (const std::exception& e)
    {
        logSimpleError(std::string(__func__) + ", Exception: " + e.what());
        return 0;
    }
    catch (...)
    {
        logSimpleError(std::string(__func__) + ", Exception: Unknown Exception");
        return 0;
    }
}

int odbc::isConnectedUnlocked()
{
    if (!initialized_ || dbc == SQL_NULL_HDBC)
    {
        return 0;
    }

    SQLUINTEGER connectionDead = SQL_CD_TRUE;

    const SQLRETURN ret =
        ::SQLGetConnectAttr(
            dbc,
            SQL_ATTR_CONNECTION_DEAD,
            &connectionDead,
            static_cast<SQLINTEGER>(
                sizeof(connectionDead)),
            nullptr);

    if (!SQL_SUCCEEDED(ret))
    {
        // A not-yet-connected handle commonly reaches this path. Returning 0
        // is enough for ensureConnectedUnlocked() to reconnect.
        return 0;
    }

    return connectionDead == SQL_CD_FALSE ? 1 : 0;
}

bool odbc::ensureConnectedUnlocked()
{
    if (isConnectedUnlocked() == 1)
    {
        return true;
    }

    (void)disconnectUnlocked();
    return connectUnlocked() == 0;
}

bool odbc::setQueryTimeout(SQLHSTMT stmt)
{
    const SQLRETURN ret =
        ::SQLSetStmtAttr(
            stmt,
            SQL_QUERY_TIMEOUT,
            reinterpret_cast<SQLPOINTER>(
                static_cast<intptr_t>(queryTimeOut)),
            SQL_IS_UINTEGER);

    if (!SQL_SUCCEEDED(ret))
    {
        (void)GetError("SQLSetStmtAttr(SQL_QUERY_TIMEOUT)", stmt, SQL_HANDLE_STMT);
        return false;
    }

    return true;
}

bool odbc::getColumnText(
    SQLHSTMT stmt,
    SQLUSMALLINT column,
    std::string& value)
{
    value.clear();

    std::array<char, 512> buffer{};
    SQLLEN indicator = 0;

    for (;;)
    {
        buffer.fill('\0');

        const SQLRETURN ret =
            ::SQLGetData(
                stmt,
                column,
                SQL_C_CHAR,
                buffer.data(),
                static_cast<SQLLEN>(buffer.size()),
                &indicator);

        if (indicator == SQL_NULL_DATA)
        {
            value = "NULL";
            return true;
        }

        if (ret == SQL_NO_DATA)
        {
            return true;
        }

        if (!SQL_SUCCEEDED(ret))
        {
            (void)GetError("SQLGetData", stmt, SQL_HANDLE_STMT);
            return false;
        }

        value.append(buffer.data());

        if (ret == SQL_SUCCESS)
        {
            return true;
        }

        // SQL_SUCCESS_WITH_INFO commonly means the value was truncated to the
        // current buffer. Continue SQLGetData() to collect the next chunk.
    }
}

int odbc::SQLSelect(
    std::string statement,
    std::vector<ReaderItem>* result,
    bool FullResult)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (result == nullptr)
    {
        logSimpleError("SQLSelect called with null result pointer");
        return -1;
    }

    result->clear();
    NumberOfRowsAffected = 0;

    try
    {
        if (!ensureConnectedUnlocked())
        {
            return -1;
        }

        StatementHandle statementHandle;

        SQLRETURN ret =
            ::SQLAllocHandle(
                SQL_HANDLE_STMT,
                dbc,
                statementHandle.put());

        if (!SQL_SUCCEEDED(ret))
        {
            (void)GetError("SQLAllocHandle(SQL_HANDLE_STMT)", dbc, SQL_HANDLE_DBC);
            return -1;
        }

        SQLHSTMT stmt = statementHandle.get();

        if (!setQueryTimeout(stmt))
        {
            return -1;
        }

        ret =
            ::SQLExecDirect(
                stmt,
                asSqlChar(statement),
                SQL_NTS);

        if (!SQL_SUCCEEDED(ret))
        {
            (void)GetError("SQLExecDirect", stmt, SQL_HANDLE_STMT);
            return -1;
        }

        if (ret == SQL_SUCCESS_WITH_INFO)
        {
            (void)GetError("SQLExecDirect", stmt, SQL_HANDLE_STMT);
        }

        SQLSMALLINT columns = 0;
        ret = ::SQLNumResultCols(stmt, &columns);

        if (!SQL_SUCCEEDED(ret))
        {
            (void)GetError("SQLNumResultCols", stmt, SQL_HANDLE_STMT);
            return -1;
        }

        SQLLEN rows = 0;
        ret = ::SQLRowCount(stmt, &rows);

        if (SQL_SUCCEEDED(ret))
        {
            NumberOfRowsAffected = static_cast<long>(rows);
        }

        std::vector<ReaderItem> rowsResult;

        for (;;)
        {
            ret = ::SQLFetch(stmt);

            if (ret == SQL_NO_DATA)
            {
                break;
            }

            if (!SQL_SUCCEEDED(ret))
            {
                (void)GetError("SQLFetch", stmt, SQL_HANDLE_STMT);
                return -1;
            }

            ReaderItem item;

            for (SQLUSMALLINT column = 1; column <= static_cast<SQLUSMALLINT>(columns); ++column)
            {
                std::string columnValue;

                if (!getColumnText(stmt, column, columnValue))
                {
                    return -1;
                }

                item.appendData(std::move(columnValue));
            }

            if (columns >= 1)
            {
                rowsResult.push_back(std::move(item));
            }

            if (!FullResult)
            {
                break;
            }
        }

        *result = std::move(rowsResult);
        return 0;
    }
    catch (const std::exception& e)
    {
        logSimpleError(std::string(__func__) + ", Exception: " + e.what());
        return -1;
    }
    catch (...)
    {
        logSimpleError(std::string(__func__) + ", Exception: Unknown Exception");
        return -1;
    }
}

int odbc::SQLExecutNoneQuery(std::string statement)
{
    std::lock_guard<std::mutex> lock(mutex_);

    NumberOfRowsAffected = 0;

    try
    {
        if (!ensureConnectedUnlocked())
        {
            return -1;
        }

        StatementHandle statementHandle;

        SQLRETURN ret =
            ::SQLAllocHandle(
                SQL_HANDLE_STMT,
                dbc,
                statementHandle.put());

        if (!SQL_SUCCEEDED(ret))
        {
            (void)GetError("SQLAllocHandle(SQL_HANDLE_STMT)", dbc, SQL_HANDLE_DBC);
            return -1;
        }

        SQLHSTMT stmt = statementHandle.get();

        if (!setQueryTimeout(stmt))
        {
            return -1;
        }

        ret =
            ::SQLExecDirect(
                stmt,
                asSqlChar(statement),
                SQL_NTS);

        if (!SQL_SUCCEEDED(ret))
        {
            (void)GetError("SQLExecDirect", stmt, SQL_HANDLE_STMT);
            return -1;
        }

        if (ret == SQL_SUCCESS_WITH_INFO)
        {
            (void)GetError("SQLExecDirect", stmt, SQL_HANDLE_STMT);
        }

        SQLLEN rows = 0;
        ret = ::SQLRowCount(stmt, &rows);

        if (!SQL_SUCCEEDED(ret))
        {
            (void)GetError("SQLRowCount", stmt, SQL_HANDLE_STMT);
            return -1;
        }

        NumberOfRowsAffected = static_cast<long>(rows);

        return 0;
    }
    catch (const std::exception& e)
    {
        logSimpleError(std::string(__func__) + ", Exception: " + e.what());
        return -1;
    }
    catch (...)
    {
        logSimpleError(std::string(__func__) + ", Exception: Unknown Exception");
        return -1;
    }
}

std::vector<std::string> odbc::GetError(
    const char* fn,
    SQLHANDLE handle,
    SQLSMALLINT type)
{
    std::vector<std::string> messages;

    if (fn != nullptr)
    {
        messages.emplace_back(fn);
    }

    if (handle == SQL_NULL_HANDLE)
    {
        return messages;
    }

    for (SQLSMALLINT record = 1;; ++record)
    {
        SQLCHAR state[7]{};
        SQLINTEGER nativeError = 0;
        SQLCHAR text[512]{};
        SQLSMALLINT textLength = 0;

        const SQLRETURN ret =
            ::SQLGetDiagRec(
                type,
                handle,
                record,
                state,
                &nativeError,
                text,
                static_cast<SQLSMALLINT>(sizeof(text)),
                &textLength);

        if (ret == SQL_NO_DATA)
        {
            break;
        }

        if (!SQL_SUCCEEDED(ret))
        {
            break;
        }

        const std::string stateText(reinterpret_cast<const char*>(state));
        const std::string messageText(reinterpret_cast<const char*>(text));

        std::ostringstream log;
        log << stateText
            << ':'
            << record
            << ':'
            << nativeError
            << ':'
            << messageText;

        Logger::getInstance()->FnLog(log.str(), "", "ODBC");

        messages.push_back(messageText);
    }

    return messages;
}

void odbc::logSimpleError(const std::string& message) const
{
    try
    {
        Logger::getInstance()->FnLogExceptionError(message);
    }
    catch (...)
    {
        // Logging must never make ODBC cleanup/error handling fail.
    }
}

int odbc::isValidSeason(
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
    std::string& dtValidFrom)
{
    std::lock_guard<std::mutex> lock(mutex_);

    sSerialNo.clear();
    iRateType = 0;
    sFee = 0.0F;
    sAdminFee = 0.0F;
    sAppFee = 0.0F;
    iExpireDays = 0;
    iRedeemTime = 0;
    sRedeemAmt = 0.0F;
    dtValidTo.clear();
    dtValidFrom.clear();

    try
    {
        if (!ensureConnectedUnlocked())
        {
            return -1;
        }

        StatementHandle statementHandle;

        SQLRETURN ret =
            ::SQLAllocHandle(
                SQL_HANDLE_STMT,
                dbc,
                statementHandle.put());

        if (!SQL_SUCCEEDED(ret))
        {
            (void)GetError("SQLAllocHandle(SQL_HANDLE_STMT)", dbc, SQL_HANDLE_DBC);
            return -1;
        }

        SQLHSTMT stmt = statementHandle.get();

        if (!setQueryTimeout(stmt))
        {
            return -1;
        }

        auto bindSucceeded =
            [this, stmt](
                SQLRETURN bindRet,
                const char* name)
            {
                if (SQL_SUCCEEDED(bindRet))
                {
                    return true;
                }

                (void)GetError(name, stmt, SQL_HANDLE_STMT);
                return false;
            };

        SQLSCHAR returnStatusValue = 0;
        SQLCHAR tmpSerialNo[5]{};
        SQLCHAR tmpValidTo[24]{};
        SQLCHAR tmpValidFrom[24]{};

        SQLLEN lenNTS = SQL_NTS;
        SQLLEN lenNULL = SQL_NULL_DATA;

        SQLLEN* seasonIndicator = sSeasonNo.empty() ? &lenNULL : &lenNTS;

        ret =
            ::SQLBindParameter(
                stmt,
                1,
                SQL_PARAM_INPUT,
                SQL_C_CHAR,
                SQL_VARCHAR,
                16,
                0,
                const_cast<char*>(sSeasonNo.c_str()),
                16,
                seasonIndicator);

        if (!bindSucceeded( ret, "SQLBindParameter(SQL_PARAM_INPUT1)"))
        {
            return -1;
        }

        ret =
            ::SQLBindParameter(
                stmt,
                2,
                SQL_PARAM_INPUT,
                SQL_C_TINYINT,
                SQL_TINYINT,
                0,
                0,
                &iInOut,
                0,
                nullptr);

        if (!bindSucceeded(ret, "SQLBindParameter(SQL_PARAM_INPUT2)"))
        {
            return -1;
        }

        ret =
            ::SQLBindParameter(
                stmt,
                3,
                SQL_PARAM_OUTPUT,
                SQL_C_TINYINT,
                SQL_TINYINT,
                0,
                0,
                &returnStatusValue,
                0,
                &lenNTS);

        if (!bindSucceeded(ret, "SQLBindParameter(SQL_PARAM_OUTPUT3)"))
        {
            return -1;
        }

        ret =
            ::SQLBindParameter(
                stmt,
                4,
                SQL_PARAM_OUTPUT,
                SQL_C_CHAR,
                SQL_VARCHAR,
                sizeof(tmpSerialNo),
                0,
                tmpSerialNo,
                sizeof(tmpSerialNo),
                &lenNTS);

        if (!bindSucceeded(ret, "SQLBindParameter(SQL_PARAM_OUTPUT4)"))
        {
            return -1;
        }

        ret =
            ::SQLBindParameter(
                stmt,
                5,
                SQL_PARAM_OUTPUT,
                SQL_C_SSHORT,
                SQL_SMALLINT,
                0,
                0,
                &iRateType,
                0,
                &lenNTS);

        if (!bindSucceeded(ret, "SQLBindParameter(SQL_PARAM_OUTPUT5)"))
        {
            return -1;
        }

        ret =
            ::SQLBindParameter(
                stmt,
                6,
                SQL_PARAM_OUTPUT,
                SQL_C_FLOAT,
                SQL_DECIMAL,
                5,
                2,
                &sFee,
                0,
                &lenNTS);

        if (!bindSucceeded(ret, "SQLBindParameter(SQL_PARAM_OUTPUT6)"))
        {
            return -1;
        }

        ret =
            ::SQLBindParameter(
                stmt,
                7,
                SQL_PARAM_OUTPUT,
                SQL_C_FLOAT,
                SQL_DECIMAL,
                5,
                2,
                &sAdminFee,
                0,
                &lenNTS);

        if (!bindSucceeded(ret, "SQLBindParameter(SQL_PARAM_OUTPUT7)"))
        {
            return -1;
        }

        ret =
            ::SQLBindParameter(
                stmt,
                8,
                SQL_PARAM_OUTPUT,
                SQL_C_FLOAT,
                SQL_DECIMAL,
                5,
                2,
                &sAppFee,
                0,
                &lenNTS);

        if (!bindSucceeded(ret, "SQLBindParameter(SQL_PARAM_OUTPUT8)"))
        {
            return -1;
        }

        ret =
            ::SQLBindParameter(
                stmt,
                9,
                SQL_PARAM_OUTPUT,
                SQL_C_SSHORT,
                SQL_SMALLINT,
                0,
                0,
                &iExpireDays,
                0,
                &lenNTS);

        if (!bindSucceeded(ret, "SQLBindParameter(SQL_PARAM_OUTPUT9)"))
        {
            return -1;
        }

        SQLCHAR zoneIdValue = static_cast<SQLCHAR>(iZoneID);

        ret =
            ::SQLBindParameter(
                stmt,
                10,
                SQL_PARAM_INPUT,
                SQL_C_UTINYINT,
                SQL_TINYINT,
                0,
                0,
                &zoneIdValue,
                0,
                &lenNTS);

        if (!bindSucceeded(ret, "SQLBindParameter(SQL_PARAM_INPUT10)"))
        {
            return -1;
        }

        ret =
            ::SQLBindParameter(
                stmt,
                11,
                SQL_PARAM_OUTPUT,
                SQL_C_SSHORT,
                SQL_SMALLINT,
                0,
                0,
                &iRedeemTime,
                0,
                &lenNTS);

        if (!bindSucceeded(ret, "SQLBindParameter(SQL_PARAM_OUTPUT11)"))
        {
            return -1;
        }

        ret =
            ::SQLBindParameter(
                stmt,
                12,
                SQL_PARAM_OUTPUT,
                SQL_C_FLOAT,
                SQL_DECIMAL,
                5,
                2,
                &sRedeemAmt,
                0,
                &lenNTS);

        if (!bindSucceeded(ret, "SQLBindParameter(SQL_PARAM_OUTPUT12)"))
        {
            return -1;
        }

        SQLLEN* holderIndicator = AllowedHolderType.empty() ? &lenNULL : &lenNTS;

        ret =
            ::SQLBindParameter(
                stmt,
                13,
                SQL_PARAM_INPUT,
                SQL_C_CHAR,
                SQL_VARCHAR,
                20,
                0,
                AllowedHolderType.data(),
                20,
                holderIndicator);

        if (!bindSucceeded(ret, "SQLBindParameter(SQL_PARAM_INPUT13)"))
        {
            return -1;
        }

        ret =
            ::SQLBindParameter(
                stmt,
                14,
                SQL_PARAM_OUTPUT,
                SQL_C_CHAR,
                SQL_TIMESTAMP,
                sizeof(tmpValidTo),
                0,
                tmpValidTo,
                sizeof(tmpValidTo),
                &lenNTS);

        if (!bindSucceeded(ret, "SQLBindParameter(SQL_PARAM_OUTPUT14)"))
        {
            return -1;
        }

        ret =
            ::SQLBindParameter(
                stmt,
                15,
                SQL_PARAM_OUTPUT,
                SQL_C_CHAR,
                SQL_TIMESTAMP,
                sizeof(tmpValidFrom),
                0,
                tmpValidFrom,
                sizeof(tmpValidFrom),
                &lenNTS);

        if (!bindSucceeded(ret, "SQLBindParameter(SQL_PARAM_OUTPUT15)"))
        {
            return -1;
        }

        std::string storedProcedure =
            "{CALL sp_IsValidSeason (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)}";

        ret =
            ::SQLPrepare(
                stmt,
                asSqlChar(storedProcedure),
                SQL_NTS);

        if (!SQL_SUCCEEDED(ret))
        {
            (void)GetError("SQLPrepare(SQL_HANDLE_STMT)", stmt, SQL_HANDLE_STMT);
            return -1;
        }

        ret = ::SQLExecute(stmt);

        if (!SQL_SUCCEEDED(ret))
        {
            (void)GetError("SQLExecute(SQL_HANDLE_STMT)", stmt, SQL_HANDLE_STMT);
            return -1;
        }

        for (;;)
        {
            ret = ::SQLMoreResults(stmt);

            if (ret == SQL_NO_DATA)
            {
                break;
            }

            if (!SQL_SUCCEEDED(ret))
            {
                (void)GetError("SQLMoreResults", stmt, SQL_HANDLE_STMT);
                return -1;
            }
        }

        // The original implementation bound tmpSerialNo but never copied it
        // back to sSerialNo. Preserve the stored procedure output correctly.
        sSerialNo = reinterpret_cast<const char*>(tmpSerialNo);

        CE_Time validTo;
        CE_Time validFrom;

        if (std::strlen(reinterpret_cast<const char*>(tmpValidTo)) == 0)
        {
            const std::time_t now = std::time(nullptr);
            std::tm localTime{};

#if defined(_WIN32)
            localtime_s(&localTime, &now);
#else
            localtime_r(&now, &localTime);
#endif

            validTo.SetTime(
                1900 + localTime.tm_year,
                localTime.tm_mon + 1,
                localTime.tm_mday,
                localTime.tm_hour,
                localTime.tm_min,
                localTime.tm_sec);

            validTo.SetTime(
                validTo.Year() - 4,
                validTo.Month(),
                validTo.Day(),
                validTo.Hour(),
                validTo.Minute(),
                validTo.Second());

            dtValidTo = validTo.DateTimeString();
        }
        else
        {
            validTo.SetTime(
                std::string(
                    reinterpret_cast<const char*>(
                        tmpValidTo)));

            // Preserve existing business rule: ValidTo is inclusive, so add
            // one day to the stored-procedure value.
            validTo.SetTime(
                validTo.GetUnixTimestamp() +
                86400);

            dtValidTo = validTo.DateTimeString();
        }

        if (std::strlen(reinterpret_cast<const char*>(tmpValidFrom)) == 0)
        {
            const std::time_t now = std::time(nullptr);
            std::tm localTime{};

#if defined(_WIN32)
            localtime_s(&localTime, &now);
#else
            localtime_r(&now, &localTime);
#endif

            validFrom.SetTime(
                1900 + localTime.tm_year,
                localTime.tm_mon + 1,
                localTime.tm_mday,
                0,
                0,
                0);

            dtValidFrom = validFrom.DateTimeString();
        }
        else
        {
            dtValidFrom = reinterpret_cast<const char*>(tmpValidFrom);
        }

        return static_cast<int>(returnStatusValue);
    }
    catch (const std::exception& e)
    {
        logSimpleError(std::string(__func__) + ", Exception: " + e.what());
        return -1;
    }
    catch (...)
    {
        logSimpleError(std::string(__func__) + ", Exception: Unknown Exception");
        return -1;
    }
}
