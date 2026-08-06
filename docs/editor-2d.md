# Editor 2D / 3D

## 产品场景

Editor 的当前闭环同时覆盖 schema-v1 World2D snapshot、schema-v2 Prefab、TileMap schema-v3 root +
TileMapChunk schema-v1 payload family，以及 SpriteAnimationClip schema-v1。Hierarchy/Inspector/Timeline 把一次
用户意图提交为一个 authoring revision，Undo/Redo 切换已经验证的 revision，Preview 直接把当前 canonical bytes
交给对应 Runtime parser 与 Scene instantiate。工具不能绕过 `AssetFormat` 写半合法数据，也不维护 editor-only 或旧
schema 兼容格式。

独立 `Tina::Editor` target 提供 `World2DAuthoringDocument/File`、`World3DAuthoringDocument/File`、
`TileMapAuthoringDocument` 与 `SpriteAnimationAuthoringDocument`；独立
`Tina::EditorApp` 负责桌面组合，正式产品 target `tina_editor_desktop` 输出 `TinaEditor.exe`，其 `main()` 只负责
调用应用模块。2D Inspector 编辑 Position X/Y、
Rotation Z（度）与 Scale X/Y；3D Inspector 编辑完整 Position/Rotation/Scale XYZ，并把 Euler XYZ 一次规范化为 quaternion。
`Apply Transform`、`Move X +1`、viewport Move、Undo、Redo 都接到 active document，每次成功 canonical command 后
从同一份 bytes 实例化新的 `Scene::World`。2D Camera/Sprite 与 3D PerspectiveCamera/Mesh preview 都由同一个
World/binding 驱动，不维护平行的 UI 模拟状态，也不把默认 proxy 冒充已解析的 Catalog 产品资源。

当前 Editor application 的 retained UI 布局已完整铺开：Toolbar、2D/3D 模式切换、上下文工具条、Hierarchy dock、active viewport 工作区、
可滚动 Inspector（Identity/Transform/Components/Authoring/Document）、SpriteAnimationClip Timeline 和底部 status bar
均由 `Flex`、`minMax`、
固定控件高度与滚动容器组合。`updateUI()` 从上一轮成功提交的 viewport/root `worldRect` 计算
`RenderNormalizedViewport`，因此窗口变大时 viewport 与中间工作区共同增长，两侧 dock 保持 bounded width，
world pass 下一帧跟随新的布局；首帧在 committed rect 可用前不提交 world，避免用 `1280×800` 写死区域或全屏闪烁。
`--world2d-path=<UTF-8 path>` 与 `--world3d-path=<UTF-8 path>` 分别配置两个 workspace session；已有文件按各自
schema 原子加载为 clean baseline，不存在的路径保留为该 workspace 的新文档 Save target。每个 session 独立持有
path、loaded flag 与 saved baseline，切换模式不移动或复用这些状态；Toolbar Save 只原子保存 active canonical
document，未给当前 workspace 配置路径时保持 disabled。两种路径不得指向同一文件。
2D 的五个字段和 3D 的九个字段都通过显式 Apply 合并为一次 document revision；严格拒绝 trailing text、NaN 和 Infinity，
拒绝时恢复 canonical 字段并保持 document/history/preview 不变。2D Rotation Z 发布平面 Z quaternion；3D Euler XYZ
发布完整规范化 quaternion，并保留 hierarchy、Mesh/Material 与 visibility。GPU preview 同步读取完整 canonical TRS。
Move tool 在 preview layer 使用 routed pointer capture：Move 事件只更新临时 `Scene::World`，ButtonUp 才按 stable ID
向 active document 提交一次 revision。2D 将 logical delta 映射到 XY 并翻转 Y，3D 首切片映射到 XZ ground plane 并保留 Y；
PointerCancel、selection/workspace/revision 冲突均丢弃临时状态并恢复 canonical preview，document/history 不变。

2D Hierarchy 选中 TileMap 后开放 viewport `Tile Paint` / `Tile Erase` 和 Inspector 的 Paint、Erase、Toggle Layer、
Add Tile Layer、Add Object Layer、Cook Preview。Pointer 坐标通过 committed viewport rect 和 Camera2D 投影换算到
真实 cell；每次点击只发布一个完整 root/chunk revision，空 chunk 自动删除。TileMap Undo/Redo 与 World2D document
history 相互独立；切到 3D 或离开 TileMap selection 会关闭 tile tools。新增 Tile layer 会立即成为 active brush layer，
preview 按 root authoring order 提取全部可见 Tile layer，而不是只渲染第一层。

`--catalog-root=<UTF-8 path>` 配置项目 Cooked Catalog；Editor 启动时通过真实 `AssetSystem` 完整打开并校验 package。
未配置时只为新建文档创建并明确标记临时 built-in preview Catalog，不把它伪装为项目内容。Editor 从 canonical
World2D/Prefab/TileMap/SpriteAnimationClip 的 `AssetId` 收集并去重 preview 根资源：2D 由
`Sprite2DBindingRegistry` 持有 Sprite 或
Tileset 的 Texture2D
Lease/GPU/binding，3D 由 `Mesh3DBindingRegistry` 持有 StaticMesh、Material 与共享 Texture2D 的
Lease/GPU/binding；Scene extraction 只取得 packet-local `FrameResourceRef`。项目 Catalog 中缺失或 kind 不匹配的引用
只从本次 preview 过滤，authoring document 与 history 保持不变，不回退到固定 binding key 或彩色 proxy。

## Editor application layout

```text
Toolbar (document/path/mode/undo/redo/save)
Context bar (breadcrumb/select/move/tile-paint/tile-erase/frame/status)
Workspace
  Hierarchy dock (filter/actions/virtual TreeView/selection summary)
  Active 2D/3D viewport (mode/tools/zoom/preview canvas/footer)
  Inspector dock (scrollable identity/transform/components/TileMap authoring/document)
SpriteAnimationClip Timeline (frames/playback/mode/duration/reorder/undo/redo/cook)
Status bar (schema/entities/revision/preview/selection)
```

这层属于 `Tina::EditorApp` 组合根，`Tina::Editor` 公共头仍不依赖 UI、Runtime、Scene 或 backend。布局与 GPU smoke 的
结构化输出报告 `editorLayoutRegions=7`、`viewportLayoutReady=true`、`inspectorScrollConfigured=true`、
`renderExtractions`、2D `gpuViewportSprites=13`（1 World Sprite + 12 Tile sprites）或 3D
`gpuViewportMeshes=3`、`gpuViewportReady=true`，以及 committed logical rect 和 normalized viewport。
`gpuViewportDocumentRevision` 还必须与最终 canonical document revision 一致。字段事务由 `inspectorTransactions`、
`inspectorRejectedTransactions` 和最终 Player/Hero 完整 TRS 取证；gizmo 由 begin/preview/commit/cancel/reject counter 与最终
world delta 取证；文件保存另由 active document 字段与 `world2D/3DDocumentPathConfigured`、`world2D/3DDocumentLoaded`、
`world2D/3DDocumentDirty`、`world2D/3DSavedSnapshotBytes` 和落盘 bytes 取证。
Catalog 接线由 `catalogReady`、`projectCatalogConfigured` / `builtInPreviewCatalog`、entry/load/GPU/binding/unresolved
计数以及 `catalogResolved2DSprites` / `catalogResolved3DMeshes` 取证。TileMap 另报告 document revision、layer/chunk/
non-empty cell、root+chunk cook artifact/bytes、emitted sprite 与 edit/undo/redo 计数；Animation 另报告
document revision、frame/cook bytes、preview frame、edit/undo/redo 与 playback transition。

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

Shell 在真正写入前先准备 saved baseline，避免“文件已保存但 UI 因随后分配失败仍报成功”的半状态。保存成功后以
canonical bytes 与 saved baseline 的完整相等比较判断 dirty；因此保存后编辑再 Undo 回 saved bytes 会恢复 `Saved`，
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
| SpriteAnimationClip frame | 256 | 当前 Editor document 显式配置；schema hard limit 为 4096 |
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
- `TileMapAuthoringDocument::setCells/paintCell`：批量 cell 作为一个 root/chunk revision；重复坐标、越界、错误
  layer kind 或容量失败都不发布；
- `addTileLayer/addObjectLayer/eraseLayer/renameLayer/setLayerVisibility` 与 `upsertObject/eraseObject`：只写当前
  TileMap schema，稳定 layer/object ID 由 writer 全图校验；
- `loadPayloadFamily()`：只接受完整 current-schema root + stable-ID chunk family，并建立 clean baseline；
- `cookPreview()`：输出一个 TileMap cooked artifact 和每个非空 chunk 的 TileMapChunk artifact；
- viewport gizmo：Down 固定 workspace/stable ID/revision/start point/committed viewport extent，Move 发布
  absolute-delta Scene preview，Up 锁定终态且只调用一次 `upsertEntity()` / `upsertNode()`；Cancel、no-op 或 baseline
  冲突恢复 canonical preview，不生成 history entry；
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
  --gtest_filter=SpriteAnimationAuthoringDocumentTests.*
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_editor_desktop --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\TinaEditor.exe `
  --frames=60 --frame-delay-ms=0 --workspace=2d
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\TinaEditor.exe `
  --frames=60 --frame-delay-ms=0 --workspace=3d
```

Core 另保留 `WriteFileTests.FailedAtomicReplacePreservesExistingTargetDirectory` 回归。由于它目前位于 monolithic
`tina_tests`，小型 Editor 保存切片不为单个 filter 重编整个 Runtime 测试 executable；下一次大功能统一 Core gate
再执行。当前 Editor file overwrite 用例会真实经过 Windows `MoveFileExW` 覆盖路径。

人工操作按钮时使用较长帧数并传 `--no-auto-demo`；该模式仍验证 document 与 runtime preview 一般不变量，
但不把用户产生的 revision 数量误判为自动 smoke 失败。

document 与 EditorApp 接线切片关闭需要：canonical preview 与 AssetFormat writer bytes 完全一致；GPU viewport 的 Camera、
resolved transforms 与 revision 来自同一 preview World/binding，committed UI rect 正确归一化且不保存 snapshot borrow；非法 edit 原子失败；
subtree 删除；bounded undo/redo、branch replacement 与 history byte failure；旧 schema 拒绝；Editor 2D/3D 公共头
isolation 编译通过；已有文件加载为 clean baseline、加载失败不改变 current/history；文件保存 exact canonical bytes、
覆盖失败不删除旧目标；TinaEditor 自动完成
Move → Apply Transform → viewport drag → Undo → Redo → Save → other workspace → Animation Next/Mode/Undo/Redo/Cook
→ initial workspace。一次 drag 固定报告
begin/preview/commit=`1/2/1`、cancel/reject=`0/0`，只增加一个 document revision；round-trip 固定
`workspaceSwitches=2`、runtime preview instantiations=`8`、document/GPU revision=`7/7`、undo depth=`3`，并证明
inactive session 的 path/loaded/baseline/dirty 未变化。2D delta 固定为 `(2,-1,0)`；3D XZ delta 固定为 `(2,0,1)`，
完整 TRS 在 canonical Prefab、Scene preview 与结构化结果中一致。built-in Catalog smoke 还必须固定报告 entry/load=`8/7`、
Texture/Mesh upload=`1/1`、Sprite/Mesh/Material binding=`1/1/1`、unresolved=`0` 与 resolved 2D/3D=`1/3`。

TileMap root+chunk authoring/cook preview、SpriteAnimationClip timeline authoring/cook 与 Catalog-resolved viewport 已完成。
Timeline 提供 6 槽可滚动窗口、Play/Pause、Prev/Next、Add/Duplicate/Delete、Sprite 切换、重排、逐帧时长、
Once/Loop/PingPong、独立 Undo/Redo 和正式 Cook Preview；2D Player 直接预览已解析 Sprite frame，3D workspace
保留该 dock 但禁用 2D 编辑。2D smoke 固定验证 TileMap layers/chunks/cells/artifacts/emitted sprites=`2/2/12/3/12`、
动画 revision/frame/cook=`4/4/256 B`、Catalog entry/load=`8/7` 和 GPU sprites=`13`。下一产品切片为
Project/Asset Browser、从 Catalog 打开资源及多文档切换；继续只保留现行 schema，不增加旧资产兼容分支。
