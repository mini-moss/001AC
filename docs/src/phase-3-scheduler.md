# Phase 3 — 核心调度器

> **预估周期**：1–2 周
> **进入门槛**：Phase 2 完成
> **退出标志**：3 个任务在 QEMU 跑起来，周期抖动 < 10 μs

**这是整个项目最关键的一步。建议只做这个就 tag 一个 `v0.1`。**

## 3.1 任务控制块 TCB

```c
// include/kernel/task.h
typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,    // 等待信号量、队列等
    TASK_SUSPENDED,  // 主动挂起
    TASK_DORMANT     // 未启动
} task_state_t;

struct task {
    void           *sp;            // 栈指针（架构相关，置首便于汇编寻址）
    uint32_t       *stack_base;
    size_t          stack_size;
    int             priority;       // 静态优先级
    task_state_t    state;
    list_node_t     node;           // 挂到就绪/等待队列
    tick_t          wakeup_tick;    // 睡眠超时时刻
    char            name[16];
    void           *arg;
    uint32_t        magic;          // 栈溢出检测用
};
```

> **为什么 `sp` 放第一个**：`cpu_context_switch` 用汇编实现，偏移 0 直接是栈指针，省去加载结构体基址的指令。

## 3.2 优先级位图调度器（O(1)）

```c
// kernel/sched/bitmap.c
#define MAX_PRIO  32   // 默认支持 32 级优先级

static list_head_t ready_table[MAX_PRIO];
static uint32_t    ready_bitmap;   // 位 i 置 1 表示 ready_table[i] 非空

void ready_add(int prio, list_node_t *node) {
    list_append(&ready_table[prio], node);
    ready_bitmap |= (1U << prio);
}

void ready_remove(int prio, list_node_t *node) {
    list_remove(node);
    if (list_empty(&ready_table[prio]))
        ready_bitmap &= ~(1U << prio);
}

int ready_highest(void) {
    // __builtin_clz 计算前导零数目，硬件指令 O(1)
    return 31 - __builtin_clz(ready_bitmap);
}
```

**为什么选位图**：
- 调度时间确定（硬实时要求）
- `__builtin_clz` 编译为单条 CLZ 指令
- 32 级优先级满足绝大多数机器人场景；超 32 级可用 `uint32_t[4]` + 二分

## 3.3 上下文切换

### Cortex-M 路径

**为什么用 PendSV**：
- PendSV 优先级可设为最低，保证所有 ISR 完成后才切换
- 硬件自动压栈 xPSR / PC / LR / R12 / R3-R0，剩下 R4-R11 手动保存
- 触发只需写 `ICSR.PENDSVSET = 1`

```asm
// arch/arm/cortex-m/src/switch.S
.global cpu_context_switch
cpu_context_switch:
    mrs r0, psp                  @ 旧任务 PSP
    stmdb r0!, {r4-r11}          @ 保存剩余寄存器
    str r0, [r1]                 @ *old_sp = r0
    ldmia r2!, {r4-r11}          @ 恢复新任务寄存器
    msr psp, r2                  @ 切到新任务 PSP
    bx lr

.global PendSV_Handler
PendSV_Handler:
    mrs r0, psp
    stmdb r0!, {r4-r11}
    ldr r1, =g_current_sp
    str r0, [r1]

    ldr r0, =g_next_sp
    ldr r0, [r0]
    ldmia r0!, {r4-r11}
    msr psp, r0

    ldr r0, =g_current_tcb
    ldr r1, =g_next_tcb
    ldr r2, [r1]
    str r2, [r0]

    bx lr
```

### RISC-V 路径

RISC-V 没有硬件压栈，**全部寄存器手动保存**：

```asm
// arch/riscv/src/switch.S
.global cpu_context_switch
cpu_context_switch:
    sw sp, 0(a0)            @ *old_sp = sp
    lw sp, 0(a1)            @ sp = *new_sp

    addi sp, sp, -64
    sw ra, 0(sp)
    sw s0, 4(sp)
    sw s1, 8(sp)
    ...                     @ 保存 s0-s11
    addi sp, sp, 64

    ret
```

## 3.4 系统 Tick

```c
// kernel/time/tick.c
static volatile tick_t g_tick_count;

void SysTick_Handler(void) {     // Cortex-M
    g_tick_count++;
    tick_advance();              // 检查超时、轮转
    trigger_pendsv();
}

// 调用顺序
//   1. ISR 现场（硬件自动保存）
//   2. 递增 tick
//   3. 唤醒超时任务
//   4. 设置 PendSV
//   5. ISR 返回时硬件触发 PendSV
//   6. PendSV 切换到最高优先级就绪任务
```

> **关键细节**：PendSV 优先级必须**最低**，否则中断退出时直接切走，会丢失短 ISR 的尾处理。

## 3.5 调度器主循环

```c
// kernel/sched/sched.c
void schedule(void) {
    int prio = ready_highest();
    task_t *next = list_first(&ready_table[prio]);
    if (next != g_current_task) {
        g_next_task = next;
        trigger_pendsv();
    }
}

void task_yield(void) {
    // 把当前任务移到就绪队列尾（同优先级时间片轮转）
    int prio = g_current_task->priority;
    list_move_tail(&ready_table[prio], &g_current_task->node);
    schedule();
}
```

## 3.6 Idle 任务

```c
// kernel/sched/idle.c
static task_t idle_task;
static uint32_t idle_stack[256];

void idle_entry(void *arg) {
    (void)arg;
    while (1) {
        cpu_wfi();   // Wait For Interrupt，省电
    }
}

void idle_init(void) {
    task_create(&idle_task, "idle", idle_entry, NULL,
                sizeof(idle_stack), 0 /* 最低优先级 */);
}
```

## 3.7 第一个多任务 demo

```c
// app/main.c
static task_t t1, t2, t3;
static uint32_t s1[256], s2[256], s3[256];

void led_task(void *arg) {
    int period = (int)arg;
    tick_t last = tick_get();
    while (1) {
        printk("[%s] tick=%u\n", g_current_task->name, tick_get());
        task_sleep_until(&last, period);
    }
}

int main(void) {
    board_init();
    sched_init();
    idle_init();

    task_create(&t1, "fast",   led_task, (void*)10,  sizeof(s1), 3);
    task_create(&t2, "medium", led_task, (void*)50,  sizeof(s2), 2);
    task_create(&t3, "slow",   led_task, (void*)100, sizeof(s3), 1);

    sched_start();   // 切到第一个任务，永不返回
}
```

预期 QEMU 输出（节选）：
```
[idle] tick=0
[slow] tick=0
[medium] tick=0
[fast] tick=0
[fast] tick=10
[fast] tick=20
...
```

## 3.8 栈溢出检测

两种轻量方法（先实现 magic number）：

```c
#define STACK_MAGIC 0xDEADBEEF

int task_create(...) {
    ...
    task->stack_base[0] = STACK_MAGIC;
    task->stack_base[1] = STACK_MAGIC;
    task->stack_base[2] = STACK_MAGIC;
    task->stack_base[3] = STACK_MAGIC;
    ...
}

void idle_check_stack_overflow(void) {
    for (each task) {
        if (task->stack_base[0] != STACK_MAGIC
            || task->stack_base[1] != STACK_MAGIC
            || task->stack_base[2] != STACK_MAGIC
            || task->stack_base[3] != STACK_MAGIC) {
            printk("STACK OVERFLOW in %s\n", task->name);
            cpu_irq_disable();
            while (1) { cpu_wfi(); }
        }
    }
}
```

> **更强方案**：在 Cortex-M 上启用硬件栈溢出检测（特定芯片有配置寄存器）。但需要芯片支持，暂作可选。

## 3.9 QEMU 验证

```bash
# 启动
make BOARD=stm32f407-disco run
```

预期行为：
- fast 任务以 10ms 周期打印
- medium 任务以 50ms 周期打印
- slow 任务以 100ms 周期打印
- 总打印顺序反映优先级抢占

**测量抖动**（v0.1 可选）：
- 在 QEMU 中可用 `-d trace` 抓 PendSV 触发时间
- 真实硬件：用 GPIO 翻转 + 示波器
- 目标：同优先级任务的实际周期抖动 < 10 μs

## 验证标准

- [ ] 3 个不同优先级任务都能按预期周期运行
- [ ] 抢占行为正确（高优先级任务能立即打断低优先级）
- [ ] 上下文切换 < 10 μs（在 QEMU trace 中可见）
- [ ] 关闭 `sched_start()` 后各任务不执行
- [ ] Idle 任务优先级最低，永不饿死
- [ ] 栈溢出检测能捕获故意写坏的栈

## 常见坑

- ⚠️ Cortex-M 的 PSP 必须在第一次切换前由硬件初始化（中断返回时 LR=0xFFFFFFFD）
- ⚠️ RISC-V 的 `mret` 不像 Cortex-M 的 `bx lr`，需要手动恢复 `mepc` / `mstatus`
- ⚠️ `cpu_wfi()` 在中断关闭时是死循环；确保 idle 任务开启中断
- ⚠️ 不要在 idle 任务里调用任何阻塞 API，否则系统会僵死

## 接下来

进入 [Phase 4 — 同步与通信原语](phase-4-sync.md)，为多任务协作打下基础。
