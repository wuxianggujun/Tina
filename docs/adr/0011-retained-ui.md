# ADR 0011：自研高性能 Retained UI，输出后端无关 DisplayList

- 状态：Accepted
- 日期：2026-07-16
- 修订：2026-07-17
- 实施状态：M7-C1a 已实现 `tina_ui` 树核心、generation `UINodeId`、`UIContext`、
  `UIRootOwner` RAII、结构 snapshot 和输入 route-result view ABI；layout、hit route、
  DisplayList、widgets、FreeType、bgfx UI pass 与 Runtime UI producer 仍后置。

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
当前 M7-C1a `tina_ui` 只依赖 `Tina::Core` 与 `Tina::Platform`；Font Asset 与 Render descriptor/
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

无变化 UI 必须满足以下硬门禁：

- `0` 次 Style/Measure/Arrange；
- `0` 个 PaintCache rebuild；
- `0` 字节 Tina-owned heap allocation；
- 允许按 committed paint snapshot 线性发射可见 DisplayList 并 pin 本帧资源，但不得扫描 mutable tree、
  重建 PaintCache 或转向 Tina heap 分配。

测试和 Metrics 至少记录 layout pass、style resolve、PaintCache rebuild、display command、dirty high-water、
capacity failure 与 Tina allocation delta。每窗口 layout pass 每帧只能为 `0` 或 `1`。

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
当前 M7-C1a 已实现的 `UIContext::Create(ownerWindow, capacities, memory_resource)` 中，
`memory_resource` 只用于 tree/id/committed structure snapshot storage；small control-plane 对象和
off-thread `UIRootOwner` release 队列在 Create 期间使用默认 heap 预分配。owner thread 析构
`UIRootOwner` 立即回收；非 owner thread 只入队 root id，由下一次 owner-thread UI mutation/commit
drain 并物理回收。

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

## 被拒绝方案

- 用 ImGui/RmlUi 替换产品 UI：与 Retained 生命周期、产品交互和确定性测试目标不匹配；
- UI 直接提交 bgfx：第三方类型、线程规则和 GPU 生命周期会泄漏到 Game SDK；
- 复用或兼容 Legacy UI API：会把旧生命周期和隐式更新语义固化到 vNext；
- 每帧全树 layout/paint：无法满足无变化 UI 的零工作与零 Tina heap allocation 门禁；
- 跨透明 paint order 全局排序：会为了 batch 数量改变可观察的混合结果。
