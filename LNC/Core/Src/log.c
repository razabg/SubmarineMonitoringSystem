/*
 * log.c - LNC Log module
 * See log.h for the public API and the class-level overview.
 */
#include "log.h"
#include "main.h"
#include "sdfatfs.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define LOG_FILENAME_FMT "LOG%u.TXT"

struct Log
{
    Communication *comm;
    /* Sunday-first weekday slot (1-7) of the last line written;
     * 0 (never a real slot) means nothing written yet. */
    uint8_t last_written_day;
};

static struct Log g_log;

/* ===============================================================
 * Public API: create / destroy
 * =============================================================== */

Log *log_create(Communication *comm)
{
    g_log.comm = comm;
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

/* ===============================================================
 * Dispatch: Communication -> Log (TLV_TAG_QUERY_DATA, section 2.5)
 *
 * Provisional wire format -- same "nothing on the CC side parses this
 * yet" status as event.c's mode_change_payload_t / init.c's
 * time_payload_t. Timestamp fields match their layout/units (year is
 * an offset from 2000).
 * =============================================================== */

typedef struct __attribute__((packed)) {
    uint8_t start_year, start_month, start_date, start_hour, start_min, start_sec;
    uint8_t end_year, end_month, end_date, end_hour, end_min, end_sec;
} query_range_payload_t;

/* Packs a timestamp into one monotonically-comparable integer, so
 * "is this line's timestamp within [start, end]" is a plain integer
 * comparison instead of six separate field comparisons. year/month/
 * date/hour/min/sec are all <=99, so each fits in two decimal digits
 * and the result stays well within uint64_t. */
static uint64_t timestamp_key(uint8_t year, uint8_t month, uint8_t date,
                               uint8_t hour, uint8_t min, uint8_t sec)
{
    uint64_t k = year;
    k = k * 100u + month;
    k = k * 100u + date;
    k = k * 100u + hour;
    k = k * 100u + min;
    k = k * 100u + sec;
    return k;
}

/* Parses the "[2026-09-06 14:30:00]" prefix log_write() itself
 * generates (via current_timestamp_and_slot()) back into its six
 * fields. Returns true only if all six were found -- a malformed or
 * unexpectedly-short line (e.g. a partial write from a power loss
 * mid-write) is treated as unmatchable rather than guessed at. */
static bool parse_line_timestamp(const char *line, uint64_t *out_key)
{
    unsigned year, month, date, hour, min, sec;

    if (sscanf(line, "[20%2u-%2u-%2u %2u:%2u:%2u]",
               &year, &month, &date, &hour, &min, &sec) != 6) {
        return false;
    }
    *out_key = timestamp_key((uint8_t)year, (uint8_t)month, (uint8_t)date,
                              (uint8_t)hour, (uint8_t)min, (uint8_t)sec);
    return true;
}

typedef struct {
    uint64_t start_key;
    uint64_t end_key;
    Communication *comm;
} query_data_ctx_t;

/* SDFatFS_ForEachLine() callback -- forwards the whole line as-is
 * (already a complete, self-describing "[timestamp] temp=... mode=..."
 * string) as one TLV_TAG_QUERY_RECORD, rather than re-encoding it into
 * a separate binary struct: the stored data is already text, so
 * re-parsing it into a different wire format would be pure extra
 * complexity for no benefit. */
static void query_data_line_cb(const char *line, void *ctx)
{
    query_data_ctx_t *c = (query_data_ctx_t *)ctx;
    uint64_t key;

    if (!parse_line_timestamp(line, &key)) {
        return;
    }
    if (key < c->start_key || key > c->end_key) {
        return;
    }
    (void)comm_send(c->comm, TLV_TAG_QUERY_RECORD,
                     (const uint8_t *)line, (uint8_t)strlen(line));
}

void log_on_frame(const tlv_frame_t *f)
{
    query_data_ctx_t ctx;
    const query_range_payload_t *p;

    if (f == NULL || f->value == NULL || f->len != sizeof(query_range_payload_t)) {
        return;
    }
    p = (const query_range_payload_t *)f->value;

    ctx.comm = g_log.comm;
    ctx.start_key = timestamp_key(p->start_year, p->start_month, p->start_date,
                                   p->start_hour, p->start_min, p->start_sec);
    ctx.end_key = timestamp_key(p->end_year, p->end_month, p->end_date,
                                 p->end_hour, p->end_min, p->end_sec);

    /* LOG*.TXT slots are keyed by weekday, not calendar date, and get
     * overwritten every 7 days (see log.h/CLAUDE.md) -- there's no
     * reliable way to know which calendar dates a given slot currently
     * holds without just reading it. So every slot that currently
     * exists gets searched unconditionally; FR_NO_FILE (a slot never
     * written yet) is an expected outcome, not an error. */
    for (unsigned slot = 1; slot <= 7; slot++) {
        char filename[13];
        snprintf(filename, sizeof(filename), LOG_FILENAME_FMT, slot);
        (void)SDFatFS_ForEachLine(filename, query_data_line_cb, &ctx);
    }

    (void)comm_send(g_log.comm, TLV_TAG_QUERY_END, NULL, 0);
}
