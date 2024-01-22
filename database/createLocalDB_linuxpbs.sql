-- CreateLocalDB_linuxpbs.sql

-- Create a new database
CREATE DATABASE IF NOT EXISTS linux_pbs;
USE linux_pbs;

-- Create a Entry_Trans table in database
CREATE TABLE IF NOT EXISTS Entry_Trans (
    EntryID INT AUTO_INCREMENT PRIMARY KEY,
    Station_id INT,
    Entry_Time DATETIME,
    iu_tk_no VARCHAR(20),
    trans_type INT,
    paid_amt INT,
    TK_SerialNo INT,
    Status INT,
    Send_Status BOOLEAN,
    card_no VARCHAR(20),
    parking_fee INT,
    gst_amt DECIMAL(10, 2),
    Card_Type INT DEFAULT 0,
    Owe_Amt INT,
    lpr VARCHAR(20),
    add_dt TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Create a session_mst table in database
CREATE TABLE IF NOT EXISTS season_mst (
    season_id INT AUTO_INCREMENT PRIMARY KEY,
    season_no VARCHAR(20),
    season_type INT,
    s_status INT,
    date_from DATETIME,
    date_to DATETIME,
    vehicle_no VARCHAR(20),
    add_dt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    update_dt DATETIME,
    rate_type INT DEFAULT 0,
    pay_to DATETIME,
    pay_date DATETIME,
    multi_season_no VARCHAR(20),
    zone_id INT,
    redeem_amt INT,
    redeem_time INT,
    holder_type INT,
    sub_zone_id INT
);

-- Create a Station_Setup table in database
CREATE TABLE IF NOT EXISTS Station_Setup (
    StationID INT,
    StationName VARCHAR(20),
    StationType INT,
    Status INT,
    PCName VARCHAR(20),
    CHUPort INT,
    AntID INT,
    ZoneID INT DEFAULT 0,
    IsVirtual INT,
    SubType INT,
    VirtualID INT,
    add_dt TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Create a Param_mst table in database
CREATE TABLE IF NOT EXISTS Param_mst (
    ParamID INT AUTO_INCREMENT PRIMARY KEY,
    ParamName VARCHAR(20),
    ParamValue VARCHAR(20),
    AddDT TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UpdateDT DATETIME
);

-- Create message_mst table in database
CREATE TABLE IF NOT EXISTS message_mst (
    MsgID INT AUTO_INCREMENT PRIMARY KEY,
    msg_id VARCHAR(20),
    descrip VARCHAR(20),
    msg_body VARCHAR(20),
    m_status INT,
    add_dt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    update_dt DATETIME
);

-- Create Vehicle_type table in database
CREATE TABLE IF NOT EXISTS Vehicle_type (
    VTypeID INT AUTO_INCREMENT PRIMARY KEY,
    IUCode INT,
    TransType INT,
    add_dt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    update_dt DATETIME
);