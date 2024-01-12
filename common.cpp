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

std::string Common::FnGetDateTimeSpace()
{
    std::ostringstream oss;
    oss << std::setfill(' ') << std::setw(DATE_TIME_FORMAT_SPACE) << "";
    return oss.str();
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

bool Common::FnIsNumeric(const std::vector<char>& data)
{
    for (char character : data)
    {
        if (!std::isdigit(character))
        {
            return false;
        }
    }
    return true;
}