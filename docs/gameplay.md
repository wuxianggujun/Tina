# 游戏入口与 State

本文描述当前游戏侧入口。Runtime 已支持私有 `GameStateStack`、通过 `FrameUpdateContext` 排队的
push/pop/replace/policy 命令，以及 `GameStatePolicy` 对 fixed/frame/render/UI 的向下阻断调度
（`RUNTIME-001`）。Game SDK 仍不能直接持有可变 stack。

## 当前接口

### `IGameApplication`

```cpp
class IGameApplication {
public:
    virtual ~IGameApplication() noexcept = default;

    virtual Core::Result<std::unique_ptr<IGameState>> createInitialState(
        GameStartupContext& context) = 0;
    virtual void onShutdown(GameShutdownContext& context) noexcept = 0;
};
```

Application 只负责创建初始 State 与最终 shutdown，不接收逐帧回调，也不拥有 backend。

### `IGameState`

```cpp
class IGameState {
public:
    virtual ~IGameState() noexcept = default;

    virtual Core::Status onEnter(GameStateEnterContext& context) = 0;
    virtual void onExit(GameStateExitContext& context) noexcept = 0;
    virtual GameStatePolicy initialPolicy() const noexcept = 0;

    virtual Core::Status fixedUpdate(FixedUpdateContext& context);
    virtual Core::Status updateFrame(FrameUpdateContext& context);
    virtual Core::Status extractRenderScene(RenderSceneExtractionContext& context) const;
    virtual Core::Status updateUI(UIUpdateContext& context);
};
```

默认逐帧实现返回 success。Runtime 持有私有 `GameStateStack`（定容 8）：enter 成功后采样
`initialPolicy()`，帧内可 `requestPush` / `requestPop` / `requestReplace` / `requestPolicyChange`。
四相位 `blocksFixedUpdateBelow` / `blocksFrameUpdateBelow` / `blocksRenderBelow` /
`blocksUIInputBelow`（控制下层 `updateUI` 是否调用；**不**回改当帧 UI route）已调度。
`blocksGameplayInputBelow`：下层 `fixedUpdate`/`updateFrame` 收到空 action snapshot
（`SimulationActionSnapshot::suppressed` / `FrameActionSnapshot::suppressed`）。

## 生命周期

```text
EngineHost::run
  -> IGameApplication::createInitialState
  -> bind primary UIContext, or Headless unavailable
  -> IGameState::onEnter
  -> sample initialPolicy + commit startup UI snapshot + push stack
  -> frame loop (top-down phase dispatch; Transition Commit after Frame Update)
  -> exit each committed State (onExit) then IGameApplication::onShutdown
  -> module shutdown
```

创建 initial State、UI bind 或 `onEnter()` 在提交前失败时：candidate 被销毁，不调用 candidate
`onExit()`，也不调用 Application `onShutdown()`。State 提交后，正常 close、游戏请求退出或 Runtime
失败都会调用一次 `onExit()` 和一次 `onShutdown()`；上下文提供 `RunStopCause` 与只借用的失败 Error。

## 游戏侧所有权

推荐每个产品 State 按功能组合 owner：

```text
Game2DState / Game3DState
  -> Scene::World
  -> CatalogSnapshot / AssetSystem / AssetLease
  -> optional PhysicsWorld2D
  -> gameplay models and controllers
  -> UI root + listener tokens
```

这些 owner 不进入全局变量。State 在 `onExit()` 中先停止 ingress/回调 token，再释放 root、physics、
asset lease 和 world。Runtime Context、RenderSceneWriter、UI updater 与 Audio pointer都是 phase-local
borrow，禁止存入成员。

`Tina::Desktop::CreateEngine` 当前组合：

- GLFW WindowSurface platform；
- bgfx RenderDevice；
- BoundedTaskSystem，默认补1个 IO worker和有界 IO/Main queue；
- backend-neutral `AudioEngine`；
- 启用 FreeType 且找到字体 fixture 时创建带 rasterizer 的 UIContext。

Desktop 并非 DisabledTaskSystem。交互路径经 `resolveDesktopTaskSystemParams` 默认
`max(1, hardwareConcurrency-1)` CPU worker（TASK-001 Done）；直接工厂 `cpuWorkerCount=0` 仍为
IO-only / NotSupported。AssetSystem/Physics2D/miniaudio device 由产品或 feature 图进一步组合。

## Fixed 与 Frame domain

输入在 **State 栈调度之前** 已经完成 Platform normalize、UI route/consume/claim 与 Action Mapping
（因此 `blocksUIInputBelow` 不能回改当帧已产生的 route；它只阻断下层 `updateUI`）：

- `FixedUpdateContext::simulationActions()` 只读目标 simulation tick 的 action；
- `FrameUpdateContext::frameActions()` 只读当前 render frame 的 action；
- 0 fixed-step 帧保留 Simulation edge；追赶多个 fixed step 时只在第一个目标 tick 消费一次；
- `requestExitAfterFrame()` 完成当帧 render/UI/submit/present 后退出。

影响玩法正确性的 movement、collision、Physics step、tile command 应放在 `fixedUpdate()`；Camera follow
presentation、菜单动画等可放在 `updateFrame()`，但不能反向修改已经提交的 simulation 结果。

## Scene 与 UI 输出

`extractRenderScene()` 通过 callback-only `RenderSceneWriter` 写已解析的 Camera/Sprite/Mesh POD；State
不能发布 view、持有 writer 或把 AssetHandle 留给 backend。产品通常先确保 AssetLease/GPU binding 有效，
再让 Scene extraction 写对应 key。

`onEnter()` 可通过 `PrimaryWindowUIRootBuilder` 创建 root；`updateUI()` 通过绑定该 root 的 updater 修改树。
builder/updater 在回调结束时无条件失效。`UIRootOwner` 和 move-only listener token 可以保存在 State；
listener 不保活 Context/root，退出时应先 reset listener 再释放 root。

## 当前产品 State

| 产品 | 当前组合 |
| --- | --- |
| `tina_sample_2d` | Catalog recipe、Texture2D/TileMap、CharacterController、Scene 2D、UI、Audio；完整 feature 图含 Box2D、FreeType、miniaudio |
| `tina_sample_3d` | glTF cook、Catalog/Prefab、StaticMesh/Material upload、Scene 3D、bgfx |
| `tina_sample_null` | Headless/Null 生命周期与 phase 契约 |

样例内部的参数解析、fixture 和计数器不是通用 Game SDK。正式 SDK package/export 与通用
save/load orchestration 尚未完成。

## `RUNTIME-001` 状态

已落地（见 `GameStateStackTests` / `GameStateStackIntegrationTests`，以及
`tina_sample_2d` 自动 pause overlay 产品证据）：

- push/pop/replace 只在 `updateFrame` 后、`extractRenderScene` 前唯一 commit；
- enter 失败丢弃 candidate 且不调用 `onExit`，栈保持；
- `blocksFixedUpdateBelow` / `blocksFrameUpdateBelow` / `blocksRenderBelow` /
  `blocksUIInputBelow`（下层 `updateUI`）自顶向下阻断；
- structural command 仅栈顶可排队；pop 空栈 → `GameStateStackBecameEmpty`。

仍后置：

- 交互式菜单/暂停（按键切换）；当前 `tina_sample_2d` 为自动收尾 push/pop 证据（≥60 帧 smoke）；
- State UI root / listener / TaskGroup / AssetLease 在 transition 后的完整 stale-owner 矩阵；
- 交互式菜单/暂停（按键切换）；`tina_sample_2d` 已有自动收尾 pause overlay 证据。

帧阶段详见 [Runtime](runtime.md)，任务状态见 [Backlog](backlog.md)。
