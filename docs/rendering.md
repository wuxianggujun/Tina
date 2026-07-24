# Rendering

Tina 的公开 Render 边界是 backend-neutral `Tina::Render`；bgfx 只存在于 `tina_render_bgfx` 私有
实现。当前产品已经有 2D、3D、UI/Glyph 与 Texture2D/StaticMesh upload 路径，以及 EngineHost 侧
`RenderFramePacket` + FramePin + Null completion 首切片；没有通用 pass scheduler、PBR 或真 GPU fence
驱动的异步 completion。

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
- submit-call-local primary World `RenderSceneView`；
- submit-call-local primary UI `UIDisplayListView`；
- optional R8 `UIGlyphAtlasPageView`。

所有 view/span/pixel pointer 只在 `IRenderDevice::submitFrame()` 调用期间有效。backend 必须同步消费，
返回后不能保存。`submitFrame()` 返回 `Submitted` 或 `SkippedSuspendedSurface`；只有前者允许 Runtime
调用 `present()`。

`RenderFramePacket` + `FramePin` + `ISubmissionCompletionLedger` 已接入 EngineHost：submit 前可登记
Surface/GlyphAtlas pin；失败路径 abandon；shutdown 冲刷 deferred handoff。

Host 持有 **`unique_ptr<ISubmissionCompletionLedger>`**（经可选
`EngineCompositionFactories::createSubmissionCompletionLedger` 注入）：

| 路径 | 类型 | `completionMode` | 完成语义 |
| --- | --- | --- | --- |
| 默认 / Null / Headless | `NullSubmissionCompletionLedger` | `PresentSync` | present-return 即 complete 并释放 pin |
| Desktop `CreateEngine` | `BgfxSubmissionCompletionLedger` | **`FrameDeferred`** | present 后 `handOffDeferred` 保留 pin，**下一 present**（或 shutdown）再 complete；`lastPresentFrameToken()` = `bgfx::frame()` |

FrameDeferred 是双缓冲 lag，**不是**真 GPU fence 对象轮询；勿写成「GPU 已退役」。真 fence 见 backlog
`RENDER-FENCE` 剩余项。

Opaque3D unlit：`setMesh3DMaterialTextureBinding(materialKey)` 在 `submit` 时 `setTexture(0, s_texColor)`；
shader 输出 `baseColorFactor * texture2D`；未绑定 materialKey 使用 1×1 白贴图（非「只 bind 不采样」）。

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
- 容量/非法数值/非法 key 失败时不发布半份 scene。

Scene/Runtime writer 不能创建 GPU resource。产品 State 在安全阶段上传并绑定资源，再让 extraction 输出
对应 backend-neutral key。

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
| `create/destroyTexture2DRgba8` | 逻辑 generation storage | 私有 RGBA8 texture |
| `setSprite2DTextureBinding` | 校验/记录 binding | sprite key → texture |
| `create/destroyStaticMeshP3N3UV2` | 逻辑 mesh storage | 私有 VB/IB |
| `setMesh3DBinding` | 校验/记录 binding | mesh key → GPU mesh |
| `setMesh3DMaterialTextureBinding` | 校验/记录 binding | material key → base-color texture；Opaque3D unlit submit **采样** `s_texColor`（默认 1×1 白） |
| `capturePrimaryFrameRgba8` | Unsupported | present 后异步截图路径 |

`GpuTextureId`/`GpuMeshId` 是 backend owner 的 generation handle，不是 AssetHandle。销毁后 stale handle
失败；Asset Catalog 使用 `AssetId`，产品 resolver 显式映射 AssetId → key → GPU handle。

`UploadTicketLedger` 与 `NullSubmissionCompletionLedger` 均为 backend-neutral/Null 逻辑 completion
首切片。真实 bgfx resource API 可上传 Cooked Texture2D/StaticMesh；GPU fence 驱动的异步 retirement
仍待后续扩展。

## bgfx backend

当前私有 backend 已实现：

- native WindowSurface 初始化、resize/suspend、submit/present/shutdown；
- transient frame budget 与容量失败；
- Sprite2D textured quad pass；
- Opaque3D unlit mesh/depth pass；
- UI solid/glyph pass；
- Texture2D/StaticMesh generation storage 与 key binding；
- present 后 primary framebuffer capture；
- D3D11/OpenGL/Vulkan 对应 embedded shader 选择（按构建与平台可用性）。

`tina_sample_2d` 已使用 Cooked Texture2D 与产品 sprite binding；`tina_sample_3d` 已使用 Cooked
StaticMesh/Material/Prefab 的 **双 mesh** product key binding（3D-001 Done），并调用
`setMesh3DMaterialTextureBinding`（GPU 采样仍后置）。

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

测试映射与 Visual 证据规则见 [测试说明](testing.md)。当前后置项是 PBR/lighting/pass scheduling 与
Opaque3D baseColor 采样（`RENDER-001`）、真 GPU fence completion（RUNTIME-002 尾巴）与跨 DPI/GPU
visual gate（`UI-003`）。
