/*
 * board.h — Raspberry Pi 4 (BCM2711) 板级配置
 *
 * 本文件是每块板不同硬件参数的唯一真实来源：
 * MMIO 基地址、外设类型、内存布局等。
 *
 * Phase 1 范围：仅 UART。更多条目（GIC、timer、RAM）将在
 * Phase 2 HAL 层正式化时添加。
 */

#pragma once

/* ── UART ──────────────────────────────────────────────────────────
 * BCM2711 有两个 PL011 UART。UART0 引出到 GPIO 14/15。
 * 基地址来自 BCM2711 ARM Peripherals 手册 §2.1。
 */
#define BOARD_UART0_BASE  0xFE201000UL
#define BOARD_UART_TYPE   UART_TYPE_PL011
