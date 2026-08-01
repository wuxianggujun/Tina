# ADR 0023：UI 组合扩展、样式、图片/图标与 Motion

- 状态：Accepted
- 日期：2026-07-31
- Accepted：2026-07-31（固定容量/失败模型、首个 UI benchmark protocol 与一次性迁移策略）
- 决策者：Tina maintainers

## 背景

ADR 0011 已决定使用自研 Retained UI 和 backend-neutral DisplayList；ADR 0021 已决定 Game SDK 只取得
startup/phase-scoped 主窗口 UI capability；ADR 0022 已完成统一 `UIElementDescriptor`、recipe
`createElement()`、Flex/Overlay、committed content placement、显式 Semantics、StyleRole/override reset、
固定预算 build transaction 与 `SolidRect` Canvas。

下一阶段有四类真实缺口：

1. `UIElementBehavior` 虽表现为正交 flags，私有 resolver 仍只接受能映射为既有
   `BuiltinElementKind` 的精确组合，第三方只能组合既有控件，不能自由组合标准交互能力；
2. `UIStyleRoleId` 是封闭枚举，缺少用户 StyleClass、pseudo-state rule 与有界 stylesheet；
3. Paint/DisplayList 只有 SolidRect/SolidQuad/Glyph，HUD、Inventory 和常规游戏界面必需的
   Image/Icon/NineSlice 尚无资源所有权闭环；现有 bgfx UI shader 只把 R8 `.r` 当作 coverage，不能直接
   作为 RGBA 图片 shader；
4. Button 已有即时 hover/pressed/focus/disabled 反馈，但没有 clock、duration、easing、transition 或
   reduced-motion。

这些能力若通过第二套 Widget 继承树、完整 CSS、任意 paint callback 或 GPU handle escape hatch 实现，会
破坏 per-window owner、固定容量、committed snapshot 原子发布和 UI/Render 隔离。

## 决定

Tina UI 下一阶段采用以下单一演进方向：

1. 保留唯一 retained Element tree、`UINodeId`、`UIRootOwner` 和 per-window `UIContext`；不增加第二套 UI
   ABI，不恢复公开 `UIWidgetKind`，不开放 `class Widget` 继承面；
2. 第三方业务扩展以固定预算 Component recipe 为主。Runtime phase facade 提供不可逃逸的 bounded
   component transaction，成功返回 `UINodeId` 集合，失败回滚完整子树；
3. 标准 Activate/Toggle/Range/TextInput/Scroll/Selection 等 Behavior 迁移为独立 fixed-capacity side store，
   Element 创建和 setter 按 capability 校验，不再要求组合必须等于一个 concrete built-in kind；
4. Theme、StyleSheet 与 local override 分离。首版 stylesheet 只支持 node-local
   `role + class + state mask`，使用 startup-registered 强类型 ID 和预编译规则，不支持 descendant、
   `nth-child`、运行时 CSS parser 或任意 specificity；
5. Paint 增加 backend-neutral Image/Icon/NineSlice。Image 是第一类 Element content，也可作为 Canvas
   paint；Icon 只是 Image 的 atlas-source/tint/default-layout recipe，不增加 Widget、Behavior、Asset kind、
   DisplayList kind 或独立 atlas manager；NineSlice 复用 Image source 并在 DisplayList 前展开为 1..9 个
   ImageQuad；
6. retained tree 只保存稳定 AssetId、source rect、texture extent、intrinsic logical size、fit/alignment、
   tint 与 sampling metadata，不保存 AssetHandle/Lease、FrameResourceRef 或 GPU/backend handle。资源由
   owning-root scoped resolver 在 frame packet 构建期间解析，经通用 Texture2D FrameResourceRef 和 owning
   FramePin 保活；公共 UI 不依赖 Asset 模块。前置切片已将 `FrameResourceKind::Texture2D` 与 binding SPI
   泛化为 Sprite/UI 共用命名，未新增 `UITexture` kind 或长期兼容别名；root-scoped resolver、packet pin 与
   `ImageQuad` 仍在 `UI-IMAGE-001 A` 后续垂直切片中交付；
7. Motion 使用每窗口 monotonic clock 和 fixed-capacity active transition store。首版只插值颜色、opacity、
   统一圆角和 visual offset 等 paint-only 属性，支持 retarget 和 reduced-motion；
8. Motion 不延迟 callback、不改变真实 hit rect、不隐式延期 destroy，也不建立第二套游戏 update loop；
9. startup-only custom Behavior SPI 不属于首轮承诺。只有标准 Behavior + routed listener 无法满足有证据的
   插件场景时才单独提出，并且不得取得 `UIContext`、allocator、Renderer 或 GPU callback。
10. 新能力必须沿用固定容量、dirty phase 和 committed snapshot 统计：Component 不留下 retained wrapper，
    Style 只解析 node-local 预编译 rule bucket，Motion 只遍历 active track，Image/Icon 各输出一个 bounded
    quad，NineSlice 只展开 1..9 个 bounded quad；容量不足保持失败原子性，不得 heap fallback 或同步
    Asset I/O。

## 容量与失败模型

`fixed-capacity` 必须对应可配置、可统计的具体单位，不能只表示“内部用了某个 vector”：

| 能力 | Create/startup 时冻结的容量单位 | 容量不足语义 |
| --- | --- | --- |
| Component | node、text byte、canvas command，以及 Activate/Toggle/Range/TextInput/Scroll/Selection 各类 side-state slot | `beginComponent` 在首个节点 mutation 前完成 reservation；任一池不足则 live tree 零变化，后续 descriptor 失败回滚全部 reservation |
| Style | role/class/token/rule/declaration、node-class link、token-dependency link、每 bucket 候选 rule 上限 | stylesheet/theme candidate 不完整发布；旧 compiled sheet/resolved style 继续有效，不退化为运行时全表字符串匹配 |
| Image/Icon/NineSlice | retained image metadata/command、展开后的 paint entry、root-resolver binding、每帧 unique image resource/ref/pin、DisplayList command/batch | authoring metadata 非法时旧 committed snapshot 继续有效；NineSlice 不截断；DisplayList/pin 容量不足在 backend 副作用前整帧失败；运行时 missing/not-ready/wrong-kind 按 root 的 Skip/Fallback/FailFrame policy 处理并计数，不泄漏未 pin handle |
| Motion | transition definition/track、active index、每节点可动画 property 上限 | Style 绑定/commit 时预留 track；不足则保留旧 Style candidate。输入状态切换只激活已预留 track，不能因动画容量不足丢弃 action/callback |

Component budget 是实际 reservation 上限，不是事后提示。Motion 不应在 Pointer route 中临时申请 slot：对声明
transition 的节点在 Style candidate 阶段预留 track，状态变化只写 start/target/time 并切换 active bit；
`reduced-motion` 直接落到 target，不占 active list。Theme token reverse dependency 若实现，link 也必须来自固定池；
未实现该索引的首切片应诚实记录 Theme swap `O(N)` inspected-node，而不是隐式 heap 建图。

## 实施次序

1. `UI-RANGE-INPUT-KEYBOARD` 是不受本 ADR 接受门槛约束的独立交互分支，只依赖已完成的 Slider
   Focusable 子切片与 Runtime input route；它冻结 capability-shaped Range command 与成对 consumption，
   现行 RangeInput value/callback/UIA 路径是唯一状态源；迁移必须一次完成，不保留 adapter、双写或旧
   command 入口，公共 command 不按 `Slider` kind 命名。该分支不阻塞
   `UI-PERF-001` 或 Image/Icon，排期仍服从 Backlog 的 Now/P0 优先规则；
2. `UI-STATE-FEEDBACK` 的 Dark/Light 产品视觉证据已经关闭，maintainers 据此接受本 ADR；后续切片按本
   ADR 冻结的容量、失败原子性与单一状态源边界实现；
3. `UI-PERF-001` 先建立首个 workload/counter/checksum milestone；此后 `UI-IMAGE-001` 与
   `UI-COMPONENT-001` 是无直接依赖的并行分支。只有一条 UI lane 时先做 Image/Icon，因为它们已可由
   现有 leaf Element 承载，并直接
   解锁 HUD/Inventory/Settings 产品视觉；
4. `UI-IMAGE-001` 内部分为 A：Image/Icon quad + resolver/pin/RGBA backend + Image semantics，B：NineSlice
   1..9 quad 原子展开，C：产品采用与 workload；三个子切片共用一个 backlog ID，完整垂直链路关闭前不
   发布半份公共 API；
5. `UI-STYLE-001` 等待 Image 与 Component 两条属性面稳定，随后 `UI-MOTION-001` 才在稳定 Style target
   上实现 transition；
6. `SDK-001` 是独立 packaging/consumer lane，不等待 Deferred 的 `UI-FLOW-001`；它先证明当前公开 API
   可由仓库外开发者使用，后续每个新增公共 UI 切片同步扩展 consumer gate；
7. `UI-FLOW-001` 只在真实页面栈需求出现后进入；高级 Behavior SPI 继续后置到标准能力确有表达缺口时。

## 结果

- 游戏开发者可用标准 Behavior、StyleClass、Image/Icon/NineSlice 和 Component recipe 构建业务 UI，而不依赖
  Tina 私有 kind；
- `make*Element()` 继续作为内建控件 recipe，现有产品树可渐进迁移，不要求一次性重写；
- selector matching、Behavior state 和 active Motion 均有显式容量与失败语义，可保留稳态零隐式扩容；
- Image/Icon/NineSlice 必须从 authoring、committed paint、resource pin、DisplayList 到 backend 做完整垂直切片，
  不能先暴露半份公共 API；
- 装饰 Icon 默认不单独发布 semantics；icon-only Button 由 Button root 提供显式 name；有独立信息价值的
  Image 才发布 `UISemanticsRole::Image`，且不能从资源文件名推导 name；
- Theme/token 或 pseudo-state 变化必须按属性 dirty metadata 精确失效；首版 Motion 不引入 layout animation；
- 正式“第三方可使用”还依赖 `SDK-001` 的 install/export/package 与外部 consumer gate，源码树内可链接不等于
  SDK 已发布；
- 需要为状态矩阵、component rollback、stylesheet precedence/capacity、Image/Icon resource lifetime、Motion
  deterministic clock/reduced-motion 建立对应门禁。

## 性能后果

- clean `UIContext` commit 必须继续在无 dirty/viewport change 时直接返回，不运行 Measure/Arrange、Style、
  Hit 或 Paint snapshot rebuild；
- Behavior capability 拆分增加的是固定 side-store slot 和直接索引，不是每节点对象、vtable 或 allocator；
  Component recipe 只在 authoring phase 支付与其创建节点数成正比的成本；
- 单节点 Style state 只匹配该节点预编译 bucket，不扫描祖先、后代或完整 rule table；Theme 全局切换在
  有固定 reverse-dependency index 时按受影响 link 处理，否则明确按 `O(N)` 扫描，并输出 inspected/resolved
  node counter；
- Motion sampling 与 active track 数 `M` 成正比，`M == 0` 时不产生额外 Paint dirty。当前 committed paint
  candidate 的容量校验与 publication 仍按 layout/paint 数据线性工作，因此连续动画帧的真实成本必须与
  `N/P/M` 一起测量，不能仅用“dirty cache 局部更新”推导整帧 `O(M)`；
- Image/Icon/NineSlice 的 paint/DisplayList 工作量与实际展开 quad 数 `Q` 成正比，资源 lookup/pin 与每帧
  唯一 `(resolver scope, AssetId)` 数 `U` 成正比；Image/Icon 各为一个 quad，NineSlice 为 1..9 个 quad。
  resolve/pin 在 frame packet 构建阶段完成，UI commit 不等待文件或网络；
- RGBA ImageQuad 使用独立 shader mode/program；现有 Solid/Glyph 的 R8 coverage shader 不能冒充彩色图片
  采样。sampled straight-alpha RGBA 必须在 shader 中 premultiply 后再应用 committed tint，继续使用现有
  premultiplied blend；相邻 batch key 包含 texture ref、clip、sampling 与 shader mode，不允许全局重排
  paint order；checksum 只编码确定性 frame-resource ordinal，不编码逐帧变化的 packet generation 或 backend
  binding key；
- `UI-PERF-001` 先建立 `ui_static_commit_v1`、`ui_paint_dirty_v1`、`ui_route_v1`、
  `ui_virtual_collection_v1` 与通用 counter/checksum protocol；Component、Style、Image、Motion 切片再分别
  增加 `ui_component_build_v1`、`ui_style_state_v1`、`ui_image_nineslice_v1` 与 `ui_motion_v1`。固定机毫秒
  hard gate、median/MAD 和受审 baseline 仍由
  `PERF-002` 冻结；在此之前不承诺跨机器绝对时间不回归。

## 被拒绝方案

- 公开 `Widget` vtable、按控件继承并重写 event/paint：生命周期、容量和 dirty 影响无法由 Context 统一验证；
- 复制完整 CSS/USS：descendant matching、动态字符串属性和复杂 specificity 超出首轮产品需求；
- 引入 WPF DependencyProperty/Binding/反射模板体系：对象模型和运行时成本不符合轻量游戏 Runtime；
- 复制 Slate/UMG/CommonUI 三层对象结构：会重复 Tina 已有 Element/Runtime/DisplayList 边界；
- 每帧遍历全部 Element 查找 active animation，或在单节点 pseudo-state 变化时重新匹配整棵树/完整规则表：
  成本会随无关节点或规则增长，违反 active-set 与 node-local resolver 边界；
- 为拆分 `UIContext::Impl` 引入 per-node heap object、虚接口或跨模块 Service Locator：只增加间接访问与生命周期
  成本，不提供产品扩展能力；
- 公开 bgfx texture、shader/material handle 或任意 GPU paint callback：破坏 Game SDK 与 backend 隔离；
- 为 UI 新增 `UITexture`、`IconAsset` 或第二套 icon atlas manager：会复制现有 Texture2D Asset、frame-resource
  与 pin 生命周期；Image/Icon/NineSlice 应复用或泛化同一 Texture2D 资源；
- 首版支持 SVG/vector、animated image、远程 URL/runtime decode、Tile NineSlice 或任意 material：扩大资源、
  shader 和容量契约，不能阻塞 RGBA bitmap/atlas 的产品闭环；
- 在 Image/Icon/NineSlice 和 VisualState 一致性之前实现完整 timeline/editor：优先级与产品收益不匹配。
