# Tina 游戏 Runtime

Tina 是一个以 C++23 为基线的 2D/3D 游戏 Runtime。当前产品路径是 vNext Desktop 与
`tina_sample_*`；Legacy `Tina.exe`、旧横版 2D 游戏和旧 UI 产品图已经退役。

当前 retained UI 仍位于 `include/tina/ui` 与 `src/ui`。因此，“Legacy UI 已删除”只表示旧产品实现
已经删除，不表示删除当前 `src/ui`。

## 当前能力

- `EngineHost` 是唯一非全局组合根，`IGameApplication` 管程序生命周期，`IGameState` 承担帧行为；
- Platform/Input 使用 Tina 公共契约与私有 GLFW adapter；
- Render 使用后端无关 `RenderFrame`/`RenderScene`，bgfx 只存在于私有 backend；
- Scene 支持 generation `EntityId`、Transform 层级及 2D/3D extraction；
- Asset 支持 Catalog/Cooked、AssetId、Handle/Lease、Task-backed IO/Main completion、GPU
  upload/retirement；
- UI 支持 retained tree、布局、路由、文本/Glyph、Button、Checkbox、Slider、ProgressBar、RadioButton
  和单行 TextEdit；
- Audio 提供 backend-neutral engine 与可选 miniaudio；Physics2D 提供可选 Box2D 3.x adapter；
- `tina_sample_2d` 是 Catalog/TileMap/UI/Audio/Physics2D 产品门禁，`tina_sample_3d` 是
  glTF/Prefab/Scene/Render 产品门禁。

Game SDK 与公开头不暴露 bgfx、GLFW、Box2D、miniaudio、FreeType、cgltf、stb_image 或 xxHash 类型。

## 快速开始

环境要求：CMake 3.25+、Visual Studio 2026/MSVC 19.50、`VCPKG_ROOT`，源码和终端使用 UTF-8。
Windows 构建通过 `/utf-8` 保证中文源码、日志和 UI 文案不乱码。

基础 Null 图：

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_sample_null -- /m:2 /v:m
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_sample_null.exe --frames=300
```

Windows bgfx 产品图：

```powershell
cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug --target tina_sample_2d tina_sample_3d tina_tests -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
```

完整 2D feature 图（bgfx + Physics2D + FreeType + miniaudio）：

```powershell
cmake --preset windows-msvc-vnext-bgfx-product-2d
cmake --build --preset windows-vnext-bgfx-product-2d-debug --target tina_sample_2d tina_physics2d_tests tina_ui_freetype_tests tina_audio_tests tina_audio_miniaudio_tests -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
```

测试 executable 直接运行，不通过 CTest。不要以 `--clean-first` 或删除 `out/build` 作为日常验证步骤。

## 当前状态

- Legacy 产品删除：完成；剩余的是依赖、兼容文件、文档和 Linux 复验扫尾；
- 2D 产品竖切：已形成 Windows 产品门禁；
- 3D Cooker：multi-mesh glTF cooking、AssetId resolver 与 baseColorTexture PNG/JPEG→Texture2D cook
  已完成；当前产品样例仍只映射单个 product mesh，纹理 GPU 绑定、安全 URI/size policy、PBR 和完整
  multi-mesh 产品 E2E 未关闭；
- UI：当前工作树的 Windows Debug 门禁为 `tina_ui_tests` 190/190、`tina_runtime_ui_tests` 77/77、
  `tina_ui_render_integration_tests` 12/12；ProgressBar/RadioButton 已有 product-2d 结构化与视觉证据；
- Task：ADR 0017 已接受，但 Desktop 尚未落实交互模式 CPU worker 默认值，此项列入 Backlog；
- Linux、跨 GPU/DPI 视觉 golden、UIA/AT-SPI 和完整 benchmark protocol 仍是后续工作。

任务状态统一维护在 [Roadmap](docs/roadmap.md) 与 [Backlog](docs/backlog.md)。架构、构建、测试和
决策分别见 [文档索引](docs/README.md)、[架构总览](docs/architecture.md)、
[构建说明](docs/building.md)、[测试说明](docs/testing.md)与 [ADR 索引](docs/adr/README.md)。
