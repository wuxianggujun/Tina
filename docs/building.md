# 构建与运行

## 环境要求

| 当前/Legacy 门禁 | 要求 | 当前验证环境 |
| --- | --- | --- |
| Windows | Visual Studio 2026 x64、CMake、`VCPKG_ROOT` | VS 18.4.3、MSVC 19.50.35717、CMake 4.2.3 |
| Linux | GCC 或 Clang、Ninja、CMake、`VCPKG_ROOT` | Ubuntu 22.04 / GCC 13.4 与 Clang 22.1.8 已通过 vNext Null、GLFW 与 Desktop bgfx X11 图 |

| vNext 正式目标 | 最低目标 |
| --- | --- |
| Windows | Visual Studio 2026 / MSVC 19.50，统一 C++23，CMake Preset |
| Linux GCC | GCC 13+，统一 C++23，Ninja |
| Linux Clang | Clang 22.x + GCC/libstdc++ 15.x，统一 C++23；独立 ASan/UBSan preset |

Tina 自有 target 已统一请求 `cxx_std_23`，MSVC 继续强制 `/utf-8` 与 `/Zc:__cplusplus`。MSVC 19.50 是当前完整验证基线，CMake 将 `cxx_std_23` 映射为该工具链的 `stdcpplatest`。使用 Preset 时 CMake 至少需要 3.25。

当前 Windows vNext 实测使用 Visual Studio 2026 18.4.3、MSVC 19.50.35717 与
`D:\Programs\CMake\bin\cmake.exe` 4.2.3；该 CMake 目录已在 Machine PATH 中，命令可直接写成
`cmake`。

Ubuntu 22.04 自带的 GCC 11/Clang 14 与 CMake 3.22 不构成 vNext 正式 C++23 门禁。
`linux-gcc13-vnext` 固定 GCC 13；`linux-clang22-vnext` 通过
`cmake/toolchains/linux-clang22-libstdcxx15.cmake` 同时固定 Clang 22.x 与 GCC/libstdc++ 15.x。
原因是只指定 Clang 可执行文件不会固定 C++ 标准库：旧 libstdc++/libc++ 组合不能同时稳定提供
`std::expected` 与 `std::move_only_function`。两条 Linux Null 门禁均已用 CMake 4.2.3、Ninja
1.13.2、GoogleTest 1.17.0 实际完成；Clang 配置同时通过 ASan/UBSan。

当前源码不使用 C++ Modules，因此根构建显式关闭 CMake 的 Modules dependency scan，避免
Clang 安装未附带 `clang-scan-deps` 时产生无关失败；未来真正采用 Modules 必须单独建立 ADR、
依赖扫描与 BMI/ABI 门禁后再开启。

```powershell
cmake --version
cmake --list-presets
```

依赖由固定 vcpkg baseline 解析。项目窗口与基础输入只使用 GLFW，不需要 SDL/SDL3。
当前旧 target 仍需要源码形式的 EASTL/EABase；vNext target 不再依赖它们，只有 Legacy
零引用并通过完整门禁后才从仓库与构建中删除。xxHash 继续作为窄 Hash adapter 的私有后端。

## Windows Debug

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-debug --target Tina tina_tests tina_legacy_tests
out\build\windows-msvc\bin\Debug\tina_tests.exe
out\build\windows-msvc\bin\Debug\tina_legacy_tests.exe
```

## Windows vNext 最小构建

该 preset 关闭 Legacy、bgfx/shader 和 vcpkg 默认 feature，构建当前 vNext M6-A/M7-A/M7-B1 与
M7-C1b/C1c-a/C1c-b1/C1c-b2/C1c-b3a/C1c-b3b/C1c-b3c/C1c-b3d1/C1c-b3d2/C1c-b3e/M8/M9-A 的 `tina_core`、`tina_platform`、
`tina_task`、`tina_render`、`tina_runtime`、`tina_scene`、`tina_ui`，以及最新 SolidFill paint、Render DisplayList、D2 scoped
`setBoxPaint()` 与
`tina_ui_render_integration`、
直接 GoogleTest 门禁与 Null/2D/3D extraction infrastructure 样例：

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests tina_scene_tests tina_render_scene_tests tina_sample_null tina_sample_2d_infrastructure tina_sample_3d_extraction
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_ui_render_integration_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_render_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_sample_null.exe --frames=300
out\build\windows-msvc-vnext\bin\Debug\tina_sample_2d_infrastructure.exe --frames=300
out\build\windows-msvc-vnext\bin\Debug\tina_sample_3d_extraction.exe --frames=300
```

### M8-A Scene World/Transform

`tina_scene_tests` 是独立 GoogleTest executable；当前 Windows MSVC Debug/Release 均19/19，覆盖固定容量
`Scene::World`、generation/owner `EntityId`、Local/World Transform 层级传播、keep-world/keep-local、
父销毁/显式子树销毁、PMR 回滚、宽/深树、overflow/shear、四元数与 owner-thread 读写。它当前只验证
standalone Scene foundation；阶段末 command buffer、Scene component integration、Asset 和正式 2D
产品样例仍属于后续 M8/M10/M11 切片。M8-B 的 RenderScene builder 使用独立测试与 Headless/Null
infrastructure sample，不代表可见 Sprite、bgfx Sprite pass 或正式产品门禁：

```powershell
cmake --build --preset windows-vnext-debug --target tina_scene_tests
out\build\windows-msvc-vnext\bin\Debug\tina_scene_tests.exe --gtest_color=yes

cmake --build --preset windows-vnext-release --target tina_scene_tests
out\build\windows-msvc-vnext\bin\Release\tina_scene_tests.exe --gtest_color=yes

cmake --build --preset windows-vnext-debug --target tina_render_scene_tests
out\build\windows-msvc-vnext\bin\Debug\tina_render_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_sample_2d_infrastructure.exe --frames=300
```

### M9-A 3D CPU/Null extraction

`tina_sample_3d_extraction` 与 `tina_render_scene_tests` 属于 vNext Null 图，不需要 GLFW、bgfx、shader
或 GPU。当前 Debug 直接结果为 RenderScene 22/22 和样例300帧；样例每帧记录4 submitted、3 visible、
1 culled、2 batches，并在1280x720切到800x800时验证一次 aspect 更新与退出资源归零。它不显示 Cube；
M9-B 可见 `tina_sample_3d_infrastructure` 和 M10 Cooked glTF `tina_sample_3d` 尚未实现。

```powershell
cmake --build --preset windows-vnext-debug --target tina_render_scene_tests tina_sample_3d_extraction
out\build\windows-msvc-vnext\bin\Debug\tina_render_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_sample_3d_extraction.exe --frames=300
```

当前最小图的唯一第三方测试依赖是 `tests` manifest feature 提供的 GoogleTest 1.17.0；
`TINA_BUILD_TESTING=OFF` 且不启用该 feature 时，vcpkg 安装图也不包含 GTest。它不进入或链接 GLFW、bgfx、EASTL、
EnTT、FreeType、miniaudio、Box2D、xxHash 或 SDL/SDL3。`tina_sample_null` 只组合 Headless Platform、
Disabled TaskSystem 与 NullRenderDevice。

## Windows vNext UI 树、布局、命中、paint 与 DisplayList bridge

`tina_ui` 仍只依赖 Core/Platform；`tina_render` 不依赖 UI；只有
`tina_ui_render_integration` PUBLIC 依赖二者。门禁分别直接运行独立 GoogleTest executable：

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug --target tina_ui_tests
out\build\windows-msvc-vnext\bin\Debug\tina_ui_tests.exe --gtest_color=yes

cmake --build --preset windows-vnext-release --target tina_ui_tests
out\build\windows-msvc-vnext\bin\Release\tina_ui_tests.exe --gtest_color=yes

cmake --build --preset windows-vnext-debug --target tina_ui_render_integration_tests
out\build\windows-msvc-vnext\bin\Debug\tina_ui_render_integration_tests.exe --gtest_color=yes

cmake --build --preset windows-vnext-release --target tina_ui_render_integration_tests
out\build\windows-msvc-vnext\bin\Release\tina_ui_render_integration_tests.exe --gtest_color=yes
```

截至 dirty-subtree b4a，Windows 11 / MSVC 19.50 Debug 的 UI
门禁为115/115；Windows Release、Linux GCC 13.4 与 Clang 22 sanitizer 的 UI
历史门禁均为81/81：
16项覆盖 generation tree/ownership，
23项覆盖事务式 Flex-lite layout，15项覆盖固定 PMR 容量、`Ignore`/`Targetable`、route ancestry、
同一 `UICommittedHitView` 内严格递增且唯一的 paint ordinal、双缓冲 view、当时的三-snapshot事务回滚、
stale generation、50,000节点与 PMR 回收；5项覆盖反向 paint-order 查询、Ignore 穿透、world/clip
半开边界、非有限坐标 miss、snapshot binding、visited count 与300次查询零新增 UI PMR allocation；
16项覆盖 fixed-capacity synthetic listener route、Capture/Target/Bubble 顺序、stop/consume、路由中
add/reset/destroy 安全失效、off-thread deferred reset、route/commit reentrancy guard、错误 context
销毁 death test、300次 route 零新增 supplied UI PMR allocation 与递归 route 拒绝；3项覆盖
root-scoped `UITreeUpdater` 子节点创建、跨 root 拒绝与失效 root；b3e 新增3项覆盖 claim 合并、非法值
与无命中。最新 SolidFill paint 再新增11项，Windows Debug/Release、Linux GCC 13.4 与 Linux Clang 22
sanitizer 的 `tina_ui_tests` 均为92/92。Render builder 的11项测试已随 Windows Debug 与两条 Linux图的
`tina_tests` 205/205 通过；D0 后 Windows Debug/Release 基础测试增至207/207。独立 bridge 的12项在 Windows
Debug/Release 与两条 Linux 图均通过，覆盖 logical→framebuffer
outward rounding/clamp、冗余/空 clip、严格 paint order、容量/输入失败与完整 transaction rollback。
本轮 dirty-subtree b4a 又把独立 UI 门禁增至115/115，新增 clean sibling reuse、parent/viewport full rebuild、
Auto ancestor、Collapsed subtree 与 candidate failure 回退测试；Button default action 的前序109项覆盖默认
`Targetable`、primary Pointer pressed/activation、`preventDefaultAction()`、set/replace/clear、cancel/reset 与 PMR 复用。
这些结果不证明持久 Pointer Capture、Focus/Modal、Button Keyboard/Gamepad activation、Image/Text/Glyph、owning
Runtime packet/FramePin 或完整可见 Widget 已完成；D1/D2 的可见 SolidFill panel 由 Desktop bgfx 门禁单独覆盖。

## Windows vNext Runtime→UI producer、primary-window owner、layout coordinator、scoped Game SDK UI access 与 Pointer Button claim bridge

M7-C1c-b3b/b3c/b3d1/b3d2/b3e 使用独立 `tina_runtime_ui_tests`，避免把 vNext `UIContext` 与 Legacy ON 图中的不兼容
`Tina::UI` 定义放进同一最终二进制：

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug --target tina_runtime_ui_tests
out\build\windows-msvc-vnext\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes

cmake --build --preset windows-vnext-release --target tina_runtime_ui_tests
out\build\windows-msvc-vnext\bin\Release\tina_runtime_ui_tests.exe --gtest_color=yes
```

已验证的 C1c-b3c Windows MSVC 19.50 / CMake 4.2.3 Debug/Release 基线均为20/20。其中12项 producer 用例验证只路由
Move/Button/Wheel，reset/cancel/非 Pointer 保留 raw ordinal hole、该历史切片的 claims 为 canonical `None`，以及双预分配
PMR bitset 在300帧共用 supplied PMR 时 allocation count 不增长。注入的 `memory_resource` 必须比 producer
活得更久。失败用例先产生1次 listener side effect，后续 route path capacity 失败；旧 published view 保持，
attempted watermark 推进，同帧 retry 被拒且 callback 仍为1。

另8项 owner 用例覆盖 headless lazy bind、同一 primary `WindowId` 复用、绑定后窗口消失或 generation
替换失败、metrics/content scale/minimize 变化不重绑、幂等 shutdown/停止后拒绝、错线程拒绝，以及 PMR allocation
失败不发布 binding 并可重试。C1c-b3c 的正式 `EngineHost` 已在 Platform event dispatch 之后、
`ActionMapper` 之前选择该 Context 并调用 producer；Context 在 Render → Task → Platform → Clock 模块关闭前销毁。

C1c-b3d1 继续使用同一 target 覆盖 focused UI capacity validator、`EngineConfig` pre-factory rejection、
primary owner 使用配置容量，以及 Runtime-private layout coordinator 的 Headless no-op、logical extent、
owner/identity、严格递增 frame id 和事务失败边界。正式帧在 `updateUI` 后、Render submit 前至多尝试一次
`commitLayout()`；失败阻断 Render 且本帧 attempt 已消费。b3d1 在 owner/coordinator 侧新增9项用例，
该历史切片在 Windows MSVC 19.50 Debug/Release、Linux GCC 13.4 与 Clang 22 sanitizer 均直接通过29/29；
上面的20/20只保留为 b3c 历史基线。

C1c-b3d2 在同一 target 增加 startup primary-window metrics seed、显式 startup bind、`onEnter`
`PrimaryWindowUIRootBuilder`、`updateUI` `PrimaryWindowUITreeUpdater`、phase epoch expiry、sticky 首错、
跨线程拒绝与无分配 `abortPhase()` 回滚验证。Windows MSVC 19.50 Debug/Release、Linux GCC 13.4 与
Clang 22 sanitizer 均直接通过 `tina_runtime_ui_tests` 42/42。

C1c-b3e 在同一 target 增加 held primary Pointer Button claim bridge。Move/Button/Wheel listener 都可请求接管
仍 held 的 primary pointer button；Runtime 按 final Platform snapshot 过滤已 release/cancel/reset 或非
primary 的 claim、对重复 claim 去重，capacity 失败时不发布新 claims。`ActionMapper` 按 final snapshot
注入这些 claims；同帧 PointerDown 触发的 claim 即使事件未 consume，也会拦截 Gameplay。Windows MSVC 19.50
Debug/Release、Linux GCC 13.4 与 Clang 22 sanitizer 均直接通过 `tina_runtime_ui_tests` 46/46。
Key/Gamepad/axis claims 仍后置。

当前 Game SDK 已能在 `onEnter` 创建 retained root、在 `updateUI` 通过绑定 root 的 updater 修改 subtree，
且 D0 已在 layout/paint commit 后、Render submit 前通过 Runtime-private coordinator 构建
primary-window UIDisplayList，并把它作为 `RenderFrame` 的 submit-call-local borrow 交给 backend。
Headless、0 framebuffer 与 suspended surface 发布空 list；构建失败不保留旧 publication 或截断 list。
D2 又把 `setBoxPaint()` 暴露到 scoped `PrimaryWindowUITreeUpdater`，并覆盖 phase expiry、
wrong-context/stale-generation sticky failure。D2 后 Windows MSVC 19.50 / CMake 4.2.3 Debug/Release 的
`tina_runtime_ui_tests` 均为53/53；本轮 Button default action Windows Debug 增至60/60。正式样例仍没有
Widget 文本、Button Keyboard/Gamepad activation、Focus/Capture/Modal、Text/Glyph 或完整产品 UI；最小可见
SolidFill panel 由 Desktop bgfx 样例单独证明。该 target 直接运行
GoogleTest，不使用 CTest。

## Windows vNext GLFW Platform 与 Desktop bgfx

GLFW adapter 使用独立 build tree 和 vcpkg `platform-glfw` feature，不改变上面的 Null 依赖闭包。
它创建 `GLFW_NO_API` 窗口、发布 M7-B1 WindowSurface snapshot/lease，并组合 NullRender。M7-B2
私有 bgfx core 使用单独 preset 构建；D1 在该私有 backend 中加入 build-tree shaderc 生成的
glsl/spv/dxbc SolidQuad embedded shader、transient 32-bit indexed UI geometry、Sequential view、top-left
framebuffer ortho、premultiplied blend 和 scissor。Desktop bootstrap 通过
`Tina::Desktop::CreateEngine(config)` 私有组合 `SteadyClock + GLFW WindowSurface + DisabledTaskSystem + bgfx`。
`tina_sample_desktop` 不带参数时默认运行300帧真实 GPU，并创建1个 retained root 与4个 painted panel：

```powershell
cmake --preset windows-msvc-vnext-platform
cmake --build --preset windows-vnext-platform-debug `
  --target tina_tests tina_platform_glfw_tests tina_sample_platform
out\build\windows-msvc-vnext-platform\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-platform\bin\Debug\tina_platform_glfw_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-platform\bin\Debug\tina_sample_platform.exe `
  --frames=300 --frame-delay-ms=0

cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_tests tina_runtime_ui_tests tina_platform_glfw_tests tina_render_bgfx_tests tina_sample_desktop
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_platform_glfw_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_bgfx_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_desktop.exe --frames=1200 --frame-delay-ms=0
```

Release 必须在 Debug build 结束后串行执行：

```powershell
cmake --build --preset windows-vnext-platform-release `
  --target tina_tests tina_platform_glfw_tests tina_sample_platform
out\build\windows-msvc-vnext-platform\bin\Release\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-platform\bin\Release\tina_platform_glfw_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-platform\bin\Release\tina_sample_platform.exe `
  --frames=300 --frame-delay-ms=0

cmake --build --preset windows-vnext-bgfx-release `
  --target tina_tests tina_runtime_ui_tests tina_platform_glfw_tests tina_render_bgfx_tests tina_sample_desktop
out\build\windows-msvc-vnext-bgfx\bin\Release\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Release\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Release\tina_platform_glfw_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Release\tina_render_bgfx_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Release\tina_sample_desktop.exe --frames=300 --frame-delay-ms=0
```

Windows 构建会把 GLFW runtime DLL 复制到对应 `bin/<Config>`。样例默认零延迟300帧；截图/长跑门禁可显式
使用 `--frames=1200 --frame-delay-ms=0`，该路径不属于 benchmark。D1 的 shader 通过 build tree 内
`bgfx::shaderc` 生成，不把 generated/cooked header 提交进源码。
最新 Windows 门禁使用 Visual Studio 2026 / MSVC 19.50.35717 与 CMake 4.2.3。D2 的 Debug/Release
均直接通过基础207/207、独立 UI92/92、独立 Runtime→UI53/53、UI→Render bridge12/12与Null样例300帧；
前序 D2 另通过真实 D3D11 Intel Iris Xe 的 `tina_sample_desktop`：Debug 1200帧截图检查，Release 300帧输出
`clean status ok`；Debug 退出时的 `RefCount is 3 (expected 0)` 是已记录第三方 debug layer 提示，
不能单独作为 Tina 资源泄漏结论。当前 M8-B Debug adapter 复验通过 GLFW专项26/26、
GLFW+Null样例300帧、bgfx专项16/16与 Desktop样例连续3次各300帧；iconify 自动化验证最小化时
沿用最后有效 logical extent，并保持 framebuffer `0x0` 让 surface 进入 Suspended。上一
C1c-b3a WindowSurface GLFW样例1800帧仍作为历史证据。
`TINA_BUILD_TESTING=OFF` 的 production-style GLFW样例300帧
同样属于早期门禁，用来证明测试 target 关闭后样例仍能运行。
当前 Desktop smoke 证明无纹理 SolidFill UI panel、alpha blend、right-edge scissor、root 回收和 bgfx surface
创建/提交/关闭链路通过；不代表 Scene、Text/Glyph、完整 Widget、Pass Scheduler/submission ticket 已完成，
也不宣称 resize→render、restore 往返或完整交互自动化通过。本轮没有重新截图，画面正确仍引用前序
D2 可见证据。

## Windows vNext Release

vNext 已提供独立 Release build preset，仍复用同一个 Visual Studio 多配置构建目录：
同一 `windows-msvc-vnext` build tree 的 Debug/Release `cmake --build` 必须串行执行，不能由两个
MSBuild 进程并发驱动同一生成图；配置输出目录虽然隔离，共享的生成状态仍可能发生争用。

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-release `
  --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests tina_render_scene_tests tina_sample_null tina_sample_2d_infrastructure tina_sample_3d_extraction
out\build\windows-msvc-vnext\bin\Release\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Release\tina_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Release\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Release\tina_ui_render_integration_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Release\tina_render_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Release\tina_sample_null.exe --frames=300
out\build\windows-msvc-vnext\bin\Release\tina_sample_2d_infrastructure.exe --frames=300
out\build\windows-msvc-vnext\bin\Release\tina_sample_3d_extraction.exe --frames=300
```

Legacy Release 仍可使用原有多配置构建目录：

```powershell
cmake --build out\build\windows-msvc `
  --config Release `
  --target Tina tina_tests tina_legacy_tests `
  --parallel 2
out\build\windows-msvc\bin\Release\tina_tests.exe
out\build\windows-msvc\bin\Release\tina_legacy_tests.exe
```

Debug、Release 可执行文件和 app-local DLL 分别位于 `bin/Debug`、`bin/Release`，不能混用 GTest 或其他运行时 DLL。

## Linux Debug

在 Linux 或 WSL 的仓库目录中执行：

```bash
cmake --preset linux-ninja
cmake --build --preset linux-debug --target Tina tina_tests tina_legacy_tests
./out/build/linux-ninja/bin/tina_tests --gtest_color=no
./out/build/linux-ninja/bin/tina_legacy_tests --gtest_color=no
```

只做 vNext 无 GPU 的编译、链接和单元测试门禁时，使用独立 `linux-gcc13-vnext` 目录，避免污染 Legacy 可运行构建：

```bash
cmake --preset linux-gcc13-vnext
cmake --build --preset linux-gcc13-vnext-debug \
  --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests tina_sample_null
./out/build/linux-gcc13-vnext/bin/tina_tests --gtest_color=no
./out/build/linux-gcc13-vnext/bin/tina_ui_tests --gtest_color=no
./out/build/linux-gcc13-vnext/bin/tina_runtime_ui_tests --gtest_color=no
./out/build/linux-gcc13-vnext/bin/tina_ui_render_integration_tests --gtest_color=no
./out/build/linux-gcc13-vnext/bin/tina_sample_null --frames=300
```

Clang 22 + libstdc++15 的普通与 Sanitizer 门禁使用独立目录。主机需同时提供 `clang-22`、
`clang++-22` 与 `g++-15`；chainload toolchain 会验证 major version 并将 Clang 明确绑定到 GCC 15
安装目录：

```bash
cmake --preset linux-clang22-vnext
cmake --build --preset linux-clang22-vnext-debug --target tina_tests tina_sample_null
./out/build/linux-clang22-vnext/bin/tina_tests --gtest_color=no

cmake --preset linux-clang22-vnext-sanitize
cmake --build --preset linux-clang22-vnext-sanitize-debug \
  --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests tina_sample_null
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
LSAN_OPTIONS=exitcode=23 \
./out/build/linux-clang22-vnext-sanitize/bin/tina_tests --gtest_color=no
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
LSAN_OPTIONS=exitcode=23 \
./out/build/linux-clang22-vnext-sanitize/bin/tina_ui_tests --gtest_color=no
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
LSAN_OPTIONS=exitcode=23 \
./out/build/linux-clang22-vnext-sanitize/bin/tina_runtime_ui_tests --gtest_color=no
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
LSAN_OPTIONS=exitcode=23 \
./out/build/linux-clang22-vnext-sanitize/bin/tina_ui_render_integration_tests --gtest_color=no
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
LSAN_OPTIONS=exitcode=23 \
./out/build/linux-clang22-vnext-sanitize/bin/tina_sample_null --frames=300
```

这些输出不包含 Legacy 产品、窗口、真实渲染后端或 cooked shader，只用于 Headless 生命周期验证，
不能作为游戏产品或发布包。最新 paint/DisplayList/bridge 切片中，GCC 13.4 直接通过基础
`tina_tests` 205/205、`tina_ui_tests` 92/92、`tina_runtime_ui_tests` 46/46、
`tina_ui_render_integration_tests` 12/12与Null样例300帧；Clang 22.1.8 + libstdc++15.2 在
ASan/UBSan/LSan 下通过相同门禁且无 sanitizer 诊断。
但仍不能用 Ubuntu 22.04 的旧工具链降级冒充正式结果。

## Linux vNext GLFW Platform

X11 adapter 使用独立 platform preset。基础测试无需显示环境；GLFW 专项与样例在 CI/WSL 中应由
`xvfb-run` 提供隔离 X server：

```bash
cmake --preset linux-gcc13-vnext-platform
cmake --build --preset linux-gcc13-vnext-platform-debug \
  --target tina_tests tina_platform_glfw_tests tina_sample_platform
./out/build/linux-gcc13-vnext-platform/bin/tina_tests --gtest_color=no
xvfb-run -a ./out/build/linux-gcc13-vnext-platform/bin/tina_platform_glfw_tests --gtest_color=no
xvfb-run -a ./out/build/linux-gcc13-vnext-platform/bin/tina_sample_platform \
  --frames=300 --frame-delay-ms=0

cmake --preset linux-clang22-vnext-platform-sanitize
cmake --build --preset linux-clang22-vnext-platform-sanitize-debug \
  --target tina_tests tina_platform_glfw_tests tina_sample_platform
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
./out/build/linux-clang22-vnext-platform-sanitize/bin/tina_tests --gtest_color=no
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
LSAN_OPTIONS=suppressions="$PWD/cmake/sanitizers/lsan-x11.supp":print_suppressions=1 \
xvfb-run -a ./out/build/linux-clang22-vnext-platform-sanitize/bin/tina_platform_glfw_tests \
  --gtest_color=no
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
LSAN_OPTIONS=suppressions="$PWD/cmake/sanitizers/lsan-x11.supp":print_suppressions=1 \
xvfb-run -a ./out/build/linux-clang22-vnext-platform-sanitize/bin/tina_sample_platform \
  --frames=300 --frame-delay-ms=0
```

基础 `tina_tests` 故意不设置 `LSAN_OPTIONS`。只有初始化 GLFW/X11 的两个进程使用
`cmake/sanitizers/lsan-x11.supp` 中唯一的 `_XimOpenIM` 精确规则；它对应 Ubuntu 22.04 libX11
关闭 XIM 后保留的 allocation（专项测试12次4896 B，样例1次408 B），不能扩展为宽泛 suppression。

以下是前序 M7-B1 Linux 运行门禁。GCC 13.4 X11 已通过基础测试183/183、GLFW专项22/22、
Null样例300帧与GLFW样例300帧。Clang 22.1.8 X11 的 ASan/UBSan/LSan 图已通过基础测试
183/183（无 suppression）、GLFW专项22/22、Null样例300帧与GLFW样例300帧；其中
`_XimOpenIM` 精确 suppression 仅在 X11 专项测试命中12次/4896 B、GLFW样例命中1次/408 B。

M7-B2 Desktop 使用上述 platform preset 加 `-DTINA_BUILD_RENDER_BGFX=ON`，再直接构建/运行
`tina_tests`、`tina_platform_glfw_tests`、`tina_render_bgfx_tests` 与 `tina_sample_desktop --frames=300`，
仍不使用 CTest。GCC 13.4 已通过183/183、22/22、11/11和 Desktop 300帧；Clang 22.1.8 +
ASan/UBSan/LSan 通过相同门禁。Clang 基础/bgfx测试无 suppression；X11精确规则在GLFW专项命中
12次/4896 B、Desktop命中1次/408 B。Clang Desktop 经 bgfx 选择 Vulkan，但当前 WSL2 adapter 是
llvmpipe 软件实现，因此只证明 Linux Vulkan/backend 生命周期，不代表硬件 GPU 性能；resize、最小化、
恢复的真实自动化仍未覆盖。

Wayland 使用单独的 `linux-gcc13-vnext-platform-wayland` 与
`linux-clang22-vnext-platform-wayland-sanitize` preset，它们同时启用 manifest 的
`platform-glfw;wayland` feature。GCC 13 双后端产物已在受控环境完成两条运行门禁：

- Xvfb 为 Weston 9 `x11-backend` 提供 `wl_seat`，启动嵌套 Weston 后从子进程环境移除
  `DISPLAY`，并断言 `glfwGetPlatform() == GLFW_PLATFORM_WAYLAND`；基础测试183/183、GLFW专项
  22/22 与样例300帧通过；
- 同一双后端产物随后移除 `WAYLAND_DISPLAY`，在 Xvfb 下强制选择 X11，基础测试183/183、
  GLFW专项22/22与样例300帧通过。

运行门禁需要真实 Wayland session，或像上述 Weston 一样显式提供 `wl_seat` 的受控
compositor。纯 Weston headless 在没有 `wl_seat` 时会命中项目锁定 GLFW 3.4 的已知
初始化崩溃；这不是 Tina 回归，也不表示 Tina 支持无 seat compositor。

Clang 22 的同类双后端 sanitizer 产物也已完成门禁：

- 基础 `tina_tests` 在 ASan/UBSan/LSan 下不使用任何 suppression，183/183通过，Null样例300帧通过；
- 带 `wl_seat` 的嵌套 Weston 强制 Wayland 后，GLFW专项22/22与样例300帧通过，
  `_XimOpenIM` 精确 suppression 匹配计数为0；
- 同一产物在 Xvfb 下强制 X11 后，GLFW专项22/22与样例300帧通过；此路径仅
  精确抑制 libX11 `_XimOpenIM`，专项12次/4896 B、样例1次/408 B。

Sanitizer 插桩覆盖 Tina 自有 target；由 vcpkg 提供的第三方 GLFW 本身未被
sanitizer 插桩。因此该门禁证明 Tina 代码、边界交互和生命周期未被 sanitizer 报错，
不等于对 GLFW 内部路径进行了完整插桩检查。配置/构建成功不能替代运行结果，
最终状态记录在[测试文档](testing.md)。

## vNext 目标 Preset

`windows-msvc-vnext`、`windows-msvc-vnext-platform`、`linux-gcc13-vnext`、
`linux-gcc13-vnext-platform`、`linux-gcc13-vnext-platform-wayland`、`linux-clang22-vnext`、
`linux-clang22-vnext-platform`、`linux-clang22-vnext-sanitize`、
`linux-clang22-vnext-platform-sanitize` 与 `linux-clang22-vnext-platform-wayland-sanitize` 已落地。
下列性能名称仍是后续设计契约：

| Preset | 优化/插桩 | 用途 |
| --- | --- | --- |
| `windows-bench` / `linux-bench` | Release、Trace none、VSync off、固定 worker | 正式 `tina_bench` |
| `windows-profile` / `linux-profile` | 与 Bench 相同优化/CRT/assert/LTO、保留符号、Tracy on | 定位同 workload 回退 |

Bench/Profile 不能只用两个默认 CMake build type 猜测“优化差不多”；最终 flags 和依赖
fingerprint 写入 benchmark JSON。Tracy 只由 Profile preset 的 optional vcpkg feature解析，
普通构建和发行包不查找或打包 Tracy。

## Release 冒烟

```powershell
# 主菜单 + 中文 UI
out\build\windows-msvc\bin\Release\Tina.exe --smoke-frames=300

# 独立 UI 场景
out\build\windows-msvc\bin\Release\Tina.exe --smoke-ui --smoke-frames=300

# 完整 2D 场景
out\build\windows-msvc\bin\Release\Tina.exe --smoke-game --smoke-frames=300

# 透视相机 + 深度测试 + 静态索引 Cube
out\build\windows-msvc\bin\Release\Tina.exe --smoke-3d --smoke-frames=300
```

四条命令返回 0 代表主循环与退出链路完成，不自动代表画面验收通过。3D 和 UI 仍应通过人工观察或稳定截图门禁确认。

## 常用选项

| 选项 | 默认值 | 用途 |
| --- | --- | --- |
| `TINA_BUILD_TESTING` | `ON` | 构建 vNext 基础 `tina_tests`；Legacy ON 时另建 `tina_legacy_tests`；UI、Runtime→UI、UI→Render integration 与 GLFW/bgfx adapter 使用各自独立专项测试 |
| `TINA_BUILD_SHADERS` | `ON` | 构建运行时 shader；关闭后只适合编译/链接门禁 |
| `TINA_BUILD_LEGACY` | `ON` | 迁移期构建现有游戏与旧模块；vNext preset 固定关闭 |
| `TINA_BUILD_PLATFORM_GLFW` | `OFF` | 构建私有 vNext GLFW Window/Input adapter；需启用 vcpkg `platform-glfw` feature，不改变 Game SDK 边界 |
| `TINA_BUILD_RENDER_BGFX` | `OFF` | 构建私有 vNext bgfx backend、build-tree shaderc UI shader 与 Desktop SolidQuad smoke，不改变 Game SDK 边界 |
| `TINA_BUILD_BENCHMARKS` | `OFF` | 后续构建独立 `tina_bench` |
| `TINA_ENABLE_SANITIZERS` | `OFF` | GCC/Clang Unix target 同时启用 ASan/UBSan；其他工具链配置时报错 |
| `TINA_BUILD_WAYLAND` | `OFF` | Linux Wayland 构建，需要对应 vcpkg feature |
| `TINA_AUTOUPDATE_SUBMODULE` | `OFF` | 是否自动更新源码依赖，日常构建应保持关闭 |
| `TINA_BUILD_DOCS` | `ON` | 当前为预留选项，尚未注册文档生成 target |

测试直接运行相应 GoogleTest executable，不注册 CTest。

vNext 后续还将增加 `TINA_PROFILE_BACKEND` 和 Tracy lock/memory 子选项；在真正加入 CMake 前只记录为目标，不能把表述当成当前可用命令。
完整依赖可见性、版本和许可证门禁见 [第三方依赖与版本治理](dependencies.md)。
