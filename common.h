#pragma once

#include <boost/endian/conversion.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#define DATE_TIME_FORMAT_SPACE  32

class Common
{
public:
    static Common* getInstance();
    void FnLogExecutableInfo(const std::string& str);
    std::string FnGetDateTime();
    std::string FnGetDateTimeFormat_yyyymmddhhmmssfff();
    std::string FnGetDateTimeFormat_ddmmyyy_hhmmss();
    std::string FnGetDateTimeFormat_hh();
    std::string FnGetDateTimeFormat_yyyymm();
    std::string FnGetDateTimeFormat_yyyymmdd();
    std::string FnGetDateTimeFormat_yyyymmddhhmmss();
    std::string FnGetDateTimeFormat_yyyymmdd_hhmmss();
    std::string FnGetDateTimeFormat_yyyymmddhhmm();
    std::string FnGetDateTimeFormat_yymmddhhmmss();
    std::string FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
    std::string FnFormatDateTime(const std::string& timeString, const std::string& inputFormat, const std::string& outputFormat);
    std::string FnFormatDateTime(const std::tm& timeStruct, const std::string& outputFormat);
    std::string FnConvertDateTime(uint32_t epochSeconds);
    std::string FnFormatEpochTime(std::time_t epochSeconds);
    std::time_t FnGetEpochSeconds();
    std::string FnGetDateTimeSpace();
    std::string FnGetDate();
    std::string FnGetTime();
    int FnGetCurrentHour();
    int FnGetCurrentDay();
    bool validateDateTime(const std::string& dateTime);
    uint64_t FnGetSecondsSince1January0000();
    std::string FnConvertSecondsSince1January0000ToDateTime(uint64_t seconds_since_0000);
    std::chrono::system_clock::time_point FnParseDateTime(const std::string& dateTime);
    int64_t FnGetDateDiffInSeconds(const std::string& dateTime);
    int64_t FnCompareDateDiffInMinutes(const std::string& dateTime1, const std::string& dateTime2);
    uint16_t FnStringToUint16(const std::string& s); 
    std::string FnStringConvertDBTime(const std::string& s); 
    std::string FnGetFileName(const std::string& str);
    std::string FnGetLittelEndianUCharArrayToHexString(const unsigned char* array, std::size_t pos, std::size_t size);
    std::string FnGetUCharArrayToHexString(const unsigned char* array, std::size_t size);
    std::string FnGetVectorCharToHexString(const std::vector<char>& data);
    std::string FnGetVectorCharToHexString(const std::vector<uint8_t>& data, std::size_t startPos, std::size_t length);
    std::string FnGetDisplayVectorCharToHexString(const std::vector<char>& data);
    std::string FnGetDisplayVectorCharToHexString(const std::vector<uint8_t>& data);
    bool FnIsNumeric(const std::string& str);
    bool FnIsStringNumeric(const std::string& str);
    std::string FnPadRightSpace(int length, const std::string& str);
    std::string FnPadLeft0(int width, int count);
    std::string FnPadLeft0_Uint32(int width, uint32_t count);
    std::string FnPadLeftSpace(int width, const std::string& str);
    template <typename T>
    std::string FnToHexLittleEndianStringWithZeros(T value);
    template <typename T>
    T FnLittleEndianStringToUnsignedInteger(const std::string& littleEndianStr);
    std::vector<uint8_t> FnLittleEndianHexStringToVector(const std::string& hexStr);
    std::string FnConvertuint8ToString(uint8_t value);
    std::string FnConvertuint8ToHexString(uint8_t value);
    std::string FnConvertVectorUint8ToHexString(const std::vector<uint8_t>& data, bool little_endian = false);
    std::string FnConvertVectorUint8ToUpperCaseHexString(const std::vector<uint8_t>& data);
    std::string FnConvertVectorUint8ToBcdString(const std::vector<uint8_t>& data);
    std::vector<uint8_t> FnConvertAsciiToUint8Vector(const std::string& asciiStr);
    std::vector<uint8_t> FnGetDateInArrayBytes();
    std::vector<uint8_t> FnGetTimeInArrayBytes();
    std::vector<uint8_t> FnConvertUint32ToVector(uint32_t value);
    std::vector<uint8_t> FnConvertToLittleEndian(std::vector<uint8_t> bigEndianVec);
    std::vector<uint8_t> FnConvertHexStringToUint8Vector(const std::string& str);
    std::vector<uint8_t> FnExtractSubVector(const std::vector<uint8_t>& source, std::size_t offset, std::size_t length);
    uint8_t FnConvertToUint8(const std::vector<uint8_t>& vec, std::size_t offset = 0);
    uint16_t FnConvertToUint16(const std::vector<uint8_t>& vec, std::size_t offset = 0);
    uint32_t FnConvertToUint32(const std::vector<uint8_t>& vec, std::size_t offset = 0);
    uint64_t FnConvertToUint64(const std::vector<uint8_t>& vec, std::size_t offset = 0);
    std::string FnReverseByPair(std::string hexStr);
    std::string FnUint32ToString(uint32_t value);
    std::string FnBiteString(std::string& str, char c);
    std::vector<std::string> FnParseString(std::string str, char c);
    std::string FnVectorUint8ToBinaryString(const std::vector<uint8_t>& vec);
    std::string FnConvertBinaryStringToString(const std::string& data);
    std::string FnConvertStringToHexString(const std::string& data);
    uint32_t FnConvertStringToDecimal(const std::string& data);
    std::string FnConvertHexStringToString(const std::string& data);
    std::string FnConvertVectorUint8ToString(const std::vector<uint8_t>& data);
    std::string FnConvertVectorUint8ToRawString(const std::vector<uint8_t>& data);
    std::string ConvertVectorUint8ToHex(const std::vector<uint8_t>& data);
    std::string longToHex(long value);
    std::string buildTimeUTC8();
    std::vector<uint8_t> FnConvertStringToVector(const std::string& str);
    std::string SetFeeFormat(float fee);
    std::string FnFormatToFloatString(const std::string& str);
    std::string FnToUpper(const std::string& str);
    std::string FnTrim(const std::string& str);
    void FnAppendUint16LE(std::vector<uint8_t>& buffer, uint16_t value);
    void FnAppendUint32LE(std::vector<uint8_t>& buffer, uint32_t value);
    uint16_t FnReadUint16LE(const std::vector<uint8_t>& buffer, std::size_t offset);
    uint32_t FnReadUint24LE(const std::vector<uint8_t>& buffer, std::size_t offset);
    uint32_t FnReadUint24BE(const std::vector<uint8_t>& buffer, std::size_t offset);
    uint32_t FnReadUint32LE(const std::vector<uint8_t>& buffer, std::size_t offset);
    uint32_t FnReadUint32BE(const std::vector<uint8_t>& buffer, std::size_t offset);
    uint64_t FnReadUint40BE(const std::vector<uint8_t>& buffer, std::size_t offset);
    uint64_t FnReadUint40LE(const std::vector<uint8_t>& buffer, std::size_t offset);
    uint64_t FnReadUint56LE(const std::vector<uint8_t>& buffer, std::size_t offset);
    uint64_t FnReadUint56BE(const std::vector<uint8_t>& buffer, std::size_t offset);
    uint64_t FnReadUint64LE(const std::vector<uint8_t>& buffer, std::size_t offset);
    uint64_t FnReadUint64BE(const std::vector<uint8_t>& buffer, std::size_t offset);
    bool FnConvertHexStringToByteArray(const std::string& input, uint8_t* outputArray, std::size_t outputSize, bool littleEndian = false);
    bool FnDecimalStringToTwoBytes(const std::string& decimalString, uint8_t output[2], bool littleEndian = false);
    bool FnParseDateTimeString(const std::string& dateTimeStr, std::tm& outTm);
    std::string FnDecimalIntToHexString(uint64_t value, std::size_t byteSize);
    bool FnUint32ToByteString(uint32_t value, std::string& output, std::size_t outputSize, bool littleEndian = false);

    // Singleton: no copy and no move.
    Common(const Common&) = delete;
    Common& operator=(const Common&) = delete;
    Common(Common&&) = delete;
    Common& operator=(Common&&) = delete;

private:
    Common() = default;
    ~Common() = default;
};


template <typename T>
std::string Common::FnToHexLittleEndianStringWithZeros(T value)
{
    static_assert(std::is_integral<T>::value, "T must be an integral type.");

    T reversed_value = boost::endian::endian_reverse(value);

    std::ostringstream stream;
    stream << std::hex << std::setfill('0');

    if constexpr (sizeof(T) <= sizeof(uint32_t))
    {
        stream << std::setw(sizeof(T) * 2) << static_cast<uint32_t>(reversed_value);
    }
    else
    {
        stream << std::setw(sizeof(T) * 2) << static_cast<uint64_t>(reversed_value);
    }

    return stream.str();
}

template <typename T>
T Common::FnLittleEndianStringToUnsignedInteger(const std::string& littleEndianStr)
{
    static_assert(std::is_integral<T>::value, "T must be an integral type.");
    T result = 0;

    if (littleEndianStr.length() != sizeof(T) * 2)
    {
        throw std::invalid_argument("Input string length does not match the size of the target type.");
    }

    for (std::size_t i = 0; i < sizeof(T); i++)
    {
        std::string byteStr = littleEndianStr.substr(i * 2, 2);
        unsigned int byteValue;

        if (std::sscanf(byteStr.c_str(), "%2x", &byteValue) != 1)
        {
            throw std::runtime_error("Conversion from string to integer failed.");
        }
        result |= (static_cast<T>(byteValue) << (i * 8));
    }

    return result;
}