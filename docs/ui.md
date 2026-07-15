# 自研 UI

## 当前实现

现有 UI 是 Retained Tree，包含 UINode、Panel、Button、Toolbar、Dialog、List、TextEdit、Measure/Layout 和自研事件路由。

## 已知问题

- UIContext 只有一个全局 root，而 GameScene 一帧内可能切换多个 root 并重复处理输入；
- 动态 addChild 后布局管理器继承与注册不完整；
- requestLayout 的上下传播会重复扩大 dirty 范围；
- hit-test、节点事件和全局 EventBus 存在重复分发风险。

## 目标契约

每个活动 Scene/窗口只拥有一个 UIContext 和一个逻辑 Root；一次 Input Snapshot 只执行一次 hit-test，按 Capture → Target → Bubble 路由。Measure dirty 向上收敛，Layout/Transform dirty 只在父输出变化时向下传播；hit-test 和 render 不允许隐式触发布局。

UI 绘制输出 Quad、Text、Clip DisplayList，由 Renderer 批处理。中文文本统一走 UTF-8 与 FreeType Glyph Atlas。
