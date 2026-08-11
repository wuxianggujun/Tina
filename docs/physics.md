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

- body 与 shape 分离创建；一个 body 可挂多个 Box/Circle/Capsule/ConvexPolygon shape，并可独立查询/销毁
  shape；ConvexPolygon 接受 3..8 个按顺/逆时针严格凸边界排列、且满足当前 backend hull 容差的顶点，可附加
  local center/angle transform；
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

产品还创建一个 circle sensor，以及远离 crate/角色主场景的 spring Distance、Revolute 和 Prismatic
joint。当前 Windows
product-2d 300 帧结构化报告记录 `physicsEnabled=true`、`physicsSteps=300`、`physicsStaticBodies=1`
（recipe 地图 8×4、cooker chunk size 16，collision layer 只有一个 resident chunk，其11个 solid cell
合并成2个 box shape）、非零 `physicsDynamicContacts`、`physicsSensorEnters=1`、`physicsSensorExits=1` 与
`physicsJointReady=true`、`physicsConvexPolygonReady=true`、`physicsRevoluteJointReady=true` 与
`physicsPrismaticJointReady=true`。这已经是产品接入证据，不代表 3D physics、全部 shape/joint 类型或跨平台门禁
已完成。

## 生命周期与容量

- `PhysicsWorld2D::Create()` 固定 body/shape/joint/contact/command 容量；body/shape/joint 描述符均在 registry
  reservation 和 backend create 前完成校验，非法 polygon transform/hull 或 joint 参数不会消耗容量；
- shape 只校验当前 kind 消费的几何字段；Prismatic `localAxisA` 是尺度无关的非零方向，spring damping
  ratio 支持大于1的 overdamped 配置；Revolute/Prismatic 状态共用 `limitEnabled`/`motorEnabled`，数值按
  kind 分别读取 angle/translation 字段；
- body、shape、joint 分别验证 owner/generation；销毁 body 会同步退休其全部 shape 与关联 joint；
- `step()` 只接受配置的 fixed delta，Runtime accumulator 决定每帧调用0..4次；
- contact view 从成功 `step()` 到下一次 `step()`、move、shutdown 或销毁有效；
- query 不扩容、不在回调中重入 step/mutation；
- deferred command 在下一次 step 前按 FIFO flush，stale command 被跳过并计数；
- `shutdown()` owner-thread 幂等，world 移动后 owner 转移到目标线程；
- body/shape/joint ID 是运行时句柄，不能持久化为 gameplay/asset ID。

Physics 不在 solver callback 中直接修改 Scene；contact 应在 step 返回后转成带 generation ID 的 gameplay
事件，再由 State 在安全阶段写 World。

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
Box/Circle/Capsule/ConvexPolygon、polygon 凸性/有限 transform/事务失败、sensor enter/exit、
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

Chain 等更多 2D shape/joint，跨平台 bitwise determinism、CCD/vehicle 等高级
语义只有在真实产品场景和固定线程策略冻结后才能承诺。
