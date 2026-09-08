/*
 * dca_test.cpp - manual smoke test for DataCollectionAnalysis. Not a
 * permanent part of the build -- proves record_measurement()/
 * record_event() directly, and on_frame()'s decoding of synthetic
 * frames built from the exact same mode_change_payload_t/
 * keepalive_payload_t types on_frame() itself decodes (declared in
 * data_collection_analysis.h) -- so this test can never silently
 * drift out of sync with the real decode logic.
 */
#include "data_collection_analysis.h"

#include <cstring>
#include <iostream>

int main()
{
    try {
        DataCollectionAnalysis dca("test_output/central_computer.db", /*retention_days=*/7);

        /* Still exercise the direct API too. */
        dca.record_measurement("2026-09-08 10:00:00", 24, 55, 60, 80, 0);
        dca.record_event("2026-09-08 10:00:05", "object_detected", "");

        /* Synthetic MODE_CHANGE frame: Normal(0) -> Warning(1). */
        mode_change_payload_t mc{};
        mc.old_mode = 0;
        mc.new_mode = 1;
        mc.temp_c = 27;
        mc.humidity_pct = 38;
        mc.light_pct = 45;
        mc.battery_pct = 76;
        tlv_frame_t mc_frame{TLV_TAG_MODE_CHANGE, sizeof(mc), reinterpret_cast<const uint8_t *>(&mc)};
        dca.on_frame(mc_frame);

        /* Synthetic KEEP_ALIVE frame, LNC-side timestamp 2026-09-08 11:30:00. */
        keepalive_payload_t ka{};
        ka.year = 26;
        ka.month = 9;
        ka.date = 8;
        ka.hour = 11;
        ka.min = 30;
        ka.sec = 0;
        ka.temp_c = 25;
        ka.humidity_pct = 50;
        ka.light_pct = 55;
        ka.battery_pct = 82;
        ka.mode = 0;
        tlv_frame_t ka_frame{TLV_TAG_KEEP_ALIVE, sizeof(ka), reinterpret_cast<const uint8_t *>(&ka)};
        dca.on_frame(ka_frame);

        /* No-payload frames. */
        tlv_frame_t detected_frame{TLV_TAG_OBJECT_DETECTED, 0, nullptr};
        dca.on_frame(detected_frame);
        tlv_frame_t cleared_frame{TLV_TAG_OBJECT_CLEARED, 0, nullptr};
        dca.on_frame(cleared_frame);

        std::cout << "wrote rows via direct API + on_frame(), check test_output/central_computer.db\n";

        /* --- query_measurements() / query_events() --- */
        std::cout << "\n--- query_measurements(2026-09-08 00:00:00 .. 2026-09-08 23:59:59) ---\n";
        for (const auto &m : dca.query_measurements("2026-09-08 00:00:00", "2026-09-08 23:59:59")) {
            std::cout << m.timestamp << " | temp=" << m.temp_c << " hum=" << m.humidity_pct
                       << " light=" << m.light_pct << " batt=" << m.battery_pct
                       << " mode=" << m.mode << "\n";
        }

        std::cout << "\n--- query_events(2026-09-08 00:00:00 .. 2026-09-08 23:59:59) ---\n";
        for (const auto &e : dca.query_events("2026-09-08 00:00:00", "2026-09-08 23:59:59")) {
            std::cout << e.timestamp << " | " << e.type << " | " << e.description << "\n";
        }

        /* --- purge_old(): insert a deliberately ancient row, then
         * confirm it's gone but everything else survives. --- */
        dca.record_measurement("2020-01-01 00:00:00", 0, 0, 0, 0, 0);
        dca.record_event("2020-01-01 00:00:00", "object_detected", "ancient");

        std::cout << "\n--- before purge_old(): measurements matching 2000-01-01..2099-12-31 ---\n";
        auto before = dca.query_measurements("2000-01-01 00:00:00", "2099-12-31 23:59:59");
        std::cout << before.size() << " rows\n";

        dca.purge_old();

        auto after = dca.query_measurements("2000-01-01 00:00:00", "2099-12-31 23:59:59");
        std::cout << "--- after purge_old(): " << after.size() << " rows (should be "
                   << before.size() - 1 << ", the 2020 row dropped) ---\n";
        for (const auto &m : after) {
            std::cout << m.timestamp << "\n";
        }
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
