# Platform、Window 与 Input 契约

> 状态：vNext 设计讨论稿。本文只定义平台边界，不表示当前 GLFW 实现已经完成迁移。

## 结论

Tina 只使用 GLFW 处理 Windows/Linux 窗口、键鼠和标准 Gamepad；不引入 SDL/SDL3。
Windows 文本组合由 `tina_platform_glfw` 内部的 IMM32 adapter 补充。玩法、UI、Scene 和
Render 公共接口不能包含 `GLFWwindow*`、Win32 `HWND` 或平台键值。

为了让 Null Runtime 真正不链接 GLFW，vNext 将平台契约和具体实现分开：

| Target | 职责 | 依赖 |
| --- | --- | --- |
| `tina_platform` | `WindowId`、Window/Input/Event 描述、线程命名、不透明 Render surface、Headless test backend | `tina_core`、OS 最小适配 |
| `tina_platform_glfw` | GLFW 生命周期、真实窗口、键鼠、Gamepad、DPI、Windows IMM32 | `tina_core`、`tina_platform`、GLFW、Windows 时使用 IMM32 |

`tina_platform` 不提供第二套通用窗口框架；拆 target 只为隔离第三方类型和支持无窗口测试。
UTF-8 路径、读取和原子文件写入统一属于 `tina_core/io` 的 OS-specific `.cpp`，Platform 不再
实现第二套 filesystem adapter。

## 所有权与线程

- `EngineHost` 在主线程创建并销毁 Platform backend；
- GLFW 初始化、事件轮询、窗口创建/销毁和大多数窗口操作只在主线程执行；
- 每个窗口使用 generation `WindowId`，stale ID 不能解析到复用后的窗口；
- 首个产品切片只支持一个主窗口，但所有输入、DPI、Focus 与 UIContext 状态都按 Window
  保存，不使用进程级可变全局；
- 关闭窗口只产生 `CloseRequested`，由 Runtime 在安全提交点决定退出，平台回调中不销毁
  Scene、UI 或 RenderDevice；
- 后台线程不能调用 GLFW，也不能保存原生窗口指针。

Platform 与 Render 之间只交换窄的 `RenderSurfaceDescriptor`。其中的原生句柄是不透明借用，
只允许 `tina_render_bgfx` 在创建/重置 surface 时读取；游戏、Scene、UI 和公共 Render API
看不到 `HWND`、X11、Wayland 或 GLFW 类型。窗口销毁前 Runtime 必须先停止对应 surface 的
提交并完成 Render barrier。

## 每帧 InputFrame

Platform 每个 Render Frame 只轮询一次并生成不可变 `InputFrame`。它同时包含最终设备状态
`InputSnapshot` 和按 platform sequence 排序的 `InputTransitionBatch`；只保留最终布尔快照会
丢失同一轮 Poll 内的 press→release、多个 wheel/text/composition 事件及其顺序。

```text
GLFW poll / native callbacks
  -> normalize platform events
  -> finalize per-window InputFrame
       InputSnapshot + ordered InputTransitionBatch
  -> Runtime Event Queue
  -> UI routed transitions (上一帧稳定布局)
  -> per-frame InputConsumption mask
  -> game action mapping / fixed input latch
```

`InputSnapshot` 按窗口保存：

- 键盘、Pointer 和 Gamepad 的最终 `held` 状态；
- Pointer 最终 position、累计 delta 和当前 button；
- Window focus、logical size、framebuffer size、content scale；

`InputTransitionBatch` 保存 Key/Button Down/Up、Pointer Move、Wheel、Gamepad connect/button、已提交
UTF-8 text 与 composition transition；每项都有 window/device/pointer id 和单调 sequence。
连续 Move 只有在中间不存在 Button/Wheel/Capture/Focus 边界时才可合并，文本、composition、
Down/Up 与设备连接绝不合并。批次使用预分配有界存储：先合并可合并 Move；仍满时记录
`InputOverflow`、取消本帧交互并在下一帧从设备最终状态做显式 resync，不能静默丢一半按键让
`held` 永久卡住。

Fixed Simulation 可能在一个 Render Frame 内运行0到4步，因此输入边沿不能简单复制到每个
fixed step：

- 每个 Action 在映射时声明 `Simulation` 或 `Frame` domain；同一 active Input Context 中同一
  transition 默认不能同时产生两个 domain 的 action，确有需求必须用两个显式物理 binding；
- Simulation `held/axis` 状态可供本帧所有 fixed step 读取；未被 UI 消费的 Down/Up transition
  转成 Simulation Action edge 后，由第一个实际执行的 fixed step 消费一次；
- 如果本帧没有 fixed step，待消费 Action 保留到下一次 fixed step，不静默丢失；
- Frame Action edge 只在当前 Variable Update 消费一次，帧末丢弃，不在0个 fixed step 时保留；
- Pointer delta、wheel、文本和 UI navigation 每个 Render Frame 最多消费一次，不重复给4个
  fixed step；
- 回放/确定性测试只记录归一化 Simulation Action 与目标 simulation tick，而不是 GLFW key
  code；Frame Action 只用于相机/表现等非确定性帧逻辑。

UI 使用同一 InputFrame 的有序 transition 生成 Pointer/Key/Navigation routed event。每个 Pointer
transition 最多 hit-test 一次；有 Capture 时直接解析 captured NodeId，需要判定 click 的 release
再做一次且仅一次 release-position hit-test。UI `preventDefault()` 不回写 Platform 数据，而是
按 transition sequence 写入本帧独立的 `InputConsumption`。Gameplay Action Mapping 只读取未
消费 transition；持续 `held` 是否被遮挡由 AppState 的 Input Context/Action Map 明确决定，
不能依赖 UI 修改全局键盘状态。消费记录只活到当前 Render Frame，回放记录最终 Action 与
目标 simulation tick，不记录 UI 内部 NodeId。

## 三条事件通道

以下通道保持分离：

1. `InputFrame`：最终设备快照 + 有序 transition，适合轮询、UI route 和 Action Mapping；
2. Runtime `EventQueue`：Window resize/focus/close、设备连接等离散事件，RAII Token 订阅；
3. UI routed event：一次 hit-test 后按 Capture → Target → Bubble 投递。

同一平台事件可以派生出快照状态和一个 Window event，但只能在各自固定阶段泵送一次。
订阅取消、窗口销毁和 generation 失效必须立即阻止后续回调访问旧对象。

## Focus、Capture、DPI 与 IME

- 窗口失焦时合成所有 held key/button 的 release，清除 Pointer Capture、keyboard pressed、
  Gamepad repeat 和未提交 composition；
- 鼠标按下建立的 OS capture 与 UI `NodeId` capture 分开记录，两者在释放、失焦、窗口关闭
  或目标失效时成对清理；
- GLFW logical size、framebuffer size 与 content scale 是三个不同量；UI 逻辑坐标只做一次
  明确转换，Render 使用 framebuffer pixel；
- Windows IMM32 context 由窗口 adapter 拥有，subclass/association 必须在窗口销毁前恢复；
- `TextInputEvent` 只包含已提交 UTF-8；`TextCompositionEvent` 保存 preedit、codepoint cursor
  和 Started/Updated/Ended/Cancelled；
- Linux 首期保证 committed text，原生 preedit 后置，但接口必须安全返回“不支持”而不是
  伪造已提交文本。

## Gamepad

首期只接受 GLFW standard mapping。每个设备使用 generation `GamepadId`，连接/断开在
Event Queue 提交；按钮和轴进入下一帧快照。UI 的 D-pad/左摇杆、Accept/Cancel 是设备无关
语义，摇杆回滞和长按重复由 UI navigation 层处理，玩法 Action Map 保留独立配置。

实体手柄冒烟不能替代自动化。Platform adapter 必须允许测试注入标准化状态，以验证连接、
断开、回滞、重复、Focus 丢失和同帧边沿。

## 错误与降级

- 主窗口或必需 surface 创建失败：`EngineHost::Create` 返回带平台 error code 的 `Result` 并
  逆序回滚；
- Gamepad、clipboard、cursor shape 等可选能力失败：记录结构化 warning，保持窗口可运行；
- 无显示环境的测试显式选择 Headless backend，不能捕获 GLFW 初始化失败后偷偷降级；
- Platform 回调边界捕获异常并转换为错误/退出请求，异常不能穿过 C callback；
- 日志只记录 UTF-8 操作名、错误码和必要上下文，不写用户输入正文或敏感 clipboard。

## 验收

- Headless backend 不链接、不加载 GLFW，Null Runtime 可运行300帧和10,000帧；
- Visual Studio 2026 / MSVC 19.50 与 Linux GLFW backend 能创建、resize、最小化、恢复并正常关闭窗口；
- WindowId/GamepadId stale generation、失焦合成 release、CloseRequested 延迟提交有直接测试；
- fixed step 0/1/4 次时，Action edge 只消费一次且不丢失；
- 同一 Poll 内 Down→Up、多次 Wheel/Text/Composition 保持 sequence；Move 合并不跨语义边界；
- InputTransitionBatch 满容量会受控 resync，不产生永久 stuck held/capture；
- 100%、150%、200% DPI 下 Pointer 命中和 framebuffer viewport 一致；
- Windows IMM32 composition/commit/cancel 与窗口销毁顺序通过测试；
- Platform/UI/Gameplay 三条输入通道互不重复投递。
