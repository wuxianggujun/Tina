# 物理系统

## 后端决策

Tina 只保留两个物理方向：

- 2D：Box2D 3.x，通过 `Tina::Physics2D` 封装；
- 3D：Jolt Physics，当前仍为 Deferred，尚未进入 manifest、源码或 target。

PhysX、Bullet、Rapier 等不加入依赖。若未来需要替换后端，必须新增 ADR，而不是并列引入第三套
公共接口。

## 当前实现

`tina_physics2d` 的 PUBLIC 头只依赖 Core；Box2D 类型留在 PRIVATE 实现。`PhysicsWorld2D` 是 owner-thread、
move-only 的固定容量 World，使用 generation `PhysicsBodyId`/`PhysicsShapeId`，不依赖 Scene、Runtime、
Asset 或 Render。

当前 API 已覆盖：

- 原子创建/销毁 box body + shape；
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
TileMap recipe / IGridCollisionProvider
  -> collect solid cells
  -> syncTileMapSolidsToStaticBodies
  -> PhysicsWorld2D (static terrain + dynamic crate)
  -> fixedUpdate: step + contact/body state
  -> CharacterController2D (grid sweep, not Box2D rigid body)
  -> Scene/Render extraction
```

`tina_sample_2d` 已在 product-2d 图实例化 PhysicsWorld2D：TileMap 生成 static bodies，创建 dynamic
crate，每个 fixed tick 推进并读取 contact；角色仍使用确定性的 `CharacterController2D` + grid AABB。
这两种运动模型共享 Tile solid 数据，但不强行放入同一 controller。

当前 Windows product-2d 结构化报告记录 `physicsEnabled=true`、300 次 `physicsSteps`、11 个 static
bodies 与非零 `physicsDynamicContacts`。这已经是产品接入证据，不代表 3D physics 或跨平台门禁已完成。

## 生命周期与容量

- `PhysicsWorld2D::Create()` 固定 body/shape/contact/command 容量，创建失败全量回滚；
- `step()` 只接受配置的 fixed delta，Runtime accumulator 决定每帧调用0..4次；
- contact view 从成功 `step()` 到下一次 `step()`、move、shutdown 或销毁有效；
- query 不扩容、不在回调中重入 step/mutation；
- deferred command 在下一次 step 前按 FIFO flush，stale command 被跳过并计数；
- `shutdown()` owner-thread 幂等，world 移动后 owner 转移到目标线程；
- body/shape ID 是运行时句柄，不能持久化为 gameplay/asset ID。

Physics 不在 solver callback 中直接修改 Scene；contact 应在 step 返回后转成带 generation ID 的 gameplay
事件，再由 State 在安全阶段写 World。

## Grid bridge

Asset 层提供 `syncTileMapSolidsToStaticBodies()`，Physics2D 不 include TileMap：

1. 通过 `IGridCollisionProvider` 批量收集 `MaterialSolid` cell；
2. 使用 caller scratch 和 output buffer；
3. 调用 Physics2D 原子创建 static box；
4. scratch/output 容量不足时返回 `CapacityExceeded`，不留下半批 body。

当前 bridge 不做共线 cell 合并，也不提供 GPU chunk cache；Tile chunk revision/dirty 策略由 Asset/产品层
维护。

## 验证

```powershell
cmake --preset windows-msvc-vnext-physics2d
cmake --build --preset windows-vnext-physics2d-debug --target tina_physics2d_tests tina_physics2d_bench -- /m:1 /v:m
out\build\windows-msvc-vnext-physics2d\bin\Debug\tina_physics2d_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-physics2d\bin\Debug\tina_physics2d_bench.exe --bodies=64 --warmup=60 --steps=300 --rays=4
```

`tina_physics2d_tests` 覆盖 generation、容量回滚、contact、query、deferred command、grid bridge 和
CharacterController coexistence；测试数量随工作树变化，不在本文固化数字。product-2d 还需直接运行
对应 sample 与 Audio/UI/FreeType targets，命令见 [测试说明](testing.md)。

## 后置范围

| Backlog | 范围 |
| --- | --- |
| `PHYSICS-001` | Jolt 3D adapter、独立 Tina::Physics3D API 与性能门禁 |
| `PERF-001` | 统一 `tina_bench` schema、固定 workload/fingerprint 与 p50/p95/p99 baseline |

跨平台 bitwise determinism、CCD/vehicle 等高级语义只有在真实产品场景和固定线程策略冻结后才能承诺。
