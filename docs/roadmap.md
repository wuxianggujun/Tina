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
- M7-A 已补齐有界 PlatformFrame/Input/Action 与 Platform lifecycle dispatch；私有 GLFW desktop
  adapter、UI routed consumption producer 和连续 axis mapping 仍在后续子切片；
- 有界 CPU/IO/Main worker、阶段指标与完整 shutdown deadline/fatal-stop；
- typed render resource handle、Pass Scheduler、World RenderScene/UIDisplayList、Runtime-private
  RenderFramePacket/pool 与 submission completion 保活；
- Scene、Asset、UI、Audio 的真实契约和消费者；不为固定初始化顺序预先制造无消费者空壳；
- `tina_bench` schema v1、Bench/Profile preset、`tina_profile_tracy` 和 Tracy/Metrics A/B；
- Linux Null 图已完成 GCC 13.4 与 Clang 22.1.8 + libstdc++15 ASan/UBSan 门禁；完整 GLFW/bgfx
  产品图仍必须在对应切片重新验证。

## M7 Platform、最小 Surface 与高性能 UI 垂直切片

M7 分为五个独立提交，不将 GLFW、bgfx、FreeType、IMM32 和完整 UI 同时并入。

### M7-A PlatformFrame 与 Input correctness

实施状态（2026-07-17）：Headless Platform/Input 内核已完成；下列 GLFW `NO_API` 窗口与
`tina_sample_platform` 是本里程碑下一独立提交，不能把 Headless 结果冒充真实窗口验收。
Windows MSVC 19.50 Debug/Release、Linux GCC 13.4 与 Clang 22.1.8 + libstdc++15 ASan/UBSan
均直接通过162/162项 GoogleTest；Null sample 在各门禁连续运行300帧和10,000帧并正常退出。

- **已完成**：`tina_platform` 实现 generation `WindowId`、`PrimaryWindowConfig`、`WindowMetricsSnapshot`、
  `PlatformFrameView`、`WindowInputSnapshot`、有序 `InputTransitionBatch` 与 `PlatformEventBatch`；
- **已完成**：`PlatformFrameBuilder` 单测直接注入 Down→Up、Focus Cancel、overflow reset 与
  lifecycle payload；Runtime test adapter 验证 EngineHost wiring。Headless 仍不链接 GLFW；可复用
  production-like deterministic PlatformBackend test double 随 GLFW adapter 测试加入；
- **已完成**：Runtime 建立空 UI consumption seam、`InputTransitionConsumption`、`ContinuousControlClaims`、
  digital Action Map 和有序 Simulation Action latch，验证0/1/4 fixed-step 只消费一次；
- **已完成**：`EngineConfig::inputActions` 注册唯一 Engine default Input Context 的 digital bindings；
  raw/event/text、action/binding 与 subscription 分别由职责明确的配置块一次性分配。UI claim
  在 M7-A 无 producer，内部固定上限64；Escape 不走 backend shortcut；
- **已完成**：Runtime 建立只承载 resize/focus 等平台生命周期的有界 private `PlatformEventDispatcher`；Game SDK
  只暴露 `PlatformEventSubscriptions` 与 RAII subscription；
  OS CloseRequested 只走 control outcome，不进入队列；这不是通用 Gameplay EventBus；
- **下一提交**：实现私有 `tina_platform_glfw`：`GLFW_NO_API` 主窗口、create/destroy/poll、close/focus/
  resize 与键鼠；不引入 SDL/SDL3，callback 只写预分配 buffer/sticky failure；
- **下一提交**：`tina_sample_platform` 用 GLFW Window + NullRender；Escape 通过 Frame Action 请求完整当帧后退出，
  `--frames=N` 可自动退出；GLFW 失败不得静默降级 Headless；
- 本切片不实现 native surface/bgfx、UI tree、FreeType、IMM32、production GLFW Gamepad
  adapter/registry/navigation、通用 Gameplay EventBus 或多窗口。

### M7-B Native Window Surface 与最小 bgfx

- 按 ADR 0020 实现 move-only `NativeWindowSurfaceLease` 与内部
  `PlatformAwareRenderFactory`；只有 `tina_render_bgfx` 私有 decoder 可解析 native payload；
- 实现 Creating → Active ↔ Suspended → Closing → Draining → Closed，独立
  `engineFrameIndex`/`submissionIndex`，覆盖 resize revision、300 suspended frames 与 drain 顺序；
- 建立 `tina_bootstrap_desktop` 和私有最小 bgfx clear/present；Game SDK/Phase Context 不暴露
  RenderDevice/native/bgfx，Engine Module SPI 只暴露纯 Tina Render 类型。

### M7-C 增量 UI Core 与 Null DisplayList

- 建立 WindowRecord-owned `UIContext`、Window-owned generation `UINodeId`、move-only RootOwner、
  committed hit/paint snapshot、Flex-lite、细粒度 dirty 和持久 PaintCache；
- 实现后端无关 Quad/Text/Clip DisplayList、FramePinSink/capacity rollback 和相邻兼容 batching
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
