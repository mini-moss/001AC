# Phase 4 — 同步与通信原语

> **预估周期**：1 周
> **进入门槛**：Phase 3 完成
> **退出标志**：所有原语通过单元测试，覆盖 timeout / from-ISR / 优先级反转场景

按使用频率从高到低实现。每个原语都遵循统一接口约定：

```c
typedef struct sync_obj sync_obj_t;

int  sync_create(sync_obj_t *obj, ...);     // 静态初始化，无需分配
int  sync_take(sync_obj_t *obj, tick_t timeout);   // 阻塞，timeout=TIMEOUT_NEVER 表示永久等
int  sync_give(sync_obj_t *obj);
int  sync_give_from_isr(sync_obj_t *obj);   // ISR 安全版本，不阻塞
```

## 4.1 信号量（Semaphore）

二值与计数合一：

```c
// include/kernel/sem.h
typedef struct {
    int             count;     // 当前计数
    int             max;       // 上限
    list_head_t     waiters;   // 等待任务链表
} sem_t;

void sem_init(sem_t *sem, int initial, int max);
int  sem_take(sem_t *sem, tick_t timeout);
int  sem_give(sem_t *sem);
int  sem_give_from_isr(sem_t *sem);

/* 状态码 */
#define SEM_OK            0
#define SEM_TIMEOUT      -1
#define SEM_INVALID      -2
```

**实现要点**：
- 等待队列按「唤醒后优先级」排序，相同优先级按等待时间排序（防饥饿）
- `take`：count > 0 直接减；否则挂起到 waiters，触发调度
- `give`：waiters 非空则唤醒队首（不增加 count），否则 count++
- `give_from_isr`：在 ISR 上下文调用，**不**触发调度；只置位 `need_resched` 标志，由 PendSV 统一处理

```c
int sem_take(sem_t *sem, tick_t timeout) {
    unsigned state = cpu_irq_save();
    if (sem->count > 0) { sem->count--; cpu_irq_restore(state); return SEM_OK; }
    if (timeout == 0)   { cpu_irq_restore(state); return SEM_TIMEOUT; }
    g_current_task->state = TASK_BLOCKED;
    list_append(&sem->waiters, &g_current_task->node);
    g_current_task->wakeup_tick = tick_get() + timeout;
    schedule();   // 触发 PendSV
    cpu_irq_restore(state);
    // ... 被唤醒后回到这里，检查 wakeup 原因
    return g_current_task->wakeup_status;
}
```

## 4.2 互斥锁（Mutex）

**核心特性：优先级继承协议**（防优先级反转）：

```c
typedef struct {
    task_t         *owner;     // 当前持有者
    int             lock_count; // 支持递归锁
    list_head_t     waiters;
    int             original_prio;  // 持有者被临时提升前的优先级
} mutex_t;
```

**场景**：
- 任务 L（低优先级）持有锁
- 任务 H（高优先级）等待锁，被阻塞
- 任务 M（中优先级）就绪，抢占 L
- 结果：H 实际上被 M 间接阻塞，**优先级反转**

**优先级继承解法**：
- 当 H 等待 L 持有的锁时，**临时把 L 提升到 H 的优先级**
- L 释放锁时，恢复原优先级
- 这样 M 无法抢占 L，H 尽快拿到锁

```c
int mutex_lock(mutex_t *mtx, tick_t timeout) {
    unsigned state = cpu_irq_save();
    if (mtx->owner == NULL) {
        mtx->owner = g_current_task;
        mtx->lock_count = 1;
        cpu_irq_restore(state);
        return MUTEX_OK;
    }
    if (mtx->owner == g_current_task) {
        mtx->lock_count++;
        cpu_irq_restore(state);
        return MUTEX_OK;
    }
    // 优先级继承
    if (g_current_task->priority > mtx->owner->priority) {
        mtx->owner->priority = g_current_task->priority;
        // 重排就绪队列
        ready_remove(mtx->owner->priority, &mtx->owner->node);
        ready_add(mtx->owner->priority, &mtx->owner->node);
    }
    g_current_task->state = TASK_BLOCKED;
    list_append(&mtx->waiters, &g_current_task->node);
    schedule();
    cpu_irq_restore(state);
    return g_current_task->wakeup_status;
}
```

> **更高级**：天花板协议（PCP）也可作为可选策略，避免链式继承。

## 4.3 消息队列

```c
typedef struct {
    uint8_t    *buf;        // 环形 buffer
    size_t      msg_size;   // 单条消息字节数
    size_t      capacity;   // 消息条数
    size_t      head, tail; // 读写指针（按消息计）
    size_t      count;
    list_head_t readers;    // 等数据的任务
    list_head_t writers;    // 等空间的任务
} queue_t;
```

- `queue_send`：count < capacity 写入；否则挂起到 writers
- `queue_recv`：count > 0 读出；否则挂起到 readers
- 复制语义：写入 / 读出都是 `memcpy`，避免引用问题
- ISR 版本：空间够则直接 memcpy 并返回；否则丢弃或返回错误（依 API 设计）

## 4.4 事件标志组

```c
typedef struct {
    uint32_t      flags;
    list_head_t   waiters;   // 每个等待者带自己的 mask + mode
} event_t;

// 模式：AND（全部置位才唤醒）/ OR（任一置位即唤醒）
#define EVENT_AND  0
#define EVENT_OR   1

int event_wait(event_t *ev, uint32_t mask, int mode, tick_t timeout);
int event_set(event_t *ev, uint32_t mask);
int event_clear(event_t *ev, uint32_t mask);
```

**位图唤醒算法**：
- 每次 `event_set`，遍历 waiters 检查每个任务 mask 是否满足
- 满足的任务从 waiters 摘除、就绪
- 时间复杂度 O(waiters)，适合少量任务（< 10）

## 4.5 软件定时器

```c
typedef struct {
    tick_t     deadline;     // 触发时刻
    tick_t     period;       // 周期（0 表示单次）
    void     (*callback)(void *);
    void      *arg;
    list_node_t node;
} sw_timer_t;

// 内核用最小堆（O(log n)）或时间轮（O(1)）
// 30 级时间轮 + 5 位哈希是常见做法
```

**触发流程**：
- Tick 中断检测堆顶 / 时间轮当前槽
- 到达 deadline 的定时器：执行 callback（**在 ISR 上下文**，不能阻塞！）
- 周期定时器：re-add 到堆

> ⚠️ **重要约束**：callback 中不能调用任何阻塞 API；如需长处理，发信号量通知任务做。

## 4.6 单元测试

在 host (x86 Linux) 上跑逻辑测试，绕过硬件：

```c
// tests/unit/test_sem.c
void test_sem_basic(void) {
    sem_t s;
    sem_init(&s, 0, 1);

    // take in thread A: should block
    // give in thread B: should wake A
    TEST_ASSERT_EQ(sem_take(&s, 0), SEM_TIMEOUT);
    TEST_ASSERT_EQ(sem_give(&s), SEM_OK);
    TEST_ASSERT_EQ(sem_take(&s, 0), SEM_OK);
}

void test_sem_priority_inversion(void) {
    // 故意构造优先级反转场景
    // 验证优先级继承协议生效
    ...
}

test_register("sem_basic", test_sem_basic);
test_register("sem_inversion", test_sem_priority_inversion);
```

可用 CMocka 或自写极简框架（~200 行）。

## 4.7 性能 / 复杂度清单

文档化每个 API 的时间复杂度：

| API | 复杂度 | 备注 |
|---|---|---|
| `sem_take`（非阻塞路径） | O(1) | 计数 > 0 |
| `sem_take`（阻塞路径） | O(1) | 加入等待队列 |
| `sem_give`（无等待者） | O(1) | 计数++ |
| `sem_give`（有等待者） | O(1) | 唤醒队首 |
| `mutex_lock` | O(1) | 不含优先级继承重排 |
| `mutex_unlock` | O(waiters) | 唤醒队首 |
| `queue_send` / `queue_recv` | O(1) | 环形 buffer |
| `event_set` | O(waiters) | 遍历检查 |
| `sw_timer` 启动 | O(log n) | 最小堆 |
| `sw_timer` 触发 | O(1) | 堆顶检测 |

## 验证标准

- [ ] 信号量 / 互斥 / 队列 / 事件 / 软件定时器 全部单元测试通过
- [ ] timeout=0 立即返回（不阻塞）
- [ ] `*_from_isr` 在 ISR 上下文中不触发调度
- [ ] 优先级反转测试：构造反转场景，验证继承协议把低优先级任务临时提升
- [ ] 死锁检测：单元测试中故意 A 等 B、B 等 A，验证超时返回

## 常见坑

- ⚠️ `sem_give_from_isr` 不能调用 `schedule()`，否则 ISR 上下文栈被破坏
- ⚠️ 优先级继承是「递归」的：A 继承 B，B 继承 C，最终三者优先级统一
- ⚠️ 队列里存指针 vs 存数据：指针版更灵活但有生命周期问题；先做数据版
- ⚠️ 事件标志组不要用「全局事件」（所有任务共享），容易产生隐式耦合

## 接下来

进入 [Phase 5 — 内存管理](phase-5-memory.md)，为同步原语与驱动提供确定性内存分配。
