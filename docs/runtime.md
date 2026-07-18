# Runtime 与 Frame Pipeline

## 当前实现：Legacy 产品路径

Application 已集成 GLFW Window/Input、EventSystem、资源管理器、miniaudio、SceneManager 和 bgfx。Core 提供 Clock 与 FrameTimer。

当前每帧的实际主线程顺序为：Poll Platform/Input → Event Queue → Asset Completion → App Event → Fixed Simulation → Variable/Scene Update → UI Node Update → UI Batch Layout → UI Hit-test/Routed Event → Scene Render → App Render → Input End Frame → Deferred Task → Present。Event Queue、Asset Completion 和 UI Pointer Routing 都只有一个明确泵送点；资源 completion 默认每帧最多提交8个。

Fixed Simulation 默认 60 Hz、每个 Render Frame 最多追赶4步；超出保护阈值的积压会被丢弃，Render 可通过 `interpolationAlpha()` 读取 accumulator 插值比例。GameScene 的 ECS、碰撞、水模拟、粒子与昼夜推进已进入 fixed phase，相机和 UI 保持可变帧率。

Application 初始化现在保留明确的成功状态；任一必需子系统失败会按逆序回滚，`run()` 会拒绝半初始化实例。`--smoke-frames=N` 可在提交 N 帧后沿正常生命周期退出，用于验证析构和资源回收。

## 已知问题

- Scene 操作和资源上传/销毁还没有完全归入独立 Frame Phase；
- Legacy Application 仍承担过多系统所有权，尚未迁移到 vNext EngineHost/factory/阶段 Context。

## vNext Runtime 内核与首个 GLFW Platform 切片

M6-A 已把 C++23 Core 基础接入可独立运行的 Headless Runtime，M7-A 又接入 Platform/Input，并以
独立提交加入私有 GLFW Window/Keyboard/Pointer/committed text producer；它们与 Legacy Application
并存，不改变现有产品路径：

- `Result<T>/Status` 使用 `std::expected`，Error 已包含稳定 domain/code、origin、native code
  和 UTF-8 context chain；
- `IMonotonicClock`/`SteadyMonotonicClock` 可由 Runtime 构造注入，测试使用 Manual Clock；
- `FixedStepAccumulator` 已明确真实 delta 钳制、gameplay time scale、variable `updateDelta`、
  固定步计划、最多4步、超额整步丢弃与 interpolation；
- vNext 公共头位于 `include/tina`，Legacy 时间类型只留在 `src/core` 兼容层；
- `EngineHost::Create(config, factories)` 已按 Clock → Platform → Task → Render 创建模块；WindowSurface
  分支在 Task 后获取 primary window lease/snapshot，Render 成功后才发布窗口。任一步失败都逆序
  回滚；M7-C1c-b3c 绑定 Context 后的销毁顺序为 UIContext → Render → Task → Platform → Clock；Create/run 均为
  `noexcept` 边界，普通异常转换为结构化 Error，硬 OOM 若连 Error 都无法构造则 fatal；
- `EngineHost::Create`、`run` 与销毁组成同一个 owner-thread 生命周期。跨线程 `run` 返回
  `WrongOwnerThread` 且不消耗 run-once；跨线程销毁会在调用任何 native API 前终止进程，不能
  通过错误线程冒险释放桌面窗口资源；
- `EngineConfig` 在任何 factory 前完成校验，并硬拒绝 `maximumStepsPerFrame > 4`；M7-C1c-b3d1
  新增 `primaryWindowUICapacities`，复用 `Tina::UI::validateUIContextCapacityConfig()` 拒绝非法
  node/root、派生 scratch/snapshot 和 listener 容量；
- `IPlatformBackend::initialPrimaryWindowMetrics()` 已为 startup transaction 提供 backend-neutral
  primary-window metrics seed：它不能 poll、泵送事件、消费 frame id/source sequence；Headless 返回
  `nullopt`，GLFW 在 owner thread 从已创建窗口读取原生 metrics 并保留后续首帧 metrics event；
- `EngineConfig::platformFrameCapacities`、`inputActions` 与 `platformEventSubscriptions` 已按职责
  分离；Platform raw/event/text storage 在 backend 创建时一次性分配，Poll 期间不扩容；
- `PlatformFrameBuilder` 校验 UTF-8、final Snapshot、全局单调 sequence、Gamepad lifecycle 以及
  最后一个未被 cancel/reset 覆盖的 digital edge；edge 与 final held 不一致会在任何外部回调前失败；
- Runtime-private `PlatformEventDispatcher` 提供 activation-order 稳定、generation-safe 的 RAII
  订阅；游戏 Context 只能取得 `PlatformEventSubscriptions` 注册门面；
- Action Mapper 已区分 Simulation/Frame domain；0 fixed-step 帧保留 Simulation edge，追赶步只在
  第一个目标 tick 消费一次，`FixedUpdateContext`/`FrameUpdateContext` 分别只读对应 Snapshot；
- `IGameApplication`、单个已提交 `IGameState` 与最小阶段 Context 已落地；创建 initial State 或
  `onEnter` 失败不调用 candidate `onExit`/Application `onShutdown`，提交后退出或失败则各调用一次；
- Headless Platform、Disabled TaskSystem、生命周期级 NullRenderDevice 已分别位于
  `tina_platform`、`tina_task`、`tina_render`，`tina_runtime` 只依赖 Tina SPI；
- 可选 `tina_platform_glfw` 通过无第三方类型的 factory SPI 接入 Runtime，GLFW 只在 adapter
  实现层可见；M7-B1 的 WindowSurface 组合已接入 `IWindowSurfacePlatformBackend`、
  `acquirePrimaryWindowSurfaceLease()`、`primaryWindowSurfaceSnapshot()` 与
  `publishPrimaryWindow()`；`tina_sample_platform` 仍显式注入 GLFW + DisabledTask + NullRender，
  M7-B2 已实现面向普通游戏的 `Desktop::CreateEngine` clear-only Desktop bootstrap；
- M7-C1b/C1c-a/C1c-b1/C1c-b2 已在独立 `tina_ui` 树核心上实现 generation `UINodeId`、`UIContext`、`UIRootOwner`
  RAII、结构/布局 snapshot、UI-owned input route-result view ABI、事务式 Flex-lite layout，以及固定容量
  Pointer policy/route-ancestry scratch、双缓冲 `UICommittedHitView`、无分配 `queryPointerHit()`、固定容量
  listener storage、generation-safe RAII listener token、48-byte fixed-inline `noexcept` callback 与 synthetic
  Capture→Target→Bubble route；M7-C1c-b3b 已实现 Runtime-private `UIInputRouteProducer`，M7-C1c-b3c 已把
  primary-window `UIContext` owner/selection 与 producer 接入正式帧循环；持久 Pointer Capture、Focus/Modal
  或 Button default action 仍未实现；
- M7-C1c-b3c 的 owner 在首次看到 primary `WindowId` 时惰性创建唯一 `UIContext`；Headless 绑定前传 null，
  绑定后 primary 消失或 generation 更换返回结构化失败，同一 ID 的 metrics/content scale/minimized 变化不重绑。
  Context 在 Render → Task → Platform → Clock module shutdown 前于 owner thread 销毁；owner 本身不调用
  `commitLayout()`，route 只读上一帧 committed snapshot；
- M7-C1c-b3d1 的 Runtime-private `PrimaryWindowUILayoutCoordinator` 在 `IGameState::updateUI()` 成功后、
  Render submit 前使用主窗口 logical extent 提交本帧下一份 layout/hit snapshot。每个有效且严格递增的
  `PlatformFrameId` 至多尝试一次；Headless 窗口/Context 双缺席成功 no-op，identity 或 commit 失败阻断
  Render，并保持本帧 attempt 已消费，禁止同帧重放；
- M7-C1c-b3d2 在 `createInitialState()` 后读取 `initialPrimaryWindowMetrics()`，并在 `onEnter` 前显式
  bind primary `UIContext` 或显式进入 Headless unavailable 状态。`GameStateEnterContext` 提供
  `PrimaryWindowUIRootBuilder`，`UIUpdateContext` 提供绑定 `UIRootOwner` 的
  `PrimaryWindowUITreeUpdater`；两者都是 move-only、owner-thread、phase-epoch-scoped facade，回调结束后
  无条件失效。第一次 capability operation 失败会成为该 phase 的 sticky error，并在 callback 返回后由
  Runtime 与 callback status 合并；
- M7-C1c-b3e 允许 routed Pointer listener 通过 `claimPointerButton()` 请求接管当前 route Window/Pointer
  上仍 held 的 primary Pointer Button。Runtime 按最终 snapshot 过滤、跨 route 去重并发布 bounded claim；
  ActionMapper 取消已 active 的 Gameplay source，或直接拦截同帧尚未消费的 ButtonDown，并抑制到真实 Up；
- 当前帧循环为 Poll Platform → frame/payload/capacity/sequence 预校验 → Platform lifecycle dispatch
  → primary UIContext selection + UI Input Routing → Action Mapping → Fixed Update（0..4）→ Frame Update
  → Render Scene Extraction → UI Update → primary UI Layout Commit → Null submit → present；`requestExitAfterFrame()` 会完成当帧
  submit/present 后退出；
- `tina_sample_null --frames=N` 已在 MSVC 2026 Debug/Release 连续通过300帧与10,000帧，且
  vNext Null 构建图不加入或链接 GLFW、bgfx、EnTT、FreeType、miniaudio、SDL/SDL3。

当前 Null 与 GLFW+Null 两条路径证明生命周期、回滚、固定步、Platform/Input、真实窗口与空提交契约。
GLFW close 在 Poll 中只返回不可取消的 tagged outcome，并丢弃尚未提交的 partial frame；Runtime 在
任何 Game callback 前停止。Escape 则经 Frame Action 调用 `requestExitAfterFrame()`，完成当帧
Null submit/present 后退出。M7-B1 已有 backend-neutral Native Surface handoff、初始/逐帧
`RenderSurfaceState` 和 Suspended 帧 `SkippedSuspendedSurface` 结果；Runtime 固定 source window identity，
拒绝 metrics revision 回退、surface facts 在旧 metrics revision 上变化，以及 surface revision 跳号/回退。
当前实现仍没有完整 GameStateStack/commands、CPU/IO worker、通用 Runtime Event Queue、
Pass Scheduler/RenderFramePacket，也没有 Scene、Asset 或 Audio。Render 只完成 clear-only bgfx
Desktop smoke；UI 只完成 standalone `tina_ui` 树核心、route-result view ABI、事务式 Flex-lite layout、
committed hit-snapshot 数据基础、point query/反向目标选择、synthetic Capture→Target→Bubble route，以及
Runtime-private producer/primary Context 接线、Runtime-private 每帧 layout commit，以及 startup
primary-window seed + root-scoped phase capability、held primary Pointer Button claim bridge。
Panel/Label/Button 只是在 retained tree 中可创建的节点类型；当前没有可见 UI。Key/Gamepad/axis claim、
持久 Pointer Capture、Focus/Modal、Button
default action、paint snapshot/DisplayList、nested clip、dirty subtree pruning、text/glyph、FreeType 与 bgfx UI pass 仍未实现。这些能力不能从同名 Phase Context、真实 GLFW 窗口或 clear-only
Desktop smoke 推断为已经实现。

## 完整 vNext 所有权目标

vNext 以 `EngineHost::Create(const EngineConfig&, EngineCompositionFactories)` 建立唯一组合根；普通游戏通过
`Desktop::CreateEngine(config)` 使用隐藏具体 backend 的默认组合。成功创建后 EngineHost 接管
所有权，Runtime 本身不依赖具体 backend。`IGameApplication` 只创建初始 `IGameState` 和接收
最终 shutdown；逐帧 Fixed Update/Frame Update/Render Scene Extraction/UI Update 只由
`IGameState` 通过短生命周期
Context 执行。禁止保存 Context、writer、span 或模块 owner；禁止 `Application::instance()`、
Singleton 和 Service Locator。

初始化每成功一步立即登记对应逆操作。创建失败必须返回完整上下文错误并逆序回滚，不能
留下可调用 `run()` 的半初始化对象。Engine 状态为 Ready → Starting → Running → Stopping →
Stopped/Failed，`run()` 只允许一次。`createInitialState + initial onEnter + initial UI snapshot`
同属启动事务；成功后 `IGameApplication::onShutdown` 恰好一次，失败自动回滚且不调用
candidate `onExit` 或 Application `onShutdown`。Frame 回调返回 Status，
异常在 `IGameApplication`/`IGameState`/Task/C callback 边界转换。完整接口和迁移切片见
[vNext 目标架构](vnext-architecture.md)。

完整目标的 module 创建顺序为：Diagnostics → MemorySystem → Headless/GLFW Platform → TaskSystem →
RenderDevice → AssetSystem → AudioEngine。此时只创建 primary-window UI owner，不绑定 `UIContext`；
`run()` 的 startup transaction 在 `createInitialState()` 后读取 metrics seed，在 `onEnter()` 前绑定 Context。
统一关闭顺序为：

1. 按栈顶到栈底把 `IGameState` 从 phase/UI eligibility 移除，关闭 ingress，清理 Focus/Capture/
   Modal 并 signal State TaskGroup cancellation；
2. barrier/join TaskGroup 并丢弃迟到 completion，再调用一次 State `onExit`，最后析构 State，
   让其 root/lease/订阅/World RAII owner 幂等释放并验证无残留；
3. 调用一次 `IGameApplication::onShutdown`，撤销游戏级注册，不再创建 Engine 工作；
4. Asset 停止新请求、取消未提交工作并完成 IO/CPU barrier，暂时保留 Audio lease；
5. Audio 停止 callback/stream producer、drain completion 并释放所有 lease；
6. Asset 丢弃迟到 completion，drain upload/destroy ticket 并释放 CPU/GPU payload；
7. Render 完成 deferred retire、关闭 device/surface；Audio backend 完成最终销毁；
8. TaskSystem 停止接收并 join；Platform 销毁窗口和 backend；
9. 检查所有 MemoryTag 后销毁 MemorySystem，最后关闭 Diagnostics。

barrier 超时绝不能 reset Arena 或继续普通析构。Engine 转 fatal-stop、请求协作取消并等待
join；超过硬 shutdown deadline 仍有线程访问 Engine 内存时记录 CrashContext 后 fast-fail，
不能冒险释放对象制造 UAF。详细 barrier 见 [Task System](task-system.md)，内存归属见
[性能与内存](performance-memory.md)。

## Event 通道契约

Platform 生命周期通知、未来的 Runtime 通用短事件、UI routed event 与 fixed gameplay command
是四种不同语义，不能为了复用一个 EventBus 合并。

### M7-A 已实现：Platform 生命周期分发器

Runtime-private `PlatformEventDispatcher` 只分发当前 `PlatformFrameView` 中已经归一化并通过容量、reset shape 与全局
sequence 校验的 Window metrics 和 Gamepad connection 生命周期通知。它是主线程、同步、借用当前帧
final registry snapshot 的窄分发器，不是后台 ingress queue，也不承载 Asset/App 自定义事件。

- 游戏只从启动/状态进入 Context 取得 `PlatformEventSubscriptions` 注册门面，不能取得 dispatcher owner、
  调用 dispatch/shutdown 或移动 dispatcher；
- 订阅返回 generation-safe RAII `PlatformEventSubscription`；dispatcher 先销毁、回调中退订、订阅者或
  dispatcher 自销毁都安全；
- dispatch 中新增订阅从同 batch 的下一事件生效，取消立即阻止尚未开始的回调；slot 复用仍按
  activation 顺序稳定分发；
- 同一 dispatcher 的递归 dispatch 明确返回 `RecursivePlatformEventDispatch`，不排队、不递归调用；
- callback 异常转为包含 event sequence 的结构化失败；外部 callback 前必须完成 frame id、容量、
  reset shape、UTF-8 text budget、snapshot/payload 与全局 source sequence 校验；
- `PlatformEventStreamReset` 的 callback view 可枚举 final Window/Gamepad state，用于容量溢出或 backend
  recovery 后重建观察者缓存；raw input 永不从该通知通道暴露；
- 首期主窗口关闭不可取消，只作为 `PlatformPollResult::ExitRequested` tagged 分支返回；Runtime 在任何
  新 frame/Event/Input phase 前停止，不创建 `PlatformFrameView`，也不入队同义 `CloseRequested`。

Platform handler 不直接 push/pop `IGameState`、销毁 World 或等待 Task；它只写当前 State intent，
由随后的 `updateFrame()` 转为延迟状态命令。

### 后续目标：Runtime 通用短事件

Asset completion 与 App 自定义短事件将在对应模块落地后使用另一条 owning、有界通道：后台线程只写
ingress，主线程在唯一 Event Phase 按 sequence 合并；跨帧 payload 不借用 Worker/Arena。届时才引入
Control/Normal/Background priority、各类预留容量及显式 droppable telemetry/refresh policy。递归
dispatch 的具体排队语义必须在该通道自己的 ADR 中冻结，不能套用到当前同步
`PlatformEventDispatcher`。

M7-C1c-b3c 已让首个 primary Window 的 Runtime-private `UIContext` 在 Platform lifecycle dispatch 后路由
Pointer transition，并把 producer 结果交给 ActionMapper；Headless 绑定前没有 Context。当前 route 读取上一帧
committed snapshot，不隐式 layout。M7-C1c-b3d1 只在后续 `updateUI` phase 成功后由 coordinator 提交
本帧下一份 snapshot；因此输入路由与 layout 发布仍是两个明确提交点。M7-C1c-b3e 已实现第一条真实
continuous-control producer：Move/Wheel/Button route 都可请求接管最终仍 held 的 primary Pointer Button，
且 consumption 与 claim 分开发布。Key/Gamepad/axis claim、Focus/Capture/Modal 仍后置。Gameplay Action
带目标 simulation tick，由 fixed loop 消费。

## IGameApplication 与 IGameState 调用顺序

当前仍只有一个 State，实际顺序为：create initial State → read primary-window metrics seed →
bind primary UIContext 或 Headless unavailable → `onEnter`（可使用 root capability）→ sample policy →
startup UI layout/hit snapshot → frame loop → `onExit` → Application `onShutdown` → module shutdown。
M7-C1c-b3d2 已实现 backend-neutral startup primary-window metrics seed 和 root-scoped、phase-epoch-scoped
capability；尚未实现的是完整 GameStateStack、TaskGroup barrier 或状态命令提交。

完整目标顺序为：

```text
EngineHost::run(gameApplication)
  -> begin startup transaction
  -> gameApplication.createInitialState()
  -> platform.initialPrimaryWindowMetrics()
  -> bind primary-window UIContext, or explicitly bind Headless
  -> initialState.onEnter(primary-window root capability)
  -> sample initialPolicy + initial UI layout/snapshot
  -> commit GameStateStack
  -> frame loop
  -> stop accepting state commands
  -> remove eligibility / close ingress / requestStop
  -> TaskGroup barrier/join
  -> state.onExit() top-to-bottom
  -> gameApplication.onShutdown() exactly once
  -> module shutdown
```

`IGameApplication` 不属于帧分发集合；它没有 Fixed/Update/Render/UI 回调。`IGameState` 是唯一
帧客户端，Menu、Settings、Game2D、Game3D、Pause 都使用同一接口。完整命名、示例和 policy
见[游戏程序入口与状态栈](gameplay.md)。

## 完整目标 Frame 契约

帧阶段固定为：Poll Platform → `PlatformFrameView` Finalize（Snapshot + ordered transitions）→
Platform lifecycle dispatch（当前 `PlatformEventDispatcher`）→ Runtime Event Queue（未来 Gameplay/
Domain/async）→ Asset CPU Completion → Audio Completion → UI Input Routing → Gameplay Action Mapping → Fixed Loop（每个 tick 内部 barrier +
command commit + transform propagation）→ Frame Update（variable delta）→ State Transition Commit →
Render Scene Extraction → UI Model
Commit/Layout/Paint Cache/Display List → GPU Upload Budget → Assemble immutable RenderFrame →
Render Pass → Present → Deferred Cleanup。

Simulation 默认固定 60 Hz，每帧最多追赶4步；Render 使用可变帧率和 interpolation alpha。
Runtime 每帧把单次单调时钟采样差传给
`FixedStepAccumulator::advance(realDelta, gameplayTimeScale)`：先钳制真实 delta，再计算缩放后的
`updateDelta`；超额整步 Simulation 债务被丢弃，小于一个 fixed delta 的余量保留。真实时间、
玩法缩放时间、被拒绝的真实时间和被丢弃的 Simulation 时间分别记录，UI/Asset/Audio timeout
仍使用未缩放真实时间。
所有队列必须有预算、统计和唯一所有者。`PlatformFrameView`、同步
`PlatformEventDispatcher`、未来通用 Runtime Event Queue 与 UI routed event 相互分离；UI 先产生
`Tina::UI::InputTransitionConsumptionView` 与 `Tina::UI::ContinuousControlClaimsView`，避免玩法输入穿透。
Pressed/Released Action 只由下一个实际
fixed tick 消费一次，本帧0步不丢失、4步不重复。`IGameState` 结构变更只在 Frame Update 后提交；
新状态参与同帧 Render/UI snapshot，下一帧才接收输入；World command 在每个 fixed substep 末提交。

Action Map 将 edge/held/axis 显式归入 Simulation 或 Frame domain。`FixedUpdateContext` 只读取目标
tick 的 Simulation Action；`FrameUpdateContext` 只读取当帧 Frame Action。Runtime 不把同一隐式
Pressed 同时广播给两个更新域，避免0步帧之后发生双重执行。

Fixed Simulation 和 Render Scene Extraction 是首期仅有的 Worker barrier。每个 barrier 必须完成
当前 TaskGroup 才能提交结构变更或 reset FrameArena；Worker 完成顺序不能改变 Entity、Event
或 RenderItem 的最终顺序。主线程其余阶段禁止无界 wait。GPU Upload 产生的新 Ready Asset
统一在下一帧 snapshot 可见，避免 extraction 过程中观察到半帧状态变化。
