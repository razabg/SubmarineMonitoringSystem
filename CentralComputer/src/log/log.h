/*
 * log.h - Central Computer Log module (section 3, "prints logs and
 * writes them to files").
 *
 * This is the Central Computer's own OPERATIONAL log -- a running trail
 * of what the CC program itself is doing (connections, commands sent,
 * replies received, errors) -- not a copy of the LNC's sensor/event
 * data. That data (measurements + events) is Data Collection &
 * Analysis's job, stored in the database; writing it here too would be
 * the same data persisted twice for no reason.
 *
 * RAII, matching every other class in this project (SerialPort,
 * Communication): the constructor prepares things, the destructor
 * cleans up -- no separate open()/close() to remember. Modern C++
 * throughout (std::cout/std::ofstream, std::filesystem for rotation),
 * unlike the printf-family style used in the transport-layer code --
 * that C-style choice was specifically about matching errno/strerror
 * system-call error reporting, which doesn't apply to this module.
 *
 * Rotation: one real file per calendar day (date-stamped filename --
 * no 8.3-short-filename constraint here like the LNC's SD card has, so
 * no need for its weekday-slot workaround), keeping `retention_days`
 * days by default (7, matching the "one week" convention used
 * everywhere else in this project).
 *
 * Thread-safe: write() may be called from more than one thread at once
 * (e.g. Communication's rx_thread_ reacting to an incoming frame, vs.
 * the main thread issuing a command directly) -- same reasoning as
 * Communication::send_mutex_.
 *
 * Parameter-passing convention used throughout this class (and this
 * project): a parameter the function only ever READS takes `const T&`
 * -- no copy, ever. A parameter the function needs to STORE somewhere
 * long-lived (here, base_path into base_path_) takes T BY VALUE, then
 * gets std::move'd into storage -- not const&, because you can't move
 * out of a const reference, so const&+copy would force a copy on
 * every call with no exceptions; by-value+move lets the compiler skip
 * that copy whenever the caller passes a temporary, and costs no more
 * than const&+copy would have when they don't. Same idiom already used
 * for Communication::set_management_handler()'s FrameHandler param.
 */
#ifndef LOG_H
#define LOG_H

#include <fstream>
#include <mutex>
#include <string>

class Log
{
public:
    /* Files are named "<base_path>-YYYY-MM-DD.log". base_path is taken
     * by value and moved into base_path_ (stored -- see the file
     * comment above for why not const&). Throws std::runtime_error if
     * today's file can't be opened. */
    explicit Log(std::string base_path, int retention_days = 7);

    /* Closes the current file. Never throws. */
    ~Log();

    Log(const Log &) = delete;
    Log &operator=(const Log &) = delete;
    Log(Log &&) = delete;
    Log &operator=(Log &&) = delete;

    /* Prepends a "[YYYY-MM-DD HH:MM:SS] " timestamp to `message`, prints
     * it (std::cout) and appends it to today's file (flushed
     * immediately). Rolls over to a fresh file, and purges anything
     * older than retention_days, the first time this is called on a
     * new calendar day. Thread-safe. `message` is only ever read here,
     * never stored -- const& is both correct and cheapest. */
    void write(const std::string &message);

private:
    std::string base_path_;
    int retention_days_;
    std::ofstream file_;
    std::string open_date_; /* which date's file file_ currently is; empty if none yet */
    std::mutex mutex_;

    static std::string today_date();     /* "YYYY-MM-DD" */
    static std::string timestamp_now();  /* "YYYY-MM-DD HH:MM:SS" */

    /* `date` is only read here to build a return value -- const&. */
    std::string file_path_for(const std::string &date) const;

    /* Opens today's file if open_date_ isn't already today, and (only
     * on that same transition) purges files older than
     * retention_days_. Caller must hold mutex_. */
    void roll_to_today_if_needed();

    /* The oldest date still worth keeping, "YYYY-MM-DD" (today minus
     * `retention_days` days). Pure -- doesn't touch any member state,
     * so it's static and takes the day count as a plain parameter
     * rather than reading retention_days_ itself. */
    static std::string compute_cutoff_date(int retention_days);

    /* If `filename` matches "<prefix>YYYY-MM-DD<suffix>" exactly,
     * returns the embedded date; otherwise returns "" (no match). Pure
     * string parsing, no filesystem access -- easy to reason about (and
     * to test) on its own, separately from purge_old_files()'s
     * directory-iteration/deletion logic. */
    static std::string parse_file_date(const std::string &filename,
                                        const std::string &prefix,
                                        const std::string &suffix);

    /* Deletes any "<base_path>-*.log" file whose date is older than
     * retention_days_ days ago. Caller must hold mutex_. */
    void purge_old_files() const;
};

#endif /* LOG_H */
