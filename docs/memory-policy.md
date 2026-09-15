# 内存与容量策略

决策：[ADR 0052](adr/0052-demand-driven-memory-policy.md)；本批落地边界见
[ADR 0065](adr/0065-demand-grown-runtime-owners.md)。策略已接受；实现按 owner 迁移，不代表全引擎已经动态化。

## 原则

**按需拥有内存，热路径复用，按预算控制成本。固定容量不是全引擎不变量。**

- 普通内容增长不应因预估的元素数量不足而失败。资源登记、编辑器文档、场景与 UI 等长期数据优先按需增长。
- 按需不等于每次插入都重新分配恰好 `size + 1`。连续容器采用摊销增长；需要地址稳定时使用分块/分页存储与 generation handle。
- 帧内 scratch、packet、临时命令优先复用 arena，可在安全边界根据高水位预热/扩容，不要求全引擎永久零分配。
- 动态分配不等于无限内存。缓存回收无引用对象，队列提供背压；不能靠无限增长隐藏消费者过慢。
- 扩容或 OOM 仍必须保持事务、线程和所有权契约。

基础类型统一不等于更换 allocator：`Core::usize/u32` 等是精确标量别名，容器仍使用标准库与
`std::pmr`。复用已有 `CountingMemoryResource`/`MemoryTracker` 观察真实分配，再决定 owner 采用
普通 heap、pool 或 arena。只在 reset 前已结束借用且对象已析构的临时数据上使用 FrameArena；
Task capture、跨帧资源、retained UI snapshot 和 GPU 在途数据不得跟随 CPU 帧无条件 reset。
移除 Editor 数字解析中的临时 owning string 是局部工作量减少，不代表全引擎已实现零分配或全局 FPS 改善。

## 按场景选择

| 场景 | 推荐策略 | 必须保留的边界 |
| --- | --- | --- |
| UI 节点、Scene entity、编辑器文档 | 按需分块增长、稳定 handle、空槽复用 | ID 表示范围、真实内存不足 |
| Asset 登记与 retirement ledger | 活动对象驱动增长、完成记录及时回收 | Lease/pin/ticket 寿命与 GPU 完成证据 |
| 字体 glyph cache / atlas | 实际字符串驱动生成、分页与预算 | 每页 GPU 尺寸、活动页与 UV 稳定性 |
| 资源缓存 | 字节预算、引用保护、LRU/优先级回收、流式加载 | 在途资源不得提前释放 |
| 帧临时数据 | arena 复用、高水位预留、安全帧边界扩容 | 帧时间预算与 packet/fence 生命周期 |
| 工作/完成队列 | 负载驱动预留、背压、取消与限时等待 | 不允许无限生产耗尽内存 |
| GPU uniform、shader 格式、设备资源 | 按设备能力与 wire schema 校验 | 硬件与协议硬上限 |
| 崩溃处理、实时音频 callback | 必要的预分配与无分配路径 | 特殊实时/故障路径不执行不受控分配 |

## 接口语义

新接口区分 **initial reserve（初始预留）**、**soft byte budget（软字节预算）**、**hard limit（有依据的硬上限）**。软预算可触发回收、延后加载、降级或背压；硬限制必须说明来源。

不能把现有 `capacity` 字段在文档中偷偷改成另一种含义。迁移同时修改实现、生产方、消费者与测试；旧 API 删除，不留兼容双轨。wire 变化 bump schema，拒绝不匹配文件。

## 增长与事务

准备新 storage/申请页 → 校验依赖 → 原子提交索引/版本 → 在借用或 fence 结束后回收旧 storage。

增长不能使活动 Lease、回调、借用 span、已提交 UI snapshot 或 GPU UV 悬空。禁止仅给固定容器添加 `resize()` 而不迁移其引用契约。

## 当前实现与验收

TPCK 资源包已采用共享只读映射、无分配查找与 owning view；Catalog metadata 按实际规模分配，
count ceiling 为 0 时不附加数量限制。同步/异步加载和增量 cooker 不再复制完整 cooked payload；
queue 使用 head cursor 摊销压缩，逻辑 metadata 字节预算计入 queued + in-flight，不能当作实际 heap/working-set 指标。
跨帧 pin 不进入 FrameArena。详见 [ADR 0063](adr/0063-package-file-system.md)，实测与源码状态分开记录。

Core `GenerationPool` 已提供显式稳定分块 `reserve()`；`tryEmplace()` 不隐式增长。Gameplay Timer/Action/subscriber、
Navigation2D/3D blocker、PhysicsNavigationSync2D registration 已按需增长；Blackboard 改为稀疏 typed table；
Action/BT/FSM 不再附加任意节点数量上限。Scene2DRuntime 先按 authored 节点数预留 side tables，voice tracking 随
活动播放增长并回收。0 初始预留合法；新增操作可能分配，不能再声称所有 mutation 在 Create 后零分配。
实现与验证状态见 [2026-09-13 实施记录](capacity-and-lifetime-2026-09-13.md)。

Scene World 与 AssetStore 已改为稳定分块增长；Runtime PlatformEventSubscriptions 独立拥有 callback，新增订阅不会
搬走执行中的 callable。Sprite2D/Mesh3D/Shader registry 的活动 Entry 使用 PMR 稳定页，candidate/pending 表在 GPU
接管前按实际事务规模预留，FramePin 地址不因增长改变。Asset 请求队列的 count/metadata-byte 背压与 Store 预留解耦，
两种背压不得同时关闭。SaveStore 不再定义 slot 数量，按规范文件名枚举实际存档。

Editor World2D/World3D document 只拥有 canonical revision bytes，不再重复配置 entity/node 数量；Hierarchy 和 preview
随实际文档规模准备空间，保留 history/gameplay 字节预算和 AssetFormat wire 校验。UI 多个 PMR storage、RenderScene /
frame packet、State stack、Physics registry 等尚未迁移；这不是对其必要性的永久认可，也不是新模块必须复制的模板。
后续逐 owner 处理提交与借用寿命。
MSDF atlas 当前仍为单页，满页返回错误；多页增长不能仅凭本策略宣称完成。Task/Signal 队列、实时音频、搜索/追赶
工作量与资源驻留预算继续保留，不能以无限队列替代背压。

Asset retirement ledger 已迁移为活动记录驱动增长：完成即时回收、累计计数独立、预留摊销增长且复用峰值空间。
`recordCapacity` 显示实际预留槽位；不会为每次历史完成保留 tombstone。验证见 [生命周期与退役收口](lifecycle-retirement-2026-09-07.md)。

迁移验收：

1. 超过初始预留后正常创建、布局与交互，活动 handle/snapshot 不失效。
2. 反复加载卸载后的 ledger/registry 占用与活动资源规模相关，不随历史操作总量增长。
3. 新 atlas 页不改变旧页 UV，活动 GPU 提交安全。
4. 同一 workload 比较分配次数/字节数、峰值和稳态驻留、扩容搬迁次数、CPU p50/p95/p99 与 GPU 时间，记录 build/config/hardware。
5. 注入 OOM、取消、并发卸载与 shutdown timeout，验证无泄漏、UAF、半提交和无限等待。

未完成代码和实测时标为“策略已接受、迁移中”，不宣称全引擎动态容量重构完成或凭标签承诺性能提升。
