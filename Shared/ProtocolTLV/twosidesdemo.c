/*
 * demo_two_sides.c
 *
 * The whole flow, both sides, in one small file.
 *
 *   lnc_send_keepalive()  - what the firmware does
 *   cc_on_bytes()         - what the Central Computer does
 *
 * A fake "wire" array stands in for the UART so it runs on a laptop
 * with no board attached. Replace the two marked lines with a real
 * write() and read() and this is the actual code.
 *
 * Build and run:  make sides
 */

#include <stdio.h>
#include "Tlv.h"
#include "TlvNames.h"

/* =============================================================
 * THE FAKE WIRE
 * Stands in for the UART cable. Nothing clever.
 * ============================================================= */

static uint8_t g_wire[512];
static size_t  g_wire_len;

/* =============================================================
 * THE LNC SIDE  (this runs on the STM32)
 * ============================================================= */

static void lnc_send_keepalive(uint32_t time, int16_t temp, uint16_t hum,
                               uint16_t light, uint16_t batt, uint8_t mode)
{
    uint8_t      payload[TLV_MAX_VALUE];
    uint8_t      frame[TLV_MAX_FRAME];
    tlv_writer_t w;
    size_t       frame_len = 0;

    /* --- step 1: chop the numbers into bytes --- */
    tlv_writer_init(&w, payload, sizeof(payload));
    tlv_writer_put_u32(&w, time);
    tlv_writer_put_i16(&w, temp);
    tlv_writer_put_u16(&w, hum);
    tlv_writer_put_u16(&w, light);
    tlv_writer_put_u16(&w, batt);
    tlv_writer_put_u8 (&w, mode);

    /* --- step 2: did it fit? if not, drop it --- */
    if (!tlv_writer_ok(&w)) {
        return;                    /* never send half a message */
    }

    /* --- step 3: wrap it in the envelope --- */
    if (tlv_encode(TLV_TAG_KEEP_ALIVE, payload, (uint8_t)w.len,
                   frame, sizeof(frame), &frame_len) != TLV_OK) {
        return;
    }

    /* --- step 4: send it ---
     * ON THE REAL BOARD this line becomes:
     *     HAL_UART_Transmit(&huart2, frame, frame_len, 100);
     */
    {
        size_t i;
        for (i = 0; i < frame_len; i++) {
            g_wire[g_wire_len++] = frame[i];
        }
    }

    printf("LNC: sent %s, %zu bytes on the wire\n",
           tlv_tag_name(TLV_TAG_KEEP_ALIVE), frame_len);
}

/* =============================================================
 * THE CENTRAL COMPUTER SIDE  (this runs on Linux)
 * ============================================================= */

/* One of these lives for the whole program, next to the serial port. */
static tlv_rx_t g_rx;

/* Called once per complete, CRC-checked message. THIS is the decode
 * you were looking for: turning the value bytes back into numbers. */
static void cc_handle_message(const tlv_frame_t *f)
{
    tlv_reader_t r;

    printf("CC : got %s (tag 0x%02X), %u value bytes\n",
           tlv_tag_name(f->tag), f->tag, f->len);

    switch (f->tag) {

    case TLV_TAG_KEEP_ALIVE: {
        uint32_t time;
        int16_t  temp;
        uint16_t hum, light, batt;
        uint8_t  mode;

        tlv_reader_init(&r, f->value, f->len);
        time  = tlv_reader_get_u32(&r);     /* same order as the writer */
        temp  = tlv_reader_get_i16(&r);
        hum   = tlv_reader_get_u16(&r);
        light = tlv_reader_get_u16(&r);
        batt  = tlv_reader_get_u16(&r);
        mode  = tlv_reader_get_u8 (&r);

        if (!tlv_reader_done(&r)) {
            printf("CC : payload was the wrong size - ignoring\n");
            return;
        }

        printf("CC :   time %u  temp %.2f C  hum %.2f%%  light %u  "
               "batt %.3f V  mode %u\n",
               time, temp / 100.0, hum / 100.0, light, batt / 1000.0, mode);
        break;
    }

    case TLV_TAG_OBJECT_DETECTED:
        tlv_reader_init(&r, f->value, f->len);
        printf("CC :   object detected at %u\n", tlv_reader_get_u32(&r));
        break;

    default:
        printf("CC :   no handler for this tag yet\n");
        break;
    }
}

/* Called every time read() returns some bytes. */
static void cc_on_bytes(const uint8_t *buf, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        tlv_frame_t  f;
        tlv_status_t st = tlv_rx_feed_byte(&g_rx, buf[i], &f);

        if (st == TLV_OK) {
            cc_handle_message(&f);       /* use it NOW, before the next byte */
        } else if (st == TLV_ERR_CRC) {
            printf("CC : a message arrived damaged - dropped\n");
        }
        /* TLV_INCOMPLETE means "keep going", nothing to do */
    }
}

/* =============================================================
 * TIE IT TOGETHER
 * ============================================================= */

int main(void)
{
    size_t pos = 0;

    tlv_rx_init(&g_rx);

    printf("--- the LNC sends two messages ---\n");
    lnc_send_keepalive(1756400000u, 2140, 4820, 712, 3300, 0);
    lnc_send_keepalive(1756400006u, -1250, 3900, 15, 3120, 1);

    printf("\n--- the CC reads them, 5 bytes at a time ---\n");
    printf("(a real read() also returns odd little chunks like this)\n\n");

    /* ON THE REAL PROGRAM this loop becomes:
     *     while ((n = read(fd, buf, sizeof(buf))) > 0)
     *         cc_on_bytes(buf, n);
     */
    while (pos < g_wire_len) {
        size_t chunk = (g_wire_len - pos < 5) ? (g_wire_len - pos) : 5;
        cc_on_bytes(g_wire + pos, chunk);
        pos += chunk;
    }

    printf("\ncounters: ok=%u crc_errors=%u dropped=%u\n",
           (unsigned)g_rx.frames_ok, (unsigned)g_rx.crc_errors,
           (unsigned)g_rx.bytes_dropped);
    return 0;
}