/*
 * printk.c — 最小内核 printf
 *
 * 逐字符将格式化输出写入 uart_putc()。
 * 无堆分配，无浮点，小栈 footprint。
 */

#include "kernel/printk.h"
#include "kernel/uart.h"

/* ── 十六进制数字查找表 ──────────────────────────────── */
static const char g_hex_digits[] = "0123456789abcdef";

/* ── 内部函数：写单个字符 ────────────────────────────── */
static void put(char c)
{
    /* 将 '\n' 转换为 "\r\n" 以适应原始串行终端 */
    if (c == '\n')
        uart_putc('\r');
    uart_putc(c);
}

/* ── 内部函数：写无符号整数的十进制表示 ────────────────── */
static void put_unsigned(uint64_t val)
{
    char buf[21];   /* 2^64 = 18446744073709551616 → 20 位 + NUL */
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

/* ── 内部函数：写无符号整数的十六进制表示 ────────────────────── */
static void put_hex(uint64_t val)
{
    /*
     * 打印 64 位宽度的前导零。
     * 对于 Phase 1 中常见的 32 位值，前导零使地址
     * （如 0xFE201000）一目了然。
     */
    for (int shift = 60; shift >= 0; shift -= 4)
        put(g_hex_digits[(val >> shift) & 0xf]);
}

/* ── 简易 vsnprintf 核心 ─────────────────────────────────────────── */
static void vprintk(const char *fmt, __builtin_va_list ap)
{
    for (const char *p = fmt; *p != '\0'; p++) {
        if (*p != '%') {
            put(*p);
            continue;
        }

        p++;  /* 跳过 '%' */
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
            /* 未知格式说明符 —— 按字面量打印 */
            put('%');
            put(*p);
            break;
        }
    }
}

/* ── 公开 API ────────────────────────────────────────────────────── */
void printk(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vprintk(fmt, ap);
    __builtin_va_end(ap);
}
