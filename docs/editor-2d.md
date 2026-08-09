# Editor 2D / 3D

## 产品场景

Editor 的当前闭环同时覆盖 schema-v1 World2D snapshot、schema-v2 Prefab、TileMap schema-v3 root +
TileMapChunk schema-v1 payload family，以及 SpriteAnimationClip schema-v1。Hierarchy/Inspector/Timeline 把一次
用户意图提交为一个 authoring revision，Undo/Redo 切换已经验证的 revision，Preview 直接把当前 canonical bytes
交给对应 Runtime parser 与 Scene instantiate。工具不能绕过 `AssetFormat` 写半合法数据，也不维护 editor-only 或旧
schema 兼容格式。

独立 `Tina::Editor` target 提供 `World2DAuthoringDocument/File`、`World3DAuthoringDocument/File`、
`TileMapAuthoringDocument/File`、`SpriteAnimationAuthoringDocument/File`，以及项目根模型与空项目目录创建 API；独立
`Tina::EditorApp` 负责桌面组合，正式产品 target `tina_editor_desktop` 输出 `TinaEditor.exe`，其 `main()` 只负责
调用应用模块。2D Inspector 编辑 Position X/Y、Rotation Z（度）与 Scale X/Y；3D Inspector 编辑完整
Position/Rotation/Scale XYZ。viewport 多选时各字段独立显示 `Mixed`，显式 Apply 只解析用户给出具体数值的字段，
`Mixed` 字段按 `std::nullopt` 保留每个对象自己的 canonical 值。一次多对象 Apply 只调用一次 active document
`replace()`，no-op 不发布 revision、command counter 或 dirty 状态。`Apply Transform`、`Move X +1`、viewport transform
gizmo、Undo、Redo 都接到 active document，每次成功 canonical command 后从同一份 bytes 实例化新的 `Scene::World`。
2D Camera/Sprite 与 3D PerspectiveCamera/Mesh preview 都由同一个
World/binding 驱动，不维护平行的 UI 模拟状态，也不把默认 proxy 冒充已解析的 Catalog 产品资源。

Editor 默认进入无帧数上限的交互模式，由主窗口关闭结束生命周期；自动演示不再默认执行。只有同时显式传入
`--auto-demo` 与 `--frames=<N>` 且 `N >= 54` 才运行自动 authoring 流程，重复 `--auto-demo`、缺少 `--frames`
或帧预算不足都拒绝启动。正式短 smoke 统一使用 60 帧。
单独传 `--frames=<N>` 只运行有限帧普通模式，其退出门禁只检查生命周期、UI、viewport、preview、document 与有限值等
通用不变量；自动编辑目标与选择都从当前 hierarchy 的 stable ID 动态解析。

当前 Editor application 的 retained UI 布局已完整铺开：Toolbar、2D/3D 模式切换、上下文工具条、Hierarchy dock、active viewport 工作区、
可滚动 Inspector（Identity/Transform/Components/Authoring/Document）、SpriteAnimationClip Timeline 和底部 status bar
均由 `Flex`、`minMax`、
固定控件高度与滚动容器组合。`updateUI()` 从上一轮成功提交的 viewport/root `worldRect` 计算
`RenderNormalizedViewport`，因此窗口变大时 viewport 与中间工作区共同增长，两侧 dock 保持 bounded width，
world pass 下一帧跟随新的布局；首帧在 committed rect 可用前不提交 world，避免用 `1280×800` 写死区域或全屏闪烁。
`--world2d-path=<UTF-8 path>` 与 `--world3d-path=<UTF-8 path>` 分别配置两个 pinned workspace session；已有文件按各自
schema 原子加载为 clean baseline，不存在的路径保留为该 workspace 的新文档 Save target。每个 pinned/Catalog tab
都有固定容量 session，独立持有 document key、strict UTF-8 path、target platform、loaded flag 与完整 canonical
baseline。Toolbar 的 path 是单行 TextEdit：Save 只在 active document 已有路径且 dirty 时启用；Save As 对四类可写
document 启用，Asset Inspector 保持只读。Windows 使用系统 native dialog；Linux 私有 adapter 使用 `zenity` 并在缺失时
回退 `kdialog`。World2D 选择 `.tworld`、World3D 选择 `.tprefab`、SpriteAnimation 选择 `.tasset` 文件，TileMap 选择 package
输出目录。取消 dialog 不修改 path、baseline、dirty、tab 或 selection；其他未支持平台返回 `Unsupported`，EditorApp
明确回退到 TextEdit 中的 strict UTF-8 路径。
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
preview，document/history 不变。Hierarchy 与 status bar 在多选时显示 `N selected | Group pivot`，Inspector 标题显示
`Mixed values`，gizmo 开始、预览、提交与取消都有明确状态反馈。

Hierarchy 每次在 World2D/World3D document 变化后都从 canonical entity/node
及其 parent stable ID 重建动态树；document root 使用非持久化的 UI key，实际场景项直接以 stable ID 作为 key。`Add`、
`Duplicate`、`Delete`、任意 `Reparent`、`To Root` 与 `Focus` 已同时接入 2D/3D：状态操作只发布一个 canonical revision，刷新后按
stable ID 恢复选择；Prefab v2 删除最后一个完整 subtree 会原子拒绝。Select tool 的 marquee 从当前 preview 投影收集候选，
以固定 512 项容量发布按 stable ID 排序的 Replace/Add/Toggle 多选及 added/removed diff，primary stable ID 同步回 Hierarchy；
空结果把 Hierarchy 明确切回 document root，不会用伪造 stable ID 恢复旧 viewport selection。只有 selection 实际变化才推进
selection revision，活动 gizmo 通过该 revision 检测并安全取消。

`EditorPlaySession` 已接入 Toolbar 的 Play/Pause/Step/Stop。启动时复制当前 2D snapshot 或 3D Prefab canonical bytes，
以有界 fixed-step clock 驱动隔离 preview；authoring document 不被 simulation 修改。play session 活跃期间锁定 authoring command
与 document tab 切换，仍允许 viewport Focus；Stop 后丢弃隔离 snapshot 并从 canonical authoring document 重建 preview。

Editor 快捷键使用 frame action mapping：`Ctrl+S` Save、`Ctrl+Shift+S` Save As、`Ctrl+Z` Undo、`Ctrl+Y` Redo、
`Ctrl+D` Duplicate、`Delete` Delete、`Ctrl+1` / `Ctrl+2` 切换 2D/3D、`Ctrl+0` Frame All、`Ctrl+F` Focus Selection、
`F6` Play/Resume、`F7` Step、`F8` Stop。`Escape` 按优先级取消 gizmo、marquee、navigation，关闭 dirty modal 或停止 Play。
不绑定裸 `Q/W/E/R`，避免 Inspector TextEdit 输入期间误触 viewport tool。

2D workspace 激活 TileMap document tab 后开放 viewport `Tile Paint` / `Tile Erase` 和 Inspector 的 Paint、Erase、Toggle Layer、
Add Tile Layer、Add Object Layer、Cook Preview、Generate Gameplay。Pointer 坐标通过 committed viewport rect 和 Camera2D 投影换算到
真实 cell；每次点击只发布一个完整 root/chunk revision，空 chunk 自动删除。TileMap Undo/Redo 与 World2D document
history 相互独立；切到 3D 或离开 TileMap document 会关闭 tile tools。新增 Tile layer 会立即成为 active brush layer，
preview 按 root authoring order 提取全部可见 Tile layer，而不是只渲染第一层。

`--catalog-root=<UTF-8 path>` 配置项目 Cooked Catalog；Editor 启动时通过真实 `AssetSystem` 完整打开并校验 package。
未配置时只为新建文档创建并明确标记临时 built-in preview Catalog，不把它伪装为项目内容。Editor 从 canonical
World2D/Prefab/TileMap/SpriteAnimationClip 的 `AssetId` 收集并去重 preview 根资源：2D 由
`Sprite2DBindingRegistry` 持有 Sprite 或
Tileset 的 Texture2D
Lease/GPU/binding，3D 由 `Mesh3DBindingRegistry` 持有 StaticMesh、Material 与共享 Texture2D 的
Lease/GPU/binding；Scene extraction 只取得 packet-local `FrameResourceRef`。项目 Catalog 中缺失或 kind 不匹配的引用
只从本次 preview 过滤，authoring document 与 history 保持不变，不回退到固定 binding key 或彩色 proxy。

Project Browser 直接拥有 Catalog descriptor 的确定性 AssetId 排序索引，并提供 All/2D/3D/Media 过滤、稳定选择和
固定 32 px 虚拟列表行。每个 owned descriptor 还保存由 `AssetKind + AssetId` 重新派生的 canonical cooked 相对路径与
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
`--import-recipe=<path>` / `--import-gltf=<path>` 表达完整 intended unit 集；`--import-on-start` 在安全帧启动导入，
`--project-root` 与 `--catalog-root` 互斥。Project Assets 的 Import Source 也把 `.recipe` / `.gltf` / `.glb` 加入同一
intended set。后台 `EditorSourceImportService` 只调用共享 Asset pipeline，probe 完整 unit 集并生成 fully validated fresh
stage，不接触 UI、Render 或 `AssetSystem`；owner thread 在下一安全帧携带当前 Sprite/Mesh participant 调用
`reloadCatalog()`。dirty Catalog document 会在 commit 前保留 Ready stage；`CatalogReloadBusy` 同样保留 stage 并逐安全帧
重试。fresh stage 在 Ready 前已包含 sibling current import state；Catalog、Browser、documents 与 2D/3D/Animation preview
全部提交后，Editor 只原子发布项目 `.tina/cache/source-import/active-catalog.path`。再次以 `--project-root` 启动或 Project Open
时验证 pointer、stage state、Catalog revision/output binding 与 physical containment，并恢复 Catalog 和完整 intended unit 集。
Windows 使用系统原生 dialog；Linux 私有 adapter 以 `zenity` 为首选、`kdialog` 为缺失回退，覆盖 open/save/folder 且不经过
shell。Linux 定向编译和真实 helper 产品门禁仍是平台证据，但不是 Editor 完成度的唯一剩余项；视口导航、gizmo、
场景操作与可视化门禁必须分别按当前源码状态记录。

## Editor application layout

```text
Toolbar (document/editable path/mode/play/pause/step/stop/undo/redo/save/save-as)
Document tabs (World2D/World3D/TileMap/Animation/Catalog assets/close)
Context bar (breadcrumb/select/translate/rotate/scale/world-local/snap/marquee/tile tools/frame/status)
Workspace
  Left dock
    Hierarchy (filter/add/duplicate/delete/reparent/focus/virtual dynamic TreeView/selection summary)
    Project Assets (All/2D/3D/Media/virtual ListView/new/open/import/refresh)
  Active 2D/3D viewport (mode/tools/zoom/preview canvas/footer)
  Inspector dock (scrollable identity/transform/components/TileMap authoring/document/36px dependency list)
SpriteAnimationClip Timeline (frames/playback/mode/duration/reorder/undo/redo/cook)
Status bar (schema/entities/revision/preview/selection)
Dirty-close modal (save/save-as/discard/cancel)
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
Catalog 接线由 `catalogReady`、`projectCatalogConfigured` / `builtInPreviewCatalog`、entry/load/GPU/binding/unresolved
计数以及 `catalogResolved2DSprites` / `catalogResolved3DMeshes` 取证。TileMap 另报告 document revision、layer/chunk/
non-empty cell、root+chunk cook artifact/bytes、emitted sprite、edit/undo/redo，以及
`tileMapGameplayGenerations/SpawnRecords/Bytes/SourceRevision`；Animation 另报告
document revision、frame/cook bytes、preview frame、edit/undo/redo 与 playback transition。Project Browser/tabs 另报告
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
- 3D 使用真实 camera yaw/pitch/distance/FOV 投影并裁剪 XZ perspective ground grid；当前 retained UI 只提供
  axis-aligned quad，因此每条斜向 ray 以固定容量、首尾相接的水平/垂直短线构成连续阶梯 polyline，不再显示为一串离散小方块，
  也不能退化成普通屏幕方格。X/Z axis 独立着色。
- overlay node 在 root 创建期一次性预分配，未使用槽为 `Collapsed`，全部 `UIPointerHitPolicy::Ignore`；命中仍由
  `viewportPreviewLayer_` 统一路由给 navigation、transform gizmo、marquee 与 TileMap brush。
- Zoom Slider 与 Frame All 共用 `viewportZoomPercent`。2D projection、TileMap visibility query、pointer-to-cell、gizmo delta
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

`loadWorld3DAuthoringDocument()` / `saveWorld3DAuthoringDocument()` 对 Prefab v2 提供相同契约：读取上限由
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
- `World3DAuthoringDocument::replace/loadPayload/upsertNode/eraseNodeSubtree`：在 Prefab v2 上提供相同的
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
- `EditorPlaySession`：复制 current-schema canonical bytes，提供 Playing/Paused、bounded fixed-step advance、single-step 与
  Stop；session 不持有或修改 authoring document；
- `undo()` / `redo()`：只移动已发布 revision cursor，不重新解析、不分配。

相同 canonical bytes 是 no-op，不增加 revision 或裁剪 redo。成功 edit/undo/redo 单调推进 document revision；达到
`u64` 最大值后饱和，不回绕。

## 失败语义

候选在任何 live mutation 前完成 document capacity、current schema validation、canonical serialization 与
history 可撤销性检查。以下失败均保留 current bytes、revision、undo depth 与 redo depth：

- stable ID 为零/重复、parent 自引用/前向引用/缺失；
- transform/component 非有限、枚举/flag/AssetId/Gameplay identity 非法；
- 输入为旧 schema、非 canonical wire bytes、截断 payload，或 chunk 不属于 root/稳定派生 ID 不匹配；
- entity/gameplay 超出 document config；
- current + candidate 超出 history byte budget；
- 分配失败。

Undo/Redo 无对应 revision 时分别返回 `UndoUnavailable` / `RedoUnavailable`，不改变 cursor。

## 验收

Editor application 切片只增量构建正式 `tina_editor_desktop` target，并直接运行 `TinaEditor.exe` 的 2D/3D 产品短
smoke；`tina_sample_2d` / `tina_sample_3d` 不是 Editor 验收入口。纯 `Tina::Editor` document 改动才运行
`tina_editor_tests` 的精确 filter，修改 Runtime UI committed-layout 契约时才补对应 `tina_runtime_ui_tests` filter。

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_editor_tests --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_editor_tests.exe `
  --gtest_filter=EditorPlaySessionTests.*:EditorSceneOperationsTests.*:EditorViewportNavigationTests.*:EditorViewportGridTests.*:EditorTransformGizmoTests.*:EditorMarqueeSelectionTests.*:TileMapGameplaySpawnPlanTests.*:SpriteAnimationAuthoringFileTests.*:TileMapAuthoringFileTests.*:ProjectAssetBrowserTests.*:EditorProjectWorkspaceTests.*:EditorProjectCreationTests.*
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_editor_desktop --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\TinaEditor.exe `
  --frames=60 --frame-delay-ms=0 --workspace=2d --auto-demo
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\TinaEditor.exe `
  --frames=60 --frame-delay-ms=0 --workspace=3d --auto-demo
```

Core 另保留 `WriteFileTests.FailedAtomicReplacePreservesExistingTargetDirectory` 回归。由于它目前位于 monolithic
`tina_tests`，小型 Editor 保存切片不为单个 filter 重编整个 Runtime 测试 executable；下一次大功能统一 Core gate
再执行。当前 Editor file overwrite 用例会真实经过 Windows `MoveFileExW` 覆盖路径。

人工操作直接启动 `TinaEditor.exe`，不传 `--frames` 与 `--auto-demo`，由窗口关闭结束；需要有限帧观察时只传较长的
`--frames=<N>`。两种方式都不会运行自动 authoring 流程，也不会把用户产生的 revision 数量误判为自动 smoke 失败。

document 与 EditorApp 接线切片关闭需要：canonical preview 与 AssetFormat writer bytes 完全一致；GPU viewport 的 Camera、
resolved transforms 与 revision 来自同一 preview World/binding，committed UI rect 正确归一化且不保存 snapshot borrow；非法 edit 原子失败；
subtree 删除；bounded undo/redo、branch replacement 与 history byte failure；旧 schema 拒绝；Editor 2D/3D 公共头
isolation 编译通过；已有文件加载为 clean baseline、加载失败不改变 current/history；文件保存 exact canonical bytes、
覆盖失败不删除旧目标；TinaEditor 在显式 `--auto-demo` 下自动完成 Move → Apply Transform → 当前 workspace navigation
→ Translate Gizmo → Marquee Replace/Add → 多目标 Rotate/Scale Gizmo → Marquee Toggle → Undo/Redo
→ Add/Duplicate/To Root/Reparent/Delete/Delete → Play/Pause/Step/Resume/Stop → Generate Gameplay（2D）→ Save
→ other workspace navigation → Animation Next/Mode/Undo/Redo/Cook → initial workspace → Open Selected Asset。
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
且 source revision 非零；3D 的四项 gameplay generation 字段保持零。built-in Catalog smoke 还必须固定报告 entry/load=`9/7`、
Texture/Mesh upload=`1/1`、Sprite/Mesh/Material binding=`1/1/1`、unresolved=`0` 与 resolved 2D/3D=`1/3`。

TileMap root+chunk authoring/cook/save、SpriteAnimationClip timeline authoring/cook/save、Catalog-resolved viewport、
Project Browser、分类过滤、资源 Inspector、Catalog current-schema open/refresh、固定容量 document tabs/session、
Save/Save As、Windows native dialog、Linux `zenity`/`kdialog` dialog 与 dirty-close Modal 已完成。项目 workspace 与空目录创建的基础 API 已完成，
Windows Project `New` 也能创建 Source/Catalog、manifest-last 发布空 current-schema package 并 reopen/typed-validate；
Project `Open` 与 New/Open 的下一安全帧 live project/Catalog switch 也已完成。Editor source import 的完整 intended unit
probe、后台 fresh-stage cook、主线程 Catalog reload/busy retry、dirty-document commit gate、stage sibling state + 单一 active pointer
commit 与 reopen 恢复也已完成；新增 viewport/hierarchy/play 功能的专项测试、产品交互和视觉证据，以及 Linux helper 门禁仍待收口。
Timeline 提供 6 槽可滚动窗口、Play/Pause、Prev/Next、Add/Duplicate/Delete、Sprite 切换、重排、逐帧时长、
Once/Loop/PingPong、独立 Undo/Redo 和正式 Cook Preview；2D 中当前可渲染实体直接预览已解析 Sprite frame，3D workspace
保留该 dock 但禁用 2D 编辑。2D smoke 固定验证 TileMap layers/chunks/cells/artifacts/emitted sprites=`2/2/12/3/12`、
动画 frame/cook=`4/256 B`、Catalog entry/load=`9/7` 和 GPU sprites=`13`。继续只保留现行 schema，
不增加旧资产兼容分支。`EditorSceneOperations` / `EditorPlaySession` 的专门 unit 与 header-isolation 已接线。
`2D-EDITOR` 仍保持 InProgress 的真实剩余项是：完成跨 DPI/GPU 视觉金标；完成 Linux Editor target 定向编译及
`zenity`/`kdialog` open/save/folder/cancel 产品门禁。其他未支持平台继续结构化返回 `Unsupported`，document Save 路径保留
TextEdit 回退。
