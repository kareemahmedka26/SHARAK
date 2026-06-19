/*
 * test_adxl345_driver.c — drives the real adxl345 driver against the simulator.
 *
 * Verifies the bus-dependent driver logic (REQ-FW-008/009/011) without any
 * hardware, by injecting i2c_sim as the bus:
 *   - init succeeds and the documented config bytes actually land in the sim's
 *     register file (DATA_FORMAT=0x0B, BW_RATE=0x0A, POWER_CTL=0x08)
 *   - read returns a plausible sample (Z ~ +1000 mg from the +256 LSB model)
 *   - a wrong DEVID is rejected with ADXL345_E_WRONG_DEVID
 *   - NULL arguments are rejected with ADXL345_E_PARAM
 *   - a bus that always fails surfaces as ADXL345_E_BUS
 *
 * Expected values come from the datasheet/spec math and the sim's documented
 * model, not from running the driver (G-9).
 */
#include <stdio.h>
#include <stdint.h>
#include "i2c.h"
#include "i2c_sim.h"
#include "adxl345.h"

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond) do {                                            \
        g_checks++;                                                 \
        if (!(cond)) {                                              \
            g_failed++;                                             \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                           \
    } while (0)

/* A bus backend whose every operation fails, to exercise the E_BUS path. */
static int fail_write(void *ctx, uint8_t addr, const uint8_t *data, size_t len)
{
    (void)ctx; (void)addr; (void)data; (void)len;
    return I2C_ERR_BUS;
}
static int fail_write_read(void *ctx, uint8_t addr,
                           const uint8_t *wdata, size_t wlen,
                           uint8_t *rdata, size_t rlen)
{
    (void)ctx; (void)addr; (void)wdata; (void)wlen; (void)rdata; (void)rlen;
    return I2C_ERR_BUS;
}

int main(void)
{
    i2c_sim_t sim;
    i2c_bus_t bus;
    int rc;

    /* ---- 1. Successful init configures the chip ---------------------------*/
    i2c_sim_init(&sim);
    i2c_sim_bind(&bus, &sim);

    rc = adxl345_init(&bus);
    CHECK(rc == ADXL345_OK);
    /* The exact config bytes must have been written into the sim register file. */
    CHECK(sim.regs[ADXL345_REG_DATA_FORMAT] == 0x0Bu);  /* FULL_RES | +-16 g */
    CHECK(sim.regs[ADXL345_REG_BW_RATE]     == 0x0Au);  /* 100 Hz            */
    CHECK(sim.regs[ADXL345_REG_POWER_CTL]   == 0x08u);  /* Measure           */

    /* selftest should now pass too. */
    CHECK(adxl345_selftest(&bus) == ADXL345_OK);

    /* ---- 2. Read returns a plausible sample ------------------------------ */
    {
        adxl345_accel_t a;
        rc = adxl345_read(&bus, &a);
        CHECK(rc == ADXL345_OK);
        /* Z = +256 LSB -> 256 * 125 / 32 = 1000 mg exactly (1 g). */
        CHECK(a.z_mg == 1000);
        /* X/Y are the small wobble: well within +-100 mg. */
        CHECK(a.x_mg > -100 && a.x_mg < 100);
        CHECK(a.y_mg > -100 && a.y_mg < 100);
    }

    /* ---- 3. Wrong DEVID rejected ----------------------------------------- */
    {
        i2c_sim_t bad;
        i2c_bus_t bbus;
        i2c_sim_init(&bad);
        bad.regs[ADXL345_REG_DEVID] = 0x00u;   /* not an ADXL345 */
        i2c_sim_bind(&bbus, &bad);
        CHECK(adxl345_init(&bbus) == ADXL345_E_WRONG_DEVID);
        CHECK(adxl345_selftest(&bbus) == ADXL345_E_WRONG_DEVID);
    }

    /* ---- 4. NULL argument handling --------------------------------------- */
    {
        adxl345_accel_t a;
        CHECK(adxl345_init(NULL) == ADXL345_E_PARAM);
        CHECK(adxl345_read(NULL, &a) == ADXL345_E_PARAM);
        CHECK(adxl345_read(&bus, NULL) == ADXL345_E_PARAM);
        CHECK(adxl345_selftest(NULL) == ADXL345_E_PARAM);
    }

    /* ---- 5. Injected bus failure surfaces as E_BUS ----------------------- */
    {
        i2c_bus_t fbus;
        adxl345_accel_t a;
        fbus.write = fail_write;
        fbus.write_read = fail_write_read;
        fbus.ctx = NULL;
        CHECK(adxl345_init(&fbus) == ADXL345_E_BUS);
        CHECK(adxl345_read(&fbus, &a) == ADXL345_E_BUS);
        CHECK(adxl345_selftest(&fbus) == ADXL345_E_BUS);
    }

    printf("\ntest_adxl345_driver: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
