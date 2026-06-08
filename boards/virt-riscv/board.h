/*
 * board.h — QEMU RISC-V 'virt' machine board-level configuration
 *
 * QEMU's `-machine virt` is a generic RISC-V 64-bit platform.  It does
 * NOT correspond to any real SoC; it exists to give firmware / OS devs a
 * standard target before real silicon is available.
 *
 * The SG2002 (C906) does not have a native QEMU model, so we use `virt`
 * for Phase 1–2 RISC-V development.  When real SG2002 hardware arrives
 * (Phase 2 end), a separate boards/sg2002/board.h will replace this file
 * for real-machine builds.
 */

#pragma once

/* ── UART ──────────────────────────────────────────────────────────
 * QEMU 'virt' defaults to an NS16550-compatible UART at 0x1000_0000.
 * The clock frequency is irrelevant in QEMU — the model just forwards
 * bytes to the host terminal.
 */
#define BOARD_UART0_BASE  0x10000000UL
#define BOARD_UART_TYPE   UART_TYPE_NS16550
