# ADR-0003 — 许可证选型

> **Status**：Accepted
> **Date**：2026-06-07
> **Deciders**：junbo.dai
> **Refs**：[requirements §5](../requirements.md#5-许可协议)

---

## Context

这是一个**开源**、**长期维护**的机器人 RTOS。许可证选择要平衡：

1. **工业机器人公司**会看专利条款（机器人领域专利环境复杂）
2. **商业公司**需要明确能商用
3. **学术/爱好者**需要友好条款
4. **第三方依赖**（micro-ROS / lwIP / LVGL / FatFS）各有协议，要兼容

---

## Decision

**自身代码采用 Apache License 2.0**；第三方依赖保留各自原协议。

| 类别 | 协议 | 理由 |
|---|---|---|
| 自身代码 | **Apache 2.0** | 见下文 |
| micro-ROS | Apache 2.0 | 同协议，融合无障碍 |
| lwIP | BSD-3-Clause | 与 Apache 2.0 兼容 |
| LVGL | MIT | 与 Apache 2.0 兼容 |
| FatFS | **暂不引入** | 协议非 OSI；评估改用 Petit FatFS / FatFs IoT |

---

## Consequences

### 为什么是 Apache 2.0 而不是 MIT

| 维度 | MIT | Apache 2.0 | 选择 |
|---|---|---|---|
| 简单 | ✅ 极简（~190 词） | 较长（~400 词 + 附录） | MIT 赢 |
| 商用 | ✅ 允许 | ✅ 允许 | 平 |
| 专利授权 | ❌ **无** | ✅ **显式授予** | **Apache 赢** |
| 商标保护 | ❌ 无 | ✅ 有 | Apache 赢 |
| 贡献者专利反诉条款 | ❌ 无 | ✅ 有（终止条件） | 视场景 |
| 工业机器人契合度 | 中 | **高** | **Apache 赢** |

**关键差异**：Apache 2.0 的**专利授权条款**。工业机器人公司最怕「用你的代码 → 被你告专利侵权」，Apache 2.0 显式终止这种担忧。

### 积极

- ✅ **工业友好**：专利授权 + 商标保护
- ✅ **贡献者放心**：Apache 也给贡献者提供专利反诉保护
- ✅ **第三方协议兼容**：BSD / MIT / Apache 三大族互相兼容，依赖混用无障碍
- ✅ **公司内部可用**：Apache 是绝大多数公司的「白名单协议」

### 消极

- ❌ 协议文本比 MIT 长，新人理解成本高
- ❌ 「贡献者专利反诉」条款有争议（极少数场景下可能让贡献者退缩）

### 中性

- LICENSE 文件必须在仓库根
- 每个源文件**不必**加 license header（项目级 LICENSE 即可），但**建议**加（看团队习惯）
- 后续如果有人贡献代码，需要在 PR 里加「Signed-off-by」+ CLA（v1.0 不强制，v1.1 视情况引入）

---

## Alternatives Considered

### 备选 A：MIT

- ✅ 极简，全世界都懂
- ❌ **无专利授权**——工业场景直接被排除
- **结论**：pass

### 备选 B：GPL-2.0 / GPL-3.0

- ✅ 防商业闭源
- ❌ **病毒式传染**：集成到商业产品时，整个产品都得开源
- ❌ 工业机器人公司**绝不会**碰 GPL 内核（怕污染）
- **结论**：pass

### 备选 C：BSD-3-Clause

- ✅ 简单、跟 MIT 几乎一样
- ❌ 同样**无专利授权**
- **结论**：pass

### 备选 D：Dual-license（GPL + 商业授权）

- ✅ 商业用户付费可闭源
- ❌ 需要商业实体维护（律师 + 销售），本项目目前无此能力
- **结论**：v2.0+ 视情况，本 ADR 不预留

### 备选 E：MIT + 自己加 PATENT 单独文件

- ✅ 灵活
- ❌ 不是标准做法，法律可执行性弱
- **结论**：不如直接用 Apache 2.0

---

## Validation

- [x] 仓库根有 `LICENSE` 文件，文本 = Apache 2.0 标准文本（https://www.apache.org/licenses/LICENSE-2.0.txt）
- [x] README 明确写「Apache 2.0」
- [ ] 引入新第三方依赖时，**必须**更新本 ADR + README 第三方表
- [ ] 任一商业公司询问「能不能用」时 → 直接指回本 ADR

---

## Notes

- FatFS 评估见 [requirements §5](../requirements.md#5-许可协议) 注释
- 若未来选 v2.0 引入 dual-license，**新开 ADR**，不在本 ADR 修订
