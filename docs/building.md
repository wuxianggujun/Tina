# 构建与运行

## 环境要求

| 平台 | 要求 | 当前验证环境 |
| --- | --- | --- |
| Windows | Visual Studio 2026 x64、支持 C++23 的 MSVC、CMake、`VCPKG_ROOT` | VS 18.4.3、MSVC 19.50.35717、CMake 4.2.3 |
| Linux | 支持 C++23 的 GCC 或 Clang、Ninja、CMake、`VCPKG_ROOT` | Ubuntu 22.04 / GCC 11.4 基础语法门禁 |

Tina 的设计目标是 C++23，但当前自有 target 仍请求 `cxx_std_20`。语言标准需要在独立实现任务中统一恢复并通过 MSVC/GCC/Clang 门禁，本文档不把尚未落地的迁移写成已完成。使用 Preset 时 CMake 至少需要 3.25。先确认当前终端没有命中旧版 CMake：

MSVC 19.50 是当前完整验证基线。Ubuntu 22.04 自带的 GCC 11/Clang 14 只能作为 Tina 当前所用 C++23 子集的兼容门禁；计划使用更多 C++23 标准库能力时，Linux 正式工具链应提升到 GCC 13+ 或 Clang 17+。

```powershell
cmake --version
cmake --list-presets
```

依赖由固定 vcpkg baseline 解析。项目窗口与基础输入只使用 GLFW，不需要 SDL/SDL3。

## Windows Debug

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-debug --target Tina tina_tests
out\build\windows-msvc\bin\Debug\tina_tests.exe
```

## Windows Release

当前没有单独的 Release build preset，使用同一个 Visual Studio 多配置构建目录：

```powershell
cmake --build out\build\windows-msvc `
  --config Release `
  --target Tina tina_tests `
  --parallel 2
out\build\windows-msvc\bin\Release\tina_tests.exe
```

Debug、Release 可执行文件和 app-local DLL 分别位于 `bin/Debug`、`bin/Release`，不能混用 GTest 或其他运行时 DLL。

## Linux Debug

在 Linux 或 WSL 的仓库目录中执行：

```bash
cmake --preset linux-ninja
cmake --build --preset linux-debug --target Tina tina_tests
./out/build/linux-ninja/bin/tina_tests --gtest_color=no
```

只做无 GPU 的编译、链接和单元测试门禁时，可以关闭 shader 构建：

```bash
cmake --preset linux-ninja -DTINA_BUILD_SHADERS=OFF
cmake --build --preset linux-debug --target Tina tina_tests
./out/build/linux-ninja/bin/tina_tests --gtest_color=no
```

`TINA_BUILD_SHADERS=OFF` 的 Tina 可执行文件不包含 cooked shader，不能用作运行包。当前仓库还没有可直接使用的 Clang ASan/UBSan preset，因此 Sanitizer 在补齐配置和验证前保持“待完成”状态。

## Release 冒烟

```powershell
# 主菜单 + 中文 UI
out\build\windows-msvc\bin\Release\Tina.exe --smoke-frames=300

# 独立 UI 场景
out\build\windows-msvc\bin\Release\Tina.exe --smoke-ui --smoke-frames=300

# 完整 2D 场景
out\build\windows-msvc\bin\Release\Tina.exe --smoke-game --smoke-frames=300

# 透视相机 + 深度测试 + 静态索引 Cube
out\build\windows-msvc\bin\Release\Tina.exe --smoke-3d --smoke-frames=300
```

四条命令返回 0 代表主循环与退出链路完成，不自动代表画面验收通过。3D 和 UI 仍应通过人工观察或稳定截图门禁确认。

## 常用选项

| 选项 | 默认值 | 用途 |
| --- | --- | --- |
| `TINA_BUILD_TESTING` | `ON` | 构建 `tina_tests` |
| `TINA_BUILD_SHADERS` | `ON` | 构建运行时 shader；关闭后只适合编译/链接门禁 |
| `TINA_BUILD_WAYLAND` | `OFF` | Linux Wayland 构建，需要对应 vcpkg feature |
| `TINA_AUTOUPDATE_SUBMODULE` | `OFF` | 是否自动更新源码依赖，日常构建应保持关闭 |
| `TINA_BUILD_DOCS` | `ON` | 当前为预留选项，尚未注册文档生成 target |

测试直接运行 GoogleTest 生成的 `tina_tests`，项目不使用 CTest 调度。
