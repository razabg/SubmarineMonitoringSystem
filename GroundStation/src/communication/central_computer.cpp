#include "central_computer.h"

std::vector<std::string> CentralComputer::queryLogs(const std::string &start, const std::string &end) const
{
    return {"(mock) no live Central Computer link yet -- would return log lines for "
            + start + " .. " + end};
}

std::vector<std::string> CentralComputer::queryEvents(const std::string &start, const std::string &end) const
{
    return {"(mock) no live Central Computer link yet -- would return event lines for "
            + start + " .. " + end};
}
