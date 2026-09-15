# 按需容量与可靠性实施交接（2026-09-13）

## 范围与结论

本批承接 [逐模块审查](module-audit-2026-09-13.md) 的 F1–F8 / `NAV-GRID-PMR-001`，并实施
[ADR 0065](adr/0065-demand-grown-runtime-owners.md)：**普通 owner 删除无根据的数量上限，必要的预算保留。**
基线为 `ae5d5981`；保留此前文档修改，不改写历史审查证据。2026-09-14 续批新增 Scene World、Platform
订阅、SaveStore、Asset owner 与 Editor 文档/预览规模迁移；没有资源 wire schema 变化、SDK 安装或发布。
SDK 源码 epoch 为 **0.4.0**，所有消费者须用同版本头文件与 archive 重编，不能混用已安装 0.3.0。
**下方 2026-09-13 的 11-target 编译仅覆盖首批，不是当前工作树通过证明。** 当前尚不能重装 D 盘。

## 模块与落地

| 模块 | 实际迁移 | 仍保留什么 |
| --- | --- | --- |
| Core | `GenerationPool::reserve()` 分块稳定增长，0 初始槽合法，move 仅转移 owner；旧 Value/ID 不因扩容失效 | u32 ID/地址空间与 OOM；`tryEmplace()` 不隐式增长；析构任意重入不是底层保证 |
| Gameplay | Timer/Action/subscriber 按需增长；注册顺序与槽复用分开；factory 全程 RAII；取消在安全点回收 | 单 owner-thread；Timer catch-up、Repeat 迭代预算与 Signal 延迟队列背压 |
| Action program | 删除 256 节点上限；平铺 postorder、子树连续区间、按深度预留显式执行栈；repeat 接管 program 而非反复复制 | 内存与索引/容器范围；没有全帧所有节点的总预算 |
| AI | Blackboard 稀疏 typed table；删除 65536 key、4096 BT/FSM 上限；FSM guarded exactly-once exit | 类型绑定、u32 无效 key、拓扑校验、node/transition budget；长期新 key 的类型元数据保留 |
| Navigation2D/3D | blocker 按需稳定增长；每格引用计数改为 u32；Nav2D Data/overlay 稳定 PMR owner | Grid 输入尺寸、工作集、A* 扩展预算与 revision/address 借用规则 |
| PhysicsNavigationSync2D | registration/planner 几何预留；同步先准备全部新增 blocker 空槽，再修改发布状态 | 外部 World/Grid 地址与 owner 校验；不能把 generation-retired 槽假定可复用 |
| Scene2DRuntime | 按真实 authored 数量一次预留 side tables；层 demand 按最大实际层数预留；voice tracking 随活动播放增长/回收 | 子系统驻留/实时预算；runtime 不可移动；TileMap/FX 借用先拆再 shutdown |
| Task | 删除 IO 16 / CPU 32 工厂 cap，自动 CPU 仍为 max(1, hw-1)；每域失败计数与 TaskGroup failedCount | OS 线程/内存不足、队列背压、禁止 detach/强杀、shutdown deadline |
| Math | inverse 在 double→float 前检查有限性与 float 范围，不用大 epsilon 排除合法小尺度 | optional 失败语义与精确浮点策略 |
| Scene World | entity/component 稳定分块；辅助索引与 scratch 先准备，pool 后增长；Prefab/World2D restore 按批次预留 | ID/地址范围、组件/层级校验、current wire 限制；dense span/view 在 reserve/create 尝试后失效 |
| PlatformEvents | callback 独立 shared owner，slot/每事件 snapshot 按需增长；捕获析构前发布 token/dispatcher 状态 | owner-thread、activation order、退订立即生效、新订阅下一事件、递归 dispatch 拒绝 |
| SaveStore | 删除 slot 数量与稠密空槽；整个 u32 slot ID 域可用；非递归枚举规范 regular file 并去重排序 | payload 字节、Busy、UTF-8/路径与 primary/backup 恢复；IO 错误不伪装为空 |
| AssetStore / AssetSystem | 稳定 resource generation 按需增长；reload 按实际 replacement/dependency 数准备迁移与双驻留；Null upload 协调表在 ticket 接管前几何预留 | 请求队列 count/metadata 字节背压、staging/ticket 与 staged 文件字节预算、Lease 与单次 publish |
| Sprite/Mesh/Shader registry | PMR-owned 稳定 deque 保持 FramePin 地址；prepared/pending 表在 GPU 接管前准备；material instance 稳定 pool | GPU key/ID 范围、冲突、active pin 阻止 retirement、失败可重试 owner |
| Editor document/Hierarchy/preview | 删除 World2D/3D 重复数量配置与 app 128 上限；collapse table、preview World 按实际内容准备；移除演示级 history/gameplay 覆盖值 | AssetFormat wire、history/gameplay byte 预算；UI/Render/Physics 等未迁移 owner 不因此自动放开 |

增长采取“准备内存 → 发布新身份/索引”的顺序；失败可以留下已预留的空闲空间，但不发布半个新对象、不使已有 handle
失效。不宣称整批预留字节也事务回滚。PMR resource 长于 owner；不把用户 callable 内的分配算成内部零分配。

## 公开迁移清单

旧名直接删除，无 alias/deprecated/双轨：

| 旧 API | 当前 API / 用法 |
| --- | --- |
| Scheduler `timerCapacity` / stats `timerCapacity` | `initialTimerReserve` / `reservedTimerSlots` |
| ActionRunner `actionCapacity` / stats `actionCapacity` | `initialActionReserve` / `reservedActionSlots` |
| Signal `subscriberCapacity` / stats `subscriberCapacity` | `initialSubscriberReserve` / `reservedSubscriberSlots` |
| `MaximumActionNodeCount` | 删除，按实际 program 节点和深度分配 |
| Blackboard `slotCapacity` / `capacity()` | `initialSlotReserve` / `boundSlotCount()`；后者统计类型绑定数，不是 reserve |
| Navigation2D/3D `dynamicBlockerCapacity` / `dynamicBlockerCapacity()` | `initialBlockerReserve` / `reservedBlockerSlots()`；`dynamicBlockerCountAt()` 返回 u32 |
| PhysicsNavigationSync2D `registrationCapacity` / `capacity()` | `initialRegistrationReserve` / `reservedRegistrationSlots()` |
| Scene2DRuntime 四类节点 capacity、`audioVoiceCapacity`、`tileLayersPerMapCapacity` | 删除；节点/层来自实际内容，voice tracking 动态增长 |
| Scene2DRuntime `tileSpriteCapacity` | `initialTileSpriteReserve`，允许 0 |
| Signal `Result<u32>` 投递数量 | `emit/drain` 返回 `Result<Core::usize>` |
| 自定义 `ITaskSystem` 实现 | 必须实现 `failureStats() const noexcept`；各域累计 u64 计数 |
| World `entityCapacity` / `entityCapacity()` / `MaxEntityCapacity` | `initialEntityReserve` / `reservedEntitySlots()`；新增 `reserveAdditionalEntities()` / `isOwnerThread()` |
| PlatformEventSubscriptionConfig `subscriberCapacity` / dispatcher `capacity()` | `initialSubscriberReserve` / `reservedSubscriberSlots()` |
| AssetStore `capacity`、AssetSystem `storeCapacity` / Store `capacity()` | `initialAssetReserve` / `reservedAssetSlots()`；新增 `reserveAdditionalAssets()` |
| Sprite/Mesh/Shader registry 各 `*Capacity` 字段/查询/默认与最大常量 | `initial*Reserve` / `reserved*Slots()`；0 合法，不保留旧 alias |
| `CatalogReloadConfig::maxResidentMigrations` | 删除；按实际迁移列表准备 |
| `SaveStoreConfig::slotCapacity`、Default/MaxSaveSlotCapacity、`SaveErrorCode::InvalidSlot` | 删除；所有 u32 slot 合法，错误编号 2 留空 |
| Editor World2D `entityCapacity`、World3D `nodeCapacity` | 直接删除；不是初始预留 hint，数量仅由当前 wire 校验 |
| 三类 registry 的 `*BindingCapacityExceeded` 错误常量 | 删除；编号 30/34/49 留空，真实内存/地址错误使用现有结构化错误 |

Scene2DRuntime 新增 `state()` / `trackedVoiceCount()`，TaskGroup 新增 `failedCount()`。
`TaskErrorCode::CallableFailed`、`SceneErrorCode::RetirementPending` 以新 code 追加，不重编号旧错误。
7 个 TaskSystem 实现/fake、模块测试、两个样例、SDK consumer 源码与 CMake 版本请求已迁移。

## 关键状态与失败路径

- **Signal**：dispatch 捕获 entry tail，新订阅下一 payload 生效；退订立即变 inactive，捕获安全点销毁。
  预分配 ring 的预算包含 in-flight。`clearQueued()` 清未派发条目但不销毁当前 payload；clear 后 post 留到下次 drain。
  callback throw 消费当前 payload 一次并保留其余；payload 构造/析构结构重入受 guard 保护。
  公开操作持有局部 State owner，callback 可 reset/move facade；token replacement 先发布 incoming，再销毁 previous。
- **Scheduler/ActionRunner**：live list 按原始顺序，dispatch 只处理入口批次；intrusive 待取消链在安全点回收并批量压缩。
  回收期间可新增或取消，但不可嵌套 advance，也不可 move/replace/destroy facade。Timer 真正触及追赶预算才丢 backlog，
  callback 内 pause 保留余量；极端 backlog 计数饱和，最后一次 callback throw 也退休一次性 timer。
- **Scene2D**：`Empty -> Ready -> Stopping -> Empty`。play 先预留 tracking，再提交 Play。shutdown 每次至多 pump 一次；
  Stop 拒绝/终态未确认返回失败，保留 voice、clip Lease、Asset borrow。Stopping 拒绝帧更新/新播放；成功退役才释放。
  析构仍有未退休 reader 时 fail-stop，不把 PCM 提前释放。样例只因独占 device-less AudioEngine 才可先关该引擎后重试；
  共享或真设备产品必须由组合根协调，不套用样例捷径。
- **Task**：IO/CPU catch 各计一次失败并继续 drain；TaskGroup 同一失败在其 group 维度计一次，不表示业务成功。
  Main throw 返回 CallableFailed，其余 queue 保留。callable 捕获在锁外析构，active/pending 随后减少，idle 不早报。
  shutdown 不得在本 Task 的 callable/capture 析构中调用，finalize 不与 Main pump 并发。
- **World / GPU registry**：不把增长等同于可搬移所有 storage。World component 指针与活动 binding Entry 地址稳定，
  dense span/view 则必须重取；callback/FramePin 不允许指向会被 vector growth 搬走的对象。自定义 PMR 异常转换成
  结构化错误；扩容失败不消费调用方 GPU owner。Mesh 共享 Texture index 在增长后仍指向同一 Entry。
- **Catalog reload**：candidate/resident migration/next index/root 与 replacement generations 都在 publish 前准备；旧 Lease
  可继续读取旧 payload，旧 weak Handle 与新 active generation 分开。commit 后旧 GPU retirement 拒绝不回滚新 Catalog，
  留 pending owner 供 drain 重试。队列预算不再隐式回退到初始 Store 预留。
- **Save**：`slot-0000.tsave` 到 `slot-4294967295.tsave` 按至少四位生成，不截断、不发生补零减法下溢；
  primary/backup 合并成一个列表项，缺失根目录返回空。异步 Busy 消费者用确定性 IO worker 阻挡，不靠大量空槽拖慢扫描。

## 验证记录

### 2026-09-14 续批（当前）

源码、公开契约与主题文档已继续收口；Asset/Scene/Runtime/Save 的既有测试消费者已迁移到按需语义，包含跨增长
Lease/payload/FramePin、零预留、真正范围/OOM、reload 双驻留、Save 高 ID 与稀疏枚举。Editor 文档里程碑完成后统一
迁移了既有消费者，错误断言改为真实 wire 范围或 gameplay/history 字节预算；未新增 Editor 测试。
当前集中编译进行中，不以首批产物代替当前验证。首轮暴露并修正两处漏迁移：`AssetGpuUpload` 仍调用旧 Store getter、
异步 `SaveStore::beginLoad()` 仍调用已删除的 slot 校验。另修正 pending upload 的逐项 reserve，避免零初始预留后的二次方搬移。
对应回归源码覆盖 coordinator 先于 Store 内容创建、动态增长与真实 ledger 背压，以及最大 u32 slot 的异步 save/load/list。
本次 `testRuns=0 / sampleRuns=0 / installRuns=0`，没有启动 Editor 或 gate。

### 2026-09-13 首批（历史编译证据）

2026-09-13 Windows / MSVC / Debug 集中增量编译已完成，**11 个指定 target 均成功，build exit 0**。
复用 `windows-msvc-vnext-bgfx-product-2d` 常驻树，必要的 CMake regenerate 成功；日志无 compiler error/warning
或 CMake Error/Warning。没有 clean、SDK 安装、失败重编，也没有启动编译产物。
**本轮 testRuns=0、sampleRuns=0**；未执行 GoogleTest、CTest、产品/视觉 gate。下表是已经编译的回归源码，
不是运行通过记录：

| 编译目标 | 回归源码覆盖 |
| --- | --- |
| `tina_tests` | Core 稳定增长/0 reserve/OOM；Task 高核配置、各域异常、capture 先于 idle、shutdown 锁外析构 |
| `tina_gameplay_tests` | 超预留与回调中扩容、factory OOM、捕获回收重入、8192 层 Repeat、1024 分支、Signal clear/post/throw 顺序 |
| `tina_ai_tests` / `tina_math_tests` | 高 key/类型绑定/OOM、8193 深树、8192 states、FSM 析构重入；极小尺度/逆平移溢出/正常矩阵 |
| `tina_navigation2d_tests` / `tina_navigation3d_tests` | 超预留/0 reserve、65536 重叠计数、growth OOM 与旧 ID/revision；稳定 PMR move |
| `tina_physics2d_tests` / `tina_gameplay2d_tests` | registration/grid 预留失败事务、Scene2D Stop queue 满/重试/Lease 保留、voice tracking OOM、80 Fx nodes；physics on/off header isolation |
| `tina_asset_tests` | 受影响 Task fakes / Nav bridge API 的现有消费者编译 |
| `tina_sample_2d` / `tina_sample_2d_authored_scene` | 容量字段与 Scene2D teardown 的样例消费者编译，不启动 |

实际构建命令（Git Bash，工作目录为仓库根目录）：

```bash
MSYS2_ARG_CONV_EXCL='*' VSLANG=1033 PYTHONUTF8=1 \
cmake --build out/build/windows-msvc-vnext-bgfx-product-2d --config Debug \
  --target tina_tests tina_gameplay_tests tina_ai_tests tina_math_tests \
  tina_navigation2d_tests tina_navigation3d_tests tina_physics2d_tests \
  tina_gameplay2d_tests tina_asset_tests tina_sample_2d tina_sample_2d_authored_scene \
  --parallel 2 -- /nr:false
```

日志：`out/build/windows-msvc-vnext-bgfx-product-2d/capacity-lifetime-20260913-debug-build.log`。
Gameplay、AI、Math、Physics2D、Gameplay2D 的独立 header-isolation target 也已由依赖图编译；不把编译结果
等同于 Linux / sanitizer / installed consumer 或真实输入、音频、GPU 产品验收。

产物根目录：

```text
C:/Users/wuxianggujun/CodeSpace/CMakeProjects/Tina/out/build/windows-msvc-vnext-bgfx-product-2d
```

以下时间为本轮核验的 `LastWriteTimeUtc`，不是旧产物成功记录；完整路径、字节数与精确时间见同目录的
`capacity-lifetime-20260913-resource-report.json`。

| 产物（相对上述根目录） | UTC 修改时间（2026-09-13） |
| --- | --- |
| `lib/Debug/Tina.lib` | 13:58:37 |
| `bin/Debug/tina_tests.exe` | 13:59:17 |
| `bin/Debug/tina_gameplay_tests.exe` | 13:59:48 |
| `bin/Debug/tina_ai_tests.exe` | 14:00:11 |
| `bin/Debug/tina_math_tests.exe` | 14:00:36 |
| `bin/Debug/tina_navigation2d_tests.exe` | 14:01:04 |
| `bin/Debug/tina_navigation3d_tests.exe` | 14:01:23 |
| `bin/Debug/tina_physics2d_tests.exe` | 14:02:17 |
| `bin/Debug/tina_gameplay2d_tests.exe` | 14:02:48 |
| `bin/Debug/tina_asset_tests.exe` | 14:04:51 |
| `bin/Debug/tina_sample_2d.exe` | 14:05:43 |
| `bin/Debug/tina_sample_2d_authored_scene.exe` | 14:06:21 |

静态检查：变更文件严格 UTF-8 解码，`git diff --check` 成功；`CheckDocs.ps1` 为 143 个 Markdown、errors=0、
warnings=4。四条 warning 来自既有 `docs/building.md` 中 `tina_core/task/platform/render` 的 runtime helper
target 识别，不是本轮新增链接错误。迁移范围的旧 API 定向搜索无残留；公开头第三方 token 搜索只有解释性注释，
未发现新增第三方类型或 include 泄漏。

授权运行后优先执行上述回归，再补真设备 reader 延期、shared AudioEngine 无关 voice 不受影响、跨平台 PMR/数值、
新版本 installed consumer 与真实产品流程。本轮不提供这些场景通过或帧率改善的结论。

## 剩余容量迁移

UI tree/side stores/Grid tracks、RenderScene/frame packet、Animation3D、Physics2D registry、Scene2DPhysicsBridge、
Scene3DRuntime side stores、Runtime Action binding/State stack、Editor 其它数量表与 SaveMigrationPipeline
等仍有固定存储。**“未迁移”不等于“全部限制都有必要”**：普通持久 registry 继续按 ADR 0052 迁移；snapshot/packet
须一起处理提交与借用失效，不能仅调用 resize。实时音频/队列/硬件/wire 安全预算则继续保留。
导航查询工作区仍显式预分配，本轮没有把所有 query owner 变成无预算增长。
TileMap 编排层不再设置重复的层数上限，但 `TileMapWire::MaxLayers = 256` 仍是现有 cooked payload 校验范围；
删除编排容量不代表同时放宽 wire/input 预算，本轮未改变资源 schema。

## 2026-09-13 首批资源收尾（历史，不覆盖续批）

本轮只启动一个构建 job `j-3dlakl`，已退出 0；`/nr:false` 禁用 MSBuild 节点复用。2026-09-13 14:11:27 UTC
核验时 `MSBuild/cl/link/cmake/ninja/vcpkg` 进程列表为空，不存在本轮编译残留。

| 资源 | 实际状态 |
| --- | --- |
| buildTree | 常驻树保留。13:53:55 UTC 构建前为 **19,302,925,678 bytes**；14:11:27 UTC 采样为 **19,318,504,020 bytes**（写入资源报告之前）；未执行清理，回收 0 bytes |
| process | 构建 job 已结束；上述编译进程清单为空，未启动 sample / test / Editor |
| helper / watchdog / windowManager | 本轮未创建 |
| container / volume / image | 本轮未创建、未调用；未进行全局盘点，不声称其它任务资源已释放 |
| cache | 沿用常驻树和既有依赖缓存，未清理；全局缓存占用未核验 |
| agent | 本轮未派生子代理，无本轮子代理待回收 |
| temporary buildTree | 未创建专用临时构建目录，无本轮临时树待删除 |

保留常驻树是为了复用已编译依赖和交付产物；不删除既有安装、其它工作区或用户缓存。完整资源采样与产物记录
位于上述 JSON，仅对记录中明确核验的范围作结论。
