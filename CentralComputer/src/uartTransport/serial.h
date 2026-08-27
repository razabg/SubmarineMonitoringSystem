/*
 * serial.h - raw byte transport over a POSIX serial port.
 *
 * This is an ADT (abstract data type): callers only ever hold a
 * serial_t *. The struct's actual fields are defined in serial.c and
 * are not visible here - callers may not read or write them directly,
 * only call these functions.
 *
 * This layer knows about termios and file descriptors.
 * It knows NOTHING about TLV, messages, or commands.
 * Nothing above this file should ever include <termios.h>.
 */
#ifndef SERIAL_H
#define SERIAL_H

#include <stddef.h>
#include <stdint.h>

/*
 * Fixed at 115200 for this project. The Nucleo's ST-LINK virtual COM port
 * and the CC's serial transport must agree on this; both sides hardcode it
 * rather than pass it around, so there is one less thing that can mismatch.
 */
#define SERIAL_BAUD B115200

/* Opaque type. Only serial.c knows what is inside this struct. */
typedef struct serial serial_t;

/*
 * Create and open a serial port at 115200 8N1.
 *
 * Returns a heap-allocated handle on success, or NULL on failure with
 * errno set. The caller owns the returned pointer and must eventually
 * pass it to serial_destroy().
 */
serial_t *serial_create(const char *path);

/*
 * Close the port, free the handle. Safe to call with NULL (does nothing).
 * After this call, the pointer is no longer valid - do not use it again.
 */
void serial_destroy(serial_t *s);

/*
 * Close the underlying fd (if open) and open it again, using the same
 * path the handle was created with. The serial_t * itself is unchanged -
 * any code already holding this pointer keeps working with no changes.
 *
 * Use this when the fd has gone bad: the board was unplugged, or
 * temporarily taken over for flashing, and /dev/ttyACM0 came back.
 *
 * Returns 0 on success, -1 on failure with errno set.
 */
int serial_reconnect(serial_t *s);

/* The raw fd, so callers can use poll()/select() on it. -1 if closed. */
int serial_fd(const serial_t *s);

/*
 * Read whatever has arrived, up to len bytes.
 *   > 0  number of bytes read
 *   = 0  nothing arrived before the read timeout (NOT end of file)
 *   < 0  error, errno set
 */
long serial_read(serial_t *s, uint8_t *buf, size_t len);

/*
 * Write all len bytes. Loops internally, because write() is allowed
 * to accept fewer bytes than you gave it.
 *   = len  success
 *   < 0    error, errno set
 */
long serial_write(serial_t *s, const uint8_t *buf, size_t len);

/* Throw away anything sitting in the kernel's input and output queues. */
int serial_flush(serial_t *s);

/* Block until every byte handed to write() has actually left the port. */
int serial_drain(serial_t *s);

#endif /* SERIAL_H */
