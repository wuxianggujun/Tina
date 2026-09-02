# Carbon Engine 参考取证

> 取证日期：**2026-07-16**（本地研究集）；落地状态最后复核：**2026-09-01**。
> 本文是参考取证记录，不是当前契约：下方“采纳/拒绝”与“尚未落地”都会随 Tina 源码前进而过期，
> 引用前按 [文档索引](README.md) 的优先级回到当前源码/CMake 复核。

Carbon 只作为成熟工程经验来源，不是 Tina 的依赖、兼容目标或代码移植源。官方项目入口见
[Carbon Engine GitHub](https://github.com/carbonengine) 与
[Fenris Carbon](https://fenris.com/carbon)。本地只读参考位于被忽略的 `temp/carbon-engine`，不进入 Tina
构建、提交或发布包。

## 取证边界

2026-07-16 的本地研究集包括 `core`、`exefile`、`trinity`、`ime`、`blue`、`resources`、
`scheduler`、`math`、`mesh`、`imagetools`、`imageio`、`audio` 与 `destiny`。精确 URL/commit 保存在
本地 `REFERENCE_VERSIONS.md`。

官方组织没有独立公开的现代 `CarbonUI` retained widget 仓库；`trinity/UI` 与工具中的 ImGui 只能
作为事件/工具取证，不能冒充完整 CarbonUI 源码或 Tina UI 的实现依据。

## 采纳与拒绝

| 领域 | 取证结论 | Tina 当前采纳 | 明确拒绝/后置 |
| --- | --- | --- | --- |
| 初始化 | 成熟 Runtime 把创建分阶段并登记逆操作 | factory + 非全局 `EngineHost`，失败不发布半对象 | 全局组合根、半加载状态、`void*` cookie |
| Frame | phase 顺序、延迟变更与安全 cleanup 比通用 tick 更可靠 | Platform→UI→Action→Fixed/Frame→Scene/UI→Render | 巨型全局 pump、所有对象挂同一 Tick |
| Core | 时间、锁/线程、内存、trace、crash 都应有独立契约 | Result、ScopeExit、MonotonicClock、MemoryTag/PMR、generation ID | 全局 new/delete、强杀线程、宏式 allocator、全局 profiler |
| Window/Input | Mouse/Key/Text/Focus/Capture 分流有价值 | GLFW adapter、ordered PlatformFrame、Gamepad registry、IMM32 composition | 复制 Win32 枚举/消息宏、native pointer 公共 API、SDL |
| UI | focus/capture/IME owner 必须显式 | Tina retained tree、generation node、route、DisplayList、TextEdit | 全局 UI 状态；把公开旧接口当现代 widget 实现 |
| Render | 命名步骤、失败停止、状态恢复和计时有价值 | backend-neutral Scene/UI frame，私有 bgfx | Carbon step API、自研通用 RHI；Pass Scheduler 当前仍后置 |
| Asset | 后台 load、主线程 prepare、取消与预算需分层 | Cooked/Catalog、AssetId、Task、Handle/Lease、GPU binding | raw `this` callback、模糊 bool 状态、Carbon 私有格式 |
| Cooker | Build 后再校验、最后 atomic write | `tina_assetc`、typed payload、manifest/hash、Core atomic `writeFile` | CMF/FBX 产品工具链直接进入 Runtime |
| Simulation | accumulator 余量、追赶上限、插值和阶段外删除 | 60 Hz、最多4步、Simulation/Frame Action 分域 | MMO tick、Python tasklet、产品专用 Ballpark |
| Audio | Disabled 状态、callback→main completion、voice budget有价值 | backend-neutral AudioEngine + miniaudio adapter | Wwise/SoundBank、全局 audio manager、裸 callback cookie |

## 已经落地的 Tina 结论

Carbon 取证只是输入，以下事实由 Tina 当前源码/测试独立证明：

- `EngineHost::Create` 分阶段构造 Diagnostics/Clock/Platform/Task/Audio/Render，失败逆序回滚；
- `IGameApplication::createInitialState` + 单 `IGameState` 生命周期替代旧全局 Application/SceneManager；
- `PlatformFrameView` 发布 final Window/Gamepad snapshot 与 ordered transition；
- GLFW hidden-window + WindowSurface lease 在 Render 成功后才发布窗口；
- UI 使用 per-window `UIContext`、generation node/root、phase-scoped facade、route/default action 与
  backend-neutral DisplayList；
- Scene/Asset/Render 分离，Cooked payload 先验证再上传，backend 只接收 Tina key/descriptor；
- FixedStepAccumulator 保留余量、限制每帧最多4步并输出 interpolation；
- Audio command/completion 与 realtime mix 不从 callback 修改游戏 owner。

这些能力的状态和门禁分别以 [架构](architecture.md)、[Runtime](runtime.md)、[UI](ui.md)、
[资源](resources.md)和[测试](testing.md)为准，不在本文复制测试数量。

## 未采纳的历史模式

- `Application::instance()`、`BeOS`、Service Locator 或其他全局模块表；
- 强杀线程、detach 后继续访问 Engine owner；
- 函数指针 + 无类型 cookie 的业务回调；
- Blue/Python/Stackless 启动链；
- 公共 API 暴露 native window、bgfx、Wwise、Carbon resource format；
- 32-bit FNV 作为 AssetId/内容身份；
- 因参考项目“成熟”就跳过 Tina 自己的失败注入、sanitizer、产品 smoke 和资源账本。

## 曾列为“尚未落地”、现已落地的部分

2026-09-01 复核，以下取证结论已由 Tina 自己的源码与门禁证明，不再算待办：

- **owning `RenderFramePacket` 与 CPU completion**：`include/tina/render/RenderFramePacket.hpp`；
  [风险登记](risks.md) 的 R-LIFE-01 已 Closed（FramePin、Texture/Mesh readback marker 与
  AssetLease-backed retirement）。
- **pass scheduling**：`RenderPassScheduler`（`src/render/RenderPassScheduler.cpp`）与
  `RenderPipelineSchedule`/`buildRenderPipelineSchedule`（`include/tina/render/RenderPostProcess.hpp`）；
  [测试](testing.md) 的 G3 记为 deterministic pass scheduler。
- **进程级崩溃取证**：`include/tina/core/diagnostics/CrashHandler.hpp`；`TinaEditor.exe` 把 crash 与
  顶层 fatal error 写入 `%TEMP%/tina_editor_crash.txt`。
- **Trace 前端**：`include/tina/core/trace/Trace.hpp`（Tracy 后端在 `src/trace/tracy`）。

## 仍可借鉴但尚未落地

- 统一 Metrics 注册表，以及 Trace 的 session/capture 控制面（[Core](core.md) 明确记为仍缺）；
- 固定机器、版本化 workload 与 benchmark baseline；
- 通用 GPU submission fence（当前只有 backend-proven readback marker，见根 README）；
- render resource state 跟踪与 GPU timing；
- Audio streaming/voice virtualization；
- Bundle/Patch 交付层。

这些项目必须先进入 ADR/Backlog，再由 Tina 的源码、target 和测试证明。Carbon 文档或同名类型不能
作为“已实现”证据。

## 使用规则

1. 引用 Carbon 时记录仓库、commit、文件和观察到的真实行为；
2. 区分“值得学习的契约”和“准备复制的代码”，默认不复制；
3. 新依赖/全局状态/公共 ABI 变化仍走 Tina ADR；
4. 任何实现结论回到 Tina 当前源码、CMake target、直接 GoogleTest 和产品 sample；
5. 不把本地参考目录加入 Git、include path、license bundle 或发布包。
