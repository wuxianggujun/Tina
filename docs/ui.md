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

Visual Studio 2026 / MSVC 19.50 与 Linux/GCC 当前已有自动化门禁覆盖：hit-test 不隐式布局、重叠节点只命中最上层、Capture/Target/Bubble 顺序、动态子节点继承上下文、stale NodeId、上下文先析构、节点移除/自移除安全失效、捕获外释放、正反向焦点遍历、焦点 KeyDown 路由/默认取消/重复键抑制/路由中删除目标、KeyUp 完整路由/停止传播后的局部清理/generation 失效、方向键 beam 优先与隐藏/禁用过滤、Modal Focus Scope 限制/嵌套恢复/自动失效、设备无关语义导航的 scope/Accept/Cancel 生命周期、未处理按键向祖先回退、每窗口 Theme/DPI 隔离、200% DPI 逻辑坐标命中、裁剪命中边界、ScrollView 钳制/祖先滚轮路由、十万行虚拟范围、Button action 重入/异常/自销毁，以及 composition 与 committed text 的事件隔离。

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

每个 Window 只拥有一个 UIContext；AppState 可以拥有若干 retained roots，但只向窗口 Context
注册/注销，不拥有 Node registry、Focus、Capture 或 Layout Manager。首期只有一个 primary
Window。Platform InputFrame 中每个有序 Pointer transition 最多执行一次 hit-test，并按
Capture → Target → Bubble 路由；不能把一整帧多个 Down/Up/Wheel/Text transition 压成一次
交互。
Measure dirty 向上收敛，Layout/Transform dirty 只在父输出变化时向下传播；hit-test 和 render
不允许隐式触发布局。

UI Phase 拆成两段：玩法 Action Mapping 前先用上一帧已提交布局执行 Input Routing 并输出
消费掩码；Variable Update 后再执行 model commit、最多一次布局和 DisplayList。AppState 新
root 必须先显式布局，下一帧才开放命中；UI action 引发的 push/pop/replace 在 Deferred
Cleanup 提交，因此当前 routed event 不会销毁正在遍历的状态，也不会让点击穿透到玩法。

vNext 不提供每帧重建树的 `buildUI(UIContext&)`；`UIUpdateContext` 只允许更新 retained model、
style、action 和 dirty state，Runtime 负责生成 DisplayList。

UI 绘制输出 Quad、Text、Clip DisplayList，由 Renderer 批处理。中文文本统一走 UTF-8 与 FreeType Glyph Atlas。

### 字体、中文与 Glyph Atlas 生命周期

Runtime 不按文件路径打开字体。Cooker 产出 Font Asset，UI/字体任务持有 `AssetLease<FontData>`；
`tina_ui_freetype` 只把拥有生命周期的字体 bytes + face/size/glyph request 转成 CPU bitmap/metrics，
Atlas page、generation GlyphHandle、UV 和 GPU texture 仍由 `tina_ui`/Render typed handle 管理。
FreeType face 不跨 Worker 并发共享；需要并行时每个 raster worker 使用自己的 library/face 实例。

首屏中文字体和 fallback chain 在 AppState 开放输入前预热。运行时缺字进入有界 raster queue，
完成后经 main completion + GPU upload budget 发布，并从下一帧可见；迟到结果重新校验 Font/
Atlas generation。等待期间使用确定 fallback glyph/advance，完成只标记对应 Text measure dirty，
hit-test/render 不得暗中触发布局。Atlas 使用固定 page/byte 上限，page eviction 只在 Deferred
Cleanup 且当前 DisplayList/GPU ticket 已退役后发生；满容量不能覆盖仍在用 UV。

UTF-8 解码错误产生 U+FFFD 与结构化诊断，不越界；首期没有复杂 shaping，文档与样例必须明确
支持范围，不能把 CJK/emoji/combining sequence 的 codepoint 逐字渲染宣称为完整排版。

### 设置页首批表单控件

`Checkbox` 与 `Slider` 是 vNext Product UI 的首批新增控件，因为当前设置页确实需要全屏开关
和 Master/Music/SFX 音量。首期不建设反射式 Data Binding；AppState 拥有明确的
`SettingsModel`，控件 action 调用窄的 Settings/Window/Audio 接口，失败时恢复 model 并显示
UTF-8 错误。控件销毁、回调替换和回调内删除自身必须延续 Button 已验证的安全语义。

`Checkbox` 契约：

- 值只有 checked/unchecked；indeterminate 等真实需求出现后再扩展；
- Pointer click、Space/Enter 和 Gamepad Accept 走同一个 default action，一次按下/释放最多
  切换一次；被 `preventDefault()`、disabled 或 modal scope 拦截时不改变值；
- 值实际变化后才发送带新 `bool` 的 change action，程序化 `setChecked` 可明确选择是否通知；
- Focus、Hover、Pressed、Checked、Disabled 都是 Theme state，不由业务代码手写颜色。

`Slider` 契约：

- `min/max/value/step` 必须有限且 `min <= max`、`step > 0`；输入先 clamp，再以 min 为原点
  做稳定量化，浮点比较使用由 step 推导的容差；
- Pointer down 建立 capture，拖动与 pointer up 即使离开轨道也保持同一交互；Left/Down 减一
  step，Right/Up 加一 step，Home/End 到边界，Gamepad 导航复用同一语义；
- `normalizedValue` 在 `min == max` 时确定返回0，不能除零；只有量化后的值实际改变才发送
  `float` change action，单帧最多提交一次最终值；
- 轨道、填充、thumb、Focus、Hover、Dragging、Disabled 使用 Theme token，并在100%/150%/
  200% DPI 下保持最小命中尺寸。

两者至少暴露 Role、Name、Value、Enabled、Focused 与可执行 Action 的基础语义节点；这不是完整
读屏实现，但从第一个新控件开始禁止把可访问性完全后补。参数化 change action 使用 Tina
自有、实例级的受控 dispatch；不引入全局 signal bus，也不让异常穿过 UI route 边界。

### 基础可访问语义与 Theme token

每个交互节点可持久保存小型 `Semantics`：Role、UTF-8 Name/Description、Value/Range、
Checked/Enabled/Focused 状态、可执行 Action 和可选 `labelledBy NodeId`。装饰节点默认不进入语义
树；语义子序与可见 UI 树顺序稳定一致。TextEdit 默认不把密码或 composition 正文写入诊断，
语义失效仍通过 generation NodeId 检查。首期先提供可查询的内部语义树和 GoogleTest；Windows
UI Automation/Linux AT-SPI adapter 在独立平台设计完成前不得用 FrameArena 裸指针或跨线程
直接访问 UINode。

Theme 至少把 Color、Typography、Spacing、Radius、Border、FocusRing、MinimumHitSize 定义为
每窗口值 token。Widget 只解析 token + 显式局部 override，不保存指向可变全局 Theme 的裸引用；
主题或 DPI revision 变化只标记受影响 style/layout，不能无条件重建整棵树。品牌色和业务间距
留在产品 Theme，不硬编码进 Checkbox/Slider 等通用控件。

Retained Node、Style、Text model 和 glyph metadata 属于 UI tagged persistent resource；Measure/
Layout scratch 与 DisplayList 分别使用当前帧 UI Arena。节点不得保存 FrameArena 指针，
DisplayList command 只在本帧 UI Pass 前有效。UI tree 只允许主线程结构修改；后台字体/图片
任务返回 generation handle，由主线程 completion 提交。

性能门禁以 dirty 行为而不是控件数量衡量：无变化 UI 的 layout 次数和 Tina-owned 动态分配
增量都为0；
5,000逻辑节点与100,000行虚拟列表基准只处理 dirty/可见集合，并记录 layout、hit-test、
DisplayList build 和 batch p50/p95/p99。统一预算见 [性能预算与内存系统](performance-memory.md)。

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
