/*
 * trap.c — AArch64 C 层异常 / IRQ 处理
 *
 * 职责：
 *   - trap_init():     将 vector_table_el1 安装到 VBAR_EL1
 *   - sync_trap_handler():  解码 ESR_EL1，打印诊断信息，park CPU
 *   - irq_trap_handler():   分派到 timer / GIC 处理器
 */

#include "trap.h"
#include "kernel/printk.h"
#include "kernel/uart.h"

/* ── 汇编符号 ──────────────────────────────────────────────── */
extern void vector_table_el1(void);

/* ── 异常初始化 ───────────────────────────────────────────── */

void trap_init(void)
{
    /*
     * 安装 EL1 异常向量表。
     * ARMv8 要求向量表 2 KiB（0x800）对齐；链接脚本和 vector.S
     * 中的 .balign 保证这一点。
     */
    __asm__ volatile("msr vbar_el1, %0" :: "r"(&vector_table_el1));

    /*
     * 在 CPU 级别启用 IRQ（清除 DAIF.I）。
     * timer PPI 现在可以到达 IRQ 异常处理器。
     */
    __asm__ volatile("msr daifclr, #2");  /* DAIFClr.I = 1 → 取消 IRQ 屏蔽 */

    printk("trap: VBAR_EL1 已设置，IRQ 已取消屏蔽\n");
}

/* ── 同步异常处理器 ────────────────────────────────────
 *
 * 意外的同步异常（未定义指令、数据 abort 等）到达此处。
 * Phase 1.3 没有恢复策略 —— 打印原因并 park 核心。
 */
void sync_trap_handler(uint64_t esr, uint64_t far, uint64_t elr)
{
    uint64_t ec = (esr >> 26) & 0x3f;  /* Exception Class（位 31:26） */

    printk("\n=== 同步异常 ===\n");
    printk("  ESR_EL1: 0x%x\n", (uint64_t)esr);
    printk("  FAR_EL1: 0x%x\n", far);
    printk("  ELR_EL1: 0x%x\n", elr);
    printk("  EC:      0x%x (", ec);

    switch (ec) {
    case 0x11: printk("当前 EL 指令 Abort\n");  break;
    case 0x15: printk("当前 EL 数据 Abort\n");  break;
    case 0x20: printk("SP 对齐故障\n");          break;
    case 0x21: printk("PC 对齐故障\n");          break;
    case 0x25: printk("当前 EL 数据 Abort（DAIF 置位）\n"); break;
    default:   printk("未知)\n");                 break;
    }

    printk("CPU 已 park。\n");
    while (1)
        ;
}

/* ── IRQ 处理器 ────────────────────────────────────────────────────── */

/*
 * 前向声明 —— timer 处理在平台层（arch/aarch64/timer.c 或类似文件）。
 * 目前内联在此处。
 */
static uint64_t g_tick_count;

static void handle_timer_irq(void)
{
    /*
     * 读取 generic timer 频率，然后重新加载 timer 以产生下一个 tick。
     * 默认 TICK_HZ 为 100 —— 后续可配置。
     */
    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));

    /* 设置下一个 tick 的 timer（约 10 ms，频率 = 62.5 MHz / 100） */
    uint64_t period = freq / 100;          /* 100 Hz tick */
    __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(period));

    g_tick_count++;

    /* 每 100 ticks 打印一次（100 Hz 下 = 每秒一次） */
    if ((g_tick_count % 100) == 0) {
        printk("tick %d\n", (int)g_tick_count);
    }
}

void irq_trap_handler(trap_frame_t *frame)
{
    /*
     * 最小 IRQ 分派：检查 generic timer ISTATUS 位。
     * 如果 timer 触发，处理之。否则这是意外的 IRQ —— 报告并忽略。
     *
     * 完整的 GIC 驱动（Phase 4+）将替换此实现。
     */
    uint64_t cntp_ctl;
    __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(cntp_ctl));

    if (cntp_ctl & (1 << 2)) {           /* ISTATUS = timer 条件满足 */
        handle_timer_irq();
    } else {
        /* 意外的 IRQ —— 可能是 GIC 尚未配置 */
        printk("IRQ: 意外的中断 (CNTP_CTL_EL0=0x%x)\n", cntp_ctl);
    }

    (void)frame; /* trap frame 指针 —— Phase 1.3 未使用 */
}
