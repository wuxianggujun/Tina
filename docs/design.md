# Tina 设计导读

这份文档用于回答三个问题：当前引擎是什么、各模块如何协作、下一步为什么这样推进。它描述设计边界，不替代各主题文档中的实现细节。

## 一句话定位

Tina 当前是一套以现有游戏为验证载体的小型 2D/3D Runtime。近期目标是让 Runtime、Scene、资源、渲染和自研 UI 的生命周期稳定且可测试，而不是立即建设编辑器、完整自研 RHI 或通用商业引擎。

## 已确定的技术边界

| 领域 | 当前决定 | 原因 |
| --- | --- | --- |
| 语言与编码 | 目标基线为 C++23、UTF-8，MSVC 使用 `/utf-8`；当前 CMake 尚为 C++20 | 后续独立恢复 C++23，不能只改文档而跳过跨平台验证 |
| 窗口与基础输入 | GLFW，不引入 SDL/SDL3 | 保持平台层单一；Windows IME 仅用 IMM32 补充 |
| 渲染 | bgfx | 先复用成熟后端，不在当前阶段自研 RHI |
| UI | Tina 自研 Retained UI | 服务游戏内 UI，并用 generation `NodeId` 管理交互生命周期 |
| ECS | EnTT | 当前仍暴露 registry 与 `entt::entity`；新接口应逐步收敛到 Tina `EntityId` |
| 音频 | miniaudio | 单一轻量音频后端 |
| 2D 物理 | Box2D 3.x；现有封装尚未接入玩法 | 保留必要的 TileMap AABB，不提前统一 2D/3D 物理 API |
| 3D 物理 | Jolt 已确定为唯一目标后端，但尚未接入 | PhysX、Bullet、Rapier 不进入依赖或备用实现 |
| 测试 | GoogleTest 1.17.0，直接运行 `tina_tests` | 不使用 CTest 调度，失败由进程返回码表达 |

## 当前模块与所有权

```mermaid
flowchart TD
    Main["main / Game application"] --> Application["Application"]
    Application --> Platform["GLFW Window + Input"]
    Application --> Events["EventSystem"]
    Application --> Resources["FileSystem + ResourceManagerHub"]
    Application --> Audio["miniaudio"]
    Application --> Scenes["SceneManager"]
    Application --> RenderServices["Renderer services"]
    Scenes --> GameScenes["Menu / Game / UI Smoke / 3D Smoke"]
    GameScenes --> World["EnTT World + game systems"]
    GameScenes --> UI["Retained UI tree"]
    GameScenes --> Rendering["2D / 3D render paths"]
    UI --> Events
    UI --> Rendering
    Rendering --> Bgfx["bgfx"]
```

`Application` 目前仍是组合根和主循环所有者。这个职责偏重，但在初始化回滚、Scene 延迟操作和渲染资源边界尚未全部获得测试前，不应直接替换成大型 `EngineHost` 重写。

## 当前每帧数据流

```text
Frame timing
  -> Input begin frame
  -> GLFW poll / window state
  -> queued Event dispatch
  -> resource completion budget
  -> application event hook
  -> bounded fixed simulation (60 Hz, max 4 steps)
  -> variable application and Scene update
  -> UI update / layout / routed input
  -> Scene render and application render hook
  -> Input end frame
  -> deferred tasks
  -> bgfx frame submit/present
```

输入快照、普通 Event Queue 和 UI routed event 是三条不同通道。它们可以在同一帧中协作，但不能共享隐式泵送点或绕过各自的生命周期规则。

## 当前实现与目标边界

| 领域 | 当前实现 | 近期目标 | 暂不建设 |
| --- | --- | --- | --- |
| Runtime | `Application` 持有多数服务 | 补齐失败回滚、析构顺序和阶段测试，再逐步提取 Context | 一次性替换全部 Runtime API |
| Scene/ECS | Scene 栈、延迟操作、Scene 自有 UI roots；World 仍暴露 EnTT/bgfx | 测试切换提交点，并逐步引入 `EntityId` 与 render extraction | 编辑器式多 World 管理 |
| UI | Retained Tree、路由事件、焦点、IME、虚拟列表 | 完成 action 生命周期、常用表单控件、可访问语义和截图门禁 | 用 UI 系统直接承担完整编辑器 |
| Render | 多条 2D/3D 路径直接提交 bgfx | 先统一 Pass/View 所有权，再引入 typed handle 与 UI Display List | 自研多后端 RHI、完整 Render Graph |
| Asset | 按路径异步读取，主线程 completion 有任务预算 | 分离 CPU decode 与 GPU upload，再设计稳定 AssetId/Cooker | 当前直接上完整热重载编辑器管线 |
| Physics | Box2D 工具封装尚未接入玩法，Jolt 尚未集成 | 2D 正式接入 Box2D，3D 只接入 Jolt，并分别建立性能门禁 | 第三套物理后端或强行统一 2D/3D Physics API |

## 推荐推进顺序

Carbon 对应模块取证已经完成，详细证据和采纳/拒绝矩阵见
[Carbon Engine 参考取证](carbon-reference.md)。这次分析改变了短期优先级：先停止横向
增加 UI 控件，补齐跨模块生命周期门禁，再回到产品 UI。

1. 为 Application 的 Window、bgfx、Event、Input、Resource 和 Renderer 初始化阶段增加
   可注入失败点，验证逆序回滚；不进行一次性 `EngineHost` 重写。
2. 参考 Trinity Render Job 的命名、计时与失败恢复，建立小型帧内 Pass Scheduler、
   typed generation handle、NullRenderDevice 和 GPU 资源计数；不复制动态 Step 类型体系。
3. 参考 BlueAsyncRes 把当前资源 completion 中连续执行的 CPU Decode/GPU Upload 拆成
   两个队列，并同时按任务数、字节和时间预算。
4. 参考 Destiny 在 fixed phase 中增加 World mutation barrier/deferred command buffer，
   保证实体销毁、层级变更和 interpolation snapshot 不互相踩生命周期。
5. 上述边界稳定后实现设置页真正需要的 Checkbox、Slider，并继续收敛 UI Display List、
   可访问语义和截图回归；不按控件数量衡量 UI 完成度。
6. 在 Render/Asset 所有权稳定后建设 `tina_assetc`、Cooked Manifest、AssetId 和最小
   静态 glTF cooker。
7. 只有这些契约稳定后，再评估是否需要把 `Application` 收敛为
   `EngineHost/EngineContext`。

每一步都必须能独立构建、直接运行 GoogleTest，并至少通过对应的 2D、UI 或 3D 冒烟路径。进程返回 0 只能证明运行和释放链路；画面正确、实体手柄兼容性等结论必须单独记录证据。

## 设计讨论的默认基线

在没有新的产品需求前，默认把 Tina 设计为“游戏优先的 Runtime”，而不是“编辑器优先的通用引擎”：

- 现有 2D 游戏和 3D Smoke Scene 是架构验收载体；
- UI 优先满足菜单、设置、HUD、对话框和长列表；
- Renderer 优先保证明确的资源所有权和 Pass 顺序；
- Asset pipeline 优先支持可重复构建，不追求首期在线编辑；
- 新抽象必须消除现有复杂度或建立可测试边界，不能只为了名称更像成熟引擎。
