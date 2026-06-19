/*
 * test_i2c_sim.c — host unit tests for the simulated ADXL345 I2C backend.
 *
 * These verify the *contract* the real driver relies on (REQ-FW-006):
 *   - DEVID is readable and equals 0xE5
 *   - any address other than 0x53 NACKs
 *   - data registers stay frozen (zero) until POWER_CTL Measure is set
 *   - a 6-byte burst is an atomic, self-consistent snapshot
 *   - the sample advances exactly once per burst
 *   - re-init reproduces an identical byte sequence (determinism, G-9)
 *
 * Expected sample bytes are derived from the simulator's documented signal
 * model (see i2c_sim.c header): Z = +256 LSB; X/Y from the 16-entry triangle
 * {0,4,8,12,16,12,8,4,0,-4,-8,-12,-16,-12,-8,-4}, Y shifted +4 (90°), phase
 * starting at 0 and incrementing per burst. They are NOT captured from output.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "i2c.h"
#include "i2c_sim.h"

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond) do {                                            \
        g_checks++;                                                 \
        if (!(cond)) {                                              \
            g_failed++;                                             \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                           \
    } while (0)

#define ADDR 0x53u

static void set_measure(i2c_bus_t *bus)
{
    /* Write POWER_CTL (0x2D) = 0x08 (Measure) so data registers update. */
    uint8_t w[2] = { 0x2Du, 0x08u };
    int rc = bus->write(bus->ctx, ADDR, w, sizeof w);
    CHECK(rc == I2C_OK);
}

static int read_burst(i2c_bus_t *bus, uint8_t out[6])
{
    uint8_t reg = 0x32u;   /* DATAX0 */
    return bus->write_read(bus->ctx, ADDR, &reg, 1u, out, 6u);
}

int main(void)
{
    i2c_sim_t sim;
    i2c_bus_t bus;
    int rc;

    i2c_sim_init(&sim);
    i2c_sim_bind(&bus, &sim);

    /* 1. DEVID readable, equals 0xE5. */
    {
        uint8_t reg = 0x00u, devid = 0x00u;
        rc = bus.write_read(bus.ctx, ADDR, &reg, 1u, &devid, 1u);
        CHECK(rc == I2C_OK);
        CHECK(devid == 0xE5u);
    }

    /* 2. Wrong address NACKs on both methods. */
    {
        uint8_t reg = 0x00u, b = 0x00u;
        rc = bus.write_read(bus.ctx, 0x10u, &reg, 1u, &b, 1u);
        CHECK(rc == I2C_ERR_NACK);
        rc = bus.write(bus.ctx, 0x10u, &reg, 1u);
        CHECK(rc == I2C_ERR_NACK);
    }

    /* 3. Data registers frozen (all zero) until Measure is set. */
    {
        uint8_t b[6];
        rc = read_burst(&bus, b);
        CHECK(rc == I2C_OK);
        CHECK(b[0] == 0 && b[1] == 0 && b[2] == 0 &&
              b[3] == 0 && b[4] == 0 && b[5] == 0);
    }

    /* 4. After Measure, the first burst (phase 0) matches the model exactly:
     *    X = 0      -> 0x00,0x00
     *    Y = +16    -> 0x10,0x00
     *    Z = +256   -> 0x00,0x01   (little-endian) */
    set_measure(&bus);
    {
        uint8_t b[6];
        rc = read_burst(&bus, b);
        CHECK(rc == I2C_OK);
        CHECK(b[0] == 0x00u && b[1] == 0x00u);   /* X = 0      */
        CHECK(b[2] == 0x10u && b[3] == 0x00u);   /* Y = +16    */
        CHECK(b[4] == 0x00u && b[5] == 0x01u);   /* Z = +256   */
    }

    /* 5. Sample advances between bursts: next burst is phase 1:
     *    X = +4 -> 0x04,0x00 ; Y = +12 -> 0x0C,0x00 ; Z still +256. */
    {
        uint8_t b[6];
        rc = read_burst(&bus, b);
        CHECK(rc == I2C_OK);
        CHECK(b[0] == 0x04u && b[1] == 0x00u);   /* X = +4     */
        CHECK(b[2] == 0x0Cu && b[3] == 0x00u);   /* Y = +12    */
        CHECK(b[4] == 0x00u && b[5] == 0x01u);   /* Z = +256   */
    }

    /* 6. Determinism: re-init reproduces the exact same sequence. Capture the
     *    first four bursts, re-init, capture again, compare byte-for-byte. */
    {
        uint8_t seqA[4][6];
        uint8_t seqB[4][6];

        i2c_sim_init(&sim);
        i2c_sim_bind(&bus, &sim);
        set_measure(&bus);
        for (int i = 0; i < 4; i++) {
            rc = read_burst(&bus, seqA[i]);
            CHECK(rc == I2C_OK);
        }

        i2c_sim_init(&sim);
        i2c_sim_bind(&bus, &sim);
        set_measure(&bus);
        for (int i = 0; i < 4; i++) {
            rc = read_burst(&bus, seqB[i]);
            CHECK(rc == I2C_OK);
        }

        CHECK(memcmp(seqA, seqB, sizeof seqA) == 0);
    }

    printf("\ntest_i2c_sim: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
