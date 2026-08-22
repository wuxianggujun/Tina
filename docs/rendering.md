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
- optional R8 `UIGlyphAtlasPageView`。

所有 view/span/pixel pointer 只在 `IRenderDevice::submitFrame()` 调用期间有效。backend 必须同步消费，
返回后不能保存。`submitFrame()` 返回 `Submitted` 或 `SkippedSuspendedSurface`；只有前者允许 Runtime
调用 `present()`。

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
映射为 `Mesh3DCascadedDirectionalShadow`，以排序后的 `directionalLightIndex` 关联灯光。bgfx 使用独立
2×2 D16 `DirectionalShadowAtlas`（默认2048×2048、每 tile 1024×1024），并执行 camera-slice projection
与每级联3×3 PCF；级联 pass 不消费
primary-surface clear ownership。最多一个 camera-affecting spot light 可携带 optional `SpotLightShadow3D`；
它以正且小于 influence radius 的 near plane、depth bias 与 normal bias 描述。Render snapshot 在 spot lights
稳定排序后映射 `spotLightIndex`，bgfx 在四个 CSM pass 后执行一个 sampled D16 depth pass（默认1024×1024），
receiver 仅对匹配的 spot slot 应用3×3 PCF。最多一个 camera-affecting point light 可携带 optional
`PointLightShadow3D`；它使用同样有界的 near/depth/normal bias，并在 point lights culling 与稳定排序后映射
`pointLightIndex`。bgfx 拥有六张相同尺寸的 sampled D16 map（默认512×512），按 `+X/-X/+Y/-Y/+Z/-Z` 提交六个
depth pass；receiver 以 dominant axis 选择面，只对匹配的 point slot 应用3×3 PCF。固定顺序为
CSM×4 → Spot×1 → Point×6 → Opaque3D → Transparent3D → Sprite2D → UI，所有 shadow pass 都不消费 primary-surface clear。第二个可见
point shadow 或非法 near/depth/normal bias 在 Scene publish 前 fail closed。

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

`EngineConfig::renderMsaaSamples` 同样是 device-lifetime 启动配置：`0`（默认，关闭）或 `2/4/8/16`，
其余值在 EngineConfig 校验与 bgfx factory 双双 fail closed。它映射为 backbuffer 的
`BGFX_RESET_MSAA_X*` flag，由 init 与 resize reset 共用。samples 与所有像素证据 gate 保持 `0`
以免破坏已冻结的视觉金标；`TinaEditor` 以 `8` 启动来抗锯齿 world pass（mesh 边缘与 sprite 采样）。

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
flat normal 相对 Lambert factor 精确为1。当前仍无 HDR/tone mapping。product-2d schema 29 继承 schema 19，并以
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
  -> validate, cull, stable sort and batch
  -> commit borrowed view
```

当前能力包括：

- Camera2D projection、viewport、pixel snap 与 world picking；
- Sprite2D 视锥裁剪、layer/order/ordinal 稳定排序、相邻 `(baseTexture, normalTexture)` batch；
- optional self-contained Sprite2D lighting snapshot、最多8个 committed point light、32个 shadow segment 与 ambient；
- PerspectiveCamera3D、point/spot/static/skinned sphere-frustum culling、Opaque3D stable batch，以及 static/skinned 统一 Transparent3D back-to-front draw 序列；
- optional self-contained Mesh3D lighting snapshot、可选 CSM/SpotLight shadow 描述、重复设置/非法描述的事务失败与统计；
- framebuffer 0x0 suspended 路径；
- 容量/非法数值/非法 resource ref 失败时不发布半份 scene。

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
| `create/destroyTexture2DRgba8` | 逻辑 generation storage；同步 retire | 私有 RGBA8 texture；逻辑失效后 marker 延迟销毁 |
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
| `capturePrimaryFrameRgba8` | Unsupported | present 后异步截图路径；owner thread 有界推进最多120个 bgfx frame，并在轮询间给 render callback 1ms 调度窗口，超限返回 `FrameCaptureFailed` |

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
`RenderStatistics` 分别报告 `pendingGpuRetirements/completedGpuRetirements`。

bgfx retirement marker 使用 1×1 source texture 与 `BLIT_DST | READ_BACK` destination texture；在最后一个
view 提交 blit/readback，仅以 `readTexture()` 的 ready frame 判断完成。active/suspended/capture/drain 都会
推进同一 timeline；uint32 frame wrap 使用半区间比较。缺少 blit/readback capability 时，带外部 pin 的
请求在任何 generation/binding/native slot 变化前返回 `GpuRetirementUnsupported`，且不消费 pin；无外部
pin 的 destroy 则立即使逻辑 handle/binding 失效并把 native handle 交给 `bgfx::destroy` 的 backend-owned
deferred destruction，不进入 marker timeline。只有已经进入 marker timeline、但有界 drain 未完成的外部
pin，才以 `bgfx::shutdown()` 返回作为 hard completion fallback。

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
- D3D11/OpenGL/Vulkan 对应 embedded shader 选择（按构建与平台可用性）。

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
