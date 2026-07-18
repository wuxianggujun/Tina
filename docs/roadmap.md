# Roadmap

## M0 可构建基线（已完成）

- 固定 GLFW、miniaudio、vcpkg、bgfx 与 Core 基线；
- 建立 Windows/Linux Preset、独立 GoogleTest 可执行文件和可回滚提交；
- 明确不使用 SDL/SDL3 和 CTest 调度。

## M1 构建与生命周期（已完成首轮）

- GoogleTest 独立测试程序迁移（已完成，直接运行 `tina_tests`）；
- 修复 Application 初始化、Frame Phase、Event/Resource pump 和 shutdown；
- 通过 2D/3D 退出冒烟验证资源释放；自动化资源计数仍在后续测试门禁中。

## M2 2D + 自研 UI（已完成首轮）

- 统一每个 Scene 的 UI 树所有权，并由 EventSystem 对当前 roots 做单次输入路由；
- 修复布局注册、显式事件上下文注入、hit-test 隐式布局和单次输入路由；
- 引入 generation NodeId，统一 Pointer Capture、Focus/Tab 和节点销毁安全失效；
- 引入每窗口 UIContext、Dark/Light/Custom Theme、DPI/content scale 和用户缩放，修正高 DPI 命中与场景布局；
- 增加嵌套 Clip、通用 ScrollView、ListView 可见行虚拟化和 Windows 原生 IME composition；
- 增加焦点 KeyDown/KeyUp routed event、可取消默认行为、Button Enter/Space pressed 生命周期、方向键空间导航和焦点视觉；
- 增加可嵌套 Modal Focus Scope、generation 失效与焦点恢复，移除 UIDialog 的全局键盘旁路，并修正 Scene roots 在 `onEnter`/`onResume` 前的激活时机；
- 增加 GLFW 标准手柄状态、摇杆回滞、方向长按重复和设备无关 Accept/Cancel 导航，且语义导航服从 Modal Focus Scope；
- 增加 UI 布局、唯一命中、路由顺序、动态子树上下文和交互生命周期 GoogleTest；
- 增加 `--smoke-ui`，实际运行虚拟列表、对话框、中文和已聚焦 TextEdit。

## M3 最小 3D（已完成）

- 增加 Perspective Camera、深度缓冲和静态 Mesh；
- 建立独立 `--smoke-3d` 运行入口，不影响现有 2D 游戏；
- 验证退出时 GPU 资源全部释放。

## M4 UI 可靠性（已完成首轮，暂停横向扩展）

- 完成 Button action 的独立重入保护、异常恢复和回调销毁目标安全门禁；
- Retained Tree、generation NodeId、路由、Focus/Capture、Modal、Theme/DPI、Scroll/List、
  TextEdit/IME 与 GLFW 手柄导航已有可运行基线；
- Checkbox、Slider、可注入手柄轮询测试、可访问语义和截图回归移到 M11，不继续用控件
  数量挤占 Runtime/Render/Asset 生命周期工作。

## M5 vNext 设计审计、候选冻结与 Carbon Core（已完成）

- 官方 Carbon 对应模块已下载到被忽略的 `temp/carbon-engine`，并记录 URL、精确提交和
  研究用途；参考源码不进入 Tina 依赖或提交；
- 已完成 Runtime、Window/Input/UI、Render、Asset/Cooker、Scheduler、Simulation、
  Audio 和 Core 的采纳/拒绝矩阵；
- 已形成 core、platform/platform_glfw、task、runtime、scene、asset_format/asset、render/
  render_bgfx、ui/ui_freetype、audio/audio_miniaudio、profile_tracy、assetc、bootstrap_desktop
  的候选职责和单向
  依赖，等待本轮确认；
- 已形成 factory 注入、阶段 Context、`IGameApplication`/`IGameState`/Runtime-private GameStateStack、Frame Pipeline、generation + owner、
  Render Scene Extraction、Asset Handle/Lease/Ticket 和 UI Display List 的候选契约；
- 决定 Core diagnostics 的最小范围：Metrics、TraceZone 空后端、MemoryTag、可注入 Clock、
  CrashContext 和 UTF-8 原子 IO；不复制 Carbon 的全局 allocator/profiler/thread API；
- vNext 不依赖 EASTL，也不自研通用 STL；只实现 StaticVector、InlineFunction、FrameArena、
  GenerationPool 等有真实消费者的专用结构；xxHash 作为私有 Hash backend 保留；
- 采用中端桌面1080p、120 FPS设计目标/60 FPS硬门禁；直接 GoogleTest 检查确定性契约，
  独立 Release `tina_bench` 记录 phase p50/p95/p99 与内存/分配；
- 采用 Tina-owned Trace/Metrics 前端与可选 Tracy 0.13.1 Profile backend；benchmark 默认
  关闭 profiler，Profile preset 用相同 workload 定位回退，不同时引入第二套 profiler；
- 已形成 MemorySystem tag、FrameArena reset 点、跨模块资源矩阵，以及 CPU/IO/Main executor、
  TaskGroup、背压、barrier 和无强杀 shutdown 候选状态机；
- 补齐 Platform/Input、Audio、公共 API、依赖治理、风险登记与 ADR；性能基准冻结 schema、
  workload version/checksum、独立进程统计、baseline fingerprint 和 Tracy/Metrics A/B；
- 设计冻结前只更新文档和取证，不修改 Runtime 源码；冻结后在独立 `codex/` 迁移分支按可回滚批次实施。

## M6 Null Runtime 与公共入口垂直切片

实施状态（2026-07-17）：M6-A 生命周期内核已完成。Tina 自有 target 已统一 C++23；
`tina_core`、`tina_platform`、`tina_task`、`tina_render`、`tina_runtime` 五个 target 与
`tina_sample_null` 已加入 vNext-only 构建图。当时 M6-A 基线在 Windows MSVC 2026 Debug/Release、Linux GCC 13.4
与 Clang 22.1.8 + libstdc++15 ASan/UBSan 直接 GoogleTest 均为92/92；Null sample 连续运行
300帧和10,000帧并返回0。M6-A 没有加入或
链接 GLFW、bgfx、EnTT、FreeType、miniaudio、SDL/SDL3。Legacy ON Debug 共存图已构建并直接
通过135/135；菜单、2D/UI、3D 四条 Legacy Debug 产品路径均完成300帧并正常回收资源。M6
仍不因生命周期内核通过而整体完成。

M6-A 已完成：

- `EngineHost::Create(config, factories)`、当前最小 `EngineConfig`、阶段 Context、lifecycle-only
  `IGameApplication` 与单个 `IGameState`；
- Clock → Platform → Task → Render 创建、任意 factory failure/null/throw 的逆序回滚、Ready Host
  析构、run-once、幂等 shutdown 与提交前/后的游戏回调清理语义；
- 可注入 Clock、固定60 Hz/最多4步、0/1/4步时序，以及 Poll → Fixed → Frame → Extraction → UI
  → Null submit/present 的 M6-A Frame Pipeline；
- Headless Platform、Disabled TaskSystem 与生命周期级 NullRenderDevice；Null backend 强制
  submit/present 配对和连续 frame index，300帧资源计数保持为0；
- C++23 Core 的 `std::expected` Result/Status、稳定 Error、FixedStepAccumulator、MemoryTag、
  并发计数 PMR、无回退 FrameArena 与 owner-aware GenerationPool；
- `windows-vnext-debug`/`windows-vnext-release` build preset、直接 `tina_tests` 与
  `tina_sample_null --frames=N` 门禁。

M6 生命周期之后仍未实现：

- 完整 GameStateStack/commands/policy propagation、通用 Runtime Event Queue 与 State TaskGroup；
- M7-A 已补齐有界 PlatformFrame/Input/Action、Platform lifecycle dispatch，以及私有 GLFW
  Window/Keyboard/Pointer/committed text desktop adapter；M7-C1c-b3b 已补独立 Runtime-private UI routed
  consumption producer，M7-C1c-b3c 已补 primary-window `UIContext` ownership/selection 与 EngineHost
  接线；EngineConfig UI capacities、Game SDK scoped UI access、真实 continuous claims、IMM32、production
  Gamepad 和连续 axis mapping 仍在后续子切片；
- 有界 CPU/IO/Main worker、阶段指标与完整 shutdown deadline/fatal-stop；
- typed render resource handle、Pass Scheduler、World RenderScene/UIDisplayList、Runtime-private
  RenderFramePacket/pool 与 submission completion 保活；
- Scene、Asset、Audio 的真实契约和消费者，以及 Runtime-integrated UI root/layout/render
  pipeline；M7-C1b/C1c-a/C1c-b1/C1c-b2 C++23 standalone `tina_ui` tree/layout/committed-hit/
  point-query/synthetic-route foundation、M7-C1c-b3b 私有 producer 与 M7-C1c-b3c EngineHost 接线已实现，
  但 Game SDK 尚不能创建 root，仍没有可见 UI；
- `tina_bench` schema v1、Bench/Profile preset、`tina_profile_tracy` 和 Tracy/Metrics A/B；
- Linux Null 图已完成 GCC 13.4 与 Clang 22.1.8 + libstdc++15 ASan/UBSan 门禁；M7-B2 Desktop bgfx
  X11 图也已完成 GCC 13.4 和 Clang 22.1.8 + ASan/UBSan/LSan 的183/22/11直接测试及300帧门禁。
  Clang WSL2 使用 Vulkan/llvmpipe，只证明软件 Vulkan/backend 生命周期；Scene/UI/Asset/Audio 等
  完整产品图仍必须在对应切片重新验证。

## M7 Platform、最小 Surface 与高性能 UI 垂直切片

M7 分为五组可回滚垂直切片，组内继续按 C1a/C1b 等边界拆分提交；不将 GLFW、bgfx、FreeType、
IMM32 和完整 UI 同时并入。

### M7-A PlatformFrame 与 Input correctness

实施状态（2026-07-17）：Headless Platform/Input 内核与私有 GLFW `NO_API` desktop adapter 已按
两个独立提交落地；随着 M7-B1 WindowSurface 覆盖加入，基础测试固定183项，GLFW adapter 专项固定22项，必须作为两个 executable
分别直接运行；Null sample 继续覆盖300帧和10,000帧，`tina_sample_platform` 覆盖真实窗口300帧。
Linux GCC 13.4 X11 已通过183/183、22/22，Null/GLFW样例各300帧。Clang X11 对libX11
`_XimOpenIM` 的XIM retention使用唯一精确 LSan suppression 后，基础183/183（无 suppression）、
专项22/22（12次/4896 B），Null/GLFW样例各300帧且GLFW样例命中1次/408 B。Windows MSVC 2026
Debug/Release也均通过183/183、22/22与Null/GLFW样例各300帧。GCC 13 X11/Wayland 双后端
产物也已通过：带 `wl_seat` 的嵌套 Weston 9 中基础183/183、专项22/22与样例300帧通过，并在移除
`DISPLAY` 后断言 GLFW 实际选择 Wayland；同一产物强制 X11 后专项与样例复验通过。
纯 Weston headless 无 `wl_seat` 会触发锁定 GLFW 3.4 的已知初始化崩溃，不是 Tina 回归，
也不在当前支持范围。Clang 22 X11/Wayland 双后端 sanitizer 产物也已通过：基础
183/183无 suppression且Null样例300帧通过；Wayland 下专项22/22与样例300帧通过且
`_XimOpenIM` 匹配为0；同一产物强制 X11 后22/22与300帧通过，仅使用精确 `_XimOpenIM`
抑制（专项12次/4896 B、样例1次/408 B）。该门禁未对 vcpkg 第三方 GLFW 本身插桩，不宣称完整
覆盖 GLFW 内部实现。

- **已完成**：`tina_platform` 实现 generation `WindowId`、`PrimaryWindowConfig`、`WindowMetricsSnapshot`、
  `PlatformFrameView`、`WindowInputSnapshot`、有序 `InputTransitionBatch` 与 `PlatformEventBatch`；
- **已完成**：`PlatformFrameBuilder` 单测直接注入 Down→Up、Focus Cancel、overflow reset 与
  lifecycle payload；Runtime test adapter 验证 EngineHost wiring。Headless 仍不链接 GLFW；可复用
  production-like deterministic PlatformBackend test double 随 GLFW adapter 测试加入；
- **已完成**：Runtime 建立 UI consumption seam，并作为 ActionMapper consumer 接受
  `Tina::UI::InputTransitionConsumptionView` 与 `Tina::UI::ContinuousControlClaimsView`、
  digital Action Map 和有序 Simulation Action latch，验证0/1/4 fixed-step 只消费一次；
- **已完成**：`EngineConfig::inputActions` 注册唯一 Engine default Input Context 的 digital bindings；
  raw/event/text、action/binding 与 subscription 分别由职责明确的配置块一次性分配。UI claim
  在 M7-C1c-b3b producer 中仍恒为 canonical `None`，内部固定上限64；Escape 不走 backend shortcut；
- **已完成**：Runtime 建立只承载 resize/focus 等平台生命周期的有界 private `PlatformEventDispatcher`；Game SDK
  只暴露 `PlatformEventSubscriptions` 与 RAII subscription；
  OS CloseRequested 只走 control outcome，不进入队列；这不是通用 Gameplay EventBus；
- **已完成**：私有 `tina_platform_glfw` 使用 hidden `GLFW_NO_API` create transaction、generation
  Window registry、单进程 backend lease和 owner-thread约束，实现 create/destroy/poll、close/focus/
  resize、键盘、Primary Pointer 与 committed UTF-8 text；callback 只写有界 buffer/sticky failure；
- **已完成**：`tina_sample_platform` 用 GLFW Window + NullRender；Escape 通过 Frame Action 请求完成
  当前帧后退出，`--frames=N --frame-delay-ms=0` 可自动退出；GLFW失败不静默降级 Headless；
- **已完成**：公共 factory header 不含 GLFW/native 类型，Null 构建图不加入或链接 GLFW；
  `tina_platform_glfw_tests` 与基础 `tina_tests` 分离，不使用 CTest；
- 本切片当时不实现 WindowSurface handoff 或真实 bgfx、UI tree、FreeType、IMM32、production GLFW Gamepad adapter/
  registry/navigation、OS Pointer Capture、通用 Gameplay EventBus 或多窗口。WindowSurface handoff 已由
  M7-B1 完成；M7-B2 bgfx core、Desktop 产品接线与真实 GPU 冒烟已完成。

### M7-B Native Window Surface 与最小 bgfx

实施状态（2026-07-17）：M7-B1 private WindowSurface handoff 与 M7-B2 private bgfx clear-only core、
Desktop bootstrap、真实 GPU 冒烟已完成。`Tina::Desktop::CreateEngine(config)` 当前私有组合
`SteadyClock + GLFW WindowSurface + DisabledTaskSystem + bgfx`；`tina_sample_desktop` 默认300帧
deep-blue clear/present。Windows 最新门禁在 MSVC 19.50 与 CMake 4.2.3 下通过 Debug/Release
构建、基础183/183、GLFW专项22/22、bgfx专项11/11、Null样例300帧、WindowSurface GLFW样例300帧，
以及真实 D3D11 Intel Iris Xe Desktop样例默认300帧；`TINA_BUILD_TESTING=OFF` 的 production-style
GLFW样例300帧也已通过。Game SDK/public header 无 bgfx、GLFW 或 native 泄漏。Linux M7-B1 门禁已在 GCC 13.4 X11、Clang 22.1.8 X11 sanitizer、
GCC 13 与 Clang 22 X11/Wayland 双后端通过基础183/183、GLFW专项22/22和300帧样例；
Clang 基础测试无 suppression，Wayland匹配0，X11仅精确抑制 `_XimOpenIM` 的第三方 retention。
Linux M7-B2 X11 图又在 GCC 13.4 与 Clang 22.1.8 sanitizer 下通过基础183/183、GLFW专项22/22、
bgfx专项11/11和 Desktop 300帧；Clang X11 suppression命中专项12次/4896 B、Desktop 1次/408 B，
Desktop 使用 bgfx Vulkan/llvmpipe，因此不计作硬件 GPU 性能门禁。

- **已完成 M7-B1**：按 ADR 0020 实现 move-only `NativeWindowSurfaceLease`、generation
  `WindowSurfaceId`、backend-neutral `RenderSurfaceState`、`WindowSurfaceSnapshot` 与
  WindowSurface-aware tagged composition；公共接口不暴露 native、`void*`、GLFW 或 bgfx getter；
- **已完成 M7-B1**：只有私有 decoder 能解析 native payload；Win32、X11、Wayland native bridge
  分别位于私有 TU；Platform/Render 通过 `IndependentPlatformRenderFactories` 与
  `WindowSurfacePlatformRenderFactories` 明确组合；
- **已完成 M7-B1**：EngineHost 创建顺序为 Clock → Platform → Task → lease → Render → publish
  window；Render 创建失败和窗口 publish 失败均逆序释放 lease 后再销毁 Platform；
- **已完成 M7-B1**：surface snapshot 只从 committed Platform metrics 派生，resize/content-scale/
  suspend 改变才把 `surfaceRevision` 精确增加1；Runtime 与 Render 分别拒绝 source revision 未前进、
  surface revision 跳号/回退、事实未变却递增，以及本帧 Window metrics、revision、identity 不一致；
- **已完成 M7-B1**：NullRender 分离 `engineFrameIndex` 与 `submissionIndex`；Suspended 帧继续进入
  Render maintenance，但不 Present、不增加 submission index；
- **已完成 M7-B2 core**：建立可选 `tina_render_bgfx`、真实 bgfx init/clear/touch/frame/shutdown、
  owner-thread 与 move-only lease 生命周期；初始 Suspended 使用内部1×1 bootstrap，resize/resume 才 reset，
  content-scale-only 不 reset，Suspended 不 clear/present/递增 submission；7项纯 Tina planner 测试与4项 factory/lease 回滚测试
  不向 public header 暴露 native/bgfx 类型；
- **已完成 M7-B2 Desktop**：建立 `tina_bootstrap_desktop`、`Tina::Desktop::CreateEngine` 和300帧真实 GPU 样例；
  Game SDK/Phase Context 不暴露 RenderDevice/native/bgfx，Engine Module SPI 只暴露纯 Tina Render 类型；
- **仍后置**：Scene/UI/Pass Scheduler、submission ticket/drain、production Gamepad、完整 DPI/IMM32，以及
  resize、最小化、恢复的真实自动化验收。

### M7-C 增量 UI Core 与 Null DisplayList

- **已完成 M7-C1a**：建立 `tina_ui` target，当前只依赖 `Tina::Core` 与 `Tina::Platform`；
  已实现 generation `Tina::UI::UINodeId`、`Tina::UI::UIContext`、move-only
  `Tina::UI::UIRootOwner` RAII、`Tina::UI::UICommittedStructureView` 结构 snapshot，以及
  UI-owned `Tina::UI::InputTransitionConsumptionView` / `Tina::UI::ContinuousControlClaimsView`
  route-result view ABI；
- **已完成 M7-C1b**：新增 `UILayoutLength/UILayoutStyle/UIDirty` 与
  `UICommittedLayoutView`；width/height/min/max 采用 border-box，Percent 使用 `0..100` 且相对
  containing content box，默认 Auto root 以 viewport content box 为确定基准；
- **已完成 M7-C1b**：Create 期固定容量 PMR style/dirty side array、dirty queue、layout scratch 与
  committed structure/layout 双缓冲；style mutation 先预检再 node→root 原子 dirty 传播；
  `commitLayout()` 原子发布 pending structure+layout，`commitStructure()` 仅作为 C1a 诊断 seam；
- **已完成 M7-C1b**：非递归 Measure/Arrange 覆盖 Px/Percent/Auto、margin/padding/gap、
  Row/Column/grow、justify/align/stretch、Absolute Overlay、visibility 与 min-wins；viewport 变化重排，
  同 viewport 无变化为0 pass；finite/算术溢出/容量失败保留旧 snapshot 与 pending dirty；
- **已完成 M7-C1b 门禁**：50,000节点深树非递归 layout，以及首次发布后连续300次无变化 commit
  的0 layout pass、revision 不变与0新增 supplied UI PMR allocation；
- **已完成 M7-C1c-a**：固定容量 PMR Pointer policy/route-ancestry scratch、`Ignore`/`Targetable`、
  双缓冲 `UICommittedHitView`；同一 view 内 entry 的 paint ordinal 唯一且严格递增，view 携带 structure/layout/
  paint-order/hit revision；hit-only commit 为0次 layout，成功 `commitLayout()` 事务发布 structure/layout/hit，
  任一候选失败时三份旧 snapshot 均保持不变；
- **已完成 M7-C1c-a 门禁**：15项 committed hit snapshot 测试，当时 `tina_ui_tests` 为54/54；覆盖
  50,000节点非递归快照、stale generation、固定容量失败和 supplied UI PMR 释放；
- **已完成 M7-C1c-b1**：无分配 `queryPointerHit()` 反向扫描 committed hit view，只命中同时位于
  world/effective clip 的 `Targetable` entry，使用半开边界并返回 route index、四类 revision 与 visited count；
- **已完成 M7-C1c-b1 门禁**：新增5项 query 测试后当时 `tina_ui_tests` 总数为59项；Windows Debug/Release、
  Linux GCC 13.4 与 Clang 22 ASan/UBSan/LSan 均通过，300次查询无新增 supplied UI PMR allocation；当前总数见
  下一条 C1c-b2 75/75；
- **已完成 M7-C1c-b2**：fixed-capacity synthetic routed pointer event；使用固定容量 route path/listener
  storage、48-byte fixed-inline `noexcept` callback、generation-safe RAII token、owner-thread immediate reset、
  off-thread deferred reset、Capture→Target→Bubble、stop/consume、route 中 add/reset/destroy 安全失效和
  route/commit reentrancy guard；
- **已完成 M7-C1c-b2 门禁**：新增16项 route 测试后 `tina_ui_tests` 为75/75；Windows MSVC 19.50
  Debug/Release、Linux GCC 13.4、Linux Clang 22.1.8 + libstdc++15.2 ASan/UBSan/LSan 均通过，Clang
  无 sanitizer 诊断；初次 GCC 暴露的 routed-pointer callback `requires` 名称可见性问题已修复，二次
  GCC/Clang 构建无 warning；
- **已完成 M7-C1c-b3a**：Pointer Button/Wheel transition 固化事件时 window-logical position，
  `PlatformFrameBuilder` 拒绝非有限坐标，私有 GLFW producer 按 callback 顺序保存位置；新增 builder
  与真实 GLFW 集成门禁证明 `A → Button/Wheel → B` 中事件仍使用 A，帧末 B 不会覆盖历史坐标；
- **已完成 M7-C1c-b3b**：实现 Runtime-private `UIInputRouteProducer` 与独立
  `tina_runtime_ui_tests`；只把 Move/Button/Wheel 逐 raw ordinal 路由，reset/cancel/非 Pointer 保留 hole，
  consumed 写入双预分配 PMR bitset，claims 恒为 canonical `None`；300帧共用 supplied PMR 时 allocation
  count 不增长，且 supplied PMR 必须长于 producer；
- **已完成 M7-C1c-b3b 失败边界**：测试先产生1次 root Move listener side effect，再让后续深层
  Button route 因 route path capacity 失败；staging 不发布、旧 published view 保持，但推进 attempted
  watermark，同一 frame retry 被拒且 callback 仍为1。测试 target 直接运行 GoogleTest，不使用 CTest；
- **已完成 M7-C1c-b3c**：`EngineHost` 在 `PlatformEventDispatcher` 后、`ActionMapper` 前调用 producer；
  Runtime-private owner 在首次看到 primary `WindowId` 时惰性创建唯一 `UIContext`，Headless 绑定前为 null，
  绑定后 primary 消失或 generation 更换会结构化失败，同一 ID 的 metrics/content scale/minimized 变化不重绑；
  Context 在 Render → Task → Platform → Clock module shutdown 前于 owner thread 销毁；
- **M7-C1c-b3c 边界**：owner 不调用 `commitLayout()`，route 只读上一帧 committed snapshot；claims 仍为
  canonical `None`。Game SDK 尚不能取得 Context 或创建 root，所以该切片只完成输入时序/所有权，不能称为
  可见 UI；
- **当前限制**：changed frame 仍对整棵 live tree执行一次 Measure/Arrange，dirty leaf 跳过无关
  subtree 尚未实现；正式路径虽已接线，但没有 Game SDK root access，无法产生产品 UI；
- **仍后置**：EngineConfig UI capacities、Game SDK scoped UI access、真实 claims、dirty subtree pruning、
  持久 Pointer Capture、Focus/Modal、Button default action、paint snapshot/DisplayList、nested clip、text/glyph、
  FreeType 与 bgfx UI pass；
- 后续继续实现后端无关 Quad/Text/Clip DisplayList、FramePinSink/capacity rollback 和相邻兼容 batching
  contract；Null UI 直接测试 route/layout/paint order，不链接 FreeType/bgfx；
- 硬门禁：无变化 UI 每帧0 layout、0 PaintCache rebuild、0 Tina heap allocation。

### M7-D 可见 UI 与中文字体

- `tina_ui_freetype` 只在生产 adapter 中使用 FreeType，text layout 与 glyph raster 分离，
  Atlas 带 generation/预算/retirement；
- 扩展私有 `tina_render_bgfx` 实现后端无关 `UIDisplayListView` 的 UI Pass、内置 UI shader 与
  Atlas texture upload；`tina_ui` 不链接 bgfx，也不暴露 view id/handle；
- 使用版本化内置 Cooked Font/Texture fixture 形成 Panel、中文 Label、Button、Modal 可运行样例；
  禁止 Runtime 路径加载源字体、直接调用 bgfx 或复用 Legacy UIRenderer/API。

### M7-E Platform 完整输入

- Windows IME 只使用私有 IMM32 adapter；补 Focus/Capture/composition 取消与窗口销毁顺序；
- 接入 GLFW standard Gamepad sampled diff、primary-window routing、回滞/重复/Accept/Cancel；不伪造
  两次 Poll 之间不可观测的 Down→Up；
- GLFW adapter 向 M7-A 已有的 `PlatformEventBatch`/`PlatformEventDispatcher` 发出 Gamepad
  connect/disconnect 生命周期；断连先产生 `InputCancelTransition` 再回收 generation；
- 完成100%/150%/200% DPI、键鼠、composition、实体手柄与资源回收门禁。

## M8 Scene 与 2D 垂直切片

- EnTT 只作为内部存储，公共接口只暴露 generation `EntityId`；
- 建立 Local/Parent/World Transform、层级循环检测和阶段末 command commit；
- 建立 Camera2D、SpriteRenderer2D、稳定 layer/order、只读 Render Scene Extraction 和 chunk culling
  接口；基础样例使用内置 Cooked Sprite fixture；
- 定义 TileMap 为 gameplay feature、`IGridCollisionProvider` 和 Tile AABB/Box2D 分工；正式
  TileMap 产品路径在 M10/M11 接入 Cooked 资产并验收；
- 2D infrastructure 样例显示 Sprite、中文 Label/Button，验证 fixed-step、插值、world picking、
  UI 输入不穿透和资源释放；
- World 不依赖 GLFW 输入、具体 TileMap 或 bgfx。

## M9 Render 与 3D 垂直切片

- Tina Engine Module Render SPI 只使用 typed handle/descriptors；Game SDK/Phase Context 不暴露
  RenderDevice，bgfx 类型只在 `tina_render_bgfx` 私有层；
- 固定 Opaque3D、Sprite2D、UI、Present Pass，明确 clear/load/store、失败停止和资源计数；
- 扩展 M7 backend 支持 Perspective、depth、canonical static Mesh、UnlitBaseColor Material v1、
  Shader ABI、bounds/culling 和静态 instancing；
- procedural 3D infrastructure 样例持续显示非空 Mesh，并验证 resize、depth 和退出时
  buffer/texture/pipeline 零泄漏；
- 不引入完整自研多后端 RHI、PBR、阴影、动画或后处理。

## M10 Asset 与 Cooker 垂直切片

- 后台 CPU Decode 与主线程/GPU Upload 分队列，按任务数、字节数、时间预算；
- Asset 状态、generation 和取消贯穿两阶段，迟到任务不能复活旧 slot；
- 实现弱 Handle/强 Lease、UploadTicket/retirement、稳定128位 AssetId、`tina_asset_format`、
  依赖 DAG、内容 Hash 和事务 Manifest；
- `tina_assetc` 执行 Parse → Validate → Build → Validate Cooked → Atomic Write；
- 固定 cgltf v1.15；最小 glTF 输出 StaticMesh/Texture2D/Material/Prefab；2D 输出 Texture2D/
  Sprite/Tileset/TileMap；不支持特性返回明确诊断；
- 为正式产品路径把 M7–M9 的内置 fixture 替换为 Catalog/Manifest 资产；hermetic、版本锁定的
  infrastructure/module-test fixture 继续保留；新增 Cooked glTF/Material/Prefab 3D 产品样例。

## M11 产品 2D、UI 与 Audio

- 增加 Checkbox、Slider，将主音量、音乐、音效和全屏接入真实后端；
- 建立 `tina_physics2d`，公共 header 只暴露 Tina PhysicsBodyId/PhysicsWorld2D/command/contact，Box2D 3.x 作为
  PRIVATE 实现；完成创建/step/contact/query/关闭和资源归零门禁；
- 建立单线程 `tina_physics2d_bench` 基线；只有 step p99 超预算才在后续独立提交接入 Box2D
  worker callbacks，不把未验证并行作为 M11 正确性的前置条件；
- 以当前游戏为正式 2D 产品门禁：Cooked TileMap/Tileset、Camera2D、chunk culling/dirty rebuild、
  CharacterController2D/Tile AABB、至少一个 Box2D dynamic body 和 UI overlay；
- `tina_audio_miniaudio` 作为唯一真实 backend，通过 generation voice handle、命令队列和
  主线程 completion 保证关闭安全；
- 覆盖 callback 0分配/0阻塞、command/completion 满容量、设备 Disabled、Music underrun、
  Asset lease ACK 和300帧资源归零；
- 增加基础可访问语义和稳定截图回归；
- Dropdown、TreeView、多行文本、复杂 shaping 和 IME 候选窗只按真实需求增加。

## M12 Legacy 删除

- 新切片覆盖 2D、UI、3D、Asset 与 Audio 的必要路径；
- 正式 `tina_sample_2d` 只使用最终 Catalog/Manifest，并覆盖 Cooked TileMap、角色/Box2D/UI；
  正式 `tina_sample_3d` 只使用最终 Cooker 产物并覆盖 glTF/Material/Prefab；M8/M9 fixture 样例
  不能替代产品门禁；
- 旧 Application、CoreLegacy、EASTL/EABase、公开 EnTT/bgfx 边界和路径资源接口确认零引用；
- Windows/Linux 构建、直接 GoogleTest、所有 smoke 与资源计数通过；
- 删除旧 target、旧源码和无用依赖形成独立可回滚提交；
- vNext 分支合入最新已提交的 `dev` 并复验后，才合并回干净主工作区。

## 后续能力

- 只有 profiling 证明需要后才把已落地的 Box2D step 接入 Task System；Jolt 作为唯一 3D 后端在
  真实玩法出现后接入；PhysX、Bullet、Rapier 不进入依赖或构建；不统一 2D/3D Physics API；
- PBR、阴影、动画、脚本和编辑器均等待 Runtime、Render、Asset 基础契约稳定；
- Linux/GCC 告警先区分第三方与 Tina 自身，再按模块清理；持续保持并复验 Clang 22 + libstdc++15 ASan/UBSan 可复现门禁。
