#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/serial_port.hpp>

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

    Printer(const Printer&) = delete;
    Printer& operator=(const Printer&) = delete;
    Printer(Printer&&) = delete;
    Printer& operator=(Printer&&) = delete;

    bool FnPrinterInit(unsigned int baudRate, const std::string& comPortName);
    void FnPrinterClose();

    void FnSetPrintMode(int mode);
    int FnGetPrintMode() const;

    void FnSetDefaultAlign(CBM_ALIGN align);
    CBM_ALIGN FnGetDefaultAlign() const;

    void FnSetDefaultFont(int font);
    int FnGetDefaultFont() const;

    void FnSetLeftMargin(int leftMargin);
    int FnGetLeftMargin() const;

    void FnSetLineSpace(int space);
    int FnGetLineSpace() const;

    void FnSetSelfTestInterval(int interval);
    int FnGetSelfTestInterval() const;

    void FnSetSiteID(int id);
    int FnGetSiteID() const;

    void FnSetPrinterType(PRINTER_TYPE type);
    PRINTER_TYPE FnGetPrinterType() const;


    void FnPrintLine(const std::string& text, int font = 0, int align = 0, bool underline = false, int font2 = 0);

    void FnFullCut(int bottom = 0);
    void FnGetAllFonts();

    void FnPrintBarCode(const std::string& text, int height = 0, int width = 0, int fontSetting = 21);

    void FnFeedLine(int line);

private:
    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
    
    Printer();
    ~Printer();

    bool startIoThreadLocked();
    bool initOnIoThread(unsigned int baudRate, const std::string& comPortName);
    void shutdownOnIoThread();

    void configurePrinterCommandsOnIoThread(bool sendFtpSetup);

    void startReadOnIoThread();
    void handleReadOnIoThread(const boost::system::error_code& error, std::size_t bytesTransferred);

    void enqueueWriteOnIoThread(std::vector<std::uint8_t> data);
    void startWriteOnIoThread();
    void handleWriteOnIoThread(const boost::system::error_code& error, std::size_t bytesTransferred);

    void handleCmdResponseOnIoThread(const std::vector<std::uint8_t>& response);
    
    void startMonitorStatusTimerOnIoThread();
    void handleMonitorStatusTimeoutOnIoThread(const boost::system::error_code& error);
    
    void startSelfTestTimerOnIoThread(int milliseconds);
    void handleSelfTestTimeoutOnIoThread(const boost::system::error_code& error);
    
    void inquireStatusOnIoThread();
    void publishStatusOnIoThread(PRINTER_STATUS status, bool force = false);
    
    void printLineOnIoThread(const std::string& text, int font, int align, bool underline, int font2);

    void fullCutOnIoThread(int bottom);
    void getAllFontsOnIoThread();

    void printBarcodeOnIoThread(const std::string& text, int height, int width, int fontSetting);
    
    void feedLineOnIoThread(int line);

    void log(const std::string& message) const;
    void logException(const std::string& functionName, const std::exception& exception) const;
    
    static std::vector<std::uint8_t> toBytes(const std::string& value);
    static std::string toHex(const std::vector<std::uint8_t>& data);
    static const char* printerTypeName(PRINTER_TYPE type);
    static const char* statusName(PRINTER_STATUS status);

    boost::asio::io_context ioContext_;
    boost::asio::serial_port serialPort_;
    boost::asio::steady_timer selfTestTimer_;
    boost::asio::steady_timer monitorStatusTimer_;
    std::optional<WorkGuard> workGuard_;
    std::thread ioThread_;

    mutable std::mutex lifecycleMutex_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> initialized_{false};
    std::atomic<bool> acceptingWork_{false};

    std::deque<std::vector<std::uint8_t>> writeQueue_;
    bool writeInProgress_{false};
    std::array<std::uint8_t, 1024> readBuffer_{};

    std::atomic<int> defaultFont_{2};
    std::atomic<int> defaultAlign_{static_cast<int>(CBM_ALIGN::CBM_LEFT)};
    std::atomic<int> lineSpace_{6};
    std::atomic<int> leftMargin_{0};
    std::atomic<int> printMode_{0};
    std::atomic<PRINTER_TYPE> printerType_{PRINTER_TYPE::CBM1000};
    std::atomic<int> siteID_{0};
    std::atomic<int> selfTestInterval_{0};

    std::string cmdLeftMargin_;
    std::string cmdCut_;
    int lastAlign_{static_cast<int>(CBM_ALIGN::CBM_LEFT)};

    std::array<std::string, 4> alignCommands_{};
    std::array<std::string, 17> cbmFonts_{};
    std::array<std::string, 13> ftpFonts_{};
    std::array<std::string, 17> activeFonts_{};

    PRINTER_STATUS currentStatus_{PRINTER_STATUS::IDLE};

    const std::string logFileName_{"printer"};

    static constexpr std::chrono::seconds kMonitorStatusInterval{10};
};