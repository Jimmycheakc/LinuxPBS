#pragma once

#include <cstdint>

class LCD
{

public:
    static const int MAXIMUM_CHARACTER_PER_ROW  = 20;
    static const int MAXIMUM_LCD_LINES          = 2;
    static const char BLOCK                     = '�';

    static LCD* getInstance();
    void FnLCDInit();
    void FnLCDClear();
    void FnLCDHome();
    void FnLCDDisplayCharacter(char aChar);
    void FnLCDDisplayString(std::uint8_t row, std::uint8_t col, char* str);
    void FnLCDDisplayStringCentered(std::uint8_t row, char* str);
    void FnLCDDisplayRow(std::uint8_t row, char* str);
    void FnLCDDisplayScreen(char* str);
    void FnLCDWipeOnLR(char* str);
    void FnLCDWipeOnRL(char* str);
    void FnLCDWipeOffLR();
    void FnLCDWipeOffRL();
    void FnLCDCursorLeft();
    void FnLCDCursorRight();
    void FnLCDCursorOn();
    void FnLCDCursorOff();
    void FnLCDDisplayOff();
    void FnLCDDisplayOn();
    void FnLCDCursorReset();
    void FnLCDCursor(std::uint8_t row, std::uint8_t col);
    void FnLCDClose();

    /**
     * Singleton LCD should not be cloneable.
     */
    LCD (LCD& lcd) = delete;

    /**
     * Singleton LCD should not be assignable.
     */
    void operator=(const LCD&) = delete;

private:
    static LCD* lcd_;
    int lcdFd_;
    bool lcdInitialized_;
    LCD();
    void FnLCDInitDriver();
    void FnLCDDeinitDriver();
};