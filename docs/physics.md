# 物理系统

## 后端决策

Tina 只保留两个物理方向：

- 2D：Box2D 3.x，通过 `Tina::Physics2D` 封装；
- 3D：Jolt Physics，当前仍为 Deferred，尚未进入 manifest、源码或 target。

PhysX、Bullet、Rapier 等不加入依赖。若未来需要替换后端，必须新增 ADR，而不是并列引入第三套
公共接口。

## 当前实现

`tina_physics2d` 的 PUBLIC 头只依赖 Core；Box2D 类型留在 PRIVATE 实现。`PhysicsWorld2D` 是 owner-thread、
move-only 的固定容量 World，使用相互独立的 generation `PhysicsBodyId`/`PhysicsShapeId`/
`PhysicsJointId`，不依赖 Scene、Runtime、Asset 或 Render。

当前 API 已覆盖：

- body 与 shape 分离创建；一个 body 可挂多个 Box/Circle/Capsule/ConvexPolygon/Chain shape，并可独立查询/销毁
  shape；ConvexPolygon 接受 3..8 个按顺/逆时针严格凸边界排列、且满足当前 backend hull 容差的顶点，可附加
  local center/angle transform；Chain 接受4..64个 finite body-local 点，支持 open/loop，并且只能挂到 static
  body、不能作为 sensor；
- sensor enter/exit 复用有界 contact event view，并用 `isSensor` 明确区分；
- Distance/Revolute/Prismatic joint 的创建、查询、销毁、spring/limit/motor 状态、容量、generation reuse 与
  关联 body 级联 retirement；
- Dynamic/Kinematic/Static body state、重力与固定步 `step()`；
- begin/end/hit contact event 的有界 storage、overflow 统计和 borrowed view；
- `overlapAabb`、`castRay`、`castRayClosest`，稳定排序和 caller-owned output buffer；
- FIFO deferred command：destroy、transform、velocity、force/impulse、enabled/awake；
- 从 `IGridCollisionProvider` 创建/销毁 Tile solid static bodies；
- stale/cross-world handle、owner-thread、shutdown 与 PMR 回收错误路径。

Box2D solver 目前按配置的单线程 `step()` 执行，没有隐式创建未知 worker。只有 profiling 证明超预算时，
才考虑 Tina Job adapter；这不是当前产品能力。

## 产品 2D 数据流

```text
TileMap recipe -> v3 root + deferred TileMapChunk
  -> TileMapStream demand/pump/commit collision layer
  -> TileMapPhysicsSync2D::synchronize(map, world)
       per resident chunk: greedy rectangle merge -> one static body / N box shapes
  -> PhysicsWorld2D (chunk colliders + dynamic crate)
  -> fixedUpdate: step + contact/body state
  -> CharacterController2D (grid sweep via TileMapGridCollision, not Box2D rigid body)
  -> Scene/Render extraction
```

`tina_sample_2d` 已在 product-2d 图实例化 PhysicsWorld2D：先确认 collision chunk resident，再用
`TileMapPhysicsSync2D::Create(stream.map(), {.layerId = 20, ...})` 绑定 hidden collision tile layer，
每个 fixed tick 在 stream commit 之后、`step()` 之前调用 `synchronize()`，因此 collider 集合永远不会
和 resident cell 不一致。gameplay object layer `30` 的 rectangle object `102` 提供 dynamic crate 初始
位置，crate 使用 ConvexPolygon shape。角色仍使用确定性的 `CharacterController2D` + grid AABB
（经 `TileMapGridCollision`），并由 point object `101` 初始化位置。这两种运动模型共享 layer `20`
的 Tile solid 数据，但不强行放入同一 controller。

`Asset::PhysicsNavigationSync2D` 负责另一条显式桥：gameplay 注册动态 body 及 body-local AABB，Physics transform
是唯一真相，桥将旋转后的保守 world AABB 发布为 NavigationGrid2D dynamic blocker。它不扫描 PhysicsWorld、不暴露
Box2D geometry，也不把导航规则反向变成 collider。disabled/出界 body 暂时无 blocker，stale body 在下一次同步中自动
retirement；固定容量 planner 与 registration 在 Create 时预留，稳态同步零分配。Product 2D 的 Cooked Navigation
只含静态 Tile solid，crate blocker 由该桥发布，teardown 时先 shutdown bridge 再关闭两个 owner。

产品还创建一个 circle sensor、远离 crate/角色主场景的 spring Distance/Revolute/Prismatic joint，以及
一个5点 open Chain。当前 Windows
product-2d 300 帧结构化报告记录 `physicsEnabled=true`、`physicsSteps=300`、`physicsStaticBodies=1`
（recipe 地图 8×4、cooker chunk size 16，collision layer 只有一个 resident chunk，其11个 solid cell
合并成2个 box shape）、非零 `physicsDynamicContacts`、`physicsSensorEnters=1`、`physicsSensorExits=1` 与
`physicsJointReady=true`、`physicsConvexPolygonReady=true`、`physicsRevoluteJointReady=true` 与
`physicsPrismaticJointReady=true` 与 `physicsChainReady=true`。这已经是产品接入证据，不代表 3D physics、
全部 shape/joint 类型或跨平台门禁
已完成。

## 生命周期与容量

- `PhysicsWorld2D::Create()` 固定 body/shape/joint/contact/command 容量；body/shape/joint 描述符均在 registry
  reservation 和 backend create 前完成校验，非法 polygon transform/hull 或 joint 参数不会消耗容量；
- shape 只校验当前 kind 消费的几何字段；Prismatic `localAxisA` 是尺度无关的非零方向，spring damping
  ratio 支持大于1的 overdamped 配置；Revolute/Prismatic 状态共用 `limitEnabled`/`motorEnabled`，数值按
  kind 分别读取 angle/translation 字段；
- body、shape、joint 分别验证 owner/generation；销毁 body 会同步退休其全部 shape 与关联 joint；
- Chain 的全部 backend segment 只映射为一个公开 `PhysicsShapeId`；销毁/级联 retirement 记录全部 segment
  tombstone，contact endpoint 可恢复已销毁公开 ID。AABB/ray query 按公开 ShapeId 去重，ray 穿过多个 segment
  时只保留最近 fraction；
- `step()` 只接受配置的 fixed delta，Runtime accumulator 决定每帧调用0..4次；
- contact view 从成功 `step()` 到下一次 `step()`、move、shutdown 或销毁有效；
- query 不扩容、不在回调中重入 step/mutation；
- deferred command 在下一次 step 前按 FIFO flush，stale command 被跳过并计数；
- `shutdown()` owner-thread 幂等，world 移动后 owner 转移到目标线程；
- body/shape/joint ID 是运行时句柄，不能持久化为 gameplay/asset ID。

Physics 不在 solver callback 中直接修改 Scene；contact 应在 step 返回后转成带 generation ID 的 gameplay
事件，再由 State 在安全阶段写 World。

Chain 采用 Box2D one-sided 边界语义，按输入点序的每条有向边右侧为碰撞面；open chain 的首尾 ghost edge
不形成额外碰撞面，loop 输入不重复首点。
任意两点间距必须严格大于 `0.005 m`，避免 backend 退化边；当前 Tina 不检测 self-intersection，authoring
侧必须提供简单边界。Chain 不开放 sensor、dynamic/kinematic body 或 per-segment public handle。

## Scene 运行时所有者

`Tina::Gameplay2D` 除物理桥外还提供 `Scene2DRuntime`，它是「把 authored 场景跑起来」的所有者
（[ADR 0031](adr/0031-scene-2d-runtime-ownership.md)）。它**编排而不重新实现**：TileMap residency 仍归
`TileMapStream`，particle/trail 仍归 `ParticleSystem2D`/`Trail2D`，寻路网格仍归 `NavigationGrid2D`，
播放仍归 `AudioEngine`。它只做这些类型自己做不到的四件事——从 `AssetId` 走到可用对象（含各自要求的
lease 获取与 kind 校验）、把运行时状态放进按 `EntityId` 索引的 side table、固定每帧调用顺序、逆序释放。

存在理由是：这套顺序与生命周期对每个游戏都一样，而**每一处错误都是安静的**。TileMap 在 `commitReady()`
之前 extract 只会少画；`AudioPcmClipView` 是非拥有的，clip lease 早放就是 use-after-free。此前每个游戏
自己手写全套（见 `samples/2d_tilemap_bgfx`，6660 行）。

每帧顺序：

```text
Scene2DRuntime::updateDemand(camera)      // TileMap chunk demand（world → map-local）
AssetSystem::pump(budget)                 // 留在外部：AssetSystem 服务整个游戏
Scene2DRuntime::commitReady()             // chunk residency 提交
Scene2DRuntime::fixedUpdate(delta)        // particles / trails
Scene2DRuntime::fixedUpdatePhysics(world) // step → applyTo → updateWorldTransforms
Scene2DRuntime::extract(...)              // tile + particle sprites
```

`extract()` 在 `commitReady()` 之前调用返回错误，而不是安静地画出过期地图。`pump` 不藏进 runtime，
因为一个场景对象不应隐式驱动全局资源系统。

`fixedUpdatePhysics()` 把三步合成一次调用（ADR 0031 的 D5）：**`step()` → `applyTo()` →
`updateWorldTransforms()`**。这三步顺序错了同样是安静的——在 `applyTo` 之前读 transform 会渲染出落后
一帧的画面，漏掉 `updateWorldTransforms` 则子节点停在父 body 移动前的世界位置。每次调用只走**一个**
step：fixed-step accumulator 与 catch-up 策略归帧循环所有者（ADR 0015），需要多 substep 就调用多次。
`build()` 未传 `PhysicsWorld2D` 时返回 `Unsupported`，而不是静默空转。物理仍单向 authoritative，
移动 body 走 `PhysicsWorld2D::enqueueSetTransform`；`physicsBridge()` 暴露桥本身以便按 authored entity
查 body。

`tileMap(entity)` 返回 resident `TileMapInstance` 的引用，`tileLayers(entity)` 返回 authored 层序与
visibility。二者必须暴露，因为五个真实消费者都需要按引用拿到 instance 且都无法在 runtime 内重新实现：
`TileMapGridCollision` 借用其整个生命周期、`TileMapPhysicsSync2D` 在 Create 与每次 synchronize 都要、
`TileChunkDirtyCache::syncVisible` 每帧要、cell picking 与相机世界边界读它的 extent。层 id 是 asset
authored 的而非固定值，所以游戏必须读 `tileLayers()` 才知道该查哪层碰撞。

因此 `Scene2DRuntime` 是**不可移动的**（move 构造与赋值都已删除）：移动它会在借用方背后搬走 instance，
而那个失败是悬垂读取，不该只靠注释约束。`shutdown()` 使借用失效，故借用方必须先拆。

节点位置由 authored `WorldTransform` 决定：TileMap 的 map-local 原点与 Fx 的 burst origin 都在其上偏移
（Fx2D payload 自带的 origin 是节点内偏移，两者相加而非互相取代）。因此在 Editor 里拖动节点，运行时位置
随之改变。

TileMap 节点绑定的是整张地图，wire format 没有「选哪个 layer」的字段，因此 runtime 驱动该地图的**全部
tile layer**：每层都 stream（不可见层是游戏查询碰撞/寻路的数据源），但只有 `visible` 层 emit sprite，
这正是 layer 标志在 asset 与 Editor 中已有的含义。层序决定 `sortingLayer`，每层使用独立的 stable key
区间，否则两层同一 cell 的 tile 会被 sprite 排序视作同一项。object layer 被跳过（它承载 spawn 数据而非
cell）。一张地图的 tile layer 数超过 `tileLayersPerMapCapacity` 时 build 失败。

失败语义：kind 不匹配或 AssetId 不在 catalog 中的节点计入 `unresolvedCount` 并跳过——一个陈旧引用不该让
整个场景无法加载；容量不足与 lease 获取失败则 fail closed，且回滚本次已获取的全部 lease。
`active == false` 的节点仍被实例化并保留 lease，但不 update、不 extract、也**不可达**——
`navigationGrid()`/`fxInstance()` 返回 null，`playAudio()` 返回 `InvalidArgument`，因为能被寻路查询到的
「已关闭」区域仍会阻挡路径。切回 active 因此是一次布尔翻转而不是一次加载。

`shutdown()` 必须在 `AssetSystem`/`AudioEngine` 之前调用，可重复调用。它**先停 voice 再放 lease**：
`AudioPcmClipView` 是非拥有的，而释放最后一个 lease 会立即擦除 cooked payload，顺序反了就是
use-after-free 而不是泄漏。`playAudio()` 返回的 voice 被跟踪到终态；`releaseFinishedVoices()` 回收引擎
已退休的槽位，超出 `audioVoiceCapacity` 时 fail closed 而不是启动一个 shutdown 无法停止的 voice。

## Scene collider bridge

`Tina::Gameplay2D`（target `Tina::Gameplay2D`，仅在 `TINA_BUILD_PHYSICS2D=ON` 时存在）是 authored
场景与 `PhysicsWorld2D` 之间的桥。它必须是独立模块：`tina_scene` 不能链接 Physics2D（否则所有 Scene
消费者——含纯渲染与 UI 场景——都被迫拖入 Box2D），而 `tina_physics2d` 不能知道 Scene。这与
`TileMapPhysicsSync2D` 住在 `tina_asset` 是同一条理由（见 [ADR 0030](adr/0030-gameplay-2d-binding-and-physics-bridge.md)）。

`Scene2DPhysicsBridge`：

1. `build(world, physicsWorld, config)` 为每个带 `PhysicsBody2D` 的 entity 建一个 body，body 的
   position/angle 取该 entity 已发布的 `WorldTransform`（因此调用前必须先跑
   `World::updateWorldTransforms()`）。随后把每个带 `PhysicsShape2D` 的 entity 挂到**最近的 physics-body
   祖先**上——父子关系决定归属，与 Editor 要求 `CollisionShape2D` 必须有 body 父节点同一条规则；shape 与
   body 也可以在同一 entity 上。没有 body 祖先的 shape 计入 `orphanShapeCount` 而不是静默丢弃，因为
   「碰不到任何东西的 shape」和「桥没看见的 shape」在现场表现一样。
2. body kind 映射：`Rigid`→Dynamic，`Static`→Static，`Character`/`Area`→Kinematic（前者由 gameplay
   移动驱动、后者是 trigger 体，都不该被碰撞反推）。
3. `applyTo(world, physicsWorld)` 在 `step()` 之后把 body 的 position/angle 写回 `LocalTransform`，
   之后由调用方跑 `updateWorldTransforms()`。**物理单向 authoritative**：桥从不反读 transform 去驱动
   物理，因为双向同步必须定义谁赢与何时，而 teleport 与 CCD 语义会互相打架。要移动 body 就走
   `enqueueSetTransform`。有 parent 的 entity 被跳过：它的 `LocalTransform` 是相对父节点的，而 body 在
   world space，写进去会每帧叠加一次父变换。
4. 容量在 build 时固定，超出返回 `CapacityExceeded`；build 失败会销毁本次已创建的全部对象，不留半个
   模拟。重复 build 返回 `AlreadyExists`（否则会泄漏第一批 handle）。`shutdown(physicsWorld)` 必须在销毁
   physics world 之前调用，且可重复调用。

桥**不**驱动 `step()`：fixed-step accumulator 归 Runtime，与 `PhysicsWorld2D::step()` 既有契约一致。
`ConvexPolygon`/`Chain` 没有 authoring 表示，需要它们的游戏直接用 Physics2D API 建。

## Chunk collider bridge

Physics2D 侧只提供 backend-neutral 的 `PhysicsGridSolidRect2D` 与
`createStaticBodyForSolidRectangles()`：一次调用产出**一个 static body + 每个矩形一个 box shape**，
任一 shape 失败就销毁该 body 与本次已建 shape，空矩形 span 是成功的 no-op。Physics2D 不 include
TileMap，也不知道 chunk 概念。

Asset 侧的 `TileMapPhysicsSync2D` 是唯一现行 TileMap→Physics 桥，逐 cell 的
`collectSolidCellsForPhysics()` / `syncTileMapSolidsToStaticBodies()` 已删除，不保留兼容别名：

1. `Create(map, config)` 绑定一个 tile layer 并固定全部容量：chunk record、rectangle scratch、
   occupancy bitmap、staged/retired body 列表。`rectangleCapacityPerChunk=0` 表示按源 chunk cell 数取
   精确值；大于一个 chunk 的容量、非 tile layer、`layerId=0`、非法 material 都在发布前失败。
2. `synchronize(map, world)` 只遍历 **resident** chunk，按 `residencyGeneration` + `contentRevision`
   判定 unchanged / added / rebuilt / removed。unchanged chunk 的 body **原样保留**，不销毁重建。
3. 变化的 chunk 先把 solid cell 烧进 occupancy bitmap，再做**确定性 greedy rectangle 合并**
   （row-major 扫描，先向 +X 延伸再向 +Y 整行延伸），显著减少 body/shape 数量。
4. 新 collider 全部 staged 成功后才退休旧 body。任一 bake/create 失败会回滚本轮 staged body、
   保留上一次成功发布的 collider，并且不推进 stats —— **禁止半份发布**。
5. 容量不足返回 `AssetErrorCode::TileMapPhysicsCapacityExceeded`。`Create()` 之后稳态零分配。
6. `shutdown(world)` 退休全部 collider，owner-thread 幂等；必须在 world 关闭或 `TileMapInstance`
   消失之前调用。

layer visibility 只控制 TileMap 可见提取/渲染，不是碰撞开关；hidden tile layer 仍可被显式选为
collision layer。result-returning 的 tile/chunk/solid query 在不存在 layer 或误选 object layer 时返回
明确的 Asset error，而不是退回第0层。`TileMapPhysicsSync2D` 借用 `stream.map()` 且校验绑定契约
（asset id、尺寸、chunk size、cell size），必须先 `shutdown()` 并销毁 sync 再移动/shutdown/destroy
stream。`CharacterController2D` 继续使用独立的 `TileMapGridCollision`/`IGridCollisionProvider` grid
sweep 路径，两者共享同一 layer 的 solid 数据但互不依赖。

当前 bridge 不做共线 cell 合并，也不提供 GPU chunk cache；Tile chunk revision/dirty 策略由 Asset/产品层
维护。

## 验证

```powershell
cmake --preset windows-msvc-vnext-physics2d
cmake --build --preset windows-vnext-physics2d-debug --target tina_physics2d_tests tina_physics2d_bench --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-physics2d\bin\Debug\tina_physics2d_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-physics2d\bin\Debug\tina_physics2d_bench.exe --bodies=64 --warmup=60 --steps=300 --rays=4
```

`tina_physics2d_tests` 定向覆盖独立 body/shape/joint generation、多 shape/body、
Box/Circle/Capsule/ConvexPolygon/Chain、polygon 凸性/有限 transform/事务失败、Chain count/finite/separation/
static-only/non-sensor/open-loop lifecycle 与 multi-segment query 去重、sensor enter/exit、
Distance/Revolute/Prismatic joint 状态与 body cascade、容量回滚、contact、query、deferred command 和
CharacterController coexistence。chunk collider 桥另有专项覆盖：rectangle 合并后的跨 span 碰撞、
非法矩形/material/cell size 拒绝、shape 容量耗尽的整体回滚，以及 `TileMapPhysicsSync2D` 的
`Create()` 校验矩阵、确定性 greedy 合并、unchanged 复用（body 不重建）、content revision 只重建受影响
chunk、residency detach/attach 的 remove/add、失败后旧 collider 仍然发布且 stats 不推进、chunk 容量
超限、绑定契约拒绝、`shutdown()` 幂等与 `Create()` 后稳态零分配。测试数量不是永久基线；product-2d 还需
直接运行对应 sample 与 Audio/UI/FreeType targets，命令见 [测试说明](testing.md)。

## 后置范围

| Backlog | 范围 |
| --- | --- |
| `PHYSICS-001` | Jolt 3D adapter、独立 Tina::Physics3D API 与性能门禁 |
| `PERF-001` | **Done**：统一 `tina_bench` schema v1、`null_runtime_frames` workload/fingerprint 与 p50/p95/p99 |
| `PERF-002` | 固定机受审 baseline、hard gate 与多进程 median/MAD 协议 |

更多 2D shape/joint，跨平台 bitwise determinism、CCD/vehicle 等高级
语义只有在真实产品场景和固定线程策略冻结后才能承诺。
