/*
 * i2c_sim.c — a fake ADXL345 behind the i2c_bus_t interface.
 *
 * Purpose
 *   QEMU's lm3s6965evb does not emulate I2C, so the node needs a "sensor" it
 *   can actually talk to under emulation, and the host unit tests need a bus
 *   the real driver can drive without hardware. This module is that fake chip:
 *   a RAM register file plus a deterministic, integer-only synthetic vibration
 *   generator. It implements i2c.h's write / write_read so the unmodified
 *   adxl345 driver runs against it exactly as it would against silicon
 *   (REQ-FW-006).
 *
 * Modeled ADXL345 behavior
 *   - DEVID (0x00) reads 0xE5; any address other than 0x53 NACKs.
 *   - Standard I2C register access: the first written byte is the register
 *     pointer; further writes store bytes with auto-increment; reads stream
 *     bytes from the pointer with auto-increment.
 *   - Data registers 0x32..0x37 only update when POWER_CTL Measure (bit 3) is
 *     set — they stay frozen in standby, mirroring the real chip.
 *   - One 6-byte burst read returns an ATOMIC snapshot (the sample is computed
 *     once at the start of the read and does not advance mid-burst), mirroring
 *     the ADXL345's shadow-register / torn-sample-safe behavior. The sample
 *     advances exactly once per data-region burst, so consecutive bursts differ
 *     but a single burst is self-consistent.
 *
 * Determinism (G-9): every value comes from a const integer lookup table and
 * the phase counter in ctx, which i2c_sim_init() resets. No floats (G-2), no
 * mutable statics. Re-init => identical future sequence, so tests hard-code
 * expected bytes from datasheet math, never from this code's own output.
 *
 * Portability: uses only <stdint.h>/<stddef.h> (via i2c.h), so it compiles
 * both for the host (tests) and for the ARM image (linked into the firmware).
 */
#include "i2c_sim.h"

/* ---- Local copies of the ADXL345 facts this fake needs to honor ------------
 * Defined locally (not pulled from adxl345.h) so the simulator stays a
 * self-contained model of the *chip*, independent of the driver it serves. */
#define SIM_ADDR              0x53u   /* 7-bit I2C address                    */
#define SIM_REG_DEVID         0x00u
#define SIM_REG_POWER_CTL     0x2Du
#define SIM_REG_DATAX0        0x32u   /* first of the 6 data bytes            */
#define SIM_REG_DATAZ1        0x37u   /* last of the 6 data bytes             */
#define SIM_DEVID_VALUE       0xE5u
#define SIM_POWER_CTL_MEASURE 0x08u   /* POWER_CTL bit D3                      */

/* Advance the register pointer with wrap, so a runaway length can never index
 * outside the register file (defensive; real reads stay in range). */
static uint8_t sim_next_ptr(uint8_t ptr)
{
    ptr = (uint8_t)(ptr + 1u);
    if (ptr >= I2C_SIM_NREGS) {
        ptr = 0u;
    }
    return ptr;
}

/*
 * Recompute the six data registers as one atomic snapshot and advance the
 * vibration phase once. Signal model: Z ~ +1 g (+256 LSB), with a small
 * triangle-wave wobble on X and Y (±16 LSB, Y phase-shifted 90°). All values
 * are raw little-endian signed 16-bit, exactly as the real data registers.
 */
static void sim_refresh_sample(i2c_sim_t *s)
{
    /* 16-entry triangle wave, amplitude 16 LSB. const => read-only, shared,
     * does not affect determinism (no per-call mutable state lives here). */
    static const int16_t wobble[16] = {
        0,  4,  8, 12, 16, 12,  8,  4,
        0, -4, -8,-12,-16,-12, -8, -4
    };

    int16_t x = wobble[s->phase & 15u];
    int16_t y = wobble[(s->phase + 4u) & 15u];   /* +4 entries = 90° shift */
    int16_t z = 256;                             /* +1 g in FULL_RES (256 LSB/g) */

    /* Pack little-endian. Signed->unsigned conversion is well defined in C
     * (modulo 2^16), which is exactly the two's-complement bit pattern the
     * real chip would place in these registers. */
    s->regs[SIM_REG_DATAX0 + 0u] = (uint8_t)((uint16_t)x & 0xFFu);
    s->regs[SIM_REG_DATAX0 + 1u] = (uint8_t)(((uint16_t)x >> 8) & 0xFFu);
    s->regs[SIM_REG_DATAX0 + 2u] = (uint8_t)((uint16_t)y & 0xFFu);
    s->regs[SIM_REG_DATAX0 + 3u] = (uint8_t)(((uint16_t)y >> 8) & 0xFFu);
    s->regs[SIM_REG_DATAX0 + 4u] = (uint8_t)((uint16_t)z & 0xFFu);
    s->regs[SIM_REG_DATAX0 + 5u] = (uint8_t)(((uint16_t)z >> 8) & 0xFFu);

    s->phase++;
}

/* True if a read of [ptr, ptr+rlen) overlaps the data-register block. */
static int sim_touches_data(uint8_t ptr, size_t rlen)
{
    return (ptr <= SIM_REG_DATAZ1) &&
           (((uint32_t)ptr + (uint32_t)rlen) > SIM_REG_DATAX0);
}

static int sim_write(void *ctx, uint8_t addr, const uint8_t *data, size_t len)
{
    i2c_sim_t *s = (i2c_sim_t *)ctx;

    if (s == NULL) {
        return I2C_ERR_PARAM;
    }
    if (addr != SIM_ADDR) {
        return I2C_ERR_NACK;                 /* only our fake chip answers */
    }
    if (len == 0u) {
        return I2C_OK;                        /* nothing to do */
    }
    if (data == NULL) {
        return I2C_ERR_PARAM;
    }

    /* First byte sets the register pointer; the rest are register writes. */
    s->ptr = data[0];
    for (size_t i = 1u; i < len; i++) {
        s->regs[s->ptr] = data[i];
        s->ptr = sim_next_ptr(s->ptr);
    }
    return I2C_OK;
}

static int sim_write_read(void *ctx, uint8_t addr,
                          const uint8_t *wdata, size_t wlen,
                          uint8_t *rdata, size_t rlen)
{
    i2c_sim_t *s = (i2c_sim_t *)ctx;

    if (s == NULL) {
        return I2C_ERR_PARAM;
    }
    if (addr != SIM_ADDR) {
        return I2C_ERR_NACK;
    }
    if ((wlen > 0u && wdata == NULL) || (rlen > 0u && rdata == NULL)) {
        return I2C_ERR_PARAM;
    }

    /* Write phase: the pointer byte (and any extra written bytes). */
    if (wlen > 0u) {
        s->ptr = wdata[0];
        for (size_t i = 1u; i < wlen; i++) {
            s->regs[s->ptr] = wdata[i];
            s->ptr = sim_next_ptr(s->ptr);
        }
    }

    /* If this read pulls from the data registers and the chip is measuring,
     * take a fresh atomic snapshot BEFORE serving any bytes — so the whole
     * burst is self-consistent and the sample advances once per burst. */
    if (rlen > 0u &&
        sim_touches_data(s->ptr, rlen) &&
        (s->regs[SIM_REG_POWER_CTL] & SIM_POWER_CTL_MEASURE) != 0u) {
        sim_refresh_sample(s);
    }

    /* Read phase: stream bytes from the pointer with auto-increment. */
    for (size_t i = 0u; i < rlen; i++) {
        rdata[i] = s->regs[s->ptr];
        s->ptr = sim_next_ptr(s->ptr);
    }
    return I2C_OK;
}

void i2c_sim_init(i2c_sim_t *sim)
{
    if (sim == NULL) {
        return;
    }
    for (size_t i = 0u; i < I2C_SIM_NREGS; i++) {
        sim->regs[i] = 0u;
    }
    sim->regs[SIM_REG_DEVID] = SIM_DEVID_VALUE;   /* identity byte */
    sim->ptr   = 0u;
    sim->phase = 0u;
}

void i2c_sim_bind(i2c_bus_t *bus, i2c_sim_t *sim)
{
    if (bus == NULL) {
        return;
    }
    bus->write      = sim_write;
    bus->write_read = sim_write_read;
    bus->ctx        = sim;
}
