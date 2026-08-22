# ADR 0028：UI 固定容量 Grid 布局

- 状态：Accepted
- 日期：2026-08-22
- 决策者：Tina maintainers

## 背景

ADR 0022 冻结了父容器与子项属性分离、Flow/Overlay placement、固定容量事务和统一 committed 内容放置，
但普通 Flow 子项只有单轴 Flex。Editor Inspector 需要稳定的 label/value 列、同一行内的一至三个轴字段，
以及父级统一控制 label、输入框、gap 和垂直对齐。用嵌套 Flex、宽度阈值和运行期方向切换拼装这类二维
排版，会把同一结构拆成多个私有 recipe，并容易在可见性恢复时只恢复部分布局属性。

引入通用 Grid 不能建立第二棵布局树、运行期 heap track、CSS 解析器或新的提交路径；也不能与表示大量
逻辑数据项的 `UIVirtualGridView` / `UIDataGrid` 混为一类能力。

## 决定

### 1. Flex 与 Grid 共用现有 Element 布局契约

1. `UILayoutStyle::containerLayout` 在 `Flex` 和 `Grid` 间选择父节点对 direct `Flow` child 的解释方式。
   `Overlay` child 继续使用既有 overlay alignment/offset，不进入 Flex 或 Grid 的 intrinsic size 与自动放置。
2. `flexContainer` / `flexItem` 与 `gridContainer` / `gridItem` 保持职责分离。一个 Element 可以作为父级
   Flex/Grid 的 item，同时以另一种 container layout 排列自己的子节点；未选中的 container 属性没有兼容性副作用。
3. Tree 顺序仍同时决定自动放置、paint、focus 与 semantics 顺序，不引入 CSS `order` 或第二份视觉顺序。

### 2. 固定 8x8 track 模型

1. 每轴最多8条显式或隐式 track。`UIGridTrackList` 使用 inline `std::array`，运行期不增长、不 heap fallback；
   超过容量的 track、index、span 或自动放置在 normalization/layout commit 时 fail closed。
2. Track 只支持 `Px`、`Auto` 和正数 `Fr`：`Px` 是有限非负 logical pixel；`Auto` 取 Flow child 的
   intrinsic demand；`Fr` 先保留 intrinsic base，再按权重分配容器剩余空间。row/column gap 必须有限非负。
3. 空 track list 按实际放置创建隐式 `Auto` track，显式与隐式 track 共用同一8条容量，不新增独立隐式上限。

### 3. 放置、测量与对齐

1. `gridItem` 使用 zero-based row/column、`1..8` span 和 `UIGridAutoIndex`。全自动 item 按 source-order
   row-major 放置并跳过已占用 cell；只指定一个轴时沿另一个轴寻找首个可用区域。显式区域允许有意重叠，
   但仍占位，后续自动 item 不会进入该区域。`Collapsed` 和 `Overlay` child 不参与占位。
2. Measure 累积 child 的 measured size、margin、span 与 gap demand；Arrange 由同一 source-order 规则重放区域，
   解析 `Px/Auto/Fr` track 后应用 `justifyItems/alignItems` 与 `justifySelf/alignSelf`。
3. `Stretch` 只拉伸对应轴为 `Auto` size 的 item；显式 size、margin、min/max 继续使用既有规则。百分比 size/min/max
   在最终 Arrange 前以实际 grid area 为 parent content basis 刷新，不能继续按整个 Grid content rect 解释。

### 4. 所有权与提交

1. Grid placement 使用局部64-bit occupancy 和固定数组 scratch；不增加 `UIContext` side storage、owner 或 lifetime。
2. Grid 继续走现有 Measure/Arrange、dirty queue 和 committed Layout/Hit/Paint publication。非法几何、容量溢出或
   candidate publication 失败保留最后一次成功 snapshot，不发布半份 track 或 item rect。
3. Grid 是普通容器布局，不是虚拟集合。`UIVirtualGridView` 继续负责大量等宽逻辑 item，`UIDataGrid` 继续负责
   column/row/cell pool、selection 与双轴滚动；二者不改名、不转接到本 Grid API。

## 结果

- 表单、Inspector、工具条和其他二维排版可由父容器统一约束，无需产品侧宽度阈值或方向切换状态；
- Flex、Overlay、committed snapshot、transaction 和固定容量所有权保持不变；
- 固定8x8边界让 scratch、失败语义和最坏成本可证明，但本 Grid 不适合任意大表格、dense auto-flow、named area、
  minmax/repeat 或 CSS 完整 track sizing；这些需求必须由虚拟集合或新的独立决策承担；
- Editor 直接迁移到 Grid，不保留旧向量行 recipe、阈值字段或 compatibility alias。

## 被拒绝方案

- 继续在 Editor 私有层嵌套 Flex 并按 Inspector 宽度切换方向：父约束分散，恢复可见性时容易覆盖成半份布局；
- 为每种 axis 数量建立专用公开容器：扩展性差，重复 Grid 的 track、gap、span 和 alignment 语义；
- 引入通用 CSS Grid 解析与无限 track：扩大 API 和运行期内存模型，违反固定容量 UI 所有权；
- 复用 `UIVirtualGridView` / `UIDataGrid`：它们是带数据源、materialized pool、selection 和 scroll 的控件，
  不是普通 Element 子树的二维父布局。
