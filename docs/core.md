# tina_core 设计与 Carbon Core 取证

> 状态：设计已冻结并分批实施。Carbon Core 是经验样本，不是 Tina 的依赖或移植源。

## 结论

Carbon Core 最值得学习的不是某个容器或宏，而是长期工程中形成的可观测性和故障契约：
线程、锁、队列、内存、计时、崩溃上下文都能命名、计数、取样并独立测试。

Tina 不应复制 Carbon 的历史包袱。vNext 保留现有现代 C++ 基础，在其上增加轻量、可注入、
无后端绑定的 diagnostics；不替换全局 `new/delete`，不自造一套标准线程库或 STL，
不暴露全局 Telemetry/Crash Reporter，也不用宏式分配贯穿业务代码。

## Tina 当前基础

当前 `tina_core` 已经包含：

- `base`：固定宽度类型、Platform/Compiler、C++23 SourceLocation/EnumFlags 和只接受
  `noexcept invoke + noexcept move` callback 的 ScopeExit；
- `error`：以 C++23 `std::expected` 实现的 `Result<T>`/`Status`，显式稳定编号的
  `ErrorDomain + ErrorCode`，以及 origin、native integer code 和 UTF-8 context chain；
- `time`：强类型 `Duration/MonotonicTimePoint`、可注入 `IMonotonicClock`、
  `SteadyMonotonicClock` 与新 Runtime 使用的 `FixedStepAccumulator`；
- `diagnostics`：可替换 handler 的 `TINA_ASSERT`；
- `include/tina/core/...`：不借用 `src` include root、无第三方 header 的正式公共面。

旧 `Clock/FrameTimer/FixedStepTicker` 只保留在 `src/core` forwarding/compatibility 路径，供
Legacy Application 迁移期间继续使用；它们不是 vNext Game SDK。

EASTL、xxHash、旧 `Memory.hpp/Path.hpp/Time.hpp` 已隔离在 `tina_core_legacy`。vNext 的新决定
是：EASTL 只随 Legacy 存续，迁移结束后删除；xxHash 从 Legacy 中解耦，作为窄接口后的
私有 Hash 后端保留。`Result` 已用 C++23 `std::expected` 收敛，ScopeExit/Clock 则保留语义并
建立更窄、更清晰的新接口；我们不把旧 `Container.hpp` 复制到新 Core。

当前缺口主要是：

- 指标、Trace Zone、线程/锁/队列命名尚未形成统一接口；
- 只有 programmer assertion，尚未区分 ensure、recoverable error 与 crash context；
- 缺少内存 tag、当前值/峰值和资源预算快照；
- UTF-8 路径、原子文件写入与平台错误尚未形成新 Core 契约；
- `Hash`、路径和内存的有效能力仍在 Legacy compatibility 中。

## vNext Core 边界

首期保持单一 `tina_core` target，内部按以下目录和 namespace 分责：

| 子域 | 目标能力 | 不属于 Core |
| --- | --- | --- |
| `core/base` | Types、Result/Error、SourceLocation、EnumFlags、ScopeExit、专用 generation ID | 游戏配置、窗口、渲染 |
| `core/time` | SteadyMonotonicClock、SystemClock 边界、Duration/TimePoint、可注入 `IMonotonicClock` | Frame Pipeline 所有权 |
| `core/concurrency` | 协作停止、命名和可观测包装；优先标准库 primitive | 线程池策略、Asset job 类型 |
| `core/diagnostics` | Log、Assert/Ensure、Metric、TraceZone、CrashContext | Tracy SDK 公共类型、上传服务 |
| `core/memory` | MemoryTag、当前/峰值计数、`std::pmr` 统计资源、FrameArena | 全局 allocator 替换、通用对象池 |
| `core/containers` | StaticVector、InlineFunction；按证据增加固定 SPSC queue | 通用 Vector/String/Map/智能指针/算法库 |
| `core/io` | UTF-8 API、`std::filesystem::path`、原子写、明确错误 | 资产格式解析、虚拟文件包 |
| `core/id` | generation ID 基础、StringId、AssetId/Uuid、Hash 类型 | ECS registry、Asset registry |

这些只是逻辑子域。没有真实构建收益前不拆成多个静态库，也不提供 `Core.hpp` 巨型聚合头；
调用方 include 自己实际需要的窄头文件。

## EASTL、专用容器与 xxHash

当前 EASTL 通过旧 `Container.hpp`、`Memory.hpp` 和 `Core.hpp` 扩散到 UI、Scene、ECS、
Renderer、Event、Audio、Physics 与测试。它不能立即删除，否则只会产生一次没有架构收益的
机械改写；但新 vNext target 从第一天起不链接 EASTL，旧路径只作为迁移桥。

Tina 不会因为只需要 EASTL 的一部分就复制其实现。自研范围按“引擎专用契约”而不是
“EASTL API 子集”确定：

- `StaticVector<T, N>`：对象内存储、无 heap fallback、满容量返回失败；
- `InlineFunction<Signature, Bytes>`：小对象内联、禁止动态分配；
- `FrameArena`：按帧线性分配、统一 reset、对齐与高水位统计；
- `GenerationPool<T, Tag>`：owner/slot/generation 生命周期与 stale/wrong-owner 检测；
- `SpscRingQueue<T, N>`：仅在 Audio/Upload 单生产者单消费者路径测得需要后实现。

其余使用 `std::vector/string/optional/variant/unique_ptr` 等标准类型；需要生命周期 allocator
时使用 `std::pmr`。不实现 DynamicVector、通用 HashMap、SharedPtr、String、Sort 或完整
Allocator framework。这样既去掉 EASTL 依赖，又避免维护一套未经多年验证的 Tina STL。

xxHash 不随 EASTL 删除。它只通过窄 adapter 服务 `ContentHash`、Cook cache 和可选
`StringId`，算法/seed/version 写入格式契约。禁止把64位 Hash 当作 AssetId，禁止只凭 Hash
判断路径相等，也禁止用非密码学 xxHash 证明不可信包未被篡改。

M10-A2a 已落地：`digestContentHashV1(std::span<const std::byte>)` 在 Core 实现侧 PRIVATE 调用
XXH3-128（seed=0），把 `low64`/`high64` 按 little-endian 写入 16 字节 `ContentHash`；公共头与
Game SDK 不得出现 `xxhash.h` 或 `XXH*` 符号。空输入合法且结果确定；全零 digest 拒绝发布。

## Carbon Core 证据与决策

本次针对本地 `temp/carbon-engine/core` 的 `342f118ca4b70f6a8de50577674a75e2aa893fd3`
进行取证。该目录不会进入 Tina Git 或构建。

| Carbon 能力 | 可学习的工程契约 | Tina 的实现方向 | 明确不采纳 |
| --- | --- | --- | --- |
| `CcpScopeGuard` | 初始化成功后立刻登记逆操作 | 保留现有类型安全、`noexcept` 的 `ScopeExit`，Runtime 用它构建阶段回滚 | 大量 0/1/2 参数特化、调用约定宏 |
| `CcpTime` | 高精度单调时间与 UTC/日历时间分离 | `steady_clock` 测耗时，`system_clock` 只处理墙钟；通过 `IMonotonicClock` 做确定性测试 | 无单位 `uint64_t timestamp + frequency` 作为公共 API |
| `CcpMutex/Semaphore` | 长生命周期锁和 semaphore 必须有 telemetry 名称 | `std::mutex/scoped_lock/counting_semaphore` 为基础，外加可选命名/等待统计 | 无证据重写 OS 同步原语、公开 Acquire/Release 风格 |
| `CcpThread` | 线程注册、名称、优先级和 CPU 时间有诊断价值 | `std::jthread + stop_token`；线程名称/优先级下沉 platform adapter | `CcpKillThread`、裸 HANDLE、当前 header 的 Windows/Apple 限制 |
| `CcpTelemetry` | RAII Zone、线程/锁/内存跟踪、明确 capture 状态 | Tina-owned trace 宏语义、EngineHost-owned `TraceSession`、可选 Tracy 编译期 adapter，空后端零开销 | 业务可访问的全局 profiler、函数指针 + `void*` 回调、首期 fiber/tasklet |
| `CcpStatistics` | 指标具备名称、单位/说明、每帧或 lifetime、current/peak；RAII timer | EngineHost 拥有 `MetricsRegistry`，使用 typed `MetricId`，帧边界生成 snapshot | 全局静态宏 entry、继承式 mean/stddev 体系 |
| `CCPMemory`/Tracker | 分配对齐、溢出、OOM、越界、泄漏、报告和 tracker 自递归都应测试 | `MemoryTag`、统计 `pmr::memory_resource`、高水位和资源预算；热点 arena 以 profiling 为前提 | 全局 new/delete 替换、每处分配宏、每类型全局 cached allocator |
| `CcpFileUtils` | 路径 normalization、`.`/`..`、重复分隔符和不存在路径都有测试 | UTF-8 边界、`std::filesystem::path` 内部表示、Result + 原子替换 | `CCP_MAX_PATH`、wide string/raw fd 公共 API、失败静默的 `void remove` |
| Assert/Callstack/Crash | invariant、调用栈、批量符号化、crash key/value 是独立能力 | `TINA_ASSERT`、`TINA_ENSURE`、`Result` 分流；`ICrashSink` 由 EngineHost 拥有 | 每断言点“永久忽略”、全局 `BeCrashes`、用 assert 处理用户数据错误 |
| UTF-8 转换 | Windows 边界需要显式 UTF-8/Wide 转换 | 引擎 API 始终 UTF-8，进入 Win32 API 前转换，非法序列返回 Error | ATL `CW2A/CA2W`、locale-dependent ASCII 转换 |
| `CCPHash` | 必须区分稳定身份 hash 与进程内查找 hash | 128 位 AssetId 与强内容 Hash 分离；StringId 使用固定 64 位并诊断碰撞 | 32 位 FNV 作为 AssetId 或内容身份 |
| Core tests | Core 的每项契约都能独立测试 | 为 IO、内存、时间、诊断、并发分别建立直接 GoogleTest | 只靠整机 smoke 覆盖底层错误 |

## 时间与可测试性

耗时、帧节奏和墙钟是不同概念：

- `SteadyMonotonicClock`：单调，供 delta、timeout、profile 使用；
- `SystemClock`：UTC/日历时间，只用于日志时间戳、文件元数据等；
- `IMonotonicClock`：测试注入，不进入每个热点调用的虚函数链；由拥有 timeout/调度策略的对象保存；
- `Duration`/`TimePoint`：使用强类型，禁止把毫秒、tick 和秒混在裸整数中。

`FixedStepAccumulator::advance(realDelta, gameplayTimeScale)` 先验证真实 delta 和 time scale，
再把真实 delta 钳制到 `maximumAcceptedRealDelta`，最后缩放为 `updateDelta`。默认固定步长
1/60 秒、真实 delta 上限250 ms、每帧最多4步；超额整步债务被丢弃并单独计数，但小于一步的
余量保留用于 interpolation。`realDelta`、`acceptedRealDelta`、`rejectedRealDelta`、
`updateDelta` 和 `discardedSimulationDelta` 不混成一个模糊数值。测试使用 Manual Clock，
不依赖真实 sleep。旧 `FixedStepTicker` 只作为 Legacy 行为回归保留。

## 并发与任务边界

Core 只提供标准并发 primitive 的小型辅助和 instrumentation，`tina_task` 才拥有线程池、
队列、任务调度与 shutdown policy。

首期契约：

- 所有后台线程均有稳定名称；
- 停止使用 `stop_token` 协作完成，Engine shutdown 必须 join；
- 队列有容量、等待时间、当前深度、峰值和丢弃/取消计数；
- 不允许 detach 后继续访问 Engine 对象；
- 不提供强杀线程；无法在期限内结束属于明确 shutdown error；
- 只有 profiling 证明必要时才增加 spin lock 或无锁结构。

## Diagnostics

推荐最小公共面：

```cpp
using MetricId = GenerationId<MetricTag>;

enum class MetricKind { Counter, Gauge, Timer };
enum class MetricLifetime { Frame, Process };
enum class MetricValueType { U64, I64, F64 };
enum class MetricUnit { Nanoseconds, Bytes, Count, Ratio };
enum class MetricThreadModel { MainOnly, Atomic, BarrierReduced };

struct MetricDescriptor {
    std::string_view name;
    std::string_view description;
    MetricKind kind;
    MetricLifetime lifetime;
    MetricValueType valueType;
    MetricUnit unit;
    MetricThreadModel threadModel;
};

class TraceSession final {
public:
    static Result<TraceSession> Create(const TraceConfig& config);
    void markFrame() noexcept;
};

#define TINA_TRACE_ZONE(literal_name) /* compile-time backend adapter */
```

日志、Metrics、Trace 和 Crash 是四条不同通道。`Diagnostics` 由 EngineHost 拥有，模块在构造时
取得不可拥有的窄 `DiagnosticChannel`，`IGameApplication`/`IGameState` 只从注入的生命周期或
Phase Context 取得 view；业务
代码不能通过全局 `Logger::instance()` 查找 sink。Log record 固定包含 level、稳定 category、
UTF-8 message、SourceLocation、thread/phase 和可选小型结构化字段，不允许动态高基数 category。

- Debug/Trace 级别在正式 Release/Bench 可编译掉或由静态最低级别短路，未启用时参数不求值；
- benchmark 测量区默认不格式化/写出普通日志，错误计数先进入 Metrics，run 结束后输出摘要；
- Worker 不直接争用文件/console sink，Task failure 把固定上下文随 completion 返回主线程；
- Audio callback、crash signal/SEH 路径禁止普通日志；后者只写预分配 CrashContext/emergency sink；
- 可恢复 sink 失败降级为受控 stderr/debug output 并增加计数，不能递归记录“日志失败”；
- shutdown 先停止生产者、join Worker/Audio，再 flush/close sink，Diagnostics 最后销毁；
- token、用户正文、clipboard、绝对资源路径等敏感/高基数值默认不进入日志或 Tracy zone。

首切片可以使用简单同步 sink，因为成功热点不写日志；若 profiling 证明日志吞吐需要异步，
再增加有界队列和明确 QueueFull 策略，不能默认创建无界后台日志线程。spdlog 若继续使用，只能
是 sink/format adapter 的私有实现，公共 header、Error 和 LogRecord 不暴露 spdlog 类型。

`MetricsRegistry` 由 `EngineHost` 拥有，模块在初始化时注册指标，在唯一帧边界 capture。不是
所有写入都需要 atomic：主线程独占指标使用普通数值，只有跨线程计数使用原子。Registry
拒绝同名不同 descriptor 和动态高基数 label；p50/p95/p99 由 bench 预分配 sample buffer 离线
计算，不在常驻 Metrics 中维护复杂 histogram。

推荐首批指标：

- frame/fixed step 次数、丢弃 accumulator 次数；
- event、scene command、asset completion、GPU upload 队列当前/峰值；
- CPU completion 和 GPU upload 的任务数、字节数、耗时；
- Render handle、buffer、texture、pipeline 当前/峰值；
- UI layout 次数、节点数、display command 数；
- 按 `MemoryTag` 聚合的 CPU 内存，以及后端报告的 GPU 资源字节。

Trace 后端在 configure/compile time 选择。公共头文件不包含 Tracy；`none` 时宏展开为
`(void)0` 且参数不求值，不能改变控制流或留下每-zone 虚调用。`TraceSession` 只负责低频
初始化、frame mark 和 shutdown，不由热点路径查询；`tina_profile_config` 只传播 Tina backend
selection，Tracy 自身 definitions 私有留在 `tina_profile_tracy`，并由其唯一链接 Client。业务 TU
只构造 Tina `TraceZoneToken` 并调用窄 begin/end adapter，不包含 Tracy inline header；静态
site 只注册一次，帧内不分配。Zone 名只接受字面量/稳定静态 ID，动态敏感文本与高基数路径
不得进入 capture。

Carbon Core 的实际 profiler backend 是 Tracy，不存在本地 `TinyProfile` 模块。其 CMake
使用 `WITH_TELEMETRY`、`find_package(Tracy CONFIG REQUIRED)` 和私有 `Tracy::TracyClient`；
Telemetry tests 还实现了最小 Tracy client 来断言 zone/lock 事件。Tina 学习这种“公共语义
自有、后端可替换、事件可测试”的方式，并把编译配置一致性纳入门禁；不提供业务可调用的
全局 profiler API，也不让 Tracy 名称扩散到 Runtime/Scene/UI API。第三方 Tracy 内部状态
被限制在 adapter，EngineHost 仍显式拥有 capture session 的启动和关闭时机。

## 内存策略

Carbon Memory Tracker 展示了成熟内存诊断需要覆盖的边界，但 Tina 首期不做 allocator 大战。

推荐三层：

1. 默认使用标准 allocator，正确性优先；
2. Debug/Profiling 使用 `CountingMemoryResource`，按 `MemoryTag` 统计 current、peak、alloc/free；
3. Frame scratch、UI layout scratch、Render extraction 使用经过测试的 `FrameArena`；其他
   arena 只有测得热点后再增加。

内存 tracker 自己不能通过被跟踪路径分配，否则会递归。OOM、`size * count` 溢出、非法
alignment、计数下溢和 tracker shutdown 顺序都需要测试。没有逐指针表时不能可靠声称检测
任意 pointer、错误 size/alignment 或 double free；首期由 PMR 前置条件、ASan 和专用诊断配置
覆盖。CPU allocator 统计与 GPU/Asset 资源计数是不同体系，不能用一次全局 new hook 假装
覆盖全部资源。

## 错误、断言与崩溃

三类失败必须分开：

| 类型 | 用法 | 行为 |
| --- | --- | --- |
| Programmer invariant | `TINA_ASSERT` | Debug 中断/终止；Release 不承担业务分支 |
| 可继续但违反内部预期 | `TINA_ENSURE` | 记录一次完整上下文；只有状态仍安全时继续 |
| 可恢复失败 | `Result<T, Error>` | 初始化、IO、资产、设备创建、非法外部数据显式返回 |

`Error` 已提供稳定 `ErrorDomain + ErrorCode`、UTF-8 message、origin SourceLocation、可选 native
integer code 和按传播顺序追加的 context chain。Domain/code 数值只允许追加，不能重排；Core
通用错误位于 `CoreErrorCode`，Runtime/Asset/Render 等模块各自定义窄 code namespace。
`ICrashSink` 由 `EngineHost` 注入并拥有；CrashContext 只保存小型 key/value、最后阶段、线程、
场景和资源计数等非敏感 breadcrumb。崩溃路径尽量不分配，优先保存 raw address，正常工具
或后处理再符号化。

C++ exception 作为编译能力保持开启，原因是标准库、`std::pmr` 与第三方依赖可能抛出；但
Tina 公共模块边界统一返回 `Result/Status`，热点正常流程不使用 throw。Engine create/run、
`IGameApplication`/`IGameState`、Worker、音频/平台 C callback 和 Cooker 命令入口是强制 catch
边界；捕获后追加 phase/task/asset 上下文并转换。析构、rollback、`onShutdown`、`onExit` 和实时 callback 必须 `noexcept`，
若其内部仍抛出则视为 programmer invariant 并终止，不能吞掉后继续。

## UTF-8、路径和文件

公共字符串和路径输入使用 UTF-8。Windows adapter 在调用 Win32 API 前严格转换为 UTF-16；
非法 UTF-8 返回 `CoreErrorCode::InvalidArgument`，不能使用系统 locale 猜测。

Core IO 首期只提供：

- normalize/canonical 语义明确的路径操作；
- 有大小上限的 `readFile`；
- 创建父目录；
- 临时文件写入、flush、原子 replace；
- 包含 OS error code、UTF-8 路径和操作名称的 Result。

M10-A2b 已落地有界 `readFile`：`ReadFileConfig{maxBytes, memoryResource*}`，UTF-8 路径、
`std::filesystem` 内部、常规文件、分配前 size 上限、成功返回 `std::pmr::vector<std::byte>`。
原子写/normalize 完整套件仍后置。

Cooker 依赖原子写保证失败时旧产物仍有效。路径测试覆盖空路径、`.`、`..`、重复分隔符、
不存在路径、Unicode、只读、权限失败、跨卷 replace 和 traversal 边界。

## Hash 与身份

- `AssetId`：稳定 128 位逻辑身份；
- `ContentHash`：版本化128位非密码学内容 Hash，用于增量 Cook、缓存和非对抗性损坏检测；
  安全完整性使用独立密码学 Hash/签名；
- `StringId`：固定算法的 64 位运行时 ID，Debug 保存原文并检测碰撞；
- `EntityId/UINodeId/RenderHandle`：owner token + index + generation；UINodeId 另含语义 owner WindowId，
  不使用内容 hash。

类型之间不提供隐式转换，避免把“身份”“内容版本”“容器 hash”混为一个整数。

## Core 验收清单

- `std::expected` Result 的 value/error、移动、context chain、native code 和 misuse 行为；
- `ScopeExit` 移动、release、异常安全和逆序回滚；
- SteadyMonotonic/Manual Clock 与 FixedStepAccumulator 的钳制、time scale、最多4步、丢弃量、
  非零余量和失败不改状态测试；SystemClock 尚待 IO/日志消费者落地；
- Metric 的 frame reset、lifetime、current/peak、跨线程计数和 snapshot；
- Trace backend 开/关不改变业务结果；
- Diagnostics owner/sink 先后顺序、级别短路不求值、Worker completion 日志、sink failure 不递归、
  敏感字段过滤与 shutdown flush；
- UTF-8/Wide 非法序列、Unicode 路径和原子写失败恢复；
- MemoryTag current/peak、全量无分配 snapshot、OOM、溢出、alignment、并发计数和 tracker 自递归；
- StaticVector 的满容量、构造/析构/移动和无堆分配；InlineFunction 的大小限制、移动和
  回调自销毁；FrameArena 的对齐、reset、OOM 与高水位；
- generation ID 的 stale/wrong-owner handle、wrap retire、固定容量/零稳态分配和类型隔离；
- Assert/Ensure handler、death test、CrashContext 容量和敏感字段限制；
- Visual Studio 2026 / MSVC 19.50 与 Linux GCC/Clang 直接执行 GoogleTest，不通过 CTest 调度。

## 推荐优先级

Core 第一阶段只落地支撑首个 Null Runtime 切片的能力：Result context、可注入 Clock、
MetricsRegistry、TraceZone 空后端、线程/队列命名、MemoryTag、FrameArena、GenerationPool
和原子文件写。StaticVector/InlineFunction 在首个真实消费者出现时加入；Callstack 符号化、
完整 crash upload、更多 arena、复杂 histogram 与无锁容器都延后，直到 Runtime、Asset 或
Render 出现真实需求和 profiling 证据。

实施状态（2026-07-17）：`MemoryTag`、原子 `MemoryTracker`、`CountingMemoryResource` 和 owning
`FrameArena` 已进入 `include/tina/core/memory`。Arena 只在 `Create` 取得一次 backing block，
支持 PMR、对齐/溢出/OOM 计数、epoch/reset 和零 heap fallback；`MemorySystem` 聚合 owner、
完整 Metrics/Trace 仍属于后续独立批次。`GenerationId<Tag>` 与固定容量 `GenerationPool<T,Tag>`
已落地：ID 编码非零 owner token/index/32位 generation，Create 只分配一次 slot block，构造失败
不消费 slot，erase 立即使 stale ID 失效，回绕策略为永久 retire。owner token 由 Core 自动单调分配，
pool 销毁后也不复用，跨 pool ID 在 Release 确定拒绝。

实施状态（2026-07-22，M-Diag-A0）：`include/tina/core/diagnostics/` 增加最小日志面
`LogLevel`、`LogRecord`、`DiagnosticChannel`、`Diagnostics`。`Diagnostics` 由 `EngineHost`
在 factory 接线前创建、在 module shutdown 最后销毁；模块可取不可拥有的 `DiagnosticChannel`。
默认私有 console sink（Info→stdout，Warn+→stderr），级别短路不写 sink；sink 失败计数且
不递归；shutdown 后 channel 写为 no-op。公开头不暴露 spdlog/fmt。Deferred：file sink、
有界异步队列与 QueueFull、MetricsRegistry、TraceZone/Tracy adapter、CrashContext、
Worker completion 聚合、敏感字段过滤、IGameApplication/IGameState Phase Context 注入。
