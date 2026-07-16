# 自研 UI

## 当前实现

现有 UI 是 Retained Tree，并非只有简单的 Panel/Label/Button。当前实际参与构建并被场景使用的能力包括：

- 基础控件：Panel、Label、Button、ProgressBar；
- 复合控件：Toolbar、CharacterPanel、Dialog、虚拟化 ListView、ScrollView、TextEdit；
- 布局：Measure/Layout、Percent、WrapContent、MatchParent、Margin、VBox、HBox、Grid；
- 文本：UTF-8、FreeType、中文字体、动态 Glyph Atlas、选择区、单行编辑和 Windows 原生 IME preedit；
- 渲染：颜色/图片/文字批处理、Layer、可嵌套 Scissor Clip；
- 输入：每帧统一选择最上层命中目标，Pointer Move/Down/Up/Click、Hover、Wheel、Focus、Capture Changed 和焦点 KeyDown 均支持 Capture → Target → Bubble。

每个窗口由 EventSystem 持有一个 `UIContext`、一个输入路由上下文和 generation slot registry。`UIContext` 独立拥有 Theme、逻辑窗口尺寸、framebuffer 尺寸、content scale、用户缩放和 revision；多窗口之间不共享可变主题状态。`NodeId(index, generation)` 是窗口内唯一交互句柄，hover、pressed、focused、captured 和 roots 均保存 NodeId；slot 复用前递增 generation，因此旧句柄不能解析到替代节点。节点还观察 EventSystem 的生命周期，即使节点晚于窗口上下文析构也不会解引用悬空指针。

当前 Scene 可注册多个顶层 UI 树，但每帧只在统一 UI Phase 中选择一个最上层目标。Scene 显式向整棵 UI 树注入 EventSystem 和 UILayoutManager，动态新增子节点继承相同上下文；节点从父树移除时立即注销 NodeId 并清除相关交互状态。

鼠标按下可点击节点时建立 Pointer Capture，Move/Up 即使发生在节点外仍发送给捕获节点，释放后产生 Capture Changed 并自动解除。点击只在按下节点与释放命中节点相同时成立。窗口焦点由 EventSystem 唯一持有，Tab 与 Shift+Tab 按 UI 树顺序切换；普通 KeyDown 只路由到当前 generation NodeId 对应的焦点节点。订阅者可在 Capture/Target/Bubble 任一阶段 `preventDefault()`，传播控制状态可从只读事件回调安全修改。Button 默认可聚焦，Enter/Space 非重复按键只激活一次并显示主题焦点框；TextEdit 的编辑键不再通过全局按键订阅接收。

Theme 首轮已接入 Panel、Label、Button 和 TextEdit，提供每窗口 Dark/Light/Custom 值对象；控件没有显式设色或字号时解析窗口 Theme，显式设置仍可安全覆盖并可恢复主题值。DPI 以 GLFW logical size 与 framebuffer size 的比值为唯一来源，逻辑鼠标坐标只转换一次后进入 framebuffer-space hit-test。菜单、世界选择、设置和暂停界面的最终缩放统一为“逻辑分辨率响应式 × content scale × 用户缩放”，TextEdit 的字体、padding、拖选坐标也使用同一窗口度量。

`UINode::setClipChildren(true)` 同时约束渲染和 hit-test，嵌套空裁剪不会意外恢复为“无裁剪”。`UIScrollView` 提供垂直/水平/双轴、目标偏移、边界钳制、DPI 滚轮步长和帧率无关平滑；滚轮命中子按钮时会向上寻找最近可滚动祖先。ListView 只遍历可见行与 overscan，十万行数据不会产生十万次绘制，并已接入窗口 Theme。

Windows 文本输入保持两条独立通道：GLFW character callback 只提交最终 `TextInputEvent`；Win32 IMM32 通过窗口 subclass 产生 `TextCompositionEvent`，携带 Started/Updated/Ended/Cancelled、UTF-8 preedit 和 codepoint 光标。TextEdit 在正文光标处显示 preedit、下划线和组合光标，持续更新候选框位置，composition 期间不会让 Backspace/方向键同时修改正文。Linux 保持已提交字符路径，平台桥接安全退化为空实现。

布局请求由每 Scene 的 UILayoutManager 批量处理，每帧最多提交一次。`UINode::update()`、`render()` 和 `containsPoint()` 均不再隐式触发布局。逻辑节点与 bgfx 渲染实现已拆文件，使布局和事件可以在无 GPU 的 GoogleTest 中验证。

Windows/MSVC 2026 当前已有自动化门禁覆盖：hit-test 不隐式布局、重叠节点只命中最上层、Capture/Target/Bubble 顺序、动态子节点继承上下文、stale NodeId、上下文先析构、节点移除/自移除安全失效、捕获外释放、正反向焦点遍历、焦点按键路由/默认取消/重复键抑制/路由中删除目标、每窗口 Theme/DPI 隔离、200% DPI 逻辑坐标命中、裁剪命中边界、ScrollView 钳制/祖先滚轮路由、十万行虚拟范围，以及 composition 与 committed text 的事件隔离。

## 已知问题

- 当前 Input Snapshot 只暴露一个鼠标左键布尔状态，事件结构虽预留 pointerId，尚未接入多指针、多按钮和触摸；
- Tab/Shift+Tab、Button 焦点视觉和 Enter/Space 激活已完成首轮；仍缺 KeyUp 视觉状态、方向键空间导航和手柄映射；
- Theme 已接入基础控件，但场景中仍有显式品牌色和尺寸；后续需要 token 化 spacing/radius/border，并补主题切换示例和截图回归；
- 当前是 VBox/HBox/Grid 布局，不是完整 Flex；dirty 上下传播仍可能扩大更新范围；
- ScrollView 与 ListView 已有纵向基础能力，但尚缺拖动滚动条、惯性/触摸手势、嵌套滚动消费和可复用 item template；
- TextEdit 已支持 Windows IME preedit/composition，但 Linux 原生 preedit、复杂 shaping、字形簇、IME attribute span 和完整多行编辑仍未完成；
- UI 绘制仍直接依赖 bgfx UIRenderer，尚未形成后端无关 Display List；
- 尚缺运行时 content-scale 回调、键盘/手柄统一导航、无障碍语义和稳定截图回归。

## 目标契约

每个活动 Scene/窗口只拥有一个 UIContext 和一个逻辑 Root；一次 Input Snapshot 只执行一次 hit-test，按 Capture → Target → Bubble 路由。Measure dirty 向上收敛，Layout/Transform dirty 只在父输出变化时向下传播；hit-test 和 render 不允许隐式触发布局。

UI 绘制输出 Quad、Text、Clip DisplayList，由 Renderer 批处理。中文文本统一走 UTF-8 与 FreeType Glyph Atlas。

## 推进顺序

1. generation NodeId、统一 Pointer/Focus/Capture 生命周期和对应 GoogleTest 已完成首轮；
2. 每窗口 Style/Theme、DPI/content scale 和高 DPI 输入坐标已完成首轮；
3. 通用 Clip/ScrollView、ListView 虚拟化与 Windows IME composition 已完成首轮；
4. 焦点 KeyDown 路由、Button 键盘激活和焦点视觉已完成首轮；下一步补 KeyUp/方向键/手柄导航，再增加 Checkbox、Slider、Dropdown、TreeView 等控件；
5. 随后把 UI 绘制收敛为后端无关 Display List，并补稳定截图回归。

因此 UI 需要继续完善，但当前优先级应是稳定基础契约，而不是先继续堆控件数量。
