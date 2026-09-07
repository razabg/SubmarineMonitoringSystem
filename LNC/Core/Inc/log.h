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
#include "communication.h"
#include "tlv.h"

typedef struct Log Log;
/* Opaque handle -- fields live only in log.c. */

/* Call once from main(), before Monitor's task starts running (it
 * calls log_write() every round). `comm` is needed to reply to
 * TLV_TAG_QUERY_DATA (section 2.5's "get measurement data for a time
 * range") -- may be NULL if Communication isn't up yet; log_on_frame()
 * just won't be able to reply in that case, same as every other
 * module's comm_send() call already tolerates a NULL comm. Returns the
 * handle -- never NULL in practice, kept for ADT consistency with the
 * other modules. */
Log *log_create(Communication *comm);

/* Provided for ADT completeness; not expected to be called in practice
 * on real hardware. Null-safe. */
void log_destroy(Log *l);

/* Monitor -> Log: one round's measurement + mode. Signature matches
 * the weak stub already declared in monitor.c exactly, so this strong
 * definition overrides it at link time -- monitor.c and its call site
 * don't change. */
void log_write(const monitor_measurement_t *data, monitor_mode_t mode);

/* Communication -> Log: TLV_TAG_QUERY_DATA, "measurements in a time
 * range" (section 2.5). Searches every LOG1..7.TXT file (whichever
 * currently exist) for lines whose embedded timestamp falls in the
 * requested range, replying with one TLV_TAG_QUERY_RECORD per match
 * (the whole matching line, forwarded as-is) followed by one
 * TLV_TAG_QUERY_END. Signature matches the weak stub already declared
 * in communication.c, overriding it at link time. */
void log_on_frame(const tlv_frame_t *f);

#endif /* LOG_H */
