# ADR-0005 — 用户态/内核态边界、syscall ABI、trap 路径

> **Status**：Accepted（v0.3.1 新建）
> **Date**：2026-06-07
> **Supersedes**：无（v0.3.1 新建）
> **Deciders**：junbo.dai
> **Refs**：[requirements §4.2](../requirements.md#42-用户态-syscall用户进程跨边界调用) · [requirements §7](../requirements.md#7-内存模型v031-重写) · [ADR-0001](0001-cpu-arch.md) · [ADR-0002](0002-scheduler-algorithm.md) · [ADR-0004](0004-single-address-space.md) · [dev-log/005](../../dev-log/005-unified-hybrid-kernel-v1-subscope.md)

---

## Context

v0.3.1 引入进程隔离 + 用户态 ELF + 8 个最小 syscall（[§4.2](../requirements.md#42-用户态-syscall用户进程跨边界调用)）。**MMU 是先决条件**（per [ADR-0001](0001-cpu-arch.md) + [ADR-0004](0004-single-address-space.md)）——本 ADR 的特权级选择、trap 路径、`copy_*_user` 实现**全部假设 MMU 启用**，无"裸金属 fallback"。

需要回答：

1. **内核 / 用户在哪个特权级**？（AArch64 EL 选择、RISC-V mode 选择）
2. **syscall 怎么进、怎么出**？（指令选择、trap 路径）
3. **syscall ABI**：寄存器约定、syscall 号、返回值
4. **用户访存怎么安全传给内核**（`copy_from_user` / `copy_to_user`）
5. **内核栈是 per-thread / per-process / per-CPU**？
6. **信号 / EINTR / 异步打断**：v1.0 做不做？
7. **trap 入口模板**怎么写（要尽量短，最坏路径要可测）

---

## Decision

### 特权级选择

| 架构 | 内核 | 用户 | 固件/最高级 | 入口路径 |
|---|---|---|---|---|
| AArch64 | **EL1** | EL0 | EL3 (TF-A / Pi GPU 固件) | EL3 → EL1（boot） |
| RISC-V  | **S-mode** | U-mode | M-mode (OpenSBI) | M → S（boot） |

### syscall 指令与 ABI（Linux-compatible）

| 架构 | syscall 指令 | syscall 号寄存器 | args 寄存器 | 返回值 | 错误 |
|---|---|---|---|---|---|
| AArch64 | `svc #0` | `x8` | `x0–x5`（最多 6 个） | `x0` | 返回负 errno（如 `-EINVAL`），无 TLS errno |
| RISC-V  | `ecall` | `a7` | `a0–a5`（最多 6 个） | `a0` | 返回负 errno，无 TLS errno |

### syscall 号表（v1.0 冻结）

| # | 名称 | 原型 | 阻塞 |
|---|---|---|---|
| 0 | `sys_exit` | `void sys_exit(int code)` | — |
| 1 | `sys_getpid` | `pid_t sys_getpid(void)` | 否 |
| 2 | `sys_sched_set_rt` | `int sys_sched_set_rt(int prio)` | 否 |
| 3 | `sys_read` | `ssize_t sys_read(int fd, void *buf, size_t n)` | 可能 |
| 4 | `sys_write` | `ssize_t sys_write(int fd, const void *buf, size_t n)` | 可能 |
| 5 | `sys_open` | `int sys_open(const char *path, int flags)` | 可能 |
| 6 | `sys_close` | `int sys_close(int fd)` | 否 |
| 7 | `sys_yield` | `void sys_yield(void)` | 是 |

v1.1+ 新 syscall 必须**追加号**，不复用已分配号。

### trap 入口（最小路径）

- AArch64: `VBAR_EL1` 指向 vector table；synchronous exception from EL0 走 `el0_sync` 入口
- RISC-V: `stvec` 指向 trap handler；区分 ecall vs page fault vs interrupt 用 `scause`

**入口模板**：

1. 切到 per-thread kernel stack（`SP_EL1` 已是 / `sscratch` swap）
2. 保存 user GPR（push 全部通用寄存器到 kernel stack）
3. 区分原因（syscall / page fault / interrupt / illegal instruction）
4. syscall：查 syscall 号 → 调用 handler → 把返回值写回 saved `x0`/`a0`
5. 触发调度点检查（是否要切换）
6. 恢复 GPR、`eret` / `sret` 回 user

### 用户访存：`copy_from_user` / `copy_to_user`

```c
int copy_from_user(void *dst, const void __user *src, size_t n);
int copy_to_user(void __user *dst, const void *src, size_t n);
```

- 实现：访问前**不**预检查；触发 page fault 时由 fault handler 识别"这是 copy_*_user 路径"并返回 `-EFAULT`
- 识别方法：handler 检查 fault PC 是否落在已知 copy_* 函数区间
- 用户指针标记：`__user` 是空宏（v1.0），仅作文档；v2.0 评估静态检查工具

### 内核栈

- **per-thread 内核栈**：每个 kernel thread / user thread 都有自己的 8 KB 内核栈
- 进程切换：换 `SP_EL1` / `sp`（已隐含在调度器上下文切换里）
- syscall：进入时切到当前 thread 的 kernel stack，返回时还原 user stack
- **不**做 per-CPU 内核栈（Linux 老式做法，v1.0 不需要）

### 异步打断（v1.0 不做）

- **无信号**（per [requirements §6](../requirements.md#6-不做什么out-of-scope)）
- **阻塞 syscall 一旦阻塞，只能被以下唤醒**：等待资源就绪、超时、target 进程退出
- 不存在 `EINTR` 路径——文档化在 syscall 注脚里

---

## Consequences

### 积极

- ✅ **Linux ABI 兼容**：`x8`/`a7` 寄存器约定跟 Linux 一致，未来移植 musl libc 时 syscall stub 几乎不动
- ✅ **trap 路径短而可测**：~30 条汇编指令（AArch64）/~25 条（RISC-V），可以 trace 每条
- ✅ **per-thread 内核栈简化阻塞 syscall**：阻塞时 kernel stack 自然挂起，唤醒时 resume——不需要 continuation-passing
- ✅ **`copy_*_user` 用 fault-handler 识别**：无前置检查开销，正常路径零成本
- ✅ **跟 ADR-0004 / ADR-0002 正交**：内存模型 + 调度器都不需要知道 syscall 细节

### 消极

- ❌ **8 KB × 64 thread = 512 KB 内核栈预算**——已计入 [requirements §2 内核 RAM < 512 KB](../requirements.md#2-实时性指标)，但偏紧
- ❌ **无 errno TLS**：用户态包装层要把负返回值翻译成 `errno`（musl 习惯做法，但要写 wrapper）
- ❌ **无信号 = 无法异步 kill 阻塞进程**：v1.0 deadlock 进程要靠 watchdog 检测后整个重启
- ❌ **syscall 号冻结**：v1.1+ 加 syscall 要追加号，号段可能稀疏
- ❌ **fault handler 多了"是不是 copy_*_user 路径"分支**：增加 page fault handler 复杂度

### 中性

- 用户态/内核态切换的 ABI 是项目跟外界（musl libc / GDB / 调试器）的合同——一旦冻结很难改
- `__user` 标记是预留——v1.0 不强制，v2.0 可加 Sparse / Smatch 静态分析

---

## Alternatives Considered

### 备选 A：RISC-V 内核跑 M-mode（不用 S-mode）

- ✅ 不依赖 OpenSBI，自包含
- ❌ M-mode 没有 MMU 直接控制；放弃 MMU = 放弃 ADR-0004 进程隔离
- ❌ 不跟 RISC-V 主流生态（Linux / FreeBSD / seL4 都是 S-mode）
- **结论**：pass

### 备选 B：per-CPU 内核栈（Linux 老式）

- ✅ 节省内存（64 thread 不用 × 8 KB）
- ❌ **阻塞 syscall 必须 continuation-passing**：syscall handler 不能阻塞当前栈，要把状态存进 TCB 再让出
- ❌ 写阻塞 syscall（`sys_read`、`sys_open` 等待 vfat IO）会非常痛苦
- ❌ Linux 新版（per-thread stack）已经放弃这条
- **结论**：pass，per-thread 是值得花的内存

### 备选 C：自定义 syscall ABI（不跟 Linux）

- ✅ 可以为 v1.0 sub-scope 优化（如 syscall 号紧凑、参数压缩）
- ❌ 未来移植 musl libc / 重用 Linux 工具链全部要重写
- ❌ 维护成本：每个新 syscall 都要重新讨论 ABI
- **结论**：pass，**Linux 兼容是 1.5–2 人年 sub-scope 内最大的杠杆**

### 备选 D：v1.0 做最小信号（SIGTERM 而已）

- ✅ 能 kill 阻塞进程
- ❌ syscall 全要加 `EINTR` 路径；handler 注册 / 默认行为 / 阻塞 syscall 重启全要设计
- ❌ §6 已经明确"信号 v1.0 不做"
- **结论**：pass，watchdog 重启在 v1.0 sub-scope 是可接受的

### 备选 E：`copy_*_user` 用预检查（probe）

- ✅ 错误路径短，不依赖 page fault handler
- ❌ 每次 syscall 多 ~100 cycles 预检查开销（即使正常路径）
- ❌ 多页时检查也只能查头页（中间页 fault 仍要 handler 处理）
- **结论**：pass，fault-handler 识别是 Linux / FreeBSD 主流做法

---

## 关键设计细节（影响后续 Phase）

### AArch64 trap 入口（Phase 2 实现）

```asm
.align 11
vbar_el1_table:
    // ... 16 个 entry，每个 0x80 字节
el0_sync:
    msr     sp_el0, sp          // 保存 user sp 到 SP_EL0
    ldr     sp, =kernel_stack_top
    stp     x0, x1, [sp, #-16]!
    // push x2-x30 + sp_el0 + spsr_el1 + elr_el1
    mrs     x0, esr_el1
    bl      trap_dispatch
    // pop 反向
    eret
```

### RISC-V trap 入口（Phase 2 实现）

```asm
.align 4
trap_entry:
    csrrw   sp, sscratch, sp    // 换栈：user sp 进 sscratch
    addi    sp, sp, -32*8
    sd      x1, 1*8(sp)
    // ...（push x2–x31）
    csrr    a0, scause
    csrr    a1, stval
    csrr    a2, sepc
    call    trap_dispatch
    // pop x1–x31
    csrrw   sp, sscratch, sp
    sret
```

### syscall dispatch（C 代码）

```c
long trap_dispatch_syscall(int nr, long a0, long a1, long a2,
                           long a3, long a4, long a5) {
    if (nr < 0 || nr >= NR_SYSCALLS) return -ENOSYS;
    return syscall_table[nr](a0, a1, a2, a3, a4, a5);
}
```

### copy_from_user 实现要点

- 函数体在专用 section `.text.copy_user`
- page fault handler 检查 `fault_pc ∈ [.text.copy_user 起止]`，是 → `regs->ret = -EFAULT; regs->pc = next_pc`
- 长度循环按页处理，遇 fault 立刻停

### Phase 实现路径

- **Phase 2**：HAL 包装 trap entry + exception vector + `boot.S`
- **Phase 3**：调度器复用 trap 入口的 GPR 保存机制
- **Phase 5**：syscall dispatch + `copy_from_user` + 第一个 syscall `sys_exit`
- **Phase 6**：剩余 7 个 syscall（read/write/open/close/yield/getpid/sched_set_rt）
- **Phase 9**：trap 路径性能 + fault 路径 fuzz 测试

---

## Validation

- [ ] **Phase 2 末**：trap vector 跑通——人为触发 illegal instruction，trap handler 打印异常信息后正确 `eret`/`sret`
- [ ] **Phase 5 末**：第一个 syscall `sys_exit` 跑通——用户态 ELF 调 `svc #0` / `ecall`，内核接到、kill 进程
- [ ] **Phase 5 末**：`copy_from_user` 越界拦截——用户传一个非映射地址，syscall 返回 `-EFAULT` 而非崩溃
- [ ] **Phase 5 末**：阻塞 syscall——`sys_read` 等不到数据正确阻塞当前 thread，数据来时唤醒
- [ ] **Phase 6 末**：8 个 syscall 全部 round-trip 测试，返回值 / 错误码符合预期
- [ ] **Phase 9 末**：trap 入口开销测量：syscall round-trip < 1 μs（不含 syscall body）
- [ ] **Phase 9 末**：fault 路径 fuzz：随机用户指针调 syscall，应**永远**返回 `-EFAULT`，**永远不崩内核**

任一项不达标 → 触发 [requirements §8 90 天止损](../requirements.md#phase-1-启动-3-个月止损检查点) 评估。

---

## Notes

- 本 ADR 是 [ADR-0004 进程隔离](0004-single-address-space.md) 与 [ADR-0002 调度（含 R2）](0002-scheduler-algorithm.md) 的实现接口
- syscall ABI **冻结 = 兼容承诺**：v1.0 → v1.x 不打破，v2.x 大版本时再评估
- `__user` 标记是 Linux/Sparse 风格的预留——v1.0 仅文档作用，v2.0 评估静态检查
- v0.2 ADR-0004 单地址空间下用户态根本不存在 → 本 ADR 是 v0.3.1 翻转的**必要补集**
- C906 RISC-V 实测 trap entry 路径预期 ~150 cycles；Cortex-A72 ~80 cycles；若实测显著偏高，触发本 ADR 修订
