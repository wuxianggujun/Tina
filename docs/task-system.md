# Task System 与线程生命周期

`tina_task` 提供有界 IO、CPU、Main completion 执行域和结构化 `TaskGroup`。它不认识 Asset、Scene、
Render 或 UI 对象；上层通过 `TaskCallable` 提交工作，并在 completion 端重新校验 generation/owner。

## 当前公共契约

```cpp
struct TaskSystemCreateParams {
    u32 ioWorkerCount = 1;
    u32 cpuWorkerCount = 0;
    usize ioQueueCapacity = 64;
    usize cpuQueueCapacity = 64;
    usize mainQueueCapacity = 64;
};

class ITaskSystem {
    Status scheduleIo(TaskCallable work);
    Status scheduleCpu(TaskCallable work);
    Status postMain(TaskCallable work);
    Result<u32> pumpMain(u32 budget = 0);
    void requestStop() noexcept;
    void shutdownAndJoin() noexcept;
};
```

`cpuWorkerCount=0` 明确表示 CPU pool disabled，`scheduleCpu()` 返回 `NotSupported`。这是当前实现事实，
但与 Accepted [ADR 0017](adr/0017-bounded-task-system.md) 的“交互默认保留一个硬件线程”不一致；
该决策由 [TASK-001](backlog.md) 解决，不能靠文档把默认0写成已符合 ADR。

`TaskGroup` 当前是 CPU domain 的最小结构化封装：`add()`、`pending()`、`isIdle()`、`waitIdle()`、
`waitIdleFor()`；析构等待 pending work，不 detach。

## 执行域

| Domain | 当前默认 | 允许 | 禁止 |
| --- | --- | --- | --- |
| Main | 1 | Frame、World/UI commit、GPU submit、completion pump | 无界阻塞 IO、跨 phase 裸等待 |
| IO | 1 | 有界阻塞文件读 | UI/World/Render mutation、长 CPU decode |
| CPU | 0（可配置） | 纯 CPU decode/culling/simulation chunk | 阻塞 IO、窗口、UI、bgfx |
| Audio callback | backend-owned | 固定命令消费与 mix | 分配、锁等待、Task wait、格式化日志 |

IO 与 CPU 队列分离，避免慢磁盘占满 CPU worker。普通 preset 的 IO-only 图保持 `cpuWorkerCount=0`；
Desktop 交互默认的 CPU worker 行为待 TASK-001 落定。

## 不变量

- 不 detach、不强杀 Worker；停止使用 stop request + join；
- Worker 不直接修改 World、UI tree、RenderDevice 或 GLFW；
- Task 捕获不可变值、句柄或 generation ID，不捕获未受 TaskGroup 保护的裸 `this`；
- completion 只在 owner thread 提交，且重新检查 owner/generation；
- Main completion 不等待产生它的 Worker，避免环形等待；
- 队列满返回结构化错误，不复制到无界 side list；
- Worker 入口捕获异常并转为失败状态，不让异常逃出线程；
- 关闭时先停止新任务，再取消/排空上层 owner，最后 `shutdownAndJoin()`。

## 队列与背压

每个队列都有固定 capacity，并应记录 current/peak/submitted/completed/rejected。满队策略：

- IO/CPU：返回 `QueueFull`，调用方在当前 phase 明确降级或失败；
- Asset：slot 保留 pending 状态和 owning result，不创建无界等待容器；
- Main：completion 必须保留可重试的 owning state，不能丢 payload；
- shutdown 后所有 schedule/post 立即返回 stopping 错误；
- Audio command queue 只允许丢弃明确标记为可丢的非关键更新。

当前实现优先保证 bounded queue、异常安全和关闭可验证性；priority、fiber、work stealing、动态 DAG
不属于现行 API。

## Runtime 集成顺序

```text
Platform poll/dispatch
  -> UI route + ActionMapper
  -> fixedUpdate (必要时显式 CPU TaskGroup barrier)
  -> updateFrame
  -> Asset/Audio Main completion pump
  -> RenderScene extraction (当前为主线程确定性路径)
  -> updateUI/layout/DisplayList
  -> GPU upload/submit/present
  -> deferred retirement
```

并行 chunk 即使未来启用，也必须按稳定 chunk/index 合并，不能用 Worker 完成先后决定 Entity、RenderItem
或事件顺序。Main thread 不能对不受当前 phase 限制的 Task 做通用 wait。

## 生命周期与关闭

```text
Accepting -> Draining -> StopRequested -> Joined
```

当前 `EngineHost` 关闭顺序：

1. 停止 GameApplication/State 产生新任务；
2. State 自己停止 UI/Scene/Asset/Physics/miniaudio device 等产品 owner；
3. Runtime 关闭私有 UI owner 与 Platform dispatcher；
4. `AudioEngine::shutdown()`；
5. `RenderDevice::shutdown()`；
6. `TaskSystem::shutdownAndJoin()`；
7. Platform → Clock → Diagnostics。

当前尚无通用 State TaskGroup barrier、soft/hard deadline 或 CrashContext fast-fail protocol。未来实现时
必须保证 Worker join 前不 reset Arena/释放 owner；该工作属于 RUNTIME-001/PERF-001，而不是现有接口。

## 测试

`tina_tests` 当前覆盖 BoundedTaskSystem、DisabledTaskSystem、队列满、stop/join、异常和 TaskGroup；
Runtime/Asset tests 覆盖 IO completion、generation 迟到结果和 retirement。新增 Task 行为至少直接运行：

```powershell
cmake --build --preset windows-vnext-debug --target tina_tests tina_asset_tests -- /m:1 /v:m
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_asset_tests.exe --gtest_color=yes
```

TASK-001 完成前不要把 `cpuWorkerCount=0` 的 `NotSupported` 测试删除；它是当前配置语义，不是错误测试。

## 性能与待冻结项

正式 benchmark 仍由 [ADR 0018](adr/0018-benchmark-protocol.md) 提案和 [PERF-001](backlog.md) 跟踪。
在 `tina_bench` 落地前，只记录模块级 `tina_physics2d_bench` 或局部耗时，不把它们当作跨机器基线。

待冻结：CPU worker 默认、各队列容量、Task capture size、phase barrier soft deadline、shutdown hard
deadline、Background 饥饿策略。work stealing/fiber 只有 profile 证明共享队列是瓶颈后才另建 ADR。
