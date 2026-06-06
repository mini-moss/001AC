# Phase 10 — 文档与发布

> **预估周期**：1 周+（持续维护）
> **进入门槛**：Phase 9 质量门禁全绿
> **退出标志**：`v1.0.0` tag 发布

写代码是 50% 的工作，另外 50% 是**让代码可被发现、可被理解、可被使用**。

## 10.1 文档层级

| 文档 | 目标读者 | 维护者 |
|---|---|---|
| `README.md` | 第一次访问仓库的人 | 项目 owner |
| `docs/` (本 mdbook) | 开发者 / 贡献者 | 全员 |
| API 参考 (doxygen) | 应用开发者 | 全员 |
| 移植指南 | 新平台移植者 | HAL 维护者 |
| 应用手册 | 终端用户 / 集成商 | 应用层维护者 |
| ADR（架构决策记录） | 所有人 | 决策者 |

## 10.2 README

```markdown
# Robot RTOS

> 用 C 语言从零为机器人构建的多平台实时操作系统。

## 特性
- 多架构：ARM Cortex-M、RISC-V
- 优先级位图调度，O(1) 任务切换
- 完整同步原语 + 优先级继承互斥
- 统一驱动模型（device + ops）
- micro-ROS / DDS 集成
- 静态优先内存管理

## 快速开始
\`\`\`bash
git clone https://github.com/<org>/robot-rtos.git
cd robot-rtos
make BOARD=stm32f407-disco run
\`\`\`

## 文档
- [开发指南](docs/)（mdbook）
- [API 参考](https://<org>.github.io/robot-rtos/api/)
- [移植指南](docs/porting.md)

## 许可证
Apache 2.0

## 状态
v0.x — 实验性，不建议用于生产
```

## 10.3 mdbook 完整结构（本仓库）

```
docs/
├── book.toml
└── src/
    ├── SUMMARY.md
    ├── introduction.md
    ├── overview.md
    ├── phase-0-requirements.md
    ├── phase-1-infrastructure.md
    ├── phase-2-hal.md
    ├── phase-3-scheduler.md
    ├── phase-4-sync.md
    ├── phase-5-memory.md
    ├── phase-6-drivers.md
    ├── phase-7-microros.md
    ├── phase-8-advanced.md
    ├── phase-9-quality.md
    ├── phase-10-release.md
    └── next-steps.md
```

**本地预览**：

```bash
cargo install mdbook
cd docs/
mdbook serve    # 浏览器打开 http://localhost:3000
```

**部署**：

```yaml
# .github/workflows/docs.yml
name: Docs
on:
  push:
    branches: [main]
    paths: [docs/**]

jobs:
  deploy:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install mdbook
        run: |
            curl -sSL https://github.com/rust-lang/mdBook/releases/download/v0.4.40/mdbook-v0.4.40-x86_64-unknown-linux-gnu.tar.gz | tar -xz
            sudo mv mdbook /usr/local/bin/
      - name: Build
        run: cd docs && mdbook build
      - name: Deploy
        uses: peaceiris/actions-gh-pages@v3
        with:
          github_token: ${{ secrets.GITHUB_TOKEN }}
          publish_dir: ./docs/book
```

## 10.4 API 参考（doxygen）

### 配置

```doxygen
# Doxyfile
PROJECT_NAME           = "Robot RTOS"
PROJECT_NUMBER         = $(VERSION)
INPUT                  = include/ kernel/ drivers/ arch/ boards/
GENERATE_HTML          = YES
GENERATE_LATEX         = NO
EXTRACT_ALL            = NO
JAVADOC_AUTOBRIEF      = YES
WARN_AS_ERROR          = YES
WARN_NO_PARAMDOC       = YES
```

### 风格约定

每个公开 API 必须有：

```c
/**
 * @file sem.h
 * @brief Counting/binary semaphores.
 */

/**
 * @brief Take a semaphore with timeout.
 *
 * Decrements the semaphore count, blocking the current task if count
 * is zero. Returns when the semaphore is given or timeout expires.
 *
 * @param sem      Pointer to initialized semaphore
 * @param timeout  Maximum ticks to wait. Use SEM_WAIT_FOREVER for
 *                 unbounded wait, or SEM_NO_WAIT for non-blocking.
 *
 * @retval SEM_OK            Acquired successfully
 * @retval SEM_TIMEOUT       Timeout expired
 * @retval SEM_INVALID       Semaphore not initialized
 *
 * @wcet
 *   - Non-blocking: 1.2 μs (Cortex-M4 @ 168MHz)
 *   - Blocking:     2.5 μs (Cortex-M4 @ 168MHz)
 *
 * @note Not ISR-safe. Use sem_give_from_isr() in interrupt context.
 *
 * @complexity O(1)
 */
int sem_take(sem_t *sem, tick_t timeout);
```

### 自动部署

```yaml
# .github/workflows/api-docs.yml
- name: Generate API docs
  run: doxygen Doxyfile
- name: Deploy
  uses: peaceiris/actions-gh-pages@v3
  with:
    publish_dir: ./docs/api
```

## 10.5 移植指南

```markdown
# 移植到新板子

支持新板子需要做两件事：

## 1. 在 boards/ 添加 BSP

复制 boards/stm32f407-disco/ 模板，修改：
- 时钟配置（PLL、分频）
- UART 引脚映射
- 链接脚本中的 Flash/RAM 地址

## 2. 在 Kconfig 添加条目

config BOARD_MY_BOARD
    bool "My Custom Board"
    select SOC_STM32F407
    depends on ARCH_ARM

## 3. CI 验证

make BOARD=my-board run
```

## 10.6 应用手册

面向「用本 RTOS 写机器人应用」的人：

- 任务设计模式（周期性、事件驱动）
- 控制环路实现
- 与 ROS 2 互通
- 调试技巧

## 10.7 CHANGELOG

```markdown
# Changelog

## [1.0.0] - 2026-06-30

### Added
- Priority bitmap scheduler with O(1) task selection
- Synchronization primitives: sem, mutex (priority inheritance),
  queue, event flags, software timers
- Static memory pool with O(1) alloc/free
- Unified device driver model
- Drivers: GPIO, UART (DMA), SPI, I2C, CAN, PWM, encoder
- Robot drivers: MPU6050 IMU, diff-drive odometry
- micro-ROS integration via custom transport
- Shell with ps / mem / drv commands
- Static analyzer + unit test + QEMU integration test + HIL test
- Documentation: mdbook, doxygen, porting guide

### Fixed
- PendSV priority inversion causing delayed task wakeup
- DMA double-buffer pointer alignment

### Changed
- API: renamed k_* prefix to plain module names

### Removed
- legacy v0.x sync API (use sem/mutex/queue)

[1.0.0]: https://github.com/<org>/robot-rtos/compare/v0.9.0...v1.0.0
```

## 10.8 版本与发布

### semver

- **Major**：API 不兼容
- **Minor**：新增功能（向后兼容）
- **Patch**：Bug 修复（向后兼容）

**0.x 阶段**说明：
- API 可能变动
- 文档可能有遗漏
- 不建议用于生产

**v1.0.0 准入**：
- [ ] 2+ 个真实项目使用过
- [ ] 6 个月 API 稳定期
- [ ] 至少 1 名非作者贡献者完成 commit
- [ ] 安全审计（如有需要）

### 发布脚本

```bash
# tools/release.sh
#!/bin/bash
set -e

VERSION=$1
if [ -z "$VERSION" ]; then
    echo "Usage: $0 <version>"
    exit 1
fi

# 更新 CHANGELOG
echo "## [$VERSION] - $(date +%Y-%m-%d)" >> CHANGELOG.md

# 提交版本
git add -A
git commit -m "Release $VERSION"
git tag -a "v$VERSION" -m "Release $VERSION"

# 推送
git push origin main
git push origin "v$VERSION"

# 触发 GitHub Release
gh release create "v$VERSION" \
    --title "v$VERSION" \
    --notes-file CHANGELOG.md
```

## 10.9 社区与贡献

### CONTRIBUTING.md

- 提 Issue 的模板（bug / feature / question）
- PR 流程（fork → feature branch → CI 通过 → review → merge）
- 编码规范（链接到本 mdbook）
- 提交者许可协议（如需要 CLA）

### CODE_OF_CONDUCT.md

基于 Contributor Covenant，强调技术讨论保持尊重。

## 10.10 后续路线图

### v1.1
- [ ] SMP 多核支持
- [ ] 动态加载（模块化 .so）
- [ ] 蓝牙 / Wi-Fi 驱动

### v1.2
- [ ] POSIX 兼容层
- [ ] 容器化（Docker 嵌入式开发）
- [ ] RISC-V 64 位支持

### v2.0
- [ ] 完整安全认证（IEC 61508 SIL 2）
- [ ] 实时 Linux 桥接（cgroup）
- [ ] 工具链：可视化调试器、Tracealyzer 集成

## 验证标准

- [ ] mdbook 在 GitHub Pages 正常显示
- [ ] doxygen API 参考可搜索
- [ ] 移植指南在 1 个新板上成功验证
- [ ] CHANGELOG 格式完整
- [ ] `v1.0.0` tag 已创建 + GitHub Release

## 常见坑

- ⚠️ 文档**写代码时同步更新**，不要留到 release 前夕
- ⚠️ 公开 API 改了先在 `-dev` 系列发布，至少 1 个月
- ⚠️ doxygen 警告要当 error 处理，避免「半文档化」API
- ⚠️ 移植指南如果步骤 > 10 步，需要拆分或自动化

## 接下来

阅读 [下一步行动](next-steps.md)，从这里开始你的实际项目。
