/*
 * main.c — kernel C entry
 *
 * From boot.S we arrive at EL1 with:
 *   - stack set (__stack_top)
 *   - BSS zeroed
 *   - MMU off (direct physical addresses)
 *   - only CPU 0 running (CPUs 1–3 parked)
 *
 * Phase 1.3 adds:
 *   1. UART init + printk hello
 *   2. Exception vector table (VBAR_EL1)
 *   3. Generic timer (100 Hz tick)
 *   4. Idle loop with IRQ-driven tick logging
 */
#include "arch/aarch64/trap.h"
#include "kernel/printk.h"
#include "kernel/uart.h"

/* ── Generic timer setup ────────────────────────────────────────────── */

static void timer_setup(void)
{
    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    printk("timer: CNTFRQ_EL0 = %u Hz\n", freq);

    /*
     * Configure the EL1 physical timer for a 100 Hz tick.
     * CNTP_TVAL_EL0 is a down-counter; when it reaches 0 the timer
     * asserts ISTATUS and (if unmasked) fires an IRQ.
     */
    uint64_t period = freq / 100;
    __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(period));

    /*
     * CNTP_CTL_EL0:
     *   bit 0 = ENABLE   (1 = timer counting)
     *   bit 1 = IMASK    (0 = interrupt NOT masked)
     *   bit 2 = ISTATUS  (read-only: 1 = timer condition met)
     */
    __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(1UL));

    printk("timer: 100 Hz tick enabled (period = %u)\n", period);
}

/* ── Kernel entry ──────────────────────────────────────────────────── */

void kernel_main(void)
{
    uart_init();
    printk("\n=== minimoss 001AC ===\n");
    printk("hello from EL1 (AArch64 / Pi 4)\n");

    /*
     * trap_init() installs the vector table and unmasks IRQ (DAIF.I).
     * Timer IRQs will now reach el1_irq_handler.
     */
    trap_init();

    /*
     * Start the generic timer.  The first tick will fire in ~10 ms
     * and the IRQ handler in trap.c will re-arm it.
     */
    timer_setup();

    printk("kernel: entering idle loop (tick every ~10 ms)\n");

    /* Idle loop — timer IRQs tick and printk in the handler */
    while (1) {
        /*
         * WFI (Wait For Interrupt) puts the CPU in a low-power state
         * until an IRQ fires.  Once the IRQ handler returns, we loop
         * back to WFI.
         */
        __asm__ volatile("wfi");
    }
}
