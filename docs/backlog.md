# Tina 可执行 Backlog

本文件是未完成工作的唯一明细表。Roadmap 只表达优先级窗口，ADR 只表达决策，不在多个主题文档
重复维护任务状态。

## 状态与证据

任务状态只使用：`Planned`、`InProgress`、`Blocked`、`Done`、`Deferred`。

证据强度与任务状态分开记录：

- `Unit`：模块单元测试；
- `Integration`：跨模块测试；
- `Smoke`：sample/CLI 生命周期与结构化输出；
- `Visual`：截图、像素或人工视觉；
- `Platform`：指定 OS/toolchain/backend 门禁。

`Done` 必须满足验收条件并留下对应证据；“已经写代码”不自动等于产品门禁完成。

## Now

| ID | 状态 | 优先级 | 工作 | 依赖 | 验收条件 | 证据 |
| --- | --- | --- | --- | --- | --- | --- |
| ASSET-HANDLE-SCENE | InProgress | P1 | A1-A6 已清除 2D/3D Scene 持久 key；N16.1 建立 packet-local resource table，N16.2 已让全部 Sprite2D extraction 只写 `FrameResourceRef` 并由 registry frame pin 保护 binding | N16.2 | N16.3 统一 registry/Lease/GPU retirement owner 并升级产品证据 | Unit + Integration + Smoke；最终产品 ownership 证据待 N16.3 |

## Next

| ID | 状态 | 优先级 | 工作 | 依赖 | 验收条件 | 证据 |
| --- | --- | --- | --- | --- | --- | --- |


| UI-002 | Partial | P1 | Windows UIA 真机 adapter | UI-002-SPI | **已完成** 属性映射 + HostBridge + **EngineHost 产品 HWND 自动 attach/publish**（lease 解码 Win32）；**待** Narrator/Inspect 人工金标、Linux AT-SPI | Unit + Integration |
| UI-003 | Partial | P1 | 建立跨 DPI/GPU 容差视觉门禁 | 稳定门禁机 | **已完成** ContentScale* 单测 + 单机 ROI/baseline + **content-scale-like 逻辑尺寸矩阵**（960/1200/1440/1280/1920，`RunUi003SizeMatrix.ps1` + 分尺寸 baseline）+ sample JSON `contentScale`/`logical`/`framebuffer` 一致性 + **字体 identity fingerprint**（`fontFingerprint`：path/sha256/env `TINA_UI_FONT_PATH`/FreeType-on；baseline schema 3；mismatch 默认 fail）；**待** OS 级 100/150/200% DPI 真机矩阵、跨 GPU 像素金标 | Unit + Visual |






## Later

| ID | 状态 | 优先级 | 工作 | 验收条件 |
| --- | --- | --- | --- | --- |
| RENDER-001 | Partial | P2 | PBR Material、lighting 与 pass scheduling | **已完成** experimental MR + factors + baseColor/MR/normal；唯一 `setMesh3DLighting` 有界 0..4 directional lights；sample_3d 一次提交3灯 + 自动相机 + Khronos 球体/盒。**待** IBL/shadow、light component/culling、pass scheduling、vertex tangents |
| PHYSICS-001 | Deferred | P2 | Jolt 3D adapter | 独立 Tina::Physics3D API、Jolt PRIVATE、生命周期/查询/性能门禁 |
| UI-004 | Deferred | P2 | 通用 Focus Scope、Modal、持久 Pointer Capture | 多 root/state transition 与输入恢复测试通过 |
| UI-005 | Deferred | P2 | ScrollView、虚拟 ListView、Dropdown、TreeView | 100k item 虚拟化与零稳态分配门禁通过 |
| TEXT-001 | Deferred | P2 | 多行 TextEdit、grapheme/shaping、候选窗定位 | 中英混排、组合输入、selection 与平台 IME 矩阵通过 |
| ASSET-002 | Deferred | P2 | 热重载与增量 Cooker | 不破坏 AssetId/Lease/retirement 契约，失败不发布半包 |
| UI-THEME-C | Deferred | P2 | 圆角/Image/stylesheet 级 Theme | 不破坏 create 默认 chrome 与局部覆盖契约 |
| 2D-LIGHT | Deferred | P2 | 2D lighting 与 shadow | backend-neutral light/occluder 数据、遮挡合成与透明 Sprite 排序契约明确；多光源容量和视觉门禁通过 |
| 2D-NAV | Deferred | P3 | 2D navigation | Tile/object layer 可生成可版本化导航数据；动态阻挡、不可达与异步取消有确定结果 |
| 2D-SERIALIZATION | Deferred | P3 | 2D world/gameplay serialization | schema/version/migration、稳定资源引用与失败原子性明确；round-trip 和旧版本 fixture 通过 |
| 2D-EDITOR | Deferred | P3 | TileMap/Scene/动画 editor tooling | 工具只写受验证的 authoring 数据；undo/redo、非法输入诊断、cook preview 与 runtime 产物一致性通过 |

## Done

| ID | 完成项 | 证据入口 |
| --- | --- | --- |
| ASSET-HANDLE-SCENE-N16.2-SPRITE | `AssetFrameResourceResolver` + Runtime sink 将 World/TileMap/selection/Particle/Trail 全部迁移为 packet-local Sprite texture ref；registry 同帧去重并以 entry borrow pin 阻止活跃帧 unbind；Null/bgfx 在任何提交副作用前验证 cross-packet/stale/wrong-kind/range；旧 `RenderSprite2D*::spriteKey` 零引用 | [Rendering](rendering.md) · [Scene](scene-ecs.md) · [资源](resources.md) · Runtime/Scene/Asset/Render tests · 2D product smoke |
| ASSET-HANDLE-SCENE-N16.1-CORE | packet-local `FrameResourceRef`/固定容量资源表按 kind+binding 去重并持有 owning pin；cross-packet/stale/wrong-kind fail closed；Runtime extraction 前 begin 且失败/skip/complete 在 State teardown 前释放；Texture2D 既有 Lease+GPU owner 仅在 backend 接受后消费，PMR/ledger/backend 失败完整恢复 | [Rendering](rendering.md) · [Runtime](runtime.md) · [资源](resources.md) · FramePinPacket/RuntimeLifecycle/AssetGpuRetirement tests |
| ASSET-HANDLE-SCENE-3D-A6-BINDINGS | fixed-capacity owner-thread `Mesh3DBindingRegistry` 借用 Store/device/PMR；mesh/material 使用独立 device-instance allocator，成功后才消费且解绑不复用；Material 三张纹理与 factors 原子发布，dependency stale fail closed；exact stale handle 可解绑，失败保留记录重试；3D product schema 2 记录2组注册/释放、2 mesh/6 texture 销毁与 registry 释放 | [资源](resources.md) · [Rendering](rendering.md) · [3D](game-3d.md) · Mesh3DBindingRegistry/Render tests · 3D product smoke |
| ASSET-HANDLE-SCENE-3D-A5 | `MeshRenderer3D` 保存 copyable weak StaticMesh/Material Handle；visible extraction 分别借用 kind-specific resolver，invalid/stale/wrong-kind/unbound/zero fail closed，hidden 不解析；Prefab 只做 AssetId→Handle 并事务 rollback；3D product evidence schema 1 记录 handle 发布、两类 resolver hits 与 AssetStore active | [Scene](scene-ecs.md) · [3D](game-3d.md) · Scene tests · 3D product smoke |
| ASSET-HANDLE-SCENE-2D-A4 | `TileChunkSpriteEmitParams` 保存 copyable weak Tileset `AssetHandle` 与调用期 borrowed `AssetBindingResolver`，不再保存 `spriteKey`；registry 沿 Tileset 唯一 required Texture2D dependency fail closed；hidden/off-camera/empty 不解析，非空可见集合只解析一次，失败清空输出；product-2d schema 12 提供 TileMap resolver hits | [资源](resources.md) · [2D](game-2d.md) · TileChunk/Registry tests · 2D product gate |
| ASSET-HANDLE-SCENE-2D-A3 | `ParticleSystem2D`/`Trail2D` 保存 copyable weak Sprite `AssetHandle`，显式 extract 借用共享 resolver；空 handle 在 emit/Create 拒绝，资源失效映射为 `UnresolvedSprite`；空 FX 不解析，Trail 每次非空 extract 解析一次、Particle 按 live item 解析；不持 Lease/payload/GPU owner；product-2d schema 11 提供独立 resolver hits，FX fingerprint schema 2 使用稳定 AssetId | [Scene](scene-ecs.md) · [2D](game-2d.md) · Particle/Trail tests · 2D product gate |
| ASSET-HANDLE-SCENE-2D-A2 | owner-thread `Sprite2DBindingRegistry` 借用 Store/device；device-instance allocator 将 Texture Handle + GPU texture 事务注册为唯一、单调不复用 key，同 device 多 registry 不冲突；Sprite/Tileset 通过唯一 required Texture2D cooked dependency fail-closed resolve；State 严格 unbind 后 destroy；当前 product-2d schema 12 保留2纹理注册、释放、销毁与 resolver hits | [资源](resources.md) · [2D](game-2d.md) · Registry tests · 2D product gate |
| ASSET-HANDLE-SCENE-2D-A1 | `SpriteRenderer2D` weak `AssetHandle` + borrowed resolver；空/stale/cross-store/wrong-kind/unbound fail closed，hidden 不解析；Scene 不持有 Lease/GPU owner | [Scene](scene-ecs.md) · Scene component/extract tests · 2D product sample |
| RUNTIME-SHUTDOWN-DEADLINE | `shutdownAndJoinFor` 有界 stop/join；invalid 不触发 stop，timeout 保留 stopping TaskSystem/Worker/owner 并可 retry；Host 仅为 TaskSystem worker-exit/join 使用配置 deadline，超时先写 Diagnostics 再 terminate，绝不继续析构 owner；不 detach/强杀 | [Task](task-system.md) · [Runtime](runtime.md) · Disabled/Bounded Task shutdown tests · EngineHost Task-shutdown deadline/death tests |
| DONE-001 | Legacy `Tina.exe`、旧横版 2D 与旧 UI 产品图删除 | [M12 退役说明](m12-legacy-ui-retirement.md) |
| DONE-002 | `EngineHost`、Platform/Input、WindowSurface、Desktop/bgfx 垂直切片 | [架构](architecture.md) · [测试](testing.md) |
| DONE-003 | Scene 2D/3D extraction 与 Catalog/Cooked/Handle/Lease/Task/Upload 首轮 | [Scene](scene-ecs.md) · [资源](resources.md) |
| DONE-004 | 2D product sample 与 glTF/Prefab 3D product sample | [2D](game-2d.md) · [3D](game-3d.md) |
| DONE-005 | Retained UI 文本/Glyph、Checkbox/Slider/TextEdit、ProgressBar/RadioButton 库级实现 | [UI](ui.md) |
| DONE-006 | glTF multi-mesh Cooker 与 distinct AssetId/Prefab dependency 测试 | [3D](game-3d.md) |
| 2D-INPUT-ADV | Runtime 唯一 unified binding 覆盖 digital/analog value、deadzone/scale、SumClamped/StrongestMagnitude、多 Gamepad、UI consume/claim suppression 与顶层 State transactional rebind（next-frame apply、Reject/Swap、cancel、generation disconnect/reset） | [Platform/Input](platform-input.md) · [Runtime](runtime.md) · ActionMapper/InputAction/Rebind unit + Runtime/UI integration + `tina_sample_2d` 300-frame smoke 是本轮最终验证目标，执行结果以最终验证记录为准 |
| 2D-TILEMAP-LAYERS | 有序 tile/object layer、map-wide 非零唯一稳定 layer/object ID、visibility、UTF-8 name/properties、point/rectangle；runtime render/collision 显式选择 layer；当前由 TileMap v3 stream root 延续该契约，旧 v1/v2 均不双读 | [2D](game-2d.md) · [资源](resources.md) · [物理](physics.md) · TileMap/CatalogCook/TileChunk/TileMapPhysics tests |
| 2D-TILEMAP-STREAM | TileMap v3 stream root + 独立 `TileMapChunk` v1 Cooked asset；chunk 为 deferred dependency；`TileMapStream` 按 Camera/layer 有界 request、cancel、unload、lease 与 transactional capacity；resident generation 贯通 dirty cache；sample 每帧 demand→pump→commit 并验证 visual/collision 两 chunk 驻留 | [2D](game-2d.md) · [资源](resources.md) · [公开 API](public-api.md) · TileMapChunk/TileMapStream/AssetSystem/dirty-cache tests |
| 2D-TILEMAP-LRU | retain window 仅作 optional cache；按最近一次成功 demand update 时位于 load window 的 recency 自动淘汰，读取 API 不 touch；desired 强需求单独超 resident capacity 仍 transactional failure | [2D](game-2d.md) · [资源](resources.md) · TileMapStream retain-overflow/LRU tests |
| 2D-PHYSICS-EXPAND | Body/Shape/Joint 独立 generation handle；Box/Circle/Capsule、多 shape/body、sensor enter/exit、Distance joint 与级联 retirement；TileMap bridge/sample 全部迁移，Box2D 保持 PRIVATE；29/29 模块测试与产品 300 帧 sensor/joint 证据通过 | [物理](physics.md) · [2D](game-2d.md) · PhysicsWorld2D/TileMapPhysics/CharacterControllerPhysics tests |
| 2D-AUDIO-ADV | voice gain/pitch/pan/fade 与 transient one-shot retirement；PCM stream 在 Create 固定预分配，owner thread 整块原子 submit，Task worker 必须 marshal；非 EOF underrun 仅静音计数，EOF 排空后单次 Stopped，cancel 单次 Cancelled；terminal completion 在 ring 满/active realtime reader 时延迟而不丢，单 realtime consumer、absorbing terminal 与有界 shutdown 已覆盖；产品 sample 贯通 Catalog PCM stream | [Audio](audio.md) · [公开 API](public-api.md) · AudioEngine PCM stream tests · MiniaudioDevice null-backend stream test · product-2d 300帧 smoke |
| 2D-FX | `ParticleSystem2D` / `Trail2D` 作为独立 Scene systems：Create 唯一持久 PMR 分配、固定容量与稳定 key 单调不复用；粒子固定 seed（含0）确定性、burst/update 失败事务性；trail anchor/segment/break、独立 segment lifetime 与按 age 宽度插值；产品门禁 schema 9 提供结构化与非空像素证据 | [2D](game-2d.md) · [Scene](scene-ecs.md) · [测试](testing.md) · `tina_scene_tests` · `RunProduct2dGate.ps1` |
| RENDER-001-NLIGHT | 删除 Opaque3D key/fill 双 setter；`Mesh3DLightingDesc` 单次提交0..4 directional lights + ambient；Null/bgfx/shader 同一上限与验证；sample_3d 提交3灯并输出 count | [Rendering](rendering.md) · [3D](game-3d.md) · GpuTextureUploadTests |
| RENDER-FENCE | present-return CPU ticket 与 GPU resource retirement 分离；bgfx 用末尾 view 的 blit + `readTexture()` ready frame 作为可证明 completion marker，Texture/Mesh generation 立即失效、native handle 延迟销毁；AssetLease pin、suspend flush、显式 drain 与 shutdown hard drain 已贯通 | [Rendering](rendering.md) · [Resources](resources.md) · [ADR 0016](adr/0016-asset-ownership-and-retirement.md) · BgfxRetirementTimeline/AssetGpuRetirement tests |
| 2D-SPRITE-BATCH | 任意有效 packet-local texture ref；bgfx 按最终渲染顺序建立连续 ref batch，解析 device binding 后逐 batch 绑定纹理；双纹理 sample 保持透明排序 | [2D](game-2d.md) · BgfxSprite2DGeometryTests |
| 2D-SPRITE-ANIM | SpriteAnimationClip cooked payload/typed validation/recipe；SpriteAnimator2D Once/Loop/PingPong、暂停、倍速与大 delta；sample 双纹理 `Idle -> Walk -> HitWall` | [2D](game-2d.md) · [资源](resources.md) · SpriteAnimationClipPayloadTests · SpriteAnimator2DTests |
| 3D-001 | multi-mesh 产品 E2E：双 mesh glTF fixture → cook → 两 StaticMesh upload/bind（当时 meshKey 1/2，N15 已替换为 registry allocator）→ Prefab 每节点 resolve → extract/draw → ledger 归零；`tina_sample_3d` 300 帧 `multiMesh=true` | [3D](game-3d.md) |
| TASK-001 | Desktop `resolveDesktopTaskSystemParams`：交互默认 `max(1, hw-1)` CPU worker；`createBoundedTaskSystem(cpu=0)` IO-only 仍 NotSupported；BoundedTaskSystem 单测覆盖 | [Task](task-system.md) · ADR 0017 |
| CLEAN-001 | 删除 vcpkg `legacy` feature 及 EnTT/GLM/spdlog/utfcpp 死依赖声明；preset 无引用 | [dependencies](dependencies.md) |
| CLEAN-002 | 删除无消费者 `StringUtils.hpp`（EASTL/utfcpp）与 Clock/FrameTimer/FixedStepTicker compatibility；`SteadyMonotonicClock` 实现迁到 `MonotonicClock.cpp` | [core](core.md) |
| CLEAN-003 | miniaudio 实现 TU 与 CMake FATAL 文案不再暗示 Legacy ON 可运行 | [dependencies](dependencies.md) |
| TEST-002 | product-2d 同轮：UI/RuntimeUI/bridge/FreeType/Physics2D/Audio/miniaudio/Asset 测试 + sample 300 帧；`productGate=bgfx-physics-freetype-audio`；脚本 `tools/windows/RunProduct2dGate.ps1` | [building](building.md) · [Windows 证据](m12-evidence-windows.md) |
| ASSET-001 | glTF 外部 URI root containment/`..`/scheme 拒绝 + 64MiB 上限；`tina_sample_3d` 上传/绑定 Cooked Texture2D 到 materialKey；路径逃逸单测 | [3D](game-3d.md) · GltfCookTests |
| UI-001 | ProgressBar/RadioButton 已接入 product-2d；190/190 UI、77/77 Runtime UI、12/12 Render bridge 通过，结构化输出与 Windows client-area 视觉证据成立 | [UI](ui.md) · [Windows 证据](m12-evidence-windows.md) |
| DOC-001 | 文档职责与任务体系重组完成；本地链接、configure/build preset、CMake target、Markdown fence 与格式扫描通过；UI 绘制链路和控件矩阵已归档 | [文档索引](README.md) · [Roadmap](roadmap.md) · [UI](ui.md) |
| UI-THEME-AB | 薄 `UITheme` token；`UIBoxPaint` 亮/暗边 + 可选 shadow；sample_2d 设置面板 elevation；hex `rgb`/`argb`；`UIThemeTests` | [UI](ui.md) |
| UI-THEME-DEFAULT | Context `productTheme`/`setProductTheme`；`create*` 自动 apply `make*Chrome`；`makeLightProductTheme`；局部 `set*Paint` 覆盖；`UIThemeTests` | [UI](ui.md) |
| DOC-002 | `tools/docs/CheckDocs.ps1`：docs 本地链接、cmake configure/build preset、`--target` 名、Legacy 产品文案软警告；不扫 out/build/thirdparty | [building](building.md) · [testing](testing.md) |
| RUNTIME-001 | `GameStateStack` + commands + 唯一 commit；**policy 向下阻断**（fixed/frame/render/UI 自顶向下 `forEachDispatch`）；enter 失败丢 candidate；`GameStateStackTests` / policy dispatch 单测 | [gameplay](gameplay.md) · ADR 0014 |
| RUNTIME-002 | `FramePin`/`FramePinSink`、`RenderFramePacket`、`CpuSubmissionCompletionLedger`；EngineHost submit/present 挂 pin 并在 present/skip 后 complete；shutdown abandon；`FramePinPacketTests` | [rendering](rendering.md) · ADR 0016 |
| PERF-001 | ADR 0018 Accepted；`tools/bench` → `tina_bench` schema v1；workload `null_runtime_frames`；JSON fingerprint/checksum/p50/p95/p99；共享机 `conclusion=provisional`；固定 hard-gate 机与多进程 MAD 后置 | [performance-memory](performance-memory.md) · ADR 0018 |
| RUNTIME-001-INT | Null Host 集成：base `requestPush` overlay（block fixed/frame below）→ overlay `requestPop` → base 恢复；enter 失败无 `onExit`；`GameStateStackIntegrationTests` | [gameplay](gameplay.md) · ADR 0014 |
| RUNTIME-001-SAMPLE | `tina_sample_2d` 收尾自动 pause overlay（≥60 帧）：push/pop + policy block；JSON `pauseOverlay*`；短 smoke 跳过 | [2D](game-2d.md) · ADR 0014 |
| UI-002-SPI | `UIAccessibilityTree`/`IUIAccessibilityProvider`/`UIAccessibilityProbeProvider`：从 committedSemantics 发布 role/name/state/range；stale node 拒绝；`UIAccessibilityTests`；`tina_sample_2d` 产品 JSON `accessibility*` | [UI](ui.md) · [2D](game-2d.md) |
| UI-002-UIA-MAP | 可选 `tina_ui_uia`：UIA 形属性映射 + factory 零 COM；`tina_ui_uia_tests` | [UI](ui.md) |
| UI-002-HWND | `WindowsUiaHostBridge`：SetWindowSubclass + WM_GETOBJECT + fragment root/children `IRawElementProviderSimple`；HostBridge 单测 | [UI](ui.md) |
| UI-002-HOST | `TINA_HAS_UI_UIA`：`EngineHost` 从 surface lease 取 Win32 HWND，startup/每帧 layout 后 publish `committedSemantics`；shutdown detach | [UI](ui.md) · Runtime |
| API-CLEAN-POLICY | `blocksUIUpdateBelow` 替换误导名 `blocksUIInputBelow`；文档/sample/tests 同步 | [gameplay](gameplay.md) |
| API-CLEAN-ASSET-READY | 删除 `AssetLogicalState Ready` 别名（仅 ReadyCpu/ReadyGpu） | [resources](resources.md) |
| API-CLEAN-SCENE-KEYS | Scene `fixture*Key` → `meshKey`/`materialKey`/`spriteKey`；注释改为 bind-table 语义 | [Scene](scene-ecs.md) · [3D](game-3d.md) |
| API-CLEAN-UI-STATS | `UIContextStatistics` phase dirty 单源：`structureDirty`/`layoutDirty`/`hitDirty`/`paintDirty`/`semanticsDirty` 由内部 `phaseDirty` mask 派生；去掉 public `dirty` 与并行 bool 双轨 | [UI](ui.md) |
| RENDER-LEDGER-SPI | `ISubmissionCompletionLedger` + `CpuSubmissionCompletionLedger` 实现；`RenderFramePacket` 走接口 | [rendering](rendering.md) · ADR 0016 |
| RENDER-LEDGER-INJECT | Host 持 `unique_ptr<ISubmissionCompletionLedger>`；可选 `createSubmissionCompletionLedger` 仅替换 CPU 记账实现；所有 composition 都在 present-return complete；mock 多态单测 | [rendering](rendering.md) · ADR 0016 |
| RUNTIME-001-INPUT | `blocksGameplayInputBelow`：下层 fixed/frame 使用空 action snapshot；`gameplayInputBlockedForDepth` + unit tests | [gameplay](gameplay.md) · ADR 0014 |
| RENDER-3D-TEX | Opaque3D unlit 采样 materialKey 绑定贴图（shader `s_texColor` + default white）；关闭「bind 不 draw」假完成 | [3D](game-3d.md) |
| TEST-001-GCC13-NULL | Docker Desktop + `linux-gcc13-vnext`：`tina_tests`/`tina_ui_tests`(255)/`tina_runtime_ui_tests`(83)/bridge(13)/`tina_sample_null` 300 帧；vcpkg baseline 与仓库一致；`artifacts/gates/test-001-linux-gcc13-null.json` | [Linux 证据](m12-evidence-linux.md) |
| TEST-001-GCC13-PLATFORM | Docker + Xvfb + `linux-gcc13-vnext-platform`：`tina_tests` + `tina_platform_glfw_tests`(34/34) + `tina_sample_platform --frames=60`；`artifacts/gates/test-001-linux-gcc13-platform.json` | [Linux 证据](m12-evidence-linux.md) |
| SAMPLE-FACADE-001 | `tina_sample_2d` 去手写 factories：`Desktop::CreateEngine(options)` + `wrapWindowSurfaceRenderDevice`；`DeviceCapture` 抽出 sample helper | [2D](game-2d.md) · [public-api](public-api.md) |
| SAMPLE-FACADE-3D | `tina_sample_3d` 同样走 `Desktop::CreateEngine` + wrap；`DeviceCapture.hpp` 抽出；去掉 Glfw/bgfx 直链 factories | [3D](game-3d.md) |
| TEST-001 | Docker Desktop 复验 tip：GCC13 Null + Platform/GLFW(Xvfb) + Clang22 Null + Clang22 ASan/UBSan/LSan；`libclang-rt-22-dev` 修 sanitizer 链接；证据 JSON 见 `artifacts/gates/test-001-linux-*.json` | [Linux 证据](m12-evidence-linux.md) |
| TEST-001-CLANG22-NULL | `linux-clang22-vnext`：tests/ui/runtime_ui/bridge + sample_null 300；`test-001-linux-clang22-null.json` | [Linux 证据](m12-evidence-linux.md) |
| TEST-001-CLANG22-SAN | `linux-clang22-vnext-sanitize`：同上 + ASan/UBSan/LSan；`test-001-linux-clang22-sanitize.json` | [Linux 证据](m12-evidence-linux.md) |
| UI-003-MAP | `buildUIDisplayList` content-scale 映射：logical 100×100 → fb 100/150/200 时 rect 与 clip 像素确定性；`UIRenderDisplayListTest.ContentScale*` | [UI](ui.md) · bridge tests |
| UI-003-VIS | `RunUi003VisualGate.ps1`：ROI 指纹 + 可选 baseline 比对（`tools/windows/baselines/ui-003-sample2d-960x540.json`）；blankLike 排除；`artifacts/gates/ui-003-visual-*.json` | [UI](ui.md) · CaptureSampleWindow |
| UI-003-SIZE | `tina_sample_2d --width/--height` + `RunUi003SizeMatrix.ps1`：content-scale-like 逻辑窗口 960/1200/1440/1280/1920；分尺寸 ROI baseline；`artifacts/gates/ui-003-size-matrix-*.json` | [UI](ui.md) · sample_2d |
| UI-003-METRICS | sample JSON：`logicalPixel*` / `framebufferPixel*` / `contentScale*`；gate 断言 `fb ≈ logical * scale` 与 capture client 一致 | [UI](ui.md) · sample_2d |
| UI-003-FONT-FP | `RunUi003VisualGate.ps1`：`fontFingerprint`（resolvedPath/source/fileName/sizeBytes/sha256/envTinaUiFontPath/freeTypeLikelyOn/identity）写入 gate JSON 与 baseline；baseline 含 fingerprint 时 mismatch **fail**（可选 `-AllowFontFingerprintMismatch` 仅跳过 ROI）；分尺寸 baseline schema 3 | [UI](ui.md) · baselines |
