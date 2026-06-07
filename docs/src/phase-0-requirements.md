# Phase 0 — 需求与范围定义

> **状态**：**进行中**（截至 v0.3.1）
> **退出门槛**：见 [requirements.md §8 Phase 0 退出](../requirements/requirements.md#8-验收标准phase-0-退出门槛)

Phase 0 已经历 3 次 scope 升级：

| 版本 | 范围 | 状态 |
|---|---|---|
| v0.1 | Cortex-M + RV32 + 无 MMU | Superseded |
| v0.2 | Cortex-A + RV64 + MMU + 单地址空间 | Superseded |
| **v0.3.1** | **双 ISA 统一混合内核 + 每进程独立页表 + 基础 VFS** | **当前** |

当前 Phase 0 交付物：

- **宪法**：[requirements.md](../requirements/requirements.md) v0.3.1（10 节）
- **ADR**（6 份）：
  - [ADR-0001 CPU 架构](../requirements/decisions/0001-cpu-arch.md)
  - [ADR-0002 调度算法](../requirements/decisions/0002-scheduler-algorithm.md)
  - [ADR-0003 许可证](../requirements/decisions/0003-license.md)
  - [ADR-0004 内存模型](../requirements/decisions/0004-single-address-space.md)
  - [ADR-0005 用户态/内核态边界](../requirements/decisions/0005-user-kernel-boundary.md)
  - [ADR-0006 VFS 抽象](../requirements/decisions/0006-vfs.md)
- **dev-log**：详见 [docs/dev-log/](../dev-log/)，重点读 [005](../dev-log/005-unified-hybrid-kernel-v1-subscope.md)

> **注**：本文件以前是 v0.1 路线下的「Phase 0 实施指南」（讲怎么挑 MCU、调度策略表格等）。现在 Phase 0 实际交付物已经迁到 [docs/requirements/](../requirements/)，本文件保留作为入口指针。

## 接下来

进入 [Phase 1 — 基础设施搭建](phase-1-infrastructure.md)（v0.3.1 升级版待写）。
