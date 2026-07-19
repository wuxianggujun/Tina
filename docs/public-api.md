# vNext 公共接口与生命周期规则

> 状态：分批实施。Core、M6-A 生命周期、M7-A Platform/Input、私有 GLFW adapter、M7-B1 Native
> Surface handoff，以及 M7-B2 私有 bgfx clear-only device、Desktop bootstrap 和300帧 backend
> 冒烟已落地；M7-C1b/C1c-a/C1c-b1/C1c-b2 C++23 standalone `tina_ui` tree/layout/committed-hit/
> point-query/synthetic routed-pointer foundation 已落地；M7-C1c-b3b Runtime-private
> `UIInputRouteProducer` 与独立测试 target 已落地；M7-C1c-b3c 已让 `EngineHost` 私有延迟绑定
> primary-window `UIContext`，并将 producer 输出接入 ActionMapper；M7-C1c-b3d1 已加入 focused
> `UIContextCapacityConfig`、`EngineConfig::primaryWindowUICapacities` 与 Runtime-private layout
> coordinator；M7-C1c-b3d2 已加入 startup primary-window metrics seed、`onEnter` 前显式 bind、
> startup layout/hit/paint snapshot 与 root-scoped、phase-epoch-scoped Game SDK UI facade。
> M7-C1c-b3e 已让 routed Pointer listener 请求接管仍处于 held 状态的 primary Pointer Button，并由
> Runtime 过滤、去重后发布 continuous-control claim。SolidFill-only committed paint、Render-owned
> 单帧 `UIDisplayListBuilder` 与独立 `Tina::UIRenderIntegration` target 也已落地；bridge 在 Windows
> MSVC 19.50 Debug/Release、Linux GCC 13.4 与 Linux Clang 22 sanitizer 构建中直接 GoogleTest 均为
> 12/12，Clang 无 sanitizer 诊断。D0 已通过 Runtime-private `PrimaryWindowUIDisplayCoordinator`
> 在 layout/paint commit 后、Render submit 前构建 primary-window UIDisplayList，并以
> `RenderFrame::primaryWindowUIDisplayList` 的 submit-call-local borrow 交给 backend。D1 已让私有
> `tina_render_bgfx` 消费 SolidQuad DisplayList；D2 已让
> `PrimaryWindowUITreeUpdater::setBoxPaint()` 进入 Game SDK facade，并让 Desktop 样例显示4个
> retained SolidFill panel。后续兼容扩展又加入 root-scoped
> `PrimaryWindowUITreeUpdater::addRoutedPointerListener()`，并由 EngineHost 端到端门禁证明 listener
> 在 ActionMapper 前执行、claim-only 可抑制同帧 Gameplay Action。后续 Button default action 切片已加入
> `PrimaryPointerId + PointerButton::Primary` 的 pressed/activation、`preventDefaultAction()` 与
> root-scoped `setButtonAction()`/`clearButtonAction()`/`isButtonPressed()` facade。Key/Gamepad/axis claim、
> 完整状态栈、worker、Scene/Asset/Audio、文本/Glyph/Widget 完整 UI、
> production Gamepad、完整 DPI 与
> Windows IMM32 仍是后续契约。

## 公开类型命名规则

公开名称必须同时表达“所属领域”和“具体职责”，不能要求使用者先阅读实现才知道它是什么：

| 名称 | 从名称即可判断的职责 |
| --- | --- |
| `IGameApplication` | 整个游戏程序的启动与关闭入口 |
| `IGameState` | 状态栈中一个可逐帧运行的菜单、关卡或覆盖层 |
| `GameStateCommands` | 延迟提交的状态 push/pop/replace/policy-change 请求 |
| `RenderSceneExtractionContext` | 只在 Render Scene Extraction 阶段有效的能力视图 |

因此不采用 `IGame`、`IClient`、`IManager`、`Context`、`Handle` 这类缺少角色限定的独立名称。
接口前缀 `I` 只表示纯抽象契约，后面的完整角色名仍不可省略；`GameStateStack` 是 Runtime
private 实现名，不进入 Game SDK，游戏只能通过 `GameStateCommands` 请求变化。实现类应使用产品含义命名，
例如 `TinaGameApplication`、`MainMenuState` 和 `Game2DState`，而不是 `GameImpl`。

## API 分级

Tina 不把所有可链接 header 都称为“用户 API”：

| 层级 | 面向对象 | 主要类型 |
| --- | --- | --- |
| Game SDK | 普通游戏代码 | `IGameApplication`、`IGameState`、`GameStateCommands`、World/Camera/component、AssetHandle、UI Widget |
| Engine Module SPI | Tina 模块、backend adapter 和测试 | EngineCompositionFactories、PlatformFrameView、RenderDevice typed handle/descriptor、RenderFrame、FramePinSink、Pass Scheduler |
| Backend Private | 具体 adapter | GLFW、bgfx/bx/bimg、FreeType、miniaudio、EnTT 等第三方类型 |

当前 Runtime Game SDK 只实现 `EngineHost`、`IGameApplication`、单个 `IGameState`、`GameStatePolicy`、
Platform 生命周期订阅、Action Snapshot、最小 Phase Context，以及受限的 primary-window
`PrimaryWindowUIRootBuilder`/`PrimaryWindowUITreeUpdater`。standalone `Tina::UI` 已实现 tree/layout/
hit/route 与 SolidFill committed paint，`Tina::Render` 已实现后端无关 SolidQuad DisplayList，独立
integration target 已闭合二者的坐标转换；M8-B 已把固定容量、后端无关的 Camera2D/Sprite2D
`RenderSceneWriter` 接入 extraction callback，并把 committed view 作为 submit-call-local
`RenderFrame::primaryWorldScene` 交给 backend。D0 已把 primary-window DisplayList 以相同 borrow 规则放入
`RenderFrame`，D1 已由私有 bgfx backend 消费 SolidQuad，D2 已让 Game SDK facade
暴露 `setBoxPaint()` 并跑通 Desktop 4-panel 可见样例；后续 Button default action 切片又暴露
`setButtonAction()`/`clearButtonAction()`/`isButtonPressed()` 并接入 primary Pointer activation。当前仍没有
owning frame packet、FramePin、Text/Glyph、Label 文本、Button Keyboard/Gamepad activation 或完整 Widget facade。表中
Asset/UI Widget、`GameStateCommands`、typed render handle/descriptor 和 Pass Scheduler 均未实现；M8-A 的
`Tina::Scene::World`、`EntityId` 与 Transform standalone 边界见下文，尚未接入 Runtime World capability。

完整 Game SDK 不提供 RenderDevice、GPU resource handle、native window/surface 或第三方 factory。
已落地的 `tina_bootstrap_desktop` 提供只使用 Tina 类型的 `Desktop::CreateEngine(config)`，让普通
游戏无需知道当前生产组合实际使用 GLFW/bgfx。FreeType、miniaudio 与对应 UI/Audio 模块仍按
后续垂直切片接入，但不得改变该公开边界。

## ABI 范围

vNext 承诺同一仓库、同一工具链构建下的 C++ source API，不承诺跨编译器/CRT 的稳定插件 ABI。
`std::span/string_view/pmr` 可以在 Tina targets 间使用，但不能据此加载任意第三方二进制插件；
未来插件/脚本边界需要独立 C ABI 或序列化协议 ADR。

所有已安装/Game SDK header 必须零第三方类型、宏和传递 include。EnTT 作为 Scene 内部存储也
不能出现在 Game SDK；“内部使用第三方”从来不是公开类型例外。

## Core 公共基础

Core 头文件从 `include/tina/core/...` 暴露，调用方不需要 `src` include root：

```cpp
namespace Tina::Core {

template<class Value>
using Result = std::expected<Value, Error>;

using Status = Result<void>;

enum class ErrorDomain : std::uint16_t;

struct ErrorCode {
    ErrorDomain domain;
    std::uint32_t value;
};

class IMonotonicClock {
public:
    virtual ~IMonotonicClock() = default;
    [[nodiscard]] virtual MonotonicTimePoint now() const noexcept = 0;
};

} // namespace Tina::Core
```

`Result/Status` 是 alias，因此每个返回它们的函数自身都必须写 `[[nodiscard]]`。Error domain/code
使用显式、只追加的稳定编号；Core 通用错误写成 `CoreErrorCode::InvalidArgument`，模块错误使用
`RuntimeErrorCode`、`AssetErrorCode` 等完整领域名，避免一个无限增长的全局 enum。

## 最小启动接口

当前可用启动接口为：

```cpp
class EngineHost final {
public:
    [[nodiscard]] static Core::Result<std::unique_ptr<EngineHost>> Create(
        const EngineConfig& config,
        EngineCompositionFactories factories) noexcept;

    [[nodiscard]] Core::Result<RunExitReason> run(IGameApplication& gameApplication) noexcept;
};
```

`EngineCompositionFactories` 是高级集成/测试 SPI。当前 `tina_sample_null` 显式注入 Clock、Headless
Platform、Disabled TaskSystem 和 NullRenderDevice；`tina_sample_platform` 只把 Platform factory
换成私有 GLFW adapter，仍使用 NullRender。WindowSurface 组合已通过
`WindowSurfacePlatformRenderFactories` 支持 lease/snapshot/deferred publish handoff；M7-B2 已实现
私有 bgfx backend、Desktop 组合、300帧 backend smoke 与 SolidQuad UI pass，但它仍不等于完整
Render/Scene/Text/Widget UI pass。`EngineHost` 由 main 的 `unique_ptr` 唯一拥有。

Create、run 与析构属于同一 owner-thread 生命周期：跨线程 `run` 返回 `WrongOwnerThread` 且不
消耗 run-once；跨线程析构在调用 native backend 前 `terminate`。Game SDK 不提供把 EngineHost
转交后台线程运行/销毁的捷径。

面向普通桌面游戏的后续组合入口为：

```cpp

namespace Desktop {

[[nodiscard]] Core::Result<std::unique_ptr<EngineHost>>
CreateEngine(EngineConfig config);

} // namespace Desktop
```

`Desktop::CreateEngine` 已在 `tina_bootstrap_desktop` 中实现当前生产组合；它是组合
helper，不是 Singleton，也不能从游戏代码全局查询。当前 vNext target 只承诺 build-tree consumer；
正式 `Tina::GameSDK`、`install(EXPORT ...)`、版本化 package config 与外部 SDK consumer 门禁仍后置，
当前不伪装成可安装 SDK。

## IGameApplication：游戏程序入口

名称明确表达“整个游戏程序的生命周期入口”；它不是 World、Scene 或逐帧玩法接口。

```cpp
class IGameApplication {
public:
    virtual ~IGameApplication() noexcept = default;

    [[nodiscard]] virtual Core::Result<std::unique_ptr<IGameState>>
    createInitialState(GameStartupContext& context) = 0;

    virtual void onShutdown(GameShutdownContext& context) noexcept = 0;
};
```

- `createInitialState` 每次 `run()` 只调用一次；返回恰好一个 initial State；
- M6-A 在 initial State `onEnter` 成功并采样 `initialPolicy()` 后 commit；创建或 enter 失败自动
  回滚，不调用 candidate `onExit` 或 Application `onShutdown`；M7-C1c-b3d2 已把 backend-neutral
  startup metrics seed、`onEnter` 前 primary `UIContext` bind、root-scoped phase capability 和首份
  UI structure/layout/hit snapshot 纳入启动事务；
- commit 后无论正常退出还是帧错误，当前 State `onExit` 后调用一次 `onShutdown`；完整状态栈
  加入后扩展为所有 committed State 按规定顺序退出；
- 它没有 fixed/update/render/UI 回调；
- 可以拥有不依赖 Engine handle 的 Settings/Save repository，并通过构造函数显式传给 State；
- 不得保存 Phase Context、AssetLease、EntityId、UINodeId、Render handle 或 Engine module owner。

完整示例见[游戏程序入口与状态栈](gameplay.md)。

## IGameState：唯一帧行为入口

```cpp
struct GameStatePolicy {
    bool blocksGameplayInputBelow = false;
    bool blocksUIInputBelow = false;
    bool blocksFixedUpdateBelow = false;
    bool blocksFrameUpdateBelow = false;
    bool blocksRenderBelow = false;
};

class IGameState {
public:
    virtual ~IGameState() noexcept = default;

    virtual Core::Status onEnter(GameStateEnterContext& context) = 0;
    virtual void onExit(GameStateExitContext& context) noexcept = 0;
    [[nodiscard]] virtual GameStatePolicy initialPolicy() const noexcept = 0;

    virtual Core::Status fixedUpdate(FixedUpdateContext&) {
        return Core::success();
    }
    virtual Core::Status updateFrame(FrameUpdateContext&) {
        return Core::success();
    }
    virtual Core::Status extractRenderScene(RenderSceneExtractionContext&) const {
        return Core::success();
    }
    virtual Core::Status updateUI(UIUpdateContext&) {
        return Core::success();
    }
};
```

Menu、Settings、Game2D、Game3D、Pause 都实现 `IGameState`。默认空帧方法避免 UI-only State
编写无意义 boilerplate。World、UI roots、订阅、Asset lease 和 State TaskGroup 只属于已提交
State。

M6-A 只持有一个 committed State：`initialPolicy()` 已采样，但尚无下层 State 可传播或阻断；
`GameStateCommands`、栈传播与状态提交均未实现。以下传播和命令规则是完整 Runtime 目标。

Gameplay Input、UI Input、Fixed Update、Frame Update 从栈顶向下按各自 committed policy 传播，
Render Scene Extraction/UI visible roots 从最底可见层向上。`initialPolicy()` 只在成功 `onEnter`
后采样一次；Runtime 持有唯一 committed policy，后续只能通过状态命令修改，不能靠成员变量
悄悄改变当前帧传播。

`push/pop/replace/policy-change` 只通过 `FrameUpdateContext::gameStateCommands()` 排队，在 Frame
Update 结束后的 State Transition Commit 提交。首期 `blocksRenderBelow` 同时决定下层 World 和
UI root 可见性。

## GameStateCommands：后续唯一状态变化入口

```cpp
struct GameStateCommandId {
    std::uint64_t value = 0;
};

enum class GameStateCommandKind : std::uint8_t {
    Push,
    PopSelf,
    ReplaceSelf,
    PolicyChange,
};

struct GameStateCommandCompletion {
    GameStateCommandId id;
    GameStateCommandKind kind;
    Core::Status status;
};

class GameStateCommands {
public:
    [[nodiscard]] Core::Result<GameStateCommandId>
    requestPush(std::unique_ptr<IGameState> state);

    [[nodiscard]] Core::Result<GameStateCommandId> requestPopSelf();

    [[nodiscard]] Core::Result<GameStateCommandId>
    requestReplaceSelf(std::unique_ptr<IGameState> state);

    [[nodiscard]] Core::Result<GameStateCommandId>
    requestPolicyChange(GameStatePolicy policy);
};
```

- `GameStateCommands` 绑定当前 `IGameState`，不暴露可变 `GameStateStack`；
- 结构和 policy command 都只允许当前栈顶 State 请求；首期每个 State 每帧总共至多接受一个
  command，避免 replace 与 policy change 指向同一旧 State 的歧义。下层 State 得到
  `NotTopState`，重复请求得到 `AlreadyQueued`；
- 接受 push/replace 后 candidate 所有权立即移交 Runtime；立即拒绝或后续 enter 失败时直接
  rollback + 析构，均不调用 candidate `onExit`。Enter 期间创建的 Task 可以执行，但 completion
  在 commit 前不可发布；失败回滚固定执行 close ingress → requestStop → barrier/join → 逆序释放
  staged owner → 析构；
- 固定容量满返回 `CapacityExceeded`；Stopping/Exit callback 中没有 commands capability；
- 提交按 command sequence 稳定排序。pop 最后一个 State 表示正常结束 `run()`；
- 请求成功只表示已排队，并同时预留一个固定容量 completion slot；没有 slot 时请求返回
  `CompletionCapacityExceeded`。`FrameUpdateContext::completedStateCommands()` 返回
  callback-lifetime `std::span<const GameStateCommandCompletion>`；记录由 Runtime owning，不能跨
  callback 保存。来源 State 被遮挡时有界保留，下一次实际执行其 `updateFrame()` 时交付并在
  callback 返回后消费。slot 状态固定为 `Reserved → Completed → Delivered → Free`；来源先退出
  则为 `Completed → ReportedToDiagnostics → Free`，commit/exit 当场把记录转移到有界 Diagnostics
  ring（溢出只累计 dropped metric）并立即释放原 slot，不能因 pop/replace 稳定泄漏；
- policy change 隐式以当前 State 为目标，下一次 commit 后才影响传播。

## Phase Context

Context 是不可复制/移动、只在当前 callback 有效的 capability view：

| 当前 Context | 当前可访问能力 |
| --- | --- |
| `GameStartupContext` / `GameStateEnterContext` | 只读 `EngineConfig` 与 callback-scope `PlatformEventSubscriptions` 注册门面 |
| `FixedUpdateContext` | `FrameTiming`、`FixedUpdateTiming` 与目标 tick 的 `SimulationActionSnapshot` |
| `FrameUpdateContext` | `FrameTiming`、当帧 `FrameActionSnapshot` 与 `requestExitAfterFrame()` |
| `RenderSceneExtractionContext` | 只读 `FrameTiming` 与 phase-local `RenderSceneWriter` |
| `UIUpdateContext` | 只读 `FrameTiming` 与已拥有 root 的 phase-scoped UI facade |
| `GameStateExitContext` / `GameShutdownContext` | `RunStopCause` 与可选 Runtime failure |

`runtimeFailure()` 返回 callback-only 的只读借用，只保证在当前 `onExit`/`onShutdown` 调用期间
有效；回调可以复制稳定 code/message 供诊断，但不得保存 Error 指针或 Context。

当前 Context 不提供 raw Platform Input、World、Asset 或 Task；M8-B 已提供 Render writer，M7 已提供
root-scoped UI facade。完整垂直切片将继续按能力逐项扩展；下表同时标记当前能力与后续目标：

| Context | 可访问能力 | 明确禁止 |
| --- | --- | --- |
| `GameStartupContext` | 只读 config/capability、游戏级启动回滚 | World/UI root、RenderDevice、保存 Context |
| `GameStateEnterContext` | World、`PrimaryWindowUIRootBuilder`、Input Context、订阅、TaskGroup 的事务创建 | 裸 `UIContext*`、commit 前发布 Task completion、直接激活半成品、修改旧栈、backend 类型 |
| `FixedUpdateContext` | fixed timing、Simulation Action、World query/command、当前 TaskGroup | Frame/UI Action、Window/RenderDevice、保存 Arena span |
| `FrameUpdateContext` | 每 Render Frame 一次的 real/update delta、Frame Action、Asset snapshot、GameStateCommands | Simulation edge、直接 commit 状态、阻塞 IO |
| `RenderSceneExtractionContext` | 当前：interpolation、`RenderSceneWriter`；后续：只读 World/Asset ready snapshot | 修改 World、创建 GPU 资源、访问 bgfx、保存 writer |
| `UIUpdateContext` | 为已拥有 root 创建 phase-epoch-scoped `PrimaryWindowUITreeUpdater`、retained model/style/action/dirty | 创建新 root、跨 phase 保存 updater、每帧重建 UIContext、直接提交 Render/backend |
| `GameStateExitContext` | TaskGroup 已 join 后读取退出原因；State 释放自己的 RAII owner | 创建新 Task/Asset/State、直接操作 Runtime registry |
| `GameShutdownContext` | 最终退出原因和诊断、游戏级注销 | 创建新 Engine 工作 |

Context 不提供 `services()`、`resolve<T>()` 或 `EngineContext*`。Task 只能捕获 owning/immutable
输入和 generation id，不能捕获 Context、writer 或 FrameArena span。

Builder/Writer 使用 sticky first-error：第一次失败后后续 append 为空操作，Runtime 在 callback
结束强制合并 callback Status 与 writer error，不能提交半帧。

## FrameTiming 与输入域

```cpp
struct FrameTiming {
    Duration realDelta;
    Duration acceptedRealDelta;
    Duration rejectedRealDelta;
    Duration updateDelta;
    Duration discardedSimulationDelta;
    Duration fixedDelta;
    double interpolation;
    std::uint64_t frameIndex;
    std::uint64_t completedSimulationTicks;
    std::uint32_t fixedStepCount;
};

struct FixedUpdateTiming {
    Duration fixedDelta;
    std::uint64_t simulationTickIndex;
    std::uint32_t fixedStepIndexInFrame;
    std::uint32_t fixedStepCountInFrame;
};
```

`realDelta` 未缩放但经过有限性检查；`FixedStepAccumulator` 先把它钳制为
`acceptedRealDelta`，再应用 gameplay time scale 得到 `updateDelta`。超出真实 delta 上限的部分
记为 `rejectedRealDelta`；最多4步之后仍存在的完整 Simulation 步记为
`discardedSimulationDelta`，只保留小于一步的余量计算 interpolation。time scale 只影响玩法，
不影响 Platform/UI/Asset/Audio/diagnostics wall timeout。

M7-A 已实现 `SimulationActionSnapshot` 与 `FrameActionSnapshot`：fixed context 只读取目标 tick 的
Simulation Action，frame context 只读取当帧 Frame Action；0步帧保留 edge，最多4个追赶步也只在
第一个目标 tick 消费一次。

## M7 PlatformFrame 与 Action 接口

M7-A 已实现下列职责完整的 Platform/Input 名称；M7-C1a 已把 UI consumption/claim 的公开 view ABI
迁到 `Tina::UI`，Runtime ActionMapper 只消费这些 view：

| 类型 | API 层 | 职责与寿命 |
| --- | --- | --- |
| `WindowId` | Game SDK / Module SPI | 带 generation 的窗口标识，不能还原 native handle |
| `PrimaryWindowConfig` | Desktop bootstrap / Module SPI | UTF-8 title、logical extent、窗口模式和能力策略的纯值配置 |
| `WindowMetricsSnapshot` | Game SDK 只读 / Module SPI | logical/framebuffer extent、content scale、focus/minimized/visible 与唯一 metrics revision；不保存 Render surface suspended |
| `PlatformPollResult` | Engine Module SPI | `ContinueFrame{PlatformFrameView}` 或 `ExitRequested` 的 tagged union；失败只通过外层 `Result` |
| `PlatformFrameView` | Engine Module SPI | 只存在于 Continue 分支的 Poll/Input-phase borrowed view，包含 metrics/input/device snapshot、platform event 和 transition batch；Engine 结束该 phase 后失效 |
| `WindowInputSnapshot` | Engine Module SPI | 该窗口在本次 Poll 结束时的 held/pointer 最终状态，引用同一 metrics revision；M7-A 只允许 `PrimaryPointerId` |
| `GamepadSnapshot` | Engine Module SPI | Engine 级最终 sampled state；同一帧必须来自同一 registry owner，slot index 唯一且最多16个 |
| `InputTransitionBatch` | Engine Module SPI | 按 platform sequence 排序的有界 transition；不保存 GLFW key code 之类 backend 值 |
| `PlatformEventBatch` / `PlatformEventDispatcher` | Engine Module SPI / Runtime integration | Window metrics、Gamepad lifecycle/reset 的有界帧批次与 Runtime-owned 同步分发器；不是通用 Runtime Event Queue |
| `PlatformEventSubscriptions` / `PlatformEventSubscription` | Game SDK | 只允许注册 callback 的窄门面与 generation-safe RAII token；Game 不能取得 dispatcher owner |
| `Tina::UI::InputTransitionConsumptionView` | UI-owned view ABI / Runtime ActionMapper consumer | 只读 view，借用 route-result producer 的 frame storage，标记本帧哪些 transition ordinal 已由 UI 消费 |
| `Tina::UI::ContinuousControlClaimsView` | UI-owned view ABI / Runtime ActionMapper consumer | 只读 view，声明当前 UI route 的 digital/axis/pointer ownership seam；M7-A mapper 只消费 digital claim，axis/pointer continuous mapping 后续实现 |
| `InputActionId` | Game SDK | 显式的语义 Action 标识，不暴露物理键值为 gameplay contract |
| `SimulationActionSnapshot` | Game SDK | 当前 simulation tick 的 state/axis + 有序 edge batch |
| `FrameActionSnapshot` | Game SDK | 仅当前 Render Frame 可用一次的 camera/presentation/UI 外围 Action |

`PlatformFrameView` 及其 span 只在当前 Poll/Input phase 有效，不能保存；backend storage 即使到
下一次 Poll 才复用，也不延长 API 借用寿命。Runtime 先让 UI 产生
`Tina::UI::InputTransitionConsumptionView` 和 `Tina::UI::ContinuousControlClaimsView`，再由
Runtime-owned ActionMapper 映射 Gameplay Action。M7-A 被消费/claim 的 digital
Down 建立 `suppressedUntilRelease`，匹配 Up 只解除 suppression，不补发 Gameplay edge；axis/pointer
identity 目前只是已冻结 seam schema，等后续 analog binding 才实现 dead-zone/neutral suppression。
Focus lost、设备断开和 `InputStreamReset` 取消 transient edge、repeat、Capture 与 composition，
但不把跨帧状态粗暴清零：M7-A Action Mapper 对 resync 后仍 held 的 digital control 保留
`suppressedUntilRelease`；Simulation latch 插入 reset marker 并保留最终 action state。它们
都不伪造会激活按钮的普通 Up。

M7-C1c-b3c 已按上述顺序接入正式 run path：同步 `PlatformEventDispatcher` 完成后，Runtime-private
owner 选择 primary-window Context，producer 使用上一份 committed hit snapshot 路由，再把 consumption/
claims 交给 ActionMapper。b3c 阶段或当前 State 尚未创建 root 时，Context 不会消费输入；b3d1 在本帧
更晚的 `updateUI` 后提交下一份 layout，不改变已发布的 route 结果。这不是
可见 UI pipeline 或 Widget 已完成的证据；D0 后续只在 Render submit 调用内借用式提交
primary-window UIDisplayList。

M7-A 的 Pointer snapshot、Pointer Button/Move/Wheel transition 与 pointer binding 只接受
`PrimaryPointerId`（0），多 Pointer 是后续契约。M7-C1c-b3a 已让 Button/Wheel transition 携带
事件时 window-logical position；它与帧末 Pointer snapshot 明确分离，Runtime UI route 不得用后者
倒推历史命中位置。`PlatformFrameBuilder` 还要求同一帧所有
`GamepadSnapshot` 使用同一 `GamepadId` owner，且一个 slot 只能出现一个 generation；Connect/Disconnect
必须与最终 snapshot 及同 Poll 的 cancel/reset 证据一致。

Action Mapper 在处理本帧 transition 后，对跨帧保留的 `active`/suppressed physical source 再读取最终
snapshot：仍保留的 Key、Primary Pointer Button 或 Gamepad Button 必须仍为 held。Primary Window 或
Gamepad generation 在 retained state 尚未取消时消失/替换属于 `LifecycleInvariantViolation`，不能把旧
source 静默迁移到新 owner/generation。

M7-A 的 Action Mapper 由 Runtime 拥有；`EngineConfig::inputActions.digitalBindings` 是唯一注册入口，
首批只有 priority=0 的 Engine default Input Context。raw/text/platform-event 容量由
`platformFrameCapacities` 唯一配置；Simulation/Frame action/binding 容量由
`inputActions.capacities` 配置；订阅 slot 由 `platformEventSubscriptions` 配置。M7-C1c-b3b private
producer 已由 M7-C1c-b3c 接入 `EngineHost`；M7-C1c-b3e 又发布已去重且在最终 Pointer snapshot 中
仍 held 的 primary Pointer Button claim。内部 continuous claim 上限默认64且不属于 game-facing Action
Map 配置；Runtime 以同一容量创建 producer 的两份 PMR claim buffer。批次另有不可被普通项
占用的 reset slot，运行期不得扩容。

0 fixed-step 帧不把 Down→Up 压成布尔值；Runtime 保存有界、有序
`SimulationActionTransitionBatch`，并把它绑定到“下一个未完成 simulation tick”。第一个实际
fixed step 消费 batch 一次，同帧后续3个追赶步只读最终 held/axis。回放记录最终
Action state、ordered edges 和明确 target tick，不记录 GLFW 或 UI node。

`FixedUpdateContext::simulationActions()` 只暴露目标 tick snapshot；
`FrameUpdateContext::frameActions()` 只暴露当帧 snapshot。窗口关闭是不可取消的
`PlatformPollResult::ExitRequested`，该分支不创建 `PlatformFrameView`、不分配 engine frame index；
Runtime 在 Poll 后、新帧开始前停止，既不向当前 `PlatformEventDispatcher` 发布，也不向未来通用
Runtime Event Queue 重复发布同义 `CloseRequested`。游戏内 Escape/退出按钮继续通过 Frame Action 调用
`requestExitAfterFrame()`，保证当帧逻辑阶段与 Deferred Cleanup 完整结束；Active surface 正常
submit/present，Suspended surface 返回明确 skipped 结果且不伪造 GPU submission。

当前 `PlatformEventDispatcher` 只同步分发 `PlatformEventBatch` 中的平台生命周期通知，并由
`PlatformEventSubscriptions` 提供 generation-safe RAII 订阅；未来通用 Runtime Event Queue 面向
Gameplay/Domain/异步模块事件，当前尚未实现，也不复用 dispatcher 的 owner、容量或投递语义。

## vNext UI tree/layout/hit/route/paint public surface

当前 C++23 standalone `Tina::UI` public surface 只依赖 `Tina::Core` 与 `Tina::Platform`，不出现
FreeType、bgfx、GLFW、Legacy UI 或 Runtime 类型。核心值类型和入口为：

```cpp
enum class UILayoutLengthUnit : u8 { Px, Percent, Auto };
struct UILayoutLength;
struct UILayoutStyle;
enum class UIDirty : u16;
enum class UIPointerHitPolicy : u8 { Ignore, Targetable };
struct UIPointerHitTarget;
struct UIPointerHitQueryResult;
struct UIPointerInputEvent;
struct UIPointerRouteResult;
class UIRoutedPointerCallback;
class UIRoutedPointerListenerToken;
struct UIRoutedPointerListenerDesc;
struct UIStraightSrgba8Color;
struct UIPremultipliedRgba8Color;
struct UISolidFill;
struct UIBoxPaint;
class UICommittedPaintView;

struct UIContextCapacityConfig {
    usize nodeCapacity;
    usize rootCapacity;
    usize dirtyQueueCapacity;
    usize layoutSnapshotCapacity;
    usize hitSnapshotCapacity;
    usize paintSnapshotCapacity;
    usize routePathCapacity;
    usize routedPointerListenerCapacity;
};

[[nodiscard]] Core::Status validateUIContextCapacityConfig(
    const UIContextCapacityConfig& config);

class UITreeUpdater {
public:
    [[nodiscard]] Core::Status setLayoutStyle(
        UINodeId node,
        const UILayoutStyle& style);
    [[nodiscard]] Core::Status setPointerHitPolicy(
        UINodeId node,
        UIPointerHitPolicy policy);
    [[nodiscard]] Core::Status setBoxPaint(
        UINodeId node,
        const UIBoxPaint& paint);
    [[nodiscard]] Core::Result<UIRoutedPointerListenerToken>
    addRoutedPointerListener(
        UIRoutedPointerListenerDesc descriptor,
        UIRoutedPointerCallback callback);
};

class UIContext {
public:
    [[nodiscard]] Core::Status commitLayout(UILogicalSize viewportSize);
    [[nodiscard]] UICommittedLayoutView committedLayout() const noexcept;
    [[nodiscard]] UICommittedHitView committedHit() const noexcept;
    [[nodiscard]] UICommittedPaintView committedPaint() const noexcept;
    [[nodiscard]] UIPointerHitQueryResult queryPointerHit(
        UILogicalPoint point) const noexcept;
    [[nodiscard]] Core::Result<UIRoutedPointerListenerToken>
    addRoutedPointerListener(
        UIRoutedPointerListenerDesc descriptor,
        UIRoutedPointerCallback callback);
    [[nodiscard]] Core::Result<UIPointerRouteResult> routePointerInput(
        const UIPointerInputEvent& input);
};
```

- `UILayoutStyle::size` 与 `minMax` 的 width/height 都是 border-box；Percent 使用 `0..100`，
  相对父节点最终 arranged content box，包括 min/max、grow、stretch 或 absolute inset 后的结果。
  默认 Auto root 以 viewport content box 作为确定 Percent 基准；Auto 循环在 Measure 阶段不计入
  父级 intrinsic size，并记录 `lastLayoutPercentMeasureFallbackCount`，Arrange 只解析一次且不求 fixed point；
- layout 支持 Px/Percent/Auto、margin/padding/gap、Row/Column/grow、justify/align/stretch、
  Absolute Overlay、Visible/Hidden/Collapsed 与 min-wins clamp；所有 style、viewport 和候选几何
  必须 finite/non-negative，溢出返回 `UIErrorCode::InvalidLayout`；
- `UIContextCapacityConfig` 位于 focused `UIContextConfig.hpp`；shared validator 要求 node/root 非0且不超过
  各自上限、root 不超过 node、非0的 dirty/layout/hit/paint/route-path 不超过 node，并限制 listener 最大值；
- `UIContextCapacityConfig::dirtyQueueCapacity/layoutSnapshotCapacity/hitSnapshotCapacity/paintSnapshotCapacity/routePathCapacity` 为0时从 node capacity 派生，
  `routedPointerListenerCapacity` 为0时也从 node capacity 派生但可单独配置；这些值都是运行期不可扩容
  的固定容量。supplied PMR 在 Create 阶段分配 tree/id/style/dirty side array、Pointer policy、dirty queue、
  layout/route-ancestry scratch、route path scratch、listener slots、local SolidFill cache 与 committed
  structure/layout/hit/paint 双缓冲；
- `routedPointerListenerCapacity` 最大为1,048,576；dispatch 不扩容也不 heap fallback；
- `UIRoutedPointerCallback` 只接受48字节 fixed-inline、`noexcept` move/destruct/invoke 的 callable，
  超出大小、对齐或异常契约的 callable 在编译期拒绝，没有 allocator fallback；
- `setLayoutStyle()` 对 same normalized value 是 no-op；值变化先预检 node→root 全路径容量，再
  原子合并 dirty。容量不足时 style/dirty 均不改变；
- `commitLayout()` 完整构建后原子发布 pending structure、候选 layout/hit/paint snapshot；非法 viewport、
  计算溢出或容量失败保留四份旧 published snapshot 和 pending dirty。`commitStructure()` 仅为 M7-C1a
  诊断 seam，可单独发布结构并保留旧 layout/hit/paint；Runtime
  发布不得将二者拆开；
- viewport 变化会重排；相同 viewport 且无 structure/layout dirty 时不执行 layout pass、不增加
  layout revision。当前 dirty-subtree b4a 已复用 clean subtree 的 Measure/Arrange 调度、prepared-input
  cache 与既有几何结果，并在父约束/viewport/Collapsed/候选失败时回退完整布局；但
  `buildLayoutOrder`、父级 `arrangeChildren`、committed layout、hit 与 paint snapshot 仍可能线性遍历，
  因此完整 dirty-range pruning 尚未实现。

`UICommittedLayoutView` 是 owner-thread borrowed view，携带对应 structure/layout revision；在
下一次成功发布新 layout 或 `UIContext` 析构后失效。它只发布 logical local/world rect、effective
clip、effective visibility 与稳定 ordinal，不是跨线程、跨 commit 的 owning snapshot，也不证明
Image/Text/Glyph PaintCache 或完整 Widget 行为已实现。phase-driven layout commit
已由 b3d1 Runtime-private coordinator 接入；b3d2 又允许 Game State 通过 scoped facade 创建 root 和基础
retained node；D0 会从已发布 paint 构建 submit-call-local DisplayList，D1/D2 已跑通 SolidFill panel
可见路径，后续 Button default action 已覆盖 primary Pointer activation，但它仍只覆盖 colored quad 与
窄按钮交互，不覆盖文本或完整 Widget 行为。
当前 `effectiveClip` 仅表示 `viewport ∩ worldRect`；祖先 clip policy 与 hit/paint clip chain 尚未实现。

`UICommittedHitView` 同样是 owner-thread borrowed 双缓冲 view。它保存所有 effective-visible
route-ancestry entry（包括 `Ignore` 祖先），每项包含 snapshot-local parent/root index、world rect、
effective clip、`Ignore`/`Targetable` 和 paint ordinal；ordinal 在同一 view 内严格递增且唯一。
view 携带 structure/layout/paint-order/hit revision。仅 policy 变化的 hit-only commit 不执行 layout，
不增加 layout revision。M7-C1c-b1 的 `queryPointerHit()` 反向扫描 committed entries，只接受
`Targetable` 且同时位于 world rect/effective clip 的首个目标；边界为 left/top inclusive、right/bottom
exclusive，非有限坐标安全返回未命中。结果携带 target/root entry index、四类 snapshot revision 与
visited count；查询不执行 layout/hit rebuild、不分配、不派发事件。

M7-C1c-b2 的 synthetic route 入口是 `routePointerInput(const UIPointerInputEvent&)`。它只处理单条
backend-normalized pointer transition，position/delta 保持 window logical coordinate，校验
`PlatformFrameId`、source sequence、owner `WindowId`、primary pointer、kind、finite position/delta 与 button。
route 对上一份 committed hit snapshot 最多执行一次 point query，使用固定容量 route path scratch，并按
Capture→Target→Bubble 派发匹配 kind/phase 的 listener。`UIPointerRouteResult` 是 owning value，携带 point
query、route depth、listener invocation count、consumed/stopped/targetInvalidated，以及 listener 通过
`claimPointerButton()` 请求的固定大小 Pointer Button bitset；它仍不是 Runtime
`InputTransitionConsumptionView` 或 `ContinuousControlClaimsView`。`UIRoutedPointerListenerToken` 是
generation-safe move-only RAII owner：owner-thread reset 立即生效，off-thread reset 进入 bounded queue 并在下一次
owner-thread mutation/route 前 drain，context 销毁后 reset 仍安全。低层 `UITreeUpdater` 的注册入口只接受
绑定 root subtree 内的节点；跨 root/stale generation 失败不占 listener slot，也不推进 high-water。
fixed-inline callback 的 move/destructor 可以执行用户代码，因此最终 move 后会重新校验 root、node generation、
subtree 与 registration serial；若重入释放 root/节点则整次注册回滚。callback operation 期间销毁 Context
触发生命周期 terminate，不能继续潜在 UAF。当前没有持久 Pointer Capture、Focus/Modal、完整 dirty-range pruning、
nested clip、Button Keyboard/Gamepad activation 或完整 Widget default behavior。
`UIContext` 的 mutation、route 与销毁只允许 owner thread；route callback/callback cleanup 内销毁或
非 owner-thread 销毁会触发生命周期硬门禁，而不是继续执行潜在 UAF。

### SolidFill committed paint 与 UI→Render integration SPI

`UIBoxPaint` 当前只含可选 `UISolidFill`。authoring 使用 straight sRGBA8，`premultiply()` 以确定性整数
规则生成 `UIPremultipliedRgba8Color`；`setBoxPaint()` 对 same value 是 no-op。`UICommittedPaintView`
是 owner-thread borrowed 双缓冲 view，只包含 effective-visible、非透明节点的 `UINodeId`、logical
world rect/effective clip、严格递增 paint ordinal、预乘色及 structure/layout/paint-order/paint revisions。
下一次成功 paint publication 或 Context 析构使其失效；paint-only commit 不执行 layout/hit rebuild，
成功 no-op commit 不使旧 view 失效。

Render 与 integration 的公共窄接口为：

```cpp
namespace Tina::Render {
struct UIPixelRect;
struct UIPremultipliedRgba8;
struct UISolidQuadInput;
struct UIDisplayListCapacity;
class UIDisplayListView;

class UIDisplayListBuilder final {
public:
    [[nodiscard]] static Core::Result<UIDisplayListBuilder> Create(
        UIDisplayListCapacity capacity,
        std::pmr::memory_resource& storage);
    [[nodiscard]] Core::Status beginFrame();
    [[nodiscard]] Core::Status addSolidQuad(const UISolidQuadInput& input);
    [[nodiscard]] Core::Result<UIDisplayListView> commit();
    void rollback() noexcept;
};
}

namespace Tina::Integration {
struct UIRenderViewportMapping {
    Render::UIPixelRect framebufferViewport;
};
struct UIRenderDisplayListBuild;

[[nodiscard]] Core::Result<UIRenderDisplayListBuild> buildUIDisplayList(
    Render::UIDisplayListBuilder& builder,
    UI::UICommittedPaintView paintView,
    UIRenderViewportMapping mapping);
}
```

`tina_ui` 与 `tina_render` 不互相依赖；只有 `tina_ui_render_integration` PUBLIC 依赖二者。
bridge 以 paint view 的 logical viewport 和 framebuffer viewport 计算 X/Y 比例，对 origin 使用 floor、
对非零 end 使用 ceil，并 clamp 到 half-open framebuffer viewport；空 logical/framebuffer viewport
成功发布空 list。它完整拥有一次 builder transaction：若 `beginFrame()` 失败，调用方已打开的事务保持
原状；一旦 begin 成功，后续 validation、projection 或 capacity 失败都 rollback。

`UIDisplayListBuilder` 仅从 supplied PMR 使用 Create 期固定的 command/clip/batch storage；当前只支持
SolidQuad、axis-aligned clip first-seen interning、相邻兼容 batching、paint-order checksum 与空/透明/
clip 剪枝。它是单缓冲：`beginFrame()` 立即使旧 borrowed view 失效，失败的 replacement 不保留旧 view，
也不发布截断的新 view。该 SPI 不包含 bgfx、GLFW 或 OS native 类型；FrameResourceRef/pin、Image/Text/
Glyph 与 Runtime `RenderFramePacket` 尚未实现。私有 bgfx SolidQuad pass 已在 `tina_render_bgfx`
实现，但不扩大此公共 SPI。

D0 的 Runtime-private `PrimaryWindowUIDisplayCoordinator` 使用这条 SPI，但不把自身暴露给 Game SDK 或
普通 module public header。它在 layout/paint commit 成功后、`IRenderDevice::submitFrame()` 前，用
fixed PMR builder 构建 primary-window list，并把 `UIDisplayListView` 作为
`RenderFrame::primaryWindowUIDisplayList` 的 submit-call-local borrow 交给 backend。backend 必须在
`submitFrame()` 调用内同步消费、复制或编码；返回后禁止保留 view、span 或元素指针。Headless、0
framebuffer 与 suspended surface 路径发布空 list；构建或容量失败会清空当次 publication，不保留旧
list，也不提交截断 list。D1 的私有 bgfx backend 已消费该 borrowed list；backend 仍不得在
`submitFrame()` 返回后保存任何 borrowed UI view。

### M7-C1c-b3b/b3c Runtime-private route producer 与 primary-window owner

`UIInputRouteProducer` 是 `tina_runtime` 私有实现，不进入 Game SDK 或普通 Module public header。它只接受
同一 primary Window 的 Pointer Move/Button/Wheel，按 raw transition ordinal 调用 `routePointerInput()`；
Button/Wheel 使用 transition 自带的事件时 logical position。reset、cancel 与非 Pointer transition 保留
ordinal hole，不路由、不合成 Up；在 b3b 切片中 claims 当时恒为 canonical `None`。

producer 使用两份 Create 期预分配的 PMR consumption bitset，成功发布时交换 buffer；supplied
`memory_resource` 是借用依赖，必须比 producer 活得更久，300帧共用同一 PMR 的直接测试中 allocation
count 不增长。失败测试先让 root Move listener 产生1次 side effect，再让后续深层 Button route 因 route
path capacity 失败：staging view 不发布、旧 published view 保持，但 attempted frame/sequence watermark
已推进；同一 frame retry 被拒且 callback 仍为1，明确证明 side effect 不回滚也不重放。独立
`tina_runtime_ui_tests` 直接运行 GoogleTest，不使用 CTest，并与 Legacy UI 所在的
`tina_legacy_tests` 分离。

M7-C1c-b3c 没有增加 Game SDK public type。`EngineHost` 内部的 primary-window owner 在首次有效
`WindowId` 出现时创建并绑定一个 `UIContext`，相同 owner/index/generation 持续复用；绑定后的窗口
消失或 replacement generation 是 `LifecycleInvariantViolation`，终止本次 run 而不迁移 retained state。
最小化、logical/framebuffer metrics 与 content scale 变化只更新窗口事实，不重绑 Context。Headless
frame 在首次绑定前返回 `nullptr`。Context 在 Render → Task → Platform → Clock modules 前 shutdown，owner
shutdown 幂等且保持 owner-thread 契约。

owner 只负责 identity/lifetime，不调用 `commitLayout()`，因此 input route 绝不会隐式布局。正式帧顺序为
Platform lifecycle dispatch → owner selection → producer route(previous committed hit snapshot) →
ActionMapper。当前 producer 仍只有 Move/Button/Wheel raw ordinal consumption；b3e 后 primary Pointer Button
claim 已接入，后续 Button default action 已在同一 producer 阶段合并 consumption/claim；Key/Gamepad/axis
claim 仍后置。Game SDK 不获得裸 `UIContext*`，完整 Widget 仍需要后续 text/glyph、Keyboard/Gamepad
activation 与 theme/disabled 行为切片。

### M7-C1c-b3d1 公开容量配置与 Runtime-private layout coordinator

`EngineConfig::primaryWindowUICapacities` 是 `Tina::UI::UIContextCapacityConfig` 纯值。Runtime 不复制一套
平行校验：`EngineConfig::validate()` 在任何 factory 前调用 focused public validator，失败时包装为
`ConfigurationErrorCode::InvalidEngineConfig` 并保留 UI 领域原因；owner 随后只使用已经验证的值创建
primary-window Context。

`PrimaryWindowUILayoutCoordinator` 是 Runtime private，不进入 Game SDK 或普通 module public header。
正式帧在 `IGameState::updateUI()` 成功后、`IRenderDevice::submitFrame()` 前调用它；它只使用
`WindowMetricsSnapshot::logicalExtent` 形成 logical viewport。每个有效且严格递增的
`PlatformFrameId` 至多尝试一次：窗口与 Context 同时缺席的 Headless 帧成功 no-op；二者只缺一个、
window identity 不一致或 `commitLayout()` 失败都会阻断 Render，并保持本帧 attempt 已消费。
因此 callback 已产生的 retained mutation 不会通过同帧 retry 重放。输入 route 仍发生在本次 commit
之前，只读取上一份 committed hit snapshot。

该 b3d1 切片没有增加 root builder/updater、Widget、DisplayList 或可见 UI；b3d2 后来补了 scoped
root/updater，后续切片补了低层 SolidFill DisplayList bridge，D0 又补了 Runtime submit-call-local
DisplayList handoff，D1/D2 又补了私有 bgfx SolidQuad pass、Game SDK box-paint setter 与 Desktop
4-panel 可见样例；Button default action 又补了 primary Pointer 窄交互。完整 Widget 行为、文本/glyph 与 owning packet 仍后置。
普通游戏不会获得裸 `UIContext*`，也不能在任意阶段调用 `createRoot()`。

### M7-C1c-b3d2 启动与 Game SDK capability

[ADR 0021](adr/0021-runtime-ui-startup-capability.md) 冻结以下 API；当前 b3d2 已实现这些
root-scoped、phase-epoch-scoped facade，但它们只覆盖 retained tree mutation，不代表完整 Widget 或
可见 UI：

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
    [[nodiscard]] Core::Status setBoxPaint(
        UI::UINodeId node,
        const UI::UIBoxPaint& paint);
    [[nodiscard]] Core::Result<UI::UIRoutedPointerListenerToken>
    addRoutedPointerListener(
        UI::UIRoutedPointerListenerDesc descriptor,
        UI::UIRoutedPointerCallback callback);
    [[nodiscard]] Core::Status setButtonAction(
        UI::UINodeId button,
        UI::UIButtonActionCallback callback);
    [[nodiscard]] Core::Status clearButtonAction(UI::UINodeId button);
    [[nodiscard]] Core::Result<bool> isButtonPressed(UI::UINodeId button) const;
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

ADR 0011 已冻结并在后续切片实现的 Button Primary Pointer action 扩展如下。命名明确表达
Button 语义，不把它伪装成通用 Runtime/Game action，也不让游戏代码处理 backend Pointer 对象：

```cpp
enum class UIButtonActivationSource : u8 {
    PrimaryPointer,
};

struct UIButtonActionEvent final {
    UI::UINodeId buttonNode{};
    UIButtonActivationSource source = UIButtonActivationSource::PrimaryPointer;
    Platform::PlatformFrameId platformFrame{};
    u64 sourceSequence = 0; // 成功 activation 的 Primary Up transition
};

class UIButtonActionCallback final { // move-only, 48-byte fixed-inline, noexcept
public:
    void operator()(const UIButtonActionEvent& event) noexcept;
};

class PrimaryWindowUITreeUpdater final {
public:
    [[nodiscard]] Core::Status setButtonAction(
        UI::UINodeId button,
        UI::UIButtonActionCallback callback);
    [[nodiscard]] Core::Status clearButtonAction(UI::UINodeId button);
    [[nodiscard]] Core::Result<bool> isButtonPressed(UI::UINodeId button) const;
};
```

同名低层操作存在于 root-scoped `UI::UITreeUpdater`。Action 是节点属性，因此 set 原子替换，clear、
Button/root 销毁负责撤销，不返回会被临时析构的 token。`UIRoutedPointerListenerToken` 仍只管理低层
routed listener registration；两种生命周期不能混用。Button 默认 `Targetable`，但调用方仍可显式设置
`Ignore`。本切片只定义 `PrimaryPointerId + PointerButton::Primary` 的 Down/Move/Up 状态机；Keyboard、
Gamepad、Focus、Capture、文本与完整 Widget facade 不属于这组 API 的完成范围。

同一切片还向 `UIRoutedPointerEvent` 增加互补的
`preventDefaultAction() noexcept` 与 `isDefaultActionPrevented() const noexcept`。前者只阻止可取消的
Button arm/activation，不等于 stop、consume 或 claim；后者只观察本条 callback-scope route 的决定。

两种 facade 都是 move-only、owner-thread、phase-epoch-scoped。Runtime 在回调进入时生成 epoch，离开时
无条件失效；跨回调保存后操作返回 `RuntimeErrorCode::UIPhaseCapabilityExpired`。Headless 主动请求返回
`RuntimeErrorCode::PrimaryWindowUIUnavailable`。State 只持久保存 `UIRootOwner` 与 `UINodeId`；updater
始终验证 root generation、owner window 与 subtree containment。第一次 capability operation 失败会成为
该 phase 的 sticky error，后续 mutation 不再执行，Runtime 在 callback 结束统一合并错误；即使异常或
错误边界提前离开 phase，Runtime 也会用 no-throw abort guard 使已发出的 facade 失效。

后续 listener 扩展保持同一权限模型：注册动作只能通过 current-phase、root-scoped updater 执行；
`UIRoutedPointerListenerToken` 是唯一允许由 State 跨 phase 保存的 listener 对象。token 不保活
`UIContext`、`UIRootOwner` 或目标节点，因此 `onExit()` 固定先 reset listener token，再 reset root。
cross-root 注册错误进入既有 sticky first-error，且低层事务不消耗 listener slot/high-water。

Platform SPI 同时增加
`initialPrimaryWindowMetrics() -> Result<optional<WindowMetricsSnapshot>>`。它不能 poll、泵送事件、消费
frame id/source sequence；首个 frame 的 identity/revision 与 lifecycle event 必须和 seed 收敛。启动事务
在 `onEnter` 前绑定 Context，并在 State/policy commit 前按 logical extent 原子发布首份
structure/layout/hit/paint snapshot。

### M7-C1c-b3e held Pointer Button claim bridge

`UIRoutedPointerEvent::claimPointerButton(button)` 与 `consumeInputTransition()` 是两条独立权限：前者请求
当前 route 的 Window/Pointer 在本帧接管一个 continuous button control，后者只消费当前 raw transition。
合法重复请求幂等，非法 enum 返回 `false`；Move、Wheel 与 Button listener 都可以显式接管已按住的按钮，
因此 UI 不必等待下一次 ButtonDown 才能夺回正在被 Gameplay 使用的控制。

`UIInputRouteProducer` 只把最终 `PointerSnapshot::heldButtons` 仍为真的请求转换成
`PointerButtonControlIdentity`，跨 route 去重并写入 Create 期预分配的 published/staging PMR buffer。
容量失败时不发布 staging consumption/claims，但 attempted watermark 已推进，同一帧不可重试，避免重放
listener side effect。ActionMapper 先应用 claim 再映射 transition：已 active 的 Gameplay source 被取消并
抑制到真实 Up；同帧 ButtonDown 即使没有被 consume，只要被 claim 也不会激活 Gameplay。

该 b3e 切片只覆盖 primary Pointer Button claim。Key、Gamepad button/axis、持久 Pointer Capture、Focus/Modal
仍未实现；后续 Button default action 已覆盖 primary Pointer activation，但文本/Glyph 与完整 Widget 行为仍未实现。
D0 的 Runtime DisplayList handoff 只消费已有 paint snapshot，D2 的 `setBoxPaint()` 只提供 SolidFill authoring。

## EngineConfig

`EngineConfig` 是可复制纯值，Create 前一次性验证：

- 当前字段：UTF-8 `applicationName`、`primaryWindow`、`platformFrameCapacities`、
  `primaryWindowUICapacities`、`primaryWindowUIDisplayListCapacities`（`commandCapacity`、
  `clipCapacity`、`batchCapacity`）、`inputActions`、
  `platformEventSubscriptions`、fixed delta（默认1/60 s）、max fixed steps（固定上限4，配置默认4）、
  max accepted real delta、gameplay time scale 与 shutdown deadline；
- 后续字段：日志/资源根、CPU/IO worker、通用 Event queue、FrameArena、
  其余 UI/Asset/Audio/Render 预算、backend policy 与 Metrics/trace capture 策略。

默认值集中在 `EngineConfig::Defaults()`。当前已在创建任何模块前拒绝空/非法 UTF-8 应用名、
非法窗口配置、非法 Platform/Action/订阅容量、重复 physical binding、0/非有限时间值、
`maximumStepsPerFrame > 4`、非法 UI Context 容量、非法 primary-window UI DisplayList 三项容量、
非法 time scale 与 shutdown deadline；backend 组合验证随对应字段加入。
`Create` 通过 `const EngineConfig&` 接收输入，并在自身
`noexcept`/`try` 边界内复制所有权，避免调用前的字符串复制逃出异常边界。当前 Disabled
TaskSystem 没有等待过程，因此 shutdown deadline 只完成配置校验；有界 Worker 切片必须在它
真正驱动“请求停止 → 有界等待 → fatal-stop”后才能宣称 deadline 已实施。

## EngineCompositionFactories SPI

`EngineCompositionFactories` 只属于组合/测试 SPI，是一次性移动值，不是运行期 registry。它包含
MonotonicClock、TaskSystem 和 tagged `platformRender`。`platformRender` 在
`IndependentPlatformRenderFactories` 与 `WindowSurfacePlatformRenderFactories` 之间二选一，避免同一
次创建同时填写无 Surface Render 与 windowed Render slot。每个 type-erased factory 接受窄
CreateParams 并返回 `Result<unique_ptr<Interface>>`，成功即由 EngineHost 接管并登记逆操作。
缺少任一必要 factory 会在调用任何 factory 前失败。

- Runtime 不依赖具体 GLFW/bgfx/miniaudio factory；
- `tina_bootstrap_desktop` 的一个 composition translation unit 选择真实 adapter；
- Headless + DisabledTask + NullRender 与 GLFW + DisabledTask + NullRender 是当前两条显式组合，
  不是生产失败后的静默降级；Disabled UI/Audio 尚未实现；
- factory 不保存 CreateParams、不注册全局对象、不后台完成半创建；
- backend shutdown 幂等且由 EngineHost 唯一调用。

### M7-B Window Surface integration SPI

Windowed production 组合不能继续使用空 `RenderDeviceCreateParams`。M7-B1 已在不安装到
Game SDK 的 integration SPI 中实现：

- `EngineCompositionFactories::platformRender` 是二选一 tagged union：
  `IndependentPlatformRenderFactories{PlatformBackendFactory, RenderDeviceFactory}` 用于
  Headless+Null/M7-A GLFW+Null；
  `WindowSurfacePlatformRenderFactories{WindowSurfacePlatformBackendFactory,
  WindowSurfaceRenderDeviceFactory}` 用于 GLFW+surface-aware Render；
- `IWindowSurfacePlatformBackend` 同时实现 `IPlatformBackend` 和 primary-surface provider 契约，因此
  EngineHost 不做 RTTI/native escape；
- `acquirePrimaryWindowSurfaceLease()` 返回 move-only `NativeWindowSurfaceLease`；
- `primaryWindowSurfaceSnapshot()` 返回最近一次由 committed Platform metrics 派生的 surface snapshot，
  Runtime 转换为 `RenderSurfaceState` 后放入 `RenderDeviceCreateParams::initialPrimaryWindowSurface`
  与每帧 `RenderFrame::primaryWindowSurface`；
- `publishPrimaryWindow()` 只在 surface-aware RenderDevice 创建成功后执行，发布失败会触发逆序回滚；
- `WindowSurfaceRenderDeviceFactory` 在创建期接收 lease，成功后由具体 Render backend
  持有，失败时在 factory 内完整回滚；
- lease 只有 `tina_render_bgfx` 私有 decoder 可解析，没有 public native/`void*` getter；
- `WindowSurfaceId` 与 `WindowSurfaceSnapshot` 是 Platform/Runtime integration 的清晰名称；
  Render module 只接收转换后的 `RenderSurfaceState`；
- shutdown 顺序固定为停止新 packet → drain submission → RenderDevice/bgfx shutdown →
  lease release → GLFW window destroy → GLFW terminate。

Desktop bootstrap 只构造 tagged factory bundle，不在外部创建或持有 owner；EngineHost 统一执行
Clock → Platform → Task → lease（仅 WindowSurface）→ Render 的事务和逆序回滚。Null/Headless 与
M7-A GLFW+Null 组合不构造伪 lease，GLFW/bgfx 失败也不得静默降级 Null。M7-B1 覆盖 lease/
snapshot/deferred publish/runtime handoff；M7-B2 已建立私有 `tina_render_bgfx` device core，并由
`Tina::Desktop::CreateEngine(config)` 把该 factory 封装成产品组合入口，`tina_sample_desktop` 已通过
300帧真实 backend 冒烟。完整 factory
签名与 Surface state machine 见 [ADR 0020](adr/0020-window-surface-handoff.md)。

## `Scene::World`：M8-A 已实现的最小边界

`Tina::Scene` 是独立 C++23 module，不是 Legacy `src/ecs/World` facade。当前公开入口刻意保持小：

```cpp
auto world = Scene::World::Create(Scene::WorldConfig{.entityCapacity = 4096});
Core::Result<Scene::EntityId> createEntity(Scene::LocalTransform local = {});
Core::Status setParent(
    Scene::EntityId child,
    std::optional<Scene::EntityId> parent,
    Scene::ReparentMode mode = Scene::ReparentMode::KeepWorld);
Core::Status setLocalTransform(Scene::EntityId entity, Scene::LocalTransform local);
Core::Status updateWorldTransforms();
Core::Status destroyEntity(Scene::EntityId entity);
Core::Status destroySubtree(Scene::EntityId entity);
```

`World` 是 move-only owner，Create 时一次性为 entity slots、live registry、遍历/销毁 scratch 和 visited
bits 分配固定容量 PMR storage。所有 mutation、读查询和 transform publication 只能在 owner thread 执行；
move 构造把 owner 转移到目标线程。`setParent()` 默认 `KeepWorld`，也可显式 `KeepLocal`；父实体的
`destroyEntity()` 默认把直接子节点提升到 root 并保持 world，递归删除必须调用 `destroySubtree()`。
跨 World、stale generation、循环 reparent、非有限/溢出 Transform、不可由 TRS 表达的非均匀 scale+rotation
组合返回 Scene domain 的结构化错误。层级编辑立即改变 owner-thread hierarchy，`updateWorldTransforms()`
是显式的 phase-end publication barrier；失败不会发布部分 world snapshot。阶段 command buffer、generic
component query、EnTT storage、Scene-owned Camera/Sprite component 与 Runtime World capability 尚未进入本 API。公共头
不包含 EnTT、GLM、GLFW 或 bgfx。

## `RenderScene`：M8-B 已实现的提取边界

`Tina::Render` 现提供 `RenderSceneCapacity`、move-only `RenderSceneBuilder`、phase-local
`RenderSceneWriter`、`RenderSceneView`、`RenderCamera2DInput` 与 `RenderSprite2DInput`。Runtime 在每次
`IGameState::extractRenderScene()` 前开启 builder transaction，把 writer 放入 Context；回调失败、异常或
sticky writer error 会 rollback，只有 commit 成功的 view 才进入 `RenderFrame::primaryWorldScene`。

writer 当前只接受已经解析的纯 Tina 值；`stableCameraKey`、`stableEntityKey` 与临时 M8 `spriteKey` 必须非零。
Camera/Sprite 几何与 viewport 必须 finite，容量满或单帧多 Camera 返回 Render domain 的结构化错误。commit
执行透明/隐藏剪枝、旋转 Sprite 的保守 Camera culling、Camera/Sprite pixel snap 与
`sortingLayer -> orderInLayer -> stableEntityKey -> insertionOrder` 稳定顺序。`spriteKey` 会在 M10 由
`FrameResourceRef` 替换；当前没有 AssetHandle 解析、Mesh/Tile packet 或 bgfx Sprite pass。

`RenderSceneWriter` 及其引用只能在当前 extraction callback 内使用，禁止保存。`RenderSceneView` 借用 builder
固定 storage；下一次 `beginFrame()`、rollback、replacement commit、builder move/析构都会使旧 view 失效。
放入 `RenderFrame` 后的 view 只在当前 `IRenderDevice::submitFrame()` 调用内有效，backend 必须同步消费、复制
或编码，不得保留 view、span 或元素指针。

## Handle、借用与 API 可见性

下表同时列出 M6-A 已实现的最小接口和后续已冻结目标，不能把“目标”行理解成当前已有 API：

| 类型 | 状态 | API 层 | Owner/寿命 | 失败方式 |
| --- | --- | --- | --- | --- |
| `EngineHost` | M6-A 已实现 | Game SDK | main `unique_ptr`，直到 shutdown | Create/run `Result` |
| 最小 Phase Context | M6-A 已实现 | Game SDK | Runtime stack，当前 callback | callback `Status` |
| `WindowId` | M7-A generation 类型、Headless 契约与生产 GLFW registry 已实现 | Game SDK / Module SPI | GLFW backend-owned Window registry；首期一个 primary slot | invalid/stale/wrong owner/identity mismatch |
| `WindowSurfaceId` | M7-B1 已实现 | Runtime integration SPI | Platform surface registry generation | invalid/stale/wrong owner/revision |
| `Tina::UI::UINodeId` | M7-C1a 已实现，M7-C1b layout 继续复用 | UI public / Game SDK 目标 | Window-owned UI registry generation + WindowId owner | invalid/stale/wrong owner |
| `Tina::UI::UICommittedStructureView` | M7-C1a 已实现 | UI public | owner-thread borrowed structure snapshot，下一次结构发布（diagnostic `commitStructure()` 或原子 `commitLayout()`）或 `UIContext` 销毁后失效 | stale borrowed view / owner-thread misuse |
| `Tina::UI::UICommittedLayoutView` | M7-C1b 已实现 | UI public | owner-thread borrowed 双缓冲 layout snapshot，下一次成功发布新 layout 或 `UIContext` 销毁后失效；hit-only/no-op commit 不使其失效 | InvalidLayout/CapacityExceeded/owner-thread misuse |
| `Tina::UI::UICommittedHitView` | M7-C1c-a 已实现 committed 数据基础 | UI public | owner-thread borrowed 双缓冲 hit snapshot，下一次成功 hit 发布或 `UIContext` 销毁后失效 | CapacityExceeded/owner-thread misuse |
| `Tina::UI::UICommittedPaintView` | SolidFill paint 切片已实现 | UI public / integration input | owner-thread borrowed 双缓冲 paint snapshot，下一次成功 paint 发布或 `UIContext` 销毁后失效；no-op commit 不使其失效 | CapacityExceeded/invalid paint/owner-thread misuse |
| `Tina::UI::UIPointerHitQueryResult` | M7-C1c-b1 已实现 | UI public | owning value；entry index 只对结果 revision 对应的 committed hit view 有效 | 非有限坐标为正常 miss；不执行 listener/route |
| `Tina::UI::UIRoutedPointerListenerToken` | M7-C1c-b2 已实现 | UI public | generation-safe move-only RAII listener owner；owner-thread reset immediate，off-thread reset deferred，context 销毁后 reset 安全 | capacity exceeded / stale node / wrong owner |
| `Tina::UI::UIPointerRouteResult` | M7-C1c-b2 synthetic route；b3e 增加 claim request | UI public | owning value；描述单条 normalized pointer input 的 route/consume 结果与固定大小 Pointer Button claim request | invalid input / route reentrancy / capacity exceeded；不是 Runtime consumption/claim view，Runtime 仍须按最终 held snapshot 过滤 |
| `Tina::UI::UIContextCapacityConfig` | M7-C1c-b3d1 已实现 focused config/validator | UI public / EngineConfig value | Create 前复制纯值；Runtime Context owner 使用已验证配置 | invalid node/root/derived/listener capacity；factory 前拒绝 |
| `PrimaryWindowUIDisplayListCapacityConfig` | D0 已实现 | EngineConfig value / Runtime private | Create 前复制纯值；配置 fixed PMR builder 的 command/clip/batch 容量 | command=0、clip>command、batch=0或batch>command、超过最大值；factory 前拒绝 |
| `Tina::Scene::EntityId` | M8-A 已实现 | Scene public / 后续 Game SDK | `Scene::World` registry generation + owner；slot 复用递增 generation | InvalidEntity/StaleEntity/WrongWorld |
| `Tina::Scene::World` | M8-A 已实现 standalone owner | Scene public；尚未接入 Phase Context | move-only、owner-thread 读写、Create 时固定 entity/遍历/scratch storage；析构归还 supplied PMR | invalid capacity/owner thread/corrupt hierarchy |
| `LocalTransform` / `WorldTransform` | M8-A 已实现 | Scene public | World-owned POD；pointer query 只在 owner thread、下一次对应 mutation、entity destroy 或 World 析构前有效；world publication 由 `updateWorldTransforms()` barrier 完成 | non-finite/overflow/zero quaternion/unsupported TRS composition |
| `RenderSceneWriter` | M8-B 已实现 | Game SDK phase capability | 只在当前 `extractRenderScene()` callback 内有效；不可复制、不可保存 | invalid input/capacity/sticky transaction failure |
| `RenderSceneView` | M8-B 已实现 | Render SPI | borrowed fixed builder storage；下一次 build/rollback/replacement/move/destruction 后失效；`RenderFrame` 中只到当前 submit 返回 | invalid transaction；backend 不得保留 borrow |
| `AssetHandle<T>` | M10 目标 | Game SDK | 弱 slot lookup，不延长 payload | NotReady/stale/type mismatch |
| `AssetLease<T>` | M10 目标 | Game SDK 窄场景/Module SPI | 强引用，跨任务/帧显式持有 | acquire Result |
| Render typed handle | M7/M9 目标 | Engine Module SPI | RenderDevice registry 到 retire | invalid/stale/wrong device |
| 最小 `RenderFrame` | M6-A + D0 + M8-B 已实现 | Engine Module SPI | 当前 submit 调用；包含 submit-call-local borrowed `primaryWindowUIDisplayList` 与 `primaryWorldScene` | submit `Status`；backend 不得保留 borrowed view |
| `Render::UIDisplayListView` | SolidQuad DisplayList builder 已实现 | Engine Module SPI / integration output | borrowed 单缓冲 builder storage；下一次成功 `beginFrame()`、builder move 或析构后失效；已开启事务的 rollback 只清空该次候选 | CapacityExceeded/invalid input/transaction misuse |
| `RenderFramePacket` | M7-B/C 目标、Runtime Private | Runtime Private | Runtime 固定容量 pool；backend completion 前 owning | PoolExhausted/submit failure |

M6-A 已实现的通用 `GenerationPool` 使用强类型 Tag，并在所有构建中校验非零 registry owner token、slot index
和32位 generation；owner token 由当前单一 Core 链接镜像自动单调分配，registry 销毁后也不复用。
当前 `Scene::EntityId` 与 `UINodeId` 已复用该契约；后续 Render/Asset handle 也必须复用，而不是各自重新发明弱校验 ID。
若未来插件各自静态链接 Core，必须改为 EngineHost-owned/单一导出 allocator。`UINodeId` 还在 Game SDK
语义上显式编码并校验 owner `WindowId`。Debug 可再保存更宽的 Engine/registry cookie 立即诊断
跨 Host/World/Window/Device 使用，但不承担 Release 正确性。Game component 保存 AssetHandle 和
语义属性，不保存 Render typed handle。普通 Game SDK 不提供 raw parts factory；handle 只能由所属
registry 创建并返回。

## 第三方与 native 零泄漏

- Game SDK 与 Tina module public header 禁止 GLFW/bgfx/bx/bimg/FreeType/miniaudio/EnTT 类型；
  Engine Module SPI 可公开纯 Tina `RenderDevice`/descriptor，但 Game SDK/Phase Context 不可见；
- 游戏代码不获得 RenderDevice、native Window、GPU handle、ViewId 或 shader uniform；
- Platform/Render 的 `NativeWindowSurfaceLease` 位于内部 integration SPI，不安装到 Game SDK；
- 公共错误只返回 Tina category/code、可选 native integer code 和 UTF-8 context；
- public umbrella header 在没有第三方 include path 的外部 consumer 中独立编译。

渲染分层和自动化门禁见[渲染架构与 bgfx 边界](rendering.md)。

## 错误与异常

公共 API 使用 C++23 `std::expected` alias `Result/Status`；外部文件/config 错误使用稳定
`ErrorDomain + ErrorCode`、origin SourceLocation、可选 native integer code 与 UTF-8 context chain，
assert 不处理用户数据。所有 size/count/offset 在分配前检查溢出，所有异步 completion 提交前
重新校验 owner + generation。

C++ exception 保持开启以兼容标准库/第三方，但会在 Engine、`IGameApplication`、`IGameState`、Task
和 C callback 边界尽力转换为结构化错误。M6-A 的 `EngineHost::Create/run` 已标为 `noexcept`；若
系统已耗尽到连返回 `Error` 所需内存都无法分配，则按 fatal/terminate 处理，不能承诺继续构造
一个错误对象。析构、rollback、`onExit`、`onShutdown`、Audio callback 必须 `noexcept`；热点
正常路径不使用 throw 控制流程。

## 公共接口验收

M6-A/M7-A Headless 内核与首个 GLFW adapter 已验证：

- `IGameApplication` 没有逐帧虚函数，`IGameState` 是唯一帧行为接口；
- Core/Platform/Task/Render/Runtime 的 public header 逐头独立 include 和编译；
- `tina_sample_null` 只使用 Tina C++23 API 与显式 factory SPI，构建图不加入或链接
  GLFW/bgfx/EnTT/FreeType/miniaudio/SDL/SDL3；
- `createGlfwPlatformBackend` 的 public module header 只包含 Tina 类型；GLFW/native handle 保持在
  `tina_platform_glfw` PRIVATE 实现中，样例仍看不到 `GLFWwindow*`；
- Platform/Input public header 可独立编译，Runtime SPI owner 不通过 Game Context 暴露 dispatch/shutdown；
- `PrimaryPointerId`、Gamepad snapshot 同 owner/slot 唯一、跨帧 retained source 与最终 snapshot、
  Platform lifecycle dispatch/RAII subscription 行为使用直接 GoogleTest 覆盖；最终平台与数量结果只在
  [测试文档](testing.md) 维护。

后续验收：`tina_game_api_consumer`/Game SDK umbrella、正式 SDK/install package、完整
forbidden-token/dependency-closure、production Gamepad、完整 DPI/Windows IMM32，以及只使用
Game SDK 运行 UI/2D/3D 产品样例。`Desktop::CreateEngine`、bgfx smoke、root-scoped
Game SDK UI root/update facade、box-paint setter、私有 SolidQuad UI pass 与 Desktop 4-panel 可见样例已实现，
但不代表正式 SDK、完整 Render pass、Text/Glyph、Label 文本、Button Keyboard/Gamepad activation 或产品级 UI/2D/3D 样例已完成。
M7-C1b/C1c-a/C1c-b1/C1c-b2 已覆盖50,000节点
非递归 layout/hit snapshot、连续300次无变化 commit 的0 layout pass/0新增 UI PMR allocation，以及
15项 committed hit snapshot、5项 point query、19项 synthetic route 与3项 tree-updater 门禁；
截至 b3e，Windows MSVC 19.50 Debug/Release、Linux GCC 13.4 与 Clang 22.1.8 + libstdc++15.2
ASan/UBSan/LSan 的独立 `tina_ui_tests` 均已通过81/81，且 Clang 无 sanitizer 诊断；初次 GCC
暴露的 routed-pointer callback `requires` 名称可见性问题已修复，二次 GCC/Clang 构建无 warning。
前序 Windows MSVC 19.50 / CMake 4.2.3 Debug/Release 已通过 UI92/92（含11项 paint snapshot）、
基础207/207（含 Render DisplayList 与 `RenderFrame::primaryWindowUIDisplayList`）、Runtime→UI51/51、
独立 UI→Render bridge12/12，以及 Null 样例300帧。D1/D2 Windows Debug/Release 已通过
Runtime→UI53/53、bgfx16/16、bridge12/12；Desktop 可见 retained UI 样例 Debug 1200帧、
Release 300帧通过。Linux GCC 13.4 与 Linux Clang 22 sanitizer 仍保留
paint/DisplayList/bridge 门禁：基础205/205、UI92/92、Runtime→UI46/46、bridge12/12与Null样例300帧，
Clang 无 sanitizer 诊断；D1/D2 的 bgfx SolidQuad pass、Game SDK `setBoxPaint()` facade 与可见
Desktop 4-panel 样例尚未在 Linux 图重跑。
现有测试尚未覆盖完整 dirty-range pruning、Focus/Capture/Modal、Button Keyboard/Gamepad activation、
Disabled/theme 视觉、Image/Text/Glyph PaintCache、Runtime packet、nested clip、完整 Widget 默认行为或文本 Widget。M7-C1c-b3b 的 producer
只补齐 Move/Button/Wheel→consumption 私有桥，M7-C1c-b3c 只补齐 primary-window Context 生命周期与
EngineHost 顺序接线，M7-C1c-b3d1 只补齐容量配置与 Runtime-private layout commit；b3d2 只补齐
startup bind 与 root-scoped Game SDK UI facade；b3e 只补齐 held primary Pointer Button claim bridge。
Button default action 只补齐 primary Pointer 窄 pressed/activation 与 retained action property。
SolidFill paint、Render builder、integration bridge、D0 Runtime submit-call-local handoff、D1 bgfx pass 与
D2 visible panels 也不扩大到完整 Widget、owning packet、Text/Glyph 或产品 UI 结论。当前 M8-B Windows
Debug/Release 均通过基础211/211、Runtime→UI60/60、UI115/115、RenderScene11/11与两个300帧Null样例；
dirty-subtree b4a 的 UI 数量包含6项 reuse/回退测试。Linux GCC 与 Clang sanitizer 的 b3e 独立
Runtime→UI门禁仍为46/46。
