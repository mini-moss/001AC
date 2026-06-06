# Dev Log · 002 — Scope 升级：从 Cortex-M → Cortex-A + RV64

> **日期**：2026-06-07
> **范围**：Phase 0 · Step 1 的**修订**（不是新 Phase）
> **对应交付物**：[requirements/requirements.md](../requirements/requirements.md) v0.2
> **目录重组**：[docs/requirements/](../requirements/) 新建，docs/src/ 未动

---

## 这次改了什么

### 1. 硬件平台升级

| 维度 | v0.1（昨天） | v0.2（今天） | 影响 |
|---|---|---|---|
| ARM | Cortex-M4 (STM32F407) | **Cortex-A72/A76 (Pi 4/5)** | 改工具链、上下文切换实现、内存模型 |
| RISC-V | RV32IMAC (GD32VF103) | **RV64GC (SG2002 C906)** | 改工具链、ABI 改 lp64d、MMU 必做 |
| 仿真 | QEMU MPS2 + sifive_e | **QEMU raspi4b + virt(rv64)** | 两条仿真命令都重写 |
| MMU | ❌ 不做 | **✅ 必做** | 整个内核结构重写 |

### 2. docs/ 目录重组

```
之前
docs/
├── book.toml
├── src/                  # mdbook
├── requirements.md       ← 散在根
└── dev-log/

之后
docs/
├── book.toml
├── src/                  # mdbook，未动
├── requirements/         ← 新建
│   ├── requirements.md   ← 移到这里（v0.2）
│   └── decisions/        ← 下一步放 ADR
├── dev-log/
├── architecture/         ← 后续放架构图
└── reports/              ← 后续放测试报告
```

**为什么这样分**：

- `requirements/` 是**项目的「宪法」**（会被代码、ADR、测试反复引用），需要稳定路径。
- `decisions/` 是**对宪法的「解释」**（为什么这么定），跟宪法并列放方便交叉引用。
- `architecture/` 是**宪法的「图解」**（UML / 框图），放外面不混进 mdbook。
- `dev-log/` 是**时间线日志**（做了什么、为什么），跟最终文档解耦。

### 3. requirements.md 内部改动

- 第 1 节「硬件平台」：整段重写
- 第 2 节「实时性指标」：任务切换时间 10 μs → 20 μs（MMU 开销），RAM/ROM 改为「内核专用」
- 第 6 节「不做什么」：**删除「MMU / 虚拟内存」**那条，**新增**「用户态进程隔离」「IOMMU」两条 v1.0 不做
- 第 7 节「内存模型」：**全新**，48-bit 虚拟地址布局 + Sv48 / 4-level page table
- 任务数 32 → 64

---

## 为什么这个升级值得做

### 反驳「为什么不直接用 STM32」

| 角度 | Cortex-M | Cortex-A |
|---|---|---|
| **学习价值** | 写裸 RTOS 入门简单 | 真正理解现代操作系统的 MMU / EL / GIC / SVC |
| **工业契合度** | 适合电机控制、传感器节点 | **机器人主控（Pi 4 + Ubuntu）就是 Cortex-A** |
| **可扩展性** | 加 GUI / 网络是外挂 | 直接接 ROS 2 / Linux 工具链 |
| **运行 micro-ROS** | 跑得动，但工具链割裂 | micro-ROS agent + ROS 2 全套天然适配 |

> **关键洞察**：现在 90% 的机器人主控跑 Linux，那台 Linux 机器的 CPU 几乎都是 Cortex-A。
> 你写的 RTOS **不是要替代 Linux**，而是要**跑在 Linux 同款硬件的某些核上**（AMP / 异构），或者**裸机接管某个实时关键路径**（电机控制环）。
> 这正是 SG2002 的用法：A53 跑 Linux + C906 跑裸 RTOS。

### 反驳「为什么不只做 ARM 一个」

写 RTOS 最容易陷入「在某个 ISA 的特殊寄存器里写太多糖」，第二个 ISA 是**逼出 HAL 层的唯一方法**。
RV64 不是选来「将来用的」，是**现在就要能跑**，这样 HAL 设计从一开始就是干净的。

---

## 这个升级让我们**多做什么**（按 Phase 排序）

### Phase 0（本 Phase）

- [ ] ADR-0001 改写：从「M4 + RV32」改为「A72 + RV64 + 必须 MMU」
- [ ] ADR-0004 新建：「v1.0 单地址空间 vs 多地址空间」

### Phase 1（基础设施）

- 工具链：arm-none-eabi → **aarch64-elf**；riscv32 → **riscv64-elf**
- 启动：原本只需要「设置 MSP + 跳 main」，现在要「**设置 EL、配置 GIC/PLIC、建第一份页表、跳 main**」
- 链接脚本：原本 `.text` 放 flash 即可，现在要**显式指定虚拟地址 vs 物理地址映射**

### Phase 2（HAL）

- 异常级别（EL）/ 异常向量表：AArch64 4 级 EL、RISC-V 3 种特权模式
- 上下文切换：原本只需保存 r4-r11, sp, lr；现在要保存 **SP_ELx, ELR, SPSR, TPIDRRO_EL0** 等
- GICv3 / PLIC 驱动：M0+ 的 NVIC 简单，GICv3 多 distributor + redistributor
- MMU 驱动：**新加**（页表建立、TLB invalidate、ASID 管理）

### Phase 3（调度器）

- 任务切换：原 PendSV → AArch64 的 **SVC + eret**、RISC-V 的 **ecall + sret**
- 优先级位图算法不变，但**调度点**变多（新增 page fault handler 也是调度点）

### Phase 5（内存管理）

- 原本「固定分区 + 最佳适配堆」够用
- 现在要加：**虚拟内存分配器**、**按需分页**、**缺页中断**、**copy-on-write**（v2.0）

### Phase 7（micro-ROS）

- micro-ROS 之前需要 Linux 同侧跑 agent，现在可以**在 Cortex-A 上同时跑 RTOS + 共享内存通信**到 Linux 侧的 micro-ROS agent

---

## 哪些**不变**

- C11 编码标准
- 命名约定（`module_action()` / `module_name_t` / `UPPER_SNAKE`）
- API 风格（`task_create` / `sem_take`）
- Apache 2.0 许可
- 调度策略（固定优先级 + 时间片 + PIP）
- 头文件保护、`.clang-format`、`cppcheck` 静态分析

> 这些是**架构无关的好习惯**，Cortex-M 上这样写、换 Cortex-A 也这样写、不变才说明设计抽象到位。

---

## 风险与未决项

1. **Pi 4 vs Pi 5 选哪个？** 两者都支持，但 Pi 5 的 GIC 配置有差异，建议**先 Pi 4 跑通、Pi 5 当 v1.1 验证**。
2. **SG2002 启动模式**：默认从 A53 启动 Linux，C906 跑裸机需要厂商提供的**二级启动协议**（pico-SoC SDK / Sophgo SDK）。这条路径**比想象中复杂**，Phase 1 留出时间做 PoC。
3. **QEMU 对 SG2002 的支持**：`qemu-system-riscv64 -machine virt` 是**通用** RISC-V 64 板，**不是 SG2002 真实硬件**。Phase 1 仿真通过 ≠ Phase 6 真机能跑。**真机调试是 Phase 2 末的硬门槛**。
4. **micro-ROS + RV64**：micro-ROS 官方主要支持 32 位，64 位下要重新评估 transport（XRCE-DDS 的内存对齐）。这是 Phase 7 的事，**先记下来**。

---

## 下一步

按 todo 走 → **Step 2：写 3 份 ADR**（其中 ADR-0001 要按 v0.2 改写）。
ADR 是「**为什么这么选**」的存档，跟 requirements.md 互补。

**不急**。先看 [requirements/requirements.md](../requirements/requirements.md) v0.2，告诉我哪些要再调；都 OK 我们进 Step 2。
