# Editor 2D / 3D

## 产品场景

Editor 的当前闭环同时覆盖 schema-v4 World2D snapshot (448-byte named entity records)、schema-v4 Prefab (208-byte named node records)、TileMap schema-v3 root +
TileMapChunk schema-v1 payload family，以及 SpriteAnimationClip schema-v2（含 per-frame notify events 和
Timeline event marker authoring）。Hierarchy/Inspector/Timeline 把一次
用户意图提交为一个 authoring revision，Undo/Redo 切换已经验证的 revision，Preview 直接把当前 canonical bytes
交给对应 Runtime parser 与 Scene instantiate。工具不能绕过 `AssetFormat` 写半合法数据，也不维护 editor-only 或旧
schema 兼容格式。

独立 `Tina::Editor` target 提供 `World2DAuthoringDocument/File`、`World3DAuthoringDocument/File`、
`TileMapAuthoringDocument/File`、`SpriteAnimationAuthoringDocument/File`、`Navigation2DAuthoringDocument`、
`Fx2DAuthoringDocument`，以及项目根模型与空项目目录创建 API；独立
`Tina::EditorApp` 负责桌面组合，正式产品 target `tina_editor_desktop` 输出 `TinaEditor.exe`，其 `main()` 只负责
调用应用模块。2D Inspector 编辑 Position X/Y、Rotation Z（度）与 Scale X/Y；3D Inspector 编辑完整
Position/Rotation/Scale XYZ。viewport 多选时各字段独立显示 `Mixed`，显式 Apply 只解析用户给出具体数值的字段，
`Mixed` 字段按 `std::nullopt` 保留每个对象自己的 canonical 值。一次多对象 Apply 只调用一次 active document
`replace()`，no-op 不发布 revision、command counter 或 dirty 状态。Transform Header 的 Reset 对 2D/3D 使用对应 identity TRS 并复用同一提交路径；Inspector Header 的 Modified Badge 读取真实 document dirty，校验错误在下一行绑定当前 stable ID，切换对象后清除。`Apply Transform`、`Move X +1`、viewport transform
gizmo、Undo、Redo 都接到 active document，每次成功 canonical command 后从同一份 bytes 实例化新的 `Scene::World`。
2D Camera/Sprite 与 3D PerspectiveCamera/Mesh preview 都由同一个
World/binding 驱动，不维护平行的 UI 模拟状态，也不把默认 proxy 冒充已解析的 Catalog 产品资源。

Editor 只暴露单一 Node authoring 模型。`world2DNodeTemplateRegistry()` 注册 `Node2D`、`Sprite2D`、
`AnimatedSprite2D`、`Camera2D`、`PointLight2D`、`ShadowOccluder2D`，3D registry 注册 `Node3D` 与 `Mesh3D`；
Hierarchy 创建器、Hierarchy Kind 和 Inspector 都读取同一词表。AssetFormat 的 `World2DEntityDesc` optional payload
与 Prefab 的 mesh/material 字段只是 current-schema wire 表示，不暴露为可挂载、可移除的组件 API。分类必须精确映射到
一个受支持 Node kind；旧式多 payload 混合直接 fail-closed，不用“最具体组件优先”掩盖数据形状。
`EditorNodePropertyOperations` 只编辑节点类型本来拥有的属性，不改变节点类型：`Sprite2D`/`AnimatedSprite2D` 发布
Rendering，`Camera2D` 发布 Camera，`PointLight2D` 发布 Light，`ShadowOccluder2D` 发布 Occlusion，
`AnimatedSprite2D` 额外发布 Animation，`Mesh3D` 发布 Rendering。每个可见区段只包含属性行、Apply 与
`visible/active/autoPlay` Compact switch，不存在 Components Header、Add Component、Remove Component 或兼容菜单。
同类型多选时属性按一致性显示 `Mixed`，未明确填写的字段保留每个节点自己的 canonical 值；一次 Apply 最多发布一条
revision，非法值、未知 stable ID 或类型不匹配保持 document/history 字节不变，no-op 不发布 revision。
`Assign Sprite From Selection` 只更新 `Sprite2D`/`AnimatedSprite2D` 的 Sprite AssetId。Play active 时全部节点属性控件
与其它 authoring 控件一同锁定；成功编辑后 runtime preview 从新的 canonical bytes 重建。

Editor 默认进入无帧数上限的交互模式，由主窗口关闭结束生命周期；自动演示不再默认执行。只有同时显式传入
`--auto-demo` 与 `--frames=<N>` 且 `N >= 70` 才运行自动 authoring 流程，重复 `--auto-demo`、缺少 `--frames`
或帧预算不足都拒绝启动。正式短 smoke 统一使用 70 帧（Create Node picker 的打开与 Confirm 各占一帧）；新增阶段会选择内置 PointLight2D、展开 RGB Color Picker，
等待 Inspector 自动滚动和目标控件几何 committed 后保留完整绘制帧，最后一帧继续确认最终 Hierarchy selection 已提交。
单独传 `--frames=<N>` 只运行有限帧普通模式，其退出门禁只检查生命周期、UI、viewport、preview、document 与有限值等
通用不变量；自动编辑目标与选择都从当前 hierarchy 的 stable ID 动态解析。

## 开发与验证节奏

Editor 采用完整大功能闭环后统一验证的节奏。大功能内部的小功能、小细节和连续源码切片只做源码/API 阅读、
定向静态搜索、`git status` 与 `git diff --check`；不得新增或修改 Editor 测试，也不执行 configure、build、
GoogleTest、sample、smoke、Visual 或平台 gate。大功能的交互、状态、错误处理和文档全部收口后，才复用核心
常驻 build tree 集中执行一次受影响 Editor target 的增量 build、仓库已有定向 executable 和必要的最短 2D/3D
smoke。统一 gate 的失败集中修复后只重跑失败或直接受影响项，不按每个小修复重复整套验证。详细命令与证据边界
见[测试与验证](testing.md#editor-开发与验证节奏)。

### 第一批 E1/E2/E3 状态

TileMap Inspector 已提供固定容量 Tile Palette：数据来自当前 resident Tileset 的 cooked payload，选择 tile 不产生
document revision，且无 Tileset 时 Paint 控件禁用。Editor 私有 settings carrier 已接入启动读取和退出原子写入，保存
SplitView fraction/可见性、Bottom Panel 与 snap enabled；主题和 snap 步长 Preferences UI 尚未承诺完成。Recent Projects
在统一 Catalog switch 提交点记录最近 10 个 project root，并在 Start Center 以固定按钮行呈现；无效路径从列表移除。
上述能力没有新增自动测试；本批统一证据已完成：`tina_editor_tests` 112/112、`tina_editor_app_tests` 23/23，
`tina_sample_2d --frames=300 --frame-delay-ms=0` 与 `tina_sample_3d --frames=30 --frame-delay-ms=0` 均
`status=ok`。跨重启 settings、TileMap 多次选择/绘制、Recent Projects 失效移除及文件对话框重复打开仍需人工验收。

当前 Editor application 的 retained UI 布局已完整铺开：带 `File/Edit/View/Help` 的 Command Bar、按需出现的 Document Tab strip、
Hierarchy/Project Assets Left Dock、active Viewport、可滚动 Inspector
（Identity/Transform/节点专属属性/Hierarchy/TileMap）、可折叠底部面板和 Status Bar。
Command Bar 中央的 `2D/3D` 是 workspace selector，`View > Workspace` 提供同一命令的菜单入口；Document Tab strip
只呈现从 Project Assets 实际打开的 scene/Catalog document，Undo/Redo/Save 已并入 Command Bar。内建 World2D/World3D/TileMap/Animation
session 都是 workspace/context 的内部状态，不再重复显示成顶层文档标签；没有可关闭的项目文档时关闭按钮也折叠。
2D Viewport Header 以 `Scene/TileMap` 切换当前 authoring context。底部面板默认收起，Status Bar 的
`Animation` / `Output` / `Layout` 按钮用于打开或切换面板，再次点击当前按钮会收起；Animation authoring 位于
Animation 面板，Output 以有界三列 DataGrid 显示 `Level | Context | Message` 历史；Layout Debugger 使用 committed
layout snapshot 提供节点树、authored/resolved 布局参数、几何与 basis 详情、界面拾取和非侵入式 bounds overlay，
并在所有构建配置中可用。Hierarchy 与 Inspector Header 分别提供向外收起按钮，
`View` 菜单中的 `Left Dock` / `Inspector` Check 项用于恢复或再次隐藏；收起前保存用户最后拖拽比例。
根使用四个连续 band；Workspace 由三个嵌套 `SplitView` 组成：`Left Dock | Main`、`Center | Inspector`、
`Viewport | Bottom Panel Host`。三个 splitter 都使用第一方 Pointer Capture/RangeInput 状态机，可直接拖动调整 Left Dock、
Inspector 与已打开的底部面板；Editor Theme 让 10 logical px 命中区本身完整绘制为分隔条，不再在热区两侧保留
透明边距。任一 pane 收起时，其 splitter 同步 `Collapsed`，SplitView fraction 同步落到 `0` 或 `1`，中央 Viewport
立即获得全部释放空间；重新打开时恢复先前 fraction，pane 最小尺寸仍由 SplitView
约束。Button、TextEdit、Tab、
List/Tree row、Status Bar 和 spacing/padding 读取 `UITheme` metrics，不再由 Editor 局部颜色 token 和固定控件高度
定义产品外观。`updateUI()` 从上一轮成功提交的 viewport/root `worldRect` 计算
`RenderNormalizedViewport`，因此窗口变大时 viewport 与中间工作区共同增长，Dock/Timeline 由 SplitView 调整，
world pass 下一帧跟随新的布局；首帧在 committed rect 可用前不提交 world，避免用 `1280×800` 写死区域或全屏闪烁。
Windows GLFW 将原生 window/Pointer 坐标按 `contentScale` 归一为 logical pixel；Editor 字体层级固定为
14–20 logical px，因此 200% DPI 最大化时布局、文字与命中使用同一缩放。Viewport 只保留一条 context toolbar
与画布，不再发布 footer；当前工具由 toolbar 的选中态表达，缩放只由画布滚轮驱动，正常 Catalog/camera/grid
状态不常驻占用画布高度。
2026-08-20 Tina Studio Compact 集中门禁通过：`tina_ui_tests` 771/771、`tina_ui_uia_tests` 14/14、
`tina_runtime_ui_tests` 145/145、`tina_editor_tests` 114/114、`tina_editor_app_tests` 13/13；2D/3D workspace
`--auto-demo --frames=70` 均 exit 0、`automaticAuthoringStage=57`、`selectionVerified=true`，且各自通过
`capturePrimaryFrameRgba8()` 写出完整 `2560x1600x4` top-left RGBA8 帧并完成像素内容核验。Color Picker
capture 还确认 Color Field 的单一 swatch/hex summary、R/G/B 通道色和 `0..255` 数值标签均完整可见且无裁剪或重叠。
Inspector 浮点字段使用 6 位有效数字的 fixed-capacity、locale-independent presentation，不再显示
`1.000000` 一类无意义尾零；解析、Apply transaction 与 canonical document 数值不受该显示格式影响。

Editor 视觉系统为 `Tina Studio Compact`：它从
`makeModernDesktopTheme(UIColorScheme::Dark, UIDensity::Compact)` 派生 Editor 专属 product theme，并在
`createRoot()` 前绑定，按桌面 authoring 工作台维持高密度控件，不照搬移动端 Material 组件尺寸。Theme 使用
graphite 中性 surface、teal selection/focus、coral destructive action、低对比 outline 与仅用于 modal 的短阴影；
普通命令默认 Tonal，Play 使用 Primary，Save 与 inline Delete/Remove 使用 Outlined，Undo/Redo/Cancel 使用
Text，只有 Dialog 的最终 Delete/Discard 使用 Danger。2D/3D workspace、Scene/TileMap context、Select/Move/Rotate/Scale/Tile、
marquee、Project filter 与 document tab 都使用 `SegmentedButton` role，并通过真实 `setRadioButtonSelected()` 表达 active selection；enabled 只表示
命令是否可执行，不再兼作选中视觉。Segmented 仍复用 RadioButton 的互斥、Focus、Keyboard/Gamepad、UIA 和
`Selected` pseudo-state，不建立 Editor 私有状态机或 backend paint 分支。
Command Bar、Document Tab strip、Dock、Inspector、Timeline 与 Status Bar 的有色容器通过
`UISurface` 的 Filled/Elevated profile authoring，纯布局容器仍保持普通 Panel；Command Bar 与 Viewport toolbar
使用无行为、从 Semantics 排除的竖向 `UIDivider` 区分 workspace/context、transform/snap/marquee/frame 命令组。
EditorApp 另以私有固定预算 recipe 实现 `EditorToolbarGroup`、`EditorPanelHeader`、`EditorSectionHeader`、
`EditorPropertyRow` 与 `EditorSearchField`：`EditorPanelHeader` 使用无圆角、无描边的扁平 surface band，并把 badge、
状态和命令统一末端对齐；`EditorSectionHeader` 以 3 个节点组合标题和可伸缩的 subtle horizontal divider，明确区分
Inspector 的内容段而不再制造嵌套 card。Undo/Redo、Save/Save As、Play/Pause/Step/Stop、transform、scene、viewport、timeline 等熟悉命令
使用 Lucide 1.33.0 SVG 离线 cook 的单色私有 atlas 与 IconButton/Tooltip：cooker 以 4x coverage 栅格化后 Lanczos
缩至每格 36 px，并保留 2 px 内部 gutter 与透明边界，运行时按逻辑 18 px 线性过滤；SVG、manifest、license、生成的 atlas alpha 与 UV metadata 一并提交，
普通 configure/build 不运行 Python、SVG parser 或 runtime vector renderer。IconButton/Toggle 把 image content 直接放在
唯一 Button/RadioButton 节点上；Text Button 的 disabled background 保持与 normal 一样透明，只有图标/文字按
`disabledContentAlpha` 降灰，不再绘制整块灰色矩形。hover/pressed/selected tint 与背景共享同一 retained state，因此图标始终居中于
对应 chrome；选中工具继续由 RadioButton 状态表达，视觉统一采用无 indicator 的 SegmentedButton chrome。Header、68 px property label
column 和 Search icon/TextEdit 统一密集工作台的对齐；SearchField 内层 TextEdit 使用明确的水平 content inset，
空值 caret 也按当前文字行高在输入区域内垂直居中。Hierarchy filter 按 ASCII 大小写不敏感匹配 label，发布匹配项
及其祖先，搜索期间不受 collapsed 分支遮挡，并按 stable ID 保留可见选择。
Inspector 节点属性折叠 Header 使用同一私有 atlas 中的 ChevronRight/ChevronDown Icon node，并按 retained expanded state
切换 visibility，不再显示文本伪箭头。
Transform 使用 Position、Rotation、Scale 三行第一方 Grid：每行以52 logical px label track + `Fr` value track
约束内容，value track 再按 workspace 发布一至三个等权 `Fr` axis cell；每个 axis cell 使用12 logical px 居中的
X/Y/Z label + `Fr` TextEdit，并把整体限制在52..96 logical px。拖动 Inspector 只改变 track 分配，不切换
方向、不重建节点，也不使用宽度阈值。World2D entity 的 Position/Scale 发布 X/Y，Rotation 只发布 Z；
World3D entity 发布完整九轴。Asset Inspector、TileMap document 或无 entity selection 时，Header、向量字段容器与
Apply 一并 `Collapsed`。旧的九行 `UINumberField`、逐轴减号/加号回调与延迟 step 状态已删除，数值仍由 Transform
Header 右侧的 Apply action 统一解析并提交。节点专属属性与 Parent ID 等其他 Inspector PropertyRow 使用
68 logical px label + `Fr` value Grid，单值 TextEdit 最大宽度统一为132 logical px。Sprite Size/Pivot 与
ShadowOccluder Start/End 分别把 X/Y 合并在同一属性行，轴 label 的 horizontal/vertical content alignment 均居中。
节点专属属性按当前 selection/workspace 发布：只有同类型 Node 多选才显示该类型固有的 property section；
`AnimatedSprite2D` 同时显示 Rendering 与 Animation，其余 Node 只显示对应区段。Asset Inspector、TileMap document、
无 Node selection 或多选类型不一致时全部专属 root 收口；workspace/context 切换只改 root visibility，不覆盖用户
保留的内部 expanded state，也不提供改变 Node kind 的 Inspector 入口。
Hierarchy 的 Header、Parent ID 行和 Apply Parent wrapper 只在同一 entity context 发布；TileMap 的 Header、状态摘要与
四个 action row 只在 TileMap document 激活时发布。最终 Asset Inspector 只呈现 Identity 与 asset metadata/dependencies，
Scene node 呈现 Identity、Transform、节点专属属性与 Hierarchy，TileMap document 呈现 Identity 与 TileMap；
Scene 无 entity selection 时只保留 Identity。所有上下文切换都从构建时保存的完整 Grid/root layout 修改
`visibility`，恢复后不会把 PropertyRow 重置为旧 Flex，也不会丢失 IconButton wrapper 的居中布局。
Inspector ScrollView 的内容高度由这些实际可见 section 的 Flow intrinsic height 决定，不再使用固定 1980 px canvas；
`Auto` scrollbar 只在 content extent 真正超过 viewport 时绘制并参与命中。
维护图标映射后使用工作区 Python 执行 `tools/editor_icons/cook_editor_icons.py`，提交
`EditorIconAtlas.generated.hpp/.inc`；CI 或本地审计可追加 `--check` 验证生成物未漂移。cooker 对 SVG element、attribute、
path command 与资源根目录执行 allowlist 校验，避免静默接受脚本、外部引用或未支持的 vector 特性。
Viewport 的 World/Local orientation 与 Snap 状态使用 `World`/`Snap` icon toggle 和 tooltip，不再运行时改写文字按钮；
Inspector 的 Identity 使用固定高度 `EditorPropertyRow` 表达 Name/Kind，说明文字独占整行并使用 ellipsis，
Scene selection 折叠 Asset 专属 metadata/dependency 区域，Asset Inspector 激活时再显式展开，避免空区域挤压字段。
可见 Node 属性中的布尔状态使用 Compact `UISwitch`，继续复用既有 Toggle storage、`setChecked()`、
`setCheckboxAction()` 以及 Focus/Keyboard/Gamepad/UIA action；Status Bar 的选择摘要使用 Accent `UIBadge`，仍由
同一个 `statusSelection_` 节点通过 `setText()` 动态刷新；document/runtime 两段按 0.8/1.2 分配剩余宽度并分别
ellipsis，不引入 Editor 私有控件状态。Create Node picker、dirty-close、Scene Delete 和 About Tina 均持有
`UIDialogParts`，统一通过 `openDialog()/dismissDialog()` 驱动 retained intent，并在后续 UI commit 发布或恢复
Modal focus；不再保存私有 Modal layout/visibility 状态。dirty-close 保留 Save/Discard/Cancel 状态流，其路径编辑区域改由第一方 `UIFormField` 原子构建；Editor 只持有
返回的 TextEdit node，并继续通过既有 text/enabled/focus API 驱动唯一 TextInput 状态，不复制表单行为。
Scene Delete `UIDialog` 使用显式 `560` logical px surface、两行正文与 `Cancel | Delete` action 顺序；scrim 保留
背景上下文但不再压成近黑，默认焦点仍落在 destructive action，避免排版调整改变键盘确认语义。
`Help > About Tina Editor` 使用同一 bounded `UIDialog` profile 显示应用信息；Modal 阻止输入穿透，默认焦点落在
`Close`，关闭提交后恢复到 Help 菜单锚点，不再把 About 内容写入 Status Bar。
Editor 的 authoring feedback 通过公共 `UISnackbarHost` 发布，并同步到用户主动打开的 Output 底部面板：成功编辑在当前 document 可撤销时提供 Undo action，
warning/error 使用独立 tone bar；入退场只用 opacity 与 8 px visual offset，不请求 Focus。Snackbar 在普通 workspace
之后、Modal/Dialog 之前创建，确保 modal z-order 与 barrier 仍优先；message 在固定 surface 内使用 ellipsis，完整
UTF-8 文本仍保留在 polite live-region semantics。Inspector 不再保留 `Authoring` 提示段或
`Move X +1` 调试按钮；Snackbar 负责短时反馈，Output 只保留最多 64 条 presentation history，不成为第二份业务状态。Output 支持 All/Info/Warning/Error 过滤、计数、Clear、选中详情，以及只对真实 AssetId/DocumentKey/scene stable ID 开放的 Locate/双击定位；过滤使用稳定 sequence row key，不从消息字符串解析目标。Panel/Toolbar/Header/Search/Floating/Dialog 使用
6 px 以内圆角和 Raised/Floating/Modal 无模糊 shadow；rounded/stencil descendant clip 与 backdrop/blur 仍不在本轮范围。
`--world2d-path=<UTF-8 path>` 与 `--world3d-path=<UTF-8 path>` 分别配置两个内部 workspace session；已有文件按各自
schema 原子加载为 clean baseline，不存在的路径保留为该 workspace 的新文档 Save target。每个 pinned/Catalog tab
都有固定容量 session，独立持有 document key、strict UTF-8 path、target platform、loaded flag 与完整 canonical
baseline。主 Command Bar 不显示可编辑路径：Command Bar 与 File menu 的 Save 只在 active document 已有路径且 dirty 时启用；Save As 对四类可写
document 启用，Asset Inspector 保持只读。Windows 使用系统 native dialog；Linux 私有 adapter 使用 `zenity` 并在缺失时
回退 `kdialog`。World2D 选择 `.tworld`、World3D 选择 `.tprefab`、SpriteAnimation 选择 `.tasset` 文件，TileMap 选择 package
输出目录。取消 dialog 不修改 path、baseline、dirty、tab 或 selection；其他未支持平台返回 `Unsupported`，EditorApp
显示明确失败反馈，不再把主工具栏输入框当作平台 fallback。Command Bar 使用等宽 grow 的左右 region，保持中央
workspace selector 稳定居中并保护右侧 history/save 与 play controls。Document Tab 仅负责用户从项目实际打开的 scene/Catalog document；
内建 pinned World2D/World3D/TileMap/Animation tab 保留 session、dirty 与自动门禁所有权，但 UI 固定折叠。World2D/World3D
由 Command Bar workspace selector 进入，TileMap 由 2D Viewport context 进入，Animation 由 Timeline 承载；没有外部
document tab 时整个 strip 为 `Collapsed`，不会保留空白横栏。
每个可见槽使用 170 px preferred、112 px minimum 和 shrink，未占用及 context-only 槽为 `Collapsed`；每次 refresh
都重新应用完整 slot layout，因此重新打开 Catalog document 可恢复 `Visible`，不会保留旧折叠状态。
两个打开的 document 不得拥有相同文本路径。
2D 的五个字段和 3D 的九个字段都通过显式 Apply 合并为一次 document revision；严格拒绝 trailing text、NaN 和 Infinity，
拒绝时恢复 canonical 字段并保持 document/history/preview 不变。多选 batch 对每个 optional 缺失字段保留原值，并要求
全部 selected stable ID 都存在后才原子发布。2D Rotation Z 总是发布 X/Y 为零的平面 Z quaternion；3D Euler XYZ
合并当前未编辑分量后发布完整规范化 quaternion，并保留 hierarchy、Mesh/Material 与 visibility。GPU preview 同步读取完整 canonical TRS。
`EditorTransformGizmo` 在 preview layer 使用 routed pointer capture，并以固定容量 snapshot 提供 Translate、Rotate、Scale，
支持 World/Local orientation、translation/rotation/scale snapping，以及 2D/3D axis、plane、rotation ring 与 uniform scale
handle。EditorApp 的 group transaction 只捕获 selected transformable roots；同时选中 parent/child 时过滤 child，group pivot
取这些 root 的平均 world position。Move 事件把绝对 delta 应用到临时 `Scene::World` 的 world transform，再按当前 parent
反算 local transform；零 scale、非法 TRS 或会产生 shear 的组合 fail closed。ButtonUp 才以一次 `replace()` 提交全部目标。
PointerCancel、pointer/selection/workspace/document revision 冲突、no-op 或非法 scale 均丢弃临时状态并恢复 canonical
preview，document/history 不变。Status bar 在多选时显示 `N selected | Group pivot`，Inspector 标题显示
`Mixed values`，gizmo 开始、预览、提交与取消都有明确状态反馈。

Hierarchy 每次在 World2D/World3D document 变化后都从 canonical wire item 及其 parent stable ID 重建动态树；
document root 使用非持久化的 UI key，实际场景 Node 直接以 stable ID 作为 key。Header 下只常驻 `Add`、`Duplicate`、
`Delete` 与 `Focus`。拖放到目标行中间 50% 会把源 Node 设为目标子节点，拖到上/下 25% 会在同级执行 before/after
排序；拖到 document root 会回到场景根。跨父级边缘排序、拖入自身 subtree 与无效目标均 fail-closed，no-op 不发布
revision；成功操作只发布一个 canonical revision，并按 stable ID 恢复选择、重建 preview 和 Inspector。
主列表支持双击节点进入 inline rename；名称始终按 UTF-8 codepoint 计数，空名、非法 UTF-8、过长文本和 document
revision 冲突都会 fail-closed，不会销毁当前事件路径。对任意节点右键会打开 Hierarchy context menu，提供 Rename、
Move Up、Move Down、Move to Root 和 Delete；菜单操作直接绑定 stable ID，右键目标不依赖 Inspector 当前选择。
Editor 默认 authoring document capacity 为 128 个 entity/node，Hierarchy materialized window 为 64 项，达到真实容量前
Add 仍保持可用，容量耗尽只拒绝当前创建事务并保留原 hierarchy。
`Add` 先打开第一方 Create Node picker `UIDialog`，列出当前 workspace 的 node template：World2D 为
`Node2D`、`Sprite2D`、`AnimatedSprite2D`、`Camera2D`、`PointLight2D`、`ShadowOccluder2D`，World3D 为 `Node3D`、`Mesh3D`；
选中 template 后 Confirm 以一次 canonical revision 直接创建完整类型节点，因此选择节点类型只消耗一次 Undo，也不存在
先建空节点再追加组件的转换路径。新 Node 默认成为打开 picker 时当前选中 Node 的子节点；选择 document root 时创建在
场景根。picker 固定 active document key、workspace、parent stable ID 与 document
revision，Confirm 前任一项变化即事务安全取消；Cancel/Escape 不修改 canonical document 或 selection，关闭后 focus
恢复到 Hierarchy。`Sprite2D` / `AnimatedSprite2D` / `Mesh3D` 所需 AssetId 优先读取 Project Assets 当前对应 kind 的选择，
否则使用内建 preview asset，因此没有不可达 template。node template registry 同时是 Hierarchy 标签与 Inspector Kind 的
唯一词表，创建为 `AnimatedSprite2D` 的节点在两处也读回 `AnimatedSprite2D`。`Delete` 先打开第一方 `UIDialog`，并固定 active document key、workspace、stable ID 与 document
revision；只有 Confirm 执行原有 subtree 删除，Cancel/Escape/关闭不修改 canonical document 或 selection。Confirm 前目标、
document/session 或 revision 已变化时事务安全取消，重复 Delete/Confirm 不会建立第二条删除流程；Dialog 关闭提交后 focus
恢复到 Hierarchy。Auto-demo 可通过 `--rgba-output=<path>` 与
`--rgba-stage=workspace|color-picker|delete-dialog` 调用
`IRenderDevice::capturePrimaryFrameRgba8()` 写出指定产品阶段的完整 top-left RGBA8 帧；`delete-dialog` 用于确认
scrim、surface、文案与 action，`color-picker` 用于确认 Inspector 的真实 Color Field、preview 和 RGB sliders，
`workspace` 用于确认稳定工作台。结构化结果统一报告 stage、capture 尺寸、字节数和写入状态。Prefab v4 删除最后一个完整 subtree 会在打开确认前拒绝。Select tool 的 marquee 从当前 preview 投影收集候选，
以固定 512 项容量发布按 stable ID 排序的 Replace/Add/Toggle 多选及 added/removed diff，primary stable ID 同步回 Hierarchy；
空结果把 Hierarchy 明确切回 document root，不会用伪造 stable ID 恢复旧 viewport selection。只有 selection 实际变化才推进
selection revision，活动 gizmo 通过该 revision 检测并安全取消。

`EditorPlaySession` 已接入 Toolbar 的 Play/Pause/Step/Stop。Timeline 不再重复提供专用 Undo/Redo，动画文档统一走
Command Bar 或 Edit menu 的 active-document Undo/Redo。按下 Play 时才复制当前 2D snapshot 或 3D Prefab canonical bytes，
`canonicalByteCapacity` 只限制合法快照大小，不在 Editor 空启动时预留整块容量；复制成功后才原子进入 Playing，
以有界 fixed-step clock 驱动隔离 preview，authoring document 不被 simulation 修改。play session 活跃期间锁定 authoring command
与 document tab 切换，仍允许 viewport Focus；Stop 后释放隔离 snapshot 容量并从 canonical authoring document 重建 preview。

普通工作区信息、路径与状态摘要统一使用 Theme primary text；warning/error 只用于真实可恢复异常或失败反馈，
不再把常规信息染成 warning 黄色。

Editor 快捷键使用 frame action mapping：`Ctrl+S` Save、`Ctrl+Shift+S` Save As、`Ctrl+Z` Undo、`Ctrl+Y` Redo、
`Ctrl+D` Duplicate、`Delete` Delete、`Ctrl+1` / `Ctrl+2` 切换 2D/3D、`Ctrl+0` Frame All、`Ctrl+F` Focus Selection、
`F6` Play/Resume、`F7` Step、`F8` Stop。`Escape` 优先关闭 Create Node picker，其次关闭 scene Delete confirmation，
再关闭 dirty-close Dialog，之后才取消 gizmo、marquee、navigation 或停止 Play。
不绑定裸 `Q/W/E/R`，避免 Inspector TextEdit 输入期间误触 viewport tool。

2D workspace 通过 Viewport Header 的 `TileMap` context 激活内建 TileMap session 后，开放 viewport `Tile Paint` / `Tile Erase`
和 Inspector 的 Paint、Erase、Toggle Layer、
Add Tile Layer、Add Object Layer、Cook Preview、Generate Gameplay 与 Bake Navigation。Pointer 坐标通过 committed viewport rect 和 Camera2D 投影换算到
真实 cell；每次点击只发布一个完整 root/chunk revision，空 chunk 自动删除。TileMap Undo/Redo 与 World2D document
history 相互独立；切到 3D 或离开 TileMap document 会关闭 tile tools。新增 Tile layer 会立即成为 active brush layer，
preview 按 root authoring order 提取全部可见 Tile layer，而不是只渲染第一层。Bake Navigation 从当前 resident TileMap
派生 `NavigationGrid2D` v1 canonical payload 和 Cooked artifact；TileMap revision 变化会把 bake 标为 dirty，成功发布通过
fresh authoring overlay 更新项目 active Catalog pointer，不会覆写 Source Import baseline。

SpriteAnimationClip Timeline 在选中帧内显示 event marker，并提供 Prev/Next、Add/Apply/Remove。Timeline 同时发布时间刻度、playhead、只命中 committed frame button 的 hover time 和固定槽 event marker row；切换 panel/workspace 会清理 hover。tag 接受 `0x`
十六进制或标识符，offset 接受 `[0,1]` 小数或百分比；提交统一调用 `setFrameEvents()`，按 offset 稳定排序并作为
一次 canonical revision 参与 Undo/Redo/Cook Preview。tag=0、非有限/越界 offset、每帧64上限或 stale selection
均 fail closed。6 个可见帧槽保持固定 `44 logical px` 宽度，只显示稳定帧号；选中帧的 Sprite、毫秒时长和事件数集中放在
selected-frame summary，长文案以 ellipsis 保持编辑命令不被挤动。选中帧通过 `ButtonPrimary` chrome 表达，其余与空槽
使用 `ButtonOutlined`，不再把 `>` 拼入业务标签。`Fx2DAuthoringDocument` 同样只持有已验证的 canonical v1 payload，提供 bounded replace/Undo/Redo；
当前 EditorApp 尚未提供独立 FX effect graph 或可见专用面板，不能把公共 document API 写成已经存在的图形化 FX 编辑器。

`--catalog-root=<UTF-8 path>` 配置项目 Cooked Catalog；Editor 启动时通过真实 `AssetSystem` 完整打开并校验 package。
普通启动未配置项目时只发布零 entry 的临时 session Catalog，Project Browser 与 2D/3D 资源 preview 都保持为空；
只有显式 `--auto-demo` 才创建并标记 test fixture Catalog，提供自动 smoke 所需的测试 Texture/Sprite/Tileset、
Cube Mesh 与 Material，不把它们伪装为项目内容。Editor 从 canonical
World2D/Prefab/TileMap/SpriteAnimationClip 的 `AssetId` 收集并去重 preview 根资源：2D 由
`Sprite2DBindingRegistry` 持有 Sprite 或
Tileset 的 Texture2D
Lease/GPU/binding，3D 由 `Mesh3DBindingRegistry` 持有 StaticMesh、Material 与共享 Texture2D 的
Lease/GPU/binding；Scene extraction 只取得 packet-local `FrameResourceRef`。项目 Catalog 中缺失或 kind 不匹配的引用
只从本次 preview 过滤，authoring document 与 history 保持不变，不回退到固定 binding key 或彩色 proxy。

Project Browser 直接拥有 Catalog descriptor 的确定性 AssetId 排序索引，并提供 ASCII case-insensitive name/kind 搜索、
All/Images/Models/Scenes/Audio/Animation/Other 类型过滤、stable item key、按 AssetId 恢复的稳定选择，以及 Grid/List 两种固定 metrics 的
`UIVirtualGridView` presentation。Grid 使用 116 logical px 最小 item 宽度与 84 logical px 高度，并保持按 viewport
计算出的响应式列数；即使末行只有一个资源也不再把 item 拉伸到整行。List 使用 320 logical px 最小 item 宽度与
60 logical px 高度；两档只改变 presentation/metrics，不改变 logical order、key 或 selection。
每个 materialized item 现在同时发布 primary label、kind secondary label、status label/status，以及 preview/icon metadata；Texture/Sprite 使用
真实 cooked 缩略图，Grid 把 48 logical px 预览放在名称上方，List 把 40 logical px 预览放在名称左侧；选中背景先于
图片绘制，不会再遮住预览。Mesh/Audio/World/Prefab/TileMap/Animation 使用稳定类型 icon fallback。缩略图 resolve 失败只把本项标为
`Missing`，不改变 Catalog 或 authoring bytes；Importing/Ready/Error 状态现在按稳定 AssetId 保留导入历史：已知 source mapping 的资源逐项显示
`Queued`、`Preparing`、`Copying`、`Cooking`、`Ready`、`Imported` 或 `Error`，新输出在 validated stage 返回后补入历史；未受当前批次影响的资源保持 Ready。
网格下固定高度的 selected-asset summary 保存完整 canonical AssetId，空间不足时仅在绘制阶段以 ellipsis 截断。Project Open 保留
FolderOpen，打开当前 Asset 使用 divider 后的 ArrowRight，两个命令不再共享图形。Project Assets 同时提供 Breadcrumb、Start Center、
无资源 EmptyState、导入 Activity 文本、失败 InlineCallout、Retry/Open Output；Project Assets anchored 右键菜单和按 AssetId 双击打开均已接线，Locate/Copy 类平台能力在当前平台层缺失时显式禁用。
每个 owned descriptor 还保存由 `AssetKind + AssetId` 重新派生的 canonical cooked 相对路径与
完整、按 AssetId 排序的 dependency records，不借用 Catalog snapshot。只读 Asset Inspector 按 active Inspector tab 的
`AssetId` 取得对应 snapshot，以固定 36 px 行高虚拟化显示 dependency kind、AssetId 与 flags，而不是继续跟随 Project
Browser 的临时 selection。Prefab、TileMap 与 SpriteAnimationClip 只按当前 schema 打开到对应 authoring surface；其他
kind 打开只读 Asset Inspector。固定容量 document tab model 以 `(document kind, AssetId)` 去重，支持 pinned/dirty close
保护和 2D/3D workspace 路由。EditorApp 的固定 slot 对每个 Catalog authoring tab 独立拥有 document/history/session；
切换时只 swap 同 kind active owner，重复打开既有 key 只激活而不 reload。dirty 的非 pinned tab 仍可点击 Close，随后
Modal 提供 Save/Save As、Discard、Cancel；Cancel 不改变 tab、document 或 selection，保存失败保留 Modal 与 dirty 状态。
Project Browser 的 Refresh 在下一帧 Render packet 建立前调用 `AssetSystem::reloadCatalog()`，显式携带当前 Sprite/Mesh
registry participant；成功后重建 browser selection 与 preview binding，失败继续使用旧 Catalog。所有路径只写当前
schema，不增加 editor-only wire 或旧资产兼容分支。

长文本显示统一使用框架级单行 `Ellipsis`：document tab、状态栏文档路径与 Project Browser selected summary
直接设置节点 overflow，Project Browser grid 通过 `UIVirtualGridViewStyle::itemTextOverflow` 配置私有 materialized item；
data source 与 Semantics/UIA 始终保留各节点提交的完整文本，不在 Editor 业务层切断 UTF-8 code point。

`EditorProjectWorkspace::Create()` 已提供 owning、move-only 的 project/source/Cooked Catalog root 模型：三个 root 必须是
bounded strict UTF-8 absolute path，Source 与 Catalog 都严格位于 project root 下且彼此不重叠；这里只做 lexical
normalization。`CreateNewEditorProject()` 进一步创建或采用一个既有空 project root，建立默认 `Source/` 与 `Catalog/`
目录，拒绝 symlink/junction/reparse root，核验最终物理 containment，并只按捕获的目录 identity 回滚本次事务创建的目录。
EditorApp 的 Project `New` action 在 Windows 先选择空目录，再调用该 API，写零 entry 的 current-schema manifest，通过
`publishCatalogPackage()` manifest-last 发布，并以 typed validation 重新打开磁盘 package。Project `Open` 要求所选 root
包含物理 `Source/` 与 `Catalog/` 目录，拒绝 symlink/junction/reparse 布局并核验 final containment。New/Open 成功后只排队
workspace，下一安全帧在 Render packet 建立前通过 `AssetSystem::reloadCatalog()` 的 Sprite/Mesh participant transaction
验证并切换 live Catalog。commit 前脏 Catalog document 会阻止切换，其他动态 Catalog tab 在成功切换后失效；Browser
只从 AssetSystem 已提交 snapshot 重建，固定 TileMap/Animation document 也从新 Catalog 重新打开。owning workspace、
Browser/selection、2D/3D binding 与 animation preview 全部验证成功后才增加 project-switch 计数；commit 前失败保留旧
Catalog，commit 后 preview 重建失败则作为结构化致命错误返回，不能伪装为成功。

Editor source import 已完成产品接线。自动化入口使用 strict UTF-8 absolute `--project-root=<path>`，以可重复且可混合的
`--import-recipe=<path>` / `--import-gltf=<path>` / `--import-texture=<path>` / `--import-audio=<path>` 表达完整 intended unit 集；`--import-on-start` 在安全帧启动导入，
`--project-root` 与 `--catalog-root` 互斥。Project Assets 标题栏的小 `+`（与 `File > Import Files...` 同一命令）可在 Windows
原生对话框中一次批量选择 `.recipe` / `.gltf` / `.glb` / `.png` / `.jpg` / `.jpeg` / `.wav` 并加入同一 intended set。
无项目启动时选择文件后，Editor 自动在系统临时目录创建并持有唯一临时 Project，完成 live Catalog switch 后直接继续导入，
不会紧接着弹出目录选择器。临时 Project 的 `Save` / `Save As` 才要求选择空目录：Editor 先初始化正式 Project，再通过同一后台事务复制
`Source` 资源并在新根重新 cook，成功切换后清理旧临时目录；取消或失败保留临时 Project，未保存退出时由 Editor 定向清理。
项目 `Source/` 内文件直接使用；
外部 PNG/JPEG/WAV 在整批预检成功后分别安全复制到 `Source/Imported/Images/` 与 `Source/Imported/Audio/`。左侧 `Source Imports`
使用三列 DataGrid 显示完整 intended set：`Kind` 固定 88 logical px，显示 Catalog/glTF/Texture/Audio；`Source`
当前约 190 logical px，显示完整 UTF-8 source path；`Status` 显示 Queued/Preparing/Copying/Cooking/Committing/Imported/Failed。
DataGrid 使用固定 3 列、5 行 materialized pool、128 logical px bounded viewport、双轴滚动和 stable
row selection；`Remove` 只读取 selected logical row。非空集合只常驻显示带数量 Badge 的标题栏，记录表默认
`Collapsed`，用户通过 ChevronRight/ChevronDown 手动展开或收起，收起时 `Remove` 禁用，切换项目后恢复默认收起；百分比高度
不再向 Auto 父节点传播，避免展开后 DataGrid viewport 超出 materialized row pool 而触发 UI fatal；
空集合时整段 `Collapsed`，Import 入口仍保留在 Project Assets
标题栏的小 `+`；`Remove` 在 owner thread 生成删除后的候选集合并启动同一 fresh-stage 事务，不会先打开或重新校验
被删除的文件，因此已经从磁盘消失的 stale unit 仍可移除。移除最后一个 unit 会从有效 baseline 增量发布零 entry
Catalog 和零 unit import state；没有有效 baseline 的首次 full cook 仍拒绝空集合。Import/Remove、项目切换和 Catalog
refresh 互斥，隔离 PlaySession active 时列表与 authoring command 同步锁定。Editor 以 4096 unit 为产品上限；原生 dialog 关闭后
UI thread 只把 owned path batch 提交给 `EditorSourceImportService`，后台 worker 在临时候选中完成整批
扩展名识别、物理路径规范化、containment 与去重校验，任一文件非法、越界或分配失败时都不修改既有 intended set；
外部媒体 ingress 也在任何复制前完成整批预检，后续 intended-set 合并、cook、Catalog commit、取消或 shutdown 失败会删除本批新文件与空目录。
目标同名且内容相同时复用项目文件，内容不同时使用 `_2`、`_3` 等后缀，绝不覆盖；批内同一物理文件只复制一次。
Project Assets 的 Source-backed Texture/Audio 资源显示真实文件名，而不是 metadata 中的 `#id` 或可漂移别名；右键
`Rename Source File` 只对单输出 Texture2D/AudioClip 开放，输入限制为同目录、原扩展名不变的 UTF-8 文件名。
Editor 先执行物理文件 rename，再以原 AssetId 作为 stable media override 重建完整 intended set；导入失败、取消、shutdown
或 stage 丢失都会回滚文件，只有 Catalog、Browser、preview 与 import state 全部提交后才确认 rename。recipe、glTF 与多输出
资源保持只读并通过禁用的 Rename 菜单明确提示不支持。每张图片只生成一个 Texture2D；创建或编辑 Sprite2D Node 时可直接选择该 AssetId，不再等待或查找自动生成的
全幅 Sprite wrapper。显式 recipe 中 authored Sprite 的 wrapper/dependency 路径继续可用。
外部 `.recipe` / `.gltf` / `.glb` 因相对依赖无法靠复制单个主文件保证完整性而明确拒绝，调用方需先把完整依赖集置于
`Source/`。已存在或本批重复选择的 unit 只保留一份，但仍会触发完整 intended set 的 reimport；Windows 大小写、
分隔符或 `..` 形成的同文件别名按同一物理路径处理。后台 `EditorSourceImportService` 在单个有界 batch 中执行 ingress、merge 和共享 Asset pipeline，
按 `Preparing` / `Copying` / `Cooking` 发布批级状态，probe 完整 unit 集并生成 fully validated fresh stage；文件比较每 64 KiB
检查协作取消，不为几十张图片派生无界 per-file 线程，也不接触 UI、Render 或 `AssetSystem`；pipeline 把精确的 fully
validated owning `CatalogSnapshot` 一并交给 Ready stage。owner thread 在下一安全帧携带当前 Sprite/Mesh participant 调用
`reloadPreparedCatalog()`，不重新打开或验证 package。dirty Catalog document 会在 commit 前保留 Ready stage；
`CatalogReloadBusy` 及其他 commit 前失败同样保留 snapshot/stage 并逐安全帧重试。fresh stage 在 Ready 前已包含 sibling
current import state；Catalog、Browser、documents 与 2D/3D/Animation preview
全部提交后，Editor 只原子发布项目 `.tina/cache/source-import/active-catalog.path`，交换完整 intended set 并确认 ingress；Ready
在此之前拥有 rollback transaction。再次以 `--project-root` 启动或 Project Open
时验证 pointer、stage state、Catalog revision/output binding 与 physical containment，并恢复 Catalog 和完整 intended unit 集；
有限帧自动导入会直接返回 parser/path/cooker 的真实错误，而不是用 lifecycle 通用错误覆盖首错。Windows/GLFW 文件释放事件会在
Editor owner thread 复制为有界 UTF-8 path batch：释放到 Project Assets committed rect 或 active viewport 都会自动走同一 Source Import
事务；两种入口都只把资源导入 Project Assets/Catalog，不创建或修改场景节点。导入期间的 Play、modal、项目切换或另一批 import
会保留队列并在 Editor 空闲后继续处理，不会改变既有 scene document。GLFW 3.4 已通过统一的 `glfwSetDropCallback` ingress 覆盖 Windows
HDROP、X11 XDND 与 Wayland `wl_data_device`；标准 callback 只有最终 release/drop，不提供 drag-enter/drag-over，因此当前不宣称真正
的系统拖动 hover 高亮。Editor 私有 DropOverlay 仅在 release 后显示 accepted/processing/committed/rejected/failed 状态，并自动复用
Source Import phase 与 Catalog commit 结果。失败 Retry 保存完整 intended units 和原始外部 path batch；Running Cancel 在 owner thread 停止并 join worker，清理 fresh stage、drop intent、retry snapshot 和未提交资源历史，同时保留旧 Catalog/selection。Linux X11/Wayland 构建、运行和产品拖放证据，以及 Explorer 人工拖放证据，尚未完成。
Catalog commit 后重建 preview binding 时，Editor 先退休旧 Sprite/Mesh registry；首次释放失败会 drain
`IRenderDevice` 与 `AssetSystem` 的 GPU retirement 并重试一次。若 registry 仍处于 engaged 状态，下一次 prepare 返回
`CatalogReloadBusy`，不会用 `optional::emplace` 覆盖仍持有 live binding 的 registry 而触发析构期 `std::terminate`。
Windows 使用系统原生 dialog；Linux 私有 adapter 以 `zenity` 为首选、`kdialog` 为缺失回退，覆盖 open/save/folder 且不经过
shell。Windows COM guard 只在 `CoInitializeEx()` 返回 `S_OK` 时调用一次
`CoUninitialize()`。`EditorProjectWorkspace` 的 canonical generic UTF-8 路径在进入 Windows Shell 前通过
`std::filesystem::path::make_preferred()` 转为原生分隔符，避免第二次 Import Files 将 `C:/.../Source` 交给
`SHCreateItemFromParsingName()` 后得到 `E_INVALIDARG`。连续两次真实系统 dialog 仍属于人工产品证据。Linux 定向编译和真实 helper 产品门禁仍是平台证据，但不是 Editor 完成度的唯一剩余项；视口导航、gizmo、
场景操作与可视化门禁必须分别按当前源码状态记录。

## Editor application layout

```text
Command bar (File/Edit/View/Help; project lifecycle only under File; centered 2D-3D workspace/undo-redo-save-save-as/play-pause-step-stop)
Conditional document tab strip (external scene/Catalog tabs/close; hidden when empty)
Workspace
  Left dock
    Hierarchy (filter/add/duplicate/delete/focus/virtual dynamic TreeView)
    Project Assets (Search + All/2D/3D/Media/compact Grid-List/import/refresh/collapsed-empty imports)
    Source Imports (default-collapsed Kind | Source | Status virtual DataGrid/remove; toggle in header)
  Active 2D/3D viewport (one Scene-TileMap/transform/snap/marquee/tile/frame/view toolbar + preview canvas)
  Inspector dock (scrollable identity/transform/node properties/hierarchy/TileMap/document/36px dependency list)
Collapsible bottom panel (Animation timeline or structured Output history; closed by default)
Status bar (schema/entities/revision/preview/selection)
Dirty-close modal (save/save-as/discard/cancel)
Snackbar host (feedback/optional undo/polite live region)
```

这层属于 `Tina::EditorApp` 组合根，`Tina::Editor` 公共头仍不依赖 UI、Runtime、Scene 或 backend。布局与 GPU smoke 的
结构化输出报告 `editorLayoutRegions=9`、`viewportLayoutReady=true`、`inspectorScrollConfigured=true`、
`renderExtractions`、2D `gpuViewportSprites=13`（1 World Sprite + 12 Tile sprites）或 3D
`gpuViewportMeshes=3`、`gpuViewportReady=true`，以及 committed logical rect 和 normalized viewport。正式视口不再使用
`600x360` preview 上限或在画布中放置 Scene/Entity/Cook 测试说明；中央 GPU 区域随 workspace 剩余空间伸展。
`gpuViewportDocumentRevision` 还必须与最终 canonical document revision 一致。字段事务由 `inspectorTransactions`、
`inspectorRejectedTransactions` 和当前动态 stable ID 对应的有限、正 scale 完整 TRS 取证；gizmo 由
begin/preview/commit/cancel/reject counter 与有限且非 identity 的最终 delta 取证。Hierarchy 自动目标只记录运行时解析出的
stable ID，并要求最终 key 与该 stable ID 一致、logical index 位于动态 item count 内。
文件保存另由 active document 字段与 `world2D/3DDocumentPathConfigured`、`world2D/3DDocumentLoaded`、
`world2D/3DDocumentDirty`、`world2D/3DSavedSnapshotBytes` 和落盘 bytes 取证。
Catalog 接线由 `catalogReady`、`projectCatalogConfigured` / `testFixtureCatalog`、entry/load/GPU/binding/unresolved
计数以及 `catalogResolved2DSprites` / `catalogResolved3DMeshes` 取证。TileMap 另报告 document revision、layer/chunk/
non-empty cell、root+chunk cook artifact/bytes、emitted sprite、edit/undo/redo，以及
`tileMapGameplayGenerations/SpawnRecords/Bytes/SourceRevision`，Navigation bake 另报告 bake/source revision、payload bytes、
ready/dirty/published 与 Catalog reload；Animation 另报告 document revision、frame/cook bytes、preview frame、
edit/undo/redo、event marker 增删改与 playback transition。Project Browser/tabs 另报告
`projectAssetBrowserReady`、`projectAssetVisibleItems`、`projectAssetOpenCount`、`documentTabsReady`、`documentTabCount` 与
`documentTabSwitches`；自动 smoke 从 4 个 pinned tab 打开一个额外 current-schema Animation asset，再恢复 pinned
Animation 与初始 workspace，最终 tab/open/load/swap/binding-refresh 固定为 `5/1/1/2/2`。门禁不绑定内部 command/action
枚举的固定数量；
source-import 自动化另报告 project root、intended unit count、start/complete/failure/busy-retry、unit/object 统计、Running/Ready
与 state-committed 状态。

## 2D/3D viewport navigation, grid and zoom

`Tina::Editor::EditorViewportNavigation` 同时拥有独立的 2D center/zoom 与 3D target/yaw/pitch/distance 状态，并提供
`set2DView()`、`set3DView()` 与 3D Perspective/Top/Bottom/Front/Back/Left/Right preset 的原子直接发布。
最多 64 条输入组成一次原子 batch：任一输入非法时两套 camera state 与 revision 都不变，有实际变化的 batch 只推进一次
revision。EditorApp 已把中键拖动接到 2D/3D pan、3D 右键拖动接到 orbit、滚轮接到 2D pointer-anchored zoom 或 3D
dolly；Focus 与 Frame All 根据当前动态选择或全部 preview 内容重建 navigation state。2D 与 3D workspace 分别持有 camera
session，切换 workspace 或重建 preview 后恢复各自最近视角；Frame All reference 与用户 session 分离，不会在 preview rebuild
时覆盖已初始化 session。3D view selector 按 Perspective → Top → Front → Right → Back → Left → Bottom 循环，orbit 后清除
preset 并显示 `View: Custom`。状态再统一驱动 Camera、projection、grid、TileMap picking/culling、marquee candidate projection
与 transform gizmo，不存在只移动装饰网格的旁路。

`Tina::Editor::EditorViewportGrid` 是 backend-neutral、固定 `160` segment 容量的正式 Editor 模块。输入只包含 projection、
committed logical extent、25%-400% zoom、相机中心和 world spacing；成功 publication 输出归一化且限制在 `[0,1]` 的
projected segment、revision 与 minor/major/axis 计数。配置未变化时不发布新 revision，也不产生 retained layout
mutation；非法配置或容量失败保留上一份 publication。公共头不依赖 UI、Scene、Render 或 backend。

- 2D 使用 Camera2D 中心、视口 aspect 与 `FixedWorldHeight2D` 生成 1 m orthographic grid；低像素密度时按
  `1/2/5 x 10^n` 自适应隐藏过密 minor line，X/Y axis 独立着色。
- 3D 使用真实 camera yaw/pitch/distance/FOV 投影并裁剪 XZ perspective ground grid；2D/3D grid 的每个
  projected segment 都直接发布为一个 `UIBoxPaint::Line`，X/Y/Z axis 独立着色。Line committed paint 保留
  logical 端点和线宽，Render bridge 对线宽法向四角分别应用 framebuffer X/Y scale，以 exact 四顶点支持
  anisotropic 投影；integer envelope 只用于裁剪和剔除。
- transform gizmo 的轴线和平面边界使用同一 Line 原语；rotation ring 使用单个向内描边的
  `UIBoxPaint::Ellipse`，不再拆成弦段。Ellipse 的填充/向内描边由 coverage shader 完成。
- 原有 axis-aligned 多段近似及其 12K node、32K paint、16K DisplayList 预算代码已经删除。固定160个 grid
  segment 与256个 gizmo overlay 槽均使用单节点 Line/Ellipse，Editor 因而恢复默认 UI/DisplayList 容量；
  overlay 继续使用 `PointerHitPolicy::Ignore`，effective clip 仍是 axis-aligned scissor，几何命中继续由
  gizmo backend 负责。Editor 不再为整个 backbuffer 打开 8× MSAA，跨 GPU UI-003 golden 仍由 backlog
  `RENDER-LINES-001` 跟踪。
- Layout Debugger 把 Editor 的 UI DisplayList 上限固定为32768，并为 scene submission 额外预留1024，
  因此 `EngineConfig::renderDrawCallCapacity` 为33792。它仍低于 bgfx 的65K通用上限，同时不会让满载
  layout overlay 与 world pass 争用同一份32K ceiling。
- grid、transform gizmo 与 marquee overlay node 在 root 创建期一次性预分配，未使用槽为 `Collapsed`，并保持
  `UIPointerHitPolicy::Ignore`；它们的命中仍由 `viewportPreviewLayer_` 统一路由给 navigation、transform gizmo、
  marquee 与 TileMap brush。
- viewport 右上角使用 Tina orientation control：2D 是无底盘的 58 logical px 紧凑 X/Y 罗盘，按引擎世界坐标
  显示 `+X` 向右、`+Y` 向上并隐藏 Z；3D 是 82 logical px 的分层球形 View Gizmo，以内层球面、高光、经纬线和
  外轮廓建立空间感，并将世界 X/Y/Z 轴按真实 Camera3D rotation 正交投影。红/绿/蓝轴色保持不变，背向相机的轴
  降低不透明度。整个控件建立独立 Pointer barrier，空白区域不会穿透到 viewport；3D 的正 X/Y/Z
  端点复用标准 Button 激活路径，分别切换 `Right` / `Top` / `Front` 视图，回调只排队 intent 并在合法
  `updateUI()` 阶段应用 preset。2D 坐标盘保持方向反馈，点击端点不改变固定 Orthographic 视角。
- 画布滚轮直接进入 2D pointer-anchored zoom 或 3D dolly；Viewport 不保留 footer、缩放按钮、Slider 或
  常驻百分比。`viewportZoomPercent` 仍作为内部状态与自动门禁证据。2D projection、TileMap visibility query、pointer-to-cell、gizmo delta
  和 grid 使用同一 world extent；3D 同步调整 perspective FOV，避免只缩放网格而场景内容不变。

产品结果必须报告 `viewportGridRevision/Segments/MinorLines/MajorLines/AxisLines`、`viewportZoomPercent`、
`viewportGridReady` 与 `viewportGrid2DObserved/viewportGrid3DObserved`。自动 smoke 必须实际发布过两种 projection；
默认交互模式或未带 `--auto-demo` 的有限帧运行只要求当前 workspace 对应的 projection。

## 文件加载与原子保存

`loadWorld2DAuthoringDocument(utf8Path, document)` 先以 document 配置可容纳的当前 schema 最大 wire size 为上限完整读取文件，再调用
`loadSnapshot()`；成功 canonicalize 并清空旧 history，read/旧 schema/截断/document capacity/history capacity
失败均保留原 document 与 history。Shell 不把“文件存在但加载失败”当作新文件继续运行，避免退出时覆盖坏文件或
更高版本资产。

`saveWorld2DAuthoringDocument(utf8Path, document)` 只读取 document 已发布的 `snapshotBytes()`，通过 Core
`writeFile()` 在目标同目录写完整临时文件，再用 OS 原子 replace 发布；缺失父目录会创建。Windows 使用
`MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`，其他平台使用同目录 rename。replace 失败时删除临时
文件但不先删除旧目标，document、revision 与 undo/redo 也完全不变。

`loadWorld3DAuthoringDocument()` / `saveWorld3DAuthoringDocument()` 对 Prefab v4 提供相同契约：读取上限由
document node capacity 和当前 wire size 计算，成功加载建立 clean baseline，保存只发布 `payloadBytes()`，不生成
Editor 私有格式。2D 与 3D 文件失败都不会改写 active document 或既有目标。

`saveSpriteAnimationAuthoringDocument(path, document, platform)` 直接写 `cookPreview(platform)` 的唯一 current-schema
Cooked artifact，并以 sibling atomic replace 发布。`saveTileMapAuthoringDocument(root, document, platform)` 使用每个
artifact 的 canonical relative path，先逐个原子发布全部 TileMapChunk，再最后发布 TileMap root 作为 commit marker；
返回 artifact/byte count。二者都自动创建父目录，不生成 manifest、source metadata 或 Editor 私有格式。

Shell 在真正写入前先准备 saved baseline 与目标 path，避免“文件已保存但 UI 因随后分配失败仍报成功”的半状态。
World/Animation baseline 比较 identity + primary payload；TileMap baseline 还比较 root identity、全部 chunk identity 与
payload。保存成功后以 canonical bytes 与 saved baseline 的完整相等比较判断 dirty；因此保存后编辑再 Undo 回 saved bytes 会恢复 `Saved`，
而不是仅按单调 revision 误报 `Modified`。Save 是 Editor command，不是 authoring revision，也不触发多余的
`Scene::World` preview instantiate。

2D/3D session 在启动时独立 open；任一路径 NotFound 只让对应 document 保持默认内容和空 baseline，另一 session
不受影响。模式切换只改变 active view，目标 session 的 path、loaded、baseline、dirty 与 history 均保持原值；自动
smoke 在编辑和可选 Save 后执行一次 2D↔3D round-trip，并回到初始 workspace。

## 模块边界与数据流

```text
Inspector / gizmo / tile brush / animation timeline / importer intent
  -> World2DAuthoringDocument / World3DAuthoringDocument / TileMapAuthoringDocument
     / SpriteAnimationAuthoringDocument candidate
  -> AssetFormat current-schema validate + canonical write
  -> bounded revision publication
  -> World2D/Prefab bytes, TileMap root + non-empty chunk bytes, or SpriteAnimationClip payload/dependencies
  -> AssetId + validated Catalog / AssetSystem
  -> Sprite2D/Tileset / Mesh3D binding registry
  -> World2D instantiate / TileMapInstance / SpriteAnimator2D / Prefab-to-World3D preview
  -> packet-local FrameResourceRef extraction
  -> active document save -> Core atomic sibling replace
```

TileMap gameplay 路径不把游戏组件引入 Editor：

```text
TileMap visible object layer
  -> bounded owning TileMapGameplaySpawnPlan
  -> game-owned archetype table + encoder
  -> one World2DAuthoringDocument::replace(schema/version/gameplay bytes)
```

- `Editor` 依赖 `Core` 与 `AssetFormat`，不依赖 UI、Runtime、Scene 或 backend；
- `Editor` 是工具侧已安装 target，但不由 `Tina::GameSDK` 聚合链接；
- 持久化身份仍只有 stable entity/parent ID 与 `AssetId`，没有 Runtime `EntityId`、generation、Handle、Lease 或 GPU
  identity；
- `snapshotBytes()` / `payloadBytes()` 是唯一 preview/cook 输入，不生成第二份语义相近的 editor wire payload。

## 容量边界

| 预算 | 默认 | hard limit / 规则 |
| --- | ---: | --- |
| entity | 4096 | `World2DSnapshotWire::MaximumEntities` |
| prefab node | 4096 | `PrefabWire::MaxNodes` |
| gameplay bytes | 4 MiB | `World2DSnapshotWire::MaximumGameplayBytes` |
| TileMap layer / object / chunk | 16 / 128 / 128 | 当前 Editor document 显式配置；schema hard limit 更高 |
| TileMap gameplay spawn record | 128 | 当前 Editor 使用 object capacity；公开 hard limit 为 `TileMapWire::MaxObjectsPerMap` |
| SpriteAnimationClip frame | 256 | 当前 Editor document 显式配置；schema hard limit 为 4096 |
| Project Asset descriptor / dependencies total / per asset | 4096 / 4,000,000 / 4096 | 创建时复制 canonical path 与完整 dependency records 并按 AssetId 排序；超限、重复或图不一致原子失败 |
| Project root UTF-8 path | 4096 bytes | project/source/Catalog 各自上限；Source 与 Catalog 必须是 project 的隔离子目录 |
| EditorApp document tab | 6 | 固定 UI 槽；公共 model 默认 16、hard limit 64 |
| history entries（含 current） | 32 | 2..256 |
| history canonical bytes（含 current） | 16 MiB | 64 bytes..1 GiB |

history vector 在 Create 时一次 reserve 到配置 entry 上限。发布新 revision 前先裁剪 redo，并按 entry/byte budget
淘汰最老 revision；不会通过隐式扩容突破配置。为了让每个成功编辑至少有一步 Undo，current 与 candidate 必须能
同时放入 history byte budget，否则本次编辑失败。

## 编辑与状态流

- `replace(desc)`：完整 batch transaction，适合多字段 Inspector commit、gizmo drag end 或 importer；
- `loadSnapshot(bytes)`：只接受完整通过当前 parser 的 snapshot，再按当前 writer 规范化并原子建立新 baseline；
  成功清空 undo/redo，旧 schema 或容量失败保留原 document/history；
- `upsertEntity(entity)`：按 stable ID 替换或追加一个 entity，parent 仍必须指向此前 entity；
- `eraseEntitySubtree(id)`：按 topological authoring order 删除目标及全部后代，避免悬空 parent；
- `setGameplay(schema, version, bytes)`：游戏自有 blob 仍要求“空 blob ↔ 零 schema/version、非空 blob ↔ 非零”；
- `World3DAuthoringDocument::replace/loadPayload/upsertNode/eraseNodeSubtree`：在 Prefab v4 上提供相同的
  canonical publication、subtree 删除、容量与失败原子性；
- `EditorSceneOperations`：在两种 scene document 上提供 add、duplicate subtree、delete subtree 与 reparent；新 stable ID
  从当前 document 动态派生，成功状态变更只发布一个 canonical revision，no-op reparent 不发布；
- `TileMapAuthoringDocument::setCells/paintCell`：批量 cell 作为一个 root/chunk revision；重复坐标、越界、错误
  layer kind 或容量失败都不发布；
- `addTileLayer/addObjectLayer/eraseLayer/renameLayer/setLayerVisibility` 与 `upsertObject/eraseObject`：只写当前
  TileMap schema，稳定 layer/object ID 由 writer 全图校验；
- `loadPayloadFamily()`：只接受完整 current-schema root + stable-ID chunk family，并建立 clean baseline；
- `cookPreview()`：输出一个 TileMap cooked artifact 和每个非空 chunk 的 TileMapChunk artifact；
- `TileMapGameplaySpawnPlan::Build()`：从指定 visible object layer 生成按 stable object ID 排序的 owning records；hidden
  object 跳过，unknown/duplicate/capacity failure 不发布；
- `generateTileMapGameplay()`：game-owned encoder 成功后只发布一个 World2D revision，因此整次生成只增加一个 undo entry；
- `EditorViewportNavigation`：2D pan/anchored zoom 与 3D orbit/pan/dolly 共用有界原子输入 batch，direct view/preset 走同一
  revision；EditorApp 为 2D/3D 分别保存 session，并把 Frame All reference 与 session 分账；
- Inspector batch apply：`Mixed` 解析为缺失 optional，遍历完整多选 stable ID 后只调用一次 `replace()`；2D 只写
  Position XY/Rotation Z/Scale XY 且 Rotation Z 强制平面 quaternion，3D 合并未编辑 Euler 分量；no-op 不生成 history entry；
- `EditorTransformGizmo`：Down 固定 workspace/document/selection revision、handle、orientation、selected transformable roots
  与 projected axes，过滤 selected parent 的 child 并建立平均 world pivot；Move 发布 Translate/Rotate/Scale absolute-delta
  Scene preview，并把 world 结果安全反算为 local transform；Up 锁定终态且只调用一次 document `replace()`；Cancel、no-op、
  shear/零 scale 或 baseline 冲突恢复 canonical preview，不生成 history entry；
- `EditorMarqueeSelection::Evaluate()`：以固定容量 stable-ID candidate/current selection 计算 Replace/Add/Toggle 结果与
  added/removed diff；非法 rect、重复/零 stable ID 或 union 超限时原子失败；空结果同步 Hierarchy document root；
- `EditorPlaySession`：Play 时按实际 payload 事务复制 current-schema canonical bytes，提供 Playing/Paused、bounded
  fixed-step advance、single-step 与 Stop；空 session 不按容量上限预留，Stop 释放快照，session 不持有或修改
  authoring document；
- `undo()` / `redo()`：只移动已发布 revision cursor，不重新解析、不分配。

相同 canonical bytes 是 no-op，不增加 revision 或裁剪 redo。成功 edit/undo/redo 单调推进 document revision；达到
`u64` 最大值后饱和，不回绕。

## 失败语义

候选在任何 live mutation 前完成 document capacity、current schema validation、canonical serialization 与
history 可撤销性检查。以下失败均保留 current bytes、revision、undo depth 与 redo depth：

- stable ID 为零/重复、parent 自引用/前向引用/缺失；
- transform/node property payload 非有限、枚举/flag/AssetId/Gameplay identity 非法；
- 输入为旧 schema、非 canonical wire bytes、截断 payload，或 chunk 不属于 root/稳定派生 ID 不匹配；
- entity/gameplay 超出 document config；
- current + candidate 超出 history byte budget；
- 分配失败。

Undo/Redo 无对应 revision 时分别返回 `UndoUnavailable` / `RedoUnavailable`，不改变 cursor。

进程级不可恢复故障与普通 document transaction failure 分账。`TinaEditor.exe` 在任何 Editor/Runtime 创建前安装
Core CrashHandler；每次启动在 `%TEMP%/tina_editor_crash.txt` 建立 armed marker；系统临时目录查询失败时回退当前工作目录的
同名文件。`std::terminate`、abort 或 Windows
fatal exception 写 `status=crash`、reason 与 best-effort backtrace；穿过顶层 application boundary 的可表示
`Core::Error` 写 `status=fatal`、domain/code、origin 与完整 context chain。该文件用于回答“窗口为何消失”，不代表
Editor 能从损坏进程恢复；导入/保存等可恢复错误仍应留在 UI feedback/Output 并保持旧 Catalog/document。

## 验收

Editor application 切片使用正式 `tina_editor_desktop` target；只有明确授权自动验收 gate 时才运行
`TinaEditor.exe` 的 2D/3D 产品短 smoke。`tina_sample_2d` / `tina_sample_3d` 不是 Editor 验收入口。纯 `Tina::Editor` document 改动才运行
`tina_editor_tests` 的精确 filter，修改 Runtime UI committed-layout 契约时才补对应 `tina_runtime_ui_tests` filter。

上述 smoke 只属于明确授权的自动验收 gate。用户要求“编译”“给我编译版本”或说明“我手动测试”时，必须只增量
构建 `tina_editor_desktop` 并交付 `TinaEditor.exe`；不得运行 Editor test executable、不得启动 Editor、不得追加
2D/3D smoke。编译、运行和测试是三个独立阶段，不能用“大功能闭环”作为扩大用户授权的理由。

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_editor_tests --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_editor_tests.exe `
  --gtest_filter=EditorPlaySessionTests.*:EditorSceneOperationsTests.*:EditorViewportNavigationTests.*:EditorViewportGridTests.*:EditorTransformGizmoTests.*:EditorMarqueeSelectionTests.*:TileMapGameplaySpawnPlanTests.*:SpriteAnimationAuthoringFileTests.*:TileMapAuthoringFileTests.*:ProjectAssetBrowserTests.*:EditorProjectWorkspaceTests.*:EditorProjectCreationTests.*
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_editor_desktop --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\TinaEditor.exe `
  --frames=70 --frame-delay-ms=0 --workspace=2d --auto-demo
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\TinaEditor.exe `
  --frames=70 --frame-delay-ms=0 --workspace=3d --auto-demo
```

Core 另保留 `WriteFileTests.FailedAtomicReplacePreservesExistingTargetDirectory` 回归。由于它目前位于 monolithic
`tina_tests`，小型 Editor 保存切片不为单个 filter 重编整个 Runtime 测试 executable；下一次大功能统一 Core gate
再执行。当前 Editor file overwrite 用例会真实经过 Windows `MoveFileExW` 覆盖路径。

人工操作直接启动 `TinaEditor.exe`，不传 `--frames` 与 `--auto-demo`，由窗口关闭结束；需要有限帧观察时只传较长的
`--frames=<N>`。两种方式都不会运行自动 authoring 流程，也不会把用户产生的 revision 数量误判为自动 smoke 失败。
需要分析空 Editor 的 UI 内存时追加 `--profile-ui --frames=<N>`；末尾 JSON 会同时输出 UIContext PMR
创建分类与首末帧增量、dirty/rebuild counters，以及 Catalog/Engine/首帧/末帧/销毁阶段的 Windows Working Set /
Private Bytes。该诊断不修改产品容量；应先根据阶段 delta 定位 owner，再根据 UI PMR 分类决定是否重构稠密
state storage，不能只看单一容量字段判断泄漏。
同一参数也会记录交互式 source import 的 before/worker/commit/peak 内存快照、cooked payload、临时 pool
峰值和释放后字节、AssetStore resident CPU payload，以及导入前/中/后的 FPS/帧耗时。图片只导入到 Project Assets
而尚未被场景引用时，完整 Texture2D payload 不属于 resident 数据；worker 和 Catalog validation 的完整文件缓冲
必须在对应阶段结束时释放。
普通无项目启动不会提供任何测试资源；真实资源验收可直接点击 Project Assets 标题栏的小 `+` 选择项目外
PNG/JPEG/WAV（可一次选择几十张），确认 dialog 关闭后 Editor 仍可响应，状态依次显示 Preparing/Copying/Cooking/Committing，
文件自动进入临时 Project 的 `Source/Imported`，且每张图片只产生 Texture2D；把该 Texture2D AssetId 直接用于
Sprite2D Node 后可进入 2D 预览。关闭后再次打开 `Import Files...`，确认第二次系统 dialog 仍能解析初始目录。
随后点击 `Save` 或 `Save As`，选择空目录并确认资源迁移、重新 cook 和临时目录清理。
已有永久项目也可通过同一 `+` 直接导入。glTF/GLB 的完整
主文件与相对依赖仍先放入项目 `Source/` 再选择，导入后应产生 Mesh/Material/Prefab
并用于 3D 预览；任一路径都不得回退到 auto-demo fixture。

document 与 EditorApp 接线切片关闭需要：canonical preview 与 AssetFormat writer bytes 完全一致；GPU viewport 的 Camera、
resolved transforms 与 revision 来自同一 preview World/binding，committed UI rect 正确归一化且不保存 snapshot borrow；非法 edit 原子失败；
subtree 删除；bounded undo/redo、branch replacement 与 history byte failure；旧 schema 拒绝；Editor 2D/3D 公共头
isolation 编译通过；已有文件加载为 clean baseline、加载失败不改变 current/history；文件保存 exact canonical bytes、
覆盖失败不删除旧目标；TinaEditor 在显式 `--auto-demo` 下自动完成 Move → Apply Transform → 当前 workspace navigation
→ Translate Gizmo → Marquee Replace/Add → 多目标 Rotate/Scale Gizmo → Marquee Toggle → Undo/Redo
→ Add/Duplicate/To Root/Reparent/Delete/Delete → Play/Pause/Step/Resume/Stop → Generate Gameplay（2D）→ Save
→ other workspace navigation → Animation Next/Mode/Undo/Redo/Cook → Open Selected Asset → World2D PointLight2D
Color Picker 可见帧 → initial workspace 最终选择。
自动目标、marquee probe、gizmo handle 与 Reparent parent 都从当前 preview/hierarchy 动态解析 stable ID 和投影几何，
不依赖固定行号或最终坐标；Add/Duplicate 产生的对象在 scene operation 链末尾删除，实体数恢复为初始值。

三类 Gizmo 必须各自 begin/preview/commit 一次且 cancel/reject 为零；Rotate 与 Scale 还必须分别证明实际捕获至少两个
祖先过滤后的 transform target，不能用 parent/child 表面多选冒充 group transform。Translate delta、rotation degrees 与
scale factors 都必须有限且 non-identity。Marquee Replace/Add/Toggle 各 commit 一次、selection 都实际变化，并同时观察
added/removed 与最大多选数。Scene command 固定为 Add/Duplicate/To Root/Reparent/Delete=`1/1/1/1/2`；Play
固定为 Start/Pause/Step/Resume/Stop=`1/1/1/1/1`，paused Step 必须实际推进 simulation tick。
最终选择按 stable ID 重新解析并验证 logical index 位于动态 item count 内；最终 TRS 有限且 scale 为正，document/GPU
revision 相等，Undo/Redo 都实际完成且 redo depth 为零。round-trip 至少执行两次 workspace switch、消费 2D pan/zoom
与 3D orbit/pan/dolly、验证两种 workspace preview ready，并证明 inactive session 的 path/loaded/baseline/dirty 未变化；
不再固化最终坐标、scene revision、undo depth 或 preview instantiate 次数。
2D gameplay generation/records/bytes=`1/2/64`
且 source revision 非零；3D 的四项 gameplay generation 字段保持零。auto-demo test fixture Catalog smoke 还必须固定报告 entry/load=`9/7`、
Texture/Mesh upload=`1/1`、Sprite/Mesh/Material binding=`1/1/1`、unresolved=`0` 与 resolved 2D/3D=`1/3`。

TileMap root+chunk authoring/cook/save、SpriteAnimationClip timeline authoring/cook/save、Catalog-resolved viewport、
Project Browser、分类过滤、资源 Inspector、Catalog current-schema open/refresh、固定容量 document tabs/session、
Save/Save As、Windows native dialog、Linux `zenity`/`kdialog` dialog 与四类 retained `UIDialog` 已完成。项目 workspace 与空目录创建的基础 API 已完成，
Windows Project `New` 也能创建 Source/Catalog、manifest-last 发布空 current-schema package 并 reopen/typed-validate；
Project `Open` 与 New/Open 的下一安全帧 live project/Catalog switch 也已完成。Editor source import 的后台 ingress/分块校验/intended-set merge、完整 intended unit
probe、fresh-stage cook、主线程 Catalog reload/busy retry、dirty-document commit gate、stage sibling state + 单一 active pointer
commit 与 reopen 恢复，以及 intended-set 三列 `Kind | Source | Status` DataGrid、选择、stale unit 删除和最终空 Catalog 发布也已完成；新增
viewport/hierarchy/play 功能的专项测试、Windows 产品交互和 2D/3D 70 帧 smoke 已在本轮统一门禁收口：`tina_editor_tests` 112/112、`tina_editor_app_tests` 23/23、`tina_tests` 377/377，三类 executable 均 exit 0。跨 DPI/GPU 视觉证据，以及 Linux helper 门禁仍待收口。
Timeline 提供 6 槽可滚动窗口、Play/Pause、Prev/Next、Add/Duplicate/Delete、Sprite 切换、重排、逐帧时长、
Once/Loop/PingPong、独立 Undo/Redo 和正式 Cook Preview；2D 中当前可渲染实体直接预览已解析 Sprite frame，3D workspace
保留该 dock 但禁用 2D 编辑。2D smoke 固定验证 TileMap layers/chunks/cells/artifacts/emitted sprites=`2/2/12/3/12`、
动画 frame/cook=`4/256 B`、test fixture Catalog entry/load=`9/7` 和 GPU sprites=`13`。继续只保留现行 schema，
不增加旧资产兼容分支。`EditorSceneOperations` / `EditorPlaySession` 的专门 unit 与 header-isolation 已接线。
`2D-EDITOR` 仍保持 InProgress 的真实剩余项是：完成跨 GPU 视觉金标；完成 Linux Editor target 定向编译及
`zenity`/`kdialog` open/save/folder/cancel 产品门禁。其他未支持平台继续结构化返回 `Unsupported`，document Save 路径保留
TextEdit 回退。
