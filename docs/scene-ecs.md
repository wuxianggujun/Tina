# Scene 与 Entity World

本文描述当前 `Tina::Scene`。旧 `SceneManager`、Legacy Scene stack 与旧 EnTT World 已随产品图删除；
当前模块不链接 EnTT，也不承担 `IGameState` 生命周期。

## 模块边界

`tina_scene` 负责：

- 固定容量、PMR-backed `World`；
- generation/owner-aware `EntityId`；
- Local/World Transform 层级与显式 publication barrier；
- Camera2D、SpriteRenderer2D、PerspectiveCamera3D、MeshRenderer3D 组件；
- standalone fixed-capacity `ParticleSystem2D` 与 `Trail2D`；
- World 到 phase-local `RenderSceneWriter` 的 2D/3D extraction；
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
| `SpriteRenderer2D` | weak Sprite `AssetHandle`、尺寸/pivot/UV override、颜色与排序 | World 只校验结构；visible extract 必须解析为当前 packet texture ref；UV finite 且严格递增 |
| `PerspectiveCamera3D` | perspective 参数与 active 标志 | 每帧最多一个 active 3D camera |
| `MeshRenderer3D` | weak mesh/material `AssetHandle`、bounds、base color、可见性 | World 只校验结构；visible extract 通过两个 kind-specific resolver 解析非0 key |

组件 storage 与 entity slot 共用固定容量。`set*` 替换当前值，`clear*` 移除组件；访问 stale 或 cross-world
ID 失败。Camera2D 与 PerspectiveCamera3D 是独立轨道，可以在同一帧同时存在；各自出现多个 active
camera 时 extraction 失败。

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

两个 system 的 `extract()` 都显式借用共享 `Sprite2DBindingResolver`、当前 packet
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
  -> borrowed resolver asks Asset registry to intern current Sprite texture binding
  -> emit visible SpriteRenderer2D items
  -> borrowed kind-specific resolvers ask Mesh3D registry to intern current mesh/material bindings
  -> emit visible MeshRenderer3D items
  -> caller commits RenderSceneBuilder
```

2D sprite 使用 world position/scale、Z rotation、pivot/size/UV override，并由 RenderScene 执行排序、
culling、batch 规划与 pixel snap。`ExtractRenderSceneParams::spriteBindingResolver` 是只在本次调用有效的
borrowed function-pointer seam；它必须按当前 AssetStore 验证 owner/generation、Sprite kind 与 binding，
并返回有效 packet-local texture ref。缺 resolver、空/stale/cross-store/wrong-kind/unbound handle 或空 ref统一
产生 `UnresolvedSprite`；hidden sprite 不调用 resolver。`MeshRenderer3D` 同样只保存 weak mesh/material
`AssetHandle` 与 world pose/scale、local bounds、material color 等语义字段。extraction 分别借用
`mesh3DBindingResolver`/`material3DBindingResolver`，按预期 AssetKind intern 非空 packet-local ref；任一
handle/resolver/binding 失效返回 `UnresolvedMesh`，mesh 失败不会继续调用 material resolver，hidden mesh
不解析。Scene 不保存 resolver、FrameResourceSink/ref、AssetLease、Cooked payload 或 GPU handle。

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
  extraction、Prefab rollback/AssetId→Handle resolver、3D kind-specific resolver fail-closed，以及
  Particle/Trail 的 PMR、确定性、事务失败、lifetime、weak Handle 保留、resolver fail-closed/解析次数与
  writer capacity；
- `tina_asset_tests`：Sprite/Mesh3D binding registry 的容量/owner thread、Sprite register/retirement 与 Mesh
  register/unbind transaction、key
  non-reuse、Texture/Sprite/Tileset/StaticMesh/Material Handle 与 cooked dependency fail-closed，以及 TileMap
  解析次数/失败清空；
- `tina_render_scene_tests`：Camera resolve、culling、排序、batch、world picking；
- `tina_sample_2d` / `tina_sample_3d`：产品 Asset → Scene → Render 路径；
- header-isolation：公开头不得泄漏 EnTT、GLM、bgfx 或 cgltf。

具体命令见 [测试说明](testing.md)，2D/3D 产品边界见 [2D](game-2d.md)与 [3D](game-3d.md)。
