#pragma once

class Test
{

public:
    static Test* getInstance();
    void FnTest(char* argv);

    /**
     * Singleton Test should not be cloneable.
     */
    Test (Test& test) = delete;

    /**
     * Singleton Test should not be assignable.
     */
    void operator=(const Test&) = delete;

private:
    static Test* test_;
    Test();
    void common_test(char* argv);
    void systeminfo_test();
    void iniparser_test();
    void crc_test();
    void dio_test();
    void upt_test();
    void lcd_test_ascii();
    void lcd_test_display_two_rows();
    void lcd_test_row_string();
    void lcd_test_center_string();
    void lcd_test_whole_string();
    void lcd_test();
    void logger_test();
    void led614_test();
    void led216_test();
    void led226_test();
};