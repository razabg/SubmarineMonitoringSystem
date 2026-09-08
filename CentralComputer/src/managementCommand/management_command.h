/*
 * management_command.h - Central Computer Management Command module.
 * Sends the eight SET_* threshold commands + GET_TIME (section 2.5),
 * and handles their replies (ACK/NACK/TIME_REPLY) plus the LNC's own
 * TLV_TAG_TIME_SYNC_REQUEST, which it answers with TLV_TAG_TIME_SYNC_REPLY
 * -- see on_frame()'s TLV_TAG_TIME_SYNC_REQUEST case in the .cpp for
 * the actual comm_.send() call that does this (fixes CLAUDE.md's known
 * "route_frame() silently drops TIME_SYNC_REQUEST" gap).
 *
 * Needs Communication& to both send and receive, so its constructor
 * self-registers on_frame() as the management handler.
 *
 * ACK/NACK: the LNC's config.c doesn't send either yet -- handled here
 * anyway, ready for when it does. send() itself is fire-and-forget.
 *
 * Also takes a Log& -- every command actually sent, and every reply
 * received (including the automatic TIME_SYNC_REQUEST fix above),
 * gets written there. Logging lives here rather than in each caller
 * (e.g. main.cpp's menu) so nothing that goes through this class can
 * be sent without leaving a permanent record, regardless of who calls it.
 */
#ifndef MANAGEMENT_COMMAND_H
#define MANAGEMENT_COMMAND_H

#include <cstdint>

#include "communication.h"
#include "log.h"
#include "tlv.h"

/* Wire formats, duplicated from the LNC's own (config.c, init.c) --
 * same convention as data_collection_analysis.h. Declared here, not
 * private to the .cpp, so a future test can reuse the exact types. */

struct __attribute__((packed)) temp_range_payload_t {
    int8_t min;
    int8_t max;
};

struct __attribute__((packed)) bound_payload_t {
    uint8_t min;
};

/* Same shape both ways: LNC's TIME_REPLY and our TIME_SYNC_REPLY. */
struct __attribute__((packed)) time_payload_t {
    uint8_t year; /* 0-99, offset from 2000 */
    uint8_t month;
    uint8_t date;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
    uint8_t dow; /* 1-7, Monday=1..Sunday=7 */
};

class ManagementCommand
{
public:
    /* Both held by reference, not stored -- caller keeps them alive,
     * same as Communication's own Transport&. */
    ManagementCommand(Communication &comm, Log &log);

    ManagementCommand(const ManagementCommand &) = delete;
    ManagementCommand &operator=(const ManagementCommand &) = delete;

    /* false = failed a sanity check (min<=max; 0-100 bounds), nothing
     * sent. true = handed to Communication::send() -- not a delivery
     * confirmation. */
    bool set_temp_normal(int8_t min, int8_t max);
    bool set_temp_warning(int8_t min, int8_t max);
    bool set_humidity_normal(uint8_t min);
    bool set_humidity_warning(uint8_t min);
    bool set_light_normal(uint8_t min);
    bool set_light_warning(uint8_t min);
    bool set_battery_normal(uint8_t min);
    bool set_battery_warning(uint8_t min);

    /* Sends TLV_TAG_GET_TIME; reply arrives later via on_frame(). */
    void get_time();

private:
    Communication &comm_;
    Log &log_;

    bool send_temp_range(uint8_t tag, const char *label, int8_t min, int8_t max);
    bool send_bound(uint8_t tag, const char *label, uint8_t min);

    /* Registered as Communication's management handler. */
    void on_frame(const tlv_frame_t &frame);
};

#endif /* MANAGEMENT_COMMAND_H */
