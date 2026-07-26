# Public API

本文描述当前 `include/tina` 公共面和 CMake target。它不是未来 SDK 愿望清单；尚未存在的能力（通用
event queue、通用 GPU submission fence、完整 PBR 等）列在末尾。State 栈、FramePin 与 present-return CPU completion
首切片**已经存在**。

## 分层

| 层 | 使用者 | 入口 | 约束 |
| --- | --- | --- | --- |
| Game API | 普通游戏/样例 | `Tina::DesktopBootstrap` + Runtime/Scene/Asset/UI 等 Tina 模块 | 不接触具体 backend owner/native handle |
| Module API/SPI | Tina 模块与高级集成 | `include/tina/<module>`、`runtime/spi`、`integration` | 只暴露 Tina-owned 类型和窄 factory |
| Backend Private | GLFW/bgfx/FreeType/miniaudio/Box2D/cgltf/stb_image | `src/...` adapter 实现 | 第三方类型、宏、链接保持 PRIVATE |

当前没有已安装的 `Tina::GameSDK` 聚合 target、`install(EXPORT ...)`、版本化 package config 或外部 SDK
consumer 门禁。调用方按需链接现有模块 target；正式 SDK packaging 是后续工作。

## CMake targets

| Target | 公共角色 |
| --- | --- |
| `Tina::Core` | Result、time、memory、ID/hash、UTF-8、IO、diagnostics |
| `Tina::Platform` | Window/Input/PlatformFrame/backend SPI |
| `Tina::Task` | bounded IO/CPU/Main TaskSystem |
| `Tina::Render` | RenderDevice、Surface/Frame/Scene/UI DisplayList、GPU IDs |
| `Tina::Runtime` | EngineHost、Game Application/State、phase context、Action/Event facade |
| `Tina::DesktopBootstrap` | 普通 Windows/Linux Desktop 组合入口 |
| `Tina::Scene` | World/Entity/Transform、2D/3D components/extraction/Prefab、standalone Particle/Trail |
| `Tina::AssetFormat` | versioned Cooked payload/manifest types |
| `Tina::Asset` | Catalog、AssetSystem、Handle/Lease、Cooker helpers、typed parse/upload |
| `Tina::UI` | retained UI、Widget、text、semantics |
| `Tina::Audio` | backend-neutral AudioEngine/PCM、voice gain/pitch/pan/fade |
| `Tina::Physics2D` | optional Box2D-backed Tina API |

Adapter targets `Tina::PlatformGlfw`、`Tina::RenderBgfx`、`Tina::UIFreetype`、
`Tina::AudioMiniaudio` 主要用于 bootstrap/高级组合，不把第三方 header 传播给调用方。

## Core 约定

- 所有可恢复模块边界使用 `Core::Result<T>`/`Core::Status`；
- `Error` 提供稳定 domain/code、UTF-8 message、origin、native code 与 context；
- 公共文本/路径是 strict UTF-8；Windows 转换留在 adapter；
- generation ID 与 `AssetId`/`ContentHash` 是不同类型，不隐式转换；
- callback-only view/span/string_view 必须注明失效点。

公开头不允许依赖传递 include 才能编译；每个重要头有 header-isolation translation unit。

## Engine 与游戏入口

### 正确姿势（普通桌面游戏）

```cpp
// 1) 实现 IGameApplication：createInitialState + onShutdown
// 2) 实现 IGameState：onEnter/onExit/initialPolicy + 需要的相位
// 3) 组合并运行（不要自建主循环）
auto host = Tina::Desktop::CreateEngine(config);
if (!host) { /* handle error */ }
return host.value()->run(application);
```

- 帧逻辑只写在 `IGameState` 相位里；Application 不做逐帧工作。
- 优先 `Desktop::CreateEngine`；手写 `EngineCompositionFactories` 是测试/Headless/特殊集成的接线税。
- 不取得 `IRenderDevice*`、不缓存 phase Context/writer/span 跨回调。
- AssetSystem / Scene::World / ParticleSystem2D / Trail2D / Physics2D 由游戏 State（或样例）显式持有，
  不是 Host 内置模块。

普通桌面游戏调用：

```cpp
Core::Result<std::unique_ptr<EngineHost>>
Tina::Desktop::CreateEngine(const EngineConfig& config) noexcept;

Core::Result<std::unique_ptr<EngineHost>>
Tina::Desktop::CreateEngine(const EngineConfig& config,
                            Desktop::CreateEngineOptions options) noexcept;
```

`CreateEngineOptions::wrapWindowSurfaceRenderDevice` 可在产品/门禁路径包装已创建的
`IRenderDevice`（例如帧捕获装饰器），不暴露 bgfx/GLFW，也不替代 EngineHost 组合根。

高级测试/集成可使用：

```cpp
EngineHost::Create(const EngineConfig&, EngineCompositionFactories) noexcept;
```

`EngineCompositionFactories` 提供 Clock、Task、Platform/Render tagged composition、可选 Audio 与可选
primary UIContext factory。普通游戏应优先 `Desktop::CreateEngine`，不应动态拼装 native surface
backend，也不取得 `IRenderDevice*`。

`EngineHost` 在创建线程拥有全部 Runtime module，`run()` 只允许一次。跨线程 run 返回错误；错误线程
析构带 native owner 的 Host 会终止，避免在错误线程调用平台 API。

`EngineConfig::shutdownDeadline` 默认5秒，必须是 finite positive `Core::Duration`。它只预算
`EngineHost` 关闭过程中 `ITaskSystem::shutdownAndJoinFor()` 的 Worker-exit/join 阶段，不覆盖此前的
`AudioEngine::shutdown()`、`IRenderDevice::shutdown()`，也不是整个 Host shutdown 的总耗时上限。若该
TaskSystem 阶段返回 `TaskErrorCode::WaitTimeout`，Host 先写入 `runtime.lifecycle` Diagnostics，再
`std::terminate()`；不会 reset TaskSystem 或继续析构 Platform、Clock、Diagnostics 等剩余 owner。

## `IGameApplication` 与 `IGameState`

当前 Application 接口：

```cpp
createInitialState(GameStartupContext&)
onShutdown(GameShutdownContext&) noexcept
```

当前 State 接口：

```cpp
onEnter(GameStateEnterContext&)
onExit(GameStateExitContext&) noexcept
initialPolicy() const noexcept
fixedUpdate(FixedUpdateContext&)
updateFrame(FrameUpdateContext&)
extractRenderScene(RenderSceneExtractionContext&) const
updateUI(UIUpdateContext&)
```

Runtime 私有持有 `GameStateStack`（定容 8）。`FrameUpdateContext` 提供 `requestPush` /
`requestPop` / `requestReplace` / `requestPolicyChange`：每帧最多一个 structural command，在
`updateFrame` 之后、`extractRenderScene` 之前唯一 commit。各相位自顶向下调用栈上 State，直到
某层 policy 的相位阻断字段阻止继续向下。structural command 仅栈顶 context 可排队。Game SDK
不获得可变 stack 引用。

`GameStatePolicy`（`include/tina/runtime/GameState.hpp`）：

| 字段 | 行为 |
| --- | --- |
| `blocksFixedUpdateBelow` / `blocksFrameUpdateBelow` / `blocksRenderBelow` / `blocksUIUpdateBelow` | 截断该相位向下分发 |
| `blocksUIUpdateBelow` | 只挡下层 `updateUI`；**不**挡当帧 UI route |
| `blocksGameplayInputBelow` | 下层 fixed/frame 收到空 suppressed Action snapshot；不截断回调本身 |

完整帧序与输入四段式见 [Runtime](runtime.md)。

## Phase Context

| Context | 暴露 | 生命周期 |
| --- | --- | --- |
| `GameStartupContext` | EngineConfig、Platform event subscription | `createInitialState()` 回调 |
| `GameStateEnterContext` | subscription、primary UI root builder | `onEnter()` 回调 |
| `FixedUpdateContext` | frame/fixed timing、Simulation Action、可选 Audio | `fixedUpdate()` 回调 |
| `FrameUpdateContext` | timing、Frame Action、可选 Audio、exit-after-frame；仅栈顶可借用 `InputActionRebinding` | `updateFrame()` 回调 |
| `RenderSceneExtractionContext` | phase-local `RenderSceneWriter` | extraction 回调 |
| `UIUpdateContext` | root-scoped UI updater | `updateUI()` 回调 |
| Exit/Shutdown Context | stop cause、只借用 failure Error | 对应 callback |

这些 Context 不可复制/移动，地址、内部 view、writer 和模块 pointer 都不得保存。可以保存的 owner 是明确
RAII token/root/lease，而不是 Context 本身。

## Platform 与 Input

`IPlatformBackend` 发布 `PlatformPollResult` 与 `PlatformFrameView`。PlatformFrame 包含 final Window/
Gamepad snapshot、ordered lifecycle/input transition、strict UTF-8 text/composition。所有 storage 创建时
固定容量；view 到下一次 poll/build 失效。

公开输入类型覆盖 Key、Pointer、标准 Gamepad、TextInput、TextComposition、Cancel/Reset。GLFW/native
枚举不会越过 adapter。`PlatformEventSubscription` 是 generation-safe RAII token；dispatcher owner 和
dispatch/shutdown 不暴露给游戏。

Gameplay Action 只有 Runtime 一套公开模型：`InputActionMapConfig::bindings` 保存带稳定
`InputBindingId` 的 `InputActionBinding`，同一 binding variant 表达 digital control 与
`StandardGamepadAxisBinding`。axis 支持 Signed/PositiveHalf/NegativeHalf/Trigger、gameplay deadzone 与
scale；多个 keyboard/pointer/所有已连接 Gamepad generation 的贡献按每个 Action 固定的
`SumClamped` 或 `StrongestMagnitude` 合成。Simulation/Frame snapshot 公开排序后的
`InputActionState{action, value}` 和 Started/ValueChanged/Completed/Cancelled transition；调用方通过
`value()` / `isActive()` 查询，不读取 physical held state。

UI transition consumption 与 continuous-control claim 先于 Action mapping：digital source 抑制到真实
release，axis 抑制到 neutral/deadzone，均不会穿透 gameplay。只有栈顶 State 可从
`FrameUpdateContext::inputActionRebinding()` 借用窄 facade；`begin`/`commit` 把修改排到下一 mapping
frame 原子应用，冲突显式选择 `Reject` 或 `Swap`，`Capturing`/`Queued` 均可取消。绑定的 Gamepad
generation 断连或 raw reset 失去该 generation 时 transaction 取消，不自动迁移到重连设备。

## Task

`ITaskSystem` 提供 `scheduleIo`、`scheduleCpu`、`postMain`、`pumpMain`、`requestStop`、
`shutdownAndJoinFor`、`shutdownAndJoin`。有界关闭只接受 finite positive `Core::Duration`；非法值不触发
stop，timeout 返回 `TaskErrorCode::WaitTimeout` 并保留 stopping 对象/Worker ownership 供后续重试。
`TaskSystemCreateParams::cpuWorkerCount=0` 在直接工厂中表示 CPU domain disabled；
`Desktop::CreateEngine` 经 `resolveDesktopTaskSystemParams` 将 0 解析为 `max(1, hardware_concurrency-1)`。
`TaskGroup` 提供结构化 pending/wait，不允许 detach/强杀。

## Render

`IRenderDevice` 核心方法是 `submitFrame`、`present`、`statistics`、`shutdown`。可选资源 API包括：

- RGBA8 Texture2D create/destroy、Sprite2D key binding；
- P3N3UV2/U16 StaticMesh create/destroy、Mesh3D key binding；
- material key → base-color / metallic-roughness / normal texture binding；
- material key → metallic/roughness factors；
- experimental Opaque3D `Mesh3DLightingDesc`（同步提交0..4 directional lights + 非负 ambient）；
- primary framebuffer RGBA8 capture。

`GpuTextureId`/`GpuMeshId` 是 RenderDevice generation handle，不是 AssetHandle。当前 `RenderFrame` 的
Surface/Scene/UI/Glyph view 只在 `submitFrame()` 调用内有效；backend 不能保存。Runtime 使用
`RenderFramePacket`、`FramePin` 与 submission completion ledger（成功 present 返回后关闭 CPU 借用，见
`include/tina/render/FramePin.hpp`）。`SubmissionTicket` 不可复制且绑定签发 ledger，packet 取得唯一所有权
后负责 complete/abandon。它不代表 GPU execution/retirement；Texture2D/StaticMesh 使用独立的
`retire*` + backend marker，不能把两类 completion 混用。

`RenderSceneBuilder/Writer` 提供 fixed-capacity Camera2D/PerspectiveCamera3D/Sprite2D/Mesh3D extraction，
commit 后返回 borrowed view。`UIDisplayList` 支持 SolidQuad/Glyph 与 axis-aligned clip。

## UI

`UIContext`、`UINodeId`、`UIRootOwner` 与 builder/updater 提供 retained tree。当前 Widget：Root、Panel、
Label、Button、Checkbox、Slider、ProgressBar、RadioButton、单行 TextEdit。

游戏通过 Runtime phase facade 创建/更新主窗口 root，不获得裸 UIContext。Text 使用 strict UTF-8；
可选 FreeType、R8 Glyph atlas、semantics snapshot 与 `UIAccessibilityTree`/probe provider 均为 Tina API。
可选 Windows UIA 私有 adapter（`TINA_BUILD_UI_UIA`）映射 UIA 形属性，公开头无 COM；产品路径可经
EngineHost 自动附着 HWND HostBridge（`IRawElementProviderSimple` 首切片）。Narrator 人工金标与
Linux AT-SPI 仍未完成（UI-002）。

## Scene

`Scene::World` 是 fixed-capacity、generation entity owner，提供 Transform hierarchy、Camera2D/
SpriteRenderer2D/PerspectiveCamera3D/MeshRenderer3D。`extractRenderSceneFromWorld()` 写调用方的
RenderSceneWriter；`instantiatePrefab()` 事务式创建 hierarchy，并可通过 AssetId resolver 映射 mesh/
material key。

`ParticleSystem2D` 与 `Trail2D` 是独立 Scene owners，不属于 World/ECS，也不依赖 Asset 或 bgfx。二者
在 `Create()` 中通过调用方 PMR resource 建立固定容量 storage；成功的 emit/append、update、extract
不增长 storage。它们直接复用调用方 phase-local `RenderSceneWriter` 提交 backend-neutral Sprite2D。

`ParticleSystem2DConfig::randomSeed` 对所有值（包括0）都是固定确定 seed。`emitBurst()` 的 validation、
容量与稳定 key preflight 失败不改变 RNG、next key 或 live set；成功 key 单调分配且过期/clear 后不
复用。`update()` 先检查全部 next age 和 survivor position，失败时所有粒子不变；extract 按 normalized
age 插值 size/color。

`Trail2D::appendPoint()` 的第一点建立 anchor，之后每点生成一段；`breakTrail()` 使下一点建立新 anchor。
segment 各自从创建时计算 lifetime/age，width 按 normalized age 在 start/end width 间线性插值。
非法 geometry、容量或 key exhaustion 不修改 anchor/segments/next key；update 对所有 age 先 preflight，
成功后才推进和移除过期段。稳定 segment key 单调且不复用。

当前没有公开 SceneManager、ECS registry 或 Runtime-owned World capability。EnTT 不在公开面，也未被当前
Scene target 使用。

## Asset 与 Cooked

`AssetFormat` 定义 versioned manifest/cooked wire format 和 Texture2D/StaticMesh/Material/Prefab/TileMap/
TileMapChunk/AudioClip 等 typed payload。Runtime 不解析源 glTF/WAV/image；cgltf/stb_image 与源文件解析只在
Cooker/tool。

TileMap 的唯一当前 root wire contract 是 schema v3。`TileMapPayloadView` 按 authoring 顺序通过
`layerAt()/findLayer(TileMapLayerId)` 暴露 tile/object layer；稳定 layer/object ID 都是 map-wide 非零唯一
`u32`。layer 与 object 都有独立 visibility；name/properties 是 strict UTF-8 borrowed views；object kind
当前只有 Point 和 axis-aligned Rectangle。tile layer 保存按坐标排序的非空 chunk ref，缺失坐标是已知
空块；cell 位于独立 `TileMapChunk` v1 payload。旧 schema v1/v2 均不兼容，也没有默认单层 API。

`TileMapInstance` 拷贝验证后的 root metadata/tileset 定义，只为当前 resident chunk 持有可变 cells、
content revision 与 residency generation。`layer(id)` 返回借用到 instance-owned payload 的 metadata/object
view；`tileIdAt()`、`tileInfoAt()`、`setTile()`、`chunkRevision()`、`querySolidAabb()`、chunk extraction/
dirty cache/sprite emit 与 `TileMapGridCollision` 都要求显式 `TileMapLayerId`。引用存在但尚未驻留时返回
`TileMapChunkNotResident`；误选 object layer 与不存在 layer 分别返回
`TileMapLayerTypeMismatch`/`TileMapLayerNotFound`。grid SPI 的 `materialFlagsAt()` 仍按约定把无效、空或
未驻留 cell 表现为0。visibility=false 会跳过可见 chunk/sprite emit，但不禁止显式用作 collision。

`TileMapStream::Create()` 消费 root/tileset `AssetLease` 并拥有 resident `TileMapInstance`。调用顺序必须是
`updateDemand() -> AssetSystem::pump() -> commitReady()`。load window 中的 desired chunk 单独超过
resident capacity 时 failure 是 transactional，旧 active set 保持不变；retain window 只是 optional
cache，overflow 时按最近一次成功 demand update 的 recency 自动淘汰，读取 API 不 touch recency。
`map()` 是借用视图：先把 stream 放到最终地址再创建 `TileMapGridCollision`，stream 不得在
borrower 存活时移动，并必须早于它所引用的 `AssetSystem` 析构。

`AssetSystem` 提供 request/load/pump、generation slot 与 typed state。`AssetHandle` 是弱 lookup；
`AssetLease` 强保活 CPU payload。逻辑 invalidation 不等于物理释放。产品 helper 可把 Cooked Texture2D/
StaticMesh 上传到 RenderDevice，并建立 backend key binding；`AssetSystem::retireTexture2D` /
`retireStaticMesh` 把 lease 移入 `FramePin`，成功后弱 lookup 立即失效，backend completion 后才释放 payload。
失败不消费 pin，Asset 仍保持可用。`drainGpuRetirements()` 用于 owner-thread teardown。

multi-mesh / multi-primitive glTF Cooker：每个 TRIANGLES prim 生成 distinct StaticMesh/Material AssetId；
单 prim 节点直接引用，多 prim mesh 在 Prefab 中展开为 transform 父 + 子 draw 节点。Material v2 含
metallic/roughness factors 与可选 baseColor/MR/normal Texture2D deps。Runtime Opaque3D 为 experimental
MR hybrid（`setMesh3DMaterialTextureBinding` + 可选 `setMesh3DMaterialMetallicRoughnessTextureBinding`；
`setMesh3DMaterialFactors` + 可选 normal 贴图 + 有界0..4 directional lights）。完整 light component/IBL/shadow
尚未完成。

## Audio 与 Physics

`AudioEngine` 提供 generation voice、bus、bounded command/completion、non-owning clip PCM、Tina-owned
bounded stream ring 与 realtime mix。
voice 控制入口为 `setVoiceGain()`、`setVoicePitch()`、`setVoicePan()`、
`startVoiceFade(AudioVoiceFadeDesc)`、`cancelVoiceFade()` 与 `voicePlaybackState()`；gain、pitch、pan 的公开
范围分别为 `[0,1]`、`[0.25,4]`、`[-1,1]`。所有 setter 只接受 live owner voice 和有限值，失败不发布
半份 realtime 状态。

pitch 使用 `sourceSampleRate / outputSampleRate * pitch` 的 source step 与线性插值。stereo pan 使用兼容
既有 center 响度的 linear balance，hard-left/right 只衰减对侧；mono 输出忽略 pan。fade 由 target gain、
正的 `Core::Duration` 与 `KeepPlaying/StopVoice` end action 构成，并在 realtime callback block 边界
start/cancel；cancel 保留 callback 已推进到的 current gain。

clip PCM 调用方必须保活到底层 voice stop/completion；产品 2D 用 `AssetLease` 持有 Cooked AudioClip。普通
`createVoice()` 由调用方显式销毁；`playOneShotPcm()` 的 transient voice 在 Stop、fade-to-stop 或 natural
end 被 pump 后自动 retire，completion 携带的 one-shot ID 随后允许 stale。miniaudio device/decode 留在
adapter。

bounded stream 公开入口为 `playPcmStream()`、`submitPcmStreamFrames()`、`signalPcmStreamEof()`、
`cancelPcmStream()` 与 `pcmStreamState()`。Create 一次性预分配每 voice 的双声道最大 ring；descriptor 的
逻辑容量至少为2且不得超过配置。`AudioPcmStreamChunkView` 只是调用期 borrow，successful submit 返回前已
整块复制，容量不足不发布半块。owner thread 是唯一 producer；Task worker 必须 marshal，`Tina::Audio`
不依赖 `Tina::Task`，miniaudio 也没有 producer API。`mixRealtime()` 只允许一个 non-overlapping realtime
consumer。EOF 排空后单次 Stopped、cancel 单次 Cancelled，terminal completion 受 ring/reader backpressure
时延迟但不丢，成功 pump 后 transient stream voice 自动 retire。

`PhysicsWorld2D` 提供 Box2D-backed fixed-step world，以及相互独立的 body/shape/joint generation ID。
唯一创建模型是 `createBody()` + `createShape()`；backend-neutral `PhysicsShape2DDesc` 支持
Box/Circle/Capsule 与 sensor，一个 body 可拥有多个 shape。sensor enter/exit 通过 contact view 的
`isSensor` 表达；joint 当前为 Distance，并有 create/query/destroy 与关联 body 级联 retirement。公共面还
包含 query、deferred command 与 Tile grid static body helper。Box2D 类型不出现在 public header；
Jolt/Physics3D 尚未接入。

## Handle 与借用速查

| 类型 | 所有权 | 典型失效点 |
| --- | --- | --- |
| `EntityId/UINodeId/PhysicsBodyId/PhysicsShapeId/PhysicsJointId/...` | registry owner | erase/owner destroy/generation reuse |
| `AssetHandle` | 弱 | slot invalidation/reuse |
| `AssetLease` | 强 CPU payload owner | lease reset/destroy |
| `AudioVoiceId` | `AudioEngine` generation owner | 显式 destroy、engine shutdown/generation reuse；one-shot/stream terminal completion pump 后自动 retire |
| `TileMapStream::map()` | `TileMapStream` 借用 | stream move/shutdown/destroy；borrower 必须先销毁 |
| `GpuTextureId/GpuMeshId` | RenderDevice | retire/destroy 时逻辑失效；有外部 pin 时由 completion marker（或 shutdown hard drain）证明完成，无 pin fallback 交给 backend deferred destroy |
| PlatformFrame view | Platform | 下一次 poll/build |
| Phase context/writer | Runtime callback | callback 返回 |
| committed UI view | UIContext | 下一次对应 commit/context destroy |
| RenderFrame view | Runtime builder | `submitFrame()` 返回 |
| `AudioPcmClipView` | non-owning | 调用方 payload 释放；必须晚于 voice completion |
| `AudioPcmStreamChunkView` | 调用期 non-owning | `submitPcmStreamFrames()` 返回；成功数据已复制到 Tina-owned ring，失败零发布 |
| `TileMapLayerPayloadView` / object/property view | `TileMapPayloadView` 或 `TileMapInstance` 借用 | backing payload 释放；instance view 还会在 instance move/destroy 时失效 |

不要手工构造 generation ID、跨 owner 混用、持久化 Runtime handle，或把 non-owning view包装成“看似
拥有”的裸 pointer成员。

## 尚不存在或仅部分落地的公共能力

**已存在（勿再文档成“没有”）：** `GameStateStack` 与 structural commands；相位 `blocks*Below` 与
`blocksGameplayInputBelow` 空 snapshot；`RenderFramePacket` / `FramePin` / present-return CPU
submission ledger；Windows UIA provider + HWND HostBridge 首切片。

**仍不存在或未完成：**

- 多 World / editor orchestration；
- 通用 Runtime owning event queue；
- 通用 GPU submission fence（现有 readback marker 只服务 Texture/Mesh retirement）；
- 完整 PBR/IBL/shadow、light component/culling 与通用 pass scheduler；
- TileMap 优先级 IO 调度、editor orchestration、旧 schema migration 与自动 gameplay 生成；
- 完整 Focus Scope / Modal / 持久 Capture、复杂 text / 虚拟列表；
- Narrator 合规金标、Linux AT-SPI；
- Jolt Physics3D；
- 正式 `Tina::GameSDK` install/export/package ABI。

任务状态见 [Backlog](backlog.md)。修改公开头后必须构建 header-isolation/consumer、扫描第三方 token，
并按 [测试说明](testing.md) 运行受影响 executable 与 sample。
