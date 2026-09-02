# ADR 0015：PlatformFrameView、Action domain 与逐 substep 提交

- 状态：Accepted
- 日期：2026-07-16
- 接受日期：2026-07-17
- 实施状态：M7 Headless/GLFW Platform/Input、fixed-step latch、Runtime-private UI route 和正式
  EngineHost 帧链路已落地；2D-INPUT-ADV/N3 已收敛为 Runtime 唯一 unified digital/analog mapper，覆盖
  value/deadzone/scale、SumClamped/StrongestMagnitude、多 Gamepad source、UI digital/axis suppression 与
  顶层 State transactional rebind。Windows IMM32 与可见 Runtime UI 已落地；真实设备/DPI 平台矩阵仍按
  独立测试门禁记录。本轮 N3 命令执行结果以最终验证记录为准。
  2026-09-02 扩展：`FrameActionSnapshot` 增加 `wheelDeltaX/Y` 与 per-pointer `FramePointerState` 表
  （`pointers` / `pointerState()`），滚轮按帧求和、per-pointer 认领生效；认领词汇未变
  （`PointerContinuousControl { Delta, Wheel }` 与 per-pointer identity 本来就存在，缺口在 `ActionMapper`
  的发布侧）。下面「M7-A 仅接受 PrimaryPointerId」一段已相应订正

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

Runtime 是 Action mapping 的唯一 owner；Platform 只发布 raw frame，不提供第二套 Action ID、binding、
snapshot 或 mapper。`InputActionMapConfig::bindings` 统一表达 Key、Pointer Button、Gamepad Button 与
Gamepad Axis。每个 binding 有稳定 `InputBindingId`、Action/domain、composition、deadzone 和 scale；
Digital source 用 scale 表达 active value，axis 先按 value mode 归一化，再做 gameplay deadzone 外重映射
与 scale。Action snapshot/transition 使用浮点 value，不再把 Gameplay 语义压成 held bool。

同一 Action/domain 的多 source 使用一致的 `SumClamped` 或 `StrongestMagnitude`：前者求和后 clamp 到
[-1, 1]，后者取绝对值最大的 source，等幅值以稳定 binding/source 顺序决胜。所有当前连接的标准
Gamepad generation 都可贡献；native slot 不进入 Game API，重连后的 generation 不继承旧 source 状态。

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
使用其他 id 均结构化失败。**这一条已被 ADR 0032 的 C1 部分取代**：Pointer snapshot 与 transition
现在按 `PointerCapacity` 做边界校验（`include/tina/platform/PlatformFrame.hpp:770,803`），非 primary
槽位是合法的；**pointer binding 仍然只接受 primary**（`src/runtime/EngineConfig.cpp:29`），所以第 2 根
手指按下不产生 Action，只能从 `FrameActionSnapshot::pointers` 读。同一帧所有 `GamepadSnapshot` 必须
来自同一个 registry owner，每个 slot index 唯一；同 slot 的不同 generation 不能同时作为最终快照。
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
`tina_runtime_ui_tests` 直接执行 GoogleTest 验证，不使用 CTest。

M7-C1c-b3c 的正式帧路径先同步分发 Platform lifecycle event，再由 Runtime-private owner 选择 Context、
调用 producer 路由上一份 committed hit snapshot，最后将 consumption/claims 交给 ActionMapper。
Headless frame 在首次绑定前选择 `nullptr`；首个有效 primary `WindowId` 延迟创建 Context，相同
owner/index/generation 复用。绑定后主窗口消失或 replacement generation 以
`LifecycleInvariantViolation` 终止本次 run；最小化、metrics/content scale 变化不重绑。Context 在
Render → Task → Platform → Clock modules 前 shutdown。owner 不调用 `commitLayout()`，因此 hit-test/route
不能隐式布局。该 b3c 切片当时没有 Game SDK Context/root 访问，正式路径的 committed hit snapshot 仍为空；
b3d2 后续加入 startup bind 与 root-scoped phase capability；到该切片为止 claims 仍为 canonical `None`，
也没有可见 Widget、Focus/Capture/Modal 或 DisplayList。b3e 随后加入 held primary Pointer Button claim：
UI request 只在最终 snapshot 仍 held 时发布，ActionMapper 在 transition mapping 前应用 claim，取消 active
Gameplay source或拦截同帧 Down，并抑制到真实 Up。N3 随后把相同规则扩展到 Key/Gamepad Button，
并让 Gamepad Axis claim 抑制到 neutral/deadzone；这些 claim 仍由 UI-owned route-result ABI 表达。

Action Mapper 由 Runtime 唯一拥有，当前仍只有 EngineConfig 注册的单一 default Input Context
（priority=0），但 binding 可通过受控事务更新。UI consumption/claim 优先；未来多 Context 以显式
priority 决胜，同 priority 竞争同一 physical control 时拒绝激活，同一 Context 内禁止一个 control
重复绑定不同 action/domain。

运行时 rebind 只通过栈顶 State 的 `FrameUpdateContext::inputActionRebinding()` 暴露 phase-local facade，
下层 State 无修改全局 map 的权限。`begin()` 进入 Capturing；`commit()` 用 Reject 或 Swap 处理冲突，只
排入 Queued，并在下一次 mapping frame 开始时原子应用。Reject 返回冲突 binding 且保留 capture；
`cancel()` 可取消 Capturing 或 Queued transaction。若 capture 绑定了 `GamepadId` generation，设备
disconnect，或 raw reset 后无法继续证明该 generation 存活，则 transaction 以 DeviceDisconnected
取消，不能迁移到重连后的新 generation。

每帧 transition 映射后，Mapper 会用最终 Platform snapshot 验证跨帧 retained source：仍为 `active`
或 suppressed 的 Key、Primary Pointer Button、Gamepad Button/Axis 必须仍与 final value 一致；Primary
Window/Gamepad generation 若在 retained state 未先 cancel/reset/release/neutral 时消失或替换，返回
`LifecycleInvariantViolation`，不能静默清零或迁移到新 generation。
默认/硬容量冻结为：raw transition 256/4096、UTF-8 text bytes 16 KiB/1 MiB、Platform event
64/1024、continuous claim 64/1024（M7-A 仅为 Runtime ActionMapper consumer 内部固定上限，不属于 game-facing
`InputActionMapConfig`）、
pending Simulation transition 128/4096、Frame Action transition 128/4096、unified Action binding 64/4096；
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
- GLFW standard Gamepad producer 已落地，但只能轮询最终 sampled state，因此只能为相邻 Poll 之间观察到的状态差
  生成 transition；无法承诺捕获两次 Poll 之间已经完成的物理 Down→Up。该限制必须进入能力说明
  和 replay 语义，不能为补齐它引入 SDL/SDL3；当前 GLFW producer 不得改变已落地的 Headless
  契约，真实设备与完整 DPI 矩阵仍需独立平台门禁。

## 被拒绝方案

- 只保留 pressed/released 布尔快照：同帧事件顺序丢失；
- UI/玩法共用 EventBus 广播：消费和所有权不清；
- 所有 fixed substep 完成后只提交一次结构变化：追赶步语义错误。
