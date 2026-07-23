# Public API

本文描述当前 `include/tina` 公共面和 CMake target。它不是未来 SDK 愿望清单；不存在的 State stack、
event queue、packet/pin 或 PBR API 会明确列在末尾。

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
| `Tina::Scene` | World/Entity/Transform、2D/3D components/extraction/Prefab |
| `Tina::AssetFormat` | versioned Cooked payload/manifest types |
| `Tina::Asset` | Catalog、AssetSystem、Handle/Lease、Cooker helpers、typed parse/upload |
| `Tina::UI` | retained UI、Widget、text、semantics |
| `Tina::Audio` | backend-neutral AudioEngine/PCM |
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

普通桌面游戏调用：

```cpp
Core::Result<std::unique_ptr<EngineHost>>
Tina::Desktop::CreateEngine(const EngineConfig& config) noexcept;
```

高级测试/集成可使用：

```cpp
EngineHost::Create(const EngineConfig&, EngineCompositionFactories) noexcept;
```

`EngineCompositionFactories` 提供 Clock、Task、Platform/Render tagged composition、可选 Audio 与可选
primary UIContext factory。普通游戏不应动态拼装 native surface backend，也不取得 `IRenderDevice*`。

`EngineHost` 在创建线程拥有全部 Runtime module，`run()` 只允许一次。跨线程 run 返回错误；错误线程
析构带 native owner 的 Host 会终止，避免在错误线程调用平台 API。

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

Runtime 目前只持有一个 committed State。不存在公开 `GameStateStack`、`GameStateCommands` 或
push/pop/replace API；`GameStatePolicy` 已采样但尚无下层 State 调度。对应扩展见 `RUNTIME-001`。

## Phase Context

| Context | 暴露 | 生命周期 |
| --- | --- | --- |
| `GameStartupContext` | EngineConfig、Platform event subscription | `createInitialState()` 回调 |
| `GameStateEnterContext` | subscription、primary UI root builder | `onEnter()` 回调 |
| `FixedUpdateContext` | frame/fixed timing、Simulation Action、可选 Audio | `fixedUpdate()` 回调 |
| `FrameUpdateContext` | timing、Frame Action、可选 Audio、exit-after-frame | `updateFrame()` 回调 |
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

## Task

`ITaskSystem` 提供 `scheduleIo`、`scheduleCpu`、`postMain`、`pumpMain`、`requestStop`、
`shutdownAndJoin`。`TaskSystemCreateParams::cpuWorkerCount=0` 在直接工厂中表示 CPU domain disabled；
`Desktop::CreateEngine` 经 `resolveDesktopTaskSystemParams` 将 0 解析为 `max(1, hardware_concurrency-1)`。
`TaskGroup` 提供结构化 pending/wait，不允许 detach/强杀。

## Render

`IRenderDevice` 核心方法是 `submitFrame`、`present`、`statistics`、`shutdown`。可选资源 API包括：

- RGBA8 Texture2D create/destroy、Sprite2D key binding；
- P3N3UV2/U16 StaticMesh create/destroy、Mesh3D key binding；
- material key → base-color texture binding；
- primary framebuffer RGBA8 capture。

`GpuTextureId`/`GpuMeshId` 是 RenderDevice generation handle，不是 AssetHandle。当前 `RenderFrame` 的
Surface/Scene/UI/Glyph view 只在 `submitFrame()` 调用内有效；backend 不能保存。owning packet/FramePin/
completion 尚未实现。

`RenderSceneBuilder/Writer` 提供 fixed-capacity Camera2D/PerspectiveCamera3D/Sprite2D/Mesh3D extraction，
commit 后返回 borrowed view。`UIDisplayList` 支持 SolidQuad/Glyph 与 axis-aligned clip。

## UI

`UIContext`、`UINodeId`、`UIRootOwner` 与 builder/updater 提供 retained tree。当前 Widget：Root、Panel、
Label、Button、Checkbox、Slider、ProgressBar、RadioButton、单行 TextEdit。

游戏通过 Runtime phase facade 创建/更新主窗口 root，不获得裸 UIContext。Text 使用 strict UTF-8；
可选 FreeType、R8 Glyph atlas 和 semantics snapshot均为 Tina API。平台 UIA/AT-SPI 尚未实现。

## Scene

`Scene::World` 是 fixed-capacity、generation entity owner，提供 Transform hierarchy、Camera2D/
SpriteRenderer2D/PerspectiveCamera3D/MeshRenderer3D。`extractRenderSceneFromWorld()` 写调用方的
RenderSceneWriter；`instantiatePrefab()` 事务式创建 hierarchy，并可通过 AssetId resolver 映射 mesh/
material key。

当前没有公开 SceneManager、ECS registry 或 Runtime-owned World capability。EnTT 不在公开面，也未被当前
Scene target 使用。

## Asset 与 Cooked

`AssetFormat` 定义 versioned manifest/cooked wire format 和 Texture2D/StaticMesh/Material/Prefab/TileMap/
AudioClip 等 typed payload。Runtime 不解析源 glTF/WAV/image；cgltf/stb_image 与源文件解析只在
Cooker/tool。

`AssetSystem` 提供 request/load/pump、generation slot 与 typed state。`AssetHandle` 是弱 lookup；
`AssetLease` 强保活 CPU payload。逻辑 invalidation 不等于物理释放。产品 helper 可把 Cooked Texture2D/
StaticMesh 上传到 RenderDevice，并建立 backend key binding。

multi-mesh glTF Cooker 当前为每个 mesh 生成 distinct StaticMesh/Material AssetId，并让 Prefab node 引用；
baseColorTexture 可 cook 为 Texture2D required dependency。单 mesh 多 primitive、纹理产品绑定/安全策略与
PBR 其他通道尚未完成。

## Audio 与 Physics

`AudioEngine` 提供 generation voice、bus、bounded command/completion、non-owning PCM 与 realtime mix。
PCM 调用方必须保活到底层 voice stop/completion；产品 2D 用 AssetLease持有 Cooked AudioClip。miniaudio
device/decode 留在 adapter。

`PhysicsWorld2D` 提供 Box2D-backed fixed-step world、body/shape generation ID、contact/query/deferred
command 与 Tile grid static body helper。Box2D 类型不出现在 public header。Jolt/Physics3D 尚未接入。

## Handle 与借用速查

| 类型 | 所有权 | 典型失效点 |
| --- | --- | --- |
| `EntityId/UINodeId/PhysicsBodyId/...` | registry owner | erase/owner destroy/generation reuse |
| `AssetHandle` | 弱 | slot invalidation/reuse |
| `AssetLease` | 强 CPU payload owner | lease reset/destroy |
| `GpuTextureId/GpuMeshId` | RenderDevice | destroy/device shutdown |
| PlatformFrame view | Platform | 下一次 poll/build |
| Phase context/writer | Runtime callback | callback 返回 |
| committed UI view | UIContext | 下一次对应 commit/context destroy |
| RenderFrame view | Runtime builder | `submitFrame()` 返回 |
| `AudioPcmClipView` | non-owning | 调用方 payload 释放；必须晚于 voice completion |

不要手工构造 generation ID、跨 owner 混用、持久化 Runtime handle，或把 non-owning view包装成“看似
拥有”的裸 pointer成员。

## 尚不存在的公共能力

- State stack/commands、多 World/editor orchestration；
- 通用 Runtime owning event queue；
- owning RenderFramePacket、FramePin、submission completion；
- PBR Material/lighting/pass scheduler；
- UIA/AT-SPI、完整 Focus Scope/Modal/Capture、复杂 text/虚拟列表；
- Jolt Physics3D；
- 正式 `Tina::GameSDK` install/export/package ABI。

任务状态见 [Backlog](backlog.md)。修改公开头后必须构建 header-isolation/consumer、扫描第三方 token，
并按 [测试说明](testing.md) 运行受影响 executable 与 sample。
