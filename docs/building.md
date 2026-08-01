# 构建与运行

本文只记录当前可执行命令。测试数量和一次性机器结果见 [testing.md](testing.md) 与
[M12 Windows 证据](m12-evidence-windows.md)。所有 Windows Debug/Release 构建在同一 build tree
内串行执行；日常验证禁止 `--clean-first` 和删除 `out/build`。

## 环境

| 平台 | 当前支持基线 | 备注 |
| --- | --- | --- |
| Windows | Visual Studio 2026 / MSVC 19.50、CMake 4.2.3、`VCPKG_ROOT` | MSVC 全局 `/utf-8`、`/Zc:__cplusplus` |
| Linux GCC | GCC 13+、Ninja、CMake 3.25+ | `linux-gcc13-vnext*` |
| Linux Clang | Clang 22.x + GCC/libstdc++ 15.x | chainload toolchain；sanitizer 使用独立 preset |

项目不使用 C++ Modules；CMake 显式关闭 Modules dependency scan。依赖由 vcpkg manifest/baseline
管理，bgfx 源码由 `thirdparty/bgfx.cmake` 锁定。`TINA_BUILD_LEGACY=ON` 会立即失败。

```powershell
cmake --version
cmake --list-presets
```

文档本地链接、preset 名与常见 target 名可用：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\docs\CheckDocs.ps1
```

（DOC-002；不扫描 `out/`/`build/`/`thirdparty/`。）

## Preset 选择

| Preset | 图 | Manifest features |
| --- | --- | --- |
| `windows-msvc-vnext` | Null/Core/Asset/Scene/UI 基础图 | `tests` |
| `windows-msvc-vnext-platform` | GLFW + NullRender | `tests;platform-glfw` |
| `windows-msvc-vnext-bgfx` | GLFW + bgfx + Desktop/产品样例 | `tests;platform-glfw`（默认 preset 继承） |
| `windows-msvc-vnext-physics2d` | Null + Box2D | `tests;physics2d` |
| `windows-msvc-vnext-bgfx-physics2d` | bgfx + Box2D | `tests;platform-glfw;physics2d` |
| `windows-msvc-vnext-ui-freetype` | Null + FreeType UI | `tests;ui-freetype` |
| `windows-msvc-vnext-bgfx-ui-freetype` | bgfx + FreeType UI | `tests;platform-glfw;ui-freetype` |
| `windows-msvc-vnext-audio-miniaudio` | Null + miniaudio adapter | `tests;audio-miniaudio` |
| `windows-msvc-vnext-audio-miniaudio-codecs` | miniaudio + Vorbis/Opus | 对应 codec features |
| `windows-msvc-vnext-bgfx-product-2d` | bgfx + Physics2D + FreeType + miniaudio | `tests;platform-glfw;physics2d;ui-freetype;audio-miniaudio` |
| `linux-gcc13-vnext` | Linux Null | `tests` |
| `linux-gcc13-vnext-platform` | Linux GLFW/X11 | `tests;platform-glfw` |
| `linux-gcc13-vnext-platform-wayland` | Linux GLFW/Wayland | `tests;platform-glfw;wayland` |
| `linux-clang22-vnext` | Clang Null | `tests` |
| `linux-clang22-vnext-sanitize` | Clang ASan/UBSan/LSan | `tests` |
| `linux-clang22-vnext-platform*` | Clang GLFW/X11/Wayland + sanitizer 变体 | 对应 platform features |

`TINA_BUILD_UI_FREETYPE`、`TINA_BUILD_AUDIO_MINIAUDIO`、`TINA_BUILD_PHYSICS2D`、
`TINA_BUILD_PLATFORM_GLFW` 和 `TINA_BUILD_RENDER_BGFX` 必须与相应 manifest feature 同时启用；
CMake 会拒绝不匹配组合。

## Windows 日常 UI 增量构建

日常 UI 开发优先使用固定脚本，不手写 MSBuild native 参数：

```powershell
# 自动复用已有 build tree；缺少 CMakeCache.txt 时才 configure。
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunMsvcUiTests.ps1

# 只跑本次相关用例；仍会先增量构建 tina_ui_tests。
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunMsvcUiTests.ps1 `
  -GTestFilter 'UITextPaintEmitterTests.*'
```

脚本固定使用 MSVC preset、CMake `--parallel 2` 和 MSBuild `/nr:false`，构建结束后验证本轮新建的
`cmake/MSBuild/cl/link` 进程已退出；不执行 `--clean-first`。切换 build graph 时同时传
`-ConfigurePreset`、`-BuildPreset`、`-BuildTree`，切换输出配置时传 `-Configuration Release`。

必须手工构建时，统一使用以下参数顺序：

```powershell
cmake --build --preset windows-vnext-bgfx-debug `
  --parallel 2 --target tina_ui_tests -- /nr:false
```

并发度由 CMake `--parallel` 管理；不要再向 MSBuild 重复传 `/m` 或 `/v`。这可避免 Git Bash/MSYS
对斜杠参数做路径转换，也确保 MSBuild node reuse 不残留编译进程。

## Windows Null 基础图

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug `
  --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests `
           tina_scene_tests tina_render_scene_tests tina_asset_format_tests tina_asset_tests `
           tina_audio_tests `
           tina_sample_null tina_sample_asset tina_sample_2d_infrastructure tina_sample_2d_tilemap `
           tina_sample_3d_extraction tina_assetc tina_catalog_validate --parallel 2 -- /nr:false
```

多配置输出位于 `out/build/windows-msvc-vnext/bin/Debug` 或 `bin/Release`。对应 executable 必须使用
同一配置目录的 DLL；Debug/Release 不得混跑。

## Windows / Linux 安装 SDK consumer

先配置 Null 图，再运行安装 consumer 门禁：

```powershell
cmake --preset windows-msvc-vnext
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkConsumerGate.ps1 `
  -BuildDirectory out\build\windows-msvc-vnext -Configuration Debug
```

脚本以 `--parallel 1` 和 `/nr:false` 增量构建 backend-neutral SDK 闭包，将当前配置安装到 build tree
内的独立 prefix，扫描实际安装头的第三方 include/type token，然后从 `tests/sdk_consumer` 建立独立
build tree。consumer 的 CMake 入口只有 `find_package(Tina CONFIG REQUIRED)` 和 `Tina::GameSDK`，并拒绝
源码树 `include` 路径或安装 prefix 外的 Tina include。Debug/Release package 必须与 consumer 配置一致。

PlatformGlfw 安装 consumer 使用独立 prefix/build tree，不污染 Null gate：

```powershell
cmake --preset windows-msvc-vnext-platform
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkConsumerGate.ps1 -Consumer PlatformGlfw -Configuration Debug
```

consumer 通过 `find_package(Tina CONFIG REQUIRED COMPONENTS PlatformGlfw)`，只链接
`Tina::PlatformGlfw`，创建隐藏窗口、读取初始 metrics、poll 一帧并 shutdown。

Linux GCC13 Null 图使用同一安装头扫描和同一个仓库外 consumer：

```bash
export VCPKG_ROOT=/opt/vcpkg
tools/linux/run-sdk-consumer-gate.sh
tools/linux/run-sdk-platform-glfw-consumer-gate.sh
```

基础脚本默认增量配置 `linux-gcc13-vnext`，PlatformGlfw wrapper 默认使用
`linux-gcc13-vnext-platform` 并通过 `xvfb-run` 执行；可用 `TINA_SDK_CONFIGURE_PRESET`、
`TINA_SDK_BUILD_DIRECTORY`、`TINA_SDK_CONFIGURATION`、`TINA_SDK_BUILD_JOBS` 与
`TINA_SDK_CONSUMER` 覆盖工具链、build tree 和 consumer。
Windows 主机可通过现有 GCC13 Docker 镜像运行相同门禁：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunLinuxDockerGate.ps1 -Gate sdk-consumer
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunLinuxDockerGate.ps1 -Gate sdk-platform-glfw-consumer
```

这些门禁证明 Windows/Linux headless/Null SDK 与独立 PlatformGlfw adapter；不等价于已安装
DesktopBootstrap 或 bgfx/FreeType/miniaudio adapter。package 继续通过 vcpkg toolchain 解析 `xxHash`、
`glfw3` 等声明的外部依赖，不把第三方复制进 Tina SDK。

## Windows Platform/Desktop/bgfx

```powershell
cmake --preset windows-msvc-vnext-platform
cmake --build --preset windows-vnext-platform-debug `
  --target tina_tests tina_platform_glfw_tests tina_sample_platform --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-platform\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-platform\bin\Debug\tina_platform_glfw_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-platform\bin\Debug\tina_sample_platform.exe --frames=300 --frame-delay-ms=0

cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_tests tina_runtime_ui_tests tina_platform_glfw_tests tina_render_bgfx_tests `
           tina_sample_desktop tina_sample_2d_infrastructure_bgfx tina_sample_3d_infrastructure `
           tina_sample_ui_showcase tina_sample_3d tina_sample_2d --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_bgfx_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_desktop.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d_infrastructure_bgfx.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d_infrastructure.exe --frames=300 --frame-delay-ms=0
```

GLFW/GLFW sample 在 Linux X11/Wayland 图中应通过 `xvfb-run` 或受控 compositor 运行；X11 的
`_XimOpenIM` suppression 只能用于初始化 GLFW 的进程，不能扩大为 Tina 全局 suppression。

## UI control showcase

`tina_sample_ui_showcase` 在普通 bgfx 图也可构建，但默认图未启用 FreeType，只能看到文字 placeholder。
用于完整控件、中文、按钮状态层次和 Dark/Light 换肤验收时，使用专用 FreeType 图：

```powershell
cmake --preset windows-msvc-vnext-bgfx-ui-freetype
cmake --build --preset windows-vnext-bgfx-ui-freetype-debug `
  --target tina_sample_ui_showcase tina_ui_tests tina_runtime_ui_tests `
           tina_ui_render_integration_tests tina_ui_freetype_tests --parallel 2 -- /nr:false

out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_sample_ui_showcase.exe
out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_sample_ui_showcase.exe `
  --frames=150 --frame-delay-ms=0 --theme=dark --auto-demo
```

交互模式关闭窗口退出；自动模式输出 JSON 并校验20个控件、两次换肤、Slider→ProgressBar、
Dropdown/List/Tree selection、Tree expansion、ScrollView offset 和 UI root 生命周期。可用
`--theme=light` 改初始主题。字体仍按 CMake cache、环境变量
`TINA_UI_FONT_PATH`、可选 repo fixture 的顺序解析；没有真实字体不得记录为 CJK 视觉通过。

## 完整 product-2d 图

```powershell
cmake --preset windows-msvc-vnext-bgfx-product-2d
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_sample_2d tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests `
           tina_ui_freetype_tests tina_physics2d_tests tina_audio_tests tina_audio_miniaudio_tests `
           tina_asset_tests --parallel 2 -- /nr:false
```

字体可通过 `-DTINA_UI_FONT_PATH=C:/path/font.otf` 或环境变量 `TINA_UI_FONT_PATH` 注入。没有字体时
FreeType test 可以 `GTEST_SKIP`，但不能把 skip 记录为 CJK 视觉通过。

```powershell
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_render_integration_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_freetype_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_physics2d_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_audio_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_audio_miniaudio_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0 --ui-theme-demo --ui-tree-demo
```

完整图的结构化标签应为 `productGate=bgfx-physics-freetype-audio`。

一键复现（configure 可跳过）：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunProduct2dGate.ps1
```

完整 product-3d 同轮门禁：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunProduct3dGate.ps1
```

## Asset CLI

先构建工具，再运行工具；工具不读取未构建的旧 binary：

```powershell
cmake --build --preset windows-vnext-debug --target tina_assetc tina_catalog_validate --parallel 2 -- /nr:false
out\build\windows-msvc-vnext\bin\Debug\tina_assetc.exe --out <catalogRoot>
out\build\windows-msvc-vnext\bin\Debug\tina_catalog_validate.exe --root <catalogRoot> --typed-payloads
out\build\windows-msvc-vnext\bin\Debug\tina_sample_asset.exe --frames=60 --catalog=<catalogRoot>
```

成功为 exit 0；验证失败为 exit 1；参数错误为 exit 2。Catalog manifest 相对路径和派生 object path 已校验
UTF-8，并拒绝绝对路径与 `..` 逃逸。glTF Cooker 把 authoring 输入视为不可信：主路径与 percent-decoded
外部 URI 要求 strict UTF-8 without NUL；主文件及 relative buffer/image 均从一次打开的 handle/fd 快照
读取，外部最终路径必须位于主文件最终 authoring root 下。root 内 symlink/junction 可用，逃逸链接、
读取期间替换、超出 file/count/range/parser/decode/output 预算均结构化失败，不产生可发布的半包。

## Linux Null 与 sanitizer

Windows 宿主可用 Docker Desktop 复现 GCC13 Null / Platform（TEST-001 子图）：

```powershell
# Null
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxDockerGate.ps1 `
  -Gate gcc13-null -OutJson artifacts\gates\test-001-linux-gcc13-null.json

# Platform GLFW/X11 (Xvfb)
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxDockerGate.ps1 `
  -Gate gcc13-platform -OutJson artifacts\gates\test-001-linux-gcc13-platform.json
```

容器内等价命令（`tools/linux/run-gcc13-null-gate.sh`）：

```bash
cmake --preset linux-gcc13-vnext
cmake --build --preset linux-gcc13-vnext-debug \
  --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests tina_sample_null
./out/build/linux-gcc13-vnext/bin/tina_tests --gtest_color=no
./out/build/linux-gcc13-vnext/bin/tina_ui_tests --gtest_color=no
./out/build/linux-gcc13-vnext/bin/tina_runtime_ui_tests --gtest_color=no
./out/build/linux-gcc13-vnext/bin/tina_ui_render_integration_tests --gtest_color=no
./out/build/linux-gcc13-vnext/bin/tina_sample_null --frames=300
```

Clang sanitizer（本机 Linux）：

```bash
cmake --preset linux-clang22-vnext-sanitize
cmake --build --preset linux-clang22-vnext-sanitize-debug \
  --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests tina_sample_null
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
LSAN_OPTIONS=exitcode=23 ./out/build/linux-clang22-vnext-sanitize/bin/tina_tests --gtest_color=no
```

其余 sanitizer executable 应使用相同环境变量逐个运行；不要把一次 configure/build 成功当成测试通过。

## 常用选项

| 选项 | 默认 | 作用 |
| --- | --- | --- |
| `TINA_BUILD_EXAMPLES` | 顶层工程 ON | samples 与 `tina_assetc`/`tina_catalog_validate` |
| `TINA_BUILD_TESTING` | ON | GoogleTest targets |
| `TINA_BUILD_SHADERS` | ON | build-tree shaderc/cooked shader；Null 图可 OFF |
| `TINA_BUILD_LEGACY` | OFF，强制 | 已退役；ON 直接 FATAL |
| `TINA_BUILD_RENDER_BGFX` | OFF | 私有 bgfx backend |
| `TINA_BUILD_PLATFORM_GLFW` | OFF | 私有 GLFW adapter |
| `TINA_BUILD_UI_FREETYPE` | OFF | 私有 FreeType rasterizer |
| `TINA_BUILD_AUDIO_MINIAUDIO` | OFF | 私有 miniaudio adapter |
| `TINA_BUILD_PHYSICS2D` | OFF | 可选 Box2D 模块 |
| `TINA_ENABLE_SANITIZERS` | OFF | Unix GCC/Clang ASan/UBSan |
| `TINA_BUILD_BENCHMARKS` | OFF | 打开时构建 `tina_bench`；examples 开启时也会构建 |
| `TINA_UI_FONT_PATH` | 空 | 可选字体文件 |
