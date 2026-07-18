# 高性能自研 UI

> 状态：vNext 目标契约已接受。M7-C1b 已完成 C++23 standalone `tina_ui` 的事务式 Flex-lite
> layout；M7-C1c-a 已完成固定容量 Pointer policy/route-ancestry scratch 与双缓冲
> `UICommittedHitView` 数据基础；M7-C1c-b1 已完成无分配 `queryPointerHit()`、反向目标选择与
> visited count；M7-C1c-b2 已完成 synthetic routed pointer event：generation-safe RAII listener token、
> 固定容量 route path/listener storage、48-byte fixed-inline `noexcept` callback、Capture→Target→Bubble、
> stop/consume、路由中 mutation-safe invalidation 和 route/commit reentrancy guard。M7-C1c-b3b 已完成
> Runtime-private `UIInputRouteProducer` 与独立 `tina_runtime_ui_tests`，但 `EngineHost` 仍传 canonical
> `None` 且未拥有/选择 `UIContext`。持久 Pointer Capture、Focus/Modal、Button default action、paint snapshot/DisplayList、nested clip、
> dirty subtree pruning、文本/Glyph Atlas 与 bgfx UI pass 尚未实现。Tina UI 是游戏内 Retained UI，
> 不是 Immediate UI，也不是桌面编辑器工具包。

## 当前 Legacy 基线

现有实现已经具备 Retained Tree、generation `NodeId`、Capture/Target/Bubble、Pointer Capture、
Focus/Tab/方向导航、Modal Focus Scope、Theme/DPI、Clip、ScrollView、十万行虚拟 ListView、
单行 TextEdit、FreeType 中文 Glyph Atlas、Windows IMM32 composition 和 GLFW 手柄导航。

这些交互语义应迁移，但现有结构不能直接视为 vNext 实现：

- EventSystem 仍承担部分 `UIContext` 所有权；
- UI header、Button icon、TextRenderer 和 UIRenderer 仍直接使用 bgfx 类型；
- layout dirty 传播偏粗，命中与绘制顺序存在分别排序的路径；
- route/layout/text/render 热点仍有动态 Vector、map、string copy 或即时 glyph/upload 行为；
- `Grid/VBox/HBox/MatchParent/WrapContent` 是 Legacy 布局语义，不与新 Flex-lite API 并存；
- 在线平均/自适应批策略会随 frame time 改变行为，不适合作为确定性性能基线。

因此迁移策略是保留经过测试的行为，重建数据和 backend 边界，而不是继续给 Legacy
`UIRenderer` 增加控件。

## 目标数据流

```text
ordered PlatformFrameView transitions
  -> route against previous CommittedUISnapshot
  -> Tina::UI::InputTransitionConsumptionView + Tina::UI::ContinuousControlClaimsView
  -> gameplay Action Mapping
  -> IGameState model/intent update
  -> UI structural command commit
  -> dirty style/measure/arrange/paint update
  -> persistent local PaintCache update
  -> next CommittedUISnapshot
  -> immutable UIDisplayListView
  -> order-preserving UI batching
  -> UI Pass
```

UI 的性能来自增量 dirty、缓存、可观测容量和稳定数据布局，不来自重新实现一套通用 STL，
也不通过跳过正确事件或破坏透明绘制顺序换取虚假的 draw call 数。

## 所有权与清晰公共接口

Runtime 的每个 `WindowRecord` 唯一拥有一个 `UIContext`：

```text
UIContext
  NodeRegistry / generation slots
  Root registrations
  Focus / PointerCapture / Modal scopes
  Dirty queues / active animations
  Text layout references / PaintCache
  Committed paint + hit-test + semantics snapshots
```

这是冻结的最终 ownership，不是当前 `EngineHost` 已完成的能力。M7-C1c-b3b 只实现可独立调用的
Runtime-private producer；当前正式 run path 尚无 WindowRecord→`UIContext` owner/selection。

Platform/Event 只把 `PlatformFrameView` 的 UI-eligible transitions 交给 UIContext，不拥有节点、Focus、Capture 或 Layout。
`IGameState` 持有 move-only `UIRootOwner` 和非 owning `UINodeId`，不持有 UIContext、Renderer、
UINode 裸指针或跨帧 writer。
当前 M7-C1b/C1c-a/C1c-b1/C1c-b2 `tina_ui` 仍只 PUBLIC 依赖 `Tina::Core` 与 `Tina::Platform`；Font Asset、Render
descriptor/DisplayList 和 bgfx UI pass 分别在后续 asset/render 切片接入，不能提前把
Asset/Render 依赖塞回 UI tree/layout core。

最小游戏侧类型：

```cpp
namespace Tina::UI {

class UINodeId {         // 逻辑字段：owner WindowId + slot index + generation
public:
    [[nodiscard]] bool hasValue() const noexcept;

private:
    WindowId ownerWindow_;
    std::uint32_t index_ = 0;
    std::uint32_t generation_ = 0;
};

class UIRootOwner;       // move-only，销毁时安全注销整棵 root
class UIRootBuilder;     // 仅 GameStateEnter phase，可创建 root/tree/action
class UITreeUpdater;     // 仅 UIUpdate phase，绑定 State 已拥有的 root
struct UICommittedStructureView; // owner-thread borrowed structure snapshot
struct UICommittedLayoutView;    // owner-thread borrowed layout snapshot
struct UICommittedHitView;       // owner-thread borrowed hit/route-ancestry snapshot
struct UIPointerHitTarget;       // owning target facts + snapshot-local route indices
struct UIPointerHitQueryResult;  // owning point-query result + revisions/visited count
struct UIPointerInputEvent;      // owning normalized pointer input for one synthetic route
struct UIPointerRouteResult;     // owning route result; not Runtime consumption/claim output yet
class UIRoutedPointerListenerToken; // move-only RAII listener registration
class UIRoutedPointerCallback;   // 48-byte fixed-inline noexcept callback
struct UIRoutedPointerListenerDesc;
struct UISemanticsView;  // 只读 committed snapshot

struct InputTransitionConsumptionView;
struct ContinuousControlClaimsView;

} // namespace Tina::UI
```

`hasValue()` 只判断句柄是否非空，不声称对应节点仍存活；真正的 owner + generation 校验由绑定
目标窗口 registry 的 `UIContext::contains(UINodeId)` 或 `UITreeUpdater::isAlive(UINodeId)` 完成。
公开 API 不提供一个无法访问 registry 却名为 `isValid()` 的误导性成员。

`UIDisplayListView` 只在 Tina UI/Render SPI 之间传递，普通游戏代码拿不到。所有构建都会先校验
`UINodeId.ownerWindow + generation`，Debug 额外携带 Engine/registry cookie 改善诊断；热点遍历在
一次校验后只使用 UIContext 内部的紧凑 slot index。generation 回绕的 slot 永久 retire。

目标 Game SDK 伪代码（当前尚不可编译，Widget 与 EngineHost UIContext/producer 集成尚未实现）：

```cpp
class SettingsState final : public Tina::IGameState {
public:
    Core::Status onEnter(Tina::GameStateEnterContext& context) override {
        auto& ui = context.primaryWindowUI();
        root_ = TRY(ui.createRoot());
        panel_ = TRY(ui.createPanel(root_.id(), PanelDesc::Settings()));
        volume_ = TRY(ui.createSlider(panel_, SliderDesc{0.0f, 1.0f, 0.01f}));
        apply_ = TRY(ui.createButton(panel_, u8"应用"));

        TRY(ui.setAction(apply_, UIActionKind::Activate, [this] {
            intent_ = SettingsIntent::Apply;
        }));
        return Core::success();
    }

    Core::Status updateUI(Tina::UIUpdateContext& context) override {
        auto ui = TRY(context.uiTree(root_));
        TRY(ui.setSliderValue(volume_, model_.masterVolume));
        return Core::success();
    }

    Core::Status updateFrame(Tina::FrameUpdateContext& context) override {
        if (std::exchange(intent_, SettingsIntent::None) == SettingsIntent::Apply) {
            TRY(context.gameStateCommands().requestPopSelf());
        }
        return Core::success();
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override {
        return {.blocksGameplayInputBelow = true,
                .blocksUIInputBelow = true,
                .blocksFixedUpdateBelow = true,
                .blocksFrameUpdateBelow = true,
                .blocksRenderBelow = false};
    }

    void onExit(Tina::GameStateExitContext&) noexcept override {
        root_.reset();
    }

private:
    Tina::UI::UIRootOwner root_;
    Tina::UI::UINodeId panel_{};
    Tina::UI::UINodeId volume_{};
    Tina::UI::UINodeId apply_{};
    SettingsIntent intent_ = SettingsIntent::None;
    SettingsModel model_;
};
```

UI action 只写 State intent/model；`IGameState::updateFrame()` 再提交 `GameStateCommands`。回调不访问
全局 Runtime，不直接 push/pop 状态，也不捕获 Phase Context。

Action 是节点拥有的 retained property：`setAction` 原子替换同 kind action，`clearAction`、节点
删除或 root 注销负责撤销，不返回容易被临时析构的 RAII token。需要观察 Runtime EventBus 的
非 UI 订阅仍使用独立 RAII token，二者不能混为一套生命周期。

## 节点存储与结构修改

公共契约不暴露具体容器，但首个实现以 `GenerationPool<NodeRecord>` 和 UI tagged persistent
memory 为基线。`NodeRecord` 使用内部 index 连接 parent/first-child/next-sibling，热路径不要求
每节点一个独立 heap object、`shared_ptr` 或 vtable。Style、Text、Action、Semantics 等可选数据
进入按职责组织的池，避免所有节点承担最大 Widget 的内存。

UI tree 只允许 owner thread 修改。C1c-b2 的 synthetic route 期间允许 listener reset/add 与节点
destroy，但当前输入 transition 使用 route 开始时的 committed hit snapshot；`commitStructure()` 与
`commitLayout()` 在 route 进行中会返回结构化错误，防止 callback 让 borrowed committed view 在派发中失效。
destroy 会立即令 generation lookup 失败，slot 物理回收延后到 route cleanup，因此 Capture 阶段删除目标会
停止后续 Target/Bubble，不会向复用 slot 的新节点投递，也不会 UAF。`UIContext` 本身必须在 owner-thread
phase 边界销毁；从 routed callback 或 callback cleanup 内销毁，或从非 owner thread 销毁，违反生命周期契约。

新 root/节点完成结构、layout 和 snapshot commit 后，才能被后续 hit snapshot 使用。M7-C1c-a 的
`commitLayout(viewportSize)` 是 Runtime 应使用的发布入口：它按需完整构建所有受影响候选，再在同一
事务内切换对应双缓冲；任何验证、算术溢出或容量失败都保留上一份 structure/layout/hit snapshot 与
pending dirty。仅 Pointer policy 变化的 hit-only commit 不执行 Measure/Arrange，也不增加 layout
revision。`commitStructure()` 只保留为 M7-C1a 结构诊断 seam；它可以单独发布结构并保留旧 layout/hit，
Runtime 不得把它与 `commitLayout()` 拆成两个可观察发布阶段。

`UICommittedStructureView`、`UICommittedLayoutView` 与 `UICommittedHitView` 都是 owner-thread
borrowed view，分别在下一次对应成功发布或 `UIContext` 析构后失效，不是跨线程快照。节点不能保存 FrameArena
指针；Action 注册期允许在 UI persistent memory 分配，dispatch/layout/paint 热点不得产生稳态
heap allocation。

`UIContext::Create(ownerWindow, capacities, memory_resource)` 的 `memory_resource` 当前服务固定容量
tree/id、style/pointer-policy side array、dirty state/queue、layout 与 route-ancestry scratch、route path
scratch、routed pointer listener slots，以及 committed structure/layout/hit 双缓冲；
少量 control-plane 对象和 off-thread root release 队列在 Create 期间使用进程默认 heap 预分配。
`dirtyQueueCapacity`、`layoutSnapshotCapacity` 与 `hitSnapshotCapacity` 为0时从 `nodeCapacity` 派生，
非0时固定且不得超过节点容量；`routePathCapacity` 为0时同样从节点容量派生，`routedPointerListenerCapacity`
为0时从节点容量派生但可单独配置到最大1,048,576，因为一个节点可以注册多个事件监听。该 supplied PMR
范围不等于整个进程不使用 heap。`UIRootOwner` 在 owner
thread 析构时立即回收；若在其他线程释放，只把 root id 写入
预分配队列，下一次 owner-thread UI mutation/commit 再物理回收。
`UIRoutedPointerListenerToken` 同样是 move-only RAII：owner-thread reset 立即失效，包括 dispatch 中；
off-thread reset 进入有界队列，并在下一次 owner-thread mutation/route 前 drain。Token 在 `UIContext`
已经销毁后 reset 仍安全，但不能用来延长 context 生命周期。listener callback 使用48字节 fixed-inline
`noexcept` callable；超出容量或对齐要求的 callable 在编译期被拒绝，没有 allocator 或 heap fallback。

## 坐标、DPI 与可见性

vNext 只保留一套 UI 坐标语义：

- layout、hit-test、Pointer event 全部使用 window-logical coordinate；
- 左上原点、X 向右、Y 向下；
- Platform 输出 logical pointer，UI 不先放大到 framebuffer 再命中；
- content scale 只在 DisplayList extraction 把 logical rect/clip 转 framebuffer pixel，恰好一次；
- 用户 UI scale 通过 Theme typography/spacing/min-hit-size 影响布局，不等同 framebuffer scale；
- glyph raster pixel size 和 scissor 根据 content scale 计算，不能在 shader 再隐式翻转/缩放。

`UIVisibility` 固定为 `Visible`、`Hidden`、`Collapsed`：Hidden 参与布局但不画、不命中；Collapsed
不参与布局、绘制或语义树。Opacity=0 不自动等于 Hidden，Pointer behavior 必须显式配置。

## Dirty、布局和提交快照

```cpp
enum class UIDirty : std::uint16_t {
    None      = 0,
    Structure = 1u << 0,
    Style     = 1u << 1,
    Measure   = 1u << 2,
    Arrange   = 1u << 3,
    Transform = 1u << 4,
    Clip      = 1u << 5,
    Paint     = 1u << 6,
    Composite = 1u << 7,
    HitTest   = 1u << 8,
    Order     = 1u << 9,
    Semantics = 1u << 10,
};
```

`Structure` 表达增删、重排、换父与 root attach/detach；它再按变化范围派生最小的
Measure/Order/HitTest/Semantics 失效集合。`UIDirty` 是固定宽度 bit mask；只通过显式 bitwise helper 组合，不能把枚举 ordinal 当索引，
也不能用动态集合保存 dirty 状态。

M7-C1b 已实现的当前边界是：`setLayoutStyle()` 先规范化并比较旧值，值相同不入队；值变化时先
预检整条 node→root 路径需要的 dirty queue 容量，再一次性合并节点与祖先 dirty，容量不足时
style 与 dirty 均不变。dirty queue、style/dirty side array、layout scratch 与两套 committed
snapshot 全部在 Create 阶段由 UI PMR 固定容量分配。当前任意有效 layout dirty 会在 changed frame
对整棵 live tree 执行一次非递归 Measure/Arrange；dirty leaf 跳过无关 subtree 尚未实现，因此还
不能把下面的“最小传播与重建边界”全部宣称为现状。无变化且 viewport 相同的 commit 则是0次
Measure/Arrange；viewport 变化会强制一次重排。M7-C1c-a 的 `setPointerHitPolicy()` 支持
`Ignore`/`Targetable`，相同值为 no-op，值变化只使 hit snapshot 失效；非法枚举和容量失败不修改
已发布数据。

Mutation 先比较规范化后的旧值；值未变化不置 dirty。传播规则：

- intrinsic text/content 或 Auto 尺寸变化：Measure 向上收敛到首个 layout boundary/root；
- Arrange 只处理 dirty container，父输出 rect 真变化才向受影响子节点传播；
- position：Transform + Composite + HitTest，不触发 Measure 或 local PaintCache 重建；
- scroll/祖先 clip：Transform + Clip + Composite + HitTest，只更新受影响 subtree 的累计变换与
  effective clip，不重建后代 local PaintCache；
- color/hover/pressed：Paint，必要时 Semantics，不触发布局；
- opacity：当前节点和受 inherited opacity 影响的连续子树置 Composite，必要时 Semantics；不重建
  local PaintCache；
- sibling/z/root 变化：父 Order，并重建稳定 paint/hit 顺序；
- Theme revision 只把实际变化 token 映射为 Measure/Paint/Semantics，不无条件重建整树。
- Visible ↔ Hidden：受 effective visibility 影响的连续子树置 Composite + HitTest + Semantics，
  布局结果不变；
- 任意状态 ↔ Collapsed：父链 Measure/Arrange + Order，子树 Composite/HitTest/Semantics；
  不能只修改节点自身。

Local `PaintCache` 只保存节点局部几何与视觉数据；扁平 snapshot 通过 inherited transform/clip/
opacity/effective-visibility revision 更新累计值。祖先 revision 变化时仅扫描对应连续 subtree
range，不把每个后代错误标成 Paint dirty。

每窗口每帧最多一次 Measure/Arrange。布局中产生的外部新 dirty 留到下一帧，不能在 batch 末
清空丢失。`hitTest()`、Widget render、PaintCache/DisplayList build 都不能隐式调用 layout。

M7-C1c-a 的双缓冲 `UICommittedHitView` 保存所有 effective-visible route-ancestry entry，包括 policy
为 `Ignore` 的祖先；`Hidden`/`Collapsed` 子树不进入 snapshot。每项包含 `UINodeId`、snapshot-local
parent/root index、world rect、当前仅为 `viewport ∩ worldRect` 的 effective clip、`Ignore`/`Targetable`
与 paint ordinal。同一 view 内的 entry 按 paint ordinal 严格递增且唯一；view
携带 structure/layout/paint-order/hit revision。布局、结构或 policy 改变后，成功的 `commitLayout()`
原子发布匹配的 structure/layout/hit snapshot。

M7-C1c-b1 的 `queryPointerHit(point)` 对这份 view 做反向扫描，只接受 `Targetable` 且 point 同时位于
world rect/effective clip 的首个 entry；边界固定为 left/top inclusive、right/bottom exclusive，非有限
坐标安全返回未命中。结果复制 target/root identity、snapshot-local route index、四类 revision 与
visited count。查询为 `const noexcept`，不执行 layout/hit rebuild、不分配、不派发事件。

C1c-b2 的 `routePointerInput(input)` 是当前唯一已实现的 route 执行入口。它校验 `PlatformFrameId`、
source sequence、owner `WindowId`、primary pointer、事件 kind、有限 logical position/delta 和 button，
然后对上一份 committed hit snapshot 最多执行一次 point query，使用固定容量 route path scratch 构建
root→target ancestry，并按 Capture→Target→Bubble 调用匹配 kind/phase 的 listener。`stopPropagation()`
结束后续节点但不回滚当前节点已完成 callback；`stopImmediatePropagation()` 同时跳过当前节点剩余
listener；`consumeInputTransition()` 只标记结果 consumed，不等于 Widget default action。route 中 reset
后续 listener、add listener、destroy target/root、callback 持有的 root 自销毁、off-thread token reset
和递归 route 都有结构化语义或拒绝路径；容量不足时不派发任何 partial callback。当前仍没有持久
Pointer Capture、Focus/Hover/Modal、Button default behavior、独立 z-order/stacking 或 nested clip policy；
paint order 来自稳定 tree preorder，hit rebuild 仍线性扫描整份 committed layout，尚未按 dirty subtree 剪枝。
M7-C1c-b3a 已在 Platform raw transition 层补齐 Button/Wheel 的事件时 logical position；Runtime
producer 必须原样传入 `UIPointerInputEvent.position`，不得使用帧末 Pointer snapshot 覆盖历史坐标。
M7-C1c-b3b producer 已遵守该约束，并只转换 Move/Button/Wheel；reset、cancel 与非 Pointer transition
不路由、不伪造 Up，在 raw ordinal 空间保留 hole。listener consume 写回对应 ordinal bit，claims 当前恒为
canonical `None`。

producer 使用 Create 期双预分配 PMR bitset；supplied `memory_resource` 必须长于 producer。300帧复用
同一 PMR 的直接测试中 allocation count 不增长。失败用例先让 root Move listener 产生1次 side effect，
再让后续深层 Button route 因 route path capacity 失败；staging 不发布、旧 published view 保持，attempted
watermark 已推进，同帧 retry 被拒且 callback 仍为1，明确证明 listener side effect 不回滚也不重放。

## Flex-lite v1

M7-C1b 已实现游戏 UI 实际需要的首个确定子集：

- `Length { Px, Percent, Auto }`；
- width/height、Min/Max、Margin、Padding、Gap；
- Row/Column、grow；
- Justify：Start/Center/End/SpaceBetween；
- Align：Start/Center/End/Stretch；
- Absolute Overlay + inset；
- `Visible/Hidden/Collapsed`。

不实现 CSS cascade、selector、margin collapse、wrap、完整 Grid 或浏览器兼容算法。Percent 以
`0..100` 表示，并以 containing content box 为基准；父节点经过 min/max、grow、stretch 或 absolute
inset 后，以最终 arranged content box 重新解析后代 Percent。默认 Auto root 直接以 viewport content box
作为确定 Percent 基准。width/height/min/max 都描述 border-box，padding 从 border-box 内部扣除；
Auto 轴在 Measure 阶段无法得到确定 containing size 时，Percent 不参与父级 intrinsic size，并增加
`lastLayoutPercentMeasureFallbackCount`；父级取得最终 arranged content box 后只解析一次 Percent，
不回头重新 Measure 父级或迭代求 fixed point，因此循环场景可能产生确定性 overflow。所有 style、
viewport 与候选几何都验证 finite/non-negative，计算溢出返回 `InvalidLayout`；
Min/Max 冲突固定使用 min-wins。失败不发布半份结构或 layout snapshot，也不清除 pending dirty。

当前覆盖 Px/Percent/Auto、margin/padding、row/column gap、Row/Column、grow、Start/Center/End/
SpaceBetween、Start/Center/End/Stretch、Absolute Overlay、Visible/Hidden/Collapsed，以及布局后的
min/max clamp。它是布局几何基础，不代表事件路由、clip policy、PaintCache、文本测量或 Widget
行为已经完成。

Text/Widget measure cache key 覆盖 available constraint、style revision、content revision 和字体
layout revision。虚拟列表只创建 visible + overscan item；100,000 行不产生100,000个 Node。

## 输入、路由与默认行为

本节分为已实现的 C1c-b2 synthetic route、C1c-b3b Runtime-private producer 和冻结的后续 Runtime 目标。C1c-b2 已提供 committed
hit/route-ancestry snapshot、纯 point query、固定容量 listener 注册与单条 synthetic Pointer input
的 Capture→Target→Bubble 派发；C1c-b3b 已把 Move/Button/Wheel consume 生成 frame-local consumption
view，但 claims 恒为 `None`。尚未实现 EngineHost UIContext ownership/selection/接线、Focus/Capture/Modal、
Button default action 或真实 continuous claim 输出。

Runtime 在每次状态命令提交后，根据 committed `GameStateStack`、`GameStatePolicy` 和 root
registration 生成每窗口不可变 `UIInputScopeSnapshot`。它按视觉层级列出 eligible roots，并在
首个 `blocksUIInputBelow` 处截断；UIContext 针对这一个集合做一次全局 hit-test 和一次路由，
不是对每个 State 依次 hit-test。这样“栈顶到栈底传播”表示 eligibility 计算顺序，不是重复分发。

State/root 变为 ineligible 时，在下一份 input snapshot 发布前完成以下事务：有效 Pointer Capture
先收到一次 `PointerCancel` 再释放；Focus 记录为该 root 的弱 `UINodeId` history，并转移到最上层 eligible
root 的默认焦点；Modal scope 不允许跨 root。重新变为 eligible 时，仅在 history 仍通过 owner +
generation 校验且未被新 modal 覆盖时恢复焦点。状态命令在路由结束后提交，因此当前 transition
始终使用同一份 scope snapshot。

- `PlatformFrameView` 保留 ordered transitions；每个需要 target 的 Pointer transition最多一次 hit-test；
- 已有 Pointer Capture 的 transition 直接解析 captured UINodeId，通常0次 hit-test；
- hit-test 逆 paint order 扫描 committed hit entries，以 bounds/clip 提前剪枝；首期不上 BVH，
  先记录 visited count，只有 profiling 证明需要才加分块索引；
- route path 使用预配置 fixed-capacity scratch，超过最大树深返回 `CapacityExceeded`，不 heap fallback；
- 每个路由阶段重新解析 UINodeId owner + generation；listener 按稳定注册顺序；
- 当前 C1c-b2 只执行 Capture -> Target -> Bubble listener dispatch，不执行 Widget default action；
- route 中新增 listener 从下一次 route 生效；删除/重置立即 tombstone，route cleanup 后回收 callback storage；
- 目标后续才保存 Pointer Capture；当前 synthetic route 只支持 primary pointer，不把预留字段宣称为触摸支持；
- Focus、Modal、Keyboard、Gamepad Accept/BackNavigation 与 Button default action 后续复用同一生命周期。

UI 返回只属于当前 Platform frame 的 `Tina::UI::InputTransitionConsumptionView`，以及当帧
`Tina::UI::ContinuousControlClaimsView`。这些 view 的 ABI 归 `tina_ui` 拥有，Runtime ActionMapper
只读取、不拥有、不回写；Gameplay Action Mapping 只读取未消费 transition 与未 claim control；
被 UI 消费的 digital Down 由 Action Mapper 跨帧抑制到真实 release，axis 抑制到 neutral。
Focus lost、断连和 `InputStreamReset` 产生 `InputCancelTransition`，不伪造可激活 Button 的普通 Up。UI routed
listener 不复用 Runtime EventBus；二者的生命周期、顺序和 payload 不同。
当前正式 `EngineHost` 还没有调用这一 producer，仍向 ActionMapper 传 canonical `None`；独立
`tina_runtime_ui_tests` 直接运行 GoogleTest，不通过 CTest，也不构成可交互 Widget/DisplayList 证据。

## PaintCache、DisplayList 与批处理

每个可绘制节点持久保存 backend-neutral local `PaintCache`。只有 Paint dirty 才重建 Quad/Image/
TextRun 数据；Order、layout 或 clip 改变更新 committed paint entries。无变化帧仍需遍历 visible
paint entries 组装当前 `UIDisplayListView`，但不重复 style、layout、text shaping 或 local paint。

```text
UIDisplayList
  ClipRect[]        已求交并确定性 intern 的 framebuffer scissor，ClipId 0 表示无 clip
  DrawCommand[]     Quad / ImageQuad / GlyphRange，保持 paint order
  GlyphInstance[]   本帧 FrameArena 数据
```

Draw command 只含 framebuffer geometry、UV、premultiplied color、ClipId 和 packet-local
`FrameResourceRef`。DisplayList extraction 把所用 Texture/Sampler/Atlas generation pin 登记到
Render SPI 的 `FramePinSink`，由 Runtime-private owning `RenderFramePacket` 保活；UI 不访问 packet
类型。禁止 Widget/UINode 指针、AssetHandle、bgfx handle、ViewId、state flag、uniform location
或 backend vertex declaration。

Renderer 只合并相邻、兼容命令：

```text
UI pipeline kind + Texture/Atlas page + Sampler + Blend + ClipId
```

DisplayList extraction 以规范化整数 framebuffer scissor 值做确定性 interning；相同 effective
clip 必须得到同一 `ClipId`。因此 batch key 比较 `ClipId` 等价于比较 clip 值，不会因节点来源
不同而错误拆批。

批处理不能跨透明 paint order 全局排序。batch 前后 paint checksum 必须一致；空 clip 直接剪枝。
首期只支持 axis-aligned scissor，rounded/stencil clip 后置。Frame capacity 超限返回 sticky
`CapacityExceeded`，不提交半份 DisplayList，也不回退 heap。

UI 内建 pipeline 由 Render module 持有；游戏 Widget 不能选择 shader/pipeline。bgfx 只存在于
`tina_render_bgfx` 私有实现，详细门禁见[渲染架构](rendering.md)。

## UTF-8、中文与 Glyph Atlas

Runtime 不按路径打开字体。Cooked `FontAsset` 提供 owning bytes、face metadata 和确定 fallback
chain；UI 持有 `AssetLease<FontData>`。FreeType 类型只存在于 `tina_ui_freetype`。

缓存键至少覆盖：

```text
TextLayoutKey = Font AssetId+generation / fallback revision / logical size /
                text revision / wrap constraint / locale+layout mode
GlyphKey      = face identity / glyph index / raster pixel size / render mode
GlyphHandle   = atlas page / slot / generation
```

Text measure/layout 与 glyph raster 分离：主线程先得到 glyph index、advance、kerning 和 line
metrics；Worker 只把 owning font bytes/lease + generation request raster 成 CPU bitmap。Glyph 未
上传时绘制确定 fallback box，但沿用目标 advance。因此 raster/upload completion 只标记 Paint
dirty，不重新 Measure；只有 fallback face 或 layout metrics 真变化才触发布局，避免字体到达时
命中区域抖动。

Atlas 使用固定 page size/page count/byte budget，首期优先 R8 coverage。raster completion 和
GPU upload 都是有界队列；满容量不能覆盖 in-flight UV。Page eviction/reset 只在 Deferred
Cleanup 且 Render ticket 退役后发生，generation 随之递增。迟到任务重新校验 Font/Atlas
generation。

首屏中文和 fallback chain 在 root 开放输入前预热。非法 UTF-8 转 U+FFFD 并产生结构化诊断。
首期明确支持 Latin/CJK codepoint、kerning 和显式换行；不宣称 Arabic/Indic、emoji sequence、
combining cluster 的完整 shaping。出现真实需求后再评估 HarfBuzz，不手写复杂 shaping。

## Theme、动画与可访问性

Theme 是每窗口值对象，至少包含 Color、Typography、Spacing、Radius、Border、FocusRing、
MinimumHitSize、AnimationDuration 和 ReducedMotion token。Widget 保存 token id + 局部 override，
不保存指向全局可变 Theme 的裸指针。Theme/DPI revision 只 dirty 实际受影响属性。

`UIAnimator` 维护 active-node list，只 sample 活跃 tween、caret 和 scroll，不递归 update 全树。
动画使用 unscaled real delta：

- opacity -> Composite；color -> Paint；
- translation -> Transform + Composite + HitTest；
- scroll -> Transform + Clip + Composite + HitTest；
- size -> Measure + Arrange，允许但标记昂贵，默认控件不用；
- ReducedMotion 把非必要 duration 置0；完成 action 在阶段末提交并支持目标自销毁。

所有交互 Widget 从第一版生成 `Semantics`：Role、UTF-8 Name/Description、Value/Range、Checked、
Enabled、Focused、Actions 和可选 `labelledBy UINodeId`。装饰节点过滤；Hidden/Collapsed 不进入
语义树，Disabled 保留语义但 Action 不可执行。平台 UIA/AT-SPI adapter 只读 immutable snapshot，
不能跨线程访问 UINode。密码和 composition 正文不进入诊断。

首批 Role：Group、Label、Button、Checkbox、Slider、TextEdit、Dialog、List、ListItem。

## Widget 范围

迁移保留 Panel、Label、Button、Image、ProgressBar、Dialog、ScrollView、虚拟 ListView 和单行
TextEdit。完成后增加设置页真实需要的 Checkbox、Slider；Dropdown、TreeView、多行 TextEdit、
复杂 shaping 和触摸手势按真实产品需求增加。

Checkbox：Pointer/Space/Enter/Gamepad Accept 走同一 default action，值实际变化才通知；disabled、
preventDefault 或 Modal 拦截时不改变。Slider：finite min/max/value/step、clamp、以 min 为原点的
稳定量化；Pointer capture、方向键、Home/End 和 Gamepad 共用语义，单帧只提交最终 change。

不建设反射式 Data Binding。`SettingsState` 持有明确 `SettingsModel`，Action 调用窄 Window/
Audio settings command；backend 失败恢复 model 并显示 UTF-8 错误。

## 线程与内存

- UIContext、Node registry、route、layout、PaintCache、snapshot 和 DisplayList build 都在主线程；
- Worker 只执行字体/图片 CPU 工作，不持有 UINode/UIContext/FrameArena 指针；
- completion 在主线程重新校验 owner + generation 后发布；
- GPU staging 跨帧使用独立 owner；DisplayList 的资源/Atlas pin 通过 FramePinSink 转移并由
  Runtime-private RenderFramePacket 保活，不借用
  Worker scratch/UI FrameArena；
- retained node/style/text/action/PaintCache 属 UI persistent tag；
- M7-C1b/C1c-a 的 `std::pmr::memory_resource` 覆盖 tree/id/style/pointer-policy/dirty、layout 与
  route-ancestry scratch，以及 committed structure/layout/hit 双缓冲；这只约束 Tina UI 的指定固定容量
  storage，不代表整个进程零 heap。`UIContext::Create` 的少量 control-plane allocations 默认 heap，
  off-thread `UIRootOwner` release 使用 Create 期预分配队列并由 owner thread 下一次操作 drain；
- layout scratch、route scratch、DisplayList 使用彼此独立且有上限的 arena/capacity；
- Arena 满返回结构化错误，不 heap fallback。

首期不并行 layout/route/DisplayList。只有 Tracy/tina_bench 证明单线程热点超过预算且可以保持
稳定顺序时，才讨论 immutable subtree 并行；不为了“看起来高性能”先引入锁。

## Metrics 与性能门禁

常驻 Metrics 至少记录：

```text
ui.nodes.total / visible / interactive
ui.dirty.measure / arrange / paint / order / semantics
ui.layout.pass_count / node_count
ui.input.transition_count / hit_test_count / hit_entries_visited / max_route_depth
ui.paint_cache.rebuild_count
ui.display.command / quad / glyph / clip / bytes
ui.batch.count / draw_count / texture_switch / clip_switch
ui.glyph.cache_hit / miss / raster_queued / upload_bytes / pages / evictions / failures
ui.memory.persistent / frame / atlas current+peak bytes
ui.time.route / layout / paint_cache / display_build / submit ns
```

p50/p95/p99 由 `tina_bench` 的预分配 sample buffer 离线计算，不在常驻 Registry 在线维护。

确定性硬门禁：

- 无变化 UI：Style resolve=0、Measure/Arrange=0、PaintCache rebuild=0、Tina heap allocation delta=0；
- 每窗口每帧 Layout pass <= 1；每个 Pointer transition hit-test <= 1；
- route/layout/DisplayList 无 heap fallback；
- dirty leaf 不无条件重排不相关 subtree；
- adjacent-compatible batching 数等于 compatibility runs + capacity split，paint checksum 不变；
- 5,000 logical node 和100,000行虚拟列表只处理 dirty/visible+overscan 集合；
- 无变化 UI、单 Label 更新、滚动列表、中文文本、Modal 和设置页分别记录 p50/p95/p99；
- 绝对毫秒预算等固定门禁机建立后再阻断，零分配、容量、顺序和资源归零立即阻断。

M7-C1b/C1c-a/C1c-b1/C1c-b2 已建立其中第一组可执行证据：50,000 节点深树的非递归 layout 与 committed hit
snapshot；首次发布后连续300次
同 viewport、无 mutation 的 `commitLayout()` 均为0 layout pass、layout revision 不变且 supplied
UI PMR allocation count 不增加。15项 hit snapshot 测试覆盖固定容量、policy、route ancestry、同一 view
内严格递增且唯一的 paint ordinal、revision、三快照失败回滚、stale generation 与 PMR 回收；新增5项
point query 测试覆盖反向目标选择、Ignore 穿透、world/clip 半开边界、非有限坐标 miss、revision/index
binding、visited count 与300次查询零新增 UI PMR allocation；C1c-b2 新增16项 route 测试覆盖稳定
Capture/Target/Bubble 顺序、stopPropagation、stopImmediatePropagation、dispatch 中 reset/add/destroy、
generation-safe target invalidation、listener 容量原子失败与复用、route depth 容量失败无 partial
callback、token move/context-destroyed/off-thread reset、callback root 自销毁、route 中 commit 拒绝、
错误销毁 context 的 death test、300次 route 零新增 supplied UI PMR allocation/不改变 committed state，
以及递归 route 拒绝。Windows MSVC 19.50 Debug/Release、Linux GCC 13.4、Linux Clang 22.1.8 +
libstdc++15.2 ASan/UBSan/LSan 的完整 `tina_ui_tests` 均为75/75；Clang 无 sanitizer 诊断。初次 GCC
暴露的 routed-pointer callback `requires` 名称可见性问题已修复，二次 GCC/Clang 构建无 warning。
它尚未证明 dirty leaf 不扫描无关 subtree，也未覆盖 PaintCache、
DisplayList、文本、Widget default action、EngineHost UIContext/producer 集成或 GPU 资源归零；这些门禁仍由后续切片补齐。

## 测试与实施顺序

必须直接 GoogleTest 覆盖：

- stale/cross-window UINodeId、所有构建的 owner 校验、generation 回绕 retire、Root owner rollback、route 自销毁；
- 每类 setter 的 dirty bit/传播、布局中新增 dirty 不丢、无变化0布局；
- Flex Px/Percent/Auto/MinMax/absolute、非有限输入和循环 Percent 诊断；
- ordered Down/Up/Wheel、capture/失焦、paint-hit 顺序、clip 和 consumption；
- UTF-8/fallback metrics 与 raster 分离、Atlas 满/退役、迟到 Worker；
- DisplayList 头文件无 backend 依赖、clip 求交、空剪枝、顺序保持 batch、capacity failure；
- active animation/reduced motion、Semantics 稳定顺序和敏感文本脱敏；
- 100%/150%/200% DPI 中文截图，Checkbox/Slider/Modal/TextEdit 和实体手柄人工矩阵；
- NullRenderDevice 连续300帧后 UI persistent/frame/atlas/resource count 归零。

实施顺序固定为：

1. 已完成 M7-C1a：`UIContext` / generation `UINodeId` / `UIRootOwner` / structure snapshot / input route-result view ABI；
2. 已完成 M7-C1b：layout value/dirty API、固定容量 PMR side array/queue/scratch、Flex-lite
   Measure/Arrange、structure+layout 原子发布与 committed layout snapshot；
3. 已完成 M7-C1c-a：固定容量 Pointer policy/route-ancestry scratch、双缓冲 committed hit snapshot、
   structure/layout/hit 事务发布和 hit-only 0 layout；
4. 已完成 M7-C1c-b1：无分配 point query、反向目标选择、route index/revision 与 visited count；
5. 已完成 M7-C1c-b2：generation-safe RAII listener token、fixed-inline callback、synthetic
   Capture→Target→Bubble route、stop/consume、mutation-safe invalidation 与 route/commit reentrancy guard；
6. 已完成 M7-C1c-b3b：独立 Runtime-private Move/Button/Wheel producer、raw ordinal consumption、
   canonical `None` claims、双预分配 PMR 与失败不可重放门禁；
7. 由 EngineHost 拥有/选择 `UIContext`，接入 producer 并实现真实 claims；
8. 让 dirty leaf 跳过无关 subtree，并输出后端无关 DisplayList；
9. 完成 PaintCache/text layout + glyph atlas；
10. 迁移 Focus/Capture/Modal/Scroll/List/TextEdit/IME/手柄语义与 Button；
11. 增加 Checkbox/Slider、Semantics、动画与截图门禁；
12. 性能基准证明需要后再扩展布局、空间索引、shaping 或并行。

所以“完善 UI”首先是完成正确的数据和 backend 边界，然后才是增加 Widget 数量。
