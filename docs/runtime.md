# Runtime 与 Frame Pipeline

本文描述当前 `tina_runtime` 与 `Tina::Desktop::CreateEngine`。未完成扩展（如真实 GPU fence、产品
暂停 sample）见 [Backlog](backlog.md)；已落地 stack/packet 能力见下文「当前结论」。

## 当前结论

- `EngineHost` 是唯一非全局组合根；`run()` 只能在创建线程调用一次。
- `IGameApplication` 只通过 `createInitialState()` 创建首个 State，并在已提交游戏结束时接收
  `onShutdown()`。
- Runtime 持有私有 `GameStateStack`（定容 8）；初始 State 经 onEnter 成功后 push。
- `FrameUpdateContext` 提供 `requestPush` / `requestPop` / `requestReplace` / `requestPolicyChange`：
  每帧最多一个 structural command，在 Frame Update 与 extract 之间唯一 commit；enter 失败只丢
  candidate（不 onExit），栈保持。pop 空栈产生 `RunExitReason::GameStateStackBecameEmpty`。
- `GameStatePolicy` 在 enter 成功后采样；`requestPolicyChange` 更新栈顶 committed policy。
  帧阶段按栈 **自顶向下** 调度（见下节 policy 语义）。structural command 仍仅栈顶可排队。
- `IGameState` 承担 `fixedUpdate()`、`updateFrame()`、`extractRenderScene()` 与 `updateUI()`。
- Runtime 唯一 `ActionMapper` 使用 unified digital/analog binding；只有栈顶 State 可在
  `FrameUpdateContext` 中排队下一 mapping frame 生效的 rebind transaction。
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

与 `src/runtime/EngineHost.cpp` 一致：

```text
Platform poll
  -> validate frame id/capacity/source sequence and WindowSurface revision
  -> monotonic timing + FixedStepAccumulator plan
  -> Platform lifecycle dispatch
  -> select primary UIContext
  -> UI input routing/default action against previous committed hit snapshot
  -> ActionMapper applies queued rebind, then maps with UI consumption/claims
  -> 0..4 fixedUpdate (stack top-down; suppressed Action if blocked by above)
  -> updateFrame (stack top-down; only top may queue stack commands)
  -> commitPendingGameStateCommands  // after frame, before extract
  -> Audio completion pump
  -> RenderScene begin/extract/commit (stack top-down)
  -> updateUI through root-scoped capability (stack top-down)
  -> UI layout/hit/paint/semantics commit (+ optional UIA publish)
  -> UI DisplayList + optional R8 Glyph atlas view
  -> RenderFramePacket pins + RenderDevice::submitFrame
  -> present when the surface is active, then complete CPU frame pins (else completeSkipped)
  -> latch presented Camera2D for next-frame world picking
```

Fixed simulation 默认 60 Hz，单帧最多追赶 4 步。真实 delta、接受/拒绝的真实时间、variable
`updateDelta`、丢弃的 simulation delta 与 interpolation 分开记录。Simulation Action 只在目标 fixed
tick 消费一次；0 步帧不丢 edge，多步追赶不重复消费。Frame Action 只进入 `updateFrame()`。

`requestExitAfterFrame()` 会完成当帧 extraction、UI、submit 和 present 后退出。主窗口 close 是
Platform poll 的不可取消退出分支，在任何新游戏回调前停止。

## State 栈与 `GameStatePolicy`

Runtime 私有 `GameStateStack`（定容 8）。相位 `forEachDispatch` **自顶向下**访问；某层
`policyBlocksBelow(phase)` 为真则不再调用更下层。

| Policy 字段 | 作用 | 注意 |
| --- | --- | --- |
| `blocksFixedUpdateBelow` | 截断下层 `fixedUpdate` | — |
| `blocksFrameUpdateBelow` | 截断下层 `updateFrame` | 下层即使不跑，栈命令仍只有顶层可排队 |
| `blocksRenderBelow` | 截断下层 `extractRenderScene` | 暂停层可设 `false` 以继续画底层世界 |
| `blocksUIUpdateBelow` | 截断下层 `updateUI` | **不**挡当帧 UI 指针/IME route（route 在栈 dispatch 前） |
| `blocksGameplayInputBelow` | 不截断 fixed/frame 回调 | 下层收到 `SimulationActionSnapshot::suppressed` / `FrameActionSnapshot::suppressed`（空 held/edge）；栈顶永不被此 helper 压制 |

`blocksGameplayInputBelow` 与 `blocksFrameUpdateBelow` **正交**：可只冻输入仍跑下层动画，也可停回调仍保留真实 Action 形状（由 Host 在 dispatch 时选择 real vs suppressed snapshot）。

产品证据：`samples/2d_tilemap_bgfx` 的 `PauseOverlayState` 使用
`blocksGameplayInputBelow` + 多数相位阻断 + `blocksRenderBelow = false`。

栈命令提交点：`updateFrame` 之后、`extractRenderScene` 之前唯一 commit。enter 失败只丢 candidate、
不调用其 `onExit`，原栈保持。pop 至空栈 → `RunExitReason::GameStateStackBecameEmpty`。

## 输入、UI 与世界点击

Platform transition、UI route result 与 Gameplay Action 是三层独立数据：

1. `UIInputRouteProducer` 按原始 ordinal 路由 Pointer/Keyboard/Gamepad/Text/IME；
2. UI 发布 transition consumption 与 continuous-control claim；
3. `ActionMapper` 只处理未被 UI 接管的输入（产出完整 snapshot）；
4. Host 在 stack dispatch 时按 `gameplayInputBlockedForDepth` 决定是否换成 suppressed snapshot。

`InputActionMapConfig::bindings` 是唯一 Action 配置。Digital 和 Gamepad Axis 都映射为浮点 value；axis
依次经过 value-mode 归一化、gameplay deadzone 外重映射与 scale。多个物理 source（包括多个已连接
Gamepad generation）按 `SumClamped` 或 `StrongestMagnitude` 合成，snapshot 通过 `value()` /
`isActive()` 暴露结果。UI consume/claim 会取消对应 gameplay source，并使 digital 抑制到真实 release、
axis 抑制到 neutral/deadzone。

Button、Checkbox、Slider、RadioButton、TextEdit 的当前默认行为在 UI route 阶段完成，早于 Action
Mapping。Tab/Shift+Tab、Enter/Space/KeypadEnter 和 Gamepad South 已有窄 focus/default-action 路径；
通用 Focus Scope、Modal 和持久 Pointer Capture 尚未实现。

未被 UI 消费或 claim 的 primary Pointer transition 可以形成带 `WorldPointerSample` 的 Simulation
edge。坐标使用 last-presented Camera2D 与对应 surface revision 锁存；跨 0 步帧延迟消费时不会用新
Camera 或 resize 重算。

运行时 rebind 不暴露 mapper owner。只有栈顶 `FrameUpdateContext` 返回 phase-local
`InputActionRebinding`；`begin()` 进入 `Capturing`，`commit()` 以 `Reject`/`Swap` 处理冲突并排入
`Queued`，下一次 Action mapping frame 才原子应用。`cancel()` 可取消 Capturing 或 Queued transaction；
capture 绑定的 `GamepadId` generation 在 disconnect，或 raw reset 后不再存在时，以
`DeviceDisconnected` 结束，不迁移到新 generation。

## Phase Context 与借用寿命

所有 Context 都是 callback-only：

| Context | 当前能力 | 失效点 |
| --- | --- | --- |
| `GameStartupContext` | EngineConfig、Platform event subscription facade | `createInitialState()` 返回 |
| `GameStateEnterContext` | Platform subscription、primary UI root builder | `onEnter()` 返回 |
| `FixedUpdateContext` | timing、Simulation Action、可选 Audio borrow | `fixedUpdate()` 返回 |
| `FrameUpdateContext` | timing、Frame Action、可选 Audio、退出请求；仅栈顶可借用 rebind facade | `updateFrame()` 返回 |
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
- active surface 在 `present()` 成功返回后关闭 submission ticket 并释放 FramePin；该时点只结束 CPU
  借用，不声明 GPU 已执行完成或 Asset 已物理退役。
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

## 已落地 vs 后置

| 项 | 状态 |
| --- | --- |
| `TASK-001` Desktop 交互 CPU 默认 `max(1, hw-1)` | Done；工厂 `cpuWorkerCount=0` 仍为 IO-only |
| State 栈 / structural commands / 四相位 `blocks*Below` | Done |
| `blocksGameplayInputBelow` → 下层空 Action snapshot | Done（Host dispatch，非 ActionMapper 内） |
| `blocksUIUpdateBelow` 仅挡 `updateUI` | Done；不回改当帧 UI route |
| `2D-INPUT-ADV` unified digital/analog Action + transactional rebind | Done；本轮测试结果以最终验证记录为准 |
| `RenderFramePacket` + `FramePin` + present-return CPU ledger | Done；固定帧延迟假 fence 已删除，真实 GPU fence 后置 |
| Runtime 拥有 AssetSystem/World | 否；仍由产品 State/样例持有 |
| 通用 Event Queue、多 World/editor orchestration、pass scheduler | 未做；需 ADR/Backlog 后开 |

验证命令与 sample 证据边界见 [测试说明](testing.md)，公开契约见 [Public API](public-api.md)。
