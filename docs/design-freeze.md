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
| Profiling | Tina Trace/Metrics；Tracy 用于定位，bench 用于回归 | [0002](adr/0002-tracy-and-benchmark.md) | Partial：`tina_bench` schema v1；Trace/Tracy adapter 仍后置 |
| 组合 | backend factory + 非全局 `EngineHost` | [0003](adr/0003-backend-factories.md) | Implemented |
| 错误 | 内部可用 exception，模块边界转 `Result`/`Status` | [0004](adr/0004-exceptions-and-errors.md) | Implemented |
| Platform | GLFW + 窄原生适配，不引入 SDL/SDL3 | [0005](adr/0005-glfw-without-sdl.md) | Implemented |
| 测试 | 直接运行 GoogleTest，不用 CTest 调度 | [0006](adr/0006-direct-googletest.md) | Implemented |
| 容器/Hash | 标准库/PMR，不使用 EASTL；xxHash 私有 | [0007](adr/0007-standard-containers-and-hash.md) | Implemented |
| Render | bgfx 是首个真实 backend，保持私有 | [0008](adr/0008-bgfx-render-backend.md) | Implemented |
| Asset | Runtime 只读 Cooked；cgltf 只在 Cooker | [0009](adr/0009-cooked-assets-and-cgltf.md) | Implemented；baseColor/MR/normal Texture2D cook + 外部 URI 安全 + 产品 material binding；bgfx Opaque3D experimental MR 与有界4 directional lights 已落地；完整 PBR 后置 |
| Physics | Box2D 与 Jolt API 分离 | [0010](adr/0010-separate-physics-backends.md) | Box2D implemented：Box/Circle/Capsule、sensor、Distance joint；Jolt deferred |
| UI | Tina Retained UI 输出后端无关 DisplayList | [0011](adr/0011-retained-ui.md) | Implemented foundation/product slice |
| Audio | miniaudio 是唯一真实 audio backend | [0012](adr/0012-miniaudio-backend.md) | Implemented optional adapter |
| ECS | 若使用 EnTT，只能是 Scene 私有存储 | [0013](adr/0013-entt-internal-storage.md) | Not used：当前 Scene 不链接 EnTT |
| Runtime | `IGameApplication` lifecycle + `IGameState` frame behavior | [0014](adr/0014-runtime-phase-and-state.md) | stack/commands + 四相位阻断 + `blocksGameplayInputBelow` 空 snapshot 已落地；`blocksUIUpdateBelow` 仅拦 `updateUI`（不回改当帧 UI route） |
| Input | ordered PlatformFrame、Action domain、逐 substep 提交 | [0015](adr/0015-input-and-fixed-step.md) | Implemented foundation |
| Asset 生命周期 | 弱 Handle、强 Lease、物理 retirement | [0016](adr/0016-asset-ownership-and-retirement.md) | CPU/Null upload ledger + 独立 present-return CPU completion；bgfx readTexture completion marker、AssetLease→Texture/Mesh retirement 与 suspend/shutdown drain 已落地 |
| Task | 有界 CPU/IO/Main、TaskGroup、禁止 detach/强杀 | [0017](adr/0017-bounded-task-system.md) | Implemented；Desktop 交互默认 `max(1, hw-1)`（TASK-001 Done），工厂 0=IO-only |
| Benchmark | 版本化 JSON、fingerprint、provisional vs hard gate | [0018](adr/0018-benchmark-protocol.md) | Accepted 首切片：`tina_bench` schema v1；固定机 hard gate / 多进程 MAD 后置 |
| Handle | 强类型 generation + owner 边界 | [0019](adr/0019-generation-handles.md) | Implemented across current modules |
| WindowSurface | move-only native lease 与主窗口交接 | [0020](adr/0020-window-surface-handoff.md) | Implemented |
| Runtime UI | startup transaction + root/phase-scoped capability | [0021](adr/0021-runtime-ui-startup-capability.md) | Implemented |

## Proposed

当前无 Proposed ADR。固定机 hard-gate / 多进程 MAD 等实现尾巴记在 [Backlog](backlog.md)（PERF-001 扩展），不单独占 Proposed 行。

## Deferred

| 领域 | 后置范围 | 重新开启条件 |
| --- | --- | --- |
| Render | 完整 PBR/IBL/shadow、通用 pass scheduler、自研 RHI | experimental MR 产品路径稳定且有 profile/视觉需求 |
| Physics | Jolt 3D adapter | 有明确 3D gameplay 场景与性能预算 |
| UI | 通用 Modal/Focus Scope/Capture、虚拟列表、复杂 shaping、多行编辑 | 当前 Widget/产品/accessibility 门禁稳定 |
| Asset | 热重载、增量 Cooker、在线编辑 | Cooked schema、Lease/retirement 与产品打包稳定 |
| Task | work stealing、fiber、lock-free 重写 | profile 证明共享有界队列是瓶颈，并新增 ADR |
| Runtime | 多 World/editor orchestration | 游戏 Runtime 的 State/packet 生命周期先完成 |

## 当前必须解决的偏差

| 偏差 | 事实 | 处理方式 |
| --- | --- | --- |
| UI 平台证据 | Semantics + probe + `tina_ui_uia` 映射/HostBridge + EngineHost 自动 HWND 接线已有；Narrator 人工金标与 AT-SPI 后置 | 见 UI-002；勿把单测写成合规读屏门禁 |
| Linux 状态 | tip Docker：GCC13 Null/Platform + Clang22 Null/sanitizer 已有证据 | 见 [m12-evidence-linux.md](m12-evidence-linux.md)；TEST-001 Done |
| UI route vs policy | `blocksUIUpdateBelow` 不回改当帧 UI route（route 在 stack 前） | 文档已标明；若需真挡 UI 输入另开切片 |
| AssetHandle 终态 | 2D World Sprite、standalone Particle/Trail、TileMap emit 与 3D MeshRenderer 已存 weak Handle；两类 registry 已删除产品手写 key；N16.1 建立 packet-local table，N16.2 已让全部 Sprite2D item 使用 `FrameResourceRef` 并以 frame pin 保护 binding，但 registry 尚不拥有 Lease/GPU retirement | A1-A6 + N16.1/N16.2 已完成；总项 InProgress，由 N16.3 收口 |

## 不变量

- 当前 UI 目录是 `include/tina/ui` + `src/ui`，不得被 Legacy 清理误删。
- `TINA_BUILD_LEGACY=ON` 必须失败；不恢复 `Tina.exe` 或旧产品依赖图。
- Game SDK/Public headers 不暴露第三方类型。
- 资源和 Task 的失败路径不能以继续析构活跃内存收场。
- 同一 Visual Studio build tree 的 Debug/Release 构建串行执行。
- GoogleTest executable 直接运行，sample exit 0、结构化证据与视觉证据分别记录。
