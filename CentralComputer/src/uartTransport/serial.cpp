#include "serial.h"

#include <cerrno>
#include <fcntl.h>
#include <system_error>
#include <unistd.h>
#include <utility>

SerialPort::SerialPort(std::string path) : path_(std::move(path))
{
    do_open();
}

SerialPort::~SerialPort()
{
    close_fd();
}

int SerialPort::fd() const noexcept
{
    return fd_;
}

int SerialPort::open_fd() const
{
    /*
     * O_RDWR     : read and write.
     * O_NOCTTY   : this port must not become our controlling terminal,
     *              otherwise a break on the line could kill the process.
     * O_NONBLOCK : open() on a tty can block waiting for carrier detect.
     *              We open non-blocking, then clear the flag right after,
     *              once open() has already returned.
     */
    int fd = ::open(path_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
    {
        throw std::system_error(errno, std::generic_category(),
                                "open: " + path_);
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0)
    {
        int saved_errno = errno;
        ::close(fd);
        throw std::system_error(saved_errno, std::generic_category(),
                                "fcntl: " + path_);
    }

    return fd;
}

void SerialPort::configure_port(int fd, struct termios &applied_out)
{
    if (tcgetattr(fd, &saved_) != 0)
    {
        throw std::system_error(errno, std::generic_category(),
                                "tcgetattr: " + path_);
    }

    struct termios tio = saved_;

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
        throw std::system_error(errno, std::generic_category(),
                                "tcsetattr: " + path_);
    }

    applied_out = tio;
}

void SerialPort::verify_port(int fd, const struct termios &applied) const
{
    struct termios check{};

    if (tcgetattr(fd, &check) != 0)
    {
        throw std::system_error(errno, std::generic_category(),
                                "tcgetattr (verify): " + path_);
    }

    if (check.c_cflag != applied.c_cflag ||
        cfgetispeed(&check) != SERIAL_BAUD ||
        cfgetospeed(&check) != SERIAL_BAUD)
    {
        throw std::system_error(EIO, std::generic_category(),
                                "settings did not take effect: " + path_);
    }
}

void SerialPort::do_open()
{
    int fd = open_fd(); /* throws on failure, nothing to clean up yet */

    struct termios applied{};
    try
    {
        configure_port(fd, applied);
        verify_port(fd, applied);
    }
    catch (...)
    {
        ::close(fd);
        throw; /* rethrow the original exception, fd_ is left untouched */
    }

    /* Drop anything the board sent while we were not listening. */
    tcflush(fd, TCIOFLUSH);

    fd_ = fd;
}

void SerialPort::close_fd() noexcept
{
    if (fd_ < 0)
    {
        return;
    }
    /* Put the port back the way we found it, then close. Destructors and
       cleanup paths must not throw, so errors here are silently ignored -
       there is nothing useful left to do with them at this point anyway. */
    tcsetattr(fd_, TCSANOW, &saved_);
    ::close(fd_);
    fd_ = -1;
}

void SerialPort::reconnect()
{
    close_fd();
    do_open(); /* fd_ stays -1 if this throws - caller can retry later */
}

long SerialPort::read(uint8_t *buf, size_t len)
{
    if (fd_ < 0)
    {
        throw std::logic_error("SerialPort::read on a closed port");
    }

    for (;;)
    {
        ssize_t n = ::read(fd_, buf, len);
        if (n >= 0)
        {
            return static_cast<long>(n);
        }
        if (errno == EINTR)
        {
            continue; /* a signal arrived, not a real error */
        }
        throw std::system_error(errno, std::generic_category(), "read");
    }
}

long SerialPort::write(const uint8_t *buf, size_t len)
{
    if (fd_ < 0)
    {
        throw std::logic_error("SerialPort::write on a closed port");
    }

    size_t sent = 0;
    /* write() may accept fewer bytes than asked. Loop until all are gone. */
    while (sent < len)
    {
        ssize_t n = ::write(fd_, buf + sent, len - sent);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            throw std::system_error(errno, std::generic_category(), "write");
        }
        sent += static_cast<size_t>(n);
    }

    return static_cast<long>(sent);
}

void SerialPort::flush()
{
    if (fd_ < 0)
    {
        throw std::logic_error("SerialPort::flush on a closed port");
    }
    if (tcflush(fd_, TCIOFLUSH) != 0)
    {
        throw std::system_error(errno, std::generic_category(), "tcflush");
    }
}

void SerialPort::drain()
{
    if (fd_ < 0)
    {
        throw std::logic_error("SerialPort::drain on a closed port");
    }
    if (tcdrain(fd_) != 0)
    {
        throw std::system_error(errno, std::generic_category(), "tcdrain");
    }
}