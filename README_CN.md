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
- UI 支持 retained tree、布局、路由、文本/Glyph、Button、Checkbox、Slider、ProgressBar、RadioButton、
  单行 TextEdit、ScrollView、Dropdown/Popup 及虚拟化 ListView/TreeView；
- `tina_sample_ui_showcase` 提供 20 控件工作台、完整交互层次、集合/滚动流程与 Dark/Light 实时换肤；
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
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0 --ui-theme-demo --ui-tree-demo
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0 --ui-theme=dark --ui-theme-demo
```

UI 控件与换肤工作台（完整文字使用 FreeType 图）：

```powershell
cmake --preset windows-msvc-vnext-bgfx-ui-freetype
cmake --build --preset windows-vnext-bgfx-ui-freetype-debug --target tina_sample_ui_showcase -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_sample_ui_showcase.exe --frames=150 --frame-delay-ms=0 --theme=dark --auto-demo
```

完整 2D feature 图（bgfx + Physics2D + FreeType + miniaudio）：

```powershell
cmake --preset windows-msvc-vnext-bgfx-product-2d
cmake --build --preset windows-vnext-bgfx-product-2d-debug --target tina_sample_2d tina_physics2d_tests tina_ui_freetype_tests tina_audio_tests tina_audio_miniaudio_tests -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0 --ui-theme-demo --ui-tree-demo
```

测试 executable 直接运行，不通过 CTest。不要以 `--clean-first` 或删除 `out/build` 作为日常验证步骤。

## 当前状态

- Legacy 产品删除及依赖/兼容扫尾：完成；仅保留 `TINA_BUILD_LEGACY=ON` 的 FATAL 拒绝开关；
- 2D 产品竖切：已形成 Windows 产品门禁；当前工作树已接入 Catalog `SpriteAnimationClip`、
  `SpriteAnimator2D` 和角色 `Idle -> Walk -> HitWall` 状态证据；
- 3D 产品：multi-mesh glTF cooking、AssetId resolver、外部 URI/size policy、baseColor/MR/normal
  Texture2D cook 与 product GPU upload/bind 已完成；样例按多个 mesh/material key 提交，experimental
  metallic-roughness 路径采样三类贴图并使用 material factors 与 key/fill directional light；
  完整 PBR、IBL、shadow、通用 light/pass system 与通用 GPU submission fence 仍后置；Texture/Mesh
  已使用 readback marker 完成 AssetLease-backed GPU retirement；
- UI：20 控件 showcase、虚拟化 ListView/TreeView、Runtime facade，以及 2D Scene Explorer 和 3D
  Asset/Scene collections 已接入产品门禁；具体测试数量以本轮直接运行的 GoogleTest 输出为准；
- Task：ADR 0017 的 Desktop 交互默认值已落实为 `max(1, hw-1)` 个 CPU worker，显式配置保持不变；
- Linux tip 已有 GCC/Clang（含 sanitizer）证据；Wayland、跨 GPU/DPI 视觉 golden、Narrator/AT-SPI
  和完整 benchmark protocol 仍是后续工作。

任务状态统一维护在 [Roadmap](docs/roadmap.md) 与 [Backlog](docs/backlog.md)。架构、构建、测试和
决策分别见 [文档索引](docs/README.md)、[架构总览](docs/architecture.md)、
[构建说明](docs/building.md)、[测试说明](docs/testing.md)与 [ADR 索引](docs/adr/README.md)。
