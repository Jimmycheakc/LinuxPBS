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
    paid_amt DECIMAL(10,2) DEFAULT 0.00,
    TK_SerialNo INT DEFAULT 0,
    Status INT DEFAULT 0,
    Send_Status BOOLEAN,
    card_no VARCHAR(20),
    parking_fee DECIMAL(10,2) DEFAULT 0.00,
    gst_amt DECIMAL(10, 2) DEFAULT 0.00,
    Card_Type INT DEFAULT 0,
    Owe_Amt DECIMAL(10,2) DEFAULT 0.00,
    lpn VARCHAR(20),
    entry_lpn_SID VARCHAR(32),
    add_dt DATETIME DEFAULT CURRENT_TIMESTAMP
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
    add_dt DATETIME DEFAULT CURRENT_TIMESTAMP,
    update_dt DATETIME,
    rate_type INT DEFAULT 0,
    pay_to DATETIME,
    pay_date DATETIME,
    multi_season_no VARCHAR(100),
    zone_id VARCHAR(100),
    redeem_amt DECIMAL(10,2) DEFAULT 0.00,
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
    add_dt DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Create a Param_mst table in database
CREATE TABLE IF NOT EXISTS Param_mst (
    ParamID INT AUTO_INCREMENT PRIMARY KEY,
    ParamName VARCHAR(100),
    ParamValue VARCHAR(100),
    AddDT DATETIME DEFAULT CURRENT_TIMESTAMP,
    UpdateDT DATETIME
);

-- Create message_mst table in database
CREATE TABLE IF NOT EXISTS message_mst (
    MsgID INT AUTO_INCREMENT PRIMARY KEY,
    msg_id VARCHAR(100),
    descrip VARCHAR(100),
    msg_body VARCHAR(100),
    m_status INT DEFAULT 0,
    add_dt DATETIME DEFAULT CURRENT_TIMESTAMP,
    update_dt DATETIME
);

-- Create Vehicle_type table in database
CREATE TABLE IF NOT EXISTS Vehicle_type (
    VTypeID INT AUTO_INCREMENT PRIMARY KEY,
    IUCode INT DEFAULT 0,
    TransType INT DEFAULT 0,
    add_dt DATETIME DEFAULT CURRENT_TIMESTAMP,
    update_dt DATETIME
);

-- Create TR_mst table in database
CREATE TABLE IF NOT EXISTS TR_mst (
    TRType INT NOT NULL DEFAULT 0,
    Line_no INT NOT NULL DEFAULT 0,
    Enabled INT DEFAULT 1,
    LineText VARCHAR(50) DEFAULT NULL,
    LineVar VARCHAR(10) DEFAULT NULL,
    LineFont INT DEFAULT 2,
    LineAlign INT DEFAULT 1,
    Spare1 VARCHAR(10) DEFAULT NULL,
    PRIMARY KEY(TRType, Line_no)
);

-- Create Exit_Trans table in database
CREATE TABLE IF NOT EXISTS Exit_Trans (
    EXITID INT AUTO_INCREMENT PRIMARY KEY,
    Station_ID SMALLINT,
    exit_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    iu_tk_no VARCHAR(20),
    card_mc_no VARCHAR(20),
    trans_type SMALLINT,
    parked_time INT DEFAULT 0,
    Parking_Fee DECIMAL(7,2) DEFAULT 0.00,
    Paid_Amt DECIMAL(7,2) DEFAULT 0.00,
    Receipt_No VARCHAR(20),
    Send_Status TINYINT(1) DEFAULT 0,
    Redeem_amt DECIMAL(7,2) DEFAULT 0.00,
    Redeem_time SMALLINT DEFAULT 0,
    Redeem_no VARCHAR(20),
    Status SMALLINT DEFAULT 0,
    gst_amt DECIMAL(7,2) DEFAULT 0.00,
    chu_debit_code VARCHAR(20) DEFAULT 0,
    Card_Type SMALLINT DEFAULT 0,
    Top_Up_Amt DECIMAL(7,2) DEFAULT 0.00,
    lpn VARCHAR(50),
    exit_lpn_SID VARCHAR(32),
    uposbatchno VARCHAR(20),
    feefrom VARCHAR(10),
    Entry_ID SMALLINT,
    entry_time DATETIME,
    Add_dt DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Create Rate_Type_Info table in database
CREATE TABLE IF NOT EXISTS Rate_Type_Info (
    Rate_Type SMALLINT NOT NULL PRIMARY KEY,
    Has_Holiday TINYINT NOT NULL DEFAULT 0,
    Has_Holiday_Eve TINYINT NOT NULL DEFAULT 0,
    Has_Special_Day TINYINT NOT NULL DEFAULT 0,
    Has_Init_Free TINYINT NOT NULL DEFAULT 0,
    Has_3Tariff TINYINT NOT NULL DEFAULT 0,
    Has_Zone_Max TINYINT NOT NULL DEFAULT 0,
    Has_FirstEntry_Rate TINYINT NOT NULL DEFAULT 0,
    Add_Date DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Create Special_Day_mst table in database
CREATE TABLE IF NOT EXISTS Special_Day_mst (
    Special_Date DATETIME NOT NULL PRIMARY KEY,
    Rate_Type TINYINT DEFAULT 0,
    Day_Code VARCHAR(10),
    Add_Date DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Create Rate_Free_Info table in database
CREATE TABLE IF NOT EXISTS Rate_Free_Info (
    Rate_Type SMALLINT NOT NULL,
    Day_Type VARCHAR(50) NOT NULL,
    Init_Free SMALLINT NOT NULL DEFAULT 0,
    Free_Beg VARCHAR(50) NOT NULL,
    Free_End VARCHAR(50) NOT NULL,
    Free_Time SMALLINT NOT NULL DEFAULT 0,
    Add_Date DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Create 3Tariff_Info table in database
CREATE TABLE IF NOT EXISTS 3Tariff_Info (
    Rate_Type SMALLINT NOT NULL PRIMARY KEY,
    Day_Type VARCHAR(50) NOT NULL,
    Time_From VARCHAR(50) NOT NULL,
    Time_Till VARCHAR(50) NOT NULL,
    T3_Start SMALLINT NOT NULL,
    T3_Block SMALLINT NOT NULL DEFAULT 0,
    T3_Rate DECIMAL(7,2) NOT NULL DEFAULT 0.00,
    Add_Date DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Create Rate_Max_Info table in database
CREATE TABLE IF NOT EXISTS Rate_Max_Info (
    Rate_Type SMALLINT NOT NULL,
    Day_Type VARCHAR(50) NOT NULL,
    Start_Time VARCHAR(50) NOT NULL,
    End_Time VARCHAR(50) NOT NULL,
    Max_Fee DECIMAL(7,2) Not NULL DEFAULT 0.00,
    Add_Date DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Create holiday_mst table in database
CREATE TABLE IF NOT EXISTS holiday_mst (
    holiday_date DATETIME NOT NULL PRIMARY KEY,
    descrip VARCHAR(20) NOT NULL,
    add_dt DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Create tariff_setup table in database
CREATE TABLE IF NOT EXISTS tariff_setup (
    tariff_id INT PRIMARY KEY,
    day_index TINYINT DEFAULT 0,
    start_time1 DATETIME,
    end_time1 DATETIME,
    rate_type1 TINYINT DEFAULT 0,
    charge_time_block1 SMALLINT DEFAULT 0,
    charge_rate1 DECIMAL(7,3) DEFAULT 0.000,
    grace_time1 SMALLINT DEFAULT 0,
    min_charge1 DECIMAL(7,3) DEFAULT 0.000,
    max_charge1 DECIMAL(7,3) DEFAULT 0.000,
    first_free1 SMALLINT DEFAULT 0,
    first_add1 DECIMAL(7,3) DEFAULT 0.000,
    second_free1 SMALLINT DEFAULT 0,
    second_add1 DECIMAL(7,3) DEFAULT 0.000,
    third_free1 SMALLINT DEFAULT 0,
    third_add1 DECIMAL(7,3) DEFAULT 0.000,
    allowance1 SMALLINT DEFAULT 0,
    start_time2 DATETIME,
    end_time2 DATETIME,
    rate_type2 TINYINT DEFAULT 0,
    charge_time_block2 SMALLINT DEFAULT 0,
    charge_rate2 DECIMAL(7,3) DEFAULT 0.000,
    grace_time2 SMALLINT DEFAULT 0,
    min_charge2 DECIMAL(7,3) DEFAULT 0.000,
    max_charge2 DECIMAL(7,3) DEFAULT 0.000,
    first_free2 SMALLINT DEFAULT 0,
    first_add2 DECIMAL(7,3) DEFAULT 0.000,
    second_free2 SMALLINT DEFAULT 0,
    second_add2 DECIMAL(7,3) DEFAULT 0.000,
    third_free2 SMALLINT DEFAULT 0,
    third_add2 DECIMAL(7,3) DEFAULT 0.000,
    allowance2 SMALLINT DEFAULT 0,
    start_time3 DATETIME,
    end_time3 DATETIME,
    rate_type3 TINYINT DEFAULT 0,
    charge_time_block3 SMALLINT DEFAULT 0,
    charge_rate3 DECIMAL(7,3) DEFAULT 0.000,
    grace_time3 SMALLINT DEFAULT 0,
    min_charge3 DECIMAL(7,3) DEFAULT 0.000,
    max_charge3 DECIMAL(7,3) DEFAULT 0.000,
    first_free3 SMALLINT DEFAULT 0,
    first_add3 DECIMAL(7,3) DEFAULT 0.000,
    second_free3 SMALLINT DEFAULT 0,
    second_add3 DECIMAL(7,3) DEFAULT 0.000,
    third_free3 SMALLINT DEFAULT 0,
    third_add3 DECIMAL(7,3) DEFAULT 0.000,
    allowance3 SMALLINT DEFAULT 0,
    start_time4 DATETIME,
    end_time4 DATETIME,
    rate_type4 TINYINT DEFAULT 0,
    charge_time_block4 SMALLINT DEFAULT 0,
    charge_rate4 DECIMAL(7,3) DEFAULT 0.000,
    grace_time4 SMALLINT DEFAULT 0,
    min_charge4 DECIMAL(7,3) DEFAULT 0.000,
    max_charge4 DECIMAL(7,3) DEFAULT 0.000,
    first_free4 SMALLINT DEFAULT 0,
    first_add4 DECIMAL(7,3) DEFAULT 0.000,
    second_free4 SMALLINT DEFAULT 0,
    second_add4 DECIMAL(7,3) DEFAULT 0.000,
    third_free4 SMALLINT DEFAULT 0,
    third_add4 DECIMAL(7,3) DEFAULT 0.000,
    allowance4 SMALLINT DEFAULT 0,
    start_time5 DATETIME,
    end_time5 DATETIME,
    rate_type5 TINYINT DEFAULT 0,
    charge_time_block5 SMALLINT DEFAULT 0,
    charge_rate5 DECIMAL(7,3) DEFAULT 0.000,
    grace_time5 SMALLINT DEFAULT 0,
    min_charge5 DECIMAL(7,3) DEFAULT 0.000,
    max_charge5 DECIMAL(7,3) DEFAULT 0.000,
    first_free5 SMALLINT DEFAULT 0,
    first_add5 DECIMAL(7,3) DEFAULT 0.000,
    second_free5 SMALLINT DEFAULT 0,
    second_add5 DECIMAL(7,3) DEFAULT 0.000,
    third_free5 SMALLINT DEFAULT 0,
    third_add5 DECIMAL(7,3) DEFAULT 0.000,
    allowance5 SMALLINT DEFAULT 0,
    start_time6 DATETIME,
    end_time6 DATETIME,
    rate_type6 TINYINT DEFAULT 0,
    charge_time_block6 SMALLINT DEFAULT 0,
    charge_rate6 DECIMAL(7,3) DEFAULT 0.000,
    grace_time6 SMALLINT DEFAULT 0,
    min_charge6 DECIMAL(7,3) DEFAULT 0.000,
    max_charge6 DECIMAL(7,3) DEFAULT 0.000,
    first_free6 SMALLINT DEFAULT 0,
    first_add6 DECIMAL(7,3) DEFAULT 0.000,
    second_free6 SMALLINT DEFAULT 0,
    second_add6 DECIMAL(7,3) DEFAULT 0.000,
    third_free6 SMALLINT DEFAULT 0,
    third_add6 DECIMAL(7,3) DEFAULT 0.000,
    allowance6 SMALLINT DEFAULT 0,
    start_time7 DATETIME,
    end_time7 DATETIME,
    rate_type7 TINYINT DEFAULT 0,
    charge_time_block7 SMALLINT DEFAULT 0,
    charge_rate7 DECIMAL(7,3) DEFAULT 0.000,
    grace_time7 SMALLINT DEFAULT 0,
    min_charge7 DECIMAL(7,3) DEFAULT 0.000,
    max_charge7 DECIMAL(7,3) DEFAULT 0.000,
    first_free7 SMALLINT DEFAULT 0,
    first_add7 DECIMAL(7,3) DEFAULT 0.000,
    second_free7 SMALLINT DEFAULT 0,
    second_add7 DECIMAL(7,3) DEFAULT 0.000,
    third_free7 SMALLINT DEFAULT 0,
    third_add7 DECIMAL(7,3) DEFAULT 0.000,
    allowance7 SMALLINT DEFAULT 0,
    start_time8 DATETIME,
    end_time8 DATETIME,
    rate_type8 TINYINT DEFAULT 0,
    charge_time_block8 SMALLINT DEFAULT 0,
    charge_rate8 DECIMAL(7,3) DEFAULT 0.000,
    grace_time8 SMALLINT DEFAULT 0,
    min_charge8 DECIMAL(7,3) DEFAULT 0.000,
    max_charge8 DECIMAL(7,3) DEFAULT 0.000,
    first_free8 SMALLINT DEFAULT 0,
    first_add8 DECIMAL(7,3) DEFAULT 0.000,
    second_free8 SMALLINT DEFAULT 0,
    second_add8 DECIMAL(7,3) DEFAULT 0.000,
    third_free8 SMALLINT DEFAULT 0,
    third_add8 DECIMAL(7,3) DEFAULT 0.000,
    allowance8 SMALLINT DEFAULT 0,
    start_time9 DATETIME,
    end_time9 DATETIME,
    rate_type9 TINYINT DEFAULT 0,
    charge_time_block9 SMALLINT DEFAULT 0,
    charge_rate9 DECIMAL(7,3) DEFAULT 0.000,
    grace_time9 SMALLINT DEFAULT 0,
    min_charge9 DECIMAL(7,3) DEFAULT 0.000,
    max_charge9 DECIMAL(7,3) DEFAULT 0.000,
    first_free9 SMALLINT DEFAULT 0,
    first_add9 DECIMAL(7,3) DEFAULT 0.000,
    second_free9 SMALLINT DEFAULT 0,
    second_add9 DECIMAL(7,3) DEFAULT 0.000,
    third_free9 SMALLINT DEFAULT 0,
    third_add9 DECIMAL(7,3) DEFAULT 0.000,
    allowance9 SMALLINT DEFAULT 0,
    whole_day_max DECIMAL(7,3) DEFAULT 0.000,
    whole_day_min DECIMAL(7,3) DEFAULT 0.000,
    zone_cutoff INT DEFAULT 0,
    day_cutoff SMALLINT DEFAULT 0,
    day_type VARCHAR(100),
    update_dt DATETIME,
    add_dt DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Create tariff_type_info table in database
CREATE TABLE IF NOT EXISTS tariff_type_info (
    tariff_type SMALLINT PRIMARY KEY,
    start_time DATETIME,
    end_time DATETIME,
    update_dt DATETIME,
    add_dt DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- Create X_Tariff table in database
CREATE TABLE IF NOT EXISTS X_Tariff (
    day_index VARCHAR(100) PRIMARY KEY,
    auto0 TINYINT DEFAULT 0,
    fee0 DECIMAL(5,2) DEFAULT 0.00,
    time1 VARCHAR(8),
    auto1 TINYINT DEFAULT 0,
    fee1 DECIMAL(5,2) DEFAULT 0.00,
    time2 VARCHAR(8),
    auto2 TINYINT DEFAULT 0,
    fee2 DECIMAL(5,2) DEFAULT 0.00,
    time3 VARCHAR(8),
    auto3 TINYINT DEFAULT 0,
    fee3 DECIMAL(5,2) DEFAULT 0.00,
    time4 VARCHAR(8),
    auto4 TINYINT DEFAULT 0,
    fee4 DECIMAL(5,2) DEFAULT 0.00
);

-- Create a new user and grant privileges
CREATE USER IF NOT EXISTS 'linuxpbs'@'localhost' IDENTIFIED BY 'SJ2001';
GRANT ALL PRIVILEGES ON linux_pbs.* TO 'linuxpbs'@'localhost';
FLUSH PRIVILEGES;