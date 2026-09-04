# Rendering

Tina 的公开 Render 边界是 backend-neutral `Tina::Render`；bgfx 只存在于 `tina_render_bgfx` 私有
实现。当前产品已经有 2D、3D、UI/Glyph 与 Texture2D/StaticMesh upload 路径，以及 EngineHost 侧
`RenderFramePacket` + FramePin + packet-local `FrameResourceRef` table + present-return CPU completion。Opaque3D 使用
metallic-roughness Cook-Torrance GGX：Scene extraction 每帧提交0..4个 directional + 0..8个 point + 0..8个
spot lights + ambient 的自包含 `RenderScene` snapshot；可选 `Mesh3DImageBasedLightingDesc` 再绑定一份 cooked
diffuse irradiance cubemap、prefiltered specular cubemap 与 BRDF LUT。`setMesh3DLighting()` 仍是低层 device
fallback/direct SPI。当前已有 PointLight3D 与 SpotLight3D、PerspectiveCamera3D influence-sphere culling，以及
固定4级联 directional CSM、单 SpotLight shadow、单 PointLight 全向 shadow、startup-only shadow extent 配置、
显式 alpha-blended Transparent3D 与确定性 pass scheduler；仍无通用 GPU submission fence；
Texture2D/GPU mesh/EnvironmentMap 已有独立、backend-proven 的 GPU resource retirement completion。

## Target 边界

| Target | 当前职责 |
| --- | --- |
| `tina_render` | `IRenderDevice`、RenderSurface、RenderFrame、RenderScene、UI DisplayList、GPU generation IDs、Null backend |
| `tina_render_bgfx` | WindowSurface/bgfx 生命周期、Sprite2D、Opaque3D/Transparent3D PBR/IBL、UI solid/glyph pass、GPU texture/mesh/environment storage |
| `tina_ui_render_integration` | committed UI paint → Render `UIDisplayList` |
| `tina_window_surface_integration` | Platform native surface → Render 的 move-only handoff |

公开头不得出现 bgfx/bx/GLFW/native window token。Scene、UI 与 Asset 只依赖 Tina 描述符和句柄。

## RenderFrame 借用契约

当前 `RenderFrame` 包含：

- frame index 与 interpolation；
- optional primary `RenderSurfaceState`；
- submit-call-local `FrameResourceTableView`；
- submit-call-local primary World `RenderSceneView`；
- submit-call-local primary UI `UIDisplayListView`；
- optional R8 `UIGlyphAtlasPageView`。它带一个单调的 `pageRevision`：page 始终按满尺寸分配并逐帧传入，
  所以 backend 用它判断是否可以跳过整页上传，`0` 表示未知、必须上传。`UIGlyphAtlas::pageRevision()` 只在
  glyph 像素写入或 `clear()` 时自增。

所有 view/span/pixel pointer 只在 `IRenderDevice::submitFrame()` 调用期间有效。backend 必须同步消费，
返回后不能保存。`submitFrame()` 返回 `Submitted` 或 `SkippedSuspendedSurface`；只有前者允许 Runtime
调用 `present()`。

### native surface 重建（`nativeBindingRevision`）

`RenderSurfaceState::nativeBindingRevision` 单调递增，**仅在 surface 背后的 native window 被替换时**
递增——Android 切后台销毁 `ANativeWindow`、回前台给一个新的。它与 `surfaceRevision` 正交，后者表达
几何与可用性。

**它不能与 `Suspended` 合并**：`Suspended` 承诺资源仍然有效、只是暂停呈现；而新的 native window 意味着
backbuffer 的 surface 与 swapchain 必须重建。合并会让引擎持有一个依附于已消失窗口的 backbuffer
（[ADR 0034](adr/0034-native-surface-rebind.md)）。

校验集中在 `RenderSurfaceStateTracker`：递增必须伴随新的 `surfaceRevision` 与新的
`sourceMetricsRevision`（binding 变化本身就是一次 surface fact 变化），后退与 0 一律拒绝。tracker 用
一个 latch 把「本次提交发生了 rebind」告知 backend，因此不 rebind 的 backend 无需任何改动，且被拒绝的
提交不会遗留待处理的 rebind。

bgfx 后端的 rebind 是 `setPlatformData(新 nwh)` + 强制 `bgfx::reset()`，**不调用 `bgfx::shutdown()`**。
`setPlatformData()` 的头文件注释写「必须在 `init` 之前调用」，但其实现只禁止改 display type 与 context
（`bgfx.cpp:448-458`），native window handle 明确允许更换；`renderer_vk.cpp:7622-7626` 在下一次 reset
观察到不同的 nwh 时重建 surface。reset 是强制的，因为 resume 时 extent 可能不变——只看几何会漏掉
same-size resume。

因此 **program/texture/mesh/shadow atlas 等 device 资源与全部 `AssetLease` 都存活**，没有资源被重新上传。
`RenderStatistics::nativeSurfaceRebinds` 记录次数；桌面恒为 0（native window 在 device 生命周期内不变），
产品 gate 可据此断言。`NativeWindowBindingChangedUnsupported` 保留给真正无法 rebind 的后端。

`RenderFramePacket` + `FramePin` + `ISubmissionCompletionLedger` 已接入 EngineHost：submit 前可登记
Surface/GlyphAtlas pin；失败路径 abandon；suspended skip 与成功 present 都确定性释放。Host 持有
`unique_ptr<ISubmissionCompletionLedger>`，可选 factory 只允许替换记账实现，**不能改变完成时点**。
`SubmissionTicket` 是不可复制、绑定签发 ledger 的唯一所有权 token；移动进 packet 后由 packet 在
complete/abandon、复用或析构时关闭，不能用副本或其他 ledger 重复消费 in-flight 计数。
EngineHost 只有在 packet abandon 成功后才可继续 State teardown；persistent failure 必须记录
`runtime.lifecycle` 并 fail-stop，不能带着 live frame owner 销毁 State。

`FrameResourceRef` 是 packet-local `{owner, generation, index}` token，不拥有资源。packet 的固定容量资源表
按 `{FrameResourceKind, deviceBindingKey}` intern：首次登记消费 owning `FramePin`，同帧重复登记立即释放
重复 pin 并返回同一 ref；invalid/capacity 失败不消费调用方 pin。`FrameResourceTableView::resolve()` 对
cross-packet、stale、越界与 wrong-kind ref fail closed。view/ref 在 complete、completeSkipped、abandon 或
packet 复用后立即失效，backend 只能在 `submitFrame()` 内同步解析。全部 Sprite2D 与 Mesh3D item 都只保存
packet-local ref；3D geometry/material 分别使用 `Mesh3DGeometry`/`Mesh3DMaterial` kind。Null/bgfx 在任何
提交副作用前验证 owner/generation/kind/index 与 `u32` device binding range。

所有 composition 采用唯一语义：`submitFrame()` 同步消费借用 view，成功 `present()` 返回后关闭 CPU
submission ticket 并释放 frame pin。该完成点只表示 Host/backend 已不再借用本帧 CPU 数据，**不表示 GPU
执行完成或 Asset 物理退役**。旧 `PresentSync`/`FrameDeferred` 分支、bgfx ledger wrapper、下一帧固定延迟
handoff 与 `bgfx::frame()` 假 fence token 已删除。GPU resource retirement 走下文独立 marker，不复用该
CPU ticket，也不把普通 frame number 描述为 fence。

Opaque3D metallic-roughness PBR：产品 registry 通过单次 `setMesh3DMaterialBinding()` 原子提交
baseColor/MR/normal 与 Cooked Material v2 metallic/roughness factor；细粒度 setter 仅保留为低层 direct
SPI。submit 时分别绑定 `s_texColor`、`s_texMR`（glTF 打包：G=roughness、B=metallic）与
`s_texNormal`。`Mesh3DLightingDesc` 是唯一 lighting 描述模型：writer/device 均同步消费0..4个
backend-neutral directional light、0..8个 point light、0..8个 spot light 与 ambient；超容量、零方向、
非法 cone、非正 radius、负 RGB/ambient 或非有限值失败。
`RenderSceneWriter::setMesh3DLighting()` 将调用方 span 深拷贝进固定4+8+8槽的 committed frame snapshot；
bgfx 只在当前 submit 中临时覆盖持久 device fallback，不污染后续帧。World 未声明灯组件时不发布 snapshot；
已声明但全部 inactive 时发布0灯 + ambient snapshot，显式覆盖 fallback。
direct lighting 对 directional/point/spot radiance 统一使用 GGX NDF、Smith geometry、Schlick Fresnel 与
energy-conserving diffuse；point/spot 继续应用既有径向/角度衰减。`GpuEnvironmentMapId` 以一个 generation
identity 原子拥有 RGBA16F diffuse cubemap、带完整 mip 链的 RGBA16F specular cubemap 和 RG16F BRDF LUT。
`setMesh3DImageBasedLighting()` 只接受同 device live handle、非负 intensity 与 finite world-Y rotation；shader
按 roughness 选择 specular LOD，并以 split-sum 合成 diffuse/specular IBL。未绑定 IBL 时保留 frame ambient
fallback；未绑定 baseColor 用1×1白，未绑定 MR 图时 metallic=0、roughness=1，未绑定 normal 图时只用几何法线。P3N3T4UV2
mesh 由 vertex shader 输出 tangent/handedness，fragment shader 构造正交 TBN；signed model scale 会修正
handedness。StaticMesh 只有 P3N3T4UV2 一种布局，不存在旧布局运行时分支。诚实限制：当前
DirectionalLight3D/PointLight3D/SpotLight3D 均可进入 Scene snapshot；point/spot 使用 world position/influence
radius，在有效 PerspectiveCamera3D 与非0 surface 时做 sphere-frustum culling。static/skinned mesh 同样在
Scene extraction 内将 authored local sphere 按 WorldTransform 转成保守 world sphere，并在 Asset resolver、pose
provider 与 RenderScene 容量消费前剔除；无相机/0x0 surface 保留未裁剪行为。spot 额外提交 world-space
出光方向与 inner/outer cone cosine。`outerConeHalfAngleDegrees` 的公开合法域保持 `<90°`，backend 不另行
收紧完整 FOV，也不静默 clamp。一个 directional light 可携带 optional `CascadedDirectionalShadow3D`；
它以 `maximumDistanceMeters`、`depthBias` 与 `normalBiasMeters` 描述固定4级联阴影。Render snapshot
映射为 `Mesh3DCascadedDirectionalShadow`，以排序后的 `directionalLightIndex` 关联灯光。bgfx 在首次出现对应
shadow pass 时按需创建独立2×2 D16 `DirectionalShadowAtlas`（默认2048×2048、每 tile 1024×1024），并执行 camera-slice projection
与每级联3×3 PCF；级联 pass 不消费
primary-surface clear ownership。级联的横向窗口取 frustum slice 的**外接球**而非八角点紧 AABB，并把窗口中心
吸附到整数个 atlas texel（ADR 0044）：外接球半径只由 split 深度与镜头决定，相机平移旋转都不改变它，因而存在
一个固定的 texel 栅格可供吸附；深度轴不吸附也不用外接球，仍取八角点紧 AABB，因为它不做光栅化，且放宽会把
远处沿光轴的几何（如太阳 billboard）拖进 depth pass。窗口额外携带1个 texel 余量以容纳吸附偏移。最多一个 camera-affecting spot light 可携带 optional `SpotLightShadow3D`；
它以正且小于 influence radius 的 near plane、depth bias 与 normal bias 描述。Render snapshot 在 spot lights
稳定排序后映射 `spotLightIndex`，bgfx 在四个 CSM pass 后执行一个 sampled D16 depth pass（默认1024×1024），
receiver 仅对匹配的 spot slot 应用3×3 PCF。最多一个 camera-affecting point light 可携带 optional
`PointLightShadow3D`；它使用同样有界的 near/depth/normal bias，并在 point lights culling 与稳定排序后映射
`pointLightIndex`。bgfx 拥有六张相同尺寸的 sampled D16 map（默认512×512），按 `+X/-X/+Y/-Y/+Z/-Z` 提交六个
depth pass；receiver 以 dominant axis 选择面，只对匹配的 point slot 应用3×3 PCF。固定顺序为
CSM×4 → Spot×1 → Point×6 → Opaque3D → Transparent3D → Sprite2D → UI，所有 shadow pass 都不消费 primary-surface clear。第二个可见
point shadow 或非法 near/depth/normal bias 在 Scene publish 前 fail closed。Spot 与 Point 的 D16 map 同样在各自
pass 首次出现时创建；无任何 shadow pass 的启动期只保留一个1×1 sampled D16 fallback 供 shader sampler 合法绑定。

`Mesh3DAlphaMode` 只有 `Opaque` 与 `Blend`，由 Cooked Material/authoring 显式声明；Runtime 不从
base-color alpha 或纹理内容推断 pass。Scene extraction 把 material intent 复制到 static/skinned item，
RenderScene commit 将两类 item 各自分成 Opaque 前缀与 Blend 后缀，Opaque batch 只覆盖 static Opaque
前缀。所有 Blend static/skinned item 再进入一个统一 `RenderTransparent3DDraw` 排序域：按相机到
world-space bounds center 的平方距离降序，等距时按 stable entity key、draw kind 与 item index 建立全序。
`transparent3DDrawCapacity` 默认等于 static/skinned 默认容量之和，最大值同样由两类最大容量求和；
容量不足整次 build 失败，不发布部分 scene。

bgfx Transparent3D 使用 straight-alpha `SRC_ALPHA/INV_SRC_ALPHA` blend，写 RGB/A、保留
`DEPTH_TEST_LESS`，但不写 depth。透明 item 不进入 directional/spot/point shadow caster batch，因此不投
shadow；receiver 仍使用与 Opaque3D 相同的 direct lighting、CSM/Spot/Point shadow、PBR material 与 IBL。
static transparent draw 从共享 instance buffer 按单 item range 提交，skinned transparent draw 逐项提交
palette。Null/bgfx 在任何 submit 副作用前验证 alpha 前后缀、统一 back-to-front 全序、完整无重复覆盖、
material binding alpha 一致性与 resource/palette 范围；未注册 material binding 只解析为 Opaque。
当前没有 `Mask`/alpha-test、order-independent transparency 或 transparent shadow caster。

`EngineConfig::shadowMapExtents` 是 device-lifetime 不可变启动配置，并原样传播到
`RenderDeviceCreateParams`。`directionalCascadeTileExtent`、`spotLightMapExtent` 与
`pointLightFaceExtent` 必须是 `[128,4096]` 内的2次幂；directional atlas 始终为2×2 tile。
EngineHost 在创建任何 module factory 前 fail closed，Null/bgfx 直接 factory 也独立校验。bgfx 的 D16
资源创建、shadow view rect 与3×3 PCF texel size 使用同一份实际 extent；不支持热改或旧固定尺寸分支。

`EngineConfig::renderDrawCallCapacity` 是每帧 backend submission 的启动定容，合法值为1024的整数倍或
精确的 native 上限65535，默认65535以保持通用 Runtime 上限。bgfx 将它传给 `Init::limits.numDrawCalls`，并因 Tina
只允许 RenderDevice owner thread 提交而把 `maxEncoders` 固定为1；工具可依据其冻结的 scene/UI 容量显式降低。
`TinaEditor` 使用50176：48K 对应 Layout Debugger 可占满的 UI DisplayList（overlay 边框会被面板排除区
最多切成两段），额外1K留给有界 scene pass；
这仍避免空会话承担 bgfx 的65K双帧 render-item arrays 与默认多 encoder uniform buffers。

### 图形 API 选择（`EngineConfig::rendererApi`）

`Render::RendererApi` 是 backend-neutral 的 API 选择：`Automatic`（默认）、`Vulkan`、`Direct3D11`、
`Direct3D12`、`Metal`、`OpenGL`、`OpenGLES`。非法枚举值在 `EngineConfig::validate()` 就拒绝——若放到
device 创建才报，读起来像驱动问题而不是配置错误。

**显式请求不被满足时 fail closed。** bgfx 接受显式 `init.type` 后仍可能回退到别的 renderer；因此
device 在 `bgfx::init()` 之后比对 `bgfx::getRendererType()`，不一致即失败。实测在 Windows 上
`--renderer=metal` 返回「bgfx created a different renderer than the requested graphics API」而**不是**
静默跑 D3D11。理由很直接：要求 Vulkan 却拿到 GL 的游戏会带着错误的性能假设发布，且无从察觉。
Null device 同理只接受 `Automatic`——它不实现任何图形 API，接受显式请求就等于假称满足了「我需要 Vulkan」。

`Automatic` **不等于**"无意见"：它先过 `Render::preferredRendererApi()`。**Android 上偏好 Vulkan**，
因为 bgfx 自己的评分（`bgfx/src/bgfx.cpp:3269-3276`）在 Android 分支只给 `OpenGLES` 加 20 分、
**给 Vulkan 加 0 分**，而 Vulkan 自 Android 7.0 起可用、且在 bgfx 里本就为 `BX_PLATFORM_ANDROID`
编译进去（`bgfx/src/config.h:105`）。也就是说拦住 Vulkan 的只有自动挑选的评分，不是能力缺失；ESSL
变体退居 Vulkan 驱动不可用时的回退路径。

**桌面保持 `Automatic`（即 bgfx 选 D3D11）是刻意的**：本仓库全部产品像素指纹与视觉 gate baseline 都在
该 renderer 下固化，改默认等于一次刻意的 re-baseline，而不是配置调整。游戏仍可显式要求 Vulkan。

**已知限制（本机实测）：** Windows 上显式 Vulkan 可正常初始化并提交帧（124 帧、3 张贴图 upload/bind/
retire 齐全），但 `capturePrimaryFrameRgba8()` 失败、`pixelCaptureOk=false`。原因在 bgfx：
`renderer_vk.cpp:4450` 的 `isSwapChainReadable()` 要求 `m_supportsReadback`，本机 Vulkan swapchain 不满足，
`requestScreenShot` 直接返回。**因此像素证据 gate 必须留在默认 renderer 上**，不能用 `--renderer=vulkan`
跑视觉门禁。这是 bgfx/驱动的能力边界，不是 Tina 的缺陷。

`EngineConfig::renderMsaaSamples` 同样是 device-lifetime 启动配置：`0`（默认，关闭）或 `2/4/8/16`，
其余值在 EngineConfig 校验与 bgfx factory 双双 fail closed。它映射为 backbuffer 的
`BGFX_RESET_MSAA_X*` flag，由 init 与 resize reset 共用。samples 与所有像素证据 gate 保持 `0`
以免破坏已冻结的视觉金标；`TinaEditor` 同样使用默认 `0`，不为整个 authoring backbuffer 常驻多采样资源。

垂直同步是**唯一可热改**的设备设置：`IRenderDevice::setVsyncEnabled()` / `vsyncEnabled()` 带默认实现
（默认报告开启并吸收写入），所以既有 backend 与测试替身无需改动。`EngineConfig::renderVsync`（默认
`true`）负责播种，`BGFX_RESET_VSYNC` 不再硬编码进 `kDefaultResetFlags`，而是按当前状态 OR 进
`resetFlags_`。因为 vsync 切换不改变任何几何，surface frame planner 不会请求 reset，所以 backend 用一个
dirty flag 在下一个已提交帧以当前 extent 重新 `bgfx::reset`；planner 保持纯几何职责不变。切换被推迟到
提交帧而非立即执行，是因为 `bgfx::reset` 不能在 `submitFrame` 与其 `present()` 之间运行。null device 会
记录请求状态，使 headless 工具与真实 backend 报告一致。默认保持开启，因此像素证据 gate 不受影响。

vsync 的游戏侧入口是 Frame Update 相位的 `FrameUpdateContext::displaySettings()`，它返回 phase-local
的 `DisplaySettings` 句柄，只暴露 `setVsyncEnabled()` / `vsyncEnabled()`。该句柄不得跨帧保存。它和
Action rebinding 一样是 **top-state 权限**：只有栈顶 State 拿到有效句柄，非栈顶 State 拿到空句柄
（`hasValue()` 为 `false`），`setVsyncEnabled()` **静默无效且不报错**（`EngineHost` 按
`depthFromTop == 0` 传入 device 或 `nullptr`），这样下层暂停菜单不会把设备从上层正在运行的内容脚下
改掉。同样地，`vsyncEnabled()` 报告的是**已请求**状态，backend 最迟在下一次 `present()` 应用，
不保证在请求当帧生效；无 device 时它返回 `true`。

`DisplaySettings` 不是资源 API 的入口。GPU 资源走 `IRenderDevice` 本身，游戏通过
`GameStateEnterContext::renderDevice()` / `GameStateExitContext::renderDevice()`（`IRenderDevice&`，
host-lifetime，可记下地址给非相位 helper）或 `FrameUpdateContext::renderDevice()`（`IRenderDevice*`，
仅栈顶，非栈顶为 `nullptr`）借用引擎拥有的那一个实例，不自己组合 device
（[ADR 0046](adr/0046-render-device-borrow-in-phase-contexts.md)）。

Sprite2D lighting（`2D-LIGHT-N5`）只使用 frame-scoped `Sprite2DLightingDesc`：0..8个 committed world-space point
light、0..32个 world-space shadow segment、正 influence radius、0..influence radius 的 source radius、
非负 RGB 与 ambient；shadow endpoint 必须 finite，
且 segment 不得退化为同一点。`setSprite2DLighting()` 深拷贝调用方 span，重复/非法描述使 build
原子失败；未配置 snapshot 时 bgfx 使用 ambient=1 的既有 unlit 输出。配置后 fragment shader 以 world
position 计算线性径向衰减。source radius=0 时以 fragment→light 与 segment 相交测试清零被遮挡的点光贡献；
正值时把 segment 裁剪到归一化深度 `(0.001,0.999)`，投影到 finite line-source 区间并按覆盖率连续缩放
visibility，多 segment 以 multiplicative transmittance 合成；每个连续
texture batch 都重新提交完整 uniform arrays。ambient、sorting layer → order → ordinal 与
premultiplied-alpha 合成保持不变。Scene extraction 在 descriptor 之前以 resolved、pixel-snapped Camera2D
执行旋转相机空间的精确 circle-vs-rectangle culling，只有 camera-affecting light 占用8个 committed 槽；
第9盏仍显式失败。没有 resolved camera 时保留未裁剪上限，shadow segment 始终不裁剪。soft shadow 保持
`8×32` 固定循环，不增加 uniform 数组或多重采样；多 segment 重叠是乘法近似，不是精确 area-light union。
每个 Sprite2D item 还可携带 optional packet-local normal Texture2D ref。Null/bgfx 在任何 submit 副作用前
对 base/normal ref 同步执行 owner/generation/kind/binding-range preflight；连续 batch identity 是
`(baseTexture, normalTexture)`，normal 不参与透明排序。bgfx slot 1 绑定 batch normal 或 device-owned flat-normal
texture，以 batch-local uniform 控制分支；fragment shader 用 world-position/UV derivatives 构造 TBN，因此
rotation、signed scale、atlas UV 与 flip 无需额外矩阵。normal 只调制 point-light contribution，ambient、shadow
visibility、attenuation 与 premultiplied alpha 保持原契约；无 normal 走原有分支，RGBA8 `(128,128,255)` 的
flat normal 相对 Lambert factor 精确为1。Render 已发布 HDR/tone-mapping 后处理契约，Null 可执行 reference
math；当前 bgfx 产品路径对非空后处理 chain 显式 fail closed，尚无真实 HDR/tone-mapping GPU pass。product-2d
schema 29 继承 schema 19，并以
`authoredPointLight2DCount=3`、`pointLight2DCount=2`、`culledPointLight2DCount=1` 提供集成证据，继承双
ShadowOccluder2D 与 soft/hard 差分，并以 `normalMappedSpriteCount=1/0` 的 normal on/off 可重复像素差分关闭 N5。
Cooked `Fx2D` 不扩展 Render wire contract：Scene factory 最终仍通过 ParticleSystem/Trail 生成 Sprite2D item，
并在 extraction 时把 weak Sprite handle 解析成 packet-local `FrameResourceRef`。当前没有 GPU FX simulation、
effect graph render pass 或 mesh-ribbon trail。

## RenderScene

`RenderSceneBuilder` 在固定容量 storage 中事务式构建 Camera/Sprite/Mesh：

```text
beginFrame(surface facts)
  -> phase-local writer
  -> add Camera2D/PerspectiveCamera3D/Sprite2D/Mesh3D/SkinnedMesh3D
  -> optional setSprite2DLighting (deep-copy fixed frame snapshot)
  -> optional setMesh3DLighting (deep-copy fixed frame snapshot)
  -> optional setClearColor (once per frame)
  -> validate, cull, stable sort and batch
  -> commit borrowed view
```

当前能力包括：

- Camera2D projection、viewport、pixel snap 与 world picking；
- Sprite2D 视锥裁剪、layer/order/ordinal 稳定排序、相邻 `(baseTexture, normalTexture)` batch；
- optional self-contained Sprite2D lighting snapshot、最多8个 committed point light、32个 shadow segment 与 ambient；
- PerspectiveCamera3D、point/spot/static/skinned sphere-frustum culling、Opaque3D stable batch，以及 static/skinned 统一 Transparent3D back-to-front draw 序列；
- optional self-contained Mesh3D lighting snapshot、可选 CSM/SpotLight shadow 描述、重复设置/非法描述的事务失败与统计；
- 逐帧 clear color（ADR 0042）；
- framebuffer 0x0 suspended 路径；
- 容量/非法数值/非法 resource ref 失败时不发布半份 scene。

`RenderSceneWriter::setClearColor()` 收**线性** `RenderLinearColor`，与 `baseColorFactor`、
`Mesh3DDirectionalLight::colorR` 同一标尺；sRGB 编码由后端负责，公共 API 不出现 gamma。
一帧只允许设一次，重复设置返回 `RenderErrorCode::InvalidSceneClearColor`；非有限、负、
大于 1 的分量同样拒绝。未调用时使用 `DefaultSceneClearColor`，其 sRGB 编码逐字节等于旧后端
常量 `0x102a43`，故不设色的产品与既有 gate 输出不变。后端不再持有颜色常量。
这只是清屏色，不是天空盒：没有渐变、没有大气散射，需要那些就得画东西。

自发光材质（ADR 0043）：`Mesh3DMaterialBindingDesc::emissiveFactorR/G/B` 是逐材质的线性
radiance，默认 0，故不改变任何既有材质。它在 direct 与 ambient/IBL 之后**相加**，不乘 `NdotL`、
不乘衰减、不受阴影影响，因此背对所有光源的面也能是亮的。校验取 finite 且非负但**不设上限** ——
它与 `Mesh3DDirectionalLight::colorR` 同标尺，不是 `metallicFactor` 那种 `[0,1]` 的 BRDF 参数。
静态/骨骼/透明三条路共用 `fs_tina_opaque3d_mr`，所以三者行为一致。当前只有 factor，没有
emissive 贴图，即整个材质均匀自发光。**没有 tone mapping**：shader 末尾是 `linearToSrgb(lit)`，
超过 1 的部分直接被编码 clamp 成白，所以自发光物体是纯白色块而非过曝光晕；选值时要对着传输函数
验算，否则算好的颜色会在编码阶段被悄悄吃掉。bloom 在 bgfx 后端不可用。

Scene/Runtime writer 不能创建 GPU resource。产品 State 在安全阶段上传并绑定资源；Sprite2D extraction
与 Mesh3D extraction 都通过 phase-local `FrameResourceSink` intern 当前 binding，RenderScene 不保存
持久 device key。

## UI DisplayList 与 Glyph

UI paint 通过 integration 转为固定容量 DisplayList。当前 command/batch 支持 SolidQuad、SolidEllipse、Glyph
与 ImageQuad；clip 仍为 axis-aligned scissor。UI authoring 的 `UIBoxPaint` 支持 Rectangle/Ellipse/Line，
Canvas 支持 `SolidRect`/`SolidEllipse`/`SolidLine`。SolidLine committed entry 保存 world-space 端点、logical
thickness 和 conservative envelope；integration 在 logical 空间构造线宽法向的四个角点，再对每个角点分别
应用 framebuffer `scaleX/scaleY`，以 exact `UISolidQuadVertices` 写入 SolidQuad command。其 integer bounds
只用于 culling/clip，因此 `scaleX != scaleY` 的 anisotropic 投影保留精确四顶点，不依赖 angle 或单一像素
thickness 近似。

SolidEllipse command 以 bounds 和 pixel stroke width 表达；零 stroke 为填充，正 stroke 为向内描边。bgfx
geometry 仍生成一个 quad，统一 R8 coverage shader 根据 local UV、pixel extent 计算外椭圆 coverage，描边时
再减去内椭圆 coverage。Glyph 使用 UIContext-owned R8 atlas page：Runtime 在 submit 时借用像素，bgfx 创建
或更新私有 atlas texture；ImageQuad 使用 packet-local Texture2D ref、normalized UV、tint 与 Linear/Nearest
sampling，并选择独立 RGBA shader。

已实现的路径包括：

- Text/Glyph placement → `UIDrawCommandKind::Glyph`；
- Box/Canvas Ellipse → `UIDrawCommandKind::SolidEllipse`，填充/向内描边均走 shader coverage；
- Box/Canvas Line → exact 四顶点 `UIDrawCommandKind::SolidQuad`；
- Editor grid/gizmo segment → `UIBoxPaint::Line`，rotation ring → 单个 `UIBoxPaint::Ellipse`；
- atlas UV 与 page 校验；
- solid/glyph 按 clip、kind、atlas page batching；
- Image/Icon content 与 Canvas Image/NineSlice → `UIDrawCommandKind::ImageQuad`；
- root-scoped `(root, AssetId)` resolve/pin 去重、source rect UV、image texture/clip/sampling 相邻 batching；
- NineSlice row-major 1..9 quad 原子展开及 fractional-DPI 共享边界投影；
- bgfx R8 atlas create/update、solid white texture 与 glyph texture binding；
- bgfx RGBA ImageQuad program、Texture2D binding preflight 与 straight-alpha-to-premultiplied sampling；
- UI → Render integration tests 与 bgfx geometry tests。

Render `SolidQuad` 已用 `UIPixelCornerRadii` 将四角像素半径贯通 DisplayList 校验/checksum、bgfx vertex 与
coverage shader；Retained UI 的 `UILogicalCornerRadii` 现从 box/Canvas `SolidRect` 经 committed paint 逐角投影，
并按目标 bounds half-extent 独立夹紧。rounded/stencil 子树 clip、复杂 material 与跨 GPU golden 仍未完成；Image/Icon/NineSlice 的产品、
失效、尺寸矩阵及性能证据已实现，不再列作缺口。

`UI-IMAGE-001` 让 Image/Icon 各发一个 Image entry，NineSlice 在 UI committed paint 中展开为
1..9个相同 entry。batch 持有 packet-local 通用 Texture2D ref 与 sampling，command 持有
bounds/UV/tint/clip；只采样 R8 `.r` coverage 的 Solid/Glyph shader 继续保留，RGBA 图片选择独立
shader mode/program，并在采样后 premultiply。DisplayList/frame resource 容量不足在 backend 副作用前
整次 rollback；C 的产品采用、资源失效矩阵与 `Q/U/B` 性能证据见 [UI 框架设计](ui-framework.md)。

## GPU 资源

`IRenderDevice` 当前提供可选资源 API：

| API | Null | bgfx |
| --- | --- | --- |
| `create/destroyTexture2D` | 校验格式、色彩空间、sampler 与完整 mip chain；逻辑 generation storage；同步 retire | 映射 native format/sRGB/sampler flags，连续上传完整 mip blob；adapter 不支持的格式 fail closed；逻辑失效后 marker 延迟销毁 |
| `validateTexture2D` | 非消费式 owner/live/generation 校验；零突变 | owner-thread 非消费式 owner/live/generation 校验；零突变 |
| `retireTexture2D(texture, pin)` | 成功同步释放 pin | 成功消费 pin，readback marker 后释放 |
| `createTexture2DBinding` | device-instance allocator；bind 成功才消费非0 key | device-instance allocator；bind 成功才消费非0 key |
| `setTexture2DBinding` | 校验/记录 binding | device binding key → texture |
| `createStaticMesh` / `createSkinnedMesh` / `destroyGpuMesh` | 共享 `GpuMeshId` generation storage；同步 retire | 静态 tangent vertex stream 与可选 skin stream；逻辑失效后 marker 延迟销毁 |
| `retireGpuMesh(mesh, pin)` | 成功同步释放 pin | 成功消费 pin，readback marker 后释放 |
| `drainGpuRetirements` | 已完成，无操作 | owner-thread completion-only flush；有界失败返回结构化错误 |
| `createMesh3DBinding` | device-instance mesh allocator；成功才消费 key | device-instance mesh allocator；成功才消费 key |
| `setMesh3DBinding` | 校验/记录 binding | mesh key → GPU mesh |
| `createMesh3DMaterialBinding` | device-instance material allocator；成功才消费 key | device-instance material allocator；成功才消费 key |
| `set/clearMesh3DMaterialBinding` | 原子替换/整组清除三张纹理、factors 与显式 alpha mode | 原子替换/整组清除三张纹理、factors 与显式 alpha mode |
| `setMesh3DMaterialTextureBinding` | 校验/记录 binding | material key → base-color texture；Opaque3D MR submit **采样** `s_texColor`（默认 1×1 白） |
| `setMesh3DMaterialMetallicRoughnessTextureBinding` | 校验/记录 binding | material key → optional MR texture；未 bind 用默认 metallic=0/roughness=1 |
| `setMesh3DLighting` | 同步校验/复制低层 fallback | 未提供 frame-scoped Scene lighting 时使用；0..4 directional + 0..8 point + 0..8 spot lights + ambient |
| `set/createMesh3DMaterialBinding` 的 `emissiveFactorR/G/B` | 校验 finite 且非负，无上限 | 逐材质自发光 radiance，`u_emissiveFactor`；在 direct + ambient/IBL **之后相加**，不乘 `NdotL`、衰减、阴影或 ambient |
| `create/destroyShader` | 校验 kind/profile 表；逻辑 generation storage；同步 retire。Sprite2D 与 Mesh3D 均接受 | 一份 Mesh3D fragment binary 同时链接刚性与蒙皮 vertex stage；作者 uniform 上限 16；反射表 64 |
| `validateShader` | 非消费式 owner/live/generation 校验 | owner-thread 非消费式 owner/live/generation 校验 |
| `retireShader(shader, pin)` | 成功同步释放 pin，并清掉引用该 program 的 shader binding | 成功消费 pin；清掉引用该 program 的 shader binding，不碰独立的 uniform binding namespace |
| `createShaderBinding` / `setShaderBinding` | device-instance allocator；bind 成功才消费非0 key | device binding key → program；key 0 拒绝 |
| `setShaderUniformBinding` | 按名校验 finite vec4、去重、上限 16 | 独立 namespace；draw-local，每 batch 重发；未声明的名字 fail closed |
| `capturePrimaryFrameRgba8` | Unsupported | present 后异步截图路径；owner thread 有界推进最多120个 bgfx frame，并在轮询间给 render callback 1ms 调度窗口，超限返回 `FrameCaptureFailed` |

## Sprite2D 自定义 fragment

Sprite2D item 可携带 optional packet-local `shader` 与 `shaderUniforms` ref。空 shader ref 走引擎
`fs_tina_sprite2d_fixture`；非空 ref 必须在 submit 前解析到一个 live Sprite2D program，否则
`submitFrame` 以 `ShaderNotFound` / `InvalidFrameResource` 失败，**不会**回落到引擎 fragment。
这是刻意的不兼容：静默回落会让错误的 binding 画出“看起来正常”的精灵。

作者 fragment 必须 `#include <tina_sprite2d.sh>`。该头声明引擎拥有的 sampler/uniform 集；bgfx 按名
去重 uniform，用不同的类型再声明其中任何一个会破坏所有读它的引擎 draw。`shaderc` 在有该头时把
重定义报成编译错误，而不是运行期错乱。varying 行必须是源文件第一行：`shaderc` 在预处理器之前从
原文扫描 `$input`。

作者也可以声明自己的 sampler，但**register 不是自由选择**：`SAMPLER2D(name, N)` 的 N 就是硬件 stage，
D3D11/Vulkan 把它烘进二进制，而 GL 的宏直接丢掉 N、改由引擎按 stage 绑定 texture unit。所以引擎与作者
必须写同一个数：Sprite2D 引擎集占 0..1，作者第 N 个 sampler 必须落在 `2 + N`；Mesh3D 占 0..13，起点是 14
（`GpuShaderTextureStages`，`include/tina/render/RenderDevice.hpp`，cooker 与 backend 共用这一份）。

这条规则由 cook 时的源码检查把关（`parseShaderSamplerDeclarations` + `validateAuthorSamplerRegisters`，
`tools/assetc` 在跑 shaderc **之前**调用），不能靠 cooked 二进制仲裁：三个 profile 的 `regIndex` 语义互不相同
——GLSL 全写 0，SPIR-V 写 +2 偏移的 binding，只有 DXBC 是真 stage。检查覆盖三类“编译全绿但 GPU 采错槽”的
缺陷，每一条都实测过：

- register 与 stage 不一致（错一格就让 shader 采到引擎的贴图，或采到未绑定的槽——D3D11 上读回全黑）。
- 声明顺序与 register 顺序不一致。**DXBC 的 uniform 表按 register 排序，SPIR-V 保持源码顺序**，所以乱序声明
  在 D3D11 上正确、在 Vulkan 上两张贴图互换。位置式规则同时锁死了这一点。
- 声明了但从未采样。编译器会把它整条丢掉，backend 按反射表顺序编号 stage，于是它后面每个 sampler 都下移
  一格——而源码里 register 看起来完全连续。这是唯一一类“源码自洽却仍然错”的形状，所以 cook 直接拒绝。

register 写成宏（非十进制字面量）同样拒绝：那正是 cooker 无法证明两边一致的情况，放过去等于把不一致丢回
GPU。未 bind 的作者 sampler 拿到引擎的 1×1 白色兜底，而不是继承上一次 draw 的贴图。

作者 uniform 只有 `vec4`，按**名字**匹配，因为 cooked 二进制里的 uniform 顺序是 shaderc 细节。
每 draw 由 backend 重发当前 binding 的值；调用方停更就会冻在上次发布的数。引擎 lighting
（`u_spriteLightParams` 等）仍由 Sprite2D pass 提交，但自定义 fragment 不必消费它们——
`samples/2d_custom_shader` 的 `fs_pulse.sc` 就不乘 ambient。

Cook 走 `tina_assetc --shader-source`，产出带 profile 表的 Shader payload，而不是单个 `.bin`。
SDK 包把 `tina_sprite2d.sh` / `tina_mesh3d.sh` / varying def 装到 `Tina_SHADER_INCLUDE_DIR`，把
`bgfx_shader.sh` 装到 `Tina_BGFX_SHADER_INCLUDE_DIR`；两者都作为 `--shader-include` 根，与 in-tree
配方同形。这份清单由 `tests/sdk_consumer/VerifyInstalledTargets.cmake` 的
`tina_verify_installed_shader_include_dirs` 把关，而不是靠 in-tree 构建：`samples/2d_custom_shader` 与
`samples/3d_custom_shader` 在变量未设时会回落到 `${PROJECT_SOURCE_DIR}/src/render/bgfx/shaders`，
所以源码树里编译成功**不能**证明装出来的包够用——包一个文件都不装，in-tree 也照样全绿。Mesh3D 自定义 fragment 见下一节。后处理 `CustomShader` 步骤仍等 offscreen GPU 切片，
不是这条 Sprite2D 路径的一部分。

产品证据：`tina_sample_2d_custom_shader` 在两相 pinned `u_pulse.x` 上要求 custom 区域 RGB 均值差
`>= 8`、引擎对照区域差 `== 0`，并在对照精灵的四象限上断言 2×2 棋盘（红/绿/蓝/白）。对照精灵使用
`ambientScale=1` 的引擎 fragment，因为默认 `0.2` 会把 255 缩到 ~51，采样判据会在正确 UV 下失败。
`tina_sample_2d_shader_materials` 用同一 program、三套独立 uniform binding 证明 material 不是
“换一张图”，并额外把其中一个 material 的两个 value **倒序**发布来锁定“按名匹配”：`flatMaterialTexelDistance == 0`
要求它落在自己 UV 指向的纹素上，而按位取值会让 UV 出界钳到别的边缘色——同样平坦，所以
`flatMaterialSpread` 单独看对这类缺陷是失明的。

同一个判据也是自定义 sampler 的像素证据：`fs_tint.sc` 声明 `SAMPLER2D(s_mask, 2)`，只有那个 material 在
**同一个 binding key** 上发布一张 1×1 青色贴图（`setShaderTextureBinding`），于是它的期望色从 `(255,40,40)`
变成 `(0,40,40)`，`flatMaterialTexelDistance == 0` 仍然成立。另外两个 material 不发布贴图，拿白色兜底，
像素不变。已实测的反向对照：把 backend 的 stage 故意加一，stage 2 就没人绑，mask 采到全黑，整帧 material
区域塌成同色，`minimumMaterialSeparation` 从 43 掉到 0、`flatMaterialTexelDistance` 从 0 变成 26。
青色而非灰度：错绑时红色通道从 0 变回 255，是整条通道的差，而不是一个可能被容差吞掉的偏移。

一个 value binding 表带着“我的哪个值喂它那个 author uniform”的解析结果（`ShaderUniformBindingTable`
的 `valueIndices`），按 `ShaderSlot::authorUniformsRevision` 作废。缓存放在 binding 表侧而不是 program
侧，因为多对一的方向是 material→program：同一个 program 一帧内被多个 binding key 轮流绘制，缓存挂在
slot 上会在交错序列里每 draw 都 miss，比不缓存更慢；而一个 key 发布一次、之后被每个引用它的 draw 读取。

Scene 侧的入口是 `SpriteRenderer2D::shader`（一个 weak Shader `AssetHandle`）。extraction 要求
`shaderBindingResolver` 与 `shaderUniformBindingResolver` **成对**提供，并把同一个 handle 解析成
`FrameResourceKind::Shader` 和 `FrameResourceKind::ShaderUniforms` 两个 ref；任一缺失、解析为空或
asset 已 unload 都以 `SceneErrorCode::UnresolvedSprite` 失败，不回落引擎 fragment。uniform 值不放在组件里：
`Asset::ShaderBindingRegistry` 在注册 program 时就分配一个空 uniform slot（键来自与 shader binding
独立的 namespace），值经 `setShaderUniformValues()` 逐 shader asset 发布，所以一个 program 可带多套
material 而组件只需记住 handle。

### 自定义 sampler（device 层已落地，尚无产品装配点）

作者的 fragment 现在可以声明 `SAMPLER2D` 而不只是 `vec4`。纹理经
`IRenderDevice::setShaderTextureBinding(key, {samplerName, GpuTextureId}[])` 发布，key 与
`setShaderUniformBinding` **同一个 namespace**——一个 material 就是一个 key，数字和纹理各占一半，各自
用空表清除自己那半（清一半不动另一半，这条有单测钉住，因为它的失效是静默的：纹理被误删只会回落到默认
纹理而不报错）。

**stage 预算是硬约束。** bgfx 每 draw 只有 16 个 texture stage（`BGFX_CONFIG_MAX_TEXTURE_SAMPLERS`），
Sprite2D 引擎占 0..1，Mesh3D 引擎占 0..13（base color / MR / normal / CSM atlas / 三张 IBL /
spot shadow / 六张 point shadow face）。所以作者上限**按 kind 不同**：Sprite2D 8 个（受 binding 表
`MaximumValueCount` 而非硬件限制），Mesh3D 只有 2 个。超出在 upload 时 `InvalidShaderUpload` 拒绝——
接受它等于让多出来的 sampler 去读引擎刚绑在那个 stage 上的纹理。stage 在 upload 按反射顺序一次分配好，
不是每 draw 挑，否则会和引擎的固定分配撞车。常量在 `src/render/bgfx/BgfxCustomShader.hpp`，**新增引擎
sampler 必须同步改那里**：漏改不会编译失败，表现为作者纹理覆盖引擎 stage。

绑定时校验的是"这个 id 是本设备的活资源"，不只是形状：draw 阶段没有报错渠道，只能回落默认纹理。已发布
但随后被 retire 的纹理在 draw 时回落到该路径自己的默认纹理（Sprite2D / Mesh3D 各有一张），**不是**跳过
绑定——跳过会采到上一次 draw 留在那个 stage 的纹理。

**尚未声称：** 没有像素证据，也没有任何产品装配点或 Scene 侧入口调用它（`ShaderBindingRegistry` 只有
`setShaderUniformValues`，没有纹理对应物）。目前只有 Null 设备上的单测端到端跑过。

## Mesh3D 自定义 fragment

Mesh3D 与 Sprite2D 共用同一套 Shader payload / registry / extraction 接线，差别只在 vertex
stage 与引擎 uniform 集。`RenderMesh3DItem` 与 `RenderSkinnedMesh3DItem` 都携带 optional
packet-local `shader` / `shaderUniforms`。空 shader ref 走引擎 `fs_tina_opaque3d_mr`；非空
ref 必须在 `submitFrame` 前解析到 live Mesh3D program，否则以 `ShaderNotFound` /
`InvalidFrameResource` 失败，**不会**回落到引擎 fragment。蒙皮 item 不 batch，shader 挂在
item 上而不是 batch key 上。

作者 fragment 必须 `#include <tina_mesh3d.sh>`，`$input` 必须是源文件第一行，且必须与
`tina_opaque3d_mr.def.sc` / `tina_opaque3d_skinned.def.sc` 的 varying 集一致：
`v_color0, v_texcoord0, v_normal, v_worldPos, v_tangent`。两份 `.def.sc` 的 varying 相同，
bgfx 按 varying 名表的 murmur 链接，所以**一份** cooked Mesh3D fragment binary 同时链接刚性
与蒙皮 vertex stage，不需要 `GpuShaderKind::SkinnedMesh3D`。蒙皮 palette
（`u_tinaSkinPalette` 等）在 vertex stage，不进 fragment 契约头，也不从作者 uniform 表里扣。
CSM depth pass 仍用引擎 depth program，自定义 fragment 改不了 receiver 采样到的深度。

上传时一次反射、两次 `createProgram`；刚性成功而蒙皮失败则整次 upload 拒绝，避免蒙皮 draw
静默跑引擎 fragment。`liveResources` 按一次 upload 计一份，第二个 program 销毁时不另减。
作者 uniform 同样每 draw 按名重发，未提供的名字显式写零，避免继承上一批的值。引擎 Mesh3D
lighting（最多 4 个 directional、8 个 point、8 个 spot）仍由 pass 提交；Sprite2D lighting
是另一套上限（8 个 point、32 条阴影线段），不要混用。

Scene 入口是 `MeshRenderer3D::shader` / `SkinnedMeshRenderer3D::shader`。extraction 用与
Sprite2D **同一对** `shaderBindingResolver` / `shaderUniformBindingResolver`；失败码是
`UnresolvedMesh`。World2D snapshot 不序列化 3D 组件。后处理 `CustomShader` 与替换 vertex
stage 都不在本路径。

### cooked mesh 自带默认 fragment

cooked StaticMesh / SkinnedMesh 可以自己指定默认 Mesh3D fragment stage，这样一份网格不必靠每个
组件重复填 handle。wire 侧是 payload flag 加一条 cooked dependency 两者**必须同时存在**：

- `StaticMeshWire::SchemaVersion = 2` / `SkinnedMeshWire::SchemaVersion = 3`，
  `flags` 的 `FlagHasShaderOverride` 位在 payload offset 32。
- 同一个 cooked 文件必须带一条 `{AssetKind::Shader, DependencyFlags::Required}` dependency。
- 位置**不是**契约的一部分：`parseCookedAssetView` 要求整条 dependency 流按 AssetId 严格升序，
  所以 override 的下标由它的 id 决定，任何读取方都不能按顺序取"最后一条"。
  `Asset::readMesh3DShaderOverride`（`include/tina/asset/Mesh3DShaderOverride.hpp`）按
  `AssetKind::Shader` 选取，并双向校验：只有 flag 没有 dependency，或只有 dependency 没有 flag，
  一律 `InvalidCatalogConfig`。前者会让网格索要一个没人能解析的 fragment stage，后者会让 override
  对所有 payload reader 隐形却仍然 pin 住那个 Shader。它只做解析，不持有任何 lease——
  shader 的 lease / `GpuShaderId` / binding key 仍全部归 `ShaderBindingRegistry`，
  `Mesh3DBindingRegistry` 仍只拥有 mesh/material/texture。

cook 侧入口是 `staticmesh <id> cube [shader <shader32hex>]`；三 token 的旧形式语义不变。

extraction 侧是另一对**以 mesh handle 为 key** 的 resolver：
`mesh3DDefaultShaderBindingResolver` / `mesh3DDefaultShaderUniformBindingResolver`。key 是网格而
不是 shader，因为只有网格的 cooked dependency 知道它指了哪个 Shader。优先级是
**组件覆盖 cooked**：`MeshRenderer3D::shader` 非空就用它，此时这两个 resolver 完全不被调用；
为空才回落到 cooked 默认；两者都为空才走引擎 fragment。resolver 未设置不算失败（没有 cook 默认的
调用方保持原行为），但一对 ref 只解析出一半就是缺陷，返回 `UnresolvedMesh`——program 缺 uniform
槽会在 submit 期 fail closed，那里已经没法返回错误了。刚性与蒙皮走同一个解析函数，因为一份 cooked
fragment binary 同时链接两个引擎 vertex stage。

Texture2D cooked wire 当前为 schema v2。格式集合固定为 `Rgba8Unorm`、BC1/BC3/BC7 与 ASTC 4x4；
色彩空间是 Linear 或 sRGB；sampler 支持 Repeat/Mirror/Clamp/Border、Point/Linear，以及必须同时用于
min/mag 的 Anisotropic。公开契约不提供逐纹理的数值 anisotropy 上限，因为 bgfx 只能兑现 sampler enable
与 device-wide maximum。单级纹理必须使用 `mipFilter=None`；多级纹理必须从 base level
连续减半到 1x1，并选择 Point 或 Linear mip filter。Asset parser 同时核对 cooked header 的 type version，
因此旧 v1 payload/type version 不走兼容分支。

这里的“支持”是 schema、typed reader、Asset→Render 映射与 Null/bgfx upload 能消费调用方提供的完整数据，
不表示生产 cooker 已会生成高级数据。当前 `CatalogCook` 的 `texture2d` recipe、MediaCook、GltfCook 与 assetc
仍只写 Rgba8 单 mip；glTF 仅正确区分 base-color sRGB 和 normal/metallic-roughness Linear。压缩/transcode、
mip 生成和 source sampler authoring 仍属于 ASSET-TEX-002 的剩余工作。

`createTexture2DBinding()` 分配的 key 单调且解绑后不复用；backend bind 失败不消费候选 key。
caller-chosen `setTexture2DBinding()` key 与 allocator-managed key 共用 device namespace，registry
管理期间不得混用。

Mesh3D mesh/material 分别使用独立的 device-instance allocator namespace。两类 key 都从2开始并分别保留
内置 mesh/material key 1。两类 key 都只在完整 backend bind 成功后消费，解绑后不复用；
caller-chosen setter 与同类 allocator-managed key 不得混用。`setMesh3DMaterialBinding()` 先完整校验三张
可选纹理与 factors，再原子替换整组状态；`clearMesh3DMaterialBinding()` 幂等清除整组状态。

`GpuTextureId`/`GpuMeshId` 是 backend owner-scoped generation handle，不是 AssetHandle。wrong-owner 与销毁后
stale handle 都失败；Asset Catalog 使用 `AssetId`。2D extraction resolver 把 live Sprite/Tileset handle 映射并 intern 为
packet-local ref；3D Prefab 先映射 weak AssetHandle，Mesh3D registry 再把 live mesh/material binding intern
为 packet-local ref。backend 只在同步 submit 中解析 descriptor 内的 device key，再映射到 GPU handle。
`validateTexture2D()` 不消费候选 handle；成功只证明其 owner/index/generation 当前能在目标 device 的
Texture2D storage 中解析，失败不改变 generation storage、binding 或 retirement 状态。

`Mesh3DBindingRegistry` 是 3D resident owner：Mesh entry 持有 `AssetLease`、`GpuMeshId` 与 binding；Material
entry 持有 `AssetLease` 与 binding；Texture entry 按 `AssetId` 唯一持有共享 `AssetLease`/`GpuTextureId`，
并以 material reference count 阻止过早 retirement。geometry/material ref 的首次 intern 持有 entry borrow
pin；active packet 结束前拒绝对应 retirement。Mesh/Texture 通过 lease-consuming AssetSystem transaction
交给 backend retirement，Material 先清除原子 bundle 再 logical unload。调用方不再保留 registered flag、
第二份 GPU cleanup 账簿或持久 binding key。

`UploadTicketLedger` 与 `CpuSubmissionCompletionLedger` 仍分别表达 staging 与 CPU completion。GPU 资源
retirement 不复用它们：`destroy*` 是无外部 pin 的便利入口，`retire*` 成功才转移 pin；
`RenderStatistics` 分别报告 `pendingGpuRetirements/completedGpuRetirements`。因为 upload ticket 只管
staging 所有权，它没有失败状态可报（`submit()` 之后没有可失败的环节）；`UploadTicketState::Failed`
是那套被取代的 fence 设计的残留，已于 2026-08-28 删除。

bgfx retirement marker 使用 1×1 source texture 与 `BLIT_DST | READ_BACK` destination texture；在最后一个
view 提交 blit/readback，仅以 `readTexture()` 的 ready frame 判断完成。active/suspended/capture/drain 都会
推进同一 timeline；uint32 frame wrap 使用半区间比较。缺少 blit/readback capability 时，带外部 pin 的
请求在任何 generation/binding/native slot 变化前返回 `GpuRetirementUnsupported`，且不消费 pin；无外部
pin 的 destroy 则立即使逻辑 handle/binding 失效并把 native handle 交给 `bgfx::destroy` 的 backend-owned
deferred destruction，不进入 marker timeline。只有已经进入 marker timeline、但有界 drain 未完成的外部
pin，才以 `bgfx::shutdown()` 返回作为 hard completion fallback。

## 后处理与 offscreen（RenderPostProcess）

`RenderPostProcessChainView` 是 `RenderFrame` 上的可选 offscreen/HDR 图，与 `primaryWorldScene`
遵循同一条 submit-call-local 借用契约。它承载 HDR scene target、ping/pong、offscreen pass、decal、
fog、bloom、tone mapping 与自定义 step。

**核心 pass 枚举保持冻结。** `RenderPassKind`（Clear / 三种 shadow depth / Opaque3D / Transparent3D /
Sprite2D / UI）不因新增效果而改变；后处理由**另一个**枚举 `RenderPipelinePassKind` 承载，
`buildRenderPipelineSchedule()` 只规划扩展 pass。顺序固定为
offscreen → decal → fog → bloom（prefilter / downsample×N / blur / upsample）→ 自定义 step →
tone mapping → UI composite。bloom 在 ping/pong 间交替，故没有任何一步读写同一张贴图。

**`enabled()` 不把 tone mapping 计入。** `ToneMappingDesc::operation` 默认是 `AcesFitted`，而每个
`RenderFrame` 都携带一个默认构造的 chain；若默认 operator 参与判定，则**任何**从未提到后处理的帧
都会被报告为「请求了后处理」，随后因为没有 scene color target 而无法通过自身校验。tone mapping 是
offscreen 渲染的一个**阶段**而不是独立开关，只有别的东西先 opt in 时才生效。同理，
`buildRenderPipelineSchedule()` 对未启用的 chain 直接返回空 schedule。

**Binding key 0 永远表示主 surface**，不可被 `setRenderTextureBinding()` 占用；这让 chain 用 0 表达
「输出到 surface」而无需额外 flag。chain 引用的每个非零 key 必须在使用它的那一帧之前完成绑定，
否则 submit 返回 `RenderTextureNotFound`。校验与绑定解析都发生在任何帧状态推进之前，因此被拒绝的
帧不消费 frame index。

**当前后端支持面：**

| 后端 | 行为 |
| --- | --- |
| Null | **消费内建 step**：创建/绑定/销毁 render texture、校验 chain、构建扩展 schedule，并对一个 1×1 探针像素执行共享 reference math（`toneMapLinearColor`/`bloomPrefilterLinearColor`/`applyFogToLinearColor`），结果计入 `postProcessChainsExecuted` 与 `postProcessPassesPlanned`。**`CustomShader` step 例外**：shader binding 尚未进入 Null device SPI，任何 `CustomShader` 在帧状态推进前即返回 `RenderTextureUnsupported`（`src/render/null/NullRenderDevice.cpp:1547-1554`），因此 Null 后端**不能**用来验证自定义 shader step |
| bgfx | **显式 fail closed**：非空 chain 返回 `RenderTextureUnsupported`。GPU 实现（offscreen framebuffer、HDR 格式协商、bloom mip 链，以及 `CustomShader` 所需的 shader binding）是独立切片 |

bgfx 选择报错而非静默忽略：静默忽略会让调用方设置完整 HDR 链、得到 `success()`、却既看不到变化
也收不到错误。这正是 ADR 0030 所禁止的「payload 必须被消费或显式拒绝」。一个诚实的错误比一个
看起来成功的空操作有用。

## bgfx backend

当前私有 backend 已实现：

- native WindowSurface 初始化、resize/suspend、submit/present/shutdown；
- transient frame budget 与容量失败；
- Sprite2D textured quad pass + frame-scoped 0..8 point lights；
- Opaque3D/Transparent3D metallic-roughness Cook-Torrance GGX mesh pass 与 opaque-only shadow depth pass（优先消费 frame-scoped 0..4 directional + 0..8 point + 0..8 spot lights；可选 split-sum IBL；每帧只编码一次 uniform arrays）；
- 确定性 `Clear -> CascadedDirectionalShadowDepth[0..3] -> SpotLightShadowDepth -> PointLightShadowDepth[0..5] -> Opaque3D -> Transparent3D -> Sprite2D -> UI` scheduler；三类 shadow resource 使用 startup-only 配置 extent，且都不取得 primary-surface clear ownership；
- UI solid/glyph pass；
- Texture2D/GPU mesh/EnvironmentMap generation storage、静态与蒙皮 vertex layout 及 key binding；
- Texture2D/GPU mesh/EnvironmentMap backend-proven retirement marker、suspend flush 与 shutdown hard drain；
- present 后 primary framebuffer capture；
- D3D11/OpenGL/Vulkan 对应 embedded shader 选择（按构建与平台可用性），可选加入 OpenGLES。

### Embedded shader profile 与 OpenGL ES

6 个 program（含 skinned 的 vertex-only 与 CSM depth）共 11 个 shader 二进制，由
`cmake/TinaBgfxEmbeddedShaders.cmake` 的 `tina_bgfx_shader_profiles()` 统一决定 profile 集合，
`src/render/bgfx/*Shader.cpp` 的 4 张表按后缀引用对应 header。默认 `glsl`(120) + `spv`(spirv)，
Windows 额外 `dxbc`(s_5_0)。

后缀不是自选的：bgfx 自己的 `bgfxToolUtils.cmake` 在 `_bgfx_get_profile_ext()` 里给出
`120→glsl`、`100_es`/`300_es`→`essl`、`spirv→spv`、`s_5_0→dxbc`、`metal→mtl`，注释标明
"consistent with embedded_shader.h"。加 Metal 时用 `mtl`。`spv` 固定用 `platform=linux`，与 bgfx
`bgfxToolUtils.cmake:650` 对 spirv 强制 LINUX 的做法一致，不要"顺手"改成目标平台。

`TINA_RENDER_BGFX_MOBILE_SHADERS=ON` 额外 cook `essl`(300_es, platform=android) 并在 4 张表里启用
`RendererType::OpenGLES` 条目（Android 与 iOS GLES 都报这个 renderer，一份二进制覆盖两者）；同一个
`if` 同时设置 option 与编译宏 `TINA_RENDER_BGFX_MOBILE_SHADERS`，因此表不可能引用 cook 步骤没产出的
header。该选项在桌面上也可开启，这是刻意的——否则 ESSL 分支只会被没人运行的工具链编译，正是已删除的
`cmake/ShaderUtils.cmake` "假装 Metal/GLES 已就绪"的成因（[ADR 0032](adr/0032-mobile-platform-contract-boundaries.md) 第 5 节）。

**未采用 bgfx 的 `bgfx_compile_shaders()`**：它按 profile 分子目录输出（我们的表按后缀平铺引用，且
`100_es`/`300_es` 会撞同一个 `essl` 目录）、按**宿主**平台选 profile（而 C5 要的正是在 Windows 上 cook
android essl）、并把 `-O` 绑定到 CMake 配置（会改动 Debug 的 shader 二进制与产品像素指纹）。只采纳其命名
权威。

这一步只证明**编译与查表**：ESSL 二进制能产出、表能命中。GLES 上的实际绘制仍需真机或模拟器。

`tina_sample_2d` 已使用 Cooked Texture2D 与产品 sprite binding；`tina_sample_3d` 已使用 Cooked
StaticMesh/Material/Prefab 的 **双 mesh** engine-provided、State-owned registry binding（3D-001 / N15），
N16.4 再统一其 Lease/GPU/binding owner 与 packet-local resource ref；Material 通过原子 bundle 提交
baseColor/MR/normal/factors；当前 Scene `DirectionalLight3D`/`PointLight3D`/`SpotLight3D` 每帧提取到
RenderScene snapshot，point/spot influence sphere 在容量检查前按 PerspectiveCamera3D frustum cull；产品
Catalog 另加载 deterministic RGBA16F/RG16F EnvironmentMap 并绑定 split-sum IBL。complete-PBR fixture 的 NORMAL+UV primitive 由 Cooker 生成
MikkTSpace tangent 并走唯一 P3N3T4UV2 upload；缺少 NORMAL 或 TEXCOORD_0 的 primitive 在 Cooker 阶段显式失败。

## Surface 与线程

RenderDevice 由创建线程拥有。WindowSurface lease 在 Platform 创建窗口后交给 Render factory，Render
成功后 Platform 才发布窗口。每帧 surface/metrics revision 必须单调一致；suspended frame 不调用
present。

同一 Visual Studio build tree 的 Debug/Release 构建串行执行；不允许两个 GPU sample 同时复用同一
固定证据目录。截图通过只证明特定 backend/尺寸的画面，不替代结构化资源账本和生命周期测试。

## 验证

```powershell
cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_tests tina_render_scene_tests tina_render_bgfx_tests `
           tina_ui_render_integration_tests tina_sample_2d tina_sample_3d --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_bgfx_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
```

测试映射与 Visual 证据规则见 [测试说明](testing.md)。`RENDER-001` 已落地 Cook-Torrance GGX、
MR/normal/factors、有界4 directional + 8 point + 8 spot-light 描述、Scene component 到逐帧 RenderScene snapshot，
以及 cooked diffuse/specular/BRDF split-sum IBL，
以及 authored/MikkTSpace P3N3T4UV2 tangent 到 signed-scale-correct TBN 的完整路径；
产品 sample 的3个 directional entity，以及 PointLight3D/SpotLight3D 各自
authored/committed/culled=`3/2/1` 连续300帧稳定提交，并由首个可见 SpotLight 与 PointLight 分别提交固定
单灯 shadow。product-3d schema 14 固化两类 authored/submitted=`1/1`，PointLight shadow 另由 on/off
中央 3D RGB ROI fingerprint 与正逐像素 L1 差分证明实际影响像素。startup-only shadow extent 配置已闭环；
通用 GPU submission fence 与跨 DPI/GPU visual gate（`UI-003`）仍后置；Texture/Mesh/EnvironmentMap
resource retirement 已不属于这些后置项。
