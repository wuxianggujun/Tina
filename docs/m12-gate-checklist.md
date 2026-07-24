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
| G2 | UI 产品 | Partial | Text/Glyph、设置控件、TextEdit、ProgressBar、RadioButton 均有产品/结构化/视觉证据；平台 accessibility adapter、跨 DPI/GPU golden 与完整控件矩阵后置 |
| G3 | 3D 产品 | Partial | multi-mesh 产品 E2E（3D-001）与 texture bind API 已通过；Opaque3D 贴图采样 / PBR / fence pin 后置 |
| G4 | Asset/Cooker | Strong | multi-mesh、multi-primitive SPLIT、distinct AssetId/Prefab dependency、baseColorTexture PNG/JPEG cook 与 Material dependency 已完成；PBR 后置 |
| G5 | Audio | Evidence | backend-neutral tests、miniaudio null-device 与 product-2d JSON 已有 Windows 证据 |
| G6 | 平台矩阵 | Strong | tip Docker：GCC13 Null + Platform/GLFW(Xvfb) + Clang22 Null + Clang22 sanitizer 均 exit 0（见 [Linux 证据](m12-evidence-linux.md)） |
| G7 | Legacy smoke | N/A | 产品已删除，不再运行 Legacy smoke |
| G8 | 产品旧接口零引用 | Done | Legacy product target/source 已删除；整库残留见 CLEAN-001～003 |
| G9 | 独立产品删除 | Done | `e2ef3d5e` 起的删除及后续迁移提交 |

门禁状态与功能路线分开。G2/G3/G6 尚未关闭不改变“M12 产品删除 Done”，只表示当前产品/平台
证据仍需扩展。

## UI 证据

当前工作树 Windows Debug 直接结果：

- `tina_ui_tests`：190/190；包含 ProgressBar、RadioButton、TextEdit、tree/layout/hit/paint、
  default action、Semantics、文本 arena/IME 原子性；Checkbox/Slider mutation、TextEdit pointer selection
  与多节点 focus step 在 dirty queue 容量不足时保持状态/回调原子性，同文本替换 selection 会重绘；
- `tina_runtime_ui_tests`：77/77；包含 phase/root-scoped capability、三类新控件 facade、
  TextEdit 输入消费与 Runtime 接线；
- `tina_ui_render_integration_tests`：12/12；覆盖 committed paint 到 DisplayList bridge 的容量和顺序回归；
- product-2d 字体图的 `tina_ui_freetype_tests`：2/2；真实 fixture 的中文 measure/raster 通过；
- product-2d 300 帧：profile-name TextEdit、UTF-8 回读、ProgressBar value=65、RadioButton
  selection invariant、`evidenceSchema=3` 与 `pixelCaptureOk=true`；
- `artifacts/screenshots/sample-2d-product/20260723-013100/report.json`：`ok=true`、`exitCode=0`、
  `processOk=true`、`forcedTermination=false`、`stdoutStatusOk=true`；3次 client-only 960x540 capture 中
  有2张稳定非空帧，初始化白帧由 `blankLike=true` 排除；
- 人工与像素复核：`玩家名：星河` 正常；TextEdit/ProgressBar/RadioButton 无裁剪或重叠；65% fill
  精确为143 px（x=700..842），第一项 Radio 有 selected 内块（x=706..721）、第二项没有，未混入标题栏；
  延长 warmup 后仍只有 `PrintWindow` 首次调用为白帧，后续稳定，因此判定为 capture 瞬态而非持续 UI 异常。

ProgressBar/RadioButton 已完成 UI-001 的产品接入与 Windows client-area 可见证据；G2 仍只因平台
accessibility adapter、跨 DPI/GPU golden 和完整控件/输入矩阵保持 Partial。

## 3D 与 Cooker 边界

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| 最小 glTF/GLB parse + Prefab | Done | `tina_sample_3d` 单 mesh 产品路径 |
| 多 glTF mesh cook | Done | 每个 mesh 生成 distinct StaticMesh/Material AssetId，Prefab 依赖可解析 |
| baseColorTexture cook | Done | 相对文件或 bufferView 的 PNG/JPEG 解码为 RGBA8 Texture2D，Material 发布 required dependency，相同 image 去重 |
| multi-primitive mesh (SPLIT) | Done | 每 TRIANGLES prim → StaticMesh+Material；Prefab 展开父+子节点；非三角 prim 结构化失败 |
| multi-mesh product bind/draw | Open | sample 当前仍映射单个 product mesh key；见 3D-001 |
| PBR/其他纹理通道 | Deferred | metallic-roughness/normal/emissive 与 Material/Render 产品门禁仍后置 |

## 当前剩余工作

1. TEST-001：当前 tip Linux 全门禁；
2. TEST-002：product-2d 完整模块测试与 sample 门禁；
3. 3D-001：multi-mesh 产品 upload/bind/extract/draw E2E；
4. CLEAN-001～003：vcpkg/EASTL/compatibility/错误文案残留；
5. UI-002～003：accessibility adapter、跨 DPI/GPU golden 与完整控件/输入矩阵。

## Windows 快速复验

禁止 `--clean-first`。

```powershell
# Base bgfx product samples
cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug --target tina_sample_2d tina_sample_3d tina_asset_tests -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_asset_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0

# Full 2D feature graph
cmake --preset windows-msvc-vnext-bgfx-product-2d
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests `
           tina_ui_freetype_tests tina_physics2d_tests tina_audio_tests `
           tina_audio_miniaudio_tests tina_sample_2d -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_render_integration_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_freetype_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_physics2d_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_audio_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_audio_miniaudio_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
```

完整图成功时 sample 标签必须为 `productGate=bgfx-physics-freetype-audio`。

## 字体规则

FreeType 字体解析顺序：CMake `TINA_UI_FONT_PATH`、环境变量 `TINA_UI_FONT_PATH`、最后才是可选的
`resources/fonts/SourceHanSansSC-Regular.otf` fixture。没有字体时相关测试可以明确 skip，但不能把
skip 记录为 CJK 视觉通过。
