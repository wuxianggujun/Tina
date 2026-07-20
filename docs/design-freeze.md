# vNext 设计冻结清单

> 状态：分片冻结。Backend 组合、Runtime/State、C++ 错误边界、M7 Input/UI 与
> Window Surface 所有权已经接受；固定 hard-gate machine profile 仍需建立。

Accepted 决定的理由与代价记录在 [ADR 索引](adr/README.md)，尚未关闭的工程风险记录在
[风险登记](risks.md)。本文是 Proposed/Accepted/Deferred 状态的权威汇总，Roadmap 只描述顺序。

## 已统一的设计

### 产品与技术边界

- Tina 是游戏优先的 2D/3D Runtime，不以编辑器优先；
- Windows/Linux，Visual Studio 2026 / MSVC 19.50 为 Windows 主门禁，目标 C++23 与全链路 UTF-8；
- GLFW + Windows IMM32，不使用 SDL/SDL3；
- bgfx 是首个且唯一真实 Render backend；Game SDK 与 Phase Context 不暴露
  `RenderDevice`/native handle/bgfx，Engine Module SPI 只可暴露纯 Tina Render 类型，任何已安装
  public header 都不出现 bgfx/GLFW/Win32 类型；普通游戏只调用 desktop bootstrap；
- EnTT 只作为 Scene 内部存储；
- Tina 自研 Retained UI，不使用 RmlUi/ImGui 作为游戏 UI；
- GoogleTest 1.17.0 直接运行，不使用 CTest 调度；
- Carbon 只读参考，不提交、不链接、不兼容其 API。

这些已经确认的边界由 ADR 固化；后续若要反转，必须新增替代 ADR，不能只修改主题文档：

| 主题 | ADR | 状态 |
| --- | --- | --- |
| 完整目标 + 垂直切片迁移 | [0001](adr/0001-vnext-vertical-slices.md) | Accepted |
| Tracy 定位 + 独立 benchmark 回归 | [0002](adr/0002-tracy-and-benchmark.md) | Accepted |
| GLFW/IMM32，不引入 SDL/SDL3 | [0005](adr/0005-glfw-without-sdl.md) | Accepted |
| 直接 GoogleTest，不使用 CTest | [0006](adr/0006-direct-googletest.md) | Accepted |
| 标准容器/pmr，不使用 EASTL；xxHash 私有 | [0007](adr/0007-standard-containers-and-hash.md) | Accepted |
| bgfx 唯一真实 Render backend | [0008](adr/0008-bgfx-render-backend.md) | Accepted |
| Runtime 只读 Cooked Asset，cgltf 只在 Cooker | [0009](adr/0009-cooked-assets-and-cgltf.md) | Accepted |
| Box2D/Jolt 分离 | [0010](adr/0010-separate-physics-backends.md) | Accepted |
| 自研 Retained UI + 后端无关 DisplayList | [0011](adr/0011-retained-ui.md) | Accepted |
| miniaudio 唯一真实 Audio backend | [0012](adr/0012-miniaudio-backend.md) | Accepted |
| EnTT 只作为 Scene 内部存储 | [0013](adr/0013-entt-internal-storage.md) | Accepted |

### 模块与所有权

- 目标模块为 core、platform、platform_glfw、task、runtime、scene、asset_format、asset、render、
  render_bgfx、ui、ui_freetype、audio、audio_miniaudio、profile_tracy、assetc、
  physics2d、bootstrap_desktop 和 samples/tests；
- `EngineHost` 是唯一非全局组合根；普通游戏通过 `tina_bootstrap_desktop` 的纯 Tina API 创建，
  高级测试通过 `Create(config, factories)` 注入；Null Runtime 不链接 GLFW/bgfx/miniaudio；
- 禁止 Singleton、Service Locator 和新的 `Application::instance()`；
- 初始化每成功一步登记逆序回滚，失败不得留下半初始化对象；
- Frame Phase、队列泵送、Scene/`IGameState` 结构变更和 shutdown 都有唯一提交点；
- `IGameApplication` 只创建 initial `IGameState` 和接收 shutdown，`IGameState` 是唯一帧行为入口。

### 性能与内存

- 中端桌面1080p，120 FPS设计目标、60 FPS硬门禁；
- 记录 phase p50/p95/p99、current/peak、allocation、queue depth 和资源计数；
- Fixed Update、Render Scene Extraction、无变化 UI 的 Tina-owned 稳态动态分配为0；进程/第三方 heap
  由平台工具另行交叉验证；
- EngineHost-owned MemorySystem，按 MemoryTag 提供 counting pmr resource；
- Scene command、Render extraction、UI DisplayList 使用独立 FrameArena；
- Arena 满不 heap fallback，reset 前必须通过 Task/consumer barrier；
- GPU/IO 跨帧数据使用自有 staging allocation。

### 容器与 Hash

- vNext 不依赖 EASTL/EABase；Legacy 迁移完成后删除；
- 不自研通用 STL；标准容器和字符串使用标准库/`std::pmr`；
- 只按真实消费者实现 StaticVector、InlineFunction、FrameArena、GenerationPool；
- SPSC Ring Queue 必须由 Audio/Upload profiling 证明需要；
- xxHash 私有保留，ContentHash、StringId、AssetId 和 generation ID 是不同强类型；
- 路径/对象相等不能只比较 Hash，xxHash 不承担安全签名。

### 性能分析工具

- Carbon Core 实际使用 Tracy，不存在已取证的 `TinyProfile` 模块；
- Tina 提供自有 `TINA_TRACE_*` 与 Metrics 前端，Tracy 0.13.1 作为首个可选 backend；
- Profile preset 使用 Release-equivalent optimization + symbols + Tracy，正式 `tina_bench` 默认
  关闭 Tracy；
- 只有 `tina_profile_tracy` 包含 Tracy inline header并链接唯一 Client；业务 TU 使用 Tina token
  adapter，禁止复用 Carbon 单文件强制 NDEBUG workaround；
- 不同时集成 MicroProfile/第二套插桩 profiler；只有 Tracy 无法满足已量化需求时才新建 ADR；
- profiler 用于定位，`tina_bench` 与固定门禁机才负责性能回归结论。

### Task 与线程

- CPU、阻塞 IO、Main completion 和 Audio callback 是不同执行域；
- 首期使用有界队列、TaskGroup、stop token、显式 barrier，不建设 DAG/fiber；
- Worker 不直接修改 World/UI/RenderDevice，结果由主线程唯一 phase 提交；
- 并行输出按稳定 chunk/index 合并，不以完成先后决定结果；
- 关闭只协作取消和 join，不强杀线程；
- 未证明共享队列是瓶颈前不实施 work stealing。

### Scene、Render、Asset 与 UI

- `EntityId/UINodeId/RenderHandle/AssetHandle` 都带 generation；`UINodeId` 在所有构建中编码并
  校验 owner `WindowId`，Debug cookie 只补充诊断；
- generation handle 同时受 owner registry 限制，Debug 使用 owner cookie 检测跨 World/Device；
- fixed update 中结构变更写 command buffer，每个 substep barrier 后稳定提交；
- `PlatformFrameView` 保留每窗口最终 `WindowInputSnapshot` 与有序 transition batch；
  UI 使用上一帧稳定布局逐 transition 路由，先于 Gameplay Action Mapping 产生
  `Tina::UI::InputTransitionConsumptionView` 与 `Tina::UI::ContinuousControlClaimsView`；被 UI 消费的 Down 在匹配
  release 前一直抑制 Gameplay held；axis claim 保持到 neutral 是完整 M7 analog 目标，M7-A 只实现
  digital suppression；
- 0 fixed-step 帧把有序 `SimulationActionTransitionBatch` 保留到下一个未完成
  simulation tick；Focus lost、断连与 overflow 生成 `InputCancelTransition`/`InputStreamReset`，不伪造可点击
  的普通 Up；
- M7-A 的 Action bindings 只由 `EngineConfig::inputActions.digitalBindings` 注册到 Runtime-owned
  default Context；raw/text/event 容量只来自 `platformFrameCapacities`，Simulation/Frame/binding
  容量只来自 `inputActions.capacities`，订阅 slot 来自 `platformEventSubscriptions`。M7-C1c-b3b 已加入
  独立 Runtime-private UI producer，M7-C1c-b3c 已让 `EngineHost` 在 Platform lifecycle dispatch 后惰性
  选择/持有首个 primary `WindowId` 的唯一 `UIContext`，调用 producer 后再进入 ActionMapper；绑定前 Headless
  帧使用 null Context，绑定后 primary 消失或 generation 更换会结构化失败。M7-C1c-b3e 已让 routed
  Pointer listener 请求当前 window/pointer 的 button claim；producer 只发布帧末仍 held 的 primary
  Pointer Button，并以 Create 期双 PMR buffer 去重。Key/Gamepad/axis claim producer 仍未实现，
  continuous claim 的 Runtime consumer 上限64不泄漏到 game-facing Action Map；
- World RenderScene 与 UI DisplayList 分别冻结，统一组合为 RenderFrame view；M8-B 已落地固定容量
  `RenderSceneBuilder`、phase-local `RenderSceneWriter`、resolved Camera2D/Sprite2D input、稳定排序/裁剪/
  pixel snap，以及 Runtime extraction 的 begin/commit/rollback。M9-A 在同一 builder 上补齐
  Perspective/Mesh3D extraction、当前 PlatformFrame aspect 注入、球体 frustum culling、稳定排序和相邻
  instance batch finalize；`tina_sample_3d_extraction` 只证明 CPU/Null 边界，不证明可见 bgfx 3D。
  M9-B/M9-C 当前分别只把 Opaque3D 与 Sprite2D 的内置 fixture 子集接入私有 bgfx backend；M9-C 固定
  View 0 clear、1 Opaque3D、2 Sprite2D、3 UI 只是 fixture view 编号，不是 Pass Scheduler，也不代表
  Asset/Texture/Sprite 产品路径、正式 `tina_sample_2d`、TileMap、Box2D 或中文文本已完成。M10-A0
  只补 Cooked wire format，M10-A1 只补 owning CatalogSnapshot 与 DAG cycle，均未把 fixture 替换为产品资产。
  `RenderFrame` 携带 submit-call-local borrowed
  `primaryWorldScene` 与 `primaryWindowUIDisplayList`，backend 只能在 `submitFrame()` 调用内消费/复制/编码并禁止
  保留；Runtime-private owning RenderFramePacket 继续作为后续目标，届时才持有 FrameArena/资源
  lease/Atlas/surface pin/submit ticket 到 backend completion；Renderer 不访问 EnTT；
- 目标 Pass 从 Opaque3D、Sprite2D、UI、Present 起步；当前 M9-C 的固定 bgfx View 顺序不能写成已完成
  Pass Scheduler；
- Asset 使用弱 Handle、强 Lease、UploadTicket/retirement ledger，以及 IO → CPU Decode → Main
  Completion → GPU Upload 四段路径和三重预算；
- Runtime 只读取 Cooked Asset，Cooker 先验证产物再原子写；M10-A0 已落地只依赖 Core 的
  `tina_asset_format`、16字节 `AssetId`/`ContentHash`、固定 v1 header/entry 与成功路径零分配的
  little-endian borrowed parser；M10-A1 已落地 `tina_asset` 的 owning 不可变 `CatalogSnapshot`、
  AssetId binary search 与完整 DAG cycle 校验，但尚未接入 Runtime 文件 IO、Asset registry/Handle/Lease
  或 Cooker writer；
- UI tree retained、UIContext 主线程唯一拥有，输出后端无关 DisplayList；细粒度 dirty、持久
  PaintCache 和稳定 paint/hit snapshot 保证无变化 UI 0布局、0 PaintCache rebuild、0 Tina heap allocation；
  dirty 分类包含 `Structure`，用于增删、重排、换父与 root attach/detach，并派生最小
  Measure/Order/HitTest/Semantics 失效集合。

M8-A 已把上述 Scene identity/transform 契约落成独立 `tina_scene`：`Scene::World` 在 Create 期为
entity slots、live registry、dense live index 与非递归 traversal/world scratch 分配固定容量 PMR storage，
所有 mutation、读查询和 publication 只在 owner thread，`EntityId` 校验 owner/generation，
`updateWorldTransforms()` 以两阶段 scratch 作为显式 publication barrier。`setParent()` 默认 KeepWorld、
KeepLocal 必须显式指定；`destroyEntity()` 默认提升直接子节点，`destroySubtree()` 才递归删除。溢出、
四元数异常和当前 TRS 无法表达的 shear 都返回诊断且不发布部分 snapshot。本轮暂不引入 EnTT/GLM，EnTT
仍只能作为后续 component storage 的 PRIVATE 实现；阶段 command buffer、Scene-owned Camera/Sprite
components、Asset 解析和 Runtime World capability 尚未接受为已实现能力。M8-B 的 RenderScene writer
只接受 Scene/Asset integration 已解析的值，不向 extraction context 暴露 World。

M7-C1b 已实现其中的 layout foundation：`UILayoutLength/UILayoutStyle/UIDirty`、固定容量 PMR
style/dirty side array 与 dirty queue、非递归 Flex-lite Measure/Arrange、双缓冲 committed
structure/layout snapshot，以及 `commitLayout()` 的结构+布局原子发布。width/height/min/max 是
border-box；Percent 使用 `0..100` 且相对 containing content box，默认 Auto root 以 viewport
content box 为确定基准。当前 dirty-subtree 切片又加入固定容量 layout work bits 与
prepared-input cache，clean subtree 可复用 Measure/Arrange 调度和既有几何结果；Auto 祖先、
Collapsed 子树、父约束/viewport 变化以及候选失败会回退完整布局。M7-C1c-a 又实现固定容量 PMR Pointer policy/route-ancestry scratch、
`Ignore`/`Targetable`、双缓冲 `UICommittedHitView` 和 structure/layout/paint-order/hit revision。hit entry
的 paint ordinal 在同一 view 内唯一且严格递增；hit-only commit 不执行布局，失败的 `commitLayout()` 保留旧
structure/layout/hit/paint snapshot。当前 `buildLayoutOrder`、父级 `arrangeChildren`、committed layout、
hit 与 paint snapshot 仍可能线性遍历；因此这只是 clean-subtree reuse，不是完整 dirty-range pruning；
`queryPointerHit()` 已在 M7-C1c-b1 按反向 paint order 实现无分配的 world/clip point query，并返回
route index/revision/visited count；M7-C1c-b2 已实现 fixed-capacity synthetic listener route，包括
generation-safe RAII token、48-byte fixed-inline `noexcept` callback、Capture→Target→Bubble、stop/consume、
route 中 add/reset/destroy 安全失效与 route/commit reentrancy guard。M7-C1c-b3a 已让 Platform Pointer
Button/Wheel transition 固化事件时 logical position，禁止 Runtime 用帧末 Pointer snapshot 猜测历史命中。
M7-C1c-b3b 已实现 Runtime-private `UIInputRouteProducer` 与独立 `tina_runtime_ui_tests`：只路由
Move/Button/Wheel，reset/cancel/非 Pointer 保留 raw ordinal hole；该 b3b 切片的 claims 当时恒为 canonical `None`。双预分配
PMR consumption bitset 在300帧共用 PMR 测试中 allocation count 不增长，supplied PMR 必须长于 producer。
失败测试先产生1次 listener side effect，后续 route path capacity 失败不发布旧 view 的替代结果，但推进
attempted watermark；同帧 retry 被拒且 callback 仍为1，证明 side effect 不回滚也不重放。
M7-C1c-b3c 已接入 Runtime-private primary-window owner 与正式帧路径：同一 `WindowId` 的 metrics、content
scale 或 minimized 变化复用 Context，绑定后 identity 消失/替换会失败；Context 在 Render → Task → Platform → Clock module
shutdown 前于 owner thread 销毁。该 owner 不调用 `commitLayout()`，route 只读取上一帧 committed snapshot。
M7-C1c-b3d1 已把 `UIContextCapacityConfig` 移入 focused public header，并通过
`EngineConfig::primaryWindowUICapacities` 在任何 backend factory 前共享校验；Runtime-private coordinator
在 `IGameState::updateUI()` 成功后、Render submit 前按主窗口 logical extent 对每个有效且严格递增的
`PlatformFrameId` 至多尝试一次 `commitLayout()`。Headless 窗口/Context 双缺席是成功 no-op；identity、
capacity 或 layout 失败会阻断 Render，并消费本帧 attempt，禁止同帧重放。b3d1 结束时 Game SDK
尚不能取得 Context 或创建 root，因此该阶段生产路径仍不会形成可见 UI；该阶段 claims 仍为 canonical `None`。
M7-C1c-b3d2 已实现
startup primary-window metrics seed 与 root-scoped、phase-epoch-scoped Game SDK capability：seed 不 poll、
不消费 frame id；Runtime 在 `onEnter` 前显式 bind primary `UIContext`，并在 State commit 前发布首份
structure/layout/hit/paint snapshot；`PrimaryWindowUIRootBuilder` 与 `PrimaryWindowUITreeUpdater` 只在 owner
thread/current phase epoch 内有效，回调结束后无条件失效，第一次 capability error 作为 sticky phase
error 合并。
后续兼容扩展已把 routed Pointer listener 注册加入低层 `UITreeUpdater` 与 Game SDK root-scoped facade：
注册动作仍只能发生在 current phase/current root subtree；只有返回的 move-only token 可跨 phase 保存，
且不延长 Context/root 生命周期。callback 最终 move/destructor 若重入释放 root/节点，注册事务在重新校验
generation/subtree 后原子回滚；若在该 callback operation 中销毁 Context，则触发生命周期 terminate。
M7-C1c-b3e 已实现第一条真实 claim producer：`UIRoutedPointerEvent::claimPointerButton()` 以固定 bitset
合并 callback 请求；Runtime 只把最终 Platform snapshot 中仍 held 的 primary Pointer Button 转为去重
`ContinuousControlClaimsView`。claim storage 在 Create 期双预分配，容量耗尽或后续 route 失败均不发布
staging 结果但推进 attempted watermark；Move/Wheel/Button route 都可显式接管仍 held 的按钮，
ActionMapper 复用既有 Cancel + suppress-until-Up 语义，同帧未 consume 的 Down 也会被 claim 拦截。
随后 `tina_ui` 已加入 `UIBoxPaint` 的可选 SolidFill、本地 premultiplied RGBA8 cache、固定
`paintSnapshotCapacity` 与双缓冲 `UICommittedPaintView`；成功 `commitLayout()` 事务发布
structure/layout/hit/paint 四份 snapshot，paint-only commit 不执行 layout 或 hit rebuild。`tina_render`
已加入 UI-independent、固定 PMR 容量的单帧 `UIDisplayListBuilder`，当前只发射 SolidQuad，支持
axis-aligned clip interning、相邻兼容 batching、paint checksum、剪枝和整帧 rollback。独立
`Tina::UIRenderIntegration` target 负责 logical→framebuffer outward rounding/clamp 与完整 builder transaction；
Windows MSVC 19.50 Debug/Release、Linux GCC 13.4 与 Linux Clang 22 sanitizer 的12项直接 GoogleTest
均通过，Clang 无诊断。后续 Button default action 切片已实现
`PrimaryPointerId + PointerButton::Primary` 的窄 pressed/activation、retained action property 与
cancel/reset 清理。持久 Pointer Capture、Focus/Modal、Button Keyboard/Gamepad activation、
Image/Text/Glyph PaintCache、Runtime `RenderFramePacket`、完整 dirty-range pruning 与 nested clip 尚未实现，
不能把最小 SolidFill 数据管线误写成完整 Widget UI。D0 后续已
把 committed paint 经 Runtime-private `PrimaryWindowUIDisplayCoordinator` 构建为 primary-window
`UIDisplayListView`，并以 `RenderFrame::primaryWindowUIDisplayList` 的 submit-call-local borrow
提交；Headless、0 framebuffer 与 suspended 发布空 list，失败不保留旧 publication 或截断 list。
随后 D1 已让私有 bgfx backend 消费 SolidQuad DisplayList，D2 已让
`PrimaryWindowUITreeUpdater::setBoxPaint()` 进入 Game SDK facade 并跑通 Desktop 4-panel visible sample。
该结果仍不等价于 owning RenderFramePacket、FramePin、Text/Glyph、Label 文本、Button
Keyboard/Gamepad activation、Image/Texture 或完整 Widget UI。

### 实施与验证

- 设计冻结后创建独立 `codex/` 分支和 worktree，不复制/stash/提交主工作区差异；
- 双架构迁移期 `TINA_BUILD_LEGACY=ON` 保留当前游戏，并有 vNext-only OFF preset；覆盖完整
  2D/UI/3D/Audio 门禁后翻为 OFF，最终删除选项和旧实现；
- 垂直切片顺序为 Null Runtime → Platform/最小 Surface/UI → Scene/2D → Render/3D → Asset/Cooker →
  Product 2D/UI/Audio → Legacy 删除；
- M7–M9 只使用版本化内置 Cooked fixture/procedural geometry；M7 已分片建立私有最小 bgfx UI Pass
  与 SolidFill 可见样例，M9-A 先完成 backend-neutral 3D extraction，M9-B 再接入私有 procedural Cube/depth，
  M9-C 再接入私有 Sprite2D fixture pass 与 `tina_sample_2d_infrastructure_bgfx`，
  禁止 UI 临时调用 Legacy renderer；
- 每批都有代码、直接 GoogleTest、对应可运行样例、资源回收证据、UTF-8 文档和独立提交；
- `tina_bench` Release 直接运行，普通 CI 不用不稳定的绝对微秒阈值；
- Exit code、资源计数、性能数据和实际画面分别验收。

## 冻结状态（P0）

状态语义：Accepted 可以进入实现；Proposed 是当前推荐但尚未得到本轮最终确认，涉及它的
切片不得先写代码；Deferred 不进入首期。P1 参数不阻塞完整架构冻结，但必须在首个消费者
编码前量化并记录。实现中发现设计不成立时暂停对应切片，新建 ADR 后再继续，禁止让代码
反向悄悄决定公共契约。

| 决策 | 状态 | 当前决定 | 影响 |
| --- | --- | --- | --- |
| Backend 组合 | [Accepted](adr/0003-backend-factories.md) | `Create(config, factories)`；M7-B1 已使用 Independent 或 WindowSurface 的 tagged composition，M7-B2 在该组合上接入私有 bgfx，bootstrap 只选 factory、EngineHost 唯一创建/回滚 owner | 消除 Runtime 对具体 backend 依赖与 lease 接线歧义 |
| Runtime/State | [Accepted](adr/0014-runtime-phase-and-state.md) | `IGameApplication` lifecycle-only + `IGameState` 唯一帧入口；Frame Update 后提交状态命令 | 消除名称歧义、双帧入口与首帧 UI 时序冲突 |
| RenderFrame/Surface 所有权 | [Accepted](adr/0020-window-surface-handoff.md) | bgfx backend 在 factory 成功后持有 move-only window surface lease；后续 Runtime packet 切片按契约持有 owning RenderFramePacket，Render SPI 只暴露纯 Tina view/pin sink | 消除 native 泄漏、依赖环、backend 越界与在途 UAF |
| UI 增量管线 | [Accepted](adr/0011-retained-ui.md) | M7-C1b 至 C1c-b3e 已完成 Flex-lite layout、committed hit/query/route、Runtime owner/layout/startup capability 与 held primary Pointer Button claim；后续又完成 SolidFill-only committed paint、Render-owned 单帧 SolidQuad DisplayList builder 及独立 UI→Render integration bridge。D0 已把 primary-window DisplayList 作为 `RenderFrame` submit-call-local borrow 接入 Runtime 正式帧，backend 禁止保留；Headless、0 framebuffer 与 suspended 发布空 list，失败不保留旧/截断 publication。D1 已让私有 bgfx SolidQuad pass 消费该 DisplayList；D2 已通过 Game SDK `setBoxPaint()` 和 Desktop 4-panel visible sample 验证 SolidFill 可见路径。后续 Button default action 切片已实现 `PrimaryPointerId + PointerButton::Primary` 的 Button armed/pressed/Up activation、`preventDefaultAction()`、retained action property、固定容量事务 slot 与 cancel/reset 清理；Game SDK 取得 root-scoped set/clear/query facade，producer 在 ActionMapper 前合并 default action 与 listener 的 consumption/claim。当前 b4a 又完成 clean-subtree Measure/Arrange reuse、prepared-input cache 与父约束/viewport/Collapsed/候选失败回退；Windows Debug/Release `tina_ui_tests` 均为115/115。`commitLayout()` 成功时原子发布 structure/layout/hit/paint 四份 snapshot；bridge 的 Windows MSVC 19.50 Debug/Release、Linux GCC/Clang sanitizer 直接 GoogleTest 均为12/12。D1/D2 Windows Debug/Release 已通过 Runtime→UI53/53、bgfx16/16、bridge12/12和 Desktop visible smoke Debug1200/Release300；本轮 M8-A 重新运行了 Windows Null/UI/Runtime→UI/UI→Render/Scene Debug/Release，Linux D1/D2 尚未重跑。完整 dirty-range pruning、Key/Gamepad/axis claims、Pointer Capture/Focus/Modal、Button Keyboard/Gamepad activation、Disabled/theme 视觉、Image/Text/Glyph PaintCache、owning Runtime packet/FramePin 与 nested clip 后置 | 高性能且保持命中/透明顺序，同时让 UI/Render 双方无反向依赖 |
| UI startup/root capability | [Accepted](adr/0021-runtime-ui-startup-capability.md) | Platform backend 提供不 poll、不消费 frame id 的 `initialPrimaryWindowMetrics` seed；Runtime 在 `onEnter` 前创建 primary Context 并提交首份 structure/layout/hit/paint snapshot。Game SDK 只取得 root-scoped、phase-epoch-scoped 的 `PrimaryWindowUIRootBuilder`/`PrimaryWindowUITreeUpdater`；facade 通过 owner thread、phase epoch、root owner 与 subtree containment 校验，回调结束后无条件失效，第一次 capability error 作为 sticky phase error 合并；不暴露裸 `UIContext*`。D2 后续扩展 `setBoxPaint()`；本轮后续扩展 `addRoutedPointerListener()`，其 token 是唯一可跨 phase 保存的 listener 对象但不保活 Context/root，State 在 `onExit()` 先 reset token 再释放 root | 让 ADR 0014 的 initial UI transaction 可落地，同时避免丢失首帧输入或扩大 Game SDK owner 权限 |
| Frame/Input | [Accepted](adr/0015-input-and-fixed-step.md) | 保序 PlatformFrame、UI transition consumption + continuous claims；Action 分 Simulation/Frame domain；每个 substep 独立 commit | 防输入穿透、0步帧丢边沿、双重执行和追赶步错误 |
| C++ exception | [Accepted](adr/0004-exceptions-and-errors.md) | 编译开启；公共 API 用 Result/Status，Engine/Frame/Worker/C callback 边界捕获 | pmr/第三方兼容、错误边界 |
| Generation | [Accepted](adr/0019-generation-handles.md) | 32位 generation，回绕 retire；UINodeId 所有构建编码 owner WindowId，Debug cookie 只诊断 | stale/跨 registry 安全 |
| Asset 生命周期 | [Proposed](adr/0016-asset-ownership-and-retirement.md) | 弱 Handle、强 Lease、UploadTicket 和 retirement ledger | GPU/Audio 异步 UAF 防护 |
| ContentHash | [Accepted](adr/0007-standard-containers-and-hash.md) | 版本化 XXH3-128；M10-A2a 契约落地 Core 私有 adapter 与 Cooked payload 可选校验；安全校验未来另选密码学 Hash | Cooker cache 与格式稳定性 |
| Worker 默认 | [Proposed](adr/0017-bounded-task-system.md) | 交互运行默认 `max(1, hardware_threads-1)`；IO 默认1；benchmark 必须显式 worker 数 | 可复现性与扩展性 |
| Trace backend | [Accepted](adr/0002-tracy-and-benchmark.md) | `none|tracy` 编译期 backend，唯一 config/client；Profile 与 Bench 优化语义一致 | 可观测性与依赖边界 |
| Benchmark 协议 | [Proposed](adr/0018-benchmark-protocol.md) | schema v1、workload version/checksum、5进程/10k正式p99、nearest-rank、median+MAD | 可重复回归结论 |
| 固定 hard-gate 机器 | [Proposed](adr/0018-benchmark-protocol.md) | 当前开发机只做 provisional baseline；需创建稳定 machine profile 后才阻断绝对耗时 | 绝对 p99 是否可信 |
| Cooked 布局 | [Accepted](adr/0009-cooked-assets-and-cgltf.md) | M10-A0 已实现每 Asset 独立文件/Manifest 的 v1 wire schema 与只读校验；M10-A1 已实现 owning CatalogSnapshot 与完整 DAG cycle；事务 writer/publish、AssetSystem 状态机、cgltf 产品转换与 Bundle/Patch 仍后置 | Cooker/Runtime 一致性 |

当前仍需在相应切片开始前确认的是 Asset 强弱所有权、Worker 默认值和
benchmark/hard-gate 规则；Backend、Runtime/State、C++ 错误边界、GLFW、Input、UI、
Window Surface、Tracy、EASTL、bgfx、Cooked/cgltf 与测试调度不再重复摇摆。

## P1：垂直切片内确认

- 后续非 Input 消费者的 StaticVector 容量和哪些溢出可安全丢弃；M7-A Input 容量已在主题文档冻结；
- Fixed Simulation 首批并行系统和 chunk size；
- FrameArena、Task callable inline、CPU/IO/Main completion queue 默认容量与线程栈预算；
- RenderScene、UI DisplayList 与 upload staging 的默认字节预算；
- 每个 benchmark 的 absolute noise floor、MAD 稳定阈值和 hard-gate machine profile；
- Audio voice/command/completion/stream buffer 容量与 callback period 门禁；
- 完整 GLFW/bgfx Linux 产品图的 X11/Wayland 选择与 sanitizer 复验；
- UI Screenshot 的参考 GPU、字体和允许像素差。

这些不阻塞完整目标架构冻结，但必须在对应切片编码前确认并写入 ADR 或主题文档。

## 明确后置

- PBR、阴影、动画、后处理；
- 脚本、编辑器、热重载与在线源资产解析；
- 完整 Render Graph、多真实渲染后端；
- 通用 Task DAG、fiber、work stealing；
- 通用自研 HashMap/String/Vector/SharedPtr；
- 完整 crash upload、复杂 histogram、通用内存池；
- 复杂文本 shaping、多行 TextEdit、完整 Linux IME preedit；
- Bundle/Patch、资产远程分发；
- Jolt 3D 物理，直到真实3D玩法需要；Box2D/Jolt 不统一 API。

## 已完成的第一切片与下一项实施

第一切片 M6-A 实现 `tina_core + tina_platform(headless) + tina_task(DisabledTaskSystem) +
tina_runtime + tina_render/NullRenderDevice + tina_tests + tina_sample_null`。它只固定
`EngineHost::Create(config, factories)`、单个 initial `IGameState`、阶段错误边界、初始化逆序回滚、
关闭顺序，以及 request-exit-after-frame 的完整帧语义。

M6-A 不建立无真实消费者的 `scene/asset/ui/audio` 空壳，不实现 Worker、Pass Scheduler、完整
GameStateStack/Commands、Asset、UI、Audio、Tracy 或 benchmark 协议。它完成300帧与10,000帧
Null 运行，且不链接 GLFW/bgfx/EnTT/FreeType/miniaudio/Tracy/cgltf。后续切片必须先接受自己的
P0/P1 决定，再加入对应模块和 benchmark；这样每个提交都可独立定位和回滚。

M7 不作为一个巨型提交：M7-A 先分为已完成的 PlatformFrame/Input correctness、Headless backend、
PlatformFrameBuilder 直接注入和 Runtime test adapter，以及紧随其后的私有 GLFW `NO_API` 窗口 +
NullRender 子切片；可复用 production-like PlatformBackend test double 随 GLFW adapter 测试加入；M7-B1 已实现
Native Window Surface lease、surface snapshot、WindowSurface-aware composition 与 NullRender suspended 语义；
M7-B2 已实现私有 bgfx clear-only core、Desktop bootstrap 与真实 GPU 门禁；M7-C1a 已实现
`tina_ui` tree core，M7-C1b 已实现 layout/dirty API、固定容量 PMR 存储、Flex-lite 非递归
Measure/Arrange 与 committed structure+layout 原子发布；M7-C1c-a 已实现 committed hit snapshot 数据基础；
M7-C1c-b1 已实现 point query 与反向目标选择；M7-C1c-b2 已实现 synthetic Capture→Target→Bubble listener route；
M7-C1c-b3b 已实现独立 Runtime-private producer，M7-C1c-b3c 已完成 EngineHost primary-window `UIContext`
owner/selection 与 producer 接线，M7-C1c-b3d1 已完成 UI capacities 与 Runtime-private layout coordinator。
M7-C1c-b3d2 已按 ADR 0021 补齐 startup metrics seed 与 root-scoped、phase-epoch-scoped Game SDK access；
M7-C1c-b3e 已补 primary Pointer Button claim。SolidFill committed paint、Render 单帧 SolidQuad DisplayList
builder 与独立 UI→Render bridge 也已实现并在 Windows MSVC 19.50 Debug/Release、Linux GCC/Clang
sanitizer 通过 bridge 12/12。D0 又把 Runtime-private primary-window DisplayList 作为 `RenderFrame`
submit-call-local borrowed view 接入正式 submit 路径；D1/D2 已补私有 bgfx SolidQuad UI pass、
Game SDK `setBoxPaint()` facade 与 Desktop 4-panel visible sample。
后续独立切片已补 `PrimaryWindowUITreeUpdater::addRoutedPointerListener()`、root/subtree 原子注册、
callback move 重入回滚与 EngineHost claim-before-ActionMapper 闭环；当前 b4a 又补上 clean-subtree
Measure/Arrange reuse、prepared-input cache 与父约束/viewport/Collapsed/候选失败回退，Windows Debug/Release
`tina_ui_tests` 均为115/115；该 b4a 切片当时已重跑 Windows Release，未重跑 Linux/bgfx/可见样例。
后续 M8-B 已另行复验 Windows Debug bgfx/desktop 生命周期，但没有新增截图或 Linux 结果。
之后继续实现 Key/Gamepad/axis claims、focus/capture/widget、Button Keyboard/Gamepad activation、完整 dirty-range pruning、
owning Runtime RenderFramePacket 与资源 pin，M7-D 实现 Label/Button/Modal + FreeType 文本/中文样例；
M7-E 最后接入 IMM32、Gamepad 和完整 DPI/输入门禁。

M9-A 已完成 RenderScene 的 3D CPU/Null extraction foundation：Perspective Camera、Mesh3D item/batch、
当前帧 aspect、包围球裁剪、稳定排序和 instance batch finalize；该切片不创建 bgfx Buffer/Shader/Pipeline，
不显示 Cube。M9-B 当前最小实现已建立私有 canonical `P3_N3_UV2` Cube、Opaque depth pass、真实
transient instance buffer、Opaque3D 后覆盖的 retained UI 与可见300帧样例；它仍仅接受 fixture key，
不代表通用 Mesh/Material/Pipeline 或正式3D产品路径。M9-C 又建立私有 Sprite2D fixture pass、固定
View 0/1/2/3 顺序和 `tina_sample_2d_infrastructure_bgfx` 2D/UI 300帧样例；Debug/Release
`tina_render_bgfx_tests` 均43/43，两配置样例均为5个 Sprite、2个 UI panel且资源账本平衡，Debug 截图确认旋转、透明、
flip 与 UI overlay。它仍是 fixture/infrastructure，不代表 Asset/Texture/Sprite 产品路径、正式
`tina_sample_2d`、TileMap、Box2D或中文文本。M10-A0 已另行完成 Cooked Header/Manifest
wire-format 基础；M10-A1 已完成 owning CatalogSnapshot 与完整 DAG cycle；后续 M10 切片仍负责
AssetSystem 状态机/Handle/Lease、Cooker/cgltf 和正式资产产品路径。

M10-A0 新增 `Tina::AssetFormat`：Cooked Header 112B、Manifest Header 64B、Manifest Entry 56B、
Dependency Entry 24B，全部显式 little-endian decode；Manifest entry 与 dependency subrange 使用稳定
AssetId 排序，依赖目标和 kind 在发布 view 前验证。object 路径由 kind/AssetId 确定派生，不把路径当身份。
该切片明确不实现完整 DAG cycle、XXH3 计算、AssetHandle/Lease、Task/IO、GPU upload、cgltf 或 writer。

M10-A1 新增 `Tina::Asset`：`CatalogSnapshot` 在注入 PMR 上 owning 复制 entry/dependency，Create 后
不依赖 Manifest bytes；`find` 为 binary search；依赖边解析为稳定 entry index；完整 cycle 用迭代着色 +
显式 stack，`O(V + E)`，禁止递归。失败不发布部分 Snapshot。该切片明确不实现 Handle/Lease、registry
状态机、文件 IO、Task、GPU upload、XXH3、cgltf 或 writer；ADR 0016 仍为 Proposed。

M10-A2a 已落地 Core 私有 XXH3-128 v1 ContentHash digest：seed=0、16 字节 little-endian 输出、公共头
无 xxHash 类型；`verifyCookedAssetContentHash` 只校验 payload。该切片不实现文件 IO、Handle/Lease、
Catalog 磁盘加载、Cooker 或密码学签名。

M10-A2b 已落地有界 Catalog 文件加载：Core `readFile` + Asset `loadCatalogSnapshotFromManifestFile`。
该切片不实现 Handle/Lease、async IO、Cooker writer 或 GPU upload。

M10-A2c 已落地 owning Cooked object 文件加载与 Catalog 路径解析校验；不实现 Handle/Lease。

M10-A2d 已落地 Catalog 依赖展开与 dependencies-first 确定性加载序；M10-A2e 已落地按该顺序的
批量同步 Cooked 文件加载，任一失败销毁已加载对象且不发布部分批。两者都不实现 Handle/Lease、
异步 IO 或 Task worker。

M10-A2f 已落地 `validateCatalogPackageOnDisk`：严格 UTF-8 Catalog root、确定性 object path、
metadata-only 常规文件/精确大小校验，以及逐文件 full parse + 强制 ContentHash + Catalog entry 对齐。
校验不枚举目录、不保留对象且同时最多持有一个 Cooked 文件；不实现 Handle/Lease、CLI 或 Cooker。
