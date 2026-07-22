# 2D 游戏架构

> 状态：vNext 候选冻结。本文定义正式 2D 产品路径，不把“显示一张 Sprite”当作完整验收。

当前 M8-B 完成后端无关 RenderScene extraction foundation：固定容量 writer 接受解析后的 Camera2D/
Sprite2D 值，执行稳定排序、保守裁剪和 pixel snap，并由 Headless/Null
`tina_sample_2d_infrastructure` 验证 300 帧 Runtime handoff。M9-C 又完成私有 bgfx Sprite2D fixture
pass。**M8-C0** 又在 `tina_scene` 落地 `Camera2D` / `SpriteRenderer2D` 组件存储与
`extractRenderSceneFromWorld`（读 World 组件 → 写现有 `RenderSceneWriter`）；Sprite 首切片使用
`fixtureSpriteKey`（非 AssetHandle 解析）。正式 Asset 解析、Runtime Phase Context World 能力、Prefab、
MeshRenderer3D、以及 `tina_sample_2d` 厚 State 改写仍为 Deferred。

## 范围与模块边界

Tina 的通用 `tina_scene` 只认识 Entity、Transform、Orthographic Camera 和 Sprite render
component。TileMap、角色控制、水、昼夜、玩法碰撞和关卡规则属于游戏/2D feature 层，不得
反向进入 Scene core。

```text
Game2DState
  -> gameplay systems / TileMap instance / CharacterController2D
  -> tina_scene World + Transform + Camera2D + SpriteRenderer2D
  -> Render Scene Extraction
  -> RenderScene.Sprite2D items
  -> tina_render batching
  -> tina_render_bgfx private backend
```

World 不读取 GLFW/`PlatformFrameView`，不 include 具体 TileMap，不持有 Renderer、ShaderManager、view id
或 bgfx handle。输入先映射成 Simulation Action，再由 Game2DState/玩法系统写 World command。

## 坐标与 Camera2D

- 世界统一为右手坐标、Y-up、-Z forward、单位米；2D 内容位于 XY 平面；
- UI 使用左上原点的逻辑像素，不能与 World 坐标混用；
- Tile grid 使用整数 cell coordinate；TileMap 明确 `cellSizeMeters`，不把像素当世界单位；
- GLFW logical coordinate、framebuffer pixel、UI logical coordinate、World meter 和 Tile cell
  必须通过明确 helper 转换，World 不读取窗口尺寸。

Camera entity 的位置和旋转统一来自 `WorldTransform`，投影 component 不再重复保存 center/rotation。
首期用两个互斥投影模式避免 `orthographicHeight` 与 `pixelsPerUnit` 同时成为权威量：

```cpp
struct FixedWorldHeight2D {
    float heightMeters = 18.0f;
};

struct PixelPerfect2D {
    float referencePixelsPerMeter = 16.0f;
    std::uint32_t referenceHeightPixels = 288;
};

enum class PixelSnapPolicy : std::uint8_t {
    Disabled,
    CameraTranslation,
    CameraAndSprites,
};

struct Camera2D {
    std::variant<FixedWorldHeight2D, PixelPerfect2D> projection;
    RenderNormalizedViewport normalizedViewport{0.0f, 0.0f, 1.0f, 1.0f};
    PixelSnapPolicy pixelSnap = PixelSnapPolicy::Disabled;
    bool active = true; // M8-C0: multi-camera selection uses active flag; >1 active fails extract
};
```

M8-C0 已实现：`include/tina/scene/Camera2D.hpp`（POD + `isValid`）、`World::setCamera2D` /
`camera2D` / `clearCamera2D`，以及 extract 时调用 `Render::makeResolvedCamera2DInput`（表面 viewport
由 `ExtractRenderSceneParams` 传入；0×0 跳过 setCamera2D）。

Aspect 只来自当前 `WindowSurfaceSnapshot` 的有效 viewport，不能由游戏缓存旧窗口宽高。Resize、DPI
和最小化在帧边界生效。`FixedWorldHeight2D` 的实际 framebuffer PPM 为
`viewportPixelHeight / heightMeters`；`PixelPerfect2D` 使用
`integerScale = max(1, floor(viewportPixelHeight / referenceHeightPixels))`、
`actualPPM = referencePixelsPerMeter * integerScale`，再以 `viewportPixelHeight / actualPPM` 反推
可见 world height。配置值和推导值不能混用。

Camera 值在 component command commit 和 Render Scene Extraction 两端校验：`heightMeters`、
`referencePixelsPerMeter` 必须 finite 且大于0，`referenceHeightPixels` 必须大于0。`RectF` 在此处
固定表示 `{x, y, width, height}`；normalized viewport 必须 finite，`x/y >= 0`、`width/height > 0`，
且 `x + width <= 1`、`y + height <= 1`，越界不做静默 clamp。非法 Camera 不生成 RenderView，
并按 component revision 去重报告诊断。Surface 的有效 framebuffer viewport 为0表示
`Suspended`，不是 Camera 配置错误，此时既不提取该 view，也不 clear/present。

Pixel snap 只影响最终 render transform，不回写 Simulation Transform。两种模式都必须使用
当前帧推导出的 actual framebuffer PPM 对平移进行确定量化，不能直接使用配置中的 reference
PPM。`FixedWorldHeight2D` 可选择任一 policy；`PixelPerfect2D` 强制
`PixelSnapPolicy::CameraAndSprites`，其他组合是非法 Camera 配置而不是运行时猜测。Camera follow、
插值与 snap 的固定顺序为：

```text
previous/current interpolation -> camera view transform -> optional pixel snap -> framebuffer
```

首期支持 Stretch-free 的 aspect extend 策略；固定逻辑分辨率 letterbox 和多 Camera 后置，但
接口预留 normalized viewport，不把策略硬编码进 shader。

World picking 在 Gameplay Action Mapping 阶段完成一次：只有未被 UI consume/claim 的 Pointer
transition 实际形成 Simulation edge，才用该 `PlatformFrameView` 对应的 last-presented Camera2D latch
与 primary-window logical extent 将 logical coordinate 转换为
`WorldPointerSample { worldX, worldY, cameraRevision, surfaceRevision, inputSequence, hit }`。
Simulation edge 即使跨0 fixed-step 帧保留，也保存这份 sample，不在消费 tick 用新 Camera 或 resize
重新换算；viewport 外输入返回明确的 `hit=false` no-hit，缺 last-presented camera 是结构化失败。

## Sprite 数据契约

游戏组件保存资产引用和语义属性，不保存 GPU 资源：

```cpp
enum class SpriteOverrideFlags : std::uint8_t {
    None = 0,
    Size = 1 << 0,
    Pivot = 1 << 1,
};

// M8-C0/C2 已落地的 Scene 组件（fixture 路径；AssetHandle 解析 Deferred）
struct SpriteRenderer2D {
    std::uint32_t fixtureSpriteKey = 0; // non-zero product/fixture resource seed
    SpriteOverrideFlags overrides = SpriteOverrideFlags::None; // Size | Pivot | UvRect
    Vec2 sizeOverrideMeters{1.0f, 1.0f};
    Vec2 pivotOverride{0.5f, 0.5f};
    SpriteUvRect uvRectOverride{}; // only when UvRect flag; else extract uses [0,1]
    ColorRgba8 color{}; // default white
    std::int16_t sortingLayer = 0;
    std::int32_t orderInLayer = 0;
    bool flipX = false;
    bool flipY = false;
    bool visible = true;
};

// 目标产品形态（Deferred）：AssetHandle<SpriteAsset> + blend + typed UV/PPU resolve
```

M8-C0 已实现：`include/tina/scene/SpriteRenderer2D.hpp`、`World::setSpriteRenderer2D` /
`spriteRenderer2D` / `clearSpriteRenderer2D`，`extractRenderSceneFromWorld` 将可见 sprite 写为
`RenderSprite2DInput`（pivot 调整几何中心、Z 轴四元数 → `rotationRadians`、size override 或默认 1m）。
`fixtureSpriteKey == 0` 在 set 与 extract 两侧均拒绝。**M8-C2** 增加可选 `SpriteOverrideFlags::UvRect`
+ `SpriteUvRect`，extract 转发到 `RenderSprite2DInput::{u0,v0,u1,v1}`（与 RenderScene 校验一致：
finite、[0,1]、严格 `u0<u1`/`v0<v1`）；无 flag 时仍写全图 `[0,1]`。`tina_sample_2d` 角色/crate 用
半张 atlas UV 恢复 C1 前外观；Cooked Sprite UV 解析仍 Deferred。

`SpriteAsset` 目标仍是 Cooked Asset（引用 `Texture2DAsset`、像素 rect、规范化 UV、默认 pivot 与
PPU）。未设置对应 override flag 时，产品路径 size 由像素 rect / PPU 推导；本切片未接 AssetSystem，
无 Size flag 时使用 1×1 m 默认。Atlas 是批处理组织方式，不改变组件 API。

M9-C 当前可见 Sprite 只使用内置 `spriteKey=1` fixture，并由私有 bgfx backend 直接生成 transient
P2/UV2/ABGR geometry；它不解析 `SpriteAsset`、`Texture2DAsset`、Atlas、Cooked Catalog 或 Manifest。
因此它只能作为基础设施样例和 adapter 测试证据，不能写入正式 2D 产品验收。

Cook profile 定义唯一 `canonical2DPixelsPerMeter`，记录进 Catalog 和每个未显式指定世界尺寸的
SpriteAsset。`PixelPerfect2D::referencePixelsPerMeter` 必须与当前 Catalog 的 canonical 值一致；
不一致的 Camera 不生成 view。PixelPerfect view 强制 nearest sampler。兼容性校验不只看
WorldTransform，而是在 interpolation、Camera view（含 Camera rotation）、resolved asset pixel
rect、asset/default 或 size override、pivot、flip、sprite transform、snap 和 viewport mapping
全部应用后检查最终 sprite-to-framebuffer 2D affine transform：每个 texture-texel basis vector
必须恰好映射到一个 framebuffer axis，绝对长度为正整数 pixel，两个 basis 正交；最终 texel
origin 必须位于整数 framebuffer pixel grid。这个检查自然把 Sprite 相对 Camera 的旋转、Size
override 和父层级 transform 纳入契约。任一条件不满足时按 Sprite/component revision 去重产生
结构化诊断并跳过该 Sprite，不静默降级；需要任意缩放、旋转或 filtering 的内容应使用
`FixedWorldHeight2D`。这样“PixelPerfect”是基于最终变换可验证的渲染契约，而不只是 Camera 名称。

Sprite 的语义顺序固定为：

```text
RenderView -> sortingLayer ascending -> orderInLayer ascending -> stable EntityId
```

较大的 layer/order 后绘制并位于前景；Sprite 排序不隐式读取 Transform Z。透明 Sprite 不允许
为了纹理合批跨语义顺序全局重排。Renderer 只合并相邻且 pipeline、atlas
page、sampler、clip/depth 状态兼容的 item；需要大量合批时由 Atlas 和合理 layer/order 解决，
不能牺牲遮挡正确性。`Sprite2D` 是 World pass，UI 永远由独立 UI pass 在其后绘制。

## TileMap 所有权与数据流

TileMap 属于产品 2D feature，不进入 `tina_scene`。首期 Cooked 类型：

- `TilesetAsset`：Tile 到 Sprite/动画/碰撞材料的映射；
- `TileMapAsset`：地图尺寸、cell size、layer metadata、chunk 索引和初始 tile 数据；
- `TileCollisionAsset`：规范化 solid/one-way/trigger/material flags；可以作为 TileMap 的子 payload；
- `SpriteAnimationClipAsset` 后置到出现真实动画消费者时，M8 不为它阻塞基础迁移。

运行时 `TileMapInstance` 保存游戏可变 tile、layer 和每 chunk revision。默认 chunk 尺寸由资产
设置并在 Cooker 校验为合理的2次幂；文档不把某个固定尺寸当 API。编辑 tile 只标记受影响的
render/collision chunk dirty。

```text
Tile edit command
  -> fixed-step TileMap commit
  -> increment affected chunk revision
  -> rebuild collision boundary if needed
  -> 2D extraction receives visible TileChunkView(revision)
  -> resolve AssetHandle + update only changed chunk cache
  -> emit backend-neutral TileChunkRenderPacket / SpriteRenderItem
```

`TileChunkView` 只存在于 2D gameplay/extraction 边界，包含 Tina 数据、AssetHandle、只读 tile
span/revision 和 world bounds。Scene/2D integration 解析资产并生成只含 `FrameResourceRef`、几何
slice、sort key 和 bounds 的 `TileChunkRenderPacket`；`RenderFrame` 以后不再携带 tile span 或
AssetHandle。Game 不能创建 vertex/index buffer，bgfx adapter 也不理解 TileMap/tile 语义。

相机视锥只访问 World bounds，先做 chunk culling，再展开可见 tile。空 chunk 不进入 extraction。
动态水等高频变化数据必须有独立预算，不能让单个 cell 变化无条件重建整张地图。

## 2D 碰撞与 Box2D 分工

首期采用明确的混合方案，而不是把所有对象强行塞入一个后端：

| 对象 | 权威实现 | 原因 |
| --- | --- | --- |
| 静态格子地形 | `IGridCollisionProvider` / Tile AABB | 查询确定、适合平台跳跃和大地图 |
| 角色控制器 | `CharacterController2D` swept AABB | 可控的坡面/台阶/单向平台语义，避免刚体角色不稳定 |
| 动态刚体与约束 | Box2D 3.x | 成熟 solver、sleeping、contact 和 query |
| Tile 与动态刚体接触 | Tile collision adapter 生成/更新 Box2D static chunk fixtures | 保持动态物体和静态地图碰撞一致 |

`IGridCollisionProvider` 是只读小接口，只暴露批量 AABB/sweep/query 和 tile material，不暴露
具体 `TileMap` 类。Character controller 和玩法系统依赖该接口；`tina_scene` 不依赖它。

Physics/Transform 同步固定为：

- Static：创建/显式重建时 World -> Physics；
- Kinematic：每个 step 前 World current -> Physics；
- Dynamic：每个 step 后 Physics -> World current；
- Teleport 使用显式 command，并清理或保留 velocity 的策略必须由调用者选择；
- Dynamic body 首期不能挂在具有非单位父 scale 的 Transform 下；不支持组合返回诊断；
- Box2D contact 与 Tile controller hit 在 fixed tick 末转换为带 EntityId/PhysicsBodyId generation 的
  gameplay event，回调栈中不修改 World。

## 2D 每帧数据流

```text
PlatformFrameView
  -> UI routing / consumption
  -> Simulation Action latch
  -> Game2DState::fixedUpdate
       gameplay intent
       character/grid collision
       Box2D step
       contact events
       TileMap/World command commit
       Transform propagation
  -> Game2DState::updateFrame
       camera follow / presentation model
  -> Game2DState::extractRenderScene
       Camera2D + SpriteRenderItem + TileChunkRenderPacket
  -> UI update / DisplayList
  -> RenderFrame
```

水、昼夜和粒子必须明确属于 Simulation 还是 Presentation。影响玩法的水/昼夜使用 fixed tick；
纯视觉粒子可以使用 frame delta，但不能反向修改 Simulation。Sprite animation 首期后置，
接入时动画状态由 fixed/frame domain 显式选择，不能同时读取同一 edge。

## 性能与内存门禁

正式基准至少包含：

- `2D.SpriteBatch.10000`：大部分 Sprite 来自少量 Atlas page；记录 item/batch/draw/texture switch；
- `2D.SpriteSort.MixedLayers`：验证排序稳定、透明遮挡和无稳态分配；
- `2D.TileMap.Scroll.256x256`：Camera 滚动、chunk culling、可见 tile 与 rebuild 数；
- `2D.TileMap.DirtyChunks`：单点/矩形编辑只更新受影响 chunk；
- `2D.Collision.TileAABB.Queries`：角色 sweep 和批量 query p50/p95/p99；
- `2D.UIOverlay.InputTransitionConsumptionView`：UI 覆盖世界时 Pointer 不穿透。

记录 `visibleChunks`、`culledChunks`、`visibleSprites`、`batches`、`draws`、`textureSwitches`、
`tileRebuilds`、`collisionQueries` 和各阶段 p50/p95/p99。Fixed Update、Render Scene Extraction 的
Tina-owned 稳态动态分配增量必须为0。

## 正式 2D 验收

`tina_sample_2d` 不能只显示一张 Sprite，也不能由 M9-C 的 fixture Sprite 样例替代。
M10-A36–A44 已在 `windows-msvc-vnext-bgfx-product-2d` 上落地产品门禁：磁盘
`sample_2d.recipe` → cook/load Texture2D+Tileset+TileMap、CharacterController 脚本行走、
UI HUD panel/Label/Button（可选 FreeType 中文）、至少一个 Physics2D dynamic crate 与 Tile
static 交互；JSON `sample=tina_sample_2d` + `catalogFromRecipeFile=true`。

**M10 收口（A39–A44 pointer / selection 产品闭环，tip `70618808`）：** 默认不再开 M10-A45。

| 切片 | 行为 |
| --- | --- |
| A39 | 点击 HUD Button 不穿透世界 pointer（`tina_runtime_ui_tests` 合成；smoke 不点） |
| A40–A42 | logical→world pick + last-presented latch + `worldPointerSample` |
| A43 | `fixedUpdate` 只消费未 UI 消费的 `Pressed + hit`；map-local bottom-left + 半开 cell |
| A44 | 选中格可见高亮；CLI `--seed-tile-selection=cellX,cellY` 受控门禁 |

- **默认 smoke：** `--frames=300` 可不点；`tileSelectionHits=0` / 无高亮合法。
- **受控门禁：** `--seed-tile-selection=1,1`（建议 300 帧）要求 hits≥1、`lastHighlightSprites=1`、
  `selectionHighlightSprites==renderExtractions`；sample-private，**不是** OS/GLFW 真点击。
- 完整 cooker CLI / cgltf / 厚 world-pick Game SDK 仍 **Deferred**。

已有基线：中文 FreeType Label + HUD Button 接线、角色脚本化右走撞墙、Physics2D crate、
连续 300 帧退出与资源/lifecycle JSON。

Legacy 删除前仍须补齐/加强（**非** A39–A44 阻断）：

- 最终生产 Cooker/Catalog/Manifest 全量（当前磁盘 fixture recipe + temp catalog）；
- Orthographic Camera：M11-B0 投影 resolve；M11-B2 样例 follow + `FrameTiming.interpolation`
  插值呈现 + map clamp（sample-private）；RenderScene pixel snap 仍在 commit；
- 多 layer Sprite、透明混合；Tile chunk culling 已有；M11-B1 已加 CPU `TileChunkDirtyCache`
  revision 门禁（asset 300 帧压力 + `tina_sample_2d` 产品 JSON）；GPU chunk mesh cache 仍可后置；
- 稳定截图回归；输入 / 日志 / 返回码分别留证据（pointer 路径证据见上表）。

首期明确不实现无限地图 streaming、复杂 Tile 编辑器、Sprite skeletal animation、GPU particle、
2D lighting 或网络 rollback；它们不能通过把 backend handle 暴露给玩法代码临时绕过架构。
