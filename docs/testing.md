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

## Windows UI 快速门禁

UI 日常修改使用统一入口，脚本负责增量构建、直接运行 GoogleTest、传递 filter 和编译进程退出检查：

```powershell
# 完整 tina_ui_tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunMsvcUiTests.ps1

# 定向回归
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunMsvcUiTests.ps1 `
  -GTestFilter 'UITextPaintEmitterTests.*:*Text*:*Ime*:*Paint*'

# 已确认 binary 对应当前源码时，只运行测试
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunMsvcUiTests.ps1 `
  -SkipBuild -GTestFilter 'UITextPaintEmitterTests.*'
```

默认 topology 是 `windows-msvc-vnext-bgfx` Debug；没有 build tree 时自动 configure，已有 tree 由
CMake 在需要时自动 regenerate。脚本使用 `/nr:false`，不应再在调用处追加 `/m:2 /v:m`。

## 测试 target 拓扑

| Executable | 主要范围 | 可用条件 |
| --- | --- | --- |
| `tina_tests` | Core、Platform contract、Task、Runtime、NullRender、Input/Action、header isolation | 基础图 |
| `tina_ui_tests` | UI tree/layout/hit/route/paint/semantics、Widget、文本/Glyph | 基础图 |
| `tina_runtime_ui_tests` | Runtime UI owner/capability/route/layout/display handoff | 基础图 |
| `tina_ui_render_integration_tests` | committed UI paint → Render DisplayList | 基础图 |
| `tina_scene_tests` | Entity/Transform/2D/3D component/extraction、ParticleSystem2D、Trail2D | 基础图 |
| `tina_render_scene_tests` | Camera2D/3D、culling、sort/batch、world picking | 基础图 |
| `tina_asset_format_tests` | Cooked/Manifest 与 typed payload schema | 基础图 |
| `tina_asset_tests` | Catalog、AssetSystem、Handle/Lease、Cooker、upload/retirement | 基础图 |
| `tina_audio_tests` | backend-neutral AudioEngine/voice/bus/command/completion | 基础图 |
| `tina_platform_glfw_tests` | GLFW adapter 与 WindowSurface | `TINA_BUILD_PLATFORM_GLFW=ON` |
| `tina_render_bgfx_tests` | bgfx lifecycle、2D/3D/UI geometry/resource | `TINA_BUILD_RENDER_BGFX=ON` |
| `tina_ui_freetype_tests` | FreeType font open/measure/rasterize | `TINA_BUILD_UI_FREETYPE=ON` |
| `tina_ui_uia_tests` | Windows UIA property/fragment、control pattern、action 与 provider lifecycle | `TINA_BUILD_UI_UIA=ON` (Windows) |
| `tina_physics2d_tests` | Box2D lifecycle/contact/query/deferred command/grid bridge | `TINA_BUILD_PHYSICS2D=ON` |
| `tina_audio_miniaudio_tests` | miniaudio null-device、decode/mix adapter | `TINA_BUILD_AUDIO_MINIAUDIO=ON` |

## 基础 Windows 门禁

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug `
  --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests `
           tina_scene_tests tina_render_scene_tests tina_asset_format_tests tina_asset_tests `
           tina_audio_tests tina_sample_null --parallel 2 -- /nr:false

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

测试数量随功能增长，不作为永久契约；本轮必须直接运行对应 GoogleTest executable，并以最终 gate
JSON 与退出码记录结果。

## UI performance quick run

`tina_bench` 的 UI workload 使用真实 `UIContext`、committed snapshots、pointer route、虚拟集合与
backend-neutral DisplayList。共享开发机只记录 `conclusion=provisional`；checksum、固定工作量、容量和
warmup 后 UI PMR allocation delta 属于确定性不变量。

```powershell
cmake --build --preset windows-vnext-bgfx-debug --target tina_bench --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_static_commit_v1 --warmup=60 --samples=600 --seed=1
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_paint_dirty_v1 --warmup=60 --samples=600 --seed=1
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_route_v1 --warmup=60 --samples=600 --seed=1
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_virtual_collection_v1 --warmup=60 --samples=600 --seed=1
```

正式采样规模、fingerprint 与固定机规则见[性能与内存](performance-memory.md)和
[ADR 0018](adr/0018-benchmark-protocol.md)。

## UI showcase 门禁

完整控件、中文与主题视觉验收使用 bgfx + FreeType 图：

```powershell
cmake --preset windows-msvc-vnext-bgfx-ui-freetype
cmake --build --preset windows-vnext-bgfx-ui-freetype-debug `
  --target tina_sample_ui_showcase tina_ui_tests tina_runtime_ui_tests `
           tina_ui_render_integration_tests tina_ui_freetype_tests --parallel 2 -- /nr:false

out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_sample_ui_showcase.exe `
  --frames=150 --frame-delay-ms=0 --theme=dark --auto-demo
out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_sample_ui_showcase.exe `
  --frames=150 --frame-delay-ms=0 --theme=light --auto-demo
```

两个自动 smoke 均须 exit 0，并输出 `controls=20`、`imageProducts=4`、`themeSwitches=2`、`sliderChanges>0`、
`progressValue=84`、`dropdownSelection=1`、`listSelectionKey=1007`、`treeSelectionKey=4`、
`treeExpansionChanges=2`、`scrollOffset=80`、`uiRootsCreated=1`、`uiRootsReleased=1`，最终主题回到
`initialTheme`。图片产品证据还必须满足 `imageAtlasUploaded=true`、`imageAtlasReleased=true`、
`imageResolverCalls=imageResolverHits>0`、`imageResolverUnavailable=0`、`maxImageQuads=12`、
`maxImageBatches=4`、`maxUniqueImageResources=1`、`imageLinear=true`、`imageNearest=true` 与非零
`paintOrderChecksum`；退出阶段还须有 `imageAtlasInvalidated=true`。
`--auto-demo` 与显式 `--frames` 同用时至少 120 帧。

资源失效与 missing/unavailable 产品 smoke 使用独立模式，不能与 `--auto-demo` 同用：

```powershell
out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_sample_ui_showcase.exe `
  --frames=30 --frame-delay-ms=0 --image-lifecycle-demo
```

该模式第 10 帧销毁 atlas，第 20 帧释放 root-scoped resolver registration；必须 exit 0，并输出
`imageAtlasInvalidated=true`、`imageResolverCalls=19`、`imageResolverHits=9`、
`imageResolverUnavailable=10`、`imageResolverUnbound=true`、`imageFrames=9`、
`imageFreeFrames=21`、`maxImageQuads=12`、`imageFrameBorrowsAtRelease=0` 及非零
`paintOrderChecksum`。这证明 unavailable
阶段连续 skip、missing resolver 阶段不再调用 resolver，且非图片 UI 仍持续提交。

Visual/interaction 验收另跑不带 `--auto-demo` 的窗口：确认 Dark/Light 切换后既有控件同步换肤，
Primary/Destructive/Disabled Button 层次清楚，pointer press 会压低阴影并切换圆角 border ring 颜色，Tab focus 可辨，
Slider 与 ProgressBar 联动，Dropdown、List、Tree、Scroll 可操作，TextEdit 中文可读且左右 padding 与
pointer caret 一致。普通
`windows-msvc-vnext-bgfx` 图未启用 FreeType，placeholder text 不能计为字体或 CJK 视觉通过。

## Windows UIA 产品门禁

`RunUi002UiaGate.ps1` 使用 Windows UI Automation client API 从独立进程连接真实
`tina_sample_ui_showcase` HWND，验证外部发现、属性、fragment 与
Invoke/Toggle/RangeValue/Value action；脚本正常发送 `WM_CLOSE`，让 EngineHost/UI owner 按产品路径退出。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunUi002UiaGate.ps1
```

该 gate 不替代 Narrator/Inspect 人工金标，也不证明 Linux AT-SPI。已有 build 可使用
`-SkipConfigure -SkipBuild`，并通过 `-OutJson` 固化结构化证据。

## 改动到门禁映射

| 改动范围 | 最小测试 | 追加 smoke/平台 |
| --- | --- | --- |
| Core/Result/Time/Memory | `tina_tests` | Null 300帧；Linux sanitizer |
| Platform/Input/WindowSurface | `tina_tests`、`tina_platform_glfw_tests` | `tina_sample_platform`，X11/Wayland |
| Task/关闭顺序 | `tina_tests` | Null/Desktop 300帧，失败注入 |
| Runtime phase/state | `tina_tests`、`tina_runtime_ui_tests` | Null、2D、3D products |
| UI/Widget/Text | `tina_ui_tests`、`tina_runtime_ui_tests`、bridge | FreeType、UI showcase、product-2d、截图 |
| Windows UIA/accessibility action | `tina_ui_tests`、`tina_ui_uia_tests`、`tina_runtime_ui_tests` | `RunUi002UiaGate.ps1` + Narrator/Inspect 人工金标 |
| RenderScene/Scene/2D-FX | `tina_render_scene_tests`、`tina_scene_tests` | extraction samples、2D/3D products |
| bgfx backend | `tina_render_bgfx_tests` | Desktop/2D/3D GPU samples + Visual |
| Asset format/Cooker | `tina_asset_format_tests`、`tina_asset_tests` | `assetc`→validate→sample、3D product |
| TileMap payload/runtime | `tina_asset_format_tests`、`tina_asset_tests`、`tina_physics2d_tests` | `tina_sample_2d`；验证显式 visual/collision/object layer |
| Audio | `tina_audio_tests` | miniaudio tests、product-2d |
| Physics2D | `tina_physics2d_tests`（body/shape/joint、sensor、query、grid bridge） | Release bench、product-2d |
| CMake/preset/dependency | 所有受影响 configure 图 | 最小 executable + product smoke |

公共 API 变化还必须编译 header-isolation/consumer 测试，并扫描公开头是否出现第三方 token。

## Shutdown deadline

`RUNTIME-SHUTDOWN-DEADLINE` 的自动门禁归属 `tina_tests`，必须覆盖：

- Disabled/Bounded TaskSystem 拒绝非 finite 或非正 deadline，且非法调用不触发 stop；
- idle、queued drain 和已完成后的重复调用在 deadline 内成功；
- blocked Worker 触发 `TaskErrorCode::WaitTimeout` 后，TaskSystem 仍为 stopping，线程、队列和 owner storage
  未被 join/clear/reset；放行任务后对同一对象重试成功；
- `EngineHost` 将 `EngineConfig::shutdownDeadline` 原样传入 TaskSystem，且该值只预算
  Worker-exit/join 阶段，不被描述为 Audio/Render/整个 Host shutdown 的总耗时上限；
- Host 的 TaskSystem timeout death path 在 `std::terminate()` 前写入 `runtime.lifecycle` Diagnostics，
  并且不继续析构 TaskSystem、Platform、Clock、Diagnostics 等剩余 owner。

```powershell
cmake --build --preset windows-vnext-debug --target tina_tests --parallel 1 -- /nr:false
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes `
  --gtest_filter="DisabledTaskSystemTest.ShutdownDeadline*:BoundedTaskSystemTest.ShutdownDeadline*:EngineHostCreationTest.PassesConfiguredShutdownDeadlineToTaskSystem:EngineHostShutdownDeadlineDeathTest.*"
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes
```

仅观察进程终止不够：death test 同时匹配 `ShutdownDeadlineExceeded` Diagnostics 输出，并以析构哨兵证明
超时后没有 Task owner teardown。本轮 shutdown deadline 聚焦门禁和完整 `tina_tests` 均已直接执行通过。

## Scene Sprite AssetHandle A1

`ASSET-HANDLE-SCENE-2D-A1` 的自动门禁归属 `tina_scene_tests` 与 2D product gate：

- `AssetHandle.hpp`、`SpriteRenderer2D.hpp`、`ExtractRenderScene.hpp` header isolation 编译；
- World 可保存 default weak handle，但 visible extract 必须返回 `UnresolvedSprite`；
- stale、cross-store、wrong-kind、unbound、缺 resolver 和 resolver 返回0都 fail closed；
- hidden unresolved sprite 不调用 resolver；成功解析保持 UV/pivot/transform/color/sort，writer failure 原样传播；
- `SpriteAnimator2D` 的空 handle frame 仍为 `InvalidAnimation`；
- `tina_sample_2d` 的 World crate/character 组件来自 Catalog Sprite handle，resolver 每次验证 Store/kind/
  binding 后映射 key。A1 本身未证明 registry；A2 已补该证据，A3 已完成 FX Handle 化，A4 已完成
  TileMap Tileset Handle 化；A5 已完成 3D component/Prefab Handle 化，A6 已补 engine-provided、State-owned 3D registry；
  Sprite2D 与 Mesh3D 的 `FrameResourceRef`/retirement ownership 已由 N16.1-N16.4 统一，总项 Done。

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_scene_tests tina_sample_2d --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe `
  --frames=300 --frame-delay-ms=0
```

## Sprite2D Binding Registry A2 / N16.3

`ASSET-HANDLE-SCENE-2D-A2` 的模块门禁归属 `tina_asset_tests`，产品闭环归属 2D product gate：

- `Sprite2DBindingRegistry.hpp` header isolation；Create 容量/PMR failure 与 owner-thread 约束；
- Texture2D Handle/GPU texture validation、exact duplicate conflict、同 AssetId conflict、同一 registry
  重复 GPU owner conflict、固定容量，以及 device instance namespace 内 key 唯一、单调不复用；
- 多个 registry 共享同一 device 时 key distinct、独立 resolve/retirement；
- register 成功消费一份 `AssetLease` 与调用方 `GpuTextureId&`；所有 preflight/acquire/backend failure
  保留候选 GPU 且无 Lease 净增长；
- active frame borrow 阻止 retirement；PMR/backend retirement failure 保留完整 Entry，成功才 handoff；
  覆盖同步 completion、延迟 completion 在 Registry 析构后释放 Lease、retire-all 部分提交/重试与析构门禁；
- Sprite 必须是 live Handle 且 Cooked 文件恰有一个 required `Texture2D` dependency；stale/wrong-kind/
  missing/multiple dependency、unbound/stale texture 都返回0；
- Registry 是 Sprite2D Lease/GPU/binding 唯一 owner；产品 State 不再保存裸 GPU owner，退出只通过
  `retireAllTextureBindings()` handoff；
- N16.3 当时的 product evidence schema 14 已由当前 schema 16 继承：`spriteBindingTextures=2`、
  `spriteTextureLeasesAcquired=2`、
  `spriteTextureRetirementsAccepted=2`、`spriteBindingRegistryReleased=true`、
  `spriteTextureHandlesInvalidated=2`、`spriteTextureRetirementRecords=2`、
  `spriteTextureRetirementReleased=2`、`spriteTextureRetirementLive=0`，且四类 resolver hit 均非0。

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_asset_tests tina_scene_tests tina_sample_2d --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_asset_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe `
  --frames=300 --frame-delay-ms=0
```

A2 保留 A1 resolver ABI；A3 随后完成 Particle/Trail Handle 化，A4 完成 TileMap Tileset Handle 化，A5
完成 3D Mesh/Material component Handle 化，A6 再补 engine-provided、State-owned 3D registry。N16.3
统一 Sprite2D retirement ownership 与 `FrameResourceRef`，N16.4 已统一 Mesh3D/Material/共享 Texture owner，
因此 `ASSET-HANDLE-SCENE` 总项 Done。

## Particle/Trail Sprite AssetHandle A3

`ASSET-HANDLE-SCENE-2D-A3` 的模块门禁归属 `tina_scene_tests`，产品闭环归属 2D product gate：

- `AssetFrameResourceResolver.hpp`、`ParticleSystem2D.hpp`、`Trail2D.hpp` header isolation 编译；
- Particle burst 与 live particle、Trail config 保存 weak Sprite `AssetHandle`，无旧 key overload/双轨，
  且不持有 resolver、Lease、Cooked payload 或 GPU owner；
- 空 Particle burst/Trail config 在发布前返回 `InvalidComponent`；Particle 失败保持 live set、stable key 与
  RNG 状态不变；
- live Particle 按 item 解析；非空 Trail 只解析一次并复用。missing/zero/stale/wrong-kind binding 均
  `UnresolvedSprite` fail closed，空 Particle/Trail 不调用 resolver；
- Particle 18/18、Trail 13/13，完整 `tina_scene_tests` 83/83；覆盖 handle 保留、PMR 300帧稳态与 writer
  capacity failure；
- product evidence schema 11 分别要求 `particleSpriteBindingResolverHits>0`、
  `trailSpriteBindingResolverHits>0`；FX fingerprint schema 2 序列化稳定 `AssetId` bytes，不序列化 runtime
  generation bits 或 render key。

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_scene_tests tina_sample_2d --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_scene_tests.exe `
  --gtest_filter="ParticleSystem2DTests.*:Trail2DAssetTest.*" --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe `
  --frames=300 --frame-delay-ms=0
```

A3 不迁移 TileMap、3D Mesh/Material registry、统一 retirement ownership 或 `FrameResourceRef`。

## TileMap Tileset AssetHandle A4

`ASSET-HANDLE-SCENE-2D-A4` 的模块门禁归属 `tina_asset_tests`，产品闭环归属 2D product gate：

- N13 当时的 `AssetBindingResolver.hpp` 与更新后的 `TileChunkRender.hpp` header isolation 编译；N16.2 已以
  `AssetFrameResourceResolver.hpp` 替代旧 header，Scene resolver 保留语义 alias，AssetTypes 不反向依赖 Scene；
- `TileChunkSpriteEmitParams` 保存 weak Tileset Handle 与 borrowed resolver，无旧 `spriteKey` 字段/双轨；
- registry `resolveTileset()` 要求 live Tileset、唯一 required Texture2D dependency 与 live binding；wrong-kind、
  queued、stale、missing/unbound dependency 均返回0；
- hidden/off-camera/empty TileMap 不调用 resolver，单 chunk 与跨 chunk 非空 visible set 均只解析一次；
  missing/zero binding fail closed 为 `SpriteBindingNotFound` 并清空输出；
- `tina_asset_tests` 170/170、`tina_scene_tests` 83/83、headless TileMap sample 300 帧通过；
- product evidence schema 12 新增 `tileMapSpriteBindingResolverHits>0`，selection highlight 即时解析 Tileset，
  产品 State 不再保存 `tileSpriteKey_`。

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_asset_tests tina_scene_tests tina_sample_2d tina_sample_2d_tilemap --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_asset_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d_tilemap.exe --frames=300
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunProduct2dGate.ps1 `
  -SkipConfigure -SkipBuild
```

A4 本身不迁移 3D；A5 随后完成 component/Prefab Handle 化，但不实现 engine-provided、State-owned Mesh/Material
registry、统一 retirement ownership 或 `FrameResourceRef`。

## Mesh/Material AssetHandle A5

`ASSET-HANDLE-SCENE-3D-A5` 的模块门禁归属 `tina_scene_tests`，产品闭环归属 3D product smoke：

- `MeshRenderer3D` 保存 weak StaticMesh/Material Handle，不保存 mesh/material key；结构属性校验与资源
  存活校验分开，World 可保存 empty weak handle；
- visible extraction 分别借用 mesh/material resolver，严格按预期 AssetKind 解析；empty/stale/cross-store/
  wrong-kind/unbound/zero 统一 `UnresolvedMesh`，mesh 失败不调用 material resolver，hidden mesh 不解析；
- `PrefabMeshBinding` 只做 AssetId→Handle 并保持失败全量 rollback，不生成 Render key；
- infrastructure sample 使用 State-owned 最小 `AssetStore` fixture；产品 sample 的 Resources-owned
  `AssetStore` 覆盖 World/extraction，backend key 只保留在私有 GPU binding slot；
- 3D product evidence schema 1 要求 mesh/material handle 发布数匹配 slot 数、两类 resolver hits>0、
  AssetStore active、像素捕获成功与 GPU ledger 归零。

```powershell
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_scene_tests tina_sample_3d_infrastructure tina_sample_3d --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d_infrastructure.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=300 --frame-delay-ms=0
```

A5 不实现 engine-provided、State-owned 3D Mesh/Material binding registry、统一 retirement ownership 或
`FrameResourceRef`，因此 `ASSET-HANDLE-SCENE` 总项仍为 Partial。

## Mesh3D Binding Registry A6

`ASSET-HANDLE-SCENE-3D-A6-BINDINGS` 的模块门禁归属 `tina_asset_tests` 与 Render 测试，产品闭环归属
3D product smoke：

- `Mesh3DBindingRegistry.hpp` header isolation；Create 分别校验 mesh/material 容量、PMR failure 与
  owner-thread 约束，稳态 register/resolve/unbind 不再分配 PMR；
- mesh/material 使用独立 device-instance key namespace；两类 key 都从2开始，分别保留内置 key 1；
  同 device 多 registry key distinct，bind 成功后才消费，解绑后不复用；
- mesh 与 material Handle 的 invalid/stale/cross-store/wrong-kind/not-ready、exact duplicate、同 AssetId
  conflict、容量和 backend rollback 都有失败原子性覆盖；
- Material 从 Cooked payload 取得 factors，并要求每个启用 role 有 live Texture2D/GPU pair，按
  baseColor/MR/normal 顺序与 Cooked required Texture2D dependency 精确匹配；三张纹理与
  factors 单次原子发布，任一 dependency stale 后 resolve fail closed；
- Material v2 writer 要求最多三个 Texture2D dependency 在 baseColor/MR/normal role 顺序下 AssetId 严格
  递增且唯一；乱序与多 role 共享同一 AssetId 都返回结构化错误；
- exact handle stale 后仍可 unbind；backend unbind 失败保留 entry 供重试，仅当资源仍 live 时 resolve 继续
  有效，stale 资源始终解析为0；成功 unbind 才删除 entry；
- 产品先 reset World，再逆序 exact unbind；只有 unbind 成功才 destroy 对应 GPU owner，失败保留 owner 供
  teardown 重试。schema 2 要求 `meshBindingsRegistered=2`、`materialBindingsRegistered=2`、
  `meshBindingsReleased=2`、`materialBindingsReleased=2`、`meshesDestroyed=2`、`texturesDestroyed=6`、
  `meshAssetBindingResolverHits=600`、`materialAssetBindingResolverHits=600`、
  `bindingRegistryReleased=true`、GPU ledger 归零与像素捕获成功。

```powershell
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_tests tina_render_bgfx_tests tina_asset_format_tests tina_asset_tests tina_sample_3d --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_bgfx_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_asset_format_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_asset_tests.exe --gtest_color=yes `
  --gtest_filter="Mesh3DBindingRegistryTests.*"
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=300 --frame-delay-ms=0
```

A6 当时不拥有 GPU resource、`AssetLease` 或 retirement record；其历史门禁保持不变。N16.4 已用统一
retirement ownership 与 `FrameResourceRef` 替代该分裂 owner 契约，并关闭 `ASSET-HANDLE-SCENE` 总项。

## Frame Resource Core N16.1

`ASSET-HANDLE-SCENE-N16.1-CORE` 的门禁归属 `tina_tests`、`tina_asset_tests` 与 Render header isolation：

- `FrameResourceRef` 不能由调用方构造有效 identity；packet table 按 kind+binding key 去重；重复 pin 立即
  释放，invalid/capacity 失败不消费 pin；
- cross-packet、stale generation、wrong-kind 与越界 ref fail closed；complete、skip、abandon、复用和析构
  exactly-once 释放，普通 FramePin 与 resource pin 容量相互独立；
- Runtime 在 extraction 前 begin packet，空 State 栈正常退出不 begin；extract/UI/submit/present 失败和成功
  present 都在 State `onExit` 前清零 submission accounting；若 persistent failure 使 packet abandon 失败，
  Runtime 必须 fail-stop，不能继续销毁仍被 live frame owner 引用的 State；
- Texture2D 既有 Lease+GPU owner retirement 覆盖成功转移、PMR payload allocation rollback、backend reject
  后重试、wrong-kind/cross-store/invalid/wrong-thread 不变性、同步 completion 完成最后一个
  `UnloadPending` Lease，以及 drain exactly-once。

```powershell
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_tests tina_asset_tests tina_render_bgfx_tests --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_asset_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_bgfx_tests.exe --gtest_color=yes
```

本轮结果：`tina_tests` 329/329、`tina_asset_tests` 188/188、`tina_render_bgfx_tests` 52/52；2D/3D
产品 sample 均通过 300 帧 smoke，DOC-002 为0 error / 0 warning。

N16.1 不迁移 Scene item，也不让 registry 拥有 Lease/GPU retirement；产品 2D 证据由 N16.2/N16.3 升级，
`ASSET-HANDLE-SCENE` 总项保持 InProgress。

## Sprite Frame Resource N16.2

`ASSET-HANDLE-SCENE-N16.2-SPRITE` 的门禁覆盖 Runtime、Scene、Asset、RenderScene、Null 与 bgfx：

- `AssetFrameResourceResolver` 只在 extraction 期间借用当前 `FrameResourceSink`，World、TileMap、selection、
  Particle 与 Trail 的 Sprite2D item 只保存 packet-local texture ref；同帧相同 binding 只 intern 一次；
- registry 首次 intern 时转移 entry borrow pin；N16.3 后活跃 packet 完成、跳过或 abandon 前 retirement
  必须失败且保留完整 Entry，packet 释放后可重试；
- Null/bgfx 在任何 draw/binding 提交副作用前验证 cross-packet、stale、wrong-kind、越界与 binding key
  表示范围，失败不产生部分提交；
- N16.2 当时尚未迁移 3D `Mesh3D` item；该剩余项已由 N16.4 完成。

```powershell
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_tests tina_scene_tests tina_asset_tests tina_render_scene_tests tina_render_bgfx_tests `
           tina_sample_2d tina_sample_3d --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_asset_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_bgfx_tests.exe --gtest_color=yes
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunProduct2dGate.ps1 `
  -SkipConfigure -SkipBuild
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=300 --frame-delay-ms=0
```

本轮基础 bgfx 图直接测试结果：`tina_tests` 330/330、`tina_scene_tests` 91/91、
`tina_asset_tests` 199/199、`tina_render_scene_tests` 39/39、`tina_render_bgfx_tests` 54/54。
Product-2D 同轮门禁已返回 `productGate=bgfx-physics-freetype-audio`，3D 300 帧 smoke exit 0 且
`renderResourceLedgerBalanced=true`、final-present capture 成功。

## Mesh3D Frame Resource 与 Owner N16.4

`ASSET-HANDLE-SCENE-N16.4-MESH-OWNER` 的门禁覆盖 Scene、Asset、RenderScene、Null、bgfx 与完整
product-3d：

- `RenderMesh3DInput/Item/Batch` 只保存 `Mesh3DGeometry`/`Mesh3DMaterial` packet-local ref；旧
  `meshKey/materialKey` 字段无兼容双轨；
- packet 固定资源预算为320，覆盖默认64个 Sprite2D Texture，以及 Product 3D 上限各128个
  Mesh3D Geometry/Material 的混合 working set；不动态增长，超过预算仍返回
  `FrameResourceCapacityExceeded`；
- Scene mesh/material resolver 统一使用 `AssetFrameResourceResolver`；旧 `AssetBindingResolver.hpp` 与
  header-isolation TU 删除；invalid/stale/wrong-kind/unbound/empty 继续 `UnresolvedMesh` fail closed；
- Null/bgfx 在提交副作用前验证 cross-packet、stale、wrong-kind、index 与 `u32` binding range；
- `GpuTextureId`/`GpuMeshId` 校验 device owner；两个 live Null device 的 index/generation 碰撞仍确定性拒绝；
- bgfx Texture/Mesh 私有 slot 的 generation 达到 `u32` 上限后永久退役，创建扫描不再复用该 slot；
- Mesh registry 注册成功才消费 GPU owner；Mesh/Texture retirement 的 owner-thread、kind/store/state、PMR、
  ledger 与 backend failure 保留 Lease/GPU/Entry，可重试；active Mesh/Material frame borrow 阻止 retirement；
- Material 引用计数允许跨 Material 共享 Texture owner，并阻止 live dependency 被退休；`retireAllBindings()`
  按 Material→Texture→Mesh 关闭，析构要求全空；
- product-3d schema 4 要求2 Mesh、2 Material、3共享 Texture 上传与 retirement，weak handle 失效数为
  2/2/3，retirement records 全部 Released 且 live=0，两类 frame-resource resolver hits 为600/600，registry
  释放、GPU ledger 平衡、pixel capture 成功。

```powershell
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_tests tina_scene_tests tina_asset_tests tina_render_scene_tests tina_render_bgfx_tests `
           tina_sample_3d tina_sample_3d_extraction tina_sample_3d_infrastructure --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_asset_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_bgfx_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d_extraction.exe --frames=300
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d_infrastructure.exe --frames=300 --frame-delay-ms=0
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunProduct3dGate.ps1
```

基础 bgfx 图结果：`tina_tests` 335/335、`tina_scene_tests` 91/91、`tina_asset_tests` 204/204、
`tina_render_scene_tests` 39/39、`tina_render_bgfx_tests` 61/61；两个 infrastructure sample 与 product sample
均完成300帧。FreeType 同轮产品门禁结果见下文 TEST-003。

## Scene DirectionalLight3D

`RENDER-001-SCENE-LIGHTS` 的最小门禁覆盖：

- World `set/clear/queryDirectionalLight3D()`、非法 component 零替换与 entity 生命周期；
- active light 的 world local `+Z` 方向、color×intensity、ambient、稳定 Entity identity 排序；
- 超过固定4灯上限显式 `TooManyActiveDirectionalLights`；inactive-only 发布 ambient-only snapshot；
- `RenderSceneWriter::setMesh3DLighting()` 深拷贝调用方 span，重复/非法描述使 build 原子失败；
- Null/bgfx submit preflight；bgfx frame snapshot 临时覆盖 device fallback，且每帧只编码一次 uniform arrays；
- product-3d schema 5 要求 `directionalLightCount=3`、`sceneLightingFrames=300`。

直接验证入口为 `tina_scene_tests`、`tina_render_scene_tests`、`tina_render_bgfx_tests` 与
`tina_sample_3d --frames=300 --frame-delay-ms=0 --ui-theme=dark --ui-theme-demo`。公开新头还由
`DirectionalLight3DHeader.cpp` header-isolation TU 覆盖。

## Scene ShadowOccluder2D

`2D-LIGHT-N2` 的最小门禁覆盖：

- World `set/clear/queryShadowOccluder2D()`、非法 component 零替换与 entity slot reuse；
- active segment 的已发布 XY scale/rotation/position、稳定 Entity identity 排序与 inactive 过滤；
- 超过固定32段上限显式 `TooManyActiveShadowOccluders2D`，occluder-only 保持 unlit；
- `RenderSceneWriter::setSprite2DLighting()` 深拷贝 segment span，非 finite、退化、超容量或重复描述使
  build 原子失败；
- Null/bgfx submit preflight；bgfx fragment→light 相交仅清零对应点光贡献；
- product-2d schema 16 从成功提交的 committed snapshot 观测 `pointLight2DCount=2`、
  `shadowOccluder2DCount=2` 与逐帧 lighting count。

直接验证入口为 `tina_scene_tests`、`tina_render_scene_tests`、`tina_render_bgfx_tests` 与
`tina_sample_2d --frames=300 --frame-delay-ms=0 --ui-theme-demo --ui-tree-demo`。公开新头还由
`ShadowOccluder2DHeader.cpp` header-isolation TU 覆盖。

## 产品样例的证据边界

| Sample | 证明 | 不证明 |
| --- | --- | --- |
| `tina_sample_null` | EngineHost、固定帧、Headless/Null lifecycle | GLFW、GPU、可见 UI |
| `tina_sample_platform` | GLFW window/input/WindowSurface + NullRender | bgfx 绘制 |
| `tina_sample_desktop` | Desktop bootstrap、真实 bgfx surface、UI pass | 2D/3D 产品内容 |
| `tina_sample_asset` | Catalog→Task→AssetSystem→ReadyGpu/Lease | 可见纹理/mesh |
| `tina_sample_2d_infrastructure` | CPU/Null Camera2D/Sprite extraction | Catalog/产品 UI/GPU |
| `tina_sample_2d_infrastructure_bgfx` | fixture Sprite2D + UI overlay | 正式 Catalog TileMap 产品 |
| `tina_sample_2d` | Catalog TileMap v3 root + deferred TileMapChunk；每帧 visual=10/collision=20 demand→pump→commit 与 resident 证据；gameplay objects=30，消费 point 101/rectangle 102；SpriteAnimationClip/Animator、fixed-capacity Particle/Trail、Gameplay、成熟 Theme UI 与 Scene Explorer TreeView、Audio；Physics 含 multi-shape API、sensor enter/exit 与 Distance joint；全部 Sprite2D extraction 使用 packet-local `FrameResourceRef`；schema 16 增加两盏 `PointLight2D`、两条 `ShadowOccluder2D` 与 `sceneLightingFrames=renderExtractions`，并保留 Dark→Light→Dark、Tree stable-key selection/scroll/semantics、两份 Registry Lease/GPU/binding owner handoff、两次 retirement、weak texture handle 失效、ledger Released、World/TileMap/Particle/Trail resolver hits 与 FX fingerprint schema 2，final-present RGBA8 capture 与单机 exact golden；feature 图含 Physics/FreeType/miniaudio | Registry transaction/PMR/owner-thread 压力（由 `tina_asset_tests` 证明）、Particle/Trail 事务性与 PMR 压力（由 `tina_scene_tests` 证明）、TileMap retain-capacity LRU 压力（由 `tina_asset_tests` 证明）、2D light culling/soft shadow/normal map、priority IO/editor/自动 gameplay 生成、更多 shape/joint、Linux、跨 GPU golden |
| `tina_sample_3d_extraction` | CPU/Null Perspective/Mesh extraction | 可见 GPU 3D |
| `tina_sample_3d_infrastructure` | procedural fixture Cube/depth/instance | Cooked product mesh |
| `tina_sample_3d` | 双 mesh glTF→Cooked→AssetSystem→Prefab/Scene weak Handle→engine-provided、State-owned Mesh3D registry→packet-local geometry/material ref→bgfx；evidence schema 5、Mesh/Material/3共享 Texture owner handoff 与 retirement ledger、原子 baseColor/MR/normal/factors binding、3个 World DirectionalLight3D 的逐帧 snapshot、成熟 retained controls、Asset ListView/Scene TreeView、Dark→Light→Dark、final-present RGBA8 capture 与单机 exact golden | Registry transaction/PMR/owner-thread 压力（由 `tina_asset_tests` 证明）、完整 PBR/IBL/shadow、point/spot light + culling、跨 GPU golden |

`tina_sample_2d` 是唯一产品 2D target；中间迁移名 `tina_sample_2d_tilemap_bgfx` 已删除。

## Asset/Cooker E2E

```powershell
cmake --build --preset windows-vnext-debug `
  --target tina_asset_format_tests tina_asset_tests tina_scene_tests tina_assetc tina_catalog_validate tina_sample_asset --parallel 2 -- /nr:false
out\build\windows-msvc-vnext\bin\Debug\tina_asset_format_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_asset_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_assetc.exe --out <catalogRoot> --recipe <recipe>
out\build\windows-msvc-vnext\bin\Debug\tina_catalog_validate.exe --root <catalogRoot> --typed-payloads
out\build\windows-msvc-vnext\bin\Debug\tina_sample_asset.exe --frames=60 --catalog=<catalogRoot>
```

multi-mesh glTF Cooker 的库级测试与 `tina_sample_3d` 双 mesh 产品 E2E（3D-001）均已完成：distinct
mesh/material AssetId、Prefab dependency、AssetId→Handle→registry-owned binding→packet-local ref 与双 mesh
binding 可验证。Opaque3D 已做
baseColor/MR/normal 贴图 **采样**、material factors 与 World directional lights 的逐帧 snapshot；完整
PBR/IBL/shadow、point/spot light 与 culling 仍后置。

`ASSET-SEC-001` 的定向门禁是 `GltfCookTests.*`：覆盖主/外部文件 64MiB 上限、短 buffer、strict UTF-8
与 percent-decoded traversal、root 内和逃逸 symlink/junction、bufferView/accessor/count/overflow、PNG
dimension/decoded-byte budget、multi-primitive 与完整 PBR fixture 回归。Windows 还直接运行完整
`tina_asset_tests`；Linux 至少用 GCC13 重新编译 `tina_asset` 与该测试 TU，并从仓库 build tree 链接同一
测试后端运行 filter。不得用只编译 TU 或临时 probe 代替测试结果。

```powershell
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_asset_tests.exe `
  --gtest_filter=GltfCookTests.* --gtest_color=no
```

`SpriteAnimationClip` 覆盖 payload/schema、Catalog typed view、dependency contract 与
`SpriteAnimator2D` 的 Once/Loop/PingPong、暂停、倍速和大 delta；`tina_sample_2d` 再提供
`Idle -> Walk -> HitWall` 的产品状态证据。

TileMap 当前最小回归覆盖：root schema v3 与 `TileMapChunk` v1 round-trip（含 layer/object visibility、
chunk ref、parent/layer/coord/extent/non-empty）；旧 schema、重复/零稳定 ID、非法几何/UTF-8 拒绝；recipe
显式 layer block与旧裸 `row` 拒绝；Cooker 在 Manifest 发布前验证 eager Tileset、deferred chunk dependency
和所有非零 tile localId。Runtime 还覆盖仅加载 visible chunk、demand shift 的 cancel/unload、capacity
transaction、retain overflow 自动淘汰与 demand-recency LRU、Asset async active-read move/destroy 生命周期，
以及 residency generation 驱动 dirty cache 重建；render/collision 全部显式传 layer ID。desired load window
单独超 capacity 仍必须验证旧 active set 不变。产品 smoke 必须看到 `objectLayerConsumed=true`、
`objectLayerObjects=2`、`tileMapStreamRequests/Committed/Resident=2`。

```powershell
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_asset_format_tests tina_asset_tests tina_sample_2d --parallel 2 -- /nr:false
cmake --build --preset windows-vnext-bgfx-physics2d-debug `
  --target tina_physics2d_tests --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_asset_format_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_asset_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-physics2d\bin\Debug\tina_physics2d_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
```

## 2D-FX

`tina_scene_tests` 是 `ParticleSystem2D` / `Trail2D` 的模块门禁：覆盖 Create 固定 PMR allocation 与失败
回收、300帧无 storage growth、固定 seed 可复现、burst validation/capacity/stable-key failure 原子性、
stable key 过期后不复用、update preflight 零发布，以及向 `RenderSceneWriter` 的 lifetime/size/color/width
extraction 和 writer capacity failure。A3 进一步覆盖 weak Sprite Handle 保留、空 handle 发布前拒绝、
stale/wrong-kind/missing/zero resolver fail closed、空集合不解析与 Trail 每次非空 extraction 只解析一次。
专项为 Particle 18/18、Trail 13/13；A5 新增 3D Handle 边界后，完整 `tina_scene_tests` 为91/91。

```powershell
cmake --build --preset windows-vnext-debug --target tina_scene_tests --parallel 2 -- /nr:false
out\build\windows-msvc-vnext\bin\Debug\tina_scene_tests.exe --gtest_color=yes
```

product-2d gate 还必须构建并直接运行 `tina_scene_tests`，再验证 sample 的 `evidenceSchema=16`。通用结构化
字段包括 `spriteBindingTextures=2`、`spriteTextureLeasesAcquired=2`、
`spriteTextureRetirementsAccepted=2`、`spriteBindingRegistryReleased=true`、
`spriteTextureHandlesInvalidated=2`、`spriteTextureRetirementRecords=2`、
`spriteTextureRetirementReleased=2`、`spriteTextureRetirementLive=0`、
`spriteBindingResolverHits>0`、`tileMapSpriteBindingResolverHits>0`、`particleSpriteBindingResolverHits>0`、
`trailSpriteBindingResolverHits>0`、`sprite2DLightingConfigured=true`、`pointLight2DCount=2`、
`shadowOccluder2DCount=2`、
`sceneLightingFrames=renderExtractions`、
`particleCapacity=12`、`particleRandomSeed=1414090305`、`particleEmitted=10`、
`trailCapacity=8`、`trailSegmentsCreated=3`、`trailBreaks=1`，以及32字符小写 hex
`fxInitialFingerprint`；该 fingerprint 的哈希输入内部 schema 为2。Theme 门禁还要求
`uiThemeDemoRequested=true`、`uiThemeSwitches=2`、`uiThemeButtonActivations=0`、
`uiThemeFinalLight=false`。TreeView 门禁还要求13个 logical item、12个 materialized slot、两次 selection、
最终 stable key `402`/index `12`、滚动、Theme paint 与 Tree/TreeItem selected semantics。300帧 gate 进一步要求
`particleExpired=4`、`particleActive=6`、
`particleExtracted=6`、`trailActive=3`、`trailExtracted=3`。这些字段证明固定配置下的 simulation/extract
数量与初始状态指纹；`pixelCaptureOk` 和单机 golden/非空窗口证据仍单独证明可见输出。

## UI 与视觉

UI 逻辑门禁至少包括：

- generation/root ownership、容量失败与 PMR 回收；
- layout/hit/paint/semantics 的事务提交；
- 50,000 层 structure/layout/hit/paint 非递归 stress 与 popup stable publication；
- routed input、default action、consume/claim、reset/cancel；
- Button/Checkbox/Slider/ProgressBar/RadioButton/TextEdit 的 kind/property/错误路径；
- UTF-8、IME preedit/commit、Glyph atlas 与 FreeType adapter；
- Runtime phase facade 过期、sticky error 与跨 root 拒绝。

Visual 证据必须同时记录 sample 返回码、client-area 尺寸、是否强制终止、blank/black 比例、字体来源和
截图。初始化白帧不得作为稳定画面；截图通过也不能替代 UIA/AT-SPI。

2D/3D 产品 sample 还提供 backend primary-frame 的单机像素门禁。第一次运行采集
`pixelFingerprint`，第二次把同机、同 backend、同尺寸、同资源版本的值传回 exact golden 参数：

```powershell
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0 `
  --expect-pixel-fingerprint=<first-run-pixelFingerprint>

out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0 `
  --expect-pixel-fingerprint=<first-run-pixelFingerprint>
```

通过必须同时看到 `pixelCaptureOk=true`、非零 width/height/bytes、
`pixelGoldenChecked=true` 与 `pixelGoldenMatched=true`。该 exact hash 是 machine-local gate，不得跨 GPU、
driver 或 backend 复制为通用金标。需要可人工查看的 PNG 与 blank/black 分析时，再使用
`tools/windows/CaptureSampleWindow.ps1 -RequireNonBlank`；内置 hash 不替代该窗口证据。

## Physics2D 与 Audio

完整 product-2d 图直接运行：

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_scene_tests tina_physics2d_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests `
           tina_ui_freetype_tests tina_audio_tests tina_audio_miniaudio_tests tina_sample_2d --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_render_integration_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_freetype_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_physics2d_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_audio_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_audio_miniaudio_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe `
  --frames=300 --frame-delay-ms=0 --ui-theme-demo --ui-tree-demo
```

miniaudio null-device 证明 adapter callback/mix/lifecycle，不证明真实扬声器质量。Physics2D Release bench
是模块基线。统一 schema 使用 `tina_bench`（ADR 0018 schema v1；共享机仅 provisional）。

Audio N7 模块门禁除 one-shot/pitch/pan/fade 外，还覆盖 fixed ring 的 submit/mix/EOF/retire、非 EOF
underrun 静音恢复、整块原子 submit 与 wrap、completion ring 满时 terminal debt、mix-slot reuse ABA、
wrong-owner/bounded shutdown、active callback reader quiescence、terminal absorbing、fractional stream 最小
容量和 terminal priority。关键测试入口包括：

- `PcmStreamSubmitsMixesSignalsEofAndRetires`；
- `PcmStreamUnderrunOutputsSilenceWithoutFakingEof`；
- `PcmStreamSubmitIsWholeChunkAtomicAndWraps`；
- `PcmStreamCancelCompletionSurvivesFullCompletionRing`；
- `DeferredStreamTerminalDoesNotClearReusedMixSlot`；
- `PcmStreamValidationWrongThreadAndShutdownAreBounded`；
- `PcmStreamCancelConcurrentWithRealtimeMixRetiresExactlyOnceBeforeReuse`；
- terminal absorbing/priority、cancel-vs-EOF exactly-once 与 concurrent shutdown 回归。

`MiniaudioDeviceTest.NullBackendConsumesBoundedStreamEofAndCancel` 验证 adapter 作为 realtime consumer 的
EOF/Cancel 路径。产品 300帧还要求 `audioStreamQueued/submitted/eof/mixed/drained/stopped/retired=true`、
submitted/consumed frame 数一致且 `audioStreamUnderrunFrames=0`；当前 product evidence schema 为16。

Physics2D N2 的模块门禁覆盖：`createBody/createShape` 独立 generation、多 Box/Circle/Capsule shape/body、
shape 单独销毁、sensor enter/exit、Distance joint create/query/destroy、body 级联退休 shape/joint、
wrong-world/stale/capacity/PMR rollback，以及 TileMap bridge/CharacterController coexistence。当前直接运行
`tina_physics2d_tests` 为 29/29；产品 300 帧还要求 `physicsSensorEnters>0`、
`physicsSensorExits>0`、`physicsJointReady=true`。

Windows 同轮 product-2d 拓扑由 `tools/windows/RunProduct2dGate.ps1` 固化（TEST-002）：包含
`tina_scene_tests` 的上述测试 executable 全部 exit 0 后，再跑 sample 300 帧并校验
`productGate=bgfx-physics-freetype-audio` 与 schema 16 Theme、TreeView、Sprite owner/retirement、
TileMap/Particle/Trail Handle resolver，以及两盏 `PointLight2D`、两条 `ShadowOccluder2D` 和逐帧 lighting
extraction 字段。

Windows 同轮 product-3d 拓扑由 `tools/windows/RunProduct3dGate.ps1` 固化（TEST-003）：默认使用
`windows-msvc-vnext-bgfx-ui-freetype`，直接构建并运行 Core、Scene、AssetFormat、Asset、bgfx Render、
UI、Runtime UI、UI Render bridge 与 FreeType 测试，再执行300帧 `--ui-theme=dark --ui-theme-demo`。
schema 5 同时断言双 mesh、3个跨 Material 共享 PBR Texture、packet-local resolver 600/600、Mesh/Texture
retirement records Released、3个 World light/`sceneLightingFrames=300`/registry 生命周期、7 Panel/13 Label、Button/Checkbox/Slider/ProgressBar/
ListView/TreeView 创建、2次 collection step、stable keys、继承 chrome、Dark→Light→Dark、100% progress、
final-present capture 与 ledger 归零：

本次实际两轮验收命令（两轮均为脚本默认 `windows-msvc-vnext-bgfx-ui-freetype` topology）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunProduct3dGate.ps1 `
  -SkipConfigure
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunProduct3dGate.ps1 `
  -SkipConfigure -SkipBuild -OutJson artifacts\gates\product-3d.json
```

2026-07-29 基线 `e61b6a00` + N16.4 工作树的实际验收先使用 `-SkipConfigure` 执行 build、测试与 sample，
再使用 `-SkipConfigure -SkipBuild -OutJson artifacts\gates\product-3d.json` 无重建复验并写报告。脚本 build exit 0；
`tina_tests` 335/335、`tina_scene_tests` 91/91、`tina_asset_format_tests` 59/59、
`tina_asset_tests` 204/204、`tina_render_scene_tests` 39/39、`tina_render_bgfx_tests` 61/61、
`tina_ui_tests` 282/282、`tina_runtime_ui_tests` 85/85、`tina_ui_render_integration_tests` 15/15、
`tina_ui_freetype_tests` 3/3；`tina_sample_3d` 300帧 exit 0，schema 4 校验通过。
无重建复验写出 `artifacts/gates/product-3d.json`，报告 `ok=true`。

2026-07-30 当前 DirectionalLight3D 提交候选使用 MSVC 14.50 `cl.exe` 和上述第一条命令完成同轮
build、测试与300帧 sample。`tina_tests` 336/336、`tina_scene_tests` 96/96、
`tina_asset_format_tests` 59/59、`tina_asset_tests` 204/204、`tina_render_scene_tests` 41/41、
`tina_render_bgfx_tests` 63/63、`tina_ui_tests` 488/488、`tina_runtime_ui_tests` 97/97、
`tina_ui_render_integration_tests` 16/16、`tina_ui_freetype_tests` 3/3、`tina_ui_uia_tests` 12/12；
`tina_sample_3d` 300帧 exit 0，最终输出
`product-3d gate ok schema=5 frames=300 theme=dark-light-dark collections=list-tree`。

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
一致性（GLFW metrics，非 COM DPI API）；**字体 identity fingerprint**（`fontFingerprint`：env
`TINA_UI_FONT_PATH` / repo fixture path、`sha256`、`freeTypeLikelyOn`、`identity`；baseline schema 3；
与 baseline 不一致时默认 fail，`-AllowFontFingerprintMismatch` 可 provisional 跳过 ROI 比对）。

**未证明：** OS 显示缩放 100/150/200% 真机多 DPI 金标；多显示器混 DPI；跨 GPU 像素金标。

## Linux 与 sanitizer

Linux 门禁必须记录 compiler、stdlib、CMake、vcpkg baseline、display backend 和 sanitizer 环境。
Clang preset 通过 chainload 固定 libstdc++15；Ubuntu 默认旧工具链不能冒充正式结果。

### Docker Desktop（Windows 宿主）— GCC13 Null 子图

见 [m12-evidence-linux.md](m12-evidence-linux.md)。快捷：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxDockerGate.ps1 `
  -Gate gcc13-null -OutJson artifacts\gates\test-001-linux-gcc13-null.json
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
