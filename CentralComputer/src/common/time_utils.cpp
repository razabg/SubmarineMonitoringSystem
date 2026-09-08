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

CalendarFields now_fields()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};
    localtime_r(&t, &tm);

    CalendarFields f{};
    f.year = static_cast<uint8_t>(tm.tm_year + 1900 - 2000); /* tm: since 1900; wire: offset from 2000 */
    f.month = static_cast<uint8_t>(tm.tm_mon + 1);           /* tm: 0-indexed; wire: 1-12 */
    f.date = static_cast<uint8_t>(tm.tm_mday);
    f.hour = static_cast<uint8_t>(tm.tm_hour);
    f.min = static_cast<uint8_t>(tm.tm_min);
    f.sec = static_cast<uint8_t>(tm.tm_sec);

    /* tm_wday: 0=Sunday..6=Saturday. Wire/HAL: 1=Monday..7=Sunday.
     * Every day except Sunday maps to itself; Sunday (0) becomes 7. */
    f.dow = (tm.tm_wday == 0) ? 7 : static_cast<uint8_t>(tm.tm_wday);

    return f;
}

} // namespace TimeUtils
