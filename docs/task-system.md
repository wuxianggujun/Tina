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
    bool disableCpuWorkers = false;
};

class ITaskSystem {
    Status scheduleIo(TaskCallable work);
    Status scheduleCpu(TaskCallable work);
    Status postMain(TaskCallable work);
    Result<u32> pumpMain(u32 budget = 0);
    TaskFailureStats failureStats() const noexcept;
    void requestStop() noexcept;
    [[nodiscard]] Core::Status shutdownAndJoinFor(Core::Duration deadline) noexcept;
    void shutdownAndJoin() noexcept;
};
```

`shutdownAndJoinFor()` 的 deadline 必须 finite 且大于0。非法值返回 `TaskErrorCode::InvalidArgument`，
不触发 stop；有效值进入 stopping 并等待 Worker 退出。deadline 到期返回 `TaskErrorCode::WaitTimeout`，
对象仍保持 stopping，线程与队列 ownership 不变，调用方可在任务放行后重试。成功后 join/clear，重复调用
仍成功。`shutdownAndJoin()` 保留给明确允许无界等待的调用方；`EngineHost` 使用有界接口。

`cpuWorkerCount=0` 在直接工厂和 Desktop 中都选择交互默认 `max(1, hardware_concurrency-1)`；
`disableCpuWorkers=true` 才明确禁用 CPU pool，此时 `scheduleCpu()` 返回 `NotSupported`。
显式非零配置保持不变；工厂已删除 IO 16 / CPU 32 的任意数量上限，不再把合法的高核心数自动结果拒绝。
IO worker 必须非零，启用执行域必须有非零队列容量；实际线程/内存创建失败结构化返回并 join 已启动的 worker。
这不是强行创建线程必成功的承诺，也不把显式值悄悄 clamp。见 [ADR 0065](adr/0065-demand-grown-runtime-owners.md)。

`TaskGroup` 当前是 CPU domain 的最小结构化封装：`add()`、`pending()`、`failedCount()`、`isIdle()`、`waitIdle()`、
`waitIdleFor()`；析构等待 pending work，不 detach。TaskSystem 进入 stopping 后仍 drain 已接受的 worker
任务，因此 stopping 不是 group 完成条件：`waitIdle()` 只在 `pending==0` 时返回成功，`waitIdleFor()`
在 deadline 内仍有任务时返回 `WaitTimeout`。

`idle/pending==0` 只证明工作及用户捕获清理结束，不证明业务成功。`failureStats()` 返回累计
`ioFailureCount/cpuFailureCount/mainFailureCount`（u64 原子计数），TaskGroup 的 `failedCount()` 单独统计其
callable 异常；失败报告不分配错误队列。group wrapper 计数后重新抛给 worker 边界，因此同一失败在 group 与
system 两个维度各记录一次，不能相加当作不同任务；group idle 可能先于 worker catch，系统统计在系统 idle/join 后对账。
业务返回的失败值若没有抛异常，不由此计数，详细业务结果仍由上层保存。

`pumpMain(budget=0)` 消费到队列为空，不是入口 snapshot；需要帧成本上限时显式传 budget。
Main callable 抛异常返回 `TaskErrorCode::CallableFailed`，当前 work 消费一次，后续队列保留供下次 pump。
Main 执行中也计 active；正常执行及 shutdown 丢弃 work 都在队列锁外销毁捕获，再减少 active，避免 cleanup 查询 idle 死锁或早报。

## 执行域

| Domain | 当前默认 | 允许 | 禁止 |
| --- | --- | --- | --- |
| Main | 1 | Frame、World/UI commit、GPU submit、completion pump | 无界阻塞 IO、跨 phase 裸等待 |
| IO | 1 | 有界阻塞文件读 | UI/World/Render mutation、长 CPU decode |
| CPU | 工厂与 Desktop：0 选择交互默认；显式 opt-out 可禁用 | 纯 CPU decode/culling/simulation chunk | 阻塞 IO、窗口、UI、bgfx |
| Audio callback | backend-owned | 固定命令消费与 mix | 分配、锁等待、Task wait、格式化日志 |

IO 与 CPU 队列分离，避免慢磁盘占满 CPU worker。IO-only 图显式调用
`createBoundedTaskSystem({.disableCpuWorkers=true})`；不要继续用零 worker 参数表达禁用。

## Trace 定位

Bounded backend 使用 backend-neutral `TINA_TRACE_ZONE` 标注 `Task.IOWorker.Lifetime`、
`Task.CPUWorker.Lifetime`、`Task.Worker.Execute`、`Task.Main.Pump` 与 `Task.Main.Execute`。worker lifetime
zone 表达线程角色和完整存活区间；每个出队的 IO/CPU work 以及每个由 `pumpMain()` 执行的 Main work
都有独立 execution zone。名称均为静态 literal，不从 callable、队列或线程动态拼接，也不引入
Tina-owned TLS 或 TaskSystem 全局状态。None backend 下这些标注完全编译消失；Tracy Profile backend
仅用于定位，不能替代 benchmark 回归协议。

## 不变量

- 不 detach、不强杀 Worker；停止使用 stop request + join；
- Worker 不直接修改 World、UI tree、RenderDevice 或 GLFW；
- Task 捕获不可变值、句柄或 generation ID，不捕获未受 TaskGroup 保护的裸 `this`；
- completion 只在 owner thread 提交，且重新检查 owner/generation；
- Main completion 不等待产生它的 Worker，避免环形等待；
- 队列满返回结构化错误，不复制到无界 side list；
- Worker 入口不让异常逃出线程，每域失败计数可查询；异常捕获后仍 drain 已接受的工作；
- 关闭时先停止新任务，再取消/排空上层 owner，最后由组合根调用 `shutdownAndJoinFor()`；timeout 不释放
  TaskSystem、Worker 或仍可能被访问的 owner。

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
Accepting -> StopRequested/Draining -> Joined
                    | deadline
                    +-> StopRequested (WaitTimeout, ownership retained, retryable)
```

非法 deadline 保持 `Accepting`；成功进入 `Joined` 后重复关闭仍返回成功。timeout 不是可继续析构状态，
也不会隐式 detach 或强杀 Worker。

不要从该 TaskSystem 自己的 callable 或捕获析构中调用 shutdown/join（会等待自身）；最终关闭不能与 Main pump
并发。上层组合根停止派发后再 join。可在捕获析构中查询 idle 或尝试 post，停止后的 post 明确失败。

当前 `EngineHost` 关闭顺序：

1. 停止帧 dispatch，abandon packet，向候选与所有 committed State scope 请求取消；
2. 所有 scope join，再执行 TaskSystem worker drain/join；
3. 销毁候选、State onExit/销毁、Application onShutdown；
4. Runtime 私有 UI owner 与 Platform dispatcher 关闭；
5. Audio → Render → TaskSystem destruction → Platform → Clock → Diagnostics。

`EngineConfig::shutdownDeadline` 是单次停止尝试中 scope/TaskSystem join 与 Audio 实时 reader 关闭的共享预算
（[ADR 0057](adr/0057-retryable-audio-shutdown.md)）；它不能抢占任意用户回调或 Render backend 调用。
`StateTaskScope::requestCancellation()` 不等待，generation 只失效一次；
`cancelAndJoinFor(0)` 可非阻塞探测。Worker 不可等待 owner-thread completion 才退出。

Host timeout 返回 `ShutdownDeadlineExceeded` 且 `isStopping()==true`；调用方保留 Host/Application，并在 owner
thread 重试 `stop(app)`，期间不继续帧 dispatch。所有 State/backend 在 join 前保留，退出回调在成功后执行一次。
只有无恢复 owner 的析构/Create 回滚仍 fail-stop；无 detach/强杀，见 [ADR 0053](adr/0053-retryable-host-shutdown.md)。

## 测试

`tina_tests` 当前覆盖 Disabled/Bounded 的 invalid、idle、queued drain、blocked timeout 状态保留与 retry、
重复成功，以及 `EngineHost` 启动/运行/切换 timeout owner 保留、共享 deadline、重试与析构硬边界。
Runtime/Asset tests 继续覆盖 IO completion、generation 迟到结果和 retirement。至少直接运行：

```powershell
cmake --build --preset windows-vnext-debug --target tina_tests tina_asset_tests --parallel 1 -- /nr:false
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_asset_tests.exe --gtest_color=yes
```

IO-only 的 `NotSupported` 用例应显式设置 `disableCpuWorkers=true`；
自动值覆盖 0/1/2/32/33/64/128；新增 IO 17 / CPU 33、异常计数、Main 剩余队列与捕获回收顺序的回归源码。
本轮未执行测试；实际编译状态见 [实施记录](capacity-and-lifetime-2026-09-13.md)。

## 性能与待冻结项

正式 benchmark 协议由 [ADR 0018](adr/0018-benchmark-protocol.md)（Accepted）约束；PERF-001 已落地
`tina_bench` schema v1 + `null_runtime_frames`，共享机仅 provisional。固定机 hard gate 与多进程
MAD 由 [PERF-002](backlog.md) 继续跟踪。模块级 `tina_physics2d_bench` 或局部耗时仍不可当作跨机器基线。

已落实：Desktop 交互 CPU worker 默认 `max(1, hw-1)`（TASK-001），以及
RUNTIME-SHUTDOWN-DEADLINE hard deadline。仍待冻结：各队列容量、Task capture size、phase barrier soft
deadline、Background 饥饿策略。work stealing/fiber
只有 profile 证明共享队列是瓶颈后才另建 ADR。
