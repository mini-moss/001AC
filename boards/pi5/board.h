/*
 * board.h — Raspberry Pi 5 (BCM2712) 板级配置
 *
 * Phase 2+ 占位。BCM2712 使用与 BCM2711 不同的 MMIO 映射
 * （如 UART0 从 0xFE20_1000 移到 0x1_07D0_0100_0000）。
 *
 * 本文件在 Phase 1 构建中未使用 —— Pi 5 目标按 dev-log/006
 * 决策 2 推迟（AArch64 Pi 4 优先，Pi 5 稍后）。
 */

#pragma once

/* ── UART ──────────────────────────────────────────────────────────
 * BCM2712 PL011 UART0，48 MHz UARTCLK。
 * 基地址来自 BCM2712 初步外设文档。
 */
#define BOARD_UART0_BASE  0x107D001000UL
#define BOARD_UART_TYPE   UART_TYPE_PL011
