# 网络

`Tina::Network` 提供 backend-neutral 传输与协议客户端。契约见
[ADR 0033](adr/0033-network-module-boundaries.md)。

## 组件

| 组件 | 公开头 | 说明 |
| --- | --- | --- |
| `IpAddress` / `NetworkEndpoint` | `network/NetworkEndpoint.hpp` | 数值地址与端口；严格 literal 解析，RFC 5952 规范化输出 |
| `UdpSocket` | `network/UdpSocket.hpp` | 非阻塞 datagram 收发 |
| `TcpConnection` | `network/TcpConnection.hpp` | 非阻塞客户端连接；实现 `IByteStream` |
| `TcpListener` | `network/TcpListener.hpp` | 非阻塞 accept，交出的连接与拨出的同类型 |
| `IByteStream` | `network/ByteStream.hpp` | 传输中立的字节流接缝 |
| `HttpRequest` | `network/HttpClient.hpp` | HTTP/1.1 客户端，跑在任意 `IByteStream` 上 |
| `WebSocket` | `network/WebSocket.hpp` | RFC 6455 客户端，同上 |
| `DnsResolver` | `network/DnsResolver.hpp` | 名字解析；模块内唯一使用 worker 的部分 |
| `TlsConnection` | `network/tls/TlsConnection.hpp` | 可选适配器（`TINA_BUILD_NETWORK_TLS`），mbedTLS；实现 `IByteStream` |

`http`/`https` 与 `ws`/`wss` 不是两份实现:协议只认 `IByteStream`,递
`TcpConnection` 得到明文,递 `TlsConnection` 得到加密。

传输层零第三方依赖,只用平台 socket API;TLS 适配器用 mbedTLS,且 mbedTLS 类型不出现在
任何公开头中。

**不在范围内**:可靠 UDP 通道(序号/ack/重传/分片重组/拥塞控制)、快照同步、客户端
预测、NAT 穿透、HTTP/2、HTTP/3、DNS 缓存与 TTL、代理、断点续传、证书固定。可靠 UDP
与 netcode 由 ADR 0033 的 D9 排除,需单独 ADR。

## 并发模型:每帧一次 readiness 查询

所有传输都由调用方驱动:`pump()` 做一次非阻塞 readiness 查询
(`WSAPoll`/`poll`,timeout=0)覆盖全部 socket,然后推进各自状态机。**没有 worker
线程、没有锁、没有跨线程 marshal。**

这不是"暂时没做多线程"。TCP 与 TLS 的耗时在**等**而非**算**,而等待不必占线程。线程池
方案会同时撞上三条既有约束:非原子的 `FixedRing`(`AudioEngine.cpp:29-89`)、稳态零
分配、单线程 mutation。

代价是必须每帧 `pump()`。漏调则内核缓冲区最终溢出并静默丢包 —— 传输不会替你重试。

`DnsResolver` 是唯一例外,见下。

## Owner 线程

`Create` 捕获调用线程。所有方法从其他线程调用返回 `WrongOwnerThread`,与
`AudioEngine`、`PhysicsWorld2D` 同一形态。

## 固定容量

`Create` 是唯一分配点。`receive()`/`peekReceived()` 返回的 span **借用**内部存储,只在
下一次 `pump()`/`consume()` 之前有效。

槽位表(`DnsResolver`、`ReadinessPoller`)用 `occupied` 标志加线性扫描;流缓冲
(UDP/TCP/HTTP/WebSocket)在消费前缀后 `memmove` 压缩以保持连续。

## 地址解析的严格性

`IpAddress::parse()` 只接受数值 literal,刻意拒绝几类看起来合法的输入:

- **带前导零的 octet**(`010.0.0.1`)—— `010` 在某些解析器里是八进制、另一些里是
  十进制,同一字符串会指向两个不同主机。这是真实的地址伪造手法。
- **方括号形式**(`[::1]`)—— 方括号属于 URI authority 语法,不属于地址本身。
- **主机名**、**带端口后缀**(`127.0.0.1:8080`)、**带 scheme** —— 任何需要 resolver
  的输入一律拒绝;域名解析是 `DnsResolver` 的显式一步。

`format()` 做的是**规范化而非恒等回显**:解析接受 `::` 代表单个零组,格式化则展开它
(RFC 5952 禁止压缩单组)。故 `parse(format(a)) == a` 恒成立,`format(parse(s)) == s`
仅在 `s` 已 canonical 时成立。

## 数据报大小

`MaximumDatagramBytes` 为 1200,远低于 IPv4 理论上限 65507。超过路径 MTU 会在 IP 层
分片,单个分片丢失即导致整个 datagram 被丢弃。

**恰好等于上限的 payload 可完整收发。** 这靠一个实现细节:接收 slot 比声明上限多一个
字节。POSIX `recvfrom` 超限时静默截断,无法区分「恰好等于缓冲区」与「被截断」;多留
一字节后只有真正超限的才会写到那格。超限的被丢弃并计入
`totalOversizedDatagramCount`,不会截断后当完整数据交出。

## TLS 信任锚

**平台 store 优先,显式锚点覆盖,不内嵌 bundle。**

留空 `trustAnchorsPem` 即使用平台信任库(Windows `CertOpenSystemStoreW(ROOT)`;POSIX
按顺序探测常见 bundle 路径)。提供锚点则**替换**而非追加平台集合 —— 固定私有 CA 的
调用方不会仍然信任所有公共 CA。这是 Godot 的优先级模型。

平台 store 在进程内**只读一次并缓存**。`tlsTrustStoreInfo()` 可在任何连接之前调用,
用于在启动时判断这个构建能否验证公网端点。

**不内嵌 CA bundle。** 内嵌是一份会过期的负债 —— Godot 保留它只作为无法读取 store 的
平台的兜底,而正是那份兜底会过期。平台不可读时本模块明确失败并要求调用方提供锚点。

**读锚点不等于把裁决交给 OS。** 本实现提取扁平锚点列表交 mbedTLS 判定,因此平台的
distrust 记录、EKU 约束、CTL 状态与吊销信息**均未被查询**。这是行业普遍限制(Godot、
UE5 非 Apple、Unity 同此;只有 UE5-on-Apple 与 `rustls-platform-verifier` 真正委托
裁决)。具体后果:macOS store 仍列出 Apple 按日期屏蔽的 StartCom 锚点,扁平快照无法
表达这条。

跳过校验需要**两个独立 opt-in**(`InsecureSkipVerify` 加 `allowInsecureVerification`),
且 Release 构建直接拒绝。一个静默接受任意证书的传输提供加密但不提供认证,比明确失败
更糟 —— 它看起来是work的。

## DNS

`getaddrinfo` 阻塞且无可移植非阻塞版本,所以 `DnsResolver` 把它交给 IO worker,由
`pump()` 收取结果。这与全行业一致:libuv 用 4 线程池(DNS 受
`slow_work_thread_threshold` 限制,默认并发只有 2)、Boost.Asio 每 context 一条
resolver 线程、libcurl 8.20 起用线程池。Godot 更保守:单条线程串行处理。

**唯一的另一条路是自己讲 DNS over UDP(c-ares 走的路),本模块不走。** 那会放弃
`getaddrinfo`/nsswitch 背后的一切:NSS 模块、DNSSEC、mDNS `.local`、split-horizon/VPN
分流、search domain、Windows NRPT。

**取消不能打断 `getaddrinfo`。** 所以取消标记 slot 为 abandoned,直到 worker 返回才
回收 —— 立刻复用会让 worker 写进后续查询占用的 slot。已解析的查询同样持有 slot 直到
`release()`,使答案不会在被读取前被覆盖。

数值地址也走同一路径并立即解析,所以调用方可以统一传名字或地址而不必特判。

## 失败语义

`send()` 成功只表示已交给 OS,**不表示送达**。UDP 无投递保证,TCP 也只是入队。

**`send()` 之后紧接 `shutdownSend()` 会丢弃排队字节。** `send()` 只入队,要靠后续
`pump()` 冲出;而 `shutdownSend()` 按设计丢弃未发出的排队字节。读起来像「发这个,然后
收尾」,实际不是。

`WouldBlock` 是瞬态的(发送缓冲区满),应在后续帧重试而非当作错误。

**丢弃与延迟分开计数。** 队列满是**延迟** —— 数据留在内核缓冲区等下次 `pump()`,不计入
丢弃。混为一个计数器会让「该扩队列容量」和「该扩单包上限」读起来一样。

`receive()` 在排空中途遇到硬失败仍返回已收集的数据,只有一个都没收到时才返回错误。

## 协议层的拒绝清单

HTTP 与 WebSocket 拒绝而非容忍下列输入,因为它们都是真实的攻击面:

- HTTP 同时带 `Content-Length` 与 `Transfer-Encoding`(请求走私)
- HTTP header 冒号前有空格(同上)
- HTTP body 短于声明长度 → `HttpIncompleteResponse`,不是「短成功」
- WebSocket 保留位置位(无协商扩展时非法)
- WebSocket 未知 opcode、被分片的控制帧、无消息进行中的 continuation
- 超过上限的消息 → 失败而非截断

WebSocket 客户端帧**强制掩码且每帧换掩码**:服务端遇到未掩码的客户端帧必须断开,复用
掩码会跨帧泄漏明文结构。未消费的消息**阻塞下一条**而非被覆盖。

## 构建与测试

传输层无条件构建。TLS 是可选适配器:

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug --target tina_network_tests tina_sample_network --parallel 2 -- /nr:false
out\build\windows-msvc-vnext\bin\Debug\tina_network_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_sample_network.exe --frames=300

cmake --preset windows-msvc-vnext-network-tls
cmake --build --preset windows-vnext-network-tls-debug --target tina_network_tls_tests --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-network-tls\bin\Debug\tina_network_tls_tests.exe --gtest_color=yes
```

测试直接运行,不注册 CTest(ADR 0006)。

`tina_sample_network` 是 tests 之外的首个消费者:headless、无 GPU、无 EngineHost,在
**同一个 pump 循环**里跑 UDP、DNS、TcpListener、三条客户端连接、HTTP 与 WebSocket,
输出带 `evidenceSchema` 的单行 JSON。

**2026-08-29 证据:** `tina_network_tests` 166/167(1 skip)、
`tina_network_tls_tests` 27/27、`tina_sample_network --frames=300` `status=ok` 且两次
运行逐字节一致、SDK 头扫描通过。TLS 握手、证书验证失败(主机名不匹配与无关签发者)、
应用数据往返、`close_notify`、HTTP over TLS、WebSocket over TLS 均有真实
mbedTLS 服务端对端验证。

## 未测到的部分

- **Linux 一次没验证。** 十个组件的 POSIX 分支写了但从未编译或运行过,包括
  `SystemTrustStore.cpp` 的 bundle 路径探测。这是当前最大的未知面。
- **所有测试在 loopback。** 真实丢包、乱序、重复、路径 MTU 分片、NAT 一概未覆盖。
- 发送缓冲区满的 `WouldBlock` 分支与 `receive()` 的 syscall 上限分支在 loopback 上
  无法稳定触发。
- `tina_sample_network` 未接入 product gate 脚本;`RunSdkConsumerGate.ps1` 的完整
  relocated consumer gate 未随 `Tina::NetworkTls` 复跑。
