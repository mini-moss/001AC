/*
 * vector.S — AArch64 EL1 exception vector table
 *
 * ARMv8 requires a 16-entry vector table at VBAR_EL1, 2 KiB aligned,
 * with each entry exactly 0x80 bytes.
 *
 * Entries that matter for Phase 1.3 (EL1, SP_ELx selected):
 *   +0x200  Synchronous  — unexpected; read ESR and hang
 *   +0x280  IRQ           — timer IRQ; save context, call C, restore, eret
 *
 * Phase 3+ will add entries +0x400/+0x480 for user-space exceptions.
 */

.section .text.vectors, "ax"

/* ── Vector table ─────────────────────────────────────────────────────
 * Each entry is 0x80 bytes (32 instructions).  Unused entries spin
 * forever; implemented entries branch to a handler further down.
 */
.balign 0x800
.globl vector_table_el1
vector_table_el1:

    /* ── Entries 0–3: from SP_EL0 (not used — we run at SP_EL1) ──── */
    b    .
    .balign 0x80
    b    .
    .balign 0x80
    b    .
    .balign 0x80
    b    .
    .balign 0x80

    /* ── Entry 4: Synchronous from current EL with SP_ELx (+0x200) ─ */
    b    el1_sync_handler
    .balign 0x80

    /* ── Entry 5: IRQ from current EL with SP_ELx (+0x280) ────────── */
    b    el1_irq_handler
    .balign 0x80

    /* ── Entry 6: FIQ from current EL with SP_ELx (+0x300) ────────── */
    b    .
    .balign 0x80

    /* ── Entry 7: SError from current EL with SP_ELx (+0x380) ─────── */
    b    .
    .balign 0x80

    /* ── Entries 8–11: from AArch64 lower EL (user space, Phase 3+) ── */
    b    .
    .balign 0x80
    b    .
    .balign 0x80
    b    .
    .balign 0x80
    b    .
    .balign 0x80

    /* ── Entries 12–15: from AArch32 lower EL (not used) ───────────── */
    b    .
    .balign 0x80
    b    .
    .balign 0x80
    b    .
    .balign 0x80
    b    .
    .balign 0x80

/* ══════════════════════════════════════════════════════════════════════
 * EL1 Synchronous exception handler
 *
 * Called on unexpected exceptions (undefined insn, data abort, etc.).
 * Reads ESR_EL1 and FAR_EL1 to report the cause, then parks the CPU.
 * ══════════════════════════════════════════════════════════════════════ */
el1_sync_handler:
    /*
     * Save x0-x3 so we can use them as scratch, then call the C handler.
     * The C handler takes (esr, far, elr) and never returns.
     */
    sub  sp, sp, #(8 * 5)        /* space for x0-x3 + lr */
    stp  x0, x1, [sp, #0]
    stp  x2, x3, [sp, #16]
    str  x30, [sp, #32]

    mrs  x0, esr_el1             /* arg0: Exception Syndrome Register */
    mrs  x1, far_el1             /* arg1: Fault Address Register */
    mrs  x2, elr_el1             /* arg2: Exception Link Register */
    bl   sync_trap_handler       /* C handler (never returns) */

    /* Should not reach here */
    b    .

/* ══════════════════════════════════════════════════════════════════════
 * EL1 IRQ exception handler
 *
 * Saves the caller-save registers + exception state on the kernel stack,
 * calls irq_trap_handler() in C, restores everything, and returns via eret.
 * ══════════════════════════════════════════════════════════════════════ */
el1_irq_handler:
    /*
     * Build a trap frame on the stack.  Layout (bottom → top):
     *   x0–x18     (19 regs)
     *   x29 (FP), x30 (LR)
     *   elr_el1
     *   spsr_el1
     *
     * 24 registers × 8 bytes = 192 bytes.  192 is 16-byte aligned.
     */
    sub  sp, sp, #192

    /* Save caller-save registers */
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

    /* Save frame pointer and link register */
    stp  x29, x30, [sp, #(8 * 19)]

    /* Save ELR_EL1 and SPSR_EL1 (exception return state) */
    mrs  x21, elr_el1
    mrs  x22, spsr_el1
    stp  x21, x22, [sp, #(8 * 21)]

    /* ── Call C handler ─────────────────────────────────────────── */
    mov  x0, sp                  /* arg0: pointer to trap_frame */
    bl   irq_trap_handler

    /* ── Restore exception return state ─────────────────────────── */
    ldp  x21, x22, [sp, #(8 * 21)]
    msr  elr_el1, x21
    msr  spsr_el1, x22

    /* Restore caller-save registers */
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
