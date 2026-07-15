# 自研 UI

## 当前实现

现有 UI 是 Retained Tree，包含 UINode、Panel、Button、Toolbar、Dialog、List、TextEdit、Measure/Layout 和自研事件路由。

每个窗口由 EventSystem 持有一个 UIEventContext。当前 Scene 可注册多个顶层 UI 树，但每帧只在统一 UI Phase 中选择一个最上层目标，随后执行 Capture → Target → Bubble；场景代码不再各自重复提交 Pointer 输入。Scene 切换会先停止 UI 观察并注销整棵布局树，再释放节点所有权。

布局请求由每 Scene 的 UILayoutManager 批量处理，每帧最多提交一次。`UINode::update()`、`render()` 和 hit-test 不再隐式触发布局；运行时新增子节点会继承并注册到同一个布局管理器。

## 已知问题

- requestLayout 的上下传播会重复扩大 dirty 范围；
- hit-test、节点事件和全局 EventBus 存在重复分发风险。

## 目标契约

每个活动 Scene/窗口只拥有一个 UIContext 和一个逻辑 Root；一次 Input Snapshot 只执行一次 hit-test，按 Capture → Target → Bubble 路由。Measure dirty 向上收敛，Layout/Transform dirty 只在父输出变化时向下传播；hit-test 和 render 不允许隐式触发布局。

UI 绘制输出 Quad、Text、Clip DisplayList，由 Renderer 批处理。中文文本统一走 UTF-8 与 FreeType Glyph Atlas。
