# ADR 0049：独立 AI 决策层

- 状态：Accepted
- 日期：2026-09-06
- 决策依据：用户确认独立 typed Blackboard / BehaviorTree / AI FSM 的完整实现，不放入 Navigation2D、不复用 Runtime State stack。

## 决定

`Tina::AI` 只依赖 Core + Math，错误域为 `AI = 22`；不修改另一条并行开发的 Navigation3D lane（其错误域为 21）。
实例由玩法 owner 持有，显式传入 delta，不创建线程/时钟/Singleton，不取得 Scene、Asset、Physics 或引擎帧循环。

- Blackboard 是固定容量的 O(1) typed slot table；首写锁定类型，清空值不改变类型。只支持标量与 Vec2/Vec3，不开放 any、裸对象指针或隐式服务查找。
- BehaviorTree 在 Create 时深拷贝并校验单根无环、无共享子节点的拓扑；每实例保存显式执行栈。Sequence/Selector 为 memory 语义，Running 从当前叶子恢复，终态直到 reset 才重跑。
- 每次 tree tick 有 node budget，包括父节点的结果传播；预算耗尽保留准确游标，不重复已完成的 side effect。Condition 不得返回 Running。
- Action 提供可选 noexcept halt callback，cancel/reset/fault/destructor 对活跃 action 至多调用一次；callback userdata 由调用方拥有，必须长于 tree。teardown 不解引用 Blackboard。
- FSM 独立定义 enter/update/exit；每 tick 可按显式 transition budget 有界级联转换，预算耗尽后保留当前状态。enter/update 失败进入 Faulted，已执行的退出或 callback 副作用不伪装成事务回滚。FSM 的 Blackboard 必须由 owner 保证长于 FSM；析构不会访问已解绑的 Blackboard。
- 拒绝重入；callback 返回错误或抛出异常有结构化结果。存储使用稳定 PMR owner，Create 后 dispatch/move 不分配；不移动 MSVC Debug 的 PMR vector 来实现 owner move。

## 代价与边界

无 reactive selector、并行节点、编辑器、行为图序列化或游戏脚本；这些不是首切片的空壳 API。
Blackboard / tree / FSM 不可并发 mutation，不允许在 dispatch 中销毁或移动当前 owner。
导航通过用户 callback 或 Gameplay2D binding 消费，决策模块本身不依赖任何导航维度。
