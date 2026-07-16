# 资源与生命周期

## 当前实现

FileSystem 提供异步读取和 best-effort 取消，ResourceManagerHub 管理 Texture、Font 和 Audio 资源，AudioEngine 使用 miniaudio。资源管理器拥有缓存对象，ResourceRef 表达客户端引用。

当前资源状态为 Unloaded、Queued、Loading、ReadyCpu、UploadQueued、Ready、Failed、Cancelled。每次加载都有递增 generation，完成回调只有在资源仍存活、generation 匹配且请求未取消时才会提交结果。

异步 completion 由 Application 在固定的 Asset Completion 阶段通过 ResourceManagerHub 每帧泵送一次，默认预算为8个回调。各 ResourceManager 不再重复驱动共享 FileSystem。管理器销毁时会先取消未完成请求，再调用资源的 unload 释放 CPU/GPU 对象。

## 已知问题

- 当前解析、CPU Ready 和 GPU Upload 仍在同一个受预算约束的主线程 completion 回调内连续完成，还没有独立上传队列；
- best-effort 取消不能中断已经开始的底层文件读取，只能阻止其结果被提交；
- 资源监听仍逐项查询文件时间，缺少独立预算和指标；
- 尚未实现 Cooked Asset、稳定 AssetId、依赖清单、内容 Hash 和增量 Cooker。

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
