/*
 * tlv.c - Tag / Length / Value message protocol
 * See tlv.h for the frame layout and the design notes.
 */

#include "tlv.h"

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
 *
 * Needs the complete frame already in `in`. Right for a transport
 * where a whole block arrives at once (a file, a TCP message) and
 * for tests. Not for UART: read() hands back arbitrary chunks, so
 * use the streaming parser (tlv_rx_feed_byte) there instead.
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

void tlv_receiver_init(tlv_receiver_t *recv) //state machine struct
{
    if (recv == NULL) {
        return;
    }
    recv->state         = ST_SOF0;
    recv->tag           = 0u;
    recv->len           = 0u;
    recv->idx           = 0u;
    recv->crc_calc      = 0xFFFFu;
    recv->crc_rx        = 0u;
    recv->frames_ok     = 0u;
    recv->crc_errors    = 0u;
    recv->bytes_dropped = 0u;
}

tlv_status_t tlv_receiver_feed_byte(tlv_receiver_t *recv, uint8_t byte, tlv_frame_t *out)
{
    if (recv == NULL || out == NULL) {
        return TLV_ERR_NULL;
    }

    switch (recv->state) {

    case ST_SOF0:
        if (byte == (uint8_t)TLV_SOF0) {
            recv->state = ST_SOF1;
        } else {
            /* Junk between frames, or the tail of a frame we gave
             * up on. Count it and keep hunting for the sync pair. */
            recv->bytes_dropped++;
        }
        break;

    case ST_SOF1:
        if (byte == (uint8_t)TLV_SOF1) {
            recv->crc_calc = 0xFFFFu;
            recv->state    = ST_TAG;
        } else if (byte == (uint8_t)TLV_SOF0) {
            /* A5 A5 5A is a valid start. Stay here rather than
             * going back to ST_SOF0, or we would miss it. */
            recv->bytes_dropped++;
        } else {
            recv->bytes_dropped += 2u;   /* the A5 and this byte */
            recv->state = ST_SOF0;
        }
        break;

    case ST_TAG:
        recv->tag      = byte;
        recv->crc_calc = tlv_crc16_update(recv->crc_calc, byte);
        recv->state    = ST_LEN;
        break;

    case ST_LEN:
        recv->len      = byte;
        recv->idx      = 0u;
        recv->crc_calc = tlv_crc16_update(recv->crc_calc, byte);
        recv->state    = (byte == 0u) ? (uint8_t)ST_CRC_HI : (uint8_t)ST_VALUE;
        break;

    case ST_VALUE:
        recv->value[recv->idx] = byte;
        recv->crc_calc       = tlv_crc16_update(recv->crc_calc, byte);
        recv->idx++;
        if (recv->idx >= recv->len) {
            recv->state = ST_CRC_HI;
        }
        break;

    case ST_CRC_HI:
        recv->crc_rx = (uint16_t)((uint16_t)byte << 8);
        recv->state  = ST_CRC_LO;
        break;

    case ST_CRC_LO:
        recv->crc_rx = (uint16_t)(recv->crc_rx | (uint16_t)byte);
        recv->state  = ST_SOF0;

        if (recv->crc_rx != recv->crc_calc) {
            recv->crc_errors++;
            return TLV_ERR_CRC;
        }

        out->tag   = recv->tag;
        out->len   = recv->len;
        out->value = (recv->len > 0u) ? recv->value : NULL; // equals the uint8_t value[TLV_MAX_VALUE] of tlv_receiver_t
        recv->frames_ok++;
        return TLV_OK;

    default:
        /* Cannot happen. Reset rather than sit in a bad state. */
        tlv_receiver_init(recv);
        break;
    }

    return TLV_INCOMPLETE;
}

size_t tlv_receiver_feed(tlv_receiver_t      *recv,
                   const uint8_t *data,
                   size_t         len,
                   tlv_frame_cb   cb,
                   void          *ctx)
{
    size_t      i;
    size_t      delivered = 0u;
    tlv_frame_t frame;

    if (recv == NULL || data == NULL) {
        return 0u;
    }

    for (i = 0; i < len; i++) {
        if (tlv_receiver_feed_byte(recv, data[i], &frame) == TLV_OK) {
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

void tlv_writer_init(tlv_writer_t *writer, uint8_t *buf, uint16_t cap)
{
    if (writer == NULL) {
        return;
    }
    writer->buf = buf;
    writer->cap = cap;
    writer->len = 0u;
    writer->ok  = (buf != NULL) ? 1 : 0;
}

static void writer_put_byte(tlv_writer_t *writer, uint8_t byte)
{
    if (!writer->ok) {
        return;
    }
    if (writer->len >= writer->cap) {
        writer->ok = 0;            /* sticky: every later write is a no-op */
        return;
    }
    writer->buf[writer->len] = byte;
    writer->len++;
}

void tlv_writer_put_u8(tlv_writer_t *writer, uint8_t value)
{
    if (writer == NULL) { return; }
    writer_put_byte(writer, value);
}

void tlv_writer_put_u16(tlv_writer_t *writer, uint16_t value)
{
    if (writer == NULL) { return; }
    writer_put_byte(writer, (uint8_t)(value >> 8));
    writer_put_byte(writer, (uint8_t)(value & 0xFFu));
}

void tlv_writer_put_u32(tlv_writer_t *writer, uint32_t value)
{
    if (writer == NULL) { return; }
    writer_put_byte(writer, (uint8_t)(value >> 24));
    writer_put_byte(writer, (uint8_t)(value >> 16));
    writer_put_byte(writer, (uint8_t)(value >> 8));
    writer_put_byte(writer, (uint8_t)(value & 0xFFu));
}

/* Signed values are cast to unsigned before shifting. Shifting a
 * negative number is undefined behaviour in C; casting first is
 * defined, and two's complement round-trips exactly. */
void tlv_writer_put_i16(tlv_writer_t *writer, int16_t value)
{
    tlv_writer_put_u16(writer, (uint16_t)value);
}

void tlv_writer_put_i32(tlv_writer_t *writer, int32_t value)
{
    tlv_writer_put_u32(writer, (uint32_t)value);
}

void tlv_writer_put_bytes(tlv_writer_t *writer, const uint8_t *src, uint16_t n)
{
    uint16_t i;

    if (writer == NULL) { return; }
    if (src == NULL && n > 0u) {
        writer->ok = 0;
        return;
    }
    for (i = 0; i < n; i++) {
        writer_put_byte(writer, src[i]);
    }
}

int tlv_writer_ok(const tlv_writer_t *writer)
{
    return (writer != NULL && writer->ok) ? 1 : 0;
}

/* ===============================================================
 * Payload reader
 *
 * Same sticky-flag idea. Once it runs off the end it stops and
 * returns zeros, so a short or malformed payload cannot make the
 * reader touch memory it does not own.
 * =============================================================== */

void tlv_reader_init(tlv_reader_t *reader, const uint8_t *buf, uint16_t len)
{
    if (reader == NULL) {
        return;
    }
    reader->buf = buf;
    reader->len = len;
    reader->pos = 0u;
    reader->ok  = (buf != NULL || len == 0u) ? 1 : 0;
}

static uint8_t reader_get_byte(tlv_reader_t *reader)
{
    uint8_t byte;

    if (!reader->ok) {
        return 0u;
    }
    if (reader->pos >= reader->len) {
        reader->ok = 0;
        return 0u;
    }
    byte = reader->buf[reader->pos];
    reader->pos++;
    return byte;
}

uint8_t tlv_reader_get_u8(tlv_reader_t *reader)
{
    if (reader == NULL) { return 0u; }
    return reader_get_byte(reader);
}

uint16_t tlv_reader_get_u16(tlv_reader_t *reader)
{
    uint16_t hi, lo;

    if (reader == NULL) { return 0u; }
    hi = (uint16_t)reader_get_byte(reader);
    lo = (uint16_t)reader_get_byte(reader);
    return (uint16_t)((hi << 8) | lo);
}

uint32_t tlv_reader_get_u32(tlv_reader_t *reader)
{
    uint32_t byte0, byte1, byte2, byte3;

    if (reader == NULL) { return 0u; }
    byte0 = (uint32_t)reader_get_byte(reader);
    byte1 = (uint32_t)reader_get_byte(reader);
    byte2 = (uint32_t)reader_get_byte(reader);
    byte3 = (uint32_t)reader_get_byte(reader);
    return (byte0 << 24) | (byte1 << 16) | (byte2 << 8) | byte3;
}

int16_t tlv_reader_get_i16(tlv_reader_t *reader)
{
    return (int16_t)tlv_reader_get_u16(reader);
}

int32_t tlv_reader_get_i32(tlv_reader_t *reader)
{
    return (int32_t)tlv_reader_get_u32(reader);
}

void tlv_reader_get_bytes(tlv_reader_t *reader, uint8_t *dst, uint16_t n)
{
    uint16_t i;

    if (reader == NULL) { return; }
    if (dst == NULL && n > 0u) {
        reader->ok = 0;
        return;
    }
    for (i = 0; i < n; i++) {
        dst[i] = reader_get_byte(reader);
    }
}

int tlv_reader_ok(const tlv_reader_t *reader)
{
    return (reader != NULL && reader->ok) ? 1 : 0;
}

int tlv_reader_done(const tlv_reader_t *reader)
{
    return (reader != NULL && reader->ok && reader->pos == reader->len) ? 1 : 0;
}
