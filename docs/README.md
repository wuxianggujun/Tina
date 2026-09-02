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

## 15 分钟上手（推荐顺序）

目标：建立心智模型，能写一个 `IGameState` 并知道一帧里发生了什么。不必通读全部主题文档。

| 步 | 读什么 | 带走什么 |
| --- | --- | --- |
| 1 | [设计导读](design.md) | 产品定位、七条原则、输入/资源/UI 三条数据流 |
| 2 | [架构总览](architecture.md) | 模块依赖、所有权、启动事务、每帧顺序 |
| 3 | [Public API](public-api.md) | `Desktop::CreateEngine`、Application/State、借用寿命 |
| 4 | [Runtime](runtime.md) | 帧 pipeline、State 栈、policy 两套语义、输入四段式 |
| 5 | 跑 sample | `tina_sample_ui_showcase` / `tina_sample_2d` / `tina_sample_3d`（命令见 [构建](building.md)） |
| 6 | 读 sample 源码 | UI 工作台：`samples/ui_showcase`；2D 产品接线：`samples/2d_tilemap_bgfx`（含 PauseOverlay policy）；3D：`samples/3d*` |

改契约或公开头前：对照 [design-freeze](design-freeze.md) 与对应 ADR；验证按 [testing](testing.md) 直接跑 GoogleTest executable。

### 心智模型（一图）

```text
游戏: IGameApplication ──createInitialState──► IGameState (栈, 定深 8)
                      │
产品入口: Desktop::CreateEngine(EngineConfig)
                      │
              EngineHost (唯一组合根)
         Platform · Task · Render · UI · Audio
                      │
一帧: poll → UI route → Action → fixed* → frame
      → 栈命令 commit → extract → updateUI → commit UI
      → submitFrame → present → latch Camera2D
```

普通游戏只实现 Application/State，并调用 `Tina::Desktop::CreateEngine`；不要自建第二套主循环，
不要取得 `IRenderDevice*` 或在 phase context 里缓存指针跨回调。

### 按任务继续读

| 你要做的事 | 主题文档 |
| --- | --- |
| 改帧相位 / 输入 / 栈 | [Runtime](runtime.md) · [Platform 与 Input](platform-input.md) |
| 改 UI 树 / 命中 / DisplayList | [Retained UI](ui.md) · [UI 框架设计](ui-framework.md) |
| 改现代视觉 / Desktop Shell / Theme Token | [Modern Desktop UI](ui-modern-desktop.md) · [Retained UI](ui.md) |
| 规划或实现 Editor 整体美化、Project Assets 缩略图、文件拖入与状态反馈 | [Editor UI/UX 路线图](editor-ui-ux-roadmap.md) · [Editor 2D / 3D](editor-2d.md) · [Modern Desktop UI](ui-modern-desktop.md) |
| 规划 Editor 下一批 authoring 功能（Tile Palette、设置持久化、Copy/Paste、Physics/FX/Prefab authoring） | [Editor 功能扩展计划](editor-feature-plan.md) · [Editor 2D / 3D](editor-2d.md) · [Backlog](backlog.md) |
| 改 2D/3D 抽取或 World | [Scene](scene-ecs.md) · [2D](game-2d.md) · [3D](game-3d.md) |
| 改 2D 栅格导航 / TileMap 导航转换 | [2D 导航](navigation2d.md) · [2D](game-2d.md) · [资源](resources.md) |
| 改 2D World/gameplay 存档 | [World2D 序列化](world2d-serialization.md) · [Scene](scene-ecs.md) |
| 改 2D/3D Editor、Project Browser/document tabs、World/TileMap/SpriteAnimation authoring、undo、保存、Timeline 或 viewport | [Editor 2D / 3D](editor-2d.md) · [World2D 序列化](world2d-serialization.md) · [资源](resources.md) · [3D](game-3d.md) |
| 改 Catalog / Cook / Handle | [资源](resources.md) |
| 改 submit / bgfx 边界 | [Render](rendering.md) |
| 用向量/四元数/矩阵/包围盒/视锥，或加新几何类型 | [Math](math.md) · [ADR 0035](adr/0035-math-module-boundaries.md) |
| 用 timer/tween/sequence，或让两个 gameplay owner 解耦通信 | [Gameplay 工具层](gameplay-tooling.md) · [ADR 0036](adr/0036-gameplay-tooling-boundaries.md) |
| 做 3D 角色动画：crossfade / 状态机 / blend tree / layer+mask / root motion / IK | [3D 动画图](animation-3d.md) · [ADR 0037](adr/0037-animation3d-graph-boundaries.md) |
| 查 `std::terminate`、崩溃或 Editor 致命退出 | [Core](core.md) · [Editor 2D / 3D](editor-2d.md) · [测试](testing.md) |
| 选 preset / 跑门禁 | [构建](building.md) · [测试](testing.md) |
| 查“允许做什么” | [design-freeze](design-freeze.md) · [ADR](adr/README.md) |
| 查“下一步做什么” | [Roadmap](roadmap.md) · [Backlog](backlog.md) |

## 从这里开始（完整索引路径）

1. [设计导读](design.md)：产品定位、模块协作和核心约束。
2. [架构总览](architecture.md)：当前 target、依赖方向、帧数据流和 Legacy 边界。
3. [Public API](public-api.md)：`include/tina` 公共面与游戏正确姿势。
4. [Runtime](runtime.md)：帧 pipeline、State 栈与输入。
5. [Roadmap](roadmap.md) / [Backlog](backlog.md)：优先级与可验收工作项。
6. [构建说明](building.md) 与 [测试说明](testing.md)：preset 与直接门禁。

## 主题文档

| 主题 | 文档 |
| --- | --- |
| Runtime / 公共 API | [Runtime](runtime.md) · [公共 API](public-api.md) · [Gameplay](gameplay.md) · [Gameplay 工具层](gameplay-tooling.md) |
| Platform / Input / Task | [Platform 与 Input](platform-input.md) · [Task System](task-system.md) |
| 3D 动画 | [3D 动画图](animation-3d.md) · [3D](game-3d.md) |
| Scene / 2D / 3D / Navigation / Editor | [Scene](scene-ecs.md) · [2D](game-2d.md) · [2D 导航](navigation2d.md) · [World2D 序列化](world2d-serialization.md) · [Editor 2D / 3D](editor-2d.md) · [Editor UI/UX 路线图](editor-ui-ux-roadmap.md) · [Editor 功能扩展计划](editor-feature-plan.md) · [3D](game-3d.md) |
| Render / Asset | [Render](rendering.md) · [资源](resources.md) |
| UI / Audio / Physics | [Retained UI](ui.md) · [UI 框架设计](ui-framework.md) · [Modern Desktop UI](ui-modern-desktop.md) · [Audio](audio.md) · [Physics](physics.md) |
| Network | [网络](network.md) |
| Core / Math / 性能 / 依赖 | [Core](core.md) · [Math](math.md) · [性能与内存](performance-memory.md) · [依赖治理](dependencies.md) |
| 参考与完整目标 | [vNext 目标架构](vnext-architecture.md) · [Carbon 参考](carbon-reference.md) |
| 风险 | [风险登记](risks.md) |

## 退役与证据

- [M12 产品退役说明](evidence/m12-legacy-ui-retirement.md)：Legacy 产品图删除后的准确边界。
- [M12 门禁清单](evidence/m12-gate-checklist.md)：产品删除已完成，剩余复验与扫尾状态。
- [M12 Windows 证据](evidence/m12-evidence-windows.md)：带日期的本机运行摘录，不代替当前测试。
- [M12 Linux 证据](evidence/m12-evidence-linux.md)：Linux tip 复验摘录。
- [UI 状态反馈 Windows 证据](evidence/ui-state-feedback-evidence-windows.md)：Dark/Light 交互状态产品差分门禁。

### 带日期的一次性运行快照

以下文档都是**某一次运行的历史快照**，不是当前契约。文件名或标题里的日期即取证日期；其中的
路径、目标名、计数与门禁结论都可能已被后续提交推翻。引用前先按 `docs/README.md` 顶部的优先级
回到当前源码/CMake 复核。

- [Agent 视觉巡检（2026-08-03）](evidence/agent-visual-inspect-20260803.md)：sample 截图逐项人工检查。
- [Docker tip 证据（2026-08-03）](evidence/docker-tip-evidence-20260803.md)：Linux 容器门禁详表。
- [SDK-001 Windows consumer 证据](evidence/sdk-001-windows-consumer-evidence.md)：moved-prefix install/consumer 门禁。
- [UI-002 Windows UIA 证据](evidence/ui-002-uia-evidence-windows.md)：外部 HWND UIA 客户端门禁。
- [UI-002 Narrator/Inspect 清单](ui-002-narrator-inspect-checklist.md)：人工金标步骤，自动门禁不覆盖。
- [UI-003 tip 证据](evidence/ui-003-tip-evidence.md)：UI-003 视觉/尺寸矩阵取证。
- [PERF-002 机器画像草稿](perf-002-machine-profile-draft.md)：benchmark 机器画像草稿。
- [PERF-002 临时证据](evidence/perf-002-provisional-evidence.md)：未固化的 benchmark 取证。

当前产品 UI 是 `include/tina/ui` + `src/ui`。旧 UI 产品图已删除，但不能据此删除或称退役当前
`src/ui`。产品入口是 `Tina::Desktop` 与 `tina_sample_*`，不再存在 Legacy `Tina.exe`。
