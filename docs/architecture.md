# 架构总览

## 当前双轨实现

Tina 当前处于 Legacy 产品与 vNext 垂直切片并存的迁移期。Legacy 仍以单个游戏可执行文件为主，
源码按 Core、Engine、Renderer、UI、ECS 和 Game 组织；vNext 已建立独立的 `tina_core`、
`tina_platform`、`tina_task`、`tina_render`、`tina_runtime` 五个 C++23 target，以及不依赖真实
窗口和 GPU 的 `tina_sample_null`。M7-A 已在这些 target 内加入有界 Platform Frame、输入
Snapshot/Transition、Platform 生命周期订阅以及 Simulation/Frame Action Mapping。
现有 Legacy target 的包依赖由 vcpkg manifest 管理，bgfx、EASTL、EABase 仍保持固定源码
版本；其中 EASTL/EABase 只属于迁移期现状，不是 vNext 目标依赖。

Legacy 当前大致依赖为 Core → Platform/Engine → ECS/Renderer/UI → Game；它不是 vNext 目标图。
新 target 的权威依赖关系见 [vNext 目标架构](vnext-architecture.md)，第三方类型不得继续向
不相关层扩散。

“当前实现”不再作为 vNext 的最终模块形态。完整目标和迁移策略见
[Tina vNext 目标架构](vnext-architecture.md)。

## 已知问题

- Application 仍承担过多初始化、主循环和服务访问职责；
- Scene 同时管理状态、相机、UI、渲染视图和事件订阅；
- GameScene 混合世界、ECS、输入、音频、UI 和渲染编排；
- Renderer 中存在多套未统一的命令与高层绘制入口。

## 旧架构删除状态

结论：旧文档已经替换，但旧源码架构没有完全删除。`TINA_BUILD_LEGACY` 与 vNext-only preset
已落地，能够把旧依赖和产品 target 排除出最小构建图。`EngineHost` 生命周期与 M7-A Platform/Input
内核已经可独立运行，但它还没有生产窗口、真实渲染、Scene/Asset/UI/Audio 或完整状态栈，因此 Legacy
仍是当前 2D/UI/3D 产品实现，不能直接整目录删除。

| 范围 | 状态 | 证据或影响 |
| --- | --- | --- |
| 旧阶段文档 | 已删除/替换 | `docs` 只保留当前架构、契约、验证和 Roadmap |
| Legacy 构建隔离 | 首批完成 | vNext preset 同时关闭 Legacy、vNext bgfx backend 与 shader：不发现 Legacy package、不进入 bgfx/EASTL、不复制旧资源，也不建立 `Tina` target |
| 单体游戏 target | 仍在使用 | 主程序仍由一个 `Tina` executable 汇集 Engine、Game、Renderer、UI 和 ECS |
| Core compatibility | 仍在使用 | 主程序和测试仍链接 `Tina::CoreLegacy` |
| `Application` 组合根 | 仍在使用 | 继续持有 Window、Input、Event、Scene、Resource、Audio 和渲染服务 |
| Scene/GameScene 旧职责 | 仍在使用 | GameScene 仍混合玩法、ECS、输入、UI、音频与渲染编排 |
| EnTT 边界 | 尚未收敛 | `World` 暴露 `entt::registry`，GameScene 直接访问 |
| bgfx 边界 | 尚未收敛 | Renderer、UI 和部分公共结构仍直接暴露 bgfx handle/type |
| 路径资源系统 | 仍在使用 | Cooked Asset、稳定 AssetId 和独立 GPU upload queue 尚未落地 |
| vNext M6-A Runtime | 已完成生命周期切片 | `EngineHost`、`IGameApplication`、单个 `IGameState`、最小阶段 Context、Backend SPI、Headless Platform、Disabled TaskSystem 与 NullRenderDevice 已落地 |
| vNext M7-A Platform/Input | 已完成 Headless 内核切片 | 固定容量 `PlatformFrameBuilder`、final Snapshot、保序 transition、generation-typed `GamepadId` 与帧内生命周期一致性校验、Runtime-private `PlatformEventDispatcher`、RAII 订阅和 Simulation/Frame Action Mapper 已落地；production GLFW registry/polling 仍属下一子提交 |
| 完整 vNext Runtime | 尚未完成 | GameStateStack/commands、worker、Pass Scheduler/RenderFramePacket、Scene/Asset/UI/Audio、Desktop bootstrap 与真实 backend 仍按后续切片实施 |

因此不能用“删除旧 `src`”作为下一步。正确顺序是：建立新边界和测试 → 迁移调用点 → 确认旧接口零引用 → 通过 2D/UI/3D 验收 → 在独立提交中删除旧实现。

旧模块只有同时满足以下条件才能删除：

1. 已有明确替代模块和所有权关系；
2. 生产代码与测试不再 include、链接或调用旧接口；
3. Windows/Linux 构建和直接 GoogleTest 通过；
4. 2D、UI、3D 冒烟和资源回收门禁通过；
5. 删除操作不夹带新功能，能够独立回滚。

## 目标契约

- 不新增全局 Singleton 或 Service Locator；
- 以 `EngineHost` 作为唯一非全局组合根；普通游戏调用纯 Tina API 的 desktop bootstrap，
  高级测试才显式注入 factories；
- `IGameApplication` 只负责程序启动/停止，`IGameState` 是唯一帧行为入口；
- 初始化必须显式返回错误，失败后按逆序释放已创建资源；
- `tina_core`、`tina_platform`、`tina_platform_glfw`、`tina_task`、`tina_runtime`、`tina_scene`、
  `tina_asset_format`、`tina_asset`、`tina_render`、`tina_render_bgfx`、`tina_ui`、
  `tina_ui_freetype`、`tina_audio`、`tina_audio_miniaudio`、`tina_profile_tracy` 与 `tina_assetc`
  形成单向依赖；
- vNext target 不依赖 EASTL；标准库/`std::pmr` 承担通用容器，Tina 只提供少量经过测试的
  固定容量和 generation 专用结构；xxHash 只藏在 Hash adapter 后；
- 2D/3D 共享右手 Y-up 世界，2D 位于 XY 平面；
- Game SDK 和 Phase Context 不暴露 RenderDevice/native handle；Tina module public header 可以暴露
  纯 Tina Render SPI，但任何公共头都不暴露 bgfx/native 类型；bgfx 只出现在
  `tina_render_bgfx` 私有实现和离线 shader 工具；
- 自研 UI 只输出后端无关 DisplayList，以细粒度 dirty、PaintCache 和 committed snapshot
  实现无变化 UI 的0布局/0 PaintCache rebuild/0 Tina heap allocation；
- EnTT 只作为 Scene 内部存储，模块接口只暴露 generation `EntityId`；
- MemorySystem 由 EngineHost 拥有，通用持久内存按模块 tag 统计，帧临时数据按 phase Arena
  分配；Task 只通过有界 CPU/IO/Main executor 和显式 barrier 跨线程；
- 完整目标可以不兼容旧 API，但实施按可运行垂直切片推进，不进行不可验证的大爆炸提交。
