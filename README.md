# 001AC

> 用 C 语言从零为机器人构建一个多平台实时操作系统（RTOS）

## 项目状态

🚧 **实验性** —— 路线图阶段，代码尚未实现

## 文档

完整开发指南收录在 [`docs/`](docs/) 目录下，以 [mdbook](https://rust-lang.github.io/mdBook/) 形式组织。

### 在线阅读

启动本地预览：

```bash
cargo install mdbook       # 首次运行需要安装
cd docs/
mdbook serve                # 浏览器打开 http://localhost:3000
```

### 目录结构

```text
docs/
├── book.toml
├── requirements/                   Phase 0 决策与 ADR
│   ├── requirements.md             需求规格（宪法）
│   └── decisions/                  ADR（架构决策记录）
├── dev-log/                        实施日志（时间线）
├── architecture/                   架构图（后续填充）
├── reports/                        评审/测试报告（后续填充）
└── src/                            mdbook 源（路线图）
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

## 范围

- **平台**：ARM Cortex-A (Pi 4/5) + RISC-V RV64 (SG2002 C906)
- **起点**：从零手写内核（**带 MMU**，单地址空间）
- **功能**：基础内核 + 电机/传感器驱动 + micro-ROS + 文件系统/网络/GUI

详细范围与排除项见 [docs/requirements/requirements.md](docs/requirements/requirements.md)，决策背景见 [docs/dev-log/](docs/dev-log/)。

## 许可证

待定（建议 Apache 2.0，见 [requirements §5](docs/requirements/requirements.md#5-许可协议)）。
