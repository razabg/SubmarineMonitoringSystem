/*
 * comm_test.cpp - manual round-trip test for the Communication class.
 *
 * Constructs a Communication against a serial device and prints every
 * decoded frame that arrives, tagged by which handler routed it
 * (LOG vs MGMT). Ctrl-C to quit.
 *
 *   ./comm_test /dev/ttyACM0
 *
 * Purpose: prove the Communication class's RX pipeline (rx_thread_ ->
 * SerialPort::read() -> tlv_receiver_feed() -> frame_trampoline() ->
 * route_frame() -> the registered handler) works end to end against
 * real hardware -- sermon.cpp already proved the raw bytes are correct,
 * this proves our own class decodes and routes them correctly too.
 */
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>

#include "communication.h"

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

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/dev/ttyACM0";

    std::printf("opening %s ...\n", path);

    try {
        Communication comm(path);

        comm.set_log_handler([](const tlv_frame_t &f) {
            print_frame("LOG ", f);
        });
        comm.set_management_handler([](const tlv_frame_t &f) {
            print_frame("MGMT", f);
        });

        std::signal(SIGINT, on_sigint);
        std::printf("listening. reset the board now. Ctrl-C to quit.\n");

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