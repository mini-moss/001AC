# Phase 9 — 质量保障

> **预估周期**：贯穿全程，本 Phase 是「建立流程」
> **进入门槛**：Phase 3 起就要并行做
> **退出标志**：每次发布前完成所有 checklist

实时系统的 bug 一旦发作就可能是「机器人在展厅撞了人」级别的灾难。质量保障不是后置任务，**是基础设施**。

## 9.1 单元测试

### 9.1.1 Host 测试

在 x86 Linux 上跑内核逻辑测试，**不需要硬件**：

```c
// tests/unit/test_sync.c
#include <test.h>   // 自写极简框架或 CMocka

// 模拟 tick 推进
void mock_tick_advance(tick_t ms);

void test_sem_take_timeout(void) {
    sem_t s;
    sem_init(&s, 0, 1);

    // 启动一个"任务"在后台 take
    start_fake_task("t1", TASK_PRIO_LOW);
    t1_take_sem_blocking(&s, 100);

    // 推进 50ms，sem 还应阻塞
    mock_tick_advance(50);
    assert(t1_state == TASK_BLOCKED);

    // give 后推进 1ms
    sem_give(&s);
    mock_tick_advance(1);
    assert(t1_state == TASK_READY);
}
```

**框架选项**：
- CMocka：成熟、有 mock
- 自写：~200 行，更轻量

### 9.1.2 代码覆盖率

```bash
# GCC/Clang 编译时加 -fprofile-arcs -ftest-coverage
# 运行测试后用 gcov / lcov
lcov --capture --directory build/ --output-file coverage.info
genhtml coverage.info --output-directory coverage_html
```

目标：**核心内核 80%+ 覆盖**，驱动层 60%+（mock 总线情况下）。

## 9.2 QEMU 集成测试

```bash
# tools/qemu/run_tests.sh
#!/bin/bash
set -e

TEST_ELF=build/stm32f407-disco/test_runner.elf

# -icount 启用指令计数，模拟确定时间
# -d trace 抓执行 trace
# -serial tcp:... 自动化串口输入输出
qemu-system-arm -machine mps2-an385 -nographic \
    -kernel $TEST_ELF \
    -semihosting-config enable=on,target=native \
    -icount shift=auto,sleep=on \
    -serial file:/tmp/serial.log \
    -d test_verbose

# 检查输出
if grep -q "ALL TESTS PASSED" /tmp/serial.log; then
    echo "PASS"
    exit 0
else
    echo "FAIL"
    cat /tmp/serial.log
    exit 1
fi
```

**测试场景**：
- 任务切换路径
- 中断嵌套
- 调度抖动统计
- 同步原语互操作

## 9.3 硬件在环（HIL）测试

实物板 + 自动化测试：

```c
// tests/hil/test_motor.c
void test_motor_pwm(void) {
    // 启动测试模式：上位机发指令，板子执行并回报
    test_begin("motor_pwm");

    pwm_set_duty(motor_l, 0, 3000);  // 30%
    test_delay(100);

    // 读取 GPIO 翻转频率验证 PWM 输出
    int freq = gpio_measure_freq(MOTOR_TEST_PIN);
    assert(freq > 24500 && freq < 25500);  // 25kHz ± 2%

    // 读取电流传感器（如果有）
    int current = adc_read(MOTOR_CURRENT_ADC);
    assert(current > 100);  // 至少 100mA

    test_pass();
}
```

**CI 集成**（GitHub Actions）：

```yaml
# .github/workflows/hil.yml
name: HIL Test
on: [push]

jobs:
  hil:
    runs-on: [self-hosted, robotics]   # 连接到测试台的 runner
    steps:
      - uses: actions/checkout@v4
      - name: Build
        run: make BOARD=stm32f407-disco
      - name: Flash
        run: make flash
      - name: Run tests
        run: make hil-test
```

## 9.4 WCET 分析

### 9.4.1 插桩测量

```c
// arch/<cpu>/include/hal.h
static inline void wcet_pin_high(void) { /* 拉高测试 GPIO */ }
static inline void wcet_pin_low(void)  { /* 拉低测试 GPIO */ }

#define WCET_BEGIN(name) \
    wcet_pin_high();    \
    /* do something */  \
    wcet_pin_low()

// 用法
int sem_take(sem_t *sem, tick_t timeout) {
    WCET_BEGIN("sem_take");
    // ...
    wcet_pin_low();
    return SEM_OK;
}
```

### 9.4.2 工具化分析

- **Tracealyzer**（商业）：可视化任务调度，记录每个 API 的执行时间分布
- **自写脚本**：抓 GPIO 翻转 trace，统计 P95 / P99 / max

### 9.4.3 记录到 API 文档

```c
/**
 * @brief Take a semaphore.
 *
 * @wcet
 *   - Non-blocking path: 1.2 μs (Cortex-M4 @ 168MHz)
 *   - Blocking path: 2.5 μs (Cortex-M4 @ 168MHz)
 *
 * @complexity O(1)
 */
int sem_take(sem_t *sem, tick_t timeout);
```

## 9.5 静态分析

### MISRA-C 子集

MISRA-C 规则过多（> 100 条），先抓重点：

| 类别 | 规则示例 | 工具 |
|---|---|---|
| 类型安全 | 禁止隐式类型转换 | `clang-tidy` |
| 控制流 | 禁止 `goto` | `cppcheck` |
| 函数 | 禁止递归 | `clang-tidy` |
| 指针 | 禁止算术运算 | 自写检查 |
| 标准库 | 禁止 `malloc` / `free` | `clang-tidy` |

```yaml
# .clang-tidy
Checks: >
    -*,
    clang-analyzer-*,
    bugprone-*,
    cert-*,
    misc-*,
    readability-*,
    -bugprone-easily-swappable-parameters

WarningsAsErrors: ''
```

### 自定义检查

```bash
# tools/check_no_malloc.sh
#!/bin/bash
if grep -rn '\bmalloc\b\|\bcalloc\b\|\brealloc\b\|\bfree\b' \
    kernel/ arch/ drivers/ 2>/dev/null | grep -v 'tlsf' | grep -v '^[^:]*:[[:space:]]*//'; then
    echo "ERROR: bare malloc/free found"
    exit 1
fi
```

## 9.6 压力测试

### 9.6.1 24h Soak Test

```c
// tests/stress/soak.c
// 创建 N 个任务，全部跑随机同步原语操作
// 每 1 分钟打印一次任务状态
// 24 小时后检查无内存泄漏、无死锁、无栈溢出
```

### 9.6.2 Fault Injection

```c
// 故意制造故障
void test_malloc_failure(void) {
    fault_inject_malloc_fail = true;
    task_create(...);   // 应优雅失败，不应崩溃
    assert(errno == ENOMEM);
}

void test_uart_rx_overrun(void) {
    // 故意让 DMA buffer 溢出
    // 验证驱动能恢复，不卡死
}
```

## 9.7 安全 / 健壮性检查清单

- [ ] 所有外部输入做边界检查（CAN 帧长、UART 字节数、I2C 应答）
- [ ] 指针参数做 NULL 检查
- [ ] 数组索引做越界检查（用 `-fsanitize=bounds`）
- [ ] 所有 `assert` 编译期 + 运行时双开关
- [ ] watchdog 守护：idle 任务喂狗；任何任务卡死 > 100ms 触发复位
- [ ] 关键状态写入用「写两次 + 校验」防位翻转（特别在 NVM 中）

### Watchdog 集成

```c
// kernel/watchdog.c
static tick_t last_pet[MAX_TASKS];

void watchdog_pet(task_t *t) {
    last_pet[t->task_id] = tick_get();
}

void watchdog_check(void) {
    for_each_task(t) {
        if (t->state == TASK_BLOCKED) continue;
        if (tick_get() - last_pet[t->task_id] > CONFIG_WATCHDOG_TIMEOUT_MS) {
            printk("!!! task '%s' hung, reset\n", t->name);
            cpu_reset();
        }
    }
}
```

## 9.8 发布前 checklist

每次发版本前必查：

### 功能
- [ ] 所有单元测试通过
- [ ] 所有 QEMU 集成测试通过
- [ ] 实物板所有 HIL 测试通过
- [ ] 24h soak test 无崩溃

### 性能
- [ ] WCET 满足需求
- [ ] RAM / ROM 占用 < 预算
- [ ] 启动时间 < 500ms

### 质量
- [ ] 静态分析无 ERROR
- [ ] 关键模块覆盖率 > 80%
- [ ] 所有公开 API 文档完整
- [ ] CHANGELOG 记录本次变更
- [ ] 版本号遵循 semver
- [ ] 至少 2 名维护者 code review

### 兼容性
- [ ] 至少 2 个 board 通过
- [ ] 至少 2 个 CPU 架构通过
- [ ] Kconfig 组合完整覆盖

## 9.9 工具链集成（CI）

```yaml
# .github/workflows/ci.yml
name: CI
on: [push, pull_request]

jobs:
  static_analysis:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: cppcheck
        run: cppcheck --enable=all --error-exitcode=1 kernel/ arch/ drivers/
      - name: clang-tidy
        run: |
            clang-tidy --config-file=.clang-tidy $(find kernel arch drivers -name '*.c')

  unit_tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build host tests
        run: make tests-host
      - name: Run
        run: ./build/host/test_runner

  qemu_tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install QEMU
        run: sudo apt install qemu-system-arm qemu-system-riscv
      - name: Run
        run: ./tools/qemu/run_tests.sh
```

## 验证标准

- [ ] CI 流水线全绿
- [ ] 覆盖率报告趋势向上
- [ ] 24h soak test 报告在 docs/soak/
- [ ] 至少 1 次实物板 72h 长稳测试
- [ ] WCET 数据表在 `docs/wcet.md`

## 常见坑

- ⚠️ QEMU 不能完全模拟真实硬件时序，HIL 必须用实物
- ⚠️ 单元测试覆盖率 100% 不等于无 bug，覆盖率 > 80% + 强静态分析更可靠
- ⚠️ watchdog 喂狗任务要独立于 idle，防止「应用正常 + idle 死」的场景漏检
- ⚠️ 静态分析要作为 PR 合并门禁，不要靠人记得

## 接下来

进入 [Phase 10 — 文档与发布](phase-10-release.md)，把项目包装成可分发版本。
