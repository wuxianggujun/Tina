# ADR 0060：物理输入扇出与显式重绑定

- 状态：Accepted
- 日期：2026-09-12
- 接受依据：用户要求不保留一对一旧设计的完整重构。
- 部分替代：[ADR 0015](0015-input-and-fixed-step.md) 的物理控件唯一绑定、primary-only pointer binding 和单冲突重绑定；帧序、Action domain、fixed-step latch 与 reset 契约不变。

## 背景

Android 游戏需要同一手柄按钮同时驱动原始 Pad Action 与玩法 Action。旧配置禁止重复物理控件，Mapper 又只查找第一条数字绑定，二者使合理配置在创建渲染器之前失败。另一个独立缺口是 Platform/Mapper 已支持 8 个 pointer 槽位，EngineConfig 仍只接受槽位 0，导致双摇杆游戏在启动校验阶段退出。

只删除重复检查不成立：UI claim、取消、模拟轴、重绑定和有界事件流都依赖原有的一对一假设。

## 决定

1. 一条 `InputActionBinding` 是一条物理 pattern 到 Action 的边。Binding ID 唯一；同一个物理控件可以显式连接多个 Action，包括不同 domain。一个 Action 的 domain/composition 必须一致，同一 Action 不允许重复的相同 pattern。轴的 value mode 属于每条边的变换，不是另一个物理控件。
2. `InputActionMapConfig::validate()` 是 EngineConfig、Mapper 创建和合并设置的唯一配置规则入口。Pointer 采用 `Platform::PointerCapacity` 边界，错误附带绑定索引、ID、Action 和控件参数。
3. Mapper 使用物理控件到绑定链、Action 到贡献源链两个索引。索引和事务 scratch 在创建时分配；映射与重绑定应用不扩容，不在每个事件上扫描整个绑定表。
4. 一个物理事件先完成所有 source 更新的预检和暂存，再按 Action 合成一次、发布 transition。世界拾取等失败恢复该事件的全部 source，不得先发布部分 Frame Action。排序为 raw sequence，再按配置中首次匹配 Action 的顺序；组合等幅值仍按绑定/source 的稳定顺序。
5. UI 先 route/consume/claim，再由 Mapper 把消费或 claim 作用于全部匹配 source。释放、失焦、断连、reset 与重绑定清理不能遗留兄弟绑定。Frame/Simulation 流独立；任一域溢出后，该域仅发布显式 reset 并压制 held source，不保留半份扇出结果。
   `InputCancelTransition::pointer` 只取消该槽的全部绑定，不能清掉另一根手指、键盘或手柄的 source。
   取消批次先一次性移除全部受影响 Action 的未消费事件，再从各自已交付基线重建；不得因逐边释放/分配顺序误触发容量 reset。批量压缩复用预分配 scratch，不逐 Action 反复扫描整个事件流。
6. 删除 `RebindConflictPolicy` 和单个 `conflictingBinding`。新 `RebindOptions` 明确 Reject/Share/Swap；结果返回全部冲突 Binding ID，Swap 必须指定要交换的绑定，不任挑第一个。共享不允许制造重复的 Action/pattern 边。修改仍在下一 mapping frame 原子应用，未改动的共享边不取消；相同 pattern 的无操作提交不打断 held 状态。
7. 设置格式升级为 v2：pointer pattern 保存 pointer ID 与 button；持久化仅按显式 Binding ID 匹配，校验 Action/domain，不再回退到“第一个相同 Action”。旧 schema 明确失败，不做兼容解析。全部 producer、consumer 和 fixture 同步迁移。
8. 按 ADR 0024 将 SDK API/ABI epoch 升为 **0.2.0**，所有当前消费方使用对应版本。旧 0.1.0 头文件或静态库不能混用。

## 边界与代价

- 本次不引入多 Context/priority/chord 编辑器。多用途的启用条件由 State/UI 语义决定；一对多不是把 UI 已消费的输入无条件广播给玩法。
- 相同 Action 被多个业务模块读取仍只需一个 binding；多个物理来源合成同一 Action 的既有能力保留。
- 扇出会增加事件数量；容量仍由产品明确配置。事务 scratch 占用随绑定规模线性增长。
- SDK 消费者必须重新编译并同时更新头文件与静态库，不保留一对一 compatibility flag、旧重绑定 overload 或游戏侧 Pad→Key 复制分支。

## 验证

直接运行无窗口 GoogleTest：配置/所有 pointer 槽、数字与轴的一对多、UI 消费与 claim、release/cancel/reset、Gamepad generation、跨域失败原子性、容量溢出、显式多冲突重绑定及设置往返。Android SDK 和游戏配置使用同一新代码重编，真实 GPU/触屏体验与这些无窗口测试分开报告。

### 2026-09-12 Windows Release 实际记录

- 复用 `out/build/windows-msvc-vnext-bgfx-product-2d`，增量构建 `tina_tests`、`tina_runtime_ui_tests`、`tina_ui_tests`、`tina_editor_tests`、`tina_editor_app_tests`、`tina_editor_desktop`，退出码均为 0；未启动 Editor 或游戏。
- 配置/InputActionMapper/SimulationActionLatch/GameSettings 专项 90 项、Runtime/UI 151 项、UI 850 项、已有 Editor authoring 146 项、已有 Editor app 25 项，直接执行对应 GoogleTest executable，均通过。
- 非主 pointer、跨域共享绑定、一键扇出三项回归在重构前失败；取消批次容量顺序问题另有一次失败复现，修复后通过。
- 扩展执行完整 `tina_tests`：722 项中 721 项通过。`CrashHandlerTest.BacktraceIsResolvedWhereSupportedAndExplicitWhereNot` 失败：该 Release 输出没有 `tina_tests.pdb`，崩溃报告的引擎帧为裸地址，只有依赖库符号可解析。未放宽断言，不把此结果计为全量通过。
- strict SDK version probe 通过：仅接受 0.2.0，拒绝旧版、相邻版本、四段版本及范围请求。
- 原始日志与 GoogleTest JSON 保留在 `out/input-fanout-validation/`，不作为源码提交；源码/文档为 UTF-8，MSVC 使用 `/utf-8`。没有运行 CTest、模拟器或真机 APK。
