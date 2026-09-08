/*
 * gateway.cpp - Ethernet-simulation gateway.
 *
 *   ./gateway [serial_device] [tcp_port]
 *   ./gateway /dev/ttyACM0 5555   (defaults if omitted)
 *
 * See CLAUDE.md's "Ethernet-simulation gateway" note for the full design.
 * Short version: the board only ever has UART; this process owns the
 * real serial port (via SerialPort, uartTransport/serial.h -- no new
 * UART code) and exposes it as a plain TCP server on localhost, so the
 * Central Computer's TcpTransport can connect to it and, as far as its
 * own code is concerned, be talking Ethernet to the LNC.
 *
 * This process is a dumb byte pipe: it never calls tlv_encode() or
 * tlv_receiver_feed_byte(), has no dependency on Shared/ProtocolTLV, and
 * does not know what a TLV frame is. It only ever moves raw bytes.
 * Encode/decode happens only on the two real endpoints (the LNC
 * firmware and the Central Computer), regardless of which transport
 * carried the bytes.
 *
 * Role: this process is the TCP *server* (it owns the serial port, the
 * resource being shared); the Central Computer is always the *client*.
 * One client at a time -- accept() loops back around for the next
 * connection once the current one ends, so the Central Computer can be
 * restarted/reconnected without restarting this process.
 */
#include "serial.h"

#include <atomic>   /* std::atomic<bool> stop_flag -- shared safely between the two relay threads */
#include <csignal>  /* std::signal, SIGINT, sig_atomic_t */
#include <cstdio>   /* std::printf/fprintf -- stdout for status, stderr for errors (see below) */
#include <cstdint>
#include <cstdlib>  /* std::atoi -- parses the tcp_port argument */
#include <cstring>  /* std::strerror -- turns an errno value into a readable message */
#include <memory>   /* std::unique_ptr, std::make_unique */
#include <thread>   /* std::thread, std::ref */

#include <arpa/inet.h>   /* inet_ntoa() -- binary IP back to printable text, htons()/ntohs() */
#include <netinet/in.h>  /* struct sockaddr_in, INADDR_LOOPBACK */
#include <sys/socket.h>  /* socket(), bind(), listen(), accept(), send(), recv(), setsockopt() */
#include <unistd.h>      /* close() */

namespace { /* everything below is private to this file -- same idea as C's `static` on a function */

volatile sig_atomic_t g_stop = 0; /* sig_atomic_t: the one integer type the C++ standard
                                    * guarantees is safe to write from inside a signal
                                    * handler -- an ordinary int isn't guaranteed-safe there */

/* Runs when Ctrl-C is pressed. Does the absolute minimum -- just sets
 * the flag -- same "handlers do the minimum" instinct as an ISR on the
 * LNC side; anything more here would run in a signal-handling context
 * that's unsafe for most real work. */
void on_sigint(int sig)
{
    (void)sig;
    g_stop = 1;
}

/* Matches SerialPort's own VMIN=0/VTIME=1 (0.1s) read timeout, and
 * TcpTransport's SO_RCVTIMEO -- so every blocking call in this relay
 * wakes up periodically on its own to notice g_stop or a session-local
 * "time to stop" flag, without needing to be forcibly interrupted from
 * another thread. constexpr: fixed at compile time, not just "won't
 * change" like plain const. */
constexpr int kRecvTimeoutSec = 0;
constexpr int kRecvTimeoutUsec = 100000;

/* Relays serial_port -> client_fd until either side fails or stop_flag
 * is set. Runs on its own thread; the mirror direction
 * (tcp_to_serial()) runs on another. Neither touches the other's state
 * -- the two directions are fully independent, so no locking needed
 * between them. */
void serial_to_tcp(SerialPort &serial_port, int client_fd, std::atomic<bool> &stop_flag)
{
    uint8_t buf[256];

    while (!stop_flag.load() && !g_stop) {
        long n;
        try {
            n = serial_port.read(buf, sizeof(buf)); /* blocks up to ~100ms, see VMIN/VTIME above */
        } catch (const std::exception &e) {
            /* stderr, not stdout: keeps error output separable from
             * normal status messages (e.g. `./gateway 2>errors.log`
             * captures only this). fprintf/printf throughout this file
             * (not std::cout) matches SerialPort's/comm_test.cpp's own
             * style -- mixing iostream and cstdio output in one program
             * risks them printing out of order relative to each other. */
            std::fprintf(stderr, "gateway: serial read failed: %s\n", e.what());
            stop_flag = true;
            break;
        }

        if (n <= 0) {
            continue; /* nothing arrived within the timeout -- normal, keep going */
        }

        ssize_t sent = ::send(client_fd, buf, static_cast<size_t>(n), 0);
        if (sent <= 0) {
            /* Client gone -- let tcp_to_serial's own recv() discover
             * the same thing and report it; this thread just stops. */
            stop_flag = true;
            break;
        }

        /* Byte count only, never the content -- still a dumb pipe, no
         * TLV parsing (see this file's header comment); this is just
         * visible proof relaying is actually happening during a demo. */
        std::printf("gateway: relayed %ld bytes serial -> tcp\n", n);
        std::fflush(stdout);
    }
}

/* Relays client_fd -> serial_port. See serial_to_tcp()'s comment above
 * for why the two directions don't need to coordinate with each other
 * beyond the shared stop_flag. */
void tcp_to_serial(int client_fd, SerialPort &serial_port, std::atomic<bool> &stop_flag)
{
    uint8_t buf[256];

    while (!stop_flag.load() && !g_stop) {
        ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
        if (n == 0) {
            /* recv() returning exactly 0 means the client closed the
             * connection -- a real disconnect (different from a serial
             * port's read timeout, which just means "nothing yet"). */
            std::printf("gateway: client disconnected\n");
            std::fflush(stdout);
            stop_flag = true;
            break;
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue; /* SO_RCVTIMEO elapsed, nothing arrived -- normal, not an error */
            }
            if (errno == EINTR) {
                continue; /* a signal interrupted the call -- not a real error, retry */
            }
            std::fprintf(stderr, "gateway: tcp recv failed: %s\n", std::strerror(errno));
            stop_flag = true;
            break;
        }

        try {
            serial_port.write(buf, static_cast<size_t>(n));
        } catch (const std::exception &e) {
            std::fprintf(stderr, "gateway: serial write failed: %s\n", e.what());
            stop_flag = true;
            break;
        }

        std::printf("gateway: relayed %zd bytes tcp -> serial\n", n);
        std::fflush(stdout);
    }
}

/* Runs both relay directions for one connected client, until either
 * side ends the session, then returns once both threads have stopped. */
void run_session(SerialPort &serial_port, int client_fd)
{
    std::atomic<bool> stop_flag{false}; /* fresh flag per session -- both threads below share it */

    /* std::thread's constructor takes a function plus its arguments and
     * immediately starts a new OS thread running that function
     * concurrently. By default std::thread COPIES every argument into
     * its own storage (a safety default, in case the caller's locals
     * go away before the new thread gets to them) -- but
     * serial_to_tcp()/tcp_to_serial() take `SerialPort &`/`atomic<bool> &`
     * (references): copying would try to build a whole new SerialPort
     * (copying is disabled on it) and would give each thread its own
     * separate stop_flag instead of the one they need to share.
     * std::ref() tells std::thread "pass this through as a real
     * reference, don't copy it." client_fd (a plain int) has no such
     * problem, so it's passed normally. */
    std::thread t1(serial_to_tcp, std::ref(serial_port), client_fd, std::ref(stop_flag));
    std::thread t2(tcp_to_serial, client_fd, std::ref(serial_port), std::ref(stop_flag));

    t1.join(); /* block here until t1 has actually stopped running */
    t2.join(); /* ... and until t2 has too, before returning to the caller */
}

} /* namespace */

int main(int argc, char **argv)
{
    const char *serial_path = (argc > 1) ? argv[1] : "/dev/ttyACM0";
    uint16_t tcp_port = (argc > 2) ? static_cast<uint16_t>(std::atoi(argv[2])) : 5555;

    std::signal(SIGINT, on_sigint); /* register the Ctrl-C handler */

    std::printf("gateway: opening %s ...\n", serial_path);
    std::fflush(stdout); /* forces this line to appear now, not whenever the output buffer fills */

    /* unique_ptr, not a plain `SerialPort serial_port(...)`: this needs
     * to be constructed conditionally, inside the try block below, and
     * then used afterward outside it -- a plain SerialPort's
     * constructor has to run right at its declaration, a unique_ptr can
     * start empty and be assigned later. This is also the fix for a
     * real bug found by testing: SerialPort's constructor throws on
     * failure, and an earlier version of this file let that exception
     * escape main() uncaught -- which calls std::terminate(), crashing
     * via abort() instead of failing gracefully. This try/catch (same
     * pattern comm_test.cpp already uses) is the fix. */
    std::unique_ptr<SerialPort> serial_port_ptr;
    try {
        serial_port_ptr = std::make_unique<SerialPort>(serial_path);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "gateway: opening %s failed: %s\n", serial_path, e.what());
        return 1;
    }
    /* Dereference the unique_ptr once, here, into a plain reference --
     * everything below this line (run_session(), the relay threads)
     * just uses `serial_port` like an ordinary object and never has to
     * think about pointers again. */
    SerialPort &serial_port = *serial_port_ptr;

    /* AF_INET = IPv4, SOCK_STREAM = TCP (vs. SOCK_DGRAM for UDP), 0 =
     * let the OS pick the default protocol for that combination. */
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::fprintf(stderr, "gateway: socket() failed: %s\n", std::strerror(errno));
        return 1;
    }

    /* Lets the gateway be restarted right away without "address already
     * in use" errors from a socket still winding down in TIME_WAIT. */
    int reuse = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    /* htonl/htons = "host TO network long/short" -- multi-byte numbers
     * can be stored in memory two different ways (little-endian, what
     * x86/x86-64 uses, or big-endian); network protocols always require
     * big-endian ("network byte order") regardless of the machine's own
     * native format, so these convert before the value goes into a
     * struct the OS network stack will use. htonl for the 32-bit
     * address, htons for the 16-bit port. INADDR_LOOPBACK = 127.0.0.1:
     * localhost only, matching the design (nothing outside this
     * machine can connect). */
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(tcp_port);

    /* bind()/accept() predate C++ (and struct inheritance) -- they were
     * designed in C to work with ANY address family through one generic
     * parameter type, struct sockaddr*. You build the specific struct
     * for the family you're actually using (sockaddr_in, for IPv4) and
     * reinterpret_cast it to the generic type the function expects.
     * This is valid (not just a risky type-pun) specifically because
     * sockaddr_in was deliberately designed with the same initial
     * layout as sockaddr -- the classic pre-C++ way of getting
     * polymorphism via a shared struct prefix. */
    if (::bind(listen_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0) {
        std::fprintf(stderr, "gateway: bind() failed: %s\n", std::strerror(errno));
        ::close(listen_fd);
        return 1;
    }

    if (::listen(listen_fd, 1) != 0) { /* backlog 1: one Central Computer at a time */
        std::fprintf(stderr, "gateway: listen() failed: %s\n", std::strerror(errno));
        ::close(listen_fd);
        return 1;
    }

    std::printf("gateway: listening on 127.0.0.1:%u, relaying to/from %s\n",
                tcp_port, serial_path);
    std::printf("gateway: waiting for the Central Computer to connect. Ctrl-C to quit.\n");
    std::fflush(stdout);

    while (!g_stop) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        /* Blocks until someone connects, then returns a NEW, separate
         * fd just for talking to that one client -- listen_fd itself
         * stays reserved purely for accepting future connections. */
        int client_fd = ::accept(listen_fd, reinterpret_cast<struct sockaddr *>(&client_addr),
                                  &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue; /* Ctrl-C during accept() -- g_stop check at the top handles it */
            }
            std::fprintf(stderr, "gateway: accept() failed: %s\n", std::strerror(errno));
            continue;
        }

        /* Matches SerialPort's own read timeout -- see the constants
         * above -- so tcp_to_serial()'s recv() wakes up periodically
         * instead of blocking forever if the client goes quiet without
         * actually disconnecting. Applied per-client, right after each
         * accept(), since it's a property of this one connection. */
        struct timeval tv{};
        tv.tv_sec = kRecvTimeoutSec;
        tv.tv_usec = kRecvTimeoutUsec;
        ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        /* inet_ntoa(): binary IP -> printable text, the reverse of what
         * inet_pton()/getaddrinfo() do when building an address.
         * ntohs(): "network TO host short" -- the reverse of htons()
         * above, converting the port we just received back to this
         * machine's native format for printing. */
        std::printf("gateway: client connected from %s:%u\n",
                    inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        std::fflush(stdout);

        run_session(serial_port, client_fd); /* blocks here for the whole session */

        ::close(client_fd);
        std::printf("gateway: session ended, waiting for the next connection.\n");
        std::fflush(stdout);
    }

    ::close(listen_fd);
    std::printf("gateway: closed\n");
    return 0;
}
