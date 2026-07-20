# 物理系统设计与选型

## 最终决定

Tina 正式支持的物理后端限制为两个：

1. 2D 使用 Box2D 3.x；
2. 3D 使用 Jolt Physics。

PhysX、Bullet、Rapier 和其他物理引擎不加入 Tina 依赖、不参与构建，也不维护备用后端。未来若产品需求发生根本变化，必须通过新的 ADR 替换现有后端，而不是增加第三套实现。

## 当前事实

vcpkg 当前解析到 Box2D 3.1.1，`Physics2D` 已参与 Tina 链接。但现有游戏没有实例化该类：角色移动与碰撞实际由 ECS `PhysicsSystem` 对 TileMap 做 AABB 查询。

当前 `Physics2D` 只设置重力并调用 `b2World_Step`，没有为 `b2WorldDef` 配置 worker count、enqueue task 和 finish task callbacks。因此它目前是一个未接入玩法、未接入任务系统的基础封装，不能据此评价 Box2D 的完整性能。

Jolt 当前尚未进入 vcpkg manifest、源码或构建系统。文档确定的是目标后端，不代表已经完成集成。

## 为什么保留这两个

| 后端 | 负责范围 | 选择理由 | 必须完成的性能接入 |
| --- | --- | --- | --- |
| Box2D 3.x | 2D 刚体、碰撞与约束 | 成熟、轻量、MIT，具有 SIMD 和任务接口，当前已经是项目依赖 | fixed-step、sleeping、批量 query 和 layer/filter；worker callbacks 由 benchmark 决定 |
| Jolt Physics | 3D CPU 刚体、碰撞与场景查询 | 面向实时游戏、多线程 Job System、批量查询、MIT，适合 Windows/Linux 和不同品牌 GPU | Tina Job/Allocator 适配、BroadPhase Layer、批量 query 和资源回收 |

普通游戏最关心 CPU fixed-step 的稳定 p95/p99，而不是某个极端 demo 的最高物体数量。Tina 不采用 PhysX GPU 路径，因为它会增加 NVIDIA 平台约束、CPU/GPU 同步和玩法查询回读成本，不符合当前跨平台 Runtime 的边界。

## 模块边界

```text
tina_scene / gameplay
  -> tina_physics2d interface -> Box2D 3.x backend
  -> tina_physics3d interface -> Jolt backend
```

两个模块可以共享 Transform、Layer/Mask、固定步长和调试绘制数据格式，但不设计包含所有维度能力的巨型 `IPhysicsWorld`。

公共接口应只暴露 Tina 类型：

- generation `PhysicsBodyId`、`PhysicsShapeId`；
- `PhysicsBody2DDesc`/`PhysicsShape2DDesc`；
- collision layer、mask 和 query filter；
- ray cast、shape cast、overlap query；
- 帧末批量 Contact Event；
- 后端无关 Debug Draw 数据。

Box2D 的 `b2BodyId` 和 Jolt 类型只存在于各自实现层。创建、销毁和 Contact 回调不得在求解器回调栈中直接修改 Scene；统一进入 deferred command/event queue。

## Simulation 契约

- Physics 只在 60 Hz fixed update 推进，单 Render Frame 最多4步；
- gameplay 在 step 前提交 command，在 step 后消费 contact/query 结果；
- Render 读取插值 Transform，不反向写入 Physics World；
- M11 首个正确实现允许明确的单线程 Box2D step；若启用后端任务，必须使用有预算的 Tina Job
  Adapter，退出时先停止调度再销毁 World，不能让 Box2D 隐式创建未知线程；
- determinism 只在同后端、同版本、同架构和固定线程配置内承诺，不能默认宣称跨平台 bitwise deterministic。

## M11-A0 Physics2D 首刀契约

M11 的第一刀只建立可独立装配的 `Tina::Physics2D` 生命周期基础，不把尚未冻结的空间 Query
和 deferred command ABI 混入同一提交：

- `PhysicsWorld2D` 由 `Game2DState` 或 2D feature 持有，不进入 `EngineHost`，不依赖 Scene/ECS、Runtime、
  Task、Asset 或 Render；
- `tina_physics2d` PUBLIC 只依赖 Core，Box2D 3.x 始终为 PRIVATE 实现；基础 vNext Null 构建不解析、
  不链接 Box2D；
- World、Body 与 Shape 只允许 owner thread 访问；World move 后 owner 转移到执行 move 的线程，
  `shutdown()` 在 owner thread 幂等，析构前必须已回到 owner thread；
- `PhysicsBodyId` 与 `PhysicsShapeId` 分别复用 Core owner-aware `GenerationPool`，拒绝 invalid、stale 和
  cross-world handle；它们是 runtime identity，不得持久化为 gameplay ID；
- `PhysicsWorld2DConfig` 默认 body/shape 容量为1024/2048，硬上限为1,048,576/2,097,152；默认固定步长
  为1/60秒，Box2D solver sub-step 默认为4、有效范围为1至64；所有容量在 Create 时固定；
- `createBoxBody()` 原子创建一个 Body 与一个 Box Shape，任一步失败都逆序回滚且不发布半成品；首刀的
  Body 只拥有这一 Shape，销毁 Body 同时使两个 generation handle 失效；
- `step()` 不接收 render delta，只推进配置中的固定步长；Runtime 继续负责 accumulator 与单帧最多4个
  fixed tick 的 catch-up policy；
- Tina-owned registry/Pimpl 走调用者注入的 PMR；不调用进程全局 `b2SetAllocator()`；后续资源门禁将在
  独立 Physics2D 测试进程中用 backend byte baseline 验收，A0 当前测试源码尚未覆盖该项。

A0 直接门禁已在 Windows `windows-msvc-vnext-physics2d` Debug/Release 上通过；与 A1–A3 合计
`tina_physics2d_tests` 20/20。

## M11-A1 Contact Event 契约

A1 在 `step()` 返回前把 Box2D transient contact 复制到 Create 时固定的 Tina storage：

- 默认 begin/end/hit 容量为256/256/64，硬上限各 1,048,576；容量为0表示该通道永不发布；
- `PhysicsBoxShape2DDesc::enableContactEvents` 默认 true，`enableHitEvents` 默认 false；
- `contactEvents()` 返回 owner-thread borrowed view：有效期从成功 `step()` 到下一次 `step()`、
  `shutdown()`、move 或销毁；不得越界保存 span；
- begin/hit 只发布两侧 shape 仍可解析且未销毁的事件；end 允许 destroy tombstone，
  `shape*Destroyed` 标记至少一侧已销毁，仍尽量保留 last-known Tina Body/Shape ID；
- 各通道独立 overflow 标志与累计 dropped 计数；overflow 时只保留前缀，不扩容、不 heap fallback；
- 不提供 solver callback 内重入入口。

## M11-A2 Spatial Query 契约

A2 提供 owner-thread 同步空间查询，结果写入调用方 buffer，不扩容、不 heap fallback：

- `overlapAabb()` 用与 AABB 等大的 box proxy 做 **精确** overlap（不是 broadphase-only），命中按
  body index → shape index → generation 稳定排序；
- `castRay()` 收集全部 hit 后按 fraction → body/shape index 排序；`castRayClosest()` 走 Box2D
  closest 路径并映射 Tina Body/Shape ID；
- `PhysicsQueryWriteResult2D` 报告 `written`/`totalFound`/`overflow`；空 buffer 仍可报告 totalFound；
- 输入预校验：AABB lower≤upper 且有限；ray translation 非零且有限；query filter categoryBits≠0；
- Query 期间禁止重入 step/mutation（与 `ensureUsable` 同一 owner-thread 门闩）。

A0–A2 直接门禁：Windows `windows-msvc-vnext-physics2d` Debug/Release `tina_physics2d_tests` 已通过；
与 A3 合计 **20/20**。

## M11-A3 Deferred Command 契约

A3 在 Create 时固定 command 容量（默认 256，硬上限 1,048,576；0 表示禁止 enqueue）：

- gameplay 在 owner thread 上 `enqueue*`，`step()` 进入 solver 前按 FIFO 应用整队；
- 支持：`DestroyBody`、`SetTransform`、`SetLinearVelocity`、`SetAngularVelocity`、
  `ApplyForceToCenter`、`ApplyLinearImpulseToCenter`、`SetEnabled`、`SetAwake`；
- 满队列返回 `CapacityExceeded`，不丢弃已排队命令；`clearCommands()` 可整队丢弃；
- enqueue 时校验 body 仍有效；flush 时 stale body 跳过并计入 `skippedStaleCommandCount`；
- 立即 `destroyBody` 与 deferred destroy 共用同一退役路径；不提供 solver 回调内重入 enqueue；
- `createBoxBody` 保持立即原子创建，不进入 deferred 队列。

A0–A3 直接门禁：Windows `windows-msvc-vnext-physics2d` Debug/Release `tina_physics2d_tests` **20/20**。

## M11-A4 Single-thread Bench 契约

A4 提供独立可执行 `tina_physics2d_bench`（`samples/physics2d_bench`，`TINA_BUILD_PHYSICS2D` 图）：

- 固定 `stack_dynamic` 场景：1 个 static ground + N dynamic box（默认 64）；
- 参数：`--bodies=N`、`--warmup=N`、`--steps=N`、`--rays=N`；
- warm-up 后测量单线程 `step()` 的 p50/p95/p99/max/mean（ns，nearest-rank）；
- 可选每步 `castRay` 查询累计时间与 hit 总数；退出前 `shutdown()`；
- 输出精简 JSON（`status/sample/workload/step_ns/...`），**不是** ADR 0018 完整 `tina_bench` schema；
- 不启用 Box2D worker callbacks；仅当本基线 p99 超产品预算时，才另开提交做 Job Adapter。

Windows Release 样例命令：

```powershell
cmake --build --preset windows-vnext-physics2d-release --target tina_physics2d_bench
out\build\windows-msvc-vnext-physics2d\bin\Release\tina_physics2d_bench.exe --bodies=64 --warmup=60 --steps=300 --rays=4
```

## M11-A5 Grid Static Body Sync 契约

A5 在 `Tina::Physics2D` 内提供 **不依赖 Asset/TileMap** 的网格静态体同步：

- `createStaticBodiesForSolidCells()`：每个 solid cell 一个 static box；中心
  `((x+0.5)*cellSize, (y+0.5)*cellSize)`，半边长 `cellSize/2`；
- 单次调用全有或全无：任一步失败逆序销毁本批已创建 body，不留半同步几何；
- 调用方从 `IGridCollisionProvider`/`TileMapInstance::querySolidAabb` 收集 cell 列表后传入；
  physics2d 模块 PUBLIC 仍只依赖 Core；
- `destroyBodies()` 批量销毁，stale/invalid 跳过；
- 不合并共线 cell、不做 chunk dirty rebuild（后续产品接线切片再做）。

A0–A5 直接门禁：Windows Debug/Release `tina_physics2d_tests` **23/23**。

## Tina 性能门禁

M11 已有单线程 `tina_physics2d_bench` 基线；只有实测证明单线程 step 超预算，才启用 Box2D worker
callbacks。至少测量：

1. 静态场景上的大量动态刚体堆叠；
2. 高速物体与 CCD；
3. 大量 sleeping/waking；
4. ray cast、shape cast 和 overlap 批量查询；
5. 角色控制器、约束或车辆等真实玩法负载；
6. 单线程 step 的 p50/p95/p99、主线程占用、峰值内存和300帧资源回收；
7. 只有 profiling 触发并实现 Tina Job Adapter 后，对应提交才必须验证1、2、4、8 worker 的
   缩放、主线程占用、结果一致性与退出 barrier；M11 基线不为跑这项测试提前实现 worker adapter。

每个后端必须使用稳定的固定场景、fixed delta、broadphase 边界、Release 优化和日志设置。记录 warm-up 后的 step time p50/p95/p99、峰值内存、活跃接触数和查询吞吐；只比较平均 FPS 不足以判断集成质量。若 Jolt 未达到 3D 性能预算，应先分析任务划分、Layer、sleeping、查询和同步成本，而不是把第二套 3D 后端长期塞进项目。

## 近期落地顺序

1. 先确认当前游戏哪些对象继续使用确定性的 TileMap AABB，哪些需要 Box2D 刚体。
2. 将 Box2D 封装成独立 2D World，先完成单线程正确性、生命周期和 benchmark；
3. 用同一 `PhysicsBodyId` generation 测试覆盖销毁、复用和 stale handle。
4. 仅当基准超出预算时接入 Tina Job Adapter/worker callbacks，并固定 worker/shutdown 契约；
5. 等出现真实 3D 玩法测试场景后，再正式引入 Jolt 和对应 benchmark；
6. PhysX、Bullet、Rapier 不进入 Tina manifest、源码和构建目标。
