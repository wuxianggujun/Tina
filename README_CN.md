# Tina 游戏引擎

Tina 是以 C++23 为目标语言基线的 2D/3D 游戏引擎项目。平台与输入层使用 GLFW，私有渲染后端使用 bgfx，音频使用 miniaudio，ECS 使用 EnTT，UI 为完全自研的 Retained UI。vNext 游戏侧契约不暴露 bgfx、GLFW 或其他第三方类型；仍在运行的 Legacy 实现尚未完成这项边界迁移。

## 当前目标

当前阶段允许不兼容旧 API 的完整 vNext 重构，但不会用一个长期不可运行的大提交替换全部
Runtime。现有2D/UI/3D路径继续作为验收基线，新架构按可独立构建和运行的垂直切片迁移：

- 保证现有 2D 场景和自研 UI 能启动、交互和正确释放资源；
- 修复 Application、Event、Resource、Scene 和 UI 的生命周期与每帧驱动顺序；
- 使用直接运行的 GoogleTest 可执行文件覆盖核心行为；
- 增加最小 3D 冒烟场景，验证透视相机、深度测试和静态 Mesh；
- 以 `IGameApplication` 表示“整个游戏程序入口”，以 `IGameState` 表示“菜单、关卡、暂停等逐帧运行状态”，不再使用含义模糊的 `IGame`；
- 建立细粒度 dirty、单次布局、持久 Paint Cache 和稳定 Display List 的高性能 Retained UI；
- 参考 Carbon Engine 的 Frame Step、资源 Load/Prepare/Upload 和 GPU 生命周期，但保持 Tina 架构小而清晰。
- 采用 Tina-owned Trace/Metrics 和可选 Tracy 定位热点，独立 `tina_bench` 建立可重复性能回归；
- 新 target 不使用 EASTL，也不自研通用 STL；标准库/`std::pmr` 加少量专用固定容量结构。

当前旧文档已经替换，但旧源码架构仍是正在运行的主实现，并未完全删除。迁移状态和删除门禁见 [架构总览](docs/architecture.md)。物理后端固定为 2D Box2D 3.x 与 3D Jolt，不引入第三套物理引擎。

vNext 已完成 C++23 Headless Runtime 生命周期内核、M7-A Platform/Input 内核、首个桌面适配切片、
M7-B1 私有 WindowSurface handoff、M7-B2 Desktop bootstrap + 真实 GPU 冒烟，以及 M7-C1b
standalone Retained Tree/Flex-lite layout foundation：私有
`tina_platform_glfw` 已能创建 `GLFW_NO_API` 窗口，
并把键盘、Pointer、Focus、resize、close 与已提交 UTF-8 文本归一化到同一份有界
`PlatformFrameView`；Runtime 通过 generation `WindowSurfaceId`、无原生句柄的
`WindowSurfaceSnapshot` 和 move-only `NativeWindowSurfaceLease` 把窗口 surface 交给 Render
组合，Game SDK 不暴露 native 或 bgfx 类型。`Tina::Desktop::CreateEngine(config)` 已作为普通桌面入口
落地，当前私有组合为 `SteadyClock + GLFW WindowSurface + DisabledTaskSystem + bgfx`；
`tina_sample_desktop` 默认运行300帧，以深蓝色 clear/present 验证真实 Render backend 路径。
`tina_ui` 已有 generation tree、固定容量事务式布局和 committed layout snapshot，但尚未接入 Runtime
或 Render。production Gamepad、Windows IMM32 composition、Scene、hit route、DisplayList、文本/Widget、
Pass Scheduler、submission ticket/drain 与可见中文 UI 分别放在后续切片。

## 当前 Legacy 已完成基线

- 现有 Legacy target 的包依赖已迁移到 vcpkg manifest；bgfx 与 EASTL/EABase 仍是源码依赖；
  这是当前实现事实，不是 vNext 的最终依赖方案；
- Window/Input 已迁移到 GLFW；
- 音频已迁移到 miniaudio；
- Core 已增加强类型、Result、Assert、ScopeExit、Clock 和 FrameTimer；
- Windows/Linux CMake Preset 已建立。
- `GameScene` 已通过真实 2D TileMap、ECS、中文 UI 和音频冒烟；
- `Smoke3DScene` 已通过右手透视相机、深度测试和静态索引 Mesh 冒烟。

vNext 将继续使用锁定源码版本的 bgfx，但新 target 禁止 EASTL/EABase；具体版本、可见性与
删除门禁见[第三方依赖与版本治理](docs/dependencies.md)。

## 构建

目标构建需要 CMake 3.25 以上、支持 C++23 的编译器和 `VCPKG_ROOT`。Tina 自有 target 已统一请求 `cxx_std_23`，MSVC 保持 `/utf-8` 与 `/Zc:__cplusplus`。Windows 最新门禁已在 Visual Studio 2026 18.4.3、MSVC 19.50.35717 和 `D:\Programs\CMake\bin\cmake.exe` 4.2.3 下通过 vNext Null、UI、GLFW 与 bgfx Debug/Release 图：基础183/183、UI 39/39、GLFW专项22/22、bgfx专项11/11、Null样例300帧、WindowSurface GLFW样例300帧，以及真实 D3D11 Intel Iris Xe 的 `tina_sample_desktop` 默认300帧；Debug/Release 构建均通过。`TINA_BUILD_TESTING=OFF` 的 production-style WindowSurface GLFW样例300帧也已通过。Game SDK 与公开头检查未发现 bgfx、GLFW 或 native handle 泄漏。

Linux M7-B1 Platform 门禁覆盖 GCC 13.4 X11、Clang 22.1.8 X11 sanitizer，以及 GCC 13/Clang 22 X11/Wayland 双后端；Wayland 使用带 `wl_seat` 的嵌套 Weston 9。M7-B2 Desktop/bgfx X11 图也已直接运行：GCC 13.4 与 Clang 22.1.8 + ASan/UBSan/LSan 均通过基础183/183、GLFW专项22/22、bgfx专项11/11和 Desktop样例300帧。Clang 基础/bgfx测试不使用 suppression；X11 只对第三方 libX11 `_XimOpenIM` retention 使用精确 suppression，GLFW专项命中12次/4896 B、Desktop样例命中1次/408 B。Clang Desktop 经 bgfx 选择 Vulkan，但当前 WSL2 适配器是 llvmpipe 软件实现，因此该结果证明 Linux Vulkan/backend 生命周期，不代表硬件 GPU 性能。由 vcpkg 提供的 GLFW 本身未被 sanitizer 插桩。详细边界见[测试文档](docs/testing.md)。Clang preset 使用项目 chainload toolchain 固定标准库，不能退回 Ubuntu 22.04 自带的旧 libstdc++。先确认终端没有命中不支持 `Visual Studio 18 2026` 生成器的旧版 CMake：

```powershell
cmake --version
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug --target tina_tests
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe

# 可选 GLFW + NullRender 平台切片
cmake --preset windows-msvc-vnext-platform
cmake --build --preset windows-vnext-platform-debug --target tina_tests tina_platform_glfw_tests tina_sample_platform
out\build\windows-msvc-vnext-platform\bin\Debug\tina_tests.exe
out\build\windows-msvc-vnext-platform\bin\Debug\tina_platform_glfw_tests.exe
out\build\windows-msvc-vnext-platform\bin\Debug\tina_sample_platform.exe --frames=300 --frame-delay-ms=0

# 可选 Desktop bootstrap + 真实 bgfx clear-only GPU 冒烟
cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug --target tina_tests tina_platform_glfw_tests tina_render_bgfx_tests tina_sample_desktop
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_bgfx_tests.exe
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_desktop.exe

cmake --preset windows-msvc
cmake --build --preset windows-debug --target Tina tina_tests
```

测试构建完成后直接运行基础 `tina_tests`；启用 GLFW adapter 时再直接运行独立的
`tina_platform_glfw_tests`。两者都不通过额外测试调度器：

```powershell
out\build\windows-msvc\bin\Debug\tina_tests.exe
```

Release 使用同一个 Visual Studio 多配置构建目录：

```powershell
cmake --build out\build\windows-msvc --config Release --target Tina tina_tests --parallel 2
out\build\windows-msvc\bin\Release\tina_tests.exe
```

Visual Studio 的测试程序和 GTest 运行库按配置隔离在 `bin\Debug`、`bin\Release`，避免 Debug/Release CRT 混用；Linux 单配置构建仍输出到 `bin`。

完整 Windows/Linux 构建说明、选项和门禁限制见 [构建与运行](docs/building.md)。

运行时验收入口：

```powershell
# 主菜单 + 中文 UI
out\build\windows-msvc\bin\Release\Tina.exe --smoke-frames=300

# 专用 UI：虚拟化列表 + 对话框 + 中文 TextEdit（启动即聚焦）
out\build\windows-msvc\bin\Release\Tina.exe --smoke-ui --smoke-frames=300

# 完整 2D + 自研 UI
out\build\windows-msvc\bin\Release\Tina.exe --smoke-game --smoke-frames=300

# 最小 3D：Perspective Camera + Depth Test + Indexed Cube
out\build\windows-msvc\bin\Release\Tina.exe --smoke-3d --smoke-frames=300
```

项目不使用 CTest 调度；测试直接运行固定 GoogleTest 1.17.0 生成的基础 `tina_tests` 与按需构建的
adapter 专项测试。当前精确数量和平台矩阵只在[测试文档](docs/testing.md)维护。

当前 Legacy UI 已具备 generation `NodeId`、Pointer Capture、Focus/Tab、KeyDown/KeyUp 的 Capture/Target/Bubble 路由、方向键空间焦点导航、可嵌套 Modal Focus Scope、Button 的 Enter/Space pressed/release 生命周期与单次激活、每窗口 Theme/DPI、嵌套 Clip、通用 `UIScrollView`、十万行范围计算的 ListView 虚拟化，以及 Windows 原生 IME preedit/composition。每个 Button action 具有独立重入保护、异常恢复和回调自销毁安全性；routed click 目标在路由中删除后通过 generation `NodeId` 立即失效。GLFW 标准手柄的 D-pad/左摇杆可驱动空间导航，A/B 映射为 Accept/Cancel；摇杆带回滞并支持方向长按重复，语义导航仍服从最上层 Modal Focus Scope。Dialog 不再订阅全局键盘事件，Escape 仅在焦点控件未消费时沿祖先链处理；Scene 会在 `onEnter`/`onResume` 交互前激活对应 UI roots。窗口与基础输入只使用 GLFW；IME 通过 Win32 IMM32 补充，不引入其他窗口或输入库。测试数量和平台验证结果只在 [测试文档](docs/testing.md) 中维护。

上段的 `NodeId` 是当前 Legacy 类型名；vNext Game SDK 使用职责更明确、并在所有构建校验
owner `WindowId` 的 `UINodeId`，两者不能被文档混称为已完成迁移。

不了解整体设计时，先阅读 [设计导读](docs/design.md)，再阅读[游戏程序与状态接口](docs/gameplay.md)、[高性能 UI](docs/ui.md)和[后端无关渲染](docs/rendering.md)，或从 [文档索引](docs/README.md) 进入各模块；
候选/已接受/后置状态以 [设计冻结清单](docs/design-freeze.md)与
[ADR 索引](docs/adr/README.md)为准。所有源码、文档、日志和配置统一使用 UTF-8，MSVC
强制启用 `/utf-8`。
