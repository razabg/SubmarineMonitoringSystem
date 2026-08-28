/*
 * test_tlv.c - unit tests for the TLV protocol.
 *
 * No test framework. A framework is one more thing to install on
 * every machine that builds this, and the whole harness here is
 * twenty lines. printf is fine: this file never goes near the MCU.
 *
 * Build and run:  make test
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "Tlv.h"
#include "TlvNames.h"

/* --------------------------------------------------------------- */

static int g_checks;
static int g_failures;

#define CHECK(cond, msg)                                              \
    do                                                                \
    {                                                                 \
        g_checks++;                                                   \
        if (!(cond))                                                  \
        {                                                             \
            g_failures++;                                             \
            printf("  FAIL  %s:%d  %s\n", __FILE__, __LINE__, (msg)); \
        }                                                             \
    } while (0)

#define CHECK_EQ(a, b, msg)                                   \
    do                                                        \
    {                                                         \
        long _a = (long)(a);                                  \
        long _b = (long)(b);                                  \
        g_checks++;                                           \
        if (_a != _b)                                         \
        {                                                     \
            g_failures++;                                     \
            printf("  FAIL  %s:%d  %s (got %ld, want %ld)\n", \
                   __FILE__, __LINE__, (msg), _a, _b);        \
        }                                                     \
    } while (0)

static void banner(const char *name)
{
    printf("[ %s ]\n", name);
}

/* ===============================================================
 * 1. CRC against the published check value
 * =============================================================== */

static void test_crc_known_vector(void)
{
    const uint8_t input[] = "123456789";
    uint16_t crc = tlv_crc16(input, 9);

    banner("crc known vector");
    /* CRC-16/CCITT-FALSE of "123456789" is 0x29B1. If this fails,
     * our CRC is not the standard one and no other side will
     * interoperate with us. */
    CHECK_EQ(crc, 0x29B1, "CRC-16/CCITT-FALSE check value");
}

/* ===============================================================
 * 2. Encode then decode, empty payload
 * =============================================================== */

static void test_roundtrip_empty(void)
{
    uint8_t buf[TLV_MAX_FRAME];
    size_t n = 0;
    size_t consumed = 0;
    tlv_frame_t f;
    tlv_status_t rc;

    banner("roundtrip, empty payload");

    rc = tlv_encode(TLV_TAG_GET_TIME, NULL, 0, buf, sizeof(buf), &n);
    CHECK_EQ(rc, TLV_OK, "encode");
    CHECK_EQ(n, TLV_MIN_FRAME, "smallest frame is 6 bytes");
    CHECK_EQ(buf[0], TLV_SOF0, "starts with sync byte 0");
    CHECK_EQ(buf[1], TLV_SOF1, "then sync byte 1");

    rc = tlv_decode(buf, n, &f, &consumed);
    CHECK_EQ(rc, TLV_OK, "decode");
    CHECK_EQ(f.tag, TLV_TAG_GET_TIME, "tag survived");
    CHECK_EQ(f.len, 0, "length is zero");
    CHECK(f.value == NULL, "empty payload gives NULL pointer");
    CHECK_EQ(consumed, n, "consumed the whole frame");
}

/* ===============================================================
 * 3. Encode then decode, biggest payload
 * =============================================================== */

static void test_roundtrip_max(void)
{
    uint8_t payload[TLV_MAX_VALUE];
    uint8_t buf[TLV_MAX_FRAME];
    size_t n = 0;
    tlv_frame_t f;
    tlv_status_t rc;
    int i;

    banner("roundtrip, 255 byte payload");

    for (i = 0; i < (int)TLV_MAX_VALUE; i++)
    {
        payload[i] = (uint8_t)i;
    }

    rc = tlv_encode(TLV_TAG_QUERY_RECORD, payload, TLV_MAX_VALUE,
                    buf, sizeof(buf), &n);
    CHECK_EQ(rc, TLV_OK, "encode");
    CHECK_EQ(n, TLV_MAX_FRAME, "largest frame is 261 bytes");

    rc = tlv_decode(buf, n, &f, NULL);
    CHECK_EQ(rc, TLV_OK, "decode");
    CHECK_EQ(f.len, TLV_MAX_VALUE, "length survived");
    CHECK(memcmp(f.value, payload, TLV_MAX_VALUE) == 0, "payload identical");
}

/* ===============================================================
 * 4. A payload full of SOF bytes must not confuse anything.
 *    This is the test that proves we do not need byte stuffing.
 * =============================================================== */

static void test_sof_inside_payload(void)
{
    uint8_t payload[16];
    uint8_t buf[TLV_MAX_FRAME];
    size_t n = 0;
    tlv_frame_t f;
    tlv_status_t rc;
    int i;

    banner("sync bytes inside the payload");

    /* The payload is the sync pair over and over. If the parser
     * ever looked for markers while inside a frame, this breaks it.
     * It does not, because past the header it is counting bytes.
     * That is why no byte stuffing is needed. */
    for (i = 0; i < (int)sizeof(payload); i += 2)
    {
        payload[i] = (uint8_t)TLV_SOF0;
        payload[i + 1] = (uint8_t)TLV_SOF1;
    }

    rc = tlv_encode(TLV_TAG_DATA_REPORT, payload, sizeof(payload),
                    buf, sizeof(buf), &n);
    CHECK_EQ(rc, TLV_OK, "encode");

    rc = tlv_decode(buf, n, &f, NULL);
    CHECK_EQ(rc, TLV_OK, "decode");
    CHECK(memcmp(f.value, payload, sizeof(payload)) == 0, "payload identical");
}

/* ===============================================================
 * 5. Refusing bad input instead of crashing
 * =============================================================== */

static void test_encode_rejects(void)
{
    uint8_t small[4];
    uint8_t payload[8] = {0};
    size_t n = 0;

    banner("encode refuses bad input");

    CHECK_EQ(tlv_encode(TLV_TAG_ACK, NULL, 0, NULL, 10, &n),
             TLV_ERR_NULL, "NULL output buffer");
    CHECK_EQ(tlv_encode(TLV_TAG_ACK, NULL, 4, small, sizeof(small), &n),
             TLV_ERR_NULL, "NULL value with non-zero length");
    CHECK_EQ(tlv_encode(TLV_TAG_ACK, payload, 8, small, sizeof(small), &n),
             TLV_ERR_SPACE, "output buffer too small");
}

static void test_decode_rejects(void)
{
    uint8_t buf[TLV_MAX_FRAME];
    uint8_t payload[4] = {1, 2, 3, 4};
    size_t n = 0;
    tlv_frame_t f;
    size_t i;

    banner("decode refuses bad input");

    (void)tlv_encode(TLV_TAG_DATA_REPORT, payload, 4, buf, sizeof(buf), &n);

    /* no SOF */
    buf[0] = 0x00;
    CHECK_EQ(tlv_decode(buf, n, &f, NULL), TLV_ERR_NO_SOF, "missing sync 0");
    buf[0] = (uint8_t)TLV_SOF0;

    buf[1] = 0x00;
    CHECK_EQ(tlv_decode(buf, n, &f, NULL), TLV_ERR_NO_SOF, "missing sync 1");
    buf[1] = (uint8_t)TLV_SOF1;

    /* truncated at every possible cut point */
    for (i = 0; i < n; i++)
    {
        tlv_status_t rc = tlv_decode(buf, i, &f, NULL);
        CHECK(rc == TLV_INCOMPLETE, "short buffer reports INCOMPLETE");
    }

    /* one flipped bit anywhere in the frame must be caught */
    for (i = 0; i < n; i++)
    {
        tlv_status_t rc;
        buf[i] ^= 0x01u;
        rc = tlv_decode(buf, n, &f, NULL);
        CHECK(rc != TLV_OK, "single bit flip is rejected");
        buf[i] ^= 0x01u;
    }

    /* and the good frame still decodes after all that poking */
    CHECK_EQ(tlv_decode(buf, n, &f, NULL), TLV_OK, "frame still intact");
}

/* ===============================================================
 * 6. Streaming parser, split at every possible boundary
 * =============================================================== */

typedef struct
{
    int count;
    uint8_t tag;
    uint8_t len;
    uint8_t value[TLV_MAX_VALUE];
} collector_t;

static void collect(const tlv_frame_t *f, void *ctx)
{
    collector_t *c = (collector_t *)ctx;
    c->count++;
    c->tag = f->tag;
    c->len = f->len;
    if (f->len > 0)
    {
        memcpy(c->value, f->value, f->len);
    }
}

static void test_stream_split(void)
{
    uint8_t payload[20];
    uint8_t buf[TLV_MAX_FRAME];
    size_t n = 0;
    size_t cut;
    int i;

    banner("stream split at every byte boundary");

    for (i = 0; i < 20; i++)
    {
        payload[i] = (uint8_t)(0x30 + i);
    }
    (void)tlv_encode(TLV_TAG_KEEP_ALIVE, payload, 20, buf, sizeof(buf), &n);

    for (cut = 0; cut <= n; cut++)
    {
        tlv_rx_t rx;
        collector_t c;

        memset(&c, 0, sizeof(c));
        tlv_rx_init(&rx);

        tlv_rx_feed(&rx, buf, cut, collect, &c);
        tlv_rx_feed(&rx, buf + cut, n - cut, collect, &c);

        CHECK_EQ(c.count, 1, "exactly one frame no matter where we cut");
        CHECK_EQ(c.tag, TLV_TAG_KEEP_ALIVE, "tag survived the split");
        CHECK(memcmp(c.value, payload, 20) == 0, "payload survived the split");
    }
}

/* ===============================================================
 * 7. Three frames back to back in one read()
 * =============================================================== */

static void test_stream_back_to_back(void)
{
    uint8_t stream[TLV_MAX_FRAME * 3];
    size_t total = 0;
    size_t n = 0;
    tlv_rx_t rx;
    collector_t c;
    uint8_t p1[1] = {0xAA};
    uint8_t p2[3] = {1, 2, 3};

    banner("three frames in one buffer");

    (void)tlv_encode(TLV_TAG_STARTUP, p1, 1, stream, sizeof(stream), &n);
    total += n;
    (void)tlv_encode(TLV_TAG_KEEP_ALIVE, NULL, 0,
                     stream + total, sizeof(stream) - total, &n);
    total += n;
    (void)tlv_encode(TLV_TAG_MODE_CHANGE, p2, 3,
                     stream + total, sizeof(stream) - total, &n);
    total += n;

    memset(&c, 0, sizeof(c));
    tlv_rx_init(&rx);
    CHECK_EQ(tlv_rx_feed(&rx, stream, total, collect, &c), 3, "three delivered");
    CHECK_EQ(rx.frames_ok, 3, "counter agrees");
    CHECK_EQ(rx.crc_errors, 0, "no CRC errors");
    CHECK_EQ(rx.bytes_dropped, 0, "nothing dropped");
}

/* ===============================================================
 * 8. Resync: garbage first, then a good frame.
 *    This is the case where the program starts while the LNC is
 *    already mid-message.
 * =============================================================== */

static void test_stream_resync(void)
{
    uint8_t stream[64];
    uint8_t frame[TLV_MAX_FRAME];
    size_t n = 0;
    size_t total = 0;
    tlv_rx_t rx;
    collector_t c;
    uint8_t p[2] = {0x11, 0x22};
    int i;

    banner("resync after garbage");

    /* 13 bytes of junk containing two lone 0xA5 bytes. With a
     * one-byte marker each of these would have started a fake
     * frame whose fake length swallowed the real one. */
    for (i = 0; i < 13; i++)
    {
        stream[i] = ((i == 5) || (i == 9)) ? (uint8_t)TLV_SOF0
                                           : (uint8_t)(0xFF - i);
    }
    total = 13;

    (void)tlv_encode(TLV_TAG_OBJECT_DETECTED, p, 2, frame, sizeof(frame), &n);
    memcpy(stream + total, frame, n);
    total += n;

    memset(&c, 0, sizeof(c));
    tlv_rx_init(&rx);
    tlv_rx_feed(&rx, stream, total, collect, &c);

    CHECK_EQ(c.count, 1, "the real frame still arrives");
    CHECK_EQ(c.tag, TLV_TAG_OBJECT_DETECTED, "correct tag");
    CHECK(rx.crc_errors + rx.bytes_dropped > 0, "the junk was noticed");
}

/* ===============================================================
 * 9b. A corrupted frame must not take the next one down with it.
 *     This is the noise-on-the-cable case.
 * =============================================================== */

static void test_stream_recovers_after_bad_frame(void)
{
    uint8_t stream[TLV_MAX_FRAME * 2];
    size_t total = 0;
    size_t n = 0;
    tlv_rx_t rx;
    collector_t c;
    uint8_t p1[6] = {1, 2, 3, 4, 5, 6};
    uint8_t p2[2] = {0xC0, 0xDE};

    banner("recovery after a corrupted frame");

    (void)tlv_encode(TLV_TAG_DATA_REPORT, p1, 6, stream, sizeof(stream), &n);
    stream[TLV_HEADER_SIZE + 2] ^= 0xFFu; /* smash a payload byte */
    total = n;

    (void)tlv_encode(TLV_TAG_ACK, p2, 2,
                     stream + total, sizeof(stream) - total, &n);
    total += n;

    memset(&c, 0, sizeof(c));
    tlv_rx_init(&rx);
    tlv_rx_feed(&rx, stream, total, collect, &c);

    CHECK_EQ(rx.crc_errors, 1, "the damaged frame was rejected");
    CHECK_EQ(c.count, 1, "the good frame after it still arrived");
    CHECK_EQ(c.tag, TLV_TAG_ACK, "and it is the right one");
}

/* ===============================================================
 * 9c. A5 A5 5A is a valid start. A naive hunter that resets to
 *     "looking for A5" on a mismatch would miss it.
 * =============================================================== */

static void test_stream_double_sync_byte(void)
{
    uint8_t stream[TLV_MAX_FRAME + 4];
    uint8_t frame[TLV_MAX_FRAME];
    size_t n = 0;
    tlv_rx_t rx;
    collector_t c;
    uint8_t p[1] = {0x77};

    banner("A5 A5 5A must still start a frame");

    (void)tlv_encode(TLV_TAG_QUERY_END, p, 1, frame, sizeof(frame), &n);

    stream[0] = (uint8_t)TLV_SOF0; /* an extra stray A5 in front */
    memcpy(stream + 1, frame, n);

    memset(&c, 0, sizeof(c));
    tlv_rx_init(&rx);
    tlv_rx_feed(&rx, stream, n + 1u, collect, &c);

    CHECK_EQ(c.count, 1, "frame found after the stray sync byte");
    CHECK_EQ(c.tag, TLV_TAG_QUERY_END, "correct tag");
}

/* ===============================================================
 * 9. Random bytes must never crash the parser and must never
 *    produce a frame we then mis-handle.
 * =============================================================== */

static void test_stream_fuzz(void)
{
    tlv_rx_t rx;
    int round;

    banner("random bytes, 200k of them");

    tlv_rx_init(&rx);
    srand(12345); /* fixed seed: a failure must be reproducible */

    for (round = 0; round < 200000; round++)
    {
        tlv_frame_t f;
        uint8_t b = (uint8_t)(rand() & 0xFF);
        tlv_status_t rc = tlv_rx_feed_byte(&rx, b, &f);

        if (rc == TLV_OK)
        {
            /* A random stream will hit a valid CRC roughly once in
             * 65536 frames. When it does, the frame must still be
             * self-consistent. */
            CHECK(f.len == 0 || f.value != NULL, "value pointer matches len");
        }
    }
    CHECK(1, "survived without crashing");
    printf("        (ok=%u crc_err=%u dropped=%u)\n",
           (unsigned)rx.frames_ok, (unsigned)rx.crc_errors,
           (unsigned)rx.bytes_dropped);
}

/* ===============================================================
 * 10. Writer and reader
 * =============================================================== */

static void test_writer_reader(void)
{
    uint8_t payload[TLV_MAX_VALUE];
    uint8_t frame[TLV_MAX_FRAME];
    tlv_writer_t w;
    tlv_reader_t r;
    tlv_frame_t f;
    size_t n = 0;
    uint8_t raw[3] = {0xDE, 0xAD, 0xBE};
    uint8_t raw_back[3];

    banner("writer and reader roundtrip");

    /* A plausible data report: time, temperature in hundredths of
     * a degree, humidity, light, battery, mode. */
    tlv_writer_init(&w, payload, sizeof(payload));
    tlv_writer_put_u32(&w, 1756400000u); /* unix seconds       */
    tlv_writer_put_i16(&w, -1250);       /* -12.50 C           */
    tlv_writer_put_u16(&w, 4830);        /* 48.30 %            */
    tlv_writer_put_u16(&w, 712);         /* light raw          */
    tlv_writer_put_u16(&w, 3300);        /* 3.300 V            */
    tlv_writer_put_u8(&w, 2);            /* mode = Error       */
    tlv_writer_put_bytes(&w, raw, 3);
    CHECK(tlv_writer_ok(&w), "everything fitted");
    CHECK_EQ(w.len, 4 + 2 + 2 + 2 + 2 + 1 + 3, "expected byte count");

    (void)tlv_encode(TLV_TAG_DATA_REPORT, payload, (uint8_t)w.len,
                     frame, sizeof(frame), &n);
    CHECK_EQ(tlv_decode(frame, n, &f, NULL), TLV_OK, "decode");

    tlv_reader_init(&r, f.value, f.len);
    CHECK_EQ(tlv_reader_get_u32(&r), 1756400000u, "timestamp");
    CHECK_EQ(tlv_reader_get_i16(&r), -1250, "negative temperature");
    CHECK_EQ(tlv_reader_get_u16(&r), 4830, "humidity");
    CHECK_EQ(tlv_reader_get_u16(&r), 712, "light");
    CHECK_EQ(tlv_reader_get_u16(&r), 3300, "battery");
    CHECK_EQ(tlv_reader_get_u8(&r), 2, "mode");
    tlv_reader_get_bytes(&r, raw_back, 3);
    CHECK(memcmp(raw, raw_back, 3) == 0, "raw bytes");
    CHECK(tlv_reader_done(&r), "reader consumed exactly the whole payload");
}

static void test_writer_overflow(void)
{
    uint8_t small[3];
    tlv_writer_t w;

    banner("writer stops at the end of its buffer");

    tlv_writer_init(&w, small, sizeof(small));
    tlv_writer_put_u32(&w, 0x11223344u); /* needs 4, has 3 */
    CHECK(!tlv_writer_ok(&w), "overflow reported");

    tlv_writer_put_u8(&w, 0xFF); /* must be a no-op now */
    CHECK(!tlv_writer_ok(&w), "error is sticky");
    CHECK(w.len <= sizeof(small), "never wrote past the end");
}

static void test_reader_underflow(void)
{
    uint8_t two[2] = {0x01, 0x02};
    tlv_reader_t r;

    banner("reader stops at the end of the payload");

    tlv_reader_init(&r, two, sizeof(two));
    (void)tlv_reader_get_u32(&r); /* asks for 4, has 2 */
    CHECK(!tlv_reader_ok(&r), "underflow reported");

    CHECK_EQ(tlv_reader_get_u8(&r), 0, "reads past the end return zero");
    CHECK(!tlv_reader_ok(&r), "error is sticky");
}

/* ===============================================================
 * 11. Tag names, so a typo in the table shows up here
 * =============================================================== */

static void test_tag_names(void)
{
    banner("tag names");
    CHECK(strcmp(tlv_tag_name(TLV_TAG_KEEP_ALIVE), "KEEP_ALIVE") == 0,
          "known tag");
    CHECK(strcmp(tlv_tag_name(0x99), "UNKNOWN") == 0, "unknown tag");
    CHECK(strcmp(tlv_status_name(TLV_ERR_CRC), "ERR_CRC") == 0, "status name");
}

/* =============================================================== */

int main(void)
{
    printf("TLV protocol tests\n");
    printf("------------------\n");

    test_crc_known_vector();
    test_roundtrip_empty();
    test_roundtrip_max();
    test_sof_inside_payload();
    test_encode_rejects();
    test_decode_rejects();
    test_stream_split();
    test_stream_back_to_back();
    test_stream_resync();
    test_stream_recovers_after_bad_frame();
    test_stream_double_sync_byte();
    test_stream_fuzz();
    test_writer_reader();
    test_writer_overflow();
    test_reader_underflow();
    test_tag_names();

    printf("------------------\n");
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}