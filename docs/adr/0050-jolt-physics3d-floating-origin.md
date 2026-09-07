# ADR 0050: Jolt Physics3D 与显式浮动原点

- 状态：Proposed
- 日期：2026-09-06
- 关联：ADR 0010、0015、0019；`PHYSICS-001`
- 实施依据：本轮用户明确要求推进 Jolt 3D Physics 与 floating origin；本文补充版本、线程、坐标和失败边界，尚未声称独立完成 maintainer 审阅。

## 背景

3D 物理此前没有模块或消费者。直接给 `Scene::Transform`、Renderer 或 Navigation 换成 double，既会扩大
并行改动面，也不能使 GPU 的 float 矩阵自动拥有大世界精度。Jolt 5.5.0 的 `PhysicsSystem` 没有公开
`ShiftOrigin()`；不能按不存在的后端接口设计公共 API。

## 决定

1. 新增可选 `Tina::Physics3D`，PUBLIC 只依赖 Core/Math，PRIVATE 链接 Jolt。固定 vcpkg baseline 下的
   `joltphysics 5.5.0`，不带 debugrenderer/profiler；编译检查版本及单精度配置，创建时再检查 Jolt ABI feature ID。
   该 port 没有 ConfigVersion 文件，因此 CMake 不写无法解析的 `find_package(Jolt 5.5.0 EXACT)`。
2. `PhysicsWorld3D` 是 State/game-owned、owner-thread、move-only world。`JobSystemSingleThreaded`
   不产生 worker；`step()` 只推进配置的 fixed delta，accumulator/catch-up 仍归 Runtime。未承诺跨 CPU/编译器
   bitwise 确定性，也不把 vcpkg 的 `CROSS_PLATFORM_DETERMINISTIC=OFF` 写成网络 lockstep 保证。
3. 采用唯一坐标模型：局部 `Math::Vec3` float 米制、Y-up；累计原点与全局位置是显式
   `PhysicsGlobalPosition3D` double。全局减原点在 double 中完成后才窄化，禁止先把大世界坐标转成 float。
   全局坐标限制每轴 `1e12 m`，公开局部输入限制每轴 `1e6 m`。这些是拒绝边界，不是推荐的常驻精度范围。
4. `shiftOrigin(offset)` 在两个 step 之间执行：`local -= offset`、`origin += offset`。预检查全部 body、
   全局原点、浮点可表示性和 revision，再更新每个 Jolt body 及 broadphase，最后发布一次 origin revision。
   Static/Kinematic/Dynamic、sensor 与 sleeping body 都必须包含。保持 ID、旋转、速度和 awake 布尔状态；
   后端 contact cache 被显式失效，active body 的 sleep-test timer 会重启。零位移不推进 revision。
5. 一个 body 持有一个不可变的居中 Box/Sphere/Capsule shape，不复制 2D 的 body/shape/joint registry 结构。
   `GenerationPool` 检查 owner/index/generation；变换、射线与 AABB 结果携带 origin revision。AABB 是明确的
   conservative broadphase query，不冒充精确 shape overlap。
6. Body registry、rebase scratch 和 query scratch 在 Create 固定容量。Jolt body/shape 与 solver 临时分配
   仍使用后端 allocator，不承诺整个 solver PMR 化或零分配。pair/contact budget 不足导致 Update 返回错误时，
   将 world 标记 faulted；只有 stats/shutdown 可用，不把已经执行的部分求解假装回滚。
7. Jolt 强制的进程级 type registration 由私有引用计数 RAII lease 管理，最后一个 world 销毁后注销。
   这不是 World locator；外部已注册 Jolt factory 时明确拒绝接管，不覆盖其全局指针/allocator。

## 数据流

```text
game global double position - PhysicsOrigin3D
  -> local float body descriptor
  -> fixed step (Jolt, single owner)
  -> optional explicit origin shift (all bodies, one revision)
  -> bodyState + originRevision
  -> game-owned Scene/camera synchronization
  -> local float RenderScene extraction
```

## 后续边界

本切片不修改 Scene、Render、Navigation 或 Asset wire schema，不创建 `Scene3DRuntime`，不把 Physics 放进
EngineHost。游戏必须在同一安全阶段同步 Scene roots、相机、灯光和自有缓存，然后才 extract；不能只移动物理世界
却继续使用旧的渲染/查询坐标。Joints、compound/mesh shape、CCD、character controller、contact event API、
Scene authoring/bridge 和性能预算由后续明确的切片处理。

## 验证

独立 `tina_physics3d_tests` 包含 header isolation、三类 shape 的落体求解、句柄/容量/线程/lifetime，以及
global/local 转换、全体 rebase、sleeping body、失败原子性、重复平移与平移后查询/碰撞的回归源码。
编译、单元执行、产品接入和性能证据分别登记，不以编译成功替代实际执行。
