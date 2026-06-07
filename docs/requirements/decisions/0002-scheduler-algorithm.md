# ADR-0002 — 调度算法选型（v0.3.1 双队列）

> **Status**：Accepted（v0.3.1 重写：单层 8 级 → 双队列 RT+通用；含 R2 用户态 RT 进程）
> **Date**：2026-06-07
> **Supersedes**：v0.2 版本 ADR-0002（"单层 8 级优先级"，2026-06-07 同日被本 ADR 推翻）
> **Deciders**：junbo.dai
> **Refs**：[requirements §3](../requirements.md#3-调度策略) · [requirements §4.2](../requirements.md#42-用户态-syscall用户进程跨边界调用) · [dev-log/002 §不做什么](../../dev-log/002-scope-upgrade-to-mmu.md) · [dev-log/005](../../dev-log/005-unified-hybrid-kernel-v1-subscope.md) · [ADR-0001](0001-cpu-arch.md) · [ADR-0004](0004-single-address-space.md) · [ADR-0005](0005-user-kernel-boundary.md)

---

## Context

调度器是混合内核的核心。dev-log/005 决定 v1.0 = 双 ISA 统一混合内核（RT + GPOS 共存），调度器要在**同一份调度循环**里同时调度：

- **RT 任务**：电机控制、传感器、ISR 唤醒响应；硬实时；WCET 必须有界
- **通用进程**：shell、文件 IO、批处理；交互延迟容忍 ~100 ms；不要饿死

约束：

1. 主调度策略：抢占？协作？时间片？混合？
2. 同优先级如何处理：FIFO？时间片？
3. 互斥锁防优先级反转：是否用优先级继承协议（PIP / PCP）？
4. 可扩展性：是否要为 EDF / RMS 留接口？
5. 可分析性：能不能让 WCET 工具（Tracealyzer / Chronos）算出来？
6. **v0.3.1 新增**：双队列如何关系？RT 永远优先 vs RT 有 budget vs hybrid？
7. **v0.3.1 新增**：用户态进程能否申请 RT 优先级（R2 路径）？

---

## Decision

**v1.0 调度策略 = 双队列调度器 + R2 路径**：

### RT 队列（硬实时）

| 策略 | 选 |
|---|---|
| 固定优先级抢占 | ✅ 主调度，8 级（0–7，7 最高） |
| 同优先级时间片轮转 | ✅ 默认 10 ms |
| 优先级继承协议（PIP） | ✅ 仅 RT 队列内部 |
| EDF | ⚪ 插件，v1.1+ |
| RMS | ⚪ 仅分析工具，不进调度器 |

### 通用队列（GPOS）

| 策略 | 选 |
|---|---|
| 时间片轮转（RR） | ✅ 主调度，默认 100 ms |
| 优先级 | ❌ v1.0 flat，所有通用进程平等 |
| nice / 公平调度 | ⚪ v1.1+ 评估 |

### 队列间关系

- **严格抢占**：RT 队列非空 → **永远**优先于通用队列
- **饿死保护**：监控 RT 队列 CPU 占用 < 80%（软门槛，不进调度器）

### R2 路径（用户态 RT）

- 用户进程可通过 `sys_sched_set_rt(int priority)` syscall 升级到 RT 队列
- syscall 实现见 [ADR-0005](0005-user-kernel-boundary.md)
- RT 队列内部对 "kernel thread" 和 "user-mode RT 进程" **一视同仁**

### 上限

- RT 调度单元上限：64（kernel thread + 用户态 RT 进程共用此池）
- 通用进程上限：32

### SMP

- v1.0 **不做** SMP，但 TCB / 进程表预留 `cpu_mask` 字段

---

## Consequences

### 积极

- ✅ **RT 与 GPOS 各自最优**：RT 队列用固定优先级（可分析、低延迟），通用队列用 RR（公平、简单）——不需要互相妥协
- ✅ **O(1) 调度**：RT 队列位图 + ready queue，通用队列循环链表，两者都无遍历
- ✅ **教科书成熟**：PREEMPT_RT / QNX / VxWorks 都是这套
- ✅ **优先级反转有解**：PIP 协议在 RT 队列内部解决 mutex 死锁
- ✅ **可分析**：RT 固定优先级 + RMS 工具能算 schedulability
- ✅ **R2 路径让用户态 RT 可能**：micro-ROS / 用户级电机控制不必塞进 kernel

### 消极

- ❌ **通用饿死风险**：RT 占满 CPU 时通用永远饥饿；v1.0 仅靠监控告警（< 80% 软门槛），不进调度器
- ❌ **RT 利用率上限 ~70%**（固定优先级理论上限是 `n(2^(1/n) - 1)`，3 任务时 ~78%）
- ❌ **跨队列优先级反转无解**：通用进程持锁、RT 等锁——PIP 不能跨队列提升通用进程优先级（v1.0 文档化为"已知约束"，v2.0 评估）
- ❌ **配置复杂**：用户得懂 RT vs 通用、8 级优先级、时间片 3 套概念
- ❌ **R2 路径意味着 syscall 入口要细致**：v1.0 信任所有进程（capability=不做），但 API 留口

### 中性

- 时间片长度可配（编译期 `RT_TICK_MS` / `GENERIC_TICK_MS`）
- RT-budget 留 v1.1+ 评估（如果 v1.0 通用饿死成真，再加）
- SMP v1.0 不做，但 ready queue 数据结构按 per-CPU 设计

---

## Alternatives Considered

### 备选 A：EDF（Earliest Deadline First）作主调度

- ✅ 理论利用率 100%
- ❌ 就绪队列按 deadline 排序 = O(log n)，调度点开销涨 5–10 倍
- ❌ 可分析性差：固定优先级工具链成熟，EDF 工具链稀缺
- ❌ 机器人场景不划算：电机/传感器是周期任务，固定优先级 + RMS 就够
- **结论**：作为 RT 队列**插件**保留，v1.1+ 评估

### 备选 B：单队列 + Linux nice 值（CFS-lite）

- ✅ 简单，所有任务一个池
- ❌ **RT 与 GPOS 在同一池**：高优先级 RT 任务和 nice -20 通用进程区分不清
- ❌ 公平性 vs 确定性互斥
- **结论**：pass，跟 v0.3.1 的"RT + GPOS 共存且严格分层"目标冲突

### 备选 C：双队列 + RT-budget（PREEMPT_RT throttle）

- ✅ 通用进程有保证执行时间
- ❌ RT 任务 WCET 多一层"被 budget 限制"边界——分析复杂
- ❌ v1.0 sub-scope 不需要（监控告警足够）
- **结论**：v1.1+ 评估；若 v1.0 通用饿死成真，触发本 ADR 修订

### 备选 D：协作式调度（Cooperative）

- ✅ 简单，无抢占开销
- ❌ 最坏响应时间无界，不能用于 RT
- **结论**：pass

### 备选 E：纯时间片轮转（无优先级）

- ✅ 极简
- ❌ 无法保证高优先级 RT 任务响应
- **结论**：pass

---

## 关键设计细节（影响后续 Phase）

### 优先级范围

- RT 队列 **8 级**（0–7，7 最高）；通用队列无优先级
- 默认 RT 64 任务 / 8 级，每级平均 8 任务
- 可配（编译期 `KERNEL_RT_PRIORITY_LEVELS`）

### 时间片

- RT 队列默认 **10 ms**（10 个 tick @ 1 kHz）
- 通用队列默认 **100 ms**（100 个 tick @ 1 kHz）
- 任务运行满 1 个时间片 → 放回**同队列**就绪队列尾部

### 优先级继承协议（PIP）

- 持锁时**临时**提升优先级到**所有等待者中最高的优先级**
- 释放锁时**恢复**原优先级
- **仅** RT 队列内部（跨队列反转不支持，文档化为已知约束）
- v1.0 不支持嵌套 mutex 持锁

### 饿死保护

- 调度器**每 1 秒**统计 RT 队列累计 CPU 时间
- 占用 > 80% 时通过 `printk`/trace 事件告警
- **不进调度器逻辑**——只是 monitor + 触发开发者修任务设计
- v1.1+ 若需要硬约束 → 加 RT-budget（备选 C）

### R2: 用户态 RT 路径

- syscall `int sys_sched_set_rt(int priority)` — priority ∈ [0, 7]
- v1.0 **不做 capability check**——任何用户进程可申请；信任模型见 [requirements §6](../requirements.md#6-不做什么out-of-scope) capability 不做
- 进程升级到 RT 后，从通用队列**移到** RT 队列（保留 ASID + 页表）
- 跨队列移动时的 ASID 切换代价见 [ADR-0004](0004-single-address-space.md)

### 调度点

- `task_yield()` / `sys_yield()`（主动让出）
- 时间片耗尽
- 阻塞原语（`sem_take` 等不到、`sys_read` 阻塞等）
- ISR 退出（如果 ISR 唤醒了更高优先级 RT 任务）
- syscall 返回（如 `sys_sched_set_rt` 升级触发）
- page fault handler（kill 当前进程后切换）

---

## Validation

- [ ] **Phase 3 末**：调度点开销 < 1 μs（GPIO 翻转测量）
- [ ] **Phase 3 末**：RT 3 任务场景，响应时间符合 RMS 工具预测
- [ ] **Phase 4 末**：PIP 正确处理经典优先级反转 demo（2 个低 + 1 个高 + 共享 mutex）
- [ ] **Phase 5 末**：双队列严格抢占 demo——RT 任务在通用进程跑着时唤醒，单 tick 内切到 RT
- [ ] **Phase 5 末**：R2 路径 demo——用户进程调 `sys_sched_set_rt(7)`，下一调度点进 RT 队列
- [ ] **Phase 9 末**：饿死告警 demo——RT 故意占 > 80% → trace 事件被触发，但调度器不强行抢占
- [ ] **Phase 9 末**：Tracealyzer 能完整 trace 双队列调度事件，并能区分 kernel thread / user-mode RT

任一项不达标 → 触发 [requirements §8 的 90 天止损](../requirements.md#phase-1-启动-3-个月止损检查点)，评估降级（如把 R2 推迟到 v1.1，仅做 R1 内核态 RT）。

---

## Notes

- 本 ADR **重写** v0.2 ADR-0002（"单层 8 级优先级"），背景见 [dev-log/005](../../dev-log/005-unified-hybrid-kernel-v1-subscope.md)
- v0.2 的「同优先级时间片 vs FIFO」之争被 v0.3.1 的双队列结构隐式解决——RT 队列时间片、通用队列 RR
- R2 路径（用户态 RT 进程）是 [requirements §3.1](../requirements.md#31-rt-队列) 决策的实现细节；R3（混合）路径不实施，但 syscall ABI 留口
- 跨队列优先级反转是已知缺陷，v2.0 评估 RT-budget 或 priority ceiling 跨队列方案
