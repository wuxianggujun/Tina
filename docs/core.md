# Core

`Tina::Core` 提供 Runtime 各模块共用的最小 C++23 基础。公共头位于 `include/tina/core`，不暴露
xxHash、EASTL、spdlog 或平台 SDK 类型。

## 当前能力

| 子域 | 已实现 |
| --- | --- |
| Base | 固定宽度类型、Platform/Compiler、SourceLocation、EnumFlags、`ScopeExit`、`MoveOnlyFunction`、`CancellationSignal`/`CancellationToken` |
| Error | C++23 `std::expected` 的 `Result<T>`/`Status`、稳定 domain/code、origin、native code、UTF-8 context chain |
| Time | `Duration`、`MonotonicTimePoint`、`IMonotonicClock`、`SteadyMonotonicClock`、`FixedStepAccumulator` |
| Diagnostics | `TINA_ASSERT`、`TINA_LOG_*` 编译期剥离宏、`LogLevel/LogRecord/LogFormat`、Engine-owned `Diagnostics` 与 console/file/platform sink，以及 opt-in `CrashHandler` 最后故障报告 |
| Trace | backend-neutral `TINA_TRACE_ZONE(nameLiteral)` 编译期 frontend；None + 可选 Tracy Profile backend |
| Memory | `MemoryTag`、`MemoryTracker`、`CountingMemoryResource`、owning `FrameArena` |
| ID | `GenerationId/GenerationPool`、`AssetId` |
| Hash | 128-bit `ContentHash` 与 PRIVATE XXH3-128 digest adapter |
| IO/Text | strict UTF-8 helpers、`convertUtf16ToStrictUtf8`、`parseStrictFloat`、有界 `readFile`、`createParentDirectories`、`writeFile` 与 atomic sibling replace |

不在当前 Core 的能力：通用线程池、Asset job、Runtime event queue、全局 allocator 替换、MetricsRegistry、
Trace session/capture 控制面、minidump/CrashContext、可移植 callstack 符号化、崩溃恢复和通用 Tina STL。

## Result 与失败边界

模块边界使用 `Result<T>`/`Status`。Error 包含稳定 `ErrorDomain + ErrorCode`、UTF-8 message、源位置、
可选 native integer code 与 context chain。外部输入、IO、容量、backend 创建等可恢复失败不得用 assert
替代；programmer invariant 使用 `TINA_ASSERT`。

`ScopeExit` 只接受可 `noexcept` 调用和移动的回滚动作。初始化每成功一步立即登记逆操作，避免出现
只能靠大块 cleanup 分支恢复的半初始化状态。

## 时间

`SteadyMonotonicClock` 只提供单调时间；墙钟/日历时间不是当前公共 Core API。Runtime 持有
`IMonotonicClock`，测试注入 manual clock，不依赖真实 sleep。

`FixedStepAccumulator::advance(realDelta, gameplayTimeScale)` 明确区分：

- 原始与接受/拒绝的 real delta；
- variable `updateDelta`；
- fixed delta、step count 与 interpolation；
- 超出单帧追赶预算后丢弃的 simulation delta。

默认 60 Hz、最大接受 real delta 250 ms、每帧最多4步。失败不修改 accumulator 已提交状态。

## Memory 与 generation

`MemoryTracker` 按 `MemoryTag` 记录 current/peak/alloc/free；`CountingMemoryResource` 为 PMR 调用提供
统计，不能声称追踪任意全局 pointer。`FrameArena` 创建时取得一次 backing block，支持 alignment、
OOM/overflow、高水位、epoch/reset，不做 heap fallback。

`GenerationPool<T, Tag>` 固定容量并自动分配 owner token。slot erase 后旧 ID 立即 stale；cross-pool/
cross-type ID 拒绝，generation wrap 时永久 retire slot。ID 是 Runtime identity，不能代替 `AssetId` 或
ContentHash。

## Hash 与身份

- `AssetId`：稳定 128-bit 逻辑身份；
- `ContentHash`：版本化 128-bit 非密码学内容摘要，用于 Cook/cache/非对抗损坏检测；
- generation ID：owner + slot + generation 的运行时句柄。

`digestContentHashV1()` 在 Core 实现侧 PRIVATE 调用 XXH3-128，按固定 little-endian 布局发布16字节。
公共头不出现 `xxhash.h`/`XXH*`。ContentHash 不是安全签名，类型之间不提供隐式转换。

## UTF-8 与文件

公开字符串/路径输入使用 strict UTF-8 且禁止 embedded NUL；Windows 实现进入 Win32/filesystem 边界时
显式转换，不依赖系统 ANSI code page。

当前 IO 已实现：

- `readFile(path, {maxBytes, memoryResource})`：只读 regular file，分配前检查大小，返回 owning PMR bytes；
- `createParentDirectories(path)`；
- `writeFile(path, bytes, config)`：默认创建父目录，并使用同目录唯一临时文件 + OS 原子 replace；Windows
  `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)` 与其他平台 rename 均不通过“先删除目标”降级；
- 空路径、目录、非法 UTF-8、容量、permission 与 OS error 返回结构化 Error；
- `userApplicationDirectory(appName, kind)` 与 `userApplicationFilePath(appName, fileName, kind)`：解析
  per-user `Config`/`State` 目录并返回 UTF-8 路径。**只做路径拼接，不碰文件系统**——创建目录由
  `writeFile()` 的 `createParents` 负责，因此只读环境不会在 resolve 阶段失败。Windows 优先
  `%LOCALAPPDATA%` 再 `%APPDATA%`；其它平台按 XDG basedir 用 `$XDG_CONFIG_HOME`/`$XDG_STATE_HOME`，
  缺失时回退 `$HOME/.config`、`$HOME/.local/state`。环境 base 必须是**绝对**路径，相对值会解析到进程
  工作目录而不是 per-user 位置，因此显式拒绝并返回 `NotFound`。`appName`/`fileName` 各须是单个
  UTF-8 path segment：空串、`.`、`..`、含分隔符或 `:` 一律 `InvalidArgument`，因为 segment 是逐字
  拼接的，不做净化。
- `applicationDirectory()` 与 `applicationFilePath(relativePath)`（`core/io/ApplicationPaths.hpp`）：
  解析**正在运行的可执行文件所在目录**，以及相对它的路径。这是随包只读数据的锚点：装出去的游戏会被
  拷到任意位置、并从别处启动，所以编译期绝对路径（只在编译它的那台机器上对）和进程工作目录（在玩家
  启动它的地方）都找不到自己的资产，只有 exe 位置可以。Windows 读已加载模块路径，其它平台读
  `/proc/self/exe`；返回 UTF-8，不含尾部分隔符。
  - 与 `UserPaths.hpp` 是两个**不同**问题，故不合并：那边答的是「产品可以往哪写」，按 application
    name 索引；这边答的是「产品的只读数据在哪」，除 exe 位置外无输入。
  - 同样**只做路径拼接，不碰文件系统**，所以只读或空安装目录里 resolve 不会失败，资产缺失由随后失败
    的读取报告，而不是由 resolve 报告。
  - `relativePath` 允许多级目录（随包数据本来就是分层摆放的），但必须用 `/` 作分隔符，让同一逻辑路径
    只有一种写法。逐字拼接，因此凡是可能解析到 exe 目录**之外**的值一律 `InvalidArgument`：绝对路径、
    含 `\`、含 `:`、`.`、`..`、以及任何空组件（含 `//` 与尾部 `/`）。
  - 根目录会保留其分隔符：`""` 是相对路径，而 `C:` 指的是 C 盘的当前工作目录而非根目录。

路径 canonicalization、sandbox/root containment 属于具体 Cooker/Asset 输入策略，不由 Core
`writeFile()` 自动猜测。

## Diagnostics

`Diagnostics` 由 `EngineHost` 在其他模块前创建、在模块 shutdown 后最后关闭。模块只持有不可拥有的
`DiagnosticChannel`。级别短路不写 sink，sink 失败计数且不递归，shutdown 后 channel 写入为 no-op。
全部成员可从任意线程调用。

调用点用 `TINA_LOG_*`（`Log.hpp`），它按 `TINA_LOG_LEVEL_COMPILED` 在编译期剥离低于该常量的级别——
语句整体消失，参数不被求值。Debug 保留 Trace，其余配置从 Info 起；`TINA_LOG_CRITICAL` 永不剥离。
测试与持有 channel 的代码用 `TINA_LOG_TO(channel, level, ...)`。格式化是自研的 `{}` 占位符
（`LogFormat.hpp`），不用 `<format>`——见 [ADR 0039](adr/0039-logging-frontend-and-async-sinks.md)。

`DiagnosticsConfig` 默认同步；`asyncQueueCapacity` 非 0 时启一个 drain 线程与该容量的有界队列，
满时丢弃最新并累加 `droppedByCapacityCount()`。Error 及以上在 `write()` 返回前完成投递，因为仓库
有 149 处 `std::terminate()`。`filePath` 非空时叠加 file sink（追加、按 `fileRotateBytes` 轮转并保留
一个 `.1` 备份、父目录自动创建、打开失败不使 `Create` 失败）。`platformDebugSink` 在 Windows 走
`OutputDebugStringA`（仅当已附加调试器）、Android 走 logcat。EngineHost 用 1024 槽异步 + 每用户
`tina.log`（8 MiB 轮转）。

异步不是无条件更快：sink 极快时队列争用反而使它变慢，收益只在 sink 本身慢时出现（实测见 ADR 0039
结果表）。仍然没有 MetricsRegistry 或 Trace session/capture 控制面。日志不得包含 token、密钥、
用户正文和不必要的绝对路径；Audio callback/异常信号路径不调用普通日志。

`<tina/core/diagnostics/CrashHandler.hpp>` 是与上述日志 owner 分离的进程级最后兜底。应用应在任何 factory、线程或
窗口创建前显式调用 `installCrashHandler()`；`EngineHost`/Desktop 不自动安装。handler 覆盖
`std::terminate()` 与 `SIGABRT`，Windows 还覆盖选定 fatal SEH、pure virtual call 和 CRT invalid parameter。
Windows 的 fatal SEH 有两个入口：`SetUnhandledExceptionFilter` 与更早触发的 vectored handler。**实际生效的
几乎总是 vectored** —— 它注册在前，而 report latch 是 first-wins，因此二者必须产生同样的 detail（access
violation 的读/写/执行分类与 faulting address）。这四类入口各自绕开 `std::terminate`，terminate hook 不能
作为它们的证据，故 `CrashHandlerTest` 用受控 death-test 子进程分别触发真实故障。
配置字符串在安装时复制到固定存储。**所有平台**在安装时都截断 report file 并写入 armed marker，因此报告
始终描述当前这次运行，且「文件只有 marker」与「文件不存在」可区分——前者表示进程死在 handler 观察不到的
地方，后者表示 handler 从未安装。Windows 额外在安装时预打开并保持 handle，故障时无需再打开文件；其他平台
在报告时以 append 模式重新打开。故障时写 application/reason/pid/tid（非 Windows 无 pid/tid）与
`{"status":"crash"...}`。

backtrace 的**符号解析目前仅 Windows 具备**（私有 DbgHelp），其他平台该段仍然出现但明确记为
`unavailable on this platform`——段落本身不会消失，否则读者无法区分「没有栈帧」与「没装 handler」。
`captureBacktrace` 的公开注释已按此说明限定，`CrashHandlerTest` 按平台分别断言两种形态。

只有第一份并发或级联故障会完整输出；`reportFatalAndTerminate()` 走同一漏斗并以非零状态结束进程。
`crashReportCount()` 是进程生命周期累计值，不在重复 install 时清零。该路径避免动态分配和 C++ iostream，
但仍依赖平台/CRT/符号服务，因此不是损坏堆栈下必然成功的 crash-safe dump 协议。当前 `TinaEditor.exe` 在最早期
安装，并将 crash 与顶层 fatal `Core::Error` 统一写入 `%TEMP%/tina_editor_crash.txt`；后者额外保留
domain/code、origin 与 context chain。若 `std::filesystem::temp_directory_path()` 不可用，Editor 回退到当前工作目录下的
同名文件；该回退不改变 CrashHandler 本身的 best-effort 边界。

## Trace

`<tina/core/trace/Trace.hpp>` 提供唯一 frontend 宏 `TINA_TRACE_ZONE(nameLiteral)`。根构建的
`TINA_TRACE_BACKEND` 只接受 `none` 或 `tracy`；`Tina::Core` 只向 consumer PUBLIC 传播 backend-neutral 的
None/Enabled frontend 定义，不传播具体 profiler 名称。None backend 把宏展开为不引用参数的空语句：
参数不求值，不构造 zone 对象，不调用函数，不分配内存，也不读取或创建全局状态。

Tracy backend 要求 vcpkg manifest feature `profile-tracy`。公共头仍只含 Tina-owned inline RAII zone：
`max_align_t` 对齐的固定 64-byte opaque storage 经 `Detail::constructZone/destroyZone` 进入
`Tina::TraceTracy` adapter；第三方 header、类型和 client implementation 留在 adapter 内。宏使用
`__COUNTER__` 生成唯一局部对象，并把 string literal 的静态长度与 `SourceLocation` 传给 adapter，
zone name 不执行运行时字符串扫描或分配。adapter 当前从 `SourceLocation` 读取 file/function metadata。
首切片不提供 session/capture 控制面。

Runtime 已在 `GameStateDispatchPhase::FrameUpdate` 的逐 State dispatch 内使用
`TINA_TRACE_ZONE("Runtime.GameState.UpdateFrame")`。None 构建中它完全编译消失；Tracy Profile 构建中，
同一 annotation 由可选 adapter 发布为 zone。普通 benchmark 仍使用 None，不能把 profiler capture
当作稳定性能 baseline。

## 三个替代标准库的 Base 类型（libc++ 缺口，非风格选择）

以下三个类型都不是「自研 STL」，而是因为 **libc++ 至今没有实现对应的标准库设施**，而 Android NDK 用
libc++：任何公共头一旦命名它们，整个模块就无法为 Android 编译。ADR 0007 允许在有明确消费者时实现少量
引擎专用结构（原文举例即含 `InlineFunction`），这三个各有唯一且已存在的消费者。

| 类型 | 替代 | libc++ 缺口 | 消费者 |
| --- | --- | --- | --- |
| `Core::MoveOnlyFunction` | `std::move_only_function` | NDK 28（libc++ 19）与 NDK 29（libc++ 21）都把 `__cpp_lib_move_only_function` 在 `<version>` 里注释掉；这是库缺口不是语言缺口，Clang 接受 `-std=c++23` | 全部 backend factory、`TaskCallable`、`PlatformEventCallback` |
| `Core::parseStrictFloat` | `std::from_chars`（浮点重载） | NDK 28 的 libc++ 无 `__charconv/from_chars_floating_point.h`，只有整数与 `to_chars` 两半；NDK 29 才补上 | `CatalogCook` 的 `parseFloatToken`、`parseNumberFieldValue` |
| `Core::CancellationSignal`/`CancellationToken` | `std::stop_token` | NDK 28 把它挡在 `_LIBCPP_HAS_NO_EXPERIMENTAL_STOP_TOKEN` 后；开 `_LIBCPP_ENABLE_EXPERIMENTAL` 会一次打开全部未完成特性，代价远大于本类型 | `executeSourceImportPipeline` |

三点刻意的取舍：

- **`MoveOnlyFunction` 有堆回退，不是纯 inline。** 最初写成 inline-only，被编译器证否：`TaskGroup::add`
  把调用者的 `TaskCallable` 再包一层以附加完成记账，外层 target 因此包含一整个 `MoveOnlyFunction`，
  **按构造**必然大于它要放进的缓冲区 —— 没有任何容量值能满足（128→256 只是重演同一失败）。本仓库真正
  的不变量是**有界队列与 arena** 不静默增长（队列满返回 `CapacityExceeded`），而 type-erased callable
  不属于那类。移动只搬指针，故仍是 noexcept 且不分配。
- **`parseStrictFloat` 不是 `strtof` 的薄封装。** `strtof` 会接受 `" 1.5"`、`0x1p3`、`inf`、`nan`，而
  `from_chars` 一个都不接受；替换若不显式拒绝这些，等于悄悄放宽了 cooked 文本的格式。`strtof` 的
  locale 敏感性在此无害：Tina 从不调用 `setlocale` 也不 imbue，进程终生停在 "C" locale。
- **`CancellationToken` 比 `std::stop_token` 小得多，是有意的。** 没有 `stop_callback` 注册、没有共享
  引用计数、没有侵入式回调链 —— Tina 的调用点从来只在工作项之间问一次 `stop_requested()`。桌面侧
  `editor_app` 保留 `std::jthread`，在 Asset 边界用一个 `std::stop_callback` 把 token 桥成 signal
  （选 callback 而非轮询：它对「调用前已经请求过取消」也会立刻触发，不会漏掉）。

## UTF-16 → 严格 UTF-8

`convertUtf16ToStrictUtf8()` 存在的原因是**平台 IME 说 UTF-16，而 Tina 的每一条文本契约都是严格 UTF-8**。
它放在 Core 而不是某个平台适配器里，因为这个缺口是通用的：Android 的 JNI 直接给 UTF-16，Windows 的 IMM32
同样如此。

Android 上不能用看似方便的 `GetStringUTFChars`：它返回的是 **modified UTF-8**，NUL 编码成两字节，且非 BMP
字符（emoji）以 **CESU-8 代理对**到达 —— 一个 emoji 会变成两个非法的三字节序列，被严格校验器拒绝，字符静默
丢失。

三条刻意的取舍：

- **只产最短形式。** 输出 overlong 序列会让这个函数自己的产物被下游校验器拒绝。
- **未配对代理、内嵌 NUL、输出溢出都返回 `nullopt`，绝不截断。** 截断出的半个多字节字符本身就是非法 UTF-8，
  忽略返回值的调用者会污染数据流。
- **失败时输出 span 可能含部分字节**，故返回的长度是唯一权威 —— 拿到 `nullopt` 就不得读该 span。这比每条
  失败路径都清空更省，而调用者本来就必须检查结果才知道长度。

## 时间与文本

产品公共 API 使用 `MonotonicClock.hpp` 与 `FixedStepAccumulator.hpp`。Legacy `Clock`/`FrameTimer`/
`StringUtils` compatibility 已删除（CLEAN-002）；`SteadyMonotonicClock` 实现位于
`src/core/time/MonotonicClock.cpp`。

## 验证

Core 修改至少运行：

```powershell
cmake --build --preset windows-vnext-debug --target tina_tests tina_sample_null --parallel 1 -- /nr:false
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_sample_null.exe --frames=300
```

测试覆盖 Result、ScopeExit、time、UTF-8、Read/WriteFile、hash、memory、generation、assert/diagnostics、
CrashHandler death/report-file/backtrace/idempotent install 与 header isolation。Linux sanitizer 与正式 benchmark 见 [测试说明](testing.md)和
`TEST-001`/`PERF-001`/`PERF-002`。
