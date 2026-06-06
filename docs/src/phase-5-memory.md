# Phase 5 — 内存管理

> **预估周期**：3 天
> **进入门槛**：Phase 4 完成
> **退出标志**：静态内存池分配/释放时间稳定，无碎片；栈溢出检测可定位溢出任务

实时系统**禁用**无界 `malloc`。本 Phase 实现两类分配器，并加上栈溢出检测。

## 5.1 静态内存池（首选）

```c
// include/kernel/mem.h
typedef struct mem_pool {
    uint8_t       *base;       // 整块内存
    size_t         block_size; // 单块大小（向上对齐到 2 的幂）
    size_t         capacity;   // 块数
    list_head_t    free_list;  // 空闲块链表
    uint32_t       used;
    uint32_t       high_watermark;
} mem_pool_t;

int  mem_pool_init(mem_pool_t *pool, void *base, size_t size, size_t block_size);
void *mem_alloc(mem_pool_t *pool);
int   mem_free(mem_pool_t *pool, void *block);
size_t mem_pool_used(const mem_pool_t *pool);
```

**实现要点**：
- 块大小向上对齐到 2 的幂（位运算 `(n + (n-1)) & ~(n-1)`）
- 空闲链表用「块内嵌链表节点」节省 RAM
- 分配 O(1)、释放 O(1)
- 无碎片

**典型用法**：

```c
// 给 UART 驱动分配接收 buffer
static uint8_t uart_rx_pool_mem[10 * 64];  // 10 块 × 64B
static mem_pool_t uart_rx_pool;

void uart_init(void) {
    mem_pool_init(&uart_rx_pool, uart_rx_pool_mem,
                  sizeof(uart_rx_pool_mem), 64);
}

uint8_t *buf = mem_alloc(&uart_rx_pool);
// ... 用完后
mem_free(&uart_rx_pool, buf);
```

## 5.2 可选：动态分配器

**仅在以下场景启用**：
- 应用层偶尔请求内存
- 实时性要求不严

**推荐**：`tlsf`（Two-Level Segregated Fit）
- 时间复杂度 O(1)
- 碎片率低
- 头文件单文件，集成简单

```c
// kernel/mem/tlsf.c
// 标记为「非确定性 API」，需 Kconfig 显式开启
#ifdef CONFIG_MEM_DYNAMIC
    tlsf_t g_tlsf;
    static uint8_t tlsf_mem[CONFIG_MEM_DYNAMIC_SIZE];

    void mem_dynamic_init(void) {
        g_tlsf = tlsf_create_with_pool(tlsf_mem, sizeof(tlsf_mem));
    }
    void *kmalloc(size_t size) { return tlsf_malloc(g_tlsf, size); }
    void  kfree(void *ptr)      { tlsf_free(g_tlsf, ptr); }
#endif
```

**约束文档**（写在头文件注释）：

```c
/**
 * @warning kmalloc 在中断处理函数中**不可**调用。
 *          实时路径请使用 mem_alloc()（静态内存池）。
 */
```

## 5.3 栈溢出检测

Phase 3 的 magic number 方法已在 `task_create` 中实现。Phase 5 补全检测逻辑：

```c
// kernel/sched/stack_check.c
#define STACK_MAGIC 0xDEADBEEF
#define STACK_GUARD_WORDS 4

static inline void stack_fill_guard(task_t *t) {
    for (int i = 0; i < STACK_GUARD_WORDS; i++) {
        t->stack_base[i] = STACK_MAGIC;
    }
}

static inline bool stack_check_guard(const task_t *t) {
    for (int i = 0; i < STACK_GUARD_WORDS; i++) {
        if (t->stack_base[i] != STACK_MAGIC) return false;
    }
    return true;
}

void stack_check_all(void) {
    for_each_task(t) {
        if (!stack_check_guard(t)) {
            printk("!!! STACK OVERFLOW in task '%s' !!!\n", t->name);
            printk("    sp = %p, base = %p, size = %u\n",
                   t->sp, t->stack_base, (unsigned)t->stack_size);
            cpu_irq_disable();
            while (1) { cpu_wfi(); }
        }
    }
}
```

**调用时机**：
- Idle 任务中（每 N 个 tick 检查一次）
- 任务切换时（只检查被切出的任务，更快但覆盖率低）

### 更强方案：MPU / 硬件栈检查

部分 Cortex-M 芯片有 MPU 或专用栈溢出寄存器：
- STM32F4：双栈水印寄存器
- NXP Kinetis：硬件栈检查

如果目标芯片支持，作为可选优化接入，**不**作为基线方案。

## 5.4 内存统计

```c
typedef struct {
    size_t total;
    size_t used;
    size_t high_watermark;
    size_t peak_block_count;
} mem_stats_t;

void mem_pool_stats(const mem_pool_t *pool, mem_stats_t *stats);
```

`printk` 调试命令：
```
> mem stats
pool  block  total  used  high
uart  64     10     3     8
imu   32     20     12    18
```

## 5.5 内存布局总览

```
┌─────────────────┐ 0x20000000  RAM 起始
│  .data          │ ← 从 Flash 复制过来的初始化数据
├─────────────────┤
│  .bss           │ ← 全局未初始化变量（清零）
├─────────────────┤
│  heap (可选)    │ ← tlsf / 动态分配区
├─────────────────┤
│  mem_pool 区    │ ← 用户静态内存池
├─────────────────┤
│  任务栈 #N      │
├─────────────────┤
│  ...            │
├─────────────────┤
│  main 栈 / 中断栈│
├─────────────────┤ 0x20020000  RAM 结束
│                 │
```

链接脚本中显式定义各段，避免互相覆盖。

## 验证标准

- [ ] 分配 10000 次、释放 10000 次，无内存泄漏（用 high_watermark 验证）
- [ ] 分配 / 释放时间稳定（用 GPIO 翻转 + 示波器量 < 1 μs）
- [ ] 故意把任务栈用满，触发栈溢出检测并打印任务名
- [ ] 链接脚本中 `.data` + `.bss` + heap + pool + 所有任务栈 ≤ RAM 大小
- [ ] `arm-none-eabi-size rtos.elf` 报告的 RAM 占用与设计值一致

## 常见坑

- ⚠️ 静态内存池的 block_size 必须向上对齐到 2 的幂，否则链表指针会跨块边界
- ⚠️ 把内存池放在 `.bss` 而非 `.data`，避免启动时复制浪费时间
- ⚠️ 内存池大小要预留 10–20% 余量，方便后期扩展
- ⚠️ 不要在 ISR 中分配内存，即使分配时间再短，递归调用栈可能很深

## 接下来

进入 [Phase 6 — 驱动框架](phase-6-drivers.md)，把硬件能力通过统一接口暴露给上层。
