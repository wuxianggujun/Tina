# vNext 设计冻结清单

> 状态：候选冻结。架构与接口 P0 已形成推荐决定；固定 hard-gate machine profile 仍需建立，
> 且只有用户确认本轮设计后才创建实现 worktree。

Accepted 决定的理由与代价记录在 [ADR 索引](adr/README.md)，尚未关闭的工程风险记录在
[风险登记](risks.md)。本文是 Proposed/Accepted/Deferred 状态的权威汇总，Roadmap 只描述顺序。

## 已统一的设计

### 产品与技术边界

- Tina 是游戏优先的 2D/3D Runtime，不以编辑器优先；
- Windows/Linux，Visual Studio 2026 / MSVC 19.50 为 Windows 主门禁，目标 C++23 与全链路 UTF-8；
- GLFW + Windows IMM32，不使用 SDL/SDL3；
- bgfx 是首个且唯一真实 Render backend，Game SDK、Tina module public header 和 Phase Context
  均不暴露 RenderDevice/native handle/bgfx；普通游戏只调用 desktop bootstrap；
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
- InputFrame 保留最终设备状态与有序 transitions；UI 使用上一帧稳定布局逐 transition 路由，
  先于 Gameplay Action Mapping 产生消费掩码；
- World RenderScene 与 UI DisplayList 分别冻结，统一组合为 RenderFrame view；Runtime-private owning
  RenderFramePacket 持有 FrameArena/资源 lease/Atlas/surface pin/submit ticket 到 backend
  completion；Renderer 不访问 EnTT；
- Pass 固定从 Opaque3D、Sprite2D、UI、Present 起步；
- Asset 使用弱 Handle、强 Lease、UploadTicket/retirement ledger，以及 IO → CPU Decode → Main
  Completion → GPU Upload 四段路径和三重预算；
- Runtime 只读取 Cooked Asset，Cooker 先验证产物再原子写；
- UI tree retained、UIContext 主线程唯一拥有，输出后端无关 DisplayList；细粒度 dirty、持久
  PaintCache 和稳定 paint/hit snapshot 保证无变化 UI 0布局、0 PaintCache rebuild、0 Tina heap allocation。

### 实施与验证

- 设计冻结后创建独立 `codex/` 分支和 worktree，不复制/stash/提交主工作区差异；
- 双架构迁移期 `TINA_BUILD_LEGACY=ON` 保留当前游戏，并有 vNext-only OFF preset；覆盖完整
  2D/UI/3D/Audio 门禁后翻为 OFF，最终删除选项和旧实现；
- 垂直切片顺序为 Null Runtime → Platform/最小 Surface/UI → Scene/2D → Render/3D → Asset/Cooker →
  Product 2D/UI/Audio → Legacy 删除；
- M7–M9 只使用版本化内置 Cooked fixture/procedural geometry；M7 已建立私有最小 bgfx UI Pass，
  M9 只扩展3D，禁止 UI 临时调用 Legacy renderer；
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
| Backend 组合 | [Proposed](adr/0003-backend-factories.md) | `Create(config, factories)`；production 与 headless/null 由 bootstrap 注入 | 消除 Runtime 对具体 backend 依赖 |
| Runtime/State | [Proposed](adr/0014-runtime-phase-and-state.md) | `IGameApplication` lifecycle-only + `IGameState` 唯一帧入口；Frame Update 后提交状态命令 | 消除名称歧义、双帧入口与首帧 UI 时序冲突 |
| RenderFrame 所有权 | Proposed | 轻量 RenderFrame view 放入 Runtime-private owning RenderFramePacket；Render SPI 只暴露 FramePinSink，资源/Atlas/surface pin 到 completion | 消除依赖环、UI 重复 extraction、backend 越界与在途 UAF |
| UI 增量管线 | Proposed | 细粒度 dirty、一次 layout、持久 PaintCache、稳定 snapshot、相邻兼容 batching | 高性能且保持命中/透明顺序 |
| Frame/Input | [Proposed](adr/0015-input-and-fixed-step.md) | InputFrame 保序、UI 先路由；Action 分 Simulation/Frame domain；每个 substep 独立 commit | 防输入穿透、同帧丢边沿、双重执行和追赶步错误 |
| C++ exception | [Proposed](adr/0004-exceptions-and-errors.md) | 编译开启；公共 API 用 Result/Status，Engine/Frame/Worker/C callback 边界捕获 | pmr/第三方兼容、错误边界 |
| Generation | [Accepted](adr/0019-generation-handles.md) | 32位 generation，回绕 retire；UINodeId 所有构建编码 owner WindowId，Debug cookie 只诊断 | stale/跨 registry 安全 |
| Asset 生命周期 | [Proposed](adr/0016-asset-ownership-and-retirement.md) | 弱 Handle、强 Lease、UploadTicket 和 retirement ledger | GPU/Audio 异步 UAF 防护 |
| ContentHash | [Accepted](adr/0007-standard-containers-and-hash.md) | 版本化 XXH3-128；安全校验未来另选密码学 Hash | Cooker cache 与格式稳定性 |
| Worker 默认 | [Proposed](adr/0017-bounded-task-system.md) | 交互运行默认 `max(1, hardware_threads-1)`；IO 默认1；benchmark 必须显式 worker 数 | 可复现性与扩展性 |
| Trace backend | [Accepted](adr/0002-tracy-and-benchmark.md) | `none|tracy` 编译期 backend，唯一 config/client；Profile 与 Bench 优化语义一致 | 可观测性与依赖边界 |
| Benchmark 协议 | [Proposed](adr/0018-benchmark-protocol.md) | schema v1、workload version/checksum、5进程/10k正式p99、nearest-rank、median+MAD | 可重复回归结论 |
| 固定 hard-gate 机器 | [Proposed](adr/0018-benchmark-protocol.md) | 当前开发机只做 provisional baseline；需创建稳定 machine profile 后才阻断绝对耗时 | 绝对 p99 是否可信 |
| Cooked 布局 | [Accepted](adr/0009-cooked-assets-and-cgltf.md) | 每 Asset 独立文件 + 事务 Manifest + `tina_asset_format`；Bundle/Patch 后置 | Cooker/Runtime 一致性 |

当前真正需要继续讨论并确认的是前四项接口/语义、Asset 强弱所有权、Worker 默认值和
benchmark/hard-gate 规则；GLFW、Tracy、EASTL、bgfx、Cooked/cgltf 与测试调度不再重复摇摆。

## P1：垂直切片内确认

- StaticVector 的首批容量和哪些溢出可安全丢弃；
- Fixed Simulation 首批并行系统和 chunk size；
- FrameArena、Task callable inline、CPU/IO/Main completion queue 默认容量与线程栈预算；
- RenderScene、UI DisplayList 与 upload staging 的默认字节预算；
- 每个 benchmark 的 absolute noise floor、MAD 稳定阈值和 hard-gate machine profile；
- Audio voice/command/completion/stream buffer 容量与 callback period 门禁；
- Linux 正式 Clang/GCC 版本和 sanitizer preset；
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

## 冻结后的第一项实施

第一切片实现 `tina_core + tina_platform(headless) + tina_task + tina_runtime + tina_render/
NullRenderDevice + tina_tests + tina_bench + tina_sample_null`，并建立 `scene/asset/ui/audio` 被最终
Phase Context 和关闭顺序需要的最小契约/Empty 或 Disabled 生命周期壳。它们只允许返回空集合、
Unavailable 或 Disabled，不实现 World、加载、Widget、Glyph 或真实设备，也不链接 EnTT、
FreeType、miniaudio、asset format/cgltf。

该切片一次固定最终 EngineHost 所有权、Frame Pipeline、Metrics、失败注入和 shutdown 顺序，
完成300帧/10,000帧无泄漏运行。Trace backend 固定为 `none`，且不链接 GLFW/bgfx/EnTT/
FreeType/miniaudio/Tracy/cgltf；不迁移现有2D、UI 或 Asset 内容，避免首个提交失去可定位性。
