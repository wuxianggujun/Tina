# Platform、Window 与 Input 契约

> 状态：vNext Input 契约已由 ADR 0015 接受；本文定义 M7 Platform/Input 边界，不表示当前
> GLFW 实现已经完成迁移。

## 结论

Tina 只使用 GLFW 处理 Windows/Linux 窗口、键鼠和标准 Gamepad；不引入 SDL/SDL3。
Windows 文本组合由 `tina_platform_glfw` 内部的 IMM32 adapter 补充。玩法、UI、Scene 和
Render 公共接口不能包含 `GLFWwindow*`、Win32 `HWND` 或平台键值。

为了让 Null Runtime 真正不链接 GLFW，vNext 将平台契约和具体实现分开：

| Target | 职责 | 依赖 |
| --- | --- | --- |
| `tina_platform` | `WindowId`、Window/Input/Event 描述、线程命名、不透明 Render surface、Headless test backend | `tina_core`、OS 最小适配 |
| `tina_platform_glfw` | GLFW 生命周期、真实窗口、键鼠、Gamepad、DPI、Windows IMM32 | `tina_core`、`tina_platform`、GLFW、Windows 时使用 IMM32 |

`tina_platform` 不提供第二套通用窗口框架；拆 target 只为隔离第三方类型和支持无窗口测试。
UTF-8 路径、读取和原子文件写入统一属于 `tina_core/io` 的 OS-specific `.cpp`，Platform 不再
实现第二套 filesystem adapter。

## 所有权与线程

- `EngineHost` 在主线程创建并销毁 Platform backend；
- GLFW 初始化、事件轮询、窗口创建/销毁和大多数窗口操作只在主线程执行；
- 每个窗口使用 generation `WindowId`，stale ID 不能解析到复用后的窗口；
- 首个产品切片只支持一个主窗口，但所有输入、DPI、Focus 与 UIContext 状态都按 Window
  保存，不使用进程级可变全局；
- 主窗口 OS CloseRequested 只设置不可取消的 Platform control latch；Platform Poll 返回该 outcome
  后，Runtime 在下一个 frame 开始前进入统一 shutdown。它不进入可订阅 EventQueue，也不在平台
  callback 中销毁 Scene、UI、Surface 或 RenderDevice；
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
GLFW poll / native callbacks
  -> normalize platform events
  -> primary CloseRequested?
       yes: return PlatformPollResult::ExitRequested
            no PlatformFrameView / no engine frame index
            Runtime stops before Event Queue/Input or any new frame phase
       no:  finalize PlatformPollResult::ContinueFrame
            PlatformFrameView(metrics/input/device snapshots
            + PlatformEventBatch + ordered InputTransitionBatch)
            -> Runtime Platform Event Queue
            -> UI routed transitions (上一帧稳定布局)
            -> InputTransitionConsumption + ContinuousControlClaims
            -> game action mapping / fixed input latch
```

`WindowInputSnapshot` 按窗口保存：

- 键盘和 Pointer 的最终 `held` 状态；
- Pointer 最终 position、累计 delta 和当前 button set；
- 引用同一帧 `WindowMetricsSnapshot::revision`，但不复制 focus、extent、scale 等窗口事实。

Gamepad registry 与最终 sampled state 属于 Engine，不复制进每个 Window snapshot。首个单窗口
切片把所有 Gamepad 输入显式路由到 primary Window；未来多窗口必须通过单独的 active input target
决定路由，不能让同一设备在多个窗口重复产生 Action。

`InputTransitionBatch` 保存 Key/Button Down/Up、Pointer Move、Wheel、Gamepad observed button/axis
change、已提交 UTF-8 text、composition、`InputCancelTransition` 和 `InputStreamReset`；每项都有适用的
window/device/pointer id 与单调 sequence。
连续 Move 只有在中间不存在 Button/Wheel/Capture/Focus 边界时才可合并，文本、composition、
Down/Up 与设备连接绝不合并。批次使用预分配有界存储：先合并可合并 Move；仍满时记录
`InputOverflow` metric，并使用预留 control slot 写入不可丢的 `InputStreamReset`。Reset 使本帧
尚未完成的 UI route、Capture、composition、repeat 和 Action edge 全部按
`InputCancelTransition` 语义取消；下一帧从最终
设备状态显式 resync。Reset 后仍处于 held/non-neutral 的 control 必须保持 gameplay suppressed，
直到真实 release/neutral，不能静默丢一半按键让 `held` 永久卡住。

## UI consumption 与持续控制所有权

UI 使用同一 `PlatformFrameView` 的有序 transition 生成 Pointer/Key/Navigation routed event。每个 Pointer
transition 最多 hit-test 一次；有 Capture 时直接解析 captured UINodeId，需要判定 click 的真实
release 再做一次且仅一次 release-position hit-test。

UI route 输出两份彼此分离的数据：

- `InputTransitionConsumption` 与当前 `PlatformFrameView` identity 绑定，按 transition sequence/ordinal 使用
  固定 bit storage 标记一次性 transition；重复 consume 幂等，错误 frame/sequence 返回明确错误；
- `ContinuousControlClaims` 使用归一化 window/device/control identity，声明本帧由 UI 拥有的
  held/axis/pointer-delta。它不修改 Platform Snapshot，也不是跨帧容器。

Gameplay Action Mapper 只映射未消费 transition 和未 claim control，并跨帧维护
`suppressedUntilReleaseOrNeutral`：

- UI 消费 digital Down 后，即使下一帧 consumption 已销毁，Gameplay 仍看不到该 control 的 held；
- UI 在 control 已经 held 时才通过 `ContinuousControlClaims` 接管它，也必须立即取消该 control
  尚未完成的 Gameplay edge/repeat，并保持 suppression 到真实 Up；该清理使用 Action Cancel 语义，
  不能伪造普通 Released；
- 匹配的真实 Up 仅解除 suppression，不生成 Gameplay Released；
- axis/连续 control 被 claim 后保持 suppressed，直到进入 binding 配置的 dead zone/neutral；
- `InputCancelTransition`、断连和 `InputStreamReset` 取消既有 Gameplay edge/repeat，并把 resync 后仍 held/non-neutral
  的 control 保持 suppressed，直到真实 release/neutral；
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
Input Context；bootstrap 在 `EngineConfig::input.digitalBindings` 注册 `DigitalActionBinding`，样例的
Escape → Frame Exit Action 也必须走该入口，禁止读取 GLFW key 或在 Runtime 中硬编码。后续
GameState-owned Input Context 使用显式整数 priority：UI consumption/claim 永远先于所有 Context；
较高 priority 胜出；同一 Context 内一个 physical control 只能绑定一个 action/domain；两个 active
Context 在相同 priority 竞争同一 control 时激活失败，不能依赖注册顺序。多个 control 可以绑定同一
Action，只有最后一个未抑制 source 释放时才产生 normal Released。

M7-A 容量均在 `EngineConfig::input` 启动时配置、Create 时一次性分配，运行期不扩容：

| 容量 | 默认有效项 | 额外预留 | 硬上限 |
| --- | ---: | ---: | ---: |
| raw `InputTransitionBatch` | 256 | 1 个 `InputStreamReset` slot | 4096 |
| `PlatformEventBatch` | 64 | 1 个 `PlatformEventStreamReset` slot | 1024 |
| `ContinuousControlClaims` | 64 | 0 | 1024 |
| pending `SimulationActionTransitionBatch` | 128 | 1 个 `SimulationInputStreamReset` slot | 4096 |
| Frame Action transition | 128 | 1 个 reset slot | 4096 |
| digital binding | 64 | 0 | 4096 |

`InputTransitionConsumption` 是按本帧 raw transition ordinal 建立的固定 bit storage，不另设独立
capacity。raw 输入一旦 overflow，builder 写入 reset 后，本次 Poll 后续 callback 只更新最终设备
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

## 三条事件通道

以下通道保持分离：

1. `PlatformFrameView`：最终设备快照 + 有序 transition，适合轮询、UI route 和 Action Mapping；
2. Runtime `PlatformEventQueue`：由 `PlatformEventBatch` 泵送 Window resize/focus、设备连接等可观察
   离散事件，RAII Token 订阅；它不是通用 Gameplay EventBus；
3. UI routed event：一次 hit-test 后按 Capture → Target → Bubble 投递。

同一平台事件可以派生出快照状态和一个 Window event，但只能在各自固定阶段泵送一次。
订阅取消、窗口销毁和 generation 失效必须立即阻止后续回调访问旧对象。
主窗口 OS CloseRequested 是例外的不可取消 Platform control outcome：它不进入 EventQueue，避免
与 Runtime stop latch 重复投递。

## Focus、Capture、DPI 与 IME

- 窗口失焦时生成 `InputCancelTransition(reason=FocusLost)`，取消所有 held key/button 的交互语义并清除
  Pointer Capture、keyboard pressed、Gamepad repeat 和未提交 composition；不得伪造普通 Up，
  以免触发 release-position click 或 Gameplay Released；
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
Event Queue；断开时必须先提交
`InputCancelTransition(reason=DeviceDisconnected, device=GamepadId)`，再
回收该 generation，禁止伪造普通 Button Up。

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
- 日志只记录 UTF-8 操作名、错误码和必要上下文，不写用户输入正文或敏感 clipboard。

## 验收

- Headless backend 不链接、不加载 GLFW，Null Runtime 可运行300帧和10,000帧；
- Visual Studio 2026 / MSVC 19.50 与 Linux GLFW backend 能创建、resize、最小化、恢复并正常关闭窗口；
- WindowId/GamepadId stale generation、Focus/设备断开 `InputCancelTransition` 有直接测试；该
  cleanup transition 不触发
  click、Released action 或普通 Up 订阅；
- `CloseRequested` 只产生一次不可取消的 Platform control outcome；本次 Poll 返回后、任何
  Event/Input/UI/Fixed phase 开始前停止，Event Queue 中没有重复关闭事件；
- fixed step 为 0/1/4 次时，`SimulationActionTransitionBatch` 绑定 next uncompleted tick；跨 0-step
  frame 的 transition 保持顺序，首个实际 substep 只消费一次 edge，后续追赶步只读 held/axis；
- UI 消费 Down 后，Gameplay 持续控制保持 suppressed，直至真实 Up 或 axis 回到 neutral；
- keyboard/pointer/text/composition 在同一 Poll 内的 Down→Up 与多事件保持 sequence，Move 合并
  不跨语义边界；Gamepad 只保证相邻 Poll sampled diff，不声称恢复 Poll 间完整 Down→Up；
- Raw `InputTransitionBatch` 与 Simulation action latch 满容量时都通过保留的 reset/control slot
  发出显式 reset，取消 transient edge/repeat/capture，保留最终 physical state，并让仍 held/non-neutral
  的 control 进入 suppression；replay 记录 reset、最终 normalized state 与 tick 绑定，重放结果确定
  且不会永久 stuck；
- Engine 级 Gamepad 输入每帧只路由一次到 primary window，不在每窗口 snapshot 重复投递；
- 100%、150%、200% DPI 下 Pointer 命中和 framebuffer viewport 一致；
- Windows IMM32 composition/commit/cancel 与窗口销毁顺序通过测试；
- Platform/UI/Gameplay 三条输入通道互不重复投递；公共头文件不含 GLFW/SDL 类型，Headless
  backend 不链接、不加载 GLFW 或 SDL。
