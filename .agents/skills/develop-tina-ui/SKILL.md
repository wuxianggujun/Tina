---
name: develop-tina-ui
description: 实现、重构或排查 Tina 的 Retained UI，包括 Element authoring、节点所有权、布局/内容放置、focus/capture、事件路由、输入消费、控件、中文字体和 UI 渲染。Use when editing src/ui, include/tina/ui, tests/ui, tests/ui_freetype, docs/ui.md, ADR 0011, or ADR 0022.
---

# 开发 Tina UI

## 唯一产品轨道

Tina **只有一套** 产品 UI：`include/tina/ui/**` + `src/ui/**`（Retained）。  
Legacy UI / `Tina.exe` 产品图已删除；禁止再引入第二套 UI ABI。

| 项 | 路径 / 事实 |
| --- | --- |
| 公开头 | `include/tina/ui/**` |
| 实现 | `src/ui/**`（可选 `src/ui/freetype`） |
| 测试 | `tests/ui`、`tests/ui_freetype`、`tests/runtime_ui`、`tests/ui_render` |
| Context / ID | `UIContext`、`UINodeId`（owner WindowId + generation） |

先读：`docs/ui.md`、`docs/adr/0011-retained-ui.md`、
`docs/adr/0022-ui-element-authoring-and-layout.md`、相关 `include/tina/ui/*.hpp`、`src/ui/**`、对应测试
CMakeLists。

## 约定

- owner-thread、固定容量 PMR、move-only root owner。
- 布局 / hit / paint 用 committed snapshot；route 不隐式 layout。
- Game SDK 不暴露 FreeType/bgfx；字体字节由游戏或可选 fixture 注入。
- FreeType 字体路径：`TINA_UI_FONT_PATH`（CMake/env）或可选 `resources/fonts/*.otf`（见 `cmake/TinaUiFont.cmake`）。
- 公开创建统一使用 `createElement(parent, descriptor)`；内建控件通过 `make*Element()` recipe authoring。
  不恢复 `createPanel/createButton/createListView/...` 成员 API，也不增加 compatibility alias。
- 布局严格区分 `flexContainer`（父容器解释）、`flexItem`（父容器为子项解释）与
  `placement=Flow/Overlay`。普通产品页面用 Flow/Flex；Overlay 只用于真正叠放，Popup 仍走 anchor policy。
- 控件内部文字使用 `UIContentAlignment`；paint、caret/selection、Pointer-to-codepoint 必须读取
  `UICommittedContentPlacement`，不得各自从 `worldRect + padding` 重算。
- Semantics 由 descriptor 显式声明 `Automatic/Publish/MergeDescendants/Exclude`、role/name/description/actions；
  accessibility action 只能执行 committed semantics 已发布的 action。
- Theme 选择使用 `UIStyleRoleId`，局部 setter 只 detach 对应属性；`clearOverride()` 恢复当前 role recipe。
- 多节点组件用固定 node budget 的 `UIElementBuildTransaction`；失败或析构回滚整棵子树及 text/canvas storage，
  active transaction 期间禁止 structure/layout commit。
- 自定义 Canvas command 必须 backend-neutral、复制进 Context 固定容量 storage；当前只冻结 `SolidRect`，
  不开放 GPU callback。

## ADR 0022 当前边界

已经实现：Layout/Content、完整 `UIElementDescriptor` authoring、官方内建 recipes、统一
`createElement()`、组合 Semantics、StyleRole/属性 override reset、固定预算 multi-node transaction、
固定容量 `SolidRect` Canvas、Showcase Flow/Flex 层级，以及所有 create 失败的节点/text/canvas 回滚。
公开 `UIWidgetKind` 与 create-by-kind API 已删除；私有 `BuiltinElementKind` 只负责既有控件 storage/行为分派。

尚未实现：RoundedRect/Image/NineSlice 等更宽 Canvas command、圆角 clip 与可组合 stylesheet；不要把
当前第一阶段 Canvas 描述成任意 GPU 绘制或完整 CSS/Widget 扩展系统。

## 验证

```powershell
cmake --build --preset windows-vnext-bgfx-debug --target tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes
```

有 FreeType 时再编/跑 `tina_ui_freetype_tests`；无字体则相关用例应 skip。

静态收口至少检查旧 create-by-kind 成员调用和旧布局字段零命中、Showcase 普通页面无 Overlay，并运行
`git diff --check`。公开头新增契约同时补 header-isolation translation unit；第三方 token 仍必须留在
PRIVATE adapter。
