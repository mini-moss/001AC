# Dev Log · 003 — MMU 与 RTOS 的 4 个张力

> **日期**：2026-06-07
> **范围**：澄清 v0.2 中「MMU + RTOS 不冲突」的关键点
> **触发**：用户提问「MMU 和 RTOS 冲突吗？」
> **影响**：[requirements/requirements.md §2 / §6 / §7](../requirements/requirements.md) 已为这些张力留位

---

## 一句话

> **MMU 不破坏「实时性」，但破坏「天真地写代码就能实时」的假设。**

「实时」= WCET 有界，**不**等于「无 MMU」。QNX / VxWorks / INTEGRITY / LynxOS-178 全部用 MMU，都是硬实时。Linux + PREEMPT_RT 跑机器人也用 MMU。

---

## 4 个具体张力

### ① 缺页异常 = 调度器之外的"随机"延迟

| 场景 | Cortex-M | Cortex-A + MMU |
|---|---|---|
| 访问 task 栈 | < 10 ns（cache 命中） | **可能触发 page fault，几十到几百 μs** |
| 第一次访问 task 数据 | 同样快 | 同样可能 page fault |

**缓解**（v0.2 必须做）：

- task 创建时**主动 touch 整个栈**（写 0 到每 4 KB 页），预消费缺页
- 用一次性分配，避免运行时分页
- 关键路径**锁页**（PTE 标 `M` / RISC-V pinned TLB）

### ② TLB miss = 上下文切换的尾巴抖动

每次切 `TTBR0_EL1`，第一次访问新地址 = TLB miss ≈ 30 周期。

**缓解**（v0.2 已计划）：

- 用 ASID（v1.0 单地址空间可省；v2.0 进程隔离再开）
- 切换后**预热**几个关键地址
- 见 [requirements §7.3](../requirements/requirements.md)

### ③ ISR 自己也会 page fault

Cortex-M：ISR 永远能访问内存。

Cortex-A + MMU：ISR 在 EL1，**同样查页表**。如果 ISR 触发缺页：

- fault handler 是普通 IRQ，会和 ISR 抢同一异常级别
- 处理不好 = **递归 fault = 死**

**缓解**：

- ISR 用**独立栈 + 永久映射的页**
- `vector` 入口绝对不允许 fault
- 关键内核代码**锁住**（PTE 加 `UXN` / `PAN` 等保护位）

### ④ D-Cache 一致性

Cortex-M 一般没 D-Cache → 不用管协议。

Cortex-A 有 D-Cache + I-Cache → **DMA / 多核 / 同一物理页两个虚拟地址** 都有 alias 风险。

**缓解**：

- 启动时**显式 invalidate I-cache + D-cache**
- DMA buffer 走 `dma_alloc_coherent`（cacheline 对齐、不可 cache）
- 自修改代码必须 `__builtin___clear_cache` / `dc civac`

---

## 我们的 requirements.md 已经为这些张力留位

| 张力 | 在 requirements.md 的位置 |
|---|---|
| ① 缺页开销 | §2 任务切换 10 μs → **20 μs** |
| ② TLB miss | §7.3 TLB 管理（ASID / shootdown） |
| ③ ISR fault | §6 「不做用户态进程隔离」+ §7.2 单地址空间 |
| ④ Cache | §4 API 文档加「内存影响」字段 |
| 整体 | §6 「MMU」从 ❌ 改为 ✅，但「IOMMU / 进程隔离」仍是 v2.0 |

---

## 怎么验证（Phase 0 退出门槛的额外补充）

Phase 1 跑通 hello world 后，加一个**最坏执行时间 sanity check**：

- 写一个 task，故意访问未映射内存 → 应该**立刻** page fault（几 μs 内），不是延迟到调度点
- 写一个 task，重复访问同一组地址 → 第二次开始应该是 TLB 命中（< 100 ns）

这两个 check 验证「**MMU 是开着的，且中断路径不会因为 page fault 死**」。

---

## 一句话总结

**MMU + RTOS 在 v0.2 是合理的、成熟的、可达成的。** 学习曲线比纯 Cortex-M 陡，但回报是真实工业栈。
