# 需求规格说明书（Requirements）

> **状态**：v0.3.1（**架构重定义**：纯 RTOS → 双 ISA 统一混合内核）
> **对应阶段**：Phase 0 — 需求与范围定义
> **变更方式**：所有改动需在 [dev-log](../dev-log/) 留痕，重大改动需同步更新本文件

本文是整个项目的「宪法」。所有架构、API、驱动设计都必须能在本文中找到依据，反过来发现某条设计违反本文时，先改本文再改代码。

> ⚠️ **v0.3.1 重大变更**：从 v0.2 的「单地址空间纯 RTOS」翻转为「**双 ISA 统一混合内核 + 每进程独立页表 + 基础 VFS**」。
> 后果：调度器、内存模型、用户态/内核态边界、VFS 抽象全部重新设计；3 份 ADR 重写 + 2 份 ADR 新建。
> 详细影响见 [dev-log/005](../dev-log/005-unified-hybrid-kernel-v1-subscope.md)；v0.1→v0.2 的 MMU 升级历史见 [dev-log/002](../dev-log/002-scope-upgrade-to-mmu.md)。

---

## 0. 项目愿景

本项目长期目标：构建一个**双 ISA 统一混合内核**——同一份内核代码同时调度硬实时任务（控制电机、读传感器）与通用任务（运行 shell、读 vfat），跨 ARM Cortex-A 和 RISC-V RV64 验证设计的可移植性。

**v1.0 交付物**：

| 维度 | v1.0 做 | v2.0+ 留 |
|---|---|---|
| 架构 | 统一内核（非"RTOS 叠 GPOS"） | — |
| 内存 | 每进程独立页表 + ASID | 大页 / IOMMU |
| 调度 | 双队列（RT 8 级 + 通用 RR） | EDF / CFS |
| 隔离 | 用户态 ELF + 基础 syscall | Capability / verified |
| 文件 | vfat + 字符设备 | ext4 / 网络 / GUI |

**参考定位**：Fuchsia Zircon / QNX Neutrino 的精简起步版本，目标 1.5–2 人年完成 sub-scope。

详细范围见 §9 sub-scope 清单；背景见 [dev-log/005](../dev-log/005-unified-hybrid-kernel-v1-subscope.md)。

---

## 1. 硬件平台

### 1.1 CPU 架构

**必选**（双 ISA 跑**同一份**内核代码，强制把 ISA 相关逻辑收敛到 HAL 层；**不是** AMP 异构）：

| 架构 | ISA | 代表芯片 | 选择理由 |
|---|---|---|---|
| **ARM Cortex-A** | AArch64 | Raspberry Pi 4 (A72) / Pi 5 (A76) | 主流通用处理器、生态最广、能跑 Linux/ROS 2 完整工具链 |
| **RISC-V** | RV64GC | Sophgo SG2002 (C906) | 开源 ISA、面向未来、SG2002 是国产主流 RISC-V 64 SoC |

> **v0.3.1 关键澄清**：本项目是**单一内核架构**——同一份内核源码在 ARM 与 RISC-V 上各自编译/运行一份，**不是** "ARM 跑 Linux + RISC-V 跑 RTOS" 的 AMP 异构。
> 选两个 ISA 的工程意义：跨架构验证设计真的可移植 + 强制 HAL 层不藏 ISA 糖。
> v0.1→v0.2 的 ISA 升级历史见 [dev-log/002](../dev-log/002-scope-upgrade-to-mmu.md)；MMU 决策见 [ADR-0001](decisions/0001-cpu-arch.md)。

### 1.2 参考开发板

- **ARM**：Raspberry Pi 4 Model B / Raspberry Pi 5（任选其一，Phase 2 之后定型）
- **RISC-V**：SG2002 开发板（如 **LicheeRV Nano** / **Milk-V Duo** / **SG2002 评估板**）

### 1.3 仿真平台

QEMU 是主线开发/调试环境，**两个架构都用 QEMU**：

| 架构 | QEMU 命令 | 备注 |
|---|---|---|
| ARM | `qemu-system-aarch64 -machine raspi4b -kernel ...` | Pi 4 / Pi 5 模型均支持 |
| RISC-V | `qemu-system-riscv64 -machine virt -cpu rv64 -bios none` | SG2002 无原生 QEMU 模型，用 `virt` 通用 RISC-V 64 板仿真（需注明偏差） |

> **真机烧录** 留到 Phase 2 之后；QEMU 跑通是真机前唯一硬门槛。

---

## 2. 实时性指标

> 数字决定了调度器、上下文切换、MMU 配置的设计。**越紧越难实现**，不要随便改。
> v0.3.1 引入进程隔离后，「任务切换」分两种：同进程内 task 切换（不切 ASID）vs 跨进程 process 切换（切 ASID + 可能 TLB 抖动）。

| 指标 | 目标值 | 测量方法 |
|---|---|---|
| 中断响应时间（GIC/PLIC 入口到首条指令） | < 10 μs | GPIO 翻转 + 示波器（裸模式）/ 逻辑分析仪 |
| 任务切换时间（同进程，不切 ASID） | < 20 μs | 桩函数 + GPIO 翻转（kernel 内 task 间切换） |
| 进程切换时间（跨进程，切 ASID） | < 50 μs | 桩函数 + GPIO 翻转（含 TTBR0/satp 写入 + ASID 切换） |
| Tick 周期（默认） | 1 ms | 配置宏 `TICK_RATE_HZ=1000` |
| Tick 周期（高精度模式） | 100 μs | 配置宏 `TICK_RATE_HZ=10000` |
| WCET（每 API） | 文档化 | Tracealyzer / 手动插桩 |
| **内核 RAM** | < 512 KB | 内核专用静态分配（含 VFS / ELF 加载器 / 进程表），不含用户态堆 |
| **内核 ROM** | < 2 MB | 内核镜像（含 .text / .rodata / 启动 stub / vfat 后端 / syscall 分发） |
| **用户可用 RAM** | < 数 GB | 受 SoC 限制（Pi 4 = 1/2/4/8 GB，SG2002 = 256 MB） |

> **v0.3.1 变化**：
> - 任务切换拆为「同进程 / 跨进程」两行——跨进程含 ASID 切换 + 可能的 TLB 抖动（见 ADR-0004 重写版）
> - 内核 RAM/ROM 上限按 sub-scope 倍放宽——VFS + ELF 加载器 + syscall 层进来后的诚实预算（512 KB / 2 MB）
> - RAM/ROM 上限 **针对内核本身**，不再限制用户态可用内存
> - 数字仍偏紧，是「达标即合格」的目标，不是「做不到就改需求」

---

## 3. 调度策略

v0.3.1 采用**双队列调度器**：RT 队列承担硬实时调度单元（电机控制、传感器、ISR 唤醒响应；可以是内核线程或申请了 RT 优先级的用户态进程），通用队列承担普通用户进程（shell、文件 IO、批处理）。两队列**严格按优先级抢占**——RT 队列非空时永远优先于通用队列。

### 3.1 RT 队列

| 策略 | 是否支持 | 备注 |
|---|---|---|
| 固定优先级抢占式 | ✅ 主调度 | 8 级（0–7，7 最高） |
| 同优先级时间片轮转 | ✅ | 默认 10 ms |
| 优先级继承协议（PIP） | ✅ 互斥锁 | 仅 RT 队列内部 |
| 用户态 RT 进程 | ✅ | syscall `sched_set_rt()`，详见 ADR-0005 |
| 最早截止优先（EDF） | ⚪ 插件 | v1.1+ |
| 速率单调（RMS） | ⚪ 仅分析工具 | 不进调度器 |

**RT 调度单元上限**：64（内核线程 + 用户态 RT 进程共用此池）

### 3.2 通用队列

| 策略 | 是否支持 | 备注 |
|---|---|---|
| 时间片轮转（RR） | ✅ 主调度 | 默认 100 ms 时间片 |
| 优先级 | ❌ | v1.0 通用进程一视同仁 |
| nice / 公平调度 | ⚪ | v1.1+ 评估 |

**通用进程上限**：32

### 3.3 队列间策略

- **严格抢占**：RT 队列就绪 → 立即抢占当前通用进程
- **跨进程 RT 切换**：用户态 RT 进程互切要付 ASID 切换 + TLB 抖动代价（见 §2 跨进程切换 < 50 μs 指标）
- **饿死风险**：RT 占满 CPU 时通用永远饥饿——§8 验收要求 RT 队列 < 80% CPU 占用为软门槛
- 详细设计与替代方案（含 RT-budget 等）见 [ADR-0002 重写版](decisions/0002-scheduler-algorithm.md)

### 3.4 SMP

| 策略 | 是否支持 | 备注 |
|---|---|---|
| SMP 多核调度 | ⚠️ v1.0 不做 | TCB / 进程表预留 `cpu_mask` 字段，v2.0 路线保留 |

---

## 4. API 风格

v0.3.1 起 API 分两层：

### 4.1 内核态 API（kernel thread / 调度器内部）

参考 FreeRTOS / Zephyr 的「名词_动词」式（沿用 v0.2）：

```c
task_t *task_create(const char *name,
                    void (*entry)(void *),
                    void *arg,
                    size_t stack_size,
                    int priority);
void    task_yield(void);

sem_t  *sem_create(int initial_count, int max_count);
int     sem_take(sem_t *sem, tick_t timeout);
int     sem_give(sem_t *sem);
int     sem_give_from_isr(sem_t *sem);
```

### 4.2 用户态 syscall（用户进程跨边界调用）

POSIX-ish 命名 + 数值化 syscall 号（详见 ADR-0005）：

```c
// 进程
int   sys_exit(int code);
pid_t sys_getpid(void);
int   sys_sched_set_rt(int priority);  // R2 路径：升级到 RT 队列

// IO
ssize_t sys_read(int fd, void *buf, size_t n);
ssize_t sys_write(int fd, const void *buf, size_t n);
int     sys_open(const char *path, int flags);
int     sys_close(int fd);

// 调度
void    sys_yield(void);
```

### 4.3 文档要求

每个公开 API（不分两层）必须文档化：

- 参数、返回值
- **是否可在 ISR 中调用**（仅适用于内核态 API）
- 时间复杂度（`O(1)` / `O(n)`）
- 阻塞语义
- **内存影响**（是否触发 page fault / TLB shootdown / 跨核 IPI）
- **syscall 专属**：syscall 号、寄存器约定、是否会被信号打断（v1.0 无信号 → 留 `EINTR` 不可能的注脚）

---

## 5. 许可协议

| 类别 | 协议 | 理由 |
|---|---|---|
| 自身代码 | **Apache 2.0** | 明确专利授权，工业机器人场景友好 |
| 第三方 | 各自保留 | micro-ROS = Apache 2.0；lwIP = BSD；LVGL = MIT |

> FatFS 协议评估结论同 v0.1：暂不引入，需时用 Petit FatFS / IoT FatFS 替代。

---

## 6. 不做什么（Out of Scope）

v0.3.1 把 v0.2 的「进程隔离 = v2.0+」推翻——v1.0 sub-scope 现在**做** 进程隔离 + 基础 VFS + 用户态 ELF。但 sub-scope 边界其他项**全部保持不做**：

| 项 | v1.0 | v2.0+ | 原因 |
|---|---|---|---|
| ❌ SMP 多核 | 不做 | 评估 | TCB/进程表已预留 `cpu_mask` |
| ❌ Capability 安全模型 | 不做 | 评估 | seL4 风格；v1.0 走"信任所有进程"路线 |
| ❌ 形式化验证（verified） | 不做 | 不规划 | 学术目标，超 sub-scope |
| ❌ 完整 POSIX 兼容 | 不做 | 部分 | v1.0 仅 §4.2 的 8 个 syscall；完整 POSIX 留 v2.2 |
| ❌ fork / exec 分离语义 | 不做 | 评估 | v1.0 用 `sys_exec_elf()` 直接创建进程 |
| ❌ 信号（signal） | 不做 | 评估 | 留 v2.x；v1.0 syscall 无 `EINTR` 路径 |
| ❌ 网络协议栈（lwIP / TCP / socket） | 不做 | 做 | 留 v2.1 |
| ❌ GUI（LVGL / Wayland / framebuffer） | 不做 | 做 | 留 v3.0+，独立子项目 |
| ❌ 动态加载 / 共享库 | 不做 | 评估 | v1.0 仅静态链接 ELF |
| ❌ 自研 DDS | 不做 | 不规划 | 用 micro-ROS XRCE-DDS（v1.2 集成路径） |
| ❌ 大页 hugepage | 不做 | 评估 | v1.0 仅 4 KB 页 |
| ❌ IOMMU / 设备虚拟化 | 不做 | 评估 | v1.0 设备直驱 |
| ❌ 安全功能认证（IEC 61508 / ISO 26262） | 不做 | 不规划 | 工程量超 1 数量级 |

> **v0.3.1 关键变化**：v0.2 的"用户态进程隔离 = v1.0 不做"被推翻——现在是**必做**，详见 §0 / §7 / [ADR-0004 重写版](decisions/0004-single-address-space.md)。
>
> **"不做"≠"永远不做"**——本表是 v1.0 sub-scope 的红线，每一项的"v2.0+"列指向后续版本扩展方向（背景：dev-log/004 / 005）。

---

## 7. 内存模型（v0.3.1 重写）

v0.2 的"单地址空间"在 v0.3.1 被推翻：每个用户进程有**独立的页表 + 独立的 ASID**，内核态（EL1 / Supervisor）通过映射进每个进程页表的**高半区**访问。详细设计见 [ADR-0004 重写版](decisions/0004-single-address-space.md)。

### 7.1 地址空间布局（每进程独立）

```
虚拟地址空间（48 位，canonical）
┌─────────────────────────────────────┐ 0xFFFF_FFFF_FFFF
│   内核高半区（所有进程共享映射）       │
│   - 内核 .text / .rodata             │
│   - 内核栈 / 调度器 / 进程表          │
│   - 设备 MMIO 直接映射                │
│   AArch64: TTBR1_EL1                │
│   RISC-V:  高位映射（按 Sv48 约定）   │
├─────────────────────────────────────┤ 0xFFFF_0000_0000
│   ...（canonical hole，硬件强制）     │
├─────────────────────────────────────┤ 0x0000_8000_0000
│   进程私有低半区（每进程独占）         │
│   - 用户 .text / .data / .bss        │
│   - 用户栈（按需分页）                 │
│   - 用户堆 / mmap（v2.0+）            │
│   AArch64: TTBR0_EL1（切进程时切换）  │
│   RISC-V:  satp（切进程时切换）       │
├─────────────────────────────────────┤
│   0x0000_0000_0000                  │ ← 零页（未映射，防 NULL deref）
└─────────────────────────────────────┘
```

### 7.2 页表

- **粒度**：4 KB（v1.0），2 MB hugepage 留 v2.0
- **算法**：Sv48（RISC-V）/ 4-Level Translation Table（ARMv8 48-bit VA）
- **内核页表**：启动时建好；它的**高半区条目被复制进每个用户进程的页表**——所以内核访问不需要切页表
- **用户页表**：**每进程一份**；进程创建时 `mm_alloc()` 分配，进程退出时 `mm_free()` 回收

### 7.3 TLB / ASID 管理

- **ASID**：v1.0 **启用** 16 位 ASID（AArch64 `TCR_EL1.A1` + `TTBR0_EL1.ASID`；RISC-V Sv48 `satp.ASID`）
- **进程切换**：写 `TTBR0_EL1`（AArch64）/ `satp`（RISC-V），ASID 一并切换
- **TLB 维护**：进程切换时**不主动 flush**——ASID 隔离让旧条目自动失效
- **跨核 shootdown**：v1.0 单核**不需要**；v2.0 SMP 用 IPI

### 7.4 ASID 回收

16 位 ASID 上限 = 65535，远超 32 通用进程上限（§3.2），v1.0 **不需要 ASID 回收算法**。

> **v0.3.1 重大变化**：v0.2 的"单地址空间，所有 task 共享页表"被推翻。后果：
>
> - 任务切换时间分两档（§2）
> - syscall 入口要切到内核栈（ADR-0005）
> - 用户访存要走 `copy_from_user()` / `copy_to_user()` 跨边界（ADR-0005）

---

## 8. 验收标准（Phase 0 退出门槛）

### Phase 0 退出（进 Phase 1 前必须勾完）

- [x] 硬件平台、实时指标、调度策略与 v0.3.1 sub-scope 一致（§1 / §2 / §3）
- [x] 「不做什么」列表明确（§6，含 sub-scope 红线）
- [x] 内存模型明确（§7，每进程独立页表 + ASID）
- [x] §0 项目愿景 + §9 sub-scope 详细清单
- [x] 6 份 ADR 全部写完：
  - [x] ADR-0001 重写（双 ISA 单一统一内核）
  - [x] ADR-0002 重写（双队列调度，含 R2 用户态 RT）
  - [x] ADR-0003 保留不动（Apache 2.0）
  - [x] ADR-0004 重写（每进程独立页表 + ASID）
  - [x] ADR-0005 新建（用户态/内核态边界 + syscall ABI）
  - [x] ADR-0006 新建（VFS 抽象）
- [x] 整体自评审通过（dev-log/005 + 6 份 ADR + requirements v0.3.1 一致）

### Phase 1 启动 3 个月止损检查点

参考 [dev-log/005 §风险 1](../dev-log/005-unified-hybrid-kernel-v1-subscope.md)，Phase 1 启动后 90 天内必须达成：

- [ ] **两个 ISA** 都跑通 QEMU hello world（带 MMU、用户态 ELF）
- [ ] **双队列调度器** 跑通：RT 任务抢占通用进程 + 同优先级 RR 切换

**任何一项未达成** → 触发 sub-scope 复盘：

- 选项 A：降级到**单 ISA**（推荐 Pi 4，留 RV64 给 v1.1）
- 选项 B：把"用户态 RT 进程"（R2 路径）推迟到 v1.1，v1.0 只做 R1 路径
- 选项 C：评审是否要把进程隔离整个推到 v2.0（回到 v0.3.0 的纯 RT 路线）

### Phase 0 退出软门槛（不阻塞）

- [ ] 一个 5 分钟的演示动画 / 视频，向第三方解释「v1.0 做什么、为什么、不做什么」
- [x] markdown linter 配置对齐项目 idiom（MD060 / MD028 / MD032 收尾）

---

## 9. v1.0 sub-scope 详细清单

来源：[dev-log/005](../dev-log/005-unified-hybrid-kernel-v1-subscope.md)。本节是 §6「不做什么」的对偶——明确**做什么**。

| 模块 | v1.0 实现 | 留 v2.0+ | 对应 ADR / 章节 |
|---|---|---|---|
| Boot | 双 ISA QEMU hello（Pi 4 + virt RISC-V） | U-Boot / 真板启动 | ADR-0001 |
| MMU | Sv48 / 4-level 基础页表，单核 | 大页 / IOMMU | ADR-0004 / §7 |
| 调度 | 双队列：RT 8 级抢占 + 通用 RR；含 PIP | EDF / CFS / SMP | ADR-0002 / §3 |
| 进程隔离 | 每进程独立页表 + 16 位 ASID | Capability / verified | ADR-0004 / §7 |
| 上下文切换 | AArch64 EL1↔EL0 + RISC-V M↔U | 汇编手工优化 | ADR-0005 |
| Syscall | 8 个最小集（§4.2） | 完整 POSIX | ADR-0005 / §4 |
| VFS | vfat + 字符设备 | ext4 / devfs / procfs / 网络 | ADR-0006 |
| 用户态 | ELF 加载器 + 静态链接 + 简单 shell | 动态链接 / libc / signal | ADR-0005 / §6 |
| 测试 | QEMU 自动化 + Pi 4 真机 smoke | 完整 CTS / fuzzing | Phase 9 |

**预算**：1.5–2 人年（参考 Fuchsia Zircon 10+ 人年 / QNX 30+ 人年，本项目 sub-scope 是骨架版）

**v1.0 不在表中的项**：见 §6

---

## 变更记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v0.1 | 2026-06-07 | 初稿：Cortex-M4 + RV32IMAC，无 MMU |
| v0.2 | 2026-06-07 | **scope 升级**：Cortex-A (Pi 4/5) + RV64 (SG2002 C906)，**必须做 MMU**；新增第 7 节内存模型；任务数 32→64 |
| v0.3.1 | 2026-06-07 | **架构重定义**：纯 RTOS + 单地址空间 → 双 ISA 统一混合内核 + 进程隔离 + VFS；新增 §0 项目愿景；3 份 ADR 待重写 + 2 份新建 |

