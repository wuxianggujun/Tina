# 构建与运行

## 环境要求

| 当前/Legacy 门禁 | 要求 | 当前验证环境 |
| --- | --- | --- |
| Windows | Visual Studio 2026 x64、CMake、`VCPKG_ROOT` | VS 18.4.3、MSVC 19.50.35717、CMake 4.2.3 |
| Linux | GCC 或 Clang、Ninja、CMake、`VCPKG_ROOT` | Ubuntu 22.04 / GCC 11.4 当前 C++20 门禁 |

| vNext 正式目标 | 最低目标 |
| --- | --- |
| Windows | Visual Studio 2026 / MSVC 19.50，统一 C++23，CMake Preset |
| Linux GCC | GCC 13+，统一 C++23，Ninja |
| Linux Clang | Clang 17+，统一 C++23；独立 ASan/UBSan preset |

Tina 的设计目标是 C++23，但当前自有 target 仍请求 `cxx_std_20`。语言标准需要在独立实现任务中统一恢复并通过 MSVC/GCC/Clang 门禁，本文档不把尚未落地的迁移写成已完成。使用 Preset 时 CMake 至少需要 3.25。先确认当前终端没有命中旧版 CMake：

MSVC 19.50 是当前完整验证基线。Ubuntu 22.04 自带的 GCC 11/Clang 14 只能作为 Tina 当前所用 C++23 子集的兼容门禁；计划使用更多 C++23 标准库能力时，Linux 正式工具链应提升到 GCC 13+ 或 Clang 17+。

```powershell
cmake --version
cmake --list-presets
```

依赖由固定 vcpkg baseline 解析。项目窗口与基础输入只使用 GLFW，不需要 SDL/SDL3。
当前旧 target 仍需要源码形式的 EASTL/EABase；vNext target 不再依赖它们，只有 Legacy
零引用并通过完整门禁后才从仓库与构建中删除。xxHash 继续作为窄 Hash adapter 的私有后端。

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

只做无 GPU 的编译、链接和单元测试门禁时，应使用独立 compile-gate preset/build 目录关闭
shader，避免在 `linux-ninja` 同一 cache 留下 `OFF` 后误拿去做运行包。目标命令为：

```bash
cmake --preset linux-compile-gate
cmake --build --preset linux-compile-gate-debug --target Tina tina_tests
./out/build/linux-compile-gate/bin/tina_tests --gtest_color=no
```

该 preset 尚未落地；在实现前临时使用 `-DTINA_BUILD_SHADERS=OFF` 时必须指定不同
`-B out/build/linux-compile-gate`。输出不包含 cooked shader，不能用作运行包。当前仓库还
没有可直接使用的 Clang ASan/UBSan preset，因此 Sanitizer 在补齐配置和验证前保持“待完成”。

## vNext 目标 Preset

下列名称是设计契约，尚未出现在当前 `CMakePresets.json`：

| Preset | 优化/插桩 | 用途 |
| --- | --- | --- |
| `windows-bench` / `linux-bench` | Release、Trace none、VSync off、固定 worker | 正式 `tina_bench` |
| `windows-profile` / `linux-profile` | 与 Bench 相同优化/CRT/assert/LTO、保留符号、Tracy on | 定位同 workload 回退 |
| `linux-clang-sanitize` | Debug/RelWithDebInfo + ASan/UBSan、Trace none | 生命周期/UB 门禁 |
| `linux-compile-gate` | 独立目录、Shaders off | 无 GPU 编译与测试 |

Bench/Profile 不能只用两个默认 CMake build type 猜测“优化差不多”；最终 flags 和依赖
fingerprint 写入 benchmark JSON。Tracy 只由 Profile preset 的 optional vcpkg feature解析，
普通构建和发行包不查找或打包 Tracy。

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

vNext 还将增加 `TINA_BUILD_LEGACY`、`TINA_BUILD_BENCHMARKS`、`TINA_PROFILE_BACKEND` 和
Tracy lock/memory 子选项；在真正加入 CMake 前只记录为目标，不能把表述当成当前可用命令。
完整依赖可见性、版本和许可证门禁见 [第三方依赖与版本治理](dependencies.md)。
