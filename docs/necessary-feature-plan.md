# Tina 必要功能推进计划

## 目的与执行规则

本计划覆盖 Tina vNext Runtime 的必要 2D/3D 能力切片。事实优先级固定为：当前源码、CMake target、测试和 sample 实际运行结果；其次是 Accepted ADR；主题文档仅作为线索。每个切片完成时，必须同步更新本文件、`docs/backlog.md` 和对应主题文档。

统一约束：

- `EngineHost` 是唯一非全局组合根；不新增 Singleton 或 Service Locator。
- 公共头和 Game SDK 不暴露 bgfx、GLFW、Box2D、cgltf 等第三方类型。
- Runtime 只读取 Cooked 产物；Cooker 失败时不发布半包；`AssetId` 在 load 时不得随机生成。
- 破坏性 schema/API 变更一次性更新 cooker、runtime、测试、sample recipe、门禁字段和文档；不保留旧格式双读或旧 API 兼容别名。
- 每项完成前运行最小受影响 target、直接 GoogleTest executable 和对应 sample 短 smoke；不以 CTest 替代直接测试。

## 切片总览

| 顺序 | 工作项 | 当前状态 | 完成定义 |
| --- | --- | --- | --- |
| N1 | 2D-TILEMAP-LAYERS | 已完成 | 有序 tile/object layer、稳定 ID、visibility/properties、显式渲染和碰撞 layer 选择已贯通 Cooked、runtime、sample 与测试 |
| N2 | 2D-PHYSICS-EXPAND | 已完成 | backend-neutral 多 shape、sensor enter/exit、distance joint、独立 generation handle 与级联 retirement 已覆盖；Box2D 保持 PRIVATE |
| N3 | 2D-INPUT-ADV | 已完成 | Runtime 单一 unified binding 已覆盖 analog deadzone/缩放/合成、运行时 rebind、UI consume/claim 后输入不穿透 |
| N4 | 3D RENDER-001 | 已完成 | Opaque3D 使用唯一有界 0..4 directional-light 提交；Null/bgfx/shader/sample/test 同步，完整 PBR 边界保持明确 |
| N5 | RENDER-FENCE | 已完成 | CPU submission completion 与 GPU resource retirement 已分离；bgfx readback marker、AssetLease pin、suspend/shutdown drain 与测试已贯通 |

## N1 - 2D-TILEMAP-LAYERS

### 已完成契约

1. `TileMapPayload` 的唯一当前格式是 schema v2：地图保存 authoring-order layer 容器，schema v1 不双读。
2. Tile/object layer 都有 map-wide 非零唯一稳定 ID、visibility、strict UTF-8 name/properties；tile layer 有独立 row-major grid。
3. Object ID 也 map-wide 非零唯一；对象支持 point 与 axis-aligned rectangle，并拥有独立 visibility 与 UTF-8 name/properties。
4. `TileMapInstance`、chunk view/dirty cache/render 和 solid query 全部围绕显式 `TileMapLayerId` 工作；不存在默认第0层。
5. `TileMapGridCollision(map, layerId)` 显式选择碰撞层。hidden tile layer 不渲染，但仍可参与 collision。
6. Cooker 在发布 Manifest 前校验 TileMap v2、恰好一个 required Tileset dependency、依赖 kind/version，以及每个非零 tile localId；失败不发布半包。

### 唯一 recipe 语法

```text
tilemap <id> <tileset-id> <width> <height> <cell-size>
tilelayer <layer-id> <0|1-visible> <name>
property <key> <value>
row <local-id>...
endlayer
objectlayer <layer-id> <0|1-visible> <name>
property <key> <value>
point <object-id> <0|1-visible> <name> <x> <y>
rectangle <object-id> <0|1-visible> <name> <x> <y> <width> <height>
objectproperty <object-id> <key> <value>
endlayer
endtilemap
```

`row` 只能位于 `tilelayer`；`point/rectangle/objectproperty` 只能位于 `objectlayer`；所有 layer 和地图
必须显式闭合。旧的裸 `row` 单层格式会被拒绝。recipe 的 name/key/value 当前是不含空白的单 UTF-8
token；payload writer/parser 继续执行 strict UTF-8/NUL/长度校验。

### 完成结果

| 阶段 | 完成结果 |
| --- | --- |
| N1.1 | payload schema v2 writer/parser/view 已替换 v1 单层字段，并验证 ID、UTF-8、properties、几何、容量与尾随字节 |
| N1.2 | recipe 仅接受显式 layer block；package validation 在发布前验证 v2、required Tileset dependency 与 tile localId |
| N1.3 | runtime tile storage、chunk revision/dirty/cache/render/collision 全部携带显式 layer ID；错误区分 layer not found/type mismatch |
| N1.4 | sample 使用 visual=10、collision=20、gameplay objects=30；消费 point 101 与 rectangle 102；JSON 输出 `objectLayerConsumed`/`objectLayerObjects` |
| N1.5 | payload/Cooker/runtime/chunk/Physics tests 覆盖 round-trip、拒绝路径、hidden render 与 explicit collision layer |
| N1.6 | `game-2d/resources/physics/testing/public-api/design/roadmap/backlog` 已同步当前契约与后置边界 |

### 验收

```powershell
cmake --build --preset windows-vnext-bgfx-debug --target tina_tests tina_sample_2d -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_tests.exe --gtest_filter="TileMap*:CatalogCook*:TileChunk*:CharacterController*:TileMapPhysics*"
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
```

N1 明确不包含 chunk streaming、关卡/Scene/动画编辑器、undo/redo、旧 schema migration、自动从任意
object layer 生成完整 gameplay、2D 光照或导航。sample 对 object 101/102 的消费是产品垂直切片，不是
通用 editor/gameplay pipeline。

## N2 - 2D-PHYSICS-EXPAND

### 已完成契约

- 旧 `createBoxBody`/`PhysicsBoxShape2DDesc` 已删除；唯一创建模型是 `createBody()` 后按需调用
  `createShape()`，一个 body 可拥有多个 shape。
- `PhysicsShape2DDesc` 以 backend-neutral 字段支持 Box、Circle、Capsule；shape 可独立销毁、查询
  `shapeState()`，并拥有独立 generation `PhysicsShapeId`。
- sensor enter/exit 合并进有界 `PhysicsContactEvents2DView`，通过 `isSensor` 区分；事件携带 body/shape
  generation ID，不把 Box2D callback view 暴露到公共边界。
- `PhysicsJoint2DDesc` 当前支持 Distance joint，提供 `createJoint()`、`jointState()`、`destroyJoint()` 与
  generation `PhysicsJointId`；销毁任一关联 body 会自动退休 joint。
- body、shape、joint 分别执行 stale/wrong-world/capacity 校验；销毁 body 会自动退休其全部 shape 与 joint。
- TileMap solid bridge 只调用公开的 `createBody()`/`createShape()`，批量失败仍原子回滚；Box2D 头和类型
  保持在 `tina_physics2d` PRIVATE 实现。
- 产品 2D sample 创建 circle sensor 与远离主场景的 spring distance joint，结构化输出 sensor enter/exit
  与 joint ready，不干扰 crate/角色门禁。

### 完成结果

| 阶段 | 完成结果 |
| --- | --- |
| N2.1 | Body/Shape/Joint 拆成独立 generation handle 与固定容量 pool，旧 box-only API 零调用 |
| N2.2 | Box/Circle/Capsule、多 shape/body、shape query/destroy 与 body 级联 retirement 已覆盖 |
| N2.3 | sensor enter/exit 与 Distance joint 生命周期、capacity、stale/reuse 已覆盖 |
| N2.4 | Tile solid bridge、bench、产品 sample 全部迁移到新 API；Box2D 仍为 PRIVATE |
| N2.5 | `tina_physics2d_tests` 29/29 通过；产品 2D 300 帧输出 sensor enter=1、exit=1、joint ready=true |

### 验收

```powershell
cmake --build --preset windows-vnext-physics2d-debug `
  --target tina_physics2d_tests tina_physics2d_bench -- /m:2 /v:m
out\build\windows-msvc-vnext-physics2d\bin\Debug\tina_physics2d_tests.exe --gtest_color=yes
cmake --build --preset windows-vnext-bgfx-product-2d-debug --target tina_sample_2d -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
```

N2 明确不包含 polygon/chain shape、revolute/prismatic joint、Jolt、3D physics，或把
`CharacterController2D` 无依据地改成刚体。

## N3 - 2D-INPUT-ADV

### 已完成契约

- Runtime 是 Action mapping 的唯一 owner；`InputActionMapConfig::bindings` 以
  `InputActionBinding`/`InputBindingId` 一套模型表达 digital 与 Gamepad Axis，不保留 Platform mapper 或
  digital-only 配置双轨。
- Action snapshot 统一公开浮点 value 和 Started/ValueChanged/Completed/Cancelled transition。Axis 支持
  Signed/PositiveHalf/NegativeHalf/Trigger，随后应用 gameplay deadzone 外重映射与 scale；digital 同样
  使用 scale。
- 多 source/多 Gamepad generation 按 `SumClamped` 或 `StrongestMagnitude` 合成；等幅值使用稳定
  binding/source 顺序，设备重连后的新 generation 不继承旧 retained state。
- UI transition consume 与 continuous-control claim 先于 Gameplay mapping；digital 抑制到真实 release，
  axis 抑制到 neutral/deadzone，已有 Gameplay source 使用 Cancelled 清理而非伪造 Completed。
- 只有栈顶 State 可从 `FrameUpdateContext` 借用 `InputActionRebinding`。commit 以 Reject/Swap 处理冲突并
  queue 到下一 mapping frame 原子应用；Capturing/Queued transaction 均可取消，捕获的 Gamepad
  generation 在 disconnect 或 raw reset 后失效时以 DeviceDisconnected 结束。
- World pointer sample 在 mapping 时按 last-presented Camera2D/surface revision 固化，跨0-step frame
  不按后续 Camera/resize 重算。公共 API 不泄漏 GLFW。

### 完成结果

| 阶段 | 完成结果 |
| --- | --- |
| N3.1 | Runtime public Action map 合并为 unified `bindings`，digital/axis 共享 stable binding ID、domain、composition、deadzone/scale |
| N3.2 | ActionMapper 产出 value state/transition，支持 axis value mode、两种 composition 与多 Gamepad source |
| N3.3 | UI consume/claim suppression 覆盖 key/pointer/gamepad button/axis，并保持 release/neutral 与 cancel/reset 语义 |
| N3.4 | 顶层 `FrameUpdateContext` 提供 phase-local rebind facade；next-frame apply、Reject/Swap、Capturing/Queued cancel 与 generation 断连/reset 已定义 |
| N3.5 | world-pointer mapping-time latch 与 zero-step simulation latch 保持原契约；重复 Platform mapper 已删除 |

### 验收命令（本轮结果以最终验证记录为准）

```powershell
cmake --build --preset windows-vnext-bgfx-debug --target tina_tests tina_sample_2d -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_tests.exe --gtest_filter="ActionMapper*:Input*:UI*Input*"
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
```

不包含完整输入设置 UI、云同步 binding 或第三方 input SDK。

## N4 - 3D RENDER-001

### 已完成契约

代码审查选择 **有界 N-light**，不改 StaticMesh/Cooked schema：当前 normal map 已有 derivative TBN 的
真实消费路径，而 key/fill 两套 setter 是更直接的公共契约分叉。

- 删除 `setMesh3DDirectionalLight()` / `setMesh3DFillDirectionalLight()`；唯一入口是
  `setMesh3DLighting(const Mesh3DLightingDesc&)`。
- `Mesh3DLightingDesc` 同步消费 0..4 个 backend-neutral `Mesh3DDirectionalLight` 与非负 ambient；超容量、
  NaN/Inf、负 RGB/ambient、零方向均返回 `InvalidMesh3DLighting`。
- Null 与 bgfx 使用同一 validator 和相同上限；bgfx 把 direction/color 作为两个 4×vec4 uniform array
  提交，inactive slot 的 `w=0`。
- Opaque3D MR shader 固定上限遍历最多4个 directional light；仍是 Lambert + Blinn-Phong 近似 specular
  + ambient，不宣称能量守恒 PBR。
- `tina_sample_3d` 一次提交3个 lights，并输出 `lightingConfigured=true`、
  `directionalLightCount=3`；旧 key/fill JSON 字段已删除。
- Cooked StaticMesh 仍为 P3N3UV2；本切片未伪装成 tangent/PBR schema 升级。

### 完成结果

| 阶段 | 完成结果 |
| --- | --- |
| N4.1 | Render public API 收敛为单一 `Mesh3DLightingDesc`，固定上限4，无旧 setter 双轨 |
| N4.2 | Null/bgfx validator、状态与错误码一致；shader uniform array 在 D3D11/GLSL/SPIR-V 编译通过 |
| N4.3 | Null tests 覆盖3-light、ambient-only、超容量、零方向、负 RGB/ambient |
| N4.4 | 产品 sample 使用3-light 单次提交并把 count 纳入生命周期成功门禁 |

明确保留的边界：这不是完整 PBR/light system；IBL、shadow、light component/culling、vertex tangents、
skin/animation 与通用 pass scheduler 仍不在本切片内。

### 验收

```powershell
cmake --build --preset windows-vnext-bgfx-debug --target tina_tests tina_sample_3d -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_tests.exe --gtest_filter="*Opaque3D*:*Material*:*StaticMesh*"
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
```

## N5 - RENDER-FENCE

### 目标契约

- `FramePin` 完成与 asset retirement 收敛到一种产品完成语义；Null 可有简化实现，但契约字段和状态机一致。
- 真实 backend 优先以 GPU fence poll 或等价、可证明安全的 submission-completion 语义驱动完成。
- 删除无引用的 PresentSync/FrameDeferred/假完成分支，避免同一资源在不同 backend 走不同 lifetime 规则。

### 完成结果

- `FramePin`/`SubmissionTicket` 已统一为 present-return CPU submission completion：成功 present 后 complete，
  suspended skip 直接释放，失败/shutdown abandon。
- 删除 `SubmissionCompletionMode`、`BgfxSubmissionCompletionLedger`、deferred handoff、dynamic_cast 与
  `lastPresentFrameToken()`；`bgfx::frame()` 只推进 backend 帧，不再冒充 fence token。
- `submitFrame()` 的 borrowed view 继续要求同步消费；GPU Texture/Mesh 资源所有权由 bgfx backend 管理。
- `IRenderDevice::retireTexture2D/retireStaticMesh` 成功才消费 `FramePin`；generation 与 binding 立即失效，
  native handle 等 backend-proven completion 后销毁。Null 同步完成，统计统一暴露 pending/completed。
- bgfx 在所有资源引用 view 之后提交 1×1 blit + readback marker，只以 `readTexture()` 返回的 ready frame
  完成 retirement；frame wrap 使用半区间比较，suspend/drain 使用 `BGFX_FRAME_FLUSH` 推进。
- `AssetSystem` retirement helper 把 `AssetLease` 移入 pin：逻辑 lookup 立即移除，Store 保持
  `UnloadPending`，callback exactly-once 转 `Released/Unloaded`。析构会 drain，或识别 backend 已完成的回调。
- 无 blit/readback capability 时，带外部 pin 的异步 retirement 在资源状态变化前原子拒绝且不消费 pin；
  无 pin destroy 立即使逻辑 handle/binding 失效并交给 `bgfx::destroy` 的 backend-owned deferred
  destruction，不进入 marker timeline。有界 marker drain 未完成时，shutdown 才是仍存活外部 pin 的
  hard completion fallback。通用 GPU submission fence 仍不是本项能力，也未被伪装实现。

### 验收

```powershell
cmake --build --preset windows-vnext-bgfx-debug --target tina_tests tina_asset_tests tina_render_bgfx_tests tina_sample_2d_catalog tina_sample_2d tina_sample_3d -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_tests.exe --gtest_filter="*FramePin*:*Retire*:*Fence*"
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_asset_tests.exe --gtest_filter="*Retirement*"
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_bgfx_tests.exe --gtest_filter="*Retirement*"
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d_catalog.exe --frames=30 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
```

## 每项收口清单

1. 列出代码变更和已删除的旧路径。
2. 列出实际执行的测试与 sample 命令及结果。
3. 明确破坏性调用方式变更，不声明未实现的兼容。
4. 列出已回写的文档。
5. 写清本项未做边界，再将下一项标记为进行中。
