# 测试与验证

Tina 使用 GoogleTest 1.17.0。CMake 生成多个独立 executable，构建后逐个直接运行；项目不注册
CTest 测试。测试进程任一返回非0即失败。

## 基本规则

1. 先构建受影响 target，再直接运行对应 executable。
2. Windows 多配置输出使用 `bin/Debug` 或 `bin/Release`，不能混用运行时 DLL。
3. 同一 Visual Studio build tree 的 Debug/Release 构建串行执行。
4. 日常门禁不使用 `--clean-first`，不删除 `out/build`。
5. 测试数量是易变证据；架构状态不以固定数量定义。
6. sample exit 0 只证明生命周期/结构化断言；画面正确必须另有 Visual 证据。
7. sanitizer、真实 backend、字体和 accessibility 结果不能由 Null 单元测试替代。

## 测试 target 拓扑

| Executable | 主要范围 | 可用条件 |
| --- | --- | --- |
| `tina_tests` | Core、Platform contract、Task、Runtime、NullRender、Input/Action、header isolation | 基础图 |
| `tina_ui_tests` | UI tree/layout/hit/route/paint/semantics、Widget、文本/Glyph | 基础图 |
| `tina_runtime_ui_tests` | Runtime UI owner/capability/route/layout/display handoff | 基础图 |
| `tina_ui_render_integration_tests` | committed UI paint → Render DisplayList | 基础图 |
| `tina_scene_tests` | Entity/Transform/2D/3D component 与 extraction | 基础图 |
| `tina_render_scene_tests` | Camera2D/3D、culling、sort/batch、world picking | 基础图 |
| `tina_asset_format_tests` | Cooked/Manifest 与 typed payload schema | 基础图 |
| `tina_asset_tests` | Catalog、AssetSystem、Handle/Lease、Cooker、upload/retirement | 基础图 |
| `tina_audio_tests` | backend-neutral AudioEngine/voice/bus/command/completion | 基础图 |
| `tina_platform_glfw_tests` | GLFW adapter 与 WindowSurface | `TINA_BUILD_PLATFORM_GLFW=ON` |
| `tina_render_bgfx_tests` | bgfx lifecycle、2D/3D/UI geometry/resource | `TINA_BUILD_RENDER_BGFX=ON` |
| `tina_ui_freetype_tests` | FreeType font open/measure/rasterize | `TINA_BUILD_UI_FREETYPE=ON` |
| `tina_physics2d_tests` | Box2D lifecycle/contact/query/deferred command/grid bridge | `TINA_BUILD_PHYSICS2D=ON` |
| `tina_audio_miniaudio_tests` | miniaudio null-device、decode/mix adapter | `TINA_BUILD_AUDIO_MINIAUDIO=ON` |

## 基础 Windows 门禁

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug `
  --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests `
           tina_scene_tests tina_render_scene_tests tina_asset_format_tests tina_asset_tests `
           tina_audio_tests tina_sample_null -- /m:2 /v:m

out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_ui_render_integration_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_render_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_asset_format_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_asset_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_audio_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_sample_null.exe --frames=300
```

当前工作树已增量构建并直接验证 `tina_ui_tests` 190/190、`tina_runtime_ui_tests` 77/77 与
`tina_ui_render_integration_tests` 12/12。其他 target 必须按最终验证命令重新运行后才能记录本轮数字。

## 改动到门禁映射

| 改动范围 | 最小测试 | 追加 smoke/平台 |
| --- | --- | --- |
| Core/Result/Time/Memory | `tina_tests` | Null 300帧；Linux sanitizer |
| Platform/Input/WindowSurface | `tina_tests`、`tina_platform_glfw_tests` | `tina_sample_platform`，X11/Wayland |
| Task/关闭顺序 | `tina_tests` | Null/Desktop 300帧，失败注入 |
| Runtime phase/state | `tina_tests`、`tina_runtime_ui_tests` | Null、2D、3D products |
| UI/Widget/Text | `tina_ui_tests`、`tina_runtime_ui_tests`、bridge | FreeType、product-2d、截图 |
| RenderScene/Scene | `tina_render_scene_tests`、`tina_scene_tests` | extraction samples、2D/3D products |
| bgfx backend | `tina_render_bgfx_tests` | Desktop/2D/3D GPU samples + Visual |
| Asset format/Cooker | `tina_asset_format_tests`、`tina_asset_tests` | `assetc`→validate→sample、3D product |
| Audio | `tina_audio_tests` | miniaudio tests、product-2d |
| Physics2D | `tina_physics2d_tests` | Release bench、product-2d |
| CMake/preset/dependency | 所有受影响 configure 图 | 最小 executable + product smoke |

公共 API 变化还必须编译 header-isolation/consumer 测试，并扫描公开头是否出现第三方 token。

## 产品样例的证据边界

| Sample | 证明 | 不证明 |
| --- | --- | --- |
| `tina_sample_null` | EngineHost、固定帧、Headless/Null lifecycle | GLFW、GPU、可见 UI |
| `tina_sample_platform` | GLFW window/input/WindowSurface + NullRender | bgfx 绘制 |
| `tina_sample_desktop` | Desktop bootstrap、真实 bgfx surface、UI pass | 2D/3D 产品内容 |
| `tina_sample_asset` | Catalog→Task→AssetSystem→ReadyGpu/Lease | 可见纹理/mesh |
| `tina_sample_2d_infrastructure` | CPU/Null Camera2D/Sprite extraction | Catalog/产品 UI/GPU |
| `tina_sample_2d_infrastructure_bgfx` | fixture Sprite2D + UI overlay | 正式 Catalog TileMap 产品 |
| `tina_sample_2d` | Catalog TileMap、Gameplay、UI、Audio；feature 图含 Physics/FreeType/miniaudio | Linux、跨 GPU golden、完整编辑器/UI 工具包 |
| `tina_sample_3d_extraction` | CPU/Null Perspective/Mesh extraction | 可见 GPU 3D |
| `tina_sample_3d_infrastructure` | procedural fixture Cube/depth/instance | Cooked product mesh |
| `tina_sample_3d` | 双 mesh glTF→Cooked→GPU→Prefab→Scene→bgfx；material texture **bind API** | Opaque3D 贴图采样画面、PBR、Handle/Lease→fence pin |

`tina_sample_2d_tilemap_bgfx` 是 `tina_sample_2d` 的兼容 ALIAS；新脚本使用正式 target 名。

## Asset/Cooker E2E

```powershell
cmake --build --preset windows-vnext-debug `
  --target tina_asset_format_tests tina_asset_tests tina_assetc tina_catalog_validate tina_sample_asset -- /m:2 /v:m
out\build\windows-msvc-vnext\bin\Debug\tina_asset_format_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_asset_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_assetc.exe --out <catalogRoot> --recipe <recipe>
out\build\windows-msvc-vnext\bin\Debug\tina_catalog_validate.exe --root <catalogRoot> --typed-payloads
out\build\windows-msvc-vnext\bin\Debug\tina_sample_asset.exe --frames=60 --catalog=<catalogRoot>
```

multi-mesh glTF Cooker 的库级测试与 `tina_sample_3d` 双 mesh 产品 E2E（3D-001）均已完成：distinct
mesh/material AssetId、Prefab dependency 与 product meshKey 1/2 binding 可验证。Opaque3D baseColor
贴图 **采样** 与 PBR 仍后置，不得把 material texture bind API 写成“画面已贴图”。

## UI 与视觉

UI 逻辑门禁至少包括：

- generation/root ownership、容量失败与 PMR 回收；
- layout/hit/paint/semantics 的事务提交；
- routed input、default action、consume/claim、reset/cancel；
- Button/Checkbox/Slider/ProgressBar/RadioButton/TextEdit 的 kind/property/错误路径；
- UTF-8、IME preedit/commit、Glyph atlas 与 FreeType adapter；
- Runtime phase facade 过期、sticky error 与跨 root 拒绝。

Visual 证据必须同时记录 sample 返回码、client-area 尺寸、是否强制终止、blank/black 比例、字体来源和
截图。初始化白帧不得作为稳定画面；截图通过也不能替代 UIA/AT-SPI。

## Physics2D 与 Audio

完整 product-2d 图直接运行：

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_physics2d_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests `
           tina_ui_freetype_tests tina_audio_tests tina_audio_miniaudio_tests tina_sample_2d -- /m:1 /v:m
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_render_integration_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_freetype_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_physics2d_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_audio_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_audio_miniaudio_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
```

miniaudio null-device 证明 adapter callback/mix/lifecycle，不证明真实扬声器质量。Physics2D Release bench
是模块基线。统一 schema 使用 `tina_bench`（ADR 0018 schema v1；共享机仅 provisional）。

Windows 同轮 product-2d 拓扑由 `tools/windows/RunProduct2dGate.ps1` 固化（TEST-002）：上表测试 executable
全部 exit 0 后，再跑 sample 300 帧并校验 `productGate=bgfx-physics-freetype-audio`。

文档扫描（DOC-002）：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\docs\CheckDocs.ps1
```

UI-003 单机视觉 ROI 门禁（映射单测之外的截图证据；排除 PrintWindow 白帧；可选 baseline 比对）：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunUi003VisualGate.ps1 `
  -SkipBuild -OutDir artifacts\screenshots\ui-003-visual
# 写入/更新本机金标（同机回归）：
#   ... -WriteBaseline
# 默认读取 tools/windows/baselines/ui-003-sample2d-960x540.json

# 逻辑 / content-scale-like 尺寸矩阵（非 OS Settings DPI；sample --width/--height）
# 含 960×540 / 1200×675 / 1440×810 / 1280×720 / 1920×1080；按尺寸 ROI baseline
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunUi003SizeMatrix.ps1 -SkipBuild
# 首次/刷新各尺寸 baseline：
#   ... -WriteBaselines
```

**已证明：** ContentScale* 映射单测；单机 ROI + blankLike 排除；设计 960×540 absolute 布局 baseline；
逻辑窗口 content-scale-like 矩阵；sample JSON `logicalPixel*` / `framebufferPixel*` / `contentScale*`
一致性（GLFW metrics，非 COM DPI API）。

**未证明：** OS 显示缩放 100/150/200% 真机多 DPI 金标；多显示器混 DPI；跨 GPU/字体 fingerprint 金标。

## Linux 与 sanitizer

Linux 门禁必须记录 compiler、stdlib、CMake、vcpkg baseline、display backend 和 sanitizer 环境。
Clang preset 通过 chainload 固定 libstdc++15；Ubuntu 默认旧工具链不能冒充正式结果。

### Docker Desktop（Windows 宿主）— GCC13 Null 子图

见 [m12-evidence-linux.md](m12-evidence-linux.md)。快捷：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxGcc13NullGate.ps1 `
  -OutJson artifacts\gates\test-001-linux-gcc13-null.json
```

2026-07-23 tip `e0d94faa`：GCC13 Null exit 0。  
2026-07-24 tip `d883d787`：GCC13 Platform/GLFW + Xvfb exit 0（34/34）。  
2026-07-24 tip `66374135`：Clang22 Null + Clang22 sanitizer Null 全 executable exit 0。  
详见 [m12-evidence-linux.md](m12-evidence-linux.md)；TEST-001 主验收已关。

### 本机 Linux / Clang sanitizer

```bash
cmake --preset linux-clang22-vnext-sanitize
cmake --build --preset linux-clang22-vnext-sanitize-debug \
  --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests tina_sample_null

ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
LSAN_OPTIONS=exitcode=23 \
./out/build/linux-clang22-vnext-sanitize/bin/tina_tests --gtest_color=no
```

其余 executable 逐个运行。只有 GLFW/X11 进程允许使用已记录的 `_XimOpenIM` 精确 suppression；基础、
Asset、UI、RenderScene 测试不得继承宽泛 suppression。

## 证据记录模板

每次正式门禁至少记录：

```text
commit/worktree: <sha + dirty files if any>
date/platform/toolchain: <...>
preset/configuration: <...>
build command + exit code: <...>
test/sample command + exit code: <...>
test summary / structured JSON: <...>
visual/sanitizer evidence: <not run | path/result>
known limitations: <...>
```

测试日志不得包含 token、凭据、用户名或不必要的绝对路径。
