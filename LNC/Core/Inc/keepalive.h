/*
 * keepalive.h - LNC Keep-Alive module (section 2.8)
 *
 * Every 6 s, sends a keep-alive to the Central Computer through
 * Communication: timestamp + Monitor's latest measurement + current
 * mode. Runs on its own independent schedule (osDelayUntil, no
 * hardware timer -- see CLAUDE.md section 9), not synchronized to
 * Monitor's 5 s round; just reads whatever Monitor last saw via
 * monitor_get_latest().
 *
 * ADT, matching monitor.c's/objectdetection.c's shape: opaque handle,
 * one static instance.
 */
#ifndef KEEPALIVE_H
#define KEEPALIVE_H

#include "communication.h"

typedef struct KeepAlive KeepAlive;

/* Creates Keep-Alive's task. Returns the handle, or NULL if the task
 * failed to create. */
KeepAlive *keepalive_create(Communication *comm);

/* Stops the task. Null-safe. Provided for ADT completeness; not
 * expected to be called in practice on real hardware. */
void keepalive_destroy(KeepAlive *k);

#endif /* KEEPALIVE_H */
