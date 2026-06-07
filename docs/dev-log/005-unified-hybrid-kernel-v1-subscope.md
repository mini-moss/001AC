# Dev Log · 005 — 架构重定义：双 ISA + 单一统一内核（v1.0 sub-scope）

> **Supersedes**：[004-rtos-to-hybrid-roadmap.md](004-rtos-to-hybrid-roadmap.md)（"AMP 借力" 思路整体推翻）
> **日期**：2026-06-07
> **范围**：v0.3 → v0.3.1（**架构重定义**，不是 scope 微调）
> **触发**：用户明确「一个 CPU 一个 OS 就能实现 RT+GPOS」，再叠加「双 ISA 都留 + v1.0 就统一」
> **对应交付物**：[requirements/requirements.md](../requirements/requirements.md) v0.3.1（待写）+ 4 份 ADR 大改（待写）

---

## 一句话

> **本项目 v1.0 = 单一统一内核，同时支持 RT 任务和 GPOS 任务，跑在双 ISA 上。**

这是从「RTOS 项目」到「混合内核项目」的架构性升级。**4 份 ADR 中 3 份要重写**。

---

## 决策分解

### 三个独立决定的叠加

1. **双 ISA 都留**（用户上一轮）：ARM Cortex-A (Pi 4/5) + RISC-V RV64 (SG2002)
2. **单 OS 走完**（这一轮）：不搞 AMP，不分 A53/C906，单一内核同时做 RT 和 GPOS
3. **v1.0 就统一**（这一轮）：进程隔离 + VFS 都是 v1.0 必有，不是 v2.0+

### 这意味着什么

- ❌ 推翻 dev-log/002 的「AMP 借力」思路
- ❌ 推翻 dev-log/004 的「v1.0 仍纯 RT」思路
- ❌ **推翻** ADR-0004（单地址空间 → 必须进程隔离）
- ❌ **重写** ADR-0001（双 ISA 仍留，但理由从「逼出 HAL」改成「同一 OS 双 ISA 验证可移植」）
- ❌ **重写** ADR-0002（调度从「单层 8 级优先级」改成「RT + 通用双队列」）
- ✅ ADR-0003（许可证）不动
- ➕ 新增 ADR-0005（用户态/内核态边界）
- ➕ 新增 ADR-0006（VFS 抽象）

---

## 现实范围检查

「双 ISA + 统一内核 + v1.0 含进程隔离 + VFS」≈ **Fuchsia / QNX 路线**。

| 参考 | 规模 | 团队 |
|---|---|---|
| Fuchsia Zircon | 10+ 人年 | Google 100+ 人 |
| QNX Neutrino | 30+ 人年 | BlackBerry QNX |
| seL4 verified | 2–3 人年 | 学术团队 |

**1–2 人年不可能做 Fuchsia 完整版**。所以 v1.0 sub-scope 是「**架构对，特性少**」：

### v1.0 sub-scope

| 模块 | v1.0 实现 | 留 v2.0+ |
|---|---|---|
| Boot | ✅ 2 ISA 跑 QEMU hello | U-Boot / 真板启动 |
| MMU | ✅ Sv48 / 4-level + 基础页表 | 大页 / IO 映射 |
| 调度 | ✅ **双队列**：RT（固定优先级 8 级）+ 通用（RR） | CFS / EDF |
| 进程隔离 | ✅ 每进程独立页表 + ASID | Capability 安全 |
| 上下文切换 | ✅ AArch64 EL1↔EL0 + RISC-V M↔U 切换 | 汇编优化 |
| 基础 syscall | ✅ `exit` / `write` / `read` / `yield` / `open` | 完整 POSIX |
| VFS | ✅ vfat + 字符设备 | ext4 / devfs / procfs |
| 用户态 | ✅ ELF 加载器 + 简单 shell | dynamic linker / libc |
| **总计** | **~1.5–2 人年** | 后续 5+ 年 |

**关键差别 vs Fuchsia**：
- v1.0 **不**做 capability
- v1.0 **不**做 verified
- v1.0 **不**做网络 / GUI / 完整 POSIX
- v1.0 **只**做「RT task 控电机 + 几个用户进程跑 shell + 看 vfat」

但**架构上**已经是"统一内核"，**不是**"RTOS 上面叠 GPOS"。

---

## 接下来按什么顺序改

按依赖关系，最小可评审批次：

### 批次 1（本 commit）

- dev-log/005（本文件）—— 把决策留底
- requirements.md v0.2 → **v0.3.1**：
  - §0 顶部「项目愿景」段
  - §1 「硬件平台」段：去掉 AMP 提及
  - §3 「调度策略」段：双队列（RT + RR）
  - §6 「不做什么」段：去掉「进程隔离是 v2.0+」
  - §7 「内存模型」段：单地址空间 → **每进程独立页表 + ASID**
  - 新增 §9 「v1.0 sub-scope 详细清单」

### 批次 2（下一 commit）

- ADR-0001 **重写**：双 ISA 单一统一内核
- ADR-0002 **重写**：双队列调度
- ADR-0004 **重写**（从「单地址空间」翻转为「进程隔离必有」）
- ADR-0005 **新建**：用户态/内核态边界
- ADR-0006 **新建**：VFS 抽象

### 批次 3（评审）

- 4 + 2 = **6 份 ADR 一起评审**（你点头才进 Phase 1）

---

## 风险（提前列）

1. **sub-scope 仍可能偏大**
   - 双 ISA × 统一内核 × 进程隔离 = 学习曲线极陡
   - 若 3 个月内 Boot + 双队列调度跑不通，**考虑先做 1 个 ISA**（Pi 4）
2. **GLIBC / newlib 选型是隐藏大坑**
   - 用户态程序必须链接 libc，写 syscall stub 是脏活
   - 建议 v1.0 用 **musl-libc**（最小可商用）
3. **Pi 4 真机启动 vs QEMU 仿真**
   - 树莓派 GPU 固件 + 设备树 + AArch64 内核镜像 格式 跟 QEMU 不一样
   - 留到 Phase 2 末再处理
4. **双 ISA 工具链维护成本**
   - 每个新代码改动要 2 套 CI 跑
   - 1–2 人年本来就紧，**CI 自动化是 v1.0 退出硬门槛**

---

## 我之前讲错的地方

我上一轮回答里说"AMP 是 v1.x 借力手段"——这是错的引导。**AMP 整个被推翻**。本日志 005 正式记录这次架构重定义。

---

## 接下来

1. dev-log/005（本文件）先 commit
2. 批次 1：requirements v0.3.1 改写
3. 批次 2：6 份 ADR（4 改 2 新）
4. 批次 3：整体评审
5. 整体 commit v0.3.1 + 6 ADRs
6. 不急，**不**在 v0.3.1 评审通过前进 Phase 1
