# Phase 2 — 硬件抽象层 HAL

> **预估周期**：1 周
> **进入门槛**：Phase 1 完成
> **退出标志**：QEMU 上能通过真实 UART 看到 `printk` 输出

HAL 是多平台支持的基石。**这一层做好了，Phase 3–9 全部架构无关**。

## 2.1 HAL 分层

```
┌────────────────────────────────────┐
│   应用 / 驱动（架构无关）            │
├────────────────────────────────────┤
│   arch/<cpu>/include/hal.h         │  ← 公共 API
│   arch/<cpu>/src/                  │
├────────────────────────────────────┤
│   boards/<board>/bsp.c             │  ← 板级 BSP
│   boards/<board>/link.ld           │  ← 链接脚本
│   boards/<board>/startup.s         │  ← 启动文件
└────────────────────────────────────┘
```

## 2.2 CPU 抽象 API

`arch/<cpu>/include/hal.h` 至少定义：

```c
/* 中断控制 */
void cpu_irq_disable(void);
void cpu_irq_enable(void);
unsigned cpu_irq_save(void);     // 返回先前状态
void cpu_irq_restore(unsigned state);

/* 原子操作 */
void cpu_atomic_add(volatile int *ptr, int val);
int  cpu_atomic_xchg(volatile int *ptr, int new_val);
bool cpu_atomic_cas(volatile int *ptr, int *expected, int desired);

/* 内存屏障 */
void cpu_dmb(void);   // Data Memory Barrier
void cpu_dsb(void);   // Data Sync Barrier
void cpu_isb(void);   // Instruction Sync Barrier

/* 上下文切换 */
typedef struct cpu_context cpu_context_t;
void  cpu_context_init(cpu_context_t *ctx, void (*entry)(void *),
                       void *arg, void *stack_top);
void  cpu_context_switch(cpu_context_t **old_sp, cpu_context_t *new_sp);
void  cpu_trigger_pendsv(void);

/* 时间 */
void  cpu_systick_init(uint32_t hz);
tick_t cpu_systick_get(void);
void  cpu_delay_us(uint32_t us);

/* 启动 */
void  cpu_start_main(void (*main_fn)(void));
```

### Cortex-M 实现要点
- `cpu_irq_*`：用 `cpsid i` / `cpsie i`
- `cpu_atomic_*`：用 `ldrex` / `strex` 指令
- 上下文切换用 **PendSV**：在 PendSV 异常处理中硬件自动压栈（xPSR, PC, LR, R12, R3-R0），手动保存 R4-R11
- `cpu_systick_init`：配置 SysTick 寄存器，优先级设最低

### RISC-V 实现要点
- `cpu_irq_*`：用 `mstatus.MIE` 位操作
- `cpu_atomic_*`：用 `amoadd.w` / `lr.w` / `sc.w`
- 上下文切换用 **软件中断**（mcause = 3）
- 时钟：`mtime` + `mtimecmp`（SiFive 实现）
- 没有硬件压栈，所有寄存器手动保存

## 2.3 启动文件

`arch/<cpu>/src/startup_<cpu>.S` 负责：

1. 设置初始栈指针（`ldr sp, =__stack_top`）
2. 把 `.data` 从 Flash 复制到 RAM
3. 把 `.bss` 清零
4. 跳转到 `main()`

```asm
// ARM Cortex-M 启动伪代码
.section .isr_vector
.word __stack_top              // 初始 SP
.word reset_handler            // Reset
.word irq_nmi_handler          // NMI
// ... 其他中断向量

.section .text
reset_handler:
    ldr sp, =__stack_top

    // 复制 .data
    ldr r0, =__sdata
    ldr r1, =__edata
    ldr r2, =__sdata_load
copy_loop:
    cmp r0, r1
    bge copy_done
    ldr r3, [r2], #4
    str r3, [r0], #4
    b copy_loop
copy_done:

    // 清零 .bss
    ldr r0, =__sbss
    ldr r1, =__ebss
zero_loop:
    cmp r0, r1
    bge zero_done
    mov r2, #0
    str r2, [r0], #4
    b zero_loop
zero_done:

    bl main
    b .   // 不应到达
```

## 2.4 链接脚本

`boards/<board>/link.ld` 至少定义：

```ld
MEMORY {
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1M
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

SECTIONS {
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .text       : { *(.text*) *(.rodata*) } > FLASH
    .data       : AT(LOADADDR(.text) + SIZEOF(.text))
                  { __sdata = .; *(.data*) __edata = .; } > RAM
    .bss        : { __sbss = .; *(.bss*) *(COMMON) __ebss = .; } > RAM
    ._user_heap : { __heap_start = .; } > RAM

    __stack_top = ORIGIN(RAM) + LENGTH(RAM);  // 栈顶 = RAM 末尾
}
```

> **关键**：把 `__stack_top` 放在 RAM 末尾是一个常见约定，但更稳健的做法是用专用 `STACKSIZE` section 显式控制。

## 2.5 板级 BSP

`boards/<board>/bsp.c` 暴露板级初始化：

```c
// 由链接脚本强符号提供默认实现
void __weak board_early_init(void) { /* 默认空 */ }
void __weak board_init(void)       { /* 默认空 */ }

void board_init(void) {
    board_early_init();   // 时钟、PLL
    uart_init(CONFIG_DEBUG_UART, 115200);
    board_app_init();     // 用户覆写
}

void uart_console_putc(char c) {
    while (!(UART_SR(CONFIG_DEBUG_UART) & UART_SR_TXE)) { }
    UART_DR(CONFIG_DEBUG_UART) = c;
}
```

### 通用 UART API（位操作风格）
为支持多家 MCU，先抽象一个最小通用接口：

```c
struct uart_ops {
    int  (*init)(const struct device *dev, uint32_t baud);
    int  (*putc)(const struct device *dev, char c);
    int  (*getc)(const struct device *dev, char *c);
    int  (*write)(const struct device *dev, const void *buf, size_t len);
    int  (*read)(const struct device *dev, void *buf, size_t len);
};
```

每个 board 提供自己的 `uart_ops` 实例。

## 2.6 替换 Phase 1 的临时 printk

现在把 Phase 1 的 semihosting 替换成真实 UART：

```c
// kernel/printk.c
#include <kernel/printk.h>
#include <bsp/board.h>
#include <stdarg.h>
#include <stdint.h>

static char digits[] = "0123456789abcdef";

static void printk_putc(char c) {
    if (c == '\n') uart_console_putc('\r');
    uart_console_putc(c);
}

void printk(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { printk_putc(*p); continue; }
        p++;
        switch (*p) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            while (*s) printk_putc(*s++);
            break;
        }
        case 'd': {
            int v = va_arg(ap, int);
            if (v < 0) { printk_putc('-'); v = -v; }
            char buf[12]; int i = 0;
            do { buf[i++] = '0' + (v % 10); v /= 10; } while (v);
            while (i--) printk_putc(buf[i]);
            break;
        }
        case 'x': {
            unsigned v = va_arg(ap, unsigned);
            for (int i = 7; i >= 0; i--)
                printk_putc(digits[(v >> (i*4)) & 0xf]);
            break;
        }
        case 'c':
            printk_putc((char)va_arg(ap, int));
            break;
        case '%':
            printk_putc('%');
            break;
        }
    }

    va_end(ap);
}
```

## 2.7 QEMU 验证脚本

`tools/qemu/run.sh`：

```bash
#!/bin/bash
set -e
BOARD=${1:-stm32f407-disco}
ELF=build/${BOARD}/rtos.elf

case $BOARD in
    stm32f407-disco)
        qemu-system-arm -machine mps2-an385 -nographic \
            -kernel $ELF -semihosting-config enable=on,target=native
        ;;
    esp32-c3-devkit)
        qemu-system-riscv32 -machine sifive_e -nographic \
            -bios none -kernel $ELF -semihosting
        ;;
esac
```

## 验证标准

- [ ] `main.c` 中 `printk("Hello %s on %s\n", "Robot RTOS", CONFIG_BOARD)` 在 QEMU 控制台可见
- [ ] `printk` 支持 `%s %d %x %c %%`
- [ ] 链接脚本正确分配 `.data` / `.bss` / 栈（用 `arm-none-eabi-nm` 检查符号）
- [ ] `arm-none-eabi-size rtos.elf` 报告 text/data/bss 三段都合理
- [ ] 同样的 `main.c` 在 ARM 和 RISC-V 两个 QEMU 模型上都跑通

## 常见坑

- ⚠️ Cortex-M 的 `__stack_top` 必须是 8 字节对齐（硬件浮点单元要求）
- ⚠️ RISC-V 链接时指定 `-march=rv32imac -mabi=ilp32`，否则 `mstatus` 设置失败
- ⚠️ QEMU 的 MPS2 默认时钟是 25MHz，USART 分频计算要按这个值
- ⚠️ 在 RISC-V 上中断栈（如果有）和任务栈要分开，否则嵌套中断会破坏栈

## 接下来

进入 [Phase 3 — 核心调度器](phase-3-scheduler.md)，让多任务真正跑起来。
