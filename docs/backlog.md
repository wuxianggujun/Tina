# Tina 可执行 Backlog

本文件是未完成工作的唯一明细表。Roadmap 只表达优先级窗口，ADR 只表达决策，不在多个主题文档
重复维护任务状态。

## 状态与证据

任务状态只使用：`Planned`、`InProgress`、`Blocked`、`Done`、`Deferred`。

证据强度与任务状态分开记录：

- `Unit`：模块单元测试；
- `Integration`：跨模块测试；
- `Smoke`：sample/CLI 生命周期与结构化输出；
- `Visual`：截图、像素或人工视觉；
- `Platform`：指定 OS/toolchain/backend 门禁。

`Done` 必须满足验收条件并留下对应证据；“已经写代码”不自动等于产品门禁完成。

## Now

| ID | 状态 | 优先级 | 工作 | 依赖 | 验收条件 | 证据 |
| --- | --- | --- | --- | --- | --- | --- |
| TASK-001 | Planned | P0 | 解决 ADR 0017 与 Desktop CPU worker 默认值冲突 | ADR 0017 | 交互 Desktop 默认 `max(1, hardware_threads-1)`，或新 ADR 明确 supersede；0-worker IO-only 图继续可配置；关闭/TaskGroup 测试通过 | Unit + Integration |
| CLEAN-001 | Planned | P1 | 删除未消费的 vcpkg `legacy` feature | 当前 presets/依赖闭包 | 所有 preset 不引用该 feature；移除 EnTT/GLM/spdlog/utfcpp 等死依赖声明；Windows 基础与产品 configure 通过 | Platform |
| CLEAN-002 | Planned | P1 | 清理 EASTL 与 Legacy compatibility 残留 | CLEAN-001 | 证明 `src/core/utils/StringUtils.hpp` 无消费者后删除/重写；决定并清理 Clock/FrameTimer/FixedStepTicker compatibility；源码和 CMake 无 EASTL token | Unit + forbidden scan |
| CLEAN-003 | Planned | P2 | 清理 miniaudio Legacy 条件说明与余下错误文案 | CLEAN-001 | `MiniaudioImplementation.cpp`、CMake FATAL 和文档不再暗示当前 `src/ui` 已退役或 Legacy ON 可运行 | Scan |
| TEST-001 | Planned | P0 | 复验 Linux vNext 当前 tip | 可用 GCC 13、Clang 22 + libstdc++15 | GCC Null/GLFW 及 Clang sanitizer 直接测试通过；记录工具链、返回码、sanitizer 与 X11/Wayland 条件 | Platform |
| TEST-002 | Planned | P0 | 固化 product-2d 完整门禁 | product-2d preset | 构建并直接运行 Physics2D、UI、UI FreeType、Audio、miniaudio 测试与 300 帧 sample；标签严格为 `bgfx-physics-freetype-audio` | Integration + Smoke |
| 3D-001 | Planned | P0 | 完成 multi-mesh 产品 E2E | 已完成 multi-mesh Cooker/AssetId resolver | 产品 fixture 含至少两个 glTF mesh/Prefab node；两个 AssetId 分别 upload/bind/extract/draw；账本归零 | Unit + Integration + Smoke + Visual |
| DOC-001 | InProgress | P1 | 建立文档职责与一致性检查 | 本次文档重组 | README/架构/设计/构建/测试/任务/M12 无已知状态冲突；本地链接和命令 target 扫描通过 | Scan |

## Next

| ID | 状态 | 优先级 | 工作 | 依赖 | 验收条件 | 证据 |
| --- | --- | --- | --- | --- | --- | --- |
| UI-002 | Planned | P1 | 建立 Windows UIA / Linux AT-SPI adapter 首切片 | Semantics snapshot | Button/Checkbox/Slider/ProgressBar/RadioButton/TextEdit role、name、state 可由真实辅助技术读取，生命周期无 stale node | Integration + Platform |
| UI-003 | Planned | P1 | 建立跨 DPI/GPU 容差视觉门禁 | 稳定门禁机 | 100/150/200% DPI、字体 fingerprint 与区域阈值可复现；初始化白帧不计入 stable capture | Visual + Platform |
| ASSET-001 | Planned | P1 | glTF 外部 buffer/baseColorTexture 安全策略与产品绑定 | 3D-001 | root containment、URI/type/size 上限、路径穿越与资源炸弹用例通过；已 Cooked 的 Texture2D 进入 Render 产品路径 | Unit + Integration |
| RUNTIME-001 | Planned | P1 | GameState stack/commands | 当前单 State 生命周期 | push/pop/replace 仅在唯一 commit 点生效；onEnter/onExit 顺序、UI root、Task barrier 与失败回滚有测试 | Unit + Integration |
| RUNTIME-002 | Planned | P0 | owning RenderFramePacket、FramePin 与 submission completion | ADR 0016 | 在途 Asset/UI atlas/Surface 不因 logical unload 失效；完成后所有 pin/ticket/ledger 归零 | Unit + failure injection |
| PERF-001 | Planned | P1 | 完成 ADR 0018 决策并实现 `tina_bench` | 固定 workload/fingerprint | 接受或拒绝 ADR 0018；JSON schema、checksum、p50/p95/p99、baseline compatibility 与固定 worker 生效 | Benchmark + Platform |
| DOC-002 | Planned | P2 | 自动检查文档本地链接、preset 与 target 名 | DOC-001 | CI/脚本在链接、未知 preset/target、禁止 Legacy 文案回归时失败，不扫描生成目录 | Automated scan |

## Later

| ID | 状态 | 优先级 | 工作 | 验收条件 |
| --- | --- | --- | --- | --- |
| RENDER-001 | Deferred | P2 | PBR Material、lighting 与 pass scheduling | 产品 3D 使用 Cooked texture/material，排序与资源退役有门禁 |
| PHYSICS-001 | Deferred | P2 | Jolt 3D adapter | 独立 Tina::Physics3D API、Jolt PRIVATE、生命周期/查询/性能门禁 |
| UI-004 | Deferred | P2 | 通用 Focus Scope、Modal、持久 Pointer Capture | 多 root/state transition 与输入恢复测试通过 |
| UI-005 | Deferred | P2 | ScrollView、虚拟 ListView、Dropdown、TreeView | 100k item 虚拟化与零稳态分配门禁通过 |
| TEXT-001 | Deferred | P2 | 多行 TextEdit、grapheme/shaping、候选窗定位 | 中英混排、组合输入、selection 与平台 IME 矩阵通过 |
| ASSET-002 | Deferred | P2 | 热重载与增量 Cooker | 不破坏 AssetId/Lease/retirement 契约，失败不发布半包 |

## Done

| ID | 完成项 | 证据入口 |
| --- | --- | --- |
| DONE-001 | Legacy `Tina.exe`、旧横版 2D 与旧 UI 产品图删除 | [M12 退役说明](m12-legacy-ui-retirement.md) |
| DONE-002 | `EngineHost`、Platform/Input、WindowSurface、Desktop/bgfx 垂直切片 | [架构](architecture.md) · [测试](testing.md) |
| DONE-003 | Scene 2D/3D extraction 与 Catalog/Cooked/Handle/Lease/Task/Upload 首轮 | [Scene](scene-ecs.md) · [资源](resources.md) |
| DONE-004 | 2D product sample 与 glTF/Prefab 3D product sample | [2D](game-2d.md) · [3D](game-3d.md) |
| DONE-005 | Retained UI 文本/Glyph、Checkbox/Slider/TextEdit、ProgressBar/RadioButton 库级实现 | [UI](ui.md) |
| DONE-006 | glTF multi-mesh Cooker 与 distinct AssetId/Prefab dependency 测试 | [3D](game-3d.md) |
| UI-001 | ProgressBar/RadioButton 已接入 product-2d；190/190 UI、77/77 Runtime UI、12/12 Render bridge 通过，结构化输出与 Windows client-area 视觉证据成立 | [UI](ui.md) · [Windows 证据](m12-evidence-windows.md) |
