# ADR 0037：`Tina::Animation3D` 动画图边界，与 SkinnedMesh v2 骨骼名称

- 状态：Accepted
- 日期：2026-08-31
- 决策者：Tina maintainers

## 背景

本 ADR 之前，3D 动画只有 `Scene::Animator3D`：**一副骨架 + 一个 clip** 的 CPU 求值器。全仓搜索
确认 `crossfade`、`blend tree`、`state machine`、`root motion`、`retarget`、`IK`、`animation layer`
在 `include/` 与 `src/` **零命中**。

`Animator3D` 无法作为上层的基础，有三处结构性阻碍：

| 阻碍 | 位置 | 后果 |
| --- | --- | --- |
| `evaluatePose` 是私有的，`update(delta)` 是唯一驱动 | `Animator3D.hpp:45` | 无法在不推进播放状态的前提下采样，而 crossfade 正是「同一 clip 采两个时间」 |
| 输出只写自己的成员缓冲 | `Animator3D.hpp:104-108` | 无法把 pose 取到调用方存储里做混合 |
| `setPlaybackSpeed` 要求 `> 0` | `Animator3D.cpp:439` | 状态机倒放 clip 需要另 cook 一份反向 clip |
| `m_globalMatrices` 计算但不公开 | `Animator3D.hpp:106` | IK 需要全局空间，拿不到 |

更根本的是**骨骼没有名称**。`SkinnedMeshWire` v1 有 `parentJoint` 与 bind TRS，但没有 name 字段、
没有字符串表；`GltfCook.cpp` 对 `cgltf_node::name` 的读取次数是 **0**。而 cooker 按
`(depth, sourceIndex)` 对 joint 重排以保证 parent 先于 child（`GltfCook.cpp:2113-2147`），因此
**cooked index 是一个消费者无法反推的排列**。bone mask、retarget 映射、IK goal 全都无 key 可依。

参考实现调研：Unity 有 Animator/StateMachine/BlendTree/Avatar Mask/Humanoid retarget；Godot 有
`AnimationTree` 与 `AnimationNodeBlendTree`；cocos2d-x 3D 侧较弱。按 `docs/carbon-reference.md`
的规则，三者作为取舍来源而非移植源。

## 决策记录

| # | 决策点 | 采纳 | 主要备选与取舍 |
| --- | --- | --- | --- |
| D1 | 是否动 wire schema | **SkinnedMesh v1→v2，追加 `jointCount * 64` 字节名称块** | 保持 v1、mask 只用 index：index 是 cooker 派生的排列，源文件一改就静默指向另一根骨头。ADR 0034 的「已发布无生产者」缺陷正是这类默认值问题的镜像 |
| D2 | 名称块是否条件化 | **无条件，不设 header flag** | flag 化会让布局依赖某个字段，而本 payload 刻意「全部 offset 仅由 count 推导」，writer 与 parser 因此不可能不一致。代价是 256 joint 时 16 KiB，相对 MB 级顶点块可忽略 |
| D3 | 空名称是否合法 | **合法**；重名在 encode/parse/cook **三处**拒绝 | 合成 `joint_7`：生成名与授权名对消费者不可区分，会在承诺「按名字」的接口下重新引入 index 依赖。glTF 允许重名，故 cook 拒绝是承重的 |
| D4 | 新模块还是扩 Scene | **独立 `Tina::Animation3D`** | 扩 `Tina::Scene`：Scene 是并行会话的所属文件，且动画图需要 `Tina::Gameplay` 的 `Easing`，让 Scene 依赖 Gameplay 会倒转依赖方向 |
| D5 | Pose 空间 | **joint-local** | global space：混合两个 global pose 会拉伸肢体 —— 子关节的世界位置独立于父关节插值时不保长度。global 只在最后 `composeSkinningMatrices` 派生一次 |
| D6 | pose 复用 `Scene::LocalTransform` | **复用** | 新声明第三份 TRS：违反 ADR 0035 的唯一定义点精神，且 root motion 本来就要把 pose 送进 Scene 层级 |
| D7 | 失败表达 | **`Result`/`Status`，占 `ErrorDomain::Animation3D = 18`** | 照 ADR 0035 用 `optional`：这里的失败要区分 `SkeletonMismatch`、`UnknownJointName`、`EvaluationFailed`，单个 bool 无法承载 |
| D8 | mask 存储 | **index 位集，名称在 build 时解析一次** | 存字符串：要么每帧重解析，要么仍缓存 index；per-joint 循环里做字符串比较的成本在 200 骨骼的 rig 上才暴露 |
| D9 | 空 mask 语义 | **「全部 joint」** | 「无 joint」：没要求 mask 的调用方想要的是整副骨架动起来；「什么都不动」用 weight 0 表达 |
| D10 | 基础层能否被 mask | **不能** | 允许：被排除的 joint 会持有 pose 缓冲上一帧的残留值，而「未定义」在这里等于「看起来像卡帧」 |
| D11 | additive 的参考姿势 | **显式参数，无默认** | 默认 bind pose：clip 按自身首帧授权时会把每个偏移**翻倍**，这是 additive 最常见的缺陷 |
| D12 | 参考姿势何时求值 | **Create 时一次** | 每帧采样：参考是 bind pose 或 clip 首帧，都不会变；每帧求值等于每帧分配去算一个常量 |
| D13 | 缺失 transition | **拒绝，返回 `InvalidTransition`** | 回落到默认时长：缺 transition 是授权缺口，静默默认会把它藏到「动画只是有点生硬」为止 |
| D14 | 进行中 transition 被打断 | **由 `canInterrupt` 显式决定** | 一律丢弃（输入像被忽略）或一律叠加（pose 累积）。两者都错，所以逐 transition 表达 |
| D15 | root motion 是否留在 pose | **移除，并单独上报 delta** | 两者都留：角色会**移动两次**，而这种倍速看起来像调参问题而不是 bug |
| D16 | IK 算法 | **两骨解析解（余弦定理）** | CCD/FABRIK：两骨链有闭式解，迭代法只是逼近同一答案，还引入一个「预算耗尽前不可见」的迭代次数 |
| D17 | IK 弯曲方向 | **可选 pole target，默认沿用当前弯曲平面** | 默认用世界轴：腿摆过该轴时膝盖会瞬间反向 —— IK 最易被识别的缺陷 |
| D18 | 是否做 retargeting | **不做，记录理由** | 做：需要 humanoid rig 映射与 T-pose 归一化，是独立于本切片的一整块。v2 名称已解除阻塞，但没有消费者的功能就是 ADR 0035 D3 拒绝的那种 |
| D19 | 句柄类型 | **index + owner token，不用 `GenerationId`** | `GenerationId`：只能由 `GenerationPool` 铸造，而 state/layer 从不擦除，pool 的 free list 与逐槽 generation 是死重 |

## 决定

`include/tina/animation3d` 是 3D 姿势求值、混合、状态机与 IK 的定义点。它链接
Core + Math + AssetFormat + Scene + Gameplay，**不链接 Asset**（只消费 payload view，绝不持有
lease；资产生命周期属于 AssetSystem 的所有者，与 ADR 0031 为 Scene 划的边界一致）与
**不链接 Render**（skinning palette 写进调用方提供的 float span，Render 的 item 类型不出现在此）。

`SkinnedMeshWire::SchemaVersion` 提升到 **2**，追加逐 joint 64 字节 UTF-8 名称块。

### 1. `Animator3D` 不被替代，也不被迁移

`src/scene/Animator3D.cpp` 一字未改。它仍是单 clip 求值器，仍有 5 个测试固定其契约。新模块建在
它**旁边**而非之上：`ClipSampler3D` 重新实现了采样，因为 `Animator3D` 的采样是私有且自带播放状态的
（见背景表）。两者并存是有意的 —— 已有产品消费面（`samples/3d_product`）不受影响。

### 2. 每帧零分配，由测试固定

`AnimationGraph3D::advance` + `evaluate` 不分配。所有 scratch pose、blend tree 的逐节点输出缓冲、
additive 参考姿势都在 Create 时分配完毕。`AdvanceAndEvaluateAreAllocationFree` 用
`CountingMemoryResource` 在 crossfade（最重路径：采样两个 state 再混合）上跑 20 帧比对分配计数。

### 3. 旋转永远走 slerp

逐分量四元数插值便宜、看起来「差不多对」，然后在相距接近 180° 处塌向零长度、关节瞬跳。
`RotationBlendStaysUnitLengthNearOppositeOrientations` 固定这一点（测 170° 处仍为单位长度）。
重复 slerp 会累积长度漂移，故 graph 在 root 处统一 `normalizeRotations` 一次，而不是要求每个算子
承诺输出单位四元数。

### 4. 余量与积压：方向相反且刻意

与 [ADR 0036](0036-gameplay-tooling-boundaries.md) 第 2 节同源。sequence 子节点边界的余量**必须
携带**；而 `ClipPlayhead3D::cyclesCompleted` 上报一次 advance 跨越的整周期数，因为 root motion 按
周期累积 —— 只由 `(previous, current)` 算 delta 会在任何一次 advance 跨过循环点时丢掉整整一个周期
的位移，而这在低帧率下就是常态，表现为角色「走得比动画短」。

## 结果

- 首次存在：`Skeleton3D`/`Pose3D`/`JointMask`、`PoseBlend3D`（overwrite/additive/pair）、
  `ClipSampler3D`（三种播放模式 + 负速度）、`BlendTree3D`（Clip/Blend2/Blend1D/Additive）、
  `AnimationGraph3D`（state machine + crossfade + layer/mask + root motion）、`solveTwoBoneIk`。
- 占 `ErrorDomain::Animation3D = 18`、`MemoryTag::Animation3D = 15`（`MemoryTagCount` 15→16）。
- 成本与限制：
  - **没有 retargeting**（D18）。v2 名称解除了阻塞，但需要 humanoid 映射与 T-pose 归一化。
  - **没有 morph target / blend shape**：`AnimationChannel` 无 weights 枚举值，cook 也拒绝。
  - **IK 只有两骨链**，且忽略 scale（缩放链无良定义骨长；已在代码注释中写明而非半支持）。
  - **`Blend1D` 无 2D blend space**、无 sync group、无 animation event/marker。
  - **`Animator3D` 与 `ClipSampler3D` 两份采样实现并存**（第 1 节），有意如此。
  - **pose-aware bounds 仍缺失**：extraction 用授权 `localBounds` 剔除，大幅位移或 IK 会导致 pop。
- 已建立的门禁：`tina_animation3d_tests` 28/28（含 7 个 header-isolation 单 TU）；
  `tina_asset_format_tests` 126/126 与 `tina_asset_tests` 325/325 覆盖 v2 名称的 round-trip、
  重名拒绝、cooker 跨重排保名与 malformed corpus；`tina_tests` 455/455、`tina_scene_tests` 181/181
  无回归。

## 被拒绝方案

- **扩展 `Animator3D` 使其支持多 clip**：其三处结构限制（背景表）都在公共契约上，放开等于重写它，
  而它有 5 个固定契约的测试与一个产品消费面。并存成本低于原地改写风险。
- **复用 UI keyframe timeline（ADR 0026）**：其属性集是 `UIAnimatableProperty` 的九个 UI 属性、
  容量按窗口分配、publication 走 Layout/Hit/Paint 事务。驱动一根骨头需要把 UI 的 presentation
  owner 变成通用 pose owner。
- **在 wire 里存 skeleton 而非 per-mesh 内嵌**：`AssetKind` 无 `Skeleton`，新增资产种类是独立切片；
  当前 clip 与 mesh 的唯一兼容信号仍是 `jointCount` 相等，本 ADR 未改这一点，只是把它显式校验。
- **让 mask 存名称并每帧解析**：见 D8。
- **移植 Unity Mecanim 的 Humanoid 抽象**：需要一整套 rig 归一化契约，且会把「所有角色共享一副
  抽象骨架」的假设烧进引擎。retarget 留待有真实消费者时另立 ADR。
