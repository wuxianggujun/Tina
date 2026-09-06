# 2D 导航

`Tina::Navigation2D` 是当前 backend-neutral 的固定容量 2D 栅格导航模块。它只依赖 `Tina::Core` 与 `Tina::Math`，
不进入 `Scene::World`，也不取得 AssetSystem、RenderDevice、Physics2D 或 TaskSystem 的所有权。产品通常由
`IGameState`（或其稳定 Resources owner）持有导航 Grid 与 Pathfinder。

## 模块边界

```text
resident TileMapInstance
  -> Asset::buildTileMapNavigation2DData()
  -> immutable NavigationGrid2DData
  -> NavigationGrid2D + fixed-capacity dynamic blockers
  |- NavigationPathfinder2D -> NavigationPathSmoother2D -> NavigationPathFollower2D
  |      `- NavigationAgent2D 组合以上三者，由 actual position 产生 desired velocity
  `- NavigationFlowField2D -> 一次反向 Dijkstra，多个追逐者共享查询
```

- `Tina::Navigation2D`：immutable grid、可变栅格、generation blocker、确定性 A*、路径处理、Agent 与 Flow field；
- `Tina::Asset`：从当前 resident `TileMapInstance` 转换导航数据；
- 产品 State：选择 layer/property、配置容量、推进/取消 query，并决定路径如何驱动 gameplay；
- `Scene::World`、Render 与 Physics2D 不隐式拥有或同步导航状态。

`NavigationGrid2DData` 是 Tina-owned runtime 数据，既可从 resident TileMap snapshot 派生，也可通过
`Asset::loadNavigationGrid2DDataFromCooked()` 从已存在的 `AssetKind::NavigationGrid2D` schema v1 加载。
玩法 API 不改变 wire schema，不序列化 Pathfinder/Agent 的内部运行状态。

## 唯一当前 Grid 契约

`NavigationGrid2DData::Create()` 直接校验唯一当前 immutable grid layout：

- 非零 `widthCells` / `heightCells`，单轴最多4096；
- 总 cell 数最多16 Mi；
- `cellSizeMeters` 必须 finite 且大于0；
- row-major `cellFlags` 与 `traversalCosts` 数量都必须精确匹配尺寸；
- `cellFlags` 只允许 `NavigationGrid2DContract::CellBlocked`，任何保留位都失败；
- 每格 `traversalCosts` 是 `[1,16]` 的 `u8` 移动倍率，blocked cell 也必须提供合法值。

创建成功后数据不可变，并由调用方提供的 PMR resource 持有 cell flags 与 traversal costs。Grid 同时发布
全局最小 traversal cost，供 A* 构造 admissible heuristic。

## TileMap 转换

`Asset::buildTileMapNavigation2DData()` 接收：

- 一个 solid tile layer ID；
- 可选 blocker object layer ID；
- 精确匹配的 blocker property key/value；
- 可选、唯一的 `materialFlags -> traversalCost` 规则。

cost rule 对完整 `materialFlags` 做精确相等匹配，不做 bit 包含匹配；`materialFlags == 0` 保留为默认 cost 1，
规则中的 cost 必须位于 `[1,16]`。未匹配、空 tile 的 cost 均为1。solid layer 中，Tileset
`MaterialSolid` 非零的 tile 会阻挡对应 cell。object layer 只处理 **visible、
axis-aligned Rectangle**，且其 property 必须与配置完全相等；隐藏对象和未匹配对象被忽略。矩形按与 cell
实际相交的范围栅格化，地图外部分会裁剪，完全在地图外的矩形不产生 blocked cell。被标记的 Point 或
非法/非有限 Rectangle 会结构化失败。

转换要求引用到的 solid TileMapChunk 已驻留。任何引用 chunk 未驻留、layer kind 错误、geometry/contract
非法或分配失败都会原子返回错误，不发布半份 `NavigationGrid2DData`。返回统计包括 solid tile cell 数、
匹配的 blocker rectangle 数、去重后的 blocked cell 数、cost 大于1的 cell 数与全局最大 traversal cost。
blocked cell 仍属于 grid 数据，因此也计入 cost 统计。

## 动态阻挡与 revision

`NavigationGrid2D::Create()` 在创建期完成：

- 固定容量 generation blocker registry；
- 与 grid cell 数相同的 `u16` blocker reference-count storage。

`addBlocker()`、`updateBlocker()`、`removeBlocker()` 只接受完全位于 grid 内的非空矩形。重叠 blocker 通过
per-cell 引用计数组合；移除一个 blocker 不会错误清除其他 blocker。`NavigationBlockerId` 同时校验
owner/index/generation，stale 或跨 Grid ID 会失败。容量、非法矩形和 stale ID 失败均不改变当前状态。

每次真实 mutation 推进非零 `revision()`；no-op update 不推进。创建成功后，合法 blocker mutation 不再
向 PMR resource 申请新 storage。

## 路径查询

`NavigationPathfinder2D` 默认使用四方向 A*，也可通过每次 query 的
`NavigationPathQueryOptions::diagonalMode` 选择八方向。`Create(cellCapacity)` 一次性分配 node record、
open heap 和最终 path storage；后续成功 query 不扩容。Grid cell 数超过 capacity 或 query option 非法时，
会在发布新 query 前失败并保留上一份完成结果。

对角策略只有三个强类型值：

- `Disabled`：默认四方向；
- `RequireClearAdjacentCells`：允许对角移动，但与该对角相邻的两个正交 cell 必须都可通行，禁止穿墙角；
- `AllowCornerCutting`：只要求目标对角 cell 可通行，显式允许贴角穿过。

路径使用确定性整数 cost：进入目标 cell 的直行成本为 `10 * destination traversalCost`，对角成本为
`14 * destination traversalCost`。`NavigationPathQueryResult::pathCost` 只在 `Reached` 时非零；start 等于
goal 时为0。四方向使用 `Manhattan * 10`，八方向使用 octile distance；二者再乘 Grid 全局最小 traversal
cost，保持 admissible。

确定性规则为：

1. 较小 `f = g + heuristic`；
2. 较小 Manhattan/octile heuristic；
3. 较小 row-major cell index。

同步入口 `findPath()` 会完成整个有界查询。分步入口为 `begin()` + 多次 `advance(expansionBudget)`，可由
产品逐帧推进或在自己的 owner-thread 调度中编排；模块本身不创建 worker/thread。`cancel()` 将 Pending
query 置为吸收态 `Cancelled`。

终态语义：

| State | 含义 |
| --- | --- |
| `Reached` | 找到路径；`path()` 包含 start 和 goal |
| `Unreachable` | open set 耗尽，或 start/goal 当前被阻挡 |
| `Cancelled` | Pending query 被显式取消 |
| `Invalidated` | 分步 query 期间传入了不同 Grid 地址，或同一 Grid revision 已变化 |

越界 start/goal、零 expansion budget、未开始 query 等属于 API error，而不是上述搜索终态。
`path()` 是借用 span，只到下一次 `begin()`、`reset()` 或 Pathfinder 析构有效。Pending query 借用开始时的
**同一个 Grid 对象地址**与 revision；不得在 Pending 期间移动或修改 Grid。

## 世界坐标、可见性与路径平滑

Grid/Data 的 `worldToCell(Math::Vec2)` 与 `cellCenter(cell)` 使用米，cell 正 Y 对应世界正 Y：

- 世界矩形是左/下边界包含、右/上边界不包含的半开区间；
- 先用 double 做减法、除法和范围校验，再转无符号 index；负坐标越界、NaN/Inf 返回 `nullopt`；
- 中心无法用 finite `Math::Vec2` 表示并往返到同一 cell 时返回 `nullopt`，不掩盖大世界浮点精度损失。

`hasNavigationLineOfSight2D()` 检查 cell-center 线段，`hasNavigationWorldLineOfSight2D()` 检查世界坐标线段。
两者遍历跨过的格子并包含动态 blocker；严格模式不允许穿墙角或沿被阻挡格的边界通行。
`Disabled` 仅接受轴对齐线段，`AllowCornerCutting` 必须显式选择。

`NavigationPathSmoother2D::Create({.waypointCapacity = ...}, memory)` 预分配输出、候选与成本前缀存储。
`smooth(grid, cells, options)` 校验完整输入后原子发布 string-pulled 路径：

- 默认严格墙角；四向 A* 的结果也可在空旷区域拉直；
- 默认 `preserveTraversalCost=true`，捷径不能增加原折线的**连续地形加权长度**，避免跨过 A* 绕开的高代价格；该量不是 A* 的整数 `pathCost`，两种度量不等价；
- 错误保留旧输出，输入可直接使用自身 `path()`；重复点去重并保留端点；
- `path()` 借用到成功重算/reset/move/析构；`isCurrent(grid)` 对账地址和 revision；
- 平滑是同步贪心处理，最坏需要二次数量的候选可见性检查，不受 A* `expansionBudget` 分步控制；产品应限制容量/调用频率。

## 路径跟随与 Agent

`NavigationPathFollower2D` 拷贝世界折线，接受**实际**位置与正 finite delta，返回状态、desired velocity、
目标点和 waypoint index。`setSpeed()` 可更新速度，成功路径创建后不再分配 PMR，失败替换保留原路径。
只有最终 waypoint 使用到达容差，中间拐点不因“接近”而跳过；单次速度限制为最多到达当前 waypoint，
超大 delta 不横跨多个拐点。它不自行积分位置、不假定 Physics 接受了上一帧速度，Idle 更新是 `NoActiveGoal`。

`NavigationAgent2D` 组合分步 A*、smoother 与 follower：

```text
setGoal(grid, actualPosition, exactWorldGoal)
  -> Planning --update(..., expansionBudget)--> Following -> Arrived
              |                                  |
              +-> Unreachable                    +-> revision 变化/离开可通行折线 -> Planning
cancel() -> Cancelled
不同 Grid 地址 / 禁止重规划时 revision 变化 -> Invalidated
```

目标保留精确世界坐标；同 cell 直接走向目标，跨 cell 路径先访问起点中心，再沿验证过的中心折线前进。
每次发出 velocity 前验证实际位置到 waypoint 的 LOS。Pending 时实际位置跨 cell 会重启查询。
`replanOnGridChange=true` 默认在 revision 变化后重新规划，包括重试 Unreachable；关闭该选项则失效。
`Cancelled/Invalidated` 到 `setGoal/reset` 都是终态。Grid 与 PMR resource 必须长于使用期。

推荐由 fixed update 读取 Physics 权威位置、调用 Agent、再由 gameplay 将速度交给 controller/physics。
Agent 不写 Scene Transform、不 step Physics、不注册自己的 blocker。它是**点角色导航**，不是 radius
clearance、局部避障或 crowd；需要角色体积时由产品先构造膨胀后的可通行 grid。

## 共享 Flow field

`NavigationFlowField2D::Create({.cellCapacity = ...}, memory)` 预分配每格记录与 indexed heap。
`build(grid, goal, options)` 同步；`begin/advance/cancel/reset` 支持协作式分帧：

- 从 goal 做反向 Dijkstra，predecessor 松弛计费为正向 `predecessor -> current` 的 `10/14 * traversalCost(current)`，不是 predecessor 成本；与相同策略的正向 A* 整数成本一致；
- 堆按 cost、row-major index 决胜，相等成本的下一跳取较小 row-major index；正成本保证下一跳不成环；
- `Idle/Pending/Ready/Cancelled/Invalidated` 状态明确；Pending 不发布半份场，blocked goal 得到 Ready 但零可达格；
- `sample(grid, cell)` 返回可达性、剩余 cost、下一 cell 与中心到中心的单位方向；goal 可达、cost=0、无下一跳；
- sample 每次检查 Grid 地址/revision，动态 blocker 变化后返回 `GridInvalidated`，不交出陈旧方向；
- 多个追逐者共享 Ready field，无需各自跑 A*。方向只是 cell-center 路由，不是任意位置下安全的碰撞/避障速度。

## Physics2D 动态 blocker 同步

`Tina::Asset::PhysicsNavigationSync2D` 是当前唯一的 Physics2D -> Navigation2D 桥。PhysicsWorld 与 NavigationGrid
仍由 gameplay owner 显式持有，桥只接受注册的 `PhysicsBodyId` 和 body-local `PhysicsAabb2D`，不扫描 PhysicsWorld
也不读取 Box2D geometry。每次 owner-thread Physics step 后调用 `synchronize(world, grid)`，桥读取 body transform，
将旋转后的保守 world AABB 栅格化为一个 fixed-capacity generation blocker。disabled 或完全出界 body 临时移除 blocker，
重新入界后恢复；stale body 自动摘除 registration。所有 registration、planner 与 blocker capacity 在 `Create()` 预分配，
稳态同步不使用 PMR/system heap。容量不足、wrong owner/grid、外部 blocker mutation 均 fail closed 并保留上一份发布状态。

产品 2D 的 Cooked Navigation 只包含 TileMap solid cells；crate 等动态 gameplay body 不再由 Navigation bake 重复写入，
而由该桥运行时发布。teardown 必须先 `shutdown(grid)`，再销毁 Physics/Navigation owner。

## 产品 2D 接入

`tina_sample_2d` 使用 collision layer `20` 生成静态 Cooked/live Navigation；gameplay object layer `30` 的
`role=crate` 不再 bake 进静态 grid，而由 `PhysicsNavigationSync2D` 在每次 Physics step 后发布为动态 blocker。
当前8×4产品地图的静态 grid 生成：

- solid tile cells：11；
- bake 期 blocker rectangles：0；
- 去重 blocked cells：11；
- weighted cells：1，maximum traversal cost：5；
- 独立 `NavigationGrid2D` v1 Cooked asset 与 live derive bit-exact；
- 基础/动态/严格/切角 path cells/cost 均为 `5/40`（crate 由 bridge 发布后路径已绕行）；
- `(1,2) -> (5,2)` 加权路径绕过 cost 5 cell：7 cells、cost 60；
- 分步查询展开1个 node 后取消：`Cancelled`；
- Physics bridge：synchronizations/adds/updates/removes 与 registered/published=`1/1`；
- 产品 mutation 后 Grid revision：10，dynamic blocker mutations：2。

这些字段进入 product evidence schema 29 与 sample JSON；它们证明 Cooked Navigation、material cost、对角
策略、Physics 动态 blocker 与取消的产品垂直接线，不替代模块的容量、不可达、stale ID、失效与零稳态分配测试。

## 最小验证

独立安装消费范例见 [sdk_consumer_navigation2d/main.cpp](../tests/sdk_consumer_navigation2d/main.cpp) 与
[CMakeLists](../tests/sdk_consumer_navigation2d/CMakeLists.txt)：只链接 `Tina::Navigation2D`，实际执行世界坐标、
A*、平滑、Flow field、跟随和动态阻挡重规划，不需要 Window/Scene/Physics owner。
带日期的本机测试、安装产物及资源状态见 [2026-09-06 交接](navigation-ai-handoff-2026-09-06.md)。

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_navigation2d_tests tina_sample_2d --parallel 1 -- /nr:false

out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_navigation2d_tests.exe `
  --gtest_filter="*Navigation*" --gtest_color=yes

out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe `
  --frames=300 --frame-delay-ms=0
```

不需要为 Navigation2D 小改动运行全量 UI/Render/backend 测试。只有公开契约或产品里程碑关闭时，才扩大到
安装 SDK consumer、完整 product-2d gate 和跨平台图。

## 当前限制

- 仅矩形 grid 与每格整数 traversal multiplier；没有 navmesh 或 hierarchical pathfinding；
- 没有内建异步 worker、query queue、半径 clearance 或 crowd/avoidance；
- Physics2D 动态 blocker 仅经显式注册的 `PhysicsNavigationSync2D` 桥同步，不扫描 World、不反向生成 collider；
- 独立 Cooked NavigationGrid2D v1 与 Editor bake/overlay 已落地；不提供 gameplay 侧 navigation snapshot 序列化；
- Grid/Pathfinder/Smoother/Follower/Agent/FlowField 是单 owner-thread 可变对象，不提供并发 mutation/query；
- 行为树/黑板/AI FSM 与 3D navmesh 属于独立决策/3D 导航层，不通过扩宽 Navigation2D 实现。
