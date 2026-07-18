# ADR 0011：自研高性能 Retained UI，输出后端无关 DisplayList

- 状态：Accepted
- 日期：2026-07-16
- 修订：2026-07-19
- 实施状态：M7-C1b 已实现事务式 Flex-lite layout；M7-C1c-a 已实现固定容量 PMR Pointer policy/
  route-ancestry scratch、`Ignore`/`Targetable`、双缓冲 `UICommittedHitView` 与成功 `commitLayout()` 的
  structure/layout/hit 原子发布；M7-C1c-b1 已实现无分配 `queryPointerHit()`、反向目标选择与 visited count。
  M7-C1c-b2 已实现 fixed-capacity synthetic routed pointer event：generation-safe RAII listener token、
  48-byte fixed-inline `noexcept` callback、Capture→Target→Bubble、stop/consume、路由中 add/reset/destroy
  安全失效和 route/commit reentrancy guard。M7-C1c-b3b 已实现 Runtime-private
  `UIInputRouteProducer` 与独立 `tina_runtime_ui_tests`；M7-C1c-b3c 已让 `EngineHost` 私有延迟绑定
  primary-window `UIContext`，并按 Platform lifecycle dispatch → route → ActionMapper 接入正式帧；
  M7-C1c-b3d1 已加入 focused capacity config/shared validator、EngineConfig pre-factory validation 与
  `updateUI` 后、Render 前的 Runtime-private layout coordinator；M7-C1c-b3d2 已加入 startup seed 与
  root-scoped Game SDK UI facade；M7-C1c-b3e 已加入 held primary Pointer Button claim bridge。
  最新切片已加入 SolidFill-only local paint cache、双缓冲 `UICommittedPaintView`、Render-owned 单帧
  SolidQuad `UIDisplayListBuilder`，以及独立 `Tina::UIRenderIntegration` logical→framebuffer bridge；
  bridge 在 Windows MSVC 19.50 Debug/Release、Linux GCC 13.4 与 Linux Clang 22 sanitizer 构建中
  直接 GoogleTest 均为12/12，Clang 无 sanitizer 诊断。后续 Button default action 切片已实现
  `PrimaryPointerId + PointerButton::Primary` 的窄 default action、retained action property 与
  cancel/reset 清理；dirty-subtree b4a 又加入 prepared-input cache、clean-subtree Measure/Arrange reuse
  与父约束/viewport/Collapsed/候选失败回退；Windows Debug 通过基础208/208、UI115/115、Runtime→UI60/60
  与 Null 300帧。
  Key/Gamepad/axis claim、持久 Pointer Capture、Focus/Modal、Button Keyboard/Gamepad activation、
  Disabled/theme 视觉、Image/Text/Glyph PaintCache、Runtime `RenderFramePacket`、完整 dirty-range pruning、
  nested clip 与含资源 bgfx UI pass 仍后置。

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
M7-C1c-b3c 已先实现单主窗口 Runtime-private ownership：首次有效 primary `WindowId` 延迟创建 Context，
相同 owner/index/generation 复用；绑定后窗口消失或换代返回 `LifecycleInvariantViolation`，最小化、metrics
与 content scale 改变不重绑。Headless frame 在首次绑定前选择 `nullptr`，Context 在 modules 前销毁。
这不是全局对象，也尚不是完整多窗口 WindowRecord owner；在 b3c 切片当时，Game SDK 仍不能取得
Context 或创建 root，b3d2 后来只通过 scoped facade 开放 root 创建/更新。
M7-C1c-b3d1 没有改变该 ownership：`EngineConfig::primaryWindowUICapacities` 只把已验证的固定容量纯值
交给 owner，layout coordinator 仍是 Runtime-private phase owner，不是 Game SDK 可保存的 Context/writer。
`tina_ui` 仍只 PUBLIC 依赖 `Tina::Core` 与 `Tina::Platform`；`tina_render` 也不依赖 UI。
跨模块转换只存在于独立 `tina_ui_render_integration`，它 PUBLIC 依赖 `Tina::UI` 与 `Tina::Render`，
双方均不反向依赖该桥。Font Asset、FrameResourceRef/pin 与 bgfx UI Pass 在后续切片进入。

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

这是布局基础而非最终细粒度增量实现：当前已可复用 clean subtree 的 Measure/Arrange 调度与既有几何
结果，并在 Auto 祖先、父约束/viewport、Collapsed 子树和候选失败时回退完整布局；但
`buildLayoutOrder`、父级 `arrangeChildren`、committed layout、hit 与 paint snapshot 仍可能线性遍历。
后续完整 dirty-range pruning 必须在不改变本 ADR 事务、容量和 committed snapshot 契约的前提下，把工作量
继续收敛到实际失效区域。

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
5. 更新 PaintCache、composite、hit-test 和 semantics，并一次性发布新的 committed snapshots；当前最小实现
   只发布 SolidFill committed paint；
6. UI/Render integration 从 committed paint snapshot 生成本帧 DisplayList；后续再把它交给
   Runtime-owned `RenderFramePacket`。

layout barrier 后发生的结构、样式和布局 mutation 延迟到下一帧，禁止 hit-test、paint 或 Widget getter
隐式触发布局。提交期间外部只能看到上一份完整 snapshot 或新一份完整 snapshot，不能看到半更新树。
committed hit snapshot 与 committed paint snapshot 使用相同的 tree revision、layout revision 和 paint order
revision；revision 不一致属于生命周期错误，当前帧不得提交部分 UI。

`commitLayout(viewportSize)` 已作为 structure/layout/hit/paint 的事务发布入口：它按需构建
受影响候选，并只在全部验证成功后原子切换；`commitStructure()` 仅保留为 M7-C1a 结构诊断 seam，
可单独发布结构并保留旧 layout/hit/paint，Runtime 不得把完整发布拆开。viewport 变化即使没有 mutation 也重排；相同
viewport 且无 structure/layout dirty 时不增加 revision、不执行 layout pass。非法 viewport、候选
几何算术溢出、dirty queue、layout/hit/paint snapshot 容量不足都保留四份旧 published snapshot 与
pending dirty。

M7-C1c-b3c 的 primary-window owner 只负责 Context identity/lifetime，不调用 `commitLayout()`。正式输入
路由读取上一份 committed hit snapshot；hit-test/route 不得隐式触发布局。M7-C1c-b3d1 已由独立
UI phase coordinator 在 `IGameState::updateUI()` 成功后、Render submit 前按主窗口 logical extent
执行本帧下一份 layout/commit。每个有效且严格递增的 `PlatformFrameId` 至多尝试一次；Headless
窗口/Context 双缺席成功 no-op，identity 或 commit 失败阻断 Render 且消费当前 attempt，禁止同帧重放。

无变化 UI 必须满足以下硬门禁：

- `0` 次 Style/Measure/Arrange；
- `0` 个 PaintCache rebuild；
- `0` 字节 Tina-owned heap allocation；
- 允许按 committed paint snapshot 线性发射可见 DisplayList 并 pin 本帧资源，但不得扫描 mutable tree、
  重建 PaintCache 或转向 Tina heap 分配。

测试和 Metrics 至少记录 layout pass、style resolve、PaintCache rebuild、display command、dirty high-water、
capacity failure 与 Tina allocation delta。每窗口 layout pass 每帧只能为 `0` 或 `1`。

当前直接测试已覆盖50,000节点深树的非递归布局/hit snapshot，以及首次发布后连续300次同 viewport、
无 mutation commit 的0 layout pass、revision 不变和 supplied UI PMR allocation count 不增加；query/route
继续覆盖反向目标选择、Ignore 穿透、半开边界、非有限坐标 miss、Capture/Target/Bubble、路由中
reset/add/destroy、容量失败、off-thread token reset、reentrancy 和300次零新增 supplied UI PMR allocation。
最新 Windows MSVC 19.50 Debug 的 `tina_ui_tests` 为115/115，其中包含6项 dirty-subtree reuse/回退测试；
上一轮 Linux GCC 13.4 与 Linux Clang 22 sanitizer 的 `tina_ui_tests` 为92/92；`tina_tests` 均为205/205，其中含11项 Render
DisplayList builder 测试。独立 bridge 在 Windows Debug/Release 与两条 Linux 图均为12/12；Linux
Null 样例也各运行300帧，Clang 无 sanitizer 诊断。Windows Release 的完整 UI 与基础测试仍沿用 b3e
的81/81与194/194历史门禁，尚未针对最新切片重跑。上述结果仍不等价于 Game SDK paint authoring、
Runtime packet、bgfx UI Pass、可见 UI、进程 heap 或 GPU 资源门禁已经完成。

### PaintCache、DisplayList 与批处理

当前已实现的最小 PaintCache 只保存 `UIBoxPaint` 的可选 SolidFill：authoring color 使用 straight
sRGBA8，按确定性整数规则缓存为 premultiplied RGBA8；same-value setter 是 no-op。双缓冲
`UICommittedPaintView` 只发布 effective-visible、非透明节点的 `UINodeId`、logical world rect、
effective clip、严格递增 paint ordinal、预乘颜色及 structure/layout/paint-order/paint revisions。
paint-only commit 不执行 layout 或 hit rebuild；成功 no-op commit 不使旧 paint view 失效，任何候选失败
保留上一份完整的四份 snapshot publication。当前这不包含 Image/Text/Glyph、资源引用或 nested clip。

`Tina::Render` 已实现 UI-independent `UIDisplayListBuilder`。它在 Create 时从 supplied PMR 一次取得固定
command/clip/batch storage，当前只接受 SolidQuad，按首次出现顺序 intern axis-aligned clip，只合并相邻
兼容命令，并记录 paint-order checksum 与空 bounds、透明、空/相离 clip 剪枝统计。builder 是单缓冲：
`beginFrame()` 立即使上一 borrowed `UIDisplayListView` 失效；新事务失败或显式 rollback 后既不保留旧 view，
也不发布截断的新 view。

独立 `Tina::Integration::buildUIDisplayList()` 拥有一次完整 builder transaction。它用 committed
logical viewport 与调用方提供的 framebuffer viewport 计算横纵比例，origin 向下取整、非零 end 向上
取整并 clamp 到 half-open framebuffer viewport；空 logical/framebuffer viewport 成功提交空 list。
`beginFrame()` 本身失败时不回滚调用方已经打开的事务；一旦 begin 成功，validation、projection 或
capacity 失败都会 rollback。Windows MSVC 19.50 Debug/Release 的12项 bridge GoogleTest 均通过。

完整目标 PaintCache 仍是节点持久、后端无关的 local paint 结果。它只包含节点局部坐标下的 Quad/Text/Clip
片段和稳定资源引用，不包含祖先 transform、scroll、effective clip、Widget/UINode 指针或帧内存地址。
仅 `Paint` 或 local paint schema/version 变化可以重建 PaintCache；Transform、scroll、opacity 和祖先 clip
通过 composite snapshot 合成并复用原 cache。

完整 DisplayList 是 frame-local、只读、后端无关的顺序命令流。它不包含 backend vertex layout、ViewId、
uniform、encoder、GPU fence 或 bgfx handle。命令引用确定性 intern 的 effective clip，以及
packet-local `FrameResourceRef`。后续 packet builder 生成含资源命令的 DisplayList 时必须接收
`FramePinSink`：每个本帧使用的
字体 atlas、纹理页或其他可退役资源先成功 pin，再发布对应命令。

`FramePinSink` 是调用方拥有、不可保存的 frame capability；UI 只能在 DisplayList emission 期间把
模块私有 Atlas/Asset lease 类型擦除为 move-only `FrameLifetimePin`，再调用
`add(FramePinKind, FrameLifetimePin&&) -> Status` 转移真实所有权。`FrameResourceRef` 只是 packet-local
资源表索引，本身不能延长生命周期，因此禁止用“pin 一个 ref”冒充 owning pin。资源表 intern 与同一
资源的帧内去重由 packet builder/sink 协作负责；UI 不依赖重复 add 次数表达所有权，也不能把 sink
捕获进 Widget callback 或 PaintCache。`add()` 失败时调用方仍持有 pin，整份 frame packet 事务回滚。

未来含资源的 DisplayList、clip intern 表和 FramePinSink 记录都由 RenderFramePacket 的 frame storage 拥有，只能存活到
该 packet 完成提交/放弃；它们不得写回 PaintCache。资源 pin 由 RenderFramePacket 保活，并在 packet 完成
或回滚时统一释放，Widget 和 UIContext 不直接等待 GPU、查询 fence 或执行 unpin。

paint order 是可观察语义，特别是透明 Quad/Text。Renderer 只能合并 paint order 中相邻且
pipeline、texture、sampler、blend、effective clip 完全兼容的命令；遇到不兼容命令、clip 边界或顺序屏障
立即结束 batch。禁止为了减少 draw call 跨透明命令、跨节点或跨 clip 全局排序。

### 输入 consumption、claim 与路由边界

本节是 Accepted 目标语义。M7-C1c-a/C1c-b1 已提供 committed hit/route-ancestry 数据与纯 point query；
M7-C1c-b2 已提供 synthetic listener dispatch 与 Capture → Target → Bubble 执行。M7-C1c-b3b 已提供
Runtime-private producer：它只把 raw Pointer Move/Button/Wheel 逐 ordinal 转成 `UIPointerInputEvent`，并把
listener 的 consume 结果写入 `InputTransitionConsumptionView`；在 b3b 切片中
`ContinuousControlClaimsView` 当时恒为 canonical `None`。reset、cancel 与所有非 Pointer transition 不路由，也不伪造 Button Up，而是在 raw ordinal
空间保留 hole。M7-C1c-b3c 已在 Platform lifecycle dispatch 后选择 Context 并调用 producer，再把结果
交给 ActionMapper。Focus/Capture/Modal、Button default action、真实 continuous claims 与 Game SDK root
访问在该切片仍未实现。

下一实现切片冻结 Button 的第一条窄 default action，不扩大为完整 Widget 系统：

- 只支持主窗口 `PrimaryPointerId + PointerButton::Primary`；Keyboard Enter/Space、Gamepad Accept、
  Focus、持久 Pointer Capture、Disabled 与多 Pointer 继续后置；
- `Button` 创建后默认 `Targetable`，`Root/Panel/Label` 仍默认 `Ignore`；调用方可以显式把 Button 改为
  `Ignore`，但注册 action 本身不隐式修改 hit policy；
- Primary Down 的 committed route 中最近 Button 在 listener 完成后进入 armed/pressed，消费该 Down 并
  claim Primary button；held 期间 Move 只更新 Pointer 是否仍位于该 Button committed subtree，并继续
  请求 claim；Primary Up 总是清除 armed/pressed，只有 Up 仍位于同一 live Button subtree 时才激活一次；
- `InputCancelTransition`（非 gamepad-only）与覆盖当前窗口的 `InputStreamReset` 直接清除 retained
  Pointer interaction，不路由、不合成 Up、不触发 action；节点/root 销毁也立即使对应状态失效；
- `UIRoutedPointerEvent::preventDefaultAction()` 只阻止本次 Down arm 或 Up activation。它与
  `stopPropagation()`、`consumeInputTransition()`、`claimPointerButton()` 四者独立；Up/cancel/reset 的
  必要状态清理不可阻止，也绝不回写 Platform snapshot；
  `isDefaultActionPrevented()` 只观察本条 callback-scope route 的累计决定；
- action 是 Button 节点拥有的 retained property，通过 `setButtonAction()` 原子替换、
  `clearButtonAction()` 或节点销毁撤销，不返回 RAII token。`UIButtonActionCallback` 是48字节
  fixed-inline、move-only、`noexcept` callback；`UIButtonActionEvent` 只携带 Button identity、
  `PrimaryPointer` activation source、Platform frame 与触发 Up 的 source sequence，不暴露 Runtime/backend；
- `buttonActionCapacity` 为0时从 node capacity 派生，非0时不得超过 node capacity。实现使用固定 action
  slot pool 与一个不计入 published capacity 的预分配事务 slot，使满容量时替换已有 action 仍可原子
  staging；callback move/destructor 或 invocation 造成节点/root 自毁时依靠 generation、registration serial
  与延迟回收避免 UAF。route 中新 set/replace 的 action 从下一条 route 生效，clear 可阻止本条 route
  尚未执行的 default action。

producer 使用两份 Create 期预分配的 PMR consumption bitset，成功时交换 published/staging storage；supplied
`memory_resource` 必须比 producer 活得更久，连续300帧共用时 allocation count 不增长。失败测试先让 root
Move listener 产生1次 side effect，再让后续深层 Button route 因 route path capacity 失败；本次结果不发布、
上一份成功 view 保持，但 attempted frame/sequence watermark 已推进，同一 frame retry 被拒且 callback 仍为1，
明确证明 listener side effect 不回滚也不重放。独立 `tina_runtime_ui_tests` 直接运行 GoogleTest，覆盖 raw ordinal
hole、63/64位边界、事件时坐标、失败发布语义、保留 reset slot、数值预检、300帧 PMR 与 ActionMapper
suppression；它不经 CTest。上述 producer 用例本身只证明 b3b 私有桥，b3c 另以 owner 与
EngineHost lifecycle/order 直接测试覆盖正式接线。

M7-C1c-b3c 的 Runtime-private owner 在 Headless bind 前返回 `nullptr`，首个有效 primary Window 延迟绑定
`UIContext`，相同 generation 复用；绑定后 disappearance/replacement 是对本次 run 致命的结构化
lifecycle failure。最小化、metrics/content scale 变化不重绑，Context 在 Render → Task → Platform → Clock modules 前
shutdown。owner 不提交 layout；producer 始终只路由 previous committed hit snapshot。b3d1 coordinator
在同帧后续 `updateUI` 之后提交下一份 snapshot，不改变已完成的 route。当前 Game SDK 无 root capability，
因此正式路径虽已接线，仍没有可见或可交互 Widget。

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
当前 `UIContext::Create(ownerWindow, capacities, memory_resource)` 已用 supplied PMR
固定分配 tree/id、style/pointer-policy/dirty side array、dirty queue、layout/route-ancestry scratch、route path
scratch、listener slots、local SolidFill cache 与 committed structure/layout/hit/paint 双缓冲；
`dirtyQueueCapacity`、`layoutSnapshotCapacity`、`hitSnapshotCapacity`、`paintSnapshotCapacity` 和
`routePathCapacity` 为0时从 node capacity 派生，非0时不得超过 node capacity。
`routedPointerListenerCapacity` 为0时也从 node capacity 派生，可单独配置且最大为1,048,576。small
control-plane 对象、token state、off-thread `UIRootOwner`/listener release 队列仍在 Create 期间使用默认 heap 预分配。
M7-C1c-b3d1 把该配置移入 focused `UIContextConfig.hpp` 并公开 shared validator：node/root 必须非0且
不超过各自上限，root 不超过 node，非0的 dirty/layout/hit/paint/route-path 不超过 node，listener 不超过
其最大值。`EngineConfig::validate()` 在任何 factory 前复用该函数并把失败包装为
`InvalidEngineConfig`；Runtime 不维护一份可能漂移的平行规则。
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

M7-C1b/C1c-a/C1c-b1/C1c-b2 完成无变化布局零工作、changed-frame 单 pass、committed hit snapshot、纯 point query
与 synthetic listener route；M7-C1c-b3b 只完成 Runtime-private Pointer route-result producer，
M7-C1c-b3c 只完成 primary-window Context lifetime 与 EngineHost 顺序接线，M7-C1c-b3d1 只完成
bounded capacity 与每帧 layout commit；
“CPU 成本由实际变化区域决定”目前只由 clean-subtree Measure/Arrange reuse 部分证明；完整 dirty-range pruning
仍需后续证明，不能从 dirty bit/queue 或 hit view
已存在直接推断。最新 SolidFill committed paint、Render DisplayList builder 与 integration bridge 只闭合
后端无关数据转换；Widget default action、Runtime packet、bgfx UI Pass 与可见 UI 不能由这些组件、
private owner/producer、root-scoped facade 或 synthetic route 推断。

M7-C1c-b3d2 已按 [ADR 0021](0021-runtime-ui-startup-capability.md) 实现 startup primary-window metrics
seed 与 root-scoped、phase-epoch-scoped Game SDK capability。普通游戏仍不获得裸 `UIContext*`，也不能在
任意阶段调用 `createRoot()`；该 facade 与 startup snapshot 本身仍不构成可见 UI。

M7-C1c-b3e 已实现第一条 continuous-control claim producer：任意 Move/Wheel/Button route listener 都可
请求当前 Window/Pointer 的一个 primary Pointer Button，Runtime 只发布最终 snapshot 仍 held 的请求并
跨 route 去重。transition consumption 与 claim 相互独立；ActionMapper 先应用 claim，因此既能取消已
active 的 Gameplay source，也能拦截同帧未 consume 的 ButtonDown，并抑制到真实 Up。后续 Button default
action 切片又复用该 producer 阶段，实现 primary Pointer 的 pressed/activation、`preventDefaultAction()`、
retained `setButtonAction()`/`clearButtonAction()` 与 cancel/reset 清理。该实施不改变本 ADR 对后续
Pointer Capture、Focus/Modal、Button Keyboard/Gamepad activation、完整含资源 DisplayList、Runtime packet 与
文本渲染的边界。

## 被拒绝方案

- 用 ImGui/RmlUi 替换产品 UI：与 Retained 生命周期、产品交互和确定性测试目标不匹配；
- UI 直接提交 bgfx：第三方类型、线程规则和 GPU 生命周期会泄漏到 Game SDK；
- 复用或兼容 Legacy UI API：会把旧生命周期和隐式更新语义固化到 vNext；
- 每帧全树 layout/paint：无法满足无变化 UI 的零工作与零 Tina heap allocation 门禁；
- 跨透明 paint order 全局排序：会为了 batch 数量改变可观察的混合结果。
