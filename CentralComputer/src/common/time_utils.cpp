/*
 * time_utils.cpp - see time_utils.h for the overview.
 */
#include "time_utils.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace TimeUtils
{

std::string now()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};
    localtime_r(&t, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string format(uint8_t year, uint8_t month, uint8_t date, uint8_t hour, uint8_t min,
                    uint8_t sec)
{
    std::tm tm{};
    tm.tm_year = year + 2000 - 1900; /* struct tm wants "years since 1900" */
    tm.tm_mon = month - 1;           /* struct tm wants 0-indexed months */
    tm.tm_mday = date;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string days_ago_date(int days_ago)
{
    auto tp = std::chrono::system_clock::now() - std::chrono::hours(24 * days_ago);
    std::time_t t = std::chrono::system_clock::to_time_t(tp);

    std::tm tm{};
    localtime_r(&t, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d");
    return oss.str();
}

} // namespace TimeUtils
