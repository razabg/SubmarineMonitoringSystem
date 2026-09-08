/*
 * management_command.cpp - see management_command.h for the overview.
 */
#include "management_command.h"

#include <cstdio>

#include "time_utils.h"

ManagementCommand::ManagementCommand(Communication &comm) : comm_(comm)
{
    comm_.set_management_handler([this](const tlv_frame_t &f) { on_frame(f); });
}

bool ManagementCommand::send_temp_range(uint8_t tag, int8_t min, int8_t max)
{
    if (min > max) {
        return false;
    }
    temp_range_payload_t p{min, max};
    comm_.send(tag, reinterpret_cast<const uint8_t *>(&p), sizeof(p));
    return true;
}

bool ManagementCommand::send_bound(uint8_t tag, uint8_t min)
{
    if (min > 100) {
        return false;
    }
    bound_payload_t p{min};
    comm_.send(tag, reinterpret_cast<const uint8_t *>(&p), sizeof(p));
    return true;
}

bool ManagementCommand::set_temp_normal(int8_t min, int8_t max)
{
    return send_temp_range(TLV_TAG_SET_TEMP_NORMAL, min, max);
}

bool ManagementCommand::set_temp_warning(int8_t min, int8_t max)
{
    return send_temp_range(TLV_TAG_SET_TEMP_WARNING, min, max);
}

bool ManagementCommand::set_humidity_normal(uint8_t min)
{
    return send_bound(TLV_TAG_SET_HUM_NORMAL, min);
}

bool ManagementCommand::set_humidity_warning(uint8_t min)
{
    return send_bound(TLV_TAG_SET_HUM_WARNING, min);
}

bool ManagementCommand::set_light_normal(uint8_t min)
{
    return send_bound(TLV_TAG_SET_LIGHT_NORMAL, min);
}

bool ManagementCommand::set_light_warning(uint8_t min)
{
    return send_bound(TLV_TAG_SET_LIGHT_WARNING, min);
}

bool ManagementCommand::set_battery_normal(uint8_t min)
{
    return send_bound(TLV_TAG_SET_BATT_NORMAL, min);
}

bool ManagementCommand::set_battery_warning(uint8_t min)
{
    return send_bound(TLV_TAG_SET_BATT_WARNING, min);
}

void ManagementCommand::get_time()
{
    comm_.send(TLV_TAG_GET_TIME, nullptr, 0);
}

void ManagementCommand::on_frame(const tlv_frame_t &frame)
{
    switch (frame.tag) {
    case TLV_TAG_TIME_REPLY: {
        if (frame.value == nullptr || frame.len != sizeof(time_payload_t)) {
            return;
        }
        const auto *p = reinterpret_cast<const time_payload_t *>(frame.value);
        std::printf("MGMT: LNC reports time %s\n",
                    TimeUtils::format(p->year, p->month, p->date, p->hour, p->min, p->sec).c_str());
        break;
    }

    case TLV_TAG_ACK:
        std::printf("MGMT: ACK\n");
        break;

    case TLV_TAG_NACK:
        std::printf("MGMT: NACK\n");
        break;

    case TLV_TAG_TIME_SYNC_REQUEST: {
        /* The LNC asked at boot for the real time -- this is the reply
         * that was missing (CLAUDE.md's known gap). Sent from the CC's
         * own current time, same fields/units the LNC's time_payload_t
         * already uses for TLV_TAG_TIME_REPLY. */
        TimeUtils::CalendarFields f = TimeUtils::now_fields();
        time_payload_t reply{f.year, f.month, f.date, f.hour, f.min, f.sec, f.dow};
        comm_.send(TLV_TAG_TIME_SYNC_REPLY, reinterpret_cast<const uint8_t *>(&reply), sizeof(reply));
        std::printf("MGMT: LNC requested time sync at boot -- replied with %s\n", TimeUtils::now().c_str());
        break;
    }

    default:
        break;
    }
}
