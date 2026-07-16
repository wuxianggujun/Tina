# Runtime 与 Frame Pipeline

## 当前实现

Application 已集成 GLFW Window/Input、EventSystem、资源管理器、miniaudio、SceneManager 和 bgfx。Core 提供 Clock 与 FrameTimer。

当前每帧的实际主线程顺序为：Poll Platform/Input → Event Queue → Asset Completion → App Event → Fixed Simulation → Variable/Scene Update → UI Node Update → UI Batch Layout → UI Hit-test/Routed Event → Scene Render → App Render → Input End Frame → Deferred Task → Present。Event Queue、Asset Completion 和 UI Pointer Routing 都只有一个明确泵送点；资源 completion 默认每帧最多提交8个。

Fixed Simulation 默认 60 Hz、每个 Render Frame 最多追赶4步；超出保护阈值的积压会被丢弃，Render 可通过 `interpolationAlpha()` 读取 accumulator 插值比例。GameScene 的 ECS、碰撞、水模拟、粒子与昼夜推进已进入 fixed phase，相机和 UI 保持可变帧率。

Application 初始化现在保留明确的成功状态；任一必需子系统失败会按逆序回滚，`run()` 会拒绝半初始化实例。`--smoke-frames=N` 可在提交 N 帧后沿正常生命周期退出，用于验证析构和资源回收。

## 已知问题

- Scene 操作和资源上传/销毁还没有完全归入独立 Frame Phase；
- 当前 Application 仍承担过多系统所有权，尚未收敛到 EngineHost/factory/阶段 Context 接口。

## vNext 所有权

vNext 以 `EngineHost::Create(EngineConfig, EngineFactories)` 建立唯一组合根。Factory bundle
由 executable 选择 GLFW/bgfx/miniaudio 或 Headless/Null backend，成功创建后 EngineHost 接管
所有权；Runtime 本身不依赖具体 backend。`IGame` 和 overlay `IAppState` 通过短生命周期的
Start/Fixed/Update/RenderExtraction/UIUpdate/Stop Context 接收阶段能力，禁止保存 Context、
builder、span 或模块 owner；禁止 `Application::instance()`、Singleton 和 Service Locator。

初始化每成功一步立即登记对应逆操作。创建失败必须返回完整上下文错误并逆序回滚，不能
留下可调用 `run()` 的半初始化对象。Engine 状态为 Ready → Starting → Running → Stopping →
Stopped/Failed，`run()` 只允许一次。`onStart` transaction 成功后 `onStop` 恰好一次；失败
自动撤销启动期状态但不调用 `onStop`，重复 shutdown 保持幂等。Frame 回调返回 Status，异常
在 IGame/Task/C callback 边界转换。完整接口和迁移切片见
[vNext 目标架构](vnext-architecture.md)。

目标初始化顺序为：Diagnostics → MemorySystem → Headless/GLFW Platform → TaskSystem →
RenderDevice → AssetSystem → AudioEngine → Window UIContext/AppState → Game。统一关闭顺序为：

1. Game/AppState/UI roots/World 停止产生工作并完成自己的 TaskGroup；
2. Asset 停止新请求、取消未提交工作并完成 IO/CPU barrier，暂时保留 Audio lease；
3. Audio 停止 callback/stream producer、drain completion 并释放所有 lease；
4. Asset 丢弃迟到 completion，drain upload/destroy ticket 并释放 CPU/GPU payload；
5. Render 完成 deferred retire、关闭 device/surface；Audio backend 完成最终销毁；
6. TaskSystem 停止接收并 join；Platform 销毁窗口和 backend；
7. 检查所有 MemoryTag 后销毁 MemorySystem，最后关闭 Diagnostics。

barrier 超时绝不能 reset Arena 或继续普通析构。Engine 转 fatal-stop、请求协作取消并等待
join；超过硬 shutdown deadline 仍有线程访问 Engine 内存时记录 CrashContext 后 fast-fail，
不能冒险释放对象制造 UAF。详细 barrier 见 [Task System](task-system.md)，内存归属见
[性能与内存](performance-memory.md)。

## Event Queue 契约

Runtime Event Queue 只承载主线程生命周期事件，例如 Window、设备连接、Asset 状态通知和
App 自定义短事件；它不是 UI routed event，也不是 fixed gameplay command queue。

- 后台线程只能写入有界 ingress queue，主线程在 Event Phase 按 sequence 稳定合并；
- Event payload 在 dispatch 期间不可变，跨帧 payload 必须 owning，不能借用 Worker/Arena；
- 订阅返回 RAII `SubscriptionToken`，dispatcher 先销毁、回调中退订和订阅者自销毁都安全；
- dispatch 中新增订阅从下一事件生效，取消立即阻止尚未开始的回调；
- 同一 dispatcher 的递归 dispatch 默认入队到当前事件之后，禁止无界递归调用栈；
- 队列满时 Window close/fatal 等控制事件使用预留容量且不可丢；可丢 telemetry/refresh 事件
  必须由事件类型显式声明并记录 dropped metric；
- Event handler 不直接 push/pop AppState、销毁 World 或等待 Task，只提交延迟命令；
- 优先级只区分 Control/Normal/Background，同优先级按全局 sequence，不能依赖地址或线程
  完成顺序。

UI Pointer/Focus 继续由每窗口 UIContext 路由；Gameplay Action 带目标 simulation tick，由
fixed loop 消费。这三种语义不能为了复用一个 EventBus 而合并。

## 目标 Frame 契约

帧阶段固定为：Poll Platform → InputFrame Finalize（Snapshot + ordered transitions）→ Event Queue → Asset CPU Completion → Audio
Completion → UI Input Routing → Gameplay Action Mapping → Fixed Loop（每个 tick 内部 barrier +
command commit + transform propagation）→ Variable Update → Render Extraction → UI Model
Commit/Layout/Display List → GPU Upload Budget → Render Pass → Present → Deferred Cleanup。

Simulation 默认固定 60 Hz，每帧最多追赶4步；Render 使用可变帧率和 interpolation alpha。
所有队列必须有预算、统计和唯一所有者。InputFrame、普通 Event Queue 与 UI routed event
相互分离；UI 先产生消费掩码，避免玩法输入穿透。Pressed/Released Action 只由下一个实际
fixed tick 消费一次，本帧0步不丢失、4步不重复。AppState 结构变更只在 Deferred Cleanup
提交，新状态下一帧才接收输入；World command 则在每个 fixed substep 末提交。

Action Map 将 edge/held/axis 显式归入 Simulation 或 Frame domain。Fixed Context 只读取目标
tick 的 Simulation Action；Variable Context 只读取当帧 Frame Action。Runtime 不把同一隐式
Pressed 同时广播给两个更新域，避免0步帧之后发生双重执行。

Fixed Simulation 和 Render Extraction 是首期仅有的 Worker barrier。每个 barrier 必须完成
当前 TaskGroup 才能提交结构变更或 reset FrameArena；Worker 完成顺序不能改变 Entity、Event
或 RenderItem 的最终顺序。主线程其余阶段禁止无界 wait。GPU Upload 产生的新 Ready Asset
统一在下一帧 snapshot 可见，避免 extraction 过程中观察到半帧状态变化。
