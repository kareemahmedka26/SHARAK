/*
 * test_framing.c — host unit tests for payload pack/unpack + HDLC framing
 *                  (REQ-PR-003/004/005/006/007).
 *
 * Strategy: a tiny INDEPENDENT reference implementation of the stuffing rule
 * (stuff_frame / ref_encode below) is built straight from the spec, then the
 * production sk_frame_encode output is compared against it byte-for-byte. The
 * CRC component is validated separately in test_crc16.c against hand-derived
 * vectors, so nothing here is taken from the implementation's own output (G-9).
 *
 * Stuffing rule (spec), shown concretely:
 *   inner byte 0x7E  ->  0x7D 0x5E      (0x7E ^ 0x20 = 0x5E)
 *   inner byte 0x7D  ->  0x7D 0x5D      (0x7D ^ 0x20 = 0x5D)
 *   any other byte   ->  itself
 *   frame = 0x7E  <stuffed inner>  0x7E,  inner = payload[16] + crc_hi + crc_lo
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sharak/protocol.h>

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond) do {                                            \
        g_checks++;                                                 \
        if (!(cond)) {                                              \
            g_failed++;                                             \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                           \
    } while (0)

/* Independent reference: stuff an arbitrary inner buffer between FLAG bytes. */
static size_t stuff_frame(const uint8_t *inner, size_t ilen, uint8_t *out)
{
    size_t n = 0u;
    out[n++] = 0x7Eu;
    for (size_t i = 0u; i < ilen; i++) {
        uint8_t b = inner[i];
        if (b == 0x7Eu || b == 0x7Du) {
            out[n++] = 0x7Du;
            out[n++] = (uint8_t)(b ^ 0x20u);
        } else {
            out[n++] = b;
        }
    }
    out[n++] = 0x7Eu;
    return n;
}

/* Independent reference frame: inner = payload + CRC(big-endian), then stuff. */
static size_t ref_encode(const uint8_t payload[16], uint8_t *out)
{
    uint8_t inner[18];
    uint16_t crc;
    memcpy(inner, payload, 16);
    crc = crc16_ccitt_false(payload, 16u);
    inner[16] = (uint8_t)((crc >> 8) & 0xFFu);
    inner[17] = (uint8_t)(crc & 0xFFu);
    return stuff_frame(inner, 18u, out);
}

/* Feed a whole buffer through the decoder, counting completed frames and
 * recording the last non-MORE result and the last delivered payload. */
static int feed_all(sk_decoder_t *d, const uint8_t *buf, size_t len,
                    uint8_t payload_out[16], int *last_result)
{
    int frames = 0;
    int last = SK_DEC_MORE;
    for (size_t i = 0u; i < len; i++) {
        int r = sk_decoder_feed(d, buf[i], payload_out, 16u);
        if (r == SK_DEC_FRAME) {
            frames++;
        }
        if (r != SK_DEC_MORE) {
            last = r;
        }
    }
    if (last_result != NULL) {
        *last_result = last;
    }
    return frames;
}

int main(void)
{
    /* ---- 1. Round-trip restores the exact payload ------------------------ */
    {
        uint8_t payload[16];
        uint8_t frame[SK_FRAME_MAX];
        uint8_t got[16];
        sk_decoder_t d;
        int n, frames, last;
        uint8_t ver, type;
        uint16_t seq;
        int32_t x, y, z;

        n = sk_payload_pack(payload, sizeof payload, 0x1234u, 1000, -2000, 256);
        CHECK(n == 16);

        n = sk_frame_encode(payload, 16u, frame, sizeof frame);
        CHECK(n > 0);

        sk_decoder_init(&d);
        frames = feed_all(&d, frame, (size_t)n, got, &last);
        CHECK(frames == 1);
        CHECK(last == SK_DEC_FRAME);
        CHECK(memcmp(payload, got, 16) == 0);

        CHECK(sk_payload_unpack(got, 16u, &ver, &type, &seq, &x, &y, &z) == 0);
        CHECK(ver == SK_VER);
        CHECK(type == SK_TYPE_ACCEL);
        CHECK(seq == 0x1234u);
        CHECK(x == 1000);
        CHECK(y == -2000);
        CHECK(z == 256);
    }

    /* ---- 2. Stuffing exactness for a payload containing 0x7E and 0x7D ----- */
    {
        uint8_t payload[16];
        uint8_t frame[SK_FRAME_MAX];
        uint8_t ref[SK_FRAME_MAX];
        int n;
        size_t rn;

        /* seq = 0x7D7E -> bytes 0x7E (low), 0x7D (high); x packs 0x7D,0x7E,... */
        (void)sk_payload_pack(payload, sizeof payload,
                              0x7D7Eu, (int32_t)0x7E7D7E7D, 0, 0);

        n  = sk_frame_encode(payload, 16u, frame, sizeof frame);
        rn = ref_encode(payload, ref);
        CHECK(n > 0);
        CHECK((size_t)n == rn);
        CHECK(memcmp(frame, ref, rn) == 0);

        /* Structural guarantees: only the two delimiters are raw 0x7E, and any
         * 0x7D in the interior is a valid escape (followed by 0x5E or 0x5D). */
        CHECK(frame[0] == 0x7Eu);
        CHECK(frame[n - 1] == 0x7Eu);
        for (int i = 1; i < n - 1; i++) {
            CHECK(frame[i] != 0x7Eu);            /* no raw FLAG inside */
            if (frame[i] == 0x7Du) {
                CHECK(i + 1 < n - 1);
                CHECK(frame[i + 1] == 0x5Eu || frame[i + 1] == 0x5Du);
                i++;                              /* skip the escaped byte */
            }
        }
    }

    /* ---- 3. Corrupted CRC is rejected (no frame delivered) --------------- */
    {
        uint8_t payload[16];
        uint8_t inner[18];
        uint8_t frame[SK_FRAME_MAX];
        uint8_t got[16];
        sk_decoder_t d;
        uint16_t crc;
        size_t fn;
        int frames, last;

        (void)sk_payload_pack(payload, sizeof payload, 7u, 1, 2, 3);
        memcpy(inner, payload, 16);
        crc = crc16_ccitt_false(payload, 16u);
        crc ^= 0x0001u;                          /* deliberately wrong CRC */
        inner[16] = (uint8_t)((crc >> 8) & 0xFFu);
        inner[17] = (uint8_t)(crc & 0xFFu);
        fn = stuff_frame(inner, 18u, frame);

        sk_decoder_init(&d);
        frames = feed_all(&d, frame, fn, got, &last);
        CHECK(frames == 0);
        CHECK(last == SK_DEC_ERR_CRC);
    }

    /* ---- 4. Partial-stream feeding: arbitrary chunk boundaries still decode */
    {
        uint8_t payload[16];
        uint8_t frame[SK_FRAME_MAX];
        uint8_t got[16];
        sk_decoder_t d;
        int n, frames1, frames2, last;
        size_t split;

        (void)sk_payload_pack(payload, sizeof payload, 99u, -1, -1, -1);
        n = sk_frame_encode(payload, 16u, frame, sizeof frame);
        CHECK(n > 0);

        split = (size_t)n / 2u;                  /* cut the frame in half */
        sk_decoder_init(&d);
        frames1 = feed_all(&d, frame, split, got, &last);
        CHECK(frames1 == 0);                      /* nothing yet, mid-frame */
        frames2 = feed_all(&d, frame + split, (size_t)n - split, got, &last);
        CHECK(frames2 == 1);
        CHECK(last == SK_DEC_FRAME);
        CHECK(memcmp(payload, got, 16) == 0);
    }

    /* ---- 5. Resynchronization after garbage and after a bad frame -------- */
    {
        uint8_t payload[16];
        uint8_t frame[SK_FRAME_MAX];
        uint8_t got[16];
        sk_decoder_t d;
        int n, frames, last;
        const uint8_t garbage[5] = { 0x11u, 0x22u, 0x33u, 0xAAu, 0x55u };

        (void)sk_payload_pack(payload, sizeof payload, 0xBEEFu, 10, 20, 30);
        n = sk_frame_encode(payload, 16u, frame, sizeof frame);

        sk_decoder_init(&d);
        /* leading garbage (no FLAG) must be silently ignored */
        (void)feed_all(&d, garbage, sizeof garbage, got, &last);
        frames = feed_all(&d, frame, (size_t)n, got, &last);
        CHECK(frames == 1);
        CHECK(last == SK_DEC_FRAME);
        CHECK(memcmp(payload, got, 16) == 0);

        /* a truncated/garbled frame followed by a good one: only the good one
         * should be delivered, proving the decoder resynced on the FLAG. */
        {
            uint8_t junkframe[6] = { 0x7Eu, 0x01u, 0x02u, 0x03u, 0x04u, 0x7Eu };
            sk_decoder_init(&d);
            (void)feed_all(&d, junkframe, sizeof junkframe, got, &last);
            CHECK(last == SK_DEC_ERR_LEN);        /* too-short inner -> len error */
            frames = feed_all(&d, frame, (size_t)n, got, &last);
            CHECK(frames == 1);
            CHECK(last == SK_DEC_FRAME);
            CHECK(memcmp(payload, got, 16) == 0);
        }
    }

    printf("\ntest_framing: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
