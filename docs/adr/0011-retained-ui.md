# ADR 0011：自研高性能 Retained UI，输出后端无关 DisplayList

- 状态：Accepted
- 日期：2026-07-16
- 修订：2026-07-18
- 实施状态：M7-C1b 已实现事务式 Flex-lite layout；M7-C1c-a 已实现固定容量 PMR Pointer policy/
  route-ancestry scratch、`Ignore`/`Targetable`、双缓冲 `UICommittedHitView` 与成功 `commitLayout()` 的
  structure/layout/hit 原子发布；M7-C1c-b1 已实现无分配 `queryPointerHit()`、反向目标选择与 visited count。
  M7-C1c-b2 已实现 fixed-capacity synthetic routed pointer event：generation-safe RAII listener token、
  48-byte fixed-inline `noexcept` callback、Capture→Target→Bubble、stop/consume、路由中 add/reset/destroy
  安全失效和 route/commit reentrancy guard。Runtime UI producer、持久 Pointer Capture、Focus/Modal、
  Button default action、paint snapshot/DisplayList、dirty subtree pruning、nested clip、文本/Glyph Atlas
  与 bgfx UI pass 仍后置。

## 背景

Tina 需要同时支持游戏内 HUD、菜单、设置页、模态交互、中文文本和无 GPU 测试。Immediate UI
难以稳定表达跨帧 Focus、Pointer Capture、Modal Scope、动画与可访问性状态；让 UI 直接依赖
bgfx，又会把第三方类型、线程约束和 GPU 生命周期泄漏到 Game SDK。

Legacy UI 已验证部分产品交互，但它的类型、API 和生命周期不是 vNext 的兼容目标。vNext 需要先固定
可测量的增量更新、输入所有权和帧资源契约，再逐批增加控件，避免以“功能完整”为理由重新引入
隐式全树布局、全量重绘或后端耦合。

## 决定

`tina_ui` 实现独立的 Retained UI。每个 Runtime WindowRecord 唯一拥有一个 `UIContext`；
`IGameState` 只持有 move-only root owner，并用包含 owner WindowId 与 generation 的 `UINodeId`
引用节点。Game SDK 不拥有 `UIContext`，也不能取得 Widget、PaintCache 或 Render backend 指针。
当前 M7-C1b/C1c-a `tina_ui` 仍只 PUBLIC 依赖 `Tina::Core` 与 `Tina::Platform`；Font Asset 与 Render descriptor/
DisplayList 依赖在后续 asset/render 切片进入。

FreeType 只存在于可选生产适配器 `tina_ui_freetype`。Headless、Null UI 和公共 Widget API 不解析
FreeType、GLFW 或 bgfx 依赖。vNext 不包含、继承、别名或适配 Legacy UI 类型与 API；Legacy 代码
只能作为行为参考，不能进入 `tina_ui` 的链接图。

### Dirty 分类与传播

每个节点持有固定宽度 dirty bitset，dirty 入队采用去重标记；同一节点同一类别在一次 commit 中最多
进入一次工作队列。属性元数据必须声明其最小失效集合，禁止所有 setter 无条件标记整棵树 dirty。

| Dirty 类别 | 直接来源 | 传播与重建边界 |
| --- | --- | --- |
| `Structure` | 增删、重排、换父节点、root attach/detach | 标记最近 Layout Root 的 `Measure`，并使受影响 paint order、hit 与 semantics 子树失效 |
| `Style` | class、theme token、DPI 或继承属性变化 | 只重算受继承影响的后代；解析后的属性再映射到 `Measure`、`Arrange`、`Paint`、`Composite`、`HitTest` 或 `Semantics` |
| `Measure` | intrinsic size、字体度量、约束相关样式变化 | 从节点向上到最近 Layout Root；不跨越明确的 layout containment 边界 |
| `Arrange` | 父约束、对齐、margin/padding 或已测量尺寸变化 | 由最近 Layout Root 向实际受影响的布局后代传播；同时使其 committed geometry、composite 与 hit 数据失效 |
| `Composite` | transform、opacity、scroll offset、effective clip 变化 | 只更新受影响子树的 composite/hit snapshot；不得因此重建节点 local PaintCache |
| `Paint` | 颜色、边框、图像、文本 run 或 glyph raster completion 变化 | 只重建节点 local PaintCache；不得反向触发布局 |
| `HitTest` | visibility、pointer policy、geometry 或 composite 变化 | 只重建受影响的 hit-test 索引/记录 |
| `Semantics` | label、role、enabled、checked、focus 状态变化 | 只重建受影响的 semantics 记录；不隐式触发布局或绘制 |

文本 shaping/layout 与 glyph raster 分离。已有 glyph advance 决定 Measure 结果；异步 raster completion
只标记 `Paint`，不能改变布局。若字体度量或 shaping 结果实际变化，则由文本系统明确标记 `Measure`。

M7-C1b 先落地布局事务所需的最小子集：`UILayoutLength/UILayoutStyle/UIDirty` 与
`UICommittedLayoutView` 已成为 public Tina 类型；style setter 先做规范化和 same-value 比较，再
预检 node→root 全路径所需的 dirty queue slot，成功后才一次性合并节点与祖先 dirty。width、height、
min/max 都是 border-box；Percent 使用 `0..100`，相对父节点最终 arranged content box（包括 min/max、
grow、stretch 或 absolute inset 的结果），默认 Auto root 把 viewport content box 作为确定基准。
当前支持 Px/Percent/Auto、margin/padding/gap、Row/Column/
grow、justify/align/stretch、Absolute Overlay、Visible/Hidden/Collapsed 和 min-wins clamp。
Auto 轴的 Percent 在 Measure 无确定 basis 时不参与父级 intrinsic size，并记录诊断；Arrange 取得最终
content box 后只解析一次，不重新 Measure 父级，也不迭代求 fixed point，循环场景允许确定性 overflow。

这是布局基础而非最终细粒度增量实现：changed frame 当前仍对整棵 live tree 执行一次非递归
Measure/Arrange，尚不能依据 dirty leaf 跳过无关 subtree。后续实现必须在不改变本 ADR 事务、容量
和 committed snapshot 契约的前提下，把工作量收敛到实际失效区域。

M7-C1c-a 又加入 `UIPointerHitPolicy::{Ignore, Targetable}` 与双缓冲 `UICommittedHitView`。每份 view
保存 effective-visible route-ancestry entry，保留 `Ignore` 祖先并省略 `Hidden`/`Collapsed` 子树；同一
view 内的 paint ordinal 唯一且严格递增，并携带 structure/layout/paint-order/hit revision。仅 policy
变化的 hit-only commit 为0次 layout。当前 effective clip 仅为 `viewport ∩ worldRect`，hit rebuild 仍
线性扫描整份 committed layout。M7-C1c-b1 的 `queryPointerHit()` 反向扫描 view，只接受同时位于
world/effective clip 的 `Targetable` entry，使用半开边界并返回 route index、四类 revision 与 visited count；
这个纯 query 本身不派发 listener，也不实现独立 z-order/stacking 或 nested clip。M7-C1c-b2 在此基础上增加
synthetic `routePointerInput()`：单条 normalized pointer input 最多执行一次 committed point query，使用固定容量
route path/listener storage，并按 Capture→Target→Bubble 派发 stable-order listener；它仍不是 Runtime producer，
也不包含持久 Pointer Capture、Focus/Modal 或 Widget default action。

### 每帧事务与 committed snapshot

一个 `UIContext` 的帧处理固定为：

1. Runtime 提供 committed `UIInputScopeSnapshot` 与有稳定 transition id 的有序输入；
2. UI 只使用上一份 committed hit snapshot 做 hit-test 和路由，事件回调只提交 mutation/dirty；
3. 在 layout barrier 前原子应用通过容量预检的 mutation；
4. 按需执行 Style → Measure → Arrange；每个窗口每帧最多进入一次 Measure/Arrange pass；
5. 更新 PaintCache、composite、hit-test 和 semantics，并一次性发布新的 committed snapshots；
6. 从 committed paint snapshot 生成本帧 DisplayList，之后交给 RenderFramePacket。

layout barrier 后发生的结构、样式和布局 mutation 延迟到下一帧，禁止 hit-test、paint 或 Widget getter
隐式触发布局。提交期间外部只能看到上一份完整 snapshot 或新一份完整 snapshot，不能看到半更新树。
committed hit snapshot 与 committed paint snapshot 使用相同的 tree revision、layout revision 和 paint order
revision；revision 不一致属于生命周期错误，当前帧不得提交部分 UI。

M7-C1c-a 的 `commitLayout(viewportSize)` 已作为 structure/layout/hit 的事务发布入口：它按需构建
受影响候选，并只在全部验证成功后原子切换；`commitStructure()` 仅保留为 M7-C1a 结构诊断 seam，
可单独发布结构并保留旧 layout/hit，Runtime 不得把二者拆开发布。viewport 变化即使没有 mutation 也重排；相同
viewport 且无 structure/layout dirty 时不增加 revision、不执行 layout pass。非法 viewport、候选
几何算术溢出、dirty queue、layout snapshot 或 hit snapshot 容量不足都保留旧 published snapshot 与
pending dirty。

无变化 UI 必须满足以下硬门禁：

- `0` 次 Style/Measure/Arrange；
- `0` 个 PaintCache rebuild；
- `0` 字节 Tina-owned heap allocation；
- 允许按 committed paint snapshot 线性发射可见 DisplayList 并 pin 本帧资源，但不得扫描 mutable tree、
  重建 PaintCache 或转向 Tina heap 分配。

测试和 Metrics 至少记录 layout pass、style resolve、PaintCache rebuild、display command、dirty high-water、
capacity failure 与 Tina allocation delta。每窗口 layout pass 每帧只能为 `0` 或 `1`。

当前直接测试已覆盖50,000节点深树的非递归布局/hit snapshot，以及首次发布后连续300次同 viewport、
无 mutation commit 的0 layout pass、revision 不变和 supplied UI PMR allocation count 不增加；15项
committed hit snapshot 加5项 point query、16项 synthetic route 测试后 `tina_ui_tests` 共75/75。query
测试覆盖反向目标选择、Ignore 穿透、world/clip 半开边界、非有限坐标 miss、snapshot binding/visited count
与300次零新增 UI PMR allocation；route 测试覆盖 Capture/Target/Bubble 顺序、stop/consume、dispatch 中
reset/add/destroy、generation-safe target invalidation、listener/path 容量失败、off-thread token reset、
route 中 commit 拒绝、错误销毁 context 的 death test、300次 route 零新增 supplied UI PMR allocation 和
递归 route 拒绝。Windows MSVC 19.50 Debug/Release、Linux GCC 13.4、Linux Clang 22.1.8 +
libstdc++15.2 ASan/UBSan/LSan 均为75/75；Clang 无 sanitizer 诊断。初次 GCC 暴露的 routed-pointer
callback `requires` 名称可见性问题已修复，二次 GCC/Clang 构建无 warning。它不等价于 PaintCache/
DisplayList、Runtime producer、进程 heap 或 GPU 资源门禁已经完成。

### PaintCache、DisplayList 与批处理

PaintCache 是节点持久、后端无关的 local paint 结果。它只包含节点局部坐标下的 Quad/Text/Clip
片段和稳定资源引用，不包含祖先 transform、scroll、effective clip、Widget/UINode 指针或帧内存地址。
仅 `Paint` 或 local paint schema/version 变化可以重建 PaintCache；Transform、scroll、opacity 和祖先 clip
通过 composite snapshot 合成并复用原 cache。

DisplayList 是 frame-local、只读、后端无关的顺序命令流。它不包含 backend vertex layout、ViewId、
uniform、encoder、GPU fence 或 bgfx handle。命令引用确定性 intern 的 effective clip，以及
packet-local `FrameResourceRef`。`UIContext` 生成 DisplayList 时必须接收 `FramePinSink`：每个本帧使用的
字体 atlas、纹理页或其他可退役资源先成功 pin，再发布对应命令。

`FramePinSink` 是调用方拥有、不可保存的 frame capability；UI 只能在 DisplayList emission 期间把
模块私有 Atlas/Asset lease 类型擦除为 move-only `FrameLifetimePin`，再调用
`add(FramePinKind, FrameLifetimePin&&) -> Status` 转移真实所有权。`FrameResourceRef` 只是 packet-local
资源表索引，本身不能延长生命周期，因此禁止用“pin 一个 ref”冒充 owning pin。资源表 intern 与同一
资源的帧内去重由 packet builder/sink 协作负责；UI 不依赖重复 add 次数表达所有权，也不能把 sink
捕获进 Widget callback 或 PaintCache。`add()` 失败时调用方仍持有 pin，整份 frame packet 事务回滚。

DisplayList、clip intern 表和 FramePinSink 记录都由 RenderFramePacket 的 frame storage 拥有，只能存活到
该 packet 完成提交/放弃；它们不得写回 PaintCache。资源 pin 由 RenderFramePacket 保活，并在 packet 完成
或回滚时统一释放，Widget 和 UIContext 不直接等待 GPU、查询 fence 或执行 unpin。

paint order 是可观察语义，特别是透明 Quad/Text。Renderer 只能合并 paint order 中相邻且
pipeline、texture、sampler、blend、effective clip 完全兼容的命令；遇到不兼容命令、clip 边界或顺序屏障
立即结束 batch。禁止为了减少 draw call 跨透明命令、跨节点或跨 clip 全局排序。

### 输入 consumption、claim 与路由边界

本节是 Accepted 目标语义。M7-C1c-a/C1c-b1 已提供 committed hit/route-ancestry 数据与纯 point query；
M7-C1c-b2 已提供 synthetic listener dispatch 与 Capture → Target → Bubble 执行。Focus/Capture/Modal、
Button default action、Runtime producer、consumption/claim 输出和真实 Gameplay Action suppression 仍未实现。

Runtime 将平台输入转为后端无关 `UIInputTransition` 序列，再调用 UI 路由；UI 不读取 GLFW，不修改
全局 Input Snapshot，也不直接调用 Gameplay 输入接口。每个 Pointer transition 最多使用 committed hit
snapshot hit-test 一次，随后按 Capture → Target → Bubble 路由。

`UIRouteResult` 及其只读 consumption/claim view 由 `tina_ui` 拥有，Runtime ActionMapper 只消费这些结果；UI 不得
include Runtime SPI。Runtime-private ActionMapper 容量仍由 Runtime 自己配置。`UIRouteResult` 必须同时
返回两份固定容量输出：与当前 frame/sequence 绑定的
`Tina::UI::InputTransitionConsumptionView` 标记一次性 transition，
`Tina::UI::ContinuousControlClaimsView` 声明本帧由 UI
拥有的 held/axis/pointer-delta。Runtime 先应用两者再做 Gameplay Action Mapping；被 UI 消费的
digital Down 由 Action Mapper 保持 `suppressedUntilReleaseOrNeutral`，真实 Up 只解除抑制而不补发
Gameplay edge。结果还可携带一个未被控件消费的 `UICancelIntent`，交给状态策略决定。UI 不能通过
清空按键状态、修改设备快照或依赖调用顺序来“消费”输入。

`UICancelAction` 是 Escape、Gamepad Back 等输入映射出的 UI 语义 transition；它与 Focus lost、
设备断开或 overflow 产生的 `InputCancelTransition` 不是同一类型：

- Modal 或 Widget 可以在 routed event 中消费并处理 `UICancelAction`；
- 未被消费的 `UICancelAction` 作为一次 `UICancelIntent` 返回 Runtime，由拥有状态策略的调用方决定返回、关闭或忽略；
- UI 不直接 pop GameState、关闭窗口或调用应用退出；
- Pointer Capture 的节点失效、被禁用或离开 committed scope 时，UI 向原 capture target 路由一次
  `PointerCancel`，再释放 capture；这不等同于应用级 `UICancelIntent` 或输入流
  `InputCancelTransition`。

Focus、hover、capture 和 Modal Scope 的变更在当前路由批次结束后提交。删除 target 后依靠 generation
校验停止后续投递，不能向复用 slot 的新节点继续 bubble。

### 容量与失败策略

`UIContext::Create` 必须显式接收节点/root、mutation、dirty queue、route depth、PaintCache bytes、clip intern、
DisplayList command、text run/glyph ref 与 frame pin 容量。运行期禁止隐藏扩容、系统 allocator fallback 或
因容量不足退回全树 heap rebuild。
当前 M7-C1b/C1c-a/C1c-b1/C1c-b2 的 `UIContext::Create(ownerWindow, capacities, memory_resource)` 已用 supplied PMR
固定分配 tree/id、style/pointer-policy/dirty side array、dirty queue、layout/route-ancestry scratch、route path
scratch、listener slots 与 committed structure/layout/hit 双缓冲；`dirtyQueueCapacity`、`layoutSnapshotCapacity`、
`hitSnapshotCapacity` 和 `routePathCapacity` 为0时从 node capacity 派生，非0时不得超过 node capacity。
`routedPointerListenerCapacity` 为0时也从 node capacity 派生，可单独配置且最大为1,048,576。small
control-plane 对象、token state、off-thread `UIRootOwner`/listener release 队列仍在 Create 期间使用默认 heap 预分配。
owner thread 析构 `UIRootOwner` 立即回收；非 owner thread 只入队 root id，由下一次 owner-thread
UI mutation/commit drain 并物理回收。`UIRoutedPointerListenerToken` owner-thread reset 立即生效；
off-thread reset 进入 bounded queue 并在下一次 owner-thread mutation/route 前 drain，context 销毁后 reset 仍安全。
`UIContext` 自身的 mutation、route 与销毁必须发生在 owner thread；从 routed callback/callback cleanup 内销毁，
或在非 owner thread 销毁，会触发生命周期硬门禁并终止，避免继续执行确定的 UAF。

- 结构/样式 mutation 在应用前预留全部容量；失败时原子拒绝该 mutation，保留上一 committed tree/snapshot；
- dirty queue 容量不足时返回稳定的 `UIError::CapacityExceeded`，不得静默丢 dirty bit；
- PaintCache、clip、DisplayList 或 FramePinSink 容量不足时放弃本帧 UI packet，释放本帧已取得的 pin，并把
  typed error 交给 Runtime；禁止提交截断、顺序错误或资源未 pin 的 UI；
- 错误必须携带 capacity kind、configured capacity、requested count、frame/window 上下文，并更新 high-water
  与 failure counter；不得为每个失败元素刷屏日志；
- 未来若增加 cache eviction，只能在帧边界以确定性策略淘汰未 pin cache，并单独 ADR；首批实现不做隐式淘汰。

### 首批控件与后续边界

首批产品控件只包含：

- `Panel`：容器、背景、padding 与可选 clip；
- `Label`：只读 UTF-8 文本和后端无关 text run；
- `Button`：Pointer/Keyboard/Gamepad 的统一 Action、Focus、Disabled 与 pressed 状态；
- `Modal`：overlay、独立 Focus/Input Scope、阻断下层 claim，并按上述语义处理 `UICancelAction`。

这些控件必须只组合 Tree、Style、Layout、Action、路由和 Paint primitives，不获得 RenderDevice 或平台句柄。
Checkbox、Slider、ScrollView、VirtualList、TextEdit、IME、复杂 shaping 和 accessibility adapter 属于后续独立
垂直切片；它们必须复用本 ADR 的 dirty、snapshot、claim、容量和 DisplayList 契约，不能为了兼容 Legacy UI
重新引入旧 Widget 基类、旧回调签名或旁路布局/输入 API。

## 结果

- UI 的 CPU 成本由 dirty 数量和实际变化区域决定，无变化帧可建立严格性能门禁；
- Null UI 可以验证布局、snapshot、路由、容量与 DisplayList，而不链接真实 Renderer 或 bgfx；
- Render backend 可以批处理 UI，但不能改变透明绘制语义或把 backend 生命周期反向泄漏给 UI；
- Tina 需要自行承担控件、文本 shaping、可访问性和视觉回归成本，并以垂直切片逐步交付。

M7-C1b/C1c-a/C1c-b1/C1c-b2 只完成无变化布局零工作、changed-frame 单 pass、committed hit snapshot、纯 point query
与 synthetic listener route；
“CPU 成本由实际变化区域决定”仍需后续 dirty subtree pruning 证明，不能从 dirty bit/queue 或 hit view
已存在直接推断。Runtime producer、Widget default action、可见 UI 和 DisplayList 也不能由 synthetic route 推断。

## 被拒绝方案

- 用 ImGui/RmlUi 替换产品 UI：与 Retained 生命周期、产品交互和确定性测试目标不匹配；
- UI 直接提交 bgfx：第三方类型、线程规则和 GPU 生命周期会泄漏到 Game SDK；
- 复用或兼容 Legacy UI API：会把旧生命周期和隐式更新语义固化到 vNext；
- 每帧全树 layout/paint：无法满足无变化 UI 的零工作与零 Tina heap allocation 门禁；
- 跨透明 paint order 全局排序：会为了 batch 数量改变可观察的混合结果。
