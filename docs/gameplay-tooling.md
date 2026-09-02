# Gameplay 工具层（Timer / Tween / Signal）

`Tina::Gameplay`（`include/tina/gameplay`）提供 gameplay 时序与 gameplay 内部事件投递。它只依赖
`Tina::Core` 与 `Tina::Math`，不链接 Scene、Asset、Physics 或 UI。决策理由见
[ADR 0036](adr/0036-gameplay-tooling-boundaries.md)。

与它相邻但不同的两层：`FixedStepAccumulator`（[ADR 0015](adr/0015-input-and-fixed-step.md)）决定
一帧跑几个 substep；UI keyframe timeline（[ADR 0026](adr/0026-ui-keyframe-timeline-and-layout-animation.md)）
只动 `UIAnimatableProperty` 里的九个 UI 属性、由 `UIContext` 持有。本层是任务层：一段时间之后做
某件事，或在一段时间内把某个值从 A 变到 B。

## 三个 owner 的共同契约

`Scheduler`、`ActionRunner`、`Signal<T>` 都遵守同一组规则：

| 规则 | 含义 |
| --- | --- |
| 固定容量 | 超限返回 `CapacityExceeded`，不重新分配 |
| 单 owner、非线程安全 | 一个 owner 线程驱动；跨线程要自己 marshal |
| delta 由调用方给 | 不自采样时钟，所以能从 `fixedUpdate` 或 `updateFrame` 驱动 |
| 回调内可自由变更 | schedule/play/subscribe/cancel 都允许，含取消自己 |
| 新增下一次生效 | 回调内新增的实体从**下一次** dispatch 才参与 |
| 取消在边界生效 | dispatch 期间的 cancel 在下一个节点边界生效，回调不会被销毁在自己执行中间 |
| 重入被拒绝 | dispatch 中调用 `advance()`/`emit()`/`drain()` 返回 `ReentrantDispatch` 且不改状态 |

最后三条合起来的效果是：**投递顺序永不取决于回调嵌套深度。**

## `Scheduler`

```cpp
auto scheduler = Gameplay::Scheduler::Create({.timerCapacity = 256});

// 0.4 秒后开门
auto open = scheduler->scheduleAfter(Core::Duration{0.4}, [this](const Gameplay::TimerEvent&) {
    m_door.open();
});

// 每 2 秒刷一波，直到取消
auto spawn = scheduler->scheduleEvery(Core::Duration{2.0}, [this](const Gameplay::TimerEvent& e) {
    m_waves.spawn(e.iteration);
});

// 完整形态
auto pulse = scheduler->schedule(Gameplay::TimerDesc{
    .interval = Core::Duration{0.5},
    .initialDelay = Core::Duration{0.0},   // 下一次 advance 即触发
    .repeat = Gameplay::Repeat::times(3),
    .ignoresTimeScale = true,              // 暂停时也走
    .callback = [](const Gameplay::TimerEvent&) {},
});

// 每帧一次（fixedUpdate 或 updateFrame，取决于这些 timer 影响什么）
if (auto status = scheduler->advance(context.fixedDelta()); !status) { return status; }
```

`TimerEvent::iteration` 是 1-based，因此重复 timer 的回调不必自己记次数。

**`interval = 0`** 表示"每次 advance 一次"：零间隔无法细分，所以无论 delta 多大都只触发一次。

**`initialDelay`** 是 `optional` 而不是"零表示未设置"：缺省表示一个 interval（普通周期 timer），
显式的零表示"下一次 advance 就触发"，两者都是真实需求。

**`Repeat`**：`once()` / `times(n)` / `forever()`。`count = 0` 且非 infinite 会被拒绝 —— 不把零
重载成"永远"，否则"我算出来的次数是空"和"跑到取消为止"无法区分。

完成的 timer 自行退役；`isActive()` 随即为 false，`cancel()` 返回 `InvalidHandle`。

## `ActionRunner` 与 `Action`

```cpp
auto runner = Gameplay::ActionRunner::Create({.actionCapacity = 128});

// 缩放到 1.2、停 0.1 秒、再回到 1.0，最后播个音
auto pop = runner->play(Gameplay::Action::sequence(
    Gameplay::Action::tweenFloat(Core::Duration{0.12}, 1.0F, 1.2F,
                                 Gameplay::Easing::BackOut,
                                 [this](float s) { m_icon.setScale(s); }),
    Gameplay::Action::delay(Core::Duration{0.1}),
    Gameplay::Action::tweenFloat(Core::Duration{0.18}, 1.2F, 1.0F,
                                 Gameplay::Easing::QuadraticInOut,
                                 [this](float s) { m_icon.setScale(s); }),
    Gameplay::Action::call([this] { m_audio.playPickup(); })));

// 同时移动并淡出，重复三次
auto blink = runner->play(Gameplay::Action::repeat(Gameplay::Repeat::times(3),
    Gameplay::Action::parallel(
        Gameplay::Action::tweenVec2(Core::Duration{0.3}, from, to,
                                    Gameplay::Easing::CubicOut,
                                    [this](Math::Vec2 p) { m_ghost.setPosition(p); }),
        Gameplay::Action::tweenFloat(Core::Duration{0.3}, 1.0F, 0.0F,
                                      Gameplay::Easing::Linear,
                                      [this](float a) { m_ghost.setAlpha(a); }))));

if (auto status = runner->advance(context.frameDelta()); !status) { return status; }
```

叶子：`tween`（原始 alpha）、`tweenFloat`/`tweenVec2`/`tweenVec3`/`tweenVec4`（插值后的值）、
`delay`、`call`。组合子：`sequence`、`parallel`、`repeat`。

**写目标是 setter 回调**，不是 `EntityId` + 属性枚举。同一个 tween 因此能驱动 transform、UI 值、
audio gain 或普通 gameplay float，而本层不必链接 Scene。

**授权失败是 fail-late 的**：工厂返回 `Action` 而不是 `Result`，非法参数毒化该值而不中断表达式，
第一个失败被保留，由 `play()` 上报。所以下面这段只有一次错误检查，而诊断指向出错的那个子表达式：

```cpp
const Action bad = Action::tweenFloat(Core::Duration{-1.0}, ...);  // 毒化
assert(bad.failed() && bad.failureCode() == GameplayErrorCode::InvalidArgument);
```

毒化只存 `ErrorCode` 与字符串字面量指针，因此失败路径零分配。

**余量跨边界携带**：0.2s 子节点后接 0.3s 子节点、推进 0.25s，第一个完成并把 0.05s 交给第二个。
手写 sequence 正是在这里漂移的。`parallel` 的余量取所有子节点中**最小**的那个 —— 取最大会让外层
sequence 在最慢分支结束前就启动下一个子节点。

**端点精确**：最后一次 apply 的 alpha 是恰好 1，不是累加 elapsed 除出来的值。零时长 tween apply
恰好一次、alpha 为 1，这也是 `call()` 能是一个 tween 而不必新增节点种类的原因。

`Action` 是 move-only 并被 `play()` 消费；节点树上限 `MaximumActionNodeCount = 256`。

## `Easing`

28 条曲线：`Linear`，以及 Quadratic / Cubic / Quartic / Sine / Exponential / Circular / Back /
Elastic / Bounce 各自的 In/Out/InOut。`evaluateEasing(easing, t)` 的端点是精确的（0 和 1 原样
返回），非有限输入按已完成处理。

Back/Elastic/Bounce 会 overshoot，因此这些曲线在运行中间可以离开 `[0,1]` —— 那正是它们的用途，
所以不做 clamp。

**这是独立于 `UI::UIEasing` 的枚举。** `UIEasing` 只有三个值，因为 UI transition 是否 overshoot
由 theme 契约决定；合并会让 UI 发布其 theme 不承认的曲线。

## `Signal<T>`

```cpp
struct DamageEvent final { Scene::EntityId victim; float amount; };

auto damaged = Gameplay::Signal<DamageEvent>::Create({
    .subscriberCapacity = 32,
    .deferredCapacity = 64,     // 0 表示只支持 emit()
});

// 订阅返回 move-only token；销毁或 reset() 即退订
Gameplay::SignalSubscription m_hudSubscription =
    *damaged->subscribe([this](const DamageEvent& e) { m_hud.flash(e.amount); });

const auto delivered = damaged->emit(DamageEvent{...});   // 立即，按订阅顺序
const auto queued = damaged->post(DamageEvent{...});      // 入队，下一次 drain()
const auto drained = damaged->drain();                    // 投递本次调用前入队的 payload
```

**按 payload 类型模板化**，不是一个 type-id keyed 的中心 bus：中心 bus 需要运行时类型擦除，而它
承认的错误（为某事件名订阅了错误的 payload 类型）会变成静默 no-op 而不是编译错误。

**`emit()` 与 `post()` 都是显式的**。`emit()` 立即跑订阅者；`post()` 入队 —— 在物理回调内或另一个
signal 的订阅者内发布时需要它，因为在那里直接投递等于在别人的迭代中间跑 gameplay 代码。

`deferredCapacity = 0` 时 `post()` 返回 `DeferredDeliveryUnavailable`，不静默降级为 `emit()`：
两者的差别正是调用方选 `post()` 想要的那个顺序。

drain 期间订阅者自己 post 的 payload 留到**下一次** drain，这是自我重发的 signal 不会在一帧内跑
到底的原因。

`SignalSubscription` 持有 weak 引用，因此对已销毁 signal 的 reset 是 no-op —— State 的拆卸顺序
并不总是构造顺序的逆序。

## 暂停与时间缩放

`Scheduler` 与 `ActionRunner` 各有 `setTimeScale()`（0 是合法的全暂停；负值与非有限值被拒绝）。
单个 timer/action 可以 `ignoresTimeScale = true` 绕过它 —— 暂停菜单自己的动画和"把玩家解卡"的
watchdog 都需要，因为用 gameplay 时间缩放去缩放它们意味着它们恰好在被需要时停摆。

本层不知道 `GameStatePolicy` 的存在。要暂停整个游戏，要么设 time scale 为 0，要么不调 `advance()`。

## 追赶与上限

| 配置 | 作用 | 超出时 |
| --- | --- | --- |
| `SchedulerConfig::maximumCatchUpStepsPerAdvance` | 一次 advance 内单个 timer 的最大投递次数 | 整周期积压被丢弃并计入 `discardedCatchUpSteps` |
| `ActionRunnerConfig::maximumRepeatIterationsPerAdvance` | 一次 advance 内单个 Repeat 节点的最大重启次数 | 剩余迭代推迟到下次 advance，并计入 `clampedRepeatIterations` |

timer 积压**丢弃而不携带**：携带会让卡顿后的每一帧都发满上限，把一次停顿变成一列停顿。丢弃让 timer
重新同步到现在，计数器让这次损失在 stats 里可见而不是只在行为里可见。

**Repeat 的上限方向相反：它推迟迭代而不丢弃迭代。** 两者的差别来自被限界的东西不同 —— timer 积压是
已经流逝的时间，补投再多次也追不回那段时间；而 `Repeat::times(n)` 的 n 是授权的次数，少跑一次就是内容
没按授权跑完。所以触发上限时子树的游标会被清掉，下一次 advance 从那一迭代继续。

`clampedRepeatIterations` 非零通常意味着某个被 repeat 的子树总时长为零 —— 那种情况看起来和卡死
一模一样，所以它被限界并计数而不是让它转下去。

## 当前限制

- **没有 coroutine。** `Action` 的组合子覆盖 `Sequence`/`Spawn`/`Repeat` 的表达力，但不能在任意
  语句中间挂起。
- **没有 tween 的 relative/by 变体、没有 reverse、没有 speed 节点。**
- **`Action` 授权在堆上分配**（不取自 runner 的存储）：授权经常发生在该 runner 自己的回调执行
  期间。
- **`Signal<T>` 每个 payload 类型一份实例化**，signal 种类多的产品会付编译期成本。
- **`Signal<T>` 在 dispatch 期间取消订阅后再订阅，可能拿到 `CapacityExceeded` 而非空槽。** 被取消的
  槽在本次 dispatch 结束前既不 active 也未回到 free list，此时增长 slot 存储会重分配掉正在执行的那个
  callback，所以这个窗口选择拒绝。容量按并发订阅者的峰值留出余量即可避开。
- **尚无 sample 消费面**（单元测试见 `tests/gameplay/`）。
