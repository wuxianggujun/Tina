# Architecture Decision Records

ADR 记录已经接受或被替代的架构决定。主题文档描述完整设计，ADR 只保存“为什么选择”和
“代价是什么”。状态只使用 Proposed、Accepted、Superseded、Rejected。

| ADR | 状态 | 决定 |
| --- | --- | --- |
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

新增 ADR 从 [模板](0000-template.md) 复制。替代旧决定时新建 ADR，并把旧记录状态改为
Superseded 和链接新编号；不要改写历史理由。
