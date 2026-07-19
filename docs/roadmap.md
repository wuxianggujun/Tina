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
  接线，M7-C1c-b3d1 已补 EngineConfig UI capacities 与 `updateUI` 后、Render 前的 private layout
  coordinator，M7-C1c-b3d2 已补 startup metrics seed 与 Game SDK scoped UI access，M7-C1c-b3e 已补
  held primary Pointer Button claim，后续独立切片又补 Game SDK root-scoped routed Pointer listener；
  Key/Gamepad/axis claim producer、IMM32、production Gamepad 和连续
  axis mapping 仍在后续子切片；
- 有界 CPU/IO/Main worker、阶段指标与完整 shutdown deadline/fatal-stop；
- typed render resource handle、Pass Scheduler、World RenderScene、owning RenderFramePacket/pool 与
  submission completion 保活；后端无关 SolidQuad builder、UI→Render bridge、D0 Runtime
  submit-call-local primary-window UIDisplayList handoff、D1 私有 bgfx SolidQuad UI pass 与 D2 Game SDK
  `setBoxPaint()` facade 已先实现；
- Scene、Asset、Audio 的真实契约和消费者，以及 Runtime-integrated UI root/layout/render
  pipeline；M7-C1b/C1c-a/C1c-b1/C1c-b2 C++23 standalone `tina_ui` tree/layout/committed-hit/
  point-query/synthetic-route foundation、M7-C1c-b3b 私有 producer、M7-C1c-b3c EngineHost 接线与
  M7-C1c-b3d1 layout commit、M7-C1c-b3d2 startup/root scoped capability、M7-C1c-b3e Pointer claim、
  SolidFill committed paint、Render SolidQuad DisplayList builder、UI→Render integration bridge、D0
  Runtime DisplayList handoff、D1 bgfx UI SolidQuad pass 与 D2 可见 panel smoke 已实现，但仍没有 owning
  Runtime packet/FramePin、文本/glyph、完整 Widget facade 或产品 UI；当前 listener
  facade 只是低层 routed callback/claim seam，后续 Button default action 已另行完成 primary Pointer 窄交互；
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

实施状态（2026-07-18）：M7-B1 private WindowSurface handoff、M7-B2 private bgfx core、
Desktop bootstrap、D1 bgfx SolidQuad UI pass 与 D2 真实 GPU 可见 panel 冒烟已完成。`Tina::Desktop::CreateEngine(config)` 当前私有组合
`SteadyClock + GLFW WindowSurface + DisabledTaskSystem + bgfx`；`tina_sample_desktop` 默认300帧
并创建4个 retained SolidFill panel。Windows 最新门禁在 MSVC 19.50 与 CMake 4.2.3 下通过 Debug/Release
构建、基础207/207、UI92/92、Runtime→UI53/53、UI→Render12/12、GLFW专项25/25、bgfx专项16/16、
Null样例300帧，以及真实 D3D11 Intel Iris Xe Desktop样例 Debug 1200帧截图检查/Release 300帧；`TINA_BUILD_TESTING=OFF` 的 production-style
GLFW样例300帧也已通过。后续 listener-only Windows Debug 增量门禁为基础208/208、UI95/95、
Runtime→UI55/55与Null样例300帧；该 listener-only 切片当时没有重跑 Release、GLFW、bgfx 或可见 Desktop，
因此上一组仍是其完整产品证据。
Game SDK/public header 无 bgfx、GLFW 或 native 泄漏。Linux M7-B1 门禁已在 GCC 13.4 X11、Clang 22.1.8 X11 sanitizer、
GCC 13 与 Clang 22 X11/Wayland 双后端通过基础183/183、GLFW专项22/22和300帧样例；
Clang 基础测试无 suppression，Wayland匹配0，X11仅精确抑制 `_XimOpenIM` 的第三方 retention。
Linux M7-B2 X11 图又在 GCC 13.4 与 Clang 22.1.8 sanitizer 下通过基础183/183、GLFW专项22/22、
bgfx专项11/11和 Desktop 300帧；Clang X11 suppression命中专项12次/4896 B、Desktop 1次/408 B，
Desktop 使用 bgfx Vulkan/llvmpipe，因此不计作硬件 GPU 性能门禁；Linux 尚未复验 D1/D2 的 bgfx UI pass 与可见 panel。

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
  Linux GCC 13.4 与 Clang 22 ASan/UBSan/LSan 均通过，300次查询无新增 supplied UI PMR allocation；后续
  C1c-b2 当时增至75/75，b3d2 为78/78；Windows Debug/Release、Linux GCC 与 Clang sanitizer 的 b3e 历史门禁均为81/81；
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
  consumed 写入双预分配 PMR bitset，该 b3b 切片的 claims 当时恒为 canonical `None`；300帧共用 supplied PMR 时 allocation
  count 不增长，且 supplied PMR 必须长于 producer；
- **已完成 M7-C1c-b3b 失败边界**：测试先产生1次 root Move listener side effect，再让后续深层
  Button route 因 route path capacity 失败；staging 不发布、旧 published view 保持，但推进 attempted
  watermark，同一 frame retry 被拒且 callback 仍为1。测试 target 直接运行 GoogleTest，不使用 CTest；
- **已完成 M7-C1c-b3c**：`EngineHost` 在 `PlatformEventDispatcher` 后、`ActionMapper` 前调用 producer；
  Runtime-private owner 在首次看到 primary `WindowId` 时惰性创建唯一 `UIContext`，Headless 绑定前为 null，
  绑定后 primary 消失或 generation 更换会结构化失败，同一 ID 的 metrics/content scale/minimized 变化不重绑；
  Context 在 Render → Task → Platform → Clock module shutdown 前于 owner thread 销毁；
- **M7-C1c-b3c 边界**：owner 不调用 `commitLayout()`，route 只读上一帧 committed snapshot；该切片当时的 claims 仍为
  canonical `None`。Game SDK 在 b3d2 前仍不能取得 Context 或创建 root，所以该切片只完成输入时序/所有权，不能称为
  可见 UI；
- **已完成 M7-C1c-b3d1**：`UIContextCapacityConfig` 进入 focused public header，并由 standalone
  `UIContext::Create` 与 `EngineConfig::primaryWindowUICapacities` 共享 validator；非法配置在任何 factory
  前拒绝。Runtime-private coordinator 在 `updateUI` 成功后、Render submit 前按主窗口 logical extent
  对每个严格递增 `PlatformFrameId` 至多尝试一次 `commitLayout()`；Headless 双缺席成功 no-op，失败阻断
  Render 且消费当前 frame attempt；
- **已完成 M7-C1c-b3d2**：按 ADR 0021 为 startup transaction 增加不 poll、不消费 frame id 的
  backend-neutral primary-window metrics seed；Runtime 在 `onEnter` 前显式绑定 primary `UIContext`，
  并在 State commit 前发布首份 structure/layout/hit/paint snapshot；Game SDK 只获得 root-scoped、
  phase-epoch-scoped 的 `PrimaryWindowUIRootBuilder`/`PrimaryWindowUITreeUpdater`，不暴露裸
  `UIContext*`，也不允许任意阶段 `createRoot()`；
- **已完成 M7-C1c-b3d2 失败边界**：facade 为 move-only、owner-thread scoped；跨 phase 保存后返回
  `UIPhaseCapabilityExpired`，Headless 请求返回 `PrimaryWindowUIUnavailable`。第一次 capability operation
  失败会成为 sticky phase error，后续 mutation 不执行；Runtime 离开 callback 时用 no-throw abort guard
  确保 facade 失效；
- **已完成 Game SDK routed Pointer listener facade**：`PrimaryWindowUITreeUpdater::addRoutedPointerListener()`
  只在 current phase/current root subtree 注册；返回 token 可跨 phase 保存但不保活 Context/root，State
  在 `onExit()` 先 reset token。跨 root 失败原子且进入 sticky first-error，不占 listener slot/high-water；
  callback 最终 move/destructor 若重入释放 root 会重新校验并回滚，销毁 Context 触发生命周期 terminate；
  EngineHost 端到端测试证明 listener 先于 ActionMapper，claim-only 也拦截同帧 Gameplay Action；
- **已完成 M7-C1c-b3e**：routed Pointer listener 通过 `claimPointerButton()` 在固定 bitset 中幂等请求
  当前 window/pointer 的 button ownership；Runtime 只发布最终 Platform snapshot 中仍 held 的 primary
  Pointer Button，并在 Create 期双预分配 PMR claim buffer 中去重，不把 Key/Gamepad/axis 能力伪装成已完成；
- **已完成 M7-C1c-b3e 失败与消费边界**：claim capacity 或后续 route 失败不交换 published storage，
  但 attempted frame/sequence watermark 已推进且同帧不能重放。ActionMapper 对已 held Gameplay source
  生成 Cancel 并 suppress 到真实 Up；同帧 ButtonDown 即使没有 consume，只要被 claim 也不会激活
  Gameplay。Windows MSVC 19.50 Debug/Release 当时直接通过基础194/194、UI81/81、
  Runtime→UI46/46、GLFW25/25、bgfx11/11及Null/Platform/Desktop各300帧；Linux GCC 13.4 与 Clang 22
  sanitizer 也直接通过基础194/194、UI81/81、Runtime→UI46/46及Null样例300帧，Clang无诊断；
- **已完成 SolidFill committed paint**：`UIBoxPaint` 当前只含 optional SolidFill，straight sRGBA8 以
  确定性整数规则转换为 premultiplied RGBA8；Create 期固定 `paintSnapshotCapacity`、local cache 与双缓冲
  `UICommittedPaintView`，成功 commit 原子发布 structure/layout/hit/paint 四份 snapshot。paint-only commit
  不重排 layout/hit，no-op 不增加 revision；Windows MSVC 19.50 Debug 新增11项后 UI 为92/92；
- **已完成 Render SolidQuad DisplayList**：`tina_render` 的单缓冲 `UIDisplayListBuilder` 使用固定 PMR
  command/clip/batch storage，支持 strict paint order、axis-aligned clip interning、相邻兼容 batching、
  checksum、剪枝与整帧 rollback；Windows Debug 新增11项后基础测试为205/205；
- **已完成 UI→Render bridge**：独立 `tina_ui_render_integration` 是唯一同时 PUBLIC 依赖 UI 与 Render
  的窄桥，按 logical/framebuffer extent 做 outward rounding/clamp 并拥有完整 builder transaction；
  Windows MSVC 19.50 Debug/Release、Linux GCC 13.4 与 Linux Clang 22 sanitizer 的独立12项 GoogleTest
  均通过。Linux 两条图也通过基础205/205、UI92/92、Runtime→UI46/46和Null样例300帧，Clang无诊断；
  该切片当时 Windows Release 基础/UI 仍沿用 b3e 历史门禁，已被后续 D2 Windows Release
  207/207、UI92/92、Runtime→UI53/53覆盖；
- **已完成 D0 Runtime primary-window UI DisplayList handoff**：Runtime-private
  `PrimaryWindowUIDisplayCoordinator` 在 layout/paint commit 后、Render submit 前使用 fixed PMR builder
  构建 primary-window UIDisplayList，并把 `RenderFrame::primaryWindowUIDisplayList` 作为 submit-call-local
  borrowed view 交给 backend；backend 不得在 `submitFrame()` 后保留。Headless、0 framebuffer 与
  suspended surface 发布空 list；失败不保留旧 publication，也不提交截断 list。`EngineConfig`
  新增 `primaryWindowUIDisplayListCapacities` 的 command/clip/batch 三项固定容量。Windows MSVC
  19.50 / CMake 4.2.3 Debug/Release 均通过基础207/207、UI92/92、Runtime→UI51/51、UI→Render12/12
  和 Null样例300帧；
- **已完成 D1 private bgfx SolidQuad UI pass**：shader 由 build-tree `bgfx::shaderc` 生成
  glsl/spv/dxbc embedded headers，源码只提交 `.sc` 与 CMake 生成规则；backend 使用 transient
  32-bit indexed geometry、Sequential view、top-left framebuffer ortho、premultiplied blend 和 scissor，
  并在 suspended/空 list/容量预检失败时跳过半帧提交。Windows MSVC 19.50 Debug/Release 的
  bgfx专项增至16/16；
- **已完成 D2 Game SDK paint facade 与可见 panel smoke**：`PrimaryWindowUITreeUpdater::setBoxPaint()`
  把 SolidFill authoring 暴露到 scoped Game SDK facade，并保持 phase epoch、root scope、owner-thread
  与 sticky error 约束；Runtime→UI Windows Debug/Release 增至53/53。`tina_sample_desktop` 创建4个
  retained painted panel，Windows D3D11 Debug 1200帧截图验证 background/blue/cyan/pink、alpha blend 与
  right-edge scissor，Release 300帧 clean；
  D0/D1/D2 综合门禁中 bgfx专项16/16与 D3D11 Intel Iris Xe Desktop 样例通过，Release clean；
  Debug `RefCount is 3 (expected 0)` 为已记录第三方 debug layer 提示；
- **listener extension 历史增量门禁**：该切片的 Windows MSVC 19.50 / CMake 4.2.3 Debug 直接通过
  基础208/208、UI95/95、Runtime→UI55/55与Null样例300帧；它不是当前 M8-B 测试数量；
- **已完成 Button default action 切片**：Button 默认 `Targetable`；只实现
  `PrimaryPointerId + PointerButton::Primary` 的 Down armed/pressed、Move inside 更新、Up-inside 一次
  activation、`preventDefaultAction()`、retained `set/clearButtonAction()`、pressed query，以及非
  gamepad-only cancel/覆盖窗口 reset 的无 action 清理。action 使用48字节 fixed-inline callback、固定容量
  slot pool、一个预分配 transaction slot 与 route registration serial；该历史切片的 Windows Debug
  通过基础208/208、UI109/109、Runtime→UI60/60与 Null 300帧；
- **已完成 M7-C1c-b4a**：新增固定容量 layout work bits 与 prepared-input cache，changed frame
  只对需要 Measure/Arrange 的节点进入对应调度，clean sibling/subtree 复用既有几何结果；Auto 祖先、
  Collapsed 子树、父约束/viewport 变化和 layout/paint candidate 失败均有 full-rebuild 回退与回归测试。
  Windows Debug `tina_ui_tests` 当前为115/115；
- **当前限制**：这不是完整 dirty-range pruning。`buildLayoutOrder`、父级 `arrangeChildren`、committed
  layout、hit 与 paint snapshot 仍可能线性遍历；正式路径虽已接线并可创建 retained root/Panel/Label/Button 节点，低层也已有
  SolidFill paint/DisplayList bridge、D0 Runtime handoff、Game SDK paint setter 和私有 bgfx SolidQuad pass，
  但仍没有文本/glyph、Button Keyboard/Gamepad activation、Disabled/theme 视觉、完整 Widget facade 或产品 UI；
- **仍后置**：Key/Gamepad/axis claim producer、完整 dirty-range pruning、持久 Pointer Capture、Focus/Modal、
  Button Keyboard/Gamepad activation 与 Disabled/Theme 视觉、完整 Game SDK widget facade、Image/Text/Glyph PaintCache、nested clip、
  Runtime `RenderFramePacket`/FramePin 与 FreeType；
- 后续把当前 borrowed SolidQuad DisplayList 升级为 owning Runtime packet，再扩展 Image/Text/Glyph 与资源 pin；Null UI
  继续直接测试 route/layout/paint order，不链接 FreeType/bgfx；
- 硬门禁：无变化 UI 每帧0 layout、0 PaintCache rebuild、0 Tina heap allocation。

### M7-D 可见 UI 与中文字体

- `tina_ui_freetype` 只在生产 adapter 中使用 FreeType，text layout 与 glyph raster 分离，
  Atlas 带 generation/预算/retirement；
- 在已完成私有 `tina_render_bgfx` SolidQuad UI Pass 的基础上扩展 Image/Glyph 命令、Atlas texture upload
  与资源 pin；`tina_ui` 不链接 bgfx，也不暴露 view id/handle；
- 使用版本化内置 Cooked Font/Texture fixture 形成中文 Label、Button、Modal 可运行样例；
  禁止 Runtime 路径加载源字体、直接调用 bgfx 或复用 Legacy UIRenderer/API。

### M7-E Platform 完整输入

- Windows IME 只使用私有 IMM32 adapter；补 Focus/Capture/composition 取消与窗口销毁顺序；
- 接入 GLFW standard Gamepad sampled diff、primary-window routing、回滞/重复/Accept/Cancel；不伪造
  两次 Poll 之间不可观测的 Down→Up；
- GLFW adapter 向 M7-A 已有的 `PlatformEventBatch`/`PlatformEventDispatcher` 发出 Gamepad
  connect/disconnect 生命周期；断连先产生 `InputCancelTransition` 再回收 generation；
- 完成100%/150%/200% DPI、键鼠、composition、实体手柄与资源回收门禁。

## M8 Scene 与 2D 垂直切片

**M8-A 已完成 Scene World/Transform 基础（Windows MSVC 2026 Debug/Release `tina_scene_tests` 均19/19）：**
新增 `Tina::Scene` target、owner/generation `EntityId`、固定容量 `Scene::World`、POD
`LocalTransform`/`WorldTransform`、owner-thread 读写、非递归层级传播、keep-world/keep-local reparent、
父销毁提升、显式子树销毁、dense live index 和两阶段 publication；循环/跨 World/stale/非有限/溢出/shear
诊断。当前 M8-A 的实体 slot 使用 `Core::GenerationPool`，
不把 EnTT 带入 vNext Null；EnTT 仍只允许作为后续 Scene component storage 的 PRIVATE 实现。
层级编辑在 owner thread 立即校验/提交，`updateWorldTransforms()` 是显式 world-transform barrier；
阶段末 command buffer 仍是后续切片，不把本轮 API 误写成完整 State/World command pipeline。

- **M8-B RenderScene extraction foundation 已完成：** `tina_render` 提供固定容量
  `RenderSceneBuilder`/phase-local `RenderSceneWriter`，接受已解析 Camera2D/Sprite2D 值，执行输入校验、透明/
  隐藏剪枝、旋转保守裁剪、pixel snap、稳定 layer/order/entity/insertion 排序和统计 checksum；Runtime
  在 extraction 前后执行 begin/commit/rollback，并将 borrowed `primaryWorldScene` 放入 `RenderFrame`。
  独立 `tina_render_scene_tests` 与 Headless/Null `tina_sample_2d_infrastructure --frames=300` 验证固定容量、
  Runtime handoff 和退出回收；
- **M8-B 已记录 Windows 门禁：** Debug/Release Null 图通过基础211/211、UI115/115、Runtime→UI60/60、
  UI→Render12/12、Scene19/19、RenderScene11/11，以及 Null/2D infrastructure样例各300帧；Debug adapter
  复验通过 GLFW26/26、Platform样例300帧、bgfx16/16与Desktop连续3次各300帧。iconify 回归保持正 logical
  extent 与 framebuffer `0x0` suspended 语义；本轮没有重新截图或运行 Linux M8-B；
- EnTT 只作为内部 component storage，公共接口只暴露 generation `EntityId`；Scene component command commit、
  Camera/Sprite component storage、chunk culling、AssetHandle/FrameResourceRef 解析与正式可见 2D 产品路径仍待后续切片；
- 基础样例当前是 CPU/Null recording infrastructure，不显示 Sprite、不包含中文 Label/Button、world picking、
  TileMap 或 UI overlay；M9-C 的 `tina_sample_2d_infrastructure_bgfx` 只是另一个 GLFW+bgfx fixture 样例，能显示
  fixture Sprite 与 UI overlay，但不替代正式 `tina_sample_2d` 的 Catalog/Manifest 产品门禁；
- 定义 TileMap 为 gameplay feature、`IGridCollisionProvider` 和 Tile AABB/Box2D 分工；正式
  TileMap 产品路径在 M10/M11 接入 Cooked 资产并验收；
- 正式可见2D产品样例后续显示Sprite与中文Label/Button，并验证fixed-step、插值、world picking、
  UI输入不穿透和资源释放；当前Headless/Null infrastructure样例不承担这些可见产品门禁；
- World 不依赖 GLFW 输入、具体 TileMap 或 bgfx。

## M9 Render 与 3D 垂直切片

- **M9-A RenderScene 3D extraction foundation 已完成：** 在固定容量 `RenderSceneBuilder` 中加入
  `RenderPerspectiveCameraInput`、`RenderMesh3DInput`、resolved Perspective/Mesh3D view、当前帧
  framebuffer aspect（`0x0` 回退 logical）、正 scale 世界包围球、球体 frustum culling、稳定
  material/mesh/submesh/double-sided/depth/entity/insertion 排序和相邻 instance batch finalize；
  `tina_render_scene_tests` 当前22/22，`tina_sample_3d_extraction --frames=300` 在 Headless/Null
  验证4 submitted/3 visible/1 culled/2 batches、aspect变化和 `liveResources=0`。它不创建 bgfx 资源，
  不提供可见画面，也不计入 Legacy 删除门禁；
- **M9-B 最小 bgfx Opaque3D fixture 已完成当前最小实现：** `tina_render_bgfx` 私有消费
  M9-A 的 Perspective/Mesh3D `RenderSceneView`，但只接受 `meshKey=1`、`materialKey=1`、
  `submeshIndex=0` 的 procedural Cube fixture；Game SDK/Phase Context 仍不暴露 RenderDevice、
  ViewId、bgfx 类型或 GPU handle；
- M9-B 当时固定全 surface View 0 为唯一 color+depth clear owner，View 1 为 `Opaque3D` 并启用 depth write
  与 `Less` test，UI 后续由 M9-C 固定到 View 3 且不重复 clear；Camera 子 viewport 外也会被确定性清理；
- backend 私有创建 canonical `P3_N3_UV2` indexed Cube、`tina_opaque3d_unlit` shader program、
  静态 vertex/index buffer，并为每帧 Mesh3D item 写入真实 bgfx transient instance buffer；私有
  聚焦测试覆盖顶点/索引、fixture 写入与拒绝、容量失败原子性，以及 Opaque3D/UI 共用 transient
  vertex pool 的联合预算；
- `tina_sample_3d_infrastructure` 只在 GLFW+bgfx 图构建，通过 `Tina::Desktop::CreateEngine` 默认运行
  300帧，当前每帧提交3个 procedural Cube 和1个 instance batch；
- **M9-C 私有 bgfx Sprite2D fixture 与 2D/UI 样例已完成 Debug/Release 验证：** `tina_render_bgfx` 私有消费
  M8-B 的 Camera2D/Sprite2D `RenderSceneView` fixture 子集，只接受 `spriteKey=1`，写入 transient
  P2/UV2/ABGR vertex 与 u32 index，支持旋转、透明、flip 和稳定 Sprite order；
- 当前固定 View 顺序改为 0 clear、1 Opaque3D、2 Sprite2D、3 UI；这只是 fixture view 编号和临时提交顺序，
  不能写成 Pass Scheduler 已完成；
- M9-C 私有测试新增 Sprite2D geometry 和 Sprite2D+UI transient index budget，当前 Windows Debug/Release
  `tina_render_bgfx_tests` 均为43/43；两配置的 `tina_sample_2d_infrastructure_bgfx` 均通过 Desktop bootstrap 运行300帧，
  记录5个 Sprite、2个 UI panel、UI root 释放和 `renderResourceLedgerBalanced=true`，截图确认 Sprite 旋转、
  透明、flip 与 UI overlay；既有 D3D11 debug-layer `RefCount=3` 提示只在 Debug 出现，Release 未出现；
- 通用 Render typed handle/descriptors、Pass Scheduler、正式 Sprite2D Asset/product pass、Runtime-owned packet/FramePin、
  Cooked Mesh/Material/Texture/Prefab、Texture/Sprite Asset 产品路径、正式 `tina_sample_2d`、TileMap、Box2D、
  中文文本、glTF、PBR、阴影、动画、后处理、自动 resize/restore 产品门禁仍后置。

## M10 Asset 与 Cooker 垂直切片

- M10-A0 已完成独立 `tina_asset_format`：16字节强类型 `AssetId`/`ContentHash`、固定 little-endian
  Cooked Header/Manifest/Entry/Dependency schema、确定性 object path、成功路径零分配的 borrowed view，
  以及 magic/schema/enum/flag/limit/overflow/layout/padding/排序/依赖存在与 kind 校验；独立
  `tina_asset_format_tests` 直接运行 GoogleTest；
- M10-A0 不执行 XXH3、完整 DAG cycle、文件 IO、Asset registry/Handle/Lease、worker/upload、writer、
  atomic publish、cgltf 或产品资产替换；这些不能由“Manifest 可解析”推断为完成；
- M10-A1 契约已冻结独立 `tina_asset`：owning 不可变 `CatalogSnapshot`、注入 PMR、Create 后脱离
  Manifest bytes、AssetId binary search、依赖 target entry index 解析、完整 DAG cycle（迭代
  `O(V + E)`）、失败回滚与独立 `tina_asset_tests`；
- M10-A1 不实现 Handle/Lease、registry 状态机、文件 IO、Task worker、GPU upload、XXH3 计算、
  Cooker/cgltf 或产品资产替换；ADR 0016 仍为 Proposed，不得由 A1 偷偷冻结；
- 后台 CPU Decode 与主线程/GPU Upload 分队列，按任务数、字节数、时间预算；
- Asset 状态、generation 和取消贯穿两阶段，迟到任务不能复活旧 slot；
- M10-A2 起实现 Catalog artifact IO、版本化 XXH3 adapter、弱 Handle/强 Lease（需先确认 ADR 0016）、
  UploadTicket/retirement 和事务 Manifest writer；
- `tina_assetc` 执行 Parse → Validate → Build → Validate Cooked → Atomic Write；
- 固定 cgltf v1.15；最小 glTF 输出 StaticMesh/Texture2D/Material/Prefab；2D 输出 Texture2D/
  Sprite/Tileset/TileMap；不支持特性返回明确诊断；
- 为正式产品路径把 M7–M9 的内置 fixture 替换为 Catalog/Manifest 资产；hermetic、版本锁定的
  infrastructure/module-test fixture 继续保留；新增 Cooked glTF/Material/Prefab 3D 产品样例，2D 仍要后续
  接入 Texture/Sprite/Tileset/TileMap 资产后才形成正式 `tina_sample_2d`。

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
