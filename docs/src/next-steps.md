# 下一步行动

你已经读完了完整路线图。现在该从哪开始？

## 立即可做的 3 步

1. **在仓库创建骨架**
   - 把 `docs/` 内容已经在的目录结构落地
   - 添加 `.gitignore`、`.editorconfig`、`.clang-format`
   - 提交 `Initial docs` commit

2. **写完 `docs/requirements.md`**
   - 参照 [Phase 0](phase-0-requirements.md) 的模板
   - 包含：硬件平台、调度策略、API 风格、许可证、Out of Scope
   - 与至少一位同事 review

3. **在 QEMU 跑通 `printk`**
   - 这是 Phase 2 的退出标志
   - **风险最高的环节**，先验证工具链
   - 涉及：工具链、链接脚本、启动文件、UART 初始化

## 建议的第一个 PR

`feat: initial qemu hello with printk`

内容：
- `Makefile` 或 `CMakeLists.txt` 顶层
- `arch/arm/cortex-m/` + `arch/riscv/` 的启动 + HAL
- `boards/<bsp>/` 至少 1 个
- `kernel/printk.c`
- `app/main.c` 跑通
- `tools/qemu/run.sh`

预计 200–400 行 C + 100 行 asm + 100 行链接脚本。

## 第一个月时间表（建议）

| 周 | 目标 | 退出标志 |
|---|---|---|
| 1 | Phase 0 + Phase 1 | `requirements.md` + 工具链 + 仓库骨架 |
| 2 | Phase 2 | QEMU 上 `printk` 跑通 |
| 3 | Phase 3 | 3 个任务在 QEMU 跑，周期正确 |
| 4 | Phase 3 调试 + 实物板验证 | 实物板 LED 闪烁周期正确 |

## 团队建议

- **单人项目**：按 6 个月到 Phase 7（含基础驱动）估算
- **2 人**：可压缩到 3 个月
- **3+ 人**：建议分工
  - 人 A：HAL + 调度器
  - 人 B：同步原语 + 驱动框架
  - 人 C：CI / 测试 / 文档

## 常见心态陷阱

### 1. 「在没验证调度器前先做 GUI」
**不要**。GUI 是 Phase 8，没有稳定内核支撑就是空中楼阁。

### 2. 「先做完整功能再加测试」
**不要**。Phase 9 的检查清单是质量门禁，没有 CI 绿就不能 merge。

### 3. 「用一种 MCU 走完整个流程再移植」
**可以，但建议尽早做第 2 个平台**。第 2 个平台会逼出 HAL 中的耦合问题。

### 4. 「复制 FreeRTOS 改吧，何必手写」
如果目标是「做出能用的 RTOS」，**用 FreeRTOS**。本项目的价值在于**手写过程中学到的每一行**。但如果你只是想加快产出，及时切换到 FreeRTOS 是个合理选择。

## 需要我帮你做下一步吗？

我可以帮你：

1. **起草 `requirements.md` 模板**（基于你已选的硬件）
2. **搭建仓库骨架**（CMake + .clang-format + .gitignore + 目录）
3. **实现 Phase 2 的 HAL**（含 QEMU 验证脚本）—— 第一个能跑起来的实物
4. **实现 Phase 3 的最小调度器**（含 3 任务 demo）

告诉我你想从哪一步开始。

## 资源链接

- [ARM Cortex-M 编程手册](https://developer.arm.com/documentation/)
- [RISC-V 规范](https://riscv.org/technical/specifications/)
- [FreeRTOS（参考实现）](https://www.freertos.org/)
- [Zephyr（参考架构）](https://zephyrproject.org/)
- [micro-ROS](https://micro.ros.org/)
- [lwIP](https://savannah.nongnu.org/projects/lwip/)
- [LVGL](https://lvgl.io/)
- [FatFS](http://elm-chan.org/fsw/ff/)

## 反馈

如果读这份路线图时发现：
- 某 Phase 的预估周期明显偏短 / 偏长
- 某章节缺关键细节
- 顺序需要调整
- 验证标准不够具体

欢迎提 Issue 或直接修改——本 mdbook 本身就是项目的一部分。
