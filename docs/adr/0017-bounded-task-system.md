# ADR 0017：有界结构化 Task System，不 detach/强杀

- 状态：Accepted
- 日期：2026-07-16
- Accepted：2026-07-20（首切片：有界 IO worker + Main completion；M10-A25：可选 CPU worker +
  scheduleCpu + 最小 TaskGroup；priority、fiber/work stealing 仍后置）

## 背景

无界 queue、detached task 和超时后继续析构会把瞬时负载变成内存增长或 UAF。直接先上 fiber、
work stealing/lock-free 结构会在没有基准证据时扩大调试面。

## 推荐决定

首期分 CPU、阻塞 IO、Main completion 执行域，使用有界共享队列、TaskGroup、stop token、
显式 barrier 和每类 QueueFull 策略。交互运行 CPU worker 默认保留一个硬件线程给主线程，IO
默认1；benchmark 必须显式 worker 数。禁止 detach/强杀；barrier 超时后保留 owner/Arena，
进入 fatal-stop，硬 deadline 后 fast-fail。work stealing/fiber 只有 profile 证明需要才另写 ADR。

## 代价

- 调用者必须处理 QueueFull、pending bit 和 TaskGroup 生命周期；
- 首期共享队列的峰值扩展性可能不是最优；
- queue/stack/callable 容量要在首个 workload 前量化。

## 被拒绝方案

- `std::async`/无界线程池：执行与关闭策略不可控；
- 超时 detach 或强杀 Worker：无法保证 Engine 内存安全；
- 未测量先做 fiber/work stealing：复杂度没有收益证据。
