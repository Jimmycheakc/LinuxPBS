#include <chrono>
#include <ctime>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <string>
#include "common.h"
#include "log.h"

Common* Common::common_;

Common::Common()
{

}

Common* Common::getInstance()
{
    if (common_ == nullptr)
    {
        common_ = new Common();
    }
    return common_;
}

void Common::FnLogExecutableInfo(const std::string& str)
{
    std::ostringstream info;
    info << "start " << Common::getInstance()->FnGetFileName(str) << " , version: 1.1.2 build:" << __DATE__ << " " << __TIME__;
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
    oss << "." << std::setfill('0') << std::setw(3) << ms.count() << " ";
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

std::string Common::FnConvertDateTime(uint32_t seconds)
{
    time_t epochSeconds = seconds;
    struct tm timeinfo = {};
    localtime_r(&epochSeconds, &timeinfo);

    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%d/%m/%Y %H:%M:%S %p");
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
    std::stringstream ss;
    ss << std::hex << std::setfill('0');

    for (std::size_t i = pos + size - 1; i >= pos ; i--)
    {
        ss << std::setw(2) << static_cast<unsigned int>(array[i]);
    }

    return ss.str();
}

std::string Common::FnGetUCharArrayToHexString(const unsigned char* array, std::size_t size)
{
    std::stringstream ss;
    ss << std::hex << std::setfill('0');

    for (std::size_t i = 0; i < size ; i++)
    {
        ss << std::setw(2) << static_cast<unsigned int>(array[i]);
    }

    return ss.str();
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
    std::stringstream hexStream;
    hexStream << std::hex << std::setfill('0');

    for (std::size_t i = startPos; i < startPos + length && i < data.size(); ++i) {
        hexStream << std::setw(2) << static_cast<int>(data[i]);
    }

    return hexStream.str();
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

bool Common::FnIsNumeric(const std::vector<char>& data)
{
    for (char character : data)
    {
        if (!std::isdigit(static_cast<unsigned char>(character)))
        {
            return false;
        }
    }
    return true;
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