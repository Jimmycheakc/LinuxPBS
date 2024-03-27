#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <vector>


#define DATE_TIME_FORMAT_SPACE  32

class Common
{
public:
    static Common* getInstance();
    void FnLogExecutableInfo(const std::string& str);
    std::string FnGetDateTime();
    std::string FnGetDateTimeFormat_yyyymm();
    std::string FnGetDateTimeFormat_yyyymmddhhmmss();
    std::string FnGetDateTimeFormat_yyyymmdd_hhmmss();
    std::string FnGetDateTimeFormat_yyyymmddhhmm();
    std::string FeGetDateTimeFormat_VehicleTrans();
    std::string FnConvertDateTime(uint32_t epochSeconds);
    std::time_t FnGetEpochSeconds();
    std::string FnGetDateTimeSpace();
    int FnGetCurrentHour();
    int FnGetCurrentDay();
    std::string FnGetFileName(const std::string& str);
    std::string FnGetLittelEndianUCharArrayToHexString(const unsigned char* array, std::size_t pos, std::size_t size);
    std::string FnGetUCharArrayToHexString(const unsigned char* array, std::size_t size);
    std::string FnGetVectorCharToHexString(const std::vector<char>& data);
    std::string FnGetVectorCharToHexString(const std::vector<uint8_t>& data, std::size_t startPos, std::size_t length);
    std::string FnGetDisplayVectorCharToHexString(const std::vector<char>& data);
    std::string FnGetDisplayVectorCharToHexString(const std::vector<uint8_t>& data);
    bool FnIsNumeric(const std::vector<char>& data);
    bool FnIsStringNumeric(const std::string& str);
    std::string FnPadRightSpace(int length, const std::string& str);
    std::string FnPadLeft0(int width, int count);

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