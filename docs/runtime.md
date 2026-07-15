# Runtime 与 Frame Pipeline

## 当前实现

Application 已集成 GLFW Window/Input、EventSystem、资源管理器、miniaudio、SceneManager 和 bgfx。Core 提供 Clock 与 FrameTimer。

当前每帧的实际主线程顺序为：Poll Platform/Input → Event Queue → Asset Completion → App Event → Fixed Simulation → Variable/Scene Update → UI Node Update → UI Batch Layout → UI Hit-test/Routed Event → Scene Render → App Render → Input End Frame → Deferred Task → Present。Event Queue、Asset Completion 和 UI Pointer Routing 都只有一个明确泵送点；资源 completion 默认每帧最多提交8个。

Fixed Simulation 默认 60 Hz、每个 Render Frame 最多追赶4步；超出保护阈值的积压会被丢弃，Render 可通过 `interpolationAlpha()` 读取 accumulator 插值比例。GameScene 的 ECS、碰撞、水模拟、粒子与昼夜推进已进入 fixed phase，相机和 UI 保持可变帧率。

Application 初始化现在保留明确的成功状态；任一必需子系统失败会按逆序回滚，`run()` 会拒绝半初始化实例。`--smoke-frames=N` 可在提交 N 帧后沿正常生命周期退出，用于验证析构和资源回收。

## 已知问题

- Scene 操作和资源上传/销毁还没有完全归入独立 Frame Phase；
- 当前 Application 仍承担过多系统所有权，尚未收敛到 EngineHost/EngineContext 接口。

## 目标契约

帧阶段固定为：Poll Platform → Input Snapshot → Event Queue → Asset Completion → Fixed Update → Variable Update → Render Extraction → UI → Render → Present → Deferred Cleanup。

Simulation 默认固定 60 Hz，每帧最多追赶4步；Render 使用可变帧率和 interpolation alpha。所有队列必须有预算、统计和唯一所有者。
