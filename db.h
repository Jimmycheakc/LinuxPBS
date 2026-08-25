#pragma once

#include <atomic>
#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "structuredata.h"
#include "odbc.h"


//using namespace std;

typedef enum
{
    iCentralFail = -2,
    iLocalFail = -1,
    iDBSuccess = 0,
    iNoData = 1,     //no relevant data such as rate etc.
    iUpdateFail = 2,
    iNoentry = 3,    //no entry record
    iOpen = 5,
    iClose = 6,
    iCentralSuccess = 7,
    iLocalSuccess = 8

}DBError;


// Passive synchronous database/business service.
//
// This class intentionally owns no io_context, thread, strand, work guard,
// timer, or coroutine. All database operations execute synchronously on the
// caller's thread. Active modules must dispatch potentially blocking DB work
// to an externally owned serialized DB/blocking execution lane.
//
// Threading contract:
// - connect*/FnClose are lifecycle operations and must not race with DB calls.
// - legacy tariff/holiday caches are mutable; until they are separated during
//   the Operation/shared-state refactor, serialize calls through one DB lane.
class db {
public:
    static db* getInstance();
    int connectlocaldb(string connectstr,int LocalSQLTimeOut,int SP_SQLTimeOut,float mPingTimeOut);
    int connectcentraldb(string connectStr,string connectIP,int CentralSQLTimeOut, int SP_SQLTimeOut,float mPingTimeOut);
    ~db();

    // Call only after no DB work is in flight. Safe to call repeatedly.
    void FnClose();

    int sp_isvalidseason(
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
            std::string& validFrom);

    int local_isvalidseason(const std::string& seasonNo, unsigned int zoneId);

    int isvalidseason(const std::string& seasonNo, BYTE inOut, unsigned int zoneId);
    void synccentraltime ();
    int downloadseason();
    int writeseason2local(tseason_struct& v);
    int downloadvehicletype();
    int writevehicletype2local(const std::string& iuCode, const std::string& iuType);
    int downloadledmessage();
    int writeledmessage2local(const std::string& msgId, const std::string& msgBody, const std::string& msgStatus);
    int downloadparameter();
    int writeparameter2local(const std::string& name, const std::string& value);
    int downloadstationsetup();
    int writestationsetup2local(const tstation_struct& v);
    int downloadTR();
    int writetr2local(int trType, int lineNo, int enabled, const std::string& lineText, const std::string& lineVar, int lineFont, int lineAlign);
    int downloadtariffsetup(int iGrpID = 0, int iSiteID = 1, int iCheckStatus = 0);
    int writetariffsetup2local(const tariff_struct& tariff);
    int downloadtarifftypeinfo();
    int writetarifftypeinfo2local(const tariff_type_info_struct& tariffType);
    int downloadxtariff(int iGrpID, int iSiteID, int iCheckStatus = 0);
    int writextariff2local(const x_tariff_struct& xTariff);
    int downloadholidaymst(int iCheckStatus = 0);
    int writeholidaymst2local(const std::string& holidayDate, const std::string& description);
    int download3tariffinfo();
    int write3tariffinfo2local(const tariff_info_struct& tariffInfo);
    int downloadratefreeinfo(int iCheckStatus = 0);
    int writeratefreeinfo2local(const rate_free_info_struct& rateFreeInfo);
    int downloadspecialdaymst(int iCheckStatus = 0);
    int writespecialday2local(const std::string& specialDate, const std::string& rateType, const std::string& dayCode);
    int downloadratetypeinfo(int iCheckStatus = 0);
    int writeratetypeinfo2local(const rate_type_info_struct& rateTypeInfo);
    int downloadratemaxinfo(int iCheckStatus = 0);
    int writeratemaxinfo2local(const rate_max_info_struct& rateMaxInfo);
    int WriteTariff2RAM(const tariff_struct& tariff);
    int GetDayType(CE_Time curr_date);
    int GetDayTypeNoPE(CE_Time curr_date);
    int GetDayTypeWithHE(CE_Time curr_date);
    float HasPaidWithinPeriod(const std::string& timeFrom, const std::string& timeTo);
    float RoundIt(float val, int giTariffFeeMode);
    float CalFeeRAM2GR(string eTime, string payTime,int iTransType, bool bNoGT = false);
    float CalFeeRAM2G(
        const std::string& entryTime,
        const std::string& payTime,
        int transType,
        bool noGraceTime = false);
    int GetXTariff(int& autoDebit, float& amount, int vehicleType = 0);
    string CalParkedTime(long parkedMinutes);

    DBError insertentrytrans(tEntryTrans_Struct& tEntry);
    DBError insertexittrans(tExitTrans_Struct& tExit);
    DBError updatemovementtrans(tExitTrans_Struct& tExit);
    DBError updateUsedTicket(tExitTrans_Struct& tExit); 
    DBError DeleteBeforeInsertMT(const tExitTrans_Struct& exitTrans); 
    DBError insert2movementtrans(tExitTrans_Struct& tExit); 
    DBError insertbroadcasttrans(
                const std::string& sid,
                const std::string& iuNo,
                const std::string& cardNo = "",
                const std::string& paidAmt = "0.00",
                const std::string& iType = "1");
    DBError UpdateLocalEntry(const std::string& iuTkNo);
    DBError loadmessage();
    DBError loadExitmessage();
    DBError loadParam();
    DBError loadparamfromCentral();
    DBError loadstationsetup();
    DBError loadZoneEntriesfromLocal();
    DBError loadvehicletype();
    DBError loadTR(int iType = 0);
    DBError LoadTariff();
    DBError LoadHoliday();
    DBError ClearHoliday();
    DBError LoadTariffTypeInfo();
    DBError LoadXTariff();
    
    int FnGetVehicleType(const std::string& IUCode);
    std::string GetPartialSeasonMsg(int iTransType);
    int FetchEntryinfo(const std::string& iuNo);

    void moveOfflineTransToCentral();
    int insertTransToCentralEntryTransTmp(const tEntryTrans_Struct& entryTrans);
    int insertTransToCentralExitTransTmp(const tExitTrans_Struct& exitTrans);
    int deleteLocalTrans(const std::string& iuNo, const std::string& transTime, Ctrl_Type ctrl);
    int clearseason();
    int IsBlackListIU(const std::string& iuNo);
    int GetSeasonHolder(const std::string& iuNo);
    int HasAXS(const std::string& iuNo);
    int HasEZpay(const std::string& iuNo);
    std::string GetIUByLPN(const std::string& lpn);
    int CheckCardOK(const std::string& cardNo);
    int AddRemoteControl(const std::string& stationId, const std::string& action, const std::string& remarks);
    int AddSysEvent(const std::string& event, int eventType = 0, std::string occurTime = "");
    int UpdateSysEvent(const std::string& event, int eventType, const std::string& occurTime);
    bool HasAlertNotification();

    int FnGetDatabaseErrorFlag() const;
    int HouseKeeping();
    int clearexpiredseason();
    int updateEntryTrans(const std::string& lpn, const std::string& transId);
    int updateExitTrans(const std::string& lpn, const std::string& transId);
    int UpdateEEPExitTrans(
            const std::string& obu,
            const std::string& dSerialNo,
            const std::string& cardNo,
            float fee,
            float topupAmt,
            int transRoute,
            int result);
    int updateExitReceiptNo(const std::string& receiptNo, const std::string& stationId); 
    int isValidBarCodeTicket(
            bool isRedemptionTicket,
            const std::string& barcodeTicket,
            std::tm& expireTime,
            float& redeemAmount,
            int& redeemTime);
    int HasValidTicket(const std::string& iuNo, const std::string& lpn);
    DBError update99PaymentTrans();
    DBError insertUPTFileSummaryLastSettlement(
            const std::string& settleDate,
            const std::string& settleName,
            int settleType,
            uint64_t totalTrans,
            double totalAmt,
            int sendFlag,
            const std::string& sendDate);
    DBError insertUPTFileSummary(
            const std::string& settleDate,
            const std::string& settleName,
            int settleType,
            uint64_t totalTrans,
            double totalAmt,
            int sendFlag,
            const std::string& sendDate);

    DBError FnUpdateStationSwVersion(const std::string& sid);

    long glToalRowAffed{0};


    db(const db&) = delete;
    db& operator=(const db&) = delete;
    db(db&&) = delete;
    db& operator=(db&&) = delete;

private:

    string localConnStr;
    string CentralConnStr;
    string central_IP;
    float PingTimeOut{0.0F};
   
    template <typename T>
    string ToString(T a);

    DBError loadEntrymessage(const std::vector<ReaderItem>& selResult);
    DBError loadExitLcdAndLedMessage(const std::vector<ReaderItem>& selResult);

    int season_update_flag{0};
    int season_update_count{0};
    int param_update_flag{0};
    int param_update_count{0};

    int CentralDB_TimeOut{0};
    int LocalDB_TimeOut{0};
    int SP_TimeOut{0};

    std::atomic<int> m_local_db_err_flag{0};  // 0=ok, 1=error, 2=update fail
    std::atomic<int> m_remote_db_err_flag{0}; // 0=ok, 1=error, 2=update fail

    std::unique_ptr<odbc> centraldb;
    std::unique_ptr<odbc> localdb;;

    db();
    //-----------------------
    struct tariff_struct gtariff[300][10];
    struct tariff_type_info_struct gtarifftypeinfo[2];
    //---------------
    std::vector<std::string> msholiday;
    std::vector<std::string> mspecialday;
    std::vector<struct XTariff_Struct> msxtariff;
};


