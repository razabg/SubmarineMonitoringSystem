/*
 * log.cpp - Central Computer Log module.
 * See log.h for the class-level overview.
 */
#include "log.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

Log::Log(std::string base_path, int retention_days)
    : base_path_(std::move(base_path)), retention_days_(retention_days)
{
    std::lock_guard<std::mutex> lock(mutex_);
    roll_to_today_if_needed(); /* opens today's file; throws if that fails */
}

Log::~Log()
{
    /* file_ (std::ofstream) closes itself automatically when destroyed --
     * nothing else to do here. Kept explicit for the class's RAII
     * documentation, matching every other class in this project. */
}

std::string Log::today_date()
{
    return timestamp_now().substr(0, 10); /* "YYYY-MM-DD" is the first 10 characters of "YYYY-MM-DD HH:MM:SS" */
}

std::string Log::timestamp_now()
{
    /* now(): the current instant as an opaque chrono time_point --
     * to_time_t(): converts it to a plain integer (seconds since 1970),
     * the type localtime_r()/put_time() below actually know how to read. */
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm tm{}; /* zero-initialized calendar struct; localtime_r fills it in below */
    /* localtime_r, not std::localtime: the plain standard version isn't
     * thread-safe (it writes through one shared static buffer) -- this
     * class explicitly supports being called from more than one thread. */
    localtime_r(&t, &tm);

    std::ostringstream oss;                         /* string-builder stream */
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S"); /* formats tm per this pattern, same codes as strftime */
    return oss.str();
}

std::string Log::file_path_for(const std::string &date) const
{
    return base_path_ + "-" + date + ".log";
}

void Log::roll_to_today_if_needed()
{
    std::string today = today_date();
    if (today == open_date_)
    {
        return; /* already on the right file -- the common case, every write() call checks this */
    }

    if (file_.is_open())
    {
        file_.close();
    }

    file_.open(file_path_for(today), std::ios::app);
    if (!file_.is_open())
    {
        throw std::runtime_error("Log: failed to open " + file_path_for(today));
    }
    open_date_ = today;

    purge_old_files(); /* only on an actual day change, not every write() */
}

std::string Log::compute_cutoff_date(int retention_days) // default = 7 like the lnc
{
    auto cutoff_tp = std::chrono::system_clock::now() - std::chrono::hours(24 * retention_days);
    std::time_t cutoff_t = std::chrono::system_clock::to_time_t(cutoff_tp);

    std::tm tm{};
    localtime_r(&cutoff_t, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d");
    return oss.str();
}

std::string Log::parse_file_date(const std::string &filename, const std::string &prefix,
                                 const std::string &suffix)
{
    if (filename.size() != prefix.size() + 10 + suffix.size())
    {
        return ""; /* wrong length -- can't match the pattern */
    }
    if (filename.compare(0, prefix.size(), prefix) != 0)
    {
        return ""; /* doesn't start with "<prefix>" */
    }
    if (filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) != 0)
    {
        return ""; /* doesn't end with "<suffix>" */
    }
    return filename.substr(prefix.size(), 10); /* the "YYYY-MM-DD" in the middle */
}

void Log::purge_old_files() const
{
    /* fs::path can split itself into a directory part and a filename
     * part -- e.g. "logs/central_computer" -> parent_path()="logs",
     * filename()="central_computer". Falls back to "." (current
     * directory) when base_path_ has no directory component at all. */
    fs::path base(base_path_);
    fs::path dir = base.has_parent_path() ? base.parent_path() : fs::path(".");
    std::string prefix = base.filename().string() + "-";
    const std::string suffix = ".log";

    std::string cutoff_date = compute_cutoff_date(retention_days_);

    if (!fs::exists(dir))
    {
        return;
    }

    /* one `entry` per item in dir -- "for each item in this directory" */
    for (const auto &entry : fs::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
        {
            continue; /* skip subdirectories/symlinks/etc. */
        }

        std::string file_date = parse_file_date(entry.path().filename().string(), prefix, suffix);
        if (file_date.empty())
        {
            continue; /* doesn't match "<prefix>YYYY-MM-DD<suffix>" -- not one of ours, leave it alone */
        }

        /* "YYYY-MM-DD" is fixed-width and zero-padded, so plain string
         * comparison already matches chronological order -- no need to
         * parse it into a real date type just to compare it. */
        if (file_date < cutoff_date)
        {
            std::error_code ec;
            fs::remove(entry.path(), ec); /* best-effort -- a failed delete isn't fatal */
        }
    }
}

void Log::write(const std::string &message)
{
    std::lock_guard<std::mutex> lock(mutex_);
    roll_to_today_if_needed();

    std::string line = "[" + timestamp_now() + "] " + message;

    std::cout << line << std::endl; /* std::endl: '\n' plus an explicit flush, so it's visible right away */

    file_ << line << '\n';
    file_.flush(); /* so a live `tail -f` or editor reload always sees current content */
}
