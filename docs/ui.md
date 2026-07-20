# 高性能自研 UI

> 状态：vNext 目标契约已接受。M7-C1b 已完成 C++23 standalone `tina_ui` 的事务式 Flex-lite
> layout；M7-C1c-a 已完成固定容量 Pointer policy/route-ancestry scratch 与双缓冲
> `UICommittedHitView` 数据基础；M7-C1c-b1 已完成无分配 `queryPointerHit()`、反向目标选择与
> visited count；M7-C1c-b2 已完成 synthetic routed pointer event：generation-safe RAII listener token、
> 固定容量 route path/listener storage、48-byte fixed-inline `noexcept` callback、Capture→Target→Bubble、
> stop/consume、路由中 mutation-safe invalidation 和 route/commit reentrancy guard。M7-C1c-b3b 已完成
> Runtime-private `UIInputRouteProducer`；M7-C1c-b3c 已让 `EngineHost` 私有地延迟创建/选择主窗口
> `UIContext`，并按 Platform lifecycle dispatch → UI route → ActionMapper 接入正式帧路径；
> M7-C1c-b3d1 已加入 focused `UIContextCapacityConfig`、`EngineConfig::primaryWindowUICapacities`
> 与 `updateUI` 后、Render 前的 Runtime-private layout coordinator；M7-C1c-b3d2 已加入 startup
> primary-window metrics seed、`onEnter` root builder 与 `updateUI` root-scoped updater；M7-C1c-b3e
> 已加入 held primary Pointer Button claim bridge。D0 已在 Runtime-private
> `PrimaryWindowUIDisplayCoordinator` 中把 primary-window UIDisplayList 作为 `RenderFrame`
> submit-call-local borrow 接入正式 submit 路径。D1/D2 已在此基础上完成 SolidFill-only local paint cache、
> 双缓冲 `UICommittedPaintView`、Render-owned 单帧 SolidQuad `UIDisplayListBuilder`、独立 UI→Render
> integration bridge、Game SDK scoped `setBoxPaint()` facade 与私有 bgfx SolidQuad UI Pass；Windows
> Desktop 样例已经能在真实 D3D11 surface 上显示4个 retained SolidFill panel，并验证 alpha blend 与
> framebuffer scissor。
> 后续 Button default action 切片已实现 `PrimaryPointerId + PointerButton::Primary` 的窄默认交互，当前
> dirty-subtree 切片又完成了 clean subtree 的 Measure/Arrange reuse：
> Button 默认 `Targetable`，Down/Move/Up 维护 pressed 状态，Up-inside 只激活一次；`preventDefaultAction()`
> 独立于 stop/consume/claim；`setButtonAction()`、`clearButtonAction()` 与 `isButtonPressed()` 已通过
> root-scoped facade 暴露；cancel/reset 清理状态但不合成 Up、不触发 action。
> Key/Gamepad/axis claim、持久 Pointer Capture、Focus/Modal、Button Keyboard/Gamepad activation、
> Disabled/theme 视觉、Image/Text/Glyph PaintCache、owning Runtime `RenderFramePacket`/FramePin、nested clip、
> 完整 dirty-range pruning、文本 Glyph Atlas、Label 文本与完整 Widget facade 尚未实现。Tina UI 是游戏内 Retained UI，
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
  -> private bgfx SolidQuad UI Pass
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

这是冻结的最终 ownership。M7-C1c-b3c 先实现其中的单主窗口 Runtime-private 子集：首个有效
primary `WindowId` 延迟绑定一个 `UIContext`，后续相同 owner/index/generation 复用同一 Context；绑定后
主窗口消失或换 generation 会以 `LifecycleInvariantViolation` 终止本次 run，不能静默迁移 retained UI。
最小化、logical/framebuffer metrics 或 content scale 变化不会重绑。Headless frame 在首次绑定前的
选择结果为 `nullptr`，仍可运行 Null 路径；shutdown 在 Render → Task → Platform → Clock modules 之前销毁 Context。
当前还没有通用多窗口 `WindowRecord` UI owner。

M7-C1c-b3d1 没有把 `UIContext` 暴露给 Game SDK。它只把 Context 的固定容量契约放进独立 public
`UIContextConfig.hpp`，由 `validateUIContextCapacityConfig()` 在 standalone UI 创建和
`EngineConfig::validate()` 两处共享同一规则；Runtime owner 使用已经验证的
`primaryWindowUICapacities` 创建首个 Context。`PrimaryWindowUILayoutCoordinator` 是 Runtime-private
phase owner，不是可保存的游戏能力。

M7-C1c-b3d2 仍不暴露裸 `UIContext*`，而是在 startup transaction 中先调用 Platform backend 的
`initialPrimaryWindowMetrics()`。该调用不 poll、不消耗 `PlatformFrameId`；有主窗口时 Runtime 在
`onEnter` 前显式绑定同 generation 的 primary-window `UIContext`，Headless seed 则显式进入无主窗口模式。
随后 Game SDK 只拿到 move-only、callback-scoped facade：`PrimaryWindowUIRootBuilder` 只能在
`GameStateEnterContext` 创建 root，`PrimaryWindowUITreeUpdater` 只能在绑定 `UIRootOwner` 的 enter/update
阶段修改 subtree。每个 facade 都带 phase epoch；回调正常返回、失败返回或异常边界回滚都会失效，跨 phase
调用返回 `UIPhaseCapabilityExpired`。若 Headless 或无 primary window，请求 UI capability 返回
`PrimaryWindowUIUnavailable`，该 phase 内的首个 UI 错误会 sticky 并阻止后续 mutation。
后续 listener 扩展仍使用同一 facade：注册动作只能发生在 current phase/current root subtree；返回的
move-only `UIRoutedPointerListenerToken` 是唯一可跨 phase 保存的 listener 对象，但不延长 Context、root
或节点生命周期。

Platform/Event 只把 `PlatformFrameView` 的 UI-eligible transitions 交给 UIContext，不拥有节点、Focus、Capture 或 Layout。
`IGameState` 持有 move-only `UIRootOwner` 和非 owning `UINodeId`，不持有 UIContext、Renderer、
UINode 裸指针或跨帧 writer。
`tina_ui` 仍只 PUBLIC 依赖 `Tina::Core` 与 `Tina::Platform`，`tina_render` 也不依赖 UI；只有独立
`tina_ui_render_integration` PUBLIC 依赖二者，负责 committed paint → DisplayList 转换。D0 的
Runtime-private coordinator 只消费该桥接结果，并把 borrowed DisplayList 塞入 `RenderFrame` 当前
submit 调用；D1 的私有 bgfx 后端同步消费当前 frame 内的 SolidQuad DisplayList，D2 的 Game SDK facade
允许状态代码设置 retained box paint。Font Asset、FrameResourceRef/pin、Text/Glyph 与资源型 UI 命令分别在后续
asset/render 切片接入，
不能把 Asset/Render 依赖塞回 UI tree/layout core。

UI module 最小基础类型（其中低层 builder/updater 不直接交给普通 Game SDK）：

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
class UIRootBuilder;     // UIContext owner-thread 低层 builder；Runtime facade 不直接暴露它
class UITreeUpdater;     // UIContext owner-thread/root-scoped 低层 updater
struct UICommittedStructureView; // owner-thread borrowed structure snapshot
struct UICommittedLayoutView;    // owner-thread borrowed layout snapshot
struct UICommittedHitView;       // owner-thread borrowed hit/route-ancestry snapshot
class UICommittedPaintView;      // owner-thread borrowed SolidFill paint snapshot
struct UIStraightSrgba8Color;     // authoring color
struct UIPremultipliedRgba8Color; // committed/cache color
struct UISolidFill;
struct UIBoxPaint;                // 当前只含 optional SolidFill
struct UIPointerHitTarget;       // owning target facts + snapshot-local route indices
struct UIPointerHitQueryResult;  // owning point-query result + revisions/visited count
struct UIPointerInputEvent;      // owning normalized pointer input for one synthetic route
struct UIPointerRouteResult;     // owning route result; not Runtime consumption/claim output yet
class UIRoutedPointerListenerToken; // move-only RAII listener registration
class UIRoutedPointerCallback;   // 48-byte fixed-inline noexcept callback
struct UIRoutedPointerListenerDesc;
struct UIButtonActionEvent;      // Button default action callback payload
class UIButtonActionCallback;    // move-only 48-byte fixed-inline noexcept callback
struct UISemanticsView;  // 只读 committed snapshot

struct InputTransitionConsumptionView;
struct ContinuousControlClaimsView;
struct UIContextCapacityConfig; // focused public config；由 EngineConfig 持有，不是 UIContext owner

} // namespace Tina::UI
```

`hasValue()` 只判断句柄是否非空，不声称对应节点仍存活；真正的 owner + generation 校验由绑定
目标窗口 registry 的 `UIContext::contains(UINodeId)` 或 `UITreeUpdater::isAlive(UINodeId)` 完成。
公开 API 不提供一个无法访问 registry 却名为 `isValid()` 的误导性成员。

`UIDisplayListView` 由 `Tina::Render` 拥有，并通过独立 Tina UI/Render integration SPI 传递，普通游戏代码
拿不到。所有构建都会先校验
`UINodeId.ownerWindow + generation`，Debug 额外携带 Engine/registry cookie 改善诊断；热点遍历在
一次校验后只使用 UIContext 内部的紧凑 slot index。generation 回绕的 slot 永久 retire。

M7-C1c-b3d2/D2、后续 listener 扩展与 Button default action 已落地的 Game SDK 访问形态如下
（`TRY(expr)` 仅表示失败时立即返回 `Error`；Label 文本内容、Button Keyboard/Gamepad activation
与 `GameStateCommands` 仍属于后续切片）：

```cpp
class SettingsPanelState final : public Tina::IGameState {
public:
    Core::Status onEnter(Tina::GameStateEnterContext& context) override {
        auto rootBuilder = TRY(context.primaryWindowUIRootBuilder());
        root_ = TRY(rootBuilder.createRoot());

        auto tree = TRY(rootBuilder.treeUpdater(root_));
        panel_ = TRY(tree.createPanel(root_.rootNodeId()));
        label_ = TRY(tree.createLabel(panel_));
        button_ = TRY(tree.createButton(panel_));
        TRY(tree.setLayoutStyle(root_.rootNodeId(), settingsRootStyle()));
        TRY(tree.setLayoutStyle(panel_, settingsPanelStyle()));
        TRY(tree.setBoxPaint(panel_, Tina::UI::UIBoxPaint{
                                    .solidFill = Tina::UI::UISolidFill{
                                        .color = {.red = 28, .green = 92, .blue = 148, .alpha = 255},
                                    },
                                }));
        TRY(tree.setButtonAction(
            button_,
            Tina::UI::UIButtonActionCallback{
                [this](const Tina::UI::UIButtonActionEvent&) noexcept {
                    launchRequested_ = true;
                }}));
        return Core::success();
    }

    Core::Status updateUI(Tina::UIUpdateContext& context) override {
        auto tree = TRY(context.primaryWindowUITreeUpdater(root_));
        if (!TRY(tree.isAlive(panel_))) {
            return Core::failure(Tina::UI::UIErrorCode::InvalidNode,
                                 "Settings panel no longer exists");
        }
        TRY(tree.setLayoutStyle(panel_, currentSettingsPanelStyle()));
        TRY(tree.setBoxPaint(panel_, currentSettingsPanelPaint()));
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
    Tina::UI::UINodeId label_{};
    Tina::UI::UINodeId button_{};
    bool launchRequested_ = false;
};
```

当前 routed Pointer callback 与 Button action callback 只写 State intent/model；`IGameState::updateFrame()` 再提交未来的
`GameStateCommands`。回调不访问全局 Runtime，不直接 push/pop 状态，也不捕获 Phase Context 或 UI
facade。State 如保存 listener token，`onExit()` 必须先 reset listener token，再释放 root；token 自身不保活 Context/root。

Button action 是节点拥有的 retained property：`setButtonAction()` 原子替换，`clearButtonAction()`、节点
删除或 root 注销负责撤销，不返回容易被临时析构的 RAII token。需要观察 Runtime EventBus 的
非 UI 订阅仍使用独立 RAII token。低层 routed Pointer listener 使用
`UIRoutedPointerListenerToken` 管理 callback registration；它与未来 retained Widget action、Runtime
EventBus subscription 是三套不同生命周期，不能混用。

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

新 root/节点完成结构、layout 和 snapshot commit 后，才能被后续 hit/paint snapshot 使用。
`commitLayout(viewportSize)` 是 Runtime 应使用的发布入口：它按需完整构建所有受影响候选，再在同一
事务内切换对应双缓冲；任何验证、算术溢出或容量失败都保留上一份 structure/layout/hit/paint snapshot 与
pending dirty。仅 Pointer policy 变化的 hit-only commit 不执行 Measure/Arrange，也不增加 layout
revision；paint-only commit 不执行 layout 或 hit rebuild。`commitStructure()` 只保留为 M7-C1a 结构诊断
seam；它可以单独发布结构并保留旧 layout/hit/paint，
Runtime 不得把它与 `commitLayout()` 拆成两个可观察发布阶段。

`UICommittedStructureView`、`UICommittedLayoutView`、`UICommittedHitView` 与 `UICommittedPaintView` 都是 owner-thread
borrowed view，分别在下一次对应成功发布或 `UIContext` 析构后失效，不是跨线程快照。节点不能保存 FrameArena
指针；Action 注册期允许在 UI persistent memory 分配，dispatch/layout/paint 热点不得产生稳态
heap allocation。

`UIContext::Create(ownerWindow, capacities, memory_resource)` 的 `memory_resource` 当前服务固定容量
tree/id、style/pointer-policy side array、dirty state/queue、layout 与 route-ancestry scratch、route path
scratch、routed pointer listener slots、local SolidFill cache，以及 committed structure/layout/hit/paint 双缓冲；
少量 control-plane 对象和 off-thread root release 队列在 Create 期间使用进程默认 heap 预分配。
`dirtyQueueCapacity`、`layoutSnapshotCapacity`、`hitSnapshotCapacity` 与 `paintSnapshotCapacity` 为0时从 `nodeCapacity` 派生，
非0时固定且不得超过节点容量；`routePathCapacity` 为0时同样从节点容量派生，`routedPointerListenerCapacity`
为0时从节点容量派生但可单独配置到最大1,048,576，因为一个节点可以注册多个事件监听。该 supplied PMR
范围不等于整个进程不使用 heap。`UIRootOwner` 在 owner
thread 析构时立即回收；若在其他线程释放，只把 root id 写入
预分配队列，下一次 owner-thread UI mutation/commit 再物理回收。
`UIRoutedPointerListenerToken` 同样是 move-only RAII：owner-thread reset 立即失效，包括 dispatch 中；
off-thread reset 进入有界队列，并在下一次 owner-thread mutation/route 前 drain。Token 在 `UIContext`
已经销毁后 reset 仍安全，但不能用来延长 context 生命周期。listener callback 使用48字节 fixed-inline
`noexcept` callable；超出容量或对齐要求的 callable 在编译期被拒绝，没有 allocator 或 heap fallback。

`UIContextCapacityConfig` 的 shared validator 要求 node/root 容量非0且不超过各自上限，root 不得超过
node；非0的 dirty/layout/hit/paint/route-path 容量不得超过 node，listener 容量不得超过1,048,576。
值为0的派生容量继续由 `UIContext::Create` 规范化为 node capacity。`EngineConfig::validate()` 在任何
backend factory 前把该领域错误包装为 `InvalidEngineConfig`，避免窗口/GPU 已创建后才发现 UI 预算非法。

## 坐标、DPI 与可见性

vNext 只保留一套 UI 坐标语义：

- layout、hit-test、Pointer event 全部使用 window-logical coordinate；
- 左上原点、X 向右、Y 向下；
- Platform 输出 logical pointer，UI 不先放大到 framebuffer 再命中；
- committed logical viewport 与 framebuffer viewport 这对实际 extent 只在 DisplayList extraction
  把 logical rect/clip 转为 framebuffer pixel，恰好一次；
- 用户 UI scale 通过 Theme typography/spacing/min-hit-size 影响布局，不等同 framebuffer scale；
- SolidQuad bounds/scissor 使用同一 extent ratio；glyph raster pixel size 属于后续文字契约，不能让
  shader 再从平台 content scale 隐式重复缩放。

当前 integration bridge 不读取或保存平台 content-scale 状态，而以 committed paint view 的 logical
viewport 与调用方提供的 framebuffer viewport 作为唯一权威 extent pair，分别计算 X/Y 比例。rect/clip
origin 用 `floor`，非零 end 用 `ceil`，再 clamp 到 half-open framebuffer viewport，避免缩放后漏掉覆盖
像素；零 logical/framebuffer extent 成功提交空 DisplayList。该转换只发生一次，Render backend 不再缩放。

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
style 与 dirty 均不变。dirty queue、style/dirty side array、layout scratch、layout work bits 与两套
committed snapshot 全部在 Create 阶段由 UI PMR 固定容量分配。当前 dirty-subtree 切片已完成
clean subtree 的 Measure/Arrange reuse：复用稳定的 prepared-input cache 和既有几何结果，Auto
祖先、Collapsed 子树、viewport/父约束变化以及候选失败都会回退到完整布局。统计中的
`lastLayoutMeasuredNodeCount`/`lastLayoutArrangedNodeCount` 只表示实际进入对应调度阶段的节点数；
`buildLayoutOrder`、父级 `arrangeChildren`、committed layout、hit 与 paint snapshot 仍可能线性遍历，
因此完整 dirty-range pruning、空间索引和并行 layout 尚未实现。无变化且 viewport 相同的 commit 则是0次
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

双缓冲 `UICommittedHitView` 保存所有 effective-visible route-ancestry entry，包括 policy
为 `Ignore` 的祖先；`Hidden`/`Collapsed` 子树不进入 snapshot。每项包含 `UINodeId`、snapshot-local
parent/root index、world rect、当前仅为 `viewport ∩ worldRect` 的 effective clip、`Ignore`/`Targetable`
与 paint ordinal。同一 view 内的 entry 按 paint ordinal 严格递增且唯一；view
携带 structure/layout/paint-order/hit revision。双缓冲 `UICommittedPaintView` 只保存 effective-visible、
非透明 SolidFill entry，每项包含 node、logical world rect/effective clip、同一稳定 paint ordinal 和
premultiplied RGBA8，并携带 structure/layout/paint-order/paint revision。布局、结构、policy 或 paint
改变后，成功的 `commitLayout()` 原子发布匹配的 structure/layout/hit/paint 四份 snapshot。

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
不路由、不伪造 Up，在 raw ordinal 空间保留 hole。listener consume 写回对应 ordinal bit；该 b3b
切片的 claims 当时恒为 canonical `None`。M7-C1c-b3e 新增
`UIRoutedPointerEvent::claimPointerButton(button)`：合法 button 在 callback-scope 固定 bitset 中幂等置位，
非法枚举返回 false；请求只代表当前 routed input 的 window/pointer，且不等同于消费 transition。
Runtime producer 只把最终 Platform snapshot 中仍 held 的 primary Pointer Button 转为去重 claim，
所以 Up 后的请求不会伪造跨帧 ownership。

producer 使用 Create 期双预分配 PMR consumption bitset 与 claim vector；supplied `memory_resource` 必须长于 producer。300帧复用
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

本节分为已实现的 C1c-b2 synthetic route、C1c-b3b Runtime-private producer、C1c-b3c EngineHost 接线、
C1c-b3d1 layout commit、C1c-b3d2 scoped Game SDK UI access、C1c-b3e Pointer-button claim
、后续 Game SDK listener facade、D0/D2 paint/display handoff，以及已实现的 Button default action。C1c-b2 已提供 committed
hit/route-ancestry snapshot、纯 point query、固定容量 listener 注册与单条 synthetic Pointer input
的 Capture→Target→Bubble 派发；C1c-b3b 已把 Move/Button/Wheel consume 生成 frame-local consumption
view；该切片的 claims 当时为 `None`。C1c-b3c 已在 Runtime 内部拥有并选择 primary-window `UIContext`，每帧在
同步 Platform lifecycle dispatch 之后、Gameplay ActionMapper 之前调用 producer。C1c-b3d2 已提供
`onEnter` root 创建与 `updateUI` root-scoped tree mutation。C1c-b3e 已发布最终快照仍 held 的 primary
Pointer Button claim 并接通 ActionMapper Cancel/suppression；后续 Button 切片已在同一 producer 阶段
实现 primary Pointer 默认交互。Key/Gamepad/axis claim producer、Focus/Capture/Modal、Button
Keyboard/Gamepad activation、Label 文本或完整可见 Widget draw 仍后置。

已实现的 Button default action 只覆盖第一条窄路径，不扩大为完整 Widget 系统：

```text
Primary Down 命中 committed route 中最近的 Button
  -> routed listeners: Capture -> Target -> Bubble
  -> 未 preventDefaultAction: arm + pressed + consume Down + claim Primary

held Primary Move
  -> pressed = committed hit 是否仍位于 armed Button subtree
  -> 继续请求 Primary claim；不隐式消费 Move

Primary Up
  -> 总是先清 armed/pressed，并消费属于该 Button interaction 的 Up
  -> 未 preventDefaultAction 且仍位于同一 live Button subtree
  -> 调用 Up route 开始时已经发布的 Button action 一次

non-gamepad-only cancel / covering stream reset / node or root destroy
  -> clear armed/pressed
  -> no fabricated Up, no Button action
```

`stopPropagation()`、`stopImmediatePropagation()`、`consumeInputTransition()`、
`claimPointerButton()` 与 `preventDefaultAction()` 是不同决定：停止传播不阻止 default action；consume/claim
不自动阻止 activation；prevent-default 也不撤销 listener 已完成的 side effect。Down prevent-default 不
建立新 armed state；Up prevent-default 仍执行不可跳过的状态清理但不 activation。Button 创建后默认
`Targetable`，Root/Panel/Label 保持 `Ignore`；显式把 Button 设置为 `Ignore` 可关闭它自身的 hit target，
但 targetable descendant 的 route ancestry 仍可包含该 Button。

Action 是 Button 节点拥有的 retained property：`setButtonAction()` 原子替换，`clearButtonAction()`、
Button/root 销毁撤销；`isButtonPressed()` 只查询当前 live Button 状态。callback 使用48字节 fixed-inline
`noexcept` storage，无 allocator fallback。`buttonActionCapacity` 限制 published action 数，0从 node
capacity 派生；实现额外预分配一个 transaction slot，保证 active capacity 已满时替换已有 action 仍可
先 staging、再按 generation/serial 原子发布。route 开始捕获 action serial boundary，因此 route 中新
set/replace 从下一条 route 生效，clear 则能阻止本条 route 尚未执行的 action。callback move/destructor
或 invocation 可以释放 Button/root；记录先 tombstone、route/callback cleanup 后再回收 storage，避免
slot reuse UAF。

这个私有 owner 只负责 identity 与 lifetime，不调用 `commitLayout()`。输入始终读取上一份已提交
hit snapshot；hit-test/route 不得为“方便”隐式触发布局。M7-C1c-b3d1 已由独立 coordinator 在
`IGameState::updateUI()` 成功后、Render submit 前使用主窗口 logical extent 提交本帧下一份 snapshot；
framebuffer extent、content scale 与 minimized 标志不替代 logical viewport。每个有效且严格递增的
`PlatformFrameId` 至多尝试一次；窗口与 Context 同时缺席的 Headless 帧成功 no-op，identity 或
`commitLayout()` 失败会阻断 Render，并保持本帧 attempt 已消费。布局职责没有塞回 owner selection。
D0 随后增加 Runtime-private `PrimaryWindowUIDisplayCoordinator`：它只在 layout/paint commit 后、
Render submit 前构建 primary-window UIDisplayList，使用 fixed PMR builder，并把 list 作为
`RenderFrame::primaryWindowUIDisplayList` 的 submit-call-local borrow 交给 backend。Headless、0 framebuffer
与 suspended surface 发布空 list；构建失败不保留旧 publication，也不提交截断 list。

M7-C1c-b3d2 已由 ADR 0021 接受并实现：Runtime 先从 Platform backend 取得不 poll、不消费 frame id
的 backend-neutral startup primary-window metrics seed，在 `onEnter` 前建立 Context，再只向游戏侧提供
root-scoped、phase-epoch-scoped `PrimaryWindowUIRootBuilder`/`PrimaryWindowUITreeUpdater`。前者只在
`GameStateEnterContext` 创建 root，后者在 enter/update 中只修改指定 `UIRootOwner` 的 subtree；Runtime
在回调结束失效 epoch，跨 phase 调用返回 `UIPhaseCapabilityExpired`。异常/错误边界通过无分配
`abortPhase()` 失效 facade，避免半开放 UI 能力跨过启动或帧回滚。
普通 Game SDK 不获得裸 `UIContext*`，也不能在任意阶段调用 `createRoot()`；root 创建成功只证明 retained
tree owner 已接入。D2 又证明 scoped `setBoxPaint()`、私有 bgfx SolidQuad pass 与最小可见 retained panel
链路已接通。后续 extension 又让同一个 root-scoped updater 注册 routed Pointer listener；返回 token 可由
State 跨 phase 保存，而 facade/updater 仍在回调结束失效。EngineHost 门禁证明 listener 在 ActionMapper
之前执行，claim-only callback 也可抑制同帧 Gameplay Action。Button default action 后续复用该阶段；
它仍不证明 Widget 文本、Keyboard/Gamepad activation、Focus/Capture/Modal、Text/Glyph 或完整产品 UI。

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
- C1c-b2 当时只执行 Capture -> Target -> Bubble listener dispatch；后续 Button 切片已在完整 listener
  propagation 之后执行上述窄 default action；
- 没有 hit 的 Move/Up 仍会
  更新或清理已有 armed state，因此不能从 `queryPointerHit()` miss 提前返回；
- route 中新增 listener 从下一次 route 生效；删除/重置立即 tombstone，route cleanup 后回收 callback storage；
  root-scoped updater 注册会校验 current root/subtree，跨 root/stale generation 原子失败且不占 slot；
- fixed-inline callback 的 move/destructor 可以执行用户代码；最终 move 后重新校验 root、node generation、
  subtree 与 registration serial，重入释放 root/节点会回滚整个 registration。callback operation 中销毁
  `UIContext` 触发生命周期 terminate；
- 目标后续才保存 Pointer Capture；当前 synthetic route 只支持 primary pointer，不把预留字段宣称为触摸支持；
- Focus、Modal、Keyboard、Gamepad Accept/BackNavigation 与 Button 后续完整 Widget 生命周期复用同一清理模型。

UI 返回只属于当前 Platform frame 的 `Tina::UI::InputTransitionConsumptionView`，以及当帧
`Tina::UI::ContinuousControlClaimsView`。这些 view 的 ABI 归 `tina_ui` 拥有，Runtime ActionMapper
只读取、不拥有、不回写；Gameplay Action Mapping 只读取未消费 transition 与未 claim control；
被 UI 消费的 digital Down 由 Action Mapper 跨帧抑制到真实 release，axis 抑制到 neutral。
Focus lost、断连和 `InputStreamReset` 产生 `InputCancelTransition`，不伪造可激活 Button 的普通 Up。UI routed
listener 不复用 Runtime EventBus；二者的生命周期、顺序和 payload 不同。
M7-C1c-b3c 的正式 `EngineHost` 路径先选择 Context，再把 producer 的 consumption/claims 直接交给
ActionMapper；Headless bind 前和当前无 root 的 Context 都自然得到无消费结果，不能退回由 Host 旁路构造
两份 `None`。本帧稍后的 b3d1 layout commit 不改变已经完成的 route 结果。独立
`tina_runtime_ui_tests` 直接运行 GoogleTest，不通过 CTest；当前 Game SDK 能创建/更新 retained root，
D0 也已提交借用式 primary-window UIDisplayList，D2 已补 scoped `setBoxPaint()` 并由 Desktop 样例证明
可见 SolidFill panel；后续 listener extension 只增加低层 routed callback/claim seam，Button default action
只增加 primary Pointer 的 pressed/activation 状态机。它仍不构成完整可交互 Widget、Label/Button 文本、
Keyboard/Gamepad activation、Focus/Capture/Modal、Text/Glyph 或 Scene UI overlay 的完成证据。

## PaintCache、DisplayList 与批处理

当前最小 local PaintCache 只覆盖 `UIBoxPaint` 的可选 SolidFill。authoring color 是 straight sRGBA8，
缓存与 snapshot 使用确定性整数 premultiplication 后的 RGBA8；same-value `setBoxPaint()` 是 no-op。
当前 `UICommittedPaintView` 只发布 effective-visible、非透明 fill，未包含 Image/Text/Glyph、资源引用、
opacity 合成或 nested clip。无变化 commit 不增加 paint revision，也不使旧 paint view 失效；paint-only
commit 不执行 layout/hit rebuild。D2 已把同一 setter 通过 `PrimaryWindowUITreeUpdater::setBoxPaint()`
暴露给 scoped Game SDK facade；facade 仍受 phase epoch、root scope、owner thread 和 sticky error 约束。

```text
当前 Render::UIDisplayListView
  UIPixelRect[]     first-seen interning 的 framebuffer scissor，ClipId 0 表示无 clip
  UIDrawCommand[]   SolidQuad，严格保持 paint order
  UIDrawBatch[]     仅相邻且 kind/ClipId 兼容的 command run
```

`Tina::Render::UIDisplayListBuilder` 不依赖 UI，在 Create 时从 supplied PMR 一次分配固定
command/clip/batch storage。它校验 strictly increasing paint ordinal、有效 premultiplied color 与 pixel
rect，剪枝空 bounds、透明色、空 clip 和 clip 外命令；clip 以规范化整数值首次出现顺序 intern，batch
只合并相邻兼容 SolidQuad，并记录 paint-order checksum。容量或输入失败进入 sticky failed build；
`commit()` 不发布截断 list。

builder 是有意选择的单缓冲 frame-local owner。`beginFrame()` 立即使上一份 borrowed
`UIDisplayListView` 失效；begin 成功后的失败/rollback 留下空 `publishedView()`，不会回退旧 list；
对 Published 状态直接调用 `rollback()` 则是 no-op。独立
`Tina::Integration::buildUIDisplayList()` 拥有完整 begin/add/commit transaction；只有
`beginFrame()` 自身失败时保留调用方已经打开的事务，一旦 begin 成功，validation、坐标转换或容量失败
都会 rollback。其 Windows MSVC 19.50 Debug/Release 12项直接 GoogleTest 均已通过。

当前 DisplayList 只有 framebuffer bounds、premultiplied color 与 ClipId，不含 UI node/Widget 指针，
也不含 bgfx handle、ViewId、state flag、uniform、backend vertex declaration 或 OS native 类型。
`tina_ui` 与 `tina_render` 不互相依赖；跨模块转换只在 `tina_ui_render_integration`。

完整目标再增加 ImageQuad/GlyphRange、packet-local `FrameResourceRef` 与 `FramePinSink`。DisplayList
extraction 届时把 Texture/Sampler/Atlas generation pin 转移给 Runtime-private owning
`RenderFramePacket`；UI 仍不访问 packet 或 backend 类型。Renderer 仍只允许合并相邻、兼容命令：

```text
UI pipeline kind + Texture/Atlas page + Sampler + Blend + ClipId
```

批处理不能跨透明 paint order 全局排序。batch 前后 paint checksum 必须一致；空 clip 直接剪枝。
当前只支持 axis-aligned scissor，rounded/stencil clip 后置。Frame capacity 超限返回 sticky
`CapacityExceeded`，不提交半份 DisplayList，也不回退 heap。

UI 内建 pipeline 由 Render module 持有；游戏 Widget 不能选择 shader/pipeline。D1 已有私有
bgfx SolidQuad UI Pass，可同步消费当前 submit-call-local DisplayList；D2 Desktop 样例已形成最小可见
retained panel。当前仍无 owning packet/FramePin、Image/Text/Glyph、Atlas 或资源 pin，所以该路径只覆盖
无纹理 SolidFill UI。bgfx 只存在于
`tina_render_bgfx` 私有实现，详细门禁见[渲染架构](rendering.md)。

## UTF-8、中文与 Glyph Atlas

M7-D0 已在 `tina_ui` 落地 Label/Button 的 retained UTF-8 文本属性：`setText`/`setTextStyle` 写入 Create
期固定 `textByteCapacity` 预算，严格 UTF-8（无嵌入 NUL）失败返回 `InvalidText`，容量不足返回
`CapacityExceeded`。Auto 尺寸节点使用确定性 monospaced placeholder metrics
（`measurePlaceholderText`，默认 logicalSize 16、advanceScale 0.6、lineHeightScale 1.2）。Game SDK
`PrimaryWindowUITreeUpdater` 已透出同一 API。

M7-D1 又让有文本且 `UITextStyle.color.alpha != 0` 的节点在 committed paint 中为每个 drawable
codepoint 发布 monospaced SolidQuad fallback（`\n` 只换行），paint ordinal 在 snapshot 内严格递增，并
计入 `paintSnapshotCapacity`。DisplayList/bgfx 仍只消费 SolidQuad。

M7-D3 增加后端无关 `IUITextRasterizer`：`createPlaceholderTextRasterizer` 始终可用；可选
`tina_ui_freetype` 提供 `createFreeTypeTextRasterizer`（仅 `TINA_BUILD_UI_FREETYPE` + feature
`ui-freetype`）。公共 SPI 不暴露 FreeType 类型。

M7-D4 让 `UIContext::Create` 默认拥有 placeholder rasterizer 并打开内置 face；可选注入自定义
rasterizer。Label/Button 的 `setText`/`setTextStyle` 通过 rasterizer `measure` 更新 metrics。

M7-D5 在 paint 阶段调用 `raster`，把 per-glyph advance 拷入 scratch 后按 UTF-8（含换行）发出
SolidQuad 色块；coverage R8 仍不上传，无 Glyph DisplayList / Atlas。

M7-D6：`tina_ui_freetype_tests` 使用仓库 `resources/fonts/SourceHanSansSC-Regular.otf` 作为测试
fixture 验证中文 measure/raster；Runtime 产品路径仍不得按路径打开源字体。

M7-D7：`UIGlyphAtlas` 是固定容量 CPU R8 shelf atlas（Create 预留 page/slot，insert 不隐式扩容；
generation `UIGlyphId`；clear 使旧 id 失效）。当前仅库级 API + `UIGlyphAtlasTests`。

M7-D8：`Tina::Render` DisplayList 支持 `Glyph` 命令与 `addGlyphQuad`（atlas UV/page、batch 切分）。

M7-D9：bgfx UI 使用 textured 程序 + R8 atlas 上传 API；Solid 绑 1×1 白贴图，Glyph 绑 atlas page。

M7-D10：UIContext paint 经 `UIGlyphAtlas` 发 Glyph paint entry；integration 转 `addGlyphQuad`；
Runtime 把 R8 page 借给 `RenderFrame`；bgfx 每帧 sync 上传。

M7-D11：`openTextFont` 可替换 face；Desktop 在 FreeType ON 时注入 FreeType rasterizer 并打开
`resources/fonts/SourceHanSansSC-Regular.otf` fixture。默认 Null 图仍用 placeholder（色块）。
产品路径仍不得按路径打开源字体；后续应迁到 cooked FontAsset（M10）。

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

- UIContext、Node registry、route、layout、PaintCache、snapshot 和 DisplayList build 都在主线程；当前
  DisplayList build 由独立 integration 调用 Render builder，不由 UIContext 隐式触发；
- Worker 只执行字体/图片 CPU 工作，不持有 UINode/UIContext/FrameArena 指针；
- completion 在主线程重新校验 owner + generation 后发布；
- GPU staging 跨帧使用独立 owner；DisplayList 的资源/Atlas pin 通过 FramePinSink 转移并由
  Runtime-private RenderFramePacket 保活，不借用
  Worker scratch/UI FrameArena；
- retained node/style/text/action/PaintCache 属 UI persistent tag；
- `UIContext` 的 `std::pmr::memory_resource` 覆盖 tree/id/style/pointer-policy/dirty、layout 与
  route-ancestry scratch、local SolidFill cache，以及 committed structure/layout/hit/paint 双缓冲；这只约束 Tina UI 的指定固定容量
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

M7-C1b/C1c-a/C1c-b1/C1c-b2 已建立第一组可执行证据：50,000 节点深树的非递归 layout 与 committed hit
snapshot；首次发布后连续300次
同 viewport、无 mutation 的 `commitLayout()` 均为0 layout pass、layout revision 不变且 supplied
UI PMR allocation count 不增加。15项 hit snapshot 测试覆盖固定容量、policy、route ancestry、同一 view
内严格递增且唯一的 paint ordinal、revision、snapshot 失败回滚、stale generation 与 PMR 回收；新增5项
point query 测试覆盖反向目标选择、Ignore 穿透、world/clip 半开边界、非有限坐标 miss、revision/index
binding、visited count 与300次查询零新增 UI PMR allocation；C1c-b2 新增16项 route 测试覆盖稳定
Capture/Target/Bubble 顺序、stopPropagation、stopImmediatePropagation、dispatch 中 reset/add/destroy、
generation-safe target invalidation、listener 容量原子失败与复用、route depth 容量失败无 partial
callback、token move/context-destroyed/off-thread reset、callback root 自销毁、route 中 commit 拒绝、
错误销毁 context 的 death test、300次 route 零新增 supplied UI PMR allocation/不改变 committed state，
以及递归 route 拒绝。M7-C1c-b3d2 另补3项低层 `UITreeUpdater` 子节点创建/跨 root/失效 root 测试；
M7-C1c-b3e 再补3项 claim 合并/非法值/无命中测试。Button default action 切片新增14项，覆盖默认
`Targetable`、Down/Move/Up pressed 与一次 activation、Up outside、nearest Button ancestor、
`preventDefaultAction()`、set/replace/clear、容量、route 中 mutation、callback 自毁、cancel/reset 清理和
300次 repeated click 零新增 supplied PMR。SolidFill paint 切片新增11项测试，覆盖确定性
premultiplication、visible/transparent 筛选、paint-only/no-op revision、四份 snapshot rollback、固定容量与
supplied PMR。后续 listener extension 再补 root-scoped/cross-root 原子注册、callback move 释放 root 的
事务回滚，以及 callback move 销毁 Context 的 death test。本轮 Button default action 后又有新增
UI 用例，当前 Windows Debug/Release `tina_ui_tests` 均为115/115（含6项 dirty-subtree reuse/回退测试）；Linux GCC 13.4、Linux Clang 22.1.8 +
libstdc++15.2 ASan/UBSan/LSan 仍为92/92，且 Clang 无 sanitizer 诊断，本轮未重跑 Linux 图。初次 GCC
暴露的 routed-pointer callback `requires` 名称可见性问题已修复，二次 GCC/Clang 构建无 warning。
Render builder 的11项测试随 Windows Debug、Linux GCC/Clang `tina_tests` 205/205 通过；D0 后
Windows Debug/Release 基础测试增至207/207，`tina_runtime_ui_tests` 在 D2 增至53/53，并且 Null 样例
300帧通过。Button default action 切片的 Windows Debug/Release 均为基础208/208、Runtime→UI60/60，并重跑
Null样例300帧；本轮未重跑 Linux。独立 UI→Render bridge 的12项直接 GoogleTest 在本轮 Windows Debug/Release 与前序两条 Linux 图均通过。
Linux Null 样例各运行300帧，Clang 无 sanitizer 诊断。D1 的私有 bgfx 几何测试覆盖 SolidQuad 展开为
4顶点/6个32位索引、ABGR 颜色、容量失败不写入、非法命令预检和连续300次复用 caller-owned storage；
Windows bgfx 专项增至16/16。D2 Desktop 样例在 Windows D3D11 上验证4个 retained SolidFill panel 可见、
alpha blend、右侧 scissor clip 和 root 回收。Linux 对 D1/D2 的真实 bgfx UI pass 与可见样例尚未复验。
这些结果证明了 clean subtree 的 Measure/Arrange 调度可以复用，但尚未证明完整 dirty range 不被
layout order、父级子节点扫描、hit 或 paint snapshot 触碰；也未覆盖 Image/Text/Glyph PaintCache、FramePin、
owning Runtime packet、Label/Button 文本、Button Keyboard/Gamepad activation 或完整产品 UI。M7-C1c-b3c 只补上 Runtime 私有
primary-window Context 生命周期与 producer 顺序；M7-C1c-b3d1 只补上容量配置与 phase-driven layout
commit；M7-C1c-b3d2 只扩大到 scoped Game SDK root/updater，D0 只扩大到 submit-call-local DisplayList
handoff，D2 才扩大到 SolidFill paint authoring 与最小可见 retained panel，但不扩大到完整 Widget 行为。

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
7. 已完成 M7-C1c-b3c：EngineHost 私有 primary-window `UIContext` 延迟绑定/同 generation 复用、
   生命周期门禁、modules 前销毁，以及 Platform lifecycle dispatch → producer → ActionMapper 接线；
8. 已完成 M7-C1c-b3d1：focused UI capacity config/validator、EngineConfig pre-factory validation，以及
   `updateUI` 后、Render 前每个 Platform frame 至多一次的 Runtime-private layout commit；
9. 已完成 M7-C1c-b3d2：startup primary-window metrics seed、`onEnter` root builder、`updateUI`
   root-scoped updater、phase epoch expiry/sticky/abort，不开放裸 Context 或任意阶段 root 创建；
10. 已完成 M7-C1c-b3e：primary Pointer Button claim 请求、held-filter、去重、双 PMR buffer、
    capacity/rollback 与 ActionMapper Cancel/suppress 闭环；
11. 已完成 SolidFill-only local paint cache、双缓冲 committed paint 与四份 snapshot 原子发布；
12. 已完成 Render-owned 单帧 SolidQuad DisplayList builder，以及独立 UI→Render bridge；Windows bridge
    Debug/Release、Linux GCC 13.4 与 Linux Clang 22 sanitizer 均为12/12，Clang 无诊断；
13. 已完成 D0 Runtime-private primary-window UIDisplayList handoff：layout/paint commit 后、Render submit
    前构建 submit-call-local borrowed list，Headless/0 framebuffer/suspended 为空 list；
14. 已完成 Game SDK scoped `setBoxPaint()` 与私有 bgfx SolidQuad UI Pass；Desktop 样例显示4个 retained
    SolidFill panel，并验证 alpha/scissor/root 回收；
15. 已完成 Game SDK root-scoped `addRoutedPointerListener()`、跨 phase token 生命周期、callback move
    重入回滚，以及 claim-before-ActionMapper 端到端门禁；
16. 已完成 `PrimaryPointerId + PointerButton::Primary` 窄 Button default action、retained
    `setButtonAction()`/`clearButtonAction()` 与 `isButtonPressed()` facade、cancel/reset 清理和 Runtime
    producer 合并；
17. 已完成 clean-subtree Measure/Arrange reuse、prepared-input cache、父约束/viewport/Collapsed 回退和
   候选失败后的 full-rebuild 保护；继续实现 Key/Gamepad claim producer、完整 dirty-range pruning、完整
   Widget authoring、owning RenderFramePacket 与 FramePin；
18. 完成 Image/Text/Glyph PaintCache、text layout + glyph atlas；
19. 迁移 Focus/Capture/Modal/Scroll/List/TextEdit/IME/手柄语义与 Button Keyboard/Gamepad activation；
20. 增加 Checkbox/Slider、Semantics、动画与截图门禁；
21. 性能基准证明需要后再扩展布局、空间索引、shaping 或并行。

所以“完善 UI”首先是完成正确的数据和 backend 边界，然后才是增加 Widget 数量。
