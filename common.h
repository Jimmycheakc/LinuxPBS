#pragma once

#include <iostream>
#include <memory>
#include <string>


#define DATE_TIME_FORMAT_SPACE  32

class Common
{
public:
    static Common* getInstance();
    void FnLogExecutableInfo(const std::string& str);
    std::string FnGetDateTime();
    std::string FnGetDateTimeSpace();
    std::string FnGetFileName(const std::string& str);
    std::string FnGetLittelEndianUCharArrayToHexString(const unsigned char* array, std::size_t pos, std::size_t size);
    std::string FnGetUCharArrayToHexString(const unsigned char* array, std::size_t size);

    /**
     * Singleton Common should not be cloneable.
     */
    Common(Common &common) = delete;

    /**
     * Singleton Common should not be assignable.
     */
    void operator=(const Common&) = delete;

private:
    static Common* common_;
    Common();
};