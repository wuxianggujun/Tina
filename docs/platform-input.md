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
- Gamepad：generation `GamepadId`、标准 button/axis、连接/断开、snapshot revision；
- Text：strict UTF-8 committed text；
- Composition：Started/Updated/Ended/Cancelled、preedit 与 codepoint cursor；
- Cancel/Reset：focus lost、device disconnect、window closing、capacity/backend recovery。

Gamepad/Window ID 是 owner-aware generation identity，不能按 native index 持久化。Focus loss、disconnect
和 stream reset 必须清除 held/default-action state，不能伪造普通 Up。

## GLFW adapter 当前能力

GLFW backend 已实现：

- keyboard、pointer button/move/wheel 与 content/window/framebuffer metrics；
- Windows 启用 `GLFW_SCALE_TO_MONITOR`，并将 GLFW 原生 window/Pointer 坐标除以当前
  `contentScale` 后发布为 logical pixel；`framebufferExtent` 继续保留物理像素，因此 200% DPI 下
  `framebufferExtent` 约为 `logicalExtent * 2`，UI layout、hit-test 与 Pointer route 使用同一坐标空间；
- committed Unicode text 转 strict UTF-8；
- 标准 Gamepad 轮询（`glfwGetGamepadState`）、generation registry、button diff、axis deadzone/hysteresis、
  connect/disconnect 与 snapshot revision；
- 可选系统 Dark/Light 偏好观察；只有 Desktop 显式开启时才发布 Tina-owned
  `SystemColorSchemeChangedEvent`，同帧变化合并且不携带 Win32/GLFW 类型；
- Windows 原生 WindowSurface binding；Linux X11/Wayland binding；
- owner-thread 创建、poll、publish、lease 与 shutdown。

Windows 还接入 `Imm32CompositionHostWin32`：窗口 subclass 把 IMM32 preedit/commit/cancel 转为
`TextCompositionTransition`/`TextInputTransition`，固定 preedit 容量并校验 UTF-16→UTF-8。UI commit 后，
Runtime 从 `UIContext::committedTextInputCaretRect()` 发布 owner-window logical caret geometry；GLFW
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
- Windows Narrator/Inspect 人工金标：`UI-002`（action/control patterns 与跨进程 HWND gate 已有）；
- Linux AT-SPI adapter 与真实辅助技术验收：`UI-002-LINUX`；
- BiDi/复杂 shaping、Linux 原生 XIM/Wayland preedit/candidate placement，以及 Windows 真机 IME 候选窗
  跟随/提交/取消/失焦人工矩阵：`TEXT-001`。

这些能力不应从已有 `GamepadId`、composition type 或 focused flag 推断为已完成。
