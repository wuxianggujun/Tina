# vNext 公共接口与生命周期规则

> 状态：候选冻结。本文定义语义，不要求最终类名逐字不变。

## ABI 范围

vNext 公共接口是同一仓库、同一工具链构建下的 C++ source API，不承诺跨编译器/CRT 的稳定
插件 ABI。`std::span/string_view/pmr` 可以在 Tina targets 间使用，但不得据此宣称能加载任意
第三方二进制插件；未来插件/脚本边界需要独立 C ABI 或序列化协议 ADR。

## 通用规则

| 类型 | Owner | 借用寿命 | 线程 | 失败方式 |
| --- | --- | --- | --- | --- |
| `EngineHost` | bootstrap `unique_ptr` | 直到 shutdown | 主线程 | Create/run `Result` |
| Phase Context | Runtime stack | 当前 callback | 声明的 phase/线程 | Status/sticky builder error |
| `EntityId/NodeId/WindowId` | 对应 registry | generation 有效期 | owner 线程查询 | invalid/stale |
| `AssetHandle<T>` | 值类型、弱 lookup | slot generation | 查询按 API 约束 | NotReady/stale/type mismatch |
| `AssetLease<T>` | RAII 强引用 | lease 生命周期 | copy/release 线程安全边界明确 | acquire Result |
| Render handle | RenderDevice registry | destroy/retire 前 | 主线程使用 | invalid owner/generation |
| Frame builder/span | FrameArena | 当前 phase/barrier 前 | 当前线程/TaskGroup | CapacityExceeded |
| `SubscriptionToken` | subscriber RAII | dispatcher 或 token 先销毁 | 主线程取消 | 幂等失效 |
| `AudioVoiceId` | Audio registry | callback completion 前 | 主线程命令、callback 消费 | stale/QueueFull |

所有 generation handle 都由强类型 Tag 隔离。仅 index+generation 仍可能在另一个同类型 registry
碰巧命中，因此 API 同时限定 owner；Debug 保存 owner cookie/registry id，跨 World/Window/
Device 使用立即报错。Release 不依赖 cookie 保证业务正确，调用面应让错误 owner 难以表达。

## EngineConfig

`EngineConfig` 是可复制的纯值配置，Create 前一次性验证：

- product/app name、UTF-8 日志与资源根；
- primary window logical size、title、fullscreen/vsync；Headless 时 window 字段不生效但仍校验；
- fixed delta（默认1/60 s）、max fixed steps（默认4）、max accepted real delta（默认250 ms）；
- CPU/IO worker 数、Task/Event/Input transition queue capacity、shutdown deadline；
- FrameArena、Asset completion/upload、Audio command 和 Render resource 预算；
- backend policy：Audio required/optional、Headless/production factories；
- diagnostics：Metrics、trace backend 已由构建选择，运行时只能设置 capture/verbosity 策略。

未知字段、0/非有限 delta、容量乘法溢出、超平台上限尺寸、冲突 backend 组合在任何模块创建前
返回 `InvalidConfig`。默认值必须集中在 `EngineConfig::Defaults()`，不能散落到各模块。

## EngineFactories 与组合根

`EngineFactories` 是 bootstrap 构造并移动给 `EngineHost::Create` 的一次性值，至少明确选择
Platform、Render、UI text rasterizer/DisabledUI 和 Audio backend。它不是可在运行期查询的 registry，也不包含已经创建的
全局 service。`tina_runtime` 自己创建只有一种实现的 Memory/Task/Event/State orchestration；
只有需要 production/headless/null 选择的边界才使用 factory，避免把每个类都抽象成插件。

每个 factory 接受独立、窄且只在调用期间有效的 CreateParams。例如 Render 创建参数只借用
Diagnostics、Render 配置和已建立的 `RenderSurfaceDescriptor`，不能接收整个 EngineHost；
Audio 创建参数只得到 Audio 配置、所需 executor/asset 能力。返回值统一为
`Result<unique_ptr<Interface>>`，成功即由 EngineHost 接管，失败携带 backend/category/native
error code 并触发已完成阶段逆序回滚。

约束如下：

- 调用任何 factory 前一次性检查 bundle 完整性和 config/backend 组合；
- Headless + NullRender + DisabledAudio 是显式合法组合，不是生产 backend 失败后的偷偷降级；
- factory 不保存 CreateParams 借用、不注册全局对象、不在后台继续完成“半创建”；
- backend 实例的 shutdown 幂等且由 EngineHost 唯一调用；
- 测试 factory 可以注入第 N 步失败和销毁记录，但不能绕过正常所有权协议；
- 具体 callable/interface 形态可在首个 header 草案中收敛，但上述所有权与错误语义不可变。

## Frame timing

```cpp
struct FrameTiming {
    Duration realDelta;       // 未缩放、经有限性检查的 wall delta
    Duration updateDelta;     // 经 clamp/time-scale 后的 variable delta
    Duration fixedDelta;      // 固定 1/60 s
    double interpolation;     // [0, 1)
    std::uint64_t frameIndex;
    std::uint64_t simulationTick;
    std::uint32_t fixedStepIndexInFrame;
};
```

`realDelta` 不因暂停归零；被 Simulation 接受的 delta 有上限并单独记录 dropped time。time scale
只影响 gameplay simulation/update，不影响 Platform、UI 输入、Asset timeout、Audio device 或
diagnostics wall timeout。暂停/最小化规则见 Runtime 文档。

Input Action 不是一个同时供两个 update 域读取的共享 edge bag。`FixedUpdateContext` 只提供带
目标 tick 的 `SimulationActionSnapshot`，`UpdateContext` 只提供当前帧
`FrameActionSnapshot`；held/axis 也按 domain 显式绑定。这样0个 fixed step 可保留 Simulation
edge，却不会让同一 Press 先在 variable update 执行、下一 tick 再执行一次。

## Phase Context

- Context 不可复制/移动，构造函数只对 Runtime 可见；
- 每个 accessor 只暴露当前阶段能力，例如 Fixed Context 有 WorldCommands，不提供 UI/Render；
- Task 获取的是不可变输入和 Worker-local output，不能捕获 Context 本身；
- builder 使用 sticky first-error：第一次失败后后续 append 为空操作，Runtime 在回调结束强制
  检查；回调自己的 Status 与 builder error 合并时保留最早 phase/error context；
- 任何 callback throw 在边界转换为 `UnhandledException`；析构、rollback、onStop、audio
  callback 必须 noexcept。

## AppState

`IGame` 是 bottom frame client，overlay `IAppState` 由 Runtime AppStateStack 独占。每个状态声明：

- `blocksGameplayInputBelow`；
- `blocksUIInputBelow`；
- `blocksFixedUpdateBelow`；
- `blocksVariableUpdateBelow`；
- `blocksRenderBelow`。

Input/Update 从 top 向下，Render 从最底可见层向上。push/pop/replace 只返回 request id，在
Deferred Cleanup 按 sequence 提交；enter 失败产生 run error 并回滚，exit 是 `noexcept` 且不能
留下半退出状态。
AppState 可以拥有 World 与 UI roots，但不拥有 UIContext/RenderDevice/AssetSystem。

推荐最小生命周期为：

```cpp
struct StatePolicy {
    bool blocksGameplayInputBelow;
    bool blocksUIInputBelow;
    bool blocksFixedUpdateBelow;
    bool blocksVariableUpdateBelow;
    bool blocksRenderBelow;
};

class IAppState : public IFrameClient {
public:
    virtual Core::Status onEnter(StateEnterContext&) = 0;
    virtual void onExit(StateExitContext&) noexcept = 0;
    [[nodiscard]] virtual StatePolicy policy() const noexcept = 0;
};
```

Policy 在 enter 后保持稳定；动态变化提交显式 request 并在 Deferred Cleanup 生效。没有二值
`onPause/onResume`，因为各 phase 可独立被遮挡。push/replace 的新状态先在旧栈仍完整时执行
enter transaction，失败撤销新状态并保持旧栈；成功才提交并退出被替换状态。pop 先从下一帧
dispatch 集合移除并清理 roots/focus/capture/TaskGroup，再执行恰好一次 `noexcept onExit`。

## 资源与错误

Asset、Render、UI、Audio 和 World 的 descriptor 都是拥有自己字段的 Tina 类型；第三方类型
只在 adapter。外部文件/配置错误使用稳定 category/code + UTF-8 context chain；assert 不处理
用户数据。所有 size/count/offset 在分配前检查溢出，所有异步 completion 在提交前重新校验
owner + generation。

详细 API 在对应模块实现前补最小 header 草案和 compile-only consumer test；不能只让模块
自身测试通过，而 public header 单独 include 就失败。
