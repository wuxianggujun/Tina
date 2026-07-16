# Task System 与线程生命周期

> 状态：vNext 设计讨论稿。首期目标是可预测、有界和可关闭，不建设通用任务图框架。

## 目标边界

`tina_task` 负责 CPU Worker、阻塞 IO Worker、主线程 Completion、协作取消、TaskGroup 和
诊断。它不认识 Asset、Scene、Render 或 UI 类型；上层只提交名称、优先级、取消状态和
可调用任务。

首期不实现：

- 动态 DAG/依赖图；
- fiber/coroutine scheduler；
- 无界任务队列；
- 强杀线程；
- 任意线程直接调用 bgfx、GLFW、UI tree 或 World mutation；
- detached task 持有 Engine 对象。

## Executor 划分

| Executor | 线程 | 允许工作 | 禁止工作 |
| --- | --- | --- | --- |
| Main Thread | 1 | Frame Pipeline、World commit、UI、GPU upload/submit | 帧中无界等待、阻塞文件 IO |
| CPU Worker | 配置值，默认保留1个硬件线程给主线程 | decode、culling、独立 simulation chunk、纯 CPU cooker | 阻塞 IO、Window/UI/bgfx 调用 |
| IO Worker | 默认1，最多按配置增加 | 阻塞文件读取、Cooker 输入 | 长 CPU 计算、GPU upload |
| Audio Callback | miniaudio 后端线程 | 消费固定命令、混音所需最小工作 | 分配、锁等待、日志格式化、Task wait |

CPU Worker 首期使用有界共享队列和条件变量建立正确基线。只有基准显示锁竞争成为 p99
瓶颈后，才评估 per-worker deque/work stealing。IO 与 CPU 分池，避免慢磁盘占满计算线程。

## 核心接口草案

```cpp
enum class TaskPriority : std::uint8_t { High, Normal, Background };

struct TaskContext {
    StopToken stop;
    std::uint32_t workerIndex;
};

struct TaskDesc {
    StringId name;
    TaskPriority priority;
    StopToken stop;
    InlineFunction<void(TaskContext&), kTaskInlineBytes> function;
};

class TaskGroup final {
public:
    void requestStop() noexcept;
    [[nodiscard]] bool isComplete() const noexcept;
};

class TaskSystem final {
public:
    Core::Result<TaskHandle> schedule(TaskGroup& group, TaskDesc task);
    Core::Result<TaskHandle> scheduleIo(TaskGroup& group, TaskDesc task);
    Core::Status postMainThread(MainThreadTask task);
    Core::Status waitAtBarrier(TaskGroup& group, Deadline deadline);
};
```

`TaskHandle` 是可查询/请求取消的 generation handle，不拥有 Worker，也不允许 detach；TaskGroup
才是结构化并发的 owner。TaskGroup 析构前必须达到 complete，Debug 未完成时 fail-fast；
Release 也不能静默分离任务。单个 Task 完成只写一次 terminal 状态 Success/Failed/Cancelled，
重复取消幂等。MainThread completion queue 满时，Worker 的 owning result 保留在对应 owner
slot 并标记 `completionPending`，由后续预算重试；不能丢 payload，也不能泄漏到无界 side list。

`kTaskInlineBytes` 必须由实际 Task capture 分布决定，不在设计文档猜一个永久常量。首期收集
callable size histogram；超过容量的任务改为显式 payload handle，不允许 InlineFunction
自动 heap fallback。

## 生命周期与捕获规则

- Task 只捕获不可变值、小型句柄或 generation ID；
- 捕获裸 `this` 时，所属对象必须拥有 TaskGroup，并在 `onExit` 释放任何被 Task 访问的成员前
  完成 requestStop + barrier；仅在析构前等待还不够；
- Asset/Entity/Node/Render handle 在执行和 completion 两端都重新校验 generation；
- Worker 结果写入私有 output，主线程 completion 在唯一 phase 提交；
- Task 不直接修改 World、UI tree 或 RenderDevice；
- MainThread completion 不得等待产生它的 Worker；
- Worker 不得等待必须由 MainThread completion 才能结束的 Task，避免环形死锁；
- 每个 Task 入口捕获异常，转换为任务失败；异常不能逃出线程入口。

## 有界队列与背压

每个队列配置容量，并统计 current、peak、submitted、completed、cancelled、rejected、wait
time。队列满时：

- Runtime 高优先级任务返回 `QueueFull`，调用方在当前 phase 明确降级或失败；
- Asset/IO Background 任务只在 Asset slot 保存一个 pending bit 和 owning payload handle，由
  每帧有界扫描重试；不得把“仍 Queued”的项目复制到无界等待容器；
- Audio command 使用固定 SPSC queue，满时只允许丢弃被声明为可丢的非关键参数更新；
- shutdown 后所有 schedule 立即返回 `SchedulerStopping`。

首期只支持 High/Normal/Background 三档，不承诺严格实时优先级。Background 必须有饥饿
保护；每轮调度至少在预算允许时推进一个等待最久的任务。

## Frame Phase 集成

```text
Platform/Input
  -> Event
  -> Asset CPU Completion (MainThread queue budget)
  -> Fixed Simulation
       schedule parallel pure jobs
       waitAtBarrier
       commit World commands
  -> Frame Update (variable delta)
  -> State Transition Commit (no worker wait)
  -> Render Scene Extraction
       schedule independent extraction chunks when worthwhile
       waitAtBarrier
       merge deterministic outputs
  -> UI
  -> GPU Upload (MainThread budget)
  -> Render/Present
  -> Deferred Cleanup
```

Task 调度不能改变确定性提交顺序。并行 chunk 使用稳定 index，合并按 chunk/index 排序；
禁止用 Worker 完成先后决定 Entity、RenderItem 或 Event 顺序。

Main Thread 每帧不做通用 `wait()`。只有 Fixed Simulation 和 Render Scene Extraction 的显式 barrier
可以等待当前 phase 的有限 TaskGroup，并记录等待时间。超过 deadline 时生成诊断；不能
让 Worker 在后台继续写已经进入下一阶段或已 reset 的 FrameArena。deadline 超时后 Engine
立即进入 fatal-stop，保留 Arena 和 owner 对象、请求协作取消并继续等待 join；绝不 reset
或开始下一 phase。硬 shutdown deadline 后仍无法 join 时只能写 CrashContext 并 fast-fail，
不能执行看似“受控”但会释放活线程数据的普通析构。

## Shutdown 状态机

```text
Accepting
  -> Draining
  -> StopRequested
  -> Joined
```

Engine 关闭顺序：

1. 停止 `IGameApplication` 和 `IGameState` 产生新任务；
2. Scene/UI 请求停止并完成自己拥有的 TaskGroup；
3. Asset 停止接收、取消 generation，等待 IO/CPU job 结束，但仍保留已被 Audio 引用的资源；
4. Audio 停止 callback/command producer，并释放 Asset handle；
5. Asset 丢弃迟到 completion/upload，Render drain deferred GPU retire 并关闭设备；Audio
   backend 完成最终销毁；
6. TaskSystem 停止接收，request stop，join IO/CPU Worker；
7. Platform 销毁窗口/backend；
8. MemorySystem 检查所有 tag 并销毁，Diagnostics 最后关闭。

任何 Worker 超过 shutdown deadline 都是明确错误，输出 Task name、运行时间、线程和 owner
group；不调用强杀线程。Debug 可终止以暴露生命周期错误；发布版若无法 join 也必须
fast-fail，不能继续销毁 Worker 仍可能访问的 Engine 内存。

## 测试与性能门禁

GoogleTest 覆盖：

- schedule、执行、TaskGroup completion 和取消；
- QueueFull、shutdown 后拒绝、Background 饥饿保护；
- Task 自取消、owner 先销毁、generation 迟到 completion；
- Worker 异常转换、重复 requestStop/join；
- MainThread/Worker 环形等待检测；
- barrier 后 FrameArena 可安全 reset；
- 单 Worker 模式与多 Worker 输出顺序一致；
- IO 阻塞不占满 CPU Worker。

`tina_bench` 记录空任务成本、不同 payload/capture、queue contention、1/2/4/8 Worker scaling、
barrier wait p95/p99 和每秒任务吞吐。未证明共享队列成为瓶颈前，不实施 work stealing；未
证明 Task 数量足够大前，不把简单 for-loop 强行并行化。Benchmark CLI 必须显式写入 worker
数，不能把 `hardware_concurrency()-1` 当作可比较配置；超过物理核心的矩阵项跳过并记录原因。
每个结果同时记录 worker stack bytes、queue capacity、`sizeof(TaskItem)`、总预留字节、queue
residency p99、reject rate、Background 最大等待时间和 barrier wait，避免只用吞吐掩盖内存
与饥饿问题。

## 仍需冻结

1. CPU/IO/Main completion queue 首期容量和每 Worker stack bytes；
2. Fixed Simulation 允许并行的首批系统与 chunk size；
3. 各 workload 的 barrier soft deadline 与 shutdown hard deadline 数值；
4. 实际 capture histogram 得到的 `InlineFunction` callable 容量；
5. Background 最大 queue residency 与饥饿门禁。

Worker 默认与异常边界是 Proposed；deadline 后不得继续/reset/普通析构是安全硬约束。Benchmark 仍需
显式写 worker 数，不能使用运行时默认值作为可比较配置。
