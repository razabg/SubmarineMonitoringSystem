/*
 * sermon - a tiny serial monitor built on serial.c
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
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
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

        printf("RX %04zx  ", i);
        for (size_t j = 0; j < 16; j++)
        {
            if (j < line)
                printf("%02X ", b[i + j]);
            else
                printf("   ");
        }
        printf(" |");
        for (size_t j = 0; j < line; j++)
        {
            uint8_t c = b[i + j];
            putchar((c >= 32 && c < 127) ? (int)c : '.');
        }
        printf("|\n");
    }
    fflush(stdout);
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/dev/ttyACM0";
    serial_t *s;
    struct pollfd fds[2];

    s = serial_create(path);
    if (s == NULL)
    {
        perror(path);
        return 1;
    }

    signal(SIGINT, on_sigint);
    printf("open %s @ 115200 8N1. type a line + Enter to send, Ctrl-C to quit.\n",
           path);

    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    while (!g_stop)
    {
        int r;

        /*
         * The fd is currently dead (a previous reconnect attempt failed -
         * device not back yet, or "stm linux" not run again after replug).
         * poll() ignores a negative fd and will never report an error on
         * it again, so waiting for POLLERR here would wait forever.
         * Instead, retry on a timer until the device comes back.
         */
        if (serial_fd(s) < 0)
        {
            if (serial_reconnect(s) == 0)
            {
                printf("-- reopened --\n");
            }
            else
            {
                usleep(500 * 1000); /* retry twice a second, not a busy spin */
                continue;
            }
        }

        fds[1].fd = serial_fd(s);
        fds[1].events = POLLIN;

        r = poll(fds, 2, 200);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }

        /* The board vanished - usually because CubeIDE is flashing it. */
        if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            printf("-- port lost, reopening in 1s --\n");
            sleep(1);
            if (serial_reconnect(s) != 0)
            {
                perror("reopen");
            }
            else
            {
                printf("-- reopened --\n");
            }
            continue;
        }

        if (fds[0].revents & POLLIN)
        {
            char line[256];
            if (fgets(line, sizeof(line), stdin) == NULL)
                break;
            size_t n = strlen(line);
            if (serial_write(s, (const uint8_t *)line, n) < 0)
            {
                perror("write");
            }
            else
            {
                printf("TX %zu bytes\n", n);
            }
        }

        if (fds[1].revents & POLLIN)
        {
            uint8_t buf[256];
            long n = serial_read(s, buf, sizeof(buf));
            if (n > 0)
            {
                hexdump(buf, (size_t)n);
            }
            else if (n < 0)
            {
                perror("read");
                sleep(1);
                serial_reconnect(s);
            }
            /* n == 0 is just a timeout. Nothing to do. */
        }
    }

    serial_destroy(s);
    printf("\nclosed\n");
    return 0;
}