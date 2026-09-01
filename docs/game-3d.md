# 3D 产品架构

`tina_sample_3d` 是当前最小 3D 产品门禁：默认在运行时生成双静态 mesh complete-PBR glTF fixture，
并额外生成一个独立的 SkinnedMesh/AnimationClip3D witness，经 cgltf Cook、Catalog 发布与校验、
StaticMesh/SkinnedMesh GPU upload、Prefab 实例化、Scene extraction 后由 bgfx Opaque3D pass 绘制；产品
还 cook 一份独立 `Blend` Material，并以复用 static mesh 的两个交叠实体接入 Transparent3D pass，最后叠加
可交互、可换肤的 retained UI。它证明 multi-mesh、骨骼与透明 witness 的产品接线，不等同于完整 3D 渲染器。

也可通过 CLI 加载**磁盘上的外部** `.gltf`/`.glb`（用户模型）：同样只走 cooker，Runtime 不解析源
glTF。默认产品门禁使用仓库 **complete PBR fixture**
（`tests/fixtures/gltf/complete_pbr/complete_pbr.gltf`：双 mesh、NORMAL/UV、MikkTSpace 生成 tangent、
baseColor+MR+normal 贴图、不同 metallic/roughness）；未编译进 fixture 路径时回退到最小内建 glTF。
外部 `--gltf=` 仍为 opt-in。

Cooker 与产品 sample 支持一个 glTF 中多个 mesh（sample 槽位上限 128）：distinct Mesh/Material
AssetId、registry 分配的独立 binding、Prefab 每节点 resolver、extract/draw 与 ledger 归零。外部 URI 安全与
产品 Material texture owner/binding 已完成（ASSET-001）。Cooked Material v2 携带 metallic/
roughness factor、可选 MR/normal Texture2D dependency 与显式 `Opaque`/`Blend` alpha intent；Runtime 不从
baseColor alpha 或纹理内容猜测 pass。bgfx Opaque3D/Transparent3D 在 submit 时采样 baseColor，以
Cook-Torrance GGX 计算 direct light，并消费 cooked diffuse/specular/BRDF split-sum IBL。`Scene::World` 已提供 `DirectionalLight3D`、`PointLight3D`
与 `SpotLight3D`，每帧 extraction 把最多4个 directional、8个 camera-affecting point 与8个
camera-affecting spot component 按稳定 Entity identity 排序并复制为 self-contained `RenderScene` lighting
snapshot；point/spot influence sphere 在容量检查前按 PerspectiveCamera3D frustum cull。`tina_sample_3d`
创建3个 directional、3个 point 和3个 spot light entity（point/spot 均为2个提交、1个裁剪）、使用 ambient `0.16`，并在 prefab instantiate 后按
mesh AABB **自动框定相机**。首个 directional light 启用固定4级联 CSM，稳定排序后的首个可见 spot light
启用固定单灯阴影，首个可见 point light 启用固定六面全向阴影；`IRenderDevice::setMesh3DLighting()` 仍是
低层 direct/fallback SPI。三类 shadow resource extent 由 startup-only `EngineConfig::shadowMapExtents`
配置，默认 directional cascade tile/spot map/point face 为 `1024/1024/512`。

## CLI（`tina_sample_3d`）

```text
tina_sample_3d [--frames=N] [--frame-delay-ms=N] [--gltf=<path>|--gltf <path>]
               [--width=N] [--height=N]
               [--ui-theme=dark|light] [--ui-theme-demo]
               [--ibl=on|off] [--point-shadow=on|off]
               [--skin-animation=on|off] [--transparency=on|off] [--help]
```

| 标志 | 含义 |
| --- | --- |
| `--frames=N` | N 帧后退出（默认 300） |
| `--frame-delay-ms=N` | 每帧 sleep（默认 0） |
| `--width=N` / `--height=N` | 初始 logical client 尺寸（默认 1280×720）；用于非16:9与响应式布局验证 |
| `--gltf=<path>` / `--gltf <path>` | 从磁盘 cook 外部 `.gltf`/`.glb`；省略则用内建双 mesh fixture |
| `--ui-theme=dark|light` | 选择初始产品 Theme（默认 Dark） |
| `--ui-theme-demo` | 在 UI phase 自动执行 initial→alternate→initial，并执行2次 collection step；要求 `--frames>=3` |
| `--ibl=on|off` | 是否绑定已上传的 cooked EnvironmentMap（默认 on；off 仍完成上传与 retirement） |
| `--point-shadow=on|off` | 是否为首个可见 PointLight author 固定全向阴影（默认 on） |
| `--skin-animation=on|off` | 推进或暂停产品 skin witness 的 `Animator3D`（默认 on；off 仍逐帧 update/extract/submit） |
| `--transparency=on|off` | 创建或省略复用首个 static mesh 的两个交叠 `Blend` witness（默认 on；off 仍 cook/load/bind/retire 独立 Blend Material，也不改写外部 glTF 自带的 `BLEND` primitive） |
| `--help` / `-h` | 打印用法 |

失败（文件不存在、扩展名非法、cooker 不支持的 Draco、CUBICSPLINE、非法/超限 skin 或 animation 等）在 stderr 输出结构化 JSON：
`status=error`、`code`、`message`、可选 `context[]`。成功 stdout JSON 含 `gltfCooked`、
`externalGltf`、`meshSlotCount`、`multiMesh` 等。

## Retained UI 与换肤

`samples/3d_product/Product3DUI.*` 独立拥有产品 UI root。1280×720 是 reference layout；窗口变宽/变高时
右侧 inspector/collection rail 保持右边距，collection/list/tree 纵向扩展，底部状态栏保持底边距并横向扩展。
字体和控件继续使用 logical pixel，不随窗口 client 尺寸做全局 zoom。页面提供：标题与 PBR 元信息、
Theme Button、Auto Rotate Checkbox、Rotation Speed Slider、逐帧
ProgressBar、Asset ListView、Scene TreeView 和底部状态条。Slider 与 Checkbox 直接控制模型旋转状态，
List/Tree 展示真实产品数据，不是装饰性控件。

标准 Button/Checkbox/Slider/ProgressBar 保持 create-time Theme 继承；标题、面板、accent 与状态文字是
有意的局部层级覆盖，由 `applyTheme()` 集中重算。交互 callback 只记录 pending intent，实际 Theme、
Checkbox、Slider 与 ProgressBar 提交统一发生在 `updateUI()`，避免事件路由期间重入 retained tree。
`--ui-theme-demo` 在产品门禁中执行 Dark→Light→Dark 和2次 collection step；当前已通过 gate 输出 schema 16，
并继承 schema 15 的
两次换肤、最终 Dark、继承 chrome、7 Panel/13 Label、ListView/TreeView 各1个、Tree expansion
changes `2`、最终 stable keys `2003/4`、progress 终值、root 释放，以及300帧 Scene lighting publication；
lighting 证据固定3个 directional light，PointLight3D 与 SpotLight3D 均为
authored/committed/culled=`3/2/1`，固定 SpotLight/PointLight shadow 均 authored/submitted=`1/1`，且提交描述跨帧稳定；同时要求2个 complete-PBR mesh 均以
P3N3T4UV2 上传；
同时记录 logical/framebuffer extent、窗口 metrics event、最终提交相机 aspect 与 responsive UI authoring。
骨骼 witness 还要求 `cookedSkinnedMesh=true`、`cookedAnimationClip3D=true`、
`animatorJointCount=2`、`skinnedPrefabInstances=3`、`submittedSkinnedMesh3DCount=1`、
packet palette joint count=`2`，以及 `--skin-animation=on|off` 的 pose/pixel A/B 证据。
schema 16 另增加透明模式、Blend material、authoring/submission count 与统一排序 checksum；2026-08-15
集中 Windows gate 已通过，透明 on/off RGB fingerprint 不同、逐字节 L1 达到 ROI 门槛且 raw capture 已回收。

完整 Windows 同轮门禁使用 FreeType 图，直接运行模块测试而不是 CTest：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunProduct3dGate.ps1 `
  -OutJson artifacts\gates\product-3d.json
```

最新人工视觉证据位于
`artifacts/screenshots/3d-product-ui-freetype-dark-fixed/20260729-003922/frame-02.png` 与
`artifacts/screenshots/3d-product-ui-freetype-light-fixed/20260729-004012/frame-01.png`：两张 1280×720
FreeType client capture 中实际双 mesh、集合控件、文字和主题层级清晰，动态进度 `10%`/`1%` 完整，
面板无裁剪或重叠，3D 主视区未被遮挡。该证据同时回归 bgfx mutable glyph atlas 的运行时增量上传。

仓库附带的外部样本（Khronos Sample Models，CC 许可见各目录 README）：

```powershell
# PBR 球体网格（~123 mesh，无贴图，测 metallic/roughness factors）
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0 `
  --gltf=tests\fixtures\gltf\metal_rough_spheres\MetalRoughSpheresNoTextures.glb

# 单盒简单模型
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0 `
  --gltf=tests\fixtures\gltf\box\Box.glb
```

默认产品门禁（complete_pbr）：

```powershell
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
```

## 当前模块边界

```text
source glTF/GLB
  -> Tina::Asset cgltf Cooker
  -> CatalogCookRequest
  -> manifest.tmnft + Cooked StaticMesh/SkinnedMesh/AnimationClip3D/Material/Prefab
  -> typed Cooked views
  -> AssetSystem weak AssetHandle/Lease + RenderDevice mesh/texture/environment upload
  -> Mesh3DBindingRegistry owns Mesh/Material/shared Texture bindings
  -> Prefab AssetId-to-AssetHandle resolver
  -> Scene::World
       PerspectiveCamera3D
       MeshRenderer3D
       SkinnedMeshRenderer3D + Animator3D CPU pose
       DirectionalLight3D + PointLight3D(optional shadow) + SpotLight3D(optional shadow)
  -> extraction AssetHandle-to-FrameResourceRef resolver + pose provider + explicit alpha intent
  -> RenderScene cull + opaque batch + unified static/skinned transparent back-to-front order
     + packet-local skin palette + frame-scoped lighting snapshot
  -> bgfx Opaque3D -> Transparent3D Cook-Torrance GGX + split-sum IBL + GPU skinning -> UI
```

- cgltf 只在 `tina_asset` 的私有 Cooker 实现中出现；Runtime 不解析源 glTF；
- `Scene::World` 保存 Transform、Camera、DirectionalLight3D、PointLight3D、SpotLight3D 和 weak AssetHandle，不保存 backend key、bgfx/GPU handle；
- RenderScene 不回查 World，提交时只消费已 commit 的 Camera、mesh item、batch 与 lighting snapshot；
- `tina_render_bgfx` 私有拥有 shader、vertex/index buffer、view id、uniform 与 texture binding；
- 普通 Game API 不暴露 cgltf、bgfx 或 native surface 类型。
- 3D-SKIN-001 A1-A4 的 wire/cook/typed、Scene `SkinnedMeshRenderer3D`/`Animator3D`、CPU pose、
  packet-local palette 与 bgfx GPU skinning 已于2026-08-14通过统一产品 gate（含 on/off RGB 差分）。

## 组合入口

产品 3D sample 经 `Tina::Desktop::CreateEngine(config, options)` 启动（与 2D 一致），不再手写
`EngineCompositionFactories`。mesh/texture bind 证据通过
`CreateEngineOptions::wrapWindowSurfaceRenderDevice` + `samples/3d_product/DeviceCapture.hpp`。
EngineHost 仍是唯一组合根。

## 已实现能力

| 层 | 当前实现 |
| --- | --- |
| Cooker | glTF 2.0 JSON/GLB；每个 primitive 为 TRIANGLES；POSITION/NORMAL/TEXCOORD_0 必需，TANGENT 可选；authored TANGENT 优先，否则以 MikkTSpace 生成；multi-mesh 与 **multi-primitive SPLIT** 输出 distinct AssetId；带 skin 的 primitive 还要求匹配 JOINTS_0/WEIGHTS_0，按固定4 influence 归一化为 U16 权重，并 cook joints/bind TRS/inverse bind；animation 只接受 joint target + LINEAR/STEP，非法或超限 fail closed；scene node 转 Prefab hierarchy/dependency |
| Cooked 数据 | StaticMesh v1 固定为 P3N3T4UV2、UInt16 index、bounds/submesh；SkinnedMesh v1 复用该 vertex/submesh/index layout，并内嵌最多256 joints、inverse bind 与每顶点4个定点权重；AnimationClip3D v1 为规范化 joint/channel track 表与 times/values blocks（768 tracks、4096 keys/track、262144 total keys、1048576 floats、3600s）；Material v2 保存显式 `Opaque`/`Blend`，Prefab v4 与 EnvironmentMap v1 保持现有契约 |
| Scene | `PerspectiveCamera3D`、带显式 alpha intent 的 `MeshRenderer3D`/`SkinnedMeshRenderer3D`、`Animator3D` CPU pose、`DirectionalLight3D`、`PointLight3D`、`SpotLight3D`、Transform hierarchy、Prefab 实例化与失败回滚 |
| Extraction | 唯一 active perspective camera、surface aspect resolve、world bounds、frustum culling；Opaque static item 做稳定相邻实例 batch，Blend static/skinned draw 进入统一 back-to-front 全序，等距以 stable Entity identity/kind/item index 决定；透明容量不足使整次 build 事务失败；最多4个 active directional、8个 camera-affecting point 与8个 camera-affecting spot lights 按稳定 Entity identity 排序，point/spot influence sphere 在容量检查前裁剪；最多一个 optional `CascadedDirectionalShadow3D`、一个 camera-affecting `SpotLightShadow3D` 与一个 camera-affecting `PointLightShadow3D`，分别在稳定灯光排序后映射 Render light index 并复制进当前帧 snapshot |
| bgfx | deterministic pass schedule、color/depth clear、独立2×2 D16 directional shadow atlas（默认每 tile 1024×1024）、默认1024×1024 D16 spot shadow map 与按 `+X/-X/+Y/-Y/+Z/-Z` 排列的六张默认512×512 D16 point shadow map；三类尺寸均由 startup-only 配置驱动，receiver 使用对应 texel size 的3×3 PCF，pass 顺序固定为 CSM×4→Spot×1→Point×6→Opaque3D→Transparent3D→Sprite2D→UI；Opaque 写 depth，Transparent 使用 straight-alpha blend、depth test less 且不写 depth；透明 static/skinned 不进入 shadow caster pass，但继续接收 lighting、shadow、PBR 与 IBL；Perspective view、back-face culling/double-sided、内置 tangent Cube fixture（`meshKey=1` 未 bind 时）或显式 GPU mesh binding、Cook-Torrance GGX **采样** baseColor（`s_texColor`）、可选 MR 贴图（`s_texMR`）与 normal 贴图（`s_texNormal`），并以 diffuse irradiance + roughness LOD prefiltered specular + BRDF LUT 合成 IBL；唯一 P3N3T4UV2 使用 authored/generated tangent TBN 并修正 signed model scale；当前帧 Scene lighting 覆盖 device fallback，uniform arrays 每帧编码一次并供所有 mesh draw 复用 |

世界坐标为右手、Y-up、局部 `-Z` forward、单位米。Camera 公共字段使用 degree，并要求
`0 < near < far`、有限数值和有效 normalized viewport；aspect 每帧从 primary surface 解析，不写回
Camera component。

当前 `MeshRenderer3D`/`SkinnedMeshRenderer3D` 保存 copyable weak mesh/material `AssetHandle`、`submeshIndex`、
bounds、base color、显式 `Mesh3DAlphaMode` 与可见性，不保存 backend key、Lease、Cooked payload 或 GPU owner，
也不从 `baseColorFactor.alpha` 推导 pass。每次 extraction 分别借用 mesh/material
resolver，把 live handle intern 为当前 packet 的 `Mesh3DGeometry`/`Mesh3DMaterial` ref；hidden mesh 不解析，
空/stale/cross-store/wrong-kind/unbound handle 或空结果统一 fail closed 为 `UnresolvedMesh`。

## Prefab 与资源解析

Cooker 为每个 glTF mesh 建立 StaticMesh 或 SkinnedMesh 和 Material，并让 Prefab node 通过 AssetId 引用它们；
SkinnedMesh 的 skeleton/inverse bind 已内嵌在 mesh payload，AnimationClip3D 是独立 Cooked asset。
产品 skin witness 已在 Scene 侧以 `SkinnedMeshRenderer3D` + `Animator3D` 绑定：Animator 复制 wire 数据并在
owner thread 无分配地计算 local/global pose 与 `globalPose * inverseBind` palette，extraction 同步复制到
packet-local RenderScene palette；动画暂停只停止 pose 变化，不跳过 skinned draw 提交。
`instantiatePrefab()` 按稳定 node 顺序创建 Entity、设置父子关系，再通过调用方 resolver 把每个 node 的
Mesh/Material AssetId 转成 weak `AssetHandle`。任一步失败都会逆序销毁本次已创建 Entity；Render key
只在后续 extraction 中解析。

`tina_sample_3d` 当前使用同步 Catalog package API：

1. 默认生成双三角形 multi-mesh glTF fixture（两 scene node，第二节点 translation x=2）；或
   用 `--gltf` 指向磁盘上的 `.gltf`/`.glb`；
2. 向同一 request 加入确定性的 EnvironmentMap payload；内建产品路径还加入一份显式 `Blend` Material，
   再由 `cookAndPublishCatalogPackage` 原子发布 Catalog package；
3. 完整校验 Manifest、ContentHash 与 typed payload；
4. 加载 N 个 StaticMesh/SkinnedMesh、对应 Material、相关 Texture2D、可选 AnimationClip3D 和一个 Prefab（N≤128）并发布到
   Resources-owned `AssetStore`；EnvironmentMap 作为 Resources-owned `CookedAssetFile` 单独加载，
   内建透明 Material 同样进入 Store/registry；slot/Prefab/Scene 仍只保存 weak handle；
5. 上传 mesh/texture/environment，并向 State-owned `Mesh3DBindingRegistry` 事务移交 StaticMesh 与去重后的 Texture
   GPU owner，再原子注册 Material bundle；`--ibl=on` 绑定 EnvironmentMap，`--ibl=off` 保持未绑定但不跳过
   cook/load/upload；device-instance mesh/material allocator 都从2开始生成单调不复用 key，分别保留内置 key 1；
6. Prefab resolver 按 node AssetId 映射到对应 mesh/material handle 与 per-mesh bounds/color；每帧 Scene
   resolver 再通过 registry 按 live handle、严格 AssetKind 与 texture dependency intern 当前 frame ref；
7. 实例化、extract、提交；退出时先销毁 World，on 模式 clear IBL 后与 off 模式共同 retire EnvironmentMap，
   再由 registry 按 Material→共享 Texture→Mesh 顺序 retirement。
   active frame、backend 或 ledger 失败保留完整 Entry 供重试，Registry 析构前必须全空。

2026-08-14 的 schema 15 门禁已证明2个 StaticMesh + 1个 SkinnedMesh 的3组 AssetId/GPU binding、
3个 Material 共享3个 Texture owner，以及 Opaque3D submit 时按 packet-local material ref 解析 binding 并
**采样** baseColor/MR/normal，同时使用 cooked EnvironmentMap 的 diffuse/specular/BRDF split-sum IBL。
当前 schema 16 在这套 owner 图上新增第4个独立 Blend Material；透明 on/off 都完成其
cook/load/register/retire，只有两个 witness entity 与 Transparent3D draw 随模式切换。2026-08-15 集中 gate
证明3 Mesh/4 Material/3 Texture owner 全部正确退休。
World directional/point/spot light entity 每帧发布同一份 frame-scoped lighting snapshot 供全部 batch 着色（未 bind baseColor
用 1×1 白；未 bind MR 时默认 metallic=0/roughness=1；未 bind normal 时用几何法线）。
`tina_sample_3d` 的 `Product3DResources` 拥有固定容量 `AssetStore`，Store 覆盖 State/World/extraction
生命周期；sample 不在组件或 slot 中保存 Lease、runtime generation bits、backend key、GPU owner 或注册
提交位。engine-provided、State-owned `Mesh3DBindingRegistry` 借用 AssetSystem/device/PMR，固定容量拥有
Mesh Lease/GPU/binding、Material Lease/binding 与按 AssetId 去重的共享 Texture Lease/GPU。首次 intern 的
entry pin 覆盖 active packet，Mesh/Texture 通过 AssetSystem retirement ledger 关闭。

## 三类 3D 门禁

| Target | 证明什么 | 不证明什么 |
| --- | --- | --- |
| `tina_sample_3d_extraction` | Headless/Null Camera、culling、sort、batch、300帧退出 | GPU 画面与 Cooked Asset |
| `tina_sample_3d_infrastructure` | procedural Cube、真实 bgfx depth/instance/UI frame | 产品 glTF/Catalog mesh |
| `tina_sample_3d` | 双静态 mesh glTF→MikkTSpace tangent→Cooked P3N3T4UV2，以及独立 SkinnedMesh/AnimationClip3D→`Animator3D` CPU pose→packet palette→bgfx GPU skinning；另以独立 Blend Material + 两个复用 static mesh 的交叠实体贯通统一 transparent 排序与 Transparent3D；AssetSystem→weak Handle→Mesh3D registry→packet-local ref，Mesh/Material/共享 Texture 统一 owner；同时保留 Cook-Torrance GGX、cooked IBL、World directional/point/spot lighting/shadow、startup-only shadow extent、retained controls 与事务换肤 | post、跨 GPU golden；schema 16 集中 Windows gate 已于2026-08-15通过 |

产品 smoke 的结构化输出至少应包含 `gltfCooked`、`cookedStaticMesh`、`cookedMaterial`、
`cookedPrefab`、`meshUploaded`、`meshBound`、`materialTextureBound`（或等价字段）、`prefabInstantiated`、
`sceneExtract`、`evidenceSchema=16`、`tangentMeshesUploaded=2`、`cookedSkinnedMesh=true`、
`cookedAnimationClip3D=true`、`skinnedMeshesUploaded=1`、`uploadedSkinnedJointCount=2`、
`animatorJointCount=2`、`animatorUpdates=300`、`animatorPoseChanges=300`（off 为0）、
`submittedSkinnedMesh3DFrames=300`、`submittedSkinnedMesh3DCount=1`、`visibleSkinnedMesh3DCount=1`、
`submittedSkinnedPaletteJointCount=2`、`skinnedPrefabInstances=3`、`cookedEnvironmentMap=true`、
`imageBasedLightingMode=on`、`environmentMapsUploaded=1`、IBL bind/clear=`1/1`、EnvironmentMap retirement accepted=`1`、
diffuse/specular/mips/BRDF=`2/4/3/4×4`、`cascadedDirectionalShadowCount=1`、
`submittedCascadedDirectionalShadowCount=1`、`cascadedDirectionalShadowCascadeCount=4`、
`submittedCascadedDirectionalShadowCascadeCount=4`、`authoredSpotLightShadowCount=1`、
`submittedSpotLightShadowCount=1`、`pointLightShadowMode=on`、`authoredPointLightShadowCount=1`、
`submittedPointLightShadowCount=1`、`transparencyMode=on`、`blendMaterialCount=1`、
`authoredTransparentStaticWitnessCount=2`、`submittedTransparent3DFrames=300`、
transparent static/skinned/draw count=`2/0/2`、非零且跨帧稳定的 transparent sort checksum、
mesh/material handle 发布数=`3/4`、`meshBindingsRegistered=3`、
`materialBindingsRegistered=4`、`meshBindingsReleased=3`、`materialBindingsReleased=4`、
`texturesUploaded=3`、`meshesUploaded=3`、`meshRetirementsAccepted=3`、
`textureRetirementsAccepted=3`、对应 retirement records 全部 `Released` 且 live=0、
mesh/material/texture weak handle invalidation 数分别为3/4/3、`bindingRegistryReleased=true`、
static/skinned/pose/material resolver hits 分别为 `1200/300/300/1500`、
`lightingConfigured=true`、`directionalLightCount=3`、point/spot authored/committed/culled 均为 `3/2/1`、
`sceneLightingFrames=300`、
`cameraAspectMatchesSurface=true`、`uiResponsiveLayoutVerified=true`、logical/framebuffer extent、
中央 3D RGB ROI fingerprint/像素数/通道总量及临时 raw capture 逐像素 L1、
`uiPanelsCreated=7`、`uiLabelsCreated=13`、
Button/Checkbox/Slider/ProgressBar/ListView/TreeView 各创建1个、`uiThemeSwitches=2`、
`uiAutomatedThemeSteps=2`、`uiAutomatedCollectionSteps=2`、`uiTreeExpansionChanges=2`、
`uiListSelectionKey=2003`、`uiTreeSelectionKey=4`、
`uiThemeFinalLight=false`、`uiInheritedChromeVerified=true`、`uiProgressFinal=100`、退出计数、资源归零信息、
`pixelCaptureOk`、非零 capture 尺寸/字节数与 `pixelFingerprint`。同机 exact 视觉回归把首次 fingerprint
传给 `--expect-pixel-fingerprint=<32 lowercase hex>`，并要求
`pixelGoldenChecked/pixelGoldenMatched` 均为 true；该值不得跨 GPU/driver/backend 作为通用金标。
TEST-003 当前 gate 契约还用短帧 IBL on×2/off×2、skin animation on/off、point shadow on/off 与
transparency on/off A/B 运行证明同模式 fingerprint 稳定且跨模式不同；IBL off 的结构化证据要求
EnvironmentMap upload/retire=`1/1`、IBL bind/clear=`0/0`，从而把“资源生命周期闭环”和“IBL 确实改变像素”
分别取证。
skin-animation=off 仍要求 `animatorUpdates=30`、skinned draw frames=`30`、palette joints=`2`，但
`animatorPoseChanges=0`；on/off 的中央 3D RGB fingerprint 必须不同，逐字节 RGB L1 至少达到 ROI 像素数。
同一 gate 复用一份 IBL-on baseline，再执行一次 `--point-shadow=off`；要求 point shadow authored/submitted
从 `1/1` 变为 `0/0`，中央 3D RGB ROI fingerprint 改变且逐像素 L1 大于0。透明 baseline 为 on；
内建 fixture 的 `--transparency=off` 要求 authored/submitted frames/static/skinned/draw/checksum 全部为0，而 Blend Material
仍完成1份 load/binding/lifecycle；on/off 中央 3D RGB fingerprint 必须不同，逐字节 L1 至少达到 ROI 像素数。
所有临时 raw capture 在比较后删除。2026-08-15 schema 16 透明 gate 已通过：透明 on/off 中央 3D RGB
fingerprint 不同，逐字节 L1 达到 ROI 像素数门槛，schema 16 结构化字段与排序稳定性通过。
2026-08-14 的完整 gate 中11个 GoogleTest executable 均 exit 0，300帧 schema 15 的 Animator pose changes=`300`、
packet pose fingerprint changes=`299`；30帧 skin on/off scene fingerprint 各自稳定且跨模式不同，RGB L1
达到门槛。同轮 IBL 与 PointLight shadow 的 on/off fingerprint 也各自稳定、跨模式不同且 L1 达标；临时 raw
capture 均已回收。
测试数量不是永久契约；实际命令和门禁矩阵统一见[测试说明](testing.md)。

## 当前限制

- glTF multi-primitive 采用 **SPLIT**（非 merge）：每 TRIANGLES prim 独立 StaticMesh/SkinnedMesh+Material，
  Prefab 1 mesh/1 material 节点契约不变；不支持多 submesh 合并进单一 mesh、无 bufferView 的 data-URI
  image、Draco、morph、sparse accessor 或非三角 primitive；skin 只接受当前 SkinnedMesh v1 的固定4
  influences/最多256 joints，animation 只接受 joint target + LINEAR/STEP；不支持项返回结构化错误。
  **SkinnedMesh wire 已提到 v2**：追加逐 joint 64 字节名称块，cooker 保留 glTF node name 并保证名称
  跨 `(depth, sourceIndex)` 重排后仍跟随其 joint，重名在 encode/parse/cook 三处拒绝；
- **`Tina::Animation3D` 已落地**（见 [3D 动画图](animation-3d.md)、[ADR 0037](adr/0037-animation3d-graph-boundaries.md)）：
  crossfade、状态机、blend tree、layer + mask、root motion 与两骨 IK 建在 `Animator3D` **旁边**，后者
  一字未改且仍是 `samples/3d_product` 的消费面。仍缺 retargeting、morph target、2D blend space，以及
  pose-aware bounds（extraction 仍用授权 `localBounds` 剔除，大幅位移或 IK 会 pop）；
- Cooked Material v2 写入 metallic/roughness factor、可选 MR/normal Texture2D deps 与显式
  `Opaque`/`Blend`；Runtime/bgfx
  产品着色为 Cook-Torrance GGX（有界0..4 directional、0..8 point、0..8 spot lights + ambient fallback + baseColor/可选 MR/normal
  贴图），并可绑定一个 cooked split-sum IBL environment；已有 directional、point 与 spot Scene component，PointLight3D 使用线性径向衰减，
  SpotLight3D 使用径向与角度平滑衰减，二者都使用 influence sphere-frustum culling；已有固定4级联
  directional CSM、固定单 SpotLight shadow 与固定单 PointLight 全向 shadow；三类 D16 extent 已支持 startup-only 配置；
  Transparent3D 已接线并通过2026-08-15集中 gate；post 尚未实现；
- glTF Cooker 读取完整 `pbrMetallicRoughness` 与可选 `normalTexture`；主/外部文件使用单 handle/fd
  bounded snapshot，外部相对 URI 在 percent-decode 与 strict UTF-8 校验后按最终路径强制 authoring-root
  containment，拒绝 `..`/scheme/rooted path、逃逸 symlink/junction、读取期间替换以及 file/count/range/
  parser/decode/output budget 超限；
- Scene component 保存 mesh/material weak `AssetHandle`；`SkinnedMeshRenderer3D` 还通过调用方提供的
  `Animator3D` pose provider 取得 CPU palette；RenderScene 只保存 packet-local geometry/material/ref 与
  palette range，Null/bgfx 在任何提交副作用前验证 stale/cross-packet/wrong-kind/range/joint count。device
  binding key 只存在于 registry/backend 私有实现；
- Material v2 由 baseColor/MR/normal flags 和同顺序的 required Texture2D dependency stream 表达 role；writer
  要求 AssetId 严格递增且唯一，拒绝乱序或多个 role 共享同一 ID，registry 因而可精确拒绝 role swap；
- EngineHost 已有 `RenderFramePacket` + FramePin + present-return CPU completion；它不代表 GPU 退役；
  Texture/Mesh/EnvironmentMap 使用独立 readback marker；
- 无 pipeline cache 产品契约或 worker extraction；`tina_bench` schema v1 已落地
  （PERF-001 首切片），但不替代 3D 视觉门禁；
- Jolt/3D Physics 未接入，静态 3D 产品门禁不以它为前置条件。

下一步只在可执行 Backlog 中维护：`RENDER-001` 已由 startup-only shadow extent 配置闭环，
Texture/Mesh/EnvironmentMap backend retirement 已使用 readback completion marker；通用 GPU submission fence 不在当前
Runtime 契约内。详见 [Rendering](rendering.md) 与 [Backlog](backlog.md)。
