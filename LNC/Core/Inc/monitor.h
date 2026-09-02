/*
 * monitor.h - LNC Monitor module
 *
 * Every 5 s: samples temperature/humidity (DHT11), battery voltage and
 * light (ADC), classifies the result into a mode (section 2.10), sends
 * the measurement + mode to Log every round, and to Event only when the
 * mode actually changes. See monitor.c for the classification rule and
 * the CLAUDE.md peripheral-allocation notes for the sensor wiring.
 *
 * ADT, matching communication.c's shape: opaque handle, no malloc (one
 * static instance), create()/destroy() even though nothing outside this
 * file needs more than one instance right now -- the handle is what lets
 * monitor_task() operate through `self` instead of a bare global.
 */
#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>

typedef enum {
    MODE_NORMAL,
    MODE_WARNING,
    MODE_ERROR
} monitor_mode_t;

/* One round's readings, already in the friendly units used for both the
 * classification thresholds and whatever Log/Event do with them --
 * whole degrees/percent (DHT11's real resolution, no false precision),
 * light/battery scaled 0-100 from their raw ADC readings. */
typedef struct {
    int16_t temp_c;
    uint8_t humidity_pct;
    uint8_t light_pct;
    uint8_t battery_pct;
} monitor_measurement_t;

typedef struct Monitor Monitor;
/* Opaque handle -- fields live only in monitor.c. */

/* Creates Monitor's task. Call once from main(), after Communication is
 * up. Returns the handle, or NULL if the task failed to create. */
Monitor *monitor_create(void);

/* Stops the task. Null-safe. Provided for ADT completeness; not expected
 * to be called in practice on real hardware. */
void monitor_destroy(Monitor *m);

#endif /* MONITOR_H */