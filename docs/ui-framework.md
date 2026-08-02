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

其中 Image/Icon/NineSlice 不是装饰性补充，而是 HUD、Inventory、装备栏、技能栏、对话框和设置页的
基础视觉能力。它不依赖 Behavior side store 或 Component transaction 才能成立：`UI-PERF-001` 建立首份
计数协议后，Image 主线可与 Component 主线并行。当前 Image/Icon、NineSlice A/B 与 C 产品采用、失效、
尺寸矩阵及 `ui_image_nineslice_v1` benchmark 已全部交付，`UI-IMAGE-001` 已关闭。Icon 不另建一套控件或
渲染协议，而是 Image 的 atlas-source、tint 和默认布局 profile。

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
  routed input + focus/modal/pointer capture
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
  UIDisplayList SolidQuad/Glyph/ImageQuad + glyph atlas + packet-local Texture2D refs
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
| Paint | `include/tina/ui/UIImageSource.hpp`、`UIImage.hpp` 与 `UIPaint.hpp`；Image content 和 Canvas `SolidRect`/`Image`/`NineSlice` |
| Render bridge | `include/tina/integration/UIRenderDisplayList.hpp` |
| DisplayList | `include/tina/render/UIDisplayList.hpp`；当前有 `SolidQuad`、`Glyph` 与 `ImageQuad` |
| Runtime 帧序 | `src/runtime/EngineHost.cpp`；`updateUI()` 后统一 `commitForFrame()` |

### Element 不是传统 Widget 对象

`UIElementDescriptor` 一次描述 Layout、Content、Visual、Behavior、Semantics、Hit 与 Focus policy。Button
recipe 当前等价于：

```cpp
UIElementDescriptor button = makeButtonElement("Apply", layout);

// recipe 已提供：
// contentAlignment = Center
// styleRole       = ButtonPrimary
// behaviors       = Focusable | Activate
// semantics       = Button + Focus | Activate
```

游戏保存 `UINodeId`，不保存 `Widget*`。节点状态由 `UIContext` 的固定容量 store 拥有；generation/owner
校验拒绝 stale 或跨窗口 ID。多节点组件由 `UIElementBuildTransaction` 先声明 node budget，任一步失败时
回滚整个子树及其 text/canvas storage。

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

Button 现在已经有 hover、focused、pressed、disabled 反馈。pressed 会交换亮/暗 border 并将 shadow offset
设为零，表现出按下深度。这些状态直接选择新的 paint 值，不存在 duration、easing 或插值，所以它是
“点击效果”，还不是“点击动画”。

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
- 注册 routed pointer listener，使用 Button/Slider/selection 等已有 callback；
- 使用 Image/Icon content，以及 `SolidRect`/`Image`/Stretch-only `NineSlice` Canvas 组合 backend-neutral 图形。
- 通过安装目录使用 backend-neutral `Tina::GameSDK`、独立 `PlatformGlfw`、DesktopBootstrap/RenderBgfx
  与可选 UIFreetype component；GameSDK-only 不加载 Desktop adapter。

### 还不可以

- 依赖跨发行版 artifact transfer 或正式 ABI/兼容策略；AudioMiniaudio 安装 adapter 与
  Windows/Linux moved-prefix consumer 已可用；
- 注册任意新 Widget class 或 Behavior state machine；
- 定义用户 StyleClass、selector、pseudo-state stylesheet；
- 从 UI 持有 AssetHandle/Lease、FrameResourceRef 或 texture/bgfx handle；
- 声明 transition、tween、timeline 或 keyframe animation；
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
增加新的继承层。Runtime facade 已提供 phase-scoped、node-bounded component transaction 并保证失败时整棵
回滚；text/canvas/各 Behavior pool 的统一预留仍由 `UI-COMPONENT-001` 后续切片补齐。

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
|- Presentation
|  |- Theme tokens
|  |- StyleRole + user StyleClass
|  |- node-local pseudo-state resolver
|  |- Rect / Image (including Icon) / NineSlice / Glyph / Clip
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
struct UIStyleRoleId  { u32 value{}; };
struct UIStyleClassId { u32 value{}; };
struct UIStyleTokenId { u32 value{}; };

enum class UIStyleState : u16 {
    Hovered,
    Pressed,
    Focused,
    Disabled,
    Checked,
    Selected,
    Open,
    Dragging,
};
```

第一版 selector 只支持当前节点的 `role + class + state mask`，不支持 descendant、`nth-child`、运行时
CSS parser 或任意 specificity。推荐优先级：

```text
built-in recipe
  < product stylesheet
  < role/class/state rule
  < local inline override
```

每个属性静态声明 dirty 影响。颜色/opacity 只触发 Paint，font size/padding 触发 Measure/Arrange，hit policy
触发 Hit，语义属性触发 Semantics。目标实现可用固定容量 token reverse-dependency link 只重解析引用了变化
token 的节点；若首切片尚未建立该索引，Theme swap 必须明确按 `O(N)` 扫描并输出 inspected/resolved-node
counter，不能在没有索引和容量定义时宣称只访问受影响节点。

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

enum class UICanvasCommandKind : u8 {
    SolidRect,
    Image,
    NineSlice,
};

struct UIImagePixelInsets final {
    u32 left{};
    u32 top{};
    u32 right{};
    u32 bottom{};
};

struct UICanvasCommand final {
    UICanvasCommandKind kind = UICanvasCommandKind::SolidRect;
    UILogicalRect bounds{};
    UIStraightSrgba8Color color{}; // Solid fill or image tint.
    float cornerRadius = 0.0F;     // SolidRect only.
    UIImageSource imageSource{};
    UIImagePixelInsets imageSourceInsets{};
    UIEdgeSpacing imageDestinationInsets{};
    UIImageSampling imageSampling = UIImageSampling::Linear;
};
```

以上是当前公开契约的简化摘录，精确签名以 `UIImageSource.hpp`、`UIImage.hpp`、`UIPaint.hpp` 和
`UIElement.hpp` 为准。`UIImageContent` 是第一类 Element content：

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

Icon 只是 Image 的官方 authoring profile。`makeIconElement()` 可以默认 `Contain`、可 tint、固定 logical
size、`UIPointerHitPolicy::Ignore` 和 decorative semantics，但它仍创建普通 Element，最终只产生一个
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
UIImageContent / Canvas Image / Canvas NineSlice
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
| `Tina::UI` | `UIImageSource/Content/NineSlice` authoring、intrinsic layout、fit/crop 与 NineSlice logical patch 展开、committed Image paint；root 只携带 opaque resolver-scope id，只依赖 Core AssetId，不认识 AssetSystem/Render/backend |
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

bgfx 的 Solid/Glyph fragment shader 只采样 R8 `.r` coverage，`ImageQuad` 已选择独立 RGBA sampling
shader mode/program；command 保存 bounds/UV/tint/clip，batch 保存
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
| Authoring/layout | full texture 与 atlas subrect；Fill/Contain/Cover/None + alignment；intrinsic auto-size；text/image 互斥；invalid extent/source/inset 零 mutation；destroy/build rollback 回收 image slot |
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
};

struct UITransitionSpec final {
    UIAnimatableProperty property{};
    Core::Duration duration{};
    Core::Duration delay{};
    UIEasing easing{};
};
```

Runtime 将 monotonic time、delta 和 reduced-motion 传给每窗口 UI commit coordinator。每条 active
transition 保存 node/property/start/target/time，状态再次变化时从当前 presentation value retarget。首版
不插值布局属性，不改变 hit/semantics，不延迟 callback。

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
| 单节点 Style resolve | 目标为 `O(R)` 的 node-local `role + fixed class set + state mask` bucket lookup；Theme swap 在没有固定 reverse-dependency index 时如实记为 `O(N)`，有索引后按受影响 link 处理 | descendant/ancestor selector、运行时字符串 CSS、局部状态变化时扫描整棵树或全规则表 |
| Pointer hit/route | 当前 point query 最坏反向扫描 `O(H)` 个 committed hit entry；找到 target 后 route 按 ancestry depth/listener 数工作，且不 layout、不分配 | 把当前 point query 宣称为常数时间；没有 workload 证据就提前引入复杂空间索引 |
| active Motion | sample 目标为 `O(M)`，只遍历 active track；在当前 paint compiler 下，每个动画帧还必须如实计入 `O(N + P)` publication 成本 | 无 active track 仍使 Paint dirty、每帧扫描全部节点寻找动画、插值 Layout/Hit 属性 |
| Image/Icon/NineSlice | paint 展开为 `O(Q)`，资源工作为 `O(U)`；Image/Icon 均为 1 quad，NineSlice 每个命令最多 9 quad，完整容量预检后一次发布 | UI commit 同步 Asset I/O、每 quad 重复 resolve/pin、未 pin 的 GPU handle、容量不足时截断半个 NineSlice、为 batch 全局重排 paint order |
| 虚拟 List/Tree | warmup 后按固定 row pool/可见行工作，不按 100k logical item 全量 materialize | 滚动时增长 row storage 或遍历全部 logical item |

当前 paint candidate 的线性 publication 是已知事实，不是由 Style/Component 新设计新增的隐藏成本。Motion
会把原本偶发的 paint commit 变成连续多帧，因此 `UI-MOTION-001` 不能只证明动画正确，还必须同时输出
`N/P/M` 与 paint publication 计数；若固定 workload 超出受审预算，应先优化 paint candidate 构建，再扩大
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
| `ui_style_state_v1` | 4096 nodes、256 rules、每节点最多 4 classes | resolved nodes、candidate rules、bucket high-water；单节点 state change 不 layout/hit/全树 resolve |
| `ui_image_nineslice_v1` | 256 Image + 232 Icon + 512 full NineSlice、64 unique `(resolver scope, AssetId)` | 每 build `Q=5096/U=64/B=1000`；64 resolve hit、5032 cache dedupe、64 pin acquire/release；错误计数与 allocation delta 为 0，high-water/checksum 稳定 |
| `ui_motion_v1` | 4096 nodes、active tracks 分别为 0/64/1024、固定 clock | sampled=`M`、active/high-water；0 active 时 motion work/额外 dirty 为 0；layout/hit rebuild 为 0 |

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
3. `UI-PERF-001`：static/paint-dirty/route/virtual-collection 与通用 counter/checksum 首个 milestone
   已完成，任务转为 `InProgress` 并解锁 Image/Component；后续每个垂直切片继续扩展同一协议；
4. `UI-IMAGE-001` 与 `UI-COMPONENT-001` 是两个可并行分支；前者不依赖 Behavior side store：
   - A Done：Image/Icon content、atlas/tint/fit/sampling、root-scoped resolve/pin、RGBA ImageQuad 和 Image semantics；
   - B Done：同源 NineSlice、1..9 quad 原子展开、小 destination/inset/clip/fractional-DPI 规则；
   - C Done：icon-only/图文 Button、Inventory thumbnail、NineSlice panel、Dark/Light/atlas/sampling、missing/unavailable/resource invalidation、6-case DPI-like size matrix 与 `ui_image_nineslice_v1` benchmark 全部关闭；
5. `UI-COMPONENT-001`：Runtime bounded component transaction 与 Activate/Toggle/RangeInput/TextInput/Scroll/Select side store 切片已完成；
   `ui_component_build_activate_toggle_v1` 已为 Activate/Toggle 提供诚实的 256×4 前置 benchmark；后续补
   统一 reservation/counter 后才发布冻结的 `ui_component_build_v1`；
6. `UI-STYLE-001`：等待 Image 与 Component 两条目标属性面稳定后，开放强类型 StyleClass/token 与
   node-local pseudo-state stylesheet，并把 image tint/opacity 纳入 dirty metadata；
7. `UI-MOTION-001`：在稳定 VisualState/Style target 上增加 paint-only transition，并验证连续 paint 成本。

独立或后置 lane：

- `UI-RANGE-INPUT-KEYBOARD` 已关闭：它只依赖 Slider Focusable 子切片与 Runtime input route，不依赖
  ADR 0023；capability-shaped 调值 command 与 fixed-capacity exact-control Down/Up latch 不复用空间焦点状态；
- `SDK-001` 的 GameSDK、PlatformGlfw、DesktopBootstrap/RenderBgfx、UIFreetype、AudioMiniaudio 安装与
  Windows/Linux moved-prefix 切片已落地，待跨发行版 artifact transfer 与正式 ABI/兼容策略；此后每个新增公共 UI 切片同步增加
  consumer 覆盖；
- `UI-FLOW-001` 只有真实页面栈需求后才增加 Activatable Screen/Layer Stack/Action Router；
- `UI-BEHAVIOR-SPI-001` 只有标准 Behavior 无法满足有证据的插件场景时才冻结高级 SPI。

详细状态和验收条件只在 [Backlog](backlog.md)维护。
