# M12 产品退役后门禁

> 状态：**Legacy 产品源码与构建图删除已完成**。本文件只跟踪删除后的产品证据和整库扫尾，
> 不是再次授权删除。当前 `include/tina/ui` + `src/ui` 是 vNext retained UI，必须保留。

证据摘录见 [M12 Windows 证据](m12-evidence-windows.md)，删除范围见
[产品退役说明](m12-legacy-ui-retirement.md)，剩余任务见 [Backlog](backlog.md)。

## 门禁状态

| Gate | 范围 | 状态 | 当前结论 |
| --- | --- | --- | --- |
| G0 | 非 clean 构建可复现 | Verified | Windows 现有 build tree 可增量 configure/build；日常门禁禁止 wipe |
| G1 | 2D 产品 | Strong | base bgfx 与 product-2d 300 帧 + 同轮完整模块测试已固化（TEST-002 / RunProduct2dGate.ps1） |
| G2 | UI 产品 | Partial | 20控件 showcase、虚拟 List/Tree、2D/3D 产品集合、Text/Glyph 与主题/交互层次均有结构化和视觉证据；Windows UIA tip 跨进程 gate 已固化（2026-08-03，`ui-002-uia-evidence-windows.md`）；剩余 Narrator/Inspect 人工金标、Linux AT-SPI（UI-002-LINUX）与跨 DPI/GPU golden |
| G3 | 3D 产品 | Partial | multi-mesh 产品 E2E（3D-001）、Prefab/Scene weak Mesh/Material Handle + engine-provided、State-owned Mesh3D registry + packet-local geometry/material ref、Mesh/Material/共享 Texture 统一 owner、原子 material bundle、base/MR/normal 贴图采样、World 逐帧有界 directional-light snapshot（sample 3灯）、Texture/Mesh backend retirement marker 与 stale-safe teardown 已落地；仅完整 PBR/IBL/shadow/light system 后置 |
| G4 | Asset/Cooker | Strong | multi-mesh、multi-primitive SPLIT、distinct AssetId/Prefab dependency、baseColor/MR/normal Texture2D cook 与 Material dependency 已完成；不可信 glTF 输入的单 handle/fd 快照、最终路径 containment 与资源预算矩阵已通过 Windows/Linux 门禁；完整 PBR 属于独立 Render 后置项 |
| G5 | Audio | Evidence | backend-neutral tests、miniaudio null-device 与 product-2d JSON 已有 Windows 证据 |
| G6 | 平台矩阵 | Strong | tip Docker：GCC13 Null + Platform/GLFW(Xvfb) + Clang22 Null + Clang22 sanitizer 均 exit 0（见 [Linux 证据](m12-evidence-linux.md)） |
| G7 | Legacy smoke | N/A | 产品已删除，不再运行 Legacy smoke |
| G8 | 产品旧接口零引用 | Done | Legacy product target/source 已删除；整库残留见 CLEAN-001～003 |
| G9 | 独立产品删除 | Done | `e2ef3d5e` 起的删除及后续迁移提交 |

门禁状态与功能路线分开。G2/G3/G4 的后续证据不改变“M12 产品删除 Done”。G6 的 Linux TEST-001
已经完成；可选 Wayland/真显示器仍是独立扩展，不能反向写成当前 tip 未复验。

## UI 证据

当前 Windows gate 直接运行 UI、Runtime UI、Render bridge、FreeType、Scene、RenderScene、bgfx、
Physics2D、Audio 与 Asset GoogleTest executable；测试数量随功能增长，不作永久契约。产品证据包括：

- 20控件 showcase：Dropdown/List/Tree/Scroll 自动交互、Dark/Light 换肤与 root 生命周期；
- Windows UIA：Invoke/Toggle/RangeValue/Value control patterns 已经通过 owner-thread action seam 接入，
  `RunUi002UiaGate.ps1` 可从独立进程连接真实 showcase HWND；
- product-2d schema 19：继承两盏 committed `PointLight2D`、两条 `ShadowOccluder2D`、
  authored=3/committed=2/culled=1 与 soft=2/hard=0 四跑差分；增加独立 aligned normal atlas、
  `normalMappedSpriteCount=1/0`、`texturesUploaded=3`、3张 Texture2D owner/retirement 与 normal on/off 四跑；
  逐次随 Render extraction 发布，并保留 Scene Explorer
  13个 logical item/12个 materialized slot、最终 key `402`、滚动、Theme 与 Tree/TreeItem selected semantics；
- product-3d schema 5：3个 World DirectionalLight3D 连续逐帧发布、Asset ListView/Scene TreeView、
  2次 collection step、最终 keys `2003/4`；
- product-2d 300帧：profile-name TextEdit、ProgressBar value=65、Radio selection 与 `pixelCaptureOk=true`。

2026-07-23 的历史报告 `artifacts/screenshots/sample-2d-product/20260723-013100/report.json` 记录：

- `ok=true`、`exitCode=0`、
  `processOk=true`、`forcedTermination=false`、`stdoutStatusOk=true`；3次 client-only 960x540 capture 中
  有2张稳定非空帧，初始化白帧由 `blankLike=true` 排除；
- 人工与像素复核：`玩家名：星河` 正常；TextEdit/ProgressBar/RadioButton 无裁剪或重叠；65% fill
  精确为143 px（x=700..842），第一项 Radio 有 selected 内块（x=706..721）、第二项没有，未混入标题栏；
  延长 warmup 后仍只有 `PrintWindow` 首次调用为白帧，后续稳定，因此判定为 capture 瞬态而非持续 UI 异常。

ProgressBar/RadioButton 与 UI-005 集合控件已完成产品接入；G2 的自动 HWND UIA gate 已在 tip 固化，仍因
Narrator/Inspect 人工金标、UI-002-LINUX 的 AT-SPI 真机验收与跨 DPI/GPU golden 保持 Partial。
自动 UIA gate 不能冒充读屏人工金标。

## 3D 与 Cooker 边界

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| 最小 glTF/GLB parse + Prefab | Done | Cooked Prefab 基础路径；当前 `tina_sample_3d` 已覆盖 multi-mesh product 映射 |
| 多 glTF mesh cook | Done | 每个 mesh 生成 distinct StaticMesh/Material AssetId，Prefab 依赖可解析 |
| baseColorTexture cook | Done | 相对文件或 bufferView 的 PNG/JPEG 解码为 RGBA8 Texture2D，Material 发布 required dependency，相同 image 去重 |
| multi-primitive mesh (SPLIT) | Done | 每 TRIANGLES prim → StaticMesh+Material；Prefab 展开父+子节点；非三角 prim 结构化失败 |
| multi-mesh product bind/draw | Done | sample 通过 engine-provided、State-owned registry 注册多个 mesh/material binding，以 packet-local ref 解析并由 registry retirement；见 3D-001 / N15 / N16.4 |
| PBR/其他纹理通道 | Partial | metallic-roughness/normal 首切片已进 Material/Render 产品门禁；emissive、IBL、shadow 与完整 PBR 后置 |
| 外部文件安全矩阵 | Done | 主/外部文件单 handle/fd snapshot；root 内 symlink/junction 正向、逃逸拒绝；strict UTF-8/percent-decode、替换检测、64MiB 单文件及 accessor/bufferView/image dimension/count/overflow 与 decode/output budgets 均 fail closed；Windows/Linux `GltfCookTests` 24/24 |

3D 视觉证据分为两层：`tina_sample_3d` 在最后一次 present 后读取 primary framebuffer，stdout JSON 必须
包含 `pixelCaptureOk=true`、非零尺寸/字节数和非空 `pixelFingerprint`；同机、同 backend、同窗口尺寸与
同资源版本的第二次运行可把首次值传给 `--expect-pixel-fingerprint`，并要求
`pixelGoldenChecked/pixelGoldenMatched` 均为 true。fingerprint 不跨 GPU/driver/backend 复用。

外部窗口 PNG 证据由 `tools/windows/CaptureSampleWindow.ps1` 生成；每次运行写入
`artifacts/screenshots/sample-3d-product/<timestamp>/`，其中 `report.json`、`stdout.txt` 与 `frame-*.png`
共同记录进程结果、sample 结构化输出和连续非空画面。该目录是运行产物位置，不在文档中预填未实测的
fingerprint 或 PNG hash。

## 后续工作边界

M12 只跟踪 Legacy 产品图删除及其替代产品证据；不再把已关闭的 `TEST-001`、`TEST-002`、`3D-001` 和
`CLEAN-001`～`CLEAN-003` 重新列为待办。当前未关闭的功能和验证风险以 [Backlog](backlog.md) 为唯一
明细；本历史门禁不复制易漂移的 Now/Next 任务快照。Linux AT-SPI 已拆为 Later 的 `UI-002-LINUX`，
`RENDER-FENCE` 已完成，`RENDER-001` 保持 Deferred。

## Windows 快速复验

禁止 `--clean-first`。

```powershell
# Base bgfx product samples
cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug --target tina_sample_2d tina_sample_3d tina_asset_tests --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_asset_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0 --ui-theme-demo --ui-tree-demo
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0 --ui-theme=dark --ui-theme-demo
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0 `
  --expect-pixel-fingerprint=<first-run-pixelFingerprint>

# External client-area PNG evidence
powershell -NoProfile -ExecutionPolicy Bypass -File tools\windows\CaptureSampleWindow.ps1 `
  -Exe out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe `
  -ArgString '--frames=180 --frame-delay-ms=16' `
  -OutDir artifacts\screenshots\sample-3d-product -RequireNonBlank

# Full 2D feature graph
cmake --preset windows-msvc-vnext-bgfx-product-2d
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests `
           tina_ui_freetype_tests tina_physics2d_tests tina_audio_tests `
           tina_audio_miniaudio_tests tina_sample_2d --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_render_integration_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_freetype_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_physics2d_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_audio_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_audio_miniaudio_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0 --ui-theme-demo --ui-tree-demo

# External Windows UIA HWND client gate; Narrator/Inspect remains a separate manual gate
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunUi002UiaGate.ps1

# Same-host/backend Sprite2D differential gates (also invoked by RunProduct2dGate.ps1)
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunProduct2dShadowVisualGate.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunProduct2dNormalMapVisualGate.ps1
```

normal-map gate 固定四跑（on×2 + off×2）；两种模式都保持 `texturesUploaded=3` 与完整 owner/retirement
生命周期，同时证明模式内可重复、on/off 差分。

完整图成功时 sample 标签必须为 `productGate=bgfx-physics-freetype-audio`。

## 字体规则

FreeType 字体解析顺序：CMake `TINA_UI_FONT_PATH`、环境变量 `TINA_UI_FONT_PATH`、最后才是可选的
`resources/fonts/SourceHanSansSC-Regular.otf` fixture。没有字体时相关测试可以明确 skip，但不能把
skip 记录为 CJK 视觉通过。
