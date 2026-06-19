#ifndef SHARAK_I2C_SIM_H
#define SHARAK_I2C_SIM_H

/*
 * i2c_sim.h — public handle for the simulated ADXL345 I2C backend.
 *
 * The simulator is a fake ADXL345 living entirely in RAM. It implements the
 * i2c_bus_t interface (see i2c.h) so the real adxl345 driver can run against
 * it under QEMU (which has no I2C) and in host unit tests. Determinism is a
 * hard requirement: same init -> same byte sequence, integer-only, no floats,
 * so tests can hard-code expected values from the datasheet math (G-9).
 *
 * The state struct is exposed (not opaque) ON PURPOSE so tests can poke it
 * directly — e.g. corrupt DEVID to exercise the driver's wrong-device path.
 */

#include <stdint.h>
#include "i2c.h"

/* Number of emulated registers: covers DEVID (0x00) .. FIFO_CTL (0x38) and one
 * spare (0x39). The ADXL345 register map we use lives entirely below this. */
#define I2C_SIM_NREGS  0x3Au   /* 58 */

typedef struct {
    uint8_t  regs[I2C_SIM_NREGS]; /* the fake chip's register file            */
    uint8_t  ptr;                 /* current register pointer (auto-increments) */
    uint32_t phase;               /* synthetic-vibration table index            */
} i2c_sim_t;

/*
 * Reset the simulator to a known power-on state:
 *   - all registers zero, except DEVID (0x00) = 0xE5
 *   - register pointer = 0, vibration phase = 0
 * Re-calling this guarantees an identical future byte sequence (determinism).
 */
void i2c_sim_init(i2c_sim_t *sim);

/*
 * Bind a sim instance into an i2c_bus_t so a driver can use it:
 *   bus->write / bus->write_read point at the sim's methods, bus->ctx = sim.
 */
void i2c_sim_bind(i2c_bus_t *bus, i2c_sim_t *sim);

#endif /* SHARAK_I2C_SIM_H */
