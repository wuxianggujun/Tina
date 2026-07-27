# Rendering

Tina 的公开 Render 边界是 backend-neutral `Tina::Render`；bgfx 只存在于 `tina_render_bgfx` 私有
实现。当前产品已经有 2D、3D、UI/Glyph 与 Texture2D/StaticMesh upload 路径，以及 EngineHost 侧
`RenderFramePacket` + FramePin + packet-local `FrameResourceRef` table + present-return CPU completion。Opaque3D 产品着色为 **experimental
metallic-roughness hybrid**：`setMesh3DLighting()` 一次提交0..4个 directional lights + 常量 ambient；
无 IBL/阴影/light component/culling。当前没有通用 pass scheduler 或通用 GPU submission fence；
Texture2D/StaticMesh 已有独立、backend-proven 的 GPU resource retirement completion。

## Target 边界

| Target | 当前职责 |
| --- | --- |
| `tina_render` | `IRenderDevice`、RenderSurface、RenderFrame、RenderScene、UI DisplayList、GPU generation IDs、Null backend |
| `tina_render_bgfx` | WindowSurface/bgfx 生命周期、Sprite2D、Opaque3D、UI solid/glyph pass、GPU texture/mesh storage |
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
packet 复用后立即失效，backend 只能在 `submitFrame()` 内同步解析。N16.2 已让全部 Sprite2D item 只保存
packet-local texture ref；Mesh3D item 仍使用当前 registry key，由 N16.3 之后的独立切片评估迁移。

所有 composition 采用唯一语义：`submitFrame()` 同步消费借用 view，成功 `present()` 返回后关闭 CPU
submission ticket 并释放 frame pin。该完成点只表示 Host/backend 已不再借用本帧 CPU 数据，**不表示 GPU
执行完成或 Asset 物理退役**。旧 `PresentSync`/`FrameDeferred` 分支、bgfx ledger wrapper、下一帧固定延迟
handoff 与 `bgfx::frame()` 假 fence token 已删除。GPU resource retirement 走下文独立 marker，不复用该
CPU ticket，也不把普通 frame number 描述为 fence。

Opaque3D（experimental MR hybrid）：产品 registry 通过单次 `setMesh3DMaterialBinding()` 原子提交
baseColor/MR/normal 与 Cooked Material v2 metallic/roughness factor；细粒度 setter 仅保留为低层 direct
SPI。submit 时分别绑定 `s_texColor`、`s_texMR`（glTF 打包：G=roughness、B=metallic）与
`s_texNormal`。`Mesh3DLightingDesc` 是唯一 lighting 提交模型：同步消费
0..4个 backend-neutral directional light 与 ambient；超容量、零方向、负 RGB/ambient 或非有限值失败。
着色 = baseColor × 贴图 ×（bounded directional Lambert + Blinn-Phong 近似 specular + ambient）；未绑定 baseColor 用
1×1 白；未绑定 MR 图时 metallic=0、roughness=1；未绑定 normal 图时只用几何法线。诚实限制：无通用
light component/culling、无 IBL、无 shadow。

## RenderScene

`RenderSceneBuilder` 在固定容量 storage 中事务式构建 Camera/Sprite/Mesh：

```text
beginFrame(surface facts)
  -> phase-local writer
  -> add Camera2D/PerspectiveCamera3D/Sprite2D/Mesh3D
  -> validate, cull, stable sort and batch
  -> commit borrowed view
```

当前能力包括：

- Camera2D projection、viewport、pixel snap 与 world picking；
- Sprite2D 视锥裁剪、layer/order/ordinal 稳定排序、相邻兼容 batch；
- PerspectiveCamera3D、frustum culling、Opaque3D depth/state 与 stable batch；
- framebuffer 0x0 suspended 路径；
- 容量/非法数值/非法 resource ref/key 失败时不发布半份 scene。

Scene/Runtime writer 不能创建 GPU resource。产品 State 在安全阶段上传并绑定资源；Sprite2D extraction
通过 phase-local `FrameResourceSink` intern 当前 binding，Mesh3D 继续输出 backend-neutral key。

## UI DisplayList 与 Glyph

UI paint 通过 integration 转为固定容量 DisplayList。当前 command/batch 支持 SolidQuad 与 Glyph，clip
为 axis-aligned scissor。Glyph 使用 UIContext-owned R8 atlas page：Runtime 在 submit 时借用像素，bgfx
创建或更新私有 atlas texture，并用 textured UI shader 绘制。

已实现的路径包括：

- Text/Glyph placement → `UIDrawCommandKind::Glyph`；
- atlas UV 与 page 校验；
- solid/glyph 按 clip、kind、atlas page batching；
- bgfx R8 atlas create/update、solid white texture 与 glyph texture binding；
- UI → Render integration tests 与 bgfx geometry tests。

rounded/stencil clip、Image widget、复杂 material 与跨 GPU golden 仍未完成。

## GPU 资源

`IRenderDevice` 当前提供可选资源 API：

| API | Null | bgfx |
| --- | --- | --- |
| `create/destroyTexture2DRgba8` | 逻辑 generation storage；同步 retire | 私有 RGBA8 texture；逻辑失效后 marker 延迟销毁 |
| `retireTexture2D(texture, pin)` | 成功同步释放 pin | 成功消费 pin，readback marker 后释放 |
| `createSprite2DTextureBinding` | device-instance allocator；bind 成功才消费非0 key | device-instance allocator；bind 成功才消费非0 key |
| `setSprite2DTextureBinding` | 校验/记录 binding | device binding key → texture |
| `create/destroyStaticMeshP3N3UV2` | 逻辑 mesh storage；同步 retire | 私有 VB/IB；逻辑失效后 marker 延迟销毁 |
| `retireStaticMesh(mesh, pin)` | 成功同步释放 pin | 成功消费 pin，readback marker 后释放 |
| `drainGpuRetirements` | 已完成，无操作 | owner-thread completion-only flush；有界失败返回结构化错误 |
| `createMesh3DBinding` | device-instance mesh allocator；成功才消费 key | device-instance mesh allocator；成功才消费 key |
| `setMesh3DBinding` | 校验/记录 binding | mesh key → GPU mesh |
| `createMesh3DMaterialBinding` | device-instance material allocator；成功才消费 key | device-instance material allocator；成功才消费 key |
| `set/clearMesh3DMaterialBinding` | 原子替换/整组清除三张纹理与 factors | 原子替换/整组清除三张纹理与 factors |
| `setMesh3DMaterialTextureBinding` | 校验/记录 binding | material key → base-color texture；Opaque3D MR submit **采样** `s_texColor`（默认 1×1 白） |
| `setMesh3DMaterialMetallicRoughnessTextureBinding` | 校验/记录 binding | material key → optional MR texture；未 bind 用默认 metallic=0/roughness=1 |
| `setMesh3DLighting` | 同步校验/复制有界描述 | 0..4 directional lights + ambient；shader 使用两个4×vec4 uniform array |
| `capturePrimaryFrameRgba8` | Unsupported | present 后异步截图路径 |

`createSprite2DTextureBinding()` 分配的 key 单调且解绑后不复用；backend bind 失败不消费候选 key。
caller-chosen `setSprite2DTextureBinding()` key 与 allocator-managed key 共用 device namespace，registry
管理期间不得混用。

Mesh3D mesh/material 分别使用独立的 device-instance allocator namespace。两类 key 都从2开始并分别保留
内置 mesh/material key 1。两类 key 都只在完整 backend bind 成功后消费，解绑后不复用；
caller-chosen setter 与同类 allocator-managed key 不得混用。`setMesh3DMaterialBinding()` 先完整校验三张
可选纹理与 factors，再原子替换整组状态；`clearMesh3DMaterialBinding()` 幂等清除整组状态。

`GpuTextureId`/`GpuMeshId` 是 backend owner 的 generation handle，不是 AssetHandle。销毁后 stale handle
失败；Asset Catalog 使用 `AssetId`。2D extraction resolver 把 live Sprite/Tileset handle映射并 intern 为
packet-local ref，backend 同步解析 descriptor 中的 binding key；3D Prefab 先映射 weak AssetHandle，
extraction resolver 再取得 backend-neutral mesh/material key，最后由 RenderDevice binding 映射到 GPU handle。

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
- Sprite2D textured quad pass；
- Opaque3D experimental metallic-roughness hybrid mesh/depth pass（单次有界0..4 directional lights）；
- UI solid/glyph pass；
- Texture2D/StaticMesh generation storage 与 key binding；
- Texture2D/StaticMesh backend-proven retirement marker、suspend flush 与 shutdown hard drain；
- present 后 primary framebuffer capture；
- D3D11/OpenGL/Vulkan 对应 embedded shader 选择（按构建与平台可用性）。

`tina_sample_2d` 已使用 Cooked Texture2D 与产品 sprite binding；`tina_sample_3d` 已使用 Cooked
StaticMesh/Material/Prefab 的 **双 mesh** engine-provided、State-owned registry binding（3D-001 / N15 Done），并通过原子
material bundle 提交 baseColor/MR/normal/factors，再调用 lighting API；bgfx Opaque3D 以 experimental MR
hybrid 着色。

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
           tina_ui_render_integration_tests tina_sample_2d tina_sample_3d -- /m:1 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_bgfx_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
```

测试映射与 Visual 证据规则见 [测试说明](testing.md)。`RENDER-001` 已落地 experimental MR +
MR/normal/factors，以及唯一 `Mesh3DLightingDesc` 有界4 directional-light 提交；产品 sample 一次提交3灯。
完整 PBR / IBL / shadow / light component / pass scheduling、通用 GPU submission fence 与跨 DPI/GPU
visual gate（`UI-003`）仍后置；Texture/Mesh resource retirement 已不属于这些后置项。
