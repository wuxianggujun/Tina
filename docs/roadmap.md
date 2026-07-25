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




Now 的退出条件：当前 P1 条目均满足各自验收条件；受影响的 Windows product-2d/3d 门禁可复现；
没有未解释的 Accepted ADR/实现冲突。

## Next：可用性与资源寿命

| Backlog | 目标 |
| --- | --- |
| UI-002 | 产品 HWND 自动接线 / Narrator 金标 / AT-SPI（映射 + HostBridge 已落地） |
| UI-003 | 跨 DPI/GPU 容差视觉门禁 |
| 2D-TILEMAP-STREAM | 基于已完成 schema v2/layer ID 契约做有界 chunk 加载、取消和卸载 |



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

“M12 Done”只表示产品删除完成，不表示 Linux、PBR、accessibility、benchmark 或整库 Legacy 字符串全部
完成。剩余工作已经拆入 Backlog，不再继续扩写 M12 历史清单。

## 产品门禁视图

| 门禁 | 当前结论 | 下一关闭点 |
| --- | --- | --- |
| 2D product | Windows product-2d 同轮模块测试 + 300 帧已有证据（TEST-002）；TileMap v2 sample 使用 visual=10、hidden collision=20、gameplay objects=30；Physics 输出 sensor enter/exit 与 Distance joint ready；Advanced input 实现已完成，当前轮测试结果单独记录 | TileMap streaming/editor 与 UI-003 多 DPI 矩阵均为独立后续项 |
| Linux tip | Docker GCC13 + Clang22（含 sanitizer）已复验（TEST-001） | 可选 Wayland |
| UI product | Text/Glyph、设置控件、TextEdit、ProgressBar、RadioButton 均有结构化与 Windows 产品视觉证据 | UI-002、UI-003 |
| 3D product | 双 mesh + baseColor/MR/normal 贴图采样、material factors、有界0..4 directional lights 已有证据（产品提交3灯） | RENDER-001 的完整 PBR/IBL/shadow/light component/pass scheduling |
| Runtime stack/packet | stack/commands/policy 与 FramePin present-return CPU completion 已落地 | 产品 sample 暂停演示；Asset→GPU fence 异步 retirement |
| Asset/Cooker | multi-mesh 产品 E2E、baseColor/MR/normal Texture2D cook、外部 URI 安全；TileMap v2 + required Tileset dependency/localId 发布前验证已完成 | 更完整资源炸弹矩阵、TileMap streaming/editor、热重载与增量 Cooker |
| Audio | backend-neutral 与 miniaudio null-device 路径已有 Windows 证据 | Linux/product gate 复验 |
| Legacy retirement | 产品源码/target 删除完成；vcpkg legacy feature 与 EASTL/compatibility 扫尾完成 | 仅保留 `TINA_BUILD_LEGACY=ON` FATAL 拒绝开关 |
