# Navigation3D（体素体积导航）

`Tina::Navigation3D` 在 Y-up 米制体素世界里做寻路。它是**体素栅格**求解器，不是 navmesh：
适用于方块世界，不适用于任意三角面几何（斜坡、曲面建筑）。navmesh 是独立后续切片，
两者共存时共享的只有 agent 参数词汇。边界与取舍见
[ADR 0048](adr/0048-navigation3d-voxel-volume-boundaries.md)。

只依赖 `Tina::Core` + `Tina::Math`。**与 `Tina::Navigation2D` 互不依赖**，唯一共用的是私有头
`src/navigation/NavigationIndexHeap.hpp`（一个模板化在 records 与 priority 上的索引堆，
不含任何维度概念）。

## 公开面

| 头 | 内容 |
| --- | --- |
| `NavigationErrors3D.hpp` | `ErrorDomain::Navigation3D = 21` 的 9 个错误码 |
| `NavigationVolume3D.hpp` | `NavigationVolume3DData`（不可变 occupancy + 地形代价）、`NavigationVolume3D`（叠加动态 blocker 与 revision）、`NavigationAgentProfile3D` |
| `NavigationPathfinder3D.hpp` | 可复用 owner-thread A*、整数代价、确定性 tie-break、分步预算 |

不占 `MemoryTag`（与 `Navigation2D` 一致），故 `MemoryTagCount` 保持 17。

## 可站立性：这是与 2D 的真正差别

2D 只问「这格通不通」。3D 要问「这个身体能不能站在这」，判据由 `NavigationAgentProfile3D` 派生
而**不烘进体积** —— 一个世界要服务多种体型，烘进去就得为每种怪物存一份数据。

```
isStandable(cell, profile) =
      cell 及其上方 heightCells-1 格全部非实心   // 头顶净空
  且  cell 下方一格实心                          // 有支撑
```

`hasClearance()` 只做上半句，供台阶/坠落的落点在检查自身支撑前使用。

**越界格一律非实心**，这一条决定了两个方向各自正确的后果：

- 上方越界 → 有净空。所以顶层可站立，2 格高的 agent 站在最高一层不会因为「头在体积外」被判不可站；
- `y=0` 下方越界 → **无支撑**。所以**没有隐式地板**：一个全空体积是彻底不可站立的，
  而不是伪装成一整层可走平面。配置错误会立刻显形，不会被藏成「寻路结果有点怪」。

`heightCells` 的边界值都可用：1 格（爬行/低飞生物）合法，0 不合法 —— 一个不占格的 agent
不可能被任何东西阻挡。

## 连通性

同层水平移动走四向或八向，拐角策略与 2D 同源，但**对角要求两侧正交列都够 agent 全高**
（`RequireClearAdjacentColumns`）。只查脚下那格是 2D 的做法，在 3D 会让 agent 的躯干切过墙角 ——
这条已用反向验证确认：把校验换成脚下单格后，测试立刻抓到 agent 从角上穿了过去（路径 3 格变 2 格）。

垂直移动**仅四向**：诚实的对角台阶要校验 2×2×k 的体积净空，写错就是穿墙角。

| 移动 | 约束 | 代价 |
| --- | --- | --- |
| 同层基本方向 | 落点可站立 | `Cardinal = 10` |
| 同层对角 | 落点可站立 + 两侧正交列够全高 | `Diagonal = 14` |
| 台阶上行 | `rise <= maxStepUpCells`，且自身列要清空到能升上去 | `+ StepUpPerCell(10) * rise` |
| 坠落下行 | `drop <= maxFallCells`，且落点列一路净空 | `+ FallPerCell(4) * drop` |

`0 < FallPerCell < StepUpPerCell` 是**契约**（有重力的世界里下降比上升便宜；坠落免费会让规划器
在等长路径里无故往下跳），具体数值是可调项。

最终代价再乘落点的地形 multiplier（`[1,16]`）。

**启发式只按较便宜的那个费率计垂直距离。** 用上行费率会在「靠坠落到达目标高度」的路线上高估，
而高估的启发式不再 admissible，A* 就不再返回最小代价路径。

## 用法

```cpp
using namespace Tina::Navigation3D;

// 1) 从你的体素世界烘出 occupancy 与地形代价（row-major: x + z*width + y*width*depth）
auto data = NavigationVolume3DData::Create({
    .widthCells = 128, .heightCells = 64, .depthCells = 128,
    .originXMeters = 0.0F, .originYMeters = 0.0F, .originZMeters = 0.0F,
    .cellSizeMeters = 1.0F,
    .cellFlags = flags, .traversalCosts = costs,
});
auto volume = NavigationVolume3D::Create(std::move(*data), {.dynamicBlockerCapacity = 256});

// 2) pathfinder 在 Create 时一次性分配全部持久存储
auto pathfinder = NavigationPathfinder3D::Create({.cellCapacity = volume->cellCount()});

// 3) 查询。agent profile 描述这个身体
const NavigationAgentProfile3D walker{
    .heightCells = 2,      // 1.8m 生物在 1m 体素里占 2 格
    .maxStepUpCells = 1,
    .maxFallCells = 3,
};
auto result = pathfinder->findPath(*volume, startCell, goalCell,
                                  {.diagonalMode = NavigationDiagonalMode3D::RequireClearAdjacentColumns,
                                   .agent = walker});
if (result && result->state == NavigationPathQueryState3D::Reached) {
    for (NavigationCell3D cell : pathfinder->path()) { /* ... */ }
}
```

分步预算用 `begin()` + `advance(volume, budget)`，把一次查询摊到多帧。

## 运行时改地形

放/挖方块用 `addBlocker` / `updateBlocker` / `removeBlocker`。**blocker 让格子变实心而不只是
不可通行**：体素世界里运行时变的就是方块，而方块既阻挡它占的格、又支撑它上面的格。
所以放一个 blocker 会**新增**它顶上的可站立格 —— 这是正确行为，也是每次 mutation 必须递增
`revision()` 的原因。重叠 blocker 用引用计数，移除一个不会清掉另一个仍覆盖的格。

在飞的查询遇到 revision 变化会终止为 `Invalidated`（而不是给一个过期答案，那会把 agent 送进
刚出现的墙里）；调用方重新 `findPath()` 即可。

## 错误语义

`NotStandable` 与 `InvalidCell` **刻意分开**：前者是坐标在体积内、但这个身体放不进去
（最常见成因是调用方采样到了半空中），后者是坐标越界。混成一个会让「玩家正盯着的目标」
报成「越界」。`InvalidAgentProfile` 覆盖参数越界与「agent 比体积还高」两种情形。

起点/终点不可站立是**错误**而非确定性的 `Unreachable`；`Unreachable` 保留给「两端都能站，
但没有路」。

## 当前边界

| 不支持 | 说明 |
| --- | --- |
| 任意三角面几何 | 斜坡、曲面建筑要等 navmesh 切片 |
| 多格占地 agent | 2×2 的 agent 斜着走时占哪几格需要独立设计，猜错就是穿角。故**不提供** `footprintCells` 字段，而不是提供了再忽略 |
| crowd / 避让 / radius clearance | 点角色导航不等于这些 |
| 引擎侧 Agent / Follower | 3D 的移动接受与否由重力和碰撞决定，引擎自己积分位置会和玩法的碰撞逻辑对打。实际位置以玩法/Physics 为准 |
| flow field | 2D 有，3D 本轮未做 |
| cooked payload | 体积目前由调用方从自己的世界数据构造，没有 `AssetKind` |

## 验证

`tina_navigation3d_tests` **38/38**（Debug，直接运行 exe，不经 CTest），含 3 个 header-isolation TU。
覆盖面按契约组织而非按函数：索引布局三轴独立断言、可站立性的净空与支撑两半、
无隐式地板、agent profile 边界值可用、blocker 引用计数与 stale id、逐次分配失败注入
（每个工厂都返回 `AllocationFailed` 且不泄漏）、稳态零分配、分步与单次结果一致、
mutation 中途失效后重规划。

**两条不变量已用反向验证确认会失败：** 把对角校验换成脚下单格 → 抓到穿角；
把 `y=0` 判为有支撑（隐式地板）→ 抓到全空体积变成可走平面。

同轮 `tina_navigation2d_tests` 39/39 无回归（索引堆移出 `navigation2d` 后）。
**尚无产品消费面与 GPU/FPS 证据。**
