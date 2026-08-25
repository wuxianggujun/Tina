# ADR 0029：UI Layout Debugger 与帧内诊断覆盖层

- 状态：Accepted
- 日期：2026-08-25
- 决策者：Tina maintainers

## 背景

Tina Retained UI 已有固定容量的 structure/layout/hit/paint/semantics publication、Runtime phase facade
和后端无关 DisplayList，但排查布局仍主要依赖源码、日志或一次性截图。Editor 和游戏程序都需要像浏览器
DevTools 一样查看节点树、authoring/resolved 布局参数、测量尺寸、world/content/clip 几何，并从真实
committed hit 结果选中节点。

该能力不能只存在于 Debug 构建，也不能通过向被检查的 retained tree 插入调试节点实现，否则调试器会改变
布局、命中、Semantics 和容量结果。Tina 还必须保留 owner-thread、固定容量、失败原子性、Runtime 不暴露裸
`UIContext*` 以及 UI/Render backend 分离等既有契约。

## 决定

### 1. 始终编译的可选诊断能力

1. `UILayoutDebugger`、`UILayoutDebugSnapshotView` 与 `UILayoutDebugOptions` 属于 `Tina::UI` 公共能力，
   Debug/Release 使用同一 ABI，不引入 `#ifdef DEBUG` 或第二套 UI。
2. `UIContextCapacityConfig::layoutDebuggerSnapshotCapacity` 在 Context 创建时固定容量。零表示不分配双缓冲、
   不构建诊断 entry；非零值不得超过 node capacity。启用未配置容量的调试器返回 `CapacityExceeded`。
3. options 只保存 overlay interaction state，不触发布局、不修改 dirty state，也不隐式 commit。

### 2. 与 Layout 原子发布的只读快照

1. 每次需要 layout commit 且诊断容量非零时，从同一 candidate layout 构建 fixed-capacity 双缓冲快照；
   仅在 Layout/Hit/Paint/Semantics 整个候选事务成功后切换 published buffer。
2. 任一步失败都保留上一份 layout debug snapshot；不发布半份 entry，也不因打开调试器单独运行 layout。
3. 每个 entry 发布 node/parent/depth/preorder、稳定的诊断 element type、authored/resolved style、父与当前
   content basis、measured/min/max-content size、local/world rect、content placement、effective clip/visibility、
   pointer hit policy、behaviors、style role、enabled 和 layout/paint ordinal。
4. snapshot 是 owner-thread borrowed view，到下一次成功 layout publication 或 Context 析构失效。

### 3. Runtime phase facade 与精确拾取

1. 普通游戏通过 `UIUpdateContext` 读取/设置 layout debug options、读取 committed snapshot，不取得或保存
   `UIContext*`；view 还受当前 UI phase epoch 限制。
2. 节点拾取复用 committed hit snapshot 的 `UIInputRouter::queryPointerHit()`。Runtime 暴露窄的
   `queryCommittedPrimaryWindowUIPointerHit()` facade，不按 world rect 在产品侧重复实现命中规则。
3. selected/excluded node 启用时必须属于同一 Context 且仍存活。`excludedSubtreeRoot` 用于排除 Editor
   DevTools 自身或其他诊断 UI，避免调试器检查自己。

### 4. Frame-local Render overlay

1. UI→Render integration 在普通 committed paint 转换完成后，按当帧 options 和同 revision snapshot 追加
   outline SolidQuad；selected node 可同时显示 world rect、content box 与 effective clip。
2. overlay 不进入 retained structure、layout、hit、focus、Semantics 或 committed paint snapshot，只存在于
   当前 DisplayList build transaction，并复用普通 UI 的 logical→framebuffer projection。
3. snapshot 的 viewport、structure revision 和 layout revision 必须与 committed paint 匹配。几何、ordinal
   或 DisplayList 容量失败时整次 frame-local build rollback，不截断正常 UI 后再发布半份 overlay。
4. 产品必须在启动配置中为可能显示的 outline 预留有界 DisplayList command/batch 和 render draw-call 容量；
   不允许运行期 heap fallback。

## 结果

- Editor 与游戏 Runtime 使用同一套 Release 可用诊断契约，可查看真实 committed 布局并精确拾取节点；
- 调试器不改变被检查树的布局、输入或 accessibility 结果，失败仍遵守最后成功快照语义；
- 开启快照和 show-all overlay 会增加固定 CPU storage 与每帧绘制命令，调用方必须显式配置容量；
- 诊断 element type 只用于显示/检查，不是 authoring dispatch，不恢复已删除的 `UIWidgetKind` 或 create-by-kind API。

## 被拒绝方案

- 仅在 Debug 构建编译：无法用于 Release 产品布局和客户现场问题，形成两套行为面；
- 把边框与 DevTools 节点插入 retained tree：会污染被检查布局、hit、Semantics 和容量证据；
- 每次打开面板隐式执行 layout：破坏显式 publication phase 和失败原子性；
- Editor 按 `worldRect` 自行倒序猜节点：会绕过 clip、pointer policy、Modal barrier 与真实 paint order；
- 动态 vector/字符串树快照：违反 Context 创建期固定容量与无隐式 heap fallback 约束。
