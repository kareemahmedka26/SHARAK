#ifndef SHARAK_I2C_H
#define SHARAK_I2C_H

/*
 * i2c.h — the dependency-injection seam for I2C bus access (REQ-FW-005).
 *
 * This tiny interface is the architectural heart of the node. QEMU's
 * lm3s6965evb emulates the Cortex-M3 core and UART0 but NOT the I2C
 * peripheral, so a sensor driver hard-wired to I2C0 MMIO could never be
 * exercised in emulation. The fix: the ADXL345 driver never touches a bus
 * directly — it is handed a `const i2c_bus_t *` and calls through function
 * pointers. Two backends implement this interface:
 *
 *   - i2c_sim.c       : a fake ADXL345 in RAM (used in QEMU and host tests)
 *   - i2c_stellaris.c : the real I2C0 master (compiled in, used on silicon)
 *
 * The driver cannot tell them apart, which is the entire point: the QEMU
 * limitation is contained in one swappable backend instead of leaking into
 * the driver (docs/architecture.md section 2).
 *
 * Contract for both methods:
 *   - `addr` is the 7-bit slave address (e.g. 0x53 for the ADXL345).
 *   - Return 0 (I2C_OK) on success, a negative i2c_status_t on failure.
 *   - Backends NULL-check their pointer arguments and report I2C_ERR_PARAM.
 */

#include <stdint.h>
#include <stddef.h>

/* Negative error codes so callers can `if (rc < 0)`; 0 means success. */
typedef enum {
    I2C_OK        =  0,
    I2C_ERR_NACK  = -1,   /* slave did not acknowledge address/data        */
    I2C_ERR_BUS   = -2,   /* arbitration loss / bus error / timeout         */
    I2C_ERR_PARAM = -3    /* NULL pointer or otherwise invalid argument     */
} i2c_status_t;

typedef struct i2c_bus {
    /* Write `len` bytes to `addr`. Returns 0 or negative i2c_status_t. */
    int (*write)(void *ctx, uint8_t addr, const uint8_t *data, size_t len);

    /*
     * Combined write-then-read with a repeated START (no STOP between phases):
     * send `wlen` bytes (typically a register pointer), then read `rlen` bytes
     * into `rdata`. This is how a register read is done on I2C and is exactly
     * what an atomic multi-byte burst read of the ADXL345 needs.
     */
    int (*write_read)(void *ctx, uint8_t addr,
                      const uint8_t *wdata, size_t wlen,
                      uint8_t *rdata, size_t rlen);

    void *ctx;   /* backend-private state (sim register file, or unused on HW) */
} i2c_bus_t;

#endif /* SHARAK_I2C_H */
