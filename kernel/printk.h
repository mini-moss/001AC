/*
 * printk.h — kernel formatted console output
 *
 * A minimal printf-style function for kernel diagnostics.  Supports:
 *   %s — NUL-terminated string
 *   %d — signed decimal integer
 *   %u — unsigned decimal integer
 *   %x — unsigned hex (lowercase, 32-bit)
 *   %c — single character
 *   %% — literal %
 *
 * Output goes to uart_putc() (defined in kernel/uart.h).  This is a
 * synchronous, busy-wait implementation — no buffering, no IRQ-driven
 * output.  Fine for Phase 1–3 debugging.
 */

#pragma once

/*
 * Print a formatted string to the debug UART.
 *
 * Not interrupt-safe (uses busy-wait UART writes).  Call only from
 * task context or from exception handlers where blocking is acceptable.
 */
void printk(const char *fmt, ...);
