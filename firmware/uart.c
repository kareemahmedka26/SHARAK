/*
 * uart.c — polled UART0 driver for the TI Stellaris LM3S6965 (QEMU lm3s6965evb).
 *
 * Purpose
 *   Drive UART0 with no vendor SDK: raw memory-mapped register access only.
 *   TX is busy-wait on the TX-FIFO-full flag; RX is a non-blocking poll of the
 *   RX-FIFO-empty flag. This is the node's telemetry pipe to the (future)
 *   gateway, surfaced under QEMU as a TCP socket.
 *
 * Datasheet references (LM3S6965, verified against the local PDF — see
 *   docs/requirements.md and the prompt's verified register table):
 *     UART0 base 0x4000C000
 *       DR   +0x000  data
 *       FR   +0x018  flags   (TXFF = bit 5, RXFE = bit 4)
 *       IBRD +0x024  integer baud-rate divisor
 *       FBRD +0x028  fractional baud-rate divisor
 *       LCRH +0x02C  line control (WLEN, FEN, parity, stop)
 *       CTL  +0x030  control (UARTEN bit 0, TXE bit 8, RXE bit 9)
 *     SYSCTL base 0x400FE000
 *       RCGC1 +0x104  run-mode clock gating 1 (UART0 = bit 0)
 *
 * Implementation-defined / portability notes
 *   - All register access is through `volatile uint32_t *`: `volatile` forbids
 *     the compiler from caching or reordering MMIO loads/stores, which is
 *     mandatory for device registers (a non-volatile read could be elided).
 *   - Casting an integer literal to a pointer is implementation-defined
 *     (C17 6.3.2.3p5); it is the standard, unavoidable idiom for MMIO and is
 *     well-defined on this target's flat address map.
 */
#include "uart.h"

/* ---- UART0 registers -------------------------------------------------------*/
#define UART0_BASE   0x4000C000u
#define UART_DR      (*(volatile uint32_t *)(UART0_BASE + 0x000u)) /* data         */
#define UART_FR      (*(volatile uint32_t *)(UART0_BASE + 0x018u)) /* flags        */
#define UART_IBRD    (*(volatile uint32_t *)(UART0_BASE + 0x024u)) /* int  divisor */
#define UART_FBRD    (*(volatile uint32_t *)(UART0_BASE + 0x028u)) /* frac divisor */
#define UART_LCRH    (*(volatile uint32_t *)(UART0_BASE + 0x02Cu)) /* line control */
#define UART_CTL     (*(volatile uint32_t *)(UART0_BASE + 0x030u)) /* control      */

#define UART_FR_RXFE (1u << 4)   /* RX FIFO empty */
#define UART_FR_TXFF (1u << 5)   /* TX FIFO full  */

#define UART_LCRH_FEN    (1u << 4)           /* enable TX/RX FIFOs        */
#define UART_LCRH_WLEN_8 (3u << 5)           /* 8 data bits (WLEN = 0b11) */

#define UART_CTL_UARTEN (1u << 0)            /* UART enable     */
#define UART_CTL_TXE    (1u << 8)            /* transmit enable */
#define UART_CTL_RXE    (1u << 9)            /* receive enable  */

/* ---- SYSCTL clock gating ---------------------------------------------------*/
#define SYSCTL_BASE  0x400FE000u
#define SYSCTL_RCGC1 (*(volatile uint32_t *)(SYSCTL_BASE + 0x104u))
#define SYSCTL_RCGC1_UART0 (1u << 0)

/*
 * Baud-rate divisors for 115200 baud.
 *
 * The LM3S6965 comes out of reset running from the internal oscillator with
 * the PLL bypassed, so the system (and UART) clock is the IOSC, nominally
 * ~12 MHz. With 16x oversampling (HSE = 0, the reset default):
 *
 *   BRD  = SYSCLK / (16 * baud) = 12_000_000 / (16 * 115200) = 6.5104...
 *   IBRD = floor(BRD)                          = 6
 *   FBRD = round((BRD - IBRD) * 64)            = round(0.5104 * 64) = 33
 *
 * WHY these are hard-coded: with -nostdlib there is no floating point and no
 * runtime divide library we want to pull in for a constant; the divisors are
 * computed here at authoring time and checked in as integers (G-2: no floats).
 *
 * IMPORTANT teaching note: QEMU's lm3s6965evb UART ignores the baud divisors
 * entirely — output appears regardless of IBRD/FBRD. On REAL silicon these
 * values matter, so they are programmed correctly anyway; the driver must be
 * the same code that would run on hardware (the whole point of this project).
 */
#define UART_IBRD_115200  6u
#define UART_FBRD_115200  33u

void uart_init(void)
{
    /* 1. Gate the UART0 clock on. After enabling a peripheral clock the
     *    Stellaris requires a few cycles before its registers are accessible;
     *    a read-back of RCGC1 provides that settling delay deterministically. */
    SYSCTL_RCGC1 |= SYSCTL_RCGC1_UART0;
    (void)SYSCTL_RCGC1;

    /* 2. Disable the UART before reprogramming it (LCRH/IBRD/FBRD must only be
     *    changed while UARTEN = 0, per the datasheet's configuration sequence). */
    UART_CTL = 0u;

    /* 3. Program the baud divisors (see derivation above). */
    UART_IBRD = UART_IBRD_115200;
    UART_FBRD = UART_FBRD_115200;

    /* 4. Line control: 8 data bits, no parity, 1 stop bit (8N1), FIFOs enabled.
     *    LCRH must be written AFTER IBRD/FBRD — the write to LCRH latches the
     *    new baud configuration. */
    UART_LCRH = UART_LCRH_WLEN_8 | UART_LCRH_FEN;

    /* 5. Re-enable: UART on, TX on, RX on. */
    UART_CTL = UART_CTL_UARTEN | UART_CTL_TXE | UART_CTL_RXE;
}

void uart_putc(char c)
{
    /* Busy-wait while the TX FIFO is full, then push one byte. */
    while (UART_FR & UART_FR_TXFF) {
        /* spin */
    }
    UART_DR = (uint32_t)(uint8_t)c;
}

void uart_puts(const char *s)
{
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

void uart_write(const uint8_t *data, size_t len)
{
    for (size_t i = 0u; i < len; i++) {
        uart_putc((char)data[i]);
    }
}

int uart_try_getc(void)
{
    if (UART_FR & UART_FR_RXFE) {
        return -1;               /* RX FIFO empty: nothing to read right now */
    }
    return (int)(UART_DR & 0xFFu);
}
