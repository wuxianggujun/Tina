# M12 Windows 证据摘录

本文保存带日期的本机结果，不定义永久基线，也不授权删除当前模块。生成内容和 fingerprint 会随源码
变化；复验时以当前命令返回码与结构化输出为准。

## 环境

- 日期：2026-07-22；
- 工具链：Windows 11、Visual Studio 2026 / MSVC 19.50、CMake 4.2.3；
- build tree：`out/build/windows-msvc-vnext*`，未使用 `--clean-first`；
- 分支/提交：使用 `git rev-parse HEAD` 随证据记录，不把旧 tip 当作当前状态。

## 2D 产品

```powershell
cmake --build --preset windows-vnext-bgfx-debug --target tina_sample_2d -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
```

记录结果：exit 0。关键字段包括：

- `status=ok`、`frames=300`、`renderExtractions=300`；
- `catalogFromRecipeFile=true`、`catalogRecipeAssets=4`；
- `tileSpritesPerFrame=11`、角色撞墙与 selection/highlight 计数；
- `audioEnginePresent=true`、`audioOneShotQueued=true`、`audioStartedObserved=true`；
- `audioFromCatalogLease=true`、`pixelCaptureOk=true`；
- `stateExits=1`、`uiRootsReleased=1`。

基础 bgfx 图的标签为 `productGate=bgfx`。30 帧不足以满足行走/撞墙阈值，正式 2D 门禁使用300帧。

## 完整 2D feature 图（TEST-002）

同轮门禁脚本：`tools/windows/RunProduct2dGate.ps1`（configure + 全模块 target 构建 + 直接 GoogleTest
+ sample 300 帧；标签必须为 `productGate=bgfx-physics-freetype-audio`）。

```powershell
cmake --preset windows-msvc-vnext-bgfx-product-2d
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_sample_2d tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests `
           tina_ui_freetype_tests tina_physics2d_tests tina_audio_tests tina_audio_miniaudio_tests `
           tina_asset_tests -- /m:2 /v:m
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunProduct2dGate.ps1 -SkipConfigure -SkipBuild
```

2026-07-23 tip `eea065ad`（工作树含 3D-001/TASK-001/CLEAN 未提交改动时同轮复验）Windows product-2d：

| 步骤 | 结果 |
| --- | --- |
| configure `windows-msvc-vnext-bgfx-product-2d` | OK |
| 上表全部 target 构建 | OK |
| `tina_ui_tests` | exit 0（listed 202） |
| `tina_runtime_ui_tests` | exit 0（listed 78） |
| `tina_ui_render_integration_tests` | exit 0（listed 12） |
| `tina_ui_freetype_tests` | exit 0（listed 2） |
| `tina_physics2d_tests` | exit 0（listed 26） |
| `tina_audio_tests` | exit 0（listed 15） |
| `tina_audio_miniaudio_tests` | exit 0（listed 9） |
| `tina_asset_tests` | exit 0（listed 110） |
| `tina_sample_2d --frames=300` | exit 0 |

sample 关键字段：`productGate=bgfx-physics-freetype-audio`、`physicsEnabled=true`、
`freetypeEnabled=true`、`audioMiniaudioEnabled=true`、`audioDeviceNullBackend=true`、
`physicsSteps=300`、`uiProgressBarValueVerified=true`、`uiRadioSelectionVerified=true`、
`pixelCaptureOk=true`、`evidenceSchema=3`。listed 数量随工作树变化，不是永久基线。

## UI

当前工作树在基础 Windows Debug 图增量构建并直接运行：

```powershell
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_ui_tests.exe --gtest_color=no
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=no
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_ui_render_integration_tests.exe --gtest_color=no
```

结果：`tina_ui_tests` 190/190、`tina_runtime_ui_tests` 77/77、
`tina_ui_render_integration_tests` 12/12。覆盖 ProgressBar、RadioButton、对应 Runtime facade、
TextEdit/Checkbox/Slider/focus dirty 容量原子性与 committed UI paint → Render DisplayList bridge。

2026-07-23 的 product-2d client-area 报告位于
`artifacts/screenshots/sample-2d-product/20260723-013100/report.json`。报告 `ok=true`、exit 0，
`productGate=bgfx-physics-freetype-audio`、`evidenceSchema=3`；结构化输出确认1个 ProgressBar 的值、
2个 RadioButton 的 action 与互斥选择。3次 960x540 捕获中2帧稳定非空，初始化白帧由
`blankLike=true` 排除；人工复核 `frame-02.png` / `frame-03.png` 中 TextEdit、65% ProgressBar 与
Windowed/Fullscreen RadioButton 可见，中文无乱码，控件无裁剪或重叠。两帧 fill 均为143 px，
且选中色只出现在 Windowed RadioButton。

## 3D 产品与 Cooker

```powershell
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
```

记录结果：exit 0，JSON 含 `gltfCooked`、`multiMesh=true`、`meshesUploaded=2`、
`prefabInstantiated`、`sceneExtract` 和 `renderResourceLedgerBalanced`（3D-001 双 mesh 产品路径）。

## Audio

product-2d 同轮图证明 `tina_audio_tests` / `tina_audio_miniaudio_tests` exit 0，以及 sample 内
miniaudio null-device callback 与 Catalog AudioClip lease。真实扬声器质量、Linux tip 与 callback
性能不由这份证据证明。

## 未关闭

- 当前 tip Linux GCC/Clang/sanitizer（TEST-001）；
- Cooked texture 产品绑定与安全 URI/size policy（ASSET-001）、PBR、跨 GPU/DPI golden、UIA/AT-SPI。
