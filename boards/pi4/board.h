/*
 * board.h — Raspberry Pi 4 (BCM2711) board-level configuration
 *
 * This file is the single source of truth for hardware parameters that vary
 * per board: MMIO base addresses, peripheral types, memory layout, etc.
 *
 * Phase 1 scope: UART only.  More entries (GIC, timer, RAM) will be added
 * in Phase 2 when the HAL layer is formalised.
 */

#pragma once

/* ── UART ──────────────────────────────────────────────────────────
 * BCM2711 has two PL011 UARTs.  UART0 is brought out on GPIO 14/15.
 * Base address from BCM2711 ARM Peripherals manual §2.1.
 */
#define BOARD_UART0_BASE  0xFE201000UL
#define BOARD_UART_TYPE   UART_TYPE_PL011
