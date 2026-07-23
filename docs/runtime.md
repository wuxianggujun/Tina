# Runtime 与 Frame Pipeline

本文描述当前 `tina_runtime` 与 `Tina::Desktop::CreateEngine`。未来 State stack、owning frame packet
等工作统一在 [Backlog](backlog.md) 跟踪，不在本文写成已存在接口。

## 当前结论

- `EngineHost` 是唯一非全局组合根；`run()` 只能在创建线程调用一次。
- `IGameApplication` 只通过 `createInitialState()` 创建首个 State，并在已提交游戏结束时接收
  `onShutdown()`。
- Runtime 持有私有 `GameStateStack`（定容 8）；初始 State 经 onEnter 成功后 push。
- `FrameUpdateContext` 提供 `requestPush` / `requestPop` / `requestReplace` / `requestPolicyChange`：
  每帧最多一个 structural command，在 Frame Update 与 extract 之间唯一 commit；enter 失败只丢
  candidate（不 onExit），栈保持。pop 空栈产生 `RunExitReason::GameStateStackBecameEmpty`。
- `GameStatePolicy` 在 enter 成功后采样；`requestPolicyChange` 更新栈顶 committed policy。
  多层 policy 向下阻断（input/fixed/render 对 below 的屏蔽）尚未调度。
- `IGameState` 承担 `fixedUpdate()`、`updateFrame()`、`extractRenderScene()` 与 `updateUI()`。
- Runtime 已组合 Clock、Platform、Task、Render 和可选 Audio；AssetSystem/World/Physics2D 仍由
  产品 State 或样例显式持有，不是 `EngineHost` 模块。
- Desktop 使用 GLFW、bgfx、有界 TaskSystem（含 ADR 0017 交互 CPU 默认）和 backend-neutral `AudioEngine`。
- Null/测试图可以显式注入 Headless、DisabledTaskSystem、NullRender 和无 Audio 组合。

Legacy `Application`、`SceneManager` 与 `Tina.exe` 已删除；禁止恢复 Singleton、Service Locator 或
第二套主循环。

## 创建与启动事务

`EngineHost::Create()` 先校验 `EngineConfig` 和全部固定容量，再构造 Runtime 内部 builder/mapper。
模块创建顺序为：

```text
Diagnostics
  -> MonotonicClock
  -> Platform
  -> TaskSystem
  -> optional AudioEngine
  -> WindowSurface lease/snapshot (windowed only)
  -> RenderDevice
  -> publish primary window (windowed only)
```

任一步失败都返回 `Result` 并按对象所有权逆序回滚，不发布半初始化 Host。GLFW/native surface 与 bgfx
只存在于私有 adapter；Runtime 公共头只看到 Tina 类型。

`run()` 的启动事务为：

```text
IGameApplication::createInitialState
  -> IPlatformBackend::initialPrimaryWindowMetrics
  -> bind primary UIContext, or explicit Headless unavailable
  -> IGameState::onEnter through phase-scoped UI capability
  -> sample IGameState::initialPolicy
  -> commit startup UI layout/hit snapshot
  -> publish the single committed State
```

候选 State 在提交前失败时直接销毁，不调用其 `onExit()`，也不调用 Application `onShutdown()`。
提交后，无论正常退出还是 Runtime 失败，State `onExit()` 与 Application `onShutdown()` 各执行一次。

## 当前帧顺序

```text
Platform poll
  -> validate frame id/capacity/source sequence and WindowSurface revision
  -> monotonic timing + FixedStepAccumulator plan
  -> Platform lifecycle dispatch
  -> select primary UIContext
  -> UI input routing/default action against previous committed hit snapshot
  -> ActionMapper with UI consumption/claims
  -> 0..4 fixedUpdate callbacks
  -> updateFrame
  -> Audio completion pump
  -> RenderScene begin/extract/commit
  -> updateUI through root-scoped capability
  -> UI layout/hit/paint/semantics commit
  -> UI DisplayList + optional R8 Glyph atlas view
  -> RenderDevice::submitFrame
  -> present when the surface is active
  -> latch presented Camera2D for next-frame world picking
```

Fixed simulation 默认 60 Hz，单帧最多追赶4步。真实 delta、接受/拒绝的真实时间、variable
`updateDelta`、丢弃的 simulation delta 与 interpolation 分开记录。Simulation Action 只在目标 fixed
tick 消费一次；0步帧不丢 edge，多步追赶不重复消费。Frame Action 只进入 `updateFrame()`。

`requestExitAfterFrame()` 会完成当帧 extraction、UI、submit 和 present 后退出。主窗口 close 是
Platform poll 的不可取消退出分支，在任何新游戏回调前停止。

## 输入、UI 与世界点击

Platform transition、UI route result 与 Gameplay Action 是三层独立数据：

1. `UIInputRouteProducer` 按原始 ordinal 路由 Pointer/Keyboard/Gamepad/Text/IME；
2. UI 发布 transition consumption 与 continuous-control claim；
3. `ActionMapper` 只处理未被 UI 接管的输入。

Button、Checkbox、Slider、RadioButton、TextEdit 的当前默认行为在 UI route 阶段完成，早于 Action
Mapping。Tab/Shift+Tab、Enter/Space/KeypadEnter 和 Gamepad South 已有窄 focus/default-action 路径；
通用 Focus Scope、Modal 和持久 Pointer Capture 尚未实现。

未被 UI 消费或 claim 的 primary Pointer transition 可以形成带 `WorldPointerSample` 的 Simulation
edge。坐标使用 last-presented Camera2D 与对应 surface revision 锁存；跨0步帧延迟消费时不会用新
Camera 或 resize 重算。

## Phase Context 与借用寿命

所有 Context 都是 callback-only：

| Context | 当前能力 | 失效点 |
| --- | --- | --- |
| `GameStartupContext` | EngineConfig、Platform event subscription facade | `createInitialState()` 返回 |
| `GameStateEnterContext` | Platform subscription、primary UI root builder | `onEnter()` 返回 |
| `FixedUpdateContext` | timing、Simulation Action、可选 Audio borrow | `fixedUpdate()` 返回 |
| `FrameUpdateContext` | timing、Frame Action、可选 Audio、退出请求 | `updateFrame()` 返回 |
| `RenderSceneExtractionContext` | phase-local `RenderSceneWriter` | `extractRenderScene()` 返回 |
| `UIUpdateContext` | root-scoped UI updater | `updateUI()` 返回 |

Context、writer、span、RenderScene view、UIDisplayList view 与 Glyph atlas view都不得跨回调保存。
Platform subscription token、UI listener token、`UIRootOwner`、AssetLease 等明确 RAII owner 可以按各自
契约保存；它们不能反向保活 Runtime Context。

## 提交与失败语义

- Platform frame、RenderScene 和 UI snapshot 都先校验/构建，再单点提交；失败不发布半份 view。
- UI routing 读取上一份 committed hit snapshot；当前帧 `updateUI()` 的结果在 Render 前提交，并从
  下一帧开始参与输入命中。
- `RenderFrame` 当前携带 submit-call-local 的 World Scene、UI DisplayList 与 Glyph atlas borrow；
  backend 必须在 `submitFrame()` 返回前同步消费，不能保存指针。
- suspended surface 返回 `SkippedSuspendedSurface`，Runtime 不调用 `present()`。
- 游戏回调和 factory 抛出的普通异常在边界转换为结构化 Error；owner-thread 违规和序列回退有明确错误。

## 关闭顺序

已提交游戏先执行 State `onExit()`，再执行 Application `onShutdown()`。Runtime 私有 UI owner 与
Platform dispatcher 关闭后，模块按以下顺序退出：

```text
AudioEngine
  -> RenderDevice
  -> TaskSystem::shutdownAndJoin
  -> Platform
  -> MonotonicClock
  -> Diagnostics
```

主窗口 UIContext 在 Render、Task、Platform 与 Clock 之前于 owner thread 销毁。Task 未 join 前不得
释放其可能访问的 owner；错误线程销毁带 native 资源的 Host 会终止进程，而不是冒险制造 UAF。

## 尚未完成

| Backlog | 范围 |
| --- | --- |
| `TASK-001` | 落实或正式替代 ADR 0017 的 Desktop CPU worker 默认值 |
| `RUNTIME-001` | stack/commands 首切片已落地；多层 policy 向下阻断仍可扩展 |
| `RUNTIME-002` | owning `RenderFramePacket`、FramePin、submission completion 与资源寿命闭环 |

通用 Runtime Event Queue、AssetSystem 组合、State TaskGroup barrier、多 World/editor orchestration 与
通用 pass scheduler 均不是当前接口。实现前应补 ADR/Backlog 验收，不得从文档中的目标流程推断为已完成。

验证命令与 sample 证据边界见 [测试说明](testing.md)，公开契约见 [Public API](public-api.md)。
