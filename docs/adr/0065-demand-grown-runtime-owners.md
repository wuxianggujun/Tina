# ADR 0065：普通 Runtime owner 按需增长与安全回收

- 状态：Accepted
- 日期：2026-09-13
- 决策依据：maintainer 要求实际移除无必要的固定数量上限，不以调大容量或兼容分支代替迁移。

## 背景

[ADR 0052](0052-demand-driven-memory-policy.md) 已取消全引擎定容政策，但 Timer、Action、Signal、AI、
导航 blocker 和 Scene2D 编排表仍将普通内容数量当成硬限制。直接把连续容器改成自动 resize 会搬走正在执行的
callback 或长期借用的实例；取消容量校验也不能解决回收重入、Stop 未被接受和分配失败后的 owner 泄漏。

## 决定

1. `GenerationPool::reserve()` 以几何增长的独立块增加槽位，旧 Value 不移动，owner/index/generation 不变。
   `Create(0)` 合法；`tryEmplace()` 本身不增长，实时 owner 仍可选择预分配。索引表示范围、字节溢出和 OOM
   是真实失败，不以任意常量限制普通 owner。PMR resource 必须覆盖全部块的寿命。
2. Scheduler、ActionRunner、Signal subscriber 与 Navigation2D/3D blocker 改用按需稳定槽位；公开配置命名为
   `initial*Reserve`，0 表示按需首次分配。预留不是数量上限，也不是当前存活数。运行顺序独立于物理槽位复用。
3. Action 删除固定节点数，平铺 postorder program，按 authored depth 预留显式 continuation stack，执行不递归。
   AI 删除固定树/状态数量限制；Blackboard 按实际写入 key 使用稀疏 typed table，不为大 key 分配全部空洞。
4. PhysicsNavigationSync2D 的 registration/planner 按需增长；同步先预留全部新增 blocker 再提交 remove/update/add。
   Scene2DRuntime 先统计实际 authored 节点并预留 side tables，再构造嵌套 owner；不在活跃借用背后搬动实例。
5. 保留有根据的预算：Task/Signal 队列背压、timer catch-up/Repeat/AI tick/A* 工作量、资源驻留、实时音频、设备
   与 wire/input 校验。删除 Task IO 16 / CPU 32 的任意工厂上限；实际线程创建失败仍结构化返回，不裁剪显式值。
6. 回调回收在安全点执行。Signal 保留当前 in-flight payload，clear 只清 pending，post 后续消息留到下次 drain；
   subscriber 顺序由独立链维护。Scheduler/ActionRunner 的 facade 不可在 dispatch/捕获回收期间 move/replace/destroy。
   Core pool 不承诺 Value 析构期间任意结构重入；需要此能力的上层 owner 必须保护并延后回收。
7. Scene2DRuntime 的 `Empty -> Ready -> Stopping -> Empty` 明确分开停止请求与终态确认。未退休 voice、clip Lease、
   Asset borrow 和未关闭子系统保留供原 owner 重试；不关闭共享 AudioEngine，也不在一帧内无界等待。
8. Task 用每域原子失败计数和 TaskGroup 失败计数报告 callable 异常，不建立无限错误队列；用户捕获释放早于 idle。
   Main callable 失败返回 `CallableFailed`，当前条消费一次，其余队列保留。业务详细结果仍由提交方保存。
9. 这是 SDK API/ABI 破坏式迁移，compatibility epoch 升到 **0.4.0**，strict exact-version 拒绝旧请求。
   旧字段/API 直接删除，消费者、模板和版本 probe 一次迁移；无 wire schema 变化，不伪造资源格式升级。

## 对历史决定的影响

部分替代 [0036](0036-gameplay-tooling-boundaries.md) 的固定 registry/256 Action 节点规定、
[0049](0049-ai-decision-layer.md) 的定容稠密 Blackboard 规定。保留其模块边界、时间来源、typed key、
顺序、预算与生命周期要求；不改写历史 Accepted 理由。0031 仍为 Proposed，本 ADR 不代替其整体评审。

## 代价与验收

- 超预留新增操作可分配，不能再承诺所有 mutation 在 Create 后零分配；可按负载初始预留，稳定阶段复用。
- 分块 pool 的扩展区查询需走块链；稀疏 Blackboard 为平均 O(1)，不是稠密数组的严格常数寻址。
- 删除容量不等于删除复杂度和帧预算；深层/大量零时长 Action 仍需 gameplay 调度，不承诺全帧总执行预算。
- 验收覆盖超预留、0 reserve、稳定地址/ID、逐次 OOM、深树、回收重入、队列压力与 retry。编译、测试运行、
  产品与性能结果分开记录，见 [内存策略](../memory-policy.md)。

UI/Scene/Render registry 等未迁移 owner 仍按各自当前契约运行；本决定不是“全引擎动态化已完成”的证明。

## 2026-09-14：同一迁移批次的 Scene / Asset / Save / Editor 扩展

继续执行 maintainer 的全量迁移要求，以下补充替代上述首批中 Scene/资源 registry 尚未迁移的状态描述；
不改写先前 Accepted ADR 的历史理由：

1. World 与 AssetStore 的实体/资源 generation 使用稳定分块按需增长，`initial*Reserve` 允许 0。
   World 先准备全部辅助表，pool 最后增长；组件地址稳定，dense entity span/typed view 在 reserve/create 尝试后重取。
2. PlatformEvent subscriber callback 独立拥有，slot 扩容不能移动执行中的 callable；每事件 snapshot 固定本事件
   接收者，新订阅从下一事件生效，退订立即生效。捕获析构前先发布 token/dispatcher 状态，允许既定重入。
3. Asset binding registry 活动 Entry 使用私有 PMR-owned deque；稳定 owner 指针保障 facade 的 noexcept move。
   不把 FramePin 指向的 Entry 放进可搬移 vector。候选、待退休表按事务规模预留，GPU 接管后不再为 bookkeeping 分配。
   Shader material instance 使用稳定 GenerationPool。pending retirement 失败仍保存 owner，不能因扩容省略退役。
4. Catalog reload 不再配置最大迁移个数或双驻留槽位 headroom；从实际 changed/resident/dependency 数准备，仍执行
   staged cooked-file 字节预算、全体 participant prepare 和一次性 publish。请求 count/metadata-byte 背压不依赖
   Store 初始预留，两项不得同时关闭。
5. SaveStore 接受整个 u32 slot ID 域，按规范文件枚举实际存档，不为不存在的 slot 建表；payload 字节预算、Busy、
   文件与 UTF-8 校验继续保留。未改变 envelope 或游戏 payload schema。
6. Editor World2D/World3D document 删除重复 entity/node 配置，而不是换成不消费的 reserve hint；canonical wire
   校验是唯一数量边界。Hierarchy 按实际行数准备 collapse 状态后发布，preview World 按 authored 数量 + camera 预留。
   undo history、gameplay bytes 与尚未迁移的 UI/Render/Physics owner 仍保持各自预算/契约。

以上继续属于未发布的 SDK 0.4.0 epoch；不恢复旧字段/错误常量或迁移兼容入口。编译、运行和安装状态只记录在实施交接中。
