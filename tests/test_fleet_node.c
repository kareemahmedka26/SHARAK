/*
 * test_fleet_node.c — host unit tests for the fleet simulator (REQ-SIM-001/005).
 *
 * Two independent guarantees, neither captured from the simulator's own output:
 *   1. The synthetic signal is deterministic and matches the documented values.
 *   2. The frames the simulator builds are byte-exact with the real wire format:
 *        (a) they decode cleanly through protocol.c's decoder back to the same
 *            (seq,x,y,z) — an independent code path (decode, not encode), and
 *        (b) they are byte-identical to sk_payload_pack + sk_frame_encode,
 *            proving the simulator reuses the encoder rather than rolling its own.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sharak/protocol.h>
#include "fleet_node.h"

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond) do {                                            \
        g_checks++;                                                 \
        if (!(cond)) {                                              \
            g_failed++;                                             \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                           \
    } while (0)

int main(void)
{
    /* 1. Deterministic signal. WOBBLE = {0,5,8,5,0,-5,-8,-5}; z steady +1000. */
    {
        uint16_t seq; int32_t x, y, z;

        fleet_sample(0, &seq, &x, &y, &z);
        CHECK(seq == 0 && x == 0 && y == 8 && z == 1000);

        fleet_sample(1, &seq, &x, &y, &z);
        CHECK(seq == 1 && x == 5 && y == 5 && z == 1000);

        fleet_sample(8, &seq, &x, &y, &z);          /* phase wraps the 8-table */
        CHECK(seq == 8 && x == 0 && y == 8 && z == 1000);

        fleet_sample(0x10000u, &seq, &x, &y, &z);   /* seq wraps as uint16 */
        CHECK(seq == 0);
    }

    /* 2a. Round-trip through the real decoder (independent path). */
    {
        const uint16_t seq = 0x1234u;
        const int32_t  x = -4, y = 2, z = 1000;     /* spec-chosen vector */

        uint8_t frame[SK_FRAME_MAX];
        int n = fleet_build_frame(seq, x, y, z, frame, sizeof frame);
        CHECK(n > 0);
        CHECK(frame[0] == SK_FLAG && frame[n - 1] == SK_FLAG);

        sk_decoder_t dec;
        sk_decoder_init(&dec);
        uint8_t payload[SK_PAYLOAD_LEN];
        int got = SK_DEC_MORE;
        for (int i = 0; i < n; i++) {
            got = sk_decoder_feed(&dec, frame[i], payload, sizeof payload);
            if (got == SK_DEC_FRAME) break;
        }
        CHECK(got == SK_DEC_FRAME);

        uint8_t ver = 0, type = 0; uint16_t dseq = 0; int32_t dx = 0, dy = 0, dz = 0;
        CHECK(sk_payload_unpack(payload, sizeof payload,
                                &ver, &type, &dseq, &dx, &dy, &dz) == 0);
        CHECK(ver == SK_VER && type == SK_TYPE_ACCEL);
        CHECK(dseq == seq && dx == x && dy == y && dz == z);
    }

    /* 2b. Byte-identity with the encoder: the simulator must reuse protocol.c. */
    {
        const uint16_t seq = 7; const int32_t x = -8, y = 5, z = 1000;

        uint8_t sim_frame[SK_FRAME_MAX];
        int sn = fleet_build_frame(seq, x, y, z, sim_frame, sizeof sim_frame);

        uint8_t payload[SK_PAYLOAD_LEN];
        int pl = sk_payload_pack(payload, sizeof payload, seq, x, y, z);
        uint8_t ref_frame[SK_FRAME_MAX];
        int rn = sk_frame_encode(payload, (size_t)pl, ref_frame, sizeof ref_frame);

        CHECK(sn == rn && sn > 0);
        CHECK(memcmp(sim_frame, ref_frame, (size_t)sn) == 0);
    }

    printf("\ntest_fleet_node: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
