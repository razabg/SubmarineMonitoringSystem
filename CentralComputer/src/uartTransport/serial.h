/*
 * serial.hpp - raw byte transport over a POSIX serial port, C++ version.
 *
 * RAII: the constructor opens the port, the destructor always closes it,
 * whether the object goes out of scope normally or because of an
 * exception. There is no separate "destroy" call to forget.
 *
 * Failures are reported by throwing std::system_error, which carries the
 * original errno (via std::generic_category()) so no diagnostic detail
 * is lost compared to the C version's return-code + errno convention.
 *
 * This class knows about termios and file descriptors.
 * It knows NOTHING about TLV, messages, or commands.
 */
#ifndef SERIAL_HPP
#define SERIAL_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <termios.h>

/*
 * Fixed at 115200 for this project. The Nucleo's ST-LINK virtual COM port
 * and the CC's serial transport must agree on this; both sides hardcode it
 * rather than pass it around, so there is one less thing that can mismatch.
 */
#define SERIAL_BAUD B115200

class SerialPort
{
public:
    /*
     * Opens and configures the port at 115200 8N1 immediately.
     * This is the C version's serial_create() - C++ constructors cannot
     * be given a custom name, so the class name serves that role.
     * Throws std::system_error on failure - if this constructor returns
     * normally, the port is open and ready to use.
     */
    explicit SerialPort(std::string path);

    /*
     * The C version's serial_destroy(). Restores the original port
     * settings and closes. Never throws. Runs automatically when the
     * object goes out of scope - there is no call to forget.
     */
    ~SerialPort();

    /*
     * A SerialPort owns one OS file descriptor. Copying it would let two
     * objects both believe they own (and could both close) the same fd,
     * so copying is disabled. Not move-enabled either, for now - this
     * project never needs to relocate a live port, only reconnect it.
     */
    SerialPort(const SerialPort &) = delete;
    SerialPort &operator=(const SerialPort &) = delete;
    SerialPort(SerialPort &&) = delete;
    SerialPort &operator=(SerialPort &&) = delete;

    /*
     * Close the underlying fd (if open) and open it again, using the
     * same path this object was constructed with. The object itself is
     * unchanged - anything holding a reference or pointer to it keeps
     * working with no changes once this succeeds.
     *
     * Use this when the fd has gone bad: the board was unplugged, or
     * temporarily taken over for flashing, and the device path came back.
     *
     * Throws std::system_error on failure. After a failed call, fd()
     * returns -1, so callers can retry reconnect() again later.
     */
    void reconnect();

    /* The raw fd, so callers can use poll()/select() on it. -1 if closed. */
    int fd() const noexcept;

    /*
     * Read whatever has arrived, up to len bytes.
     *   > 0  number of bytes read
     *   = 0  nothing arrived before the read timeout (NOT end of file)
     * Throws std::system_error on a real error.
     */
    long read(uint8_t *buf, size_t len);

    /*
     * Write all len bytes. Loops internally, because write() is allowed
     * to accept fewer bytes than you gave it. Throws on error.
     */
    long write(const uint8_t *buf, size_t len);

    /* Throw away anything sitting in the kernel's input/output queues. */
    void flush();

    /* Block until every byte handed to write() has actually left the port. */
    void drain();

private:
    int fd_ = -1;
    struct termios saved_{};
    std::string path_;

    /* Open the device file, no termios configuration applied yet. */
    int open_fd() const;

    /*
     * Apply raw-mode 115200 8N1 settings to fd. Stores the port's
     * pre-existing settings into saved_ (used later to restore on close),
     * and writes what was actually applied into applied_out, so the
     * caller can verify it took effect.
     */
    void configure_port(int fd, struct termios &applied_out);

    /*
     * tcsetattr() reports success if it applied ANY of the requested
     * changes, not all of them. Reads settings back and compares against
     * what configure_port() asked for.
     */
    void verify_port(int fd, const struct termios &applied) const;

    /* Runs open_fd -> configure_port -> verify_port, sets fd_ on success. */
    void do_open();

    /* Restores saved_ settings (if fd_ is open) and closes. Never throws. */
    void close_fd() noexcept;
};

#endif /* SERIAL_HPP */