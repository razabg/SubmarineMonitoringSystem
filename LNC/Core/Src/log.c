/*
 * log.c - LNC Log module
 * See log.h for the public API and the class-level overview.
 */
#include "log.h"
#include "main.h"
#include "sdfatfs.h"
#include <stdio.h>

#define LOG_FILENAME_FMT "LOG%u.TXT"

struct Log
{
    /* Sunday-first weekday slot (1-7) of the last line written;
     * 0 (never a real slot) means nothing written yet. */
    uint8_t last_written_day;
};

static struct Log g_log;

/* ===============================================================
 * Public API: create / destroy
 * =============================================================== */

Log *log_create(void)
{
    g_log.last_written_day = 0;
    return &g_log;
}

void log_destroy(Log *l)
{
    (void)l;
}

/* ===============================================================
 * Timestamp / daily file
 * =============================================================== */

/* HAL quirk: on this family, reading the RTC's shadow time/date
 * registers only latches correctly if GetTime is immediately followed
 * by GetDate -- same as event.c's format_timestamp(). Also reads out
 * the Sunday-first weekday slot for the filename: HAL's WeekDay is
 * RTC_WEEKDAY_MONDAY(1)..RTC_WEEKDAY_SUNDAY(7), remapped here to
 * 1=Sun..7=Sat. */
static void current_timestamp_and_slot(char *ts_out, size_t ts_len, uint8_t *slot_out)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;

    HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);

    snprintf(ts_out, ts_len, "20%02u-%02u-%02u %02u:%02u:%02u",
             d.Year, d.Month, d.Date, t.Hours, t.Minutes, t.Seconds);

    *slot_out = (uint8_t)((d.WeekDay % 7u) + 1u); // Sunday-first remap since the rtc start on monday = 1 on default.
}

void log_write(const monitor_measurement_t *data, monitor_mode_t mode)
{
    char timestamp[24];
    char filename[13]; /* "LOGx.TXT" */
    char line[128];
    uint8_t slot;

    current_timestamp_and_slot(timestamp, sizeof(timestamp), &slot);
    snprintf(filename, sizeof(filename), LOG_FILENAME_FMT, (unsigned)slot);

    if (slot != g_log.last_written_day)
    {
        /* First write since a day rollover (or ever) -- start this
         * slot fresh. FR_NO_FILE (nothing there yet, e.g. the first
         * time this slot is ever used) is an expected outcome, not
         * an error, so the result is ignored. */
        (void)SDFatFS_DeleteFile(filename);
        g_log.last_written_day = slot;
    }

    snprintf(line, sizeof(line),
             "[%s] temp=%dC hum=%u%% light=%u%% batt=%u%% mode=%s\r\n",
             timestamp, data->temp_c, data->humidity_pct, data->light_pct,
             data->battery_pct, monitor_mode_name(mode));
    (void)SDFatFS_SaveString(filename, line);
}
