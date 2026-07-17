# ADR 0014：EngineHost 阶段 Context + IGameApplication/IGameState

- 状态：Accepted
- 日期：2026-07-17
- 接受日期：2026-07-17

## 背景

旧 Application 同时充当全局服务入口、模块 owner 和主循环；Scene 又形成第二套生命周期。
上一版候选名称 `IGame + IAppState` 仍有两个问题：`IGame` 无法从名称判断是程序入口、World
还是玩法对象，并且两者都实现帧回调，导致 World/UI/Render 可以放在两个位置。

单个通用 EngineContext 会把 Service Locator 换一个名字；二值 pause/resume 也无法表达“停止
Fixed/Input 但继续 Render”的覆盖状态。

## 决定

- `EngineHost` 是唯一非全局 module owner，通过短生命周期 Phase Context 暴露最小能力；
- `IGameApplication` 只负责游戏程序启动/停止：创建恰好一个 initial `IGameState`，没有帧回调；
- `IGameState` 是 Menu、Settings、Game2D、Game3D、Pause 唯一的帧行为入口；
- Runtime 独占 `GameStateStack`，不保留并列 SceneManager；
- `GameStatePolicy` 分别控制 Gameplay/UI Input、Fixed Update、Frame Update、Render 向下传播；
- State enter 是事务，已提交 State 的 exit `noexcept` 且恰好一次；
- push/pop/replace/policy change 只在 Frame Update 后的 State Transition Commit 提交；
- Phase Context 没有 `services()/get<T>()`，不允许保存或跨阶段使用。

启动事务为：

```text
gameApplication.createInitialState()
  -> initialState.onEnter()
  -> sample initialPolicy + initial UI layout/snapshot
  -> commit GameStateStack
```

任何一步失败完整回滚，不调用 candidate `onExit` 或 Application `onShutdown`；提交后即使帧
失败，也先按“关闭 ingress → signal cancel → TaskGroup barrier/join → onExit → RAII 析构”退出全部
State，再调用一次 `IGameApplication::onShutdown`。

## 命名结果

| 名称 | 读者应立即理解的含义 |
| --- | --- |
| `IGameApplication` | 整个游戏程序的生命周期入口 |
| `IGameState` | 一个可逐帧运行的菜单/游戏/覆盖状态 |
| `GameStateStack` | Runtime 拥有的状态栈 |
| `GameStatePolicy` | Runtime 持有的、状态对下层各阶段的 committed 传播规则 |
| `GameStateCommands` | 绑定当前 State 的延迟 push/pop/replace/policy-change 请求 |

删除公共 `IFrameClient`；默认空帧方法直接定义在 `IGameState`，避免另一层含糊基类。
`GameStateStack` 是 Runtime private，Game SDK 不获得可变 stack 引用。

`IGameState::initialPolicy()` 只在 enter 成功后采样一次；Runtime 保存 committed policy，后续只能
通过 `requestPolicyChange()` 修改。Structural command 只允许当前栈顶 State 每帧提交一个；
push/replace 接受后接管 candidate `unique_ptr`，enter 失败回滚并直接析构，不调用 `onExit`。
Command queue 有固定容量和 sequence，pop 最后一个 State 表示正常退出。

State Transition Commit 位于 Frame Update 与 Render Scene Extraction 之间。新 State 因而能在同帧
完成 Render/UI snapshot，下一帧开始命中，且不会要求第二次 layout。已提交 State 退出固定为：
从后续 dispatch 移除并关闭 ingress → signal cancellation → TaskGroup barrier/join → `onExit` → RAII
析构与残留断言。

## 实现分期

首个 Null Runtime 切片只实现已提交的单个 initial State，以及
`FrameUpdateContext::requestExitAfterFrame()`。该请求是主线程幂等 latch：当前帧仍须完成
Render Scene Extraction、UI、Render 处理与 Deferred Cleanup；只有这些阶段全部成功后才正常退出，
同帧错误优先于退出请求。M6-A Null backend 的 Render 处理必然是 submit/present；M7 之后 Active
surface 正常 submit/present，Suspended surface 返回明确 `SkippedSuspendedSurface` 并回滚 frame-local
pin，不伪造 GPU submission。首帧 `frameIndex` 为0，Deferred Cleanup 完成后才递增。

`GameStateStack`、`GameStateCommands`、多 State 传播与 Transition Commit 的公共最终语义在本 ADR
冻结，但实现推迟到具备真实 Menu/Settings/Gameplay 消费者的切片。首个切片不得为证明未来接口而
预先实现空队列、假状态栈或无消费者策略。

## 代价

- Phase Context/State transaction 需要更多小类型和失败注入测试；
- 旧 Scene 生命周期必须迁移，不能机械套壳；
- `IGameApplication` 与 `IGameState` 的共享产品服务必须通过构造函数显式传递；
- 调用者无法随时取得所有 Engine service，这是有意约束。

## 被拒绝方案

- `IGame`：名称无法说明是程序、世界还是玩法对象；
- `IGame` 与 State 都有逐帧回调：形成双入口；
- 全局 Application/Service Locator/保存 EngineContext：隐藏依赖和借用寿命；
- `IGameApplication` 与 SceneManager 两套平行栈：输入、渲染和退出顺序会分叉；
- 二值 onPause/onResume：无法表达各阶段独立遮挡。
