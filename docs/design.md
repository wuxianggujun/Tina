# Tina 设计导读

Tina 的设计目标不是“功能最多”，而是让游戏 Runtime 的模块边界、帧顺序和资源生命周期可以验证。
当前产品以 vNext Desktop 与 `tina_sample_2d` / `tina_sample_3d` 为准，Legacy 产品图已经退役。

## 设计原则

1. `EngineHost` 是唯一非全局组合根，模块通过明确接口和 factory 连接。
2. `IGameApplication` 管程序生命周期；`IGameState` 是唯一帧行为入口。
3. Game SDK 和公共头只暴露 Tina-owned 类型，具体 backend 保持私有。
4. 热路径结构有界、容量失败显式、稳态避免 Tina-owned 动态分配。
5. 异步取消只改变逻辑状态；物理释放等待 Lease、Ticket、completion 或 retirement 条件。
6. 源资产只在 Cooker 读取，Runtime 只消费版本化 Cooked Catalog。
7. 架构变更通过可运行垂直切片落地，每个切片必须有直接测试或 sample 证据。

## 已接受的技术边界

| 领域 | 决定 | 当前实现说明 |
| --- | --- | --- |
| 语言/编码 | C++23、UTF-8 | MSVC 使用 `/utf-8` 与 `/Zc:__cplusplus` |
| Platform | GLFW 私有 adapter，不引入 SDL/SDL3 | Null 与 GLFW 图分离 |
| Render | bgfx 是首个真实 backend | Render/Game SDK 头不含 bgfx |
| UI | Tina retained UI，输出 DisplayList | 当前实现在 `include/tina/ui` + `src/ui` |
| Asset | Catalog/Cooked + cgltf Cooker | cgltf 不进入 Runtime/public header |
| Audio | miniaudio 是可选真实 backend | backend-neutral AudioEngine 可独立测试 |
| Physics | Box2D 2D、Jolt 3D，API 分离 | Box2D adapter已实现；Jolt 尚未接入 |
| ECS | 如采用 EnTT，只能是 Scene 私有实现 | 当前 `tina_scene` 不链接 EnTT |
| 容器 | 标准库/`std::pmr` + 少量专用有界结构 | 不恢复 EASTL 产品依赖 |
| 测试 | GoogleTest executable 直接运行 | 不使用 CTest 调度 |
| Profiling | Tina Trace/Metrics + 可选 Tracy | `tina_bench` schema v1 已有；Trace/Metrics 与 Tracy adapter 仍后置 |

决策理由见 [ADR 索引](adr/README.md)，状态汇总见[设计冻结清单](design-freeze.md)。

## 系统协作

```mermaid
flowchart LR
    Game["IGameApplication / IGameState"] --> Runtime["EngineHost"]
    Runtime --> Platform["Platform + Input"]
    Runtime --> UI["Retained UI"]
    Runtime --> Scene["Scene extraction"]
    Runtime --> Audio["AudioEngine"]
    Runtime --> Render["RenderFrame"]
    Assets["Catalog / AssetSystem"] --> Scene
    Assets --> Render
    Task["IO / CPU / Main executors"] --> Assets
    Platform --> Glfw["private GLFW"]
    UI --> Display["UI DisplayList"]
    Display --> Render
    Render --> Bgfx["private bgfx"]
    Audio --> Mini["private miniaudio"]
```

游戏状态写 gameplay model、Scene extraction 和 UI retained state。Runtime 决定它们在帧内的调用顺序，
adapter 负责把 Tina-owned 数据翻译到第三方库。游戏代码不能越过 Runtime 直接驱动具体 backend。

## 三条关键数据流

### 输入

```text
OS/GLFW events
  -> bounded PlatformFrameView
  -> UI route against prior committed snapshot
  -> consumption + continuous claims
  -> ActionMapper
  -> fixed-step and frame action snapshots
  -> IGameState
```

这条顺序保证 UI 拦截先于 Gameplay mapping。输入溢出、失焦、reset 和同帧 Down/Up 都必须有显式语义，
不能依靠“当前 held 状态”猜测历史 transition。

### 资源

```text
source -> Cooker -> Cooked Catalog -> request/load -> Handle/Lease
       -> typed decode -> upload -> ReadyGpu
       -> Sprite2D Registry Entry {Lease, GPU, binding}
          -> packet-local FrameResourceRef -> AssetSystem retirement
       -> Mesh3D Registry {Mesh Lease/GPU/binding, Material Lease/binding, shared Texture Lease/GPU}
          -> packet-local FrameResourceRef -> AssetSystem retirement
```

`AssetId` 表示稳定逻辑身份，`ContentHash` 表示内容；二者不可混用。Handle 是弱查询，Lease 负责跨
Task/Render/Audio 生命周期保活。两类 registry 都固定容量并由 owner thread 串行访问。Sprite2D registry
借用 AssetSystem、RenderDevice 与可选 PMR，每个 Entry 唯一拥有 resident Lease/GPU/binding；extraction
把 binding intern 到当前 packet，entry borrow pin 阻止活跃帧 retirement，Render item 只携带 packet-local
`FrameResourceRef`。Mesh3D registry 借用 AssetSystem/device/PMR，唯一拥有 Mesh Lease/GPU/binding、Material
Lease/binding 与按 AssetId 去重的共享 Texture Lease/GPU；geometry/material extraction 同样只写 packet-local
ref，active frame pin 阻止 retirement，State 不保留第二份 cleanup owner。
binding key 在单个 RenderDevice 实例的同类 allocator namespace 内唯一且单调不复用；多个 registry 可安全
共享 device。Mesh/Material namespace 相互独立，Material 三张纹理与 factors 原子发布；caller-chosen
direct binding 与同类 allocator 共用 namespace，registry 管理期间不得混用。

### UI 与渲染

```text
UI mutation -> dirty/layout -> committed hit/paint/semantics
            -> UI-Render bridge -> DisplayList + Glyph atlas
            -> RenderFrame -> private bgfx UI pass
```

UI 不调用 bgfx。Render 不读取 UI 节点对象，只同步消费当前 submit 调用内的后端无关数据。
当前 UI 是 per-window owner-thread retained Element tree，输入读取上一份 committed hit，
structure/layout/hit/paint/semantics 候选全部成功后原子发布。Button 已有即时 hover/pressed/focus/disabled
反馈，但 Motion 尚未实现。Image/Icon 与 NineSlice 的 authoring、committed paint、root-scoped resolve/pin、
RGBA ImageQuad/backend、产品/失效/尺寸矩阵与 `ui_image_nineslice_v1` 性能证据已经落地；Component/Behavior
的全池 reservation/counter 与 `ui_component_build_v1` 也已关闭；强类型 StyleClass、node-local pseudo-state、
startup-only ColorToken registry/value、literal/token-backed BoxFill stylesheet、Runtime startup facade 与
`ui_style_state_v1` 已落地。下一阶段继续扩展运行期 token 更新/reverse dependency、Image tint/opacity 等
属性面与 paint-only Motion，具体边界见
[UI 框架设计](ui-framework.md)。

## 当前产品完成度

| 产品面 | 已有 | 仍缺 |
| --- | --- | --- |
| 2D | Catalog TileMap v3 stream root + deferred TileMapChunk、固定容量 Camera/layer demand/cancel/unload、retain-window demand-recency LRU、resident generation dirty cache；稳定 layer/object ID、对象 101/102 消费、角色/碰撞、Physics2D Box/Circle/Capsule + sensor + Distance joint、SpriteAnimationClip/Animator；World Sprite、standalone Particle/Trail 与 TileMap emit 保存 weak AssetHandle 并借用共享 resolver；全部 Sprite2D item 使用 packet-local `FrameResourceRef`；owner-thread fixed-capacity Sprite registry 唯一拥有 resident Lease/GPU/binding 并交给 AssetSystem retirement；advanced input、UI 设置、文本、Audio；可选 Box2D/FreeType/miniaudio | TileMap 优先级 IO/editor/自动 gameplay 生成/旧 schema migration、完整 FX asset/editor/GPU simulation、更多 shape/joint、2D lighting/navigation |
| 3D | multi-mesh/multi-prim SPLIT cook、Resources-owned AssetStore、Prefab/Scene weak Mesh/Material Handle、engine-provided/State-owned fixed-capacity Mesh3D registry、packet-local geometry/material ref、Mesh/Material/共享 Texture 统一 owner、原子 material bundle、baseColor/MR/normal 采样、material factors、World DirectionalLight3D→逐帧 RenderScene snapshot、URI 安全、Texture/Mesh retirement marker | 完整 PBR/IBL/shadow、point/spot light、light culling、通用 pass system |
| UI | Tree/layout/hit/route/paint/semantics、文本/Glyph、Focus Scope/Modal/Pointer Capture、ScrollView、Dropdown/Popup、虚拟 ListView/TreeView；Element/recipe authoring、完整预算 Component transaction、六类 Behavior side store 与 node/text/canvas/Behavior reservation/counter、统一 RoundedRect；Image/Icon content、Canvas Image/NineSlice、root-scoped resolver/pin、RGBA ImageQuad、产品/失效/尺寸矩阵与固定 workload；StyleClass、node-local pseudo-state、startup-only ColorToken registry/value、literal/token-backed BoxFill stylesheet、Runtime startup facade 与固定 workload；Slider Focusable/Focus semantics、RangeInput Arrow/D-pad 独立调值 command 与 Dark/Light 交互状态矩阵已关闭；accessibility action seam、Windows UIA Invoke/Toggle/RangeValue/Value patterns、HWND 桥接与跨进程 action gate | 运行期 token 更新/reverse dependency、Image tint/opacity 等属性面与 paint-only Motion；多行/grapheme/BiDi/复杂 shaping、完整 IME 候选窗、Narrator/Inspect 金标、AT-SPI |
| Runtime | State 栈/commands、四相位阻断、`blocksGameplayInputBelow` 空 snapshot、FramePin/CPU ledger、固定步长、bounded Task shutdown + Host-enforced TaskSystem deadline | 通用 GPU submission fence、多 World、Runtime 内置 Asset/World |
| 性能 | `tina_bench` schema v1 + provisional 结论；UI 50,000 节点深树 structure/layout/hit/paint 非递归回归，Popup publication 为线性步骤；UI clean/dirty/route/virtual collection、Image/NineSlice、完整 Component 与 Style workload | 完整 dirty-range pruning、固定门禁机 hard gate、多进程 MAD、Motion workload |

## 游戏侧正确姿势（摘要）

1. `IGameApplication` 只造初始 `IGameState` 并处理 `onShutdown`。
2. 帧逻辑只在 State：`fixedUpdate` → `updateFrame`（可排队栈命令）→ `extractRenderScene` → `updateUI`。
3. 产品入口用 `Tina::Desktop::CreateEngine` + `EngineHost::run`；测试才手写 factories。
4. 输入只消费 Action snapshot；UI 先 route，再 ActionMapper，再（按 policy）suppressed 下层输入。
5. 资源只走 Cooked Catalog + Handle/Lease；不在 Runtime 解析源 glTF/工程场景格式。

更细的帧序与 policy 见 [Runtime](runtime.md)；公共面见 [Public API](public-api.md)。上手阅读顺序见
[文档索引](README.md)。

## 如何推进

短期工作只从 [Backlog](backlog.md) 选取验收条件完整的任务。实现顺序遵循：

1. 保持文档/契约与 tip 源码一致（本索引与 runtime/public-api 为优先同步面）；
2. 在已完成 TileMap layer/stream、Physics2D 与 advanced input 契约上继续做独立垂直切片；
3. TileMap priority IO/editor、UI-002/UI-002-LINUX/UI-003、完整 PBR、通用 submission fence 与 bench
   hard gate 均保持独立任务，不把任一首切片扩写成完整产品能力。

任何“完成”声明都必须指出证据类型：单元测试、集成测试、sample 生命周期、结构化 JSON 或人工视觉。
进程 exit 0 不自动证明画面正确；Cooker 单测也不自动证明产品 E2E。
