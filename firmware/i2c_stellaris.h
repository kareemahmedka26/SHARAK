#ifndef SHARAK_I2C_STELLARIS_H
#define SHARAK_I2C_STELLARIS_H

/*
 * i2c_stellaris.h — real LM3S6965 I2C0 master backend (hardware only).
 *
 * Same i2c_bus_t interface as the simulator. This backend talks to the actual
 * I2C0 peripheral, so it CANNOT run under QEMU (which doesn't emulate I2C) —
 * it exists so the identical adxl345 driver works on real silicon, and it is
 * compiled into every firmware build (REQ-FW-007) so it can never bit-rot.
 */

#include "i2c.h"

/*
 * Configure I2C0 as a 100 kHz single master (clocks, PB2/PB3 alternate
 * function + SDA open-drain, master enable, timer period) and bind the bus.
 * After this returns, `bus` drives real hardware. ctx is unused (no per-bus
 * state), so it is left NULL.
 */
void i2c_stellaris_init(i2c_bus_t *bus);

#endif /* SHARAK_I2C_STELLARIS_H */
