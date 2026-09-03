/*
 * log.h - LNC Log module
 *
 * Section 2.4: builds one log line per Monitor round (timestamp +
 * measurement data + mode) and appends it to a daily file on the SD
 * card. One file per weekday, Sunday-first (LOG1.TXT..LOG7.TXT) --
 * reusing the same slot every 7 days is what gives "keep 7 days,
 * delete the oldest on day 8" for free, since starting today's slot
 * fresh IS the deletion once a slot has been used before. See log.c
 * for the day-rollover mechanics.
 *
 * ADT, matching monitor.c's/event.c's shape: opaque handle, one
 * static instance, create()/destroy().
 */
#ifndef LOG_H
#define LOG_H

#include "monitor.h"

typedef struct Log Log;
/* Opaque handle -- fields live only in log.c. */

/* Call once from main(), before Monitor's task starts running (it
 * calls log_write() every round). Returns the handle -- never NULL in
 * practice, kept for ADT consistency with the other modules. */
Log *log_create(void);

/* Provided for ADT completeness; not expected to be called in practice
 * on real hardware. Null-safe. */
void log_destroy(Log *l);

/* Monitor -> Log: one round's measurement + mode. Signature matches
 * the weak stub already declared in monitor.c exactly, so this strong
 * definition overrides it at link time -- monitor.c and its call site
 * don't change. */
void log_write(const monitor_measurement_t *data, monitor_mode_t mode);

#endif /* LOG_H */
