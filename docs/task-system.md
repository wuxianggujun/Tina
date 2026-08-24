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
    [[nodiscard]] Core::Status shutdownAndJoinFor(Core::Duration deadline) noexcept;
    void shutdownAndJoin() noexcept;
};
```

`shutdownAndJoinFor()` 的 deadline 必须 finite 且大于0。非法值返回 `TaskErrorCode::InvalidArgument`，
不触发 stop；有效值进入 stopping 并等待 Worker 退出。deadline 到期返回 `TaskErrorCode::WaitTimeout`，
对象仍保持 stopping，线程与队列 ownership 不变，调用方可在任务放行后重试。成功后 join/clear，重复调用
仍成功。`shutdownAndJoin()` 保留给明确允许无界等待的调用方；`EngineHost` 使用有界接口。

`cpuWorkerCount=0` 在 `createBoundedTaskSystem` / 直接工厂参数中仍表示 CPU pool disabled，
`scheduleCpu()` 返回 `NotSupported`（IO-only 图与单测继续可用）。产品 `Desktop::CreateEngine` 通过
`resolveDesktopTaskSystemParams` 把 0 解析为 `max(1, hardware_concurrency-1)`，符合
Accepted [ADR 0017](adr/0017-bounded-task-system.md)。

`TaskGroup` 当前是 CPU domain 的最小结构化封装：`add()`、`pending()`、`isIdle()`、`waitIdle()`、
`waitIdleFor()`；析构等待 pending work，不 detach。

## 执行域

| Domain | 当前默认 | 允许 | 禁止 |
| --- | --- | --- | --- |
| Main | 1 | Frame、World/UI commit、GPU submit、completion pump | 无界阻塞 IO、跨 phase 裸等待 |
| IO | 1 | 有界阻塞文件读 | UI/World/Render mutation、长 CPU decode |
| CPU | Desktop 交互：`max(1, hw-1)`；工厂默认 0 可配置 | 纯 CPU decode/culling/simulation chunk | 阻塞 IO、窗口、UI、bgfx |
| Audio callback | backend-owned | 固定命令消费与 mix | 分配、锁等待、Task wait、格式化日志 |

IO 与 CPU 队列分离，避免慢磁盘占满 CPU worker。普通 preset 的 IO-only 图可直接
`createBoundedTaskSystem({.cpuWorkerCount=0})`；产品 Desktop 走交互默认。

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
- Worker 入口捕获异常并转为失败状态，不让异常逃出线程；
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

当前 `EngineHost` 关闭顺序：

1. 停止 GameApplication/State 产生新任务；
2. State 自己停止 UI/Scene/Asset/Physics/miniaudio device 等产品 owner；
3. Runtime 关闭私有 UI owner 与 Platform dispatcher；
4. `AudioEngine::shutdown()`；
5. `RenderDevice::shutdown()`；
6. `TaskSystem::shutdownAndJoinFor(EngineConfig::shutdownDeadline)`；
7. Platform → Clock → Diagnostics。

`EngineConfig::shutdownDeadline` 只从第6步的 Worker-exit/join 等待开始计时，不覆盖 AudioEngine、
RenderDevice 或整个 Host shutdown 的总耗时。

TaskSystem timeout 后 `EngineHost` 先通过仍存活的 Diagnostics 写入 `runtime.lifecycle` 错误，再
`std::terminate()`；不会 reset TaskSystem，也不会继续析构 Platform、Clock、Diagnostics 等剩余 owner。
产品入口若已显式安装 Core CrashHandler，该分支会补充首份 best-effort 文本报告；`EngineHost` 不负责安装。
当前仍无通用 State TaskGroup soft deadline、CrashContext 或 minidump protocol。

## 测试

`tina_tests` 当前覆盖 Disabled/Bounded 的 invalid、idle、queued drain、blocked timeout 状态保留与 retry、
重复成功，以及 `EngineHost` 配置 deadline 透传和 timeout death path。
Runtime/Asset tests 继续覆盖 IO completion、generation 迟到结果和 retirement。至少直接运行：

```powershell
cmake --build --preset windows-vnext-debug --target tina_tests tina_asset_tests --parallel 1 -- /nr:false
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_asset_tests.exe --gtest_color=yes
```

`cpuWorkerCount=0` 的 `NotSupported` 测试保留：它描述直接工厂的 IO-only 语义，不是错误测试。

## 性能与待冻结项

正式 benchmark 协议由 [ADR 0018](adr/0018-benchmark-protocol.md)（Accepted）约束；PERF-001 已落地
`tina_bench` schema v1 + `null_runtime_frames`，共享机仅 provisional。固定机 hard gate 与多进程
MAD 由 [PERF-002](backlog.md) 继续跟踪。模块级 `tina_physics2d_bench` 或局部耗时仍不可当作跨机器基线。

已落实：Desktop 交互 CPU worker 默认 `max(1, hw-1)`（TASK-001），以及
RUNTIME-SHUTDOWN-DEADLINE hard deadline。仍待冻结：各队列容量、Task capture size、phase barrier soft
deadline、Background 饥饿策略。work stealing/fiber
只有 profile 证明共享队列是瓶颈后才另建 ADR。
