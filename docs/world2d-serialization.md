# World2D 序列化

本文描述 `Tina::AssetFormat` 与 `Tina::Scene` 已公开的 2D World/gameplay 快照边界。它是 game save/state
snapshot，不是新的 Catalog `AssetKind`，也不替产品决定文件路径、存档槽、压缩、加密或云同步。

## 当前格式

只存在 schema v7：32-byte header，随后是每 entity 固定480-byte named record，最后是可选 game-owned blob。
名称槽固定为64 bytes（UTF-8，最多63 bytes，包含NUL终止和零填充）。格式上限为4096个 entity 与4 MiB gameplay bytes。所有保留位、未声明 component 区域和未启用 Sprite
override 区域必须为零；payload 长度必须与 header 精确一致。旧 schema 均按 current-only 纪律直接拒绝，
不保留兼容读取或字段 fallback 分支。

Sprite 区偏移 `+80` 保存16-byte 自定义 fragment shader `AssetId`，`+76..+79` 保留零。
uniform 值属于 `Asset::ShaderBindingRegistry` 的 runtime binding，不进入快照。
Camera 从 entity 偏移152开始占48 bytes，PointLight/ShadowOccluder/Animation/Resource/PhysicsBody/PhysicsShape/
Name 分别位于200/232/256/288/312/360/416。名称仍占64 bytes。

entity record 保存稳定 entity ID、先出现的 parent stable ID、LocalTransform 和以下可选组件。
**每个 wire payload 都必须被 Scene 消费或显式拒绝，不允许静默丢弃**（[ADR 0030](adr/0030-gameplay-2d-binding-and-physics-bridge.md)）：

- `name`：节点 UTF-8 名称，空字符串表示未命名；
- `nodeKind`：authored 节点种类；Node2D 与 Marker2D 均无 component payload，但二者身份不同；
- `SpriteRenderer2D`：Sprite/normal Texture/custom Shader `AssetId`、override、颜色、排序、flip/visible；
- `Camera2D`：Isometric/FixedWorldHeight/PixelPerfect、viewport、pixel snap、active；
  Camera 区 `+20` 为等距 view height，`+24/+28/+32` 为 fixed height/reference PPM/reference height，
  `+36/+40/+44` 为等距 tile width/tile height/elevation step。全部尺寸 finite 且正，elevation step 允许0。
  wire descriptor 的非当前 mode 参数也独立写读；Scene variant capture/restore 保留当前 mode 的全部参数。
- `PointLight2D`：linear color、intensity、influence/source radius、active；
- `ShadowOccluder2D`：local segment 与 active；
- `SpriteAnimation2D`：SpriteAnimationClip `AssetId`、playback speed（正有限值）、autoPlay。
  该组件是对同 entity `SpriteRenderer2D` 的绑定，缺少 sprite 时校验拒绝。Scene World 以
  `SpriteAnimationBinding2D`（weak clip handle + speed + autoPlay）承载它：restore 需要 resolver 把 clip
  `AssetId` 解析为 weak handle（未解析 fail closed），capture 经 `assetIdForHandle` 写回稳定 `AssetId`。
- `PhysicsBody2D`：速度、阻尼、重力倍率与 sleep/rotation/CCD 标志。Scene 以同名**数据**组件承载，
  组件内不含 `PhysicsBodyId` 或任何 backend handle，因此 `tina_scene` 不链接 Physics2D
  （[ADR 0010](adr/0010-separate-physics-backends.md) 的后端分离不被破坏）；
- `PhysicsShape2D`：Box/Circle/Capsule 尺寸、local center/angle、capsule 端点与材质/事件标志。
  同为数据组件；`ConvexPolygon`/`Chain` 没有 wire 表示，需要它们的游戏直接用 Physics2D API 建；
- `ResourceBinding2D`：TileMap/Fx2D/NavigationGrid2D/AudioClip 的 `AssetId` 与 active。Scene **不解释**
  该 AssetId 的用途，只保证字节往返；实例化仍属游戏或 Asset 层。

body 与 resource 的**具体种类不在 payload 里**——wire format 把 Static/Rigid/Character/Area 与
TileMap/Fx/Navigation/Audio 编码在 `nodeKind` 上。因此两个 Scene 组件各自显式携带一个 kind 字段，
restore 时从 `nodeKind` 恢复、capture 时据此重新派生 `nodeKind`；否则 round-trip 会把所有 body 退化成
`StaticBody2D`。`CollisionShape2D` 必须有 physics body 父节点，这条 wire 校验与 Editor 侧约束同口径。

Marker2D 由 World 自有的空标记组件 `Scene::Marker2D` 承载。restore 设置该标记，capture 根据标记区分
无 payload 的 Marker2D 与普通 Node2D，不从 `World2DSceneIndex` 快照猜测。改名或修改 transform 不改变标记；
`clearMarker2D()` 后，无 payload 的实体才会保存为 Node2D。标记与其他 payload-bearing 组件并存时，capture
显式返回 `SceneErrorCode::InvalidComponent`，不改型或丢字段。该语义沿用现有 wire kind，schema v7 与480-byte
record 均不变。

Runtime `EntityId` owner/index/generation、weak `AssetHandle`、AssetLease、Render/GPU identity 永不序列化。
这避免 restore 后误把旧 registry identity 当成 live 对象。

## Capture 数据流

```text
World owner-thread view
  -> stableEntityId(EntityId)
  -> assetIdForHandle(Sprite/Texture/Shader weak handle)
  -> hierarchy depth + stable ID ordering
  -> validate canonical schema-v7 descriptors
  -> owning byte vector
```

非空 World 必须为每个 entity 提供唯一非零 stable ID。父节点总在子节点前；同层按 stable ID 排序，因此
create order、slot reuse 和 generation 不改变输出。capture 只支持2D组件，发现任一3D组件即 fail closed，
不生成有损快照。gameplay bytes 非空时 schema/version 必须同时非零；内容只由游戏解释。

## Parse 与 Restore

`parseWorld2DSnapshot(payload, entityStorage)` 返回 view。entity span 借用 caller-owned `entityStorage`，gameplay
span 借用原始 payload；任一 backing storage 修改或析构后 view 失效。parser 先写临时 storage，完整验证通过
后才替换 caller storage，所以失败不会抹掉上一次成功结果。

```text
schema-v7 view
  -> validate all records and parent order
  -> check remaining World capacity
  -> resolve every Sprite/Texture/Shader AssetId to weak AssetHandle
  -> prepare every component
  -> create + KeepLocal parent + set components
  -> publish world transforms
```

`World2DSnapshotAssetResolver::resolveShader` 只在某个 sprite 记录带非零 shader `AssetId` 时才被要求；缺失
或解析不出 handle 都返回 `UnresolvedSprite`，不会退回引擎 fragment。

restore 的 schema/容量/资源/组件失败发生在 World mutation 前。后续任一步失败会逆序销毁本次创建的全部
entity，再恢复 transform publication；调用前已存在的 entity 保留。返回 binding 只把 stable ID 映射到本次
生成的 runtime `EntityId`，不能持久化后者。

`loadWorld2DSceneFromFile(world, utf8Path, assets)` 是上述三步（读文件 → parse → instantiate）的组合入口，
供 shipped game 直接消费 Editor 保存的 `.tworld`。它没有独立的格式或校验分支，失败保持 World 原样。
`World2DSceneLoadResult::gameplayBytes` 是拷贝：parsed view 借用的是该调用自己拥有并在返回前释放的缓冲区。

## 版本策略

Runtime 只接受 `World2DSnapshotWire::SchemaVersion`。旧版本 fixture 必须返回 `UnsupportedSchema`；当前开发期
不保留旧布局解析、dual-write 或字段 fallback。将来确需升级时，新增独立 schema 与离线 migration 工具，
migration 输出仍必须通过唯一现行 parser，再交给 Runtime restore。

## 验证

完成源码与文档收口、获得测试授权后，可运行这两个专项测试族：

```powershell
tina_asset_format_tests.exe --gtest_filter=World2DSnapshotTests.*
tina_scene_tests.exe --gtest_filter=World2DSnapshotSceneTests.*:SceneWorldTest.MarkerTag*
```

具体 build tree 与最小门禁规则见 [测试说明](testing.md)。

重点检查非默认等距 basis 的 capture/restore、所有 camera descriptor 字段往返、Marker 的类型/名称/层级/transform
往返与 runtime 编辑、混合 payload 显式拒绝、标记 slot 复用/错线程拒绝，以及旧 schema 拒绝和失败原子性。
本次集中验证结果单独记录，不使用旧 schema 的测试结果作为当前证据。
