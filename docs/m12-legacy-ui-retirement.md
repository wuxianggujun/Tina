# M12：Legacy UI 退役盘点（**禁止删除**直至门禁闭合）

> 用户目标：彻底删除 Legacy UI（`src/ui/`）。  
> **当前结论：不得删除。** Legacy UI 仍是 Legacy 产品场景的唯一 UI 实现；vNext `Tina::UI`
> 尚未等价替代全部能力。

## 1. 两条 UI 轨道（事实）

| 轨道 | 路径 | 用途 | 能否删 |
| --- | --- | --- | --- |
| **Legacy UI** | `src/ui/*`（`UICore`、`UINode`、`NodeId`、`UITextEdit`、`UIScrollView`、`UIDialog`…） | Legacy `Application` + Menu/Game/Settings/Pause/WorldSelect 等产品 UI | **否** — 仍有生产消费者 |
| **vNext UI** | `include/tina/ui/*` + `src/vnext/ui/*` | 游戏内 HUD/菜单 Retained UI；`tina_sample_2d` panels/buttons/sliders 等 | 保留；**不是**桌面编辑器工具包 |

**禁止**同一 TU / 最终 link image 混入两套 `UIContext` / `UIErrorCode`（已知冲突）。

## 2. 当前仍 include `../ui/` 的生产代码

（相对仓库根；扫描 `src/**` 且排除 `src/ui/**`）

| 消费者 | 依赖的 Legacy UI 表面（代表） |
| --- | --- |
| `src/engine/Application.cpp` | `TextRenderer`、`UIConstants` |
| `src/engine/EventSystem.{hpp,cpp}` | `NodeId`、`UIContext`、`UINode` |
| `src/engine/Scene.{hpp,cpp}` | `UICore`、`UINode`、`UILayoutManager`、`UIConstants` |
| `src/game/GameScene.{hpp,cpp}` | `UICore`、`UIToolbar`、`UICharacterPanel`、`UIConstants` |
| `src/game/MenuScene.{hpp,cpp}` | `UICore`、`UINode`、`UIComponents`、`UIConstants`、`UIUtils` |
| `src/game/PauseScene.{hpp,cpp}` | 同上 + `UILayoutContainers` |
| `src/game/SettingsScene.{hpp,cpp}` | 同上 |
| `src/game/WorldSelectScene.{hpp,cpp}` | `UICore`、`UIComponents`、`UIListView`、`UIDialog`、`UITextEdit`、`UIUtils` |
| `src/game/Smoke3DScene.cpp` | `UIConstants` |
| `src/renderer/Pipeline.hpp` | `UIConstants`（view 常量） |

**零引用未达成。** 删除 `src/ui` 会直接破坏 Legacy `Tina` 产品与 smoke。

## 3. vNext UI 已覆盖 vs Legacy 仍独有

| 能力 | vNext 状态 | Legacy 状态 |
| --- | --- | --- |
| Retained tree / layout dirty / hit / paint SolidFill | 已有（`tina_ui_tests`） | 完整产品路径 |
| Button / Checkbox / Slider + Action | 产品 sample 已用 | 完整 |
| Text + FreeType 可选 | 有 rasterizer/path | TextEdit/中文产品路径成熟 |
| TextEdit / IME 候选 | 未等价 | **有** |
| ScrollView / VirtualList / Dialog / Toolbar | 未等价 | **有** |
| ListView / CharacterPanel | 未等价 | **有** |
| Scene 栈上的菜单/设置/暂停整页 | 未替代 | **产品入口** |

vNext 文档约束：Retained UI 用于游戏内 HUD/菜单，**不是**桌面编辑器工具包；完整编辑器不应用 UI 直接扛。

## 4. 进入「删除 Legacy UI」的前置门禁（严格并集）

在动刀 `src/ui` 或 `TINA_BUILD_LEGACY` 之前，**全部**满足：

1. **产品替代**：Menu/Game/Settings/Pause/WorldSelect 的 UI 行为有 vNext（或独立工具进程）等价路径与验收，**或**这些场景已退役且不再是产品门禁。
2. **零 include/link/call**：上表消费者对 `src/ui` 与 Legacy UI symbol 为 0。
3. **vNext 2D/UI 产品门禁**：`tina_sample_2d`（及清单 G1/G2）在约定 feature 图上稳定 300 帧。
4. **Legacy 四条 smoke 最后一次基线**（删除前）：`--smoke-frames` / `--smoke-ui` / `--smoke-game` / `--smoke-3d`（以 `docs/building.md` 为准）exit + 画面 + 资源证据分别保留。
5. **删除后**：Windows/Linux 构建与直接 GoogleTest；vNext 等价 UI 门禁；无 `#if 0` / 空 alias / disabled test 残留。
6. **独立可回滚提交**，不夹带新功能；Accepted ADR 不删历史理由。

**仅完成 3D E9 或 vNext HUD 切片 ≠ 可删 Legacy UI。**

## 5. 推荐推进顺序（删 UI 专用）

| 阶段 | 工作 | 本轮状态 |
| --- | --- | --- |
| A | 盘点消费者与能力缺口（本文） | **Done** |
| B | 用 vNext UI 覆盖仍被产品依赖的控件/页面（按真实缺口切片） | Open |
| C | Legacy 场景改为 vNext 入口或退役场景 | Open |
| D | 零引用扫描 + 测试迁移 | Open |
| E | Legacy 四条 smoke 最终基线 | Open |
| F | **仅 UI 相关**源码/CMake 删除提交（仍可能保留其它 Legacy） | Blocked |
| G | 整包 Legacy 删除（M12 全文） | Blocked |

## 6. 本轮明确动作

- **不删除** `src/ui/**`、不关 `TINA_BUILD_LEGACY`、不改 Legacy 场景去 UI。
- 同步 [m12-gate-checklist.md](m12-gate-checklist.md) 指向本文。
- 若继续推进「删 Legacy UI」：优先 **B/C**（替代实现），不是 F。
