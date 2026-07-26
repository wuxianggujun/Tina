# Tina 必要功能推进计划

## 目的与执行规则

本计划覆盖 Tina vNext Runtime 的必要 2D/3D 能力切片。事实优先级固定为：当前源码、CMake target、测试和 sample 实际运行结果；其次是 Accepted ADR；主题文档仅作为线索。每个切片完成时，必须同步更新本文件、`docs/backlog.md` 和对应主题文档。

统一约束：

- `EngineHost` 是唯一非全局组合根；不新增 Singleton 或 Service Locator。
- 公共头和 Game SDK 不暴露 bgfx、GLFW、Box2D、cgltf 等第三方类型。
- Runtime 只读取 Cooked 产物；Cooker 失败时不发布半包；`AssetId` 在 load 时不得随机生成。
- 破坏性 schema/API 变更一次性更新 cooker、runtime、测试、sample recipe、门禁字段和文档；不保留旧格式双读或旧 API 兼容别名。
- 每项完成前运行最小受影响 target、直接 GoogleTest executable 和对应 sample 短 smoke；不以 CTest 替代直接测试。

## 切片总览

| 顺序 | 工作项 | 当前状态 | 完成定义 |
| --- | --- | --- | --- |
| N1 | 2D-TILEMAP-LAYERS | 已完成 | 有序 tile/object layer、稳定 ID、visibility/properties、显式渲染和碰撞 layer 选择已贯通 Cooked、runtime、sample 与测试 |
| N2 | 2D-PHYSICS-EXPAND | 已完成 | backend-neutral 多 shape、sensor enter/exit、distance joint、独立 generation handle 与级联 retirement 已覆盖；Box2D 保持 PRIVATE |
| N3 | 2D-INPUT-ADV | 已完成 | Runtime 单一 unified binding 已覆盖 analog deadzone/缩放/合成、运行时 rebind、UI consume/claim 后输入不穿透 |
| N4 | 3D RENDER-001 | 已完成 | Opaque3D 使用唯一有界 0..4 directional-light 提交；Null/bgfx/shader/sample/test 同步，完整 PBR 边界保持明确 |
| N5 | RENDER-FENCE | 已完成 | CPU submission completion 与 GPU resource retirement 已分离；bgfx readback marker、AssetLease pin、suspend/shutdown drain 与测试已贯通 |
| N6 | 2D-TILEMAP-STREAM | 已完成 | TileMap v3 root/TileMapChunk v1、deferred dependency、固定容量 Camera/layer demand/cancel/unload、resident generation dirty cache 与产品 sample 已贯通 |
| N7 | 2D-AUDIO-ADV | 已完成 | voice gain/pitch/pan、可取消 fade、线性重采样、one-shot retirement，以及 bounded PCM streaming 的原子 submit、EOF/underrun/cancel、terminal backpressure 与 shutdown 已贯通测试和产品 sample |
| N8 | 2D-FX | 已完成 | standalone ParticleSystem2D/Trail2D 的固定容量 PMR、确定性/事务失败/lifetime/width 契约已贯通 `tina_scene_tests` 与 product-2d schema 9 |
| N9 | RUNTIME-SHUTDOWN-DEADLINE | 已完成 | `ITaskSystem` 有界 stop/join、timeout ownership retention/retry 与 `EngineHost` Diagnostics + terminate hard boundary 已贯通 `tina_tests` |
| N10 | ASSET-HANDLE-SCENE-2D-A1 | 已完成 | World Sprite 组件改存 weak AssetHandle，extract 借用零分配 resolver；资源失败 fail closed，完整 ASSET-HANDLE-SCENE 仍为 Partial |

## N1 - 2D-TILEMAP-LAYERS

### 已完成契约

1. N1 交付时 `TileMapPayload` 是 schema v2；N6 已将唯一当前格式替换为 v3 stream root，v1/v2 均不双读。
2. Tile/object layer 的 map-wide 非零唯一稳定 ID、visibility、strict UTF-8 name/properties 契约延续到 v3；tile cells 已迁入独立 chunk asset。
3. Object ID 也 map-wide 非零唯一；对象支持 point 与 axis-aligned rectangle，并拥有独立 visibility 与 UTF-8 name/properties。
4. `TileMapInstance`、chunk view/dirty cache/render 和 solid query 全部围绕显式 `TileMapLayerId` 工作；不存在默认第0层。
5. `TileMapGridCollision(map, layerId)` 显式选择碰撞层。hidden tile layer 不渲染，但仍可参与 collision。
6. N1 建立的 Tileset/localId 发布前验证仍保留；N6 进一步验证 v3 root 与 deferred chunk 一一对应。

### 唯一 recipe 语法

```text
tilemap <id> <tileset-id> <width> <height> <cell-size>
tilelayer <layer-id> <0|1-visible> <name>
property <key> <value>
row <local-id>...
endlayer
objectlayer <layer-id> <0|1-visible> <name>
property <key> <value>
point <object-id> <0|1-visible> <name> <x> <y>
rectangle <object-id> <0|1-visible> <name> <x> <y> <width> <height>
objectproperty <object-id> <key> <value>
endlayer
endtilemap
```

`row` 只能位于 `tilelayer`；`point/rectangle/objectproperty` 只能位于 `objectlayer`；所有 layer 和地图
必须显式闭合。旧的裸 `row` 单层格式会被拒绝。recipe 的 name/key/value 当前是不含空白的单 UTF-8
token；payload writer/parser 继续执行 strict UTF-8/NUL/长度校验。

### 完成结果

| 阶段 | 完成结果 |
| --- | --- |
| N1.1 | 当时以 payload schema v2 writer/parser/view 替换 v1 单层字段，并验证 ID、UTF-8、properties、几何、容量与尾随字节；当前格式见 N6 |
| N1.2 | recipe 仅接受显式 layer block；package validation 建立 required Tileset dependency 与 tile localId 验证，当前 v3/chunk 扩展见 N6 |
| N1.3 | runtime tile storage、chunk revision/dirty/cache/render/collision 全部携带显式 layer ID；错误区分 layer not found/type mismatch |
| N1.4 | sample 使用 visual=10、collision=20、gameplay objects=30；消费 point 101 与 rectangle 102；JSON 输出 `objectLayerConsumed`/`objectLayerObjects` |
| N1.5 | payload/Cooker/runtime/chunk/Physics tests 覆盖 round-trip、拒绝路径、hidden render 与 explicit collision layer |
| N1.6 | `game-2d/resources/physics/testing/public-api/design/roadmap/backlog` 已同步当前契约与后置边界 |

### 验收

```powershell
cmake --build --preset windows-vnext-bgfx-debug --target tina_tests tina_sample_2d -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_tests.exe --gtest_filter="TileMap*:CatalogCook*:TileChunk*:CharacterController*:TileMapPhysics*"
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
```

N1 当时不包含 chunk streaming；该项已由 N6 关闭。仍不包含关卡/Scene/动画编辑器、undo/redo、旧 schema
migration、自动从任意 object layer 生成完整 gameplay、2D 光照或导航。sample 对 object 101/102 的消费是产品垂直切片，不是
通用 editor/gameplay pipeline。

## N2 - 2D-PHYSICS-EXPAND

### 已完成契约

- 旧 `createBoxBody`/`PhysicsBoxShape2DDesc` 已删除；唯一创建模型是 `createBody()` 后按需调用
  `createShape()`，一个 body 可拥有多个 shape。
- `PhysicsShape2DDesc` 以 backend-neutral 字段支持 Box、Circle、Capsule；shape 可独立销毁、查询
  `shapeState()`，并拥有独立 generation `PhysicsShapeId`。
- sensor enter/exit 合并进有界 `PhysicsContactEvents2DView`，通过 `isSensor` 区分；事件携带 body/shape
  generation ID，不把 Box2D callback view 暴露到公共边界。
- `PhysicsJoint2DDesc` 当前支持 Distance joint，提供 `createJoint()`、`jointState()`、`destroyJoint()` 与
  generation `PhysicsJointId`；销毁任一关联 body 会自动退休 joint。
- body、shape、joint 分别执行 stale/wrong-world/capacity 校验；销毁 body 会自动退休其全部 shape 与 joint。
- TileMap solid bridge 只调用公开的 `createBody()`/`createShape()`，批量失败仍原子回滚；Box2D 头和类型
  保持在 `tina_physics2d` PRIVATE 实现。
- 产品 2D sample 创建 circle sensor 与远离主场景的 spring distance joint，结构化输出 sensor enter/exit
  与 joint ready，不干扰 crate/角色门禁。

### 完成结果

| 阶段 | 完成结果 |
| --- | --- |
| N2.1 | Body/Shape/Joint 拆成独立 generation handle 与固定容量 pool，旧 box-only API 零调用 |
| N2.2 | Box/Circle/Capsule、多 shape/body、shape query/destroy 与 body 级联 retirement 已覆盖 |
| N2.3 | sensor enter/exit 与 Distance joint 生命周期、capacity、stale/reuse 已覆盖 |
| N2.4 | Tile solid bridge、bench、产品 sample 全部迁移到新 API；Box2D 仍为 PRIVATE |
| N2.5 | `tina_physics2d_tests` 29/29 通过；产品 2D 300 帧输出 sensor enter=1、exit=1、joint ready=true |

### 验收

```powershell
cmake --build --preset windows-vnext-physics2d-debug `
  --target tina_physics2d_tests tina_physics2d_bench -- /m:2 /v:m
out\build\windows-msvc-vnext-physics2d\bin\Debug\tina_physics2d_tests.exe --gtest_color=yes
cmake --build --preset windows-vnext-bgfx-product-2d-debug --target tina_sample_2d -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
```

N2 明确不包含 polygon/chain shape、revolute/prismatic joint、Jolt、3D physics，或把
`CharacterController2D` 无依据地改成刚体。

## N3 - 2D-INPUT-ADV

### 已完成契约

- Runtime 是 Action mapping 的唯一 owner；`InputActionMapConfig::bindings` 以
  `InputActionBinding`/`InputBindingId` 一套模型表达 digital 与 Gamepad Axis，不保留 Platform mapper 或
  digital-only 配置双轨。
- Action snapshot 统一公开浮点 value 和 Started/ValueChanged/Completed/Cancelled transition。Axis 支持
  Signed/PositiveHalf/NegativeHalf/Trigger，随后应用 gameplay deadzone 外重映射与 scale；digital 同样
  使用 scale。
- 多 source/多 Gamepad generation 按 `SumClamped` 或 `StrongestMagnitude` 合成；等幅值使用稳定
  binding/source 顺序，设备重连后的新 generation 不继承旧 retained state。
- UI transition consume 与 continuous-control claim 先于 Gameplay mapping；digital 抑制到真实 release，
  axis 抑制到 neutral/deadzone，已有 Gameplay source 使用 Cancelled 清理而非伪造 Completed。
- 只有栈顶 State 可从 `FrameUpdateContext` 借用 `InputActionRebinding`。commit 以 Reject/Swap 处理冲突并
  queue 到下一 mapping frame 原子应用；Capturing/Queued transaction 均可取消，捕获的 Gamepad
  generation 在 disconnect 或 raw reset 后失效时以 DeviceDisconnected 结束。
- World pointer sample 在 mapping 时按 last-presented Camera2D/surface revision 固化，跨0-step frame
  不按后续 Camera/resize 重算。公共 API 不泄漏 GLFW。

### 完成结果

| 阶段 | 完成结果 |
| --- | --- |
| N3.1 | Runtime public Action map 合并为 unified `bindings`，digital/axis 共享 stable binding ID、domain、composition、deadzone/scale |
| N3.2 | ActionMapper 产出 value state/transition，支持 axis value mode、两种 composition 与多 Gamepad source |
| N3.3 | UI consume/claim suppression 覆盖 key/pointer/gamepad button/axis，并保持 release/neutral 与 cancel/reset 语义 |
| N3.4 | 顶层 `FrameUpdateContext` 提供 phase-local rebind facade；next-frame apply、Reject/Swap、Capturing/Queued cancel 与 generation 断连/reset 已定义 |
| N3.5 | world-pointer mapping-time latch 与 zero-step simulation latch 保持原契约；重复 Platform mapper 已删除 |

### 验收命令（本轮结果以最终验证记录为准）

```powershell
cmake --build --preset windows-vnext-bgfx-debug --target tina_tests tina_sample_2d -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_tests.exe --gtest_filter="ActionMapper*:Input*:UI*Input*"
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
```

不包含完整输入设置 UI、云同步 binding 或第三方 input SDK。

## N4 - 3D RENDER-001

### 已完成契约

代码审查选择 **有界 N-light**，不改 StaticMesh/Cooked schema：当前 normal map 已有 derivative TBN 的
真实消费路径，而 key/fill 两套 setter 是更直接的公共契约分叉。

- 删除 `setMesh3DDirectionalLight()` / `setMesh3DFillDirectionalLight()`；唯一入口是
  `setMesh3DLighting(const Mesh3DLightingDesc&)`。
- `Mesh3DLightingDesc` 同步消费 0..4 个 backend-neutral `Mesh3DDirectionalLight` 与非负 ambient；超容量、
  NaN/Inf、负 RGB/ambient、零方向均返回 `InvalidMesh3DLighting`。
- Null 与 bgfx 使用同一 validator 和相同上限；bgfx 把 direction/color 作为两个 4×vec4 uniform array
  提交，inactive slot 的 `w=0`。
- Opaque3D MR shader 固定上限遍历最多4个 directional light；仍是 Lambert + Blinn-Phong 近似 specular
  + ambient，不宣称能量守恒 PBR。
- `tina_sample_3d` 一次提交3个 lights，并输出 `lightingConfigured=true`、
  `directionalLightCount=3`；旧 key/fill JSON 字段已删除。
- Cooked StaticMesh 仍为 P3N3UV2；本切片未伪装成 tangent/PBR schema 升级。

### 完成结果

| 阶段 | 完成结果 |
| --- | --- |
| N4.1 | Render public API 收敛为单一 `Mesh3DLightingDesc`，固定上限4，无旧 setter 双轨 |
| N4.2 | Null/bgfx validator、状态与错误码一致；shader uniform array 在 D3D11/GLSL/SPIR-V 编译通过 |
| N4.3 | Null tests 覆盖3-light、ambient-only、超容量、零方向、负 RGB/ambient |
| N4.4 | 产品 sample 使用3-light 单次提交并把 count 纳入生命周期成功门禁 |

明确保留的边界：这不是完整 PBR/light system；IBL、shadow、light component/culling、vertex tangents、
skin/animation 与通用 pass scheduler 仍不在本切片内。

### 验收

```powershell
cmake --build --preset windows-vnext-bgfx-debug --target tina_tests tina_sample_3d -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_tests.exe --gtest_filter="*Opaque3D*:*Material*:*StaticMesh*"
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
```

## N5 - RENDER-FENCE

### 目标契约

- `FramePin` 完成与 asset retirement 收敛到一种产品完成语义；Null 可有简化实现，但契约字段和状态机一致。
- 真实 backend 优先以 GPU fence poll 或等价、可证明安全的 submission-completion 语义驱动完成。
- 删除无引用的 PresentSync/FrameDeferred/假完成分支，避免同一资源在不同 backend 走不同 lifetime 规则。

### 完成结果

- `FramePin`/`SubmissionTicket` 已统一为 present-return CPU submission completion：成功 present 后 complete，
  suspended skip 直接释放，失败/shutdown abandon。
- 删除 `SubmissionCompletionMode`、`BgfxSubmissionCompletionLedger`、deferred handoff、dynamic_cast 与
  `lastPresentFrameToken()`；`bgfx::frame()` 只推进 backend 帧，不再冒充 fence token。
- `submitFrame()` 的 borrowed view 继续要求同步消费；GPU Texture/Mesh 资源所有权由 bgfx backend 管理。
- `IRenderDevice::retireTexture2D/retireStaticMesh` 成功才消费 `FramePin`；generation 与 binding 立即失效，
  native handle 等 backend-proven completion 后销毁。Null 同步完成，统计统一暴露 pending/completed。
- bgfx 在所有资源引用 view 之后提交 1×1 blit + readback marker，只以 `readTexture()` 返回的 ready frame
  完成 retirement；frame wrap 使用半区间比较，suspend/drain 使用 `BGFX_FRAME_FLUSH` 推进。
- `AssetSystem` retirement helper 把 `AssetLease` 移入 pin：逻辑 lookup 立即移除，Store 保持
  `UnloadPending`，callback exactly-once 转 `Released/Unloaded`。析构会 drain，或识别 backend 已完成的回调。
- 无 blit/readback capability 时，带外部 pin 的异步 retirement 在资源状态变化前原子拒绝且不消费 pin；
  无 pin destroy 立即使逻辑 handle/binding 失效并交给 `bgfx::destroy` 的 backend-owned deferred
  destruction，不进入 marker timeline。有界 marker drain 未完成时，shutdown 才是仍存活外部 pin 的
  hard completion fallback。通用 GPU submission fence 仍不是本项能力，也未被伪装实现。

### 验收

```powershell
cmake --build --preset windows-vnext-bgfx-debug --target tina_tests tina_asset_tests tina_render_bgfx_tests tina_sample_2d_catalog tina_sample_2d tina_sample_3d -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_tests.exe --gtest_filter="*FramePin*:*Retire*:*Fence*"
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_asset_tests.exe --gtest_filter="*Retirement*"
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_bgfx_tests.exe --gtest_filter="*Retirement*"
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d_catalog.exe --frames=30 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
```

## N6 - 2D-TILEMAP-STREAM

### 已完成契约

- `TileMapPayload` 唯一当前格式为 schema v3 stream root；tile layer 只保存排序后的非空 chunk ref，
  object layer metadata 仍在 root；v1/v2 不双读。
- 独立 `TileMapChunk` schema v1 保存 parent/layer/coord/extent/non-empty/cells；root 以
  `Required|Deferred` dependency 引用，root eager load 不再要求完整地图常驻。
- `TileMapStream` 持有 root/tileset/chunk leases 与 resident `TileMapInstance`；唯一帧序为
  `updateDemand -> AssetSystem::pump -> commitReady`，配置显式 resident capacity、request budget、
  load/retain margin。
- demand shift 可取消 Queued/Loading、detach/unload Resident；capacity failure 保持旧 active set；重入
  attach 分配新的 residency generation，dirty cache 同时比较 generation/content revision。
- load window 中的 desired chunk 是强需求，单独超过 resident capacity 时继续 transactional failure；
  retain window 只作缓存提示，空间不足时按最近一次成功 `updateDemand` 时位于 load window 的 recency 自动
  淘汰 optional retained slot。读取 Tile/collision API 不更新 recency。
- `tina_sample_2d` 在 AssetSystem 最终地址上 acquire leases，把 stream emplace 到最终地址后再构造
  `TileMapGridCollision`；启动确认 visual/collision 两块 resident，每 frame 在 controller 查询前推进 stream。

### 完成结果

| 阶段 | 完成结果 |
| --- | --- |
| N6.1 | AssetKind/typed validation 加入 TileMapChunk v1；TileMap root 升级 v3，删除 v2 cells 双轨 |
| N6.2 | Cooker 固定16×16生成非空 chunk asset，Manifest 使用 eager Tileset + deferred chunk dependency，并做跨资产一致性验证 |
| N6.3 | resident `TileMapInstance` attach/detach/non-resident 错误与 residency generation 建立；chunk extraction/render/collision/dirty cache 迁移 |
| N6.4 | `TileMapStream` 覆盖 visible demand、shift cancel/unload、transactional capacity；AssetSystem active-read move/destroy 生命周期加固 |
| N6.5 | 产品 sample 改走 stream，并输出 demand/request/commit/resident/peak 结构化证据；Catalog 资产数由11变为13 |
| N6.6 | retain window 自动 demand-recency LRU；desired 强需求超容仍 transactional，retain overflow 不再阻止新 load-window chunk |

### 验收

```powershell
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_asset_format_tests tina_asset_tests tina_sample_2d_catalog tina_sample_2d tina_tests -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_asset_format_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_asset_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
```

N6 不包含优先级 IO 调度、GPU chunk mesh cache、editor、旧 schema migration、navigation 或自动
gameplay 生成；当前 LRU 只管理 `TileMapStream` optional retained slot，不是通用 Asset cache。

## N7 - 2D-AUDIO-ADV

### N7-A 已完成契约

- `AudioVoiceMin/MaxGain` 固定为 `[0,1]`，pitch 固定为 `[0.25,4]`，pan 固定为 `[-1,1]`；NaN、Inf、
  越界、stale/wrong-owner voice 均原子拒绝。
- `AudioEngine` 公开 `setVoiceGain()`、`setVoicePitch()`、`setVoicePan()`、
  `startVoiceFade(AudioVoiceFadeDesc)`、`cancelVoiceFade()` 与 `voicePlaybackState()`；不暴露 miniaudio 类型。
- pitch 按 `sourceSampleRate / outputSampleRate * pitch` 推进 source cursor，并使用线性插值；不同采样率
  不再静音卡住 cursor。
- stereo pan 使用兼容既有 center 响度的 linear balance：center 保持左右各1，hard-left/right 只衰减
  对侧；mono 输出忽略 pan。
- `AudioVoiceFadeDesc` 以 target gain、正的 `Core::Duration` 与
  `AudioFadeEndAction::KeepPlaying/StopVoice` 表达；start/cancel 在下一 realtime callback block 边界生效，
  cancel 保留 callback 已推进到的 current gain。
- `playOneShotPcm()` 创建 transient voice；显式 Stop、fade-to-stop 或 natural end 经 completion pump 后
  自动 retire。普通 `createVoice()` 仍由调用方显式销毁；shutdown 清除 voice/fade，不遗留 active slot。

### N7-B 已完成契约

- `AudioEngine::Create` 为每个 voice 固定预留
  `streamBufferFrameCapacity * AudioPcmStreamMaxChannels` float；descriptor 只可缩小逻辑容量且至少为2，
  callback/submit 后不扩容或分配。
- AudioEngine owner thread 是唯一 producer；`AudioPcmStreamChunkView` 只在 submit 调用内借用，成功时整块
  复制进 Tina-owned ring，容量不足零发布。Task worker 必须 marshal 到 owner thread；Audio 不新增 Task
  producer API/依赖。
- 非 EOF underrun 只输出静音并计数，保持 playing 且允许后续 submit；EOF 幂等并拒绝后续 submit，ring
  排空后发布唯一 Stopped 并自动 retire。
- cancel/Stop 是 absorbing terminal intent，拒绝后续 submit/Play；分别发布唯一 Cancelled/Stopped。
  completion ring 满或旧 realtime reader 活跃时 terminal debt 保留，发布成功后才允许 voice/slot reuse。
- `mixRealtime()` 只允许一个 non-overlapping realtime consumer；publication epoch 与 reader quiescence 防止
  cancel/EOF/slot reuse ABA。shutdown 先关闭 realtime 进入并等待已进入 block，再同步归零；shutdown 本身
  不承诺补发未 pump terminal。miniaudio 仍只是 consumer，不引入第二套 device/backend 或 producer API。

### N7 验收

```powershell
cmake --build --preset windows-vnext-audio-miniaudio-debug `
  --target tina_audio_tests tina_audio_miniaudio_tests -- /m:2 /v:m
out\build\windows-msvc-vnext-audio-miniaudio\bin\Debug\tina_audio_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-audio-miniaudio\bin\Debug\tina_audio_miniaudio_tests.exe --gtest_color=yes

cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_tests tina_audio_tests tina_audio_miniaudio_tests tina_sample_2d -- /m:1 /v:m
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe `
  --frames=300 --frame-delay-ms=0

powershell -ExecutionPolicy Bypass -File .\tools\windows\RunProduct2dGate.ps1 `
  -SkipConfigure -SkipBuild
```

N7 不包含 OS 真实扬声器质量/延迟/设备切换门禁、高质量 band-limited resampler、空间音频/HRTF、
DSP graph、正式 codec 产品策略或 callback benchmark；这些保持为后续独立切片。

## N8 - 2D-FX

### 已完成契约

- `ParticleSystem2D` / `Trail2D` 是 Scene standalone owner-thread systems，不属于 World component，也不
  依赖 Asset 或 bgfx；两者直接写调用方 phase-local `RenderSceneWriter`。
- `Create()` 通过调用方 PMR resource 建立固定容量 storage，是唯一持久分配点；成功 emit/append、update、
  extract 不增长 storage。
- Particle 的每个固定 seed（包括0）都对应确定 RNG 序列；burst 在提交前验证全部输入、remaining
  capacity 与 stable-key range。validation/capacity/key failure 不推进 RNG、next key 或 live set；成功 key
  单调分配，过期或 clear 后不复用。
- Particle update 先 preflight 全部 next age 与仍存活粒子的积分后位置，任何 overflow 保持整个 system
  不变；成功后再推进、删除过期粒子。extract 按 normalized age 线性插值 size/color。
- Trail 首点建立 anchor，后续有效非退化点建立 segment 并移动 anchor；`breakTrail()` 使下一点开始新链。
  每段从创建时拥有独立 age/lifetime，width 按 normalized age 线性插值。geometry/capacity/key failure
  保持 anchor、segments 与 next key；update 先 preflight 全部 age，再推进并删除过期段；segment key
  单调且不复用。
- product-2d 使用固定容量12/seed `1414090305` 的 Particle（发射10）和容量8的 Trail（3段、1次 break）；
  schema 9 输出初始 FX 指纹与 active/expired/extracted 计数，300帧 gate 再结合 non-empty pixel evidence。

### 完成结果

| 阶段 | 完成结果 |
| --- | --- |
| N8.1 | Scene 公共头与实现加入 `ParticleSystem2D` / `Trail2D`；两个 system 不引入 Asset/bgfx dependency |
| N8.2 | `tina_scene_tests` 覆盖 PMR、seed、validation/capacity/key/update 事务性、lifetime/width 与 RenderScene writer failure |
| N8.3 | `tina_sample_2d` 在 `updateFrame()` 使用 fixed delta 推进 Particle/Trail 并接入 extraction，evidence fingerprint 升级 schema 9 |
| N8.4 | `RunProduct2dGate.ps1` 同轮构建/直接运行 `tina_scene_tests`，并校验通用与300帧 FX 结构化字段 |

### 验收

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_scene_tests tina_sample_2d -- /m:1 /v:m
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe `
  --frames=300 --frame-delay-ms=0
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunProduct2dGate.ps1 `
  -SkipConfigure -SkipBuild
```

N8 不包含 Cooked FX asset schema、effect graph/editor、GPU particle simulation、mesh-ribbon trail、collision
或网络 rollback；当前 trail 按独立 Sprite2D segment extraction，不宣称通用 ribbon renderer。

## N9 - RUNTIME-SHUTDOWN-DEADLINE

### 已完成契约

- `ITaskSystem` 新增
  `[[nodiscard]] Core::Status shutdownAndJoinFor(Core::Duration deadline) noexcept`。deadline 必须 finite 且
  大于0；非法值返回 `TaskErrorCode::InvalidArgument`，不得触发 stop 或改变队列/Worker 状态。
- 有效调用先进入 stopping 并协作停止，等待所有 Worker 退出。deadline 内完成后 join 并清理线程/队列；
  已完成后重复调用仍成功。
- deadline 到期返回 `TaskErrorCode::WaitTimeout`；TaskSystem 保持 stopping，线程、队列和 owner storage 均
  不 join、不 clear、不 reset。阻塞任务放行后，同一对象可再次调用并成功收口。
- `EngineHost` 将已校验为 finite positive 的 `EngineConfig::shutdownDeadline` 原样传给 TaskSystem。该值只
  预算 `shutdownAndJoinFor()` 的 Worker-exit/join 阶段，不覆盖此前的 AudioEngine/RenderDevice shutdown，
  也不是整个 Host shutdown 的总耗时上限。TaskSystem 超时后，Host 先向仍存活的 Diagnostics 写入
  `runtime.lifecycle` 错误，再调用 `std::terminate()`；不得继续 reset TaskSystem 或析构 Platform、Clock、
  Diagnostics 等剩余 owner。
- 关闭路径不 detach、不强杀 Worker；deadline 是进程级 hard failure boundary，不是忽略活跃线程后继续
  析构的许可。

### 完成结果

| 阶段 | 完成结果 |
| --- | --- |
| N9.1 | Task 公共 API 与 Disabled/Bounded 实现覆盖 invalid、idle、queued drain、timeout 保留状态、release 后 retry、重复成功 |
| N9.2 | `EngineHost` 仅为 TaskSystem worker-exit/join 使用配置 deadline；成功路径继续逆序析构，timeout death path 证明先写 Diagnostics 且不继续析构剩余 owner |
| N9.3 | shutdown deadline 聚焦门禁与完整 `tina_tests` 已直接执行通过；公开头隔离由同一 target 编译覆盖，文档一致性扫描通过 |

### 验收

```powershell
cmake --build --preset windows-vnext-debug --target tina_tests -- /m:1 /v:m
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes `
  --gtest_filter="DisabledTaskSystemTest.ShutdownDeadline*:BoundedTaskSystemTest.ShutdownDeadline*:EngineHostCreationTest.PassesConfiguredShutdownDeadlineToTaskSystem:EngineHostShutdownDeadlineDeathTest.*"
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\docs\CheckDocs.ps1
```

N9 不包含通用 State TaskGroup soft deadline、Worker 抢占/强制取消、detach、跨进程 watchdog 或 crash
dump 协议；这些能力需要独立契约和门禁。

## N10 - ASSET-HANDLE-SCENE-2D-A1

### 已完成契约

- `AssetHandle` 拆入 header-only `Tina::AssetTypes`；Scene PUBLIC 只依赖该窄 target，不传递完整
  AssetSystem、Task、Render upload 或可选 Physics2D。
- `SpriteRenderer2D` 保存 copyable weak `AssetHandle`，不再保存 render key，也不持有 `AssetLease`、
  Cooked payload、`GpuTextureId` 或 backend 类型。World 只校验组件数值结构，允许异步期间的空 handle。
- `ExtractRenderSceneParams::spriteBindingResolver` 是借用的 function pointer + user data，仅在本次调用内
  有效，调用零分配且 `noexcept`。visible sprite 的空/stale/cross-store/wrong-kind/unbound handle、缺 resolver
  或 resolver 返回0统一产生 `SceneErrorCode::UnresolvedSprite`；hidden sprite 不调用 resolver。
- 产品 resolver 每次按当前 AssetSystem Store 验证 owner/generation、Sprite kind 与显式 binding mapping；
  Animator frame 复制 Sprite handle，但空 handle clip 仍返回 `InvalidAnimation`。

### 完成结果

| 阶段 | 完成结果 |
| --- | --- |
| N10.1 | 轻量 Handle 公共头与 AssetTypes target；AssetStore 保持原 generation/owner/stale 语义 |
| N10.2 | SpriteRenderer2D/World/extract/Animator 契约迁移，无 `spriteKey` 双轨字段 |
| N10.3 | Scene 测试覆盖 default/stale/cross-store/wrong-kind/unbound、返回0、hidden skip、成功 UV/pivot/transform 与 writer failure；2D product World 路径使用 Catalog Sprite handle |

### 验收

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_scene_tests tina_sample_2d -- /m:1 /v:m
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe `
  --frames=300 --frame-delay-ms=0
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\docs\CheckDocs.ps1
```

N10 不宣称完成 `ASSET-HANDLE-SCENE` 总项：产品 resolver 仍把 Handle 映射到 game-owned binding key；
TileMap emit、Particle/Trail、3D Mesh/Material 仍保留各自 key 路径。engine-owned upload/binding registry、
统一 retirement/FrameResourceRef 与 3D component Handle 化必须在后续切片完成。

## 每项收口清单

1. 列出代码变更和已删除的旧路径。
2. 列出实际执行的测试与 sample 命令及结果。
3. 明确破坏性调用方式变更，不声明未实现的兼容。
4. 列出已回写的文档。
5. 写清本项未做边界，再将下一项标记为进行中。
