# Public API

本文描述当前 `include/tina` 公共面和 CMake target。它不是未来 SDK 愿望清单；尚未存在的能力（通用
event queue、通用 GPU submission fence 等）列在末尾。State 栈、FramePin、startup-only shadow extent 配置与 present-return CPU completion
首切片**已经存在**。

## 分层

| 层 | 使用者 | 入口 | 约束 |
| --- | --- | --- | --- |
| Game API | 普通游戏/样例 | `Tina::DesktopBootstrap` + Runtime/Scene/Asset/UI 等 Tina 模块 | 不接触具体 backend owner/native handle |
| Module API/SPI | Tina 模块与高级集成 | `include/tina/<module>`、`runtime/spi`、`integration` | 只暴露 Tina-owned 类型和窄 factory |
| Backend Private | GLFW/bgfx/FreeType/miniaudio/Box2D/cgltf/stb_image/MikkTSpace | `src/...` adapter/Cooker 实现 | 第三方类型与宏不进入公共头；实现链接留在最窄 target |

当前 backend-neutral/Null SDK 可通过安装前缀中的版本化 `TinaConfig.cmake` 使用：

```cmake
find_package(Tina CONFIG REQUIRED)
target_link_libraries(game PRIVATE Tina::GameSDK)
```

启用 GLFW 的安装图还可显式请求 adapter component：

```cmake
find_package(Tina CONFIG REQUIRED COMPONENTS PlatformGlfw)
target_link_libraries(platform_tool PRIVATE Tina::PlatformGlfw)
```

Desktop 游戏只请求组合 component：

```cmake
find_package(Tina CONFIG REQUIRED COMPONENTS DesktopBootstrap)
target_link_libraries(game PRIVATE Tina::DesktopBootstrap)
```

高级音频集成可独立请求 miniaudio adapter：

```cmake
find_package(Tina CONFIG REQUIRED COMPONENTS AudioMiniaudio)
target_link_libraries(audio_tool PRIVATE Tina::AudioMiniaudio)
```

`Tina::GameSDK` 聚合下表中的 backend-neutral Runtime、Scene、Asset、UI、Audio 等稳定模块；安装 package
声明 `xxHash`、Asset Cooker 使用的 `mikktspace`（以及启用 Physics2D 时的 `box2d`）依赖；Tracy Profile
package 还解析由 Core 固定选择的 Tracy 0.13.1 内部链接闭包。这些
package target 只关闭静态库链接闭包，不把第三方类型暴露到 Tina 公共头。Windows 与 Linux 外部 headless
consumer 已经只通过安装前缀完成 configure/build/run，并复用同一安装头第三方 token 扫描。`PlatformGlfw`
component 通过 `find_dependency(glfw3 3.4 CONFIG)` 解析实现闭包并加载独立 adapter export；未请求该
component 时不会加载 GLFW 依赖或定义 `Tina::PlatformGlfw`。Windows 与 Linux/Xvfb consumer 会创建隐藏窗口、
读取初始 metrics 并 poll 一帧；它不进入 `Tina::GameSDK` 聚合。`DesktopBootstrap` 自动加载
`PlatformGlfw`、`RenderBgfx`，并在安装图启用 FreeType 时加载可选 `UIFreetype`。RenderBgfx 的同一 prefix
只携带 `bgfx`/`bx`/`bimg` runtime targets、archives 与 headers，不安装 shaderc、图片 codec 或离线工具。
`AudioMiniaudio` 将 miniaudio 实现静态编入 adapter，不传播其 header；Linux consumer 解析 `Threads`，启用
Vorbis/Opus 的安装图还分别解析 `Vorbis`、`Opus`、`OpusFile`。未请求该 component 时不加载这些依赖。

## CMake targets

| Target | 公共角色 |
| --- | --- |
| `Tina::GameSDK` | backend-neutral Game SDK 聚合 target；不包含 Desktop/backend adapter |
| `Tina::Core` | Result、time、memory、ID/hash、UTF-8、IO、diagnostics、compile-time Trace frontend |
| `Tina::Platform` | Window/Input/PlatformFrame/backend SPI |
| `Tina::PlatformGlfw` | optional installed GLFW Platform adapter；需 `COMPONENTS PlatformGlfw` |
| `Tina::Task` | bounded IO/CPU/Main TaskSystem |
| `Tina::Render` | RenderDevice、Surface/Frame/Scene/UI DisplayList、GPU IDs |
| `Tina::RenderBgfx` | optional installed bgfx Render adapter；需 `COMPONENTS RenderBgfx` |
| `Tina::Runtime` | EngineHost、Game Application/State、phase context、Action/Event facade |
| `Tina::DesktopBootstrap` | optional installed Windows/Linux Desktop 组合入口；需 `COMPONENTS DesktopBootstrap` |
| `Tina::Scene` | World/Entity/Transform、2D/3D components/extraction/Prefab、World2D snapshot、standalone Particle/Trail、`Fx2D` factory、`CameraFollow2D` |
| `Tina::Navigation2D` | immutable weighted grid、generation dynamic blocker、确定性四向/对角同步与分步 A* |
| `Tina::AssetFormat` | versioned Cooked payload/manifest types |
| `Tina::Editor` | 工具侧 validated World2D/World3D/TileMap/SpriteAnimationClip/Navigation2D/Fx2D authoring document、Project Asset index、project workspace/空目录创建、document-tab navigation、bounded revision history、文件加载/原子保存与 runtime/cook preview；不由 `Tina::GameSDK` 聚合链接 |
| `Tina::Asset` | Catalog、AssetSystem、Handle/Lease、Cooker helpers、typed parse/upload、Sprite2D/Mesh3D binding registry |
| `Tina::UI` | retained Element tree、layout/input/paint、text、semantics |
| `Tina::UIFreetype` | optional installed FreeType text rasterizer adapter；需 `COMPONENTS UIFreetype` |
| `Tina::Audio` | backend-neutral AudioEngine/PCM、voice gain/pitch/pan/fade |
| `Tina::AudioMiniaudio` | optional installed miniaudio device/decode adapter；需 `COMPONENTS AudioMiniaudio` |
| `Tina::Physics2D` | optional Box2D-backed Box/Circle/Capsule/ConvexPolygon/Chain 与 Distance/Revolute/Prismatic API |

Adapter targets `Tina::PlatformGlfw`、`Tina::RenderBgfx`、`Tina::UIFreetype`、
`Tina::AudioMiniaudio` 主要用于 bootstrap/高级组合，不把第三方 header 传播给调用方；安装 package 按构建图
条件导出四个 adapter 和 `Tina::DesktopBootstrap`。`Tina::TraceTracy` 只作为 Tracy Profile package 内
`Tina::Core` 的静态链接闭包存在，不是可请求 component，也不进入 `Tina_ADAPTER_TARGETS`。

## Core 约定

- 所有可恢复模块边界使用 `Core::Result<T>`/`Core::Status`；
- `Error` 提供稳定 domain/code、UTF-8 message、origin、native code 与 context；
- 公共文本/路径是 strict UTF-8；Windows 转换留在 adapter；
- generation ID 与 `AssetId`/`ContentHash` 是不同类型，不隐式转换；
- callback-only view/span/string_view 必须注明失效点；
- `<tina/core/trace/Trace.hpp>` 只提供 `TINA_TRACE_ZONE(nameLiteral)`；None backend 不求值参数、
  不构造对象、不调用函数、不分配内存且没有全局状态；可选 Tracy Profile backend 由构建图唯一选择。

公开头不允许依赖传递 include 才能编译；每个重要头有 header-isolation translation unit。

Trace frontend 是可供 Runtime/Game SDK consumer 编译的 Tina-owned 公共面，不暴露 Tracy token、类型或
include。启用 Profile backend 时，公共头只实例化具有 64-byte opaque storage 的 Tina-owned RAII zone，
第三方对象由 `Tina::TraceTracy` adapter 在该 storage 内构造和销毁；None 仍完全编译消失。zone name 必须是
string literal，宏传递静态长度与 call-site `SourceLocation`。首切片没有公共 session/capture API。

## Engine 与游戏入口

### 正确姿势（普通桌面游戏）

```cpp
// 1) 实现 IGameApplication：createInitialState + onShutdown
// 2) 实现 IGameState：onEnter/onExit/initialPolicy + 需要的相位
// 3) 组合并运行（不要自建主循环）
auto host = Tina::Desktop::CreateEngine(config);
if (!host) { /* handle error */ }
return host.value()->run(application);
```

- 帧逻辑只写在 `IGameState` 相位里；Application 不做逐帧工作。
- 优先 `Desktop::CreateEngine`；手写 `EngineCompositionFactories` 是测试/Headless/特殊集成的接线税。
- 不取得 `IRenderDevice*`、不缓存 phase Context/writer/span 跨回调。
- AssetSystem / Scene::World / ParticleSystem2D / Trail2D / Physics2D 由游戏 State（或样例）显式持有，
  不是 Host 内置模块。

普通桌面游戏调用：

```cpp
Core::Result<std::unique_ptr<EngineHost>>
Tina::Desktop::CreateEngine(const EngineConfig& config) noexcept;

Core::Result<std::unique_ptr<EngineHost>>
Tina::Desktop::CreateEngine(const EngineConfig& config,
                            Desktop::CreateEngineOptions options) noexcept;
```

`CreateEngineOptions::wrapWindowSurfaceRenderDevice` 可在产品/门禁路径包装已创建的
`IRenderDevice`（例如帧捕获装饰器），不暴露 bgfx/GLFW，也不替代 EngineHost 组合根。
`CreateEngineOptions::followSystemColorScheme` 默认 `false`；显式开启后，Desktop 私有 adapter 发布
Tina-owned Dark/Light preference event，Runtime 在 owner thread 的 UI Update phase 把它转换为与当前
density 相同的 canonical `UITheme`。无系统 observer、查询失败或 Headless 图都保留应用显式 Theme。

高级测试/集成可使用：

```cpp
EngineHost::Create(const EngineConfig&, EngineCompositionFactories) noexcept;
```

`EngineCompositionFactories` 提供 Clock、Task、Platform/Render tagged composition、可选 Audio 与可选
primary UIContext factory。普通游戏应优先 `Desktop::CreateEngine`，不应动态拼装 native surface
backend，也不取得 `IRenderDevice*`。

`EngineHost` 在创建线程拥有全部 Runtime module，`run()` 只允许一次。跨线程 run 返回错误；错误线程
析构带 native owner 的 Host 会终止，避免在错误线程调用平台 API。

`EngineConfig::shutdownDeadline` 默认5秒，必须是 finite positive `Core::Duration`。它只预算
`EngineHost` 关闭过程中 `ITaskSystem::shutdownAndJoinFor()` 的 Worker-exit/join 阶段，不覆盖此前的
`AudioEngine::shutdown()`、`IRenderDevice::shutdown()`，也不是整个 Host shutdown 的总耗时上限。若该
TaskSystem 阶段返回 `TaskErrorCode::WaitTimeout`，Host 先写入 `runtime.lifecycle` Diagnostics，再
`std::terminate()`；不会 reset TaskSystem 或继续析构 Platform、Clock、Diagnostics 等剩余 owner。

`EngineConfig::renderMsaaSamples`（`0` 默认关闭，或 `2/4/8/16`）在启动时选择 backbuffer MSAA 采样数，
非法值 fail closed；它是 device-lifetime 配置，不支持热改。像素证据 gate 与 samples 保持 `0`。

## `IGameApplication` 与 `IGameState`

当前 Application 接口：

```cpp
createInitialState(GameStartupContext&)
onShutdown(GameShutdownContext&) noexcept
```

当前 State 接口：

```cpp
onEnter(GameStateEnterContext&)
onExit(GameStateExitContext&) noexcept
initialPolicy() const noexcept
fixedUpdate(FixedUpdateContext&)
updateFrame(FrameUpdateContext&)
extractRenderScene(RenderSceneExtractionContext&) const
updateUI(UIUpdateContext&)
```

Runtime 私有持有 `GameStateStack`（定容 8）。`FrameUpdateContext` 提供 `requestPush` /
`requestPop` / `requestReplace` / `requestPolicyChange`：每帧最多一个 structural command，在
`updateFrame` 之后、`extractRenderScene` 之前唯一 commit。各相位自顶向下调用栈上 State，直到
某层 policy 的相位阻断字段阻止继续向下。structural command 仅栈顶 context 可排队。Game SDK
不获得可变 stack 引用。

`GameStatePolicy`（`include/tina/runtime/GameState.hpp`）：

| 字段 | 行为 |
| --- | --- |
| `blocksFixedUpdateBelow` / `blocksFrameUpdateBelow` / `blocksRenderBelow` / `blocksUIUpdateBelow` | 截断该相位向下分发 |
| `blocksUIUpdateBelow` | 只挡下层 `updateUI`；**不**挡当帧 UI route |
| `blocksGameplayInputBelow` | 下层 fixed/frame 收到空 suppressed Action snapshot；不截断回调本身 |

完整帧序与输入四段式见 [Runtime](runtime.md)。

## Phase Context

| Context | 暴露 | 生命周期 |
| --- | --- | --- |
| `GameStartupContext` | EngineConfig、Platform event subscription | `createInitialState()` 回调 |
| `GameStateEnterContext` | subscription、primary UI root builder | `onEnter()` 回调 |
| `FixedUpdateContext` | frame/fixed timing、Simulation Action、可选 Audio | `fixedUpdate()` 回调 |
| `FrameUpdateContext` | timing、Frame Action、可选 Audio、exit-after-frame；仅栈顶可借用 `InputActionRebinding` | `updateFrame()` 回调 |
| `RenderSceneExtractionContext` | phase-local `RenderSceneWriter` | extraction 回调 |
| `UIUpdateContext` | root-scoped UI updater | `updateUI()` 回调 |
| Exit/Shutdown Context | stop cause、只借用 failure Error | 对应 callback |

这些 Context 不可复制/移动，地址、内部 view、writer 和模块 pointer 都不得保存。可以保存的 owner 是明确
RAII token/root/lease，而不是 Context 本身。

## Platform 与 Input

`IPlatformBackend` 发布 `PlatformPollResult` 与 `PlatformFrameView`。PlatformFrame 包含 final Window/
Gamepad snapshot、ordered lifecycle/input transition、strict UTF-8 text/composition。所有 storage 创建时
固定容量；view 到下一次 poll/build 失效。

`TextInputCaretRect`/`TextInputPlacement` 是 backend-neutral 的 owner-window logical client geometry；
Runtime 在成功 UI paint publication 后把 `UIContext::committedTextInputCaretRect()` 交给
`IPlatformBackend::updateTextInputPlacement()`。`nullopt` 清除当前 IME hint。实现不得把 HWND/POINT/RECT、
GLFW 或 X11/Wayland 类型带过公开边界；Windows GLFW 私有 adapter 将 geometry 转为 DPI-scaled client
pixels 并驱动 IMM32 composition/candidate placement，Headless 的非空 placement 明确返回不支持错误。

公开输入类型覆盖 Key、Pointer、标准 Gamepad、TextInput、TextComposition、Cancel/Reset。GLFW/native
枚举不会越过 adapter。`PlatformEventSubscription` 是 generation-safe RAII token；dispatcher owner 和
dispatch/shutdown 不暴露给游戏。

Gameplay Action 只有 Runtime 一套公开模型：`InputActionMapConfig::bindings` 保存带稳定
`InputBindingId` 的 `InputActionBinding`，同一 binding variant 表达 digital control 与
`StandardGamepadAxisBinding`。axis 支持 Signed/PositiveHalf/NegativeHalf/Trigger、gameplay deadzone 与
scale；多个 keyboard/pointer/所有已连接 Gamepad generation 的贡献按每个 Action 固定的
`SumClamped` 或 `StrongestMagnitude` 合成。Simulation/Frame snapshot 公开排序后的
`InputActionState{action, value}` 和 Started/ValueChanged/Completed/Cancelled transition；调用方通过
`value()` / `isActive()` 查询，不读取 physical held state。

UI transition consumption 与 continuous-control claim 先于 Action mapping：digital source 抑制到真实
release，axis 抑制到 neutral/deadzone，均不会穿透 gameplay。只有栈顶 State 可从
`FrameUpdateContext::inputActionRebinding()` 借用窄 facade；`begin`/`commit` 把修改排到下一 mapping
frame 原子应用，冲突显式选择 `Reject` 或 `Swap`，`Capturing`/`Queued` 均可取消。绑定的 Gamepad
generation 断连或 raw reset 失去该 generation 时 transaction 取消，不自动迁移到重连设备。

## Task

`ITaskSystem` 提供 `scheduleIo`、`scheduleCpu`、`postMain`、`pumpMain`、`requestStop`、
`shutdownAndJoinFor`、`shutdownAndJoin`。有界关闭只接受 finite positive `Core::Duration`；非法值不触发
stop，timeout 返回 `TaskErrorCode::WaitTimeout` 并保留 stopping 对象/Worker ownership 供后续重试。
`TaskSystemCreateParams::cpuWorkerCount=0` 在直接工厂中表示 CPU domain disabled；
`Desktop::CreateEngine` 经 `resolveDesktopTaskSystemParams` 将 0 解析为 `max(1, hardware_concurrency-1)`。
`TaskGroup` 提供结构化 pending/wait，不允许 detach/强杀。

## Render

`IRenderDevice` 核心方法是 `submitFrame`、`present`、`statistics`、`shutdown`。可选资源 API包括：

- RGBA8 Texture2D create/destroy、非消费式 `validateTexture2D()` live/generation 校验、通用 Texture2D 非0 key
  binding（invalid `GpuTextureId` 清除 binding），以及 device-instance
  `createTexture2DBinding()` allocator；
- 唯一 P3N3T4UV2/U16 StaticMesh `createStaticMesh`/destroy、Mesh3D key binding 与独立 device-instance
  `createMesh3DBinding()` allocator；
- 独立 device-instance `createMesh3DMaterialBinding()` allocator，以及原子
  `set/clearMesh3DMaterialBinding()` texture/factor bundle；细粒度 material setter 是低层 direct SPI；
- Opaque3D/Transparent3D Cook-Torrance GGX direct-light `Mesh3DLightingDesc`（同步消费0..4 directional + 0..8 point +
  0..8 spot lights + 非负 ambient）；`IRenderDevice::setMesh3DLighting()` 是低层 fallback/direct SPI；
- `EnvironmentMap` create/validate/destroy/retire、`Mesh3DImageBasedLightingDesc` bind 与显式 clear；
- primary framebuffer RGBA8 capture。

`validateTexture2D()` 成功只证明该 handle 的 owner/index/generation 当前能在目标 device 的 Texture2D
storage 中解析；wrong-owner/stale/invalid 失败不消费 handle，也不修改 backend 状态。
`GpuTextureId`/`GpuMeshId`/`GpuEnvironmentMapId` 是 RenderDevice owner-scoped generation handle，不是 AssetHandle。当前
`RenderFrame` 的
Surface/resource table/Scene/UI/Glyph view 只在 `submitFrame()` 调用内有效；backend 不能保存。
`FrameResourceRef` 是 packet-local owner/generation/index token；table resolve 对 cross-packet、stale、越界与
wrong-kind ref fail closed。Runtime 使用 `RenderFramePacket`、`FramePin` 与 submission completion ledger（成功 present 返回后关闭 CPU 借用，见
`include/tina/render/FramePin.hpp`）。`SubmissionTicket` 不可复制且绑定签发 ledger，packet 取得唯一所有权
后负责 complete/abandon。它不代表 GPU execution/retirement；Texture2D/GPU mesh/EnvironmentMap 使用独立的
`retire*` + backend marker，不能把两类 completion 混用。

`RenderSceneBuilder/Writer` 提供 fixed-capacity Camera2D/PerspectiveCamera3D/Sprite2D/static Mesh3D/
SkinnedMesh3D extraction，
并可把一次 `setSprite2DLighting()` / `setMesh3DLighting()` 深拷贝为 self-contained 的 committed frame
snapshot；同类 lighting 重复设置或非法描述使当前 build 原子失败。Sprite2D snapshot 最多保存8个
world-space point light、32个 world-space shadow segment 与 ambient；Mesh3D snapshot 最多保存4个 directional、
8个 world-space point、8个 world-space spot light 与 ambient，且不改变既有 mesh batch。
commit 后返回 borrowed view。
`RenderSprite2DInput/Item::texture` 只接受当前 packet 签发的 required `FrameResourceRef`；
`normalTexture` 是 optional packet-local Texture2D ref，invalid 表示无 normal map；
`RenderMesh3DInput/Item/Batch::mesh/material` 同样只接受当前 packet 签发的 ref。
`RenderSkinnedMesh3DInput` 还同步复制 1..256 个 column-major `globalPose * inverseBind` 矩阵到
当前 frame 的固定容量 palette pool；committed item 只保存 palette offset/count，不借用 Animator storage，且不参与
static instance batch。backend 在同步 submit 中分别按 `Texture2D`、`Mesh3DGeometry`、
`SkinnedMesh3DGeometry`、`Mesh3DMaterial` kind 解析，并校验 palette joint count 与 bound skeleton 一致。
`UIDisplayList` 支持 SolidQuad/SolidEllipse/Glyph/ImageQuad、SolidQuad `UIPixelCornerRadii` 四角像素半径、可选 exact
`UISolidQuadVertices` 与 axis-aligned clip；SolidEllipse 支持填充和向内描边。UI Line 在 integration
边界按 logical 法向构造四角并分别应用 X/Y framebuffer scale，随后以 exact SolidQuad 发布，不公开
`rotationRadians`/rotated-quad 兼容 API。
ImageQuad 携带 normalized UV、premultiplied tint、packet-local Texture2D ref 与 Linear/Nearest sampling，
相邻兼容 command 才合并 batch。四角 radius、exact vertices 与 ellipse stroke 均计入 paint-order checksum，
backend 验证其有限性、凸性、bounds 覆盖与最大半径/描边宽度。

## UI

`UIContext`、`UINodeId`、`UIRootOwner` 与 builder/updater 提供 retained tree。公开 authoring 统一为
`createElement(parent, descriptor)`；`UIElementDescriptor` 一次给出 layout、behavior、text/image content、visual
StyleRole/box/Canvas、semantics、enabled、pointer/focus policy 与集合配置，`makeButtonElement()`、
`makeListViewElement()` 等是内建控件的官方 recipes。旧 `createPanel/createButton/createListView/...`
成员入口已删除，不提供 compatibility alias。当前内建行为覆盖 Root、Panel、Modal、Label、Button、
Checkbox、Slider、ProgressBar、RadioButton、TextEdit（默认单行；可选多行）、ScrollView，以及
Dropdown/Popup/Tooltip/Menu/MenuItem/DropdownItem、ListView/TreeView/VirtualGridView/DataGrid、
SplitView/Splitter、TabView/Tab。
Button 与 RadioButton descriptor 可直接使用与 text 互斥的 Image intrinsic content；此时控件必须显式发布
semantics name 并关闭 content-as-name，使图标、control chrome 与交互状态共享同一 retained node。其他带行为
Element 不接受 Image content。

`UILayoutStyle::containerLayout` 为 direct `Flow` child 选择 Flex 或固定容量 Grid。Flex 使用父级
`flexContainer` 与子项 `flexItem`；Grid 使用父级 `gridContainer` 和子项 `gridItem`，每轴最多8条显式或
隐式 `Px/Auto/Fr` track，支持 gap、zero-based row/column、span、row-major auto placement 和 per-item alignment。
容量、非有限 track、非法 index/span 或自动放置溢出均 fail closed；Grid 与 `UIVirtualGridView` / `UIDataGrid`
的数据虚拟化契约彼此独立。
`Flow/Overlay` placement 继续与容器类型正交；Overlay 使用
alignment + offset，Px offset 可为有限负值，Percent offset 范围为 `-100..100`，以表达受父级 clip 的
部分越界图元；Stretch 的边距用 margin 表达，Popup、Tooltip 与 Menu recipe 强制 Overlay。
`clipDescendants` 是默认 `false` 的显式 axis-aligned clip-owner 契约：开启后，普通 Flow/Overlay 后代的
committed `effectiveClip` 与 owner 的 world border-box 求交，但不改后代 `worldRect`，不建立 rounded clip，
也不额外改变 owner 自身的 paint clip。hit 与 paint 读取同一 committed clip；可见性、tree/semantics 顺序和
authored semantics `worldRect` 不变。ScrollView/ListView/TreeView/VirtualGridView/DataGrid viewport clip 复用同一传播机制；Popup
作为 viewport-level overlay 继续使用专用 anchor/clip policy，不受普通祖先 clip owner 限制。Tooltip 同样
强制 Overlay，但使用独立的显式 Anchor/placement contract，不复用 Popup 的 focus、input 或 barrier 状态机。
Menu 也拥有独立 Anchor/placement/state contract，仅在 Context 协调层与 Popup 共享单 Window transient overlay；
它不会把 Dropdown Popup 改造成 Menu。
控件内部文字由独立 `UIContentAlignment` 定位，layout snapshot 发布
`UICommittedContentPlacement`，paint、caret/selection 与 pointer-to-text mapping 共用该 committed origin。
`UITextOverflow::{Clip,Ellipsis}` 是独立于 `UITextStyle` 的节点 authoring intent；`Ellipsis` 只在 paint 阶段按
committed content box 和 UAX #29 grapheme 边界截断单行，intrinsic measure 与 Semantics name 始终保留完整文本。
虚拟集合通过 `UIListViewStyle::rowTextOverflow`、`UIVirtualGridViewStyle::itemTextOverflow` 以及
`UIDataGridStyle::headerTextOverflow/cellTextOverflow` 将相同策略应用到私有 materialized 节点，不要求 DataSource
预先截断 label/header/cell text。`UITheme::typography` 是 display/title/section/body/control/caption 六级命名字号 ramp。

`UIVirtualGridViewDataSource` 以 stable non-zero item key 暴露 logical item；创建时固定 materialized item pool，
layout 按 `minimumItemWidth` 响应式计算等宽列，只提供纵向滚动。`UIDataGridDataSource` 分离 row/column/cell descriptor；
column count 必须落在创建时固定 column pool，列宽是精确 logical width，logical row 由固定 materialized row/cell pool
虚拟化并支持双轴滚动。两者都提供 metrics、stable selection、scroll-to-item/cell、Pointer 与 Keyboard/Gamepad 命令；
descriptor 非法、pool 超限或候选提交失败时不发布半份 bindings/layout/paint/semantics，旧 committed snapshot 保持不变。

`UIElementDescriptor::textEditMultiline` 只对带 `TextInput` behavior 的 TextEdit 生效。启用后，
`UITextEditMultilineConfig` 允许 LF、`UITextEditWrapMode::SoftWrap`、固定 `maximumBytes` 与
`maximumVisualLines`、垂直滚动和 wheel step；visual rows、caret/selection、二维 hit-test 与
Up/Down/Home/End 都由同一份 committed layout 生成。selection/caret 的公开偏移仍是 Unicode scalar
index，但所有编辑、删除、导航和替换位置都对齐无第三方依赖的 UAX #29 grapheme 子集；BiDi 和复杂
shaping 不在当前契约内。多行配置容量不足或 visual-row 构建失败时，authored state 可以暂存并重试；
最后一次成功提交的 layout/paint/semantics snapshot 以及 route-visible visual rows、scroll 保持不变。

游戏通过 Runtime phase facade 创建/更新主窗口 root，不获得裸 UIContext。Text 使用 strict UTF-8，
descriptor 的 `string_view` 在创建时复制到固定容量 storage，失败回滚本次节点；
`PrimaryWindowUITreeUpdater` 暴露同一组 ScrollView/Dropdown/Popup/Tooltip/Menu/ListView/TreeView/VirtualGridView/
DataGrid/SplitView/TabView phase-scoped mutation/query，包括集合 DataSource、style/paint、metrics、selection、scroll 与 Tree expansion；
`setTextOverflow()/textOverflow()` 也通过相同 phase facade 暴露；
`setProductTheme()` 可事务式更新既有控件仍继承的产品 chrome；单节点
paint/text setter 只将对应属性转为局部覆盖，其余属性继续跟随 Theme。Theme metric 非法、owner-thread
错误或 dirty queue 容量不足均零发布。

`UITreeUpdater::committedLayoutRect(node)` 与 phase-scoped
`PrimaryWindowUITreeUpdater::committedLayoutRect(node)` 复制上一轮成功 layout publication 的 `worldRect`。
查询只接受当前 updater root 内仍存活且存在于 committed snapshot 的节点；返回值不借用 snapshot，节点未提交、
跨 root、失效或 facade 过期均返回结构化错误。该窄查询用于 Render/Editor 等需要把 retained layout 数值转换为
下一帧 viewport 的组合层，不暴露裸 `UIContext` 或完整 committed view。

`UITreeUpdater` 与 `PrimaryWindowUITreeUpdater` 均提供 `registerFlowLayer()`、`registerFlowScreen()`、
`pushFlowScreen()`、`popFlowScreen()`、`replaceFlowScreen()`、`activeFlowScreen()` 和
`isFlowScreenActive()`，并以 `setFlowScreenAction()` / `clearFlowScreenAction()` 为 Screen 注册
`UIFlowAction::Back/Confirm/Menu` fixed-inline callback。Layer/Screen 复用现有 retained node 与 root ownership，不增加平行 UI ABI；非栈顶
Screen 在 publication 中视为 `Collapsed`，作者样式保持不变。`UIContextCapacityConfig` 的
`flowLayerCapacity/flowScreenCapacity` 固定注册上限，`UIContextStatistics::flow` 发布 capacity/count/high-water
与失败/action 计数；callback 注册总量同样受 `flowScreenCapacity` 限制。`UIContext::routeFlowAction()` 供
Runtime 将 Escape/Gamepad East 的 Back，以及未被聚焦控件默认 Activate 消费的 Enter/Keypad Enter/Gamepad
South Confirm，以及 TextEdit 未优先消费的 P/Gamepad Start Menu，路由到 topmost committed active Screen；
处理过的 Down/Up 不再进入 gameplay。`UIFlowActionEvent::localUser` 报告实际来源用户。

`UIFlowLocalUserId` 是窗口内强类型用户身份，有效范围固定为 `1..16`，
`UIFlowPrimaryLocalUser=1`、`UIFlowLocalUserCapacity=16`。Keyboard/Pointer/Text/IME 固定属于 Primary；Gamepad
可通过 `assignFlowGamepad(gamepad, localUser)` / `clearFlowGamepadAssignment(gamepad)` 显式分配，
`flowLocalUserForGamepad(gamepad)` 对未分配身份返回 Primary。assignment 保存完整 generation `GamepadId`，
不因 Platform 槽复用继承旧用户。`UIContext`、`UITreeUpdater` 与 `PrimaryWindowUITreeUpdater` 均提供对应入口，
Runtime facade 继续受 phase epoch 限制。

`UIFlowInputDeviceState` 与 `flowInputDeviceState(localUser)` 按用户暴露 `KeyboardMouse/Gamepad` 类别、active
Gamepad、Platform frame/sequence 与仅在类别/identity 改变时递增的 revision；Runtime 通过
`observeFlowInputDevice(..., localUser, ...)` 按各用户的已验证 transition 顺序更新，release 与 axis drift 不切换。
重分配/清除只回落引用该 Gamepad 的用户状态并保留已锁存 Flow Down/Up；断连清除对应 assignment/latch，完整
stream reset 清除全部 assignment/latch。Layer/Screen 栈、focus 与 Modal 仍是窗口级唯一状态。本契约尚不包含
Back/Confirm/Menu 之外的任意 action-id。

`UIImageSource` 只保存 Texture2D `AssetId`、source pixel rect、texture pixel extent 与 intrinsic logical size；
`UIImageContent` 增加 Fill/Contain/Cover/None、alignment、tint 和 Linear/Nearest sampling。`UIIconContent` 是
强类型 authoring profile，至少包含 `UIImageSource`、tint、sampling 与 content alignment；
`makeIconElement(UIIconContent, layout)` 固定 `Contain`、居中、`UIPointerHitPolicy::Ignore` 和
`UISemanticsMode::Exclude`。`makeImageElement(image, accessibleName)` 发布 `UISemanticsRole::Image`，不再提供
要求业务直接传 `UIImageContent` 的 Icon 入口。UIIcon 仍是普通 Image content，不增加 Widget/Behavior/Asset kind。
Runtime 的 `bindImageResolver()` 返回 move-only root-scoped
registration；frame build 按 `(root, AssetId)` 去重 resolve/pin，不在 UI commit 中同步 I/O。

UI 美化 authoring 使用 `UISurfaceConfig`、`UIDividerConfig`、`UIBadgeConfig`、`UISwitchConfig` 与
`makeSurfaceElement()/makeDividerElement()/makeBadgeElement()/makeSwitchElement()`。Surface 提供
Plain/Filled/Elevated，Divider 提供 Horizontal/Vertical、Subtle/Strong/Accent 与 logical thickness，Badge 提供
Neutral/Accent/Danger。它们是普通 Panel/Label 的强类型 StyleRole/Layout/Semantics profile，不增加 retained
状态或 Render 类型；Surface/Divider 固定 Ignore hit，Divider Exclude semantics，Badge 发布只读 Label name。

`UIIconButtonConfig`、`UIFormFieldConfig`、`UIDialogConfig` 与 `UISnackbarHostConfig` 是第一方多节点 composition
profile；对应 `UIIconButtonParts`、`UIFormFieldParts`、`UIDialogParts`、`UISnackbarHostParts` 返回实际 retained node id，供调用者注册既有 Button
action 或更新 TextEdit。`requiredIconButtonBuildBudget()`、`requiredFormFieldBuildBudget()`、
`requiredDialogBuildBudget()`、`requiredSnackbarHostBuildBudget()` 在 mutation 前给出精确 node/text/Behavior reservation；`UIContext`、
`UITreeUpdater` 和 phase-scoped `PrimaryWindowUITreeUpdater` 均提供
`buildIconButton()/buildFormField()/buildDialog()/buildSnackbarHost()`。IconButton 的 Button 是唯一 behavior/semantics root，Icon
默认 Exclude semantics，Tooltip 保持独立 Anchor；FormField 只有一个 TextInput owner；Dialog 的既有 Modal
是唯一 barrier/Focus Scope owner。Snackbar 使用调用方持有的最大 4 条 inline queue、显式 monotonic clock、可选
action token 和 `Polite` live-region；它不请求 Focus，也不建立 Tooltip/Popup/Modal barrier。四类 recipe 复用同一
fixed-capacity transaction，失败不发布半棵组件树。

`buildDialog()` 构建成功后固定为 closed，调用方必须使用
`openDialog(dialog)/dismissDialog(dialog)/isDialogOpen(dialog)` 管理 presentation intent；不得把 Dialog 的 authored
visibility 改为 Hidden/Collapsed。intent query 在调用后立即变化，Modal barrier、Hit/Paint/Semantics、focus 进入及
dismiss 后的 focus restore 则只在下一次成功 `commitLayout()` 一起发布。每个 Window 同时最多一个 registered
Dialog 为 open intent；冲突、非 Dialog、stale/wrong-root 均失败，dirty queue 预检失败不改变 intent 或 committed
状态，destroy/root release/generation reuse 会移除注册。三个入口同时存在于 `UIContext`、root-scoped
`UITreeUpdater` 与 phase-scoped `PrimaryWindowUITreeUpdater`；过期 Runtime facade 返回
`UIPhaseCapabilityExpired`。打开 Dialog 还会原子关闭当前 Menu 并 hard-dismiss Tooltip。

`UINumberFieldConfig` 通过 `UINumberFieldLabelPlacement::{Above,Leading}` 明确区分表单纵向标签与
Inspector 两列属性行。`Above` 不创建额外容器，`UINumberFieldParts::content` 等于 `root`；`Leading`
创建独立、可伸缩的 content column，因此无 helper/error 时精确 node budget 从 6 增加到 7，text 与
Activate/TextInput Behavior reservation 不变。`requiredNumberFieldBuildBudget()` 与
`buildNumberField()` 继续使用同一 fixed-capacity transaction；这只是现有 Element/Button/TextEdit 的组合
profile，不增加公开 Widget kind 或兼容入口。

`UICollapsibleSectionConfig` 由产品提供 `collapsedIndicator` / `expandedIndicator` 两份 `UIIconContent` 与共享
`indicatorLayout`。recipe 在同一 6-node fixed-capacity transaction 中创建两个真实 Icon node，展开状态只切换两者和
content 的 `UIVisibility`；Header 仍是唯一 Toggle/Activate owner，不再把 `>` / `v` 字符写进 Label 来模拟图标。

Switch 默认 Standard 44x24，也提供 Compact 36x20；control root 通过 `accessibleName` 发布
`UISemanticsRole::Switch`。它继续解析为 Checkbox built-in，复用 Toggle state、`setChecked()/isChecked()`、
Checkbox action/paint API、Focus/Input/容量与 UIA TogglePattern；只有 theme track/thumb chrome 和 semantics role
不同。Windows UIA 将 Switch 映射为 CheckBox ControlType。该 profile 不新增 Switch Widget、状态池、update loop
或 GPU pipeline。

Tooltip authoring 使用 `UITooltipConfig` 与 `makeTooltipElement(text, config, layout)`。配置包含
`UITooltipPlacement::{Auto,Above,Below,Left,Right}`、anchor gap/viewport margin、三种 monotonic delay 与
`UITooltipTrigger::{PointerHover,KeyboardFocus,Manual}`。`setTooltipAnchor()` 只接受同 root 的 live、非循环、
兼容 Anchor；Tooltip 自身永远 Ignore hit、不可聚焦、Exclude semantics、无 Popup/Modal barrier。它读取最后一次
成功提交的 Anchor geometry 做 Auto/flip/clamp，并以 `UITooltipMetrics` 发布 committed anchor/tooltip rect、方向和
open 状态；失败 commit 回滚 clock-driven transient state 并保留旧 snapshot/metrics。Tooltip 文本只在 Anchor
没有显式 description 时作为 accessible description/HelpText fallback。`UIContext`、`UITreeUpdater` 和 Runtime
phase facade 均提供 `setTooltipAnchor/clearTooltipAnchor/tooltipAnchor/showTooltip/dismissTooltip/isTooltipOpen/tooltipMetrics`。

Menu authoring 使用 `UIMenuConfig`、`UIMenuItemConfig` 与
`makeMenuElement()/makeMenuItemElement()`。Menu 只接受 direct MenuItem child，Item 不能再拥有 child；
`setMenuAnchor(menu, anchor)` 要求同 root、live、非循环且 Anchor kind 稳定。一个 Window 同时最多一个 active
Menu，且 Menu 与 Dropdown Popup 共用同一个 transient overlay 槽，打开一方会原子关闭另一方。

`UIMenuConfig` 提供 `UIMenuPlacement::{Auto,Below,Above,Left,Right}`、anchor gap、viewport margin、
match-anchor-width、keyboard wrap 与 close-on-activate。`UIMenuItemKind::{Command,Check,Radio,Separator}`
分别发布命令、checked、按 `radioGroup` 互斥和非交互分隔线；同组第二个初始 checked Radio 会在 authoring
阶段失败原子地拒绝。Check/Radio 使用 Menu 专属固定容量 state，
不占用通用 Toggle storage。布局读取最后成功 committed Anchor geometry 做 Auto/flip/clamp，
`UIMenuMetrics` 发布 committed `anchorRect/menuRect/resolvedPlacement/open`，失败 commit 保留旧 snapshot/metrics。

Menu surface 固定 Ignore hit，但其 chrome/outside Pointer Down 使用 transient barrier 阻止 click-through；Item
为 Targetable。Keyboard Up/Down/Home/End/Escape 与 Gamepad D-pad Up/Down/East 通过 `UIMenuCommand`
共用一条导航/关闭路径，并优先于 Dropdown、TabView 和通用空间焦点。Menu/MenuItem 映射 Windows UIA
Menu/MenuItem；Command 发布 Invoke 且无 TogglePattern，Check/Radio 发布 Invoke、checked 与 TogglePattern。
当前契约不包含 MenuBar 或 submenu。

`UIContext`、`UITreeUpdater` 与 `PrimaryWindowUITreeUpdater` 均提供
`setMenuAnchor/clearMenuAnchor/menuAnchor/setMenuOpen/isMenuOpen/menuMetrics/setMenuItemChecked/`
`isMenuItemChecked/routeMenuCommand`；Runtime facade 继续受 phase epoch/lifetime 约束。

SplitView authoring 使用 `UISplitViewConfig`、`UISplitterConfig` 与
`makeSplitViewElement()/makeSplitterElement()`。一个 SplitView 必须绑定同 root 的三个 direct Flow child，顺序由
`setSplitViewParts(splitView, primaryPane, splitter, secondaryPane)` 显式声明；重复、self、跨 root、stale、非 direct
child、非专用 Splitter 或非三子节点均拒绝。`setSplitViewFraction()` 与 `splitViewFraction()` 访问 pending fraction，
`splitViewMetrics()` 返回最后成功 commit 的 `primaryRect/splitterRect/secondaryRect/fraction/orientation`。
`PrimaryWindowUITreeUpdater` 同样 phase-scoped 暴露 parts、fraction、metrics 与 `isSplitterDragging()`。
Splitter 只是强类型 authoring profile：复用现有 `Focusable | RangeInput`、Pointer Capture、键盘命令、Slider
semantics 与 UIA SetRangeValue，不另建 Widget state machine、atlas、Asset 或 GPU pipeline。SplitView 默认 Ignore hit，
Splitter 默认 Targetable/Slider semantics；icon-only Button 的 accessible name 仍由 Button root 提供。

TabView authoring 使用 `UITabViewConfig`、`UITabConfig` 与 `makeTabViewElement()/makeTabElement()`。
`setTabViewItems(tabView, items, activeIndex)` 一次声明完整 Tab/Panel pairs；每端都必须是同 root、不同且恰好覆盖
TabView 所有 direct Flow child，Tab 还必须是专用 kind。追加 direct child 会解除已有 relationship，调用方需重新
提交完整 list；self、重复、stale、跨 root、非 direct child、错误 kind、不完整集合和非法 active index 均零 mutation。
Top/Bottom 使用水平 strip，Left/Right 使用垂直 strip；只有 active Panel 发布为 Visible。

`UITabActivationMode::Automatic` 让方向导航同时移动 focus 与 selection，Manual 只移动 focus 并在 Activate 时选择；
Pointer、Keyboard Arrow/Home/End、Gamepad D-pad 与 accessibility Activate 复用相同路径。`UITabViewMetrics` 发布最后
成功 commit 的 strip/active Panel geometry、active Tab/Panel/index、item count 与 placement。`UITabPaint` 是
`UIStyleRoleId::Tab` 的专属 interaction chrome，可由 `setTabPaint()/tabPaint()` 局部覆盖，不复用
`UIRadioButtonPaint`。`UIContext`、`UITreeUpdater` 与 `PrimaryWindowUITreeUpdater` 均暴露 items、active state、
metrics、command 和 paint API；Runtime facade 继续受 phase epoch/lifetime 约束。

`UISemanticsDescriptor` 支持 Automatic/Publish/MergeDescendants/Exclude、显式 role/name/description/actions；
committed semantics 使用最近 published ancestor，显式空 name 不回退 content。`UIStyleRoleId` 与 behavior/
semantics 分离，`setStyleRole()` 切换 recipe，`clearOverride()` 从当前 product theme 恢复选定属性；Runtime
phase facade 同样暴露 role/query/reset。`UIElementBuildTransaction` 为直接 `UITreeUpdater` authoring 提供
`UIComponentBuildBudget`，在 component root 创建前统一预留 node、text byte、Canvas command 与六类标准
Behavior slot；Runtime 对应的 move-only `PrimaryWindowUIBuildTransaction` 由
`PrimaryWindowUITreeUpdater::beginBuildTransaction()` 创建。二者都在多节点创建失败/析构时回滚整棵子树并
阻止中途 snapshot commit。Runtime transaction 的每次操作校验 phase epoch，不得跨 callback 保存；活动事务
逃逸时 phase finish 强制回滚并返回 `BuildTransactionInProgress`，成功 commit 后只留下普通 retained subtree。
`UIContextStatistics::componentBuild` 提供各 reservation pool 的 requested/reserved/published/failure/outstanding
counter、活动事务数与失败事务数。

`UIColorField` 是 swatch + 可编辑 `#RRGGBBAA` summary；紧随其后的 `UIColorPicker` 是固定预算通道编辑组件，
只发布短 label、标准 RangeInput Slider 和 fixed-capacity `0..255` value label，不重复 preview 或 hex。
`SliderRed/SliderGreen/SliderBlue/SliderAlpha`
是只改变 filled-track 色相的 Theme recipe，仍复用标准 Slider behavior、semantics、geometry、paint storage 与
Dark/Light theme refresh；它们不建立新的 control kind 或 RangeInput 状态。

`UIElementVisual::canvas` 接受 borrowed、backend-neutral `SolidRect`/`SolidEllipse`/`SolidLine`/Image/
`NineSlice` command span。`SolidRect` 可设置 `UILogicalCornerRadii cornerRadii`；`SolidEllipse` 使用 bounds，
零 stroke 表示填充、正 stroke 表示向内描边；`SolidLine` 使用 Element-local 起止点与 logical thickness，
非法或退化线段 fail closed。Image/NineSlice 复用 `UIImageSource`，NineSlice 另带
source-pixel 与 destination-logical insets，首版只支持 Stretch。命令在
`createElement()` 返回前复制到 Context 固定容量 pool，destroy/transaction rollback 回收 slot。公开
`UIWidgetKind` 已删除；私有实现 kind 不属于 authoring/inspection ABI。`UIBoxPaint::primitive` 支持
Rectangle/Ellipse/Line；Ellipse 使用 Element layout rect，Line 使用 Element-local geometry。
`UIBoxPaint::cornerRadii` 使用同一强类型四角 logical 半径，只圆化 Rectangle 自身 chrome，不建立子树 clip；
UI→Render 逐角投影并夹紧到已有 `UIPixelCornerRadii`。NineSlice 在 committed paint 中按 row-major 精确展开1..9个 Image
entry，小目标按两侧 destination inset 比例压缩并消除零面积 patch；paint/DisplayList 容量不足不截断。
四角非法值在 descriptor/setter/Canvas assign 和 bridge 边界 fail-closed；rounded clip 仍是后续扩展。startup-only 强类型 StyleClass/ColorToken、node-local state、
literal/token-backed BoxFill stylesheet，以及运行期 ColorToken getter/setter 与固定 reverse-dependency
更新路径与 stylesheet imageTint 已开放；更广 opacity/其他属性面仍未开放。token update 按依赖链
`O(affected links)` 预检并发布 Paint dirty，不是
`O(affected)`。

`paintSnapshotCapacity` 为0时从 `nodeCapacity` 派生，非0时独立上限为8,388,608，因为一个节点可生成多个
glyph/control/Canvas/NineSlice entry；Semantics entry/scratch 仍严格按 node 数分配。

第三方当前可以组合现有 Element、布局、Semantics、StyleRole/局部 paint、Image/Icon/NineSlice、routed listener 与官方控件
callback，直接 `UITreeUpdater` 还可用固定预算 transaction 构建多节点业务组件。Activate/Toggle/RangeInput/TextInput/Scroll/Select
已使用独立 fixed-capacity side store，Activate action、Toggle state、Range value/range setter/default behavior、TextInput selection
setter/query 与 Scroll style/offset/metrics 按 capability 校验，Select pool 持有 Dropdown 当前选项；Slider paint/change callback/Pointer drag geometry、TextEdit paint、
ScrollView paint/thumb geometry 与 Dropdown selection API/popup/paint/input routing 仍是 kind-specific。TextInput/Scroll/Select 输入与视觉路由仍由私有
resolver 选择 TextEdit/ScrollView/Dropdown，并要求匹配现有 `BuiltinElementKind` contract；不受支持的混合组合返回
`InvalidElementDescriptor`。当前可在首个 retained node 前通过 `UIContext` 或 `GameStateEnter` 的
`PrimaryWindowUIRootBuilder` 注册 StyleClass/ColorToken 并安装 node-local literal/token-backed BoxFill rules；
`PrimaryWindowUIRootBuilder` 另提供 `productTheme()` / `setProductTheme()`，用于在 `createRoot()` 之前确立
density —— density 是 root 构建期属性，`setProductTheme()` 只在零 live root 时接受 density 变化，因此
密度切换必须重建 root，而 color scheme 仍可在 live root 上经 `PrimaryWindowUITreeUpdater` 事务切换；
`UIContext` 与 phase-scoped `PrimaryWindowUITreeUpdater` 提供 `styleColorToken()` / `setStyleColorToken()`；
setter 先预检 dirty queue，失败时保持 token/dirty/committed 不变。`UIContext` 与 phase-scoped facade
已提供 fixed-capacity paint-only Motion、reduced-motion、stylesheet `BackgroundColor` transition，以及
`UITimelineId` + `create/replace/play/cancel/destroy/isActive` 的 typed keyframe timeline。Timeline descriptor
按 Context 固定的 definition/track/keyframe/active-index 四类容量复制，支持 paint color/scalar/offset 以及
bounded `LayoutWidth`/`LayoutHeight`/`LayoutOffset`、retarget 与跨 Context/generation fail closed。含 layout track
的 sampling 通过唯一 commit pipeline 从同一 candidate 原子发布 Layout/Hit/Paint/Semantics，失败保留旧
presentation 与 active playback。当前仍**不支持**注册 Widget subclass、新 Behavior/state machine、通用
selector、白名单外 layout animation 或 GPU paint callback。
因此“可组合业务 UI”不等于“已有开放控件插件
ABI”。目标边界见 [UI 框架设计](ui-framework.md)和 Accepted
[ADR 0023](adr/0023-ui-extensibility-style-paint-motion.md)及
[ADR 0026](adr/0026-ui-keyframe-timeline-and-layout-animation.md)。正式外部使用仍以 `SDK-001` 的安装 package 与
consumer gate 为准；这些当前公开声明不代表跨发行版正式 ABI 已冻结。

当前图片边界已按 ADR 0023 落地：`UIIconContent`/Icon 是 Image 的 atlas source/tint/default-layout 强类型
authoring profile，Image/Icon 均
落到同一 RGBA ImageQuad；NineSlice 复用同一图片源并在 DisplayList 前展开，没有专用 backend command。
普通 Image bounds 保持 cover 投影；NineSlice committed patch 使用
`UICommittedImageBoundsProjection::SharedBoundary` 并显式携带 authored half-open right/bottom cut，Integration
无需从 float width 反推端点，相邻 patch 不产生 fractional-DPI round gap/overlap。

UI-004 的 committed Focus Scope、显式 focus、Modal barrier/焦点恢复和持久 Primary Pointer Capture 已实现；
`UIFocusNavigationDirection`/`routeFocusNavigation()` 基于 committed 几何提供不 wrap 的空间焦点选择，
Keyboard Arrow 与 Gamepad D-pad 通过 Runtime 复用该路由，复合控件方向命令保持优先；
`makeSliderElement()` 声明的 `Focusable | RangeInput` 与 Focus semantics 已由 runtime trait 对齐：Slider 可由
`requestFocus()`、Tab、空间导航和 Primary drag 获得同一 committed focus，并遵守 Modal/Contain scope；
disabled、Hidden/Collapsed、destroy 与 Modal change 会清除或迁移焦点。`UISliderPaint::focusedThumbColor`
只表达该焦点的 paint feedback，dragging 仍优先。

`UIRangeInputCommand::{Decrease,Increase}` 与 `UIContext::routeRangeInputCommand()` 提供独立于空间焦点的
capability-level 调值契约。Runtime 将 Keyboard Left/Down 与 D-pad Left/Down 映射为 Decrease，将
Right/Up 映射为 Increase；路由优先级位于 Menu/Dropdown/ListView/TreeView/VirtualGridView/DataGrid/TextEdit 等复合方向控件之后、
通用空间焦点之前。focused Slider 复用 Pointer/UIA 已有的 min/max/step/clamp、量化、value storage 与 callback
路径；`step == 0` 时使用 range 的 1%。`UIRangeInputCommandResult` 分开报告 `consumed`、`changed` 与
`targeted`：只有成功改变 value 的 Down 才建立 fixed-capacity exact-control latch，匹配 Up 即使焦点或
enabled 状态已变化仍成对消费；read-only 或边界值目标不修改、不 latch，也不被重新解释为空间焦点，
未消费 transition 仍可交给 Gameplay。
`UICheckboxPaint` 与 `UIRadioButtonPaint` 提供 hover/focus/pressed indicator override；零 alpha 回退到下一
状态，非零颜色按 pressed > hover > focus > normal 解析。disabled 仍统一应用 widget opacity，Checkbox
checked 前景与 RadioButton selected 前景继续读取各自既有字段。默认 Dark/Light recipe 均提供可辨识状态色。
`UIListViewPaint`/`UITreeViewPaint` 的 selected row overlay 同样提供 hover/focus/pressed override；focus
读取 collection owner，hover/press 读取当前 committed materialized row，优先级为 pressed > hover > focus >
selected。虚拟 row recycling、selection stable key、callback 与 command 路径不变。
`UITextEditPaint` 独立提供 hover/pressed/focused/disabled 背景以及 selection/caret 颜色；背景优先级为
disabled > pressed > hover > focus > normal，零 alpha 状态色回退到下一层。`UITreeUpdater` 与
`PrimaryWindowUITreeUpdater` 均提供 `setTextEditPaint()` / `textEditPaint()`；setter 只 detach
`UIStyleOverride::TextEditPaint`，BoxPaint 与 TextStyle 仍可继续继承当前 Theme。TextEdit 的 Pointer
selection、IME、Value action 和 committed focus 仍使用原路径，没有新增输入状态或 GPU callback。
UI-005 的 ScrollView、Dropdown/Popup 与固定 row pool ListView/TreeView 已实现；VirtualGridView/DataGrid 也已分别
接入固定 item pool 与固定 column/row/cell pool。集合控件支持 100k logical item/row 的虚拟化，但完整通用
dirty-range pruning 仍未完成。当前回归覆盖 50,000 节点深树的非递归
structure commit/destroy、layout、hit 与 paint publication；Popup membership 在 layout traversal 中缓存，
避免 publication 对每个节点重复回溯祖先。

可选 FreeType、R8 Glyph atlas、semantics snapshot 与 `UIAccessibilityTree`/probe provider 均为 Tina API。
平台中立 `UIAccessibilityAction`/`UIAccessibilityActionKind` 提供同步 owner-thread Focus、Invoke、
Toggle、SetRangeValue 与 SetTextValue seam；adapter 通过它保留正常控件 callback，stale、disabled、
类型不匹配或非法 action 返回明确错误。可选 Windows UIA 私有 adapter（`TINA_BUILD_UI_UIA`）映射 UIA 属性，
公开头无 COM；产品路径经 EngineHost 自动附着 HWND HostBridge，并实现 Invoke/Toggle/RangeValue/Value
patterns 的 owner-thread dispatch；Menu/MenuItem 映射对应 ControlType，只有发布 Toggle 的 Check/Radio MenuItem
暴露 TogglePattern，发布 Activate 的 Button/MenuItem 暴露 InvokePattern。`RunUi002UiaGate.ps1` 可由外部 client
进程连接真实 showcase HWND。
Narrator/Inspect 人工金标仍由 UI-002 跟踪，Linux AT-SPI adapter/真机验收由 UI-002-LINUX 跟踪；自动 gate
不等于真实 screen reader 合规金标。

## Scene

`Scene::World` 是 fixed-capacity、generation entity owner，提供 Transform hierarchy、Camera2D/
SpriteRenderer2D/PointLight2D/ShadowOccluder2D/PerspectiveCamera3D/MeshRenderer3D/SkinnedMeshRenderer3D/DirectionalLight3D/
PointLight3D/SpotLight3D。
`extractRenderSceneFromWorld()` 写调用方的
RenderSceneWriter；`instantiatePrefab()` 事务式创建 hierarchy，并可通过 AssetId resolver 映射 mesh/
material weak `AssetHandle`。

`SpriteRenderer2D` 只复制 required weak Sprite `AssetHandle`、optional weak normal Texture2D `AssetHandle`
和渲染语义字段，不持有 `AssetLease`/Cooked payload/GPU handle。
`ExtractRenderSceneParams::spriteBindingResolver` 是 allocation-free 的 borrowed function-pointer
view，仅在一次 extraction 调用内有效；它接收当前 `FrameResourceSink`，visible sprite 必须由它按 Store
owner/generation、Sprite kind 与 binding 状态解析并 intern 为 packet-local base texture ref。非空 normal
handle 由 `normalTextureBindingResolver` 独立按 Texture2D kind 解析；缺 resolver、stale/wrong-kind/unbound
handle 或空 ref 都在 `addSprite2D()` 前返回 `SceneErrorCode::UnresolvedSprite`，不发布半个 item。hidden
sprite 不解析任一 handle。Scene 不保存 resolver、sink、ref 或任何 Asset owner。

`MeshRenderer3D` 只复制 weak mesh/material `AssetHandle` 与渲染语义字段；`Mesh3DAlphaMode` 只接受
`Opaque`/`Blend`，Runtime 不根据 baseColor alpha 或纹理内容推断 pass。extract params 分别提供
`mesh3DBindingResolver` 和 `material3DBindingResolver`，只在本次 extraction 调用有效；visible mesh 必须
由两者按当前 Store owner/generation、预期 StaticMesh/Material kind 与 binding 状态 intern 为非空
packet-local ref。任一 resolver/handle/binding 无效返回 `UnresolvedMesh`；mesh 解析失败时不调用 material
resolver，hidden mesh 不解析。`PrefabMeshBinding` 只完成 AssetId→Handle，不保存或分配 Render key。

`SkinnedMeshRenderer3D` 同样只复制 weak SkinnedMesh/Material handle、显式 alpha mode 与渲染语义字段，并与同一 Entity 上的
`MeshRenderer3D` 互斥；成功设置任一 renderer 会清除另一种，非法 setter 保留原组件。visible skinned mesh
通过独立 `skinnedMesh3DBindingResolver` 解析 geometry，并通过 phase-local `SkinnedPose3DProvider` 按 Entity
取得 CPU palette；缺 resolver、wrong-kind/stale/unbound handle、空/非有限/非 mat4 对齐 palette 都 fail closed，
hidden component 不调用任何 resolver/provider。Scene 同步把 palette 交给 writer 后不保留 provider/span。

`Animator3D::Create(mesh, clip, resource)` 复制 borrowed SkinnedMesh/AnimationClip3D wire view，要求 joint count
精确匹配，并一次性建立 PMR storage；`update()` 在 owner thread 无分配地评估 bind pose、LINEAR/STEP tracks、
hierarchy global pose 与 `globalPose * inverseBind` skinning matrices。Once/Loop/PingPong、play/pause/restart/stop、
finite playback speed 均为显式状态；`setClip()` 事务替换同 skeleton joint count 的 clip，失败保留旧 clip/pose。

`captureWorld2DSnapshotBytes()` 将 owner-thread World 的节点名称、LocalTransform 与五类2D组件（含 SpriteAnimation
绑定）写入唯一现行 schema-v4 snapshot（448-byte named entity record）；调用方 callback 提供稳定 entity ID，并把 Sprite/normal Texture weak handle 映射为
稳定 `AssetId`。capture 按 hierarchy depth、stable ID 确定性排序，拒绝重复/零 ID、损坏层级和任何3D组件，
不会静默丢字段。`instantiateWorld2DSnapshot()` 在修改目标 World 前预检容量、全部组件与 AssetId→weak handle
解析；失败销毁本次创建的完整集合并保留既有实体。Runtime `EntityId`/generation、AssetHandle、Lease、Render
ref/key 都不持久化。gameplay blob 由 game-owned schema/version/bytes 携带，Runtime 不解释。旧 snapshot
schema 直接拒绝，不保留运行时兼容分支。详见 [World2D 序列化](world2d-serialization.md)。

`DirectionalLight3D` 保存 linear color、非负 intensity 与 active 标志；Entity 的 world local `+Z` 指向
光源。extraction 按稳定 Entity identity 收集最多4个 active light，把 world direction、color×intensity 与
`ExtractRenderSceneParams::ambientLightScale` 写入当前帧 RenderScene lighting snapshot。超容量显式返回
`TooManyActiveDirectionalLights`，不做静默裁剪；Scene 不持有 device lighting 状态。

`PointLight3D` 保存 linear color、非负 intensity、正 world-space `influenceRadiusMeters`、optional
`PointLightShadow3D` 与 active 标志；
Entity world position 是光源中心，transform scale 不缩放半径。extraction 先校验全部 active component，
在有效 PerspectiveCamera3D 与非0 surface 上做 influence sphere-vs-perspective-frustum culling，再按稳定 Entity
identity 收集最多8个 camera-affecting light。第9个显式返回 `TooManyActivePointLights3D`；无相机或0x0
surface 时保留未裁剪容量契约。position、radius 与 color×intensity 深拷贝进当前帧 Mesh3D lighting snapshot。
shadow 的 `nearPlaneMeters` 必须正且小于 influence radius，depth/normal bias 必须 finite、非负且在公开
上限内；culling 与稳定排序后以 `pointLightIndex` 关联灯槽。每帧最多一个 camera-affecting point shadow，
第二个配置显式返回 `TooManyActivePointLightShadows`，不会发布部分 snapshot。

`SpotLight3D` 保存 linear color、非负 intensity、正 world-space `influenceRadiusMeters`、满足
`0 <= inner < outer < 90` 的 cone half-angle 与 active 标志；Entity world position 是光源中心，world local
`-Z` 是出光方向，transform scale 不缩放半径。extraction 先校验全部 active component，再复用 influence
sphere-vs-perspective-frustum culling，并在容量检查前剔除。每帧最多8个 camera-affecting spot light，
第9个返回 `TooManyActiveSpotLights3D`；无相机或0x0 surface 时保留未裁剪容量契约。position、radius、
normalized direction、inner/outer cosine 与 color×intensity 深拷贝进同一 Mesh3D lighting snapshot。

`PointLight2D` 保存 linear color、非负 intensity、正 world-space `radiusMeters`、finite
`sourceRadiusMeters` 与 active 标志；`0 <= sourceRadiusMeters <= radiusMeters`，默认0精确保留 point-source
硬阴影，正值只控制 penumbra。Entity 的 world position 是光源中心，transform scale 不缩放两个半径。
extraction 先校验全部 active light，再在非0 surface
上按 resolved、pixel-snapped Camera2D 做旋转相机空间的精确 circle-vs-rectangle culling，按稳定 Entity
identity 收集最多8个 camera-affecting light，把 world position、radius、color×intensity 与
`ExtractRenderSceneParams::ambientLight2DScale` 写入 Sprite2D frame snapshot。第9盏 camera-affecting light
返回 `TooManyActivePointLights2D`，不做 top-K；无 active Camera2D 或0x0 surface 时不裁剪并对全部 active
light 保留同一上限。未声明组件保留既有 unlit path，全部 inactive 则发布 ambient-only snapshot。
`ShadowOccluder2D` 保存一条非退化 local-space 线段与 active 标志。extraction 对端点应用已发布 transform
的 XY scale、rotation、position，按稳定 Entity identity 收集最多32条 world-space segment；超容量返回
`TooManyActiveShadowOccluders2D`，非法/投影退化结果返回 `InvalidComponent`。Occluder 不做 camera culling，
因为视口外 segment 仍可能遮挡边界光线。source radius 为0时，相交 segment 清零对应点光贡献；正值时
按 receiver→light 深度把 segment 投影到 finite source 区间并连续缩放可见度，多段使用固定成本乘法透射近似。
不改变 ambient、premultiplied alpha 或 Sprite 排序；没有 PointLight2D 时不单独发布 occluder snapshot。

`ParticleSystem2D` 与 `Trail2D` 是独立 Scene owners，不属于 World/ECS，也不依赖完整 AssetSystem 或
bgfx。二者复制 copyable weak Sprite `AssetHandle`，不持有 `AssetLease`、Cooked payload、GPU owner 或
resolver；在 `Create()` 中通过调用方 PMR resource 建立固定容量 storage，成功的 emit/append、update、
extract 不增长 storage。它们直接复用调用方 phase-local `RenderSceneWriter` 提交 backend-neutral Sprite2D。

`ParticleBurst2D::sprite` 在 `emitBurst()` 时复制到每个粒子，空 handle 属于 `InvalidComponent`；
`Trail2DConfig::sprite` 在 `Create()` 时校验，空 handle 同样失败。两种显式
`extract(writer, frameResources, resolver)` 只在本次调用借用共享 `AssetFrameResourceResolver` 与 sink；缺
resolver 或 handle 被解析为空 ref 统一返回
`SceneErrorCode::UnresolvedSprite`。stale/cross-store/wrong-kind/unbound 的识别由 resolver/registry 负责。
空 system 不解析；Trail 每次非空 extract 解析一次并供所有 segment 复用，Particle 按 live item 解析。

`ParticleSystem2DConfig::randomSeed` 对所有值（包括0）都是固定确定 seed。`emitBurst()` 的 validation、
容量与稳定 key preflight 失败不改变 RNG、next key 或 live set；成功 key 单调分配且过期/clear 后不
复用。`update()` 先检查全部 next age 和 survivor position，失败时所有粒子不变；extract 按 normalized
age 插值 size/color。

`Trail2D::appendPoint()` 的第一点建立 anchor，之后每点生成一段；`breakTrail()` 使下一点建立新 anchor。
segment 各自从创建时计算 lifetime/age，width 按 normalized age 在 start/end width 间线性插值。
非法 geometry、容量或 key exhaustion 不修改 anchor/segments/next key；update 对所有 age 先 preflight，
成功后才推进和移除过期段。稳定 segment key 单调且不复用。

`createFx2DFromAsset(desc, resolvedSprite, resource)` 先校验完整 `Fx2DPayloadDesc` 与非空 weak Sprite handle，
再在同一 PMR 上创建 `ParticleSystem2D`、初始 `ParticleBurst2D` 与 `Trail2D`。任一 owner 创建失败都不返回
半份 instance；factory 不取得 AssetLease，也不自动发射 burst，调用方决定何时调用 `emitBurst()`。

`CameraFollow2D::Create()` 创建 allocation-free owner-thread controller。`fixedUpdate()` 先完整验证 target、
positive viewport half extents、可选 world bounds 与 positive fixed delta，再按 dead zone 和可选最大速度计算
next center，最后按 viewport clamp world；viewport 大于 world 时按轴居中。失败不改变 previous/current center，
`snapTo()` 原子重置两份 snapshot，`interpolatedCenter([0,1])` 只产生 presentation 值，不修改 `Camera2D`
projection component 或 `World`。

当前没有公开 SceneManager、ECS registry 或 Runtime-owned World capability。EnTT 不在公开面，也未被当前
Scene target 使用。

## Navigation2D

`NavigationGrid2DData::Create()` 按唯一 `NavigationGrid2DContract` 校验精确 row-major cell flags/traversal costs 与正 finite
cell size；每格 multiplier 必须在 `[1,16]`，非法尺寸、数组长度、multiplier 或保留 flag 直接失败。数据深拷贝到
调用方 PMR，并保存全局最小 multiplier。`NavigationGrid2D::Create()` 在调用方 PMR 上一次性建立固定容量
`NavigationBlockerId` generation pool 与 per-cell 引用计数。`addBlocker()`、`updateBlocker()`、
`removeBlocker()` 对容量、越界、stale 和 wrong-owner 失败保持当前状态；重叠 blocker 不会互相误清除，真实
mutation 推进 `revision()`。

`NavigationPathfinder2D::Create(cellCapacity)` 一次性分配 records/open-set/path storage。四向/对角 A* 的
cardinal/diagonal 进入成本为 `10/14 × destination multiplier`；Disabled 使用 Manhattan，启用对角使用
octile，两者乘 grid 最小 multiplier 保持 admissible，并按 `f`、heuristic、row-major index 确定性决胜。
对角策略可严格要求相邻正交格均畅通，或显式允许切角。`findPath()` 同步完成查询；
`begin()/advance(expansionBudget)` 提供调用方编排的分步查询，`cancel()` 产生吸收态 `Cancelled`。blocked
endpoint 或 open set 耗尽返回 `Unreachable`；Pending 期间 Grid 地址或 revision 变化返回 `Invalidated`。
越界 cell、零 budget 和未开始 query 是 `Core::Result` error。`path()` 只借用到下一次
`begin()/reset()` 或 Pathfinder 析构。

`Asset::buildTileMapNavigation2DData()` 是 TileMap 转换桥：solid tile layer 中 `MaterialSolid` cell 进入 base
blocked flags；可选 exact full-material-flags rule 写入 traversal multiplier；可选 object layer 只栅格化
property 精确匹配的 visible Rectangle。引用的 TileMapChunk 必须
已驻留，任何 layer/chunk/object/schema 错误都不返回半份数据。

独立 `AssetKind::NavigationGrid2D` 当前 schema v1 使用32-byte little-endian header，随后保存精确 row-major
cell flags 与 traversal costs，且没有 Cooked dependency。`Asset::parseNavigationGrid2DFromCooked()` 同时校验
kind/version/dependency contract；`loadNavigationGrid2DDataFromCooked()` 再把 borrowed payload 深拷贝为调用方 PMR
上的 immutable data。产品 State 持有 Grid/Pathfinder，Scene、Runtime、Render 和 Physics2D 不隐式取得所有权。详见
[2D 导航](navigation2d.md)。

`AssetKind::Fx2D` 当前 schema v1 是固定184-byte little-endian payload，并要求恰好一个
`Required Sprite` dependency。payload 保存 ParticleSystem capacity/seed/stable-key、initial burst 全部范围与
颜色/排序，以及 Trail capacity/lifetime/width/stable-key/UV/颜色/排序；reserved 字段必须为零。
`Asset::parseFx2DFromCooked()` 对账 payload Sprite AssetId 与 dependency，`Scene::createFx2DFromAsset()` 消费
已解析 desc 与调用方提供的 weak Sprite handle。

## Editor

`Tina::Editor` 是不依赖 UI/Runtime/Scene/backend 的工具侧 document target；`Tina::EditorApp` 是独立桌面组合 target，
负责 2D/3D 独立 workspace session、retained UI、Runtime preview 与 GPU viewport，不把这些依赖反向带入 document 层。
每个 session 的 path、loaded flag、saved baseline 与 dirty 状态由 EditorApp 私有持有，不扩展 document 公共 ABI。

`ProjectAssetBrowserModel::Create()` 复制 Catalog-derived descriptor、按 `AssetId` 确定性排序并拒绝重复/超容量输入；
每个 owned descriptor 保存由 `AssetKind + AssetId` 派生的 canonical relative cooked path，以及完整、按 AssetId 排序的
dependency records，并校验 count、目标、kind、flags 与重复边。`inspectorSnapshot(assetId)` 因而可按 active Inspector
tab 的稳定 `AssetId` 取得 owning metadata，不依赖浏览器当前 filter/selection。All/2D/3D/Media filter 重建稳定索引，
owned descriptor view 只在 model 析构时失效。`EditorDocumentTabs::Create()`
创建固定容量 move-only tab owner，以 `(EditorDocumentKind, AssetId)` 去重 open，失败保持 tab 列表和 active selection；
pinned tab 不可关闭，dirty tab 必须显式 `discardDirty=true`。这两个公共模型只表达工具状态，不拥有 Runtime/Scene/UI。
EditorApp 已接入 Catalog browser、资源 Inspector 和 workspace tab 路由；每个 tab 独立拥有 authoring document/history、
session 与 canonical saved baseline，同 key 重新打开只激活既有状态。Save/Save As 按 active document kind 分派；
dirty-close modal 提供 Save/Save As、Discard、Cancel；Catalog Refresh 在下一帧 packet 前复用
`AssetSystem::reloadCatalog()` 的 Sprite/Mesh participant transaction。以上编排留在 `Tina::EditorApp` 私有层，
不扩大 document 公共 ABI。

`SpriteAnimationAuthoringDocument::setFrameEvents()` 以一次 revision 替换一帧的 marker 集，沿用 payload 的每帧64/
单 clip 16384上限与稳定 offset 排序。EditorApp Timeline 显示选中帧 marker，并以 Prev/Next、Add/Apply/Remove
把十六进制或标识符 tag、decimal/percent offset 转成该 API；失败保持 document/history/selection 不变。

`Navigation2DAuthoringDocument::bakeFromTileMap()` 从 resident TileMap 构建 canonical payload/Cooked bytes，并记录
source TileMap revision；`stageCatalog()` 只在已有成功 bake 时返回包含全部 baseline object 与新 Navigation asset
的 fresh stage。`Fx2DAuthoringDocument` 提供 canonical payload `replace()` 与 bounded Undo/Redo；它是公共 document
API，当前 EditorApp 没有独立可见 FX effect graph/专用编辑面板。

EditorApp 私有 `EditorFileDialog` 在 Windows 用 `IFileSaveDialog`，在 Linux 用 `zenity` 并在 helper 缺失时回退
`kdialog`；两者都为 World2D `.tworld`、World3D `.tprefab` 与 SpriteAnimation `.tasset` 选择文件，并为 TileMap/Project
选择目录。native/COM/POSIX 类型不进入公共头，Linux argv 不经过 shell 且 child 始终有界回收。Cancel 是成功的 no-op，
保留 session path、baseline、dirty、tab 与 selection。其他未支持平台返回结构化 `CoreErrorCode::Unsupported`，
EditorApp 回退到 Toolbar/dirty-close TextEdit 中的 strict UTF-8 路径。

`EditorProjectWorkspace::Create(desc, config)` 建立 owning、move-only 的 canonical project/source/Cooked Catalog root
模型。三个 root 必须是有界 strict UTF-8 absolute path；Source 与 Catalog 必须严格位于 project root 内且互不相同、
互不嵌套。该 API 只做 lexical validation，因此后续文件操作仍须验证 final physical containment。
`CreateNewEditorProject(request)` 创建或采用既有空 project root，并创建默认 `Source`/`Catalog`（或调用方给定的两个
single-component 目录）；它拒绝非物理目录和 symlink/junction/reparse point，重新验证 root identity/final containment，
失败只删除 identity 仍匹配且确由本次事务创建的目录。成功结果不暴露 `std::filesystem` 或 native 类型。
该公共 API 本身不发布 Catalog。EditorApp 的 Project `New` command 在受支持的 desktop folder picker 成功后组合该 API、
`AssetFormat::writeCookedManifestBytes()` 与 `Asset::publishCatalogPackage()`：发布零 entry current-schema manifest 后再用
`openCatalogPackage()` typed-validate。Project `Open` 私有组合验证选中 root 的物理 `Source/`/`Catalog/` 目录、reparse
边界与 final containment。New/Open 都只在命令阶段排队 workspace；下一安全帧先拒绝仍有 dirty 的 Catalog document，
再调用 `AssetSystem::reloadCatalog()` 的 Sprite/Mesh participant transaction。Browser 必须从成功 reload 后的
`AssetSystem::catalog()` snapshot 构建，不能独立重开磁盘路径形成第二份事实；成功切换还会失效干净的动态 Catalog tab、
重新打开固定 TileMap/Animation document 并重建 2D/3D/Animation preview。active root/workspace 与 project-switch 计数只在
完整 preview 成功后发布；任何 commit 前失败继续使用旧 Catalog，commit 后失败作为结构化致命错误返回。

Editor source import 同样留在 `Tina::EditorApp` 私有组合层，不扩大 document ABI。launch option parser 消费 absolute
strict UTF-8 `--project-root`、可重复混合 `--import-recipe` / `--import-gltf` 和 `--import-on-start`，以一个有序 owning
集合表达完整 intended units。`EditorSourceImportService` 在后台只调用 Asset pipeline；Ready stage 由 owner thread 在安全帧
携带 Sprite/Mesh participants reload。dirty Catalog document 阻止 commit 但不丢弃 stage；`CatalogReloadBusy` 也保留 stage
重试。fresh stage 在 Ready 前已拥有 sibling state；Catalog/Browser/documents/preview 成功后只把
`active-catalog.path` 作为项目 tool cache 的唯一原子 commit marker。Project reopen 验证 pointer、stage state、Catalog
revision/output binding 与 physical containment 后恢复 Catalog 和完整 intended units。Editor file dialog 仍是 EditorApp 私有
adapter：Windows 调用系统 dialog，Linux 使用 `zenity` 并在缺失时回退 `kdialog`，通过 argv 直传且回收 helper 子进程。
Linux 定向编译和真实 helper 产品门禁完成前，`2D-EDITOR` 仍保持 InProgress。

`EditorSceneOperations` 暴露单一 Node authoring 契约：`world2DNodeTemplateRegistry()` /
`world3DNodeTemplateRegistry()` 是 Create Node、Hierarchy Kind 与 Inspector 的共享类型词表；`addWorld2DNode()` /
`addWorld3DNode()` 以一次 canonical revision 创建完整类型节点，duplicate/reparent/reorder/delete 只操作 Node hierarchy。
旧的空 Entity/Node 包装方法与 Add/Remove Component API 不保留。World2D optional payload 和 Prefab mesh/material 仍是
current-schema wire 细节；`classifyWorld2DNodeTemplate()` / `classifyWorld3DNodeTemplate()` 只接受精确对应一个受支持
Node kind 的形状，旧式多 payload 混合 fail-closed。

`EditorNodePropertyOperations` 只修改现有 Node kind 固有的 Rendering/Camera/Light/Occlusion/Animation 属性；它不改变
Node kind。optional 输入表示多选 `Mixed` 字段保持各节点原值，成功 batch 最多发布一次 `replace()`，kind mismatch、
非法值和未知 stable ID 保留 document/history，no-op 不发布 revision。

`World2DAuthoringDocument::Create(config)` 创建一个仅含 canonical 空 snapshot 的 move-only owner。配置显式限制
entity、gameplay bytes、history entries 与 history bytes；history entry 至少为 2，因此每次成功编辑至少可撤销一步。

`replace(desc)` 是 Inspector/gizmo/importer 的可撤销批量事务边界；`loadSnapshot()` 校验并原子建立新的 saved
baseline，成功后清空 undo/redo；`upsertEntity()`、`eraseEntitySubtree()` 与 `setGameplay()` 是同一 revision
机制上的窄操作。候选先经唯一现行
`AssetFormat::writeWorld2DSnapshotBytes()` 或 parser 完整校验，成功后才替换 current 并裁剪 redo；非法 stable ID/
parent、非有限 node payload、旧 schema、document 容量或 history byte 容量失败都保持 current、undo、redo 和 revision
不变。history 到达预算时淘汰最老 revision，不扩展声明容量；若 current + candidate 无法同时容纳则编辑失败。

`snapshotBytes()` 就是 cook/runtime preview，不存在 editor-only wire format；可直接交给
`AssetFormat::parseWorld2DSnapshot()`，随后由 `Scene::instantiateWorld2DSnapshot()` 消费。借用 bytes 在下一次成功
edit/undo/redo 后失效。完整场景、容量和失败契约见 [Editor 2D / 3D](editor-2d.md)。

`World3DAuthoringDocument::Create(config)` 以当前 Prefab v4 创建 move-only canonical owner，提供
`replace()`、`loadPayload()`、`upsertNode()`、`eraseNodeSubtree()` 与相同的 bounded undo/redo 原子性。
`payloadBytes()` 是唯一 3D preview/cook 输入；stable node ID、topological parent index、完整 TRS、Mesh/Material
`AssetId` 与 visibility 都由当前 Prefab writer/parser 验证。EditorApp 的 3D Inspector 编辑完整 TRS XYZ，提交时一次
从 Euler XYZ 生成 normalized quaternion，不清零未编辑的 hierarchy/asset/visibility 字段。

`TileMapAuthoringDocument::Create(desc, config)` 创建一个 move-only current-schema payload-family owner。一个 revision
原子拥有 TileMap/依赖 Tileset identity、canonical root bytes 与所有 canonical non-empty chunk bytes；root/chunk borrowed
view 在下一次成功 edit/load/undo/redo 后失效。`setCells()` / `paintCell()`、layer 增删/重命名/显隐、object upsert/erase
均复用同一 bounded history，空 chunk 自动删除；批量 brush 重复坐标、错误 layer kind、全图 stable ID、schema 或容量失败
不发布半份 root/chunk family。`loadPayloadFamily()` 只打开 root v3 + chunk v1，并强制每个 chunk 使用
`deriveTileMapChunkAssetId(map, layer, x, y)` 的现行稳定 identity；不双读旧 schema。`cookPreview()` 为当前 revision 输出
一个 TileMap artifact 和每个非空 chunk 的 TileMapChunk artifact，dependency contract 与正式 Cooked writer 相同。

`TileMapGameplaySpawnPlan::Build(tileMap, archetypes, config)` 从指定 visible object layer 生成 owning 记录，并暴露
`records()`、`sourceDocumentRevision()` 与 `objectLayerId()`。archetype name 和 game-owned non-zero `u32` ID 必须分别
唯一；hidden object 被忽略，visible object 的 archetype 缺失或未知、重复 binding、记录容量或分配失败都不返回半份
plan。`generateTileMapGameplay()` 在调用 game-owned encoder 完成全部 bytes 后，只用一次
`World2DAuthoringDocument::replace()` 发布 schema/version/blob；encoder、World2D 容量、parse 或 replace 失败保留
document revision 与 undo/redo。Editor 定义生成和事务边界，不定义游戏 ECS component 或运行时 archetype 行为。

`SpriteAnimationAuthoringDocument::Create(desc, config)` 创建一个 move-only SpriteAnimationClip v2 owner。revision
原子拥有 clip `AssetId`、播放模式、帧顺序、逐帧 Sprite `AssetId`/正有限时长/notify events、canonical payload 与排序去重的
required Sprite dependency stream。API 提供 replace、frame insert/append/set/duplicate/erase/move、duration、
Once/Loop/PingPong、bounded Undo/Redo、current-schema `loadCookedAsset()` 与 `cookPreview()`；非法帧、self dependency、
容量、history 或非 v2 Cooked asset 失败均不发布。

`AssetFormat::SpriteAnimationFrameDesc::events` 是**借用 span**，所以 authoring 侧不按值托管它：
`SpriteAnimationAuthoringDesc` 与内部 revision 各自用 `frameEvents[i]` 拥有第 i 帧的事件，并让
`frames[i].events` 只作为指向该存储的视图。新增/删除/移动帧或替换某帧事件后必须调用 `rebindFrameEvents()`
（`setFrameEvents()` 已内置），revision 的拷贝构造同样会重绑，避免 span 指向被销毁的调用方内存。

EditorApp 把该 document 接入独立 Timeline，并在 Asset/Scene 边界解析为 `SpriteAnimator2D`，document target
本身仍不依赖 Scene 或 Runtime。

`loadWorld2DAuthoringDocument(utf8Path, document)` 以 document 配置在当前 World2D schema 内可容纳的最大 wire size 为读取上限，读取成功后
复用 `loadSnapshot()` 原子建立 baseline；read/schema/document/history 容量失败不改变 current 或 undo/redo。
`saveWorld2DAuthoringDocument(utf8Path, document)` 把当前 `snapshotBytes()` 写入同目录临时文件并原子替换目标，
自动创建父目录。失败返回底层 Core IO error + `saveWorld2DAuthoringDocument=replace` context，不改变 document、
revision/history 或已存在的目标文件；该 API 不引入 editor-only wire format。

`loadWorld3DAuthoringDocument()` / `saveWorld3DAuthoringDocument()` 对 Prefab v4 提供同一读取上限、clean baseline、
atomic sibling replace 与失败不变契约。

`saveSpriteAnimationAuthoringDocument(utf8Path, document, platform)` 把当前 `cookPreview(platform)` 的唯一 canonical
Cooked artifact 原子替换到目标文件。`saveTileMapAuthoringDocument(utf8Root, document, platform)` 按每个
`CookedArtifactPath` 写 current-schema artifact，先发布全部 TileMapChunk、最后发布 TileMap root，并返回 artifact/byte
count。两者都创建父目录，不写 manifest，不维护 editor-only 或旧 schema 格式。

## Asset 与 Cooked

`AssetFormat` 定义 versioned manifest/cooked wire format、World2D snapshot 和 Texture2D/StaticMesh/SkinnedMesh/
AnimationClip3D/Material/Prefab/EnvironmentMap/TileMap/TileMapChunk/AudioClip 等 typed payload。Runtime 不解析源
glTF/WAV/image；cgltf/stb_image 与源文件解析只在 Cooker/tool。SkinnedMesh v1 冻结为 P3N3T4UV2 + U16 index、固定
4 influences、最多 256 joints；AnimationClip3D v1 冻结为最多 768 tracks、4096 keys/track、262144 total keys、
1048576 value floats、3600 秒，只有 LINEAR/STEP。

Prefab 当前唯一 schema 为 v4：208-byte named node payload 自带 Mesh/Material `AssetId`；Cooked dependency 是按 `AssetId`
排序去重的 required 引用集合，mesh dependency 可明确声明 `StaticMesh` 或 `SkinnedMesh`；typed parser 对 payload
与 dependency 完整对账，不按 dependency 位置恢复 node identity。

`Asset::parseSkinnedMeshFromCooked()` 与 `parseAnimationClip3DFromCooked()` 同时校验 Cooked kind、type version 和
payload wire；返回的 span/view 借用 `CookedAssetFile` bytes，caller 必须保活 file。两类 v1 Cooked asset 都没有
Catalog dependency：SkinnedMesh 内嵌 skeleton/inverse bind，AnimationClip3D 携带 jointCount；
`Animator3D::Create()`/`setClip()` 在复制 view 前要求该 count 与 skeleton 精确相等。

`cookGltfFileToCatalogRequest(gltfUtf8Path, targetPlatform, ids)` 是 `noexcept` Cooker 边界，输入路径必须是 strict UTF-8
without NUL，目标平台必须显式给出且不能为 `Invalid`。它从已打开主文件的有界快照解析 JSON/GLB；relative external buffer/image 先 percent-decode，
拒绝 scheme、rooted path 与 `..`，再打开并以最终 handle/fd 路径验证 authoring-root containment。root 内
symlink/junction 保持可用，逃逸、读取期间身份/size/time 变化或任一 file/count/range/parser/decode/output
预算失败都返回 `Core::Error`，不返回部分 `CatalogCookRequest`。调用方随后仍须经 `cookCatalogPackage` 与
package publication；该 API 不让 Runtime 直接消费 source URI，也不暴露 cgltf/stb/native handle。

`cookAndStageCatalogPackage(stagingRoot, request, config)` 先完成内存 cook，再原子取得一个调用方指定且此前
不存在的 staging root，只在该私有目录写 object/manifest，并强制完整 on-disk/content validation。成功返回
owning immutable `CatalogSnapshot`，此后 staging root 必须保持 immutable；cook 失败不创建目录，publish 或
validation 失败可保留私有 partial stage 供诊断，但不会触碰 live root。已有目录返回 `AlreadyExists` 且不修改
其中内容。`publishCatalogPackage()` 仍是 manifest-last 的 best-effort 原地写入，不是多文件替换事务。

`cookAndStageIncrementalCatalogPackage(stagingRoot, baselineRoot, baseline, cleanAssetIds, dirtyRequest, config)`
只接受已完整验证的 baseline snapshot，并在此前不存在且解析后位于 baseline 外部的 staging root 组装候选包。clean object 从 baseline owning read
后逐字节复制，不使用 hardlink；dirty asset 使用唯一现行格式 cook。API 在创建 stage 前拒绝重复/冲突 AssetId、平台
不一致、缺失或错误 kind 依赖、cycle 与无效 TileMap 跨 unit 引用，随后重建 manifest、写入并强制 full content
validation。成功返回 immutable `CatalogSnapshot`；API 不移动或修改 live root。

`captureCatalogPackageRevision(root, config)` 对完整 manifest bytes 计算固定大小 `ContentHash` revision；
`pollCatalogPackageChange(root, baseline, config)` 返回 `Unchanged|Changed` 与 candidate revision。检测器只观察
manifest commit marker，不扫描 object/source，不启动线程，也不会自动推进 baseline。调用方只有在 candidate
对应 package 通过完整 validation/reload 后才接受它；失败时继续使用旧 baseline，下一次 poll 会重复报告变化。
manifest scratch bytes 使用显式 PMR 与 `maxManifestBytes`，输出不持有 manifest buffer。

`CatalogPackageWatcher::Create(root, config)` 是 move-only opaque OS hint owner，公开头不暴露 native handle。
Create 在返回前 arm Windows overlapped `ReadDirectoryChangesW` 或 Linux non-blocking inotify；调用方随后再捕获
revision baseline，避免 watcher/baseline 之间留下事件缺口。`poll()` 只消费已就绪事件并返回
`Quiet|Changed|RescanRequired` 与匹配事件数：只匹配 manifest 直接父目录中的目标文件名，write/rename/delete/replace
产生 `Changed`，queue overflow、事件截断或目录失效产生 `RescanRequired`。hint 不读取 package、不启动线程、不推进
baseline，也不替代上面的 revision poll/full validation/reload；目录失效后调用方重建 watcher。Windows/Linux 之外返回
结构化 `Unsupported`，不保留 polling fallback。

`SourceImportMetadataFormat` 是仅供 Cooker/tool cache 使用的独立 `TINAIMPT` schema `1.1`，不进入 Runtime
`manifest.tmnft`，也不会随产品 Catalog 分发 source path。它保存 stable `SourceImportUnitId`、target/importer
version/settings hash、root-relative strict UTF-8 source path + content fingerprint/read extent、unit input/primary edge、唯一
owned output AssetId/kind，并以 Catalog manifest digest + byte size 绑定产生它的已验证 package。parser/writer 只
接受当前 schema，旧 schema、非 canonical layout/path、reserved、overflow、重复 unit/source/output owner 或缺失
primary 都直接失败；项目开发期不提供旧 import-state 兼容分支。

`validateSourceImportCatalogBinding(metadata, revision)` 在复用任何旧 cooked object 前确认 import state 与当前
Catalog manifest 一致；`validateSourceImportCatalogOutputs(metadata, catalog)` 进一步要求每个 output `(AssetId, kind)`
与 Catalog entry 一一对应且不存在未归属 entry；任一不一致均禁止复用。`planSourceImports(baseline, candidate, config)` 纯比较两个已验证
metadata view，按 stable UnitId 输出 `Added`/`Removed`/`Reimport`。target、importer kind/version、settings、source
membership/path/content/byte size/read extent、primary edge 或 output AssetId/kind 任一变化都会使匹配 unit 整体 `Reimport`；
同一 source 可被多个 unit 引用，其 fingerprint 变化会标记所有消费者。结果使用调用方 PMR/`maxChanges`，失败
不返回部分 plan，也不推进 baseline。

`captureSourceImportBytes()` 对 caller 已读取并实际消费的 bytes 建立 root-relative source fingerprint，并强制 caller
声明唯一现行 `WholeFile`/`Prefix` read extent，不自行
读取文件；`loadCatalogCookRecipeSourceFile()` 与显式接收 target platform 的 `cookGltfFileToCatalogSourceResult()` 在唯一现行 importer 路径
分别收集 recipe/WAV/generic payload 与 glTF/GLB/external buffer/image provenance。一个 authoring document 当前
对应一个 stable unit，outputs 覆盖本次 request 的全部资产。`commitSourceImportCandidate()` 生成唯一当前 schema
并 atomic replace；`tina_assetc` 只在对应 package 完整验证并取得 manifest revision 后调用它。
`probeSourceImportUnits()` 对完整预期 UnitId 集合协调 per-unit probe，分别保留 clean unit 并统计 removed unit；
`probeCatalogRecipeSourceImportState()` / `probeGltfSourceImportState()` 是单描述 wrapper，走同一 batch 逻辑。
`composeSourceImportCandidate()` 把 baseline clean unit 与本次 recooked candidate 合成唯一 current-schema graph，拒绝
source fingerprint 冲突、重复 UnitId/output owner 与 target platform 不一致。`tina_assetc` 从 recipe 推导批次 target，
纯 glTF 批次使用 host target，再据此只运行 dirty/added
importer，再通过 incremental stage API 复制 clean object、移除 removed output 并完整验证 fresh stage。Cooker API 不启动 watcher，
也不物理替换仍在使用的 live root；Runtime/tool host 可显式组合独立 `CatalogPackageWatcher`。

`executeSourceImportPipeline(request, stopToken)` 是 recipe/glTF host 共用的同步高层工具 API。request 必须给出完整 intended
unit span、显式且非 `Invalid` 的 target platform、source/baseline/state 与 fresh-stage 路径；实现统一执行 baseline current-schema validation、batch probe、all-clean
零改写复用、dirty/added recook、removed output 剔除、candidate compose、fresh package 完整验证和 stage-bound state commit。
结果明确报告 `CleanReuse|FullRecook|IncrementalRecook`、unit/object 统计及 stage/state ownership。该 API 不调用
`AssetSystem::reloadCatalog()`、不替换 live root，也不取得 UI/Render owner；host 可在后台调用，随后在 owner thread 安全点
决定是否接受 stage。stop、IO、容量、validation 或 allocation 失败不返回部分 candidate。

`planCatalogChanges(oldCatalog, newCatalog, config)` 比较两个已验证、immutable `CatalogSnapshot`，返回按
`AssetId` 排序且每 ID 唯一的 `Added`/`Removed`/`Modified`/`Affected` 行。`Modified` 覆盖 entry metadata
和完整 dependency contract；`Affected` 是新 Catalog 中对 Added/Modified 的 reverse-dependency 传递闭包，
直接变化优先。调用方显式提供 PMR 与 `maxChanges`，该 PMR 必须覆盖结果生命周期；容量或分配失败不返回
部分 plan。planner 不绑定 Catalog、不使 Handle 失效，也不取得 AssetSystem/Lease/GPU owner。

`AssetSystem::reloadCatalog(root, config)` 是同步、owner-thread-only 的 resident CPU generation + active GPU owner
transaction。它强制完整打开并验证新的 Catalog package（reload 路径不会接受关闭 on-disk/content validation），使用
`config.changePlan` 生成变化，再为当前 resident 的 Modified/Affected asset 及其新增依赖预加载 replacement generation。
`config.bindings.sprite2D/mesh3D` 是仅在本次调用借用的 registry pointer spans；每个 participant 必须非空、唯一、属于
当前 AssetSystem 并共享 owner thread。Sprite participant 先 prepare，Mesh participant 后 prepare；失败按 Mesh→Sprite
逆序 abort。所有 candidate 读取、Store 双驻留容量、`maxResidentMigrations`、新 index/root/result 分配与 participant
prepare 成功后，才无分配地原子切换 root、immutable Catalog、AssetId lookup 与 registry binding。返回
`CatalogReloadResult`，其中 change plan 与按 AssetId 排序的 resident migration 将旧 weak Handle 映射到
`Replaced|Removed|LoadedDependency` 的新 generation；旧 `AssetLease` 继续读取旧 payload，释放最后一个 lease 后旧
generation 才物理回收。任一步失败都会卸掉 staged generation、abort replacement GPU owner，并保留旧
root/Catalog/index/Handle/registry Entry。

reload 允许 active resident Handle/Lease，但 pending queue、in-flight IO、tracked GPU upload 与 retirement record 必须为空；
非 owner thread 返回 `WrongOwnerThread`，非 quiescent 工作状态返回 `CatalogReloadBusy`。Store capacity 必须显式保留
replacement generation 的双驻留 headroom，容量不足返回 `CatalogCapacityExceeded`。已有 Catalog 时，低层
`bindCatalog()` 仍受完整 idle 门禁约束，不能绕过 migration API。active frame borrow 会在 publish 前拒绝整个事务。
commit 后 replacement 立即成为唯一 active binding，旧 GPU owner 进入 registry fixed-capacity pending retirement；
best-effort drain 被 backend 拒绝时不回滚已经发布的 Catalog，而由 `pendingRetirementCount()` 与
`drainPendingRetirements()` 保留可重试 owner。AssetSystem 不自动发现 registry，调用方必须显式传入所有需跨 reload
继续服务的 active participant。

TileMap 的唯一当前 root wire contract 是 schema v3。`TileMapPayloadView` 按 authoring 顺序通过
`layerAt()/findLayer(TileMapLayerId)` 暴露 tile/object layer；稳定 layer/object ID 都是 map-wide 非零唯一
`u32`。layer 与 object 都有独立 visibility；name/properties 是 strict UTF-8 borrowed views；object kind
当前只有 Point 和 axis-aligned Rectangle。tile layer 保存按坐标排序的非空 chunk ref，缺失坐标是已知
空块；cell 位于独立 `TileMapChunk` v1 payload。旧 schema v1/v2 均不兼容，也没有默认单层 API。
`deriveTileMapChunkAssetId()` 是 AssetFormat 的公开 current-schema identity helper；Cooker 与 Editor 共用它，输入 map/layer/
coordinate 非法时返回结构化错误，不维护第二份 editor-only 派生算法。

`TileMapInstance` 拷贝验证后的 root metadata/tileset 定义，只为当前 resident chunk 持有可变 cells、
content revision 与 residency generation。`layer(id)` 返回借用到 instance-owned payload 的 metadata/object
view；`tileIdAt()`、`tileInfoAt()`、`setTile()`、`chunkRevision()`、`querySolidAabb()`、chunk extraction/
dirty cache/sprite emit 与 `TileMapGridCollision` 都要求显式 `TileMapLayerId`。引用存在但尚未驻留时返回
`TileMapChunkNotResident`；误选 object layer 与不存在 layer 分别返回
`TileMapLayerTypeMismatch`/`TileMapLayerNotFound`。grid SPI 的 `materialFlagsAt()` 仍按约定把无效、空或
未驻留 cell 表现为0。visibility=false 会跳过可见 chunk/sprite emit，但不禁止显式用作 collision。

`TileChunkSpriteEmitParams` 保存 copyable weak Tileset `AssetHandle` 与 borrowed
`AssetFrameResourceResolver`，不保存 render key/ref，也不取得 Lease/payload/GPU owner。resolver、sink 与
user data 只在当前 emit 调用内有效。单 chunk
有实际 tile 时解析一次；`emitVisibleTileMapSprites()` 对完整非空可见集合只解析一次。hidden、off-camera、
empty 不调用 resolver；空 handle 返回 `InvalidHandle`，missing/zero binding 返回 `SpriteBindingNotFound`，
任一失败都清空调用方输出。

`TileMapStream::Create()` 消费 root/tileset `AssetLease` 并拥有 resident `TileMapInstance`。调用顺序必须是
`updateDemand() -> AssetSystem::pump() -> commitReady()`。load window 中的 desired chunk 单独超过
resident capacity 时 failure 是 transactional，旧 active set 保持不变；retain window 只是 optional
cache，overflow 时按最近一次成功 demand update 的 recency 自动淘汰，读取 API 不 touch recency。同一 chunk 的 demand
取最高 `TileMapChunkDemand::priority`；新请求按 `priority desc -> layerId -> chunkY -> chunkX` 稳定 dispatch，只消费本轮
request budget，不重排或抢占已经 Requested/Resident 的 slot。
`map()` 是借用视图：先把 stream 放到最终地址再创建 `TileMapGridCollision` 或
`TileMapPhysicsSync2D`，stream 不得在 borrower 存活时移动，并必须早于它所引用的 `AssetSystem` 析构。

`Physics2D::createStaticBodyForSolidRectangles(world, rectangles, cellSizeMeters, material)` 是 Physics2D
侧唯一的 grid collider 入口：一次调用创建**一个 static body**，并为每个 `PhysicsGridSolidRect2D` 挂一个
box shape。空 span 是成功的 no-op 且返回空 body id；非法矩形（零宽/零高、cell 越界、非有限中心/extent）
返回 `InvalidShapeDescription`，非法 `cellSizeMeters` 或 material 返回 `InvalidConfiguration`，两者都在
创建任何 body 之前拒绝。任一 shape 创建失败会销毁该 body 与本次已创建的全部 shape。Physics2D 不 include
TileMap，也不感知 chunk。

`Asset::TileMapPhysicsSync2D` 是唯一现行 TileMap→Physics 桥；逐 cell 的
`collectSolidCellsForPhysics()`、`collectAllSolidCellsForPhysics()` 与
`syncTileMapSolidsToStaticBodies()` 已删除，不提供兼容别名。`Create(map, config)` 绑定一个 tile layer 并
在此处完成全部持久分配（chunk record、rectangle scratch、occupancy、staged/retired 列表）；
`rectangleCapacityPerChunk=0` 表示取源 chunk 的精确 cell 数，超过一个 chunk 的值、`layerId=0`、
非 tile layer、超出 chunk capacity 上限与非法 material 都在发布前失败。`synchronize(map, world)` 是
owner-thread 调用，只遍历 resident chunk，按 `residencyGeneration` + `contentRevision` 判定
unchanged/added/rebuilt/removed；unchanged chunk 保留原 body，不销毁重建。变化 chunk 经确定性 greedy
rectangle 合并后整体 staged，全部成功才退休旧 body；bake/create/capacity 失败保留上一次成功发布的
collider 并且不推进 `stats()`，容量不足返回 `AssetErrorCode::TileMapPhysicsCapacityExceeded`。
`Create()` 之后稳态零分配。绑定契约（tile map/tileset AssetId、尺寸、chunk size、cell size）不匹配的
map 会被拒绝，world 关闭时返回 `WorldClosed`。必须在 world 关闭或 `TileMapInstance` 消失之前调用
`shutdown(world)`；它 owner-thread 幂等，成功后对象仍可重新 `synchronize()`。该类型不持有
`AssetLease`、GPU resource 或 Scene 状态，只保存 generation-aware 的 runtime body handle。

`AssetSystem` 提供 request/load/pump、generation slot 与 typed state。`AssetHandle` 是弱 lookup；
`AssetLease` 强保活 CPU payload。逻辑 invalidation 不等于物理释放。产品 helper 可把 Cooked Texture2D/
StaticMesh/SkinnedMesh 上传到 RenderDevice，并建立 backend key binding；`AssetSystem::retireTexture2D` /
`retireGpuMesh` 把 lease 移入 `FramePin`，成功后弱 lookup 立即失效，backend completion 后才释放 payload。
Texture2D 与 GPU mesh 的 `AssetLease&` + 对应 GPU generation handle ref overload 仅在 backend 接受后
消费两者；失败完整恢复供重试。`drainGpuRetirements()` 用于 owner-thread teardown。

`Sprite2DBindingRegistry::Create(assets, device, config)` 必须在借用 `AssetSystem` 与 RenderDevice 的共享
owner thread 调用；该线程成为固定容量 registry 的 owner，所有后续操作也必须在同一线程执行。
AssetSystem 与 device 均为借用且必须保持最终地址，覆盖 registry 以及已经 handoff 的 GPU retirement
pin 生命周期；非空自定义 `memoryResource` 只需覆盖 registry storage 生命周期。
`registerTextureBinding(textureHandle, gpuTexture&)` 校验 live Texture2D Handle，取得一份 `AssetLease`，
再通过借用 device 的 `createTexture2DBinding()` 事务映射为非0 `u32` key。只有完整成功才把
Lease/GPU owner 发布到固定 Entry 并清空调用方 GPU handle；handle/kind/state、duplicate handle/AssetId/
GPU owner conflict、capacity、lease acquire 或 backend bind 任一失败都保留调用方 GPU，且不留下
Lease/Entry。拥有语义下 exact duplicate 也是 conflict；已有 key 通过 `bindingKey()` 查询。key 在该 RenderDevice 实例 namespace
内唯一、单调且 retirement 后不复用。
`retireTextureBinding()` 在没有 active frame borrow 时把 Entry 的 Lease/GPU 直接交给
`AssetSystem::retireTexture2D()`。Render retirement 成功会原子失效 texture generation 并清除所有引用
binding；失败零突变，Entry 可重试。`retireAllTextureBindings()` 先全表检查 frame borrow，再逐 Entry
handoff；它允许已成功前缀提交，失败项与后续项保留供重试。Registry 析构要求 Entry 已空，否则 fail-fast。
作为 Catalog reload participant 时，registry 在 publish 前为 Replaced Texture 上传并创建 replacement binding，
Removed entry 仅做 staged removal；active frame borrow、upload/binding 或后续 participant 失败都保持旧 Entry。
commit 后旧 Lease/GPU owner 转入 fixed-capacity pending retirement，backend reject 保留 owner 供显式 drain 重试。
`resolveSprite()` 与 `resolveTileset()` 分别沿 Cooked Sprite/Tileset 的唯一 required Texture2D dependency
fail closed 返回当前低层 key；产品 extraction 使用 `internSpriteFrameResource()` /
`internTilesetFrameResource()` 将 binding 登记到当前 sink。同帧重复 descriptor 返回同一 ref，首次 pin
阻止 retirement，直到 packet complete/skip/abandon。registry 是 Sprite2D resident Lease/GPU/binding
的唯一 owner，但不是 Scene owner；通用 `AssetFrameResourceResolver` 位于窄 `AssetTypes` target，A1 的
Scene 语义 alias 已删除；2D Sprite 与3D mesh/material resolver 都直接使用同一个通用 frame-resource
seam。

`GpuTextureId`/`GpuMeshId`/`GpuEnvironmentMapId` 携带非零 RenderDevice owner + index + generation；Null/bgfx 的 bind、validate、
destroy/retire 都校验 owner，因此即使两个 live device 恰好具有相同 index/generation，cross-device handle
也会 fail closed。handle 仍可复制，registry 的唯一 GPU owner 与 handoff 契约继续禁止 alias cleanup。

`setTexture2DBinding(callerKey, texture)` 提供 direct binding/clear SPI，但 caller-chosen key 与上述
allocator 使用同一个 device namespace。allocator-managed registry 管理期间不得混用 direct caller key；
device 不会为 direct setter 自动保留或跳过该 key。

`Mesh3DBindingRegistry::Create(assets, device, config)` 是 fixed-capacity、owner-thread owner，借用
`AssetSystem`、`IRenderDevice` 与可选 PMR。mesh/material 使用独立 device-instance key namespace，两类 key
都从2开始并分别保留内置 key 1；成功绑定后才消费，retirement 后不复用，共享同一 device 的多个 registry
仍获得 distinct key。`registerMeshBinding(mesh, gpuMesh&)` 成功后独占 StaticMesh Lease/GPU/binding；
`registerSkinnedMeshBinding(mesh, gpuMesh&)` 使用同一 mesh key namespace，但只接受 SkinnedMesh handle 与
`createSkinnedMesh()` 生成的 GPU owner，并发布 distinct `SkinnedMesh3DGeometry` frame-resource kind；
`registerMaterialTexture(texture, gpuTexture&)` 按 AssetId 唯一取得共享 Texture Lease/GPU owner，并在 owner
转移前通过 `validateTexture2D()` 拒绝 wrong-owner/invalid/stale 候选；
`registerMaterialBinding(material)` 从 Cooked payload 解析 roles/factors，只引用已注册 live Texture owner，并
通过单次 `Mesh3DMaterialBindingDesc` 原子发布 bundle。多个 Material 可共享同一 Texture owner；同一
Material 内的 role dependency 仍由 Material v2 严格顺序与唯一性约束。

`internMeshFrameResource()` / `internSkinnedMeshFrameResource()` / `internMaterialFrameResource()` 每次按当前
Store state fail closed，并把 binding 登记为 packet-local `Mesh3DGeometry` / `SkinnedMesh3DGeometry` /
`Mesh3DMaterial` ref。首次 intern 的 Entry borrow pin 阻止
active frame retirement。Material retirement 清除 bundle 并减少 Texture 引用；有 live Material 引用时
Texture retirement 失败。Mesh/Texture retirement 通过 AssetSystem 的 lease-consuming transaction 提交，
失败保留 Entry；Catalog reload participant 联合 prepare Mesh、Material 与共享 Texture，并可为 replacement Material
取得新 `LoadedDependency` Texture owner、复用 removed/free slot。全局 commit 后旧 owner 按
Material→Texture→Mesh 顺序进入可重试 retirement；`retireAllBindings()` 使用相同关闭顺序，Registry 析构要求 active
与 pending storage 全空。调用方不再持有第二份 GPU owner、registered flag 或持久 device key。

multi-mesh / multi-primitive glTF Cooker：每个 TRIANGLES prim 生成 distinct StaticMesh/Material AssetId；
单 prim 节点直接引用，多 prim mesh 在 Prefab 中展开为 transform 父 + 子 draw 节点。Material v2 含
metallic/roughness factors、显式 `Opaque`/`Blend` alpha intent 与可选 baseColor/MR/normal Texture2D deps；glTF
`MASK` 与未知 alpha mode 当前均 fail closed。Registry 将同一 alpha intent 原子写入 `Mesh3DMaterialBindingDesc`，Scene renderer、
packet item 与 device binding 必须一致。Runtime Opaque3D/Transparent3D 共用 Cook-Torrance GGX；
engine-provided、State-owned registry 使用原子 `setMesh3DMaterialBinding` 提交 baseColor/MR/normal/factors/alpha mode，
direct 细粒度 setter 仍属于低层 SPI；lighting 使用有界0..4 directional + 0..8 point + 0..8 spot lights，
World directional/point/spot component 每帧提取到 RenderScene，point/spot influence sphere 在容量检查前
按相机裁剪。Opaque static item 保持相邻实例 batch；Blend static/skinned item 进入同一个固定容量
back-to-front 全序，等距时以 stable Entity identity、kind、item index 决定，容量不足使本次 build 事务失败。
StaticMesh v1 固定为 P3N3T4UV2：glTF authored `TANGENT` 优先，否则
NORMAL+TEXCOORD_0 primitive 由 Cooker 使用 MikkTSpace 生成；缺少 NORMAL/UV 显式失败。Opaque3D
只使用 vertex tangent TBN。`EnvironmentMap` cooked v1 固定封装 RGBA16F diffuse irradiance cubemap、带完整
mip 链的 RGBA16F prefiltered specular cubemap 与 RG16F BRDF LUT；`uploadEnvironmentMapFromCooked()` 只把
typed view 交给 `createEnvironmentMap()`。三张 native texture 共享一个 `GpuEnvironmentMapId`，create/validate/
destroy/retire 与 failure rollback 均为一个事务。`Mesh3DImageBasedLightingDesc` 绑定 live handle、非负 intensity
与 world-Y rotation，`clearMesh3DImageBasedLighting()` 显式恢复无 IBL 状态。一个 directional light 可投射固定4级联
2×2 D16 atlas 阴影（默认2048×2048、每 tile 1024×1024）；optional `CascadedDirectionalShadow3D` 的 `maximumDistanceMeters`、`depthBias`
与 `normalBiasMeters` 随帧 snapshot 深拷贝，Render 侧以排序后的 `directionalLightIndex` 关联灯光。
`SpotLight3D::shadow` 可携带 `SpotLightShadow3D`；`nearPlaneMeters` 必须正且小于该灯 influence radius，
depth/normal bias 必须有限且有界。每帧最多一个 camera-affecting spot shadow，Scene 在 culling 与稳定排序后
把它深拷贝为 `Mesh3DSpotLightShadow`，以 `spotLightIndex` 关联 Render 灯槽。`PointLight3D::shadow` 同样
携带 near/depth/normal bias；每帧最多一个 camera-affecting point shadow，Scene 深拷贝为
`Mesh3DPointLightShadow` 并以 `pointLightIndex` 关联灯槽。Render scheduler 固定按 CSM×4 → Spot×1 →
Point×6 → Opaque3D → Transparent3D → Sprite2D → UI 排序；Transparent3D 使用 straight-alpha blend、
depth test less 且不写 depth。透明 static/skinned 不进入 shadow caster pass，但仍接收 lighting、shadow、
PBR 与 IBL。bgfx 为 point shadow 私有持有按 `+X/-X/+Y/-Y/+Z/-Z` 排列的六张
sampled D16 map（默认512×512），receiver 以 dominant axis 选面并执行3×3 PCF。
`ShadowMapExtentConfig` 以 `[128,4096]` 内2次幂分别配置 directional cascade tile、spot map 与 point face；
默认值为 `1024/1024/512`，directional atlas 固定为2×2 tile。`EngineConfig::shadowMapExtents` 只在创建
device 时传播到 `RenderDeviceCreateParams`，EngineHost 与 Null/bgfx direct factory 都对非法值 fail closed。

## Audio 与 Physics

`AudioEngine` 提供 generation voice、bus、bounded command/completion、non-owning clip PCM、Tina-owned
bounded stream ring 与 realtime mix。
voice 控制入口为 `setVoiceGain()`、`setVoicePitch()`、`setVoicePan()`、
`startVoiceFade(AudioVoiceFadeDesc)`、`cancelVoiceFade()` 与 `voicePlaybackState()`；gain、pitch、pan 的公开
范围分别为 `[0,1]`、`[0.25,4]`、`[-1,1]`。所有 setter 只接受 live owner voice 和有限值，失败不发布
半份 realtime 状态。

pitch 使用 `sourceSampleRate / outputSampleRate * pitch` 的 source step 与线性插值。stereo pan 使用兼容
既有 center 响度的 linear balance，hard-left/right 只衰减对侧；mono 输出忽略 pan。fade 由 target gain、
正的 `Core::Duration` 与 `KeepPlaying/StopVoice` end action 构成，并在 realtime callback block 边界
start/cancel；cancel 保留 callback 已推进到的 current gain。

clip PCM 调用方必须保活到底层 voice stop/completion；产品 2D 用 `AssetLease` 持有 Cooked AudioClip。普通
`createVoice()` 由调用方显式销毁；`playOneShotPcm()` 的 transient voice 在 Stop、fade-to-stop 或 natural
end 被 pump 后自动 retire，completion 携带的 one-shot ID 随后允许 stale。miniaudio device/decode 留在
adapter。

bounded stream 公开入口为 `playPcmStream()`、`submitPcmStreamFrames()`、`signalPcmStreamEof()`、
`cancelPcmStream()` 与 `pcmStreamState()`。Create 一次性预分配每 voice 的双声道最大 ring；descriptor 的
逻辑容量至少为2且不得超过配置。`AudioPcmStreamChunkView` 只是调用期 borrow，successful submit 返回前已
整块复制，容量不足不发布半块。owner thread 是唯一 producer；Task worker 必须 marshal，`Tina::Audio`
不依赖 `Tina::Task`，miniaudio 也没有 producer API。`mixRealtime()` 只允许一个 non-overlapping realtime
consumer。EOF 排空后单次 Stopped、cancel 单次 Cancelled，terminal completion 受 ring/reader backpressure
时延迟但不丢，成功 pump 后 transient stream voice 自动 retire。

`PhysicsWorld2D` 提供 Box2D-backed fixed-step world，以及相互独立的 body/shape/joint generation ID。
唯一创建模型是 `createBody()` + `createShape()`；backend-neutral `PhysicsShape2DDesc` 支持
Box/Circle/Capsule/ConvexPolygon 与 sensor，一个 body 可拥有多个 shape。ConvexPolygon 接受3..8个严格凸、
顺/逆时针边界顶点及有限 local transform。sensor enter/exit 通过 contact view 的 `isSensor` 表达；joint 支持
Distance/Revolute/Prismatic，`jointState()` 返回适用于当前 kind 的 spring/limit/motor backend snapshot，
并有 create/query/destroy 与关联 body 级联 retirement。公共面还
包含 query、deferred command 与 Tile grid static body helper。Box2D 类型不出现在 public header；
Jolt/Physics3D 尚未接入。

## Handle 与借用速查

| 类型 | 所有权 | 典型失效点 |
| --- | --- | --- |
| `EntityId/UINodeId/PhysicsBodyId/PhysicsShapeId/PhysicsJointId/...` | registry owner | erase/owner destroy/generation reuse |
| `AssetHandle` | 弱 | slot invalidation/reuse |
| `AssetLease` | 强 CPU payload owner | lease reset/destroy |
| `AudioVoiceId` | `AudioEngine` generation owner | 显式 destroy、engine shutdown/generation reuse；one-shot/stream terminal completion pump 后自动 retire |
| `TileMapStream::map()` | `TileMapStream` 借用 | stream move/shutdown/destroy；borrower 必须先销毁 |
| `NavigationBlockerId` | `NavigationGrid2D` generation owner | remove/grid destroy/generation reuse；跨 Grid 无效 |
| `NavigationPathfinder2D::path()` | Pathfinder 借用 | 下一次 begin/reset 或 Pathfinder 析构 |
| `GpuTextureId/GpuMeshId/GpuEnvironmentMapId` | RenderDevice | retire/destroy 时逻辑失效；有外部 pin 时由 completion marker（或 shutdown hard drain）证明完成，无 pin fallback 交给 backend deferred destroy |
| PlatformFrame view | Platform | 下一次 poll/build |
| Phase context/writer | Runtime callback | callback 返回 |
| committed UI view | UIContext | 下一次对应 commit/context destroy |
| RenderFrame view | Runtime builder | `submitFrame()` 返回 |
| `AudioPcmClipView` | non-owning | 调用方 payload 释放；必须晚于 voice completion |
| `AudioPcmStreamChunkView` | 调用期 non-owning | `submitPcmStreamFrames()` 返回；成功数据已复制到 Tina-owned ring，失败零发布 |
| `TileMapLayerPayloadView` / object/property view | `TileMapPayloadView` 或 `TileMapInstance` 借用 | backing payload 释放；instance view 还会在 instance move/destroy 时失效 |

不要手工构造 generation ID、跨 owner 混用、持久化 Runtime handle，或把 non-owning view 包装成“看似
拥有”的裸 pointer成员。

## 尚不存在或仅部分落地的公共能力

**已存在（勿再文档成“没有”）：** `GameStateStack` 与 structural commands；相位 `blocks*Below` 与
`blocksGameplayInputBelow` 空 snapshot；`RenderFramePacket` / `FramePin` / present-return CPU
submission ledger；Focus Scope/Modal/持久 Pointer Capture；ScrollView/Dropdown/Popup/虚拟
ListView/TreeView/VirtualGridView/DataGrid；Tooltip、Menu/MenuItem、SplitView/Splitter、TabView/Tab；`UIFlowLayerId`/`UIFlowScreenId`、固定容量 Screen stack、16 槽 `UIFlowLocalUserId` 与 Gamepad assignment；accessibility action seam 与 Windows UIA provider + HWND HostBridge +
Invoke/Toggle/RangeValue/Value patterns；immutable weighted Navigation2D grid、动态 blocker、四向/对角同步与
分步 A*；allocation-free `CameraFollow2D`；Physics2D ConvexPolygon 与 Revolute/Prismatic joint。

**仍不存在或未完成：**

- 多 World / editor orchestration；
- 通用 Runtime owning event queue；
- 通用 GPU submission fence（现有 readback marker 只服务 Texture/Mesh/EnvironmentMap retirement）；
- TileMap 更高层 editor orchestration；
- BiDi/复杂 shaping、Linux 原生 XIM/Wayland preedit/candidate placement，以及 Windows 真机 IME 候选窗人工金标；
- generic TextInput/Scroll/Select 输入路由；
- stylesheet 更广 opacity 等属性面、layout property 白名单扩展与高级 Motion playback；imageTint、paint-only
  transition、typed paint/bounded-layout timeline 与 ColorToken reverse-dependency 更新已落地；
- Back/Confirm/Menu 之外的任意产品 action-id；
- Narrator/Inspect 合规金标、Linux AT-SPI；
- Jolt Physics3D；
- 安装 SDK 的正式 supported ABI tuple baseline/previous-object probe；ADR 0024 的版本策略和 pre-1.0
  strict exact-version（含相邻版本/tweak/range 反例）probe 已落地，Windows/Linux moved-prefix 及 Ubuntu producer → Debian consumer 的
  artifact transfer gate 已覆盖当前源码契约，但不替代旧对象兼容证据。

任务状态见 [Backlog](backlog.md)。修改公开头后必须构建 header-isolation/consumer、扫描第三方 token，
并按 [测试说明](testing.md) 运行受影响 executable 与 sample。
