# Tina 风险登记

风险描述“可能造成什么失败”，Backlog 描述“接下来做什么”。状态只使用 `Open`、`Mitigated`、
`Closed`；风险关闭后保留记录。

## Open

| ID | 等级 | 风险 / 触发信号 | 当前缓解 | 关闭条件 | Backlog |
| --- | --- | --- | --- | --- | --- |
| R-GLTF-01 | P0 | 外部 glTF URI 经 symlink/junction/reparse、替换竞态或 size/count/dimension 资源炸弹逃逸 authoring root/预算 | 已拒绝 scheme/绝对路径/`..`，使用 canonical containment，并有 64MiB 单文件上限；Runtime 不读取源 URI | Windows/Linux 专项 escape fixture 与 buffer/accessor/image dimension/count/overflow corpus 全部通过，失败不发布半包 | ASSET-SEC-001 |
| R-UI-01 | P0 | dirty 传播或 paint batching 导致全树工作、每帧分配或透明顺序错误 | committed snapshot、固定 PMR、相邻合批、checksum；50,000 节点深树 structure/layout/hit/paint 非递归回归，Popup publication 为线性步骤 | 完整 dirty-range pruning、目标规模零稳态分配和视觉/paint checksum 同时通过 | UI-003, PERF-002 |
| R-A11Y-01 | P1 | Windows UIA 自动 HWND 接线/patterns 已有，但 Narrator/Inspect 金标与 AT-SPI 未关 | generation ID、action seam、HostBridge、EngineHost 自动 publish、Invoke/Toggle/RangeValue/Value patterns、跨进程 HWND client gate | Narrator/Inspect 人工金标 + Linux AT-SPI 真机验收 | UI-002 / UI-002-LINUX |
| R-TEXT-01 | P1 | CJK 缺字、IME composition/selection 跨平台差异 | UTF-8 校验、可选 FreeType、Windows TextInput/IME 首切片 | Windows/Linux 支持矩阵与候选窗、shaping 测试明确 | TEXT-001 |
| R-VIS-01 | P1 | GPU、driver、DPI、字体变化造成截图误报 | 结构化逻辑测试优先；初始化白帧过滤 | 固定 profile、字体 fingerprint、区域/感知阈值噪声校准 | UI-003 |
| R-PERF-01 | P0 | benchmark 跨 build/host/workload 比较或 MAD 过高 | ADR 0018 + `tina_bench` schema v1/fingerprint；共享机结论只标 provisional | 固定机 hard gate 与多进程 MAD 协议落地 | PERF-002 |

## Mitigated

| ID | 等级 | 风险 | 已有门禁 | 残余风险 |
| --- | --- | --- | --- | --- |
| R-DEP-01 | P0 | bgfx/GLFW/miniaudio/FreeType/cgltf/xxHash 类型泄漏公共 API | adapter PRIVATE link、header-isolation 与 token scan | 新模块/安装树需持续扫描 |
| R-INPUT-01 | P1 | UI 点击穿透 Gameplay 或 held input 卡住 | consumption + continuous claims 在 ActionMapper 前；Modal barrier、持久 Capture、空间方向 focus release latch、reset/cancel 与 Gamepad default-action 测试 | 更广的多 Pointer/设备矩阵仍后置 |
| R-FIXED-01 | P1 | 0/多 substep 丢失或重复输入边沿 | ordered transition + simulation latch + world pick、State stack policy tests | 完整 replay 与多 World orchestration 仍需扩展 |
| R-ASSET-01 | P0 | Catalog cycle、损坏或半发布 | borrowed parser、owning snapshot、迭代 DAG、transaction rollback、外部 URI containment/size policy | hot reload、增量 Cooker 与多文件增量 publish 原子性仍后置 |
| R-AUDIO-01 | P0 | logical cancel 后 PCM/staging UAF 或 callback 违反实时约束 | command/completion 队列、Lease、null-device tests | 真设备 callback p99 与 shutdown race 证据不足 |
| R-BACKEND-01 | P1 | D3D11 Debug `RefCount` 提示掩盖 Tina 泄漏 | Tina resource ledger 与 Release 结果单独判断 | backend/driver 升级后需复验 |

## Closed

| ID | 风险 | 关闭证据 |
| --- | --- | --- |
| R-TASK-01 | Desktop 默认 CPU domain 被 0 worker 静默禁用 | Desktop 已解析为 `max(1, hw-1)`；直接工厂 `cpuWorkerCount=0` 仍明确表示 IO-only，TASK-001 Done |
| R-LIFE-01 | RenderFrame 在途引用因 Asset unload、Atlas eviction 或 Surface close 失效 | owning `RenderFramePacket`/FramePin/CPU completion 已落地；Texture/Mesh 使用独立 readback marker 与 AssetLease-backed retirement，失败/skip/shutdown 有明确 drain/abandon |
| R-3D-01 | Cooker 单测被误当成 multi-mesh 产品 E2E | 两个 mesh AssetId 已分别完成 cook、upload/bind、Prefab resolve、extract/draw 与账本归零的 product-3d 门禁 |
| R-LINUX-01 | 当前 tip 缺少 Linux toolchain/sanitizer 证据 | TEST-001 已完成 Docker GCC13 Null/Platform 与 Clang22 Null/ASan/UBSan/LSan 复验；Wayland/真显示器是独立扩展 |
| R-LEGACY-01 | Legacy 依赖或兼容层继续进入产品图 | vcpkg `legacy` feature、EASTL StringUtils、Clock/FrameTimer compatibility 已删除；`TINA_BUILD_LEGACY=ON` 保持 FATAL |
| R-SHUT-01 | barrier/deadline 后继续析构，Worker 或 callback UAF | `shutdownAndJoinFor` timeout 保留 stopping TaskSystem/Worker/owner 并可 retry；Host 配置 deadline + Diagnostics/terminate death tests 证明不继续析构 owner；不 detach/强杀 |
| R-PRODUCT-01 | Legacy `Tina.exe`/旧 UI 产品图继续被构建或运行 | 产品源码/target 已删除，`TINA_BUILD_LEGACY=ON` FATAL |
| R-EASTL-01 | EASTL 继续作为产品 target 公共依赖 | vNext 产品 target 使用标准库/PMR；无消费者 StringUtils 与相关 compatibility 已删除，CLEAN-002 Done |
| R-ROOT-01 | `Application` 与 `EngineHost` 双组合根 | Legacy Application 已删除，产品只从 Desktop/EngineHost 进入 |

P0 风险进入受影响切片前必须有自动化门禁或 fail-safe。只记录日志后继续使用可能失效的内存，
不算风险缓解。
