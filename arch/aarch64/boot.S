/*
 * boot.S — AArch64 启动桩
 *
 * CPU 上电后执行的第一个文件。职责：
 *   1. 把非主核（CPU 1–3）park 住（v1.0 不做 SMP）
 *   2. 从 EL3/EL2 → EL1 逐级降特权（仅 CPU 0）
 *   3. 清零 BSS（C 语言要求）
 *   4. 设栈指针，跳转 kernel_main()
 *
 * QEMU raspi4b 在 EL3 启动 4 个核心（Cortex-A72 ×4）；
 * 真机 Pi 4 也由 GPU 固件在 EL3 释放所有 CPU。
 *
 * Phase 1 简化：EL3 → EL1 直达，不经过 EL2（不需要虚拟化）。
 * 如果启动时已经在 EL2，则 EL2 → EL1。
 */

.section .text._start       /* link.ld 的 KEEP 会把这节钉在 .text 最前面 */
.globl _start

_start:
    /* ── 0. 只让主核 (CPU 0) 继续，其余 park ─────────── */
    mrs  x1, mpidr_el1       /* MPIDR_EL1.Aff0 = CPU 编号 */
    and  x1, x1, #0xff
    cbnz x1, park_cpu         /* CPU 1/2/3 → 休眠 */

    /* ── 1. 检测当前异常级别，降到 EL1 ────────────────── */
    mrs  x0, CurrentEL
    cmp  x0, #(3 << 2)       /* EL3 → 直达 EL1 */
    beq  el3_to_el1
    cmp  x0, #(2 << 2)       /* EL2 → EL1 */
    beq  el2_to_el1
    cmp  x0, #(1 << 2)       /* 已在 EL1 → 直接初始化 */
    beq  setup_el1
    b    .                    /* 未知级别 → 死循环 */

/* ── 非主核休眠 ──────────────────────────────────────────────── */
park_cpu:
    wfe
    b    park_cpu

/* ── EL3 → EL1 直达 ─────────────────────────────────────────── */
el3_to_el1:
    /* SCR_EL3: 配置 EL1 为 AArch64 */
    mov  x0, #(1 << 10)       /* RW = 1: EL1 是 AArch64 */
    orr  x0, x0, #(1 << 3)   /* EA = 1: RES1（必须写 1） */
    msr  scr_el3, x0

    /* SPSR_EL3: 切换到 EL1h（EL1 + 专用 SP） */
    mov  x0, #0x3c5           /* M[3:0] = EL1h, DAIF 全屏蔽 */
    msr  spsr_el3, x0

    adr  x0, setup_el1
    msr  elr_el3, x0
    eret

/* ── EL2 → EL1 ──────────────────────────────────────────────── */
el2_to_el1:
    /* HCR_EL2: 配置 EL1 为 AArch64 */
    mov  x0, #(1 << 31)       /* RW = 1: EL1 是 AArch64 */
    msr  hcr_el2, x0

    mov  x0, #0x3c5           /* SPSR: EL1h, DAIF 全屏蔽 */
    msr  spsr_el2, x0
    adr  x0, setup_el1
    msr  elr_el2, x0
    eret

/* ── EL1 初始化 ──────────────────────────────────────────────── */
setup_el1:
    /* 设栈指针（link.ld 定义的 __stack_top） */
    ldr  x0, =__stack_top
    mov  sp, x0

    /* ── 清零 BSS（link.ld 定义了起止地址） ──────── */
    ldr  x0, =__bss_start
    ldr  x1, =__bss_end
    mov  x2, #0
1:
    cmp  x0, x1
    bge  2f
    str  x2, [x0], #8
    b    1b
2:

    /* ── 进入 C 代码 ──────────────────────────────── */
    bl   kernel_main

    /* kernel_main 不应该返回 */
    b    .
