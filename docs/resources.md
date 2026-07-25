# 资源与生命周期

当前资源主线由 `Tina::AssetFormat` 与 `Tina::Asset` 组成。Runtime 消费 versioned Cooked object 和
`manifest.tmnft`，不解析源 glTF、recipe、图片或音频源文件；不存在 Legacy `ResourceManagerHub` 或
`Application` completion 入口。

## 模块边界

```text
authoring source / recipe / glTF
  -> tina_assetc 或 Asset Cooker API
  -> CatalogCookRequest
  -> atomic package publish
       manifest.tmnft
       objects/<kind>/<prefix>/<asset-id>.tasset

Catalog package
  -> AssetFormat borrowed wire views
  -> Asset owning CatalogSnapshot / CookedAssetFile
  -> load plan + AssetSystem
  -> AssetStore Handle/Lease state
  -> optional Null UploadTicket coordinator
```

- `Tina::AssetFormat` 定义 wire schema、parser/writer 与 typed payload schema，只 PUBLIC 依赖 Core；
- `Tina::Asset` 负责 Catalog、磁盘文件、Cooker、Handle/Lease、Task IO 与 upload 账本；
- xxHash 与 cgltf 都是私有实现依赖，公开头不泄漏第三方 token；
- Render backend 只接收解析后的 payload或资源 key，不读取 Catalog/source tree；
- Scene、UI 与 gameplay 不直接拥有 bgfx Buffer/Texture/Pipeline handle。

## 当前实现

| 能力 | 现状 |
| --- | --- |
| Wire format | Cooked Asset/Manifest v1、little-endian、严格 magic/schema/enum/count/offset/alignment/padding 校验 |
| 身份与摘要 | 128-bit `AssetId` 与 `ContentHash` 强类型分离；XXH3-128 v1 校验 payload；非密码学签名 |
| Catalog | owning immutable `CatalogSnapshot`、AssetId binary search、依赖解析、完整 DAG cycle 校验 |
| Package | 确定性 object path、metadata/full 校验、load plan、依赖序批量加载、失败不发布部分批 |
| Cooker | recipe、writer、发布前 typed/package validation、staging 后原子发布；TileMap v2 会校验 required Tileset dependency 与 tile localId；glTF Cooker 支持 multi-mesh、relative-file/bufferView baseColor/metallicRoughness/normal 贴图 cook，以及 Material v2 factors |
| Registry | generation `AssetHandle`、move-only `AssetLease`、显式容量与注入 PMR |
| 异步加载 | 有界 request queue；IO Task 读取；owner-thread Main completion 解析并发布 |
| GPU 逻辑状态 | Null `UploadTicket`、ReadyCpu→UploadQueued→ReadyGpu、取消与 retirement ledger |
| 产品路径 | Texture2D/Sprite/SpriteAnimationClip/TileMap 2D、StaticMesh/Material/Prefab 3D、AudioClip 均有 Cooked 产品 consumer |

历史 M10/M11 子编号不再在这里维护。完成能力以源码、target、测试和本表为准；未完成工作统一进入
[Backlog](backlog.md)。

## 身份、视图与所有权

`AssetId` 是 Catalog 内的逻辑身份，不等于文件路径或 `ContentHash`。recipe 使用显式 canonical ID；
glTF Cooker 接受显式首 mesh/material/prefab ID，未提供时按输入路径确定性派生。当前实现没有完整的
import metadata 数据库，因此不能宣称源文件重命名后 ID 自动保持不变。

`ContentHash` 用于确定性产物校验与非对抗性损坏检测。Hash 匹配后仍必须执行 wire bounds、schema、
kind/type 与 Catalog entry 对齐检查；它不替代包签名或信任策略。

生命周期类型：

- `CookedAssetView` / `CookedManifestView` 借用 caller-owned bytes；bytes 被释放、移动或修改后立即失效；
- `CatalogSnapshot` 与 `CookedAssetFile` 是 owning、move-only 对象；
- `AssetHandle` 是弱 generation lookup，不延长 CPU payload 生命周期；
- `AssetLease` 是 move-only 强引用，Lease 存在时逻辑 unload 进入 `UnloadPending`；
- `UploadTicket` 当前只由 Null ledger 完整实现，用于验证 staging 与逻辑状态；
- `AssetRetirementLedger` 记录 `DestroyQueued`、`Retiring`、`Released`，自身不释放真实 GPU 资源。

Handle 不能持久化、不能手工构造，也不能跨 Store 混用。需要跨 Task、Audio callback 或未来 Render
submission 保留 CPU payload 时必须持有 Lease，而不是缓存 `tryGet()` 返回的裸指针。

## AssetSystem 状态流

```text
Queued -> Loading -> ReadyCpu -> UploadQueued -> ReadyGpu
                     |              |
                     +-----------> Failed

ReadyCpu / UploadQueued / ReadyGpu -- unload with live lease --> UnloadPending
UnloadPending -- last lease released --> generation erased / stale Handle
```

`AssetSystem::request()` 先按 Catalog DAG 生成 dependencies-first 计划并去重。配置 Task System 时，
`pump()` 将文件读取派发到 IO domain，完成后通过 Main queue 回到 owner thread做 parse/validate/publish；
未配置 Task System 时走同步 IO。当前没有独立 CPU decode worker stage，不能把 `scheduleCpu()` 写成现状。

同步 `load()`、异步 `request()/pump()`、状态转换、Store publish/unload 都是 owner-thread API。协作取消会
丢弃迟到结果，但不能强制终止已经进入系统调用的文件线程。

可选 `AssetGpuUploadCoordinator` 目前把 Cooked payload bytes 复制到 `NullUploadLedger`，用于验证预算、
ticket、ReadyGpu 与 unload/retirement 状态机；`retireOnGpuReady=true` 是 Null 路径行为。真实 bgfx texture/
mesh 产品上传使用 `RenderDevice` typed upload 和 key binding，尚未与通用 AssetSystem completion 合并。

## 当前 Typed Cooked Payload

| 领域 | 已有 schema/parser/writer |
| --- | --- |
| 2D | `Texture2D`、`Sprite`、`SpriteAnimationClip`、`Tileset`、`TileMap` |
| 3D | `StaticMesh`、`Material`、`Prefab` |
| Audio | `AudioClip` float32 PCM |

SpriteAnimationClip v1 保存 Once/Loop/PingPong、逐帧正有限 duration 与 required Sprite dependency
索引；recipe 使用 `spriteanim <id> <mode> <sprite-id:duration>...`。typed view 会同时校验 payload 与
Cooked dependency contract，产品 sample 再在 Asset/Scene 边界把 Sprite 解析成 Animator 所需的
`SpriteRenderer2D` 帧。

TileMap 当前唯一 typed payload 为 schema v2：按 authoring 顺序保存 tile/object layers；layer/object ID
必须 map-wide 非零唯一；layer 与 object 都有 visibility，name/properties 使用 strict UTF-8；tile layer 保存
与地图尺寸完全匹配的 row-major localId grid；object layer 保存 point/rectangle。`layerAt()/findLayer()`、
`objectAt()/findObject()` 和 property lookup 返回 borrowed view。旧 schema v1 不兼容，也不存在单层双读。

recipe 必须使用 `tilelayer/objectlayer/property/row/point/rectangle/objectproperty/endlayer/endtilemap`
组成显式 block；历史 `tilemap` 后直接跟裸 `row` 的格式会失败。Cooker 在构造并发布 Manifest 前重新解析
TileMap v2；recipe name/key/value 当前是不含空白的单 UTF-8 token。package validation 要求恰好一个
`Required` Tileset dependency、依赖存在且 kind/version 正确，并检查每个非零 tile localId 都存在于该
Tileset；任一步失败都不会发布 `manifest.tmnft`。

`AssetKind` 还包含 Shader 与 Font 枚举值，但当前没有对应的公开 typed payload header、完整 Cooker 与
产品消费闭环，不能把它们列为已完成资源类型。FreeType 字体仍通过显式 `TINA_UI_FONT_PATH`/fixture
接入，详见 [UI](ui.md)。

StaticMesh v1 为 P3N3UV2 + UInt16 index；Material v2（40B）为 Opaque `UnlitBaseColor`，携带
`baseColor` RGBA、`metallicFactor`/`roughnessFactor`，以及可选 Texture2D dependency 标志
（baseColor / metallicRoughness / normal，AssetId 在 Cooked deps 中按 flag 顺序）；Prefab 保存 node
hierarchy 与 Mesh/Material AssetId。当前 Opaque3D 是 experimental MR hybrid，已采样 baseColor/MR/normal、
应用 material factors 和唯一有界0..4 directional-light 提交；这不是完整 PBR，IBL/shadow、light
component/culling 与通用 pass scheduler 仍属 `RENDER-001`。glTF importer 的实际限制见
[3D 产品架构](game-3d.md)。

## 文件与安全边界

- Catalog root、manifest relative path 和派生 object path 必须是有效 UTF-8，拒绝 NUL、绝对 manifest
  path 与 `..` 逃逸；
- 所有 size/count/offset multiplication 在分配和访问前检查 hard limit 与溢出；
- Manifest entry 按 AssetId 严格升序，依赖范围必须完整、无 gap/overlap，依赖 kind 必须匹配；
- full package validation 每次最多持有一个 Cooked file，并强制 parse、ContentHash 与 Catalog 对齐；
- publish 先在 staging 写入并重新验证，最后原子替换 Manifest，失败不发布半个 Catalog；
- glTF Cooker 已能读取 relative-file/bufferView image，但 relative URI 的 root containment、规范化与
  symlink 逃逸策略尚未闭合；Runtime 仍不得直接打开任意 URI。

## 当前限制与下一步

- owning `RenderFramePacket`、FramePin 与 present-return CPU submission completion 已落地；它不承担
  GPU retirement，真实 backend fence 驱动的 Asset asynchronous completion 仍后置；
  由 `RUNTIME-002` 跟踪；
- glTF 外部 buffer/texture 的 root containment、URI/type/size 上限与产品接入由 `ASSET-001` 跟踪；
- hot reload、增量 Cooker、自动 LRU、Bundle/Patch 与 network Asset 尚未实现，见 `ASSET-002`；
- TileMap schema v2 不包含 chunk streaming、editor authoring/undo/redo、自动 gameplay 生成、navigation
  或旧 schema migration；这些必须作为独立切片验收；
- shader/font typed Cooked schema、密码学包签名和通用跨平台 Cooker 仍需独立设计与验收；
- Linux 当前 tip 复验属于 `TEST-001`，现有 Windows 证据不能代替它。

构建与直接 GoogleTest 命令见[构建说明](building.md)和[测试说明](testing.md)；公开契约与第三方隔离见
[公开 API](public-api.md)。
