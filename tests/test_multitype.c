/*
 * test_multitype.c — host unit tests for the type-tagged, variable-length wire
 *                    (temperature 0x02 and current 0x03), on top of the same
 *                    HDLC framing the accel path uses.
 *
 * Proves: (1) each new message packs to the documented length, (2) a full
 * encode -> decode round-trip restores the exact bytes and reports the right
 * payload length, (3) the envelope + body accessors recover the values, and
 * (4) a corrupted CRC is still rejected. The reference CRC/stuffing are the
 * spec ones (validated independently in test_crc16 / test_framing), so nothing
 * here is taken from the implementation's own output.
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

/* Feed a whole frame buffer; return the single non-MORE result and the payload
 * length it reported (positive = a delivered frame). */
static int feed_frame(const uint8_t *frame, size_t len,
                      uint8_t *payload_out, size_t cap)
{
    sk_decoder_t d;
    int last = SK_DEC_MORE;
    sk_decoder_init(&d);
    for (size_t i = 0u; i < len; i++) {
        int r = sk_decoder_feed(&d, frame[i], payload_out, cap);
        if (r != SK_DEC_MORE) {
            last = r;
        }
    }
    return last;
}

int main(void)
{
    /* ---- 1. Temperature (type 0x02) round-trips as a 6-byte payload -------- */
    {
        uint8_t payload[SK_PAYLOAD_MAX];
        uint8_t frame[SK_FRAME_MAX];
        uint8_t got[SK_PAYLOAD_MAX];
        int plen, flen, result;
        uint8_t ver, type;
        uint16_t seq;
        int16_t temp;

        plen = sk_pack_temp(payload, sizeof payload, 0x0042u, -1725); /* -17.25 C */
        CHECK(plen == 6);

        flen = sk_frame_encode(payload, (size_t)plen, frame, sizeof frame);
        CHECK(flen > 0);

        result = feed_frame(frame, (size_t)flen, got, sizeof got);
        CHECK(result == 6);                       /* decoder reports the length  */
        CHECK(memcmp(payload, got, 6) == 0);

        CHECK(sk_envelope_unpack(got, 6u, &ver, &type, &seq) == 0);
        CHECK(ver == SK_VER);
        CHECK(type == SK_TYPE_TEMP);
        CHECK(seq == 0x0042u);
        CHECK(sk_unpack_temp(got, 6u, &temp) == 0);
        CHECK(temp == -1725);
    }

    /* ---- 2. Current (type 0x03) round-trips as an 8-byte payload ----------- */
    {
        uint8_t payload[SK_PAYLOAD_MAX];
        uint8_t frame[SK_FRAME_MAX];
        uint8_t got[SK_PAYLOAD_MAX];
        int plen, flen, result;
        uint8_t type;
        int32_t cur;

        plen = sk_pack_current(payload, sizeof payload, 0xABCDu, -125000); /* -125 A */
        CHECK(plen == 8);

        flen = sk_frame_encode(payload, (size_t)plen, frame, sizeof frame);
        CHECK(flen > 0);

        result = feed_frame(frame, (size_t)flen, got, sizeof got);
        CHECK(result == 8);
        CHECK(memcmp(payload, got, 8) == 0);

        CHECK(sk_envelope_unpack(got, 8u, NULL, &type, NULL) == 0);
        CHECK(type == SK_TYPE_CURRENT);
        CHECK(sk_unpack_current(got, 8u, &cur) == 0);
        CHECK(cur == -125000);
    }

    /* ---- 3. A corrupted temperature frame is rejected --------------------- */
    {
        uint8_t payload[SK_PAYLOAD_MAX];
        uint8_t frame[SK_FRAME_MAX];
        uint8_t got[SK_PAYLOAD_MAX];
        int plen, flen, result;

        plen = sk_pack_temp(payload, sizeof payload, 1u, 2500);   /* +25.00 C */
        flen = sk_frame_encode(payload, (size_t)plen, frame, sizeof frame);
        CHECK(flen > 2);

        frame[2] ^= 0x01u;                        /* flip a bit inside the frame */
        result = feed_frame(frame, (size_t)flen, got, sizeof got);
        CHECK(result == SK_DEC_ERR_CRC);
    }

    /* ---- 4. Accel (type 0x01) still works unchanged (16-byte payload) ------ */
    {
        uint8_t payload[SK_PAYLOAD_MAX];
        uint8_t frame[SK_FRAME_MAX];
        uint8_t got[SK_PAYLOAD_MAX];
        int plen, flen, result;
        uint8_t type;

        plen = sk_payload_pack(payload, sizeof payload, 5u, 100, -100, 1000);
        CHECK(plen == 16);
        flen = sk_frame_encode(payload, (size_t)plen, frame, sizeof frame);
        result = feed_frame(frame, (size_t)flen, got, sizeof got);
        CHECK(result == 16);
        CHECK(sk_envelope_unpack(got, 16u, NULL, &type, NULL) == 0);
        CHECK(type == SK_TYPE_ACCEL);
    }

    printf("\ntest_multitype: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
