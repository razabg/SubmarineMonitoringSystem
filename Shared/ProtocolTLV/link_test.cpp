/*
 * link_test.cpp
 *
 * Proves tlv.h includes cleanly into C++ and that the extern "C"
 * guard lets the linker find plain-C symbols from a C++ program.
 * The Central Computer and Ground Station are both C++, so this
 * is what their build actually does.
 */

#include <cstdio>
#include <vector>

#include "tlv.h"
#include "tlv_names.h"

namespace
{

    std::vector<uint8_t> g_seen_tags;

    void on_frame(const tlv_frame_t *f, void *)
    {
        g_seen_tags.push_back(f->tag);
    }

} // namespace

int main()
{
    uint8_t frame[TLV_MAX_FRAME];
    size_t n = 0;
    uint8_t payload[2] = {0x01, 0x02};

    if (tlv_encode(TLV_TAG_ACK, payload, 2, frame, sizeof(frame), &n) != TLV_OK)
    {
        std::printf("encode failed\n");
        return 1;
    }

    tlv_receiver_t rx;
    tlv_receiver_init(&rx);
    tlv_receiver_feed(&rx, frame, n, on_frame, nullptr);

    if (g_seen_tags.size() != 1 || g_seen_tags[0] != TLV_TAG_ACK)
    {
        std::printf("C++ side did not receive the frame\n");
        return 1;
    }

    std::printf("C++ build ok, received %s\n", tlv_tag_name(g_seen_tags[0]));
    return 0;
}