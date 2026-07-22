# Tina 文档索引

Tina 文档按用途分为四类，避免把设计目标、当前事实和一次性测试结果混在一起：

| 类型 | 权威文档 | 内容 |
| --- | --- | --- |
| 当前事实 | [架构总览](architecture.md)、各主题文档、源码/CMake | 当前模块、接口、数据流与已知边界 |
| 决策 | [设计冻结清单](design-freeze.md)、[ADR 索引](adr/README.md) | Accepted/Proposed/Deferred；Accepted ADR 不在主题文档中静默改写 |
| 任务 | [Roadmap](roadmap.md)、[Backlog](backlog.md) | Now/Next/Later/Done 与可验收工作项 |
| 验证 | [构建说明](building.md)、[测试说明](testing.md)、M12 证据 | 可复现命令、门禁拓扑和带日期的证据 |

冲突时优先级为：当前源码/CMake/实际运行结果 > Accepted ADR 与设计冻结清单 > 主题文档 >
历史证据。测试数量属于易变证据，不应用来定义架构完成度。

## 从这里开始

1. [设计导读](design.md)：产品定位、模块协作和核心约束。
2. [架构总览](architecture.md)：当前 target、依赖方向、帧数据流和 Legacy 边界。
3. [Roadmap](roadmap.md)：当前阶段的 Now/Next/Later/Done。
4. [Backlog](backlog.md)：带 ID、依赖、验收条件和证据要求的可执行任务。
5. [构建说明](building.md)与[测试说明](testing.md)：选择 preset 并执行直接门禁。

## 主题文档

| 主题 | 文档 |
| --- | --- |
| Runtime / 公共 API | [Runtime](runtime.md) · [公共 API](public-api.md) · [Gameplay](gameplay.md) |
| Platform / Input / Task | [Platform 与 Input](platform-input.md) · [Task System](task-system.md) |
| Scene / 2D / 3D | [Scene](scene-ecs.md) · [2D](game-2d.md) · [3D](game-3d.md) |
| Render / Asset | [Render](rendering.md) · [资源](resources.md) |
| UI / Audio / Physics | [Retained UI](ui.md) · [Audio](audio.md) · [Physics](physics.md) |
| Core / 性能 / 依赖 | [Core](core.md) · [性能与内存](performance-memory.md) · [依赖治理](dependencies.md) |
| 参考与完整目标 | [vNext 目标架构](vnext-architecture.md) · [Carbon 参考](carbon-reference.md) |
| 风险 | [风险登记](risks.md) |

## 退役与证据

- [M12 产品退役说明](m12-legacy-ui-retirement.md)：Legacy 产品图删除后的准确边界。
- [M12 门禁清单](m12-gate-checklist.md)：产品删除已完成，剩余复验与扫尾状态。
- [M12 Windows 证据](m12-evidence-windows.md)：带日期的本机运行摘录，不代替当前测试。

当前产品 UI 是 `include/tina/ui` + `src/ui`。旧 UI 产品图已删除，但不能据此删除或称退役当前
`src/ui`。产品入口是 `Tina::Desktop` 与 `tina_sample_*`，不再存在 Legacy `Tina.exe`。

