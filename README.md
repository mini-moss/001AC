# minimoss 001AC

> 用 C 语言从零构建一个**双 ISA 统一混合内核**——同一份内核同时调度硬实时任务（电机/传感器）与通用任务（shell/文件 IO），跨 ARM Cortex-A 与 RISC-V RV64 验证可移植性

详细愿景见 [docs/requirements/requirements.md §0](docs/requirements/requirements.md#0-项目愿景)；架构背景见 [docs/dev-log/005](docs/dev-log/005-unified-hybrid-kernel-v1-subscope.md)。

## 项目状态

🚧 **Phase 0 — 需求与决策已定稿**（v0.3.1）：宪法 + 6 ADR 就绪，待 commit；Phase 1 代码尚未开始

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

- **平台**：ARM Cortex-A (Pi 4/5) + RISC-V RV64 (SG2002 C906)；详见 [ADR-0001](docs/requirements/decisions/0001-cpu-arch.md)
- **架构**：双 ISA 统一混合内核（RT + GPOS 共存），每进程独立页表 + ASID；详见 [ADR-0004](docs/requirements/decisions/0004-single-address-space.md)
- **v1.0 sub-scope**：进程隔离 + 双队列调度 + 基础 VFS（vfat）+ 用户态 ELF + 简单 shell；详见 [requirements §9](docs/requirements/requirements.md#9-v10-sub-scope-详细清单)
- **v1.0 不做**：capability / 完整 POSIX / 网络 / GUI / 动态加载（留 v2.0+）；详见 [requirements §6](docs/requirements/requirements.md#6-不做什么out-of-scope)

完整决策记录见 [docs/requirements/decisions/](docs/requirements/decisions/)（6 份 ADR），架构演进见 [docs/dev-log/](docs/dev-log/)。

## 许可证

Apache License 2.0；详见 [ADR-0003](docs/requirements/decisions/0003-license.md)。
