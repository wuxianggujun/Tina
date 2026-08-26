# Tina Editor UI/UX 路线图（Proposed）

- 状态：`InProgress`
- 日期：2026-08-24
- 任务入口：[EDITOR-UI-UX-001](backlog.md)
- 当前事实：[Editor 2D / 3D](editor-2d.md)、[Modern Desktop UI](ui-modern-desktop.md)、[Retained UI](ui.md)

本文记录 Tina Editor 下一阶段的视觉与交互提案，不把提案写成已经存在的产品能力。任务状态、依赖和最终验收仍以
[Backlog](backlog.md) 为准；源码、CMake 和实际门禁优先于本文。本文不新增第二套 UI ABI，也不替代已 Accepted 的
[ADR 0011](adr/0011-retained-ui.md)、[ADR 0022](adr/0022-ui-element-authoring-and-layout.md) 或
[ADR 0023](adr/0023-ui-extensibility-style-paint-motion.md)。

## 1. 目标与边界

### 1.1 目标

Tina Editor 当前已经具备完整的桌面工作台结构和较成熟的 `Tina Studio Compact` 主题。本路线图要解决的不是“控件数量
不够”，而是以下四个产品问题：

1. 视觉焦点不够明确，用户需要在大量文字和相似 surface 中寻找当前对象与当前任务。
2. Hierarchy、Inspector、Viewport、Project Assets 的状态表达还没有形成一套统一的对象语言。
3. 导入、加载、失败、空集合和未保存等状态需要更持续、更可扫描的反馈。
4. Editor 仍缺少少量可控的 Tina 品牌识别点，但不能退化成营销式首页或装饰卡片墙。

### 1.2 不变的架构边界

- 继续使用一棵 per-window retained UI tree、committed snapshot、固定容量事务和 owner-thread 更新。
- 普通页面继续优先使用 Flow/Flex；Overlay 只用于真正叠放的 Popup、Dialog、Drop overlay 和短时提示。
- 新的视觉组件优先是 Element composition profile，不新增公开 `Widget subclass`、第二套行为状态机或 GPU callback。
- `Tina::UI` 不直接拥有 AssetSystem、RenderDevice、GLFW、bgfx、FreeType 或 Windows native handle。
- 外部文件拖入若要进入正式产品，需要 Tina-owned Platform/Runtime 事件；Editor 只消费窄能力，不读取 native event 类型。
- 所有用户可见文本保持 strict UTF-8；Windows target 继续使用 `/utf-8`。

### 1.3 状态词

| 状态 | 含义 |
| --- | --- |
| `Proposed` | 设计方向已记录，但尚未冻结公共契约或实现 |
| `Planned` | 已进入 `EDITOR-UI-UX-001` 的可执行切片 |
| `InProgress` | 源码实现已经开始，但尚未完成统一门禁 |
| `Implemented` | 代码、文档和对应验收证据均已收口 |
| `Deferred` | 方向暂不进入当前里程碑 |

## 2. 当前 UI 基线

当前 Editor 已有 Command Bar、Document Tab strip、Hierarchy、Project Assets、Viewport、Inspector、Animation/Output
底部面板和 Status Bar；布局由三层 `SplitView` 组成，主题为 Dark/Compact `Tina Studio Compact`。现有公共 UI 已覆盖
Surface、Divider、Badge、Switch、Icon/Image、Tooltip、Dialog、Snackbar、ScrollView、Menu、TabView、SplitView、
ListView、TreeView、VirtualGridView 和 DataGrid。

当前最重要的资源面事实是：

- Project Assets 使用 `UIVirtualGridView`；每项现在同时发布稳定 key、主文本、kind secondary label、status label/status，以及 preview/icon image metadata。Texture/Sprite 会解析真实 cooked 缩略图，其他资源使用稳定类型 icon fallback；resolve 失败只把本项标为 `Missing`，不修改 authoring bytes。
- Project Assets 已提供 ASCII case-insensitive 搜索、All/2D/3D/Media 组合过滤、stable item key，以及固定 metrics 的 Grid/List 两档（Grid 约 132x72 logical px，List 280x30 logical px）。Breadcrumb、Start Center、无资源 EmptyState、导入 Activity 文本和失败 InlineCallout 已接入；导入 status 现在按稳定 AssetId 记录 `Queued/Preparing/Copying/Cooking/ReadyToCommit/Committed/Error`，只更新当前批次映射到的资源，不把未受影响资源误标为导入中。
- Project Assets 标题栏已有 Import、计数、Open、Refresh；Source Imports 使用三列 DataGrid（`Kind | Source | Status`）显示 intended set，其中 Source 列当前约 190 logical px。
- Source Import service 已拥有 Preparing、Copying、Cooking、Ready、Commit、Failure 等事务状态，失败时保留旧 Catalog。
- Project Assets 的导入 Activity 会显示 queued/Preparing/Copying/Cooking/Committing 等阶段；失败 Callout 提供 Retry 与 Open Output，并保持上一份 Catalog 可用。
- Retry 保存并重放完整 intended units 与原始外部 UTF-8 path batch；首次拖放或导入失败后不会因临时 request 被消费而丢失重试输入。Running 导入提供 Cancel，owner thread 会 request-stop/join worker、清理 fresh stage 和 viewport drop intent、恢复既有资源状态并移除尚未进入旧 Catalog 的临时历史。
- Snackbar 已用于短时 authoring feedback；Output 面板保留固定容量的 bounded history，并支持 All/Info/Warning/Error 筛选，不成为第二份业务状态。
- Output 已使用三列 DataGrid 发布 `Level | Context | Message` 稳定行、计数、Clear、选中详情和 Locate；只有历史项捕获到真实 AssetId、DocumentKey 或 scene stable ID 时才允许定位，双击和 Locate 按钮复用同一请求，不从日志文本猜测 ID。
- Hierarchy 已支持 Node 拖放、inline rename、context menu 和 stable ID 选择；这与外部文件拖入是两条不同的输入链路。

### 2.1 实施状态快照（2026-08-24）

已落地并进入统一验证范围：

- GLFW 3.4 `glfwSetDropCallback` release/drop 主链已接入 `FileDropEvent`、固定容量 UTF-8 path arena、Runtime primary-window forwarding、Editor 有界队列和目标命中；GLFW 在 Windows HDROP、X11 XDND、Wayland `wl_data_device` 上提供同一 ingress 能力。
- 释放到 Project Assets 或任意 active viewport 都复用 `EditorSourceImportService`；资源导入只更新 Project Assets/Catalog，不自动创建或修改场景节点。
- Project Assets 的真实 Texture/Sprite 缩略图、类型 icon、secondary/status、Breadcrumb、Start Center、EmptyState、Activity 文本、失败 InlineCallout、Retry/Open Output 已接线；导入失败保留旧 Catalog，搜索、组合过滤、stable selection owner 和 Grid/List metrics 已接线。
- Source Import Retry 已保留完整 request snapshot；Running Cancel 已接线并在停止 worker 后清理 fresh stage、drop intent、retry snapshot 和未提交资源历史，旧 Catalog/selection 继续可用。资源导入与场景 authoring 保持独立。
- Inspector 已接线无选择 EmptyState、对象摘要、Mixed、真实 document dirty 驱动的 Modified Badge、2D/3D Transform Reset 和绑定 stable ID 的行内校验错误；Timeline 已接线固定 frame slot、刻度、playhead、hover time、event marker 和 selected frame summary；Output 已切换为有界结构化 DataGrid，并支持稳定目标详情与定位。

仍明确未闭环：

- 标准 `glfwSetDropCallback` 只在最终 release/drop 时回调，不提供 drag-enter/drag-over；因此当前不能宣称真正的系统拖动 hover 或 `DragOver` 预览。GLFW 已覆盖 Windows/X11/Wayland ingress，但 Linux 构建、运行和产品拖放证据仍未完成，不需要另写 Tina Linux FileDrop adapter。
- Editor 私有 `DropOverlay` 已接入 release 后的 `Accepted`、`Processing`、`Committed`、`Rejected`、`Failed` 状态，复用现有 SourceImport phase/progress、Catalog commit 和 Sprite2D 创建结果；覆盖层只在 release 后出现，不宣称系统 drag-over 预览。
- Hierarchy/Viewport 对象级预选轮廓已接线：Hierarchy 以 stable ID 和 committed materialized row 绘制 hover 边框，Viewport 复用 2D/3D projected bounds 做最小包围盒优先命中，并在 gizmo、marquee、navigation 捕获期间隐藏。Project Assets anchored 右键菜单、Inspector 无选择 EmptyState、Hierarchy insertion indicator、Viewport 状态 overlay、双击按 AssetId 打开和逐 AssetId 导入状态历史也已接线；当前仍缺这些状态在目标视觉矩阵中的完整产品证据。
- Dark/Light、100/150/200% DPI、跨 GPU、UIA、Explorer 人工拖放和完整 product gate 证据尚未完成；因此本任务保持 `InProgress`，不能标记 `Implemented`。

因此，下一阶段应优先补“视觉呈现和状态反馈”，而不是再增加一套基础 Button、Card、Chip 或 Accordion。

## 3. Tina Studio Compact 视觉原则

### 3.1 层级

继续使用现有 graphite 中性 surface、teal selection/focus、coral destructive action 的方向，但明确层级：

```text
应用背景
  -> Dock / Viewport surface
    -> Header / Toolbar surface
      -> Selected / Focus / Hover state layer
        -> Dialog / Popup / Drop overlay
```

普通信息使用 primary/on-surface；warning/error 只表示真实异常。不要通过把所有标题、Badge 和普通状态染成高饱和色来制造层级。

### 3.2 密度与尺寸

- Compact 仍是 Editor 默认密度，控件高度只使用少数 Theme metrics 档位。
- 间距继续采用 4 的基础节奏，并优先消费 Theme spacing，不在 Editor 组件中散落 magic number。
- 普通 pane 减少描边，优先用 surface 明度和 subtle Divider 分组。
- 圆角控制在小范围；Dialog、Popup、浮层可以比普通 pane 更明显，不能把每个属性行包成卡片。
- 文本不使用 viewport 宽度缩放；超长文本使用 committed ellipsis，语义层保留完整 UTF-8。

### 3.3 状态优先级

同一节点同时存在多个状态时，语义顺序固定为：

```text
Disabled / Invalid  >  Selected  >  Focused  >  Pressed  >  Hovered  >  Normal
```

状态层必须能通过颜色以外的方式辨识，例如选中边条、图标、轮廓、文本或 semantics。支持 reduced motion；普通状态动画只做
opacity、短 visual offset 或既有 paint-only transition，不引入连续布局抖动。

## 4. 总体功能矩阵

| 优先级 | 组件或组合 | 归属 | 状态 | 主要价值 |
| --- | --- | --- | --- | --- |
| P0 | `EditorAssetTile` / Project Assets visual upgrade | Editor 私有组合 + 可能的 VirtualGrid presentation 扩展 | `InProgress` | 让资源浏览从文本列表变成可扫描的工作流 |
| P0 | `EditorDropOverlay` + File Drop ingress | Editor 私有组合 + Platform/Runtime 窄事件 | `InProgress` | release 后反馈并进入现有 SourceImportService；drag-over 仍受 GLFW callback 能力限制 |
| P0 | `UIActivityIndicator` / `ProgressRing` | 先 Editor 私有组合，复用需求成立后公共化 | `InProgress` | 表达未知时长的导入、Catalog reload 和 Build |
| P0 | `UIInlineCallout` / `InfoBar` | 公共 UI 候选，首个消费者为 Editor | `InProgress` | 持续显示错误、警告和可恢复操作 |
| P1 | `UIEmptyState` | 组合 profile | `InProgress` | 统一空项目、无结果、无选择状态 |
| P1 | `UIBreadcrumb` | Editor 私有组合 | `InProgress` | 展示资源路径、Prefab 层级和当前文档位置 |
| P1 | Start Center | Editor 私有页面组合 | `InProgress` | 首屏提供新建、打开、最近项目和品牌识别 |
| P1 | Hierarchy item presentation | Editor 私有组合，复用 TreeView | `InProgress` | 类型图标、状态角标、引导线和拖放插入反馈 |
| P1 | Inspector object header / property state | Editor 私有组合 | `InProgress` | 让对象身份、Mixed、dirty 和 Reset 更容易扫描 |
| P1 | Viewport overlay / View Gizmo | Editor 私有组合 + 现有 viewport state | `InProgress` | 强化画布定位、导航和选择反馈 |
| P2 | Timeline visual pass | Editor 私有组合 | `InProgress` | 时间刻度、播放头、event marker 和轨道层级 |
| P2 | Output filters / Status task strip | Editor 私有组合 | `InProgress` | 让日志、任务和运行状态可筛选、可定位 |
| P2 | Grid/List density switch | Editor 私有组合 | `InProgress` | 适应缩略图浏览与大量资源扫描两种工作方式 |

## 5. Project Assets UX Upgrade

Project Assets 是本路线图的第一优先级。它同时承载资源发现、导入入口、预览选择、Asset Inspector 和 Node 绑定，是视觉和
生产力收益最高的区域。

### 5.1 目标布局

```text
Project Assets                         [23 / 42] [Search] [Grid/List] [+]
[All Types]
┌──────────────┐ ┌──────────────┐
│   thumbnail   │ │  type icon    │
│               │ │               │
├──────────────┤ ├──────────────┤
│ player.png    │ │ enemy.mesh    │
│ Texture2D  ✓  │ │ StaticMesh    │
└──────────────┘ └──────────────┘
selected asset summary / import status / complete AssetId
```

目录面包屑、搜索、过滤、网格/列表视图是同一资源浏览状态的不同呈现，不建立互相独立的 selection owner。窄 Dock 中优先保证
当前选择、Import 和 Open 命令可达；低频操作进入 overflow menu。

### 5.2 `EditorAssetTile` 视觉契约

一个资源项至少包含以下固定区域：

1. **Preview region**：Texture/Sprite 使用真实缩略图；Mesh、Audio、World、Prefab、TileMap、Animation 使用类型图标或
   后续生成的预览图。资源尚未可用时使用稳定的类型 fallback，不绘制伪造的成功预览。
2. **Primary label**：显示 display name，绘制阶段可 ellipsis，数据源和 semantics 保留完整 UTF-8。
3. **Secondary label**：显示 `Texture2D`、`StaticMesh`、`AudioClip` 等 kind；不把完整 AssetId 挤进格子。
4. **Status slot**：Importing、Ready、Warning、Error、Missing 等状态使用图标或小型 Badge；状态文本通过 Tooltip/semantics
   提供完整说明。
5. **Selection chrome**：Selected、Focused、Hovered、Pressed 使用同一套 Theme state layer；selected item 仍由
   VirtualGrid 的真实 selection state 驱动。

建议提供 `Compact tile` 与 `Large preview` 两档，而不是连续滑块。两档都必须使用稳定的 `aspect-ratio`、min/max 和固定文本区高度，
避免缩略图或动态文案改变网格行高。

### 5.3 数据流与所有权

```text
CatalogSnapshot
  -> ProjectAssetBrowserModel
  -> stable item descriptor
  -> VirtualGrid materialized item presentation
  -> optional AssetId image resolve/pin
  -> committed UI paint / DisplayList
  -> Asset Inspector / document open / Node assignment
```

- `ProjectAssetBrowserModel` 继续唯一拥有排序、过滤、稳定选择和 Inspector snapshot。
- UI item 只保存 Tina-owned 的 AssetId/预览 metadata，不保存 AssetLease、GPU handle 或 native path owner。
- 缩略图 resolve 通过现有 root-scoped resolver 和 frame pin 链路完成；缺失或 kind mismatch 只影响本次预览，不修改 authoring bytes。
- VirtualGrid 继续负责可见窗口、固定 item pool、滚动、键盘/Gamepad、selection 和 UIA semantics。
- 若公共 `UIVirtualGridViewItemDescriptor` 增加辅助文本、图标/图片 metadata 或 status presentation，必须单独完成
  header-isolation、容量验证、UIA 语义和 Runtime facade 审查；不得直接把 Editor 类型放进 `include/tina/ui`。
- 在公共契约确认前，Editor 可以先用固定预算的私有 presentation profile 验证视觉和交互，但不能复制第二套虚拟化状态机。

### 5.4 资源项状态

| 状态 | 进入条件 | 视觉 | 交互 |
| --- | --- | --- | --- |
| `Empty` | 无 Catalog 或筛选无结果 | EmptyState | Import、清除过滤或打开项目 |
| `Ready` | Catalog descriptor 已提交 | 缩略图/类型图标 + kind | 单击选择、双击打开、右键菜单 |
| `Importing` | 当前 AssetId 的 SourceImportService 状态为 Queued/Preparing/Copying/Cooking/ReadyToCommit | 指示器 + phase label | 禁止重复启动同一事务，可取消时显示 Cancel |
| `Missing` | AssetId 引用失效或预览资源不可用 | 类型 fallback + warning | 打开 Inspector、定位依赖、刷新 Catalog |
| `Warning` | 可恢复验证问题 | warning badge + tooltip | 查看详情、重试或忽略 |
| `Error` | 当前 AssetId 所属导入或 Catalog commit 失败 | error badge + InlineCallout | Retry、查看 Output、保留旧 Catalog |
| `Selected` | VirtualGrid committed selection 命中 | teal selection layer + focus ring | 更新 Inspector 和绑定命令 |

### 5.5 搜索、过滤和目录

- 增加 Project Assets 专用 SearchField，按 ASCII case-insensitive name/kind 匹配；未来可扩展 path/tag，但不在首切片混入全文索引。
- `All/2D/3D/Media` 继续使用 SegmentedButton；过滤变化只重建 browser index 和 VirtualGrid data source，不丢 stable selection 恢复能力。
- `UIBreadcrumb` 只标识当前 Catalog 来源，不承担目录导航；旧的 `Assets -> Imported -> Images` 文件夹树已移除，资源区直接把完整
  Catalog 投影为 Grid/List。长文本使用 ellipsis，Tooltip/semantics 提供完整值。
- Grid/List 切换只改变 item presentation 和固定 metrics，不改变 logical item order、key 或 selection。

### 5.6 右键和快捷操作

资源项右键菜单建议包含：

- Open / Open in Inspector
- Reimport
- Locate Source
- Copy AssetId
- Copy Source Path
- Reveal Dependencies
- Delete / Remove from intended set（按资源状态区分）

菜单命令必须绑定 stable AssetId 或 stable logical row，不依赖当前 Inspector 选择；删除和丢弃动作仍使用 Dialog 确认。

当前实现已接入 Project Assets anchored context menu：Open、Open in Inspector、按选中 AssetId 对应 source mapping 的 Reimport、Reveal Dependencies
和带确认 Dialog 的 Remove from intended set 均复用 stable `AssetId` 与既有 SourceImport 事务。Locate Source、Copy AssetId、
Copy Source Path 保持显式禁用，因为当前平台层尚未提供安全的文件 reveal/clipboard adapter；未伪造 shell 或剪贴板行为。

## 6. 外部文件拖入与导入反馈

### 6.1 当前事实与缺口

当前源码已有 Hierarchy Node drag-and-drop，且 Windows/GLFW 已实现 Tina-owned OS `FileDrop` release 事件。外部文件拖入
已经沿以下窄数据流进入现有导入事务：

```text
Windows/GLFW native file-drop callback
  -> Tina-owned Platform FileDrop event
  -> bounded PlatformFrameView batch
  -> Runtime primary-window forwarding
  -> Editor drop target / validation
  -> EditorSourceImportService
```

GLFW 3.4 已在 Windows HDROP、X11 XDND、Wayland `wl_data_device` 内部统一处理 OS 文件释放；Tina 只消费
`glfwSetDropCallback` 形成的 UTF-8 path batch。标准 callback 只有最终 release/drop，没有 drag-enter/drag-over，
所以当前可实现 release/importing/rejected/failed 反馈，但不能伪造真正的系统拖动 hover。Linux X11/Wayland 的构建、运行和产品
拖放证据仍需独立门禁；公共头只出现 UTF-8 path batch、source sequence、capacity 和取消/失败语义，不出现 `HWND`、GLFW 类型或 shell command。

### 6.2 Drop overlay 状态

| 阶段 | 覆盖层 | 行为 |
| --- | --- | --- |
| `Idle` | 不显示 | 正常浏览 |
| `DragOver` | 暂不显示（GLFW callback 没有 hover 事件） | 不能伪造系统拖动预览 |
| `Accepted` | 顶部状态 overlay | release 已入有界队列，等待目标验证 |
| `Rejected` | error outline | 说明不支持扩展名、越界路径或 batch 超限 |
| `Preparing` | 顶部状态 overlay + ActivityIndicator | 不修改现有 intended set |
| `Copying/Cooking` | phase label + 进度指示 | 复用现有后台事务；禁止重复提交 |
| `ReadyToCommit` | subdued success state | owner thread 下一安全帧提交 Catalog |
| `Committed` | Snackbar + selection refresh | 显示成功数量和可撤销/定位动作 |
| `Failed` | InlineCallout | Retry、Open Output、保留上一 Catalog |

Drop target 不应无差别覆盖整个窗口。Project Assets 与 active viewport 接受同一套 Source Import 支持集；成功后只把资源
导入 Project Assets/Catalog，不创建或修改任何场景节点。拖入 Hierarchy、Inspector 或窗口其他区域时，必须显示明确拒绝反馈，
不得静默吞掉 OS drop。

### 6.3 复用现有导入安全规则

拖入路径必须直接复用 Source Import service 的既有规则：strict UTF-8、absolute path、containment、扩展名和大小限制、批内去重、
4096 unit 上限、外部媒体安全复制、glTF/recipe 相对依赖拒绝、同名内容复用/冲突后缀、fresh-stage、Catalog 原子 commit 和失败清理。
Drop UI 不得另写一套复制、cook 或路径校验逻辑。

## 7. 其他 Editor 区域的视觉升级

### 7.1 Start Center

无项目启动时显示可操作的 Tina Start Center，而不是营销 Hero：

```text
Tina Studio
[New Project] [Open Project] [Import Files]
Recent Projects
```

可包含 Tina 标识、最近项目路径、失效项目清理和“启动时显示”选项。品牌资产只出现在 Start Center、About 和 EmptyState，
不铺满日常工作区。

### 7.2 Hierarchy

- 使用 Node kind icon、稳定的层级引导线和 selected accent edge。
- Hover 时显示可见性/锁定等低频快捷入口，但不改变 TreeView row 的固定高度。
- 拖放时绘制上插入、下插入、成为子节点三种明确 insertion indicator。
- Prefab、只读节点、缺失资源和不可编辑状态使用 status icon/semantics，不仅改变文字颜色。
- 复用 TreeView selection、focus、keyboard、Gamepad、UIA 和 stable ID，不复制树状态机。

### 7.3 Inspector

Inspector 顶部增加对象摘要：图标、名称、Node kind 或 asset kind。属性行增加：

- Modified/dirty indicator；
- Mixed value 专用显示；
- hover 时出现的 Reset action；
- 错误输入的下一行 helper/error text；
- AssetId、路径等技术值的等宽字体；
- X/Y/Z 轴的克制颜色，并与 Viewport gizmo 保持一致。

无选择时使用 `UIEmptyState`；不再保留大量空白或无上下文的 Identity 行。PropertyRow 仍是 Editor 私有固定预算组合，不新增
通用 Form/Card 层级。

### 7.4 Viewport

- 右上角增加可点击的 3D View Gizmo，复用现有 Perspective/Top/Front/Right/Back/Left/Bottom view state。
- 画布角落使用轻量 Surface 显示 workspace、camera、zoom、grid/snap 和 selection count；可由 View 菜单隐藏。
- Hover/Selected 对象使用预选/选中轮廓；gizmo、Inspector 轴色和 marquee 色保持一致。
- Grid 随 zoom 分级淡出；拖入资源时显示可放置位置和轻量预览。
- 不恢复永久 footer，不把诊断信息染满画布。

### 7.5 Timeline、Output 与 Status Bar

- Timeline 增加时间刻度、播放头、hover time indicator、event marker 色彩和 selected frame summary；固定 frame slot 尺寸不随动态文案变化。
- Output 增加 All/Info/Warning/Error filter、计数 Badge、清空命令、等级图标和可展开详情；双击可定位资源或文档对象。
- Status Bar 分区显示 selection、dirty、后台任务、Catalog 状态和可选 FPS/frame time。后台任务使用 ActivityIndicator，
  不用不断改变整条 Status Bar 的背景色。

当前实现中，Timeline hover 只命中 committed frame button，并在 panel/workspace 切换时清理；动态标签不会改变固定 frame slot。
Output DataGrid 使用 bounded history 的稳定 sequence 作为 row key，过滤只重建固定索引映射；详情展示完整消息和稳定目标，Locate/
双击只消费捕获时的 AssetId、DocumentKey 或 scene stable ID。Status Bar 已分区发布 selection、document dirty、Import task、Catalog、FPS
和 frame time。等级图标当前使用 DataGrid 的 ASCII textual marker，保持 strict UTF-8/ASCII 和现有公共 cell descriptor，不为 Editor 扩大公共 UI ABI。

## 8. 公共 UI 与 Editor 私有组件边界

| 能力 | 首选落点 | 进入公共契约的条件 |
| --- | --- | --- |
| AssetTile | Editor 私有 presentation profile | 第二个产品消费者需要同一虚拟 item schema，且容量/UIA/图片 resolver 规则已冻结 |
| EmptyState | 组合 profile | 先在 Editor 私有复用；若 Showcase/Runtime 也需要同一语义再公共化 |
| ActivityIndicator | 组合 profile或公共 recipe | Editor、Catalog、Build 至少两个真实消费者；明确 reduced-motion 和固定预算 |
| InlineCallout | 公共 recipe 候选 | 需要持续 action、semantics live region、dismiss 和固定容量契约后再冻结 |
| Breadcrumb | Editor 私有 | 证明多个 Editor document/asset consumer 有共同路径语义后再公共化 |
| DropOverlay | Editor 私有 | 永远不应成为通用 OS file manager 控件；只消费 Runtime forwarding 能力 |
| VirtualGrid item presentation | `Tina::UI` 窄扩展候选 | 先完成公开 descriptor、图片/图标 metadata、容量和 UIA 设计审查 |
| FileDrop event | Platform/Runtime 窄 SPI | GLFW Windows/X11/Wayland ingress、sequence、capacity、取消和 shutdown 语义明确；三平台产品证据仍需分别收口 |

不建议新增 `Card`、`Chip`、`Accordion`、第二套 `Widget ABI`、任意 GPU callback、玻璃模糊或通用资产数据库。现有 Surface、Badge、
CollapsibleSection、Image/NineSlice 和 Theme roles 已经覆盖大多数纯视觉需求。

## 9. 实施切片

### Slice A：视觉基础与空状态（P0/P1）

- 统一 Editor EmptyState、InlineCallout、ActivityIndicator 的视觉 profile。
- 加入 Start Center、无选择 Inspector 状态、无结果 Project Assets 状态。
- 为 Hierarchy/Inspector/Viewport 明确 icon、status、selection 的 token 和 semantics。
- 只使用现有 Element/Theme/Motion，不先扩大公共 API。

### Slice B：Project Assets presentation（P0）

- 冻结 AssetTile 的 descriptor 字段、preview fallback、status 和固定 tile metrics。
- 先实现 Texture/Sprite 缩略图、kind icon、primary/secondary label、selection 和 import/error 状态。
- 保留 VirtualGrid 的 logical order、stable key、selection、scroll 和 UIA；禁止 item presentation 自建虚拟化。
- 评估 `UIVirtualGridViewItemDescriptor` 窄扩展；若无法证明公共复用，先保持 Editor 私有。

### Slice C：文件拖入与导入状态（P0）

- 冻结 Tina-owned FileDrop event 与容量/sequence/取消语义。
- 复用 GLFW 3.4 的跨平台 ingress；Windows 产品证据已接入，Linux X11/Wayland 构建、运行和真实拖放证据按独立门禁推进。
- DropOverlay 只做目标命中、反馈和 intent forwarding；校验、复制、cook、commit 全部复用 SourceImportService。
- 覆盖空项目、临时项目、已有 Catalog、重复文件、非法 batch、取消、失败和旧 Catalog 保留。

### Slice D：工作区对象语言（P1）

- Hierarchy type/status icon、拖放 insertion indicator、Inspector object header/Mixed/dirty/Reset、Viewport View Gizmo/overlay。
- Timeline、Output 和 Status Bar 统一任务反馈。

### Slice E：密度与视觉验收（P2）

- Grid/List density switch、breadcrumb、缩略图大小两档、窄 Dock 行为。
- Dark/Light × Compact、100/150/200% DPI、不同窗口尺寸和 reduced-motion 视觉矩阵。

## 10. 容量、失败与可访问性要求

- 多节点 profile 使用 `UIElementBuildTransaction`，精确声明 node/text/canvas/behavior 预算，失败整棵回滚。
- VirtualGrid materialized item pool 必须在 Create/绑定时预留；稳态滚动、筛选和 thumbnail 状态更新不得隐式增长 supplied PMR。
- 图片 resolve、FramePin 或 DisplayList 容量失败时放弃当前候选，不提交半份 paint；旧 committed snapshot 保持有效。
- Catalog reload、Source Import、Drop validation 和 temporary project cleanup 必须保持现有原子事务，不由 UI 复制业务状态。
- 每个 icon-only 命令有 accessible name 和 Tooltip；AssetTile 发布完整 name/kind/status/position semantics。
- DropOverlay、Callout、Snackbar、Dialog 的 live-region、focus restore、Escape、keyboard/Gamepad action 要复用现有 UI route。
- reduced-motion 关闭旋转/位移动画时，仍保留静态状态差异和进度语义。

## 11. 验收标准

### 11.1 结构与行为

- Project Assets 可在 Grid/List 两种 presentation 下保持相同 stable key、logical order、selection、Inspector 和 Open 行为。
- 单击、双击、键盘、Gamepad、UIA action、右键菜单和拖入入口不会建立平行业务状态。
- 导入成功后 Catalog、Browser、preview binding、Inspector 和 status 在安全帧按既有顺序更新。
- 导入失败、取消、CatalogReloadBusy 或 preview resolve 失败时，旧 Catalog、旧 selection 和旧 committed UI 仍可用。
- 文件拖入非法目标、非法路径、重复 batch、超容量和不支持扩展名均给出可理解的反馈，不静默吞事件。

### 11.2 视觉与可读性

- Dark/Light × Compact 下，Normal/Hover/Pressed/Focused/Selected/Disabled/Warning/Error/Importing 都有明确且不只依赖颜色的差异。
- 100%/150%/200% DPI 和窄/宽窗口下，文字、图标、缩略图、Badge、Tooltip、Overlay 不重叠、不裁剪、不改变固定命令位置。
- Project Assets 缩略图不拉伸；缺失预览、透明图片和非图片资源都有稳定 fallback。
- EmptyState、Callout、ActivityIndicator、DropOverlay 在真实工作流中不会遮挡重要命令，也不会永久占据不必要的 pane 高度。

### 11.3 证据

- 受影响 UI 单元测试和 Runtime facade 测试直接运行 GoogleTest executable。
- Editor 使用一次完整大功能闭环后的集中 build、EditorApp gate、必要的 2D/3D smoke 和视觉 capture；不按每个小视觉切片反复构建。
- 视觉证据至少包含 Project Assets ready/empty/importing/error/drop-over、Inspector no-selection/selected/mixed、Hierarchy drag target
  和 Viewport overlay。
- 记录 capacity high-water、steady-state PMR allocation delta、thumbnail resolve/pin、drop batch count、import phase/rollback 与 UIA semantics。
- 文档收口后检查 `git diff --check`、UTF-8、公开头第三方 token 和链接有效性。

## 12. 待确认决策

Windows 首个 FileDrop ingress 已不再是待确认项：有界 path batch 进入 `PlatformFrameView`，再由 Runtime
primary-window capability 转发给 Editor。GLFW 3.4 已明确提供 Windows/X11/Wayland 的 OS ingress；当前缺口是 Linux
构建、运行和产品证据，以及标准 callback 不具备 drag-over 的能力边界。以下剩余事项在实现对应公共契约或产品能力前仍需维护者确认；确认前不建立
占位 API，也不修改 Accepted ADR：

1. `VirtualGridView` 是否接受窄的 item presentation 扩展，还是先由 Editor 私有 profile 验证。
2. 缩略图当前优先来自 Cooked Texture/Sprite；后续是否引入 Editor cache 或独立 thumbnail artifact，以及其缓存和失效 owner 是谁。
3. Recent Projects 的策略已由实现确定：Windows 使用 `%APPDATA%/TinaEditor/settings`，Linux 依次使用
   `XDG_CONFIG_HOME/TinaEditor/settings` 或 `$HOME/.config/TinaEditor/settings`；最多 10 条 canonical project root，
   同路径去重，失效路径在读取/打开时移除。后续只需补跨重启与路径隐私的人工验收记录。
4. AssetTile 的 Grid/List 两档最终视觉 metrics 与 Project Assets 在 200% DPI 下的最小可用宽度；当前已使用真实 preview/icon
   presentation，逻辑 metrics 仍为约 132x72 / 280x30 logical px。

## 13. 明确不在本轮范围

- 不新增 Card/Chip/Accordion 作为纯样式控件。
- 不实现完整通用资产数据库、网络资产、Bundle/Patch 或 Asset cache/LRU；这些仍由资源路线维护。
- 不引入玻璃模糊、backdrop blur、复杂粒子背景、大面积渐变或装饰性 orb。
- 不让 UI 调用 bgfx、GPU callback、native file dialog 或 AssetSystem owner。
- 不把 Start Center 变成营销 Landing Page；第一屏仍应优先服务真实 Editor 工作流。

## 14. Definition of Done

`EDITOR-UI-UX-001` 只有在以下条件全部满足后才能从 `Planned/InProgress` 转为 `Implemented`：

1. Project Assets visual upgrade、EmptyState、导入状态反馈和至少一项完整的工作区对象视觉升级已闭环。
2. FileDrop 若进入范围，Platform/Runtime/Editor 三层契约和 Windows 产品证据已完成；Linux 状态单独记录，不把未做的平台写成完成。
3. 所有新增公共契约都有 header-isolation、容量/失败测试和必要的 Runtime/UIA facade 证据；纯 Editor 组合不污染公开 UI ABI。
4. Dark/Light、DPI、窄窗口、reduced-motion、键盘/Gamepad/UIA 和错误回滚证据齐全。
5. [Editor 2D / 3D](editor-2d.md) 只同步已经实现的事实；本文仍保留提案和验收记录的边界。
