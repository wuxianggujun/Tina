# UI 框架设计

本文具体说明 Tina UI **当前是什么**、其他成熟 UI 系统通常怎样分层，以及 Tina 下一阶段应演进成什么。
当前可用能力仍以 [Retained UI](ui.md)和源码为准；目标设计已由
[ADR 0023](adr/0023-ui-extensibility-style-paint-motion.md)接受，但未实现部分不能当成已经存在的 API。

## 结论

Tina 当前最接近：

> Unity UI Toolkit 的 Retained Element Tree + WPF/DOM 的 Routed Event + Unreal Slate 的后端无关绘制
> + Tina 自己的固定容量 committed snapshot。

下一阶段不应再造一套 Widget ABI，也不应复制完整 CSS、WPF DependencyProperty 或 UMG 对象体系。
推荐目标是：

> Retained Element Tree + 可组合标准 Behavior + 固定容量 Component Transaction + 预编译
> StyleSheet/Theme + backend-neutral Paint Primitives + fixed-capacity Motion。

这条路线允许游戏开发者组合 Inventory、HUD、Settings、Dialogue 等业务组件，同时保留 Tina 已有的
确定帧序、固定容量、失败原子性和 Render 隔离。

桌面产品的具体视觉语言、Compact/Comfortable density、semantic Theme、State Layer 和 Desktop Shell 不在这里
重复展开，统一见 [Tina Modern Desktop UI 设计规范](ui-modern-desktop.md)。该 program 正在实施：TMD-00..07
已经闭环，TMD-08 Desktop Shell reference 已实现结构、自动工作流与产品 icon atlas，100%/150% DPI 配对证据
暂缓；TMD-09 的 2D/3D 与 EditorApp Compact 源码迁移已完成，仍待 Editor 集中 gate，之后继续 TMD-10..11
的 OS scheme 与最终门禁。文档会明确区分当前 API 与尚未完成的产品证据。

其中 Image/Icon/NineSlice 不是装饰性补充，而是 HUD、Inventory、装备栏、技能栏、对话框和设置页的
基础视觉能力。它不依赖 Behavior side store 或 Component transaction 才能成立：`UI-PERF-001` 建立首份
计数协议后，Image 主线可与 Component 主线并行。当前 Image/Icon、NineSlice A/B 与 C 产品采用、失效、
尺寸矩阵及 `ui_image_nineslice_v1` benchmark 已全部交付，`UI-IMAGE-001` 已关闭。`UIIconContent` 是
Icon 的强类型 atlas-source/tint/sampling/alignment authoring profile；它不另建控件、图片存储、Asset、atlas
或渲染协议。

## 当前框架

### 分层

```text
Game State
  IGameState::onEnter / updateUI
        |
        v
Runtime phase capability
  PrimaryWindowUIRootBuilder / PrimaryWindowUITreeUpdater
        |
        v
per-window UIContext, owner-thread
  Element tree + generation UINodeId
  fixed-capacity text/canvas/control side storage
  routed input + focus/modal/pointer capture + Tooltip triggers + Menu commands
  theme role + local property override
        |
        v commitLayout(logical viewport)
Candidate compiler
  Structure -> Measure/Arrange -> ContentPlacement
  -> Hit/Focus/Modal -> Paint -> Semantics
        |
        v all candidates succeed
Committed snapshots, atomic publication
  Structure / Layout / Hit / Paint / Semantics
        |
        v
tina_ui_render_integration
  logical coordinates -> framebuffer coordinates
  UIDisplayList SolidQuad/SolidEllipse/Glyph/ImageQuad + glyph atlas + packet-local Texture2D refs
        |
        v
private Render backend, currently bgfx
```

对应代码入口：

| 层 | 当前入口 |
| --- | --- |
| Element 描述 | `include/tina/ui/UIElement.hpp` 的 `UIElementDescriptor` 与 `make*Element()` |
| Tree/事务/快照 | `include/tina/ui/UIContext.hpp` 的 builder、updater、`UIElementBuildTransaction` 与 committed views |
| Behavior | `include/tina/ui/UIBehavior.hpp`；Activate/Toggle/RangeInput/TextInput/Scroll/Select 使用私有 fixed-capacity side store，输入与具体视觉仍由 resolver 约束到私有 kind |
| Style/Theme | `include/tina/ui/UIStyle.hpp`、`UITheme.hpp` 与属性 override/reset |
| Visual profiles | `UISurface.hpp`、`UIDivider.hpp`、`UIBadge.hpp`、`UISwitch.hpp` + 对应 recipes；前三者复用普通 Element chrome，Switch 复用 Checkbox Toggle 状态机 |
| Tooltip | `include/tina/ui/UITooltip.hpp` + `makeTooltipElement()`；显式同 root Anchor、monotonic delay、committed metrics，独立于 Popup barrier |
| Menu | `include/tina/ui/UIMenu.hpp` + `makeMenuElement()/makeMenuItemElement()`；显式 Anchor、单 Window transient overlay、typed item state 与 committed metrics |
| TabView | `include/tina/ui/UITabView.hpp` + `makeTabViewElement()/makeTabElement()`；完整 direct-child pair、四向 layout、激活/导航与 committed metrics |
| Paint | `include/tina/ui/UIImageSource.hpp`、`UIImage.hpp`、`UIIcon.hpp` 与 `UIPaint.hpp`；Rectangle/Ellipse/Line box paint、Image/UIIcon content 和 Canvas `SolidRect`/`SolidEllipse`/`SolidLine`/`Image`/`NineSlice` |
| Render bridge | `include/tina/integration/UIRenderDisplayList.hpp` |
| DisplayList | `include/tina/render/UIDisplayList.hpp`；当前有 `SolidQuad`、`SolidEllipse`、`Glyph` 与 `ImageQuad`；`SolidQuad` 使用 `UIPixelCornerRadii`，Line 在 bridge 投影为带 exact 四顶点的 `SolidQuad` |
| Runtime 帧序 | `src/runtime/EngineHost.cpp`；`updateUI()` 后统一 `commitForFrame()` |

### Element 不是传统 Widget 对象

`UIElementDescriptor` 一次描述 Layout、Content、Visual、Behavior、Semantics、Hit 与 Focus policy。Button
recipe 当前等价于：

```cpp
UIElementDescriptor button = makeButtonElement("Apply", layout);

// recipe 已提供：
// contentAlignment = Center
// styleRole       = ButtonTonal
// behaviors       = Focusable | Activate
// semantics       = Button + Focus | Activate
```

Tonal 是普通命令的默认层级；Primary、Danger、Outlined 与 Text 通过显式 `UIStyleRoleId` 选择。
互斥工具、筛选和文档页签使用 `makeRadioButtonElement()` + `SegmentedButton` role，隐藏传统 leading
indicator，但不复制 RadioButton 的 selection、Focus、Keyboard/Gamepad 或 accessibility 状态机。

游戏保存 `UINodeId`，不保存 `Widget*`。节点状态由 `UIContext` 的固定容量 store 拥有；generation/owner
校验拒绝 stale 或跨窗口 ID。多节点组件由 `UIElementBuildTransaction` 先声明 node/text/canvas/Behavior
完整预算，任一步失败时回滚整个子树及所有已消费或未消费 reservation。

第一方视觉 profile 不等于新增 Widget ABI。Surface/Divider/Badge 只把 variant/tone/orientation 映射为
普通 Panel/Label 的 StyleRole、布局、Hit 与 Semantics 默认值；Switch 则把 Standard/Compact authoring
映射到既有 Checkbox kind、Toggle side store 和 action path，并仅在 theme chrome、track/thumb geometry 与
Switch semantics role 上区分。它们仍走同一 `createElement()`、固定容量 publication、DisplayList 与 GPU backend。

第一方多节点 profile 也不新增 owner。IconButton、FormField、Dialog 分别组合现有 Button/Icon/Tooltip、
Label/TextEdit/Button/Tooltip 与 Modal/Panel/Label/Button；`required*BuildBudget()` 先计算精确预算，三个
`build*()` 入口再经同一 `UIElementBuildTransaction` 原子构建。行为、Focus/Modal、Semantics、Image 和 Render
继续读取既有状态与 committed snapshot，不为 profile 建立平行 store、事件循环或 GPU pipeline。

### Axis-aligned descendant clip

`UILayoutStyle::clipDescendants` 默认 `false`，因此普通 Panel 不隐式裁剪后代。设为 `true` 后，当前节点
成为 axis-aligned clip owner：layout 在安排普通 in-tree Flow/Overlay children 时把继承的
`descendantClip` 与 owner 的 world border-box 求交，嵌套 owner 继续取交集。后代保持完整 `worldRect` 与
完整 Line/Ellipse/Image destination envelope，只收紧 committed `effectiveClip`；hit query、committed
paint、DisplayList clip 与 backend scissor 继续消费同一条既有数据链，不建立第二条渲染路径。

该属性不改变 `Visible/Hidden/Collapsed`、tree/paint/focus/semantics 顺序或 authored semantics
`worldRect`，也不额外改变 owner 自身的 paint clip。它不是 rounded/stencil clip；`cornerRadii` 仍只影响
Rectangle 自身 chrome，`UI-PAINT-002-A` 的四角 authoring 也不会改变该边界。ScrollView/ListView/TreeView
的 viewport clip 继续复用同一 descendant clip 传播和失败原子 publication 契约；viewport-level Popup
继续使用专用 anchor/clip policy。

### Retained per-corner authoring

`UI-PAINT-002-A` 已关闭 box chrome 的逐角 authoring 缺口。产品 consumer 是 Tina Studio Compact /
UI Showcase 的相邻 Segmented/header surface：仅外侧角为圆角、共享边为方角，并以 Canvas `SolidRect`
preview 展示四个不同 logical radius。它必须沿唯一
`UIBoxPaint/Canvas -> UICommittedPaintEntry -> UI-Render bridge -> UIPixelCornerRadii` 链路传播，不建立平行
paint snapshot 或 backend command。

容量与失败语义沿用现有事务：四角 inline 存储，不增加 capacity；Canvas `SolidRect` 仍消耗一个 command
slot/paint entry，box chrome 的 shadow/fill/border entry 数不增加。任何角为 NaN/Inf/负值时 descriptor、setter
或 Canvas assign fail-closed；dirty/paint/DisplayList 预检失败保留旧 retained/committed 状态，
backend 无副作用。超出目标尺寸的有效值逐角 clamp。rounded/stencil descendant clip、backdrop/blur、
per-corner Motion、新 shader/material 与跨 GPU/DPI golden 不属于 A。

既有 `UIAnimatableProperty::CornerRadius` 保持 uniform scalar。direct transition 只有在当前 authored 四角一致或已有
scalar presentation owner 时才有无歧义起点；否则返回 `InvalidStyle`。timeline 的 keyframe0 是显式 authoring，
因此允许从非 uniform retained radii 启动，首帧和后续 presentation 都把 scalar 写成四个相同角。

### 一帧如何流动

```text
上一份 committed Hit/Focus/Modal
  -> Platform Pointer/Keyboard/Gamepad transition
  -> Capture -> Target -> Bubble listener
  -> 未 preventDefault 时执行内建默认行为
  -> 更新 retained interaction/control state 并标 dirty
  -> Gameplay 只接收未被 UI consume/claim 的输入
  -> IGameState::updateUI 修改业务 UI
  -> UIContext commit 一次候选 snapshots
  -> 全部成功后原子替换 committed views
  -> UI-Render bridge 构建本帧 DisplayList
  -> 下一帧输入开始使用新 committed Hit
```

关键点是输入不会为了“看到刚修改的树”而隐式触发 layout。本帧 route 的 listener/callback 副作用也不会
因为后续 commit 失败而重放。这个边界比普通桌面 UI 更严格，但很适合确定性的游戏 Runtime。

### TextEdit 与 IME

TextEdit 默认是单行；在 `UIElementDescriptor::textEditMultiline` 设置
`UITextEditMultilineConfig::enabled` 后，LF、soft-wrap、固定容量 visual rows、垂直滚动、二维
hit-test 以及 Up/Down/Home/End 共享同一份 committed layout。selection/caret 的存储仍使用 Unicode
scalar offset，但编辑、删除、导航和替换位置会对齐无第三方依赖的 UAX #29 grapheme 子集；BiDi 与复杂
shaping 暂不进入契约。`UIContext::committedTextInputCaretRect()` 复制最后一次成功 paint publication
中的 logical caret，Runtime coordinator 再交给 Platform backend。

Windows GLFW adapter 将该 geometry 按当前 DPI 转成 IMM32 所需的 client pixels，并在 composition/candidate
placement 无效、失焦、隐藏或最小化时清除旧 hint；Headless 对非空 placement 返回不支持。Linux 当前只
保证 committed text，原生 XIM/Wayland preedit/candidate placement 及 Windows 真机微软拼音人工证据仍由
`TEXT-001` 跟踪。

## Button 完整链路

### 1. 创建

```text
makeButtonElement(text, layout)
  -> UIElementDescriptor
  -> createElement(parent, descriptor)
  -> 校验 behavior/semantics/style/content
  -> 分配 node + text + Button side storage
  -> 任一步失败全部回滚
```

### 2. 输入

```text
Pointer Down
  -> committed hit 找 physical target
  -> Capture/Target/Bubble listeners
  -> focus + arm + pressed

Pointer Up
  -> release capture/press
  -> 仍满足 activation 条件时调用 Button action

Keyboard Enter/Space 或 Gamepad South
  -> 默认焦点 Button
  -> 同一 activation/default-action 路径

UIA Invoke
  -> committed semantics action 校验
  -> owner thread
  -> 同一 Button 默认行为与 callback
```

### 3. 当前视觉反馈

Button 现在已经有 hover、focused、pressed、disabled 反馈。未配置 Motion 时，pressed 会交换亮/暗 border
并将 shadow offset 设为零，表现出按下深度；配置 direct/Style transition 后可对 paint 属性插值，typed
timeline 还可在白名单内原子发布 layout/hit/paint。默认行为仍即时，不因动画延迟 action callback 或节点销毁。

第一版动画建议：

| 状态 | 建议时长 | 只影响 |
| --- | --- | --- |
| Normal -> Hover | 80-120ms | background/border/text color |
| Hover -> Pressed | 40-60ms | color、shadow/visual offset |
| Pressed -> Hover/Normal | 80-120ms | color、shadow/visual offset |
| Focus change | 80-120ms | focus border/opacity |
| reduced-motion | 0ms | 立即落到目标值 |

动画不得延迟 action callback，不得移动真实 hit rect，也不得因为退出动画隐式延迟节点销毁。

## 第三方今天能做什么

### 可以

- 在源码树内链接 `Tina::UI`，通过 Runtime phase facade 创建和更新 root；
- 用 Panel、Label、Button、Checkbox、Slider、ListView、TreeView 等组合自己的业务组件；
- 定义组件函数并返回一组 `UINodeId`，例如 Inventory slot 的 root/icon/count label；
- 设置布局、文本、Semantics、命中策略、StyleRole、局部 box/text/control paint；
- 在 `UIContext` 创建首个节点前注册 StyleClass/ColorToken、安装 literal/token-backed BoxFill stylesheet；
  游戏侧通过 `GameStateEnter` 的 `PrimaryWindowUIRootBuilder` 使用同一 startup-only 契约；
- 注册 routed pointer listener，使用 Button/Slider/selection 等已有 callback；
- 使用 Rectangle/Ellipse/Line box paint、Image/Icon content，以及 `SolidRect`/`SolidEllipse`/`SolidLine`/
  Image/Stretch-only `NineSlice` Canvas 组合 backend-neutral 图形。
- 通过安装目录使用 backend-neutral `Tina::GameSDK`、独立 `PlatformGlfw`、DesktopBootstrap/RenderBgfx
  与可选 UIFreetype component；GameSDK-only 不加载 Desktop adapter。

### 还不可以

- 把 package consumer 证据当作正式 supported ABI tuple；ADR 0024 与 pre-1.0 strict exact-version gate 已落地，但
  previous-release object/API/symbol baseline 仍是 release 进入条件；
- 注册任意新 Widget class 或 Behavior state machine；
- 从 UI 持有 AssetHandle/Lease、FrameResourceRef 或 texture/bgfx handle；
- 声明白名单外的 layout animation，或 loop/seek/pause/repeat/yoyo/completion callback、spring/inertia 等高级
  playback；direct/Style transition 仍严格只覆盖 paint-only color/opacity/radius/visual-offset；
- 传入任意 GPU/paint callback。

Activate/Toggle/RangeInput/TextInput/Scroll/Select 已从 concrete kind 拆到独立 fixed-capacity side store；TextInput/Scroll/Select resolver
仍选择现有 TextEdit/ScrollView/Dropdown kind 以复用输入、焦点、Pointer selection/drag、popup 与具体视觉路径，并只接受能映射为现有
`BuiltinElementKind` storage contract 的组合。第三方组件的推荐方式是**组合 Element 子树**，
不是继承 Widget。

### 当前业务组件写法

```cpp
struct InventorySlotNodes final {
    UI::UINodeId rootButton{};
    UI::UINodeId iconPlaceholder{};
    UI::UINodeId countLabel{};
};

// 这是普通 C++ recipe。Icon child 可直接使用 makeIconElement()；Button root
// 负责 input/focus/accessible name，count label 负责文本表现。
Core::Result<InventorySlotNodes>
buildInventorySlot(Runtime::PrimaryWindowUITreeUpdater& tree,
                   UI::UINodeId parent,
                   std::string_view countText);
```

实现应由 Button root 负责 input/focus/semantics，子 Element 负责 icon/count 的表现，不为每个业务控件
增加新的继承层。Runtime facade 已提供 phase-scoped bounded component transaction，并在首次 mutation 前统一
预留 node/text/canvas/各 Behavior pool；失败时整棵回滚，commit 后不保留 component wrapper。

## 成熟 UI 系统怎样设计

| 系统 | 值得借鉴 | 不适合直接复制到 Tina |
| --- | --- | --- |
| Unity UI Toolkit | Retained `VisualElement`、USS class/pseudo-state、轻量 transition、Capture/Target/Bubble | 完整 selector/cascade、任意 mesh generation |
| Unreal Slate | 声明式组合、Brush/Image/NineSlice、backend-neutral draw elements | 公开 `SWidget` vtable、SharedPtr 生命周期和任意 `OnPaint` |
| Unreal CommonUI | Activatable Screen、Layer Stack、Action Router、手柄优先导航与输入设备提示 | Slate + UMG + CommonUI 三层对象体系 |
| Godot Control | Theme resource、Container、TextureRect/NinePatchRect、Tween | SceneTree/脚本对象与任意 `_draw()` 的强耦合 |
| WPF | Routed Event、Focus Scope、VisualState、外观与行为分离 | DependencyProperty、反射 Binding、完整模板系统 |
| Qt Quick | Item composition、State/Behavior/Transition、Image/BorderImage | QML 元对象/脚本运行时和任意 Scene Graph 扩展 |

Tina 应向 Unity 学 style class/pseudo-state 与 transition 小模型，向 CommonUI 学游戏页面和 Action Router，
向 Slate/Godot/Qt Quick 学 Brush、Image、NineSlice，向 WPF/Qt Quick 学 VisualState，但保留自己的固定容量
snapshot 和 backend 隔离。

## 目标框架

```text
Tina::UI
|- Element Kernel
|  |- ElementStore / generation IDs
|  |- root ownership / bounded component transaction
|  `- committed snapshot publisher
|- Layout
|  |- Flex
|  `- Overlay / content placement
|- Interaction
|  |- routed events / pointer capture
|  |- focus / modal / navigation
|  `- capability-based behavior side stores
|- Flow
|  |- strong Layer/Screen identities over retained nodes
|  `- fixed-capacity per-Layer active Screen stack
|- Presentation
|  |- Theme tokens
|  |- StyleRole + user StyleClass
|  |- node-local pseudo-state resolver
|  |- Rect / Ellipse / Line / Image (including Icon) / NineSlice / Glyph / Clip
|  `- fixed-capacity paint-only Motion
|- Semantics
`- Private adapters
   |- UI-Render DisplayList bridge
   |- FreeType
   `- Windows UIA / future AT-SPI
```

仍然只有一个 per-window `UIContext` 编排这些私有 store，不建立 `UIContext2`、第二套 QML/HTML 层或
另一条 renderer。

## 目标 API 形状

以下代码只表达目标边界，不是当前公共头。

### Component

```cpp
struct UIBehaviorSlotBudget final {
    usize activate{};
    usize toggle{};
    usize range{};
    usize textInput{};
    usize scroll{};
    usize selection{};
};

struct UIComponentBudget final {
    usize nodes{};
    usize textBytes{};
    usize canvasCommands{};
    UIBehaviorSlotBudget behaviors{};
};

class UIComponentBuilder final {
public:
    Core::Result<UI::UINodeId>
    createElement(UI::UINodeId parent, const UI::UIElementDescriptor& descriptor);

    Core::Status commit();
    void reset() noexcept;
};
```

Component 只是有界建树 recipe，不是新的 retained 类型。builder 只在当前 `onEnter/updateUI` phase 有效，
失败或析构回滚整个组件。`beginComponent(parent, budget)` 必须在创建第一个节点前为 node/text/canvas 和
各类 Behavior side store 做完整 reservation；任何 reservation 不足都不改变 live tree。`budget` 是本事务
可消费的已预留上限，不是仅用于事后统计的提示值。

### Behavior side store

```text
NodeRecord
|- behavior mask
|- optional ActivateState slot
|- optional ToggleState slot
|- optional RangeState slot
|- optional TextInputState slot
|- optional ScrollState slot
`- optional SelectState slot
```

创建 Element 时按 capability 从对应固定池取得 slot，setter 按 capability 校验，而不是按 concrete kind
校验。Activate/Toggle/RangeInput/TextInput/Scroll/Select 切片已实现六池原子预检、publish/release/reuse 与 capacity/active/high-water 统计；
Activate/Toggle/RangeInput 默认行为、TextInput selection setter/query 与 Scroll style/offset/committed metrics 按 capability；Select pool 持有 Dropdown 当前选项。
TextInput 的 Keyboard/IME/Pointer 路由仍复用 concrete TextEdit；ScrollView paint/thumb geometry 与 Pointer drag 路由、Slider
paint/change callback/Pointer drag geometry、TextEdit paint，以及 Dropdown selection API/popup/paint/input routing 同样由对应 concrete kind 承担。官方
`make*Element()` 保持为稳定 recipe。只有标准 Behavior + routed listener 无法表达真实需求时，才考虑
startup-only custom Behavior SPI。

### StyleSheet

```cpp
enum class UIStyleRoleId : u8 {
    None = 0,
    PanelSurface,
    // Existing built-in product roles...
    TreeView,
};

struct UIStyleClassId { u32 value{}; };
struct UIStyleTokenId { u32 value{}; };

enum class UIStyleState : u16 {
    None     = 0,
    Hovered  = 1U << 0U,
    Pressed  = 1U << 1U,
    Focused  = 1U << 2U,
    Disabled = 1U << 3U,
    Checked  = 1U << 4U,
    Selected = 1U << 5U,
    Open     = 1U << 6U,
    Dragging = 1U << 7U,
};
```

`UI-STYLE-001` 已完成。已落地的 foundation/context 切片包括：强类型
`UIStyleClassId`/`UIStyleTokenId` 与 node-local state mask、startup-only ColorToken registry/value、公开
literal/token-backed `UIStyleBoxFillRule`，以及私有 fixed-capacity 双缓冲 compiler。compiler 按
`(role,class)` 建 bucket、二分查找并做 required-state subset 匹配；同一匹配链 later rule wins，token rule
拒绝未注册 ID 以及 literal/token 二义性，compile 失败保留旧 sheet 与 revision，构造完成后
register/compile/resolve 不增长 PMR storage。

`UIContextCapacityConfig` 已固定 class/token/rule/bucket/rules-per-bucket/node-class-link 容量；
`registerStyleClass()`、`registerStyleColorToken()` 与 `installStyleSheet()` 仅允许首个节点创建前在 owner thread
调用。ColorToken 值和 rules 均复制到 Context-owned PMR storage，安装会原子替换 compiled sheet。
`UIElementVisual::styleClasses` 是 authoring borrowed span，创建时复制到每节点最多 4 个 class slot；destroy、
创建回滚与 slot reuse 都会释放 link。`UIContextStatistics::style` 已发布 class/token/rule/bucket/link 的
capacity/count/high-water、failure 与 revision counter。

首个 paint/state 垂直切片也已落地：Context 从既有 retained interaction/control storage 派生
Hovered/Pressed/Focused/Disabled/Checked/Selected/Open/Dragging，按 dirty node 刷新 per-node resolved BoxFill
cache，再由 committed paint 只读该 cache。优先级为 product chrome < stylesheet < local override；单节点
paint/state 更新不扫描全树或完整 rule table，并输出 inspected node、resolved node 与 candidate rule counter。
ListView/TreeView owner 更新时仅额外刷新固定 materialized row pool，防止虚拟行复用残留 Selected 样式。

`PrimaryWindowUIRootBuilder` 已在 `GameStateEnter` 暴露同一 startup-only class/ColorToken 注册与
literal/token-backed sheet 安装契约；`UIContext` 与 phase-scoped `PrimaryWindowUITreeUpdater` 还提供运行期
`styleColorToken()` / `setStyleColorToken()`。运行期 token 更新使用固定容量 reverse-dependency 链：resolve
cache 在每节点记录 winning ColorToken，destroy/local override 时 unlink，token setter 只遍历依赖节点做
dirty-queue 预检与 Paint dirty，不再对 live tree 做两遍 `O(N)` resolve。`ui_style_state_v1` 已补齐
4096-node/256-rule 固定 workload、单节点 resolve counter、token/bucket/class-link high-water、clean commit
  零工作与稳定 checksum。stylesheet imageTint、属性 dirty metadata、showcase Integration 与
  `RunUiStyleVisualGate.ps1` Visual ROI 已落地；更广 opacity 属性面不属于已冻结的首版范围。

第一版 selector 只支持当前节点的 `role + class + state mask`，不支持 descendant、`nth-child`、运行时
CSS parser 或任意 specificity。推荐优先级：

```text
built-in recipe
  < product stylesheet
  < role/class/state rule
  < local inline override
```

每个属性静态声明 dirty 影响（公开 `UIStylePropertyKind` / `dirtyFlagsForStyleProperty`）：颜色/opacity 与
ImageTint 只影响 Paint（+ 通用 paint 路径的 Semantics）；ColorToken reverse 路径仅 Paint；TextStyle/
ContentAlignment 触发 Layout+Paint；hit policy 触发 Hit；LayoutStyle 触发 Layout/Hit 不强制 Paint；
TextOverflow 仅 Paint——截断按已提交 content box 解析，intrinsic measure 与 accessibility name 都保持
完整文本。Context 关键 visual setter 经 `markStylePropertyDirty` 分发，禁止 ad-hoc 相位组合。运行期 ColorToken
更新使用固定容量 reverse-dependency 链：

- 相同值直接 no-op，`lastStyleTokenUpdateInspectedNodeCount`、
  `lastStyleTokenUpdateResolvedNodeCount`、`lastStyleTokenUpdateAffectedNodeCount` 与
  `lastStyleTokenUpdateCandidateRuleCount` 全部为0；
- resolve cache 在每节点记录 winning ColorToken；destroy/local BoxFill override 时 unlink；
- 非 no-op 只遍历该 token 的依赖链并预检 dirty queue。inspected/resolved/affected 均按依赖节点计，
  reverse path 上不再跑 candidate-rule matching（`candidate=0`）；
- dirty queue 容量不足时返回 `CapacityExceeded`，保留上述检查统计，但 token 值、dirty state 与 committed
  snapshot 不变；容量通过后才修改 token，并再次遍历依赖链为 affected 节点发布 Paint dirty；
- 因此 token update 为 `O(affected links)`，不再对 live tree 做两遍 `O(N)` resolve。

### Tooltip：独立 Anchor presentation contract

Tooltip 是现有 retained tree 的独立 `BuiltinElementKind::Tooltip`，不是 Dropdown Popup 的别名。Popup 继续拥有
focus scope、输入 barrier 与 dropdown linkage；Tooltip 固定 Overlay、Ignore hit、Exclude semantics、无 behavior/
focus scope，不能拥有 child，也不会取得 focus、Pointer capture 或 active Popup/Modal 身份。Tooltip pass 只调整
既有 layout/paint order，不增加第二棵树、第二条 Runtime facade 或第二个 update loop。

公开 authoring/查询面为：

```cpp
enum class UITooltipPlacement : u8 { Auto, Above, Below, Left, Right };
enum class UITooltipTrigger : u8 {
    None = 0,
    PointerHover = 1U << 0U,
    KeyboardFocus = 1U << 1U,
    Manual = 1U << 2U,
};

struct UITooltipConfig final {
    UITooltipPlacement placement = UITooltipPlacement::Auto;
    float anchorGap = 6.0F;
    float viewportMargin = 8.0F;
    Core::Duration initialDelay{0.5};
    Core::Duration reshowDelay{0.1};
    Core::Duration dismissDelay{0.1};
    UITooltipTrigger triggers = UITooltipTrigger::PointerHover |
                                UITooltipTrigger::KeyboardFocus |
                                UITooltipTrigger::Manual;
};

struct UITooltipMetrics final {
    UILogicalRect anchorRect{};
    UILogicalRect tooltipRect{};
    UITooltipPlacement resolvedPlacement = UITooltipPlacement::Below;
    bool open = false;
};
```

`makeTooltipElement(text, config, layout)` 创建节点，`setTooltipAnchor(tooltip, anchor)` 建立 Context-owned、按 node
index 固定容量的双向关系。两端必须 live 且位于同一 root；自身、祖先/后代、跨 root、stale generation、
Tooltip/Popup/Modal/虚拟 row 等不稳定节点 fail closed。destroy/root release/slot reuse 同时清理正反边；一个
Anchor 至多一个 Tooltip，Context/per-Window 全局只允许一个 active Tooltip。

状态流固定为 `trigger intent -> pending deadline -> active -> dismiss deadline -> reshow window`。Hover 读取最后
committed physical hit ancestry而不是 capture target；Focus 读取 committed keyboard focus；Manual 由
`showTooltip()` 显式请求且立即替换同 Window 的 active Tooltip。`Core::IMonotonicClock` 通过 Context 既有
`setMotionClock()` 注入，`commitLayout()` 在原帧阶段推进；超出 native steady-clock 范围的有限 delay 饱和而不
wrap。Pointer Down、wheel、text/composition、anchor disable/visibility/destroy 与 Modal scope change 是 hard
dismiss barrier。

placement 只读取最后一次成功提交的 Anchor rect。Auto 从四向可用空间中选择，显式方向不够时 flip，最后在
viewport margin 内 clamp。Tooltip open/geometry 与 Layout/Hit/Paint/Semantics candidate 一起发布；
`UITooltipMetrics` 仅保存最后成功 commit。Context 创建时按 node capacity 预分配 Tooltip state、反向 Anchor 与
commit rollback scratch；任何后续 candidate/capacity failure 都恢复 clock-driven live state并保留旧 snapshot/
metrics，不 heap fallback。Anchor 已有显式 semantics description 时保持作者值；否则 Tooltip text 复制为
Anchor description/Windows HelpText，Tooltip 本身不发布 Focus/Activate 等 action。

`UIContext`、`UITreeUpdater` 与 Runtime `PrimaryWindowUITreeUpdater` 共享
`setTooltipAnchor/clearTooltipAnchor/tooltipAnchor/showTooltip/dismissTooltip/isTooltipOpen/tooltipMetrics`；Runtime
版本继续受 phase epoch/lifetime 与 sticky error 约束。

### Menu / MenuItem：独立 transient overlay contract

Menu 是现有 retained tree 的独立 `BuiltinElementKind::Menu`，不是 Popup alias。`UIMenuStateStorage` 按 node
index 固定容量保存 Menu config、MenuItem config/checked state、Menu↔Anchor 正反边、Item→Menu 关系、active
Menu、layout scratch 与 committed metrics；`UIMenuLayout` 只负责四向 Auto/flip/clamp，`UIMenuInput` 收口
command validation。模块不反向持有 Context，也不建立第二套 update loop、focus/capture store 或 GPU pipeline。

`setMenuAnchor()` 由 Context 校验同 root、self/ancestor cycle、generation 与稳定 Anchor kind。Menu 只接受 direct
MenuItem，Item 禁止 child；Command/Check/Radio/Separator 的行为和 semantics 在 recipe/resolver 时固化。
Check/Radio state 留在 Menu storage，Radio 按同 Menu 的 `radioGroup` 互斥，同组重复初始 checked authoring
会失败原子地拒绝，不占用标准 Toggle behavior slot。
destroy、root release、generation reuse、relationship replace 与 capacity failure 都由 Context 协调正反边和 dirty
reservation，失败时不泄漏半份 checked/open/focus 状态。

Menu 与 Dropdown Popup 只共享 `UIContext` 的 single-transient-overlay coordinator：打开一方关闭另一方；各自仍保留
独立 storage、relationship、placement 与输入实现。Menu surface 为 Ignore hit + Contain focus scope，Item 才是
Targetable。inside blank chrome/outside Primary Down 使用一个 Down/Up barrier 防止 click-through，不建立 Modal
barrier，也不捕获 Pointer；wheel/text input、Anchor/Menu disable/visibility/destroy 与 Modal scope change 会关闭。

布局从上一份成功 committed Anchor geometry 解析；Menu candidate 与 Layout/Hit/Paint/Semantics 一起提交，
`UIMenuMetrics` 只在成功后 publish。Up/Down/Home/End/Escape、D-pad Up/Down/East 使用固定容量 command latch，
先于 Dropdown、TabView 与通用 spatial focus。Pointer、默认 Activate 与 accessibility Invoke/Toggle 最终调用同一
MenuItem activation；UIA 仅让 Command/Check/Radio 暴露 Invoke，只有 Check/Radio 暴露 TogglePattern。
`MenuSurface` 与 `MenuItem` StyleRole 复用 Popup/DropdownItem chrome，indicator/separator 仍输出现有 box paint
primitive。当前没有 MenuBar、submenu、第二套 atlas 或 shader。

### SplitView / Splitter：复用既有 capability 的分栏控件

SplitView 是独立的 retained built-in contract，但仍由现有 `UIContext` 唯一 owner 协调。`UISplitViewStateStorage`
按 node index 固定容量保存 config、双向 parts、pending fraction、layout scratch 与 committed metrics；
`UISplitViewLayout` 和 `UISplitViewInput` 分别负责三个 direct child 的 arrange 计划与 splitter pointer fraction 计算。
这让新增控件主要落在 state/layout/input 模块，Context 只接入 resolver、dirty、publication 与生命周期清理。

`UISplitViewConfig` 的 orientation 为 Horizontal/Vertical，`initialFraction` 与两侧 minimum size 在 arrange 时 clamp；
`UISplitViewMetrics` 仅在 layout/Hit/Paint/Semantics 同一 atomic commit 成功后更新。`setSplitViewParts()` 要求同 root、
恰好三个不同 direct Flow child，并且中间 child 必须是 `Splitter`；清理、destroy、generation reuse、root release 与
commit capacity failure 保留旧关系/旧 metrics 或原子释放全部反向边。

`Splitter` 复用 `Focusable | RangeInput`、现有 `armedSlider`/Pointer Capture、`routeRangeInputCommand()` 与
`performAccessibilityAction(SetRangeValue)`，仅在 authoring/semantics 层区别于 Slider。它不拥有第二套 input/update loop、
focus/capture store、Icon/Image pipeline 或 GPU shader；SplitView 本身默认 Ignore hit，Splitter 才是 targetable。

### TabView / Tab：完整 pair relationship 与专属 Tab chrome

TabView/Tab 是现有 retained tree 的独立 built-in contract。`UITabViewStateStorage` 按 node index 固定容量保存
TabView config、Tab/Panel 双向 links、active Tab、`UITabPaint`、layout scratch 与 committed metrics；
`UITabViewLayout` 只计算 Top/Bottom/Left/Right strip/panel regions，`UITabViewInput` 收口 command validation。
它们不反向拥有 `UIContext`，也不增加第二套 focus、input、semantics、paint、Runtime facade 或 update loop。

`setTabViewItems()` 是完整关系提交，而不是增量 append：所有 pair 必须由同一 TabView 的不同 direct Flow child
构成并恰好覆盖其 direct children。Tab 必须是专用 Tab kind，Panel 复用普通 retained content node；关联后追加
child 会解除旧关系，下一次必须重新提交完整 list。destroy、root release、generation reuse 与 dirty/capacity
failure 均由 Context 协调固定容量 storage 原子清理或保留旧 committed publication。

布局只发布 active Panel，其余 Panel 为 Collapsed；`UITabViewMetrics` 与 Layout/Hit/Paint/Semantics candidate 同时
commit。Automatic activation 随方向 focus 选择，Manual activation 只移动 focus，直到 Pointer/Activate 明确选择。
Keyboard Arrow/Home/End、Gamepad D-pad、Pointer 与 accessibility action 复用同一默认行为；placement 对应轴的
Tab navigation 先于通用空间 focus，handled Down/Up 复用既有 fixed-capacity latch。

TabView 发布 TabList，Tab 发布可 Focus/Activate 的 Tab 与 selected state，active Panel 由关系提升为 TabPanel；
inactive Panel 不进入 committed semantics。`UITabPaint` 独立表达 selected/hover/focus/pressed/disabled surface 和
focused border，`UIStyleRoleId::Tab`/`makeTabChrome()`/Theme transition/`UIStyleOverride::TabPaint` 共同管理。
这不是 RadioButton chrome alias，也不新增 GPU primitive；实际 box/text 仍复用现有 committed paint、DisplayList
与 shader pipeline。

### Image、Icon 与 NineSlice

```cpp
struct UIImagePixelExtent final {
    u32 width{};
    u32 height{};
};

struct UIImagePixelRect final {
    u32 x{};
    u32 y{};
    u32 width{};
    u32 height{};
};

struct UIImageSource final {
    Core::AssetId texture{};
    UIImagePixelRect sourcePixels{};
    UIImagePixelExtent texturePixelExtent{};
    UILogicalSize intrinsicLogicalSize{};
};

enum class UIImageFit : u8 {
    Fill,
    Contain,
    Cover,
    None,
};

enum class UIImageSampling : u8 {
    Linear,
    Nearest,
};

struct UIImageContent final {
    UIImageSource source{};
    UIImageFit fit = UIImageFit::Contain;
    UIContentAlignment alignment{};
    UIStraightSrgba8Color tint = rgba8(255, 255, 255);
    UIImageSampling sampling = UIImageSampling::Linear;
};

struct UIIconContent final {
    UIImageSource source{};
    UIStraightSrgba8Color tint = rgba8(255, 255, 255);
    UIImageSampling sampling = UIImageSampling::Linear;
    UIContentAlignment alignment{
        .horizontal = UIAxisAlignment::Center,
        .vertical = UIAxisAlignment::Center,
    };
};

enum class UICanvasCommandKind : u8 {
    SolidRect,
    Image,
    NineSlice,
    SolidEllipse,
    SolidLine,
};

struct UIImagePixelInsets final {
    u32 left{};
    u32 top{};
    u32 right{};
    u32 bottom{};
};

struct UILogicalCornerRadii final {
    float topLeft = 0.0F;
    float topRight = 0.0F;
    float bottomRight = 0.0F;
    float bottomLeft = 0.0F;
};

struct UICanvasCommand final {
    UICanvasCommandKind kind = UICanvasCommandKind::SolidRect;
    UILogicalRect bounds{};
    UIStraightSrgba8Color color{}; // Solid fill or image tint.
    UILogicalCornerRadii cornerRadii{}; // SolidRect only.
    UIImageSource imageSource{};
    UIImagePixelInsets imageSourceInsets{};
    UIEdgeSpacing imageDestinationInsets{};
    UIImageSampling imageSampling = UIImageSampling::Linear;
    UILogicalPoint lineStart{};
    UILogicalPoint lineEnd{};
    float lineThickness = 0.0F;
    float ellipseStrokeWidth = 0.0F;
};
```

以上是当前公开契约的简化摘录，精确签名以 `UIImageSource.hpp`、`UIImage.hpp`、`UIIcon.hpp`、
`UIPaint.hpp` 和 `UIElement.hpp` 为准。`UIImageContent` 是第一类 Element content：

- `Fill/Contain/Cover/None`、alignment、tint/opacity 和 `Linear/Nearest` 都是 authoring metadata；
- `Fill` 拉伸到 content box；`Contain` 保持比例完整显示；`Cover` 保持比例并按 alignment 裁 source；`None`
  以 intrinsic logical size 按 alignment 放置并由 effective clip 裁剪；这些计算只使用 committed logical
  geometry，不读取 framebuffer DPI；
- `intrinsicLogicalSize` 参与 auto/intrinsic layout，不需要为了测量同步读取图片文件；
- `sourcePixels + texturePixelExtent` 描述完整纹理或 atlas 子矩形，并允许在资源解析前验证 UV；
- source 必须使用有效 AssetId、非零 texture extent、显式非空且位于 extent 内的 integer pixel rect，以及
  finite/positive intrinsic logical size；空 source rect 不作为“整张纹理”哨兵，整图必须显式写
  `{0, 0, textureWidth, textureHeight}`；
- atlas UV 按显式 source pixel edge 归一化；Linear atlas 的 edge-extruded gutter 与高对比相邻 cell
  不串色证据仍由产品/视觉 C 关闭；首版 UI 图片不建立 mipmap 或 wrap 契约；
- tint/opacity、sampling、AssetId/source rect、fit/alignment 只使 Paint/DisplayList 失效；只有
  `intrinsicLogicalSize` 在节点使用 Auto/intrinsic sizing 时使 Measure/Arrange 失效，均不改变 Hit；
- Image content 与 text content 首版互斥；“图标 + 文字”用两个子 Element 组合，而不是在一个 content 字段中
  引入另一套 inline layout；
- Canvas Image 使用同一 `UIImageSource`，但 destination rect 是 Element-local paint，不贡献 intrinsic layout。

`UIBoxPaint::primitive` 当前支持 Rectangle、Ellipse 与 Line。Ellipse 使用 Element layout rect，零 stroke
表示填充，正 stroke 表示向内描边；Line 使用 Element-local 起止点和 logical thickness。Canvas 的
`SolidEllipse`/`SolidLine` 使用相同语义并复制进固定容量 command storage。非法、退化或非正厚度 Line
fail closed，不回退成覆盖整个 Element 的矩形。box/Canvas `SolidRect` 只保留 canonical 四角 logical 值，
`UILogicalCornerRadii::uniform()` 与 `makeSolidBox(color, radius)` 仅负责构造四个相同角。

Line committed paint 保存 logical world endpoints、logical thickness 与覆盖实际线宽的 conservative envelope。
UI-Render bridge 先在 logical 空间构造法向四角，再分别应用 framebuffer `scaleX/scaleY`，因此非等比 DPI
映射仍得到正确平行四边形；DisplayList 以 exact `UISolidQuadVertices` 发布，不公开 `rotationRadians` 或
rotated-quad 兼容入口。Ellipse 保持完整未预裁切 bounds，发布 `SolidEllipse` command；bgfx 统一 coverage
shader 用 local UV 计算解析椭圆 coverage，并用外 coverage 减内 coverage形成向内描边。最终可见区域仍由
axis-aligned clip/scissor 决定，避免把部分越界的圆形或图片先裁 bounds 后错误拉伸。

Icon 只在 authoring/语义层与 Image 区分。业务代码使用强类型
`makeIconElement(UIIconContent, layout)`；recipe 固定 `Contain`，`UIIconContent` 默认居中并携带 tint/sampling，
节点固定 `UIPointerHitPolicy::Ignore` 和 decorative `UISemanticsMode::Exclude`。它仍创建普通 Image Element，
消耗既有 image content slot，最终只产生一个
`ImageQuad`。Ignored Icon 自身不成为 pointer target，targetable 后代规则和父 Button route ancestry 仍沿用
现有命中契约。不新增 Icon Widget、
Icon Behavior、`IconAsset`、`UITexture` 或第二套 atlas manager。icon atlas 直接使用 `sourcePixels`；
icon-only Button 由 Button root + Icon child 组成，Button root 必须提供显式 accessible name，装饰 Icon child
默认 `Exclude`。独立表达信息的图片才发布 `UISemanticsRole::Image`，并要求显式 name；不得从
资源文件名猜测可访问名称。Image role 默认没有 action；Windows adapter 映射到既有私有
`kControlTypeImage`，其他平台 adapter 使用对应 image role，不建立图片专用 action seam。

NineSlice 是 Canvas/Visual paint primitive，不是新的 Widget 或 DisplayList command kind。它在同一
`UIImageSource` 上增加 source-pixel insets 与 destination-logical insets；首版只支持 Stretch，不支持 Tile。
source inset 必须位于 source rect 内；当 destination 小于固定边之和时按确定比例压缩两侧，零面积 patch
不发命令。一个 NineSlice 因此展开为 1..9 个 `ImageQuad`，必须先计算精确数量并一次预留 retained paint、
DisplayList command 和 batch 容量，不能截断半个控件。每个 committed patch 显式携带 authored half-open
right/bottom cut；Integration 直接 round 该端点，而不是假定 float `start + (end - start) == end`。相邻 patch
因此把同一 logical cut 映射到同一 pixel boundary，避免 DPI 缩放时产生缝隙或重叠。

Context 已提供可配置、可统计的 `imageContentCapacity`、`canvasCommandCapacity` 与 root resolver slot；Icon
消耗普通 Image slot，不增加 icon 专用 pool。`paintSnapshotCapacity` 不再受 node capacity 上限约束，独立
上限为8,388,608。NineSlice 在 retained Canvas 中只占一个 command slot，但每次 paint candidate 按实际非空
patch 消耗1..9个 paint-entry slot；descriptor 创建、build transaction rollback 和 paint publication 均保持
固定容量与失败原子性。

资源与渲染链路固定为：

```text
UIImageContent / UIIconContent / Canvas Image / Canvas NineSlice
  -> fixed-capacity retained image metadata
  -> UICommittedPaintKind::Image entries + opaque root resolver-scope id
  -> owning-root scoped image resolver
  -> packet-local Texture2D FrameResourceRef + owning FramePin
  -> UIDrawCommandKind::ImageQuad
  -> RGBA textured UI batch
```

模块职责不能混在一个 `UIImage` 类里：

| 模块 | 当前 A/B 职责 |
| --- | --- |
| `Tina::UI` | `UIImageSource/UIImageContent/UIIconContent/NineSlice` authoring、intrinsic layout、fit/crop 与 NineSlice logical patch 展开、committed Image paint；UIIcon 仅是强类型 authoring profile；root 只携带 opaque resolver-scope id，只依赖 Core AssetId，不认识 AssetSystem/Render/backend |
| Runtime root capability | 将 root 绑定到 generation-checked resolver scope；root destroy/unbind 先于 State-owned AssetSystem/registry teardown，stale scope fail closed |
| `tina_ui_render_integration` | 直接读取 committed Image entry 的 scope id，执行 logical-to-pixel、clip、resolve/dedupe/pin，并事务构建 DisplayList；不重复布局/切片，也不为每个 entry 回溯祖先找 root |
| `Tina::Render` | 通用 Texture2D frame resource、`ImageQuad` command/batch、packet-local ref 校验和 Null/backend preflight |
| Asset/product adapter | 将 `(resolver scope, AssetId)` 解析为 resident Texture2D binding、实际 pixel extent 与 owning pin；不加载、不 decode、不持有 UIContext |

resolver 是窄的 frame-build SPI：输入 `AssetId + FrameResourceSink`，返回可空的
`FrameResourceRef + actualPixelExtent`；缺失是可计数的资源状态，wrong-thread、sink rejection 或容量失败才是
结构错误。它不取得 `UIContext`、DisplayList builder、Renderer 或 allocator。具体类型应放在
Integration/Render 边界而不是 `Tina::UI` 公共头；root registration 使用 move-only/generation-checked owner，
不保存无生命周期证明的裸全局 callback。

`EngineHost` 在 RenderExtract、UI update 和 DisplayList build 之前打开唯一 `RenderFramePacket`，并把该
packet 的 `resourceSink()` 与 root-scope resolver registry 传给
`PrimaryWindowUIDisplayCoordinator::buildForFrame()`；UI 不再开第二个 packet、第二张 pin 表，也不在
`submitUI()` 中临时解析 Asset。

retained UI 只保存 `AssetId + source rect/texture extent + tint + layout/paint metadata`，不能保存
`AssetHandle`、`AssetLease`、`FrameResourceRef` 或 GPU handle；UI commit 不同步加载资源，也不让
`Tina::UI` 直接依赖 `Tina::Asset`。AssetSystem 是 Game State-owned，因此 resolver 绑定 owning root 的
生命周期和 resolver scope；多个 State/root 可以解析相同 AssetId 到不同资源域，不使用全局 resolver。
frame packet 构建阶段才做有界 lookup、去重和 pin；同一 `(resolver scope, AssetId)` 每帧只解析一次，
随后复用现有 Texture2D frame resource，不增加 UI 专属纹理类型。`FrameResourceKind::Texture2D` 及其
binding SPI 已泛化为 Sprite2D/UI 共用同一 kind/binding table，且未保留 Sprite 专属 kind、`UITexture` 或
compatibility alias；root-scoped resolver、packet pin 与 `ImageQuad` 也已在 A 中落地。

authoring metadata 非法时 descriptor/paint candidate 失败且旧 committed snapshot 继续有效。当前运行时
缺 resolver、missing/not-ready 或实际 extent 不匹配使用 `Skip + counter`；resolver/sink 结构错误与
DisplayList/frame-resource/pin 容量不足在 backend 副作用前让本帧构建原子失败。产品 fallback/strict policy
尚未开放，不能把它写成现有 root API。

bgfx 的 solid-shape/Glyph fragment shader 采样 R8 `.r` coverage，并按每顶点 shape 参数及四角像素半径计算
Rectangle、RoundedRect 或 Ellipse coverage；`ImageQuad` 使用独立 RGBA sampling shader mode/program。command 保存
bounds/可选 exact vertices/shape 参数/UV/tint/clip，batch 保存
packet-local Texture2D ref 与 sampling。batch key 至少包含 `shader mode + texture ref + clip + sampling`，只合并
paint-order 中相邻兼容命令，不为减少 draw call 全局重排 UI。cooked RGBA8 仍按 straight alpha 采样，
fragment 必须先将 sampled RGB 乘 sampled alpha，再乘 committed premultiplied tint，保持现有
`ONE, INV_SRC_ALPHA` 混合契约。DisplayList checksum 对资源只编码按首次 paint appearance 分配的确定性
frame-resource ordinal 与 sampling，不能编码每帧变化的 packet owner/generation 或 backend binding key；
完整 `FrameResourceRef` 仍用于提交期 owner/generation/kind 校验。

NineSlice 的 9 个 quad 在 texture/clip/sampling 相同且 paint-order 连续时合并为一个 batch，并不等于 9 次
draw call；`Q == 0` 时 `B == 0`，否则有 `1 <= B <= Q`，但不能跨中间的不同资源/clip 命令重排来强行
降低 `B`。

首版明确排除 SVG/vector、animated image、远程 URL、UI commit 内 runtime decode、Tile NineSlice、任意
material/shader callback 和任意纹理 wrap 契约；这些能力必须有独立产品需求后再提案。

#### `UI-IMAGE-001` 验收矩阵

A/B 的 authoring、committed paint、root-scoped resolver/pin、DisplayList/backend 与 semantics 自动化证据，
以及 C 的 Product/visual 与 Performance 证据均已落地；下表保留为整个任务的关闭矩阵。

| 边界 | 必须留下的证据 |
| --- | --- |
| Authoring/layout | full texture 与 atlas subrect；Fill/Contain/Cover/None + alignment；强类型 `UIIconContent` 的 Contain/居中默认值、tint/sampling/alignment；intrinsic auto-size；text/image 互斥；invalid extent/source/inset 零 mutation；destroy/build rollback 回收 image slot |
| Committed paint | 显式 `UICommittedPaintKind` 替代 `isGlyph` 布尔扩张；Image/Icon 各 1 entry；NineSlice 1..9 entry；小 destination、零面积 patch、paint capacity failure 保留旧 snapshot |
| Resource lifetime | 两个 root/两个 resolver scope、相同 AssetId 不串域；同 scope 同资源只 resolve/pin 一次；missing/not-ready/wrong-kind/extent mismatch policy；packet complete/abandon 后 pin exactly-once 释放；stale scope fail closed |
| Display/backend | ImageQuad UV/tint/clip/checksum；高对比相邻 atlas cell 的 source-edge/1px gutter 无串色；相邻同 texture/clip/sampling/shader 批合并且不跨 paint order；Linear/Nearest；Null preflight；bgfx RGBA straight-to-premultiplied shader；D3D11/OpenGL/Vulkan cooked program |
| Input/semantics | decorative Icon 默认 `UIPointerHitPolicy::Ignore` 且语义排除；icon-only Button root 是点击/焦点/显式 name owner；有意义 Image role/name；Windows UIA Image ControlType；图片本身不发布无关 action |
| Product/visual | icon-only Button、图文 Button、Inventory thumbnail、NineSlice panel；Dark/Light、atlas subrect、Linear/Nearest、missing/unavailable skip 与 DPI-like size matrix 均有结构化状态和视觉证据 |
| Performance | `ui_image_nineslice_v1` 每 build 固定 `Q=5096/U=64/B=1000`；64 resolve hit、5032 cache dedupe、64 pin acquire/release，missing/not-ready/extent mismatch/resource-intern dedupe 与 allocation delta 为 0；输出稳定 command/batch/resource/pin high-water、checksum 与 provisional CPU 时间 |

### Motion

```cpp
enum class UIEasing : u8 {
    Linear,
    EaseOut,
    EaseInOut,
};

enum class UIAnimatableProperty : u16 {
    BackgroundColor,
    BorderColor,
    TextColor,
    Opacity,
    CornerRadius,
    VisualOffset,
    LayoutWidth,
    LayoutHeight,
    LayoutOffset,
};

struct UITransitionSpec final {
    UIAnimatableProperty property{};
    Core::Duration duration{};
    Core::Duration delay{};
    UIEasing easing{};
};
```

Runtime 将 monotonic time、delta 和 reduced-motion 传给每窗口 UI commit coordinator。每条 active
transition 保存 node/property/start/target/time，状态再次变化时从当前 presentation value retarget。direct
与 Style transition 仍不插值布局属性、不改变 hit/semantics、不延迟 callback；typed keyframe timeline 额外
允许 `LayoutWidth`、`LayoutHeight`、`LayoutOffset` 三项 bounded whitelist。layout/mixed timeline 的 sample
先形成 candidate，再从同一 geometry 构建 Layout/Hit/Paint/Semantics；任一 builder 失败时不发布部分 snapshot
或 presentation，下一次按绝对时间重试。

Direct transition 在 zero-duration/reduced-motion snap 前仍完整校验 property/duration/delay/easing；失败不改
retained target、dirty state 或既有 owner。`CornerRadius` 的 Rectangle 条件是 playback-time capability：Ellipse/
Line 节点可持有或替换 timeline definition，但 direct/play/retarget 原子拒绝，恢复 Rectangle 后才可播放；显式
scalar keyframe-0 允许从非对称 retained radii 启动。`setBoxPaint()` 在 dirty 预检成功后取消 active direct
BackgroundColor/BorderColor/CornerRadius；对应 active timeline 存在时 setter fail closed，必须显式 cancel，
而 Opacity/VisualOffset timeline 不构成 box-paint owner 冲突。

已落地的 stylesheet `BackgroundColor` transition 遵守 ADR 0023：匹配 stateful BoxFill candidate 的节点
在 Style 绑定阶段持久预留 track；运行期启用会先对已有节点做原子容量预检，pseudo-state 变化只激活
已预留槽。reserved 与 active 分开计数/high-water，完成后 reservation 保留到 role 变更、node destroy 或
transition 关闭；reduced-motion 直接发布 target，不占 active list。

## 性能模型与合入门槛

这套设计的目标不是承诺“新增功能零成本”，而是让新增成本可界定、可统计、可在固定 workload 上比较。
以下符号用于描述工作量：`N` 为 committed layout node 数，`P` 为 committed paint entry 数，`D` 为本次
真正变化的节点数，`C` 为一次 Component transaction 的 node slot、text byte、canvas command 与 behavior
slot 工作量之和（实现必须分别报告四类 counter，`C` 只用于复杂度表达），
`R` 为预编译 rule bucket 中的候选规则数，`M` 为 active motion track 数，`Q` 为
Image/Icon/NineSlice 实际展开的 image quad 数，`U` 为本帧唯一 `(resolver scope, AssetId)` 数，`B` 为
相邻合并后 RGBA image batch 数。

### 当前成本与目标上界

| 场景 | 当前或目标工作量 | 明确禁止 |
| --- | --- | --- |
| clean `UIContext` commit | 当前只检查 dirty phase/viewport 后直接返回，UI commit 近似 `O(1)`；Render bridge 仍需按帧消费 committed paint，不能把整个 UI render 误写成 `O(1)` | clean frame 重新 Measure/Arrange、Hit、Paint snapshot 或 Style resolve |
| 单节点 hover/pressed/focus paint 变化 | 当前不会触发 Layout/Hit；dirty paint cache 只重算变化节点，但 candidate 容量校验与 committed paint 组装仍线性遍历，整体按 `O(N + P)` 记录 | 将局部 paint 状态扩大为 Structure/Measure/Arrange dirty，或声称当前完整 publication 是 `O(D)` |
| Component build | 目标为创建期 `O(C)`；node/text/canvas/behavior reservation 分别计数，commit 后只留下 Element 与 side-state slot | retained wrapper object、每节点 heap/vtable、每帧重放 recipe |
| 单节点 Style resolve / ColorToken update | node-local state resolve 为 `O(R)` 的 `role + fixed class set + state mask` bucket lookup；token update 经固定 reverse-dependency 链为 `O(affected links)`，并以四个 `lastStyleTokenUpdate*` counter 记录依赖链工作量 | descendant/ancestor selector、运行时字符串 CSS、局部 state 扫描整棵树 |
| Pointer hit/route | 当前 point query 最坏反向扫描 `O(H)` 个 committed hit entry；找到 target 后 route 按 ancestry depth/listener 数工作，且不 layout、不分配 | 把当前 point query 宣称为常数时间；没有 workload 证据就提前引入复杂空间索引 |
| active Motion | direct transition sample 为 `O(M)`；keyframe timeline 只遍历 compact active-index 的 timeline/track 与实际 segment，分别报告 sampled timeline/track/segment；paint timeline 每帧发布 Paint，layout timeline 每帧以一次事务发布 Layout/Hit/Paint，definition high-water 独立于 active set | 无 active track 仍使 Paint dirty、扫描全部 definition/node 寻找动画、部分发布 layout/hit/paint、白名单外 layout 插值 |
| Image/Icon/NineSlice | paint 展开为 `O(Q)`，资源工作为 `O(U)`；Image/Icon 均为 1 quad，NineSlice 每个命令最多 9 quad，完整容量预检后一次发布 | UI commit 同步 Asset I/O、每 quad 重复 resolve/pin、未 pin 的 GPU handle、容量不足时截断半个 NineSlice、为 batch 全局重排 paint order |
| 虚拟 List/Tree | warmup 后按固定 row pool/可见行工作，不按 100k logical item 全量 materialize | 滚动时增长 row storage 或遍历全部 logical item |

当前 paint candidate 的线性 publication 是已知事实，不是由 Style/Component 新设计新增的隐藏成本。Motion
会把原本偶发的 paint commit 变成连续多帧，因此 `ui_motion_v1` / `ui_motion_timeline_v1` /
`ui_motion_layout_v1` 不能只证明动画正确，还必须同时输出 `N/P/M`、timeline/track/keyframe/active-index
high-water、sampled segment 与 publication；layout workload 额外证明每个 active sample 只发生一次原子
Layout/Hit/Paint publication；
若固定 workload 超出受审预算，应先优化 paint candidate 构建，再扩大
默认 Motion 容量，不能通过减少工作、跳过原子 publication 或放宽 checksum 掩盖问题。

即使 UI tree 是 clean，Image 的 packet-local ref/pin 也不能跨帧缓存，因此 Render bridge 每帧仍支付
`O(Q + U + B)` 的投影/命令、唯一资源 lookup/pin 与 batch 成本；这与现有 Render bridge 每帧消费 committed
paint 的事实一致，不会把 clean `UIContext` commit 变成 Layout/Paint rebuild。性能目标是同 scope 资源去重、
固定容量零隐式分配和相邻 batch 合并，而不是宣称图片渲染零成本或把 FrameResourceRef 留到下一帧。

### 实现约束

1. Behavior side store 使用固定容量、node/capability 直接索引；拆分 `UIContext::Impl` 只是职责组织，不能
   引入 per-node allocation、RTTI、虚调用或 Service Locator；
2. Component builder 只存在于 `onEnter/updateUI` authoring phase，不能成为 retained wrapper 或每帧更新树；
3. StyleSheet 在 startup 注册并预编译；节点只保存强类型 ID、固定 class set 与 resolved/presentation 数据；
4. local state/property dirty 必须由静态 metadata 精确映射到 Paint、Hit、Semantics 或 Layout，paint-only
   状态不得触发 Measure/Arrange；
5. Motion store 维护紧凑 active list 和 high-water；`M == 0` 时不得产生额外 snapshot rebuild；
6. UIFlow 通过 activate/deactivate retained root 与 Layer Stack 切换页面，禁止每帧 destroy/rebuild screen；
7. 所有新增容量在 Context Create/startup 固定，容量不足保持事务失败和旧 committed snapshot，不允许 heap
   fallback；
8. Image resolve 发生在 frame packet 构建阶段，只做 root-scoped 有界 lookup/dedupe/pin，不在 UI commit 内
   等待磁盘或网络；Image/Icon/NineSlice 复用同一 Texture2D frame-resource 与 RGBA ImageQuad 路径；
9. `UICommittedPaintEntry` 的图片路径使用显式 kind，没有在旧 `isGlyph` 上叠加 `isImage/isNineSlice`
   布尔组合；NineSlice 在进入 DisplayList 前展开，DisplayList 不保留 NineSlice 专用 command。
10. Line 每条线段只消耗一个 retained paint entry 和一个 DisplayList quad；Ellipse 每个填充或描边只消耗
    一个 retained paint entry 和一个 DisplayList command，不得恢复按像素阶梯或多段弦近似。

### `UI-PERF-001` workload

在固定机 hard gate `PERF-002` 完成前，UI workload 先以 `provisional` 方式记录趋势，但 checksum、工作量
counter、容量/分配不变量可以立即作为确定性门禁：

| Workload | 固定输入 | 必须输出/断言 |
| --- | --- | --- |
| `ui_static_commit_v1` | 4096-node committed tree，warmup 后不 mutation | layout/measured/arranged/hit/paint rebuild 均为 0；PMR allocation delta、capacity/high-water、DisplayList checksum |
| `ui_paint_dirty_v1` | 4096 nodes，每帧只修改 1 个 leaf paint state | layout/hit rebuild 为 0；paint cache rebuild、snapshot inspected/published entry、`N/P` 与 checksum |
| `ui_route_v1` | 4096 hit entries、route depth 64；固定 target/miss/capture 序列 | visited hit entries、route path depth、listener calls、consume/claim checksum；allocation delta 为 0 |
| `ui_virtual_collection_v1` | 100k logical items、固定 64-row pool 与 scroll sequence | materialized row/high-water 稳定、warmup 后 storage 不增长、selection/semantics checksum |
| `ui_component_build_v1` | 256 个四节点 Component；固定 text/canvas payload 与 Activate/Toggle/Range/TextInput/Scroll/Selection slot mix | build/commit 时间、node/text-byte/canvas/behavior 的 reserved/published counter、各 pool high-water、allocation delta 与 tree checksum；commit 后无 retained wrapper，后续 clean commit rebuild 为 0 |
| `ui_style_state_v1` | 4096 nodes、256 rules、每节点最多 4 classes | inspected/resolved nodes、candidate rules、bucket/class-link high-water；单节点 state change 不 layout/hit/全树 resolve，clean commit 为零 |
| `ui_image_nineslice_v1` | 256 Image + 232 Icon + 512 full NineSlice、64 unique `(resolver scope, AssetId)` | 每 build `Q=5096/U=64/B=1000`；64 resolve hit、5032 cache dedupe、64 pin acquire/release；错误计数与 allocation delta 为 0，high-water/checksum 稳定 |
| `ui_motion_v1` | 4096 nodes、active tracks 分别为 0/64/1024、固定 clock | sampled=`M`、active/high-water；0 active 时 motion work/额外 dirty 为 0；layout/hit rebuild 为 0 |
| `ui_motion_timeline_v1` | 4096 nodes、256 retained definition/1024 track/4096 keyframe，seed 播放 0/16/256 active timeline | full definition 与 compact active-index high-water、sampled timeline/track/segment；paint-only sample 不重建 Layout/Hit，allocation delta 为 0 |
| `ui_motion_layout_v1` | 4096 Overlay nodes、同样的 retained definition 容量与 seed | active sample 的 layout track 等于 sampled track，且每 iteration 恰好一次 Layout/Hit/Paint publication；active=0 不 rebuild，failure/allocation delta 为 0 |

绝对毫秒阈值、median/MAD 与受审机器 baseline 仍由 `PERF-002` 冻结；开发机和共享 CI 不得用一次墙钟
结果宣称“无性能回归”。详细测量口径见[性能与内存](performance-memory.md)。

## 不可破坏的契约

所有后续扩展必须继续满足：

1. `UIContext` per-window、owner-thread，Game State 不获取裸 Context；
2. storage 在 Create/startup 固定容量，稳态不隐式 heap fallback；
3. 输入只读取上一份 committed hit/focus/modal；
4. structure/layout/hit/paint/semantics 候选全部成功后才原子发布；
5. UI 只输出 backend-neutral paint，不公开 bgfx/texture/shader callback；
6. public UTF-8 严格校验，text/canvas borrowed payload 在创建期间复制；
7. accessibility action 只执行 committed semantics 已发布的 action；
8. Motion 只改变 presentation，不建立第二套业务状态或输入时序。

## 落地顺序

主 UI 扩展链：

1. `UI-STATE-FEEDBACK`：源码、测试与 Dark/Light 产品视觉证据已关闭；
2. ADR 0023 已接受，容量单位、失败语义和性能 workload 已冻结；
3. `UI-PERF-001` Done：static/paint-dirty/route/virtual-collection 与通用 counter/checksum 首个 milestone
   完成后，Image/Component/Style/Motion 均已扩展同一协议；
4. `UI-IMAGE-001` 与 `UI-COMPONENT-001` 两个并行分支均已完成；前者不依赖 Behavior side store：
   - A Done：Image/Icon content、atlas/tint/fit/sampling、root-scoped resolve/pin、RGBA ImageQuad 和 Image semantics；
   - B Done：同源 NineSlice、1..9 quad 原子展开、小 destination/inset/clip/fractional-DPI 规则；
   - C Done：icon-only/图文 Button、Inventory thumbnail、NineSlice panel、Dark/Light/atlas/sampling、missing/unavailable/resource invalidation、6-case DPI-like size matrix 与 `ui_image_nineslice_v1` benchmark 全部关闭；
5. `UI-COMPONENT-001` Done：Runtime bounded transaction、六类 Behavior side store、node/text/canvas/Behavior
   统一 reservation/counter 与冻结的 `ui_component_build_v1` 已落地；
6. `UI-STYLE-001` Done：fixed-capacity style kernel、Context class/token capacity 与 startup install、
    ColorToken registry/value 与运行期 reverse-dependency getter/setter、每节点最多 4 个 class link、retained
    state + literal/token-backed resolved BoxFill paint cache、Runtime facade，以及 `ui_style_state_v1` 已落地；
   stylesheet imageTint token、showcase Integration、属性 dirty metadata
   （`UIStylePropertyKind` + Context 分发）、Visual ROI 门禁 `RunUiStyleVisualGate.ps1` 已落地；
7. `UI-MOTION-001` Done：paint-only transition（color/opacity/radius/visual-offset）、Style BackgroundColor
   persistent reservation/activation + `ui_motion_v1`。
8. `UI-MOTION-002` Done：ADR 0026 Accepted；Context-owned typed keyframe timeline、generation/owner identity、
   四类固定容量、retarget/reduced-motion/失败原子性、Runtime facade、paint-only `ui_motion_timeline_v1`，以及
   `LayoutWidth`/`LayoutHeight`/`LayoutOffset` 的原子 Layout/Hit/Paint `ui_motion_layout_v1` 均已接线。
   2026-08-16 定向 gate 为 UI 28/28、Runtime facade 1/1、bench unit 10/10，两个 workload 的 seed 0/1/2
   均以 `warmup=30,samples=120` 通过；墙钟仍为 provisional，固定机 hard gate 由 `PERF-002` 跟踪。

独立或后置 lane：

- `UI-RANGE-INPUT-KEYBOARD` 已关闭：它只依赖 Slider Focusable 子切片与 Runtime input route，不依赖
  ADR 0023；capability-shaped 调值 command 与 fixed-capacity exact-control Down/Up latch 不复用空间焦点状态；
- `SDK-001` 的 GameSDK、PlatformGlfw、DesktopBootstrap/RenderBgfx、UIFreetype、AudioMiniaudio 安装、
  Windows/Linux moved-prefix 与 Ubuntu producer → Debian consumer gate 已落地；ADR 0024 已 Accepted，
  strict exact-version 正反 probe（含 tweak/range 拒绝）已接入。正式 supported tuple 的 baseline/object probe 独立跟踪；此后每个新增公共 UI 切片同步增加 consumer 覆盖；
- `UI-FLOW-001` Done：首切片复用 retained node，提供固定容量
  Layer/Screen 注册、push/pop/replace、栈顶 publication、失败原子性与生命周期回收；第二切片已增加
  topmost Screen 的 Back callback、Dropdown-first Escape/Gamepad East 路由与 Down/Up gameplay suppression；
  第三切片增加设备 revision、断连回落与 2D Pause 真实提示；第四切片增加
  Confirm callback、focused-control-first Enter/Keypad Enter/Gamepad South 路由；第五切片增加 Menu callback、
  TextEdit-first printable P、P/Gamepad Start 路由、Base Menu 打开 Pause、Pause Menu 恢复，以及
  `ESC / ENTER / P`、`B / A / START` 产品提示；最终切片增加固定 16 槽 `UIFlowLocalUserId`、Primary=1、
  键鼠固定 Primary、完整 generation Gamepad assignment、per-user 设备状态、action event 用户来源、
  重分配 release latch 保留与断连/reset assignment/latch 清理；Screen 栈与 focus 继续保持窗口级唯一；
- `UI-BEHAVIOR-SPI-001` 只有标准 Behavior 无法满足有证据的插件场景时才冻结高级 SPI。

详细状态和验收条件只在 [Backlog](backlog.md)维护。
