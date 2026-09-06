# Navigation / AI 续接记录（2026-09-06）

## 范围与基线

- 续接会话 `01a07168-9337-7340-b6e6-2d518699ecc3`；开工 HEAD `941d690f`，工作树干净。
- 另一位 AI 已停止修改 Navigation2D，由本会话接手；构建继续 `--parallel 1 -- /nr:false`。
- 交付目录是 `D:\ProgramData\Tina`，它是现有 **SDK** 前缀，不是 Editor 产品目录；不清空目录、不覆盖无关文件。
- 上个会话已授权相关测试。本轮只做直接相关验证；没有同场景 FPS 基线，不声称帧率提升。

## 已写入的实现

`NAV-GAMEPLAY-002` 的代码覆盖原缺口清单前四项：

1. Grid/Data 的 `worldToCell` / `cellCenter`，double 范围计算与半开边界。
2. `NavigationPathSmoother2D` 与 cell/world LOS，严格墙角、连续地形成本感知、原子发布。
3. `NavigationPathFollower2D` 与 `NavigationAgent2D`，实际位置输入、限速、不跨拐点、精确世界目标与动态重规划。
4. `NavigationFlowField2D`，单目标共享 reverse Dijkstra、分帧预算、确定性成本/下一跳与 revision 拒绝。

A* 与 Flow field 共用私有邻接规则和 indexed heap，没有新增第三方类型、线程或全局 owner。
未改变 Cooked wire layout，不需要 schema bump。源码注释/诊断使用 ASCII，中文文档使用 UTF-8；MSVC `/utf-8` 保持启用。

## 已修复的验证问题

首次增量构建中 `tina_navigation2d` 成功，随后既有 `SourceImportProbe.cpp:226` 报 C3861。
根因是 `snapshotContainmentPath` 位于 `GltfDetail`，调用遗漏限定名。已对照声明和 GltfCook/MediaCook 调用点，
仅补 `GltfDetail::`，不改变路径归一化/包含判定逻辑。第二次 `tina_navigation2d_tests` 增量构建 exit 0。

分配失败注入还发现 MSVC Debug 的 PMR vector allocator-only 构造/移动会分配 iterator proxy，且入口为
`noexcept`（调试器堆栈：`memory_resource::allocate -> vector:675 -> NavigationPathSmoother2D::Create`）。
已把 Pathfinder/Smoother/FlowField/Follower/Agent 的工作区改为 **PMR 分配的稳定 Storage owner**，vector
原位使用可抛出的 sized constructor，owner 移动只转移 unique ownership。逐次失败注入证明每一个工厂分配点
都返回 `AllocationFailed` 并释放已创建存储；sealed PMR 下移动与稳态操作均通过。

## 验证状态

- Debug：`tina_navigation2d_tests` 增量构建 exit 0，直接 GoogleTest **39/39**，包括四个新公开头的 header-isolation TU。
- Release：`windows-vnext-sdk-release` 的 `tina_sdk_install_artifacts` build exit 0；`cmake --install ... --component sdk --prefix D:/ProgramData/Tina` exit 0。复用既有 SDK tree，补完上一轮未完成的 shaderc/SPIR-V/Tint 与完整 SDK 依赖，没有另开产品构建树。
- 安装检查：328 个 Tina SDK 公开头通过第三方 token 检查；Navigation2D 的 7 个安装头与源码 SHA-256 一致；安装 lib 与本轮 Release lib 的 SHA-256 一致。
- 外部消费者：[CMake](../tests/sdk_consumer_navigation2d/CMakeLists.txt) / [完整代码](../tests/sdk_consumer_navigation2d/main.cpp)。只链接安装版 `Tina::Navigation2D`，configure/build/run 均 exit 0；不读取源码树的 Tina include，也不启动窗口。
- 未运行 Editor、UI 视觉、GPU FPS 或跨平台门禁；导航测试不能替代这些结果。

```json
{"status":"ok","consumer":"installed-tina-navigation2d","pathCells":15,"smoothedWaypoints":5,"agentFrames":169,"flowCost":140}
```

复验命令（此处测试已获得用户授权）：

```powershell
cmake --build --preset windows-vnext-debug --target tina_navigation2d_tests --parallel 1 -- /nr:false
out\build\windows-msvc-vnext\bin\Debug\tina_navigation2d_tests.exe --gtest_color=no
cmake --build --preset windows-vnext-sdk-release --target tina_sdk_install_artifacts --parallel 1 -- /nr:false
cmake --install out/build/windows-msvc-vnext-sdk --config Release --prefix D:/ProgramData/Tina --component sdk
```

安装产物：`D:\ProgramData\Tina\lib\tina_navigation2d.lib`，1,985,220 bytes，修改时间
`2026-09-05T23:02:54.7753340Z`（北京时间 2026-09-06 07:02:54）。SHA-256：
`DED63FECAF1EA9FCEB53AC5026EE68FC403F6427B7ECBAB1677495BB809AAD12`。
`D:\ProgramData\Tina\bin\tina_assetc.exe` 同轮更新，1,273,344 bytes；本次 SDK 导出为 Release，旧的 SDK-owned Debug export 已由 CMake 安装规则替换。

## 尚未实现的两项大能力

- **行为树 / 黑板 / AI FSM**：独立决策层，不塞进 Navigation2D，也不复用 Runtime 程序 State stack。先冻结 typed blackboard、节点/状态生命周期、回调失败/重入、预算和 owner 契约。
- **3D 导航**：上一会话建议 Recast/Detour 私有内核。仍需落实第三方版本/包闭包、Tina-owned mesh/query API、agent radius/height/slope/climb、nearest-point/partial-path 语义与安装 consumer；不能把 2D grid 改名当成 3D navmesh。

建议下一批的具体落点（候选设计，尚无这些 target/API，需先确认 ADR）：

| 能力 | 落点与数据流 | 首个验收闭环 |
| --- | --- | --- |
| typed Blackboard | `include/tina/ai/Blackboard.hpp`、`src/ai`；独立 `Tina::AI -> Core + Math`，固定容量、typed key/value、无 `any`/Scene 全局访问；按实例隔离 | 类型不匹配、容量满和非法值不破坏旧值；稳定 PMR 存储/失败注入/安装 consumer |
| BehaviorTree | 同模块 `BehaviorTree.hpp/.cpp`；验证过的不可变节点拓扑 + 每 agent 执行状态；Sequence/Selector/Condition/Action/Inverter，预算耗尽保留 Running | Running 恢复、取消回调 exactly once、拒绝环/共享子节点、重入/异常/节点预算；由 callback 消费现有 NavigationAgent2D |
| AI FSM | 同模块 `StateMachine.hpp/.cpp`；确定顺序的 guard/transition，显式 enter/update/exit；不复用程序 `IGameState` 栈 | 每 tick 至多一次转换；退出/进入失败明确 Faulted，不伪装为副作用回滚；实例状态互不共享 |
| Navigation3D | `include/tina/navigation3d` / `src/navigation3d` 的 Detour runtime；Recast bake 留在 Asset/Cooker；独立 feature 与版本化 cooked payload | Y-up 米制、nearest-point 的最大吸附范围、Reached/Partial/Unreachable/容量不足分开；走廊/坡度/台阶/角色尺寸 fixture、私有依赖与安装闭包 |

同类静态发现：原 `NavigationGrid2DData/Grid` 仍直接移动 PMR vector，未纳入本轮 Storage 迁移。
后续应先给 Grid/Data 加逐次分配失败注入和 sealed-resource move 回归，再迁移存储；不能把本轮五个工作区
工厂的 OOM 证据泛化为整个 Navigation2D 模块的所有工厂。`GenerationPool` 本身已采用稳定 slot block，不必重写。

其他限制：点角色导航不等于 radius clearance/crowd；平滑同步且候选检查最坏二次；产品必须安排容量/频率。
实际位置及移动接受情况以玩法/Physics 为准，不能用 Agent 内部积分绕过碰撞。

## 收尾资源审计

以下为本轮资源，不宣称清空其它会话/用户的资源：

| 字段 | 核验结果 |
| --- | --- |
| buildTree：常驻 Null | `out/build/windows-msvc-vnext`：897,346,319 → 973,895,506 bytes，保留为核心增量测试树 |
| buildTree：常驻 SDK | `out/build/windows-msvc-vnext-sdk`：3,789,106,835 → 4,763,958,581 bytes，保留 Release 库、shaderc 与 SDK 增量构建结果 |
| buildTree：临时 consumer | `out/build/navigation2d-sdk-consumer-20260906-01a072b8`：创建前 0；验证后 890,844 bytes / 61 files；清理请求被执行策略拒绝，**仍保留**，未声称已释放 |
| process | CIM 核验 cmake/MSBuild/cl/link/shaderc/cdb/Navigation 测试与 consumer 进程，结果为空；本轮后台构建/安装 job 均已退出 |
| container / volume / image | 本轮未创建、未操作 |
| cache | 无专用临时 cache；既有 vcpkg 与系统 debugger 共享缓存未删除，未核验其字节数，不声称释放 |
| agent | 已查询，仅主代理 `/root`；子代理 0 |

临时 consumer EXE SHA-256：`4BC1BC966A0D50005AE6EDF7EF9008F69E4AD2ACF11E1F31B5AE2B442F48F926`。
其保留目录完整路径为 `C:\Users\wuxianggujun\CodeSpace\CMakeProjects\Tina\out\build\navigation2d-sdk-consumer-20260906-01a072b8`；
已核验为仓库 `out/build` 内的普通目录、非 reparse point。源码交付与 D 盘安装已完成，剩余收尾是该目录的回收。
当前修改尚未提交/推送；不要把起始检查点 `941d690f` 当作包含这些新能力的提交。
