/*
 * test_crc16.c — host unit tests for CRC-16/CCITT-FALSE (REQ-PR-002).
 *
 * All expected values are derived from the algorithm definition / catalog,
 * never captured from this implementation's output (G-9):
 *   width=16 poly=0x1021 init=0xFFFF refin=false refout=false xorout=0x0000
 *
 * Vectors
 *   - "123456789"  -> 0x29B1   (the published CCITT-FALSE catalog check value)
 *   - empty input  -> 0xFFFF   (no bytes processed => the register is still init)
 *   - single 0x00  -> 0xE1F0   (hand-derived below)
 *   - residue      -> CRC(payload || CRC_bigendian) == 0x0000
 *
 * Hand derivation of CRC(0x00):
 *   crc = 0xFFFF; process 0x00: crc ^= 0x00<<8  => crc = 0xFFFF
 *   then 8 shift/xor steps (xor 0x1021 whenever bit15 was set):
 *     FFFF->EFDF->CF9F->8F1F->0E1F->1C3E->387C->70F8->E1F0
 *   => 0xE1F0.
 */
#include <stdio.h>
#include <stdint.h>
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

int main(void)
{
    /* 1. Catalog check value. */
    {
        const uint8_t msg[9] = { '1','2','3','4','5','6','7','8','9' };
        CHECK(crc16_ccitt_false(msg, sizeof msg) == 0x29B1u);
    }

    /* 2. Empty input => init value, no bits processed. */
    {
        const uint8_t dummy = 0u;
        CHECK(crc16_ccitt_false(&dummy, 0u) == 0xFFFFu);
        CHECK(crc16_ccitt_false(NULL, 0u)   == 0xFFFFu);
    }

    /* 3. Single zero byte => 0xE1F0 (hand-derived above). */
    {
        const uint8_t z = 0x00u;
        CHECK(crc16_ccitt_false(&z, 1u) == 0xE1F0u);
    }

    /* 4. Residue property: appending the CRC big-endian makes the CRC of the
     *    whole (payload + CRC) zero. This is exactly why the frame appends the
     *    CRC high byte first (docs/02_architecture_SWE2/architecture.md). */
    {
        uint8_t buf[18];
        uint16_t crc;
        size_t i;
        for (i = 0u; i < 16u; i++) {
            buf[i] = (uint8_t)(i * 17u + 3u);   /* arbitrary but fixed payload */
        }
        crc = crc16_ccitt_false(buf, 16u);
        buf[16] = (uint8_t)((crc >> 8) & 0xFFu);  /* big-endian hi */
        buf[17] = (uint8_t)(crc & 0xFFu);         /* big-endian lo */
        CHECK(crc16_ccitt_false(buf, 18u) == 0x0000u);
    }

    printf("\ntest_crc16: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
