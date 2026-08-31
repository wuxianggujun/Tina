# ADR 0036：`Tina::Gameplay` 时序工具层边界

- 状态：Accepted
- 日期：2026-08-31
- 决策者：Tina maintainers

## 背景

在本 ADR 之前，引擎没有 gameplay 时序工具层。全仓搜索确认：`Timer`、`Scheduler`、`Tween`、
`Sequence`、`Coroutine` 在 `include/` 与 `src/` 零命中；唯一的 easing 是
`include/tina/ui/UIMotion.hpp:26` 的 `UIEasing`（三个值，UI 专用）；唯一的 tween 能力是 UI 的
keyframe timeline（ADR 0026），只作用于 `UIAnimatableProperty` 枚举里的九个 UI 属性，且由
`UIContext` 持有、按窗口分配容量，游戏无法用它驱动一个 gameplay float。

现有的时间设施都在**帧层**而不是**任务层**：`FixedStepAccumulator`（ADR 0015）决定一帧跑几个
substep，`Scene2DRuntime::fixedUpdate` 推进粒子与拖尾，`AudioVoiceFadeDesc` 是单个 voice 的淡
入淡出。这些都不能表达"0.4 秒后开门"或"缩放到 1.2 再回到 1.0"。

同样缺失的是 gameplay 侧的解耦手段。`PlatformEventSubscriptions`（`include/tina/runtime/
PlatformEvents.hpp`）与 `UIRoutedPointerListenerToken` 已经建立了 scoped-token 订阅的先例，但
两者都只覆盖各自的输入域；两个 gameplay owner 之间要通信，当前只能互相持有指针或裸回调成员。

参考实现调研：cocos2d-x 有 `ActionManager` + `Action`/`ActionInterval`/`Sequence`/`Spawn`/
`Repeat`/`EaseXxx` 与 `Scheduler`；Unity 有 Coroutine 与第三方 tween 生态；Godot 有 `Tween`/
`Timer` 节点。三者都填补了同一层。按 `docs/carbon-reference.md` 的规则，它们作为取舍来源而非
移植源。

## 决策记录

| # | 决策点 | 采纳 | 主要备选与取舍 |
| --- | --- | --- | --- |
| D1 | 模块形态 | **独立静态库 `Tina::Gameplay`，只依赖 `Tina::Core` + `Tina::Math`** | 放进 `Tina::Runtime`：Runtime 已是唯一组合根，加进去会让 timer 隐式绑定帧循环所有权；放进 `Tina::Gameplay2D`：那个 target 链接 Scene/Physics/Asset/Audio（ADR 0031），而本层不需要其中任何一个，且 `Gameplay2D` 只在 `TINA_BUILD_PHYSICS2D` 下存在 |
| D2 | 时间来源 | **调用方显式传 delta** | 自采样 `MonotonicClock`：帧循环已拥有 fixed/frame 划分（ADR 0015），自采样的对象根本无法从 `fixedUpdate` 驱动，也无法被 `GameStatePolicy` 的向下阻断覆盖 |
| D3 | 失败表达 | **`Result`/`Status`，占用 `ErrorDomain::Gameplay = 17`** | 照 ADR 0035 用 `optional`：Math 的入口是纯函数且"未命中"是正常结果，而这里的失败是 CapacityExceeded、ReentrantDispatch、InvalidHandle —— 每一个都需要区分原因 |
| D4 | easing 枚举 | **新建 `Gameplay::Easing`（28 个值），不复用 `UI::UIEasing`** | 合并成一套：`UIEasing` 只有三个值，因为 UI transition 是否 overshoot 由 theme 契约决定；gameplay 默认需要 Back/Elastic/Bounce。合并要么让 UI 发布其 theme 不承认的曲线，要么让 gameplay 只剩三条 |
| D5 | tween 写目标 | **调用方提供 setter 回调** | 直接持有 `Scene::EntityId` + 属性枚举：会让本层链接 Scene，且属性枚举必须为每个新组件字段扩容。setter 方案让同一个 tween 能驱动 transform、UI 值、audio gain 或普通 float |
| D6 | Action 授权失败时机 | **fail-late：工厂返回 `Action` 而非 `Result`，毒化后由 `play()` 上报** | 每个工厂返回 `Result`：组合五个 tween 就要五次错误检查，表达式无法嵌套。只在 `play()` 校验：丢失了"哪个子表达式错了" |
| D7 | Action 节点引用 | **索引，非指针** | 指针：子树在自己的 program 里创建后被 splice 进父 program，任何提前取得的指针必然悬垂 |
| D8 | 重入 | **拒绝并返回 `ReentrantDispatch`，不递归执行** | 允许嵌套 dispatch：投递顺序会取决于嵌套深度。callback 内的 schedule/play 一律**下一次** advance 生效，cancel 在下一个节点边界生效 |
| D9 | 追赶上限 | **有界 + 计数被丢弃的次数，不携带积压** | 无界追赶：一次 500 ms 卡顿会让 10 ms timer 连发 50 次，把一次停顿变成第二次；只发一次并静默丢 49 次：损失不可见。携带积压：卡顿后的每一帧都会发满上限，把一次停顿变成一列停顿 |
| D10 | signal 形态 | **按 payload 类型模板化的 `Signal<T>`，scoped token 订阅** | 单一中心 bus + type id：需要运行时类型擦除与 any-like payload，而它承认的错误（为某事件名订阅了错误的 payload 类型）会变成静默 no-op 而不是编译错误 |
| D11 | signal 投递 | **`emit()` 立即 + `post()`/`drain()` 延迟，二者都显式** | 只有立即投递：物理回调内或另一个 signal 的订阅者内发布会在别人的迭代中间跑 gameplay 代码。只有延迟：调用方失去了它选择立即投递时想要的顺序 |
| D12 | `deferredCapacity = 0` 时 `post()` | **返回 `DeferredDeliveryUnavailable`** | 静默降级为 `emit()`：两者的差别正是调用方选 `post()` 想要的那个顺序 |
| D13 | `Repeat{count = 0}` | **拒绝，不解释为"永远"** | 零即无限（cocos2d-x 与多数 tween 库的做法）：会让"我算出来的次数是空"与"跑到取消为止"无法区分，而这两种失败在运行时表现完全不同 |
| D14 | 完成后的实例 | **自行退役** | 保留待查询：`activeCount` 会随场景生命周期单调增长，最终用永远不会再触发的 timer 耗尽 `timerCapacity` |

## 决定

`include/tina/gameplay` 是 gameplay 时序（timer/tween/sequence）与 gameplay 内部事件投递的定义点。
它只依赖 `Tina::Core` 与 `Tina::Math`，不知道 Scene、Asset、Physics 或 UI 的存在。

### 1. 三个 owner，一致的契约

`Scheduler`、`ActionRunner`、`Signal<T>` 共享同一组规则，因为它们的失败模式相同：

- 固定容量，超限是 `CapacityExceeded` 而非重新分配；
- 单 owner、非线程安全；
- 回调内可自由 schedule/play/subscribe/cancel；新增的实体从**下一次** dispatch 才参与，
  取消在下一个节点边界生效。因此投递顺序永不取决于嵌套深度；
- dispatch 期间重入 `advance()`/`emit()`/`drain()` 返回 `ReentrantDispatch` 且不改变任何状态；
- dispatch 标志由 scope guard 恢复：回调是游戏代码、可能抛出，而永久停留在 "dispatching" 的
  owner 会在此后整个进程里拒绝每一次 advance。

### 2. 余量必须携带，积压必须丢弃

这两条方向相反且都是刻意的。

**余量携带**：100 ms 的 timer 被 60 ms 推进两次，触发一次并留下 20 ms。归零会让周期每次损失一
次舍入而缓慢漂移。sequence 的子节点边界同理：0.2s 子节点后接 0.3s 子节点、推进 0.25s，第一个
完成并把 0.05s 交给第二个。手写 sequence 正是在这里漂移的。

**积压丢弃并计数**：超过 `maximumCatchUpStepsPerAdvance` 的整周期不携带，而是计入
`discardedCatchUpSteps`。见 D9。

### 3. tween 端点精确

最后一次 apply 用的 alpha 是**恰好 1**，而不是累加的 elapsed 除出来的值。"精灵停在 199.997"是
这一层最典型的缺陷。零时长的 tween apply 恰好一次、alpha 为 1，这也是 `Action::call()` 能是一个
tween 而不必新增节点种类的原因。

### 4. 授权期不分配失败路径

`Action` 的毒化状态只存 `ErrorCode` 与一个字符串字面量指针，因此授权失败路径零分配。节点树在毒
化时立即释放：毒化的 `Action` 无法被 play，保留其 setter 只会无谓延长捕获对象的生命周期。

### 5. 订阅令牌在 signal 销毁后仍安全

`SignalSubscription` 持有 weak 引用，因此对已销毁 signal 的 reset 是 no-op 而不是悬垂写。这不是
可选的便利：State 的拆卸顺序并不总是构造顺序的逆序。

## 结果

- 首次存在 `Easing`（28 曲线）、`Scheduler`/`TimerId`、`Action`/`ActionRunner`/`ActionId`、
  `Signal<T>`/`SignalSubscription`、`Repeat`。
- 占用 `ErrorDomain::Gameplay = 17` 与 `MemoryTag::Gameplay = 14`；`MemoryTagCount` 现为 15。
- 成本与限制：
  - **没有 coroutine**。`Action` 的组合子覆盖 cocos2d-x `Sequence`/`Spawn`/`Repeat` 的表达力，
    但不能在任意语句中间挂起。需要那个的话是独立切片，且需要先决定栈的所有权。
  - **`Action` 树上限 256 节点**，且授权期在堆上分配（不取自 runner 的存储）：授权经常发生在该
    runner 自己的回调执行期间，把 runner 的存储切一块给它正是这套设计要避免的别名。
  - **没有 tween 的 relative/by 变体、没有 reverse、没有 speed 节点。** 每一个都需要真实消费者
    才加，理由同 ADR 0035 的 D3。
  - **`Signal<T>` 每个 payload 类型一份实例化**，因此 signal 数量多的产品会付编译期成本；这是
    D10 换取"错误 payload 类型是编译错误"的对价。
  - **`Scheduler` 与 `ActionRunner` 的 delta 由调用方给**，因此"暂停整个游戏"要么设 time scale
    为 0，要么不调 advance —— 本层不知道 `GameStatePolicy` 的存在。
- 已建立的门禁：`tina_gameplay` 编译进 `Tina::GameSDK` 聚合与安装 package；公开头只含标准库与
  `tina/` 头。**待**：单元测试与 sample 消费面。

## 被拒绝方案

- **复用 UI 的 keyframe timeline（ADR 0026）驱动 gameplay 值**：它的属性集是 `UIAnimatableProperty`
  的九个 UI 属性，容量按窗口分配、由 `UIContext` 持有，且其 publication 走 Layout/Hit/Paint 事务。
  让它写一个 gameplay float 需要把 UI 的 presentation owner 变成通用 tween owner。
- **把 timer 放进 `EngineHost`**：会让每个游戏共享一份容量，并让 timer 生命周期与引擎而非 State
  绑定 —— 而 State 退出时正是必须取消其 timer 的时刻。
- **移植 cocos2d-x 的 `ActionManager`**：其 `Action` 持有 `Node*` target 并由 `Node` 的
  `runAction` 驱动，这会把本层绑到 Scene 上（见 D5）；同时它的 `Action` 是引用计数多态类，与
  ADR 0004/0007 的取舍不一致。按 `docs/carbon-reference.md`，只借鉴取舍不移植代码。
- **`Repeat` 用零表示无限**：见 D13。
- **让 `advance()` 在重入时递归执行**：见 D8。投递顺序取决于嵌套深度的系统，其缺陷无法复现。
