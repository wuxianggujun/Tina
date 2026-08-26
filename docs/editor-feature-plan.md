# Tina Editor 功能扩展计划（第一批实施中）

- 状态：`E1/E2/E3 源码与统一 build/test/smoke 已完成，待人工交互验收`
- 日期：2026-08-24
- 当前事实：[Editor 2D / 3D](editor-2d.md)、[Editor UI/UX 路线图](editor-ui-ux-roadmap.md)、源码 `src/editor` / `src/editor_app`
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
  当前持久化布局 fraction/可见性、底部面板、悬浮 Layout Debugger 可见性、snap enabled；主题仍沿用现有 Dark/Compact 默认，Preferences UI 留待后续切片。
- **E3 Recent Projects**：成功 project Catalog switch（覆盖 New/Open/Temporary Save As 的统一提交点）记录 canonical project root，
  最近优先、同路径去重、最多 10 条；Start Center 使用固定行按钮，File 菜单提供 `Open Recent` 子菜单，失效路径会从列表移除并报告错误。

统一验证已完成：`tina_editor_tests` 112/112、`tina_editor_app_tests` 23/23，`tina_sample_2d --frames=300`
与 `tina_sample_3d --frames=30` 均以 `status=ok` 退出。剩余证据是人工交互验收，不新增自动测试。

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
| Copy AssetId / Copy Source Path / Locate Source 因“无平台 clipboard/shell adapter”显式禁用 | `EditorWorkspaceCommands.cpp` |
| Start Center 的 Recent Projects 是写死的“No recent projects”占位，无持久化 | `EditorWorkspaceUiBuild.cpp` |
| 无任何编辑器设置持久化：主题写死 Dark/Compact，snap 步长为固定常量，布局比例不跨会话保存 | `EditorWorkspaceUiBuild.cpp` / grep 无 settings 文件路径 |
| Node registry 只有渲染/相机/灯光/遮挡类节点，无 Physics、Audio、FX、Text 类 authoring 入口 | `world2DNodeTemplateRegistry()`；`src/editor` 内 Physics/Audio 零引用 |
| `Fx2DAuthoringDocument` 公共 API 已存在，但 EditorApp 无可见 FX 面板或消费入口 | [editor-2d.md](editor-2d.md) 已明示 |
| 2D 无 Prefab 工作流：不能从选择创建 Prefab，也不能在 World2D 内实例化 Prefab | Node registry / scene operations |
| viewport 画布无右键上下文菜单（Hierarchy 已有） | grep `viewportContextMenu` 无实现 |
| 无 Undo History 面板；history 深度 32 但不可视 | `EditorWorkspaceState.hpp` |
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
| P1 | E6 Physics2D authoring 节点 | Runtime 能力齐全，Editor 完全没有入口 | World2D schema 扩展（破坏式 bump） |
| P1 | E7 FX2D 面板 | 公共 document API 已就绪，只缺 EditorApp 消费面 | `Fx2DAuthoringDocument` |
| P1 | E8 2D Prefab 工作流 | 复用是关卡生产的核心动作 | World2D schema / 新 Prefab2D 资产决策 |
| P2 | E9 自动保存与恢复 | dirty 内容不再随崩溃丢失 | E2 的持久化目录约定 |
| P2 | E10 Audio 预览与 AudioSource 节点 | AudioClip 已可导入，但听不到也放不进场景 | Runtime AudioEngine 借用；schema 扩展 |
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

**问题**：`Copy AssetId`、`Copy Source Path`、`Locate Source` 三个命令已存在但显式禁用，
禁用原因就是“当前平台层尚未提供安全的 clipboard/file-reveal adapter”。

**提案范围**：

- Platform 层新增两个窄能力：`setClipboardTextUtf8(bounded view)` 与
  `revealPathInFileManager(bounded UTF-8 absolute path)`；Windows 首发（clipboard 走 Win32，
  reveal 走 `SHOpenFolderAndSelectItems`），Linux 后置并按现有 zenity/kdialog 模式返回 `Unsupported`。
- 公共头不出现 `HWND`/GLFW 类型；能力缺失平台保持现有显式禁用 + Tooltip 说明。
- Editor 三个禁用命令接通后删除禁用文案。

**边界**：只写 clipboard 文本，不做剪贴板监听、富文本或文件粘贴；reveal 只接受项目内已校验路径。

### E5 场景节点 Copy/Paste（P1）

**问题**：只有 `Ctrl+D` Duplicate（同文档、原位置旁）；不能把节点子树复制到另一个位置、
另一 parent 或跨 World2D 文档粘贴。

**提案范围**：

- 首切片做进程内 clipboard：Copy 把选中 transformable root 的子树序列化为 current-schema
  canonical bytes（复用 snapshot writer 的实体子集路径），Paste 在当前选中 parent 下重建，
  stable ID 全部重新派生，一次 Paste 一条 revision。
- `Ctrl+C` / `Ctrl+V` / Hierarchy 右键菜单 Copy/Paste；跨 2D/3D 工作区粘贴显式拒绝并提示。
- E4 完成后可选把 canonical bytes 放入 OS clipboard（自定义格式）实现跨实例粘贴；非首切片。

**边界**：粘贴容量受 document capacity 与 history byte budget 约束，超限整体失败保持 document 不变。

### E6 Physics2D authoring 节点（P1）

**问题**：Runtime Physics2D 已覆盖 Box/Circle/Capsule/ConvexPolygon/Chain 与三类 joint，
TileMap 也有 collision 派生，但 World2D authoring 完全没有物理形状的表达；游戏侧只能靠
gameplay blob 或 TileMap 碰撞，无法在场景里摆一个碰撞体。

**提案范围**：

- World2D schema 破坏式 bump：为 entity record 增加可选 Physics payload（body type +
  shape kind + 尺寸参数 + sensor flag），沿用“分类精确映射一个 Node kind、混合 payload
  fail-closed”的现行规则。
- Node registry 新增 `StaticBody2D` / `RigidBody2D`（含 shape 参数）或首切片只做
  `CollisionShape2D`（static-only），由维护者定夺范围；Inspector 按 kind 发布 Physics 区段。
- Viewport 以既有 Line 原语绘制 shape outline（选中高亮），gizmo 缩放联动 shape 尺寸。
- 游戏消费路径与 TileMap gameplay 一致：instantiate 时由 game-owned 逻辑创建真实 Physics body，
  Editor 不直接持有 `PhysicsWorld2D`。

**边界**：不做关节 authoring、不做物理模拟预览（PlaySession 仍是渲染 preview）；旧 schema 拒绝，
一次性同步 cooker/runtime/测试/文档。这是本清单里改动面最大的一项，建议独立 ADR。

### E7 FX2D 面板（P1）

**问题**：`Fx2DAuthoringDocument`（bounded replace/Undo/Redo、39 个 recipe 值、v1 payload）已经
落地并有测试，但 EditorApp 没有任何可见消费面；文档明确“不能把公共 document API 写成已经存在的
图形化 FX 编辑器”。

**提案范围**：

- Project Assets 双击 Fx2D 资产打开 FX document tab（复用固定容量 tab/session 模型）。
- Inspector 呈现分组 PropertyRow（emitter/particle/trail 三组，复用 68px label Grid 与显式 Apply），
  一次 Apply 一条 revision；非法值 fail-closed 语义与现有字段一致。
- 2D viewport 用既有 `ParticleSystem2D`/`Trail2D` 以固定 seed 播放 preview（Play/Restart 按钮），
  preview 从 canonical payload 重建，不持第二份状态。
- 明确不做 node graph；这是参数面板 + 实时预览，effect graph 留待未来独立提案。

### E8 2D Prefab 工作流（P1）

**问题**：3D 侧 Prefab 是既有资产（glTF cook 产物），World3D authoring 直接编辑 Prefab wire；
2D 侧没有任何“把子树存成可复用资产、在多个场景实例化”的路径，关卡里重复结构只能整棵 Duplicate。

**提案范围（需先做资产形态决策）**：

- 方案 A（推荐先评估）：新增 `Prefab2D` cooked asset（entity 子集 wire 复用 World2D record 布局），
  Hierarchy 右键 `Save Subtree As Prefab...`；场景中新增 `PrefabInstance2D` 节点持 AssetId，
  instantiate 展开为运行时实体。编辑传播（修改 prefab 影响实例）首切片不做，实例为展开拷贝。
- 方案 B：只做“Subtree 模板”——把子树存为 `.tworld` 片段文件，Paste-from-file 展开，无实例链接。
  实现成本低但没有资产身份。
- 两案共同边界：不做 nested prefab override、不做 prefab variant；持久身份仍只有 stable ID 与 AssetId。

**建议**：先按方案 B 验证工作流手感（依赖 E5 的子树序列化），资产化（方案 A）单独立项 + ADR。

### E9 自动保存与恢复（P2）

**提案范围**：dirty document 每 N 分钟（settings 可配，默认 5）把 canonical bytes 原子写入项目
`.tina/cache/autosave/<document-key>`；正常 Save/关闭清理对应条目。启动或 Project Open 时发现
autosave 比目标文件新，则 Dialog 提示 Restore/Discard。CrashHandler 报告已能回答“为何消失”，
本项补“内容还在”。快照写入在 owner thread 空闲帧执行，失败只记 Output，不打断 authoring。

### E10 Audio 预览与 AudioSource 节点（P2）

**提案范围**：Project Assets 选中 AudioClip 时 Inspector 提供 Play/Stop 试听（借用 Runtime
AudioEngine 的既有 one-shot 路径，Editor 不新建第二套音频栈）；后续再评估 `AudioSource2D`
节点进 World2D schema（同 E6 一起做 schema bump 以减少破坏次数）。

### E11 viewport 右键菜单 + Camera 预览（P2）

**提案范围**：viewport 右键命中 stable ID 时弹出与 Hierarchy 一致的上下文菜单（Rename/Duplicate/
Delete/Focus/Move to Root + Create Node here）；空白处提供 Create Node/Paste/Frame All。选中
`Camera2D` 时画布角落显示固定尺寸 picture-in-picture 预览（复用现有 preview World 与
RenderNormalizedViewport 机制，View 菜单可关）。

### E12 Undo History 面板（P2）

**提案范围**：底部面板新增 `History` 页（与 Animation/Output 并列），列出 active document 最多
32 条 revision（命令名 + 序号），点击跳转等价连续 Undo/Redo 到该 cursor；只读消费现有 history，
不新增第二份状态。需要 document 侧为每条 revision 附加 bounded 命令标签（当前未保存），
属于小的公共 API 扩展。

### E13 命令面板（P2）

**提案范围**：`Ctrl+P`（或 `Ctrl+Shift+P`）打开模糊搜索 Dialog，列出全部 EditorCommand 的
名称/快捷键/可用态，回车执行；复用既有 command 枚举与 enabled 逻辑，禁用命令显示原因。
同时充当快捷键速查表，降低 F6/F7/F8、Ctrl+0/1/2 的记忆成本。

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
