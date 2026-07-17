# 构建与运行

## 环境要求

| 当前/Legacy 门禁 | 要求 | 当前验证环境 |
| --- | --- | --- |
| Windows | Visual Studio 2026 x64、CMake、`VCPKG_ROOT` | VS 18.4.3、MSVC 19.50.35717、CMake 4.2.3 |
| Linux | GCC 或 Clang、Ninja、CMake、`VCPKG_ROOT` | Ubuntu 22.04 / GCC 13.4 与 Clang 22.1.8 已通过 vNext Null 图 |

| vNext 正式目标 | 最低目标 |
| --- | --- |
| Windows | Visual Studio 2026 / MSVC 19.50，统一 C++23，CMake Preset |
| Linux GCC | GCC 13+，统一 C++23，Ninja |
| Linux Clang | Clang 22.x + GCC/libstdc++ 15.x，统一 C++23；独立 ASan/UBSan preset |

Tina 自有 target 已统一请求 `cxx_std_23`，MSVC 继续强制 `/utf-8` 与 `/Zc:__cplusplus`。MSVC 19.50 是当前完整验证基线，CMake 将 `cxx_std_23` 映射为该工具链的 `stdcpplatest`。使用 Preset 时 CMake 至少需要 3.25。

当前 Windows vNext 实测使用 Visual Studio 2026 18.4.3、MSVC 19.50.35717 与
`D:\Programs\CMake\bin\cmake.exe` 4.2.3；该 CMake 目录已在 Machine PATH 中，命令可直接写成
`cmake`。

Ubuntu 22.04 自带的 GCC 11/Clang 14 与 CMake 3.22 不构成 vNext 正式 C++23 门禁。
`linux-gcc13-vnext` 固定 GCC 13；`linux-clang22-vnext` 通过
`cmake/toolchains/linux-clang22-libstdcxx15.cmake` 同时固定 Clang 22.x 与 GCC/libstdc++ 15.x。
原因是只指定 Clang 可执行文件不会固定 C++ 标准库：旧 libstdc++/libc++ 组合不能同时稳定提供
`std::expected` 与 `std::move_only_function`。两条 Linux Null 门禁均已用 CMake 4.2.3、Ninja
1.13.2、GoogleTest 1.17.0 实际完成；Clang 配置同时通过 ASan/UBSan。

当前源码不使用 C++ Modules，因此根构建显式关闭 CMake 的 Modules dependency scan，避免
Clang 安装未附带 `clang-scan-deps` 时产生无关失败；未来真正采用 Modules 必须单独建立 ADR、
依赖扫描与 BMI/ABI 门禁后再开启。

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

## Windows vNext 最小构建

该 preset 关闭 Legacy、bgfx/shader 和 vcpkg 默认 feature，构建当前 vNext M6-A/M7-A 的 `tina_core`、
`tina_platform`、`tina_task`、`tina_render`、`tina_runtime`、直接 GoogleTest 门禁与 Null 样例：

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug --target tina_tests tina_sample_null
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_sample_null.exe --frames=300
out\build\windows-msvc-vnext\bin\Debug\tina_sample_null.exe --frames=10000
```

当前最小图的唯一第三方测试依赖是 GoogleTest 1.17.0；它不进入或链接 GLFW、bgfx、EASTL、
EnTT、FreeType、miniaudio、Box2D、xxHash 或 SDL/SDL3。`tina_sample_null` 只组合 Headless Platform、
Disabled TaskSystem 与 NullRenderDevice。

## Windows vNext Release

vNext 已提供独立 Release build preset，仍复用同一个 Visual Studio 多配置构建目录：
同一 `windows-msvc-vnext` build tree 的 Debug/Release `cmake --build` 必须串行执行，不能由两个
MSBuild 进程并发驱动同一生成图；配置输出目录虽然隔离，共享的生成状态仍可能发生争用。

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-release --target tina_tests tina_sample_null
out\build\windows-msvc-vnext\bin\Release\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Release\tina_sample_null.exe --frames=300
out\build\windows-msvc-vnext\bin\Release\tina_sample_null.exe --frames=10000
```

Legacy Release 仍可使用原有多配置构建目录：

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

只做 vNext 无 GPU 的编译、链接和单元测试门禁时，使用独立 `linux-gcc13-vnext` 目录，避免污染 Legacy 可运行构建：

```bash
cmake --preset linux-gcc13-vnext
cmake --build --preset linux-gcc13-vnext-debug --target tina_tests
 ./out/build/linux-gcc13-vnext/bin/tina_tests --gtest_color=no
```

Clang 22 + libstdc++15 的普通与 Sanitizer 门禁使用独立目录。主机需同时提供 `clang-22`、
`clang++-22` 与 `g++-15`；chainload toolchain 会验证 major version 并将 Clang 明确绑定到 GCC 15
安装目录：

```bash
cmake --preset linux-clang22-vnext
cmake --build --preset linux-clang22-vnext-debug --target tina_tests tina_sample_null
./out/build/linux-clang22-vnext/bin/tina_tests --gtest_color=no

cmake --preset linux-clang22-vnext-sanitize
cmake --build --preset linux-clang22-vnext-sanitize-debug --target tina_tests tina_sample_null
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
./out/build/linux-clang22-vnext-sanitize/bin/tina_tests --gtest_color=no
```

这些输出不包含 Legacy 产品、窗口、真实渲染后端或 cooked shader，只用于 Headless 生命周期验证，
不能作为游戏产品或发布包。GCC 13.4 与 Clang 22.1.8 + libstdc++15.2 的隔离门禁已经通过，
但仍不能用 Ubuntu 22.04 的旧工具链降级冒充正式结果。

## vNext 目标 Preset

`windows-msvc-vnext`、`linux-gcc13-vnext`、`linux-clang22-vnext` 与
`linux-clang22-vnext-sanitize` 已落地。下列性能名称仍是后续设计契约：

| Preset | 优化/插桩 | 用途 |
| --- | --- | --- |
| `windows-bench` / `linux-bench` | Release、Trace none、VSync off、固定 worker | 正式 `tina_bench` |
| `windows-profile` / `linux-profile` | 与 Bench 相同优化/CRT/assert/LTO、保留符号、Tracy on | 定位同 workload 回退 |

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
| `TINA_BUILD_LEGACY` | `ON` | 迁移期构建现有游戏与旧模块；vNext preset 固定关闭 |
| `TINA_BUILD_RENDER_BGFX` | `OFF` | 后续启用私有 vNext bgfx backend，不改变 Game SDK 边界 |
| `TINA_BUILD_BENCHMARKS` | `OFF` | 后续构建独立 `tina_bench` |
| `TINA_ENABLE_SANITIZERS` | `OFF` | GCC/Clang Unix target 同时启用 ASan/UBSan；其他工具链配置时报错 |
| `TINA_BUILD_WAYLAND` | `OFF` | Linux Wayland 构建，需要对应 vcpkg feature |
| `TINA_AUTOUPDATE_SUBMODULE` | `OFF` | 是否自动更新源码依赖，日常构建应保持关闭 |
| `TINA_BUILD_DOCS` | `ON` | 当前为预留选项，尚未注册文档生成 target |

测试直接运行 GoogleTest 生成的 `tina_tests`。

vNext 后续还将增加 `TINA_PROFILE_BACKEND` 和 Tracy lock/memory 子选项；在真正加入 CMake 前只记录为目标，不能把表述当成当前可用命令。
完整依赖可见性、版本和许可证门禁见 [第三方依赖与版本治理](dependencies.md)。
