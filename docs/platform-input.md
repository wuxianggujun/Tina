# Platform 与 Input

本文描述 backend-neutral `Tina::Platform`、私有 GLFW adapter 和 Runtime Action/UI 路由。公开头不暴露
GLFW、Win32、X11 或 Wayland 类型。

## 当前模块

| Target | 当前职责 |
| --- | --- |
| `tina_platform` | Window/Input POD、`PlatformFrameBuilder/View`、Headless backend、错误契约 |
| `tina_platform_glfw` | GLFW NO_API 窗口、Keyboard/Pointer/Gamepad、UTF-8 text、WindowSurface lease |
| `tina_window_surface_integration` | move-only native surface handoff，真实 native 类型仍在 PRIVATE adapter |
| `tina_runtime` | lifecycle dispatch、UI route、唯一 ActionMapper、unified binding、Simulation/Frame domain 与运行时 rebind |

## PlatformFrame 契约

一次成功 poll 发布一份 owning builder 背后的只读 `PlatformFrameView`：

- 严格递增 `PlatformFrameId` 与全局 source sequence；
- final Window/Gamepad snapshots；
- 按 source ordinal 排序的 lifecycle event 与 input transition；
- committed UTF-8 text 与可选 composition preedit；
- fixed-capacity storage，满容量时显式 reset/failure，不做 heap fallback。

Runtime 在任何外部 callback 前验证 frame id、容量、UTF-8、payload、reset shape、Gamepad lifecycle 以及
digital edge 与 final held state 的一致性。view、span 和 string_view 只在下一次 poll/build 前有效。

## 输入模型

公开类型包括：

- Keyboard：backend-neutral `Key`、Down/Up/repeat 与 held snapshot；
- Pointer：primary pointer、button/move/wheel，transition 保存事件发生时的 logical position；
- Gamepad：generation `GamepadId`、标准 button/axis、连接/断开、snapshot revision，以及随
  `GamepadConnectedEvent` 携带的 `GamepadDeviceInfo`（name、SDL GUID 与派生 `GamepadLayout`）；
- Text：strict UTF-8 committed text；
- Composition：Started/Updated/Ended/Cancelled、preedit 与 codepoint cursor；
- Cancel/Reset：focus lost、device disconnect、window closing、capacity/backend recovery。

Gamepad/Window ID 是 owner-aware generation identity，不能按 native index 持久化。Focus loss、disconnect
和 stream reset 必须清除 held/default-action state，不能伪造普通 Up。

`PointerSnapshot::present` 区分「没有指针」与「指针停在上次的位置」。位置在缺席时**保留最后已知值**而不是
变成哨兵——否则每个消费者都要自己认哨兵，而 `present` 是唯一需要判断的东西。缺席时不允许持有按键
（`isValidWindowSnapshot` 拒绝该组合）：那会发布一个之后没有 Up 能配对的 press，因为它所属的指针已经没了。
GLFW backend 由 `glfwSetCursorEnterCallback` 驱动它，光标离开窗口时先走既有的 cancel 路径释放按键，与
focus loss 同一形状。默认值是 `true`，因此既有后端与测试语义不变。

这不只是移动端的准备：没有它，hover 会永久锁在最后一个 hover 过的控件上。鼠标总有位置所以可以忍，触摸
设备两次点击之间根本没有位置（[ADR 0032](adr/0032-mobile-platform-contract-boundaries.md) C2）。

`InputCancelTransition::pointer` 给取消加了作用域：具名 pointer 只释放该 slot 的 arm/hover/capture，
nullopt 才是全量取消（focus loss、window closing、stream reset 用）。它与 `gamepad` 互斥——两者是不相交
的设备类别——且越界 slot 由 `isValidInputCancelTransition` 拒绝。UI 侧的 `cancelPointerInteraction()` 接受
同一可选 pointer；具名形式**不**拆除 focus、tooltip、IME 与 command press latch，因为那些是键鼠共享状态，
一根手指抬起不构成拆它们的理由（capture 持有者仍收到 synthetic `PointerCancel`）。

作用域是必需的而非优化：`present` 本来就是 per-pointer，所以平台早已能表达「第 2 根手指抬起了」，但在此
之前 UI 唯一的取消入口会清空全部八个 slot。桌面上看不出来（只有一个指针），触摸设备上就是一根手指抬起
让另一根丢掉正握着的控件——[ADR 0032](adr/0032-mobile-platform-contract-boundaries.md) 引 cocos2d-x 三个
圆形控件为反例的正是这类缺陷。GLFW 的 cursor-leave 因此只取消 Primary。

设备身份（name / SDL GUID / layout）在一次连接内固定，因此随 `GamepadConnectedEvent` 一次性交付，
**不进** `GamepadSnapshot`：snapshot 是每帧复制的16槽数组，把静态字节放进去纯属浪费。存储是固定内联的
`GamepadName`/`GamepadGuid`，超长静默截断——身份只用于展示，缩短标签比因此丢掉 connect 事件更好。
`GamepadLayout` 只决定产品画哪套按键图形，抽象的 South/East/West/North 仍是输入的唯一权威命名；
无法识别时为 `Generic`，因为猜错图形比显示中性提示更糟。分类优先读 SDL GUID 的 vendor id
（bytes 8..11，little-endian），name 只作兜底：驱动与 OS 会改 name，vendor id 不会。

身份**每帧重新采样**并与上一帧比较，因为 poll 型后端看不见"断开后立刻插入同一 joystick id"——槽位看起来
是连续占用的。若不比较，新手柄会静默继承旧 `GamepadId`、旧 layout 与按该 id 建立的 per-player 分配，
于是产品画错按键图形、把输入路由给错误的本地用户，而帧里没有任何迹象。检测到换设备时发出完整的
cancel + disconnect + connect 三段，与真实拔插同一路径，因此旧 id 上按住的输入一定被释放。
GUID 先比（它标识型号），name 也要比（同型号两只手柄 GUID 相同，但互换仍是一次连接结束、另一次开始）；
新身份为空时视为未变化——驱动停止上报身份不构成"换了设备"的证据，否则每帧都会伪造一次断开。

**震动/haptics 不在能力内,也无法通过当前后端实现**：GLFW 完全没有输出到手柄的 API（已核验 vendored
`glfw3.h`），gamepad 面是只读的。要支持震动必须新增一个平台后端能力或换/补 backend，属独立决策。

## GLFW adapter 当前能力

GLFW backend 已实现：

- keyboard、pointer button/move/wheel 与 content/window/framebuffer metrics；
- Windows 启用 `GLFW_SCALE_TO_MONITOR`，并将 GLFW 原生 window/Pointer 坐标除以当前
  `contentScale` 后发布为 logical pixel；`framebufferExtent` 继续保留物理像素，因此 200% DPI 下
  `framebufferExtent` 约为 `logicalExtent * 2`，UI layout、hit-test 与 Pointer route 使用同一坐标空间；
- committed Unicode text 转 strict UTF-8；
- 标准 Gamepad 轮询（`glfwGetGamepadState`）、generation registry、button diff、axis deadzone/hysteresis、
  connect/disconnect 与 snapshot revision；身份经 `glfwGetGamepadName`/`glfwGetJoystickGUID` 每帧采集，
  用于连接事件与换设备检测；
- `PlatformBackendCreateParams::gamepadMappings` 接受追加的 SDL_GameControllerDB 映射行，在首次 poll 前
  经 `glfwUpdateGamepadMappings` 应用（非法内容 fail closed）。GLFW 只识别**有映射**的手柄，因此比内置
  映射表更新的设备原本完全不可见；这是不重建第三方依赖就能修的唯一途径。已插入但无映射的 joystick 计入
  `PlatformFrameDiagnostics::unmappedGamepadCount`——"插了手柄但没反应"与"绑定表坏了"在现场无法区分，
  而从沉默中猜不出解法。
- 可选系统 Dark/Light 偏好观察；只有 Desktop 显式开启时才发布 Tina-owned
  `SystemColorSchemeChangedEvent`，同帧变化合并且不携带 Win32/GLFW 类型；
- Windows 原生 WindowSurface binding；Linux X11/Wayland binding；
- owner-thread 创建、poll、publish、lease 与 shutdown。

Windows 还接入 `Imm32CompositionHostWin32`：窗口 subclass 把 IMM32 preedit/commit/cancel 转为
`TextCompositionTransition`/`TextInputTransition`，固定 preedit 容量并校验 UTF-16→UTF-8。同一轮 native poll
使用有界 FIFO 保留 Started/Updated/Ended 顺序，连续 progress 合并为最新 preedit；即使输入法没有先发布 preedit，
非空 `GCS_RESULTSTR` 也会直接产生 Ended + committed text。失焦会先 drain 已发生事件，再取消 active session，
不会让旧 progress 晚于 Cancel 发布；FIFO 元素和返回的 optional 每次 move 都会把 composition 的 borrowed
`string_view` 重绑到目标 `Pending` 自有 UTF-8 storage，包含 SSO 的短中文 preedit/commit 也不会悬空。commit 由
IMM32 result 路径唯一发布，不再用跨 poll 的“吞下一字符”标记去重。UI commit 后，
Runtime 从 `context.publication().committedTextInputCaretRect()` 发布 owner-window logical caret geometry；GLFW
adapter 按当前 content scale 转为 native client pixels，并更新 IMM32 composition/candidate placement。
placement 为空、caret 与 clip 无正面积交集、窗口 hidden/minimized 或几何无效时会清除旧 hint 并恢复
IMM32 默认候选窗策略。对应 session 测试覆盖 started/updated/ended、focus-lost cancel、非法 UTF-8 与
surrogate；placement 的 logical/DPI/invalid/clear 矩阵由 `tina_platform_glfw_tests` 覆盖。

Windows 系统配色 observer 从用户 Theme preference 读取 `AppsUseLightTheme`，查询不到时不发布事件；
Linux 当前没有系统配色 adapter，因此即使请求 follow 也保持应用默认 Theme。该能力默认关闭，避免产品
CLI、像素 gate 与 Headless 测试被宿主设置隐式改写。事件若因 platform-event capacity reset 未发布，observer
不会提前提交已发布状态，下一帧继续尝试。

Linux 当前只保证 GLFW committed text；原生 XIM/Wayland preedit、候选窗定位仍未完成。非空 placement
在 Headless backend 明确返回不支持，`nullopt` 清理成功。TEST-001 已完成
GCC13 Null + Platform/GLFW(Xvfb) 与 Clang22 Null/sanitizer 的当前 tip 复验；可选 Wayland/真显示器和真实
设备 Gamepad 矩阵仍需独立平台证据，不能由 translation 单测替代。

## WindowSurface

GLFW 平台先创建隐藏的 NO_API 主窗口。windowed factory 取得 move-only native surface lease 和初始
snapshot，RenderDevice 成功创建后才 `publishPrimaryWindow()`。lease 与 snapshot identity 必须一致；
失败时逆序回滚，不能提前显示半初始化窗口。

每帧 Runtime 核对 metrics/surface revision：回退、跳号或旧 metrics 上的 surface facts 变化均失败。
framebuffer 0x0 表示 suspended；Render submit 必须返回 `SkippedSuspendedSurface`，不 present。

## Runtime 路由顺序

```text
Platform lifecycle dispatch
  -> UI input route/default action
  -> UI transition consumption + continuous-control claims
  -> ActionMapper
  -> SimulationActionSnapshot / FrameActionSnapshot
```

UI 读取上一帧 committed hit snapshot。Button、Checkbox、Slider、RadioButton、TextEdit 的 Pointer 默认
行为已接入；Tab/Shift+Tab 在可见 Targetable 控件间循环，Enter/Space/KeypadEnter 与 Gamepad South
触发默认 action。TextEdit 消费 navigation/edit key、committed text 与 IME composition，避免穿透玩法。
`UIFocusScopeMode::Contain`、显式 focus、topmost committed Modal barrier、嵌套/跨 root focus 恢复与持久
Primary Pointer Capture 已接入。capture 路由保留独立物理 hit；Up、输入 cancel、destroy、disable、
Hidden/Collapsed 与 Modal scope change 会释放 capture，其中 target 失效路径先沿原 committed ancestry
合成一次 synthetic-only `PointerCancel`。Keyboard Arrow/Gamepad D-pad 方向空间导航已复用 committed geometry
的 beam/axis scoring；成功移动的 Down/Up 由固定容量 latch 成对消费。

Gamepad **左摇杆**走同一条 spatial focus 路由，优先级与 D-pad 相同（都在有方向状态的控件拒绝该
transition 之后），因此接入它不改变 D-pad 或已聚焦 Slider 的既有行为。摇杆报的是连续值而 focus 导航是
边沿触发，所以每个 gamepad slot、每条轴各有一个 latch：越过 step 阈值 `0.60` 触发**一次**移动，必须回落到
release 阈值 `0.40` 以下才重新武装——否则玩家按住摇杆时 focus 会以帧率连跳。两个阈值分离是为了防止停在
阈值附近时反复触发；两者都高于 backend 的 `0.18` 径向死区，居中摇杆不会碰到。slot 被不同 generation 复用
时 latch 重置，新手柄不会继承上一个的方向。**右摇杆与扳机不参与 focus 导航**：右摇杆按惯例是镜头，会和
菜单导航打架。

`preventDefaultAction()`、route stop、transition consume 和 control claim 是不同语义：consume/claim 阻止
Gameplay Action，不能隐式替代 UI default-action policy。

Accessibility action 不伪造 Platform transition。平台中立 `UIAccessibilityAction` seam 在 UI owner
thread 执行 Focus/Invoke/Toggle/SetRangeValue/SetTextValue，并复用正常控件行为。Windows UIA 已实现
Invoke/Toggle/RangeValue/Value control patterns，`RunUi002UiaGate.ps1` 可从独立进程连接真实 showcase
HWND；这仍不等于 Narrator/Inspect 人工金标。Linux AT-SPI 已拆为 `UI-002-LINUX`。

## Action domain

- Runtime 只维护 `InputActionMapConfig::bindings` 一套模型。`InputActionBinding` 用稳定
  `InputBindingId` 把 Key、Pointer Button、Gamepad Button 或 Gamepad Axis 映射到一个
  `InputActionId`；Platform 不拥有第二套 Action mapper 或 snapshot。
- Digital 与 analog 都产出浮点 `InputActionState::value` 及 Started/ValueChanged/Completed/Cancelled
  transition。Digital source 的 active 值由 `scale` 决定；axis 先按 Signed/PositiveHalf/NegativeHalf/Trigger
  归一化，再应用 gameplay `deadzone`、deadzone 外重映射和 `scale`。
- 同一 Action/domain 的多个 source 使用 `SumClamped` 或 `StrongestMagnitude`。前者求和后 clamp 到
  [-1, 1]，后者选择绝对值最大的 source，并以稳定 binding/source 顺序解决等幅值；所有已连接
  Gamepad generation 都参与组合，不固定到 native slot。
- UI consume 会压制对应 transition；digital/axis continuous claim 会取消既有 Gameplay source，并分别
  保持 suppression 到真实 release 或 neutral/deadzone。UI route result 仍由 `Tina::UI` 拥有，Runtime
  只消费其 borrowed view。
- Simulation binding 只在 fixed tick 读取；0步帧保留 value transition，多步追赶不重复消费；Frame
  binding 只在 `updateFrame()` 读取。同一隐式 transition 不同时广播到两个 domain。
- world pointer action 在映射时使用 last-presented Camera2D 和 surface revision 固化
  `WorldPointerSample`；viewport miss 为 `hit=false`，缺 Camera 为结构化失败。样本跨0步帧锁存，不会按
  后续 Camera 或 resize 重算。

## 运行时 rebind

只有栈顶 State 的 `FrameUpdateContext::inputActionRebinding()` 可取得 phase-local 窄 facade；下层 State
得到 null，游戏不能持有或替换 Runtime mapper owner。`begin()` 进入 `Capturing`，`commit()` 只把替换
排入 `Queued`，并在下一次 Action mapping frame 开始时原子应用，因此当前 callback 的 snapshot 不会被
中途改写。

冲突策略是显式 `Reject` 或 `Swap`：Reject 返回冲突 binding 且保留 capture，Swap 在提交点交换 physical
pattern。`cancel()` 同时允许取消 `Capturing` 和 `Queued` transaction。绑定到特定
`GamepadId` generation 的 capture/queue 在设备断连，或 raw reset 后无法继续证明该 generation 存活时
转为 `DeviceDisconnected`，不会迁移到重连后的新 generation。当前切片不包含完整输入设置 UI、binding
持久化/云同步或第三方 input SDK。

## 验证

```powershell
cmake --preset windows-msvc-vnext-platform
cmake --build --preset windows-vnext-platform-debug `
  --target tina_tests tina_platform_glfw_tests tina_sample_platform --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-platform\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-platform\bin\Debug\tina_platform_glfw_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-platform\bin\Debug\tina_sample_platform.exe --frames=300
```

UI/Input 修改还需运行 `tina_ui_tests`、`tina_runtime_ui_tests` 与 product-2d smoke。TEST-001 的 Linux
X11(Xvfb)/sanitizer 证据已经记录；可选 Wayland/真显示器、真实 Gamepad 与后续平台结果按
[测试说明](testing.md) 单独记录。

## 尚未完成

- 可选 Linux Wayland/真显示器与真实设备 Gamepad 矩阵；TEST-001 当前 tip 已完成；
- **移动端（Android/iOS）平台后端**：`src/platform/` 只有 `glfw` 与 `headless`，全仓库零个 Android/iOS
  引用。范围与顺序已由 [ADR 0032](adr/0032-mobile-platform-contract-boundaries.md)（Proposed）冻结：后端
  本身只有 7 个纯虚，成本在**六个已生效的桌面契约**上——只发 `PrimaryPointerId`、指针位置每帧必须有限且
  存在、native surface 在 RenderDevice 生命周期内不变、poll 线程 == 渲染线程、只有 D3D11/OpenGL/Vulkan
  三个 renderer、preedit 由应用控制。其中**多点触控与 pointer presence 不需要移动后端，可在桌面上完成并
  验证**，是最大的单项前置工作（backlog `MOBILE-001` 切片 A）。ADR 0032 的 D3（`run()` 保持阻塞还是改成
  外部驱动的 `tick()`）未定则 iOS 无法开工。手柄部分的参照见下节；
- Windows Narrator/Inspect 人工金标：`UI-002`（action/control patterns 与跨进程 HWND gate 已有）；
- Linux AT-SPI adapter 与真实辅助技术验收：`UI-002-LINUX`；
- BiDi/复杂 shaping、Linux 原生 XIM/Wayland preedit/candidate placement，以及 Windows 真机 IME 候选窗
  跟随/提交/取消/失焦人工矩阵：`TEXT-001`。

这些能力不应从已有 `GamepadId`、composition type 或 focused flag 推断为已完成。

## 参考：cocos2d-x 手柄实现的可迁移教训

**补充（2026-08-28，屏幕摇杆）：cocos2d-x 整棵树里没有任何虚拟/屏幕摇杆。** 搜过
`joystick`/`thumbstick`/`dpad`/`VirtualPad`/`TouchStick`/`Sneaky` 的文件名与内容，全部命中都是**实体手柄**
代码；`cocos/ui/` 的 24 个 widget 里没有径向控件。最接近的是
`extensions/GUI/CCControlExtension` 的三个圆形控件，它们的缺陷正好构成 Tina
`include/tina/ui/UIVirtualStick.hpp` 的设计依据（每条都有对应单测）：

- `CCControlSaturationBrightnessPicker.cpp:205-209`：手指移出 base 圆的瞬间 **knob 冻结**，因为 move
  处理器在 `dist > radius` 时提前返回；修复代码就在源码里被注释掉了。Tina 改为夹到环上并保持满偏——
  继续滑动仍然满舵，这才是玩家预期。
- `CCControlHuePicker.cpp:154-166`：命中测试是**硬编码环带 59..80**，而 knob 实际travel半径是 61
  （`:107`），并且 x 上加了个裸 `+ 10` 而 y 没有对应项。Tina 的每个半径都从 config 推导，不含针对单一
  屏幕尺寸调出来的字面量。
- 全引擎无 deadzone，原始值直达游戏。Tina 用径向 deadzone + 外侧重标定，形状与 GLFW 后端一致。
- `CCEventDispatcher.cpp:997-1006`：`EventListenerTouchOneByOne` 会把**每一个**命中控件的 touch 都
  claim 进 `_claimedTouches`，所以两根手指在同一摇杆上都会驱动 knob、互相争抢；三个控件都没有 touch-id
  防护。Tina 记录engage 时的 pointer，其余一概忽略。
- 三个 picker **都没有 touch-ended 处理**，knob 永不回中。Tina 的 release/cancel 一律回中。

保留下来的一条尺寸关系来自 `CCControlHuePicker.cpp:107`（`limit = width*0.5f - 15.0f`）：travel 要让
**knob 整体留在 base 内**，而不是让 knob 圆心走到 base 边缘。绝对尺寸没有沿用——那是取色器而非拇指控件。
消费面与接线注意事项见 [UI 框架](ui.md) 的「案例：屏幕摇杆」。

2026-08-28 通读 cocos2d-x 的 controller 实现（`cocos/base/CCController*`、
`cocos/platform/android/**/GameController*`）后记录。**结论：不移植任何代码**——它的形态与 Tina 冲突
（每个 controller 一个可变对象、`delete this`、复用并 latch `stopPropagation` 的事件对象、按设备名字符串
查表），但其中的"坑"是真金，且大多来自真实设备。以下按"Tina 已避免 / Tina 应吸收"两类记录。

**Tina 已经避免的（作为设计校验，不需要动作）：**

- *身份没有 generation。* 桌面端身份只是 GLFW 槽位号，注释直言拿不到断开设备的名字；同槽换设备后旧
  mapping 会静默套到新手柄上。Tina 的 `GamepadId` 是 owner-aware generation，加上本轮的身份比较后，同槽
  换设备走完整 disconnect/connect。
- *`isConnected()` 在 3 个平台上硬编码 `return true`。* 因为它的 controller 对象在断开时 `delete this`。
  Tina 的槽位状态是值语义，断开即清空。
- *完全没有 deadzone。* 全仓库搜不到 deadzone，原始值直接进事件，全部丢给游戏。Tina 是后端径向 0.18 +
  per-binding 两级。
- *桌面端每帧对每个手柄的每个按钮/轴无条件发事件*，按住的键以 60Hz 触发 repeat。Tina 做 button diff 与轴
  hysteresis。
- *按设备名字符串精确匹配的 172 个手写 profile*（名字里带双空格、尾随空格的都有），且因为一个 raw index
  只能映射到一个 key，**180 行映射被注释掉**——其中 64 个 `DPAD_DOWN`、62 个 `DPAD_RIGHT`。结果是约 64 款
  手柄上"下"会以负值报成"上"。Tina 走 SDL_GameControllerDB + `glfwUpdateGamepadMappings`，方向由映射层
  解决，且未映射的手柄计入 `unmappedGamepadCount` 而不是静默失效。

**值得吸收的教训（其中第 1 条本轮已修）：**

1. **同槽换设备必须当成两次连接。** 已实现，见上文"输入模型"。
2. **同一物理控件不要有两条到达路径。** Android 侧 L2/R2 同时经 key 路径与 axis 路径到达同一个 key code，
   互相覆盖；只有 Moga adapter 显式 `return` 掉了 key 路径，默认路径没有。Tina 目前 trigger 只有 axis 一条
   路，`GamepadButton` 里没有 trigger 项——这条要保持，不要为"方便"再加一个 digital trigger 按钮。
3. **D-pad 可能以 hat axis 而非按钮到达。** cocos 的 Android 路径只处理 `KEYCODE_DPAD_*`，从未处理
   `AXIS_HAT_X/Y`(15/16)，所以驱动只报 hat 的手柄 D-pad 完全失效。Tina 当前依赖 GLFW 把 hat 归一化为
   `GLFW_GAMEPAD_BUTTON_DPAD_*`，**这是有效的前提假设**；若将来新增非 GLFW 后端（尤其 Android），hat→button
   的归一化必须由该后端自己补齐，不能假定上层能收到按钮。
4. **多手柄不能共享"上一次的值"。** `GameControllerHelper` 只有一组 `mOldLeftThumbstickX/Y` 等，每个
   Activity 一个实例，两只手柄互相污染彼此的轴状态与变化检测。Tina 的 `GamepadSlotState` 是 per-slot 的，
   这一点必须在任何后续重构（例如把变化检测上移）中保持。
5. **C++ 与 Java 的按键枚举靠数值巧合对齐**，无翻译层、无 static assert，改动任一侧顺序就静默错位；且
   `1017/1018` 在 C++ 叫 `AXIS_*_TRIGGER`、在 Java 叫 `BUTTON_*_TRIGGER`。若 Tina 将来引入 JNI 输入桥，
   跨语言枚举必须有单一权威来源与编译期校验，不能手抄两份。

**Android 支持的形状（cocos 的做法与代价）：**

- 检测走 `InputManagerCompat`（AOSP ControllerSample 的抄本），`SDK_INT >= 16` 用系统 `InputManager` 的
  device listener，否则每 3 秒轮询 `InputDevice.getDevice(id)` 判空推断断开。设备分类是
  `getSources() & (SOURCE_GAMEPAD | SOURCE_JOYSTICK)`。
- Java→C++ 每个事件都经 `Cocos2dxHelper.runOnGLThread`（即 `GLSurfaceView.queueEvent`）marshal 到 GL 线程
  ——线程问题解决了，但代价是**每个事件分配一个 Runnable 并捕获一个 String**，轴事件洪流下是实打实的 GC
  压力。Tina 的 `PlatformFrameBuilder` 是固定容量、per-poll 的，若做 Android 桥应让 Java 侧写入固定环形
  缓冲、C++ 在 poll 时取走，而不是每事件一个 Runnable。
- JNI 只靠名字修饰导出，没有 `RegisterNatives`；反向调用用字符串查类名，而那个类只存在于可选模块里，
  默认工程调用即失败。
- `ControllerManualAdapter` 之所以与主 java 目录分开，是因为它链接三个第三方预编译 jar（Moga
  `com.bda.controller.jar`、Nibiru、OUYA `ouya-sdk.jar`）——三个产品今天都已消亡。**教训是把设备特化的
  第三方 SDK 隔离在可选模块里**，但代价是 JNI 桥的生产者（`GameControllerAdapter.java`，在必编目录）与
  消费者（`GameControllerHelper`，在可选模块）被拆开，默认工程里手柄静默不工作且不报错。
- 若 Tina 要做 Android：需要新增 `IPlatformBackend` 实现（GLFW 不支持 Android）、Java↔JNI 输入桥、
  gradle/manifest/NDK 工具链，以及把 hat→button、L2/R2 双路径去重放在该后端内部完成——这样
  `PlatformFrame` 的语义与 GLFW 后端保持一致。属独立决策，需要先有 ADR。
