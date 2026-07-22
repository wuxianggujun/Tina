---
name: develop-tina-vnext-runtime
description: 实现或排查 Tina Runtime 的 Core、Platform/Input、EngineHost、Task、Render、WindowSurface、GLFW/bgfx adapter 与 Desktop bootstrap。Use when editing include/tina/core, platform, integration, task, render, runtime, desktop or corresponding src modules; lifecycle or frame phases; input/action; resize/suspend/close; backend create/rollback. UI 另见 develop-tina-ui（include/tina/ui + src/ui）。
---

# 开发 Tina vNext Runtime

## 先确认当前切片

检查 `git status --short` 与相关 diff，随后阅读：

- 通用：`docs/public-api.md`、`docs/runtime.md`、`docs/design-freeze.md`。
- 输入/平台：`docs/platform-input.md`、ADR 0015。
- Render/Surface：`docs/rendering.md`、ADR 0008、ADR 0020。
- 生命周期/错误：ADR 0003、0004、0014、0019。
- 实现：相关 `include/tina/**`、模块 `CMakeLists.txt`、同名 `.cpp` 和测试。

不要从目标文档推断完成度。当前已实现 Headless/GLFW Platform、bounded PlatformFrame、digital
Action mapping、单个 `IGameState` Runtime、Disabled TaskSystem、NullRender、WindowSurface lease、
clear-only bgfx backend 和 Desktop bootstrap。完整 StateStack/commands、worker、Scene、Asset、Audio、
Pass Scheduler、submission ticket/drain、production Gamepad 和 IMM32 composition 尚未落地。
`EngineHost` 是同一 owner-thread 的 one-shot 生命周期；`GameStatePolicy` 当前只采样保存，尚未驱动
stack/blocking，`GameStateStackBecameEmpty` 没有现有路径，`shutdownDeadline` 也尚未执行超时门禁。

## 保持模块依赖

- `Tina::Core` 无 PUBLIC 第三方依赖。
- `Tina::Platform`、`Tina::Task`、`Tina::Render` 只 PUBLIC 依赖 Core。
- `Tina::WindowSurfaceIntegration` 只依赖 Core + Platform，并隐藏 native payload 解码。
- `Tina::PlatformGlfw` 把 GLFW 保持 PRIVATE；`Tina::RenderBgfx` 把 bgfx 与 integration 保持 PRIVATE。
- `Tina::Runtime` PUBLIC 组合 Tina SPI，当前 PRIVATE 依赖 `Tina::UI` 只为 route-result view ABI。
- `Tina::DesktopBootstrap` PUBLIC 只暴露 Runtime，具体 GLFW/bgfx/Task 组合全部 PRIVATE。
- **无 CoreLegacy/EASTL**；只用 focused `include/tina/core/...`。
- optional adapter 的 public factory header 只暴露 Tina public API/SPI，必须在 adapter option 关闭的
  基础 `tina_tests` 图中通过 header isolation；GLFW factory 可依赖 Platform 与
  WindowSurfaceIntegration，GLFW/native include、opaque payload 与第三方链接保持 PRIVATE。

实现目录可以使用 PRIVATE include，公开头只放 `include/tina/...`。不要把 GLFW、bgfx、native handle
或 `void*` escape hatch 带入 Game API/普通 module public header。

## 遵守创建与关闭事务

当前 `EngineHost::Create` 的逻辑顺序是：

```text
validate config/factory bundle
-> FixedStepAccumulator / PlatformEventDispatcher / ActionMapper
-> Clock
-> Platform
-> TaskSystem
-> Independent branch: create RenderDevice
   or WindowSurface branch: acquire lease + snapshot/validate
      -> create surface RenderDevice(move lease) -> publish window
```

使用 `IndependentPlatformRenderFactories` 或 `WindowSurfacePlatformRenderFactories` 的 tagged variant；
不要 `dynamic_cast` provider 或混装 factory。成功创建后立即转入明确 RAII ownership，失败保留第一个
结构化错误。已提交游戏的停止顺序是 State `onExit`/析构 → Application `onShutdown` → dispatcher
shutdown → Render → Task shutdown/join → Platform → Clock；startup commit 前失败不调用 candidate
`onExit` 或 application `onShutdown`。

WindowSurface lease 必须晚于 Render backend、早于 Platform window 释放。普通错线程 API 调用返回
结构化错误；EngineHost/backend/lease 的错线程析构或带活跃 lease shutdown 才是 terminate 契约，
不要把两者混为一谈。

## 遵守帧数据流

当前真实顺序：

```text
Platform poll + frame/capacity/sequence validation
-> WindowSurface snapshot validation
-> clock/fixed-step accumulation
-> synchronous Platform lifecycle dispatch
-> UI consumption/claims seam
-> ActionMapper
-> 0..4 fixedUpdate
-> updateFrame
-> extractRenderScene
-> updateUI
-> submitFrame
-> present only when submitted and not suspended
-> exit-after-frame handling
```

`PlatformFrameView`、Phase Context、Action snapshot、UI route-result view 和其中的 `span/string_view`
都只在当前 callback/phase有效，不得缓存。当前 EngineHost 传入的 UI consumption/claims 仍是 `None`；
若接入真实 producer，保持 UI → Runtime 的单向 seam，不要把 routing 类型搬回 Runtime。

## 修改 Core

优先复用：

- `Core::Result<T>`/`Status` 与 domain-specific error code。
- `GenerationId/GenerationPool` 的 owner + index + generation、固定容量和回绕 retire。
- `FrameArena`、`CountingMemoryResource`、`MemoryTracker`。
- `FixedStepAccumulator` 与 `Utf8` validator。

返回 `Result/Status` 的函数显式标 `[[nodiscard]]`。公共边界以返回值表达失败；第三方、游戏回调、
C callback 或 frame boundary 的 exception 在各模块现有边界模式中转换，不吞异常。`EngineHost.cpp`
内部 helper 不是跨模块 API。不要重复实现 registry、arena、错误体系、UTF-8 validator 或通用 STL 容器。

## 修改 Platform/Input

- 通过 `PlatformFrameBuilder` 构造有界 snapshot/event/transition；不要直接拼借用 batch。
- Builder 在 Create 时一次性分配；transition/text/event overflow 使用预留 reset slot 并进入明确
  reset/suppression 语义，不静默丢弃、不增长容器或 heap fallback。
- 保留 final snapshot、metrics revision、global sequence、gamepad lifecycle、cancel/reset 的一致性验证。
- Platform callback 只写 backend-owned state/batch，并把首个失败变成 sticky structured error；不在
  callback 中销毁 Runtime、Window 或 Render owner。
- 保留 raw Platform、Platform lifecycle event、UI route、gameplay Action 四个责任边界。
- 游戏只使用 `InputActionId` 和 Simulation/Frame snapshot，不直接依赖 GLFW key code。
- 当前只支持 digital binding；同一 physical pattern 不重复绑定，同一 ActionId 不跨
  Simulation/Frame domain。key/pointer-button/gamepad-button claim 生效，axis/pointer continuous claim
  目前被 mapper 忽略。
- 0 fixed-step 帧必须保留 Simulation edge；多个 catch-up step 只在首个目标 tick 消费一次。
- UI consumed/claimed digital Down 要 suppression 到真实 release；Focus lost、disconnect/reset 不伪造
  会激活按钮的普通 Up。
- GLFW adapter 进程内只允许一个 active backend；创建、poll、window、shutdown 服从 platform owner
  thread，使用 `GLFW_NO_API`。不要另行初始化 GLFW、创建第二 backend 或偷偷创建 OpenGL context。

## 修改 WindowSurface/Render

- 复用 `NativeWindowSurfaceLease`、`RenderSurfaceStateTracker` 和现有 factory；不要创建第二套 bridge。
- 保持 surface identity、source revision、surface revision 和事实变化的严格协议。
- framebuffer 0×0/最小化表示 surface suspended，不表示整个 RenderDevice 失效。
- active submit 后必须恰好 present，未 present 前不得再次 submit。suspended 帧继续 CPU 生命周期但
  返回 skip、不打开 present pair、不递增 submission；恢复先应用最新 extent。
- surface identity 在生命周期内稳定；facts 变化时 `sourceMetricsRevision` 前进且
  `surfaceRevision` 严格 +1，无变化时 revision 不得增加。
- OS close 的 `PlatformPollResult::ExitRequested` 不创建新 PlatformFrame，也不伪造“最后一帧”。
- bgfx backend 当前只有 view 0 的 deep-blue clear/present；新增 Scene/UI/pass/resource 前先建立对应
  backend-neutral SPI、失败测试和资源寿命，不直接把 bgfx 调用塞进 Game/UI。
- 普通桌面游戏调用 `Desktop::CreateEngine`；显式 factories 只用于高级集成、样例和测试。

## 修改 Task

当前只有 `DisabledTaskSystem`，不要把它当线程池。ADR 0017 仍是 Proposed；新增 worker 前先确认并
更新决策状态，再落实有界 CPU/IO/Main completion、stop token、TaskGroup 和 barrier。Worker 不直接
修改 World/UI/RenderDevice。不要提前实现 DAG、fiber 或 work stealing。

## 验证

Windows Null 基线：

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug --target tina_tests tina_sample_null
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_sample_null.exe --frames=300
```

按修改扩大：Platform/GLFW 用 `windows-msvc-vnext-platform` 配置、
`windows-vnext-platform-debug` 构建，直接跑 `tina_platform_glfw_tests` 和
`tina_sample_platform --frames=300 --frame-delay-ms=0`；bgfx/Surface 用
`windows-msvc-vnext-bgfx` + `windows-vnext-bgfx-debug`，直接跑 `tina_render_bgfx_tests` 与 Desktop
sample；UI seam 另跑 `tina_ui_tests`。新增/修改公开头时加入 header-isolation `.cpp` 并构建对应
测试 target；它不是独立运行程序。真实 GLFW/bgfx smoke 需要显示/GPU 环境。Linux/toolchain/
sanitizer 命令使用 `$build-and-test-tina`。

重点测试 owner thread、factory 每个失败点、逆序回滚、0/1/4 fixed step、输入 overflow/reset、
focus/disconnect、surface revision、suspended skip、close 和 exactly-once shutdown。最后执行
`git diff --check`，并扫描 `include/tina` 的第三方/native token。
