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

## 完整 2D feature 图

```powershell
cmake --preset windows-msvc-vnext-bgfx-product-2d
cmake --build --preset windows-vnext-bgfx-product-2d-debug --target tina_sample_2d -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
```

记录结果：exit 0，`productGate=bgfx-physics-freetype-audio`，`physicsEnabled=true`、
`freetypeEnabled=true`、`audioMiniaudioEnabled=true`、`audioDeviceNullBackend=true`、
`pixelCaptureOk=true`。

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

记录结果：exit 0，JSON 含 `gltfCooked`、`prefabInstantiated`、`sceneExtract` 和
`renderResourceLedgerBalanced`。该 sample 当前是单 product mesh 映射。

独立 Asset/Cooker 测试已经覆盖 multi-mesh glTF：每个 mesh 生成 distinct StaticMesh/Material AssetId，
Prefab dependency 可解析。尚未关闭两个 mesh 在产品 sample 中分别 upload/bind/draw 的 E2E。

## Audio

`tina_audio_tests` 前序记录为 15/15；product-2d 完整 feature 图还证明 miniaudio null-device callback
与 Catalog AudioClip lease 路径。真实扬声器质量、Linux 当前 tip 与 callback 性能不由这份证据证明。

## 未关闭

- 当前 tip Linux GCC/Clang/sanitizer；
- product-2d 完整模块测试集合的同轮复验；
- multi-mesh 3D 产品 E2E；
- Cooked texture 产品绑定与安全 URI/size policy、PBR、跨 GPU/DPI golden、UIA/AT-SPI。
