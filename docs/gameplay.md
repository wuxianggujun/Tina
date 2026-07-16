# 游戏程序入口与状态栈

> 状态：vNext 候选冻结。本文是游戏开发者理解 Tina 调用面的首要入口。

## 一句话结论

`IGameApplication` 只负责建立游戏级对象并创建恰好一个初始 `IGameState`；它没有逐帧回调。
`IGameState` 是 Menu、Settings、Game2D、Game3D、Pause 等状态唯一的帧行为入口。

前缀 `I` 只表示“这是抽象接口”，真正说明职责的是完整名词 `GameApplication` 和 `GameState`；
实现类必须按产品语义命名为 `TinaGameApplication`、`MainMenuState`、`Game2DState` 等，禁止再用
`GameImpl`、`Client` 或 `Manager` 让调用者猜用途。

这条边界避免 World、UI 和渲染逻辑既能写进“游戏程序入口”、又能写进状态对象的双重入口。
Tina 不再保留与 `GameStateStack` 并列的第二套 `SceneManager` 生命周期。

## 最小公共接口

```cpp
namespace Tina {

class IGameState;

class IGameApplication {
public:
    virtual ~IGameApplication() = default;

    // 只调用一次。State 的 onEnter、initialPolicy 采样和 initial UI snapshot
    // 全部成功后，游戏启动事务才算 commit。
    [[nodiscard]] virtual Core::Result<std::unique_ptr<IGameState>>
    createInitialState(GameStartupContext& context) = 0;

    // 仅在启动事务成功后调用，且恰好一次；不得创建新 Engine 工作。
    virtual void onShutdown(GameShutdownContext& context) noexcept = 0;
};

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

} // namespace Tina
```

公共 API 不再提供 `IFrameClient`。菜单等不需要 Fixed Update 的状态直接使用默认空实现，
不必为了接口完整性编写四个无意义函数。

## 两个接口分别负责什么

| 接口 | 应该负责 | 不应该负责 |
| --- | --- | --- |
| `IGameApplication` | 拥有纯游戏级 Settings/Save repository、构造初始 State、记录启动期回滚、最终停止 | World、UI root、逐帧输入/更新/渲染、查询 RenderDevice、充当 Service Locator |
| `IGameState` | 拥有可选 World、UI roots、状态内模型、订阅和 TaskGroup；处理阶段回调 | 拥有 Window/UIContext/AssetSystem/RenderDevice、保存 Phase Context、直接提交 bgfx |
| `EngineHost` | 模块所有权、Frame Pipeline、GameStateStack、错误收敛和关闭顺序 | 游戏玩法、菜单流程、全局静态访问 |

`IGameApplication` 可以拥有不依赖 Engine 生命周期的产品对象，并通过构造函数把窄接口传给 State，
例如 `SettingsRepository&`、`SaveRepository&` 和 State factory。它不得保存 `AssetLease`、
`UINodeId`、`EntityId`、Render handle 或 Phase Context；这些对象必须由已提交 State 的 RAII
成员持有，并在 State exit transaction/析构中释放。

## 调用时序

```text
EngineHost::run(gameApplication)
  -> begin Game Start transaction
  -> gameApplication.createInitialState(startupContext)
  -> initialState.onEnter(stateEnterContext)
  -> sample initialState.initialPolicy()
  -> initial UI model/layout/snapshot
  -> commit initial State / UI roots / input context / policy
  -> frame loop
       UI input scope + one route   top -> bottom eligibility
       Fixed Update                 top -> bottom
       Frame Update (variable dt)   top -> bottom
       State Transition Commit      queued commands only
       Render Scene Extraction      bottom -> top
       UI model/layout/display      bottom -> top
       Deferred Cleanup             resource retirement only
  -> stop accepting new state commands
  -> remove state eligibility / close ingress
  -> requestStop + TaskGroup barrier/join
  -> state.onExit()                 top -> bottom, workers already joined
  -> gameApplication.onShutdown()   exactly once
  -> Engine modules reverse shutdown
```

- `createInitialState`、初始 `onEnter` 或 initial UI snapshot 失败：启动事务完整回滚，不调用
  candidate `onExit`，也不调用 Application `onShutdown`；
- 启动提交后即使帧回调失败，也先按栈顶到栈底执行 `onExit`，再调用一次 `onShutdown`；
- pop 最后一个 State 表示正常结束游戏；Headless 样例也使用显式 `EmptyState`；
- State policy 和结构变化只在 Frame Update 后的 State Transition Commit 提交；新状态在当帧
  Render/UI 阶段完成首份 snapshot，从下一帧开始接收输入。

## Phase Context 是能力视图

| Context | State 可做的事 | 明确禁止 |
| --- | --- | --- |
| `GameStartupContext` | 查询只读启动配置、登记游戏级回滚 | 创建 World/UI root、保存 Context、取得 Engine module |
| `GameStateEnterContext` | 创建 World、`UIRootBuilder`、输入上下文、订阅和 State TaskGroup | commit 前发布 Task completion、激活半构造 State、修改旧 State、取得 RenderDevice |
| `FixedUpdateContext` | 读取目标 tick 的 Simulation Action，提交 World command | UI/Window 操作、Frame Action、结构立即提交 |
| `FrameUpdateContext` | 读取 Frame Action/Asset snapshot，提交 State command | 阻塞 IO、直接 push/pop、读取隐式 Simulation edge |
| `RenderSceneExtractionContext` | 只读 World，向 `RenderSceneWriter` 写 Tina 描述 | 修改 World、创建 GPU 资源、保存 writer/span |
| `UIUpdateContext` | 通过绑定 root 的 `UITreeUpdater` 更新 retained model/style/action/dirty | 创建新 root、每帧重建 UIContext、直接生成 backend draw、访问 bgfx |
| `GameStateExitContext` | TaskGroup 已 join 后读取退出原因并释放 State 自己的 RAII owner | 发起新 Task/Asset/State 请求、直接清 Runtime registry |
| `GameShutdownContext` | 读取退出原因和最终诊断、释放游戏级注册 | 创建新 Window/Asset/Task |

Phase Context 不提供通用 `services()` 或 `get<T>()`。需要新增能力时，必须先确定它属于哪个
阶段，再增加窄 accessor；不能把 Service Locator 换一个名字重新引入。

## State 结构变化

`FrameUpdateContext::gameStateCommands()` 提供唯一的 `requestPush`、`requestPopSelf`、
`requestReplaceSelf` 和 `requestPolicyChange` 入口。它绑定当前 State，不公开可变状态栈：只有
当前栈顶能提交 command，首期 structural 与 policy change 合计每帧最多一个；policy change
只以自己为目标。请求返回
`GameStateCommandId`，只代表进入固定容量队列，不代表 candidate `onEnter` 已成功。

Runtime 在 Frame Update 返回后按 sequence 提交。push/replace 的 candidate `unique_ptr` 在请求
被接受时移交 Runtime；enter 期间 Task completion 在 commit 前隔离，失败时 close ingress、取消并
join TaskGroup、逆序回滚 owner 后直接析构，不调用 `onExit`。旧栈保持不变。请求同时预留有界
completion slot；结果以 callback-lifetime `GameStateCommandCompletion` span 在来源 State 下一次
实际执行 `updateFrame()` 时交付。队列/结果槽满、非栈顶、重复请求和 Runtime Stopping 分别返回
明确错误；pop 最后一个 State 表示正常结束游戏。完整草案见[公共接口](public-api.md)。

UI action 在输入路由期间只更新当前 State 拥有的 intent/model；同帧 `updateFrame()` 再把 intent
转换为 State command。这样按钮回调不会在 Capture/Target/Bubble 栈中销毁当前 State。

```text
Button Click
  -> MainMenuState::intent = StartGame
  -> MainMenuState::updateFrame()
  -> gameStateCommands.requestReplaceSelf(make_unique<Game2DState>(gameServices))
  -> State Transition Commit
  -> Game2DState::onEnter()
  -> current frame Render/UI snapshot
  -> next frame input becomes active
```

`push/replace` 先在旧栈仍完整时执行新 State 的 enter transaction；失败时撤销新 State并保留
旧栈。成功替换/弹出已提交 State 的退出顺序固定为：

1. 从后续 phase/UI eligibility 移除，关闭 command/task/event ingress，并清理 Focus/Capture/Modal；
2. signal State TaskGroup cancellation，Runtime barrier/join 并丢弃迟到 completion；
3. 调用一次 `onExit`，State 可释放自己的 root、lease、订阅和其他 RAII owner；
4. 析构 State，让残余 owner 幂等释放，再断言 registry/TaskGroup 无残留。

`onExit` 必须 `noexcept`；Runtime 在它之前停止并 join Worker，但不提前销毁 State owner，也不
要求它直接操作内部 registry。上一帧已提交的 GPU/Atlas 资源由独立 `RenderFramePacket` 保活，
不依赖 State 继续存活。

## 常见 GameStatePolicy

| State | Gameplay input | UI input | Fixed | Frame update | Render |
| --- | --- | --- | --- | --- | --- |
| `MainMenuState` | 阻断下层 | 阻断下层 | 阻断下层 | 阻断下层 | 阻断下层 |
| `Game2DState` / `Game3DState` | 不阻断 | 不阻断 | 不阻断 | 不阻断 | 不阻断 |
| `PauseState` | 阻断下层 | 阻断下层 | 阻断下层 | 阻断下层 | 不阻断，继续显示世界 |
| `SettingsState` overlay | 阻断下层 | 阻断下层 | 通常阻断 | 通常阻断 | 不阻断，保留背景 |
| `LoadingState` overlay | 阻断下层 | 阻断下层 | 依产品决定 | 通常不阻断下层 | 依产品决定 |

`blocksGameplayInputBelow=true` 但不阻断 Frame Update 时，下层仍收到 Frame Update 回调，但 Action Snapshot
为空。首期一个 `blocksRenderBelow` 同时决定下层 World 与 UI root 是否可见，避免提前增加两套
渲染传播标志。

## 最小游戏示例

```cpp
class TinaGameApplication final : public Tina::IGameApplication {
public:
    TinaGameApplication(SettingsRepository& settings, SaveRepository& saves)
        : settings_(settings), saves_(saves) {}

    Core::Result<std::unique_ptr<Tina::IGameState>>
    createInitialState(Tina::GameStartupContext&) override {
        return std::make_unique<MainMenuState>(settings_, saves_);
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override {}

private:
    SettingsRepository& settings_;
    SaveRepository& saves_;
};

int main() {
    auto host = Tina::Desktop::CreateEngine(Tina::EngineConfig::Defaults());
    if (!host) {
        return reportStartupError(host.error());
    }

    SettingsRepository settings;
    SaveRepository saves;
    TinaGameApplication gameApplication{settings, saves};
    auto result = host.value()->run(gameApplication);
    return result ? EXIT_SUCCESS : reportRunError(result.error());
}
```

`Tina::Desktop::CreateEngine` 的实现组合 GLFW、bgfx、FreeType 和 miniaudio，但它的 header 只
包含 Tina 类型。普通游戏入口不 include 或链接具体 backend target；自定义 backend 和失败
注入测试才使用低一层 `EngineHost::Create(config, factories)`。

## 2D、3D 与 UI 的落点

- `Game2DState` 持有 2D World、TileMap gameplay object、Camera2D、UI roots 和输入 intent；
- `Game3DState` 持有 3D World、Camera3D、Prefab instances 和 UI roots；
- `PauseState`、`SettingsState` 是 overlay，不复制底层 World；
- UI root 只在 `onEnter/onExit` 创建或销毁，每帧 `updateUI` 只改 retained 数据；
- World 只在 Fixed/command commit 修改，Render Scene Extraction 只读取并写后端无关描述。

具体契约见 [2D 游戏架构](game-2d.md)、[3D 游戏架构](game-3d.md)、
[自研 UI](ui.md)和[渲染架构](rendering.md)。

## 验收

- compile-only 示例能只包含 Game SDK headers，不需要 bgfx/GLFW/EnTT include path；
- `IGameApplication` 类型没有 fixed/update/render/UI 虚函数；
- `createInitialState`、初始 enter、启动回滚和 `onShutdown` 次数均有失败注入测试；
- Menu -> Game2D -> Pause -> Settings -> Resume 的回调和 policy 顺序固定；
- State action 在 routed UI 回调中只能排队，不能立即销毁当前 State；
- State 退出后 World、UI roots、TaskGroup、Asset lease 和订阅全部归零。
