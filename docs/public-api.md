# vNext 公共接口与生命周期规则

> 状态：分批实施。Core、M6-A 生命周期、M7-A Platform/Input 与首个私有 GLFW Window/Keyboard/
> Pointer/committed text adapter 已落地；完整状态栈、worker、Scene/Asset/UI/Audio、Desktop bootstrap、
> M7-B1 Native Surface handoff 已落地；真实 bgfx device、production Gamepad、完整 DPI 与
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

当前 Game SDK 只实现 `EngineHost`、`IGameApplication`、单个 `IGameState`、`GameStatePolicy`、
Platform 生命周期订阅、Action Snapshot 与最小 Phase Context；表中 World/Asset/UI、`GameStateCommands`、typed render
handle/descriptor 和 Pass Scheduler 均未实现。

完整 Game SDK 不提供 RenderDevice、GPU resource handle、native window/surface 或第三方 factory。
后续 `tina_bootstrap_desktop` 将提供只使用 Tina 类型的 `Desktop::CreateEngine(config)`，让普通
游戏无需知道生产组合实际使用 GLFW/bgfx/FreeType/miniaudio。

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
`WindowSurfacePlatformRenderFactories` 支持 lease/snapshot/deferred publish handoff，但真实 bgfx
device 尚未实现。`EngineHost` 由 main 的 `unique_ptr` 唯一拥有。

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

`Desktop::CreateEngine` 尚未实现；它会是组合 helper，不是 Singleton，也不能从游戏代码全局查询。
当前 vNext target 只承诺 build-tree consumer；正式 `Tina::GameSDK`、`install(EXPORT ...)`、版本化
package config 与外部 SDK consumer 门禁会在 Desktop Bootstrap 切片统一加入，当前不伪装成可安装 SDK。

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
  回滚，不调用 candidate `onExit` 或 Application `onShutdown`；首份 UI layout/snapshot 将在 UI
  切片加入后纳入同一启动事务；
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
| `RenderSceneExtractionContext` / `UIUpdateContext` | 只读 `FrameTiming` |
| `GameStateExitContext` / `GameShutdownContext` | `RunStopCause` 与可选 Runtime failure |

`runtimeFailure()` 返回 callback-only 的只读借用，只保证在当前 `onExit`/`onShutdown` 调用期间
有效；回调可以复制稳定 code/message 供诊断，但不得保存 Error 指针或 Context。

当前 Context 不提供 raw Platform Input、World、Asset、Task、UI writer 或 Render writer。完整垂直切片将按能力
逐项扩展，下表是目标边界，不是当前 API：

| Context | 可访问能力 | 明确禁止 |
| --- | --- | --- |
| `GameStartupContext` | 只读 config/capability、游戏级启动回滚 | World/UI root、RenderDevice、保存 Context |
| `GameStateEnterContext` | World、`UIRootBuilder`、Input Context、订阅、TaskGroup 的事务创建 | commit 前发布 Task completion、直接激活半成品、修改旧栈、backend 类型 |
| `FixedUpdateContext` | fixed timing、Simulation Action、World query/command、当前 TaskGroup | Frame/UI Action、Window/RenderDevice、保存 Arena span |
| `FrameUpdateContext` | 每 Render Frame 一次的 real/update delta、Frame Action、Asset snapshot、GameStateCommands | Simulation edge、直接 commit 状态、阻塞 IO |
| `RenderSceneExtractionContext` | interpolation、只读 World、`RenderSceneWriter` | 修改 World、创建 GPU 资源、访问 bgfx |
| `UIUpdateContext` | 绑定已拥有 root 的 `UITreeUpdater`、retained model/style/action/dirty | 创建新 root、每帧重建 UIContext、直接提交 Render/backend |
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

M7-A 已实现下列职责完整的 Platform/Input 名称；UI consumption/claim 数据结构属于后续 UI 接入点：

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
| `InputTransitionConsumption` | Runtime/UI integration | 只标记本帧哪些 sequence 已由 UI 消费 |
| `ContinuousControlClaims` | Runtime/UI integration | 描述当前 UI route 的 digital/axis/pointer ownership seam；M7-A mapper 只消费 digital claim，axis/pointer continuous mapping 后续实现 |
| `InputActionId` | Game SDK | 显式的语义 Action 标识，不暴露物理键值为 gameplay contract |
| `SimulationActionSnapshot` | Game SDK | 当前 simulation tick 的 state/axis + 有序 edge batch |
| `FrameActionSnapshot` | Game SDK | 仅当前 Render Frame 可用一次的 camera/presentation/UI 外围 Action |

`PlatformFrameView` 及其 span 只在当前 Poll/Input phase 有效，不能保存；backend storage 即使到
下一次 Poll 才复用，也不延长 API 借用寿命。Runtime 先让 UI 产生
transition consumption 和 continuous claims，再映射 Gameplay Action。M7-A 被消费/claim 的 digital
Down 建立 `suppressedUntilRelease`，匹配 Up 只解除 suppression，不补发 Gameplay edge；axis/pointer
identity 目前只是已冻结 seam schema，等后续 analog binding 才实现 dead-zone/neutral suppression。
Focus lost、设备断开和 `InputStreamReset` 取消 transient edge、repeat、Capture 与 composition，
但不把跨帧状态粗暴清零：M7-A Action Mapper 对 resync 后仍 held 的 digital control 保留
`suppressedUntilRelease`；Simulation latch 插入 reset marker 并保留最终 action state。它们
都不伪造会激活按钮的普通 Up。

M7-A 的 Pointer snapshot、Pointer Button/Move/Wheel transition 与 pointer binding 只接受
`PrimaryPointerId`（0），多 Pointer 是后续契约。`PlatformFrameBuilder` 还要求同一帧所有
`GamepadSnapshot` 使用同一 `GamepadId` owner，且一个 slot 只能出现一个 generation；Connect/Disconnect
必须与最终 snapshot 及同 Poll 的 cancel/reset 证据一致。

Action Mapper 在处理本帧 transition 后，对跨帧保留的 `active`/suppressed physical source 再读取最终
snapshot：仍保留的 Key、Primary Pointer Button 或 Gamepad Button 必须仍为 held。Primary Window 或
Gamepad generation 在 retained state 尚未取消时消失/替换属于 `LifecycleInvariantViolation`，不能把旧
source 静默迁移到新 owner/generation。

M7-A 的 Action Mapper 由 Runtime 拥有；`EngineConfig::inputActions.digitalBindings` 是唯一注册入口，
首批只有 priority=0 的 Engine default Input Context。raw/text/platform-event 容量由
`platformFrameCapacities` 唯一配置；Simulation/Frame action/binding 容量由
`inputActions.capacities` 配置；订阅 slot 由 `platformEventSubscriptions` 配置。M7-A 尚无 UI producer，
内部 continuous claim 上限固定为64且不属于 game-facing Action Map 配置。批次另有不可被普通项
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

## EngineConfig

`EngineConfig` 是可复制纯值，Create 前一次性验证：

- 当前字段：UTF-8 `applicationName`、`primaryWindow`、`platformFrameCapacities`、`inputActions`、
  `platformEventSubscriptions`、fixed delta（默认1/60 s）、max fixed steps（固定上限4，配置默认4）、
  max accepted real delta、gameplay time scale 与 shutdown deadline；
- 后续字段：日志/资源根、CPU/IO worker、通用 Event queue、FrameArena、
  UI/Asset/Audio/Render 预算、backend policy 与 Metrics/trace capture 策略。

默认值集中在 `EngineConfig::Defaults()`。当前已在创建任何模块前拒绝空/非法 UTF-8 应用名、
非法窗口配置、非法 Platform/Action/订阅容量、重复 physical binding、0/非有限时间值、
`maximumStepsPerFrame > 4`、非法 time scale 与 shutdown deadline；backend 组合验证随对应字段加入。
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
snapshot/deferred publish/runtime handoff；M7-B2 已建立私有 `tina_render_bgfx` device core，Desktop
bootstrap 仍待把该 factory 封装成产品入口。完整 factory
签名与 Surface state machine 见 [ADR 0020](adr/0020-window-surface-handoff.md)。

## Handle、借用与 API 可见性

下表同时列出 M6-A 已实现的最小接口和后续已冻结目标，不能把“目标”行理解成当前已有 API：

| 类型 | 状态 | API 层 | Owner/寿命 | 失败方式 |
| --- | --- | --- | --- | --- |
| `EngineHost` | M6-A 已实现 | Game SDK | main `unique_ptr`，直到 shutdown | Create/run `Result` |
| 最小 Phase Context | M6-A 已实现 | Game SDK | Runtime stack，当前 callback | callback `Status` |
| `WindowId` | M7-A generation 类型、Headless 契约与生产 GLFW registry 已实现 | Game SDK / Module SPI | GLFW backend-owned Window registry；首期一个 primary slot | invalid/stale/wrong owner/identity mismatch |
| `WindowSurfaceId` | M7-B1 已实现 | Runtime integration SPI | Platform surface registry generation | invalid/stale/wrong owner/revision |
| `UINodeId` | M7-C 目标 | Game SDK | Window-owned UI registry generation + WindowId owner | invalid/stale/wrong owner |
| `EntityId` | M8 目标 | Game SDK | World registry generation + owner | invalid/stale/wrong owner |
| `AssetHandle<T>` | M10 目标 | Game SDK | 弱 slot lookup，不延长 payload | NotReady/stale/type mismatch |
| `AssetLease<T>` | M10 目标 | Game SDK 窄场景/Module SPI | 强引用，跨任务/帧显式持有 | acquire Result |
| Render typed handle | M7/M9 目标 | Engine Module SPI | RenderDevice registry 到 retire | invalid/stale/wrong device |
| 最小 `RenderFrame` | M6-A 已实现 | Engine Module SPI | 当前 submit 调用 | submit `Status` |
| `UIDisplayListView` | M7-C 目标 | Engine Module SPI | 所属 Runtime-private RenderFramePacket 生命周期 | CapacityExceeded/invalid ref |
| `RenderFramePacket` | M7-B/C 目标、Runtime Private | Runtime Private | Runtime 固定容量 pool；backend completion 前 owning | PoolExhausted/submit failure |

M6-A 已实现的通用 `GenerationPool` 使用强类型 Tag，并在所有构建中校验非零 registry owner token、slot index
和32位 generation；owner token 由当前单一 Core 链接镜像自动单调分配，registry 销毁后也不复用。
后续 Entity/UI/Render/Asset handle 必须复用该契约，而不是各自重新发明弱校验 ID。
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

后续验收：`tina_game_api_consumer`/Game SDK umbrella、完整 forbidden-token/dependency-closure、
`Desktop::CreateEngine`、真实 bgfx device、production Gamepad、完整 DPI/Windows IMM32，以及只使用
Game SDK 运行 UI/2D/3D 产品样例。
