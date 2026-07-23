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

## Windows Null 基础图

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug `
  --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests `
           tina_scene_tests tina_render_scene_tests tina_asset_format_tests tina_asset_tests `
           tina_audio_tests `
           tina_sample_null tina_sample_asset tina_sample_2d_infrastructure tina_sample_2d_tilemap `
           tina_sample_3d_extraction tina_assetc tina_catalog_validate -- /m:2 /v:m
```

多配置输出位于 `out/build/windows-msvc-vnext/bin/Debug` 或 `bin/Release`。对应 executable 必须使用
同一配置目录的 DLL；Debug/Release 不得混跑。

## Windows Platform/Desktop/bgfx

```powershell
cmake --preset windows-msvc-vnext-platform
cmake --build --preset windows-vnext-platform-debug `
  --target tina_tests tina_platform_glfw_tests tina_sample_platform -- /m:2 /v:m
out\build\windows-msvc-vnext-platform\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-platform\bin\Debug\tina_platform_glfw_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-platform\bin\Debug\tina_sample_platform.exe --frames=300 --frame-delay-ms=0

cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_tests tina_runtime_ui_tests tina_platform_glfw_tests tina_render_bgfx_tests `
           tina_sample_desktop tina_sample_2d_infrastructure_bgfx tina_sample_3d_infrastructure `
           tina_sample_3d tina_sample_2d -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_bgfx_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_desktop.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d_infrastructure_bgfx.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d_infrastructure.exe --frames=300 --frame-delay-ms=0
```

GLFW/GLFW sample 在 Linux X11/Wayland 图中应通过 `xvfb-run` 或受控 compositor 运行；X11 的
`_XimOpenIM` suppression 只能用于初始化 GLFW 的进程，不能扩大为 Tina 全局 suppression。

## 完整 product-2d 图

```powershell
cmake --preset windows-msvc-vnext-bgfx-product-2d
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_sample_2d tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests `
           tina_ui_freetype_tests tina_physics2d_tests tina_audio_tests tina_audio_miniaudio_tests `
           tina_asset_tests -- /m:2 /v:m
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
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
```

完整图的结构化标签应为 `productGate=bgfx-physics-freetype-audio`。

一键复现（configure 可跳过）：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunProduct2dGate.ps1
```

## Asset CLI

先构建工具，再运行工具；工具不读取未构建的旧 binary：

```powershell
cmake --build --preset windows-vnext-debug --target tina_assetc tina_catalog_validate -- /m:2 /v:m
out\build\windows-msvc-vnext\bin\Debug\tina_assetc.exe --out <catalogRoot>
out\build\windows-msvc-vnext\bin\Debug\tina_catalog_validate.exe --root <catalogRoot> --typed-payloads
out\build\windows-msvc-vnext\bin\Debug\tina_sample_asset.exe --frames=60 --catalog=<catalogRoot>
```

成功为 exit 0；验证失败为 exit 1；参数错误为 exit 2。Catalog manifest 相对路径和派生 object path 已校验
UTF-8，并拒绝绝对路径与 `..` 逃逸。glTF Cooker 已能读取 relative-file/bufferView baseColorTexture，
但 relative URI 的 root containment、规范化与 symlink 逃逸策略尚未闭合；当前只应处理可信源资产，完整
安全策略由 [ASSET-001](backlog.md) 跟踪。

## Linux Null 与 sanitizer

```bash
cmake --preset linux-gcc13-vnext
cmake --build --preset linux-gcc13-vnext-debug \
  --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests tina_sample_null
./out/build/linux-gcc13-vnext/bin/tina_tests --gtest_color=no
./out/build/linux-gcc13-vnext/bin/tina_ui_tests --gtest_color=no
./out/build/linux-gcc13-vnext/bin/tina_runtime_ui_tests --gtest_color=no
./out/build/linux-gcc13-vnext/bin/tina_ui_render_integration_tests --gtest_color=no
./out/build/linux-gcc13-vnext/bin/tina_sample_null --frames=300

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
| `TINA_BUILD_BENCHMARKS` | OFF | 规划中的 benchmark targets |
| `TINA_UI_FONT_PATH` | 空 | 可选字体文件 |
