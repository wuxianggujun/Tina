# 3D 动画图（Animation3D）

`Tina::Animation3D`（`include/tina/animation3d`）在 `Scene::Animator3D` **旁边**建立姿势图：
crossfade、状态机、blend tree、layer + mask、root motion 与两骨 IK。决策理由见
[ADR 0037](adr/0037-animation3d-graph-boundaries.md)。

它链接 Core + Math + AssetFormat + Scene + Gameplay，**不链接 Asset**（只消费 payload view，
绝不持有 lease）与 **不链接 Render**（skinning palette 写进调用方给的 float span）。

## 与 `Animator3D` 的关系

`Scene::Animator3D` 一字未改，仍是「一副骨架 + 一个 clip」的求值器，仍有产品消费面
（`samples/3d_product`）。新模块**不替代它**，因为它有三处结构性阻碍无法作为上层基础：

| 阻碍 | 后果 |
| --- | --- |
| `evaluatePose` 私有，`update(delta)` 是唯一驱动 | 无法「不推进播放状态地采样」，而 crossfade 正是同一 clip 采两个时间 |
| 输出只写自己的成员缓冲 | 无法把 pose 取到调用方存储做混合 |
| `setPlaybackSpeed` 要求 `> 0` | 倒放需另 cook 反向 clip |

因此 `ClipSampler3D` 重新实现了采样（`sample()` 是 **const**）。两份采样实现并存是有意的。

## 分层

```text
Skeleton3D (bind pose + 层级 + inverse bind + 骨骼名称)
Pose3D     (joint-local TRS 数组)          JointMask (256 位位集)
        │
PoseBlend3D  blendOverwrite / blendAdditive / blendPair / normalizeRotations
        │
ClipSampler3D  采样 + 三种播放模式的 playhead 运算（无状态）
        │
BlendTree3D  Clip / Blend2 / Blend1D / Additive
        │
AnimationGraph3D  state machine + crossfade + layer/mask + root motion
        │
IkSolver3D  两骨解析解
```

## 骨骼名称（SkinnedMesh v2）

`SkinnedMeshWire::SchemaVersion` 现为 **2**，追加逐 joint 64 字节 UTF-8 名称块。

**为什么必须有：** cooker 按 `(depth, sourceIndex)` 对 joint 重排以保证 parent 先于 child，因此
**cooked index 是消费者无法反推的排列**。mask、retarget 映射、IK goal 若按 index 书写，源文件一改
就静默指向另一根骨头。v2 之前 `GltfCook.cpp` 对 `cgltf_node::name` 的读取次数是 0。

- 空名称合法（glTF 节点不必命名），`findJoint("")` 永不匹配；
- 重名在 encode / parse / cook **三处**拒绝 —— glTF 允许重名，故 cook 拒绝是承重的；
- 不合成 `joint_7`：生成名对消费者与授权名不可区分，会在承诺「按名字」的接口下重新引入 index 依赖。

## Pose 是 joint-local 的

混合两个 **global** pose 会拉伸肢体：子关节的世界位置独立于父关节插值时不保骨长。global 只在最后
`composeSkinningMatrices` 派生一次，输出即 Render skinned palette 期望的列主序布局。

`Pose3D` 复用 `Scene::LocalTransform`，不声明第三份 TRS。

## 混合

```cpp
blendOverwrite(destination, source, alpha, mask);            // layer 算子
blendAdditive(destination, source, reference, alpha, mask);   // reference 无默认
blendPair(output, from, to, alpha);                           // blend tree 节点算子
```

**旋转永远走 slerp。** 逐分量四元数插值便宜、看起来差不多对，然后在相距接近 180° 处塌向零长度、
关节瞬跳。重复 slerp 累积长度漂移，故 graph 在 root 处统一 normalize 一次。

**alpha 越界一律 clamp**，非有限值按 0 处理（保留 destination）：调用方权重算错不应该连带毁掉正在
混进去的那个 pose。

**additive 的 reference 没有默认值。** 这是 additive 最常见的缺陷来源：clip 按自身首帧授权时，
减 bind pose 会把每个偏移**翻倍**。graph 与 blend tree 各自在 Create 时解析一次自己的 reference，
绝不每帧采样。

## mask

```cpp
JointMask{}                                  // 默认 = 全部 joint
skeleton.resolveMask(names, /*descendants*/ true);   // "上半身" = 某骨骼及其全部子孙
```

- **空 mask 表示「全部」，不是「无」**：没要求 mask 的调用方想要整副骨架动；「什么都不动」用
  weight 0 表达。
- **未知骨骼名直接失败**（`UnknownJointName`），不跳过：静默丢弃解析不到的骨头会产生「处处略有
  不对」的动画，而成因通常是 mask 按另一副 rig 授权的。
- **基础层（layer 0）不可 mask**：被排除的 joint 会持有 pose 缓冲上一帧的残留，而「未定义」在这里
  看起来就是卡帧。

## 状态机与 crossfade

```cpp
auto graph = AnimationGraph3D::Create(skeleton, clips, blendTrees, config);
const LayerId base = graph->baseLayer();
auto idle = graph->addState(base, StateDesc{.clipIndex = 0});
auto run  = graph->addState(base, StateDesc{.clipIndex = 1});
graph->addTransition(base, TransitionDesc{.from = *idle, .to = *run,
                                          .duration = Core::Duration{0.2}});
graph->setState(base, *idle);          // 硬切
graph->requestTransition(base, *run);  // 按授权 transition crossfade
graph->crossfadeTo(base, *run, Core::Duration{0.3});  // 代码驱动，忽略授权

// 每帧：advance 必须在 evaluate 之前，否则渲染晚一帧
graph->advance(delta);
graph->evaluate(pose);
graph->writeSkinningMatrices(palette);
```

- **crossfade 期间两侧都推进**：淡出的 state 若停止推进会冻在半步上，混合会明显抖。
- **缺失 transition 直接拒绝**（`InvalidTransition`），不回落默认时长：缺 transition 是授权缺口，
  静默默认会把它藏到「动画只是有点生硬」为止。
- **进行中的 transition 被打断由 `canInterrupt` 决定**：一律丢弃会让输入像被忽略，一律叠加会让
  pose 累积。两者都错，所以逐 transition 表达。
- `advance` 与 `evaluate` 分开，因为离屏角色可以只走时钟不求值。**两者都不分配**（由
  `AdvanceAndEvaluateAreAllocationFree` 用 `CountingMemoryResource` 固定）。

## root motion

```cpp
config.rootMotion = RootMotionConfig{.enabled = true, .rootJoint = 0,
                                     .applyTranslationXZ = true, .applyRotation = true};
...
const RootMotionDelta3D delta = graph->rootMotion();  // 交给 controller 施加到实体
```

**delta 会从 pose 中移除。** 既留在 pose 又上报 delta，角色会移动**两次** —— 而这种倍速看起来像
调参问题而不是 bug。垂直位移默认不取（重力与跳跃弧线属于 controller）。

`ClipPlayhead3D::cyclesCompleted` 上报一次 advance 跨越的整周期数：root motion 按周期累积，只由
`(previous, current)` 算 delta 会在跨过循环点时丢掉整整一个周期的位移 —— 低帧率下就是常态，表现
为角色走得比动画短。

**注意：Loop clip 推进到恰好等于 duration 会回绕到时间 0。** 想采「最后一帧」要停在 duration 之前。

## 两骨 IK

```cpp
solveTwoBoneIk(skeleton, TwoBoneIkDesc{
    .rootJoint = hip, .middleJoint = knee, .tipJoint = foot,
    .targetPosition = groundHit,
    .poleTargetPosition = kneeForward, .usePoleTarget = true,
    .weight = ikBlend,
}, pose);
```

**解析解，不迭代**：两骨链有闭式解（余弦定理），CCD/FABRIK 只是逼近同一答案，还引入一个「预算
耗尽前不可见」的迭代次数。

- **超出可达范围时指向目标，绝不拉伸骨头**：骨长由骨架固定，拉长的肢体比差一点没够到更糟。
- **默认不完全伸直**（`maximumReachFraction = 0.999`）：恰好满伸时余弦解的方向数值不稳定，目标悬在
  最大可达处时关节会抖。
- **pole target 决定弯曲平面**，不给则沿用当前弯曲。默认用世界轴会在腿摆过该轴时让膝盖瞬间反向 ——
  IK 最易被识别的缺陷。
- **`weight` 会 slerp 混合**，这是脚落到不平地面时「贴上去」而不是「弹上去」的原因。
- 忽略 scale：缩放链没有良定义的骨长（写在代码注释里，不半支持）。
- 链的父子关系被**校验**而非信任：索引搞错仍会产生一个 pose，只是错的，下游无从分辨。

## 当前限制

- **没有 retargeting**。v2 名称解除了阻塞，但需要 humanoid 映射与 T-pose 归一化，属独立切片。
- 没有 morph target / blend shape（`AnimationChannel` 无 weights 枚举值，cook 也拒绝）。
- 没有 2D blend space、sync group、animation event / marker。
- IK 只有两骨链；没有 look-at、没有 FABRIK 长链、没有关节角度限制。
- **pose-aware bounds 仍缺失**：extraction 用授权 `localBounds` 剔除，大幅位移或 IK 会 pop。
- 尚无 sample 消费面（`samples/3d_product` 仍用 `Animator3D`）。
