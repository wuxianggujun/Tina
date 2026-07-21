# GoogleTest 与验证

## 规则

- 测试框架固定为 GoogleTest；
- `TINA_BUILD_TESTING=ON` 时 CMake 始终生成 vNext 基础 `tina_tests`；Legacy ON 时另行生成
  `tina_legacy_tests`，旧 Core/Engine/UI 测试只进入这个 Legacy-only executable，禁止和 vNext UI/Runtime
  混入同一最终二进制；M7-C1b/C1c-a/C1c-b1/C1c-b2 与 b3d2/b3e 低层 updater UI 树、布局、命中快照、
  point query 与 synthetic route 核心另有独立 `tina_ui_tests`，M7-C1c-b3b/b3c/b3d1/b3d2/b3e
  Runtime→vNext UI producer、primary-window owner、layout coordinator、scoped Game SDK access、Pointer Button claim bridge
  与 D0 primary-window UIDisplayList handoff、后续 root-scoped Game SDK listener facade、M10-A42
  world pointer Action Mapping payload 另有独立
  `tina_runtime_ui_tests`；SolidFill committed paint → Render SolidQuad DisplayList 的窄桥另有独立
  `tina_ui_render_integration_tests`；启用
  `TINA_BUILD_PLATFORM_GLFW` 时另外生成 `tina_platform_glfw_tests`，启用
  `TINA_BUILD_RENDER_BGFX` 时另外生成 `tina_render_bgfx_tests`，其中 M9-B 的 Opaque3D fixture 与
  M9-C 的 Sprite2D fixture 几何/预算测试仍留在私有 bgfx adapter 测试进程内，不注册额外测试调度；
  M8-A Scene World/Transform 另有独立 `tina_scene_tests`，M8-B 2D 与 M9-A 3D RenderScene extraction 共用独立
  `tina_render_scene_tests`；M10-A0 Cooked Header/Manifest wire-format 校验另有独立
  `tina_asset_format_tests`；M10-A1 owning CatalogSnapshot / DAG cycle 另有独立
  `tina_asset_tests`；启用 `TINA_BUILD_PHYSICS2D` 时 M11-A0 另生成 `tina_physics2d_tests`。这些专项均不并入
  基础 `tina_tests`；
- 构建完成后直接运行对应 GoogleTest executable，任一返回码非0即失败；
- Visual Studio 多配置构建把测试运行时隔离到 `bin/<Config>`，禁止 Debug/Release GTest DLL 共用目录；
- 同一 Visual Studio build tree 的 Debug/Release 构建串行执行，禁止并发启动两个 MSBuild 门禁；
- 测试依赖由固定 vcpkg baseline 提供；
- 测试日志不得包含路径外的敏感环境变量或凭据。

## 已验证基线

当前迁移结果截至 2026-07-21 的 `codex/tina-vnext-runtime`：

| 平台 | 构建图 | 配置 | GoogleTest | 状态 |
| --- | --- | --- | --- | --- |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | vNext 至 D2 + M8-B 2D + M9-A 3D extraction | Debug C++23 | 213/213 | 本轮直接通过 RenderScene22/22；Null、2D infrastructure、3D extraction样例各300帧，3D记录4 submitted/3 visible/1 culled/2 batches、一次aspect变化与资源归零。UI115/115、Runtime→UI60/60、UI→Render12/12、Scene19/19、GLFW26/26、bgfx16/16和Desktop为前一轮已验证结果；M9-A未重新截图且本身无GPU画面 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M9-C 私有 bgfx Sprite2D fixture + 2D/UI 样例 | Debug C++23 | 43/43 | `tina_render_bgfx_tests` 直接通过；`tina_sample_2d_infrastructure_bgfx --frames=300 --frame-delay-ms=0` 通过，记录5个 Sprite、2个 UI panel、`renderResourceLedgerBalanced=true`。截图确认 Sprite 旋转、透明、flip 与 UI overlay。该结果只证明 fixture/infrastructure，不证明 M10-A1+ 产品资产路径、正式 `tina_sample_2d`、Asset/Texture/Sprite、TileMap、Box2D 或中文文本 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M9-C 私有 bgfx Sprite2D fixture + 2D/UI 样例 | Release C++23 | 43/43 | `tina_render_bgfx_tests` 直接通过；`tina_sample_2d_infrastructure_bgfx --frames=300 --frame-delay-ms=0` 通过并记录相同生命周期与资源账本。Debug 的 D3D11 `RefCount=3` 提示未在 Release 出现；画面证据沿用同代码路径的 Debug 截图，不把退出码冒充截图证据 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A0 `tina_asset_format` Cooked/Manifest wire format | Debug/Release C++23 | 14/14 | `tina_asset_format_tests` 两配置均直接通过；覆盖 identity/hash 强类型、固定 little-endian schema、borrowed view、确定性 object path、limit/overflow/layout/padding/排序/依赖校验和300次重复解析。A0 明确允许跨资产 cycle，完整 DAG、XXH3 计算、文件 IO、AssetSystem、Cooker/cgltf 与产品资产路径后置 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A1 `tina_asset` CatalogSnapshot | Debug/Release C++23 | 17/17 | `tina_asset_tests` 两配置均直接通过；覆盖 empty/single/multi entry、Manifest bytes 销毁后仍有效、binary search hit/miss、依赖 target index、chain/diamond DAG、两节点/多节点 cycle、深链不递归、容量/配置/PMR 失败回滚、move、析构归还、300 次创建/销毁与 header isolation。A1 不实现 Handle/Lease、文件 IO、Task、GPU upload、XXH3、cgltf 或产品资产路径 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A2a Core ContentHash digest | Debug/Release C++23 | 218/218 | 基础 `tina_tests` 含 ContentHashDigest 5 项；`tina_asset_format_tests` 16/16 含 payload verify 成功/篡改失败。公共头无 `XXH*` token。A2a 不实现文件 IO、Handle/Lease、Catalog 磁盘加载或 Cooker |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A2b Catalog file load | Debug/Release C++23 | 223/223 + 19/19 | 基础 `tina_tests` 含 ReadFile 5 项共 223；`tina_asset_tests` 19/19 含 Manifest 文件→Snapshot。不实现 Handle/Lease、async IO 或 Cooker |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A2c Cooked object file load | Debug/Release C++23 | 223/223 + 23/23 | 基础 `tina_tests` 223；`tina_asset_tests` 含 CookedAssetFile 4 项。不实现 Handle/Lease |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A2d Catalog load order | Debug/Release C++23 | 223/223 + 27/27 | `tina_asset_tests` 含 load order chain/diamond/missing/empty。不实现 Handle/Lease |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A2e Batch cooked load | Debug/Release C++23 | 223/223 + 29/29 | `tina_asset_tests` 含批量依赖序加载与失败回滚。不实现 Handle/Lease |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A2f Catalog package validate | Debug/Release C++23 | 223/223 + 34/34 | 基础 `tina_tests` 223；`tina_asset_tests` 含 package validate 5 项：完整包、缺文件、大小不匹配、同尺寸内容篡改的 metadata/full 差异、非法 UTF-8 root；full 模式强制 ContentHash。不实现 Handle/Lease 或 CLI |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A2g Catalog package open | Debug/Release C++23 | 223/223 + 37/37 | `openCatalogPackage` 覆盖完整包打开、校验失败不发布、不安全相对路径拒绝；`tina_asset_tests` 37/37。不实现 Handle/Lease |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A2h catalog validate CLI | Debug/Release C++23 | CLI smoke | `tina_catalog_validate --help` 与缺失 root 失败 JSON；与 `tina_asset_tests` 同图。不实现 Handle/Lease |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A2i package summary | Debug/Release C++23 | 223/223 + 39/39 | `buildCatalogPackageSummary` totals/entry 行；`tina_asset_tests` 39/39；CLI `--list-entries`。不实现 Handle/Lease |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A2j catalog load plan | Debug/Release C++23 | 223/223 + 42/42 | `planCatalogLoads` 依赖先序、相对路径、缺 id/空请求；`tina_asset_tests` 42/42。不实现 Handle/Lease |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A2k plan all + CLI plan | Debug/Release C++23 | 223/223 + 43/43 | `planCatalogLoadsAll`；`tina_asset_tests` 43/43；CLI `--plan-loads`/`--asset-id`。不实现 Handle/Lease |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A2l load from plan | Debug/Release C++23 | 223/223 + 45/45 | `loadCookedAssetsFromPlan` 成功序加载与 plan 不匹配拒绝；`tina_asset_tests` 45/45。不实现 Handle/Lease |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A2m package one-shot load | Debug/Release C++23 | 223/223 + 47/47 | `loadCookedAssetsFromPackage` 成功链与失败不发布；`tina_asset_tests` 47/47。不实现 Handle/Lease |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A2n empty-request + CLI load | Debug/Release C++23 | 223/223 + 48/48 | 空请求加载全部；CLI `--load-assets`；`tina_asset_tests` 48/48。不实现 Handle/Lease |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A2o pipeline e2e + fixture | Debug/Release C++23 | 223/223 + 50/50 | 共享夹具 + open/plan/load/validate/summary 端到端；`tina_asset_tests` 50/50。不实现 Handle/Lease |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A2p plan byte total | Debug/Release C++23 | 223/223 + 52/52 | `totalCookedFileBytes` 空/正常/溢出；`tina_asset_tests` 52/52；CLI `plannedCookedFileBytes`。不实现 Handle/Lease |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A2q batch byte budget | Debug/Release C++23 | 223/223 + 54/54 | `maxTotalCookedFileBytes` 超预算拒绝、等预算通过；`tina_asset_tests` 54/54。不实现 Handle/Lease |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A3 AssetStore Handle/Lease | Debug/Release C++23 | 223/223 + 58/58 | publish/acquire/tryGet/unload 延迟/stale/capacity；`tina_asset_tests` 58/58。无 GPU retirement |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A4 AssetSystem sync | Debug/Release C++23 | 223/223 + 61/61 | bind/load/dedupe/budget/partial-fail rollback；`tina_asset_tests` 61/61。无 async |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A5 request/pump | Debug/Release C++23 | 223/223 + 65/65 | Queued/Loading/Ready/Failed；request+pump 预算；缺文件 Failed；队列满；`tina_asset_tests` 65/65。无 Task worker/GPU |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A6 bounded Task + async asset pump | Debug/Release C++23 | 225/225 + 66/66 | BoundedTaskSystem scheduleIo/postMain/pumpMain；AssetSystem+taskSystem IO 读盘 Ready；`tina_tests` 225；`tina_asset_tests` 66。无 CPU pool/GPU |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A7 Desktop Task + Null UploadTicket | Debug/Release C++23 | 227/227 + 66/66 | Desktop BoundedTask 默认；NullUploadLedger submit/poll/retire；`tina_tests` 227。无 bgfx fence |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A8 ReadyGpu coordinator | Debug/Release C++23 | 227/227 + 68/68 | ReadyCpu→ReadyGpu；UploadQueued 期间 CPU lease；`tina_tests` 227；`tina_asset_tests` 68。无 bgfx 资源 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A9 AssetSystem GPU pipeline | Debug/Release C++23 | 227/227 + 70/70 | sync load→ReadyGpu；request/pump+IO+GPU→ReadyGpu；`tina_asset_tests` 70。无 bgfx 资源 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A10 tina_sample_asset | Debug/Release C++23 | sample smoke | Catalog 包 request/pump 至 ReadyGpu；JSON ok。无画面绘制 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A11 wire writers | Debug/Release C++23 | 见实现门禁 | write→parse round-trip；unsorted reject；`tina_asset_format_tests` |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A12 writeFile + assetc | Debug/Release C++23 | 230/230 + 71/71 | `writeFile` 原子写；publish→open；`tina_assetc`+validate；`tina_tests` 230；`tina_asset_tests` 71 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A13 retirement + e2e | Debug/Release C++23 | 见实现门禁 | unload/cancelUpload retire ticket；`tina_assetc`→`tina_sample_asset --catalog=`；`tina_asset_tests` 73 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A14 recipe cook | Debug/Release C++23 | 见实现门禁 | recipe→publish→open；`tina_assetc --recipe`→sample；`tina_asset_tests` 75 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A15 Texture/Sprite payload | Debug/Release C++23 | 23/23 format | Texture2D/Sprite write/parse/cooked round-trip；`tina_asset_format_tests` 23 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A16 typed 2D product path | Debug/Release C++23 | 见实现门禁 | cook→load→parse Texture/Sprite；assetc/sample typed2d；`tina_asset_tests` 76 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A17 catalog helpers | Debug/Release C++23 | 见实现门禁 | openAndBindCatalog + kind find + typed views；`tina_asset_tests` 77 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A18 typed payload validation | Debug/Release C++23 | 见实现门禁 | verifyTypedPayload 接受/拒绝；CLI --typed-payloads；`tina_asset_tests` 79 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A19 inline typed recipe | Debug/Release C++23 | 见实现门禁 | texture2d/sprite 内联 recipe→publish→typed validate→sample；`tina_asset_tests` 80 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A20 sprite UV render bridge | Debug/Release C++23 | 见实现门禁 | makeSpriteRenderInput UV/size；render scene 22；`tina_asset_tests` 81 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A21 asset→render CPU pipeline | Debug/Release C++23 | 见实现门禁 | cook→load→RenderScene UV commit；`tina_asset_tests` 82 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A22 bgfx sprite texture sample | Debug C++23 bgfx | 43/43 | Sprite2D s_tex + default white；`tina_render_bgfx_tests` 43；2d sample 60帧 ok |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A23 GPU texture upload API | Debug C++23 | 232/232 + 83/83 | Null create/bind/destroy；uploadTexture2DFromCooked；bgfx 43 + sample ok |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A24 catalog 2D sample | Debug C++23 bgfx | sample smoke | `tina_sample_2d_catalog --frames=60` texturesUploaded=1；bgfx 创建 8x8 RGBA8 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A25 CPU TaskGroup | Debug C++23 | 234/234 + 83/83 | scheduleCpu + TaskGroup waitIdle；cpuWorkerCount=0→NotSupported |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A26 Tileset/TileMap payload | Debug C++23 | 26/26 format | write/parse/cooked round-trip；`tina_asset_format_tests` 26；asset 83 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A27 inline tileset/tilemap recipe | Debug C++23 | 84/84 asset | recipe tileset/tilemap→publish→typed validate→load parse；`tina_asset_tests` 84 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A28 TileMapInstance | Debug C++23 | 86/86 asset | Create/setTile/chunk revision/solid AABB；`tina_asset_tests` 86 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A29 chunk extract + grid collision | Debug C++23 | 88/88 asset | visible chunk cull/empty skip；grid solid query；`tina_asset_tests` 88 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A30 tile chunk sprite emit | Debug C++23 | 90/90 asset | emit UV/center；RenderScene commit；off-camera skip；`tina_asset_tests` 90 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A42 ActionMapper last-presented Camera2D world pointer payload | Debug C++23 | 237/237 + 74/74 + 30/30 | `tina_tests`、`tina_runtime_ui_tests`、`tina_render_scene_tests` 均直接通过；覆盖 UI consume/claim non-penetration、Pressed/Released sample、无 Camera 结构化失败、viewport no-hit、0 fixed-step 锁存及 EngineHost last-presented E2E |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A43 `tina_sample_2d` Tile selection consumer | Debug C++23 | 240/240 + 10/10 + product smoke | `tina_tests` 新增 3 个 sample-private consumer 用例；A39/A42 组合聚焦 10/10；`tina_sample_2d --frames=300` 返回0，默认无合成点击时 selection JSON 计数为0，product-2d 资源/lifecycle 门禁保持通过 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10-A44 selection highlight + seed gate | Debug C++23 | 6/6 Sample2D + 10/10 A39/A42 + product smoke | `Sample2DTileSelectionTest` 6 项（含 highlight/seed/scripted Pressed）；默认 300 帧 hits=0/无高亮；`--seed-tile-selection=1,1 --frames=300` hits≥1、`lastHighlightSprites=1`、`selectionHighlightSprites==renderExtractions`；A39 仍由 `tina_runtime_ui_tests` |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M10 收口 product-2d pointer/selection | Docs + tip `70618808` | A39–A44 闭环 | 默认 smoke 不点；seed CLI 为受控门禁；完整 cooker/cgltf Deferred；默认不开 A45 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M11-B0 Camera2D projection resolve | Debug C++23 | 8/8 + product smoke | `Camera2DProjectionTest`：FixedWorldHeight ppm/aspect、resize、PixelPerfect integerScale、snap 强制、0×0/非法拒绝、normalized viewport；`tina_sample_2d` JSON `cameraProjectionResolves`/`lastCameraActualPpm` |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M11-B1 TileChunk dirty cache | Debug C++23 | 6/6 asset + product smoke | `TileChunkDirtyCacheTests`：首帧 rebuild→次帧 hit；`setTile` 只脏一 chunk；300 帧 pan+edit；`tina_sample_2d` JSON `chunkDirtyRebuilds<<visible`、末帧 `lastChunkDirtyRebuilds=0` |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M11-B2 Camera follow + interp | Debug C++23 | product smoke | `tina_sample_2d`：`cameraFollowUpdates>0`、`maxCameraCenterX>min+0.25`、JSON center/interpolation；FixedWorldHeight=4m 可 pan |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M11-A7 `tina_audio` Disabled lifecycle | Debug C++23 | `tina_audio_tests` | Create Disabled、voice generation/capacity/stale、幂等 shutdown、错线程拒绝、PMR 归零；header isolation 编入同 target |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M11-A8 command/completion + bus | Debug C++23 | `tina_audio_tests` | Play/Stop → pump Started/Stopped；destroy 后 pending → RejectedStale；满队列 CapacityExceeded；Master/Music/SFX volume/mute/effectiveGain |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M11-A9 miniaudio null device | Debug C++23 | `tina_audio_miniaudio_tests` | null backend start/stop、callback 计数>0、bundle engine+device |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M11-A10–A13 audio product path | Debug C++23 | audio 15 + miniaudio 9 | 可选编解码；clip bind；mixRealtime；playOneShot；WAV decode→mix e2e |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M11-A14 EngineHost optional Audio | Debug C++23 | `EngineHostRunTest.OptionalAudio*` | factory 注入 Disabled Audio；phase 可见；host pump 后 Started；默认无 factory 时 audioEngine()==null |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M11-A15 Desktop/sample Audio inject | Debug C++23 | product-2d sample smoke | Desktop+sample factory 注入；JSON `audioEnginePresent`/`audioStartedObserved` |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M11-A16 sample miniaudio null SFX | Debug C++23 | product-2d + audio-miniaudio | null device callbacks>0、mixFramesRendered>0、productGate 含 audio；hermetic 不要求扬声器 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M11-A17 AudioClip cooked payload | Debug C++23 | `tina_asset_format_tests` | write/parse round-trip、stereo、非法 geometry、cooked asset + content hash |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M11-A18 AudioClip cooked playback | Debug C++23 | `tina_asset_tests` AudioClipCooked* | parseAudioClipFromCooked + playOneShot + mix；wrong kind 拒绝 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M11-A19 recipe AudioClip + sample lease | Debug C++23 | CatalogCookTests + product-2d sample | recipe `audioclip sine`；JSON `audioFromCatalogLease`/`audioClipFrameCount` |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M11-A20 recipe WAV file cook | Debug C++23 | CatalogCookTests.AudioClipFileWav* | `audioclip … file click.wav` → cooked 8kHz/8 frames PCM |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M11-C0 UI Checkbox | Debug C++23 | `tina_ui_tests` UICheckbox* | create/toggle click/keyboard、setChecked 静默、wrong kind 拒绝 |


















| Windows 11 / MSVC 19.50 / CMake 4.2.3 | M8-A `tina_scene` World/Transform | Debug/Release C++23 | 19/19 | `tina_scene_tests` Debug 与 Release 均直接运行通过；覆盖 generation/owner、keep-world/keep-local、父销毁/显式子树销毁、非递归20,000层传播、宽树删除、固定容量/PMR回滚与稳定构造错误、overflow/shear、四元数和错线程读写；Linux Scene 图尚未运行 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | vNext 至 D2 + M8-B 2D + M9-A 3D extraction | Release C++23 | 213/213 | 本轮直接通过 UI115/115、Runtime→UI60/60、UI→Render12/12、Scene19/19、RenderScene22/22；Null、2D infrastructure与3D extraction样例各300帧，3D记录4 submitted/3 visible/1 culled/2 batches、一次aspect变化与资源归零；GLFW/bgfx/Desktop沿用前序D2证据 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | Legacy ON 与 C1c-b3b vNext 共存构建，Legacy/vNext 测试进程隔离（前序门禁） | Debug/Release C++23 | 185/185 + 43/43 | `tina_tests` 185/185、`tina_legacy_tests` 43/43，均直接运行通过 |
| Ubuntu 22.04 / GCC 13.4 / CMake 4.2.3 | Legacy ON 与 C1c-b3b vNext 共存构建，Legacy/vNext 测试进程隔离（前序门禁） | Debug C++23 | 185/185 + 43/43 | `tina_tests` 185/185、`tina_legacy_tests` 43/43，均直接运行通过；构建保留旧源码/EASTL 既有 warning |
| Ubuntu 22.04 / GCC 13.4 | vNext 至 SolidFill committed paint、Render SolidQuad DisplayList 与 UI→Render bridge，Legacy/真实 bgfx backend 关闭 | Debug C++23 | 205/205 | UI92/92、Runtime→UI46/46、UI→Render12/12、Null样例300帧；X11 GLFW23/23为前序门禁 |
| Ubuntu 22.04 / Clang 22.1.8 + libstdc++15.2 | 同上，ASan/UBSan/LSan（detect_leaks=1、halt_on_error=1） | Debug C++23 | 205/205 | UI92/92、Runtime→UI46/46、UI→Render12/12、Null样例300帧且零 sanitizer 诊断；X11 GLFW23/23与精确第三方 XIM suppression为前序门禁 |

GLFW adapter 和 bgfx adapter 测试是独立 executable，不能把多个进程伪写成单个合并测试数。当前测试拓扑为：

| 构建图 | 基础 GoogleTest | GLFW 专项 GoogleTest | bgfx 专项 GoogleTest | 状态 |
| --- | ---: | ---: | ---: | --- |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 Debug | 213/213 | 26/26 | 43/43 | 本轮直接通过 UI115/115、Runtime→UI60/60、UI→Render12/12、Scene19/19、RenderScene22/22；Null、Desktop、3D fixture 与2D/UI fixture样例均运行300帧并返回0，两个 fixture 样例资源账本平衡；Debug bgfx 窗口样例保留已知 D3D11 `RefCount=3` 提示，截图确认 Sprite2D/UI fixture 可见 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 Release | 213/213 | 26/26 | 43/43 | 本轮直接通过 UI115/115、Runtime→UI60/60、UI→Render12/12、Scene19/19、RenderScene22/22；Null、Desktop、3D fixture 与2D/UI fixture样例均运行300帧并返回0，两个 fixture 样例资源账本平衡；Release 无 D3D11 `RefCount=3` 提示 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 production-style | 测试 target 关闭 | 不构建 | 不构建 | 更早门禁：`TINA_BUILD_TESTING=OFF`，GLFW样例300帧返回0 |
| Ubuntu 22.04 / GCC 13.4 vNext Null；前序 GLFW X11 | 205/205 | 23/23（历史 C1c-b3a） | 未运行 | 最新 UI92/92、Runtime→UI46/46、UI→Render12/12与Null样例300帧；adapter样例保留历史门禁 |
| Ubuntu 22.04 / Clang 22.1.8 + libstdc++15.2 vNext Null + ASan/UBSan/LSan；前序 GLFW X11 | 205/205 | 23/23（历史 C1c-b3a） | 未运行 | 最新 UI92/92、Runtime→UI46/46、UI→Render12/12与Null样例300帧，零 sanitizer 诊断；C1c-b3a GLFW仅精确抑制第三方 `_XimOpenIM`，13次/5304 B |
| Ubuntu 22.04 / GCC 13.4 + GLFW X11/Wayland 双后端 | 183/183 | 22/22 | 未运行 | 通过；嵌套 Weston 9 强制 Wayland 与 Xvfb 强制 X11 均通过基础、专项与300帧样例 |
| Ubuntu 22.04 / Clang 22.1.8 + libstdc++15.2 + GLFW X11/Wayland 双后端 + ASan/UBSan/LSan | 183/183 | 22/22 | 未运行 | 通过；基础测试无 suppression且Null样例300帧；Wayland专项与样例 suppression 命中0，X11专项命中12次/4896 B、样例命中1次/408 B |

当前 M9-A 在 MSVC 19.50 / CMake 4.2.3 Windows Debug/Release 下均直接通过基础213/213、
UI115/115、Runtime→UI60/60、UI→Render12/12、Scene19/19与RenderScene22/22，并运行
Null/2D/3D extraction样例各300帧；2D样例验证每帧3个Sprite，3D样例验证每帧4 submitted/
3 visible/1 culled/2 batches、一次aspect变化，三者退出资源归零。Windows Debug 另沿用前序
GLFW专项26/26、Platform样例300帧、bgfx专项16/16与Desktop样例连续3次各300帧。
新增 iconify 回归证明最小化时 logical extent 保持正值、framebuffer 仍为 `0x0`；本轮没有重新截图，
Desktop画面正确仍引用前序D2产品证据。
上一轮完整 Windows D2 门禁仍是 Debug/Release 基础207/207、UI92/92、Runtime→UI53/53、
UI→Render12/12与Null样例300帧；bgfx专项16/16和 D3D11 Intel Iris Xe Desktop样例也均通过，Debug
1200帧含截图检查，Release 300帧 clean，Debug RefCount=3 为已记录第三方 debug layer 提示；
GLFW专项25/25与Platform样例300帧保留前序门禁。Linux GCC 13.4 与 Clang sanitizer 最新
均通过基础205/205、UI92/92、Runtime→UI46/46、UI→Render12/12与Null样例300帧；Clang 未报告
sanitizer 诊断；Linux 对 D1/D2 的真实 bgfx UI pass、Game SDK paint facade 和可见 Desktop panel 尚未复验。前序 C1c-b3a 的23项 GLFW
专项覆盖 Button/Wheel 事件时 logical position、非有限坐标拒绝、Move 语义边界和
GLFW `A → Button/Wheel → B` 的帧末位置隔离。
Windows D2 bgfx 构建另有独立 `tina_render_bgfx_tests` 16/16，Debug/Release 均实际返回0；D1 新增5项覆盖
SolidQuad CPU geometry、ABGR、32位索引、容量失败原子性与300次 storage reuse。D2 在 D3D11 Intel
Iris Xe 上完成 Desktop 4-panel 可见 UI：Debug 1200帧截图验证 alpha/scissor，Release 300帧输出
clean status ok。该结果覆盖当前无纹理 SolidFill UI pass、factory/lease 回滚和 Desktop smoke，不覆盖后续
Scene/Pass Scheduler/submission ticket、Text/Glyph、Widget 默认行为，也不声明 resize、最小化、恢复的真实自动化通过。
M7-C1b/C1c-a/C1c-b1/C1c-b2 UI 树、布局、committed hit snapshot、纯 point query、synthetic route 与
b3d2 root-scoped child 创建、b3e listener claim 请求，以及最新11项 SolidFill paint 使用独立
`tina_ui_tests`。后续 listener extension 新增 root-scoped/cross-root 原子注册、callback move 释放 root
回滚和 callback move 销毁 Context death test。Button default action 切片新增14项后达到109/109；dirty-subtree
b4a 再新增6项 reuse/回退测试，当前 Windows 11 / MSVC 19.50 Debug/Release 均为115/115；Linux GCC 13.4
与 Clang 22.1.8 + libstdc++15.2 ASan/UBSan/LSan 仍为92/92，Clang 无 sanitizer 诊断，本轮未重跑 Linux。初次 GCC 暴露的 routed-pointer callback `requires`
名称可见性问题已修复，二次 GCC/Clang 构建无 warning。
M7-C1c-b3b/b3c 使用另一个独立 `tina_runtime_ui_tests`。前12项 producer 用例覆盖 null Context canonical `None`、raw ordinal
hole、63/64 bit 边界、Button/Wheel 事件时位置、reset/cancel/非 Pointer 不路由、不伪造 Up、该历史切片 claims 为
`None`、ActionMapper suppression、预留 reset slot、float 表示范围预检，以及300帧共用 supplied PMR 时
allocation count 不增长。supplied `memory_resource` 是私有借用依赖，必须比 producer 活得更久。
失败门禁先让 root Move listener 产生1次 side effect，再让后续深层 Button route 因 route path capacity
失败；staging 不发布、上一份成功 view 保持，attempted watermark 已推进，同帧 retry 被拒且 callback 仍为1，
证明既不回滚也不重放 listener side effect。新增8项 owner 用例覆盖 headless lazy bind、Context复用、
绑定后 primary window 消失或 generation 替换失败、metrics/scale/minimize 不重绑、幂等 shutdown/停止后拒绝、
错线程拒绝，以及 PMR allocation 失败后事务式重试。该 target 直接执行 GoogleTest，不注册 CTest。
C1c-b3c 基线在 Windows 11 / MSVC 19.50 / CMake 4.2.3 Debug/Release、Linux GCC 13.4 Null 与
Clang 22.1.8 + libstdc++15.2 Null sanitizer 均已直接运行20/20并返回0；Clang 无 sanitizer 诊断。
该独立 executable 不能用基础 `tina_tests` 或 `tina_ui_tests` 数量代替。
M10-A39 在同一 `tina_runtime_ui_tests` 追加产品级 pointer non-penetration 门禁：
`ProductButtonClickDoesNotPenetrateWorldPointerAction` 走 Button default action + `ActionMapper`
合成 Primary Down/Up：命中 Button 时 UI 激活且世界 pointer Simulation Action 无 Pressed；
未命中时世界 Action 正常 Pressed。`tina_sample_2d` smoke 仍不合成点击，selection 计数保持显式空语义。
M10-A42 在同一 target 追加 `WorldPointerActionMappingTest`：覆盖 consumed/claimed pointer 不要求
last-presented camera、未消费 pointer 缺 camera 以 `LifecycleInvariantViolation` 失败且 Press/Release
状态可重试、按事件坐标锁存 Pressed/Released `worldPointerSample`、viewport miss 产生 `hit=false`
no-hit，以及0 fixed-step 帧跨后续 camera/resize 保持已锁存 sample。基础 `tina_tests` 另以
`EngineHostRunTest.WorldPointerActionPayloadUsesLastPresentedCamera2D` 证明 Runtime 正式路径使用上一份
成功 present 的 Camera，而不是同帧 extraction 的新 Camera。

M10-A43 的样例消费者测试与生产代码共用 `samples/2d_tilemap_bgfx/TileSelection.hpp`：覆盖
`Pressed + hit` 的 cell 映射和锁存 provenance、viewport miss、负数/地图外/半开上边界、NaN、缺 payload、
`Released/Cancelled`、错误 action，以及空 catch-up transition batch。GLFW product smoke 不注入真实点击，
因此默认 `worldPointerPresses`/`tileSelectionHits` 为0；真实 UI non-penetration 仍由 A39 合成测试证明。

M10-A44 在同一 helper 上追加 `makeSelectionHighlightSprite`（cell 中心、inset、sortingLayer=2、非法
grid/cell/spriteKey 结构化失败）与 `makeScriptedWorldCellPress`/`seedTileSelection`（锁存 cell 中心
world sample，经同一 `consumeTileSelectionTransitions` 消费）。`tina_sample_2d` 默认 `--frames=300`
仍无选中/无高亮；`--seed-tile-selection=cellX,cellY` 在 first fixedUpdate 注入一次 scripted Pressed
edge，要求 `tileSelectionHits>=1`、`lastHighlightSprites=1` 且
`selectionHighlightSprites==renderExtractions`。不注入 OS/GLFW pointer；A39 Button non-penetration
继续由 `tina_runtime_ui_tests` 证明。

M10 收口：A39–A44 为 product-2d **pointer / selection 产品闭环**（见 `docs/roadmap.md` 收口清单）。
两道样例门禁语义分离——默认 300 帧证明生命周期/资源/行走/Physics/UI 接线且 selection 可为空；
`--seed-tile-selection` 证明「选中 → JSON → 高亮 emit」。完整 cooker CLI、cgltf、厚 world-pick
Game SDK 与删 Legacy 不在本闭环内，**默认不开 M10-A45**。

M8-A 使用独立 `tina_scene_tests`，当前 19 项覆盖：World 固定容量与 PMR 错误回滚/稳定构造错误、Entity generation/owner/stale
校验、keep-world/keep-local reparent、父销毁与显式子树销毁、Local/World Transform 组合、非递归深树与宽树
删除、非有限/零 quaternion、四元数归一化、overflow/shear 拒绝和 owner-thread 读写。该 target 只链接
`Tina::Scene`/`Tina::Core`，不依赖 EnTT、GLM、GLFW 或 bgfx；
它证明 Scene 基础生命周期，不证明 Scene component integration、Asset 或产品 2D 样例。

M11-A0/A1/A2/A3 使用独立 `tina_physics2d_tests`。A0 覆盖 World/desc 输入预校验、单线程固定 step、
Body/Shape generation owner/stale、跨 World 拒绝、Box body 原子创建与容量回滚、pose snapshot、
幂等 shutdown、move、错线程拒绝、PMR 归零和 public header isolation。A1 追加 begin/end contact
发布、destroy 后 end tombstone、begin overflow 标志与空 step 清空 view。A2 追加 `overlapAabb`
排序/overflow、`castRay`/`castRayClosest` 与无效 query 拒绝。A3 追加 deferred command FIFO 应用、
满队列拒绝、stale skip 与 deferred destroy。该 target 只通过 `Tina::Physics2D` 消费 Box2D 的
PRIVATE link dependency，不把 Box2D include 暴露给公共头。Windows `windows-msvc-vnext-physics2d`
上 Debug/Release 均为 **25/25**（A5 grid body 3 项 + A6 TileMap bridge 2 项）。M11-A4 另提供
`tina_physics2d_bench`（Release 推荐）。正式 2D 产品画面门禁仍未实现；已有 `tina_sample_desktop` /
`tina_sample_2d_infrastructure_bgfx` 仍是独立视觉基线，不能冒充 TileMap+Box2D 产品截图。

M8-B 使用独立 `tina_render_scene_tests`，覆盖 RenderScene 固定容量分配失败、事务 rollback、Camera/Sprite
输入校验、透明/隐藏剪枝、旋转保守裁剪、pixel snap、稳定 layer/order/entity/insertion 排序、300 帧零新增
分配和 move/destruction 释放。`tina_sample_2d_infrastructure --frames=300` 通过 Headless Platform 与
recording Null device 从 standalone World 提取 Camera2D 和3个 Sprite，并记录每帧提交/呈现、统计和退出回收；
它是 CPU/Null infrastructure 闭环，不证明 M9-C 的可见 bgfx fixture、正式可见 Sprite、中文 Label/Button、
world picking、TileMap、Asset/Cooker 或正式 2D 产品门禁。

M9-A 继续使用 `tina_render_scene_tests`，当前22项覆盖：Perspective Camera pose/quaternion/FOV/near-far/aspect
校验、正 scale Mesh3D transform 与 local sphere bounds、近远/侧面 frustum culling、
透明/隐藏/容量失败、material/mesh/submesh/double-sided/depth/entity/insertion 稳定排序、相邻 batch finalize、
多次 begin/rollback/view 生命周期与 storage allocation rollback。`tina_sample_3d_extraction --frames=300`
使用 standalone `Scene::World` 和 recording Null device，验证4 submitted、3 visible、1 culled、2 batches、
resize aspect 更新、退出顺序和 `liveResources=0`；它不创建 bgfx 资源、不显示 GPU 画面，也不能替代
M9-B 的 procedural Cube/depth 或 M10-A1+ 的 Cooked glTF 产品门禁。Runtime 生命周期测试另覆盖当前帧
framebuffer aspect 注入及 `0x0` 时回退 logical extent。

M9-B 当前最小实现把最小 Opaque3D fixture 接入私有 `tina_render_bgfx`，使用同一个
`tina_render_bgfx_tests` executable 增加 `BgfxOpaque3DGeometryTest` 与 transient frame budget 测试：canonical `P3_N3_UV2`
Cube 顶点/索引和 outward winding、空 Opaque3D frame、fixture world transform/baseColor instance 写入、
unsupported fixture key 明确失败，以及 instance output 容量不足时不写 partial data。`tina_sample_3d_infrastructure`
只在 GLFW+bgfx 图构建，通过 Desktop bootstrap 默认/门禁运行300帧，当前每帧提交3个 procedural Cube 和
1个 instance batch。M9-B 当时运行全 surface clear View 0、depth-tested Opaque3D View 1、UI pass 与真实
bgfx transient instance buffer；进程返回码、实际截图和退出日志必须分别验收，不能用结构化样例成功行
替代画面或资源回收证据。它不证明 Cooked Mesh/Material/Texture/Prefab、glTF、
通用 Pipeline/PBR、Pass Scheduler 或 3D 产品门禁。
当前 backend 在 `noexcept` shutdown 中按创建顺序的严格逆序销毁 Tina-owned IB、VB 和三个 program，
并在调用 `bgfx::shutdown()` 前校验内部 `liveResources == 0`；账本不平衡会终止进程而不是强制清零。
因此样例只在 `EngineHost` 析构成功后输出 `renderResourceLedgerBalanced=true`。M10 引入资源/retirement
系统后仍须用下面的完整 backend-neutral 字段替换这一最小 fixture 证据。

M9-C 当前最小实现把 Sprite2D fixture 接入私有 `tina_render_bgfx`，仍使用同一个
`tina_render_bgfx_tests` executable：新增 `BgfxSprite2DGeometryTest` 覆盖 P2/UV2/ABGR vertex layout、空
Sprite frame、fixture 排序展开、旋转/缩放、flip UV、unsupported fixture key、缺失 Camera 拒绝、
输出容量不足不写 partial data 和300次 caller-owned storage 复用；transient budget 测试又覆盖
Sprite2D+UI index pool 汇总/溢出。当前固定 View 0 clear、1 Opaque3D、2 Sprite2D、3 UI 只是私有
fixture view 编号，不是 Pass Scheduler。Windows Debug/Release `tina_render_bgfx_tests` 均43/43。
两配置的 `tina_sample_2d_infrastructure_bgfx --frames=300 --frame-delay-ms=0` 均通过 Desktop bootstrap 运行300帧，
记录5个 Sprite、2个 UI panel、UI root 释放、`EngineHost` 销毁和 `renderResourceLedgerBalanced=true`；
截图确认 Sprite 旋转、透明、flip 与 UI overlay。该结果只证明 fixture/infrastructure，不证明
Asset/Texture/Sprite 产品路径、正式 `tina_sample_2d`、TileMap、Box2D、中文文本或 M10-A1+。

M10-A0 使用独立 `tina_asset_format_tests`，当前14项覆盖严格小写 `AssetId` canonical text、
`ContentHash` 与身份类型隔离、112B Cooked Header、64B Manifest Header、56B Manifest Entry、24B
Dependency Entry、payload borrow、确定性 object path、magic/schema/enum/flags/reserved、truncation、
overflow、alignment、zero padding、EOF、caller hard limit、entry/dependency 排序、重复/self/missing target、
kind mismatch 与 dependency range 完整覆盖。300次重复解析验证无内部状态漂移。A0 的 cycle 用例刻意
证明 parser 不拒绝跨资产环；完整 DAG cycle 检测属于 M10-A1 `CatalogSnapshot`，不能把14/14
描述为 Cooker、异步加载或正式2D/3D资产产品门禁。

M10-A1 使用独立 `tina_asset_tests`，覆盖 owning 不可变 `CatalogSnapshot`、Manifest bytes 销毁后仍可
查询、AssetId binary search hit/miss、依赖 target entry index、合法 chain/diamond DAG、两节点与多节点
cycle、深链不递归、容量/非法配置/PMR 分配失败不发布部分 Snapshot、move 语义、析构归还 PMR、
300 次创建/销毁与 public header isolation。A1 不实现 Handle/Lease、文件 IO、Task、GPU upload、XXH3、
cgltf 或正式资产产品门禁。

M7-C1c-b3d1 在同一测试拓扑中新增三组契约：focused `UIContextCapacityConfig` validator 与
`EngineConfig` 在任何 factory 前拒绝非法容量；primary owner 确实使用配置容量；layout coordinator
只取 logical extent，在 `updateUI` 后、Render 前每个严格递增 `PlatformFrameId` 至多尝试一次。
Headless 窗口/Context 双缺席是成功 no-op；identity、容量或 layout 失败不发布新 snapshot、阻断 Render，
并消费该 frame attempt，禁止同帧 retry 重放 retained mutation。该切片为独立 Runtime→UI process
新增9项测试；Windows Debug/Release、Linux GCC 与 Clang sanitizer 均直接通过29/29。上面的20/20
继续只表示已验证的 b3c 历史基线。

M7-C1c-b3d2 继续使用同一测试拓扑：Platform/GLFW 覆盖 `initialPrimaryWindowMetrics()` 的 Headless
空 seed、隐藏窗口 startup seed、首帧 metrics 保留、停止后拒绝与 WindowSurface seed 一致性；Runtime/UI
覆盖 invalid startup metrics 不发布且可重试、startup bind one-shot、explicit Headless 后拒绝晚到窗口、
metrics revision 回退拒绝、startup 首份 structure/layout/hit commit、Game SDK 在 `onEnter` 创建 root/panel/
label/button 并在 `updateUI` 修改 retained style、onEnter 后失败先重置 State owner 再回滚模块；
独立 capability 测试覆盖 phase expiry、`PrimaryWindowUIUnavailable` sticky、首个 tree 错误 sticky、跨线程拒绝、
moved-from facade 过期与 `abortPhase()`。这些用例证明 scoped retained tree access，不证明 Label 文本、
Button 默认 action、Focus/Capture/Modal、Game SDK paint authoring 或可见 UI。后续切片已补齐
primary Pointer Button default action、Game SDK paint authoring 与 SolidFill 可见路径，但不改变 b3d2 当时边界。

M7-C1c-b3e 在同一测试拓扑中新增 held primary Pointer Button claim bridge。Move/Button/Wheel listener API 都可请求
接管 primary window 上 `PrimaryPointerId` 的仍 held `PointerButton`；Runtime 按 final snapshot 过滤 release/cancel/reset 后不再 held、
非 primary 或重复的 claim，capacity 失败时不发布新 claims。同帧 PointerDown 触发的 claim 即使事件未
consume，也会在 `ActionMapper` 拦截 Gameplay。b3e 当时的 Windows Debug/Release、Linux GCC 13.4 与
Linux Clang sanitizer 均直接通过 Runtime→UI46/46与 UI81/81；最新 Linux UI 已随 paint 切片增至92/92，
Runtime→UI仍为46/46。Clang 无诊断。Key/Gamepad/axis claims 仍后置。

D0 继续使用同一 Runtime→UI 测试拓扑，新增 primary-window UIDisplayList handoff 覆盖。Runtime 在
layout/paint commit 后、Render submit 前构建 `RenderFrame::primaryWindowUIDisplayList`；Headless、
0 framebuffer 与 suspended surface 发布空 list；容量/identity/metrics 失败不保留旧 publication 或
截断 list。D2 在同一 executable 中新增 Game SDK scoped `setBoxPaint()` facade 覆盖，验证正常写入
committed paint、phase expiry、wrong-context/stale-generation sticky failure 和后续 mutation 阻断。
Windows Debug/Release 的完整 D2 门禁为53/53。

后续 Game SDK listener extension 在同一拓扑新增2项 capability 测试：token 跨 registration phase 保持
有效，以及 cross-root 注册 sticky/no-slot；基础 `tina_tests` 另增1项 EngineHost E2E，验证 listener
在 ActionMapper 前发布 claim、claim-only 路径不产生 Gameplay transition，并在 `onExit()` 先释放 token。
Button default action 后续在同一拓扑增加 primary Pointer activation、cancel/reset 与 facade 测试。
旧的 208/208 结果属于 M8-A 前序记录，不能作为本轮 M8-B 结果；本轮以实际直接运行的 executable 输出为准，Linux 未在本轮重跑。
X11 在隔离 X server 下运行。GCC Wayland 门禁由 Xvfb 托载
Weston 9 `x11-backend` 并提供 `wl_seat`；移除 `DISPLAY` 后断言
`glfwGetPlatform() == GLFW_PLATFORM_WAYLAND`，再运行专项测试和300帧样例。同一双后端产物
还在移除 `WAYLAND_DISPLAY` 后由 Xvfb 强制 X11 复验22/22与300帧。配置/构建成功不能冒充窗口测试通过。

纯 Weston headless 在不提供 `wl_seat` 时会触发项目锁定 GLFW 3.4 的已知初始化崩溃。
该问题不是 Tina 回归，当前门禁也不声明支持无 seat compositor；Wayland 环境必须是真实
session 或显式提供 `wl_seat` 的受控 compositor。

前序 C1c-b3a Clang X11 的基础 `tina_tests` 在**无 suppression**条件下通过185/185。只有会初始化 GLFW/X11 的
专项测试与样例使用 `cmake/sanitizers/lsan-x11.supp` 中唯一的 `leak:_XimOpenIM`：Ubuntu 22.04
libX11 在 GLFW 调用 `XCloseIM` 后保留 XIM allocation，当前23项专项测试共13次/5304 B；样例1次/408 B
是上一产品门禁结果。
抑制按第三方符号精确匹配，Tina allocation 仍由 LSan 阻断；不得增加宽泛的 module/category
suppression来隐藏 Tina 泄漏。

前序 Clang 22 Wayland 双后端产物的基础测试也在 ASan/UBSan/LSan 下**无 suppression**
通过183/183，Null样例通过300帧。带 `wl_seat` 的嵌套 Weston 强制 Wayland 后，专项22/22和样例300帧
通过，`_XimOpenIM` 抑制匹配计数为0。同一产物强制 X11 后专项22/22与样例
300帧再次通过，仅精确匹配 `_XimOpenIM`：专项12次/4896 B、样例1次/408 B。

这组 sanitizer 门禁插桩 Tina 自有 target，但 vcpkg 提供的第三方 GLFW 本身未被
sanitizer 插桩。因此结果能验证 Tina 代码、边界交互与生命周期，不宣称完整覆盖
GLFW 内部实现。

当前 C1c-b3e Headless 构建的 `tina_sample_null` 已在 Windows Debug/Release、Linux GCC 13.4 与
Linux Clang 22 ASan/UBSan/LSan 连续运行300帧；Clang 无诊断。M6-A/M7-A 历史构建还曾在 Linux 连续运行10,000帧，均
返回0，并验证 `IGameState::onExit` 与 `IGameApplication::onShutdown` 恰好一次。该样例组合
Headless Platform、Disabled TaskSystem 与 NullRenderDevice，不加入或链接 GLFW、bgfx、EnTT、
FreeType、miniaudio、SDL/SDL3；它不证明真实窗口、GPU、Scene/Asset/Audio 或 Game-facing/visible UI pipeline
已经可用。

以下是 2026-07-16 的迁移前完整平台历史基线（含 Button action 生命周期修复）：

| 平台 | 工具链 | 配置 | GoogleTest | 状态 |
| --- | --- | --- | --- | --- |
| Windows 11 | VS 2026 18.4.3 / MSVC 19.50.35717 | Debug | 50/50 | 通过 |
| Windows 11 | VS 2026 18.4.3 / MSVC 19.50.35717 | Release | 50/50 | 通过 |
| Ubuntu 22.04 | GCC 11.4 | 单配置门禁 | 50/50 | 通过 |
| Linux | Clang + ASan/UBSan | 迁移前无可复现 preset | 未验证 | 历史缺口 |

本批已经重新配置和构建 Legacy ON Debug 共存图，并直接执行135/135。菜单、
2D/UI、3D 四条 Debug 路径均完成300帧并正常返回0；日志确认 Scene、Audio、Input、
Event、Window 正常关闭，3D vertex/index buffer 已释放，未留下 Tina 进程。这证明主循环
和退出资源链路通过，不等同于新的截图级画面验收或实体手柄兼容性验收。迁移前表中
GCC 11.4 与旧 Clang 的 Linux 数据仍是历史证据。

## 当前自动化覆盖与 vNext 门禁

- Core 当前：C++23 `std::expected` Result/Status、稳定 Error domain/code、origin/native code/context
  chain、ScopeExit noexcept invoke/move、EnumFlags `std::to_underlying`、Assert、强类型 Duration、
  可注入 Monotonic Clock、固定步钳制/time scale/最多4步/丢弃与余量、基础类型和 Legacy
  Compatibility，以及 MemoryTag、并发 MemoryTracker、Counting PMR、无回退 FrameArena；公共
  memory/error/time/id 头另有逐头独立编译门禁；
- Core vNext 待补：完整 Metric frame/lifetime reset、Trace 开关、Unicode 路径与原子 IO、
  原子写失败恢复、Ensure/CrashContext；严格 UTF-8 scalar/NUL 校验和 owner-aware generation
  ID/Pool 已完成；
- Core 专用结构：GenerationPool 的 fixed storage、stale/wrong-owner、构造回滚、析构和 wrap
  helper 已完成；FrameArena 对齐/reset/OOM/高水位/零回退已完成；StaticVector/InlineFunction
  尚未实现，只在出现真实消费者时加入；
- Task 后续：有界队列、QueueFull/停止后拒绝、TaskGroup 取消与 barrier、owner/generation 迟到任务、
  异常不逃出线程、IO/CPU executor 隔离和确定性合并；
- Runtime 时间：新 FixedStepAccumulator 已覆盖固定步长、真实 delta 钳制、time scale、插值、
  最大追赶步、超额整步丢弃、非零余量、reset 和非法输入不改状态；Legacy FixedStepTicker
  继续覆盖旧 Application 的异常步消费；
- Runtime M6-A：完整 factory bundle/config 在产生副作用前校验；Clock/Platform/Task/Render 的
  failure、success-null 与 throw 覆盖逆序回滚；Ready Host 直接析构、startup transaction、
  run-once、0/1/4 fixed steps、当帧退出仍完成 extraction/UI/submit/present、失败清理及300帧
  Null Runtime 均有直接 GoogleTest；EngineConfig 还覆盖 `maximumStepsPerFrame > 4` 的硬拒绝，
  EngineHost Create/run 为 `noexcept` 边界；
- Platform/Input M7-A：有界 `PlatformFrameView`、严格 UTF-8 owning text arena、最终 Window/Input/
  Gamepad snapshot、保序 raw transition、overflow reset 与 Platform lifecycle batch；只接受
  `PrimaryPointerId`，Gamepad snapshot 强制同 owner/slot 唯一，connect/disconnect/cancel/reset 时序与
  最终 registry 一致；Action Mapper 覆盖 UI consumption/claim 注入、Frame/Simulation domain、0/1/4
  fixed-step、跨帧 active/suppressed source 与最终 held snapshot、窗口/手柄 generation 切换和
  overflow reset；`PlatformEventDispatcher` 覆盖 RAII generation token、自取消、自销毁、重入与异常；
  EngineHost 会在任何 Game callback 前拒绝恶意超限 backend frame；
- Platform/GLFW：私有 `GLFW_NO_API` hidden create transaction、单进程 backend lease、generation
  Window registry、Keyboard/Pointer/Focus/resize/close/committed UTF-8 text producer 已落地；专项测试
  覆盖严格 title/mode/extent 校验、键鼠映射、Unicode codepoint、repeat 状态、失焦 synthetic release
  抑制、close 不发布 partial frame、失败 partial Poll 后双 stream reset/recovery，以及 resize 的单一
  metrics revision/lifecycle event；
- WindowSurface M7-B1：generation `WindowSurfaceId`、backend-neutral `RenderSurfaceState`、
  `WindowSurfaceSnapshot` identity/revision/suspended、move-only `NativeWindowSurfaceLease`、
  WindowSurface-aware factory composition、Render 创建失败/窗口发布失败逆序回滚、私有 Win32/X11/Wayland
  native binding 解码、source window/revision 单调与精确 `surfaceRevision + 1`、NullRender 连续300帧
  suspended maintenance、独立 `engineFrameIndex`/`submissionIndex` 已有直接测试；
- GLFW suspended pacing：专项测试连续300次从 suspended 路径调用 `glfwWaitEventsTimeout(1/60s)` 的
  可缩短测试 seam，不走 busy-loop；seam 在 `TINA_BUILD_TESTING=OFF` 的生成图与产物中不存在；
- Runtime/Platform 防护：`PlatformFrameBuilder::discardFrame()` 允许错误 Poll 后恢复；EngineHost
  wrong-owner-thread `run` 返回结构化错误且不消耗 run-once；
- Platform/Task/Render M6-A：Headless shutdown 后拒绝 poll，Disabled TaskSystem 始终 idle 且
  shutdown 幂等；NullRenderDevice 强制连续 frame index 和 submit/present 配对，300帧始终
  `liveResources == 0`；各模块公共头均有独立编译门禁；
- UI M7-C1a/C1b/C1c-a/C1c-b1/C1c-b2：`tina_ui_tests` 的16项 tree core 覆盖 generation `UINodeId`、`UIContext`
  capacity/create、`UIRootOwner` move/reset/destruction/off-thread release、tree updater owner 校验、
  结构 snapshot、header isolation 和 storage memory 回零；新增23项 layout 覆盖 style 校验、Flex-lite、
  viewport 重排、事务式容量回滚、stale generation、50,000节点非递归布局，以及300次无变化 commit
  零 UI PMR 分配；新增15项 committed hit snapshot 覆盖固定 PMR 容量、`Ignore`/`Targetable`、
  route ancestry、同一 view 内严格递增且唯一的 paint ordinal、双缓冲 view/revision、hit-only 0 layout、
  structure/layout/hit 事务回滚、stale generation、50,000节点与 PMR 回收；新增5项纯 point query
  覆盖反向目标选择、`Ignore` 穿透、world/clip 半开边界、非有限坐标 miss，以及连续300次查询
  无新增 supplied-PMR 分配和 UI 状态变化；新增16项 synthetic route 覆盖 Capture/Target/Bubble 顺序、
  stopPropagation、stopImmediatePropagation、dispatch 中 reset/add/destroy、generation-safe target invalidation、
  listener 容量原子失败与复用、route depth 容量失败无 partial callback、token move/context-destroyed/
  off-thread reset、callback root 自销毁、route 中 commit 拒绝、错误 context 销毁 death test、300次 route
  零新增 supplied-PMR 分配/不改变 committed state 和递归 route 拒绝。该 C1b2 历史门禁中，Windows MSVC 19.50 Debug/Release 与
  Linux GCC 13.4/Clang 22 sanitizer 均为75/75，当时 Linux 基础为183/183，Clang 无 sanitizer 诊断；初次 GCC
  暴露的 routed-pointer callback `requires` 名称可见性问题已修复，二次 GCC/Clang 构建无 warning；
- Game SDK listener extension：低层 UI 新增 root-scoped/cross-root atomic、callback move 释放 root
  rollback、callback move 销毁 Context terminate；Runtime capability 新增 token 跨 phase 与 cross-root
  sticky/no-slot；基础 EngineHost E2E 覆盖 claim-before-actions、claim-only suppression 与 onExit token reset。
  该历史切片的 Windows Debug 分别为 UI95/95、Runtime→UI55/55、基础208/208；
- UI dirty-subtree b4a：6项测试覆盖 clean sibling Measure/Arrange reuse、viewport 与 parent-style constraint
  full rebuild、Auto 祖先重算与 sibling 位移、Collapsed 子树 oracle，以及 paint candidate 失败后禁用下一次 reuse。
  `lastLayoutMeasuredNodeCount`/`lastLayoutArrangedNodeCount` 只统计进入对应调度的节点，不代表
  `buildLayoutOrder`、父级子节点、hit/paint snapshot 已完成完整 dirty-range pruning；Windows Debug 为115/115；
- UI paint 与 Render DisplayList：11项 `UIPaintSnapshotTest` 覆盖 straight sRGBA8 的确定性预乘、
  effective-visible/transparent 筛选、same-value/no-op、paint-only 0 layout/hit、四份 snapshot 原子回滚、
  `paintSnapshotCapacity` 与 supplied PMR；11项 `UIDisplayListTest` 覆盖单缓冲 view 生命周期、strict paint
  ordinal、SolidQuad/clip first-seen interning、相邻 batching、checksum、剪枝、sticky input/capacity failure
  与 rollback。Windows Debug/Release、Linux GCC/Clang 分别随 UI92/92与基础测试通过；D0 后 Windows
  基础为207/207，Linux paint/bridge 门禁仍为205/205；
- UI→Render integration：独立12项测试覆盖 logical/framebuffer extent 映射、floor origin/ceil nonzero end、
  half-open clamp、空 viewport、冗余 clip 省略、非有限/非法输入、paint order 与完整 builder transaction；
  Windows MSVC 19.50 Debug/Release、Linux GCC 13.4 与 Linux Clang 22 sanitizer 均为12/12，Clang
  无 sanitizer 诊断。它不覆盖 Runtime packet 或 bgfx UI Pass；
- Runtime→UI M7-C1c-b3b/b3c：private producer 的12项用例与 primary-window Context owner 的8项用例
  保持在独立 `tina_runtime_ui_tests`。`EngineHost` 在 Platform event dispatch 后 lazy bind/复用首个 primary
  `WindowId` 的 `UIContext`，随后路由并把 consumption/claims 交给 `ActionMapper`；基础测试另覆盖非有限
  Pointer 值在 game phases 前失败，以及第二帧 Window generation 变更在 submit 前失败并完整清理。该
  b3b/b3c 切片当时没有 Game SDK root/widget 创建入口，空 Context 下 consumption/claims 为 canonical
  `None`；b3d2 虽已补 root-scoped facade，该历史切片本身仍不证明 Widget 默认行为、DisplayList handoff 或可见 UI；
- Runtime→UI M7-C1c-b3d1：同一独立 executable 增加 UI capacity shared validation、EngineConfig
  pre-factory rejection、configured owner capacity、Headless no-op、logical viewport、一次 attempt、
  wrong owner/thread 与 snapshot 失败原子性；`EngineHost` 的提交点固定在 `updateUI` 后、Render submit 前。
  该切片证明 phase-driven layout commit，不证明 Game SDK root/updater、DisplayList handoff、Widget 或可见 UI；
- Runtime→UI M7-C1c-b3d2：同一独立 executable 增加 startup metrics seed、explicit bind、startup
  structure/layout/hit commit、Game SDK root builder/updater、phase capability sticky/expiry/abort。该切片证明
  `onEnter`/`updateUI` scoped retained tree mutation，不证明 Label 文本、Button 默认 action、Focus/Capture/Modal、
  DisplayList handoff、bgfx UI Pass 或可见 UI；
- Runtime→UI M7-C1c-b3e：同一独立 executable 增加 held primary Pointer Button claim bridge。Move/Button/Wheel
  listener 可请求接管仍 held 的 primary pointer button；Runtime 基于 final snapshot 做 held/primary 过滤、
  去重与 capacity 失败不发布，并让同帧 PointerDown claim 即使未 consume 也拦截 Gameplay。该切片仍不证明
  Key/Gamepad/axis claims、Widget 默认行为、DisplayList handoff 或可见 UI；后续 Button default action
  另由当前 Windows Debug UI109/109与 Runtime→UI60/60 覆盖；
- Runtime→UI D0/D2：同一独立 executable 增加 `PrimaryWindowUIDisplayCoordinator` 与 scoped
  `setBoxPaint()` 用例，覆盖
  layout/paint commit 后、Render submit 前的 primary-window UIDisplayList 构建、submit-call-local borrow、
  Headless/0 framebuffer/suspended 空 list、固定 PMR 复用，以及容量/identity/metrics 失败不保留旧
  publication、不提交截断 list；还覆盖 facade 正常写入 SolidFill、phase expiry、wrong-context/stale-generation
  sticky failure。完整 D2 Windows Debug/Release 均为53/53；后续 listener extension 的 Windows Debug
  为55/55。该切片本身仍不证明 owning packet、FramePin、
  Text/Glyph、Widget 默认行为或可见 UI，后者由 Desktop 样例单独验证；

以下仍是 Legacy 共存构建的回归覆盖，不能当作 vNext UI/Scene/Asset 已实现：

- Legacy 3D Camera：60° 垂直 FOV 必须按 bx 要求以 degrees 进入投影矩阵，防止误转 radians 后 Cube 近距离铺满屏幕；
- Legacy Event：优先级队列、RAII Token、dispatcher 先销毁、立即取消订阅，以及 IME composition 与已提交文本分离；
- Legacy Resource：共享 FileSystem 唯一 completion pump、主线程预算、取消和过期 generation 隔离；
- Legacy Windows 栈预算：EventSystem 实例不得重新引入超过默认线程栈预算的大块 inline queue；
- Legacy UI：hit-test 不隐式布局、重叠节点唯一命中、Capture/Target/Bubble 顺序、动态子节点上下文继承、stale NodeId 失效、上下文先析构、节点移除/自移除生命周期、Pointer Capture 外部释放、Tab/Shift+Tab 焦点遍历、焦点 KeyDown 路由/默认取消/重复键抑制/路由中删除目标、KeyUp 完整路由/停止传播后的局部清理/路由中删除目标、方向键 beam 优先与隐藏/禁用节点过滤、Modal Focus Scope 限制/嵌套恢复/自动失效、设备无关语义导航的 scope/Accept/Cancel 生命周期、未处理按键向祖先回退、每窗口 Theme/DPI 隔离、200% DPI 逻辑坐标命中、裁剪边界、ScrollView 滚轮/钳制和十万行虚拟范围；Button action 还覆盖实例级重入隔离、异常后恢复、不同 action 嵌套、回调销毁自身，以及 Capture 阶段删除 routed click 目标后的 generation 失效。

## 待补自动化门禁

- Legacy Application 现有失败点继续回归；M6-A 尚未覆盖的 initial UI layout、GameStateStack
  与后续模块初始化失败点要随对应消费者加入；
- `IGameState` top-only、structural 与 policy-change 合计每 State 每帧最多一个 command，验证
  replace 后再请求 policy-change 返回 `AlreadyQueued`；覆盖 queue/completion capacity、sequence、
  completion slot 的 Reserved/Delivered/Diagnostics 回收，以及 `initialPolicy` 单次采样；
- push/replace enter 失败保持旧栈且不调 candidate onExit；失败注入必须在 enter 中真实启动读取
  staged owner 的 Task，验证 completion 在 commit 前不可发布，回滚先 cancel + barrier/join、再释放
  Worker 可访问的 owner，最终 Task/owner/completion 计数归零；
- State Transition Commit 后新 State 同帧只 layout 一次、下一帧输入生效；pop/replace 按“关闭
  ingress → cancel → barrier/join → onExit → RAII 析构”清理 roots/focus/capture/TaskGroup，onExit
  恰好一次，Worker 不能观察已释放的 State 成员；
- Platform 后续：production GLFW Gamepad registry/sampled diff、OS Pointer Capture、100%/150%/200%
  DPI 的 UI 命中、Windows IMM32 composition 与窗口销毁顺序；Window/Keyboard/Pointer/Focus/
  close/committed text callback/poll adapter 已完成，不能继续列为待实现；
- 2D world picking：M10-A40 已在 `tina_render_scene_tests` 验证
  `pickWorldFromLogicalPointer`（中心/角点/半开 viewport/旋转/平移/非法输入/锁存字段语义）；
  M10-A41 已在 `tina_runtime_ui_tests` 验证 `LastPresentedCamera2DLatch`（present 前失败、中心映射、
  无相机清空、extraction-only 不改锁存、viewport miss）并在 `EngineHost` present 后写入；
  M10-A42 又验证 `ActionMapper` 将 sample 并入 Simulation Action、UI consume/claim 不穿透、无
  last-presented camera 结构化失败后的 Press/Release 状态可重试、viewport miss no-hit 与0 fixed-step
  跨帧不重算；
- Camera2D 覆盖 NaN/Inf/非正投影值、`x + width`/`y + height` 越界、零 Surface suspension 和
  Catalog canonical PPM mismatch；PixelPerfect 覆盖强制 CameraAndSprites snap/nearest sampler、
  Camera 相对旋转、Size override 与最终 texel basis/origin 校验，不合格 Camera 不生成 view、
  不合格 Sprite 被去重诊断并跳过；
- Scene 延迟 push/pop/replace，以及 fixed phase mutation barrier、延迟实体销毁和
  interpolation snapshot；
- UI 后续：在 b3e startup/root capability、Pointer Button claim bridge、已实现 SolidFill paint/DisplayList
  bridge、D0 Runtime DisplayList handoff、D2 scoped `setBoxPaint()`、Button default action 与 clean-subtree
  Measure/Arrange reuse 基础上补完整 Widget owner、Label 文本、Image/Text/Glyph PaintCache、完整 dirty-range pruning
  和布局中新增 dirty 不丢；
- UIInputScopeSnapshot 对多个 eligible State roots 只做一次全局 hit-test；阻断/恢复时 Pointer Cancel、
  Focus history、Modal root scope 与 generation 失效顺序固定；
- Transform/scroll/clip 只重建 composite snapshot，不重建 local PaintCache；Visible/Hidden/Collapsed
  dirty 传播完整，相同 effective clip 确定性 intern 为同一 ClipId；
- 无变化 UI 必须 Style/Layout/PaintCache rebuild=0且 Tina heap allocation delta=0；每窗口 layout
  <=1、每 Pointer transition hit-test<=1，dirty leaf 不重排无关 subtree；
- UI 多指针/多按键、触摸输入、GLFW 手柄轮询/回滞/长按重复的可注入测试、实体手柄矩阵、焦点回调中的延迟销毁、可访问语义和截图级激活视觉状态；
- Checkbox 的 Pointer/Keyboard/Gamepad 单次切换、disabled/preventDefault、回调自销毁；Slider
  的有限性校验、clamp/step 量化、min==max、capture 拖动、Home/End、每帧单次 change 与 DPI
  命中；设置 backend 失败时 model 回滚且不留下错误全屏/音量状态；
- UI Semantics 的 Role/Name/Range/Checked/Enabled/Focused、labelledBy stale UINodeId、装饰节点过滤
  和稳定树序；Theme/DPI revision 只使必要 style/layout dirty，敏感 TextEdit 正文不进诊断；
- Font Asset lease、UTF-8 非法序列替换、中文 fallback、Atlas page 满容量/退役、raster completion
  stale generation；text measure 与 raster 分离，glyph 发布只 Paint dirty，不改变既定 advance；
- M7-C1c-b3b/b3c/b3d1/b3d2/b3e/D0 已把 Move/Button/Wheel routed consumption、held primary Pointer Button claim bridge、
  primary-window `UIContext` ownership、bounded capacity、每帧 layout commit、startup seed、Game SDK root owner 与
  submit-call-local UIDisplayList handoff 放进独立/生命周期门禁；
  Key/Gamepad/axis claims 与 capture/focus/modal 取消仍后置；M7-E GLFW Gamepad 只验证相邻 Poll sampled diff，
  实体矩阵和回滞/重复；
- Replay 后续只记录 target tick、normalized action state、ordered edge 和 reset marker，不记录 GLFW
  key 或 UI node；CloseRequested 的真实 GLFW callback 路径不得重复发布生命周期或 gameplay 事件；
- Game SDK umbrella header 在无 bgfx/GLFW/EnTT include path 下独立编译；public source/include、
  module direct/public dependency 通过第三方 forbidden-token/target 检查；外部 Game consumer
  只声明 Game SDK + desktop bootstrap 也能完成生产链接；可选 `Tina::Physics2D` consumer 在无
  Box2D include path 下单独编译和链接；
- RenderScene 与现有 UIDisplayList 分别只生成一次；D0 当前以 submit-call-local borrow 汇入
  `RenderFrame`，后续含资源路径再升级为 Runtime-owned RenderFramePacket。DisplayList
  不含 Widget/bgfx，现有相邻兼容 batching 继续保持 paint checksum；
- 在途 RenderFramePacket 期间卸载 Asset、退役 Atlas、关闭 Surface 和注入 Pass 失败仍保持引用
  有效；completion 后 packet/lease/pin/resource count 归零；纯 UI/2D-only/3D-only/无内容/
  `Suspended` surface 的 initial clear 次数固定，UI-only/2D-only depth allocation count 为0；
- Render Pass 顺序、禁用与失败停止、临时资源清理、typed handle generation 与
  RenderFramePacket 引用保活；
- Asset CPU Decode/GPU Upload 双队列的 generation 取消，以及任务数、字节、时间预算和
  饥饿保护；弱 Handle/强 Lease、UploadTicket/retirement、依赖循环/失败链、Cooker 的损坏/
  不支持 glTF、生成后验证、事务 Manifest 和增量更新；
- Asset import 的缺失/重复 ID、移动保 ID、复制分配新 ID，以及同一锁定输入跨两次独立 cook
  生成 byte-for-byte 相同产物；设置/依赖/schema/target 任一变化都会令 cache key 失效；
- Cooker 拒绝绝对/UNC/远程/根外 `..` 与 symlink URI、超限 data URI、整数溢出和解压炸弹；
  Runtime 即使 ContentHash 匹配也拒绝越界 payload/dependency table；
- Audio：Voice generation、command/completion 满容量、callback 0分配/0阻塞、设备 Disabled、
  Stop/自然结束竞争、Asset lease ACK 和重复 shutdown；
- 2D layer/order/alpha、Camera resize/world picking、Tile chunk culling/dirty rebuild、Tile AABB/
  Box2D 分工和 UI overlay 不穿透；
- 3D Camera/Material/Texture/depth occlusion、bounds/culling/instance、resize、Cooked glTF/Prefab 和
  不支持特性诊断；
- 完整 Tina 游戏的 Linux GCC/Clang production backend 2D/UI/3D 运行，以及 Clang ASan/UBSan preset。

Windows 和 Linux 必须分别构建。项目直接运行 GoogleTest 可执行文件，不使用 CTest 调度；
Clang ASan/UBSan 使用项目固定的 Clang22 + libstdc++15 chainload toolchain，不能只换 compiler
可执行文件却继续绑定旧系统标准库。

vNext 的每个垂直切片先通过模块级 GoogleTest，再启动对应样例。Null Runtime 连续300帧；
Platform/UI、Scene/2D、Render/3D、Asset/Cooker 分别保留独立运行入口。测试程序返回0、日志
资源计数为0和实际画面正确是三个不同证据，验收记录必须分别给出。

规划中的性能数据将由独立 Release `tina_bench` 直接运行并输出带 schema、workload version/checksum、
硬件、工具链、依赖 fingerprint 和提交信息的结果，不使用 CTest；当前尚未实现该 target 或 Bench preset。
普通 GoogleTest 不使用
易抖动的绝对微秒阈值；Tina-owned 零稳态分配、容量溢出、checksum 和资源归零等确定性契约
仍直接阻断。当前开发机只产生 provisional 结果；固定门禁机才允许绝对预算和相对回归门禁。

正式 p99 每 workload 至少5个独立进程、每进程 warm-up 600帧并采10,000帧，nearest-rank
计算；比较 run-level p99 中位数和 baseline MAD。Build/host/workload fingerprint 不同返回
BaselineIncompatible。基准输出不记录 hostname、用户名或绝对路径，正确性 checksum 不同的
run 不参与性能比较。

未来 Tracy Profile 构建单独验证 Tina zone、frame、thread name、可选 lock/memory event 和正常
shutdown；空后端与 Tracy 后端必须产生相同业务结果。Bench/Profile 使用相同优化/CRT/assert/
LTO 语义，只改变插桩和符号；规划的 `tina_bench` 默认关闭 Tracy，需要定位回退时才用相同
workload 启用。Tracy overhead 与常驻 Metrics off/on overhead 分开记录。

Windows M6-A/M7-A/M7-B1 Headless 的完整直接门禁为：

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug --target tina_tests tina_sample_null
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_sample_null.exe --frames=300

cmake --build --preset windows-vnext-release --target tina_tests tina_sample_null
out\build\windows-msvc-vnext\bin\Release\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Release\tina_sample_null.exe --frames=300
```

Windows UI 树、布局、命中、route 与 SolidFill committed paint 的独立直接门禁为：

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug --target tina_ui_tests
out\build\windows-msvc-vnext\bin\Debug\tina_ui_tests.exe --gtest_color=yes

cmake --build --preset windows-vnext-release --target tina_ui_tests
out\build\windows-msvc-vnext\bin\Release\tina_ui_tests.exe --gtest_color=yes
```

当前 Windows MSVC 19.50 Debug 为115/115；上一轮 Windows Release、Linux GCC 13.4 与 Linux Clang 22
sanitizer 为92/92，Clang 无诊断。本轮没有重跑后三条门禁。

Windows SolidFill committed paint → Render SolidQuad DisplayList bridge 的独立直接门禁为：

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug --target tina_ui_render_integration_tests
out\build\windows-msvc-vnext\bin\Debug\tina_ui_render_integration_tests.exe --gtest_color=yes

cmake --build --preset windows-vnext-release --target tina_ui_render_integration_tests
out\build\windows-msvc-vnext\bin\Release\tina_ui_render_integration_tests.exe --gtest_color=yes
```

Windows MSVC 19.50 Debug/Release、Linux GCC 13.4 与 Linux Clang 22 sanitizer 均为12/12，Clang
无 sanitizer 诊断。该 executable 与 `tina_tests`、`tina_ui_tests`、`tina_runtime_ui_tests` 独立报告，
不通过 CTest 调度。

Windows M7-C1c-b3b/b3c/b3d1/b3d2/b3e/D0/D2 与后续 listener extension 的 Runtime→vNext UI producer、
primary-window owner、layout coordinator、scoped Game SDK UI/listener access、Pointer Button claim bridge、
primary-window UIDisplayList handoff 与 scoped `setBoxPaint()` 独立直接门禁为：

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug --target tina_runtime_ui_tests
out\build\windows-msvc-vnext\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes

cmake --build --preset windows-vnext-release --target tina_runtime_ui_tests
out\build\windows-msvc-vnext\bin\Release\tina_runtime_ui_tests.exe --gtest_color=yes
```

C1c-b3c 历史记录为 Windows MSVC 19.50 Debug/Release 均20/20；b3d1 在 Windows Debug/Release、
Linux GCC 与 Clang sanitizer 均为29/29；b3d2 在 Windows Debug/Release、Linux GCC 与 Clang
sanitizer 均为42/42；b3e 在 Windows Debug/Release、Linux GCC 与 Clang sanitizer 均为46/46。
完整 D2 Windows Debug/Release 为53/53；listener extension 与 Button default action 的最新 Windows Debug/Release 均为60/60。
Linux 未在本轮重跑，仍保留 b3e 46/46，Clang 无诊断。这个 executable 必须与 `tina_ui_tests`、基础 `tina_tests`
分开报告；它不通过 CTest 调度。

Windows GLFW Platform 的独立直接门禁为：

```powershell
cmake --preset windows-msvc-vnext-platform
cmake --build --preset windows-vnext-platform-debug `
  --target tina_tests tina_platform_glfw_tests tina_sample_platform
out\build\windows-msvc-vnext-platform\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-platform\bin\Debug\tina_platform_glfw_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-platform\bin\Debug\tina_sample_platform.exe `
  --frames=300 --frame-delay-ms=0
```

Release 使用对应 `windows-vnext-platform-release` 与 `bin/Release`，并继续与 Debug 串行构建。
Windows Desktop bgfx 的独立直接门禁为：

```powershell
cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_tests tina_runtime_ui_tests tina_platform_glfw_tests tina_render_bgfx_tests tina_sample_desktop
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_platform_glfw_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_bgfx_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_desktop.exe

cmake --build --preset windows-vnext-bgfx-release `
  --target tina_tests tina_runtime_ui_tests tina_platform_glfw_tests tina_render_bgfx_tests tina_sample_desktop
out\build\windows-msvc-vnext-bgfx\bin\Release\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Release\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Release\tina_platform_glfw_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Release\tina_render_bgfx_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Release\tina_sample_desktop.exe
```

前一轮 dirty-subtree b4a + M8-A/M8-B 的 Windows Debug/Release 直接结果为基础211/211、UI115/115、Runtime→UI60/60、UI→Render12/12、Scene19/19、RenderScene11/11，以及Null/2D infrastructure样例各300帧。Windows Debug adapter另通过GLFW26/26、Platform样例300帧、bgfx16/16与Desktop连续3次各300帧；本轮没有重新截图。上一轮完整Windows D2 Debug/Release结果是基础207/207、UI92/92、Runtime→UI53/53、UI→Render12/12、
Null样例300帧、bgfx专项16/16与真实 D3D11 Intel Iris Xe Desktop样例；Debug 1200帧并做截图像素检查，
Release 300帧输出 clean status ok。本轮 Debug D3D11 观察到 `RefCount is 3 (expected 0)` 的已记录第三方 debug layer 提示。
GLFW专项26/26与GLFW样例300帧已在本轮Debug图复验；前序 C1c-b3a Debug
WindowSurface GLFW样例1800帧仍作为历史证据；上一门禁另有
`TINA_BUILD_TESTING=OFF` production-style WindowSurface GLFW样例300帧返回0。
Linux 最新 Null 为 GCC 与 Clang sanitizer 基础205/205、UI92/92、Runtime→UI46/46、UI→Render12/12与
Null样例300帧；Clang无诊断。Linux 尚未复验 D1/D2 的 bgfx SolidQuad UI pass 和 Desktop 4-panel 样例。
前序 Pointer/Input 为基础185/185、GLFW专项23/23。M7-B2 Desktop/bgfx
产品门禁仍记录 GCC 183/22/11 与 Clang 183/22/11、Desktop 300帧。Clang 当前基础测试无 suppression；
X11精确 suppression 在23项GLFW专项命中13次/5304 B，Desktop上一门禁命中1次/408 B。
Clang Desktop 使用 bgfx Vulkan，但 WSL2 adapter 是 llvmpipe 软件实现，不计作硬件 GPU 门禁。
Linux X11、Wayland和 Clang LSan精确 suppression的完整命令见[构建与运行](building.md)。

Visual Studio 多配置输出必须使用对应的 `bin/Debug` 或 `bin/Release`，不能混用 GoogleTest DLL。
同一 build tree 的两种配置也必须按上面命令顺序构建，不能并发驱动共享生成状态。
Legacy 的 `Tina.exe`、shaderc 和 app-local DLL 同样按配置隔离。Linux 单配置构建直接运行
`out/build/<preset>/bin/` 下对应测试 executable。

只验证 vNext Core/Runtime 和测试源码的 Linux 编译/链接时，使用独立 GCC 13 preset，避免
污染可运行的 Legacy `linux-ninja` cache：

```bash
cmake --preset linux-gcc13-vnext
cmake --build --preset linux-gcc13-vnext-debug \
  --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests tina_sample_null
./out/build/linux-gcc13-vnext/bin/tina_tests --gtest_color=no
./out/build/linux-gcc13-vnext/bin/tina_ui_tests --gtest_color=no
./out/build/linux-gcc13-vnext/bin/tina_runtime_ui_tests --gtest_color=no
./out/build/linux-gcc13-vnext/bin/tina_ui_render_integration_tests --gtest_color=no
./out/build/linux-gcc13-vnext/bin/tina_sample_null --frames=300
```

Clang 22 sanitizer 的基础、UI、Runtime→UI 与 UI→Render 直接门禁为：

```bash
cmake --preset linux-clang22-vnext-sanitize
cmake --build --preset linux-clang22-vnext-sanitize-debug \
  --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests tina_sample_null
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
LSAN_OPTIONS=exitcode=23 \
./out/build/linux-clang22-vnext-sanitize/bin/tina_tests --gtest_color=no
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
LSAN_OPTIONS=exitcode=23 \
./out/build/linux-clang22-vnext-sanitize/bin/tina_ui_tests --gtest_color=no
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
LSAN_OPTIONS=exitcode=23 \
./out/build/linux-clang22-vnext-sanitize/bin/tina_runtime_ui_tests --gtest_color=no
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
LSAN_OPTIONS=exitcode=23 \
./out/build/linux-clang22-vnext-sanitize/bin/tina_ui_render_integration_tests --gtest_color=no
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
LSAN_OPTIONS=exitcode=23 \
./out/build/linux-clang22-vnext-sanitize/bin/tina_sample_null --frames=300
```

Linux 最新 paint/DisplayList/bridge 切片中，GCC 13.4/CMake 4.2.3 已通过基础205/205、UI92/92、
Runtime→UI46/46、UI→Render12/12与Null样例300帧；Clang 22.1.8 + libstdc++15.2
ASan/UBSan/LSan 已通过相同门禁且无 sanitizer 诊断。`TINA_BUILD_SHADERS=OFF` 输出
不含 Legacy 产品和 cooked shader，只能作为 Headless 验证程序，
不能作为游戏产品或发布包。

## 当前 Legacy 运行冒烟

构建命令和环境前提见 [构建与运行](building.md)。以下命令描述验收入口，不替代构建步骤。

菜单 2D + 中文 UI，正常提交300帧后退出：

```bash
./Tina --smoke-frames=300
```

直接进入完整 2D TileMap、ECS、Toolbar 和 CharacterPanel：

```bash
./Tina --smoke-game --smoke-frames=300
```

直接显示虚拟化世界列表、新建世界对话框、中文标签和已聚焦 TextEdit：

```bash
./Tina --smoke-ui --smoke-frames=300
```

运行右手透视相机、深度测试和静态索引 Cube：

```bash
./Tina --smoke-3d --smoke-frames=300
```

四个命令都必须返回0，并在日志中出现正常初始化、达到帧数、场景退出、资源管理器释放、bgfx 和窗口关闭记录。UI 路径还必须出现 `UI smoke scene ready`，且不得出现 `无法建立模态焦点范围`；3D 路径必须肉眼或截图确认透视 Cube 可见，并出现 `Smoke3DScene released vertex and index buffers`，且不得出现 `BGFX LEAK` 或 `MEMORY LEAK`。只检查 exit code 和 buffer 生命周期不足以证明画面正确。

bgfx Debug/D3D11 当前会在关闭 `ID3D11InfoQueue` 时输出一次 `RefCount is N (expected 0)`；本机
Legacy 与 vNext 进程观察到的 `N` 会随调试对象组合变化，本轮 D2 为3。同一路径的 MSVC Release
300帧验证无该提示、无 stderr、无 leak marker；Tina 仍以自身资源账本和严格 shutdown 顺序作为
泄漏门禁，不把这条第三方提示单独当作 Tina 资源泄漏结论。

## vNext 独立样例门禁

`tina_sample_null`、`tina_sample_platform`、`tina_sample_desktop`、`tina_sample_2d_infrastructure`、
`tina_sample_2d_tilemap`、`tina_sample_2d_infrastructure_bgfx` 与 `tina_sample_3d_extraction` 已落地，
其余 executable 仍是后续里程碑目标；
不得用当前 Legacy `Tina --smoke-*` 的结果冒充 vNext 样例：

| 样例 | 状态 | 主要证明 | 资源策略 |
| --- | --- | --- | --- |
| `tina_sample_null` | M6-A/M7-A/M7-B1/M7-C1c-b3e/D0/Button Headless 已实现 | EngineHost、PlatformFrame/Input/Action、单个 `IGameState`、私有 primary-window UI owner/route/layout/claim/display/default-action handoff seam、Headless/Disabled/Null、300帧生命周期；无窗口 startup seed 显式选择 Headless，UI capability 请求返回结构化 unavailable，layout coordinator 与 D0 DisplayList handoff 均成功发布空结果；本轮 Button default action Windows Debug 运行300帧且返回0，Linux 10,000帧仍是上一批历史结果 | 无真实第三方 backend，DisplayList 为空，不证明 UI root/完整 Widget/可见 UI |
| `tina_sample_platform` | M7-A + M7-B1 已实现 | 私有 GLFW `NO_API` 窗口、键鼠、resize/focus/close、committed text、WindowSurface handoff 与 NullRender | 不创建真实 bgfx GPU device |
| `tina_sample_desktop` | M7-B2 Desktop bootstrap + C1c-b3e Runtime UI owner/route/layout/claim bridge seam + Button default action + D0 DisplayList handoff + D1 bgfx SolidQuad UI pass + D2 visible panel smoke 已实现 | `Tina::Desktop::CreateEngine` 私有组合 SteadyClock、GLFW WindowSurface、DisabledTaskSystem 与 bgfx；默认300帧；startup seed 显式绑定 primary-window Context，创建1个 retained root和4个 painted panel，通过 Render 前 layout/paint snapshot 与 UIDisplayList borrow 进入私有 bgfx SolidQuad pass；claim/display/setBoxPaint/default-action handoff 由独立 Runtime→UI 测试覆盖，真实可见性由该样例截图证明 | 当前 D2 Windows D3D11 Intel Iris Xe Debug 1200帧截图检查与 Release 300帧均通过，Release clean；截图验证 background RGB(9,24,40)、blue(28,92,148)、cyan alpha over blue/background、右边界 scissor clip 与 pink panel；Debug RefCount=3 为第三方 debug layer 提示；Linux Desktop 仍保留前序 GCC 13.4 与 Clang 22 sanitizer 门禁；Clang WSL2 为 Vulkan/llvmpipe，不代表硬件 GPU 性能，也不代表 Scene/Pass Scheduler、Text/Glyph 或完整 Widget 完成 |
| `tina_sample_ui` | 未实现 | 在现有 Desktop SolidFill panel smoke 和 primary Pointer Button default action 上补中文、Label 文本、Button Keyboard/Gamepad activation、Modal、TextEdit、Runtime packet、Glyph Atlas 与资源型 UI Render | M7 内置 Cooked Font/Texture fixture |
| `tina_sample_2d_infrastructure` | M8-B Headless/Null extraction foundation 已实现 | Scene World → resolved Camera2D/Sprite2D、layer/order、cull/snap、Runtime `primaryWorldScene` handoff、300帧资源/生命周期归零 | 当前只用内置纯值 fixture；Asset/Cooker、可见 bgfx fixture、world picking、UI overlay 后置 |
| `tina_sample_2d_tilemap` | M10-A31 Headless/Null TileMap 产品烟测已实现 | 内建 Tileset/TileMap → TileMapInstance → emitVisibleTileMapSprites + CharacterController2D → 每帧 11 tile + 1 角色 sprite；300 帧 JSON | 非正式 Catalog/bgfx/UI/Box2D `tina_sample_2d` |
| `tina_sample_2d` | M10-A32–A44 正式 2D 产品样例（A39–A44 pointer/selection 闭环收口） | 磁盘 recipe Catalog + Character + UI/Text/Button；可选 Physics crate / FreeType；脚本化行走撞墙；A43 消费 last-presented sample → cell；A44 高亮 + `--seed-tile-selection`；JSON selection/highlight/seed；`catalogFromRecipeFile`；product-2d `productGate=bgfx-physics-freetype` | 默认 300 帧不点（hits=0 合法）；seed CLI 为受控门禁（非 OS 点击）；A39 由 `tina_runtime_ui_tests`；完整 cooker CLI / cgltf **Deferred**；默认不开 A45 |
| `tina_sample_2d_tilemap_bgfx` | ALIAS → `tina_sample_2d` | 兼容旧脚本 target 名 | 请迁移到 `tina_sample_2d` |
| `tina_sample_2d_infrastructure_bgfx` | M9-C 最小 bgfx Sprite2D fixture + 2D/UI 样例已实现 Debug/Release 验证 | Desktop bootstrap + bgfx；固定 View 0 clear、View 1 Opaque3D、View 2 Sprite2D、View 3 UI；默认/门禁300帧，当前每帧5个 fixture Sprite 和2个 retained UI panel，资源账本平衡；截图确认旋转、透明、flip 与 UI overlay | 只接受 fixture key `sprite=1`；不替代正式 `tina_sample_2d` |
| `tina_sample_3d_extraction` | M9-A Headless/Null extraction foundation 已实现 | Scene World → resolved Perspective/Mesh3D、当前帧aspect、sphere culling、稳定sort/batch、Runtime handoff；300帧4 submitted/3 visible/1 culled/2 batches、一次aspect变化与资源归零 | 当前只用 fixture key/纯值和 recording Null device；无depth attachment、GPU buffer/shader/pipeline或可见画面，不计Legacy删除门禁 |
| `tina_sample_3d_infrastructure` | M9-B 最小 bgfx Opaque3D fixture 已实现 | Desktop bootstrap + bgfx；全 surface clear View 0、depth-tested procedural Cube View 1；默认/门禁300帧，当前每帧3个 Cube 和1个 instance batch | 只接受 fixture key `mesh=1/material=1/submesh=0`；canonical `P3_N3_UV2` 静态 VB/IB + unlit shader + transient instance buffer；不证明 Cooked Mesh/Material/Texture/Prefab、通用 Pipeline/PBR、Pass Scheduler 或正式3D产品 |
| `tina_sample_3d` | 未实现 | Cooked glTF -> Mesh/Material/Prefab、culling/instance | M10 Catalog/Manifest |

M7-B2 已建立私有最小 bgfx clear/present core、7项 planner 测试、4项 factory/lease 回滚测试、
Desktop bootstrap 和真实 GPU 300帧冒烟。M7-C 已建立最小 SolidFill DisplayList CPU bridge，D0 已接入
Runtime submit-call-local DisplayList handoff；D1 已加入私有 bgfx SolidQuad UI pass 和5项几何测试，D2 已加入
Game SDK `setBoxPaint()` facade 与 Desktop 4-panel 可见 smoke；M7-D 后续继续接入中文、Widget 默认行为与资源型 UI，
M9-A 只扩展 CPU/Null 3D extraction，M9-B 只扩展私有 fixture 级可见3D，M9-C 只扩展私有 fixture 级
Sprite2D 可见样例。游戏 sample source、Game SDK header 和 UI public header 不出现 bgfx。M9-C 最小直接门禁命令如下，仍直接运行 GoogleTest executable，
不使用 CTest：

```powershell
cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug --target tina_render_bgfx_tests tina_sample_2d_infrastructure_bgfx
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_bgfx_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_bgfx_tests.exe --gtest_filter=BgfxSprite2DGeometryTest.* --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d_infrastructure_bgfx.exe --frames=300 --frame-delay-ms=0
```

完整资源系统的结构化验收目标使用 backend-neutral 字段：

```text
RenderDevice stopped
render.resources.current = 0
render.retirement.pending = 0
ui.resources.current = 0
```

具体 backend 的 InfoQueue/debug marker 只属于 adapter test/log，不成为 Game API 或通用样例
成功条件。每个可见样例仍分别保存返回码、资源计数、性能结果和实际截图。

Windows GLFW 可见门禁已经分别验证：中文 UTF-8 标题可见；Escape 经过 GLFW callback、归一化
transition 和 Frame Action，在完成当前 Null submit/present 后退出；Alt+F4/原生 close 走
`PrimaryWindowRequestedClose`，不重复发布 lifecycle/gameplay event。客户区空白是 NullRender 的预期，
不能据此宣称真实 bgfx、2D、UI 或3D已完成。Debug/Release自动样例都以零延迟精确运行
300帧、返回0，`IGameState::onExit` 与 `IGameApplication::onShutdown` 计数各为1，退出后无残留 Tina
进程。

当前 D2 Windows Desktop bgfx 可见门禁已验证 Debug/Release `tina_sample_desktop` 通过
`Tina::Desktop::CreateEngine` 创建真实 D3D11 Intel Iris Xe surface；Debug 1200帧截图确认1280×720客户区
background RGB(9,24,40)、blue(28,92,148)、cyan-over-blue(29,186,167)、cyan-over-background(26,176,152)，
right clip 边界 x=1140 为 background、x=1160 与 x=1279 为 pink(239,88,122)。Release 300帧输出 clean
status ok，Debug RefCount=3 为已记录第三方 debug layer 提示；样例还验证 `uiRootsCreated=1`、
`uiPaintedPanelsCreated=4`、`uiRootsReleased=1`。Game SDK/public header 无 bgfx、GLFW 或 native 泄漏。
该门禁不包含 Scene 内容、不包含 Pass Scheduler/submission ticket、Text/Glyph 或完整 Widget 默认行为，
也不声明 resize、最小化、恢复的真实自动化通过。

Linux Desktop 门禁同样只证明 bgfx backend 初始化、300帧提交与关闭生命周期。Clang 路径的
`_XimOpenIM` suppression 仅覆盖第三方 libX11 retention（专项12次/4896 B、Desktop 1次/408 B）；
基础与bgfx专项不使用 suppression。WSL2 的 Vulkan/llvmpipe 结果不扩大为硬件 GPU、Scene/UI、
Pass Scheduler 或 resize/最小化/恢复自动化结论。
