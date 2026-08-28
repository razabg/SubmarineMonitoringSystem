/*
 * tlv.c - Tag / Length / Value message protocol
 * See tlv.h for the frame layout and the design notes.
 */

#include "Tlv.h"

/* ===============================================================
 * CRC-16/CCITT-FALSE
 *
 * Bit-by-bit, no lookup table. A table would be 512 bytes of flash
 * for about eight times the speed. At 115200 baud the link carries
 * 11.5 kB/s and the MCU runs at 80 MHz, so the slow version costs
 * well under one percent of the CPU. Flash is the scarcer resource.
 * =============================================================== */

uint16_t tlv_crc16_update(uint16_t crc, uint8_t byte)
{
    int i;
    crc ^= (uint16_t)((uint16_t)byte << 8);
    for (i = 0; i < 8; i++) {
        if (crc & 0x8000u) {
            crc = (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u);
        } else {
            crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint16_t tlv_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    size_t   i;

    if (data == NULL) {
        return crc;
    }
    for (i = 0; i < len; i++) {
        crc = tlv_crc16_update(crc, data[i]);
    }
    return crc;
}

/* ===============================================================
 * Encode
 * =============================================================== */

tlv_status_t tlv_encode(uint8_t        tag,
                        const uint8_t *value,
                        uint8_t        value_len,
                        uint8_t       *out,
                        size_t         out_cap,
                        size_t        *out_len)
{
    size_t   need;
    size_t   i;
    uint16_t crc;

    if (out == NULL || out_len == NULL) {
        return TLV_ERR_NULL;
    }
    if (value == NULL && value_len > 0u) {
        return TLV_ERR_NULL;
    }

    need = (size_t)TLV_HEADER_SIZE + (size_t)value_len + (size_t)TLV_CRC_SIZE;
    if (out_cap < need) {
        return TLV_ERR_SPACE;
    }

    out[0] = (uint8_t)TLV_SOF0;
    out[1] = (uint8_t)TLV_SOF1;
    out[2] = tag;
    out[3] = value_len;
    for (i = 0; i < (size_t)value_len; i++) {
        out[TLV_HEADER_SIZE + i] = value[i];
    }

    /* CRC over TAG, LEN and VALUE. The sync bytes are markers, not
     * data, so protecting them adds nothing: a wrong sync pair
     * means we never started reading the frame in the first place. */
    crc = tlv_crc16(&out[2], (size_t)value_len + 2u);

    out[TLV_HEADER_SIZE + value_len]      = (uint8_t)(crc >> 8);
    out[TLV_HEADER_SIZE + value_len + 1u] = (uint8_t)(crc & 0xFFu);

    *out_len = need;
    return TLV_OK;
}

/* ===============================================================
 * Decode a whole frame sitting in memory
 * =============================================================== */

tlv_status_t tlv_decode(const uint8_t *in,
                        size_t         in_len,
                        tlv_frame_t   *frame,
                        size_t        *consumed)
{
    uint8_t  len;
    size_t   total;
    uint16_t crc_calc;
    uint16_t crc_rx;

    if (in == NULL || frame == NULL) {
        return TLV_ERR_NULL;
    }
    /* Check the sync bytes as soon as each one is available, so a
     * short buffer with a wrong first byte is reported as bad sync
     * and not as "come back later". */
    if (in_len >= 1u && in[0] != (uint8_t)TLV_SOF0) {
        return TLV_ERR_NO_SOF;
    }
    if (in_len >= 2u && in[1] != (uint8_t)TLV_SOF1) {
        return TLV_ERR_NO_SOF;
    }
    if (in_len < (size_t)TLV_MIN_FRAME) {
        return TLV_INCOMPLETE;
    }

    len   = in[3];
    total = (size_t)TLV_HEADER_SIZE + (size_t)len + (size_t)TLV_CRC_SIZE;

    if (in_len < total) {
        return TLV_INCOMPLETE;
    }

    crc_calc = tlv_crc16(&in[2], (size_t)len + 2u);
    crc_rx   = (uint16_t)(((uint16_t)in[TLV_HEADER_SIZE + len] << 8) |
                           (uint16_t)in[TLV_HEADER_SIZE + len + 1u]);

    if (crc_calc != crc_rx) {
        return TLV_ERR_CRC;
    }

    frame->tag   = in[2];
    frame->len   = len;
    /* Zero copy: point straight into the caller's buffer. */
    frame->value = (len > 0u) ? &in[TLV_HEADER_SIZE] : NULL;

    if (consumed != NULL) {
        *consumed = total;
    }
    return TLV_OK;
}

/* ===============================================================
 * Streaming parser
 *
 * A byte-at-a-time state machine. This is what real serial input
 * needs, because read() returns whatever happened to arrive: half
 * a frame, three frames, one byte.
 *
 * No byte stuffing is used. A 0xA5 inside a payload is harmless,
 * because once we are past the SOF state we are counting bytes,
 * not looking for markers.
 * =============================================================== */

enum {
    ST_SOF0 = 0,
    ST_SOF1,
    ST_TAG,
    ST_LEN,
    ST_VALUE,
    ST_CRC_HI,
    ST_CRC_LO
};

void tlv_rx_init(tlv_rx_t *rx)
{
    if (rx == NULL) {
        return;
    }
    rx->state         = ST_SOF0;
    rx->tag           = 0u;
    rx->len           = 0u;
    rx->idx           = 0u;
    rx->crc_calc      = 0xFFFFu;
    rx->crc_rx        = 0u;
    rx->frames_ok     = 0u;
    rx->crc_errors    = 0u;
    rx->bytes_dropped = 0u;
}

tlv_status_t tlv_rx_feed_byte(tlv_rx_t *rx, uint8_t byte, tlv_frame_t *out)
{
    if (rx == NULL || out == NULL) {
        return TLV_ERR_NULL;
    }

    switch (rx->state) {

    case ST_SOF0:
        if (byte == (uint8_t)TLV_SOF0) {
            rx->state = ST_SOF1;
        } else {
            /* Junk between frames, or the tail of a frame we gave
             * up on. Count it and keep hunting for the sync pair. */
            rx->bytes_dropped++;
        }
        break;

    case ST_SOF1:
        if (byte == (uint8_t)TLV_SOF1) {
            rx->crc_calc = 0xFFFFu;
            rx->state    = ST_TAG;
        } else if (byte == (uint8_t)TLV_SOF0) {
            /* A5 A5 5A is a valid start. Stay here rather than
             * going back to ST_SOF0, or we would miss it. */
            rx->bytes_dropped++;
        } else {
            rx->bytes_dropped += 2u;   /* the A5 and this byte */
            rx->state = ST_SOF0;
        }
        break;

    case ST_TAG:
        rx->tag      = byte;
        rx->crc_calc = tlv_crc16_update(rx->crc_calc, byte);
        rx->state    = ST_LEN;
        break;

    case ST_LEN:
        rx->len      = byte;
        rx->idx      = 0u;
        rx->crc_calc = tlv_crc16_update(rx->crc_calc, byte);
        rx->state    = (byte == 0u) ? (uint8_t)ST_CRC_HI : (uint8_t)ST_VALUE;
        break;

    case ST_VALUE:
        rx->value[rx->idx] = byte;
        rx->crc_calc       = tlv_crc16_update(rx->crc_calc, byte);
        rx->idx++;
        if (rx->idx >= rx->len) {
            rx->state = ST_CRC_HI;
        }
        break;

    case ST_CRC_HI:
        rx->crc_rx = (uint16_t)((uint16_t)byte << 8);
        rx->state  = ST_CRC_LO;
        break;

    case ST_CRC_LO:
        rx->crc_rx = (uint16_t)(rx->crc_rx | (uint16_t)byte);
        rx->state  = ST_SOF0;

        if (rx->crc_rx != rx->crc_calc) {
            rx->crc_errors++;
            return TLV_ERR_CRC;
        }

        out->tag   = rx->tag;
        out->len   = rx->len;
        out->value = (rx->len > 0u) ? rx->value : NULL;
        rx->frames_ok++;
        return TLV_OK;

    default:
        /* Cannot happen. Reset rather than sit in a bad state. */
        tlv_rx_init(rx);
        break;
    }

    return TLV_INCOMPLETE;
}

size_t tlv_rx_feed(tlv_rx_t      *rx,
                   const uint8_t *data,
                   size_t         len,
                   tlv_frame_cb   cb,
                   void          *ctx)
{
    size_t      i;
    size_t      delivered = 0u;
    tlv_frame_t frame;

    if (rx == NULL || data == NULL) {
        return 0u;
    }

    for (i = 0; i < len; i++) {
        if (tlv_rx_feed_byte(rx, data[i], &frame) == TLV_OK) {
            delivered++;
            if (cb != NULL) {
                cb(&frame, ctx);
            }
        }
    }
    return delivered;
}

/* ===============================================================
 * Payload writer
 *
 * Every multi-byte value goes out most significant byte first.
 * We shift by hand instead of casting a pointer, so the same code
 * produces the same bytes on the little-endian ARM and on x86,
 * and no struct padding can ever leak onto the wire.
 * =============================================================== */

void tlv_writer_init(tlv_writer_t *w, uint8_t *buf, uint16_t cap)
{
    if (w == NULL) {
        return;
    }
    w->buf = buf;
    w->cap = cap;
    w->len = 0u;
    w->ok  = (buf != NULL) ? 1 : 0;
}

static void writer_put_byte(tlv_writer_t *w, uint8_t b)
{
    if (!w->ok) {
        return;
    }
    if (w->len >= w->cap) {
        w->ok = 0;            /* sticky: every later write is a no-op */
        return;
    }
    w->buf[w->len] = b;
    w->len++;
}

void tlv_writer_put_u8(tlv_writer_t *w, uint8_t v)
{
    if (w == NULL) { return; }
    writer_put_byte(w, v);
}

void tlv_writer_put_u16(tlv_writer_t *w, uint16_t v)
{
    if (w == NULL) { return; }
    writer_put_byte(w, (uint8_t)(v >> 8));
    writer_put_byte(w, (uint8_t)(v & 0xFFu));
}

void tlv_writer_put_u32(tlv_writer_t *w, uint32_t v)
{
    if (w == NULL) { return; }
    writer_put_byte(w, (uint8_t)(v >> 24));
    writer_put_byte(w, (uint8_t)(v >> 16));
    writer_put_byte(w, (uint8_t)(v >> 8));
    writer_put_byte(w, (uint8_t)(v & 0xFFu));
}

/* Signed values are cast to unsigned before shifting. Shifting a
 * negative number is undefined behaviour in C; casting first is
 * defined, and two's complement round-trips exactly. */
void tlv_writer_put_i16(tlv_writer_t *w, int16_t v)
{
    tlv_writer_put_u16(w, (uint16_t)v);
}

void tlv_writer_put_i32(tlv_writer_t *w, int32_t v)
{
    tlv_writer_put_u32(w, (uint32_t)v);
}

void tlv_writer_put_bytes(tlv_writer_t *w, const uint8_t *src, uint16_t n)
{
    uint16_t i;

    if (w == NULL) { return; }
    if (src == NULL && n > 0u) {
        w->ok = 0;
        return;
    }
    for (i = 0; i < n; i++) {
        writer_put_byte(w, src[i]);
    }
}

int tlv_writer_ok(const tlv_writer_t *w)
{
    return (w != NULL && w->ok) ? 1 : 0;
}

/* ===============================================================
 * Payload reader
 *
 * Same sticky-flag idea. Once it runs off the end it stops and
 * returns zeros, so a short or malformed payload cannot make the
 * reader touch memory it does not own.
 * =============================================================== */

void tlv_reader_init(tlv_reader_t *r, const uint8_t *buf, uint16_t len)
{
    if (r == NULL) {
        return;
    }
    r->buf = buf;
    r->len = len;
    r->pos = 0u;
    r->ok  = (buf != NULL || len == 0u) ? 1 : 0;
}

static uint8_t reader_get_byte(tlv_reader_t *r)
{
    uint8_t b;

    if (!r->ok) {
        return 0u;
    }
    if (r->pos >= r->len) {
        r->ok = 0;
        return 0u;
    }
    b = r->buf[r->pos];
    r->pos++;
    return b;
}

uint8_t tlv_reader_get_u8(tlv_reader_t *r)
{
    if (r == NULL) { return 0u; }
    return reader_get_byte(r);
}

uint16_t tlv_reader_get_u16(tlv_reader_t *r)
{
    uint16_t hi, lo;

    if (r == NULL) { return 0u; }
    hi = (uint16_t)reader_get_byte(r);
    lo = (uint16_t)reader_get_byte(r);
    return (uint16_t)((hi << 8) | lo);
}

uint32_t tlv_reader_get_u32(tlv_reader_t *r)
{
    uint32_t b0, b1, b2, b3;

    if (r == NULL) { return 0u; }
    b0 = (uint32_t)reader_get_byte(r);
    b1 = (uint32_t)reader_get_byte(r);
    b2 = (uint32_t)reader_get_byte(r);
    b3 = (uint32_t)reader_get_byte(r);
    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

int16_t tlv_reader_get_i16(tlv_reader_t *r)
{
    return (int16_t)tlv_reader_get_u16(r);
}

int32_t tlv_reader_get_i32(tlv_reader_t *r)
{
    return (int32_t)tlv_reader_get_u32(r);
}

void tlv_reader_get_bytes(tlv_reader_t *r, uint8_t *dst, uint16_t n)
{
    uint16_t i;

    if (r == NULL) { return; }
    if (dst == NULL && n > 0u) {
        r->ok = 0;
        return;
    }
    for (i = 0; i < n; i++) {
        dst[i] = reader_get_byte(r);
    }
}

int tlv_reader_ok(const tlv_reader_t *r)
{
    return (r != NULL && r->ok) ? 1 : 0;
}

int tlv_reader_done(const tlv_reader_t *r)
{
    return (r != NULL && r->ok && r->pos == r->len) ? 1 : 0;
}
