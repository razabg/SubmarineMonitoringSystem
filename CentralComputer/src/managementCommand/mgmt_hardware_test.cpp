/*
 * mgmt_hardware_test.cpp - manual real-hardware test for
 * ManagementCommand.
 *
 * Connects like comm_test.cpp/dca_hardware_test.cpp. Constructing
 * ManagementCommand alone is enough to fix the TIME_SYNC_REQUEST gap
 * -- reset the board and watch for "MGMT: LNC reports time ..." style
 * replies with no further action needed. Press Enter once connected
 * to also send a couple of manual test commands (SET_TEMP_NORMAL,
 * GET_TIME). Ctrl-C to quit.
 *
 *   ./mgmt_hardware_test /dev/ttyACM0
 *   ./mgmt_hardware_test --tcp 127.0.0.1:5555
 */
#include <csignal>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "communication.h"
#include "management_command.h"
#include "serial_transport.h"
#include "tcp_transport.h"

/* Set by the SIGINT handler below, read by main()'s loop -- sig_atomic_t
 * + volatile is the safe type for a flag touched from a signal handler. */
static volatile sig_atomic_t g_stop = 0;

/* Registered for Ctrl-C: just flips the flag, does no real work here
 * (signal handlers must stay minimal). */
static void on_sigint(int sig)
{
    (void)sig;
    g_stop = 1;
}

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
        /* Constructing this alone wires up TIME_SYNC_REQUEST handling --
         * see management_command.h. */
        ManagementCommand mgmt(comm);

        std::signal(SIGINT, on_sigint);
        std::printf("listening. reset the board now.\n");
        std::printf("press Enter to also send SET_TEMP_NORMAL(15,27) + GET_TIME. Ctrl-C to quit.\n");

        /* On its own thread since std::getline blocks -- main()'s loop
         * below still needs to keep polling g_stop for Ctrl-C. Detached:
         * nothing to join if the user quits without ever pressing Enter. */
        std::thread input_thread([&mgmt]() {
            std::string dummy;
            std::getline(std::cin, dummy); //enter pressed
            if (!g_stop) {
                bool ok = mgmt.set_temp_normal(15, 27);
                std::printf(">> set_temp_normal(15, 27): %s\n", ok ? "sent" : "rejected");
                mgmt.get_time();
                std::printf(">> get_time(): sent\n");
                std::fflush(stdout);
            }
        });
        input_thread.detach();

        /* comm's own rx_thread_ does all the real work in the background;
         * this just keeps the program (and comm/mgmt) alive until Ctrl-C. */
        while (!g_stop) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    std::printf("\nclosed\n");
    return 0;
}
