# Tina Editor 功能扩展计划（第一批实施中）

- 状态：`E1/E2/E3 源码与统一 build/test/smoke 已完成，待人工交互验收`
- 日期：2026-08-24
- 当前事实：[Editor 2D / 3D](editor-2d.md)、[Editor UI/UX 路线图](editor-ui-ux-roadmap.md)、源码 `editor/src` / `editor/app`
- 任务权威：[Roadmap](roadmap.md) 与 [Backlog](backlog.md)；本文条目进入实施前应先在 Backlog 建立正式任务行

本文回答一个问题：Editor 当前闭环之后，下一批**功能扩展**应该做什么、为什么、按什么顺序。它与
[Editor UI/UX 路线图](editor-ui-ux-roadmap.md) 分工明确：那份文档负责视觉与状态反馈升级（`EDITOR-UI-UX-001`），
本文负责 authoring 能力缺口。E1/E2/E3 已从提案进入首批实现；自动证据已收口，跨会话与真实交互验收仍以手动步骤为准，与代码冲突时以源码为准。

## 第一批实现收口（2026-08-24）

- **E1 Tile Palette**：Inspector TileMap 区段使用固定容量 `UIVirtualGridView`，从已加载 Tileset cooked payload
  读取 `localId/materialFlags/UV`；selection 只改变 `selectedTileId_`，Paint/viewport 使用该真实 localId。
  Tileset 缺失或没有 tile 时 Paint 保持禁用。未引入新的 Tileset schema，也未把 palette 选择写成 document revision。
- **E2 设置载体**：Editor 私有 UTF-8 文本 settings，Windows 写 `%APPDATA%/TinaEditor/settings`，Linux 依次使用
  `XDG_CONFIG_HOME`、`$HOME/.config`；schema version=2、固定 10 条 Recent capacity、原子 sibling replace，读取失败回默认值。
  当前持久化布局 fraction/可见性、底部面板、悬浮 Layout Debugger 可见性与 snap enabled；snap 的 translation/rotation/scale 步长仍使用 gizmo 默认值，尚未进入 settings schema；主题仍沿用现有 Dark/Compact 默认，Preferences UI 留待后续切片。
- **E3 Recent Projects**：成功 project Catalog switch（覆盖 New/Open/Temporary Save As 的统一提交点）记录 canonical project root，
  最近优先、同路径去重、最多 10 条；Start Center 使用固定行按钮，File 菜单提供 `Open Recent` 子菜单，失效路径会从列表移除并报告错误。

本次定向验证已完成：`tina_editor_tests` 116/116、`tina_editor_app_tests` 23/23；此前的
`tina_sample_2d --frames=300` 与 `tina_sample_3d --frames=30` `status=ok` 仅作为既有历史证据，未因本切片重跑。
剩余证据是人工交互验收，不新增自动测试。

## 1. 现状小结（2026-08-24 源码核验）

已完成的编辑闭环：World2D/World3D/TileMap/SpriteAnimation 四类 current-schema authoring document、
bounded Undo/Redo、Save/Save As/dirty-close、Hierarchy（拖放/重命名/右键菜单/过滤）、单击与 marquee 选择、
Translate/Rotate/Scale gizmo（World/Local + snap 开关）、2D/3D viewport navigation/grid、隔离 PlaySession、
Project Browser（搜索/过滤/缩略图/右键菜单/双击打开）、事务化 source import（对话框多选 + OS 文件拖放）、
Project New/Open/live Catalog switch、Timeline 动画与 event marker、Output/Snackbar 反馈、CrashHandler 故障报告。

本轮核验确认的**功能性缺口**（区别于视觉缺口）：

| 缺口 | 源码证据 |
| --- | --- |
| TileMap 画刷没有 tile 选择：`selectedTileId_` 按 `localId % 4 + 1` 循环，无 tileset 调色板 | `EditorWorkspaceTileMap.cpp` |
| 场景节点无 Copy/Paste，只有 Duplicate；跨文档/跨会话复制不可用 | `EditorWorkspaceCommands.cpp` |
| Locate Source 因“无平台 shell reveal adapter”显式禁用（clipboard 已落地，两条 Copy 命令已接通） | `EditorWorkspaceCommands.cpp` |
| Recent Projects 已接入 versioned settings、统一 Catalog switch 提交点和 Start Center/File 菜单；仍待跨重启人工验收 | `EditorWorkspaceState.hpp` / `EditorWorkspaceUiBuild.cpp` |
| Editor settings 已持久化布局/可见性、Bottom Panel、Layout Debugger、snap enabled 与 Recent Projects；snap 三类步长和 Preferences UI 仍未落地 | `EditorWorkspaceState.hpp` / `EditorWorkspaceUiBuild.cpp` |
| Node registry 已覆盖渲染/相机/灯光/遮挡、Physics、Audio、FX 类节点；Text authoring 入口仍缺失 | `world2DNodeTemplateRegistry()`；`EditorNodePropertyOperations` 已接入 Physics body/shape Inspector |
| `Fx2DAuthoringDocument` 公共 API 已存在；EditorApp FX 面板源码已接线，待编译与人工交互验收 | [editor-2d.md](editor-2d.md) |
| 2D 无 Prefab 工作流：不能从选择创建 Prefab，也不能在 World2D 内实例化 Prefab | Node registry / scene operations |
| viewport 右键菜单源码已接线；Camera 预览为 look-through 而非 GPU PiP | `EditorWorkspaceViewport.cpp` / View 菜单 `Camera Preview` |
| Undo History 面板源码已接线；点击行等价连续 Undo/Redo | 底部面板 `History` / `AuthoringHistory.hpp` |
| 无自动保存/崩溃后恢复：CrashHandler 只写故障报告，dirty document 内容随进程丢失 | `CrashHandler.cpp` / editor-2d.md 失败语义 |

## 2. 优先级总览

优先级依据：P0 = 阻塞真实 authoring 工作流（用户现在就会撞上）；P1 = 显著提升生产力或补齐引擎已有
runtime 能力的 authoring 面；P2 = 成熟编辑器的体验补强，可在 P0/P1 后再排。

| 优先级 | 提案 | 一句话价值 | 依赖 |
| --- | --- | --- | --- |
| P0 | E1 TileMap Tile Palette | 没有调色板，TileMap 编辑实际不可用于真实关卡 | 现有 Tileset cooked 缩略图链路 |
| P0 | E2 编辑器设置与持久化 | 主题/snap/布局/最近项目全部不可配置、不可记忆 | 无（新增 Editor 私有 settings 文件） |
| P0 | E3 Recent Projects | Start Center 已留位，开箱体验最直接的补强 | E2 的持久化载体 |
| P1 | E4 平台 clipboard/shell adapter | 解锁三个已存在但禁用的命令 + 节点 Copy/Paste | Platform 窄能力 SPI |
| P1 | E5 场景节点 Copy/Paste | 补齐 Duplicate 之外的基本编辑动作 | E4（跨进程可选，进程内可先行） |
| P1 | E6 Physics2D authoring 节点 | 首个 Physics Body/Collision Shape Inspector 属性组已落地；继续补齐完整物理 authoring | World2D current schema 与 `EditorNodePropertyOperations` |
| P1 | E7 FX2D 面板 | 公共 document API 已就绪；EditorApp tab/Inspector/preview 源码已接线 | `Fx2DAuthoringDocument` |
| P1 | E8 2D Prefab 工作流 | Prefab2D Catalog 资产 + PrefabInstance2D 展开已接线；待 compile-only / 人工交互 | E5 子树序列化 / ADR 0068 |
| P2 | E9 自动保存与恢复 | dirty World2D/World3D/Fx2D 空闲帧原子备份；Save 清理；Open 可 Restore | E2 settings 键 |
| P2 | E10 Audio 预览与 AudioSource 节点 | AudioPlayer2D 已可绑定 clip 并授权 Loop；仍缺 Inspector 内试听 | Runtime AudioEngine 借用 |
| P2 | E11 viewport 右键菜单 + Camera 预览 | 对齐 Hierarchy 已有的对象操作语言 | 现有 stable ID 拾取 |
| P2 | E12 Undo History 面板 | 32 步 history 可视化、可跳转 | 现有 document revision |
| P2 | E13 命令面板（Ctrl+P） | 命令可发现性；快捷键教学 | 现有 frame action mapping |

依赖关系上 E2 是多数 P0/P2 项的载体（settings/recent/autosave 共用同一持久化机制），建议最先冻结契约。

## 3. 提案明细

### E1 TileMap Tile Palette（P0）

**问题**：当前 `Tile Paint` 只能画出按 `% 4 + 1` 循环的 localId，用户无法选择要画哪块 tile；
真实 tileset（几十至几百块）无法工作。

**提案范围**：

- TileMap context 激活时，在 Inspector TileMap 区段（或 Left Dock 下方）提供 Tile Palette 面板：
  按当前 Tileset 的 atlas UV 网格展示全部 tile，复用 Project Assets 已有的 cooked Texture 缩略图
  resolve/pin 链路，一个 tile 一个固定尺寸 cell（建议 36 logical px，复用 `UIVirtualGridView`）。
- 单击选中 active brush tile（存入现有 `selectedTileId_`），选中态复用 SegmentedButton/selection chrome；
  画布 hover 时可显示当前 brush tile 的小型预览。
- 后续切片（不在首切片）：矩形填充、按住拖动连续绘制多 cell 合并为一个 revision、吸管（Alt+点击取 tile）。

**边界**：不新增 Tileset schema；palette 只读 cooked Tileset atlas metadata。每次绘制仍是一个完整
root/chunk revision，失败语义不变。

**验收提示**：选中 tile 后 Paint 产出对应 localId；palette 滚动/选择不产生 document revision；
Tileset 缺失时 palette 显示 EmptyState 且 Paint 禁用。

### E2 编辑器设置与持久化（P0）

**问题**：主题写死 `Dark/Compact`、snap 步长是编译期常量、SplitView 比例与面板可见性不跨会话保存、
无 Recent Projects 载体。用户每次启动都从同一硬编码状态开始。

**提案范围**：

- 新增 Editor 私有 settings 文件（建议 `%APPDATA%/TinaEditor/settings`，Linux 用 XDG；strict UTF-8、
  固定容量、版本化 schema、原子 sibling replace 写入，损坏或旧版本时静默回到默认值并重写）。
- 首批设置项：color scheme（Dark/Light，复用 `makeModernDesktopTheme()` 两档）、snap 三类步长
  （translation/rotation/scale）、Left Dock/Inspector/Bottom Panel 的 fraction 与可见性、
  Recent Projects 列表（E3）、autosave 开关（E9 预留）。
- `Edit > Preferences...` 打开 `UIDialog` 设置面板；应用主题复用 TMD-07 已验证的
  destroy-then-rebuild root 交接路径。
- Viewport Snap toggle 的步长从 settings 读取；Inspector 不重复保存副本。

**边界**：settings 属于 Editor 私有，不进公共 `Tina::Editor` 头；不保存任何 document 内容；
读取失败不得阻塞启动。

**验收提示**：改主题/步长/布局后重启进程全部恢复；删除 settings 文件后回到当前默认值；
settings 写入失败只出 Snackbar warning，不影响 authoring。

### E3 Recent Projects（P0）

**问题**：Start Center 已经渲染 “Recent Projects” 标题，但内容永远是占位文本。

**提案范围**：

- Project New/Open/临时项目 Save As 成功后，把 project root 写入 settings（建议上限 10 条，
  按最近使用排序，同路径去重）。
- Start Center 列出可点击的最近项目行（名称 + ellipsis 路径 + Tooltip 完整路径）；点击走既有
  Project Open 校验与 live Catalog switch 流程，校验失败（目录被删/结构非法）时提示并提供
  “从列表移除”。
- `File` 菜单增加 `Open Recent` 子菜单，复用同一数据。

**边界**：路径校验完全复用 `EditorProjectWorkspace` 既有 containment/reparse 规则；列表损坏时
整体丢弃不阻塞启动。

### E4 平台 clipboard / shell adapter（P1）

**状态**：clipboard 已落地；Windows file-reveal 已接线，Linux/mobile/browser 仍返回 `nullptr`。

**已落地范围**：

- Platform 层的 `IClipboard`（`IPlatformBackend::clipboard()`，无该能力返回 `nullptr`）提供
  strict UTF-8 + LF 的读写；GLFW backend 实现，Headless 用公开的 `ProcessLocalClipboard`。
  公共头不出现 `HWND`/GLFW 类型。契约见 [Platform/Input](platform-input.md#剪贴板)。
- `Copy AssetId` 与 `Copy Source Path` 已接通并删除禁用文案。两者的启用条件是真实能力：
  前者要求剪贴板存在且有 context asset，后者额外要求该 asset 有非空 source path —— 生成资产
  没有 source-import owner，提供「复制空字符串」会静默清掉用户剪贴板里的内容。
- 写入失败（Windows 上另一个进程持有全局剪贴板锁是常态）走 `authoringFeedback_` 文案，
  不把整条命令 dispatch 判失败。

**本轮已落地**：

- Platform `IShellReveal`（`IPlatformBackend::shellReveal()`，无该能力返回 `nullptr`）。Windows GLFW
  用 `SHOpenFolderAndSelectItems` 打开资源管理器并选中文件；公共头不出现 `HWND`/`PIDLIST`。
- Editor `Locate Source` 在有 source path 且 backend 提供 reveal 时启用；失败只写 `authoringFeedback_`，
  不把命令 dispatch 判失败。Linux 仍显式禁用。

**剩余范围**：Linux/xdg 文件管理器 reveal；跨进程自定义格式剪贴板。

**边界**：只做剪贴板文本，不做剪贴板监听、富文本或文件粘贴；reveal 只接受 strict UTF-8 绝对路径。

### E5 场景节点 Copy/Paste（P1）

**状态**：进程内 Copy/Paste 已落地。跨 Editor 实例的 OS clipboard 自定义格式仍后置。

**问题（已解）**：此前只有 `Ctrl+D` Duplicate（同文档、原位置旁）。

**已落地范围**：

- `copyWorld2DNodeSubtree` / `pasteWorld2DNodeSubtree` 与 3D 对应 API：Copy 抽出 parent-first 子树，
  Paste 在目标 parent 下重建并重派生全部 stable ID，一次 Paste 一条 revision。
- `Ctrl+C` / `Ctrl+V`、Edit 菜单、Hierarchy 右键 Copy/Paste。跨 2D/3D 工作区粘贴拒绝并提示。
- `CollisionShape2D` 作为粘贴根时要求 physics body parent，与创建规则相同。

**剩余范围**：把 canonical subtree bytes 放入 OS clipboard（自定义格式）以实现跨 Editor 实例粘贴。

**边界**：粘贴遵守 current-schema wire 数量范围、gameplay/history byte budget，失败整体保留 document；不增加独立 document 节点数上限。

### E6 Physics2D authoring 节点（P1）

**状态**：首个 Inspector 属性切片已落地；完整物理 authoring（关节、Polygon/Chain 形状、viewport outline 与 gizmo 尺寸联动）仍是后续工作。

**已落地范围**：

- World2D current schema 已包含 Physics body/shape payload，Node registry 提供 `StaticBody2D`、`RigidBody2D`、`CharacterBody2D`、`Area2D` 与 `CollisionShape2D`。
- Inspector 按 Node kind 发布 Physics Body 与 Collision Shape 分组，可编辑速度、阻尼、重力倍率、Enabled，以及 Box/Circle/Capsule 的尺寸、local center/angle、密度、摩擦、恢复系数和 `Sensor` / `Sensor Events` / `Contact Events` / `Hit Events` 开关；多选显示 `Mixed`，一次 Apply 最多发布一条 `replace()` revision。
- `EditorNodePropertyOperations` 对类型不匹配、非法/非有限值和不适用的形状参数 fail-closed；Undo/dirty 继续复用现有 document revision 流程。Runtime Physics2D 仍由 game-owned instantiate 路径消费，Editor 不直接持有 `PhysicsWorld2D`。

**后续范围**：不做关节 authoring、不做物理模拟预览（PlaySession 仍是渲染 preview）；Polygon/Chain 与 viewport outline/gizmo 尺寸联动待独立切片，并在对应 schema/runtime/测试闭环后更新证据。

### E7 FX2D 面板（P1）

**状态**：EditorApp 消费面源码已接线，待 compile-only / 人工交互验收；不新增测试，不把 FX 做成 pinned tab，也不扩展 Catalog recipe。

**问题（已解）**：此前 `Fx2DAuthoringDocument`（bounded replace/Undo/Redo、schema v3 268-byte payload）已有公共 API 与测试，但 EditorApp 没有可见消费面。

**已落地范围**：

- Project Assets 双击 `AssetKind::Fx2D` 打开未固定的 catalog document tab（复用固定容量 tab/session；隐藏 `{Fx2D, empty AssetId}` owner 作为 last-tab close / project switch 的占位）。
- Inspector 分组 PropertyRow：emitter / particle / trail。提交时机与现有 Inspector 相同——**失去焦点或 Enter**，一次 `replace()` 一条 revision；非法值 fail-closed，no-op 不占 history。不做显式 Apply 按钮。
- 2D viewport 用既有 `ParticleSystem2D`/`Trail2D` 从 canonical payload 以固定 seed 重建 preview；Play/Pause 与 Restart 只驱动 overlay，不另持第二份 authoring 状态。
- Sprite 绑定复用 Project Assets picker / Assign selected；颜色通道编辑后置。

**边界**：明确不做 node graph；effect graph 留待未来独立提案。不增加第 5 个 pinned tab，auto-demo 不自动打开 FX。

### E8 2D Prefab 工作流（P1）

**状态**：方案 A 源码已接线（ADR 0068），待 compile-only / 人工交互验收；nested override / variant 不做。

**问题（已解）**：3D 侧 Prefab 是既有资产，World3D authoring 直接编辑 Prefab wire；
2D 侧需要 Catalog 身份的可复用子树，而不是 explode-and-forget 文件模板。

**已落地范围（方案 A）**：

- `AssetKind::Prefab2D` cooked payload = current-schema World2D snapshot（单根、空 gameplay、禁止嵌套 PrefabInstance2D）。
- `World2DNodeKind::PrefabInstance2D` 使用 PayloadResource；World2D schema 仍为 v9。
- `instantiateWorld2DSnapshot` 展开实例，Editor preview 与 Play 共用；capture 跳过展开子孙。
- `Save as Prefab2D` 把选中子树发布进 Catalog 并用 PrefabInstance2D 替换该子树。
- Project Assets 打开 Prefab2D 为 catalog-backed World2D tab；拖放 / Place 创建实例。
- 2D `.tworld` 文件模板不再是产品路径。不做 nested override / variant。

### E9 自动保存与恢复（P2）

**状态**：源码已接线，待 compile-only / 人工交互验收；不新增测试。

**已落地范围**：

- Settings 增加 `autosave`（默认开）与 `autosaveMinutes`（默认 5，范围 1–60）；缺省键用默认值，不 bump settings schema。
- 已打开项目时，owner thread 空闲帧把 dirty 的 World2D snapshot、World3D Prefab payload 与已打开 Fx2D payload 原子写入 `<project>/.tina/cache/autosave/<stem>`。失败只写 Output，不打断 authoring。`--auto-demo` 与 Play 期间不写。
- 成功 Save 或 Dirty-close Discard 删除对应条目。
- Project Open / 启动发现比目标文件更新（或文档尚无路径）的 autosave 时弹出 Restore/Discard。Restore 把 bytes load 进 document 并保持 dirty；Discard 删除备份。TileMap 目录包与 SpriteAnimation cooked 文件本切片不做。

**剩余范围**：TileMap/SpriteAnimation autosave、可配置 Preferences UI、把间隔接到 Preferences。

### E10 Audio 预览与 AudioSource 节点（P2）

**状态**：试听源码已接线，待 compile-only / 人工交互验收；不新增测试、不 bump World2D schema。

**问题（部分已解）**：AudioPlayer2D 可绑定 clip 并授权 Loop，但 Inspector 不能试听。

**已落地范围**：

- Inspector 在选中 Project Assets 的 `AudioClip` 或场景里的 `AudioPlayer2D` 时发布 Play/Stop。
- 借用 Host `FrameUpdateContext::audioEngine()` 的 `playPcm` / `enqueueStop`，不另建第二套 AudioEngine。
- AudioPlayer2D 跟随节点 Loop；Asset 试听为 one-shot。PCM 继续借用 Catalog 已加载的 cooked payload。
- `TINA_BUILD_AUDIO_MINIAUDIO` 打开时把 miniaudio device attach 到同一台 Host engine（交互用 OS 后端，
  `--auto-demo` / `--frames` 用 null backend）。没有 device 的构建仍可排队播放，但听不到声。
- Play 期间禁用试听。Catalog/project switch 先 Stop。

**剩余范围**：不新增 `AudioSource2D` 节点，不 bump schema。空间化/3D 衰减不在本切片。

### E11 viewport 右键菜单 + Camera 预览（P2）

**状态**：源码已接线，待 compile-only / 人工交互验收；不新增测试。

**问题（部分已解）**：viewport 画布此前没有右键菜单（Hierarchy 已有）；选中 `Camera2D` 也看不到它实际
覆盖的画面。Runtime `ExtractRenderSceneFromWorld` / `RenderSceneWriter::setCamera2D` 每帧只允许一台
active Camera2D，因此无法在同一 GPU 帧里做真正的 picture-in-picture。

**已落地范围**：

- 2D/3D viewport 右键：位移小于 5 logical px 视为 click，打开与 Hierarchy 对齐的上下文菜单。命中
  stable ID 时提供 Rename / Duplicate / Delete / Focus / Move to Root / Create Node Here；空白处
  提供 Create Node / Paste / Frame All。3D 右键拖动仍是 orbit；2D 右键不平移（中键仍 pan）。
- 空白处 Paste 允许 parent 0（场景根）。`CollisionShape2D` 粘到根仍由既有 paste 规则 fail-closed。
- View 菜单 Check `Camera Preview`（默认开，仅 World2D）。选中 `Camera2D` 时 editor 相机跳到该节点的
  位置与 view height（look-through），画布角落 badge 显示 `Looking through Camera2D`；取消选择或关闭
  开关后恢复进入前的 pan/zoom。不是第二路 GPU 视口。

**剩余范围**：真 PiP 需要 Runtime 允许多 Camera2D 同帧 extract，不在本切片改 Runtime。

### E12 Undo History 面板（P2）

**状态**：源码已接线，待 compile-only / 人工交互验收；不新增测试。

**问题（已解）**：history 深度 32 但不可视，用户只能盲 Undo/Redo。

**已落地范围**：

- 五类 authoring document 为每条 retained revision 保存最多 64 UTF-8 字节的 in-memory 标签
  （`setPendingHistoryLabel` / `historyLabelAt`）。标签不进 snapshot/cooked wire，不 bump schema。
  下一次成功 commit 消费 pending 标签；no-op 与失败会清掉，避免串到无关编辑。
- 底部面板新增 `History` 页（与 Animation/Output 并列）。列出 active document 最多 32 条 revision
  （序号 + 命令名，最新在上），当前 cursor 为选中行。点击一行等价连续 Undo/Redo 到该 cursor，
  不另持第二份状态。Play 期间列表禁用。
- Settings `bottomPanel=history` 为加性键，不 bump schema。

**剩余范围**：不为每条 revision 存完整 diff；命令标签覆盖常见 authoring 命令，未点名的 mutation 显示 `Edit`。

### E13 命令面板（P2）

**状态**：源码已接线，待 compile-only / 人工交互验收；不新增测试。

**问题（已解）**：命令与快捷键分散在菜单/工具栏，F6/F7/F8、Ctrl+0/1/2 依赖记忆。

**已落地范围**：

- `Ctrl+P`（`Ctrl+Shift+P` 相同）打开 Command Palette Dialog。Help 菜单也有入口。
- 列出用户可调用命令（菜单、快捷键、工具栏），不含 Dialog 内部 Confirm/Cancel 或 Inspector
  字段提交。每行是名称 + 快捷键；不可用命令仍可见，并附原因。
- 输入做 ASCII 大小写不敏感的子串/子序列过滤。Enter 或 Run 执行选中且可用的命令；Escape 关闭。
  上/下方向键移动选择。执行前先关面板，因此 Add Node 等可以接着打开自己的 Dialog。
- Play 期间仍可打开当速查表；authoring 命令显示为不可用。

**剩余范围**：不做模糊拼音/正则；不把 Inspector 逐字段 command 放进面板。

## 4. UI 优化补充清单

视觉与状态反馈的主计划在 [Editor UI/UX 路线图](editor-ui-ux-roadmap.md)（EmptyState、AssetTile、
DropOverlay、Hierarchy/Inspector/Viewport/Timeline/Output 对象语言等，多数已 InProgress）。
以下是本轮源码走读发现、且该文档尚未覆盖或值得提级的 UI 项：

1. **Light 主题可选**（归属 E2）：`makeModernDesktopTheme()` 本身支持 Light，Editor 却写死 Dark；
   设置面板接通后即可提供，Dark/Light 视觉矩阵证据也是 `EDITOR-UI-UX-001` 待办的一部分。
2. **Play 模式的全局视觉状态**：PlaySession active 时大量控件被锁定，但除按钮态外缺少一眼可见的
   “正在 Play” 信号；建议 viewport 边框 tint（teal 1-2px）+ Status Bar Playing badge，复用现有
   Theme token，不新增状态机。
3. **Snap 状态可读性**：Snap 目前只有 icon toggle；步长可配置（E2）后，Tooltip 应显示当前步长
   （如 `Snap: 0.5 m / 15° / 0.1x`），避免用户猜测吸附粒度。
4. **TileMap 模式的画布光标反馈**（归属 E1）：Paint/Erase 激活时 hover cell 高亮 + brush tile 预览，
   与 palette 选中态共用 selection 色。
5. **多选摘要的动作入口**：多选时 Status Bar 已显示 `N selected | Group pivot`，但对齐/分布类批量
   操作缺失；可先只加 `Align X/Y`（对齐到 group pivot），作为 gizmo 群体事务的低成本延伸。
6. **Inspector 数值输入手感**：TextEdit + 显式 Apply 正确但偏重；建议评估“按住 label 左右拖动改值”
   （drag-to-scrub，提交时机与 gizmo 相同：释放时一次 replace），保持显式 Apply 作为键盘路径。
7. **Output 面板入口状态**：有新 Error/Warning 而面板收起时，Status Bar 的 `Output` 按钮应带计数
   Badge（复用 `UIBadge`），否则失败反馈依赖用户主动打开面板。
8. **文档 Tab 的 dirty 标识一致性**：pinned session 折叠后，dirty 状态只能从 Save 按钮 enabled 态
   推断；建议 Command Bar 的 Save 按钮 Tooltip 带上 active document 名称与 dirty 状态文本。

## 5. 建议推进顺序

```text
第一批（开箱可用性）：E2 settings 契约冻结 -> E1 Tile Palette -> E3 Recent Projects
第二批（编辑动作补齐）：E4 clipboard/shell -> E5 Copy/Paste -> E7 FX 面板
第三批（schema 扩展，单独 ADR）：E6 Physics2D 节点（可与 E10 AudioSource 合并一次 bump）
第四批（体验补强）：E8 Prefab（先方案 B）-> E9 自动保存 -> E11/E12/E13
```

每批仍遵循 Editor 现行“大功能闭环后统一验证”节奏；进入实施前在 [Backlog](backlog.md) 建立
带验收条件与证据类型的正式任务行，本文不替代任务状态源。

## 6. 明确不在本计划范围

- 不做 effect/shader node graph、可视化脚本或蓝图系统。
- 不做多窗口/可拆卸 Dock、自由 docking 框架；现有三层 SplitView 结构不变。
- 不做通用资产数据库、网络协作、版本控制集成。
- 不为 Editor 引入第二套 UI 树、第二份业务状态或 editor-only wire 格式。
- 不承诺 Linux 平台能力先行；Windows 首发、Linux 按现有 `Unsupported` 显式禁用模式跟进。
