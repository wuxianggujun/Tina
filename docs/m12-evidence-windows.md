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
cmake --build --preset windows-vnext-bgfx-debug --target tina_sample_2d --parallel 2 -- /nr:false
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
           tina_asset_tests --parallel 2 -- /nr:false
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunProduct2dGate.ps1 -SkipConfigure -SkipBuild
```

历史证据（2026-07-23、TreeView 接入前）tip `eea065ad`（工作树含 3D-001/TASK-001/CLEAN
未提交改动时同轮复验）Windows product-2d：

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

2026-07-22 当时的工作树在基础 Windows Debug 图增量构建并直接运行：

```powershell
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_ui_tests.exe --gtest_color=no
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=no
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_ui_render_integration_tests.exe --gtest_color=no
```

结果：`tina_ui_tests` 190/190、`tina_runtime_ui_tests` 77/77、
`tina_ui_render_integration_tests` 12/12。覆盖 ProgressBar、RadioButton、对应 Runtime facade、
TextEdit/Checkbox/Slider/focus dirty 容量原子性与 committed UI paint → Render DisplayList bridge。

同一份 TreeView 接入前历史证据的 product-2d client-area 报告位于
`artifacts/screenshots/sample-2d-product/20260723-013100/report.json`。报告 `ok=true`、exit 0，
`productGate=bgfx-physics-freetype-audio`、`evidenceSchema=3`；结构化输出确认1个 ProgressBar 的值、
2个 RadioButton 的 action 与互斥选择。3次 960x540 捕获中2帧稳定非空，初始化白帧由
`blankLike=true` 排除；人工复核 `frame-02.png` / `frame-03.png` 中 TextEdit、65% ProgressBar 与
Windowed/Fullscreen RadioButton 可见，中文无乱码，控件无裁剪或重叠。两帧 fill 均为143 px，
且选中色只出现在 Windowed RadioButton。

### Retained UI 产品化补证（2026-07-28/29）

2026-07-29 的 product-2d 同轮归档报告位于
`artifacts/reports/product-2d-treeview-gate.json`；configure、build、Scene/UI/Runtime UI/Render bridge/
FreeType/Physics2D/Audio/miniaudio/Asset 直接测试与300帧 sample 均 exit 0。直接运行结果包括
`tina_ui_tests` 313/313、`tina_runtime_ui_tests` 95/95、`tina_scene_tests` 91/91 与
`tina_asset_tests` 193/193；测试数量只描述本次工作树，不是永久基线。

该归档 sample 的 `evidenceSchema=14` 验证 Dark→Light→Dark 与 Scene Explorer TreeView：13个 logical
item、12个 materialized slot、两次 stable-key selection、最终 key `402`/index `12`、滚动、Theme paint
和 Tree/TreeItem selected semantics。当前 sample/gate 已升级到 schema 24：继承 schema 19 的 normal-map
证据与 schema 16 的两盏 committed
`PointLight2D`、两条 `ShadowOccluder2D` 与 `sceneLightingFrames=renderExtractions`，并增加
schema 17 的 `authoredPointLight2DCount=3`、`pointLight2DCount=2`、`culledPointLight2DCount=1` 以及 N4
`softShadowPointLight2DCount=2`；soft/hard 各两次同机 RGBA8 fingerprint 可重复且互异。N5 再加入独立
character normal atlas、`normalMappedSpriteCount=1/0`、15个 recipe assets、`texturesUploaded=3` 与3张
Texture2D 的 owner/retirement，并以 normal on/off 各两次同机差分关闭。schema 21 增加 Layer/Screen
注册、2D Pause Screen push/pop、Back action、单用户输入设备 revision 与 Pause 提示；schema 22 增加
Confirm action、focused-control precedence 与 action/auto-resume intent 字段；schema 23 再增加 Menu action、
P/Start 路由、Base Menu 打开 Pause 与 Pause Menu 恢复字段；schema 24 增加 Navigation2D 的
solid/rectangle/blocked=`11/1/13`、基础/动态路径=`7/9`、单步推进、取消与 revision/mutation 字段。旧报告不反向改写；本节不把尚未
归档的新 gate 运行结果写成既有 Windows 归档证据。
动态 glyph atlas 修复后的 Dark/Light
FreeType client capture 位于
`artifacts/screenshots/2d-scene-explorer-freetype-dark-fixed/20260729-001845/frame-03.png` 与
`artifacts/screenshots/2d-scene-explorer-freetype-light-fixed/20260729-002407/frame-03.png`；对应
`report.json` 均为 `ok=true`、exit 0，Scene Explorer、选中行、设置控件和 playfield 均可见，
`gameplay #30` 与主题按钮的运行时新增字符完整。

独立 showcase 当前验证20个控件、集合导航/滚动、交互层次和 Dark/Light 事务换肤；FreeType client
capture 位于 `artifacts/screenshots/ui-showcase-dark/20260728-155526/frame-03.png` 与
`artifacts/screenshots/ui-showcase-light/20260728-155914/frame-03.png`。2026-07-28 的 product-3d schema 4 验证
7 Panel/13 Label、Asset ListView/Scene TreeView、两次 collection step、最终 stable keys `2003/4` 与
Dark→Light→Dark；对应截图位于
`artifacts/screenshots/3d-product-ui-freetype-dark-fixed/20260729-003922/frame-02.png` 与
`artifacts/screenshots/3d-product-ui-freetype-light-fixed/20260729-004012/frame-01.png`，实际双 mesh 与
动态 `10%`/`1%` 进度同时可见。`tina_render_bgfx_tests` 56/56 通过，其中生产源码合同测试证明 atlas
以 mutable R8 texture 创建、首次/后续 glyph 上传复用同一 handle，且首次上传失败会销毁 texture。

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
blankLike 仍由 `CaptureSampleWindow` 排除。2026-07-29 的独立 compare 报告
`artifacts/screenshots/ui-003-size-matrix/20260729-004341/matrix-report.json` 记录5/5 `ok=true`、
五个 `baselineCompare.matched=true` 和相同字体 fingerprint；摘要为
`artifacts/gates/ui-003-size-matrix-20260729-004341.json`。

### UI-003 已证明 vs 仍开放

| 已证明（可自动化） | 仍开放 |
| --- | --- |
| `ContentScale*` 映射单测（logical→fb 100/150/200%） | OS 显示缩放 100/150/200% 真机多 DPI 金标 |
| 单机 ROI + design-1x baseline（960×540 absolute） | 多显示器混 DPI golden 矩阵 |
| content-scale-like 逻辑窗口矩阵 + 分尺寸 baseline | 跨 GPU 像素 golden |
| sample JSON contentScale/logical/fb 一致性 | |
| 字体 identity fingerprint（path/sha256/env/FreeType-on；baseline schema 3；mismatch fail） | |

## UI-002 可执行门禁（2026-08-03 tip 已固化）

自动跨进程 gate 在 tip `4da7bc03` 已 exit 0，详见
[UI-002 UIA 证据](ui-002-uia-evidence-windows.md) 与
`artifacts/gates/ui-002-uia-20260803-tip.json`（providers=69，fragment 完整，Invoke/Toggle/Range/Value/Focus
均 verified，`WM_CLOSE` 正常退出）。

**Narrator/Inspect 人工金标仍未关闭**（JSON `narratorGold=false`）。操作清单见
[ui-002-narrator-inspect-checklist.md](ui-002-narrator-inspect-checklist.md)。

## ASSET-SEC-001 glTF 输入门禁（2026-07-31）

在 Windows 11、Visual Studio 2026 / MSVC 19.50 的现有
`windows-msvc-vnext-bgfx-product-2d` build tree 增量执行，未使用 `--clean-first`：

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_asset_tests --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_asset_tests.exe `
  --gtest_filter=GltfCookTests.* --gtest_color=no
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_asset_tests.exe --gtest_color=no
```

构建 exit 0；`GltfCookTests` 24/24，完整 `tina_asset_tests` 218/218。Windows contained/escaping
junction fixture 均实际运行，无 skip；覆盖 strict UTF-8/percent-decode、主/外部文件 64MiB、短 buffer、
bufferView/accessor/count/overflow、PNG dimension/decoded bytes、multi-primitive 与完整 PBR 回归。
测试数量只描述本次工作树，不是永久基线。

## 未关闭

- OS 级多 DPI 截图矩阵与跨 GPU 像素金标（UI-003 尾巴；字体 identity fingerprint 已进 gate/baseline）、Narrator/Inspect 人工金标（UI-002）、Linux AT-SPI（UI-002-LINUX）、PBR（RENDER-001）；
- Linux 可选 Wayland / 真显示器（TEST-001 主验收已关，见 [Linux 证据](m12-evidence-linux.md)）。
