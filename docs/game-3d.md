# 3D 产品架构

`tina_sample_3d` 是当前最小 3D 产品门禁：运行时生成一个单 mesh glTF fixture，经 cgltf Cook、
Catalog 发布与校验、StaticMesh GPU upload、Prefab 实例化、Scene extraction 后由 bgfx Opaque3D pass
绘制，并叠加 retained UI。它证明单 product mesh 的端到端路径，不等同于完整 3D 渲染器。

Cooker 已支持一个 glTF 文件中的多个 mesh，并为每个 mesh/material 生成 distinct AssetId；产品 sample
仍只绑定一个 mesh/material key。两个 mesh 的独立 upload、binding、extract、draw 与视觉证据由
`3D-001` 跟踪。

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

## 已实现能力

| 层 | 当前实现 |
| --- | --- |
| Cooker | glTF 2.0 JSON/GLB；每个 mesh 一个 TRIANGLES primitive；POSITION float3，NORMAL/TEXCOORD_0 可选；multi-mesh 输出 distinct Mesh/Material AssetId；scene node 转 Prefab hierarchy/dependency |
| Cooked 数据 | StaticMesh v1 使用 P3N3UV2、UInt16 index、bounds/submesh；Material v1 为 Opaque `UnlitBaseColor`；Prefab v1 保存稳定 node id、父索引、local transform 与 Mesh/Material AssetId |
| Scene | `PerspectiveCamera3D`、`MeshRenderer3D`、Transform hierarchy、Prefab 实例化与失败回滚 |
| Extraction | 唯一 active perspective camera、surface aspect resolve、world bounds、frustum culling、稳定排序与相邻实例 batch |
| bgfx | color/depth clear、Perspective view、depth write/less、back-face culling、instance buffer、内置 Cube fixture或显式 GPU mesh binding、solid Unlit shader |

世界坐标为右手、Y-up、局部 `-Z` forward、单位米。Camera 公共字段使用 degree，并要求
`0 < near < far`、有限数值和有效 normalized viewport；aspect 每帧从 primary surface 解析，不写回
Camera component。

当前 `MeshRenderer3D` 的真实字段仍是 `fixtureMeshKey`、`fixtureMaterialKey`、`submeshIndex`、bounds、
base color 与可见性。名称保留了基础设施阶段痕迹，但非零 key 也可绑定产品 GPU mesh。它尚未直接保存
`AssetHandle<StaticMesh>` 或 `AssetHandle<Material>`，不要把未来资源解析边界写成现有 API。

## Prefab 与资源解析

Cooker 为每个 glTF mesh 建立 StaticMesh 和 Material，并让 Prefab node 通过 AssetId 引用它们。
`instantiatePrefab()` 按稳定 node 顺序创建 Entity、设置父子关系，再通过调用方 resolver 把每个 node 的
Mesh/Material AssetId 转成当前 Render key。任一步失败都会逆序销毁本次已创建 Entity。

`tina_sample_3d` 当前使用同步 Catalog package API：

1. 生成单三角形 glTF fixture；
2. cook 并原子发布 Catalog package；
3. 完整校验 Manifest、ContentHash 与 typed payload；
4. 加载一个 StaticMesh、一个 Material 和一个 Prefab；
5. 上传 mesh，并把固定 product key 绑定到 GPU mesh；
6. resolver 只接受该 mesh/material 的 AssetId；
7. 实例化、extract、提交，退出时解除 binding 并销毁 GPU mesh。

因此当前产品门禁没有证明 `AssetSystem` Handle/Lease 到 bgfx submission 的完整 pin，也没有证明两个
不同 AssetId 的并行 GPU binding。后者属于 `3D-001`，前者属于 `RUNTIME-002`。

## 三类 3D 门禁

| Target | 证明什么 | 不证明什么 |
| --- | --- | --- |
| `tina_sample_3d_extraction` | Headless/Null Camera、culling、sort、batch、300帧退出 | GPU 画面与 Cooked Asset |
| `tina_sample_3d_infrastructure` | procedural Cube、真实 bgfx depth/instance/UI frame | 产品 glTF/Catalog mesh |
| `tina_sample_3d` | 单 mesh glTF→Cooked→GPU→Prefab→Scene→bgfx 生命周期 | multi-mesh E2E、Cooked texture GPU 绑定、PBR |

产品 smoke 的结构化输出至少应包含 `gltfCooked`、`cookedStaticMesh`、`cookedMaterial`、
`cookedPrefab`、`meshUploaded`、`meshBound`、`prefabInstantiated`、`sceneExtract`、退出计数与资源归零
信息。测试数量不是永久契约；实际命令和门禁矩阵统一见[测试说明](testing.md)。

## 当前限制

- glTF importer 不支持单 mesh 多 primitive merge、无 bufferView 的 data-URI image、Draco、morph、
  skin、animation、sparse accessor 或非三角 primitive；不支持项返回结构化错误；
- Material 产品路径只有 solid Opaque Unlit；无 PBR、Light、Shadow、transparent pass 或 post-processing；
- glTF Cooker 已把相对文件或 bufferView 的 baseColorTexture 发布为 Texture2D dependency，但当前
  产品 sample 尚未把该 Cooked texture 绑定到 GPU material；
- `RenderScene` 仍使用 mesh/material key，不是 owning `FrameResourceRef`/`RenderFramePacket`；
- 无通用 pass scheduler、pipeline cache 产品契约、worker extraction 或正式 `tina_bench`；
- Jolt/3D Physics 未接入，静态 3D 产品门禁不以它为前置条件。

下一步只在可执行 Backlog 中维护：multi-mesh 产品闭环见 `3D-001`，外部 buffer/texture 安全策略见
`ASSET-001`，资源 pin/completion 见 `RUNTIME-002`，PBR/pass scheduling 见 `RENDER-001`。详见
[Backlog](backlog.md)。
