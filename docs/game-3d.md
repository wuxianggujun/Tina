# 3D 产品架构

`tina_sample_3d` 是当前最小 3D 产品门禁：默认在运行时生成双 mesh glTF fixture，经 cgltf Cook、
Catalog 发布与校验、StaticMesh GPU upload、Prefab 实例化、Scene extraction 后由 bgfx Opaque3D
pass 绘制，并叠加 retained UI。它证明 multi-mesh product 的端到端路径，不等同于完整 3D 渲染器。

也可通过 CLI 加载**磁盘上的外部** `.gltf`/`.glb`（用户模型）：同样只走 cooker，Runtime 不解析源
glTF。默认门禁仍用内建 fixture；外部路径为 opt-in。

Cooker 与产品 sample 支持一个 glTF 中多个 mesh（sample 槽位上限 8）：distinct Mesh/Material
AssetId、独立 meshKey binding、Prefab 每节点 resolver、extract/draw 与 ledger 归零。外部 URI 安全与
产品侧 `setMesh3DMaterialTextureBinding` 已完成（ASSET-001）；bgfx Opaque3D unlit 在 submit 时按
materialKey **采样** baseColor（`s_texColor` × instance factor；未 bind 用 1×1 白贴图）。
PBR 见 `RENDER-001`。

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

失败（文件不存在、扩展名非法、cooker 不支持的 multi-primitive / Draco / skin 等）在 stderr 输出
结构化 JSON：`status=error`、`code`、`message`、可选 `context[]`。成功 stdout JSON 含
`gltfCooked`、`externalGltf`、`meshSlotCount`、`multiMesh` 等。

外部路径 smoke 为 opt-in：仓库不强制附带用户模型；有模型时例如：

```powershell
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0 --gltf=path\to\model.glb
```

默认 fixture 门禁：

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
| Cooker | glTF 2.0 JSON/GLB；每个 mesh 一个 TRIANGLES primitive；POSITION float3，NORMAL/TEXCOORD_0 可选；multi-mesh 输出 distinct Mesh/Material AssetId；scene node 转 Prefab hierarchy/dependency |
| Cooked 数据 | StaticMesh v1 使用 P3N3UV2、UInt16 index、bounds/submesh；Material v1 为 Opaque `UnlitBaseColor`；Prefab v1 保存稳定 node id、父索引、local transform 与 Mesh/Material AssetId |
| Scene | `PerspectiveCamera3D`、`MeshRenderer3D`、Transform hierarchy、Prefab 实例化与失败回滚 |
| Extraction | 唯一 active perspective camera、surface aspect resolve、world bounds、frustum culling、稳定排序与相邻实例 batch |
| bgfx | color/depth clear、Perspective view、depth write/less、back-face culling、instance buffer、内置 Cube fixture（`meshKey=1` 未 bind 时）或显式 GPU mesh binding、Unlit shader **采样** materialKey 绑定贴图（`s_texColor`；默认 1×1 白） |

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
unlit submit 时按 materialKey **采样**（`s_texColor` × instance baseColorFactor；未 bind 用 1×1 白）。
尚未证明 `AssetSystem` Handle/Lease 到 submission 的完整 fence pin（真 fence 见 RUNTIME-002 尾巴）。

## 三类 3D 门禁

| Target | 证明什么 | 不证明什么 |
| --- | --- | --- |
| `tina_sample_3d_extraction` | Headless/Null Camera、culling、sort、batch、300帧退出 | GPU 画面与 Cooked Asset |
| `tina_sample_3d_infrastructure` | procedural Cube、真实 bgfx depth/instance/UI frame | 产品 glTF/Catalog mesh |
| `tina_sample_3d` | 双 mesh glTF→Cooked→GPU→Prefab→Scene→bgfx；texture upload + material key bind + Opaque3D 采样 | PBR、Handle/Lease→fence pin |

产品 smoke 的结构化输出至少应包含 `gltfCooked`、`cookedStaticMesh`、`cookedMaterial`、
`cookedPrefab`、`meshUploaded`、`meshBound`、`materialTextureBound`（或等价字段）、`prefabInstantiated`、
`sceneExtract`、退出计数与资源归零信息。测试数量不是永久契约；实际命令和门禁矩阵统一见
[测试说明](testing.md)。

## 当前限制

- glTF importer 不支持单 mesh 多 primitive merge、无 bufferView 的 data-URI image、Draco、morph、
  skin、animation、sparse accessor 或非三角 primitive；不支持项返回结构化错误；
- Material 产品路径只有 solid Opaque Unlit；无 PBR、Light、Shadow、transparent pass 或 post-processing；
- glTF Cooker 把相对文件或 bufferView 的 baseColorTexture 发布为 Texture2D dependency；外部相对 URI
  强制 root containment，拒绝 `..`/scheme/绝对路径与 >64MiB 文件；`tina_sample_3d` 已 upload、
  `setMesh3DMaterialTextureBinding`，且 Opaque3D unlit submit **采样** material 贴图；
- Scene/Render 仍使用 mesh/material **key**（bind-table 语义，非 AssetHandle 组件字段），不是 Scene 上直接存
  `AssetHandle`，也不是 extract 输出 owning `FrameResourceRef`；
- EngineHost 已有 `RenderFramePacket` + FramePin + Null completion 首切片；真 bgfx fence 后置；
- 无通用 pass scheduler、pipeline cache 产品契约或 worker extraction；`tina_bench` schema v1 已落地
  （PERF-001 首切片），但不替代 3D 视觉门禁；
- Jolt/3D Physics 未接入，静态 3D 产品门禁不以它为前置条件。

下一步只在可执行 Backlog 中维护：PBR/pass 见 `RENDER-001`，真 GPU fence completion 见 RUNTIME-002
尾巴。详见 [Backlog](backlog.md)。
