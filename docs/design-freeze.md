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
| Profiling | Tina Trace/Metrics；Tracy 用于定位，bench 用于回归 | [0002](adr/0002-tracy-and-benchmark.md) | Partial：None frontend、Tracy 0.13.1 64-byte opaque zone adapter、Runtime phase consumer 与 `tina_bench` schema v1 已落地；Metrics 与 session/capture 控制面仍后置 |
| 组合 | backend factory + 非全局 `EngineHost` | [0003](adr/0003-backend-factories.md) | Implemented |
| 错误 | 内部可用 exception，模块边界转 `Result`/`Status` | [0004](adr/0004-exceptions-and-errors.md) | Implemented |
| Platform | GLFW + 窄原生适配，不引入 SDL/SDL3 | [0005](adr/0005-glfw-without-sdl.md) | Implemented |
| 测试 | 直接运行 GoogleTest，不用 CTest 调度 | [0006](adr/0006-direct-googletest.md) | Implemented |
| 容器/Hash | 标准库/PMR，不使用 EASTL；xxHash 私有 | [0007](adr/0007-standard-containers-and-hash.md) | Implemented |
| Render | bgfx 是首个真实 backend，保持私有 | [0008](adr/0008-bgfx-render-backend.md) | Implemented |
| Asset | Runtime 只读 Cooked；cgltf 只在 Cooker | [0009](adr/0009-cooked-assets-and-cgltf.md) | Implemented；baseColor/MR/normal Texture2D cook + 外部 URI 安全 + 产品 material binding；EnvironmentMap cooked payload/publication/typed parse 与 bgfx Opaque3D Cook-Torrance GGX/split-sum IBL 已落地 |
| Physics | Box2D 与 Jolt API 分离 | [0010](adr/0010-separate-physics-backends.md) | Box2D implemented：Box/Circle/Capsule、sensor、Distance joint；Jolt deferred |
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
| UI Authoring | Element 组合 authoring、父/子布局分离与统一 committed 内容放置 | [0022](adr/0022-ui-element-authoring-and-layout.md) | Implemented：descriptor/recipe、Flow/Flex、Semantics、StyleRole/reset、bounded build transaction、`SolidRect` Canvas 与统一 RoundedRect 已落地 |
| UI 扩展 | 可组合标准 Behavior、bounded Component、node-local StyleSheet、Image/Icon/NineSlice 与 paint-only Motion | [0023](adr/0023-ui-extensibility-style-paint-motion.md) | Partial→近完成：IMAGE/COMPONENT/STYLE Done；MOTION paint-only tracks、声明式 Style BackgroundColor persistent reservation/activation 与 `ui_motion_v1` Done；完整产品 Visual 仍可增强 |

## Proposed

| 领域 | 候选决定 | ADR | 当前状态 |
| --- | --- | --- | --- |
| SDK 发布 | pre-1.0 SemVer、tuple-scoped 静态 C++ 兼容、API/symbol baseline 与 previous-release probe | [0024](adr/0024-sdk-abi-compatibility.md) | 待维护者选择 pre-1.0 版本方案；当前 `SameMajorVersion` 与 consumer gate 不构成正式 ABI 承诺 |

固定机 hard-gate / 多进程 MAD 等实现尾巴记在 [Backlog](backlog.md)（PERF-002），不单独占 Proposed 行。

## Deferred

| 领域 | 后置范围 | 重新开启条件 |
| --- | --- | --- |
| Render | 自研 RHI | bgfx backend 出现无法满足且有 profile/产品证据的明确需求 |
| Physics | Jolt 3D adapter | 有明确 3D gameplay 场景与性能预算 |
| UI | 多行编辑、grapheme/BiDi/复杂 shaping、完整 IME 候选窗；逐角半径/圆角子树 clip/backdrop；Activatable Screen/Layer Stack；startup-only 自定义 Behavior SPI | 分别由 `TEXT-001`、`UI-PAINT-002`、`UI-FLOW-001`、`UI-BEHAVIOR-SPI-001` 跟踪；只有真实产品或插件需求并先冻结容量、失败与性能边界后才重新开启 |
| Asset | 热重载、增量 Cooker、在线编辑 | Cooked schema、Lease/retirement 与产品打包稳定 |
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
