/*
 * event.h - LNC Event module
 *
 * Section 2.3: waits for events from four sources (Monitor, Object
 * Detection, Configuration, Init), timestamps each on arrival (internal
 * RTC), and reacts per the source-specific table -- LED color, alarm
 * start/stop, a timestamped line in the events file (SD/FatFS via
 * sdfatfs.h), and (Monitor/Object Detection only) a message to the
 * Central Computer.
 *
 * Only Monitor exists so far, so only event_mode_changed() is actually
 * called right now. The other three entry points are here so Object
 * Detection/Configuration/Init have something to call into once they're
 * built, same as monitor.c pre-declared log_write()/event_mode_changed()
 * before Log/Event existed.
 *
 * ADT, matching monitor.c's/communication.c's shape: opaque handle, one
 * static instance, create()/destroy().
 */
#ifndef EVENT_H
#define EVENT_H

#include <stdbool.h>
#include "monitor.h"
#include "communication.h"

typedef struct Event Event;
/* Opaque handle -- fields live only in event.c. */

/* Wires Event to Communication (may be NULL if Communication isn't up
 * yet -- comm_send() tolerates a NULL handle). Call once from main(). */
Event *event_create(Communication *comm);

/* Provided for ADT completeness; not expected to be called in practice
 * on real hardware. Null-safe. */
void event_destroy(Event *e);

/* Monitor -> Event: a mode transition. Signature matches the weak stub
 * already declared in monitor.c exactly, so this strong definition
 * overrides it at link time -- monitor.c and its call site don't change. */
void event_mode_changed(const monitor_measurement_t *data,
                         monitor_mode_t old_mode, monitor_mode_t new_mode);

/* Object Detection -> Event (module not built yet). */
void event_object_detected(void);
void event_object_cleared(void);

/* Configuration -> Event (module not built yet). Events file only, no
 * message to the Central Computer, per section 2.3. */
void event_config_changed(const char *description);

/* Init -> Event (module not built yet). Events file only, per section 2.3
 * -- Init sends its own TLV_TAG_STARTUP to the Central Computer directly,
 * that's not Event's job. */
void event_startup(bool was_watchdog_reset);

/* Called from the BUTTON_D3 EXTI path (main.c) on a debounced press. */
void event_button_pressed(void);

/* Backs the "suppress non-essential ops" / "resume full operation" column:
 * true from any -> Error until the next Error -> Warning/Normal. Nothing
 * reads this yet; here for whichever module needs to check it later. */
bool event_is_essential_only(void);

#endif /* EVENT_H */
