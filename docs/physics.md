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

## Tina 性能门禁

M11 建立独立 `tina_physics2d_bench` 基线；只有实测证明单线程 step 超预算，才启用 Box2D worker
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
