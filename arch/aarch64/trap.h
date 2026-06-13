/*
 * trap.h — AArch64 异常栈帧和处理器声明
 *
 * trap_frame 结构体与 vector.S 构建的栈布局一一对应。
 */
#pragma once

#include <stdint.h>

/*
 * IRQ 入口跳板（vector.S）保存的寄存器快照。
 * 栈布局（低地址 → 高地址）：
 *   x0–x18, x29(FP), x30(LR), elr_el1, spsr_el1
 *
 * 必须与 arch/aarch64/vector.S 保持同步！
 */
typedef struct {
    /* ── 调用者保存寄存器（由 vector.S 保存） ──────────── */
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
    /* ── 帧指针 + 链接寄存器 ──────────────────────── */
    uint64_t x29; /* FP */
    uint64_t x30; /* LR */
    /* ── 异常返回状态 ─────────────────────────────── */
    uint64_t elr_el1;
    uint64_t spsr_el1;
} trap_frame_t;

/*
 * 初始化异常处理：将向量表安装到 VBAR_EL1。
 * 必须在 kernel_init() 期间由 CPU 0 调用一次。
 */
void trap_init(void);

/*
 * EL1 同步异常的 C 处理器。
 * 由 vector.S 调用 —— 不会返回（park CPU）。
 */
void sync_trap_handler(uint64_t esr, uint64_t far, uint64_t elr);

/*
 * EL1 IRQ 异常的 C 处理器。
 * 由 vector.S 调用，参数为栈上保存的 trap frame 指针。
 * 返回 vector.S，后者恢复寄存器并执行 eret。
 */
void irq_trap_handler(trap_frame_t *frame);
