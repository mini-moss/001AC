/*
 * main.c — 内核 C 入口
 *
 * 从 boot.S 到达 EL1 时：
 *   - 栈已设置（__stack_top）
 *   - BSS 已清零
 *   - MMU 关闭（直接物理地址）
 *   - 仅 CPU 0 运行（CPU 1–3 已 park）
 *
 * Phase 1.3 新增：
 *   1. UART 初始化 + printk hello
 *   2. 异常向量表（VBAR_EL1）
 *   3. Generic timer（100 Hz tick）
 *   4. IRQ 驱动 tick 日志的空闲循环
 */
#include "arch/aarch64/trap.h"
#include "kernel/printk.h"
#include "kernel/uart.h"

/* ── Generic timer 设置 ────────────────────────────────────────────── */

static void timer_setup(void)
{
    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    printk("timer: CNTFRQ_EL0 = %u Hz\n", freq);

    /*
     * 配置 EL1 物理 timer 产生 100 Hz tick。
     * CNTP_TVAL_EL0 是递减计数器；到达 0 时 timer
     * 置位 ISTATUS 并（若未屏蔽）触发 IRQ。
     */
    uint64_t period = freq / 100;
    __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(period));

    /*
     * CNTP_CTL_EL0:
     *   位 0 = ENABLE   （1 = timer 计数中）
     *   位 1 = IMASK    （0 = 中断未屏蔽）
     *   位 2 = ISTATUS  （只读：1 = timer 条件满足）
     */
    __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(1UL));

    printk("timer: 100 Hz tick 已启用 (period = %u)\n", period);
}

/* ── 内核入口 ──────────────────────────────────────────────────── */

void kernel_main(void)
{
    uart_init();
    printk("\n=== minimoss 001AC ===\n");
    printk("hello from EL1 (AArch64 / Pi 4)\n");

    /*
     * trap_init() 安装向量表并取消 IRQ 屏蔽（DAIF.I）。
     * Timer IRQ 现在将到达 el1_irq_handler。
     */
    trap_init();

    /*
     * 启动 generic timer。第一个 tick 约 10 ms 后触发，
     * trap.c 中的 IRQ 处理器将重新装载 timer。
     */
    timer_setup();

    printk("kernel: 进入空闲循环 (tick 约每 10 ms)\n");

    /* 空闲循环 — timer IRQ 触发 tick 并在处理器中 printk */
    while (1) {
        /*
         * WFI（Wait For Interrupt）使 CPU 进入低功耗状态，
         * 直到 IRQ 触发。IRQ 处理器返回后，循环回 WFI。
         */
        __asm__ volatile("wfi");
    }
}
