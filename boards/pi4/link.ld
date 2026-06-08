/*
 * link.ld — Raspberry Pi 4 (BCM2711 / Cortex-A72) linker script
 *
 * Memory layout:
 *   0x80000 ─── .text      (code, read-only)
 *            ├── .rodata    (read-only data: strings, const globals)
 *            ├── .data      (initialized globals)
 *            └── .bss       (zero-initialized globals + 64 KB stack)
 *
 * The GPU firmware (and QEMU -kernel) loads the image at 0x80000.
 */

ENTRY(_start)

SECTIONS
{
    /* QEMU raspi4b 加载内核的基地址 */
    . = 0x80000;

    /* ── 代码段 ─────────────────────────────────────────── */
    .text : {
        /*
         * KEEP 保证 _start 不会被 --gc-sections 丢弃。
         * 把 .text._start 放在最前面，确保 _start 是映像的第一个字节。
         */
        KEEP(*(.text._start))
        *(.text*)
    }

    /* ── 只读数据（字符串字面量、const 变量） ──────────── */
    .rodata : {
        *(.rodata*)
    }

    /* ── 已初始化数据 ───────────────────────────────────── */
    .data : {
        *(.data*)
    }

    /* ── 未初始化数据（C 中所有 static/global 默认为 0） ── */
    .bss : {
        . = ALIGN(16);           /* AArch64 要求 16 字节对齐 */
        __bss_start = .;         /* boot.S 用这个地址清零 BSS */
        *(.bss*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;

        /* 64 KB 初始内核栈，放在 BSS 末尾 */
        . = ALIGN(16);
        . += 65536;
        . = ALIGN(16);
        __stack_top = .;         /* SP 从这里往下长 */
    }

    /* 丢弃不需要的 section（调试信息在 ELF 里，不影响二进制） */
    /DISCARD/ : {
        *(.comment*)
        *(.gnu*)
        *(.note*)
    }
}
