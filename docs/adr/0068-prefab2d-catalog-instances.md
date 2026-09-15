# ADR 0068：Prefab2D Catalog 资产与 PrefabInstance2D 实例展开

- 状态：Accepted
- 日期：2026-09-15
- 决策者：Tina maintainers

## 背景

2D 侧此前只有 explode-and-forget 的 `.tworld` 子树模板：没有 Catalog 身份、没有实例链接、改一份拷贝不会更新其它放置。3D 侧 Prefab 早已是 cooked `AssetKind::Prefab`，World3D authoring 直接编辑该 wire。把文件模板当作 Prefab 产品路径，会让“哪条路是对的”变成运行时才知道的事。

v1 明确不做 nested override、variant、或 Prefab 内再放 PrefabInstance2D。

## 决定

### 1. `AssetKind::Prefab2D = 19`

Prefab2D 是 Catalog cooked 资产。type version 1。payload 就是一份 current-schema World2D snapshot（单根、parent 0、空 gameplay）。不另包一层 Prefab2D header：World2D schema 自身已经 versioned；不匹配直接拒绝。

合法 Prefab2D payload：

- 非空、恰好一个 scene root（`parentStableEntityId == 0` 只出现一次且在首位）；
- 根不得是 `CollisionShape2D`；
- 不得含 `PrefabInstance2D`；
- gameplay blob 必须为空。

Cooked 依赖由 payload 内全部非零 AssetId 按 AssetId 排序去重收集（Sprite / Texture2D / Shader / SpriteAnimationClip / TileMap / Fx2D / NavigationGrid2D / AudioClip）。

### 2. `World2DNodeKind::PrefabInstance2D = 16`

additive node kind，沿用 `PayloadResource` / `World2DResourceNodeDesc`。`EntityBytes` 与字段布局不变，World2D schema 保持 v9。`requiredResourceAssetKind = Prefab2D`。

Scene `ResourceBindingKind2D::PrefabInstance` 记录该引用。与 TileMap/Fx/Nav/Audio 不同：**实例展开发生在 `instantiateWorld2DSnapshot`**，这样 Editor preview 与 Play / 产品加载共用一条路径。`Scene2DRuntime` 跳过 PrefabInstance 绑定（展开后的 TileMap/Fx/Audio/Physics 子实体仍按既有 kind 实例化）。

### 3. 展开语义

`World2DSnapshotAssetResolver::resolvePrefab2D` 在场景含 PrefabInstance2D 时必填，返回该资产的 owned entity 列表。缺失、校验失败或 payload 内再含 PrefabInstance2D 一律 fail closed。

展开子实体：

- 运行时创建，parent 0 的 prefab 根挂到 instance 实体下，内部 parent 按 prefab-local stable ID 重映射；
- 不进入返回的 `World2DEntityBinding`，不进入 World2D 文档快照；
- `captureWorld2DSnapshotBytes` 跳过 PrefabInstance 的子孙；
- 拾取/gizmo 把子实体映射回 instance 的 authored stable ID。

PrefabInstance2D 在 authored 树上是叶子：不能在其下再挂 authored 子节点。

### 4. Editor 产品路径

- **Save as Prefab2D**：把选中子树写成 Prefab2D 资产，增量发布进当前 Catalog，并用一个 PrefabInstance2D 替换该子树（根 transform/parent/name 留在 instance 上，payload 根 transform 归 identity）。
- **Place**：Create Node `PrefabInstance2D`，或把 Prefab2D 拖进 2D viewport。
- **Edit**：Project Assets 打开 Prefab2D 为 catalog-backed World2D tab（与 3D Prefab 打开 World3D 同构）；保存把 cooked Prefab2D 写回 Catalog。Prefab 文档内禁止再放 PrefabInstance2D。

explode-and-forget 的 2D `.tworld` 模板命令不再是产品路径。3D 子树文件模板不在本 ADR 范围（3D 资产已经是 Prefab）。

## 结果

- 一份 Prefab2D 资产、多处 PrefabInstance2D；改资产后下一次 instantiate/preview rebuild 更新全部实例。
- 代价：v1 无 per-instance override、无嵌套 Prefab、无变体。World2D 文档不持久化展开几何。
- 门禁：payload 校验、instantiate 展开、capture 不写出子孙、Editor 替换子树为 instance。编译与运行结果独立报告。

## 被拒绝方案

- **继续把 `.tworld` 文件模板当 Prefab**：没有身份，不能更新实例，与 3D Prefab 和 Catalog 纪律冲突。
- **双轨（模板 + Prefab2D）**：违反单轨迁移；旧入口会让产品路径在运行时分叉。
- **只在 Editor preview 展开、Play 另写一套**：两条语义，必然漂移。
- **World2D schema bump 到 v10**：本切片只增加 nodeKind 取值，记录布局未变；无故 bump 会强迫全部现存 `.tworld` 一次性迁移。
- **nested override / variant**：另立 ADR。本切片 fail closed，不为以后留空字段或版本嗅探。
