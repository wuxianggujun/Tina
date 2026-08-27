# ADR 0030：Gameplay 2D 绑定面与 Scene↔Physics2D 桥接

- 状态：Proposed
- 日期：2026-08-27
- 决策者：Tina maintainers

## 背景

Editor 已能 authoring 完整的 2D 场景并存成 `.tworld`，Runtime 也已能加载它
（`Scene::loadWorld2DSceneFromFile`，ADR 0014 的 `IGameState` 生命周期之外）。但「把游戏逻辑接到
authored 场景上」这条路目前是断的。2026-08-27 核验的事实：

1. **authored payload 在 restore 时被静默丢弃。** wire format 的 `World2DEntityDesc` 携带八个
   optional payload（`include/tina/asset_format/World2DSnapshot.hpp:239-247`），但
   `src/scene/World2DSnapshot.cpp` 只读 `sprite`/`camera`/`pointLight`/`shadowOccluder`/
   `spriteAnimation` 五个；`source.physicsBody`、`source.physicsShape`、`source.resource` 与
   `source.name` 的读取点是 **0**。Editor 能创建、能存盘、能画轮廓的 physics 节点，实例化后没有
   任何物理行为，也没有任何错误——这是正确性缺陷，不是待补功能。
2. **`Scene::World` 组件集封闭。** `World::EntityRecord`（`src/scene/World.cpp:30-45`）把 13 个组件
   按值内联并各配一个 `hasX` bool，存在单个 `GenerationPool` 中；没有 `addComponent`、`ComponentId`、
   archetype、query 或 system/scheduler。加组件必须改该 struct 与公共头，且每个 entity 都要为每种
   组件付内存。
3. **`World` 不在任何 PhaseContext 上。** `include/tina/runtime/PhaseContexts.hpp` 没有任何 accessor
   返回 `Scene::World`；游戏自己拥有并驱动 World（`docs/public-api.md:828` 已明确记录「没有公开
   SceneManager、ECS registry 或 Runtime-owned World capability」）。
4. **没有 name 查找。** 名称只存在于 wire format 的 64-byte 槽；`Scene::World` 里没有 name 字段，
   `findEntityByName` 全仓库零命中。唯一的关联机制是 `instantiateWorld2DSnapshot` 一次性返回的
   `World2DEntityBinding{stableEntityId, entity}`。
5. **模块依赖不允许就地桥接。** `tina_scene` 链接 Core/Render/AssetFormat/AssetTypes，不链接
   Physics2D；`tina_physics2d` 只链接 Core。二者互不可见。既有先例是把桥放进第三方模块：
   `TileMapPhysicsSync2D` 住在 `tina_asset`（`include/tina/asset/TileMapPhysicsSync.hpp`），因为
   `tina_asset` 链接 Physics2D 而 Physics2D 不知道 Asset。
6. **`Tina::Physics2D` 不在 `Tina::GameSDK` 里**（`cmake/TinaGameSdkPackage.cmake:18-30`）。做 2D
   物理游戏的消费者必须显式再链一个 target，而 `samples/2d_tilemap_bgfx` 正是这么做的。

ADR 0010 已决定 Box2D/Jolt 分离、不统一物理 API；ADR 0013 对 ECS 的约束是条件式的（「若使用
EnTT，只能是 Scene 私有存储」，当前 `design-freeze.md:33` 记为 Not used）。因此本 ADR 不与既有
Accepted 决策冲突，它填的是从未被决定过的空白：**游戏状态如何挂到 authored 节点上，以及 authored
physics 如何变成真的 physics。**

`design-freeze.md` 的 Runtime Deferred 条目「多 World/editor orchestration」要求先冻结 World owner
与跨 World 语义。本 ADR 不触碰该项：它不引入第二个 World，也不把 World 交给 Runtime 托管。

## 待确认决策

| # | 决策点 | 推荐 | 主要备选与取舍 |
| --- | --- | --- | --- |
| D1 | 组件模型 | `Scene::World` 保持封闭；游戏状态放在游戏自己的存储，用 stable ID / name 关联 | 开放 `addComponent<T>` 类型擦除注册：要重写 World 存储、引入 archetype/query 与并行迭代语义，没有任何 profile 证据支撑，且会把 fat-struct 的确定性布局换成运行期动态布局 |
| D2 | 关联机制 | 在 Scene 侧持久化 authored name 并提供 `World2DSceneIndex`（stableId↔entity↔name 双向查找） | 只保留一次性 binding vector：游戏必须自己建 map，且 name 永久丢失；把 name 放进 `EntityRecord`：每个 entity 付 64 bytes，多数 entity 无名 |
| D3 | 丢弃的 payload | physics/resource **必须**被消费或显式拒绝，不允许静默丢弃 | 保持现状：saved scene 重新加载后丢数据，且 Editor 画的碰撞体轮廓与 runtime 行为不一致 |
| D4 | physics 组件归属 | `PhysicsBody2D`/`PhysicsShape2D` 作为 **数据组件**进入 `Scene::World`（不含运行期 body handle） | 放进游戏侧存储：restore 无处安放 authored 值，桥接必须重新 parse 字节；把 `PhysicsBodyId` 放进组件：Scene 就必须链接 Physics2D |
| D5 | 桥接归属 | 新模块 `tina_gameplay2d`（链接 Scene + Physics2D），沿用 `TileMapPhysicsSync2D` 的 owner-thread sync 形态 | 让 Scene 链接 Physics2D：所有 Scene 消费者被迫拖入 Box2D；放进 `tina_asset`：Asset 与 gameplay 语义无关，且 Asset 已经很大 |
| D6 | transform 写回 | 桥单向 authoritative：physics → `LocalTransform`，每次 `step()` 后由桥写回 | 双向同步：需要定义谁赢与何时，且 teleport 与 CCD 语义会互相打架 |
| D7 | SDK 暴露 | `Tina::Physics2D` 与 `Tina::Gameplay2D` 都进 `Tina::GameSDK` | 保持 Physics2D 在 SDK 外：做 2D 物理游戏的消费者必须知道内部 target 名 |

## 决定（Proposed）

### 1. 组件模型：World 保持封闭

`Scene::World` 不获得开放组件注册。理由是本 ADR 找不到支撑重写的证据，而重写的代价是把
`EntityRecord` 的固定布局、`GenerationPool` 的 owner/generation 校验与现有 extraction 的确定性
迭代顺序全部重新论证。

游戏状态的 sanctioned 位置是**游戏自己的存储**，用稳定标识关联到 entity。这不是权宜之计：它与
「World 由游戏拥有、Runtime 不托管 World」（第 3 条既有事实）一致，也让游戏自由选择 SoA/AoS 与
分配器。引擎的义务是把关联做得可用——这就是第 2 节。

未来若要开放组件注册，必须新增 ADR，并先给出 profile 证据说明 fat struct 是真实瓶颈。

### 2. `World2DSceneIndex`：stable ID / name / entity 三向查找

1. `instantiateWorld2DSnapshot` 保持返回 `World2DEntityBinding` vector（不改签名）。
2. 新增 `Scene::World2DSceneIndex`：从该 vector 与 snapshot 构建，提供
   `entityForStableId(u32)`、`entityForName(std::string_view)`、`stableIdForEntity(EntityId)`、
   `nameForEntity(EntityId)`。名称按 authored bytes 精确匹配，不做大小写折叠或 trim。
3. 重名不是错误（Editor 允许）；`entityForName` 返回 stable ID 最小的那个，并提供
   `entityCountForName` 让游戏检测歧义。查找是 O(log n)，索引构建一次。
4. 索引**不**持有 `World` 引用，也不观察 entity 销毁：它是 instantiate 时刻的快照。游戏销毁 entity
   后索引里的 `EntityId` 会因 generation 失配被 `World` 拒绝，这正是 generation handle 的设计意图。

name 因此必须在 restore 时被保留（第 3 节），但**不进入 `EntityRecord`**：索引自己拥有名称字节，
无名 entity 不付成本。

### 3. 消费全部 authored payload，不静默丢弃

`instantiateWorld2DSnapshot` 与 capture 必须对八个 payload 各自表态：

| payload | 处理 |
| --- | --- |
| `sprite` `camera` `pointLight` `shadowOccluder` `spriteAnimation` | 现状不变，映射到既有 Scene 组件 |
| `name` | 由 `World2DSceneIndex` 拥有（第 2 节），capture 从索引写回 |
| `physicsBody` `physicsShape` | 映射到新的 Scene 数据组件（第 4 节） |
| `resource` | 映射到新的 `ResourceBinding2D` 数据组件：`{AssetId assetId, bool active}`。Scene 不解释该 AssetId 的用途——TileMap/Fx/Navigation/Audio 的实例化仍是游戏或 Asset 层的事——但 restore→capture 必须字节往返 |

「静默丢弃」在本 ADR 后被视为缺陷。若将来新增 payload 而某层暂不支持，该层必须显式返回错误，
不得默认忽略。

### 4. `PhysicsBody2D` / `PhysicsShape2D` 是 Scene 数据组件

1. 两者是纯数据，字段与 `World2DPhysicsBodyDesc`/`World2DPhysicsShapeDesc` 一一对应，**不含**
   `PhysicsBodyId`/`PhysicsShapeId`。因此 `tina_scene` 不需要链接 Physics2D，ADR 0010 的后端分离
   不被破坏。
2. `PhysicsShape2D` 沿用 wire 的 kind 集合（Box/Circle/Capsule）。`PhysicsShapeKind2D` 另有
   `ConvexPolygon`/`Chain`，它们没有 wire 表示，因此不出现在 Scene 组件里；需要它们的游戏直接用
   Physics2D API 建。
3. 组件校验只做 Scene 层能判定的部分（有限值、按 kind 的正尺寸），与
   `applyWorld2DPhysicsShapeNodeProperties` 的 Editor 侧校验同口径。density/friction/restitution
   的物理合理性由 Physics2D 判定。

### 5. `tina_gameplay2d`：单向 physics 桥

新模块 `tina_gameplay2d`（target `Tina::Gameplay2D`），链接 `Tina::Scene` + `Tina::Physics2D`。
首个类型 `Scene2DPhysicsBridge`，形态沿用 `TileMapPhysicsSync2D`：

1. `build(world, physicsWorld, index)`：为每个带 `PhysicsBody2D` 的 entity 建一个 body，把该 entity
   子树中带 `PhysicsShape2D` 的节点建成它的 shape。**父子关系决定归属**——这与 Editor 的
   `CollisionShape2D` 必须有 physics body 父节点的约束一致（`EditorSceneOperations.cpp` 已强制）。
2. body 初始 transform 取 `WorldTransform`；shape 的 local center/angle 直接透传。
3. `step()` 之后调用 `applyTo(world)`：把每个 body 的 position/angle 写回对应 entity 的
   `LocalTransform`，然后由游戏调用 `world.updateWorldTransforms()`。**physics 是 authoritative**，
   桥不读回 transform 去驱动 physics。游戏要 teleport 就走 `enqueueSetTransform`。
4. 桥只持 generation-aware 的 `PhysicsBodyId`/`PhysicsShapeId` 与 `EntityId`，不持 AssetLease、不
   碰 UI/backend。`shutdown(physicsWorld)` 必须在销毁 physics world 前调用（与
   `TileMapPhysicsSync2D` 同契约）。
5. 固定容量：`bodyCapacity`/`shapeCapacity` 在 build 时确定，超出返回 `CapacityExceeded`。

桥**不**驱动 `step()`：fixed-step accumulator 归 Runtime，与 `PhysicsWorld2D::step()` 的既有契约
（「Runtime owns accumulator and catch-up policy」）一致。

### 6. SDK 暴露

`Tina::Physics2D` 与 `Tina::Gameplay2D` 加入 `Tina::GameSDK` 的 interface 链接集与导出集。做 2D
物理游戏不应该需要知道内部 target 名。

## 结果

- authored 场景不再在 restore 时丢数据；Editor 里画的碰撞体与 runtime 行为一致；
- 游戏能按 authored 名称或 stable ID 找到节点，这是把逻辑挂上去的最小充分条件；
- `Scene::World` 的固定布局、generation 校验与 extraction 确定性全部不动；
- `tina_scene` 仍不链接 Physics2D，ADR 0010 的后端分离成立；
- 代价与限制：游戏必须自己维护实体状态存储；`World2DSceneIndex` 是 instantiate 时刻快照，不跟踪
   后续 create/destroy；physics 单向 authoritative，需要 teleport 必须走 command 队列；
   ConvexPolygon/Chain 无 authoring 表示；桥是 owner-thread 单线程；
- 需要建立的门禁：restore/capture 对八个 payload 的往返测试（含 physics 与 name）、
  `World2DSceneIndex` 的重名与 stale-handle 测试、`Scene2DPhysicsBridge` 的父子归属/容量/
  shutdown 顺序测试、SDK consumer 链接 `Tina::Gameplay2D` 的编译门禁。

## 被拒绝方案

- **开放 `addComponent<T>` 组件注册**：需要重写 World 存储并新增 archetype/query/并行迭代语义，
  没有 profile 证据证明当前 fat struct 是瓶颈，且会使 extraction 的确定性顺序需要重新论证；
- **把 name 放进 `EntityRecord`**：每个 entity 付 64 bytes，而多数 entity 无名；
- **让 `tina_scene` 链接 `Tina::Physics2D`**：所有 Scene 消费者（含纯渲染与 UI 场景）被迫拖入
  Box2D，且 Scene 组件里会出现运行期 body handle；
- **把桥放进 `tina_asset`**：Asset 与 gameplay 语义无关，且会让已经很大的 Asset 再长一个方向；
- **physics/transform 双向同步**：必须定义谁赢与何时，teleport 与 CCD 语义互相打架；
- **保持静默丢弃 payload**：saved scene 往返丢数据，且 Editor 与 runtime 行为不一致；
- **由桥驱动 `step()`**：与 `PhysicsWorld2D` 既有的「Runtime 拥有 accumulator」契约冲突。
