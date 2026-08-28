/*
 * cpp_link_check.cpp
 *
 * The Central Computer and the Ground Station are C++. This file
 * proves that the C header includes cleanly from C++ and that the
 * linker finds the symbols, i.e. that the extern "C" guard works.
 *
 * If someone later drops a C++ keyword or a C99-only construct into
 * tlv.h, this file stops compiling and the build fails.
 */

#include <cstdio>
#include <vector>
#include <cstring>

#include "Tlv.h"
#include "TlvNames.h"

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

    tlv_rx_t rx;
    tlv_rx_init(&rx);
    tlv_rx_feed(&rx, frame, n, on_frame, nullptr);

    if (g_seen_tags.size() != 1 || g_seen_tags[0] != TLV_TAG_ACK)
    {
        std::printf("C++ side did not receive the frame\n");
        return 1;
    }

    std::printf("C++ build ok, received %s\n", tlv_tag_name(g_seen_tags[0]));
    return 0;
}