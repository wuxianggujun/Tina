# ADR 0031：`Scene2DRuntime` 拥有 authored 场景的运行时实例化

- 状态：Proposed
- 日期：2026-08-27
- 决策者：Tina maintainers

## 背景

[ADR 0030](0030-gameplay-2d-binding-and-physics-bridge.md) 把 `ResourceBinding2D` 的实例化划给
「游戏或 Asset 层」。2026-08-27 核验的结果是：**那句话的实际效果是没人做**。
`grep -rn resourceBinding2D src/ --include=*.cpp` 除 `src/scene/World.cpp` 与
`src/scene/World2DSnapshot.cpp` 之外零命中。因此 Editor 能创建、能改绑、能存盘的
`TileMap2D`/`FxEmitter2D`/`NavigationRegion2D`/`AudioPlayer2D` 四种节点，加载回 runtime 后仍然完全惰性：
不画、不响、不阻挡寻路。ADR 0030 修好了「payload 被静默丢弃」，但没有修「payload 没有意义」。

同一次核验暴露了更根本的问题：**引擎没有「让一个 authored 场景跑起来」的所有者**。
`samples/2d_tilemap_bgfx/main.cpp` 有 **6660 行**，其中相当篇幅是手写编排，而这些编排对每个游戏都一样：

- TileMap 必须按 `updateDemand -> AssetSystem::pump -> commitReady -> extraction` 的**固定顺序**每帧调用
  （`include/tina/asset/TileMapStream.hpp:48-49`）。顺序错了不会编译失败，只会少画或画到过期 chunk；
- TileMap 需要先自行 `loadOne`/`acquire` **两个** lease（TileMap + Tileset），`TileMapStream::Create`
  只接受已经 kind-checked 的 lease，不接受裸 `AssetId`（`src/asset/TileMapStream.cpp:120`）；
- Fx2D 需要 caller 自己 parse payload、自己解析 payload 里的 `spriteAssetId`、自己 `emitBurst`
  初始 burst（`Fx2DFactory` 返回它但不发射），并且 `ParticleSystem2D` 与 `Trail2D` 是**两个**对象、
  各有 update 与 extract；
- AudioClip 的 clip view 是**非拥有**的：cooked payload 必须活到终态 `Stopped`/`Cancelled` 被 pump 到
  为止（`include/tina/audio/AudioClipView.hpp:10-11`），否则是 use-after-free；
- 两种发射约定还不一致：TileMap 填 caller 拥有的 `std::pmr::vector<RenderSprite2DInput>`，
  ParticleSystem/Trail 直接写 `RenderSceneWriter`。

也就是说：**每个游戏都要重写同一套编排，并且每一处顺序或生命周期错误都是安静的。** 这不是「缺个便利
封装」，这是缺一个所有者。

依赖关系允许集中解决（全部来自各模块 `CMakeLists.txt`）：

| target | links |
| --- | --- |
| `tina_scene` | Core, Render, AssetFormat, AssetTypes |
| `tina_asset` | Core, AssetTypes, AssetFormat, Task, Render, **Navigation2D**, **Physics2D** |
| `tina_audio` / `tina_navigation2d` / `tina_physics2d` | Core |
| `tina_gameplay2d` | Core, **Scene**, **Physics2D** |
| `tina_runtime` | Core, Platform, Task, Render, WindowSurfaceIntegration, UI, Audio |

关键事实：**依赖边只从别处指向 Scene，从不反向**。`tina_asset` 已经公开链接 Navigation2D 与 Physics2D，
且**不**链接 Scene；`tina_audio`/`tina_navigation2d`/`tina_physics2d` 只链接 Core。除
`tests/physics2d` 外没有任何 target 链接 `Tina::Gameplay2D`。因此 `tina_gameplay2d` 追加
`Tina::Asset` + `Tina::Audio` **不产生环**，并且它已经是「两个互相不可见的子系统唯一同时可见处」
（`src/gameplay2d/CMakeLists.txt:17-19` 的既有注释）。

另一条约束：`tina_runtime` 既不链接 Scene 也不链接 Asset，所以**接线只能发生在 game/app 层**
（`tina_editor_app` 正是这么做的）。本 ADR 不改变这一点，也不把 World 交给 Runtime 托管——
`design-freeze.md` 的「多 World/editor orchestration」后置项不被触碰。

## 待确认决策

| # | 决策点 | 推荐 | 主要备选与取舍 |
| --- | --- | --- | --- |
| D1 | 所有者形态 | **单个 `Scene2DRuntime`**，位于 `tina_gameplay2d`，负责四种 resource kind 的实例化、生命周期与每帧编排 | 每 kind 一个小 bridge 分散在各自子系统模块：模块更薄、可按需链接，但**每帧顺序仍然是游戏的问题**——而那才是 sample 6660 行的真正来源；纯文档 + 参考实现：最省事，但每个游戏重复同一批安静的顺序与生命周期错误 |
| D2 | Gameplay2D 的依赖 | 追加 `Tina::Asset` + `Tina::Audio`（已核验无环） | 让 Scene 直接链接这些：Scene 会把 Box2D/Asset 拖给所有消费者（含纯渲染、纯 UI 场景），违反 ADR 0010 的后端分离 |
| D3 | 运行时状态位置 | **side table**，按 `EntityId` 索引，组件内不存任何运行时句柄 | 把 `TileMapStream`/`AssetLease`/`AudioVoiceId` 放进组件：Scene 立刻需要链接 Asset/Audio，且 `EntityRecord` 会为每个 entity 付出全部 kind 的体积 |
| D4 | 顺序保证 | 由 `Scene2DRuntime` 的 `updateDemand/pump/commit` 三段式**在内部**保证，不暴露给游戏 | 只在文档里写顺序：与现状等价，等于没解决 |
| D5 | 物理桥归属 | `Scene2DPhysicsBridge` 保留独立类型，但由 `Scene2DRuntime` 作为第五种 kind 统一驱动 | 合并进 `Scene2DRuntime`：物理有独立的 fixed-step 语义与 authoritative 方向，混进同一个类会让 `applyTo` 的时序含义变模糊 |
| D6 | 发射约定 | `Scene2DRuntime::extract(writer, sink, resolver)` 统一成写 writer；TileMap 的中间 vector 由 runtime 内部持有 | 保留两套约定：调用方必须记住哪种 kind 用哪套 |
| D7 | `active` 标志 | `active == false` 的节点被实例化但不 update/不 extract | 不实例化：切回 active 需要重新 load，会在 gameplay 中造成不可预期的卡顿 |

## 决定（Proposed）

### 1. `Scene2DRuntime` 是 authored 场景的运行时所有者

新类型 `Tina::Gameplay2D::Scene2DRuntime`，头文件 `include/tina/gameplay2d/Scene2DRuntime.hpp`。
`tina_gameplay2d` 追加 `Tina::Asset` 与 `Tina::Audio`。

它的职责边界是**编排与生命周期**，不是重新实现子系统：TileMap residency 仍归 `TileMapStream`，
particle/trail 仍归 `ParticleSystem2D`/`Trail2D`，寻路网格仍归 `NavigationGrid2D`，
播放仍归 `AudioEngine`。`Scene2DRuntime` 只做四件这些类型自己做不到的事：

1. 从 `ResourceBinding2D::assetId` 走到「可用的运行时对象」，包括它们各自要求的前置
   lease 获取与 kind 校验；
2. 把运行时状态放进按 `EntityId` 索引的 side table；
3. 按每个子系统各自的契约驱动每帧调用，**顺序在内部固定**；
4. `shutdown()` 逆序释放全部 lease、body 与 voice。

### 2. 生命周期与 side table

运行时状态一律不进组件（D3）。`Scene::ResourceBinding2D` 保持纯数据，因此 `tina_scene` 继续不链接
Asset/Audio/Physics2D，ADR 0010 与 ADR 0030 的结论都不被推翻。

`Scene2DRuntime` 拥有：每个 TileMap 节点的两个 root `AssetLease` 与一个 `TileMapStream`；每个 Fx 节点的
`Fx2DInstance` 与其 sprite lease；每个 Audio 节点的 clip lease，**持有到终态 completion 被 pump 到**；
Navigation 节点的 `NavigationGrid2DData`/`NavigationGrid2D`。

`shutdown()` 必须在 `AssetSystem`、`AudioEngine`、`PhysicsWorld2D` 之前调用，与
`TileMapPhysicsSync2D`/`Scene2DPhysicsBridge` 同一条契约。它可重复调用。

### 3. 每帧三段式

```text
Scene2DRuntime::updateDemand(camera)   // TileMap chunk demand
AssetSystem::pump(budget)              // 调用方持有 AssetSystem，故这一步在外部
Scene2DRuntime::commitReady()          // chunk residency 提交
Scene2DRuntime::fixedUpdate(delta)     // particles / trails
Scene2DRuntime::extract(writer, ...)   // tilemap + fx sprites
```

`pump` 留在外部是因为 `AssetSystem` 是调用方拥有的、且服务于整个游戏而不只是这个场景；把它藏进
runtime 会让一个场景对象隐式驱动全局资源系统。其余三步的相对顺序由 runtime 内部断言保证：
`commitReady` 之前调用 `extract` 返回错误而不是安静地少画。

### 4. `active` 语义

`active == false` 的节点仍然被实例化并保留 lease，但不参与 update 与 extract（D7）。切换 active 因此是
一次布尔翻转而不是一次加载，不会在 gameplay 中引入不可预期的卡顿。

### 5. 不做的事

- **不**引入 `Scene::World` 的开放组件注册：ADR 0030 的 D1 结论不变，本 ADR 不提供新证据推翻它；
- **不**把 `World` 或 `Scene2DRuntime` 交给 `tina_runtime` 托管：runtime 不链接 Scene/Asset，接线留在
  game/app 层；
- **不**合并 `Scene2DPhysicsBridge`：物理的 authoritative 方向与 fixed-step 语义独立（D5）；
- **不**处理 3D：`World2DSnapshot` 明确拒绝携带 3D 组件的 entity，3D 场景序列化本身尚不存在。

## 结果

- authored 的四种 resource 节点第一次在 runtime 有意义，Editor 里画的东西和跑起来的东西一致；
- 每帧顺序与 lease 生命周期从「每个游戏各自手写、错了也不报」变成「一处保证、错了返回 Error」；
- `tina_scene` 依赖不变，组件仍是纯数据；
- 代价与限制：`tina_gameplay2d` 成为最重的模块，链接它即拖入 Asset 与 Box2D；只覆盖 2D；
  `AssetSystem::pump` 仍需调用方自己按帧调用；`tina_runtime` 依然不能直接调用本模块；
- 需要建立的门禁：四种 kind 各自的实例化/shutdown 测试、乱序调用（extract 早于 commit）必须失败、
  `active=false` 不 update/不 extract、audio clip lease 活到终态 completion、shutdown 幂等。

## 被拒绝方案

- **每 kind 一个独立 bridge**：模块更薄，但每帧编排仍留给游戏，而那正是要消除的成本；
- **纯文档 + 参考实现**：与现状等价；
- **运行时句柄放进 Scene 组件**：Scene 立即需要链接 Asset/Audio，`EntityRecord` 为所有 entity 付出
  全部 kind 的体积；
- **让 `tina_scene` 链接 Asset/Audio/Physics2D**：把 Box2D 与 Asset 强加给纯渲染/纯 UI 消费者；
- **把 `AssetSystem::pump` 藏进 runtime**：一个场景对象不应隐式驱动全局资源系统；
- **合并物理桥**：会让 `applyTo` 的 fixed-step 时序语义变模糊。
