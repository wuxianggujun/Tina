# Retained UI

当前产品 UI 位于 `include/tina/ui` 与 `src/ui`。旧 Legacy UI 产品图已删除，但这两个目录是 vNext
正式实现，不得作为 Legacy 残留移除。架构决策见 [ADR 0011](adr/0011-retained-ui.md)、
[ADR 0021](adr/0021-runtime-ui-startup-capability.md)和
[ADR 0022](adr/0022-ui-element-authoring-and-layout.md)和
[ADR 0028](adr/0028-ui-fixed-capacity-grid-layout.md)以及
[ADR 0029](adr/0029-ui-layout-debugger.md)。当前实现与下一阶段框架演进的分界见
[UI 框架设计](ui-framework.md)和 Accepted [ADR 0023](adr/0023-ui-extensibility-style-paint-motion.md)。

## 当前能力

| 领域 | 已实现 |
| --- | --- |
| 所有权 | per-window `UIContext`、generation/owner-aware `UINodeId`、move-only `UIRootOwner` |
| Authoring | `UIElementDescriptor` + `make*Element` recipes、组合 Semantics、StyleRole/override reset、固定预算 build transaction；Surface/Divider/Badge/Switch 是强类型第一方 profile；IconButton/FormField/Dialog 是固定预算多节点 composition profile；旧 create-by-kind 与公开 `UIWidgetKind` 已删除 |
| Tree | Root/Panel/Modal/Label/Button/Checkbox/Slider/ProgressBar/RadioButton/TextEdit/ScrollView/Dropdown/Popup/Tooltip/Menu/MenuItem/DropdownItem/ListView/TreeView/VirtualGridView/DataGrid/SplitView/Splitter/TabView/Tab，固定容量 mutation |
| Layout/Content | Flex/Grid container 与 item 分离、Flex 子项 `NoWrap/Wrap`、固定8x8 `Px/Auto/Fr` Grid track、基于直接父 content width 的 bounded responsive rules、Flow/Overlay placement、logical pixel、可选 axis-aligned border-box descendant clip、committed content placement、Tooltip/Menu 基于上一份成功 Anchor geometry 的 flip/clamp、SplitView 与 TabView committed geometry、事务 commit、clean-subtree reuse |
| Hit/route | committed hit snapshot、Capture→Target→Bubble、持久 Pointer Capture、Modal/Popup/Menu transient barrier、Tooltip Ignore/click-through、listener token、consume/prevent/claim |
| Paint | `UIBoxPaint` Rectangle/Ellipse/Line、box/text/control paint、第一类 Image/Icon content、固定容量 backend-neutral `SolidRect`/`SolidEllipse`/`SolidLine`/`Image`/`NineSlice` Canvas、axis-aligned clip、PaintCache、committed paint snapshot；NineSlice 在 commit 时原子展开为1..9个 Image entry |
| Theme/Style（A/B/C1 + UI-STYLE-001 slice） | `UITheme` token + `UIStyleRoleId` recipe + 属性 override mask/reset；默认 Button 为 Tonal，显式提供 Primary/Danger/Outlined/Text，并以 RadioButton/Checkbox 状态机分别提供 SegmentedButton/Switch；Surface、Divider 与 Badge 复用 Box/Text chrome；强类型 StyleClass/ColorToken、node-local pseudo-state、literal/token-backed BoxFill stylesheet 与运行期 ColorToken getter/setter 已落地；token 更新经固定 reverse-dependency 链为 `O(affected links)`，**无**圆角子树 clip/毛玻璃/完整 CSS |
| Motion | direct/Style transition 仍为 fixed-capacity paint-only；typed keyframe timeline 已支持 paint 属性及 bounded `LayoutWidth`/`LayoutHeight`/`LayoutOffset` 白名单，并沿唯一 commit pipeline 原子发布；`ui_motion_v1`、`ui_motion_timeline_v1` 与 `ui_motion_layout_v1` 均已有确定性 gate，墙钟结论保持 provisional |
| Text | strict UTF-8、普通 Label 按最终 content width 自动换行、可选 FreeType rasterizer、R8 Glyph atlas、DisplayList Glyph |
| Input | Focus Scope/显式 focus、Pointer capture/cancel、Tab 与 committed 几何空间焦点、Keyboard/Gamepad activation、Menu/Dropdown/List/Tree/VirtualGrid/DataGrid/TabView navigation、TextEdit edit/selection/IME、Tooltip PointerHover/KeyboardFocus/Manual 与 monotonic delay、Splitter Pointer drag/RangeInput keyboard；UI Flow 固定 16 槽本地用户、Gamepad assignment 与 per-user 设备 revision |
| Semantics | Automatic/Publish/MergeDescendants/Exclude、显式 role/name/description/actions、Tooltip 文本作为 Anchor description/HelpText fallback、Menu/MenuItem 与 checked state、Splitter Slider range/value、TabList/Tab/TabPanel selected state，以及 List/Tree/VirtualGrid/DataGrid materialized item 的 stable row 元数据与 selected/focused state |
| Runtime | startup root builder、phase-scoped tree updater、Tooltip/Menu/SplitView/TabView facade 与 bounded component transaction（含 List/Tree/VirtualGrid/DataGrid 的 DataSource/metrics/selection/scroll，及 Tree expansion）、DisplayList/Glyph atlas handoff |
| Performance | `tina_bench` 已接入 static commit、paint-only dirty、route/capture、100k virtual collection、Image/NineSlice、完整 Component build、Style 与 direct/timeline Motion schema-v1 provisional workload；VirtualGrid/DataGrid 的 100k logical item/row 由固定 materialized pool 单测约束 |
| Product | 独立 20 控件 showcase、product-2d Scene Explorer TreeView、product-3d Asset ListView/Scene TreeView 与 Dark/Light Windows 视觉证据 |

## 所有权与句柄

`UIContext::Create(window, capacities, resource)` 在创建时固定 node/root/listener/layout/paint/text/semantics
等 storage 容量。`layoutDebuggerSnapshotCapacity` 默认为0，此时不保留诊断双缓冲，也不在 layout commit
构建诊断快照；设置为非零值后容量不得超过 node capacity。该能力始终编译进产品，不依赖 Debug 构建。
`paintSnapshotCapacity` 为0时从 node 数派生，非0时可独立提高到8,388,608，以容纳
单节点的 glyph/control/Canvas/NineSlice 多 entry 展开；Semantics entry/scratch 仍严格按 node 数分配。
`UINodeId` 同时校验语义 owner WindowId、registry owner、slot 与 generation；stale、
cross-context、cross-window ID 必须失败。

`UIRootOwner` 是 root 的唯一 RAII owner。销毁 root 会使其子树 ID 失效；listener token 不保活 Context/
root。产品 State 在退出时应先 reset listener，再 reset root。Context 只能在 owner thread mutation/commit。

### 私有实现职责边界

公开 `UIContext` 只作为每窗口唯一的组合根和生命周期边界：它负责 `Create()`、owner Window、节点归属检查、
统计，以及七个显式 capability accessor；不再直接暴露节点创建/修改、Theme、Motion、Text、提交或输入 API，
也不保留旧成员函数的 compatibility alias。调用方按职责包含对应公开头：

| Capability | 公开入口 | 职责 |
| --- | --- | --- |
| Authoring | `context.authoring()` | 只创建 `UIRootBuilder` 和 root-scoped `UITreeUpdater`；节点 mutation、组件 transaction 与控件 retained state 都由 updater 承担 |
| Style | `context.style()` | 产品 Theme、StyleClass、ColorToken 与 stylesheet registry |
| Motion | `context.motion()` | 可注入时钟、reduced-motion、direct transition 与 timeline |
| Text | `context.text()` | 字体注入、IME 状态与 composition/input/edit-command routing |
| Publication | `context.publication()` | structure/layout transaction、committed views、caret rect 与 Glyph atlas publication |
| Layout Debugger | `context.layoutDebugger()` | owner-thread layout 诊断快照及 overlay 选择/排除状态；不触发布局 mutation 或 commit |
| Input | `context.input()` | Pointer/Focus/Flow/控件命令/accessibility action routing 及窗口级输入状态 |

这些 capability 都是按值返回的 non-owning owner-thread view，不得晚于所属 `UIContext` 使用；
`UIRootBuilder` 同样只借用 Context，`UITreeUpdater` 还绑定创建时的 root identity，并在每次操作校验
root/child generation。某类 committed view 在下一次该类成功 publication 或 Context 析构时失效；失败提交继续保留
最后一份成功 snapshot，调用方仍应在后续成功提交后重新取得 view。Runtime phase facade 另有更短的 epoch 生命周期。
直接消费者应显式包含 `UIAuthoring.hpp`、`UIStyleController.hpp`、`UIMotionController.hpp`、
`UITextSystem.hpp`、`UIPublicationPipeline.hpp`、`UILayoutDebugger.hpp` 或 `UIInputRouter.hpp`；`UIContext.hpp` 只 forward declare
capability，`UI.hpp` 才是有意提供完整表面的 umbrella header。

`UILayoutDebugSnapshotView` 是下一次成功 layout publication 前有效的 owner-thread borrowed view，包含每个
committed node 的 authored/resolved style、父/当前 content basis、测量与 min/max-content 尺寸、local/world rect、
effective clip/content placement、visibility、hit policy、behavior、style role 和稳定的公共元素类型。layout commit
任一步失败时继续保留上一份成功快照。`UILayoutDebugOptions` 只控制 overlay 的 enabled、全节点边界、选择节点和
调试器自身排除子树；启用时非空 node 必须属于同一 Context 且仍存活，禁用状态允许保留 stale handle 以便调用方
先清空或重配。启用但 Context 未配置诊断容量会返回 `CapacityExceeded`。

这仍是同一套 UI ABI 和同一个固定容量事务 owner，不新增第二套 UI。大体量私有实现按 Tree、Layout、Paint、
Semantics、Input、Controls、Collections、Overlays、Style、Motion 与 Text 等职责拆到独立 translation unit；
共享 `UIContext::Impl` 保留完成原子 publication/rollback 所需的集中状态。`UITextStorage` 已独立拥有固定容量
UTF-8 arena、空闲块复用/合并、bump 回收与 used/high-water 统计；`UIImeCompositionState` 独立拥有
固定 preedit buffer、active/cursor 状态、容量契约和可复制的事务快照；
`UIButtonActionRegistry` 与 `UISliderChangeCallbackRegistry` 分别独立拥有 Button/Slider callback 的
固定容量 slot、stage/commit/rollback、generation-aware capture/invoke 与调用期间延迟回收；Button
registry 还封装 route clear barrier、registration serial 与 high-water 统计。
`UIVirtualGridViewStateStorage` 使用独立 bounded sparse pool 持有响应式网格、materialized item、selection、
layout scratch 与 committed metrics；`UIDataGridStateStorage` 对 Grid/column/materialized row/cell 使用四个独立
bounded sparse pool，二维 selection、双轴 metrics 与 layout scratch 跟随 Grid state，列数与可见行数都必须
落在创建时组件容量内。二者的 link/scratch reserve 也按对应组件池定容，不再为每种 state 分配完整 node-index
数组。`UISplitViewStateStorage` 以独立 bounded pool 持有 SplitView/Splitter relationship、requested fraction、layout
scratch 与 committed metrics；`UISplitViewLayout` 解析 orientation/minimum/clamp，`UISplitViewInput` 只提供
splitter pointer fraction/grab 计算。`UITabViewStateStorage` 固定持有 TabView/Tab/Panel relationship、active Tab、
专属 `UITabPaint` 与 committed metrics；`UITabViewLayout` 和 `UITabViewInput` 分别收口四向 regions 与命令校验。
`UIMenuStateStorage` 固定持有 Menu/Item/Anchor 双向关系、checked state、active Menu 与 committed metrics；
`UIMenuLayout` 和 `UIMenuInput` 分别收口 placement/flip/clamp 与命令校验。
`UIGridLayout` 使用局部固定数组与64-bit occupancy 完成普通 Element 子树的 intrinsic measurement、row-major
auto placement、span demand、`Px/Auto/Fr` track resolution 和 item alignment；它不持有 Context side state，
也不替代带 DataSource/materialized pool 的 VirtualGridView/DataGrid。
`UIContext::Impl` 统一执行
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

`PrimaryWindowUITreeUpdater` 通过统一 `createElement()` 创建 ListView/TreeView/VirtualGridView/DataGrid，并暴露其 DataSource、
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

- `containerLayout=Flex` 时，`flexContainer`（direction/wrap/justifyContent/alignItems/alignContent/gap）由父节点解释，
  `flexItem`（grow/shrink/basis/alignSelf）由该父容器为当前子节点解释；
  `wrap=Wrap` 按最终主轴 content extent 分行，每行独立计算 grow/shrink/justify，cross-axis 行尺寸和 row/column
  gap 参与容器 auto-size；`alignContent` 再把完整行集合按 Start/Center/End/SpaceBetween/Stretch 分布到最终
  cross-axis content extent，`NoWrap` 不消费该属性。最终约束在 Arrange 才确定时，提交使用最多八轮 bounded
  convergence：每轮分别记录 `ResponsiveStyle`、`Visibility`、`FlexConstraint`、`TextMetrics` 反馈，并对字段级
  resolved state 生成双 `u64` 指纹；稳定时发布，no-progress、cycle 或 pass-budget-exhausted 时携带分类节点上下文
  fail closed，保留上一份 committed snapshot 与待处理 dirty state；
- `containerLayout=Grid` 时，`gridContainer` 提供每轴最多8条 `Px/Auto/Fr` track、row/column gap 和
  `justifyItems/alignItems`；`gridItem` 提供 zero-based row/column、span 与 self alignment。空 track list 按
  source-order row-major 自动放置生成隐式 `Auto` track；显式与隐式 track 共用8条容量，超限 fail closed。
  `Collapsed`/`Overlay` child 不占 cell；Arrange 前百分比 item size/min/max 以最终 grid area 为 basis 刷新；
- `placement=Flow` 参与当前父级选择的 Flex 或 Grid，`placement=Overlay` 使用 horizontal/vertical alignment、offset 与
  margin，不参与 Flow 尺寸和顺序；Popup、Tooltip 与 Menu recipe 强制 Overlay。Tooltip 使用显式 Anchor
  且永不形成 barrier；Menu 也使用独立 Anchor/placement contract，但拥有自己的 transient input barrier，
  不借用 Dropdown Popup 的 relationship 或 state；
- `clipDescendants=false` 是普通容器默认值；启用后，该节点成为 axis-aligned clip owner，普通 in-tree Flow
  与 Overlay 后代沿既有 `descendantClip` 链与 owner 的 world border-box 求交。后代 `worldRect`、布局和
  tree/semantics 顺序不变，committed `effectiveClip` 统一供 hit/paint 使用；viewport-level Popup 继续走专用
  anchor/clip policy；这不是 rounded clip，也不额外改变 owner 自身的 paint clip；
- tree 顺序同时决定 layout、paint、focus 与 semantics 顺序，没有 CSS `order` 双轨。paint 顺序在此之上
  再叠加一层 layer 提升：`Content < Modal < Popup < Tooltip`（见下）。hit、tab/方向 focus、semantics
  都从同一份 paint 顺序数组派生，因此三者始终与所见一致。
- `UILayoutLength::{MinContent,MaxContent}` 可用于 width/height、Flex basis 与 min/max constraint。文本叶子分别取
  最长不可断词宽和最长 authored line 宽；Image 取 intrinsic logical size；Flex/Grid 容器继续聚合后代、padding、
  gap、margin、显式 size 与 min/max constraint。无确定 basis 的 Percent 不参与 intrinsic contribution；wrapped
  Flex 的 min-content 按每项独占一行/列计算。Flex shrink 在没有显式 min constraint 时不会压过 min-content floor；
- `aspectRatio` 表达 width/height，仅当 width/height 中恰好一个轴为 `Auto` 时从另一轴派生，并在 min/max clamp
  之前应用；两个轴都显式或都为 Auto 时不覆盖既有尺寸意图；
- `responsiveRules` 是最多4条按顺序、互不重叠的半开区间 `[minParentWidth,maxParentWidth)`；匹配直接父节点
  最终 content width，可覆盖 Flex/Grid 容器类型、Flex direction、Grid rows/columns、visibility、当前激活容器的
  gap、节点 padding 与 min/max constraint。解析只写 layout scratch，不回写 authored style；无匹配区间继续使用
  base style。父容器最终 content width 驱动的动态切换是 bounded convergence 的核心输入，不会因周期诊断而降级成
  外部手工布局。Desktop Shell 的 Inspector/Timeline pane 与 splitter 已直接消费该契约，不再从 resize callback
  计算宽度档位或改写 fraction。

控件内部文字使用独立的 `UIContentAlignment`，不再借用父容器的 child alignment。layout 将
`contentBox/origin/intrinsicSize` 作为 `UICommittedContentPlacement` 随 snapshot 一次发布；文字绘制、
caret/selection 与 pointer-to-codepoint 读取同一份 committed 结果。

`UISemanticsDescriptor` 支持 Automatic/Publish/MergeDescendants/Exclude、显式 role/name/description/actions；
Merge 按 tree 顺序合并 eligible descendant name，Exclude 删除完整语义子树。`UIStyleRoleId` 与 behavior/
semantics 独立，属性 setter 只 detach 对应 theme binding，`clearOverride()` 从当前 role/theme 恢复；
Runtime phase facade 同样可切换/query role 和 reset override。

当前 `UISemanticsRole::Image` 已实现。`UIIconContent` 是 Icon 的强类型 authoring profile，包含
`UIImageSource`、tint、sampling 与 content alignment；`makeIconElement(UIIconContent, layout)` 固定使用
`UIImageFit::Contain`、居中默认值、`UIPointerHitPolicy::Ignore` 与 `UISemanticsMode::Exclude`。它继续消耗普通
Image content slot 并复用 ImageQuad/resolver/pin/Texture2D/DisplayList/GPU shader，不是第二套图片、Asset、atlas
或 GPU pipeline。Button/RadioButton 可直接拥有与 text 互斥的 Image intrinsic content，使 control chrome、交互状态和
图标共享同一 layout/paint 节点；这种 icon-only control 必须提供显式 semantics name，并关闭 content-as-name。
其他带行为 Element 仍拒绝 Image content。`makeImageElement()` 只为独立表达信息的
图片发布 Image role，并要求调用方显式给出 name，不能从 AssetId 或资源文件名推导可访问名称。

### 视觉组件 authoring profile

`UISurface.hpp`、`UIDivider.hpp` 与 `UIBadge.hpp` 提供无行为的强类型视觉 profile：
`makeSurfaceElement()` 选择 Plain/Filled/Elevated surface，`makeDividerElement()` 提供 Horizontal/Vertical、
Subtle/Strong/Accent 与有限非负 logical thickness，`makeBadgeElement()` 提供 Neutral/Accent/Danger 的只读紧凑
label。三者继续创建普通 Panel/Label Element，只复用 `UIStyleRoleId`、`UIBoxPaint`、`UITextStyle`、固定节点/text
storage 与现有 committed paint；不增加 Widget kind、side state、atlas 或 Render pipeline。Surface/Divider 默认
Ignore hit，Divider 排除 semantics；Badge 发布只读 Label semantics，并以其完整文本作为 accessible name。

`UISwitch.hpp` 的 `UISwitchConfig` 提供 Standard/Compact 尺寸与由 control root 发布的
`accessibleName`；`makeSwitchElement()` 默认创建 44x24 logical px 的 Standard switch。它解析为既有
Checkbox built-in，复用同一个 Toggle value、Activate callback、Focus、Pointer/Keyboard/Gamepad、capacity、
dirty transaction 与 UIA TogglePattern，只通过 `UIStyleRoleId::Switch`、
`UIToggleIndicatorPresentation::Switch` 和 committed track/thumb geometry 区分外观。Semantics 使用
`UISemanticsRole::Switch`，Windows UIA 映射为 CheckBox ControlType；没有第二套 Switch 状态机。

### 桌面多节点 composition profile

`UIIconButtonConfig`、`UIFormFieldConfig`、`UIDialogConfig`、`UISnackbarHostConfig` 及对应 `Parts` 返回值提供第一方桌面组合入口。
`requiredIconButtonBuildBudget()/requiredFormFieldBuildBudget()/requiredDialogBuildBudget()/requiredSnackbarHostBuildBudget()` 在 mutation 前计算精确
node/text/Behavior 预算，root-scoped `UITreeUpdater` 与 `PrimaryWindowUITreeUpdater` 均提供
`buildIconButton()/buildFormField()/buildDialog()/buildSnackbarHost()`。IconButton 复用 Button + Icon + 独立 Tooltip；FormField 复用
Label/TextEdit/Button/Tooltip；Dialog 复用 Modal/Panel/Label/Button；Snackbar 复用 Panel/Label/Button 与
`FloatingSurface` elevation。Snackbar 状态使用调用方 fixed-capacity inline queue 和显式 monotonic sample，消息发布
`Polite` live-region 且永不自动改变 Focus。它们不新增 Behavior store、Modal/Focus owner、
update loop、Semantics pipeline 或 Render pipeline，失败时由同一 build transaction 回滚完整组合树。

### Dialog presentation 契约

`buildDialog()` 只负责原子构建并注册 Dialog，返回时 presentation intent 固定为 closed；Dialog authored
`UILayoutStyle::visibility` 必须保持 `Visible`，`UIDialogStateStorage` 在 layout candidate 中把 closed intent 解析为
effective `Collapsed`。调用方使用 `openDialog()/dismissDialog()/isDialogOpen()` 管理 retained intent，不再通过
`setLayoutStyle()` 维护第二份显隐状态。open/dismiss 均幂等，query 立即返回 intent；真正的 Modal barrier、Hit、
Paint、Semantics、Focus 进入与 dismiss 后的 focus restore 只在下一次成功 `commitLayout()` 时一起发布。

一个 Window 同时最多有一个 registered Dialog 的 open intent。打开 Dialog 会在同一预检事务中关闭 Menu 并
hard-dismiss Tooltip；第二个 Dialog 冲突、非 Dialog、stale generation 与 root-scoped updater 的跨 root 访问均明确
失败。dirty queue 容量预检失败时，Dialog intent、active Modal、focus 与全部 committed snapshot 保持不变；
destroy、root release 和 generation slot reuse 会清理注册状态。root-scoped `UITreeUpdater` 与 phase-scoped
`PrimaryWindowUITreeUpdater` 都提供这三个 presentation API；Runtime facade 在 phase epoch 结束后统一
返回 `UIPhaseCapabilityExpired`。

### Tooltip 契约

`makeTooltipElement(text, config, layout)` 创建独立 `Tooltip` 内建契约；它不是把 Dropdown Popup 改名或复用
Popup 的 focus/input barrier。`UITooltipConfig` 提供 `Auto/Above/Below/Left/Right`、`anchorGap`、
`viewportMargin`、`initialDelay/reshowDelay/dismissDelay` 与
`PointerHover/KeyboardFocus/Manual` trigger flags。`setTooltipAnchor(tooltip, anchor)` 建立同 root 的显式双向关系；
自身锚定、祖先/后代循环、跨 root、stale generation、Tooltip/Popup/Modal/虚拟 row 等不稳定 Anchor 均拒绝。
一个 Anchor 至多关联一个 Tooltip，一个 Window 同时最多发布一个 Tooltip。

延迟由 `Core::IMonotonicClock`/`MonotonicTimePoint` 推进并复用 `context.motion().setMotionClock()` 的可注入时钟；
没有第二条 update loop，现有 `commitLayout()` 帧阶段只推进 fixed-capacity 状态。Tooltip 读取**最后一次成功
提交**的 Anchor `worldRect`：Auto 按可用空间选方向，显式方向空间不足时 flip，最终在 `viewportMargin` 内
clamp。`UITooltipMetrics` 只报告最后成功 publication 的 `anchorRect/tooltipRect/resolvedPlacement/open`；
Layout/Hit/Paint/Semantics 或容量失败会回滚本次时钟状态推进并保留旧 snapshot/metrics，后续按绝对时钟重试。
所有有限非负 delay 均合法，超过 native steady-clock 可表示范围时 deadline 饱和到最大时间点而不溢出。

Tooltip 固定 Ignore hit、Exclude semantics、不可聚焦、不捕获 Pointer，也不建立 Modal/Popup barrier；Pointer Down、
wheel、文本输入、Anchor disable/Hidden/Collapsed/destroy、Modal scope 改变会关闭，hover/focus 离开应用
`dismissDelay`，快速切换 Anchor 再应用 `reshowDelay`。Tooltip 文本只作为 Anchor 缺少显式 description 时的
accessible description/Windows HelpText fallback；不会覆盖作者 description，也不发布 Activate/Focus 等 action。
root-scoped `UITreeUpdater` 与 phase-scoped `PrimaryWindowUITreeUpdater` 都提供
`setTooltipAnchor/clearTooltipAnchor/tooltipAnchor/showTooltip/dismissTooltip/isTooltipOpen/tooltipMetrics`。

### Menu / MenuItem 契约

`makeMenuElement(config, layout)` 与 `makeMenuItemElement(text, config, layout)` 创建单层正式 Menu。Menu 是
独立 `BuiltinElementKind::Menu`，不是 Dropdown Popup 的别名；两者只在 `UIContext` 协调层共享同一 Window
最多一个 transient overlay 的规则，打开 Menu 会关闭 active Popup，打开 Popup 也会关闭 active Menu。
`setMenuAnchor(menu, anchor)` 建立同 root 的显式双向关系，并拒绝 self、祖先/后代循环、跨 root、stale、
 Popup/Tooltip/Menu/MenuItem/DropdownItem/Modal 与虚拟 row 等不稳定 Anchor。一个 Anchor 至多关联一个 Menu。

Menu 强制 Overlay、Ignore hit、Contain focus scope，只接受 direct `MenuItem` child。Item 不能拥有 child，
`UIMenuItemKind::{Command,Check,Radio,Separator}` 分别表达普通命令、可切换项、按 `radioGroup` 互斥项和
非交互分隔线；Check/Radio checked state 只存在于 `UIMenuStateStorage`，不占用通用 Toggle slot。
Menu surface 的空白区域和 outside Pointer Down 由 transient barrier 消费，防止同一按键 click-through；active
Menu chain 的几何还会遮挡下层 Pointer Move/hover/tooltip，disabled Item 与 Separator 不可激活但仍不会让下层控件
获得 hover。Item 仍是 Targetable，Menu 不捕获 Pointer。wheel、文本输入、Anchor/Menu disable、Hidden/Collapsed、destroy
与 Modal scope 改变都会关闭 Menu并恢复进入 overlay 前的合法焦点。

`UIMenuConfig` 提供 `Auto/Above/Below/Left/Right` placement、`anchorGap`、`viewportMargin`、
`matchAnchorWidth`、keyboard wrap 与 `closeOnActivate`。布局读取最后成功提交的 Anchor geometry，按可用空间
Auto/flip 并在 viewport 内 clamp；`UIMenuMetrics` 只在 Layout/Hit/Paint/Semantics 同一提交成功后更新
`anchorRect/menuRect/resolvedPlacement/open`，失败提交保留旧 snapshot 与 metrics。

Keyboard Up/Down/Home/End/Escape 与 Gamepad D-pad Up/Down/East 通过同一 `UIMenuCommand` 路径导航或关闭，
handled Down 的匹配 Up 由固定容量 latch 消费；Menu 命令先于 Dropdown、TabView 和通用空间焦点。
Pointer、Keyboard/Gamepad Accept 与 accessibility Invoke/Toggle 共用 MenuItem 默认激活路径。Menu/MenuItem
发布对应 semantics role；Command 无 TogglePattern，Check/Radio 发布 checked state 与 TogglePattern，
Separator 排除 semantics。当前不提供 MenuBar、submenu 或第二套 popup/runtime/update loop。

root-scoped `UITreeUpdater` 与 Runtime facade 均提供
`setMenuAnchor/clearMenuAnchor/menuAnchor/setMenuOpen/isMenuOpen/menuMetrics/setMenuItemChecked/`
`isMenuItemChecked/routeMenuCommand`。

### SplitView / Splitter 契约

`makeSplitViewElement(config, layout)` 与 `makeSplitterElement(config, layout)` 是第一方 recipes。SplitView 只解释
三个 direct Flow child：primary pane、专用 Splitter、secondary pane；`setSplitViewParts()` 对 self、重复、跨 root、
非 direct child、错误 kind、已有关系和非三子节点配置 fail closed。`UISplitViewConfig` 提供
`Horizontal/Vertical`、initial fraction、两侧最小尺寸与 splitter extent；空间不足时按最小尺寸比例退让。

```cpp
enum class UISplitViewOrientation : u8 { Horizontal, Vertical };
struct UISplitViewConfig final;
struct UISplitterConfig final; // keyboardStep
struct UISplitViewParts final;
struct UISplitViewMetrics final;
```

`UISplitViewMetrics` 只发布最后一次成功 layout commit 的三个 rect、resolved fraction 与 orientation。
`setSplitViewFraction()` 修改 pending fraction，失败 commit 不覆盖旧 metrics；成功 commit 后三个子节点的 layout、hit、
paint 和 semantics 一起可见。Splitter 复用现有 `Focusable | RangeInput`、Pointer Capture、键盘 RangeInput 和
accessibility SetRangeValue 路由，不拥有独立 update loop、capture store、Widget 状态机或 GPU pipeline。
SplitView 默认 Ignore hit；Splitter 为 Targetable、Slider semantics、Focus/SetRangeValue actions。destroy、root release、
generation reuse 与 capacity failure 都由 Context owner-thread fixed-capacity storage 原子清理关系。

### TabView / Tab 契约

`makeTabViewElement(config, layout)` 与 `makeTabElement(text, config, layout)` 创建独立正式控件。TabView 默认
Ignore hit 并发布 TabList semantics；Tab 是 `Focusable | Activate` 的 Targetable header，发布 Tab semantics，
内容 Panel 在关联后由现有节点提升为 TabPanel semantics。`UITabViewConfig` 提供 Top/Bottom/Left/Right placement、
Automatic/Manual activation、tab/content gap 与键盘导航是否循环。

`setTabViewItems(tabView, items, activeIndex)` 必须一次提交完整关系：每个 Tab/Panel 都是同一 TabView 的不同
direct Flow child，全部 pair 必须恰好覆盖 TabView 的所有 direct child，且 Tab 必须由 `makeTabElement()` 创建。
self、重复、跨 root、stale generation、非 direct child、错误 kind、非法 active index 或不完整 child 集合均 fail
closed。已关联 TabView 一旦追加 direct child，会先原子解除旧关系，调用者必须重新提交新的完整 pair list，避免
旧 active Panel 与新树拓扑混用。

Top/Bottom 使用水平 Tab strip，Left/Right 使用垂直 strip；只将 active Panel 发布为 Visible，其余关联 Panel
以 Collapsed 进入同一 Layout/Hit/Paint/Semantics transaction。`UITabViewMetrics` 只保存最后成功 commit 的
`tabStripRect/activePanelRect/activeTab/activePanel/activeIndex/itemCount/placement`，失败 commit、容量不足、
destroy、root release 与 generation reuse 不泄漏半份关系或 metrics。

Pointer、Activate accessibility action、Keyboard Arrow/Home/End 与 Gamepad D-pad 复用同一 selection/focus 路径。
Automatic 模式随方向焦点同步选择，Manual 模式只移动焦点，Activate 才选择；方向输入只在 placement 对应轴上
优先于通用空间焦点。一个 handled Down 使用既有 fixed-capacity latch 消费匹配 Up。`UITabPaint` 是 Tab 专属
selected/hover/focus/pressed/disabled chrome，由 `UIStyleRoleId::Tab`、`makeTabChrome()`、Theme transition 与
属性级 `TabPaint` override 管理，不复用 RadioButton paint。root-scoped `UITreeUpdater` 与 Runtime phase facade
提供 items、active Tab/Panel、metrics、command 以及 `setTabPaint()/tabPaint()`。

多节点业务组件可通过 `UIElementBuildTransaction` 或 Runtime 的 `PrimaryWindowUIBuildTransaction` 声明完整
`UIComponentBuildBudget`；node、UTF-8 byte、Canvas command、Activate/Toggle/Range/TextInput/Scroll/Select slot
会在创建 component root 前统一预留，集合内部 row pool 也计入预算。创建失败、reset 或析构会回滚整棵组件，
active transaction 期间 structure/layout commit 返回 `BuildTransactionInProgress`。Runtime facade 由 capability
state 持有底层事务并逐操作校验 epoch/phase；成功 commit 后只留下普通 retained subtree，不保留 component
wrapper。公开 `UIWidgetKind` 已删除；私有 `BuiltinElementKind` 只服务成熟控件 storage/行为分派。
`UIBoxPaint::primitive` 明确区分 Rectangle/Ellipse/Line：Rectangle 保留 fill/border/shadow/`cornerRadii`
box chrome；Ellipse 以 Element layout rect 为边界，`ellipseStrokeWidth=0` 表示填充，正值表示向内描边；
Line 使用 Element-local 起止点与 logical thickness。Canvas 命令复制到固定容量 storage：`SolidRect`
支持 `UILogicalCornerRadii cornerRadii`，`SolidEllipse`/`SolidLine` 与上述几何语义一致，`Image` 复用 `UIImageSource`，
`NineSlice` 使用 source-pixel/destination-logical insets 且首版仅 Stretch。Render `SolidQuad` 已接入四角
`UIPixelCornerRadii`；Retained box/Canvas `SolidRect` 现已逐角投影，不再保留并行 scalar authoring 字段。
`UI-PAINT-002-A` 已把两条 authoring 路径收敛为 canonical 四角 logical radii。圆角子树 clip、
backdrop/blur 与 BoxFill 之外的属性面仍属后续扩展。

`context.style().styleColorToken()` / `setStyleColorToken()` 以及 phase-scoped
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
| 自定义 Canvas | 可用首版 | backend-neutral `SolidRect`、`SolidEllipse`、`SolidLine`、`Image`、Stretch-only `NineSlice`；只保存几何、颜色与 AssetId/图片元数据，不能提交 shader、GPU handle 或任意 paint callback |
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

### 案例：屏幕摇杆（product-side composition）

`include/tina/ui/UIVirtualStick.hpp` 与 `samples/virtual_stick` 是上表「自定义交互」那一行的具体样板：
**没有新 widget kind，没有新 Behavior，没有新 pointer 管道**。摇杆 = 两个 Element（base ring 用
`makeEllipseOutline`，knob 用 `makeSolidEllipse`）+ base 上四个 routed pointer listener
（ButtonDown / Move / ButtonUp / PointerCancel）+ 产品自己持有的 drag state。

三个必须知道的接线点：

- **base 必须显式 `setPointerHitPolicy(Targetable)`**。裸 Panel 默认 `Ignore`，只有携带标准 Behavior
  时才被自动提升，而自定义 drag 控件不携带。
- **knob 必须显式 `Ignore`**。它整段手势都在指针下方，若可命中就会从 base 抢走 press。
- **knob 是 base 的兄弟节点而非子节点**，因为子节点会被约束在 base 的 layout box 内，而 knob 必须能停在
  drag 决定的任意位置。每帧改写它的 `overlay.offset` 来移动它——**Canvas command 在 `createElement()`
  之后不可变**，所以 knob 不能是一条重画的 canvas 命令。

`capturePointer()` 是关键：它让指针移出 ring 之后 Move 仍然到达同一个 listener。摇杆逻辑本身在
`UIVirtualStick.hpp` 里是纯函数（无 `UIContext` 依赖、可单测）。几何与失败语义：travel 半径是
`baseRadius - knobRadius`（knob 不越出 base）、命中是圆形而非 Element 矩形、超出 travel 时 knob 夹到环上
并保持满偏（**不是**冻结）、deadzone 径向且外侧重标定（与 gamepad 后端同一形状，因此手指与实体摇杆手感
一致）、对角线归一化（W+D 不比 D 快 `sqrt(2)` 倍）、单一 pointer 拥有摇杆（第二根手指被忽略）、
release/cancel 一律回中。每一条都对应 cocos2d-x 圆形控件上的一个真实缺陷，详见头文件注释与
[平台输入](platform-input.md) 的 cocos2d-x 参考节。

## Tree 与事务提交

Tree mutation、layout、hit、paint 与 semantics 都有固定容量和明确 commit。失败不能发布半份 snapshot；
下一次成功 commit 才替换旧 view。UI input route 读取上一帧 committed hit snapshot，本帧 `updateUI()`
后的 layout/paint 在 Render 前提交，并从下一帧开始参与命中。

`UIElementBuildTransaction` 开始后其子树是 live retained state，但任何 committed view 都不能中途观察它；
成功 `commit()` 只结束 build guard，下一次正常 `commitLayout()` 才一次发布。预算耗尽、子节点 descriptor
失败、显式 reset 或析构都销毁 component root，从而统一回收子树、UTF-8、Canvas 与 Behavior slot；
`UIContextStatistics::componentBuild` 同时发布各池 requested/reserved/published/failure/outstanding counter。

Layout 使用窗口 logical extent，不直接读取 framebuffer pixel。content scale/resize 更新 layout size，但同一
WindowId 不重建 Context。clean-subtree measure/arrange reuse 已实现；ListView/TreeView 通过固定 row pool、
VirtualGridView 通过固定 item pool、DataGrid 通过固定 column/row/cell pool 支持 100k logical item/row 虚拟化。
DataSource descriptor、文本或容量失败时，候选 bindings/layout/paint/semantics 不发布，旧 committed snapshot 保持可读；
完整通用 dirty-range pruning 仍未实现。

Tree structure publication、subtree destroy 以及 layout/hit/paint snapshot 构建不依赖 C++ 调用栈递归；
专项 stress gate 使用 50,000 层 retained tree 覆盖这些路径。绘制顺序所需的 `paintLayer` 在 layout
preorder 中一次传播，publication 不再为每个节点重复回溯祖先链。

绘制层级为 `UIPaintLayer`：`Content < Modal < Popup < Tooltip`，发射顺序是先按 layer 升序、layer 内按
tree preorder。节点 layer 为 `max(parentLayer, ownLayer)`，只有 `Modal` / `Popup` / `Menu`（与 Popup 同层）
/ `Tooltip` 四种 kind 会提升自己及其子树。

**没有 per-node z-index，这是刻意的**：hit 与 semantics 快照都靠回查"已发射条目"解析祖先，前者在祖先缺失时
硬失败，后者会静默读到零值 scratch 而损坏 a11y 父子关系。因此提升只能整棵子树移动，子节点永远不能越过自己的
父节点。

`Modal` 是真实 layer 而非普通流，因为模态性是由 hit 快照强制的：在此之前把 Modal 写在兄弟节点之前，会让
chrome 画在 scrim 之上却又完全不可点击 —— 看起来像渲染 bug，且没有任何断言或诊断。

活动 menu 链是 **layer 内**的排序问题（父菜单要在其子菜单之前），扁平 layer 表达不了，所以这些节点被从
Popup pass 中延后，紧随其后按链序单独遍历。

`layoutOrdinal` 保持纯 preorder，与 `paintOrdinal` 解耦 —— Layout Debugger 的子树排除依赖 preorder 区间性质。

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
box，不再由 paint/input 各自从 `worldRect + padding` 重算平行结果。空 TextEdit 没有 glyph metrics 时仍以当前
text style 的 line height 作为零宽 intrinsic content，因此默认 caret 保持在 padded content box 内垂直居中。

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
- ScrollView：wheel、thumb drag、轴向 clamp 与持久 pointer capture；`Auto` scrollbar 只有在对应轴
  `contentExtent > viewportExtent` 时才绘制并参与命中；
- Dropdown/Popup：Pointer/Keyboard/Gamepad 开关与选择，Up/Down/D-pad 导航，Escape/Gamepad East dismiss，
  Tab/Shift+Tab 关闭并退出 Popup scope，外部点击关闭且阻止 click-through；
- Menu/MenuItem：Up/Down/Home/End、D-pad Up/Down 导航，Escape/Gamepad East dismiss；Command/Check/Radio
  复用默认 Activate，Separator 跳过，inside chrome/outside Down barrier 阻止 click-through；
- Tooltip：Pointer hover、键盘 focus 或显式 Manual show；initial/reshow/dismiss delay 由可注入 monotonic clock
  推进；Pointer Down/wheel/text input 与 Anchor/Modal 失效关闭，始终 click-through 且不改变 focus/capture；
- ListView：Up/Down、PageUp/PageDown、Home/End、Keyboard/Gamepad activate 与 stable-key selection；
- TreeView：沿用集合导航，并以 Left/Right 折叠、展开或移动到父/子项；
- VirtualGridView：响应式等宽列、纵向滚动；Left/Right 移动到前/后相邻 item（可跨 logical row），Up/Down/Page 保持 logical column
  跨行移动，Home/End 到首尾可用 item，disabled item 会沿命令方向继续搜索；
- DataGrid：固定 authored 列宽、虚拟 logical row 与双轴滚动；Left/Right 切换 column，Up/Down/Page 保持 column
  跨行移动，Home/End 到首尾可用 cell，disabled row 跳过；Pointer 选择完整 row/column cell，Activate 只对当前
  committed 且 enabled 的 cell 成功；
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
  duration/delay/easing，并在再次变化时从当前 presentation value retarget。

ADR 0026 的 typed keyframe timeline 已加入同一个 `UIContext` 时钟：generation-safe `UITimelineId` 引用
Context-owned definition，track 使用 typed color/scalar/offset keyframe；create/replace/play 先预检
definition/track/keyframe/active-index 容量和 owner conflict，再原子发布。再次 play 从当前 presentation
retarget；active cancel/destroy 与 reduced-motion play 落 final target，inactive cancel 为 no-op，inactive
destroy 只释放 definition。不同 Context、stale generation、direct/Style transition owner 冲突均 fail closed。

Direct API 即使走 `duration=0` 或 `reduced-motion` snap，也会先完整校验 property、duration、delay 与
easing；非法调用不写 retained target、不取消既有 direct owner，也不产生 Paint dirty。`CornerRadius`
direct/playback 只接受 Rectangle box paint；timeline definition 可在 Ellipse/Line 状态下 create/replace，
因为 primitive 是可变 retained capability，但每次 play/retarget 都会重新预检，切回 Rectangle 后才能播放。
非对称 authored 四角仍可通过显式 scalar keyframe-0 timeline 启动并收敛到 uniform target。

`setBoxPaint()` 对 active direct BackgroundColor/BorderColor/CornerRadius 采用 setter-wins：dirty 预检成功后
一次取消这些 owner；dirty 容量失败则保留全部 owner 和旧 paint。任一对应 active timeline owner 存在时，
setter 原子拒绝，调用方必须先显式 cancel；Opacity/VisualOffset timeline 不阻止 box-paint setter。

direct transition 与 Style `BackgroundColor` transition 仍只覆盖 paint 属性。Timeline 在相同 paint 属性之外，
还允许 `LayoutWidth`、`LayoutHeight` 与仅由 Overlay placement 消费的 `LayoutOffset`。含 layout track 的 sample
会同时暂存同 timeline 的 paint track 与同帧 direct transition，再从同一 interpolated geometry 构建 Layout、
Hit、Paint、Semantics candidate；全部 builder 成功后才提交 presentation、completion target、active owner 与
snapshot。任一阶段失败保留最后成功 snapshot/presentation 和 active playback，下一次按绝对 monotonic time
重试。`reduced-motion` 立即清空 active index 并写 final target，最终几何由下一次普通 `commitLayout()` 一次
发布。Motion 不延迟 callback、改变 generation owner 或建立第二套 update loop。

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

当前 direct/Style Motion 保持 fixed-capacity 且只覆盖 paint-only 属性：背景/边框/文字颜色、opacity、统一圆角和
visual offset；typed timeline 额外开放上述三项 bounded layout 白名单。声明 transition 的匹配节点在 Style 绑定/候选阶段持久预留 BackgroundColor track；启用时
对已有节点先做全量容量预检，容量不足保留旧 spec，输入状态变化只激活已预留槽。reserved 与 active
分别统计 count/high-water；reduced-motion 直接落到 target，不进入 active list。建议 Button hover
80-120ms、press 40-60ms、release 80-120ms；
direct paint animation 不得改变真实 hit rect；layout timeline 必须让可见 rect 与 committed hit rect 同时发布。
两类动画均不得延迟 callback、隐式延期 `destroy()` 或建立第二套游戏 update loop。spring/inertia、
loop/seek/pause/repeat/yoyo/completion callback 与白名单外 layout property 仍未开放。

## Text、UTF-8 与 IME

所有 UI 文本是 strict UTF-8，无 embedded NUL；MSVC target 使用 `/utf-8`。TextEdit 默认保持单行；启用
`UITextEditMultilineConfig` 后接受 LF、支持 fixed-capacity visual-line records、soft wrap、二维 caret/selection/
hit-test、Up/Down/Home/End、垂直滚动与边界 wheel 透传。CR 仍拒绝，selection/caret 仍按 Unicode scalar index
维护，不把 UTF-8 byte offset 暴露给游戏；编辑、删除、导航和替换位置会对齐无第三方依赖的 UAX #29
grapheme 子集。BiDi/复杂 shaping 仍后置。

普通 intrinsic text 通过 `UITextWrapMode::{NoWrap,Words}` 表达换行；`makeLabelElement()` 默认 `Words`，按最终
committed content width 优先在 ASCII 空格/Tab 边界断行，长词和 CJK 按 UTF-8 codepoint 硬折行，显式 LF 保留。
Measure 与 Paint 共享同一 line cursor，并在 Flex grow/shrink、Grid area、ScrollView viewport 或 responsive rule
改变最终宽度后重新测量高度。`UITextWrapMode` 不控制 TextEdit；TextEdit 仍只通过
`UITextEditMultilineConfig`/`UITextEditWrapMode` 管理编辑 visual rows。Runtime facade 对应暴露
`setTextWrapMode()/textWrapMode()`。

`UITextLineClamp::maximumLines` 为普通 `Words` 文本提供有界 visual-line 数；零表示不限制，正值在仍有隐藏文本时
把最后一条可见行按 grapheme cluster 缩短并追加 U+2026。Measure 与 Paint 共享 clamp cursor，所以 layout 高度、
固定 paint 容量预留与最终 glyph run 一致；Semantics/UIA 始终发布完整 authored text。它不复用单行
`UITextOverflow::Ellipsis`，也不允许用于 TextEdit 或 `NoWrap` 文本；Runtime facade 暴露
`setTextLineClamp()/textLineClamp()`。

### 单行溢出与 typography

`setTextOverflow(node, UITextOverflow)` 控制单行文本超出 content box 时的行为。默认 `Clip` 保留全部
glyph 并依赖 content-box 裁剪；`Ellipsis` 丢弃放不下的尾部 grapheme cluster 并追加 `UITextEllipsisUtf8`
（U+2026），因此不会把 cluster 切成两半。三条边界：只作用于未聚焦的单行文本（聚焦 TextEdit 需要完整
run 定位 caret/selection，多行 box 换行而不省略）；含显式 `\n` 的文本退回 `Clip`；content box 宽度非正
时不截断。截断在 paint 阶段按已提交 content placement 解析，所以属性只标 Paint dirty，intrinsic measure
与 accessibility name 继续发布完整文本；布局提交必然重建 paint snapshot，宽度变化会自动重算截断点。
若字体缺少 U+2026 的可见字形，省略号不产生 paint entry，退化为无标记硬截断。
`Words` 与单行 `Ellipsis` 是显式互斥契约；需要省略的 Label 必须先选择 `NoWrap`，setter 不隐式更改另一项作者意图。
Runtime 产品通过 `PrimaryWindowUITreeUpdater::setTextOverflow()/textOverflow()` 使用相同契约与 phase
capability 校验。

`ListView` 的 materialized row 是框架私有节点；调用方不要预先截断 data source label。需要省略长行名时，
设置 `UIListViewStyle::rowTextOverflow = UITextOverflow::Ellipsis`，框架会把策略同步到固定容量 row pool，
paint 使用省略号而 Semantics/UIA 继续发布 data source 的完整 label。

`UITheme::typography` 是唯一命名字号 ramp（`display`/`title`/`section`/`body`/`control`/`caption`，
logical px），由 `validateProductTheme` 要求每级有限且为正。控件 chrome 与
`makeDisplay/Title/Section/Body/Secondary/Caption/AccentTextStyle` 都从该 ramp 取值，产品换字号只需替换
整个 ramp。`makeCompactTypographyScale()` 是 Tina Studio Compact 的紧凑桌面 ramp（20/16/15/14/14），
Editor 使用它。

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
路径不能冒充 CJK 视觉通过。Windows GLFW adapter 已提供 IMM32 preedit/commit/cancel，以及由 committed
caret geometry 驱动的 DPI-scaled composition/candidate placement；poll-local 有界队列保留 composition 顺序、
合并连续 progress，并支持无 preedit 的 direct result commit；队列项在 FIFO/optional move 后会把 borrowed view
重绑到目标对象自己的 UTF-8 storage，短字符串 SSO 也不会留下悬空 view。失焦不会在 Cancel 后补发旧 progress。
placement 清除会恢复 IMM32 默认策略。
Linux 当前只保证 committed text，原生 XIM/Wayland preedit/candidate placement 仍后置；Windows 真机候选窗
跟随/提交/取消/失焦人工证据见 `TEXT-001`。

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
SetTextValue；`context.input().performAccessibilityAction()` 在 owner thread 验证 generation、控件 kind、
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

当前支持 SolidQuad/SolidEllipse/Glyph/ImageQuad；Element `UIBoxPaint` 可选择 Rectangle/Ellipse/Line，
Canvas 可提交 `SolidRect`/`SolidEllipse`/`SolidLine`/Image/NineSlice，clip 仍为 axis-aligned scissor。
Line 在 integration 中降为携带 exact 四顶点的 SolidQuad，而不是增加 backend command kind。Runtime `RenderFramePacket`/FramePin 的 present-return CPU
completion 已落地（Null 同步 complete）；root-scoped resolver 在 frame packet 构建时按
`(root scope, AssetId)` 去重并 pin Texture2D。图片产品/失效/尺寸矩阵与固定性能 workload 已关闭；
Render `SolidQuad` 的四角像素半径已贯通 DisplayList 与 bgfx coverage shader；`UI-PAINT-002-A` 又将 Retained
box/Canvas `SolidRect` 逐角 authoring 贯通到同一链路。rounded/stencil 子树 clip 与跨 GPU/DPI golden（UI-003）不在
该切片内。

### Retained 逐角圆角首切片（UI-PAINT-002-A）

产品场景固定为 Tina Studio Compact / UI Showcase 的相邻 Segmented/header surface：第一个/最后一个 item
只圆外侧角，共享边保持方角；同一页面的 Canvas preview 以四个不同 logical radius 证明不是 uniform 值的
显示别名。Dark/Light 换肤、resize 与普通 `--auto-demo` 继续走现有唯一 retained tree 和 DisplayList 路径。

本切片实现并冻结以下边界：

- `UIBoxPaint` Rectangle 与 Canvas `SolidRect` 使用同一组固定四角 logical radii；uniform helper 只是生成
  四个相同值，committed paint 不并存第二份 scalar radius；
- shadow 继承外层四角，rounded border 使用同一外层四角，inset fill 对每个角独立减去 border inset 并夹到0；
  UI→Render bridge 逐角应用既有 logical→pixel 比例与 half-extent clamp，继续发布已有
  `UIPixelCornerRadii`，不增加 command kind、shader 或 material；
- 四个值必须 finite 且非负。descriptor、setter、Canvas assign 或 paint candidate 的非法输入不替换旧
  retained/committed 状态；dirty queue、paint snapshot 或 DisplayList 容量失败均在 backend
  副作用前原子失败并保留最后一次成功 snapshot；
- 四角值 inline 存储，不增加 pool 或 startup capacity；一个 Canvas `SolidRect` 仍占一个 canvas slot 和一个
  paint entry，box shadow/fill/border 的 entry 数与现有 uniform rounded chrome 相同；
- 现有 `UIAnimatableProperty::CornerRadius` 继续是 uniform scalar，采样值写入四个相同角。direct transition 在没有
  scalar presentation owner 且 authored 四角不相同时返回 `InvalidStyle`；timeline 的显式 keyframe0 仍可从该状态
  启动并发布 uniform presentation。per-corner track、rounded/stencil descendant clip、backdrop/blur、跨 GPU/DPI
  golden 均明确后置。

## 实际绘制链路

UI 是 Retained UI：游戏代码先创建节点并修改属性，Runtime 在一帧内提交一次布局；绘制和命中都读取
同一份已提交快照，不在 `updateUI()` 回调里直接调用 bgfx。当前主窗口的顺序是：

```text
IGameState::onEnter / updateUI
  -> UIRootOwner + UITreeUpdater 修改节点树
  -> context.publication().commitLayout(logical extent)
  -> Measure / Arrange
  -> committed layout + hit + paint + semantics snapshots
  -> UICommittedPaintView
  -> tina_ui_render_integration::buildUIDisplayList
  -> logical pixels 映射到 framebuffer pixels、裁剪、相邻 batch 合并
  -> UIDisplayList SolidQuad / SolidEllipse / Glyph / ImageQuad commands
  -> bgfx transient vertex/index buffer
  -> UI textured shader + scissor + premultiplied alpha
  -> RenderDevice::submitFrame 后显示
```

`UIPublicationPipeline::commitLayout()` 的私有 paint 阶段按 paint order 遍历可见节点。`UIBoxPaint::Rectangle` 生成矩形 entry；圆角且
同时有 fill/border 时以外层统一 border + inset fill 两条 entry 表达，shadow 继承外层半径。
`UIBoxPaint::Ellipse` 生成一个 SolidEllipse entry，`UIBoxPaint::Line` 将 Element-local 端点提交为 world-space
端点，并保存覆盖线宽的 conservative envelope。Canvas `SolidRect`/`SolidEllipse`/`SolidLine`/`Image` 从
Element local 坐标转换到 world，并在 box chrome 后按 descriptor 命令顺序追加；NineSlice 先精确计算有效
source/destination patch，再按 row-major 展开为1..9个 Image entry；
文字生成 Glyph entry；ProgressBar 追加按 value 缩短的 foreground，RadioButton 追加 indicator 和
选中内块，TextEdit 在焦点状态下追加 selection highlight、IME preedit 和 caret。Integration 再把
逻辑坐标投影为像素矩形，并丢弃空/透明/完全在 clip 外的 entry。

Solid、SolidEllipse 和 Glyph 共用一套带 UV 的 UI coverage shader：SolidQuad/SolidEllipse 绑定 1×1
白色 R8 纹理，Glyph 绑定 UIContext 持有的 R8 atlas。圆角 SolidQuad 由每顶点携带的像素
width/height/radius 计算 SDF coverage；SolidEllipse 使用像素 extent 与 local UV 计算填充 coverage，正
stroke width 再减去内椭圆 coverage，形成向内描边。Line 则在 logical 空间构造线宽法向的四个角点，
逐点应用 framebuffer `scaleX/scaleY` 后作为 exact `UISolidQuadVertices` 提交；integer bounds 只作
conservative AABB，因此 anisotropic 投影也不会退化为 angle + 单一缩放的近似。上述路径保持相邻 batch
无 per-command uniform。片元颜色是顶点 premultiplied 颜色乘 coverage，
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
| `Panel` | 容器和布局 | `UIBoxPaint` 的 SolidQuad；默认允许后代越过自身 border-box，`clipDescendants` 可选择 axis-aligned 后代裁剪 |
| `Modal` | committed Focus/Input scope、下层输入 barrier、Dialog semantics | Theme surface chrome；布局/内容由 retained 子树组合 |
| `Label` | 只读 UTF-8 文本 | Glyph quads；没有可用字体时为确定性的 placeholder SolidQuad |
| `Button` | Pointer、Tab、Enter/Space/KeypadEnter、Gamepad South | 默认 Tonal；Primary/Danger 为强调填充，Outlined 保留边界，Text 仅在交互态显示 state layer，disabled 仍保持透明背景并仅降低内容 alpha；统一使用 `UIButtonPaint` 状态色 + 文本/图标 tint |
| `Checkbox` | checked 切换，复用 Button action/焦点路径 | 背景 SolidQuad + `UICheckboxPaint` 勾选指示块；标签由相邻 Label 组合 |
| `Slider` | Pointer 横向拖动、Tab/空间导航/显式焦点，min/max/value/step | 背景 track + `UISliderPaint` filled track/thumb；状态优先级为 drag > focus > normal |
| `TextEdit` | 默认单行；可选 LF/soft-wrap 多行、选择、grapheme 对齐光标、IME | `UIBoxPaint` 背景 + `UITextEditPaint` hover/press/focus/disabled、selection highlight 与 caret + 按 visual row 的文本 Glyph/placeholder |
| `ProgressBar` | 非交互 determinate range/value | track SolidQuad + 按比例缩短的 foreground SolidQuad |
| `RadioButton` | 同直接父节点互斥选择 | 标准 role 绘制 indicator + 选中内块；SegmentedButton role 隐藏 indicator，以 selected/hover/focus/pressed/disabled 背景和焦点边界表达状态，仍复用同一互斥选择、键盘、Gamepad 与 accessibility 路径 |
| `ScrollView` | wheel/thumb drag 与 viewport clip | 内容沿 offset 平移并裁剪，追加 track/thumb SolidQuad |
| `Dropdown` | ComboBox value、Pointer/Keyboard/Gamepad 开关 | Button chrome + 文本 + 下拉指示块 |
| `Popup` | 独立 List/focus scope、anchor flip/clamp、输入 barrier | 顶层 overlay surface chrome，始终晚于普通树绘制 |
| `Tooltip` | Anchor help；PointerHover/KeyboardFocus/Manual，不可聚焦、Ignore hit、无 barrier | 独立顶层 overlay surface；基于最后成功 Anchor geometry 做 Auto/flip/clamp，最后成功 metrics 与整套 snapshot 原子发布 |
| `Menu` | 显式 Anchor、单 Window transient overlay、键盘/Gamepad navigation 与焦点恢复 | `MenuSurface` 复用 Popup surface chrome；四向 Auto/flip/clamp，surface Ignore hit、chrome/outside barrier 不 click-through |
| `MenuItem` | Command/Check/Radio/Separator；Activate、checked/radio group 与 UIA Invoke/Toggle | `MenuItem` 复用 DropdownItem state chrome，Check/Radio 追加 theme metric 驱动的 indicator，Separator 绘制细线 |
| `SplitView` | 两个 direct pane + 一个专用 Splitter；fraction/minimum/orientation | 不绘制自身 chrome；一次 arrange 三个 pane/splitter rect，metrics 与 Layout/Hit/Paint/Semantics 同一成功 commit 发布 |
| `Splitter` | Pointer drag、键盘 RangeInput、Slider semantics | 复用现有 RangeInput/Focus 路径的 targetable splitter surface；不新增 GPU 或 Widget pipeline |
| `TabView` | 完整 direct-child Tab/Panel pairs、Top/Bottom/Left/Right、Automatic/Manual activation | 不绘制自身 chrome；Tab strip、active Panel visibility 与 committed metrics 在同一成功 transaction 发布 |
| `Tab` | Pointer、Keyboard/Gamepad navigation、Activate、Tab selected state | `UITabPaint` 独立解析 selected/hover/focus/pressed/disabled surface 与 focused border，文本沿现有 Glyph pipeline 绘制 |
| `DropdownItem` | ListItem selection 与焦点 | Button chrome + 选中背景 + 文本 |
| `ListView` | 虚拟化 List/ListItem、键盘/手柄选择与滚动 | 固定 row pool + 选中/hover chrome + scrollbar |
| `TreeView` | 虚拟化 Tree/TreeItem、层级展开/折叠 | 固定 row pool + disclosure/indent + 选中 chrome + scrollbar |
| `VirtualGridView` | 响应式等宽列、虚拟 item、二维键盘/手柄选择与纵向滚动 | 固定 item pool + 选中/hover chrome + vertical scrollbar |
| `DataGrid` | 固定列宽/header、虚拟 row/cell、二维选择与双轴滚动 | 固定 column/row/cell pool + header/grid line/selected-row chrome + horizontal/vertical scrollbar |

控件创建入口集中为 `UIRootBuilder`/`UITreeUpdater::createElement(descriptor)`；属性 setter 只修改
retained 状态并标记必要的 dirty 类别。

**产品 Theme（默认皮肤 + 全局换肤 + 局部覆盖）：**

- `UIContext` 持有产品 Theme 状态，通过 `context.style().productTheme()` 读取，默认 `makeDefaultProductTheme()`；
- `createElement(..., make*Element(...))` 按 descriptor 的 `UIStyleRoleId` 创建 Button/Checkbox/Slider/TextEdit/ProgressBar/RadioButton/
  ScrollView/Dropdown/Popup/Menu/MenuItem/DropdownItem/ListView/TreeView/VirtualGridView/DataGrid/Tab 与 Label 文本样式在创建时 **自动 apply** 对应
  `make*Chrome` / text style；Root/Panel 默认无底色（容器），需背景时用
  `makePanelBoxPaint` / `makeSettingsPanelChrome`；
- `setProductTheme(theme)` 会校验 metric，并事务式重绑所有仍继承产品 Theme 的既有控件属性；容量、
  文本测量或线程校验失败时，Theme 与控件属性均保持不变；之后新建的节点继承最新 Theme；
- 局部覆盖按属性分离：`setBoxPaint` / `set*Paint` / `setTextStyle` 只让对应属性脱离后续全局换肤，
  同一控件上未覆盖的其他属性仍会跟随 Theme；即使 setter 写入当前相同值，也视为显式局部覆盖；
  `clearOverride(mask)` 从当前 StyleRole 和当前 Theme 恢复选定属性；
- `makeButtonElement()` 默认绑定 `ButtonTonal`；Primary/Danger/Outlined/Text 必须由业务按命令层级显式选择，
  不再保留“所有按钮默认 Primary”的旧视觉行为；
- `setStyleRole()` 原子切换 recipe，保留显式 local override；ButtonPrimary/ButtonDanger/ButtonTonal/
  ButtonOutlined/ButtonText/SegmentedButton、四级 Text、
  Panel/Modal/Popup/Tooltip/Menu surface、MenuItem、Tab 与全部现有控件 role 均有 recipe；
- Runtime 游戏通过 phase-scoped `PrimaryWindowUITreeUpdater::productTheme()` / `setProductTheme()` 换肤，
  不取得裸 `UIContext`；
- 默认 panel/button 分别使用 6px/4px 统一圆角；普通 PanelSurface、Primary 与 Tonal 使用平面填充，
  Outlined、Segmented、Modal/Popup 和框定工具才保留单色 1px 边界；默认主题不再使用 light/dark
  假浮雕或偏移阴影，focus 使用独立边界，因此 hover / pressed / focused / selected / disabled 仍可辨识；
- 默认 Dark/Light palette 均使用中性 surface、蓝色 accent、明确 on-accent 前景；另提供
  `makeLightProductTheme()` 与完整 chrome 工厂（`makeButtonChrome`、`makeTonalButtonChrome`、
  `makeOutlinedButtonChrome`、`makeTextButtonChrome`、`makeSegmentedButtonChrome`）。
  `makeButtonChrome()` 的 label 使用 `onAccent`，因此 Dropdown 与 CollectionItem 这类复用填充按钮
  chrome 但坐在中性 surface 上的 role，必须把 `label.color` 显式复位为 `textPrimary`；
  校验“控件是否继承 Theme”的产品代码要对齐节点实际 role，默认 `makeButtonElement()` 对应
  `makeTonalButtonChrome()` 而不再是 `makeButtonChrome()`。

`UIBoxPaint` 仍是 escape hatch；Rectangle 可携带 borderLight/borderDark/borderWidth、shadow（假 elevation）
与 `UILogicalCornerRadii cornerRadii`，Ellipse/Line 则使用各自封闭的几何字段。Image/Icon/NineSlice 基础绘制、产品采用、
失效/尺寸矩阵与性能 workload 已关闭。
Render 四角像素半径与 Retained 逐角 authoring 已由 `UI-PAINT-002-A` 贯通；圆角子树 clip、毛玻璃与
完整 CSS 仍未实现；
ColorToken startup registry/value 与运行期 reverse-dependency update、literal/token-backed BoxFill rule、node-local state
和 Runtime 入口已经可用。

## 产品接入与证据

`tina_sample_ui_showcase` 是可调整窗口的 Modern Desktop workbench，默认 `1280x800`，最小
`960x640`。它使用一棵 retained root 和五个 Desktop band：Command Bar、Explorer、可滚动 Component Canvas、
Token/State Inspector 与 Status Bar，并展示 24 个控件和三个第一方 composition profile：

- Primary、Danger、disabled Outlined 与 Text reset Button；
- Checkbox、Slider→ProgressBar 联动、UTF-8 TextEdit；
- Performance/Balanced/Quality、Dark/Light 与 Compact/Comfortable 三组 RadioButton；
- Dropdown、虚拟化 ListView/TreeView 与 ScrollView；
- `UIIconButton`、`UIFormField`、`UIDialog`，以及 Panel elevation、圆角/边框/阴影和主题色板；相邻
  segmented Radio 只圆外侧角，首个 palette swatch 通过 Canvas 发布四个不同 radius，合计
  `asymmetricCornerProducts=3`。

Showcase 的普通页面树使用 Flow/Flex：`Root -> Background -> CommandBar/Main/StatusBar`，
`Main -> Explorer/ComponentScrollView/Inspector`，中央 Component Canvas 下的 section 是全宽纵向 Flow 子项，
不再使用双列卡片墙或嵌套卡片。只有 Dropdown Popup、Tooltip 与 Dialog Modal 使用 Overlay。

它使用默认 product chrome 呈现 hover/pressed/focused/disabled 层次，并通过
`setProductTheme()` 在既有 retained tree 上事务切换 Dark/Light。Density 不在 live root 上热改：
`ShowcaseApplication` 持有 `ShowcaseUIState`，保存 FormField 文本、值控件、选择/展开、两个 ScrollView offset 与
Dialog 状态；切换 Compact/Comfortable 时通过 `GameState::requestReplace()` 释放并重建唯一 root，业务状态连续保留。
Context 为失败原子的 State replacement handoff 预留两个 root slot，稳定帧仍只有一个 active root；Window 级
StyleClass/ColorToken/StyleSheet 只注册一次并由 replacement root 复用。
`--auto-demo` 会执行 scheme 往返、两次 density replacement、Slider/ProgressBar、Dropdown/List/Tree、两个滚动区和
Dialog，并在退出 JSON 中验证 `controls=24`、`componentProfiles=3`、`workbenchBands=5`、
`densityRebuilds=2`、`uiRootsCreated=3`、`componentScrollOffset=240` 与最终 Dialog closed。完整文字视觉验收必须使用
bgfx + FreeType preset；普通 bgfx preset 的 placeholder text 只用于确定性降级和生命周期 smoke。旧 1280x980
capture 仅作为 TMD-07 前的卡片墙基线，不再代表当前布局。

2026-07-31 的 `RunUiStateFeedbackVisualGate.ps1` 通过 Windows MSVC/bgfx/FreeType 产品路径驱动真实
Win32 pointer route，对 Dark/Light 的 normal、hover、focus、pressed/drag、selected 与 disabled 状态执行
22项差分检查并全部通过。可复现环境、可执行文件与截图哈希记录在
[UI-STATE-FEEDBACK Windows Evidence](evidence/ui-state-feedback-evidence-windows.md)；生成截图已按约定回收。

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

`TinaEditor` 的 Modern Desktop 源码迁移使用 Dark/Compact `UITheme`，并在 `createRoot()` 前绑定 density。
根节点按 Command Bar、按需出现的外部 Document Tabs、Workspace、Status Bar 排列；Command Bar 同行放置菜单、居中的
2D/3D selector、history/save 与 play controls，active Viewport 只保留一条 context toolbar 与画布，不再发布 footer；Workspace 由三层
`SplitView` 表达 Left Dock、Viewport、Inspector 与可折叠底部面板，不引入 Dock Runtime 或第二棵 UI tree。底部面板
默认 `Collapsed` 且 vertical SplitView fraction 为 `1`，Status Bar 可在 Animation 与 Output 间切换或再次点击当前项
收起；Output 复用最新 authoring feedback。Left Dock/Inspector Header 提供向外收起按钮，`View` 菜单提供 Check
入口恢复；三类 pane 隐藏时 pane、splitter 与 fraction 同步更新，恢复时还原用户最后拖拽尺寸。
Editor 的 Button/TextEdit/Tab、List/Tree row、Splitter、Status Bar 与 spacing/padding 读取 Theme metrics；Editor 专属
Splitter 将 10 logical px 的可见线宽与命中宽度统一，不再绘制 1 px 中线并浪费两侧热区；
旧的 Editor 局部 surface ColorToken、StyleClass 和自动换色路径已删除。Render viewport 仍只消费成功提交的
retained layout rect，Scene/Asset/Render owner 与数据流未改变。2026-08-20 按 Editor 大功能规则完成统一增量
build；`tina_ui_tests` 771/771、`tina_editor_tests` 114/114、`tina_editor_app_tests` 13/13。TinaEditor
workspace、Color Picker 与 Delete Dialog 的产品取证均通过
`IRenderDevice::capturePrimaryFrameRgba8()` 写出 2560x1600 RGBA8（16,384,000 bytes）；Color Picker capture
还会验证 Color Field swatch/text-edit 与 R/G/B slider/value 的 committed rect 全部位于 Inspector viewport 内，避免只凭
内部展开状态误报视觉通过。2D/3D `--auto-demo --frames=68` 均完成 stage 57 与最终 selection commit。

`UIColorField` 统一承担 swatch 与规范化 `#RRGGBBAA` summary；展开的 `UIColorPicker` 不再重复同一颜色信息，
每个通道行固定为 `R/G/B/A + Slider + 0..255`。通道数值由 fixed-capacity ASCII snapshot 生成，Slider 分别绑定
`SliderRed/SliderGreen/SliderBlue/SliderAlpha` Theme role，因此 Dark/Light 切换会重算完整 slider chrome，
不依赖会 detach Theme binding 的局部 paint override。Editor 在 selection publication 与 Slider callback 的下一次
retained update 中同步 Color Field hex/swatch、全部 channel value 和 slider value。

Editor Inspector 的 Transform 使用三行固定父级 Grid：每行以52 logical px label track 加一个 `Fr` value track
表达 Position、Rotation、Scale，value track 再按当前 workspace 发布一至三个等权 `Fr` axis cell。每个 axis cell
使用12 logical px 居中轴标签加 `Fr` TextEdit，整体限制在52..96 logical px；拖动 Inspector 只改变 Grid 分配，
不切换方向、不重建节点，也不使用宽度阈值。World2D entity 的 Position/Scale 发布 X/Y，
Rotation 只发布 Z；World3D entity 发布完整九轴。Asset Inspector、TileMap document 或无 entity selection 时，
Header、字段与 Apply 一并 `Collapsed`。旧的九行 `UINumberField`、逐轴 step Button 与延迟 step 状态不再属于
Editor 产品布局。Apply action 位于 Transform Header 右侧。节点专属属性与 Parent ID 等其他 Inspector PropertyRow
使用68 logical px label + `Fr` value Grid，单值 TextEdit 最大宽度统一为132 logical px；Sprite Size/Pivot 与
ShadowOccluder Start/End 分别把 X/Y 合并在同一属性行。通用表单仍可独立使用 `UINumberField` 的
`Above`/`Leading` placement。Command Bar 使用等宽 grow region 保持 workspace selector 居中，并保护
mode/play/history/save 命令；document path 由 session 与平台 Save As dialog 持有，不再显示为 toolbar 输入框。
六个固定 Tab 槽中空槽为 `Collapsed`，刷新时从统一 layout recipe 恢复
`Visible`；普通信息文字使用 Theme primary，只有 warning/error feedback 使用对应 tone。Inspector 的 Transform、
Sprite、Camera、PointLight、ShadowOccluder 与 SpriteAnimation 浮点显示统一经过 fixed-capacity、locale-independent
`to_chars` snapshot，以 6 位有效数字去除无意义尾零；该 presentation 不改变输入解析、canonical document 或事务精度。
节点属性折叠 Header 的收起/展开提示分别使用产品 atlas 提供的 ChevronRight/ChevronDown `UIIconContent`，两个 Icon node
随同一 retained state 切换 visibility，不再用文本字符模拟图标。Timeline 六个帧槽保持固定 `44 logical px` 宽度，只显示帧号；
选中帧 summary 承载 Sprite、时长和事件数，并在空间不足时以 ellipsis 绘制。当前帧使用 `ButtonPrimary` chrome，
其余与空槽使用 `ButtonOutlined`，不再在标签前拼接 `>`。

Inspector 节点专属属性按 selection/workspace 和严格 Node kind 发布：同类型多选只显示该类型固有的 property section，
`AnimatedSprite2D` 显示 Rendering + Animation，其他 Node 只显示对应区段。不存在 Components Header、Add/Remove
Component 或兼容菜单；Asset Inspector、TileMap document、无 Node selection 或多选类型不一致时折叠全部专属 root。
Hierarchy 的 Header、Parent ID 行与 Apply Parent wrapper 仅在 entity context 发布，TileMap 的 Header、状态摘要与
四个 action row 仅在 TileMap document 发布。Asset Inspector 只保留 Identity 与 asset metadata/dependencies，
Scene node 保留 Identity、Transform、节点专属属性与 Hierarchy，TileMap document 保留 Identity 与 TileMap；
Scene 无 entity selection 时只保留 Identity。切换只更新构建时保存的完整 Grid/root layout 的 `visibility`，
不会把 PropertyRow 恢复成旧 Flex、破坏 IconButton wrapper 的居中布局或丢失 section state。

Project Assets 的 compact virtual grid 使用 120 logical px 最小格宽和固定 compact 行高；格子显示类型优先的
`AssetKind #abcd` 短标签。完整 canonical AssetId 由网格下固定 22 logical px 的 selected summary 与 Inspector 保留，
summary 在窄 Dock 中只做单行 ellipsis，不改变 retained 文本、格子尺寸或下游布局。New/Open Project 只保留在
`File` 菜单；左侧 Dock 不再重复项目生命周期命令。打开当前 Asset 使用 divider 后的 ArrowRight。

EditorApp 私有 `EditorPanelHeader` 以无圆角、无描边的扁平 surface band 统一 Dock、Inspector 与 Timeline 顶栏，
actions 区占据剩余宽度并末端对齐；`EditorSectionHeader` 以固定 3 节点组合标题与可伸缩 subtle Divider，统一 Inspector
的 Identity、Transform、节点专属属性、Hierarchy 与 TileMap authoring 段，并按 context 仅发布有效段。
Hierarchy、Project Assets、Source Imports
计数使用 Neutral Badge，Inspector selection 使用 Accent Badge。原 `Authoring` 提示段、`Move X +1` 调试按钮及重复的
静态说明已从可见树删除，短时编辑结果由现有 `UISnackbarHost` 发布，用户主动打开的 Output 底部面板镜像最新反馈。
Status document/runtime 按 0.8/1.2 分配剩余
宽度，二者与 Snackbar message 都使用 ellipsis，完整 retained/semantics 文本不变；command、document transaction
与 auto-demo 路径不变。

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
generation `GamepadId` 作为 assignment identity，可由 `context.input()`、`UITreeUpdater` 或 Runtime phase facade 的
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
| `UI-MODERN-DESKTOP-001` | InProgress：TMD-00..07 已完成并通过集中 UI/Runtime/UI-Render/UIA 门禁；TMD-08 Desktop Shell reference 已完成结构、嵌套 SplitView、正式 TabView、Menu/Dialog/Tooltip、Splitter、产品 icon atlas，并已迁移到父容器宽度驱动的 bounded responsive rules，删除 resize callback 的手工宽度档位。TMD-09 Editor/2D/3D 迁移与 TMD-10 OS scheme 已通过 2026-08-19 集中 build、定向测试、产品 smoke 和 installed DesktopBootstrap consumer gate。TMD-11 已通过 `tina_bench_tests` 10/10 与 Static/Component/Style/Motion 冻结 workload 确定性 gate，开发机墙钟仍为 provisional，固定机 hard gate 继续由 PERF-002 跟踪。当前宿主固定 200%，仍待 Desktop Shell 100%/150% 配对视觉报告及最终文档收口；完整规范见 [Tina Modern Desktop UI](ui-modern-desktop.md) |
| `UI-002` | Windows UIA：tip 跨进程 gate 证据已固化（2026-08-03）；待 Narrator/Inspect 人工金标 |
| `UI-003` | 跨 DPI/GPU 容差视觉门禁（映射单测 + 单机 ROI/baseline + content-scale-like 逻辑尺寸矩阵 + sample contentScale JSON + 字体 identity fingerprint 已有；2026-08-10 当前 Windows 宿主 100%/150%/200% raster baseline 已分别通过独立复跑；多显示器混合 DPI 与跨 GPU 像素金标后置） |
| `TEXT-001` | InProgress：T1 多行、T2 UAX #29 grapheme 子集、T3 Windows IMM32 placement 的代码/自动 gate 已完成；BiDi/复杂 shaping、Linux 原生 XIM/Wayland 与 Windows 真机 IME 人工证据待补 |
| `UI-PERF-001` | Done；clean 4096-node、单节点 paint dirty、route、100k 虚拟集合、`ui_image_nineslice_v1`、完整 `ui_component_build_v1`、`ui_style_state_v1` 与 `ui_motion_v1` 已落地；固定机前时间结论只报 provisional |
| `UI-COMPONENT-001` | Done；Runtime phase-scoped bounded transaction、六类 fixed-capacity Behavior side store、node/text/canvas/各 Behavior pool 统一 reservation/counter 与 `ui_component_build_v1` 已落地 |
| `UI-DIALOG-001` | Done：generation-safe `UIDialogStateStorage`、build 后默认 closed、`openDialog/dismissDialog/isDialogOpen` Context/Updater/Runtime facade、commit-bound Modal/Focus publication、单 Window 单 open intent、Menu/Tooltip 协调、容量失败原子性与 Editor/Showcase/Desktop Shell consumer 迁移已落地 |
| `UI-STYLE-001` | Done；强类型 StyleClass/ColorToken、startup registry/value、运行期 reverse-dependency token getter/setter、node-local pseudo-state selector、literal/token-backed BoxFill/imageTint rule、预编译 stylesheet、Runtime facade、固定 workload 与 Integration/Visual 门禁已落地；不做完整 CSS |
| `UI-MOTION-001` | Done；fixed-capacity paint-only transition、monotonic clock、retarget、reduced-motion、Style BackgroundColor persistent reservation/activation 与 `ui_motion_v1` |
| `UI-MOTION-002` | Done；bounded layout whitelist、跨 direct/timeline candidate transaction 与两个 timeline workload 已落地；2026-08-16 定向 gate 为 UI 28/28、Runtime facade 1/1、bench unit 10/10，paint/layout seed 0/1/2 均通过且 allocation delta=0；固定机 hard gate 仍由 `PERF-002` 跟踪 |
| `UI-PAINT-002-A` | Done；`UILogicalCornerRadii` 已贯通 Retained box/Canvas `SolidRect`、committed paint、border/inset/shadow、UI→Render 投影、checksum 与 Showcase consumer；2026-08-17 Windows gate 为 UI 672/672、Runtime UI 130/130、UI-Render 28/28、bgfx 111/111、bench 10/10，Showcase 120 帧 exit 0。rounded/stencil 子树 clip、backdrop/blur、per-corner Motion 与跨 GPU/DPI golden 仍由 `UI-PAINT-002`/`UI-003` 后置跟踪 |
| `UI-FLOW-001` | Done：固定容量 Activatable Screen/Layer Stack、Back/Confirm/Menu Action Router、16 槽本地用户、完整 generation Gamepad assignment、per-user 设备 revision、断连/reset 清理与 2D 产品接入已落地 |
| `UI-TOOLTIP-001` | Done：独立 Tooltip contract、同 root Anchor 关系、Hover/Focus/Manual + monotonic delay、Auto/flip/clamp、单 Window 独占、Ignore hit、输入/可见性/Modal dismissal、committed metrics、失败回滚、accessible description/HelpText fallback 与 Runtime phase facade 已落地 |
| `UI-MENU-001` | Done：独立 Menu/MenuItem recipes、显式同 root Anchor、单 Window transient overlay 与 Popup 协调、Command/Check/Radio/Separator、四向 Auto/flip/clamp、Pointer barrier、Keyboard/Gamepad/accessibility 共享激活、Menu/MenuItem UIA、committed metrics/失败原子性、固定容量 state/layout/input 模块与 Runtime facade 已落地 |
| `UI-TABVIEW-001` | Done：独立 TabView/Tab recipes、完整 direct-child pair 关系、四向 placement、Automatic/Manual activation、Pointer/Keyboard/Gamepad/UIA 共享路径、TabList/Tab/TabPanel semantics、专属 `UITabPaint`、committed metrics、失败原子性与 Runtime phase facade 已落地 |
| `UI-GRID-COLLECTIONS` | Done：VirtualGridView/DataGrid 的固定容量 layout、双轴/单轴滚动、Pointer/Keyboard/Gamepad、selection、paint、semantics 与 Runtime phase facade 已实现；Editor Project Assets/Source Imports 已分别成为首批 VirtualGrid/DataGrid consumer。2026-08-21 集中 gate：定向 UI 10/10、Runtime facade 3/3、UI 782/782、Runtime UI 148/148、UI-Render 28/28、EditorApp 20/20，Editor 2D/3D 各 68 帧 exit 0 |
| `UI-BEHAVIOR-SPI-001` | Deferred：只有标准 Behavior + routed listener 存在有证据的表达缺口时才评估 startup-only 高级 SPI |
| `UI-002-LINUX` | Linux AT-SPI adapter 与真实辅助技术验收（Deferred，不阻塞 Windows UI-002） |
| `SDK-001` | package/consumer gate 已落地；ADR 0024 已 Accepted，pre-1.0 strict exact-version 正反 probe（含 tweak/range 拒绝）已纳入 Windows gate。正式 supported ABI tuple baseline/object probe 仍是 release checklist |

ProgressBar/RadioButton 的产品接入 `UI-001` 已完成，不应重新列为 Planned。
Theme A/B（token、panel 边、Low 假影、sample 改 token）已在产品 sample 路径落地；UI-002-SPI 与
可选 `tina_ui_uia` 属性、fragment、control pattern 与 action 切片已落地；UI-004 的 Focus Scope/Modal/Pointer Capture 与 UI-005 的
ScrollView、Dropdown/Popup、虚拟 ListView/TreeView 已完成。外部 Narrator 真机门禁与 UI-003
跨 GPU/DPI 金标仍不能标成 Done。ADR 0022 的 Element composition 主体已完成；Image/Icon、
Component/Behavior、StyleClass/pseudo-state、ColorToken 运行期更新、stylesheet imageTint、产品视觉门禁与
paint-only transition 与 paint/bounded-layout keyframe timeline 已汇合。spring/inertia、高级 playback、layout
property 白名单扩展与更广 Style 属性面仍是独立后续项。不再重复列已删除的
`UIWidgetKind` 迁移，也不把尚未实现的目标 API 写成当前能力。
