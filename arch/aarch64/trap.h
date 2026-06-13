/*
 * trap.h — AArch64 exception trap frame and handler declarations
 *
 * The trap_frame struct mirrors the stack layout built by vector.S.
 */
#pragma once

#include <stdint.h>

/*
 * Register snapshot saved by the IRQ entry trampoline (vector.S).
 * Stack layout (bottom / lowest address → top):
 *   x0–x18, x29(FP), x30(LR), elr_el1, spsr_el1
 *
 * Keep in sync with arch/aarch64/vector.S!
 */
typedef struct {
    /* ── caller-save registers (saved by vector.S) ──────────── */
    uint64_t x0;
    uint64_t x1;
    uint64_t x2;
    uint64_t x3;
    uint64_t x4;
    uint64_t x5;
    uint64_t x6;
    uint64_t x7;
    uint64_t x8;
    uint64_t x9;
    uint64_t x10;
    uint64_t x11;
    uint64_t x12;
    uint64_t x13;
    uint64_t x14;
    uint64_t x15;
    uint64_t x16;
    uint64_t x17;
    uint64_t x18;
    /* ── frame pointer + link register ──────────────────────── */
    uint64_t x29; /* FP */
    uint64_t x30; /* LR */
    /* ── exception return state ─────────────────────────────── */
    uint64_t elr_el1;
    uint64_t spsr_el1;
} trap_frame_t;

/*
 * Initialise exception handling: install vector table at VBAR_EL1.
 * Must be called once on CPU 0 during kernel_init().
 */
void trap_init(void);

/*
 * C handler for EL1 synchronous exceptions.
 * Called by vector.S — does NOT return (parks the CPU).
 */
void sync_trap_handler(uint64_t esr, uint64_t far, uint64_t elr);

/*
 * C handler for EL1 IRQ exceptions.
 * Called by vector.S with a pointer to the saved trap frame on the stack.
 * Returns to vector.S which restores registers and does eret.
 */
void irq_trap_handler(trap_frame_t *frame);
