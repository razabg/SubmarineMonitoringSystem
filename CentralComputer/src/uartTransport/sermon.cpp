/*
 * sermon - a tiny serial monitor built on SerialPort (serial.hpp/.cpp)
 *
 * Type a line and press Enter to send it.
 * Anything the board sends is printed as hex plus ASCII.
 * Ctrl-C to quit.
 *
 *   ./sermon /dev/ttyACM0
 *
 * Always 115200 8N1 - that is the only rate this project uses.
 *
 * Purpose: prove the raw byte layer works before any protocol exists.
 */
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <csignal>
#include <memory>
#include <poll.h>
#include <unistd.h>

#include "serial.h"

static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void hexdump(const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i += 16)
    {
        size_t line = (n - i < 16) ? (n - i) : 16;

        std::printf("RX %04zx  ", i);
        for (size_t j = 0; j < 16; j++)
        {
            if (j < line)
                std::printf("%02X ", b[i + j]);
            else
                std::printf("   ");
        }
        std::printf(" |");
        for (size_t j = 0; j < line; j++)
        {
            uint8_t c = b[i + j];
            std::putchar((c >= 32 && c < 127) ? static_cast<int>(c) : '.');
        }
        std::printf("|\n");
    }
    std::fflush(stdout);
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/dev/ttyACM0";
    struct pollfd fds[2];

    std::unique_ptr<SerialPort> port;
    try
    {
        port = std::make_unique<SerialPort>(path);
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }

    std::signal(SIGINT, on_sigint);
    std::printf("open %s @ 115200 8N1. type a line + Enter to send, Ctrl-C to quit.\n",
                path);

    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    while (!g_stop)
    {
        /*
         * The fd is currently dead (a previous reconnect attempt failed,
         * or the port only just came back). poll() ignores a negative
         * fd and will never report an error on it again, so waiting for
         * POLLERR here would wait forever. Retry on a timer instead.
         */
        if (port->fd() < 0)
        {
            try
            {
                port->reconnect();
                std::printf("-- reopened --\n");
            }
            catch (const std::exception &)
            {
                usleep(500 * 1000); /* retry twice a second, not a busy spin */
                continue;
            }
        }

        fds[1].fd = port->fd();
        fds[1].events = POLLIN;

        int r = poll(fds, 2, 200);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            std::perror("poll");
            break;
        }

        /* The board vanished - usually because CubeIDE is flashing it. */
        if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            std::printf("-- port lost, reopening in 1s --\n");
            sleep(1);
            try
            {
                port->reconnect();
                std::printf("-- reopened --\n");
            }
            catch (const std::exception &e)
            {
                std::fprintf(stderr, "reopen: %s\n", e.what());
            }
            continue;
        }

        if (fds[0].revents & POLLIN)
        {
            char line[256];
            if (fgets(line, sizeof(line), stdin) == nullptr)
                break;
            size_t n = std::strlen(line);
            try
            {
                port->write(reinterpret_cast<const uint8_t *>(line), n);
                std::printf("TX %zu bytes\n", n);
            }
            catch (const std::exception &e)
            {
                std::fprintf(stderr, "write: %s\n", e.what());
            }
        }

        if (fds[1].revents & POLLIN)
        {
            uint8_t buf[256];
            try
            {
                long n = port->read(buf, sizeof(buf));
                if (n > 0)
                {
                    hexdump(buf, static_cast<size_t>(n));
                }
                /* n == 0 is just a timeout. Nothing to do. */
            }
            catch (const std::exception &e)
            {
                std::fprintf(stderr, "read: %s\n", e.what());
                sleep(1);
                try
                {
                    port->reconnect();
                }
                catch (const std::exception &)
                {
                }
            }
        }
    }

    port.reset(); /* explicit, but optional - runs ~SerialPort() early */
    std::printf("\nclosed\n");
    return 0;
}
