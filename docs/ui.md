# 自研 UI

## 当前实现

现有 UI 是 Retained Tree，并非只有简单的 Panel/Label/Button。当前实际参与构建并被场景使用的能力包括：

- 基础控件：Panel、Label、Button、ProgressBar；
- 复合控件：Toolbar、CharacterPanel、Dialog、ListView、TextEdit；
- 布局：Measure/Layout、Percent、WrapContent、MatchParent、Margin、VBox、HBox、Grid；
- 文本：UTF-8、FreeType、中文字体、动态 Glyph Atlas、选择区和单行编辑；
- 渲染：颜色/图片/文字批处理、Layer、Scissor Clip；
- 输入：每帧统一选择最上层命中目标，Pointer Move/Down/Up/Click、Hover、Wheel、Focus 和 Capture Changed 均支持 Capture → Target → Bubble。

每个窗口由 EventSystem 持有一个 UIEventContext 和 generation slot registry。`NodeId(index, generation)` 是窗口内唯一交互句柄，hover、pressed、focused、captured 和 roots 均保存 NodeId；slot 复用前递增 generation，因此旧句柄不能解析到替代节点。节点还观察 EventSystem 的生命周期，即使节点晚于窗口上下文析构也不会解引用悬空指针。

当前 Scene 可注册多个顶层 UI 树，但每帧只在统一 UI Phase 中选择一个最上层目标。Scene 显式向整棵 UI 树注入 EventSystem 和 UILayoutManager，动态新增子节点继承相同上下文；节点从父树移除时立即注销 NodeId 并清除相关交互状态。

鼠标按下可点击节点时建立 Pointer Capture，Move/Up 即使发生在节点外仍发送给捕获节点，释放后产生 Capture Changed 并自动解除。点击只在按下节点与释放命中节点相同时成立。窗口焦点由 EventSystem 唯一持有，TextEdit 只响应 Focus Gained/Lost，Tab 与 Shift+Tab 按 UI 树顺序切换。

布局请求由每 Scene 的 UILayoutManager 批量处理，每帧最多提交一次。`UINode::update()`、`render()` 和 `containsPoint()` 均不再隐式触发布局。逻辑节点与 bgfx 渲染实现已拆文件，使布局和事件可以在无 GPU 的 GoogleTest 中验证。

Windows/MSVC 2026 当前已有自动化门禁覆盖：hit-test 不隐式布局、重叠节点只命中最上层、Capture/Target/Bubble 顺序、动态子节点继承上下文、stale NodeId、上下文先析构、节点移除/自移除安全失效、捕获外释放和正反向焦点遍历。

## 已知问题

- 当前 Input Snapshot 只暴露一个鼠标左键布尔状态，事件结构虽预留 pointerId，尚未接入多指针、多按钮和触摸；
- Tab/Shift+Tab 已可遍历焦点，但 Button 等控件尚未统一焦点视觉、Enter/Space 激活和手柄导航；
- `UIStyle/UITheme` 原型尚未接入活动控件，场景仍有重复硬编码颜色和尺寸；
- 当前是 VBox/HBox/Grid 布局，不是完整 Flex；dirty 上下传播仍可能扩大更新范围；
- TextEdit 支持 UTF-8 已提交字符，但没有 IME preedit/composition、复杂 shaping 和完整多行编辑；
- UI 绘制仍直接依赖 bgfx UIRenderer，尚未形成后端无关 Display List；
- 尚缺 DPI/content-scale、键盘/手柄统一导航、无障碍语义和稳定截图回归。

## 目标契约

每个活动 Scene/窗口只拥有一个 UIContext 和一个逻辑 Root；一次 Input Snapshot 只执行一次 hit-test，按 Capture → Target → Bubble 路由。Measure dirty 向上收敛，Layout/Transform dirty 只在父输出变化时向下传播；hit-test 和 render 不允许隐式触发布局。

UI 绘制输出 Quad、Text、Clip DisplayList，由 Renderer 批处理。中文文本统一走 UTF-8 与 FreeType Glyph Atlas。

## 推进顺序

1. generation NodeId、统一 Pointer/Focus/Capture 生命周期和对应 GoogleTest 已完成首轮；
2. 下一步统一 Style/Theme、DPI 缩放、Clip/Scroll 与 ListView 虚拟化；
3. 随后完善 TextEdit 的 IME/composition 和键盘/手柄导航；
4. 最后再增加 Checkbox、Slider、Dropdown、TreeView 等新控件。

因此 UI 需要继续完善，但当前优先级应是稳定基础契约，而不是先继续堆控件数量。
