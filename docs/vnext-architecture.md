# vNext 目标架构

本文只描述已接受的不变量与“当前实现到完整目标”的差距。当前源码事实见 [架构总览](architecture.md)，
决策状态见 [设计冻结清单](design-freeze.md)，可执行工作见 [Backlog](backlog.md)。目标名称不等于
对应接口已实现。

## 产品目标

Tina 是 C++23 的 2D/3D 游戏 Runtime，产品路径为 Desktop bootstrap + Game State + samples。
Legacy `Tina.exe`、旧横版 2D 和旧 UI 产品图已经删除。当前 retained UI 位于 `src/ui`，属于 vNext。

核心不变量：

1. `EngineHost` 是唯一非全局组合根；
2. `IGameApplication` lifecycle-only，帧行为只在 `IGameState`；
3. Game API、Module SPI、Backend Private 三层分离；
4. 公开头不泄漏 GLFW/bgfx/Box2D/miniaudio/FreeType/cgltf/stb_image/EnTT；
5. 热路径有界、容量失败显式、借用寿命可验证；
6. Runtime 只读 Cooked Catalog，源资产只在 Cooker；
7. 逻辑失效与物理释放通过 Handle/Lease/Ticket/completion 分离。

## 目标模块图

```mermaid
flowchart TD
    Game["Game / IGameState"] --> Runtime["Runtime / EngineHost"]
    Runtime --> Platform["Platform + Input"]
    Runtime --> Task["Task"]
    Runtime --> Render["Render"]
    Runtime --> UI["Retained UI"]
    Runtime --> Audio["Audio"]
    Game --> Scene["Scene"]
    Game --> Asset["Asset"]
    Game --> Physics["optional Physics2D/3D"]
    Asset --> Task
    Asset --> Render
    Scene --> Render
    Platform --> Glfw["private GLFW"]
    Render --> Bgfx["private bgfx"]
    UI --> FreeType["private FreeType"]
    Audio --> Mini["private miniaudio"]
```

当前 `EngineHost` 已组合 Platform/Task/Render/UI seam/Audio；Scene、Asset 与 Physics由产品 State 持有。
未来若把 AssetSystem 组合进 Runtime，也必须保持 Game SDK 窄 capability，不能暴露模块 owner。

## 组合与启动

目标创建顺序保持事务式：Diagnostics/Memory → Clock → Platform → Task → optional Audio/Asset →
WindowSurface lease → Render → publish window。每一步成功后立即登记逆操作；失败时返回结构化 Error，
不发布半初始化 Host。

当前已经实现 Diagnostics、Clock、Platform、Task、optional Audio、WindowSurface/Render 的事务；独立
MemorySystem 与 Runtime-owned AssetSystem 尚未实现。

游戏启动目标：

```text
createInitialState
  -> bind primary UIContext or Headless
  -> initialState.onEnter
  -> commit initial policy/UI snapshot
  -> publish State runtime
```

当前只发布一个 State。完整 State stack/commands 是目标，见 `RUNTIME-001`；文档不得把
`GameStateStack` 当作现有类型或 owner。

## 目标 Frame Pipeline

当前已实现的主干：

```text
Platform poll/validate
  -> Platform lifecycle dispatch
  -> UI input route/default action
  -> Action Mapping
  -> 0..4 Fixed Update
  -> Frame Update
  -> Audio completion
  -> RenderScene extraction/commit
  -> UI update/layout/paint/semantics/DisplayList
  -> Render submit/present
```

完整目标在不破坏上述顺序的前提下增加：

- owning Runtime event/completion phase；
- State command 唯一提交点与 policy 调度；
- TaskGroup barrier 与 deterministic merge；
- GPU upload budget；
- owning `RenderFramePacket`、FramePin、submission completion；
- deferred resource retirement。

这些新增阶段必须有唯一 owner、容量、失败语义与 reset/retire 点，不能通过一个通用 EventBus 或裸
pointer 把生命周期问题藏起来。

## 输入与 UI 目标

Platform final snapshot、ordered transition、UI route result 与 Gameplay Action 保持独立。UI 必须先发布
consume/claim，ActionMapper 才生成 Simulation/Frame Action；world pointer 使用 last-presented Camera2D
锁存。

当前已有 retained tree、layout/hit/paint/semantics、Text/Glyph、常用控件、TextEdit、Keyboard/Gamepad
default action 与 Windows IMM32。目标差距是 UIA/AT-SPI、通用 Focus Scope/Modal/Capture、复杂 text 与
虚拟列表，不是“UI 尚不可见”。

## Scene、Asset 与 Render 目标

- Scene：generation World、Transform、2D/3D component 与 extraction 已完成；command buffer/多 World
  orchestration 后置。
- Asset：Catalog/Cooked、AssetId、Handle/Lease、IO Task/Main completion、typed payload、Texture/Mesh
  upload、retirement ledger 与 AssetLease completion pin 已有；hot reload/增量 Cooker 后置。
- Render：Null/bgfx、Sprite2D/Opaque3D/UI Glyph、Texture2D/StaticMesh binding、owning packet 与 readback
  retirement marker 已有；通用 pass scheduler 与完整 PBR 后置。
- Physics：Box2D 2D 产品路径已有；Jolt 3D 未接入。

multi-mesh glTF Cooker 已生成 distinct AssetId/Prefab dependency；`3D-001` 产品 sample 已关闭两个 mesh 的
upload/bind/extract/draw E2E。

## 所有权与借用

| 对象 | owner | 当前/目标寿命规则 |
| --- | --- | --- |
| Platform frame | Platform backend | 到下一次 poll；回调内借用 |
| Phase Context | Runtime stack | 对应 callback 返回即失效 |
| UI root/listener | Game State RAII | token 不保活 Context/root |
| RenderScene/UI view | Runtime builder | 当前仅 submit-call-local borrow |
| AssetHandle | Asset registry | 弱 lookup，可失效 |
| AssetLease | Asset payload | 强保活 CPU payload |
| GPU handle | RenderDevice | generation backend owner |
| FramePin/packet | present-return CPU complete 已落地；Texture/Mesh 用独立 readback marker retirement | 通用 GPU submission fence 后置 |

任何 borrowed view 都必须注明精确失效点。不能用“一律不能跨帧”代替 committed UI view、Platform
view、phase writer 和 Lease 各自不同的规则。

## 关闭目标

当前关闭：State exit → Application shutdown → UI/dispatcher → Audio → Render → Task join → Platform →
Clock → Diagnostics。完整目标增加 State TaskGroup barrier、Asset/Audio lease drain、GPU completion 与
hard failure policy，但必须保持“join/completion 之前不释放被访问 owner”。

## 当前差距

| Backlog | 目标差距 |
| --- | --- |
| `TASK-001` | **Done**：Desktop 交互 CPU worker 默认 |
| `RUNTIME-001` | stack/四相位 policy **Done**；gameplay input policy / 交互暂停 / stale-owner 矩阵仍后置 |
| `RUNTIME-002` | FramePin + present-return CPU completion **Done**；RENDER-FENCE 的 Asset/Texture/Mesh readback retirement 亦已完成，通用 submission fence 非当前契约 |
| `3D-001` / `ASSET-001` | multi-mesh E2E + URI 安全 + base/MR/normal texture sampling **Done**；完整 PBR/IBL/shadow 后置 |
| `UI-002` / `UI-003` | accessibility 外部真机尾项与跨 DPI/GPU 视觉矩阵仍开放 |
| `UI-004` / `UI-005` | **Done**：Focus Scope/Modal/Pointer Capture，以及 ScrollView、Dropdown/Popup、虚拟 ListView/TreeView |
| `TEXT-001` | 多行编辑、grapheme/shaping 与完整 IME 候选窗仍开放 |
| `PERF-001` | schema v1 **Done**；固定机 hard gate / 多进程 MAD 后置 |
| `CLEAN-001`～`CLEAN-003` | **Done**（扫尾记录） |
| `TEST-001` / `TEST-002` | Linux 当前 tip 仍缺；product-2d 同轮门禁 **Done** |

实现目标差距时，若反转 Accepted 决策必须新增 ADR 并 supersede 旧 ADR；不能只修改本文。
