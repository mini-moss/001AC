# ADR-0004 — v1.0 每进程独立页表 + 16 位 ASID（进程隔离）

> **Status**：Accepted（v0.3.1 重写：架构反转 from v0.2 单地址空间）
> **Date**：2026-06-07
> **Supersedes**：v0.2 版本 ADR-0004（"单地址空间"，2026-06-07 同日被本 ADR 翻转推翻）
> **Deciders**：junbo.dai
> **Refs**：[requirements §7](../requirements.md#7-内存模型v031-重写) · [requirements §3](../requirements.md#3-调度策略) · [dev-log/003](../../dev-log/003-mmu-rtos-tensions.md) · [dev-log/005](../../dev-log/005-unified-hybrid-kernel-v1-subscope.md) · [ADR-0001](0001-cpu-arch.md) · [ADR-0005](0005-user-kernel-boundary.md)

---

## Context

ADR-0001 决定开启 MMU（Cortex-A + RV64 强制要求）。dev-log/005 又决定 v1.0 = 双 ISA 统一混合内核（RT + GPOS 共存），需要**用户态进程**和**真正的隔离**。

地址空间组织 3 个主要选项：

| 方案 | 描述 | v0.2 | v0.3.1 |
|---|---|---|---|
| A. 关 MMU | 裸物理地址 | 已被 ADR-0001 推翻 | 仍推翻 |
| B. 单地址空间 | kernel + 所有 task 共享 1 份页表 | ✅ v0.2 选 | ❌ 翻转推翻 |
| C. **进程隔离** | 每进程独立页表 + ASID | 留 v2.0 | ✅ **v0.3.1 选** |

v0.2 选 B 的理由是「dev-log/003 列的 4 个 MMU 张力中，B 几乎不踩」。但 dev-log/005 决定「统一混合内核 + RT/GPOS 共存」后，**B 路线无法实现真正的用户态隔离**——RT 任务可踩通用进程内存，反之亦然，跟「混合内核」的设计目的冲突。

---

## Decision

**v1.0 采用方案 C：每进程独立页表 + 16 位 ASID**。

- 每个用户进程拥有**独立的低半区页表**（AArch64 TTBR0_EL1 / RISC-V satp）
- **内核高半区**映射进每个进程的页表（共享内核映射，避免 syscall 时切页表）
- **16 位 ASID**：硬件 tag，进程切换时不主动 flush TLB
- **TLB shootdown**：v1.0 单核**不需要**；v2.0 SMP 用 IPI
- **ASID 回收**：v1.0 32 进程 << 65535 ASID，**不需要回收算法**
- 详细布局见 [requirements §7](../requirements.md#7-内存模型v031-重写)

---

## Consequences

### 积极

- ✅ **真正的进程间隔离**：用户进程踩内存 → page fault → kill 单个进程，**不污染其他进程 / kernel**
- ✅ **真正的用户态**：EL0/U 跑用户代码，syscall 进 EL1/S；ELF 加载器、shell、micro-ROS user-agent 的前提
- ✅ **混合内核可实现**：RT 任务（kernel thread）和通用进程（user mode）能在统一内核里共存
- ✅ **走主流路线**：Linux / QNX / Fuchsia 都是「高半区内核 + 低半区用户 + ASID」这套，工具链 / 调试器 / ABI 无障碍
- ✅ **VFS / ELF 加载器可干净实现**：进程有独立地址空间，文件描述符表、栈分配都有明确归属
- ✅ **dev-log/003 张力 4 个中消解 2 个**：
  - ISR fault：ISR 仍在 EL1/S，进程切换不在 ISR 内
  - Cache：高半区共享避免 alias

### 消极

- ❌ **任务切换更慢**：跨进程要写 `TTBR0_EL1`/`satp`、切 ASID（同进程内 task < 20 μs / 跨进程 < 50 μs，见 requirements §2）
- ❌ **syscall 入口要切内核栈**：每次 syscall 多 ~50 cycles 的栈切换成本
- ❌ **copy_from_user / copy_to_user 路径**：用户态数据不能直接 deref，跨边界要走专用 API（详见 ADR-0005）
- ❌ **dev-log/003 张力的另外 2 个仍要面对**：
  - 缺页异常：用户进程可能 page fault（v1.0 仅 kill task，v2.0 可加按需分页）
  - TLB miss：ASID 切换后冷启动；但 ASID 隔离让旧进程 TLB 不消失，回切后命中
- ❌ **实现量比 v0.2 大 2-3 倍**：page table 分配器、ASID 分配器、user/kernel 切换路径
- ❌ **学习曲线陡**：跟 Fuchsia / QNX / Linux 学，没有 RTOS 教科书直接讲

### 中性

- 工具链 / 调试器 / GDB stub 按主流 ABI 工作，无定制需求
- 进程表 TCB 里加 `asid` 字段
- page fault handler 写成「可扩展为按需分页」形式（v1.0 触发时 kill 当前进程；v2.0 可改）

---

## Alternatives Considered

### 备选 A：关 MMU（v0.1 方案）

- ❌ Cortex-A + RV64 不允许关 MMU
- **结论**：已被 ADR-0001 推翻

### 备选 B：单地址空间（v0.2 方案，已翻转推翻）

- ✅ 实现简单，启动快，TLB 全命中
- ❌ **没有真正的用户态**：所有代码在 EL1，无法实现 ELF 加载器 / shell / micro-ROS user-agent
- ❌ **没有进程间隔离**：RT 任务和通用进程互相踩内存
- ❌ **跟 dev-log/005 的「统一混合内核」目标冲突**
- ❌ **v2.0 升级要重写内存子系统**——v0.2 ADR-0004 已经预料到这一点
- **结论**：v0.2 的「v1.0 先用 B，v2.0 升 C」路径被 v0.3.1 缩短为「v1.0 直接 C」

### 备选 D：Hybrid（部分进程隔离，部分共享）

- ❌ API 不一致：`process_create()` 不知道拿到的是隔离的还是共享的
- ❌ 调度器复杂度翻倍：要在 2 种地址空间切换
- ❌ 几乎没有人用（QNX / Linux 都是要么全做要么全不做）
- **结论**：pass

---

## 关键设计细节（影响后续 Phase）

### 虚拟地址布局

见 [requirements §7.1](../requirements.md#71-地址空间布局每进程独立)。

### ASID 分配

- 启动时初始化 ASID 池（1 ~ 65535，0 保留给内核 / 无 ASID）
- 进程创建：从池里分配下一个未使用 ASID，写入 TCB
- 进程退出：归还 ASID 到池
- v1.0 32 进程 << 65535，**无需 LRU 回收算法**；v2.0+ 多进程时再加

### 页表内存

- 每进程 1 个根页表（4 KB）+ 按需分配中间级页表
- 进程平均预估 ~16 KB 页表
- 32 进程 × 16 KB = 512 KB 池，已计入 [requirements §2 内核 RAM < 512 KB](../requirements.md#2-实时性指标)

### TLB / 切换路径

- 进程切换：`SET_ASID()` → `WRITE_TTBR0_EL1` / `WRITE_SATP` → `isb`/`sfence.vma`（仅同步指令，**不 flush**）
- 同进程内 task 切换：只切寄存器组 + kernel stack 指针，**不动**页表
- 详细汇编路径见 ADR-0005

### Phase 实现路径

- **Phase 1**：启动 stub 建 1 份内核页表（identity map + 高半区），跳到 main
- **Phase 2**：HAL 层包装 `mm_alloc()` / `mm_free()` / `mm_switch()`
- **Phase 5**：进程表 + ASID 池 + page fault handler；第一个用户进程跑通
- **Phase 6+**：VFS / ELF 加载器依赖 §7 的 `mm_t` 抽象

---

## Validation

- [ ] **Phase 1 末**：QEMU 跑通 hello world（MMU 启用、内核页表正确）
- [ ] **Phase 5 末**：第一个用户态 ELF 进程跑通（独立页表 + ASID + EL1↔EL0 切换）
- [ ] **Phase 5 末**：进程 A 故意写进程 B 内存 → A 触发 page fault → A 被 kill，**B / kernel 不受影响**
- [ ] **Phase 5 末**：野指针 → page fault → kill 当前进程，**其他进程 / kernel 不污染**
- [ ] **Phase 9 末**：Tracealyzer trace 任务切换时间分布
  - 同进程内 task 切换 < 20 μs（中位数）
  - 跨进程切换 < 50 μs（中位数）
- [ ] **Phase 9 末**：ASID 计数器 monitor，确认 v1.0 sub-scope 内未触及 65535

任一项不达标 → 触发 [requirements §8 的 90 天止损](../requirements.md#phase-1-启动-3-个月止损检查点)，评估降级或回退。

---

## Notes

- 本 ADR **翻转推翻** v0.2 版本 ADR-0004（"单地址空间"），背景见 [dev-log/005](../../dev-log/005-unified-hybrid-kernel-v1-subscope.md)
- v0.2 ADR-0004 的「v1.0 → v2.0 平滑升级」路径**不再适用**——v0.3.1 直接走目标态
- dev-log/003 的「MMU 与 RTOS 4 个张力」仍是设计参考；本 ADR 在其中 2 个上踩坑（缺页异常 + TLB miss），通过 ASID 缓解
- 不依赖 capability / verified；那些留 v2.0+（见 [requirements §6](../requirements.md#6-不做什么out-of-scope)）
- **测量基准**：跨进程切换 < 50 μs 是 Cortex-A72@1.5GHz / C906@1GHz 的合理保守值；若实测 > 100 μs，触发 ADR 修订
