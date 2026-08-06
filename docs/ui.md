# Retained UI

当前产品 UI 位于 `include/tina/ui` 与 `src/ui`。旧 Legacy UI 产品图已删除，但这两个目录是 vNext
正式实现，不得作为 Legacy 残留移除。架构决策见 [ADR 0011](adr/0011-retained-ui.md)、
[ADR 0021](adr/0021-runtime-ui-startup-capability.md)和
[ADR 0022](adr/0022-ui-element-authoring-and-layout.md)。当前实现与下一阶段框架演进的分界见
[UI 框架设计](ui-framework.md)和 Accepted [ADR 0023](adr/0023-ui-extensibility-style-paint-motion.md)。

## 当前能力

| 领域 | 已实现 |
| --- | --- |
| 所有权 | per-window `UIContext`、generation/owner-aware `UINodeId`、move-only `UIRootOwner` |
| Authoring | `UIElementDescriptor` + `make*Element` recipes、组合 Semantics、StyleRole/override reset、固定预算 build transaction；旧 create-by-kind 与公开 `UIWidgetKind` 已删除 |
| Tree | Root/Panel/Modal/Label/Button/Checkbox/Slider/ProgressBar/RadioButton/TextEdit/ScrollView/Dropdown/Popup/DropdownItem/ListView/TreeView，固定容量 mutation |
| Layout/Content | Flex container/item 分离、Flow/Overlay placement、logical pixel、committed content placement、事务 commit、clean-subtree reuse |
| Hit/route | committed hit snapshot、Capture→Target→Bubble、持久 Pointer Capture、Modal/Popup barrier、listener token、consume/prevent/claim |
| Paint | box/text/control paint、第一类 Image/Icon content、固定容量 backend-neutral `SolidRect`/`Image`/`NineSlice` Canvas、axis-aligned clip、PaintCache、committed paint snapshot；NineSlice 在 commit 时原子展开为1..9个 Image entry |
| Theme/Style（A/B/C1 + UI-STYLE-001 slice） | `UITheme` token + `UIStyleRoleId` recipe + 属性 override mask/reset；强类型 StyleClass/ColorToken、node-local pseudo-state、literal/token-backed BoxFill stylesheet 与运行期 ColorToken getter/setter 已落地；token 更新经固定 reverse-dependency 链为 `O(affected links)`，**无**圆角子树 clip/毛玻璃/完整 CSS |
| Motion | **Done：** 可伪造 clock + fixed-capacity paint-only tracks；Runtime phase facade；显式 begin*Transition；stylesheet `BackgroundColor` transition 在 Style 绑定阶段持久预留、pseudo-state 变化时激活；showcase 主题切换 card transition（`motionBegins`）；`ui_motion_v1` |
| Text | strict UTF-8、可选 FreeType rasterizer、R8 Glyph atlas、DisplayList Glyph |
| Input | Focus Scope/显式 focus、Pointer capture/cancel、Tab 与 committed 几何空间焦点、Keyboard/Gamepad activation、Dropdown 与 List/Tree navigation、TextEdit edit/selection/IME；UI Flow 固定 16 槽本地用户、Gamepad assignment 与 per-user 设备 revision |
| Semantics | Automatic/Publish/MergeDescendants/Exclude、显式 role/name/description/actions、状态/range/value snapshot 与虚拟 item 元数据 |
| Runtime | startup root builder、phase-scoped tree updater 与 bounded component transaction（含集合 DataSource/metrics/selection/scroll/expansion）、DisplayList/Glyph atlas handoff |
| Performance | `tina_bench` 已接入 static commit、paint-only dirty、route/capture、100k virtual collection、Image/NineSlice 与完整 Component build schema-v1 provisional workload |
| Product | 独立 20 控件 showcase、product-2d Scene Explorer TreeView、product-3d Asset ListView/Scene TreeView 与 Dark/Light Windows 视觉证据 |

## 所有权与句柄

`UIContext::Create(window, capacities, resource)` 在创建时固定 node/root/listener/layout/paint/text/semantics
等 storage 容量。`paintSnapshotCapacity` 为0时从 node 数派生，非0时可独立提高到8,388,608，以容纳
单节点的 glyph/control/Canvas/NineSlice 多 entry 展开；Semantics entry/scratch 仍严格按 node 数分配。
`UINodeId` 同时校验语义 owner WindowId、registry owner、slot 与 generation；stale、
cross-context、cross-window ID 必须失败。

`UIRootOwner` 是 root 的唯一 RAII owner。销毁 root 会使其子树 ID 失效；listener token 不保活 Context/
root。产品 State 在退出时应先 reset listener，再 reset root。Context 只能在 owner thread mutation/commit。

### 私有实现职责边界

公开 `UIContext` 继续作为每窗口唯一的生命周期、owner-thread、capacity 与提交门面，不新增第二套 UI
ABI。大体量私有实现按可独立验证的职责逐步下沉到 `src/ui/detail`：`UITextStorage` 已独立拥有固定容量
UTF-8 arena、空闲块复用/合并、bump 回收与 used/high-water 统计；`UIImeCompositionState` 独立拥有
固定 preedit buffer、active/cursor 状态、容量契约和可复制的事务快照；
`UIButtonActionRegistry` 与 `UISliderChangeCallbackRegistry` 分别独立拥有 Button/Slider callback 的
固定容量 slot、stage/commit/rollback、generation-aware capture/invoke 与调用期间延迟回收；Button
registry 还封装 route clear barrier、registration serial 与 high-water 统计。`UIContext::Impl` 只保留
owner/root/kind 校验并编排 UTF-8 语义校验、测量、dirty transaction、焦点和控件行为。私有组件不得反向持有
`UIContext`，也不得绕开 committed snapshot、owner-thread 或 PMR 固定容量约束。

Runtime 私有持有主窗口 UIContext；普通游戏不取得裸 `UIContext*`：

- `GameStateEnterContext::primaryWindowUIRootBuilder()` 只在 `onEnter()` 当前 epoch 有效；
- `UIUpdateContext::primaryWindowUITreeUpdater(root)` 只在 `updateUI()` 当前 epoch 对该 root 有效；
- `PrimaryWindowUITreeUpdater::committedLayoutRect(node)` 复制上一轮成功 layout commit 的 root-scoped
  `worldRect`；返回值不借用 snapshot，未提交/跨 root/失效节点返回 `InvalidNode`；
- `PrimaryWindowUITreeUpdater::beginBuildTransaction()` 返回的 move-only transaction 只在当前 epoch 有效；
- facade 的第一次失败成为 phase sticky error，回调结束后统一返回；
- builder/updater/transaction、span 和 committed view 不得跨 phase 保存；活动 transaction 若逃逸 callback，
  phase finish 会先回滚整棵组件，再返回 `BuildTransactionInProgress`。

`PrimaryWindowUITreeUpdater` 通过统一 `createElement()` 创建 ListView/TreeView，并暴露其 DataSource、
style/paint、metrics、selection、scroll，以及 Tree expansion 操作；这些 facade 与其他 mutation 一样受 phase epoch 和
owner-thread 约束，不把 `UIContext` 或第三方类型暴露给游戏。
直接 `UITreeUpdater` 提供相同的 `committedLayoutRect()` 数值复制契约。由于 Runtime 的 RenderScene extraction 位于
本帧 UI update/commit 之前，组合层在 `updateUI()` 读取 rect 后应从下一帧开始使用，不能假定正在修改的布局已发布。

## Element authoring 与布局契约

当前已完成 ADR 0022 的 authoring/composition 主体：`UIRootBuilder`、`UITreeUpdater` 与 Runtime phase facade
统一接收 `UIElementDescriptor`，旧 `createPanel/createButton/createListView/...` 成员 API 不再保留。
`makePanelElement()`、`makeButtonElement()`、`makeListViewElement()` 等官方 recipes 负责给出内建控件的
behavior、默认内容对齐、Semantics、StyleRole 及集合容量配置；调用者可在创建前一次性补齐 layout、
text/text style、visual box/Canvas、enabled、pointer hit policy 与 focus scope。intrinsic/semantics
`string_view` 和 Canvas span 会复制进 `UIContext` 的固定容量 storage；任一 descriptor 校验、测量或容量
失败时，本次节点及全部 side storage 回滚。

布局属性按解释方拆分：

- `flexContainer`（direction/justifyContent/alignItems/gap）由父节点解释；
- `flexItem`（grow/shrink/basis/alignSelf）由父容器为当前子节点解释；
- `placement=Flow` 参与正常 Flex 顺序，`placement=Overlay` 使用 horizontal/vertical alignment、offset 与
  margin，不参与 Flow 尺寸和顺序；Popup recipe 强制 Overlay，仍走受控 anchor/flip/clamp policy；
- tree 顺序同时决定 layout、paint、focus 与 semantics 顺序，没有 CSS `order` 双轨。

控件内部文字使用独立的 `UIContentAlignment`，不再借用父容器的 child alignment。layout 将
`contentBox/origin/intrinsicSize` 作为 `UICommittedContentPlacement` 随 snapshot 一次发布；文字绘制、
caret/selection 与 pointer-to-codepoint 读取同一份 committed 结果。

`UISemanticsDescriptor` 支持 Automatic/Publish/MergeDescendants/Exclude、显式 role/name/description/actions；
Merge 按 tree 顺序合并 eligible descendant name，Exclude 删除完整语义子树。`UIStyleRoleId` 与 behavior/
semantics 独立，属性 setter 只 detach 对应 theme binding，`clearOverride()` 从当前 role/theme 恢复；
Runtime phase facade 同样可切换/query role 和 reset override。

当前 `UISemanticsRole::Image` 已实现。`makeIconElement()` 默认作为装饰内容使用 `Exclude`；icon-only Button
必须由 Button root 提供显式 name。`makeImageElement()` 只为独立表达信息的图片发布 Image role，并要求
调用方显式给出 name，不能从 AssetId 或资源文件名推导可访问名称。

多节点业务组件可通过 `UIElementBuildTransaction` 或 Runtime 的 `PrimaryWindowUIBuildTransaction` 声明完整
`UIComponentBuildBudget`；node、UTF-8 byte、Canvas command、Activate/Toggle/Range/TextInput/Scroll/Select slot
会在创建 component root 前统一预留，集合内部 row pool 也计入预算。创建失败、reset 或析构会回滚整棵组件，
active transaction 期间 structure/layout commit 返回 `BuildTransactionInProgress`。Runtime facade 由 capability
state 持有底层事务并逐操作校验 epoch/phase；成功 commit 后只留下普通 retained subtree，不保留 component
wrapper。公开 `UIWidgetKind` 已删除；私有 `BuiltinElementKind` 只服务成熟控件 storage/行为分派。
Canvas 命令复制到固定容量 storage：`SolidRect` 支持统一 `cornerRadius`，`Image` 复用 `UIImageSource`，
`NineSlice` 使用 source-pixel/destination-logical insets 且首版仅 Stretch。逐角半径、圆角子树 clip 与
BoxFill 之外的属性面仍属后续扩展。

`UIContext::styleColorToken()` / `setStyleColorToken()` 以及 phase-scoped
`PrimaryWindowUITreeUpdater` 对应入口提供运行期 ColorToken 读取和更新。相同值 no-op 的四个
`lastStyleTokenUpdate*` counter 均为0；非 no-op 只遍历 reverse-dependency 链上的节点并预检 dirty queue，
成功后再沿同一依赖链发布 Paint dirty。inspected/resolved/affected 均按依赖节点计，reverse path 上
`candidate=0`（不再做全树 resolve）。容量失败仍保留这组检查统计，但 token 值、dirty state 与 committed
snapshot 均不改变。这些公开接口也不代表正式发布 ABI 已冻结。

### 第三方扩展边界

当前第三方可以使用 Tina 的公开 Element authoring 组合业务 UI，但“组合业务组件”和“注册全新控件原语”
不是同一能力：

| 扩展方式 | 当前状态 | 边界 |
| --- | --- | --- |
| 组合 Panel/Label/Button/List 等 retained 子树 | 可用 | 通过统一 `createElement()` 与官方 `make*Element()` recipe |
| 自定义布局、Semantics、命中策略、局部 box/text paint | 可用 | 仍受 fixed-capacity、owner-thread 与 phase 生命周期约束 |
| 多节点业务组件 | 直接与 Runtime phase facade 均可用 | `UIElementBuildTransaction` / `PrimaryWindowUIBuildTransaction` 统一预留 node/text/canvas/Behavior 完整预算、失败整棵回滚；Runtime transaction 不得跨 callback 保存 |
| 自定义 Canvas | 可用首版 | backend-neutral `SolidRect`、`Image`、Stretch-only `NineSlice`；只保存 AssetId/图片元数据，不能提交 shader、GPU handle 或任意 paint callback |
| 自定义 Theme/外观 | 部分可用 | 可替换 `UITheme`、选择封闭 `UIStyleRoleId`、做属性级 override，在首个 retained node 前注册强类型 StyleClass/ColorToken、安装 node-local pseudo-state + literal/token-backed BoxFill rule，并在 owner thread/有效 Runtime phase 更新 ColorToken（reverse-dependency `O(affected)`）；尚无通用 selector / descendant 匹配 |
| 自定义交互 | 部分可用 | 可组合标准 Activate/Toggle/RangeInput capability、挂 routed listener 和使用现有控件 callback；不能注册全新的 Behavior/state machine |
| 安装后作为外部 SDK 使用 | 主要切片可用 | backend-neutral `Tina::GameSDK`、PlatformGlfw、DesktopBootstrap/RenderBgfx、UIFreetype 与 AudioMiniaudio 已有安装 consumer；Windows/Linux moved-prefix 及 Ubuntu producer → Debian consumer gate 已通过；待正式 ABI 策略 |

`UIElementBehavior` 在公开头中表现为正交 flags。Activate/Toggle/RangeInput/TextInput/Scroll/Select 已迁移到私有固定容量 side store：
创建时按 capability 对六个 pool 原子预检并发布 slot，destroy/事务回滚会释放并复用 slot；Activate action、
Toggle state、Range value/range setter/default behavior、TextInput selection setter/query 与 Scroll style/offset/metrics 按 capability 校验，
Select pool 持有 Dropdown 当前选项。`UIContextStatistics` 同时公开六个 pool 的 capacity、active 与 high-water。Slider paint/change callback/
Pointer drag geometry、TextEdit paint、ScrollView paint/thumb geometry，以及 Dropdown selection API、popup 关系、paint 与输入路由仍是对应 private kind 的具体能力；
TextInput/Scroll/Select resolver 当前仍选择 TextEdit/ScrollView/Dropdown，以复用既有输入与视觉路径，不受支持的混合组合继续返回
`InvalidElementDescriptor`。因此当前任意组合面仍是 Activate/Toggle/RangeInput；TextInput、Scroll 与 Select 只完成状态所有权迁移，
不能仅靠新增 flags 得到任意输入路由、滚动条视觉、Dropdown popup、Knob、自定义 drag state machine 或新的 Behavior SPI；更高阶
SPI 继续后置，不能绕过 committed snapshot 或 Render 边界。

## Tree 与事务提交

Tree mutation、layout、hit、paint 与 semantics 都有固定容量和明确 commit。失败不能发布半份 snapshot；
下一次成功 commit 才替换旧 view。UI input route 读取上一帧 committed hit snapshot，本帧 `updateUI()`
后的 layout/paint 在 Render 前提交，并从下一帧开始参与命中。

`UIElementBuildTransaction` 开始后其子树是 live retained state，但任何 committed view 都不能中途观察它；
成功 `commit()` 只结束 build guard，下一次正常 `commitLayout()` 才一次发布。预算耗尽、子节点 descriptor
失败、显式 reset 或析构都销毁 component root，从而统一回收子树、UTF-8、Canvas 与 Behavior slot；
`UIContextStatistics::componentBuild` 同时发布各池 requested/reserved/published/failure/outstanding counter。

Layout 使用窗口 logical extent，不直接读取 framebuffer pixel。content scale/resize 更新 layout size，但同一
WindowId 不重建 Context。clean-subtree measure/arrange reuse 已实现；ListView/TreeView 通过固定 row pool
支持 100k logical item 虚拟化，完整通用 dirty-range pruning 仍未实现。

Tree structure publication、subtree destroy 以及 layout/hit/paint snapshot 构建不依赖 C++ 调用栈递归；
专项 stress gate 使用 50,000 层 retained tree 覆盖这些路径。Popup 最终绘制顺序所需的
`inPopupSubtree` 在 layout preorder 中一次传播，publication 不再为每个节点重复回溯祖先链。

当前性能边界需要如实区分：无 dirty 且 viewport 不变的 `commitLayout()` 会直接返回，
`lastLayoutPassCount/lastHitRebuildCount/lastPaintSnapshotRebuildCount` 均为零；paint-only 状态变化不会使
Layout/Hit dirty，dirty paint cache 只重算变化节点。但是当前 paint candidate 的容量校验与 committed
snapshot 组装仍会线性遍历 layout/paint 数据，因此不能把一次局部 hover/pressed 更新描述为完整
publication `O(1)`。Style/Motion/Image 扩展必须沿用现有 counter/high-water，并由 `UI-PERF-001` 分别测量
clean commit、局部 dirty、active Motion 与 DisplayList 构建；口径见[性能与内存](performance-memory.md)和
[UI 框架设计](ui-framework.md#性能模型与合入门槛)。

`UILayoutStyle::padding` 同时参与 measure/auto-size 与 committed content box。所有 intrinsic-text
element（Label、Button、TextEdit、RadioButton、Dropdown/DropdownItem 及虚拟 List/Tree row）的文字
origin 都由 `UIContentAlignment` 在该 content box 内计算；多行文字回到同一 committed origin，
TextEdit pointer-to-caret、selection 与 caret 也使用该 origin。四边 padding 均参与固有尺寸和 content
box，不再由 paint/input 各自从 `worldRect + padding` 重算平行结果。

## 输入与默认行为

Pointer route 顺序为 Capture → Target → Bubble。listener 可以分别：

- stop propagation；
- prevent default action；
- consume 当前 transition，阻止 Gameplay Action；
- claim held continuous control，直到真实 release/reset；
- `capturePointer()` / `releasePointerCapture()` 改变后续 transition 的 routed target，同时保留物理
  `pointQuery.target` 供 drag/drop、hover 与边界判断使用。

这些语义互不隐式替代。Primary Up 是 capture release barrier。capture target 被禁用、销毁、Hidden/
Collapsed，或因新 Modal 离开 committed scope 时，UI 沿原 committed ancestry 合成一次
`PointerCancel` 再释放；该 kind 只允许 listener 注册，外部 `routePointerInput()` 不能伪造。输入流
cancel/reset 同样清理 pressed/armed/focus/edit state，不伪造普通 Up，也不允许同帧失败后重放已经发生的
callback 副作用。

当前默认行为：

- 通用空间焦点：已有普通控件焦点时，Keyboard Arrow 与 Gamepad D-pad 复用
  `UIFocusNavigationDirection`，按 committed `worldRect` 在 Left/Right/Up/Down 半平面内选择候选；优先
  同轴 beam，再比较主轴间距、次轴间距和稳定 paint ordinal。无焦点、无方向候选或边缘不 wrap 时不消费
  Gameplay input；成功移动后以固定容量 latch 消费匹配 release；
- Button：primary pointer pressed/action，Enter/Space/KeypadEnter/Gamepad South activation；
- Checkbox：Pointer/Keyboard/Gamepad toggle；
- Slider：Pointer drag、range/value clamp；`requestFocus()`、Tab、空间焦点与 Primary drag 都收敛到同一
  committed keyboard focus；focused Slider 将 Left/Down Arrow 与 D-pad 映射为 Decrease，将 Right/Up
  映射为 Increase，复用 Pointer/UIA 的 min/max/step/clamp、量化、value storage 与 callback 路径；
- RadioButton：同一直接父节点互斥，Pointer/Keyboard/Gamepad selection；
- ScrollView：wheel、thumb drag、轴向 clamp 与持久 pointer capture；
- Dropdown/Popup：Pointer/Keyboard/Gamepad 开关与选择，Up/Down/D-pad 导航，Escape/Gamepad East dismiss，
  Tab/Shift+Tab 关闭并退出 Popup scope，外部点击关闭且阻止 click-through；
- ListView：Up/Down、PageUp/PageDown、Home/End、Keyboard/Gamepad activate 与 stable-key selection；
- TreeView：沿用集合导航，并以 Left/Right 折叠、展开或移动到父/子项；
- ListView/TreeView 的 `rowHeight` 是精确行高，必须容纳当前 `CollectionItem` 单行文本；内部 row 仅保留
  横向 padding，纵向内容盒使用完整行高并居中放置文本。字体 raster batch 显式发布 line-top baseline，
  glyph paint 与 clip 必须完整落在所属 row 内，不能依赖下一行覆盖或静默裁掉 descender；
- TextEdit：Pointer focus/selection，Tab traversal，Left/Right/Home/End、Backspace/Delete、Shift selection、
  Ctrl+A、committed text 与 IME；
- ProgressBar：非交互 determinate range/value，hit policy 为 Ignore。

RangeInput 调值在 Dropdown/ListView/TreeView/TextEdit 等复合方向控件之后、通用空间焦点之前路由。
只有成功改变 value 的 Down 才用 fixed-capacity exact-control latch 消费匹配 Up；焦点或 enabled 状态在
Down/Up 之间变化不会泄漏半个 transition。read-only 或边界值目标不修改、不 latch、不误触发空间焦点，
未消费 transition 继续对 Gameplay 可见。

`UI-004` 已完成：`UIFocusScopeMode::Contain` 将 Tab/Shift+Tab 限制在 committed scope；显式
`requestFocus()` 拒绝未提交、隐藏、disabled、非 Targetable、非键盘控件及 active Modal 外节点。
最后绘制的 committed visible Modal 是 active Modal，屏蔽下层 hit/Tab 并消费 backdrop 输入；嵌套 Modal
逐层保存/恢复 focus，跨 root Modal 释放后也恢复原 root 的有效焦点。commit 的 Modal/focus/capture
变化与 paint/semantics 一起事务发布，失败提交不发送 `PointerCancel`、不释放旧 capture。空间方向导航
沿用相同 committed Modal/Contain scope；disabled、非 Targetable 与复合控件内部 Dropdown/List/Tree item
不会成为通用候选。Dropdown、ListView、TreeView 与 TextEdit 始终优先执行各自的方向命令。

### 状态反馈与 Motion

Button 已有真实点击反馈。默认 Theme/control paint 仍是**即时状态切换**；当 Context 显式配置
stylesheet `BackgroundColor` transition，匹配 stateful BoxFill rule 的节点会使用 paint-only 动画：

- hover/focus/pressed/disabled 选择各自背景色，pressed 优先于 hover/focus；
- pressed 时交换亮/暗边框并把 shadow offset 收为零，形成按下深度；
- Pointer、Keyboard、Gamepad 与 UIA 最终复用默认 action/callback，不等待视觉效果结束；
- 未配置 stylesheet transition 时，状态改变只使后续 paint snapshot 采用新值；配置后使用 monotonic clock、
  duration/delay/easing，并在再次变化时从当前 presentation value retarget；仍没有 keyframe timeline。

现有控件的状态视觉还没有统一成通用 VisualState，各控件仍由既有输入状态直接解析 paint。Checkbox 的
外 indicator 与 RadioButton indicator 已使用同一 hover tracking，采用 pressed > hover > focus > normal
优先级；disabled 继续使用共享 widget opacity，checked/selected 前景不建立第二份状态。Slider 已对齐
`Focusable` 契约：私有 trait、显式/Tab/空间焦点与 Primary drag 使用同一 committed focus，semantics
发布真实 focus；其 thumb 的状态优先级为 drag > focus > normal，产品主题用 `focusRing` 提供 focus 色。
ListView/TreeView 的 selected overlay 也复用既有 row hover/press 与 collection focus，采用 pressed > hover >
focus > selected；虚拟 row 不保存第二份状态。TextEdit 通过独立 `UITextEditPaint` 复用唯一
`hoveredPrimaryControl`、`armedTextEdit` 与 committed text focus，背景按 disabled > pressed > hover > focus >
normal 解析；selection/caret 颜色也由同一 paint 提供。Primary Up/cancel、disable、Hidden/Collapsed、destroy
与 Modal scope change 均沿既有状态清理和原子 commit 路径移除 stale focus/pressed feedback。

首版 Motion 保持 fixed-capacity 且只覆盖 paint-only 属性：背景/边框/文字颜色、opacity、统一圆角和
visual offset。声明 transition 的匹配节点在 Style 绑定/候选阶段持久预留 BackgroundColor track；启用时
对已有节点先做全量容量预检，容量不足保留旧 spec，输入状态变化只激活已预留槽。reserved 与 active
分别统计 count/high-water；reduced-motion 直接落到 target，不进入 active list。建议 Button hover
80-120ms、press 40-60ms、release 80-120ms；
动画不得延迟 callback、改变真实 hit rect、隐式延期 `destroy()`，也不得为 active Motion 建立第二套游戏
update loop。完整 keyframe timeline、spring/inertia 与 layout animation 不属于首版。

## Text、UTF-8 与 IME

所有 UI 文本是 strict UTF-8，无 embedded NUL；MSVC target 使用 `/utf-8`。TextEdit 当前为单行，拒绝
CR/LF，selection/caret 按 Unicode scalar index 维护，不把 UTF-8 byte offset 暴露给游戏。

文本路径：

```text
Intrinsic element text (Label/Button/TextEdit/Radio/Dropdown/List/Tree row)
  -> text measure/layout
  -> Glyph placement
  -> UI-owned R8 atlas
  -> committed paint
  -> Render UIDisplayList Glyph command
  -> private bgfx atlas texture + textured UI pass
```

FreeType 是可选私有 rasterizer。字体 fixture 优先由 `TINA_UI_FONT_PATH` 注入；未加载字体时 placeholder
路径不能冒充 CJK 视觉通过。Windows GLFW adapter 已提供 IMM32 preedit/commit/cancel；Linux 当前只保证
committed text。多行、grapheme、BiDi/shaping、候选窗定位见 `TEXT-001`。

## Semantics 与 accessibility

`committedSemantics()` 按每个 Element 的 `UISemanticsDescriptor` 发布，而非按公开 widget kind 推导。
官方 recipes 覆盖 Modal、Label、Button、Checkbox、Slider、ProgressBar、RadioButton、TextEdit、
ScrollView、Dropdown、Popup、DropdownItem、List/ListItem 与 Tree/TreeItem：

- role/name/description/actions；
- checked/selected；
- min/max/value；
- TextEdit valueText；
- collection virtual item key/index、Tree level 与 expanded；
- 有限 focused state；
- Modal 的 Dialog role。

Automatic 省略自身但保留 eligible descendants；MergeDescendants 发布自身并按 tree 顺序折叠后代 name；
Exclude 删除自身和完整语义子树。entry parent 始终是最近 published ancestor。显式空 name 与“未指定”不同，
不会回退 intrinsic content。UIA/Accessibility action 必须先由当前 committed entry 的 actions 发布，再复用
对应 control behavior；live behavior 本身不能绕过语义授权。

这是后端无关 semantics snapshot，不等同于平台辅助技术。

`UIAccessibilityTree` / `IUIAccessibilityProvider` / `UIAccessibilityProbeProvider`（UI-002-SPI）
从 `committedSemantics()` 构建可查询的无障碍节点表（role/name/state/range/value）。
`UIUpdateContext::committedSemantics()` 与 `PrimaryWindowUICapabilityState::committedSemantics`
暴露同一快照；`tina_sample_2d` 每帧 `updateUI` 经 probe 发布并输出 `accessibility*` JSON 证据。
Probe 可验证 stale node 拒绝。

平台无关 action seam 使用 `UIAccessibilityAction` 表达 Focus、Invoke、Toggle、SetRangeValue 与
SetTextValue；`UIContext::performAccessibilityAction()` 在 owner thread 验证 generation、控件 kind、
enabled/read-only/range 与 UTF-8，再复用控件默认行为。平台 adapter 只负责线程 marshal 和平台协议映射，
不能直接修改 retained storage。

**Windows UIA 私有 adapter（UI-002，可选）：** `TINA_BUILD_UI_UIA=ON`（Windows-only）时构建
`tina_ui_uia` 并让 `tina_runtime` 在 WindowSurface 产品路径上自动接线：

1. 属性映射：`UIAccessibilityTree` → UIA ControlType/Name/Enabled/Focus/Selection/Range/Toggle/Value；
2. **HWND 桥**：`WindowsUiaHostBridge`（`SetWindowSubclass` + `WM_GETOBJECT` +
   `IRawElementProviderSimple` 根/子 fragment）；公开工厂头仍零 COM；
3. **产品自动 attach**：`EngineHost` 从主窗口 lease 解码 Win32 HWND，startup layout 与每帧
   `commitForFrame` 后 `rebuildFrom(committedSemantics)` 并 `publish`；shutdown 时 detach；
4. **Control patterns 与 action**：Button/Checkbox/RadioButton/Slider/TextEdit 分别发布并执行适用的
   `IInvokeProvider`、`IToggleProvider`、`IRangeValueProvider`、`IValueProvider`；跨线程 COM 调用通过
   HWND registered message 同步 marshal 回 UI owner thread。

`tina_ui_uia_tests` 覆盖映射、provider、control patterns 与 HostBridge attach/navigate；
`RunUi002UiaGate.ps1` 从外部进程连接真实 showcase HWND，验证 Tina Framework、唯一 RuntimeId/AutomationId、
fragment 父链、动态属性 republish 以及 Invoke/Toggle/RangeValue/Value/Focus action。**Narrator/Inspect 人工金标仍是 Windows UI-002 的开放验收；
Linux AT-SPI 已拆为独立后置项**。自动 gate 不等于真实 screen reader 合规金标。

## Render 边界

UI 不调用 bgfx。`tina_ui_render_integration` 把 committed paint 转为固定容量 `UIDisplayList`，Runtime
在 `RenderFrame` 中只借用 DisplayList 和可选 R8 atlas page。backend 必须在 `submitFrame()` 内同步
消费。

当前支持 SolidQuad/Glyph/ImageQuad、带统一圆角半径的 Box/Element Canvas `SolidRect`、Image/Icon content、
Canvas Image/NineSlice 与 axis-aligned scissor。Runtime `RenderFramePacket`/FramePin 的 present-return CPU
completion 已落地（Null 同步 complete）；root-scoped resolver 在 frame packet 构建时按
`(root scope, AssetId)` 去重并 pin Texture2D。图片产品/失效/尺寸矩阵与固定性能 workload 已关闭；
rounded/stencil 子树 clip 和跨 GPU/DPI golden（UI-003）尚未完成。

## 实际绘制链路

UI 是 Retained UI：游戏代码先创建节点并修改属性，Runtime 在一帧内提交一次布局；绘制和命中都读取
同一份已提交快照，不在 `updateUI()` 回调里直接调用 bgfx。当前主窗口的顺序是：

```text
IGameState::onEnter / updateUI
  -> UIRootOwner + UITreeUpdater 修改节点树
  -> UIContext::commitLayout(logical extent)
  -> Measure / Arrange
  -> committed layout + hit + paint + semantics snapshots
  -> UICommittedPaintView
  -> tina_ui_render_integration::buildUIDisplayList
  -> logical pixels 映射到 framebuffer pixels、裁剪、相邻 batch 合并
  -> UIDisplayList SolidQuad / Glyph / ImageQuad commands
  -> bgfx transient vertex/index buffer
  -> UI textured shader + scissor + premultiplied alpha
  -> RenderDevice::submitFrame 后显示
```

`UIContext::buildCommittedPaint()` 按 paint order 遍历可见节点。普通 `UIBoxPaint` 生成矩形 entry；圆角且
同时有 fill/border 时以外层统一 border + inset fill 两条 entry 表达，shadow 继承外层半径。Canvas
`SolidRect`/`Image` 从 Element local 坐标转换到 world，并在 box chrome 后按 descriptor 命令顺序追加；
NineSlice 先精确计算有效 source/destination patch，再按 row-major 展开为1..9个 Image entry；
文字生成 Glyph entry；ProgressBar 追加按 value 缩短的 foreground，RadioButton 追加 indicator 和
选中内块，TextEdit 在焦点状态下追加 selection highlight、IME preedit 和 caret。Integration 再把
逻辑坐标投影为像素矩形，并丢弃空/透明/完全在 clip 外的 entry。

Solid 和 Glyph 共用一套带 UV 的 UI shader：SolidQuad 绑定 1×1 白色 R8 纹理，采样值恒为 1；Glyph
绑定 UIContext 持有的 R8 atlas，采样灰度作为 coverage。圆角 SolidQuad 额外由每顶点携带的像素
width/height/radius 计算 SDF coverage，保持相邻 batch 无 per-command uniform。片元颜色是顶点
premultiplied 颜色乘 coverage，
backend 对每个 clip batch 设置 bgfx scissor，并使用 `ONE, INV_SRC_ALPHA` 混合。UI 模块本身不依赖
bgfx；这条依赖只存在于 `tina_ui_render_integration` 和私有 bgfx backend。

Solid/Glyph shader 继续只读取纹理 `.r` coverage；RGBA Image/Icon 使用独立 `ImageQuad` shader
mode/program。相邻 image batch key 包含 packet-local Texture2D ref、clip、sampling 和 shader mode；sampled
straight-alpha RGBA 在 shader 中 premultiply 后再应用 committed tint，继续遵守 `ONE, INV_SRC_ALPHA`
混合。NineSlice 在 DisplayList 前展开为同一种 ImageQuad，不建立专用 backend primitive；committed patch
显式保存共享 logical right/bottom cut，fractional-DPI 投影不从 float width 反推端点，避免相邻 quad 出现
缝隙或重叠。

## 控件绘制矩阵

| 控件 | 语义/交互 | 当前实际绘制 |
| --- | --- | --- |
| `Root` | 树和所有权边界 | 默认不绘制；设置 `UIBoxPaint` 后也可作为背景 SolidQuad |
| `Panel` | 容器和布局 | `UIBoxPaint` 的 SolidQuad；当前 effective clip 是 viewport 与自身矩形的交集 |
| `Modal` | committed Focus/Input scope、下层输入 barrier、Dialog semantics | Theme surface chrome；布局/内容由 retained 子树组合 |
| `Label` | 只读 UTF-8 文本 | Glyph quads；没有可用字体时为确定性的 placeholder SolidQuad |
| `Button` | Pointer、Tab、Enter/Space/KeypadEnter、Gamepad South | `UIBoxPaint` 背景 + 可选 `UIButtonPaint` 状态色 + 文本 |
| `Checkbox` | checked 切换，复用 Button action/焦点路径 | 背景 SolidQuad + `UICheckboxPaint` 勾选指示块；标签由相邻 Label 组合 |
| `Slider` | Pointer 横向拖动、Tab/空间导航/显式焦点，min/max/value/step | 背景 track + `UISliderPaint` filled track/thumb；状态优先级为 drag > focus > normal |
| `TextEdit` | 单行编辑、选择、光标、IME | `UIBoxPaint` 背景 + `UITextEditPaint` hover/press/focus/disabled、selection highlight 与 caret + 文本 Glyph/placeholder |
| `ProgressBar` | 非交互 determinate range/value | track SolidQuad + 按比例缩短的 foreground SolidQuad |
| `RadioButton` | 同直接父节点互斥选择 | indicator SolidQuad + 选中内块 + 文本 Glyph |
| `ScrollView` | wheel/thumb drag 与 viewport clip | 内容沿 offset 平移并裁剪，追加 track/thumb SolidQuad |
| `Dropdown` | ComboBox value、Pointer/Keyboard/Gamepad 开关 | Button chrome + 文本 + 下拉指示块 |
| `Popup` | 独立 List/focus scope、anchor flip/clamp、输入 barrier | 顶层 overlay surface chrome，始终晚于普通树绘制 |
| `DropdownItem` | ListItem selection 与焦点 | Button chrome + 选中背景 + 文本 |
| `ListView` | 虚拟化 List/ListItem、键盘/手柄选择与滚动 | 固定 row pool + 选中/hover chrome + scrollbar |
| `TreeView` | 虚拟化 Tree/TreeItem、层级展开/折叠 | 固定 row pool + disclosure/indent + 选中 chrome + scrollbar |

控件创建入口集中为 `UIRootBuilder`/`UITreeUpdater::createElement(descriptor)`；属性 setter 只修改
retained 状态并标记必要的 dirty 类别。

**产品 Theme（默认皮肤 + 全局换肤 + 局部覆盖）：**

- `UIContext` 持有 `productTheme()`，默认 `makeDefaultProductTheme()`；
- `createElement(..., make*Element(...))` 按 descriptor 的 `UIStyleRoleId` 创建 Button/Checkbox/Slider/TextEdit/ProgressBar/RadioButton/
  ScrollView/Dropdown/Popup/DropdownItem/ListView/TreeView 与 Label 文本样式在创建时 **自动 apply** 对应
  `make*Chrome` / text style；Root/Panel 默认无底色（容器），需背景时用
  `makePanelBoxPaint` / `makeSettingsPanelChrome`；
- `setProductTheme(theme)` 会校验 metric，并事务式重绑所有仍继承产品 Theme 的既有控件属性；容量、
  文本测量或线程校验失败时，Theme 与控件属性均保持不变；之后新建的节点继承最新 Theme；
- 局部覆盖按属性分离：`setBoxPaint` / `set*Paint` / `setTextStyle` 只让对应属性脱离后续全局换肤，
  同一控件上未覆盖的其他属性仍会跟随 Theme；即使 setter 写入当前相同值，也视为显式局部覆盖；
  `clearOverride(mask)` 从当前 StyleRole 和当前 Theme 恢复选定属性；
- `setStyleRole()` 原子切换 recipe，保留显式 local override；ButtonPrimary/ButtonDanger、四级 Text、
  Panel/Modal/Popup surface 与全部现有控件 role 均有 recipe；
- Runtime 游戏通过 phase-scoped `PrimaryWindowUITreeUpdater::productTheme()` / `setProductTheme()` 换肤，
  不取得裸 `UIContext`；
- 默认 panel/button 分别使用 6px/4px 统一圆角；圆角 border 使用单一外层 ring，pressed 状态收拢阴影
  并通过 light/dark 交换改变 ring 色，focus 使用独立边框色，因此 hover / pressed / focused / disabled
  具有可辨识层次；
- 另提供 `makeLightProductTheme()` 与完整 chrome 工厂（`makeButtonChrome` 等）。

`UIBoxPaint` 仍是 escape hatch，并可携带 borderLight/borderDark/borderWidth、shadow（假 elevation）与
统一 `cornerRadius`。Image/Icon/NineSlice 基础绘制、产品采用、失效/尺寸矩阵与性能 workload 已关闭。
逐角半径、圆角子树 clip、毛玻璃与完整 CSS 仍未实现；
ColorToken startup registry/value 与运行期 reverse-dependency update、literal/token-backed BoxFill rule、node-local state
和 Runtime 入口已经可用。

## 产品接入与证据

`tina_sample_ui_showcase` 是控件与换肤的独立工作台，固定 1280×980 logical extent，同屏展示20个控件：

- Primary、destructive、disabled 与 reset Button；
- Checkbox、Slider→ProgressBar 联动、UTF-8 TextEdit；
- Performance/Balanced/Quality 与 Dark/Light 两组 RadioButton；
- Dropdown、虚拟化 ListView/TreeView 与 ScrollView；
- Panel elevation、圆角/边框/阴影、状态栏与主题色板。

Showcase 的普通页面树使用 Flow/Flex：`Root -> Background -> Header/Main`，`Main -> Navigation/Cards`，
Cards 再按三行双列组织；控件均挂在对应 Card/Row/Column 下。只有 Dropdown Popup 使用 Overlay，普通
页面不再依赖固定绝对 offset 排版。

它使用默认 product chrome 呈现 hover/pressed/focused/disabled 层次，并通过
`setProductTheme()` 在既有 retained tree 上事务切换 Dark/Light。`--auto-demo` 会执行
Dark→Light→Dark（或相反）、Slider→ProgressBar、Dropdown/List/Tree selection、Tree expansion 与滚动联动，
并在退出 JSON 中验证 `controls=20`、`treeExpansionChanges=2`、`listSelectionKey=1007`、
`treeSelectionKey=4`、`dropdownSelection=1`、`scrollOffset=80` 和 root 生命周期。完整文字视觉验收必须使用
bgfx + FreeType preset；普通 bgfx preset 的 placeholder text 只用于确定性降级和生命周期 smoke。
最新 Dark/Light FreeType client capture 分别位于
`artifacts/screenshots/ui-showcase-dark/20260728-155526/frame-03.png` 和
`artifacts/screenshots/ui-showcase-light/20260728-155914/frame-03.png`；两张 1280×980 画面完整呈现
20个控件、集合区、滚动区及主题差异，没有裁剪或重叠。

2026-07-31 的 `RunUiStateFeedbackVisualGate.ps1` 通过 Windows MSVC/bgfx/FreeType 产品路径驱动真实
Win32 pointer route，对 Dark/Light 的 normal、hover、focus、pressed/drag、selected 与 disabled 状态执行
22项差分检查并全部通过。可复现环境、可执行文件与截图哈希记录在
[UI-STATE-FEEDBACK Windows Evidence](ui-state-feedback-evidence-windows.md)；生成截图已按约定回收。

`tina_sample_2d` 当前 UI 包含：

- HUD Label/Theme Button；
- Master/Music/SFX Checkbox/Slider；
- profile-name 单行 TextEdit；
- 65% ProgressBar；
- Windowed/Fullscreen RadioButton 组；
- Scene Explorer TreeView：13个 logical item、12个 materialized row slot、stable-key selection。

标准控件保留 create-time Theme 绑定；Theme Button callback 只记录 pending intent，`updateUI()` 再事务调用
`setProductTheme()`。Panel 与标题文字是有意的局部层级覆盖，每次换肤集中重算。`--ui-theme-demo`
在300帧产品门禁中执行 Dark→Light→Dark；`--ui-tree-demo` 同时选择 gameplay 与最终
`crate_spawn #102`。schema 14 验证 `uiTreeViewsCreated=1`、13/12 logical/materialized、两次 selection、
最终 stable key `402`/index `12`、滚动、Theme paint 及 Tree/TreeItem selected semantics。
2026-07-28 的完整门禁报告 `artifacts/reports/product-2d-treeview-gate.json` 记录全部步骤与
300帧 sample exit 0。动态 glyph atlas 修复后的 Dark/Light FreeType client capture 分别位于
`artifacts/screenshots/2d-scene-explorer-freetype-dark-fixed/20260729-001845/frame-03.png` 和
`artifacts/screenshots/2d-scene-explorer-freetype-light-fixed/20260729-002407/frame-03.png`；对应 report
均为 `ok=true`、exit 0。Scene Explorer、选中行、设置控件与 playfield 无明显裁剪或重叠，
`gameplay #30` 与 `Switch to dark/light` 的运行时新增字符完整可见。2026-07-29 的 UI-003 compare
矩阵报告 `artifacts/screenshots/ui-003-size-matrix/20260729-004341/matrix-report.json` 另证明五个逻辑尺寸
全部 `ok=true` 且 `baselineCompare.matched=true`。

作为 TreeView 接入前的历史 UI-001 证据，2026-07-23 的
`artifacts/screenshots/sample-2d-product/20260723-013100/report.json` 记录 `ok=true`、exit 0、schema 3，
3次 960x540 client capture 中2帧稳定非空；首次 `PrintWindow` 白帧由 `blankLike=true` 排除。
人工复核 `frame-02.png` / `frame-03.png` 中 TextEdit、ProgressBar 与 RadioButton 可见、中文正常且
无裁剪或重叠；两帧 65% fill
均为 x=700..842（143 px），选中色只出现在 Windowed RadioButton，client capture 未混入标题栏。

`tina_sample_3d` 的 `Product3DUI` 使用同一产品 Theme 契约提供 Theme Button、Auto Rotate Checkbox、
Rotation Speed Slider、Frame ProgressBar、Asset ListView、Scene TreeView 与标题/Inspector/状态层级。
Checkbox 与 Slider 控制实际模型旋转；callback 只提交 intent，`updateUI()` 统一处理控件状态、ProgressBar
与 `setProductTheme()`。Product3D 的 1280×720 仅是 reference layout：right rail 使用 End 对齐，
collection/list/tree 与 footer 使用 Stretch/End 保持边距并扩展；字体和控件仍以 logical pixel authoring，
不会随 client 尺寸做全局 zoom。当前 product-3d schema 14 的自动门禁要求 Dark→Light→Dark、2次 collection step、7 Panel、
13 Label、ListView/TreeView 各1个、Tree expansion changes `2`、最终 stable keys `2003/4`、进度100%
与 root 释放，并继续在同一300帧产品门禁中验证3D Scene lighting。动态 glyph atlas 修复后的 FreeType 暗/亮截图分别在
`artifacts/screenshots/3d-product-ui-freetype-dark-fixed/20260729-003922/frame-02.png` 和
`artifacts/screenshots/3d-product-ui-freetype-light-fixed/20260729-004012/frame-01.png`；实际双 mesh、
集合控件、主题层级及动态 `10%`/`1%` 进度均清晰可见，没有裁剪或重叠。

bgfx glyph atlas 必须保持可变：向 `createTexture2D` 传入 initial memory 会创建 immutable texture，
导致运行时新 glyph 的 `updateTexture2D` 被拒绝。当前实现以 `nullptr` 创建 R8 atlas，并让首次和后续上传
统一走 update 路径；首次上传失败会立即销毁 texture。`tina_render_bgfx_tests` 中的生产源码合同测试覆盖
同一 handle 的首次/后续更新、immutable reject 为零及失败回收。

当前完整门禁直接运行 `tina_ui_tests`、`tina_runtime_ui_tests`、`tina_ui_uia_tests`、
`tina_ui_render_integration_tests` 与 product-2d 图的 `tina_ui_freetype_tests`；具体数量以本轮
GoogleTest 输出为准。UI 容量回归
覆盖 Checkbox/Slider mutation、TextEdit pointer selection 和需要同时重绘旧/新节点的 focus step；
dirty queue 容量不足时状态与 callback 原子不变，同文本替换 selection 仍发布新 paint；文本 padding
回归覆盖 auto-size、多行回行和可变 glyph advance 的 TextEdit pointer selection。数字是当前工作树
证据，不是架构永久基线。

## 验证

```powershell
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_ui_render_integration_tests.exe --gtest_color=yes
```

完整 showcase：

`tina_sample_ui_showcase` 的多列控件布局直接展示该能力：Pointer 或 Tab 建立焦点后，Arrow/D-pad 可在
Button、Checkbox、RadioButton、Dropdown、ListView 与 TreeView 等控件本体之间按屏幕几何移动，焦点
边框继续使用当前 Dark/Light Theme；进入复合控件后由控件自身接管方向命令。

```powershell
cmake --preset windows-msvc-vnext-bgfx-ui-freetype
cmake --build --preset windows-vnext-bgfx-ui-freetype-debug `
  --target tina_sample_ui_showcase tina_ui_tests tina_runtime_ui_tests `
           tina_ui_render_integration_tests tina_ui_freetype_tests --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_sample_ui_showcase.exe `
  --frames=150 --frame-delay-ms=0 --theme=dark --auto-demo
```

完整 product-3d UI/资源同轮门禁：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunProduct3dGate.ps1 `
  -OutJson artifacts\gates\product-3d.json
```

FreeType、bgfx 和 product-2d 需要对应 feature 图；完整命令见 [构建说明](building.md)与
[测试说明](testing.md)。

## UI Flow

`UI-FLOW-001` 复用唯一 retained tree。`UIFlowLayerId` 与 `UIFlowScreenId` 只是现有 `UINodeId` 的强类型
身份：Layer 必须是 updater root 的直接子节点，Screen 必须是 Layer 的直接子节点。Screen 注册后默认
inactive；`pushFlowScreen()`、`popFlowScreen()`、`replaceFlowScreen()` 只让每个 Layer 的栈顶 Screen 参与
layout/hit/paint/semantics publication，其余 Screen 视为 `Collapsed`，但不改写作者的 `UILayoutStyle`。

Layer/Screen 容量由 `UIContextCapacityConfig::flowLayerCapacity/flowScreenCapacity` 在 Context 创建时固定；
运行期切换先预检 dirty queue，失败时栈和 committed snapshot 均不变。节点销毁会解链并回收注册容量，
失活 Screen 内的 committed focus 会在 layout commit 时清理。Runtime 的
`PrimaryWindowUITreeUpdater` 暴露相同 phase-scoped API。

Pause Action Router 冻结 `UIFlowAction::Back/Confirm/Menu` 三个产品 action：每个注册项拥有一个 48-byte
fixed-inline callback，注册总量受 `flowScreenCapacity` 限制，无 heap fallback。Runtime 先让打开的 Dropdown
处理 Back dismiss；Escape / Gamepad East 路由 Back。Enter / Keypad Enter / Gamepad South 先交给已聚焦控件的
默认 Activate，未被消费时再将 Confirm 路由给 committed layout 中最上层 active Screen。TextEdit 聚焦时可打印
P Down 优先进入文本，其他 P / Gamepad Start 路由 Menu。callback 只记录
intent。被处理 Down 的精确 control 会锁存，匹配 Up 在 Screen pop/destroy 后仍消费，避免同一数字 transition
穿透 gameplay；重复 Down 不重复 callback。无 active Screen 或未注册 callback 时不消费。路由稳态无分配，
每次 action 最多反向扫描一次 bounded committed layout。

多本地用户继续复用同一窗口 UI 树，而不是建立 per-user Screen 栈或 focus。`UIFlowLocalUserId` 的有效值固定为
`1..16`，`UIFlowPrimaryLocalUser=1`；Keyboard、Pointer、Text/IME 设备观察固定属于 Primary。Gamepad 使用完整
generation `GamepadId` 作为 assignment identity，可由 `UIContext`、`UITreeUpdater` 或 Runtime phase facade 的
`assignFlowGamepad()` / `clearFlowGamepadAssignment()` 管理；未分配 Gamepad 由
`flowLocalUserForGamepad()` 解析为 Primary。`UIFlowActionEvent::localUser` 报告实际路由用户。

`flowInputDeviceState(localUser)` 按用户发布 `KeyboardMouse/Gamepad`、active Gamepad、Platform frame/sequence 与
revision。Gamepad button Down 只更新其当前用户，release 和 axis drift 不切换。重分配/清除会让引用该 Gamepad
的旧用户提示回落键鼠并递增 revision，但不会清除 Flow physical-control latch，因此匹配 Up 在 assignment 改变后
仍成对消费；断连会清除该 Gamepad assignment/default-action/Flow latch，完整 stream reset 清除全部 assignment
与 latch，并回落所有 Gamepad 状态。普通查询、观察和 assignment 为 O(1)，reset 最多扫描固定 16 个用户槽，
稳态无分配。

首个产品 consumer 是 2D Pause State：基础页面与暂停 Modal 分属两个 Screen，当前读取 Primary 用户提示，标签在
`ESC / ENTER / P TO RESUME` / `B / A / START TO RESUME` 间按 revision 更新；Base Menu intent 在合法
State phase push Pause，Pause Back/Confirm/Menu intent 在 `updateUI` pop Screen，下一帧再 pop GameState。
Back/Confirm/Menu 之外的任意 action-id 仍属于独立后续扩展。

## 相关后续任务（状态以 Backlog 为准）

| ID | 范围 |
| --- | --- |
| `UI-002` | Windows UIA：tip 跨进程 gate 证据已固化（2026-08-03）；待 Narrator/Inspect 人工金标 |
| `UI-003` | 跨 DPI/GPU 容差视觉门禁（映射单测 + 单机 ROI/baseline + content-scale-like 逻辑尺寸矩阵 + sample contentScale JSON + 字体 identity fingerprint 已有；OS 级 100/150/200% DPI 真机矩阵与跨 GPU 像素金标后置） |
| `TEXT-001` | 多行 TextEdit、grapheme/shaping、候选窗定位 |
| `UI-PERF-001` | Done；clean 4096-node、单节点 paint dirty、route、100k 虚拟集合、`ui_image_nineslice_v1`、完整 `ui_component_build_v1`、`ui_style_state_v1` 与 `ui_motion_v1` 已落地；固定机前时间结论只报 provisional |
| `UI-COMPONENT-001` | Done；Runtime phase-scoped bounded transaction、六类 fixed-capacity Behavior side store、node/text/canvas/各 Behavior pool 统一 reservation/counter 与 `ui_component_build_v1` 已落地 |
| `UI-STYLE-001` | Done；强类型 StyleClass/ColorToken、startup registry/value、运行期 reverse-dependency token getter/setter、node-local pseudo-state selector、literal/token-backed BoxFill/imageTint rule、预编译 stylesheet、Runtime facade、固定 workload 与 Integration/Visual 门禁已落地；不做完整 CSS |
| `UI-MOTION-001` | Done；fixed-capacity paint-only transition、monotonic clock、retarget、reduced-motion、Style BackgroundColor persistent reservation/activation 与 `ui_motion_v1` |
| `UI-PAINT-002` | 逐角半径、圆角子树 clip 与 backdrop；已完成的统一 RoundedRect 不再重复列为缺口 |
| `UI-FLOW-001` | Done：固定容量 Activatable Screen/Layer Stack、Back/Confirm/Menu Action Router、16 槽本地用户、完整 generation Gamepad assignment、per-user 设备 revision、断连/reset 清理与 2D 产品接入已落地 |
| `UI-BEHAVIOR-SPI-001` | Deferred：只有标准 Behavior + routed listener 存在有证据的表达缺口时才评估 startup-only 高级 SPI |
| `UI-002-LINUX` | Linux AT-SPI adapter 与真实辅助技术验收（Deferred，不阻塞 Windows UI-002） |
| `SDK-001` | GameSDK、PlatformGlfw、DesktopBootstrap/RenderBgfx、UIFreetype、AudioMiniaudio 安装、moved-prefix 与 Ubuntu producer → Debian consumer gate 已落地；待正式 ABI/兼容策略；新增公共 UI 切片后同步扩展 consumer 覆盖 |

ProgressBar/RadioButton 的产品接入 `UI-001` 已完成，不应重新列为 Planned。
Theme A/B（token、panel 边、Low 假影、sample 改 token）已在产品 sample 路径落地；UI-002-SPI 与
可选 `tina_ui_uia` 属性、fragment、control pattern 与 action 切片已落地；UI-004 的 Focus Scope/Modal/Pointer Capture 与 UI-005 的
ScrollView、Dropdown/Popup、虚拟 ListView/TreeView 已完成。外部 Narrator 真机门禁与 UI-003
跨 GPU/DPI 金标仍不能标成 Done。ADR 0022 的 Element composition 主体已完成；Image/Icon、
Component/Behavior、StyleClass/pseudo-state、ColorToken 运行期更新、stylesheet imageTint、产品视觉门禁与
paint-only Motion 均已汇合。完整 keyframe timeline/layout animation 与更广 Style 属性面仍是独立后续项。不再重复列已删除的
`UIWidgetKind` 迁移，也不把尚未实现的目标 API 写成当前能力。
