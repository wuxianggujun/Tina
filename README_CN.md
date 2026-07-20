# Tina 游戏引擎

Tina 是以 C++23 为目标语言基线的 2D/3D 游戏引擎项目。平台与输入层使用 GLFW，私有渲染后端使用 bgfx，音频使用 miniaudio，ECS 使用 EnTT，UI 为完全自研的 Retained UI。vNext 游戏侧契约不暴露 bgfx、GLFW 或其他第三方类型；仍在运行的 Legacy 实现尚未完成这项边界迁移。

## 当前目标

当前阶段允许不兼容旧 API 的完整 vNext 重构，但不会用一个长期不可运行的大提交替换全部
Runtime。现有2D/UI/3D路径继续作为验收基线，新架构按可独立构建和运行的垂直切片迁移：

- 保证现有 2D 场景和自研 UI 能启动、交互和正确释放资源；
- 修复 Application、Event、Resource、Scene 和 UI 的生命周期与每帧驱动顺序；
- 使用直接运行的 GoogleTest 可执行文件覆盖核心行为；
- 增加最小 3D 冒烟场景，验证透视相机、深度测试和静态 Mesh；
- 以 `IGameApplication` 表示“整个游戏程序入口”，以 `IGameState` 表示“菜单、关卡、暂停等逐帧运行状态”，不再使用含义模糊的 `IGame`；
- 建立细粒度 dirty、单次布局、持久 Paint Cache 和稳定 Display List 的高性能 Retained UI；
- 参考 Carbon Engine 的 Frame Step、资源 Load/Prepare/Upload 和 GPU 生命周期，但保持 Tina 架构小而清晰。
- 采用 Tina-owned Trace/Metrics 和可选 Tracy 定位热点；规划独立 `tina_bench` 建立可重复性能回归，
  但当前尚未实现该 target；
- 新 target 不使用 EASTL，也不自研通用 STL；标准库/`std::pmr` 加少量专用固定容量结构。

当前旧文档已经替换，但旧源码架构仍是正在运行的主实现，并未完全删除。迁移状态和删除门禁见 [架构总览](docs/architecture.md)。物理后端固定为 2D Box2D 3.x 与 3D Jolt，不引入第三套物理引擎。

vNext 已完成 C++23 Headless Runtime 生命周期内核、M7-A Platform/Input 内核、首个桌面适配切片、
M7-B1 私有 WindowSurface handoff、M7-B2 Desktop bootstrap + 真实 GPU 冒烟，以及
M7-C1b/M7-C1c-a/C1c-b1/C1c-b2/C1c-b3a/C1c-b3b/C1c-b3c/C1c-b3d1/b3d2/b3e Retained Tree/Flex-lite layout/
committed hit/paint snapshot、point query、synthetic routed pointer、private Runtime route、startup UI seed、
Game SDK scoped capability、后端无关 SolidQuad DisplayList foundation，以及 D0 Runtime-private
primary-window UIDisplayList submit handoff、D1 私有 bgfx SolidQuad UI pass 和 D2 Game SDK box-paint authoring：
私有
`tina_platform_glfw` 已能创建 `GLFW_NO_API` 窗口，
并把键盘、Pointer、Focus、resize、close 与已提交 UTF-8 文本归一化到同一份有界
`PlatformFrameView`；Runtime 通过 generation `WindowSurfaceId`、无原生句柄的
`WindowSurfaceSnapshot` 和 move-only `NativeWindowSurfaceLease` 把窗口 surface 交给 Render
组合，Game SDK 不暴露 native 或 bgfx 类型。`Tina::Desktop::CreateEngine(config)` 已作为普通桌面入口
落地，当前私有组合为 `SteadyClock + GLFW WindowSurface + DisabledTaskSystem + bgfx`；
`tina_sample_desktop` 默认运行300帧，并用 retained tree 创建4个 SolidFill panel 验证 UI DisplayList
被私有 bgfx backend 消费；深蓝色 clear 只作为背景。
`tina_ui` 已有 generation tree、固定容量事务式布局、固定容量 PMR Pointer policy/route ancestry scratch，
以及双缓冲 `UICommittedHitView`。同一 view 内 hit entry 的 paint ordinal 唯一且严格递增，并携带 structure/layout/
paint-order/hit revision；hit-only commit 不执行布局。`UIBoxPaint` 当前支持可选 SolidFill，并以固定
`paintSnapshotCapacity`、local premultiplied RGBA8 cache 与双缓冲 `UICommittedPaintView` 发布
effective-visible 非透明 entry；paint-only commit 不重排 layout/hit，`commitLayout()` 失败会同时保留旧
structure/layout/hit/paint 四份 snapshot。`queryPointerHit()` 已按反向 paint order 做无分配的 committed point query，返回稳定
route index/revision 与 visited count，不触发布局或事件。C1c-b2 新增 synthetic `routePointerInput()`：
只针对一条已归一化 Pointer input，在上一份 committed hit snapshot 上最多查询一次，并使用固定容量
route path/listener storage、48-byte fixed-inline `noexcept` callback、generation-safe RAII listener token、
owner-thread 立即 reset、off-thread 有界 deferred reset、Capture→Target→Bubble、stopPropagation/
stopImmediatePropagation、consumeInputTransition、路由中 add/reset/destroy 安全失效，以及 route/commit
reentrancy guard。`UIContext` 的 mutation、route 与销毁仍只允许 owner thread，且不能在 route callback
或 callback cleanup 内销毁。
C1c-b3b 已实现有界 private producer；C1c-b3c 让 `EngineHost` 在第一次看到 primary `WindowId` 时
lazy bind 一个私有 `UIContext`，在 Platform event dispatch 之后、`ActionMapper` 之前执行路由，窗口身份
消失或 generation 更换时结构化失败，并在 Render → Task → Platform → Clock 模块关闭前先销毁 Context。
C1c-b3d1 把固定容量配置收敛到独立的 `UIContextCapacityConfig`，让
`EngineConfig::primaryWindowUICapacities` 在任何 backend factory 运行前完成统一校验，并增加
Runtime-private layout coordinator：`IGameState::updateUI()` 成功后、Render submit 前，使用主窗口
logical extent 对每个 `PlatformFrameId` 至多尝试一次 `commitLayout()`；Headless 帧同时没有窗口和
Context 时是成功 no-op。提交失败会阻断 Render，且本帧 attempt 已消费，不能用同一批 mutation 重放；
输入路由仍只读取上一份 committed hit snapshot。C1c-b3d2 已按
[ADR 0021](docs/adr/0021-runtime-ui-startup-capability.md) 实现 backend-neutral
`initialPrimaryWindowMetrics()` seed、`onEnter` 前显式绑定 primary `UIContext`、State commit 前的 startup
structure/layout/hit/paint 发布，以及 root-scoped、owner-thread、phase-epoch-scoped 的
`PrimaryWindowUIRootBuilder` / `PrimaryWindowUITreeUpdater`。这些 facade 在回调结束时无条件失效，第一次
capability operation 失败会成为该 phase 的 sticky error，且不会向 Game SDK 暴露裸 `UIContext*`。
后续兼容扩展已把 `addRoutedPointerListener()` 同时加入低层 `UITreeUpdater` 与 Game SDK facade：只有返回的
move-only `UIRoutedPointerListenerToken` 可以跨 phase 保存，token 不延长 `UIContext` 或 root 生命周期，
State 必须在 `onExit()` 先 reset listener token、再释放 `UIRootOwner`。注册在 user callback 最终 move 后会
重新校验 root/generation/subtree；callback move/destructor 若重入释放 root，整次注册原子回滚且不消耗 listener
slot/high-water。callback move/cleanup 期间销毁 `UIContext` 属生命周期硬错误并终止进程。
M7-C1c-b3e 又让 Move/Wheel/Button routed Pointer listener 通过 `claimPointerButton()` 请求当前窗口/Pointer 的按键所有权；
Runtime 只把帧末快照中 `PrimaryPointerId` 上仍 held 的任意 Pointer Button 去重写入双缓冲
`ContinuousControlClaimsView`，容量失败不发布半份结果，已有 `ActionMapper` 会立即 Cancel Gameplay source
或拦截同帧未 consume 的 Down，并抑制到真实 Up。EngineHost 端到端门禁已经证明 Game SDK listener 先于
ActionMapper 执行，且只 claim、不 consume 的 callback 也会抑制同帧 Gameplay Action。后续 Button default action
切片已在同一 primary-window route seam 上实现窄交互：Button 创建后默认 `Targetable`，只处理
`PrimaryPointerId + PointerButton::Primary` 的 Down/Move/Up pressed 状态与一次 Up-inside activation；
`preventDefaultAction()` 只阻止 Down arm 或 Up activation，且独立于 stop/consume/claim；action 作为
Button retained property 由 `setButtonAction()` 原子替换、`clearButtonAction()` 或节点/root 销毁撤销；
cancel/reset 清理 armed/pressed，不合成 Up、不触发 action。Runtime-private producer 会在
`ActionMapper` 前把 listener 与 default action 产生的 consumption/claim 合并发布。`Tina::Render` 已实现固定 PMR、单缓冲的
SolidQuad `UIDisplayListBuilder`；独立 `Tina::UIRenderIntegration` target 已能把 committed logical paint 以
outward rounding/clamp 转换为 framebuffer DisplayList，且 Windows MSVC 19.50 Debug/Release 的12项
bridge GoogleTest 均通过。D0 已在 Runtime-private `PrimaryWindowUIDisplayCoordinator` 中于
layout/paint commit 后、Render submit 前用固定 PMR builder 构建 primary-window UIDisplayList，并通过
`RenderFrame::primaryWindowUIDisplayList` 作为 submit-call-local borrowed view 交给 backend；backend 必须同步
消费/复制/编码，禁止在 `submitFrame()` 返回后保留 view、span 或元素指针。Headless、0 framebuffer 与
suspended surface 路径发布空 list；构建失败不保留旧 publication，也不提交截断 list。D1 后
`tina_render_bgfx` 会把非空 SolidQuad DisplayList 写入 transient VB/IB、按 batch 提交私有 shader 程序；
D2 又让 `PrimaryWindowUITreeUpdater::setBoxPaint()` 进入 Game SDK facade，Desktop 样例可见4个
retained SolidFill panel。当前可见路径仍只是 SolidFill quad，不等于完整 Widget/UI：文本/glyph
渲染尚未接入，Label 仍不绘制文本，Button 的 Keyboard/Gamepad activation、Disabled/theme 视觉与完整
Widget facade 仍后置。持久 Pointer Capture、Focus/Modal、Key/Gamepad/axis claim、
Image/Text/Glyph PaintCache、完整 dirty-range pruning、nested clip、owning Runtime RenderFramePacket、
FramePin、production Gamepad、Windows IMM32 composition、Pass Scheduler、submission ticket/drain 与可见
中文 UI 分别放在后续切片。M8-A 已新增独立 `tina_scene`：固定容量 `Scene::World`、generation/owner
`EntityId`、Local/World Transform、非递归层级传播、默认 keep-world/显式 keep-local、父销毁提升与显式
子树销毁；两阶段 publication 保证失败不发布部分 World snapshot，并诊断循环、溢出和当前 TRS 无法表达的
shear。M8-B 在此基础上新增固定容量、后端无关的 `RenderSceneBuilder`、只在
`extractRenderScene()` 回调内有效的 `RenderSceneWriter`、解析后的 Camera2D/Sprite2D 输入、稳定 layer/order
排序、保守裁剪、pixel snap、Runtime `RenderFrame` handoff，以及 `tina_sample_2d_infrastructure` Headless/Null
记录样例。本切片不链接 EnTT/GLM，也不实现 Scene component command buffer、Asset/Cooker、可见 bgfx
fixture、world picking 或正式 2D/UI 产品门禁；M9-C 只补齐私有 Sprite2D fixture，可见产品路径仍按
后续切片推进。

M9-A 在同一 builder 上新增后端无关的 Perspective Camera 与 Mesh3D extraction：右手 Y-up、`-Z forward`、
正 scale 的世界包围球、球体 frustum culling、稳定 material/mesh/submesh/double-sided/depth/entity 顺序，
以及相邻兼容项的 instance batch finalize。Runtime 每帧从当前 primary `PlatformFrame` 注入 framebuffer aspect；
framebuffer 为 `0x0` 时回退到正的 logical extent。新增 `tina_sample_3d_extraction` 只验证 CPU/Headless/Null
边界，不显示 GPU Cube，也不能替代正式 3D 产品门禁。M9-B 已在私有 `tina_render_bgfx`
接入 fixture 级 Opaque3D：只接受 `meshKey=1/materialKey=1/submeshIndex=0`，用 canonical
`P3_N3_UV2` procedural Cube、Unlit shader 和真实 bgfx transient instance buffer 形成最小可见3D样例
`tina_sample_3d_infrastructure`。M9-C 当前又在同一私有 fixture 基础设施中接入 Sprite2D：只接受
`spriteKey=1`，把 Sprite 展开为 transient P2/UV2/ABGR 四边形，支持旋转、透明和 flip。当前固定
fixture View 顺序是 0 clear、1 Opaque3D、2 Sprite2D、3 UI；这只是临时固定 View 编号，不是
Pass Scheduler。`tina_sample_2d_infrastructure_bgfx` 默认/门禁运行300帧，每帧5个 fixture Sprite
和2个 retained UI panel；它不等于 Asset/Texture/Sprite 产品路径、正式 `tina_sample_2d`、TileMap、
Box2D、中文文本或 M10 的产品资产路径。

M10-A0 已新增独立 `tina_asset_format`：互不兼容的16字节 `AssetId`/`ContentHash`、固定
little-endian Cooked Header/Manifest/Entry/Dependency schema、确定性 object path，以及不分配的
caller-owned borrowed view。parser 在发布 view 前校验 hard limit、checked arithmetic、canonical layout、
zero padding、排序/重复、依赖存在与 kind。该切片只完成 wire-format 基础，不计算 XXH3、不做完整
DAG cycle、文件 IO、Asset registry/Handle/Lease、worker/upload、cgltf、Cooker writer 或正式资产样例。

M10-A1 已新增独立 `tina_asset`：owning 不可变 `CatalogSnapshot` 在注入 `std::pmr` 上事务式复制
已解析 Manifest，Create 后不依赖原始 bytes；`find(AssetId)` 为 binary search；依赖目标解析为稳定
entry index；完整 DAG cycle 使用迭代着色算法（`O(V + E)`，禁止递归）。该切片不实现 Handle/Lease、
registry 状态机、文件 IO、Task、GPU upload、XXH3、cgltf 或正式资产样例；ADR 0016 仍为 Proposed。

## 当前 Legacy 已完成基线

- 现有 Legacy target 的包依赖已迁移到 vcpkg manifest；bgfx 与 EASTL/EABase 仍是源码依赖；
  这是当前实现事实，不是 vNext 的最终依赖方案；
- Window/Input 已迁移到 GLFW；
- 音频已迁移到 miniaudio；
- Core 已增加强类型、Result、Assert、ScopeExit、Clock 和 FrameTimer；
- Windows/Linux CMake Preset 已建立。
- `GameScene` 已通过真实 2D TileMap、ECS、中文 UI 和音频冒烟；
- `Smoke3DScene` 已通过右手透视相机、深度测试和静态索引 Mesh 冒烟。

vNext 将继续使用锁定源码版本的 bgfx，但新 target 禁止 EASTL/EABase；具体版本、可见性与
删除门禁见[第三方依赖与版本治理](docs/dependencies.md)。

## 构建

目标构建需要 CMake 3.25 以上、支持 C++23 的编译器和 `VCPKG_ROOT`。Tina 自有 target 已统一请求
`cxx_std_23`，MSVC 保持 `/utf-8` 与 `/Zc:__cplusplus`。Windows 已在 Visual Studio 2026 18.4.3、
MSVC 19.50.35717 和 `D:\Programs\CMake\bin\cmake.exe` 4.2.3 下通过前一轮 dirty-subtree b4a +
M8-A/M8-B 的 Debug/Release 直接门禁：基础211/211、独立 UI 115/115、独立 Runtime→UI 60/60、
UI→Render 12/12、Scene 19/19、RenderScene 11/11，以及 Null 与2D infrastructure 样例各300帧正常
退出；2D样例同时验证每帧3个 Sprite、Render shutdown 恰好1次和资源归零。该轮还重新通过 Windows
Debug GLFW专项26/26、Platform样例300帧、bgfx专项16/16，以及 Desktop样例连续3次各300帧。新增
iconify 回归验证最小化时沿用最后有效 logical extent，同时保留 framebuffer `0x0` 的 suspended 语义。
该轮没有重新截图，因此画面正确仍引用前序 D2 可见证据；Linux M8-B 与可见 Sprite 仍未复验。上一轮完整 D2
Windows Debug/Release 证据仍为基础207/207、UI92/92、Runtime→UI53/53、UI→Render bridge12/12、
bgfx专项16/16、Null样例300帧，以及真实 D3D11 Intel Iris Xe 的 `tina_sample_desktop` 可见 retained UI
样例 Debug 1200帧与 Release 300帧；Release 输出 clean status ok。Debug D3D11 退出时
`RefCount is 3 (expected 0)` 是已记录的第三方 debug layer 提示，不作为 Tina 泄漏结论。前序 WindowSurface
GLFW专项25/25、GLFW样例300/1800帧仍作为历史证据。Legacy ON 图的前序隔离门禁为 vNext 185/185 + Legacy 43/43。
`TINA_BUILD_TESTING=OFF` 的 production-style WindowSurface GLFW样例300帧也已通过。Game SDK 与
公开头检查未发现 bgfx、GLFW 或 native handle 泄漏。

M9-A 当前 Windows MSVC Debug/Release 直接结果均为基础 `tina_tests` 213/213、独立
`tina_render_scene_tests` 22/22，以及 `tina_sample_null`、`tina_sample_2d_infrastructure`、
`tina_sample_3d_extraction` 各300帧返回0。3D extraction 每帧提交4个 Mesh、可见3个、裁剪1个、
形成2个 instance batch，resize 产生一次 aspect 变化，退出时 `liveResources=0`。Release 还直接通过
UI115/115、Runtime→UI60/60、UI→Render12/12与Scene19/19。

M9-C 当前 Windows MSVC Debug/Release 均通过完整 bgfx adapter 专项43/43，并各运行
`tina_sample_2d_infrastructure_bgfx` 300帧：每帧5个 fixture Sprite、2个 retained UI panel，
退出时 UI root 恰好释放1次，`EngineHost` 已销毁且 Render 资源账本平衡。Debug 截图确认 Sprite
旋转、透明、flip 和 UI overlay；既有 D3D11 debug-layer `RefCount=3` 提示只在 Debug 出现，Release
未出现。该结果只证明 fixture/infrastructure，不证明 Asset/Texture/Sprite 产品路径、正式
`tina_sample_2d`、TileMap、Box2D、中文文本或 M10-A1+ 产品资产路径。Linux M9-C 尚未复验。

M10-A0 当前 Windows MSVC Debug/Release 均直接通过独立 `tina_asset_format_tests` 14/14；基础
`tina_tests` 两配置仍为213/213，`tina_sample_null --frames=300` 均返回0。该结果证明格式边界与
只读校验，不证明 AssetSystem、异步加载、Cooker、glTF 转换或2D/3D正式资产样例。

M10-A1 当前 Windows MSVC Debug/Release 均直接通过独立 `tina_asset_tests` 17/17；基础 `tina_tests`
仍为213/213，`tina_asset_format_tests` 14/14，`tina_sample_null --frames=300` 返回0；bgfx Release
回归 `tina_render_bgfx_tests` 43/43，`tina_sample_2d_infrastructure_bgfx` 300帧资源账本平衡。该结果
只证明 owning Catalog 与 DAG cycle 边界，不证明 Handle/Lease、异步加载、Cooker 或正式资产样例。

M10-A2a 已实现：Core 私有 XXH3-128 v1 计算 `ContentHash`，可选校验 Cooked payload；公共头无
xxHash 类型。Windows Debug/Release 基础 `tina_tests` 218/218、`tina_asset_format_tests` 16/16。
不包含文件 IO、Handle/Lease 或 Cooker。

M10-A2b 已实现：Core 有界 `readFile` 与 Manifest 文件→`CatalogSnapshot` 加载闭环。Windows
Debug/Release 基础 `tina_tests` 223/223、`tina_asset_tests` 19/19。仍不包含 Handle/Lease、async IO
或 Cooker。

M10-A2c～A2q 同步 Catalog/Cooked 闭环；M10-A3～A5 CPU Handle/Lease + AssetSystem request/pump；
M10-A6 有界 IO Task 与 Asset 异步读盘（Main completion）。Windows 测试见最近门禁。
仍不包含 CPU TaskGroup 线程池、GPU UploadTicket/retirement 或 Cooker。

Linux 最新 paint/DisplayList/bridge Null 门禁也已完成：GCC 13.4 通过基础205/205、
`tina_ui_tests` 92/92、`tina_runtime_ui_tests` 46/46、bridge 12/12与Null样例300帧；
Clang 22.1.8 + libstdc++15.2 在 ASan/UBSan/LSan 下通过相同205/92/46/12与Null样例300帧，
且无 sanitizer 诊断。前序 M7-B1 Platform
门禁覆盖 GCC 13.4 X11、Clang 22.1.8 X11 sanitizer，以及 GCC 13/Clang 22 X11/Wayland 双后端；
Wayland 使用带 `wl_seat` 的嵌套 Weston 9。初次 GCC 暴露的 routed-pointer callback `requires`
名称可见性问题已修复。前序 M7-B2 Desktop/bgfx X11 图也已直接运行：GCC 13.4 与 Clang 22.1.8 +
ASan/UBSan/LSan 均通过基础183/183、GLFW专项22/22、bgfx专项11/11和 Desktop样例300帧。Clang
基础/bgfx测试不使用 suppression；X11 只对第三方 libX11 `_XimOpenIM` retention 使用精确 suppression，
GLFW专项命中12次/4896 B、Desktop样例命中1次/408 B。Clang Desktop 经 bgfx 选择 Vulkan，但当前
WSL2 适配器是 llvmpipe 软件实现，因此该结果证明 Linux Vulkan/backend 生命周期，不代表硬件 GPU
性能。由 vcpkg 提供的 GLFW 本身未被 sanitizer 插桩。D1/D2 的 bgfx SolidQuad UI pass、
Game SDK `setBoxPaint()` facade 和可见 Desktop 4-panel 样例尚未在 Linux 图重跑，不能沿用前序
Linux Null/bridge 结果替代。详细边界见[测试文档](docs/testing.md)。Clang
preset 使用项目 chainload toolchain 固定标准库，不能退回 Ubuntu 22.04 自带的旧 libstdc++。先确认
终端没有命中不支持 `Visual Studio 18 2026` 生成器的旧版 CMake：

```powershell
cmake --version
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests tina_scene_tests tina_render_scene_tests tina_asset_format_tests tina_asset_tests tina_sample_null tina_sample_2d_infrastructure tina_sample_3d_extraction
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe
out\build\windows-msvc-vnext\bin\Debug\tina_ui_tests.exe
out\build\windows-msvc-vnext\bin\Debug\tina_runtime_ui_tests.exe
out\build\windows-msvc-vnext\bin\Debug\tina_ui_render_integration_tests.exe
out\build\windows-msvc-vnext\bin\Debug\tina_scene_tests.exe
out\build\windows-msvc-vnext\bin\Debug\tina_render_scene_tests.exe
out\build\windows-msvc-vnext\bin\Debug\tina_asset_format_tests.exe
out\build\windows-msvc-vnext\bin\Debug\tina_asset_tests.exe
out\build\windows-msvc-vnext\bin\Debug\tina_sample_null.exe --frames=300
out\build\windows-msvc-vnext\bin\Debug\tina_sample_2d_infrastructure.exe --frames=300
out\build\windows-msvc-vnext\bin\Debug\tina_sample_3d_extraction.exe --frames=300

# 可选 GLFW + NullRender 平台切片
cmake --preset windows-msvc-vnext-platform
cmake --build --preset windows-vnext-platform-debug --target tina_tests tina_platform_glfw_tests tina_sample_platform
out\build\windows-msvc-vnext-platform\bin\Debug\tina_tests.exe
out\build\windows-msvc-vnext-platform\bin\Debug\tina_platform_glfw_tests.exe
out\build\windows-msvc-vnext-platform\bin\Debug\tina_sample_platform.exe --frames=300 --frame-delay-ms=0

# 可选 Desktop bootstrap + 真实 bgfx SolidFill UI 冒烟
cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug --target tina_tests tina_runtime_ui_tests tina_platform_glfw_tests tina_render_bgfx_tests tina_sample_desktop
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_runtime_ui_tests.exe
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_bgfx_tests.exe
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_desktop.exe

# M9-C 私有 bgfx Sprite2D fixture 与 2D/UI 300 帧样例
cmake --build --preset windows-vnext-bgfx-debug --target tina_render_bgfx_tests tina_sample_2d_infrastructure_bgfx
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_bgfx_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d_infrastructure_bgfx.exe --frames=300 --frame-delay-ms=0

cmake --preset windows-msvc
cmake --build --preset windows-debug --target Tina tina_tests tina_legacy_tests
```

测试构建完成后直接运行 vNext 基础 `tina_tests`；`TINA_BUILD_LEGACY=ON` 时还必须直接运行
Legacy-only `tina_legacy_tests`。启用 GLFW adapter 时再直接运行独立的
`tina_platform_glfw_tests`；Scene、RenderScene、UI、Runtime→UI、UI→Render 与 AssetFormat 也各有
独立 GoogleTest executable。所有测试都直接运行，不通过额外测试调度器：

```powershell
out\build\windows-msvc\bin\Debug\tina_tests.exe
out\build\windows-msvc\bin\Debug\tina_legacy_tests.exe
```

Release 使用同一个 Visual Studio 多配置构建目录：

```powershell
cmake --build out\build\windows-msvc --config Release --target Tina tina_tests tina_legacy_tests --parallel 2
out\build\windows-msvc\bin\Release\tina_tests.exe
out\build\windows-msvc\bin\Release\tina_legacy_tests.exe
```

Visual Studio 的测试程序和 GTest 运行库按配置隔离在 `bin\Debug`、`bin\Release`，避免 Debug/Release CRT 混用；Linux 单配置构建仍输出到 `bin`。

完整 Windows/Linux 构建说明、选项和门禁限制见 [构建与运行](docs/building.md)。

运行时验收入口：

```powershell
# 主菜单 + 中文 UI
out\build\windows-msvc\bin\Release\Tina.exe --smoke-frames=300

# 专用 UI：虚拟化列表 + 对话框 + 中文 TextEdit（启动即聚焦）
out\build\windows-msvc\bin\Release\Tina.exe --smoke-ui --smoke-frames=300

# 完整 2D + 自研 UI
out\build\windows-msvc\bin\Release\Tina.exe --smoke-game --smoke-frames=300

# 最小 3D：Perspective Camera + Depth Test + Indexed Cube
out\build\windows-msvc\bin\Release\Tina.exe --smoke-3d --smoke-frames=300
```

项目不使用 CTest 调度；测试直接运行固定 GoogleTest 1.17.0 生成的基础 `tina_tests`、Legacy ON
构建图中的 `tina_legacy_tests`，以及按需构建的 adapter 专项测试。当前精确数量和平台矩阵只在
[测试文档](docs/testing.md)维护。

当前 Legacy UI 已具备 generation `NodeId`、Pointer Capture、Focus/Tab、KeyDown/KeyUp 的 Capture/Target/Bubble 路由、方向键空间焦点导航、可嵌套 Modal Focus Scope、Button 的 Enter/Space pressed/release 生命周期与单次激活、每窗口 Theme/DPI、嵌套 Clip、通用 `UIScrollView`、十万行范围计算的 ListView 虚拟化，以及 Windows 原生 IME preedit/composition。每个 Button action 具有独立重入保护、异常恢复和回调自销毁安全性；routed click 目标在路由中删除后通过 generation `NodeId` 立即失效。GLFW 标准手柄的 D-pad/左摇杆可驱动空间导航，A/B 映射为 Accept/Cancel；摇杆带回滞并支持方向长按重复，语义导航仍服从最上层 Modal Focus Scope。Dialog 不再订阅全局键盘事件，Escape 仅在焦点控件未消费时沿祖先链处理；Scene 会在 `onEnter`/`onResume` 交互前激活对应 UI roots。窗口与基础输入只使用 GLFW；IME 通过 Win32 IMM32 补充，不引入其他窗口或输入库。测试数量和平台验证结果只在 [测试文档](docs/testing.md) 中维护。

上段的 `NodeId` 是当前 Legacy 类型名；vNext Game SDK 使用职责更明确、并在所有构建校验
owner `WindowId` 的 `UINodeId`，两者不能被文档混称为已完成迁移。

不了解整体设计时，先阅读 [设计导读](docs/design.md)，再阅读[游戏程序与状态接口](docs/gameplay.md)、[高性能 UI](docs/ui.md)和[后端无关渲染](docs/rendering.md)，或从 [文档索引](docs/README.md) 进入各模块；
候选/已接受/后置状态以 [设计冻结清单](docs/design-freeze.md)与
[ADR 索引](docs/adr/README.md)为准。所有源码、文档、日志和配置统一使用 UTF-8，MSVC
强制启用 `/utf-8`。
