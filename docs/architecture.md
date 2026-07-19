# 架构总览

## 当前双轨实现

Tina 当前处于 Legacy 产品与 vNext 垂直切片并存的迁移期。Legacy 仍以单个游戏可执行文件为主，
源码按 Core、Engine、Renderer、UI、ECS 和 Game 组织；vNext 已建立独立的 `tina_core`、
`tina_platform`、`tina_task`、`tina_render`、`tina_runtime`、`tina_scene`、`tina_asset_format` 与
`tina_asset`
八个 C++23 target，以及可选的
`tina_platform_glfw` adapter、私有 `tina_render_bgfx` backend、Desktop bootstrap、
M7-C1b/M7-C1c-a/C1c-b1/C1c-b2 `tina_ui` tree/layout/committed-hit/point-query/synthetic-route foundation，
以及 M7-C1c-b3b/b3c Runtime-private UI route-result producer、primary-window `UIContext` owner 与
`EngineHost` 接线、M7-C1c-b3d1 的公开容量配置和 Runtime-private layout coordinator、
M7-C1c-b3d2 的 startup UI seed 与 Game SDK scoped capability、后续 Game SDK root-scoped routed Pointer
listener extension，以及 M7-C1c-b3e 的 held primary
Pointer Button claim bridge、窄 Button default action，以及 SolidFill-only committed paint、Render-owned 单帧 DisplayList builder
和独立的 `tina_ui_render_integration` UI→Render bridge code surface，以及 D0 Runtime-private
primary-window UIDisplayList submit handoff、D1 私有 bgfx SolidQuad UI pass、D2 Game SDK box-paint
authoring、Desktop retained 4-panel visible sample，以及 M9-C 私有 bgfx Sprite2D fixture pass 与
`tina_sample_2d_infrastructure_bgfx` 2D/UI 300帧样例。
`tina_sample_null` 不依赖真实窗口和 GPU；`tina_sample_platform`
把私有 GLFW `NO_API` 窗口与 NullRender 组合；`tina_sample_desktop` 通过
`Tina::Desktop::CreateEngine(config)` 私有组合 `SteadyClock + GLFW WindowSurface + DisabledTaskSystem + bgfx`，
默认运行300帧真实 GPU deep-blue clear/present。M7-A 已加入有界 Platform Frame、输入
Snapshot/Transition、Platform 生命周期订阅以及 Simulation/Frame Action Mapping；M7-B1 已加入
generation `WindowSurfaceId`、无原生句柄的 `WindowSurfaceSnapshot`、move-only
`NativeWindowSurfaceLease` 和 WindowSurface-aware composition；M7-B2 已建立 bgfx clear-only core、
Desktop 产品接线和真实 GPU 冒烟；M7-C1a 已建立 standalone `tina_ui` tree core，M7-C1b 在保持
只 PUBLIC 依赖 Core/Platform 的前提下，实现 `UILayoutLength/UILayoutStyle/UIDirty`、固定容量
PMR style/dirty side array 与 dirty queue、非递归 Flex-lite Measure/Arrange、双缓冲 committed
layout snapshot，以及 pending structure+layout 的原子发布。M7-C1c-a 又加入固定容量 PMR Pointer policy/
route-ancestry scratch、`Ignore`/`Targetable`、双缓冲 `UICommittedHitView` 与 structure/layout/paint-order/hit
revision；同一 view 内 entry 的 paint ordinal 唯一且严格递增，hit-only commit 为0次 layout，失败的
`commitLayout()` 不发布 structure/layout/hit 三份候选 snapshot。M7-C1c-b1 又实现无分配
`queryPointerHit()`：反向扫描 committed hit entry，并返回 route index、snapshot revision 与 visited count。
M7-C1c-b2 又实现 synthetic routed pointer event：固定容量 route path/listener storage、48-byte fixed-inline
`noexcept` callback、generation-safe RAII token、Capture→Target→Bubble、stop/consume、mutation-safe
invalidation 与 route/commit reentrancy guard；M7-C1c-b3a 又让 Platform Button/Wheel transition 固化
事件时 logical position，避免 Runtime producer 用帧末坐标改变历史 hit-test。M7-C1c-b3b 又实现
Runtime-private `UIInputRouteProducer`：只路由 Move/Button/Wheel，按 raw ordinal 生成 consumption，
reset/cancel/非 Pointer 保留 hole；该 b3b 切片的 claims 当时恒为 canonical `None`。双预分配 PMR bitset 在300帧共用 PMR
测试中 allocation count 不增长，且 supplied `memory_resource` 必须长于 producer。M7-C1c-b3c 又让
`EngineHost` 在 `PlatformEventDispatcher` 后、`ActionMapper` 前惰性选择首个 primary `WindowId` 并由
Runtime-private owner 持有唯一 `UIContext`：绑定前的 Headless 帧使用 null Context；绑定后 primary 消失或
generation 变化会结构化失败，同一 ID 的 metrics/content scale/minimized 变化不重绑。owner 本身不隐式调用
`commitLayout()`，输入始终读取上一帧 committed snapshot，并在 Render → Task → Platform → Clock module shutdown 前于 owner
thread 销毁 Context。M7-C1c-b3d1 又把固定容量契约收敛为 focused public
`UIContextCapacityConfig`，通过 `EngineConfig::primaryWindowUICapacities` 在任何 factory 前统一校验，
并在 `updateUI` 成功后、Render submit 前由 Runtime-private coordinator 使用主窗口 logical extent
至多尝试一次 `commitLayout()`。Headless 帧在窗口与 Context 同时缺席时成功 no-op；提交失败阻断
Render 且消费当前 `PlatformFrameId` 的 attempt，不能同帧重放。M7-C1c-b3d2 已新增不 poll、
不消费 frame id 的 `initialPrimaryWindowMetrics()` seed，在 `IGameState::onEnter()` 前显式绑定 primary
`UIContext`，并在 State commit 前发布首份 structure/layout/hit/paint snapshot。Game SDK 只取得
`PrimaryWindowUIRootBuilder` 与绑定自己 root 的 `PrimaryWindowUITreeUpdater`；facade 以 owner thread
和 phase epoch 校验，回调结束后无条件失效，第一次 capability operation 失败作为 sticky phase error
回传 Runtime。后续兼容扩展又把 root-scoped `addRoutedPointerListener()` 加入低层 updater 与 Game SDK
facade；只有返回的 move-only token 可跨 phase 保存，且不延长 Context/root 生命周期。注册在 callback
最终 move 后重校验 root/generation/subtree，重入释放 root 会原子回滚；State 在 `onExit()` 先 reset token、
再释放 root。M7-C1c-b3e 已允许 routed Pointer listener 请求接管仍 held 的 primary Pointer Button，
Runtime 按最终 snapshot 过滤并去重，ActionMapper 取消或拦截 Gameplay source 并抑制到真实 Up。
随后 `tina_ui` 又实现 `UIBoxPaint` 的可选 SolidFill、本地预乘色 cache、`paintSnapshotCapacity` 和
双缓冲 `UICommittedPaintView`；成功 `commitLayout()` 现在事务发布 structure/layout/hit/paint 四份
snapshot，paint-only commit 不重排布局或 hit，同值 mutation 与无变化 commit 不发布新 revision。
`tina_render` 独立实现单缓冲、固定 PMR 容量的 `UIDisplayListBuilder`，当前只发射 SolidQuad，支持
axis-aligned clip interning、相邻兼容 batching、paint-order checksum、空/透明/clip 剪枝和整帧回滚。
`Tina::UIRenderIntegration` target 进一步把 committed logical paint 按 logical/framebuffer extent
比例投影成 pixel DisplayList；其12项直接 GoogleTest 已在 Windows MSVC 19.50 Debug/Release、Linux
GCC 13.4 与 Linux Clang 22 sanitizer 通过，Clang 无诊断。
D0 已在 Runtime private 层新增 `PrimaryWindowUIDisplayCoordinator`：正式帧在 layout/paint commit 后、
Render submit 前用固定 PMR builder 构建 primary-window `UIDisplayListView`，并把它作为
`RenderFrame::primaryWindowUIDisplayList` 的 submit-call-local borrow 传给 backend。backend 只能在
`submitFrame()` 调用内消费/复制/编码该 view，禁止保留 view、span 或元素指针。Headless、0 framebuffer
与 suspended surface 发布空 list；构建失败清空当次 publication，不回退旧 list，也不提交截断 list。
这些层随后已被 D1 私有 bgfx SolidQuad UI pass 消费：`tina_render_bgfx` 在 submit 前预检 transient
VB/IB、index32 能力与 batch，再把 SolidQuad 写入私有 vertex/index buffer，并按 UI batch 提交私有
shader program。D2 又把 `PrimaryWindowUITreeUpdater::setBoxPaint()` 暴露给 root-scoped Game SDK facade，
`tina_sample_desktop` 现在以4个 retained SolidFill panel 形成可见 UI 垂直切片。它仍不是完整 Widget
系统：owning Runtime `RenderFramePacket`、FramePin、文本/glyph/Label 文本、Button Keyboard/Gamepad
activation、Disabled/theme 视觉、Image/Texture、Key/Gamepad/axis claim、持久 Pointer Capture、
Focus/Modal、完整 dirty-range pruning 与 nested clip 仍未完成；当前已具备 clean-subtree
Measure/Arrange reuse，但 hit/paint/layout-order 仍可能线性遍历。

M8-A 已新增可独立构建的 `tina_scene`：`Scene::World` 使用固定容量 owner/generation entity
registry，提供 `EntityId`、Local/World Transform、非递归层级传播、keep-world/keep-local reparent、
父销毁提升、显式子树销毁与循环诊断。World 使用 dense live index 和两阶段 scratch publication，
对 Transform 溢出、四元数异常及当前 TRS 无法表达的 shear 返回结构化错误；读写均限制在 owner thread。
它当前只依赖 `Tina::Core`，不链接 EnTT/GLM/GLFW/bgfx；EnTT component storage 与阶段末 command
buffer 仍未实现。

最新窄 UI 切片已完成实现：Button 默认 `Targetable`，只增加
`PrimaryPointerId + PointerButton::Primary` 的 armed/pressed/Up activation、retained action callback、
`preventDefaultAction()` 和 cancel/reset 状态清理。该能力仍留在 `tina_ui` 与 Runtime-private input
producer，Game SDK 只取得 root-scoped setter/query；它不引入 SDL、Runtime/Render 依赖或 bgfx 类型，
也不等于 Focus/Capture、键盘/手柄、文本或完整 Widget 系统已经完成。

现有 Legacy target 的包依赖由 vcpkg manifest 管理，bgfx、EASTL、EABase 仍保持固定源码
版本；其中 EASTL/EABase 只属于迁移期现状，不是 vNext 目标依赖。

Legacy 当前大致依赖为 Core → Platform/Engine → ECS/Renderer/UI → Game；它不是 vNext 目标图。
新 target 的权威依赖关系见 [vNext 目标架构](vnext-architecture.md)，第三方类型不得继续向
不相关层扩散。

“当前实现”不再作为 vNext 的最终模块形态。完整目标和迁移策略见
[Tina vNext 目标架构](vnext-architecture.md)。

## 已知问题

- Application 仍承担过多初始化、主循环和服务访问职责；
- Scene 同时管理状态、相机、UI、渲染视图和事件订阅；
- GameScene 混合世界、ECS、输入、音频、UI 和渲染编排；
- Renderer 中存在多套未统一的命令与高层绘制入口。

## 旧架构删除状态

结论：旧文档已经替换，但旧源码架构没有完全删除。`TINA_BUILD_LEGACY` 与 vNext-only preset
已落地，能够把旧依赖和产品 target 排除出最小构建图。`EngineHost` 生命周期、M7-A Platform/Input
内核、私有 GLFW 窗口子切片、M7-B1 WindowSurface handoff、M7-B2 bgfx core、Desktop 真实 GPU 样例以及
startup primary-window UI capability、私有 bgfx SolidQuad UI pass 与最小 retained SolidFill 可见样例
已经可独立构建；它仍没有接入 Scene/Asset/Audio、完整文本/Widget UI、Pass Scheduler/submission ticket
或完整状态栈，因此 Legacy 仍是当前 2D/UI/3D 产品实现，不能直接整目录删除。

| 范围 | 状态 | 证据或影响 |
| --- | --- | --- |
| 旧阶段文档 | 已删除/替换 | `docs` 只保留当前架构、契约、验证和 Roadmap |
| Legacy 构建隔离 | 首批完成 | Null vNext preset 关闭 Legacy、GLFW、vNext bgfx backend 与 shader；platform preset 只额外启用私有 GLFW feature，仍不进入 bgfx/EASTL、不复制旧资源，也不建立 `Tina` target |
| 单体游戏 target | 仍在使用 | 主程序仍由一个 `Tina` executable 汇集 Engine、Game、Renderer、UI 和 ECS |
| Core compatibility | 仍在使用 | 主程序和测试仍链接 `Tina::CoreLegacy` |
| `Application` 组合根 | 仍在使用 | 继续持有 Window、Input、Event、Scene、Resource、Audio 和渲染服务 |
| Scene/GameScene 旧职责 | 仍在使用 | GameScene 仍混合玩法、ECS、输入、UI、音频与渲染编排 |
| EnTT 边界 | 尚未收敛 | `World` 暴露 `entt::registry`，GameScene 直接访问 |
| bgfx 边界 | 尚未收敛 | Renderer、UI 和部分公共结构仍直接暴露 bgfx handle/type |
| 路径资源系统 | 仍在使用 | M10-A0 已落地 Cooked wire schema、稳定 AssetId/ContentHash 值类型与只读校验；M10-A1 已落地 owning `CatalogSnapshot`、AssetId binary search 与完整 DAG cycle 校验；AssetSystem、Catalog 文件 IO、Handle/Lease 和独立 GPU upload queue 尚未落地 |
| vNext M6-A Runtime | 已完成生命周期切片 | `EngineHost`、`IGameApplication`、单个 `IGameState`、最小阶段 Context、Backend SPI、Headless Platform、Disabled TaskSystem 与 NullRenderDevice 已落地 |
| vNext M7-A Platform/Input | 已完成 Headless 内核与首个桌面 adapter 切片 | 固定容量 `PlatformFrameBuilder`、final Snapshot、保序 transition、Runtime-private `PlatformEventDispatcher`、Action Mapper，以及私有 GLFW Window/Keyboard/Pointer/committed text producer 已落地；IMM32、production Gamepad 与完整 DPI 门禁后置 |
| vNext M7-B1 WindowSurface handoff | 已完成私有 surface 所有权切片 | Platform/Render 通过 tagged composition 接线；`NativeWindowSurfaceLease` move-only 且 PIMPL；Win32/X11/Wayland native binding 只在私有 TU 解码；Render 创建失败和窗口发布失败会逆序释放 lease；NullRender 可验证 suspended/resume、surface revision 与 submission index |
| vNext M7-B2 Desktop + bgfx | 已完成私有 clear-only backend core、Desktop bootstrap 与真实 GPU smoke | `Tina::RenderBgfx` 只 PUBLIC 依赖 `Tina::Render`，bgfx 与 WindowSurface bridge 均为 PRIVATE；`Tina::Desktop::CreateEngine` 私有组合 SteadyClock、GLFW WindowSurface、DisabledTaskSystem 与 bgfx；planner 覆盖 1×1 bootstrap、resize/resume、content-scale-only 和 suspended skip；Windows Debug/Release 真实 D3D11 Intel Iris Xe 300帧通过 |
| vNext M7-C1b/C1c-a/C1c-b1/C1c-b2 UI tree/layout/hit query/route foundation | 已完成 standalone `tina_ui` 树、布局、committed hit、point query 与 synthetic route 数据/派发基础 | `Tina::UI` 只 PUBLIC 依赖 `Tina::Core` 与 `Tina::Platform`；在 generation `UINodeId`、`UIContext`、move-only `UIRootOwner`、结构 snapshot 与 route-result ABI 上，已实现 layout/dirty 类型、固定容量 PMR side array/queue/scratch、Flex-lite 非递归 Measure/Arrange、双缓冲 `UICommittedHitView`、structure/layout/hit 事务发布、无分配 `queryPointerHit()`、固定容量 route path/listener storage、48-byte fixed-inline `noexcept` listener callback、generation-safe RAII token 和 Capture→Target→Bubble synthetic dispatch；route 支持 stop/consume、路由中 add/reset/destroy 安全失效与 route/commit reentrancy guard。当前 changed frame 与 hit rebuild 仍全树扫描；持久 Pointer Capture、Focus/Modal、Button Keyboard/Gamepad activation、nested clip、bgfx UI pass 后置 |
| vNext SolidFill committed paint | 已实现最小 PaintCache 与双缓冲 snapshot | `UIBoxPaint` 当前只含可选 SolidFill；颜色以确定性整数规则转换为 premultiplied RGBA8，本地 cache 与双缓冲 `UICommittedPaintView` 使用 Create 期固定 PMR 容量。view 只发布 effective-visible、非透明 entry 的 world rect/effective clip/paint ordinal 与 revisions；失败保留旧的 structure/layout/hit/paint 四份 snapshot。它不等于 Image/Text/Glyph PaintCache、Widget 默认绘制或 nested clip |
| vNext backend-independent UI DisplayList | Render builder 与 UI→Render bridge 已实现 | `Tina::Render` 的单帧 builder 只接受纯 Tina SolidQuad/pixel clip 类型，不依赖 UI；`Tina::UIRenderIntegration` target 是唯一同时 PUBLIC 依赖 UI 与 Render 的窄桥，二者不反向依赖它。bridge 负责 logical→framebuffer outward rounding/clamp 和完整 builder transaction；Windows MSVC 19.50 Debug/Release、Linux GCC/Clang sanitizer 的12项直接 GoogleTest 均通过。Runtime packet、FramePin、Image/Text/Glyph 与 Texture 后置 |
| vNext M7-C1c-b3b Runtime→UI route-result producer | 已完成独立 Runtime-private 组件与测试 target | `UIInputRouteProducer` 只转换 raw Move/Button/Wheel，使用事件时 logical position，并把 consume 写入 raw ordinal bit；reset/cancel/非 Pointer 保留 hole；在该切片中 claims 当时恒为 canonical `None`。双预分配 PMR bitset 在300帧共用 PMR 测试中 allocation count 不增长，supplied PMR 必须长于 producer；失败测试先产生1次 listener side effect，后续 route path capacity 失败不发布但推进 attempted watermark，同帧 retry 被拒且 callback 仍为1。独立 `tina_runtime_ui_tests` 直接运行 GoogleTest、不使用 CTest |
| vNext M7-C1c-b3c EngineHost→UIContext 接线 | 已完成 Runtime-private primary-window owner 与正式帧路径接线 | `EngineHost` 在 Platform lifecycle dispatch 后惰性绑定首个 primary `WindowId`，随后调用 producer 并把结果交给 ActionMapper；Headless 绑定前为 null，同一 ID 的 metrics/content scale/minimized 变化复用 Context，绑定后 primary 消失或 generation 更换结构化失败。Context 在 module shutdown 前于 owner thread 销毁；owner 不调用 `commitLayout()`，route 只读上一帧 committed snapshot；该切片当时的 claims 仍为 canonical `None`。Game SDK 尚无 scoped Context/root 入口，所以此切片只闭合 Runtime 输入时序与所有权，不代表可见 UI |
| vNext M7-C1c-b3d1 UI 容量与布局提交 | 已完成公开配置与 Runtime-private phase coordinator | `UIContextCapacityConfig` 有独立公共头和共享 validator；`EngineConfig::primaryWindowUICapacities` 在任何 factory 前拒绝非法 node/root/derived/listener/paint snapshot 容量。正式帧在 `updateUI` 后、Render submit 前按 primary logical extent 至多尝试一次 `commitLayout()`；Headless 双缺席成功 no-op，identity/容量/layout/paint 失败阻断 Render 且该 frame attempt 不可重试。b3d1 当时只提交 layout/paint snapshot，D0 后续才构建 Render DisplayList |
| vNext M7-C1c-b3d2 UI 启动与 Game SDK capability | 已完成 startup seed 与 phase-scoped facade | Platform seed 不 poll、不消费 frame id；Runtime 在 `onEnter` 前绑定 primary Context 并提交首份 structure/layout/hit/paint snapshot。游戏只取得 `PrimaryWindowUIRootBuilder` 与绑定自己 root 的 `PrimaryWindowUITreeUpdater`，facade 以 owner thread 与 phase epoch 校验并在回调结束后失效；第一次 capability error 作为 sticky phase error 合并；不暴露裸 `UIContext*`。本行只记录 b3d2 当时事实，后续 paint/listener 扩展分别列在下方 |
| vNext M7-C1c-b3e held Pointer Button claim | 已完成 UI request、Runtime filter/publish 与 ActionMapper 门禁 | `claimPointerButton()` 与 transition consumption 分离，Move/Wheel/Button route 都可请求当前 Window/Pointer 的按钮；Runtime 只发布最终 snapshot 仍 held 的 primary Pointer Button，跨 route 去重并使用 Create 期 PMR 双 buffer。capacity 失败不发布 staging 且同帧不可重试；claim 会取消 active Gameplay source或拦截同帧 Down，并抑制到真实 Up。Key/Gamepad/axis claim、Capture/Focus/Modal 后置 |
| vNext Button default action | 已完成 primary Pointer 窄默认交互 | Button 创建后默认 `Targetable`，Root/Panel/Label 默认 `Ignore`；只支持 `PrimaryPointerId + PointerButton::Primary` 的 Down arm/pressed、Move inside 更新、Up-inside 一次 activation。`preventDefaultAction()` 独立于 stop/consume/claim；`setButtonAction()`/`clearButtonAction()`/`isButtonPressed()` 作为 root-scoped Game SDK facade 暴露，action 是 retained property。cancel/reset 与节点/root 销毁清理状态但不合成 Up、不触发 action；producer 在 ActionMapper 前合并 listener 与 default action 的 consumption/claim。Keyboard/Gamepad activation、Focus/Capture/Modal、Disabled/theme 视觉、Label 文本与完整 Widget 后置 |
| vNext D0 Runtime primary-window UI DisplayList handoff | 已完成 Runtime-private submit-call-local borrow | `PrimaryWindowUIDisplayCoordinator` 在 layout/paint commit 后、Render submit 前从 fixed PMR `UIDisplayListBuilder` 构建 primary-window list，并把 borrowed `primaryWindowUIDisplayList` 放进 `RenderFrame`。backend 不得在 `submitFrame()` 后保留；Headless、0 framebuffer 与 suspended 为空 list；失败不发布旧 list 或截断 list。owning RenderFramePacket/FramePin 后置 |
| vNext D1 bgfx SolidQuad UI pass | 已完成私有 backend 消费 Runtime DisplayList | `tina_render_bgfx` 私有创建 SolidQuad shader program，submit 前先预检 commands/batches、transient VB/IB 容量和 index32 能力；提交时使用 top-left ortho、scissor clip、premultiplied alpha 与 batch index range。bgfx/bx 类型不进入 Game SDK、Runtime 或普通 Render public header；Windows Debug/Release bgfx 专项16/16通过 |
| vNext D2 Game SDK box-paint visible sample | 已完成 root-scoped paint setter 与 Desktop 4-panel smoke | `PrimaryWindowUITreeUpdater::setBoxPaint()` 通过 phase epoch、owner thread、root/subtree 与 sticky error 门禁后委派到 `UI::UITreeUpdater::setBoxPaint()`；`tina_sample_desktop` 在 retained tree 中创建4个 SolidFill panel，并通过真实 D3D11 bgfx backend 可见提交。Windows Debug 1200帧、Release 300帧通过；Linux D1/D2 尚未重跑 |
| vNext Game SDK routed Pointer listener facade | 已完成 root-scoped 注册与 Runtime 端到端闭环 | `PrimaryWindowUITreeUpdater::addRoutedPointerListener()` 经 phase epoch、owner thread、root/subtree 校验后委派到 `UI::UITreeUpdater`；cross-root 失败 sticky 且不占 slot/high-water，callback move 重入释放 root 会事务回滚。token 可跨 phase 保存但不保活 Context/root，State 在 `onExit()` 先 reset token。EngineHost 证明 claim 在 ActionMapper 前生效；本行只代表低层 listener facade，不等于 Focus/Capture/Modal 或完整 Widget |
| vNext M8-A Scene World/Transform | 已完成最小基础切片 | `Tina::Scene`/`tina_scene` 提供固定容量 owner/generation `EntityId`、Local/World Transform、非递归层级传播、默认 keep-world/显式 keep-local reparent、父销毁提升、显式子树销毁、dense live index 与两阶段 publication；循环/跨 World/stale/非有限/溢出/shear/构造异常诊断；Windows MSVC Debug/Release `tina_scene_tests` 均19/19。当前不链接 EnTT/GLM，阶段 command buffer、Scene-owned Camera/Sprite component 与正式2D产品样例后置 |
| vNext M8-B RenderScene extraction foundation | 已完成后端无关基础切片 | `Tina::Render` 提供固定容量 `RenderSceneBuilder`/phase-local `RenderSceneWriter`，解析后的 Camera2D/Sprite2D input、稳定 layer/order/entity/insertion 排序、旋转保守裁剪、透明/隐藏剪枝、pixel snap、统计 checksum 与结构化容量/输入错误；Runtime 在 extraction 前后执行 begin/commit/rollback，并以 submit-call-local `RenderFrame::primaryWorldScene` 交给 backend；`tina_sample_2d_infrastructure` 以 Headless/Null 连续提取 300 帧。Scene component command buffer、Asset/Cooker、`FrameResourceRef`、正式可见产品路径、world picking 与正式 2D 产品样例仍后置；M9-C 只补私有 Sprite2D fixture，不改变本行的产品边界 |
| vNext M9-A RenderScene 3D extraction foundation | 已完成 CPU/Null 基础切片 | 在同一固定容量 builder 中增加 Perspective Camera、Mesh3D input/item/batch、右手 Y-up `-Z` forward、当前 PlatformFrame framebuffer aspect（`0x0` 回退 logical）、正 scale 世界包围球、球体 frustum culling、稳定 material/mesh/submesh/double-sided/depth/entity/insertion 排序与相邻 instance batch finalize；`tina_render_scene_tests` 22/22，`tina_sample_3d_extraction` 300 帧记录4 submitted/3 visible/1 culled/2 batches、aspect变化与资源归零。该样例不显示 GPU 画面；私有 bgfx Cube/depth 属 M9-B，Cooked glTF 属 M10 |
| vNext M9-B bgfx Opaque3D fixture | 已完成最小可见3D fixture | `tina_render_bgfx` 私有消费 M9-A Mesh3D view 的 fixture 子集，只接受 `meshKey=1/materialKey=1/submeshIndex=0`；canonical `P3_N3_UV2` Cube、Unlit shader、静态 VB/IB 与 transient instance buffer 形成 `tina_sample_3d_infrastructure`。它不证明 Cooked Mesh/Material/Texture/Prefab、glTF、通用 Pipeline/PBR、Pass Scheduler 或正式3D产品 |
| vNext M9-C bgfx Sprite2D fixture + 2D/UI sample | 已完成 Debug/Release fixture/infrastructure 验证 | `tina_render_bgfx` 私有消费 Sprite2D fixture 子集，只接受 `spriteKey=1`，写入 transient P2/UV2/ABGR vertex 与 u32 index，并与 Opaque3D/UI 做联合 transient budget 预检；当前固定 View 0 clear、1 Opaque3D、2 Sprite2D、3 UI 只是 fixture view 编号，不是 Pass Scheduler。Windows Debug/Release `tina_render_bgfx_tests` 均43/43；两配置的 `tina_sample_2d_infrastructure_bgfx` 均运行300帧，记录5个 Sprite、2个 UI panel、`renderResourceLedgerBalanced=true`，Debug 截图确认旋转、透明、flip 与 UI overlay。它不证明 Asset/Texture/Sprite 产品路径、正式 `tina_sample_2d`、TileMap、Box2D、中文文本或 M10-A1+ |
| vNext M10-A0 Cooked wire format | 已完成只读格式基础切片 | `Tina::AssetFormat` 只 PUBLIC 依赖 Core；新增互不兼容的16字节 `AssetId`/`ContentHash`、固定112B Cooked Header、64B Manifest Header、56B Manifest Entry、24B Dependency Entry、确定性 object path，以及成功路径零分配的 little-endian borrowed views。parser 在暴露数据前校验 hard limit、checked arithmetic、canonical layout、zero padding、排序/重复、依赖存在与 kind；A0 不计算 XXH3、不做完整 cycle、文件 IO、AssetHandle/Lease、worker/upload、cgltf/Cooker writer 或正式资产产品样例 |
| vNext M10-A1 CatalogSnapshot | 已完成 owning Catalog 基础切片 | `Tina::Asset`/`tina_asset` PUBLIC 依赖 Core + AssetFormat；`CatalogSnapshot::Create` 把已解析 Manifest 事务式转换为注入 PMR 上的不可变 owning 快照，Create 后不依赖 Manifest bytes；`find(AssetId)` 对严格升序 entry 做 binary search；依赖目标在 Create 时解析为稳定 entry index；完整 DAG cycle 使用迭代着色、显式 stack，复杂度 `O(V + E)`，禁止递归。失败不发布部分 Snapshot 并回滚 PMR。独立 `tina_asset_tests` 17/17。A1 不实现 Handle/Lease、registry 状态机、文件 IO、Task、GPU upload、XXH3、cgltf/Cooker 或正式资产产品样例；ADR 0016 仍为 Proposed |
| vNext GLFW 边界 | 已形成可运行切片 | `Tina::PlatformGlfw` 只 PUBLIC 依赖 Tina Platform，GLFW 为 PRIVATE；公共 factory header 不出现 GLFW/native 类型，Null 构建闭包仍不链接 GLFW |
| 完整 vNext Runtime | 尚未完成 | GameStateStack/commands、worker、Pass Scheduler/RenderFramePacket、Scene component-integrated extraction、AssetSystem/Audio、owning UI packet/pin、Text/Glyph/完整 Widget 与 submission drain 仍按后续切片实施；M8-A World/Transform、M8-B 2D extraction、M9-A 3D CPU/Null extraction、M9-B Opaque3D fixture、M9-C Sprite2D fixture、M10-A0 wire format、M10-A1 CatalogSnapshot、Desktop SolidFill 可见样例、standalone UI/Render/bridge foundation、Runtime-private route/layout/startup/display borrow 接线、Button primary-pointer default action 和私有 bgfx SolidQuad pass 不代表完整产品路径完成 |

因此不能用“删除旧 `src`”作为下一步。正确顺序是：建立新边界和测试 → 迁移调用点 → 确认旧接口零引用 → 通过 2D/UI/3D 验收 → 在独立提交中删除旧实现。

旧模块只有同时满足以下条件才能删除：

1. 已有明确替代模块和所有权关系；
2. 生产代码与测试不再 include、链接或调用旧接口；
3. Windows/Linux 构建和直接 GoogleTest 通过；
4. 2D、UI、3D 冒烟和资源回收门禁通过；
5. 删除操作不夹带新功能，能够独立回滚。

## 目标契约

- 不新增全局 Singleton 或 Service Locator；
- 以 `EngineHost` 作为唯一非全局组合根；普通游戏调用纯 Tina API 的 desktop bootstrap，
  高级测试才显式注入 factories；
- `IGameApplication` 只负责程序启动/停止，`IGameState` 是唯一帧行为入口；
- 初始化必须显式返回错误，失败后按逆序释放已创建资源；
- `tina_core`、`tina_platform`、`tina_platform_glfw`、`tina_task`、`tina_runtime`、`tina_scene`、
  `tina_asset_format`、`tina_asset`、`tina_render`、`tina_render_bgfx`、`tina_ui`、
  `tina_ui_freetype`、`tina_audio`、`tina_audio_miniaudio`、`tina_profile_tracy` 与 `tina_assetc`
  形成单向依赖；
- vNext target 不依赖 EASTL；标准库/`std::pmr` 承担通用容器，Tina 只提供少量经过测试的
  固定容量和 generation 专用结构；xxHash 只藏在 Hash adapter 后；
- 2D/3D 共享右手 Y-up 世界，2D 位于 XY 平面；
- Game SDK 和 Phase Context 不暴露 RenderDevice/native handle；Tina module public header 可以暴露
  纯 Tina Render SPI，但任何公共头都不暴露 bgfx/native 类型；bgfx 只出现在
  `tina_render_bgfx` 私有实现和离线 shader 工具；
- 自研 UI 只输出后端无关 DisplayList，以细粒度 dirty、PaintCache 和 committed snapshot
  实现无变化 UI 的0布局/0 PaintCache rebuild/0 Tina heap allocation；
- EnTT 只作为 Scene 内部存储，模块接口只暴露 generation `EntityId`；
- MemorySystem 由 EngineHost 拥有，通用持久内存按模块 tag 统计，帧临时数据按 phase Arena
  分配；Task 只通过有界 CPU/IO/Main executor 和显式 barrier 跨线程；
- 完整目标可以不兼容旧 API，但实施按可运行垂直切片推进，不进行不可验证的大爆炸提交。
