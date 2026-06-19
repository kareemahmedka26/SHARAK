#ifndef SHARAK_ADXL345_H
#define SHARAK_ADXL345_H

#include <stdint.h>

/*
 * ADXL345 3-axis MEMS accelerometer driver (Sharak node).
 *
 * The driver is split into:
 *   - PURE logic (this decode/scale function): no hardware, host-testable.
 *   - Bus-dependent logic (init/read): added later, talks through i2c_bus_t.
 *
 * Datasheet facts encoded below (see header comments in adxl345.c too):
 *   - 7-bit I2C address 0x53 (SDO/ALT pin low).
 *   - DEVID always reads 0xE5.
 *   - Data registers 0x32..0x37 are little-endian, signed 16-bit two's complement.
 *   - Read all six in ONE burst to avoid torn samples.
 *   - FULL_RES mode => constant 3.90625 mg/LSB (256 LSB/g) on every range.
 */

/* ---- I2C address ---- */
#define ADXL345_I2C_ADDR            0x53u

/* ---- Register map (subset we use) ---- */
#define ADXL345_REG_DEVID           0x00u   /* read-only, == 0xE5            */
#define ADXL345_REG_BW_RATE         0x2Cu   /* output data rate              */
#define ADXL345_REG_POWER_CTL       0x2Du   /* standby/measure control       */
#define ADXL345_REG_DATA_FORMAT     0x31u   /* range + resolution            */
#define ADXL345_REG_DATAX0          0x32u   /* first of 6 data bytes         */
#define ADXL345_REG_FIFO_CTL        0x38u   /* FIFO mode (unused for now)    */

/* ---- Known/constant values ---- */
#define ADXL345_DEVID_VALUE         0xE5u

/* POWER_CTL bits */
#define ADXL345_POWER_CTL_MEASURE   0x08u

/* DATA_FORMAT bits */
#define ADXL345_DATA_FORMAT_FULL_RES 0x08u
#define ADXL345_RANGE_2G            0x00u
#define ADXL345_RANGE_4G            0x01u
#define ADXL345_RANGE_8G            0x02u
#define ADXL345_RANGE_16G           0x03u

/* BW_RATE codes (low nibble) */
#define ADXL345_RATE_100HZ          0x0Au
#define ADXL345_RATE_800HZ          0x0Du
#define ADXL345_RATE_3200HZ         0x0Fu

/* Scaling: full-res sensitivity is 256 LSB/g => mg = raw * 1000 / 256
 *                                              => mg = raw * 125 / 32   */
#define ADXL345_MG_NUM              125
#define ADXL345_MG_DEN              32

/* One acceleration sample, per-axis, in milli-g (mg). */
typedef struct {
    int32_t x_mg;
    int32_t y_mg;
    int32_t z_mg;
} adxl345_accel_t;

/*
 * adxl345_decode
 * --------------
 * Convert the 6 raw data-register bytes (DATAX0,DATAX1,DATAY0,DATAY1,DATAZ0,DATAZ1)
 * into a signed milli-g sample.
 *
 *   raw[0]=DATAX0 (low),  raw[1]=DATAX1 (high)
 *   raw[2]=DATAY0 (low),  raw[3]=DATAY1 (high)
 *   raw[4]=DATAZ0 (low),  raw[5]=DATAZ1 (high)
 *
 * Each axis is little-endian, signed 16-bit two's complement.
 * Scale to mg using ADXL345_MG_NUM / ADXL345_MG_DEN (use a 32-bit intermediate
 * to avoid overflow before dividing).
 *
 * Pure function: no hardware, no side effects. Fully unit-testable on the host.
 */
void adxl345_decode(const uint8_t raw[6], adxl345_accel_t *out);

/* ===========================================================================
 * Bus-dependent driver API (appended after the owner-reviewed base header).
 * These talk to the chip through the injected i2c_bus_t (sim or real I2C0),
 * so they are testable against i2c_sim.c on the host (REQ-FW-005/008/009/011).
 * =========================================================================== */
#include "i2c.h"

/* Negative error codes; 0 == success. Callers use `if (rc != ADXL345_OK)`. */
typedef enum {
    ADXL345_OK            =  0,
    ADXL345_E_PARAM       = -1,   /* NULL bus/out or missing bus methods   */
    ADXL345_E_BUS         = -2,   /* underlying i2c_bus_t reported an error */
    ADXL345_E_WRONG_DEVID = -3    /* DEVID != 0xE5 (not an ADXL345)         */
} adxl345_status_t;

/*
 * Verify identity, then configure the chip:
 *   DEVID == 0xE5, DATA_FORMAT = 0x0B (FULL_RES | +-16 g), BW_RATE = 0x0A
 *   (100 Hz), POWER_CTL = 0x08 (Measure). Returns ADXL345_OK or an error code.
 */
int adxl345_init(const i2c_bus_t *bus);

/*
 * Read all six data registers in ONE burst from 0x32 (torn-sample safe,
 * REQ-FW-009), then decode to milli-g into *out.
 */
int adxl345_read(const i2c_bus_t *bus, adxl345_accel_t *out);

/* Lightweight liveness check: re-read and verify DEVID. */
int adxl345_selftest(const i2c_bus_t *bus);

#endif /* SHARAK_ADXL345_H */
