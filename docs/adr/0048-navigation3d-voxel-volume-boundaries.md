# ADR 0048：`Tina::Navigation3D` 体素体积导航边界

- 状态：Proposed
- 日期：2026-09-06
- 决策者：Tina maintainers

## 背景

本 ADR 之前，仓库内没有任何 3D 寻路能力：`include/` 与 `src/` 对 `navigation3d`、`navmesh`、
`Recast`、`Detour` 全部零命中，`Tina::Navigation2D` 是唯一的导航模块，而它的 `NavigationCell2D`
只有 `x`/`y` 两个分量，`NavigationGrid2DData` 只存一层 row-major flags。

现有 3D 场景里唯一存在可行走几何的是 `samples/3d_voxel`：`VoxelWorld` 是 128×64×128 的 dense
block grid（`samples/3d_voxel/core/VoxelWorld.hpp:38-40`），按 16³ 分 chunk，`solidAt()` 是它
唯一的实心判据；玩家侧已有重力、跳跃与 AABB 碰撞（`DesktopMain.cpp:1660-1760`），并且方块在
运行时可增删（`setBlock` 在 `:1616` 与 `:1653` 各一处）。该 sample 目前对
`navigation|pathfind|agent|ai` 的命中数为 **0**。

`samples/3d_product` 是静态展示，没有角色移动；`Tina::Physics3D` 不存在。因此"3D 里能走路的
地方"当前只有体素世界这一处，这决定了首个切片的形状。

[导航续接记录](../navigation-ai-handoff-2026-09-06.md) 曾建议 Recast/Detour 私有内核。本 ADR
不否定该方向，但把它划为独立后续切片：见 D1。

## 决策记录

| # | 决策点 | 采纳 | 主要备选与取舍 |
| --- | --- | --- | --- |
| D1 | 首个 3D 求解器形态 | **体素 occupancy 体积**，navmesh 另立 ADR | 直接上 Recast/Detour：需要第三方闭包、烘焙管线、版本化 cooked payload 与 `dtTileCache` 才能应对运行时改地形，而当前唯一的 3D 可行走场景本身就是体素 —— 把体素栅格化成三角面再让 Recast 重新体素化回去，是绕一圈回到起点。两者共存时共享的只有 agent 参数词汇，求解器各自独立 |
| D2 | 是否复用 `Navigation2D` 的类型 | **不复用，新模块** | 给 `NavigationCell2D` 加 `z`：会改动一个已发布并已有安装 consumer 的公开契约，且 2D 的邻接规则里没有"支撑""头顶净空""台阶/坠落"的位置。两个模块各自只依赖 `Core + Math` |
| D3 | 可站立性由谁决定 | **按 agent profile 派生，不烘进体积** | 烘进体积：体积会变成 agent 专属，一个世界里的两种体型就要两份数据。代价是每次邻居查询多 `heightCells` 次字节读取，height=2 时可忽略 |
| D4 | 越界格视为什么 | **一律非实心** | 视为实心：会让体积顶层的方块永远站不上去（顶上本来就是天空）。选"非实心"后两个谓词各自得到正确后果 —— 上方越界 → 有净空；`y=0` 下方越界 → **无支撑**。故不存在隐式地板，一个全空体积不会伪装成一整层可走平面，而是彻底不可站立 |
| D5 | agent 占地面积 | **固定一格**，多格明确不在范围 | 加 `footprintCells` 字段却按一格处理：多格占地与对角移动的交互需要自己的设计（2×2 的 agent 斜着走时占哪几格），猜错就是穿角。不加这个字段，而不是加了再忽略 |
| D6 | 垂直移动是否允许对角 | **仅四向基本方向** | 允许对角台阶：物理上诚实的话需要校验 2×2×k 的体积净空，做错就是 agent 穿墙角 —— 正是 2D 拐角规则要防的那类缺陷 |
| D7 | 对角净空校验到什么高度 | **两侧正交列都要够 agent 全高** | 只查脚下那格（2D 的做法）：2D 没有高度概念所以够用，3D 里只查脚格会让 agent 的躯干切过墙角 |
| D8 | 动态覆盖层改"可通行"还是"实心" | **改实心** | 只改可通行：在体素世界里运行时变化的就是**方块**，而方块既阻挡又提供支撑。分成两个概念等于对同一个体素持有两份真相。选实心后，放一个方块会**新增**它顶上的可站立格，这是正确行为，也是 revision 必须递增的原因 |
| D9 | 垂直代价 | `StepUpPerCell = 10`、`FallPerCell = 4` | 坠落代价取 0：等长路径下规划器会无缘无故往下跳。契约是**次序** `0 < Fall < StepUp`（有重力的游戏里下降总比上升便宜），具体数值是可调项 |
| D10 | 本切片是否含 Agent/Follower | **不含** | 提供引擎侧 Agent：3D 的移动接受与否由重力和碰撞决定，一个自己积分位置的 Agent 会和 sample 已有的碰撞逻辑对打。按续接记录的口径，实际位置以玩法/Physics 为准 |
| D11 | 索引布局 | `index = x + z * width + y * width * depth` | 让"上一格"是常数步长 `width * depth`，而可站立判据要连读 `heightCells` 个上方格 |
| D12 | 索引堆归属 | 移到 `src/navigation/NavigationIndexHeap.hpp`，两个模块共享 | 复制 70 行到 `navigation3d`：那个堆模板化在 Records 与 Priority 上，**零 2D 概念**，名字里的 `2D` 本来就是错的。跨模块 include `src/navigation2d/` 私有头会造出"Navigation3D 依赖 Navigation2D 私有实现"的错误模块关系 |

## 决定

新增 `Tina::Navigation3D`：只依赖 `Core + Math` 的 Y-up 米制体素导航模块。
`NavigationVolume3DData` 是不可变 occupancy + 地形代价，`NavigationVolume3D` 叠加固定容量
generation 动态实心块并维护 revision；可站立性由 `NavigationAgentProfile3D`
（`heightCells` / `maxStepUpCells` / `maxFallCells`）在查询时派生，判据为
**自身与上方 `heightCells-1` 格净空，且下方一格实心**。连通性在同层走四向或八向（拐角策略与
2D 同源，但对角要求两侧正交列够 agent 全高），台阶与坠落只走四向并各自受上限约束。
`NavigationPathfinder3D` 是可复用的 owner-thread A*，整数代价、确定性 tie-break、分步预算，
状态机与 `NavigationPathfinder2D` 逐项对应。

占用 `ErrorDomain::Navigation3D = 21`。**不占 `MemoryTag`**（与 `Navigation2D` 一致，故
`MemoryTagCount` 保持 17）。

## 结果

- 体素世界拿到真实寻路：`samples/3d_voxel` 成为 tests 之外的首个消费者，挖掉一格方块会经
  revision 使在飞的查询失效并触发重规划；
- `src/navigation/NavigationIndexHeap.hpp` 成为两个导航模块共享的私有堆，`Tina::Navigation::Detail`
  命名空间只收录与维度无关的导航内部件；
- 明确不提供：任意三角面几何（斜坡、曲面建筑）、多格占地 agent、crowd/避让、
  引擎侧 Agent/Follower、3D navmesh。前者要等 navmesh 切片，后者归玩法层；
- 需要建立的门禁：`tina_navigation3d_tests` 单测与 header isolation、逐次分配失败注入、
  安装版 `Tina::Navigation3D` 的外部 consumer、以及体素 sample 里的结构化寻路证据。

## 被拒绝方案

- **把 2D 栅格改名当 3D 用**：续接记录已明确点名这条不可接受。2D 没有支撑、净空、台阶与坠落，
  改名不会产生这些概念。
- **隐式地板（`y=0` 视为有支撑）**：见 D4。会让一个全空体积看起来像可走平面，把配置错误伪装成
  正常数据。
- **把可站立性烘进体积**：见 D3。体积会变成 agent 专属。
- **先做 Recast/Detour**：见 D1。当前唯一的 3D 可行走场景是体素，把它三角化再让 Recast 重新
  体素化回去是白绕一圈；navmesh 的价值在网格场景，按独立切片推进。
