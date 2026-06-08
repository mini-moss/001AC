/*
 * uart.h — Minimal UART abstraction for Phase 1
 *
 * This header defines:
 *   1. The UART_TYPE_* constants that each board.h uses
 *   2. The public interface: uart_init(), uart_putc(), uart_puts()
 *
 * Each board provides BOARD_UART0_BASE and BOARD_UART_TYPE via its own
 * <board.h>, selected at compile time (-DBOARD_XXX).
 *
 * Only ONE translation unit (uart-pl011.c *or* uart-ns16550.c) is
 * compiled into the kernel — whichever matches the target board's
 * BOARD_UART_TYPE.
 */

#pragma once

#include <stdint.h>

/* ── UART controller type constants ──────────────────────────────── */
#define UART_TYPE_PL011    1
#define UART_TYPE_NS16550  2

/* ── Public interface ─────────────────────────────────────────────── */

/* Initialise the UART: disable, configure baud/word/FIFO, enable TX+RX. */
void uart_init(void);

/* Output a single character (busy-wait until TX FIFO can accept it). */
void uart_putc(char c);

/* Output a NUL-terminated string.  '\n' is expanded to "\r\n". */
void uart_puts(const char *s);
