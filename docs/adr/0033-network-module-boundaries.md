# ADR 0033：网络模块边界与跨线程完成契约

- 状态：Accepted
- 日期：2026-08-28（2026-08-29 按已落地实现校准）
- 决策者：Tina maintainers

## 背景

Tina 当前没有任何网络能力：`include/tina/` 下无 network 模块，`ErrorDomain`
（`include/tina/core/error/Error.hpp:15-30`）也没有对应 domain。仓库里存在
`src/network/` 与 `include/tina/network/` 两个目录，但它们**不参与构建** —— 根
`CMakeLists.txt:260-283` 的 `add_subdirectory` 列表和 `tests/CMakeLists.txt:133-165`
都没有它们。这份代码引用了不存在的 `Core::ErrorDomain::Network`，使用了项目明确
不采用的 `gtest_discover_tests`（ADR 0006），并在模块内写了本地 `install()`
（安装规则统一在 `cmake/TinaGameSdkPackage.cmake`）。它是草稿，不是基线，本 ADR
不把它当作既有实现。

网络的真实成本不在协议实现，而在它同时压到六条已生效的桌面契约上。

| # | 现有契约 | 位置 | 网络为何冲突 |
| --- | --- | --- | --- |
| C1 | Task 系统只报告"工作是否被接受"，无 per-task 完成通知 | `include/tina/task/TaskSystem.hpp:71,75` | 请求完成必须由调用方自己发现 |
| C2 | 无 per-task 取消，只有整体 `requestStop()` | `include/tina/task/TaskSystem.hpp:84,90` | 切场景中止在途请求是网络刚需 |
| C3 | `postMain`/`pumpMain` 存在但生产代码零调用点 | `src/task/bounded/BoundedTaskSystem.cpp:135,154` | 网络会是它的第一个真实用户 |
| C4 | `Error` 含 `std::string` + `std::vector<ErrorContext>`，构造即分配 | `include/tina/core/error/Error.hpp:62,65` | 稳态零分配要求成功路径永不构造 Error |
| C5 | `ErrorDomain` 与 `MemoryTag` 均为闭合 append-only 枚举 | `Error.hpp:15-30`、`memory/MemoryTag.hpp:12-27` | 新增成员会改共享头；`MemoryTag` 还有 `MemoryTagCount == 13U` 的 static_assert（`tests/core/PublicHeaderIsolationTests.cpp`） |
| C6 | 公开安装头禁止出现第三方 token，扫描器持固定清单 | `cmake/VerifyInstalledTinaSdkHeaders.cmake:29-47` | curl/OpenSSL 类型必须完全不可见 |

还有一条**不能照抄**的既有模式：Asset 的异步完成采用 completed-prefix 顺序
（`src/asset/AssetSystem.cpp:1445-1452`），遇到第一个未完成请求即 `break`，以保证
"worker 完成时序不决定 generation 可见顺序"（`:1292-1293`）。这对 asset 正确，对
网络则是队头阻塞：慢请求会挡住已完成的快请求。网络必须允许乱序完成。

参考实现调研：cocos2d-x v4.0（MIT，但 `Uri.cpp/h` 为 Apache-2.0 folly 移植，
libwebsockets 2.4.2 为 **LGPL-2.1 + 静态链接例外**，其署名声明是强制义务）。它的
网络模块提供了明确的反面教材，详见"被拒绝方案"。其 C++ 引擎层**没有任何 UDP**
（`SOCK_DGRAM`/`recvfrom`/`sendto` 在 `cocos/`、`extensions/` 零命中），故 UDP 方向
无可参考先例。

## 决策记录

本节记录已定案的决策点。**实现先于追认** —— owner 逐轮口头授权推进，模块整体落地后本
ADR 才转 Accepted。这个顺序本身是流程偏差，留在此处而非抹平：它使本文档有一段时间与
代码不符，而下面的「与首版的偏差」正是那段时间累积的账。

**D3 与 D7 已撤销并由 D11 取代。** 原 D3 把问题设成「worker 干完活结果怎么回到 owner
线程」，这个提问预设了 worker 的存在，而该前提对 TCP/TLS 是错的：慢在**等**而不在
**算**，等待不必占线程。理由见 D11 与第 12 节。

| # | 决策点 | 推荐 | 主要备选与取舍 |
| --- | --- | --- | --- |
| D1 | 首切片范围 | **UDP datagram only** | HTTP first：更常用，但引入 TLS + DNS + curl 三块第三方面，契约与依赖同时变动；WebSocket first：还要叠 RFC 6455 帧解析。UDP 无 TLS、无 DNS（收数值地址）、无协议解析，是能完整验证 C1-C6 的最小切片 |
| D2 | 模块切分 | **neutral `Tina::Network` + 独立 adapter** | 单一 `tina_network` 全包：cocos2d-x 正是如此，结果 HttpClient 分裂成三份复制粘贴（其一 31KB 无人编译）。neutral + adapter 复用 `Tina::Audio` / `Tina::AudioMiniaudio` 既有先例 |
| ~~D3~~ | ~~完成通知机制~~ | **已撤销，见 D11** | 撤销于 2026-08-28。原推荐（Asset 式 per-request 状态 + 原子标志）与备选（`postMain`/`pumpMain`）都在用线程模拟等待。`postMain` 另有两处实测代价：它与 `scheduleIo`/`scheduleCpu` 共用同一把 `m_mutex`（`BoundedTaskSystem.cpp:325`，三个 queue 全在其下），故每个完成事件都与所有任务提交争锁；且 `TaskCallable` 是 `std::move_only_function<void()>`，每个事件一次堆分配，与稳态零分配冲突 |
| D4 | `ErrorDomain` 是否新增 `Network = 15` | **新增** | 复用 `Platform`：语义不符，且 `AssetFormat` 复用 `Asset` domain 的做法依赖显式值域划分注释（`AssetErrors.hpp:7`），网络无自然归属。新增即触碰共享头，须与其他并行工作协调 |
| D5 | `MemoryTag` 是否新增 `Network` | **已新增** | 落地时 `MemoryTagCount` 由 13 改为 14，并同步 `memoryTagName()` switch 与**两处** static_assert（`PublicHeaderIsolationTests.cpp:39`、`MemoryTrackerTests.cpp:50`）。复用 `Core` tag 则诊断粒度变粗 |
| D6 | TLS 信任锚来源 | **平台信任库优先，显式锚点覆盖，不内嵌 bundle** | 见第 5 节与 D13。跳过校验必须显式、刺眼、且 Release 不可生效 |
| ~~D7~~ | ~~取消语义~~ | **已简化，见 D11** | 撤销于 2026-08-28。跨线程取消需要「不再投递完成」这种绕的承诺，因为无法中断已进入 syscall 的操作。在单线程 readiness 模型下取消就是把状态机置为 Cancelled 并关闭 socket，同步完成、无需跨线程协调。仍不承诺远端未收到已发出的字节 |
| D8 | 完成顺序 | **允许乱序，按完成时序投递** | 严格 FIFO：见背景，队头阻塞 |
| D9 | 可靠 UDP / netcode 是否在范围内 | **不在** | 序号/ack/重传/分片重组/拥塞控制/快照 delta/客户端预测是独立子系统，需单独 ADR |
| D10 | 第三方选型 | **传输层零第三方；TLS 另议** | 平台 socket + readiness API 足以实现 UDP/TCP。TLS 必须选可非阻塞驱动的库（OpenSSL memory BIO 或 mbedTLS 自定义 BIO），不接受自带线程或自带事件循环的库。注意 libuv 已因 lws 被链入但项目从不调用（`cocos/` 侧 `uv_` 零命中），不构成可用基础 |
| D11 | 传输的并发模型 | **owner-thread readiness 多路复用，不引入 worker 线程** | 取代 D3/D7。每帧一次 `WSAPoll`/`epoll_wait`（timeout=0）覆盖全部 socket，然后推进各自状态机。线程池方案：与非原子 `FixedRing`（`AudioEngine.cpp:29-89` 的 head/tail/count 均非 atomic）、稳态零分配、单线程 mutation 三条既有约束处处冲突。代价是要写状态机，且 TLS 需非阻塞接口 |
| D12 | DNS 解析 | **独立切片，`scheduleIo` + owner 轮询** | `getaddrinfo` 阻塞且无可移植非阻塞版本，是 readiness 模型下唯一真正需要线程的部分。**首版曾推荐 `postMain`，实现时改用 `scheduleIo` + per-request `atomic<Outcome>` + owner 轮询** —— 见 D14 |
| D13 | 是否自建 DNS over UDP 以保持单线程 | **不自建** | 只有 c-ares 走这条路，代价是放弃 `getaddrinfo`/nsswitch 背后的一切：NSS 模块、DNSSEC、mDNS `.local`、split-horizon/VPN 分流、search domain、Windows NRPT。c-ares#134 记录了它采集到 loopback/VirtualBox 的 site-local server 并排在真实 resolver 之前，造成「multi-second delays of up to 14 seconds」；curl 维护者 Eissing：「it can never reach 100% equality」，且「Many distributions building curl do therefore not enable c-ares」 |
| D14 | DNS 的完成回传机制 | **`scheduleIo` + per-request 原子标志 + owner 轮询** | 取代 D12 首版推荐的 `postMain`。三条实测理由：`pumpMain` **不捕获异常**（`BoundedTaskSystem.cpp:175-179` 无 try/catch，而 `workerLoop:296-306` 有），任务抛出会逃到调用方且该任务已出队无法重试；`postMain` 与 `scheduleIo`/`scheduleCpu` 共用同一把 `m_mutex`（`:325`，三个 deque 全在其下）；`TaskCallable` 是 `std::move_only_function<void()>`，每事件一次堆分配。而 `AssetSystem.cpp:1328-1419` 的 per-request 模式在本仓库已有验证，且不吃这三项代价。**但不照抄它的 completed-prefix 顺序**（`:1445-1452`），那对网络是队头阻塞 |

## 与首版的偏差

本 ADR 首版写在实现之前，随后模块整体落地。以下是文档与代码分歧过、现已按代码校准的
条目。列出而非改写，因为「为什么当初那样想」和「为什么最后没那样做」都是决策的一部分。

| 处 | 首版所写 | 实际实现 | 为什么改 |
| --- | --- | --- | --- |
| §1 | 「本 ADR 不实现任何协议」 | UDP/TCP/TLS/HTTP/WebSocket/DNS/Listener 全部落地 | 该句描述的是首版交付范围，模块推进后成为陈述性错误 |
| §3 | scratch 用 `std::fill` 清理 | 用 `memmove` 压缩前缀 | 网络缓冲是**流**而非槽位表：消费一个前缀后剩余字节必须搬到头部保持连续，调用方拿到的是 `span`。`std::fill` 适合 `PhysicsNavigationSync2D` 那种定长槽位，不适合流 |
| §3 | 查找用 `occupied` + 线性扫描 | 仅 `DnsResolver` 与 `ReadinessPoller` 如此 | UDP/TCP 没有槽位表可扫；它们是单 socket 加缓冲区 |
| §4 | `pump()` 走 plan → preflight → apply | TCP/TLS/HTTP/WebSocket 是状态机 | 三阶段适合「一次发布一批相互依赖的变更」；协议推进是**逐步**的，每步都可能只完成一部分（部分写、半个 frame），没有可整体回滚的批次 |
| §5 | 「默认使用平台系统信任库」 | 首版实现**不读** store，要求调用方自带锚点 | 文档原本是对的、实现落后。2026-08-29 已按 D6 补齐平台 store 读取 |
| §6 | cancel 语义是「不再投递完成」 | readiness 传输是同步关闭；只有 DNS 是延迟语义 | 单线程 readiness 下取消可以同步完成，不需要那条绕行承诺。它只对无法中断的 `getaddrinfo` 成立 |
| §8 | 句柄用 `Core::GenerationId` | `DnsQueryHandle` 自建 slot + generation | 公开头引 `GenerationId.hpp` 会把 Core 的 ID 体系拉进网络的公开面，而 DNS 只需要两个 `u32`。generation-safe 的语义保留了 |
| D5 | 「待定」 | 已新增 | 表格未随实现更新，与同节上方段落自相矛盾 |
| D12 | DNS 用 `postMain` | 用 `scheduleIo` + 轮询 | 见 D14 的三条实测理由 |

## 决定

### 1. 交付顺序

首版只交付契约；实现随后逐切片推进，每切片自带测试与门禁。现已全部落地，遗留缺口见
「结果」节。

### 2. Owner-thread 单线程 mutation

所有公开 mutation 方法第一条语句校验 owner 线程，返回
`NetworkErrorCode::WrongOwnerThread`，与 `AudioEngine`
（`src/audio/AudioEngine.cpp:408-424`）和 `PhysicsWorld2D` 同一形态。owner 线程在
`Create` 时捕获。

按 D11，传输层**不存在 worker 线程** —— 所有 socket 状态只由 owner 线程读写，因此
不需要原子量、不需要锁、不需要 marshal。这不是"暂时没做多线程"，而是有意的模型
选择：既有的非原子 `FixedRing`、稳态零分配与单线程 mutation 三条约束都建立在这个
前提上。

唯一例外是 `DnsResolver` 的 worker（第 13 节）：它只写自己独占的 per-request 状态，
以一次 release store 发布，从不触碰 owner 的容器。

### 3. 固定容量与 Create 一次性分配

容量在 `Create` 的 Config 中声明并校验非零，存储用 `std::pmr::vector` 在 `Create`
时 `resize()` 到定容，运行期不改 size。

**槽位表与流用不同的清理方式。** `DnsResolver`/`ReadinessPoller` 是定长槽位表，查找用
`occupied` 标志加线性扫描（`GenerationPool` 无迭代器，见 `AudioEngine.cpp:1445`）。而
UDP/TCP/HTTP/WebSocket 的接收缓冲是**流**：调用方消费一个前缀后，剩余字节必须 `memmove`
到头部以保持连续，因为交出去的是 `span`。首版写的 `std::fill` 只适合前者。

`Error` 构造会分配（C4），故成功路径永不构造 `Error`；容量耗尽等预期失败返回带 message
的 `Error` 是可接受的一次性分配。稳态零分配须由分配计数门禁证明，形态照
`tests/asset/Sprite2DBindingRegistryTests.cpp:1430-1457`。

### 4. 协议推进是状态机，不是批量发布

首版要求 `pump()` 走 `PhysicsNavigationSync2D::synchronize()` 那种
plan → preflight → apply。实现没有采用：那个形状适合「一次发布一批相互依赖的变更」，
而协议推进是**逐步**的 —— 一次 `send` 可能只走掉一半、一个 frame 可能只到一半 —— 没有
可整体回滚的批次。

取而代之的不变量是：**任何一步失败都保留上一次成功发布的状态**。未发完的尾部留在队列
里，未解析完的字节留在缓冲里，都不产生部分生效的中间态。

### 5. TLS 默认安全，且不可静默降级

完整校验证书链与主机名。校验失败即失败，不降级、不自动加例外。跳过校验需要两个独立
opt-in（`InsecureSkipVerify` 加 `allowInsecureVerification`），且 Release 构建直接拒绝。

**信任锚来源：平台 store 优先，显式锚点覆盖，不内嵌 bundle。** 调用方留空
`trustAnchorsPem` 即使用平台 store；提供锚点则**替换**而非追加平台集合，使固定私有 CA
的调用方不会仍然信任所有公共 CA。这是 Godot 的优先级模型（显式 bundle 覆盖胜出，OS
store 其次）。

**不内嵌 CA bundle。** 内嵌意味着一份会过期的负债 —— Godot 保留内嵌 bundle 仅作为无法
读取 store 的平台的兜底，而正是那份兜底会过期。平台不可读时本模块选择明确失败并要求
调用方提供锚点，而不是静默用一份陈旧快照。

**读锚点不等于把裁决交给 OS。** 本实现提取一份扁平锚点列表交 mbedTLS 判定，因此平台的
distrust 记录、EKU 约束、CTL 状态与吊销信息**均未被查询**。这是行业普遍限制：Godot、
UE5（非 Apple）、Unity 都是如此，只有 UE5-on-Apple 与 `rustls-platform-verifier` 真正
委托裁决。rustls 自己的文档措辞是 system store「with no (dis)trust decisions. All roots
are treated equally regardless of their status」。具体后果：macOS store 仍列出 Apple
按日期屏蔽的 StartCom 锚点，扁平快照无法表达这条。该限制写在公开头而非藏在实现里。

**一次读取并缓存。** 平台 store 在进程内只读一次。成本是实测的：naive 实现会「populate
the x509 store again for every *new* connection」，而解析成本随 OpenSSL 版本从 4.5ms
跳到 50ms（openssl#16878），macOS 在 Go 的测量里到秒级。Godot 在 `main.cpp` 启动时读一
次，UE5 在 `FSslModule::StartupModule()` 读一次，本模块同此。

这条针对 cocos2d-x 的三层不安全默认：`HttpClient.cpp:193-195` 在 CA 为空时关闭
`VERIFYPEER`/`VERIFYHOST`；`CCDownloader-curl.cpp:390-391` 无条件关闭且无开关；
`WebSocket.cpp:845` 降级为 `ALLOW_SELFSIGNED | SKIP_SERVER_CERT_HOSTNAME_CHECK`。
其 Apple 死代码更进一步，在 `kSecTrustResultRecoverableTrustFailure` 时用
`SecTrustSetExceptions` 把导致失败的原因加入白名单（`HttpClient-apple.mm:185-191`）。

### 6. 取消语义按传输分层

每个在途操作有 generation-safe 句柄，句柄级 `cancel()` 是公开 API 而非后续扩展。

**readiness 传输（UDP/TCP/TLS/HTTP/WebSocket）的取消是同步的** —— 置状态并关闭 socket，
当场完成。单线程模型下不需要「不再投递完成」这种绕行承诺。

**只有 DNS 是延迟语义。** `getaddrinfo` 无法中断，所以取消标记 slot 为 abandoned，直到
worker 发布才回收；立刻复用会让 worker 写进后续查询占用的 slot。

两者都**不承诺**已发出的字节未送达、不承诺远端未收到。这写在公开头，避免 cocos2d-x 那种
`clearResponseAndRequestQueue` 只能清未出队请求、已进入 `curl_easy_perform` 的无法中断
而契约又不说明的状态。

### 7. 所有接收路径有显式上限

单条消息/数据报、单次响应体、接收缓冲总量、在途操作数、待投递完成事件数，全部
有 Config 声明的上限，超限是明确错误而非无界增长。cocos2d-x 的
`_requestQueue`（`HttpClient.h:215`）、WebSocket 的 `_receivedData`
（`WebSocket.cpp:1112`）和 Downloader 的 `_requestQueue`
（`CCDownloader-curl.cpp:692`）均无上限，服务端或中间人可用单个巨帧打满内存。

**输出有上限不等于工作量有上限。** 被丢弃的输入不消费输出槽位，因此排空循环还需要
独立的 syscall 预算，否则持续发送不可用输入的对端能让单次调用一直做 syscall。UDP
切片取容量的两倍。

### 8. 公开头不暴露第三方类型

采用 opaque `struct Impl;` + `Impl* m_impl` 的 pimpl，配 `GenerationId` 句柄，形态照
`include/tina/physics2d/PhysicsWorld2D.hpp:123-134` 与
`include/tina/audio/miniaudio/MiniaudioDevice.hpp:51-54`。Tag 定义为
`namespace Detail` 内的空 `final struct`，句柄为 `using` 别名，公开头只 include
`<tina/core/id/GenerationId.hpp>` 而非 `GenerationPool.hpp`
（照 `include/tina/physics2d/PhysicsIds.hpp`）。引入依赖时须同步扩充
`cmake/VerifyInstalledTinaSdkHeaders.cmake:29-47` 的禁止 token 清单。

### 9. 不复制参考实现的代码

cocos2d-x 本体为 MIT，允许复制，但本 ADR 明确只借鉴设计取舍，不移植代码。若未来
决定 vendor 其 WebSocket 实现，libwebsockets 2.4.2 的 LGPL-2.1 署名声明
（"based in part on the work of the libwebsockets project"）成为强制义务，且修改
lws 本身会使改动落回 LGPL 第 1/2/4 节。`Uri.cpp/h` 为 Apache-2.0，与仓库其余部分
许可证不同，须单独处理。

### 10. 丢弃与延迟必须分开计数

「因为超限而永久丢弃」与「因为本次批次已满而留到下一次」是两种不同故障，混为一个
计数器会让「该扩单包上限」和「该扩队列容量」读起来一样。前者计入 discard，后者不计。
discard 计数与成功计数必须不相交。

### 11. 上限值本身必须可用

对外声明的单包上限是**可完整收发的**，不是「接近就会失败」。POSIX `recvfrom` 在
datagram 超出缓冲区时静默截断，使「恰好等于缓冲区」与「被截断」无法区分；实现必须
让缓冲区严格大于对外声明的上限，从而把区分能力建立起来，而不是把边界值一并丢弃。

这条是从首轮实现的真实缺陷提炼的：接收 slot 原本等于 `maximumDatagramBytes`，于是
`send()` 允许的最大 datagram 在接收侧被当作疑似截断丢弃 —— 一个只在恰好取边界值时
出现的静默丢包。任何新增传输都必须为其声明的上限提供边界值测试。

### 12. 传输统一使用 owner-thread readiness 多路复用

所有传输（UDP、TCP 及其上的协议）都以「每帧一次非阻塞 readiness 查询 + 推进状态机」
实现，不引入 worker 线程、不引入锁、不引入跨线程 marshal。

具体形状：owner 每帧调用一次 `pump()`，内部对全部 socket 做一次
`WSAPoll`/`epoll_wait`（timeout=0），对每个 ready 的 socket 推进其状态机
（connect 完成 → 发请求 → 读响应 → 解析），完成的操作写进固定容量完成队列供调用方
读取。一次 syscall 覆盖所有连接。

**为什么不是线程池。** TCP 与 TLS 的耗时在**等待**而非**计算**，而等待不必占用线程。
把等待交给线程会同时撞上三条既有约束：非原子的 `FixedRing`
（`src/audio/AudioEngine.cpp:29-89` 的 head/tail/count 均非 atomic）、稳态零分配、
以及单线程 mutation。`postMain` 具体还有两处实测代价：它与 `scheduleIo`/`scheduleCpu`
共用同一把 `m_mutex`（`BoundedTaskSystem.cpp:325`），故每个完成事件都与所有任务提交
争锁；`TaskCallable` 是 `std::move_only_function<void()>`，每个事件一次堆分配。

**这个模型让 D3 消失、D7 退化为同步操作。** 没有 worker 就没有「结果怎么回来」的问题；
取消是把状态机置为 Cancelled 并关闭 socket，同步完成，不需要「不再投递完成」这种因
无法中断 syscall 而不得不做的绕行承诺。

**已承认的代价。** 一是必须手写状态机，比「扔给线程等着」费心，但那是一次性成本，
换掉的是线程模型的持续复杂度。二是 TLS 必须使用可由调用方驱动的非阻塞接口
（OpenSSL memory BIO 或 mbedTLS 自定义 BIO），自带线程或自带事件循环的库不可接受。
三是 DNS 例外，见第 13 节。

**尚未存在的基础设施。** 仓库当前对 `epoll_create`、`WSAPoll`、
`CreateIoCompletionPort`、`io_uring`、`kqueue`、`select` 的命中数全部为零，因此
readiness 层是新建而非复用。UDP 切片因每帧 `receive()` 已是此模型的退化形式（单
socket、无需 poll），故与本节一致，不需要改造。

### 13. DNS 是唯一的线程例外

`getaddrinfo` 阻塞且无可移植非阻塞版本，是 readiness 模型下唯一真正需要线程的部分。
这与全行业一致，不是本项目的妥协：libuv 用 4 线程池（且 DNS 受
`slow_work_thread_threshold` 限制，默认池下并发只有 **2**）、Boost.Asio 每 execution
context 一条 resolver 线程、libcurl 8.20 起用线程池（`CURLMOPT_RESOLVE_THREADS_MAX`
默认 20）。Godot 更保守：**单条** resolver 线程串行处理，256 个待处理槽位，缓存无 TTL
无淘汰。

唯一的例外是 c-ares —— 它自己讲 DNS 协议以保持单线程，代价见 D13，本模块不走那条路。

**完成回传用 `scheduleIo` + per-request 原子标志 + owner 轮询，不用 `postMain`。**
理由见 D14。传输层只接受数值地址，域名解析是调用方显式调用 `DnsResolver` 的一步 ——
不在 `HttpRequest` 内部隐式解析，因为那会让每个协议都依赖 task system。

## 结果

- 全部传输已落地并有测试证据：UDP、readiness poller、TCP、TLS（含真实握手）、
  HTTP/1.1、HTTPS、WebSocket、wss、DNS、TcpListener。`IByteStream` 接缝使 HTTP 与
  WebSocket 各只有一份实现，`ws`/`wss` 与 `http`/`https` 的差别只是传入哪个流。
- 除 DNS 外零 worker 线程、零锁、零跨线程 marshal。因此 `postMain`/`pumpMain` 至今
  仍是零生产调用点 —— D14 决定连 DNS 也不用它，那三条实测代价（不捕获异常、共用
  `m_mutex`、每事件堆分配）对任何用户都成立，不只对网络。
- 第 10/11 节是从实现缺陷复盘补写的，对后续传输同样生效。第 11 节尤其：
  `MaximumDatagramBytes` 原本发得出收不到，是只在恰好取边界值时出现的静默丢包。
- 成本与限制：
  - **Linux 一次没验证。** 十个组件的 POSIX 分支写了但从未编译或运行过，包括
    `SystemTrustStore.cpp` 的 bundle 路径探测。这是当前最大的未知面。
  - **所有测试在 loopback。** 真实丢包、乱序、重复、路径 MTU 分片、NAT 一概未覆盖；
    发送缓冲区满的 `WouldBlock` 分支与 `receive()` 的 syscall 上限分支在 loopback 上
    无法稳定触发。
  - **不委托验证裁决给 OS**（第 5 节），因此 distrust 记录/EKU/CTL/吊销未被查询。
  - **无证书固定（pinning）。** UE5 有 `[SSL] +PinnedPublicKeys`，本模块没有。
  - 不在范围：可靠 UDP、netcode、HTTP/2、HTTP/3、DNS 缓存与 TTL、代理、断点续传、
    以及委托式验证。
- 已建立的门禁：每个公开头一个 `tests/network/header_isolation/*Header.cpp` 单 TU；
  `VerifyInstalledTinaSdkHeaders.cmake` 的 token 清单已扩充 socket/Winsock/mbedTLS 并
  以合成泄漏头反向验证；容量耗尽 / stale 句柄 / 错误 owner / 取消后不投递 / 超限拒绝
  的定向单测；稳态零分配的分配计数门禁；`tina_sample_network` 作为 tests 之外的首个
  消费者，在同一 pump 循环里跑通全部组件。测试可执行文件直接运行，不注册 CTest
  （ADR 0006）。
- 尚未建立的门禁：`tina_sample_network` 未接入 product gate 脚本；`RunSdkConsumerGate.ps1`
  的完整 relocated consumer gate 未随 `Tina::NetworkTls` 复跑。

## 被拒绝方案

- **直接扩写现存 `src/network/` 草稿**：它引用不存在的 `ErrorDomain::Network`、
  使用 `gtest_discover_tests`、含本地 `install()`，且未接线构建。在错误形状上增量
  只会放大偏差。
- **单例 + 全局访问点**：cocos2d-x 的 `HttpClient` 是文件级裸指针单例，
  `getInstance()` 无锁（`HttpClient.cpp:343-351`），`destroyInstance()` 先置空全局
  指针再等 `_threadCount` 归零（`:362-363`、`:621-636`），期间 `getInstance()` 会造出
  第二个实例；更糟的是 `configureCURL` 内部用 `getInstance()` 而非传入的形参
  （`:183,187`），可在关闭过程中复活单例。且引擎从不调用 `destroyInstance()`。
  Tina 以 `EngineHost` 为唯一非全局组合根，不引入第二个全局。
- **单 worker 串行 + 无界队列**：cocos2d-x 一条 worker 串行跑
  `curl_easy_perform`，队列无上限无背压，要并发只能用 `sendImmediate`，而它每次
  new 一条 OS 线程（`HttpClient.cpp:472`），Lua 的 XHR 正走这条路。
- **每请求一线程**：与 `BoundedTaskSystem` 的有界 worker 池直接冲突。
- **worker 池 + 完成事件回传（用于传输层）**：无论用 per-request 原子标志还是
  `postMain`/`pumpMain`，对**传输**都是用线程模拟等待，比 readiness 多路复用更复杂且
  更慢，见第 12 节。注意这条只拒绝把它用在传输层 —— DNS 无法用 readiness 表达，那里
  per-request 原子标志正是采用的方案（D14）。
- **内嵌 CA bundle**：一份会过期的负债。Godot 保留内嵌 bundle 只作为无法读取 store 的
  平台的兜底，而正是那份兜底会过期（其 `ca-bundle.crt` 在 4.6 才从
  `ca-certificates.crt` 改名，官方文档至今仍链接死路径）。平台不可读时明确失败胜过静默
  使用陈旧快照。
- **自建 DNS over UDP**：见 D13。放弃 nsswitch 背后的一切换取单线程，代价过高。
- **引入自带事件循环的网络库（libuv、asio、libcurl multi 的内建循环）**：会把第二个
  调度器带进只有一个 owner 线程的运行时。libuv 虽已因 libwebsockets 被链入 cocos2d-x，
  但那份树里 `uv_` 零调用，不构成先例。
- **回调在 worker 线程直接触发**：违反 owner-thread mutation 契约，会破坏
  非原子的固定容量结构（`FixedRing` 的 head/tail/count 均非 atomic，
  `src/audio/AudioEngine.cpp:29-89`）。
- **按平台复制粘贴实现**：cocos2d-x 的 Android HttpClient 除 `processResponse` 外
  约 350 行是 `HttpClient.cpp` 的复制，已漂移出真实行为差异（404 在 Android 算
  成功、在 curl 算失败；gzip 一开一关；`getCookie()` 返回值随平台变化）。同仓库的
  `IDownloaderImpl` 后端抽象是正确示范。
- **`sleep` + 自旋掩盖同步问题**：cocos2d-x WebSocket 的 `close()` 里
  `sleep_for(5ms)` 配注释 "Wait 5 milliseconds for onConnectionClosed to exit!"
  （`WebSocket.cpp:736-737`），对端是 `for(;;)` 反复 `notify_one` 直到状态翻转
  （`:1244-1253`）。这是 condition variable 缺谓词导致的丢失唤醒，不是可接受的
  workaround。
- **手写协议解析**：cocos2d-x SocketIO 用 `+2/-3/+9/-11` 魔法偏移
  （`SocketIO.cpp:884-885`）和混用两套坐标系的索引运算（`:984-985`），而 RapidJSON
  已链入且在写路径上使用 —— 读路径不用是主动选择。协议解析要么用库，要么写完整
  状态机加完整校验。
- **在首切片内做可靠 UDP / netcode**：见 D9。
