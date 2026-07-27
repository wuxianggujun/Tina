# Retained UI

当前产品 UI 位于 `include/tina/ui` 与 `src/ui`。旧 Legacy UI 产品图已删除，但这两个目录是 vNext
正式实现，不得作为 Legacy 残留移除。架构决策见 [ADR 0011](adr/0011-retained-ui.md)和
[ADR 0021](adr/0021-runtime-ui-startup-capability.md)。

## 当前能力

| 领域 | 已实现 |
| --- | --- |
| 所有权 | per-window `UIContext`、generation/owner-aware `UINodeId`、move-only `UIRootOwner` |
| Tree | Root/Panel/Modal/Label/Button/Checkbox/Slider/ProgressBar/RadioButton/TextEdit，固定容量 mutation |
| Layout | Flex-lite measure/arrange、logical pixel、事务 commit、clean-subtree reuse |
| Hit/route | committed hit snapshot、Capture→Target→Bubble、持久 Pointer Capture、Modal barrier、listener token、consume/prevent/claim |
| Paint | box/text/control paint、clip、PaintCache、committed paint snapshot |
| Theme（A/B） | `UITheme` token + `makePanelBoxPaint`：surface 色阶、1px 亮/暗边、可选 Low elevation 假影；hex helper `rgb`/`argb`；**无**圆角/毛玻璃/CSS Theme（C） |
| Text | strict UTF-8、可选 FreeType rasterizer、R8 Glyph atlas、DisplayList Glyph |
| Input | Focus Scope/显式 focus、Pointer capture/cancel、Tab focus、Keyboard/Gamepad activation、TextEdit edit/selection/IME |
| Semantics | role/name/checked/selected/range/value/valueText/focused snapshot，Modal 映射 Dialog role |
| Runtime | startup root builder、phase-scoped tree updater、DisplayList/Glyph atlas handoff |
| Product | 独立 13 控件 showcase（Dark/Light 实时换肤）、product-2d HUD/设置面板、product-3d Scene Controls 与 Windows 视觉证据 |

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

`UILayoutStyle::padding` 同时参与 measure/auto-size 与 content origin：Label、Button、TextEdit、
RadioButton 的文字从 `left/top` padding 后开始绘制，多行文字换行回到同一 padded x；TextEdit 的
pointer-to-caret 命中也以 padded 文本起点计算。`right/bottom` padding 参与固有尺寸，但不额外移动文字。

## 输入与默认行为

Pointer route 顺序为 Capture → Target → Bubble。listener 可以分别：

- stop propagation；
- prevent default action；
- consume 当前 transition，阻止 Gameplay Action；
- claim held continuous control，直到真实 release/reset；
- `capturePointer()` / `releasePointerCapture()` 改变后续 transition 的 routed target，同时保留物理
  `pointQuery.target` 供 drag/drop、hover 与边界判断使用。

这些语义互不隐式替代。Primary Up 是 capture release barrier。capture target 被禁用、销毁、Hidden/
Collapsed，或因新 Modal 离开 committed scope 时，UI 沿原 committed ancestry 合成一次
`PointerCancel` 再释放；该 kind 只允许 listener 注册，外部 `routePointerInput()` 不能伪造。输入流
cancel/reset 同样清理 pressed/armed/focus/edit state，不伪造普通 Up，也不允许同帧失败后重放已经发生的
callback 副作用。

当前默认行为：

- Button：primary pointer pressed/action，Enter/Space/KeypadEnter/Gamepad South activation；
- Checkbox：Pointer/Keyboard/Gamepad toggle；
- Slider：Pointer drag、range/value clamp；
- RadioButton：同一直接父节点互斥，Pointer/Keyboard/Gamepad selection；
- TextEdit：Pointer focus/selection，Tab traversal，Left/Right/Home/End、Backspace/Delete、Shift selection、
  Ctrl+A、committed text 与 IME；
- ProgressBar：非交互 determinate range/value，hit policy 为 Ignore。

`UI-004` 已完成：`UIFocusScopeMode::Contain` 将 Tab/Shift+Tab 限制在 committed scope；显式
`requestFocus()` 拒绝未提交、隐藏、disabled、非 Targetable、非键盘控件及 active Modal 外节点。
最后绘制的 committed visible Modal 是 active Modal，屏蔽下层 hit/Tab 并消费 backdrop 输入；嵌套 Modal
逐层保存/恢复 focus，跨 root Modal 释放后也恢复原 root 的有效焦点。commit 的 Modal/focus/capture
变化与 paint/semantics 一起事务发布，失败提交不发送 `PointerCancel`、不释放旧 capture。空间/方向导航
仍未实现。

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

`committedSemantics()` 当前发布 Modal、Label、Button、Checkbox、Slider、ProgressBar、RadioButton、TextEdit：

- role/name；
- checked/selected；
- min/max/value；
- TextEdit valueText；
- 有限 focused state；
- Modal 的 Dialog role。

这是后端无关 semantics snapshot，不等同于平台辅助技术。

`UIAccessibilityTree` / `IUIAccessibilityProvider` / `UIAccessibilityProbeProvider`（UI-002-SPI）
从 `committedSemantics()` 构建可查询的无障碍节点表（role/name/state/range/value）。
`UIUpdateContext::committedSemantics()` 与 `PrimaryWindowUICapabilityState::committedSemantics`
暴露同一快照；`tina_sample_2d` 每帧 `updateUI` 经 probe 发布并输出 `accessibility*` JSON 证据。
Probe 可验证 stale node 拒绝。

**Windows UIA 私有 adapter（UI-002，可选）：** `TINA_BUILD_UI_UIA=ON`（Windows-only）时构建
`tina_ui_uia` 并让 `tina_runtime` 在 WindowSurface 产品路径上自动接线：

1. 属性映射：`UIAccessibilityTree` → UIA 形 ControlType/Name/Enabled/Focus/Range/Toggle/Value；
2. **HWND 桥**：`WindowsUiaHostBridge`（`SetWindowSubclass` + `WM_GETOBJECT` +
   `IRawElementProviderSimple` 根/子 fragment）；公开工厂头仍零 COM；
3. **产品自动 attach**：`EngineHost` 从主窗口 lease 解码 Win32 HWND，startup layout 与每帧
   `commitForFrame` 后 `rebuildFrom(committedSemantics)` 并 `publish`；shutdown 时 detach。

`tina_ui_uia_tests` 覆盖映射、provider 与 HostBridge attach/navigate。**Narrator/Inspect 人工金标与
Linux AT-SPI 仍后置**；不得把单测写成「真机 screen reader 合规已过」。

## Render 边界

UI 不调用 bgfx。`tina_ui_render_integration` 把 committed paint 转为固定容量 `UIDisplayList`，Runtime
在 `RenderFrame` 中只借用 DisplayList 和可选 R8 atlas page。backend 必须在 `submitFrame()` 内同步
消费。

当前支持 solid/glyph quad 与 axis-aligned scissor。Runtime `RenderFramePacket`/FramePin 的
present-return CPU completion 已落地
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
| `Modal` | committed Focus/Input scope、下层输入 barrier、Dialog semantics | Theme surface chrome；布局/内容由 retained 子树组合 |
| `Label` | 只读 UTF-8 文本 | Glyph quads；没有可用字体时为确定性的 placeholder SolidQuad |
| `Button` | Pointer、Tab、Enter/Space/KeypadEnter、Gamepad South | `UIBoxPaint` 背景 + 可选 `UIButtonPaint` 状态色 + 文本 |
| `Checkbox` | checked 切换，复用 Button action/焦点路径 | 背景 SolidQuad + `UICheckboxPaint` 勾选指示块 + 可选文本 |
| `Slider` | Pointer 横向拖动，min/max/value/step | 背景 track + `UISliderPaint` filled track/thumb（拖动 thumb 色） |
| `TextEdit` | 单行编辑、选择、光标、IME | 文本 Glyph/placeholder + selection highlight + caret SolidQuad |
| `ProgressBar` | 非交互 determinate range/value | track SolidQuad + 按比例缩短的 foreground SolidQuad |
| `RadioButton` | 同直接父节点互斥选择 | indicator SolidQuad + 选中内块 + 文本 Glyph |

控件创建入口集中在 `UIRootBuilder`/`UITreeUpdater`；属性 setter 只修改 retained 状态并标记必要的
dirty 类别。

**产品 Theme（默认皮肤 + 全局换肤 + 局部覆盖）：**

- `UIContext` 持有 `productTheme()`，默认 `makeDefaultProductTheme()`；
- `create*`（Button/Checkbox/Slider/TextEdit/ProgressBar/RadioButton）与 Label 文本样式在创建时
  **自动 apply** 对应 `make*Chrome` / text style；Root/Panel 默认无底色（容器），需背景时用
  `makePanelBoxPaint` / `makeSettingsPanelChrome`；
- `setProductTheme(theme)` 会校验 metric，并事务式重绑所有仍继承产品 Theme 的既有控件属性；容量、
  文本测量或线程校验失败时，Theme 与控件属性均保持不变；之后新建的节点继承最新 Theme；
- 局部覆盖按属性分离：`setBoxPaint` / `set*Paint` / `setTextStyle` 只让对应属性脱离后续全局换肤，
  同一控件上未覆盖的其他属性仍会跟随 Theme；即使 setter 写入当前相同值，也视为显式局部覆盖；
- Runtime 游戏通过 phase-scoped `PrimaryWindowUITreeUpdater::productTheme()` / `setProductTheme()` 换肤，
  不取得裸 `UIContext`；
- 默认 Button chrome 使用 Low elevation 双边框与阴影；pressed 状态收拢阴影并反转双边框，focus 使用
  独立边框色，因此 hover / pressed / focused / disabled 具有可辨识层次；
- 另提供 `makeLightProductTheme()` 与完整 chrome 工厂（`makeButtonChrome` 等）。

`UIBoxPaint` 仍是 escape hatch，并可携带 borderLight/borderDark/borderWidth 与 shadow（假 elevation）。
rounded rectangle、Image widget、毛玻璃与 CSS 式 stylesheet 仍未实现（Phase C）。

## 产品接入与证据

`tina_sample_ui_showcase` 是控件与换肤的独立工作台，固定 1280×720 logical extent，同屏展示：

- Primary、destructive、disabled 与 reset Button；
- Checkbox、Slider→ProgressBar 联动、UTF-8 TextEdit；
- Performance/Balanced/Quality 与 Dark/Light 两组 RadioButton；
- Panel elevation、双边框/阴影、状态栏与主题色板。

它使用默认 product chrome 呈现 hover/pressed/focused/disabled 层次，并通过
`setProductTheme()` 在既有 retained tree 上事务切换 Dark/Light。`--auto-demo` 会执行
Dark→Light→Dark（或相反）及 Slider→ProgressBar 联动，并在退出 JSON 中验证 theme switch、value、
13 个控件与 root 生命周期。完整文字视觉验收必须使用 bgfx + FreeType preset；普通 bgfx preset 的
placeholder text 只用于确定性降级和生命周期 smoke。
最新 Dark/Light client capture 分别位于
`artifacts/screenshots/ui-showcase-final-dark-v3/20260727-191013` 和
`artifacts/screenshots/ui-showcase-final-light-v3/20260727-191058`：两组均取得连续2帧 1280x720
非空画面，Performance/Balanced/Quality 三项完整显示且互不遮挡，导航状态文字也未占用控件区域。

`tina_sample_2d` 当前 UI 包含：

- HUD Label/Theme Button；
- Master/Music/SFX Checkbox/Slider；
- profile-name 单行 TextEdit；
- 65% ProgressBar；
- Windowed/Fullscreen RadioButton 组。

标准控件保留 create-time Theme 绑定；Theme Button callback 只记录 pending intent，`updateUI()` 再事务调用
`setProductTheme()`。Panel 与标题文字是有意的局部层级覆盖，每次换肤集中重算。`--ui-theme-demo`
在300帧产品门禁中执行 Dark→Light→Dark；schema 13 验证两次切换、最终 Dark、控件创建、TextEdit
UTF-8 初值、ProgressBar value 与 Radio 互斥 selection。
`artifacts/screenshots/sample-2d-product/20260723-013100/report.json` 记录 `ok=true`、exit 0、schema 3，
3次 960x540 client capture 中2帧稳定非空；首次 `PrintWindow` 白帧由 `blankLike=true` 排除。
人工复核 `frame-02.png` / `frame-03.png` 中上述控件可见、中文正常且无裁剪或重叠；两帧 65% fill
均为 x=700..842（143 px），选中色只出现在 Windowed RadioButton，client capture 未混入标题栏。

`tina_sample_3d` 的 `Product3DUI` 使用同一产品 Theme 契约提供 Theme Button、Auto Rotate Checkbox、
Rotation Speed Slider、Frame ProgressBar、标题/Inspector/状态层级。Checkbox 与 Slider 控制实际模型旋转；
callback 只提交 intent，`updateUI()` 统一处理控件状态、ProgressBar 与 `setProductTheme()`。schema 3 的
自动门禁要求 Dark→Light→Dark、标准控件 chrome 继承验证、5 Panel、9 Label、四类控件各1个、进度
100% 与 root 释放。FreeType 暗/亮截图分别在
`artifacts/screenshots/sample-3d-ui-dark/20260727-174319` 和
`artifacts/screenshots/sample-3d-ui-light/20260727-174414`，均取得连续2帧 1280×720 非空 client capture；
人工复核文字、控件层级、主题差异及 3D 主视区均成立，无裁剪或重叠。

当前 tip 最近直接验证为：`tina_ui_tests` 282/282（含 Focus/Modal/Pointer Capture）、`tina_runtime_ui_tests` 85/85、
`tina_ui_render_integration_tests` 15/15、product-2d 图的 `tina_ui_freetype_tests` 3/3。UI 容量回归
覆盖 Checkbox/Slider mutation、TextEdit pointer selection 和需要同时重绘旧/新节点的 focus step；
dirty queue 容量不足时状态与 callback 原子不变，同文本替换 selection 仍发布新 paint；文本 padding
回归覆盖 auto-size、多行回行和可变 glyph advance 的 TextEdit pointer selection。数字是当前工作树
证据，不是架构永久基线。

## 验证

```powershell
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_ui_render_integration_tests.exe --gtest_color=yes
```

完整 showcase：

```powershell
cmake --preset windows-msvc-vnext-bgfx-ui-freetype
cmake --build --preset windows-vnext-bgfx-ui-freetype-debug `
  --target tina_sample_ui_showcase tina_ui_tests tina_runtime_ui_tests `
           tina_ui_render_integration_tests tina_ui_freetype_tests -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_sample_ui_showcase.exe `
  --frames=150 --frame-delay-ms=0 --theme=dark --auto-demo
```

完整 product-3d UI/资源同轮门禁：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunProduct3dGate.ps1 `
  -OutJson artifacts\gates\product-3d.json
```

FreeType、bgfx 和 product-2d 需要对应 feature 图；完整命令见 [构建说明](building.md)与
[测试说明](testing.md)。

## 后续任务

| ID | 范围 |
| --- | --- |
| `UI-002` | 产品 HWND 自动接线 + Narrator/Inspect 金标 + Linux AT-SPI（映射 + HostBridge 已有） |
| `UI-003` | 跨 DPI/GPU 容差视觉门禁（映射单测 + 单机 ROI/baseline + content-scale-like 逻辑尺寸矩阵 + sample contentScale JSON + 字体 identity fingerprint 已有；OS 级 100/150/200% DPI 真机矩阵与跨 GPU 像素金标后置） |
| `UI-005` | ScrollView、虚拟 ListView、Dropdown、TreeView |
| `TEXT-001` | 多行 TextEdit、grapheme/shaping、候选窗定位 |
| （可选） | Phase C：圆角 clip、backdrop blur、完整 style resolver |

ProgressBar/RadioButton 的产品接入 `UI-001` 已完成，不应重新列为 Planned。
Theme A/B（token、panel 边、Low 假影、sample 改 token）已在产品 sample 路径落地；UI-002-SPI 与
可选 `tina_ui_uia` 映射切片已落地；UI-004 的 Focus Scope/Modal/Pointer Capture 已完成，但不要把外部
Narrator 真机门禁或 UI-003 标成 Done。
