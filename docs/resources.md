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
  -> optional Sprite2DBindingRegistry / Mesh3DBindingRegistry (borrow Store + RenderDevice)
  -> optional TileMapStream chunk residency owner
  -> optional Null UploadTicket coordinator
```

- `Tina::AssetFormat` 定义 wire schema、parser/writer 与 typed payload schema，只 PUBLIC 依赖 Core；
- `Tina::AssetTypes` 承载 header-only weak `AssetHandle` 与 borrowed `AssetBindingResolver`；`Tina::Asset` 负责 Catalog、磁盘文件、Cooker、
  Lease、Task IO 与 upload 账本；
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
| Cooker | recipe、writer、发布前 typed/package validation、staging 后原子发布；TileMap v3 root + `TileMapChunk` v1 会校验 Tileset、deferred chunk dependency、parent/layer/coord/extent/localId；glTF Cooker 支持 multi-mesh、relative-file/bufferView baseColor/metallicRoughness/normal 贴图 cook，以及 Material v2 factors |
| Registry | generation `AssetHandle`、move-only `AssetLease`；fixed-capacity owner-thread Sprite2D/Mesh3D registry 校验 live Handle/dependency，RenderDevice 实例 allocator 事务分配唯一、单调不复用 key |
| 异步加载 | 有界 request queue；IO Task 读取；owner-thread Main completion 解析并发布 |
| GPU 生命周期 | Null `UploadTicket` 状态机；Texture/Mesh backend retirement marker；AssetLease pin 与 retirement ledger |
| 产品路径 | Texture2D/Sprite/SpriteAnimationClip/TileMap root/TileMapChunk streaming 2D、StaticMesh/Material/Prefab 3D、AudioClip 均有 Cooked 产品 consumer |

`AssetHandle.hpp` 被拆为窄 `Tina::AssetTypes` 公共面。2D World 的 `SpriteRenderer2D`、standalone
`ParticleSystem2D`/`Trail2D` 与 3D `MeshRenderer3D` 复制 weak handle，并在 extraction 时显式借用产品 resolver；A2
产品 resolver 薄调用 Asset-owned API surface 的
`Sprite2DBindingRegistry`，后者按当前 Store owner/generation、Sprite/Tileset kind、唯一 required Texture2D cooked dependency
与 live binding 解析 key。Scene 不因此依赖完整 AssetSystem，也不取得 Lease/payload/GPU owner。
Particle/Trail 不缓存解析结果或 resolver：空 FX 不解析，Trail 每次非空 extract 解析一次，Particle 按 live
item 解析。TileMap emit 保存 weak Tileset Handle，不缓存 key/resolver；hidden/off-camera/empty 跳过解析，
非空可见集合每次调用只解析一次，失败清空输出。3D Prefab 先把 AssetId 解析为 weak StaticMesh/Material
handle；Scene extraction 再通过两个 kind-specific resolver 取得 backend key。产品 `AssetStore` 覆盖
World/extraction 生命周期；A6 的 `Mesh3DBindingRegistry` 原子注册 mesh/material GPU bundle，并由 resolver
fail closed 解析。N16.1 已落地 packet-local `FrameResourceRef`/资源表基础设施；Scene item 迁移与统一
retirement owner 仍后置。

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
- `Sprite2DBindingRegistry` 是 fixed-capacity owner-thread mapping；只借用 Store/device，不拥有 GPU texture、
  Lease 或 retirement，Texture2D 释放前必须先成功 unbind；
- `Mesh3DBindingRegistry` 分别维护 StaticMesh/Material 固定容量 mapping；Material v2 writer 要求 required
  Texture2D dependency 按 baseColor/MR/normal role 顺序保持 AssetId 严格递增且唯一，registry 从 Cooked
  payload 派生 factors 并逐 role 校验；它只借用 GPU owner，释放前必须 exact unbind；
- `TileMapStream` 持有 root/tileset lease 与 demanded chunk handle/lease；必须先把 `AssetSystem` 和 stream
  放到最终地址，再创建借用 `stream.map()` 的 collision adapter，且 stream 必须先于 AssetSystem 析构；
- `UploadTicket` 当前只由 Null ledger 完整实现，用于验证 staging 与逻辑状态；
- `AssetRetirementLedger` 按 Logical/UploadStaging/GpuTexture2D/GpuStaticMesh 记录
  `DestroyQueued`、`Retiring`、`Released`；真实 GPU 销毁仍由 RenderDevice 执行。

Handle 不能持久化、不能手工构造，也不能跨 Store 混用。需要跨 Task、Audio callback 或未来 Render
submission 保留 CPU payload 时必须持有 Lease，而不是缓存 `tryGet()` 返回的裸指针。

### Sprite2D binding registry

`Sprite2DBindingRegistry::Create()` 必须在借用 Store 与 RenderDevice 的共享 owner thread 调用，并验证容量
（1..4096），使用调用方 PMR 一次性建立 entry storage；Store、RenderDevice 与非空自定义 memory resource
都是借用且必须覆盖 registry 生命周期。该共享 owner thread 同时成为 registry owner；后续
register/unbind/resolve 不增长 storage，全部在同一线程调用。注册输入是当前 Store 的
Texture2D `AssetHandle` 与同一借用 RenderDevice 的 `GpuTextureId`：registry 完成 handle/capacity preflight
后调用 `createSprite2DTextureBinding()`；device binding 失败时不发布 entry，也不消费候选 key。成功 key
在该 device 实例 namespace 内从1起单调增长，unbind 后不复用；多个 registry 共享 device 时仍获得
distinct key。live exact duplicate 幂等，GPU/同 AssetId 冲突、registry 容量与 device key exhaustion 都
显式失败。普通 `bindingKey()` 对 stale/unloaded Texture2D fail closed 返回0。

direct `setSprite2DTextureBinding()` 的 caller-chosen key 与 allocator-managed key 共用 namespace；device
allocator 不追踪 direct key。registry 管理期间不得混用两种写入方式，否则 direct binding 可能被后续
allocator candidate 覆盖。

Sprite/Tileset resolve 不缓存 Cooked payload 指针：每次验证 live handle 与 expected kind，读取恰好一个
expected kind 为 Texture2D、flags 为 `Required` 的 cooked dependency，再验证 registry 中对应 Texture
handle/payload/binding 后返回 key，否则返回0。只有 unbind 使用原 exact handle，即使 Store handle 已 stale
仍可清理 device；device 清除失败时 entry
保持不变，调用方可重试。registry 析构不替代资源 teardown，产品 State 必须按
`unbind -> destroy/retire Texture2D` 顺序关闭，之后外部 Asset/Render owner 才能停止；State/registry 只借用
这些外部 owner，并不负责销毁它们。

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

可选 `AssetGpuUploadCoordinator` 把 Cooked payload bytes 复制到 `NullUploadLedger`，用于验证预算、ticket、
ReadyGpu 与 unload/retirement 状态机；`retireOnGpuReady=true` 是 Null staging 路径行为。真实 bgfx texture/
mesh 产品上传使用 `RenderDevice` typed upload 和 key binding；handle-based `AssetSystem::retireTexture2D` /
`retireStaticMesh` 会先 acquire `AssetLease`，把 lease 转入 render completion pin，再立即 logical unload 与
移除 AssetId lookup。Texture2D 另提供既有 `AssetLease&` + `GpuTextureId&` overload：只有 backend 接受
retirement 后才消费两个 owner；owner-thread、kind/store/state、PMR payload allocation、ledger 或 backend
失败都保留输入供重试。marker 前 Store 保持 `UnloadPending`，callback 后进入 `Released/Unloaded`。
同步 backend 可在 retirement 调用返回前执行 completion；若它释放最后一个 `UnloadPending` Lease，Store
会当场完成 generation erase，调用方不得再以旧 handle 重复 unload。

RenderDevice 必须覆盖有 live GPU pin 的 AssetSystem 生命周期。`AssetSystem::drainGpuRetirements()` 与析构
执行有界 drain；若 backend 已在普通 present/shutdown 中 exactly-once 释放 pin，AssetSystem 只根据 ledger
清除非 owning device 指针，不再调用 stopped device。失败的 render retirement 不消费 pin、取消 ledger
记录，AssetHandle 仍可用。

## 当前 Typed Cooked Payload

| 领域 | 已有 schema/parser/writer |
| --- | --- |
| 2D | `Texture2D`、`Sprite`、`SpriteAnimationClip`、`Tileset`、`TileMap` v3 root、`TileMapChunk` v1 |
| 3D | `StaticMesh`、`Material`、`Prefab` |
| Audio | `AudioClip` float32 PCM |

SpriteAnimationClip v1 保存 Once/Loop/PingPong、逐帧正有限 duration 与 required Sprite dependency
索引；recipe 使用 `spriteanim <id> <mode> <sprite-id:duration>...`。typed view 会同时校验 payload 与
Cooked dependency contract，产品 sample 再在 Asset/Scene 边界把 Sprite 解析成 Animator 所需的
`SpriteRenderer2D` 帧。

TileMap 当前唯一 root typed payload 为 schema v3：按 authoring 顺序保存 tile/object layers；layer/object ID
必须 map-wide 非零唯一；layer 与 object 都有 visibility，name/properties 使用 strict UTF-8。tile layer 只保存
按 `{chunkY, chunkX}` 严格排序的非空 chunk ref，缺失坐标表示已知空块；object layer 仍保存
point/rectangle。`layerAt()/findLayer()`、`chunkRefAt()/findChunkRef()`、`objectAt()/findObject()` 和 property
lookup 返回 borrowed view。旧 schema v1/v2 均不兼容。

每个非空块是独立 `AssetKind::TileMapChunk` schema v1，保存 parent TileMap `AssetId`、layer ID、chunk
坐标、边缘实际尺寸、非空计数和 row-major cells。parent ID 内嵌在 payload，而不建立 chunk→parent
dependency，避免 parent→deferred chunk 图形成环。TileMap root 对每个 chunk 使用
`Required|Deferred` dependency；普通 root load 只 eager 加载 Tileset 链，chunk 只在被明确 request 时进入
Store。

recipe 必须使用 `tilelayer/objectlayer/property/row/point/rectangle/objectproperty/endlayer/endtilemap`
组成显式 block；历史 `tilemap` 后直接跟裸 `row` 的格式会失败。Cooker 当前用固定16×16切分，只为
非空块生成确定性 chunk asset/ref；recipe name/key/value 当前是不含空白的单 UTF-8 token。package
validation 要求恰好一个 eager `Required` Tileset dependency，所有 root chunk ref 与
`Required|Deferred TileMapChunk` dependency 一一对应，并检查 chunk parent/layer/coord/extent、非空计数
及每个非零 tile localId；任一步失败都不会发布 `manifest.tmnft`。

`TileMapStream` 是固定容量 owner。调用方每帧按
`updateDemand -> AssetSystem::pump -> commitReady` 推进；load/retain margin、request budget 和 resident
capacity 都在 config 中有界。需求移出 retain window 时可取消 Queued/Loading 请求或 detach/unload
Resident chunk。desired load window 单独超过 capacity 时 failure 不改变旧 active set；retain window 只是
optional cache，overflow 时按最近一次成功 demand update 的 recency 自动淘汰，读取 API 不 touch。
`TileMapInstance` 对引用但未驻留的访问返回
`TileMapChunkNotResident`，重新 attach 会分配新的 residency generation，dirty cache 因而不会把旧
resident 数据误当成 cache hit。

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

- owning `RenderFramePacket` 的 present-return CPU completion 不承担 GPU retirement；Texture2D/StaticMesh
  已改走独立 readback marker。通用 GPU submission fence 仍未提供；
- glTF 外部 buffer/texture 的 root containment、URI/type/size 上限与产品接入由 `ASSET-001` 跟踪；
- hot reload、增量 Cooker、通用 Asset cache/LRU、Bundle/Patch 与 network Asset 尚未实现，见
  `ASSET-002`；
- TileMap streaming 已提供固定容量 Camera/layer demand、取消/卸载与 retain-window demand-recency LRU；
  优先级 IO 调度、editor authoring/undo/redo、自动 gameplay 生成、navigation 与旧 schema migration
  仍须独立验收；
- shader/font typed Cooked schema、密码学包签名和通用跨平台 Cooker 仍需独立设计与验收；
- Linux 当前 tip 复验属于 `TEST-001`，现有 Windows 证据不能代替它。

构建与直接 GoogleTest 命令见[构建说明](building.md)和[测试说明](testing.md)；公开契约与第三方隔离见
[公开 API](public-api.md)。
