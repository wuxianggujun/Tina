# ADR 0015：PlatformFrameView、Action domain 与逐 substep 提交

- 状态：Accepted
- 日期：2026-07-16
- 接受日期：2026-07-17
- 实施状态：M7-A Headless Platform/Input、Action Mapper、fixed-step latch、
  `PlatformEventDispatcher` 与私有 GLFW Window/Keyboard/Pointer/committed text producer 已落地；
  M7-C1a 已落地 UI-owned route-result view ABI，M7-C1c-b3a 已让 Pointer Button/Wheel 固化事件时
  logical position，M7-C1c-b3b 已实现独立的 Runtime-private `UIInputRouteProducer`；`EngineHost` 仍传
  canonical `None` 且未拥有/选择 `UIContext`，production Gamepad、完整 DPI、Windows IMM32 与完整
  Runtime UI 集成仍是目标

## 背景

每帧最终按键布尔值无法表达同 Poll 的 Down→Up、Wheel/Text/IME 顺序；UI 之后才路由会产生
玩法穿透；把同一 Press 同时交给 Frame/Fixed Update 会双重执行。多个追赶步只在帧末提交
World command，又会让第 N+1 步看不到第 N 步结果。

## 决定

Platform 每轮 Poll 返回 `ContinueFrame{PlatformFrameView}` 或 `ExitRequested` 的 tagged
`PlatformPollResult`；failure 只走外层 `Result`。只有 Continue 分支才生成一份不可变
`PlatformFrameView`：最终 `WindowMetricsSnapshot`/`WindowInputSnapshot`、Engine 级设备 Snapshot、
`PlatformEventBatch` 与按 Platform sequence 排序的 transition batch。ExitRequested 不创建 frame
view、不分配 engine frame index，也不泵送当前 `PlatformEventDispatcher`、未来通用 Runtime Event Queue，
或进入本轮 Input/任何新 frame phase。Continue 分支中的 `PlatformEventBatch` 当前只由 Runtime-owned
`PlatformEventDispatcher` 同步分发平台生命周期通知；它不是通用 Gameplay/Domain Event Queue。
UI 使用上一帧稳定布局逐 transition 路由；UI 输出与当前 `PlatformFrameView` 绑定的
`Tina::UI::InputTransitionConsumptionView`，按 transition ordinal 标记一次性事件是否已被
消费。Gameplay Action Mapping 只读取未消费 transition，不允许 UI 修改 Platform Snapshot。

仅有逐帧 consumption 仍不足以阻止持续输入穿透，因此同时建立
`Tina::UI::ContinuousControlClaimsView` 与 Action Mapper 的 `suppressedUntilReleaseOrNeutral` 状态：

- UI 消费数字控制的 Down 后，对应 physical control 在 Action Mapper 中保持 suppressed，直到
  匹配的真实 Release；该 Release 只解除 suppression，不生成 Gameplay edge；
- UI 通过 `Tina::UI::ContinuousControlClaimsView` 接管一个已经 held 的 digital control 时同样立即建立
  suppression，取消该 source 尚未完成的 Gameplay edge/repeat，并以 Action Cancel 而非 normal
  Released 表达清理；
- UI claim 连续 axis/pointer delta 后，该 control 保持 suppressed，直到回到配置 dead zone/neutral；
- Focus loss、设备断开与输入流 reset 使用 `InputCancelTransition`/Reset 语义清理 UI Capture、composition、Action
  edge 和 repeat；不能伪造普通 Up，因为普通 Up 可能触发 Button click 或 Gameplay release action；
- `Tina::UI::InputTransitionConsumptionView` 只活到当前 `PlatformFrameView`；
  `Tina::UI::ContinuousControlClaimsView` 是当前路由结果，
  `suppressedUntilReleaseOrNeutral` 由 Action Mapper 跨帧持有。三者职责不能合并成修改全局 held
  状态的一个 bitset。

Action 明确属于 Simulation 或 Frame domain。同一 active Input Context 中，同一 transition 默认
只能映射到一个 domain；确需两个行为必须配置两条显式 binding。Frame Action transition 只在
当前 Frame Update 消费一次，帧末丢弃。

Simulation Action 使用有界、保序的 `SimulationActionTransitionBatch`。映射得到的 transition
绑定到当前 next uncompleted simulation tick：

- 本帧有 fixed step 时，batch 只由第一个实际 substep 消费，后续最多3个追赶步只读取 action 的
  held/axis snapshot，不重复 edge；
- 本帧0步时，batch 与最终 action state 跨 Render Frame 保留；后续 Down→Up 等 transition 继续按
  source sequence 追加到同一目标 tick，不能压缩成单个 pressed/released bool；
- 第一个实际执行该 tick 的 `FixedUpdateContext` 读取一次完整有序 batch，callback 返回后清空；
- batch 使用配置固定容量并预留 Reset slot。容量耗尽时不得静默丢弃部分 edge：Runtime 记录
  overflow metric，丢弃 reset 之前尚未消费的 transient edge，插入显式
  `SimulationInputStreamReset`，保留最终 action state，并继续记录 reset 之后仍可容纳的 transition；
  当前 held/non-neutral control 进入 suppressed，直到真实 release/neutral；
- Replay 只记录归一化 Simulation Action 的目标 tick、最终 action state、有序 transition 与显式
  reset marker；不记录 GLFW key、UI UINodeId 或消费实现细节。相同记录必须产生相同 tick 输入。

原始 InputTransitionBatch 同样为预分配有界存储。可合并 Move 优先合并；仍满时产生不可丢的
`InputStreamReset`，取消当前 UI/Action 交互，并由本帧 Action Mapper 从最终设备状态 resync、后续帧
继续验证 retained state。Focus loss 使用
`InputCancelTransition(reason=FocusLost)`；两者都不是普通 Up/Release。
Text/Composition 先经过严格 UTF-8 与 cursor 校验，再整段复制到 Create 时预分配的 byte arena；
默认16 KiB、硬上限1 MiB。byte arena 满时不截断字符串，使用 raw batch 的保留 slot 写入
`InputStreamReset(reason=TextByteCapacityExceeded)`。
Key、Pointer Button 与 Gamepad Button 的最后一个未被后续 cancel/raw reset 覆盖的 digital edge
必须与 final held Snapshot 一致；实现使用固定数组单次扫描，不分配、不做 O(n²) 回看。
M7-A 仅接受 `PrimaryPointerId`（0）：Pointer snapshot、Button/Move/Wheel transition 与 pointer binding
使用其他 id 均结构化失败。同一帧所有 `GamepadSnapshot` 必须来自同一个 registry owner，每个 slot
index 唯一；同 slot 的不同 generation 不能同时作为最终快照。
Pointer Button/Wheel transition 必须保存该事件发生时的 window-logical position。UI hit-test 不得用
Poll 结束时的最终 Pointer snapshot 替换它；否则 Button/Wheel 后同 Poll 内的 Move 会改变历史事件的
目标。生产 GLFW adapter 使用按 callback 顺序维护的 backend-owned pointer state 固化该坐标，builder
同时拒绝非有限位置。

M7-C1c-b3b 的 Runtime-private producer 只路由 Pointer Move/Button/Wheel，并按 raw transition ordinal
生成 consumption bit；reset、cancel 和非 Pointer 项保留 ordinal hole，不路由、不伪造 Up。claims 当前
恒为 canonical `None`。producer 使用双预分配 PMR bitset；supplied `memory_resource` 必须长于 producer，
300帧共用时 allocation count 不增长。失败测试先产生1次 root Move listener side effect，再让后续深层
Button route 因 route path capacity 失败；staging 不发布、旧 published view 保持，但 attempted watermark
已推进，同一 frame retry 被拒且 callback 仍为1，证明 side effect 不回滚也不重放。该组件由独立
`tina_runtime_ui_tests` 直接执行 GoogleTest 验证，不使用 CTest；在 `EngineHost` 接线前，正式帧路径仍向
ActionMapper 传 canonical `None`。

M7-A Action Mapper 由 Runtime 唯一拥有，只实现 EngineConfig 注册的单一 immutable default Input
Context（priority=0）。UI consumption/claim 优先；未来多 Context 以显式 priority 决胜，同 priority
竞争同一 physical control 时拒绝激活，同一 Context 内禁止一个 control 重复绑定不同 action/domain。
每帧 transition 映射后，Mapper 会用最终 Platform snapshot 验证跨帧 retained source：仍为 `active`
或 suppressed 的 Key、Primary Pointer Button、Gamepad Button 必须仍为 held；Primary Window/Gamepad
generation 若在 retained state 未先 cancel/reset/release 时消失或替换，返回
`LifecycleInvariantViolation`，不能静默清零或迁移到新 generation。
默认/硬容量冻结为：raw transition 256/4096、UTF-8 text bytes 16 KiB/1 MiB、Platform event
64/1024、continuous claim 64/1024（M7-A 仅为 Runtime ActionMapper consumer 内部固定上限，不属于 game-facing
`InputActionMapConfig`）、
pending Simulation transition 128/4096、Frame Action transition 128/4096、digital binding 64/4096；
raw/event/simulation/frame batch 各有不计入有效项的一个 reset slot。所有容量 Create 时一次性分配，
运行期不扩容。完整 reset 后续规则见[平台与输入](../platform-input.md)。

主窗口 OS CloseRequested 是不可取消的 `PlatformPollResult::ExitRequested`：Platform Poll 返回后、下一个
Runtime frame 开始前正常停止并进入统一 shutdown。它既不进入当前可订阅的
`PlatformEventDispatcher`，也不进入未来通用 Runtime Event Queue，不再派生第二个同义 Window event。
游戏若需要确认退出，必须使用自己的 UI/State intent，再调用
Runtime 的显式退出请求，不能劫持 OS CloseRequested。

固定 Simulation 为60 Hz、每个 Render Frame 最多4步；每个 substep 独立 jobs/barrier、稳定
command merge/commit 与 Transform propagation，Render 使用 previous/current interpolation。

## 代价

- 需要有界 raw/action transition batch、control suppression、resync、tick latch 和回放格式；
- UI 命中使用上一份 committed geometry；State Transition Commit 位于 Frame Update 后，使动态
  root 能在同帧唯一 UI layout 中进入 snapshot、下一帧才交互；
- 每 substep barrier/commit 有固定成本，需要 benchmark 后再并行。
- GLFW standard gamepad 只能轮询最终 sampled state，因此只能为相邻 Poll 之间观察到的状态差
  生成 transition；无法承诺捕获两次 Poll 之间已经完成的物理 Down→Up。该限制必须进入能力说明
  和 replay 语义，不能为补齐它引入 SDL/SDL3；当前 GLFW窗口/键鼠 producer 不得改变已落地的
  Headless 契约，production Gamepad、完整 DPI与 IMM32仍属于完整 M7目标。

## 被拒绝方案

- 只保留 pressed/released 布尔快照：同帧事件顺序丢失；
- UI/玩法共用 EventBus 广播：消费和所有权不清；
- 所有 fixed substep 完成后只提交一次结构变化：追赶步语义错误。
