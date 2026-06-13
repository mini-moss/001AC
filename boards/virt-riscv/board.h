/*
 * board.h — QEMU RISC-V 'virt' 机器板级配置
 *
 * QEMU 的 `-machine virt` 是一个通用 RISC-V 64 位平台。
 * 它不对应任何真实 SoC；存在目的是在真实芯片可用之前
 * 给固件/OS 开发者一个标准目标。
 *
 * SG2002（C906）没有原生 QEMU 模型，因此 Phase 1–2 的 RISC-V
 * 开发使用 `virt`。当真 SG2002 硬件就绪时（Phase 2 末），
 * 单独的 boards/sg2002/board.h 将替换本文件用于真机构建。
 */

#pragma once

/* ── UART ──────────────────────────────────────────────────────────
 * QEMU 'virt' 默认在 0x1000_0000 连接一个 NS16550 兼容 UART。
 * QEMU 中时钟频率无关 —— 模型仅将字节转发到宿主机终端。
 */
#define BOARD_UART0_BASE  0x10000000UL
#define BOARD_UART_TYPE   UART_TYPE_NS16550
