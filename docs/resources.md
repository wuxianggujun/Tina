# 资源与生命周期

## 当前实现

FileSystem 提供异步读取和 best-effort 取消，ResourceManagerHub 管理 Texture、Font 和 Audio 资源，AudioEngine 使用 miniaudio。资源管理器拥有缓存对象，ResourceRef 表达客户端引用。

当前资源状态为 Unloaded、Queued、Loading、ReadyCpu、UploadQueued、Ready、Failed、Cancelled。每次加载都有递增 generation，完成回调只有在资源仍存活、generation 匹配且请求未取消时才会提交结果。

异步 completion 由 Application 在固定的 Asset Completion 阶段通过 ResourceManagerHub 每帧泵送一次，默认预算为8个回调。各 ResourceManager 不再重复驱动共享 FileSystem。管理器销毁时会先取消未完成请求，再调用资源的 unload 释放 CPU/GPU 对象。

vNext M10-A0 已独立实现 `Tina::AssetFormat`：`AssetId`/`ContentHash` 是互不兼容的16字节
Core 强类型，`CookedAssetView`/`CookedManifestView` 对 caller-owned bytes 做成功路径零分配、显式
little-endian 解析。该 target 只 PUBLIC 依赖 `Tina::Core`，不链接 xxHash、文件系统、Task、Render、
cgltf 或 Legacy 资源代码。

vNext M10-A1 已实现 `Tina::Asset`/`tina_asset`：在已解析的 `CookedManifestView` 上事务式构造
不可变、owning 的 `CatalogSnapshot`。Snapshot 只在 Create 阶段通过注入的 `std::pmr::memory_resource`
分配，成功后完全拥有 entry/dependency 表；原始 Manifest bytes 销毁后仍可查询。该切片仍不链接
文件系统、Task、Render、xxHash 实现或 Legacy 资源代码，也不实现 Handle/Lease/状态机。


## 已知问题

- 当前解析、CPU Ready 和 GPU Upload 仍在同一个受预算约束的主线程 completion 回调内连续完成，还没有独立上传队列；
- best-effort 取消不能中断已经开始的底层文件读取，只能阻止其结果被提交；
- 资源监听仍逐项查询文件时间，缺少独立预算和指标；
- M10-A0 已实现 Cooked wire schema、稳定 `AssetId` 值类型、版本化 `ContentHash` 字段与只读校验；
  M10-A1 已实现 owning `CatalogSnapshot`、稳定 `AssetId` binary search 与完整 DAG cycle 校验；
  M10-A2a 已实现 Core 私有 XXH3-128 v1 `ContentHash` 计算与 Cooked payload 可选校验；
  M10-A2b 已实现有界 Manifest 文件读与 `loadCatalogSnapshotFromManifestFile`；
  M10-A2c 已实现 owning Cooked object 文件加载与 Catalog 路径解析校验；
  M10-A2d 已实现依赖展开加载序，M10-A2e 已实现失败不发布部分批的同步批量加载；
  M10-A2f 已实现磁盘 Catalog package 的 metadata-only/full 校验；
  M10-A2g 已实现 `openCatalogPackage`（catalogRoot/manifest → Snapshot，可选打开时校验）；
  M10-A2i 已实现 `buildCatalogPackageSummary` 诊断摘要与 CLI `--list-entries`；
  M10-A2j 已实现 `planCatalogLoads` 依赖序加载计划（含确定性相对路径，不触盘）；
  Asset registry/状态机、Handle/Lease、异步 IO/Decode/Upload、增量 Cooker 与产品资产仍未实现。


## 下一阶段契约

将 Decode 与 Upload 拆成两个队列：IO Executor 只做阻塞文件读取，CPU Worker 做 Decode，
主线程 Asset Completion 只提交 CPU 结果，Render Upload 阶段按任务数、字节数和时间三重
预算创建 GPU 资源。状态转换、取消和 generation 校验必须贯穿读取、Decode、Completion
和 Upload，GPU 上传和销毁只发生在明确的 Render Phase。

Asset CPU payload 属于 Asset memory domain 和对应 slot generation；进入 Upload 后由独立
staging allocation 持有，不能借用 Worker scratch 或 FrameArena。完成、取消、失败与迟到
generation 都必须归还 payload/staging 并更新 current/peak/failed 指标。

公共生命周期类型分开：

- `AssetId`：稳定128位逻辑身份，来自 Cooked Manifest，不等于路径或 ContentHash；
- `AssetHandle<T>`：弱 typed generation lookup，不延长 payload 生命周期；
- `AssetLease<T>`：强引用，跨 Task、GPU upload、Audio callback 或 RenderFramePacket 保存数据时
  必须持有；
- `UploadTicket`：拥有已提交 GPU staging，backend completion/fence 后才释放；
- retirement ledger：跟踪 DestroyQueued/Retiring/Released，物理释放前不递减 GPU/Audio
  resource count。

### 首期 Cooked 类型

| 领域 | 类型 | 主要依赖/说明 |
| --- | --- | --- |
| 通用渲染 | `Texture2DAsset`、`ShaderAsset` | 颜色空间/format/mip 与 Tina Shader ABI；backend payload 私有 |
| UI | `FontAsset` | owning bytes、face metadata、确定 fallback chain；Runtime 不按路径开字体 |
| 2D | `SpriteAsset`、`TilesetAsset`、`TileMapAsset` | UV/pivot/PPU、tile flags/layer/chunk/collision metadata |
| 3D | `StaticMeshAsset`、`MaterialAsset`、`PrefabAsset` | Mesh/Submesh/bounds、UnlitBaseColor、节点层级与资产引用 |
| Audio | `AudioClipAsset`/stream metadata | PCM/压缩 payload 与 miniaudio backend 类型分离 |

Scene/Game component 保存上述 `AssetHandle<T>`，不保存 Render Buffer/Texture/Pipeline handle。
World/UI extraction 在当前 ready snapshot 中解析资源，并把 lease、FrameResourceRef 和 Atlas pin
通过 Render SPI 的 `FramePinSink` 登记；Runtime-private owning `RenderFramePacket` 到 backend
completion 前不得释放。Asset/UI 不 include 或访问 packet 类型。游戏代码不能绕过 AssetSystem
直接创建 GPU 资源。

M7–M9 在完整 Cooker 落地前只使用版本化、确定性的内置 Cooked fixture 或 procedural geometry。
Fixture 也必须走同一 wire schema/AssetHandle/RenderFramePacket 接口，不允许恢复 Runtime 路径读取，
M10 起正式产品样例改用 Catalog/Manifest；hermetic、版本锁定的 fixture 继续保留给 infrastructure
sample 和 module test，避免测试依赖外部资产目录或 Cooker 现场状态。

`ShaderAsset` 的逻辑外壳包含 ABI version、interface/layout hash、binding table 和 Tina target；
shaderc profile/二进制只属于 backend payload。`MaterialAsset` 依赖 Shader/Texture/Sampler schema，
Cooker 离线验证 slot 和常量布局，Runtime 不在 draw 热点按字符串查询 uniform。

glTF 首期转换为 StaticMesh/Texture2D/Material/Prefab；2D importer 生成 Sprite/Tileset/TileMap。
Prefab 实例化和 TileMap 运行时边界分别见[3D 游戏架构](game-3d.md)和
[2D 游戏架构](game-2d.md)。

### AssetId、元数据与 Cook key

`AssetId` 在资源首次执行显式 import 时一次性分配，并持久写入项目 Asset Catalog/相邻 metadata；
普通 `cook` 是只读源目录的确定性操作，遇到缺失或重复 AssetId 必须报错，不能在构建中偷偷
生成新身份。移动/重命名源文件时保留 metadata，因此 ID 不变；“复制为新 Asset”必须由 import
分配新 ID，直接复制出重复 metadata 会得到包含两条路径的诊断。Manifest 使用固定小写
32 hex（或等价规范二进制）序列化，解析时拒绝全0和重复 ID。

ContentHash 与 AssetId 分离。Cook cache key 至少覆盖：源内容、规范化 import settings、所有
必需依赖的 Cooked ContentHash、Cooker build/version、目标平台、schema/type version 和 shader
ABI；任一项变化都不能错误命中旧产物。对于同一锁定输入，Cooked bytes 和 Manifest 排序必须
确定，时间戳、绝对路径、机器名和随机数不得进入产物。

GPU Asset 在 Upload 阶段成功后只进入下一帧的 ready snapshot；CPU-only Asset 在 main
completion 后同样按帧边界发布。逻辑取消可阻止 Ready，却不能提前释放已提交 GPU/Audio
数据。首期没有自动 LRU eviction，避免未观测的卸载/重载抖动；只支持显式 unload 和 shutdown。

依赖在调度前验证为 DAG：self/cycle、缺失必需依赖和依赖类型错误返回完整 chain；父 Asset
在所有必需依赖 Ready 前不能 Ready。Retry 使用新 request generation。Fallback 必须由
Asset 类型显式声明，不能把损坏 Mesh/Shader 静默替成另一个资源。

Runtime 与 Cooker 通过独立 `tina_asset_format` 共享 wire schema。Header 至少验证 magic、
schema、asset type/version、target platform、endianness、payload size/alignment、dependency
table 和 ContentHash；不可信 size/count 在分配前检查上限。Cooker 在 staging 目录写入并
重新读取验证所有产物，最后原子替换 Manifest，保证崩溃后旧 Manifest 不指向半成品。

### M10-A0 wire schema v1

v1 只接受 schema `1.0`、little-endian 与 `XXH3-128 v1` 字段标识。M10-A0 的 parser 只保证字段非零、
算法 ID 已知和文件结构可信，不计算 Hash。M10-A2a 在 Core 私有 adapter 中实现 `XXH3-128 v1`
摘要计算，并提供 Cooked payload 可选校验；校验失败返回结构化错误，不能静默接受。未来接受更高
minor 前必须先定义新增字段的兼容规则，当前 parser 不静默跳过未知 schema、enum、flag 或 reserved
数据。

### M10-A2a ContentHash digest 契约

模块边界：

```text
xxHash (PRIVATE, vcpkg)
  -> Tina::Core private adapter  (digest only; no xxHash types in public headers)
  -> Tina::AssetFormat optional verifyCookedAssetContentHash (uses Core digest)
```

公共 API（`Tina::Core`，无第三方类型）：

| API | 职责 |
| --- | --- |
| `ContentHashAlgorithm::Xxh3_128V1` | 与 wire `HashAlgorithm::Xxh3_128V1` 对齐的算法版本标签 |
| `digestContentHash(span, algorithm)` | 对 caller-owned bytes 计算版本化 ContentHash |
| `digestContentHashV1(span)` | 固定 V1 的便捷入口 |

算法契约：

1. V1 使用 XXH3-128、默认 seed=0、无 secret。
2. 输出 16 字节 little-endian：`low64` 在前、`high64` 在后，与 little-endian wire 一致。
3. 全零 digest 视为无效并拒绝发布（与 `ContentHash::fromBytes` 一致；若算法返回全零则失败）。
4. 空 span 合法：对空输入计算确定性摘要（仍须非全零才发布）。
5. 公共头禁止 include `xxhash.h` 或暴露 `XXH*` 类型/宏。
6. 成功热路径不分配；失败返回 `Core::Result`/`Status` 结构化错误。
7. 不承担安全签名；即使 Hash 匹配，Runtime 仍须做 schema/bounds 校验。

Cooked 校验（`Tina::AssetFormat`）：

- `verifyCookedAssetContentHash(CookedAssetView)` 仅在 `hashAlgorithm == Xxh3_128V1` 时，对
  **payload 字节**（不含 header/dependency table）计算 digest，并与 header `contentHash` 比较。
- 算法不匹配、digest 失败、或 Hash 不匹配返回专用错误，不修改 view。
- Manifest entry 的 ContentHash 是产物摘要字段，A2a 不对 Manifest 表本身做整表 re-hash。

非目标：文件 IO、流式文件读、Catalog 磁盘加载、Handle/Lease、Cooker writer、密码学签名、
StringId、bundle 完整性。


Cooked Asset Header 固定112字节：

| Offset | 类型 | 字段 |
| ---: | --- | --- |
| `0x00` | `byte[8]` | magic `TINAASST` |
| `0x08` | `u16/u16/u32` | schema major/minor、header bytes=`112` |
| `0x10` | `u16/u16/u16/u8/u8` | AssetKind、type version、target、endian、hash algorithm |
| `0x18` | `u32/u32` | flags=`0`、reserved=`0` |
| `0x20` | `byte[16]` | AssetId |
| `0x30` | `byte[16]` | ContentHash |
| `0x40` | `u64/u32/u32` | dependency offset/count/entry bytes=`24` |
| `0x50` | `u64/u64/u32/u32/u64` | payload offset/bytes/alignment、reserved、file bytes |

Manifest Header 固定64字节，magic 为 `TINAMNFT`，依次保存 schema、target/endian/hash、零 flags、
entry count/size、dependency count/size、entries offset、dependencies offset 与 file bytes。Manifest
Entry 固定56字节：`AssetId[16] + ContentHash[16] + kind/typeVersion + zero flags +
dependencyFirst/dependencyCount + cookedFileBytes`。Cooked 与 Manifest 共用24字节 Dependency Entry：
`AssetId[16] + expectedKind + flags + zero reserved`；v1 只允许并要求 `Required` bit。

布局是 canonical 的：Cooked 必须为 `112B header -> sorted dependency table -> zero padding -> payload -> EOF`；
Manifest 必须为 `64B header -> AssetId 严格升序 entries -> flattened dependency table -> EOF`。
Manifest 每个 dependency range 必须无 gap/overlap 并完整覆盖 dependency table；子范围严格按 AssetId
排序，目标必须存在且 `expectedKind` 匹配。A0 拒绝 self dependency，但不做完整 DAG cycle 检测。
完整 cycle 校验属于 M10-A1 `CatalogSnapshot::Create`，不能把 A0 描述成已完成依赖调度。

### M10-A1 CatalogSnapshot 契约

模块边界：

```text
Tina::Core
  -> Tina::AssetFormat   (borrowed CookedManifestView / wire types)
  -> Tina::Asset         (owning CatalogSnapshot)
```

`tina_asset` 只 PUBLIC 依赖 `Tina::Core` 与 `Tina::AssetFormat`，PRIVATE 使用 `Tina::ProjectOptions`。
禁止依赖 Runtime、Render、Task、UI、GLFW、bgfx、Legacy、EASTL、xxHash 实现或文件系统。

数据流：

```text
caller-owned Manifest bytes
  -> AssetFormat::parseCookedManifestView (A0, zero-alloc borrow)
  -> Asset::CatalogSnapshot::Create(view, CatalogConfig)
       copy entries/deps into injected PMR
       resolve dependency targets to stable entry index
       iterative DAG cycle validation
  -> immutable CatalogSnapshot (owning)
  -> later AssetSystem (not in A1) consumes Snapshot for load planning
```

公共类型：

| 类型 | 职责 |
| --- | --- |
| `CatalogConfig` | Create 前复制的容量与 `std::pmr::memory_resource*`；禁止默认堆 fallback |
| `CatalogSnapshot` | move-only、不可变、owning 的 Catalog 快照 |
| `CatalogEntry` | 单条资产的 owning 小值（AssetId/ContentHash/kind/typeVersion/cookedFileBytes/dependencyCount） |
| `CatalogDependency` | 单条依赖的 owning 小值（AssetId、resolved `targetEntryIndex`、expectedKind、flags） |

`CatalogSnapshot::Create` 成功后：

1. 完全拥有需要的 Catalog 数据；不保留 Manifest byte buffer 或 wire offset。
2. 原始 Manifest bytes 可立即销毁/修改；Snapshot 仍可 `find`/`entry`/`dependency`。
3. Snapshot 创建完成后不可变；只允许 move 与析构。
4. 只能在 Create 阶段分配；查询路径零分配。
5. 使用 `CatalogConfig::memoryResource`；`nullptr` 或非法容量立即失败。
6. 不使用全局 allocator 或隐藏 heap fallback。
7. 构造失败不得发布部分 Snapshot；输出对象保持默认空状态。
8. allocation failure 回滚已构造的 PMR 对象后返回结构化错误。
9. `find(AssetId)` 对已排序 entry 做 binary search；首期禁止 `unordered_map`。
10. 内部依赖边在 Create 时解析为稳定 entry index，运行期查询不再二次 AssetId 查找。
11. 公共接口不暴露内部 PMR container、裸指针或 AssetFormat wire offset。
12. accessor 返回 owning 小值；若未来提供 borrowed span，失效点为 Snapshot move 或析构。
13. move 后源对象进入可析构、不可查询的空状态（`operator bool() == false`）。
14. 禁止复制大型 Snapshot（copy ctor/assign deleted）。

`CatalogConfig` 容量上限：

- `maxEntries`、`maxDependencies`、`maxDependenciesPerAsset` 必须 `> 0` 时才允许对应非空表；
  空 Manifest（0 entry / 0 dependency）在配置允许且 `memoryResource != nullptr` 时合法。
- 任一上限不得超过 `AssetFormat::Wire` 对应 hard limit（1,000,000 entries / 4,000,000 deps /
  4096 per asset）。
- Create 时若 Manifest 的 entry/dependency 计数超过配置上限，返回 capacity 错误且不发布。

DAG cycle 校验（Create 阶段）：

1. 拒绝 self dependency（A0 已保证；A1 仍作为防御性失败路径）。
2. 拒绝任意长度依赖环。
3. 允许 diamond DAG 与任意合法深链。
4. 禁止递归 DFS；使用显式预分配 color/stack scratch（同注入 PMR）。
5. 目标复杂度 `O(V + E)`：每个 entry 与每条依赖边访问常数次。
6. 专用错误码 `DependencyCycle`；失败消息可记录形成环的 AssetId chain（仅失败路径，不进入成功热路径）。
7. 不重新承担 A0 的 wire bounds/layout/magic/schema 校验；输入必须是已成功解析的
   `CookedManifestView`。

错误码（`Tina::Asset::AssetErrorCode`，`ErrorDomain::Asset`，与 A0 的 1–12 编号分离）：

| Code | 含义 |
| --- | --- |
| `InvalidCatalogConfig` | `memoryResource == nullptr`、上限为0但需要存储、或超过 wire hard limit |
| `CatalogCapacityExceeded` | Manifest 计数超过 `CatalogConfig` 上限 |
| `DependencyCycle` | 发现任意长度依赖环 |
| `AllocationFailed` | 注入 PMR 分配失败（含 `std::bad_alloc`） |

失败回滚：

- Create 内部任何步骤失败时，销毁全部临时 PMR storage 与 scratch，不发布 `CatalogSnapshot`。
- 调用方已有 Snapshot 不会被 `Create` 原地修改；`Create` 返回新对象或错误。
- 析构与 move-assign 必须 `noexcept` 归还全部 PMR 字节。

后续 AssetSystem 消费方式（非本切片）：

- 以 `CatalogSnapshot` 为只读 Catalog 真相来源，按 `AssetId`/`entry index` 规划加载顺序；
- Handle/Lease/registry slot、文件 IO、Task worker、GPU upload 均在后续切片接入；
- 本切片不把 ADR 0016（仍为 Proposed）的 Handle/Lease/retirement 标为已接受。

本切片非目标：`AssetHandle`/`AssetLease`、registry 状态机、File IO、Task/IO thread、Main completion
queue、GPU upload、UploadTicket、retirement ledger、XXH3 计算、cgltf、`tina_assetc`、Cooker writer、
atomic publish、glTF/纹理/字体/shader 转换、2D/3D 正式产品样例、hot reload、Bundle/Patch、自动 LRU、
网络 Asset。

### M10-A2b Catalog 文件加载契约

Core 最小读文件 API（`Tina::Core`）：

| API | 职责 |
| --- | --- |
| `ReadFileConfig` | `maxBytes` + `memoryResource*`；禁止默认堆 fallback |
| `readFile(utf8Path, config)` | 同步读取整个文件到 owning `std::pmr::vector<std::byte>` |

`readFile` 契约：

1. 路径为 UTF-8 `string_view`；空路径、内嵌 NUL 失败。
2. 仅读取常规文件；目录/非文件失败。
3. 先查大小：`size > maxBytes` 或 `size == 0` 且不允许空文件时失败且不分配大缓冲。
4. `maxBytes == 0` 或 `memoryResource == nullptr` → `InvalidArgument`。
5. 默认 `maxBytes` 上限不超过实现 hard cap（建议 256 MiB，与 Manifest wire limit 对齐）。
6. 读失败保留结构化 `CoreErrorCode::Io`/`NotFound`/`PermissionDenied`，可附 native code。
7. 成功返回 owning bytes；调用方负责生命周期。
8. 不实现 async、mmap、目录遍历、write、atomic replace（后续 Cooker）。

Asset Catalog 加载（`Tina::Asset`）：

```text
utf8Path
  -> Core::readFile(maxFileBytes, catalog PMR or dedicated file PMR)
  -> AssetFormat::parseCookedManifestView(bytes, limits)
  -> CatalogSnapshot::Create(view, catalogConfig)
  -> destroy temporary file bytes (Snapshot owns catalog data)
```

| API | 职责 |
| --- | --- |
| `CatalogFileLoadConfig` | `CatalogConfig` + `CookedManifestLimits` + `maxFileBytes` |
| `loadCatalogSnapshotFromManifestFile(path, config)` | 事务式文件→Snapshot |

失败不发布 Snapshot；临时文件缓冲全部释放。可选后续对 Cooked object 再 `readFile` +
`verifyCookedAssetContentHash`，A2b 首期只闭合 Manifest→CatalogSnapshot。

非目标：Handle/Lease、registry、Task/IO thread、GPU upload、Cooker writer、目录扫描、热重载。

### M10-A2c Cooked object 文件加载契约

数据流：

```text
catalogRoot + AssetId
  -> CatalogSnapshot::find
  -> makeCookedArtifactPath(kind, id)   // objects/<kind>/<aa>/<id>.tasset
  -> join root + relative path (no .. escape)
  -> Core::readFile
  -> parseCookedAssetView
  -> optional verifyCookedAssetContentHash
  -> match Catalog entry (id/kind/typeVersion/contentHash/cookedFileBytes)
  -> owning CookedAssetFile (bytes + validated header accessors)
```

| API | 职责 |
| --- | --- |
| `CookedAssetFileLoadConfig` | maxFileBytes、CookedAssetLimits、PMR、是否校验 ContentHash |
| `CookedAssetFile` | move-only owning bytes；accessor 返回 header/payload/dependency 小值 |
| `loadCookedAssetFile(path, config)` | 单文件加载+校验 |
| `loadCookedAssetFromCatalog(root, catalog, id, config)` | Catalog 解析路径后加载并对齐 entry |

失败不发布部分对象；临时缓冲回滚。公共头无第三方/文件系统类型泄漏（实现可用 `std::filesystem`）。

非目标：Handle/Lease、状态机、异步 IO、GPU upload、Cooker、目录枚举。

### M10-A2d/A2e 依赖序与批量同步加载契约

`computeCatalogLoadOrder` 对请求 `AssetId` 做迭代式传递依赖展开，返回 dependencies-first、去重的
Catalog entry index 序；scratch 与返回 vector 都使用调用方注入 PMR。`loadCookedAssetsFromCatalog`
随后按该顺序逐个调用 `loadCookedAssetFromCatalog`，任一失败先销毁已加载对象再返回首个结构化错误，
不发布部分批。两者都不引入 registry、Handle/Lease、Task worker 或异步 IO。

### M10-A2f Catalog package 磁盘校验契约

```text
strict UTF-8 catalogRoot + immutable CatalogSnapshot
  -> for each Catalog entry, derive deterministic object path
  -> metadata-only: regular-file + exact cookedFileBytes
  -> full: bounded read + parse + forced ContentHash + Catalog entry alignment
  -> destroy the loaded object before validating the next entry
```

`CatalogPackageValidationConfig::verifyContent=false` 只查询常规文件与精确大小，不读取或解析内容，也不
要求文件 PMR；`true` 要求 `file.memoryResource`，忽略嵌套的 `file.verifyContentHash=false` 并强制完整
ContentHash 校验，其余 Cooked file limit 继续生效。校验按 Catalog 顺序遇到首个错误即停止，同时最多
持有一个 owning `CookedAssetFile`，成功后不保留资产；entry 相关失败附带 canonical `AssetId` 上下文。

该 API 不扫描目录，因此不把无关额外文件视为错误；路径只来自已验证 kind/AssetId 的确定性相对路径，
Catalog root 必须是非空、无 NUL 的严格 UTF-8。metadata-only 只能发现缺文件、非普通文件和大小不一致，
不能替代 full parse/ContentHash；XXH3 仍不是对抗性安全签名。

非目标：`AssetHandle`/`AssetLease`、registry 状态机、异步 IO、Task worker、GPU upload、目录清理、
Cooker writer、atomic publish、Bundle/Patch 或产品资产样例。

### M10-A2g Catalog package 打开入口

```text
catalogRoot + safe relative manifest path (default manifest.tmnft)
  -> loadCatalogSnapshotFromManifestFile
  -> optional validateCatalogPackageOnDisk
  -> publish CatalogSnapshot  (or destroy on any failure)
```

`openCatalogPackage` 是同步工具/宿主入口：不引入 registry、Handle/Lease、异步 IO 或全局包缓存。
manifest 相对路径禁止绝对路径与 `..` 逃逸。打开失败不发布 Snapshot。

### M10-A2h `tina_catalog_validate` CLI

可执行文件 `tina_catalog_validate` 调用 `openCatalogPackage`：

- `--root <path>` 必填
- `--manifest <relative>` 默认 `manifest.tmnft`
- `--metadata-only`：仅存在性/大小
- `--no-validate`：只打开 Snapshot
- 成功 stdout JSON：`status/entries/dependencies/validated/contentHash`
- 失败 stdout JSON error + exit 1；用法错误 exit 2

非目标：Handle/Lease、registry、async IO、Cooker、目录扫描清理。


默认 hard limits 为：单 Cooked 文件与 payload 最大1 GiB、单资产最多4096个直接依赖、Manifest
最大256 MiB/1,000,000 entries/4,000,000 dependencies、payload alignment 为2次幂且不超过4096。
所有 count multiplication、offset addition 和 alignment 在访问前 checked；padding 非零、region 越界/
重叠、尾随垃圾、全零 ID/Hash、重复/乱序 ID 与未知字段均失败。

Manifest 不保存源路径或任意运行时路径。Cooked object 路径由已验证的 kind 与 AssetId 确定派生：

```text
objects/<4位小写hex kind>/<AssetId前2位>/<32位小写AssetId>.tasset
```

这让逻辑身份、内容 Hash 和物理位置保持分离，也避免把绝对路径、机器名或源目录写入确定性产物。
`CookedAssetView`/`CookedManifestView` 借用完整输入 bytes；输入被修改、释放或移动后 view 立即失效。
M10-A0 不提供文件 IO、owning decoded object、writer、atomic publish、AssetHandle/Lease、worker、GPU
upload、cgltf 或正式2D/3D资产样例。

Cooker 把源目录视为不可信输入边界。外部 URI 先按 UTF-8 规范化并相对当前 Asset/source root
解析，拒绝绝对路径、UNC/device path、远程 HTTP(S)、NUL、`..`/symlink 解析后逃出允许根的
路径；data URI 只有在类型和 decoded size 上限内才允许。输出文件名来自验证后的 AssetId/
类型，不拼接源文件提供的路径。glTF accessor/count/stride/offset、图片尺寸、解压后字节数、
依赖深度和总产物大小都在分配/乘法前检查上限。

XXH3 ContentHash 只发现缓存变化和非对抗性损坏，不能证明恶意包可信；即使 Hash 匹配，
Runtime 仍执行完整边界/schema/type 检查。发布包签名或密码学完整性属于后续独立安全 ADR。

退出顺序为：Scene/UI 停止产生请求 → Asset 停止接收并 requestStop → IO/CPU Task barrier →
Audio 停止 callback 并释放 Asset lease → 丢弃失效 completion/upload → 释放 CPU/GPU Asset →
Render shutdown → Task join → Platform shutdown。Task queue 与内存矩阵分别见
[Task System](task-system.md)和 [性能预算与内存系统](performance-memory.md)。

这里的“释放 Asset”包含等待 UploadTicket、GPU deferred destroy 和 Audio callback ACK；如果
硬 deadline 内无法获得物理完成，Engine 必须 fast-fail，不能继续释放其 backing memory。
