# Scene 与 Entity World

本文描述当前 `Tina::Scene`。旧 `SceneManager`、Legacy Scene stack 与旧 EnTT World 已随产品图删除；
当前模块不链接 EnTT，也不承担 `IGameState` 生命周期。

## 模块边界

`tina_scene` 负责：

- 固定容量、PMR-backed `World`；
- generation/owner-aware `EntityId`；
- Local/World Transform 层级与显式 publication barrier；
- Camera2D、SpriteRenderer2D、SpriteAnimationBinding2D、PointLight2D、ShadowOccluder2D、PerspectiveCamera3D、MeshRenderer3D、DirectionalLight3D 组件；
- standalone allocation-free `CameraFollow2D` controller；
- standalone fixed-capacity `ParticleSystem2D` 与 `Trail2D`；
- World 到 phase-local `RenderSceneWriter` 的 2D/3D extraction；
- 2D World 组件与 game-owned gameplay blob 的 current-only schema-v2 快照；
- Cooked Prefab node 到 World entity hierarchy 的事务式实例化。

它不负责：

- State push/pop、菜单或暂停流程；
- Platform/Input、TileMap gameplay、Physics stepping 或 Audio；
- AssetSystem IO/Lease 生命周期或 binding registry ownership；
- bgfx handle、shader、native window 或 backend command。

依赖方向保持为 Scene → Core/Render/AssetFormat/AssetTypes；`AssetTypes` 只公开轻量 `AssetHandle` 与
borrowed resolver，并只传递 Tina-owned Core/Render，不会把完整 AssetSystem、Task 或可选 Physics2D
传递给 Scene。第三方 ECS 或 renderer 类型不得进入公开头。

## World 所有权

`World::Create(WorldConfig, memory_resource)` 在创建时固定 entity/component storage 容量。`World` 不可
复制，只能 move-construct；所有 mutation 由 owner thread 串行执行。

`EntityId` 是 Runtime identity，不是持久化 ID：

- `createEntity()` 发布新 generation；
- destroy 后旧 ID 必须失败；
- 跨 World 使用 ID 必须失败；
- Catalog/Prefab 持久引用使用 `AssetId` 或内容自己的稳定 ID，不能序列化 `EntityId`。

默认 `destroyEntity()` 只销毁目标实体，直接子节点提升到 root 并保持最后发布的 world transform；
需要整体删除时显式调用 `destroySubtree()`。

## Transform 契约

每个实体拥有 `LocalTransform` 与已发布的 `WorldTransform`。父子修改支持：

- `ReparentMode::KeepWorld`：默认，重挂后保持 world pose；
- `ReparentMode::KeepLocal`：保持 local pose，由新父节点决定 world pose。

循环、stale parent、非法 finite/scale 输入会返回结构化错误，不做静默修复。`setParent()` 与
`setLocalTransform()` 只修改 owner state；`updateWorldTransforms()` 是显式 publication barrier，按稳定
顺序非递归传播。Extraction 会先调用该 barrier，确保读取同一批已发布 transform。

## 当前组件

| 组件 | 用途 | 关键约束 |
| --- | --- | --- |
| `Camera2D` | FixedWorldHeight/PixelPerfect 投影、viewport、pixel snap | 每帧最多一个 active 2D camera；surface 0x0 时跳过 |
| `SpriteRenderer2D` | weak Sprite `AssetHandle`、optional weak normal Texture2D `AssetHandle`、尺寸/pivot/UV override、颜色与排序 | World 只校验结构；visible extract 必须解析 base，非空 normal 独立解析为当前 packet Texture2D ref；UV finite 且严格递增 |
| `SpriteAnimationBinding2D` | weak SpriteAnimationClip `AssetHandle`、playback speed、autoPlay | World 只保存 binding；clip 推进与帧解析由产品 State/Animator 负责，speed 必须正 finite |
| `PointLight2D` | linear RGB color、非负 intensity、正影响半径、0..影响半径内的 source radius、active 标志 | Entity world position 是灯光中心；source radius=0 为硬阴影、正值启用连续 penumbra；有 resolved Camera2D 时每帧最多提交8个 camera-affecting light，无相机/0x0 surface 时对全部 active light 保留同一上限 |
| `ShadowOccluder2D` | 一条 local-space 线段与 active 标志 | 应用已发布 XY scale/rotation/position；每帧最多32个 active segment，按稳定 Entity identity 发布且不做 camera culling |
| `PerspectiveCamera3D` | perspective 参数与 active 标志 | 每帧最多一个 active 3D camera |
| `MeshRenderer3D` | weak mesh/material `AssetHandle`、bounds、base color、可见性 | World 只校验结构；visible extract 通过两个 kind-specific resolver 解析非0 key |
| `DirectionalLight3D` | linear RGB color、非负 intensity、active 标志 | Entity world local `+Z` 指向光源；每帧最多4个 active light，按稳定 Entity identity 发布 |
| `PointLight3D` | linear RGB color、非负 intensity、正 influence radius、optional `PointLightShadow3D`、active 标志 | Entity world position 是灯光中心；每帧最多8个 camera-affecting active light，按稳定 Entity identity 发布；有有效 PerspectiveCamera3D 时 influence sphere 在容量检查前做 frustum culling；最多一个 camera-affecting shadow config |
| `SpotLight3D` | linear RGB color、非负 intensity、正 influence radius、合法 inner/outer cone half-angle、optional `SpotLightShadow3D`、active 标志 | Entity world position 是灯光中心，world local `-Z` 是出光方向；每帧最多8个 camera-affecting active light；与 point light 一样先做 influence sphere culling，再按稳定 Entity identity 发布；最多一个 camera-affecting shadow config |

组件 storage 与 entity slot 共用固定容量。`set*` 替换当前值，`clear*` 移除组件；访问 stale 或 cross-world
ID 失败。Camera2D 与 PerspectiveCamera3D 是独立轨道，可以在同一帧同时存在；各自出现多个 active
camera 时 extraction 失败。

## CameraFollow2D

`CameraFollow2D` 属于 `Tina::Scene`，但不是 World component，也不修改 `Camera2D` projection 配置。产品
State 在 owner thread 持有它，并在 fixed update 提交 target、delta、viewport world size 与可选 world bounds：

- dead zone 决定 target 在哪个轴越界后才推动相机；
- 可选最大速度限制每次 simulation center 推进量；
- world bounds 按 viewport half extent clamp，viewport 大于 world 时对应轴固定在 world 中点；
- 成功 step 同时发布 previous/current center，非法 finite、负 delta、退化 bounds 或非法 viewport 事务失败；
- `interpolatedCenter(alpha)` 只为 presentation 读取 previous/current，非法 alpha 不改变 simulation state。

该 controller 创建后不分配，也不持有 World、Asset、Render 或 backend owner。`tina_sample_2d` 的 streaming
读取 current simulation center，render extraction 读取 interpolated center；旧的散落 previous/current float
和手写 map clamp 不再是产品状态源。

## Standalone 2D-FX systems

`ParticleSystem2D` 和 `Trail2D` 属于 `Tina::Scene`，但不是 World component，也不持有 Entity。二者由
产品 State 显式拥有，owner thread 串行调用；它们只依赖轻量 AssetTypes 与 Core/Render 的 Tina-owned
类型，不依赖完整 AssetSystem 或 bgfx。二者保存 copyable weak Sprite `AssetHandle`，不持有
`AssetLease`、Cooked payload、GPU texture owner 或 resolver。`Create()` 使用调用方
`std::pmr::memory_resource` 完成唯一持久分配并固定容量，成功的 emit/append、update、extract 均不增长
storage。

`ParticleSystem2D` 的每个 seed（包括0）都选择固定 RNG 序列。burst 先完整校验非空 Sprite handle、有限值、
正 lifetime/size、剩余容量和稳定 key 空间；validation、capacity 或 key exhaustion 失败都保持 RNG、
next key 与 live particles 不变。emit 把 burst 的 weak handle 值复制到每个粒子；稳定 particle key 从配置
基值单调分配，粒子过期或 `clear()` 后不复用。
`update()` 先 preflight 全部 next age 与 survivor position，再统一推进并 stable-compact live set；任何
溢出使更新零发布。

`Trail2D` 以第一个有效点建立 anchor，之后每个有效非退化点建立一段并成为新 anchor；`breakTrail()`
只断开 anchor，使下一点开始新链。每段保存创建时的独立 age/lifetime，update 先 preflight 全部 age，
再推进并移除过期段；extract 按每段 normalized age 在线性 start/end width 间取值。非法 geometry、固定
capacity 或稳定 key exhaustion 失败均不修改 anchor、segment set 或 next key；过期 key 不复用。
`Trail2D::Create()` 对 config 的空 Sprite handle 做结构校验并拒绝。

两个 system 的 `extract()` 都显式借用共享 `AssetFrameResourceResolver`、当前 packet
`FrameResourceSink` 与调用方 phase-local `RenderSceneWriter`，把粒子或 segment 转为 backend-neutral
Sprite2D item。resolver/userData/sink 只在当前调用内有效，system 不保留。缺 resolver，或
stale/cross-store/wrong-kind/unbound handle 使 resolver 返回空 ref，
统一 fail closed 为 `UnresolvedSprite`。空 FX 不调用 resolver；Trail 每次非空 extract 只解析一次并复用
到所有 segment，Particle 按每个 live item 即时解析。writer failure 原样传播，simulation owner state 不
因此变化；真实 texture binding 和 bgfx submission 仍由 RenderDevice/backend 负责。

## Render extraction

`extractRenderSceneFromWorld(World&, RenderSceneWriter&, FrameResourceSink&, params)` 在调用方提供的
writer 与当前 packet sink 中事务式写入：

```text
updateWorldTransforms
  -> resolve active Camera2D and/or PerspectiveCamera3D
  -> borrowed resolvers ask Asset registry to intern current Sprite base/optional-normal texture bindings
  -> emit visible SpriteRenderer2D items
  -> validate/cull active PointLight2D, collect all active ShadowOccluder2D, then deep-copy lighting
  -> collect active DirectionalLight3D/PointLight3D/SpotLight3D, cull point/spot influence spheres, and deep-copy the frame lighting snapshot
  -> borrowed kind-specific resolvers ask Mesh3D registry to intern current mesh/material bindings
  -> emit visible MeshRenderer3D items
  -> caller commits RenderSceneBuilder
```

2D sprite 使用 world position/scale、Z rotation、pivot/size/UV override，并由 RenderScene 执行排序、
culling、batch 规划与 pixel snap。`ExtractRenderSceneParams::spriteBindingResolver` 是只在本次调用有效的
borrowed function-pointer seam；它必须按当前 AssetStore 验证 owner/generation、Sprite kind 与 binding，
并返回有效 packet-local base texture ref。组件 normal handle 非空时，`normalTextureBindingResolver` 独立验证
weak Texture2D handle 并返回 packet-local normal ref；任一解析失败都在 `addSprite2D()` 前返回
`UnresolvedSprite`，不提交半成品。hidden sprite 不调用两个 resolver，normal binding 不改变排序。
`MeshRenderer3D` 同样只保存 weak mesh/material
`AssetHandle` 与 world pose/scale、local bounds、material color 等语义字段。extraction 分别借用
`mesh3DBindingResolver`/`material3DBindingResolver`，按预期 AssetKind intern 非空 packet-local ref；任一
handle/resolver/binding 失效返回 `UnresolvedMesh`，mesh 失败不会继续调用 material resolver，hidden mesh
不解析。`DirectionalLight3D` 使用已发布 world rotation 把 local `+Z` 转为朝向光源的 world direction，
将 color×intensity 与 `ExtractRenderSceneParams::ambientLightScale` 写入固定4槽的 self-contained
RenderScene snapshot。`PointLight3D` 使用已发布 world position、influence radius 与 color×intensity；
`SpotLight3D` 额外把 world local `-Z` 转成出光方向，并把 inner/outer half-angle 转成 cosine。optional
`PointLightShadow3D`/`SpotLightShadow3D` 都要求 `0 < nearPlaneMeters < influenceRadiusMeters`、有限且有界的 depth/normal bias。
二者在有效
PerspectiveCamera3D 与非0 surface 时按 influence sphere-frustum culling 在各自固定8槽容量检查前裁剪，
稳定 Entity identity 排序后深拷贝为同一 snapshot；shadow 在排序后分别映射 `pointLightIndex`/
`spotLightIndex`，任一类型超过一个 camera-affecting config 都显式失败。没有灯组件时保留低层 device fallback；存在组件但全部 inactive 时发布 ambient-only
snapshot，避免重新启用 fallback 方向光。超过对应上限返回 `TooManyActiveDirectionalLights` 或
`TooManyActivePointLights3D` 或 `TooManyActiveSpotLights3D`，不静默裁剪。
`PointLight2D` 使用已发布 world position、显式 world-space influence/source radii 与 color×intensity。
source radius 必须 finite、非负且不大于 influence radius；它不改变 N3 culling 外圆。Extraction 先验证每个
active component、`WorldTransform` 和 color×intensity，因此非法灯即使位于视口外也返回
`InvalidComponent`，不会被 culling 隐藏。有非0 surface 上 resolved Camera2D 时，灯心转换到旋转相机
局部空间，以 circle-vs-rectangle 精确相交测试裁剪；相机中心使用与 committed Camera2D 一致的 pixel snap，
边界相切仍可见。只有 camera-affecting light 按稳定 Entity identity 排序并占用固定8槽，第9盏仍返回
`TooManyActivePointLights2D`，不做 top-K 或静默丢弃。没有 active Camera2D 或 surface 0x0 时不裁剪，
全部 active light 继续受同一8槽上限约束。没有灯组件时保留 unlit path，inactive-only 发布 ambient-only。
`ShadowOccluder2D` 的 local 端点应用已发布 XY scale、rotation、position 后，按稳定 Entity identity 写入
固定32槽 world-space segment；超容量返回 `TooManyActiveShadowOccluders2D`，非法或投影退化返回
`InvalidComponent`。Occluder 不做 camera culling，因为视口外 segment 仍可能遮挡边界光线。source radius=0
走既有 hard-ray intersection；正值把遮挡段按归一化 receiver→light 深度投影到 finite source 区间，连续计算
单段覆盖率，并用 multiplicative transmittance 合成多段。该固定 `8×32` 成本近似不宣称精确 area-light
interval union；ambient 与 Sprite 透明排序不变，没有 PointLight2D 时不单独发布 occluder snapshot。
当前 product-2d schema 27 继承 schema 19 的 normal-map 证据，并固定创建3盏 active light，其中1盏永久离屏；提交结果保持
`authoredPointLight2DCount=3`、`pointLight2DCount=2`、`culledPointLight2DCount=1`，并继承 schema 16 的
双 ShadowOccluder2D 逐帧 snapshot 证据；默认 committed soft light count=2，`--force-hard-shadows` 为0。
Scene 不保存 resolver、FrameResourceSink/ref、AssetLease、Cooked payload 或 GPU handle。

A2 提供的产品 resolver 最初把 Sprite Handle 转交 `Sprite2DBindingRegistry::resolveSprite()`，由 Asset 层沿
Sprite 的唯一 required Texture2D cooked dependency 验证 live binding。A3 让 Particle/Trail 保存 Handle；
A4 当时将通用 borrowed resolver 下沉到 AssetTypes，Scene 名称保留为 alias，并让 TileMap
保存 weak Tileset Handle、调用 `resolveTileset()`，不再跨帧保存 registry key。N16.3 后产品 State 只拥有
registry；每个 Entry 唯一拥有 resident Lease/GPU/binding，State 不再维护第二份 GPU cleanup 账簿。
registry 借用 `TileMapResources` 中最终地址稳定的 AssetSystem 和 `DeviceCapture` 中的 RenderDevice；外部
owner 必须覆盖 State/registry 与已提交 retirement pin 的生命周期。key 由 RenderDevice 实例 allocator 分配，
同一 device 的多个 registry 共享唯一/单调 namespace；allocator-managed registry 管理期间不得混用 direct
caller-chosen binding key。N16.2 将 2D seam 升级为 `AssetFrameResourceResolver`：registry 把验证后的
binding intern 到当前 packet，并以 entry borrow pin 阻止活跃帧 retirement；Scene 不参与 key 分配或
retirement，也不保存 frame ref。

A6 的产品 resolver 把 mesh/material Handle 转交 `Mesh3DBindingRegistry`；N16.4 已替代当时的分裂 owner
契约。registry 固定容量、owner-thread，借用 AssetSystem/device/PMR；StaticMesh 与 Material 使用独立 device
key namespace，Material 通过一次原子 bundle 提交 baseColor/MR/normal texture 与 factors。Mesh entry
唯一拥有 Lease/GPU/binding，Material entry 拥有 Lease/binding，共享 Texture entry 按 AssetId 去重并拥有
Lease/GPU。extraction 只获得 packet-local ref，active frame pin 阻止 retirement。

writer、committed view 与其中 span 只在对应 Runtime phase/submit 调用内有效，不能保存到下一帧。

## World2D 快照

`AssetFormat::writeWorld2DSnapshotBytes()` / `parseWorld2DSnapshot()` 定义唯一现行 schema v2；
`captureWorld2DSnapshotBytes()` / `instantiateWorld2DSnapshot()` 在该 wire 与 `World` 间转换。持久化边界只包含：

- 调用方提供的非零稳定 entity ID 与 parent stable ID；
- LocalTransform；
- SpriteRenderer2D、Camera2D、PointLight2D、ShadowOccluder2D、SpriteAnimation2D binding；
- Sprite/normal Texture 的稳定 `AssetId`；
- Runtime 不解释的 gameplay schema/version/bytes。

Runtime `EntityId` 的 owner/index/generation、weak `AssetHandle`、AssetLease、Render key 和 GPU handle 均不进入
字节流。capture 先按 hierarchy depth、再按 stable ID 排序，保证 parent 先于 child 且字节不受 World slot/
generation 影响；发现3D组件时直接失败，避免生成丢字段的“成功”存档。Sprite handle 必须由借用 callback
解析为 `AssetId`；restore 则在修改 World 前把全部 AssetId 解析回 weak handle。

parser 使用临时 entity storage，完整 header、保留位、component canonical bytes、层级、值域和 gameplay
身份通过后才替换调用方 storage。restore 在 mutation 前完成 schema、容量、资源与组件预检；后续
create/reparent/component/publication 任一步失败都会逆序销毁本次 entity，不影响原有 World。旧 schema 不走
兼容或 migration 分支，统一 `UnsupportedSchema`；需要格式演进时定义新的明确 schema 与离线迁移工具，不在
运行时堆叠开发期兼容代码。完整 wire、容量与借用期见 [World2D 序列化](world2d-serialization.md)。

## Prefab 实例化

`instantiatePrefab()` 读取 `AssetFormat::PrefabPayloadView`，按稳定 node 顺序执行：

```text
createEntity(local transform)
  -> setParent(KeepLocal)
  -> optional setMeshRenderer3D
```

`PrefabMeshBinding` 可以通过回调把每个 mesh/material `AssetId` 解析为 weak `AssetHandle`，并补充 bounds/
baseColor。它不分配 Render key；任一步失败都会销毁本次已创建的全部 entity，不留下半份 hierarchy。

multi-mesh Cooker 已能让不同 Prefab node 引用 distinct StaticMesh/Material AssetId；产品 sample 把全部
Cooked owner 发布到 Resources-owned `AssetStore`，Prefab 保存 handle，extraction 再解析到 distinct
packet-local refs。双 mesh upload/bind/extract/draw、共享 Texture owner 与 frame-resource resolver evidence
已进入产品门禁。

## 与 Runtime/Game 的关系

当前 Runtime 不把 `World` 放入 Phase Context。产品 State 自己持有 World，并在
`IGameState::extractRenderScene()` 中调用 Scene extraction：

```text
Game State fixed/frame update
  -> mutate gameplay + World
  -> extractRenderSceneFromWorld
  -> Runtime commits RenderScene
  -> Render backend consumes the submitted view
```

TileMap instance、CharacterController2D、PhysicsWorld2D、AssetSystem、binding registry 与 AssetLease 由
产品层持有；它们必须覆盖 extraction 的借用期。Scene core 只依赖 AssetTypes，不反向取得这些 owner。

## 验证

- `tina_scene_tests`：entity generation、owner、destroy/reparent、Transform propagation、2D/3D component、
  extraction、PointLight2D/ShadowOccluder2D/DirectionalLight3D set/query/clear、world
  position/direction/segment/color/intensity/ambient、stable identity 排序、inactive/超容量、
  World2D capture/restore 确定性 round-trip、AssetId resolver 与失败 rollback，Prefab rollback/
  AssetId→Handle resolver、3D kind-specific resolver fail-closed，以及
  Particle/Trail 的 PMR、确定性、事务失败、lifetime、weak Handle 保留、resolver fail-closed/解析次数与
  writer capacity；
- `tina_asset_tests`：Sprite/Mesh3D binding registry 的容量/owner thread、Sprite register/retirement 与 Mesh
  register/unbind transaction、key
  non-reuse、Texture/Sprite/Tileset/StaticMesh/Material Handle 与 cooked dependency fail-closed，以及 TileMap
  解析次数/失败清空；
- `tina_render_scene_tests`：Camera resolve、culling、排序、batch、world picking、lighting snapshot 深拷贝与事务失败；
- `tina_sample_2d` / `tina_sample_3d`：产品 Asset → Scene → Render 路径；
- header-isolation：公开头不得泄漏 EnTT、GLM、bgfx 或 cgltf。

具体命令见 [测试说明](testing.md)，2D/3D 产品边界见 [2D](game-2d.md)与 [3D](game-3d.md)。
