# ADR 0021：主窗口 UI 启动事务与阶段能力

- 状态：Accepted
- 日期：2026-07-18
- 接受日期：2026-07-18
- 实施状态：M7-C1c-b3d2 已落地 startup metrics seed、显式 bind、startup layout/hit snapshot 与
  root-scoped phase capability；D2 与后续 listener 兼容扩展另见下文。本 ADR 不代表完整 Widget 已完成。

## 背景

ADR 0014 已把 `createInitialState + onEnter + initial UI layout/snapshot` 定义为一个启动事务，
ADR 0011 也要求输入路由只读取已经提交的 hit snapshot。但是当前 Runtime 只能在首个
`PlatformFrameView` 到来后惰性创建 primary-window `UIContext`，所以 `onEnter` 无法建立 retained
root，第一帧输入也早于第一份布局快照。

直接在启动阶段调用 `pollFrame()` 会消费 `PlatformFrameId`、泵送事件，并破坏“启动回滚前没有帧
副作用”的既有门禁。直接把 `UIContext*` 或底层 `UIRootBuilder` 交给游戏代码，又会允许跨 root、跨
phase 或任意时刻修改 UI，重新形成 Service Locator 式的宽能力面。

## 决定

### Platform 启动指标种子

`IPlatformBackend` 增加 backend-neutral、owner-thread-only 的查询：

```cpp
[[nodiscard]] virtual Core::Result<std::optional<WindowMetricsSnapshot>>
initialPrimaryWindowMetrics() = 0;
```

- 查询只读取并刷新 backend 已拥有的 primary-window facts；不得调用事件 pump、不得生成
  `PlatformFrameView`、不得消费 `PlatformFrameId` 或 source sequence；
- Headless 或明确永远没有 primary window 的 backend 返回 `std::nullopt`。一个随后会发布 primary
  window 的 backend 不得先返回空值；
- 非空 seed 必须含有效 `WindowId`、非零 metrics revision、有限且大于0的 content scale。logical/
  framebuffer extent 可以为0，以保留最小化和零尺寸平台事实；
- 首个 `pollFrame()` 仍是唯一帧事实来源，frame id 从1开始。它的 primary identity 必须与 seed
  一致；同一窗口的 metrics revision 只能保持或前进，primary 消失或 generation 替换是生命周期错误；
- seed 查询观察到的 resize、scale、focus、visibility 或 minimize 变化不能吞掉首帧 lifecycle event。
  GLFW 保留 pending metrics dirty/event，首帧按最终 revision 合并发布；首个 `pollFrame()` 边界派生的
  WindowSurface snapshot 也必须与该 revision 保持同一事实，不得独立重采样 native 状态。

### 启动事务

Runtime 固定按以下顺序执行：

```text
begin startup transaction
  -> gameApplication.createInitialState()
  -> platform.initialPrimaryWindowMetrics()
  -> bind primary-window UIContext, or explicitly bind Headless
  -> initialState.onEnter(GameStateEnterContext + primary-window root capability)
  -> sample initialPolicy
  -> commit initial UI structure/layout/hit snapshot with logical extent
  -> atomically commit initial State + policy
```

seed 查询不提前到 `createInitialState()` 之前，保持 ADR 0014 已接受的游戏启动顺序。seed 获取、校验、
`UIContext::Create`、`onEnter`、root mutation、初始 layout/hit commit 任一步失败，都属于 startup commit
前失败：先销毁 candidate，使其 `UIRootOwner` 在 Context 仍存活时回收，再销毁 Context 并逆序关闭模块；
不得调用 candidate `onExit` 或 `IGameApplication::onShutdown`。

初始 commit 即使没有 root 也要发布可查询的空 hit snapshot。首个正式 Platform frame 的输入路由因此
只读这份已提交快照，不隐式触发布局；同帧 `updateUI` 后的常规 layout commit 只发布下一份快照。

### Game SDK UI 能力

公共 API 使用职责完整的名称，不把底层 `UIContext`、native window 或 Render backend 暴露给游戏：

```cpp
class PrimaryWindowUIRootBuilder final {
public:
    [[nodiscard]] Core::Result<UI::UIRootOwner> createRoot();
    [[nodiscard]] Core::Result<PrimaryWindowUITreeUpdater>
    treeUpdater(UI::UIRootOwner& rootOwner);
};

class PrimaryWindowUITreeUpdater final {
public:
    [[nodiscard]] Core::Result<bool> isAlive(UI::UINodeId node) const;
    [[nodiscard]] Core::Result<UI::UINodeId> createPanel(UI::UINodeId parent);
    [[nodiscard]] Core::Result<UI::UINodeId> createLabel(UI::UINodeId parent);
    [[nodiscard]] Core::Result<UI::UINodeId> createButton(UI::UINodeId parent);
    [[nodiscard]] Core::Status setLayoutStyle(
        UI::UINodeId node,
        const UI::UILayoutStyle& style);
    [[nodiscard]] Core::Status setPointerHitPolicy(
        UI::UINodeId node,
        UI::UIPointerHitPolicy policy);
    [[nodiscard]] Core::Status destroy(UI::UINodeId node);
};

class GameStateEnterContext final {
public:
    [[nodiscard]] bool hasPrimaryWindowUI() const noexcept;
    [[nodiscard]] Core::Result<PrimaryWindowUIRootBuilder>
    primaryWindowUIRootBuilder();
};

class UIUpdateContext final {
public:
    [[nodiscard]] bool hasPrimaryWindowUI() const noexcept;
    [[nodiscard]] Core::Result<PrimaryWindowUITreeUpdater>
    primaryWindowUITreeUpdater(UI::UIRootOwner& rootOwner);
};
```

`PrimaryWindowUIRootBuilder` 与 `PrimaryWindowUITreeUpdater` 是 move-only、owner-thread、phase-scoped
facade，并以 epoch 强制执行阶段寿命。Runtime 在每次 `onEnter`/`updateUI` 回调开始时发布新 epoch，
并在回调离开时无条件失效；保存
facade、updater 或 Context 到回调之外后，任何操作返回
`RuntimeErrorCode::UIPhaseCapabilityExpired`，不能访问悬空 owner。

游戏 State 只持久拥有 move-only `UIRootOwner`，并可保存 generation-safe `UINodeId`。Enter 阶段能
创建 root 并取得绑定该 root 的 updater；UIUpdate 阶段只能为当前 State 已拥有的 root 取得 updater，
不能创建新 root。所有 child create、query、style、hit policy 和 destroy 都重新验证 phase epoch、
owner window、root generation 与 subtree containment；跨 root parent/node 返回结构化 UI 错误。

Headless 的 `hasPrimaryWindowUI()` 为 false；主动请求 builder/updater 返回
`RuntimeErrorCode::PrimaryWindowUIUnavailable`。`GameStartupContext`、`FixedUpdateContext`、
`FrameUpdateContext`、`RenderSceneExtractionContext`、exit/shutdown Context 都不提供 UI mutation
capability。

每个 capability phase 保存 sticky first-error：第一次操作失败后，后续 mutation 不再执行；Runtime 在
callback 结束合并 callback `Status` 与该错误。这样失败帧不会发布半份 layout/hit snapshot。正常路径
不分配；错误对象允许走冷路径分配诊断字符串。

### 后续兼容扩展记录

本节记录 D2 与后续独立切片，不改写 b3d2 接受本 ADR 时的原始 API/实施事实。两次扩展都保持上述
primary-window、root、phase 三层权限模型：

- D2 增加 `PrimaryWindowUITreeUpdater::setBoxPaint()`，只 author 当前 SolidFill box paint；
- 后续切片增加 `PrimaryWindowUITreeUpdater::addRoutedPointerListener()`，并委派到同样 root-scoped 的
  `UI::UITreeUpdater`；注册动作只能发生在 current phase/current root subtree；
- 返回的 move-only `UIRoutedPointerListenerToken` 是唯一允许跨 phase 保存的 listener 对象，但不延长
  `UIContext`、`UIRootOwner` 或节点生命周期。State 在 `onExit()` 先 reset token，再释放 root；
- callback 最终 move/destructor 可能执行用户代码。若其重入释放 root/节点，注册在重新校验 generation、
  subtree 与 serial 后原子回滚，不占 listener slot/high-water；callback operation 中销毁 Context 触发
  生命周期 terminate；
- EngineHost 端到端门禁证明 listener 在 ActionMapper 前执行；即使不 consume transition，只 claim 当前
  仍 held 的 primary Pointer Button，也会抑制同帧 Gameplay Action。

这些扩展在当时仍不代表 Button default action、Focus、Pointer Capture、Modal、Text/Glyph、Label 文本或完整
Widget 已完成；后续 Button default action 切片已另行实现 primary Pointer 窄默认交互，但不扩大本 ADR 的
startup/root capability 边界。

### 性能与验收门禁

- seed 查询后首个 frame id 仍为1，首帧 metrics event/revision 与 WindowSurface 一致；
- startup 任意失败点验证 candidate/root/context/module 逆序回收，且 poll count、`onExit`、
  `onShutdown` 均为0；
- `onEnter` 创建的 root 在第一帧 route 前已有 committed hit snapshot；route 不触发布局；
- Headless 显式无 UI 可继续运行，窗口 backend 的空 seed、identity replacement/disappearance 必须失败；
- updater 跨 phase、跨 root、stale generation、wrong owner thread 都返回稳定错误；
- 无变化 UI 为0次 Style/Measure/Arrange、0次 PaintCache rebuild、0次 Tina heap allocation；每窗口每帧
  layout pass 仍只能为0或1；
- 公共 Runtime/UI header 不出现 bgfx、GLFW、Win32/X11/Wayland、SDL/SDL3 或 native handle；测试继续
  直接运行 GoogleTest，不注册 CTest。

本切片只闭合 retained root 创建、更新和首帧 snapshot 时序；在 b3d2 切片结束时，真实 continuous claims、Pointer
Capture、Focus/Modal、Button default action、paint snapshot/DisplayList、文本/中文字体和 bgfx UI pass
仍属后续切片，不能据此宣称 UI 已经可见或可交互。后续已分别补齐 held primary Pointer Button claim、
SolidFill paint/DisplayList/bgfx pass、setBoxPaint facade、listener facade 与 primary Pointer Button default action。

## 结果

- ADR 0014 的 initial UI transaction 获得可实现且不消费首帧的 Platform seam；
- Game SDK 能建立 retained tree，但能力按 primary window、root 和 phase 三层收窄；
- 需要为每个 Platform backend、测试 fake、Runtime startup 与 UI facade 增加实现和失败注入门禁；
- 每次 capability 调用多一次常数级 epoch/root 校验，这是换取悬空与越权安全的有意成本。

## 被拒绝方案

- 启动时调用一次 `pollFrame()`：会消费帧身份、泵送事件并破坏启动回滚；
- 把 GLFW/native surface 指标直接传给 Runtime：泄漏 backend 且与 committed Platform facts 分叉；
- 给 Game SDK `UIContext&`、`UIContext*` 或通用 EngineContext：权限过宽且可跨 phase 保存；
- 只靠注释禁止保存 updater：无法在运行时拒绝过期 capability；
- `updateUI` 每帧销毁重建 root：破坏 retained UI、dirty cache 和零分配目标；
- 为启动 UI 增加 heap fallback 或第二次隐式 layout：隐藏容量错误并让 hit/render snapshot 时序分叉。
