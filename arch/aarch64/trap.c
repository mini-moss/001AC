/*
 * trap.c — AArch64 C-level exception / IRQ handling
 *
 * Responsibilities:
 *   - trap_init():     install vector_table_el1 at VBAR_EL1
 *   - sync_trap_handler():  decode ESR_EL1, print diagnostic, park CPU
 *   - irq_trap_handler():   dispatch to timer / GIC handlers
 */

#include "trap.h"
#include "kernel/printk.h"
#include "kernel/uart.h"

/* ── Assembly symbols ──────────────────────────────────────────────── */
extern void vector_table_el1(void);

/* ── Trap initialisation ───────────────────────────────────────────── */

void trap_init(void)
{
    /*
     * Install the EL1 exception vector table.
     * ARMv8 requires the table to be 2 KiB (0x800) aligned; the linker
     * and .balign in vector.S guarantee this.
     */
    __asm__ volatile("msr vbar_el1, %0" :: "r"(&vector_table_el1));

    /*
     * Enable IRQ at the CPU level (clear DAIF.I).
     * The timer PPI will now reach the IRQ exception handler.
     */
    __asm__ volatile("msr daifclr, #2");  /* DAIFClr.I = 1 → unmask IRQ */

    printk("trap: VBAR_EL1 set, IRQ unmasked\n");
}

/* ── Synchronous exception handler ────────────────────────────────────
 *
 * An unexpected sync exception (undefined instruction, data abort, etc.)
 * lands here.  Phase 1.3 has no recovery strategy — print the reason
 * and park the core.
 */
void sync_trap_handler(uint64_t esr, uint64_t far, uint64_t elr)
{
    uint64_t ec = (esr >> 26) & 0x3f;  /* Exception Class (bits 31:26) */

    printk("\n=== SYNCHRONOUS EXCEPTION ===\n");
    printk("  ESR_EL1: 0x%x\n", (uint64_t)esr);
    printk("  FAR_EL1: 0x%x\n", far);
    printk("  ELR_EL1: 0x%x\n", elr);
    printk("  EC:      0x%x (", ec);

    switch (ec) {
    case 0x11: printk("Instruction Abort (current EL)\n");  break;
    case 0x15: printk("Data Abort (current EL)\n");         break;
    case 0x20: printk("SP alignment fault\n");              break;
    case 0x21: printk("PC alignment fault\n");              break;
    case 0x25: printk("Data Abort (current EL, DAIF set)\n"); break;
    default:   printk("unknown)\n");                         break;
    }

    printk("CPU parked.\n");
    while (1)
        ;
}

/* ── IRQ handler ────────────────────────────────────────────────────── */

/*
 * Forward declaration — timer handling is in the platform layer
 * (arch/aarch64/timer.c or similar).  For now it lives inline.
 */
static uint64_t g_tick_count;

static void handle_timer_irq(void)
{
    /*
     * Read the generic timer frequency, then reload the timer for the
     * next tick.  TICK_HZ is 100 by default — configurable later.
     */
    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));

    /* Set timer for the next tick (~10 ms if freq=62.5 MHz / 100) */
    uint64_t period = freq / 100;          /* 100 Hz tick */
    __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(period));

    g_tick_count++;

    /* Print a dot every 100 ticks (= 1 per second at 100 Hz) */
    if ((g_tick_count % 100) == 0) {
        printk("tick %d\n", (int)g_tick_count);
    }
}

void irq_trap_handler(trap_frame_t *frame)
{
    /*
     * Minimal IRQ dispatch: check the generic timer ISTATUS bit.
     * If the timer fired, handle it.  Otherwise this is an unexpected
     * IRQ — report and ignore.
     *
     * A full GIC driver (Phase 4+) will replace this.
     */
    uint64_t cntp_ctl;
    __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(cntp_ctl));

    if (cntp_ctl & (1 << 2)) {           /* ISTATUS = timer condition met */
        handle_timer_irq();
    } else {
        /* Unexpected IRQ — could be GIC not configured yet */
        printk("IRQ: unexpected (CNTP_CTL_EL0=0x%x)\n", cntp_ctl);
    }

    (void)frame; /* trap frame pointer — unused in Phase 1.3 */
}
