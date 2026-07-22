# Platform 与 Input

本文描述 backend-neutral `Tina::Platform`、私有 GLFW adapter 和 Runtime Action/UI 路由。公开头不暴露
GLFW、Win32、X11 或 Wayland 类型。

## 当前模块

| Target | 当前职责 |
| --- | --- |
| `tina_platform` | Window/Input POD、`PlatformFrameBuilder/View`、Headless backend、错误契约 |
| `tina_platform_glfw` | GLFW NO_API 窗口、Keyboard/Pointer/Gamepad、UTF-8 text、WindowSurface lease |
| `tina_window_surface_integration` | move-only native surface handoff，真实 native 类型仍在 PRIVATE adapter |
| `tina_runtime` | lifecycle dispatch、UI route、ActionMapper、Simulation/Frame domain |

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
- committed Unicode text 转 strict UTF-8；
- 标准 Gamepad 轮询（`glfwGetGamepadState`）、generation registry、button diff、axis deadzone/hysteresis、
  connect/disconnect 与 snapshot revision；
- Windows 原生 WindowSurface binding；Linux X11/Wayland binding；
- owner-thread 创建、poll、publish、lease 与 shutdown。

Windows 还接入 `Imm32CompositionHostWin32`：窗口 subclass 把 IMM32 preedit/commit/cancel 转为
`TextCompositionTransition`/`TextInputTransition`，固定 preedit 容量并校验 UTF-16→UTF-8。对应 session
测试覆盖 started/updated/ended、focus-lost cancel、非法 UTF-8 与 surrogate。

Linux 当前只保证 GLFW committed text；原生 XIM/Wayland preedit、候选窗定位仍未完成。真实设备
Gamepad 矩阵、Linux 当前 tip 与不同窗口系统必须通过平台门禁证明，不能由 translation 单测替代。

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
当前有最小 focused state/focus ring；通用 Focus Scope、Modal、持久 Pointer Capture、方向/手柄空间导航
仍后置。

`preventDefaultAction()`、route stop、transition consume 和 control claim 是不同语义：consume/claim 阻止
Gameplay Action，不能隐式替代 UI default-action policy。

## Action domain

- Simulation binding 只在 fixed tick读取；0步帧保留 edge，多步追赶不重复消费；
- Frame binding 只在 `updateFrame()` 读取；
- 同一隐式 edge 不同时广播到两个 domain；
- world pointer action 使用 last-presented Camera2D 锁存结果；viewport miss 为 `hit=false`，缺 Camera 为
  结构化失败。

## 验证

```powershell
cmake --preset windows-msvc-vnext-platform
cmake --build --preset windows-vnext-platform-debug `
  --target tina_tests tina_platform_glfw_tests tina_sample_platform -- /m:1 /v:m
out\build\windows-msvc-vnext-platform\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-platform\bin\Debug\tina_platform_glfw_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-platform\bin\Debug\tina_sample_platform.exe --frames=300
```

UI/Input 修改还需运行 `tina_ui_tests`、`tina_runtime_ui_tests` 与 product-2d smoke。Linux X11/Wayland、
真实 Gamepad 与 sanitizer 结果按 [测试说明](testing.md) 单独记录。

## 尚未完成

- Linux 当前 tip 的 GCC/Clang/X11/Wayland 复验：`TEST-001`；
- Windows UIA / Linux AT-SPI：`UI-002`；
- 通用 Focus Scope、Modal、持久 Pointer Capture：`UI-004`；
- 多行编辑、复杂 shaping 与候选窗定位：`TEXT-001`。

这些能力不应从已有 `GamepadId`、composition type 或 focused flag 推断为已完成。
