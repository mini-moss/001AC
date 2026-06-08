/*
 * uart-pl011.c — PL011 UART driver (ARM PrimeCell)
 *
 * Supports: Raspberry Pi 4 (BCM2711), Raspberry Pi 5 (BCM2712).
 *
 * The PL011 is a standard PrimeCell UART with 16-entry TX/RX FIFOs.
 * Register layout is stable across BCM SoCs; only the base address changes.
 */

#include "uart.h"

/* ── Select the board header ───────────────────────────────────────
 * In Phase 1 (no Makefile), the build command passes -DBOARD_XXX.
 * When the Makefile lands, this block will be replaced by a single
 * `-include boards/$(BOARD)/board.h` compiler flag.
 */
#if defined(BOARD_PI5)
#  include "../boards/pi5/board.h"
#else
/* Default: Pi 4 — the Phase 1 primary target.
 * Pass -DBOARD_PI5 to build for Raspberry Pi 5. */
#  include "../boards/pi4/board.h"
#endif

/* ── Hardware register map (PL011) ────────────────────────────────── */
struct pl011 {
    volatile uint32_t DR;       /* 0x00: Data Register (read=RX, write=TX) */
    volatile uint32_t RSR;      /* 0x04: Receive Status / Error Clear       */
    uint32_t _pad0[4];          /* 0x08–0x14: reserved                      */
    volatile uint32_t FR;       /* 0x18: Flag Register                      */
    uint32_t _pad1[1];          /* 0x1C                                     */
    volatile uint32_t ILPR;     /* 0x20: IrDA low-power counter (unused)    */
    volatile uint32_t IBRD;     /* 0x24: Integer Baud Rate Divisor          */
    volatile uint32_t FBRD;     /* 0x28: Fractional Baud Rate Divisor       */
    volatile uint32_t LCR_H;    /* 0x2C: Line Control Register              */
    volatile uint32_t CR;       /* 0x30: Control Register                   */
    volatile uint32_t IFLS;     /* 0x34: FIFO Interrupt Level Select        */
    volatile uint32_t IMSC;     /* 0x38: Interrupt Mask Set / Clear         */
    volatile uint32_t RIS;      /* 0x3C: Raw Interrupt Status               */
    volatile uint32_t MIS;      /* 0x40: Masked Interrupt Status            */
    volatile uint32_t ICR;      /* 0x44: Interrupt Clear                    */
    volatile uint32_t DMACR;    /* 0x48: DMA Control                        */
};

#define UART0  ((volatile struct pl011 *)BOARD_UART0_BASE)

/* ── FR (Flag Register) bits ──────────────────────────────────────── */
#define FR_TXFF  (1 << 5)   /* TX FIFO full  — must wait before write  */
#define FR_RXFE  (1 << 4)   /* RX FIFO empty — no data available       */

/* ── CR (Control Register) bits ───────────────────────────────────── */
#define CR_UARTEN  (1 << 0)   /* UART enable      */
#define CR_TXE     (1 << 8)   /* Transmit enable   */
#define CR_RXE     (1 << 9)   /* Receive enable    */

/* ── LCR_H bits ───────────────────────────────────────────────────── */
#define LCR_H_WLEN_8  (3 << 5)   /* 8-bit word length */
#define LCR_H_FEN     (1 << 4)   /* FIFO enable       */

/* ── uart_putc ────────────────────────────────────────────────────── */
void uart_putc(char c)
{
    /* Wait until TX FIFO is not full */
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
    /* 1. Disable UART before changing configuration */
    UART0->CR = 0;

    /*
     * 2. Set baud rate = 115200.
     *
     *    Formula: BAUDDIV = UARTCLK / (16 × baud_rate)
     *    Assuming UARTCLK = 48 MHz (Pi 4 default):
     *      BAUDDIV = 48_000_000 / (16 × 115200) ≈ 26.04167
     *      IBRD = 26,  FBRD = round(0.04167 × 64) = 3
     *
     *    In QEMU the clock tree is not cycle-accurate — bytes are
     *    forwarded directly to the host terminal, so these values
     *    are for correctness in case a future real-machine check
     *    cares about baud rate.
     */
    UART0->IBRD = 26;
    UART0->FBRD = 3;

    /* 3. 8-bit word + FIFO enable */
    UART0->LCR_H = LCR_H_WLEN_8 | LCR_H_FEN;

    /* 4. Enable UART + TX + RX */
    UART0->CR = CR_UARTEN | CR_TXE | CR_RXE;
}
