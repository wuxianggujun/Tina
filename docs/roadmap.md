# Tina Roadmap

Roadmap 只表达优先级窗口，不保存逐提交流水。可执行任务、依赖和验收条件统一维护在
[Backlog](backlog.md)；历史切片可从 Git 与 ADR 追溯。

## 状态规则

- `Now`：当前应优先关闭，通常包含契约冲突、产品门禁或迁移残留；
- `Next`：Now 关闭后进入，不与 P0 工作抢占验证资源；
- `Later`：方向成立但未承诺排期；
- `Done`：已经满足当时验收条件，后续扩展不改变其历史完成状态。

任务状态与证据强度分开：实现完成、测试通过、产品 smoke 和视觉验证是不同结论。

## Now：契约一致与产品收口

| Backlog | 目标 | 为什么现在做 |
| --- | --- | --- |
| UI-002 | 收口 Windows UIA：tip 跨进程 gate 证据已固化；完成 Narrator/Inspect 人工金标后关闭 | 自动 HWND client gate + unit 已在 tip 复现；只剩操作员读屏/Inspect 清单 |
| UI-STUDIO-DESIGN | 收口 `Tina Studio Compact` 设计系统：Tonal 默认 Button、Primary/Danger/Tonal/Outlined/Text/Segmented recipes 与 Editor 旧 disabled-active 视觉移除已合并主线，单测/样例/Style visual gate 全绿 | 默认 role 翻转已进主线，视觉回归风险应尽快闭合；只剩 `RunUiStateFeedbackVisualGate.ps1` 需干净门禁机补跑 |

Now 的退出条件：UIA 属性、fragment 与 Invoke/Toggle/RangeValue/Value action 的跨进程结果可复现；Narrator/Inspect
人工记录明确；没有未解释的 Accepted ADR/实现冲突。Linux AT-SPI 作为独立后置项，不阻塞 Windows
UI-002 关闭。交互状态矩阵的 Dark/Light 产品视觉证据已完成；即时反馈与 Motion 的文档边界保持明确。
UI-STUDIO-DESIGN 的代码与自动证据已固化，剩余 state-feedback 视觉门禁依赖可独占合成指针输入的干净
门禁机；两项 Now 的残留环境依赖统一见 [Backlog](backlog.md) 的“交接：宿主与环境依赖”。

## Next：产品验收、性能基线与新能力 lane

| Backlog | 目标 |
| --- | --- |
| UI-MODERN-DESKTOP-001 | InProgress：TMD-00..07 已闭环；TMD-08 Desktop Shell reference 已实现单 root、五 band、三层嵌套 SplitView、正式 TabView、Menu/Dialog/Tooltip、Splitter、产品 icon atlas 与响应式档位，并通过自动工作流矩阵；真实 DPI 专用门禁已就绪，100%/150% 配对证据暂缓。TMD-09 的 2D/3D metrics 与 EditorApp Compact 迁移、TMD-10 OS scheme 接线已完成，并通过 2026-08-19 集中 build、Platform/Runtime/UI/Editor gate、产品 smoke 与 installed DesktopBootstrap consumer gate。TMD-11 已通过 `tina_bench_tests` 10/10 及 Static/Component/Style/Motion 0/64/1024 active-track 冻结 workload 确定性 gate，开发机墙钟保持 provisional，fixed-machine hard gate 由 PERF-002 跟踪；现待真实 100%/150% DPI 配对视觉与最终文档收口 |
| UI-003 | 跨 DPI/GPU 容差视觉门禁 |
| PERF-002 | 固定机 benchmark hard gate、多进程 median/MAD 与受审 baseline |
| 2D-EDITOR | 2D/3D Editor 产品闭环：四类 current-schema authoring document/file、bounded history、per-tab session、Save/Save As/dirty-close、Project Browser/New/Open + live Catalog switch、事务化 source import 与一步 PNG/JPEG/WAV 导入、Godot 式 Node registry/Hierarchy 拖放、`EditorNodePropertyOperations` 节点专属属性、TRS Inspector、viewport navigation/grid/gizmo/marquee、PlaySession、frame shortcuts 与 Line/Ellipse 视口视觉均已落地；旧 Components/Add/Remove 与空节点包装 API 已破坏式删除，实现明细与本轮证据见 [Backlog](backlog.md)。**2026-08-24 Windows 统一证据：** `tina_editor_tests` 112/112、`tina_editor_app_tests` 23/23、`tina_tests` 377/377；`TinaEditor.exe --auto-demo --frames=70` 的 2D/3D smoke 均 exit 0。**待：** 跨 DPI/GPU 视觉金标与 Linux `zenity`/`kdialog` 真实 dialog 产品门禁；完成前保持 InProgress |
| EDITOR-UI-UX-001 | InProgress：Project Assets 已接入 ASCII case-insensitive name/kind 搜索、All/2D/3D/Media 组合过滤、stable key/selection 与 Grid/List 固定 metrics（约 132x72 / 280x30）；Texture/Sprite 使用 cooked 缩略图，其他类型使用类型 icon fallback，并显示 secondary label/status slot 与 Missing 语义；Breadcrumb、Start Center、EmptyState、导入 Activity phase 文本、失败 InlineCallout、Retry/Open Output、双击按 AssetId 打开、anchored 右键菜单与稳定 AssetId 命令已接入；Source Imports 已为 `Kind | Source | Status` 三列（Source 约 190），并按稳定 AssetId 记录 `Queued/Preparing/Copying/Cooking/ReadyToCommit/Committed/Error`，只标记当前批次已知输出，Catalog commit 后补齐新输出并在项目切换时清理。GLFW 3.4 FileDrop 已经由 Platform→Runtime→Editor 有界链路覆盖 Windows、X11 与 Wayland ingress，把文件释放到 Project Assets 或 active viewport 都自动导入资源，且不会创建或修改场景节点；失败保留旧 Catalog。**2026-08-24 Windows 统一证据：** `tina_editor_tests` 112/112、`tina_editor_app_tests` 23/23、`tina_tests` 377/377，2D/3D 70 帧 smoke 均 exit 0。**当前限制：** 新资源在 worker 返回 validated outputs 后才获得 AssetId 级历史；标准 GLFW callback 仅提供最终 drop/release，没有 drag-enter/drag-over hover。**待：** Linux 构建/运行与真实产品拖放证据、Explorer 人工拖放，以及 Dark/Light、DPI、UIA、跨 GPU 视觉证据；完成前不得标记 Implemented |
| UI-PAINT-002-A | Done：`UIBoxPaint`/Canvas `SolidRect` 的 `UILogicalCornerRadii` 已沿 committed paint、border/inset/shadow 与 UI→Render bridge 进入已有 `UIPixelCornerRadii`；UI Showcase 以两个仅圆外侧角的相邻 Radio 和一个四角不同的 Canvas preview 固化 consumer。复用现有固定容量，失败保留旧 snapshot；2026-08-17 Windows gate：UI 672/672、Runtime UI 130/130、UI-Render 28/28、bgfx 111/111、bench 10/10，Showcase 120 帧 exit 0 |
| RENDER-LINES-001 | UI Line/Ellipse 原语与 Editor 视口 grid/gizmo/rotation ring 已落地，100%/150%/200% DPI 证据完成；剩跨 GPU 证据随 UI-003 关闭 |
| EDITOR-ANIM-EVENT-TIMELINE | Done：per-frame notify marker 显示、CRUD、tag/offset 解析、稳定排序、Undo/Redo 与 fail-closed 已落地；Editor tests 与 2D/3D 60帧 auto-demo gate 已闭环 |
| 2D-ANIM-EVENTS-PRODUCT | Done：product-2d 消费 `crossedEvents`；300帧 gate 固化 footstep/hit=`15/1`、overflow/unknown=`0/0`（字段现由 schema 29 继承） |
| TEXT-001 | InProgress：多行、UAX #29 grapheme 子集、二维 hit/navigation 与 Windows IMM32 caret/candidate placement 的代码和自动 gate 已完成；BiDi/复杂 shaping、Linux 原生 XIM/Wayland 与 Windows 真机 IME 人工矩阵仍待收口 |
| NAV-COOK-001 | Done：Cooked NavigationGrid2D v1、typed load、Editor bake/persistent overlay 与 product bit-exact 双路径通过统一模块/Editor/product gate |
| FX-ASSET-001 | Done：Fx2D v1、完整 recipe、typed dependency、Scene factory 与 Editor document 通过统一 gate；GPU simulation 留 Later |
| PHYS2D-CHAIN | Done：static open/loop Chain、多 segment 生命周期/query 去重通过 Physics2D 49/49 与产品 ready gate |
| ASSET-SEC-002 | Done：glTF 之外全部 cooked payload 的资源炸弹/malformed 矩阵已补齐，`tina_asset_format_tests` 124/124、corpus 17/17、`tina_asset_tests` 312/312 |
| UI-MOTION-002 | Done：keyframe timeline、bounded `LayoutWidth`/`LayoutHeight`/`LayoutOffset` 与 atomic Layout/Hit/Paint publication；UI 28/28、Runtime facade 1/1、bench unit 10/10 及 paint/layout seed 0/1/2 通过 |
| UI-PERF-001 | Done；含 `ui_motion_v1` 在内的 UI workload 集已齐 |
| SDK-001 | Done：可安装 Tina Game SDK、版本化 CMake package 与外部 `find_package` consumer gate；ADR 0024 Accepted，pre-1.0 strict exact-version 正反 probe 已接入 |
| UI-FLOW-001 | Done；固定容量 Layer/Screen 栈、Back/Confirm/Menu Action Router、16 槽本地用户、Gamepad assignment、per-user 设备 revision 与 2D 产品已闭环 |
| UI-COMPONENT-001 | Done；标准 Behavior 独立 side store、phase-scoped bounded transaction、全池 reservation/counter 与 `ui_component_build_v1` 已落地 |
| UI-IMAGE-001 | Done；A Image/强类型 `UIIconContent`、B NineSlice 与 C 产品/失效/尺寸/`ui_image_nineslice_v1` 性能证据均已关闭；UIIcon 只在 authoring/语义层区分，复用 Image storage/资源链/atlas/GPU pipeline，不另建 Widget/Asset 系统 |
| UI-TOOLTIP-001 | Done；独立 Tooltip contract、显式 Anchor、Hover/Focus/Manual monotonic delay、Auto/flip/clamp、单 Window 独占、Ignore hit、输入/Anchor/Modal dismissal、committed metrics/失败回滚、HelpText fallback 与 Runtime facade 已闭环 |
| UI-VISUAL-COMPONENTS-001 | Done；Surface/Divider/Badge 强类型无状态视觉 profile 与 Switch 已落地；前三者复用普通 Element/Theme/paint，Switch 复用 Checkbox Toggle/input/action/capacity 并增加 track/thumb chrome、Switch semantics 与 UIA TogglePattern，无第二套 state/runtime/render pipeline |
| UI-MENU-001 | Done；独立 Menu/MenuItem recipes、固定容量 state/layout/input 模块、显式 Anchor、单 Window transient overlay 与 Popup 协调、Command/Check/Radio/Separator、四向 Auto/flip/clamp、Pointer barrier、Keyboard/Gamepad/accessibility 共享激活、Menu/MenuItem UIA、committed metrics 与 Runtime facade 已闭环 |
| UI-SPLITVIEW-001 | Done；强类型 SplitView/Splitter recipes、固定容量 state/layout/input 模块、三 direct child 同 root parts、横纵 orientation/minimum/fraction clamp、committed metrics、Pointer Capture/RangeInput keyboard、Slider semantics/UIA SetRangeValue、generation/root/capacity cleanup 与 Runtime facade 已闭环 |
| UI-TABVIEW-001 | Done；独立 TabView/Tab recipes、固定容量 state/layout/input 模块、完整 direct-child Tab/Panel pairs、四向 placement、Automatic/Manual activation、Pointer/Keyboard/Gamepad/accessibility 共享路径、TabList/Tab/TabPanel semantics、专属 `UITabPaint`、committed metrics 与 Runtime facade 已闭环 |
| UI-STYLE-001 | Done；StyleClass/ColorToken、reverse-dependency、imageTint、dirty metadata、showcase Integration、`RunUiStyleVisualGate.ps1` 已落地 |
| UI-MOTION-001 | Done；paint-only color/opacity/radius/visual-offset + reduced-motion + Style BackgroundColor reservation/activation + `ui_motion_v1` |
| METRICS-001 | Blocked：Runtime Metrics 固定容量 counter registry 首切片；[ADR 0027](adr/0027-runtime-metrics-registry.md) 为 Proposed，待 maintainer 确认关键决策（D1-D8）后开工，确认前不建立占位 API |

UI 框架主线按以下顺序推进；这是 UI lane 的依赖顺序，不阻塞 Asset、Render、UIA 等其他 lane 并行：

```text
UI-RANGE-INPUT-KEYBOARD Done
  (independent capability command + exact-control Down/Up latch; no spatial-focus state reuse)

UI-STATE-FEEDBACK Done (Windows Dark/Light visual gate 22/22)
  -> ADR 0023 Accepted (capacity/failure/performance contract)
  -> UI-PERF-001 Done
       |-> UI-IMAGE-001 A Done: Image/Icon + resolver/pin + RGBA ImageQuad + semantics
       |     -> B Done: NineSlice 1..9 quad atomic expansion
       |     -> C Done: product/failure/size adoption + ui_image_nineslice_v1
       `-> UI-COMPONENT-001 Done: complete pool reservation + ui_component_build_v1
UI-IMAGE-001 Done + UI-COMPONENT-001 Done
  -> UI-STYLE-001 Done
  -> UI-MOTION-001 Done

UI-FLOW-001 Done
  -> Layer/Screen stack + 2D Pause product path implemented
  -> Pause Back/Confirm Action Router implemented
  -> per-user input-device hints + 2D Primary-user Pause prompt implemented
  -> Pause Confirm routing + focused-control precedence implemented
  -> Base/Pause Menu routing + TextEdit-first printable P implemented
  -> fixed 16-slot local users + full-generation Gamepad assignment implemented
  -> reassignment/disconnect/reset routing and latch cleanup implemented
SDK-001 (independent packaging lane; does not wait for UI-FLOW-001)
```

`UI-IMAGE-001` 与 `UI-COMPONENT-001` 两条无直接依赖的 lane 均已完成：前者关闭 Image/强类型 UIIcon/NineSlice
产品与性能证据，后者关闭完整 component reservation/counter 与 `ui_component_build_v1`。汇合后的
`UI-STYLE-001` 与 `UI-MOTION-001` 也已完成：stylesheet imageTint、属性 dirty metadata、Integration/Visual
门禁、paint-only Motion 与 `ui_motion_v1` 均已落地。
`UI-RANGE-INPUT-KEYBOARD` 已独立关闭且不依赖 ADR 0023；已完成的 Image/Component 均未复制其输入状态。
`UI-PERF-001` 已完成，Component、Image、Style 与 Motion 均已扩展同一 counter/checksum 协议。
固定机绝对时间阈值仍由 `PERF-002` 冻结；在此之前 clean-frame rebuild、容量、
分配和 checksum 等确定性不变量可以阻断，开发机墙钟只报告 `provisional`。`UI-FLOW-001` 已关闭：
Layer/Screen、Action Router、多本地用户与设备提示都复用窗口级唯一 retained tree/focus，不引入第二棵 UI 树。
`SDK-001` package/consumer gate 已收口；后续新增公共 UI 切片仍需同步扩展 consumer gate，正式 supported
ABI tuple 的 artifact/API/symbol baseline 与 previous-object probe 作为 release checklist 独立跟踪。

## Later：扩展能力

- Jolt 3D physics adapter；
- `UI-PAINT-002-A` 已完成逐角 Retained box/Canvas chrome；后续另行推进 rounded/stencil 子树 clip、backdrop/blur、per-corner Motion 与更完整的视觉效果；
- Back/Confirm/Menu 之外的任意产品 action-id；
- 仅在标准 Behavior 不足时评估 startup-only 自定义 Behavior SPI；
- Linux AT-SPI adapter 与真实辅助技术验收；
- Linux Editor native dialog 定向编译与 `zenity`/`kdialog` 真实产品门禁（Windows/Linux adapter 与 Editor authoring/保存/导入闭环均已落地，明细见 Backlog `2D-EDITOR`）；
- Linux 原生 XIM/Wayland IME 与文本输入平台矩阵（`TEXT-001` Windows IMM32 自动接线之后）；
- FX GPU simulation（`FX-ASSET-001` 完成 asset/editor 化后评估）；
- 3D post-process（HDR/tone mapping）与跨 GPU golden（transparent pass 已由 `RENDER-002-TRANSPARENT` 闭环）；
- layout whitelist 扩展，以及 loop/seek/pause/repeat/yoyo/completion callback、spring/inertia 等高级 Motion playback；
- `MeshRenderer3D` / `SkinnedMeshRenderer3D` 的 LOD 与剩余 instancing 扩展（static/skinned sphere-frustum culling 已在 resolver 前完成；opaque static 当前仅对 mesh/material/submesh/doubleSided 相同的连续 item 合批并以 bgfx instanced draw 提交，transparent static 与 skinned path 仍逐 item draw）；
- Asset Bundle/Patch、cache/LRU 与 network Asset；

Later 项进入 Now 前必须先补清楚产品场景、容量边界、失败语义和验收命令，不能只按功能名称开工。

## Done：已关闭工作

| 阶段/任务 | 完成结果 |
| --- | --- |
| RENDER-002-TRANSPARENT | Material/Scene/Prefab/RenderScene/Null/bgfx 的显式 `Opaque`/`Blend` 与统一 static/skinned back-to-front 全序已贯通；pass 固定 CSM×4→Spot→Point×6→Opaque3D→Transparent3D→Sprite2D→UI。2026-08-15 schema 16 集中 Windows gate 的11个 GoogleTest executable、300帧主 sample、透明 on/off RGB 差分及既有 IBL/skin-animation/point-shadow A/B 全部通过，raw capture 已回收 |
| 3D-SKIN-001 | A1-A4 已关闭：SkinnedMesh/AnimationClip3D wire/cook/typed、joint remap、`SkinnedMeshRenderer3D`/`Animator3D` CPU pose、packet-local palette 与 bgfx GPU skinning 贯通。2026-08-14 schema 15 集中 gate 的11个 GoogleTest executable 全部 exit 0；300帧固定 total/static/skinned mesh=`3/2/1`、joints=`2`、skinned Prefab instances=`3`、pose changes=`300`、packet fingerprint changes=`299`；30帧 skin on/off fingerprint 各自稳定且跨模式不同，RGB L1 达到门槛 |
| 2D-ANIM-EVENTS | SpriteAnimationClip 收敛为唯一现行 schema v2 notify events（非零 u32 tag + u16 定点帧内 normalized offset，每帧64/单 clip 16384 上限）；recipe `#tag@offset` 后缀；`SpriteAnimator2D::update()` 零分配上报 `crossedEvents`（半开区间、Once 终点闭合、PingPong 镜像反向、overflow 截断）；v1 拒绝无迁移 |
| 2D-SERIALIZATION | World2D snapshot 唯一现行 schema v4（448-byte entity record，固定容量 UTF-8 节点名）（32-byte header、256-byte entity，含 SpriteAnimation 绑定；4096 entity/4 MiB gameplay blob 上限）；capture 确定排序并拒绝3D有损写入，restore 全量预检、失败 subtree rollback；旧 schema 拒绝 |
| 2D-TILEMAP-PRIORITY-GEN | 同 chunk demand 聚合最高 priority 并按确定顺序消费 IO budget，不抢占已 dispatch IO；Editor 从 visible object layer 生成 bounded spawn plan，经 game-owned encoder 单次发布 World2D gameplay revision |
| 2D-NAV | 独立 `Tina::Navigation2D` 的 immutable weighted grid、generation blocker/revision、确定性四向/对角同步/分步 A*、严格/允许切角、path cost 与 TileMap exact material-cost/property Rectangle 转换已落地；`PhysicsNavigationSync2D` 将显式注册的 Physics body-local AABB 按权威 transform 保守栅格化为动态 blocker。当前 product-2d schema 29 的静态 Cooked grid 记录 solid/rectangle/blocked=`11/0/11`、weighted/max-cost=`1/5`，运行时 crate blocker 使 registered/published=`1/1`，基础/动态/严格/切角 path cells/cost=`5/40`，独立 weighted path=`7/60` 且绕开高代价 cell，并保留单步推进、取消与 revision/mutation=`10/2` |
| ASSET-002 | manifest watcher hint + revision polling、immutable Catalog/import planner、fresh-stage validation、current-only import state/provenance、mixed dirty-unit executor 与 all-clean 零改写复用已落地；`reloadCatalog()` 以双驻留 CPU generation staging 和显式 Sprite/Mesh participant prepare/commit 原子发布 Catalog/index/active GPU binding，active frame 与 prepare failure 在发布前全局回滚，旧 Lease/GPU owner 保持可重试 retirement |
| UI-RANGE-INPUT-KEYBOARD | capability-shaped Decrease/Increase command、Keyboard Arrow/Gamepad D-pad Runtime 映射与 fixed-capacity exact-control Down/Up latch 已落地；Slider 调值复用 Pointer/UIA 的量化 mutation/callback，read-only/边界值不修改、不误触发空间焦点且 Gameplay transition 保持可见 |
| UI-STATE-FEEDBACK | Slider、Checkbox/RadioButton、List/Tree 与 TextEdit 复用唯一交互/焦点状态源并完成 stale-state 清理；Windows MSVC/bgfx/FreeType Dark/Light 产品门禁22项差分检查全部通过，证据见 [UI-STATE-FEEDBACK Windows Evidence](ui-state-feedback-evidence-windows.md) |
| UI-ELEMENT-AUTHORING / ADR 0022 | authoring 统一为 descriptor/recipe `createElement()`；Flex/Overlay、committed content placement、显式 Semantics/Merge/Exclude、Theme role/override reset、固定容量 build transaction 与 bounded Canvas `SolidRect` 已落地；公开 `UIWidgetKind`/create-by-kind surface 删除；UI/Runtime UI/UI-Render/FreeType/UIA/bgfx 直接测试、Showcase Dark/Light smoke/capture、2D/3D smoke 与 UI-003 baseline gate 通过 |
| M0～M5 | C++23 构建基线、设计审计、ADR 与依赖方向建立 |
| M6 | Headless Runtime、`EngineHost`、`IGameApplication`/`IGameState` 生命周期 |
| M7 | Platform/Input、WindowSurface、Desktop/bgfx、Retained UI 核心与 UI DisplayList |
| M8 | generation Scene World、Transform 与 2D extraction |
| M9 | 3D extraction、bgfx Opaque3D/Sprite2D fixture |
| M10 | Catalog/Cooked、AssetSystem、Handle/Lease、Task、GPU upload、TileMap 与正式 2D sample |
| M11 | Physics2D、Audio/miniaudio、UI 设置/文本、StaticMesh/Material/Prefab/glTF 3D 产品路径 |
| M12 | Legacy `Tina.exe`、旧横版 2D、旧 UI 产品图与 `src/vnext` 前缀删除 |
| DOC-001 | README/架构/设计/构建/测试/任务职责重组完成，Backlog 成为未完成工作的唯一明细，一致性扫描通过 |
| 2D-TILEMAP-LAYERS / N1 | TileMap schema v2 有序 tile/object layers、非零唯一稳定 ID、visibility/UTF-8 properties、point/rectangle；recipe 单一显式 block 语法；runtime render/chunk/collision 显式 layer ID；sample 消费 layer 30 的 object 101/102；发布前验证 Tileset dependency/localId |
| 2D-PHYSICS-EXPAND / N2 | Body/Shape/Joint 独立 generation；Box/Circle/Capsule 与多 shape/body；sensor enter/exit；Distance joint；body 级联 retirement；TileMap bridge 与产品 sample 迁移；模块测试及 300 帧 sensor/joint 证据 |
| 2D-PHYSICS-CHUNK-COLLIDER | TileMap→Physics 收敛为 `TileMapPhysicsSync2D`：per-resident-chunk static collider、确定性 greedy rectangle 合并、residency/revision 增量同步、staged-then-retire 事务与失败保留旧 collider、`Create()` 后稳态零分配；逐 cell 同步 API 已删除。product-2d `physicsStaticBodies=1`（11 solid cell → 2 box shape），字段由 evidence schema 29 继承 |
| RENDER-001-NLIGHT / N4 | Opaque3D lighting 收敛为唯一 `Mesh3DLightingDesc`，有界0..4 directional lights；Null/bgfx/shader/sample/test 同步；产品一次提交3灯 |
| RENDER-001-SCENE-LIGHTS | `DirectionalLight3D` 进入 World；最多4个 active component 按稳定 Entity identity 转换 world direction/color×intensity，逐帧深拷贝为 RenderScene snapshot；Null/bgfx 与 product-3d schema 5 证明3灯连续300帧消费 |
| RENDER-001-POINT-LIGHTS | `PointLight3D` 进入 World；最多8个 camera-affecting active component 按稳定 Entity identity 转换 world position/radius/color×intensity；PerspectiveCamera3D sphere-frustum culling 先于容量检查；Null/RenderScene/bgfx/shader 与 product-3d schema 6 证明 authored/committed/culled=`3/2/1` 连续300帧消费 |
| RENDER-001-PRODUCT-RESIZE | product-3d 以实时 framebuffer extent 驱动 Perspective aspect 与 point-light frustum，UI right rail/footer 以 End/Stretch 响应 logical resize；schema 7 记录 extent/aspect 匹配与 responsive authoring |
| RENDER-001-SPOT-LIGHTS | `SpotLight3D` 进入 World；world local `-Z` 生成出光方向，inner/outer half-angle 转 cosine；最多8个 camera-affecting active component 在容量检查前按 influence sphere cull；Null/RenderScene/bgfx/shader 与 product-3d schema 8 证明 authored/committed/culled=`3/2/1` 稳定消费 |
| RENDER-001-VERTEX-TANGENTS | StaticMesh v1 固定为 P3N3T4UV2；glTF authored/MikkTSpace tangent、Asset upload、Null/bgfx 唯一 layout 与 signed-scale-correct shader TBN 贯通；缺 NORMAL/UV 显式失败；product-3d schema 12 证明2份 tangent mesh upload |
| RENDER-001-CSM | 最多一个 `DirectionalLight3D::cascadedShadow` 进入 frame snapshot；稳定排序后映射 `directionalLightIndex`，固定4个 `CascadedDirectionalShadowDepth` pass 写入 startup-only 配置 cascade tile 的2×2 sampled D16 `DirectionalShadowAtlas`（默认每 tile 1024²），camera-slice projection、depth/normal bias 与配置尺寸对应的每级联3×3 PCF 贯通；primary-surface clear ownership保持独立；product-3d schema 12 证明 authored/submitted config=`1` 与 cascade constant=`4`，GPU 四级联实绘由 bgfx contract 和产品视觉 gate 证明 |
| RENDER-001-SPOT-SHADOW | 最多一个 camera-affecting `SpotLight3D::shadow` 进入 frame snapshot；稳定 spot-light 排序后映射 `spotLightIndex`，单个 `SpotLightShadowDepth` pass 写入 startup-only 配置的 sampled D16 `SpotLightShadowMap`（默认1024²）；projection、depth/normal bias、匹配 spot slot 与配置尺寸对应的3×3 PCF 贯通，primary-surface clear ownership保持独立；product-3d schema 16 继承 schema 15 的 authored/submitted config=`1/1` 与跨帧稳定 descriptor |
| RENDER-001-POINT-SHADOW | 最多一个 camera-affecting `PointLight3D::shadow` 进入 frame snapshot；稳定 point-light 排序后映射 `pointLightIndex`，非法 near/depth/normal bias 与第二个可见 shadow fail closed；固定六个 `PointLightShadowDepth` pass 按 `+X/-X/+Y/-Y/+Z/-Z` 写入六张 startup-only 配置同尺寸的 sampled D16 map（默认每面512²），dominant-axis 选面、匹配 point slot 与配置尺寸对应的3×3 PCF 贯通；pass 顺序固定 CSM×4→Spot×1→Point×6→Opaque3D→Transparent3D→Sprite2D→UI，primary-surface clear ownership保持独立；product-3d schema 16 继承 schema 15 的 authored/submitted=`1/1`，on/off 中央 RGB ROI fingerprint 与正逐像素 L1 差分证明影响最终像素 |
| RENDER-001-IBL | EnvironmentMap v1 固定 RGBA16F diffuse/specular cubemap + 完整 specular mip 链 + RG16F BRDF LUT；`GpuEnvironmentMapId` 聚合三张 native texture 的 create/validate/bind/clear/retirement；Opaque3D 使用 Cook-Torrance GGX direct light 与 split-sum IBL；product-3d schema 12 固化 Catalog cooked fixture、上传/bind/clear/retire 与尺寸 |
| 2D-LIGHT-N3 | PointLight2D 在容量提交前按 resolved、pixel-snapped Camera2D 做旋转相机空间精确 circle-vs-rectangle culling；仅 camera-affecting light 占8槽，第9盏显式失败；无相机/0x0 surface 与 ShadowOccluder2D 保留未裁剪语义；product-2d schema 17 固化 authored=3/committed=2/culled=1 |
| 2D-LIGHT-N4 | PointLight2D source radius 默认0保留硬阴影，正值以固定成本 normalized-depth segment projection 生成连续 penumbra；CPU reference、GLSL120/SPIR-V/DXBC shaderc 与 product-2d schema 18 soft/hard 四跑差分通过 |
| 2D-LIGHT-N5 | Sprite2D 可选 weak normal Texture2D handle 经独立 resolver 进入 packet-local ref；Null/bgfx 双 ref preflight、`(base, normal)` 连续 batch、derivative TBN 与 flat-normal fallback 已贯通；product-2d schema 19 固定 `texturesUploaded=3`，normal on/off 四跑差分及3份 Texture owner/retirement lifecycle 通过 |
| 2D-INPUT-ADV / N3 | Runtime 单一 unified binding 覆盖 digital/analog value、deadzone/scale、两种合成、多手柄、UI suppression 与 next-frame transactional rebind；本轮测试执行结果以最终验证记录为准 |
| 2D-TILEMAP-STREAM | TileMap v3 stream root + 独立 deferred TileMapChunk；固定容量 Camera/layer demand、request budget、cancel/unload、lease 与 transactional capacity；resident generation 贯通 dirty cache；产品 sample 每帧推进 visual/collision residency |
| 2D-TILEMAP-LRU | retain window 作为 optional cache；按成功 demand update 时位于 load window 的 recency 自动淘汰，读取不 touch；desired 强需求超 capacity 仍 transactional |
| 2D-AUDIO-ADV / N7 | voice gain/pitch/pan/fade、transient one-shot retirement；Create 固定预分配的 bounded PCM stream、owner-thread atomic submit、EOF/underrun/cancel、terminal backpressure/debt、generation 与 shutdown realtime gate；产品 sample 以 owner-thread deterministic mix 验证 stream，miniaudio callback 由 adapter tests 验证 |
| 2D-FX / N8 | Scene standalone `ParticleSystem2D` / `Trail2D`：Create 唯一持久 PMR 分配、固定容量与单调不复用 stable key；固定 seed 粒子、事务式 burst/update、trail anchor/break/独立 lifetime/width age interpolation；product-2d schema 9 结构化与像素证据 |
| RUNTIME-SHUTDOWN-DEADLINE / N9 | `ITaskSystem::shutdownAndJoinFor` 有界等待、invalid 无状态变化、timeout ownership retention/retry；`EngineHost` 使用配置 deadline，失败先写 Diagnostics 再 terminate，不继续析构 owner |
| ASSET-HANDLE-SCENE-2D-A1 / N10 | `SpriteRenderer2D` 保存 weak `AssetHandle`，extract 借用 allocation-free resolver；invalid/stale/cross-store/wrong-kind/unbound fail closed，hidden 不解析；窄 AssetTypes target 避免 Scene 传递完整 Asset |
| ASSET-HANDLE-SCENE-2D-A2 / N11 | N11 当时契约：fixed-capacity owner-thread Sprite registry 借用 Store/device；device-instance allocator 事务分配唯一、单调不复用 key，同 device 多 registry 安全；唯一 required Texture2D cooked dependency fail-closed resolve；当时产品 State unbind 后 destroy，schema 13 保留 binding/release/texture destroy/resolver evidence；该分裂 owner 契约已由 N16.3 替代 |
| ASSET-HANDLE-SCENE-2D-A3 / N12 | standalone Particle/Trail 从 `u32` key 迁移为 weak Sprite `AssetHandle`；显式 extract 借用共享 resolver，空/失效资源 fail closed；空 FX 跳过解析，Trail 每次非空 extract 解析一次、Particle 按 live item 解析；schema 11 独立记录两类 hits，FX fingerprint schema 2 使用稳定 AssetId |
| ASSET-HANDLE-SCENE-2D-A4 / N13 | TileMap emit 从持久 `u32` key 迁移为 weak Tileset `AssetHandle` + borrowed Asset resolver；Tileset 唯一 required Texture2D dependency fail closed；hidden/off-camera/empty 不解析，非空可见集合只解析一次，失败清空；schema 12 独立记录 TileMap hits |
| ASSET-HANDLE-SCENE-3D-A5 / N14 | `MeshRenderer3D` 保存 weak StaticMesh/Material Handle；Prefab 只做 AssetId→Handle；visible extraction 分别借用 kind-specific resolver，资源失效 fail closed，hidden 不解析；3D product evidence schema 1 记录 handle 发布、两类 resolver hits 与 AssetStore active |
| ASSET-HANDLE-SCENE-3D-A6-BINDINGS / N15 | fixed-capacity owner-thread Mesh3D registry 借用 Store/device/PMR；mesh/material 独立 device allocator 事务分配、不复用；Material texture/factors 原子发布并按 dependency fail closed；exact stale unbind 与产品逆序释放安全；schema 2 记录注册/释放/销毁与 registry 释放 |
| ASSET-HANDLE-SCENE-N16.1-CORE | packet-local `FrameResourceRef`/固定容量资源表、同帧去重与 owning pin；Runtime 在 extraction 前开启 packet，并在 complete/skip/abandon 时 exactly-once 释放；Texture2D retirement 事务覆盖 backend reject 与重试 |
| ASSET-HANDLE-SCENE-N16.2-SPRITE | World、TileMap、selection、Particle 与 Trail 的 Sprite2D extraction 全部只写 packet-local texture ref；registry 用 frame borrow pin 阻止活跃帧 unbind；Null/bgfx 在提交副作用前验证 ref owner/generation/kind/range |
| ASSET-HANDLE-SCENE-N16.3-SPRITE-OWNER | Sprite registry Entry 唯一拥有 resident Lease/GPU/binding；register 成功才消费 GPU，active borrow 与 retirement failure 保留 Entry；product-2d schema 14 证明2份 owner handoff、weak handle 失效与 retirement ledger 全部 Released |
| ASSET-HANDLE-SCENE-N16.4-MESH-OWNER | Mesh/Material Render item 迁移为 packet-local geometry/material ref；Mesh3D registry 唯一拥有 Mesh Lease/GPU/binding、Material Lease/binding 与按 AssetId 去重的共享 Texture Lease/GPU；active frame/material reference count 阻止过早 retirement；历史 schema 4 证明2 Mesh/2 Material/3 Texture，schema 15 随 skin witness 扩展为3/3/3，2026-08-15 schema 16 再证明独立 Blend Material 下3/4/3全部 load/bind/retire |
| ASSET-HANDLE-SCENE | A1-A6 与 N16.1-N16.4 全部完成；Scene/Prefab/FX/TileMap 只持 weak Handle，Render item 只持 packet-local ref，Sprite2D/Mesh3D resident ownership 与 retirement 均收敛到 registry + AssetSystem |
| UI-SHOWCASE / PRODUCT-2D / PRODUCT-3D | 独立20控件 showcase、product-2d Scene Explorer TreeView 与 product-3d Asset ListView/Scene TreeView 统一使用继承式产品 Theme；Button pressed/focus/elevation 层级、Dark/Light 事务换肤、实际 Slider/Checkbox 状态联动、FreeType client capture 与结构化生命周期证据成立 |
| UI-DEEP-TREE | 50,000 层 retained tree 的 structure/layout/hit/paint publication 与 subtree destroy 使用非递归路径；popup publication 不再对每个节点重复回溯祖先链 |
| UI-002-ACTION | 平台中立 `UIAccessibilityAction` seam 完成 Focus/Invoke/Toggle/SetRangeValue/SetTextValue；owner-thread dispatch 保留正常控件 callback，并对 stale、disabled 与 kind 不兼容目标 fail closed（`7d84ae67`） |
| UI-002-CONTROL-PATTERNS | Windows UIA 完成 Invoke/Toggle/RangeValue/Value pattern、跨线程 HWND action dispatch 与 provider snapshot 生命周期门禁；对应 UI/UIA 单测落地（`e82f1aaf`） |
| UI-002-EXTERNAL-HWND-GATE | `RunUi002UiaGate.ps1` 由独立 Windows UI Automation client 连接真实 showcase HWND，自动验证属性/fragment、四类 control pattern action 与 `WM_CLOSE` 正常退出，并输出 schema 1 JSON；不声称 Narrator 合规（`e82f1aaf`） |
| TEST-003 | bgfx + FreeType 图同轮运行11个 Core/Scene/Asset/Render/UI/UIA GoogleTest executable 和300帧 product-3d；2026-08-15 schema 16 统一验证 glTF/PBR、skin witness、Animator3D CPU pose→packet palette→bgfx GPU skinning、IBL、CSM/Spot/Point shadow、surface aspect、responsive UI、lighting snapshot、资源 retirement、packet-local resolver、retained UI，以及独立 Blend Material、双 static transparent witness、统一 static/skinned back-to-front checksum 与 transparency on/off RGB L1 |

“M12 Done”只表示产品删除完成，不自动证明后续 Linux、PBR、accessibility 或 benchmark 门禁；
其中 Linux tip 已由 TEST-001 另行关闭，其他剩余工作继续由 Backlog 跟踪，不再扩写 M12 历史清单。

## 产品门禁视图

| 门禁 | 当前结论 | 下一关闭点 |
| --- | --- | --- |
| 2D product | TEST-002 保留 schema 23 的历史同轮证据；当前 sample/gate 已升级到 schema 29。TileMap v3 每帧 demand/pump/commit visual=10 与 hidden collision=20，gameplay objects=30 留在 root；Navigation2D 从 collision/gameplay layer 派生静态 immutable weighted grid，solid/rectangle/blocked=`11/0/11`、weighted/max-cost=`1/5`，`PhysicsNavigationSync2D` 每次 Physics step 后把 crate 的权威 transform 同步为动态 blocker，sync/add/update/remove=`301/1/5/0`、registered/published=`1/1`；基础/动态/严格/切角 path cells/cost=`5/40`，weighted path=`7/60` 且绕开高代价 cell，并保留单步 expanded=`1`、revision/mutation=`10/2` 与取消终态；World2D snapshot 以唯一现行 schema v4 保存节点名称、stable hierarchy、五类2D组件（含 SpriteAnimation binding）、稳定 AssetId 与 game-owned blob，448-byte named entity record，旧 schema 拒绝且 restore 失败全量 rollback；CameraFollow2D 负责 dead zone/限速/world clamp 与 presentation interpolation；retain-window LRU、Advanced input、World/Particle/Trail weak Sprite Handle、TileMap weak Tileset Handle 已完成；Physics2D 已覆盖 Box/Circle/Capsule/ConvexPolygon/Chain 与 Distance/Revolute/Prismatic，产品断言 sensor enter/exit、Chain 和三类新增 shape/joint ready 字段；TileMap→Physics 已收敛为 per-resident-chunk static collider + greedy rectangle 合并 + residency/revision 增量同步，产品 `physicsStaticBodies=1`（11 solid cell → 2 box shape）并在 State 退出时证明 collider 全量退休；Sprite2D base/optional-normal 都使用 packet-local `FrameResourceRef`，fixed-capacity registry 唯一拥有 resident Lease/GPU/binding；schema 29 继承 schema 19 的 normal-map、双灯双遮挡、authored/committed/culled=`3/2/1` 与 soft/hard 差分，并保留 UI Flow base/pause Screen push/pop、Back/Confirm/Menu、Primary 用户设备 revision、Dark→Light→Dark、Scene Explorer TreeView、`texturesUploaded=3`、3份 owner/retirement handoff、ledger Released 与四类 resolver hits，以及 notify 消费计数、Cooked/live Navigation bit-exact、Catalog Fx2D `0/10/10` 与 Chain ready 证据 | 跨 GPU lighting exact golden、FX GPU simulation与高级约束 |
| Linux tip | Docker GCC13 + Clang22（含 sanitizer）已复验（TEST-001） | 可选 Wayland |
| UI product | 20控件独立 showcase、Dark/Light 实时换肤、Button hover/pressed/focus/disabled 反馈、product-2d Scene Explorer TreeView 与 product-3d Asset ListView/Scene TreeView 均有结构化与 Windows FreeType 视觉证据；authoring 已统一为 descriptor/recipe `createElement()`，Showcase 普通页面使用 Flow/Flex；Semantics/Theme role/reset、bounded build transaction、Canvas `SolidRect` 与统一 RoundedRect 已关闭；Image/Icon 的 root-scoped resolve/pin/RGBA ImageQuad 链路与 Canvas NineSlice 原子展开已完成；Showcase 已接入 icon-only/图文 Button、Inventory thumbnail、NineSlice panel、Dark/Light atlas/sampling 视觉和逐帧结构化证据，以 lifecycle mode 覆盖 atlas invalidation、unavailable 与 missing resolver 连续 skip，并以 6-case size matrix 覆盖 Dark/Light × 1x/1.25x/1.5x client footprint；`ui_image_nineslice_v1` 关闭 `Q=5096/U=64/B=1000`、resolve/pin/dedupe/high-water/allocation/checksum；Component 完成全池 reservation/counter 与 `ui_component_build_v1`；Style/Motion 完成 imageTint、Visual gate、persistent BackgroundColor reservation/activation、`ui_motion_v1`，以及 typed paint/bounded-layout timeline 与统一确定性 gate；`Tina Studio Compact` 按钮层级（默认 Tonal、Primary/Danger/Tonal/Outlined/Text 与 Segmented=RadioButton role）及 Editor 旧 disabled-active 视觉移除已合并主线（UI-STUDIO-DESIGN，state-feedback 视觉门禁待干净门禁机补跑） | OS 级 DPI 与跨 GPU 金标由 `UI-003` 跟踪 |
| UI accessibility | 平台中立 action seam、Windows UIA Invoke/Toggle/RangeValue/Value patterns 与真实 showcase HWND 跨进程自动 gate 已落地；gate 可输出属性/fragment、action 结果和正常关闭的 schema 1 JSON | 固化当前 tip 的带日期 gate 结果并完成 Windows Narrator/Inspect 人工金标；Linux AT-SPI 由 `UI-002-LINUX` 独立跟踪 |
| 3D product | 双静态 mesh + authored/MikkTSpace tangent + 唯一 P3N3T4UV2 + Resources-owned AssetSystem + Prefab/Scene weak mesh/material Handle + engine-provided、State-owned Mesh3D registry + packet-local geometry/material resolver、Mesh/Material/共享 Texture 统一 owner、原子 material bundle、baseColor/MR/normal 贴图采样、material factors、Cook-Torrance GGX + cooked EnvironmentMap split-sum IBL、World DirectionalLight3D/PointLight3D/SpotLight3D→逐帧 RenderScene snapshot、point/spot influence-sphere culling、固定4级联 CSM、固定单 SpotLight shadow、固定单 PointLight 六面全向 shadow、startup-only shadow extent 与 deterministic pass scheduler 已有证据；schema 15 的 SkinnedMesh/AnimationClip3D、Animator3D CPU pose、packet palette、GPU skinning、固定2-joint/3-instance witness 与 skin-animation on/off RGB 差分已于2026-08-14集中 gate 闭环；schema 16 的显式 Opaque/Blend、统一 static/skinned back-to-front 排序、Transparent3D straight-alpha/depth-less-no-write、不投 shadow但接收 lighting/shadow/PBR/IBL、双 static witness 与 transparency on/off RGB 差分已于2026-08-15闭环，并继承实时 surface/camera aspect、responsive UI、lighting、IBL/shadow、3 Mesh/4 Material/3 Texture handoff、weak handle 失效、ledger Released、Asset ListView/Scene TreeView 与 Dark→Light→Dark | post 与跨 GPU golden 后置 |
| Runtime stack/packet | stack/commands/policy、FramePin present-return CPU completion、Texture/Mesh AssetLease-backed retirement 与 EnvironmentMap GPU-owner readback retirement，以及 Task timeout/retry + Host-enforced TaskSystem worker-exit/join deadline 已落地 | 产品 sample 暂停演示；通用 GPU submission fence 非当前 Runtime 契约 |
| Asset/Cooker | multi-mesh 产品 E2E、baseColor/MR/normal Texture2D cook、外部 URI 安全；TileMap v3 root/TileMapChunk v1 + eager Tileset/deferred chunk dependency/localId 发布前验证及 retain-window LRU；chunk demand priority 聚合与 Editor spawn plan 生成；resident TileMap 到 Navigation2D immutable weighted 数据的 exact material-cost 原子派生；独立 NavigationGrid2D v1 cook/typed load/Editor bake overlay；SpriteAnimationClip v2 notify events cook（`#tag@offset` recipe）；Fx2D v1 cook/typed dependency/Scene factory；ASSET-002 watcher/revision、增量 Cooker 与 CPU/GPU resident reload transaction 已完成 | 五项 2D 任务统一 gate、更完整资源炸弹矩阵、Asset cache/LRU、Bundle/Patch 与 network Asset |
| Audio | `2D-AUDIO-ADV / N7` 已完成；Windows product-2d 以 owner-thread deterministic mix 验证 bounded stream，miniaudio callback/mixer 与 lifecycle 由 adapter tests 验证 | Linux、真实设备质量/延迟/切换与 callback benchmark |
| Legacy retirement | 产品源码/target 删除完成；vcpkg legacy feature、EASTL/compatibility 与剩余迁移 shim 扫尾完成 | 仅保留 `TINA_BUILD_LEGACY=ON` FATAL 拒绝开关 |
