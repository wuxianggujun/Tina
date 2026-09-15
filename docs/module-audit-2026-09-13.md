# Tina 逐模块审查与下一阶段建议（2026-09-13）

第 1–5 节保留 `ae5d5981` 基线的原始静态审查；其后续实施见第 6 节和
[容量与可靠性实施记录](capacity-and-lifetime-2026-09-13.md)，历史行号不对应修改后的工作树。

## 1. 结论与审查边界

**下一阶段应优先修复生命周期、回调重入与失败可观测性，再验收现有 Editor/游戏消费链，而不是继续横向增加模块。**
当前源码的能力明显多于部分完成度文档所描述的能力；“尚未实现”和“已实现但尚缺当前基线的产品证据”必须分开。

- 源码基线：`ae5d5981`；开始审查时工作树干净。
- 方法：逐模块核对公开契约、CMake 接线、代表实现、关键创建/提交/失败/关闭路径及现有测试源码。
  这是模块级静态审查，**不是每个文件逐行审计，也不是整个项目无缺陷证明**。
- 本轮仅更新文档；不修功能源码、不新增或修改测试，不 configure/build，不运行 GoogleTest、sample、smoke、视觉或平台 gate。
- 下列 F1–F7 是由源码控制流定位的问题；F8 是失败可观测性缺口。全部尚未运行复现。
  本轮执行的文档检查器只验证链接、preset 和部分 target 文案，不构成产品测试。
- 历史 Unit/Build/Visual 结果不作废，但不能证明新增场景或当前源码已经通过。活跃状态与验收条件以
  [Backlog](backlog.md) 为唯一明细；本文件保留这次审查的证据和推导。

### 推荐顺序

1. **可靠性批次**：F1 高核心数默认启动、F2 Signal 队列生命周期、F3 Scene2D 音频关停先修；
   随后集中处理 F4–F8 与既有 `NAV-GRID-PMR-001`。
2. **产品批次**：完成一个真实工程的“导入 → 保存/重开 → Play → Stop → 切换工程/退出”；
   同时确认取消失败不丢旧 Catalog、不污染 authoring document、不提前释放资源。
3. **发布与测量批次**：按 source/build-id 对齐 installed consumer、平台证据和性能基线；
   再按实际游戏需要接入 Save、AI、Navigation3D、Localization。脚本/navmesh/更多渲染特效不应抢在前三项之前。

## 2. 模块覆盖矩阵

“未新增问题”只表示本次阅读范围未定位到新的确定问题，不代表模块已经全面通过验收。
下面的“后续”包括既有 Backlog，不都属于本轮新增缺陷。

| 模块 / 边界 | 本轮核对入口与现有能力 | 结论 / 后续 |
| --- | --- | --- |
| Core / IO / JSON | [Core](../src/core)、`WriteFile.cpp`、`PackageFile.cpp`、`JsonDocument.cpp`：Result/PMR、严格 UTF-8、原子替换、不可变映射 pin、JSON 输入预算 | 原子可见性不能写成掉电持久化；宽 JSON object 构建存在平方级查找，先测再优化，见第 4 节 |
| Math | [Mat4.hpp](../include/tina/math/Mat4.hpp)、`Geometry3D.hpp` 与数值测试：唯一几何类型、逆矩阵、空间查询 | F6：inverse 的 double → float 窄化未校验范围 |
| Task | [TaskSystem.hpp](../include/tina/task/TaskSystem.hpp)、BoundedTaskSystem、TaskGroup 与 ownership tests | F1 自动 worker 超上限；F8 异常无失败通道。`cpuWorkerCount=0` 现为自动，IO-only 必须显式 opt-out |
| Runtime / Input mapping | [EngineHost.cpp](../src/runtime/EngineHost.cpp)、StateTaskScope、ActionMapper、GameSettings：帧序、scope、Stopping/retry、输入扇出 | 不能回退已完成的可重试 owner 关闭；UI claim 在 Action 前。后续验收多 State/失败候选/Audio deadline 组合，不另建全局 owner |
| Platform 公共面 / Headless | [Platform](../include/tina/platform)、进程内 Clipboard、ordered input 与 WindowSurface | Clipboard 缺失以空 capability 表达；headless clipboard 不等于系统剪贴板 |
| GLFW adapter | [GLFW](../src/platform/glfw)：真实窗口、剪贴板、输入、IME、surface lease | 真实剪贴板占用、IME 候选窗、DPI/热插拔仍需平台与人工证据；源码接线不能替代这些证据 |
| Android adapter / bootstrap | [Platform Android](../src/platform/android)、[AndroidEngine.cpp](../src/android/AndroidEngine.cpp)：native rebind、组合根、移动输入 | 后端与手柄已存在；双手柄、后台恢复、触摸/键盘共存和 Vulkan 真机验收应接续 MOBILE-001，不重写后端 |
| iOS adapter | [iOS](../src/platform/ios)、IosSession/InputBridge/CompositionSession 与 host 接线 | iOS 不是零实现；host-independent adapter tests 由模块 CMake 接入，不能误判为漏注册。Apple host/真机证据单列 |
| HTML5 adapter | [HTML5](../src/platform/html5)：canvas/外驱 tick、TouchSlotTable、溢出取消 | 重点为浏览器真实触控、失焦与恢复；当前无 Clipboard capability 是明确边界 |
| Desktop bootstrap | [DesktopEngine.cpp](../src/desktop/DesktopEngine.cpp)：GLFW/bgfx/Task/可选 adapter 组合 | 受 F1 影响；不由 sample 各建一套设备/主循环。源码中旧 IO-only 注释也应随未来修复校正 |
| AssetFormat / AssetTypes | [AssetFormat](../src/asset_format)、[Asset 模块接线](../src/asset/CMakeLists.txt)：wire 校验、弱 Handle/Resolver 窄边界 | 新 schema 必须同步 producer/reader/fixtures；不能为旧散文件或旧版本增加静默 fallback |
| Asset / Cooker / Catalog | [AssetSystem.cpp](../src/asset/AssetSystem.cpp)、CatalogPackage/Publish、GltfFileSnapshot、binding registries | TPCK schema 2 与稳定 borrow 已存在。后续重点是旧映射存活时替包、worker 迟到结果、磁盘失败和实际 Editor 重开，而不是再实现 Catalog 层 |
| Render / bgfx / Video | [RenderScene.cpp](../src/render/RenderScene.cpp)、RenderFrame、BgfxRetirementTimeline、SurfaceFramePlanner、retireTexture2D、VideoDecode | CPU packet completion 与 GPU retirement 分账；后处理/自定义 fragment 已有实现。GPU、driver、视频 codec/设备能力需真实消费证据，不能从能力声明推定播放通过 |
| Scene | [World.cpp](../src/scene/World.cpp)、World2DSnapshot、PrefabInstantiate：层级、预检/回滚、2D/3D extraction | 已有回滚实现；失败注入与原有实体不受损验收继续沿 SCENE-LOAD-ROLLBACK-001，不将预检等同于任意异常的全事务 |
| Gameplay 工具 | [Scheduler.cpp](../src/gameplay/Scheduler.cpp)、ActionRunner、[Signal.hpp](../include/tina/gameplay/Signal.hpp) 与现有五组测试接线 | F2/F4/F5；ActionAuthoring/ActionRunner 测试文件确实存在，旧“无法链接”描述已过期 |
| Gameplay2D | [Scene2DRuntime.cpp](../src/gameplay2d/Scene2DRuntime.cpp)：TileMap/FX/Nav/Audio owner、物理桥 | F3；实质 Scene2DRuntime 测试位于 `tests/physics2d`，不能只看 `tina_gameplay2d_tests` 的 header-isolation 结果 |
| Gameplay3D | [Scene3DRuntime.cpp](../src/gameplay3d/Scene3DRuntime.cpp)、公开契约：单 Prefab 实例、动画 Lease、可选物理桥 | 已有实现；产品拥有，Editor Play 使用隔离 World。关注 Stop 后 authoring 文档不被运行态修改，非“尚无 3D gameplay” |
| AI | [StateMachine.cpp](../src/ai/StateMachine.cpp)、BehaviorTree、Blackboard 与 `AITests.cpp` | F7；基础决策层已有，但真实 gameplay owner、预算耗尽/取消/析构组合验收仍薄弱 |
| Navigation2D | [Navigation2D](../src/navigation2d)：Grid/Data、分步 A*、Agent、FlowField | Grid/Data 的 Debug PMR 异常边界沿用 NAV-GRID-PMR-001；动态 revision、物理位移后的重新规划与稳定存储不宜拆成重复实现 |
| Navigation3D | [Navigation3D](../src/navigation3d)：PMR volume storage、体素净空/支撑、分步路径查询 | 是体素导航而非 navmesh；后续先接真实体素游戏消费者，ADR 0048 仍需审阅，不以新增 navmesh 替代消费验收 |
| Animation3D | [Animation3D](../src/animation3d)：ClipSampler、BlendTree、Graph、layer/root motion、IK | `samples/3d_animation_graph` 与 `samples/3d_ik_chain` 已接线；不能再写“只有 tests”。Editor authoring 与真实角色组合另验 |
| Localization | [LocalizationCatalog.cpp](../src/localization/LocalizationCatalog.cpp)、Asset bridge/cooker 与测试接线 | 模块与 producer 已存在；缺的是产品切换语言、字体回退/缺失 key 行为的消费验证，不是再写一套字符串表 |
| Save | [SaveStore.hpp](../include/tina/save/SaveStore.hpp)、SaveStore/SaveMigration 实现 | 同步/异步 slot、backup/digest/迁移均有实现；补独立[主题文档](save.md)与真实产品消费。durability、多进程共享不属于已证明契约 |
| Network / TLS | [Network](../src/network)、ReadinessPoller/DnsResolver/HTTP/WebSocket/TLS：owner pump 与 IO DNS 交接 | 固定缓冲不等于全模块仅 Create 分配；TCP 背压不等于 UDP 丢包。POSIX、非 loopback、平台信任库与 installed consumer 的当前证据沿 NET-001，不从旧文档推断“从未编译” |
| Audio / miniaudio | [AudioEngine.cpp](../src/audio/AudioEngine.cpp)、AudioDecode、MiniaudioDevice | engine 已能延期终态并等待实时 reader；F3 出在上层 owner 未遵守释放条件。真设备 callback 尾延迟与关闭交错仍需测量 |
| Physics2D | [Physics2D](../src/physics2d)：shape/joint/query/contact publication 与 tests | Chain/bridge 已存在。继续极端坐标、capacity、contact snapshot 和真实游戏负载验收，不把新约束需求当既有错误 |
| Physics3D | [Physics3D](../src/physics3d)：Jolt、Character/contact/shape cast、floating origin | 上述能力已实现；joint/compound/mesh/CCD 是扩展项。ADR 0050 仍 Proposed，跨平台/installed consumer/性能证据独立 |
| UI 核心 | [UI](../src/ui)：Element/tree 事务、layout/hit/paint/semantics 原子 publication、pipeline 与 tests/CMake | 不恢复 Legacy UI，不为 Editor 复制 UI 状态机。优先真实操作与发布失败保持旧 snapshot，dirty-range 性能需要 profile |
| UIFreetype | [TextShaper.cpp](../src/ui/freetype/TextShaper.cpp)、模块与字体测试 CMake | HarfBuzz/FriBidi/MSDF 已接入；多语言显示、双向编辑与不同平台 IME 验收是不同层面，不能统称“shaping 未实现” |
| UIUia | [UIA](../src/ui/uia)：COM/provider snapshot 与 HostBridge | 外部 action gate 不等于 Narrator/Inspect；本次未证明 COM snapshot OOM 的全事务性，不将其登记成确定缺陷 |
| Integration | [Integration](../src/integration)：WindowSurface 与 UI→Render 单向桥 | 保持 move-only surface owner 与 committed paint 借用寿命；不让 backend 回读 UI 节点 |
| Editor authoring | [Editor source](../editor/src)：World2D 文件、revision history、PlaySession 与 tests/CMake | authoring 文件/历史/运行态分层已形成。优先真实保存、重开、Undo/Redo、2D/3D Play→Stop 文档不变验收 |
| EditorApp | [Editor app](../editor/app)：SourceImportService、WorkspaceDocuments、WorkspaceLifecycle | 取消导入仍同步 join，需测尾延迟；现有导入/dirty-close 失败路径要纳入人工流程，不只验 auto-demo |
| Trace / Benchmark | [TraceTracy.cpp](../src/trace/tracy/TraceTracy.cpp)、[bench](../tools/bench)、preset | Trace 定位不等于 benchmark；固定 profile、样本数、fingerprint、median/MAD 与当前大场景负载需统一 |
| SDK / tools / 验证脚本 | [TinaRuntimeLibrary.cmake](../cmake/TinaRuntimeLibrary.cmake)、TinaGameSdkPackage/TinaConfig、samples/tools/CMake、Windows/Linux gate scripts、CheckDocs | 单 archive、feature/配置/tuple 与 installed headers 已有机制；本轮没有验证发布包。脚本覆盖不是全模块运行证明，DOC-002 还有路径/target 解析噪声，见第 4 节 |

公开头的定向第三方 include/类型扫描仅命中 `RenderDevice.hpp` 中解释 bgfx 的注释，未发现该扫描规则下的
实际第三方 include 或类型泄漏。此扫描不替代 header-isolation 编译与 installed consumer。

## 3. 源码问题与可验证修复方案

### F1

**[P1] 自动 CPU worker 数超出同一工厂的合法范围。** Backlog：`TASK-AUTO-WORKERS-001`。

- 证据：[TaskSystem.hpp](../include/tina/task/TaskSystem.hpp) 27–31、57–59 行计算 `max(1, hw-1)`；
  [BoundedTaskSystem.cpp](../src/task/bounded/BoundedTaskSystem.cpp) 351–364 行拒绝 CPU worker > 32。
- 触发：`hardware_concurrency()` 报告 ≥ 34，且使用默认配置；例如报告 64 时得到 63，默认创建返回
  `InvalidArgument`。Desktop/Android bootstrap 也消费这条默认策略。
- 修复：把“自动策略”和“合法上限”归到同一处；自动值 clamp，显式非法配置仍拒绝，不能悄悄修改用户显式值。
  IO-only 继续由 `disableCpuWorkers=true` 表达，不恢复旧的零值双义。
- 验证：为 helper 覆盖 0/1/2/32/33/64/128，核对自动结果可被工厂接受；另保留显式越界拒绝与禁用 CPU 的场景。
  现有 `BoundedTaskSystemTests.cpp` 的 helper 测试只列到 8，未覆盖上限交叉点。

### F2

**[P1] Signal::drain 回调内 clearQueued 使正在派发的 payload 与批次范围失效。**
Backlog：`GAMEPLAY-SIGNAL-CLEAR-001`。

- 证据：[Signal.hpp](../include/tina/gameplay/Signal.hpp) 311–328 行保存旧队列长度、按引用派发，
  随后 erase 旧范围；334–339 行 `clearQueued()` 直接 clear，没有派发保护。
- 触发：post 两个非平凡 payload，首个 subscriber 调用 `clearQueued()`。
  当前 callback 的 payload 立即析构，之后的 subscriber/下一条记录仍可能读它，尾部 erase 也不再是有效范围。
- 修复：在安全派发点实施延迟 clear，并明确当前消息、尚未派发消息及 callback 新 post 的处理规则；
  或提供显式可报告的重入拒绝契约。不能只增加文档禁令或静默忽略。
- 验证：析构计数 payload；首个 callback clear；同一 payload 的多个 subscriber；clear 后 post；
  callback 抛异常后的下一次 drain。现有 `ClearQueuedDropsPayloadsWithoutDelivering` 仅覆盖派发前 clear。

### F3

**[P1] Scene2DRuntime 关停没有证明音频终态，便释放借用 PCM 的 Lease。**
Backlog：`GAMEPLAY2D-AUDIO-STOP-001`。

- 证据：[Scene2DRuntime.cpp](../src/gameplay2d/Scene2DRuntime.cpp) 704–720 行忽略所有 `enqueueStop` 结果，
  只 pump 一次并清空 voice 跟踪；768–806 行随后清空 `m_audio_nodes` 及资产 borrow。
  [AudioEngine.cpp](../src/audio/AudioEngine.cpp) 1008–1047 行在实时 reader 未退出时保留 pending terminal。
- 触发一：`AudioEngineConfig::commandCapacity=1`，`playAudio()` 的 Play 尚在队列中就调用 runtime shutdown。
  Stop 被 `CapacityExceeded` 拒绝，随后 pump 可以应用 Play；runtime 却释放节点 Lease 并返回成功。
- 触发二：Stop 已接受，但 callback 尚在读取该 PCM；一次 pump 不是 quiescence 证明。
  若 Asset 已逻辑 unload，或调用方在 shutdown 成功后 unload，最后 Lease 释放可能留下悬空音频视图。
- 修复：分开“请求停止”和“确认全部终态”。未接受的 Stop 要重试；voice 未退休前保留跟踪与 clip Lease，
  返回可重试状态。不能关闭所有共享 AudioEngine 使用者，也不能在 owner 线程无界等待。
  析构及 build 失败回滚必须沿用相同 owner 保留规则。
- 验证：commandCapacity=1 的确定性场景；多个 voice 超过 command ring；受控 reader 暂停与释放；
  最后 Lease release 的时序、反复 shutdown 与 shared engine 其他 voice 不受影响。
  现有 [ShutdownStopsVoicesBeforeReleasingClipLeases](../tests/physics2d/Scene2DRuntimeTests.cpp) 382–406 行
  只覆盖队列可用且无 realtime 并发的正常路径，不证明上述两种边界。

### F4

**[P2] Scheduler / ActionRunner 创建时最后一次 reserve 失败会泄漏 Impl。**
Backlog：`GAMEPLAY-FACTORY-RAII-001`。

- 证据：[Scheduler.cpp](../src/gameplay/Scheduler.cpp) 166–173 行、
  [ActionRunner.cpp](../src/gameplay/ActionRunner.cpp) 355–362 行先 `new Impl`，再 reserve；
  catch 返回错误但没有释放已创建的 Impl。
- 触发：Pool 创建与 Impl 构造成功，`liveTimers/liveActions.reserve` 抛 `bad_alloc`；
  已移动进 Impl 的 pool 也失去 owner。
- 修复：候选 Impl 从创建开始就放入 `unique_ptr`，所有初始化成功后再交接；不增加手写多处分支清理。
- 验证：将失败注入精确落到最后一次 reserve，检查返回错误与 outstanding bytes 回到调用前基线；
  不仅检查错误码，也检查 pool 及 Impl 的析构。

### F5

**[P2] Signal 复用物理槽后违背订阅顺序契约。** Backlog：`GAMEPLAY-SIGNAL-ORDER-001`。

- 证据：[Signal.hpp](../include/tina/gameplay/Signal.hpp) 127、241–242 行承诺 subscription order，
  200–212 行复用 free slot，478–484 行按物理 slot 下标派发。
- 触发：A 占 slot 0，B 占 slot 1；退订 A，再订 C。实际顺序 C → B，契约要求 B → C。
- 修复：逻辑订阅顺序与槽位存储分离；保留 generation 与派发期间新增订阅下一次才生效的规则。
- 验证：初次顺序、退订后复用、派发中退订/新增、capacity 退让；
  现有 `EmitDeliversInSubscriptionOrderAndCountsSubscribers` 只证明未复用槽位时的顺序。

### F6

**[P2] Mat4::inverse 只检查 double 有限，未检查结果是否能用 float 表示。**
Backlog：`MATH-INVERSE-RANGE-001`。

- 证据：[Mat4.hpp](../include/tina/math/Mat4.hpp) 329–351 行，
  `scaled` 的 `isfinite` 检查在 `static_cast<float>` 之前，且没有范围校验。
- 触发：保留次正规数的环境下，`inverse(scaleMat4({1e-39F, 1, 1}))` 的逆尺度约为 1e39：
  double 有限，但超出 float 可表示范围，无法保证返回有限矩阵。
- 修复：窄化前检查 `numeric_limits<float>::max()` 范围，失败返回 `nullopt`；必要时复核最终矩阵有限性。
  不用任意“大 epsilon”拒绝所有小尺度可逆矩阵。
- 验证：极小非零 scale、最大可表示逆值附近、普通 affine/projection round trip、NaN/Inf/奇异矩阵。

### F7

**[P2] AI StateMachine 析构 exit 缺少重入 guard，可重复执行退出副作用。**
Backlog：`AI-FSM-EXIT-001`。

- 证据：[StateMachine.cpp](../src/ai/StateMachine.cpp) 26–29 行析构直接执行 exit，仍保持 Running 且
  `m_dispatching=false`；92–102 行的 cancel/reset 因此可以再次进入 exit。
  对照 BehaviorTree 析构已使用 DispatchGuard，以及 [ADR 0049](adr/0049-ai-decision-layer.md) 的拒绝重入要求。
- 触发：活跃 FSM 被销毁，其 exit 通过 userdata 调用同一 FSM 的 cancel/reset；exit 被调用两次。
  内层 cancel 自带 guard，因此这里不声称必然无限递归。
- 修复：析构也走 guarded、至多一次的结束路径；回调前锁定结束状态/借用，
  保留 Blackboard 长于 FSM 的契约，不能以停止 guard 检查换取“可重入”。
- 验证：Running/Idle/终态析构；exit 中 cancel/reset/tick 的明确拒绝；exit 计数恰为一次；
  正常 transition 和 fault 不重复退出。

### F8

**[P2，可观测性] Task worker / TaskGroup 吞掉 callable 异常，没有可查询的失败记录。**
Backlog：`TASK-FAILURE-REPORT-001`。

- 证据：[BoundedTaskSystem.cpp](../src/task/bounded/BoundedTaskSystem.cpp) 296–306 行、
  [TaskGroup.cpp](../src/task/TaskGroup.cpp) 50–60 行：catch 后继续清理计数，
  “later diagnostics”仅是注释，不是错误通道。
- 后果：用户任务部分执行后抛异常，外界只能看到 pending/idle；这些量只证明工作结束，不能证明业务成功。
  此问题不同于已关闭的 `TASK-GROUP-ALLOCATION-001` 包装分配问题。
- 修复：明确 owner 可消费的有界 error sink、失败计数或 per-operation 结果；worker 不向线程外抛异常。
  失败上报自身也应有不分配/不递归失败策略，并保持 capture 析构早于 completion。
- 验证：标准与非标准异常、多个任务混合成功/失败、队列满及 shutdown/retry；
  任务失败只报告一次，pending 最终归零，不能提前发布 idle。

## 4. 风险、验收缺口与文档漂移

### 不应冒充“已复现 bug”的项目

- **Editor 取消尾延迟**：`EditorSourceImportService.cpp:769–787` 的 `request_stop()+join()` 是同步等待，
  `EditorWorkspaceLifecycle.cpp:235–236` 在退出路径调用它。停止请求不会强制中断文件/codec 操作。
  当前未测得延迟或死锁；应先测大文件、慢盘和 cooking 中取消，必要时让 Cancelling 状态跨帧持有 owner。
  不采用 detach/杀线程来缩短退出。
- **JSON 宽对象复杂度**：`src/core/text/JsonDocument.cpp:275–285` 每插入一个 key 都线性查找已有字段，
  n 个不同 key 的比较次数可达 O(n²)。现有输入预算会限制上界，但不是性能结论；先记录规模/耗时/分配再考虑索引。
- **存档持久化边界**：`WriteFile.cpp:184–215` 做流 flush/close 与同目录 rename，
  没有显式文件/目录 fsync 或 FlushFileBuffers 协议。它能提供原子发布，不应许诺突然断电后最新存档必然存在；
  primary/backup 也不是跨进程锁或恶意篡改认证。详见 [Save](save.md)。
- **Navigation2D PMR**：既有 `NAV-GRID-PMR-001` 已跟踪 Debug STL 分配与 noexcept 的冲突，本轮不重复建项。
- **产品证据**：UIA、IME、Clipboard、Mobile gamepad、GPU/video、TLS 平台信任库、真实导入/对话框都需要对应环境；
  “有 API / 有测试源码 / 曾编译”不能相互替代。

### 本轮纠正的当前事实

- Task 零 worker 参数语义、失败上报与 Audio shutdown deadline 的文档已过期。
- 2D Navigation cook/physics bridge、Chain、FX document，3D postprocess/Jolt gameplay、AnimationGraph samples、
  HarfBuzz/FriBidi、移动手柄已有源码，不再列为“零实现”。
- Game API 可借用 Tina-owned `IRenderDevice`（ADR 0046）；禁止的是取得 backend/native owner 或擅自驱动 Host 帧生命周期。
- Catalog Runtime 是 `catalog.pck` 包内 Manifest/对象 + owning pin，不是旧散文件加载流程。
- design-freeze 补齐 Accepted 0053/0054/0058/0062/0063 和 Proposed 0050 的索引；
  **不改 ADR 历史理由，不把 Proposed 自动批准为 Accepted**。
- 动态测试计数/MemoryTag 数量不是设计原则；概览改为能力与边界，历史结果留在带日期证据中。

### 文档检查器自身的边界

本轮在未编辑前执行 `tools/docs/CheckDocs.ps1`，使用默认规范化仓库路径时得到
`errors=2 / warnings=4`：profiling 文档的不存在 preset 与断链，本轮修正文档；
四个 warning 来自检查器未识别 `tina_add_runtime_module` 定义的内部 target，并非 target 缺失。
显式传入带正斜杠的 Windows `-RepoRoot` 时，还会因未统一分隔符而把仓内链接误判为越界并跳过校验。
调用时先使用脚本默认根路径；解析器完善单独列入 `DOC-CHECK-PATH-001`，本轮不改脚本。

## 5. 后续落地与验收方式

### 第一批：可靠性

将 F1–F8 作为一个边界修复批次，不夹带重构整个引擎或新功能。Runtime/Audio/Asset owner 的终态规则
必须先一致，随后再做性能工作。修复时针对上述触发条件使用最小受影响 target 和直接 GoogleTest executable；
只有用户授权测试时才执行。未运行的用例保持“待验证”，不因修了代码就标 Done。

### 第二批：真实产品消费

- Editor：使用含中文路径、图片、音频、2D/3D 场景的真实工程，验证导入失败/取消、重试、dirty-close、
  保存/重开、Undo/Redo、Play/Stop、工程切换和退出，记录产物是否真的可重新读取。
- Save：一个游戏 owner 负责快照和 dataVersion，演示正常载入、主份损坏后的备份恢复、显式修复、迁移与异步繁忙反馈。
  不把 World2D snapshot、Editor authoring file 和 Save envelope 混成一个格式。
- AI/Navigation3D/Localization：先选一个已有游戏消费场景，证明预算、动态阻挡或语言回退在真实帧循环中可用，
  再判断是否确实需要更多 API。
- Editor 仍遵守大功能闭环后集中验证；这份建议不要求为每个小切片补测试，也不覆盖用户明确的 compile-only/手动测试选择。

### 第三批：发布与性能

记录 source/build-id、feature、toolchain、Debug/Release、backend/driver、字体/资源 hash 与命令；
发布 consumer 必须脱离 producer 源码树，确认 optional feature 的头安装和依赖闭包。
Windows/Linux/mobile/browser 按能力矩阵分别取证；普通 Linux Null gate 不代表所有独立模块 test executable 已运行。
测量优先关注帧尾延迟、音频 callback、导入取消、宽 JSON、UI dirty publication 和实际渲染负载，
开发机结果保持 provisional，不提前给未经测量的 FPS/延迟承诺。

### 本轮交付边界

仅 Markdown 文档变更；没有新启动的编译进程、产品窗口、agent、容器或临时 build tree。
本轮没有盘点或删除既有常驻构建资源，也不声称它们已被释放。
收尾检查：`git diff --check` 通过；14 份变更文档严格 UTF-8 解码通过，未检出替换字符；
文档链接/preset 检查 `errors=0 / warnings=4`，四项均为前述内部 target helper 的解析误报。
这些结果不计为 Unit/Smoke/Platform 通过，源码问题仍保持未修复、待运行复现。

## 6. 后续实施状态（同日追加）

maintainer 随后要求实际实施并从根本删除无必要的数量上限。本批已迁移 F1–F8 与 NAV-GRID-PMR-001 的
实现、公开契约、消费者和回归源码；0 初始预留/超预留增长、稳定 ID、稀疏 Blackboard 与非递归 Action 同批落地。
**F1 最终方案不是第 3 节的 clamp 建议**：IO16/CPU32 工厂 cap 没有硬件或协议依据，已直接删除。

SDK epoch 升为 0.4.0，旧 API 无兼容层，wire schema 不变；设计理由见 [ADR 0065](adr/0065-demand-grown-runtime-owners.md)。
本次续接的静态检查、编译、资源状态与未运行项统一保存在 [实施交接](capacity-and-lifetime-2026-09-13.md)，
不回写或抹除第 1–5 节的基线缺陷证据。Backlog 保持 InProgress，未运行测试不标 Done。
