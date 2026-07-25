# Public API

本文描述当前 `include/tina` 公共面和 CMake target。它不是未来 SDK 愿望清单；尚未存在的能力（通用
event queue、真 GPU fence、PBR 等）列在末尾。State 栈、FramePin 与 Null completion 首切片**已经存在**。

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
- AssetSystem / Scene::World / Physics2D 由游戏 State（或样例）显式持有，不是 Host 内置模块。

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
- material key → base-color / metallic-roughness / normal texture binding；
- material key → metallic/roughness factors；
- experimental Opaque3D key/fill directional lights（非负 RGB + 非负 ambient）；
- primary framebuffer RGBA8 capture。

`GpuTextureId`/`GpuMeshId` 是 RenderDevice generation handle，不是 AssetHandle。当前 `RenderFrame` 的
Surface/Scene/UI/Glyph view 只在 `submitFrame()` 调用内有效；backend 不能保存。Runtime 使用
`RenderFramePacket`、`FramePin` 与 submission completion ledger（Null/present-sync 首切片，见
`include/tina/render/FramePin.hpp`）；真实 GPU fence 驱动的 completion 尚未实现。

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

当前没有公开 SceneManager、ECS registry 或 Runtime-owned World capability。EnTT 不在公开面，也未被当前
Scene target 使用。

## Asset 与 Cooked

`AssetFormat` 定义 versioned manifest/cooked wire format 和 Texture2D/StaticMesh/Material/Prefab/TileMap/
AudioClip 等 typed payload。Runtime 不解析源 glTF/WAV/image；cgltf/stb_image 与源文件解析只在
Cooker/tool。

`AssetSystem` 提供 request/load/pump、generation slot 与 typed state。`AssetHandle` 是弱 lookup；
`AssetLease` 强保活 CPU payload。逻辑 invalidation 不等于物理释放。产品 helper 可把 Cooked Texture2D/
StaticMesh 上传到 RenderDevice，并建立 backend key binding。

multi-mesh / multi-primitive glTF Cooker：每个 TRIANGLES prim 生成 distinct StaticMesh/Material AssetId；
单 prim 节点直接引用，多 prim mesh 在 Prefab 中展开为 transform 父 + 子 draw 节点。Material v2 含
metallic/roughness factors 与可选 baseColor/MR/normal Texture2D deps。Runtime Opaque3D 为 experimental
MR hybrid（`setMesh3DMaterialTextureBinding` + 可选 `setMesh3DMaterialMetallicRoughnessTextureBinding`；
`setMesh3DMaterialFactors` + 可选 normal 贴图 + key/fill directional lights）。完整 light system/IBL/shadow
尚未完成。

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

## 尚不存在或仅部分落地的公共能力

**已存在（勿再文档成“没有”）：** `GameStateStack` 与 structural commands；相位 `blocks*Below` 与
`blocksGameplayInputBelow` 空 snapshot；`RenderFramePacket` / `FramePin` / Null 或 present-sync
completion ledger；Windows UIA provider + HWND HostBridge 首切片。

**仍不存在或未完成：**

- 多 World / editor orchestration；
- 通用 Runtime owning event queue；
- 真实 GPU fence 驱动的 submission completion；
- PBR Material / lighting / 通用 pass scheduler；
- 完整 Focus Scope / Modal / 持久 Capture、复杂 text / 虚拟列表；
- Narrator 合规金标、Linux AT-SPI；
- Jolt Physics3D；
- 正式 `Tina::GameSDK` install/export/package ABI。

任务状态见 [Backlog](backlog.md)。修改公开头后必须构建 header-isolation/consumer、扫描第三方 token，
并按 [测试说明](testing.md) 运行受影响 executable 与 sample。
