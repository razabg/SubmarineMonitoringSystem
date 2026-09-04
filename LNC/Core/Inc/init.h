/*
 * init.h - LNC Init module
 *
 * Section 2.7: at boot, sets the internal RTC from the external DS1307
 * (CLAUDE.md's RTC design -- DS1307 is the durable fallback, available
 * immediately, before Communication with the CC is even up), reports
 * whether the last boot was caused by a watchdog reset (section 2.9)
 * to Event, then requests a time sync from the Central Computer through
 * Communication (corrects the DS1307-sourced fallback once the CC
 * replies). Also answers the CC's own "what time do you have" query
 * (section 2.5's TLV_TAG_GET_TIME) with the LNC's current time, since
 * Init is the module that owns RTC concerns.
 *
 * "Starts all system activities" (section 2.7): init_create() is also
 * where Event, Log, Configuration, and Monitor actually get created --
 * main() only creates Init (and Communication, which is transport
 * plumbing, not a "system activity"). Event has to come first, since
 * Init's own startup report needs it to already exist; everything
 * else follows after Init's own watchdog/DS1307/CC-sync-kickoff work.
 *
 * ADT, matching monitor.c's/event.c's/log.c's shape: opaque handle,
 * one static instance, create()/destroy().
 */
#ifndef INIT_H
#define INIT_H

#include "communication.h"

typedef struct Init Init;
/* Opaque handle -- fields live only in init.c. */

/* Call once from main(), after Communication (comm may be NULL if it
 * isn't up yet -- comm_send() tolerates that, same as every other
 * module's comm use). Creates Event/Log/Configuration/Monitor
 * internally -- main() doesn't call their create() functions itself.
 * Returns the handle, or NULL if any of them (or Init's own setup)
 * failed. */
Init *init_create(Communication *comm);

/* Provided for ADT completeness; not expected to be called in practice
 * on real hardware. Null-safe. */
void init_destroy(Init *i);

#endif /* INIT_H */
