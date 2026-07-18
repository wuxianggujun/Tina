# Platform、Window 与 Input 契约

> 状态：vNext Input 契约已由 ADR 0015 接受；M7-A Headless 的 `PlatformFrameView`、Action Mapper、
> fixed-step latch 与 `PlatformEventDispatcher` 已实现，首个私有 GLFW desktop adapter 也已完成
> Window/Keyboard/Pointer/Focus/resize/close/committed text。M7-B1 已完成私有 WindowSurface handoff；
> M7-B2 已完成私有真实 bgfx clear/present 和 Desktop smoke，production Gamepad、Windows IMM32 composition、OS Pointer
> Capture 与完整 DPI/UI 门禁仍是后续能力。M7-C1c-b3a 已补齐 Pointer Button/Wheel 的事件时
> logical position；M7-C1c-b3b 已实现 Runtime-private `UIInputRouteProducer`，M7-C1c-b3c 已让
> `EngineHost` 私有延迟绑定主窗口 `UIContext`，并按 Platform lifecycle dispatch → UI route →
> ActionMapper 接入正式帧路径。M7-C1c-b3d2 已加入 startup primary-window metrics seed 与 Game SDK
> root-scoped UI facade；M7-C1c-b3e 已让 Pointer listener 请求 button ownership，producer 只发布最终
> snapshot 仍 held 的 primary Pointer Button claim。Key/Gamepad/axis claim producer、可见 Widget、
> Focus/Capture/Modal 与 bgfx UI pass 仍未实现；D0 已把 primary-window UIDisplayList 作为 Runtime-private
> submit-call-local `RenderFrame` borrow 接入，但不代表可见 Widget。

## 结论

Tina 只使用 GLFW 处理 Windows/Linux 窗口、键鼠和标准 Gamepad；不引入 SDL/SDL3。
Windows 文本组合由 `tina_platform_glfw` 内部的 IMM32 adapter 补充。玩法、UI、Scene 和
Render 公共接口不能包含 `GLFWwindow*`、Win32 `HWND` 或平台键值。

为了让 Null Runtime 真正不链接 GLFW，vNext 已将平台契约和具体实现分开：

| Target | 职责 | 依赖 |
| --- | --- | --- |
| `tina_platform` | `WindowId`、Window/Input/Event 描述、线程命名、不透明 Render surface、Headless test backend | `tina_core`、OS 最小适配 |
| `tina_platform_glfw` | 最终职责为 GLFW 生命周期、真实窗口、键鼠、Gamepad、DPI、Windows IMM32；当前只完成 Window/Keyboard/Pointer/committed text 子集 | `tina_core`、`tina_platform`、GLFW、Windows 时使用 IMM32 |

`tina_platform` 不提供第二套通用窗口框架；拆 target 只为隔离第三方类型和支持无窗口测试。
UTF-8 路径、读取和原子文件写入统一属于 `tina_core/io` 的 OS-specific `.cpp`，Platform 不再
实现第二套 filesystem adapter。

## 所有权与线程

- `EngineHost::Create`、`run` 与销毁必须位于同一 owner thread；桌面 bootstrap 最终把该线程定义为
  Engine 主线程。跨线程 `run` 返回结构化 `WrongOwnerThread` 且不消耗 run-once，跨线程销毁在
  调用 native API 前 `terminate`；
- GLFW 初始化、事件轮询、窗口创建/销毁和窗口操作只在该 owner thread 执行；
- 每个窗口使用 generation `WindowId`，stale ID 不能解析到复用后的窗口；
- 首个产品切片只支持一个主窗口，但所有 Window/Input/DPI/Focus 状态都保存在 backend-owned
  Window record，不使用进程级全局保存这些事实。唯一例外是一个 process-wide atomic GLFW backend
  lease，用来拒绝同进程并存的第二个 Tina GLFW owner；shutdown 后显式释放该 lease；
- 主窗口 OS CloseRequested 只设置不可取消的 Platform control latch；Platform Poll 返回该 outcome
  后，Runtime 在下一个 frame 开始前进入统一 shutdown。它既不进入当前生命周期
  `PlatformEventDispatcher`，也不进入未来通用 Runtime Event Queue；平台 callback 本身不销毁
  Scene、UI、Surface 或 RenderDevice；
- 后台线程不能调用 GLFW，也不能保存原生窗口指针。

Game SDK 的 Platform API 只暴露 `WindowId` 与 logical/framebuffer/content-scale metrics。
Platform 与 Render backend 通过不安装到 Game SDK 的 integration SPI 交换 generation
`WindowSurfaceId`、从 committed Window Metrics 原子派生且无原生句柄的
`WindowSurfaceSnapshot(extent, scale, sourceMetricsRevision, surfaceRevision, suspended)`，以及创建/重置期的
`NativeWindowSurfaceLease`。Lease payload 是 Tina-owned POD，只允许 `tina_render_bgfx` 私有
映射；游戏、Scene、UI 和公共 Render API 看不到 `HWND`、X11、Wayland、GLFW 或
`bgfx::PlatformData`。窗口销毁前 Runtime 必须停止对应 surface 提交并关闭 RenderDevice；具体
bgfx backend 在 drain/shutdown 中释放自己持有的 lease，随后 Platform 才能销毁窗口。

## 每帧 PlatformFrameView

Platform 每轮主循环只轮询一次。`pollFrame()` 返回结构化错误或一个无歧义 tagged
`PlatformPollResult`：`ContinueFrame{PlatformFrameView}` 与 `ExitRequested` 二选一。只有 Continue
分支才生成不可变 `PlatformFrameView`；它包含每窗口最终
`WindowMetricsSnapshot`/`WindowInputSnapshot`、Engine 级设备 Snapshot、`PlatformEventBatch` 和按
Platform sequence 排序的 `InputTransitionBatch`。只保留最终布尔快照会丢失同一轮 Poll
内的 press→release、多个 wheel/text/composition 事件及其顺序。`PlatformFrameView` 与其中的
string/span 都由 Platform backend owning；API 借用寿命在 Engine 结束当前 Poll/Input phase 时终止，
即使 backend storage 到下一次 Poll 才复用也不得继续访问。跨 phase/帧需要的数据必须复制到对应
的固定容量 owning latch。

```text
Platform backend poll（Headless 与当前 GLFW adapter 共用同一契约）
  -> normalize platform events
  -> primary CloseRequested?
       yes: return PlatformPollResult::ExitRequested
            no PlatformFrameView / no engine frame index
            Runtime stops before PlatformEventDispatcher/Input or any new frame phase
       no:  finalize PlatformPollResult::ContinueFrame
            PlatformFrameView(metrics/input/device snapshots
            + PlatformEventBatch + ordered InputTransitionBatch)
            -> Runtime PlatformEventDispatcher（仅生命周期通知）
            -> UI routed transitions (上一帧稳定布局)
            -> Tina::UI::InputTransitionConsumptionView + Tina::UI::ContinuousControlClaimsView
            -> game action mapping / fixed input latch
```

## 当前 GLFW desktop adapter

当前实现按以下创建事务工作：

```text
validate PrimaryWindowConfig and capacities
  -> acquire the one-process GLFW lease
  -> glfwInit
  -> normalize WindowCreatePlan
  -> create hidden GLFW_NO_API primary window
  -> publish generation WindowId registry slot
  -> register noexcept callbacks and read initial snapshot
  -> expose initialPrimaryWindowMetrics() seed without polling or consuming frame id
  -> optionally show the fully initialized window
```

任一步失败都以 scope rollback 逆序销毁窗口、终止 GLFW 并释放 process lease。title 必须是无内嵌
NUL 的严格 UTF-8，logical extent 必须非零且可表示为 GLFW `int`；当前只接受 Windowed 与
BorderlessFullscreen，不承诺 exclusive fullscreen。公共 factory header 只返回 Tina
`IPlatformBackend`，不暴露 `GLFWwindow*` 或 native handle。

GLFW C callback 不调用 UI、Gameplay 或销毁系统，只更新 backend-owned final state、向当前有界
`PlatformFrameBuilder` 追加归一化 transition，或记录 first-sticky failure。Poll 的任一操作、callback
或 finalize 失败都会 `discardFrame()`；后续 Poll 重新从 final physical snapshot 发出两条 stream reset，
不发布半帧。adapter 不安装/接管 GLFW 的进程级 error callback；每个 owner-thread 操作通过
`glfwGetError()` 获取并复制 native error code/context。

当前 producer 覆盖 Keyboard press/release/repeat、Primary Pointer move/button/wheel、Focus、logical/
framebuffer/content-scale/visible/iconified metrics、resize、close 与 GLFW char callback 提供的 committed
Unicode scalar。Gamepad registry/sampled diff、Windows preedit/composition、OS Pointer Capture、
可见 Widget/DisplayList 与 Key/Gamepad/axis 持续控制 claims 尚未接入。Runtime-private UI routed consumption producer 已在
M7-C1c-b3c 接入 `EngineHost`，startup UI metrics seed 与 Game-facing retained root capability 在
M7-C1c-b3d2 被 Runtime 消费，但二者都不属于 GLFW
公共 API。WindowSurface snapshot/lease 已在 M7-B1 接入到
Platform/Render 组合；M7-B2 已完成真实 bgfx clear/present 和基础 GPU surface lifecycle，仍不覆盖
后续 UI pass、Pass Scheduler 或 resize/最小化/恢复自动化。

`WindowInputSnapshot` 按窗口保存：

- 键盘和 Pointer 的最终 `held` 状态；
- Pointer 最终 position、累计 delta 和当前 button set；
- 引用同一帧 `WindowMetricsSnapshot::revision`，但不复制 focus、extent、scale 等窗口事实。

M7-A 只接受 `PrimaryPointerId`（值为0）：最终 `PointerSnapshot`、Pointer Button/Move/Wheel transition
以及 `PrimaryPointerButtonBinding` 都必须使用该 id；其他 pointer id 在配置或 frame finalize 时结构化失败。
多触点/多 Pointer 是后续能力，不能因为公共类型保留了 `PointerId` 就宣称当前已支持。

Gamepad registry 与最终 sampled state 属于 Engine，不复制进每个 Window snapshot。首个单窗口
切片把所有 Gamepad 输入显式路由到 primary Window；未来多窗口必须通过单独的 active input target
决定路由，不能让同一设备在多个窗口重复产生 Action。同一个 `PlatformFrameView` 的所有
`GamepadSnapshot` 必须来自同一个 generation registry owner，且每个 slot index 最多出现一次；同 slot
的不同 generation 也不能同时作为帧末快照发布。

`InputTransitionBatch` 保存 Key/Button Down/Up、Pointer Move、Wheel、Gamepad observed button/axis
change、已提交 UTF-8 text、composition、`InputCancelTransition` 和 `InputStreamReset`；每项都有适用的
window/device/pointer id 与单调 sequence。
Pointer Button 与 Wheel 还携带该条 transition 发生时的 window-logical position；该坐标由 backend
按 callback 顺序固化，独立于 Poll 结束时的 `PointerSnapshot`。Runtime/UI 只能使用 transition 自带
坐标做 hit-test，不能用帧末位置倒推，否则同帧 Button/Wheel 之后继续 Move 会命中错误节点。
连续 Move 只有在中间不存在 Button/Wheel/Capture/Focus 边界时才可合并，文本、composition、
Down/Up 与设备连接绝不合并。批次使用预分配有界存储：先合并可合并 Move；仍满时记录
`InputOverflow` metric，并使用预留 control slot 写入不可丢的 `InputStreamReset`。Reset 使本帧
尚未完成的 UI route、Capture、composition、repeat 和 Action edge 全部按
`InputCancelTransition` 语义取消；本帧 Action Mapper 立即从最终设备状态显式 resync，后续帧继续
验证该状态。M7-A 对 reset 后仍处于 held 的 digital control 保持 gameplay suppressed，直到真实
release，不能静默丢一半按键让 `held` 永久卡住；axis/non-neutral suppression 要等后续 analog
binding 与 continuous mapping 落地。

Text/Composition 的 `string_view` 不借用 GLFW/IMM32 callback buffer。`PlatformFrameBuilder::Create`
一次性分配 UTF-8 byte arena，append 时验证严格 UTF-8、拒绝内嵌 NUL，并整段复制后再发布借用 view；
不得按 byte 截断。byte arena 满时整项不复制，使用 raw batch 的保留 slot 写入
`InputStreamReset(TextByteCapacityExceeded)` 并记录独立诊断；无效 UTF-8 则让当前 frame 以结构化
`InvalidInputPayload` 失败。

builder 还会用固定数组单次扫描 raw batch：Key、Pointer Button 与16个 Gamepad slot 的最后一个
未被后续 `InputCancelTransition`/`InputStreamReset` 覆盖的 digital edge，必须与 final held
Snapshot 一致。该校验零分配且为 O(raw items + control count)；`PlatformEventStreamReset` 属于另一条
生命周期流，不能替 raw reset 豁免不一致。这样 Action Mapper 不需要在帧末猜测或静默修补丢失边沿。

Action Mapper 还在每帧映射完成后验证跨帧 retained source：仍为 `active` 或仍处于
`suppressedUntilRelease` 的 Key、Primary Pointer Button、Gamepad Button 必须在本帧最终快照中仍为 held。
若 primary Window 消失/换 generation、Gamepad slot 换 generation，必须先由同一输入流中的
cancel/reset/release 消除 retained 状态；否则返回 `LifecycleInvariantViolation`，不能静默清零或把旧
generation 绑定到新设备。

## UI consumption 与持续控制所有权

UI 使用同一 `PlatformFrameView` 的有序 transition 生成 Pointer/Key/Navigation routed event。每个 Pointer
transition 最多 hit-test 一次；有 Capture 时直接解析 captured UINodeId，需要判定 click 的真实
release 再做一次且仅一次 release-position hit-test。

UI route 输出两份彼此分离的数据，当前公开 ABI 由 `tina_ui` 拥有，Runtime 只作为 ActionMapper
consumer 读取这些 view：

- `Tina::UI::InputTransitionConsumptionView` 与当前 `PlatformFrameView` identity 绑定，按 transition
  ordinal 使用固定 bit storage 标记一次性 transition；重复 consume 幂等，错误 frame/sequence 返回明确错误；
- `Tina::UI::ContinuousControlClaimsView` 使用归一化 window/device/control identity，声明本帧由 UI 拥有的
  held/axis/pointer-delta。它不修改 Platform Snapshot，也不是跨帧容器。

M7-C1c-b3b 的 Runtime-private `UIInputRouteProducer` 已实现当前最窄转换：只把 raw Pointer
Move/Button/Wheel 按序映射为 `UIPointerInputEvent`，Button/Wheel 必须使用 transition 自带的事件时
logical position。listener 调用 `consumeInputTransition()` 时，producer 在同一 raw ordinal 的 bit 上标记
consumed；reset、cancel、Key/Text/Gamepad 等非 Pointer 项不路由，也不伪造 Button Up，而是在 raw ordinal
空间保留 hole；该 b3b 切片的 `ContinuousControlClaimsView` 当时始终为 canonical `None`。

M7-C1c-b3e 为 Pointer route 增加第一条真实 claim producer。listener 可调用
`claimPointerButton(button)`，请求与当前 routed input 相同 window/pointer 的 button；固定 bitset 合并
多 phase/重复请求，非法 enum 返回 false。Runtime 将请求与本帧最终 `PointerSnapshot::heldButtons`
求交，只发布仍 held 的 primary Pointer Button，并在双 PMR claim buffer 中去重。claim 与 consume
彼此独立：同帧 Down 只 claim 也会在 ActionMapper 中被拦截，已经 active 的 Gameplay source 则产生
Cancel 并保持 suppression 到真实 Up。

本文“primary Pointer Button”精确指主窗口 `PrimaryPointerId` 上的任意合法 `PointerButton`，不是只指
`PointerButton::Primary`；多 Pointer identity 仍属于后续契约。

consumption 与 claims 分别使用 Create 期双预分配 PMR storage；成功发布只交换对应 staging/published storage，supplied
`memory_resource` 必须比 producer 活得更久，300帧共用时 allocation count 不增长。失败测试先让 root
Move listener 产生1次 side effect，再让后续深层 Button route 因 route path capacity 失败；本次 staging
不发布、上一份成功 view 保持，但 attempted watermark 已推进，同一 frame retry 被拒且 callback 仍为1，
证明 listener side effect 不回滚也不重放。独立 `tina_runtime_ui_tests` 直接运行 GoogleTest，不使用 CTest。
M7-C1c-b3c 已把 producer 接入正式帧：同步 `PlatformEventDispatcher` 完成后，Runtime-private owner
先为本帧选择 Context，再路由上一份 committed hit snapshot，最后把 producer 输出交给 ActionMapper。
M7-C1c-b3d2 把 primary-window UI owner 绑定提前到 startup transaction：Platform backend 提供
`initialPrimaryWindowMetrics()`，该 seed 不 poll、不推进 frame id，也不消费首帧 metrics event；Runtime
在 `onEnter` 前据此显式创建或显式 Headless。后续 frame 选择只验证 startup identity/revision 单调性；
绑定后窗口消失、换代或 revision 后退以 `LifecycleInvariantViolation` 终止本次 run。最小化、metrics 或
content scale 变化不重绑，Context 在 Render → Task → Platform → Clock modules 之前销毁。
owner selection 不调用 `commitLayout()`，hit-test/route 不会隐式触发布局。Game SDK 已能在受限 phase 内创建/
更新 retained root，但当前产品帧仍没有 Widget 默认行为、Focus/Capture/Modal 或 paint authoring。D0 已在
paint/layout commit 后构建并借用提交 primary-window DisplayList；在缺少 Game SDK paint setter 和 bgfx UI
pass 时，当前产品列表仍为空且不可见。

M7-A Action Mapper 的 claim consumer 能识别 Key、Primary Pointer Button 与 Gamepad Button，但当前
UI producer 只生成 primary Pointer Button claim。`GamepadAxisControlIdentity` 和 Pointer continuous
identity 已冻结为 UI/Runtime seam schema；Key/Gamepad/axis producer 以及 analog/pointer action mapping
落地前，不会产生对应 ownership 或 axis/continuous Gameplay 状态，也不能据此宣称已经实现
neutral/dead-zone suppression。

Gameplay Action Mapper 只映射未消费 transition 和未 claim control，并跨帧维护
`suppressedUntilReleaseOrNeutral`：

- UI 消费 digital Down 后，即使下一帧 consumption 已销毁，Gameplay 仍看不到该 control 的 held；
- UI 在 control 已经 held 时才通过 `Tina::UI::ContinuousControlClaimsView` 接管它，也必须立即取消该 control
  尚未完成的 Gameplay edge/repeat，并保持 suppression 到真实 Up；该清理使用 Action Cancel 语义，
  不能伪造普通 Released；
- 匹配的真实 Up 仅解除 suppression，不生成 Gameplay Released；
- 完整目标中，axis/连续 control 被 claim 后保持 suppressed，直到进入 binding 配置的 dead zone/neutral；
  该项不属于 M7-A 已实现 digital mapper；
- `InputCancelTransition`、断连和 `InputStreamReset` 取消既有 Gameplay edge/repeat；M7-A 把 resync 后
  仍 held 的 digital control 保持 suppressed 到真实 release，后续 analog mapper 再扩展到 neutral；
- `preventDefault()` 只写 consumption/claim，绝不能回写 Platform held 状态或全局键盘对象。

## Simulation 与 Frame Action

Fixed Simulation 可能在一个 Render Frame 内运行0到4步，因此输入边沿不能简单复制到每个
fixed step：

- 每个 Action 在映射时声明 `Simulation` 或 `Frame` domain；同一 active Input Context 中同一
  transition 默认不能同时产生两个 domain 的 action，确有需求必须用两个显式物理 binding；
- Simulation `held/axis` 状态可供本帧所有 fixed step 读取；未被 UI 消费/claim 的 transition
  映射为有序 `SimulationActionTransitionBatch`，绑定当前 next uncompleted simulation tick；
- 如果本帧没有 fixed step，batch、目标 tick 与最终 action state 跨 Render Frame 保留。后续
  Down→Up 继续按 source sequence 追加到同一 tick，不能压缩为一个 pressed/released bool；
- 第一个实际执行目标 tick 的 fixed step 消费一次完整 batch；同帧后续最多3步只读取 held/axis
  snapshot，不重复 edge；callback 返回后 batch 才转为已消费并清空；
- Frame Action edge 只在当前 Frame Update 消费一次，帧末丢弃，不在0个 fixed step 时保留；
- Pointer delta、wheel、文本和 UI navigation 每个 Render Frame 最多消费一次，不重复给4个
  fixed step；
- `SimulationActionTransitionBatch` 使用配置固定容量并预留 Reset slot。满容量时记录 metric，
  清除 reset 之前尚未消费的 transient edge，插入 `SimulationInputStreamReset`，保留最终 action
  state，并允许 reset 后 transition 继续按容量追加；不能静默保留任意一半 edge；
- replay 只记录归一化 Simulation Action 的目标 tick、最终 state、有序 transition 与 reset marker，
  不记录 GLFW key code、UI UINodeId 或 consumption bit；Frame Action 不进入确定性 replay。

## Action Context、注册与容量

Action Mapper 由 Runtime 唯一拥有。M7-A 只实现一个 Engine-owned、priority=0 的 immutable default
Input Context；bootstrap 在 `EngineConfig::inputActions.digitalBindings` 注册 `DigitalActionBinding`，样例的
Escape → Frame Exit Action 也必须走该入口，禁止读取 GLFW key 或在 Runtime 中硬编码。后续
GameState-owned Input Context 使用显式整数 priority：UI consumption/claim 永远先于所有 Context；
较高 priority 胜出；同一 Context 内一个 physical control 只能绑定一个 action/domain；两个 active
Context 在相同 priority 竞争同一 control 时激活失败，不能依赖注册顺序。多个 control 可以绑定同一
Action，只有最后一个未抑制 source 释放时才产生 normal Released。

M7-A 容量在 `EngineConfig::platformFrameCapacities`、`inputActions.capacities` 与
`platformEventSubscriptions` 中按职责配置，Create 时一次性分配，运行期不扩容：

| 容量 | 默认有效项 | 额外预留 | 硬上限 |
| --- | ---: | ---: | ---: |
| raw `InputTransitionBatch` | 256 | 1 个 `InputStreamReset` slot | 4096 |
| UTF-8 text/composition byte arena | 16 KiB | 共用 raw reset slot | 1 MiB |
| `PlatformEventBatch` | 64 | 1 个 `PlatformEventStreamReset` slot | 1024 |
| `Tina::UI::ContinuousControlClaimsView`（M7-A Runtime consumer 内部上限） | 64 | 0 | 1024 |
| pending `SimulationActionTransitionBatch` | 128 | 1 个 `SimulationInputStreamReset` slot | 4096 |
| Frame Action transition | 128 | 1 个 reset slot | 4096 |
| digital binding | 64 | 0 | 4096 |

`Tina::UI::InputTransitionConsumptionView` 是按本帧 raw transition ordinal 建立的固定 bit storage，不另设独立
capacity。M7-C1c-b3b producer 按 configured raw capacity 加一个 reserved reset slot 预分配两份 bitset，
因此有效 raw 项与末尾 reset 都能保持原 ordinal。raw 输入一旦 overflow，builder 写入 reset 后，本次 Poll 后续 callback 只更新最终设备
snapshot，不再追加 transition；Mapper 根据 reset + 最终 snapshot resync，并把仍 held/non-neutral
control 保持 suppressed。`PlatformEventBatch` 先合并同窗口最终 resize/scale/focus；仍满时写入 reset，
本 Poll 后续只更新最终 registry/snapshot。Simulation latch overflow 则清除 reset 前未消费 transient
edge、插入 reset、保留最终 state，并允许 reset 后的新 normalized Action 在腾出的容量内继续追加。
UI claims 满容量属于配置/运行错误：本次 `UIRouteResult` 原子失败，Runtime 在 Gameplay Mapping 前
停止该帧并返回结构化错误，绝不能提交一半 claims 后让输入穿透。

World pointer action 不能等到未来 fixed tick 再用“最新 Camera”换算。Action Mapping 对未被 UI
消费的 transition，使用与该 `PlatformFrameView` 对应的 last-presented Camera/Surface snapshot 转换
一次，并把 world point、camera revision、surface revision 和 input sequence 一起写入
Simulation Action。0 fixed-step 帧只延迟消费这份结果，不重新计算；viewport 外输入显式 no-hit。

## 当前三条通道与未来通用事件队列

以下通道保持分离：

1. `PlatformFrameView`：最终设备快照 + 有序 transition，适合轮询、UI route 和 Action Mapping；
2. Runtime-private `PlatformEventDispatcher`：由 `PlatformEventBatch` 同步泵送
   `WindowMetricsChangedEvent`、Gamepad connect/disconnect 与 stream reset 等平台生命周期通知；Game SDK
   只取得 `PlatformEventSubscriptions` 门面和 RAII Token，不取得 dispatcher owner；
3. UI routed event：一次 hit-test 后按 Capture → Target → Bubble 投递。

同一平台事件可以派生出快照状态和一个 Window event，但只能在各自固定阶段泵送一次。
订阅取消、窗口销毁和 generation 失效必须立即阻止后续回调访问旧对象。
未来通用 Runtime Event Queue 面向 Gameplay/Domain/异步模块事件，当前 M7-A 尚未实现；它与同步的
`PlatformEventDispatcher` 不是同一 owner、存储或投递语义。主窗口 OS CloseRequested 是例外的不可取消
Platform control outcome：既不进入 `PlatformEventDispatcher`，也不进入未来通用 Runtime Event Queue，
避免与 Runtime stop latch 重复投递。

## Focus、Capture、DPI 与 IME

- 窗口失焦时生成 `InputCancelTransition(reason=FocusLost)`，取消所有 held key/button 的交互语义并清除
  Pointer Capture、keyboard pressed、Gamepad repeat 和未提交 composition；不得伪造普通 Up，
  以免触发 release-position click 或 Gameplay Released；
- 当前 GLFW adapter 在发布一次 FocusLost cancel 后立即清空 final held，并精确吞掉 GLFW 随后可能
  产生的 synthetic key/button release；窗口重新获得焦点后真实 Down 正常恢复，同 control 的新 Down
  会先解除 stale-release mask，避免误吞随后真实 Up；
- 鼠标按下建立的 OS capture 与 UI `UINodeId` capture 分开记录，两者在释放、失焦、窗口关闭
  或目标失效时成对清理；
- GLFW logical size、framebuffer size 与 content scale 是三个不同量；UI layout、Pointer event
  和 hit-test 始终使用 logical coordinate，只有 DisplayList extraction 转 framebuffer pixel；
- Windows IMM32 context 由窗口 adapter 拥有，subclass/association 必须在窗口销毁前恢复；
- `TextInputEvent` 只包含已提交 UTF-8；`TextCompositionEvent` 保存 preedit、codepoint cursor
  和 Started/Updated/Ended/Cancelled；
- Linux 首期保证 committed text，原生 preedit 后置，但接口必须安全返回“不支持”而不是
  伪造已提交文本。

## Gamepad

首期只接受 GLFW standard mapping。`GamepadId` registry、generation 与 sampled state 属于
Engine 级输入域，不复制进每窗口 snapshot；首期由 Input Router 把手柄输入显式路由到
primary window，后续多窗口再扩展 active input target。连接/断开作为可观察生命周期事件进入
`PlatformEventBatch`，并由当前 `PlatformEventDispatcher` 同步通知订阅者；它们不进入尚未实现的
通用 Runtime Event Queue。断开时必须先提交
`InputCancelTransition(reason=DeviceDisconnected, device=GamepadId)`，再
回收该 generation，禁止伪造普通 Button Up。

首期固定16个 Gamepad registry/source slots，而不是仅限制“某一帧 snapshot 数量”。所有有效
`GamepadId` 的 slot index 必须落在该范围；generation 负责检测断开后的 stale id。一个 Poll 内的
disconnect cancel 可以合法引用 final snapshot 已不存在的旧 generation，因此 builder 不要求该
generation 仍在帧末 connected snapshot；但必须由同一 Poll 中顺序正确的 cancel → disconnect，
或覆盖对应 raw/lifecycle stream 的 reset 证明。仍连接的 generation 不能收到 DeviceDisconnected
cancel，cancel 之后也不能再接收同 generation 输入。

帧末 `GamepadSnapshot` 还有两条独立门禁：所有 snapshot 的 `GamepadId::owner()` 必须相同，slot
index 必须唯一。connect 事件若未被同 Poll 的后续 disconnect 覆盖，必须能在最终 snapshot 找到相同
generation；disconnect 事件则必须在最终 snapshot 中消失，并由更早的 device cancel/raw reset 证明。

GLFW gamepad API 是 sampled polling，而不是可枚举全部边沿的事件流。因此 adapter 只能比较
相邻两次 Poll 的标准化状态，并为**观察到的状态差异**生成 transition；若实体按钮在两次 Poll
之间完整 Down→Up，平台无法承诺恢复该边沿。这里不引入 SDL/SDL3 补偿该限制，也不向公共
接口暴露 GLFW 类型。UI 的 D-pad/左摇杆、Accept/Cancel 是设备无关语义，摇杆回滞和长按
重复由 UI navigation 层处理，Gameplay Action Map 保留独立配置。

实体手柄冒烟不能替代自动化。Platform adapter 必须允许测试注入标准化 sampled state，以
验证相邻 Poll diff、连接、断开、回滞、重复和 Focus 丢失；测试不得把 GLFW 未观察到的
两次 Poll 间完整 Down→Up 当作 adapter 的可靠保证。

## 错误与降级

- 主窗口或必需 surface 创建失败：`EngineHost::Create` 返回带平台 error code 的 `Result` 并
  逆序回滚；
- Gamepad、clipboard、cursor shape 等可选能力失败：记录结构化 warning，保持窗口可运行；
- 无显示环境的测试显式选择 Headless backend，不能捕获 GLFW 初始化失败后偷偷降级；
- GLFW/IMM32 C callback 必须为 `noexcept`，只写入预分配队列或 sticky structured failure；
  异常不得穿过 C callback，failure 由下一次 `pollFrame` 以 `Result` 返回，callback 不直接调用
  UI、Gameplay 或销毁系统；
- `CloseRequested` 是正常且不可取消的 Platform control outcome，不得混入 error path；
- adapter 不接管 GLFW 全局 error callback；操作级错误从 owner thread 的 `glfwGetError()` 复制到
  Tina Error，避免与同进程其他 GLFW consumer 争夺 callback ownership；
- 日志只记录 UTF-8 操作名、错误码和必要上下文，不写用户输入正文或敏感 clipboard。

## 验收

当前 M7-A Headless/GLFW Window 与 M7-B1 WindowSurface handoff 子切片已验证：

- Headless backend 不链接、不加载 GLFW；最新 Windows 门禁覆盖 Null Runtime 300帧并完成一次性逆序关闭，
  10,000帧仍作为 M6-A/M7-A 阶段的历史长跑证据保留；
- WindowId/GamepadId generation identity、跨帧 lifecycle mismatch、Focus/设备断开
  `InputCancelTransition` 有直接测试；生产 Window registry 已由 GLFW adapter 持有，production
  Gamepad registry 仍后置；该
  cleanup transition 不触发
  click、Released action 或普通 Up 订阅；
- `CloseRequested` 只产生一次不可取消的 Platform control outcome；本次 Poll 返回后、任何
  PlatformEventDispatcher/Input/UI/Fixed phase 开始前停止，dispatcher 中没有重复关闭事件；
- fixed step 为 0/1/4 次时，`SimulationActionTransitionBatch` 绑定 next uncompleted tick；跨 0-step
  frame 的 transition 保持顺序，首个实际 substep 只消费一次 edge，后续追赶步只读 held/axis；
- 注入的 `Tina::UI::InputTransitionConsumptionView` / `Tina::UI::ContinuousControlClaimsView`
  使 Gameplay digital control 保持 suppressed，直至真实 Up；
  axis/pointer continuous mapping 尚未实现；
- keyboard/pointer/text/composition 在同一 Poll 内的 Down→Up 与多事件保持 sequence，Move 合并
  不跨语义边界；Pointer Button/Wheel 保存事件时 logical position，后续帧末 Move 不会覆盖该位置；
  Gamepad 只保证相邻 Poll sampled diff，不声称恢复 Poll 间完整 Down→Up；
- Raw `InputTransitionBatch` 与 Simulation action latch 满容量时都通过保留的 reset/control slot
  发出显式 reset，取消 transient edge/repeat/capture，保留最终 physical state，并让仍 held/non-neutral
  的 control 进入 suppression；replay 记录 reset、最终 normalized state 与 tick 绑定，重放结果确定
  且不会永久 stuck；
- M7-A 只接受 `PrimaryPointerId`；Gamepad snapshot 同 owner、slot 唯一，Engine 级 Gamepad 输入每帧
  只路由一次到 primary window；跨帧 retained active/suppressed source 与最终 held snapshot 一致；
- Platform 生命周期通知与 Headless/Input Action 映射互不重复投递；M7-C1a 已实现 UI-owned
  route-result view ABI，M7-C1c-b3b 已实现 Runtime-private producer，M7-C1c-b3c 已实现主窗口
  Context 选择、同 generation 复用、换代/消失门禁和正式 EngineHost 顺序接线；M7-C1c-b3d2 已实现
  startup metrics seed、root-scoped Game SDK UI facade 与 phase expiry/sticky/abort；公共头文件不含
  GLFW/SDL 类型，Headless backend 不链接、不加载 GLFW 或 SDL；
- M7-B1 覆盖 `WindowSurfaceId` generation、`WindowSurfaceSnapshot` identity/revision/suspended、
  move-only `NativeWindowSurfaceLease`、重复 lease 拒绝、Render 创建失败与窗口发布失败逆序回滚、
  surface snapshot 只由 committed metrics 派生、resize/content-scale/suspend 改变才递增
  `surfaceRevision`，以及 NullRender suspended 帧维护调用但不 present、不增加 submission index；
- Windows 最新 D0 Debug/Release 均通过基础 `tina_tests` 207/207、独立 UI 92/92、独立
  Runtime→UI 51/51、UI→Render bridge12/12、bgfx专项11/11，以及Null/Desktop样例300帧；
  GLFW专项25/25与Platform样例300帧保留前序门禁；Release clean，本轮 Debug D3D11
  `RefCount is 3 (expected 0)` 为已记录第三方 debug layer 提示；
- Linux C1c-b3e GCC 13.4 与 Clang 22.1.8 sanitizer Null 图均通过基础194/194、独立 UI 81/81、
  独立 Runtime→UI 46/46与Null样例300帧，Clang 无 sanitizer 诊断。上一 C1c-b3a Pointer/Input
  门禁通过基础185/185、GLFW专项23/23；
  其中 Clang X11 的基础测试不使用 suppression，只有 GLFW/X11 专项进程使用精确
  `leak:_XimOpenIM`，对应13次/5304 B的libX11 XIM retention。GCC 13.4 与 Clang X11 的 Null/GLFW样例各300帧均为
  上一产品门禁，其中 Clang GLFW样例命中1次/408 B。GCC 13 Wayland 双后端产物
  在带 `wl_seat` 的嵌套 Weston 9 下通过183/183、22/22与样例300帧，并通过移除
  `DISPLAY` 与断言 `glfwGetPlatform() == GLFW_PLATFORM_WAYLAND` 确认真正使用 Wayland；
  同一产物强制 X11 后也通过183/183、22/22与样例300帧。纯
  Weston headless 无 `wl_seat` 会命中锁定 GLFW 3.4 的已知初始化崩溃，不是 Tina
  回归，也不属于当前支持范围。Clang 22 双后端 sanitizer 产物同样通过：基础
  183/183无 suppression且Null样例300帧通过；强制 Wayland 后22/22与样例300帧通过且
  `_XimOpenIM` 匹配为0；强制 X11 后22/22与样例300帧通过，仅精确抑制 `_XimOpenIM`
  （专项12次/4896 B、样例1次/408 B）。第三方 GLFW 本身未被 sanitizer 插桩，门禁不宣称
  完整覆盖其内部实现。

完整 M7-E Platform 输入仍需达到：

- 最小化、恢复与 OS Pointer Capture 的真实交互门禁；
- 100%、150%、200% DPI 下 Pointer 命中和 framebuffer viewport 一致；
- Windows IMM32 composition/commit/cancel 与窗口销毁顺序通过测试；
- production GLFW Gamepad sampled diff/registry 保持 M7-A 已冻结的 owner/slot、最终快照和生命周期
  分发契约。
