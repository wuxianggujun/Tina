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

实施状态（2026-07-17）：前两批已完成。Tina 自有 target 已统一 C++23；新增 `TINA_BUILD_LEGACY`、`TINA_BUILD_RENDER_BGFX`、`TINA_BUILD_BENCHMARKS`；vcpkg Legacy feature、`windows-msvc-vnext` 与 `linux-gcc13-vnext` 已建立。Core 已迁到独立 `include/tina` 公共面，Result/Status 已使用 `std::expected`，稳定 Error domain/code/context、可注入 Monotonic Clock 和 FixedStepAccumulator 已落地。Windows vNext 最小图 14/14、Legacy Debug 57/57、UI/3D 各300帧通过。Linux 正式 C++23 工具链和下列 Runtime/Core 能力仍按本里程碑继续实施，不能因 Core 地基通过而标记 M6 完成。

- 建立 `EngineHost::Create(config, factories)`、EngineConfig、阶段 Context、lifecycle-only
  `IGameApplication` 和唯一帧入口 `IGameState`；删除候选公共 `IFrameClient`；
- 初始化阶段支持失败注入，并覆盖任意失败点逆序回滚、析构顺序、重复 shutdown；
- 接入可注入 Clock、固定60 Hz/最多4步、唯一 Frame Phase 和阶段指标；其中 Clock 与固定步
  accumulator 已完成，EngineHost Frame Pipeline 接线待后续批次；
- 建立 Headless Platform、TaskSystem、FrameArena、GenerationPool 和 MemoryTag；专用容器以
  首个消费者为触发点；
- 实现 NullRenderDevice、typed generation handle、最小 Pass Scheduler，以及
  `RenderFrame = World RenderScene + UIDisplayList + RenderSurfaceState + timing`；Runtime private
  `RenderFramePacket`/pool 负责保活到 submission completion；
- 建立 Scene/Asset/UI/Audio 的最小公共契约与 Empty/Disabled 生命周期壳，只为一次固定最终
  Context/初始化/关闭顺序；不接 EnTT、FreeType、miniaudio、Cooked format 或产品功能；
- 增加 `tina_sample_null` 与 `tina_bench` schema v1；无 GLFW/bgfx/EnTT/FreeType/miniaudio/
  Tracy/cgltf 依赖连续
  运行300帧和10,000帧并直接通过 GoogleTest。
- Null/Bench 稳定后用独立提交加入 `tina_profile_tracy` 与 Bench-equivalent Profile preset，
  验证 zone/frame/thread capture、正常 shutdown、唯一 Client 和 Tracy/Metrics A/B；发布/bench
  仍保持 backend none。

## M7 Platform、最小 Surface 与高性能 UI 垂直切片

- 实现 `tina_platform_glfw` 并迁移 GLFW Window/Input，不引入 SDL/SDL3；Windows IME 继续
  只使用 IMM32；
- 建立 `tina_bootstrap_desktop` 和私有最小 `tina_render_bgfx` Surface/UI Pass；Game SDK、
  `tina_ui` public header 和 `IGameState` 不 include/link bgfx；
- 保留独立 InputFrame（最终 Snapshot + 有序 transitions）、Event Queue 和 UI routed event；
- 建立 WindowRecord-owned UIContext、带 owner 的 generation UINodeId、move-only RootOwner、committed
  paint/hit snapshot、细粒度 dirty、Flex-lite、持久 PaintCache 和后端无关 DisplayList；
- 迁移 Focus/Capture、Modal、Theme/DPI、中文字体和 TextEdit/IME；文本 layout 与 glyph raster
  分离，Atlas 具有 generation/预算/retirement；
- 使用版本化内置 Cooked Font/Texture fixture 形成 Label、Button、Modal 样例；禁止 Runtime
  路径加载和 Legacy UIRenderer；
- 为窗口失焦/销毁、composition、手柄回滞和输入重复建立自动化门禁。

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
- Linux/GCC 告警先区分第三方与 Tina 自身，再按模块清理；Clang ASan/UBSan 需要可复现 preset 和实际门禁结果。
