# Runtime 与 Frame Pipeline

## 当前实现

Application 已集成 GLFW Window/Input、EventSystem、资源管理器、miniaudio、SceneManager 和 bgfx。Core 提供 Clock 与 FrameTimer。

当前每帧的实际主线程顺序为：Poll Platform/Input → Event Queue → Asset Completion → App Event → Variable Update → Scene Update → Scene Render → App Render → Input End Frame → Deferred Task → Present。Event Queue 和 Asset Completion 都只有一个明确泵送点；资源 completion 默认每帧最多提交8个。

## 已知问题

- 当前只有可变步长 update，Simulation 与 Render 没有明确分离；
- 初始化函数无法完整表达失败，部分访问器可能在半初始化后解引用空对象；
- Scene 操作和资源上传/销毁还没有完全归入独立 Frame Phase；
- 当前 Application 仍承担过多系统所有权，尚未收敛到 EngineHost/EngineContext 接口。

## 目标契约

帧阶段固定为：Poll Platform → Input Snapshot → Event Queue → Asset Completion → Fixed Update → Variable Update → Render Extraction → UI → Render → Present → Deferred Cleanup。

Simulation 默认固定 60 Hz，每帧最多追赶4步；Render 使用可变帧率和 interpolation alpha。所有队列必须有预算、统计和唯一所有者。
