# 2D 产品架构

`tina_sample_2d` 是当前正式 2D 产品门禁，不再只是 fixture Sprite 样例。完整 feature 图通过
Catalog/TileMap/Scene/Particle/Trail/UI/Audio/Physics2D/FreeType/miniaudio 的300帧结构化与 Windows 视觉证据。

## 模块边界

```text
Game2DState
  -> Catalog / AssetSystem / typed Cooked payload
  -> TileMapStream -> resident TileMapInstance + CharacterController2D
  -> optional PhysicsWorld2D
  -> Scene::World (Camera2D + SpriteRenderer2D)
  -> Scene::ParticleSystem2D + Trail2D
  -> RenderScene extraction
  -> RenderDevice Texture2D binding + bgfx Sprite2D pass
  -> retained UI + AudioEngine
```

- `tina_scene` 的 World Sprite 与独立 `ParticleSystem2D`/`Trail2D` 只存 weak Sprite `AssetHandle`，
  extraction 显式借用共享 resolver 后写 backend-neutral key；
- TileMap 当前仍直接写 backend-neutral sprite key，但产品 key 由 `Sprite2DBindingRegistry` 生成，
  不再由游戏手写；
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

2D gameplay 不读取 GLFW key/axis。`InputActionBinding` 用同一模型表达 keyboard/pointer/gamepad button
与 gamepad axis，State 从 Simulation/Frame snapshot 读取浮点 Action value。Axis 的 deadzone/scale 与
`SumClamped`/`StrongestMagnitude` 合成在 Runtime mapper 中完成，多个已连接手柄可共同贡献；UI
consume/claim 后 digital/analog source 均不会穿透。运行时改键只通过栈顶
`FrameUpdateContext::inputActionRebinding()` 排队，下一 mapping frame 原子应用；完整设置页面和 binding
持久化不属于当前产品切片。

## Sprite 与 GPU 资源

`SpriteRenderer2D` 保存：

- copyable weak Sprite `AssetHandle`；
- size、pivot、UV override；
- color、sorting layer、order、flip 与 visible。

它不保存 `AssetLease`、`GpuTextureId` 或 bgfx handle。产品路径先把 Cooked Texture2D 解析并上传为
`GpuTextureId`，再由固定容量 owner-thread `Sprite2DBindingRegistry` 校验 Texture2D Handle，并调用
RenderDevice 实例 allocator 事务绑定 GPU texture。返回 key 在该 device namespace 内唯一、单调且不复用；
backend bind 失败不消费 key，同一 device 上的多个 registry 不会碰撞。allocator-managed registry 管理期间
不得混用 caller-chosen `setSprite2DTextureBinding()` key。Scene extraction 每帧继续借用 A1 的
`Sprite2DBindingResolver`，产品实现薄调用 registry，沿 Sprite 唯一 required Texture2D cooked dependency 校验
Store owner/generation、kind、payload 与 live binding，然后只写 key、transform、UV 与颜色。缺 resolver
或无法解析的 visible sprite 返回 `UnresolvedSprite`；hidden sprite 不触发解析。

Sprite 顺序为 sorting layer → order in layer → stable source ordinal。透明语义不能为了全局纹理合批
被重排；bgfx 按最终顺序扫描相邻且 `spriteKey` 相同的 Sprite，按连续区间分别绑定纹理并 submit。
例如 key 序列 A/A/B/A 会形成3个 batch，而不会重排成 A/A/A/B。除0以外的 key 都可参与该路径；
0仍作为非法资源 key 拒绝。UI 使用独立 pass，始终不混入 World Sprite batch。

产品 sample 当前上传两张 Cooked Texture2D，并通过 State-owned `Sprite2DBindingRegistry` 注册动态 key；
registry 借用 `TileMapResources` 中的 Store 与 `DeviceCapture` 中的 RenderDevice，两个外部 owner 都必须
覆盖 State/registry 生命周期。World 里的 crate/角色帧保存 Catalog Sprite handle，再由 borrowed resolver
调用 registry 解析。Particle/Trail 保存 Catalog Sprite handle，并分别通过显式借用的 resolver 调用 registry；
TileMap emit 仍保存并消费 registry 生成的 `u32` key，不再依赖手写 `1/2` binding 表。registry 借用
AssetStore 与 RenderDevice，不拥有 GPU texture、AssetLease 或 retirement；State RAII teardown 必须先成功
unbind 两项 binding，再 destroy texture。A3 已完成 FX Handle 化，但尚未覆盖 TileMap、3D 或
`FrameResourceRef`，因此 `ASSET-HANDLE-SCENE` 总项仍为 Partial。
Tile 与角色因此可以在同一 RenderScene 中保持排序语义并使用不同纹理，不再受历史 fixture key 1 限制。

## Sprite 动画

`SpriteAnimationClip` 是 Cooked asset kind 与 typed payload v1。payload 保存 `Once`、`Loop`、`PingPong`
模式、按 authoring 顺序排列的帧、每帧正有限时长，以及指向去重后 required Sprite dependency stream
的索引。writer/parser 限制最多4096帧并严格检查 schema、flags、大小、依赖索引和 duration；Asset typed
view 继续校验 Cooked kind/version、依赖数量、required Sprite kind，以及每个 dependency 都实际被帧引用。

Catalog recipe 支持：

```text
spriteanim <clip-id> <Once|Loop|PingPong> <sprite-id:duration-seconds>...
```

`SpriteAnimator2D` 接收已在 Asset/Scene 边界解析为 `SpriteRenderer2D` 的帧；每帧复制 weak AssetHandle，
但不持有 AssetLease 或 backend texture。它支持 Once 停在末帧、Loop、PingPong 反向经过内部帧、pause/play/stop/restart、
正倍速和跨多帧的大 delta；无效 clip、非正倍速及负数/非有限 delta 会显式失败。clip 帧在设置时复制，
`update()` 不分配。

产品 sample 从 Catalog 解析 Idle、Walk、HitWall 三个 clip（共5个 resolved frame），由角色 fixed-step
状态驱动 `Idle -> Walk -> HitWall`。HitWall 使用 Once clip，并把完成状态写入结构化门禁；角色当前帧
直接更新 Scene 的 `SpriteRenderer2D`，使用独立角色 Sprite handle/atlas binding。

## Particles 与 Trail

`ParticleSystem2D` 和 `Trail2D` 是 `Tina::Scene` 的两个 standalone owner-thread system，不是 World
component，也不依赖完整 AssetSystem 或 bgfx；它们只复制轻量、copyable weak Sprite `AssetHandle`，不持有
`AssetLease`、Cooked payload、GPU texture owner 或 resolver。二者在 `Create()` 时通过调用方
`memory_resource` 建立固定容量 PMR storage；后续成功的 emit/append、update 和 extract 不扩容。它们复用
调用方当前 phase 的 `RenderSceneWriter` 提交 Sprite2D，真实纹理由显式借用的共享
`Sprite2DBindingResolver` 映射为 registry `spriteKey`。

粒子系统对每个 `randomSeed`（包括0）使用固定确定序列。`emitBurst()` 在写入前拒绝空 Sprite handle，并
完成其余 burst validation、
剩余容量和稳定 key 空间检查；任一失败都不推进 RNG、next key 或 live set。成功粒子按单调 key 分配，
过期或 `clear()` 后也不复用。`update()` 先 preflight 所有 age，以及仍存活粒子的积分后位置；任何溢出
使整批状态不变，成功后才推进并压缩过期粒子。extract 按 `age/lifetime` 线性插值 size 与 color，并为
每个 live particle 即时解析其保存的 handle；没有 live particle 时不调用 resolver。

Trail 第一个 `appendPoint()` 只建立 anchor，后续有效非退化点各追加一个从旧 anchor 到新点的 segment；
`breakTrail()` 使下一点建立新 anchor，不跨断点连线。每段从创建时独立计 age/lifetime，宽度按各自
normalized age 在 start/end width 间线性插值。几何、容量或稳定 key 失败不修改 anchor、segments 或
next key；update 同样先 preflight 全部 age 后再推进与删除过期段。segment key 单调分配且不因过期复用。
`Trail2D::Create()` 拒绝空 Sprite handle；每次有 segment 的 extract 只解析一次 config 中的 handle 并复用
结果，空 Trail 不调用 resolver。

两者缺 resolver，或 stale/cross-store/wrong-kind/unbound handle 使 resolver 返回0时，都 fail closed 为
`SceneErrorCode::UnresolvedSprite`。resolver 与 `userData` 仅借用到当前 extract 返回，system 不保留它们。

## TileMap 与角色控制

当前 TileMap 唯一 root payload 是 schema v3。root 按 authoring 顺序保存 tile/object layers；layer ID 和
object ID 都是 map-wide 非零唯一稳定 ID。两类 layer 都保存 visibility、strict UTF-8 name/properties；
tile layer 不再内嵌完整 grid，只保存按 `{chunkY, chunkX}` 严格排序的非空 chunk ref（坐标、实际边缘尺寸、
非空计数、`AssetId`），缺失坐标是已知空块；object layer 仍在 root 保存 point/axis-aligned rectangle 及其
metadata。旧 schema v1/v2 均不双读。

Catalog recipe 的 TileMap 唯一语法为：

```text
tilemap <id> <tileset-id> <width> <height> <cell-size-meters>
tilelayer <stable-layer-id> <0|1-visible> <name>
property <key> <value>
row <local-id>...
endlayer
objectlayer <stable-layer-id> <0|1-visible> <name>
property <key> <value>
point <stable-object-id> <0|1-visible> <name> <x> <y>
objectproperty <stable-object-id> <key> <value>
rectangle <stable-object-id> <0|1-visible> <name> <x> <y> <width> <height>
objectproperty <stable-object-id> <key> <value>
endlayer
endtilemap
```

`row` 只能出现在打开的 `tilelayer` 中；每层必须 `endlayer`，地图必须 `endtilemap`。历史
`tilemap` 后直接写裸 `row` 的单层 recipe 会被拒绝，不存在 fallback。recipe 的 name/key/value 当前是
不含空白的单个 UTF-8 token；wire schema 本身仍执行 strict UTF-8/NUL/长度校验。Cooker 当前以固定
16×16 切分 tile rows，只为非空块生成独立 `TileMapChunk` v1 Cooked asset，并把它们登记为 root 的
`Required|Deferred` dependencies；root 的 eager load 因此不会把整张地图带入 Store。

`TileMapStream` 持有 root/tileset `AssetLease`、resident `TileMapInstance` 和固定容量 chunk slot。每帧唯一
调用顺序是 `updateDemand(camera/layer) -> AssetSystem::pump() -> commitReady()`；load/retain margin、每次
request budget 与 resident capacity 都显式有界。需求移出 retain window 时，Queued/Loading 请求会取消，
Resident chunk 会从 instance detach、释放 lease 并 logical unload。load window 中的 desired chunk 是强需求，
单独超过 capacity 时旧 active set 原样保留；retain window 只作 optional cache，空间不足时按最近一次成功
`updateDemand` 时位于 load window 的 recency 自动淘汰，Tile/collision 读取不会 touch recency。

`TileMapInstance` 只复制 root metadata/tileset 定义并保存当前 resident chunk cell；`layer(id)` 继续暴露
metadata/object borrowed view。`tileIdAt()`、`setTile()`、`chunkRevision()`、chunk extraction、dirty cache、
sprite emit 和 solid query 都要求显式 layer ID；访问 root 引用但尚未 resident 的块返回
`TileMapChunkNotResident`。每次重新 attach 取得新的 residency generation，`TileChunkDirtyCache` 同时比较
generation 与 content revision，避免 unload/reload 后误命中旧缓存。

产品 recipe 使用三个稳定 layer：visible visual tile layer `10`、hidden collision tile layer `20`、visible
gameplay object layer `30`。渲染只显式提交 layer `10`；visibility=false 使 layer `20` 不进入 sprite emit，
但 `TileMapGridCollision(stream.map(), 20)` 仍可把 resident cells 作为碰撞数据。该 grid 借用 instance，
因此产品先把 stream 放到最终地址再构造 grid，并在 controller 查询前推进 streaming。object `101` 是
`player_spawn` point，object
`102` 是 `crate_spawn` rectangle；sample 按稳定 ID 和 properties 校验并消费它们，分别初始化角色位置和
dynamic crate 位置。

`CharacterController2D` 使用 `IGridCollisionProvider` 进行确定性的 Tile AABB 运动。默认产品 demo 在
ground 后向右行走并撞墙；它与 Box2D dynamic body 共用同一 Tile solid 数据，但角色本身不是刚体。

受控 `--seed-tile-selection=x,y` 可以验证 logical→world→cell 命中和 selection highlight。默认 smoke
不注入点击，`tileSelectionHits=0` 合法；UI 点击不得穿透成世界选择。

## Physics2D 产品接入

在 `TINA_BUILD_PHYSICS2D=ON` 图中：

1. 启动 demand/pump/commit visual `10` 与 collision `20`，确认两块 resident；
2. `TileMapGridCollision(stream.map(), 20)` 显式选择 hidden collision tile layer；
3. `collectAllSolidCellsForPhysics()` 从该 layer 收集 solid cell；
4. `syncTileMapSolidsToStaticBodies()` 通过公开 `createBody()` + `createShape(Box)` 原子创建 static terrain；
5. 产品 State 从 object `102` 创建一个 dynamic crate，并显式挂接 Box shape；
6. 同一 World 创建 circle sensor 和一个远离主场景的 spring Distance joint；
7. 每个 fixed tick 调用 `PhysicsWorld2D::step()`，读取 contact/sensor event、body state 与 joint state；
8. Scene sprite 使用 crate state 输出可见结果。

当前 product-2d 证据为11个 static body、dynamic contact 非0、300次 physics step、sensor enter/exit 各1次
与 `physicsJointReady=true`。完整细节见 [物理](physics.md)。

## UI 与 Audio

产品 HUD 当前包括 Label、Button、Checkbox、Slider、单行 TextEdit、65% ProgressBar 和一组
Windowed/Fullscreen RadioButton。结构化 evidence 验证控件数量、TextEdit UTF-8 初值、ProgressBar 值
与 Radio 互斥选择；Windows client-area 捕获验证可见、无明显裁剪/重叠和中文无乱码。

AudioClip 来自 Catalog lease。`2D-AUDIO-ADV / N7` 已完成 voice gain/pitch/pan、fade、transient one-shot
retirement，以及固定容量 PCM stream 的 submit/EOF/underrun/cancel/terminal backpressure/shutdown 收口。
产品 sample 通过 owner thread 显式调用固定帧数 `mixRealtime()`，确定性验证 stream drain、Stopped、自动
retire 与零 underrun；miniaudio callback 调用 mixer 和 device lifecycle 由
`tina_audio_miniaudio_tests` 的 adapter 测试证明，不把异步 callback 调度当作产品 stream 门禁。

## 当前产品证据

完整图：

```powershell
cmake --preset windows-msvc-vnext-bgfx-product-2d
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_sample_2d tina_scene_tests tina_physics2d_tests tina_ui_tests tina_runtime_ui_tests `
           tina_ui_render_integration_tests tina_ui_freetype_tests tina_audio_tests `
           tina_audio_miniaudio_tests -- /m:1 /v:m
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe `
  --frames=300 --frame-delay-ms=0
```

当前 sample 的结构化产品门禁要求：

- exit 0，`sample=tina_sample_2d`，`productGate=bgfx-physics-freetype-audio`；
- `catalogFromRecipeFile=true`、`catalogRecipeAssets=14`（含2个 cooked chunk）、`texturesUploaded=2`；
- `evidenceSchema=11`，`spriteBindingTextures=2`、`spriteBindingsReleased=2`、
  `spriteBindingTexturesDestroyed=2`、`spriteBindingResolverHits>0`，并且
  `particleSpriteBindingResolverHits>0`、`trailSpriteBindingResolverHits>0`；这些字段都进入 evidence hash；
- `tileMapStreamRequests=2`、`tileMapStreamCommitted=2`、`tileMapStreamResident=2`，且每个 frame 都推进
  demand/pump/commit；
- `objectLayerConsumed=true`、`objectLayerObjects=2`，稳定 object `101/102` 已被产品逻辑消费；
- 300次 extraction/physics step，角色 grounded/walk/hit-right；
- Tile/角色双纹理 upload/binding、连续 sprite batch、Camera follow/interpolation、chunk cache；
- 三个动画 clip 来自 Catalog，共解析5帧；Idle/Walk/HitWall 均进入，HitWall Once clip 完成；
- 固定粒子容量12、seed `1414090305`、初始发射10，300帧时 expired=4、
  active/extracted=6；Trail 容量8、创建/active/extracted segment=3、break=1；
  `fxInitialFingerprint` 是32字符小写 hex；其内部 schema 2 用 Store 解析出的稳定 Sprite `AssetId`，不把
  瞬时 generation handle bits 或 render key 写入指纹，并覆盖确定性的初始粒子/Trail 状态；
- UI/TextEdit/ProgressBar/RadioButton、Audio Catalog lease、Advanced Audio owner-thread deterministic mix、
  Physics contact；
- `physicsSensorEnters>0`、`physicsSensorExits>0`、`physicsJointReady=true`；
- `stateExits=1`、`applicationShutdowns=1`、`uiRootsReleased=1`；
- `pixelCaptureOk=true`。

既有 Windows 视觉与完整模块测试证据见 [M12 Windows 证据](m12-evidence-windows.md)；当前代码门禁还会
校验上述双纹理与动画字段。可复现脚本：
`tools/windows/RunProduct2dGate.ps1`（TEST-002）。测试数量不是永久基线。

`--frames>=60` 时产品 State 会在收尾前 `requestPush` 一层暂停 overlay（block fixed/frame/UI below，
仍 extract 下层世界），约 3 帧后 `requestPop`，JSON 输出 `pauseOverlayPushes/Pops/Frames`
（RUNTIME-001 产品证据）。短 smoke（如 30 帧）不推 overlay。

`updateUI` 每帧从 `UIUpdateContext::committedSemantics()` 重建 `UIAccessibilityTree` 并经
`UIAccessibilityProbeProvider` 发布；JSON 输出 `accessibilityPublished`、`accessibilityNodeCount`、
各 role 命中标志（UI-002-SPI 产品证据，**非**真机 UIA/AT-SPI）。

## 组合入口（接线税）

产品 sample 不再手写 `EngineCompositionFactories`（GLFW/bgfx/Task/Audio/FreeType）。`tina_sample_2d`
经 `Tina::Desktop::CreateEngine(config, options)` 启动；仅在需要帧捕获证据时通过
`CreateEngineOptions::wrapWindowSurfaceRenderDevice` 包装 `IRenderDevice`（见
`samples/2d_tilemap_bgfx/DeviceCapture.hpp`）。业务仍在 `TileMapBgfxState`；EngineHost 仍是唯一组合根。

## 当前限制

- 当前 streaming 是固定容量 Camera/layer demand owner，已有 retain-window demand-recency LRU，但不包含
  优先级 IO 调度、通用 Tile/Scene 编辑器、自动把任意 object layer 转成完整 gameplay、旧 schema
  migration、2D lighting、navigation 或网络 rollback；
- Cooked SpriteAsset 的完整 atlas/PPU metadata resolve 仍可扩展，当前产品使用 Texture2D + 显式 UV/key；
- GPU chunk mesh cache、复杂透明材质与多 camera/letterbox policy 尚未产品化；
- 当前 2D-FX 是 CPU fixed-capacity Sprite2D extraction，不包含 Cooked FX asset schema、effect graph/editor、
  GPU particle simulation 或 mesh-ribbon trail；
- Physics2D 当前 shape 为 Box/Circle/Capsule、joint 为 Distance；polygon/chain 与更多 joint 类型未产品化；
- Linux 当前 tip、跨 GPU/DPI golden 与真实扬声器不由 Windows 报告证明。

这些限制不能通过向 gameplay 暴露 backend handle 绕过。任务与验收统一见 [Backlog](backlog.md)和
[测试说明](testing.md)。
