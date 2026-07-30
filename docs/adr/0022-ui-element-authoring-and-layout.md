# ADR 0022：以 Element 组合模型替代 Widget-kind authoring，并重做布局与内容对齐

- 状态：Accepted
- 日期：2026-07-29
- 决策者：Tina maintainers
- 范围：`Tina::UI` authoring、layout、content placement、behavior、semantics、Theme role

## 背景

ADR 0011 冻结了 per-window `UIContext`、generation node、固定容量事务快照和后端无关
DisplayList，这些决定仍然有效。但首个产品切片把父容器属性、子项属性和控件自身文字混在
`UILayoutStyle::flex` 与 kind-specific side array 中：

- `grow` 与 `direction/justify/alignItems` 位于同一个结构，调用者难以判断属性由谁解释；
- 普通布局与 `left/top/right/bottom` 绝对坐标并存，产品样例容易退化为固定画布；
- `alignItems` 只排列子节点，Button/TextEdit 自身文字仍从左上 padding 起点绘制；
- measure、paint、caret/selection 和 Pointer 字符定位分别计算文字起点；
- `UIWidgetKind` switch 同时承担 authoring、行为、semantics 和 Theme 选择，第三方只能修改固定控件，
  不能通过组合定义完整的业务组件。

本次迁移允许破坏旧 API。没有已发布 SDK consumer 需要 deprecated alias、双写 bridge 或旧字段兼容。

## 决策

### 保留的 Runtime 基础

继续保留 ADR 0011 的 `UIContext` owner-thread、`UIRootOwner`、generation-aware `UINodeId`、固定容量
PMR、structure/layout/hit/paint/semantics 原子发布、上一份 committed hit 路由和 backend-neutral
DisplayList。不得以 OO Widget 树、任意虚函数或 raw GPU callback 替代这些边界。

### Element 组合模型

公开 authoring 模型收敛为：

```text
Element
├─ Layout
├─ Container
├─ Content
├─ Visual
├─ Behavior
└─ Semantics
```

Button 是 `Element + TextContent + ActivateBehavior + ButtonSemantics + StyleRole` 的官方 recipe，
而不是要求产品代码继承 Widget 基类。`UIWidgetKind` 逐步退回实现细节；新的公开创建入口使用完整
descriptor 原子创建。多节点业务组件使用 bounded build transaction，任一步失败回滚本次组件创建。

### 父容器、子项与 Overlay

旧 `UIFlexStyle`、`UILayoutPositionMode` 和 `UILayoutInsets` 直接删除，不提供 alias：

```cpp
UILayoutStyle::flexContainer // direction, justifyContent, alignItems, gap
UILayoutStyle::flexItem      // grow, shrink, basis, alignSelf
UILayoutStyle::overlay       // horizontal, vertical, offset
UILayoutStyle::placement     // Flow or Overlay
```

父容器的 `alignItems` 控制全部 Flow 子节点；单个子节点用 `alignSelf` 覆盖。常规页面使用 Flex、
Percent、grow、padding 和 gap。Overlay 使用 Start/Center/End/Stretch 加 offset；margin 表达 Stretch
边距。Popup 继续使用受控 anchor policy。普通产品布局不得把 Overlay offset 当作默认页面排版方式。

tree 顺序同时决定 layout、paint、focus 和 semantics 顺序；不加入 CSS `order`。

### 控件内部内容

`UIContentAlignment` 独立于 Flex child alignment：

- Button：Center / Center；
- Label：Start / Start；
- TextEdit、Dropdown、Radio、List/Tree item：Start / Center。

layout 发布 `UICommittedContentPlacement`。文字绘制、glyph fallback、caret、selection 和
Pointer-to-codepoint 必须读取同一份 committed origin/content box，不得再次从 `worldRect + padding`
推导平行结果。

### 扩展、Theme 与绘制

第三方组件优先组合 Element/Content/Behavior/Semantics。通用 behavior 至少覆盖 focusable、activate、
toggle、value/text change；semantics 支持显式 role/name/action 以及 MergeDescendants/Exclude。

Theme 从 kind switch 迁移为 role recipe，例如 `Button.Primary`、`Button.Danger`、`Text.Body`、
`Panel.Surface`。节点保存 `UIStyleRoleId` 与属性 override mask；`clearOverride()` 恢复继承。

自定义绘制只允许 backend-neutral、容量有界的 Rect/RoundedRect/Image/NineSlice/Text/Glyph/Canvas
命令。公开 API 不暴露 bgfx、encoder、shader handle 或任意 GPU callback。

## 迁移顺序

1. Layout/Content：删除旧字段，完成 Flex item、Overlay alignment 和 committed content placement。
2. Element/Behavior：完整 descriptor、官方 built-in recipes、bounded build transaction。
3. Semantics/Theme：组合 semantics、style role、override reset。
4. Built-ins/Runtime/Samples：迁移所有创建入口和产品布局，删除 `UIWidgetKind` authoring surface。
5. 完成替代测试后删除旧 create-by-kind 声明与仅服务旧 API 的 switch/side data。

迁移分支不得增加兼容 alias。每一阶段都必须保持固定容量、失败原子性和 committed snapshot 语义。

## 当前实现状态

截至 2026-07-29，ADR 的 authoring/composition 主体已经落地：

- Layout/Content：`flexContainer` / `flexItem`、Flow/Overlay、`UIContentAlignment` 与
  `UICommittedContentPlacement`；paint、caret/selection、Pointer-to-codepoint 共用 committed origin；
- Element authoring：`UIElementDescriptor`、内建 `make*Element` recipes，以及 `UIRootBuilder`、
  `UITreeUpdater`、Runtime phase facade 的统一 `createElement()`；旧 create-by-kind 成员声明、定义和调用点
  已删除，不保留兼容入口；
- Semantics：descriptor 显式声明 mode/role/name/description/actions，支持 Automatic、Publish、
  MergeDescendants 与 Exclude；显式空 name 不回退 content，committed parent 指向最近 published ancestor，
  accessibility action 必须先由 committed semantics 发布；
- Theme：`UIStyleRoleId` 独立于 behavior/semantics，覆盖 Panel/Text/Button/现有控件 recipe；属性级 local
  setter 只 detach 对应 binding，`clearOverride()` 按当前 role 和当前 product theme 恢复继承；StyleRole、
  Theme 与 dirty queue 更新先 preflight 再原子写入，Runtime phase facade 同样暴露 role/reset；
- 事务：`UIElementBuildTransaction` 以固定 node budget 创建一棵组件子树，ListView/TreeView 的内部 row
  pool 计入预算；任一步失败、显式 reset 或析构均回滚整棵子树，active transaction 阻止 structure/layout
  发布；
- Canvas：descriptor 借用的命令在 create 返回前复制到 Context 固定容量 pool；当前冻结 backend-neutral
  `SolidRect` 首切片，按 box 后、控件 chrome 前的顺序发布并受 Element world transform/effective clip 约束，
  destroy/transaction rollback 回收 slot；
- 失败回滚：intrinsic/semantics UTF-8 与 Canvas 均在固定容量 storage 中预检/复制，初始化失败销毁本次
  节点和全部 side storage；
- 产品迁移：UI Showcase 普通页面使用 Flow/Flex 层级，只有 Dropdown Popup 使用 Overlay。

公开 `UIWidgetKind` 头、umbrella export、header-isolation TU 与所有公开 authoring/inspection 引用已经删除。
实现内部仍保留私有 `BuiltinElementKind`，用于复用成熟控件的固定 side storage 和默认行为；它不是公开
authoring、semantics 或 Theme 身份。RoundedRect/Image/NineSlice/Text/Glyph 等更宽 Canvas 命令、圆角 clip
和 stylesheet 仍属于后续 Theme/Paint 扩展，不得把当前 `SolidRect` 切片写成 raw GPU callback 能力。

## 代价

- 所有现有 UI 调用点、测试和样例必须同步迁移，不能逐步依赖旧 ABI。
- `UIContext.cpp` 需要拆分为 ElementStore、StyleResolver、Flex/Overlay/TextLayout、Behavior、Focus、
  Semantics 和 Paint 模块；迁移期间代码 churn 较大。
- 完整 descriptor 与 bounded transaction 增加一次性 API 设计工作，但消除 create-then-many-setters 的
  半初始化状态。

## 被否决方案

- 在旧字段外增加 `gravity` alias：继续保留两套布局语义，调用者无法判断优先级。
- 只给 `UITextStyle` 增加 horizontal flag：无法表达垂直对齐，也不能统一 caret/Pointer placement。
- 开放 Widget vtable 或 raw paint callback：破坏固定容量、事务发布、accessibility 和 backend 隔离。
- 保留 deprecated create API 等 SDK 发布后再删：当前没有外部 ABI 负担，只会扩大迁移成本。
