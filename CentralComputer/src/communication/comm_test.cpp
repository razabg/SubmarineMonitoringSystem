/*
 * comm_test.cpp - manual round-trip test for the Communication class.
 *
 * Constructs a Communication against either a serial device or (via the
 * Ethernet-simulation gateway) a TCP connection, and prints every
 * decoded frame that arrives, tagged by which handler routed it
 * (LOG vs MGMT). Ctrl-C to quit.
 *
 *   ./comm_test /dev/ttyACM0        (UART, direct -- default if no args)
 *   ./comm_test --tcp 127.0.0.1:5555  (Ethernet, via the gateway -- see
 *                                       CLAUDE.md's "Ethernet-simulation
 *                                       gateway" note; start ./gateway
 *                                       first, it's the TCP server)
 *
 * Purpose: prove the Communication class's RX pipeline (rx_thread_ ->
 * Transport::read() -> tlv_receiver_feed() -> frame_trampoline() ->
 * route_frame() -> the registered handler) works end to end against
 * real hardware -- sermon.cpp already proved the raw bytes are correct,
 * this proves our own class decodes and routes them correctly too. Same
 * Communication code either way; only which concrete Transport it was
 * handed differs (see transport.h).
 *
 * Also exercises the LNC's TLV_TAG_QUERY_DATA/QUERY_EVENTS handlers
 * (log_on_frame()/event_on_frame() in the firmware): press Enter once
 * the board has booted and settled to send both, each with the widest
 * possible time range (2000-01-01 through 2099-12-31) so every stored
 * record matches regardless of what's actually on the SD card. Replies
 * (TLV_TAG_QUERY_RECORD/QUERY_END) route to on_log_ same as everything
 * else Log/Event send, so they print as ordinary "LOG ..." lines below.
 */
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "communication.h"
#include "serial_transport.h"
#include "tcp_transport.h"
#include "tlv.h"

static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void print_frame(const char *label, const tlv_frame_t &f)
{
    std::printf("%s tag=0x%02X len=%u value=", label, f.tag, f.len);
    for (uint8_t i = 0; i < f.len; i++) {
        std::printf("%02X ", f.value[i]);
    }
    std::printf("\n");
    std::fflush(stdout);
}

/* Matches log.c's/event.c's query_range_payload_t exactly (see
 * CLAUDE.md's "Section 2.5's two Instructions received" note): 12
 * bytes, start then end, each year/month/date/hour/min/sec. year is
 * an offset from 2000, same convention as every other timestamp
 * payload in this protocol. */
struct QueryRangePayload {
    uint8_t start_year, start_month, start_date, start_hour, start_min, start_sec;
    uint8_t end_year, end_month, end_date, end_hour, end_min, end_sec;
} __attribute__((packed));

static void send_query_all(Communication &comm, uint8_t tag, const char *label)
{
    QueryRangePayload p = {
        0, 1, 1, 0, 0, 0,        /* 2000-01-01 00:00:00 */
        99, 12, 31, 23, 59, 59   /* 2099-12-31 23:59:59 */
    };
    std::printf(">> sending %s (tag=0x%02X), widest possible range\n", label, tag);
    std::fflush(stdout);
    comm.send(tag, reinterpret_cast<const uint8_t *>(&p), sizeof(p));
}

/* Parses "host:port" (e.g. "127.0.0.1:5555") for --tcp. Returns false
 * (and leaves an error on stderr) on a malformed argument. */
static bool parse_host_port(const std::string &arg, std::string &host_out, uint16_t &port_out)
{
    size_t colon = arg.find(':');
    if (colon == std::string::npos || colon == 0 || colon == arg.size() - 1) {
        std::fprintf(stderr, "expected host:port, e.g. 127.0.0.1:5555 (got \"%s\")\n", arg.c_str());
        return false;
    }
    host_out = arg.substr(0, colon);
    port_out = static_cast<uint16_t>(std::atoi(arg.substr(colon + 1).c_str()));
    return true;
}

int main(int argc, char **argv)
{
    /* Which Transport to build is decided once, here -- Communication
     * itself never knows or cares which one it ends up with. */
    std::unique_ptr<Transport> transport;

    try {
        if (argc > 1 && std::string(argv[1]) == "--tcp") {
            if (argc < 3) {
                std::fprintf(stderr, "usage: %s --tcp host:port\n", argv[0]);
                return 1;
            }
            std::string host;
            uint16_t port = 0;
            if (!parse_host_port(argv[2], host, port)) {
                return 1;
            }
            std::printf("connecting to gateway at %s:%u ...\n", host.c_str(), port);
            std::fflush(stdout);
            transport = std::make_unique<TcpTransport>(host, port);
        } else {
            const char *path = (argc > 1) ? argv[1] : "/dev/ttyACM0";
            std::printf("opening %s ...\n", path);
            std::fflush(stdout);
            transport = std::make_unique<SerialTransport>(path);
        }

        Communication comm(*transport);

        comm.set_log_handler([](const tlv_frame_t &f) {
            print_frame("LOG ", f);
        });
        comm.set_management_handler([](const tlv_frame_t &f) {
            print_frame("MGMT", f);
        });

        std::signal(SIGINT, on_sigint);
        std::printf("listening. reset the board now.\n");
        std::printf("once it's booted and settled, press Enter to send test "
                     "QUERY_DATA/QUERY_EVENTS requests. Ctrl-C to quit.\n");

        /* Runs on its own thread since main's loop below has to keep
         * polling g_stop for Ctrl-C -- blocking main on std::getline
         * would swallow that. Detached: the whole process exits when
         * main() returns, at which point an still-blocked getline is
         * simply abandoned, fine for a short-lived manual test tool. */
        std::thread query_thread([&comm]() {
            std::string dummy;
            std::getline(std::cin, dummy);
            if (!g_stop) {
                send_query_all(comm, TLV_TAG_QUERY_DATA, "QUERY_DATA");
                send_query_all(comm, TLV_TAG_QUERY_EVENTS, "QUERY_EVENTS");
            }
        });
        query_thread.detach();

        while (!g_stop) {
            /* rx_thread_ (inside comm) does all the real work; this
             * thread just waits, so the Communication object stays
             * alive and its handlers keep getting called. */
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    std::printf("\nclosed\n");
    return 0;
}