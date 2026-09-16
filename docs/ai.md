# AI 决策层

`Tina::AI` 只依赖 Core + Math，由 gameplay owner 持有 Blackboard、BehaviorTree/FSM 并显式传入 delta/预算。
它不创建线程，不拥有 Scene/Navigation/Runtime State stack。边界见 [ADR 0049](adr/0049-ai-decision-layer.md)，
容量迁移见 [ADR 0065](adr/0065-demand-grown-runtime-owners.md)。安装消费者只链接 `Tina::GameSDK`。

## Blackboard

```cpp
auto blackboard = AI::Blackboard::Create({.initialSlotReserve = 0});
if (!blackboard) { return Core::failure(blackboard.error()); }
const AI::BlackboardKey<Core::u64> targetKey{70000};
if (auto status = blackboard->set(targetKey, Core::u64{42}); !status) { return status; }
```

- 稀疏 PMR typed table，只为实际写过的 key 分配；`initialSlotReserve` 是 hash table 预留提示，不限定 key 或值数量。
- 允许 bool、i64/u64、float/double、Vec2/Vec3；key 的 u32 最大值保留为无效，其他索引不受旧 65536 限制。
- 首次成功写入绑定类型，`clear(key)`/`clearValues()` 只清值、不解除类型绑定；错误不会把旧值或类型抹掉。
- `boundSlotCount()` 表示历史成功绑定的不同 key 数，`valueCount()` 表示当前有值数；旧 `capacity()` 删除。
  反复写同一组 key 复用条目，持续引入新 key 会保留类型元数据，不应把每帧随机 key 当临时变量。
- 查找平均 O(1)，不是严格 O(1) 数组；分配失败返回结构化错误。PMR 必须长于 owner。

## BehaviorTree

Create 深拷贝并校验单根、无环、无共享子节点的拓扑，不再附加 4096 节点上限；仍校验 u32 索引与真实存储范围。
Sequence/Selector 采用 memory 语义，Running 从准确游标恢复。显式栈避免 C++ 深递归，tick 预算包括结果传播；
预算耗尽保留进度，不重复已完成的副作用。成功创建后内部 dispatch/move 不分配，用户回调和 Blackboard 写入不在此保证内。

Condition 不可返回 Running。终态直到 reset 才重跑；cancel/reset/fault/destructor 对活跃 Action 至多 halt 一次。
callback userData 必须覆盖使用期，不开放任意对象查找或隐式服务定位。

## StateMachine

Create 拷贝 states，非空且 initial 在范围内，不再附加 4096 状态上限；每个 state 必须有 tick。
调用方提供 transition budget，状态为 Idle/Running/Succeeded/Failed/Cancelled/Faulted。
enter/tick 失败产生 Faulted，已经发生的回调副作用不伪装成事务回滚。

结束当前执行前先摘除 Blackboard borrow、发布终态，再调用 noexcept exit；析构也持有 dispatch guard。
exit 中对同一 FSM 调用 tick/cancel/reset 返回 `ReentrantDispatch`，不会重复 exit。普通 transition 同样受 guard 保护。
Blackboard 和 userData 必须长于活跃 FSM；禁止在 dispatch 中移动或销毁 owner。

## 验证与边界

测试源码位于 `tests/ai/AITests.cpp`，覆盖稀疏高 key/类型绑定、0 reserve、OOM、sealed-resource move、
8193 节点深树预算恢复、8192 states 与析构重入。构建目标 `tina_ai_tests` 包含 header isolation。
本轮编译/运行状态以 [内存策略](memory-policy.md) 为准，测试源码不等于测试通过。

当前不提供 reactive selector、并行 BT、编辑器行为图、行为图序列化、脚本或多线程 mutation。
真实游戏消费和预算调优仍需产品验收，不能用导航路径测试替代 AI owner 生命周期测试。
