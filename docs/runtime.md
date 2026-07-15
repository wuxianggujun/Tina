# Runtime 与 Frame Pipeline

## 当前实现

Application 已集成 GLFW Window/Input、EventSystem、资源管理器、miniaudio、SceneManager 和 bgfx。Core 提供 Clock 与 FrameTimer。

## 已知问题

- 排队事件和异步资源回调缺少稳定、唯一的主循环泵送点；
- 当前只有可变步长 update，Simulation 与 Render 没有明确分离；
- 初始化函数无法完整表达失败，部分访问器可能在半初始化后解引用空对象；
- deferred task、Scene 操作和资源回收没有统一的 Frame Phase。

## 目标契约

帧阶段固定为：Poll Platform → Input Snapshot → Event Queue → Asset Completion → Fixed Update → Variable Update → Render Extraction → UI → Render → Present → Deferred Cleanup。

Simulation 默认固定 60 Hz，每帧最多追赶4步；Render 使用可变帧率和 interpolation alpha。所有队列必须有预算、统计和唯一所有者。
