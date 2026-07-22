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
| TASK-001 | 落实或正式替代 ADR 0017 的交互 CPU worker 默认值 | Accepted 决策与 Desktop 当前行为冲突 |
| CLEAN-001～003 | 清除 vcpkg `legacy` feature、EASTL/compatibility 与错误文案 | 产品已退役，但整库零残留尚未成立 |
| TEST-001 | 复验当前 tip 的 Linux GCC/Clang/sanitizer 图 | 现有 Linux 结果早于当前 UI/Asset 产品切片 |
| TEST-002 | 把 product-2d 的完整 feature 测试拓扑固化 | 当前文档曾只运行 sample，遗漏模块测试 |
| 3D-001 | 从 multi-mesh Cooker 走到两个 mesh 的产品 E2E | G4 Cooker 已完成，G3 产品证据尚未完成 |
| DOC-001 | 完成本轮文档重组与一致性扫描 | 移除状态漂移，恢复单一任务来源 |

Now 的退出条件：没有未解释的 Accepted ADR/实现冲突；Windows product-2d 与 3D 产品门禁可复现；
Linux 当前 tip 有新证据；Legacy 残留有明确删除或保留决定。

## Next：可用性与资源寿命

| Backlog | 目标 |
| --- | --- |
| UI-002 | UIA/AT-SPI accessibility 首切片 |
| UI-003 | 跨 DPI/GPU 容差视觉门禁 |
| ASSET-001 | 安全外部 texture/buffer glTF 产品路径 |
| RUNTIME-001 | GameState stack/commands 与唯一提交点 |
| RUNTIME-002 | owning RenderFramePacket、FramePin 与 submission completion |
| PERF-001 | 决定 ADR 0018 并实现正式 `tina_bench` |
| DOC-002 | 文档链接/preset/target 自动检查 |

## Later：扩展能力

- PBR Material、lighting 与通用 pass scheduling；
- Jolt 3D physics adapter；
- 通用 Focus Scope、Modal、Pointer Capture；
- ScrollView、虚拟 ListView、Dropdown、TreeView；
- 多行 TextEdit、grapheme/shaping 与完整 IME 候选窗；
- Asset 热重载与增量 Cooker。

Later 项进入 Now 前必须先补清楚产品场景、容量边界、失败语义和验收命令，不能只按功能名称开工。

## Done：产品迁移主线

| 阶段 | 完成结果 |
| --- | --- |
| M0～M5 | C++23 构建基线、设计审计、ADR 与依赖方向建立 |
| M6 | Headless Runtime、`EngineHost`、`IGameApplication`/`IGameState` 生命周期 |
| M7 | Platform/Input、WindowSurface、Desktop/bgfx、Retained UI 核心与 UI DisplayList |
| M8 | generation Scene World、Transform 与 2D extraction |
| M9 | 3D extraction、bgfx Opaque3D/Sprite2D fixture |
| M10 | Catalog/Cooked、AssetSystem、Handle/Lease、Task、GPU upload、TileMap 与正式 2D sample |
| M11 | Physics2D、Audio/miniaudio、UI 设置/文本、StaticMesh/Material/Prefab/glTF 3D 产品路径 |
| M12 | Legacy `Tina.exe`、旧横版 2D、旧 UI 产品图与 `src/vnext` 前缀删除 |

“M12 Done”只表示产品删除完成，不表示 Linux、PBR、accessibility、benchmark 或整库 Legacy 字符串全部
完成。剩余工作已经拆入 Backlog，不再继续扩写 M12 历史清单。

## 产品门禁视图

| 门禁 | 当前结论 | 下一关闭点 |
| --- | --- | --- |
| 2D product | Windows bgfx 与 product-2d 300 帧已有证据 | TEST-002、TEST-001 |
| UI product | Text/Glyph、设置控件、TextEdit、ProgressBar、RadioButton 均有结构化与 Windows 产品视觉证据 | UI-002、UI-003 |
| 3D product | 单 product mesh 的 glTF/Prefab/Scene/Render smoke 已有证据 | 3D-001、ASSET-001、RENDER-001 |
| Asset/Cooker | multi-mesh、distinct AssetId 与 baseColorTexture→Texture2D cook 测试已完成 | 产品 multi-mesh、纹理 GPU 绑定与安全策略 |
| Audio | backend-neutral 与 miniaudio null-device 路径已有 Windows 证据 | Linux/product gate 复验 |
| Legacy retirement | 产品源码、target 与入口删除完成 | CLEAN-001～003 |
