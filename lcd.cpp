#include <sstream>
#include <unistd.h>
#include "ch341_lib.h"
#include "lcd.h"
#include "log.h"
#include "ps_par.h"

LCD* LCD::lcd_ = nullptr;
std::mutex LCD::mutex_;

LCD::LCD()
    : lcdFd_(0),
    lcdInitialized_(false)
{

}

LCD* LCD::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (lcd_ == nullptr)
    {
        lcd_ = new LCD();
    }
    return lcd_;
}

bool LCD::FnLCDInit()
{
    FnLCDInitDriver();
    FnLCDClear();
    FnLCDCursorOff();
    FnLCDHome();
    usleep(250000); // Sleep for 0.25 seconds

    if (lcdInitialized_)
    {
        Logger::getInstance()->FnLog("LCD initialization completed.");
    }
    else
    {
        Logger::getInstance()->FnLog("LCD initialization failed.");
    }

    return lcdInitialized_;
}

void LCD::FnLCDInitDriver()
{
    if (!lcdInitialized_)
    {
        lcdFd_ = CH34xOpenDevice("/dev/ch34x_pis0");
        if (lcdFd_ < 0)
        {
            std::stringstream ss;
            ss << "Failed to open CH34x LCD driver, fd = " << std::to_string(lcdFd_) << std::endl;
            Logger::getInstance()->FnLog(ss.str());
        }
        else
        {
            char cmd1[] = "0x38";
            ps_par(lcdFd_, cmd1, 0);
            char cmd2[] = "0x06";
            ps_par(lcdFd_, cmd2, 0);
            char cmd3[] = "0x0C";
            ps_par(lcdFd_, cmd3, 0);
        }
        lcdInitialized_ = true;
    }
}

void LCD::FnLCDClear()
{
    if (lcdInitialized_ == false)
        return;

    char cmd[] = "0x01";
    ps_par(lcdFd_, cmd, 0);
}

void LCD::FnLCDHome()
{
    if (lcdInitialized_ == false)
        return;

    FnLCDCursor(1, 1);
}

void LCD::FnLCDDisplayCharacter(char aChar)
{
    if (lcdInitialized_ == false)
        return;

    int str_size = 5;
    char str_buf[str_size];

    memset(&str_buf, 0, sizeof(str_buf));

    snprintf(str_buf, str_size, "0x%x", aChar);
    ps_par(lcdFd_, str_buf, 1);
}

void LCD::FnLCDDisplayString(std::uint8_t row, std::uint8_t col, char* str)
{
    if (lcdInitialized_ == false)
        return;

    std::uint8_t str_size = strlen(str);

    FnLCDCursor(row, col);

    if (str_size > MAXIMUM_CHARACTER_PER_ROW)
    {
        str_size = MAXIMUM_CHARACTER_PER_ROW;
    }

    for (int i = 0; i < str_size; i++)
    {
        FnLCDDisplayCharacter(str[i]);
    }
}

void LCD::FnLCDDisplayStringCentered(std::uint8_t row, char* str)
{
    if (lcdInitialized_ == false)
        return;

    std::uint8_t str_size = strlen(str);
    if (str_size <= MAXIMUM_CHARACTER_PER_ROW)
    {
        FnLCDCursor(row, 1);
        for (int i = 0; i < 20; i ++)
        {
            FnLCDDisplayCharacter(' ');
        }
        
        std::uint8_t col_size = ((MAXIMUM_CHARACTER_PER_ROW - str_size) / 2) + 1;

        FnLCDDisplayString(row, col_size, str);
    }
    else
    {
        FnLCDDisplayString(row, 1, str);
    }
}

void LCD::FnLCDClearDisplayRow(std::uint8_t row)
{
    FnLCDCursor(row, 1);
    for (int i = 0; i < 20; i ++)
    {
        FnLCDDisplayCharacter(' ');
    }
}

void LCD::FnLCDDisplayRow(std::uint8_t row, char* str)
{
    if (lcdInitialized_ == false)
        return;

    std::uint8_t str_size = strlen(str);

    FnLCDCursorReset();
    FnLCDCursor(row, 1);

    if (str_size > MAXIMUM_CHARACTER_PER_ROW)
    {
        str_size = MAXIMUM_CHARACTER_PER_ROW;
    }

    for (int i = 0; i < str_size; i++)
    {
        FnLCDDisplayCharacter(str[i]);
    }
}

void LCD::FnLCDDisplayScreen(char* str)
{
    if (lcdInitialized_ == false)
        return;

    char sub_string_row_1[MAXIMUM_CHARACTER_PER_ROW + 1];
    char sub_string_row_2[MAXIMUM_CHARACTER_PER_ROW + 1];

    memset(&sub_string_row_1, 0, sizeof(sub_string_row_1));
    memset(&sub_string_row_2, 0, sizeof(sub_string_row_2));

    std::string text(str);
    std::size_t found = text.find("^");
    if (found != std::string::npos)
    {
        strncpy(sub_string_row_1, str, found);
        sub_string_row_1[MAXIMUM_CHARACTER_PER_ROW] = '\0';
        FnLCDDisplayStringCentered(1, sub_string_row_1);

        strncpy(sub_string_row_2, (str + found + 1), (text.length() - found - 1));
        sub_string_row_2[MAXIMUM_CHARACTER_PER_ROW] = '\0';
        FnLCDDisplayStringCentered(2, sub_string_row_2);
    }
    else
    {
        strncpy(sub_string_row_1, str, 20);
        sub_string_row_1[MAXIMUM_CHARACTER_PER_ROW] = '\0';
        FnLCDDisplayStringCentered(1, sub_string_row_1);

        //strncpy(sub_string_row_2, (str + 20), 20);
        sub_string_row_2[MAXIMUM_CHARACTER_PER_ROW] = '\0';
        FnLCDDisplayStringCentered(2, sub_string_row_2);
    }

    if (MAXIMUM_LCD_LINES == 4)
    {
        char sub_string_row_3[MAXIMUM_CHARACTER_PER_ROW + 1];
        char sub_string_row_4[MAXIMUM_CHARACTER_PER_ROW + 1];

        memset(&sub_string_row_3, 0, sizeof(sub_string_row_3));
        memset(&sub_string_row_4, 0, sizeof(sub_string_row_4));

        strncpy(sub_string_row_3, (str + 40), 20);
        sub_string_row_3[MAXIMUM_CHARACTER_PER_ROW] = '\0';
        FnLCDDisplayStringCentered(3, sub_string_row_3);

        strncpy(sub_string_row_4, (str + 60), 20);
        sub_string_row_4[MAXIMUM_CHARACTER_PER_ROW] = '\0';
        FnLCDDisplayStringCentered(4, sub_string_row_4);
    }
}

void LCD::FnLCDWipeOnLR(char* str)
{
    if (lcdInitialized_ == false)
        return;

    for (int i = 0; i < MAXIMUM_CHARACTER_PER_ROW; i++)
    {
        FnLCDCursor(1, i);
        FnLCDDisplayCharacter(str[i]);

        FnLCDCursor(2, i);
        FnLCDDisplayCharacter(str[i + 20]);

        if (MAXIMUM_LCD_LINES == 4)
        {
            FnLCDCursor(3, i);
            FnLCDDisplayCharacter(str[i + 40]);

            FnLCDCursor(4, i);
            FnLCDDisplayCharacter(str[i + 60]);
        }
    }
}

void LCD::FnLCDWipeOnRL(char* str)
{
    if (lcdInitialized_ == false)
        return;

    for (int i = MAXIMUM_CHARACTER_PER_ROW - 1; i >= 0; i--)
    {
        FnLCDCursor(1, i);
        FnLCDDisplayCharacter(str[i]);

        FnLCDCursor(2, i);
        FnLCDDisplayCharacter(str[i + 20]);

        if (MAXIMUM_LCD_LINES == 4)
        {
            FnLCDCursor(3, i);
            FnLCDDisplayCharacter(str[i + 40]);

            FnLCDCursor(4, i);
            FnLCDDisplayCharacter(str[i + 60]);
        }
    }
}

void LCD::FnLCDWipeOffLR()
{
    if (lcdInitialized_ == false)
        return;

    for (int i = 0; i < MAXIMUM_CHARACTER_PER_ROW; i++)
    {
        FnLCDCursor(1, i);
        FnLCDDisplayCharacter(BLOCK);
        FnLCDCursor(2, i);
        FnLCDDisplayCharacter(BLOCK);

        if (MAXIMUM_LCD_LINES == 4)
        {
            FnLCDCursor(3, i);
            FnLCDDisplayCharacter(BLOCK);
            FnLCDCursor(4, i);
            FnLCDDisplayCharacter(BLOCK);
        }
    }
}

void LCD::FnLCDWipeOffRL()
{
    if (lcdInitialized_ == false)
        return;

    for (int i = MAXIMUM_CHARACTER_PER_ROW - 1; i >= 0; i--)
    {
        FnLCDCursor(1, i);
        FnLCDDisplayCharacter(BLOCK);
        FnLCDCursor(2, i);
        FnLCDDisplayCharacter(BLOCK);

        if (MAXIMUM_CHARACTER_PER_ROW == 4)
        {
            FnLCDCursor(3, i);
            FnLCDDisplayCharacter(BLOCK);
            FnLCDCursor(4, i);
            FnLCDDisplayCharacter(BLOCK);
        }
    }
}

void LCD::FnLCDCursorLeft()
{
    if (lcdInitialized_ == false)
        return;

    char cmd[] = "0x10";
    ps_par(lcdFd_, cmd, 0);
}

void LCD::FnLCDCursorRight()
{
    if (lcdInitialized_ == false)
        return;

    char cmd[] = "0x14";
    ps_par(lcdFd_, cmd, 0);
}

void LCD::FnLCDCursorOn()
{
    if (lcdInitialized_ == false)
        return;

    char cmd[] = "0x0D";
    ps_par(lcdFd_, cmd, 0);
}

void LCD::FnLCDCursorOff()
{
    if (lcdInitialized_ == false)
        return;

    char cmd[] = "0x0C";
    ps_par(lcdFd_, cmd, 0);
}

void LCD::FnLCDDisplayOff()
{
    if (lcdInitialized_ == false)
        return;

    char cmd[] = "0x08";
    ps_par(lcdFd_, cmd, 0);
}

void LCD::FnLCDDisplayOn()
{
    if (lcdInitialized_ == false)
        return;

    char cmd[] = "0x0C";
    ps_par(lcdFd_, cmd, 0);
}

void LCD::FnLCDCursorReset()
{
    if (lcdInitialized_ == false)
        return;

    char cmd[] = "0x02";
    ps_par(lcdFd_, cmd, 0);
}

void LCD::FnLCDCursor(std::uint8_t row, std::uint8_t col)
{
    if (lcdInitialized_ == false)
        return;

    std::uint8_t val = 0;
    int str_size = 5;
    char str[str_size];

    memset(&str, 0, sizeof(str));

    switch (row)
    {
        case 1:
        {
            val = 0x80 + col - 1;
            break;
        }
        case 2:
        {
            val = 0xC0 + col - 1;
            break;
        }
        case 3:
        {
            val = 0x94 + col - 1;
            break;
        }
        case 4:
        {
            val = 0xD4 + col - 1;
            break;
        }
    }

    snprintf(str, str_size, "0x%x", val);
    ps_par(lcdFd_, str, 0);
}

void LCD::FnLCDDeinitDriver()
{
    if (lcdInitialized_ == false)
        return;

    if(!CH34xCloseDevice(lcdFd_))
    {
        std::stringstream ss;
        ss << "Failed to close LCD device, fd :" << std::to_string(lcdFd_) << std::endl;
        Logger::getInstance()->FnLog(ss.str());
    }
}

void LCD::FnLCDClose()
{
    if (lcdInitialized_ == false)
        return;

    FnLCDDeinitDriver();
    lcdInitialized_ = false;
}