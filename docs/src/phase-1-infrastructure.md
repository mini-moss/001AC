# Phase 1 — 基础设施搭建

> **预估周期**：1 周
> **进入门槛**：Phase 0 完成
> **退出标志**：所有架构可在 QEMU 跑通 `make qemu-hello`

本 Phase 唯一目标：**让所有工具链在所有目标上跑得通**。这一周写得越整齐，后面九周越省力。

## 1.1 仓库骨架

```
robot-rtos/
├── arch/                  # 架构相关代码
│   ├── arm/cortex-m/
│   └── riscv/
├── boards/                # 板级支持包
│   ├── stm32f407-disco/
│   └── esp32-c3-devkit/
├── kernel/                # 内核（架构无关）
│   ├── sched/
│   ├── sync/
│   ├── mem/
│   └── time/
├── drivers/               # 驱动框架 + 具体驱动
│   ├── bus/               # GPIO / UART / SPI / I2C / CAN
│   └── robot/             # PWM / Encoder / IMU
├── include/               # 公共头文件
├── lib/                   # 可选组件
│   ├── fs/
│   ├── net/
│   └── gui/
├── third_party/           # 第三方（micro-ROS、lwIP、LVGL、FatFS）
├── tests/
│   ├── unit/
│   └── qemu/
├── tools/                 # 构建 / 分析 / 烧录脚本
├── docs/
├── Makefile
├── CMakeLists.txt         # 顶层 CMake（推荐）
├── .gitignore
├── .editorconfig
├── .clang-format
├── README.md
└── LICENSE
```

## 1.2 工具链

### 编译器
- **ARM**：`arm-none-eabi-gcc`（[GNU Arm Embedded Toolchain](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain)）
- **RISC-V**：`riscv32-unknown-elf-gcc` 或 `riscv64-unknown-elf-gcc`（SiFive 或 xPack）

### 仿真
- QEMU ≥ 7.0
  - `qemu-system-arm`：MPS2 / mps3-an547 等板级模型
  - `qemu-system-riscv32`：sifive_e / sifive_u / virt

### 烧录 / 调试
- OpenOCD + GDB（多板通用）
- 或厂商工具：STM32CubeProgrammer、esptool.py

### 构建
- **推荐 CMake**：跨平台、多 IDE 集成
- 或纯 Make（更轻量但要自己写规则）

### 静态分析
- `cppcheck` —— 基础静态分析
- `clang-tidy` —— 风格 + MISRA-C 子集
- `include-what-you-use` —— 头文件包含审计

## 1.3 编码规范

### C 标准
- **C11**（启用 `_Static_assert`、`_Generic`、`<stdalign.h>`）
- 禁止 C++ 扩展
- 不用 GNU C 扩展的「可移植增强」部分

### 命名约定
| 元素 | 风格 | 示例 |
|---|---|---|
| 函数 | `module_action()` | `task_create()`、`sem_take()` |
| 类型 | `module_name_t` | `task_t`、`sem_t` |
| 宏 / 常量 | `UPPER_SNAKE` | `MAX_TASKS`、`TICK_RATE_HZ` |
| 静态变量 | `s_module_name` | `s_ready_queue` |
| 全局变量 | `g_module_name` | `g_tick_count` |

### 头文件
- 每个头文件加保护：`ROBOT_RTOS_<MODULE>_H`
- 不要在头文件中 `typedef struct { ... } x;` —— 总是用不透明指针
- 函数声明必须加参数名（方便 doxygen）

### 注释
- 公开 API 用 doxygen 风格：

```c
/**
 * @brief Create a new task.
 *
 * @param name     Human-readable name (for debug)
 * @param entry    Task entry function
 * @param arg      Argument passed to entry
 * @param stack_size  Stack size in bytes
 * @param priority    Priority (0 = lowest, MAX_PRIO = highest)
 *
 * @return Pointer to TCB on success, NULL on failure
 *
 * @note Not ISR-safe. Must be called before scheduler starts.
 */
task_t *task_create(const char *name, void (*entry)(void *), void *arg,
                    size_t stack_size, int priority);
```

## 1.4 Git 规范

- 主分支 `main` 永远可编译
- 功能分支：`feature/phase-3-scheduler` 形式
- Commit message：参考 Conventional Commits（`feat(sched): add bitmap scheduler`）
- `.gitignore` 至少排除：`build/`、`*.o`、`*.elf`、`*.map`、`book/`、IDE 文件

## 1.5 构建脚本骨架

`CMakeLists.txt` 顶层示例：

```cmake
cmake_minimum_required(VERSION 3.20)
project(robot_rtos C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 工具链配置
if(BOARD STREQUAL "stm32f407-disco")
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/tools/cmake/arm-none-eabi.cmake)
elseif(BOARD STREQUAL "esp32-c3-devkit")
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/tools/cmake/riscv.cmake)
endif()

add_subdirectory(arch)
add_subdirectory(kernel)
add_subdirectory(boards/${BOARD})
add_subdirectory(drivers)
```

每个 board 提供 `tools/cmake/<arch>.cmake`，指定 `-mcpu=`、`-mfloat-abi=`、链接脚本。

## 1.6 第一个里程碑：QEMU 上跑通

写一个最小 `main()`：

```c
#include <kernel/printk.h>

int main(void) {
    printk("Hello from Robot RTOS on %s\n", CONFIG_BOARD);
    while (1) { /* spin */ }
    return 0;
}
```

`printk` 在 Phase 2 才真正实现。Phase 1 可以临时走 **semihosting**（ARM）或 **HTIF**（RISC-V）输出：

```c
// 临时的半主机实现，仅用于 Phase 1 验证
void printk(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    // 调用 semihosting 或 HTIF write
    // ...
    va_end(ap);
}
```

## 验证标准

- [ ] 仓库目录结构与本文 1.1 一致
- [ ] `arm-none-eabi-gcc --version` 和 `riscv32-unknown-elf-gcc --version` 都有输出
- [ ] QEMU 跑 `hello.elf`，能通过 semihosting 看到 "Hello from Robot RTOS on ..."
- [ ] `make` / `cmake --build` 在 Linux/macOS 上都成功
- [ ] `cppcheck --enable=all` 无 ERROR 级问题
- [ ] `.clang-format` 配好，格式化后的代码符合团队风格

## 常见坑

- ⚠️ QEMU 的 MPS2 默认带 semihosting，但 SiFive E 需要 `-bios none -semihosting`
- ⚠️ RISC-V 工具链的 `-march` 与 `-mabi` 必须匹配（`rv32imac` + `ilp32`）
- ⚠️ macOS 上 Homebrew 安装的 `riscv64-unknown-elf-gcc` 是 64 位模式，要选 32 位版本或加 `-m32`

## 接下来

进入 [Phase 2 — 硬件抽象层 HAL](phase-2-hal.md)，把临时 `printk` 替换成真实的 UART 驱动。
