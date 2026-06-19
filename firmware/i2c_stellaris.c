/*
 * i2c_stellaris.c — real I2C0 single-master backend for the LM3S6965.
 *
 * Purpose
 *   The hardware counterpart of i2c_sim.c. It implements the same i2c_bus_t
 *   interface (i2c.h) using the LM3S6965's I2C0 master peripheral, so the
 *   unmodified adxl345 driver talks to a physical ADXL345 on real silicon.
 *
 * !!! CANNOT RUN UNDER QEMU !!!
 *   QEMU's lm3s6965evb machine does NOT emulate the I2C peripheral. Writing to
 *   these registers in emulation does nothing useful, which is the very reason
 *   the driver uses dependency injection (i2c_sim is selected under QEMU). This
 *   file is still compiled into every firmware image (REQ-FW-007) so it stays
 *   warning-free and in sync — but it is only *exercised* on hardware.
 *
 * Datasheet references (LM3S6965, verified register table from the prompt §3):
 *   SYSCTL  base 0x400FE000 : RCGC1 +0x104 (I2C0 = bit 12), RCGC2 +0x108 (GPIOB = bit 1)
 *   GPIOB   base 0x40005000 : AFSEL +0x420, ODR +0x50C, DEN +0x51C
 *                             PB2 = I2C0SCL, PB3 = I2C0SDA (SDA = open drain)
 *   I2C0 M  base 0x40020000 : I2CMSA +0x000, I2CMCS +0x004, I2CMDR +0x008,
 *                             I2CMTPR +0x00C, I2CMCR +0x020
 *   I2CMCS write bits: RUN=0, START=1, STOP=2, ACK=3
 *   I2CMCS read  bits: BUSY=0, ERROR=1, ADRACK=2, DATACK=3, ARBLST=4, IDLE=5, BUSBSY=6
 *
 * DATASHEET ERRATUM (confirmed): the LM3S6965 I2C init example text says write
 *   0x20 to I2CMCR, but the register table defines the Master Function Enable
 *   (MFE) as bit 4 = 0x10; 0x20 is bit 5 = SFE (Slave Function Enable). For a
 *   master we therefore write 0x10, NOT 0x20. See I2C_MCR_MFE below.
 */
#include "i2c_stellaris.h"

/* ---- SYSCTL ---------------------------------------------------------------*/
#define SYSCTL_BASE   0x400FE000u
#define SYSCTL_RCGC1  (*(volatile uint32_t *)(SYSCTL_BASE + 0x104u))
#define SYSCTL_RCGC2  (*(volatile uint32_t *)(SYSCTL_BASE + 0x108u))
#define RCGC1_I2C0    (1u << 12)
#define RCGC2_GPIOB   (1u << 1)

/* ---- GPIOB ----------------------------------------------------------------*/
#define GPIOB_BASE    0x40005000u
#define GPIOB_AFSEL   (*(volatile uint32_t *)(GPIOB_BASE + 0x420u))
#define GPIOB_ODR     (*(volatile uint32_t *)(GPIOB_BASE + 0x50Cu))
#define GPIOB_DEN     (*(volatile uint32_t *)(GPIOB_BASE + 0x51Cu))
#define PB2           (1u << 2)   /* I2C0SCL */
#define PB3           (1u << 3)   /* I2C0SDA */

/* ---- I2C0 master ----------------------------------------------------------*/
#define I2C0_BASE     0x40020000u
#define I2C_MSA       (*(volatile uint32_t *)(I2C0_BASE + 0x000u))  /* slave addr */
#define I2C_MCS       (*(volatile uint32_t *)(I2C0_BASE + 0x004u))  /* ctrl/status */
#define I2C_MDR       (*(volatile uint32_t *)(I2C0_BASE + 0x008u))  /* data        */
#define I2C_MTPR      (*(volatile uint32_t *)(I2C0_BASE + 0x00Cu))  /* timer period */
#define I2C_MCR       (*(volatile uint32_t *)(I2C0_BASE + 0x020u))  /* master ctrl */

/* I2CMCS — write (command) bits */
#define MCS_RUN       (1u << 0)
#define MCS_START     (1u << 1)
#define MCS_STOP      (1u << 2)
#define MCS_ACK       (1u << 3)
/* I2CMCS — read (status) bits */
#define MCS_BUSY      (1u << 0)
#define MCS_ERROR     (1u << 1)
#define MCS_ADRACK    (1u << 2)
#define MCS_DATACK    (1u << 3)
#define MCS_ARBLST    (1u << 4)

/* I2CMSA bit 0: 0 = master transmit, 1 = master receive */
#define MSA_RECEIVE   (1u << 0)

/* I2CMCR: Master Function Enable — bit 4 (see ERRATUM in the file header). */
#define I2C_MCR_MFE   (1u << 4)   /* = 0x10, NOT 0x20 */

/*
 * I2CMTPR timer-period (TPR) for ~100 kHz SCL.
 *   SCL_PERIOD = 2 * (1 + TPR) * (SCL_LP + SCL_HP) * CLK_PRD, SCL_LP=6, SCL_HP=4
 *   => TPR = SYSCLK / (2 * 10 * SCL_FREQ) - 1
 * Using the reset-default IOSC (~12 MHz, matching uart.c) and 100 kHz:
 *   TPR = 12_000_000 / (20 * 100_000) - 1 = 6 - 1 = 5
 * QEMU never runs this; on real silicon TPR must match the actual SYSCLK.
 */
#define I2C_SYSCLK_HZ 12000000u
#define I2C_SCL_HZ    100000u
#define I2C_TPR       ((I2C_SYSCLK_HZ / (20u * I2C_SCL_HZ)) - 1u)

/* Map a finished-transaction status word to an i2c_status_t. */
static int mcs_to_status(uint32_t status)
{
    if (status & MCS_ARBLST) {
        return I2C_ERR_BUS;          /* lost arbitration: a bus-level failure */
    }
    if (status & (MCS_ERROR | MCS_ADRACK | MCS_DATACK)) {
        return I2C_ERR_NACK;         /* address or data byte was not acked */
    }
    return I2C_OK;
}

/* Spin until the master finishes the current byte, then return its status. */
static int i2c_wait(void)
{
    /* The master sets BUSY shortly after a command is written; poll until it
     * clears. (On hardware a few NOPs of settle time precede this; QEMU n/a.) */
    while (I2C_MCS & MCS_BUSY) {
        /* spin */
    }
    return mcs_to_status(I2C_MCS);
}

static int stellaris_write(void *ctx, uint8_t addr, const uint8_t *data, size_t len)
{
    (void)ctx;                       /* no per-bus state on hardware */

    if (data == NULL || len == 0u) {
        return I2C_ERR_PARAM;
    }

    I2C_MSA = (uint32_t)((addr << 1) & 0xFEu);   /* transmit to 7-bit addr */

    for (size_t i = 0u; i < len; i++) {
        uint32_t cmd = MCS_RUN;
        if (i == 0u) {
            cmd |= MCS_START;        /* first byte: generate START */
        }
        if (i == (len - 1u)) {
            cmd |= MCS_STOP;         /* last byte: generate STOP */
        }
        I2C_MDR = data[i];
        I2C_MCS = cmd;

        int st = i2c_wait();
        if (st != I2C_OK) {
            /* Best-effort release of the bus on error. */
            I2C_MCS = MCS_STOP;
            return st;
        }
    }
    return I2C_OK;
}

static int stellaris_write_read(void *ctx, uint8_t addr,
                                const uint8_t *wdata, size_t wlen,
                                uint8_t *rdata, size_t rlen)
{
    (void)ctx;

    if (wdata == NULL || wlen == 0u || rdata == NULL || rlen == 0u) {
        return I2C_ERR_PARAM;
    }

    /* ---- Write phase: send the register pointer, NO stop (repeated start). */
    I2C_MSA = (uint32_t)((addr << 1) & 0xFEu);   /* transmit */
    for (size_t i = 0u; i < wlen; i++) {
        uint32_t cmd = MCS_RUN;
        if (i == 0u) {
            cmd |= MCS_START;
        }
        I2C_MDR = wdata[i];
        I2C_MCS = cmd;               /* deliberately no STOP: keep the bus */

        int st = i2c_wait();
        if (st != I2C_OK) {
            I2C_MCS = MCS_STOP;
            return st;
        }
    }

    /* ---- Read phase: repeated START as receiver, ACK all but the last byte. */
    I2C_MSA = (uint32_t)(((addr << 1) & 0xFEu) | MSA_RECEIVE);
    for (size_t i = 0u; i < rlen; i++) {
        uint32_t cmd = MCS_RUN;
        if (i == 0u) {
            cmd |= MCS_START;        /* repeated START into the read */
        }
        if (i == (rlen - 1u)) {
            cmd |= MCS_STOP;         /* last byte: NACK + STOP (no ACK bit) */
        } else {
            cmd |= MCS_ACK;          /* ack to request another byte */
        }
        I2C_MCS = cmd;

        int st = i2c_wait();
        if (st != I2C_OK) {
            I2C_MCS = MCS_STOP;
            return st;
        }
        rdata[i] = (uint8_t)(I2C_MDR & 0xFFu);
    }
    return I2C_OK;
}

void i2c_stellaris_init(i2c_bus_t *bus)
{
    if (bus == NULL) {
        return;
    }

    /* 1. Gate clocks to I2C0 and GPIOB; read-back gives the peripherals a few
     *    cycles to come ready before their registers are touched. */
    SYSCTL_RCGC1 |= RCGC1_I2C0;
    SYSCTL_RCGC2 |= RCGC2_GPIOB;
    (void)SYSCTL_RCGC2;

    /* 2. PB2/PB3 to I2C alternate function, digital enabled; SDA (PB3) must be
     *    open-drain (I2C is a wired-AND bus). This LM3S part has no GPIOPCTL
     *    mux register, so AFSEL is sufficient to route I2C0 to these pins. */
    GPIOB_AFSEL |= (PB2 | PB3);
    GPIOB_ODR   |= PB3;
    GPIOB_DEN   |= (PB2 | PB3);

    /* 3. Enable the master function (MFE = bit 4 = 0x10 — see ERRATUM). */
    I2C_MCR = I2C_MCR_MFE;

    /* 4. Program the SCL timer period for ~100 kHz. */
    I2C_MTPR = I2C_TPR;

    /* 5. Bind the bus to this backend. */
    bus->write      = stellaris_write;
    bus->write_read = stellaris_write_read;
    bus->ctx        = NULL;
}
