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
| C6 | preedit 由应用控制 | `TextCompositionTransition` + 四阶段 `TextCompositionStage`（`Input.hpp:384-396`） | 软键盘只交付已提交文本。且**没有** show/hide keyboard 方法，`WindowMetricsSnapshot`（`Window.hpp:38-47`）无键盘遮挡概念。**已解决（2026-08-30）**：显式映射表 + `AndroidCompositionSession`，见下文 C6 补充 |

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
| D3 | `run()` 形态（C4/iOS） | **已定（2026-08-28）：外部驱动 `start()`/`tick()`**，`run()` 保留为 `while` 封装、75 处调用点零改动。maintainer 未采纳原推荐的「保持阻塞、后端内部适配」，因为后者在 iOS 上需要第二线程 + 每帧信号 + 渲染提交 marshal 回主线程，反而违背它要保住的 C4。详见「决定」第 4 节 | 已否决：保持阻塞 `run()` 让移动后端内部驱动 |
| D4 | 多点触控的下游状态（C1） | UI 侧单槽状态改为按 `PointerId` 索引的固定容量表 | 只放宽平台校验：`armedSlider` 等仍是单槽，第二根手指会抢走第一根的控件——正是 cocos 三个圆形控件的多点缺陷 |
| D5 | pointer presence（C2） | `PointerSnapshot` 增加 presence 标志；缺席时 hover 判定跳过，且不要求位置有限 | 用哨兵位置（如 NaN 或屏幕外）：会与 `PlatformFrame.hpp:918-920` 的有限性校验冲突，且每个消费者都要自己认哨兵 |
| D6 | surface 重建（C3） | 新增一类 native binding 失效/重建事件，允许 RenderDevice 在同一 run 内重建 GPU 资源；`NativeWindowBindingChangedUnsupported` 保留给**真正不支持**的后端 | 把 Android 的 surface 循环压成 `suspended`：语义错误——GPU 资源确实丢了，画上去是未定义行为 |
| D7 | 软键盘（C6） | `IPlatformBackend` 增加 show/hide keyboard 与键盘遮挡矩形；移动后端只发 `TextInputTransition`，preedit 保持可选 | 让移动端伪造 preedit 阶段：cocos 的 `setMarkedText:`（`CCInputView-ios.mm:178-188`）存下 marked text 却**什么都不发**，因为 `CCIMEDelegate.h` 里没有 preedit 概念可以送——伪造只会得到同样的空壳。**落地时的两处偏离**：走 Android 专属 facet 而非新增纯虚（见下）；preedit 不再"保持可选"而是已实现，因为"不直接对应"不等于"无法映射" |
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

### 4. D3 已定：外部驱动的 `start()`/`tick()`（2026-08-28，maintainer 选择备选方案）

本节原推荐「`run()` 保持阻塞，移动后端在内部适配」，理由是改动面小。**maintainer 选择了备选方案**，
因为「内部适配」的代价并非表面那么小：iOS 上它需要引擎线程 + `CADisplayLink` 线程、每帧信号传递，以及
把渲染提交 marshal 回主线程（`CAMetalLayer` 必须主线程访问）——而这恰好违背它本想保住的 C4（poll 线程 ==
渲染线程），使 `EngineHost.cpp` 的错线程检查变成必须绕过的东西。外部驱动是零跨线程、零信号、零 marshal。

实现方式**不是**替换 `run()`，而是把帧提取成可调用单元：

- `tickOnce()` 是唯一帧函数体，`run()` 变为 `while` 封装。原循环 5 个 loop-local 变量收进
  `m_frameLoop`，57 个 return 中 48 个本来就走 `stopNormally`/`failAfterStartupCommit`，其余 9 个是 lambda
  体内的正常返回，**不是**循环出口——所以出口收敛比预估简单：`std::optional<Result<RunExitReason>>` 即可
  表达「继续 / 终态」，无需新增 outcome 枚举。
- `startUnchecked()` 从 `runUnchecked()` 拆出，公开为 `start()`；`tick()` 推进一帧。
- 三条入口（`run`/`start`/`tick`）共用 `guardRunBoundary()`，因此异常→`Result` 与 teardown 完全一致；
  `failUnexpectedRunException()` 改为返回裸 `Error` 以适配三种不同的 `Result` 类型。

代价与原文预估的差异也记录在案：ADR 0014 的四相位阻断（`forEachDispatch` 四处）**不受影响**，它作用在
一帧之内，与谁拥有循环无关；`RunExitReason` 语义未变；75 处 `run()` 调用点**零改动**，因为 `run()` 保留。
`start()`/`run()` 互斥、`tick()` 需先 `start()`、终态后 `tick()` 拒绝，三条各有测试，且
`ExternallyDrivenFramesMatchRunExactly` 逐事件比对两条路径。

剩余的 iOS 工作因此收窄为后端本身：`CADisplayLink` 回调直接调 `tick()`，无需线程编排。

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

## 后续实测修正（2026-08-29）

本 ADR 仍是 Proposed，上文保留为 2026-08-28 的原始判断。以下两条已被实测推翻或收窄，记录在此而不改写原文：

- **「构建工具链完全未涉及」已不成立。** Android 交叉编译现已打通并**包含 bgfx render backend**：
  NDK 28/29 × arm64-v8a/x86_64 四种组合下 16 个静态库零 error。仍未涉及的是**资源打包与 CI**，以及
  gradle/`AndroidManifest.xml` —— 即「能编译」到「能安装运行」之间的部分。配方见 `docs/building.md`。
- **C5 的「6 个 shader program 重编」低估了数量、也误判了难点位置。** 实际是 4 张表 11 个 program；而
  真正的障碍不在 shader 也不在 bgfx，是 `shaderc` 这个**构建期宿主工具**：bgfx.cmake 上游以朴素
  `add_executable()` 声明它、无交叉编译处理，于是交叉构建把它也编成目标架构（实测 460 MB AArch64
  ELF，宿主无法执行）。修法是 `TINA_BGFX_SHADERC_EXECUTABLE` 从宿主树导入。这再次印证 D8 拒绝
  `bgfx_compile_shaders()` 的第二条理由（它按**宿主**平台选 profile）是对的：宿主与目标的区分在这条
  路径上处处是坑。
- 此外 C3 已由 [ADR 0034](0034-native-surface-rebind.md) 落地，其「需重建全部 GPU 资源」的预估同样
  高估了范围（surface/swapchain 属 backbuffer，program/texture 属 device，不随 surface 失效）。

- **`NativeWindowBindingKind::Android` 分支已落地（2026-08-29）。** `ANativeWindow*` 是自包含的：bgfx 自己
  的 Android entry 把它直接交给 `nwh`、用默认 handle type、从不设 `ndt`。因此该分支**拒绝**携带
  display 指针的 binding —— 否则那个字段会被 bgfx 静默忽略，等于接受一个有无意义字段的 binding。缺失
  window handle 无需新代码：`WindowSurfaceLease.cpp:51` 早已在更前一层拒绝，Android 直接继承该守卫。
  `toBgfxPlatformData()` 的 kind switch 现在**穷尽且无 `default`**，故新增 kind 会编译失败而非运行期
  拒绝；非枚举值（如 `static_cast<...>(255)`）由 switch 之前一处显式范围检查负责。两条不变量已 revert
  验证：去掉 display 守卫后测试**挂死在 `bgfx::init`**（而非优雅失败），这正是该守卫存在的理由。

- **Android 平台后端骨架已落地（2026-08-29），且首次在真模拟器上运行。** `src/platform/android/` 实现全部
  7 个纯虚，成为 `tina_platform_android`（与 `tina_platform_glfw` 对称的独立 target）。三个设计取舍：
  ①**按目标平台而非 option 门控**（`if(ANDROID)`）—— option 会宣告一个不存在的选择，GLFW 是 option 因为
  桌面构建确实可以不带它；②**native window 以不透明 `std::uintptr_t` 跨边界**，公开头不出现
  `ANativeWindow*`，宿主从 `ANativeWindow_fromSurface()` 取值传入；③**零第三方依赖**（不链 libandroid、
  无 JNI、无 Android SDK 头），依赖足迹与 Headless 相同 —— 该依赖属于 JNI 输入桥那一片，不在此预留。
  extent 与 content scale 为 0 一律拒绝而非取默认值：二者都会静默改变下游每个 UI 元素的尺寸，比启动失败
  难查得多。
- **「编译通过」与「能用」的差距被实测抓到一次。** 骨架交叉编译零 error、零 warning，但在 x86_64 模拟器
  上 `pollFrame()` **每次都失败**：默认构造的 `WindowInputSnapshot` 通不过 `PlatformFrameBuilder` 的窗口
  校验（input 的 window id 与 metrics revision 必须与其伴随的 metrics 一致）。修正时顺带发现一个语义
  问题：该默认值让 primary pointer 保持 `present`，那适合总有位置的鼠标，对触摸是错的 —— 两次点击之间
  手指根本没有位置，present 却空闲的指针会把 hover 永久钉在上次触摸的控件上（正是 C2 要解决的缺陷）。
  故 Android 的静默输入把**全部** pointer 置为 absent。`tina_platform_android_tests` 10/10 在
  `emulator-5554`（Android 36 x86_64）**实机通过**，不是仅编译。

- **触摸输入桥已落地（2026-08-29）。** 形态取自 `docs/platform-input.md` 记录的 cocos2d-x 反例：它每个事件
  经 `runOnGLThread` marshal，**每事件分配一个 Runnable 并捕获一个 String**，轴事件洪流下是实打实的 GC
  压力。Tina 的 `PlatformFrameBuilder` 是固定容量、per-poll 的，故改为**单生产者/单消费者无锁环形缓冲**：
  Android UI 线程 push，owner 线程每次 poll 排空。有界且**有意可丢**——满时丢最新并计数（`droppedEventCount`
  单调不重置），因为增长会破坏固定容量不变量，而阻塞会让 UI 线程卡在停滞的引擎线程后面冻结整个 app。
  留一个空槽使「满」与「空」无需额外 size 计数即可区分。
- **`AndroidTouchSlotTable` 是对 cocos 那个具体缺陷的修复。** Android 的 pointer id 稀疏且会复用，必须映射
  到 Tina 的 0..7 密集槽。cocos 形态对而实现错：`handleTouchesBegin` 对**已在 map 中的 id 没有 else 分支**，
  静默丢弃，而 id 复用使一个陈旧条目让该 identity 上的**后续每根手指永久不可见**。本实现把映射收敛到唯一
  一处，且每条路径都有明确结果：重复 Down **保留原槽**（重复 Down 意味着 Up 丢了，重用最坏是重启该手指，
  丢弃则让它整个手势不可见）；槽位耗尽**拒绝**而非驱逐仍按下的手指；`releaseAll()` 供手势整体被收回时使用
  （cocos 没有这个钩子，后台化的拖拽会把手指卡到进程退出）。表按 `PointerCapacity` 而非字面量定尺，且**不
  可默认构造**——零初始化会让八个槽都声称在追踪 Android pointer id 0，而那是合法 id。
- **Cancel 按 pointer 作用域，绝不按窗口。** 逐指取消用 `InputCancelTransition::pointer` 命名该指；
  nullopt 意为全部八个，正是 ADR 引 cocos 的多点缺陷。未新增 `InputCancelReason` 枚举值：`FocusLost` 语义
  已足（Android 收回手势即窗口不再接收），新增只会是无行为差异的公开契约变更。
- **证据：** `tina_platform_android_tests` **27/27 在 `emulator-5554`（Android 36 x86_64）实机通过**。两条
  不变量已 revert 验证会失败：重新引入「重复 Down 返回 InvalidSlot」，以及把 cancel 改成窗口级。桌面
  `tina_tests` 448/448、`tina_render_bgfx_tests` 114/114、`tina_platform_glfw_tests` 53/53 无回归。

- **surface 重建已接线，并修好一处「已发布但无生产者」的契约（2026-08-29）。** ADR 0034 的
  `RenderSurfaceState::nativeBindingRevision` 自落地起**从未被任何生产代码赋值** —— 全仓库只有
  `tests/render/NullRenderDeviceTests.cpp` 在写它，`EngineHost::toRenderSurfaceState()` 干脆没有转发这个
  字段，且 `Integration::WindowSurfaceSnapshot` 连该字段都不存在。也就是说整条 rebind 路径此前只能从测试
  到达。本轮补齐：snapshot 新增字段、EngineHost 转发、Android 后端递增。这正是本仓库反复出现的形态 ——
  契约写下了，实现边界却没跟上。
- **C3/C6 走 Android 专属接口 `IAndroidPlatformBackend`，而非新增 `IPlatformBackend` 纯虚。** 理由是没有
  任何桌面后端能有意义地实现它们：加到通用接口会迫使 GLFW、Headless 与**每个测试替身**都实现一个只能返回
  失败的方法。宿主用 `dynamic_cast` 取得该 facet。这也让 D7 的顾虑消失 —— 原本担心的正是「新增纯虚会波及
  全部后端」。
- **C6 的形状：请求是意向，遮挡由宿主上报。** `requestShow/HideSoftKeyboard()` 只**latch 意向**，因为只有
  Java 能调 `InputMethodManager`；读取（`pendingSoftKeyboardRequest()`）**不清空**，由宿主在 IME 调用真的
  成功后用 `acknowledgeSoftKeyboardRequest(request)` 显式清除，故一次请求恰好产生一次生效的 IME 调用，而不
  是每帧重复施加。**此处原先写作「读取即清空」，与实现不符，已更正**：consume-on-read 会在
  `InputMethodManager` 缺席、或 view 尚无 window token（`hideSoftInputFromWindow` 需要它）时把意向永久丢掉，
  现场表现是「键盘就是不弹出来」而没有任何错误。acknowledge 还要求**与 latch 中的值仍然一致**才清除，否则一个
  更新的反向请求会被旧读取的迟到 acknowledge 抹掉。键盘遮挡高度**必须由宿主上报**（`onSoftKeyboardOcclusionChanged`，物理
  像素）而不能由引擎推算：Android 的 IME 高度取决于键盘应用、语言、是否显示候选条与分屏几何，猜测会把
  聚焦输入框错位任意距离。读取侧 `softKeyboardOccludedLogicalHeight()` 返回 window-logical，供 UI 直接
  相减 —— 没有它，聚焦的文本框会躺在键盘背后且无从得知需要滚动。高于窗口的遮挡值**拒绝而非 clamp**：那
  意味着宿主与后端量的是不同几何，clamp 会掩盖该分歧。**`updateTextInputPlacement` 仍然拒绝** —— caret
  placement 是 IMM32 那类 IME 定位协议，Android 没有对应物，与「显示/隐藏键盘」是两件事，实现了后者不等
  于实现了前者。
- **窗口丢失释放全部手指。** `onNativeWindowDestroyed()` 逐个清空 pointer：被任务切换打断的拖拽绝不能留下
  滞留手指，那正是 cocos2d-x 把手指卡到进程退出的成因。该方法幂等（Android 在 teardown 时会在没有前置
  INIT 的情况下投递 TERM）。窗口替换要求三个 revision **同时**推进，因为 tracker 拒绝「binding 变了但
  surface/metrics revision 没变」的提交 —— 否则后端会在两个不同帧里分别观察到 rebind 与它该 reset 到的
  几何。几何校验与创建路径**共用同一个函数**：rebind 若接受工厂会拒绝的几何，就是一个直通 `bgfx::reset`
  的洞。
- **证据：** `tina_platform_android_tests` **38/38 在 `emulator-5554`（Android 36 x86_64）实机通过**（+11）。
  两条不变量已 revert 验证会失败：去掉 `++nativeBindingRevision`、去掉窗口丢失时的 `releaseAllPointers()`。
  桌面 `tina_tests` 448/448、`tina_render_bgfx_tests` 114/114、`tina_platform_glfw_tests` 53/53、
  `tina_runtime_ui_tests` 151/151、`tina_ui_render_integration_tests` 32/32 无回归；`tina_sample_2d` 300 帧
  `status=ok`、`evidenceFingerprint=488c124f…` 未变。

- **JNI 桥与可安装 APK 已落地，Android 上首次真正运行（2026-08-29）。** `android/` 是 AGP 工程，产出装得上
  的 APK；`src/platform/android/jni/` 是唯一出现 Java 名字的地方，且**不含引擎逻辑** —— 每个入口都转发进
  `tina_platform_android`，故槽映射、环形缓冲与生命周期规则只有一份实现且可在无 JVM 环境下测试。
- **用 `RegisterNatives` 而非 name mangling。** platform-input.md 记载 cocos2d-x 只靠符号修饰导出、并用字符串
  查反向调用的类名，而那个类只存在于可选模块里，默认工程调用即失败且无诊断。显式注册改为在
  `System.loadLibrary` 时就失败并点名签名不匹配的方法，故 Java/C++ 签名漂移不可能静默出货。
- **跨语言枚举的权威在 C++。** Java 侧只有整数常量，C++ 侧 `static_assert` 钉住那四个值，并且**拒绝**未知值
  而不是强转 —— 强转会把 Java 侧的改动变成越界枚举，进而破坏下游 pointer 状态。这正是 lesson 5 要求的
  「单一权威来源 + 编译期校验」。
- **Java 侧的多点事件拆分有两个不对称，写错任一个都是静默缺陷：** `ACTION_POINTER_DOWN/UP` 必须**只**取
  `getActionIndex()` 那一个 pointer（该事件携带全部活动 pointer，全报一遍会把已按下的手指重复按下），而
  `ACTION_MOVE` **没有** action index、一次报告全部 pointer，必须全部转发（否则除第一根以外的手指会卡住
  不动）。
- **实机验证（`emulator-5554`，Android 36 x86_64）：** APK 安装并启动，进程存活、窗口获得焦点、
  **`-Xcheck:jni` 零警告**；注入真实 tap 与 swipe 后零故障；**旋转**与**后台/前台循环**后进程仍是同一个 pid
  且零故障 —— 即 surface 销毁后重建的 rebind 路径**在真机上实际跑通**，这是 ADR 0034 首次端到端闭环。
  `tina_platform_android_tests` 38/38 同轮通过；桌面 `tina_tests` 448/448、`tina_platform_glfw_tests` 53/53
  无回归。

- **渲染器已接进 APK，引擎首次在 Android 上画出画面（2026-08-29）。** 截图分析确认主色为 RGB(16,42,67)
  （1134/1300 采样点），即 bgfx 的清屏色，而非 Android 的默认黑或白。**旋转**与**后台/前台循环**后画面均恢复
  为同一颜色且零错误 —— 至此 ADR 0034 的 GPU 侧重建（`bgfx::reset` 对真实 `ANativeWindow` 替换）也验证完毕。
  渲染器通过 `-Ptina.shaderc=<宿主 shaderc 路径>` 可选启用，不给则 APK 仍可装可跑、只是画面空白。
- **真机暴露了两个模拟器/单测都看不见的缺陷，值得单独记下。** 二者的症状**完全相同**：屏幕变黑，无崩溃、无
  日志。是加了一行结构化错误日志才定位的 —— 这类失败在视觉上与「清屏色恰好是黑」不可区分。
  ①**`surfaceRevision` 一次跳了 2。** Android 在后台/前台循环里**背靠背**投递 TERM_WINDOW 与 INIT_WINDOW，
  中间没有任何 poll；原实现在每个生命周期回调里各加一次，于是 `RenderSurfaceStateTracker` 以
  「revision must advance exactly once for each committed state change」拒掉了 resume 之后的**每一帧**。修法
  是让计数器跟踪**被观察到的状态**而非事件：两次观察之间的多次变化合并为一步，观察点就是 `pollFrame()` 完成
  时（放在这里而不是让宿主显式调用，宿主就无法忘记）。已补单测 revert 验证。
  ②**被拒绝的 submit 不能推进 frameIndex。** device 要求索引连续，原实现用 `frameIndex++` 传参，于是一次
  瞬时失败会让其后**每一帧**都以「frame indices must be contiguous」被拒 —— 一个瞬态错误变成永久故障。
- **模拟器的 Vulkan 驱动不可用，这不是引擎缺陷。** SDK 模拟器的 `vulkan.ranchu.so` 在
  `SetDebugUtilsObjectNameEXT` 内 SIGSEGV（栈顶就是它，第二帧才是 bgfx 的 `SwapChainVK::createSwapChain`）。
  故 `RendererApi` 需可选择：真机保持 Automatic（Android 上偏好 Vulkan），模拟器按 `Build.HARDWARE` 含
  `ranchu`/`goldfish` 自动改用 GLES。这正是 `RenderDevice.hpp` 那条「GLES 变体作为 Vulkan 驱动不可用时的
  回退」注释所预留的情形，本轮第一次真的用上。

- **EngineHost 已接进 APK：真实引擎相位在 Android 上运行（2026-08-29）。** JNI 不再直接驱动 RenderDevice，
  而是持有 `EngineHost` 并每帧调 `tick()` —— 这正是 D3 的落地形态：引擎不拥有循环，Android 自己的节拍说话。
  实机日志 `frameUpdates=300 fixedUpdates=314`：两个计数**刻意不同步**，因为固定步长累加器走的是独立时钟，
  而这恰是它从阻塞 `run()` 迁到外部驱动 `tick()` 后仍然正确的证据。
- **`EngineHost::Create()` 会立刻运行平台工厂，所以 host 必须在首个 surface 之后才能创建。** Android 的窗口是
  异步交付的，在 `surfaceCreated` 之前没有任何东西可以构造后端。这个不对称正是 D3 选择外部驱动而非阻塞
  `run()` 的原因，本轮第一次在实现层面被迫面对。
- **EngineHost 不暴露它的后端（刻意封装），故 Android 生命周期需要一个观察指针。** 该指针在平台工厂内部捕获
  —— 那是唯一同时能看见具体类型与所有权交接的位置。
- **第三处 revision 缺陷：Runtime 侧的校验漏了 `nativeBindingRevision`。** `RenderSurfaceStateTracker` 早已把
  binding 变化并入它的 facts 比较（`|| nativeBindingChanged`），但 `EngineHost` 的同名校验没有 —— 于是**几何
  不变的窗口替换**（Android 以相同尺寸回到前台时恰好如此）让 revision 前进了、而该校验仍认为「什么都没变」，
  从而拒绝 resume 之后的每一帧。两处校验必须一致，否则同一次 rebind 对一方合法、对另一方非法。已补桌面
  单测 `AcceptsANativeBindingReplacedAtUnchangedGeometry` 并 revert 验证。
  这是本轮第三个**同一类**缺陷（前两个见上）：都只在真机的后台/前台循环里出现，症状都是「画面停住、无崩溃」。
- **顺带修一个日志洪水：** Java 侧未对「引擎已终止」加 latch，一次后台/前台循环产生了 **50 万条**相同日志。
  终态是永久的，所以必须停掉帧循环而不是继续每帧重试。

- **UI 内容已渲染：引擎在 Android 上画出了真实的 Retained UI（2026-08-29）。** 游戏状态在 `updateUI` 里发布
  一棵 UI 树（一个百分比尺寸面板 + 一个每 60 帧换色的子面板），像素与引擎状态**逐次对应**：`pulseOn=false` →
  RGB(60,190,120)、`pulseOn=true` → RGB(220,90,40)，八次连续采样全部命中。动画是刻意的 —— 静态面板在
  「引擎在跑」与「引擎卡死」两种情况下截图完全相同，而那正是本周两次调试的失败形态。
- **`UIRootOwner` 必须在 `onEnter` 里经 root builder 创建，不能只声明。** 默认构造的 owner 没有 root，
  `primaryWindowUITreeUpdater` 会以「UI tree updater requires a root owner」拒绝。root 还必须显式设为
  100% 尺寸，否则百分比子元素相对 auto 尺寸父节点解析、塌成零 —— 症状只是「面板没出现」。
- **第四个真机独有缺陷：旋转时 `SIGABRT`。** 中止信息是
  `FORTIFY: pthread_mutex_lock called on a destroyed mutex`，栈顶 `Surface::hook_query`。根因是
  **bgfx 有自己的渲染线程且仍持有那个 `ANativeWindow`**：surface 挂起只让 `submitFrame` 跳过绘制，但它仍
  `pumpRetirementOnlyFrameIfNeeded()` → `bgfx::frame()`，那个线程会去 query window。此前
  `surfaceDestroyed` 立即 `ANativeWindow_release`，放掉最后一个引用，下一次 query 就整进程中止。修法是
  **不在 surfaceDestroyed 里释放**，而是持有到替换窗口到达（rebind 成功后再放旧的）或 session 销毁
  （那时先拆 render device）。Java 的 Surface 对象照常死亡，这只是让 native handle 多活一会儿 ——
  `ANativeWindow_acquire` 本就是为此存在的。
  这是本轮**第四个**只在真机出现的缺陷，也是唯一一个会崩进程的；前三个都表现为「画面停住、无崩溃」。

- **触摸闭环已完成：手指改变了画面（2026-08-29）。** UI 注册了 routed pointer listener，按住面板会让子面板同时
  换色并放大（50% → 90%）。像素证据：空闲绿色采样 0、**按住 11242**、松开 0；`presses` 与 `releases` 始终相等，
  `droppedTouches=0`。至此整条链闭合：手指 → Java `MotionEvent` → JNI → 无锁环形缓冲 → `PlatformFrame` →
  UI 路由 → 游戏代码 → 画面。**同时改颜色与尺寸**是刻意的：只改颜色在截图里难与那个 1 秒周期的动画区分。
- **两条多点规则在此复用而非重新发明：** 第一根手指 latch 视觉、第二根不得重新触发（否则就是 ADR 引 cocos2d-x
  的「第二根手指抢走第一根的控件」）；释放按 pointer 作用域，另一根手指抬起不得解开这一根。子面板设为
  `Ignore` 命中策略，避免它从父面板抢走触摸 —— 与屏上摇杆对旋钮的做法一致。
- **第五个真机缺陷：`LAUNCH_MULTIPLE` 造出第二个 engine。** Activity 默认允许多实例，于是从启动器再次启动会
  产生**第二个 `onCreate`（且没有 `onDestroy`）**，第二个 session 试图再初始化 bgfx —— 而 bgfx 是进程级单例，
  于是 `EngineHost::Create` 以「bgfx failed to initialize」失败，app 冻结且**没有任何其他症状**。修法是
  manifest 声明 `android:launchMode="singleTask"`。诊断手段是在 `onCreate`/`onDestroy` 打印 session 句柄：
  两次 create 之间没有 destroy 就是该缺陷的指纹，此前绕了数轮都在猜。
- **同时记下一个把我误导的测试陷阱：** 用 `adb shell input swipe ... &` 加 job control 会在 adb 会话结束时
  **杀掉被测 app**（logcat 显示 `exited cleanly (3)` 与 `Remote process closed the socket`）。那看起来像引擎
  崩溃，实际是测试方法。正确做法是把 `&` 放进设备端 shell：`adb shell 'input swipe ... & sleep 1; screencap'`。

- **按键输入已完成（2026-08-29）。** 实机 `keys=3`、`droppedKeys=0`，且按键期间绿色采样 1458、松开后 0 ——
  按键与触摸产生**同一个**视觉结果（一个状态驱动呈现，而非两条路径）。链路：Android `KeyEvent` → Java →
  JNI → 无锁环形缓冲 → C++ 侧映射 → `PlatformFrame` → `InputAction` → 游戏代码。
- **跨语言键映射的权威**完全在 C++（lesson 5 的直接落实）：Java 侧**没有任何键表**，只把 Android 原始
  `KEYCODE_*` 原样传过来，`androidKeyFromKeyCode()` 是唯一映射点。字母/数字/功能键三段连续区间按范围映射，
  并用 `static_assert` 钉住 Tina `Key` 枚举的连续性 —— 手写 26 个字母 case 正是这个函数要防的转写错误，而
  枚举重排会静默错位每一个字母。未映射的键码返回 `Key::Unknown` 并被**丢弃**，不发布：`Unknown` 对消费者
  永远不可操作。
- **两个键语义决定：** Android 的 `BACK` 映射到 `Escape`（它就是"取消/返回"，与 Escape 同义；新增移动端专属
  枚举会迫使每个消费者处理同一意图的两种拼法），`DPAD_CENTER` 映射到 `Enter`（D-pad/遥控器的确认键）。
  `DEL`→`Backspace` 与 `FORWARD_DEL`→`Delete` 有专门测试，因为写反是经典转写失误、且会让文本编辑删错方向。
- **未被引擎映射的键交还系统**：`onKeyDown` 只在 native 侧确实消费时返回 true，否则落回 `super`。吞掉
  `BACK` 会把用户困在 app 里，吞掉音量键会破坏设备自身控制。
- **窗口丢失同时释放按键与手指**：按下时窗口消失则永远收不到 Up，留着位就把它 latch 到整个 run 结束 ——
  与滞留手指同一失败模式。
- **触摸/按键两个环形缓冲共用一份实现**（`AndroidEventQueue<Event, Capacity>` 模板）。这里唯一复杂的是内存
  序，而写两遍正是第二份出错的方式。
- **与 cocos2d-x 的对照（2026-08-29 读源码确认）：** cocos 的 6 个 `AndroidManifest.xml` **全部**使用
  `android:launchMode="singleTask"`，与本轮为修第五个缺陷所做的选择一致 —— 这条不是本项目特有，是 Android
  上持有进程级图形状态的通用要求。但 cocos 还加了
  `android:configChanges="orientation|keyboardHidden|screenSize"`，那是**阻止**旋转时重建 activity，即绕过
  问题而非解决它；Tina 刻意不加，因为让 Android 正常重建才会真正走 ADR 0034 的 surface rebind 路径。

- **文本输入已完成（2026-08-29）。** 实机 `textCommits=8`、`droppedKeys=0`，`dumpsys input_method` 确认
  `mInputShown=true` 且 `mServedView=dev.tina.TinaSurfaceView` —— 软键盘真的服务于引擎的 view。链路：软键盘 →
  `InputConnection.commitText` → JNI → 环形缓冲 → 严格 UTF-8 校验 → `TextInputTransition`。
- **文本必须走 `InputConnection`，不能靠按键。** 软键盘**根本不产生 key code**，它通过 `commitText` 交付整串
  （自动补全、粘贴、IME 转换都是一次调用）—— 只处理 `onKeyDown` 的宿主在用户打字时什么都收不到。
  `onCheckIsTextEditor()` 必须返回 true，否则 IME 从不索取 `InputConnection`，键盘会弹出来但打字毫无反应。
- **事件自持字节，不引用 Java 字符串。** `AndroidTextEvent` 内联 256 字节：`string_view` 会指向一个在 JNI 调用
  返回瞬间就失效的 Java 字符串，而消费者在另一个线程、可能数帧之后才读。校验放在**生产端**（JNI 转换处），
  因为那是唯一能把失败报告给始作俑者的位置。
- **校验复用引擎自己的 `countStrictUtf8CodepointsWithoutNul`**，不另写一份：`PlatformFrameBuilder` append 时
  施加的正是同一条规则，而两份实现终会分歧 —— 届时被拒的是**整帧**而非这一个 commit。
- **emoji/星平面文本已支持（2026-08-29，上一轮记录的限制已解除）。** 原因是必须用 `GetStringChars` 而**不是**
  `GetStringUTFChars`：后者返回 **modified UTF-8**，NUL 编成两字节且非 BMP 字符以 CESU-8 代理对到达 —— 一个
  emoji 变成两个非法的三字节序列，被严格校验器拒绝、字符静默丢失。UTF-16 才是 Java 实际存储的形式，从它转换
  是唯一能让这些字符完整通过的路径。新增 `Core::convertUtf16ToStrictUtf8()` 放在 Core 而非 Android 侧，因为
  这是「平台 IME 说 UTF-16、Tina 契约要严格 UTF-8」的通用缺口，Windows adapter 将来同样需要。转换只产最短形式
  （否则自己的输出会被下游校验器拒绝），未配对代理、内嵌 NUL、输出溢出一律返回 nullopt 而非截断 —— 截断出的
  半个多字节字符本身就是非法 UTF-8。实机 `textCommits=1` 确认 U+1F600 通过（此前会被拒）。
  验证手段也记录在案：非 ASCII **无法**从测试工具注入（`adb shell input text` 按 keycode 合成、表达不了代理
  对；驱动真实 IME 需要人），故 APK 提供 `--ez tina.commitEmoji true` 走**与真实键盘同一条** `commitText`
  路径，而不是绕过转换去伪造结果。
- **`deleteSurroundingText` 译成 Backspace 按键**而非文本编辑：引擎在此没有可删除的 editable buffer，而按键
  路径是它的 TextEdit 消费者已经处理的东西。
- **BACK 键由 Java 侧无条件券走，退出应用因此成了引擎的责任（2026-08-30 更正）。** 原先「未消费的键交还
  系统」这条对 BACK **不成立**：`nativeOnKey` 的返回值只表示「入队成功」，而按键是异步跨线程入队、下一帧
  才被处理 —— `onKeyDown` 必须立刻作答的时刻，引擎还根本没看到它。于是 BACK **两件事同时发生**：实测
  gallery 里按一次返回，场景 pop 了，activity 也回了桌面。修法是 Java 对 `KEYCODE_BACK` 一律返回 true。
  代价必须说清：**没有上一层可回的 State 必须自己调 `requestExitAfterFrame()`**，否则 BACK 什么都不做、
  用户困在应用里。这不能由平台层兜底 —— 只有游戏知道自己是不是最外层。
- **一个按键切换软键盘**，因为手机没有硬件键：游戏 latch 意向、宿主执行（只有 Java 能调 `InputMethodManager`），
  且必须交替 —— 只有一个键可用，不交替就无法收起键盘。

### C6 补充（2026-08-30）：preedit 组词文本与 caret placement

上一轮记录「`setComposingText` 刻意不转发」，理由是 Android 的 `InputConnection` 与 Tina 的四阶段模型不直接
对应，而 C6 拒绝伪造阶段。**该限制现已解除**：不直接对应不等于无法映射 —— 拒绝伪造的是「存下 marked text
却什么都不发」，而不是「先定清楚映射再发」。中文/日文输入法完全依赖它：不转发时用户在整个转换过程中屏幕上
什么都看不到，直到最后一次 `commitText` 才突然出现结果。

**映射表**（唯一权威是 C++ 的 `AndroidCompositionSession`，Java 侧不持有任何 composition 状态）：

| Android 调用 | 条件 | 发出 |
| --- | --- | --- |
| `setComposingText(非空)` | 无 active session | `Started`，preedit = text |
| `setComposingText(非空)` | 有 active session | `Updated`，preedit = text |
| `setComposingText(空/null)` | 有 active session | `Cancelled` |
| `setComposingText(空/null)` | 无 active session | **什么都不发** |
| `finishComposingText()` | 有 active session | `Cancelled` |
| `finishComposingText()` | 无 active session | **什么都不发** |
| `commitText(text)` | 有 active session | `Ended`，随后 `TextInputTransition(text)` |
| `commitText(text)` | 无 active session | 只发 `TextInputTransition(text)`（既有行为不变） |
| 窗口销毁 | 有 active session | `Cancelled`（下一次 poll 发布） |

四个取舍，每条都有具体代价：

- **空 preedit 与 `finishComposingText` 都映射 `Cancelled` 而非 `Ended`。** Android 根本没有 cancel 调用，
  删空拼音与放弃组词是同一个调用。选 `Ended` 在 UI 侧行为上无差别（`UIContextText.cpp` 两个 stage 走同一
  分支），但 `UIInputRouteProducer.cpp:94` 把 `Cancelled` 排除在 flow device observation 之外 —— 也就是
  「放弃组词」不该算一次用户设备活动，而「组词提交了」该算。选错会让删空拼音也去点亮 flow device 指示。
- **两条「什么都不发」是刻意的。** 输入法在取得/交还组词区时会例行发这两个调用，宣告一个从未开始的组词结束
  是噪声，每个消费者都得自己过滤。
- **`commitText` 必须先 `Ended` 再 `TextInputTransition`，因此 commit 与 preedit 走同一条队列。** 顺序反了
  **也能工作**（`routeTextInput` 自己会 `clearImeComposition()`），这正是危险处：帧里的事件序列会变成
  「文本先出现、组词后结束」，任何按 stage 重建 IME 状态机的消费者都会错。两条独立队列**无法**表达这个先后
  关系 —— 它们会被依次排空，于是每个 commit 都落在产生它的那一遍组词的所有 stage 之前。与 IMM32 host 的既有
  顺序一致（`GlfwPlatformBackend.cpp` 先 composition 再 text）。
- **光标夹取而非拒绝，且必须从 UTF-16 code unit 换算成 codepoint。** Android 的 `newCursorPosition` 是相对
  偏移，可以为负、可以越界（输入法常传 1 表示「文本之后」，不管文本多长）。拒绝会丢掉合法 preedit；而越界值
  会让 `PlatformFrameBuilder` 拒绝**整帧**而非这一个 transition。换算是必需的：一个代理对是两个 code unit
  但一个 codepoint，preedit 里有 emoji 时两个计数就会分歧（输入法预测 emoji 时会先放进组词区）。
- **preedit 容量 512 而非 commit 的 256。** 两个独立理由：中文/日文组词串本就比一次 commit 长；且
  `UI::Detail::UIImeCompositionState::MaximumPreeditBytes` 就是 512，后者是硬约束 —— 接受超过 UI 能容纳的
  preedit 会让后端发布出去、`routeTextComposition` 再以 `CapacityExceeded` 拒绝，代价是整帧而非这一个事件。
- **窗口销毁必须取消在飞的组词，且在下一次 poll 才发布。** 丢失 surface 时 Android **不会**发任何
  `InputConnection` 调用，所以不显式取消的话 UI 会一直画着输入法早已忘掉的 preedit，而下一遍组词会对一个
  消费者从未见过开始的 session 报 `Updated`。之所以不在事发处直接 append：那条路径运行在帧之外，对未
  `beginFrame` 的 builder 追加是失败。

**`updateTextInputPlacement` 从「拒绝」改为 latch —— 这修的是一个会让帧循环永久停止的缺陷。**
原实现对任何非空 placement 返回 `InvalidArgument`，当时是诚实的（没有可交付的 IME 集成）。但
`EngineHost` 每帧无条件 publish caret，而 caret 只在 TextEdit 聚焦时才存在，协调器把这个拒绝升级成
`LifecycleInvariantViolation`，`tick()` 走终止路径并被 latch —— **应用在文本框获得焦点的那一刻就停止产帧**，
现场唯一的线索是那个 caret。此前没暴露只因为 demo 里没有 TextEdit。

Android 真正的 caret 协议是 `CursorAnchorInfo`，与 IMM32 形状完全不同：**候选窗属于输入法进程，应用侧没有
窗口可以定位**，只能上报几何。因此与软键盘 show/hide 同构 —— 引擎 latch，宿主执行（只有 Java 能构造
`CursorAnchorInfo`）。两个细节：读取 caret **不清除**（caret 是持续状态而非一次性意向，输入法每次索取都要
拿到当前值，consume-on-read 会让第二次索取拿到空），且只在输入法通过 `requestCursorUpdates()` 主动索取后
才上报（Android 的契约是请求/上报成对，无条件每帧上报是白付一次 JNI 调用加一次对象分配）。

索取状态**按位存储而非布尔**：`requestCursorUpdates(mode)` 的 `CURSOR_UPDATE_IMMEDIATE`(1) 与
`CURSOR_UPDATE_MONITOR`(2) 语义不同 —— 前者只要一次上报，后者要持续到取消（mode 0）。压成一个 bool 会让
一次性请求变成永久每帧上报。因此 JNI 面是 `nativeSetCursorUpdateMode(int)` / `nativeCursorUpdateMode()`，
IMMEDIATE 位由宿主在**上报真的送达输入法之后**调 `nativeAcknowledgeImmediateCursorUpdate()` 退位，与软键盘
意向同一个理由：这一帧可能没有聚焦 caret、或拿不到 `InputMethodManager`，读取即清会把唯一那次上报吞掉。
两个常量值取自框架自身（`InputConnection` 原样把 mode 交过来），故本工程复述它们时值必须一致；已实测
android-36 的 `CURSOR_UPDATE_IMMEDIATE == 1`、`CURSOR_UPDATE_MONITOR == 2`。框架日后新增的其他位在 native
侧被掩掉，`requestCursorUpdates` 也只对**本工程真的实现的位**返回 true —— 声称支持一个没写实现的模式，比
不声称更糟。
非法几何（非有限、高度非正、窗口不匹配）**拒绝而非夹取**：替换成一个看似合理的矩形会把候选窗放到任意位置，
且现场无从追溯。

**验证手段与其边界。** 组词过程**无法**从测试工具注入 —— `adb shell input text` 合成 keycode，根本不携带
组词区。故 APK 提供 `--ez tina.composeText true`，走**与真实键盘完全同一条** `setComposingText` 路径分三步
注入 `ni`→`nihao`→commit`你好`，每 30 帧一步（TextEdit 需要一个已提交的帧才可聚焦，且分帧才能让 preedit
在屏幕上停留足够久、各 stage 落在不同 poll）。这能证明整条链，**不能**证明某个具体输入法的行为。
**候选窗是否真的跟随光标未经验证**：某个输入法是否调用 `requestCursorUpdates` 不由本工程决定，模拟器默认
输入法是否调用也未确认；该项需要一个会索取 cursor updates 的中文输入法加人眼，如实记录为未验证。

**落地时发现的两处只有真机能暴露的问题**（都不是单测能到达的）：

- `CursorAnchorInfo.Builder.build()` 在**设了位置却没设 matrix** 时抛
  `IllegalArgumentException("Coordinate transformation matrix is required when positional parameters
  are specified")`。它杀掉的是跑帧循环的 Handler Runnable，所以现场是「进度行停了、`Tina` tag 里什么都
  没有」，读起来与原生挂死一致 —— 只有 `logcat -s AndroidRuntime:E` 能定位。且这条路径**只有真实输入法
  索取 cursor updates 时才走到**，比任何脚本化诊断都晚。修法是给 view-local 坐标配一个 view-to-screen
  matrix，而不是先把坐标偏移到屏幕空间：matrix 由输入法自己应用，所以之后 surface 被滚动或变换时仍然正确，
  而预先烤进去的偏移会静默失效。
- **加一个聚焦的 TextEdit 会让 demo 的按键证据静默归零。** 聚焦的 TextEdit 消费**除 Tab / Enter / Escape
  以外的每一个键**（`UIInputRouteProducer` 的 `imeFocus().hasValue()` 分支是一条总括规则，不是逐命令
  查表），而 UI consume 会压制 gameplay action —— 完全正确的行为，现场却与「按键桥坏了」一模一样。所以
  demo 把 Enter 也绑进同一个 action：能在有文本框聚焦时继续作为证据的只有那三个键。

因此移动端剩余的真实工作是：**在真实设备上跑 Vulkan 路径**（模拟器的 Vulkan 驱动不可用，只能验证 GLES）、
**候选窗跟随的人工验收**，以及 iOS。

## 被拒绝方案

- **先写后端再补契约**：会产出只能发 `PrimaryPointerId` 的移动后端，重演 cocos 默认模板单点触控的处境，
  并把公开契约与测试的修改推到之后；
- **把 surface 销毁压成 `suspended`**：语义错误，GPU 资源确实已失效；
- **让移动端伪造 preedit 阶段**：cocos 的 `setMarkedText:` 正是这样，存下 marked text 却什么都不发，
  因为它的抽象接口没有 preedit 概念——伪造只会得到同样的空壳。**注意这条拒绝的是"存下却不发"，不是"映射"**：
  2026-08-30 的 C6 补充给出了显式映射表并落地，见上文；
- **让 Java 侧持有 composition 状态**：那就是第二份 stage 语义，与 C++ 的那份终会分歧，而后果是 `Started`
  发两次或 `Ended` 从不发 —— 后者会让 preedit 永久留在屏幕上。与按键表同一条理由（lesson 5）；
- **把 commit 与 preedit 放两条队列**：无法表达 `Ended` 必须先于它产生的文本，因为两条队列会被依次排空；
- **`updateTextInputPlacement` 继续拒绝非空值**：它会在 TextEdit 获得焦点时终止整个帧循环，见上文 C6 补充。
  「诚实地拒绝一个未实现的能力」在这里的实际代价是应用冻结；
- **保留 `cmake/ShaderUtils.cmake` 备用**：本仓库风格是直接删除不用的路径（AGENTS.md），而一个假装支持
  Metal/GLES 的死文件比没有更糟；
- **同时开工 Android 与 iOS**：iOS 额外背着 `run()` 形态冲突，两条一起做会让 D3 在没有证据时被草率决定。
