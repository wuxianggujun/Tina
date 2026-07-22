# Core

`Tina::Core` 提供 Runtime 各模块共用的最小 C++23 基础。公共头位于 `include/tina/core`，不暴露
xxHash、EASTL、spdlog 或平台 SDK 类型。

## 当前能力

| 子域 | 已实现 |
| --- | --- |
| Base | 固定宽度类型、Platform/Compiler、SourceLocation、EnumFlags、`ScopeExit` |
| Error | C++23 `std::expected` 的 `Result<T>`/`Status`、稳定 domain/code、origin、native code、UTF-8 context chain |
| Time | `Duration`、`MonotonicTimePoint`、`IMonotonicClock`、`SteadyMonotonicClock`、`FixedStepAccumulator` |
| Diagnostics | `TINA_ASSERT`、`LogLevel/LogRecord`、`DiagnosticChannel`、Engine-owned `Diagnostics` 与私有 console sink |
| Memory | `MemoryTag`、`MemoryTracker`、`CountingMemoryResource`、owning `FrameArena` |
| ID | `GenerationId/GenerationPool`、`AssetId` |
| Hash | 128-bit `ContentHash` 与 PRIVATE XXH3-128 digest adapter |
| IO/Text | strict UTF-8 helpers、有界 `readFile`、`createParentDirectories`、`writeFile` 与 atomic sibling replace |

不在当前 Core 的能力：通用线程池、Asset job、Runtime event queue、全局 allocator 替换、MetricsRegistry、
Trace/Tracy session、CrashContext、callstack 符号化和通用 Tina STL。

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
- `writeFile(path, bytes, config)`：默认创建父目录，并使用同目录唯一临时文件 + rename/replace；
- 空路径、目录、非法 UTF-8、容量、permission 与 OS error 返回结构化 Error。

路径 canonicalization、sandbox/root containment 属于具体 Cooker/Asset 输入策略，不由 Core
`writeFile()` 自动猜测。

## Diagnostics

`Diagnostics` 由 `EngineHost` 在其他模块前创建、在模块 shutdown 后最后关闭。模块只持有不可拥有的
`DiagnosticChannel`。当前默认 sink 同步输出到 console；级别短路不写 sink，sink 失败计数且不递归，
shutdown 后 channel 写入为 no-op。

当前没有 file sink、异步日志队列、MetricsRegistry、TraceZone/Tracy adapter 或 CrashContext。日志不得
包含 token、密钥、用户正文和不必要的绝对路径；Audio callback/异常信号路径不调用普通日志。

## Legacy compatibility 残留

Legacy 产品 target 已删除，但 Core 文件层还不是零残留：

- `src/core/CMakeLists.txt` 仍编译私有 `time/Clock.cpp`、`Clock.hpp` 与 `FrameTimer.hpp`；
- `src/core/utils/StringUtils.hpp` 仍 include EASTL，当前不在 `tina_core` source list，需先证明无消费者；
- 当前产品公共 API 使用 `MonotonicClock.hpp`/`FixedStepAccumulator.hpp`，不能把上述 compatibility 文件
  写成正式 Game SDK。

这些项由 `CLEAN-002` 处理。删除前必须全仓库扫描 include/target/source consumer；不能仅凭“私有”或
“未列入 source list”直接删除用户未提交代码。

## 验证

Core 修改至少运行：

```powershell
cmake --build --preset windows-vnext-debug --target tina_tests tina_sample_null -- /m:1 /v:m
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_sample_null.exe --frames=300
```

测试覆盖 Result、ScopeExit、time、UTF-8、Read/WriteFile、hash、memory、generation、assert/diagnostics 与
header isolation。Linux sanitizer 与正式 benchmark 见 [测试说明](testing.md)和 `TEST-001`/`PERF-001`。
