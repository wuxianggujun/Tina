# ADR 0032：移动平台（Android / iOS）的契约边界

- 状态：Proposed
- 日期：2026-08-28
- 决策者：Tina maintainers

## 背景

2026-08-28 核验了「现在支持 Android/iOS 需要什么」，并通读了 cocos2d-x 的 iOS 平台层作为参照。

**后端本身很小。** `IPlatformBackend`（`include/tina/platform/PlatformBackend.hpp:42-50`）只有 4 个纯虚，
加上 `IWindowSurfacePlatformBackend`（`include/tina/integration/WindowSurface.hpp:60-73`）的 3 个，共
**7 个**。headless 后端是 96 行；GLFW 后端连头文件是 4158 行，其中 gamepad 映射表 271 行、file drop 与
system color scheme 等桌面专属项可以不做。

**真正的成本在六个已生效的桌面假设上。** 它们不是缺陷——每一条都是被写下来并（多数）由测试守住的决定，
而移动端需要把其中五条往外推一格、第六条重新决定：

| # | 现有契约 | 位置 | 移动端为何冲突 |
| --- | --- | --- | --- |
| C1 | 只发布 `PrimaryPointerId` | `PlatformFrame.hpp:829,835,840,918` 四处校验；`tests/platform/PlatformBackendTests.cpp:711,727,743,1614` 断言非 0 被拒；ADR 0020:31 写成门禁 | 多点触控。且 UI 侧 `armedSlider`/`capturedPointerNode`/`hoveredPrimaryControl`（`src/ui/detail/UIContextImpl.hpp:293-304`）都是单槽 |
| C2 | 指针位置每帧必须有限且存在 | `PointerSnapshot`（`Input.hpp:199-212`）无 presence 标志；`PlatformFrame.hpp:918-920` 要求有限 | 手指抬起后没有位置。契约无法表达"不在了"，最后触点会永久 hover，`Hovered` 样式与 tooltip 锚点一直保留 |
| C3 | native surface 在 RenderDevice 生命周期内不变 | `WindowSurface.hpp:31-33` lease 钉住 binding；binding 变化返回 `NativeWindowBindingChangedUnsupported`（`RenderErrors.hpp:13`，ADR 0020:154-156） | Android 的 `surfaceDestroyed`/`surfaceCreated` 循环当前会终止整个 run |
| C4 | poll 线程 == 渲染线程 | `PlatformBackend.hpp:35-38` 明文；`EngineHost.cpp:620-622` 错线程析构 `std::terminate`，`:642-645` 错线程 run 返回 `WrongOwnerThread` | 移动端输入到达 UI 线程 |
| C5 | 只有 D3D11/OpenGL/Vulkan 三个 renderer | `cmake/TinaBgfxEmbeddedShaders.cmake:15-21` 只产出 glsl/spv(+dxbc)；4 张表 6 个 program（`BgfxUITexturedShader.cpp:24-32` 等）硬编码这三个 | iOS/Android 上 `getRendererType()` 返回 Metal/OpenGLES，查表未命中，program 创建失败 |
| C6 | preedit 由应用控制 | `TextCompositionTransition` + 四阶段 `TextCompositionStage`（`Input.hpp:384-396`） | 软键盘只交付已提交文本。且**没有** show/hide keyboard 方法，`WindowMetricsSnapshot`（`Window.hpp:38-47`）无键盘遮挡概念 |

`suspended` 已存在但只表达"最小化或 0x0"（`GlfwPlatformBackend.cpp:843-844`），ADR 0020:197 明确
「`surfaceSuspended` 不表示 device lost」。所以 C3 不是"补个状态"，是新增一类生命周期事件。

**cocos2d-x 参照的三个结构教训**（细节见 `docs/platform-input.md`）：

1. **iOS 上 `Application::run()` 不循环。** `CCApplication-ios.mm:50-57` 调完 `startMainLoop` 立即返回，
   帧由 `CADisplayLink` 驱动；其他平台的 `run()` 阻塞。这个不对称是它 iOS 层最深的结构差异，而
   `EngineHost::run()`（`EngineHost.cpp:642`）正是阻塞循环。
2. **它的生命周期只有两个钩子，而自动 pause 被删掉了。** `AppController.mm:98-99` 注释原文：
   *"We don't need to call this method any more. It will interrupt user defined game pause&resume logic"*。
   而且有两个互不知情的 pause 闸门（ObjC 层听 resign-active、C++ 层听 enter-background），
   `Director::stopAnimation()` 只置 `_invalid = true` 而**不停 CADisplayLink**，后台仍每秒醒 60 次。
3. **它的 touch id 分配形态对、实现有洞。** ObjC 侧把 `UITouch*` 指针 reinterpret 成 `intptr_t` 过边界
   （`CCEAGLView-ios.mm:263`），C++ 侧用 `map<intptr_t,int>` + 32 位空闲位掩码分配密集 id。形态是对的
   （引擎要 0..n），但 `handleTouchesBegin`（`CCGLView.cpp:310-328`）对**已在 map 里的 id 没有 else
   分支**，静默丢弃；UIKit 复用 `UITouch` 对象，因此陈旧条目让那个地址上的下一根手指永久不可见。
   `IOS_MAX_TOUCHES_COUNT 10` 与 `EventTouch::MAX_TOUCHES 15` 不一致，超出直接 `break`。没有任何
   "释放全部 touch"机制挂在后台/视图销毁上，拖动中切后台且 cancel 被吞则该手指卡到进程结束。

## 待确认决策

| # | 决策点 | 推荐 | 主要备选与取舍 |
| --- | --- | --- | --- |
| D1 | 顺序 | **先扩宽契约（C1/C2），再写后端**。多点触控与 pointer presence 可以完全在桌面上实现并验证 | 先写后端：会在没有多点模型的情况下产出一个只能发 `PrimaryPointerId` 的移动后端，等于把 cocos 默认模板 `setMultipleTouchEnabled:NO` 的处境重演一遍 |
| D2 | 平台顺序 | **Android 先，iOS 后**。`ANativeWindow` → bgfx 比 `CAMetalLayer` 简单，且 iOS 还要额外解决 `run()` 不能阻塞 | iOS 先：Metal 后端更现代，但 `run()` 形态冲突会立刻迫使一个尚未准备好的决定 |
| D3 | `run()` 形态（C4/iOS） | **保持阻塞 `run()`，移动后端在内部驱动**：Android 用 NDK 的 looper 在 `pollFrame()` 内取事件；iOS 需要一个由 `CADisplayLink` 驱动的 pump 而 `run()` 在其上阻塞等待 | 改成外部驱动的 `tick()`：更贴合 iOS，但会改变所有平台的 `EngineHost` 契约与 `RunExitReason` 语义，且 ADR 0014 的四相位阻断都建立在 `run()` 拥有循环之上 |
| D4 | 多点触控的下游状态（C1） | UI 侧单槽状态改为按 `PointerId` 索引的固定容量表 | 只放宽平台校验：`armedSlider` 等仍是单槽，第二根手指会抢走第一根的控件——正是 cocos 三个圆形控件的多点缺陷 |
| D5 | pointer presence（C2） | `PointerSnapshot` 增加 presence 标志；缺席时 hover 判定跳过，且不要求位置有限 | 用哨兵位置（如 NaN 或屏幕外）：会与 `PlatformFrame.hpp:918-920` 的有限性校验冲突，且每个消费者都要自己认哨兵 |
| D6 | surface 重建（C3） | 新增一类 native binding 失效/重建事件，允许 RenderDevice 在同一 run 内重建 GPU 资源；`NativeWindowBindingChangedUnsupported` 保留给**真正不支持**的后端 | 把 Android 的 surface 循环压成 `suspended`：语义错误——GPU 资源确实丢了，画上去是未定义行为 |
| D7 | 软键盘（C6） | `IPlatformBackend` 增加 show/hide keyboard 与键盘遮挡矩形；移动后端只发 `TextInputTransition`，preedit 保持可选 | 让移动端伪造 preedit 阶段：cocos 的 `setMarkedText:`（`CCInputView-ios.mm:178-188`）存下 marked text 却**什么都不发**，因为 `CCIMEDelegate.h` 里没有 preedit 概念可以送——伪造只会得到同样的空壳 |
| D8 | shader profile（C5） | 把 `TinaBgfxEmbeddedShaders.cmake` 的 profile 列表按目标平台扩展，4 张表补 Metal/ESSL 分支 | 运行期从磁盘加载 shader：与 embedded shader 的现有形态冲突，且移动端资源打包另是一件事 |

## 决定（Proposed）

### 1. 不在本 ADR 内实现任何移动后端

本 ADR 只冻结**契约变更的范围与顺序**。写后端之前必须先落地 C1、C2 的模型，理由是 D1：没有多点模型的
移动后端只能发 `PrimaryPointerId`，那等于把桌面限制搬到触摸设备上，而修正它要回头改公开契约与测试。

### 2. C1/C2 先在桌面上完成，不等移动后端

多点触控与 pointer presence 是**桌面上可实现、可验证**的：放宽 `PlatformFrame` 的四处 `PrimaryPointerId`
校验（连同 `PlatformBackendTests.cpp` 的四个断言）、把 UI 的单槽交互状态改为按 `PointerId` 索引的固定容量
表、给 `PointerSnapshot` 加 presence 标志。`include/tina/ui/UIVirtualStick.hpp:70,191-206` 已经按
`PointerId` 隔离，但当前不可达，因为生产端只能发 0——它会成为第一个真实消费者。

Windows 上可以用多个 pointer 设备验证；无设备时至少 `PlatformFrameBuilder` 与 UI 路由的单测可以覆盖。

### 3. C3 是独立切片，且必须是事件而不是状态

`suspended` 表达"暂时不可画"，surface 销毁表达"GPU 资源已失效"。二者不能合并：把后者压成前者会让引擎在
资源已经消失后继续认为它们有效。因此需要新增一类失效/重建事件，并允许 RenderDevice 在同一 run 内重建
资源。`NativeWindowBindingChangedUnsupported` 保留给真正不支持 rebind 的后端（GLFW 可以继续用它）。

### 4. `run()` 保持阻塞（D3 推荐），代价记录在案

改成外部驱动的 `tick()` 会波及每个平台的 `EngineHost` 契约、`RunExitReason` 语义，以及 ADR 0014 建立在
"`run()` 拥有循环"之上的四相位阻断。因此推荐让移动后端在内部适配：Android 在 `pollFrame()` 内取 looper
事件；iOS 则需要 `CADisplayLink` 驱动一个 pump 而 `run()` 在其上等待。**这条如果选错，代价比其他五条
加起来更大**，所以它是本 ADR 最需要 maintainer 明确的一项。

### 5. 立即执行的一件事：删除死的 `cmake/ShaderUtils.cmake`

该文件（556 行）实现了 metal/300_es/ios/android 的 profile 与两套 shader 编译函数，但**全仓库零处
include 它**——`add_shader_compile_dir`、`add_shaders_directory`、`bgfx_compile_shaders` 的引用数均为 0。
它还引用了已删除的产品目标名 `tina2d_bgfx_shaders`（AGENTS.md:37 记载旧产品图已删除）。

它同时是最危险的一类残留：**看起来 Metal/GLES 已经支持了，实际上一行都没接进构建。** 真要做 C5 时应扩展
在用的 `TinaBgfxEmbeddedShaders.cmake`，而不是复活这个文件。因此删除，不保留兼容路径。

## 结果

- 移动端的成本第一次被精确表述：7 个纯虚 + 1 个 `NativeWindowBindingKind` 分支与对应 bgfx 解码 +
  6 个 shader program 重编 + JNI/UIKit 输入桥，加上上表六个契约；
- C1/C2 与移动后端解耦，可以先在桌面上做完并验证，`UIVirtualStick` 成为第一个真实多点消费者；
- 死的 shader cmake 不再冒充「Metal/GLES 已就绪」；
- 代价与限制：本 ADR 不实现任何后端；C3 需要 RenderDevice 生命周期内重建 GPU 资源的能力，属独立切片；
  D3 未定则 iOS 无法开工；移动端的资源打包、构建工具链与 CI 完全未涉及；
- 需要建立的门禁：多点触控的 `PlatformFrameBuilder` 校验与 UI 路由单测（同时按 `PointerId` 隔离
  armed/hover/capture）、pointer presence 的 hover 清除测试、surface 重建的 GPU 资源重建测试、
  Metal/ESSL shader 编译在对应平台的构建门禁。

## 被拒绝方案

- **先写后端再补契约**：会产出只能发 `PrimaryPointerId` 的移动后端，重演 cocos 默认模板单点触控的处境，
  并把公开契约与测试的修改推到之后；
- **把 surface 销毁压成 `suspended`**：语义错误，GPU 资源确实已失效；
- **让移动端伪造 preedit 阶段**：cocos 的 `setMarkedText:` 正是这样，存下 marked text 却什么都不发，
  因为它的抽象接口没有 preedit 概念——伪造只会得到同样的空壳；
- **保留 `cmake/ShaderUtils.cmake` 备用**：本仓库风格是直接删除不用的路径（AGENTS.md），而一个假装支持
  Metal/GLES 的死文件比没有更糟；
- **同时开工 Android 与 iOS**：iOS 额外背着 `run()` 形态冲突，两条一起做会让 D3 在没有证据时被草率决定。
