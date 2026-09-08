/*
 * data_collection_analysis.h - Central Computer Data Collection &
 * Analysis module (section 3, item 4: "stores measurement data and
 * events in a database, and builds reports broken down by different
 * criteria"). Also owns the "keep only one week of data" retention
 * rule (section 3), same idea as the LNC's own 7-day log rotation.
 *
 * This is the live consumer of Communication's on_log_ handler --
 * every MODE_CHANGE/KEEP_ALIVE/OBJECT_DETECTED/OBJECT_CLEARED frame
 * the LNC sends ends up here via on_frame(), gets decoded, and is
 * stored in one of two SQLite tables: `measurements` (sensor readings
 * + mode) and `events` (object detected/cleared, mode transitions).
 *
 * Backed by SQLiteCpp (a thin RAII C++ wrapper over SQLite's C
 * library) -- not an ORM, not a query builder: every SQL statement in
 * this class is plain, hand-written SQL, matching this project's
 * "write it yourself" convention (see log.h's file comment).
 */
#ifndef DATA_COLLECTION_ANALYSIS_H
#define DATA_COLLECTION_ANALYSIS_H

#include <SQLiteCpp/SQLiteCpp.h>
#include <mutex>
#include <string>
#include <vector>

#include "tlv.h"

/* ===============================================================
 * Wire formats for the frames this module consumes -- duplicated
 * locally from the LNC's own definitions (event.c's
 * mode_change_payload_t, keepalive.c's keepalive_payload_t). Those
 * structs live only in the firmware's .c files, not a shared header,
 * so the CC needs its own copy with matching field order/types/
 * packing to decode the raw bytes -- same convention as the LNC's own
 * log.c/event.c duplicating query_range_payload_t between themselves.
 *
 * Declared here (not privately inside data_collection_analysis.cpp)
 * so dca_test.cpp can build frames of the exact same type on_frame()
 * decodes, rather than a second, independently-typed lookalike that
 * could silently drift out of sync with the real one.
 * =============================================================== */

struct __attribute__((packed)) mode_change_payload_t {
    uint8_t old_mode;
    uint8_t new_mode;
    int16_t temp_c;
    uint8_t humidity_pct;
    uint8_t light_pct;
    uint8_t battery_pct;
};

struct __attribute__((packed)) keepalive_payload_t {
    uint8_t year; /* 0-99, offset from 2000 */
    uint8_t month;
    uint8_t date;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
    int16_t temp_c;
    uint8_t humidity_pct;
    uint8_t light_pct;
    uint8_t battery_pct;
    uint8_t mode;
};

class DataCollectionAnalysis
{
public:
    /* One row of the `measurements` table -- a sensor snapshot + the
     * mode it produced, from either a MODE_CHANGE or KEEP_ALIVE frame. */
    struct Measurement {
        std::string timestamp;
        int temp_c;
        int humidity_pct;
        int light_pct;
        int battery_pct;
        int mode;
    };

    /* One row of the `events` table -- object detected/cleared, or a
     * mode transition. */
    struct Event {
        std::string timestamp;
        std::string type;
        std::string description;
    };

    /* Opens (creating if missing) the SQLite database file at
     * db_path, and creates the measurements/events tables if they
     * don't already exist. db_path is stored (well -- handed to
     * SQLite::Database, which opens the file) so it's taken by value
     * + moved, same convention as Log's base_path. */
    explicit DataCollectionAnalysis(std::string db_path, int retention_days = 7);

    DataCollectionAnalysis(const DataCollectionAnalysis &) = delete;
    DataCollectionAnalysis &operator=(const DataCollectionAnalysis &) = delete;

    /* The frame handler to register via Communication::set_log_handler().
     * Decodes MODE_CHANGE/KEEP_ALIVE/OBJECT_DETECTED/OBJECT_CLEARED by
     * tag and stores the result; any other tag is ignored. */
    void on_frame(const tlv_frame_t &frame);

    /* Inserts one row into `measurements`. `timestamp` is only read
     * here, never stored beyond the SQL call -- const&. */
    void record_measurement(const std::string &timestamp, int temp_c, int humidity_pct,
                             int light_pct, int battery_pct, int mode);

    /* Inserts one row into `events`. */
    void record_event(const std::string &timestamp, const std::string &type,
                       const std::string &description);

    /* Returns every measurement/event row with start <= timestamp <= end
     * ("YYYY-MM-DD HH:MM:SS" strings, same format Log uses), ordered
     * oldest-first -- the two "get X for a time range" reports section
     * 3/4 need. */
    std::vector<Measurement> query_measurements(const std::string &start, const std::string &end);
    std::vector<Event> query_events(const std::string &start, const std::string &end);

    /* Deletes rows older than retention_days. Locks mutex_ itself --
     * safe to call from outside (e.g. a periodic daily cleanup once
     * the orchestration class exists). NOT used internally by the
     * constructor, which is already inside its own lock when it needs
     * this same cleanup -- see purge_old_locked() below and the .cpp. */
    void purge_old();

private:
    SQLite::Database db_;
    int retention_days_;
    std::mutex mutex_;

    /* Side effect: creates db_path's parent directory if it doesn't
     * exist yet (SQLite::Database, like std::ofstream, only creates
     * the file itself, never a missing folder). Returns db_path
     * unchanged, so this can run as part of db_'s own member-
     * initializer expression, before db_ is constructed -- see the
     * constructor in the .cpp for why it has to happen there and not
     * in the constructor body. */
    static std::string prepare_path(const std::string &db_path);

    void create_tables();

    /* The actual DELETE statements -- assumes the caller already holds
     * mutex_, does NOT lock it itself. purge_old() (public) locks then
     * calls this. The constructor also needs this same cleanup, but
     * must call THIS, never purge_old(): std::mutex isn't reentrant --
     * a thread that tries to lock a mutex it's already holding just
     * blocks forever, waiting on itself. The constructor is already
     * inside its own lock_guard(mutex_) when it runs, so calling the
     * locking purge_old() from there would deadlock the very first
     * time a DataCollectionAnalysis is ever constructed. */
    void purge_old_locked();
};

#endif /* DATA_COLLECTION_ANALYSIS_H */
