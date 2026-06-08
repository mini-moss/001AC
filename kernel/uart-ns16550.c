/*
 * uart-ns16550.c — NS16550-compatible UART driver
 *
 * Supports: QEMU RISC-V 'virt' machine, and any board whose
 *           BOARD_UART_TYPE == UART_TYPE_NS16550.
 *
 * The NS16550 is the classic PC-style UART (8250 family).  QEMU's
 * `virt` machine wires one at 0x1000_0000.  Unlike PL011, the
 * register offsets, bit definitions, and init sequence are entirely
 * different.
 *
 * Phase 1 scope: TX only (no RX, no interrupts, no FIFO tuning).
 */

#include "uart.h"

/* ── Select the board header ─────────────────────────────────────── */
/* Default: QEMU RISC-V 'virt' — the only NS16550 target in Phase 1.
 * When SG2002 or other NS16550 boards arrive, add #if defined(BOARD_XXX). */
#include "../boards/virt-riscv/board.h"

/* ── Hardware register map (NS16550) ────────────────────────────────
 * Registers are 8-bit wide on real hardware, but QEMU maps them as
 * 32-bit MMIO in the `virt` machine (byte-access works too; we use
 * 32-bit writes for simplicity).
 */
struct ns16550 {
    volatile uint32_t THR;   /* 0x00: Transmit Holding Register (write)   */
                             /*       also RBR: Receive Buffer (read)     */
                             /*       also DLL: Divisor Latch LSB         */
    volatile uint32_t IER;   /* 0x04: Interrupt Enable Register           */
                             /*       also DLM: Divisor Latch MSB         */
    volatile uint32_t IIR;   /* 0x08: Interrupt Identification (read)     */
                             /*       also FCR: FIFO Control (write)      */
    volatile uint32_t LCR;   /* 0x0C: Line Control Register               */
    volatile uint32_t MCR;   /* 0x10: Modem Control Register              */
    volatile uint32_t LSR;   /* 0x14: Line Status Register                */
    volatile uint32_t MSR;   /* 0x18: Modem Status Register               */
    volatile uint32_t SCR;   /* 0x1C: Scratch Register                    */
};

#define UART0  ((volatile struct ns16550 *)BOARD_UART0_BASE)

/* ── LSR (Line Status Register) bits ──────────────────────────────── */
#define LSR_THRE  (1 << 5)   /* Transmit Holding Register Empty —
                               * set when we can write the next byte.  */
#define LSR_TEMT  (1 << 6)   /* Transmitter Empty — both THR and shift
                               * register are empty (not needed for TX). */

/* ── LCR (Line Control Register) bits ─────────────────────────────── */
#define LCR_DLAB  (1 << 7)   /* Divisor Latch Access Bit —
                               * set to access DLL/DLM for baud config. */
#define LCR_WLEN8 (3 << 0)   /* 8-bit word length (bits 0-1 = 0b11).    */

/* ── IER bits ─────────────────────────────────────────────────────── */
#define IER_DISABLE  0x00    /* All interrupts off.                       */

/* ── uart_putc ────────────────────────────────────────────────────── */
void uart_putc(char c)
{
    /* Wait until THR is empty (can accept another byte). */
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
 * NS16550 init sequence (minimal, QEMU-tested):
 *   1. Disable interrupts (IER = 0).
 *   2. Set DLAB to access divisor registers.
 *   3. Program divisor for 115200 baud (assuming 1.8432 MHz clock,
 *      the QEMU virt default — but QEMU doesn't simulate real timing,
 *      so any reasonable divisor works).
 *   4. Clear DLAB, set 8N1.
 *
 *   Unlike PL011, NS16550 has no explicit UART-enable bit — the UART
 *   is "live" as soon as registers are programmed.
 */
void uart_init(void)
{
    /* 1. Disable all interrupts */
    UART0->IER = IER_DISABLE;

    /*
     * 2. Set baud rate = 115200.
     *
     *    Set DLAB = 1 to access DLL/DLM divisor registers.
     *    Divisor = UARTCLK / (16 × baud)
     *           = 1_843_200 / (16 × 115200)
     *           = 1_843_200 / 1_843_200
     *           = 1
     *    DLL = 1, DLM = 0.
     *
     *    QEMU virt does not emulate a real UART clock — bytes go
     *    straight to the host chardev — so these values only matter
     *    for correctness on real NS16550 hardware.
     */
    UART0->LCR = LCR_DLAB;          /* DLAB=1 → THR/IER become DLL/DLM */
    UART0->THR = 1;                 /* DLL = divisor & 0xFF              */
    UART0->IER = 0;                 /* DLM = (divisor >> 8) & 0xFF       */

    /* 3. 8-bit word, no parity, 1 stop bit, clear DLAB */
    UART0->LCR = LCR_WLEN8;

    /*
     * 4. No explicit "enable TX" bit on NS16550 — writing to THR
     *    always transmits.  The UART is ready immediately.
     */
}
