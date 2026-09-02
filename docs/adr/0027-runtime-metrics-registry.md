# ADR 0027：Runtime Metrics 固定容量 counter registry

- 状态：Proposed
- 日期：2026-08-17
- 决策者：Tina maintainers

> **实现状态更正（2026-09-01）：** 截至 2026-09-01 仍无任何实现代码。
> `MetricsRegistry`、`MetricId`、`CounterRegistry` 在 `include/`、`src/`、`tests/`、`tools/`
> 全部零命中，与下文 2026-08-17 核验时的结论一致。本 ADR 状态为 **Proposed**，
> 因此这不是未兑现的决定，而是尚未开始的提案：文中出现的类型与 API 均为**设计草案，不可调用**。
> 消费方在本 ADR 被 Accepted 并有源码与测试证明之前，请继续使用下文「相邻事实」列出的
> 既有模块自报统计（`MemoryTracker`、`UIContextStatistics` 等）。

## 背景

[ADR 0002](0002-tracy-and-benchmark.md) 接受了「Tina Trace/Metrics」方向。Trace lane 已由
TRACE-001/TRACE-002 闭环：唯一 `TINA_TRACE_ZONE(nameLiteral)` 前端、None/Tracy backend 编译期唯一
选择、Tina-owned 64-byte opaque RAII bridge 与 PRIVATE Tracy adapter。Metrics 至今零实现：本 ADR
提出前（2026-08-17 核验），仓库没有任何 Metrics 公共头、实现、测试或任务条目（Backlog
`METRICS-001` 随本 ADR 同批建立）；`core.md`、`design-freeze.md`、`dependencies.md` 一致
记录「MetricsRegistry 不在当前 Core」「Metrics 仍只有设计」。ADR 0002 对 Metrics 的唯一既有约束是
一句测量要求：「Metrics 常驻成本和 Tracy 插桩成本分别 A/B」；owner、生命周期、容量、线程模型、
counter 类型、注册方式、snapshot 语义与错误模型全部未定义。Carbon 取证同样要求这类能力必须先进入
ADR/Backlog，再由源码、target 和测试证明。

两类相邻事实已经存在，本 ADR 必须与它们划清边界：

- 模块自报统计是各模块 API 的一部分：`MemoryTracker` 按 `MemoryTag` 的原子计数、
  `UIContextStatistics` 的 capacity/high-water/dirty 计数、各池 requested/reserved/published
  counter。`performance-memory.md` 明确要求 UI 扩展现有统计模型，不另建不可观测子系统；
- `tina_bench`（[ADR 0018](0018-benchmark-protocol.md)）的 workload counter/checksum 是 benchmark
  输出协议，不是运行时注册面。

缺失的是 Runtime 级常驻 counter 的统一注册、更新与读取面：让 EngineHost、游戏代码与后续
evidence/bench 消费者用同一套固定容量账本上报和读取低频计数，而不是每处再造一个 ad-hoc 结构。

## 待确认决策

本 ADR 处于 Proposed。下表列出需要 maintainer 确认的关键决策、推荐方案与备选取舍；确认后按
「决定」节落地，任何改选都在本 ADR 更新后再进入实现：

| # | 决策点 | 推荐 | 主要备选与取舍 |
| --- | --- | --- | --- |
| D1 | owner 与组合 | Core 提供 `MetricsRegistry` 类型；`EngineHost` 是唯一产品 owner，仿 `Diagnostics` 前置创建、逆序关闭 | Runtime 私有类型：Core/Asset 等低层无法上报；每模块自建：A/B 口径与读取面碎片化 |
| D2 | counter 模型 | 首切片仅单调 `u64` counter，溢出自然回绕 | 同时引入 gauge/histogram：API 与验收面扩大，违背最小垂直切片 |
| D3 | 注册身份 | 运行期 owner-thread 显式 `registerCounter(name)`，返回带 owner token 的 `MetricId`；名称复制进固定存储 | 编译期 `MemoryTag` 式枚举：跨模块与 SDK 消费者每加 counter 都要改 Core 公共头；generation handle：无 unregister 需求时多付位宽与校验 |
| D4 | 线程模型 | register/snapshot/value 仅 owner thread；`tryAdd` 任意线程 lock-free relaxed 原子 | 全接口跨线程：注册与 name 发布需要 acquire/release 协议，首切片没有消费者需要 |
| D5 | 热路径错误 | `tryAdd -> bool` + 内部 rejected 计数；不构造 `Error` | 热路径返回 `Status`：失败构造含 `std::string` 的 `Error`，违反热路径零分配 |
| D6 | 容量 | `MetricsConfig::counterCapacity` 默认 256、合法区间 `[1, 4096]`，非法 fail-closed；Create 一次性分配 | 动态增长：违反固定容量与 `CapacityExceeded` 惯例 |
| D7 | snapshot | observational（逐 counter relaxed load）、注册顺序稳定、caller-provided span、不足即失败不截断 | 全局一致性快照：需要停写或双缓冲，首切片无消费者需要 |
| D8 | 编译开关 | 不设 `TINA_METRICS` 开关，常驻成本由 bench A/B workload 证明 | 编译开关：条件编译扩散到所有 consumer；若 bench 证明常驻成本不可接受再议 |

## 决定（Proposed）

### 1. Owner 与生命周期

1. `Tina::Core::Metrics::MetricsRegistry` 是 Core 公共类型，头文件
   `include/tina/core/metrics/MetricsRegistry.hpp`；`Create(config)` 返回
   `Result<std::unique_ptr<MetricsRegistry>>`，私有构造，不可拷贝/移动（`Diagnostics` 同模式）。
2. 产品组合中 `EngineHost` 是唯一 owner：接线切片在 `Diagnostics` 之后、Clock/Platform/Task/
   Render/Audio 之前创建 registry，shutdown 逆序销毁；模块与游戏代码只借用非拥有引用，EngineHost
   关闭后不得访问。不引入全局单例或 Service Locator。
3. 测试与工具可独立 `Create` 多个 registry；跨 registry 的 `MetricId` 使用被显式拒绝（第 3 节）。

### 2. Counter 模型

首切片唯一 counter 类型是单调递增 `u64`：`tryAdd(id, delta)` 以 relaxed `fetch_add` 累加，
`delta == 0` 是合法 no-op，溢出按 u64 自然回绕，不 saturate、不报错。gauge、high-water、
histogram、timer、rate 与复位语义全部不在本决定内；每类扩展都有独立容量与验收面，另行排片。

### 3. 注册与身份

1. `registerCounter(name)` 仅允许 owner thread（创建线程）调用，返回 `Result<MetricId>`。这是
   startup 显式建立、运行期固定的注册模型，与 `UIContextCapacityConfig`、`FrameArena`、
   `GenerationPool` 的 Create 期固定一致；违反 owner-thread 契约属于 programmer error，不做逐调用
   线程校验。
2. 名称约束：strict UTF-8、禁止 embedded NUL、长度 `[1, 64]` bytes；注册时复制进 registry 固定
   name 存储，不要求调用方使用字面量或 static 生命周期。推荐 `Module.Subject.Counter` 点分风格，
   但只硬校验上述三条。
3. 重复名称返回 `AlreadyExists`，容量满返回 `CapacityExceeded`，非法名称返回 `InvalidArgument`；
   失败零副作用。
4. `MetricId` 是强类型值 `{ ownerToken: u32, index: u32 }`，默认构造无效；ownerToken 进程内单调
   分配（`GenerationPool` owner token 同模式），使 cross-registry id 可被 O(1) 拒绝。首切片没有
   unregister，slot 永不复用，因此不需要 generation 位；未来若引入删除必须新增 ADR。

### 4. 线程模型

1. `registerCounter`、`value`、`snapshotInto` 与统计查询仅 owner thread。
2. `tryAdd` 可从任意线程调用（含 Task worker）：lock-free、`noexcept`、零分配、无字符串处理，
   仅一次 owner/range 校验加一次 relaxed `fetch_add`。
3. snapshot 是 observational 读取：逐 counter relaxed load，可能横跨并发更新，跨 counter 不构成
   一致性事务（`MemoryTracker` snapshot 同措辞）。需要更强一致性的消费者必须自行停写。

### 5. 热路径与错误模型

1. 冷路径（register/value/snapshotInto）使用 `Result<T>`/`Status`，复用 `CoreErrorCode`
   （`InvalidArgument`/`AlreadyExists`/`CapacityExceeded`），不新增 `ErrorDomain`。
2. 热路径 `tryAdd(MetricId, u64) noexcept -> bool`：无效 id（默认值、错误 ownerToken、越界
   index）返回 `false` 并递增内部原子 `rejectedUpdateCount`，保证静默丢弃可观测
   （`MemoryTracker::recordInvalidDeallocation`、`Diagnostics::sinkFailureCount` 同模式）；不构造
   `Error`、不写日志。
3. `value(id)` 对无效 id 返回 `InvalidArgument`；`snapshotInto(span)` 在 span 小于当前 counter 数
   时返回 `InvalidArgument`，不截断输出。

### 6. 容量与内存

1. `MetricsConfig{ counterCapacity = 256 }`，合法区间 `[1, 4096]`；越界时 `Create` 返回
   `InvalidArgument`（fail-closed，不 clamp，`EngineConfig::validate` 同模式）。上限同时是
   cardinality 上限：不支持动态 metric 名称或无限 cardinality。
2. `Create` 一次性预留 counter 原子数组与 `capacity × 64` bytes name 存储；生命周期内零增长、
   无 heap fallback、运行期零分配（`FrameArena`/`GenerationPool` 同模式）。默认容量常驻内存约
   18 KiB（256 × 72 bytes），上限约 288 KiB（4096 × 72 bytes）。
3. 首切片不接入 `MemoryTag`/PMR 注入；registry 自身是账本而不是被追踪对象（`Diagnostics` 同级
   root 对象先例）。

### 7. snapshot 读取

1. `counterCount()`、`capacity()`、`rejectedUpdateCount()` 为 owner-thread 查询。
2. `snapshotInto(std::span<MetricCounterSnapshot> out) -> Result<usize>` 按注册顺序写入
   `{name, value}` 并返回写入数量；`name` 是借用 registry 内部存储的 `std::string_view`，生命周期
   与 registry 相同。
3. 注册顺序稳定且可复现；序列化/导出（JSON、evidence schema）不在本决定内。

### 8. 与 Trace、bench 与模块统计的边界

1. Metrics 与 Trace 独立：Trace 是编译期选择 backend 的 zone 插桩；Metrics 是运行时常驻账本，
   无编译期 backend、无 `TINA_METRICS` 开关，始终参与产品构建。
2. ADR 0002 的「Metrics 常驻成本和 Tracy 插桩成本分别 A/B」由 bench 对照达成：Tracy 用构建变体
   A/B；Metrics 用后续 `metrics_registry_v1` workload 的参数对照（0 counter 对 N counter、update
   off 对 on）。未使用的 registry 只付一次性内存，没有每帧成本。该 workload 不在首切片。
3. 模块自报统计（`MemoryTracker`、`UIContextStatistics`、各池 counter）保持原状，不迁移、不镜像
   进 MetricsRegistry；Metrics 服务 Runtime 级与游戏级低频常驻计数，任何聚合/桥接需要独立决定。

### 9. 公共面与验证

1. 公共头只用 Tina 类型与标准库，进入既有 header-isolation TU、`PublicHeaderIsolationTests`
   include 清单、SDK 目录安装与第三方 token 扫描。
2. 首切片（Backlog `METRICS-001`，本 ADR Accepted 后启动）：Core `MetricsRegistry` 注册/更新/
   snapshot 闭环与定向 GoogleTest，覆盖容量边界（注册满、非法 config）、重复注册、非法名称、
   无效与跨 registry id、并发 `tryAdd`、snapshot 语义与 span 不足、运行期零分配。
3. EngineHost 接线（容量经 `EngineConfig` startup-only 配置，`ShadowMapExtentConfig` 同模式）、
   首个 Runtime counter、游戏可见 facade 与 bench workload 是后续切片，不在 `METRICS-001`。

## 结果

- Runtime 与游戏获得统一、固定容量、fail-closed 的常驻 counter 注册与读取面；
- 公共 API 零第三方类型；热路径 lock-free 零分配；未使用时只付一次性内存，可被 bench A/B 证明；
- 成本与限制：首版只有 u64 counter，注册仅 owner thread，名称上限 64 bytes，容量上限 4096，
  无导出/序列化；
- 需要建立的门禁：`METRICS-001` 单测矩阵 + header isolation + 第三方 token 扫描；
  `metrics_registry_v1` A/B workload 与 EngineHost 接线随后续切片。

## 被拒绝方案

- 编译期 `MemoryTag` 式全局枚举 counter：每加 counter 都要改 Core 公共头，SDK 消费者无法注册
  自定义计数；
- 热路径字符串 key 查找：违反「热路径不做字符串处理」；
- 全局单例或 Service Locator：违反非全局组合根不变量；
- 每模块自建 registry、无统一读取面：常驻成本 A/B 与 evidence 聚合没有单一入口；
- generation handle + unregister：首切片无删除需求，永不复用的 slot 不需要 generation 位；
- 首切片同时交付 gauge/histogram/导出：扩大 API 与状态机，无法用一个垂直门禁证明；
- 无界或动态增长容量：违反固定容量 + `CapacityExceeded` 惯例；
- `TINA_METRICS` 编译开关：条件编译扩散到全部 consumer，A/B 已可由 workload 对照完成。
