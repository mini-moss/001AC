# Dev Log · 004 — 项目愿景：RT → RT+GPOS 渐进式混合

> **Status**：**Superseded by [005-unified-hybrid-kernel-v1-subscope.md](005-unified-hybrid-kernel-v1-subscope.md)**（AMP 借力思路整体推翻；v1.0 直接走统一混合内核 + 进程隔离）
> **日期**：2026-06-07
> **范围**：v0.2 → v0.3 的**愿景升级**（不是 scope 升级）
> **触发**：用户在 4 份 ADR 评审后明确「RT+通用混合发展路线」是项目的 north star
> **对应交付物**：[requirements/requirements.md](../requirements/requirements.md) v0.3（待写）

> ⚠️ **本日志已被 005 推翻**。保留是为了记录"AMP 借力"曾经被认真考虑过及推翻的理由；当前生效的路线见 dev-log/005。

---

## 一句话

> **v1.0 交付纯 RTOS，但项目的 north star 是「RT + GPOS 渐进式混合」**——v1.x 借 AMP 用 Linux 的 GPOS，v2.x 起让 RTOS 自己造 GPOS。

---

## 关键澄清：什么是"渐进式"

不是"v1.0 就把 GPOS 全做完"，而是**分阶段推进**，每阶段都有**可演示的里程碑**：

| 版本 | 目标 | GPOS 来源 | 对应路线图 |
|---|---|---|---|
| **v1.0** | 纯 RTOS | — | 当前 Phase 1–10 |
| v1.1 | RT + A53 Linux 通过 RPMsg 通信 | 借力 Linux | 新增「AMP 集成」阶段 |
| v1.2 | micro-ROS XRCE-DDS 跨核（RT → ROS 2） | 借力 Linux | 在 v1.1 基础上加 micro-ROS 跨核 |
| v2.0 | 自有 VFS（vfat）+ 字符设备 | **自造** | 推翻 ADR-0004（加进程隔离） |
| v2.1 | lwIP 完整 + BSD socket | **自造** | — |
| v2.2 | POSIX 兼容 API（pthread / mmap） | **自造** | — |
| v2.3 | ELF 加载器 + shell | **自造** | — |
| v3.0+ | GUI（LVGL / Wayland） | **自造** | 独立子项目 |

**判断标准**：
- v1.0–v1.2：v1.0 ADR 完全不变，**新增** AMP 集成类 ADR
- v2.0+：v1.0 部分 ADR 会被推翻（如 ADR-0004），需要**复盘 + 新 ADR**

---

## 不变的（v1.0 红线）

这次升级**不重写** v1.0 决策：

- ✅ **ADR-0004 单地址空间**：v1.0 仍然正确，理由甚至更强了（**AMP 让 A53 承担 GPOS，RTOS 专心 RT**）
- ✅ **ADR-0002 调度**：8 级优先级 + 时间片 + PIP，v1.0 不动
- ✅ **ADR-0003 许可证**：Apache 2.0 不变
- ✅ **8 级优先级、64 任务上限、~20 μs 切换时间**

---

## 要改的（仅 3 处）

1. **requirements.md v0.2 → v0.3**：
   - §1 顶部加「项目愿景」段（明示 north star）
   - 加 §9「GPOS 路线图」（v1.0 → v3.0+）
   - §6「不做什么」表加注释：`v1.0 不做 ≠ 永远不做`
2. **ADR-0001 轻改**（仅 Decision 段加 1 句）：
   > "本项目的长期愿景是 RT+GPOS 混合（详见 [requirements §1 愿景 / §9 路线图](../requirements/requirements.md)）；v1.0 通过 SG2002 的 A53 Linux 走 AMP 路径部分实现。"
3. **未来新增 ADR**（不现在写）：
   - v1.1：ADR-0005（AMP / RPMsg / virtio-mmio 选型）
   - v2.0：ADR-0006（进程隔离选型，复盘 ADR-0004）
   - v2.2：ADR-0007（POSIX 兼容层范围）

---

## 我之前过度概括了什么

我上一轮回答里把 **AMP 当成了 v1.0 的"答案"**——这是**误导**。AMP 只是在 SG2002 上 v1.x 阶段**借用** Linux GPOS 的一种**手段**，**不是终点**。项目的目标是 v2.0+ 让 RTOS 自己具备 GPOS 能力。

修正后的措辞更准确：

> **SG2002 上的 AMP 是 v1.x 借力，不是 v1.0 的全部**

---

## 风险（提前列）

1. **「渐进式」听起来美好，每阶段都要重新验证**
   - v2.0 加进程隔离会推翻 ADR-0004，要复盘
   - v1.1 加 AMP 集成会改 Phase 1–6 工具链/启动路径
2. **v1.x "借 Linux" 看似不花钱，实际上要求 RTOS 能调 Linux 资源**
   - v1.1+ 的 virtio-mmio / RPMsg 是真功夫，**不是装个 Linux 就完事**
3. **GUI 是另一个量级的工程**
   - 建议放 v3.0 之后再说，**不要塞进 v2.x**
4. **「混合」≠「同时实现两套」**
   - v1.0 仍然**只**做 RT，**绝不**提前埋 POSIX 兼容层
   - 提前埋 = 提前污染，害大于利

---

## 接下来

1. dev-log/004（本文件）先 commit
2. requirements.md v0.2 → v0.3
3. ADR-0001 轻改 1 句
4. 整体评审 → 整体 commit v0.3
5. **不急**——v1.0 ADR 评审已经过了，红线部分不需要动
