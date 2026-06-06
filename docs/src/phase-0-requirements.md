# Phase 0 — 需求与范围定义

> **预估周期**：1–2 天
> **进入门槛**：仓库已创建
> **退出标志**：`docs/requirements.md` 完成并经过评审

这个 Phase 不写代码，但**至关重要**。它把后续所有决策固定下来，避免在写到 Phase 5 才发现选错了 MCU。

## 1. 硬件平台

### 1.1 CPU 架构
**建议**：先选 **2 个** 足以逼出可移植层
- **首选架构 A**：ARM Cortex-M4（生态最广，工具链最成熟）
- **首选架构 B**：RISC-V（ESP32-C3 / GD32V 系列，开源、面向未来）

> 如果团队人手有限，第 1 阶段也可以只做一个架构，但要在 HAL 层保持清晰边界。

### 1.2 开发板
每架构选 1–2 块参考板：
- ARM：STM32F407 Discovery / NUCLEO-F446ZE（带板载 ST-Link）
- RISC-V：ESP32-C3 DevKit / GD32VF103 V2

### 1.3 仿真平台
- QEMU 是前 3 个 Phase 的主要调试环境
- `qemu-system-arm -machine mps2-an385`（Cortex-M3 仿真）
- `qemu-system-riscv32 -machine sifive_e`

## 2. 实时性指标

在 `requirements.md` 中量化以下指标：

| 指标 | 目标值 | 测量方法 |
|---|---|---|
| 中断响应时间 | < 5 μs | GPIO 翻转 + 示波器 |
| 任务切换时间 | < 10 μs | PendSV 入口到出口 |
| Tick 周期 | 1 ms（默认） / 100 μs（高精度模式） | 配置宏 |
| 最坏执行时间（WCET） | 每 API 文档化 | Tracealyzer / 插桩 |
| RAM 上限 | < 32 KB | 链接脚本报告 |
| ROM 上限 | < 128 KB | 链接脚本报告 |

## 3. 调度策略

| 策略 | 是否支持 | 备注 |
|---|---|---|
| 固定优先级抢占式 | ✅ 主调度 | 必须支持 |
| 时间片轮转 | ✅ 同优先级任务间 | 必须支持 |
| 最早截止优先 (EDF) | ⚪ 可选 | 通过插件形式 |
| 速率单调 (RMS) | ⚪ 可选 | 通过分析工具支持 |
| 优先级继承协议 | ✅ 互斥锁 | 必须支持，防优先级反转 |

## 4. API 风格

参考 FreeRTOS / Zephyr 风格：

```c
// 创建任务
task_t *task_create(const char *name,
                    void (*entry)(void *),
                    void *arg,
                    size_t stack_size,
                    int priority);

// 信号量
sem_t *sem_create(int initial_count, int max_count);
int sem_take(sem_t *sem, tick_t timeout);
int sem_give(sem_t *sem);
int sem_give_from_isr(sem_t *sem);
```

每个 API 必须文档化：
- 参数、返回值
- 是否可在 ISR 中调用
- 时间复杂度（O(1) / O(n)）
- 阻塞语义

## 5. 许可协议

**建议**：
- 自身代码：**Apache 2.0**（明确专利授权，工业友好）
- 第三方集成：保留各自许可（micro-ROS 走 Apache 2.0，lwIP 走 BSD，LVGL 走 MIT）

## 6. 不做什么（Out of Scope）

把范围控制写进需求文档：

- ❌ MMU / 虚拟内存
- ❌ SMP 多核（v1.0）
- ❌ POSIX 严格兼容
- ❌ 自研 DDS / TCP/IP
- ❌ 安全功能认证

## 7. 交付物

完成本 Phase 后，仓库里应至少包含：

```
docs/
├── requirements.md      ← 本 Phase 的核心交付
└── decisions/
    ├── 0001-cpu-arch.md
    ├── 0002-scheduler-algorithm.md
    └── 0003-license.md
```

> **建议使用 ADRs（Architecture Decision Records）**记录关键决策，理由 + 备选 + 取舍一目了然。

## 验证标准

- [ ] 团队对支持的硬件 / 调度策略 / 实时指标达成一致
- [ ] `requirements.md` 评审通过
- [ ] 至少 3 份 ADR 写完
- [ ] 「不做什么」列表明确

## 接下来

进入 [Phase 1 — 基础设施搭建](phase-1-infrastructure.md)。
