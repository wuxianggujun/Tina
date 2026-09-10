# Architecture Decision Records

ADR 记录处于提议、接受、被替代或拒绝状态的架构决定。主题文档描述完整设计，ADR 只保存“为什么选择”和
“代价是什么”。状态只使用 Proposed、Accepted、Superseded、Rejected。

| ADR | 状态 | 决定 |
| --- | --- | --- |
| [0059](0059-isometric-2d-extraction.md) | Accepted | 单轨 Sprite2D 仿射 quad、共享相机投影与独立空间深度；Tile/FX/Editor 全链路一致，World2D v7 无损保存相机 basis |
| [0057](0057-retryable-audio-shutdown.md) | Accepted | Audio realtime shutdown 纳入 Host 剩余 deadline；超时保留 Stopping voice/PCM/stream 与 module owner，callback 退出后原 owner 重试 |
| [0058](0058-asset-system-stable-borrow.md) | Accepted | AssetSystem move 保留 Store/异步请求状态；长期持有 facade 指针的 owner 使用稳定 borrow，borrow 存在时拒绝 facade move |
| [0056](0056-resource-residency-and-window-snapshots.md) | Accepted | CPU 驻留与 GPU 实例分账，registry 不隐式 unload 共享 Asset；phase 返回已提交窗口 metrics 值快照 |
| [0055](0055-single-runtime-archive.md) | Accepted | 单一 `Tina::GameSDK` 实体静态库；内部 OBJECT 分组不导出；能力校验替代模块库选择；0.1.0、配置隔离、build-id 与内容清单 |
| [0054](0054-gameplay3d-scene-runtime.md) | Accepted | `Gameplay3D::Scene3DRuntime` 由产品拥有一个 Prefab v5 Scene 实例的 animation/physics/resource 生命周期；Editor Play 使用隔离 World、authored camera 且 Stop 不污染 document |
| [0053](0053-retryable-host-shutdown.md) | Accepted | Host timeout 保留 Stopping owner；所有 State 先取消、共享 deadline join，再执行退出回调与 backend teardown；owner-thread stop 重试 |
| [0052](0052-demand-driven-memory-policy.md) | Accepted / 实现迁移中 | 普通内容按需增长、热路径复用、缓存预算化；固定容量不再是全引擎不变量，保留寿命/事务/硬件边界 |
| [0051](0051-shaped-msdf-text-and-layout-constraints.md) | Accepted（容量策略由 0052 部分修订） | HarfBuzz + BiDi、按需 MSDF / 彩色 Emoji、有界 glyph/string cache、精确字形投影、统一布局约束与 UIPanel 组合背景 |
| [0050](0050-jolt-physics3d-floating-origin.md) | Proposed | Jolt 5.5.0 私有 Physics3D、单 owner/fixed step、double global + float local 的显式 floating origin；全体 body 预检查与一次 origin revision，Scene/Render 同步由 game owner 完成 |
| [0046](0046-render-device-borrow-in-phase-contexts.md) | Accepted | `IRenderDevice` 借用进入三个 phase context：Enter/Exit 给 `IRenderDevice&`（host-lifetime，可记下地址给非相位 helper），Frame 给可空指针且仅栈顶。取代样例与 Editor 各自用 `wrapWindowSurfaceRenderDevice` 抓指针的 `DeviceCapture` 绕道；telemetry 装饰器（`2d_tilemap_bgfx`、`3d_product`）保留，因为它们提供 `requestCaptureNextPresent()` 与 post-run 统计。`DisplaySettings` 继续只有 vsync。代价是资源 API 全面对 State 可见，包括不该由 State 调用的 `shutdown()`/`submitFrame()`/`present()` |
| [0049](0049-ai-decision-layer.md) | Accepted | `Tina::AI` 独立提供 typed Blackboard、BehaviorTree 与 AI FSM；不依赖 Scene/Navigation，不拥有线程和时钟，回调由 owner 驱动并受预算/重入/生命周期契约约束 |
| [0001](0001-vnext-vertical-slices.md) | Accepted | 完整 vNext 目标，通过垂直切片迁移 |
| [0002](0002-tracy-and-benchmark.md) | Accepted | Tina Trace + Tracy 定位，tina_bench 回归 |
| [0003](0003-backend-factories.md) | Accepted | 具体 backend 由 bootstrap factory 注入 |
| [0004](0004-exceptions-and-errors.md) | Accepted | 保持 C++ exception，模块边界转 Result/Status |
| [0005](0005-glfw-without-sdl.md) | Accepted | GLFW + 原生窄适配，不引入 SDL/SDL3 |
| [0006](0006-direct-googletest.md) | Accepted | 直接运行 GoogleTest，不使用 CTest 调度 |
| [0007](0007-standard-containers-and-hash.md) | Accepted | vNext 不使用 EASTL，xxHash 保持私有 |
| [0008](0008-bgfx-render-backend.md) | Accepted | bgfx 是首个且唯一真实渲染后端 |
| [0009](0009-cooked-assets-and-cgltf.md) | Accepted | Runtime 只读 Cooked Asset，cgltf 只在 Cooker |
| [0010](0010-separate-physics-backends.md) | Accepted | Box2D/Jolt 分离，不强行统一物理 API |
| [0011](0011-retained-ui.md) | Accepted | 自研 Retained UI，输出后端无关 DisplayList |
| [0012](0012-miniaudio-backend.md) | Accepted | miniaudio 是唯一真实音频后端 |
| [0013](0013-entt-internal-storage.md) | Accepted | EnTT 只作为 Scene 内部 ECS 存储 |
| [0014](0014-runtime-phase-and-state.md) | Accepted | IGameApplication 程序入口 + IGameState 唯一帧状态接口 |
| [0015](0015-input-and-fixed-step.md) | Accepted | PlatformFrameView、Action domain 与逐 substep 提交 |
| [0016](0016-asset-ownership-and-retirement.md) | Accepted | 弱 Handle、强 Lease 与物理退役账本（Null staging、Texture/Mesh readback marker、AssetLease pin 与 drain 已落地） |
| [0017](0017-bounded-task-system.md) | Accepted | 有界 IO/CPU/Main 与 TaskGroup 已落地；Desktop 交互默认 `max(1, hw-1)`（TASK-001 Done） |
| [0018](0018-benchmark-protocol.md) | Accepted | 版本化 benchmark 协议；`tina_bench` schema v1 首切片 |
| [0019](0019-generation-handles.md) | Accepted | 强类型 generation handle + owner 边界 |
| [0020](0020-window-surface-handoff.md) | Accepted | 主窗口、move-only native surface lease 与 bgfx 交接 |
| [0021](0021-runtime-ui-startup-capability.md) | Accepted | 主窗口 UI 启动事务与 root/phase-scoped Game SDK 能力 |
| [0022](0022-ui-element-authoring-and-layout.md) | Accepted | Element 组合 authoring、父/子布局分离与统一 committed 内容放置 |
| [0023](0023-ui-extensibility-style-paint-motion.md) | Accepted | Component/Behavior 扩展、node-local stylesheet、Image/Icon/NineSlice 与 paint-only Motion |
| [0024](0024-sdk-abi-compatibility.md) | Accepted（发布布局与迁移方式由 0055 部分替代） | SDK pre-1.0 版本、compatibility tuple、baseline 与发布兼容证据 |
| [0025](0025-ui-line-and-ellipse-primitives.md) | Accepted | UI Line exact quad 与 Ellipse coverage 图元，删除阶梯/多段弦近似 |
| [0026](0026-ui-keyframe-timeline-and-layout-animation.md) | Accepted | 每窗口 fixed-capacity keyframe timeline 与 Layout/Hit/Paint 原子动画边界 |
| [0027](0027-runtime-metrics-registry.md) | Proposed | Runtime Metrics 固定容量 counter registry：Core 类型、EngineHost 唯一产品 owner、u64 counter 首切片 |
| [0028](0028-ui-fixed-capacity-grid-layout.md) | Accepted | Flex/Grid 并存的固定8x8 `Px/Auto/Fr` 普通容器布局 |
| [0029](0029-ui-layout-debugger.md) | Accepted | Release 可用的固定容量 UI layout snapshot、精确拾取与 frame-local overlay |
| [0030](0030-gameplay-2d-binding-and-physics-bridge.md) | Proposed | World 保持封闭 + `World2DSceneIndex` 关联；authored payload 不得静默丢弃；`tina_gameplay2d` 单向 physics 桥 |
| [0031](0031-scene-2d-runtime-ownership.md) | Proposed | `Scene2DRuntime` 拥有四种 authored resource 节点的实例化、lease 生命周期与每帧顺序 |
| [0032](0032-mobile-platform-contract-boundaries.md) | Proposed | 移动端（Android/iOS）需要扩宽的六个桌面契约、先扩契约后写后端的顺序，以及删除死的 shader cmake |
| [0033](0033-network-module-boundaries.md) | Accepted | 传输统一用 owner-thread readiness 多路复用而非 worker 池；DNS 是唯一线程例外且用 `scheduleIo` 而非 `postMain`；TLS 信任锚取平台 store、不内嵌 bundle |
| [0034](0034-native-surface-rebind.md) | Accepted | native binding 变化是 surface **事件**而非 `Suspended` 状态；bgfx 实测允许 init 后更换 nwh，故 rebind 只需 `setPlatformData` + `reset`，device 资源与 Lease 全部存活。取代 ADR 0020 的「不支持 live native rebind」 |
| [0035](0035-math-module-boundaries.md) | Accepted | `Tina::Math` 是几何类型的唯一定义点；header-only、列主序右手系、失败用 `optional` 不占 `ErrorDomain`，旧 `Scene::Vec3`/`PhysicsVec2` 直接删除 |
| [0036](0036-gameplay-tooling-boundaries.md) | Accepted | `Tina::Gameplay` 只依赖 Core+Math 提供 timer/tween/sequence 与 `Signal<T>`；delta 由调用方给、余量携带而积压丢弃并计数、重入返回 `ReentrantDispatch`；tween 写目标是 setter 回调而非 Scene 属性枚举 |
| [0037](0037-animation3d-graph-boundaries.md) | Accepted | `Tina::Animation3D` 在 `Animator3D` 旁建立 pose 图：local-space pose、crossfade/状态机/blend tree/layer+mask/root motion/两骨 IK；SkinnedMesh wire 提到 v2 加骨骼名称，因 cooked joint index 是不可反推的排列 |
| [0038](0038-json-writer-without-json-library.md) | Accepted（历史） | 记录最初的手写 header-only `Core::JsonWriter` 迁移背景与诊断报告字节约束；实现选择已由 0047 统一迁移到 nlohmann/json |
| [0047](0047-nlohmann-json-document.md) | Accepted | 内置 nlohmann/json v3.11.3 作为唯一通用 JSON 解析与序列化后端；`Core::JsonDocument`/`JsonWriter` 对外提供 Tina-owned API，nlohmann 类型只留在两个 core `.cpp`；writer 使用 ordered DOM、紧凑 dump 与 failed 错误边界 |
| [0039](0039-logging-frontend-and-async-sinks.md) | Accepted | 日志前端按编译期常量剥离且不用 `__VA_OPT__`；`LogRecord` 自持 256B 内联缓冲以支持异步；单 drain 线程 + 有界队列丢弃最新并计数；Error 及以上同步 flush（149 处 `std::terminate()`）；console/file/platform 三 sink 叠加。实测：异步只在 sink 够慢时才更快 |
| [0040](0040-path-util-single-ordinal-semantics.md) | Accepted | 路径包含判定统一为序数折叠（Windows 用 `CompareStringOrdinal`，与 NTFS 一致），取代逐 `wchar_t` 的 `towlower`（locale 敏感、无法折叠代理对）；`PathUtil.hpp` 留在 `src/` 私有，因 `<filesystem>` 不进公共 SDK 面；三处刻意不统一：大小写敏感比较、`.tmeta` 持久化的 towlower 变换、Windows 保留设备名检查 |
| [0041](0041-editor-module-boundaries.md) | Accepted | 编辑器移出 `src/` 成顶层 `editor/` 树，位于引擎之上因而允许依赖产品模块（`tina_editor` → `Tina::Asset` 如实入文档）；`TINA_BUILD_EDITOR` 默认 `PROJECT_IS_TOP_LEVEL`，Android 不再为 arm64 编译 authoring 代码；退出 SDK 包（breaking，无兼容转发）。因需引擎私有头 `core/io/PathUtil.hpp` 而不能作为独立工程或独立仓库 |
| [0042](0042-scene-owned-clear-color.md) | Proposed | 清屏色从 bgfx 后端的 `kClearRgba` 常量搬到场景：`RenderSceneView` 每帧带非可选的 `RenderLinearColor clearColor`，`setClearColor()` 一帧只准设一次；接口收**线性**色（与 `baseColorFactor`、光照同标尺），后端负责 sRGB 编码。默认值往返后逐字节还原 `0x102a43`，故既有样例与 gate 输出不变。天空盒是另一件事，本 ADR 不碰被冻结的 `RenderPassKind` |
| [0043](0043-mesh3d-emissive-factor.md) | Proposed | `Mesh3DMaterialBindingDesc` 增加 `emissiveFactorR/G/B`，走 `u_emissiveFactor`，shader 在 ambient/IBL 之后 `lit += emissive`。放 material 而非 per-instance（glTF 如此，且静态/骨骼/透明共用一个 fs）。校验 finite 且非负但**不设上限**：它是 radiance，与光照同标尺，不是 `[0,1]` 的 BRDF 参数。默认 0 故不改变任何既有材质。不含 emissive 贴图；无 tone mapping，超过 1 直接 clamp 成白，故不承诺光晕 |
| [0044](0044-csm-stable-cascade-bounds.md) | Proposed | 级联横向窗口从 frustum 八角点紧 AABB 改为 slice **外接球**，中心在世界原点锚定的 light space 中吸附到整数个 atlas texel（+1 texel 余量）。两者必须同时改：紧 AABB 的尺寸随相机朝向变，栅格间距每帧不同，单加吸附无跨帧含义；light view 锚在相机上则参考系自身滑动。深度轴刻意不吸附也不用外接球（不光栅化、caster/receiver 同变换相互抵消，放宽会拖进 250m 太阳 billboard）。代价是同分辨率下阴影更粗，稳定性换清晰度 |
| [0045](0045-script-module-boundaries.md) | Proposed | 玩法脚本是 `IGameState` 的客人而非第二套引擎：只做 Luau、可选 `Tina::Script`、cooked `require` 依赖图、白名单宿主、每相位预算；v1 无 coroutine。确认前不占位 API |
| [0048](0048-navigation3d-voxel-volume-boundaries.md) | Proposed | 3D 导航首个切片做**体素体积**而非 navmesh：当前唯一的 3D 可行走场景本身就是体素，三角化再让 Recast 重新体素化是白绕一圈（navmesh 另立 ADR，两者共享的只有 agent 参数词汇）。可站立性**按 agent profile 派生不烘进体积**（一个世界服务多种体型），判据为自身与上方 `heightCells-1` 格净空 + 下方一格实心。越界格**一律非实心**，故上方是天空、`y=0` 下方是无支撑 —— 没有隐式地板，全空体积彻底不可站立而非伪装成一层平面。动态覆盖层改**实心**不改可通行（体素世界里变化的是方块，它既阻挡又支撑），故放方块会新增其顶上的可站立格。垂直移动仅四向且对角要求两侧正交列够 agent 全高。占 `ErrorDomain::Navigation3D = 21`，不占 `MemoryTag`。不含任意三角面几何、多格占地、crowd 与引擎侧 Agent |

新增 ADR 从 [模板](0000-template.md) 复制。替代旧决定时新建 ADR，并把旧记录状态改为
Superseded 和链接新编号；不要改写历史理由。
