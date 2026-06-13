# Dev Log · 008 — Phase 1.3：异常向量 + printk + 定时器中断 + CI

> **日期**：2026-06-14

## 概述

Phase 1.3 在 Phase 1.2 的 "hello world" 基础上引入内核诊断基础设施：
AArch64 异常向量表、格式化打印（printk）、generic timer 中断驱动 tick，
以及 CI 构建链路和工具链安装脚本。

## 新增文件

### `arch/aarch64/vector.S` — EL1 异常向量表

- ARMv8 16-entry 向量表，2 KiB 对齐，每项 0x80 字节
- 实现两个路径：
  - **+0x200 Synchronous**：保存 x0-x3/LR，读取 ESR_EL1/FAR_EL1/ELR_EL1，调用 C handler 后 parking
  - **+0x280 IRQ**：保存完整 trap frame（x0-x18, x29 FP, x30 LR, ELR_EL1, SPSR_EL1），调用 C handler，恢复后 `eret`
- 其余 14 个 entry 为 `b .` spin（Phase 3+ 补充 lower-EL 入口）

### `arch/aarch64/trap.h` / `trap.c` — C 层异常处理

- `trap_frame_t` 结构体与 vector.S 栈布局一一对应
- `trap_init()`：安装向量表到 VBAR_EL1，unmask IRQ（DAIFClr.I）
- `sync_trap_handler()`：解码 ESR_EL1 Exception Class（Data Abort / Instruction Abort / alignment fault 等），打印诊断后 park CPU
- `irq_trap_handler()`：检查 CNTP_CTL_EL0.ISTATUS，分发到 timer handler；每 100 ticks（1 秒）打印一次

### `kernel/printk.h` / `printk.c` — 内核格式化打印

- 最小 printf 实现，支持 `%s` `%d` `%u` `%x` `%c` `%%`
- 字符级输出到 `uart_putc()`，`\n` → `\r\n` 转换
- 零堆分配，无浮点，小栈 footprint（21 字节数字缓冲区）
- 使用 `__builtin_va_list`（GCC/Clang 内建，无需 `<stdarg.h>`）

### `.github/workflows/build.yml` — CI 构建 + QEMU smoke test

- Matrix 构建：`aarch64/pi4`（必须通过）+ `riscv64/virt-riscv`（允许失败直到 Phase 2）
- 步骤：checkout → 安装工具链 → make → QEMU smoke test（验证 "hello" 输出）
- 使用 `tools/install-toolchain.sh` 安装工具链

### `tools/install-toolchain.sh` — 交叉编译器安装脚本

- 支持 `aarch64` / `riscv64` 两个架构
- 下载固定版本 tarball（AArch64: ARM GNU Toolchain 13.3.rel1）
- 校验 SHA256（Linux/macOS），解压到 `~/.local/cross/<arch>/`
- `--check-only` 模式用于 CI 快速验证

## 修改文件

### `kernel/main.c`

- 重写：从简单 `uart_puts("hello")` 升级为完整内核初始化流程：
  1. `uart_init()` → printk 输出 banner
  2. `trap_init()` → 安装向量表、unmask IRQ
  3. `timer_setup()` → 配置 CNTP 100 Hz tick
  4. `while (1) { wfi(); }` — IRQ 驱动 idle loop
- 注释全部英文化

### `Makefile`

- `C_SRCS` 拆分为 `KERNEL_C_SRCS` + `ARCH_C_SRCS` + `BOARD_C_SRCS`，三层组合
- 新增 `qemu-test` target：非交互 smoke test（后台 QEMU + grep "hello"），用于 CI
- 新增 `.DEFAULT_GOAL := all`

## 设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 格式化打印 | 自研 `printk`（不用 libc `printf`） | 零依赖，内核可控；`__builtin_va_list` 跨 GCC/Clang |
| IRQ 处理语言 | 汇编保存/恢复 + C 逻辑 | 汇编处理寄存器不可序列化部分；C 写 dispatch 和 timer 逻辑 |
| 定时器频率 | 100 Hz（10 ms tick） | 足够调试，不压垮 UART 输出；后续可配置 |
| 当前 IRQ dispatch | 仅读 CNTP_CTL_EL0.ISTATUS | GIC 驱动留到 Phase 4；当前 timer 是唯一 IRQ 源 |
| CI matrix | aarch64 必须通过，riscv64 允许失败 | RV64 trap 未实现（Phase 2），但 Makefile 骨架已就绪 |

## 验证方式

```bash
# 本地 QEMU
make ARCH=aarch64 BOARD=pi4 qemu
# 预期输出：hello + tick 每秒打印一次

# CI smoke test
make ARCH=aarch64 BOARD=pi4 qemu-test
# 预期：PASS

# 工具链检查
./tools/install-toolchain.sh --check-only aarch64
```
