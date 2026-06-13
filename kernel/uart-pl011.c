/*
 * uart-pl011.c — PL011 UART 驱动（ARM PrimeCell）
 *
 * 支持：Raspberry Pi 4 (BCM2711)、Raspberry Pi 5 (BCM2712)。
 *
 * PL011 是标准 PrimeCell UART，具有 16 条目 TX/RX FIFO。
 * 寄存器布局在 BCM SoC 间稳定；仅基地址不同。
 */

#include "uart.h"

/* ── 选择板级头文件 ───────────────────────────────────────
 * Phase 1 通过编译参数 -DBOARD_XXX 选择。
 * Makefile 根据 BOARD 变量选择对应的 UART 驱动文件。
 */
#if defined(BOARD_PI5)
#  include "../boards/pi5/board.h"
#else
/* 默认：Pi 4 —— Phase 1 主要目标。
 * 编译 Raspberry Pi 5 时传 -DBOARD_PI5。 */
#  include "../boards/pi4/board.h"
#endif

/* ── 硬件寄存器映射（PL011） ────────────────────────────────── */
struct pl011 {
    volatile uint32_t DR;       /* 0x00: 数据寄存器（读=RX，写=TX） */
    volatile uint32_t RSR;      /* 0x04: 接收状态 / 错误清除          */
    uint32_t _pad0[4];          /* 0x08–0x14: 保留                   */
    volatile uint32_t FR;       /* 0x18: 标志寄存器                   */
    uint32_t _pad1[1];          /* 0x1C                                */
    volatile uint32_t ILPR;     /* 0x20: IrDA 低功耗计数器（未使用）  */
    volatile uint32_t IBRD;     /* 0x24: 整数波特率除数               */
    volatile uint32_t FBRD;     /* 0x28: 小数波特率除数               */
    volatile uint32_t LCR_H;    /* 0x2C: 行控制寄存器                 */
    volatile uint32_t CR;       /* 0x30: 控制寄存器                   */
    volatile uint32_t IFLS;     /* 0x34: FIFO 中断级别选择            */
    volatile uint32_t IMSC;     /* 0x38: 中断屏蔽置位 / 清除         */
    volatile uint32_t RIS;      /* 0x3C: 原始中断状态                 */
    volatile uint32_t MIS;      /* 0x40: 已屏蔽中断状态               */
    volatile uint32_t ICR;      /* 0x44: 中断清除                     */
    volatile uint32_t DMACR;    /* 0x48: DMA 控制                     */
};

#define UART0  ((volatile struct pl011 *)BOARD_UART0_BASE)

/* ── FR（标志寄存器）位 ──────────────────────────────────────── */
#define FR_TXFF  (1 << 5)   /* TX FIFO 满  — 写之前必须等待         */
#define FR_RXFE  (1 << 4)   /* RX FIFO 空 — 无可用数据              */

/* ── CR（控制寄存器）位 ───────────────────────────────────── */
#define CR_UARTEN  (1 << 0)   /* UART 启用    */
#define CR_TXE     (1 << 8)   /* 发送启用      */
#define CR_RXE     (1 << 9)   /* 接收启用      */

/* ── LCR_H 位 ───────────────────────────────────────────────────── */
#define LCR_H_WLEN_8  (3 << 5)   /* 8 位字长  */
#define LCR_H_FEN     (1 << 4)   /* FIFO 启用  */

/* ── uart_putc ────────────────────────────────────────────────────── */
void uart_putc(char c)
{
    /* 等待 TX FIFO 不满 */
    while (UART0->FR & FR_TXFF)
        ;
    UART0->DR = (uint32_t)c;
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

/* ── uart_init ────────────────────────────────────────────────────── */
void uart_init(void)
{
    /* 1. 修改配置前先禁用 UART */
    UART0->CR = 0;

    /*
     * 2. 设置波特率 = 115200。
     *
     *    公式：BAUDDIV = UARTCLK / (16 × 波特率)
     *    假设 UARTCLK = 48 MHz（Pi 4 默认）：
     *      BAUDDIV = 48_000_000 / (16 × 115200) ≈ 26.04167
     *      IBRD = 26,  FBRD = round(0.04167 × 64) = 3
     *
     *    QEMU 中时钟树不是周期精确的 —— 字节直接转发到
     *    宿主机终端，因此这些值仅为未来真机验证时
     *    波特率正确性而设。
     */
    UART0->IBRD = 26;
    UART0->FBRD = 3;

    /* 3. 8 位字长 + FIFO 启用 */
    UART0->LCR_H = LCR_H_WLEN_8 | LCR_H_FEN;

    /* 4. 启用 UART + TX + RX */
    UART0->CR = CR_UARTEN | CR_TXE | CR_RXE;
}
