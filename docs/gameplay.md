# 游戏入口与 State

本文描述当前游戏侧入口。Runtime 已支持私有 `GameStateStack` 与通过 `FrameUpdateContext` 排队的
push/pop/replace/policy 命令（`RUNTIME-001` 首切片）；Game SDK 仍不能直接持有可变 stack。

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

默认逐帧实现返回 success。当前 Runtime 只保存一个 committed State；`initialPolicy()` 已采样，但
`blocks*Below` 尚无“下层 State”可调度，不能据此宣称 stack 已完成。

## 生命周期

```text
EngineHost::run
  -> IGameApplication::createInitialState
  -> bind primary UIContext, or Headless unavailable
  -> IGameState::onEnter
  -> commit startup UI snapshot and the single State
  -> frame loop
  -> IGameState::onExit
  -> IGameApplication::onShutdown
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

Desktop 并非 DisabledTaskSystem。其 CPU worker 默认值仍为0，与 ADR 0017 的交互默认约定冲突，见
`TASK-001`。AssetSystem/Physics2D/miniaudio device 由产品或 feature 图进一步组合。

## Fixed 与 Frame domain

输入在进入 State 前已经完成 Platform normalize、UI route/consume/claim 与 Action Mapping：

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

样例内部的参数解析、fixture 和计数器不是通用 Game SDK。正式 SDK package/export、State stack 与通用
save/load orchestration 尚未完成。

## `RUNTIME-001` 验收边界

未来 State stack 至少需要同时关闭：

- push/pop/replace 只在唯一 commit 点生效；
- enter/exit 顺序、失败回滚与 Application shutdown 明确；
- input/fixed/frame/render/UI policy 对上下层的阻断有测试；
- State UI root、listener、TaskGroup 与 AssetLease 在 transition 后无 stale owner；
- command 不允许在 callback 中直接修改正在遍历的 stack。

在这些条件完成前，文档与代码都应继续使用“当前单 State”表述。帧阶段详见 [Runtime](runtime.md)，
任务状态见 [Backlog](backlog.md)。
