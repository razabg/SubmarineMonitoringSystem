/*
 * time_utils.h - shared timestamp formatting for the Central Computer.
 *
 * Both Log (operational log lines) and DataCollectionAnalysis
 * (measurement/event rows) need the same "YYYY-MM-DD HH:MM:SS"
 * timestamp shape, and Log's rotation/purge and
 * DataCollectionAnalysis's purge both need the same "N days ago, date
 * only" cutoff calculation. Pulled out here instead of duplicated in
 * both, since -- unlike the per-module wire-format payload structs
 * (deliberately duplicated, see data_collection_analysis.cpp) -- this
 * is genuinely the same calculation with no reason to drift.
 */
#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include <cstdint>
#include <string>

namespace TimeUtils
{
/* Current local wall-clock time, "YYYY-MM-DD HH:MM:SS". */
std::string now();

/* Formats explicit calendar fields into the same "YYYY-MM-DD HH:MM:SS"
 * shape -- for a timestamp that came from somewhere else (e.g. the
 * LNC's own RTC, embedded in a KEEP_ALIVE payload) rather than this
 * machine's clock. `year` is 0-99, an offset from 2000 (matches the
 * LNC's own payload convention); `month` is 1-12; `date` is 1-31. */
std::string format(uint8_t year, uint8_t month, uint8_t date, uint8_t hour, uint8_t min,
                    uint8_t sec);

/* "now minus days_ago days", date only ("YYYY-MM-DD", no time-of-day)
 * -- the retention cutoff shape both Log's file rotation and
 * DataCollectionAnalysis's row purge need. days_ago=0 means "today". */
std::string days_ago_date(int days_ago);
} // namespace TimeUtils

#endif /* TIME_UTILS_H */
