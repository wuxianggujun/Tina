# Tina 设计状态清单

本文件汇总当前设计状态，不保存实施流水。ADR 是决策理由的权威记录；源码/CMake/测试是实现事实的
权威记录。替代 Accepted 决策必须新增 ADR，并把旧 ADR 标为 Superseded。

## 状态定义

| 状态 | 含义 |
| --- | --- |
| Accepted | 已接受的架构约束；实现可能分阶段完成 |
| Proposed | 候选决定，不能当作现有契约 |
| Deferred | 当前明确不实施，进入 Backlog Later |
| Superseded / Rejected | 仅在对应 ADR 中记录历史 |

设计状态与实现状态分开。`Accepted + Partial` 表示决定已经生效，但所有切片尚未落完。

## Accepted

| 领域 | 决定 | ADR | 实现状态 |
| --- | --- | --- | --- |
| 迁移 | 完整目标、小步垂直切片、持续可运行 | [0001](adr/0001-vnext-vertical-slices.md) | Implemented |
| Profiling | Tina Trace/Metrics；Tracy 用于定位，bench 用于回归 | [0002](adr/0002-tracy-and-benchmark.md) | Partial：None frontend、Tracy 0.13.1 64-byte opaque zone adapter、Runtime phase consumer 与 `tina_bench` schema v1 已落地；Metrics 与 session/capture 控制面仍后置，Metrics 契约提案见 Proposed [0027](adr/0027-runtime-metrics-registry.md) |
| 组合 | backend factory + 非全局 `EngineHost` | [0003](adr/0003-backend-factories.md) | Implemented |
| 错误 | 内部可用 exception，模块边界转 `Result`/`Status` | [0004](adr/0004-exceptions-and-errors.md) | Implemented；Core 另提供应用显式安装的最后故障文本报告，`EngineHost` 不隐式取得进程 handler owner |
| Platform | GLFW + 窄原生适配，不引入 SDL/SDL3 | [0005](adr/0005-glfw-without-sdl.md) | Implemented |
| 测试 | 直接运行 GoogleTest，不用 CTest 调度 | [0006](adr/0006-direct-googletest.md) | Implemented |
| 容器/Hash | 标准库/PMR，不使用 EASTL；xxHash 私有 | [0007](adr/0007-standard-containers-and-hash.md) | Implemented |
| Render | bgfx 是首个真实 backend，保持私有 | [0008](adr/0008-bgfx-render-backend.md) | Implemented |
| Asset | Runtime 只读 Cooked；cgltf 只在 Cooker | [0009](adr/0009-cooked-assets-and-cgltf.md) | Implemented；baseColor/MR/normal Texture2D cook + 外部 URI 安全 + 产品 material binding；EnvironmentMap cooked payload/publication/typed parse 与 bgfx Opaque3D Cook-Torrance GGX/split-sum IBL 已落地 |
| Physics | Box2D 与 Jolt API 分离 | [0010](adr/0010-separate-physics-backends.md) | Box2D implemented：Box/Circle/Capsule/ConvexPolygon/Chain、sensor、Distance/Revolute/Prismatic joint；Jolt deferred |
| UI | Tina Retained UI 输出后端无关 DisplayList | [0011](adr/0011-retained-ui.md) | Implemented product slice；UI-004 Focus Scope/Modal/Pointer Capture 与 UI-005 ScrollView/Dropdown/Popup/虚拟 ListView/TreeView 已完成；accessibility action seam + Windows UIA Invoke/Toggle/RangeValue/Value patterns 已落地 |
| Audio | miniaudio 是唯一真实 audio backend | [0012](adr/0012-miniaudio-backend.md) | Implemented optional adapter |
| ECS | 若使用 EnTT，只能是 Scene 私有存储 | [0013](adr/0013-entt-internal-storage.md) | Not used：当前 Scene 不链接 EnTT |
| Runtime | `IGameApplication` lifecycle + `IGameState` frame behavior | [0014](adr/0014-runtime-phase-and-state.md) | stack/commands + 四相位阻断 + `blocksGameplayInputBelow` 空 snapshot 已落地；`blocksUIUpdateBelow` 仅拦 `updateUI`（不回改当帧 UI route） |
| Input | ordered PlatformFrame、Action domain、逐 substep 提交 | [0015](adr/0015-input-and-fixed-step.md) | Implemented foundation |
| Asset 生命周期 | 弱 Handle、强 Lease、物理 retirement | [0016](adr/0016-asset-ownership-and-retirement.md) | CPU/Null upload ledger + 独立 present-return CPU completion；bgfx readTexture completion marker、AssetLease→Texture/Mesh retirement、EnvironmentMap GPU-owner retirement 与 suspend/shutdown drain 已落地 |
| Task | 有界 CPU/IO/Main、TaskGroup、禁止 detach/强杀 | [0017](adr/0017-bounded-task-system.md) | Implemented；Desktop 交互默认 `max(1, hw-1)`（TASK-001 Done），工厂 0=IO-only |
| Benchmark | 版本化 JSON、fingerprint、provisional vs hard gate | [0018](adr/0018-benchmark-protocol.md) | Accepted 首切片：`tina_bench` schema v1；固定机 hard gate / 多进程 MAD 后置 |
| Handle | 强类型 generation + owner 边界 | [0019](adr/0019-generation-handles.md) | Implemented across current modules |
| WindowSurface | move-only native lease 与主窗口交接 | [0020](adr/0020-window-surface-handoff.md) | Implemented |
| Runtime UI | startup transaction + root/phase-scoped capability | [0021](adr/0021-runtime-ui-startup-capability.md) | Implemented |
| UI Authoring | Element 组合 authoring、父/子布局分离与统一 committed 内容放置 | [0022](adr/0022-ui-element-authoring-and-layout.md) | Implemented：descriptor/recipe、Flow/Flex（含 Wrap/alignContent）、父 content width bounded responsive rules（含 spacing/minMax）、普通 Label width wrapping/line clamp、aspect ratio、min/max-content、Semantics、StyleRole/reset、bounded build transaction、`SolidRect` Canvas 与统一 RoundedRect 已落地；Desktop Shell 已删除手工 width tier |
| UI 扩展 | 可组合标准 Behavior、bounded Component、node-local StyleSheet、Image/Icon/NineSlice 与 paint-only Motion | [0023](adr/0023-ui-extensibility-style-paint-motion.md) | Implemented：IMAGE/COMPONENT/STYLE/MOTION 均 Done；paint-only tracks、声明式 Style BackgroundColor persistent reservation/activation 与 `ui_motion_v1` 已落地；后续 Visual 增强不改变本决定的实现状态 |
| SDK 发布 | `0.y.z` compatibility epoch、tuple-scoped 静态 C++ 兼容、API/symbol baseline 与 previous-release probe | [0024](adr/0024-sdk-abi-compatibility.md) | Accepted；当前 package 已用 strict exact-version ConfigVersion 对三段 exact、相邻版本、tweak/range 正反 probe fail closed。正式 supported tuple 仍需 release artifact/baseline/object probe |
| UI 绘制图元 | Line 使用 exact 四顶点投影，Ellipse 使用解析 coverage；不保留阶梯折线、弦环或 rotated-quad 兼容 API | [0025](adr/0025-ui-line-and-ellipse-primitives.md) | 代码已实现：Box/Canvas authoring、committed paint、DisplayList、bgfx 与 Editor grid/gizmo 已贯通；100%/150%/200% Editor/UI-003 视觉证据已完成，`RENDER-LINES-001` 在跨 GPU UI-003 证据完成前保持 InProgress |
| UI Keyframe | 每窗口 fixed-capacity timeline、唯一 monotonic clock、presentation owner，以及 Layout/Hit/Paint 原子 layout-animation 边界 | [0026](adr/0026-ui-keyframe-timeline-and-layout-animation.md) | Accepted/Implemented：paint 与 bounded `LayoutWidth`/`LayoutHeight`/`LayoutOffset` timeline、跨 motion candidate transaction、Runtime facade 与两个 workload 均已落地；UI/Runtime/bench unit 及 seed 0/1/2 确定性 gate 已通过 |
| UI Grid | Flex/Grid 并存；普通 Flow child 使用固定8x8 `Px/Auto/Fr` track、span、row-major auto placement 与 per-item alignment | [0028](adr/0028-ui-fixed-capacity-grid-layout.md) | Implemented：公开 layout 契约、normalization、私有 Measure/Arrange、header isolation 与 Editor Inspector consumer 已落地；不替代 VirtualGridView/DataGrid |
| UI Layout Debugger | 始终编译的 fixed-capacity committed layout snapshot、Runtime 精确拾取与 frame-local Render overlay | [0029](adr/0029-ui-layout-debugger.md) | Implemented：UI capability、双缓冲原子 publication、phase facade、pointer-hit query、subtree exclusion、DisplayList overlay 与 Editor DevTools consumer 已落地；容量默认关闭，产品显式启用 |
| Native surface rebind | native binding 变化是 surface **事件**（`nativeBindingRevision`）而非 `Suspended` 状态；rebind 只重建 surface/swapchain，device 资源与 `AssetLease` 全部存活 | [0034](adr/0034-native-surface-rebind.md) | Implemented：tracker 校验（递增须伴随新 surface/metrics revision，后退与 0 拒绝）+ latch 通知 backend；bgfx 用 `setPlatformData` + 强制 reset，不 `shutdown()`；未新增 `IRenderDevice` 纯虚、未新增 drain 协议。取代 [0020](adr/0020-window-surface-handoff.md) 的「不支持 live native rebind」。**待**：Android 真机验证 |
| Math | `Tina::Math` 是几何类型的唯一定义点；header-only、列主序右手系、clip 深度范围显式传入、失败用 `optional`/`bool` 故不占 `ErrorDomain`/`MemoryTag`；旧重复定义直接删除不留别名 | [0035](adr/0035-math-module-boundaries.md) | Implemented（`MATH-001`）：七个公开头、依赖接线、全部调用点迁移与四个数值等价性回归已落地；`Scene::Vec3`/`Vec2`/`Quaternion`、`PhysicsVec2` 与三处私有副本已删除。有意保留 `UI::UILogicalRect`、bgfx 后端的 `bx::Vec3` 与 `RenderScene.cpp` 的 float 精度球-视锥测试。2026-08-31 统一 gate 全绿，2D/3D 产品证据数字未变（见 [Backlog](backlog.md) `MATH-001`）。`Ray` 的生产者已由 `EDITOR-PICK-RAY-001` 补齐（Editor 3D 单击拾取）。**待**：`OBB`/`Mat3`/SIMD 明确不在范围 |
| Gameplay 工具层 | `Tina::Gameplay` 只依赖 Core+Math 提供 timer/tween/sequence 与 `Signal<T>`；delta 由调用方给、余量携带而积压丢弃并计数、dispatch 重入返回 `ReentrantDispatch`；tween 写目标是 setter 回调而非 Scene 属性枚举 | [0036](adr/0036-gameplay-tooling-boundaries.md) | Implemented（源码与安装 package 已落地）：`Easing`（28 曲线）、`Scheduler`/`TimerId`、`Action`/`ActionRunner`、`Signal<T>`/`SignalSubscription`、`Repeat`；占用 `ErrorDomain::Gameplay = 17` 与 `MemoryTag::Gameplay = 14`（`MemoryTagCount` 现为 15）。**待**：单元测试与 sample 消费面。明确不在范围：coroutine、tween 的 relative/reverse/speed 变体 |
| 3D 动画图 | `Tina::Animation3D` 在 `Animator3D` 旁建立 pose 图，不替代也不迁移它；pose 为 joint-local（混合 global 会拉伸肢体）、additive 参考姿势无默认、root motion 从 pose 中移除并单独上报、基础层不可 mask；SkinnedMesh wire v2 加骨骼名称，因 cooked joint index 是 cooker 派生的不可反推排列 | [0037](adr/0037-animation3d-graph-boundaries.md) | Implemented：`Skeleton3D`/`Pose3D`/`JointMask`、`PoseBlend3D`、`ClipSampler3D`（三播放模式 + 负速度）、`BlendTree3D`（Clip/Blend2/Blend1D/Additive）、`AnimationGraph3D`（状态机 + crossfade + layer/mask + root motion）、两骨 IK 均已落地并接入安装 package；占用 `ErrorDomain::Animation3D = 18` 与 `MemoryTag::Animation3D = 15`（`MemoryTagCount` 现为 16）。2026-08-31 证据：`tina_animation3d_tests` 28/28、`tina_asset_format_tests` 126/126、`tina_asset_tests` 325/325、`tina_tests` 455/455、`tina_scene_tests` 181/181。明确不在范围：retargeting（v2 名称已解阻塞但无消费者）、morph target、2D blend space、pose-aware bounds |

## Proposed

当前未决的 Proposed ADR 有四个。新的候选决定必须先新增 ADR，不能只写入主题文档。

| ADR | 范围 | 实现状态 |
| --- | --- | --- |
| [0027](adr/0027-runtime-metrics-registry.md) | Runtime Metrics 固定容量 counter registry：owner/生命周期、counter 模型、注册身份与线程模型、热路径错误、容量、snapshot 语义、编译开关等关键决策待确认 | 未实现：Accepted 前不建立占位 API，不修改 Runtime |
| [0030](adr/0030-gameplay-2d-binding-and-physics-bridge.md) | World 保持封闭 + `World2DSceneIndex` 关联；authored payload 不得静默丢弃；`tina_gameplay2d` 单向 physics 桥 | **已实现**（`GAMEPLAY2D-001`）。与 0027 不同，本 ADR 的代码先落地了：payload 静默丢弃是正确性缺陷，不能等待审阅。maintainer 审阅可能要求调整已实施部分 |
| [0031](adr/0031-scene-2d-runtime-ownership.md) | `Scene2DRuntime` 拥有四种 authored resource 节点的实例化、lease 生命周期与每帧顺序 | **已实现**（`GAMEPLAY2D-001`）：D1-D7 全部落地，含 D5 的 `fixedUpdatePhysics()`。`samples/2d_authored_scene` 已链接 `Tina::Gameplay2D`（`samples/2d_authored_scene/CMakeLists.txt:10`，在 `TINA_BUILD_PHYSICS2D` guard 下接入 `samples/CMakeLists.txt`），故该 owner 已有 sample 消费面。**待**：两个 ADR 的 maintainer 审阅 |
| [0032](adr/0032-mobile-platform-contract-boundaries.md) | 移动端（Android/iOS）需要扩宽的六个桌面契约、先扩契约后写后端的顺序 | **部分实现**（`MOBILE-001`）：不实现任何后端，但切片 A 三项桌面契约扩宽均已落地 —— pointer presence（`3569f0b4`）、多点触控路由（`4ec987a9`，UI 侧 per-pointer `PointerInteractionState` 表 + save/restore 交换）与 per-pointer cancel 作用域（`8479c360`）。**D3 已定（2026-08-28）**：maintainer 选择外部驱动 `start()`/`tick()`，未采纳 ADR 原推荐的「保持阻塞 `run()`、后端内部适配」；`run()` 保留为 `while` 封装，75 处调用点零改动，ADR 0014 的四相位阻断不受影响。切片 B 三项**均已完成**：surface 重建事件转由 [0034](adr/0034-native-surface-rebind.md) 承载并已接线（该 ADR 落地时 `nativeBindingRevision` 从未被生产代码赋值，属「已发布无生产者」，见其「后续修正」节）、ESSL profile（`TINA_RENDER_BGFX_MOBILE_SHADERS`；Metal 仍未做）、C6 软键盘（走 Android 专属 `IAndroidPlatformBackend`，不给 `IPlatformBackend` 加纯虚）。Android 平台后端已存在：交叉编译含 bgfx backend（NDK 28/29 × arm64-v8a/x86_64，16 个静态库零 error；交叉构建用 `TINA_BGFX_SHADERC_EXECUTABLE` 从宿主树导入 shaderc），窗口 + 触摸 + 生命周期 + 软键盘可用，`tina_platform_android_tests` **80/80** 在 Android 36 x86_64 模拟器实机通过。**已补齐（2026-08-29/30）：** 按键、committed text（含 emoji，须用 `GetStringChars` 而非 modified UTF-8 的 `GetStringUTFChars`）、**preedit 组词文本**、JNI 绑定（`RegisterNatives` 显式注册）、gradle 工程与可安装 APK。preedit 的四阶段映射**先写成 ADR 补充再实现**（见 0032「C6 补充」）：Java 侧不持有 composition 状态；commit 与 preedit 共用一条队列，因为 `Ended` 必须先于它产生的文本而两条队列会被依次排空。**`updateTextInputPlacement` 由「拒绝非空」改为 latch caret**：原行为会在 TextEdit 聚焦时让 `tick()` 终止并被 latch，**帧循环永久停止** —— 属「诚实拒绝一个未实现的能力，实际代价是应用冻结」，此前未暴露只因 demo 里没有 TextEdit。Android 的 caret 协议是 `CursorAnchorInfo`（候选窗属输入法进程，应用只能上报几何），故与软键盘同构：引擎 latch、宿主执行，读取不清除，且只在 `requestCursorUpdates()` 后上报。**仍缺：** 真机 Vulkan 路径、候选窗跟随的人工验收（归 `TEXT-001`）、iOS。另已删除死的 `cmake/ShaderUtils.cmake` |

「Proposed 但已实现」是明确的例外状态而非常态：它表示实现事实已存在、决策理由尚未被批准，因此
`design-freeze.md` 的 Accepted 表不收录它们，且不得被引用为既定契约。

固定机 hard-gate / 多进程 MAD 等实现尾巴记在 [Backlog](backlog.md)（PERF-002），不单独占 Proposed 行。
`UI-FLOW-001` 复用现有 retained node：Layer 是 root 直接子节点，Screen 是 Layer 直接子节点，固定容量栈
只发布栈顶 Screen。第二个产品切片冻结 Pause `Back` Action，第四个产品切片加入 `Confirm`，第五个产品
切片加入 `Menu`；每个 action 拥有一个 fixed-inline callback，注册总量受 `flowScreenCapacity` 约束。Dropdown
dismiss 优先于 Back；已聚焦控件默认 Activate 优先于 Confirm；TextEdit 聚焦时可打印的 `P` Down 优先进入
文本输入。随后才向 committed layout 中最上层 active Screen 路由 Escape/Gamepad East、Enter/Keypad
Enter/Gamepad South 或 P/Gamepad Start。被处理的 Down 锁存精确 physical control，对应 Up 即使 Screen 已 pop
也继续消费；callback 只记录 intent，树与 State mutation 在合法 frame phase 完成。无 active Screen、无 callback
或 callback 未处理前的输入不会从 gameplay 隐藏。最终切片冻结 per-window、固定 16 槽的
`UIFlowLocalUserId`：有效值为 `1..16`，`UIFlowPrimaryLocalUser=1`；Keyboard/Pointer 永远属于 Primary，
Gamepad 可通过 `assignFlowGamepad()` 显式分配，未分配时回落 Primary。分配表保存完整 generation
`GamepadId`，槽复用不会继承旧用户。`UIFlowActionEvent` 携带 local user；`UIFlowInputDeviceState` 按用户隔离，
Keyboard/Pointer 合并为 `KeyboardMouse`，有意义的 Down、wheel、明显 pointer move 与 Gamepad button Down
按各用户的 Platform sequence 更新，release 与手柄轴漂移不切换。Gamepad 重分配或清除只回落受影响用户的
设备提示并递增 revision，但保留已经锁存的 physical Down/Up，因此重分配后的匹配 Up 仍被消费；Gamepad
断连清除对应 assignment/latch，完整 stream reset 清除全部 assignment/latch，并将受影响设备状态回落键鼠。
Layer/Screen 栈、focus、Modal 与 retained tree 仍是窗口级唯一状态，不复制每用户 UI 树。2D Pause 读取 Primary
revision，驱动真实 `ESC / ENTER / P TO RESUME` / `B / A / START TO RESUME` 标签；Base Screen 的 Menu 打开
Pause，Pause Screen 的 Back/Confirm/Menu 恢复游戏。该能力不冻结任意 action-id 系统；普通设备观察、分配
查询和路由状态更新为 O(1)，完整 reset 只扫描固定 16 槽，每个 action 最多反向扫描一次 bounded committed
layout，全部稳态无分配。
验收面固定为 `tina_ui_tests --gtest_filter=*Flow*`、
`tina_runtime_ui_tests --gtest_filter=*Flow*` 与 `tina_sample_2d --frames=300`；
完整 product gate 留到同一功能批次最终收口，不作为每次源码编辑的前置步骤。

## Deferred

| 领域 | 后置范围 | 重新开启条件 |
| --- | --- | --- |
| Render | 自研 RHI | bgfx backend 出现无法满足且有 profile/产品证据的明确需求 |
| Physics | Jolt 3D adapter | 有明确 3D gameplay 场景与性能预算 |
| UI | BiDi/复杂 shaping；Linux 原生 XIM/Wayland preedit 与候选窗；Windows 真机 IME 候选窗人工金标；layout whitelist 扩展、loop/seek/pause/repeat/yoyo/completion callback、spring/inertia；rounded/stencil 子树 clip 与 backdrop/blur；Back/Confirm/Menu 之外的任意 action-id；startup-only 自定义 Behavior SPI | 多行 TextEdit、UAX #29 grapheme 子集、Windows IMM32 placement、paint-only timeline 与 bounded layout timeline 均已完成；`UI-PAINT-002-A` 已实现 Retained 逐角 box/Canvas chrome 并复用 Render 四角像素半径，不建立 rounded clip；其余分别由 `TEXT-001`、后续 Motion 决策、`UI-PAINT-002`、独立 Flow 扩展、`UI-BEHAVIOR-SPI-001` 跟踪 |
| Asset | Bundle/Patch、cache/LRU 与 network Asset | ASSET-002 的 Catalog reload、增量 Cooker 与 Editor source import 已完成；后续项进入 Now 前先冻结产品场景、容量边界、失败语义和验收命令 |
| Task | work stealing、fiber、lock-free 重写 | profile 证明共享有界队列是瓶颈，并新增 ADR |
| Runtime | 多 World/editor orchestration | 有明确产品/editor 场景，并先冻结 World owner、State TaskGroup barrier 与跨 World 提交/关闭语义 |

## 当前必须解决的偏差

| 偏差 | 事实 | 处理方式 |
| --- | --- | --- |
| UI 平台证据 | Semantics + probe + action seam + UIA patterns/HostBridge/EngineHost 接线已有；tip 跨进程 HWND client gate 证据已固化（2026-08-03）；Narrator/Inspect 人工金标与 AT-SPI 后置 | Windows 收口见 UI-002，Linux 见 UI-002-LINUX；勿把自动 gate 写成合规读屏门禁 |
| UI 深树复杂度 | 50,000 节点 structure commit/destroy、layout、hit、paint publication 已有非递归回归；Popup publication 不再逐节点回溯祖先，当前步骤为线性 | 这是当前实现事实，不替代完整 dirty-range pruning 或固定机 PERF hard gate |
| Linux 状态 | tip Docker：GCC13 Null/Platform + Clang22 Null/sanitizer 已有证据 | 见 [m12-evidence-linux.md](m12-evidence-linux.md)；TEST-001 Done |
| UI route vs policy | `blocksUIUpdateBelow` 不回改当帧 UI route（route 在 stack 前） | 文档已标明；若需真挡 UI 输入另开切片 |
| AssetHandle 终态 | 2D World Sprite、standalone Particle/Trail、TileMap emit 与 3D MeshRenderer 保存 weak Handle；Sprite/Mesh/Material Render item 只保存 packet-local ref；两类 registry 分别唯一拥有 resident Lease/GPU/binding，3D Texture owner 按 AssetId 跨 Material 共享并由引用计数保护；active frame pin 阻止 retirement | A1-A6 + N16.1-N16.4 已完成；`ASSET-HANDLE-SCENE` Done |

## 不变量

- 当前 UI 目录是 `include/tina/ui` + `src/ui`，不得被 Legacy 清理误删。
- `TINA_BUILD_LEGACY=ON` 必须失败；不恢复 `Tina.exe` 或旧产品依赖图。
- Game SDK/Public headers 不暴露第三方类型。
- 资源和 Task 的失败路径不能以继续析构活跃内存收场。
- 同一 Visual Studio build tree 的 Debug/Release 构建串行执行。
- GoogleTest executable 直接运行，sample exit 0、结构化证据与视觉证据分别记录。
