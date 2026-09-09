# ADR 0057：Audio realtime 关闭共享 deadline 并保留 owner 重试

- 状态：Accepted
- 日期：2026-09-09
- 关联：[ADR 0012](0012-miniaudio-backend.md)、[ADR 0053](0053-retryable-host-shutdown.md)

## 背景

`AudioEngine::shutdown()` 会先关闭新的 realtime callback admission，再等待已经进入的 callback block。
原实现用无期限 `yield` 等待；若 backend callback 永久阻塞，`EngineHost::stop()` 也永久阻塞，
`EngineConfig::shutdownDeadline` 无法覆盖完整的异步 owner 收口。

超时后直接清空 voice、PCM 或 stream storage 会让仍在运行的 callback 解引用已释放内存；detach 或强杀
callback 同样不能证明资源寿命结束。

## 决定

1. 新增 owner-thread `AudioEngine::shutdownFor(Duration) -> Status`。首次调用关闭新 realtime admission，
   并等待已进入 reader 到 deadline；无效 deadline 与错误线程明确失败。
2. 超时返回 `AudioErrorCode::ShutdownDeadlineExceeded`，Engine 保持 `Stopping`，保留 voice、PCM、stream
   storage 与 reader 计数。普通 producer/mutation API 在 `Stopping` 返回 `EngineClosed`；`stats()` 与再次
   `shutdownFor()` 可用。
3. callback 退出后，owner 重试 `shutdownFor()`；只有 reader 为零时才清空状态并进入 `Stopped`。
4. `EngineHost::stop()` 将每次尝试的同一剩余 shutdown budget 依次用于 State scope、TaskSystem 与 Audio。
   Audio 超时映射为 `RuntimeErrorCode::ShutdownDeadlineExceeded`，保留 application identity 以及尚未关闭的
   Audio/Render/Platform module owner，以便同一 application 再次调用 `stop()`。
5. Task/State 超时发生在退出回调之前；Audio 超时发生在 State `onExit`、Application `onShutdown` 与 UI
   teardown 之后。重试不得重复退出回调。文档和诊断必须区分这两个阶段。
6. `AudioEngine::shutdown()` 保留为无界硬边界，供已停止 device 的显式关闭和析构使用。错误线程析构或
   Host 在 `Stopping` 状态被销毁仍 fail-stop，不能用提前释放掩盖 callback 生命周期错误。

## 代价

- 调用方必须先停止/detach 外部 audio device，并在 timeout 后保留 Engine/Application owner；
- `Stopping` 成为可观测且只能前进到 `Stopped` 的吸收状态；
- Host 的 Audio timeout 可能发生在游戏退出回调已执行之后，重试路径必须保持 exactly-once。

## 拒绝方案

- timeout 后释放 PCM/stream：仍在执行的 callback 会产生 UAF；
- detach 或强杀 callback：无法证明 native/backend 已停止访问 owner；
- 仅依赖 command/completion ring 有界：队列容量不能约束 callback 执行时间；
- 保持无限等待且只写文档要求先停 device：错误集成会让产品关闭永久卡死且不可诊断。

## 验收

- active reader + 极短 deadline 返回 timeout，voice/stream 仍可观测且 producer 已关闭；
- reader 退出后重试成功、资源计数归零，重复 shutdown 成功；
- invalid deadline / wrong owner thread 不改变状态；
- EngineHost 的 Task deadline 精确传递、timeout owner 保留与 optional Audio 正常关闭回归通过。
