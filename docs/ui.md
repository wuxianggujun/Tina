# 自研 UI

## 当前实现

现有 UI 是 Retained Tree，并非只有简单的 Panel/Label/Button。按可用程度划分如下：

| 领域 | 状态 | 当前能力或限制 |
| --- | --- | --- |
| 基础控件 | 可用 | Panel、Label、Button、ProgressBar |
| 复合控件 | 可用但仍需扩展 | Toolbar、CharacterPanel、Dialog、虚拟化 ListView、ScrollView、单行 TextEdit |
| 表单控件 | 未实现 | Checkbox、Slider、Dropdown、TreeView |
| 布局 | 可用的自研子集 | Measure/Layout、Percent、WrapContent、MatchParent、Margin、VBox、HBox、Grid；不是完整 Flex |
| 文本 | Windows 主路径可用 | UTF-8、FreeType、中文 Glyph Atlas、选择区、单行编辑和 IMM32 preedit；复杂 shaping、多行和 Linux preedit 未完成 |
| 渲染 | 当前可用、边界待收敛 | 颜色/图片/文字批处理、Layer、嵌套 Scissor Clip；仍直接依赖 bgfx UIRenderer |
| 输入与焦点 | 基础契约较完整 | 单次 hit-test、Pointer、Wheel、Focus、Capture、KeyDown/KeyUp、空间导航和 Modal Focus Scope |
| 可访问性与视觉回归 | 未完成 | 缺语义树、读屏契约、实体手柄矩阵和稳定截图门禁 |

因此当前 UI 的主要短板不是 Retained Tree、布局或事件路由，而是常用产品控件、可访问性、跨平台文本细节和后端无关绘制边界。

每个窗口由 EventSystem 持有一个 `UIContext`、一个输入路由上下文和 generation slot registry。`UIContext` 独立拥有 Theme、逻辑窗口尺寸、framebuffer 尺寸、content scale、用户缩放和 revision；多窗口之间不共享可变主题状态。`NodeId(index, generation)` 是窗口内唯一交互句柄，hover、pressed、focused、captured 和 roots 均保存 NodeId；slot 复用前递增 generation，因此旧句柄不能解析到替代节点。节点还观察 EventSystem 的生命周期，即使节点晚于窗口上下文析构也不会解引用悬空指针。

当前 Scene 可注册多个顶层 UI 树，但每帧只在统一 UI Phase 中选择一个最上层目标。Scene 显式向整棵 UI 树注入 EventSystem 和 UILayoutManager，动态新增子节点继承相同上下文；节点从父树移除时立即注销 NodeId 并清除相关交互状态。活跃 Scene 在 `addUIRoot()`、每帧业务更新前和 `onResume()` 前同步 roots；场景暂停时立即停用旧 roots，因此 `onEnter()` 中显示的模态框可以建立焦点范围，暂停场景也不会继续接收 UI 输入。

鼠标按下可点击节点时建立 Pointer Capture，Move/Up 即使发生在节点外仍发送给捕获节点，释放后产生 Capture Changed 并自动解除。点击只在按下节点与释放命中节点相同时成立。窗口焦点由 EventSystem 唯一持有，Tab 与 Shift+Tab 按 UI 树顺序切换；普通 KeyDown/KeyUp 只路由到当前 generation NodeId 对应的焦点节点。订阅者可在 Capture/Target/Bubble 任一阶段 `preventDefault()`，传播控制状态可从只读事件回调安全修改。方向键在焦点控件未消费时执行空间导航，优先选择导航方向 beam 内的可见、启用节点，相同评分按树顺序确定；TextEdit 消费 Left/Right 后仍保持光标编辑语义。Button 默认可聚焦，Enter/NumpadEnter/Space 非重复按键只激活一次，KeyDown 设置键盘 pressed，KeyUp 或焦点丢失清理 pressed；即使 KeyUp 传播被停止，也会执行目标控件的局部状态清理。每个 Button 独立持有 `UIAction`：同一 action 的递归调用会被拒绝，不同 action 可以嵌套；异常离开后 dispatch 状态自动恢复，回调替换、清除或销毁按钮自身均不会访问失效 action。routed click 在每个阶段重新解析 generation `NodeId`，目标在 Capture 阶段被移除后立即停止后续投递和本地默认回调。

手柄输入只由 GLFW 标准映射直接轮询。InputSystem 暴露连接状态、按钮 Down/Pressed/Released 和左右摇杆、扳机轴；D-pad 与左摇杆转换为设备无关的 `UINavigationAction`，A/B 分别映射 Accept/Cancel。左摇杆使用 0.60 engage、0.40 release 回滞，方向按住 350ms 后以 100ms 间隔重复，避免临界值抖动和过快跨越。Accept/Cancel 复用焦点 KeyDown/KeyUp 生命周期，但不会产生全局游戏键盘事件，因此 UI 导航不污染玩法按键订阅。

Modal Focus Scope 使用 generation `NodeId` 栈管理。直接聚焦、Tab/Shift+Tab 和方向键导航都只能进入最上层 scope；嵌套 scope 按栈顺序退出并恢复进入前焦点，隐藏、禁用、移除或 generation 失效的 scope 会自动退出。`UIDialog` 的显示/隐藏负责进入/退出 scope，不再通过全局 `KeyPressedEvent` 绕过 routed event；焦点目标未消费按键时，默认处理才从目标向祖先回退，因此 TextEdit 可保留 Enter 等编辑语义，Dialog 仍可在祖先位置处理 Escape。

Theme 首轮已接入 Panel、Label、Button 和 TextEdit，提供每窗口 Dark/Light/Custom 值对象；控件没有显式设色或字号时解析窗口 Theme，显式设置仍可安全覆盖并可恢复主题值。DPI 以 GLFW logical size 与 framebuffer size 的比值为唯一来源，逻辑鼠标坐标只转换一次后进入 framebuffer-space hit-test。菜单、世界选择、设置和暂停界面的最终缩放统一为“逻辑分辨率响应式 × content scale × 用户缩放”，TextEdit 的字体、padding、拖选坐标也使用同一窗口度量。

`UINode::setClipChildren(true)` 同时约束渲染和 hit-test，嵌套空裁剪不会意外恢复为“无裁剪”。`UIScrollView` 提供垂直/水平/双轴、目标偏移、边界钳制、DPI 滚轮步长和帧率无关平滑；滚轮命中子按钮时会向上寻找最近可滚动祖先。ListView 只遍历可见行与 overscan，十万行数据不会产生十万次绘制，并已接入窗口 Theme。

Windows 文本输入保持两条独立通道：GLFW character callback 只提交最终 `TextInputEvent`；Win32 IMM32 通过窗口 subclass 产生 `TextCompositionEvent`，携带 Started/Updated/Ended/Cancelled、UTF-8 preedit 和 codepoint 光标。TextEdit 在正文光标处显示 preedit、下划线和组合光标，持续更新候选框位置，composition 期间不会让 Backspace/方向键同时修改正文。Linux 保持已提交字符路径，平台桥接安全退化为空实现。

布局请求由每 Scene 的 UILayoutManager 批量处理，每帧最多提交一次。`UINode::update()`、`render()` 和 `containsPoint()` 均不再隐式触发布局。逻辑节点与 bgfx 渲染实现已拆文件，使布局和事件可以在无 GPU 的 GoogleTest 中验证。

Windows/MSVC 2026 与 Linux/GCC 当前已有自动化门禁覆盖：hit-test 不隐式布局、重叠节点只命中最上层、Capture/Target/Bubble 顺序、动态子节点继承上下文、stale NodeId、上下文先析构、节点移除/自移除安全失效、捕获外释放、正反向焦点遍历、焦点 KeyDown 路由/默认取消/重复键抑制/路由中删除目标、KeyUp 完整路由/停止传播后的局部清理/generation 失效、方向键 beam 优先与隐藏/禁用过滤、Modal Focus Scope 限制/嵌套恢复/自动失效、设备无关语义导航的 scope/Accept/Cancel 生命周期、未处理按键向祖先回退、每窗口 Theme/DPI 隔离、200% DPI 逻辑坐标命中、裁剪命中边界、ScrollView 钳制/祖先滚轮路由、十万行虚拟范围、Button action 重入/异常/自销毁，以及 composition 与 committed text 的事件隔离。

## 已知问题

- 当前 Input Snapshot 只暴露一个鼠标左键布尔状态，事件结构虽预留 pointerId，尚未接入多指针、多按钮和触摸；
- Tab/Shift+Tab、方向键与 GLFW 标准手柄空间导航、Modal Focus Scope、Button 焦点视觉和 Enter/Space/Accept 完整按下/释放生命周期已完成首轮；仍缺可访问语义与实体手柄矩阵验收；
- Theme 已接入基础控件，但场景中仍有显式品牌色和尺寸；后续需要 token 化 spacing/radius/border，并补主题切换示例和截图回归；
- 当前是 VBox/HBox/Grid 布局，不是完整 Flex；dirty 上下传播仍可能扩大更新范围；
- ScrollView 与 ListView 已有纵向基础能力，但尚缺拖动滚动条、惯性/触摸手势、嵌套滚动消费和可复用 item template；
- TextEdit 已支持 Windows IME preedit/composition，但 Linux 原生 preedit、复杂 shaping、字形簇、IME attribute span 和完整多行编辑仍未完成；
- UI 绘制仍直接依赖 bgfx UIRenderer，尚未形成后端无关 Display List；
- 尚缺运行时 content-scale 回调、无障碍语义、实体手柄自动化注入和稳定截图回归。

## 目标契约

每个活动 Scene/窗口只拥有一个 UIContext 和一个逻辑 Root；一次 Input Snapshot 只执行一次 hit-test，按 Capture → Target → Bubble 路由。Measure dirty 向上收敛，Layout/Transform dirty 只在父输出变化时向下传播；hit-test 和 render 不允许隐式触发布局。

UI 绘制输出 Quad、Text、Clip DisplayList，由 Renderer 批处理。中文文本统一走 UTF-8 与 FreeType Glyph Atlas。

## 推进顺序

1. generation NodeId、统一 Pointer/Focus/Capture 生命周期和对应 GoogleTest 已完成首轮；
2. 每窗口 Style/Theme、DPI/content scale 和高 DPI 输入坐标已完成首轮；
3. 通用 Clip/ScrollView、ListView 虚拟化与 Windows IME composition 已完成首轮；
4. 焦点 KeyDown/KeyUp 路由、Button 键盘 pressed 生命周期、方向键/GLFW 标准手柄空间导航和 Modal Focus Scope 已完成首轮；
5. Button action 的实例级重入、异常恢复、回调自销毁和 routed click 目标失效门禁已完成；
6. 随后实现设置界面直接需要的 Checkbox、Slider，并补可注入手柄测试和基础可访问语义；
7. Dropdown、TreeView 等复杂控件按真实场景需求增加，不提前堆控件；
8. 在继续扩大 UI 绘制面之前，将其收敛为后端无关 Display List，并补稳定截图回归。

因此 UI 需要继续完善，但当前优先级应是稳定基础契约，而不是先继续堆控件数量。
