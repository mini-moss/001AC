# Dev Log · 007 — Phase 1.2 完成：UART 可移植 + Make 构建 + Pi 4 QEMU hello world

> **日期**：2026-06-09
> **范围**：boot stub 调通 → `make qemu` 输出 "hello"
> **触发**：dev-log/006 第一周工作清单第 5 天（QEMU hello world）
> **对应交付物**：可工作的 boot stub + UART 抽象 + Makefile

---

## 一句话

`kernel/main.c` 里的 UART 硬编码（`0xFE201000`、`struct pl011`）被拆成 board 级配置 + 两个 UART 驱动，加上 Makefile 构建系统，`make qemu` 一键编译运行 Pi 4 QEMU 输出 "hello"。

---

## 做了什么

### 1. UART 可移植性——board.h + 双驱动

**问题**：不同硬件的 UART 基地址不同，UART 控制器类型也可能不同（PL011 vs NS16550）。

**方案**：

```text
boards/<board>/board.h    → 每块板的硬件参数（UART 基地址、控制器类型）
kernel/uart.h             → 统一接口：uart_init() / uart_putc() / uart_puts()
kernel/uart-pl011.c       → PL011 驱动（Pi 4 / Pi 5）
kernel/uart-ns16550.c     → NS16550 驱动（RISC-V virt / SG2002）
```

**当前支持的 board → UART 映射**：

| Board | UART 基地址 | 控制器 | 驱动文件 |
|---|---|---|---|
| `pi4` | `0xFE20_1000` | PL011 | `uart-pl011.c` |
| `pi5` | `0x107D_0010_0000` | PL011 | `uart-pl011.c` |
| `virt-riscv` | `0x1000_0000` | NS16550 | `uart-ns16550.c` |
| `sg2002` | 待定 | TBD | (暂指 `uart-ns16550.c`) |

每个 UART 驱动文件有默认 board（PL011 默认 Pi 4，NS16550 默认 virt-riscv），所以 `make` 不需要手动传 `-DBOARD_XXX`。切换到 Pi 5 时需 `-DBOARD_PI5`。

**Phase 1 故意不做的事**：
- 不做运行时 UART 类型检测
- 不做完整的 HAL 层（Phase 2 的事）
- 不做 SG2002 真机配置（无 QEMU 模型）

### 2. Makefile 构建系统

按 dev-log/006 决定 3 设计，但 Phase 1 文件太少（5 个源文件），不做子系统 `Makefile.partial` 拆分，一个根 Makefile 解决。

```bash
make                              # 编译（默认 ARCH=aarch64 BOARD=pi4）
make ARCH=riscv64 BOARD=virt-riscv  # 切架构/板子
make qemu                         # 编译 + QEMU 运行
make qemu-debug                   # 编译 + QEMU GDB stub (:1234)
make clean                        # 删除 build/
make help                         # 帮助
```

**关键设计决策**：

- **工具链自动检测**：依次尝试 `aarch64-elf-` → `aarch64-none-elf-` → `aarch64-linux-gnu-`，取第一个在 PATH 里能找到的。（RISC-V 同理）
- **延迟检查**：`make help` / `make clean` 不需要交叉编译器也能运行；只有实际编译时才检查工具链是否存在
- **Board → UART 驱动映射**：在 Makefile 里根据 `BOARD` 变量选择编入哪个 `uart-*.c`

### 3. boot.S 修复——3 个 bug

boot.S 最初在 QEMU 下内核不启动。修了 3 个问题：

| Bug | 现象 | 修复 |
|---|---|---|
| 标签名错误 `beq drop_to_el1` | 链接失败：undefined reference to `drop_to_el1` | 改为 `beq setup_el2`（实际存在的标签） |
| 多核同时执行 | QEMU `raspi4b` 有 4 核（Cortex-A72 ×4），非主核在不同 EL 触发未定义指令异常 → 无限重启 | `_start` 入口读 `MPIDR_EL1.Aff0`，非 CPU 0 全部 `wfe` 休眠 |
| EL2 `msr hcr_el2` 触发异常 | QEMU 下 EL3→EL2→EL1 逐级路径在某些条件下不稳定 | 改为 EL3→EL1 直达（Phase 1 不需要虚拟化，EL2 没用） |

**最终启动流程**：

```
CPU 0: _start → MPIDR check (pass) → EL3 → EL1 直达 → 栈 + BSS → kernel_main()
CPU 1-3: _start → MPIDR check (fail) → wfe park
```

---

## 文件变更

```
新增:
  Makefile                        # 构建系统
  boards/pi4/board.h              # Pi 4 板级配置
  boards/pi5/board.h              # Pi 5 板级配置（占位）
  boards/virt-riscv/board.h      # RISC-V QEMU virt 板级配置
  kernel/uart.h                   # UART 抽象接口
  kernel/uart-pl011.c             # PL011 驱动
  kernel/uart-ns16550.c           # NS16550 驱动

修改:
  kernel/main.c                   # 移除硬件寄存器 → 仅 28 行
  arch/aarch64/boot.S             # 修标签 + SMP park + EL3→EL1
```

---

## 当前状态

```bash
$ make qemu
  AS      arch/aarch64/boot.S
  CC      kernel/main.c
  CC      kernel/uart-pl011.c
  BUILD   build/vmlinux.elf (aarch64 / pi4)
hello     # ← QEMU 终端输出
```

**接下来**（按 dev-log/006 第一周清单）：

- [ ] Day 6: `.github/workflows/build.yml` CI
- [ ] Day 7: RISC-V toolchain 安装（`tools/install-toolchain.sh` RV64 部分）
- [ ] Phase 1.3: 异常向量表 + timer interrupt + `printk`

---

## 学到的教训

1. **QEMU `-machine raspi4b` 是多核的**——4 个 Cortex-A72 同时从 `0x80000` 执行，Phase 1 不做 SMP 就要 park 非主核
2. **EL3→EL2→EL1 逐级降级在 QEMU 不一定稳**——直达路径更简单也够了
3. **编译时 `-DBOARD_XXX` 会造成 IDE clangd 报假阳性**——因为没有 `compile_commands.json`，clangd 看不到宏定义。解决：UART 驱动给一个默认 board 而不依赖 `#error`
4. **NS16550 和 PL011 的寄存器布局完全不同**——不只是地址问题；驱动层必须抽象
