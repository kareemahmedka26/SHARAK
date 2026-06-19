/*
 * adxl345.c — ADXL345 accelerometer driver (decode + bus-dependent init/read).
 *
 * Layering: adxl345_decode() is PURE (no hardware) and host-tested directly;
 * init/read/selftest talk to the chip only through the injected i2c_bus_t
 * (i2c.h), so they too run against the simulator on the host (REQ-FW-005).
 *
 * ---------------------------------------------------------------------------
 * SIGN-RECONSTRUCTION BUG FIX (guideline G-7)
 *   The original decode scaled `(int32_t)x` where x is uint16_t. For a negative
 *   reading like 0xFFFF (-1 LSB), (int32_t)0xFFFF widens to +65535, NOT -1, so
 *   every negative axis came out large and positive. The fix reinterprets the
 *   16-bit two's-complement pattern first: `(int32_t)(int16_t)x`.
 *
 *   Why the inner cast is the whole point: converting a uint16_t whose value
 *   exceeds INT16_MAX to int16_t is IMPLEMENTATION-DEFINED in C17 (6.3.1.3p3).
 *   GCC (our compiler, G-6) documents modulo-2^16 wrapping, which yields the
 *   two's-complement reinterpretation we want (0xFFFF -> -1). The outer widen
 *   to int32_t is then fully defined sign-extension. The explicit cast is
 *   required by G-6 and must never be called "portable C".
 * ---------------------------------------------------------------------------
 */
#include "adxl345.h"

/* DATA_FORMAT value: FULL_RES (D3) | +-16 g range (D1:D0 = 11) = 0x0B. */
#define ADXL345_DATA_FORMAT_CONFIG \
    ((uint8_t)(ADXL345_DATA_FORMAT_FULL_RES | ADXL345_RANGE_16G))

void adxl345_decode(const uint8_t raw[6], adxl345_accel_t *out)
{
    /* extract the actual raw data from received frame (little-endian, G-3) */
    uint16_t x = ((uint16_t)raw[0] | ((uint16_t)raw[1] << 8));
    uint16_t y = ((uint16_t)raw[2] | ((uint16_t)raw[3] << 8));
    uint16_t z = ((uint16_t)raw[4] | ((uint16_t)raw[5] << 8));

    /* convert raw data to its physical representation in mg.
     * (int16_t) reinterprets the raw two's-complement bit pattern so negatives
     * decode correctly (G-7); multiply-before-divide in int32_t keeps full
     * precision before the single truncating divide (G-1). */
    out->x_mg = (int32_t)(int16_t)x * ADXL345_MG_NUM / ADXL345_MG_DEN;
    out->y_mg = (int32_t)(int16_t)y * ADXL345_MG_NUM / ADXL345_MG_DEN;
    out->z_mg = (int32_t)(int16_t)z * ADXL345_MG_NUM / ADXL345_MG_DEN;
}

/* ---- small bus helpers ----------------------------------------------------*/

/* True only if the bus and both of its methods are present. */
static int bus_ok(const i2c_bus_t *bus)
{
    return (bus != NULL) && (bus->write != NULL) && (bus->write_read != NULL);
}

/* Write one register (reg, val). Maps any bus failure to ADXL345_E_BUS. */
static int reg_write(const i2c_bus_t *bus, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    int rc = bus->write(bus->ctx, ADXL345_I2C_ADDR, buf, sizeof buf);
    return (rc == I2C_OK) ? ADXL345_OK : ADXL345_E_BUS;
}

/* Read one register into *val. Maps any bus failure to ADXL345_E_BUS. */
static int reg_read(const i2c_bus_t *bus, uint8_t reg, uint8_t *val)
{
    int rc = bus->write_read(bus->ctx, ADXL345_I2C_ADDR, &reg, 1u, val, 1u);
    return (rc == I2C_OK) ? ADXL345_OK : ADXL345_E_BUS;
}

int adxl345_init(const i2c_bus_t *bus)
{
    uint8_t devid = 0u;
    int rc;

    if (!bus_ok(bus)) {
        return ADXL345_E_PARAM;
    }

    /* 1. Identity: DEVID must read 0xE5 before we configure anything. */
    rc = reg_read(bus, ADXL345_REG_DEVID, &devid);
    if (rc != ADXL345_OK) {
        return rc;                       /* ADXL345_E_BUS */
    }
    if (devid != ADXL345_DEVID_VALUE) {
        return ADXL345_E_WRONG_DEVID;
    }

    /* 2. DATA_FORMAT = 0x0B (FULL_RES, +-16 g). */
    rc = reg_write(bus, ADXL345_REG_DATA_FORMAT, ADXL345_DATA_FORMAT_CONFIG);
    if (rc != ADXL345_OK) {
        return rc;
    }

    /* 3. BW_RATE = 0x0A (100 Hz output data rate). */
    rc = reg_write(bus, ADXL345_REG_BW_RATE, ADXL345_RATE_100HZ);
    if (rc != ADXL345_OK) {
        return rc;
    }

    /* 4. POWER_CTL = 0x08 (leave standby, start measuring) — done LAST so the
     *    chip only begins sampling once fully configured. */
    rc = reg_write(bus, ADXL345_REG_POWER_CTL, ADXL345_POWER_CTL_MEASURE);
    if (rc != ADXL345_OK) {
        return rc;
    }

    return ADXL345_OK;
}

int adxl345_read(const i2c_bus_t *bus, adxl345_accel_t *out)
{
    uint8_t reg = ADXL345_REG_DATAX0;    /* 0x32 */
    uint8_t raw[6];
    int rc;

    if (!bus_ok(bus) || out == NULL) {
        return ADXL345_E_PARAM;
    }

    /* ONE 6-byte burst read (never register-by-register): the ADXL345 latches
     * all six data bytes when the first is read, so a single transaction is
     * torn-sample safe (REQ-FW-009). */
    rc = bus->write_read(bus->ctx, ADXL345_I2C_ADDR, &reg, 1u, raw, sizeof raw);
    if (rc != I2C_OK) {
        return ADXL345_E_BUS;
    }

    adxl345_decode(raw, out);
    return ADXL345_OK;
}

int adxl345_selftest(const i2c_bus_t *bus)
{
    uint8_t devid = 0u;
    int rc;

    if (!bus_ok(bus)) {
        return ADXL345_E_PARAM;
    }

    rc = reg_read(bus, ADXL345_REG_DEVID, &devid);
    if (rc != ADXL345_OK) {
        return rc;
    }
    return (devid == ADXL345_DEVID_VALUE) ? ADXL345_OK : ADXL345_E_WRONG_DEVID;
}
