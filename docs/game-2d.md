# 2D 产品架构

`tina_sample_2d` 是当前正式 2D 产品门禁，不再只是 fixture Sprite 样例。完整 feature 图通过
Catalog/TileMap/Scene/UI/Audio/Physics2D/FreeType/miniaudio 的300帧结构化与 Windows 视觉证据。

## 模块边界

```text
Game2DState
  -> Catalog / AssetSystem / typed Cooked payload
  -> TileMapInstance + CharacterController2D
  -> optional PhysicsWorld2D
  -> Scene::World (Camera2D + SpriteRenderer2D)
  -> RenderScene extraction
  -> RenderDevice Texture2D binding + bgfx Sprite2D pass
  -> retained UI + AudioEngine
```

- `tina_scene` 只认识 Entity、Transform、Camera2D、SpriteRenderer2D 与 backend-neutral key；
- TileMap、角色控制、选择高亮、Physics sync 和产品规则留在 Asset/产品 State；
- Render backend 不理解 tile/cell/gameplay，也不接收 AssetHandle；
- Box2D、bgfx、FreeType、miniaudio 均位于可选私有 adapter。

## 坐标与 Camera2D

- World：右手坐标、Y-up、单位米；
- Tile grid：整数 cell，使用显式 `cellSizeMeters`；
- UI/GLFW：窗口逻辑坐标；
- Render surface：framebuffer pixel；
- Camera2D：`FixedWorldHeight2D` 或 `PixelPerfect2D`，viewport 为规范化矩形。

`extractRenderSceneFromWorld()` 用当前 surface extent resolve Camera2D。framebuffer 0x0 表示 suspended，
跳过 camera view而不是修改配置。active Camera2D 超过1个会失败；非法 viewport、非有限数值或非法
pixel-perfect 组合不会静默 clamp。

World picking 在 Action Mapping 阶段使用 last-presented Camera2D 和 surface revision。只有未被 UI
consume/claim 的 primary pointer transition 才生成 `WorldPointerSample`；0 fixed-step 帧延迟消费时仍
使用锁存坐标，不按新 resize/Camera 重算。

## Sprite 与 GPU 资源

`SpriteRenderer2D` 保存：

- 非0 `fixtureSpriteKey`/产品 resource key；
- size、pivot、UV override；
- color、sorting layer、order、flip 与 visible。

它不保存 `GpuTextureId` 或 bgfx handle。产品路径先把 Cooked Texture2D 解析并上传为
`GpuTextureId`，再通过 `setSprite2DTextureBinding(spriteKey, texture)` 建立 backend binding。Scene
extraction 只写 key、transform、UV 与颜色。

Sprite 顺序为 sorting layer → order in layer → stable source ordinal。透明语义不能为了全局纹理合批
被重排；UI 使用独立 pass，始终不混入 World Sprite batch。

## TileMap 与角色控制

当前产品 recipe 从磁盘 cook/load Texture2D、Tileset、TileMap 与 AudioClip。`TileMapInstance` 保存可变
tile/chunk revision；`TileChunkDirtyCache` 只重建 revision 变化的可见 chunk，结构化输出记录 dirty
rebuild/cache hit。

`CharacterController2D` 使用 `IGridCollisionProvider` 进行确定性的 Tile AABB 运动。默认产品 demo 在
ground 后向右行走并撞墙；它与 Box2D dynamic body 共用同一 Tile solid 数据，但角色本身不是刚体。

受控 `--seed-tile-selection=x,y` 可以验证 logical→world→cell 命中和 selection highlight。默认 smoke
不注入点击，`tileSelectionHits=0` 合法；UI 点击不得穿透成世界选择。

## Physics2D 产品接入

在 `TINA_BUILD_PHYSICS2D=ON` 图中：

1. `collectAllSolidCellsForPhysics()` 从 Tile grid 收集 solid cell；
2. `syncTileMapSolidsToStaticBodies()` 原子创建 Box2D static body；
3. 产品 State 创建一个 dynamic crate；
4. 每个 fixed tick 调用 `PhysicsWorld2D::step()`，读取 contact 与 body state；
5. Scene sprite 使用 crate state输出可见结果。

当前 product-2d 证据为11个 static body、dynamic contact 非0、300次 physics step。完整细节见
[物理](physics.md)。

## UI 与 Audio

产品 HUD 当前包括 Label、Button、Checkbox、Slider、单行 TextEdit、65% ProgressBar 和一组
Windowed/Fullscreen RadioButton。结构化 evidence 验证控件数量、TextEdit UTF-8 初值、ProgressBar 值
与 Radio 互斥选择；Windows client-area 捕获验证可见、无明显裁剪/重叠和中文无乱码。

AudioClip 来自 Catalog lease。`AudioEngine` 接收 one-shot，完整 feature 图附加 miniaudio null-device，
报告 callback/mix 与 started completion；这不等同于真实扬声器质量门禁。

## 当前产品证据

完整图：

```powershell
cmake --preset windows-msvc-vnext-bgfx-product-2d
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_sample_2d tina_physics2d_tests tina_ui_tests tina_runtime_ui_tests `
           tina_ui_render_integration_tests tina_ui_freetype_tests tina_audio_tests `
           tina_audio_miniaudio_tests -- /m:1 /v:m
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe `
  --frames=300 --frame-delay-ms=0
```

已记录的产品报告满足：

- exit 0，`sample=tina_sample_2d`，`productGate=bgfx-physics-freetype-audio`；
- `catalogFromRecipeFile=true`、`catalogRecipeAssets=4`；
- 300次 extraction/physics step，角色 grounded/walk/hit-right；
- Texture upload、Tile sprites、Camera follow/interpolation、chunk cache；
- UI/TextEdit/ProgressBar/RadioButton、Audio Catalog lease、Physics contact；
- `stateExits=1`、`applicationShutdowns=1`、`uiRootsReleased=1`；
- `pixelCaptureOk=true`。

Windows 视觉证据见 [M12 Windows 证据](m12-evidence-windows.md)。测试数量不是永久基线；同轮完整
模块测试集合仍由 `TEST-002` 跟踪。

## 当前限制

- 无无限地图 streaming、通用 Tile 编辑器、Sprite skeletal animation、2D lighting 或网络 rollback；
- Cooked SpriteAsset 的完整 atlas/PPU metadata resolve 仍可扩展，当前产品使用 Texture2D + 显式 UV/key；
- GPU chunk mesh cache、复杂透明材质与多 camera/letterbox policy 尚未产品化；
- Linux 当前 tip、跨 GPU/DPI golden 与真实扬声器不由 Windows 报告证明。

这些限制不能通过向 gameplay 暴露 backend handle 绕过。任务与验收统一见 [Backlog](backlog.md)和
[测试说明](testing.md)。
