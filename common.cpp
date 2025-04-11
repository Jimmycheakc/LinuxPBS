#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <bitset>
#include <chrono>
#include <ctime>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <string>
#include "common.h"
#include "log.h"
#include "version.h"

Common* Common::common_;
std::mutex Common::mutex_;

Common::Common()
{

}

Common* Common::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (common_ == nullptr)
    {
        common_ = new Common();
    }
    return common_;
}

void Common::FnLogExecutableInfo(const std::string& str)
{
    std::ostringstream info;
    info << "start " << Common::getInstance()->FnGetFileName(str) << " , version: " << SW_VERSION << " build:" << __DATE__ << " " << __TIME__;
    Logger::getInstance()->FnLog(info.str());
}

std::string Common::FnGetDateTime()
{
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto timer = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo = {};
    localtime_r(&timer, &timeinfo);

    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%d/%m/%y %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string Common::FnGetDateTimeFormat_ddmmyyy_hhmmss()
{
    auto now = std::chrono::system_clock::now();
    auto timer = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo = {};
    localtime_r(&timer, &timeinfo);

    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%d/%m/%Y  %H:%M:%S");
    return oss.str();
}

std::string Common::FnGetDateTimeFormat_hh()
{
    auto now = std::chrono::system_clock::now();
    auto timer = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo = {};
    localtime_r(&timer, &timeinfo);

    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%H");
    return oss.str();
}

std::string Common::FnGetDateTimeFormat_yyyymm()
{
    auto now = std::chrono::system_clock::now();
    auto timer = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo = {};
    localtime_r(&timer, &timeinfo);

    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%Y%m");
    return oss.str();
}

std::string Common::FnGetDateTimeFormat_yyyymmdd()
{
    auto now = std::chrono::system_clock::now();
    auto timer = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo = {};
    localtime_r(&timer, &timeinfo);

    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%Y%m%d");
    return oss.str();
}

std::string Common::FnGetDateTimeFormat_yyyymmddhhmmss()
{
    auto now = std::chrono::system_clock::now();
    auto timer = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo = {};
    localtime_r(&timer, &timeinfo);

    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%Y%m%d%H%M%S");
    return oss.str();
}

std::string Common::FnGetDateTimeFormat_yyyymmdd_hhmmss()
{
    auto now = std::chrono::system_clock::now();
    auto timer = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo = {};
    localtime_r(&timer, &timeinfo);

    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%Y%m%d_%H%M%S");
    return oss.str();
}

std::string Common::FnGetDateTimeFormat_yyyymmddhhmm()
{
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto timer = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo = {};
    localtime_r(&timer, &timeinfo);

    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%Y%m%d%H%M");
    return oss.str();
}

std::string Common::FnGetDateTimeFormat_yymmddhhmmss()
{
    auto now = std::chrono::system_clock::now();
    auto timer = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo = {};
    localtime_r(&timer, &timeinfo);

    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%y%m%d%H%M%S");
    return oss.str();
}

std::string Common::FnConvertDateTime(uint32_t seconds)
{
    time_t epochSeconds = seconds;
    struct tm timeinfo = {};
    localtime_r(&epochSeconds, &timeinfo);

    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%d/%m/%Y %H:%M:%S %p");
    return oss.str();
}

std::string Common::FnFormatEpochTime(std::time_t epochSeconds)
{
    std::tm* tm = std::localtime(&epochSeconds);
    std::ostringstream oss;
    oss << std::put_time(tm, "%Y%m%d%H%M%S");
    return oss.str();
}

std::string Common::FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss() 
{
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto timer = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo = {};
    localtime_r(&timer, &timeinfo);

    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string Common::FnFormatDateTime(const std::string& timeString, const std::string& inputFormat, const std::string& outputFormat)
{
    try
    {
        std::tm tm = {};
        std::istringstream ss(timeString);

        // Parse the input string using the provided input format
        ss >> std::get_time(&tm, inputFormat.c_str());
        if (ss.fail())
        {
            throw std::invalid_argument("Failed to parse time: " + timeString + " with format: " + inputFormat);
        }

        // Format the std::tm into desired output format
        std::ostringstream oss;
        oss << std::put_time(&tm, outputFormat.c_str());
        return oss.str();
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return "";
    }
}

std::string Common::FnFormatDateTime(const std::tm& timeStruct, const std::string& outputFormat)
{
    std::ostringstream oss;
    oss << std::put_time(&timeStruct, outputFormat.c_str());
    return oss.str();
}

std::time_t Common::FnGetEpochSeconds()
{
    auto currentTime = std::chrono::system_clock::now();

    auto durationSinceEpoch = currentTime.time_since_epoch();

    auto secondsSinceEpoch = std::chrono::duration_cast<std::chrono::seconds>(durationSinceEpoch).count();

    return static_cast<std::time_t>(secondsSinceEpoch);
}

std::string Common::FnGetDateTimeSpace()
{
    std::ostringstream oss;
    oss << std::setfill(' ') << std::setw(DATE_TIME_FORMAT_SPACE) << "";
    return oss.str();
}

std::string Common::FnGetDate()
{
    auto now = std::chrono::system_clock::now();
    auto timer = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo = {};
    localtime_r(&timer, &timeinfo);

    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%Y%m%d");
    return oss.str();
}

std::string Common::FnGetTime()
{
    auto now = std::chrono::system_clock::now();
    auto timer = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo = {};
    localtime_r(&timer, &timeinfo);

    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%H%M%S");
    return oss.str();
}

int Common::FnGetCurrentHour()
{
    auto now = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);
    auto local_now = *std::localtime(&now_time);
    return local_now.tm_hour;
}

int Common::FnGetCurrentDay()
{
    auto now = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);
    auto local_now = *std::localtime(&now_time);
    return local_now.tm_mday; // Returns the day of the month (1-31)
}

bool Common::validateDateTime(const std::string& dateTime)
{
    if (dateTime.length() != 14)
    {
        return false;
    }

    std::string yearStr = dateTime.substr(0, 4);
    std::string monthStr = dateTime.substr(4, 2);
    std::string dayStr = dateTime.substr(6, 2);
    std::string hourStr = dateTime.substr(8, 2);
    std::string minuteStr = dateTime.substr(10, 2);
    std::string secondStr = dateTime.substr(12, 2);

    try
    {
        int year = std::stoi(yearStr);
        int month = std::stoi(monthStr);
        int day = std::stoi(dayStr);
        int hour = std::stoi(hourStr);
        int minute = std::stoi(minuteStr);
        int second = std::stoi(secondStr);

        boost::gregorian::date date(year, month, day);
        if (date.is_not_a_date())
        {
            return false;
        }

        if (hour < 0 || hour >= 24 || minute < 0 || minute >= 60 || second < 0 || second >= 60)
        {
            return false;
        }

        return true;
    }
    catch (...)
    {
        return false;
    }
}

uint64_t Common::FnGetSecondsSince1January0000()
{
    auto now = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);

    // Calculate the number of seconds from epoch to now
    uint64_t seconds_since_epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    // Calculate the number of days between January 1, 0000 and January 1, 1970
    const uint64_t days_between_0000_and_1970 = 719527;

    // Calculate the number of seconds between January 1, 0000 and January 1, 1970
    const uint64_t seconds_between_0000_and_1970 = days_between_0000_and_1970 * 24 * 3600;

    // Calculate the total number of seconds from January 1, 0000 to now
    uint64_t seconds_since_0000 = seconds_since_epoch + seconds_between_0000_and_1970;

    return seconds_since_0000;
}

std::string Common::FnConvertSecondsSince1January0000ToDateTime(uint64_t seconds_since_0000)
{
    try
    {
        // Define the epoch (January 1, 0000)
        const uint64_t days_between_0000_and_1970 = 719527;
        const uint64_t seconds_between_0000_and_1970 = days_between_0000_and_1970 * 24 * 3600;

        // Check for underflow
        if (seconds_since_0000 < seconds_between_0000_and_1970)
        {
            throw std::invalid_argument("Input seconds_since_0000 is invalid (too small).");
        }

        // Adjust seconds_since_0000 to seconds since epoch (1970)
        uint64_t seconds_since_epoch = seconds_since_0000 - seconds_between_0000_and_1970;
        std::chrono::system_clock::time_point date_time = std::chrono::system_clock::time_point(std::chrono::seconds(seconds_since_epoch));

        // Convert to time_t for easier extraction of date and time components
        std::time_t tt = std::chrono::system_clock::to_time_t(date_time);
        std::tm* gmt = std::gmtime(&tt);

        // Check for nullptr from gmtime
        if (gmt == nullptr)
        {
            throw std::runtime_error("Failed to convert time_t to tm structure.");
        }

        // Format the date-time string
        char buf[80];
        strftime(buf, sizeof(buf), "%d %b %Y   %H:%M:%S", gmt);

        return std::string(buf);
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return "";
    }
}

std::chrono::system_clock::time_point Common::FnParseDateTime(const std::string& dateTime)
{
    try
    {
        std::tm tm = {};
        std::istringstream ss(dateTime);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");

        if (ss.fail())
        {
            throw std::runtime_error("Failed to parse datetime string.");
        }

        std::time_t tt = std::mktime(&tm);
        if (tt == -1)
        {
            throw std::runtime_error("Invalid datetime components in: " + dateTime);
        }

        return std::chrono::system_clock::from_time_t(tt);
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        // Return epoch (1970-01-01) as a fallback
        return std::chrono::system_clock::time_point{};
    }
}

int64_t Common::FnGetDateDiffInSeconds(const std::string& dateTime)
{
    auto parsedTime = FnParseDateTime(dateTime);
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - parsedTime);
    return duration.count();
}

int64_t Common::FnCompareDateDiffInMinutes(const std::string& dateTime1, const std::string& dateTime2)
{
    auto parsedTime1 = FnParseDateTime(dateTime1);
    auto parsedTime2 = FnParseDateTime(dateTime2);
    auto duration = std::chrono::duration_cast<std::chrono::minutes>(parsedTime2 - parsedTime1);
    return duration.count();
}

std::string Common::FnGetFileName(const std::string& str)
{
    size_t found = str.find_last_of("/");
    if (found != std::string::npos)
    {
        return str.substr(found + 1);
    }
    return str;
}

std::string Common::FnGetLittelEndianUCharArrayToHexString(const unsigned char* array, std::size_t pos, std::size_t size)
{
    try
    {
        // Validate inputs (throw on critical errors)
        if (array == nullptr)
        {
            throw std::invalid_argument("Null pointer passed to array");
        }
        if (size == 0)
        {
            throw std::invalid_argument("Size cannot be zero");
        }

        std::stringstream ss;
        ss << std::hex << std::setfill('0');

        for (std::size_t i = pos + size - 1; i >= pos ; i--)
        {
            ss << std::setw(2) << static_cast<unsigned int>(array[i]);
        }

        return ss.str();
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return "";
    }
}

std::string Common::FnGetUCharArrayToHexString(const unsigned char* array, std::size_t size)
{
    try
    {
        // Validate inputs (throw on critical errors)
        if (array == nullptr)
        {
            throw std::invalid_argument("Null pointer passed to array");
        }
        if (size == 0)
        {
            throw std::invalid_argument("Size cannot be zero");
        }

        std::stringstream ss;
        ss << std::hex << std::setfill('0');

        for (std::size_t i = 0; i < size ; i++)
        {
            ss << std::setw(2) << static_cast<unsigned int>(array[i]);
        }

        return ss.str();
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return "";
    }
}

std::string Common::FnGetVectorCharToHexString(const std::vector<char>& data)
{
    std::stringstream hexStream;

    for (const auto& character : data)
    {
        hexStream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(character);
    }

    return hexStream.str();
}

std::string Common::FnGetVectorCharToHexString(const std::vector<uint8_t>& data, std::size_t startPos, std::size_t length)
{
    try
    {
        // Validate inputs
        if (startPos >= data.size())
        {
            throw std::out_of_range("startPos exceeds vector size");
        }

        if (length == 0)
        {
            return "";  // Empty result for zero length
        }

        if (startPos + length > data.size())
        {
            throw std::out_of_range("startPos + length exceeds vector size");
        }

        std::stringstream hexStream;
        hexStream << std::hex << std::setfill('0');

        for (std::size_t i = startPos; i < startPos + length && i < data.size(); ++i) {
            hexStream << std::setw(2) << static_cast<int>(data[i]);
        }

        return hexStream.str();
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return "";
    }
}

std::string Common::FnGetDisplayVectorCharToHexString(const std::vector<char>& data)
{
    std::stringstream hexStream;

    for (const auto& character : data)
    {
        hexStream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(character) << ' ';
    }

    return hexStream.str();
}

std::string Common::FnGetDisplayVectorCharToHexString(const std::vector<uint8_t>& data)
{
    std::stringstream hexStream;

    for (const auto& character : data)
    {
        hexStream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(character) << ' ';
    }

    return hexStream.str();
}

bool Common::FnIsNumeric(const std::string& str)
{
    if (str.empty())
    {
        return false;
    }

    try
    {
        std::stod(str);
        return true;
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return false;
    }
}

bool Common::FnIsStringNumeric(const std::string& str)
{
    for (char character :str)
    {
        if (!std::isdigit(character))
        {
            return false;
        }
    }
    return true;
}

std::string Common::FnPadRightSpace(int length, const std::string& str)
{
    if (str.length() >= length) {
        return str; // No padding needed if string is already longer or equal to desired length
    }
    else
    {
        std::ostringstream oss;
        oss << str;
        oss << std::string(length - str.length(), ' '); // Append spaces to reach desired length
        return oss.str();
    }
}

std::string Common::FnPadLeft0(int width, int count)
{
    std::stringstream ss;
    ss << std::setw(width) << std::setfill('0') << count;
    return ss.str();
}

std::string Common::FnPadLeft0_Uint32(int width, uint32_t count)
{
    std::stringstream ss;
    ss << std::setw(width) << std::setfill('0') << count;
    return ss.str();
}

std::string Common::FnPadLeftSpace(int width, const std::string& str)
{
    if (str.length() > static_cast<std::size_t>(width))
    {
        return str;
    }

    return std::string(width - str.length(), ' ') + str;
}

std::vector<uint8_t> Common::FnLittleEndianHexStringToVector(const std::string& hexStr)
{
    std::vector<uint8_t> result;

    try
    {
        // 1. Input validation
        if (hexStr.empty())
        {
            return result;  // Return empty vector for empty input
        }

        if (hexStr.length() % 2 != 0)
        {
            throw std::invalid_argument("Hex string must have even number of characters");
        }

        for (int i = static_cast<int>(hexStr.length()) - 2; i >= 0; i -= 2)
        {
            std::string byteStr = hexStr.substr(i, 2);
            uint8_t byte = static_cast<uint8_t>(stoul(byteStr, nullptr, 16));
            result.push_back(byte);
        }

        return result;
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return result;
    }
}

std::string Common::FnConvertuint8ToString(uint8_t value)
{
    return std::string(1, static_cast<char>(value));
}

std::string Common::FnConvertuint8ToHexString(uint8_t value)
{
    std::ostringstream oss;
    oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(value);
    return oss.str();
}

std::string Common::FnConvertVectorUint8ToHexString(const std::vector<uint8_t>& data, bool little_endian)
{
    std::vector<uint8_t> tmpData = data;

    if (little_endian)
    {
        std::reverse(tmpData.begin(), tmpData.end());
    }

    std::ostringstream oss;

    for (const auto& value : tmpData)
    {
        oss << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(value);
    }

    return oss.str();
}

std::string Common::FnConvertVectorUint8ToBcdString(const std::vector<uint8_t>& data)
{
    std::ostringstream oss;

    for (std::size_t i = 0; i < data.size(); i++)
    {
        uint8_t byte = data[i];
        uint8_t upperDigit = (byte >> 4) & 0x0F;
        uint8_t lowerDigit = byte & 0x0F;

        oss << static_cast<int>(upperDigit) << static_cast<int>(lowerDigit);
    }

    return oss.str();
}

std::vector<uint8_t> Common::FnConvertAsciiToUint8Vector(const std::string& asciiStr)
{
    std::vector<uint8_t> uint8Vec(asciiStr.begin(), asciiStr.end());
    return uint8Vec;
}

std::vector<uint8_t> Common::FnGetDateInArrayBytes()
{
    std::string hexAscii = FnGetDate();
    return FnConvertAsciiToUint8Vector(hexAscii);
}

std::vector<uint8_t> Common::FnGetTimeInArrayBytes()
{
    std::string hexAscii = FnGetTime();
    return FnConvertAsciiToUint8Vector(hexAscii);
}

std::vector<uint8_t> Common::FnConvertUint32ToVector(uint32_t value)
{
    std::vector<uint8_t> bytes(4);
    bytes[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    bytes[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    bytes[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    bytes[3] = static_cast<uint8_t>(value & 0xFF);

    return bytes;
}

std::vector<uint8_t> Common::FnConvertToLittleEndian(std::vector<uint8_t> bigEndianVec)
{
    std::reverse(bigEndianVec.begin(), bigEndianVec.end());
    return bigEndianVec;
}

std::vector<uint8_t> Common::FnConvertHexStringToUint8Vector(const std::string& hexStr)
{
    std::vector<uint8_t> result;

    try
    {
        if (hexStr.empty())
        {
            return result;  // Return empty vector for empty input
        }

        if (hexStr.length() % 2 != 0)
        {
            throw std::invalid_argument("Hex string must have an even length");
        }

        for (std::size_t i = 0; i < hexStr.length(); i+=2)
        {
            // Extract a substring of 2 characters
            std::string byteString = hexStr.substr(i, 2);
            // Convert the 2-character string to a uint8_t
            uint8_t byte = static_cast<uint8_t>(std::stoi(byteString, nullptr, 16));
            // Add the byte to the result vector
            result.push_back(byte);
        }

        return result;
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return result;
    }
}

std::vector<uint8_t> Common::FnExtractSubVector(const std::vector<uint8_t>& source, std::size_t offset, std::size_t length)
{
    if (offset >= source.size())
    {
        return {};
    }

    // Calculate the end position
    auto end = std::min(source.begin() + offset + length, source.end());

    // Create and return the subvector
    return std::vector<uint8_t>(source.begin() + offset, end);
}

uint8_t Common::FnConvertToUint8(const std::vector<uint8_t>& vec, std::size_t offset)
{
    try
    {
        if (offset + sizeof(uint8_t) > vec.size())
        {
            throw std::out_of_range("Offset out of range");
        }
        return vec[offset];
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return 0;
    }
}

uint16_t Common::FnConvertToUint16(const std::vector<uint8_t>& vec, std::size_t offset)
{
    try
    {
        if (offset + sizeof(uint16_t) > vec.size())
        {
            throw std::out_of_range("Offset out of range");
        }
        uint16_t value;
        std::memcpy(&value, vec.data() + offset, sizeof(uint16_t));
        return value;
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return 0;
    }
}

uint32_t Common::FnConvertToUint32(const std::vector<uint8_t>& vec, std::size_t offset)
{
    try
    {
        if (offset + sizeof(uint32_t) > vec.size())
        {
            throw std::out_of_range("Offset out of range");
        }

        uint32_t value;
        std::memcpy(&value, vec.data() + offset, sizeof(uint32_t));
        return value;
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return 0;
    }
}

uint64_t Common::FnConvertToUint64(const std::vector<uint8_t>& vec, std::size_t offset)
{
    try
    {
        if (offset + sizeof(uint64_t) > vec.size())
        {
            throw std::out_of_range("Offset out of range");
        }

        uint64_t value;
        std::memcpy(&value, vec.data() + offset, sizeof(uint64_t));
        return value;
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return 0;
    }
}

std::string Common::FnReverseByPair(std::string hexStr)
{
    try
    {
        if (hexStr.empty())
        {
            return "";  // Return empty vector for empty input
        }
    
        if (hexStr.length() % 2 != 0)
        {
            throw std::invalid_argument("Hex string must be even.");
        }

        std::string reverseHexStr;
        reverseHexStr.reserve(hexStr.length());

        for (auto it = hexStr.rbegin(); it != hexStr.rend(); it += 2)
        {
            reverseHexStr.push_back(*(it + 1));
            reverseHexStr.push_back(*it);
        }

        return reverseHexStr;
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return "";
    }
}

std::string Common::FnUint32ToString(uint32_t value)
{
    std::ostringstream oss;

    oss << std::setw(8) << std::setfill('0') << value;

    return oss.str();
}

std::string Common::FnBiteString(std::string& str, char c)
{
    std::size_t pos = str.find(c);

    std::string return_str;

    if (pos == std::string::npos)
    {
        return_str = str;
        str = "";
    }
    else
    {
        return_str = str.substr(0, pos);
        str = str.substr(pos + 1);
    }

    return return_str;
}

std::vector<std::string> Common::FnParseString(std::string str, char c)
{
    std::vector<std::string> return_vector;

    while (str != "")
    {
        return_vector.push_back(FnBiteString(str, c));
    }

    return return_vector;
}

std::string Common::FnVectorUint8ToBinaryString(const std::vector<uint8_t>& vec)
{
    std::string binaryString;

    for (const auto& byte : vec)
    {
        binaryString += std::bitset<8>(byte).to_string();
    }

    return binaryString;
}

std::string Common::FnConvertBinaryStringToString(const std::string& data)
{
    std::string sRet;

    try
    {
        // 1. Check for empty input
        if (data.empty())
        {
            return sRet;
        }

        // 2. Check if length is a multiple of 8 (optional)
        if (data.length() % 8 != 0)
        {
            throw std::invalid_argument("Binary string length must be a multiple of 8");
        }

        // Iterate over the binary string in chunks of 8
        for (std::size_t i = 0; i < data.length(); i += 8)
        {
            std::string byteStr = data.substr(i, 8);

            // Convert the binary string (8 bits) to an unsigned long and then to a character
            char character = static_cast<char>(std::bitset<8>(byteStr).to_ulong());

            // Append the character to the result string
            sRet += character;
        }

        return sRet;
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return sRet;
    }
}

std::string Common::FnConvertStringToHexString(const std::string& data)
{
    std::stringstream ss;
    ss << std::hex << std::setfill('0');

    for (char c : data)
    {
        ss << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
    }

    return ss.str();
}

uint32_t Common::FnConvertStringToDecimal(const std::string& data)
{
    uint32_t result = 0;

    for (char c : data)
    {
        result = (result << 8) | static_cast<uint8_t>(c);
    }

    return result;
}

std::string Common::FnConvertHexStringToString(const std::string& data)
{
    std::string result;

    try
    {
        // 1. Input validation
        if (data.empty())
        {
            return result;  // Return empty string for empty input
        }

        if (data.length() % 2 != 0)
        {
            throw std::invalid_argument("Hex string must have even length");
        }

        for (std::size_t i = 0; i < data.length(); i += 2)
        {
            // Extract two characters (one byte) from hex string
            std::string byteString = data.substr(i, 2);

            // Convert the hex byte to an integer
            char byte = static_cast<char>(std::stoi(byteString, nullptr, 16));

            // Append the corresponding character to the result string
            result += byte;
        }

        return result;
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return result;
    }
}

std::string Common::SetFeeFormat(float fee)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << fee;
    std::string gsFee = oss.str();
    return gsFee;
}

std::string Common::FnConvertVectorUint8ToString(const std::vector<uint8_t>& data)
{
    return std::string(data.begin(), data.end());
}

std::vector<uint8_t> Common::FnConvertStringToVector(const std::string& str)
{
    return std::vector<uint8_t>(str.begin(), str.end());
}

std::string Common::FnFormatToFloatString(const std::string& str)
{
    try
    {
        float value = std::stof(str) / 100.0;

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << value;
        return oss.str();
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return "Invalid Input";
    }
}

std::string Common::FnToUpper(const std::string& str)
{
    std::string upperStr = str;
    std::transform(upperStr.begin(), upperStr.end(), upperStr.begin(), ::toupper);
    return upperStr;
}

std::string Common::FnTrim(const std::string& str)
{
    auto start = std::find_if(str.begin(), str.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    });

    auto end = std::find_if(str.rbegin(), str.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base();

    return (start < end) ? std::string(start, end) : "";
}
