/*
 * vector.S — AArch64 EL1 异常向量表
 *
 * ARMv8 要求在 VBAR_EL1 处放置 16 条目向量表，对齐 2 KiB，
 * 每个条目恰好 0x80 字节。
 *
 * Phase 1.3 需要的条目（EL1，SP_ELx 选择）：
 *   +0x200  同步异常  — 非预期异常；读取 ESR 后挂起
 *   +0x280  IRQ       — timer IRQ；保存上下文，调用 C，恢复，eret
 *
 * Phase 3+ 将为用户态异常添加 +0x400/+0x480 条目。
 */

.section .text.vectors, "ax"

/* ── 向量表 ─────────────────────────────────────────────────────
 * 每个条目 0x80 字节（32 条指令）。未使用的条目永久 spin；
 * 实现的条目跳转到下方的处理器。
 */
.balign 0x800
.globl vector_table_el1
vector_table_el1:

    /* ── 条目 0–3：来自 SP_EL0（不使用 — 我们运行在 SP_EL1） ──── */
    b    .
    .balign 0x80
    b    .
    .balign 0x80
    b    .
    .balign 0x80
    b    .
    .balign 0x80

    /* ── 条目 4：来自当前 EL 且使用 SP_ELx 的同步异常 (+0x200) ─ */
    b    el1_sync_handler
    .balign 0x80

    /* ── 条目 5：来自当前 EL 且使用 SP_ELx 的 IRQ (+0x280) ────────── */
    b    el1_irq_handler
    .balign 0x80

    /* ── 条目 6：来自当前 EL 且使用 SP_ELx 的 FIQ (+0x300) ────────── */
    b    .
    .balign 0x80

    /* ── 条目 7：来自当前 EL 且使用 SP_ELx 的 SError (+0x380) ─────── */
    b    .
    .balign 0x80

    /* ── 条目 8–11：来自 AArch64 低异常级别的异常（用户态，Phase 3+） ── */
    b    .
    .balign 0x80
    b    .
    .balign 0x80
    b    .
    .balign 0x80
    b    .
    .balign 0x80

    /* ── 条目 12–15：来自 AArch32 低异常级别的异常（不使用） ───────────── */
    b    .
    .balign 0x80
    b    .
    .balign 0x80
    b    .
    .balign 0x80
    b    .
    .balign 0x80

/* ══════════════════════════════════════════════════════════════════════
 * EL1 同步异常处理器
 *
 * 在非预期异常（未定义指令、数据 abort 等）时调用。
 * 读取 ESR_EL1 和 FAR_EL1 以报告原因，然后 park CPU。
 * ══════════════════════════════════════════════════════════════════════ */
el1_sync_handler:
    /*
     * 保存 x0-x3 作为临时寄存器，然后调用 C 处理器。
     * C 处理器接收 (esr, far, elr) 参数，永不返回。
     */
    sub  sp, sp, #(8 * 5)        /* 为 x0-x3 + lr 分配空间 */
    stp  x0, x1, [sp, #0]
    stp  x2, x3, [sp, #16]
    str  x30, [sp, #32]

    mrs  x0, esr_el1             /* arg0: Exception Syndrome Register */
    mrs  x1, far_el1             /* arg1: Fault Address Register */
    mrs  x2, elr_el1             /* arg2: Exception Link Register */
    bl   sync_trap_handler       /* C 处理器（永不返回） */

    /* 不应到达此处 */
    b    .

/* ══════════════════════════════════════════════════════════════════════
 * EL1 IRQ 异常处理器
 *
 * 在内核栈上保存调用者保存寄存器 + 异常状态，
 * 调用 C 函数 irq_trap_handler()，恢复所有寄存器，通过 eret 返回。
 * ══════════════════════════════════════════════════════════════════════ */
el1_irq_handler:
    /*
     * 在栈上构建 trap frame。布局（低 → 高）：
     *   x0–x18     （19 个寄存器）
     *   x29 (FP), x30 (LR)
     *   elr_el1
     *   spsr_el1
     *
     * 24 个寄存器 × 8 字节 = 192 字节。192 是 16 字节对齐。
     */
    sub  sp, sp, #192

    /* 保存调用者保存寄存器 */
    stp  x0,  x1,  [sp, #(8 * 0)]
    stp  x2,  x3,  [sp, #(8 * 2)]
    stp  x4,  x5,  [sp, #(8 * 4)]
    stp  x6,  x7,  [sp, #(8 * 6)]
    stp  x8,  x9,  [sp, #(8 * 8)]
    stp  x10, x11, [sp, #(8 * 10)]
    stp  x12, x13, [sp, #(8 * 12)]
    stp  x14, x15, [sp, #(8 * 14)]
    stp  x16, x17, [sp, #(8 * 16)]
    str  x18,      [sp, #(8 * 18)]

    /* 保存帧指针和链接寄存器 */
    stp  x29, x30, [sp, #(8 * 19)]

    /* 保存 ELR_EL1 和 SPSR_EL1（异常返回状态） */
    mrs  x21, elr_el1
    mrs  x22, spsr_el1
    stp  x21, x22, [sp, #(8 * 21)]

    /* ── 调用 C 处理器 ─────────────────────────────────────────── */
    mov  x0, sp                  /* arg0: trap_frame 指针 */
    bl   irq_trap_handler

    /* ── 恢复异常返回状态 ─────────────────────────────────────────── */
    ldp  x21, x22, [sp, #(8 * 21)]
    msr  elr_el1, x21
    msr  spsr_el1, x22

    /* 恢复调用者保存寄存器 */
    ldp  x0,  x1,  [sp, #(8 * 0)]
    ldp  x2,  x3,  [sp, #(8 * 2)]
    ldp  x4,  x5,  [sp, #(8 * 4)]
    ldp  x6,  x7,  [sp, #(8 * 6)]
    ldp  x8,  x9,  [sp, #(8 * 8)]
    ldp  x10, x11, [sp, #(8 * 10)]
    ldp  x12, x13, [sp, #(8 * 12)]
    ldp  x14, x15, [sp, #(8 * 14)]
    ldp  x16, x17, [sp, #(8 * 16)]
    ldr  x18,      [sp, #(8 * 18)]
    ldp  x29, x30, [sp, #(8 * 19)]

    add  sp, sp, #192
    eret
