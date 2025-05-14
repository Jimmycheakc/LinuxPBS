#pragma once

#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <queue>
#include "boost/asio.hpp"
#include "boost/asio/serial_port.hpp"

namespace ASCII
{
    // Define constexpr characters using short-form names
    constexpr char NUL = 0;
    constexpr char SOH = 1; // Start of Heading
    constexpr char STX = 2; // Start of Text
    constexpr char ETX = 3; // End of Text
    constexpr char EOT = 4; // End of Transmission
    constexpr char ENQ = 5; // Enquiry
    constexpr char ACK = 6; // Acknowlege
    constexpr char BEL = 7; // Bell
    constexpr char BS  = 8; // Backspace
    constexpr char TAB = 9; // Horizontal Tab
    constexpr char LF  = 10; // Line Feed
    constexpr char VT  = 11; // Vertical Tab
    constexpr char FF  = 12; // Form Feed
    constexpr char CR  = 13; // Carriage Return
    constexpr char SO  = 14; // Shift Out
    constexpr char SI  = 15; // Shift In
    constexpr char DLE = 16; // Data Link Escape
    constexpr char DC1 = 17; // Device Control 1
    constexpr char DC2 = 18; // Device Control 2
    constexpr char DC3 = 19; // Device Control 3
    constexpr char DC4 = 20; // Device Control 4
    constexpr char NAK = 21; // Negative Acknowledge
    constexpr char SYN = 22; // Synchronous Idle
    constexpr char ETB = 23; // End of Transmission Block
    constexpr char CAN = 24; // Cancel
    constexpr char EM  = 25; // End of Medium
    constexpr char SUB = 26; // Substitute
    constexpr char ESC = 27; // Escape
    constexpr char FS  = 28; // File Separator
    constexpr char GS  = 29; // Group Separator
    constexpr char RS  = 30; // Record Separator
    constexpr char US  = 31; // Unit Separator
    constexpr char SPACE     = 32;
    constexpr char EXCLAM    = 33; // !
    constexpr char QUOTE     = 34; // "
    constexpr char HASH      = 35; // #
    constexpr char DOLLAR    = 36; // $
    constexpr char PERCENT   = 37; // %
    constexpr char AMP       = 38; // &
    constexpr char APOST     = 39; // '
    constexpr char LPAREN    = 40; // (
    constexpr char RPAREN    = 41; // )
    constexpr char ASTER     = 42; // *
    constexpr char PLUS      = 43; // +
    constexpr char COMMA     = 44; // ,
    constexpr char HYPHEN    = 45; // -
    constexpr char PERIOD    = 46; // .
    constexpr char SLASH     = 47; // /
    constexpr char DIGIT_0   = 48;
    constexpr char DIGIT_1   = 49;
    constexpr char DIGIT_2   = 50;
    constexpr char DIGIT_3   = 51;
    constexpr char DIGIT_4   = 52;
    constexpr char DIGIT_5   = 53;
    constexpr char DIGIT_6   = 54;
    constexpr char DIGIT_7   = 55;
    constexpr char DIGIT_8   = 56;
    constexpr char DIGIT_9   = 57;
    constexpr char COLON     = 58; // :
    constexpr char SEMICOLON = 59; // ;
    constexpr char LESS_THAN = 60; // <
    constexpr char EQUALS    = 61; // =
    constexpr char GREATER_THAN   = 62; // >
    constexpr char QUESTION  = 63; // ?
    constexpr char AT        = 64; // @
    constexpr char A         = 65;
    constexpr char B         = 66;
    constexpr char C         = 67;
    constexpr char D         = 68;
    constexpr char E         = 69;
    constexpr char F         = 70;
    constexpr char G         = 71;
    constexpr char H         = 72;
    constexpr char I         = 73;
    constexpr char J         = 74;
    constexpr char K         = 75;
    constexpr char L         = 76;
    constexpr char M         = 77;
    constexpr char N         = 78;
    constexpr char O         = 79;
    constexpr char P         = 80;
    constexpr char Q         = 81;
    constexpr char R         = 82;
    constexpr char S         = 83;
    constexpr char T         = 84;
    constexpr char U         = 85;
    constexpr char V         = 86;
    constexpr char W         = 87;
    constexpr char X         = 88;
    constexpr char Y         = 89;
    constexpr char Z         = 90;
    constexpr char LBRACKET  = 91; // [
    constexpr char BACKSLASH = 92; // '\'
    constexpr char RBRACKET  = 93; // ]
    constexpr char CARET     = 94; // ^
    constexpr char UNDERSCORE = 95; // _
    constexpr char GRAVE      = 96; // `
    constexpr char a         = 97;
    constexpr char b         = 98;
    constexpr char c         = 99;
    constexpr char d         = 100;
    constexpr char e         = 101;
    constexpr char f         = 102;
    constexpr char g         = 103;
    constexpr char h         = 104;
    constexpr char i         = 105;
    constexpr char j         = 106;
    constexpr char k         = 107;
    constexpr char l         = 108;
    constexpr char m         = 109;
    constexpr char n         = 110;
    constexpr char o         = 111;
    constexpr char p         = 112;
    constexpr char q         = 113;
    constexpr char r         = 114;
    constexpr char s         = 115;
    constexpr char t         = 116;
    constexpr char u         = 117;
    constexpr char v         = 118;
    constexpr char w         = 119;
    constexpr char x         = 120;
    constexpr char y         = 121;
    constexpr char z         = 122;
    constexpr char LBRACE    = 123; // {
    constexpr char VBAR      = 124; // |
    constexpr char RBRACE    = 125; // }
    constexpr char TILDE     = 126; // ~
    constexpr char DEL       = 127; // Delete
}

class Printer
{

public:

    enum class PRINTER_STATUS : int
    {
        ERROR = -1,
        IDLE  = 0,
        NO_PAPER = 1
    };

    enum class PRINTER_TYPE
    {
        CBM = 1,
        FTP = 2,
        CBM1000 = 3
    };

    enum class CBM_ALIGN
    {
        CBM_LEFT   = 1,
        CBM_CENTER = 2,
        CBM_RIGHT  = 3
    };

    static Printer* getInstance();

    /*
     * Singleton Printer should not be cloneable
     */
    Printer(Printer& printer) = delete;

    /*
     * Singleton Printer should not be assignable
     */
    void operator=(const Printer&) = delete;

    bool FnPrinterInit(unsigned int baudRate, const std::string& comPortName);
    void FnSetPrintMode(int mode);
    int FnGetPrintMode();
    void FnSetDefaultAlign(CBM_ALIGN align);
    CBM_ALIGN FnGetDefaultAlign();
    void FnSetDefaultFont(int font);
    int FnGetDefaultFont();
    void FnSetLeftMargin(int leftMargin);
    int FnGetLeftMargin();
    void FnSetLineSpace(int space);
    int FnGetLineSpace();
    void FnSetSelfTestInterval(int interval);
    int FnGetSelfTestInterval();
    void FnSetSiteID(int id);
    int FnGetSiteID();
    void FnSetPrinterType(PRINTER_TYPE type);
    PRINTER_TYPE FnGetPrinterType();


    void FnPrintLine(const std::string& text, int font = 0, int align = 0, bool underline = false, int font2 = 0);
    void FnFullCut(int bottom = 0);
    void FnGetAllFonts();
    void FnPrintBarCode(const std::string& text, int height = 0, int width = 0, int fontSetting = 21);
    void FnFeedLine(int line);

    void FnPrinterClose();

private:
    static Printer* printer_;
    static std::mutex mutex_;
    boost::asio::io_context ioContext_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    std::unique_ptr<boost::asio::serial_port> pSerialPort_;
    std::string logFileName_;
    std::thread ioContextThread_;
    std::queue<std::vector<uint8_t>> writeQueue_;
    std::array<uint8_t, 1024> readBuffer_;
    int defaultFont_;
    int defaultAlign_;
    int lineSpace_;
    int leftMargin_;
    int printMode_;
    PRINTER_TYPE printerType_;
    int siteID_;
    std::string cmdLeftMargin_;
    std::string cmdCut_;
    int lastAlign_;
    std::string Align_[4];
    std::string FC_[17];
    std::string FF_[13];
    std::string Font_[17];
    bool isPrinterError_;
    int selfTestInterval_;
    boost::asio::deadline_timer selfTestTimer_;
    boost::asio::deadline_timer monitorStatusTimer_;
    Printer();
    void startIoContextThread();
    void setPrinterSetting(PRINTER_TYPE type, int align, int font, int siteID, int leftMargin, int milliseconds);
    void startSelfTestTimer(int milliseconds);
    void handleSelfTestTimerTimeout(const boost::system::error_code& error);
    void inqStatus();
    void handleCmdResponse(const std::vector<uint8_t>& rsp);
    void startMonitorStatusTimer();
    void handleMonitorStatusTimeout(const boost::system::error_code& error);

    // Serial read and write
    void startRead();
    void readEnd(const boost::system::error_code& error, std::size_t bytesTransferred);
    void enqueueWrite(const std::vector<uint8_t>& data);
    void startWrite();
    void writeEnd(const boost::system::error_code& error, std::size_t bytesTransferred);
};