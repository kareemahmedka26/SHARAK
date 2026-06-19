/*
 * main.c — Sharak node super-loop (REQ-FW-012).
 *
 * Flow: configure UART0 -> bring up the (simulated) ADXL345 over the injected
 * I2C bus -> forever { read a sample -> pack the 16-byte payload -> HDLC-encode
 * with CRC -> push the frame out UART0 -> pace ~100 Hz }.
 *
 * WHY the simulator and not i2c_stellaris here: QEMU's lm3s6965evb does not
 * emulate I2C, so under emulation the node's "sensor" is i2c_sim (the same
 * driver code would talk to i2c_stellaris on real silicon — that is the whole
 * point of the i2c_bus_t dependency injection; see docs/architecture.md).
 *
 * Memory model: static buffers only, no heap, builds with -nostdlib. The image
 * carries its own runtime (startup.c); there is no libc here.
 */
#include "uart.h"
#include "i2c.h"
#include "i2c_sim.h"
#include "adxl345.h"
#include <sharak/protocol.h>

/*
 * Crude busy-wait pacing. This is NOT calibrated timing — QEMU has no real
 * clock and the count is a rough placeholder so the stream is watchable. A real
 * product would pace the loop from a SysTick interrupt at exactly 100 Hz; that
 * is a planned refinement (docs/architecture.md section 5).
 * `volatile` stops the optimizer from deleting the empty loop.
 */
static void delay_pace(void)
{
    for (volatile uint32_t n = 0u; n < 200000u; n++) {
        /* spin */
    }
}

int main(void)
{
    /* Static buffers: no stack-hungry locals, no heap. */
    static i2c_sim_t       sim;
    static i2c_bus_t       bus;
    static adxl345_accel_t sample;
    static uint8_t         payload[SK_PAYLOAD_LEN];
    static uint8_t         frame[SK_FRAME_MAX];

    uint16_t seq = 0u;

    uart_init();
    uart_puts("Sharak node online\r\n");

    /* Wire up the simulated sensor as the I2C backend, then start the chip. */
    i2c_sim_init(&sim);
    i2c_sim_bind(&bus, &sim);

    if (adxl345_init(&bus) != ADXL345_OK) {
        /* Don't spin emitting garbage frames — report once and stop. */
        uart_puts("ERROR: ADXL345 init failed; halting\r\n");
        for (;;) {
            /* halt */
        }
    }

    for (;;) {
        int plen;
        int flen;

        if (adxl345_read(&bus, &sample) != ADXL345_OK) {
            uart_puts("ERROR: ADXL345 read failed; halting\r\n");
            for (;;) {
                /* halt */
            }
        }

        plen = sk_payload_pack(payload, sizeof payload,
                               seq, sample.x_mg, sample.y_mg, sample.z_mg);
        if (plen == (int)SK_PAYLOAD_LEN) {
            flen = sk_frame_encode(payload, (size_t)plen, frame, sizeof frame);
            if (flen > 0) {
                uart_write(frame, (size_t)flen);
            }
        }

        seq++;            /* uint16_t: wraps naturally at 0xFFFF -> 0x0000 */
        delay_pace();
    }
}
