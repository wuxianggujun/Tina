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

| TEST-001 | Planned | P0 | 复验 Linux vNext 当前 tip | 可用 GCC 13、Clang 22 + libstdc++15 | GCC Null/GLFW 及 Clang sanitizer 直接测试通过；记录工具链、返回码、sanitizer 与 X11/Wayland 条件 | Platform |



## Next

| ID | 状态 | 优先级 | 工作 | 依赖 | 验收条件 | 证据 |
| --- | --- | --- | --- | --- | --- | --- |

| UI-002 | Planned | P1 | Windows UIA / Linux AT-SPI 真机 adapter | UI-002-SPI | 真实辅助技术可读 Button/Checkbox/Slider/ProgressBar/RadioButton/TextEdit；生命周期无 stale | Integration + Platform |
| UI-003 | Planned | P1 | 建立跨 DPI/GPU 容差视觉门禁 | 稳定门禁机 | 100/150/200% DPI、字体 fingerprint 与区域阈值可复现；初始化白帧不计入 stable capture | Visual + Platform |






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
| 3D-001 | multi-mesh 产品 E2E：双 mesh glTF fixture → cook → 两 StaticMesh upload/bind（meshKey 1/2）→ Prefab 每节点 resolve → extract/draw → ledger 归零；`tina_sample_3d` 300 帧 `multiMesh=true` | [3D](game-3d.md) |
| TASK-001 | Desktop `resolveDesktopTaskSystemParams`：交互默认 `max(1, hw-1)` CPU worker；`createBoundedTaskSystem(cpu=0)` IO-only 仍 NotSupported；BoundedTaskSystem 单测覆盖 | [Task](task-system.md) · ADR 0017 |
| CLEAN-001 | 删除 vcpkg `legacy` feature 及 EnTT/GLM/spdlog/utfcpp 死依赖声明；preset 无引用 | [dependencies](dependencies.md) |
| CLEAN-002 | 删除无消费者 `StringUtils.hpp`（EASTL/utfcpp）与 Clock/FrameTimer/FixedStepTicker compatibility；`SteadyMonotonicClock` 实现迁到 `MonotonicClock.cpp` | [core](core.md) |
| CLEAN-003 | miniaudio 实现 TU 与 CMake FATAL 文案不再暗示 Legacy ON 可运行 | [dependencies](dependencies.md) |
| TEST-002 | product-2d 同轮：UI/RuntimeUI/bridge/FreeType/Physics2D/Audio/miniaudio/Asset 测试 + sample 300 帧；`productGate=bgfx-physics-freetype-audio`；脚本 `tools/windows/RunProduct2dGate.ps1` | [building](building.md) · [Windows 证据](m12-evidence-windows.md) |
| ASSET-001 | glTF 外部 URI root containment/`..`/scheme 拒绝 + 64MiB 上限；`tina_sample_3d` 上传/绑定 Cooked Texture2D 到 materialKey；路径逃逸单测 | [3D](game-3d.md) · GltfCookTests |
| UI-001 | ProgressBar/RadioButton 已接入 product-2d；190/190 UI、77/77 Runtime UI、12/12 Render bridge 通过，结构化输出与 Windows client-area 视觉证据成立 | [UI](ui.md) · [Windows 证据](m12-evidence-windows.md) |
| DOC-001 | 文档职责与任务体系重组完成；本地链接、configure/build preset、CMake target、Markdown fence 与格式扫描通过；UI 绘制链路和控件矩阵已归档 | [文档索引](README.md) · [Roadmap](roadmap.md) · [UI](ui.md) |
| UI-THEME-AB | 薄 `UITheme` token；`UIBoxPaint` 亮/暗边 + 可选 shadow；sample_2d 设置面板 elevation；hex `rgb`/`argb`；`UIThemeTests` | [UI](ui.md) |
| DOC-002 | `tools/docs/CheckDocs.ps1`：docs 本地链接、cmake configure/build preset、`--target` 名、Legacy 产品文案软警告；不扫 out/build/thirdparty | [building](building.md) · [testing](testing.md) |
| RUNTIME-001 | `GameStateStack` + commands + 唯一 commit；**policy 向下阻断**（fixed/frame/render/UI 自顶向下 `forEachDispatch`）；enter 失败丢 candidate；`GameStateStackTests` / policy dispatch 单测 | [gameplay](gameplay.md) · ADR 0014 |
| RUNTIME-002 | `FramePin`/`FramePinSink`、`RenderFramePacket`、`NullSubmissionCompletionLedger`；EngineHost submit/present 挂 pin 并在 present/skip 后 complete；shutdown abandon；`FramePinPacketTests` | [rendering](rendering.md) · ADR 0016 |
| PERF-001 | ADR 0018 Accepted；`tools/bench` → `tina_bench` schema v1；workload `null_runtime_frames`；JSON fingerprint/checksum/p50/p95/p99；共享机 `conclusion=provisional`；固定 hard-gate 机与多进程 MAD 后置 | [performance-memory](performance-memory.md) · ADR 0018 |
| RUNTIME-001-INT | Null Host 集成：base `requestPush` overlay（block fixed/frame below）→ overlay `requestPop` → base 恢复；enter 失败无 `onExit`；`GameStateStackIntegrationTests` | [gameplay](gameplay.md) · ADR 0014 |
| RUNTIME-001-SAMPLE | `tina_sample_2d` 收尾自动 pause overlay（≥60 帧）：push/pop + policy block；JSON `pauseOverlay*`；短 smoke 跳过 | [2D](game-2d.md) · ADR 0014 |
| UI-002-SPI | `UIAccessibilityTree`/`IUIAccessibilityProvider`/`UIAccessibilityProbeProvider`：从 committedSemantics 发布 role/name/state/range；stale node 拒绝；`UIAccessibilityTests` | [UI](ui.md) |
