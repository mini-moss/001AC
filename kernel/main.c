/*
 * main.c — 内核 C 入口
 *
 * 从 boot.S 跳转过来的第一个 C 函数。此时：
 *   - CPU 已在 EL1（内核态）
 *   - 栈已设置
 *   - BSS 已清零
 *   - MMU 未开（直接操作物理地址）
 *
 * 当前唯一任务：往 UART 输出 "hello"，验证整个工具链 + QEMU 链路跑通。
 *
 * UART 硬件细节（基地址、控制器类型）由 boards/<board>/board.h 提供，
 * 驱动代码在 uart-pl011.c / uart-ns16550.c 中。main.c 只依赖 uart.h
 * 的公共接口，不碰硬件寄存器。
 */

#include "uart.h"

/* ── 内核入口 ──────────────────────────────────────────────────── */
void kernel_main(void)
{
    uart_init();
    uart_puts("hello\n");

    /* 内核永不退出 */
    while (1)
        ;
}
