-- CreateLocalDB_linuxpbs.sql

-- Create a new database
CREATE DATABASE IF NOT EXISTS linux_pbs;
USE linux_pbs;

-- Create a Entry_Trans table in database
CREATE TABLE IF NOT EXISTS Entry_Trans (
    EntryID INT AUTO_INCREMENT PRIMARY KEY,
    Station_id INT DEFAULT 0,
    Entry_Time DATETIME,
    iu_tk_no VARCHAR(20),
    trans_type INT DEFAULT 0,
    paid_amt DECIMAL(10,2) DEFAULT 0,
    TK_SerialNo INT DEFAULT 0,
    Status INT DEFAULT 0,
    Send_Status BOOLEAN,
    card_no VARCHAR(20),
    parking_fee DECIMAL(10,2) DEFAULT 0,
    gst_amt DECIMAL(10, 2) DEFAULT 0,
    Card_Type INT DEFAULT 0,
    Owe_Amt DECIMAL(10,2) DEFAULT 0,
    lpr VARCHAR(20),
    add_dt TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Create a session_mst table in database
CREATE TABLE IF NOT EXISTS season_mst (
    season_id INT AUTO_INCREMENT PRIMARY KEY,
    season_no VARCHAR(20),
    season_type INT DEFAULT 0,
    s_status INT DEFAULT 0,
    date_from DATETIME,
    date_to DATETIME,
    vehicle_no VARCHAR(100),
    add_dt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    update_dt DATETIME,
    rate_type INT DEFAULT 0,
    pay_to DATETIME,
    pay_date DATETIME,
    multi_season_no VARCHAR(100),
    zone_id VARCHAR(100),
    redeem_amt DECIMAL(10,2) DEFAULT 0,
    redeem_time INT DEFAULT 0,
    holder_type INT DEFAULT 0,
    sub_zone_id VARCHAR(100)
);

-- Create a Station_Setup table in database
CREATE TABLE IF NOT EXISTS Station_Setup (
    StationID INT DEFAULT 0,
    StationName VARCHAR(100),
    StationType INT DEFAULT 0,
    Status INT DEFAULT 0,
    PCName VARCHAR(100),
    CHUPort INT DEFAULT 0,
    AntID INT DEFAULT 0,
    ZoneID INT DEFAULT 0,
    IsVirtual INT DEFAULT 0,
    SubType INT DEFAULT 0,
    VirtualID INT DEFAULT 0,
    add_dt TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Create a Param_mst table in database
CREATE TABLE IF NOT EXISTS Param_mst (
    ParamID INT AUTO_INCREMENT PRIMARY KEY,
    ParamName VARCHAR(100),
    ParamValue VARCHAR(100),
    AddDT TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UpdateDT DATETIME
);

-- Create message_mst table in database
CREATE TABLE IF NOT EXISTS message_mst (
    MsgID INT AUTO_INCREMENT PRIMARY KEY,
    msg_id VARCHAR(100),
    descrip VARCHAR(100),
    msg_body VARCHAR(100),
    m_status INT DEFAULT 0,
    add_dt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    update_dt DATETIME
);

-- Create Vehicle_type table in database
CREATE TABLE IF NOT EXISTS Vehicle_type (
    VTypeID INT AUTO_INCREMENT PRIMARY KEY,
    IUCode INT DEFAULT 0,
    TransType INT DEFAULT 0,
    add_dt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    update_dt DATETIME
);