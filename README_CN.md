# Tina 游戏引擎

Tina 是使用 C++20 开发的 2D/3D 游戏引擎项目。平台与输入层使用 GLFW，渲染使用 bgfx，音频使用 miniaudio，ECS 使用 EnTT，UI 为完全自研的 Retained UI。

## 当前目标

当前阶段不是推倒重写，而是在现有大量修改之上完成一条可验证的运行链路：

- 保证现有 2D 场景和自研 UI 能启动、交互和正确释放资源；
- 修复 Application、Event、Resource、Scene 和 UI 的生命周期与每帧驱动顺序；
- 使用直接运行的 GoogleTest 可执行文件覆盖核心行为；
- 增加最小 3D 冒烟场景，验证透视相机、深度测试和静态 Mesh；
- 参考 Carbon Engine 的 Frame Step、资源 Load/Prepare/Upload 和 GPU 生命周期，但保持 Tina 架构小而清晰。

## 已完成基线

- 依赖迁移到 vcpkg manifest，bgfx 与 EASTL 保留源码依赖；
- Window/Input 已迁移到 GLFW；
- 音频已迁移到 miniaudio；
- Core 已增加强类型、Result、Assert、ScopeExit、Clock 和 FrameTimer；
- Windows/Linux CMake Preset 已建立。
- `GameScene` 已通过真实 2D TileMap、ECS、中文 UI 和音频冒烟；
- `Smoke3DScene` 已通过右手透视相机、深度测试和静态索引 Mesh 冒烟。

## 构建

需要 CMake、C++20 编译器和 `VCPKG_ROOT`。Windows 当前验证环境为 Visual Studio 2026 18.4.3、MSVC 19.50，Linux 使用 GCC/Clang + Ninja。

Visual Studio 2026 的生成器名为 `Visual Studio 18 2026`。本机系统 `PATH` 已配置 `D:\Programs\CMake\bin`，当前使用 CMake 4.2.3；不要再调用不识别该生成器的旧版 CMake：

```powershell
$cmake = 'D:\Programs\CMake\bin\cmake.exe'
& $cmake --preset windows-msvc
& $cmake --build --preset windows-debug
```

测试构建完成后直接运行 `tina_tests`，不通过额外测试调度器：

```powershell
out\build\windows-msvc\bin\Debug\tina_tests.exe
```

Visual Studio 的测试程序和 GTest 运行库按配置隔离在 `bin\Debug`、`bin\Release`，避免 Debug/Release CRT 混用；Linux 单配置构建仍输出到 `bin`。

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

项目不使用 CTest 调度；测试直接运行固定 GoogleTest 1.17.0 生成的 `tina_tests`。

当前 UI 已具备 generation `NodeId`、Pointer Capture、Focus/Tab、KeyDown/KeyUp 的 Capture/Target/Bubble 路由、方向键空间焦点导航、Button 的 Enter/Space pressed/release 生命周期与单次激活、每窗口 Theme/DPI、嵌套 Clip、通用 `UIScrollView`、十万行范围计算的 ListView 虚拟化，以及 Windows 原生 IME preedit/composition。相关行为由 41 项 GoogleTest 覆盖。窗口与基础输入仍使用 GLFW；IME 通过 Win32 IMM32 补充，不引入其他窗口或输入库。

详细状态与约束从 [文档索引](docs/README.md) 开始阅读。所有源码、文档、日志和配置统一使用 UTF-8，MSVC 强制启用 `/utf-8`。
