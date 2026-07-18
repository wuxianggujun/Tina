# Carbon Engine 参考取证与 Tina 决策

## 参考范围

Carbon 只作为成熟工程经验来源，不作为 Tina 的依赖或兼容目标。官方源码来自
[Carbon Engine GitHub 组织](https://github.com/carbonengine)，产品与模块关系以
[Fenris Carbon 页面](https://fenris.com/carbon)为补充说明。

2026-07-16 已在本地 `temp/carbon-engine` 建立 13 个仓库的只读研究基线：
`core`、`exefile`、`trinity`、`ime`、`blue`、`resources`、`scheduler`、
`math`、`mesh`、`imagetools`、`imageio`、`audio` 和 `destiny`。各仓库 URL、
精确提交与用途记录在被 Git 忽略的 `temp/carbon-engine/REFERENCE_VERSIONS.md`；
参考源码不会进入 Tina 提交、构建或发布包。

官方组织当前没有独立公开的 `CarbonUI` 仓库。可研究的公开 UI 底层主要位于
`trinity/trinity/UI`；`mesh` 中的工具界面使用 ImGui，只是资产查看器，不是
CarbonUI 的 Retained Widget 实现。因此不能声称已经取得或复用 CarbonUI 控件源码。

## 结论摘要

Carbon 最值得 Tina 学习的是长期运行后形成的阶段边界、预算、失败恢复和延迟变更；
不值得复制的是 Python/Blue 暴露、全局对象、原始指针回调、庞大的历史 API 和特定
产品数据格式。部分公开源码本身也包含明显历史债务，所以“运行多年”只能证明很多
设计问题值得研究，不能替代 Tina 自己的 RAII、generation handle 和自动化门禁。

| 领域 | Carbon 证据 | Tina 采纳 | Tina 明确拒绝 |
| --- | --- | --- | --- |
| Core | `ScopeGuard`、Time、Telemetry/Statistics、命名锁/线程、Memory Tracker、Callstack/Crash 和细粒度测试；`CachedAllocator`/`TrackableContainer` 提供命名与用量观测 | 保留 Tina 现代 Result/ScopeExit/chrono；增加 EngineHost-owned Metrics、TraceZone、MemoryTag、CrashContext、UTF-8 原子 IO，并记录 current/peak/high-water | 全局 new/delete、宏式分配、singleton pool、固定 `growBy`、强杀线程、全局 profiler/crash pointer、32位 FNV 资产身份 |
| 进程与 Runtime | `exefile` 按模块启动；`PumpOSInternal()` 固定时间、ticker、统计与 pending-delete 顺序 | 把初始化拆成可注入事务并逆序回滚；保留清晰 phase、阶段末提交与 deferred cleanup | Blue 动态函数表、`BeOS` 全局、`void*` tick cookie、Python/Stackless 启动链、巨型全局 pump/组合根 |
| Window/Input | `Tr2MainWindow` 分离 Down/Up/Move/Wheel、Key/Char、Focus、Close；Windows 按下建立 Capture，释放后解除 | 保持 GLFW；独立实现 normalized WindowCreatePlan、hidden create transaction、generation registry、callback collect/final snapshot 与 scope rollback；继续分离输入快照、生命周期通知和 UI routed event | 复制 Carbon 源码/Win32 风格枚举、消息宏、native pointer 或 Carbon 窗口 API；引入 SDL/SDL3 |
| UI/IME | `IUILib.h` 体现 MouseDown/Up/CaptureChanged 与 Set/KillFocus 分离；`ime` 显式拥有窗口 HIMC 上下文 | 把 Tina Legacy generation `NodeId` 语义迁移为带 owner 的 `UINodeId`，保留 Capture/Target/Bubble、Modal Focus Scope 和 IMM32；只借鉴上下文所有权 | 旧全局 UI 状态、固定 256 wchar 缓冲、全局 IMM32 函数指针；把 ImGui Viewer 当 CarbonUI |
| Render | `TriRenderJob` 顺序执行命名 Step，统一 GPU marker/CPU-GPU 计时，并在失败或中断时检查、修复 RT/DS 栈；Trinity 有 Stub 后端 | 小型顺序 Pass Scheduler、命名和统计、显式资源状态、失败后清理、NullRenderDevice | 数十种运行时可配置 Step、跨帧 `IN_PROGRESS`、隐式 Push/Pop 状态作为公共 API、完整自研多后端 RHI |
| 异步资源 | `BlueAsyncRes` 后台 `DoLoad`、主线程 `DoPrepare`，支持取消、队列泵送和后台内存预留 | 分离 CPU Decode 与 GPU Upload；generation/取消贯穿两队列；同时按任务数、字节和时间预算 | 原始 `this` 回调、析构前要求外部手工清空监听、多个 bool 拼出的模糊状态 |
| 资源交付 | `resources` 使用版本化 ResourceGroup、校验和、Bundle/Patch 与无效版本测试 | Cooked Manifest 包含 schema、稳定 AssetId、依赖、内容 hash 和产物位置；明确 Bundle/Patch 层 | 把交付分组误当运行时 typed asset registry，或直接采用 Carbon 私有格式 |
| Cooker | `mesh` 先构建 CMF，再验证生成结果，最后写文件；处理器有独立命令与诊断 | `tina_assetc` 采用 Parse → Validate → Build → Validate Cooked → Atomic Write；错误返回源路径与不支持特性 | CMF、FBX 工具链、Viewer ImGui 和产品专用处理命令进入首期 Runtime |
| Scheduler | Scheduler 支持按时间或任务数量运行，并保证极小预算下至少推进一个任务 | completion/upload queue 采用可观测预算和饥饿保护 | Greenlet、Python Tasklet/Channel、全局回调进入 Tina 首期 |
| 固定步模拟 | Destiny 保留 accumulator 余量、限制追赶、保留 old/new state，并在 evolve 外处理 moribund 对象 | 继续 60 Hz、最多4步和 interpolation；增加 Simulation mutation barrier 与 deferred command buffer | 1秒 MMO tick、Ballpark 单体、Python 事件和产品 bubble 逻辑 |
| Audio | Carbon Audio 有 `Uninitialized/Disabled/Enabled` 和 SoundBank 状态；后台回调通过主线程队列通知；按距离/可见性/重要性裁剪声音 | 保持 miniaudio；后续使用 generation `AudioVoiceId`、音频命令队列、主线程 completion 和 voice budget | Wwise 依赖、`g_audioManager`、回调 cookie 裸指针和 Carbon 的产品元数据格式 |
| 坐标转换 | Carbon Audio 在 Wwise 适配边界显式执行 RH→LH 转换 | Tina 内部统一右手、Y-up、-Z forward，只在第三方后端适配层转换并测试 | 在 World、组件或玩法代码中混用坐标约定 |

## 关键取证

### Runtime：学习分阶段回滚，不照搬全局运行时

`exefile` 的启动流程把模块加载、崩溃报告、Socket Logger、Windows timer resolution、
控制台、路径和脚本运行时分成顺序阶段，并在成功后立即登记对应的退出动作。这证明
“初始化阶段必须拥有自己的逆操作”是成熟 Runtime 的核心契约。

Tina 当前 `Application::shutdown()` 已按上层对象、资源、音频、事件、bgfx、窗口的
顺序幂等释放，但初始化仍直接创建真实 GLFW/bgfx/miniaudio 对象，难以逐失败点测试，
并保留静态 `Application::instance()`。vNext 目标明确改为非全局 `EngineHost`，但按可运行
垂直切片迁移：先以 Null Runtime 建立 subsystem factory/phase seam，使测试能在 Window、
RenderDevice、Event、Input、Resource 等任意阶段注入失败并验证逆序回滚，再逐步迁入
2D/UI/3D。完成迁移前不再增加新的全局访问点。

`BlueInterface::LoadBlue()` 还提供了一个值得避免的半加载反例：它先把动态库句柄写入
`m_module`，再逐项解析函数；任一符号缺失会直接返回 `false`，但对象已持有模块句柄，后续调用又会因
`m_module` 非空提前返回 `true`。析构最终能够卸载模块，并不能修复“失败候选已被发布”的状态语义。
Tina 的 factory/owner 必须先在局部候选中完成全部验证，成功后一次性发布；失败则按已完成阶段逆序
回滚、保持 owner 未绑定，并保留最初的结构化错误。这个结论来自当前源码行为，不代表 Carbon 项目
一定以该路径作为正式产品门禁。

### Blue OS pump：学习顺序和延迟清理，不复制通用 Tick

Carbon `IBlueOS` 公开全局 `BeOS`，`RegisterForTicks()` 接收 `IBlueEvents*` 与原始 `void* cookie`，
`TickTickers()` 再把该 cookie 交给通用 `OnTick(realTime, simTime, cookie)`。公开代码中
`Tr2MainWindow::OnTick()` 甚至为空，说明“所有对象都挂到 Tick”会留下没有明确阶段职责的订阅面；
这是 Tina 不应复刻的历史接口，而不是现代 Game SDK 的依据。

另一方面，`PumpOSInternal()` 的执行顺序本身有参考价值：它显式防重入，依次处理等待/网络、时间、
Python task、time dilation、ticker、帧率统计，最后执行 recycler、`ProcessPendingDeletes()`、内存跟踪
和统计更新。Tina 采纳的是“顺序可见、变更在阶段末提交、清理延迟到安全边界”这三个契约，继续使用
Platform/Event/UI route/Fixed Update/Variable Update/Render/Deferred Cleanup 等有名 phase 和窄
context；不会引入 `BeOS` 式全局对象、巨型 pump 或无类型 cookie。

### Core：学习可观测契约，不复制历史基础库

Carbon Core 将 Scope Guard、高精度时间、线程/锁命名、Telemetry Zone、每帧/生命周期
Statistics、内存跟踪、调用栈、崩溃字段、UTF-8 转换和文件路径分别测试。对 Tina 最重要
的启发是：Core 的正确性不是一个 umbrella header，而是一组可独立验证的底层契约。

Tina 已有 C++23 `std::expected` Result、`ScopeExit`、可注入 `IMonotonicClock` 和
`FixedStepAccumulator`；旧 `FixedStepTicker` 仅作为 Legacy 兼容，应继续
保留。vNext 增加 EngineHost-owned `MetricsRegistry`、后端无关 `TraceZone`、MemoryTag
current/peak、CrashContext、UTF-8 路径与原子写；线程使用 `std::jthread/stop_token`，只在
平台层增加名称和优先级适配。全局 `new/delete` 替换、分配宏、`CcpKillThread`、全局
`BeCrashes`、函数指针加 `void*` 回调和32位 FNV 资产身份明确不采纳。完整专项矩阵见
[tina_core 设计与 Carbon Core 取证](core.md)。

Carbon Core 的 `WITH_TELEMETRY` 实际接入 Tracy 0.13.1，并对 zone、lock、连接状态做专门
测试；本地13个参考仓库未发现 `TinyProfile` 模块。Tina 因此采用自有 Trace/Metrics 前端 +
可选 Tracy backend，同时保留独立 `tina_bench`。Profiler 负责解释热点，不能取代固定
workload、固定门禁机和 p50/p95/p99 回归基准。

本轮进一步确认 `CcpTelemetry` 不是一个简单的“开/关宏”：它用
`StartRequested → Started → StopRequested → Stopped` 管理 session，在 tick 边界发出 `FrameMark`；
`CcpStatistics` 则把命名统计以 `TracyPlotConfig`/`TracyPlot` 送入 profiler。这些状态、帧标记和
命名曲线值得借鉴，但 Tina 的 session、事件订阅与统计注册表仍由 `EngineHost` 拥有和关闭，不能变成
进程全局 profiler。

`CachedAllocator` 与 `TrackableContainer` 同样应拆开评价：命名分配与 used/allocated 统计能为
Tina 计算 current/peak 和 pool 高水位提供证据；但前者依赖每类型 singleton、预分配后按固定 `growBy` 扩容和宏式
`new/delete`，后者通过命名 allocator/容器派生绑定全局内存宏。Tina 只采纳可观测指标，继续使用
标准库/`std::pmr`、显式 owner 和有界专用结构，不复制这些通用容器、singleton 或增长策略。

Carbon Audio 也提供了反例：当前公开 `AudManager` 析构对
`m_soundPrioritization` 存在重复删除路径，`InitLowLevel()` 或 `InitSound()` 中途失败时也
没有逐阶段回滚。这进一步说明 Tina 必须依赖自动化失败注入和 RAII，而不能因为代码
来自成熟引擎就默认生命周期正确。

### Window/UI：Tina 已有更现代的句柄安全

`Tr2MainWindow_Windows.cpp` 在鼠标按下时 `SetCapture`、对应释放时
`ReleaseCapture`，并把 Mouse、Key、Char、Focus 和 Close 回调分开。这个契约与 Tina
当前单次 hit-test、Pointer Capture、KeyDown/KeyUp、TextInput/Composition 分流一致。

Carbon 的公开 `IUILib.h` 是 Win32 风格的历史接口；Tina vNext 的 generation `UINodeId`、
节点删除后重新解析、RAII 订阅、Modal Focus Scope 和每窗口 `UIContext` 更适合当前
目标。Carbon 参考不会改变 Tina 的自研 UI 路线，也不能替代 Checkbox、Slider、
可访问语义和 Display List 的自主设计。现有公开 Carbon UI 证据能支持 native window、focus/capture
和输入事件语义，却不足以证明现代 retained tree 的 root ownership、generation-safe mutation、
事务 layout snapshot 或后端无关 paint/display list；Tina 不能用这些公开接口替代自己的 retained
ownership 设计。

本批 GLFW adapter 只采纳上述**阶段与所有权思想**，没有复制 Carbon 实现：先把 Tina
`PrimaryWindowConfig` 规范化为内部 `WindowCreatePlan`，再以 hidden `GLFW_NO_API` window 完成
generation registry、callback 和 initial snapshot 后才显示；C callback 只采集 Tina transition，
Poll 统一冻结 final snapshot；任一步失败由 scope rollback 逆序撤销。对应源码、类型名、错误码、
测试 seam 和 CMake target 全部由 Tina 独立实现。

明确拒绝把 Carbon 的原生 Win32 枚举、消息宏、raw native pointer、Blue/Python入口、全局 callback
或全局 service 移入 Tina。Carbon 仓库不进入 Tina 构建、链接、提交或发布包；“成熟引擎运行多年”
只说明这些问题值得研究，不能替代 Tina 自己的构建、分进程测试、样例运行和资源回收证据。当前
M7-C1c-b3e 的 Windows Debug/Release 事实分别为：Null 图 `tina_tests` 194/194、`tina_ui_tests`
81/81、`tina_runtime_ui_tests` 46/46；可选 adapter 另有 `tina_platform_glfw_tests` 25/25 与
`tina_render_bgfx_tests` 11/11。Linux b3e 的 GCC 与 Clang sanitizer Null 图也通过194/194、81/81、46/46，
以及Null样例300帧；Clang 无 sanitizer 诊断。它们是五个独立 GoogleTest
executable，不能相加成一个“总测试数”。

### Render：采用 Step 契约，缩小为显式 Pass

`TriRenderJob::Run()` 会复制当前步骤，按顺序执行 `BeginExecute → Execute →
EndExecute`，并在 Step 返回失败、终止或跨帧未完成时停止。每个 Step 统一建立 GPU
marker 和计时；Job 记录执行前 RT/DS 栈深度，异常退出时尝试恢复。

Tina 应保留这些可观测性与失败恢复思想，但把公共模型缩小为固定帧内 Pass：3D
Opaque、2D、UI、Present。每个 Pass 显式声明名称、顺序、读写资源与 clear/load/store，
执行失败后停止后续依赖 Pass，并由调度器收敛临时资源。bgfx 类型只存在于实现层，
NullRenderDevice 用同一描述验证顺序、generation handle 和资源计数。

### Asset/Cooker：形成 CPU、GPU、交付三条边界

Carbon 的 `BlueAsyncRes` 把后台加载与主线程 Prepare 分开，`IBlueResMan` 提供主线程
queue pump、取消、暂停和后台内存预留。`resources` 解决版本化文件清单、Bundle 和
Patch，`mesh` 则执行“生成后验证再写盘”。三者承担不同职责，不能合并成一个
ResourceManager。

Tina 对应拆分为：

1. Runtime Asset：`AssetId`、状态机、generation、依赖和客户端句柄；
2. Decode/Upload：后台 CPU decode 与主线程/GPU upload 两个预算队列；
3. Cook/Delivery：`tina_assetc` 生成并验证 Cooked Asset，Manifest 管理 schema、
   content hash、依赖与产物位置，Bundle/Patch 以后单独增加。

首个 glTF 版本仍只支持静态三角形 Mesh、节点层级、Position/Normal/UV0/Index、基础
颜色和纹理；Skin、Animation、Morph 与压缩扩展必须返回明确诊断，不能静默降级。

### Simulation 与 Audio：延迟变更比新功能更重要

Destiny 的 Ballpark 会保留 accumulator 余量、限制过量追赶，并保存 old/new 状态供
插值；演化中的对象只标记 moribund，真正删除在阶段外按预算执行。Tina 当前固定 60 Hz、
最多4步和 interpolation 已对齐，下一项应是 World mutation barrier：fixed update 中的
创建、销毁和层级变化写入 command buffer，在阶段末统一提交。

Carbon Audio 的声音裁剪会考虑距离、是否在衰减范围、2D/vital、可见性和当前事件数。
这适合成为 Tina 未来的 voice budget 输入，但首期不引入 Wwise、SoundBank 或产品 JSON。
Tina 当前 miniaudio 路径只需要先保证 Engine/Resource/Voice 的关闭顺序、异步回调不触碰
已销毁对象，以及坐标转换只发生在后端适配层。

## 本轮 Tina 设计状态

以下状态描述的是 Tina 当前分批实施边界，不是从 Carbon 直接移植的功能：

- **M7-C1c-b3c 已实现**：`EngineHost` 私有拥有 primary-window `UIContext`，在 Platform lifecycle
  dispatch 后选择 Context、生产 UI route，再把结果交给 ActionMapper；Context owner 不隐式执行
  layout。该切片闭合的是 owner 与输入时序，Game SDK 仍不能创建 retained root，也不代表窗口已经
  显示 Widget、中文文本或 UI draw call。
- **M7-C1c-b3d1 已实现**：把 focused `UIContextCapacityConfig` 与 shared validator 接入
  `EngineConfig::primaryWindowUICapacities`，并增加 Runtime-private 每帧 layout coordinator。它在
  `updateUI` 成功后、Render submit 前按窗口 logical extent 对每个严格递增 `PlatformFrameId` 至多尝试
  一次 layout commit；Headless 双缺席成功 no-op，失败阻断 Render 且消费 frame attempt。hit-test 和
  route 继续只读取上一份 committed snapshot，不能隐式触发布局。该 b3d1 历史切片的独立
  Runtime→UI 门禁为29/29；b3d2 历史门禁为42/42。
- **M7-C1c-b3d2 已实现**：ADR 0021 接受的 startup transaction 已接入
  `initialPrimaryWindowMetrics`，该 seed 不 poll、不消耗 frame id；Runtime 在 `onEnter` 前显式绑定
  primary-window `UIContext` 或 Headless 状态，再向游戏侧提供 root-scoped、phase-epoch-scoped
  `PrimaryWindowUIRootBuilder`/`PrimaryWindowUITreeUpdater`。普通 Game SDK 不获得裸 `UIContext*`，也不能在任意
  阶段调用 `createRoot()` 或跨 phase 借用；root ownership、generation 校验、sticky 首错、phase expiry
  和异常/失败边界的 `abortPhase()` 仍由 Runtime/UI owner 管理。该设计不包含可见 UI 验收结论。
- **M7-C1c-b3e 已实现**：routed Pointer listener 可通过 `claimPointerButton()` 请求接管当前 route
  Window/Pointer 上仍 held 的 primary Pointer Button；Runtime 按最终 snapshot 过滤、跨 route 去重并使用
  Create 期 PMR 双 buffer 发布。ActionMapper 会取消 active Gameplay source，或拦截同帧未 consume 的
  ButtonDown，并抑制到真实 Up。该切片不包含 Key/Gamepad/axis claim、Focus/Capture/Modal 或可见 UI。

这四步延续了 Carbon 值得学习的事务发布、明确泵送顺序和延迟清理，同时拒绝 `BeOS`/BlueInterface
式全局或半加载状态。后续只有在 Widget model、paint snapshot/Display List、字体/中文路径与真实
bgfx UI pass 都建立并通过可见 smoke 后，才能宣称 vNext UI 可见可用。

## 调整后的推进顺序

1. 冻结完整 vNext 模块、Core 契约、公共接口、Frame Pipeline 和依赖方向；
2. Null Runtime：新 `EngineHost`、失败回滚、Metrics 和 NullRenderDevice 连续300帧；
3. Platform/UI：迁移 GLFW、`PlatformFrameView`（最终 Snapshot + 有序 transitions）、中文、IMM32 与基础 UI；
4. Scene/2D：generation Entity、command buffer、Render Scene Extraction 与 Sprite；
5. Render/3D：typed handle、Pass Scheduler、bgfx、Perspective 与 depth；
6. Asset/Cooker：双阶段队列、AssetId、Manifest 和最小静态 glTF；
7. Product 2D/UI/Audio：正式 TileMap/Box2D、Checkbox、Slider、设置后端与 miniaudio 生命周期；
8. 新路径覆盖全部验收后独立删除 Legacy。

每批修改继续遵守 Visual Studio 2026 / MSVC 19.50 与 Linux 构建、直接执行 GoogleTest、不使用 CTest、
运行 2D/UI/3D 对应冒烟、验证资源释放、更新 UTF-8 文档和独立提交。
