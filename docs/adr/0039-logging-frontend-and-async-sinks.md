# ADR 0039：日志前端按编译期剥离，记录自持字节，队列有界且丢弃可计数

- 状态：Accepted
- 日期：2026-09-01

## 背景

`Tina::Core::Diagnostics` 一直是自研的，仓库从未引入 spdlog、fmt 或任何第三方日志库
（`thirdparty/` 下只有 bx、bgfx、cgltf、box2d、dear-imgui、freetype、miniaudio）。
M-Diag-A0 切片的注释把它自己描述为"main-thread synchronous writes, private console sink
by default, no background queue"，这不是缺失的功能清单，而是四个真实缺陷：

1. `LogRecord::message` 是 `std::string_view`，借用调用方的缓冲。异步化在这个前提下
   不可能成立：一条排队的记录必然比产生它的栈帧活得久。
2. console sink 每行一次 `std::fflush`。一次 flush 是一次系统调用，这是当时的主成本，
   而不是格式化。
3. 没有编译期剥离。被 `minLevel` 过滤掉的日志仍然求值全部参数、仍然构造记录。
4. 整个类没有线程安全保证。"main-thread" 是它的**前提**，而引擎实际有多个工作线程。

公共 API 政策（`docs/public-api.md`）规定公共头不得命名 libc++ 19（Android NDK 28 用它）
缺失的设施——命名了整个模块就无法为 Android 编译。这条政策直接排除 `std::format`：
全仓库 `<format>` 出现 0 次，这是政策的结果而非疏漏。

还有一个决定 flush 策略的事实：仓库有 149 处 `std::terminate()`。日志紧跟终止是常见形态，
而一条排队未投递的记录会随进程一起死掉——恰恰是解释这次终止的那一行。

## 决定

### 前端：宏 + 编译期剥离

`TINA_LOG_TRACE/DEBUG/INFO/WARN/ERROR/CRITICAL` 按 `TINA_LOG_LEVEL_COMPILED` 常量在编译期
剥离低于它的级别，语句整体消失、参数不被求值（Debug 保留 Trace，其余配置从 Info 起）。
`TINA_LOG_CRITICAL` 永不剥离：无法报告自身致命状况的构建比慢的构建更糟。

宏**不使用** `__VA_OPT__`。MSVC 传统预处理器只在 `/Zc:preprocessor` 下支持它，而公共头不能
要求消费者传这个 flag（`tina_core` 从 bx 传递性地拿到了它，`tina_runtime` 没有——这是实测的
编译失败，不是推测）。改为把 `pattern` 一并放进 `__VA_ARGS__`，可变参数因此永远非空。

### 格式化：自研，不用 `<format>`

`LogFormat.hpp` 提供 `{}` 占位符，通过类型擦除的 `Format::Argument` 隐式构造承接实参，
`format()` 本身不是模板。公共头只依赖 `<string_view>` 和 `<cstddef>`；`<charconv>` 只出现在
`.cpp`。整数走 `std::to_chars`，浮点走 `std::snprintf("%g")`——`ParseFloat.hpp` 只记录了
`from_chars` 的缺口，`to_chars` 浮点半边在 NDK 28 上是否可用**未经此处验证**，所以用无歧义
可用的那个。locale 敏感性无害，理由与 `ParseFloat.hpp` 接受 `strtof` 相同：Tina 从不调用
`setlocale`。参数数量不匹配显式可见（缺参 `{?}`、多参 ` {extra:N}`），不静默丢弃。

### 记录：自持 256 字节内联缓冲

`LogRecord` 从借用视图改为拥有型，`message` 是 `char[256]`，由 `LogRecord::make()` 复制；
`category` 仍借用（字符串字面量有静态存储期）。缓冲末尾保留 NUL，因为
`OutputDebugStringA` 与 `__android_log_write` 都需要它。实测 `sizeof(LogRecord)` = 304 字节。
这是**破坏性变更**：全部聚合初始化点改为 `LogRecord::make()`，字段访问改为访问器，不留别名。

### 线程安全与队列

计数器全部 `std::atomic<u64>` relaxed，`m_open` 原子，递归守卫改为 `thread_local`（sink 里再记
日志是缺陷，无论它指向哪个 Diagnostics 实例）。sink 调用在 `m_sinkMutex` 下串行，因为
`LogSinkFn` 由消费者编写、不能假定可重入。

队列是单 drain 线程 + 有界环形数组（mutex + condition_variable，与 `BoundedTaskSystem` 同一
房子模式）。满时**丢弃最新并计数**：解释突发如何开始的那条记录比突发中段的更有价值。
默认同步（测试写完立刻断言，无需 flush），EngineHost 显式选 1024 槽异步。队列内存直接
`new[]`，不经 MemoryTracker——Diagnostics 先于 tracker 存在，让日志依赖另一个子系统会颠倒
它本该服务的依赖方向。

**Error 及以上在 `write()` 返回前完成投递。** 这是那 149 处 `std::terminate()` 的直接结果。
仍然先入队再等待，不插队到队首，否则致命行会排在为它铺垫上下文的记录之前。这个缺陷是
`EngineHostFramePacketDeathTest` 实测暴露的，不是设计时想到的。

### sink：叠加而非互斥

console 只在没有自定义 sink 时作为后备（保住既有捕获测试的隔离）；file 与 platform 独立叠加。
file sink 用追加模式打开、绝不截断（上次运行的日志是证据，崩溃-重启循环本会擦掉解释它的
那一次），缺失父目录自动创建。轮转在写入**前**检查、只保留一个备份 `.1`，因此文件可能超出
阈值一行的长度——截断记录会更糟。打开失败不使 `Create` 失败（为一个日志文件拒绝启动比没有
它继续记录更糟），状态由 `isFileSinkOpen()` 报告。platform sink 在 Windows 是
`OutputDebugStringA`（仅当 `Create` 时已附加调试器）、Android 是 `__android_log_write`
（logcat 是用户唯一能读到的日志，故默认开），其他平台为空。

## 结果

实测数字（Windows x64 Release，`TINA_LOG_LEVEL_COMPILED=2`，一次性探针程序，
未纳入仓库；每格纳秒/次）：

| 场景 | ns/次 | 投递 | 丢弃 |
| --- | --- | --- | --- |
| 被剥离的 Trace（20 万次） | 0.00 | 0 | — |
| 被 `minLevel` 过滤（20 万次） | 1.57 | 0 | — |
| 同步 + 空 sink（20 万次） | 67.62 | 200000 | 0 |
| 异步入队 + 空 sink（20 万次） | 184.09 | 176246 | 23754 |
| 同步 + file sink（2 万次） | 890.17 | 20000 | 0 |
| 异步入队 + file sink（2 万次） | 125.58 | 1495 | 17483 |

这些数字纠正了两个本会写进本 ADR 的错误主张：

1. **"异步更快"不是无条件成立的。** 对空 sink，异步比同步慢 2.7 倍（184 vs 68 ns）——
   没有任何工作可以与之重叠，剩下的只有队列 mutex 的争用。异步的收益只在 sink 本身够慢时
   出现：对 file sink 是 7.1 倍（126 vs 890 ns），这才是它存在的理由。
2. **紧密突发下异步会大量丢弃。** file sink 那一行丢了 87.4%（17483/20000）。这不是缺陷，
   而是 drop-newest-and-count 策略按设计工作并如实上报；但它意味着 1024 槽只够吸收正常
   帧节奏下的抖动，不足以吸收一个 2 万次的紧密循环。判断依据是
   `droppedByCapacityCount()`，不是"日志看起来没丢"。

其余结果：

- 被剥离的级别真的零成本，且该分支只在 Release 才编译——Debug 全绿不构成对它的验证；
- 生产路径不再有 per-line `fflush`；console sink 全级别走 stderr，默认无缓冲，无需 flush
  即可到达；
- 记录自持字节，队列因此成立；任意线程可写；
- Error/Critical 在进程终止前必然落地；
- 玩家的 bug 报告自带产生它的那次运行的日志。

两个**有意接受**的代价：

- 慢 sink 会阻塞记 Error 的线程，上限是 flush 超时（默认 2000 ms）。已有测试锁住"卡住的
  sink 不导致死锁、只是有界等待"。将来接网络/遥测这类慢 sink 需给它独立队列或调低超时，
  但不能关掉 Error 自动 flush——那会丢掉最该保留的记录。
- `shutdown()` 的 `join()` 无超时。drain 线程此刻正在访问 `Diagnostics`，detach 后销毁是
  use-after-free；挂住比内存损坏更容易诊断。与 `BoundedTaskSystem::shutdownAndJoin()`
  同一取舍（它也是无超时 wait，deadline 版本是另一个入口）。

未验证项，全部需要真实设备或模拟器：

- logcat 分级是否如预期；
- drain 线程在 Android 进程冻结（onPause/onStop 后）下的行为；
- **file sink 在 Android 上是否真的启用。** EngineHost 经 `Core::userApplicationFilePath`
  解析路径，而 `UserPaths.cpp` 没有 Android 分支，只有 XDG/`$HOME` 回退。Zygote 通常会把
  `HOME` 设成应用私有目录（那样它可写、file sink 正常）；若 `HOME` 为空则返回 NotFound，
  EngineHost 安全降级为不开 file sink（不崩溃，但 Android 只剩 logcat）。两个方向都未验证。
  在设备上打印 `getenv("HOME")` 的实际值之后再决定是否给 `UserPaths` 加 Android 分支——
  **不要**改用 `ApplicationPaths`，它在 Android 明确返回 Unsupported 并要求调用方回退到
  per-user 路径。

## 首轮实现遗留的四个缺陷（已修复，2026-09-01）

上面的设计描述在首轮实现里有四处没有落到代码，另有一处报告未列出、读代码时发现：

1. **console sink 把 Trace/Debug/Info 送 stdout。** stdout 是机器可读证据通道，
   `run_benchmark_gate.py:_extract_child_report` 要求子进程恰好写一行非空 JSON。
   已反证：缺陷在位时 gate 报 `must write exactly one non-empty JSON line`；改为全级别
   stderr 后，在引擎启动路径插一条 `TINA_LOG_INFO` 仍 `status=ok`。产品 gate 因为用
   `Where-Object { $_ -match '^\{"status":"ok"' }` 过滤而容忍多余行，所以症状只在 bench
   gate 出现。
2. **`shutdown()` 的残余 drain 循环无锁读写 ring。** 与 `tryEnqueue` 的加锁写构成数据竞态。
   改为持 `m_queueMutex` 取记录、放锁后 `deliver()`——不能在持 `m_queueMutex` 时调
   `deliver()`，因为 `flush()` 的顺序是 `m_queueMutex` → `m_sinkMutex`，反序即新死锁。
   同时给 `tryEnqueue` 加 `isOpen()` 复查：`shutdown()` 在取该 mutex 之前就清了 `m_open`，
   所以复查要么先拿到锁并被残余循环排空，要么后拿到锁并被拒绝。
3. **sink 里调 `flush()` 自锁。** `deliver()` 持 `m_sinkMutex` 调用户 sink，`flush()` 也取
   同一个非递归 mutex。`t_inSink` 此前只在 `deliver()`/`write()` 检查。已在 `flush()` 顶部
   加早退返回 `false`。已反证：去掉该早退后测试挂死 30 秒无输出。
4. **`Detail::log` 按值收参数包。** `TINA_LOG_INFO("c", "x={}", someStdString)` 因此堆分配，
   与 `LogFormat.hpp` 的"从不分配"承诺矛盾。改为 `const Values&...`。已用重载
   `operator new` 计数反证：100 条日志从 **200 次**分配降到 **0 次**（每条 2 次，不是 1 次）。
5. **报告未列出：`flush()` 读 `std::thread` 成员与 `join()` 竞态。** `m_drainThread.joinable()`
   / `get_id()` 与 `shutdown()` 的 `join()` 并发即 UB。改用 `isAsync()`；join 之后
   `m_stopping` 会让等待立刻返回，语义不变。

`LogSinkFn` 的头文件注释此前只说"递归被抑制"，与实现不符（`flush`/`shutdown` 不检查守卫）。
已改为逐条写明：write 被拒并计入 sink failure，flush 返回 false 且不 flush，
shutdown 被接受但留 drain 线程自行退出。

**一条不能声称已验证的结论。** 缺陷 2 的新测试
（`ShutdownInterleavedWithWritersDeliversEachRecordExactlyOnce`）确实到达了残余 drain 循环
——给该循环加计数器后，无界 writer 的形态多数轮次命中，有界 writer 的形态 **0 次命中**
（所以有界写法的测试在缺陷代码上必然通过，等于没测）。但它**不会**在缺陷代码上失败：
残余循环运行时 ring 里只有 1 条记录，无锁更新丢失的表现就是丢掉那一条，而"写入撞上 close
被合法拒绝"在外部完全同形。缺陷代码 6/6 通过该测试。

该竞态因此是**靠阅读证明、靠构造消除**的：无锁非原子访问与加锁访问并发，按定义是 UB。
测试锁住的是重复投递与幽灵投递，不是丢记录。要确定性检出需要 TSAN 一类工具，MSVC 上没有。

## 被拒绝方案

- **引入 spdlog**：大量使用 `<format>`，违反公共头政策，整个模块无法为 Android 编译。
  改用 `std::format` 自建前端同样被这条政策排除。
- **用 bx 的 `BX_TRACE`**：`tina_core` 会为了记日志而依赖 bgfx 栈。
- **无锁队列**：省一次 mutex，代价是极难验证的内存序缺陷。`BoundedTaskSystem` 已确立
  mutex + condition_variable 的房子模式，日志没有理由偏离它。上表也显示队列争用只在
  sink 极快时才是主成本，而那种场景本就该用同步。
- **队列满时覆盖最旧**：突发的起因比尾部更有诊断价值。
- **无界队列**：内存随日志速率增长直到被 OOM killer 杀掉；有界 + 计数把它变成可观测的
  负载信号。
- **保留 per-line `fflush`**：stderr 默认无缓冲，Warn 以上仍立即到达，Error 以上另有显式
  flush，所以取消它不牺牲致命信息的可达性。
- **让 `LogRecord::message` 继续借用、改为要求调用方保证生命周期**：把一个编译期无法检查
  的约束推给每个调用点，失效表现为读已释放内存，且在同步模式下根本不显现。
- **每条记录自动 flush**：等于放弃异步。只有 Error 及以上需要这个保证。
- **多 sink 用虚接口 + 动态注册**：引入没有消费者的抽象；函数指针 + 不透明 userData 与
  既有风格一致。
- **公共头暴露 `std::filesystem::path`**：配置用 `std::string_view`，`Create` 立即复制，
  `<filesystem>` 只留在 `.cpp`。
- **`defaultChannel()` 用惰性单例**：引入静态初始化与退出顺序问题。改为 EngineHost 显式
  赋值的 `std::atomic<Diagnostics*>`（`g_assertHandler` 的既有模式）；赋值前与清空后返回
  关闭通道，写入是 no-op。
- **在宏里为被过滤的日志计数**：那次原子自增会比它本该省下的检查更贵。
  `droppedByLevelCount()` 因此只统计到达 `write()` 的记录，语义已写进头文件注释。
