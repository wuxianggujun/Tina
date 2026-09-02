---
name: develop-tina-ui
description: 实现、重构或排查 Tina C++23 Retained UI，包括 Element authoring、树与固定容量事务、布局/内容放置、Focus/Modal/Pointer Capture、控件与虚拟集合、Text/IME、semantics/accessibility、FreeType/UIA adapter 和 UI-Render 桥。Use when editing include/tina/ui、src/ui、tests/ui、tests/ui_freetype、tests/ui_uia、tests/runtime_ui、tests/ui_render、docs/ui.md、ADR 0011 或 ADR 0022。
---

# 开发 Tina Retained UI

## 建立事实

先执行 `git status --short` 并保留用户改动。阅读 `docs/ui.md`、`docs/design-freeze.md`、
ADR 0011、ADR 0022、相关公开头/实现/测试与模块 `CMakeLists.txt`。完成度以源码、target 和实际结果为准，
不要从旧设计段落推断当前能力。

Tina 只有一套产品 UI：`include/tina/ui/**` + `src/ui/**`。Legacy UI 已删除，不得恢复第二套 UI ABI。
当前已有 Element composition、Focus Scope/Modal/持久 Pointer Capture、ScrollView、Dropdown/Popup、
虚拟 ListView/TreeView、Windows UIA 属性/fragment/control pattern/action；开放项见 `docs/backlog.md`。
`TinaEditor` 是最大的内部 UI consumer：它显式放宽 node/paint/display-list 容量，并用大量 axis-aligned
SolidQuad 阶梯折线近似 3D 视口斜线（UI 层暂无旋转矩形/线段图元，正式方案见 backlog `RENDER-LINES-001`）。

## 所有权与事务

- `UIContext` owner-thread、固定容量 PMR；`UIRootOwner` move-only，node 使用 Window owner + generation。
- Tree mutation 与 structure/layout/hit/paint/semantics 分阶段事务提交；失败保留旧 committed snapshot。
- route 读取上一份 committed hit；route 不隐式 layout，本帧 UI commit 从下一帧参与命中。
- committed view 到下一次对应 commit 或 Context 析构失效；builder/updater/facade 到当前 phase 返回失效。
- 结构发布、subtree destroy、layout/hit/paint snapshot 构建保持非递归；50,000 层专项 gate 防止栈溢出。
- Popup/Modal 的最终 hit/paint 顺序必须稳定，不通过每节点祖先回溯重新引入平方复杂度。

## Element、布局与 Theme

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
- 自定义 Canvas command 必须 backend-neutral、复制进 Context 固定容量 storage；已冻结的 kind 为
  `SolidRect`、`Image`、`NineSlice`、`SolidEllipse`、`SolidLine`（`UICanvasCommandKind`，
  见 `include/tina/ui/UIPaint.hpp:205`），不开放 GPU callback、不接受任意几何。

ADR 0022 已实现 Layout/Content、完整 `UIElementDescriptor` authoring、官方内建 recipes、统一
`createElement()`、组合 Semantics、StyleRole/属性 override reset、固定预算 multi-node transaction、
固定容量 `SolidRect` Canvas、Showcase Flow/Flex 层级，以及所有 create 失败的节点/text/canvas 回滚。
公开 `UIWidgetKind` 与 create-by-kind API 已删除；私有 `BuiltinElementKind` 只负责既有控件 storage/行为分派。

**已实现**：`Image`/`NineSlice` Canvas command 与 retained image source（`UIImage.hpp`、
`UIImageSource.hpp`；Showcase 门禁强制 `imageProducts=5`、`imageAtlasUploaded/Released=true`）；
逐角圆角 authoring `UILogicalCornerRadii`（`UIBoxPaint::cornerRadii`、`UICanvasCommand::cornerRadii`，
门禁 `asymmetricCornerProducts=3`，UI-PAINT-002-A）；可组合 stylesheet
`UIStyleController::installStyleSheet()`（门禁 `stylesheetInstalled=true`、`styleTokenUpdates>=3`）。

**仍未实现**：**圆角/stencil 子树 clip** 与 backdrop/blur（backlog `UI-PAINT-002` Deferred）。逐角半径只
影响该 box 自身 chrome；后代 clip 与 `clipDescendants` 仍是 axis-aligned（`UIPaint.hpp:140`、
`UILayout.hpp:452`）。不要把逐角半径当成后代 clip，也不要把当前 Canvas 描述成任意 GPU 绘制或完整
CSS/Widget 扩展系统。

## 输入、焦点与控件

- 路由顺序固定为 Capture -> Target -> Bubble；consume、prevent-default、gameplay claim 含义分开。
- 持久 Pointer Capture 在 Up/cancel/destroy/disable/Hidden/Collapsed/Modal scope change 时精确释放；
  synthetic cancel 沿原 committed ancestry 发送。
- Modal barrier、Focus Scope 与 focus restore 使用 committed 状态；提交失败不能泄漏半份 focus/capture。
- 默认行为复用控件状态机；UIA、键盘、Gamepad 与 Pointer action 不建立平行的业务实现。
- ListView/TreeView 使用固定 row pool、stable item key 和 datasource；warmup 后稳态操作不得增长 storage。

## Text、Render 与 accessibility

- 所有公开文本严格 UTF-8；MSVC target 保持 `/utf-8`。FreeType 私有，字体通过 `TINA_UI_FONT_PATH`
  或显式字节注入；placeholder 不能冒充 CJK 视觉通过。
- TextEdit 默认是单行 scalar selection；`UITextEditMultilineConfig` 可启用 LF/soft-wrap、固定容量 visual rows、
  滚动、二维 hit/navigation。selection/caret 仍 scalar-indexed，但编辑、删除、导航位置对齐无第三方依赖的
  UAX #29 grapheme 子集。Windows GLFW 已有 logical caret → DPI-scaled client pixels 与 IMM32
  candidate/composition placement；BiDi/复杂 shaping、Linux 原生 XIM/Wayland 和 Windows 真机人工 IME 证据仍属
  `TEXT-001` 后置范围。
- UI 只输出后端无关 committed paint/DisplayList/Glyph atlas，不调用 bgfx。
- `UIAccessibilityAction` 只表达 Focus/Invoke/Toggle/SetRangeValue/SetTextValue；Context owner thread 验证后
  复用默认行为。Windows UIA adapter 负责 COM/线程 marshal，不直接写 retained storage。
- `RunUi002UiaGate.ps1` 证明外部进程发现和 action；Narrator/Inspect 人工金标与 Linux AT-SPI 是独立证据。

## 验证路由

按改动选择 `tina_ui_tests`、`tina_runtime_ui_tests`、`tina_ui_render_integration_tests`、
`tina_ui_freetype_tests`、`tina_ui_uia_tests` 与 UI showcase/product gate。直接运行 executable，
视觉、结构化 JSON、UIA 外部 client 与 screen reader 人工证据分别报告。具体命令使用
`$build-and-test-tina` 与 `docs/testing.md`。无字体时 FreeType 相关用例应 skip。

静态收口至少检查旧 create-by-kind 成员调用和旧布局字段零命中、Showcase 普通页面无 Overlay，并运行
`git diff --check`。公开头新增契约同时补 header-isolation translation unit；第三方 token 仍必须留在
PRIVATE adapter。
