# Physics3D 与浮动原点

`Tina::Physics3D` 是可选的 Jolt 5.5.0 私有适配模块，PUBLIC 依赖仅 Core/Math，与 Box2D `Physics2D`
独立。它提供 rigid body、Character controller、固定容量 contact event、ray/shape cast/AABB 和 floating origin；
`Tina::Gameplay3D::Scene3DRuntime` 用 `Scene3DPhysicsBridge` 将 Prefab v5 的物理描述映射到产品拥有的 Scene World。
PhysicsWorld3D 不拥有 Scene、Asset、Render、Editor document 或 `EngineHost`。坐标首切片决策见
[ADR 0050](adr/0050-jolt-physics3d-floating-origin.md)，产品 owner/Editor Play 边界见
[ADR 0054](adr/0054-gameplay3d-scene-runtime.md)。

## 当前 API

- `PhysicsWorld3D::Create(config)`：owner-thread、move-only；一个 fixed-step world，不产生 worker。
- `createBody/destroyBody/bodyState`：Static/Kinematic/Dynamic；每个 body 一个居中、不可变的 Box/Sphere/Capsule。
  Capsule 沿 local Y，`halfHeightMeters` 不含端部半球；单位 quaternion，质量范围 `0.001..1e6 kg`。
- `createCharacter/setCharacterInput/characterState`：Y-up capsule controller，输入的水平速度保持到替换，jump 只消费一次；
  ground state/normal/body 与 controller proxy 的 generation-safe body ID 一同返回。
- `setTransform/setLinearVelocity/addLinearImpulse/setAwake`：step 之间立即执行，不隐藏第二套 deferred command 队列。
  静态 body 不接受速度/唤醒，impulse 仅接受 Dynamic；Kinematic 不等同于碰撞求解式角色控制器。
- `castRayClosest/castShapeClosest`：完整 displacement，返回 `Result<optional<PhysicsRayHit3D>>`，无命中不是错误。
- `queryAabb`：保守 broadphase 候选，按公开 body index 排序，caller-owned output，统计完整数量与 overflow。
  两种查询都支持 Static/Moving/Sensor 和忽略单个 body；stale/跨 world 的忽略句柄明确失败。
- `readContactEvents(output)`：读取 Enter/Stay/Exit。event 持有 generation-safe 历史 identity；Exit 可指向已销毁 body，
  fixed-capacity 队列溢出以 `droppedCount` 显式报告，消费过的 event 才移出队列。
- `origin/toGlobalPosition/toLocalPosition/shiftOrigin`：显式 global/local 坐标转换与 rebase。

## 浮动原点

全局位置是 double，物理、Scene/Render 的当前位置保持原点附近的 float。不要先把全球坐标转成 Vec3 再减原点。

```cpp
#include <tina/physics3d/PhysicsWorld3D.hpp>

Tina::Core::Status createAndRebase()
{
    namespace Physics = Tina::Physics3D;
    auto physics = Physics::PhysicsWorld3D::Create({
        .initialOriginMeters = {1000000000.0, 0.0, 0.0}});
    if (!physics) { return Tina::Core::failure(std::move(physics.error())); }

    auto local = physics->toLocalPosition({1000008192.125, 4.0, 0.0});
    if (!local) { return Tina::Core::failure(std::move(local.error())); }
    auto body = physics->createBody({
        .type = Physics::PhysicsBodyType3D::Dynamic,
        .positionMeters = *local});
    if (!body) { return Tina::Core::failure(std::move(body.error())); }

    auto shifted = physics->shiftOrigin({8192.0F, 0.0F, 0.0F});
    if (!shifted) { return Tina::Core::failure(std::move(shifted.error())); }
    // Body local X is now 0.125; global X remains 1000008192.125.
    // Publish Scene/camera changes in the same safe phase before extraction.
    return physics->shutdown();
}
```

`shiftOrigin(offset)` 的规则是 `local' = local - offset`、`origin' = origin + offset`。建议由游戏在相机
越过例如 `1024 m` 的区域阈值后，按固定网格选 offset；模块不擅自选择相机或每帧 rebase。`1e6 m` 局部上限仅是
输入保护，接近上限时 float 仍会损失厘米级细节，rebase 不能恢复进入模块前已经丢掉的精度。

全部刚体（含 static/sleeping/sensor）先预检查后平移，成功只增加一次 origin revision；零位移不变更。
不重建 body、不换 ID、不改变旋转/速度或 awake 状态。为使用真实公开 Jolt API，contact cache 会失效，
尚未睡眠的 body 的 sleep-test timer 会重新开始；已经 sleeping 的 body 不会被 rebase 唤醒。
非法位移、任一 body 超出局部范围、global origin 越界或 double 吞掉非零位移时，全部 body 和 revision 保持原值。
float 平移遵循正常浮点舍入，不承诺任意小数往返逐位相等。

旧 `bodyState` / ray hit 属于其 `originRevision`，不能与新原点混用。一次正常游戏帧的顺序应是：

```text
Runtime fixed tick(s) -> physics.step()
  -> game 决定是否 shiftOrigin()
  -> 重新读取物理 bodyState
  -> game 同步 Scene root、相机、灯光、插值端点与其他 local-space 缓存
  -> updateWorldTransforms -> RenderScene extraction
```

物理绑定的 Scene 对象直接使用新的 bodyState；未绑定对象/独立相机由游戏减同一 offset。带 parent 的 Scene
层级只移动根一次，不再逐级移动 child，否则会重复平移。这里没有自动跨 Physics/Scene/Navigation 事务；当前导航
体积和其路径也不会自动 rebase，调用方必须维护各自坐标转换。持久化存 double/global 身份，不存运行时 body ID。

## 容量与失败

默认 body/pair/contact capacity 为 `1024/4096/2048`，hard limit 为 `65536/1048576/65536`；
fixed delta 为 `1/60 s`，允许 `1e-6..0.1 s`，collisionSteps 为 `1..16`。每轴 local 输入限制 `1e6 m`，
global 值限制 `1e12 m`；geometry extent 为 `0.001..10000 m`。形状只验证当前 kind 使用的字段。
Velocity 和单次 impulse magnitude 当前分别限制 `500 m/s`、`500 rad/s`、`500 N s`。

Tina registry、rebase/query scratch 一次分配；Jolt shape/body 与 `TempAllocatorMalloc` 的求解临时分配不是
PMR、不是全局零分配保证，也不把进程级 allocator exhaustion 声称为可恢复事务。求解预算失败不是可撤销编辑：
`step()` 返回 BackendFailure 并隔离 faulted world，后续只允许 `stats()` / `shutdown()`。

成功 shutdown 立即释放 world、registry、scratch；owner-thread 幂等。Move 转移到执行 move 的线程；析构也必须在
该 owner 线程发生。多个 Tina world 共享的仅是 Jolt 必需的私有类型注册 lease，最后一个 world 销毁时注销。
Jolt 版本、编译 feature ABI 或外部已占用的 factory 不匹配时 Create 失败。

## 构建与验证

```powershell
cmake --preset windows-msvc-vnext-physics3d
cmake --build out/build/windows-msvc-vnext-physics3d --config Debug `
  --target tina_physics3d_tests --parallel 2 -- /nr:false
```

该图关闭 Editor、tools、samples、bgfx 与 shaderc，仅使用 Jolt/gtest feature。只构建库时 target 换成
`tina_physics3d`；CMake 导出/安装会按 feature 加入 `Tina::GameSDK`，Jolt 是私有静态链接依赖，安装消费者仍需
同版本/feature 的 `Jolt::Jolt` package。源码与消息为 ASCII，项目 MSVC `/utf-8` 保持不变，中文文档为 UTF-8。

只有明确授权测试后才运行：

```powershell
out/build/windows-msvc-vnext-physics3d/bin/Debug/tina_physics3d_tests.exe `
  --gtest_filter=PhysicsWorld3DTests.*:FloatingOrigin3DTests.*
```

既有 rigid-body/floating-origin 测试源码与交接证据见 [本轮交接](physics3d-handoff-2026-09-06.md)。本轮扩展了
Character/contact/shape-cast 和 Scene bridge，但尚未执行统一 GoogleTest、sample、smoke、installed consumer、Linux
或性能 gate，不能以历史首切片结果覆盖当前源码。Joints、compound/mesh shape、CCD、移动平台速度传递、
跨 CPU 确定性与性能预算仍不在当前契约。
