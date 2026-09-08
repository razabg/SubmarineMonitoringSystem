/*
 * dca_hardware_test.cpp - manual real-hardware test for
 * DataCollectionAnalysis's live ingestion path.
 *
 * Same Transport/Communication setup as comm_test.cpp (see that
 * file's header comment for the full RX-pipeline explanation), but
 * instead of just printing decoded frames, this wires
 * DataCollectionAnalysis::on_frame() as the log handler too -- so
 * every real MODE_CHANGE/KEEP_ALIVE/OBJECT_DETECTED/OBJECT_CLEARED
 * frame the board sends gets stored into a real SQLite database file,
 * not just printed. Frames still print as well, so you can compare
 * "what arrived" against "what's in the table" afterward.
 *
 *   ./dca_hardware_test /dev/ttyACM0          (UART, direct -- default)
 *   ./dca_hardware_test --tcp 127.0.0.1:5555  (Ethernet, via the gateway)
 *
 * After running for a while (reset the board, let Monitor/Keep-Alive/
 * Object Detection produce some real traffic, Ctrl-C to quit), inspect
 * the result with the sqlite3 CLI:
 *   sqlite3 test_output/hardware_run.db "SELECT * FROM measurements;"
 *   sqlite3 test_output/hardware_run.db "SELECT * FROM events;"
 */
#include <csignal>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "communication.h"
#include "data_collection_analysis.h"
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

/* Same as comm_test.cpp's -- parses "host:port" for --tcp. */
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
            transport = std::make_unique<TcpTransport>(std::move(host), port);
        } else {
            const char *path = (argc > 1) ? argv[1] : "/dev/ttyACM0";
            std::printf("opening %s ...\n", path);
            std::fflush(stdout);
            transport = std::make_unique<SerialTransport>(path);
        }

        Communication comm(*transport);
        DataCollectionAnalysis dca("test_output/hardware_run.db", /*retention_days=*/7);

        comm.set_log_handler([&dca](const tlv_frame_t &f) {
            print_frame("LOG ", f);
            dca.on_frame(f);
        });
        comm.set_management_handler([](const tlv_frame_t &f) {
            print_frame("MGMT", f);
        });

        std::signal(SIGINT, on_sigint);
        std::printf("listening. reset the board now. Ctrl-C to quit.\n");

        while (!g_stop) {
            /* rx_thread_ (inside comm) does all the real work; this
             * thread just waits, so Communication (and dca, which its
             * handler closes over) stay alive and keep getting called. */
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    std::printf("\nclosed. inspect test_output/hardware_run.db with the sqlite3 CLI.\n");
    return 0;
}
