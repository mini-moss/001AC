/*
 * uart.h — Phase 1 最小 UART 抽象层
 *
 * 本头文件定义：
 *   1. 各 board.h 使用的 UART_TYPE_* 常量
 *   2. 公开接口：uart_init()、uart_putc()、uart_puts()
 *
 * 每块板通过自己的 <board.h> 提供 BOARD_UART0_BASE 和 BOARD_UART_TYPE，
 * 编译时通过 -DBOARD_XXX 选择。
 *
 * 只有一个编译单元（uart-pl011.c 或 uart-ns16550.c）被链接进内核 ——
 * 取决于目标板的 BOARD_UART_TYPE。
 */

#pragma once

#include <stdint.h>

/* ── UART 控制器类型常量 ──────────────────────────────── */
#define UART_TYPE_PL011    1
#define UART_TYPE_NS16550  2

/* ── 公开接口 ─────────────────────────────────────────────── */

/* 初始化 UART：禁用、配置波特率/字长/FIFO、启用 TX+RX。 */
void uart_init(void);

/* 输出单个字符（忙等待直到 TX FIFO 可接受）。 */
void uart_putc(char c);

/* 输出 NUL 结尾的字符串。'\n' 展开为 "\r\n"。 */
void uart_puts(const char *s);
