/*
 * board.h — Raspberry Pi 5 (BCM2712) board-level configuration
 *
 * Placeholder for Phase 2+.  The BCM2712 uses a different MMIO map than
 * BCM2711 (e.g. UART0 moves from 0xFE20_1000 to 0x1_07D0_0100_0000).
 *
 * This file is NOT used in the Phase 1 build — Pi 5 target is deferred
 * per dev-log/006 decision 2 (AArch64 Pi 4 first, Pi 5 later).
 */

#pragma once

/* ── UART ──────────────────────────────────────────────────────────
 * BCM2712 PL011 UART0, 48 MHz UARTCLK.
 * Base address from BCM2712 preliminary peripheral documentation.
 */
#define BOARD_UART0_BASE  0x107D001000UL
#define BOARD_UART_TYPE   UART_TYPE_PL011
