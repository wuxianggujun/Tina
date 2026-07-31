# 性能与内存

本文区分“已有可验证机制”和“尚未建立的固定机 hard gate”。绝对毫秒预算只有在固定 workload、
固定机器与兼容 fingerprint 上才有意义；当前已有 ADR 0018 接受的 `tina_bench` schema v1，
但没有固定机 hard-gate baseline 或多进程 MAD 结论。

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
- UI 50,000 层 retained tree 的非递归 structure/layout/hit/paint stress gate；
- Asset queue/upload/retirement 与资源归零；
- `tina_physics2d_bench` 的单线程 step/ray p50/p95/p99 JSON。

`tina_bench` 当前提供版本化 schema v1 与 `null_runtime_frames` workload；`tina_physics2d_bench` 仍是独立
模块 bench，不能冒充统一 schema。共享 CI 上的墙钟差异默认 informational；确定性失败（checksum、容量、
stale、资源不归零）可以直接阻断。

## UI 演进性能预算

当前 `UIContextCapacityConfig` 已在 Create 时固定 node/root/dirty/layout/hit/paint/canvas/listener/text 等容量，
非零配置在 Context 生命周期中不增长。`UIContextStatistics` 已公开 capacity/high-water、phase dirty、
layout measure/arrange、Hit rebuild、Paint cache/snapshot rebuild 等计数，因此 Component、StyleSheet、
Image/Icon/NineSlice 和 Motion 必须扩展现有统计模型，而不是另建不可观测的 UI 子系统。

需要区分三件事：

- clean `UIContext` commit 当前在 dirty phase 与 viewport 均未变化时直接返回，不执行 Measure/Arrange、Hit、
  Paint 或 Semantics rebuild；这不代表 Render bridge 提交既有 DisplayList 的整条渲染链是 `O(1)`；
- paint-only 局部状态不会使 Layout/Hit dirty，但当前 paint candidate 容量校验与 committed paint 组装仍会
  线性遍历 layout/paint 数据；只能说 dirty cache 重算是局部的，不能把完整 publication 宣称为 `O(D)`；
- Motion 会让 paint commit 连续发生多帧。其 active track sample 必须只遍历 `M` 条 active track，但仍要
  单独测量当前 `O(N + P)` paint publication；若不达预算，先优化 candidate compiler，再提高 Motion 容量。
- Image/Icon/NineSlice 的 committed geometry 可以在 UI clean frame 保持不变，但 packet-local
  `FrameResourceRef/FramePin` 不能跨帧复用；Render bridge 每帧仍按 `O(Q + U + B)` 构建 image commands、
  resolve/pin 唯一资源并合并相邻 batch。该成本必须单列，不能错误归入 UI commit rebuild。

`UI-PERF-001` 的首个 milestone 已建立以下前四条版本化 workload；其余 workload 随对应垂直切片补齐。
在 `PERF-002` 固定门禁机完成前，时间结论保持 `conclusion=provisional`：

| Workload | 固定规模 | 确定性输出/门禁 |
| --- | --- | --- |
| `ui_static_commit_v1` | 4096 committed nodes，warmup 后无 mutation | layout/measured/arranged/hit/paint rebuild 为 0；UI PMR allocation delta、capacity/high-water 与 DisplayList checksum |
| `ui_paint_dirty_v1` | 4096 nodes，每帧修改 1 个 leaf paint state | layout/hit rebuild 为 0；paint cache rebuild、snapshot inspected/published entries、`N/P` 与 checksum |
| `ui_route_v1` | 4096 hit entries、route depth 64；target/miss/capture 固定序列 | visited entries、path depth、listener calls、consume/claim checksum；allocation delta 为 0 |
| `ui_virtual_collection_v1` | 100k logical items、固定 64-row pool/scroll sequence | materialized row/high-water，warmup 后 Tina-routed storage 不增长，selection/semantics checksum |
| `ui_component_build_v1` | 256 个四节点 Component；固定 text/canvas payload 与 Activate/Toggle/Range/TextInput/Scroll/Selection slot mix | build/commit 时间、node/text-byte/canvas/behavior 的 reserved/published counter、各 pool high-water、allocation delta 与 tree checksum；commit 后无 retained wrapper，后续 clean commit rebuild 为 0 |
| `ui_style_state_v1` | 4096 nodes、256 rules、每节点最多 4 classes | resolved/inspected nodes、candidate rules、bucket/dependency high-water；单节点 state change 不全树 resolve |
| `ui_image_nineslice_v1` | 256 Image + 232 Icon + 512 full NineSlice、64 unique `(resolver scope, AssetId)` | `Q=5096`；输出 unique resource `U`、image batch `B`、resolve hit/miss/not-ready、pin/dedupe、command/batch/high-water、allocation delta 与 DisplayList checksum |
| `ui_motion_v1` | 4096 nodes、active track 分别为 0/64/1024、固定 clock | sampled/active/high-water=`M`；0 active 时 motion work/额外 dirty 为 0；layout/hit rebuild 为 0，记录 paint publication |

每项分别记录 UI commit、route、DisplayList build 的 active CPU 时间和工作量，不能只给混合 frame time；
Render submit/present wait 与 GPU timestamp 继续单列。UI benchmark 不能靠减少 entry、跳过 listener 或关闭
原子 publication 获得更快结果，因此每次结果必须同时校验 checksum 与固定参数。

合入上述 UI 能力时同时遵守：Behavior side store 直接索引且无 per-node heap/vtable；Component recipe 只付
创建成本、不留下 retained wrapper；StyleSheet startup precompile 且 local state 只匹配 node-local rule bucket；
Motion 只遍历 active list，`M == 0` 不产生额外 dirty；Image/Icon/NineSlice 不在 UI commit 同步加载 Asset，
资源解析按每帧唯一 `(resolver scope, AssetId)` 去重，展开容量不足时完整失败且不截断。目标复杂度和路线见
[UI 框架设计](ui-framework.md)。

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

以下是 `PERF-002` 需要选择和版本化的候选，不是当前通过的硬门禁：

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
checksum、p50/p95/p99。当前包含 `null_runtime_frames`（Headless+Null+DisabledTask）以及
`ui_static_commit_v1`、`ui_paint_dirty_v1`、`ui_route_v1`、`ui_virtual_collection_v1`。

共享开发机/CI 结论固定为 `conclusion=provisional`，**不是** hard gate。固定门禁机、多进程
median/MAD 与 baseline 仓库审核仍为后续扩展；在此之前不得把本机毫秒数写成跨机器回归通过。

下面是开发机 quick provisional run，不是 ADR 0018 的正式采样规模：

```powershell
cmake --build --preset windows-vnext-bgfx-debug --target tina_bench --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --warmup=60 --samples=600 --seed=1
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_static_commit_v1 --warmup=60 --samples=600 --seed=1
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_paint_dirty_v1 --warmup=60 --samples=600 --seed=1
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_route_v1 --warmup=60 --samples=600 --seed=1
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_virtual_collection_v1 --warmup=60 --samples=600 --seed=1
```

正式候选采样遵循 ADR 0018：每进程 warm-up 600、普通样本至少 2,000；p99/泄漏结论使用 10,000，且需
多进程 median/MAD 和固定 machine profile。当前 UI workload 是 `UI-PERF-001` 首个 milestone，不能把
上面的现有命令描述成已经覆盖 Component/Style/Motion/Image。

## 验证工具

- GoogleTest：分配次数、容量、overflow、stale、shutdown、资源归零；
- sample JSON：真实产品工作量和生命周期；
- Visual capture：画面/布局，不证明 CPU/GPU 性能；
- ASan/UBSan/LSan、VS Profiler/ETW、Linux perf/heaptrack：交叉验证；
- Tracy：未来可选定位工具，不是当前公共 API，也不作为 hard baseline。

完整门禁映射见 [测试说明](testing.md)，任务见 [Backlog](backlog.md)。
