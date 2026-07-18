# 架构总览

## 当前双轨实现

Tina 当前处于 Legacy 产品与 vNext 垂直切片并存的迁移期。Legacy 仍以单个游戏可执行文件为主，
源码按 Core、Engine、Renderer、UI、ECS 和 Game 组织；vNext 已建立独立的 `tina_core`、
`tina_platform`、`tina_task`、`tina_render`、`tina_runtime` 五个基础 C++23 target，以及可选的
`tina_platform_glfw` adapter、私有 `tina_render_bgfx` backend、Desktop bootstrap 和
M7-C1b/M7-C1c-a/C1c-b1/C1c-b2 `tina_ui` tree/layout/committed-hit/point-query/synthetic-route foundation，
以及 M7-C1c-b3b/b3c Runtime-private UI route-result producer、primary-window `UIContext` owner 与
`EngineHost` 接线、M7-C1c-b3d1 的公开容量配置和 Runtime-private layout coordinator、
M7-C1c-b3d2 的 startup UI seed 与 Game SDK scoped capability，以及 M7-C1c-b3e 的 held primary
Pointer Button claim bridge。
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
`UIContext`，并在 State commit 前发布首份 structure/layout/hit snapshot。Game SDK 只取得
`PrimaryWindowUIRootBuilder` 与绑定自己 root 的 `PrimaryWindowUITreeUpdater`；facade 以 owner thread
和 phase epoch 校验，回调结束后无条件失效，第一次 capability operation 失败作为 sticky phase error
回传 Runtime。M7-C1c-b3e 已允许 routed Pointer listener 请求接管仍 held 的 primary Pointer Button，
Runtime 按最终 snapshot 过滤并去重，ActionMapper 取消或拦截 Gameplay source 并抑制到真实 Up；没有
DisplayList、文本/glyph、Button default action 或真实 Widget 渲染，因此这条接线还不是可见 UI。
Key/Gamepad/axis claim、持久 Pointer Capture、Focus/Modal、dirty subtree pruning、
nested clip 和 DisplayList 仍未完成。
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
startup primary-window UI capability 已经可独立构建；它仍没有接入 Scene/Asset/Audio、可见 UI pipeline、
Pass Scheduler/submission ticket 或完整状态栈，因此 Legacy 仍是当前 2D/UI/3D 产品实现，不能直接整目录删除。

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
| 路径资源系统 | 仍在使用 | Cooked Asset、稳定 AssetId 和独立 GPU upload queue 尚未落地 |
| vNext M6-A Runtime | 已完成生命周期切片 | `EngineHost`、`IGameApplication`、单个 `IGameState`、最小阶段 Context、Backend SPI、Headless Platform、Disabled TaskSystem 与 NullRenderDevice 已落地 |
| vNext M7-A Platform/Input | 已完成 Headless 内核与首个桌面 adapter 切片 | 固定容量 `PlatformFrameBuilder`、final Snapshot、保序 transition、Runtime-private `PlatformEventDispatcher`、Action Mapper，以及私有 GLFW Window/Keyboard/Pointer/committed text producer 已落地；IMM32、production Gamepad 与完整 DPI 门禁后置 |
| vNext M7-B1 WindowSurface handoff | 已完成私有 surface 所有权切片 | Platform/Render 通过 tagged composition 接线；`NativeWindowSurfaceLease` move-only 且 PIMPL；Win32/X11/Wayland native binding 只在私有 TU 解码；Render 创建失败和窗口发布失败会逆序释放 lease；NullRender 可验证 suspended/resume、surface revision 与 submission index |
| vNext M7-B2 Desktop + bgfx | 已完成私有 clear-only backend core、Desktop bootstrap 与真实 GPU smoke | `Tina::RenderBgfx` 只 PUBLIC 依赖 `Tina::Render`，bgfx 与 WindowSurface bridge 均为 PRIVATE；`Tina::Desktop::CreateEngine` 私有组合 SteadyClock、GLFW WindowSurface、DisabledTaskSystem 与 bgfx；planner 覆盖 1×1 bootstrap、resize/resume、content-scale-only 和 suspended skip；Windows Debug/Release 真实 D3D11 Intel Iris Xe 300帧通过 |
| vNext M7-C1b/C1c-a/C1c-b1/C1c-b2 UI tree/layout/hit query/route foundation | 已完成 standalone `tina_ui` 树、布局、committed hit、point query 与 synthetic route 数据/派发基础 | `Tina::UI` 只 PUBLIC 依赖 `Tina::Core` 与 `Tina::Platform`；在 generation `UINodeId`、`UIContext`、move-only `UIRootOwner`、结构 snapshot 与 route-result ABI 上，已实现 layout/dirty 类型、固定容量 PMR side array/queue/scratch、Flex-lite 非递归 Measure/Arrange、双缓冲 `UICommittedHitView`、structure/layout/hit 事务发布、无分配 `queryPointerHit()`、固定容量 route path/listener storage、48-byte fixed-inline `noexcept` listener callback、generation-safe RAII token 和 Capture→Target→Bubble synthetic dispatch；route 支持 stop/consume、路由中 add/reset/destroy 安全失效与 route/commit reentrancy guard。当前 changed frame 与 hit rebuild 仍全树扫描；持久 Pointer Capture、Focus/Modal、Button default action、paint snapshot/DisplayList、nested clip、bgfx UI pass 后置 |
| vNext M7-C1c-b3b Runtime→UI route-result producer | 已完成独立 Runtime-private 组件与测试 target | `UIInputRouteProducer` 只转换 raw Move/Button/Wheel，使用事件时 logical position，并把 consume 写入 raw ordinal bit；reset/cancel/非 Pointer 保留 hole；在该切片中 claims 当时恒为 canonical `None`。双预分配 PMR bitset 在300帧共用 PMR 测试中 allocation count 不增长，supplied PMR 必须长于 producer；失败测试先产生1次 listener side effect，后续 route path capacity 失败不发布但推进 attempted watermark，同帧 retry 被拒且 callback 仍为1。独立 `tina_runtime_ui_tests` 直接运行 GoogleTest、不使用 CTest |
| vNext M7-C1c-b3c EngineHost→UIContext 接线 | 已完成 Runtime-private primary-window owner 与正式帧路径接线 | `EngineHost` 在 Platform lifecycle dispatch 后惰性绑定首个 primary `WindowId`，随后调用 producer 并把结果交给 ActionMapper；Headless 绑定前为 null，同一 ID 的 metrics/content scale/minimized 变化复用 Context，绑定后 primary 消失或 generation 更换结构化失败。Context 在 module shutdown 前于 owner thread 销毁；owner 不调用 `commitLayout()`，route 只读上一帧 committed snapshot；该切片当时的 claims 仍为 canonical `None`。Game SDK 尚无 scoped Context/root 入口，所以此切片只闭合 Runtime 输入时序与所有权，不代表可见 UI |
| vNext M7-C1c-b3d1 UI 容量与布局提交 | 已完成公开配置与 Runtime-private phase coordinator | `UIContextCapacityConfig` 有独立公共头和共享 validator；`EngineConfig::primaryWindowUICapacities` 在任何 factory 前拒绝非法 node/root/derived/listener 容量。正式帧在 `updateUI` 后、Render submit 前按 primary logical extent 至多尝试一次 `commitLayout()`；Headless 双缺席成功 no-op，identity/容量/layout 失败阻断 Render 且该 frame attempt 不可重试。DisplayList 与可见 UI 仍未实现 |
| vNext M7-C1c-b3d2 UI 启动与 Game SDK capability | 已完成 startup seed 与 phase-scoped facade | Platform seed 不 poll、不消费 frame id；Runtime 在 `onEnter` 前绑定 primary Context 并提交首份 structure/layout/hit snapshot。游戏只取得 `PrimaryWindowUIRootBuilder` 与绑定自己 root 的 `PrimaryWindowUITreeUpdater`，facade 以 owner thread 与 phase epoch 校验并在回调结束后失效；第一次 capability error 作为 sticky phase error 合并；不暴露裸 `UIContext*`。该切片仍无 DisplayList、文本/glyph、Button 默认行为；真实 claim producer 当时尚未加入 |
| vNext M7-C1c-b3e held Pointer Button claim | 已完成 UI request、Runtime filter/publish 与 ActionMapper 门禁 | `claimPointerButton()` 与 transition consumption 分离，Move/Wheel/Button route 都可请求当前 Window/Pointer 的按钮；Runtime 只发布最终 snapshot 仍 held 的 primary Pointer Button，跨 route 去重并使用 Create 期 PMR 双 buffer。capacity 失败不发布 staging 且同帧不可重试；claim 会取消 active Gameplay source或拦截同帧 Down，并抑制到真实 Up。Key/Gamepad/axis claim、Capture/Focus/Modal 后置 |
| vNext GLFW 边界 | 已形成可运行切片 | `Tina::PlatformGlfw` 只 PUBLIC 依赖 Tina Platform，GLFW 为 PRIVATE；公共 factory header 不出现 GLFW/native 类型，Null 构建闭包仍不链接 GLFW |
| 完整 vNext Runtime | 尚未完成 | GameStateStack/commands、worker、Pass Scheduler/RenderFramePacket、Scene/Asset/Audio、可见 UI pipeline 与 submission drain 仍按后续切片实施；Desktop clear-only GPU 冒烟、standalone `tina_ui` foundation 和 Runtime-private route/layout/startup capability 接线不代表这些路径完成 |

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
