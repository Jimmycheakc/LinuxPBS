#include <iostream>
#include <ctime>
#include <iomanip>
#include <stdlib.h>
#include <sstream>
#include <memory>
#include <utility>
#include <sys/time.h>
#include <boost/algorithm/string.hpp>
#include "db.h"
#include "log.h"
#include "operation.h"
#include "common.h"

namespace
{

void logDbMessage(const std::string& message, const std::string& category = "DB")
{
    Logger::getInstance()->FnLog(message, "", category);
}

void disconnectAndReset(std::unique_ptr<odbc>& connection)
{
    if (!connection)
    {
        return;
    }

    try
    {
        connection->Disconnect();
    }
    catch (...)
    {
        // FnClose/destructor must not throw. The odbc destructor will still
        // release the underlying handles when the unique_ptr is reset.
    }

    connection.reset();
}


template <typename Modifier>
bool updateOperationProcess(Modifier&& modifier)
{
    auto* op = operation::getInstance();
    const auto data = op->FnGetSharedData();

    if (!data)
    {
        return false;
    }

    auto process = data->tProcess;
    std::forward<Modifier>(modifier)(process);

    OperationSharedDataUpdate update;
    update.tProcess = std::move(process);

    return op->FnUpdateSharedData(std::move(update));
}

template <typename Modifier>
bool updateOperationSeason(Modifier&& modifier)
{
    auto* op = operation::getInstance();
    const auto data = op->FnGetSharedData();

    if (!data)
    {
        return false;
    }

    auto season = data->tSeason;
    std::forward<Modifier>(modifier)(season);

    OperationSharedDataUpdate update;
    update.tSeason = std::move(season);

    return op->FnUpdateSharedData(std::move(update));
}

template <typename Modifier>
bool updateOperationEntry(Modifier&& modifier)
{
    auto* op = operation::getInstance();

    const auto data = op->FnGetSharedData();

    if (!data)
    {
        return false;
    }

    auto entry = data->tEntry;

    std::forward<Modifier>(modifier)(entry);

    OperationSharedDataUpdate update;
    update.tEntry = std::move(entry);

    return op->FnUpdateSharedData(std::move(update));
}

} // namespace

db::db() = default;

db::~db()
{
    FnClose();
}

db* db::getInstance()
{
    static db instance;
    return &instance;
}

void db::FnClose()
{
    // Lifecycle operation: invoke only after outstanding DB work has drained.
    disconnectAndReset(centraldb);
    disconnectAndReset(localdb);
}

int db::connectcentraldb(string connectStr,string connectIP,int CentralSQLTimeOut, int SP_SQLTimeOut,float mPingTimeOut)
{

    CentralConnStr = std::move(connectStr);
    central_IP = std::move(connectIP);
    CentralDB_TimeOut = CentralSQLTimeOut;
    SP_TimeOut = SP_SQLTimeOut;
    PingTimeOut = mPingTimeOut;

    season_update_flag = 0;
    season_update_count = 0;
    param_update_flag = 0;
    param_update_count = 0;

    // Reinitialization is allowed during the lifecycle, but must not race
    // with other DB operations.
    disconnectAndReset(centraldb);

    centraldb = std::make_unique<odbc>(
        static_cast<unsigned int>(SP_TimeOut),
        1U,
        mPingTimeOut,
        central_IP,
        CentralConnStr);

    if (centraldb->Connect() == 0)
    {
        logDbMessage("Central DB is connected!");

        if (!updateOperationProcess(
                [](tProcess_Struct& process)
                {
                    process.giSystemOnline = 0;
                }))
        {
            logDbMessage("Unable to update Operation shared data.", "DB");
        }

        m_remote_db_err_flag.store(0);
        return 1;
    }

    logDbMessage("Unable to connect Central DB");
    m_remote_db_err_flag.store(1);
    return 0;
}

int db::connectlocaldb(string connectstr,int LocalSQLTimeOut,int SP_SQLTimeOut,float mPingTimeOut)
{
    localConnStr = std::move(connectstr);
    LocalDB_TimeOut = LocalSQLTimeOut;
    SP_TimeOut = SP_SQLTimeOut;
    PingTimeOut = mPingTimeOut;

    season_update_flag = 0;
    season_update_count = 0;
    param_update_flag = 0;
    param_update_count = 0;

    disconnectAndReset(localdb);

    localdb = std::make_unique<odbc>(
        static_cast<unsigned int>(LocalDB_TimeOut),
        1U,
        PingTimeOut,
        "127.0.0.1",
        localConnStr);

    if (localdb->Connect() == 0)
    {
        logDbMessage("Local DB is connected!");
        m_local_db_err_flag.store(0);
        return 1;
    }

    logDbMessage("Unable to connect Local DB");
    m_local_db_err_flag.store(1);
    return 0;
}

// Return:
//  0 = Invalid season
//  1 = Valid
// -1 = Database error
//  2 = Expired
//  3 = Terminated
//  4 = Blocked
//  5 = Lost
//  6 = Passback
//  7 = Not started
//  8 = Not found
//  9 = Complimentary
int db::sp_isvalidseason(
    const std::string& seasonNo,
    BYTE inOut,
    unsigned int zoneId,
    std::string& serialNo,
    short& rateType,
    float& fee,
    float& adminFee,
    float& appFee,
    short& expireDays,
    short& redeemTime,
    float& redeemAmount,
    std::string& allowedHolderType,
    std::string& validTo,
    std::string& validFrom)
{
    return centraldb->isValidSeason(
        seasonNo,
        inOut,
        zoneId,
        serialNo,
        rateType,
        fee,
        adminFee,
        appFee,
        expireDays,
        redeemTime,
        redeemAmount,
        allowedHolderType,
        validTo,
        validFrom);
}

int db::local_isvalidseason(
    const std::string& seasonNo,
    unsigned int zoneId)
{
    try
    {
        const std::string sqlStmt =
            "SELECT "
            "season_type, "
            "s_status, "
            "date_from, "
            "date_to, "
            "vehicle_no, "
            "rate_type, "
            "multi_season_no, "
            "zone_id, "
            "redeem_amt, "
            "redeem_time, "
            "holder_type, "
            "sub_zone_id "
            "FROM season_mst "
            "WHERE (season_no = '" + seasonNo +
            "' OR INSTR(multi_season_no, '" + seasonNo + "') > 0) "
            "AND date_from <= NOW() "
            "AND DATE(date_to) >= DATE(NOW()) "
            "AND (zone_id = '0' OR zone_id = " +
            std::to_string(zoneId) + ")";

        std::vector<ReaderItem> result;

        const int ret = localdb->SQLSelect(sqlStmt, &result, false);

        if (ret != 0)
        {
            return iLocalFail;
        }

        if (result.empty())
        {
            return iNoData;
        }

        const auto& row = result.front();

        if (!updateOperationSeason(
                [&](tseason_struct& season)
                {
                    season.SeasonType = row.GetDataItem(0);
                    season.s_status = row.GetDataItem(1);
                    season.date_from = row.GetDataItem(2);
                    season.date_to = row.GetDataItem(3);
                    season.rate_type = row.GetDataItem(5);
                    season.redeem_amt = row.GetDataItem(8);
                    season.redeem_time = row.GetDataItem(9);
                }))
        {
            logDbMessage("Unable to update Operation season data.", "DB");

            return iLocalFail;
        }

        return iDBSuccess;
    }
    catch (const std::exception& e)
    {
        logDbMessage(std::string("local_isvalidseason exception: ") + e.what(), "DB");

        return iLocalFail;
    }
}

int db::isvalidseason(const std::string& seasonNo, BYTE inOut, unsigned int zoneId)
{
    std::string serialNo;
    float fee = 0.0F;
    short rateType = 0;
    short expireDays = 0;
    short redeemTime = 0;
    float adminFee = 0.0F;
    float appFee = 0.0F;
    float redeemAmt = 0.0F;

    std::string validTo;
    std::string validFrom;
    std::string allowedHolderType;

    logDbMessage("Check Season on Central DB for IU/card: " + seasonNo, "DB");

    int retCode =
        sp_isvalidseason(
            seasonNo,
            inOut,
            zoneId,
            serialNo,
            rateType,
            fee,
            adminFee,
            appFee,
            expireDays,
            redeemTime,
            redeemAmt,
            allowedHolderType,
            validTo,
            validFrom);

    logDbMessage("Check Season Ret = " + std::to_string(retCode), "DB");

    // Central DB accessible
    if (retCode != -1)
    {
        if (retCode != 8)
        {
            logDbMessage("ValidFrom = " + validFrom, "DB");

            logDbMessage("ValidTo = " + validTo, "DB");

            if (!updateOperationSeason(
                    [&](tseason_struct& season)
                    {
                        season.date_from = validFrom;
                        season.date_to = validTo;
                        season.rate_type = std::to_string(rateType);
                        season.redeem_amt = std::to_string(redeemAmt);
                        season.redeem_time = std::to_string(redeemTime);
                    }))
            {
                logDbMessage("Unable to update Operation shared data.", "DB");
                return -1;
            }
        }

        return retCode;
    }

    // Central DB error -> fallback to Local DB
    const int localRet = local_isvalidseason(seasonNo, zoneId);

    retCode = (localRet == iDBSuccess) ? 1 : 8;

    logDbMessage("Check Local Season Return = " + std::to_string(retCode), "DB");

    return retCode;
}

DBError db::insertbroadcasttrans(
    const std::string& sid,
    const std::string& iuNo,
    const std::string& cardNo,
    const std::string& paidAmt,
    const std::string& iType)
{
    localdb->SQLExecutNoneQuery(
        "DELETE FROM Entry_Trans "
        "WHERE iu_tk_No = '" + iuNo + "'");

    CE_Time dt;
    const std::string dateTime = dt.DateString() + " " + dt.TimeString();

    const std::string sqlStmt =
        "INSERT INTO Entry_Trans "
        "(Station_ID, Entry_Time, iu_tk_No, trans_type, card_no, paid_amt) "
        "VALUES ('" +
        sid + "', '" +
        dateTime + "', '" +
        iuNo + "', '" +
        iType + "', '" +
        cardNo + "', '" +
        paidAmt + "')";

    const int result = localdb->SQLExecutNoneQuery(sqlStmt);

    if (result != 0)
    {
        logDbMessage("Insert Broadcast Entry_Trans to Local: fail", "DB");

        return iLocalFail;
    }

    return iDBSuccess;
}

DBError db::UpdateLocalEntry(const std::string& iuTkNo)
{
    const std::string sqlStmt =
        "UPDATE Entry_Trans "
        "SET Status = 3 "
        "WHERE iu_tk_no = '" + iuTkNo + "'";

    const int result = localdb->SQLExecutNoneQuery(sqlStmt);

    if (result != 0)
    {
        logDbMessage("Fail to update Local Entry_Trans", "DB");

        return iLocalFail;
    }

    return iDBSuccess;
}

DBError db::insertentrytrans(tEntryTrans_Struct& tEntry)
{
    std::string sqlStmt;
    std::string lprNo;

    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return iLocalFail;
    }

    const std::string transId = data->tProcess.gsTransID;

    // =========================================================
    // Prepare entry data
    // =========================================================
    tEntry.sEntryTime = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();

    // TK_SerialNo is integer type in DB.
    // Ensure the value contains a valid integer.
    if (tEntry.sSerialNo.empty())
    {
        tEntry.sSerialNo = "0";
    }
    else
    {
        try
        {
            (void)std::stoi(tEntry.sSerialNo);
        }
        catch (const std::exception&)
        {
            tEntry.sSerialNo = "0";
        }
    }

    // Keep Operation-owned tEntry synchronized.
    if (!updateOperationEntry(
            [&](tEntryTrans_Struct& entry)
            {
                entry.sEntryTime = tEntry.sEntryTime;
                entry.sSerialNo = tEntry.sSerialNo;
            }))
    {
        logDbMessage("Unable to update Operation entry data.", "DB");
    }

    // =========================================================
    // Determine LPR number
    // =========================================================
    if (!tEntry.sLPN[0].empty() ||
        !tEntry.sLPN[1].empty())
    {
        if (tEntry.iTransType == 7 ||
            tEntry.iTransType == 8 ||
            tEntry.iTransType == 22)
        {
            lprNo = tEntry.sLPN[1];
        }
        else
        {
            lprNo = tEntry.sLPN[0];
        }
    }

    // =========================================================
    // Try Central DB
    // =========================================================
    if (centraldb->IsConnected() != 1)
    {
        centraldb->Disconnect();

        if (centraldb->Connect() != 0)
        {
            logDbMessage("Insert Entry_Trans to Central: fail1", "DB");

            if (!updateOperationProcess(
                    [](tProcess_Struct& process)
                    {
                        process.giSystemOnline = 1;
                    }))
            {
                logDbMessage("Unable to update Operation shared data.", "DB");
            }

            goto processLocal;
        }
    }

    if (!updateOperationProcess(
            [](tProcess_Struct& process)
            {
                process.giSystemOnline = 0;
            }))
    {
        logDbMessage("Unable to update Operation shared data.", "DB");
    }

    sqlStmt =
        "INSERT INTO Entry_Trans_tmp "
        "("
        "Station_ID,"
        "Entry_Time,"
        "IU_Tk_No,"
        "trans_type,"
        "status,"
        "TK_Serialno,"
        "Card_Type,"
        "card_no,"
        "paid_amt,"
        "parking_fee,"
        "VCC,"
        "gst_amt,"
        "entry_lpn_SID";

    if (!lprNo.empty())
    {
        sqlStmt += ",lpn";
    }

    sqlStmt +=
        ") VALUES ('" +
        tEntry.esid +
        "',convert(datetime,'" +
        tEntry.sEntryTime +
        "',120),'" +
        tEntry.sIUTKNo +
        "','" +
        std::to_string(tEntry.iTransType) +
        "','" +
        std::to_string(tEntry.iStatus) +
        "','" +
        tEntry.sSerialNo +
        "','" +
        std::to_string(tEntry.iCardType) +
        "','" +
        tEntry.sCardNo +
        "','" +
        std::to_string(tEntry.sPaidAmt) +
        "','" +
        std::to_string(tEntry.sFee) +
        "','" +
        tEntry.VCC +
        "','" +
        std::to_string(tEntry.sGSTAmt) +
        "','" +
        transId +
        "'";

    if (!lprNo.empty())
    {
        sqlStmt += ",'" + lprNo + "'";
    }

    sqlStmt += ")";

    if (centraldb->SQLExecutNoneQuery(sqlStmt) != 0)
    {
        logDbMessage(sqlStmt, "DB");
        logDbMessage("Insert Entry_Trans to Central: fail.", "DB");

        return iCentralFail;
    }

    logDbMessage("Insert Entry_Trans to Central: success", "DB");

    return iDBSuccess;


processLocal:

    // =========================================================
    // Local DB fallback
    // =========================================================
    if (localdb->IsConnected() != 1)
    {
        localdb->Disconnect();

        if (localdb->Connect() != 0)
        {
            logDbMessage("Insert Entry_Trans to Local: fail1", "DB");

            return iLocalFail;
        }
    }

    sqlStmt =
        "INSERT INTO Entry_Trans "
        "("
        "Station_id,"
        "Entry_Time,"
        "iu_tk_no,"
        "trans_type,"
        "status,"
        "TK_SerialNo,"
        "Card_Type,"
        "card_no,"
        "paid_amt,"
        "parking_fee,"
        "gst_amt,"
        "entry_lpn_SID,"
        "lpn"
        ") VALUES ('" +
        tEntry.esid +
        "','" +
        tEntry.sEntryTime +
        "','" +
        tEntry.sIUTKNo +
        "','" +
        std::to_string(tEntry.iTransType) +
        "','" +
        std::to_string(tEntry.iStatus) +
        "','" +
        tEntry.sSerialNo +
        "','" +
        std::to_string(tEntry.iCardType) +
        "','" +
        tEntry.sCardNo +
        "','" +
        std::to_string(tEntry.sPaidAmt) +
        "','" +
        std::to_string(tEntry.sFee) +
        "','" +
        std::to_string(tEntry.sGSTAmt) +
        "','" +
        transId +
        "','" +
        lprNo +
        "')";

    if (localdb->SQLExecutNoneQuery(sqlStmt) != 0)
    {
        logDbMessage(sqlStmt, "DB");
        logDbMessage("Insert Entry_Trans to Local: fail", "DB");

        return iLocalFail;
    }

    logDbMessage("Insert Entry_Trans to Local: success", "DB");

    if (!updateOperationProcess(
            [](tProcess_Struct& process)
            {
                ++process.glNoofOfflineData;
            }))
    {
        logDbMessage("Unable to update Operation shared data.", "DB");
    }

    return iDBSuccess;
}

void db::synccentraltime()
{
    // =========================================================
    // Ensure Central DB connection
    // =========================================================
    if (centraldb->IsConnected() != 1)
    {
        centraldb->Disconnect();

        if (centraldb->Connect() != 0)
        {
            return;
        }
    }

    // =========================================================
    // Retrieve Central DB time
    // =========================================================
    const std::string sqlStmt = "SELECT GETDATE() AS CurrentTime";

    std::vector<ReaderItem> result;

    const int ret = centraldb->SQLSelect(sqlStmt, &result, false);

    if (ret != 0)
    {
        logDbMessage("Unable to retrieve Central DB time", "DB");

        m_remote_db_err_flag.store(1);
        return;
    }

    m_remote_db_err_flag.store(0);

    if (result.empty())
    {
        return;
    }

    // =========================================================
    // Parse Central DB time
    // =========================================================
    const std::string dateTime = result.front().GetDataItem(0);

    logDbMessage("Central DB time: " + dateTime, "DB");

    std::tm tmTime{};

    std::istringstream timeStream(dateTime);

    timeStream >> std::get_time(&tmTime, "%Y-%m-%d %H:%M:%S");

    if (timeStream.fail())
    {
        logDbMessage("Failed to parse the time string.", "DB");

        return;
    }

    // =========================================================
    // Convert to epoch time
    // =========================================================
    const std::time_t epochTime = std::mktime(&tmTime);

    timeval newTime{};
    newTime.tv_sec = epochTime;
    newTime.tv_usec = 0;

    // =========================================================
    // Update system time
    // =========================================================
    if (settimeofday(&newTime, nullptr) != 0)
    {
        logDbMessage("Error setting time.", "DB");

        return;
    }

    logDbMessage("Time set successfully.", "DB");

    // =========================================================
    // Sync system time to hardware clock
    // =========================================================
    if (std::system("hwclock --systohc") != 0)
    {
        logDbMessage("Sync error.", "DB");

        return;
    }

    logDbMessage("Sync successfully.", "DB");
}

int db::downloadseason()
{
    constexpr int kBatchSize = 10;

    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return -1;
    }

    const int stationId = data->gtStation.iSID;

    const std::string fetchedColumn = "s" + std::to_string(stationId) + "_fetched";

    // =========================================================
    // Check number of seasons pending download
    // =========================================================
    const std::string countSql =
        "SELECT SUM(A) FROM ("
        "SELECT count(season_no) AS A "
        "FROM season_mst "
        "WHERE " +
        fetchedColumn +
        " = 0 "
        ") AS B";

    std::vector<ReaderItem> countResult;

    const int countRet = centraldb->SQLSelect(countSql, &countResult, false);

    if (countRet != 0)
    {
        m_remote_db_err_flag.store(1);
        return -1;
    }

    if (countResult.empty())
    {
        return -1;
    }

    const std::string pendingCount = countResult.front().GetDataItem(0);

    int totalPending = 0;

    try
    {
        totalPending = std::stoi(pendingCount);
    }
    catch (const std::exception& e)
    {
        logDbMessage(std::string("Invalid season download count: ") + e.what(), "DB");

        return -1;
    }

    if (totalPending == 0)
    {
        return -1;
    }

    m_remote_db_err_flag.store(0);

    logDbMessage("Total: " + pendingCount + " Seasons to be download.", "DB");

    // =========================================================
    // Retrieve next batch
    // =========================================================
    const std::string selectSql =
        "SELECT TOP " +
        std::to_string(kBatchSize) +
        " * FROM season_mst "
        "WHERE " +
        fetchedColumn +
        " = 0";

    std::vector<ReaderItem> result;

    const int selectRet = centraldb->SQLSelect(selectSql, &result, true);

    if (selectRet != 0)
    {
        m_remote_db_err_flag.store(1);
        return -1;
    }

    m_remote_db_err_flag.store(0);

    int downloadCount = 0;

    // =========================================================
    // Process downloaded records
    // =========================================================
    if (!result.empty())
    {
        logDbMessage("Downloading " + std::to_string(result.size()) + " Records: Started", "DB");

        for (const auto& row : result)
        {
            tseason_struct season{};

            season.season_no = row.GetDataItem(1);
            season.SeasonType = row.GetDataItem(2);
            season.s_status = row.GetDataItem(3);
            season.date_from = row.GetDataItem(4);
            season.date_to = row.GetDataItem(5);
            season.holder_type = row.GetDataItem(6);
            season.vehicle_no = row.GetDataItem(8);
            season.rate_type = row.GetDataItem(58);
            season.pay_to = row.GetDataItem(66);
            season.pay_date = row.GetDataItem(67);
            season.multi_season_no = row.GetDataItem(72);
            season.zone_id = row.GetDataItem(77);
            season.redeem_time = row.GetDataItem(78);
            season.redeem_amt = row.GetDataItem(79);
            season.sub_zone_id = row.GetDataItem(80);

            ++season_update_count;

            // =================================================
            // Write season to Local DB
            // =================================================
            const int writeRet = writeseason2local(season);

            if (writeRet != 0)
            {
                continue;
            }

            logDbMessage("Download season: " + season.season_no, "DB");

            // =================================================
            // Mark Central DB record as fetched
            // =================================================
            const std::string updateSql =
                "UPDATE season_mst SET " +
                fetchedColumn +
                " = '1' "
                "WHERE season_no = '" +
                season.season_no +
                "'";

            const int updateRet = centraldb->SQLExecutNoneQuery(updateSql);

            if (updateRet != 0)
            {
                m_remote_db_err_flag.store(2);

                logDbMessage("Update central season status failed.", "DB");

                continue;
            }

            ++downloadCount;

            m_remote_db_err_flag.store(0);
        }

        logDbMessage(
            "Downloading Records: End, Total Record :" +
                std::to_string(result.size()) +
                " ,Downloaded Record :" +
                std::to_string(downloadCount),
            "DB");
    }

    // Less than one complete batch means all pending records
    // from this download cycle have been processed.
    if (result.size() < kBatchSize)
    {
        season_update_flag = 0;
    }

    return downloadCount;
}

int db::writeseason2local(tseason_struct& v)
{
    try
    {
        // =====================================================
        // Check whether season already exists
        // =====================================================
        std::vector<ReaderItem> result;

        const std::string checkSql =
            "SELECT season_type "
            "FROM season_mst "
            "WHERE season_no = '" + v.season_no + "'";

        int ret = localdb->SQLSelect(checkSql, &result, false);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Check local season failed.", "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        // =====================================================
        // Existing season -> UPDATE
        // =====================================================
        if (!result.empty())
        {
            v.found = 1;

            const std::string sqlStmt =
                "UPDATE season_mst SET "
                "season_no = '" + v.season_no + "', "
                "season_type = '" + v.SeasonType + "', "
                "s_status = '" + v.s_status + "', "
                "date_from = '" + v.date_from + "', "
                "date_to = '" + v.date_to + "', "
                "vehicle_no = '" + v.vehicle_no + "', "
                "rate_type = '" + v.rate_type + "', "
                "pay_to = '" + v.pay_to + "', "
                "pay_date = '" + v.pay_date + "', "
                "multi_season_no = '" + v.multi_season_no + "', "
                "zone_id = '" + v.zone_id + "', "
                "redeem_amt = '" + v.redeem_amt + "', "
                "redeem_time = '" + v.redeem_time + "', "
                "holder_type = '" + v.holder_type + "', "
                "sub_zone_id = '" + v.sub_zone_id + "' "
                "WHERE season_no = '" + v.season_no + "'";

            ret = localdb->SQLExecutNoneQuery(sqlStmt);

            if (ret != 0)
            {
                m_local_db_err_flag = 1;

                logDbMessage("Update local season failed.", "DB");

                return ret;
            }

            m_local_db_err_flag = 0;

            return ret;
        }

        // =====================================================
        // New season -> INSERT
        // =====================================================
        v.found = 0;

        const std::string sqlStmt =
            "INSERT INTO season_mst "
            "("
            "season_no, "
            "season_type, "
            "s_status, "
            "date_from, "
            "date_to, "
            "vehicle_no, "
            "rate_type, "
            "pay_to, "
            "pay_date, "
            "multi_season_no, "
            "zone_id, "
            "redeem_amt, "
            "redeem_time, "
            "holder_type, "
            "sub_zone_id"
            ") VALUES ('" +
            v.season_no + "', '" +
            v.SeasonType + "', '" +
            v.s_status + "', '" +
            v.date_from + "', '" +
            v.date_to + "', '" +
            v.vehicle_no + "', '" +
            v.rate_type + "', '" +
            v.pay_to + "', '" +
            v.pay_date + "', '" +
            v.multi_season_no + "', '" +
            v.zone_id + "', '" +
            v.redeem_amt + "', '" +
            v.redeem_time + "', '" +
            v.holder_type + "', '" +
            v.sub_zone_id + "')";

        ret = localdb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage(sqlStmt, "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        logDbMessage("Insert season to local success.", "DB");

        return ret;
    }
    catch (const std::exception& e)
    {
        m_local_db_err_flag = 1;

        logDbMessage(std::string("Local DB error in writing season record: ") + e.what(), "DB");

        return -1;
    }
}

int db::downloadvehicletype()
{
    // =========================================================
    // Get total vehicle type count
    // =========================================================
    const std::string countSql =
        "SELECT SUM(A) FROM ("
        "SELECT COUNT(IUCode) AS A "
        "FROM Vehicle_type"
        ") AS B";

    std::vector<ReaderItem> countResult;

    const int countRet = centraldb->SQLSelect(countSql, &countResult, false);

    if (countRet != 0)
    {
        m_remote_db_err_flag.store(1);

        logDbMessage("Download vehicle type fail.", "DB");

        return -1;
    }

    m_remote_db_err_flag.store(0);

    if (countResult.empty())
    {
        logDbMessage("Unable to retrieve vehicle type count.", "DB");

        return -1;
    }

    logDbMessage("Total " + countResult.front().GetDataItem(0) + " vehicle type to be download.", "DB");

    // =========================================================
    // Download vehicle type records
    // =========================================================
    std::vector<ReaderItem> result;

    const int selectRet = centraldb->SQLSelect("SELECT * FROM Vehicle_type", &result, true);

    if (selectRet != 0)
    {
        m_remote_db_err_flag.store(1);
        return -1;
    }

    m_remote_db_err_flag.store(0);

    int downloadCount = 0;

    if (!result.empty())
    {
        logDbMessage("Downloading " + std::to_string(result.size()) + " Records: Started", "DB");

        for (const auto& row : result)
        {
            const std::string iuCode = row.GetDataItem(1);
            const std::string iuType = row.GetDataItem(2);

            const int writeRet = writevehicletype2local(iuCode, iuType);

            if (writeRet == 0)
            {
                ++downloadCount;
            }
        }

        logDbMessage(
            "Downloading vehicle type Records: End, "
            "Total Record :" +
                std::to_string(result.size()) +
            " ,Downloaded Record :" +
                std::to_string(downloadCount),
            "DB");
    }

    return downloadCount;
}

int db::writevehicletype2local(const std::string& iuCode, const std::string& iuType)
{
    try
    {
        // =====================================================
        // Check whether vehicle type already exists
        // =====================================================
        std::vector<ReaderItem> result;

        const std::string checkSql =
            "SELECT IUCode "
            "FROM Vehicle_type "
            "WHERE IUCode = '" + iuCode + "'";

        int ret = localdb->SQLSelect(checkSql, &result, false);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Update vehicle type to local fail.", "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        // =====================================================
        // Existing record -> UPDATE
        // =====================================================
        if (!result.empty())
        {
            const std::string sqlStmt =
                "UPDATE Vehicle_type "
                "SET TransType = '" + iuType + "' "
                "WHERE IUCode = '" + iuCode + "'";

            ret = localdb->SQLExecutNoneQuery(sqlStmt);

            if (ret != 0)
            {
                m_local_db_err_flag = 1;

                logDbMessage("Update local vehicle type failed.", "DB");

                return ret;
            }

            m_local_db_err_flag = 0;

            return ret;
        }

        // =====================================================
        // New record -> INSERT
        // =====================================================
        const std::string sqlStmt =
            "INSERT INTO Vehicle_type "
            "(IUCode, TransType) "
            "VALUES ('" +
            iuCode + "', '" +
            iuType + "')";

        ret = localdb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Insert vehicle type to local failed.", "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        return ret;
    }
    catch (const std::exception& e)
    {
        m_local_db_err_flag = 1;

        logDbMessage(std::string("Local DB error in writing vehicle type: ") + e.what(), "DB");

        return -1;
    }
}

int db::downloadledmessage()
{
    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return -1;
    }

    const int stationId = data->gtStation.iSID;

    const std::string fetchedColumn = "s" + std::to_string(stationId) + "_fetched";

    // =========================================================
    // Get total pending message count
    // =========================================================
    const std::string countSql =
        "SELECT SUM(A) FROM ("
        "SELECT COUNT(msg_id) AS A "
        "FROM message_mst "
        "WHERE " +
        fetchedColumn +
        " = 0"
        ") AS B";

    std::vector<ReaderItem> countResult;

    const int countRet = centraldb->SQLSelect(countSql, &countResult, false);

    if (countRet != 0)
    {
        m_remote_db_err_flag.store(1);

        logDbMessage("Download LED message fail.", "DB");

        return -1;
    }

    m_remote_db_err_flag.store(0);

    if (countResult.empty())
    {
        logDbMessage("Unable to retrieve LED message count.", "DB");

        return -1;
    }

    logDbMessage("Total " + countResult.front().GetDataItem(0) + " message to be download.", "DB");

    // =========================================================
    // Download pending messages
    // =========================================================
    const std::string selectSql =
        "SELECT * FROM message_mst "
        "WHERE " +
        fetchedColumn +
        " = 0";

    std::vector<ReaderItem> result;

    const int selectRet = centraldb->SQLSelect(selectSql, &result, true);

    if (selectRet != 0)
    {
        m_remote_db_err_flag.store(1);

        logDbMessage("Download LED message fail.", "DB");

        return -1;
    }

    m_remote_db_err_flag.store(0);

    int downloadCount = 0;

    if (!result.empty())
    {
        logDbMessage("Downloading message " + std::to_string(result.size()) + " Records: Started", "DB");

        for (const auto& row : result)
        {
            const std::string msgId = row.GetDataItem(0);
            const std::string msgBody = row.GetDataItem(2);
            const std::string msgStatus = row.GetDataItem(3);

            const int writeRet = writeledmessage2local(msgId, msgBody, msgStatus);

            if (writeRet != 0)
            {
                continue;
            }

            // =================================================
            // Mark Central DB message as fetched
            // =================================================
            const std::string updateSql =
                "UPDATE message_mst SET " +
                fetchedColumn +
                " = '1' "
                "WHERE msg_id = '" +
                msgId +
                "'";

            const int updateRet = centraldb->SQLExecutNoneQuery(updateSql);

            if (updateRet != 0)
            {
                m_remote_db_err_flag.store(2);

                logDbMessage("Update central message status failed.", "DB");

                continue;
            }

            ++downloadCount;
            m_remote_db_err_flag.store(0);
        }

        logDbMessage(
            "Downloading Msg Records: End, "
            "Total Record :" +
                std::to_string(result.size()) +
            " ,Downloaded Record :" +
                std::to_string(downloadCount),
            "DB");
    }

    return downloadCount;
}

int db::writeledmessage2local(const std::string& msgId, const std::string& msgBody, const std::string& msgStatus)
{
    try
    {
        // =====================================================
        // Check whether message already exists
        // =====================================================
        std::vector<ReaderItem> result;

        const std::string checkSql =
            "SELECT msg_id "
            "FROM message_mst "
            "WHERE msg_id = '" + msgId + "'";

        int ret = localdb->SQLSelect(checkSql, &result, false);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Update local LED message failed.", "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        // =====================================================
        // Existing message -> UPDATE
        // =====================================================
        if (!result.empty())
        {
            const std::string sqlStmt =
                "UPDATE message_mst SET "
                "msg_body = '" + msgBody + "', "
                "m_status = '" + msgStatus + "' "
                "WHERE msg_id = '" + msgId + "'";

            ret = localdb->SQLExecutNoneQuery(sqlStmt);

            if (ret != 0)
            {
                m_local_db_err_flag = 1;

                logDbMessage("Update local message failed.", "DB");

                return ret;
            }

            m_local_db_err_flag = 0;

            return ret;
        }

        // =====================================================
        // New message -> INSERT
        // =====================================================
        const std::string sqlStmt =
            "INSERT INTO message_mst "
            "(msg_id, msg_body, m_status) "
            "VALUES ('" +
            msgId + "', '" +
            msgBody + "', '" +
            msgStatus + "')";

        ret = localdb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Insert message to local failed.", "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        return ret;
    }
    catch (const std::exception& e)
    {
        m_local_db_err_flag = 1;

        logDbMessage(std::string("Local DB error in writing LED message: ") + e.what(), "DB");

        return -1;
    }
}

int db::downloadparameter()
{
    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return -1;
    }

    const int stationId = data->gtStation.iSID;

    const std::string fetchedColumn = "s" + std::to_string(stationId) + "_fetched";

    // =========================================================
    // Get total pending parameter count
    // =========================================================
    const std::string countSql =
        "SELECT SUM(A) FROM ("
        "SELECT COUNT(name) AS A "
        "FROM parameter_mst "
        "WHERE " +
        fetchedColumn +
        " = 0 "
        "AND for_station = 1"
        ") AS B";

    std::vector<ReaderItem> countResult;

    const int countRet = centraldb->SQLSelect(countSql, &countResult, false);

    if (countRet != 0)
    {
        m_remote_db_err_flag.store(1);

        logDbMessage("Download parameter fail.", "DB");

        return -1;
    }

    m_remote_db_err_flag.store(0);

    if (countResult.empty())
    {
        logDbMessage("Unable to retrieve parameter count.", "DB");

        return -1;
    }

    logDbMessage("Total " + countResult.front().GetDataItem(0) + " parameter to be download.", "DB");

    // =========================================================
    // Download pending parameters
    // =========================================================
    const std::string selectSql =
        "SELECT * FROM parameter_mst "
        "WHERE " +
        fetchedColumn +
        " = 0 "
        "AND for_station = 1";

    std::vector<ReaderItem> result;

    const int selectRet = centraldb->SQLSelect(selectSql, &result, true);

    if (selectRet != 0)
    {
        m_remote_db_err_flag.store(1);

        logDbMessage("Update parameter failed.", "DB");

        return -1;
    }

    m_remote_db_err_flag.store(0);

    int downloadCount = 0;

    // =========================================================
    // No pending parameter
    // =========================================================
    if (result.empty())
    {
        param_update_flag = 0;
        return downloadCount;
    }

    logDbMessage("Downloading parameter " + std::to_string(result.size()) + " Records: Started", "DB");

    // Station-specific parameter value column.
    const std::size_t valueColumn = static_cast<std::size_t>(49 + stationId);

    // =========================================================
    // Process parameters
    // =========================================================
    for (const auto& row : result)
    {
        const std::string paramName = row.GetDataItem(0);
        const std::string paramValue = row.GetDataItem(valueColumn);

        logDbMessage(paramName + " = " + paramValue, "DB");

        ++param_update_count;

        const int writeRet = writeparameter2local(paramName, paramValue);

        if (writeRet != 0)
        {
            continue;
        }

        // =====================================================
        // Mark Central DB parameter as fetched
        // =====================================================
        const std::string updateSql =
            "UPDATE parameter_mst SET " +
            fetchedColumn +
            " = '1' "
            "WHERE name = '" +
            paramName +
            "'";

        const int updateRet = centraldb->SQLExecutNoneQuery(updateSql);

        if (updateRet != 0)
        {
            m_remote_db_err_flag.store(2);

            logDbMessage("Update central parameter status failed.", "DB");

            continue;
        }

        ++downloadCount;
        m_remote_db_err_flag.store(0);
    }

    logDbMessage(
        "Downloading Parameter Records: End, "
        "Total Record :" +
            std::to_string(result.size()) +
        " ,Downloaded Record :" +
            std::to_string(downloadCount),
        "DB");

    return downloadCount;
}

int db::writeparameter2local(const std::string& name, const std::string& value)
{
    try
    {
        // =====================================================
        // Check whether parameter already exists
        // =====================================================
        std::vector<ReaderItem> result;

        const std::string checkSql =
            "SELECT ParamName "
            "FROM Param_mst "
            "WHERE ParamName = '" + name + "'";

        int ret = localdb->SQLSelect(checkSql, &result, false);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Update parameter fail.", "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        // =====================================================
        // Existing parameter -> UPDATE
        // =====================================================
        if (!result.empty())
        {
            const std::string sqlStmt =
                "UPDATE Param_mst "
                "SET ParamValue = '" + value + "' "
                "WHERE ParamName = '" + name + "'";

            ret = localdb->SQLExecutNoneQuery(sqlStmt);

            if (ret != 0)
            {
                m_local_db_err_flag = 1;

                logDbMessage("Update parameter to local failed.", "DB");

                return ret;
            }

            m_local_db_err_flag = 0;

            return ret;
        }

        // =====================================================
        // New parameter -> INSERT
        // =====================================================
        const std::string sqlStmt =
            "INSERT INTO Param_mst "
            "(ParamName, ParamValue) "
            "VALUES ('" +
            name + "', '" +
            value + "')";

        ret = localdb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Insert parameter to local failed.", "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        return ret;
    }
    catch (const std::exception& e)
    {
        m_local_db_err_flag = 1;

        logDbMessage(std::string("Local DB error in writing parameter: ") + e.what(), "DB");

        return -1;
    }
}

int db::downloadstationsetup()
{
    // =========================================================
    // Get total station setup count
    // =========================================================
    const std::string countSql =
        "SELECT SUM(A) FROM ("
        "SELECT COUNT(station_id) AS A "
        "FROM station_setup"
        ") AS B";

    std::vector<ReaderItem> countResult;

    const int countRet = centraldb->SQLSelect(countSql, &countResult, false);

    if (countRet != 0)
    {
        m_remote_db_err_flag.store(1);

        logDbMessage("Download station setup fail.", "DB");

        return -1;
    }

    m_remote_db_err_flag.store(0);

    if (countResult.empty())
    {
        logDbMessage("Unable to retrieve station setup count.", "DB");

        return -1;
    }

    logDbMessage("Total " + countResult.front().GetDataItem(0) + " station setup to be download.", "DB");

    // =========================================================
    // Download station setup records
    // =========================================================
    std::vector<ReaderItem> result;

    const int selectRet = centraldb->SQLSelect("SELECT * FROM station_setup", &result, true);

    if (selectRet != 0)
    {
        m_remote_db_err_flag.store(1);

        logDbMessage("Download station setup fail.", "DB");

        return -1;
    }

    m_remote_db_err_flag.store(0);

    int downloadCount = 0;

    if (!result.empty())
    {
        for (const auto& row : result)
        {
            tstation_struct station{};

            station.iGroupID = std::stoi(row.GetDataItem(0));
            station.iSID = std::stoi(row.GetDataItem(2));
            station.sName = row.GetDataItem(3);
            station.sPCName = row.GetDataItem(4);

            switch (std::stoi(row.GetDataItem(5)))
            {
                case 1:
                    station.iType = tientry;
                    break;

                case 2:
                    station.iType = tiExit;
                    break;

                default:
                    break;
            }

            station.iStatus = std::stoi(row.GetDataItem(6));
            station.iCHUPort = std::stoi(row.GetDataItem(17));
            station.iAntID = std::stoi(row.GetDataItem(18));
            station.iZoneID = std::stoi(row.GetDataItem(19));
            station.iIsVirtual = std::stoi(row.GetDataItem(20));
            station.iVirtualID = std::stoi(row.GetDataItem(21));

            switch (std::stoi(row.GetDataItem(22)))
            {
                case 0:
                    station.iSubType = iNormal;
                    break;

                case 1:
                    station.iSubType = iXwithVENoPay;
                    break;

                case 2:
                    station.iSubType = iXwithVEPay;
                    break;

                default:
                    break;
            }

            const int writeRet = writestationsetup2local(station);

            if (writeRet == 0)
            {
                ++downloadCount;
            }
        }

        logDbMessage(
            "Downloading Station Setup Records: End, "
            "Total Record :" +
                std::to_string(result.size()) +
            " ,Downloaded Record :" +
                std::to_string(downloadCount),
            "DB");
    }

    return downloadCount;
}

int db::writestationsetup2local(const tstation_struct& v)
{
    try
    {
        const std::string stationId = std::to_string(v.iSID);

        // =====================================================
        // Check whether station already exists
        // =====================================================
        std::vector<ReaderItem> result;

        const std::string checkSql =
            "SELECT StationType "
            "FROM Station_Setup "
            "WHERE StationID = '" + stationId + "'";

        int ret = localdb->SQLSelect(checkSql, &result, false);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Update station setup fail.", "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        // =====================================================
        // Existing station -> UPDATE
        // =====================================================
        if (!result.empty())
        {
            const std::string sqlStmt =
                "UPDATE Station_Setup SET "
                "StationID = '" + stationId + "', "
                "StationName = '" + v.sName + "', "
                "StationType = '" + std::to_string(v.iType) + "', "
                "Status = '" + std::to_string(v.iStatus) + "', "
                "PCName = '" + v.sPCName + "', "
                "CHUPort = '" + std::to_string(v.iCHUPort) + "', "
                "AntID = '" + std::to_string(v.iAntID) + "', "
                "ZoneID = '" + std::to_string(v.iZoneID) + "', "
                "IsVirtual = '" + std::to_string(v.iIsVirtual) + "', "
                "SubType = '" + std::to_string(v.iSubType) + "', "
                "VirtualID = '" + std::to_string(v.iVirtualID) + "' "
                "WHERE StationID = '" + stationId + "'";

            ret = localdb->SQLExecutNoneQuery(sqlStmt);

            if (ret != 0)
            {
                m_local_db_err_flag = 1;

                logDbMessage("Update local station setup failed.", "DB");

                return ret;
            }

            m_local_db_err_flag = 0;

            return ret;
        }

        // =====================================================
        // New station -> INSERT
        // =====================================================
        const std::string sqlStmt =
            "INSERT INTO Station_Setup "
            "("
            "StationID, "
            "StationName, "
            "StationType, "
            "Status, "
            "PCName, "
            "CHUPort, "
            "AntID, "
            "ZoneID, "
            "IsVirtual, "
            "SubType, "
            "VirtualID"
            ") VALUES ('" +
            stationId + "', '" +
            v.sName + "', '" +
            std::to_string(v.iType) + "', '" +
            std::to_string(v.iStatus) + "', '" +
            v.sPCName + "', '" +
            std::to_string(v.iCHUPort) + "', '" +
            std::to_string(v.iAntID) + "', '" +
            std::to_string(v.iZoneID) + "', '" +
            std::to_string(v.iIsVirtual) + "', '" +
            std::to_string(v.iSubType) + "', '" +
            std::to_string(v.iVirtualID) + "')";

        ret = localdb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Insert station setup to local failed.", "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        return ret;
    }
    catch (const std::exception& e)
    {
        m_local_db_err_flag = 1;

        logDbMessage(std::string("Local DB error in writing station setup: ") + e.what(), "DB");

        return -1;
    }
}

int db::downloadtariffsetup(int iGrpID, int iSiteID, int iCheckStatus)
{
    (void)iSiteID; // Currently unused

    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return -1;
    }

    const int stationId = data->gtStation.iSID;

    const std::string fetchedColumn = "s" + std::to_string(stationId) + "_fetched";

    // =========================================================
    // Check whether tariff download is required
    // =========================================================
    if (iCheckStatus == 1)
    {
        const std::string checkSql =
            "SELECT name FROM parameter_mst "
            "WHERE name = 'DownloadTariff' "
            "AND " +
            fetchedColumn +
            " = 0";

        std::vector<ReaderItem> checkResult;

        const int checkRet = centraldb->SQLSelect(checkSql, &checkResult, true);

        if (checkRet != 0)
        {
            m_remote_db_err_flag.store(1);

            logDbMessage("Download tariff_setup failed.", "DB");

            return -1;
        }

        if (checkResult.empty())
        {
            m_remote_db_err_flag.store(0);

            logDbMessage("Tariff already downloaded.", "DB");

            return -3;
        }
    }

    // =========================================================
    // Clear existing local tariff
    // =========================================================
    const int deleteRet = localdb->SQLExecutNoneQuery("DELETE FROM tariff_setup");

    if (deleteRet != 0)
    {
        m_local_db_err_flag = 1;

        logDbMessage("Delete tariff_setup from local failed.", "DB");

        return -1;
    }

    m_local_db_err_flag = 0;

    // =========================================================
    // Determine zone
    // =========================================================
    const int zoneId = (data->gtStation.iZoneID > 0) ? data->gtStation.iZoneID : 1;

    logDbMessage(
        "Download tariff_setup for group " +
            std::to_string(iGrpID) +
        ", zone " +
            std::to_string(zoneId),
        "DB");

    // =========================================================
    // Download tariff setup
    // =========================================================
    std::string selectSql = "SELECT * FROM tariff_setup";

    if (iGrpID > 0)
    {
        selectSql +=
            " WHERE group_id = " +
            std::to_string(iGrpID) +
            " AND Zone_id = " +
            std::to_string(zoneId);
    }

    std::vector<ReaderItem> result;

    const int selectRet = centraldb->SQLSelect(selectSql, &result, true);

    if (selectRet != 0)
    {
        m_remote_db_err_flag.store(1);

        logDbMessage("Download tariff_setup failed.", "DB");

        return -1;
    }

    m_remote_db_err_flag.store(0);

    int downloadCount = 0;

    // =========================================================
    // Write tariffs to local DB
    // =========================================================
    for (const auto& row : result)
    {
        tariff_struct tariff;

        tariff.tariff_id = row.GetDataItem(0);
        tariff.day_index = row.GetDataItem(4);
        tariff.day_type = row.GetDataItem(5);

        int index = 6;

        for (int i = 0; i < 9; ++i)
        {
            tariff.start_time[i] = row.GetDataItem(index++);
            tariff.end_time[i] = row.GetDataItem(index++);
            tariff.rate_type[i] = row.GetDataItem(index++);
            tariff.charge_time_block[i] = row.GetDataItem(index++);
            tariff.charge_rate[i] = row.GetDataItem(index++);
            tariff.grace_time[i] = row.GetDataItem(index++);
            tariff.first_free[i] = row.GetDataItem(index++);
            tariff.first_add[i] = row.GetDataItem(index++);
            tariff.second_free[i] = row.GetDataItem(index++);
            tariff.second_add[i] = row.GetDataItem(index++);
            tariff.third_free[i] = row.GetDataItem(index++);
            tariff.third_add[i] = row.GetDataItem(index++);
            tariff.allowance[i] = row.GetDataItem(index++);
            tariff.min_charge[i] = row.GetDataItem(index++);
            tariff.max_charge[i] = row.GetDataItem(index++);
        }

        tariff.zone_cutoff = row.GetDataItem(index++);
        tariff.day_cutoff = row.GetDataItem(index++);
        tariff.whole_day_max = row.GetDataItem(index++);
        tariff.whole_day_min = row.GetDataItem(index++);

        const int writeRet = writetariffsetup2local(tariff);

        if (writeRet == 0)
        {
            ++downloadCount;
        }
    }

    if (!result.empty())
    {
        logDbMessage(
            "Downloading tariff_setup Records: End, "
            "Total Record :" +
                std::to_string(result.size()) +
            " ,Downloaded Record :" +
                std::to_string(downloadCount),
            "DB");
    }

    // =========================================================
    // Mark DownloadTariff as fetched
    // =========================================================
    if (iCheckStatus == 1)
    {
        const std::string updateSql =
            "UPDATE parameter_mst SET " +
            fetchedColumn +
            " = 1 "
            "WHERE name = 'DownloadTariff'";

        const int updateRet = centraldb->SQLExecutNoneQuery(updateSql);

        if (updateRet != 0)
        {
            m_remote_db_err_flag.store(1);

            logDbMessage("Set DownloadTariff fetched=1 failed.", "DB");
        }
    }

    // Original behavior:
    // no tariff record -> -1
    if (result.empty())
    {
        return -1;
    }

    return downloadCount;
}

int db::writetariffsetup2local(const tariff_struct& tariff)
{
    constexpr int kTariffPeriods = 9;

    try
    {
        // =====================================================
        // Build column list
        // =====================================================
        std::string sqlStmt =
            "INSERT INTO tariff_setup "
            "(tariff_id, day_index";

        for (int i = 1; i <= kTariffPeriods; ++i)
        {
            const std::string index =
                std::to_string(i);

            sqlStmt +=
                ", start_time" + index +
                ", end_time" + index +
                ", rate_type" + index +
                ", charge_time_block" + index +
                ", charge_rate" + index +
                ", grace_time" + index +
                ", min_charge" + index +
                ", max_charge" + index +
                ", first_free" + index +
                ", first_add" + index +
                ", second_free" + index +
                ", second_add" + index +
                ", third_free" + index +
                ", third_add" + index +
                ", allowance" + index;
        }

        sqlStmt +=
            ", zone_cutoff"
            ", day_cutoff"
            ", whole_day_max"
            ", whole_day_min"
            ", day_type"
            ") VALUES (" +
            tariff.tariff_id +
            ", " +
            tariff.day_index;

        // =====================================================
        // Build tariff period values
        // =====================================================
        for (int i = 0; i < kTariffPeriods; ++i)
        {
            sqlStmt +=
                ", '" + tariff.start_time[i] + "'" +
                ", '" + tariff.end_time[i] + "'" +
                ", " + tariff.rate_type[i] +
                ", " + tariff.charge_time_block[i] +
                ", '" + tariff.charge_rate[i] + "'" +
                ", " + tariff.grace_time[i] +
                ", '" + tariff.min_charge[i] + "'" +
                ", '" + tariff.max_charge[i] + "'" +
                ", " + tariff.first_free[i] +
                ", '" + tariff.first_add[i] + "'" +
                ", " + tariff.second_free[i] +
                ", '" + tariff.second_add[i] + "'" +
                ", " + tariff.third_free[i] +
                ", '" + tariff.third_add[i] + "'" +
                ", " + tariff.allowance[i];
        }

        sqlStmt +=
            ", " + tariff.zone_cutoff +
            ", " + tariff.day_cutoff +
            ", '" + tariff.whole_day_max + "'" +
            ", '" + tariff.whole_day_min + "'" +
            ", '" + tariff.day_type + "'" +
            ")";

        // =====================================================
        // Insert into local DB
        // =====================================================
        const int ret = localdb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Insert tariff setup to local failed.", "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        return ret;
    }
    catch (const std::exception& e)
    {
        m_local_db_err_flag = 1;

        logDbMessage(std::string("Local DB error in writing tariff setup: ") + e.what(), "DB");

        return -1;
    }
}

int db::downloadtarifftypeinfo()
{
    // =========================================================
    // Clear existing local tariff type info
    // =========================================================
    const int deleteRet = localdb->SQLExecutNoneQuery("DELETE FROM tariff_type_info");

    if (deleteRet != 0)
    {
        m_local_db_err_flag = 1;

        logDbMessage("Delete tariff_type_info from local failed.", "DB");

        return -1;
    }

    m_local_db_err_flag = 0;

    // =========================================================
    // Download tariff type info from Central DB
    // =========================================================
    logDbMessage("Download tariff_type_info.", "DB");

    std::vector<ReaderItem> result;

    const int selectRet = centraldb->SQLSelect("SELECT * FROM tariff_type_info", &result, true);

    if (selectRet != 0)
    {
        m_remote_db_err_flag.store(1);

        logDbMessage("Download tariff_type_info failed.", "DB");

        return -1;
    }

    m_remote_db_err_flag.store(0);

    // Original behavior:
    // no record -> return -1
    if (result.empty())
    {
        return -1;
    }

    int downloadCount = 0;

    // =========================================================
    // Write tariff type info to Local DB
    // =========================================================
    for (const auto& row : result)
    {
        tariff_type_info_struct tariffType;

        tariffType.tariff_type = row.GetDataItem(0);
        tariffType.start_time = row.GetDataItem(1);
        tariffType.end_time = row.GetDataItem(2);

        const int writeRet = writetarifftypeinfo2local(tariffType);

        if (writeRet == 0)
        {
            ++downloadCount;
        }
    }

    logDbMessage(
        "Downloading tariff_type_info Records: End, "
        "Total Record :" +
            std::to_string(result.size()) +
        " ,Downloaded Record :" +
            std::to_string(downloadCount),
        "DB");

    return downloadCount;
}

int db::writetarifftypeinfo2local(const tariff_type_info_struct& tariffType)
{
    try
    {
        const std::string sqlStmt =
            "INSERT INTO tariff_type_info "
            "(tariff_type, start_time, end_time) "
            "VALUES (" +
            tariffType.tariff_type + ", '" +
            tariffType.start_time + "', '" +
            tariffType.end_time + "')";

        const int ret = localdb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Insert tariff type info to local failed.", "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        return ret;
    }
    catch (const std::exception& e)
    {
        m_local_db_err_flag = 1;

        logDbMessage(std::string("Local DB error in writing tariff type info: ") + e.what(), "DB");

        return -1;
    }
}

int db::downloadxtariff(int iGrpID, int iSiteID, int iCheckStatus)
{
    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return -1;
    }

    const int stationId = data->gtStation.iSID;

    const std::string fetchedColumn = "s" + std::to_string(stationId) + "_fetched";

    // =========================================================
    // Check whether X_Tariff download is required
    // =========================================================
    if (iCheckStatus == 1)
    {
        const std::string checkSql =
            "SELECT name FROM parameter_mst "
            "WHERE name = 'DownloadXTariff' "
            "AND " +
            fetchedColumn +
            " = 0";

        std::vector<ReaderItem> checkResult;

        const int checkRet = centraldb->SQLSelect(checkSql, &checkResult, true);

        if (checkRet != 0)
        {
            m_remote_db_err_flag.store(1);

            logDbMessage("Download X_Tariff failed.", "DB");

            return -1;
        }

        if (checkResult.empty())
        {
            m_remote_db_err_flag.store(0);

            logDbMessage("X_Tariff already downloaded.", "DB");

            return -3;
        }
    }

    // =========================================================
    // Clear existing local X_Tariff
    // =========================================================
    const int deleteRet = localdb->SQLExecutNoneQuery("DELETE FROM X_Tariff");

    if (deleteRet != 0)
    {
        m_local_db_err_flag = 1;

        logDbMessage("Delete X_Tariff from local failed.", "DB");

        return -1;
    }

    m_local_db_err_flag = 0;

    // =========================================================
    // Download X_Tariff from Central DB
    // =========================================================
    logDbMessage(
        "Download X_Tariff for group: " +
            std::to_string(iGrpID) +
        ", site: " +
            std::to_string(iSiteID),
        "DB");

    const std::string selectSql =
        "SELECT * FROM X_Tariff "
        "WHERE group_id = " +
        std::to_string(iGrpID) +
        " AND site_id = " +
        std::to_string(iSiteID);

    std::vector<ReaderItem> result;

    const int selectRet = centraldb->SQLSelect(selectSql, &result, true);

    if (selectRet != 0)
    {
        m_remote_db_err_flag.store(1);

        logDbMessage("Download X_Tariff failed.", "DB");

        return -1;
    }

    m_remote_db_err_flag.store(0);

    int downloadCount = 0;

    // =========================================================
    // Write X_Tariff to Local DB
    // =========================================================
    for (const auto& row : result)
    {
        x_tariff_struct xTariff;

        xTariff.day_index = row.GetDataItem(2);
        xTariff.auto0 = row.GetDataItem(3);
        xTariff.fee0 = row.GetDataItem(4);
        xTariff.time1 = row.GetDataItem(5);
        xTariff.auto1 = row.GetDataItem(6);
        xTariff.fee1 = row.GetDataItem(7);
        xTariff.time2 = row.GetDataItem(8);
        xTariff.auto2 = row.GetDataItem(9);
        xTariff.fee2 = row.GetDataItem(10);
        xTariff.time3 = row.GetDataItem(11);
        xTariff.auto3 = row.GetDataItem(12);
        xTariff.fee3 = row.GetDataItem(13);
        xTariff.time4 = row.GetDataItem(14);
        xTariff.auto4 = row.GetDataItem(15);
        xTariff.fee4 = row.GetDataItem(16);

        const int writeRet = writextariff2local(xTariff);

        if (writeRet == 0)
        {
            ++downloadCount;
        }
    }

    if (!result.empty())
    {
        logDbMessage(
            "Downloading x_tariff Records: End, "
            "Total Record :" +
                std::to_string(result.size()) +
            " ,Downloaded Record :" +
                std::to_string(downloadCount),
            "DB");
    }

    // =========================================================
    // Mark DownloadXTariff as fetched
    // =========================================================
    if (iCheckStatus == 1)
    {
        const std::string updateSql =
            "UPDATE parameter_mst SET " +
            fetchedColumn +
            " = 1 "
            "WHERE name = 'DownloadXTariff'";

        const int updateRet = centraldb->SQLExecutNoneQuery(updateSql);

        if (updateRet != 0)
        {
            m_remote_db_err_flag.store(1);

            logDbMessage("Set DownloadXTariff fetched=1 failed.", "DB");
        }
    }

    // Original behavior:
    // no X_Tariff record -> -1
    if (result.empty())
    {
        return -1;
    }

    return downloadCount;
}

int db::writextariff2local(const x_tariff_struct& xTariff)
{
    try
    {
        const std::string sqlStmt =
            "INSERT INTO X_Tariff "
            "(day_index, auto0, fee0, "
            "time1, auto1, fee1, "
            "time2, auto2, fee2, "
            "time3, auto3, fee3, "
            "time4, auto4, fee4) "
            "VALUES ('" +
            xTariff.day_index + "', " +
            xTariff.auto0 + ", " +
            xTariff.fee0 + ", '" +
            xTariff.time1 + "', " +
            xTariff.auto1 + ", " +
            xTariff.fee1 + ", '" +
            xTariff.time2 + "', " +
            xTariff.auto2 + ", " +
            xTariff.fee2 + ", '" +
            xTariff.time3 + "', " +
            xTariff.auto3 + ", " +
            xTariff.fee3 + ", '" +
            xTariff.time4 + "', " +
            xTariff.auto4 + ", " +
            xTariff.fee4 +
            ")";

        const int ret = localdb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Insert X_Tariff to local failed.", "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        return ret;
    }
    catch (const std::exception& e)
    {
        m_local_db_err_flag = 1;

        logDbMessage(std::string("Local DB error in writing X_Tariff: ") + e.what(), "DB");

        return -1;
    }
}

int db::downloadholidaymst(int iCheckStatus)
{
    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return -1;
    }

    const int stationId = data->gtStation.iSID;

    const std::string fetchedColumn = "s" + std::to_string(stationId) + "_fetched";

    // =========================================================
    // Check whether holiday download is required
    // =========================================================
    if (iCheckStatus == 1)
    {
        const std::string checkSql =
            "SELECT name FROM parameter_mst "
            "WHERE name = 'DownloadHoliday' "
            "AND " +
            fetchedColumn +
            " = 0";

        std::vector<ReaderItem> checkResult;

        const int checkRet = centraldb->SQLSelect(checkSql, &checkResult, true);

        if (checkRet != 0)
        {
            m_remote_db_err_flag.store(1);

            logDbMessage("Download holiday_mst failed.", "DB");

            return -1;
        }

        if (checkResult.empty())
        {
            m_remote_db_err_flag.store(0);

            logDbMessage("holiday_mst already downloaded.", "DB");

            return -3;
        }
    }

    // =========================================================
    // Download holiday records
    // =========================================================
    logDbMessage("Download holiday_mst.", "DB");

    const std::string selectSql =
        "SELECT holiday_date, descrip "
        "FROM holiday_mst "
        "WHERE holiday_date > GETDATE() - 30";

    std::vector<ReaderItem> result;

    const int selectRet = centraldb->SQLSelect(selectSql, &result, true);

    if (selectRet != 0)
    {
        m_remote_db_err_flag.store(1);

        logDbMessage("Download holiday_mst failed.", "DB");

        return -1;
    }

    m_remote_db_err_flag.store(0);

    int downloadCount = 0;

    // =========================================================
    // Replace local holiday records
    // =========================================================
    if (!result.empty())
    {
        ClearHoliday();

        for (const auto& row : result)
        {
            const std::string holidayDate = row.GetDataItem(0);
            const std::string description = row.GetDataItem(1);

            const int writeRet = writeholidaymst2local(holidayDate, description);

            if (writeRet == 0)
            {
                ++downloadCount;
            }
        }

        logDbMessage(
            "Downloading holiday_mst Records: End, "
            "Total Record :" +
                std::to_string(result.size()) +
            " ,Downloaded Record :" +
                std::to_string(downloadCount),
            "DB");
    }

    // =========================================================
    // Mark DownloadHoliday as fetched
    // =========================================================
    if (iCheckStatus == 1)
    {
        const std::string updateSql =
            "UPDATE parameter_mst SET " +
            fetchedColumn +
            " = 1 "
            "WHERE name = 'DownloadHoliday'";

        const int updateRet = centraldb->SQLExecutNoneQuery(updateSql);

        if (updateRet != 0)
        {
            m_remote_db_err_flag.store(1);

            logDbMessage("Set DownloadHoliday fetched=1 failed.", "DB");
        }
    }

    // Original behavior:
    // no holiday record -> return -1
    if (result.empty())
    {
        return -1;
    }

    return downloadCount;
}

int db::writeholidaymst2local(const std::string& holidayDate, const std::string& description)
{
    try
    {
        const std::string sqlStmt =
            "INSERT INTO holiday_mst "
            "(holiday_date, descrip) "
            "VALUES ('" +
            holidayDate + "', '" +
            description + "')";

        const int ret = localdb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Insert holiday to local failed.", "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        return ret;
    }
    catch (const std::exception& e)
    {
        m_local_db_err_flag = 1;

        logDbMessage(std::string("Local DB error in writing holiday record: ") + e.what(), "DB");

        return -1;
    }
}

int db::download3tariffinfo()
{
    // =========================================================
    // Clear existing local 3Tariff_Info
    // =========================================================
    const int deleteRet = localdb->SQLExecutNoneQuery("DELETE FROM 3Tariff_Info");

    if (deleteRet != 0)
    {
        // Preserve original behavior
        m_remote_db_err_flag.store(1);

        logDbMessage("Delete 3Tariff_Info from local failed.", "DB");

        return -1;
    }

    m_remote_db_err_flag.store(0);

    // =========================================================
    // Get station information
    // =========================================================
    const auto data =
        operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return -1;
    }

    const int zoneId = data->gtStation.iZoneID;

    const std::string zone = std::to_string(zoneId);

    logDbMessage("Download 3Tariff_Info.", "DB");

    // =========================================================
    // Download tariff info for current zone
    // =========================================================
    const std::string selectSql =
        "SELECT * FROM [3Tariff_Info] "
        "WHERE Zone_ID = '" + zone + "' "
        "OR Zone_ID LIKE '" + zone + ",%' "
        "OR Zone_ID LIKE '%," + zone + ",%' "
        "OR Zone_ID LIKE '%," + zone + "'";

    std::vector<ReaderItem> result;

    const int selectRet = centraldb->SQLSelect(selectSql, &result, true);

    if (selectRet != 0)
    {
        m_remote_db_err_flag.store(1);

        logDbMessage("Download 3Tariff_Info failed.", "DB");

        return -1;
    }

    m_remote_db_err_flag.store(0);

    // Original behavior:
    // no record -> return -1
    if (result.empty())
    {
        return -1;
    }

    int downloadCount = 0;

    // =========================================================
    // Write records to local DB
    // =========================================================
    for (const auto& row : result)
    {
        tariff_info_struct tariffInfo;

        tariffInfo.rate_type = row.GetDataItem(1);
        tariffInfo.day_type = row.GetDataItem(2);
        tariffInfo.time_from = row.GetDataItem(3);
        tariffInfo.time_till = row.GetDataItem(4);
        tariffInfo.t3_start = row.GetDataItem(5);
        tariffInfo.t3_block = row.GetDataItem(6);
        tariffInfo.t3_rate = row.GetDataItem(7);

        const int writeRet = write3tariffinfo2local(tariffInfo);

        if (writeRet == 0)
        {
            ++downloadCount;
        }
    }

    logDbMessage(
        "Downloading 3Tariff_Info Records: End, "
        "Total Record :" +
            std::to_string(result.size()) +
        " ,Downloaded Record :" +
            std::to_string(downloadCount),
        "DB");

    return downloadCount;
}

int db::write3tariffinfo2local(const tariff_info_struct& tariffInfo)
{
    try
    {
        const std::string sqlStmt =
            "INSERT INTO 3Tariff_Info "
            "(Rate_Type, Day_Type, Time_From, Time_Till, "
            "T3_Start, T3_Block, T3_Rate) "
            "VALUES (" +
            tariffInfo.rate_type + ", '" +
            tariffInfo.day_type + "', '" +
            tariffInfo.time_from + "', '" +
            tariffInfo.time_till + "', " +
            tariffInfo.t3_start + ", " +
            tariffInfo.t3_block + ", " +
            tariffInfo.t3_rate + ")";

        const int ret = localdb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Insert 3Tariff_Info to local failed.", "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        return ret;
    }
    catch (const std::exception& e)
    {
        m_local_db_err_flag = 1;

        logDbMessage(std::string("Local DB error in writing 3Tariff_Info: ") + e.what(), "DB");

        return -1;
    }
}

int db::downloadratefreeinfo(int iCheckStatus)
{
    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return -1;
    }

    const int stationId = data->gtStation.iSID;

    const int zoneId = data->gtStation.iZoneID;

    const std::string fetchedColumn = "s" + std::to_string(stationId) + "_fetched";

    // =========================================================
    // Check whether Rate_Free_Info download is required
    // =========================================================
    if (iCheckStatus == 1)
    {
        const std::string checkSql =
            "SELECT name FROM parameter_mst "
            "WHERE name = 'DownloadRateFreeInfo' "
            "AND " +
            fetchedColumn +
            " = 0";

        std::vector<ReaderItem> checkResult;

        const int checkRet = centraldb->SQLSelect(checkSql, &checkResult, true);

        if (checkRet != 0)
        {
            m_remote_db_err_flag.store(1);

            logDbMessage("Download Rate_Free_Info failed.", "DB");

            return -1;
        }

        if (checkResult.empty())
        {
            m_remote_db_err_flag.store(0);

            logDbMessage("Rate_Free_Info already downloaded.", "DB");

            return -3;
        }
    }

    // =========================================================
    // Clear existing local Rate_Free_Info
    // =========================================================
    const int deleteRet = localdb->SQLExecutNoneQuery("DELETE FROM Rate_Free_Info");

    if (deleteRet != 0)
    {
        m_local_db_err_flag = 1;

        logDbMessage("Delete Rate_Free_Info from local failed.", "DB");

        return -1;
    }

    m_local_db_err_flag = 0;

    // =========================================================
    // Download Rate_Free_Info for current zone
    // =========================================================
    logDbMessage("Download Rate_Free_Info.", "DB");

    const std::string zone = std::to_string(zoneId);

    const std::string selectSql =
        "SELECT Rate_Type, Day_Type, Init_Free, "
        "Free_Beg, Free_End, Free_Time "
        "FROM Rate_Free_Info "
        "WHERE Zone_ID = '" + zone + "' "
        "OR Zone_ID LIKE '" + zone + ",%' "
        "OR Zone_ID LIKE '%," + zone + ",%' "
        "OR Zone_ID LIKE '%," + zone + "'";

    std::vector<ReaderItem> result;

    const int selectRet = centraldb->SQLSelect(selectSql, &result, true);

    if (selectRet != 0)
    {
        m_remote_db_err_flag.store(1);

        logDbMessage("Download Rate_Free_Info failed.", "DB");

        return -1;
    }

    m_remote_db_err_flag.store(0);

    int downloadCount = 0;

    // =========================================================
    // Write records to Local DB
    // =========================================================
    for (const auto& row : result)
    {
        rate_free_info_struct rateFreeInfo;

        rateFreeInfo.rate_type = row.GetDataItem(0);
        rateFreeInfo.day_type = row.GetDataItem(1);
        rateFreeInfo.init_free = row.GetDataItem(2);
        rateFreeInfo.free_beg = row.GetDataItem(3);
        rateFreeInfo.free_end = row.GetDataItem(4);
        rateFreeInfo.free_time = row.GetDataItem(5);

        const int writeRet = writeratefreeinfo2local(rateFreeInfo);

        if (writeRet == 0)
        {
            ++downloadCount;
        }
    }

    if (!result.empty())
    {
        logDbMessage(
            "Downloading Rate_Free_Info Records: End, "
            "Total Record :" +
                std::to_string(result.size()) +
            " ,Downloaded Record :" +
                std::to_string(downloadCount),
            "DB");
    }

    // =========================================================
    // Mark DownloadRateFreeInfo as fetched
    // =========================================================
    if (iCheckStatus == 1)
    {
        const std::string updateSql =
            "UPDATE parameter_mst SET " +
            fetchedColumn +
            " = 1 "
            "WHERE name = 'DownloadRateFreeInfo'";

        const int updateRet = centraldb->SQLExecutNoneQuery(updateSql);

        if (updateRet != 0)
        {
            m_remote_db_err_flag.store(1);

            logDbMessage("Set DownloadRateFreeInfo fetched = 1 failed.", "DB");
        }
    }

    // Preserve original behavior
    if (result.empty())
    {
        return -1;
    }

    return downloadCount;
}

int db::writeratefreeinfo2local(const rate_free_info_struct& rateFreeInfo)
{
    try
    {
        const std::string sqlStmt =
            "INSERT INTO Rate_Free_Info "
            "(Rate_Type, Day_Type, Init_Free, Free_Beg, Free_End, Free_Time) "
            "VALUES (" +
            rateFreeInfo.rate_type + ", '" +
            rateFreeInfo.day_type + "', " +
            rateFreeInfo.init_free + ", '" +
            rateFreeInfo.free_beg + "', '" +
            rateFreeInfo.free_end + "', " +
            rateFreeInfo.free_time + ")";

        const int ret = localdb->SQLExecutNoneQuery( sqlStmt);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Insert Rate_Free_Info to local failed.", "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        return ret;
    }
    catch (const std::exception& e)
    {
        m_local_db_err_flag = 1;

        logDbMessage(std::string("Local DB error in writing Rate_Free_Info: ") + e.what(), "DB");

        return -1;
    }
}

int db::downloadspecialdaymst(int iCheckStatus)
{
    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return -1;
    }

    const int stationId = data->gtStation.iSID;

    const int zoneId = data->gtStation.iZoneID;

    const std::string fetchedColumn = "s" + std::to_string(stationId) + "_fetched";

    // =========================================================
    // Check whether Special_Day_mst download is required
    // =========================================================
    if (iCheckStatus == 1)
    {
        const std::string checkSql =
            "SELECT name FROM parameter_mst "
            "WHERE name = 'DownloadSpecialDay' "
            "AND " +
            fetchedColumn +
            " = 0";

        std::vector<ReaderItem> checkResult;

        const int checkRet = centraldb->SQLSelect(checkSql, &checkResult, true);

        if (checkRet != 0)
        {
            m_remote_db_err_flag.store(1);

            logDbMessage("Download Special_Day_mst failed.", "DB");

            return -1;
        }

        if (checkResult.empty())
        {
            m_remote_db_err_flag.store(0);

            logDbMessage("Special_Day_mst already downloaded.", "DB");

            return -3;
        }
    }

    // =========================================================
    // Clear existing local Special_Day_mst
    // =========================================================
    const int deleteRet = localdb->SQLExecutNoneQuery("DELETE FROM Special_Day_mst");

    if (deleteRet != 0)
    {
        m_local_db_err_flag = 1;

        logDbMessage("Delete Special_Day_mst from local failed.", "DB");

        return -1;
    }

    m_local_db_err_flag = 0;

    // =========================================================
    // Download special day records for current zone
    // =========================================================
    logDbMessage("Download Special_Day_mst.", "DB");

    const std::string zone = std::to_string(zoneId);

    const std::string selectSql =
        "SELECT "
        "CONVERT(char(10), Special_Date, 103), "
        "Rate_Type, "
        "Day_Code "
        "FROM Special_Day_mst "
        "WHERE Special_Date > GETDATE() - 1 "
        "AND ("
        "Zone_ID = '" + zone + "' "
        "OR Zone_ID LIKE '" + zone + ",%' "
        "OR Zone_ID LIKE '%," + zone + ",%' "
        "OR Zone_ID LIKE '%," + zone + "'"
        ")";

    std::vector<ReaderItem> result;

    const int selectRet = centraldb->SQLSelect(selectSql, &result, true);

    if (selectRet != 0)
    {
        m_remote_db_err_flag.store(1);

        logDbMessage("Download Special_Day_mst failed.", "DB");

        return -1;
    }

    m_remote_db_err_flag.store(0);

    int downloadCount = 0;

    // =========================================================
    // Write records to Local DB
    // =========================================================
    for (const auto& row : result)
    {
        const std::string specialDate = row.GetDataItem(0);
        const std::string rateType = row.GetDataItem(1);
        const std::string dayCode = row.GetDataItem(2);

        const int writeRet = writespecialday2local(specialDate, rateType, dayCode);

        if (writeRet == 0)
        {
            ++downloadCount;
        }
    }

    if (!result.empty())
    {
        logDbMessage(
            "Downloading Special_Day_mst Records: End, "
            "Total Record :" +
                std::to_string(result.size()) +
            " ,Downloaded Record :" +
                std::to_string(downloadCount),
            "DB");
    }

    // =========================================================
    // Mark DownloadSpecialDay as fetched
    // =========================================================
    if (iCheckStatus == 1)
    {
        const std::string updateSql =
            "UPDATE parameter_mst SET " +
            fetchedColumn +
            " = 1 "
            "WHERE name = 'DownloadSpecialDay'";

        const int updateRet = centraldb->SQLExecutNoneQuery(updateSql);

        if (updateRet != 0)
        {
            m_remote_db_err_flag.store(1);

            logDbMessage("Set DownloadSpecialDay fetched=1 failed.", "DB");
        }
    }

    // Preserve original behavior
    if (result.empty())
    {
        return -1;
    }

    return downloadCount;
}

int db::writespecialday2local(const std::string& specialDate, const std::string& rateType, const std::string& dayCode)
{
    try
    {
        const std::string sqlStmt =
            "INSERT INTO Special_Day_mst "
            "(Special_Date, Rate_Type, Day_Code) "
            "VALUES ('" +
            specialDate + "', " +
            rateType + ", '" +
            dayCode + "')";

        const int ret = localdb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Insert Special_Day_mst to local failed.", "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        return ret;
    }
    catch (const std::exception& e)
    {
        m_local_db_err_flag = 1;

        logDbMessage(std::string("Local DB error in writing Special_Day_mst: ") + e.what(), "DB");

        return -1;
    }
}

int db::downloadratetypeinfo(int iCheckStatus)
{
    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return -1;
    }

    const int stationId = data->gtStation.iSID;

    const int zoneId = data->gtStation.iZoneID;

    const std::string fetchedColumn = "s" + std::to_string(stationId) + "_fetched";

    // =========================================================
    // Check whether Rate_Type_Info download is required
    // =========================================================
    if (iCheckStatus == 1)
    {
        const std::string checkSql =
            "SELECT name FROM parameter_mst "
            "WHERE name = 'DownloadRateTypeInfo' "
            "AND " +
            fetchedColumn +
            " = 0";

        std::vector<ReaderItem> checkResult;

        const int checkRet = centraldb->SQLSelect(checkSql, &checkResult, true);

        if (checkRet != 0)
        {
            m_remote_db_err_flag.store(1);

            logDbMessage("Download Rate_Type_Info failed.", "DB");

            return -1;
        }

        if (checkResult.empty())
        {
            m_remote_db_err_flag.store(0);

            logDbMessage("Rate_Type_Info already downloaded.", "DB");

            return -3;
        }
    }

    // =========================================================
    // Clear existing local Rate_Type_Info
    // =========================================================
    const int deleteRet = localdb->SQLExecutNoneQuery( "DELETE FROM Rate_Type_Info");

    if (deleteRet != 0)
    {
        m_local_db_err_flag = 1;

        logDbMessage("Delete Rate_Type_Info from local failed.", "DB");

        return -1;
    }

    m_local_db_err_flag = 0;

    // =========================================================
    // Download Rate_Type_Info for current zone
    // =========================================================
    logDbMessage("Download Rate_Type_Info.", "DB");

    const std::string zone = std::to_string(zoneId);

    const std::string selectSql =
        "SELECT "
        "Rate_Type, "
        "Has_Holiday, "
        "Has_Holiday_Eve, "
        "Has_Special_Day, "
        "Has_Init_Free, "
        "Has_3Tariff, "
        "Has_Zone_Max "
        "FROM Rate_Type_Info "
        "WHERE Zone_ID = '" + zone + "' "
        "OR Zone_ID LIKE '" + zone + ",%' "
        "OR Zone_ID LIKE '%," + zone + ",%' "
        "OR Zone_ID LIKE '%," + zone + "'";

    std::vector<ReaderItem> result;

    const int selectRet = centraldb->SQLSelect(selectSql, &result, true);

    if (selectRet != 0)
    {
        m_remote_db_err_flag.store(1);

        logDbMessage("Download Rate_Type_Info failed.", "DB");

        return -1;
    }

    m_remote_db_err_flag.store(0);

    int downloadCount = 0;

    // =========================================================
    // Write records to Local DB
    // =========================================================
    for (const auto& row : result)
    {
        rate_type_info_struct rateTypeInfo;

        rateTypeInfo.rate_type = row.GetDataItem(0);
        rateTypeInfo.has_holiday = row.GetDataItem(1);
        rateTypeInfo.has_holiday_eve = row.GetDataItem(2);
        rateTypeInfo.has_special_day = row.GetDataItem(3);
        rateTypeInfo.has_init_free = row.GetDataItem(4);
        rateTypeInfo.has_3tariff = row.GetDataItem(5);
        rateTypeInfo.has_zone_max = row.GetDataItem(6);

        const int writeRet = writeratetypeinfo2local(rateTypeInfo);

        if (writeRet == 0)
        {
            ++downloadCount;
        }
    }

    if (!result.empty())
    {
        logDbMessage(
            "Downloading Rate_Type_Info Records: End, "
            "Total Record :" +
                std::to_string(result.size()) +
            " ,Downloaded Record :" +
                std::to_string(downloadCount),
            "DB");
    }

    // =========================================================
    // Mark DownloadRateTypeInfo as fetched
    // =========================================================
    if (iCheckStatus == 1)
    {
        const std::string updateSql =
            "UPDATE parameter_mst SET " +
            fetchedColumn +
            " = 1 "
            "WHERE name = 'DownloadRateTypeInfo'";

        const int updateRet = centraldb->SQLExecutNoneQuery(updateSql);

        if (updateRet != 0)
        {
            m_remote_db_err_flag.store(1);

            logDbMessage("Set DownloadRateTypeInfo fetched = 1 failed.", "DB");
        }
    }

    // Preserve original behavior
    if (result.empty())
    {
        return -1;
    }

    return downloadCount;
}

int db::writeratetypeinfo2local(const rate_type_info_struct& rateTypeInfo)
{
    try
    {
        const std::string sqlStmt =
            "INSERT INTO Rate_Type_Info "
            "(Rate_Type, Has_Holiday, Has_Holiday_Eve, "
            "Has_Special_Day, Has_Init_Free, Has_3Tariff, Has_Zone_Max) "
            "VALUES (" +
            rateTypeInfo.rate_type + ", " +
            rateTypeInfo.has_holiday + ", " +
            rateTypeInfo.has_holiday_eve + ", " +
            rateTypeInfo.has_special_day + ", " +
            rateTypeInfo.has_init_free + ", " +
            rateTypeInfo.has_3tariff + ", " +
            rateTypeInfo.has_zone_max + ")";

        const int ret = localdb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Insert Rate_Type_Info to local failed.", "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        return ret;
    }
    catch (const std::exception& e)
    {
        m_local_db_err_flag = 1;

        logDbMessage(std::string("Local DB error in writing Rate_Type_Info: ") + e.what(), "DB");

        return -1;
    }
}

int db::downloadratemaxinfo(int iCheckStatus)
{
    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return -1;
    }

    const int stationId = data->gtStation.iSID;

    const int zoneId = data->gtStation.iZoneID;

    const std::string fetchedColumn = "s" + std::to_string(stationId) + "_fetched";

    // =========================================================
    // Check whether Rate_Max_Info download is required
    // =========================================================
    if (iCheckStatus == 1)
    {
        const std::string checkSql =
            "SELECT name FROM parameter_mst "
            "WHERE name = 'DownloadRateMaxInfo' "
            "AND " +
            fetchedColumn +
            " = 0";

        std::vector<ReaderItem> checkResult;

        const int checkRet = centraldb->SQLSelect(checkSql, &checkResult, true);

        if (checkRet != 0)
        {
            m_remote_db_err_flag.store(1);

            logDbMessage("Download Rate_Max_Info failed.", "DB");

            return -1;
        }

        if (checkResult.empty())
        {
            m_remote_db_err_flag.store(0);

            logDbMessage("Rate_Max_Info already downloaded.", "DB");

            return -3;
        }
    }

    // =========================================================
    // Clear existing local Rate_Max_Info
    // =========================================================
    const int deleteRet = localdb->SQLExecutNoneQuery("DELETE FROM Rate_Max_Info");

    if (deleteRet != 0)
    {
        m_local_db_err_flag = 1;

        logDbMessage("Delete Rate_Max_Info from local failed.", "DB");

        return -1;
    }

    m_local_db_err_flag = 0;

    // =========================================================
    // Download Rate_Max_Info for current zone
    // =========================================================
    logDbMessage("Download Rate_Max_Info.", "DB");

    const std::string zone = std::to_string(zoneId);

    const std::string selectSql =
        "SELECT "
        "Rate_Type, "
        "Day_Type, "
        "Start_Time, "
        "End_Time, "
        "Max_Fee "
        "FROM Rate_Max_Info "
        "WHERE Zone_ID = '" + zone + "' "
        "OR Zone_ID LIKE '" + zone + ",%' "
        "OR Zone_ID LIKE '%," + zone + ",%' "
        "OR Zone_ID LIKE '%," + zone + "'";

    std::vector<ReaderItem> result;

    const int selectRet = centraldb->SQLSelect(selectSql, &result, true);

    if (selectRet != 0)
    {
        m_remote_db_err_flag.store(1);

        logDbMessage("Download Rate_Max_Info failed.", "DB");

        return -1;
    }

    m_remote_db_err_flag.store(0);

    int downloadCount = 0;

    // =========================================================
    // Write records to Local DB
    // =========================================================
    for (const auto& row : result)
    {
        rate_max_info_struct rateMaxInfo;

        rateMaxInfo.rate_type = row.GetDataItem(0);
        rateMaxInfo.day_type = row.GetDataItem(1);
        rateMaxInfo.start_time = row.GetDataItem(2);
        rateMaxInfo.end_time = row.GetDataItem(3);
        rateMaxInfo.max_fee = row.GetDataItem(4);

        const int writeRet = writeratemaxinfo2local(rateMaxInfo);

        if (writeRet == 0)
        {
            ++downloadCount;
        }
    }

    if (!result.empty())
    {
        logDbMessage(
            "Downloading Rate_Max_Info Records: End, "
            "Total Record :" +
                std::to_string(result.size()) +
            " ,Downloaded Record :" +
                std::to_string(downloadCount),
            "DB");
    }

    // =========================================================
    // Mark DownloadRateMaxInfo as fetched
    // =========================================================
    if (iCheckStatus == 1)
    {
        const std::string updateSql =
            "UPDATE parameter_mst SET " +
            fetchedColumn +
            " = 1 "
            "WHERE name = 'DownloadRateMaxInfo'";

        const int updateRet = centraldb->SQLExecutNoneQuery(updateSql);

        if (updateRet != 0)
        {
            m_remote_db_err_flag.store(1);

            logDbMessage("Set DownloadRateMaxInfo fetched = 1 failed.", "DB");
        }
    }

    // Preserve original behavior
    if (result.empty())
    {
        return -1;
    }

    return downloadCount;
}

int db::writeratemaxinfo2local(const rate_max_info_struct& rateMaxInfo)
{
    try
    {
        const std::string sqlStmt =
            "INSERT INTO Rate_Max_Info "
            "(Rate_Type, Day_Type, Start_Time, End_Time, Max_Fee) "
            "VALUES (" +
            rateMaxInfo.rate_type + ", '" +
            rateMaxInfo.day_type + "', '" +
            rateMaxInfo.start_time + "', '" +
            rateMaxInfo.end_time + "', " +
            rateMaxInfo.max_fee + ")";

        const int ret = localdb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Insert Rate_Max_Info to local failed.", "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        return ret;
    }
    catch (const std::exception& e)
    {
        m_local_db_err_flag = 1;

        logDbMessage(std::string("Local DB error in writing Rate_Max_Info: ") + e.what(), "DB");

        return -1;
    }
}

int db::downloadTR()
{
    const std::string filter =
        "TRType = 1 "
        "OR TRType = 2 "
        "OR TRType = 6 "
        "OR TRType = 11 "
        "OR TRType = 12";

    // =========================================================
    // Get total TR count
    // =========================================================
    const std::string countSql =
        "SELECT COUNT(*) "
        "FROM TR_mst "
        "WHERE " + filter;

    std::vector<ReaderItem> countResult;

    const int countRet = centraldb->SQLSelect(countSql, &countResult, false);

    if (countRet != 0)
    {
        m_remote_db_err_flag.store(1);

        logDbMessage("Download TR fail.", "DB");

        return -1;
    }

    m_remote_db_err_flag.store(0);

    if (countResult.empty())
    {
        logDbMessage("Unable to retrieve TR count.", "DB");

        return -1;
    }

    logDbMessage("Total " + countResult.front().GetDataItem(0) + " TR type to be download.", "DB");

    // =========================================================
    // Download TR records
    // =========================================================
    const std::string selectSql =
        "SELECT "
        "TRType, "
        "Line_no, "
        "Enabled, "
        "LineText, "
        "LineVar, "
        "LineFont, "
        "LineAlign "
        "FROM TR_mst "
        "WHERE " + filter;

    std::vector<ReaderItem> result;

    const int selectRet = centraldb->SQLSelect(selectSql,  &result, true);

    if (selectRet != 0)
    {
        m_remote_db_err_flag.store(1);

        logDbMessage("Download TR fail.", "DB");

        return -1;
    }

    m_remote_db_err_flag.store(0);

    int downloadCount = 0;

    if (!result.empty())
    {
        logDbMessage("Downloading TR " + std::to_string(result.size()) + " Records: Started", "DB");

        for (const auto& row : result)
        {
            const int trType = std::stoi(row.GetDataItem(0));
            const int lineNo = std::stoi(row.GetDataItem(1));
            const int enabled = std::stoi(row.GetDataItem(2));
            const std::string lineText = row.GetDataItem(3);
            const std::string lineVar = row.GetDataItem(4);
            const int lineFont = std::stoi(row.GetDataItem(5));
            const int lineAlign = std::stoi(row.GetDataItem(6));

            const int writeRet =
                writetr2local(trType, lineNo, enabled, lineText, lineVar, lineFont, lineAlign);

            if (writeRet == 0)
            {
                ++downloadCount;

                // Preserve original behavior
                m_remote_db_err_flag.store(0);
            }
        }

        logDbMessage(
            "Downloading TR Records: End, "
            "Total Record :" +
                std::to_string(result.size()) +
            " ,Downloaded Record :" +
                std::to_string(downloadCount),
            "DB");
    }

    return downloadCount;
}

int db::writetr2local(int trType, int lineNo, int enabled, const std::string& lineText, const std::string& lineVar, int lineFont, int lineAlign)
{
    try
    {
        // =====================================================
        // Check whether TR record already exists
        // =====================================================
        const std::string checkSql =
            "SELECT * FROM TR_mst "
            "WHERE TRType = " +
            std::to_string(trType) +
            " AND Line_no = " +
            std::to_string(lineNo);

        std::vector<ReaderItem> result;

        int ret = localdb->SQLSelect(checkSql, &result, false);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Update local TR failed.", "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        // =====================================================
        // Existing record -> UPDATE
        // =====================================================
        if (!result.empty())
        {
            const std::string sqlStmt =
                "UPDATE TR_mst SET "
                "TRType = " + std::to_string(trType) + ", "
                "Line_no = " + std::to_string(lineNo) + ", "
                "Enabled = " + std::to_string(enabled) + ", "
                "LineText = '" + lineText + "', "
                "LineVar = '" + lineVar + "', "
                "LineFont = " + std::to_string(lineFont) + ", "
                "LineAlign = " + std::to_string(lineAlign) + " "
                "WHERE TRType = " + std::to_string(trType) +
                " AND Line_no = " + std::to_string(lineNo);

            ret = localdb->SQLExecutNoneQuery(sqlStmt);

            if (ret != 0)
            {
                m_local_db_err_flag = 1;

                logDbMessage("Update local TR failed.", "DB");

                return ret;
            }

            m_local_db_err_flag = 0;

            return ret;
        }

        // =====================================================
        // New record -> INSERT
        // =====================================================
        const std::string sqlStmt =
            "INSERT INTO TR_mst "
            "(TRType, Line_no, Enabled, LineText, LineVar, "
            "LineFont, LineAlign) "
            "VALUES (" +
            std::to_string(trType) + ", " +
            std::to_string(lineNo) + ", " +
            std::to_string(enabled) + ", '" +
            lineText + "', '" +
            lineVar + "', " +
            std::to_string(lineFont) + ", " +
            std::to_string(lineAlign) + ")";

        ret = localdb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Insert TR to local failed.", "DB");

            return ret;
        }

        m_local_db_err_flag = 0;

        return ret;
    }
    catch (const std::exception& e)
    {
        m_local_db_err_flag = 1;

        logDbMessage(std::string("Local DB error in writing TR record: ") + e.what(), "DB");

        return -1;
    }
}

DBError db::loadZoneEntriesfromLocal()
{
    auto* op = operation::getInstance();

    const auto data = op->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return iLocalFail;
    }

    const int zoneId = data->gtStation.iZoneID;

    // =========================================================
    // Load entry stations for current zone
    // =========================================================
    const std::string sqlStmt =
        "SELECT StationID "
        "FROM Station_Setup "
        "WHERE StationType = 1 "
        "AND ZoneID = '" +
        std::to_string(zoneId) + "'";

    std::vector<ReaderItem> result;

    const int ret = localdb->SQLSelect(sqlStmt, &result, false);

    if (ret != 0)
    {
        return iLocalFail;
    }

    if (result.empty())
    {
        return iNoData;
    }

    // =========================================================
    // Build ZoneEntries
    // =========================================================
    std::string zoneEntries = ",";

    for (const auto& row : result)
    {
        zoneEntries += row.GetDataItem(0) + ",";
    }

    // =========================================================
    // Update Operation shared data
    // =========================================================
    auto paras = data->tParas;

    paras.gsZoneEntries = zoneEntries;

    OperationSharedDataUpdate update;
    update.tParas = std::move(paras);

    if (!op->FnUpdateSharedData(std::move(update)))
    {
        logDbMessage("Unable to update Operation shared data.", "DB");

        return iLocalFail;
    }

    logDbMessage("Load ZoneEntries from Local: " + zoneEntries, "DB");

    return iDBSuccess;
}

DBError db::loadstationsetup()
{
    try
    {
        auto* op = operation::getInstance();

        const auto data = op->FnGetSharedData();

        if (!data)
        {
            logDbMessage("Unable to get Operation shared data.", "DB");

            return iLocalFail;
        }

        const int stationId = data->gtStation.iSID;

        // =========================================================
        // Load station setup from Local DB
        // =========================================================
        const std::string sqlStmt =
            "SELECT * FROM Station_Setup "
            "WHERE StationId = '" +
            std::to_string(stationId) + "'";

        std::vector<ReaderItem> result;

        const int ret = localdb->SQLSelect(sqlStmt, &result, false);

        if (ret != 0)
        {
            logDbMessage("Get station setup fail.", "DB");

            return iLocalFail;
        }

        if (result.empty())
        {
            return iNoData;
        }

        const auto& row = result.front();

        // =========================================================
        // Update station configuration
        // =========================================================
        auto station = data->gtStation;

        station.iSID = std::stoi(row.GetDataItem(0));
        station.sName = row.GetDataItem(1);

        switch (std::stoi(row.GetDataItem(2)))
        {
            case 1:
                station.iType = tientry;
                break;

            case 2:
                station.iType = tiExit;
                break;

            default:
                break;
        }

        station.iStatus = std::stoi(row.GetDataItem(3));
        station.sPCName = row.GetDataItem(4);
        station.iCHUPort = std::stoi(row.GetDataItem(5));
        station.iAntID = std::stoi(row.GetDataItem(6));
        station.iZoneID = std::stoi(row.GetDataItem(7));
        station.iIsVirtual = std::stoi(row.GetDataItem(8));

        switch (std::stoi(row.GetDataItem(9)))
        {
            case 0:
                station.iSubType = iNormal;
                break;

            case 1:
                station.iSubType = iXwithVENoPay;
                break;

            case 2:
                station.iSubType = iXwithVEPay;
                break;

            default:
                break;
        }

        station.iVirtualID = std::stoi(row.GetDataItem(10));

        // =========================================================
        // Mark station setup as loaded
        // =========================================================
        auto process = data->tProcess;

        process.gbloadedStnSetup = true;

        // =========================================================
        // Update Operation shared data
        // =========================================================
        OperationSharedDataUpdate update;

        update.gtStation = std::move(station);
        update.tProcess = std::move(process);

        if (!op->FnUpdateSharedData(std::move(update)))
        {
            logDbMessage( "Unable to update Operation shared data.", "DB");

            return iLocalFail;
        }

        return iDBSuccess;
    }
    catch (const std::exception& e)
    {
        logDbMessage(std::string("loadstationsetup exception: ") + e.what(), "DB");

        return iLocalFail;
    }
}

DBError db::loadParam()
{
    // Preserve existing behavior
    loadZoneEntriesfromLocal();
    loadparamfromCentral();

    auto* op = operation::getInstance();

    const auto data = op->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return iLocalFail;
    }

    auto paras = data->tParas;
    auto process = data->tProcess;

    // Preserve existing behavior
    paras.giTariffFeeMode = 0;

    // =========================================================
    // Load parameters from Local DB
    // =========================================================
    std::vector<ReaderItem> result;

    const int ret = localdb->SQLSelect("SELECT ParamName, ParamValue FROM Param_mst", &result, true);

    if (ret != 0)
    {
        logDbMessage("Load parameter failed.", "DB");

        return iLocalFail;
    }

    if (result.empty())
    {
        return iNoData;
    }

    // =========================================================
    // Convert parameter name to lowercase
    // =========================================================
    const auto toLower =
        [](std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](unsigned char ch)
                {
                    return static_cast<char>(std::tolower(ch));
                });

            return value;
        };

    // =========================================================
    // Process parameters
    // =========================================================
    for (const auto& row : result)
    {
        if (row.getDataSize() != 2)
        {
            continue;
        }

        const std::string key = row.GetDataItem(0);
        const std::string value = row.GetDataItem(1);

        const std::string normalizedKey = toLower(key);

        try
        {
            if (normalizedKey == "commportantenna")
            {
                paras.giCommPortAntenna = std::stoi(value);
            }
            else if (normalizedKey == "commportlcsc")
            {
                paras.giCommPortLCSC = std::stoi(value);
            }
            else if (normalizedKey == "commportprinter")
            {
                paras.giCommPortPrinter = std::stoi(value);
            }
            else if (normalizedKey == "eps")
            {
                paras.giEPS = std::stoi(value);
            }
            else if (normalizedKey == "carparkcode")
            {
                paras.gscarparkcode = value;
            }
            else if (normalizedKey == "locallcsc")
            {
                paras.gsLocalLCSC = value;
            }
            else if (normalizedKey == "remotelcsc")
            {
                paras.gsRemoteLCSC = value;
            }
            else if (normalizedKey == "remotelcscback")
            {
                paras.gsRemoteLCSCBack = value;
            }
            else if (normalizedKey == "cscrcdffolder")
            {
                paras.gsCSCRcdfFolder = value;
            }
            else if (normalizedKey == "cscrcdackfolder")
            {
                paras.gsCSCRcdackFolder = value;
            }
            else if (normalizedKey == "cpoid")
            {
                paras.gsCPOID = value;
            }
            else if (normalizedKey == "cpid")
            {
                paras.gsCPID = value;
            }
            else if (normalizedKey == "commportled")
            {
                paras.giCommPortLED = std::stoi(value);
            }
            else if (normalizedKey == "hasmcycle")
            {
                paras.giHasMCycle = std::stoi(value);
            }
            else if (normalizedKey == "ticketsiteid")
            {
                paras.giTicketSiteID = std::stoi(value);
            }
            else if (normalizedKey == "datakeepdays")
            {
                paras.giDataKeepDays = std::stoi(value);
            }
            else if (normalizedKey == "barrierpulse")
            {
                paras.gsBarrierPulse = std::stoi(value);
            }
            else if (normalizedKey == "antmaxretry")
            {
                paras.giAntMaxRetry = std::stoi(value);
            }
            else if (normalizedKey == "antminoktimes")
            {
                paras.giAntMinOKTimes = std::stoi(value);
            }
            else if (normalizedKey == "antinqto")
            {
                paras.giAntInqTO = std::stoi(value);
            }
            else if (normalizedKey == "antiiurepetition")
            {
                paras.gbAntiIURepetition = (std::stoi(value) == 1);
            }
            else if (normalizedKey == "commportled401")
            {
                paras.giCommportLED401 = std::stoi(value);
            }
            else if (normalizedKey == "commportreader")
            {
                paras.giCommPortKDEReader = std::stoi(value);
            }
            else if (normalizedKey == "commportcpt")
            {
                paras.giCommPortUPOS = std::stoi(value);
            }
            else if (normalizedKey == "ishdbsite")
            {
                paras.giIsHDBSite = std::stoi(value);
            }
            else if (normalizedKey == "allowedholdertype")
            {
                paras.gsAllowedHolderType = value;
            }
            else if (normalizedKey == "ledmaxchar")
            {
                paras.giLEDMaxChar = std::stoi(value);
            }
            else if (normalizedKey == "alwaystryonline")
            {
                paras.gbAlwaysTryOnline = (std::stoi(value) == 1);
            }
            else if (normalizedKey == "autodebitnoentry")
            {
                paras.gbAutoDebitNoEntry = (std::stoi(value) == 1);
            }
            else if (normalizedKey == "loopahangtime")
            {
                paras.giLoopAHangTime = std::stoi(value);
            }
            else if (normalizedKey == "operationto")
            {
                paras.giOperationTO = std::stoi(value);
            }
            else if (normalizedKey == "fullaction")
            {
                paras.giFullAction = static_cast<eFullAction>(std::stoi(value));
            }
            else if (normalizedKey == "barrieropentoolongtime")
            {
                paras.giBarrierOpenTooLongTime = std::stoi(value);
            }
            else if (normalizedKey == "bitbarrierarmbroken")
            {
                paras.giBitBarrierArmBroken = std::stoi(value);
            }
            else if (normalizedKey == "mccontrolaction")
            {
                paras.giMCControlAction = std::stoi(value);
            }
            else if (normalizedKey == "logbackfolder")
            {
                paras.gsLogBackFolder = value;
            }
            else if (normalizedKey == "logkeepdays")
            {
                paras.giLogKeepDays = std::stoi(value);
            }
            else if (normalizedKey == "dbbackupfolder")
            {
                paras.gsDBBackupFolder = value;
            }
            else if (normalizedKey == "maxsendofflineno")
            {
                paras.giMaxSendOfflineNo = std::stoi(value);
            }
            else if (normalizedKey == "maxlocaldbsize")
            {
                paras.glMaxLocalDBSize = std::stol(value);
            }
            else if (normalizedKey == "maxtransinterval")
            {
                paras.giMaxTransInterval = std::stol(value);
            }
            else if (normalizedKey == "noiuretry")
            {
                paras.giNoIURetry = std::stoi(value);
            }
            else if (normalizedKey == "maxdiffiu")
            {
                paras.giMaxDiffIU = std::stoi(value);
            }
            else if (normalizedKey == "lockbarrier")
            {
                paras.gbLockBarrier = (std::stoi(value) == 1);
            }
            else if (normalizedKey == "commportled2")
            {
                paras.giCommPortLED2 = std::stoi(value);
            }
            else if (normalizedKey == "chuip")
            {
                paras.gsCHUIP = value;
            }
            else if (normalizedKey == "site")
            {
                paras.gsSite = value;
            }
            else if (normalizedKey == "address")
            {
                paras.gsAddress = value;
            }
            else if (normalizedKey == "trytimes4ne")
            {
                paras.giTryTimes4NE = std::stoi(value);
            }
            else if (normalizedKey == "processreversedcmd")
            {
                paras.giProcessReversedCMD = std::stoi(value);
            }
            else if (normalizedKey == "hasthreewheelmc")
            {
                paras.giHasThreeWheelMC = std::stoi(value);
            }
            else if (normalizedKey == "maxdebitdays")
            {
                paras.giMaxDebitDays = std::stoi(value);
            }
            else if (normalizedKey == "firsthourmode")
            {
                paras.giFirstHourMode = std::stoi(value);
            }
            else if (normalizedKey == "peallowance")
            {
                paras.giPEAllowance = std::stoi(value);
            }
            else if (normalizedKey == "tarifffeemode")
            {
                paras.giTariffFeeMode = std::stoi(value);
            }
            else if (normalizedKey == "tariffgtmode")
            {
                paras.giTariffGTMode = std::stoi(value);
            }
            else if (normalizedKey == "hr2peallowance")
            {
                paras.giHr2PEAllowance = std::stoi(value);
            }
            else if (normalizedKey == "seasoncharge")
            {
                paras.giSeasonCharge = std::stoi(value);
            }
            else if (normalizedKey == "showseasonexpiredays")
            {
                paras.giShowSeasonExpireDays = std::stoi(value);
            }
            else if (normalizedKey == "showexpiredtime")
            {
                paras.giShowExpiredTime = std::stoi(value);
            }
            else if (normalizedKey == "mcycleperday")
            {
                paras.giMCyclePerDay = std::stoi(value);
            }
            else if (normalizedKey == "v3transtype")
            {
                paras.giV3TransType = std::stoi(value);
            }
            else if (normalizedKey == "v4transtype")
            {
                paras.giV4TransType = std::stoi(value);
            }
            else if (normalizedKey == "v5transtype")
            {
                paras.giV5TransType = std::stoi(value);
            }
            else if (normalizedKey == "firsthour")
            {
                paras.giFirstHour = std::stoi(value);
            }
            else if (normalizedKey == "hasholidayeve")
            {
                paras.giHasHolidayEve = std::stoi(value);
            }
            else if (normalizedKey == "hasredemption")
            {
                paras.gbHasRedemption = (std::stoi(value) == 1);
            }
            else if (normalizedKey == "company")
            {
                paras.gsCompany = value;
            }
            else if (normalizedKey == "gstno")
            {
                paras.gsGSTNo = value;
            }
            else if (normalizedKey == "tel")
            {
                paras.gsTel = value;
            }
            else if (normalizedKey == "zip")
            {
                paras.gsZIP = value;
            }
            else if (normalizedKey == "gstrate")
            {
                paras.gfGSTRate = std::stof(value) / 100.0F;

                if (paras.gfGSTRate == 0.0F)
                {
                    paras.gfGSTRate = 0.09F;
                }
            }
            else if (normalizedKey == "chucnto")
            {
                paras.giCHUCnTO = std::stof(value);
            }
            else if (normalizedKey == "hdrec")
            {
                paras.gsHdRec = value;
            }
            else if (normalizedKey == "hdtk")
            {
                paras.gsHdTk = value;
            }
            else if (normalizedKey == "needcard4complimentary")
            {
                paras.giNeedCard4Complimentary = std::stoi(value);
            }
            else if (normalizedKey == "exitticketredemption")
            {
                paras.giExitTicketRedemption = std::stoi(value);
            }
        }
        catch (const std::invalid_argument& e)
        {
            logDbMessage(
                "Invalid argument for key '" +
                    key +
                    "' with value '" +
                    value +
                    "': " +
                    e.what(),
                "DB");
        }
        catch (const std::out_of_range& e)
        {
            logDbMessage(
                "Out of range for key '" +
                    key +
                    "' with value '" +
                    value +
                    "': " +
                    e.what(),
                "DB");
        }
        catch (const std::exception& e)
        {
            logDbMessage(
                "Error processing key '" +
                    key +
                    "' with value '" +
                    value +
                    "': " +
                    e.what(),
                "DB");
        }
    }

    // =========================================================
    // Mark parameters as loaded
    // =========================================================
    process.gbloadedParam = true;

    // =========================================================
    // Update Operation shared data
    // =========================================================
    OperationSharedDataUpdate update;

    update.tParas = std::move(paras);
    update.tProcess = std::move(process);

    if (!op->FnUpdateSharedData(std::move(update)))
    {
        logDbMessage("Unable to update Operation shared data.", "DB");

        return iLocalFail;
    }

    return iDBSuccess;
}

DBError db::loadparamfromCentral()
{
    auto* op = operation::getInstance();

    const auto data = op->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return iCentralFail;
    }

    auto station = data->gtStation;

    auto paras = data->tParas;

    logDbMessage("Load parameter from Central DB.", "DB");

    // =========================================================
    // Load Group ID and Site ID
    // =========================================================
    std::vector<ReaderItem> siteResult;

    const int siteRet = centraldb->SQLSelect("SELECT group_id, site_id FROM site_setup", &siteResult, true);

    if (siteRet != 0)
    {
        return iCentralFail;
    }

    if (!siteResult.empty())
    {
        const auto& row = siteResult.front();

        try
        {
            paras.giGroupID = std::stoi(row.GetDataItem(0));
            paras.giSite = std::stoi(row.GetDataItem(1));
        }
        catch (const std::exception& e)
        {
            logDbMessage(std::string("Unable to parse Group/Site ID: ") + e.what(), "DB");

            return iCentralFail;
        }

        logDbMessage("Load Group ID: " + std::to_string(paras.giGroupID), "DB");
        logDbMessage("Load Site ID: " + std::to_string(paras.giSite), "DB");

        OperationSharedDataUpdate update;
        update.tParas = paras;

        if (!op->FnUpdateSharedData(std::move(update)))
        {
            logDbMessage("Unable to update Operation shared data.", "DB");

            return iCentralFail;
        }
    }

    // =========================================================
    // Load Zone Entries and Total Lots
    // =========================================================
    const std::string zoneSql =
        "SELECT entry_station, total_lots "
        "FROM counter_definition "
        "WHERE zone_id = " +
        std::to_string(station.iZoneID);

    std::vector<ReaderItem> zoneResult;

    const int zoneRet = centraldb->SQLSelect(zoneSql, &zoneResult, true);

    if (zoneRet != 0)
    {
        return iCentralFail;
    }

    if (!zoneResult.empty())
    {
        const auto& row = zoneResult.front();

        paras.gsZoneEntries =  "," + row.GetDataItem(0) + ",";

        try
        {
            station.iZoneLots = std::stoi(row.GetDataItem(1));
        }
        catch (const std::exception& e)
        {
            logDbMessage(std::string("Unable to parse Zone Total Lots: ") + e.what(), "DB");

            return iCentralFail;
        }

        logDbMessage("Load zone for entry: " + paras.gsZoneEntries, "DB");
        logDbMessage("Load Zone Total lots: " + std::to_string(station.iZoneLots), "DB");

        OperationSharedDataUpdate update;

        update.gtStation = station;
        update.tParas = paras;

        if (!op->FnUpdateSharedData(std::move(update)))
        {
            logDbMessage("Unable to update Operation shared data.", "DB");

            return iCentralFail;
        }
    }

    // =========================================================
    // Load last receipt number
    // =========================================================
    const std::string receiptSql =
        "SELECT MAX(receipt_no) "
        "FROM exit_trans "
        "WHERE station_id = " +
        std::to_string(station.iSID) +
        " AND receipt_no <> ''";

    std::vector<ReaderItem> receiptResult;

    const int receiptRet = centraldb->SQLSelect(receiptSql, &receiptResult, true);

    if (receiptRet != 0)
    {
        return iCentralFail;
    }

    if (receiptResult.empty())
    {
        logDbMessage("Load Last Receipt No: NULL", "DB");

        return iNoData;
    }

    const std::string receiptNo = receiptResult.front().GetDataItem(0);

    if (receiptNo.empty() ||
        receiptNo == "NULL")
    {
        logDbMessage("Load Last Receipt No: NULL", "DB");

        return iNoData;
    }

    // =========================================================
    // Remove Station ID from receipt number
    // =========================================================
    const std::string stationId = std::to_string(station.iSID);

    if (receiptNo.length() <= stationId.length())
    {
        logDbMessage("Invalid receipt number: " + receiptNo, "DB");

        return iCentralFail;
    }

    const std::size_t serialLength = receiptNo.length() - stationId.length();
    const std::string serialNo = receiptNo.substr(0, serialLength);

    logDbMessage("Load Last Receipt No: " + serialNo, "DB");

    try
    {
        const long lastSerialNo = std::stol(serialNo);

        if (!updateOperationProcess(
                [lastSerialNo](
                    tProcess_Struct& process)
                {
                    process.glLastSerialNo = lastSerialNo;
                }))
        {
            logDbMessage("Unable to update Operation shared data.", "DB");

            return iCentralFail;
        }
    }
    catch (const std::exception& e)
    {
        logDbMessage(std::string("Unable to parse Last Receipt No: ") + e.what(), "DB");

        return iCentralFail;
    }

    return iDBSuccess;
}

DBError db::loadvehicletype()
{
    try
    {
        // =====================================================
        // Load vehicle types from Local DB
        // =====================================================
        std::vector<ReaderItem> result;

        const int ret = localdb->SQLSelect("SELECT IUCode, TransType FROM Vehicle_type", &result, true);

        if (ret != 0)
        {
            logDbMessage("Load Trans Type failed.", "DB");

            return iLocalFail;
        }

        if (result.empty())
        {
            return iNoData;
        }

        // =====================================================
        // Get Operation shared data
        // =====================================================
        auto* op = operation::getInstance();

        const auto data = op->FnGetSharedData();

        if (!data)
        {
            logDbMessage("Unable to get Operation shared data.", "DB");

            return iLocalFail;
        }

        auto vehicleTypes = data->tVType;
        auto process = data->tProcess;

        // =====================================================
        // Load vehicle types
        // =====================================================
        for (const auto& row : result)
        {
            if (row.getDataSize() != 2)
            {
                continue;
            }

            vehicleTypes.push_back(
                {
                    std::stoi(row.GetDataItem(0)),
                    std::stoi(row.GetDataItem(1))
                });
        }

        process.gbloadedVehtype = true;

        // =====================================================
        // Update Operation shared data
        // =====================================================
        OperationSharedDataUpdate update;

        update.tVType = std::move(vehicleTypes);
        update.tProcess = std::move(process);

        if (!op->FnUpdateSharedData(std::move(update)))
        {
            logDbMessage("Unable to update Operation shared data.", "DB");

            return iLocalFail;
        }

        return iDBSuccess;
    }
    catch (const std::exception& e)
    {
        logDbMessage(std::string("loadvehicletype exception: ") + e.what(), "DB");

        return iLocalFail;
    }
}

int db::FnGetVehicleType(const std::string& iuCode)
{
    constexpr int kDefaultVehicleType = 1;

    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return kDefaultVehicleType;
    }

    try
    {
        const int iuCodeValue = std::stoi(iuCode);

        for (const auto& item : data->tVType)
        {
            if (item.iIUCode == iuCodeValue)
            {
                return item.iType;
            }
        }
    }
    catch (const std::exception& e)
    {
        logDbMessage("Invalid IUCode '" + iuCode + "': " + e.what(), "DB");
    }

    return kDefaultVehicleType;
}

DBError db::loadEntrymessage(const std::vector<ReaderItem>& selResult)
{
    auto* op = operation::getInstance();

    const auto data = op->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return iLocalFail;
    }

    if (selResult.empty())
    {
        return iNoData;
    }

    auto message = data->tMsg;
    auto process = data->tProcess;

    // =========================================================
    // Message helpers
    // =========================================================
    const auto setDefaultMessage =
        [](auto& target, const std::string& value)
        {
            target[0] = value;
            target[1] = value;
        };

    const auto setLcdMessage =
        [](auto& target, const std::string& value)
        {
            if (!value.empty() &&
                boost::algorithm::to_lower_copy(value) != "null")
            {
                target[1] = value;
            }
        };

    // =========================================================
    // Process message records
    // =========================================================
    for (const auto& row : selResult)
    {
        if (row.getDataSize() != 2)
        {
            continue;
        }

        const std::string key = boost::algorithm::to_lower_copy(row.GetDataItem(0));

        const std::string value = row.GetDataItem(1);

        // =====================================================
        // Default / LED messages
        // =====================================================
        if (key == "altdefaultled")
        {
            setDefaultMessage(message.Msg_AltDefaultLED, value);
        }
        else if (key == "altdefaultled2")
        {
            setDefaultMessage(message.Msg_AltDefaultLED2, value);
        }
        else if (key == "altdefaultled3")
        {
            setDefaultMessage(message.Msg_AltDefaultLED3, value);
        }
        else if (key == "altdefaultled4")
        {
            setDefaultMessage(message.Msg_AltDefaultLED4, value);
        }
        else if (key == "authorizedvehicle")
        {
            setDefaultMessage(message.Msg_authorizedvehicle, value);
        }

        // =====================================================
        // Card Reading Error
        // =====================================================
        else if (key == "cardreadingerror")
        {
            setDefaultMessage(message.Msg_CardReadingError, value);
        }
        else if (key == "ccardreadingerror")
        {
            setLcdMessage(message.Msg_CardReadingError, value);
        }

        // =====================================================
        // Card Taken
        // =====================================================
        else if (key == "cardtaken")
        {
            setDefaultMessage(message.Msg_CardTaken, value);
        }
        else if (key == "ccardtaken")
        {
            setLcdMessage(message.Msg_CardTaken, value);
        }

        // =====================================================
        // Car Park
        // =====================================================
        else if (key == "carfullled")
        {
            setDefaultMessage(message.Msg_CarFullLED, value);
        }
        else if (key == "carparkfull2led")
        {
            setDefaultMessage(message.Msg_CarParkFull2LED, value);
        }
        else if (key == "dberror")
        {
            setDefaultMessage(message.Msg_DBError, value);
        }

        // =====================================================
        // Default IU
        // =====================================================
        else if (key == "defaultiu")
        {
            setDefaultMessage(message.Msg_DefaultIU, value);
        }
        else if (key == "cdefaultiu")
        {
            setLcdMessage(message.Msg_DefaultIU, value);
        }

        // =====================================================
        // Default LED
        // =====================================================
        else if (key == "defaultled")
        {
            setDefaultMessage(message.Msg_DefaultLED, value);
        }
        else if (key == "cdefaultled")
        {
            setLcdMessage(message.Msg_DefaultLED, value);
        }
        else if (key == "defaultled2")
        {
            setDefaultMessage(message.Msg_DefaultLED2, value);
        }
        else if (key == "cdefaultled2")
        {
            setLcdMessage(message.Msg_DefaultLED2, value);
        }
        else if (key == "defaultmsg2led")
        {
            setDefaultMessage(message.Msg_DefaultMsg2LED, value);
        }
        else if (key == "defaultmsgled")
        {
            setDefaultMessage(message.Msg_DefaultMsgLED, value);
        }

        // =====================================================
        // Enhanced / Season Allowance
        // =====================================================
        else if (key == "eenhancedmcparking")
        {
            setDefaultMessage(message.Msg_EenhancedMCParking, value);
        }
        else if (key == "eseasonwithinallowance")
        {
            setDefaultMessage(message.Msg_ESeasonWithinAllowance, value);
        }
        else if (key == "ceseasonwithinallowance")
        {
            setLcdMessage(message.Msg_ESeasonWithinAllowance, value);
        }
        else if (key == "espt3parking")
        {
            setDefaultMessage(message.Msg_ESPT3Parking, value);
        }
        else if (key == "evipholderparking")
        {
            setDefaultMessage(message.Msg_EVIPHolderParking, value);
        }

        // =====================================================
        // Expiring Season
        // =====================================================
        else if (key == "expiringseason")
        {
            setDefaultMessage(message.Msg_ExpiringSeason, value);
        }
        else if (key == "cexpiringseason")
        {
            setLcdMessage(message.Msg_ExpiringSeason, value);
        }

        // =====================================================
        // Full
        // =====================================================
        else if (key == "fullled")
        {
            setDefaultMessage(message.Msg_FullLED, value);
        }
        else if (key == "cfullled")
        {
            setLcdMessage(message.Msg_FullLED, value);
        }

        // =====================================================
        // Idle
        // =====================================================
        else if (key == "idle")
        {
            setDefaultMessage(message.Msg_Idle, value);
        }
        else if (key == "cidle")
        {
            setLcdMessage(message.Msg_Idle, value);
        }

        // =====================================================
        // Insert Cashcard
        // =====================================================
        else if (key == "insertcashcard")
        {
            setDefaultMessage(message.Msg_InsertCashcard, value);
        }
        else if (key == "cinsertcashcard")
        {
            setLcdMessage(message.Msg_InsertCashcard, value);
        }

        // =====================================================
        // IU Problem
        // =====================================================
        else if (key == "iuproblem")
        {
            setDefaultMessage(message.Msg_IUProblem, value);
        }
        else if (key == "ciuproblem")
        {
            setLcdMessage(message.Msg_IUProblem, value);
        }

        // =====================================================
        // Lock Station
        // =====================================================
        else if (key == "lockstation")
        {
            setDefaultMessage(message.Msg_LockStation, value);
        }
        else if (key == "clockstation")
        {
            setLcdMessage(message.Msg_LockStation, value);
        }

        // =====================================================
        // Loop A
        // =====================================================
        else if (key == "loopa")
        {
            setDefaultMessage(message.Msg_LoopA, value);
        }
        else if (key == "cloopa")
        {
            setLcdMessage(message.Msg_LoopA, value);
        }

        // =====================================================
        // Loop A Full
        // =====================================================
        else if (key == "loopafull")
        {
            setDefaultMessage(message.Msg_LoopAFull, value);
        }
        else if (key == "cloopafull")
        {
            setLcdMessage(message.Msg_LoopAFull, value);
        }

        else if (key == "lorryfullled")
        {
            setDefaultMessage(message.Msg_LorryFullLED, value);
        }
        else if (key == "lotadjustmentmsg")
        {
            setDefaultMessage(message.Msg_LotAdjustmentMsg, value);
        }

        // =====================================================
        // Low Balance
        // =====================================================
        else if (key == "lowbal")
        {
            setDefaultMessage(message.Msg_LowBal, value);
        }
        else if (key == "clowbal")
        {
            setLcdMessage(message.Msg_LowBal, value);
        }

        // =====================================================
        // No IU
        // =====================================================
        else if (key == "noiu")
        {
            setDefaultMessage(message.Msg_NoIU, value);
        }
        else if (key == "cnoiu")
        {
            setLcdMessage(message.Msg_NoIU, value);
        }

        else if (key == "nonightparking2led")
        {
            setDefaultMessage(message.Msg_NoNightParking2LED, value);
        }

        // =====================================================
        // Offline
        // =====================================================
        else if (key == "offline")
        {
            setDefaultMessage(message.Msg_Offline, value);
        }
        else if (key == "coffline")
        {
            setLcdMessage(message.Msg_Offline, value);
        }

        // =====================================================
        // Printer Error
        // =====================================================
        else if (key == "printererror")
        {
            setDefaultMessage(message.Msg_PrinterError, value);
        }
        else if (key == "cprintererror")
        {
            setLcdMessage(message.Msg_PrinterError, value);
        }

        // =====================================================
        // Printing Receipt
        // =====================================================
        else if (key == "printingreceipt")
        {
            setDefaultMessage(message.Msg_PrintingReceipt, value);
        }
        else if (key == "cprintingreceipt")
        {
            setLcdMessage(message.Msg_PrintingReceipt, value);
        }

        // =====================================================
        // Processing
        // =====================================================
        else if (key == "processing")
        {
            setDefaultMessage(message.Msg_Processing, value);
        }
        else if (key == "cprocessing")
        {
            setLcdMessage(message.Msg_Processing, value);
        }

        // =====================================================
        // Reader Comm Error
        // =====================================================
        else if (key == "readercommerror")
        {
            setDefaultMessage(message.Msg_ReaderCommError, value);
        }
        else if (key == "creadercommerror")
        {
            setLcdMessage(message.Msg_ReaderCommError, value);
        }

        // =====================================================
        // Reader Error
        // =====================================================
        else if (key == "readererror")
        {
            setDefaultMessage(message.Msg_ReaderError, value);
        }
        else if (key == "creadererror")
        {
            setLcdMessage(message.Msg_ReaderError, value);
        }

        // =====================================================
        // Same Last IU
        // =====================================================
        else if (key == "samelastiu")
        {
            setDefaultMessage(message.Msg_SameLastIU, value);
        }
        else if (key == "csamelastiu")
        {
            setLcdMessage(message.Msg_SameLastIU, value);
        }

        // =====================================================
        // Scan Entry Ticket
        // =====================================================
        else if (key == "scanentryticket")
        {
            setDefaultMessage(message.Msg_ScanEntryTicket, value);
        }
        else if (key == "cscanentryticket")
        {
            setLcdMessage(message.Msg_ScanEntryTicket, value);
        }

        // =====================================================
        // Scan Validation Ticket
        // =====================================================
        else if (key == "scanvalticket")
        {
            setDefaultMessage(message.Msg_ScanValTicket, value);
        }
        else if (key == "cscanvalticket")
        {
            setLcdMessage(message.Msg_ScanValTicket, value);
        }

        // =====================================================
        // Season As Hourly
        // =====================================================
        else if (key == "seasonashourly")
        {
            setDefaultMessage(message.Msg_SeasonAsHourly, value);
        }
        else if (key == "cseasonashourly")
        {
            setLcdMessage(message.Msg_SeasonAsHourly, value);
        }

        // =====================================================
        // Season Blocked
        // =====================================================
        else if (key == "seasonblocked")
        {
            setDefaultMessage(message.Msg_SeasonBlocked, value);
        }
        else if (key == "cseasonblocked")
        {
            setLcdMessage(message.Msg_SeasonBlocked, value);
        }

        // =====================================================
        // Season Expired
        // =====================================================
        else if (key == "seasonexpired")
        {
            setDefaultMessage(message.Msg_SeasonExpired, value);
        }
        else if (key == "cseasonexpired")
        {
            setLcdMessage(message.Msg_SeasonExpired, value);
        }

        // =====================================================
        // Season Invalid
        // =====================================================
        else if (key == "seasoninvalid")
        {
            setDefaultMessage(message.Msg_SeasonInvalid, value);
        }
        else if (key == "cseasoninvalid")
        {
            setLcdMessage(message.Msg_SeasonInvalid, value);
        }

        // =====================================================
        // Season Multi Found
        // =====================================================
        else if (key == "seasonmultifound")
        {
            setDefaultMessage(message.Msg_SeasonMultiFound, value);
        }
        else if (key == "cseasonmultifound")
        {
            setLcdMessage(message.Msg_SeasonMultiFound, value);
        }

        // =====================================================
        // Season Not Found
        // =====================================================
        else if (key == "seasonnotfound")
        {
            setDefaultMessage(message.Msg_SeasonNotFound, value);
        }
        else if (key == "cseasonnotfound")
        {
            setLcdMessage(message.Msg_SeasonNotFound, value);
        }

        // =====================================================
        // Season Not Start
        // =====================================================
        else if (key == "seasonnotstart")
        {
            setDefaultMessage(message.Msg_SeasonNotStart, value);
        }
        else if (key == "cseasonnotstart")
        {
            setLcdMessage(message.Msg_SeasonNotStart, value);
        }

        // =====================================================
        // Season Not Valid
        // =====================================================
        else if (key == "seasonnotvalid")
        {
            setDefaultMessage(message.Msg_SeasonNotValid, value);
        }
        else if (key == "cseasonnotvalid")
        {
            setLcdMessage(message.Msg_SeasonNotValid, value);
        }

        // =====================================================
        // Season Only
        // =====================================================
        else if (key == "seasononly")
        {
            setDefaultMessage(message.Msg_SeasonOnly, value);
        }
        else if (key == "cseasononly")
        {
            setLcdMessage(message.Msg_SeasonOnly, value);
        }

        // =====================================================
        // Season Passback
        // =====================================================
        else if (key == "seasonpassback")
        {
            setDefaultMessage(message.Msg_SeasonPassback, value);
        }
        else if (key == "cseasonpassback")
        {
            setLcdMessage(message.Msg_SeasonPassback, value);
        }

        // =====================================================
        // Season Terminated
        // =====================================================
        else if (key == "seasonterminated")
        {
            setDefaultMessage(message.Msg_SeasonTerminated, value);
        }
        else if (key == "cseasonterminated")
        {
            setLcdMessage(message.Msg_SeasonTerminated, value);
        }

        // =====================================================
        // System Error
        // =====================================================
        else if (key == "systemerror")
        {
            setDefaultMessage(message.Msg_SystemError, value);
        }
        else if (key == "csystemerror")
        {
            setLcdMessage(message.Msg_SystemError, value);
        }

        // =====================================================
        // Valid Season
        // =====================================================
        else if (key == "validseason")
        {
            setDefaultMessage(message.Msg_ValidSeason, value);
        }
        else if (key == "cvalidseason")
        {
            setLcdMessage(message.Msg_ValidSeason, value);
        }

        // =====================================================
        // VVIP
        // =====================================================
        else if (key == "vvip")
        {
            setDefaultMessage(message.Msg_VVIP, value);
        }
        else if (key == "cvvip")
        {
            setLcdMessage(message.Msg_VVIP, value);
        }

        // =====================================================
        // Whole Day Parking
        // =====================================================
        else if (key == "wholedayparking")
        {
            setDefaultMessage(message.Msg_WholeDayParking, value);
        }
        else if (key == "cwholedayparking")
        {
            setLcdMessage(message.Msg_WholeDayParking, value);
        }

        // =====================================================
        // With IU
        // =====================================================
        else if (key == "withiu")
        {
            setDefaultMessage(message.Msg_WithIU, value);
        }
        else if (key == "cwithiu")
        {
            setLcdMessage(message.Msg_WithIU, value);
        }

        // =====================================================
        // Black List
        // =====================================================
        else if (key == "blacklist")
        {
            setDefaultMessage(message.MsgBlackList, value);
        }
        else if (key == "cblacklist")
        {
            setLcdMessage(message.MsgBlackList, value);
        }

        // =====================================================
        // Enhanced Motorcycle Parking
        // =====================================================
        else if (key == "e1enhancedmcparking")
        {
            setDefaultMessage(message.Msg_E1enhancedMCParking, value);
        }
    }

    // =========================================================
    // Mark LED messages as loaded
    // =========================================================
    process.gbloadedLEDMsg = true;

    // =========================================================
    // Update Operation shared data
    // =========================================================
    OperationSharedDataUpdate update;

    update.tMsg = std::move(message);
    update.tProcess = std::move(process);

    if (!op->FnUpdateSharedData(std::move(update)))
    {
        logDbMessage("Unable to update Operation shared data.", "DB");

        return iLocalFail;
    }

    return iDBSuccess;
}

DBError db::loadmessage()
{
    // =========================================================
    // Load LED messages
    // =========================================================
    std::vector<ReaderItem> ledResult;

    const int ledRet =
        localdb->SQLSelect(
            "SELECT msg_id, msg_body "
            "FROM message_mst",
            &ledResult,
            true);

    if (ledRet != 0)
    {
        logDbMessage("Load LED message failed.", "DB");

        return iLocalFail;
    }

    const DBError ledLoadRet = loadEntrymessage(ledResult);

    if (ledLoadRet != iDBSuccess)
    {
        return ledLoadRet;
    }

    // =========================================================
    // Load LCD messages
    // =========================================================
    std::vector<ReaderItem> lcdResult;

    const int lcdRet =
        localdb->SQLSelect(
            "SELECT msg_id, msg_body "
            "FROM message_mst "
            "WHERE m_status >= 10",
            &lcdResult,
            true);

    if (lcdRet != 0)
    {
        logDbMessage("Load LCD message failed.", "DB");

        return iLocalFail;
    }

    return loadEntrymessage(lcdResult);
}

DBError db::loadExitLcdAndLedMessage(const std::vector<ReaderItem>& selResult)
{
    auto* op = operation::getInstance();

    const auto data = op->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return iLocalFail;
    }

    if (selResult.empty())
    {
        return iNoData;
    }

    auto exitMessage = data->tExitMsg;
    auto process = data->tProcess;

    // Default message:
    // Set both LED [0] and LCD [1].
    const auto setDefaultMessage =
        [](auto& target, const std::string& value)
        {
            target[0] = value;
            target[1] = value;
        };

    // LCD override:
    // Only update [1] if value is valid.
    const auto setLcdMessage =
        [](auto& target, const std::string& value)
        {
            if (!value.empty() &&
                boost::algorithm::to_lower_copy(value) != "null")
            {
                target[1] = value;
            }
        };

    // =========================================================
    // Process Exit LED / LCD messages
    // =========================================================
    for (const auto& row : selResult)
    {
        if (row.getDataSize() != 2)
        {
            continue;
        }

        const std::string key = boost::algorithm::to_lower_copy(row.GetDataItem(0));
        const std::string value = row.GetDataItem(1);

        // =====================================================
        // Black List
        // =====================================================
        if (key == "blacklist")
        {
            setDefaultMessage(exitMessage.MsgExit_BlackList, value);
        }
        else if (key == "cblacklist")
        {
            setLcdMessage(exitMessage.MsgExit_BlackList, value);
        }

        // =====================================================
        // Card Error
        // =====================================================
        else if (key == "carderror")
        {
            setDefaultMessage(exitMessage.MsgExit_CardError, value);
        }
        else if (key == "ccarderror")
        {
            setLcdMessage(exitMessage.MsgExit_CardError, value);
        }

        // =====================================================
        // Card In
        // =====================================================
        else if (key == "cardin")
        {
            setDefaultMessage(exitMessage.MsgExit_CardIn, value);
        }
        else if (key == "ccardin")
        {
            setLcdMessage(exitMessage.MsgExit_CardIn, value);
        }

        // =====================================================
        // Complimentary To Validation
        // =====================================================
        else if (key == "comp2val")
        {
            setDefaultMessage(exitMessage.MsgExit_Comp2Val, value);
        }
        else if (key == "ccomp2val")
        {
            setLcdMessage(exitMessage.MsgExit_Comp2Val, value);
        }

        // =====================================================
        // Complimentary Expired
        // =====================================================
        else if (key == "compexpired")
        {
            setDefaultMessage(exitMessage.MsgExit_CompExpired, value);
        }
        else if (key == "ccompexpired")
        {
            setLcdMessage(exitMessage.MsgExit_CompExpired, value);
        }

        // =====================================================
        // Complimentary
        // =====================================================
        else if (key == "complimentary")
        {
            setDefaultMessage(exitMessage.MsgExit_Complimentary, value);
        }
        else if (key == "ccomplimentary")
        {
            setLcdMessage(exitMessage.MsgExit_Complimentary, value);
        }

        // =====================================================
        // Debit Fail
        // =====================================================
        else if (key == "debitfail")
        {
            setDefaultMessage(exitMessage.MsgExit_DebitFail, value);
        }
        else if (key == "cdebitfail")
        {
            setLcdMessage(exitMessage.MsgExit_DebitFail, value);
        }

        // =====================================================
        // Debit NAK
        // =====================================================
        else if (key == "debitnak")
        {
            setDefaultMessage(exitMessage.MsgExit_DebitNak, value);
        }
        else if (key == "cdebitnak")
        {
            setLcdMessage(exitMessage.MsgExit_DebitNak, value);
        }

        // =====================================================
        // Entry Debit
        // =====================================================
        else if (key == "entrydebit")
        {
            setDefaultMessage(exitMessage.MsgExit_EntryDebit, value);
        }
        else if (key == "centrydebit")
        {
            setLcdMessage(exitMessage.MsgExit_EntryDebit, value);
        }

        // =====================================================
        // Expired Card
        // =====================================================
        else if (key == "expcard")
        {
            setDefaultMessage(exitMessage.MsgExit_ExpCard, value);
        }
        else if (key == "cexpcard")
        {
            setLcdMessage(exitMessage.MsgExit_ExpCard, value);
        }

        // =====================================================
        // Fleet Card
        // =====================================================
        else if (key == "fleetcard")
        {
            setDefaultMessage(exitMessage.MsgExit_FleetCard, value);
        }
        else if (key == "cfleetcard")
        {
            setLcdMessage(exitMessage.MsgExit_FleetCard, value);
        }

        // =====================================================
        // Free Parking
        // =====================================================
        else if (key == "freeparking")
        {
            setDefaultMessage(exitMessage.MsgExit_FreeParking, value);
        }
        else if (key == "cfreeparking")
        {
            setLcdMessage(exitMessage.MsgExit_FreeParking, value);
        }

        // =====================================================
        // Grace Period
        // =====================================================
        else if (key == "graceperiod")
        {
            setDefaultMessage(exitMessage.MsgExit_GracePeriod, value);
        }
        else if (key == "cgraceperiod")
        {
            setLcdMessage(exitMessage.MsgExit_GracePeriod, value);
        }

        // =====================================================
        // Invalid Ticket
        // =====================================================
        else if (key == "invalidticket")
        {
            setDefaultMessage(exitMessage.MsgExit_InvalidTicket, value);
        }
        else if (key == "cinvalidticket")
        {
            setLcdMessage(exitMessage.MsgExit_InvalidTicket, value);
        }

        // =====================================================
        // Wrong Ticket
        // =====================================================
        else if (key == "wrongticket")
        {
            setDefaultMessage(exitMessage.MsgExit_WrongTicket, value);
        }
        else if (key == "cwrongticket")
        {
            setLcdMessage(exitMessage.MsgExit_WrongTicket, value);
        }

        // =====================================================
        // Redemption Expired
        // =====================================================
        else if (key == "redemptionexpired")
        {
            setDefaultMessage(exitMessage.MsgExit_RedemptionExpired, value);
        }
        else if (key == "credemptionexpired")
        {
            setLcdMessage(exitMessage.MsgExit_RedemptionExpired, value);
        }

        // =====================================================
        // IU Problem
        // =====================================================
        else if (key == "iuproblem")
        {
            setDefaultMessage(exitMessage.MsgExit_IUProblem, value);
        }
        else if (key == "ciuproblem")
        {
            setLcdMessage(exitMessage.MsgExit_IUProblem, value);
        }

        // =====================================================
        // Master Season
        // =====================================================
        else if (key == "masterseason")
        {
            setDefaultMessage(exitMessage.MsgExit_MasterSeason, value);
        }
        else if (key == "cmasterseason")
        {
            setLcdMessage(exitMessage.MsgExit_MasterSeason, value);
        }

        // =====================================================
        // No Entry
        // =====================================================
        else if (key == "noentry")
        {
            setDefaultMessage(exitMessage.MsgExit_NoEntry, value);
        }
        else if (key == "cnoentry")
        {
            setLcdMessage(exitMessage.MsgExit_NoEntry, value);
        }

        // =====================================================
        // Printer Error
        // =====================================================
        else if (key == "printererror")
        {
            setDefaultMessage(exitMessage.MsgExit_PrinterError, value);
        }
        else if (key == "cprintererror")
        {
            setLcdMessage(exitMessage.MsgExit_PrinterError, value);
        }

        // =====================================================
        // Redemption Ticket
        // =====================================================
        else if (key == "redemptionticket")
        {
            setDefaultMessage(exitMessage.MsgExit_RedemptionTicket, value);
        }
        else if (key == "credemptionticket")
        {
            setLcdMessage(exitMessage.MsgExit_RedemptionTicket, value);
        }

        // =====================================================
        // Season Blocked
        // =====================================================
        else if (key == "seasonblocked")
        {
            setDefaultMessage(exitMessage.MsgExit_SeasonBlocked, value);
        }
        else if (key == "cseasonblocked")
        {
            setLcdMessage(exitMessage.MsgExit_SeasonBlocked, value);
        }

        // =====================================================
        // Season Expired
        // =====================================================
        else if (key == "seasonexpired")
        {
            setDefaultMessage(exitMessage.MsgExit_SeasonExpired, value);
        }
        else if (key == "cseasonexpired")
        {
            setLcdMessage(exitMessage.MsgExit_SeasonExpired, value);
        }

        // =====================================================
        // Season Invalid
        // =====================================================
        else if (key == "seasoninvalid")
        {
            setDefaultMessage(exitMessage.MsgExit_SeasonInvalid, value);
        }
        else if (key == "cseasoninvalid")
        {
            setLcdMessage(exitMessage.MsgExit_SeasonInvalid, value);
        }

        // =====================================================
        // Season Not Start
        // =====================================================
        else if (key == "seasonnotstart")
        {
            setDefaultMessage(exitMessage.MsgExit_SeasonNotStart, value);
        }
        else if (key == "cseasonnotstart")
        {
            setLcdMessage(exitMessage.MsgExit_SeasonNotStart, value);
        }

        // =====================================================
        // Season Only
        // =====================================================
        else if (key == "seasononly")
        {
            setDefaultMessage(exitMessage.MsgExit_SeasonOnly, value);
        }
        else if (key == "cseasononly")
        {
            setLcdMessage(exitMessage.MsgExit_SeasonOnly, value);
        }

        // =====================================================
        // Season Passback
        // =====================================================
        else if (key == "seasonpassback")
        {
            setDefaultMessage(exitMessage.MsgExit_SeasonPassback, value);
        }
        else if (key == "cseasonpassback")
        {
            setLcdMessage(exitMessage.MsgExit_SeasonPassback, value);
        }

        // =====================================================
        // Season Registered No IU
        // =====================================================
        else if (key == "seasonregnoiu")
        {
            setDefaultMessage(exitMessage.MsgExit_SeasonRegNoIU, value);
        }
        else if (key == "cseasonregnoiu")
        {
            setLcdMessage(exitMessage.MsgExit_SeasonRegNoIU, value);
        }

        // =====================================================
        // Season Registered OK
        // =====================================================
        else if (key == "seasonregok")
        {
            setDefaultMessage(exitMessage.MsgExit_SeasonRegOK, value);
        }
        else if (key == "cseasonregok")
        {
            setLcdMessage(exitMessage.MsgExit_SeasonRegOK, value);
        }

        // =====================================================
        // Season Terminated
        // =====================================================
        else if (key == "seasonterminated")
        {
            setDefaultMessage(exitMessage.MsgExit_SeasonTerminated, value);
        }
        else if (key == "cseasonterminated")
        {
            setLcdMessage(exitMessage.MsgExit_SeasonTerminated, value);
        }

        // =====================================================
        // System Error
        // =====================================================
        else if (key == "systemerror")
        {
            setDefaultMessage(exitMessage.MsgExit_SystemError, value);
        }
        else if (key == "csystemerror" ||
                 key == "csystemerrord")
        {
            setLcdMessage(exitMessage.MsgExit_SystemError, value);
        }

        // =====================================================
        // Take Card
        // =====================================================
        else if (key == "takecard")
        {
            setDefaultMessage(exitMessage.MsgExit_TakeCard, value);
        }
        else if (key == "ctakecard")
        {
            setLcdMessage(exitMessage.MsgExit_TakeCard, value);
        }

        // =====================================================
        // Ticket Expired
        // =====================================================
        else if (key == "ticketexpired")
        {
            setDefaultMessage(exitMessage.MsgExit_TicketExpired, value);
        }
        else if (key == "cticketexpired")
        {
            setLcdMessage(exitMessage.MsgExit_TicketExpired, value);
        }

        // =====================================================
        // Ticket Not Found
        // =====================================================
        else if (key == "ticketnotfound")
        {
            setDefaultMessage(exitMessage.MsgExit_TicketNotFound, value);
        }
        else if (key == "cticketnotfound")
        {
            setLcdMessage(exitMessage.MsgExit_TicketNotFound, value);
        }

        // =====================================================
        // Used Ticket
        // =====================================================
        else if (key == "usedticket")
        {
            setDefaultMessage(exitMessage.MsgExit_UsedTicket, value);
        }
        else if (key == "cusedticket")
        {
            setLcdMessage(exitMessage.MsgExit_UsedTicket, value);
        }

        // =====================================================
        // Wrong Card
        // =====================================================
        else if (key == "wrongcard")
        {
            setDefaultMessage(exitMessage.MsgExit_WrongCard, value);
        }
        else if (key == "cwrongcard")
        {
            setLcdMessage(exitMessage.MsgExit_WrongCard, value);
        }

        // =====================================================
        // X Card Again
        // =====================================================
        else if (key == "xcardagain")
        {
            setDefaultMessage(exitMessage.MsgExit_XCardAgain, value);
        }
        else if (key == "cxcardagain")
        {
            setLcdMessage(exitMessage.MsgExit_XCardAgain, value);
        }

        // =====================================================
        // X Card Taken
        // =====================================================
        else if (key == "xcardtaken")
        {
            setDefaultMessage(exitMessage.MsgExit_XCardTaken, value);
        }
        else if (key == "cxcardtaken")
        {
            setLcdMessage(exitMessage.MsgExit_XCardTaken, value);
        }

        // =====================================================
        // X Default IU
        // =====================================================
        else if (key == "xdefaultiu")
        {
            setDefaultMessage(exitMessage.MsgExit_XDefaultIU, value);
        }
        else if (key == "cxdefaultiu")
        {
            setLcdMessage(exitMessage.MsgExit_XDefaultIU, value);
        }

        // =====================================================
        // X Default LED
        // =====================================================
        else if (key == "xdefaultled")
        {
            setDefaultMessage(exitMessage.MsgExit_XDefaultLED, value);
        }
        else if (key == "cxdefaultled")
        {
            setLcdMessage(exitMessage.MsgExit_XDefaultLED, value);
        }

        // =====================================================
        // X Default LED 2
        // =====================================================
        else if (key == "xdefaultled2")
        {
            setDefaultMessage(exitMessage.MsgExit_XDefaultLED2, value);
        }
        else if (key == "cxdefaultled2")
        {
            setLcdMessage(exitMessage.MsgExit_XDefaultLED2, value);
        }

        // =====================================================
        // X Expiring Season
        // =====================================================
        else if (key == "xexpiringseason")
        {
            setDefaultMessage(exitMessage.MsgExit_XExpiringSeason, value);
        }
        else if (key == "cxexpiringseason")
        {
            setLcdMessage(exitMessage.MsgExit_XExpiringSeason, value);
        }

        // =====================================================
        // X Idle
        // =====================================================
        else if (key == "xidle")
        {
            setDefaultMessage(exitMessage.MsgExit_XIdle, value);
        }
        else if (key == "cxidle")
        {
            setLcdMessage(exitMessage.MsgExit_XIdle, value);
        }

        // =====================================================
        // X Loop A
        // =====================================================
        else if (key == "xloopa")
        {
            setDefaultMessage(exitMessage.MsgExit_XLoopA, value);
        }
        else if (key == "cxloopa")
        {
            setLcdMessage(exitMessage.MsgExit_XLoopA, value);
        }

        // =====================================================
        // X Low Balance
        // =====================================================
        else if (key == "xlowbal")
        {
            setDefaultMessage(exitMessage.MsgExit_XLowBal, value);
        }
        else if (key == "cxlowbal")
        {
            setLcdMessage(exitMessage.MsgExit_XLowBal, value);
        }

        // =====================================================
        // X No Card
        // =====================================================
        else if (key == "xnocard")
        {
            setDefaultMessage(exitMessage.MsgExit_XNoCard, value);
        }
        else if (key == "cxnocard")
        {
            setLcdMessage(exitMessage.MsgExit_XNoCard, value);
        }

        // =====================================================
        // X No CHU
        // =====================================================
        else if (key == "xnochu")
        {
            setDefaultMessage(exitMessage.MsgExit_XNoCHU, value);
        }
        else if (key == "cxnochu")
        {
            setLcdMessage(exitMessage.MsgExit_XNoCHU, value);
        }

        // =====================================================
        // X No IU
        // =====================================================
        else if (key == "xnoiu")
        {
            setDefaultMessage(exitMessage.MsgExit_XNoIU, value);
        }
        else if (key == "cxnoiu")
        {
            setLcdMessage(exitMessage.MsgExit_XNoIU, value);
        }

        // =====================================================
        // X Same Last IU
        // =====================================================
        else if (key == "xsamelastiu")
        {
            setDefaultMessage(exitMessage.MsgExit_XSameLastIU, value);
        }
        else if (key == "cxsamelastiu")
        {
            setLcdMessage(exitMessage.MsgExit_XSameLastIU, value);
        }

        // =====================================================
        // X Season Within Allowance
        // =====================================================
        else if (key == "xseasonwithinallowance")
        {
            setDefaultMessage(exitMessage.MsgExit_XSeasonWithinAllowance, value);
        }
        else if (key == "cxseasonwithinallowance")
        {
            setLcdMessage(exitMessage.MsgExit_XSeasonWithinAllowance, value);
        }

        // =====================================================
        // X Valid Season
        // =====================================================
        else if (key == "xvalidseason")
        {
            setDefaultMessage(exitMessage.MsgExit_XValidSeason, value);
        }
        else if (key == "cxvalidseason")
        {
            setLcdMessage(exitMessage.MsgExit_XValidSeason, value);
        }

        // =====================================================
        // Enhanced Motorcycle Parking
        // =====================================================
        else if (key == "x1enhancedmcparking")
        {
            setDefaultMessage(exitMessage.MsgExit_X1enhancedMCParking, value);
        }
        else if (key == "xenhancedmcparking")
        {
            setDefaultMessage(exitMessage.MsgExit_XenhancedMCParking, value);
        }
        else if (key == "xspt3parking")
        {
            setDefaultMessage(exitMessage.MsgExit_XSPT3Parking, value);
        }
        else if (key == "xvipholderparking")
        {
            setDefaultMessage(exitMessage.MsgExit_XVIPHolderParking, value);
        }
    }

    // =========================================================
    // Mark Exit messages as loaded
    // =========================================================
    process.gbloadedLEDExitMsg = true;

    // =========================================================
    // Update Operation shared data
    // =========================================================
    OperationSharedDataUpdate update;

    update.tExitMsg = std::move(exitMessage);
    update.tProcess = std::move(process);

    if (!op->FnUpdateSharedData(std::move(update)))
    {
        logDbMessage("Unable to update Operation shared data.", "DB");

        return iLocalFail;
    }

    return iDBSuccess;
}

DBError db::loadExitmessage()
{
    // =========================================================
    // Load Exit LED messages
    // =========================================================
    std::vector<ReaderItem> ledResult;

    const int ledRet =
        localdb->SQLSelect(
            "SELECT msg_id, msg_body "
            "FROM message_mst",
            &ledResult,
            true);

    if (ledRet != 0)
    {
        logDbMessage("Load Exit LED message failed.", "DB");

        return iLocalFail;
    }

    const DBError ledLoadRet = loadExitLcdAndLedMessage(ledResult);

    if (ledLoadRet != iDBSuccess)
    {
        return ledLoadRet;
    }

    // =========================================================
    // Load Exit LCD messages
    // =========================================================
    std::vector<ReaderItem> lcdResult;

    const int lcdRet =
        localdb->SQLSelect(
            "SELECT msg_id, msg_body "
            "FROM message_mst "
            "WHERE m_status >= 10",
            &lcdResult,
            true);

    if (lcdRet != 0)
    {
        logDbMessage("Load Exit LCD message failed.", "DB");

        return iLocalFail;
    }

    return loadExitLcdAndLedMessage(lcdResult);
}

DBError db::loadTR(int iType)
{
    // Preserve existing behavior:
    // TR type is currently forced to 2.
    (void)iType;
    constexpr int kTRType = 2;

    // =========================================================
    // Load Ticket / Receipt format from Local DB
    // =========================================================
    const std::string sqlStmt =
        "SELECT LineText, LineVar, LineFont, LineAlign "
        "FROM TR_mst "
        "WHERE TRType = " +
        std::to_string(kTRType) +
        " AND Enabled = 1 "
        "ORDER BY Line_no";

    std::vector<ReaderItem> result;

    const int ret = localdb->SQLSelect(sqlStmt, &result, true);

    if (ret != 0)
    {
        logDbMessage("Load TR failed.", "DB");

        return iLocalFail;
    }

    if (result.empty())
    {
        logDbMessage("No Ticket/Receipt to load.", "DB");

        return iNoData;
    }

    // =========================================================
    // Get Operation shared data
    // =========================================================
    auto* op = operation::getInstance();

    const auto data = op->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return iLocalFail;
    }

    auto trItems = data->tTR;

    // =========================================================
    // Load TR items
    // =========================================================
    int loadedCount = 0;

    try
    {
        for (const auto& row : result)
        {
            if (row.getDataSize() < 4)
            {
                continue;
            }

            tTR_struc tr{};

            tr.gsTR0 = row.GetDataItem(0);
            tr.gsTR1 = row.GetDataItem(1);
            tr.giTRF = std::stoi(row.GetDataItem(2));
            tr.giTRA = std::stoi(row.GetDataItem(3));

            trItems.push_back(std::move(tr));

            ++loadedCount;
        }
    }
    catch (const std::exception& e)
    {
        logDbMessage(std::string("loadTR exception: ") + e.what(), "DB");

        return iLocalFail;
    }

    // =========================================================
    // Update Operation shared data
    // =========================================================
    OperationSharedDataUpdate update;

    update.tTR = std::move(trItems);

    if (!op->FnUpdateSharedData(std::move(update)))
    {
        logDbMessage("Unable to update Operation shared data.", "DB");

        return iLocalFail;
    }

    logDbMessage(
        "Load " +
            std::to_string(loadedCount) +
            " Ticket/Receipt Format.",
        "DB");

    return iDBSuccess;
}

std::string db::GetPartialSeasonMsg(int transType)
{
    const std::string sqlStmt =
        "SELECT Description "
        "FROM trans_type "
        "WHERE trans_type = " +
        std::to_string(transType);

    std::vector<ReaderItem> result;

    const int ret = centraldb->SQLSelect(sqlStmt, &result, true);

    if (ret != 0)
    {
        return "";
    }

    if (result.empty())
    {
        return "";
    }

    return result.front().GetDataItem(0);
}

void db::moveOfflineTransToCentral()
{
    try
    {
        const auto data = operation::getInstance()->FnGetSharedData();

        if (!data)
        {
            logDbMessage("Unable to get Operation shared data.", "DB");

            return;
        }

        const auto stationType = data->gtStation.iType;

        // Only Entry / Exit stations are supported
        if (stationType != tientry &&
            stationType != tiExit)
        {
            return;
        }

        // =====================================================
        // Reset offline status
        // =====================================================
        if (!updateOperationProcess(
                [](tProcess_Struct& process)
                {
                    process.offline_status = 0;
                }))
        {
            logDbMessage("Unable to update Operation shared data.", "DB");
        }

        // =====================================================
        // Check pending offline transactions
        // =====================================================
        const bool isEntryStation = (stationType == tientry);
        const std::string tableName = isEntryStation ? "Entry_Trans" : "Exit_Trans";
        const Ctrl_Type ctrl = isEntryStation ? s_Entry : s_Exit;

        const std::string countSql =
            "SELECT count(iu_tk_no) FROM " +
            tableName;

        std::vector<ReaderItem> countResult;

        const int countRet = localdb->SQLSelect(countSql, &countResult, false);

        if (countRet != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Move offline data failed.", "DB");

            return;
        }

        m_local_db_err_flag = 0;

        if (countResult.empty())
        {
            return;
        }

        const int pendingCount = std::stoi(countResult.front().GetDataItem(0));

        if (pendingCount <= 0)
        {
            return;
        }

        logDbMessage(
            "Total " +
                std::to_string(pendingCount) +
                (isEntryStation
                    ? " Entry trans to be uploaded."
                    : " Exit trans to be uploaded."),
            "DB");

        if (!updateOperationProcess(
                [](tProcess_Struct& process)
                {
                    process.offline_status = 1;
                }))
        {
            logDbMessage("Unable to update Operation shared data.", "DB");
        }

        // =====================================================
        // Build transaction query
        // =====================================================
        std::string sqlStmt;

        if (isEntryStation)
        {
            sqlStmt =
                "SELECT "
                "Station_ID, "
                "Entry_Time, "
                "iu_tk_No, "
                "trans_type, "
                "Status, "
                "TK_SerialNo, "
                "Card_Type, "
                "card_no, "
                "paid_amt, "
                "parking_fee, "
                "gst_amt, "
                "lpn, "
                "VCC "
                "FROM Entry_Trans "
                "ORDER BY Entry_Time DESC";
        }
        else
        {
            sqlStmt =
                "SELECT "
                "Station_ID, "
                "Exit_Time, "
                "iu_tk_No, "
                "card_mc_no, "
                "trans_type, "
                "status, "
                "parked_time, "
                "Parking_Fee, "
                "Paid_Amt, "
                "Receipt_No, "
                "Redeem_amt, "
                "Redeem_time, "
                "Redeem_no, "
                "gst_amt, "
                "chu_debit_code, "
                "Card_Type, "
                "Top_Up_Amt, "
                "lpn, "
                "Entry_ID, "
                "entry_time, "
                "VCC, "
                "EEPDSerialNo, "
                "EEPTransRoute, "
                "EEPPaymentResult, "
                "EEPPaymentTime "
                "FROM Exit_Trans "
                "ORDER BY Exit_Time DESC";
        }

        // =====================================================
        // Load transactions from Local DB
        // =====================================================
        std::vector<ReaderItem> result;

        const int selectRet = localdb->SQLSelect(sqlStmt, &result, true);

        if (selectRet != 0)
        {
            m_local_db_err_flag = 1;
            return;
        }

        m_local_db_err_flag = 0;

        if (result.empty())
        {
            return;
        }

        logDbMessage("Uploading " + std::to_string(result.size()) + " records: Started", "DB");

        // =====================================================
        // Upload transactions
        // =====================================================
        for (const auto& row : result)
        {
            int centralRet = -1;

            // =================================================
            // Entry transaction
            // =================================================
            if (isEntryStation)
            {
                tEntryTrans_Struct entryTrans{};

                entryTrans.esid = row.GetDataItem(0);
                entryTrans.sEntryTime = row.GetDataItem(1);
                entryTrans.sIUTKNo = row.GetDataItem(2);
                entryTrans.iTransType = std::stoi(row.GetDataItem(3));
                entryTrans.iStatus = std::stoi(row.GetDataItem(4));
                entryTrans.sSerialNo = row.GetDataItem(5);
                entryTrans.iCardType = std::stoi(row.GetDataItem(6));
                entryTrans.sCardNo = row.GetDataItem(7);
                entryTrans.sPaidAmt = std::stof(row.GetDataItem(8));
                entryTrans.sFee = std::stof(row.GetDataItem(9));
                entryTrans.sGSTAmt = std::stof(row.GetDataItem(10));
                entryTrans.sLPN[0] = row.GetDataItem(11);
                entryTrans.VCC = row.GetDataItem(12);

                centralRet = insertTransToCentralEntryTransTmp(entryTrans);

                // Central insert failed
                if (centralRet != 0)
                {
                    m_remote_db_err_flag.store(1);
                    continue;
                }

                // Central insert successful
                const int deleteRet = deleteLocalTrans(entryTrans.sIUTKNo, entryTrans.sEntryTime, ctrl);

                m_local_db_err_flag = (deleteRet == 0) ? 0 : 1;

                m_remote_db_err_flag.store(0);
            }

            // =================================================
            // Exit transaction
            // =================================================
            else
            {
                tExitTrans_Struct exitTrans{};

                exitTrans.xsid = row.GetDataItem(0);
                exitTrans.sExitTime = row.GetDataItem(1);
                exitTrans.sIUNo = row.GetDataItem(2);
                exitTrans.sCardNo = row.GetDataItem(3);
                exitTrans.iTransType = std::stoi(row.GetDataItem(4));
                exitTrans.iStatus = std::stoi(row.GetDataItem(5));
                exitTrans.lParkedTime = std::stoi(row.GetDataItem(6));
                exitTrans.sFee = std::stof(row.GetDataItem(7));
                exitTrans.sPaidAmt = std::stof(row.GetDataItem(8));
                exitTrans.sReceiptNo = row.GetDataItem(9);
                exitTrans.sRedeemAmt = std::stof(row.GetDataItem(10));
                exitTrans.iRedeemTime = std::stoi(row.GetDataItem(11));
                exitTrans.sRedeemNo = row.GetDataItem(12);
                exitTrans.sGSTAmt = std::stof(row.GetDataItem(13));
                exitTrans.sCHUDebitCode = row.GetDataItem(14);
                exitTrans.iCardType = std::stoi(row.GetDataItem(15));
                exitTrans.sTopupAmt = std::stof(row.GetDataItem(16));
                exitTrans.sLPN[0] = row.GetDataItem(17);
                exitTrans.iEntryID = std::stoi(row.GetDataItem(18));

                if (exitTrans.iEntryID < 1)
                {
                    exitTrans.sEntryTime = "";
                }
                else
                {
                    exitTrans.sEntryTime =  row.GetDataItem(19);
                }

                exitTrans.VCC = row.GetDataItem(20);
                exitTrans.sDSerialNo = row.GetDataItem(21);
                exitTrans.iEEPTransRoute = std::stoi(row.GetDataItem(22));
                exitTrans.iEEPPaymentResult = std::stoi(row.GetDataItem(23));
                exitTrans.sEEPpaymentTime = row.GetDataItem(24);

                centralRet = insertTransToCentralExitTransTmp(exitTrans);

                // Central insert failed
                if (centralRet != 0)
                {
                    m_remote_db_err_flag.store(1);
                    continue;
                }

                // -------------------------------------------------
                // Preserve existing movement_trans handling
                // -------------------------------------------------
                DeleteBeforeInsertMT(exitTrans);
                insert2movementtrans(exitTrans);

                const int deleteRet = deleteLocalTrans(exitTrans.sIUNo, exitTrans.sExitTime, ctrl);

                m_local_db_err_flag =(deleteRet == 0) ? 0 : 1;

                m_remote_db_err_flag.store(0);
            }
        }

        logDbMessage("Uploading trans records: End", "DB");
    }
    catch (const std::exception& e)
    {
        logDbMessage("moveOfflineTransToCentral exception: " + std::string(e.what()), "DB");

        m_local_db_err_flag = 1;
    }
}

int db::insertTransToCentralEntryTransTmp(const tEntryTrans_Struct& entryTrans)
{
    constexpr const char* kTableName = "Entry_Trans_tmp";

    // =========================================================
    // Build INSERT statement
    // =========================================================
    const std::string sqlStmt =
        "INSERT INTO " +
        std::string(kTableName) +
        " ("
        "Station_ID, "
        "Entry_Time, "
        "IU_Tk_No, "
        "trans_type, "
        "status, "
        "TK_Serialno, "
        "Card_Type, "
        "card_no, "
        "paid_amt, "
        "parking_fee, "
        "VCC, "
        "gst_amt, "
        "lpn"
        ") VALUES ('" +
        entryTrans.esid +
        "', convert(datetime, '" +
        entryTrans.sEntryTime +
        "', 120), '" +
        entryTrans.sIUTKNo +
        "', '" +
        std::to_string(entryTrans.iTransType) +
        "', '" +
        std::to_string(entryTrans.iStatus) +
        "', '" +
        entryTrans.sSerialNo +
        "', '" +
        std::to_string(entryTrans.iCardType) +
        "', '" +
        entryTrans.sCardNo +
        "', '" +
        std::to_string(entryTrans.sPaidAmt) +
        "', '" +
        std::to_string(entryTrans.sFee) +
        "', '" +
        entryTrans.VCC +
        "', '" +
        std::to_string(entryTrans.sGSTAmt) +
        "', '" +
        entryTrans.sLPN[0] +
        "')";

    // =========================================================
    // Insert into Central DB
    // =========================================================
    const int ret = centraldb->SQLExecutNoneQuery(sqlStmt);

    if (ret == 0)
    {
        logDbMessage(
            "Central DB: INSERT IUNo=" +
                entryTrans.sIUTKNo +
                " and EntryTime=" +
                entryTrans.sEntryTime +
                " INTO " +
                kTableName +
                " : Success",
            "DB");
    }
    else
    {
        logDbMessage(
            "Central DB: INSERT IUNo=" +
                entryTrans.sIUTKNo +
                " and EntryTime=" +
                entryTrans.sEntryTime +
                " INTO " +
                kTableName +
                " : Fail",
            "DB");
    }

    m_remote_db_err_flag.store(ret == 0 ? 0 : 1);

    return ret;
}

int db::insertTransToCentralExitTransTmp(const tExitTrans_Struct& exitTrans)
{
    constexpr const char* kTableName = "Exit_Trans_tmp";

    logDbMessage(
        "Central DB: INSERT IUNo=" +
            exitTrans.sIUNo +
            " and ExitTime=" +
            exitTrans.sExitTime +
            " INTO " +
            kTableName +
            " : Started",
        "DB");

    // =========================================================
    // Build INSERT statement
    // =========================================================
    const std::string sqlStmt =
        "INSERT INTO " +
        std::string(kTableName) +
        " ("
        "Station_ID, "
        "Exit_Time, "
        "IU_Tk_No, "
        "card_mc_no, "
        "trans_type, "
        "parked_time, "
        "parking_fee, "
        "paid_amt, "
        "receipt_no, "
        "status, "
        "redeem_amt, "
        "redeem_time, "
        "redeem_no, "
        "gst_amt, "
        "chu_debit_code, "
        "card_type, "
        "top_up_amt, "
        "lpn, "
        "VCC, "
        "EEPDSerialNo, "
        "EEPTransRoute, "
        "EEPPaymentResult, "
        "EEPPaymentTime"
        ") VALUES ('" +
        exitTrans.xsid +
        "', convert(datetime, '" +
        exitTrans.sExitTime +
        "', 120), '" +
        exitTrans.sIUNo +
        "', '" +
        exitTrans.sCardNo +
        "', '" +
        std::to_string(exitTrans.iTransType) +
        "', '" +
        std::to_string(exitTrans.lParkedTime) +
        "', '" +
        std::to_string(exitTrans.sFee) +
        "', '" +
        std::to_string(exitTrans.sPaidAmt) +
        "', '" +
        exitTrans.sReceiptNo +
        "', '" +
        std::to_string(exitTrans.iStatus) +
        "', '" +
        std::to_string(exitTrans.sRedeemAmt) +
        "', '" +
        std::to_string(exitTrans.iRedeemTime) +
        "', '" +
        exitTrans.sRedeemNo +
        "', '" +
        std::to_string(exitTrans.sGSTAmt) +
        "', '" +
        exitTrans.sCHUDebitCode +
        "', '" +
        std::to_string(exitTrans.iCardType) +
        "', '" +
        std::to_string(exitTrans.sTopupAmt) +
        "', '" +
        exitTrans.sLPN[0] +
        "', '" +
        exitTrans.VCC +
        "', '" +
        exitTrans.sDSerialNo +
        "', '" +
        std::to_string(exitTrans.iEEPTransRoute) +
        "', '" +
        std::to_string(exitTrans.iEEPPaymentResult) +
        "', convert(datetime, '" +
        exitTrans.sEEPpaymentTime +
        "', 120))";

    // =========================================================
    // Insert into Central DB
    // =========================================================
    const int ret = centraldb->SQLExecutNoneQuery(sqlStmt);

    if (ret == 0)
    {
        logDbMessage(
            "Central DB: INSERT IUNo=" +
                exitTrans.sIUNo +
                " and ExitTime=" +
                exitTrans.sExitTime +
                " INTO " +
                kTableName +
                " : Success",
            "DB");
    }
    else
    {
        logDbMessage(
            "Central DB: INSERT IUNo=" +
                exitTrans.sIUNo +
                " and ExitTime=" +
                exitTrans.sExitTime +
                " INTO " +
                kTableName +
                " : Fail",
            "DB");
    }

    m_remote_db_err_flag.store(ret == 0 ? 0 : 1);

    return ret;
}

int db::deleteLocalTrans(const std::string& iuNo, const std::string& transTime, Ctrl_Type ctrl)
{
    std::string tableName;
    std::string timeColumn;

    // =========================================================
    // Determine table and transaction time column
    // =========================================================
    switch (ctrl)
    {
        case s_Entry:
            tableName = "Entry_Trans";
            timeColumn = "Entry_Time";
            break;

        case s_Exit:
            tableName = "Exit_Trans";
            timeColumn = "exit_time";
            break;

        default:
            logDbMessage("deleteLocalTrans: Invalid Ctrl_Type.", "DB");

            m_local_db_err_flag = 1;
            return -1;
    }

    try
    {
        // =====================================================
        // Build DELETE statement
        // =====================================================
        const std::string sqlStmt =
            "DELETE FROM " +
            tableName +
            " WHERE iu_tk_no = '" +
            iuNo +
            "' AND " +
            timeColumn +
            " = '" +
            transTime +
            "'";

        if (ctrl == s_Exit)
        {
            logDbMessage(
                "Local DB: Delete IUNo=" +
                    iuNo +
                    " AND TrxTime=" +
                    transTime +
                    " From " +
                    tableName +
                    ": Started",
                "DB");
        }

        // =====================================================
        // Delete Local transaction
        // =====================================================
        const int ret = localdb->SQLExecutNoneQuery(sqlStmt);

        if (ret == 0)
        {
            m_local_db_err_flag = 0;

            logDbMessage(
                "Local DB: Delete IUNo=" +
                    iuNo +
                    " AND TrxTime=" +
                    transTime +
                    " From " +
                    tableName +
                    ": Success",
                "DB");
        }
        else
        {
            m_local_db_err_flag = 1;

            logDbMessage(
                "Local DB: Delete IUNo=" +
                    iuNo +
                    " AND TrxTime=" +
                    transTime +
                    " From " +
                    tableName +
                    ": Fail",
                "DB");
        }

        return ret;
    }
    catch (const std::exception& e)
    {
        m_local_db_err_flag = 1;

        logDbMessage(
            "Local DB: Delete IUNo=" +
                iuNo +
                " AND TrxTime=" +
                transTime +
                " From " +
                tableName +
                ": Fail",
            "DB");

        logDbMessage("Local DB: deleteLocalTrans error: " + std::string(e.what()), "DB");

        return -1;
    }
}

int db::clearseason()
{
    constexpr const char* kTableName = "season_mst";

    const std::string sqlStmt = "TRUNCATE TABLE " + std::string(kTableName);

    try
    {
        const int ret = localdb->SQLExecutNoneQuery(sqlStmt);

        m_local_db_err_flag = (ret == 0) ? 0 : 1;

        return ret;
    }
    catch (const std::exception& e)
    {
        logDbMessage("DB: Local DB error in clearing " + std::string(kTableName) + ": " + e.what(), "DB");

        m_local_db_err_flag = 1;

        return -1;
    }
}

int db::IsBlackListIU(const std::string& iuNo)
{
    try
    {
        const std::string sqlStmt =
            "SELECT type "
            "FROM BlackList "
            "WHERE status = 0 "
            "AND CAN = '" +
            iuNo +
            "'";

        std::vector<ReaderItem> result;

        const int ret = centraldb->SQLSelect(sqlStmt, &result, true);

        if (ret != 0)
        {
            return -1;
        }

        if (result.empty())
        {
            return -1;
        }

        return std::stoi(result.front().GetDataItem(0));
    }
    catch (const std::exception& e)
    {
        logDbMessage("IsBlackListIU exception for IU '" + iuNo + "': " + e.what(), "DB");

        return -1;
    }
}

int db::AddRemoteControl(const std::string& stationId, const std::string& action, const std::string& remarks)
{
    constexpr const char* kTableName = "remote_control_history";

    try
    {
        // =====================================================
        // Build INSERT statement
        // =====================================================
        const std::string sqlStmt =
            "INSERT INTO " +
            std::string(kTableName) +
            " ("
            "station_id, "
            "action_dt, "
            "action_name, "
            "operator, "
            "remarks"
            ") VALUES ('" +
            stationId +
            "', GETDATE(), '" +
            action +
            "', 'auto', '" +
            remarks +
            "')";

        // =====================================================
        // Insert into Central DB
        // =====================================================
        const int ret = centraldb->SQLExecutNoneQuery(sqlStmt);

        if (ret == 0)
        {
            logDbMessage("Success insert: " + action, "DB");

            m_remote_db_err_flag.store(0);
        }
        else
        {
            logDbMessage("Fail to insert: " + action, "DB");

            m_remote_db_err_flag.store(1);
        }

        return ret;
    }
    catch (const std::exception& e)
    {
        logDbMessage("AddRemoteControl exception: " + std::string(e.what()), "DB");

        m_remote_db_err_flag.store(1);

        return -1;
    }
}

int db::AddSysEvent(const std::string& event, int eventType, std::string occurTime)
{
    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return -1;
    }

    const std::string stationId = std::to_string(data->gtStation.iSID);

    if (occurTime.empty())
    {
        occurTime = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
    }

    try
    {
        std::string sqlStmt;

        // =====================================================
        // Build INSERT statement
        // =====================================================
        if (eventType > 0)
        {
            sqlStmt =
                "INSERT INTO sys_event_log "
                "(station_id, event, event_type, event_time) "
                "VALUES ('" +
                stationId +
                "', '" +
                event +
                "', " +
                std::to_string(eventType) +
                ", '" +
                occurTime +
                "')";
        }
        else
        {
            sqlStmt =
                "INSERT INTO sys_event_log "
                "(station_id, event) "
                "VALUES ('" +
                stationId +
                "', '" +
                event +
                "')";
        }

        // =====================================================
        // Insert into Central DB
        // =====================================================
        const int ret = centraldb->SQLExecutNoneQuery(sqlStmt);

        if (ret == 0)
        {
            logDbMessage("Success insert sys event log: " + event, "DB");

            m_remote_db_err_flag.store(0);
        }
        else
        {
            logDbMessage("Fail to insert sys event log: " + event, "DB");

            m_remote_db_err_flag.store(1);
        }

        return ret;
    }
    catch (const std::exception& e)
    {
        logDbMessage("AddSysEvent exception: " + std::string(e.what()), "DB");

        m_remote_db_err_flag.store(1);

        return -1;
    }
}

int db::UpdateSysEvent(const std::string& event, int eventType, const std::string& occurTime)
{
    (void)event;

    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return -1;
    }

    const std::string stationId = std::to_string(data->gtStation.iSID);

    try
    {
        // =====================================================
        // Build UPDATE statement
        // =====================================================
        const std::string sqlStmt =
            "UPDATE sys_event_log "
            "SET recoved_time = '" +
            occurTime +
            "' "
            "WHERE station_id = " +
            stationId +
            " AND event_type = " +
            std::to_string(eventType) +
            " AND recoved_time IS NULL";

        // =====================================================
        // Update Central DB
        // =====================================================
        const int ret = centraldb->SQLExecutNoneQuery(sqlStmt);

        if (ret == 0)
        {
            m_remote_db_err_flag.store(0);

            if (centraldb->NumberOfRowsAffected > 0)
            {
                logDbMessage("Success update recovered time.", "DB");
            }
            else
            {
                logDbMessage("No event found for recovered time update.", "DB");
            }
        }
        else
        {
            logDbMessage("Fail to update event recovered time.", "DB");

            m_remote_db_err_flag.store(1);
        }

        return ret;
    }
    catch (const std::exception& e)
    {
        logDbMessage("UpdateSysEvent exception: " + std::string(e.what()), "DB");

        m_remote_db_err_flag.store(1);

        return -1;
    }
}

bool db::HasAlertNotification()
{
    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return true;
    }

    const std::string stationId = std::to_string(data->gtStation.iSID);

    try
    {
        const std::string sqlStmt =
            "SELECT * "
            "FROM sys_event_log "
            "WHERE recoved_time IS NULL "
            "AND station_id = " +
            stationId;

        std::vector<ReaderItem> result;

        const int ret = centraldb->SQLSelect(sqlStmt, &result, true);

        if (ret != 0)
        {
            return true;
        }

        return !result.empty();
    }
    catch (const std::exception& e)
    {
        logDbMessage("HasAlertNotification exception: " + std::string(e.what()), "DB");

        return true;
    }
}

int db::FnGetDatabaseErrorFlag() const
{
    return m_remote_db_err_flag.load();
}

int db::HouseKeeping()
{
    // =========================================================
    // Clear expired season records
    // =========================================================
    clearexpiredseason();

    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return -1;
    }

    const auto stationType = data->gtStation.iType;
    const int dataKeepHours = data->tParas.giDataKeepDays * 24;

    // =========================================================
    // Local housekeeping helper
    // =========================================================
    const auto executeHouseKeeping =
        [this](
            const std::string& sqlStmt,
            const std::string& tableName)
        {
            try
            {
                const int ret = localdb->SQLExecutNoneQuery(sqlStmt);

                m_local_db_err_flag = (ret == 0) ? 0 : 1;

                return ret;
            }
            catch (const std::exception& e)
            {
                logDbMessage(
                    "DB: Local DB error in housekeeping(" +
                        tableName +
                        "): " +
                        e.what(),
                    "DB");

                m_local_db_err_flag = 1;

                return -1;
            }
        };

    // =========================================================
    // Entry station housekeeping
    // =========================================================
    if (stationType == tientry)
    {
        const std::string sqlStmt =
            "DELETE FROM Entry_Trans "
            "WHERE send_status = true "
            "OR TIMESTAMPDIFF(HOUR, entry_time, NOW()) >= " +
            std::to_string(dataKeepHours);

        executeHouseKeeping(sqlStmt, "Entry_Trans");

        return 0;
    }

    // =========================================================
    // Exit station housekeeping
    // =========================================================
    {
        const std::string sqlStmt =
            "DELETE FROM Exit_Trans "
            "WHERE send_status = true "
            "OR TIMESTAMPDIFF(HOUR, exit_time, NOW()) >= " +
            std::to_string(dataKeepHours);

        executeHouseKeeping(sqlStmt, "Exit_Trans");
    }

    // =========================================================
    // Clear old unmatched Entry transactions at Exit station
    // =========================================================
    {
        const std::string sqlStmt =
            "DELETE FROM Entry_Trans "
            "WHERE TIMESTAMPDIFF(HOUR, entry_time, NOW()) >= " +
            std::to_string(dataKeepHours);

        executeHouseKeeping(sqlStmt, "Entry_Trans");
    }

    return 0;
}

int db::clearexpiredseason()
{
    const std::string sqlStmt =
        "DELETE FROM season_mst "
        "WHERE TIMESTAMPDIFF(HOUR, date_to, NOW()) >= 30 * 24";

    try
    {
        const int ret = localdb->SQLExecutNoneQuery(sqlStmt);

        m_local_db_err_flag = (ret == 0) ? 0 : 1;

        return ret;
    }
    catch (const std::exception& e)
    {
        logDbMessage("DB: Local DB error in clearing expired season: " + std::string(e.what()), "DB");

        m_local_db_err_flag = 1;

        return -1;
    }
}

int db::updateEntryTrans(const std::string& lpn, const std::string& transId)
{
    try
    {
        // =====================================================
        // Try Entry_trans_tmp first
        // =====================================================
        const std::string tmpSql =
            "UPDATE Entry_trans_tmp "
            "SET lpn = '" +
            lpn +
            "' "
            "WHERE entry_lpn_sid = '" +
            transId +
            "'";

        int ret = centraldb->SQLExecutNoneQuery(tmpSql);

        if (ret != 0)
        {
            logDbMessage("Fail to update LPN to Entry_trans_tmp.", "DB");

            m_remote_db_err_flag.store(1);

            return ret;
        }

        if (centraldb->NumberOfRowsAffected > 0)
        {
            logDbMessage("Success update LPN to Entry_trans_tmp.", "DB");

            m_remote_db_err_flag.store(0);

            return ret;
        }

        // =====================================================
        // Not found in tmp, try Entry_trans
        // =====================================================
        const std::string entrySql =
            "UPDATE Entry_trans "
            "SET lpn = '" +
            lpn +
            "' "
            "WHERE entry_lpn_sid = '" +
            transId +
            "'";

        ret = centraldb->SQLExecutNoneQuery(entrySql);

        if (ret != 0)
        {
            logDbMessage("Fail to update LPN to Entry_trans.", "DB");

            m_remote_db_err_flag.store(1);

            return ret;
        }

        m_remote_db_err_flag.store(0);

        if (centraldb->NumberOfRowsAffected > 0)
        {
            logDbMessage("Success update LPN to Entry_trans.", "DB");
        }
        else
        {
            logDbMessage("No TransID found for LPN update.", "DB");
        }

        return ret;
    }
    catch (const std::exception& e)
    {
        logDbMessage("updateEntryTrans exception: " + std::string(e.what()), "DB");

        m_remote_db_err_flag.store(1);

        return -1;
    }
}

int db::updateExitTrans(const std::string& lpn, const std::string& transId)
{
    try
    {
        // =====================================================
        // Try Exit_trans_tmp first
        // =====================================================
        const std::string tmpSql =
            "UPDATE Exit_trans_tmp "
            "SET lpn = '" +
            lpn +
            "' "
            "WHERE exit_lpn_sid = '" +
            transId +
            "'";

        int ret = centraldb->SQLExecutNoneQuery(tmpSql);

        if (ret != 0)
        {
            logDbMessage("Fail to update LPN to Exit_trans_tmp.", "DB");

            m_remote_db_err_flag.store(1);

            return ret;
        }

        if (centraldb->NumberOfRowsAffected > 0)
        {
            logDbMessage("Success update LPN to Exit_trans_tmp.", "DB");

            m_remote_db_err_flag.store(0);

            return ret;
        }

        // =====================================================
        // Not found in tmp, try Exit_trans
        // =====================================================
        const std::string exitSql =
            "UPDATE Exit_trans "
            "SET lpn = '" +
            lpn +
            "' "
            "WHERE exit_lpn_sid = '" +
            transId +
            "'";

        ret = centraldb->SQLExecutNoneQuery(exitSql);

        if (ret != 0)
        {
            logDbMessage("Fail to update LPN to Exit_trans.", "DB");

            m_remote_db_err_flag.store(1);

            return ret;
        }

        m_remote_db_err_flag.store(0);

        if (centraldb->NumberOfRowsAffected > 0)
        {
            logDbMessage("Success update LPN to Exit_trans.", "DB");
        }
        else
        {
            logDbMessage("No TransID found for LPN update.", "DB");
        }

        return ret;
    }
    catch (const std::exception& e)
    {
        logDbMessage("updateExitTrans exception: " + std::string(e.what()), "DB");

        m_remote_db_err_flag.store(1);

        return -1;
    }
}

DBError db::insertexittrans(tExitTrans_Struct& exitTrans)
{
    auto* op = operation::getInstance();

    const auto data = op->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return iLocalFail;
    }

    const std::string transId = data->tProcess.gsTransID;

    // =========================================================
    // Format transaction amounts
    // =========================================================
    exitTrans.sFee = op->GfeeFormat(exitTrans.sFee);
    exitTrans.sPaidAmt = op->GfeeFormat(exitTrans.sPaidAmt);
    exitTrans.sRedeemAmt = op->GfeeFormat(exitTrans.sRedeemAmt);
    exitTrans.sGSTAmt = op->GfeeFormat(exitTrans.sGSTAmt);

    bool useLocalDb = false;

    // =========================================================
    // Check / reconnect Central DB
    // =========================================================
    if (centraldb->IsConnected() != -1)
    {
        centraldb->Disconnect();

        if (centraldb->Connect() != 0)
        {
            logDbMessage("Unable to connect to Central DB while inserting exit_trans table.", "DB");

            if (!updateOperationProcess(
                    [](tProcess_Struct& process)
                    {
                        process.giSystemOnline = 1;
                    }))
            {
                logDbMessage("Unable to update Operation shared data.", "DB");
            }

            useLocalDb = true;
        }
    }

    // =========================================================
    // Insert into Central DB
    // =========================================================
    if (!useLocalDb)
    {
        if (!updateOperationProcess(
                [](tProcess_Struct& process)
                {
                    process.giSystemOnline = 0;
                }))
        {
            logDbMessage(
                "Unable to update Operation shared data.",
                "DB");
        }

        // Central DB parked_time is SMALLINT
        if (exitTrans.lParkedTime >= 32000)
        {
            exitTrans.lParkedTime = 32000;
        }

        const bool hasEepPaymentTime =
            (exitTrans.iEEPPaymentResult == 1 ||
             exitTrans.iEEPPaymentResult == 2);

        std::string sqlStmt =
            "INSERT INTO exit_trans_tmp ("
            "station_id, "
            "exit_time, "
            "iu_tk_no, "
            "card_mc_no, "
            "trans_type, "
            "parked_time, "
            "parking_fee, "
            "paid_amt, "
            "receipt_no, "
            "status, "
            "redeem_amt, "
            "redeem_time, "
            "redeem_no, "
            "gst_amt, "
            "chu_debit_code, "
            "card_type, "
            "top_up_amt, "
            "uposbatchno, "
            "feefrom, "
            "lpn, "
            "exit_lpn_SID, "
            "EEPDSerialNo, "
            "EEPTransRoute, "
            "EEPPaymentResult, "
            "VCC";

        if (hasEepPaymentTime)
        {
            sqlStmt +=
                ", EEPPaymentTime";
        }

        sqlStmt +=
            ") VALUES (" +
            exitTrans.xsid +
            ", '" +
            exitTrans.sExitTime +
            "', '" +
            exitTrans.sIUNo +
            "', '" +
            exitTrans.sCardNo +
            "', " +
            std::to_string(exitTrans.iTransType) +
            ", " +
            std::to_string(exitTrans.lParkedTime) +
            ", " +
            std::to_string(exitTrans.sFee) +
            ", " +
            std::to_string(exitTrans.sPaidAmt) +
            ", '" +
            exitTrans.sReceiptNo +
            "', " +
            std::to_string(exitTrans.iStatus) +
            ", " +
            std::to_string(exitTrans.sRedeemAmt) +
            ", " +
            std::to_string(exitTrans.iRedeemTime) +
            ", '" +
            exitTrans.sRedeemNo +
            "', " +
            std::to_string(exitTrans.sGSTAmt) +
            ", '" +
            exitTrans.sCHUDebitCode +
            "', " +
            std::to_string(exitTrans.iCardType) +
            ", " +
            std::to_string(exitTrans.sTopupAmt) +
            ", '" +
            exitTrans.uposbatchno +
            "', '" +
            exitTrans.feefrom +
            "', '" +
            exitTrans.lpn +
            "', '" +
            transId +
            "', '" +
            exitTrans.sDSerialNo +
            "', " +
            std::to_string(exitTrans.iEEPTransRoute) +
            ", " +
            std::to_string(exitTrans.iEEPPaymentResult) +
            ", '" +
            exitTrans.VCC +
            "'";

        if (hasEepPaymentTime)
        {
            sqlStmt +=
                ", '" +
                exitTrans.sEEPpaymentTime +
                "'";
        }

        sqlStmt += ")";

        const int ret = centraldb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            logDbMessage(sqlStmt, "DB");
            logDbMessage("Insert exit_trans to Central: fail.", "DB");

            return iCentralFail;
        }

        logDbMessage("Insert exit_trans to Central: success.", "DB");

        return iCentralSuccess;
    }

    // =========================================================
    // Central unavailable - insert into Local DB
    // =========================================================
    if (localdb->IsConnected() != 1)
    {
        localdb->Disconnect();

        if (localdb->Connect() != 0)
        {
            logDbMessage("Unable to connect to Local DB while inserting Exit_Trans table.", "DB");

            return iLocalFail;
        }
    }

    const std::string sqlStmt =
        "INSERT INTO Exit_Trans ("
        "Station_ID, "
        "exit_time, "
        "iu_tk_no, "
        "card_mc_no, "
        "trans_type, "
        "parked_time, "
        "Parking_Fee, "
        "Paid_Amt, "
        "Receipt_No, "
        "Status, "
        "Redeem_amt, "
        "Redeem_time, "
        "Redeem_no, "
        "gst_amt, "
        "chu_debit_code, "
        "Card_Type, "
        "Top_Up_Amt, "
        "uposbatchno, "
        "feefrom, "
        "lpn, "
        "Entry_ID, "
        "entry_time, "
        "exit_lpn_SID, "
        "EEPDSerialNo, "
        "EEPTransRoute, "
        "EEPPaymentResult, "
        "EEPPaymentTime, "
        "VCC"
        ") VALUES (" +
        exitTrans.xsid +
        ", '" +
        exitTrans.sExitTime +
        "', '" +
        exitTrans.sIUNo +
        "', '" +
        exitTrans.sCardNo +
        "', " +
        std::to_string(exitTrans.iTransType) +
        ", " +
        std::to_string(exitTrans.lParkedTime) +
        ", " +
        std::to_string(exitTrans.sFee) +
        ", " +
        std::to_string(exitTrans.sPaidAmt) +
        ", '" +
        exitTrans.sReceiptNo +
        "', " +
        std::to_string(exitTrans.iStatus) +
        ", " +
        std::to_string(exitTrans.sRedeemAmt) +
        ", " +
        std::to_string(exitTrans.iRedeemTime) +
        ", '" +
        exitTrans.sRedeemNo +
        "', " +
        std::to_string(exitTrans.sGSTAmt) +
        ", '" +
        exitTrans.sCHUDebitCode +
        "', " +
        std::to_string(exitTrans.iCardType) +
        ", " +
        std::to_string(exitTrans.sTopupAmt) +
        ", '" +
        exitTrans.uposbatchno +
        "', '" +
        exitTrans.feefrom +
        "', '" +
        exitTrans.lpn +
        "', " +
        std::to_string(exitTrans.iEntryID) +
        ", '" +
        exitTrans.sEntryTime +
        "', '" +
        transId +
        "', '" +
        exitTrans.sDSerialNo +
        "', " +
        std::to_string(exitTrans.iEEPTransRoute) +
        ", " +
        std::to_string(exitTrans.iEEPPaymentResult) +
        ", '" +
        exitTrans.sEEPpaymentTime +
        "', '" +
        exitTrans.VCC +
        "')";

    const int ret = localdb->SQLExecutNoneQuery(sqlStmt);

    if (ret != 0)
    {
        logDbMessage(sqlStmt, "DB");
        logDbMessage("Insert Exit_Trans to Local: fail.", "DB");

        return iLocalFail;
    }

    logDbMessage("Insert Exit_Trans to Local: success.", "DB");

    if (!updateOperationProcess(
            [](tProcess_Struct& process)
            {
                ++process.glNoofOfflineData;
            }))
    {
        logDbMessage("Unable to update Operation shared data.", "DB");
    }

    return iLocalSuccess;
}

int db::updateExitReceiptNo(const std::string& receiptNo, const std::string& stationId)
{
    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return -1;
    }

    const std::string exitTime = data->tExit.sExitTime;

    try
    {
        // =====================================================
        // Try Exit_trans_tmp first
        // =====================================================
        const std::string tmpSql =
            "UPDATE Exit_trans_tmp "
            "SET receipt_no = '" +
            receiptNo +
            "' "
            "WHERE station_id = '" +
            stationId +
            "' "
            "AND Exit_time = '" +
            exitTime +
            "'";

        int ret = centraldb->SQLExecutNoneQuery(tmpSql);

        if (ret != 0)
        {
            logDbMessage("Fail to update Receipt No to Exit_trans_tmp.", "DB");

            m_remote_db_err_flag.store(1);

            return ret;
        }

        if (centraldb->NumberOfRowsAffected > 0)
        {
            logDbMessage("Success update Receipt No to Exit_trans_tmp.", "DB");

            m_remote_db_err_flag.store(0);

            return ret;
        }

        // =====================================================
        // Not found in tmp, try Exit_trans
        // =====================================================
        const std::string exitSql =
            "UPDATE Exit_trans "
            "SET receipt_no = '" +
            receiptNo +
            "' "
            "WHERE station_id = '" +
            stationId +
            "' "
            "AND Exit_time = '" +
            exitTime +
            "'";

        ret = centraldb->SQLExecutNoneQuery(exitSql);

        if (ret != 0)
        {
            logDbMessage("Fail to update Receipt No to Exit_trans.", "DB");

            m_remote_db_err_flag.store(1);

            return ret;
        }

        m_remote_db_err_flag.store(0);

        if (centraldb->NumberOfRowsAffected > 0)
        {
            logDbMessage("Success update Receipt No to Exit_trans.", "DB");
        }
        else
        {
            logDbMessage("No Receipt record found for update.", "DB");
        }

        return ret;
    }
    catch (const std::exception& e)
    {
        logDbMessage("updateExitReceiptNo exception: " + std::string(e.what()), "DB");

        m_remote_db_err_flag.store(1);

        return -1;
    }
}

DBError db::LoadTariff()
{
    constexpr int kTariffPeriods = 9;
    constexpr int kMaxAttempts = 2;

    try
    {
        logDbMessage("Load tariff_setup: Started", "DB");

        // =====================================================
        // Try Local DB first.
        // If no data, download once and retry.
        // =====================================================
        for (int attempt = 0; attempt < kMaxAttempts; ++attempt)
        {
            std::vector<ReaderItem> result;

            const int ret =
                localdb->SQLSelect(
                    "SELECT * "
                    "FROM tariff_setup "
                    "ORDER BY day_index",
                    &result,
                    true);

            if (ret != 0)
            {
                m_local_db_err_flag = 1;

                logDbMessage("Load tariff parameters failed.", "DB");

                return iLocalFail;
            }

            m_local_db_err_flag = 0;

            // =================================================
            // No Local data - download once and retry
            // =================================================
            if (result.empty())
            {
                if (attempt == 0)
                {
                    downloadtariffsetup();
                    continue;
                }

                logDbMessage("Load tariff parameters error: no data in local DB.", "DB");

                return iNoData;
            }

            // =================================================
            // Load tariff records
            // =================================================
            for (const auto& row : result)
            {
                tariff_struct tariff{};

                int index = 0;

                tariff.tariff_id = row.GetDataItem(index++);
                tariff.day_index = row.GetDataItem(index++);

                // =============================================
                // Load 9 tariff periods
                // =============================================
                for (int period = 0; period < kTariffPeriods; ++period)
                {
                    tariff.start_time[period] = row.GetDataItem(index++);

                    if (tariff.start_time[period] == "NULL")
                    {
                        tariff.start_time[period].clear();
                    }

                    tariff.end_time[period] = row.GetDataItem(index++);

                    if (tariff.end_time[period] == "NULL")
                    {
                        tariff.end_time[period].clear();
                    }

                    tariff.rate_type[period] = row.GetDataItem(index++);
                    tariff.charge_time_block[period] = row.GetDataItem(index++);
                    tariff.charge_rate[period] = row.GetDataItem(index++);
                    tariff.grace_time[period] = row.GetDataItem(index++);
                    tariff.min_charge[period] = row.GetDataItem(index++);
                    tariff.max_charge[period] = row.GetDataItem(index++);
                    tariff.first_free[period] = row.GetDataItem(index++);
                    tariff.first_add[period] = row.GetDataItem(index++);
                    tariff.second_free[period] = row.GetDataItem(index++);
                    tariff.second_add[period] = row.GetDataItem(index++);
                    tariff.third_free[period] = row.GetDataItem(index++);
                    tariff.third_add[period] = row.GetDataItem(index++);
                    tariff.allowance[period] = row.GetDataItem(index++);
                }

                // =============================================
                // Load daily tariff settings
                // =============================================
                tariff.whole_day_max = row.GetDataItem(index++);
                tariff.whole_day_min = row.GetDataItem(index++);
                tariff.zone_cutoff = row.GetDataItem(index++);
                tariff.day_cutoff = row.GetDataItem(index++);
                tariff.day_type = row.GetDataItem(index++);

                logDbMessage("Loading Tday_Type = " + tariff.day_type, "DB");

                // =============================================
                // Load each day type into RAM
                // =============================================
                std::vector<std::string> dayTypes;

                boost::algorithm::split(dayTypes, tariff.day_type, boost::algorithm::is_any_of(","));

                for (const auto& dayType : dayTypes)
                {
                    // Handles trailing comma safely.
                    if (dayType.empty())
                    {
                        continue;
                    }

                    tariff.dtype = std::stoi(dayType);

                    WriteTariff2RAM(tariff);
                }
            }

            logDbMessage("Load tariff parameters: success", "DB");

            return iDBSuccess;
        }
    }
    catch (const std::exception& e)
    {
        logDbMessage("Load tariff parameters error: " + std::string(e.what()), "DB");

        m_local_db_err_flag = 1;

        return iLocalFail;
    }

    return iNoData;
}

int db::WriteTariff2RAM(const tariff_struct& tariff)
{
    constexpr int kTariffPeriods = 9;

    // =========================================================
    // Calculate tariff RAM position
    // =========================================================
    int groupIndex = tariff.dtype / 8;
    int dayIndex = tariff.dtype % 8;

    if (dayIndex == 0)
    {
        dayIndex = 8;
        --groupIndex;

        if (groupIndex < 0)
        {
            groupIndex = 0;
        }
    }

    // =========================================================
    // Write tariff to RAM
    // =========================================================
    auto& target = gtariff[groupIndex][dayIndex];

    target.day_type = std::to_string(tariff.dtype);
    target.tariff_id = tariff.tariff_id;
    target.day_index = tariff.day_index;

    for (int period = 0; period < kTariffPeriods; ++period)
    {
        target.start_time[period] = tariff.start_time[period];
        target.end_time[period] = tariff.end_time[period];
        target.rate_type[period] = tariff.rate_type[period];
        target.charge_time_block[period] = tariff.charge_time_block[period];
        target.charge_rate[period] = tariff.charge_rate[period];
        target.grace_time[period] = tariff.grace_time[period];
        target.first_free[period] = tariff.first_free[period];
        target.first_add[period] = tariff.first_add[period];
        target.second_free[period] = tariff.second_free[period];
        target.second_add[period] = tariff.second_add[period];
        target.third_free[period] = tariff.third_free[period];
        target.third_add[period] = tariff.third_add[period];
        target.allowance[period] = tariff.allowance[period];
        target.min_charge[period] = tariff.min_charge[period];
        target.max_charge[period] = tariff.max_charge[period];
    }

    target.zone_cutoff = tariff.zone_cutoff;
    target.day_cutoff = tariff.day_cutoff;
    target.whole_day_max = tariff.whole_day_max;
    target.whole_day_min = tariff.whole_day_min;

    return 1;
}

DBError db::LoadHoliday()
{
    constexpr int kMaxAttempts = 2;

    try
    {
        logDbMessage("Load holiday: Started", "DB");

        // =====================================================
        // Try Local DB first.
        // If no data, download once and retry.
        // =====================================================
        for (int attempt = 0; attempt < kMaxAttempts; ++attempt)
        {
            std::vector<ReaderItem> result;

            const std::string sqlStmt =
                "SELECT DATE_FORMAT("
                "holiday_date, '%Y-%m-%d') "
                "AS YourDateAsString "
                "FROM holiday_mst "
                "ORDER BY holiday_date";

            const int ret = localdb->SQLSelect(sqlStmt, &result, true);

            if (ret != 0)
            {
                m_local_db_err_flag = 1;

                logDbMessage("Load holiday failed.", "DB");

                return iLocalFail;
            }

            m_local_db_err_flag = 0;

            // =================================================
            // No Local data - download once and retry
            // =================================================
            if (result.empty())
            {
                if (attempt == 0)
                {
                    downloadholidaymst();
                    continue;
                }

                logDbMessage("Load holiday error: no data in local DB.", "DB");

                return iNoData;
            }

            // =================================================
            // Load holidays into RAM
            // =================================================
            msholiday.clear();

            for (const auto& row : result)
            {
                if (row.getDataSize() < 1)
                {
                    continue;
                }

                msholiday.push_back(row.GetDataItem(0));
            }

            logDbMessage("Load holiday: success", "DB");

            return iDBSuccess;
        }
    }
    catch (const std::exception& e)
    {
        logDbMessage("Load holiday error: " + std::string(e.what()), "DB");

        m_local_db_err_flag = 1;

        return iLocalFail;
    }

    return iNoData;
}

DBError db::ClearHoliday()
{
    try
    {
        const int ret = localdb->SQLExecutNoneQuery("TRUNCATE TABLE holiday_mst");

        if (ret != 0)
        {
            m_local_db_err_flag = 1;

            logDbMessage("Failed to clear holiday_mst.", "DB");

            return iLocalFail;
        }

        m_local_db_err_flag = 0;

        return iDBSuccess;
    }
    catch (const std::exception& e)
    {
        logDbMessage("DB: Local DB error while clearing holiday_mst: " + std::string(e.what()), "DB");

        m_local_db_err_flag = 1;

        return iLocalFail;
    }
}

int db::GetDayTypeWithHE(CE_Time currDate)
{
    constexpr int kHolidayDayType = 7;
    constexpr int kHolidayEveDayType = 8;
    constexpr std::time_t kSecondsPerDay = 24 * 60 * 60;

    const int dayType = currDate.getweekday();

    // =========================================================
    // Current date
    // =========================================================
    const std::string currentDate = currDate.DateString();

    const auto isHoliday =
        [this](const std::string& date)
        {
            return std::find(
                       msholiday.begin(),
                       msholiday.end(),
                       date) != msholiday.end();
        };

    // Current date is holiday
    if (isHoliday(currentDate))
    {
        return kHolidayDayType;
    }

    // =========================================================
    // Check holiday eve
    // =========================================================
    if (dayType != 6 && dayType != 7)
    {
        CE_Time nextDate;

        nextDate.SetTime(currDate.GetUnixTimestamp() + kSecondsPerDay);

        if (isHoliday(nextDate.DateString()))
        {
            return kHolidayEveDayType;
        }
    }

    return dayType;
}

int db::GetDayTypeNoPE(CE_Time currDate)
{
    constexpr int kHolidayDayType = 8;
    const int dayType = currDate.getweekday();
    const std::string currentDate = currDate.DateString();

    const auto it =
        std::find(
            msholiday.begin(),
            msholiday.end(),
            currentDate);

    if (it != msholiday.end())
    {
        return kHolidayDayType;
    }

    return dayType;
}

int db::GetDayType(CE_Time currDate)
{
    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return -1;
    }

    if (data->tParas.giHasHolidayEve == 1)
    {
        return GetDayTypeWithHE(currDate);  //PH is 7, EvePH is 8
    }

    return GetDayTypeNoPE(currDate);
}

float db::HasPaidWithinPeriod(const std::string& timeFrom, const std::string& timeTo)
{
    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return 0.0F;
    }

    // =========================================================
    // Get current IU / Ticket No.
    // =========================================================
    const std::string currentIU =
        (data->gtStation.iType == tientry)
            ? data->tEntry.sIUTKNo
            : data->tExit.sIUNo;

    const bool isNormalIU = (currentIU.length() == 10);

    const bool isMotorcycleIU = (data->tParas.giMCyclePerDay == 2 && currentIU.length() == 16);

    if (!isNormalIU &&
        !isMotorcycleIU)
    {
        return 0.0F;
    }

    // timeTo is not used by the existing implementation.
    // Keep this explicit until the required period logic is confirmed.
    (void)timeTo;

    // =========================================================
    // Check Central DB first
    // =========================================================
    std::string centralSql =
        "SELECT paid_amt "
        "FROM exit_trans "
        "WHERE iu_tk_no = '" +
        currentIU +
        "' "
        "AND paid_amt > 0";

    if (currentIU.length() == 16)
    {
        centralSql +=
            " AND (trans_type = 7 OR trans_type = 22)";
    }

    centralSql +=
        " AND CONVERT(char(19), exit_time, 120) > '" +
        timeFrom +
        "'";

    std::vector<ReaderItem> centralResult;

    const int centralRet = centraldb->SQLSelect(centralSql, &centralResult, true);

    if (centralRet == 0)
    {
        m_remote_db_err_flag.store(0);

        if (!centralResult.empty())
        {
            try
            {
                const float paidAmount = std::stof(centralResult.front().GetDataItem(0));

                logDbMessage("Paid fee is: " + std::to_string(paidAmount), "DB");

                return paidAmount;
            }
            catch (const std::exception& e)
            {
                logDbMessage("Invalid paid amount from Central DB: " + std::string(e.what()), "DB");

                return -1.0F;
            }
        }
    }
    else
    {
        m_remote_db_err_flag.store(1);
    }

    // =========================================================
    // Not found / Central failed - check Local DB
    // =========================================================
    const std::string localSql =
        "SELECT paid_amt "
        "FROM entry_trans "
        "WHERE iu_tk_no = '" +
        currentIU +
        "' "
        "AND paid_amt > 0";

    std::vector<ReaderItem> localResult;

    const int localRet = localdb->SQLSelect(localSql, &localResult, true);

    if (localRet != 0)
    {
        m_local_db_err_flag = 1;
        return -1.0F;
    }

    m_local_db_err_flag = 0;

    if (localResult.empty())
    {
        return -1.0F;
    }

    try
    {
        const float paidAmount = std::stof(localResult.front().GetDataItem(0));

        logDbMessage("Paid fee is: " + std::to_string(paidAmount), "DB");

        return paidAmount;
    }
    catch (const std::exception& e)
    {
        logDbMessage("Invalid paid amount from Local DB: " + std::string(e.what()), "DB");

        return -1.0F;
    }
}

float db::RoundIt(float value, int tariffFeeMode)
{
    switch (tariffFeeMode)
    {
        case 1:
            return std::ceil(value);   // Round up

        case 2:
            return std::floor(value);  // Round down

        case 3:
            return std::round(value);  // Normal rounding

        default:
            return value;              // No rounding
    }
}

float db::CalFeeRAM2G(
    const std::string& entryTime,
    const std::string& payTime,
    int transType,
    bool noGraceTime)
{
    constexpr int kTariffTypeMultiplier = 40;
    constexpr float kTariffError = -4.0F;

    bool usedTariff[2] =
    {
        false,
        false
    };

    CE_Time entryDateTime;
    CE_Time payDateTime;
    CE_Time calTime;

    entryDateTime.SetTime(entryTime);
    payDateTime.SetTime(payTime);

    // =========================================================
    // Determine which tariff type is used
    // =========================================================
    for (int index = 0; index < 2; ++index)
    {
        if (entryTime <= gtarifftypeinfo[index].start_time)
        {
            continue;
        }

        // Entire parking period is within this tariff type
        if (gtarifftypeinfo[index].end_time > payTime)
        {
            usedTariff[index] = true;
            break;
        }

        // Entry is within this tariff type,
        // but payment time has crossed its end time.
        if (entryTime < gtarifftypeinfo[index].end_time)
        {
            if (index == 0)
            {
                usedTariff[0] = true;
                usedTariff[1] = true;
                break;
            }

            logDbMessage("Tariff type info Error.", "DB");

            return kTariffError;
        }
    }

    try
    {
        float totalFee = 0.0F;

        // =====================================================
        // Cross two tariff types
        // =====================================================
        if (usedTariff[0] && usedTariff[1])
        {
            const int graceTime = std::stoi(gtariff[transType][1].grace_time[0]);

            const int timeDiff =
                calTime.diffmin(
                    entryDateTime.GetUnixTimestamp(),
                    payDateTime.GetUnixTimestamp());

            if (timeDiff <= graceTime)
            {
                logDbMessage("Within grace period.", "DB");

                return 0.0F;
            }

            // -------------------------------------------------
            // First tariff type
            // -------------------------------------------------
            int tariffTypeOffset = std::stoi(gtarifftypeinfo[0].tariff_type) * kTariffTypeMultiplier;

            totalFee =
                CalFeeRAM2GR(
                    entryTime,
                    gtarifftypeinfo[0].end_time,
                    transType + tariffTypeOffset,
                    true);

            logDbMessage("Fee For Early Tariff: " + Common::getInstance()->SetFeeFormat(totalFee), "DB");

            // -------------------------------------------------
            // Second tariff type
            // -------------------------------------------------
            tariffTypeOffset = std::stoi(gtarifftypeinfo[1].tariff_type) * kTariffTypeMultiplier;

            const float currentTariffFee =
                CalFeeRAM2GR(
                    gtarifftypeinfo[1].start_time,
                    payTime,
                    transType + tariffTypeOffset,
                    true);

            logDbMessage("Fee For Current Tariff: " + Common::getInstance()->SetFeeFormat(currentTariffFee), "DB");

            totalFee += currentTariffFee;
        }
        else
        {
            // =================================================
            // Use one tariff type
            // =================================================
            const int tariffIndex = usedTariff[0] ? 0 : 1;

            logDbMessage("Use Tariff Type: " + gtarifftypeinfo[tariffIndex].tariff_type, "DB");

            const int tariffTypeOffset = std::stoi(gtarifftypeinfo[tariffIndex].tariff_type) * kTariffTypeMultiplier;

            totalFee =
                CalFeeRAM2GR(
                    entryTime,
                    payTime,
                    transType + tariffTypeOffset,
                    noGraceTime);
        }

        logDbMessage("Total Parking Fee: " + Common::getInstance()->SetFeeFormat(totalFee), "DB");

        return totalFee;
    }
    catch (const std::exception& e)
    {
        logDbMessage("CalFeeRAM2G tariff data error: " + std::string(e.what()), "DB");

        return kTariffError;
    }
}

float db::CalFeeRAM2GR(string eTime, string payTime,int iTransType, bool bNoGT) 
{
    CE_Time entryTime;
    CE_Time payDT;
    CE_Time calTime;
    CE_Time currentTime;
    CE_Time PD,pt;
    CE_Time Lpd,Npd;
    entryTime.SetTime(eTime);
    payDT.SetTime(payTime);
    float dayFee=0, zoneFee=0;
    int iDayType;
    bool bFirstFreed = false; // for first free time, only once
    bool bGotDayInfo = false; // for get day info, only once for a day
    bool b24HourBlock = false;
    //bool bGT = false; //for grace time, only valid for 1st zone
    bool bMCPerDayChecked= false;
    int i24HourBlocks;
    int s24HourFee=0, s24HourCharges=0;
    string dtStr;
    CE_Time zt;

    const auto operationData = operation::getInstance()->FnGetSharedData();
    if (!operationData)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");
        return -1.0F;
    }

    const auto& paras = operationData->tParas;

    string sT[9],eT[9];
    int rateType[10],GT[10];
    float chargeRate[9];
    int CTB[9];
    float zoneMin[9],zoneMax[9];
    float firstAdd[9],secondAdd[9],thirdAdd[9];
    int firstFree[9],secondFree[9],thirdFree[9];
    int iAllowance[9];
    int maxZone, zoneRateType;
    int iZoneCutoff,iDayCutoff;
    float dayMin, dayMax;
    int iNextGT,iNextRT;
    CE_Time zoneTime[10];
    
    int currentZone, currentRateType;
    int currentCTB, currentGT;
    float currentRate=0, currentMin=0, currentMax=0;
    float currentAdd, currentAdd2, currentAdd3;
    int currentFree, currentFree2, currentFree3;
    int currentAllowance;
    float charge=0;
    int timediff;
    float haspaid;
    float iRet=0;

    if(bNoGT== true) 
        logDbMessage( "Calculate parking fee with no grace .... ", "DB");
    else 
        logDbMessage("Calculate parking fee for rate type: "+ to_string(iTransType), "DB");
    //logDbMessage("Entry Time: " + entryTime.DateTimeString(), "DB");
    //logDbMessage("Pay Time: " + payDT.DateTimeString(), "DB");
    
    timediff = calTime.diffmin(entryTime.GetUnixTimestamp(), payDT.GetUnixTimestamp());

    if(timediff<0) return(0);

    long a,b;

    a=entryTime.Second();
    b=payDT.Second();
    
    if (a<b)
    {
        payDT.SetTime(payDT.GetUnixTimestamp()+60);
        //logDbMessage("add one minutes to PayTime: " + payDT.DateTimeStringNoS(), "DB");

    }
    currentTime.SetTime(payDT.GetUnixTimestamp());

    PD.SetTime(entryTime.Year(),entryTime.Month(),entryTime.Day(),0,0,0);
    pt.SetTime(entryTime.GetUnixTimestamp());
    
    logDbMessage("Cal fee time: " + pt.DateTimeStringNoS() + " ~ " + currentTime.DateTimeStringNoS() , "DB");
    
    while(1)
    {
        timediff=calTime.diffday(PD.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
        if(timediff<0)
        break;
        if(bGotDayInfo==false)
        {
            dayFee=0;
            Lpd.SetTime(PD.GetUnixTimestamp()-86400);
            iDayType=GetDayType(Lpd);
            //logDbMessage("last day Type:" + to_string(iDayType), "DB");
            if(gtariff[iTransType][iDayType].start_time[0].empty())
            {
                //logDbMessage("No Tariff defined for DayType: "+to_string(iDayType), "DB");
                return(-2); //No Tariff defined for DayType
            }
            maxZone=0;
            for(int k=1;k<10;k++){

                if(gtariff[iTransType][iDayType].end_time[k-1].compare(gtariff[iTransType][iDayType].start_time[0])==0)
                {
                    maxZone=k;
                    //logDbMessage("Max zone for last day is: " + to_string(maxZone), "DB");
                    break;
                }
            };
            
            if(maxZone==0) return(-4); //time zone wrong
            // put last zone of last day in array(0)
            rateType[0]=std::stoi(gtariff[iTransType][iDayType].rate_type[maxZone-1]);
            chargeRate[0]=std::stod(gtariff[iTransType][iDayType].charge_rate[maxZone-1]);
            CTB[0]=std::stoi(gtariff[iTransType][iDayType].charge_time_block[maxZone-1]);
            zoneMin[0]=std::stod(gtariff[iTransType][iDayType].min_charge[maxZone-1]);
            zoneMax[0]=std::stod(gtariff[iTransType][iDayType].max_charge[maxZone-1]);
            GT[0]=std::stoi(gtariff[iTransType][iDayType].grace_time[maxZone-1]);
            firstAdd[0]=std::stod(gtariff[iTransType][iDayType].first_add[maxZone-1]);
            firstFree[0]=std::stoi(gtariff[iTransType][iDayType].first_free[maxZone-1]);
            secondAdd[0]=std::stod(gtariff[iTransType][iDayType].second_add[maxZone-1]);
            secondFree[0]=std::stoi(gtariff[iTransType][iDayType].second_free[maxZone-1]);
            thirdAdd[0]=std::stod(gtariff[iTransType][iDayType].third_add[maxZone-1]);
            thirdFree[0]=std::stoi(gtariff[iTransType][iDayType].third_free[maxZone-1]);
            iAllowance[0]=std::stoi(gtariff[iTransType][iDayType].allowance[maxZone-1]);
            zt.SetTime(gtariff[iTransType][iDayType].start_time[maxZone-1]);
            
            dtStr=Lpd.DateString()+" "+ zt.TimeString();

            zoneTime[0].SetTime(dtStr);
            //logDbMessage ("lastest Zone time for last day start:" + zoneTime[0].DateTimeString(), "DB");
            //get day of PD
            iDayType=GetDayType(PD);
            //logDbMessage("Current day Type: "+ std::to_string(iDayType), "DB");
            if(gtariff[iTransType][iDayType].start_time[0].empty())
            {
                logDbMessage ("No Tariff defined for Daytype: "+ to_string(iDayType), "DB");
                return(-2); //No Tariff defined for DayType
            }

            maxZone=0;
            for(int k=1;k<10;k++){
                if(gtariff[iTransType][iDayType].end_time[k-1].compare(gtariff[iTransType][iDayType].start_time[0])==0)
                {
                    maxZone=k;
                    //logDbMessage ("max zone for current day is: "+ std:: to_string(maxZone),"DB");
                    break;
                }
            }
            //---------------------
            if(maxZone==0) return(-4); //time zone wrong
            for(int k=1;k<=maxZone;k++){
                rateType[k]=std::stoi(gtariff[iTransType][iDayType].rate_type[k-1]);
                chargeRate[k]=std::stod(gtariff[iTransType][iDayType].charge_rate[k-1]);
                CTB[k]=std::stoi(gtariff[iTransType][iDayType].charge_time_block[k-1]);
                zoneMin[k]=std::stod(gtariff[iTransType][iDayType].min_charge[k-1]);
                zoneMax[k]=std::stod(gtariff[iTransType][iDayType].max_charge[k-1]);
                GT[k]=std::stoi(gtariff[iTransType][iDayType].grace_time[k-1]);
                firstAdd[k]=std::stod(gtariff[iTransType][iDayType].first_add[k-1]);
                firstFree[k]=std::stoi(gtariff[iTransType][iDayType].first_free[k-1]);
                secondAdd[k]=std::stod(gtariff[iTransType][iDayType].second_add[k-1]);
                secondFree[k]=std::stoi(gtariff[iTransType][iDayType].second_free[k-1]);
                thirdAdd[k]=std::stod(gtariff[iTransType][iDayType].third_add[k-1]);
                thirdFree[k]=std::stoi(gtariff[iTransType][iDayType].third_free[k-1]);
                iAllowance[k]=std::stoi(gtariff[iTransType][iDayType].allowance[k-1]);
                
                //logDbMessage("Zone time from db is: " + gtariff[iTransType][iDayType].start_time[k-1], "DB");
                zt.SetTime(gtariff[iTransType][iDayType].start_time[k-1]);
                //logDbMessage("Zone time convert is: " + zt.DateTimeString(), "DB");
                dtStr=PD.DateString()+" "+zt.TimeString();
                
                //logDbMessage("Zone time plus pd date is: " + dtStr, "DB");
                zoneTime[k].SetTime(dtStr);
                //logDbMessage("time zone" + std::to_string(k) + " start: " + zoneTime[k].DateTimeString(), "DB");
            }
            dayMin = std::stod(gtariff[iTransType][iDayType].whole_day_min);
            dayMax = std::stod(gtariff[iTransType][iDayType].whole_day_max);
            iZoneCutoff = std::stoi(gtariff[iTransType][iDayType].zone_cutoff);
            iDayCutoff = std::stoi(gtariff[iTransType][iDayType].day_cutoff);

            //logDbMessage("daymin =" + Common::getInstance()->SetFeeFormat(dayMin), "DB");
            //logDbMessage("dayMax = "+ Common::getInstance()->SetFeeFormat(dayMax), "DB");
            //get firstzone for next day
            Npd.SetTime(PD.GetUnixTimestamp()+86400);      // add one day
            iDayType=GetDayType(Npd);
            //logDbMessage("Next day type: "+ to_string(iDayType), "DB");
            if(gtariff[iTransType][iDayType].start_time[0].empty())
            {
                logDbMessage ("No tariff defined for DayType: " + to_string(iDayType), "DB");
                return(-2); //No Tariff defined for DayType
            }
            iNextGT = stoi(gtariff[iTransType][iDayType].grace_time[0]);
            iNextRT = stoi(gtariff[iTransType][iDayType].rate_type[0]);
            zt.SetTime(gtariff[iTransType][iDayType].start_time[0]);
            if(iDayCutoff==1)
                dtStr=Npd.DateString()+" 00:00:00";
            else
                dtStr=Npd.DateString()+" "+zt.TimeString();
            //logDbMessage("zone time is:" + dtStr,"DB");
            zoneTime[maxZone+1].SetTime(dtStr);
            //logDbMessage("Next day zone time start: " + zoneTime[maxZone+1].DateTimeString(), "DB");
            GT[maxZone+1]=iNextGT;
            rateType[maxZone+1]=iNextRT;
            bGotDayInfo = true;
            
            if((dayMin==dayMax)&&(dayMin>0))
            {
                //logDbMessage ("In 24hrmode","DB");
                b24HourBlock= true;
                s24HourFee=dayMin;
                dayMin=0;
                dayMax=0;
            }
            else
            {
            //	logDbMessage ("Out 24hrmode","DB");
                b24HourBlock= false;
                i24HourBlocks=0;
            }
            
        }       // end bGotDayInfo==false
        //if defined 24 hour block, and blocks>0
        if(b24HourBlock == true)
        {
            timediff=calTime.diffhour(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
            i24HourBlocks=timediff/24;
        }
        else
            i24HourBlocks=0;
        
        if(i24HourBlocks>0)
        {
            s24HourCharges=s24HourCharges+s24HourFee;
            pt.SetTime(pt.GetUnixTimestamp()+86400);
            currentRateType=0;
            bNoGT= true;
            //logDbMessage("24hr charge: " + s24HourCharges, "DB");
        }
        else
        {
            for(int k=1;k<=maxZone+1;k++)
            {
                timediff=calTime.diffmin(pt.GetUnixTimestamp(), zoneTime[k].GetUnixTimestamp());
                
                if(timediff>0)
                {
                    currentZone=k-1;
                    //logDbMessage("Enter to zone" + std::to_string(k-1) , "DB");
                    break;
                }
            };
            //logDbMessage("current zone is: " + std::to_string(currentZone), "DB");
            
            currentRateType = rateType[currentZone];
            currentRate = chargeRate[currentZone];
            currentCTB = CTB[currentZone];
            currentMin = zoneMin[currentZone];
            currentMax = zoneMax[currentZone];
            currentGT = GT[currentZone];
            currentAdd = firstAdd[currentZone];
            currentFree = firstFree[currentZone];

            currentAdd2 = secondAdd[currentZone];
            currentFree2 = secondFree[currentZone];
            currentAdd3 = thirdAdd[currentZone];
            currentFree3 = thirdFree[currentZone];

            currentAllowance = iAllowance[currentZone];
            
            if((currentRateType<1)||(currentRateType>2))
            return(-3);
            //logDbMessage("currentRateType: " + std::to_string(currentRateType), "DB");
            //logDbMessage("current Rate: "+ Common::getInstance()->SetFeeFormat(currentRate),"DB");
            //logDbMessage("currentCTB: "+ std::to_string(currentCTB),"DB");
            //logDbMessage("currentFree: " + Common::getInstance()->SetFeeFormat(currentFree),"DB");
            //logDbMessage("currentAdd: " + Common::getInstance()->SetFeeFormat(currentAdd),"DB");
            //logDbMessage("currentFree2: " + Common::getInstance()->SetFeeFormat(currentFree2),"DB");
            //logDbMessage("currentAdd2: " + Common::getInstance()->SetFeeFormat(currentAdd2),"DB");
            //logDbMessage("currentFree3: " + Common::getInstance()->SetFeeFormat(currentFree3),"DB");
            //logDbMessage("currentAdd3: " + Common::getInstance()->SetFeeFormat(currentAdd3),"DB");
            //logDbMessage("currentGT: "+ std::to_string(currentGT),"DB");
            //logDbMessage("currentMax: "+ Common::getInstance()->SetFeeFormat(currentMax),"DB");
            //logDbMessage("currentMin: "+ Common::getInstance()->SetFeeFormat(currentMin),"DB");
            //logDbMessage("currentAllowance: "+ Common::getInstance()->SetFeeFormat(currentAllowance),"DB");
        }
        
        // check grace time
        
        if(bNoGT==false)
        {
            if(currentGT>0)
            {
                //logDbMessage("fee start time: " + pt.DateTimeString(),"DB");
                //logDbMessage("cal fee time: " + currentTime.DateTimeString(),"DB");
                //---------
                timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
                //logDbMessage("parked time = " + std::to_string(timediff),"DB");
                if(timediff>currentGT)
                {
                //	logDbMessage("No grace time","DB");
                    bNoGT=true;
                }
                else
                {
                    //logDbMessage("within grace period","DB");
                    return(0);
                }
            }
            else
                    bNoGT=true;
        }
        
        // push time, get zone fee
        zoneFee =0;

        if(currentRateType==1)         // hourly charge
        {
            if (currentCTB==0)
                return(-3);
            if(currentAllowance>0)
            {
                timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
                //logDbMessage("time diff for allowance is "+ timediff,"DB");
                if(((charge>0)||(dayFee>0))&&(timediff<=currentAllowance))
                {
                    //logDbMessage("within allowance, no change","DB");
                    break;
                }
            };
            
            if(currentFree>0)
            {
                if(((paras.giFirstHourMode==0)||((dayFee==0)&&(charge==0)))&& (bFirstFreed==false))
                {
                    //logDbMessage("gifirsthour: "+ std::to_string(paras.giFirstHour),"DB");

                    if(paras.giFirstHour>0)
                    {
                        timediff=calTime.diffmin(pt.GetUnixTimestamp(), zoneTime[currentZone+1].GetUnixTimestamp());
                        if(timediff<=currentFree)
                        {
                            if(iZoneCutoff==1)
                                pt.SetTime(zoneTime[currentZone+1].DateTimeString());
                            else
                                pt.SetTime(pt.GetUnixTimestamp()+(currentFree*60));

                            //logDbMessage("New pt time in first mode: "+ pt.DateTimeString(),"DB");
                            zoneFee = currentAdd;
                            //--- add for change per mins charge
                            timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
                            if(timediff<=0)
                            {
                                dayFee=dayFee + zoneFee;
                                break;
                            }
                            else
                            {
                                if((paras.giFirstHour>1)&&(iZoneCutoff==0))
                                {
                                    
                                    timediff=calTime.diffmin(pt.GetUnixTimestamp(), zoneTime[currentZone+1].GetUnixTimestamp());
                                    if((timediff<=0)&&(rateType[currentZone+1]==2))
                                    {
                                        zoneFee= currentAdd;
                                        goto SettleFirstFree1;
                                    }
                                    pt.SetTime(pt.GetUnixTimestamp()+(currentFree2*60));
                                    zoneFee = currentAdd + currentAdd2;
                                    timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
                                    if(timediff<=0)
                                    {
                                        dayFee=dayFee + zoneFee;
                                        break;
                                    }
                                    else
                                    {
                                        if(paras.giFirstHour>2)
                                        {
                                            timediff=calTime.diffmin(pt.GetUnixTimestamp(), zoneTime[currentZone+1].GetUnixTimestamp());
                                            if((timediff<=0)&& (rateType[currentZone+1]==2))
                                            {
                                                zoneFee = currentAdd + currentAdd2;
                                                goto SettleFirstFree1;
                                            }
                                            pt.SetTime(pt.GetUnixTimestamp()+(currentFree3*60));
                                            zoneFee = currentAdd + currentAdd2 + currentAdd3;
                                            timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
                                            if(timediff<=0)
                                            {
                                                dayFee=dayFee + zoneFee;
                                                break;
                                            }
                                        }
                                    }
                                }

SettleFirstFree1:
                                if(paras.giFirstHourMode==1)
                                bFirstFreed=true;
                            }
                        }
                        else
                        {
                            zoneFee = currentAdd;
                            pt.SetTime(pt.GetUnixTimestamp()+(currentFree*60));
                            timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
                            if(timediff<=0)
                            {
                                dayFee=dayFee + zoneFee;
                                break;
                            }
                            else
                            {
                                if(paras.giFirstHour>1)
                                {
                                    timediff=calTime.diffmin(pt.GetUnixTimestamp(), zoneTime[currentZone+1].GetUnixTimestamp());
                                    if(timediff<= currentFree2)
                                    {
                                        if(iZoneCutoff==1)
                                        pt.SetTime(zoneTime[currentZone+1].DateTimeString());
                                        else
                                        pt.SetTime(pt.GetUnixTimestamp()+(currentFree2*60));
                                        //logDbMessage( "New pt time in first mode 2 "+pt.DateTimeString(),"DB");
                                        zoneFee = currentAdd + currentAdd2;
                                        timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
                                        if(timediff<=0)
                                        {
                                            dayFee=dayFee + zoneFee;
                                            break;
                                        }
                                        else
                                        {
                                            if((paras.giFirstHour>2)&&(iZoneCutoff==0))
                                            {
                                                timediff=calTime.diffmin(pt.GetUnixTimestamp(), zoneTime[currentZone+1].GetUnixTimestamp());
                                                if((timediff<=0)&& (rateType[currentZone+1]==2))
                                                {
                                                    zoneFee = currentAdd + currentAdd2;
                                                    goto SettleFirstFree2;
                                                }
                                                pt.SetTime(pt.GetUnixTimestamp()+(currentFree3*60));
                                                zoneFee = currentAdd + currentAdd2 + currentAdd3;
                                                timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
                                                if(timediff<=0)
                                                {
                                                    dayFee=dayFee + zoneFee;
                                                    break;
                                                }
                                            }
SettleFirstFree2:
                                            if(paras.giFirstHourMode==1)
                                            bFirstFreed=true;
                                        }
                                    }
                                    else
                                    {
                                        zoneFee = currentAdd + currentAdd2;
                                        pt.SetTime(pt.GetUnixTimestamp()+(currentFree2*60));
                                        //logDbMessage( "2nd New pt time"+pt.DateTimeString(),"DB");
                                        timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
                                        if(timediff<=0)
                                        {
                                            dayFee=dayFee + zoneFee;
                                            break;
                                        }
                                        else
                                        {
                                            if(paras.giFirstHour>2)
                                            {
                                                timediff=calTime.diffmin(pt.GetUnixTimestamp(), zoneTime[currentZone+1].GetUnixTimestamp());
                                                if(timediff<= currentFree3)
                                                {
                                                    if(iZoneCutoff==1)
                                                    pt.SetTime(zoneTime[currentZone+1].DateTimeString());
                                                    else
                                                    pt.SetTime(pt.GetUnixTimestamp()+(currentFree3*60));
                                                    //logDbMessage( "New pt time in first mode 3 "+pt.DateTimeString(),"DB");
                                                    zoneFee = currentAdd + currentAdd2 + currentAdd3;
                                                    timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
                                                    if(timediff<=0)
                                                    {
                                                        dayFee=dayFee + zoneFee;
                                                        break;
                                                    }
                                                    else
                                                    {
                                                        if(paras.giFirstHourMode==1)
                                                        bFirstFreed=true;
                                                    }
                                                }
                                                else
                                                {
                                                    
                                                    zoneFee = currentAdd + currentAdd2 + currentAdd3;
                                                    pt.SetTime(pt.GetUnixTimestamp()+(currentFree3*60));
                                                    //logDbMessage( "3rd New pt time"+pt.DateTimeString(),"DB");
                                                    timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
                                                    if(timediff<=0)
                                                    {
                                                        dayFee=dayFee + zoneFee;
                                                        break;
                                                    }
                                                    else
                                                    {
                                                        if(paras.giFirstHourMode==1)
                                                        bFirstFreed = true;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            //logDbMessage("Zone Fee after first hour mode is "+Common::getInstance()->SetFeeFormat(zoneFee),"DB");
            //  one time zone  while 
            while(1)
            {
                timediff=calTime.diffmin(pt.GetUnixTimestamp(), zoneTime[currentZone+1].GetUnixTimestamp());
                
                if(timediff<=0) break;
                
                if(currentAllowance>0)
                {
                    timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
                    if(((charge>0)||(dayFee>0)||(zoneFee>0))&&(timediff<=currentAllowance))
                    {
                        //logDbMessage("within allowance, no change","DB");
                    }
                    else
                    zoneFee = zoneFee + currentRate;
                }
                else
                zoneFee = zoneFee + currentRate;
                
                if((currentMax>0)&&(zoneFee>=currentMax))
                {
                    pt.SetTime(zoneTime[currentZone+1].DateTimeString());
                    break;
                }
                pt.SetTime(pt.GetUnixTimestamp()+(currentCTB*60));
                timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
                if(timediff<=0) break;
            }   // end while for one time zone
            
            if(iZoneCutoff==1)
            {
                timediff=calTime.diffmin(pt.GetUnixTimestamp(), zoneTime[currentZone+1].GetUnixTimestamp());
                if(timediff<0)
                {
                    if((paras.giHr2PEAllowance>0)&& (rateType[currentZone+1]==2))
                    {
                        timediff=calTime.diffmin(entryTime.GetUnixTimestamp(), payDT.GetUnixTimestamp());
                        if(timediff>paras.giHr2PEAllowance)
                        {
                            pt.SetTime(zoneTime[currentZone+1].DateTimeString());
                        }
                    }
                    else
                    {
                        pt.SetTime(zoneTime[currentZone+1].DateTimeString());
                    }
                }
            };
            //logDbMessage("zone Fee is: "+ Common::getInstance()->SetFeeFormat(zoneFee),"DB");
            
        };	
        
        if(currentRateType==2)      //per entry charge
        {
            //logDbMessage("zone Fee before enter currentRateType is: "+ Common::getInstance()->SetFeeFormat(zoneFee),"DB");
            if((paras.giMCyclePerDay>0)&&(iTransType== 2))
            {
                if((bMCPerDayChecked== false)&&(currentRate>0))
                {
                    haspaid= HasPaidWithinPeriod(zoneTime[currentZone].DateTimeString(),zoneTime[currentZone+1].DateTimeString());
                    if(haspaid>0)
                    {
                        currentRate=0;
                    }
                }
                bMCPerDayChecked=true;
            }
            if(currentAllowance>0)
            {
                timediff=calTime.diffmin(pt.GetUnixTimestamp(), currentTime.GetUnixTimestamp());
                
                if(((charge>0)||(dayFee>0))&&(timediff<=currentAllowance))
                {
                    logDbMessage("within allowance, no change","DB");
                }
                else
                zoneFee = zoneFee + currentRate;
            }
            else
            zoneFee = zoneFee + currentRate;
            
            //logDbMessage("PEallowance = "+ std::to_string(paras.giPEAllowance),"DB");
            
            if(paras.giPEAllowance>0)
            {
                //logDbMessage("pt before gi Allowance is : "+pt.DateTimeString(),"DB");
                timediff=calTime.diffmin(pt.GetUnixTimestamp(), zoneTime[currentZone+1].GetUnixTimestamp());
                //logDbMessage("time diff in gi is "+std::to_string(timediff),"DB");
                if(timediff<paras.giPEAllowance)
                {
                    pt.SetTime(pt.GetUnixTimestamp()+(paras.giPEAllowance*60));
                //	logDbMessage("pt in gi Allowance 1 is : "+pt.DateTimeString(),"DB");
                }
                else
                {
                    pt.SetTime(zoneTime[currentZone+1].DateTimeString());
                //	logDbMessage("pt in gi Allowance 2 is : "+pt.DateTimeString(),"DB");
                }
            //logDbMessage("pt in gi Allowance is : "+pt.DateTimeString(),"DB");
            }
            else
            pt.SetTime(zoneTime[currentZone+1].DateTimeString());
        //	logDbMessage("pre entry zone Fee is: "+ Common::getInstance()->SetFeeFormat(zoneFee),"DB");
        };
        //logDbMessage("day Fee before current zone is: "+ Common::getInstance()->SetFeeFormat(dayFee),"DB");
        //logDbMessage("zone Fee before current zone is: "+Common::getInstance()->SetFeeFormat(zoneFee),"DB");
        //logDbMessage("currentMin Fee is: "+ Common::getInstance()->SetFeeFormat(currentMin),"DB");
        //logDbMessage("currentMax Fee is: "+ Common::getInstance()->SetFeeFormat(currentMax),"DB");
        if((currentMin>0)&&(zoneFee<currentMin))
        zoneFee=currentMin;
        if((currentMax>0)&&(zoneFee>currentMax))
        zoneFee=currentMax;
        dayFee = dayFee + zoneFee;
        //logDbMessage("PD = "+PD.DateTimeString(),"DB");
        //logDbMessage("pt = "+pt.DateTimeString(),"DB");
        //logDbMessage("zone Fee is: "+ Common::getInstance()->SetFeeFormat(zoneFee),"DB");
        //logDbMessage("day Fee is: "+ Common::getInstance()->SetFeeFormat(dayFee),"DB");
        //logDbMessage(std::to_string(PD.GetUnixTimestamp()),"DB");
        //logDbMessage(std::to_string(pt.GetUnixTimestamp()),"DB");
        timediff=calTime.diffday(PD.GetUnixTimestamp(),pt.GetUnixTimestamp()); 
        //logDbMessage("diff day is: "+ std::to_string(timediff),"DB");
        
        if(timediff>0)
        {
        //	logDbMessage("day Feeq is: " + Common::getInstance()->SetFeeFormat(dayFee),"DB");
        //	logDbMessage("charge Feeq is: " + Common::getInstance()->SetFeeFormat(charge),"DB");
            if((dayMin>0)&&(dayFee<dayMin))
            dayFee=dayMin;
            if((dayMax>0)&&(dayFee>dayMax))
            dayFee=dayMax;
            charge=charge+dayFee;
            dayFee=0;
            bGotDayInfo= false;
            PD.SetTime(PD.GetUnixTimestamp()+86400);
            //logDbMessage("next day PD is:"+PD.DateTimeString(),"DB");
            //logDbMessage("day Feeb is: "+ Common::getInstance()->SetFeeFormat(dayFee),"DB");
            //logDbMessage("charge Feeb is: "+ Common::getInstance()->SetFeeFormat(charge),"DB");
        }
        timediff=calTime.diffmin(pt.GetUnixTimestamp(),currentTime.GetUnixTimestamp());
        if(timediff<=0) break;
        //logDbMessage("loop to start calcuation fee again","DB");
    }        //end while(1) 
    
    if(dayFee>0) // day fee have not add into charge
    {
        //logDbMessage("day Fee0 is: " + Common::getInstance()->SetFeeFormat(dayFee),"DB");
        //logDbMessage("charge Fee0 is: " + Common::getInstance()->SetFeeFormat(charge),"DB");
        if((dayMin>0)&&(dayFee<dayMin))
        dayFee=dayMin;
        if((dayMax>0)&&(dayFee>dayMax))
        dayFee=dayMax;
        charge=charge+dayFee;
        //logDbMessage("day Fee1 is: " + Common::getInstance()->SetFeeFormat(dayFee),"DB");
        //logDbMessage("charge Fee1 is: " + Common::getInstance()->SetFeeFormat(charge),"DB");
    }
    //logDbMessage("After loop 24hr block ="+ std::to_string(b24HourBlock),"DB");
    
    if(b24HourBlock==true)
    {
    //	logDbMessage("b 24hr block ","DB");
    //	logDbMessage("b 24hr charge: "+ Common::getInstance()->SetFeeFormat(s24HourCharges),"DB");
        if(charge> s24HourFee) charge=s24HourFee;
    //	logDbMessage("b charge: "+ Common::getInstance()->SetFeeFormat(charge),"DB");
        charge= s24HourCharges+charge;
    }
    
    if(paras.giTariffFeeMode>0)
    {
        iRet=RoundIt(charge, paras.giTariffFeeMode)/100;
    }
    else iRet=charge;
    
    //logDbMessage("Total fee is: " + Common::getInstance()->SetFeeFormat(iRet),"DB");
    //logDbMessage("pt time is: "+pt.DateTimeString(),"DB");
    
    return(iRet);
    

}

DBError db::LoadTariffTypeInfo()
{
    constexpr int kMaxAttempts = 2;
    constexpr std::size_t kMaxTariffTypes = 2;

    try
    {
        logDbMessage("Load Tariff Type Info: Started", "DB");

        // =====================================================
        // Try Local DB first.
        // If no data, download once and retry.
        // =====================================================
        for (int attempt = 0; attempt < kMaxAttempts; ++attempt)
        {
            std::vector<ReaderItem> result;

            const std::string sqlStmt =
                "SELECT tariff_type, start_time, end_time "
                "FROM tariff_type_info "
                "ORDER BY start_time ASC";

            const int ret = localdb->SQLSelect(sqlStmt, &result, true);

            if (ret != 0)
            {
                m_local_db_err_flag = 1;

                logDbMessage("Load Tariff Type Info failed.", "DB");

                return iLocalFail;
            }

            m_local_db_err_flag = 0;

            // =================================================
            // No Local data - download once and retry
            // =================================================
            if (result.empty())
            {
                if (attempt == 0)
                {
                    downloadtarifftypeinfo();
                    continue;
                }

                logDbMessage("Load Tariff Type Info error: no data in local DB.", "DB");

                return iNoData;
            }

            // =================================================
            // Load tariff type information into RAM
            // =================================================
            const std::size_t loadCount =
                std::min(
                    result.size(),
                    kMaxTariffTypes);

            for (std::size_t index = 0; index < loadCount; ++index)
            {
                gtarifftypeinfo[index].tariff_type = result[index].GetDataItem(0);
                gtarifftypeinfo[index].start_time = result[index].GetDataItem(1);
                gtarifftypeinfo[index].end_time = result[index].GetDataItem(2);
            }

            if (result.size() > kMaxTariffTypes)
            {
                logDbMessage("Tariff Type Info contains more than  2 records. Extra records ignored.", "DB");
            }

            logDbMessage("Load Tariff Type Info: success", "DB");

            return iDBSuccess;
        }
    }
    catch (const std::exception& e)
    {
        logDbMessage("Load Tariff Type Info error: " + std::string(e.what()), "DB");

        m_local_db_err_flag = 1;

        return iLocalFail;
    }

    return iNoData;
}

std::string db::CalParkedTime(long parkedMinutes)
{
    if (parkedMinutes < 0)
    {
        return "N/A";
    }

    constexpr long kMinutesPerHour = 60;
    constexpr long kMinutesPerDay = 24 * kMinutesPerHour;

    const long days = parkedMinutes / kMinutesPerDay;
    const long remainingMinutes = parkedMinutes % kMinutesPerDay;
    const long hours = remainingMinutes / kMinutesPerHour;
    const long minutes = remainingMinutes % kMinutesPerHour;

    std::ostringstream output;

    if (days > 0)
    {
        output
            << days
            << "D "
            << std::setfill('0')
            << std::setw(2)
            << hours
            << ":"
            << std::setw(2)
            << minutes;
    }
    else
    {
        output
            << hours
            << ":"
            << std::setfill('0')
            << std::setw(2)
            << minutes;
    }

    return output.str();
}

DBError db::LoadXTariff()
{
    constexpr int kMaxAttempts = 2;
    constexpr int kTariffSlots = 5;

    try
    {
        logDbMessage("Load XTariff: Started", "DB");

        // =====================================================
        // Try Local DB first.
        // If no data, download once and retry.
        // =====================================================
        for (int attempt = 0; attempt < kMaxAttempts; ++attempt)
        {
            std::vector<ReaderItem> result;

            const std::string sqlStmt =
                "SELECT * "
                "FROM X_Tariff";

            const int ret = localdb->SQLSelect(sqlStmt, &result, true);

            if (ret != 0)
            {
                m_local_db_err_flag = 1;

                logDbMessage("Load XTariff failed.", "DB");

                return iLocalFail;
            }

            m_local_db_err_flag = 0;

            // =================================================
            // No Local data - download once and retry
            // =================================================
            if (result.empty())
            {
                if (attempt == 0)
                {
                    const auto data = operation::getInstance()->FnGetSharedData();

                    if (!data)
                    {
                        logDbMessage("Unable to get Operation shared data.", "DB");

                        return iLocalFail;
                    }

                    downloadxtariff(data->tParas.giGroupID, data->tParas.giSite, 0);

                    continue;
                }

                logDbMessage("Load XTariff error: no data in local DB.", "DB");

                return iNoData;
            }

            // =================================================
            // Load XTariff into RAM
            // =================================================
            msxtariff.clear();

            for (const auto& row : result)
            {
                XTariff_Struct xtariff{};

                xtariff.day_index = row.GetDataItem(0);
                xtariff.autocharge[0] = row.GetDataItem(1);
                xtariff.fee[0] = row.GetDataItem(2);

                // Slots 1 - 4 contain:
                // time, autocharge, fee
                for (int index = 1; index < kTariffSlots; ++index)
                {
                    const int baseColumn = 3 + ((index - 1) * 3);

                    xtariff.time[index] = row.GetDataItem(baseColumn);

                    if (xtariff.time[index].empty())
                    {
                        xtariff.time[index] = "23:59";
                    }

                    xtariff.autocharge[index] = row.GetDataItem(baseColumn + 1);
                    xtariff.fee[index] = row.GetDataItem(baseColumn + 2);
                }

                msxtariff.push_back(std::move(xtariff));
            }

            logDbMessage("Load XTariff: success", "DB");

            return iDBSuccess;
        }
    }
    catch (const std::exception& e)
    {
        logDbMessage("Load XTariff error: " + std::string(e.what()), "DB");

        m_local_db_err_flag = 1;

        return iLocalFail;
    }

    return iNoData;
}

int db::GetXTariff(int& autoDebit, float& amount, int vehicleType)
{
    constexpr int kTimeCutoffCount = 4;

    CE_Time currentDateTime;
    currentDateTime.SetTime();

    // =========================================================
    // Determine day index
    // =========================================================
    const int dayType = GetDayType(currentDateTime);

    if (dayType < 0)
    {
        logDbMessage("Unable to determine XTariff day type.", "DB");

        return 0;
    }

    const int dayIndex = dayType + (vehicleType * 3);

    const std::string targetDayIndex =
        "," +
        std::to_string(dayIndex) +
        ",";

    const std::string currentTime = currentDateTime.HMTimeString();

    // =========================================================
    // Find matching XTariff record
    // =========================================================
    for (const auto& tariff : msxtariff)
    {
        const std::string configuredDayIndex =
            "," +
            tariff.day_index +
            ",";

        if (configuredDayIndex.find(targetDayIndex) == std::string::npos)
        {
            continue;
        }

        try
        {
            // =================================================
            // Check first 4 tariff time ranges
            // =================================================
            for (int slot = 0; slot < kTimeCutoffCount; ++slot)
            {
                if (currentTime <= tariff.time[slot + 1])
                {
                    const int selectedAutoDebit = std::stoi(tariff.autocharge[slot]);
                    const float selectedAmount = std::stof(tariff.fee[slot]);

                    autoDebit = selectedAutoDebit;
                    amount = selectedAmount;

                    return 1;
                }
            }
        }
        catch (const std::exception& e)
        {
            logDbMessage("GetXTariff invalid tariff data: " + std::string(e.what()), "DB");

            return 0;
        }
    }

    return 0;
}

int db::FetchEntryinfo(const std::string& iuNo)
{
    auto* op = operation::getInstance();

    const auto data = op->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return -1;
    }

    const std::string zoneEntries = data->tParas.gsZoneEntries;

    try
    {
        // =====================================================
        // Check Central DB first
        // =====================================================
        std::string centralSql =
            "SELECT Entry_time, trans_type, parking_fee, "
            "paid_amt, owe_amt, entry_station "
            "FROM Movement_trans_tmp "
            "WHERE (iu_tk_no = '" +
            iuNo +
            "'";

        if (iuNo.length() == 16)
        {
            centralSql +=
                " OR card_mc_no = '" +
                iuNo +
                "')";
        }
        else
        {
            centralSql +=
                " OR entry_lpn = '" +
                iuNo +
                "')";
        }

        centralSql +=
            " AND exit_time IS NULL "
            "AND CHARINDEX("
            "',' + CAST(entry_station AS varchar(2)) + ',', "
            "'" +
            zoneEntries +
            "') > 0 "
            "ORDER BY entry_time DESC";

        std::vector<ReaderItem> centralResult;

        const int centralRet = centraldb->SQLSelect(centralSql, &centralResult, true);

        if (centralRet == 0)
        {
            m_remote_db_err_flag.store(0);

            if (!centralResult.empty())
            {
                auto exitData = data->tExit;

                exitData.sEntryTime = centralResult.front().GetDataItem(0);
                exitData.iTransType = std::stoi(centralResult.front().GetDataItem(1));
                exitData.sOweAmt = std::stof(centralResult.front().GetDataItem(4));
                exitData.iEntryID = std::stoi(centralResult.front().GetDataItem(5));
                
                const std::string entryTime = exitData.sEntryTime;

                OperationSharedDataUpdate update;
                update.tExit = std::move(exitData);

                if (!op->FnUpdateSharedData(std::move(update)))
                {
                    logDbMessage("Unable to update Operation shared data.", "DB");

                    return -1;
                }

                logDbMessage("Fetch Entry time from Central: " + entryTime, "DB");

                return centralRet;
            }

            logDbMessage("No Entry record in Central DB.", "DB");
        }
        else
        {
            m_remote_db_err_flag.store(1);
        }

        // =====================================================
        // Central failed / no record - check Local DB
        // =====================================================
        const std::string localSql =
            "SELECT Entry_time, trans_type, paid_amt, "
            "Owe_Amt, Station_id "
            "FROM Entry_Trans "
            "WHERE Status = 0 "
            "AND iu_tk_no = '" +
            iuNo +
            "' "
            "ORDER BY entry_time DESC";

        std::vector<ReaderItem> localResult;

        const int localRet = localdb->SQLSelect(localSql, &localResult, true);

        if (localRet != 0)
        {
            logDbMessage("Fetch local entry time failed.", "DB");

            return localRet;
        }

        if (localResult.empty())
        {
            logDbMessage("No entry record in Local DB.", "DB");

            return 3;
        }

        auto exitData = data->tExit;

        exitData.sEntryTime = localResult.front().GetDataItem(0);
        exitData.iTransType = std::stoi(localResult.front().GetDataItem(1));
        exitData.sOweAmt = std::stof(localResult.front().GetDataItem(3));
        exitData.iEntryID = std::stoi(localResult.front().GetDataItem(4));

        const std::string entryTime = exitData.sEntryTime;

        OperationSharedDataUpdate update;
        update.tExit = std::move(exitData);

        if (!op->FnUpdateSharedData(std::move(update)))
        {
            logDbMessage("Unable to update Operation shared data.", "DB");

            return -1;
        }

        logDbMessage("Fetch Entry time from Local: " + entryTime, "DB");

        return localRet;
    }
    catch (const std::exception& e)
    {
        logDbMessage("FetchEntryinfo error: " + std::string(e.what()), "DB");

        return -1;
    }
}

int db::CheckCardOK(const std::string& cardNo)
{
    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return -1;
    }

    const std::string zoneId = std::to_string(data->gtStation.iZoneID);

    try
    {
        // =====================================================
        // Check Master Season Card
        // =====================================================
        const std::string seasonSql =
            "SELECT date_from, date_to "
            "FROM Season_mst "
            "WHERE season_No = '" +
            cardNo +
            "' "
            "AND ("
            "zone_id = '0' "
            "OR CHARINDEX("
            "',' + CAST(" +
            zoneId +
            " AS varchar(2)) + ',', "
            "',' + zone_id + ',') > 0"
            ") "
            "AND s_status = 1 "
            "AND (season_type = 1 OR season_type = 9)";

        std::vector<ReaderItem> seasonResult;

        const int seasonRet = centraldb->SQLSelect(seasonSql, &seasonResult, true);

        if (seasonRet != 0)
        {
            // Preserve legacy behavior:
            // DB failure is treated as normal card.
            return 0;
        }

        if (!seasonResult.empty())
        {
            const auto& row = seasonResult.front();

            const std::string dateFrom = row.GetDataItem(0);
            const std::string dateTo = row.GetDataItem(1);
            const bool hasStarted = Common::getInstance()->FnGetDateDiffInSeconds(dateFrom) > 0;
            const bool notExpired = Common::getInstance()->FnGetDateDiffInSeconds(dateTo) < 0;

            if (hasStarted && notExpired)
            {
                logDbMessage("Master Card!", "DB");

                return 5;
            }
        }

        // =====================================================
        // Check Complimentary Card
        // =====================================================
        const std::string complimentarySql =
            "SELECT Complimentary_no "
            "FROM Complimentary "
            "WHERE Complimentary_no = '" +
            cardNo +
            "' "
            "AND exit_time IS NULL";

        std::vector<ReaderItem> complimentaryResult;

        const int complimentaryRet = centraldb->SQLSelect(complimentarySql, &complimentaryResult, true);

        if (complimentaryRet != 0)
        {
            // Preserve legacy behavior.
            return 0;
        }

        if (!complimentaryResult.empty())
        {
            logDbMessage("Complimentary Card!", "DB");

            return 2;
        }

        return 0;
    }
    catch (const std::exception& e)
    {
        logDbMessage("CheckCardOK error: " + std::string(e.what()), "DB");

        return -1;
    }
}

DBError db::updatemovementtrans(tExitTrans_Struct& exitTrans)
{
    auto* op = operation::getInstance();

    const auto data = op->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return iLocalFail;
    }

    const std::string zoneEntries = data->tParas.gsZoneEntries;
    const std::string updateTime = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();

    // =========================================================
    // Format fee values
    // =========================================================
    exitTrans.sFee = op->GfeeFormat(exitTrans.sFee);
    exitTrans.sPaidAmt = op->GfeeFormat(exitTrans.sPaidAmt);
    exitTrans.sRedeemAmt = op->GfeeFormat(exitTrans.sRedeemAmt);
    exitTrans.sGSTAmt = op->GfeeFormat(exitTrans.sGSTAmt);

    // =========================================================
    // Determine Exit LPN
    // =========================================================
    std::string lprNo;

    if (!exitTrans.sLPN[0].empty() ||
        !exitTrans.sLPN[1].empty())
    {
        if (exitTrans.iTransType == 7 ||
            exitTrans.iTransType == 8 ||
            exitTrans.iTransType == 22)
        {
            lprNo = exitTrans.sLPN[1];
        }
        else
        {
            lprNo = exitTrans.sLPN[0];
        }
    }

    try
    {
        // =====================================================
        // Build Movement_trans_tmp update
        // =====================================================
        std::string sqlStmt =
            "UPDATE movement_trans_tmp SET "
            "exit_lpn = '" +
            lprNo +
            "', "
            "exit_station = '" +
            exitTrans.xsid +
            "', "
            "exit_time = '" +
            exitTrans.sExitTime +
            "', "
            "trans_type = '" +
            std::to_string(exitTrans.iTransType) +
            "', "
            "card_mc_no = '" +
            exitTrans.sCardNo +
            "', "
            "parking_fee = '" +
            std::to_string(exitTrans.sFee) +
            "', "
            "paid_amt = '" +
            std::to_string(exitTrans.sPaidAmt) +
            "', "
            "parked_time = '" +
            std::to_string(exitTrans.lParkedTime) +
            "', "
            "receipt_no = '" +
            exitTrans.sReceiptNo +
            "', "
            "redeem_amt = '" +
            std::to_string(exitTrans.sRedeemAmt) +
            "', "
            "redeem_time = '" +
            std::to_string(exitTrans.iRedeemTime) +
            "', "
            "card_type = '" +
            std::to_string(exitTrans.iCardType) +
            "', "
            "top_up_amt = '" +
            std::to_string(exitTrans.sTopupAmt) +
            "', "
            "update_dt = '" +
            updateTime +
            "' ";

        // =====================================================
        // CHU / EEP late transaction
        // =====================================================
        if (exitTrans.sEntryTime == exitTrans.sExitTime)
        {
            sqlStmt +=
                "WHERE iu_tk_no = '" +
                exitTrans.sIUNo +
                "' "
                "AND exit_time IS NULL "
                "AND CHARINDEX("
                "',' + CAST(entry_station AS varchar(2)) + ',', "
                "'" +
                zoneEntries +
                "') > 0";
        }
        else
        {
            sqlStmt +=
                "WHERE iu_tk_no = '" +
                exitTrans.sIUNo +
                "' "
                "AND entry_time = '" +
                exitTrans.sEntryTime +
                "' "
                "AND exit_time IS NULL "
                "AND CHARINDEX("
                "',' + CAST(entry_station AS varchar(2)) + ',', "
                "'" +
                zoneEntries +
                "') > 0";
        }

        // =====================================================
        // Update Central movement transaction
        // =====================================================
        const int ret = centraldb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            logDbMessage("Fail to match Movement_trans_tmp.", "DB");

            m_remote_db_err_flag.store(1);

            return iCentralFail;
        }

        m_remote_db_err_flag.store(0);

        if (centraldb->NumberOfRowsAffected > 0)
        {
            logDbMessage("Success matching Movement_trans_tmp.", "DB");

            return iCentralSuccess;
        }

        // =====================================================
        // No matching entry - insert movement transaction
        // =====================================================
        logDbMessage("No matching Movement_trans_tmp to update.", "DB");

        insert2movementtrans(exitTrans);

        return iCentralSuccess;
    }
    catch (const std::exception& e)
    {
        logDbMessage("updatemovementtrans error: " + std::string(e.what()), "DB");

        m_remote_db_err_flag.store(1);

        return iCentralFail;
    }
}

DBError db::insert2movementtrans(tExitTrans_Struct& exitTrans)
{
    auto* op = operation::getInstance();

    // =========================================================
    // Format fee values
    // =========================================================
    exitTrans.sFee = op->GfeeFormat(exitTrans.sFee);
    exitTrans.sPaidAmt = op->GfeeFormat(exitTrans.sPaidAmt);
    exitTrans.sRedeemAmt = op->GfeeFormat(exitTrans.sRedeemAmt);
    exitTrans.sGSTAmt = op->GfeeFormat(exitTrans.sGSTAmt);

    // =========================================================
    // Determine Exit LPN
    // =========================================================
    std::string lprNo;

    if (!exitTrans.sLPN[0].empty() ||
        !exitTrans.sLPN[1].empty())
    {
        if (exitTrans.iTransType == 7 ||
            exitTrans.iTransType == 8 ||
            exitTrans.iTransType == 22)
        {
            lprNo = exitTrans.sLPN[1];
        }
        else
        {
            lprNo = exitTrans.sLPN[0];
        }
    }

    const bool hasEntryInfo = !exitTrans.sEntryTime.empty();

    try
    {
        // =====================================================
        // Build INSERT statement
        // =====================================================
        std::string sqlStmt =
            "INSERT INTO movement_trans_tmp ("
            "exit_lpn, "
            "exit_station, "
            "exit_time, "
            "trans_type, "
            "card_mc_no, "
            "iu_tk_no, "
            "parking_fee, "
            "paid_amt, "
            "Parked_time, "
            "receipt_no, "
            "redeem_amt, "
            "redeem_time, "
            "Card_Type, "
            "top_up_amt";

        if (hasEntryInfo)
        {
            sqlStmt +=
                ", entry_time, "
                "entry_station";
        }

        sqlStmt +=
            ") VALUES ('" +
            lprNo +
            "', " +
            exitTrans.xsid +
            ", '" +
            exitTrans.sExitTime +
            "', " +
            std::to_string(exitTrans.iTransType) +
            ", '" +
            exitTrans.sCardNo +
            "', '" +
            exitTrans.sIUNo +
            "', " +
            std::to_string(exitTrans.sFee) +
            ", " +
            std::to_string(exitTrans.sPaidAmt) +
            ", " +
            std::to_string(exitTrans.lParkedTime) +
            ", '" +
            exitTrans.sReceiptNo +
            "', " +
            std::to_string(exitTrans.sRedeemAmt) +
            ", " +
            std::to_string(exitTrans.iRedeemTime) +
            ", " +
            std::to_string(exitTrans.iCardType) +
            ", " +
            std::to_string(exitTrans.sTopupAmt);

        if (hasEntryInfo)
        {
            sqlStmt +=
                ", '" +
                exitTrans.sEntryTime +
                "', " +
                std::to_string(exitTrans.iEntryID);
        }

        sqlStmt += ")";

        // =====================================================
        // Insert into Central DB
        // =====================================================
        const int ret = centraldb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            logDbMessage("Fail to insert into movement_trans_tmp.", "DB");

            m_remote_db_err_flag.store(1);

            return iCentralFail;
        }

        m_remote_db_err_flag.store(0);

        logDbMessage("Success insert into movement_trans_tmp.", "DB");

        return iCentralSuccess;
    }
    catch (const std::exception& e)
    {
        logDbMessage("insert2movementtrans error: " + std::string(e.what()), "DB");

        m_remote_db_err_flag.store(1);

        return iCentralFail;
    }
}

int db::isValidBarCodeTicket(
    bool isRedemptionTicket,
    const std::string& barcodeTicket,
    std::tm& expireTime,
    float& redeemAmount,
    int& redeemTime)
{
    constexpr int kExpired = 0;
    constexpr int kValid = 1;
    constexpr int kUsed = 2;
    constexpr int kNotFound = 4;
    constexpr int kNotYetValid = 6;
    constexpr int kDbError = -1;

    // =========================================================
    // Build query
    // =========================================================
    const std::string sqlStmt =
        isRedemptionTicket
            ? "SELECT Valid_from, Valid_to, redeem_dt, "
              "redeem_amt, redeem_time "
              "FROM Redemption_view "
              "WHERE redeem_no = '" +
                  barcodeTicket + "'"
            : "SELECT Valid_from, Valid_to, exit_time "
              "FROM Complimentary_view "
              "WHERE complimentary_no = '" +
                  barcodeTicket + "'";

    logDbMessage(sqlStmt, "DB");

    // =========================================================
    // Query Central DB
    // =========================================================
    std::vector<ReaderItem> result;

    const int ret = centraldb->SQLSelect(sqlStmt, &result, true);

    if (ret != 0)
    {
        m_remote_db_err_flag.store(1);

        return kDbError;
    }

    m_remote_db_err_flag.store(0);

    if (result.empty())
    {
        logDbMessage("No barcode ticket found in Central DB.", "DB");

        return kNotFound;
    }

    try
    {
        const auto& row = result.front();
        const std::string validFrom = row.GetDataItem(0);
        const std::string validTo = row.GetDataItem(1);
        const std::string usedTime = row.GetDataItem(2);

        int resultCode = kValid;

        // =====================================================
        // Check ticket status
        // =====================================================
        if (usedTime.empty() ||
            usedTime == "NULL")
        {
            if (Common::getInstance()->FnGetDateDiffInSeconds(validFrom) < 0)
            {
                resultCode = kNotYetValid;
            }
            else if (Common::getInstance()->FnGetDateDiffInSeconds(validTo) > 0)
            {
                resultCode = kExpired;
            }
        }
        else
        {
            resultCode = kUsed;
        }

        // =====================================================
        // Get expiry time
        // =====================================================
        const auto timePoint = Common::getInstance()->FnParseDateTime(validTo);
        const std::time_t expireTimestamp = std::chrono::system_clock::to_time_t(timePoint);

        expireTime = *std::localtime(&expireTimestamp);

        // =====================================================
        // Valid redemption ticket
        // =====================================================
        if (resultCode == kValid &&
            isRedemptionTicket)
        {
            redeemAmount = std::stof(row.GetDataItem(3));
            redeemTime = std::stoi(row.GetDataItem(4));
        }

        return resultCode;
    }
    catch (const std::exception& e)
    {
        logDbMessage("Barcode ticket data format error: " + std::string(e.what()), "DB");

        return kDbError;
    }
}

DBError db::update99PaymentTrans()
{
    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return iCentralFail;
    }

    const auto& exitData = data->tExit;
    const auto& station = data->gtStation;

    try
    {
        // =====================================================
        // Build UPDATE statement
        // =====================================================
        std::string sqlStmt =
            "UPDATE Exit_trans_tmp "
            "SET status = 0";

        if (exitData.sRebateAmt > 0)
        {
            sqlStmt +=
                ", redeem_amt = " +
                std::to_string(exitData.sRebateAmt);

            sqlStmt +=
                ", paid_amt = " +
                std::to_string(exitData.sPaidAmt);

            sqlStmt +=
                ", gst_amt = " +
                std::to_string(exitData.sGSTAmt);

            sqlStmt +=
                ", Trans_Type = " +
                std::to_string(exitData.iTransType);

            sqlStmt +=
                ", Card_mc_no = '" +
                exitData.sCardNo +
                "'";

            sqlStmt +=
                ", redeem_no = '" +
                exitData.sRedeemNo +
                "'";
        }

        sqlStmt +=
            " WHERE iu_tk_no = '" +
            exitData.sIUNo +
            "' "
            "AND status = 99 "
            "AND Station_ID = " +
            std::to_string(station.iSID);

        // =====================================================
        // Update Central DB
        // =====================================================
        const int ret = centraldb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            logDbMessage("Failed to update status 99 transaction for EZPay/VCC.", "DB");

            m_remote_db_err_flag.store(1);

            return iCentralFail;
        }

        m_remote_db_err_flag.store(0);

        logDbMessage("Updated status 99 transaction to valid for EZPay/VCC.", "DB");

        return iDBSuccess;
    }
    catch (const std::exception& e)
    {
        logDbMessage("update99PaymentTrans error: " + std::string(e.what()), "DB");

        m_remote_db_err_flag.store(1);

        return iCentralFail;
    }
}

DBError db::insertUPTFileSummaryLastSettlement(
    const std::string& settleDate,
    const std::string& settleName,
    int settleType,
    uint64_t totalTrans,
    double totalAmt,
    int sendFlag,
    const std::string& sendDate)
{
    try
    {
        const std::string sqlStmt =
            "INSERT INTO UPT_File_Summary ("
            "settle_date, "
            "settle_file, "
            "settle_type, "
            "last_total_trans, "
            "last_total_amt, "
            "send_flag, "
            "send_dt"
            ") VALUES ('" +
            settleDate +
            "', '" +
            settleName +
            "', '" +
            std::to_string(settleType) +
            "', '" +
            std::to_string(totalTrans) +
            "', '" +
            std::to_string(totalAmt) +
            "', '" +
            std::to_string(sendFlag) +
            "', '" +
            sendDate +
            "')";

        const int ret = centraldb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            logDbMessage("Failed to insert into UPT_File_Summary.", "DB");

            m_remote_db_err_flag.store(1);

            return iCentralFail;
        }

        m_remote_db_err_flag.store(0);

        logDbMessage("Success to insert into UPT_File_Summary for: " + settleName, "DB");

        return iCentralSuccess;
    }
    catch (const std::exception& e)
    {
        logDbMessage("insertUPTFileSummaryLastSettlement error: " + std::string(e.what()), "DB");

        m_remote_db_err_flag.store(1);

        return iCentralFail;
    }
}

DBError db::insertUPTFileSummary(
    const std::string& settleDate,
    const std::string& settleName,
    int settleType,
    uint64_t totalTrans,
    double totalAmt,
    int sendFlag,
    const std::string& sendDate)
{
    try
    {
        const std::string sqlStmt =
            "INSERT INTO UPT_File_Summary ("
            "settle_date, "
            "settle_file, "
            "settle_type, "
            "total_trans, "
            "total_amt, "
            "send_flag, "
            "send_dt"
            ") VALUES ('" +
            settleDate +
            "', '" +
            settleName +
            "', '" +
            std::to_string(settleType) +
            "', '" +
            std::to_string(totalTrans) +
            "', '" +
            std::to_string(totalAmt) +
            "', '" +
            std::to_string(sendFlag) +
            "', '" +
            sendDate +
            "')";

        const int ret = centraldb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            logDbMessage("Failed to insert into UPT_File_Summary.", "DB");

            m_remote_db_err_flag.store(1);

            return iCentralFail;
        }

        m_remote_db_err_flag.store(0);

        logDbMessage("Success to insert into UPT_File_Summary for: " + settleName, "DB");

        return iCentralSuccess;
    }
    catch (const std::exception& e)
    {
        logDbMessage("insertUPTFileSummary error: " + std::string(e.what()), "DB");

        m_remote_db_err_flag.store(1);

        return iCentralFail;
    }
}

DBError db::DeleteBeforeInsertMT(const tExitTrans_Struct& exitTrans)
{
    try
    {
        const std::string sqlStmt =
            "DELETE FROM movement_trans_tmp "
            "WHERE iu_tk_no = '" +
            exitTrans.sIUNo +
            "' "
            "AND entry_time = '" +
            exitTrans.sEntryTime +
            "' "
            "AND exit_time IS NULL";

        const int ret = centraldb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            logDbMessage("Failed to delete existing movement_trans_tmp record.", "DB");

            m_remote_db_err_flag.store(1);

            return iCentralFail;
        }

        m_remote_db_err_flag.store(0);

        return iCentralSuccess;
    }
    catch (const std::exception& e)
    {
        logDbMessage("DeleteBeforeInsertMT error: " + std::string(e.what()), "DB");

        m_remote_db_err_flag.store(1);

        return iCentralFail;
    }
}

int db::UpdateEEPExitTrans(
    const std::string& obu,
    const std::string& dSerialNo,
    const std::string& cardNo,
    float fee,
    float topupAmt,
    int transRoute,
    int result)
{
    const auto data = operation::getInstance()->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return -1;
    }

    const float gstRate = data->tParas.gfGSTRate;
    const int stationId = data->gtStation.iSID;
    const float gstAmount = fee * gstRate / (1.0F + gstRate);

    // =========================================================
    // Build EEP transaction UPDATE statement
    // =========================================================
    const auto buildUpdateSql =
        [&](const std::string& tableName)
        {
            return
                "UPDATE " +
                tableName +
                " SET "
                "paid_amt = " +
                std::to_string(fee) +
                ", card_mc_no = '" +
                cardNo +
                "'" +
                ", EEPTransRoute = " +
                std::to_string(transRoute) +
                ", EEPPaymentResult = " +
                std::to_string(result) +
                ", Top_up_amt = " +
                std::to_string(topupAmt) +
                ", Gst_amt = " +
                std::to_string(gstAmount) +
                " WHERE EEPDSerialNo = '" +
                dSerialNo +
                "'"
                " AND iu_tk_no = '" +
                obu +
                "'"
                " AND Station_id = " +
                std::to_string(stationId) +
                " AND EEPPaymentResult != 1"
                " AND EEPPaymentResult != 2";
        };

    try
    {
        // =====================================================
        // Try Exit_trans_tmp first
        // =====================================================
        const std::string tmpSql = buildUpdateSql("Exit_trans_tmp");

        int ret = centraldb->SQLExecutNoneQuery(tmpSql);

        if (ret != 0)
        {
            logDbMessage("Fail to update EEP Trans to Exit_trans_tmp.", "DB");

            m_remote_db_err_flag.store(1);

            return ret;
        }

        if (centraldb->NumberOfRowsAffected > 0)
        {
            logDbMessage("Success update EEP Trans to Exit_trans_tmp.", "DB");

            m_remote_db_err_flag.store(0);

            return ret;
        }

        // =====================================================
        // Not found in tmp, try Exit_trans
        // =====================================================
        const std::string exitSql = buildUpdateSql("Exit_trans");

        ret = centraldb->SQLExecutNoneQuery(exitSql);

        if (ret != 0)
        {
            logDbMessage("Fail to update EEP Trans to Exit_trans.", "DB");

            m_remote_db_err_flag.store(1);

            return ret;
        }

        m_remote_db_err_flag.store(0);

        if (centraldb->NumberOfRowsAffected > 0)
        {
            logDbMessage("Success update EEP Trans to Exit_trans.", "DB");
        }
        else
        {
            logDbMessage("No transaction found for EEP update.", "DB");
        }

        return ret;
    }
    catch (const std::exception& e)
    {
        logDbMessage("UpdateEEPExitTrans error: " + std::string(e.what()), "DB");

        m_remote_db_err_flag.store(1);

        return -1;
    }
}

int db::HasValidTicket(const std::string& iuNo, const std::string& lpn)
{
    auto* op = operation::getInstance();

    const auto data = op->FnGetSharedData();

    if (!data)
    {
        logDbMessage("Unable to get Operation shared data.", "DB");

        return -1;
    }

    try
    {
        // =====================================================
        // Build IU / LPN condition
        // =====================================================
        const std::string ticketCondition =
            !iuNo.empty()
                ? "IU = '" + iuNo + "'"
                : "LPN = '" + lpn + "'";

        // =====================================================
        // Check Complimentary ticket first
        // =====================================================
        const std::string complimentarySql =
            "SELECT complimentary_no "
            "FROM complimentary "
            "WHERE valid_from <= GETDATE() "
            "AND valid_to >= GETDATE() "
            "AND " +
            ticketCondition +
            " AND exit_time IS NULL "
            "ORDER BY valid_to";

        std::vector<ReaderItem> complimentaryResult;

        const int complimentaryRet =
            centraldb->SQLSelect(complimentarySql, &complimentaryResult, true);

        if (complimentaryRet != 0)
        {
            m_remote_db_err_flag.store(1);

            return -1;
        }

        m_remote_db_err_flag.store(0);

        if (!complimentaryResult.empty())
        {
            const std::string complimentaryNo = complimentaryResult.front().GetDataItem(0);

            auto exitData = data->tExit;

            exitData.iTransType = 10;
            exitData.sPaidAmt = 0;
            exitData.sCardNo = complimentaryNo;

            OperationSharedDataUpdate update;
            update.tExit = std::move(exitData);

            if (!op->FnUpdateSharedData(std::move(update)))
            {
                logDbMessage("Unable to update Operation shared data.", "DB");

                return -1;
            }

            logDbMessage("Complimentary Ticket: " + complimentaryNo, "DB");

            return 0;
        }

        // =====================================================
        // No Complimentary ticket - check Redemption ticket
        // =====================================================
        const std::string redemptionSql =
            "SELECT redeem_no, redeem_amt, redeem_time "
            "FROM redemption "
            "WHERE valid_from <= GETDATE() "
            "AND valid_to >= GETDATE() "
            "AND " +
            ticketCondition +
            " AND exit_time IS NULL "
            "ORDER BY valid_to";

        std::vector<ReaderItem> redemptionResult;

        const int redemptionRet = centraldb->SQLSelect(redemptionSql, &redemptionResult, true);

        if (redemptionRet != 0)
        {
            m_remote_db_err_flag.store(1);

            return -1;
        }

        m_remote_db_err_flag.store(0);

        if (redemptionResult.empty())
        {
            return -1;
        }

        const auto& row = redemptionResult.front();

        auto exitData = data->tExit;
        exitData.sRedeemNo = row.GetDataItem(0);
        exitData.sRedeemAmt = std::stof(row.GetDataItem(1));
        exitData.iRedeemTime = std::stoi(row.GetDataItem(2));

        const std::string redeemNo = exitData.sRedeemNo;
        const float redeemAmount = exitData.sRedeemAmt;
        const int redeemTime = exitData.iRedeemTime;

        OperationSharedDataUpdate update;
        update.tExit = std::move(exitData);

        if (!op->FnUpdateSharedData(std::move(update)))
        {
            logDbMessage("Unable to update Operation shared data.", "DB");

            return -1;
        }

        logDbMessage("Redemption Ticket: " + redeemNo, "DB");

        if (redeemAmount >= 0.01F)
        {
            logDbMessage("Redemption Amt: " + Common::getInstance()->SetFeeFormat(redeemAmount), "DB");
        }
        else
        {
            logDbMessage("Redemption time: " + std::to_string(redeemTime), "DB");
        }

        return 0;
    }
    catch (const std::exception& e)
    {
        logDbMessage("HasValidTicket error: " + std::string(e.what()), "DB");

        return -1;
    }
}

DBError db::updateUsedTicket(tExitTrans_Struct& exitTrans)
{
    // =========================================================
    // Default used_by
    // =========================================================
    if (exitTrans.iUsedTicketBy == 0)
    {
        exitTrans.iUsedTicketBy = 1;
    }

    try
    {
        const bool isComplimentary = (exitTrans.iTransType == 10);

        const std::string tableName =
            isComplimentary
                ? "complimentary"
                : "redemption";

        const std::string ticketColumn =
            isComplimentary
                ? "complimentary_no"
                : "redeem_no";

        const std::string ticketNo =
            isComplimentary
                ? exitTrans.sCardNo
                : exitTrans.sRedeemNo;

        // =====================================================
        // Update used ticket
        // =====================================================
        const std::string sqlStmt =
            "UPDATE " +
            tableName +
            " SET "
            "used_by = " +
            std::to_string(exitTrans.iUsedTicketBy) +
            ", exit_station = '" +
            exitTrans.xsid +
            "', exit_time = '" +
            exitTrans.sExitTime +
            "', parking_fee = '" +
            std::to_string(exitTrans.sFee) +
            "', Parked_time = '" +
            std::to_string(exitTrans.lParkedTime) +
            "', iu_tk_no = '" +
            exitTrans.sIUNo +
            "' "
            "WHERE " +
            ticketColumn +
            " = '" +
            ticketNo +
            "'";

        const int ret = centraldb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            logDbMessage("Fail to update used ticket.", "DB");

            m_remote_db_err_flag.store(1);

            return iCentralFail;
        }

        m_remote_db_err_flag.store(0);

        logDbMessage("Update Used Ticket: " + std::to_string(exitTrans.iUsedTicketBy), "DB");

        return iCentralSuccess;
    }
    catch (const std::exception& e)
    {
        logDbMessage("updateUsedTicket error: " + std::string(e.what()), "DB");

        m_remote_db_err_flag.store(1);

        return iCentralFail;
    }
}

int db::GetSeasonHolder(const std::string& iuNo)
{
    try
    {
        const std::string sqlStmt =
            "SELECT Holder_Type "
            "FROM season_mst "
            "WHERE season_no = '" +
            iuNo +
            "'";

        std::vector<ReaderItem> result;

        const int ret = centraldb->SQLSelect(sqlStmt, &result, true);

        if (ret != 0)
        {
            m_remote_db_err_flag.store(1);

            return -1;
        }

        m_remote_db_err_flag.store(0);

        if (result.empty())
        {
            return 0;
        }

        return std::stoi(result.front().GetDataItem(0));
    }
    catch (const std::exception& e)
    {
        logDbMessage("GetSeasonHolder error: " + std::string(e.what()), "DB");

        m_remote_db_err_flag.store(1);

        return -1;
    }
}

int db::HasEZpay(const std::string& iuNo)
{
    try
    {
        const std::string sqlStmt =
            "SELECT * "
            "FROM tblIUList_mst "
            "WHERE valid_from <= GETDATE() "
            "AND valid_to >= GETDATE() "
            "AND iu_no = '" +
            iuNo +
            "'";

        std::vector<ReaderItem> result;

        const int ret = centraldb->SQLSelect(sqlStmt, &result, true);

        if (ret != 0)
        {
            m_remote_db_err_flag.store(1);

            return -1;
        }

        m_remote_db_err_flag.store(0);

        return result.empty() ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        logDbMessage("HasEZpay error: " + std::string(e.what()), "DB");

        m_remote_db_err_flag.store(1);

        return -1;
    }
}

int db::HasAXS(const std::string& iuNo)
{
    try
    {
        const std::string sqlStmt =
            "SELECT *"
            "FROM tblwhitelist_mst "
            "WHERE valid_from <= GETDATE() "
            "AND valid_to >= GETDATE() "
            "AND iu_no = '" +
            iuNo +
            "'";

        std::vector<ReaderItem> result;

        const int ret = centraldb->SQLSelect(sqlStmt, &result, true);

        if (ret != 0)
        {
            m_remote_db_err_flag.store(1);

            return -1;
        }

        m_remote_db_err_flag.store(0);

        return result.empty() ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        logDbMessage("HasAXS error: " + std::string(e.what()), "DB");

        m_remote_db_err_flag.store(1);

        return -1;
    }
}

std::string db::GetIUByLPN(const std::string& lpn)
{
    try
    {
        // =====================================================
        // Check Season first
        // =====================================================
        const std::string seasonSql =
            "SELECT season_no "
            "FROM season_mst "
            "WHERE vehicle_no = '" +
            lpn +
            "'";

        logDbMessage(seasonSql, "DB");

        std::vector<ReaderItem> seasonResult;

        const int seasonRet = centraldb->SQLSelect(seasonSql, &seasonResult, true);

        if (seasonRet != 0)
        {
            m_remote_db_err_flag.store(1);
        }
        else
        {
            m_remote_db_err_flag.store(0);

            if (!seasonResult.empty())
            {
                return seasonResult.front().GetDataItem(0);
            }
        }

        // =====================================================
        // Only Exit station checks movement_trans_tmp
        // =====================================================
        const auto data = operation::getInstance()->FnGetSharedData();

        if (!data)
        {
            logDbMessage("Unable to get Operation shared data.", "DB");

            return "";
        }

        if (data->gtStation.iType != tiExit)
        {
            return "";
        }

        // =====================================================
        // Check Movement transaction
        // =====================================================
        const std::string movementSql =
            "SELECT iu_tk_no "
            "FROM movement_trans_tmp "
            "WHERE entry_lpn = '" +
            lpn +
            "'";

        logDbMessage(movementSql, "DB");

        std::vector<ReaderItem> movementResult;

        const int movementRet = centraldb->SQLSelect(movementSql, &movementResult, true);

        if (movementRet != 0)
        {
            m_remote_db_err_flag.store(1);

            return "";
        }

        m_remote_db_err_flag.store(0);

        if (movementResult.empty())
        {
            return "";
        }

        return movementResult.front().GetDataItem(0);
    }
    catch (const std::exception& e)
    {
        logDbMessage("GetIUByLPN error: " + std::string(e.what()), "DB");

        m_remote_db_err_flag.store(1);

        return "";
    }
}

DBError db::FnUpdateStationSwVersion(const std::string& sid)
{
    try
    {
        const std::string sqlStmt =
            "UPDATE parameter_mst "
            "SET s" +
            sid +
            "_value = '" +
            std::string(SW_VERSION) +
            "' "
            "WHERE name = 'StationVersion'";

        const int ret = centraldb->SQLExecutNoneQuery(sqlStmt);

        if (ret != 0)
        {
            logDbMessage("Fail to update station software version.", "DB");

            m_remote_db_err_flag.store(1);

            return iCentralFail;
        }

        m_remote_db_err_flag.store(0);

        logDbMessage("Update station software version: " + std::string(SW_VERSION), "DB");

        return iCentralSuccess;
    }
    catch (const std::exception& e)
    {
        logDbMessage("FnUpdateStationSwVersion error: " + std::string(e.what()), "DB");

        m_remote_db_err_flag.store(1);

        return iCentralFail;
    }
}
