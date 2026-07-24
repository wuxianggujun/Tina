# Retained UI

当前产品 UI 位于 `include/tina/ui` 与 `src/ui`。旧 Legacy UI 产品图已删除，但这两个目录是 vNext
正式实现，不得作为 Legacy 残留移除。架构决策见 [ADR 0011](adr/0011-retained-ui.md)和
[ADR 0021](adr/0021-runtime-ui-startup-capability.md)。

## 当前能力

| 领域 | 已实现 |
| --- | --- |
| 所有权 | per-window `UIContext`、generation/owner-aware `UINodeId`、move-only `UIRootOwner` |
| Tree | Root/Panel/Label/Button/Checkbox/Slider/ProgressBar/RadioButton/TextEdit，固定容量 mutation |
| Layout | Flex-lite measure/arrange、logical pixel、事务 commit、clean-subtree reuse |
| Hit/route | committed hit snapshot、Capture→Target→Bubble、listener token、consume/prevent/claim |
| Paint | box/text/control paint、clip、PaintCache、committed paint snapshot |
| Theme（A/B） | `UITheme` token + `makePanelBoxPaint`：surface 色阶、1px 亮/暗边、可选 Low elevation 假影；hex helper `rgb`/`argb`；**无**圆角/毛玻璃/CSS Theme（C） |
| Text | strict UTF-8、可选 FreeType rasterizer、R8 Glyph atlas、DisplayList Glyph |
| Input | Pointer default action、Tab focus、Keyboard/Gamepad activation、TextEdit edit/selection/IME |
| Semantics | role/name/checked/selected/range/value/valueText/focused snapshot |
| Runtime | startup root builder、phase-scoped tree updater、DisplayList/Glyph atlas handoff |
| Product | product-2d HUD、设置控件、TextEdit、65% ProgressBar、RadioButton 组与 Windows 视觉证据 |

## 所有权与句柄

`UIContext::Create(window, capacities, resource)` 在创建时固定 node/root/listener/layout/paint/text/semantics
等 storage 容量。`UINodeId` 同时校验语义 owner WindowId、registry owner、slot 与 generation；stale、
cross-context、cross-window ID 必须失败。

`UIRootOwner` 是 root 的唯一 RAII owner。销毁 root 会使其子树 ID 失效；listener token 不保活 Context/
root。产品 State 在退出时应先 reset listener，再 reset root。Context 只能在 owner thread mutation/commit。

Runtime 私有持有主窗口 UIContext；普通游戏不取得裸 `UIContext*`：

- `GameStateEnterContext::primaryWindowUIRootBuilder()` 只在 `onEnter()` 当前 epoch 有效；
- `UIUpdateContext::primaryWindowUITreeUpdater(root)` 只在 `updateUI()` 当前 epoch 对该 root 有效；
- facade 的第一次失败成为 phase sticky error，回调结束后统一返回；
- builder/updater、span 和 committed view 不得跨 phase 保存。

## Tree 与事务提交

Tree mutation、layout、hit、paint 与 semantics 都有固定容量和明确 commit。失败不能发布半份 snapshot；
下一次成功 commit 才替换旧 view。UI input route 读取上一帧 committed hit snapshot，本帧 `updateUI()`
后的 layout/paint 在 Render 前提交，并从下一帧开始参与命中。

Layout 使用窗口 logical extent，不直接读取 framebuffer pixel。content scale/resize 更新 layout size，但同一
WindowId 不重建 Context。clean-subtree measure/arrange reuse 已实现；完整 dirty-range pruning 与大型虚拟
列表尚未实现。

## 输入与默认行为

Pointer route 顺序为 Capture → Target → Bubble。listener 可以分别：

- stop propagation；
- prevent default action；
- consume 当前 transition，阻止 Gameplay Action；
- claim held continuous control，直到真实 release/reset。

这些语义互不隐式替代。cancel/reset 清理 pressed/armed/focus/edit state，不伪造普通 Up，也不允许同帧
失败后重放已经发生的 callback 副作用。

当前默认行为：

- Button：primary pointer pressed/action，Enter/Space/KeypadEnter/Gamepad South activation；
- Checkbox：Pointer/Keyboard/Gamepad toggle；
- Slider：Pointer drag、range/value clamp；
- RadioButton：同一直接父节点互斥，Pointer/Keyboard/Gamepad selection；
- TextEdit：Pointer focus/selection，Tab traversal，Left/Right/Home/End、Backspace/Delete、Shift selection、
  Ctrl+A、committed text 与 IME；
- ProgressBar：非交互 determinate range/value，hit policy 为 Ignore。

当前是窄线性 focus/default-action 模型并有 focused visual。通用 Focus Scope、Modal、持久 Pointer
Capture、空间/方向导航与多 root transition 恢复仍由 `UI-004` 跟踪。

## Text、UTF-8 与 IME

所有 UI 文本是 strict UTF-8，无 embedded NUL；MSVC target 使用 `/utf-8`。TextEdit 当前为单行，拒绝
CR/LF，selection/caret 按 Unicode scalar index 维护，不把 UTF-8 byte offset 暴露给游戏。

文本路径：

```text
Label/Button/RadioButton/TextEdit text
  -> text measure/layout
  -> Glyph placement
  -> UI-owned R8 atlas
  -> committed paint
  -> Render UIDisplayList Glyph command
  -> private bgfx atlas texture + textured UI pass
```

FreeType 是可选私有 rasterizer。字体 fixture 优先由 `TINA_UI_FONT_PATH` 注入；未加载字体时 placeholder
路径不能冒充 CJK 视觉通过。Windows GLFW adapter 已提供 IMM32 preedit/commit/cancel；Linux 当前只保证
committed text。多行、grapheme、BiDi/shaping、候选窗定位见 `TEXT-001`。

## Semantics 与 accessibility

`committedSemantics()` 当前发布 Label、Button、Checkbox、Slider、ProgressBar、RadioButton、TextEdit：

- role/name；
- checked/selected；
- min/max/value；
- TextEdit valueText；
- 有限 focused state。

这是后端无关 semantics snapshot，不等同于平台辅助技术。

`UIAccessibilityTree` / `IUIAccessibilityProvider` / `UIAccessibilityProbeProvider`（UI-002 首切片）
从 `committedSemantics()` 构建可查询的无障碍节点表（role/name/state/range/value），供后续 Windows
UIA 与 Linux AT-SPI adapter 消费。`UIUpdateContext::committedSemantics()` 与
`PrimaryWindowUICapabilityState::committedSemantics` 暴露同一快照；`tina_sample_2d` 每帧
`updateUI` 经 probe 发布并输出 `accessibility*` JSON 证据。Probe 可验证 stale node 拒绝；
**真实 screen reader / UIA 进程桥接仍未实现**，不得把 probe/sample 字段写成真机 a11y 通过。

## Render 边界

UI 不调用 bgfx。`tina_ui_render_integration` 把 committed paint 转为固定容量 `UIDisplayList`，Runtime
在 `RenderFrame` 中只借用 DisplayList 和可选 R8 atlas page。backend 必须在 `submitFrame()` 内同步
消费。

当前支持 solid/glyph quad 与 axis-aligned scissor。Runtime `RenderFramePacket`/FramePin 首切片已落地
（Null 同步 complete）。rounded/stencil clip、Image widget 与跨 GPU/DPI golden（UI-003）尚未完成。

## 实际绘制链路

UI 是 Retained UI：游戏代码先创建节点并修改属性，Runtime 在一帧内提交一次布局；绘制和命中都读取
同一份已提交快照，不在 `updateUI()` 回调里直接调用 bgfx。当前主窗口的顺序是：

```text
IGameState::onEnter / updateUI
  -> UIRootOwner + UITreeUpdater 修改节点树
  -> UIContext::commitLayout(logical extent)
  -> Measure / Arrange
  -> committed layout + hit + paint + semantics snapshots
  -> UICommittedPaintView
  -> tina_ui_render_integration::buildUIDisplayList
  -> logical pixels 映射到 framebuffer pixels、裁剪、相邻 batch 合并
  -> UIDisplayList SolidQuad / Glyph commands
  -> bgfx transient vertex/index buffer
  -> UI textured shader + scissor + premultiplied alpha
  -> RenderDevice::submitFrame 后显示
```

`UIContext::buildCommittedPaint()` 按 paint order 遍历可见节点。普通 `UIBoxPaint` 生成一个矩形 entry；
文字生成 Glyph entry；ProgressBar 追加按 value 缩短的 foreground，RadioButton 追加 indicator 和
选中内块，TextEdit 在焦点状态下追加 selection highlight、IME preedit 和 caret。Integration 再把
逻辑坐标投影为像素矩形，并丢弃空/透明/完全在 clip 外的 entry。

Solid 和 Glyph 共用一套带 UV 的 UI shader：SolidQuad 绑定 1×1 白色 R8 纹理，采样值恒为 1；Glyph
绑定 UIContext 持有的 R8 atlas，采样灰度作为 coverage。片元颜色是顶点 premultiplied 颜色乘 coverage，
backend 对每个 clip batch 设置 bgfx scissor，并使用 `ONE, INV_SRC_ALPHA` 混合。UI 模块本身不依赖
bgfx；这条依赖只存在于 `tina_ui_render_integration` 和私有 bgfx backend。

## 控件绘制矩阵

| 控件 | 语义/交互 | 当前实际绘制 |
| --- | --- | --- |
| `Root` | 树和所有权边界 | 默认不绘制；设置 `UIBoxPaint` 后也可作为背景 SolidQuad |
| `Panel` | 容器和布局 | `UIBoxPaint` 的 SolidQuad；当前 effective clip 是 viewport 与自身矩形的交集 |
| `Label` | 只读 UTF-8 文本 | Glyph quads；没有可用字体时为确定性的 placeholder SolidQuad |
| `Button` | Pointer、Tab、Enter/Space/KeypadEnter、Gamepad South | `UIBoxPaint` 背景 + 可选 `UIButtonPaint` 状态色 + 文本 |
| `Checkbox` | checked 切换，复用 Button action/焦点路径 | 背景 SolidQuad + `UICheckboxPaint` 勾选指示块 + 可选文本 |
| `Slider` | Pointer 横向拖动，min/max/value/step | 背景 track + `UISliderPaint` filled track/thumb（拖动 thumb 色） |
| `TextEdit` | 单行编辑、选择、光标、IME | 文本 Glyph/placeholder + selection highlight + caret SolidQuad |
| `ProgressBar` | 非交互 determinate range/value | track SolidQuad + 按比例缩短的 foreground SolidQuad |
| `RadioButton` | 同直接父节点互斥选择 | indicator SolidQuad + 选中内块 + 文本 Glyph |

控件创建入口集中在 `UIRootBuilder`/`UITreeUpdater`；属性 setter 只修改 retained 状态并标记必要的
dirty 类别。`UITheme` 提供薄 token 与 panel chrome helper；`UIBoxPaint` 仍是 escape hatch，并可携带
borderLight/borderDark/borderWidth 与 shadow（假 elevation）。rounded rectangle、Image widget、
毛玻璃与完整 CSS 式 Theme 仍未实现（Phase C）。Button/Checkbox/Slider 的控件几何仍由专用 paint
与调用方参数决定，可用 theme token 填色。

## 产品接入与证据

`tina_sample_2d` 当前 UI 包含：

- HUD Label/Button；
- Master/Music/SFX Checkbox/Slider；
- profile-name 单行 TextEdit；
- 65% ProgressBar；
- Windowed/Fullscreen RadioButton 组。

300帧 product-2d JSON 验证控件创建、TextEdit UTF-8 初值、ProgressBar value 与 Radio 互斥 selection。
`artifacts/screenshots/sample-2d-product/20260723-013100/report.json` 记录 `ok=true`、exit 0、schema 3，
3次 960x540 client capture 中2帧稳定非空；首次 `PrintWindow` 白帧由 `blankLike=true` 排除。
人工复核 `frame-02.png` / `frame-03.png` 中上述控件可见、中文正常且无裁剪或重叠；两帧 65% fill
均为 x=700..842（143 px），选中色只出现在 Windowed RadioButton，client capture 未混入标题栏。

当前 tip 增量验证为：`tina_ui_tests` 255/255（含 Accessibility）、`tina_runtime_ui_tests` 83/83、
`tina_ui_render_integration_tests` 12/12、product-2d 图的 `tina_ui_freetype_tests` 2/2。UI 容量回归
覆盖 Checkbox/Slider mutation、TextEdit pointer selection 和需要同时重绘旧/新节点的 focus step；
dirty queue 容量不足时状态与 callback 原子不变，同文本替换 selection 仍发布新 paint。数字是当前
工作树证据，不是架构永久基线。

## 验证

```powershell
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_ui_render_integration_tests.exe --gtest_color=yes
```

FreeType、bgfx 和 product-2d 需要对应 feature 图；完整命令见 [测试说明](testing.md)。

## 后续任务

| ID | 范围 |
| --- | --- |
| `UI-002` | Windows UIA / Linux AT-SPI 真机 adapter（中立 SPI + sample JSON 已有） |
| `UI-003` | 跨 DPI/GPU 容差视觉门禁（映射单测 + 单机 ROI + baseline 回归已有；多 DPI 截图矩阵后置） |
| `UI-004` | 通用 Focus Scope、Modal、持久 Pointer Capture |
| `UI-005` | ScrollView、虚拟 ListView、Dropdown、TreeView |
| `TEXT-001` | 多行 TextEdit、grapheme/shaping、候选窗定位 |
| （可选） | Phase C：圆角 clip、backdrop blur、完整 style resolver |

ProgressBar/RadioButton 的产品接入 `UI-001` 已完成，不应重新列为 Planned。
Theme A/B（token、panel 边、Low 假影、sample 改 token）已在产品 sample 路径落地；不要把 UI-002/003
标成 Done。
