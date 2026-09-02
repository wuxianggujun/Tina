# 2D 产品架构

`tina_sample_2d` 是当前正式 2D 产品门禁，不再只是 fixture Sprite 样例。完整 feature 图通过
Catalog/TileMap/Navigation2D/Scene/Particle/Trail/UI/Audio/Physics2D/FreeType/miniaudio 的300帧结构化与 Windows 视觉证据。

## 模块边界

```text
Game2DState
  -> Catalog / AssetSystem / typed Cooked payload
  -> TileMapStream -> resident TileMapInstance + CharacterController2D
  -> NavigationGrid2D + NavigationPathfinder2D
  -> optional PhysicsWorld2D
  -> Scene::World (Camera2D + SpriteRenderer2D + PointLight2D + ShadowOccluder2D)
  -> Scene::ParticleSystem2D + Trail2D
  -> RenderScene extraction
  -> RenderDevice Texture2D binding + bgfx Sprite2D pass
  -> retained UI + AudioEngine
```

- `tina_scene` 的 World Sprite 与独立 `ParticleSystem2D`/`Trail2D` 只存 weak Sprite `AssetHandle`，
  extraction 显式借用共享 resolver 后写 packet-local texture ref；
- TileMap emit 保存 weak Tileset `AssetHandle`，每次非空可见 emit 借用 resolver，通过
  `Sprite2DBindingRegistry` intern packet-local texture ref，不保存产品 key/ref；
- TileMap、角色控制、选择高亮、Physics sync 和产品规则留在 Asset/产品 State；
- Navigation2D 由产品 State/Resources owner 持有，不进入 Scene World；TileMap 转换桥留在 Asset；
- Render backend 不理解 tile/cell/gameplay，也不接收 AssetHandle；
- Box2D、bgfx、FreeType、miniaudio 均位于可选私有 adapter。

## 坐标与 Camera2D

- World：右手坐标、Y-up、单位米；
- Tile grid：整数 cell，使用显式 `cellSizeMeters`；
- UI/GLFW：窗口逻辑坐标；
- Render surface：framebuffer pixel；
- Camera2D：`FixedWorldHeight2D` 或 `PixelPerfect2D`，viewport 为规范化矩形。

`extractRenderSceneFromWorld()` 用当前 surface extent resolve Camera2D。framebuffer 0x0 表示 suspended，
跳过 camera view而不是修改配置。active Camera2D 超过1个会失败；非法 viewport、非有限数值或非法
pixel-perfect 组合不会静默 clamp。

World picking 在 Action Mapping 阶段使用 last-presented Camera2D 和 surface revision。只有未被 UI
consume/claim 的 primary pointer transition 才生成 `WorldPointerSample`；0 fixed-step 帧延迟消费时仍
使用锁存坐标，不按新 resize/Camera 重算。

2D gameplay 不读取 GLFW key/axis。`InputActionBinding` 用同一模型表达 keyboard/pointer/gamepad button
与 gamepad axis，State 从 Simulation/Frame snapshot 读取浮点 Action value。Axis 的 deadzone/scale 与
`SumClamped`/`StrongestMagnitude` 合成在 Runtime mapper 中完成，多个已连接手柄可共同贡献；UI
consume/claim 后 digital/analog source 均不会穿透。运行时改键只通过栈顶
`FrameUpdateContext::inputActionRebinding()` 排队，下一 mapping frame 原子应用；完整设置页面和 binding
持久化不属于当前产品切片。

## Sprite 与 GPU 资源

`SpriteRenderer2D` 保存：

- copyable weak Sprite `AssetHandle`；
- optional copyable weak Texture2D normal-map `AssetHandle`；
- size、pivot、UV override；
- color、sorting layer、order、flip 与 visible。

它不保存 `AssetLease`、`GpuTextureId` 或 bgfx handle。产品路径先把 Cooked Texture2D 解析并上传为
`GpuTextureId`，再由固定容量 owner-thread `Sprite2DBindingRegistry` 校验 Texture2D Handle，并调用
RenderDevice 实例 allocator 事务绑定 GPU texture。返回 key 在该 device namespace 内唯一、单调且不复用；
backend bind 失败不消费 key，同一 device 上的多个 registry 不会碰撞。allocator-managed registry 管理期间
不得混用 caller-chosen `setTexture2DBinding()` key。2D 通用 resolver 是 `Tina::AssetTypes` 中唯一的
`AssetFrameResourceResolver`；Scene extraction 每帧直接借用该 seam，产品实现薄调用 registry，沿 Sprite
唯一 required Texture2D cooked dependency 校验
Store owner/generation、kind、payload 与 live binding，再把 binding intern 到当前 packet，只写
`FrameResourceRef`、transform、UV 与颜色。缺 resolver
或无法解析的 visible sprite 返回 `UnresolvedSprite`。非空 normal handle 经独立
`normalTextureBindingResolver` 解析：产品 adapter 先按 Store 验证 Handle→AssetId，再复用 registry 的
Texture2D frame-resource resolver。visible sprite 声明的 base/normal 任一解析失败都在 writer 提交前返回
`UnresolvedSprite`；hidden sprite 对两个 resolver 都是零调用。

Sprite 顺序为 sorting layer → order in layer → stable source ordinal。透明语义不能为了全局纹理合批
被重排；bgfx 按最终顺序扫描相邻且 `(baseTexture, normalTexture)` ref pair 相同的 Sprite，按连续区间解析
binding、绑定纹理并 submit。例如 base ref 序列 A/A/B/A 会形成3个 batch，而不会重排成 A/A/A/B；同一
base 的 normal ref 不同也会分 batch。base/normal 的空规则、cross-packet、stale、wrong-kind 或 binding
超范围 ref 都在提交产生副作用前失败。UI 使用独立 pass，始终不混入 World Sprite batch。

`2D-LIGHT-N1/N2` 建立 frame-scoped `PointLight2D`/`ShadowOccluder2D` snapshot；N3 在同一 World
extraction 中加入 camera-space point-light culling，N4 增加 finite source radius soft shadow，N5 增加
Sprite2D optional normal map。Scene 先验证
active light、已发布 `WorldTransform`、显式 influence/source radii 与 color×intensity，再把灯心转换到旋转
相机局部空间，以精确 circle-vs-rectangle 相交决定
是否影响当前 Camera2D；相机中心使用 RenderScene committed view 相同的 pixel snap，边界相切仍保留。
只有 camera-affecting light 按稳定 Entity identity 占用8个 committed 槽，第9盏仍显式失败。没有 active
Camera2D 或 framebuffer 0x0 时不裁剪，维持全部 active light 的原容量语义；非法离屏灯也不会被裁剪
隐藏。ShadowOccluder2D 始终按已发布 XY scale、rotation、position 转换为 world-space 线段且不做
camera culling，因为视口外 segment 仍可能遮挡边界光线。

bgfx 对每个 fragment 计算线性径向衰减。`sourceRadiusMeters=0` 时 fragment→light 与 occluder 相交只清零
该点光贡献；正 source radius 时按 receiver→light 深度投影 segment，连续计算 finite source visibility，
多 segment 使用 multiplicative transmittance。该 line-source 近似保持 `8×32` 固定成本，不是精确 area-light
积分或重叠区间 union。
ambient、上述透明排序、连续 texture batch 与 premultiplied alpha 不变。没有 PointLight2D 组件时维持
原 unlit 输出，inactive-only 可显式发布 ambient-only。产品 sample 固定创建暖/冷两盏相机内灯、1盏
永久离屏 active light 与两条遮挡线并逐帧发布；当前 schema 29 继承 schema 19 并断言 `authoredPointLight2DCount=3`、
`pointLight2DCount=2`、`culledPointLight2DCount=1` 与默认 `softShadowPointLight2DCount=2`，同时继承
schema 16 的双灯双遮挡 evidence。`RunProduct2dShadowVisualGate.ps1` 对 soft/hard 各重复两次并证明两种
RGBA8 fingerprint 稳定且不同。角色 Sprite 另带独立3×1 normal atlas；`RunProduct2dNormalMapVisualGate.ps1`
在保持 normal atlas cook/load/upload/register/retire 不变的前提下，仅清空组件 normal handle，对 on/off 各重复
两次并证明 `normalMappedSpriteCount=1/0`、同模式可重复且跨模式像素不同。当前仍不包含跨 GPU lighting exact golden。

产品 sample 当前上传三张 Cooked Texture2D（Tile、角色 base、角色 normal），并把每张 `GpuTextureId` 连同一份 resident `AssetLease`
转移给 State-owned `Sprite2DBindingRegistry`；registry 借用 `TileMapResources` 中的 `AssetSystem` 与
`DeviceCapture` 中的 RenderDevice，两个外部 owner 都必须保持地址稳定并覆盖 State/registry 及已提交的
retirement pin 生命周期。World 里的 crate/角色帧保存 Catalog Sprite handle，再由 borrowed resolver
调用 registry 解析。Particle/Trail 保存 Catalog Sprite handle，并分别通过显式借用的 resolver 调用 registry；
TileMap emit 保存 Catalog Tileset handle，每次非空可见集合通过 resolver 薄调用 `resolveTileset()`，沿
Tileset 唯一 required Texture2D dependency 取得当前 packet texture ref。hidden/off-camera/empty 不调用
resolver；解析失败清空输出并返回 `SpriteBindingNotFound`。selection highlight 同样即时解析，不跨帧
保存 ref。World、TileMap、selection、Particle 与 Trail 对同一 texture 的 intern 由 packet 去重；首次
登记的 registry entry borrow pin 覆盖 submit/present CPU 借用期，active pin 清零前 retirement 失败且
Entry 保持可重试。State RAII teardown 只调用 `retireAllTextureBindings()`；backend 接受后原子失效 GPU
generation、清除引用它的 binding，并由 AssetSystem retirement ledger 持有 Lease 到 completion。
N16.3 已关闭 Sprite2D registry/Lease/GPU 的分裂 owner；N16.4 也已关闭 3D Mesh/Material/共享 Texture
owner，总体 `ASSET-HANDLE-SCENE` 已完成。
Tile 与角色因此可以在同一 RenderScene 中保持排序语义并使用不同纹理，不再受历史 fixture key 1 限制。

## Sprite 动画

`SpriteAnimationClip` 是 Cooked asset kind，唯一现行 typed payload 为 schema v2：32-byte header、12-byte
帧记录（sprite dependency index、正有限 duration、event 区间）与 8-byte notify event（非零 u32 tag、u16
定点**帧内** normalized offset，改帧长会带着事件一起移动）。payload 保存 `Once`、`Loop`、`PingPong` 模式、
按 authoring 顺序排列的帧，以及指向去重后 required Sprite dependency stream 的索引。writer/parser 限制
最多4096帧、每帧64个/单 clip 16384个 event，并严格检查 schema、flags、大小、依赖索引、duration 与 event
offset；v1 payload 以 `UnsupportedSchema` 拒绝，不做双读或迁移。Asset typed
view 继续校验 Cooked kind/version、依赖数量、required Sprite kind，以及每个 dependency 都实际被帧引用。

这里的 `Once`/`Loop`/`PingPong` 属于 `AssetFormat::SpriteAnimationPlaybackMode`
（`include/tina/asset_format/SpriteAnimationClipPayload.hpp:15`），与下文 `SpriteAnimator2D` 使用的
`Scene::SpriteAnimationPlaybackMode`（`include/tina/scene/SpriteAnimator2D.hpp:13`）是**两个不同的类型**：
前者取值 `Once=1`/`Loop=2`/`PingPong=3`，后者取值 `Once=0`/`Loop=1`/`PingPong=2`，整体相差 1。

跨这条边界必须写显式转换，`static_cast` 会静默损坏播放模式：cooked `Once` 变成 Scene `Loop`、cooked
`Loop` 变成 Scene `PingPong`，两者都是合法 Scene 取值，因此 `SpriteAnimator2D` 的
`isValidPlaybackMode()`（`src/scene/SpriteAnimator2D.cpp:19-28`）不会报错，动画只是播错模式；只有
cooked `PingPong` 会越界成非法值并被 `setClip()` 以 `SceneErrorCode::InvalidAnimation` 拒绝。范例转换
函数见 `samples/2d_tilemap_bgfx/main.cpp:1476-1490` 的 `toScenePlaybackMode()`：逐枚举值 switch 映射，
并对未知值返回 failure 而非落到默认分支。

Catalog recipe 支持：

```text
spriteanim <clip-id> <Once|Loop|PingPong> <frame>...
frame := <sprite-id>:<duration-seconds>[#<tag>@<offset>...]
```

tag 可写 `0x` 十六进制或标识符（FNV-1a 32 哈希成非零 u32），offset 可写小数（`0.75`）或百分比（`25%`）；
同帧事件按 offset 升序稳定存储，等值保留 authoring 顺序，cook 结果确定。

`SpriteAnimator2D` 接收已在 Asset/Scene 边界解析为 `SpriteRenderer2D` 的帧；每帧复制 weak Sprite handle
与 optional weak normal Texture2D handle，
但不持有 AssetLease 或 backend texture。它支持 Once 停在末帧、Loop、PingPong 反向经过内部帧、pause/play/stop/restart、
正倍速和跨多帧的大 delta；无效 clip、非正倍速及负数/非有限 delta 会显式失败。clip 帧在设置时复制，
`update()` 不分配，并以 `crossedEvents` 按时间顺序上报本次推进穿越的 notify events（半开区间判定、Once
终点闭合、PingPong 镜像反向、overflow 截断置位）；完整穿越语义见 [Scene](scene-ecs.md)。

产品 sample 从 Catalog 解析 Idle、Walk、HitWall 三个 clip（共5个 resolved frame），由角色 fixed-step
状态驱动 `Idle -> Walk -> HitWall`。HitWall 使用 Once clip，并把完成状态写入结构化门禁；角色当前帧
直接更新 Scene 的 `SpriteRenderer2D`，使用独立角色 Sprite handle/base atlas binding；默认模式同时复制
aligned normal atlas handle，`--disable-normal-maps` 只清空该组件 handle，不跳过 normal atlas 生命周期。

## Particles 与 Trail

`ParticleSystem2D` 和 `Trail2D` 是 `Tina::Scene` 的两个 standalone owner-thread system，不是 World
component，也不依赖完整 AssetSystem 或 bgfx；它们只复制轻量、copyable weak Sprite `AssetHandle`，不持有
`AssetLease`、Cooked payload、GPU texture owner 或 resolver。二者在 `Create()` 时通过调用方
`memory_resource` 建立固定容量 PMR storage；后续成功的 emit/append、update 和 extract 不扩容。它们复用
调用方当前 phase 的 `RenderSceneWriter` 提交 Sprite2D，真实纹理由显式借用的共享
`AssetFrameResourceResolver` 与当前 `FrameResourceSink` 映射为 packet-local texture ref。

粒子系统对每个 `randomSeed`（包括0）使用固定确定序列。`emitBurst()` 在写入前拒绝空 Sprite handle，并
完成其余 burst validation、
剩余容量和稳定 key 空间检查；任一失败都不推进 RNG、next key 或 live set。成功粒子按单调 key 分配，
过期或 `clear()` 后也不复用。`update()` 先 preflight 所有 age，以及仍存活粒子的积分后位置；任何溢出
使整批状态不变，成功后才推进并压缩过期粒子。extract 按 `age/lifetime` 线性插值 size 与 color，并为
每个 live particle 即时解析其保存的 handle；没有 live particle 时不调用 resolver。

Trail 第一个 `appendPoint()` 只建立 anchor，后续有效非退化点各追加一个从旧 anchor 到新点的 segment；
`breakTrail()` 使下一点建立新 anchor，不跨断点连线。每段从创建时独立计 age/lifetime，宽度按各自
normalized age 在 start/end width 间线性插值。几何、容量或稳定 key 失败不修改 anchor、segments 或
next key；update 同样先 preflight 全部 age 后再推进与删除过期段。segment key 单调分配且不因过期复用。
`Trail2D::Create()` 拒绝空 Sprite handle；每次有 segment 的 extract 只解析一次 config 中的 handle 并复用
结果，空 Trail 不调用 resolver。

两者缺 resolver，或 stale/cross-store/wrong-kind/unbound handle 使 resolver 返回空 ref时，都 fail closed 为
`SceneErrorCode::UnresolvedSprite`。resolver 与 `userData` 仅借用到当前 extract 返回，system 不保留它们。

## TileMap 与角色控制

当前 TileMap 唯一 root payload 是 schema v3。root 按 authoring 顺序保存 tile/object layers；layer ID 和
object ID 都是 map-wide 非零唯一稳定 ID。两类 layer 都保存 visibility、strict UTF-8 name/properties；
tile layer 不再内嵌完整 grid，只保存按 `{chunkY, chunkX}` 严格排序的非空 chunk ref（坐标、实际边缘尺寸、
非空计数、`AssetId`），缺失坐标是已知空块；object layer 仍在 root 保存 point/axis-aligned rectangle 及其
metadata。旧 schema v1/v2 均不双读。

Catalog recipe 的 TileMap 唯一语法为：

```text
tilemap <id> <tileset-id> <width> <height> <cell-size-meters>
tilelayer <stable-layer-id> <0|1-visible> <name>
property <key> <value>
row <local-id>...
endlayer
objectlayer <stable-layer-id> <0|1-visible> <name>
property <key> <value>
point <stable-object-id> <0|1-visible> <name> <x> <y>
objectproperty <stable-object-id> <key> <value>
rectangle <stable-object-id> <0|1-visible> <name> <x> <y> <width> <height>
objectproperty <stable-object-id> <key> <value>
endlayer
endtilemap
```

`row` 只能出现在打开的 `tilelayer` 中；每层必须 `endlayer`，地图必须 `endtilemap`。历史
`tilemap` 后直接写裸 `row` 的单层 recipe 会被拒绝，不存在 fallback。recipe 的 name/key/value 当前是
不含空白的单个 UTF-8 token；wire schema 本身仍执行 strict UTF-8/NUL/长度校验。Cooker 当前以固定
16×16 切分 tile rows，只为非空块生成独立 `TileMapChunk` v1 Cooked asset，并把它们登记为 root 的
`Required|Deferred` dependencies；root 的 eager load 因此不会把整张地图带入 Store。

`TileMapStream` 持有 root/tileset `AssetLease`、resident `TileMapInstance` 和固定容量 chunk slot。每帧唯一
调用顺序是 `updateDemand(camera/layer) -> AssetSystem::pump() -> commitReady()`；load/retain margin、每次
request budget 与 resident capacity 都显式有界。需求移出 retain window 时，Queued/Loading 请求会取消，
Resident chunk 会从 instance detach、释放 lease 并 logical unload。load window 中的 desired chunk 是强需求，
单独超过 capacity 时旧 active set 原样保留；retain window 只作 optional cache，空间不足时按最近一次成功
`updateDemand` 时位于 load window 的 recency 自动淘汰，Tile/collision 读取不会 touch recency。同一 chunk 的 demand
取最高 priority；新请求按 `priority desc -> layerId -> chunkY -> chunkX` 消费本轮 budget，不抢占已 dispatch 的 IO。

`TileMapInstance` 只复制 root metadata/tileset 定义并保存当前 resident chunk cell；`layer(id)` 继续暴露
metadata/object borrowed view。`tileIdAt()`、`setTile()`、`chunkRevision()`、chunk extraction、dirty cache、
sprite emit 和 solid query 都要求显式 layer ID；访问 root 引用但尚未 resident 的块返回
`TileMapChunkNotResident`。每次重新 attach 取得新的 residency generation，`TileChunkDirtyCache` 同时比较
generation 与 content revision，避免 unload/reload 后误命中旧缓存。

产品 recipe 使用三个稳定 layer：visible visual tile layer `10`、hidden collision tile layer `20`、visible
gameplay object layer `30`。渲染只显式提交 layer `10`；visibility=false 使 layer `20` 不进入 sprite emit，
但 `TileMapGridCollision(stream.map(), 20)` 仍可把 resident cells 作为碰撞数据。该 grid 借用 instance，
因此产品先把 stream 放到最终地址再构造 grid，并在 controller 查询前推进 streaming。sample 遍历 object layer，
按唯一 `role=player` Point 与 `role=crate` Rectangle 消费记录，未知、重复或缺失 archetype 都 fail closed；产品逻辑
不依赖固定 object ID 或 display name。

`CharacterController2D` 使用 `IGridCollisionProvider` 进行确定性的 Tile AABB 运动。默认产品 demo 在
ground 后向右行走并撞墙；它与 Box2D dynamic body 共用同一 Tile solid 数据，但角色本身不是刚体。

`Scene::CameraFollow2D` 是 allocation-free 相机跟随状态 owner。fixed update 在 dead zone、可选最大速度、
viewport 与 world bounds 下事务式发布 previous/current center；viewport 大于 world 时按轴居中，render
extraction 只读取 `interpolatedCenter()`。产品使用零 dead zone、无限速配置保持直接跟随，streaming 读取
current simulation center，旧的散落 previous/current float 与手写 map clamp 已删除。

受控 `--seed-tile-selection=x,y` 可以验证 logical→world→cell 命中和 selection highlight。默认 smoke
不注入点击，`tileSelectionHits=0` 合法；UI 点击不得穿透成世界选择。

## Navigation2D 产品接入

`Tina::Navigation2D` 是独立于 `Scene::World` 的 backend-neutral 固定容量模块。产品在 visual/collision
chunk 已驻留、gameplay object layer 已验证后调用 `Asset::buildTileMapNavigation2DData()`：

1. hidden collision tile layer `20` 中带 Tileset `MaterialSolid` 的11个 cell 成为静态 base blocker；
2. gameplay object layer `30` 的 `role=crate` 不再 bake 进静态 grid，而在 Physics 创建后由
   `PhysicsNavigationSync2D` 注册 body-local AABB 并每次 step 后发布为动态 blocker；
3. `MaterialOneWay` 的1个 cell 通过精确 material-flags rule 得到 traversal multiplier 5，仍不成为
   Physics solid；静态 immutable grid 数据含11个 blocked cell、0 个 bake 期 rectangle；引用 chunk 未驻留或
   layer/object/rule 非法时原子失败；
4. State-owned `NavigationGrid2D` 预留固定 generation dynamic blocker 容量；
5. `NavigationPathfinder2D` 按完整32-cell map 容量一次性预分配 records/open-set/path storage。

默认四方向 A* 使用 `f -> heuristic -> row-major index` 决胜；启用对角时使用 octile heuristic，
直行/对角进入成本为 `10/14 × destination traversal multiplier`，heuristic 乘 grid 最小 multiplier。
产品在 bridge 发布 crate blocker 后，基础/动态/严格/切角 path 均为5-cell/cost 40；独立
`(1,2)->(5,2)` 查询得到7-cell/cost 60，并证明路径未经过高代价 `(3,2)` cell。另一个
`begin()` query 只 `advance(1)` 后调用 `cancel()`，终态必须为 `Cancelled`；产品 mutation 使
Grid revision 推进到10、dynamic blocker mutations=2。产品还从独立 `NavigationGrid2D` v1 Cooked asset
构建同一份 immutable data，并与 live TileMap 派生的 flags/cost payload 做 bit-exact 对账；以上字段进入
schema 29 evidence。

`begin()/advance()` 是 cooperative query，不创建 worker/thread；Pending 期间必须继续传入同一 Grid 地址与
revision，否则终态为 `Invalidated`。blocked endpoint/open-set 耗尽是 `Unreachable`，不是 API error。
完整 grid layout、借用和容量契约见 [2D 导航](navigation2d.md)。

## Physics2D 产品接入

在 `TINA_BUILD_PHYSICS2D=ON` 图中：

1. 启动 demand/pump/commit visual `10` 与 collision `20`，确认两块 resident；
2. `TileMapPhysicsSync2D::Create(stream.map(), {.layerId = 20, ...})` 绑定 hidden collision tile layer
   并固定 chunk/rectangle/staging 容量；
3. 每个 fixed tick 在 stream commit 之后、`step()` 之前调用 `synchronize(map, world)`：只处理 resident
   chunk，按 residency generation 与 content revision 增量 add/rebuild/remove，unchanged chunk 的 collider
   原样保留；
4. 每个变化 chunk 用确定性 greedy rectangle 合并后创建**一个 static body + 每矩形一个 box shape**，
   全部 staged 成功才退休旧 body；失败保留上一次发布的 collider 且不推进 stats；
5. 产品 State 从 `role=crate` 的 Rectangle record 创建一个 dynamic crate，并显式挂接 ConvexPolygon shape；
6. 同一 World 创建 circle sensor 和远离主场景的 spring Distance、Revolute 与 Prismatic joint；
7. 远离玩法区再创建一个5点 open Chain；该 Chain 挂在 static body、不是 sensor，并通过 `shapeState()` 回读为
   一个公开 `PhysicsShapeId`；
8. 每个 fixed tick 调用 `PhysicsWorld2D::step()`，读取 contact/sensor event、body state 与 joint state；
9. Scene sprite 使用 crate state 输出可见结果；
10. State 退出时先 `shutdown(world)` 退休全部 chunk collider，再释放 sync 与 stream。

当前 product-2d 证据为 `physicsStaticBodies=1`（唯一 resident collision chunk 的 collider；11个 solid cell
合并成2个 box shape）、dynamic contact 非0、300次 physics step、sensor enter/exit 各1次，以及
`physicsJointReady`/`physicsConvexPolygonReady`/`physicsRevoluteJointReady`/`physicsPrismaticJointReady`/
`physicsChainReady`
均为 true。逐 cell 的 static body 同步已删除，不保留兼容路径。完整细节见 [物理](physics.md)。

## UI 与 Audio

产品 HUD 当前包括 Label、Button、Checkbox、Slider、单行 TextEdit、65% ProgressBar、一组
Windowed/Fullscreen RadioButton，以及 Scene Explorer TreeView（13个 logical item、12个 materialized
row slot）。结构化 evidence 验证控件数量、TextEdit UTF-8 初值、ProgressBar 值、Radio 互斥选择与
Tree stable-key selection；Theme Button 通过 pending intent 在 `updateUI()` 内执行 Dark/Light 全局换肤，所有
标准控件继承产品 chrome，标题板、设置面板与标题文字的局部层级样式随 Theme 集中重算。Windows
Dark client-area 捕获验证 Scene Explorer、选中行、设置控件与 playfield 可见且无明显裁剪或重叠。

AudioClip 来自 Catalog lease。`2D-AUDIO-ADV / N7` 已完成 voice gain/pitch/pan、fade、transient one-shot
retirement，以及固定容量 PCM stream 的 submit/EOF/underrun/cancel/terminal backpressure/shutdown 收口。
产品 sample 通过 owner thread 显式调用固定帧数 `mixRealtime()`，确定性验证 stream drain、Stopped、自动
retire 与零 underrun；miniaudio callback 调用 mixer 和 device lifecycle 由
`tina_audio_miniaudio_tests` 的 adapter 测试证明，不把异步 callback 调度当作产品 stream 门禁。

## 当前产品证据

完整图：

```powershell
cmake --preset windows-msvc-vnext-bgfx-product-2d
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_sample_2d tina_navigation2d_tests tina_scene_tests tina_physics2d_tests tina_ui_tests tina_runtime_ui_tests `
           tina_ui_render_integration_tests tina_ui_freetype_tests tina_audio_tests `
           tina_audio_miniaudio_tests --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe `
  --frames=300 --frame-delay-ms=0 --ui-theme-demo --ui-tree-demo
```

当前 sample 的结构化产品门禁要求：

- exit 0，`sample=tina_sample_2d`，`productGate=bgfx-physics-freetype-audio`；
- `catalogFromRecipeFile=true`、`catalogRecipeAssets=17`（含2个 cooked chunk、独立 normal atlas、NavigationGrid2D 与 Fx2D）、`texturesUploaded=3`；
- `evidenceSchema=29`，新增 Physics2D→Navigation2D 动态 blocker 同步证据（registered/published body、sync/update/remove 计数），并继承 schema 28 的动画 notify、Cooked Navigation/Fx2D 与 Physics Chain 证据以及之前版本的 lighting/UI/normal-map 字段；Cooked Navigation 的静态 blocked cells 为 `11`，crate 运行时由 bridge 发布一个 blocker。
  `pointLight2DCount=2`、`culledPointLight2DCount=1`，并继承 schema 16 的双灯双遮挡证据；
  `shadowOccluder2DCount=2`、`softShadowPointLight2DCount=2`、
  `normalMappedSpriteCount=1`、`sceneLightingFrames=submittedRenderFrames`，并要求
  `submittedRenderFrames + skippedSuspendedSurfaceFrames = renderExtractions`；同时保留 `uiThemeDemoRequested=true`、`uiThemeSwitches=2`、
  `uiThemeButtonActivations=0`、`uiThemeFinalLight=false`，证明自动 Dark→Light→Dark 在 UI phase 完成；
- `uiTreeDemoRequested=true`、`uiTreeViewsCreated=1`、`uiTreeLogicalItems=13`、
  `uiTreeMaterializedCapacity=12`、`uiTreeSelectionChanges=2`、最终 stable key `402`/index `12`、
  `uiTreeScrolled=true`、Theme paint 与 Tree/TreeItem selected semantics 均已验证；
- `uiFlowLayersRegistered=1`、`uiFlowScreensRegistered=2`；300 帧产品路径要求
  `uiFlowScreenPushes=2`、`uiFlowScreenPops=1`、`pauseUIScreenActivated=true`、
  `baseUIScreenRestored=true`、`uiFlowActionsRegistered=4`、`uiFlowActionsCleared=4`；无人输入 gate 还要求
  `uiFlowBackActionInvocations=0`、`uiFlowConfirmActionInvocations=0`、`uiFlowMenuActionInvocations=0`、
  `pauseOpenActionInvocations=0`、
  `pauseInputDeviceHintUpdates=1`、`pauseInputDeviceRevision=0`、
  `pauseInputHintKeyboardMouse=true`、`pauseInputHintGamepad=false`、`pauseAutoResumeRequests=1`、
  `pauseResumeRequestedByAction=false`；
- `spriteBindingTextures=3`、`spriteTextureLeasesAcquired=3`、
  `spriteTextureRetirementsAccepted=3`、`spriteBindingRegistryReleased=true`、
  `spriteTextureHandlesInvalidated=3`、`spriteTextureRetirementRecords=3`、
  `spriteTextureRetirementReleased=3`、`spriteTextureRetirementLive=0`、`spriteBindingResolverHits>0`，并且
  `tileMapSpriteBindingResolverHits>0`、`particleSpriteBindingResolverHits>0`、
  `trailSpriteBindingResolverHits>0`；这些字段都进入 evidence hash；
- `tileMapStreamRequests=2`、`tileMapStreamCommitted=2`、`tileMapStreamResident=2`，且每个 frame 都推进
  demand/pump/commit；
- `objectLayerConsumed=true`、`objectLayerObjects=2`，唯一 player/crate role 与 Point/Rectangle kind 已被产品逻辑消费；
- `navigationReady=true`、`navigationFromCookedAsset=true`、`navigationCookedBitExact=true`，静态 Cooked grid
  的 solid/rectangle/blocked=`11/0/11`、weighted/max-cost=`1/5`；Physics bridge 的
  synchronizations/adds/updates/removes=`301/1/5/0`、registered/published=`1/1`；基础/动态/严格/切角路径
  cell/cost 均为 `5/40`，独立 weighted path=`7/60` 且避开高代价 cell；
  `navigationIncrementalExpandedNodes=1`、revision/mutation=`10/2` 且 `navigationCancelled=true`；
- 300次 extraction/physics step，角色 grounded/walk/hit-right；
- Tile/角色 base/角色 normal 三纹理 upload/binding、连续 sprite batch、Camera follow/interpolation、chunk cache；
- 三个动画 clip 来自 Catalog，共解析5帧；Idle/Walk/HitWall 均进入，HitWall Once clip 完成；Walk/HitWall
  notify 分别被产品消费为非零 `animEventFootsteps`/`animEventHits`，且 `animEventOverflow=0`、
  `animEventUnknownTags=0`；
- `Fx2D` v1 来自 Catalog 并经 Scene factory 创建固定容量 Particle/Trail；粒子容量12、seed `1414090305`、
  初始发射10，300帧时 expired/active/extracted=`0/10/10`；Trail 容量8、创建/active/extracted segment=3、break=1；
  `fxInitialFingerprint` 是32字符小写 hex；其内部 schema 2 用 Store 解析出的稳定 Sprite `AssetId`，不把
  瞬时 generation handle bits 或 render key 写入指纹，并覆盖确定性的初始粒子/Trail 状态；
- UI/TextEdit/ProgressBar/RadioButton、Audio Catalog lease、Advanced Audio owner-thread deterministic mix、
  Physics contact；
- `physicsSensorEnters>0`、`physicsSensorExits>0`、`physicsJointReady=true`，并要求
  `physicsConvexPolygonReady=true`、`physicsRevoluteJointReady=true`、`physicsPrismaticJointReady=true`、
  `physicsChainReady=true`；
- `stateExits=1`、`applicationShutdowns=1`、`uiRootsReleased=1`；
- `pixelCaptureOk=true`。

2026-07-28 的完整报告 `artifacts/reports/product-2d-treeview-gate.json` 记录全部 gate 步骤与300帧 sample
exit 0。2026-07-29 动态 glyph atlas 修复后的 Dark/Light FreeType 截图分别位于
`artifacts/screenshots/2d-scene-explorer-freetype-dark-fixed/20260729-001845/frame-03.png` 与
`artifacts/screenshots/2d-scene-explorer-freetype-light-fixed/20260729-002407/frame-03.png`；对应 report
均为 `ok=true`、exit 0，运行时新增的 `gameplay #30` 与主题按钮字符完整。尺寸矩阵 compare 报告
`artifacts/screenshots/ui-003-size-matrix/20260729-004341/matrix-report.json` 为5/5 `ok=true` 且
`baselineCompare.matched=true`。
历史与当前 Windows 视觉、完整模块测试证据见 [M12 Windows 证据](evidence/m12-evidence-windows.md)；当前代码门禁
还会校验上述双纹理与动画字段。可复现脚本：
`tools/windows/RunProduct2dGate.ps1`（TEST-002）。测试数量不是永久基线。

`--frames>=60` 时产品 State 会在收尾前 `requestPush` 一层 Pause GameState（block fixed/frame/UI below，
仍 extract 下层世界）。Pause `onEnter` 在既有 Layer 上 push `pause Screen`，该 Screen 的真实 Modal/标题替代
`base Screen` 参与 publication；Base 注册 `Menu` 打开 Pause，Pause 注册 `Back/Confirm/Menu` 恢复游戏。Runtime 按 Dropdown-first 顺序将
Escape/Gamepad East 的 Back 路由给该 active Screen；Enter/Keypad Enter/Gamepad South 先交给 focused control，
未消费时再路由 Confirm；可打印 P Down 在 TextEdit 聚焦时进入文本，否则 P/Gamepad Start 路由 Menu。callback
只置 pause/resume intent，合法 State/UI phase 才 push/pop Screen 与 State；`updateUI` pop Screen 并确认 base 恢复，下一帧再
`requestPop` GameState。无人输入产品 gate 在第3个暂停帧生成同一 auto-resume intent，因此 smoke 不依赖人工输入；
真实 Back/Confirm/Menu 可在第1～3帧提前恢复，提示随设备在 `ESC / ENTER / P TO RESUME` 与
`B / A / START TO RESUME` 之间切换。JSON 同时输出 `pauseOverlayPushes/Pops/Frames` 与上述 UI Flow/action 字段。短 smoke
（如 30 帧）只保持初始 base Screen，不推 Pause State。

`updateUI` 每帧从 `UIUpdateContext::committedSemantics()` 重建 `UIAccessibilityTree` 并经
`UIAccessibilityProbeProvider` 发布；JSON 输出 `accessibilityPublished`、`accessibilityNodeCount`、
各 role 命中标志（UI-002-SPI 产品证据，**非**真机 UIA/AT-SPI）。

## 组合入口（接线税）

产品 sample 不再手写 `EngineCompositionFactories`（GLFW/bgfx/Task/Audio/FreeType）。`tina_sample_2d`
经 `Tina::Desktop::CreateEngine(config, options)` 启动；仅在需要帧捕获证据时通过
`CreateEngineOptions::wrapWindowSurfaceRenderDevice` 包装 `IRenderDevice`（见
`samples/2d_tilemap_bgfx/DeviceCapture.hpp`）。业务仍在 `TileMapBgfxState`；EngineHost 仍是唯一组合根。

## 当前限制

- 当前 streaming 是固定容量 Camera/layer priority demand owner，已有稳定的新请求排序与 retain-window
  demand-recency LRU；通用 Scene 编辑器与网络 rollback 仍未提供；
- Navigation2D 仅提供唯一当前矩形 weighted grid、property-tagged Rectangle blocker 与 cooperative
    A* query；独立 Cooked `NavigationGrid2D`、Editor bake/publish、Physics2D 动态 blocker 与产品 live-vs-cooked bit-exact 对账已提供；
  当前没有 navmesh、内部 worker 或 crowd avoidance；Physics2D→Navigation2D 仅通过显式 `PhysicsNavigationSync2D` 注册桥同步，Navigation 不反向生成 collider；
- Cooked SpriteAsset 的完整 atlas/PPU metadata resolve 仍可扩展，当前产品使用 Texture2D + 显式 UV/key；
- GPU chunk mesh cache、复杂透明材质与多 camera/letterbox policy 尚未产品化；
- 当前 2D-FX 是 Cooked `Fx2D` v1 驱动的 CPU fixed-capacity Sprite2D extraction，并提供公共 authoring document；
  当前没有可见 effect graph/专用 EditorApp 面板、GPU particle simulation 或 mesh-ribbon trail；
- Physics2D 当前 shape 为 Box/Circle/Capsule/ConvexPolygon/Chain，joint 为 Distance/Revolute/Prismatic；
  Chain 是 static-only、non-sensor 的 one-sided open/loop 边界，更多高级约束未产品化；
- TileMap→Physics 当前是 per-chunk static collider + 确定性 greedy rectangle 合并 + residency/revision
  增量同步；它只覆盖 tile solid 几何，不生成 one-way/platform 语义、不做 object layer collider、
  也不提供 editor bake 或跨 chunk 的全局矩形合并；
- Linux 当前 tip、跨 GPU/DPI golden 与真实扬声器不由 Windows 报告证明。

这些限制不能通过向 gameplay 暴露 backend handle 绕过。任务与验收统一见 [Backlog](backlog.md)和
[测试说明](testing.md)。
