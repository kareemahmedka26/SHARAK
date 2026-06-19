/*
 * Host-side unit tests for adxl345_decode (pure logic, no hardware).
 * Build & run:  make -C firmware/tests
 *
 * Same dependency-free style as the protocol tests: a CHECK macro that
 * counts failures and prints the first offending line.
 */
#include <stdio.h>
#include <stdint.h>
#include "adxl345.h"

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond) do {                                            \
        g_checks++;                                                 \
        if (!(cond)) {                                              \
            g_failed++;                                             \
            printf("FAIL line %d: %s\n", __LINE__, #cond);          \
        }                                                           \
    } while (0)

/* Helper to build the 6-byte raw buffer from three little-endian axes. */
static void pack(uint8_t raw[6], int16_t x, int16_t y, int16_t z)
{
    raw[0] = (uint8_t)((uint16_t)x & 0xFFu);
    raw[1] = (uint8_t)(((uint16_t)x >> 8) & 0xFFu);
    raw[2] = (uint8_t)((uint16_t)y & 0xFFu);
    raw[3] = (uint8_t)(((uint16_t)y >> 8) & 0xFFu);
    raw[4] = (uint8_t)((uint16_t)z & 0xFFu);
    raw[5] = (uint8_t)(((uint16_t)z >> 8) & 0xFFu);
}

int main(void)
{
    adxl345_accel_t a;
    uint8_t raw[6];

    /* 1. All zeros -> 0,0,0 */
    pack(raw, 0, 0, 0);
    adxl345_decode(raw, &a);
    CHECK(a.x_mg == 0);
    CHECK(a.y_mg == 0);
    CHECK(a.z_mg == 0);

    /* 2. +256 LSB on X => 256 * 125 / 32 = 1000 mg (1 g) */
    pack(raw, 256, 0, 0);
    adxl345_decode(raw, &a);
    CHECK(a.x_mg == 1000);
    CHECK(a.y_mg == 0);
    CHECK(a.z_mg == 0);

    /* 3. -1 LSB => -125/32 = -3 (integer truncation toward zero) */
    pack(raw, -1, 0, 0);
    adxl345_decode(raw, &a);
    CHECK(a.x_mg == -3);

    /* 4. Mixed: x=+256 (1000mg), y=-256 (-1000mg), z=+512 (2000mg) */
    pack(raw, 256, -256, 512);
    adxl345_decode(raw, &a);
    CHECK(a.x_mg == 1000);
    CHECK(a.y_mg == -1000);
    CHECK(a.z_mg == 2000);

    /* 5. Endianness: raw bytes {0x00,0x01} must read as +256, not +1 */
    raw[0] = 0x00; raw[1] = 0x01;
    raw[2] = raw[3] = raw[4] = raw[5] = 0x00;
    adxl345_decode(raw, &a);
    CHECK(a.x_mg == 1000);

    printf("\nadxl345: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
