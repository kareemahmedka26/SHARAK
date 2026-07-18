#ifndef SHARAK_UART_H
#define SHARAK_UART_H

/*
 * uart.h — public interface to the polled UART0 driver (LM3S6965 / QEMU).
 *
 * Purpose
 *   The node's only output channel under QEMU is UART0, exposed on a TCP
 *   socket by `-serial tcp:...` (see scripts/run_qemu.sh). main.c emits binary
 *   HDLC frames here; there is deliberately no printf-style formatting in the
 *   firmware (no libc, -nostdlib), so the API is intentionally tiny.
 *
 * Design decisions
 *   - Polled (busy-wait) TX and a polled, NON-blocking RX (REQ-FW-004). No
 *     interrupts — at 100 Hz x <=38 B, polling provably cannot fall
 *     behind a 115200-baud line, so an IRQ + ring buffer would be complexity
 *     with no benefit yet (see docs/02_architecture_SWE2/architecture.md section 8).
 *   - `uart_write` takes a byte pointer + `size_t` length because frames are
 *     binary and may legitimately contain NUL bytes — `uart_puts` (NUL-
 *     terminated) is only for human-readable banners/errors.
 *
 * The old put_uint/put_int/put_hex8 helpers were removed: the node speaks
 * binary frames, not ASCII numbers, so decimal/hex formatting is dead weight.
 */

#include <stdint.h>
#include <stddef.h>

/* Bring UART0 out of reset and configure 8N1, FIFO-enabled, 115200 baud. */
void uart_init(void);

/* Blocking single-byte transmit (waits while the TX FIFO is full). */
void uart_putc(char c);

/* Blocking transmit of a NUL-terminated C string (banners / error text only). */
void uart_puts(const char *s);

/* Blocking transmit of an arbitrary byte buffer (binary-safe). */
void uart_write(const uint8_t *data, size_t len);

/*
 * Non-blocking receive.
 * Returns the next received byte (0..255), or -1 if the RX FIFO is empty.
 * (REQ-FW-004: a polled, non-blocking RX read.)
 */
int uart_try_getc(void);

#endif /* SHARAK_UART_H */
