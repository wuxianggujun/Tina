# Core

`Tina::Core` 提供 Runtime 各模块共用的最小 C++23 基础。公共头位于 `include/tina/core`，不暴露
xxHash、EASTL、spdlog 或平台 SDK 类型。

## 当前能力

| 子域 | 已实现 |
| --- | --- |
| Base | 固定宽度类型、Platform/Compiler、SourceLocation、EnumFlags、`ScopeExit` |
| Error | C++23 `std::expected` 的 `Result<T>`/`Status`、稳定 domain/code、origin、native code、UTF-8 context chain |
| Time | `Duration`、`MonotonicTimePoint`、`IMonotonicClock`、`SteadyMonotonicClock`、`FixedStepAccumulator` |
| Diagnostics | `TINA_ASSERT`、`LogLevel/LogRecord`、Engine-owned `Diagnostics`/console sink，以及 opt-in `CrashHandler` 最后故障报告 |
| Trace | backend-neutral `TINA_TRACE_ZONE(nameLiteral)` 编译期 frontend；None + 可选 Tracy Profile backend |
| Memory | `MemoryTag`、`MemoryTracker`、`CountingMemoryResource`、owning `FrameArena` |
| ID | `GenerationId/GenerationPool`、`AssetId` |
| Hash | 128-bit `ContentHash` 与 PRIVATE XXH3-128 digest adapter |
| IO/Text | strict UTF-8 helpers、有界 `readFile`、`createParentDirectories`、`writeFile` 与 atomic sibling replace |

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

路径 canonicalization、sandbox/root containment 属于具体 Cooker/Asset 输入策略，不由 Core
`writeFile()` 自动猜测。

## Diagnostics

`Diagnostics` 由 `EngineHost` 在其他模块前创建、在模块 shutdown 后最后关闭。模块只持有不可拥有的
`DiagnosticChannel`。当前默认 sink 同步输出到 console；级别短路不写 sink，sink 失败计数且不递归，
shutdown 后 channel 写入为 no-op。

普通 `Diagnostics` 当前没有 file sink、异步日志队列、MetricsRegistry 或 Trace session/capture 控制面。日志不得
包含 token、密钥、用户正文和不必要的绝对路径；Audio callback/异常信号路径不调用普通日志。

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
