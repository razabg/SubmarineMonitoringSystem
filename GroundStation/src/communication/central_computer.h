/*
 * central_computer.h - GroundStation's handle to a combat submarine's
 * real Central Computer (Part B), reached over Ethernet (section 4:
 * "requests stored log data and event data for a given date/time
 * range"). A CombatSubmarine composes one of these -- see
 * combat_submarine.h.
 *
 * MOCKED for now: returns canned data instead of opening a real
 * connection, so the fleet-management side can be built and tested
 * without depending on unbuilt Part-B server work. Real wiring
 * (planned): this class holds a TcpTransport (moved from
 * CentralComputer/src/communication into Shared/, since both programs
 * need it -- see chat) as the TCP client, with Central Computer
 * adding a matching TCP server/listener it doesn't have yet. Swapping
 * the mock body for that real client is meant to be the only change
 * -- callers (CombatSubmarine) never see the difference.
 */
#ifndef CENTRAL_COMPUTER_H
#define CENTRAL_COMPUTER_H

#include <string>
#include <vector>

class CentralComputer
{
public:
    CentralComputer() = default;

    /* Returns every stored log/event line in [start, end]
     * ("YYYY-MM-DD HH:MM:SS" strings, matching the Central Computer's
     * own DataCollectionAnalysis format). Mocked: always returns a
     * placeholder line noting no real link exists yet. */
    std::vector<std::string> queryLogs(const std::string &start, const std::string &end) const;
    std::vector<std::string> queryEvents(const std::string &start, const std::string &end) const;
};

#endif /* CENTRAL_COMPUTER_H */
