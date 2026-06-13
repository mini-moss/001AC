/*
 * printk.c — minimal kernel printf
 *
 * Writes formatted output character-by-character to uart_putc().
 * No heap allocation, no floating point, small stack footprint.
 */

#include "kernel/printk.h"
#include "kernel/uart.h"

/* ── Hex digit lookup ──────────────────────────────────────────────── */
static const char g_hex_digits[] = "0123456789abcdef";

/* ── Internal: write a single character ────────────────────────────── */
static void put(char c)
{
    /* Convert '\n' to "\r\n" for raw serial terminals */
    if (c == '\n')
        uart_putc('\r');
    uart_putc(c);
}

/* ── Internal: write an unsigned integer in base 10 ────────────────── */
static void put_unsigned(uint64_t val)
{
    char buf[21];   /* 2^64 = 18446744073709551616 → 20 digits + NUL */
    int i = 0;

    if (val == 0) {
        put('0');
        return;
    }

    while (val > 0) {
        buf[i++] = '0' + (char)(val % 10);
        val /= 10;
    }

    while (i-- > 0)
        put(buf[i]);
}

/* ── Internal: write an unsigned integer in hex ────────────────────── */
static void put_hex(uint64_t val)
{
    /*
     * Print leading zeros for the full 64-bit width.
     * For 32-bit values common in Phase 1, the leading zeros make
     * addresses (e.g. 0xFE201000) readable at a glance.
     */
    for (int shift = 60; shift >= 0; shift -= 4)
        put(g_hex_digits[(val >> shift) & 0xf]);
}

/* ── Simple vsnprintf core ─────────────────────────────────────────── */
static void vprintk(const char *fmt, __builtin_va_list ap)
{
    for (const char *p = fmt; *p != '\0'; p++) {
        if (*p != '%') {
            put(*p);
            continue;
        }

        p++;  /* skip '%' */
        switch (*p) {
        case '\0':
            return;

        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (s == ((void *)0))
                s = "(null)";
            while (*s)
                put(*s++);
            break;
        }

        case 'd': {
            int64_t val = __builtin_va_arg(ap, int64_t);
            if (val < 0) {
                put('-');
                val = -val;
            }
            put_unsigned((uint64_t)val);
            break;
        }

        case 'u':
            put_unsigned(__builtin_va_arg(ap, uint64_t));
            break;

        case 'x':
            put_hex(__builtin_va_arg(ap, uint64_t));
            break;

        case 'c':
            put((char)__builtin_va_arg(ap, int));
            break;

        case '%':
            put('%');
            break;

        default:
            /* Unknown format specifier — print it literally */
            put('%');
            put(*p);
            break;
        }
    }
}

/* ── Public API ────────────────────────────────────────────────────── */
void printk(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vprintk(fmt, ap);
    __builtin_va_end(ap);
}
