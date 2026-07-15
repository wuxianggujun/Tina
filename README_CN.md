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

## 构建

需要 CMake、C++20 编译器和 `VCPKG_ROOT`。Windows 使用 MSVC，Linux 使用 GCC/Clang + Ninja。

```powershell
cmake --preset windows-msvc-vs2022
cmake --build --preset windows-vs2022-debug
```

测试构建完成后直接运行 `tina_tests`，不通过额外测试调度器：

```powershell
out\build\windows-msvc-vs2022\bin\Debug\tina_tests.exe
```

详细状态与约束从 [文档索引](docs/README.md) 开始阅读。所有源码、文档、日志和配置统一使用 UTF-8，MSVC 强制启用 `/utf-8`。
