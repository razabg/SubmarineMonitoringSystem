/*
 * log.cpp - Central Computer Log module.
 * See log.h for the class-level overview.
 */
#include "log.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "time_utils.h"

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
    return TimeUtils::days_ago_date(0); /* "0 days ago" is today */
}

std::string Log::timestamp_now()
{
    return TimeUtils::now();
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

    /* ofstream::open() never creates missing directories, only files --
     * without this, a fresh deployment with no logs/ folder yet would
     * fail to start the very first time. create_directories() is a
     * no-op (returns false, doesn't throw) if the directory already
     * exists, so this is safe to call on every rollover, not just the
     * first one. */
    fs::path dir = fs::path(file_path_for(today)).parent_path();
    if (!dir.empty())
    {
        std::error_code ec;
        fs::create_directories(dir, ec);
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
    return TimeUtils::days_ago_date(retention_days);
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
