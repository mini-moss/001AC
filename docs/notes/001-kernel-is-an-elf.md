# 笔记 001 — 操作系统内核就是一个可执行文件

> **日期**：2026-06-08
> **关联**：Phase 1 写 link.ld / boot.S / main.c 时的概念梳理

---

## 内核是一个特殊的 ELF

操作系统内核本质上就是一个 **ELF 可执行文件**。

| | 普通程序（如 `ls`） | 操作系统内核 |
|---|---|---|
| 谁加载它 | 操作系统（`execve`） | 固件 / 引导器（UEFI / GPU 固件 / QEMU） |
| 依赖什么 | libc、动态链接器 | 没有任何依赖（`-nostdlib`） |
| 怎么输出 | `printf` → `write` syscall | 直接写硬件寄存器（MMIO） |
| 怎么结束 | `return 0` / `exit()` | 死循环 `while (1);`，永不返回 |
| 运行权限 | 用户态（EL0） | 内核态（EL1），可访问所有硬件 |
| 文件格式 | ELF | ELF |

**打个比方**：
- 普通程序像大楼里的租户，通过前台（系统调用）办事
- 操作系统内核就是大楼本身——直接控制地基、水管、电线（硬件），没有"下面一层"可以依赖

所以 `vmlinux.elf` 和 `/bin/ls` 是同一种文件格式。区别只在于：
1. **加载方式不同**——普通程序由 OS 的 `execve` 加载；内核由固件/QEMU 加载
2. **运行特权级不同**——普通程序在 EL0（用户态）；内核在 EL1（内核态）

QEMU 的 `-kernel` 参数就是模拟树莓派固件：把 ELF 加载到 `0x80000`，然后跳到 `_start`。

---

## link.ld 导出的符号：`__bss_start` / `__bss_end` / `__stack_top`

这些是**链接器导出的符号**，不是 C 变量。在 boot.S 中通过 `ldr x0, =__bss_start` 获取其**地址值**。

| 符号 | 用途 |
|---|---|
| `ENTRY(_start)` | ELF 头部记录入口地址，QEMU 读取后直接跳过来 |
| `KEEP(*(.text._start))` | 强制保留 `_start`，不会被 `--gc-sections` 丢弃 |
| `__bss_start` | BSS 段起始，boot.S 从这里开始清零 |
| `__bss_end` | BSS 段结束，boot.S 清零到这里停止 |
| `__stack_top` | 初始栈顶（SP 从这里往下长，栈向低地址增长） |
| 栈放 BSS 末尾 | 不占额外内存，自然在所有数据段之后 |

> ⚠️ 在 C 代码中引用这些符号时，要取地址 `&__bss_start`，而不是直接读值——因为"符号的值"就是地址本身。

---

## 为什么从 `0x80000` 开始？

Raspberry Pi 的 GPU 固件把内核加载到物理地址 `0x80000`：

1. GPU 先启动 → 执行 `bootcode.bin` / `start4.elf`
2. GPU 固件读取 SD 卡上的 `config.txt` 和 `kernel8.img`
3. 固件把内核加载到 `0x80000`
4. 释放 ARM 核心的复位，CPU 从 `0x80000` 开始执行

QEMU 的 `-kernel` 模拟了这个流程（跳过了 GPU 固件部分）。

---

## 内核启动全流程

```
硬件上电
  → GPU 固件（bootcode.bin / start4.elf）
    → 加载 kernel8.img 到 0x80000
      → 释放 CPU 复位
        → _start (boot.S)
          → EL3 → EL2 → EL1 降级
            → 设置栈指针 (SP = __stack_top)
              → 清零 BSS
                → 跳转 kernel_main() (C 代码)
                  → 初始化硬件
                    → ...
                      → 启动调度器
                        → 第一个用户进程
```

每个阶段都建立在上一阶段之上。
