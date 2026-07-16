# 渲染架构与 bgfx 边界

> 状态：vNext 候选冻结。bgfx 是 Tina 的实现依赖，不是游戏开发 API。

## 当前 Legacy 事实

当前单体 `Tina` executable 直接链接 bgfx/bx/bimg，Tile、Sprite、Primitive、Particle、Text、UI
和 3D Cube 都有 header 或调用点接触 bgfx handle、ViewId、clear/state flag。至少 Engine、ECS、
Renderer 和 UI 的多份 Legacy header 存在第三方类型泄漏；Core 的 PUBLIC include root 还会传播
整个 `src`。这些是迁移输入，不是 vNext 边界已经完成的证据。

现有 `--smoke-3d` 已证明右手 Perspective、depth-tested indexed Cube 和退出时 vertex/index
buffer 回收；现有 2D/UI 路径也能运行。但目前 Scene、World、Widget 仍可能直接提交 backend，
因此不能在旧接口上继续扩张新的 Material、UI Widget 或 3D 功能。

## 不可变规则：游戏用户看不到 bgfx

“不暴露 bgfx”不只表示函数签名里不出现 `bgfx::Handle`，而是三层完全隔离：

| 层级 | 使用者 | 允许看到 | 禁止看到 |
| --- | --- | --- | --- |
| Game SDK | 游戏程序与 `IGameState` | World、Camera、Sprite/Mesh component、AssetHandle、UI Widget | RenderDevice、GPU handle、Pass/View、native surface、bgfx/bx/bimg |
| Tina Render SPI | Runtime/Scene/Asset/UI renderer 与模块测试 | Tina typed handle、descriptor、RenderFrame、Pass Scheduler | 第三方 enum/macro/type、公开 native escape hatch |
| Backend private | `tina_render_bgfx` 与离线 shader 工具 | bgfx/bx/bimg、ViewId、PlatformData、backend stats | Game component、UINode、EnTT registry、源资产玩法语义 |

普通游戏入口调用 `Tina::Desktop::CreateEngine(config)`；`tina_bootstrap_desktop` 私有组合
GLFW、bgfx、FreeType 和 miniaudio。游戏 target 不 include `BgfxRenderFactory`，不直接链接
`tina_render_bgfx`。失败注入和引擎集成测试才使用低层的
`EngineHost::Create(config, factories)`，factory 签名也只包含 Tina 类型。

Phase Context 不提供 `RenderDevice`。游戏通过 Scene component、Asset API 和
`RenderSceneWriter` 表达画面，不能用 `getNativeHandle()`、`void* nativeDevice()`、ViewId 或
backend command callback 绕开所有权。

## RenderFrame：World 与 UI 的唯一汇合点

World Render Scene Extraction 和 UI 是两条不同生产路径：

```text
World / Scene
  -> RenderSceneWriter
  -> immutable RenderSceneView

UIContext retained tree
  -> layout / paint cache
  -> immutable UIDisplayListView

RenderSceneView + UIDisplayListView + RenderSurfaceState + FrameTiming
  -> RenderFrame view inside Runtime-owned RenderFramePacket
  -> Pass Scheduler
```

```cpp
struct RenderFrame {
    RenderSceneView world;
    UIDisplayListView ui;
    RenderSurfaceState surface;
    SurfaceAttachmentOps attachments;
    FrameTiming timing;
};
```

`RenderSurfaceState` 是 `tina_render` 自己的纯值类型，只包含 framebuffer extent、format、revision
和 suspended 等调度所需字段。Runtime 在帧边界从 Platform `SurfaceSnapshot` 显式转换并同时取得
owning `SurfaceLeasePin`；`tina_render` 因此不 include 或依赖 `tina_platform`。

Game-facing `RenderSceneWriter` 位于 Scene/Runtime integration，使用当帧 Asset ready snapshot 把
AssetHandle 解析为 `FrameResourceRef`，再写 Camera、SpriteRenderItem、TileChunkRenderPacket、
MeshRenderItem、Bounds 和 Material instance，不提取 UI。UI Phase 单独冻结
`UIDisplayListView`。`tina_render` 的低层 writer 只接受已解析的 FrameResourceRef/packet，不依赖
AssetHandle 或玩法 TileMap。

Runtime 在 Render Pass 前组装一次 `RenderFrame`。Null 与真实 Pass Scheduler 消费完全相同的
RenderFrame；bgfx adapter 只接收 RenderDevice resource/submit 调用，不直接理解 RenderScene、
TileMap、Widget 或 AssetHandle。

两个 View 只在所属 packet 生命周期内有效，不能脱离 packet 保存裸 span。物理 owning target
固定为 `tina_runtime`；`RenderFramePacket` 是 Runtime private 类型，不进入 Game SDK 或 Render
SPI public header：

```text
RenderFramePacket
  RenderFrame view
  tina_render::RenderFrameArena
  tina_render::FrameResourceTable (FrameResourceRef -> internal typed resource)
  StaticVector<tina_render::FrameLifetimePin, kMaxFramePins>
  Runtime-private SurfaceLeasePin(surface token + revision)
  tina_render::SubmissionTicket (submit 后进入 in-flight)
```

Runtime 从自己拥有的固定容量 packet pool 取得 slot。为避免 Scene/UI include Runtime，
`tina_render` SPI 只定义窄 `FramePinSink` 和 move-only、固定 inline storage、无 heap fallback 的
`FrameLifetimePin`：

```cpp
class FrameLifetimePin;

enum class FramePinKind : std::uint8_t {
    AssetLease,
    AtlasGeneration,
};

class FramePinSink {
public:
    virtual ~FramePinSink() = default;

    virtual Core::Status add(
        FramePinKind kind,
        FrameLifetimePin&& pin) noexcept = 0;
};
```

Asset/UI adapter 把模块私有 lease/pin 类型擦除后转移给 sink；packet 只保存固定容量
`StaticVector<FrameLifetimePin>`，`kind` 只用于指标和诊断，Runtime 不 downcast 或依赖 concrete
类型。`FrameLifetimePin` 的最大 size/alignment 是构建期常量，所有 adapter 用 `static_assert`
验证；move/destroy 必须 `noexcept`。`add()` 仅在成功时接管所有权；容量满时调用方仍持有 pin，
当前 frame assembly 返回错误并由 packet 事务回滚已登记项。Surface pin 由 Runtime 从 Platform
直接取得并保存，不经过 sink，避免 Platform 依赖 Render；SubmissionTicket 由 RenderDevice
submit 成功后附加，也不经过 sink。

`FrameResourceTable` 用固定容量表按 resource slot + generation 确定性 intern；同一 Asset lease 或
Atlas generation 每个 packet 只调用一次 `FramePinSink::add()`。10,000个 Sprite 只复制紧凑的
`FrameResourceRef`，不能产生10,000次虚调用或重复 pin。

pin 只表达“保活到 submission completion”，不暴露 AssetHandle、Atlas、Window 或 backend 类型。
submit 后 packet 进入 in-flight，直到 backend completion/fence 才统一回收 arena 和 pin。
NullRenderDevice 立即完成 ticket，但走相同状态机。Pass 失败、Asset unload、Atlas eviction 和
Surface shutdown 都不能提前释放 packet；pool 满返回有指标的 back-pressure/错误，不临时 heap
fallback。

## Game SDK 的渲染表达

游戏代码只保存稳定资产和场景语义：

- `AssetHandle<SpriteAsset/Texture2DAsset>`；
- `AssetHandle<StaticMeshAsset/MaterialAsset/PrefabAsset>`；
- `Camera2D`、`PerspectiveCamera`；
- `SpriteRenderer2D`、`MeshRenderer3D`；
- Transform、visibility/layer/order 和材质参数。

Scene component 不保存 Buffer/Texture/Pipeline typed handle。Render Scene Extraction 使用当前帧
Asset ready snapshot 将 AssetHandle 解析为 render-internal resource reference，并为本帧持有
AssetLease 到 RenderFramePacket。未 Ready 资产按类型显式使用占位或跳过，不能让游戏拿 GPU
handle 自行查询。

详细 2D/3D 契约见 [2D 游戏架构](game-2d.md)和[3D 游戏架构](game-3d.md)。

## Tina Render SPI

以下类型属于 Engine module SPI，不是 Game SDK：

```text
BufferHandle       TextureHandle       SamplerHandle
ShaderHandle       PipelineHandle      RenderTargetHandle
```

每种 handle 都是不同 Tag 的 `owner token + index + generation`，不能隐式互转，并受具体 RenderDevice registry
约束；Debug 增加 owner cookie。Destroy 立即令逻辑 generation 失效，物理资源进入 retirement
ledger，直到 backend completion/fence 后才释放和递减实际计数。

最小 Tina descriptor：

| Descriptor | 必需字段 |
| --- | --- |
| `BufferDesc` | size、stride、usage、debug label id |
| `TextureDesc` | dimension、extent、format、mips、usage、color space |
| `SamplerDesc` | min/mag/mip filter、address U/V/W、anisotropy |
| `VertexLayoutDesc` | Tina semantic、numeric format、offset/stride |
| `ShaderDesc` | Cooked ShaderAsset/interface hash/backend payload view |
| `GraphicsPipelineDesc` | shader、layout、topology、raster、depth、blend、attachment formats |
| `RenderTargetDesc` | color/depth attachment、extent、sample count |

所有 enum 和 bitset 都由 Tina 定义，禁止 `BGFX_*` flag。Descriptor 与上传数据分离；create/
upload 只借用调用期 span，跨帧数据由 `UploadTicket` 持有 staging allocation。任何大小、stride、
offset、format 和容量在进入 backend 前验证。

## Surface bridge

Game SDK 的 Window API 只暴露 `WindowId` 和逻辑/Framebuffer metrics。Platform 与 Render backend
通过不安装到 Game SDK 的 integration SPI 协作：

```text
SurfaceToken(index, generation)
SurfaceSnapshot(extent, contentScale, revision, suspended)
NativeSurfaceLease(kind, opaque Tina-owned POD payload)
```

只有 `tina_render_bgfx` 在创建/重置 surface 时把 `NativeSurfaceLease` 映射为 backend
`PlatformData`。Game、Scene、UI 和公共 Render API 看不到 GLFWwindow、HWND、X11/Wayland、
`bgfx::PlatformData` 或无类型 window/display 指针。Window 销毁前必须停止 surface submit、通过
Render barrier并释放 lease。

Resize/content-scale 只在帧边界更新 revision；0×0 或最小化进入 `SurfaceSuspended`，跳过
attachment 创建与 Present，并使用平台等待避免 busy loop。首期 device lost 返回结构化 fatal
run error并安全退出，不承诺透明恢复全部 GPU 资源。

## RenderView 与 Pass Scheduler

```cpp
struct DepthAttachmentDesc {
    TextureFormat format;
    LoadOp load = LoadOp::Clear;
    float clearDepth = 1.0f;
    std::uint8_t clearStencil = 0;
};

struct RenderViewDesc {
    RenderViewId id;
    CameraReference camera;       // 纯 UI view 可为空
    RectF normalizedViewport;
    std::optional<DepthAttachmentDesc> depth;
    std::uint8_t sampleCount = 1;
};

struct SurfaceAttachmentOps {
    LoadOp colorLoad = LoadOp::Clear;
    Color4 clearColor = Color4::Black();
};
```

UI-only 和不使用 depth 的 Sprite2D view 令 `depth = std::nullopt`，不得创建隐式 depth resource。
Initial clear 只作用于实际声明的 attachment；M9 的3D view 才显式请求 depth。对应 Null/backend
测试必须证明 UI-only/2D-only packet 的 depth allocation count 为0。

Camera/viewport/attachment 是 Tina 描述。Surface attachment 的 initial load/clear 由
`RenderFrame::attachments` 唯一定义；bgfx ViewId 在 backend 执行时内部映射，
不进入 Scene component、UI command、sort key 或日志公共字段。

首期 enabled pass 的稳定相对顺序：

1. `Opaque3D`；
2. `Sprite2D`；
3. `UI`；
4. `Present`。

Pass Scheduler 在 surface 未 suspended 时，把 initial load/clear 绑定到首个 enabled content
pass；若没有 World/UI content pass，则发出内部 clear-only operation 后再 Present。后续 pass
只能 Load，不能重复 clear。由此纯 UI、2D-only、3D-only 都恰好一个明确的 clear owner。

纯 UI 帧可以跳过两个 World pass且不需要 Camera；Headless Present 是可计数 no-op。Pass 在执行
前声明 attachment、LoadOp/StoreOp、read/write 和依赖，不能执行中临时插入另一套顺序。失败
停止依赖 Pass，但必须闭合 trace marker、回收 CPU frame data，并让已提交 ticket 正常 retire。

首期没有 Light 或 Transparent3D pass。`Light` 不进入 RenderScene 占位；增加 Lit/Transparent
能力前必须先冻结组件、材质、排序和测试。

## Shader 与 Material ABI

Shader 使用离线工具生成 Cooked `ShaderAsset`。Tina 外壳包含：

- shader ABI version、interface hash、vertex layout hash、variant key；
- stage table、binding table、uniform block layout、texture/sampler slot；
- Tina target/platform id；
- 私有 backend payload table。

bgfx shaderc profile 和二进制只位于 backend payload；Runtime 不调用编译器。Material Cooker
根据 Tina ABI 离线验证常量、slot、纹理颜色空间和 variant，Runtime mismatch 明确失败，不在
draw 热点按字符串查 uniform。

首期 shader source 仍可采用 shaderc-compatible 工具格式，这是资产作者的离线实现细节，不是
C++ Game API。若以后需要与 backend 无关的 shader authoring language，必须有真实第二消费者
并单独设计；当前不伪造一套没有验证价值的 DSL。

## UI DisplayList 与批处理

UI DisplayList 只允许后端无关的 Quad/Image、GlyphRange 和 Clip 数据，并通过 packet-local
`FrameResourceRef` 引用 texture/sampler/atlas generation；不包含 Widget/UINode 指针、ViewId、
shader uniform、backend vertex declaration 或 bgfx TextureHandle。

UI 使用 premultiplied alpha。Renderer 只合并相邻且以下 batch key 相同的命令：

```text
UI pipeline kind + Texture/Atlas page + Sampler + Blend + ClipId
```

禁止为了减少 texture switch 对整份透明 DisplayList 全局排序；batch 前后 paint-order checksum
必须一致。空 clip 直接剪枝，首期 clip 只用 axis-aligned scissor，rounded/stencil clip 后置。

高性能 UI 的 dirty、PaintCache、文本和 DisplayList 契约见[自研 UI](ui.md)。

## 资源、线程与关闭

- RenderDevice 由主线程唯一驱动 `beginFrame -> submit passes -> endFrame/present`；
- Worker 只能准备 immutable descriptor、bounds/culling 结果和 CPU decode，不调用 RenderDevice；
- resource create/destroy 和 GPU upload 只在 Render/GPU Upload phase；
- bgfx 内部可以有线程，但 Tina 不从任意 Worker 并发 submit；
- upload staging 和 RenderFramePacket resource/atlas lease 在 completion/fence 前保持 owning；
- 不能把固定“延迟 N 帧”当通用安全释放规则；
- 关闭先停止 `IGameState`/Asset producer，再 drain upload/RenderFramePacket/retirement，最后销毁 surface/device。

每种资源记录 logical/physical current、peak、estimated bytes、create failure 和 retire backlog。
300帧退出时 Buffer、Texture、Sampler、Shader、Pipeline、RenderTarget 和 ticket 必须归零。

## bgfx adapter 唯一职责

`tina_render_bgfx` 负责：

- Tina descriptor 到 bgfx object/state 的验证和映射；
- Surface/PlatformData、ViewId、attachment 和 Present；
- shader backend payload、uniform/binding 和 pipeline cache 实现；
- transient/instance buffer、resource upload、backend completion/retirement；
- 将 backend error/stats 转成 Tina error/metrics。

它不得 include Game component、EnTT registry、UINode、源 glTF 或玩法 TileMap。公共错误只携带
Tina category/code、可选 native integer code 和 UTF-8 context，不返回 bgfx enum/type。

## CMake 与自动化硬门禁

文档约定必须由构建阻断：

1. 新 public header 位于 target-scoped `include/tina/...`；实现目录只作为 PRIVATE include，
   禁止再次 PUBLIC 暴露整个 `src`；
2. `tina_render_bgfx` 对 bgfx/bx/bimg 使用 `PRIVATE/SYSTEM`；Scene/UI/Asset/Runtime 的 direct link、
   public interface、include/definition usage 均不能出现这些 target。Game/Sample 只能直接链接
   `Tina::GameSDK`、`Tina::DesktopBootstrap` 和按需的公开扩展模块（首个为
   `Tina::Physics2D`）；最终生产可执行文件经 bootstrap 私有实现解析到 bgfx 是预期行为，不等于
   Game SDK 暴露 backend；
3. 除 `render/bgfx/**`、离线 shader tool 和 adapter test 外，源码策略门禁禁止
   `<bgfx/...>`、`bgfx::`、`BGFX_`、`bx::`、`bimg::`；
4. `tina_game_api_consumer` 在没有 bgfx include path 时单独包含全部 Game SDK umbrella header；
   `tina_physics2d_api_consumer` 同样在没有 Box2D include path 时编译安装后的公开 header；
5. 每个 Render SPI public header 有 include-what-you-use compile-only consumer，不能依赖传递
   include 偶然成功；
6. `vnext-null` 完全不 add_subdirectory/link/load bgfx，仍构建并运行300/10,000帧；
7. SDK 安装到 staging 后由仓库外最小 game consumer 构建：源码不添加第三方 include/definition/
   direct link，仍可通过 desktop bootstrap 完成最终链接；
8. 同一 Pass Scheduler 分别搭配 NullRenderDevice 与 BgfxRenderDevice，对同一 RenderFrame 产生
   一致的 pass/order/resource-lifetime checksum；
9. 在途 packet 期间卸载 Asset、退役 Atlas、关闭 Surface 和注入 Pass 失败均保持引用有效，ticket
   完成后 packet/lease/pin/resource count 全部归零；
10. 纯 UI、2D-only、3D-only、无 content 和 SurfaceSuspended 分别验证 initial clear 恰好一次或0次。

## Roadmap 与验收解释

M7 的可见 UI 需要最小真实 Surface 和 UI Pass，因此 M7 同时建立私有的最小
`tina_render_bgfx` consumer；M9 是扩展 Opaque3D、Mesh、depth 和 3D Shader/Material，不是第一次
让 UI 直接调用 Legacy renderer。

在完整 Asset/Cooker 于 M10 接入前，M7–M9 只允许使用版本化、确定性的内置 Cooked fixture 或
procedural geometry。禁止恢复 Runtime 路径加载，也禁止游戏自行创建 bgfx resource。

验收分开记录：

- 当前 Legacy：`Tina --smoke-*` 只证明旧路径仍可运行；
- vNext infrastructure：Null/UI/2D/3D 独立 sample 验证接口和生命周期；
- vNext product：Cooked TileMap 2D 与 Cooked glTF/Material/Prefab 3D 才计入 Legacy 删除门禁；
- 进程返回码、结构化资源计数、性能数据和实际截图是四类不同证据。

`tina_bench` 记录 extraction、culling、sort、batch、submit、resource/retirement 和有效 GPU timer
p50/p95/p99。只有 backend 提供校准 timestamp、valid/disjoint 和延迟 frame mapping 时，GPU
时间才参与硬门禁；bgfx 估算 stats 只能标为 informational。
