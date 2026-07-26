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
| `tina_scene_tests` | Entity/Transform/2D/3D component/extraction、ParticleSystem2D、Trail2D | 基础图 |
| `tina_render_scene_tests` | Camera2D/3D、culling、sort/batch、world picking | 基础图 |
| `tina_asset_format_tests` | Cooked/Manifest 与 typed payload schema | 基础图 |
| `tina_asset_tests` | Catalog、AssetSystem、Handle/Lease、Cooker、upload/retirement | 基础图 |
| `tina_audio_tests` | backend-neutral AudioEngine/voice/bus/command/completion | 基础图 |
| `tina_platform_glfw_tests` | GLFW adapter 与 WindowSurface | `TINA_BUILD_PLATFORM_GLFW=ON` |
| `tina_render_bgfx_tests` | bgfx lifecycle、2D/3D/UI geometry/resource | `TINA_BUILD_RENDER_BGFX=ON` |
| `tina_ui_freetype_tests` | FreeType font open/measure/rasterize | `TINA_BUILD_UI_FREETYPE=ON` |
| `tina_ui_uia_tests` | Windows UIA property mapping / provider lifecycle | `TINA_BUILD_UI_UIA=ON` (Windows) |
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
cmake --build --preset windows-vnext-debug --target tina_tests -- /m:1 /v:m
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
  binding 后映射 key。A1 本身未证明 registry；A2 已补该证据，但 FX/TileMap/3D Handle 迁移仍未完成。

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_scene_tests tina_sample_2d -- /m:1 /v:m
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe `
  --frames=300 --frame-delay-ms=0
```

## Sprite2D Binding Registry A2

`ASSET-HANDLE-SCENE-2D-A2` 的模块门禁归属 `tina_asset_tests`，产品闭环归属 2D product gate：

- `Sprite2DBindingRegistry.hpp` header isolation；Create 容量/PMR failure 与 owner-thread 约束；
- Texture2D Handle/GPU texture validation、exact duplicate、同 AssetId conflict、固定容量，以及 device
  instance namespace 内 key 唯一、单调不复用；
- 多个 registry 共享同一 device 时 key distinct、独立 resolve/unbind；
- backend register failure 不发布记录且不消费 device key，unbind failure 保留记录可重试，成功 unbind
  才删除记录；
- Sprite 必须是 live Handle 且 Cooked 文件恰有一个 required `Texture2D` dependency；stale/wrong-kind/
  missing/multiple dependency、unbound/stale texture 都返回0；
- registry 不拥有 GPU/Lease/retirement；产品 State RAII 证明先 unbind 两项 binding，再 destroy 两张 texture；
- product evidence schema 10：`spriteBindingTextures=2`、`spriteBindingsReleased=2`、
  `spriteBindingTexturesDestroyed=2`、`spriteBindingResolverHits>0`，且 TileMap/selection/Particle/Trail
  使用 registry 动态 key。

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_asset_tests tina_scene_tests tina_sample_2d -- /m:1 /v:m
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_asset_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe `
  --frames=300 --frame-delay-ms=0
```

A2 保留 A1 resolver ABI；FX/TileMap 仍存 key，3D 与 `FrameResourceRef` 未迁移，不能据此把
`ASSET-HANDLE-SCENE` 总项标为 Done。

## 产品样例的证据边界

| Sample | 证明 | 不证明 |
| --- | --- | --- |
| `tina_sample_null` | EngineHost、固定帧、Headless/Null lifecycle | GLFW、GPU、可见 UI |
| `tina_sample_platform` | GLFW window/input/WindowSurface + NullRender | bgfx 绘制 |
| `tina_sample_desktop` | Desktop bootstrap、真实 bgfx surface、UI pass | 2D/3D 产品内容 |
| `tina_sample_asset` | Catalog→Task→AssetSystem→ReadyGpu/Lease | 可见纹理/mesh |
| `tina_sample_2d_infrastructure` | CPU/Null Camera2D/Sprite extraction | Catalog/产品 UI/GPU |
| `tina_sample_2d_infrastructure_bgfx` | fixture Sprite2D + UI overlay | 正式 Catalog TileMap 产品 |
| `tina_sample_2d` | Catalog TileMap v3 root + deferred TileMapChunk；每帧 visual=10/collision=20 demand→pump→commit 与 resident 证据；gameplay objects=30，消费 point 101/rectangle 102；SpriteAnimationClip/Animator、fixed-capacity Particle/Trail、Gameplay、UI、Audio；Physics 含 multi-shape API、sensor enter/exit 与 Distance joint；schema 10 含 Sprite registry 注册/释放/纹理销毁/resolver hits，final-present RGBA8 capture 与单机 exact golden；feature 图含 Physics/FreeType/miniaudio | Registry transaction/PMR/owner-thread 压力（由 `tina_asset_tests` 证明）、Particle/Trail 事务性与 PMR 压力（由 `tina_scene_tests` 证明）、TileMap retain-capacity LRU 压力（由 `tina_asset_tests` 证明）、priority IO/editor/自动 gameplay 生成、更多 shape/joint、Linux、跨 GPU golden、完整 UI 工具包 |
| `tina_sample_3d_extraction` | CPU/Null Perspective/Mesh extraction | 可见 GPU 3D |
| `tina_sample_3d_infrastructure` | procedural fixture Cube/depth/instance | Cooked product mesh |
| `tina_sample_3d` | 双 mesh glTF→Cooked→GPU→Prefab→Scene→bgfx；baseColor/MR/normal 贴图采样、material factors、唯一0..4 directional-light 提交（产品3灯）、shutdown retirement drain、final-present RGBA8 capture 与单机 exact golden | 完整 PBR/IBL/shadow/light component、Scene AssetHandle 产品化、跨 GPU golden |

`tina_sample_2d_tilemap_bgfx` 是 `tina_sample_2d` 的兼容 ALIAS；新脚本使用正式 target 名。

## Asset/Cooker E2E

```powershell
cmake --build --preset windows-vnext-debug `
  --target tina_asset_format_tests tina_asset_tests tina_scene_tests tina_assetc tina_catalog_validate tina_sample_asset -- /m:2 /v:m
out\build\windows-msvc-vnext\bin\Debug\tina_asset_format_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_asset_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_assetc.exe --out <catalogRoot> --recipe <recipe>
out\build\windows-msvc-vnext\bin\Debug\tina_catalog_validate.exe --root <catalogRoot> --typed-payloads
out\build\windows-msvc-vnext\bin\Debug\tina_sample_asset.exe --frames=60 --catalog=<catalogRoot>
```

multi-mesh glTF Cooker 的库级测试与 `tina_sample_3d` 双 mesh 产品 E2E（3D-001）均已完成：distinct
mesh/material AssetId、Prefab dependency 与 product meshKey 1/2 binding 可验证。Opaque3D 已做
baseColor/MR/normal 贴图 **采样**、material factors 与有界0..4 directional lights；完整 PBR/IBL/shadow
仍后置。

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
  --target tina_asset_format_tests tina_asset_tests tina_sample_2d -- /m:2 /v:m
cmake --build --preset windows-vnext-bgfx-physics2d-debug `
  --target tina_physics2d_tests -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_asset_format_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_asset_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-physics2d\bin\Debug\tina_physics2d_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
```

## 2D-FX

`tina_scene_tests` 是 `ParticleSystem2D` / `Trail2D` 的模块门禁：覆盖 Create 固定 PMR allocation 与失败
回收、300帧无 storage growth、固定 seed 可复现、burst validation/capacity/stable-key failure 原子性、
stable key 过期后不复用、update preflight 零发布，以及向 `RenderSceneWriter` 的 lifetime/size/color/width
extraction 和 writer capacity failure。

```powershell
cmake --build --preset windows-vnext-debug --target tina_scene_tests -- /m:2 /v:m
out\build\windows-msvc-vnext\bin\Debug\tina_scene_tests.exe --gtest_color=yes
```

product-2d gate 还必须构建并直接运行 `tina_scene_tests`，再验证 sample 的 `evidenceSchema=10`。通用结构化
字段包括 `spriteBindingTextures=2`、`spriteBindingsReleased=2`、`spriteBindingTexturesDestroyed=2`、
`spriteBindingResolverHits>0`、
`particleCapacity=12`、`particleRandomSeed=1414090305`、`particleEmitted=10`、
`trailCapacity=8`、`trailSegmentsCreated=3`、`trailBreaks=1`，以及32字符小写 hex
`fxInitialFingerprint`。300帧 gate 进一步要求 `particleExpired=4`、`particleActive=6`、
`particleExtracted=6`、`trailActive=3`、`trailExtracted=3`。这些字段证明固定配置下的 simulation/extract
数量与初始状态指纹；`pixelCaptureOk` 和单机 golden/非空窗口证据仍单独证明可见输出。

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
           tina_ui_freetype_tests tina_audio_tests tina_audio_miniaudio_tests tina_sample_2d -- /m:1 /v:m
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_scene_tests.exe --gtest_color=yes
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
submitted/consumed frame 数一致且 `audioStreamUnderrunFrames=0`；当前 product evidence schema 为10。

Physics2D N2 的模块门禁覆盖：`createBody/createShape` 独立 generation、多 Box/Circle/Capsule shape/body、
shape 单独销毁、sensor enter/exit、Distance joint create/query/destroy、body 级联退休 shape/joint、
wrong-world/stale/capacity/PMR rollback，以及 TileMap bridge/CharacterController coexistence。当前直接运行
`tina_physics2d_tests` 为 29/29；产品 300 帧还要求 `physicsSensorEnters>0`、
`physicsSensorExits>0`、`physicsJointReady=true`。

Windows 同轮 product-2d 拓扑由 `tools/windows/RunProduct2dGate.ps1` 固化（TEST-002）：包含
`tina_scene_tests` 的上述测试 executable 全部 exit 0 后，再跑 sample 300 帧并校验
`productGate=bgfx-physics-freetype-audio` 与 schema 10 Sprite binding、Particle/Trail 字段。

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
