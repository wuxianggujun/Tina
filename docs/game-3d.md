# 3D 产品架构

`tina_sample_3d` 是当前最小 3D 产品门禁：默认在运行时生成双 mesh glTF fixture，经 cgltf Cook、
Catalog 发布与校验、StaticMesh GPU upload、Prefab 实例化、Scene extraction 后由 bgfx Opaque3D
pass 绘制，并叠加 retained UI。它证明 multi-mesh product 的端到端路径，不等同于完整 3D 渲染器。

也可通过 CLI 加载**磁盘上的外部** `.gltf`/`.glb`（用户模型）：同样只走 cooker，Runtime 不解析源
glTF。默认产品门禁使用仓库 **complete PBR fixture**
（`tests/fixtures/gltf/complete_pbr/complete_pbr.gltf`：双 mesh、NORMAL/UV、baseColor+MR+normal 贴图、
不同 metallic/roughness）；未编译进 fixture 路径时回退到最小内建 glTF。外部 `--gltf=` 仍为 opt-in。

Cooker 与产品 sample 支持一个 glTF 中多个 mesh（sample 槽位上限 8）：distinct Mesh/Material
AssetId、独立 meshKey binding、Prefab 每节点 resolver、extract/draw 与 ledger 归零。外部 URI 安全与
产品侧 `setMesh3DMaterialTextureBinding` 已完成（ASSET-001）。Cooked Material v2 携带 metallic/
roughness factor 与可选 MR/normal Texture2D dependency。bgfx Opaque3D 在 submit 时采样 baseColor，
并以 **experimental metallic-roughness hybrid** 着色（固定 1 盏方向光 + ambient；
`setMesh3DMaterialFactors` 接 Cooked metallic/roughness；可选
`setMesh3DMaterialMetallicRoughnessTextureBinding`）。完整 light system / IBL / shadow 仍属
`RENDER-001` 后续。

## CLI（`tina_sample_3d`）

```text
tina_sample_3d [--frames=N] [--frame-delay-ms=N] [--gltf=<path>|--gltf <path>] [--help]
```

| 标志 | 含义 |
| --- | --- |
| `--frames=N` | N 帧后退出（默认 300） |
| `--frame-delay-ms=N` | 每帧 sleep（默认 0） |
| `--gltf=<path>` / `--gltf <path>` | 从磁盘 cook 外部 `.gltf`/`.glb`；省略则用内建双 mesh fixture |
| `--help` / `-h` | 打印用法 |

失败（文件不存在、扩展名非法、cooker 不支持的 Draco / skin 等）在 stderr 输出结构化 JSON：
`status=error`、`code`、`message`、可选 `context[]`。成功 stdout JSON 含 `gltfCooked`、
`externalGltf`、`meshSlotCount`、`multiMesh` 等。

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
  -> manifest.tmnft + Cooked StaticMesh/Material/Prefab
  -> typed Cooked views
  -> RenderDevice mesh upload + key binding
  -> Prefab AssetId-to-key resolver
  -> Scene::World
       PerspectiveCamera3D
       MeshRenderer3D
  -> RenderScene cull/sort/batch
  -> bgfx Opaque3D + UI
```

- cgltf 只在 `tina_asset` 的私有 Cooker 实现中出现；Runtime 不解析源 glTF；
- `Scene::World` 保存 Transform、Camera 和 backend-neutral key，不保存 bgfx/GPU handle；
- RenderScene 不回查 World，提交时只消费已 commit 的 Camera、mesh item 与 batch；
- `tina_render_bgfx` 私有拥有 shader、vertex/index buffer、view id、uniform 与 texture binding；
- 普通 Game API 不暴露 cgltf、bgfx 或 native surface 类型。

## 组合入口

产品 3D sample 经 `Tina::Desktop::CreateEngine(config, options)` 启动（与 2D 一致），不再手写
`EngineCompositionFactories`。mesh/texture bind 证据通过
`CreateEngineOptions::wrapWindowSurfaceRenderDevice` + `samples/3d_product/DeviceCapture.hpp`。
EngineHost 仍是唯一组合根。

## 已实现能力

| 层 | 当前实现 |
| --- | --- |
| Cooker | glTF 2.0 JSON/GLB；每个 primitive 为 TRIANGLES；POSITION float3，NORMAL/TEXCOORD_0 可选；multi-mesh 与 **multi-primitive SPLIT**（每 prim 一个 StaticMesh+Material；Prefab 展开为 transform 父节点 + 子 draw 节点）输出 distinct AssetId；scene node 转 Prefab hierarchy/dependency |
| Cooked 数据 | StaticMesh v1 使用 P3N3UV2、UInt16 index、bounds/submesh；Material v2 为 Opaque `UnlitBaseColor` + cooked PBR factors（metallic/roughness）与可选 baseColor/MR/normal Texture2D deps；Prefab v1 保存稳定 node id、父索引、local transform 与 Mesh/Material AssetId |
| Scene | `PerspectiveCamera3D`、`MeshRenderer3D`、Transform hierarchy、Prefab 实例化与失败回滚 |
| Extraction | 唯一 active perspective camera、surface aspect resolve、world bounds、frustum culling、稳定排序与相邻实例 batch |
| bgfx | color/depth clear、Perspective view、depth write/less、back-face culling、instance buffer、内置 Cube fixture（`meshKey=1` 未 bind 时）或显式 GPU mesh binding、experimental MR hybrid **采样** baseColor（`s_texColor`）与可选 MR 贴图（`s_texMR`）；固定方向光 |

世界坐标为右手、Y-up、局部 `-Z` forward、单位米。Camera 公共字段使用 degree，并要求
`0 < near < far`、有限数值和有效 normalized viewport；aspect 每帧从 primary surface 解析，不写回
Camera component。

当前 `MeshRenderer3D` 的真实字段仍是 `meshKey`、`materialKey`、`submeshIndex`、bounds、
base color 与可见性。名称保留了基础设施阶段痕迹，但非零 key 也可绑定产品 GPU mesh。它尚未直接保存
`AssetHandle<StaticMesh>` 或 `AssetHandle<Material>`，不要把未来资源解析边界写成现有 API。

## Prefab 与资源解析

Cooker 为每个 glTF mesh 建立 StaticMesh 和 Material，并让 Prefab node 通过 AssetId 引用它们。
`instantiatePrefab()` 按稳定 node 顺序创建 Entity、设置父子关系，再通过调用方 resolver 把每个 node 的
Mesh/Material AssetId 转成当前 Render key。任一步失败都会逆序销毁本次已创建 Entity。

`tina_sample_3d` 当前使用同步 Catalog package API：

1. 默认生成双三角形 multi-mesh glTF fixture（两 scene node，第二节点 translation x=2）；或
   用 `--gltf` 指向磁盘上的 `.gltf`/`.glb`；
2. `cookGltfFileToCatalogRequest` + `cookAndPublishCatalogPackage` 原子发布 Catalog package；
3. 完整校验 Manifest、ContentHash 与 typed payload；
4. 加载 N 个 StaticMesh、N 个 Material 和一个 Prefab（N≤8）；
5. 上传 mesh，绑定 product meshKey `1..N`；有 baseColorTexture 时 upload Texture2D 并
   `setMesh3DMaterialTextureBinding`（外部无贴图模型可跳过，Opaque3D 用 1×1 白）；
6. resolver 按 node AssetId 映射到对应 meshKey/materialKey 与 per-mesh bounds/color；
7. 实例化、extract、提交，退出时解除 binding 并销毁 GPU mesh/texture。

当前产品门禁已证明两个不同 AssetId 的并行 mesh GPU binding、material texture 绑定，以及 Opaque3D
experimental MR submit 时按 materialKey **采样** baseColor 并做固定光照（未 bind 用 1×1 白；
默认 metallic=0/roughness=1）。尚未证明 `AssetSystem` Handle/Lease 到 submission 的完整 fence pin
（真 fence 见 RUNTIME-002 尾巴）。

## 三类 3D 门禁

| Target | 证明什么 | 不证明什么 |
| --- | --- | --- |
| `tina_sample_3d_extraction` | Headless/Null Camera、culling、sort、batch、300帧退出 | GPU 画面与 Cooked Asset |
| `tina_sample_3d_infrastructure` | procedural Cube、真实 bgfx depth/instance/UI frame | 产品 glTF/Catalog mesh |
| `tina_sample_3d` | 双 mesh glTF→Cooked→GPU→Prefab→Scene→bgfx；texture upload + material key bind + Opaque3D experimental MR | 完整 light system/IBL/shadow、Handle/Lease→fence pin |

产品 smoke 的结构化输出至少应包含 `gltfCooked`、`cookedStaticMesh`、`cookedMaterial`、
`cookedPrefab`、`meshUploaded`、`meshBound`、`materialTextureBound`（或等价字段）、`prefabInstantiated`、
`sceneExtract`、退出计数与资源归零信息。测试数量不是永久契约；实际命令和门禁矩阵统一见
[测试说明](testing.md)。

## 当前限制

- glTF multi-primitive 采用 **SPLIT**（非 merge）：每 TRIANGLES prim 独立 StaticMesh+Material，Prefab
  1 mesh/1 material 节点契约不变；不支持多 submesh 合并进单一 StaticMesh、无 bufferView 的 data-URI
  image、Draco、morph、skin、animation、sparse accessor 或非三角 primitive；不支持项返回结构化错误；
- Cooked Material v2 写入 metallic/roughness factor 与可选 MR/normal Texture2D deps；Runtime/bgfx
  产品着色为 **experimental MR hybrid**（固定方向光 + ambient + baseColor/可选 MR 贴图），不是完整
  PBR；无 multi-light system、Shadow、transparent、IBL 或 post；
- glTF Cooker 读取完整 `pbrMetallicRoughness` 与可选 `normalTexture`；外部相对 URI 强制 root
  containment，拒绝 `..`/scheme/绝对路径与 >64MiB 文件；
- Scene/Render 仍使用 mesh/material **key**（bind-table 语义，非 AssetHandle 组件字段），不是 Scene 上直接存
  `AssetHandle`，也不是 extract 输出 owning `FrameResourceRef`；
- EngineHost 已有 `RenderFramePacket` + FramePin + Null completion 首切片；真 bgfx fence 后置；
- 无通用 pass scheduler、pipeline cache 产品契约或 worker extraction；`tina_bench` schema v1 已落地
  （PERF-001 首切片），但不替代 3D 视觉门禁；
- Jolt/3D Physics 未接入，静态 3D 产品门禁不以它为前置条件。

下一步只在可执行 Backlog 中维护：`RENDER-001` 剩余（light system / IBL / cooked MR factors /
pass scheduling），真 GPU fence completion 见 RUNTIME-002 尾巴。详见 [Backlog](backlog.md)。
