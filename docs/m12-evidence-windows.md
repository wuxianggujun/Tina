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

2026-07-28 N16.4 复验把 product evidence 升级到 schema 4：300 帧 exit 0，
`meshFrameResourceResolverHits=600`、`materialFrameResourceResolverHits=600`；complete-PBR fixture 的两个
Material 共享3个 Texture owner，因此 `texturesUploaded=3`、`textureRetirementsAccepted=3`，同时
`meshesUploaded=2`、`meshRetirementsAccepted=2`。Mesh/Material/Texture weak handle invalidation 分别为
2/2/3，5条 GPU retirement record 全部 `Released`、live=0，`bindingRegistryReleased=true`、
`renderResourceLedgerBalanced=true`、`pixelCaptureOk=true`。完整 bgfx + FreeType TEST-003 报告记录 fingerprint
`d0e4d69b60869f5669d530d3e5a556fa`；另以该值调用 `--expect-pixel-fingerprint` 完成一次本机精确重放。
JSON 报告对应的标准 gate 本身未传 expect 参数，因此 `pixelGoldenChecked=false`；fingerprint 与独立重放仅作
本机/backend 证据，不跨 GPU 作为金标。同轮 TEST-003 由 `tools/windows/RunProduct3dGate.ps1` 固化。

### N16.4 完整 3D feature 图（TEST-003）

2026-07-29 基线 `e61b6a00` + N16.4 工作树使用既有
`windows-msvc-vnext-bgfx-ui-freetype` configure tree 正式复验：

| 步骤 | 结果 |
| --- | --- |
| configure | 复用既有 tree（`-SkipConfigure`） |
| 脚本全部 target 构建 | OK |
| `tina_tests` | exit 0（listed 335） |
| `tina_scene_tests` | exit 0（listed 91） |
| `tina_asset_format_tests` | exit 0（listed 59） |
| `tina_asset_tests` | exit 0（listed 204） |
| `tina_render_scene_tests` | exit 0（listed 39） |
| `tina_render_bgfx_tests` | exit 0（listed 61） |
| `tina_ui_tests` | exit 0（listed 282） |
| `tina_runtime_ui_tests` | exit 0（listed 85） |
| `tina_ui_render_integration_tests` | exit 0（listed 15） |
| `tina_ui_freetype_tests` | exit 0（listed 3） |
| `tina_sample_3d --frames=300` | exit 0 |
| `productEvidence` | exit 0（schema 4） |

无重建复验写出 `artifacts/gates/product-3d.json`；报告 `ok=true`，结构化字段与上节一致。

## Audio

product-2d 同轮图证明 `tina_audio_tests` / `tina_audio_miniaudio_tests` exit 0，以及 sample 内
miniaudio null-device callback 与 Catalog AudioClip lease。真实扬声器质量、Linux tip 与 callback
性能不由这份证据证明。

## UI-003 单机视觉 ROI（2026-07-24 tip `66374135`）

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunUi003VisualGate.ps1 `
  -SkipBuild -OutDir artifacts\screenshots\ui-003-visual
```

结果：`ok=true`，client **960×540**，aspect 1.7778；CaptureSampleWindow 排除 `blankLike` 白帧；
ROI 指纹覆盖 title/settings/progress/playfield；`accessibilityPublished=true`。
仓库内 baseline：`tools/windows/baselines/ui-003-sample2d-960x540.json`（avgRgb 容差默认 28）。
二次运行 `baselineCompare.matched=true`。摘要示例：`artifacts/gates/ui-003-visual-*.json`。

逻辑 / content-scale-like 尺寸矩阵（`--width/--height`，**非** OS Settings DPI）：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunUi003SizeMatrix.ps1 -SkipBuild
# 写入分尺寸 baseline：
# powershell -ExecutionPolicy Bypass -File .\tools\windows\RunUi003SizeMatrix.ps1 -SkipBuild -WriteBaselines
```

cases（16:9，模拟 content-scale 客户端足迹，产品 absolute UI 仍锁 960×540 设计坐标）：

| label | logical | baseline |
| --- | --- | --- |
| design-1x | 960×540 | `tools/windows/baselines/ui-003-sample2d-960x540.json` |
| scale-like-1.25x | 1200×675 | `...-1200x675.json` |
| scale-like-1.5x | 1440×810 | `...-1440x810.json` |
| desktop-720p | 1280×720 | `...-1280x720.json` |
| scale-like-2x | 1920×1080 | `...-1920x1080.json` |

gate 另解析 sample JSON：`logicalPixel*` / `framebufferPixel*` / `contentScale*`，断言
`framebuffer ≈ logical * contentScale`（GLFW metrics，无需 COM/DPI API）。
blankLike 仍由 `CaptureSampleWindow` 排除。摘要：`artifacts/gates/ui-003-size-matrix-*.json`。

### UI-003 已证明 vs 仍开放

| 已证明（可自动化） | 仍开放 |
| --- | --- |
| `ContentScale*` 映射单测（logical→fb 100/150/200%） | OS 显示缩放 100/150/200% 真机多 DPI 金标 |
| 单机 ROI + design-1x baseline（960×540 absolute） | 多显示器混 DPI golden 矩阵 |
| content-scale-like 逻辑窗口矩阵 + 分尺寸 baseline | 跨 GPU 像素 golden |
| sample JSON contentScale/logical/fb 一致性 | |
| 字体 identity fingerprint（path/sha256/env/FreeType-on；baseline schema 3；mismatch fail） | |

## 未关闭

- OS 级多 DPI 截图矩阵与跨 GPU 像素金标（UI-003 尾巴；字体 identity fingerprint 已进 gate/baseline）、UIA/AT-SPI 真机（UI-002）、PBR（RENDER-001）；
- Linux 可选 Wayland / 真显示器（TEST-001 主验收已关，见 [Linux 证据](m12-evidence-linux.md)）。
