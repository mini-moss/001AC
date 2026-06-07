# ADR-0001 — CPU 架构选型（Cortex-A + RISC-V RV64）

> **Status**：Accepted（v0.3.1 重写：rationale 调整 + AMP 提及移除 + 90 天止损联动）
> **Date**：2026-06-07
> **Supersedes**：无（原 v0.1 选 Cortex-M4 + RV32IMAC，2026-06-07 同日被本 ADR 推翻）
> **Deciders**：junbo.dai
> **Refs**：[requirements §1.1](../requirements.md#11-cpu-架构) · [requirements §9](../requirements.md#9-v10-sub-scope-详细清单) · [dev-log/002](../../dev-log/002-scope-upgrade-to-mmu.md) · [dev-log/005](../../dev-log/005-unified-hybrid-kernel-v1-subscope.md)

---

## Context

需要选 1–2 个 CPU 架构来**支撑一个机器人 RTOS 项目**。约束：

1. **必选 2 个架构**：单一 ISA 写不出干净的 HAL 层（会在 ISA 特殊寄存器里堆糖）。
2. **必须能跑通真机 / 仿真**：学生级 + 工业级都能验证。
3. **2026 年的机器人主控现实**：90% 跑 Linux，主控都是 Cortex-A 或 RISC-V 应用处理器。
4. **学习价值**：要能学到现代操作系统的 MMU / EL / GIC，不是「教学简化版」。
5. **v0.3.1 新增**：**同一份内核源码**在两个 ISA 上各跑一份——验证设计可移植，**不**走 AMP 异构（一边 Linux 一边 RTOS）。

---

## Decision

**采用 ARM Cortex-A (AArch64) + RISC-V RV64GC 作为双架构**：

| 架构 | ISA | 工具链 | 仿真 | 真机参考板 |
|---|---|---|---|---|
| ARM | AArch64 | `aarch64-elf-gcc` | `qemu-system-aarch64 -machine raspi4b` | Raspberry Pi 4 / Pi 5 |
| RISC-V | RV64GC（lp64d） | `riscv64-elf-gcc` | `qemu-system-riscv64 -machine virt` | Sophgo SG2002 (C906 核) |

**关键约束**：

- 两个架构**都带 MMU**，虚拟内存**必做**
- v1.0 用**每进程独立页表 + 16 位 ASID**（统一混合内核，RT + GPOS 共存）— 详见 ADR-0004 重写版
- QEMU 是主线开发环境，真机是 Phase 2 末的硬门槛

---

## Consequences

### 积极

- ✅ **贴近工业现实**：Pi 4 + Linux 几乎就是机器人主控的标配，我们写的 RTOS 跑在「同款硬件的某些核上」是真实场景
- ✅ **真正的 HAL 层**：AArch64 与 RV64 的异常级别、寄存器命名、内存模型都不同，HAL 不可能藏 ISA 糖
- ✅ **未来上 micro-ROS + ROS 2 工具链顺滑**：AArch64/RV64 是 ROS 2 官方主支持目标
- ✅ **可验证 OS 可移植性**（v0.3.1 取代 AMP 论述）：同一份 kernel 源码在 ARM 与 RISC-V 上分别编译运行——HAL 层若藏 ISA 糖会立刻在另一个 ISA 上暴露

### 消极

- ❌ **MMU 强制必做**：v0.1 的「无 MMU 简化」没了；启动、上下文切换、ISR、cache 一致性都要重新设计（见 [dev-log/003](../../dev-log/003-mmu-rtos-tensions.md)）
- ❌ **学习曲线陡**：AArch64 比 Cortex-M 复杂很多（4 级 EL、TTBR、SVC/eret），RV64 也比 RV32 多一堆陷阱
- ❌ **QEMU 仿真 ≠ 真机**：SG2002 没有原生 QEMU 模型，用通用 `virt` 板仿真，**真机调试会暴露仿真看不到的问题**
- ❌ **Pi 4 启动路径非标准**：树莓派启动由 GPU 固件加载，不像 QEMU `virt` 板那样直接 `-kernel`，真机烧录有额外步骤

### 中性

- `aarch64-elf-gcc` 与 `riscv64-elf-gcc` 工具链**不通用**，每个架构单独配
- QEMU 两条命令分别维护，**build 脚本要识别目标**

---

## Alternatives Considered

### 备选 A：维持原 Cortex-M4 + RV32IMAC（v0.1 方案）

- ✅ 更简单（无 MMU）
- ✅ 学习曲线友好
- ❌ **不贴近工业现实**（机器人主控几乎不用 Cortex-M）
- ❌ **ARM Cortex-M4 + RV32IMAC** 跟 SG2002 + Pi 4/5 **不同 ISA 族**，等于双倍简化版 = 学不到东西
- **结论**：用户已明确选 Pi 4 + SG2002，本备选已被推翻

### 备选 B：单架构（AArch64 only）

- ✅ 工具链、build 脚本、测试都只需一套
- ❌ **写不出干净的 HAL**：你会忍不住在寄存器糖里加特定 ISA 的 hack
- ❌ 学习价值减半（缺 RV64 这面镜子）

### 备选 C：Cortex-M85（带 FPU + 少量 MPU）做 ARM 端

- ✅ 兼顾 MCU 简单性 + 一定内存保护
- ❌ **生态割裂**：M85 还是新东西，QEMU 支持弱
- ❌ **不是工业现实**：机器人主控不用 Cortex-M
- **结论**：跟 v0.1 一样的问题，pass

---

## Validation（如何验证这条决策是对的）

- [ ] Phase 1 退出时：QEMU raspi4b + virt(rv64) 都跑通 hello world
- [ ] Phase 2 末：Pi 4 真机能裸跑 RTOS
- [ ] **v0.3.1 新增**：Phase 1 后 90 天止损 ——双 ISA 任一 QEMU hello 不通过，触发 [requirements §8](../requirements.md#phase-1-启动-3-个月止损检查点) 的回退选项 A
- [ ] Phase 5 末：能在两个架构上正确处理 page fault（验证 MMU 真开着）
- [ ] Phase 6 末：两个架构都能驱动电机 + 读编码器（验证 HAL 真的可移植）

任一项不达标 → 重新评估，可能降级到备选 A。

---

## Notes

- 本 ADR 推翻 v0.1 时的选择，理由记录在 [dev-log/002](../../dev-log/002-scope-upgrade-to-mmu.md)
- 「Pi 4 vs Pi 5」「SG2002 启动路径」「QEMU 与真机的偏差」这 3 个**遗留风险**详见 dev-log/002 末「风险与未决项」
