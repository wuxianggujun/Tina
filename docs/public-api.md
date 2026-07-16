# vNext 公共接口与生命周期规则

> 状态：候选冻结并分批实施。Core 公共基础已用 public-header consumer 固化；其余接口按垂直切片落地。

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
| Engine Module SPI | Tina 模块、backend adapter 和测试 | EngineFactories、RenderDevice typed handle/descriptor、RenderFrame、FramePinSink、Pass Scheduler |
| Backend Private | 具体 adapter | GLFW、bgfx/bx/bimg、FreeType、miniaudio、EnTT 等第三方类型 |

Game SDK 不提供 RenderDevice、GPU resource handle、native window/surface 或第三方 factory。
`tina_bootstrap_desktop` 提供只使用 Tina 类型的 `Desktop::CreateEngine(config)`，普通游戏无需
知道生产组合实际使用 GLFW/bgfx/FreeType/miniaudio。

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

```cpp
class EngineHost final {
public:
    Core::Result<RunExitReason> run(IGameApplication& gameApplication);
};

namespace Desktop {

[[nodiscard]] Core::Result<std::unique_ptr<EngineHost>>
CreateEngine(EngineConfig config);

} // namespace Desktop
```

高级集成和测试 SPI：

```cpp
class EngineHost final {
public:
    [[nodiscard]] static Core::Result<std::unique_ptr<EngineHost>> Create(
        EngineConfig config,
        EngineFactories factories);
};
```

`Desktop::CreateEngine` 只是组合 helper，不是 Singleton。返回的 EngineHost 仍由 main 的
`unique_ptr` 唯一拥有，不能从游戏代码全局查询。

## IGameApplication：游戏程序入口

名称明确表达“整个游戏程序的生命周期入口”；它不是 World、Scene 或逐帧玩法接口。

```cpp
class IGameApplication {
public:
    virtual ~IGameApplication() = default;

    [[nodiscard]] virtual Core::Result<std::unique_ptr<IGameState>>
    createInitialState(GameStartupContext& context) = 0;

    virtual void onShutdown(GameShutdownContext& context) noexcept = 0;
};
```

- `createInitialState` 每次 `run()` 只调用一次；返回恰好一个 initial State；
- 初始 State enter、initial policy 采样和首份 UI layout/snapshot 全部成功后启动事务才 commit；
  之前失败自动回滚，不调用 candidate `onExit` 或 Application `onShutdown`；
- commit 后无论正常退出还是帧错误，所有 State 退出后调用一次 `onShutdown`；
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
    virtual ~IGameState() = default;

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

Gameplay Input、UI Input、Fixed Update、Frame Update 从栈顶向下按各自 committed policy 传播，
Render Scene Extraction/UI visible roots 从最底可见层向上。`initialPolicy()` 只在成功 `onEnter`
后采样一次；Runtime 持有唯一 committed policy，后续只能通过状态命令修改，不能靠成员变量
悄悄改变当前帧传播。

`push/pop/replace/policy-change` 只通过 `FrameUpdateContext::gameStateCommands()` 排队，在 Frame
Update 结束后的 State Transition Commit 提交。首期 `blocksRenderBelow` 同时决定下层 World 和
UI root 可见性。

## GameStateCommands：唯一状态变化入口

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
    Duration updateDelta;
    Duration fixedDelta;
    double interpolation;
    std::uint64_t frameIndex;
    std::uint64_t simulationTick;
    std::uint32_t fixedStepIndexInFrame;
};
```

`realDelta` 未缩放但经过有限性检查；`FixedStepAccumulator` 先把它钳制为
`acceptedRealDelta`，再应用 gameplay time scale 得到 `updateDelta`。超出真实 delta 上限的部分
记为 `rejectedRealDelta`；最多4步之后仍存在的完整 Simulation 步记为
`discardedSimulationDelta`，只保留小于一步的余量计算 interpolation。time scale 只影响玩法，
不影响 Platform/UI/Asset/Audio/diagnostics wall timeout。

`FixedUpdateContext` 只提供目标 tick 的 `SimulationActionSnapshot`；`FrameUpdateContext` 只提供当前
帧 `FrameActionSnapshot`。0 fixed-step 帧保留 Simulation edge，最多4步也只消费一次；同一隐式
Pressed 不会在两个域重复执行。

## EngineConfig

`EngineConfig` 是可复制纯值，Create 前一次性验证：

- product name、UTF-8 日志/资源根、primary window logical size/title/fullscreen/vsync；
- fixed delta（默认1/60 s）、max fixed steps（默认4）、max accepted real delta；
- CPU/IO worker、Task/Event/Input queue、shutdown deadline；
- FrameArena、UI、Asset completion/upload、Audio command 和 Render resource 预算；
- Headless/production policy、Metrics/trace capture 策略。

默认值集中在 `EngineConfig::Defaults()`。0/非有限 delta、尺寸/容量乘法溢出、冲突 backend
组合必须在创建模块前返回 `InvalidConfig`。

## EngineFactories SPI

`EngineFactories` 只属于组合/测试 SPI，是一次性移动值，不是运行期 registry。每个 type-erased
factory 接受窄 CreateParams 并返回 `Result<unique_ptr<Interface>>`；成功即由 EngineHost 接管并
登记逆操作。

- Runtime 不依赖具体 GLFW/bgfx/miniaudio factory；
- `tina_bootstrap_desktop` 的一个 composition translation unit 选择真实 adapter；
- Headless + NullRender + DisabledUI/Audio 是显式组合，不是生产失败后的静默降级；
- factory 不保存 CreateParams、不注册全局对象、不后台完成半创建；
- backend shutdown 幂等且由 EngineHost 唯一调用。

## Handle、借用与 API 可见性

| 类型 | API 层 | Owner/寿命 | 失败方式 |
| --- | --- | --- | --- |
| `EngineHost` | Game SDK | main `unique_ptr`，直到 shutdown | Create/run `Result` |
| Phase Context/Writer | Game SDK | Runtime stack，当前 callback | Status/sticky error |
| `EntityId/UINodeId/WindowId` | Game SDK | 对应 registry generation + owner | invalid/stale/wrong owner |
| `AssetHandle<T>` | Game SDK | 弱 slot lookup，不延长 payload | NotReady/stale/type mismatch |
| `AssetLease<T>` | Game SDK 窄场景/Module SPI | 强引用，跨任务/帧显式持有 | acquire Result |
| Render typed handle | Engine Module SPI | RenderDevice registry 到 retire | invalid/stale/wrong device |
| `UIDisplayListView/RenderFrame` | Engine Module SPI | 所属 Runtime-private RenderFramePacket 生命周期 | CapacityExceeded/invalid ref |
| `RenderFramePacket` | Runtime Private | Runtime 固定容量 pool；backend completion 前 owning | PoolExhausted/submit failure |

所有 generation handle 使用强类型 Tag。`UINodeId` 在所有构建中编码并校验 owner `WindowId`；
其他 handle 通过所属 capability/registry 校验，Debug 再保存 Engine/registry cookie 立即诊断
跨 World/Window/Device 使用。Game component 保存 AssetHandle 和语义属性，不保存 Render typed handle。

## 第三方与 native 零泄漏

- Game SDK 与 Tina module public header 禁止 GLFW/bgfx/bx/bimg/FreeType/miniaudio/EnTT 类型；
- 游戏代码不获得 RenderDevice、native Window、GPU handle、ViewId 或 shader uniform；
- Platform/Render 的 NativeSurfaceLease 位于内部 integration SPI，不安装到 Game SDK；
- 公共错误只返回 Tina category/code、可选 native integer code 和 UTF-8 context；
- public umbrella header 在没有第三方 include path 的外部 consumer 中独立编译。

渲染分层和自动化门禁见[渲染架构与 bgfx 边界](rendering.md)。

## 错误与异常

公共 API 使用 C++23 `std::expected` alias `Result/Status`；外部文件/config 错误使用稳定
`ErrorDomain + ErrorCode`、origin SourceLocation、可选 native integer code 与 UTF-8 context chain，
assert 不处理用户数据。所有 size/count/offset 在分配前检查溢出，所有异步 completion 提交前
重新校验 owner + generation。

C++ exception 保持开启以兼容标准库/第三方，但会在 Engine、`IGameApplication`、`IGameState`、Task
和 C callback 边界转换为结构化错误。析构、rollback、`onExit`、`onShutdown`、Audio callback
必须 `noexcept`；热点正常路径不使用 throw 控制流程。

## 公共接口验收

- `tina_game_api_consumer` 不添加第三方 include path即可包含全部 Game SDK header；
- `IGameApplication` 没有逐帧虚函数，`IGameState` 是唯一帧行为接口；
- 每个 public header 单独 include 和编译，不依赖 umbrella 偶然顺序；
- public include/install tree 通过 forbidden-token 和 dependency-closure 检查；
- 示例只使用 `Desktop::CreateEngine + IGameApplication + IGameState` 即可运行 UI/2D/3D；
- Null sample 完全不 add_subdirectory/link/load GLFW/bgfx/FreeType/miniaudio。
