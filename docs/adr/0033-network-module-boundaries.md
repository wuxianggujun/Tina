# ADR 0033：网络模块边界与跨线程完成契约

- 状态：Proposed
- 日期：2026-08-28
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

## 待确认决策

本 ADR 为 Proposed，下表为待 maintainer 追认的决策点。

**实施状态（2026-08-28）：** 项目 owner 已授权按下表推荐值实施，UDP datagram 首切片
（D1）已落地，`ErrorDomain::Network = 15` 与 `MemoryTag::Network = 13` 已写入 Core
共享头。因此本表记录的是**已执行的选择**，而非待选项；若 maintainer 否决其中任何一条，
需先更新本 ADR 再回滚对应实现。

**D3 与 D7 已于 2026-08-28 撤销并由 D11 取代。** 原 D3 把问题设成「worker 干完活结果
怎么回到 owner 线程」，这个提问本身预设了 worker 的存在，而该前提是错的：TCP/TLS 的
慢在**等**而不在**算**，等待可以不占线程。撤销理由见下方 D11 与「决定」第 12 节。

| # | 决策点 | 推荐 | 主要备选与取舍 |
| --- | --- | --- | --- |
| D1 | 首切片范围 | **UDP datagram only** | HTTP first：更常用，但引入 TLS + DNS + curl 三块第三方面，契约与依赖同时变动；WebSocket first：还要叠 RFC 6455 帧解析。UDP 无 TLS、无 DNS（收数值地址）、无协议解析，是能完整验证 C1-C6 的最小切片 |
| D2 | 模块切分 | **neutral `Tina::Network` + 独立 adapter** | 单一 `tina_network` 全包：cocos2d-x 正是如此，结果 HttpClient 分裂成三份复制粘贴（其一 31KB 无人编译）。neutral + adapter 复用 `Tina::Audio` / `Tina::AudioMiniaudio` 既有先例 |
| ~~D3~~ | ~~完成通知机制~~ | **已撤销，见 D11** | 撤销于 2026-08-28。原推荐（Asset 式 per-request 状态 + 原子标志）与备选（`postMain`/`pumpMain`）都在用线程模拟等待。`postMain` 另有两处实测代价：它与 `scheduleIo`/`scheduleCpu` 共用同一把 `m_mutex`（`BoundedTaskSystem.cpp:325`，三个 queue 全在其下），故每个完成事件都与所有任务提交争锁；且 `TaskCallable` 是 `std::move_only_function<void()>`，每个事件一次堆分配，与稳态零分配冲突 |
| D4 | `ErrorDomain` 是否新增 `Network = 15` | **新增** | 复用 `Platform`：语义不符，且 `AssetFormat` 复用 `Asset` domain 的做法依赖显式值域划分注释（`AssetErrors.hpp:7`），网络无自然归属。新增即触碰共享头，须与其他并行工作协调 |
| D5 | `MemoryTag` 是否新增 `Network` | **待定** | 新增会把 `MemoryTagCount` 从 13 改到 14，连带改 `memoryTagName()` switch 与 `PublicHeaderIsolationTests` 的 static_assert；复用 `Core` tag 则诊断粒度变粗 |
| D6 | TLS 策略（若 D1 选 HTTP/WSS） | **默认使用系统信任库，校验失败即失败** | 见"决定"第 5 节。跳过校验必须显式、刺眼、且 Release 不可生效 |
| ~~D7~~ | ~~取消语义~~ | **已简化，见 D11** | 撤销于 2026-08-28。跨线程取消需要「不再投递完成」这种绕的承诺，因为无法中断已进入 syscall 的操作。在单线程 readiness 模型下取消就是把状态机置为 Cancelled 并关闭 socket，同步完成、无需跨线程协调。仍不承诺远端未收到已发出的字节 |
| D8 | 完成顺序 | **允许乱序，按完成时序投递** | 严格 FIFO：见背景，队头阻塞 |
| D9 | 可靠 UDP / netcode 是否在范围内 | **不在** | 序号/ack/重传/分片重组/拥塞控制/快照 delta/客户端预测是独立子系统，需单独 ADR |
| D10 | 第三方选型 | **传输层零第三方；TLS 另议** | 平台 socket + readiness API 足以实现 UDP/TCP。TLS 必须选可非阻塞驱动的库（OpenSSL memory BIO 或 mbedTLS 自定义 BIO），不接受自带线程或自带事件循环的库。注意 libuv 已因 lws 被链入但项目从不调用（`cocos/` 侧 `uv_` 零命中），不构成可用基础 |
| D11 | 传输的并发模型 | **owner-thread readiness 多路复用，不引入 worker 线程** | 取代 D3/D7。每帧一次 `WSAPoll`/`epoll_wait`（timeout=0）覆盖全部 socket，然后推进各自状态机。线程池方案：与非原子 `FixedRing`（`AudioEngine.cpp:29-89` 的 head/tail/count 均非 atomic）、稳态零分配、单线程 mutation 三条既有约束处处冲突。代价是要写状态机，且 TLS 需非阻塞接口 |
| D12 | DNS 解析 | **切出本 ADR，先只支持数值地址** | `getaddrinfo` 阻塞且无可移植非阻塞版本，是 readiness 模型下唯一真正需要线程的部分。它应作为独立切片，届时那个窄口子才是 `postMain` 的合理首个用户 —— 而非整条网络关键路径 |

## 决定（Proposed）

### 1. 本 ADR 不实现任何协议

本轮交付物只有契约。协议实现、第三方依赖接线、构建接线、测试目录均在 D1-D10
确认之后另行开工。现存 `src/network/`、`include/tina/network/` 草稿**不得增量扩写**
—— 它依赖不存在的枚举成员与不采用的测试惯例，须在实现开始时整体重写。

### 2. Owner-thread 单线程 mutation

所有公开 mutation 方法第一条语句校验 owner 线程，返回
`NetworkErrorCode::WrongOwnerThread`，与 `AudioEngine`
（`src/audio/AudioEngine.cpp:408-424`）和 `PhysicsWorld2D` 同一形态。owner 线程在
`Create` 时捕获。

按 D11，传输层**不存在 worker 线程** —— 所有 socket 状态只由 owner 线程读写，因此
不需要原子量、不需要锁、不需要 marshal。这不是"暂时没做多线程"，而是有意的模型
选择：既有的非原子 `FixedRing`、稳态零分配与单线程 mutation 三条约束都建立在这个
前提上。

### 3. 固定容量、Create 一次性分配、稳态零分配

容量在 `Create` 的 Config 中声明并校验非零，存储用 `std::pmr::vector` 在 `Create`
时 `resize()` 到定容，运行期只改内容不改 size；scratch 用 `std::fill` 清理而非
`clear()`/`resize()`。查找用 `occupied` 标志 + 线性扫描（`GenerationPool` 无迭代器
，见 `src/audio/AudioEngine.cpp:1445`，而网络每次 `pump()` 需全量扫描）。这套形状
照 `PhysicsNavigationSync2D`（`src/asset/PhysicsNavigationSync2D.cpp:33-62` 的
Create、`:64-83` 的线性查找、`:233-237` 的 `clearPlan`）。

`Error` 构造会分配（C4），故成功路径永不构造 `Error`；容量耗尽等预期失败返回
带 message 的 `Error` 是可接受的一次性分配。稳态零分配须由分配计数门禁证明，
形态照 `tests/asset/Sprite2DBindingRegistryTests.cpp:1430-1457`。

### 4. 三阶段 plan → preflight → apply

`pump()` 遵循与 `PhysicsNavigationSync2D::synchronize()`
（`src/asset/PhysicsNavigationSync2D.cpp:239-377`）相同的结构：先只写 scratch 完成
plan，再一次性预检全部容量，最后按固定顺序 apply。任一阶段失败保留上一次成功
发布的状态，不留部分生效的中间态。

### 5. TLS 默认安全，且不可静默降级

默认使用平台系统信任库并完整校验证书链与主机名。校验失败即请求失败，不降级、
不自动加例外。跳过校验只能经显式命名的 opt-in 字段开启，该字段在 Release 配置下
不生效。CA 来源、证书固定（pinning）是否支持留待 D6。

这条直接针对 cocos2d-x 的三层不安全默认：`HttpClient.cpp:193-195` 在 CA 为空时关闭
`VERIFYPEER`/`VERIFYHOST`；`CCDownloader-curl.cpp:390-391` 无条件关闭且无开关；
`WebSocket.cpp:845` 降级为 `ALLOW_SELFSIGNED | SKIP_SERVER_CERT_HOSTNAME_CHECK`。
其 Apple 死代码更进一步，在 `kSecTrustResultRecoverableTrustFailure` 时用
`SecTrustSetExceptions` 把导致失败的原因加入白名单（`HttpClient-apple.mm:185-191`）。

### 6. 取消是一等公民，语义明确

每个在途操作有 generation-safe 句柄，句柄级 `cancel()` 是公开 API 的组成部分而非
后续扩展。语义明确为"不再向 owner 投递完成事件"，**不承诺**已发出的字节未送达、
不承诺远端未收到。这一点必须写进公开头注释，避免 cocos2d-x 那种
`clearResponseAndRequestQueue` 只能清未出队请求、已进入 `curl_easy_perform` 的请求
无法中断而契约又不说明的状态。

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

### 13. DNS 是唯一的线程例外，且切出本 ADR

`getaddrinfo` 阻塞且无可移植的非阻塞版本，是 readiness 模型下唯一真正需要线程的
部分。因此：传输层先只接受数值地址（UDP 切片已如此），DNS 作为独立切片单独设计。

那个切片才是 `postMain` 的合理首个用户 —— 一个窄口子、低频调用、失败可重试，而不是
整条网络关键路径。在它落地前，需要域名的调用方自行解析并传入数值地址。

## 结果

- 网络模块的 owner/容量/线程/失败/取消/TLS 语义在实现之前被冻结，避免重演
  cocos2d-x 那种"默认不安全 + 无取消 + 无界队列"三件套。
- UDP datagram 首切片已落地并通过门禁，证明第 2/3/7/8 节的形状在真实平台 socket 上
  可实现且零第三方依赖。第 10/11 节是从该实现的缺陷复盘补写的，对后续传输同样生效。
- 第 12 节取代原 D3/D7 后，网络不再需要 worker 线程，因此 `postMain`/`pumpMain`
  （至今零生产调用点）不必由整条网络关键路径充当首个用户；该角色移交给第 13 节的
  DNS 切片。UDP 与未来 TCP 因此共用同一个并发模型，而不是两套。
- 成本与限制：readiness 层是新建基础设施（仓库当前对 `epoll`/`WSAPoll`/IOCP/
  `io_uring`/`kqueue`/`select` 零使用）；TCP 需手写连接状态机；TLS 受限于可非阻塞
  驱动的库；需要域名的调用方在 DNS 切片落地前必须自行解析。第 12/13 节改变了本 ADR
  的并发结论，若 maintainer 倾向线程池模型，须先更新本 ADR 再实现。
- 需要建立的门禁：readiness 层的 pump 在无 socket、全部 idle、部分 ready 三种情形下
  的行为；连接状态机每个状态转移的定向测试；单次 pump 的 syscall 与工作量上限；
  取消发生在每个状态时的清理完整性；稳态零分配计数门禁。
- `postMain`/`pumpMain` 是否成为首个生产用户被显式提为 D3，而不是在实现中偶然
  决定。
- 明确了 Asset 的 completed-prefix 顺序**不适用**于网络，避免照抄出队头阻塞。
- 成本与限制：D1 未定则无法开工；D4/D5 涉及修改 `Core` 共享头
  （`Error.hpp` 的 `ErrorDomain`、`MemoryTag.hpp` 的 `MemoryTagCount` 及其
  static_assert），与并行工作有冲突面，须先协调再落地。可靠 UDP、netcode、HTTP/2、
  HTTP/3、DNS 缓存、代理、断点续传均不在本 ADR 范围。首切片不提供 Editor 或
  sample 消费面。
- 需要建立的门禁：每个公开头对应一个 `tests/network/header_isolation/*Header.cpp`
  单 TU；新公开头加入 `tests/core/PublicHeaderIsolationTests.cpp` 的 include 列表
  （ADR 0027 §9.1）；`cmake/VerifyInstalledTinaSdkHeaders.cmake` token 清单扩充并由
  `tools/windows/RunSdkConsumerGate.ps1` 覆盖；容量耗尽 / stale 句柄 / 错误 owner /
  取消后不投递 / 超限拒绝的定向单测；稳态零分配的分配计数门禁；若引入第三方依赖，
  新增 `tests/sdk_consumer_network*` consumer gate 与对应 vcpkg feature。测试可执行
  文件直接运行，不注册 CTest（ADR 0006）。

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
- **worker 池 + 完成事件回传（原 D3 的两个候选）**：无论用 Asset 式 per-request 原子
  标志还是 `postMain`/`pumpMain`，本质都是用线程模拟等待。`postMain` 与任务提交共用
  一把锁且每事件一次堆分配；per-request 原子标志则要为每个连接维护跨线程状态。两者
  都比 readiness 多路复用更复杂且更慢，见第 12 节。
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
