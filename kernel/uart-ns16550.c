/*
 * uart-ns16550.c — NS16550 兼容 UART 驱动
 *
 * 支持：QEMU RISC-V 'virt' 机器，以及任何
 *       BOARD_UART_TYPE == UART_TYPE_NS16550 的板子。
 *
 * NS16550 是经典 PC 风格 UART（8250 家族）。QEMU 的
 * `virt` 机器在 0x1000_0000 连接了一个。与 PL011 不同，
 * 寄存器偏移、位定义和初始化序列完全不同。
 *
 * Phase 1 范围：仅 TX（无 RX、无中断、无 FIFO 调优）。
 */

#include "uart.h"

/* ── 选择板级头文件 ─────────────────────────────────────── */
/* 默认：QEMU RISC-V 'virt' —— Phase 1 唯一的 NS16550 目标。
 * 当 SG2002 或其他 NS16550 板子加入时，添加 #if defined(BOARD_XXX)。 */
#include "../boards/virt-riscv/board.h"

/* ── 硬件寄存器映射（NS16550） ────────────────────────────────
 * 真机上寄存器为 8 位宽，但 QEMU 在 `virt` 机器中将它们映射为
 * 32 位 MMIO（字节访问也可用；为简单起见我们使用 32 位写）。
 */
struct ns16550 {
    volatile uint32_t THR;   /* 0x00: 发送保持寄存器（写）              */
                             /*       同时也是 RBR: 接收缓冲（读）      */
                             /*       同时也是 DLL: 除数锁存 LSB       */
    volatile uint32_t IER;   /* 0x04: 中断启用寄存器                    */
                             /*       同时也是 DLM: 除数锁存 MSB       */
    volatile uint32_t IIR;   /* 0x08: 中断识别（读）                   */
                             /*       同时也是 FCR: FIFO 控制（写）    */
    volatile uint32_t LCR;   /* 0x0C: 行控制寄存器                      */
    volatile uint32_t MCR;   /* 0x10: 调制解调器控制寄存器              */
    volatile uint32_t LSR;   /* 0x14: 行状态寄存器                      */
    volatile uint32_t MSR;   /* 0x18: 调制解调器状态寄存器              */
    volatile uint32_t SCR;   /* 0x1C: 暂存寄存器                        */
};

#define UART0  ((volatile struct ns16550 *)BOARD_UART0_BASE)

/* ── LSR（行状态寄存器）位 ──────────────────────────────── */
#define LSR_THRE  (1 << 5)   /* 发送保持寄存器为空 —
                               * 置位时可写入下一个字节。             */
#define LSR_TEMT  (1 << 6)   /* 发送器为空 — THR 和移位寄存器
                               * 均为空（TX 不需要此位）。            */

/* ── LCR（行控制寄存器）位 ─────────────────────────────── */
#define LCR_DLAB  (1 << 7)   /* 除数锁存访问位 —
                               * 置位以访问 DLL/DLM 进行波特率配置。 */
#define LCR_WLEN8 (3 << 0)   /* 8 位字长（位 0-1 = 0b11）。          */

/* ── IER 位 ─────────────────────────────────────────────────────── */
#define IER_DISABLE  0x00    /* 全部中断关闭。                         */

/* ── uart_putc ────────────────────────────────────────────────────── */
void uart_putc(char c)
{
    /* 等待 THR 为空（可接受下一个字节）。 */
    while (!(UART0->LSR & LSR_THRE))
        ;
    UART0->THR = (uint32_t)c;
}

/* ── uart_puts ────────────────────────────────────────────────────── */
void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            uart_putc('\r');
        uart_putc(*s++);
    }
}

/* ── uart_init ──────────────────────────────────────────────────────
 *
 * NS16550 初始化序列（最小化，QEMU 验证）：
 *   1. 禁用中断（IER = 0）。
 *   2. 置位 DLAB 以访问除数寄存器。
 *   3. 编程除数为 115200 波特率（假设 1.8432 MHz 时钟，
 *      QEMU virt 默认值 —— 但 QEMU 不仿真真实时序，
 *      因此任何合理的除数均可工作）。
 *   4. 清除 DLAB，设置 8N1。
 *
 *   与 PL011 不同，NS16550 没有显式的 UART 启用位 —— UART
 *   在寄存器编程后即"在线"。
 */
void uart_init(void)
{
    /* 1. 禁用所有中断 */
    UART0->IER = IER_DISABLE;

    /*
     * 2. 设置波特率 = 115200。
     *
     *    置位 DLAB = 1 以访问 DLL/DLM 除数寄存器。
     *    除数 = UARTCLK / (16 × 波特率)
     *         = 1_843_200 / (16 × 115200)
     *         = 1_843_200 / 1_843_200
     *         = 1
     *    DLL = 1, DLM = 0。
     *
     *    QEMU virt 不仿真真实 UART 时钟 —— 字节直达宿主机
     *    chardev —— 因此这些值仅为真 NS16550 硬件正确性而设。
     */
    UART0->LCR = LCR_DLAB;          /* DLAB=1 → THR/IER 变为 DLL/DLM */
    UART0->THR = 1;                 /* DLL = 除数 & 0xFF               */
    UART0->IER = 0;                 /* DLM = (除数 >> 8) & 0xFF        */

    /* 3. 8 位字长，无校验，1 停止位，清除 DLAB */
    UART0->LCR = LCR_WLEN8;

    /*
     * 4. NS16550 无显式"启用 TX"位 —— 写 THR 总是发送。
     *    UART 立即可用。
     */
}
