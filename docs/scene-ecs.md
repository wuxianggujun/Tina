# Scene 与 Entity World

本文描述当前 `Tina::Scene`。旧 `SceneManager`、Legacy Scene stack 与旧 EnTT World 已随产品图删除；
当前模块不链接 EnTT，也不承担 `IGameState` 生命周期。

## 模块边界

`tina_scene` 负责：

- 固定容量、PMR-backed `World`；
- generation/owner-aware `EntityId`；
- Local/World Transform 层级与显式 publication barrier；
- Camera2D、SpriteRenderer2D、PerspectiveCamera3D、MeshRenderer3D 组件；
- World 到 phase-local `RenderSceneWriter` 的 2D/3D extraction；
- Cooked Prefab node 到 World entity hierarchy 的事务式实例化。

它不负责：

- State push/pop、菜单或暂停流程；
- Platform/Input、TileMap gameplay、Physics stepping 或 Audio；
- AssetSystem IO/Lease 生命周期；
- bgfx handle、shader、native window 或 backend command。

依赖方向保持为 Scene → Core/Render/AssetFormat。第三方 ECS 或 renderer 类型不得进入公开头。

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
| `SpriteRenderer2D` | sprite key、尺寸/pivot/UV override、颜色与排序 | key 非0；UV finite 且严格递增 |
| `PerspectiveCamera3D` | perspective 参数与 active 标志 | 每帧最多一个 active 3D camera |
| `MeshRenderer3D` | mesh/material key、bounds、base color、可见性 | key 由产品 AssetId resolver 或 fixture 提供 |

组件 storage 与 entity slot 共用固定容量。`set*` 替换当前值，`clear*` 移除组件；访问 stale 或 cross-world
ID 失败。Camera2D 与 PerspectiveCamera3D 是独立轨道，可以在同一帧同时存在；各自出现多个 active
camera 时 extraction 失败。

## Render extraction

`extractRenderSceneFromWorld(World&, RenderSceneWriter&, params)` 在调用方提供的 writer 中事务式写入：

```text
updateWorldTransforms
  -> resolve active Camera2D and/or PerspectiveCamera3D
  -> emit visible SpriteRenderer2D items
  -> emit visible MeshRenderer3D items
  -> caller commits RenderSceneBuilder
```

2D sprite 使用 world position/scale、Z rotation、pivot/size/UV override，并由 RenderScene 执行排序、
culling、batch 规划与 pixel snap。3D mesh 使用 world pose/scale、local bounds 和 material color。
Scene 只写 backend-neutral key 和 POD；真实 Texture2D/StaticMesh 由产品层上传到 RenderDevice 后绑定。

writer、committed view 与其中 span 只在对应 Runtime phase/submit 调用内有效，不能保存到下一帧。

## Prefab 实例化

`instantiatePrefab()` 读取 `AssetFormat::PrefabPayloadView`，按稳定 node 顺序执行：

```text
createEntity(local transform)
  -> setParent(KeepLocal)
  -> optional setMeshRenderer3D
```

`PrefabMeshBinding` 可以通过回调把每个 mesh/material `AssetId` 解析为 backend-neutral key，并补充 bounds/
baseColor。任一步失败都会销毁本次已创建的全部 entity，不留下半份 hierarchy。

multi-mesh Cooker 已能让不同 Prefab node 引用 distinct StaticMesh/Material AssetId；当前产品 sample 仍只
绑定一个 product mesh。两个 mesh 的 upload/bind/extract/draw 闭环由 `3D-001` 跟踪。

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

TileMap instance、CharacterController2D、PhysicsWorld2D 与 AssetLease 由产品层持有；Scene core 不反向依赖
这些系统。

## 验证

- `tina_scene_tests`：entity generation、owner、destroy/reparent、Transform propagation、2D/3D component、
  extraction 与 Prefab rollback/resolver；
- `tina_render_scene_tests`：Camera resolve、culling、排序、batch、world picking；
- `tina_sample_2d` / `tina_sample_3d`：产品 Asset → Scene → Render 路径；
- header-isolation：公开头不得泄漏 EnTT、GLM、bgfx 或 cgltf。

具体命令见 [测试说明](testing.md)，2D/3D 产品边界见 [2D](game-2d.md)与 [3D](game-3d.md)。
