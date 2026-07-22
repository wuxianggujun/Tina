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
| Profiling | Tina Trace/Metrics + 可选 Tracy | 完整 `tina_bench` 仍未实现 |

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
       -> typed decode -> upload -> ReadyGpu -> frame use -> retirement
```

`AssetId` 表示稳定逻辑身份，`ContentHash` 表示内容；二者不可混用。Handle 是弱查询，Lease 负责跨
Task/Render/Audio 生命周期保活。

### UI 与渲染

```text
UI mutation -> dirty/layout -> committed hit/paint/semantics
            -> UI-Render bridge -> DisplayList + Glyph atlas
            -> RenderFrame -> private bgfx UI pass
```

UI 不调用 bgfx。Render 不读取 UI 节点对象，只同步消费当前 submit 调用内的后端无关数据。

## 当前产品完成度

| 产品面 | 已有 | 仍缺 |
| --- | --- | --- |
| 2D | Catalog TileMap、角色/碰撞、UI 设置、文本、Audio；可选 Box2D/FreeType/miniaudio | 更完整关卡/动画/编辑工具、跨平台产品门禁 |
| 3D | glTF/GLB 最小 cook、baseColorTexture→Texture2D、Prefab、Scene extract、StaticMesh/Unlit、GPU upload | Cooked texture GPU 绑定/安全策略、PBR、multi-primitive merge、完整 multi-mesh 产品 E2E |
| UI | Tree/layout/hit/route/paint/semantics、文本/Glyph、7类交互/展示控件 | Focus Scope、Modal/Capture、多行/复杂 shaping、虚拟化、accessibility adapter |
| Runtime | 单 State 生命周期、固定步长、Platform/Input/UI/Scene/Render/Audio 接线 | 完整 State stack/commands、owning frame packet/pin、统一 shutdown deadline |
| 性能 | 有界 PMR 路径与模块级测试/bench | ADR 0018 benchmark protocol、固定门禁机与正式 `tina_bench` |

## 如何推进

短期工作只从 [Backlog](backlog.md) 选取验收条件完整的任务。实现顺序遵循：

1. 修复当前契约与实现不一致，包括 ADR 0017 CPU worker 默认值；
2. 清除 Legacy 依赖和兼容残留，保持产品图不回退；
3. 补 Linux 与产品级 multi-mesh/视觉证据；
4. 再扩展 UI、3D/PBR、性能协议和 State/packet 能力。

任何“完成”声明都必须指出证据类型：单元测试、集成测试、sample 生命周期、结构化 JSON 或人工视觉。
进程 exit 0 不自动证明画面正确；Cooker 单测也不自动证明产品 E2E。
