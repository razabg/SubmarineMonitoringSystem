#include "serial.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define SERIAL_PATH_MAX 128

/*
 * The real definition of the opaque type. Only this file may see these
 * fields - everything outside includes serial.h, which only declares
 * "struct serial" by name, never its contents.
 */
struct serial
{
    int fd;                     /* -1 when closed              */
    struct termios saved;       /* settings to restore on close */
    char path[SERIAL_PATH_MAX]; /* kept so we can reopen        */
};

int serial_fd(const serial_t *s)
{
    return (s != NULL) ? s->fd : -1;
}

/* ---- small helpers used only inside this file ---- */

/*
 * Open the device file and hand back a plain fd, with no termios
 * configuration applied yet. Does the open()+fcntl() dance only.
 */
static int open_fd(const char *path)
{
    int fd, flags;

    /*
     * O_RDWR     : read and write.
     * O_NOCTTY   : this port must not become our controlling terminal,
     *              otherwise a break on the line could kill the process.
     * O_NONBLOCK : open() on a tty can block waiting for carrier detect.
     *              We open non-blocking, then clear the flag right below,
     *              once open() has already returned.
     */
    fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
    {
        return -1;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0)
    {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }

    return fd;
}

/*
 * Apply raw-mode, 115200 8N1 settings to an already-open fd.
 *
 * *previous_out receives whatever settings the port had before we
 * touched it (used later to restore on close).
 * *applied_out receives the settings we actually asked for, so the
 * caller can verify they really took effect.
 */
static int configure_port(int fd, struct termios *previous_out,
                          struct termios *applied_out)
{
    struct termios tio;

    if (tcgetattr(fd, previous_out) != 0)
    {
        return -1;
    }

    tio = *previous_out;

    /*
     * Raw mode. Without this the port is in canonical mode, which is meant
     * for a human at a keyboard: it waits for Enter, echoes what you send
     * back at you, and rewrites CR and LF. All three corrupt binary data.
     */
    cfmakeraw(&tio);

    cfsetispeed(&tio, SERIAL_BAUD);
    cfsetospeed(&tio, SERIAL_BAUD);

    tio.c_cflag |= CLOCAL; /* ignore modem control lines (DCD, DSR) */
    tio.c_cflag |= CREAD;  /* enable the receiver                   */
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;     /* 8 data bits                           */
    tio.c_cflag &= ~CSTOPB; /* 1 stop bit                            */
    tio.c_cflag &= ~PARENB; /* no parity - the CRC does that job     */
#ifdef CRTSCTS
    tio.c_cflag &= ~CRTSCTS; /* no hardware flow control              */
#endif

    /*
     * VMIN = 0, VTIME = 1 means: return as soon as any byte is there,
     * or after 0.1 s with nothing. So read() never blocks forever and
     * a return value of 0 means "timeout", not "end of file".
     */
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tio) != 0)
    {
        return -1;
    }

    *applied_out = tio;
    return 0;
}

/*
 * tcsetattr() reports success if it applied ANY of the requested changes,
 * not all of them. Read the settings back and compare against what
 * configure_port() asked for, so a partial or silently-ignored change
 * is caught here instead of showing up later as garbage on the wire.
 */
static int verify_port(int fd, const struct termios *expected)
{
    struct termios check;

    if (tcgetattr(fd, &check) != 0)
    {
        return -1;
    }

    if (check.c_cflag != expected->c_cflag ||
        cfgetispeed(&check) != SERIAL_BAUD ||
        cfgetospeed(&check) != SERIAL_BAUD)
    {
        errno = EIO;
        return -1;
    }

    return 0;
}

/* ---- public API ---- */

int serial_reconnect(serial_t *s)
{
    struct termios applied;
    int fd;

    if (s == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    if (s->fd >= 0)
    {
        close(s->fd);
        s->fd = -1;
    }

    fd = open_fd(s->path);
    if (fd < 0)
    {
        return -1;
    }

    if (configure_port(fd, &s->saved, &applied) != 0 ||
        verify_port(fd, &applied) != 0)
    {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }

    /* Drop anything the board sent while we were not listening. */
    tcflush(fd, TCIOFLUSH);

    s->fd = fd;
    return 0;
}

serial_t *serial_create(const char *path)
{
    serial_t *s;

    if (path == NULL)
    {
        errno = EINVAL;
        return NULL;
    }

    s = malloc(sizeof(*s));
    if (s == NULL)
    {
        return NULL; /* malloc already set errno (ENOMEM) */
    }

    memset(s, 0, sizeof(*s));
    s->fd = -1;
    snprintf(s->path, sizeof(s->path), "%s", path);

    if (serial_reconnect(s) != 0)
    {
        int saved_errno = errno;
        free(s);
        errno = saved_errno;
        return NULL;
    }

    return s;
}

void serial_destroy(serial_t *s)
{
    if (s == NULL)
    {
        return;
    }

    if (s->fd >= 0)
    {
        /* Put the port back the way we found it, then close. */
        tcsetattr(s->fd, TCSANOW, &s->saved);
        close(s->fd);
    }

    free(s);
}

long serial_read(serial_t *s, uint8_t *buf, size_t len)
{
    if (s == NULL || s->fd < 0 || buf == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    for (;;)
    {
        ssize_t n = read(s->fd, buf, len);
        if (n >= 0)
        {
            return (long)n;
        }
        if (errno == EINTR)
        {
            continue; /* a signal arrived, not a real error */
        }
        return -1;
    }
}

long serial_write(serial_t *s, const uint8_t *buf, size_t len)
{
    size_t sent = 0;

    if (s == NULL || s->fd < 0 || buf == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    /* write() may accept fewer bytes than asked. Loop until all are gone. */
    while (sent < len)
    {
        ssize_t n = write(s->fd, buf + sent, len - sent);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        sent += (size_t)n;
    }

    return (long)sent;
}

int serial_flush(serial_t *s)
{
    if (s == NULL || s->fd < 0)
    {
        errno = EINVAL;
        return -1;
    }
    return tcflush(s->fd, TCIOFLUSH);
}

int serial_drain(serial_t *s)
{
    if (s == NULL || s->fd < 0)
    {
        errno = EINVAL;
        return -1;
    }
    return tcdrain(s->fd);
}
