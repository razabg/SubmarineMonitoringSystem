/*
 * data_collection_analysis.cpp - Central Computer Data Collection &
 * Analysis module.
 * See data_collection_analysis.h for the class-level overview.
 */
#include "data_collection_analysis.h"

#include <cstdio>
#include <filesystem>

#include "time_utils.h"

namespace fs = std::filesystem;

/* "Normal"/"Warning"/"Error" -- matches monitor_mode_t's 0/1/2 on the
 * LNC side (monitor.h). No LNC function to call here (that's C code
 * compiled only into the firmware), so this is its own small copy,
 * just for building a readable event description. */
static const char *mode_name(int mode)
{
    switch (mode) {
    case 0:
        return "Normal";
    case 1:
        return "Warning";
    case 2:
        return "Error";
    default:
        return "Unknown";
    }
}

std::string DataCollectionAnalysis::prepare_path(const std::string &db_path)
{
    fs::path p(db_path);
    if (p.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
    }
    return db_path;
}

/* db_ is constructed here in the member-initializer list, before the
 * constructor body runs -- by the time "std::lock_guard lock(mutex_);"
 * below executes, SQLite::Database has already tried to open the
 * file. So the missing-directory fix has to happen as part of this
 * expression, not as a statement in the body (which would run too
 * late). prepare_path(db_path) does the mkdir-equivalent side effect
 * and hands db_path straight through unchanged. */
DataCollectionAnalysis::DataCollectionAnalysis(std::string db_path, int retention_days)
    : db_(prepare_path(db_path), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE),
      retention_days_(retention_days)
{
    std::lock_guard<std::mutex> lock(mutex_);
    create_tables();
    purge_old_locked(); /* NOT purge_old() -- this lock is already held; see purge_old_locked()'s declaration */
}

void DataCollectionAnalysis::create_tables()
{
    db_.exec(
        "CREATE TABLE IF NOT EXISTS measurements ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "timestamp TEXT NOT NULL,"
        "temp_c INTEGER NOT NULL,"
        "humidity_pct INTEGER NOT NULL,"
        "light_pct INTEGER NOT NULL,"
        "battery_pct INTEGER NOT NULL,"
        "mode INTEGER NOT NULL)");

    db_.exec(
        "CREATE TABLE IF NOT EXISTS events ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "timestamp TEXT NOT NULL,"
        "type TEXT NOT NULL,"
        "description TEXT)");
}

void DataCollectionAnalysis::record_measurement(const std::string &timestamp, int temp_c,
                                                  int humidity_pct, int light_pct,
                                                  int battery_pct, int mode)
{
    std::lock_guard<std::mutex> lock(mutex_);

    SQLite::Statement stmt(db_,
        "INSERT INTO measurements (timestamp, temp_c, humidity_pct, light_pct, battery_pct, mode) "
        "VALUES (?, ?, ?, ?, ?, ?)");
    stmt.bind(1, timestamp);
    stmt.bind(2, temp_c);
    stmt.bind(3, humidity_pct);
    stmt.bind(4, light_pct);
    stmt.bind(5, battery_pct);
    stmt.bind(6, mode);
    stmt.exec();
}

void DataCollectionAnalysis::record_event(const std::string &timestamp, const std::string &type,
                                            const std::string &description)
{
    std::lock_guard<std::mutex> lock(mutex_);

    SQLite::Statement stmt(db_, "INSERT INTO events (timestamp, type, description) VALUES (?, ?, ?)");
    stmt.bind(1, timestamp);
    stmt.bind(2, type);
    stmt.bind(3, description);
    stmt.exec();
}

void DataCollectionAnalysis::on_frame(const tlv_frame_t &frame)
{
    switch (frame.tag) {
    case TLV_TAG_MODE_CHANGE: {
        if (frame.value == nullptr || frame.len != sizeof(mode_change_payload_t)) {
            return; /* wrong shape -- a mismatched build on one end of the wire, or noise */
        }
        const auto *p = reinterpret_cast<const mode_change_payload_t *>(frame.value);

        /* This payload carries no timestamp of its own (unlike
         * KEEP_ALIVE below) -- stamp it with the CC's own clock, on
         * arrival, same principle as the LNC's own Event module. */
        std::string ts = TimeUtils::now();

        record_measurement(ts, p->temp_c, p->humidity_pct, p->light_pct, p->battery_pct,
                            p->new_mode);

        char desc[64];
        std::snprintf(desc, sizeof(desc), "mode %s -> %s", mode_name(p->old_mode),
                      mode_name(p->new_mode));
        record_event(ts, "mode_change", desc);
        break;
    }

    case TLV_TAG_KEEP_ALIVE: {
        if (frame.value == nullptr || frame.len != sizeof(keepalive_payload_t)) {
            return;
        }
        const auto *p = reinterpret_cast<const keepalive_payload_t *>(frame.value);

        /* This one's timestamp came from the LNC's own RTC -- use it
         * directly rather than CC's arrival time. */
        std::string ts = TimeUtils::format(p->year, p->month, p->date, p->hour, p->min, p->sec);

        record_measurement(ts, p->temp_c, p->humidity_pct, p->light_pct, p->battery_pct, p->mode);
        break;
    }

    case TLV_TAG_OBJECT_DETECTED:
        record_event(TimeUtils::now(), "object_detected", "");
        break;

    case TLV_TAG_OBJECT_CLEARED:
        record_event(TimeUtils::now(), "object_cleared", "");
        break;

    default:
        break; /* not one of ours -- Communication routes other tags to Management Command */
    }
}

std::vector<DataCollectionAnalysis::Measurement>
DataCollectionAnalysis::query_measurements(const std::string &start, const std::string &end)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Measurement> result;

    SQLite::Statement stmt(db_,
        "SELECT timestamp, temp_c, humidity_pct, light_pct, battery_pct, mode "
        "FROM measurements WHERE timestamp >= ? AND timestamp <= ? ORDER BY timestamp ASC");
    stmt.bind(1, start);
    stmt.bind(2, end);

    /* executeStep() runs the query and advances to the next matching
     * row each time it's called -- returns true if a row was found,
     * false once there are no more (this is the loop's exit
     * condition). This is the SELECT counterpart to exec()/bind():
     * a SELECT can return any number of rows, not just "did it run". */
    while (stmt.executeStep()) {
        Measurement m;
        /* getColumn(i) reads the i-th column of the CURRENT row (0-indexed,
         * matching the SELECT list's order above) -- .getString()/.getInt()
         * convert it to the C++ type we actually want. */
        m.timestamp = stmt.getColumn(0).getString();
        m.temp_c = stmt.getColumn(1).getInt();
        m.humidity_pct = stmt.getColumn(2).getInt();
        m.light_pct = stmt.getColumn(3).getInt();
        m.battery_pct = stmt.getColumn(4).getInt();
        m.mode = stmt.getColumn(5).getInt();
        result.push_back(m);
    }
    return result;
}

std::vector<DataCollectionAnalysis::Event>
DataCollectionAnalysis::query_events(const std::string &start, const std::string &end)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Event> result;

    SQLite::Statement stmt(db_,
        "SELECT timestamp, type, description FROM events "
        "WHERE timestamp >= ? AND timestamp <= ? ORDER BY timestamp ASC");
    stmt.bind(1, start);
    stmt.bind(2, end);

    while (stmt.executeStep()) {
        Event e;
        e.timestamp = stmt.getColumn(0).getString();
        e.type = stmt.getColumn(1).getString();
        e.description = stmt.getColumn(2).getString();
        result.push_back(e);
    }
    return result;
}

/* The only place that locks for a purge -- callers from outside this
 * class (anyone but the constructor) should always come through here,
 * never call purge_old_locked() directly. */
void DataCollectionAnalysis::purge_old()
{
    std::lock_guard<std::mutex> lock(mutex_);
    purge_old_locked();
}

/* Deliberately does NOT lock mutex_ -- see this method's declaration
 * in the header for why (the constructor calls this directly while
 * already holding the lock; locking again here would deadlock it). */
void DataCollectionAnalysis::purge_old_locked()
{
    /* Date-only ("YYYY-MM-DD"), same fixed-width zero-padded shape as
     * every stored timestamp's own date portion -- plain string
     * comparison in SQL ('<') sorts these correctly, same trick
     * Log::purge_old_files() already relies on. */
    std::string cutoff = TimeUtils::days_ago_date(retention_days_);

    SQLite::Statement del_measurements(db_, "DELETE FROM measurements WHERE timestamp < ?");
    del_measurements.bind(1, cutoff);
    del_measurements.exec();

    SQLite::Statement del_events(db_, "DELETE FROM events WHERE timestamp < ?");
    del_events.bind(1, cutoff);
    del_events.exec();
}
