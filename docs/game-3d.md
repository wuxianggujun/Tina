# 3D 游戏架构

> 状态：vNext 契约已冻结；M9-A CPU/Null extraction foundation 已实现。可见 Cube 仍只验证
> M9-B 基础设施；正式 3D 路径必须覆盖 Cooked Mesh、Material、Prefab、Camera、depth 与 culling。

## 首期能力边界

首期 3D 的目标是稳定、可测的静态场景，不是提前实现完整现代渲染器：

- Perspective Camera；
- 静态三角形 Mesh、Submesh 和 Material slot；
- `UnlitBaseColor` 材质，支持颜色因子和一张 sRGB 基础颜色纹理；
- Opaque depth、back-face culling、frustum culling 和相同 Mesh/Material 的静态 instancing；
- glTF 经 Cooker 转换为 Mesh/Material/Prefab，Runtime 不解析源 glTF；
- 不实现 Light、PBR、Shadow、Skin、Animation、Morph、transparent pass、post-processing；
- Jolt 仍后置到出现真实 3D 物理玩法，不为静态渲染样例提前接入。

不支持特性由 Cooker 返回带 node/mesh/material 路径的明确诊断，不能静默忽略后继续生成看似
成功的资产。

当前 M9-A 只完成其中的 backend-neutral extraction 子集：`RenderPerspectiveCameraInput`、
`RenderMesh3DInput`、固定容量 item/batch storage、世界包围球、frustum culling、稳定排序与 batch finalize。
`meshKey/materialKey` 是 M9 fixture key，不是公开 Asset/GPU handle；M10 必须用 `FrameResourceRef` 替换解析
路径。`tina_sample_3d_extraction` 在 Headless/Null 中运行300帧，不创建 depth attachment、bgfx Buffer、Shader
或 Pipeline，也不产生可见画面。

## 模块与数据流

```text
Game3DState
  -> tina_scene World
       Transform hierarchy
       PerspectiveCamera
       MeshRenderer3D
  -> Render Scene Extraction
       active Camera + visible MeshRenderItem
  -> immutable RenderScene
  -> Opaque3D pass
  -> tina_render_bgfx private backend
```

游戏代码保存 `AssetHandle<MeshAsset>`、`AssetHandle<MaterialAsset>` 和 Scene component，不接触
RenderDevice、GPU Buffer/Texture/Pipeline handle、view id、shader uniform 或 bgfx 类型。

## Camera3D

```cpp
struct PerspectiveCamera {
    float verticalFovDegrees = 60.0f;
    float nearPlaneMeters = 0.1f;
    float farPlaneMeters = 1000.0f;
    RectF normalizedViewport{0.0f, 0.0f, 1.0f, 1.0f};
    bool active = true;
};
```

- 世界为右手坐标、Y-up、-Z forward、单位米；
- FOV 公共 API 明确使用 degrees，不接受含糊裸 radians；
- `0 < near < far`，所有数值必须 finite，越界在组件写入时返回错误；
- aspect 不存储在 Camera component。当前 M9-A Runtime 从本帧 primary `PlatformFrame` 的正 framebuffer
  extent 计算，framebuffer 为 `0x0` 时回退正 logical extent；后续 surface-backed pass 还必须与
  `WindowSurfaceSnapshot` revision/viewport 保持一致；
- 每个启用 World view 首期恰好一个 active Camera，零个或多个都返回稳定诊断；
- upper layer 只表达 Tina Camera descriptor。D3D/OpenGL 的 clip depth、origin 和 projection
  差异只在 Render backend 生成矩阵时处理，不能泄漏成玩法条件分支。

首期深度约定为 clear depth 1.0、`CompareOp::Less`、depth write enabled。Reverse-Z 在有真实远
景精度需求和跨 backend 测试后另写 ADR，不能半途改变 Cooked material/pipeline key。

## Mesh、Submesh 与组件

Cooked `StaticMeshAsset` 的 v1 schema 至少包含：

```text
vertexLayout      P3_N3_UV2（Tina semantic/format，不是 bgfx VertexLayout）
vertexCount
indexFormat       UInt16 | UInt32
indexCount
submeshes[]       firstIndex / indexCount / materialSlot / localBounds
meshBounds        AABB + bounding sphere
vertexPayload
indexPayload
```

Cooker 验证 attribute count、stride、offset、index range、有限数值、三角 primitive、空 Mesh 和
size 乘法溢出。首期固定一种 canonical layout 能减少 shader/pipeline 组合；出现明确资产需求后
再用 schema version 扩展 tangent/color/skin，不接收任意 backend vertex declaration。

Scene component：

```cpp
struct MeshRenderer3D {
    AssetHandle<StaticMeshAsset> mesh;
    AssetHandle<MaterialAsset> materialOverride; // invalid 表示使用 slot 默认材质
    std::uint32_t visibilityMask = 0xffffffffu;
    bool visible = true;
};
```

一个 Mesh 的多个 Submesh 在 extraction 后形成多个 render item；Material override 首期覆盖全部
slot，逐 slot override 等出现真实消费者再增加。World Transform 与 local bounds 生成 world
bounds；非 finite/退化 Transform 不提交 draw，并产生结构化诊断。

## Material v1 与 Shader ABI

首期只冻结一个明确材质模型：

```text
MaterialAsset(UnlitBaseColor)
  shaderFamily / variantKey
  baseColorFactor       linear RGBA
  baseColorTexture      optional Texture2D AssetId，按 sRGB 采样
  sampler               Tina SamplerDesc
  doubleSided           bool
  alphaMode             Opaque only
```

`Light` 不进入 M9/M10 的 RenderScene、组件或公共接口。需要基础照明时先设计 `LitBasic` 的灯光、
法线、颜色空间和性能门禁，不能把未定义 Light 放进 RenderScene 占位。

Cooked `ShaderAsset` 由 Tina 外壳和私有 backend payload 组成：

- `shaderAbiVersion`、`interfaceHash`、`vertexLayoutHash`、stage table；
- binding table、uniform block layout、texture/sampler slot 和 variant key；
- Tina target/platform id；
- bgfx shaderc profile 与二进制只存在于 `tina_render_bgfx` 可读的 payload。

Material Cooker 在离线阶段验证类型、slot、常量布局、颜色空间和 Shader interface；Runtime
mismatch 返回错误，不在每个 draw 中按字符串查 uniform。Pipeline key 由 Shader interface、
vertex layout、attachment format、topology、raster、depth 和 blend 描述组成，由 Render 内部缓存。

首期 shader authoring 仍可使用 shaderc-compatible 源格式，这是离线工具细节；游戏 C++ API
和 Cooked 逻辑 schema 不出现 `BGFX_*`、RendererType、Program/Uniform handle 或 profile 字符串。

## glTF 到 Prefab

固定转换链：

```text
source glTF
  -> cgltf parse/validate
  -> StaticMeshAsset(s)
  -> Texture2DAsset(s)
  -> MaterialAsset(s)
  -> PrefabAsset(node hierarchy + local transform + mesh/material references)
```

`PrefabAsset` 保存稳定 node id、父索引、LocalTransform、可选 MeshRenderer3D descriptor 和资产
依赖。实例化由 Scene command 完成：先预检依赖和层级循环，再按稳定 node 顺序创建 Entity、
设置 Parent 和 render component，最后一次 commit。实例化失败不留下半个 Entity hierarchy。

源 glTF 的坐标和单位在 Cooker 中一次性规范化到 Tina 右手 Y-up/-Z forward/米；Runtime 不再
根据源格式重复翻轴。多 primitive 映射为 Submesh/Material slot。Skin、Animation、Morph、
Draco/Meshopt 等未支持扩展返回 `UnsupportedFeature` 和完整源路径。

## Render Scene Extraction、culling 与 instancing

每帧顺序：

1. 选择 RenderView 与唯一 active Camera；
2. 从 previous/current Transform 以 interpolation alpha 生成 render transform；
3. 计算/读取 world bounds并执行 frustum culling；
4. 为可见 Submesh 生成连续 `MeshRenderItem`；
5. 按 pipeline/material/mesh/depth bucket 的稳定 key 排序；
6. 相同 Mesh、Submesh、Material、Pipeline 的相邻 item 合并为 instance batch；
7. Pass Scheduler 消费 immutable RenderScene，不能回查 World。

M9-A 已实现步骤3到6的单线程 CPU 基础，但输入仍是游戏回调写入的 resolved 纯值，而不是 Scene component/
Asset snapshot。当前稳定 key 为
`material -> mesh -> submesh -> doubleSided -> depth bucket -> stableEntity -> insertion`，相邻且
Mesh/Material/Submesh/Double-sided 相同的 item 合并为 batch；Pipeline key 要等 M9-B/M10 资源边界落地后
进入排序。interpolation、worker chunk merge、FrameResourceRef 和 SubmissionTicket 尚未实现。

Worker 可以对不可变 chunk 做 bounds/culling 并写 worker-local buffer；barrier 后按稳定
chunk/index 合并。结构变化、Asset Ready 发布和 GPU resource create 不在 Worker 中发生。

Instance transform 写入当前 `RenderFramePacket` 的 staging/arena；packet 与 SubmissionTicket
持有到 backend completion，不能 `makeRef` 或等价借用即将 reset 的其他 FrameArena。

首期没有 Transparent3D pass。Cooker 拒绝 Blend 材质；Mask 也后置，避免在没有 alpha cutoff、
排序和测试契约时偷偷当 Opaque 绘制。

## 3D 资源生命周期

```text
MeshRenderer AssetHandle (weak)
  -> extraction registers AssetLease in RenderFramePacket
  -> Mesh/Material dependencies resolve
  -> FrameResourceRef -> internal Buffer/Texture/Pipeline handles
  -> packet SubmissionTicket pins resources
  -> GPU completion
  -> ticket/lease release
```

Material lease 持有本帧所需 Texture/Shader dependency，Pipeline cache 持有 Shader/interface 和
RenderState dependency。卸载先令新 snapshot 不可见，再等待 RenderFramePacket、upload ticket 和
backend retirement；不能按固定 N 帧猜测 GPU 已完成。

## 性能门禁

`3D.StaticInstances.5000` 固定记录：

- total/visible/culled instance；
- submesh item、instance batch、draw、triangle、pipeline/material switch；
- extraction、bounds、culling、sort、batch、submit 和 GPU frame p50/p95/p99；
- FrameArena/staging current/peak、Tina-owned allocation 和 GPU estimated bytes。

正确性 checksum 覆盖可见 Entity/mesh/material 和稳定 sort order。不同 Camera、worker 数或资产
checksum 不能与旧 baseline 直接比较。绝对时间门禁只在固定机器建立后启用；无稳态分配、
stale handle、资源归零和 checksum 属确定性硬门禁。

## 正式 3D 验收

分三步，避免用 CPU extraction 或 Cube 冒充完整能力：

1. `tina_sample_3d_extraction`：Headless/Null Perspective、bounds/culling/sort/batch、aspect resize、
   300帧退出与 CPU 资源归零；当前已实现，但没有 GPU 画面；
2. `tina_sample_3d_infrastructure`：procedural indexed Cube、Perspective、depth、真实 instance buffer、
   resize、300帧退出与实际截图；M9-B 尚未实现；
3. `tina_sample_3d`：Cooked textured glTF -> Mesh/Material/Prefab、层级 Transform、多个深度遮挡
   对象、frustum culling 和至少一个 instance batch。

两者分别验证进程返回码、结构化日志、Render resource count、实际截图。正式样例还必须覆盖
最小化/恢复、aspect 更新、不支持 glTF 诊断和 shutdown 时 Buffer/Texture/Shader/Pipeline 全部
退役。只有第三步通过后，3D 产品路径才可计入 Legacy 删除门禁。
