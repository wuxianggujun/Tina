# GoogleTest 与验证

## 规则

- 测试框架固定为 GoogleTest；
- `TINA_BUILD_TESTING=ON` 时 CMake 生成基础 `tina_tests`；M7-C1a UI 树核心另有独立
  `tina_ui_tests`；启用
  `TINA_BUILD_PLATFORM_GLFW` 时另外生成 `tina_platform_glfw_tests`，启用
  `TINA_BUILD_RENDER_BGFX` 时另外生成 `tina_render_bgfx_tests`，不注册额外测试调度；
- 构建完成后直接运行对应 GoogleTest executable，任一返回码非0即失败；
- Visual Studio 多配置构建把测试运行时隔离到 `bin/<Config>`，禁止 Debug/Release GTest DLL 共用目录；
- 同一 Visual Studio build tree 的 Debug/Release 构建串行执行，禁止并发启动两个 MSBuild 门禁；
- 测试依赖由固定 vcpkg baseline 提供；
- 测试日志不得包含路径外的敏感环境变量或凭据。

## 已验证基线

当前迁移结果对应 2026-07-17 的 `codex/tina-vnext-runtime`：

| 平台 | 构建图 | 配置 | GoogleTest | 状态 |
| --- | --- | --- | --- | --- |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | vNext M6-A/M7-A/M7-B1/M7-B2/M7-C1a：Core/Platform/Input/Task/Render/Runtime/UI、WindowSurface handoff、Desktop bootstrap、真实 bgfx backend | Debug C++23 | 183/183 | 通过；GLFW 22/22、bgfx 11/11、UI 16/16 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 | vNext M6-A/M7-A/M7-B1/M7-B2/M7-C1a：Core/Platform/Input/Task/Render/Runtime/UI、WindowSurface handoff、Desktop bootstrap、真实 bgfx backend | Release C++23 | 183/183 | 通过；GLFW 22/22、bgfx 11/11、UI 16/16 |
| Windows 11 / MSVC 19.50 | Legacy ON 与 vNext M6-A 共存构建 | Debug C++23 | 135/135 | 通过 |
| Ubuntu 22.04 / GCC 13.4 | vNext M6-A/M7-A/M7-B1/M7-C1a：X11 WindowSurface handoff + UI tree core，Legacy/真实 bgfx backend 关闭 | Debug C++23 | 183/183 | 通过；UI 16/16，无诊断 |
| Ubuntu 22.04 / Clang 22.1.8 + libstdc++15.2 | vNext M6-A/M7-A/M7-B1/M7-C1a：X11 WindowSurface handoff + UI tree core，ASan/UBSan/LSan，基础测试无 suppression | Debug C++23 | 183/183 | 通过；UI 16/16，无诊断 |

GLFW adapter 和 bgfx adapter 测试是独立 executable，不能把多个进程伪写成单个合并测试数。当前测试拓扑为：

| 构建图 | 基础 GoogleTest | GLFW 专项 GoogleTest | bgfx 专项 GoogleTest | 状态 |
| --- | ---: | ---: | ---: | --- |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 Debug | 183/183 | 22/22 | 11/11 | 通过；UI 16/16、Null样例300帧、WindowSurface GLFW样例300帧、真实 D3D11 Intel Iris Xe Desktop样例默认300帧返回0 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 Release | 183/183 | 22/22 | 11/11 | 通过；UI 16/16、Null样例300帧、WindowSurface GLFW样例300帧、真实 D3D11 Intel Iris Xe Desktop样例默认300帧返回0 |
| Windows 11 / MSVC 19.50 / CMake 4.2.3 production-style | 测试 target 关闭 | 不构建 | 不构建 | `TINA_BUILD_TESTING=OFF`，GLFW样例300帧返回0 |
| Ubuntu 22.04 / GCC 13.4 + GLFW X11 | 183/183 | 22/22 | 未运行 | 通过；UI 16/16，无诊断；Null样例300帧、WindowSurface GLFW样例300帧返回0 |
| Ubuntu 22.04 / Clang 22.1.8 + libstdc++15.2 + GLFW X11 + ASan/UBSan/LSan | 183/183 | 22/22 | 未运行 | 通过；UI 16/16，无诊断；基础测试无 suppression，Null/GLFW样例各300帧；`_XimOpenIM` 精确 suppression 仅专项命中12次/4896 B、GLFW样例命中1次/408 B |
| Ubuntu 22.04 / GCC 13.4 + GLFW X11/Wayland 双后端 | 183/183 | 22/22 | 未运行 | 通过；嵌套 Weston 9 强制 Wayland 与 Xvfb 强制 X11 均通过基础、专项与300帧样例 |
| Ubuntu 22.04 / Clang 22.1.8 + libstdc++15.2 + GLFW X11/Wayland 双后端 + ASan/UBSan/LSan | 183/183 | 22/22 | 未运行 | 通过；基础测试无 suppression且Null样例300帧；Wayland专项与样例 suppression 命中0，X11专项命中12次/4896 B、样例命中1次/408 B |

Windows 与 Linux 当前都是“183项基础测试 + 22项GLFW专项测试”，两个 executable 均已实际返回0。
Windows bgfx 构建另有独立 `tina_render_bgfx_tests` 11/11，Debug/Release 均实际返回0；该结果
只覆盖当前 clear-only bgfx core、factory/lease 回滚和 Desktop smoke，不覆盖后续
Scene/UI/Pass Scheduler/submission ticket，也不声明 resize、最小化、恢复的真实自动化通过。
M7-C1a UI 树核心使用独立 `tina_ui_tests`。当前 Windows 11 / MSVC 19.50 Debug/Release
均直接 GoogleTest 16/16 通过；Linux GCC 13.4 与 Clang 22.1.8 + libstdc++15.2
ASan/UBSan/LSan 均基础 `tina_tests` 183/183、`tina_ui_tests` 16/16，且无诊断。
X11 在隔离 X server 下运行。GCC Wayland 门禁由 Xvfb 托载
Weston 9 `x11-backend` 并提供 `wl_seat`；移除 `DISPLAY` 后断言
`glfwGetPlatform() == GLFW_PLATFORM_WAYLAND`，再运行专项测试和300帧样例。同一双后端产物
还在移除 `WAYLAND_DISPLAY` 后由 Xvfb 强制 X11 复验22/22与300帧。配置/构建成功不能冒充窗口测试通过。

纯 Weston headless 在不提供 `wl_seat` 时会触发项目锁定 GLFW 3.4 的已知初始化崩溃。
该问题不是 Tina 回归，当前门禁也不声明支持无 seat compositor；Wayland 环境必须是真实
session 或显式提供 `wl_seat` 的受控 compositor。

当前 Clang X11 的基础 `tina_tests` 在**无 suppression**条件下通过183/183。只有会初始化 GLFW/X11 的
专项测试与样例使用 `cmake/sanitizers/lsan-x11.supp` 中唯一的 `leak:_XimOpenIM`：Ubuntu 22.04
libX11 在 GLFW 调用 `XCloseIM` 后保留 XIM allocation，专项测试12次共4896 B，样例1次408 B。
抑制按第三方符号精确匹配，Tina allocation 仍由 LSan 阻断；不得增加宽泛的 module/category
suppression来隐藏 Tina 泄漏。

当前 Clang 22 Wayland 双后端产物的基础测试也在 ASan/UBSan/LSan 下**无 suppression**
通过183/183，Null样例通过300帧。带 `wl_seat` 的嵌套 Weston 强制 Wayland 后，专项22/22和样例300帧
通过，`_XimOpenIM` 抑制匹配计数为0。同一产物强制 X11 后专项22/22与样例
300帧再次通过，仅精确匹配 `_XimOpenIM`：专项12次/4896 B、样例1次/408 B。

这组 sanitizer 门禁插桩 Tina 自有 target，但 vcpkg 提供的第三方 GLFW 本身未被
sanitizer 插桩。因此结果能验证 Tina 代码、边界交互与生命周期，不宣称完整覆盖
GLFW 内部实现。

同一 M6-A/M7-A/M7-B1 Headless 构建的 `tina_sample_null` 已在 Windows Debug/Release、Linux GCC 13.4
与 Clang 22 ASan/UBSan/LSan 连续运行300帧；M6-A/M7-A 历史构建还曾在 Linux 连续运行10,000帧，均
返回0，并验证 `IGameState::onExit` 与 `IGameApplication::onShutdown` 恰好一次。该样例组合
Headless Platform、Disabled TaskSystem 与 NullRenderDevice，不加入或链接 GLFW、bgfx、EnTT、
FreeType、miniaudio、SDL/SDL3；它不证明真实窗口、GPU、Scene/Asset/Audio 或 Runtime UI pipeline
已经可用。

以下是 2026-07-16 的迁移前完整平台历史基线（含 Button action 生命周期修复）：

| 平台 | 工具链 | 配置 | GoogleTest | 状态 |
| --- | --- | --- | --- | --- |
| Windows 11 | VS 2026 18.4.3 / MSVC 19.50.35717 | Debug | 50/50 | 通过 |
| Windows 11 | VS 2026 18.4.3 / MSVC 19.50.35717 | Release | 50/50 | 通过 |
| Ubuntu 22.04 | GCC 11.4 | 单配置门禁 | 50/50 | 通过 |
| Linux | Clang + ASan/UBSan | 迁移前无可复现 preset | 未验证 | 历史缺口 |

本批已经重新配置和构建 Legacy ON Debug 共存图，并直接执行135/135。菜单、
2D/UI、3D 四条 Debug 路径均完成300帧并正常返回0；日志确认 Scene、Audio、Input、
Event、Window 正常关闭，3D vertex/index buffer 已释放，未留下 Tina 进程。这证明主循环
和退出资源链路通过，不等同于新的截图级画面验收或实体手柄兼容性验收。迁移前表中
GCC 11.4 与旧 Clang 的 Linux 数据仍是历史证据。

## 当前自动化覆盖与 vNext 门禁

- Core 当前：C++23 `std::expected` Result/Status、稳定 Error domain/code、origin/native code/context
  chain、ScopeExit noexcept invoke/move、EnumFlags `std::to_underlying`、Assert、强类型 Duration、
  可注入 Monotonic Clock、固定步钳制/time scale/最多4步/丢弃与余量、基础类型和 Legacy
  Compatibility，以及 MemoryTag、并发 MemoryTracker、Counting PMR、无回退 FrameArena；公共
  memory/error/time/id 头另有逐头独立编译门禁；
- Core vNext 待补：完整 Metric frame/lifetime reset、Trace 开关、Unicode 路径与原子 IO、
  原子写失败恢复、Ensure/CrashContext；严格 UTF-8 scalar/NUL 校验和 owner-aware generation
  ID/Pool 已完成；
- Core 专用结构：GenerationPool 的 fixed storage、stale/wrong-owner、构造回滚、析构和 wrap
  helper 已完成；FrameArena 对齐/reset/OOM/高水位/零回退已完成；StaticVector/InlineFunction
  尚未实现，只在出现真实消费者时加入；
- Task 后续：有界队列、QueueFull/停止后拒绝、TaskGroup 取消与 barrier、owner/generation 迟到任务、
  异常不逃出线程、IO/CPU executor 隔离和确定性合并；
- Runtime 时间：新 FixedStepAccumulator 已覆盖固定步长、真实 delta 钳制、time scale、插值、
  最大追赶步、超额整步丢弃、非零余量、reset 和非法输入不改状态；Legacy FixedStepTicker
  继续覆盖旧 Application 的异常步消费；
- Runtime M6-A：完整 factory bundle/config 在产生副作用前校验；Clock/Platform/Task/Render 的
  failure、success-null 与 throw 覆盖逆序回滚；Ready Host 直接析构、startup transaction、
  run-once、0/1/4 fixed steps、当帧退出仍完成 extraction/UI/submit/present、失败清理及300帧
  Null Runtime 均有直接 GoogleTest；EngineConfig 还覆盖 `maximumStepsPerFrame > 4` 的硬拒绝，
  EngineHost Create/run 为 `noexcept` 边界；
- Platform/Input M7-A：有界 `PlatformFrameView`、严格 UTF-8 owning text arena、最终 Window/Input/
  Gamepad snapshot、保序 raw transition、overflow reset 与 Platform lifecycle batch；只接受
  `PrimaryPointerId`，Gamepad snapshot 强制同 owner/slot 唯一，connect/disconnect/cancel/reset 时序与
  最终 registry 一致；Action Mapper 覆盖 UI consumption/claim 注入、Frame/Simulation domain、0/1/4
  fixed-step、跨帧 active/suppressed source 与最终 held snapshot、窗口/手柄 generation 切换和
  overflow reset；`PlatformEventDispatcher` 覆盖 RAII generation token、自取消、自销毁、重入与异常；
  EngineHost 会在任何 Game callback 前拒绝恶意超限 backend frame；
- Platform/GLFW：私有 `GLFW_NO_API` hidden create transaction、单进程 backend lease、generation
  Window registry、Keyboard/Pointer/Focus/resize/close/committed UTF-8 text producer 已落地；专项测试
  覆盖严格 title/mode/extent 校验、键鼠映射、Unicode codepoint、repeat 状态、失焦 synthetic release
  抑制、close 不发布 partial frame、失败 partial Poll 后双 stream reset/recovery，以及 resize 的单一
  metrics revision/lifecycle event；
- WindowSurface M7-B1：generation `WindowSurfaceId`、backend-neutral `RenderSurfaceState`、
  `WindowSurfaceSnapshot` identity/revision/suspended、move-only `NativeWindowSurfaceLease`、
  WindowSurface-aware factory composition、Render 创建失败/窗口发布失败逆序回滚、私有 Win32/X11/Wayland
  native binding 解码、source window/revision 单调与精确 `surfaceRevision + 1`、NullRender 连续300帧
  suspended maintenance、独立 `engineFrameIndex`/`submissionIndex` 已有直接测试；
- GLFW suspended pacing：专项测试连续300次从 suspended 路径调用 `glfwWaitEventsTimeout(1/60s)` 的
  可缩短测试 seam，不走 busy-loop；seam 在 `TINA_BUILD_TESTING=OFF` 的生成图与产物中不存在；
- Runtime/Platform 防护：`PlatformFrameBuilder::discardFrame()` 允许错误 Poll 后恢复；EngineHost
  wrong-owner-thread `run` 返回结构化错误且不消耗 run-once；
- Platform/Task/Render M6-A：Headless shutdown 后拒绝 poll，Disabled TaskSystem 始终 idle 且
  shutdown 幂等；NullRenderDevice 强制连续 frame index 和 submit/present 配对，300帧始终
  `liveResources == 0`；各模块公共头均有独立编译门禁；
- UI M7-C1a：`tina_ui_tests` 覆盖 generation `UINodeId`、`UIContext` capacity/create、
  `UIRootOwner` move/reset/destruction/off-thread release、tree updater owner 校验、结构
  snapshot、header isolation 和 storage memory 回零；Windows MSVC 19.50 Debug/Release
  均为16/16；Linux GCC 13.4 与 Clang 22 ASan/UBSan/LSan 均为16/16，基础183/183且无诊断；

以下仍是 Legacy 共存构建的回归覆盖，不能当作 vNext UI/Scene/Asset 已实现：

- Legacy 3D Camera：60° 垂直 FOV 必须按 bx 要求以 degrees 进入投影矩阵，防止误转 radians 后 Cube 近距离铺满屏幕；
- Legacy Event：优先级队列、RAII Token、dispatcher 先销毁、立即取消订阅，以及 IME composition 与已提交文本分离；
- Legacy Resource：共享 FileSystem 唯一 completion pump、主线程预算、取消和过期 generation 隔离；
- Legacy Windows 栈预算：EventSystem 实例不得重新引入超过默认线程栈预算的大块 inline queue；
- Legacy UI：hit-test 不隐式布局、重叠节点唯一命中、Capture/Target/Bubble 顺序、动态子节点上下文继承、stale NodeId 失效、上下文先析构、节点移除/自移除生命周期、Pointer Capture 外部释放、Tab/Shift+Tab 焦点遍历、焦点 KeyDown 路由/默认取消/重复键抑制/路由中删除目标、KeyUp 完整路由/停止传播后的局部清理/路由中删除目标、方向键 beam 优先与隐藏/禁用节点过滤、Modal Focus Scope 限制/嵌套恢复/自动失效、设备无关语义导航的 scope/Accept/Cancel 生命周期、未处理按键向祖先回退、每窗口 Theme/DPI 隔离、200% DPI 逻辑坐标命中、裁剪边界、ScrollView 滚轮/钳制和十万行虚拟范围；Button action 还覆盖实例级重入隔离、异常后恢复、不同 action 嵌套、回调销毁自身，以及 Capture 阶段删除 routed click 目标后的 generation 失效。

## 待补自动化门禁

- Legacy Application 现有失败点继续回归；M6-A 尚未覆盖的 initial UI layout、GameStateStack
  与后续模块初始化失败点要随对应消费者加入；
- `IGameState` top-only、structural 与 policy-change 合计每 State 每帧最多一个 command，验证
  replace 后再请求 policy-change 返回 `AlreadyQueued`；覆盖 queue/completion capacity、sequence、
  completion slot 的 Reserved/Delivered/Diagnostics 回收，以及 `initialPolicy` 单次采样；
- push/replace enter 失败保持旧栈且不调 candidate onExit；失败注入必须在 enter 中真实启动读取
  staged owner 的 Task，验证 completion 在 commit 前不可发布，回滚先 cancel + barrier/join、再释放
  Worker 可访问的 owner，最终 Task/owner/completion 计数归零；
- State Transition Commit 后新 State 同帧只 layout 一次、下一帧输入生效；pop/replace 按“关闭
  ingress → cancel → barrier/join → onExit → RAII 析构”清理 roots/focus/capture/TaskGroup，onExit
  恰好一次，Worker 不能观察已释放的 State 成员；
- Platform 后续：production GLFW Gamepad registry/sampled diff、OS Pointer Capture、100%/150%/200%
  DPI 的 UI 命中、Windows IMM32 composition 与窗口销毁顺序；Window/Keyboard/Pointer/Focus/
  close/committed text callback/poll adapter 已完成，不能继续列为待实现；
- 2D world picking 在 Action Mapping 使用 last-presented Camera/Surface revision 转换一次；0步后
  Camera 移动/resize 也不得改变已锁存 WorldPointerSample；viewport 外明确 no-hit；
- Camera2D 覆盖 NaN/Inf/非正投影值、`x + width`/`y + height` 越界、零 Surface suspension 和
  Catalog canonical PPM mismatch；PixelPerfect 覆盖强制 CameraAndSprites snap/nearest sampler、
  Camera 相对旋转、Size override 与最终 texel basis/origin 校验，不合格 Camera 不生成 view、
  不合格 Sprite 被去重诊断并跳过；
- Scene 延迟 push/pop/replace，以及 fixed phase mutation barrier、延迟实体销毁和
  interpolation snapshot；
- UI WindowRecord 唯一 ownership、RootOwner rollback、UINodeId cross-window owner 校验/回绕 retire、committed
  paint-hit snapshot、细粒度 dirty 和布局中新增 dirty 不丢；
- UIInputScopeSnapshot 对多个 eligible State roots 只做一次全局 hit-test；阻断/恢复时 Pointer Cancel、
  Focus history、Modal root scope 与 generation 失效顺序固定；
- Transform/scroll/clip 只重建 composite snapshot，不重建 local PaintCache；Visible/Hidden/Collapsed
  dirty 传播完整，相同 effective clip 确定性 intern 为同一 ClipId；
- 无变化 UI 必须 Style/Layout/PaintCache rebuild=0且 Tina heap allocation delta=0；每窗口 layout
  <=1、每 Pointer transition hit-test<=1，dirty leaf 不重排无关 subtree；
- UI 多指针/多按键、触摸输入、GLFW 手柄轮询/回滞/长按重复的可注入测试、实体手柄矩阵、焦点回调中的延迟销毁、可访问语义和截图级激活视觉状态；
- Checkbox 的 Pointer/Keyboard/Gamepad 单次切换、disabled/preventDefault、回调自销毁；Slider
  的有限性校验、clamp/step 量化、min==max、capture 拖动、Home/End、每帧单次 change 与 DPI
  命中；设置 backend 失败时 model 回滚且不留下错误全屏/音量状态；
- UI Semantics 的 Role/Name/Range/Checked/Enabled/Focused、labelledBy stale UINodeId、装饰节点过滤
  和稳定树序；Theme/DPI revision 只使必要 style/layout dirty，敏感 TextEdit 正文不进诊断；
- Font Asset lease、UTF-8 非法序列替换、中文 fallback、Atlas page 满容量/退役、raster completion
  stale generation；text measure 与 raster 分离，glyph 发布只 Paint dirty，不改变既定 advance；
- M7-C UI producer 必须把真实 routed consumption/continuous claim 接入已完成的 M7-A seam，并覆盖
  capture/focus/modal 取消；M7-E GLFW Gamepad 只验证相邻 Poll sampled diff，实体矩阵和回滞/重复；
- Replay 后续只记录 target tick、normalized action state、ordered edge 和 reset marker，不记录 GLFW
  key 或 UI node；CloseRequested 的真实 GLFW callback 路径不得重复发布生命周期或 gameplay 事件；
- Game SDK umbrella header 在无 bgfx/GLFW/EnTT include path 下独立编译；public source/include、
  module direct/public dependency 通过第三方 forbidden-token/target 检查；外部 Game consumer
  只声明 Game SDK + desktop bootstrap 也能完成生产链接；可选 `Tina::Physics2D` consumer 在无
  Box2D include path 下单独编译和链接；
- RenderScene 与 UIDisplayList 分别只生成一次并汇入 RenderFramePacket；DisplayList 不含 Widget/bgfx，
  相邻兼容 batching 保持 paint checksum；
- 在途 RenderFramePacket 期间卸载 Asset、退役 Atlas、关闭 Surface 和注入 Pass 失败仍保持引用
  有效；completion 后 packet/lease/pin/resource count 归零；纯 UI/2D-only/3D-only/无内容/
  `Suspended` surface 的 initial clear 次数固定，UI-only/2D-only depth allocation count 为0；
- Render Pass 顺序、禁用与失败停止、临时资源清理、typed handle generation 与
  RenderFramePacket 引用保活；
- Asset CPU Decode/GPU Upload 双队列的 generation 取消，以及任务数、字节、时间预算和
  饥饿保护；弱 Handle/强 Lease、UploadTicket/retirement、依赖循环/失败链、Cooker 的损坏/
  不支持 glTF、生成后验证、事务 Manifest 和增量更新；
- Asset import 的缺失/重复 ID、移动保 ID、复制分配新 ID，以及同一锁定输入跨两次独立 cook
  生成 byte-for-byte 相同产物；设置/依赖/schema/target 任一变化都会令 cache key 失效；
- Cooker 拒绝绝对/UNC/远程/根外 `..` 与 symlink URI、超限 data URI、整数溢出和解压炸弹；
  Runtime 即使 ContentHash 匹配也拒绝越界 payload/dependency table；
- Audio：Voice generation、command/completion 满容量、callback 0分配/0阻塞、设备 Disabled、
  Stop/自然结束竞争、Asset lease ACK 和重复 shutdown；
- 2D layer/order/alpha、Camera resize/world picking、Tile chunk culling/dirty rebuild、Tile AABB/
  Box2D 分工和 UI overlay 不穿透；
- 3D Camera/Material/Texture/depth occlusion、bounds/culling/instance、resize、Cooked glTF/Prefab 和
  不支持特性诊断；
- 完整 Tina 游戏的 Linux GCC/Clang production backend 2D/UI/3D 运行，以及 Clang ASan/UBSan preset。

Windows 和 Linux 必须分别构建。项目直接运行 GoogleTest 可执行文件，不使用 CTest 调度；
Clang ASan/UBSan 使用项目固定的 Clang22 + libstdc++15 chainload toolchain，不能只换 compiler
可执行文件却继续绑定旧系统标准库。

vNext 的每个垂直切片先通过模块级 GoogleTest，再启动对应样例。Null Runtime 连续300帧；
Platform/UI、Scene/2D、Render/3D、Asset/Cooker 分别保留独立运行入口。测试程序返回0、日志
资源计数为0和实际画面正确是三个不同证据，验收记录必须分别给出。

性能数据由独立 Release `tina_bench` 直接运行并输出带 schema、workload version/checksum、
硬件、工具链、依赖 fingerprint 和提交信息的结果，不使用 CTest。普通 GoogleTest 不使用
易抖动的绝对微秒阈值；Tina-owned 零稳态分配、容量溢出、checksum 和资源归零等确定性契约
仍直接阻断。当前开发机只产生 provisional 结果；固定门禁机才允许绝对预算和相对回归门禁。

正式 p99 每 workload 至少5个独立进程、每进程 warm-up 600帧并采10,000帧，nearest-rank
计算；比较 run-level p99 中位数和 baseline MAD。Build/host/workload fingerprint 不同返回
BaselineIncompatible。基准输出不记录 hostname、用户名或绝对路径，正确性 checksum 不同的
run 不参与性能比较。

Tracy Profile 构建单独验证 Tina zone、frame、thread name、可选 lock/memory event 和正常
shutdown；空后端与 Tracy 后端必须产生相同业务结果。Bench/Profile 使用相同优化/CRT/assert/
LTO 语义，只改变插桩和符号；正式 `tina_bench` 默认关闭 Tracy，需要定位回退时才用相同
workload 启用。Tracy overhead 与常驻 Metrics off/on overhead 分开记录。

Windows M6-A/M7-A/M7-B1 Headless 的完整直接门禁为：

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug --target tina_tests tina_sample_null
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_sample_null.exe --frames=300

cmake --build --preset windows-vnext-release --target tina_tests tina_sample_null
out\build\windows-msvc-vnext\bin\Release\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Release\tina_sample_null.exe --frames=300
```

Windows M7-C1a UI 树核心的独立直接门禁为：

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug --target tina_ui_tests
out\build\windows-msvc-vnext\bin\Debug\tina_ui_tests.exe --gtest_color=yes

cmake --build --preset windows-vnext-release --target tina_ui_tests
out\build\windows-msvc-vnext\bin\Release\tina_ui_tests.exe --gtest_color=yes
```

当前记录为 Windows MSVC 19.50 Debug/Release 均 16/16。

Windows GLFW Platform 的独立直接门禁为：

```powershell
cmake --preset windows-msvc-vnext-platform
cmake --build --preset windows-vnext-platform-debug `
  --target tina_tests tina_platform_glfw_tests tina_sample_platform
out\build\windows-msvc-vnext-platform\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-platform\bin\Debug\tina_platform_glfw_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-platform\bin\Debug\tina_sample_platform.exe `
  --frames=300 --frame-delay-ms=0
```

Release 使用对应 `windows-vnext-platform-release` 与 `bin/Release`，并继续与 Debug 串行构建。
Windows Desktop bgfx 的独立直接门禁为：

```powershell
cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_tests tina_platform_glfw_tests tina_render_bgfx_tests tina_sample_desktop
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_platform_glfw_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_bgfx_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_desktop.exe

cmake --build --preset windows-vnext-bgfx-release `
  --target tina_tests tina_platform_glfw_tests tina_render_bgfx_tests tina_sample_desktop
out\build\windows-msvc-vnext-bgfx\bin\Release\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Release\tina_platform_glfw_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Release\tina_render_bgfx_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Release\tina_sample_desktop.exe
```

当前 Windows 最新结果是 Debug/Release 均基础183/183、GLFW专项22/22、bgfx专项11/11、Null样例300帧、
WindowSurface GLFW样例300帧与真实 D3D11 Intel Iris Xe Desktop样例默认300帧返回0；另有
`TINA_BUILD_TESTING=OFF` production-style WindowSurface GLFW样例300帧返回0。
Linux M7-B2 Desktop/bgfx X11 门禁也已通过：GCC 13.4 为183/183、22/22、11/11和 Desktop 300帧；
Clang 22.1.8 + ASan/UBSan/LSan 为相同的183/22/11和 Desktop 300帧。Clang 基础/bgfx测试无
suppression；X11精确 suppression 在GLFW专项命中12次/4896 B、Desktop命中1次/408 B。
Clang Desktop 使用 bgfx Vulkan，但 WSL2 adapter 是 llvmpipe 软件实现，不计作硬件 GPU 门禁。
Linux X11、Wayland和 Clang LSan精确 suppression的完整命令见[构建与运行](building.md)。

Visual Studio 多配置输出必须使用对应的 `bin/Debug` 或 `bin/Release`，不能混用 GoogleTest DLL。
同一 build tree 的两种配置也必须按上面命令顺序构建，不能并发驱动共享生成状态。
Legacy 的 `Tina.exe`、shaderc 和 app-local DLL 同样按配置隔离。Linux 单配置构建直接运行
`out/build/<preset>/bin/` 下对应测试 executable。

只验证 vNext Core/Runtime 和测试源码的 Linux 编译/链接时，使用独立 GCC 13 preset，避免
污染可运行的 Legacy `linux-ninja` cache：

```bash
cmake --preset linux-gcc13-vnext
cmake --build --preset linux-gcc13-vnext-debug --target tina_tests tina_ui_tests
./out/build/linux-gcc13-vnext/bin/tina_tests --gtest_color=no
./out/build/linux-gcc13-vnext/bin/tina_ui_tests --gtest_color=no
```

Clang 22 sanitizer 的基础与 UI 直接门禁为：

```bash
cmake --preset linux-clang22-vnext-sanitize
cmake --build --preset linux-clang22-vnext-sanitize-debug --target tina_tests tina_ui_tests
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
LSAN_OPTIONS=exitcode=23 \
./out/build/linux-clang22-vnext-sanitize/bin/tina_tests --gtest_color=no
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
LSAN_OPTIONS=exitcode=23 \
./out/build/linux-clang22-vnext-sanitize/bin/tina_ui_tests --gtest_color=no
```

GCC 13.4/CMake 4.2.3 已通过基础 183/183 与 UI 16/16；Clang 22.1.8 + libstdc++15.2
ASan/UBSan/LSan 已通过基础 183/183 与 UI 16/16，且无诊断。`TINA_BUILD_SHADERS=OFF` 输出
不含 Legacy 产品和 cooked shader，只能作为 Headless 验证程序，
不能作为游戏产品或发布包。

## 当前 Legacy 运行冒烟

构建命令和环境前提见 [构建与运行](building.md)。以下命令描述验收入口，不替代构建步骤。

菜单 2D + 中文 UI，正常提交300帧后退出：

```bash
./Tina --smoke-frames=300
```

直接进入完整 2D TileMap、ECS、Toolbar 和 CharacterPanel：

```bash
./Tina --smoke-game --smoke-frames=300
```

直接显示虚拟化世界列表、新建世界对话框、中文标签和已聚焦 TextEdit：

```bash
./Tina --smoke-ui --smoke-frames=300
```

运行右手透视相机、深度测试和静态索引 Cube：

```bash
./Tina --smoke-3d --smoke-frames=300
```

四个命令都必须返回0，并在日志中出现正常初始化、达到帧数、场景退出、资源管理器释放、bgfx 和窗口关闭记录。UI 路径还必须出现 `UI smoke scene ready`，且不得出现 `无法建立模态焦点范围`；3D 路径必须肉眼或截图确认透视 Cube 可见，并出现 `Smoke3DScene released vertex and index buffers`，且不得出现 `BGFX LEAK` 或 `MEMORY LEAK`。只检查 exit code 和 buffer 生命周期不足以证明画面正确。

bgfx Debug/D3D11 当前会在关闭 `ID3D11InfoQueue` 时输出一次 `RefCount is N (expected 0)`；本机
Legacy 与 vNext 进程观察到的 `N` 会随调试对象组合变化。同一路径的 MSVC Release 300帧验证无该
提示、无 stderr、无 leak marker；Tina 仍以自身资源账本和严格 shutdown 顺序作为泄漏门禁，不把
这条第三方 Debug layer 引用计数提示单独当作 Tina 资源泄漏结论。

## vNext 独立样例门禁

`tina_sample_null`、`tina_sample_platform` 与 `tina_sample_desktop` 已落地，其余 executable 仍是后续里程碑目标；
不得用当前 Legacy `Tina --smoke-*` 的结果冒充 vNext 样例：

| 样例 | 状态 | 主要证明 | 资源策略 |
| --- | --- | --- | --- |
| `tina_sample_null` | M6-A/M7-A/M7-B1 Headless 已实现 | EngineHost、PlatformFrame/Input/Action、单个 `IGameState`、Headless/Disabled/Null、300帧生命周期；Linux 10,000帧仍是上一批历史结果 | 无真实第三方 backend |
| `tina_sample_platform` | M7-A + M7-B1 已实现 | 私有 GLFW `NO_API` 窗口、键鼠、resize/focus/close、committed text、WindowSurface handoff 与 NullRender | 不创建真实 bgfx GPU device |
| `tina_sample_desktop` | M7-B2 Desktop bootstrap + 真实 backend smoke 已实现 | `Tina::Desktop::CreateEngine` 私有组合 SteadyClock、GLFW WindowSurface、DisabledTaskSystem 与 bgfx；默认300帧 deep-blue clear/present | Windows D3D11 Intel Iris Xe Debug/Release、Linux GCC 13.4 与 Clang 22 sanitizer 已通过；Clang WSL2 为 Vulkan/llvmpipe，不代表硬件 GPU 性能，也不代表 Scene/UI/Pass Scheduler 完成 |
| `tina_sample_ui` | 未实现 | committed snapshot、dirty/Flex/PaintCache、中文、Modal、TextEdit、DisplayList | M7 内置 Cooked Font/Texture fixture |
| `tina_sample_2d_infrastructure` | 未实现 | Camera2D、Sprite layer/order、world picking、UI overlay | M8 内置 Cooked Sprite fixture |
| `tina_sample_3d_infrastructure` | 未实现 | Perspective、depth、canonical Mesh、Unlit pipeline | M9 procedural Cube |
| `tina_sample_2d` | 未实现 | Cooked TileMap/Tileset、chunk、角色/Tile AABB、Box2D dynamic body、正式 UI | M10/M11 Catalog/Manifest |
| `tina_sample_3d` | 未实现 | Cooked glTF -> Mesh/Material/Prefab、culling/instance | M10 Catalog/Manifest |

M7-B2 已建立私有最小 bgfx clear/present core、7项 planner 测试、4项 factory/lease 回滚测试、
Desktop bootstrap 和真实 GPU 300帧冒烟。M7-C/M7-D 建立 UI DisplayList/UI Pass，M9 只扩展3D。游戏 sample source、Game SDK
header 和 UI public header 不出现 bgfx。结构化验收使用 backend-neutral 字段：

```text
RenderDevice stopped
render.resources.current = 0
render.retirement.pending = 0
ui.resources.current = 0
```

具体 backend 的 InfoQueue/debug marker 只属于 adapter test/log，不成为 Game API 或通用样例
成功条件。每个可见样例仍分别保存返回码、资源计数、性能结果和实际截图。

Windows GLFW 可见门禁已经分别验证：中文 UTF-8 标题可见；Escape 经过 GLFW callback、归一化
transition 和 Frame Action，在完成当前 Null submit/present 后退出；Alt+F4/原生 close 走
`PrimaryWindowRequestedClose`，不重复发布 lifecycle/gameplay event。客户区空白是 NullRender 的预期，
不能据此宣称真实 bgfx、2D、UI 或3D已完成。Debug/Release自动样例都以零延迟精确运行
300帧、返回0，`IGameState::onExit` 与 `IGameApplication::onShutdown` 计数各为1，退出后无残留 Tina
进程。

Windows Desktop bgfx 可见门禁已验证 `tina_sample_desktop` 通过 `Tina::Desktop::CreateEngine`
创建真实 D3D11 Intel Iris Xe surface，默认300帧 deep-blue clear/present 后返回0；Game SDK/public header
无 bgfx、GLFW 或 native 泄漏。该门禁不包含 Scene/UI 内容、不包含 Pass Scheduler/submission ticket，
也不声明 resize、最小化、恢复的真实自动化通过。

Linux Desktop 门禁同样只证明 bgfx backend 初始化、300帧提交与关闭生命周期。Clang 路径的
`_XimOpenIM` suppression 仅覆盖第三方 libX11 retention（专项12次/4896 B、Desktop 1次/408 B）；
基础与bgfx专项不使用 suppression。WSL2 的 Vulkan/llvmpipe 结果不扩大为硬件 GPU、Scene/UI、
Pass Scheduler 或 resize/最小化/恢复自动化结论。
