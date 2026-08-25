#pragma once

// PMS / Monitor UDP application-protocol command identifiers.
//
// Keep these protocol definitions independent from udpclient so udp.h remains
// transport-only and Operation can own protocol parsing/dispatch.

enum class UdpRxCommand : unsigned int
{
    StopStationSoftware = 11,
    StatusEnquiry       = 13,
    UpdateSeason        = 20,
    DownloadMsg         = 22,
    UpdateParam         = 23,
    CarparkFull         = 24,
    OpenBarrier         = 27,
    StatusOnline        = 28,
    ContinueOpenBarrier = 30,
    DownloadTariff      = 32,
    DownloadHoliday     = 33,
    SetTime             = 35,
    TimeForNoEntry      = 36,
    ClearSeason         = 37,
    DownloadType        = 41,
    CloseBarrier        = 42,
    DownloadXTariff     = 45,
    DownloadTR          = 49,
    UpdateSetting       = 51,
    SetLotCount         = 65,
    LockupBarrier       = 67,
    AvailableLots       = 68,
    BroadcastSaveTrans  = 90,
    FeeTest             = 301,
    SetDioOutput        = 303,
    EEPStatus           = 800
};

enum class MonitorUdpRxCommand : unsigned int
{
    // Command 11 is accepted on the Monitor channel as well as PMS.
    StopStationSoftware  = 11,
    MonitorEnquiry       = 300,
    MonitorFeeTest       = 301,
    MonitorOutput        = 303,
    DownloadIni          = 309,
    DownloadParam        = 310,
    MonitorSyncTime      = 311,
    MonitorStatus        = 312,
    MonitorStationVersion = 313,
    MonitorGetStationCurrLog = 314
};
