# ADR 0045：`Tina::Script` 玩法脚本边界

- 状态：Proposed
- 日期：2026-09-03
- 决策者：Tina maintainers

> **实现状态：** 截至 2026-09-03 全仓无脚本运行时。`include/tina/script`、`Lua`/`Luau`/`Wren`
> 产品代码均为零命中。下文类型名与 API 是设计草案，**不可调用**。本 ADR 被 Accepted 之前，
> 禁止在 `include/` 占位，也禁止把 Luau 头漏进 Game SDK。玩法控制流继续用
> [ADR 0036](0036-gameplay-tooling-boundaries.md) 的 `Scheduler` / `Action` / `Signal<T>`。

## 背景

Tina 的帧循环、资源、物理步进和渲染提交已经有唯一 owner：`EngineHost` 调 `IGameState`，
2D 场景由 `Scene2DRuntime` 固定 `demand → commitReady → fixedUpdate → fixedUpdatePhysics → extract`
（[ADR 0031](0031-scene-2d-runtime-ownership.md)）。Catalog 只读 cooked 资产
（[ADR 0009](0009-cooked-assets-and-cgltf.md)），lease 与退役是硬契约
（[ADR 0016](0016-asset-ownership-and-retirement.md)）。公开头不泄漏第三方类型。

缺的是「不重编 C++ 也能改玩法控制流」。这不是缺 timer（0036 已补），也不是缺数据驱动
（Tileset / Fx2D / World2D 已是 cooked 依赖图）。缺的是**可热换的、有预算的、失败可上报的
玩法程序**。若不开脚本，策划改开门逻辑必须改 C++；若把脚本做成第二套引擎，
定容、lease、帧序和 `Result` 会从类型系统退化成作者自觉。

[ADR 0036](0036-gameplay-tooling-boundaries.md) 已拒绝通用 coroutine，并写明：任意语句中间
挂起是独立切片，且必须先决定栈所有权。本 ADR 不撤销那条；v1 脚本跨帧等待只能**提交** C++
`Action` / `Scheduler`，不能在 VM 里 `yield`。

参考（按 `docs/carbon-reference.md`，只取舍不移植）：Roblox Luau 的沙箱与 ModuleScript；
Love2D 的宽宿主（反例）；Godot GDScript 作为节点组件（引擎从第一天按此生长，Tina 不是）。

## 待确认决策

本 ADR 处于 Proposed。确认前不建立 `ErrorDomain::Script`、不改 `MemoryTagCount`、不新增 CMake
option。任何改选都在本 ADR 更新后再进入实现。

| # | 决策点 | 推荐 | 主要备选与取舍 |
| --- | --- | --- | --- |
| D1 | 要不要做 | **先冻结本 ADR，作为 Later；有「不重编就要改玩法」的产品场景再进 Next** | 现在就写 VM：没有验收命令，会和 iOS / 3D 物理 / Bundle 抢验证资源。永远不做：2D 产品被锁在 C++ State 里 |
| D2 | 语言 | **只做 Luau，且不留多语言 SPI** | PUC Lua 5.4：默认 `_ENV`/debug/os 在，沙箱后补。Wren：fiber 直接撞 0036 的栈所有权。QuickJS：弱类型与 `Result` 不对齐。C# / V8：体积、GC、公开头全冲突。多后端 SPI：成本在绑定和 cooked schema，不在 VM |
| D3 | 模块形态 | **可选静态库 `Tina::Script`，公开头只依赖 Core（+ 后续白名单里出现的 Tina 类型的值/id）** | 放进 Runtime：让每个游戏为脚本付容量，并把 VM 生命周期绑到 Host 而非 State。放进 Gameplay2D：3D / 无物理产品无法用。Luau 头只存在 `src/script/luau/` PRIVATE |
| D4 | 谁拥有循环 | **C++ 帧拥有循环。脚本是 `IGameState` 的客人：每相位 `pump(phase, dt, budget)`** | 脚本 `while true` + `wait`：第二套主循环，暂停 / `GameStatePolicy` / `onExit` 取消全部失效 |
| D5 | 跨帧等待 | **v1 无 VM coroutine / fiber。`wait` 通过提交 `Gameplay::Action` 或 `Scheduler` 表达** | 通用 `coroutine.yield`：必须先定栈 owner、取消、与 time scale 的关系（0036 已列为独立切片）。v2 若做，只允许**引擎拥有的、可随 State 退出必清的 suspend token**，仍不是通用 fiber |
| D6 | 脚本互依赖 | **`require("逻辑名")` 是一等公民；cook 期解析字面量，写成 Catalog typed dependency** | 无 `require`：作者只能单文件，这才是残。运行时搜 `package.path`：Catalog 无法预检，热更不知道拉哪些字节码。`require(变量)` 动态路径：拒绝 |
| D7 | 宿主 API 面 | **白名单绑定，默认拒绝。脚本不能 `step` 物理、不能提交渲染、不能持有 `AssetLease` 过帧、不能建 GPU 资源、不能改 retained UI 树** | 反射公开 C++ 头 / 对齐 Love2D：同一份不变量出现两条入口，测试从「一条帧序」变成「脚本所有交错」。宽 API 是所有权泄漏，不是功能 |
| D8 | 失败与异常 | **VM 内错误在 PRIVATE 边界打成 `Status`。禁止脚本错误变成 C++ 异常穿过 `EngineHost`** | `lua_error` 漏到帧循环：wasm `-fno-exceptions` 上是 abort，桌面是和 150+ `std::terminate` 同位的不可恢复路径 |
| D9 | 预算 | **每相位指令数 + 分配字节 + 回调深度。超了 `ScriptBudgetExceeded`，停本帧脚本，场景留下，不 `terminate`** | 无预算：无限循环冻死进程。超了回滚整个 World：一处 AI 写炸会把物理权威状态拆掉，比「这帧 AI 没跑」更糟 |
| D10 | 分配 | **`lua_Alloc` → 未来的 `MemoryTag::Script`；堆上限在 `ScriptEngineConfig`，超了 `CapacityExceeded`** | 走系统 heap：脚本成为唯一不受 `MemoryTracker` 约束的模块。GC 自行 `realloc` 对象图：破坏全引擎定容约定 |
| D11 | 热更 | **脚本模块是 cooked 字节码资产。`reloadCatalog()` 换世代 = 丢全部闭包与 module cache，不保证 upvalue 存活** | 尽力保住 upvalue：和 lease 双驻留失败即全局回滚相反，热更会留下跨世代句柄 |
| D12 | 确定性 | **`math.random` 走引擎播种；禁止读时钟 / 文件系统 / 网络。网络只允许 post 到已有 `Tina::Network` 命令，v1 甚至可以完全不绑网络** | 暴露 `os`/`io`：模组 = 进程权限，且 product gate 无法 bit-exact |
| D13 | 线程 | **ScriptEngine 与 Platform 一样 owner-thread。TaskSystem 上只 cook/编译，不执行玩法** | worker 跑脚本：所有 Scene/Physics/UI 绑定立刻变成数据竞争 |
| D14 | Editor / sample | **v1 不必有脚本 tab。先：宿主单测 + `assetc` 字节码 + 一个 sample State。面板后置，避免 Fx2D「有 payload 无消费面」** | 先做 REPL：没有 cooked schema 和预算，REPL 会变成第二套加载器 |

## 决定

（Proposed 草案，Accepted 后才生效。）

`Tina::Script` 是玩法程序的定义点，不是第二套引擎。C++ 继续拥有帧循环、物理步进、渲染提交、
Catalog/Lease。脚本拥有的是：**在预算内、对白名单做玩法决定，并用 `require` 把这些决定拆成模块。**

### 1. 一门语言，可选模块

只支持 Luau。CMake 以目标平台闸或 `TINA_BUILD_SCRIPT_LUAU` 为 opt-in，默认 OFF，对标
`TINA_BUILD_NETWORK_TLS`。公开头路径 `include/tina/script/`，零 Luau 类型。实现隔离在
`src/script/luau/`。

落地时占用下一个空闲槽位（当前 [已验证] `ErrorDomain::Animation3D = 18`，
`MemoryTag::Animation3D = 15`，`MemoryTagCount = 16`），即 `ErrorDomain::Script = 19`、
`MemoryTag::Script = 16`、`Count = 17`。**Accepted 并改头文件之前这些值不存在。**

### 2. 脚本是 State 的客人

`ScriptEngine` 由 `IGameState`（或其持有的场景对象）创建、pump、销毁。不要放进 `EngineHost`
模块表：State `onExit` 必须能停掉全部脚本拥有的 timer/action，并释放模块 lease。
`start()`/`tick()` 与 `run()` 共用同一帧体，脚本不因此获得第二种调度。

约定入口（名称可在实现时微调，语义不行）：`onEnter` / `onFixedUpdate` / `onFrameUpdate` /
`onExit`。**没有** `onExtract`：extract 仍是 C++ 读组件数据。

### 3. `require` 是 cooked 依赖，不是文件系统

作者源码使用字面量 `require("ai/chase")`。`assetc` 解析之，写成与 Fx2D
`spriteDependencyIndex` 同类的 typed dependency。运行时 `require` 只查当前模块的依赖表。

- 循环依赖、缺失模块、非字面量 `require`：cook 失败
- 同一 `ScriptEngine` 实例内模块 table 缓存；Catalog 新世代丢 cache
- 无 `package.path`，无 C 模块，无动态拼路径
- `maxScriptModules` 与每模块字节数是 config 硬顶

宿主 API 窄、模块系统完整：二者正交。禁止把「窄宿主」理解成「不能拆文件」。

### 4. v1 宿主白名单（默认拒绝，只开这些）

- 读 `FrameActionSnapshot` / `InputActionId`（输入已经过 Action 映射，不把物理键暴露给脚本）
- 对已存在 `EntityId` 做查询与少量字段写入（transform 等）；**禁止**遍历/创建整棵 World 的反射袋
- 调用 `Gameplay::Scheduler` / `ActionRunner` / `Signal` 的脚本投影（提交 C++ 时序，不在 VM 里再实现 tween）
- 按 `AssetHandle` 请求 `playAudio` / Fx burst；lease 仍由 C++ runtime 持有
- Physics **查询**（ray / aabb / grounded）与 `enqueue` 冲量/传送；**禁止** `step()`、改 gravity、建/毁 World
- 可选：命名 HUD 槽 `Hud.set(name, value)`。**禁止**创建/销毁 UI 节点、改 layout 树

明确永不在 v1：`RenderDevice`、GPU 资源、`AssetSystem` 直接 load/unload、文件系统、`os.execute`、
原生库、网络套接字、`extractRenderScene`。

新增绑定的准入与 Later 项相同：产品场景、容量、失败语义、验收命令。禁止「扫描公开头生成绑定」。

### 5. 失败、预算、确定性

- 脚本错误、预算打满、容量打满：`Status`，带模块逻辑名与源行号。本帧该引擎停止 pump，World / Physics 不回滚
- `lua_Alloc` 计入 `MemoryTag::Script`；超堆上限不再分配，当前调用失败
- `math.random` 必须经引擎 RNG；未播种则 pump 失败，不许静默落到系统随机
- 重入 `pump` 返回 `ReentrantDispatch`（与 0036 同一条），不递归执行

### 6. 验收（Accepted 之后的实现门禁，现在不跑）

1. header-isolation：公开 `tina/script/*.hpp` 不含 luau 标识符
2. 宿主单测（Windows 即可）：预算打满停本帧且场景仍在；`require` 字面量依赖 / 循环 cook 失败 / 动态 `require` 拒绝；错误不抛过 C++ 边界
3. `assetc` 产出带 schema version 的字节码；malformed corpus 进 `tina_asset_format_tests`
4. 一个 sample State：用脚本完成「开门 + 一种 AI」，且不链接 Luau 到公开消费方

没有 Mac / 真机 / wasm 门禁作为 v1 退出条件。Luau 是 C++，交叉编译风险在分配钩子，不在语言。

## 结果

- 正面：玩法热换有唯一契约；作者可按文件拆模块；引擎不变量仍只有一个入口
- 成本：Love2D 派会觉得宿主残；v1 过场必须写 `Action` 树而不是 `wait(0.4)`；绑定每加一条都要测试帧序交错
- 限制：无 coroutine、无宽反射、无 Editor REPL、无多语言
- 门禁：见上；确认前零源码

## 被拒绝方案

- **先写用户手册 / cookbook，不写 ADR：** 手册没有失败语义，实现时仍会争论 `require` 是否动态、错误是否抛异常
- **占位 `include/tina/script/ScriptEngine.hpp` 空壳：** 与 ADR 0027 同一纪律，Proposed 不占位
- **PUC Lua 5.4「先嵌再收沙箱」：** 默认不安全；Catalog 内容可校验的前提会被破
- **对齐 Love2D / Unity MonoBehaviour 的宿主宽度：** 废弃 0016/0031/0036 的所有权，换一套无法 header-isolate 的第二公开 API
- **无 `require`、只注册全局回调：** 把「窄宿主」误做成「单文件」；这才劝退写玩法的人
- **运行时 `package.path` 搜 `.luau`：** 与 cooked-only 冲突，热更与缺失依赖无法在 cook 失败
- **v1 通用 coroutine：** 未决定栈所有权；与 0036 D 节直接冲突
- **脚本放进 `EngineHost`：** timer 已经因此被拒绝过一次（0036）；VM 更不该绑 Host 寿命
- **多语言 SPI：** 绑定和 schema 会按语言分叉，收益只在 FAQ
- **复活或对标任何「扫描引擎反射到 VM」的绑定生成器：** 测试矩阵不可关闭
