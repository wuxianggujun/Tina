# ADR 0054: Gameplay3D Scene Runtime 与隔离 Editor Play

Status: Accepted

## Context

Prefab 过去只能实例化为 Scene entity 并参与 rendering。3D animation、Physics3D、Character input、camera 和
resource lifetime 因而由每个产品临时编排；Editor Play 又需要保证 Stop 后不写回 authoring document。仅把
PhysicsWorld3D 放进 EngineHost 会让 Runtime 同时拥有游戏内容、Scene、Asset 与 Editor 生命周期，破坏既有的
`IGameState` 产品组合边界。保留 Prefab v4 或 AnimationClip v1 的双读路径则会让 skeleton identity、event 和
physics payload 是否存在变成运行时猜测。

## Decision

- 新增 `Tina::Gameplay3D`。`Scene3DRuntime` 由产品 State 拥有，绑定一个已经实例化的 Scene World、当前 Prefab、
  AssetSystem 和可选 PhysicsWorld3D；它不拥有 RenderDevice、EngineHost 或 Editor document。
- `build()` 先验证 stable-node/entity 对应、已驻留 typed asset、SkinnedMesh/AnimationClip skeleton signature、
  fixed delta 与 Physics3D bridge 的全部创建，全部成功后才发布 runtime owner。runtime 保活 AssetLease，并为未绑定
  clip 的 skinned mesh 写 bind palette。
- fixed step 的顺序是 player input -> Scene-to-kinematic sync -> Physics step -> dynamic/Character local TRS 回写 ->
  Animator3D update。Physics 和 animation event 都是 runtime-owned fixed-capacity缓冲的 borrow，下一 step 或
  shutdown 即失效；simulation failure 使 owner faulted，调用方销毁并重建，不能消费半步状态。
- Prefab 升级到唯一 schema v5，在 node 中写入 optional Physics3D 与 Animation3D 描述；AnimationClip3D 升级到唯一
  schema v2，写入 canonical skeleton signature 和 notify event。旧 schema 一律 `UnsupportedSchema`，没有 reader
  fallback、compatibility alias 或包装层。
- Editor Play 只复制 canonical Prefab bytes 到隔离 World，复用 Scene3DRuntime；authored active Camera3D 在 Play
  期间是唯一游戏视图，Editor navigation 不覆盖它。Pause/focus loss 清空 player input/jump latch；Stop 先释放
  runtime/physics/world，再从未经修改的 authoring bytes 重建 preview。

## Consequences

- 同一 cooked Prefab 在独立 `tina_sample_3d_authored_level` 和 TinaEditor Play 经过相同 animation、physics、
  camera、AssetLease 路径；产品仍明确拥有 World/Asset/Physics，EngineHost 不产生新的 service locator。
- `Scene3DPhysicsBridge` 只接受可表达为 TRS 的层级和单位 world scale；Dynamic/Character 的物理位姿是权威，
  Kinematic 从 Scene 推送。移动平台速度、joint、compound/mesh shape、CCD、跨 CPU 确定性和性能预算不在本决定内。
- AnimationGraph3D 保持独立 pose-graph 契约；本决定不为它强加 blend/transition event 语义。ClipSampler3D 和
  Animator3D 可报告直接 clip 的 notify event，图事件需要另一个明确的语义与 API 决定。

## Verification

完成本实现后集中验证：Gameplay3D public header isolation、AssetFormat/Scene/Physics3D/Editor 定向 executable、
独立 authored-level sample 与 TinaEditor World3D Play 短 smoke、installed consumer；随后人工验证 Character input、
authored Camera 与 Stop 不污染 document。历史 static 3D/Physics 首切片门禁不作为此决定的证据。
