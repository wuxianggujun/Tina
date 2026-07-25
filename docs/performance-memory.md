# 性能与内存

本文区分“已有可验证机制”和“尚未接受的 benchmark protocol”。绝对毫秒预算只有在固定 workload、
固定机器与兼容 fingerprint 上才有意义；当前没有正式 `tina_bench` 或 hard-gate baseline。

## 当前内存基础

Core 已实现：

| 类型 | 当前契约 |
| --- | --- |
| `MemoryTracker` | 固定原子计数数组，按 `MemoryTag` 记录 current/peak/alloc/free/failure/invalid deallocation |
| `CountingMemoryResource` | 只统计显式经过该 PMR 的分配，不替换全局 new/delete |
| `FrameArena` | Create 时一次性取得 backing block，线性分配、容量失败、alignment/high-water/epoch/reset，无 heap fallback |
| `GenerationPool` | 固定 slot block、owner + generation、stale/cross-pool 拒绝、wrap retire |

Scene、UI、RenderScene、Asset、Task、Audio、Physics2D 等模块也使用固定容量 storage/queue/ledger；它们
各自报告容量和资源状态，不由一个“全局 allocator”代替所有账本。

当前没有 Engine-owned `MemorySystem` 聚合器、逐 pointer callstack tracker、guard page 或全局 leak map。
`CountingMemoryResource` 结果只能命名为 Tina-routed allocations，不能冒充 process heap/RSS/GPU memory。

## 借用与 reset

- FrameArena reset 前必须结束全部借用和可能访问它的 Task；
- 非平凡对象必须在 reset 前显式析构；
- RenderScene/UI committed view 当前由固定 builder storage 借出，到下一次对应 commit/builder 析构失效；
- `RenderFrame` 的 Scene/UI/Glyph view 只在 `submitFrame()` 调用内有效；
- AssetLease、UploadTicket、GpuTextureId/GpuMeshId、Audio voice 是不同账本，不能互相替代；
- backend 必须在 `submitFrame()` 内同步消费 frame borrow；RenderFramePacket/FramePin 在 present-return
  关闭 CPU 所有权。Texture/Mesh 必须通过 `retire*` 的独立 readback marker pin；其他 GPU 类型在没有
  对应 backend retirement 契约前仍不得借用 CPU ticket 提前释放 Asset。

容量不足必须返回 `CapacityExceeded`/模块错误，不越界、不覆盖 in-flight 数据、不静默切换系统 heap。

## 当前性能证据

已有证据主要是正确性和工作量计数，不是固定机器的绝对性能结论：

- Null/Platform/Desktop/2D/3D 的固定帧 sample 与结构化 checksum/count；
- RenderScene 的 culling/sort/batch、UI dirty/layout/DisplayList、Tile chunk dirty cache；
- Asset queue/upload/retirement 与资源归零；
- `tina_physics2d_bench` 的单线程 step/ray p50/p95/p99 JSON。

`tina_physics2d_bench` 明确不是 ADR 0018 的统一 `tina_bench` schema，不能把它扩写成全引擎 benchmark
protocol。共享 CI 上的墙钟差异默认 informational；确定性失败（checksum、容量、stale、资源不归零）
可以直接阻断。

## 测量原则

正式性能结论至少固定：

- Git commit/dirty 状态、preset/config/compiler/linker/vcpkg baseline；
- OS、CPU、GPU/driver、RAM、电源计划、affinity 与 worker count；
- workload/version/参数/random seed/warm-up/sample count；
- VSync/pacing、debugger、thermal/background load；
- checksum/invariant，防止“少做工作”看起来更快。

原始时间使用整数 ns、内存使用整数 byte。CPU active、barrier wait、present wait 与 GPU timestamp 分开；
无校准 GPU timestamp 时只能标记 informational。p50/p95/p99 在预分配 sample buffer 中离线计算，测量区
不为指标记录引入额外 heap。

## 候选 workload

以下是 `PERF-001` 需要选择和版本化的候选，不是当前通过的硬门禁：

- Null Runtime 长跑；
- Entity/Transform 层级传播；
- RenderScene 大量 Sprite/Mesh extraction、sort、cull、batch；
- TileMap scroll/dirty chunk；
- UI 静态与局部 dirty、Glyph atlas、route/hit；
- Asset completion/upload/backpressure；
- Audio realtime mix；
- Physics2D stack/query。

每个 workload 必须同时输出工作量 counter 和 checksum。Visual sample 验证画面，benchmark 验证时间/
资源，两者不能互相替代。

## ADR 0018 状态

[ADR 0018](adr/0018-benchmark-protocol.md) 已为 `Accepted`（schema v1 首切片）。独立可执行
`tina_bench`（`tools/bench`，随 `TINA_BUILD_EXAMPLES` 或 `TINA_BUILD_BENCHMARKS` 构建）输出
版本化 JSON：`schema`、workload id/version/seed/parameters、build/host fingerprint、counters
checksum、frame-time p50/p95/p99。首个 workload 为 `null_runtime_frames`（Headless+Null+DisabledTask）。

共享开发机/CI 结论固定为 `conclusion=provisional`，**不是** hard gate。固定门禁机、多进程
median/MAD 与 baseline 仓库审核仍为后续扩展；在此之前不得把本机毫秒数写成跨机器回归通过。

```powershell
cmake --build --preset windows-vnext-bgfx-debug --target tina_bench -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --warmup=60 --samples=600 --seed=1
```

## 验证工具

- GoogleTest：分配次数、容量、overflow、stale、shutdown、资源归零；
- sample JSON：真实产品工作量和生命周期；
- Visual capture：画面/布局，不证明 CPU/GPU 性能；
- ASan/UBSan/LSan、VS Profiler/ETW、Linux perf/heaptrack：交叉验证；
- Tracy：未来可选定位工具，不是当前公共 API，也不作为 hard baseline。

完整门禁映射见 [测试说明](testing.md)，任务见 [Backlog](backlog.md)。
