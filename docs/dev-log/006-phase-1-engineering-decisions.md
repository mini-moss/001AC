# Dev Log · 006 — Phase 1 启动工程决定（toolchain / build / repo / CI / ISA 优先级）

> **日期**：2026-06-07
> **范围**：Phase 1 启动前的 5 个工程决定（不动 6 ADR + requirements v0.3.1）
> **触发**：Phase 0 完成（commit f7012fb），进 Phase 1 前需要敲定工程基础设施
> **对应交付物**：Phase 1.1 工具链脚本 + Phase 1.2 boot stub + 仓库骨架 commit

---

## 一句话

Phase 1 第一周做：装工具链（AArch64 优先）+ 仓库骨架 + Make 系统 + GitHub Actions skeleton + Pi 4 QEMU hello world boot stub。

---

## 5 个决定

### 1. 工具链：ARM 官方 + 上游 RISC-V，pin 版本

- **AArch64**：ARM GNU Toolchain (`aarch64-none-elf-gcc` from <https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads>)
- **RISC-V**：上游 `riscv64-unknown-elf-gcc`（SiFive 预编译或上游 source 构建）
- **安装方式**：`tools/install-toolchain.sh` 脚本，pin 具体版本号 + sha256 校验
- **存放**：`$HOME/.local/cross/<arch>/` 或可配的 `CROSS_PATH`
- **CI 复用**：CI 跑同一脚本，保证本地 / CI 工具链版本一致

**为什么不用 apt / Nix / Docker**：apt 版本飘移；Nix 对单人团队过重；Docker 增加 CI 复杂度。脚本是中间路线，团队规模扩大后可升 Nix。

### 2. ISA 优先级：Pi 4 (AArch64) 先跑通

- Phase 1–2 主线 = Pi 4 (AArch64) on QEMU `raspi4b`
- RV64 (SG2002 LicheeRV Nano via QEMU `virt`) **在 HAL 层一开始就预留**，但不实测
- 实测 RV64 = Phase 2 末（HAL 抽象稳定后）或 Phase 3 初（调度器稳定后）
- **触发"双 ISA 并行"的条件**：Pi 4 hello world 跑通 + HAL 接口稳定 → 立即开始 RV64 移植
- 整体目标 = [requirements §8](../requirements/requirements.md#phase-1-启动-3-个月止损检查点) 的 90 天止损硬门槛（两个 ISA QEMU hello 都通）

**这跟 [dev-log/005](005-unified-hybrid-kernel-v1-subscope.md) 的差别**：005 默认"双 ISA 并行"，本日志把它**调整为"阶段式启动"**——架构上仍是双 ISA（HAL from day 1 严格分），但**实测节奏**让 AArch64 先走通。

风险：ISA-specific 设计假设可能潜入代码，等 RV64 移植才发现。**缓解**：
- HAL 层 from day 1 严格隔离（`arch/aarch64/` 单独编译，禁止其他模块 `#include arch/aarch64/...`）
- HAL 接口设计 review 时必问："两个 ISA 都用得上吗？"
- `arch/riscv64/` 目录早建好但内容空，避免心智上"忘了 RV64"

### 3. Build 系统：Make（root + per-subsystem include）

- 根 `Makefile` 暴露 targets：`all`、`clean`、`qemu`、`qemu-debug`、`test`、`check`
- 每个子系统有 `<subsys>/Makefile.partial`（包含进根 Makefile）
- 跨编译变量：`CROSS_COMPILE`（如 `aarch64-none-elf-`）+ `ARCH`（`aarch64` / `riscv64`）+ `BOARD`（`pi4` / `sg2002`）
- linker script：`arch/<arch>/boards/<board>/link.ld`（per-board，避免内存布局假设乱跑）

**为什么不用 CMake / Meson**：bare-metal 社区默认 Make + 直接 linker 控制；CMake 的 Zephyr 风格强大但需要 Kconfig 一起上，超 v1.0 scope。**如果 Phase 5+ Makefile 突破 1000 行难维护，再评估 CMake**——现在不预判。

### 4. 仓库骨架：Linux 风格

```text
.
├── kernel/         # 调度器、syscall dispatch、VFS、IPC
├── arch/
│   ├── aarch64/    # AArch64 boot / trap / MMU
│   └── riscv64/    # RV64 boot / trap / MMU（v1.0 第一周仅占位）
├── boards/
│   ├── pi4/        # Raspberry Pi 4
│   ├── pi5/        # Raspberry Pi 5（v1.x 兼容）
│   └── sg2002/     # SG2002 (LicheeRV Nano)
├── drivers/        # uart / block / gpio / pwm / ...
├── lib/            # 链表、bitmap、printk、string
├── fs/             # vfat 后端、chardev framework
├── mm/             # page allocator、vm subsystem、ASID 池
├── user/           # 用户态 demo + libc stub
├── tests/          # 单元 + 集成测试
├── tools/          # build scripts、install-toolchain.sh、qemu wrapper
├── third_party/    # FatFs IoT、必要时 OpenSBI binary
└── docs/           # 已存在
```

**子系统编译模型**：每个顶层目录是独立编译单元，有自己的 `Makefile.partial`。kernel/ 链接所有 .o 成 `vmlinux.elf`（命名跟 Linux 一致以便用同样的调试工具）。

### 5. CI：GitHub Actions（双 ISA build + QEMU hello smoke）

- `.github/workflows/build.yml`：
  - matrix：`[aarch64+pi4, riscv64+sg2002]`（RV64 job 标 `continue-on-error` 直到 Phase 2 末）
  - 步骤：`tools/install-toolchain.sh` → `make all` → `make qemu-test`
- `.github/workflows/lint.yml`：
  - markdown lint（解决 MD060 / MD034 / MD028 配置）
  - C lint（暂用 `clang-format --dry-run`；clang-tidy 留 Phase 3+）
- **PR 硬门槛**：build 必须通过（RV64 在 Pi 4 跑通前可豁免）
- **CI minutes 配额监控**：GitHub Actions free tier 每月 2000 分钟，双 ISA × 多 board × QEMU smoke 可能撞墙。监控；超出考虑 self-hosted。

---

## Phase 1 第一周工作清单（草案）

| 天 | 工作 | 交付物 |
|---|---|---|
| 1 | `tools/install-toolchain.sh` AArch64 部分 + verify gcc 跑得通 | toolchain 可装 |
| 2 | 仓库骨架（11 个顶层目录） + 空的 `Makefile.partial` | `find` 看得到完整结构 |
| 3 | 根 `Makefile` + `arch/aarch64/Makefile.partial` + `boards/pi4/Makefile.partial` | `make help` 跑得通 |
| 4 | `arch/aarch64/boot.S` 最小骨架（设 EL1 + 栈 + 跳 C `main()`） | 编译出 `vmlinux.elf` |
| 5 | `boards/pi4/link.ld` + `kernel/main.c` 打印 "hello"（PL011 UART MMIO 写 `'h'..'o'`） | `make qemu` 看到 "hello" |
| 6 | `.github/workflows/build.yml` 跑通同样的步骤 | 推 PR 见到绿勾 |
| 7 | `tools/install-toolchain.sh` RV64 部分（仅装 + verify gcc，不实测 boot） | RV64 toolchain 装好 |

第一周末 = **Phase 1.1（工具链）完成 + Phase 1.2（boot stub）启动**。Phase 1 整体目标（boot + tick + printk + 多任务雏形）大约 4–6 周。

---

## 风险（提前列）

1. **Pi 4 真机启动跟 QEMU `raspi4b` 仿真有偏差**——树莓派 GPU 固件 + config.txt + 设备树加载逻辑不同；本日志决定 **Phase 1–2 只 QEMU，Phase 2 末才上真机**
2. **RV64 决定 2 期间被 deprioritize**——`arch/riscv64/` 目录早建好但内容空，避免心智上"忘了 RV64"。HAL 接口设计要 review "两个 ISA 都用得上吗？"
3. **Make 的可读性 vs 灵活性张力**——预期到 Phase 5+ Makefile 行数会到 500+。若突破 1000 行难维护，再评估 CMake
4. **CI minutes 配额** —— 见决定 5
5. **决定 2 让 dev-log/005 的"双 ISA 并行"变成"阶段式"**——文档化为本日志的有意调整，不算违反 005

---

## 接下来

1. 本 dev-log/006 + 仓库骨架空目录（11 个顶层目录 + .gitkeep）一起 commit
2. Phase 1 第一周按上面清单推进
3. 第一周末做一次 retrospective dev-log/007（或更晚），调整节奏

完成本日志 + 骨架 commit 后 = **正式开始写代码**。
