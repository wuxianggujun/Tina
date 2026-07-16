# 性能预算与内存系统

> 状态：vNext 设计讨论稿。预算是首轮工程目标，不代表当前实现已经达到。

## 性能目标

Tina 的“性能优先”必须由相同工作负载、相同工具链和相同硬件上的证据表达。首轮默认
基线为中端桌面、Windows/Linux x64、1080p：

| 指标 | 设计目标 | 硬门禁 |
| --- | --- | --- |
| 帧率 | 120 FPS | 60 FPS |
| Frame interval p99（VSync-off样例） | 8.33 ms | 16.67 ms |
| Main Thread p99 | 不超过 6.5 ms | 不超过 13.5 ms |
| GPU Frame p99 | 不超过 8.0 ms | 有校准 timestamp 时不超过16.0 ms；否则只作 informational |
| Fixed Simulation | 60 Hz | 每个 Render Frame 最多4步 |
| Tina-owned 稳态动态分配 | Fixed Update、Render Scene Extraction、无变化 UI 为0 | 从0变为非0立即阻断 |
| Asset GPU Upload | 默认不超过1.0 ms/帧 | 同时受任务数与字节数限制 |
| UI Layout | dirty 时每窗口每帧最多1次 | 无变化 UI 不得重复布局 |
| UI 增量更新 | 无变化时 Style/Layout/PaintCache rebuild 均为0 | 任一无原因非0立即阻断 |

主线程和 GPU 分别计时，不能相加伪造总帧时间。120 FPS 是架构设计目标；60 FPS 是任何
发布样例不得突破的底线。目标硬件的 CPU、GPU、内存、驱动、电源模式和系统版本必须写入
基准结果，尚未选定固定门禁机前不宣称绝对毫秒预算已经通过。

## 指标精确定义

- `cpu.main_frame_ns`：Platform Poll 前开始，到 Deferred Cleanup 完成后结束；包含 active CPU、
  fixed/render barrier wait、upload、submit 和 present wait；
- `cpu.active_ns`、`cpu.barrier_wait_ns`、`cpu.present_wait_ns` 分开记录，三者不能互相冒充；
- `frame.interval_ns`：相邻两次 frame start 的 wall interval，只用于 pacing；开启 VSync/限帧
  时不得拿它评价 CPU throughput；
- `gpu.frame_ns`：只有 backend 提供校准 GPU timestamp、valid/disjoint 标记和延迟 frame index
  映射时成立；CPU submit 或 bgfx 估算 stats 必须标记 `informational`；
- `fixed.dropped_time_ns`：超过最大4步后丢弃的时间债务，任何非0样本都单独计数；
- 原始时间保存整数纳秒、内存保存整数 byte，显示层才换算 ms/MiB。

“稳态零分配”只指 Tina-owned allocator/FrameArena 在指定热点 phase 的动态分配增量为0。
`CountingMemoryResource` 看不到默认 allocator、第三方、驱动和系统 heap，因此结果不得把
`tina_owned_allocations` 命名为 process heap allocation。进程级分配用 ETW/VS Profiler、
heaptrack 等专用配置定期交叉验证；第三方分配、RSS/Commit、GPU estimated bytes 和资源
handle count 分账报告。

常驻 `MetricsRegistry` 只维护低成本 Counter/Gauge/CurrentPeak/Timer sample：

- 数值类型固定为 u64/i64/f64，单位使用 ns/bytes/count/ratio 枚举；
- descriptor 声明 Frame/Process lifetime 和 MainOnly/Atomic/BarrierReduced 线程模型；
- 同名但 descriptor 不同注册失败；禁止动态 label、实体名、Asset 路径等高基数维度；
- Frame metric 在唯一 capture 点先 snapshot 再 reset；注册只发生在初始化；
- p50/p95/p99 不在常驻 Registry 在线维护，由 `tina_bench` 的预分配 sample buffer 在测量结束
  后计算。

## 初始工作负载

以下是用于暴露架构问题的固定工程场景，不是产品内容上限：

| 场景 | 首轮规模 | 观察点 |
| --- | --- | --- |
| Null Runtime | 300帧及10,000帧长跑 | 生命周期、每帧固定开销、资源计数 |
| Entity/Transform | 100,000 slots、50,000活跃 Transform | generation resolve、层级传播、command commit |
| Render Scene Extraction | 20,000可见 RenderItem | 连续写入、排序、批次、零稳态分配 |
| 2D Sprite | 10,000 Sprite，其中大部分共享 Atlas | 稳定 layer/order、batch/draw/texture switch |
| 2D TileMap | 256x256、Camera 滚动和局部编辑 | chunk culling、dirty rebuild、collision query |
| 3D | 5,000静态实例、基础深度 | bounds/culling、sort、draw/instance count |
| UI 静态/局部 dirty | 5,000逻辑节点 | style/layout/PaintCache dirty 范围、DisplayList/batch |
| UI 虚拟列表 | 100,000行、约100行可见+overscan | 创建/访问节点数、滚动、hit-test visited count |
| Event | 每帧10,000次短事件投递 | queue、订阅解析、回调开销 |
| Asset | 256个完成项积压 | 三重预算、饥饿保护、generation cancel |

场景规模必须可配置，默认值只用于同版本回归。实际游戏样例继续承担画面正确性和交互验收，
不能用合成基准替代。

UI workload 还记录 nodes visible/interactive、dirty measure/arrange/paint/order、layout pass/node、
hit-test count/visited、PaintCache rebuild、DisplayList command/bytes、batch/draw/texture/clip switch、
glyph cache/raster/upload/atlas page，以及 route/layout/paint/display/submit ns。无变化帧允许遍历
可见 PaintCache entry 组装本帧 view，但不能重新 style、layout、shape 或构建 local paint。

## 测量方法

验证分成两类：

1. `tina_tests` 直接运行 GoogleTest，验证确定性契约：分配次数、容量、溢出、阶段顺序、
   stale handle、取消和资源归零；
2. `tina_bench` 是独立 Release 可执行文件，warm-up 后输出 p50/p95/p99、current/peak、
   allocations、工作量、工具链、硬件和 Git 提交；不使用 CTest 调度。

普通共享 CI 不用墙钟微秒值直接判失败，因为虚拟机抖动会产生假回归。当前交互式开发机
只建立 provisional/informational baseline；只有完成稳定性校准的固定门禁机才能阻断绝对
耗时。CPU/GPU/驱动、OS build、BIOS/微码、RAM、编译器、链接器、vcpkg baseline、优化选项、
worker 数或电源计划改变后，旧 baseline 自动失效，不能跨 fingerprint 比较。

每个进程在 warm-up 前执行 preflight：确认没有 debugger、系统未刚从 sleep/resume 返回、固定
电源计划/进程优先级/affinity 已生效、后台 CPU/GPU/IO 负载低于 machine profile 阈值，并记录
单调时钟来源。温度或频率无法稳定时该 run 只能 informational。未提交/dirty build 可以用于
本地 A/B，但不能写入版本化 hard baseline；baseline 必须对应可重建的 commit 和完整依赖/
优化 fingerprint。

基准默认要求：

- Release 优化，关闭调试日志、VSync 和帧率限制，保留 Metrics；Present pacing 使用另一组
  明确开启 VSync 的 workload；
- 固定随机种子、workload version、参数、worker 数和 warm-up 帧数；输出 checksum/invariant，
  防止优化器消除工作或错误实现因为少做工作而“更快”；
- 每个 workload 至少5个独立进程；每进程先 warm-up 600帧，再采样至少2,000帧；正式 p99
  长尾与泄漏门禁采样10,000帧；
- A/B 比较按 ABBA 或等价交错次序运行，避免“所有旧版本先跑、所有新版本后跑”把温升、
  电源和后台负载漂移误当成代码差异；
- quantile 固定使用 nearest-rank；候选结果使用各独立进程 p99 的中位数，不把所有进程样本
  拼成一个伪大样本；
- 初建 hard baseline 使用10个独立进程计算 run-level median 与 MAD；若噪声超过 workload
  允许值，标记机器不稳定而不是生成宽松 baseline；
- 单独记录 CPU phase、GPU、内存和批次数；
- 第一次运行用于 shader/文件缓存预热，不混入稳态结果；
- 测量 sample buffer、结果 descriptor 和 workload 数据在 warm-up 前预分配，测量区不因记录
  指标引入额外 heap；
- 修改优化前后保存同机对比，不用不同电脑的绝对数下结论。

性能结论分三类：

1. 绝对预算：固定门禁机上的 Main/GPU/Frame hard limit；
2. 相对回归：候选 run-level median 与同 fingerprint baseline 比较，差值同时超过该 metric 的
   10% 和 `max(3 × baseline MAD, workload absolute noise floor)` 时阻断；
3. 确定性契约：Tina-owned allocation 从0变非0、checksum 不同、资源不归零、queue overflow
   或 Arena failure 无需墙钟证据，直接阻断。

Cold Asset/Cooker、warm cache、GPU first-use 和 steady-state 是不同 workload，不得把首次
shader/文件缓存成本丢掉后又宣称 cold load 通过。所有无效 GPU timestamp、系统 sleep、
debugger attached、thermal/power 状态变化都写入 validation；无效 run 不参与统计。
除预先定义的 invalid 条件外不事后删除“看起来太慢”的样本；被拒绝 run 保存原因和原始摘要，
避免用手工挑样制造虚假提升。

## `tina_bench` 结果契约

结果采用版本化 JSON，最少包含：

```json
{
  "schema_version": 1,
  "benchmark": {
    "id": "render.extraction",
    "workload_version": 1,
    "parameters": {},
    "seed": 1
  },
  "build": {
    "git_commit": "",
    "dirty": false,
    "preset": "windows-bench",
    "configuration": "Release",
    "compiler": "",
    "linker": "",
    "cxx_standard": 23,
    "vcpkg_baseline": "",
    "dependency_fingerprint": "",
    "optimization_fingerprint": "",
    "trace_backend": "none"
  },
  "host": {
    "machine_profile_id": "",
    "os": "",
    "os_build": "",
    "cpu": "",
    "cpu_microcode": "",
    "bios": "",
    "physical_cores": 0,
    "logical_cores": 0,
    "affinity": "",
    "ram_bytes": 0,
    "gpu": "",
    "gpu_driver": "",
    "power_plan": ""
  },
  "run": {
    "warmup_samples": 600,
    "measured_samples": 10000,
    "process_repeat": 1,
    "worker_count": 1,
    "vsync": false,
    "process_priority": "",
    "timer_source": "steady_clock"
  },
  "metrics": [
    {
      "id": "cpu.main_frame_ns",
      "unit": "ns",
      "valid_samples": 10000,
      "p50": 0,
      "p95": 0,
      "p99": 0,
      "max": 0
    }
  ],
  "counters": {},
  "validation": {
    "passed": true,
    "checksum": "",
    "debugger_attached": false,
    "sleep_resume_detected": false,
    "thermal_stable": true,
    "background_load_valid": true,
    "rejection_reasons": []
  }
}
```

JSON 不记录 hostname、用户名、绝对源码路径或资源正文。临时结果写入被忽略的
`artifacts/bench/<machine-profile>/<commit>/`；经人工确认的固定门禁基线写入版本控制的
`benchmarks/baselines/<machine-profile>.json`。比较器先校验 schema、workload、build/host
fingerprint 和 checksum，再读取指标；不匹配返回 `BaselineIncompatible`，不能强行比较。
每个 metric 的 id、unit、valid sample count 和 quantile 字段固定；缺失/无效 GPU sample 不得
用0填充并参与分位数。聚合 summary 保存独立进程数量、run-level median/MAD 和被拒绝 run
原因，不能只留下一个最终“通过”布尔值。

## Profiler 与 Benchmark 分工

Carbon Core 公开实现没有名为 `TinyProfile` 的模块；它通过 `CcpTelemetry/CcpStatistics` 接入
[Tracy](https://github.com/wolfpld/tracy)。Tina 采用同一原则，但不复制 Carbon 的全局状态 API：

| 层 | 工具 | 职责 | 是否用于硬门禁 |
| --- | --- | --- | --- |
| 常驻指标 | `MetricsRegistry` | frame、queue、memory、resource current/peak | 是，确定性指标 |
| 代码插桩 | `TINA_TRACE_*` | 稳定的 Tina zone/thread/lock/allocation 标记 | 否，只是公共前端 |
| 可视化后端 | Tracy | CPU timeline、线程、锁、内存、上下文切换与可选采样 | 否，用于定位原因 |
| 重复基准 | `tina_bench` | 固定 workload 的 p50/p95/p99 与同机回归 | 是 |
| 平台工具 | VS Profiler/ETW、Linux perf | 无插桩采样、系统级验证 | 用于交叉验证 |

Tracy 是成熟的实时纳秒级 frame profiler，支持 CPU、内存、锁、上下文切换及多种 GPU API；
但启用插桩会改变被测程序的代码和运行成本。因此正式 baseline 默认关闭 Tracy，只保留低
成本 Metrics；发现 `tina_bench` 回退后，再用相同 workload 的 Profile 构建打开 Tracy 查找
热点。还要做一次 Tracy 开/关 A/B，记录 profiler 自身开销。

目标构建配置：

```text
TINA_PROFILE_BACKEND=none   # Debug/Release/正式 benchmark 默认
TINA_PROFILE_BACKEND=tracy  # Release-equivalent optimization + symbols 的 Profile preset
TINA_PROFILE_TRACY_LOCKS=OFF
TINA_PROFILE_TRACY_MEMORY=OFF
```

应用代码只使用 Tina 名称：

```cpp
TINA_TRACE_ZONE("RenderExtraction");
TINA_TRACE_FRAME("MainFrame");
TINA_TRACE_THREAD("Tina.CpuWorker.0");
```

Backend 是 configure/compile-time 选择，不在每个 zone 走虚函数或查询全局 service。
`tina_profile_config` INTERFACE target 只统一传播 Tina backend selection definition；Tracy
自己的 compile definitions 私有留在可选 adapter，
`tina_profile_tracy` 唯一链接 Tracy Client。公共 Engine 接口不暴露 Tracy 类型，也没有运行时
Service Locator。`none` 时宏真正展开为 `(void)0`，参数不求值；热点 zone 名只接受字符串
字面量/稳定静态 ID，禁止帧内拼动态路径、Entity 名或用户文本。

业务 translation unit 不包含 Tracy inline header。启用 backend 后，宏构造 Tina 自有的小型
`TraceZoneToken`，通过 `noexcept` 的 begin/end adapter 传递静态 site（name/file/line）；site
只注册一次，帧内不分配。额外函数调用只存在于 Profile 构建，正式 Bench/Release 在预处理
阶段完全消失。这样 Tracy 的 inline layout、`NDEBUG` 和 Client 配置只需在一个 adapter target
内部保持一致。

Profile preset 使用与 Bench 相同的优化、CRT、assert/LTO 语义，只额外保留符号并打开 Tracy；
不能把 Release 与另一套 RelWithDebInfo 优化配置的差值误报为 profiler overhead。Tracy 使用
固定 vcpkg 版本、ON_DEMAND 开发 capture，viewer 未连接不能阻塞或改变 Engine 成败；发布包
强制 `none` 且不打包 Tracy Client。Capture 可能带源码路径和 zone 内容，默认不上传公开
artifact。

首期只接入 CPU zone、frame、thread name 和必要 counters；lock/memory tracing 分别可开关，
避免默认产生大量事件。GPU 首期使用 bgfx Stats 和 Pass timer；只有 bgfx backend 能提供合法、
不泄漏底层设备边界的集成时才增加 Tracy GPU context。除 Tracy 开/关 A/B 外，还分别测量
常驻 Metrics off/on、counter/timer update、atomic contention 与 snapshot 成本。

Carbon 的 Tracy 0.13.1 集成记录过 Debug/Release `NDEBUG` 导致 `Profiler` 布局不一致并锁错
内存的问题。Tina 不使用对单个源文件强制 `NDEBUG` 的补丁；只有 `tina_profile_tracy` 可以
包含 Tracy inline header，adapter 与 Client 使用同一 target 配置，并增加 Debug/Profile 编译、
连接、zone capture 和 shutdown 测试。

名称已经确认是 Tracy。vNext 不同时集成 MicroProfile 或第二套 instrumentation profiler，
避免重复插桩、线程和内存开销；只有 Tracy 无法满足已量化需求时才通过新 ADR 重新评估。

## MemorySystem 所有权

`MemorySystem` 由 `EngineHost` 创建并拥有，晚于 Diagnostics 初始化、早于所有会分配引擎
对象的模块创建；销毁顺序相反。它不是 Singleton，也不替换全局 `new/delete`。

```cpp
enum class MemoryTag : std::uint8_t {
    Invalid = 0,
    Core = 1,
    Platform = 2,
    Task = 3,
    RuntimePersistent = 4,
    RuntimeFrame = 5,
    Scene = 6,
    Asset = 7,
    RenderCpu = 8,
    UI = 9,
    Audio = 10,
    Physics2D = 11,
    Cooker = 12,
    Count = 13
};

struct MemoryStatistics {
    std::size_t currentBytes;
    std::size_t peakBytes;
    std::uint64_t allocationCount;
    std::uint64_t deallocationCount;
    std::uint64_t failedAllocationCount;
    std::uint64_t invalidDeallocationCount;
};

class MemoryTracker final {
public:
    [[nodiscard]] MemoryStatistics snapshot(MemoryTag tag) const noexcept;
    [[nodiscard]] MemorySnapshot snapshot() const noexcept;
};
```

当前 Core 已实现固定原子数组 `MemoryTracker` 和不拥有依赖的 `CountingMemoryResource`；后续
`EngineHost` 的 `MemorySystem` 只负责组合并拥有一组 tracker/resource，不再发明第二套计数。
每个 tag 对应一个 Tina-owned `CountingMemoryResource`，包装配置的 upstream resource。
模块初始化时取得 resource 引用并保存在自己的 allocator-aware 对象中，热点分配不查询
字符串 Map。`MemorySnapshot` 是无分配的固定数组；并发更新期间属于观测快照，在 Runtime
barrier/帧边界读取时才视为一致检查点。

Release 保留低成本 current/peak/failed counters；逐指针 callstack、guard page 和完整泄漏
Map 只在专用诊断配置启用，避免性能工具本身改变常规帧行为。
基础 tracker 只能发现字节计数下溢，不能可靠证明任意指针、错误 size/alignment 或 double free；
这些问题由 PMR 调用契约、ASan 和后续专用诊断 allocator 覆盖。tracker、wrapper、upstream
按此顺序缩短生命周期，且 Tina 接受的 upstream `deallocate` 必须不抛异常。

## FrameMemory 与 Arena

Frame scratch 由 Runtime 拥有，不与永久模块内存混用：

```cpp
enum class FrameArenaKind : std::uint8_t {
    SceneCommands,
    RenderExtraction,
    UIDisplayList,
    Temporary
};

class FrameArena final : public std::pmr::memory_resource {
public:
    [[nodiscard]] static Result<FrameArena> Create(
        FrameArenaConfig config,
        std::pmr::memory_resource& upstream);
    [[nodiscard]] void* tryAllocate(
        std::size_t bytes,
        std::size_t alignment) noexcept;
    template<class T>
        requires std::is_trivially_destructible_v<T>
    [[nodiscard]] T* tryAllocateUninitializedArray(std::size_t count) noexcept;
    void reset() noexcept;
    [[nodiscard]] FrameArenaStatistics statistics() const noexcept;
    [[nodiscard]] std::uint64_t epoch() const noexcept;
};
```

契约：

- Engine 创建时一次性分配 backing storage，帧内不增长；
- 线性分配，单次对象不单独 free；
- `tryAllocate(0, alignment)` 仍返回可释放的 arena 地址并消耗1字节；typed array 的 count 0
  返回 `nullptr` 但不记为容量失败；
- 满容量返回 `nullptr`、增加 failed metric，由当前 builder 返回 `CapacityExceeded`；
- 不静默回退系统堆；
- `reset` 只逻辑释放字节并推进 epoch，不调用对象析构；非平凡对象必须在 reset 前显式析构；
- reset 前必须完成使用该 Arena 的所有 Task；
- Scene command arena 在 command commit 后重置；
- Render/UI arena 属于固定容量 `RenderFramePacket` slot，只有 SubmissionTicket completion 后重置；
- GPU 或异步 IO 需要跨帧数据时由 packet/staging owning allocation 持有并由 completion 释放；
- backend-private 代码禁止把 `bgfx::makeRef` 指向 packet 之外即将 reset 的 Arena；无法证明
  生命周期时使用复制语义。

`std::pmr::memory_resource` 的 `allocate` 失败以 `std::bad_alloc` 表达，因此无异常的帧内构建器
优先调用 `FrameArena::tryAllocate` 或在边界预留容量。需要 pmr 容器时，在 phase 边界捕获
分配失败并转换为 `Result`；不允许异常穿过主循环或后台线程入口。

## 专用结构失败策略

| 结构 | 满容量/失败 | 生命周期约束 |
| --- | --- | --- |
| `StaticVector<T,N>` | `tryEmplaceBack` 返回空指针/失败；不 heap fallback | 正确构造、移动、逆序析构非平凡 `T` |
| `InlineFunction<Sig,N>` | 过大/过对齐 callable 编译期拒绝 | move-only；销毁与调用中自失效安全 |
| `FrameArena` | `tryAllocate` 返回 `nullptr` 并计数 | reset 前所有借用者完成 |
| `GenerationPool<T,Tag>` | 满容量返回 `CapacityExceeded` | stale ID 永不解析到新对象；generation 回绕时 retire slot |
| `SpscRingQueue<T,N>` | `tryPush/tryPop` 显式返回 false | 只能一个 producer、一个 consumer |

可恢复的外部输入、Asset 数据和 Cooker 错误返回 `Result`。配置容量被内部稳态场景突破属于
设计错误：Debug 触发 `TINA_ENSURE` 并记录 workload，Release 安全丢弃当前可丢项或终止
当前 phase，不越界、不覆盖旧对象、不切换到隐式 heap。

## 跨模块内存矩阵

| 数据 | 分配域 | 所有者 | 释放/重置点 |
| --- | --- | --- | --- |
| Engine/模块对象 | 对应 `MemoryTag` resource | `EngineHost` | 逆序 shutdown |
| Scene 组件 | EnTT 内部池，使用 Scene resource adapter | `World` | entity destroy/World shutdown |
| Scene commands | `FrameArena::SceneCommands` | Runtime phase | command commit 后 |
| RenderScene/RenderItem/FrameResourceTable | packet FrameArena | `RenderFramePacket` | SubmissionTicket completion 后 |
| UI retained nodes | UI tagged resource | `UIContext` | node/context 销毁 |
| UI PaintCache/committed snapshot | UI tagged resource | `UIContext` | revision retire/node/context 销毁 |
| UI DisplayList/Atlas generation pin | packet FrameArena + pin set | `RenderFramePacket` | SubmissionTicket completion 后 |
| Asset CPU payload | Asset tagged resource | Asset slot generation | upload/cancel/failure后 |
| GPU upload staging | Asset/Render staging allocation | upload request | 后端 completion 后 |
| GPU resource | 后端原生资源计数 | `RenderDevice` | deferred GPU destroy |
| Audio command | 固定 queue storage | AudioEngine | audio callback consume 后 |

CPU allocator 统计、Asset payload、GPU 资源和音频 voice 是四类不同资源账本。总内存面板可以
汇总，但不能用一次全局 allocation hook 代替各自的生命周期计数。

## 禁止事项

- 全局 `new/delete` 替换；
- 帧内容量不足时静默 heap fallback；
- 跨 FrameArena reset 保存裸指针；
- 用 Hash 相等代替对象或路径相等；
- 无工作负载和 profiling 就引入 pool/lock-free/自定义 HashMap；
- 用平均 FPS 掩盖 p99 卡顿；
- 在 Debug 和 Release、不同硬件或不同工作量之间直接比较绝对耗时。

## 剩余参数门禁

候选设计选择保持 Exception 并在模块边界转 `Result`；`tina_bench` schema、结果目录和统计
协议也已形成 Proposed 决定，待本轮确认后冻结。剩余参数不允许由实现随意选择：

1. 建立固定 hard-gate machine profile；在此之前当前开发机结果只作 provisional；
2. 用户确认初始 workload 规模是否覆盖预期游戏；
3. 各切片根据测得 peak 冻结 FrameArena 容量、绝对 noise floor 和可丢/不可丢数据；
4. 记录 Metrics off/on 与 Tracy none/on 的独立 A/B 开销；
5. GPU backend 没有可靠 timestamp 前，GPU p99 只能 informational。
