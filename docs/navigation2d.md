# 2D 导航

`Tina::Navigation2D` 是当前 backend-neutral 的固定容量 2D 栅格导航模块。它只依赖 `Tina::Core`，
不进入 `Scene::World`，也不取得 AssetSystem、RenderDevice、Physics2D 或 TaskSystem 的所有权。产品通常由
`IGameState`（或其稳定 Resources owner）持有导航 Grid 与 Pathfinder。

## 模块边界

```text
resident TileMapInstance
  -> Asset::buildTileMapNavigation2DData()
  -> NavigationGrid2DData schema v1
  -> NavigationGrid2D + fixed-capacity dynamic blockers
  -> NavigationPathfinder2D
       |- synchronous findPath()
       `- cooperative begin()/advance()/cancel()
```

- `Tina::Navigation2D`：schema、可变栅格、generation blocker 与确定性 A*；
- `Tina::Asset`：从当前 resident `TileMapInstance` 转换导航数据；
- 产品 State：选择 layer/property、配置容量、推进/取消 query，并决定路径如何驱动 gameplay；
- `Scene::World`、Render 与 Physics2D 不隐式拥有或同步导航状态。

`NavigationGrid2DData` 当前不是新的 Catalog `AssetKind`。它是从已验证且已驻留的 TileMap snapshot
派生出的 Tina-owned runtime 数据；若未来需要独立 cooked 导航资产，应另行定义唯一现行 wire schema，
而不是把当前内存对象直接序列化。

## Schema v1

`NavigationGrid2DData::Create()` 只接受 `NavigationGrid2DSchema::Version == 1`：

- 非零 `widthCells` / `heightCells`，单轴最多4096；
- 总 cell 数最多16 Mi；
- `cellSizeMeters` 必须 finite 且大于0；
- row-major `cellFlags` 数量必须精确匹配尺寸；
- v1 只允许 `CellBlocked`，任何保留位都失败。

项目仍处开发期，不读取旧 schema，也没有 compatibility 分支。创建成功后数据不可变，并由调用方提供的
PMR resource 持有 cell flags。

## TileMap 转换

`Asset::buildTileMapNavigation2DData()` 接收：

- 一个 solid tile layer ID；
- 可选 blocker object layer ID；
- 精确匹配的 blocker property key/value。

solid layer 中，Tileset `MaterialSolid` 非零的 tile 会阻挡对应 cell。object layer 只处理 **visible、
axis-aligned Rectangle**，且其 property 必须与配置完全相等；隐藏对象和未匹配对象被忽略。矩形按与 cell
实际相交的范围栅格化，地图外部分会裁剪，完全在地图外的矩形不产生 blocked cell。被标记的 Point 或
非法/非有限 Rectangle 会结构化失败。

转换要求引用到的 solid TileMapChunk 已驻留。任何引用 chunk 未驻留、layer kind 错误、geometry/schema
非法或分配失败都会原子返回错误，不发布半份 `NavigationGrid2DData`。返回统计包括 solid tile cell 数、
匹配的 blocker rectangle 数与去重后的 blocked cell 数。

## 动态阻挡与 revision

`NavigationGrid2D::Create()` 在创建期完成：

- 固定容量 generation blocker registry；
- 与 grid cell 数相同的 `u16` blocker reference-count storage。

`addBlocker()`、`updateBlocker()`、`removeBlocker()` 只接受完全位于 grid 内的非空矩形。重叠 blocker 通过
per-cell 引用计数组合；移除一个 blocker 不会错误清除其他 blocker。`NavigationBlockerId` 同时校验
owner/index/generation，stale 或跨 Grid ID 会失败。容量、非法矩形和 stale ID 失败均保持旧状态。

每次真实 mutation 推进非零 `revision()`；no-op update 不推进。创建成功后，合法 blocker mutation 不再
向 PMR resource 申请新 storage。

## 路径查询

`NavigationPathfinder2D` 使用四方向、单位代价 A*。`Create(cellCapacity)` 一次性分配 node record、open heap
和最终 path storage；后续成功 query 不扩容。Grid cell 数超过 capacity 会在发布新 query 前失败，并保留
上一份完成结果。

确定性规则为：

1. 较小 `f = g + heuristic`；
2. 较小 Manhattan heuristic；
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

## 产品 2D 接入

`tina_sample_2d` 使用 collision layer `20` 和 gameplay object layer `30`，以 `role=crate` 选择 visible
Rectangle blocker。当前8×4产品地图生成：

- solid tile cells：11；
- blocker rectangles：1；
- 去重 blocked cells：13；
- `(1,3) -> (5,3)` 基础路径：7 cells；
- 添加动态 blocker `{4,2,1,1}` 后路径：9 cells；
- 分步查询展开1个 node 后取消：`Cancelled`；
- add/remove 后最终 Grid revision：3，live dynamic blocker：0。

这些字段进入 product evidence schema 24 与 sample JSON；它们证明 TileMap 转换、确定性改道和取消的产品
垂直接线，不替代模块的容量、不可达、stale ID、失效与零稳态分配测试。

## 最小验证

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

- 仅矩形 grid、四方向、单位代价；没有 diagonal、weight/cost field、navmesh 或 hierarchical pathfinding；
- 没有内建异步 worker、query queue、crowd/avoidance 或 Physics2D 自动同步；
- 没有独立 cooked Navigation asset、旧 schema migration、editor bake/preview 或 gameplay serialization；
- Grid/Pathfinder 是单 owner-thread 可变对象，不提供并发 mutation/query。
