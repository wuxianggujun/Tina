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
| Profiling | Tina Trace/Metrics；Tracy 用于定位，bench 用于回归 | [0002](adr/0002-tracy-and-benchmark.md) | Not implemented：仅有模块计数/局部 bench；见 PERF-001 |
| 组合 | backend factory + 非全局 `EngineHost` | [0003](adr/0003-backend-factories.md) | Implemented |
| 错误 | 内部可用 exception，模块边界转 `Result`/`Status` | [0004](adr/0004-exceptions-and-errors.md) | Implemented |
| Platform | GLFW + 窄原生适配，不引入 SDL/SDL3 | [0005](adr/0005-glfw-without-sdl.md) | Implemented |
| 测试 | 直接运行 GoogleTest，不用 CTest 调度 | [0006](adr/0006-direct-googletest.md) | Implemented |
| 容器/Hash | 标准库/PMR，不使用 EASTL；xxHash 私有 | [0007](adr/0007-standard-containers-and-hash.md) | Implemented |
| Render | bgfx 是首个真实 backend，保持私有 | [0008](adr/0008-bgfx-render-backend.md) | Implemented |
| Asset | Runtime 只读 Cooked；cgltf 只在 Cooker | [0009](adr/0009-cooked-assets-and-cgltf.md) | Implemented；baseColorTexture cook + 外部 URI 安全与产品 GPU 绑定完成；PBR 后置 |
| Physics | Box2D 与 Jolt API 分离 | [0010](adr/0010-separate-physics-backends.md) | Box2D implemented；Jolt deferred |
| UI | Tina Retained UI 输出后端无关 DisplayList | [0011](adr/0011-retained-ui.md) | Implemented foundation/product slice |
| Audio | miniaudio 是唯一真实 audio backend | [0012](adr/0012-miniaudio-backend.md) | Implemented optional adapter |
| ECS | 若使用 EnTT，只能是 Scene 私有存储 | [0013](adr/0013-entt-internal-storage.md) | Not used：当前 Scene 不链接 EnTT |
| Runtime | `IGameApplication` lifecycle + `IGameState` frame behavior | [0014](adr/0014-runtime-phase-and-state.md) | Single-state implemented；stack/commands pending |
| Input | ordered PlatformFrame、Action domain、逐 substep 提交 | [0015](adr/0015-input-and-fixed-step.md) | Implemented foundation |
| Asset 生命周期 | 弱 Handle、强 Lease、物理 retirement | [0016](adr/0016-asset-ownership-and-retirement.md) | CPU/Null upload/ledger implemented；FramePin/completion partial |
| Task | 有界 CPU/IO/Main、TaskGroup、禁止 detach/强杀 | [0017](adr/0017-bounded-task-system.md) | Implemented；Desktop 交互默认 `max(1, hw-1)`，工厂 0=IO-only |
| Handle | 强类型 generation + owner 边界 | [0019](adr/0019-generation-handles.md) | Implemented across current modules |
| WindowSurface | move-only native lease 与主窗口交接 | [0020](adr/0020-window-surface-handoff.md) | Implemented |
| Runtime UI | startup transaction + root/phase-scoped capability | [0021](adr/0021-runtime-ui-startup-capability.md) | Implemented |

## Proposed

| 领域 | 候选决定 | ADR | 进入 Accepted 前必须完成 |
| --- | --- | --- | --- |
| Benchmark | 版本化 JSON、固定门禁机、fingerprint/MAD/baseline protocol | [0018](adr/0018-benchmark-protocol.md) | 选择固定 workload/机器，验证 schema 与噪声，明确 accept/reject |

## Deferred

| 领域 | 后置范围 | 重新开启条件 |
| --- | --- | --- |
| Render | PBR、通用 pass scheduler、自研 RHI | 完成 3D multi-mesh/texture 产品路径且有 profile 需求 |
| Physics | Jolt 3D adapter | 有明确 3D gameplay 场景与性能预算 |
| UI | 通用 Modal/Focus Scope/Capture、虚拟列表、复杂 shaping、多行编辑 | 当前 Widget/产品/accessibility 门禁稳定 |
| Asset | 热重载、增量 Cooker、在线编辑 | Cooked schema、Lease/retirement 与产品打包稳定 |
| Task | work stealing、fiber、lock-free 重写 | profile 证明共享有界队列是瓶颈，并新增 ADR |
| Runtime | 多 World/editor orchestration | 游戏 Runtime 的 State/packet 生命周期先完成 |

## 当前必须解决的偏差

| 偏差 | 事实 | 处理方式 |
| --- | --- | --- |
| UI 平台证据 | ProgressBar/RadioButton 的 API、单测、product-2d 结构化输出与 Windows client-area 视觉证据已完成 | 后续只扩展 UIA/AT-SPI 与跨 DPI/GPU 门禁；见 UI-002、UI-003 |
| Linux 状态 | 历史 Linux 门禁早于当前 tip | 复验前不得扩写为当前平台支持证据；见 TEST-001 |

## 不变量

- 当前 UI 目录是 `include/tina/ui` + `src/ui`，不得被 Legacy 清理误删。
- `TINA_BUILD_LEGACY=ON` 必须失败；不恢复 `Tina.exe` 或旧产品依赖图。
- Game SDK/Public headers 不暴露第三方类型。
- 资源和 Task 的失败路径不能以继续析构活跃内存收场。
- 同一 Visual Studio build tree 的 Debug/Release 构建串行执行。
- GoogleTest executable 直接运行，sample exit 0、结构化证据与视觉证据分别记录。
