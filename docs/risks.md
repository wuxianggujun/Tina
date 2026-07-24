# Tina 风险登记

风险描述“可能造成什么失败”，Backlog 描述“接下来做什么”。状态只使用 `Open`、`Mitigated`、
`Closed`；风险关闭后保留记录。

## Open

| ID | 等级 | 风险 / 触发信号 | 当前缓解 | 关闭条件 | Backlog |
| --- | --- | --- | --- | --- | --- |
| R-TASK-01 | closed | Desktop 交互默认已落实 `max(1, hw-1)` CPU worker；直接工厂 `cpuWorkerCount=0` 仍为 IO-only | BoundedTaskSystem 单测 + Desktop resolve | — | TASK-001 Done |
| R-LIFE-01 | P0 | RenderFrame 在途引用因 Asset unload、Atlas eviction 或 Surface close 失效 | Handle/Lease、UploadTicket、retirement ledger | owning packet/FramePin/completion 失败注入后全部归零 | RUNTIME-002 |
| R-SHUT-01 | P0 | barrier/deadline 后继续析构，Worker 或 callback UAF | 协作取消、join、逆序 shutdown | timeout 注入证明活跃 owner 不被释放，硬 deadline 明确 fast-fail | RUNTIME-002 |
| R-3D-01 | P0 | Cooker 单测被误当成 multi-mesh 产品 E2E | 文档区分 G4 Cooker 与 G3 product | 两个 mesh AssetId 分别 upload/bind/extract/draw，视觉与账本门禁通过 | 3D-001 |
| R-GLTF-01 | P0 | 外部 URI 路径穿越、symlink escape、size/count 资源炸弹 | Runtime 不直接打开 URI；Cooker 有结构/size 校验，relative-file 仍只按可信输入处理 | root containment、URI policy、overflow/oversize corpus 全通过 | ASSET-001 |
| R-LINUX-01 | closed | tip Docker GCC13 + Clang22（含 sanitizer）已复验 | [m12-evidence-linux.md](m12-evidence-linux.md) | 可选 Wayland/真显示器 | TEST-001 Done |
| R-UI-01 | P0 | dirty 传播或 paint batching 导致全树工作、每帧分配或透明顺序错误 | committed snapshot、固定 PMR、相邻合批、checksum | 目标规模门禁、零稳态分配和视觉/paint checksum 同时通过 | UI-003, PERF-001 |
| R-A11Y-01 | P1 | Semantics 与中立 probe 可读，但真实辅助技术仍不可读 | generation ID、`UIAccessibilityTree`、stale 拒绝 | UIA/AT-SPI 真机读取与销毁/切场景测试通过 | UI-002 |
| R-TEXT-01 | P1 | CJK 缺字、IME composition/selection 跨平台差异 | UTF-8 校验、可选 FreeType、Windows TextInput/IME 首切片 | Windows/Linux 支持矩阵与候选窗、shaping 测试明确 | TEXT-001 |
| R-VIS-01 | P1 | GPU、driver、DPI、字体变化造成截图误报 | 结构化逻辑测试优先；初始化白帧过滤 | 固定 profile、字体 fingerprint、区域/感知阈值噪声校准 | UI-003 |
| R-PERF-01 | P0 | benchmark 跨 build/host/workload 比较或 MAD 过高 | 仅保留模块 bench，不宣称正式回归协议 | ADR 0018 结论与 `tina_bench` fingerprint/baseline 门禁落地 | PERF-001 |
| R-LEGACY-01 | closed | vcpkg `legacy` feature、EASTL StringUtils 与 Clock/FrameTimer 兼容层已删除；`TINA_BUILD_LEGACY=ON` FATAL | 扫描 + FATAL guard | — | CLEAN-001～003 Done |

## Mitigated

| ID | 等级 | 风险 | 已有门禁 | 残余风险 |
| --- | --- | --- | --- | --- |
| R-DEP-01 | P0 | bgfx/GLFW/miniaudio/FreeType/cgltf/xxHash 类型泄漏公共 API | adapter PRIVATE link、header-isolation 与 token scan | 新模块/安装树需持续扫描 |
| R-INPUT-01 | P1 | UI 点击穿透 Gameplay 或 held input 卡住 | consumption + continuous claims 在 ActionMapper 前；reset/cancel 测试 | 通用 Modal/Capture/Gamepad claim 尚未实现 |
| R-FIXED-01 | P1 | 0/多 substep 丢失或重复输入边沿 | ordered transition + simulation latch + world pick tests | 完整 replay/State stack 仍需扩展 |
| R-ASSET-01 | P0 | Catalog cycle、损坏或半发布 | borrowed parser、owning snapshot、迭代 DAG、transaction rollback | 外部 glTF 与多文件 publish 原子性仍需加强 |
| R-AUDIO-01 | P0 | logical cancel 后 PCM/staging UAF 或 callback 违反实时约束 | command/completion 队列、Lease、null-device tests | 真设备 callback p99 与 shutdown race 证据不足 |
| R-BACKEND-01 | P1 | D3D11 Debug `RefCount` 提示掩盖 Tina 泄漏 | Tina resource ledger 与 Release 结果单独判断 | backend/driver 升级后需复验 |

## Closed

| ID | 风险 | 关闭证据 |
| --- | --- | --- |
| R-PRODUCT-01 | Legacy `Tina.exe`/旧 UI 产品图继续被构建或运行 | 产品源码/target 已删除，`TINA_BUILD_LEGACY=ON` FATAL |
| R-EASTL-01 | EASTL 继续作为产品 target 公共依赖 | vNext 产品 target 已使用标准库/PMR；仅余 CLEAN-002 扫尾，不再构成产品依赖 |
| R-ROOT-01 | `Application` 与 `EngineHost` 双组合根 | Legacy Application 已删除，产品只从 Desktop/EngineHost 进入 |

P0 风险进入受影响切片前必须有自动化门禁或 fail-safe。只记录日志后继续使用可能失效的内存，
不算风险缓解。
