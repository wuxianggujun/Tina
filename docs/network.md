# 网络

`Tina::Network` 提供 backend-neutral 传输。当前只有一个切片：非阻塞 UDP datagram。
契约见 [ADR 0033](adr/0033-network-module-boundaries.md)。

## 当前范围

已实现：

- `IpAddress`：IPv4/IPv6 数值地址，严格 literal 解析与 RFC 5952 规范化输出；
- `NetworkEndpoint`：地址 + 端口（公开接口一律 host byte order）；
- `UdpSocket`：owner-thread、固定容量、非阻塞 datagram 收发。

**不在当前范围内**（不是"尚未完成"，而是需要各自的决策才能开工）：TCP、HTTP、
WebSocket、TLS、DNS 解析、可靠 UDP 通道（序号/ack/重传/分片重组/拥塞控制）、
快照同步、客户端预测、NAT 穿透。可靠 UDP 与 netcode 由 ADR 0033 的 D9 明确排除，
需单独 ADR。

模块不依赖任何第三方库，只使用平台 socket API（Windows Winsock2、POSIX BSD
sockets），且这些类型完全不出现在公开头中。

## 为什么没有 worker 线程

`UdpSocket` 不持有线程，也不提供完成回调。`receive()` 排空内核已缓冲的 datagram
并按到达顺序返回，由调用方决定何时让它们可见。

这不是简化，而是三个后果：固定容量队列保持单线程因此无需原子操作；datagram 不会在
帧中途出现；调用方不需要为"回调发生在哪个线程"做防御。代价是必须每帧调用
`receive()`，漏调则内核缓冲区最终溢出并静默丢包。

因此本切片**不触发** ADR 0033 的 D3（worker→owner marshal），也没有在途操作可供
D7 取消。这两项留给未来的 TCP/HTTP 切片。

## Owner 线程

`Create()` 捕获调用线程作为 owner。`send()`、`receive()`、`localEndpoint()` 从其他
线程调用返回 `WrongOwnerThread`，与 `AudioEngine`、`PhysicsWorld2D` 同一形态。

## 固定容量与稳态零分配

`Create()` 是唯一分配点：按 `receiveQueueCapacity × maximumDatagramBytes` 一次性
`resize()` 出接收存储，之后 `send()`/`receive()` 不再增长。

`receive()` 返回的 span 及其中每个 payload **借用** socket 存储，只在下一次
`receive()` 之前有效。批次必须在下次调用前消费完。

`receiveQueueCapacity` 是硬上限：超过容量的 datagram 留在内核缓冲区,由下一次
`receive()` 取走,而不是扩容。

## 地址解析的严格性

`IpAddress::parse()` 只接受数值 literal，且刻意拒绝几类看起来合法的输入：

- **带前导零的 octet**（`010.0.0.1`）—— `010` 在某些解析器里是八进制、在另一些里是
  十进制，同一个字符串会指向两个不同主机。这是真实的地址伪造手法，所以拒绝而非猜测。
- **方括号形式**（`[::1]`）—— 方括号属于 URI authority 语法，不属于地址本身。
- **主机名**（`localhost`）、**带端口后缀**（`127.0.0.1:8080`）、**带 scheme**
  （`http://127.0.0.1`）—— 任何需要 resolver 或需要拆分的输入一律拒绝。

输出始终是 canonical 形式：IPv6 小写，压缩最长的零组连续段（并列时取最左），且
单个零组不压缩（RFC 5952）。

**`format()` 做的是规范化，不是恒等回显。** 解析接受 `::` 代表单个零组（如
`::2:3:4:5:6:7:8`），但格式化不允许产生这种写法，所以输出会是展开的
`0:2:3:4:5:6:7:8`。稳定的是**地址值**：`parse(format(a)) == a` 恒成立，而
`format(parse(s)) == s` 只在 `s` 本身已是 canonical 时成立。

## 数据报大小

`MaximumDatagramBytes` 为 1200，远低于 IPv4 理论上限 65507。超过路径 MTU 的
datagram 会在 IP 层分片，而单个分片丢失即导致整个 datagram 被丢弃 —— 1200 在常见
路径上不分片。

**恰好等于 `maximumDatagramBytes` 的 payload 可以完整收发。** 这一点靠一个实现细节
保证：每个接收 slot 比对外声明的上限**多一个字节**。POSIX 的 `recvfrom` 在 datagram
超出缓冲区时静默截断，无法区分「恰好等于缓冲区」与「被截断」；多留一字节后，只有
真正超限的 datagram 才会写到那一格，于是超限可检测而非靠猜。

超限的 datagram 被丢弃并计入 `totalOversizedDatagramCount`，不会截断后当完整数据
交出。Windows 以 `WSAEMSGSIZE` 显式报告，计入同一计数器。

## 失败语义

`send()` 成功只表示 datagram 已交给 OS，**不表示送达**。UDP 无投递保证。

`WouldBlock` 是瞬态的：内核发送缓冲区已满，调用方应在后续帧重试，而不是当作错误
上报。这与容量类错误不同。

`receive()` 在排空过程中遇到硬失败时，仍返回已经收集到的 datagram —— 丢弃它们会
导致调用方永远读不到这些数据。只有在一个都没收到时才返回错误。

## 统计

`statistics()` 分两组：上次 `receive()` 的工作量（`last*`）与累计值（`total*`）。

`totalDiscardedDatagramCount` 统计「传输层交出来但无法呈现」的 datagram，与
`totalReceivedDatagramCount` 不相交，包含三类：超限、零长度、以及 family 无法识别
的发送方。`totalOversizedDatagramCount` 是其中超限那一类的子计数。

**队列满不计入丢弃** —— 超出容量的 datagram 留在内核缓冲区等下一次 `receive()`，
是延迟而非丢失。把两者混为一谈会让「该扩队列容量」和「该扩单包上限」这两个不同
问题读起来一样。

`receive()` 单次调用的 syscall 次数也有上限（容量的两倍）。被丢弃的 datagram 不占
slot，若不额外限制，持续发送不可用 datagram 的对端能让这个循环一直做 syscall。

## 构建与测试

模块无条件构建（无 vcpkg feature、无 CMake option），因为它不引入第三方依赖。

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug --target tina_network_tests --parallel 2 -- /nr:false
out\build\windows-msvc-vnext\bin\Debug\tina_network_tests.exe --gtest_color=yes
```

测试直接运行，不注册 CTest（ADR 0006）。

**2026-08-28 证据：** `tina_network_tests` 47/47（`IpAddressTest` 23、
`NetworkEndpointTest` 1、`UdpSocketTest` 23）；同轮 `tina_tests` 413/413、
`tina_ui_tests` 836/836、`tina_runtime_ui_tests` 151/151、`tina_sample_null`
300 帧 exit 0，确认新增 `MemoryTag::Network` 与 `MemoryTagCount` 由 13 改 14 后
无回归。`VerifyInstalledTinaSdkHeaders.cmake` 扫描 292 个公开头（含 3 个 network 头）
通过，扫描器已扩充 socket/Winsock token 并以合成泄漏头反向验证 token 有效。

UDP 测试使用 loopback 与 ephemeral 端口（`port = 0`），不占用固定端口、可并行运行；
IPv6 用例在本机实际执行（非 skip），IPv6 不可用的宿主上 `GTEST_SKIP`。稳态零分配由
`SteadyStateSendReceivePerformsNoPmrAllocations` 以 100 次 send/receive 后分配计数
不变证明。`ExactlyMaximumSizedPayloadIsDelivered` 覆盖 1200 字节边界，
`OversizedDatagramIsDiscardedAndCounted` 用两个不同 `maximumDatagramBytes` 的 socket
构造真实超限，`FullQueueDefersWithoutCountingDiscards` 证明队列满是延迟而非丢弃。

失败路径另有两个用例：`RepeatedFailedCreateLeavesTransportUsable` 连跑 150 次失败
`Create` 后仍能正常收发，覆盖 Winsock 进程级 refcount 的平衡（泄漏会让库永不卸载，
过度释放会在活 socket 下卸载库，两者都不可直接观测）；`BindingAnAlreadyBoundPortFails`
证明重复 bind 被拒绝而非静默产生第二个同端点 socket。

V6 解析器是手写的，因此另有 `RejectsV6GroupCountBoundaryViolations`、
`AcceptsMaximalEmbeddedV4Form`（六组 + 嵌入 V4 = 恰好八组，最大合法形式）、
`EmbeddedV4InheritsOctetStrictness`（嵌入的 V4 同样拒绝前导零）与
`RejectsEmbeddedV4InNonTrailingPosition` 覆盖分组计数与位置约束的缝。

语义边界另有四个用例：`MovedFromSocketAnswersQueriesInertly`（moved-from 句柄的每个
访问器都惰性作答而非解引用空 impl，counter 归零而非透出目标 socket 的实时值）、
`ReportsConfiguredCapacities`、`LastCountersResetPerCallWhileTotalsAccumulate`
（`last*` 每次调用重置而 `total*` 持续累计），以及
`ReceiveBoundsSyscallsWhenEveryDatagramIsDiscarded` —— 后者是唯一实际执行 syscall
上限分支的用例，用连续超限 datagram 制造「全部被丢弃、不占 slot」的情形。

## 未测到的部分

自动测试全部在 loopback 上进行，因此以下均未覆盖：真实网络丢包、乱序、重复、
路径 MTU 分片、NAT 行为、跨主机 IPv6、防火墙拦截。内核发送缓冲区满导致的
`WouldBlock` 在 loopback 上难以稳定触发，其分支未被自动执行。

`CheckDocs.ps1` 本轮受 shell 权限限制未能执行；本文引用的 target
（`tina_network`、`tina_network_tests`）、preset 与相对链接已逐条手工核验存在。

## Linux 证据

**2026-08-28，Docker `tina-linux-gcc13:test-001`（Ubuntu 24.04 + GCC 13.3.0 + CMake
3.28.3）挂载仓库运行：** POSIX 分支首次编译，`tina_network` 与三个 header-isolation
TU **零 warning 零 error**；`tina_network_tests` **35/35**，与 Windows 逐项一致。

两个最可能出现平台差异的用例都通过：`ExactlyMaximumSizedPayloadIsDelivered` 与
`OversizedDatagramIsDiscardedAndCounted`。这验证了「slot 多留一字节」的做法在 POSIX
`recvfrom` 静默截断语义下**确实**能区分「恰好等于上限」与「超限」—— 该设计原本就是为
POSIX 而非 Windows 引入的，Windows 有 `WSAEMSGSIZE` 可直接判定。

`SupportsV6Loopback` 在容器内实际执行而非 skip（`disable_ipv6=0`，结果为 OK）。

同轮 Linux 回归：`tina_ui_tests` 836/836、`tina_runtime_ui_tests` 151/151、
`tina_ui_render_integration_tests` 32/32、`tina_sample_null` 300 帧 exit 0，`tina_tests`
412 通过 / 1 失败。唯一失败是 `CrashHandlerTest.BacktraceNamesTheCallingFunction`，
**与本模块无关**：`src/core/diagnostics/CrashHandler.cpp:247-252` 的 `emitBacktrace`
只有 Windows 实现，`#else` 分支输出 "unavailable on this platform"，而该测试未按平台
守卫就断言符号名。这属 `CORE-DIAG-001`（backlog 已记载「Linux terminate/abort 生成
可读 artifact 并明确无 backtrace」），本轮未修改。

vcpkg 首次 configure 因 `codeload.github.com` 下载 mikktspace 超时失败；把宿主
`$VCPKG_ROOT/downloads` 挂载进容器 `/opt/vcpkg/downloads` 后通过。后续 Linux 门禁
建议沿用该挂载。Linux build tree 为 `out/build/linux-gcc13-vnext`，与五个
`out/build/windows-msvc-*` 互不影响。
