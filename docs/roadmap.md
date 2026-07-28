# Tina Roadmap

Roadmap 只表达优先级窗口，不保存逐提交流水。可执行任务、依赖和验收条件统一维护在
[Backlog](backlog.md)；历史切片可从 Git 与 ADR 追溯。

## 状态规则

- `Now`：当前应优先关闭，通常包含契约冲突、产品门禁或迁移残留；
- `Next`：Now 关闭后进入，不与 P0 工作抢占验证资源；
- `Later`：方向成立但未承诺排期；
- `Done`：已经满足当时验收条件，后续扩展不改变其历史完成状态。

任务状态与证据强度分开：实现完成、测试通过、产品 smoke 和视觉验证是不同结论。

## Now：契约一致与产品收口

| Backlog | 目标 | 为什么现在做 |
| --- | --- | --- |

当前无 Now 项。`ASSET-HANDLE-SCENE / N16.4` 已完成并移入 Done；下一项从 Backlog 的 P1 状态重新选择。

Now 的退出条件：当前 P1 条目均满足各自验收条件；受影响的 Windows product-2d/3d 门禁可复现；
没有未解释的 Accepted ADR/实现冲突。

## Next：可用性与资源寿命

| Backlog | 目标 |
| --- | --- |
| UI-002 | 产品 HWND 自动接线 / Narrator 金标 / AT-SPI（映射 + HostBridge 已落地） |
| UI-003 | 跨 DPI/GPU 容差视觉门禁 |



## Later：扩展能力

- PBR Material、lighting 与通用 pass scheduling；
- Jolt 3D physics adapter；
- 通用 Focus Scope、Modal、Pointer Capture；
- ScrollView、虚拟 ListView、Dropdown、TreeView；
- 多行 TextEdit、grapheme/shaping 与完整 IME 候选窗；
- Asset 热重载与增量 Cooker；
- TileMap/Scene/动画 editor tooling、undo/redo 与 cook preview；

Later 项进入 Now 前必须先补清楚产品场景、容量边界、失败语义和验收命令，不能只按功能名称开工。

## Done：已关闭工作

| 阶段/任务 | 完成结果 |
| --- | --- |
| M0～M5 | C++23 构建基线、设计审计、ADR 与依赖方向建立 |
| M6 | Headless Runtime、`EngineHost`、`IGameApplication`/`IGameState` 生命周期 |
| M7 | Platform/Input、WindowSurface、Desktop/bgfx、Retained UI 核心与 UI DisplayList |
| M8 | generation Scene World、Transform 与 2D extraction |
| M9 | 3D extraction、bgfx Opaque3D/Sprite2D fixture |
| M10 | Catalog/Cooked、AssetSystem、Handle/Lease、Task、GPU upload、TileMap 与正式 2D sample |
| M11 | Physics2D、Audio/miniaudio、UI 设置/文本、StaticMesh/Material/Prefab/glTF 3D 产品路径 |
| M12 | Legacy `Tina.exe`、旧横版 2D、旧 UI 产品图与 `src/vnext` 前缀删除 |
| DOC-001 | README/架构/设计/构建/测试/任务职责重组完成，Backlog 成为未完成工作的唯一明细，一致性扫描通过 |
| 2D-TILEMAP-LAYERS / N1 | TileMap schema v2 有序 tile/object layers、非零唯一稳定 ID、visibility/UTF-8 properties、point/rectangle；recipe 单一显式 block 语法；runtime render/chunk/collision 显式 layer ID；sample 消费 layer 30 的 object 101/102；发布前验证 Tileset dependency/localId |
| 2D-PHYSICS-EXPAND / N2 | Body/Shape/Joint 独立 generation；Box/Circle/Capsule 与多 shape/body；sensor enter/exit；Distance joint；body 级联 retirement；TileMap bridge 与产品 sample 迁移；29/29 模块测试及 300 帧 sensor/joint 证据 |
| RENDER-001-NLIGHT / N4 | Opaque3D lighting 收敛为唯一 `Mesh3DLightingDesc`，有界0..4 directional lights；Null/bgfx/shader/sample/test 同步；产品一次提交3灯 |
| 2D-INPUT-ADV / N3 | Runtime 单一 unified binding 覆盖 digital/analog value、deadzone/scale、两种合成、多手柄、UI suppression 与 next-frame transactional rebind；本轮测试执行结果以最终验证记录为准 |
| 2D-TILEMAP-STREAM | TileMap v3 stream root + 独立 deferred TileMapChunk；固定容量 Camera/layer demand、request budget、cancel/unload、lease 与 transactional capacity；resident generation 贯通 dirty cache；产品 sample 每帧推进 visual/collision residency |
| 2D-TILEMAP-LRU | retain window 作为 optional cache；按成功 demand update 时位于 load window 的 recency 自动淘汰，读取不 touch；desired 强需求超 capacity 仍 transactional |
| 2D-AUDIO-ADV / N7 | voice gain/pitch/pan/fade、transient one-shot retirement；Create 固定预分配的 bounded PCM stream、owner-thread atomic submit、EOF/underrun/cancel、terminal backpressure/debt、generation 与 shutdown realtime gate；产品 sample 以 owner-thread deterministic mix 验证 stream，miniaudio callback 由 adapter tests 验证 |
| 2D-FX / N8 | Scene standalone `ParticleSystem2D` / `Trail2D`：Create 唯一持久 PMR 分配、固定容量与单调不复用 stable key；固定 seed 粒子、事务式 burst/update、trail anchor/break/独立 lifetime/width age interpolation；product-2d schema 9 结构化与像素证据 |
| RUNTIME-SHUTDOWN-DEADLINE / N9 | `ITaskSystem::shutdownAndJoinFor` 有界等待、invalid 无状态变化、timeout ownership retention/retry；`EngineHost` 使用配置 deadline，失败先写 Diagnostics 再 terminate，不继续析构 owner |
| ASSET-HANDLE-SCENE-2D-A1 / N10 | `SpriteRenderer2D` 保存 weak `AssetHandle`，extract 借用 allocation-free resolver；invalid/stale/cross-store/wrong-kind/unbound fail closed，hidden 不解析；窄 AssetTypes target 避免 Scene 传递完整 Asset |
| ASSET-HANDLE-SCENE-2D-A2 / N11 | N11 当时契约：fixed-capacity owner-thread Sprite registry 借用 Store/device；device-instance allocator 事务分配唯一、单调不复用 key，同 device 多 registry 安全；唯一 required Texture2D cooked dependency fail-closed resolve；当时产品 State unbind 后 destroy，schema 13 保留 binding/release/texture destroy/resolver evidence；该分裂 owner 契约已由 N16.3 替代 |
| ASSET-HANDLE-SCENE-2D-A3 / N12 | standalone Particle/Trail 从 `u32` key 迁移为 weak Sprite `AssetHandle`；显式 extract 借用共享 resolver，空/失效资源 fail closed；空 FX 跳过解析，Trail 每次非空 extract 解析一次、Particle 按 live item 解析；schema 11 独立记录两类 hits，FX fingerprint schema 2 使用稳定 AssetId |
| ASSET-HANDLE-SCENE-2D-A4 / N13 | TileMap emit 从持久 `u32` key 迁移为 weak Tileset `AssetHandle` + borrowed Asset resolver；Tileset 唯一 required Texture2D dependency fail closed；hidden/off-camera/empty 不解析，非空可见集合只解析一次，失败清空；schema 12 独立记录 TileMap hits |
| ASSET-HANDLE-SCENE-3D-A5 / N14 | `MeshRenderer3D` 保存 weak StaticMesh/Material Handle；Prefab 只做 AssetId→Handle；visible extraction 分别借用 kind-specific resolver，资源失效 fail closed，hidden 不解析；3D product evidence schema 1 记录 handle 发布、两类 resolver hits 与 AssetStore active |
| ASSET-HANDLE-SCENE-3D-A6-BINDINGS / N15 | fixed-capacity owner-thread Mesh3D registry 借用 Store/device/PMR；mesh/material 独立 device allocator 事务分配、不复用；Material texture/factors 原子发布并按 dependency fail closed；exact stale unbind 与产品逆序释放安全；schema 2 记录注册/释放/销毁与 registry 释放 |
| ASSET-HANDLE-SCENE-N16.1-CORE | packet-local `FrameResourceRef`/固定容量资源表、同帧去重与 owning pin；Runtime 在 extraction 前开启 packet，并在 complete/skip/abandon 时 exactly-once 释放；Texture2D retirement 事务覆盖 backend reject 与重试 |
| ASSET-HANDLE-SCENE-N16.2-SPRITE | World、TileMap、selection、Particle 与 Trail 的 Sprite2D extraction 全部只写 packet-local texture ref；registry 用 frame borrow pin 阻止活跃帧 unbind；Null/bgfx 在提交副作用前验证 ref owner/generation/kind/range |
| ASSET-HANDLE-SCENE-N16.3-SPRITE-OWNER | Sprite registry Entry 唯一拥有 resident Lease/GPU/binding；register 成功才消费 GPU，active borrow 与 retirement failure 保留 Entry；product-2d schema 14 证明2份 owner handoff、weak handle 失效与 retirement ledger 全部 Released |
| ASSET-HANDLE-SCENE-N16.4-MESH-OWNER | Mesh/Material Render item 迁移为 packet-local geometry/material ref；Mesh3D registry 唯一拥有 Mesh Lease/GPU/binding、Material Lease/binding 与按 AssetId 去重的共享 Texture Lease/GPU；active frame/material reference count 阻止过早 retirement；product-3d schema 4 证明2 Mesh、2 Material、3 Texture handoff 与 ledger Released |
| ASSET-HANDLE-SCENE | A1-A6 与 N16.1-N16.4 全部完成；Scene/Prefab/FX/TileMap 只持 weak Handle，Render item 只持 packet-local ref，Sprite2D/Mesh3D resident ownership 与 retirement 均收敛到 registry + AssetSystem |
| UI-SHOWCASE / PRODUCT-2D / PRODUCT-3D | 独立13控件 showcase 与 2D/3D 产品样例统一使用继承式产品 Theme；Button pressed/focus/elevation 层级、Dark/Light 事务换肤、实际 Slider/Checkbox 状态联动、FreeType 暗/亮 client capture 与结构化生命周期证据成立 |
| TEST-003 | bgfx + FreeType 图同轮运行 Core/Scene/Asset/Render/UI 模块测试和 300 帧 product-3d；schema 4 同时验证 glTF/PBR/3-light、Mesh/Material/共享 Texture owner retirement、packet-local resolver 与 Dark→Light→Dark retained UI |

“M12 Done”只表示产品删除完成，不表示 Linux、PBR、accessibility、benchmark 或整库 Legacy 字符串全部
完成。剩余工作已经拆入 Backlog，不再继续扩写 M12 历史清单。

## 产品门禁视图

| 门禁 | 当前结论 | 下一关闭点 |
| --- | --- | --- |
| 2D product | Windows product-2d 同轮模块测试 + 300 帧已有证据（TEST-002）；TileMap v3 sample 每帧 demand/pump/commit visual=10 与 hidden collision=20，gameplay objects=30 留在 root；retain-window LRU、Physics sensor/joint、Advanced input、World/Particle/Trail weak Sprite Handle、TileMap weak Tileset Handle 已完成；全部 Sprite2D item 使用 packet-local `FrameResourceRef`，fixed-capacity registry 唯一拥有 resident Lease/GPU/binding；schema 14 记录 Dark→Light→Dark、2份 owner/retirement handoff、weak handle 失效、ledger Released 与四类 resolver hits，FX fingerprint schema 2 使用稳定 AssetId | TileMap priority IO/editor/自动 gameplay 生成、完整 FX asset/editor/GPU simulation 均为独立后续项 |
| Linux tip | Docker GCC13 + Clang22（含 sanitizer）已复验（TEST-001） | 可选 Wayland |
| UI product | 13 控件独立 showcase、Dark/Light 实时换肤、Button hover/pressed/focus/disabled 层次，以及 product-2d HUD 和 product-3d Scene Controls 均有结构化与 Windows FreeType 暗/亮视觉证据 | OS 级 DPI 与跨 GPU 金标 |
| 3D product | 双 mesh + Resources-owned AssetSystem + Prefab/Scene weak mesh/material Handle + engine-provided、State-owned Mesh3D registry + packet-local geometry/material resolver、Mesh/Material/共享 Texture 统一 owner、原子 material bundle、baseColor/MR/normal 贴图采样、material factors、有界0..4 directional lights 已有证据（产品提交3灯）；schema 4 进一步证明2 Mesh/2 Material/3 Texture handoff、weak handle 失效、ledger Released、成熟 retained controls 与 Dark→Light→Dark | RENDER-001 的完整 PBR/IBL/shadow/light component/pass scheduling |
| Runtime stack/packet | stack/commands/policy、FramePin present-return CPU completion、独立 Texture/Mesh AssetLease readback retirement，以及 Task timeout/retry + Host-enforced TaskSystem worker-exit/join deadline 已落地 | 产品 sample 暂停演示；通用 GPU submission fence 非当前 Runtime 契约 |
| Asset/Cooker | multi-mesh 产品 E2E、baseColor/MR/normal Texture2D cook、外部 URI 安全；TileMap v3 root/TileMapChunk v1 + eager Tileset/deferred chunk dependency/localId 发布前验证及 retain-window LRU 已完成 | 更完整资源炸弹矩阵、TileMap priority IO/editor、热重载与增量 Cooker |
| Audio | `2D-AUDIO-ADV / N7` 已完成；Windows product-2d 以 owner-thread deterministic mix 验证 bounded stream，miniaudio callback/mixer 与 lifecycle 由 adapter tests 验证 | Linux、真实设备质量/延迟/切换与 callback benchmark |
| Legacy retirement | 产品源码/target 删除完成；vcpkg legacy feature 与 EASTL/compatibility 扫尾完成 | 仅保留 `TINA_BUILD_LEGACY=ON` FATAL 拒绝开关 |
