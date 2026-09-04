# 测试与验证

Tina 使用 GoogleTest 1.17.0。CMake 生成多个独立 executable，构建后逐个直接运行；项目不注册
CTest 测试。测试进程任一返回非0即失败。

## 基本规则

1. 先区分用户请求：只要求编译/构建/生成版本时属于所有平台通用的 `compile-only`，不得执行 GoogleTest、
   CTest、sample、smoke、产品/视觉 gate，也不得启动编译产物；用户明确要求测试后才适用下列测试范围规则。
2. 开发中的小功能或单一切片只构建最小受影响 target，并优先用 `--gtest_filter` 运行新增用例及其直接
   回归用例；存在精确 filter 时，不得默认运行整个 executable、完整产品 gate 或无关 backend 矩阵。
3. 一个垂直切片功能闭环后，只扩大到该切片直接影响的 executable 和必要 sample smoke。多个连续小切片
   命中同一测试图时，合并后集中扩大一次，不在每个提交上重复全套验证。
4. 只有 Backlog 大功能/里程碑关闭、共享基础设施或跨模块公开契约变更、release candidate，以及明确要求
   生成正式产品证据时，才运行完整 executable 集、sanitizer、跨平台或完整产品 gate。
5. Windows 多配置输出使用 `bin/Debug` 或 `bin/Release`，不能混用运行时 DLL。
6. 同一 Visual Studio build tree 的 Debug/Release 构建串行执行。
7. 日常门禁不使用 `--clean-first`，不删除 `out/build`。
8. 测试数量是易变证据；架构状态不以固定数量定义。
9. sample exit 0 只证明生命周期/结构化断言；画面正确必须另有 Visual 证据。
10. sanitizer、真实 backend、字体和 accessibility 结果不能由 Null 单元测试替代。
11. 多 worktree 开发先提交并合并功能分支，再在核心集成 worktree 的常驻 build tree 集中验证；不要在
   每个功能 worktree 重复构建 bgfx、shaderc 或完整产品图。
12. 不得跨 worktree 共用 `binaryDir`。Preset 路径基于 `${sourceDir}`，CMake cache 和生成项目绑定源码
   绝对路径；需要隔离验证时使用该 worktree 自己的临时 build tree。
13. 同一提交/工作树的跨环境验证先构建一次，再用 source fingerprint + binary hash 复用产物；secondary
   环境不得为了“确认编译”重复 configure/build/test。最后一个环境完成后回收专用 build tree、容器、
   helper/watchdog/窗口管理器和 agent，并在结果中记录资源状态。
14. `compile-only` 与 test gate 严格分离：前者最多 configure/build 一次最小 target，且
    `testRuns=0`、`sampleRuns=0`；不得运行 GoogleTest、sample、smoke 或 visual/platform gate。相同
    source/toolchain/target 指纹已有成功结果时不重复编译。
15. Linux compile-only、Docker、临时 worktree 和 gate 专用 build tree 默认是 ephemeral。取得退出码与首错
    记录后，无论成功失败都回收，不能为了未来可能运行的测试保留数十 GiB 产物。收尾必须报告 tree 已不存在，
    且 compiler/helper/container/volume/agent 均归零；核心集成常驻 tree 和外部共享 `VCPKG_ROOT` 不在清理范围。

## Editor 开发与验证节奏

本节是上述通用“小功能最小构建/定向测试”规则的 Editor 专项例外，冲突时以本节为准。Editor 按 Backlog
大功能、完整用户工作流或明确里程碑划定统一验证边界，不以单个控件、命令、状态字段、错误修复或源码提交
作为验证边界。

1. 大功能尚未完整闭环时，连续实现其中的小功能和小细节；不得为 Editor 新增或修改测试代码，也不得执行
   configure、build、GoogleTest、CTest、sample、smoke、Visual 或平台 gate。
2. 实施期间只进行源码/API 阅读、定向静态搜索、编译契约人工核对、`git status` 和 `git diff --check`；这些
   静态检查不产生编译通过、测试通过或产品 smoke 通过的结论。
3. 大功能全部实现、交互与错误状态收口、文档同步完成后，在用户授权 test gate 时复用核心常驻 build tree 集中
   执行一次受影响 Editor target 的增量 build、仓库已有的定向 Editor executable，以及该功能确实需要的最短
   2D/3D smoke；若用户只要求编译给其手动测试，则只 build 并交付 `TinaEditor.exe`，不得执行任何产物。
4. 统一 gate 发现问题时，先完成同批问题修复，再只重跑失败项或直接受影响项；不得在每个修复点后重新执行
   整套 gate。完整 UI/Runtime/product/cross-platform 矩阵只用于 Editor 里程碑、release candidate 或明确要求的
   正式产品证据。

Editor 功能完成不以新增测试数量为条件。下文的 Editor 构建、测试和 smoke 命令都是“大功能统一 gate”入口，
不是日常小切片实施步骤。

## Compile-only 结果口径

所有平台 compile-only 的通过条件都只有“指定最小 target 编译 exit 0”。它不产生测试通过、sample 生命周期、真实
backend、sanitizer 或视觉结论；报告中不得出现相应的 passed 表述。需要这些结论时另开 test gate，优先复用
同一 source fingerprint 与 binary hash，直接运行对应 executable，不再次 configure/build。

| 请求类型 | configure/build | GoogleTest / sample | 临时资源生命周期 |
| --- | --- | --- | --- |
| Windows/Editor 编译给用户手动测试 | 常驻 build tree 中只构建指定 target | 一律不运行，也不启动 `TinaEditor.exe` | 保留核心常驻 tree；报告产物与进程状态 |
| 仅验证 Linux 编译 | 每个 source/toolchain/target tuple 最多各一次 | 一律不运行，`testRuns=0 sampleRuns=0` | 记录编译结果或首错后立即回收 |
| 同一轮 Linux 编译 + test gate | 只在 primary 构建一次 | 直接复用刚生成且 hash 匹配的 binary，各运行一次 | 最后一个 gate 结束后立即回收 |
| secondary helper/container 验证 | 不 configure、不 build | 不重复 GoogleTest/workspace smoke，只运行该环境独有的 helper probe | probe 结束后回收该环境及共享临时 tree |

后续只要求确认同一 tuple 是否编译通过时，复用已经记录的成功结论，不重新创建 build tree。后续确实需要运行
此前未要求的测试、但临时 binary 已按规则回收时，应把它作为新的 test gate 排期；不能以此为理由让
compile-only tree 跨任务常驻，也不能把重建成本隐藏成 compile-only 的“再次验证”。

推荐的结果与资源记录：

```text
mode=compile-only tuple=<source/toolchain/target fingerprint>
configureRuns=0|1 buildRuns=0|1 testRuns=0 sampleRuns=0
buildTree=<path> buildTreeState=absent
stagingTreeState=absent installTreeState=absent consumerTreeState=absent temporaryDirectoriesState=absent
ownedTemporaryBytesBefore=<bytes> ownedTemporaryBytesAfter=0
compilerProcesses=0 linkerProcesses=0 helperProcesses=0 watchdogProcesses=0 windowManagerProcesses=0
containers=0 namedVolumes=0 oneShotImages=0 ownedBuildCacheBytesAfter=0 agents=0
retainedCaches=<owner + path-or-id + bytes + 保留原因；没有则为 none>
cleanupStatus=complete
```

若清理前发现 build tree 异常增长，先按 `out/build` 直接子目录记录占用和 owner，再只删除本轮 ephemeral tree；
不递归扫描整个仓库，不删除核心 Windows 增量 tree，不清空共享 vcpkg cache，不执行全局 Docker prune。上面任一
临时资源状态非零、`buildTreeState` 不是 `absent`，或存在未登记的保留目录，都必须将 gate 标记为收尾未完成，
不能报告“资源已释放”。无法查询的字段必须写 `unknown` 和原因，不能用 `0` 代替未知状态。

## Windows UI 快速门禁

UI 日常修改使用统一入口，脚本负责增量构建、直接运行 GoogleTest、传递 filter 和编译进程退出检查：

```powershell
# 完整 tina_ui_tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunMsvcUiTests.ps1

# 定向回归
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunMsvcUiTests.ps1 `
  -GTestFilter 'UITextPaintEmitterTests.*:*Text*:*Ime*:*Paint*'

# 已确认 binary 对应当前源码时，只运行测试
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunMsvcUiTests.ps1 `
  -SkipBuild -GTestFilter 'UITextPaintEmitterTests.*'

# UI-MOTION-002 focused regression gate (build the affected targets first, then
# run the UI, Runtime facade, and workload assertions).
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunMsvcUiTests.ps1 `
  -SkipBuild -GTestFilter 'UIMotionTests.*'
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_runtime_ui_tests.exe `
  --gtest_filter='PrimaryWindowUICapabilityTest.TimelineFacadeOwnsDefinitionPlaybackAndExpiry'
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench_tests.exe `
  --gtest_filter='UIBenchmarkWorkloadsTests.*Timeline*'
```

### UI-DIALOG-001 集中 gate

Dialog storage/API、Runtime facade、Editor 与 sample consumer、测试和文档全部闭环后，复用常驻
`windows-msvc-vnext-bgfx-product-2d` tree 一次性增量构建，不在内部迁移切片重复 configure/build/test：

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_ui_tests tina_runtime_ui_tests tina_editor_app_tests tina_editor_desktop `
           tina_sample_ui_showcase tina_sample_desktop_shell --parallel 2 -- /nr:false

out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_tests.exe `
  --gtest_filter='UIDialogTest.*:UIComponentProfileTest.*Dialog*'
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_runtime_ui_tests.exe `
  --gtest_filter='PrimaryWindowUICapabilityTest.*Dialog*'
```

随后直接运行完整 `tina_ui_tests.exe`、`tina_runtime_ui_tests.exe` 与 `tina_editor_app_tests.exe`，再执行
Showcase、Desktop Shell 以及 Editor 2D/3D 的既有最短 auto-demo smoke。必须分别验证初始 closed、intent/commit
边界、focus 进入/恢复、幂等、single-open 冲突、wrong-root/stale/generation 清理、dirty capacity 失败原子性、
Runtime phase expiry，以及三个产品 consumer 的最终 Dialog closed 证据。

### UI-GRID-COLLECTIONS 集中 gate

VirtualGridView/DataGrid 与 Editor consumer 的源码、测试和文档全部闭环后，复用常驻
`windows-msvc-vnext-bgfx-product-2d` tree 做一次集中增量构建；不得在内部小切片重复 configure/build/test：

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_ui_tests tina_runtime_ui_tests tina_editor_app_tests tina_editor_desktop `
  --parallel 2 -- /nr:false

out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_tests.exe `
  --gtest_filter='UIVirtualGridViewTest.*:UIDataGridTest.*'
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_runtime_ui_tests.exe `
  --gtest_filter='UIInputRouteProducerTest.*Grid*:PrimaryWindowUICapabilityTest.DataGridFacade*'
```

随后直接运行完整 `tina_ui_tests.exe`、`tina_runtime_ui_tests.exe` 与既有 `tina_editor_app_tests.exe`，并执行
`TinaEditor.exe --auto-demo --frames=70 --frame-delay-ms=0 --workspace=2d|3d` 两个最短产品 smoke。逻辑门禁覆盖
100k logical item/row、固定 item/column/row/cell pool、响应式列、双轴/单轴 scrollbar、Pointer/Keyboard/Gamepad、
disabled navigation、selection/semantics/paint、capacity overflow、失败原子 publication 和 warmup 后 PMR allocation
不增长。具体 pass 数只能在本轮 executable 实际运行后回写 Backlog，不能由测试源码数量推断。

### UI-PAINT-002-A 逐角圆角统一 gate

强类型四角 authoring 是跨 UI/Integration/Render 的公开契约改动。源码、测试、Showcase 与文档全部完成后，
复用常驻 `windows-msvc-vnext-bgfx-ui-freetype` Debug tree 一次性增量构建；不重新 configure、不使用 CTest：

```powershell
cmake --build --preset windows-vnext-bgfx-ui-freetype-debug `
  --target tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests `
           tina_render_bgfx_tests tina_bench_tests tina_sample_ui_showcase `
  --parallel 2 -- /nr:false

out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_ui_tests.exe
out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_runtime_ui_tests.exe
out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_ui_render_integration_tests.exe
out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_render_bgfx_tests.exe
out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_bench_tests.exe
out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_sample_ui_showcase.exe `
  --auto-demo --frames=120 --frame-delay-ms=0
```

逻辑断言覆盖四角 retained/canvas copy、rounded shadow/border/inset fill、descriptor/setter/Canvas/bridge 非法值
原子拒绝、逐角 anisotropic 投影与 checksum。`CornerRadius` direct transition 从非 uniform authored 值启动必须
返回 `InvalidStyle`，显式 keyframe0 的 scalar timeline 则允许启动并发布 uniform presentation。当前 Showcase smoke
还必须输出 `controls=24`、`componentProfiles=3`、`workbenchBands=5`、`asymmetricCornerProducts=3` 与
既有生命周期/图片/主题字段。rounded descendant clip、
backdrop/blur、per-corner Motion、新 shader/material 与跨 GPU/DPI golden 不由该 gate 证明。

2026-08-17 在上述常驻 tree 完成统一 gate，所有命令均 exit 0：

- 六个 target 增量 build：FastCtx job `j-ma0flp`；
- `tina_ui_tests` 672/672、`tina_runtime_ui_tests` 130/130、`tina_ui_render_integration_tests` 28/28；
- `tina_render_bgfx_tests` 111/111、`tina_bench_tests` 10/10；
- UI Showcase 120 帧 smoke：`status=ok`、`frames=120`、`controls=21`、`asymmetricCornerProducts=3`、
  `imageAtlasReleased=true`、`imageAtlasInvalidated=true`、`uiRootsCreated=1`、`uiRootsReleased=1`。

默认 topology 是 `windows-msvc-vnext-bgfx` Debug；没有 build tree 时自动 configure，已有 tree 由
CMake 在需要时自动 regenerate。脚本使用 `/nr:false`，不应再在调用处追加 `/m:2 /v:m`。

### TEXT-001 多行 / grapheme / Windows IME 矩阵

TEXT-001 的自动 gate 必须在同一轮先增量构建受影响 target，再直接运行下列 executable；不使用 CTest：

```powershell
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_ui_tests.exe `
  --gtest_filter='*TextEdit*:*Grapheme*:*Ime*:*TextInput*'
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_runtime_ui_tests.exe `
  --gtest_filter='*TextInput*:*TextEdit*'
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_platform_glfw_tests.exe `
  --gtest_filter='*TextInputPlacement*:*Imm32*'
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_ui_uia_tests.exe
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_ui_showcase.exe `
  --auto-demo --frames=120 --frame-delay-ms=0
```

矩阵要求覆盖：默认单行回归；LF/CR、soft-wrap、每编辑器 byte/visual-line 容量、固定 visual-row span
耗尽时零发布；二维 pointer hit、滚动边界 wheel 透传、Up/Down preferred-X、Home/End；组合字符/emoji
等 UAX #29 子集边界的移动、选择、删除；IME preedit/commit/cancel、focus loss；caret logical geometry
到 DPI-scaled client pixel 的转换、非 finite/0 高度/贴边 clip 清除与 Headless unsupported。当前自动证据为
`tina_ui_tests` 667/667、`tina_runtime_ui_tests` 130/130、`tina_ui_uia_tests` 12/12；Windows 真机
微软拼音候选窗跟随、提交、取消、失焦仍需人工矩阵，不能由单元或 Xvfb 结果替代。Linux 原生 XIM/Wayland
preedit/candidate placement 是独立后置平台项。

### 2026-08-16 当前主工作树定向复验

在 `codex/tina-vnext-runtime` 的常驻 `windows-msvc-vnext-bgfx` Debug build tree 上，对未提交的
`TEXT-001`、`UI-MOTION-002`、`ASSET-SEC-002` 与 SDK package 相关改动执行了增量构建。`tina_tests`、
`tina_ui_tests`、`tina_runtime_ui_tests`、`tina_platform_glfw_tests`、`tina_asset_format_tests`、
`tina_asset_tests`、`tina_bench_tests`、`tina_sample_ui_showcase`、`tina_sample_2d` 与
`tina_sample_3d` 全部 build exit 0。随后直接运行的定向 GoogleTest 结果为：

- TextEdit/grapheme/IME/Motion：95/95；
- Runtime TextInput/TextEdit/timeline：7/7；
- GLFW TextInputPlacement/IMM32：8/8；
- typed malformed corpus/EnvironmentMap：23/23，其中 `TypedPayloadMalformedCorpusTests.*` 为 17/17；
- Asset/Catalog/typed EnvironmentMap：124/124；
- Motion/timeline benchmark assertions：6/6。

同轮 `tina_sample_ui_showcase --auto-demo --frames=120 --frame-delay-ms=0`、
`tina_sample_2d --frames=300 --frame-delay-ms=0` 与
`tina_sample_3d --frames=30 --frame-delay-ms=0` 都输出 `status=ok`。这些结果只重新确认当前工作树的
编译、自动行为和产品生命周期接线；不替代 TEXT-001 的 Windows 真机 IME 人工矩阵、Linux 原生 XIM/Wayland
或 BiDi/复杂 shaping 后置项，也不替代跨 GPU/DPI 视觉门禁或 `PERF-002` 固定机 hard gate。

### 2026-08-17 TextEdit horizontal soft-wrap 复验

在上述常驻 Debug build tree 上，针对 soft-wrap 共享 scalar 边界的水平导航补充增量构建并直接运行受影响
executable。`tina_ui_tests` 为 667/667，`tina_runtime_ui_tests` 为 130/130，`tina_ui_uia_tests` 为
12/12，`tina_platform_glfw_tests` 为 39/39；`tina_tests` 的定向 Headless gate 为 1/1。新增
`SoftWrapHorizontalNavigationVisitsBothBoundarySides` 与
`SoftWrapHorizontalNavigationPublishesBothBoundarySides` 均通过，确认 Left/Right 会访问共享 scalar
边界的上下游 visual caret affinity，而不会跳过其中一侧。`tina_sample_ui_showcase --auto-demo --frames=120
--frame-delay-ms=0` 返回 `status=ok`、`exit=GameRequestedExitAfterCurrentFrame`。

## 测试 target 拓扑

| Executable | 主要范围 | 可用条件 |
| --- | --- | --- |
| `tina_tests` | Core、Platform contract、Task、Runtime、NullRender、Input/Action、header isolation | 基础图 |
| `tina_math_tests` | `Vec`/`Quaternion`/`Mat4`/`Aabb`/`Rect`/`Sphere`/`Plane`/`Ray`/`Frustum`、退化输入 fail-closed、列主序与 clip 深度约定、header isolation，以及与被删实现逐元素比对的四个数值等价性回归 | 基础图 |
| `tina_save_tests` | `SaveStore` 槽位读写/备份晋升/revision 递增、损坏回退与 repair、gameId 隔离、owner-thread 与单事务闭锁、async 句柄一次性语义；`SaveMigrationPipeline` 确定性单边图、缺失路径与越界步骤、payload 上限、抛异常步骤收敛 | 基础图 |
| `tina_editor_tests` | Editor authoring document/tab/undo、Marquee、Transform gizmo、Viewport grid/navigation，以及 3D 单击拾取的 ray 构造、最近命中决胜、偏移包围球与退化输入 fail-closed（`EditorViewportPickTest`） | `TINA_BUILD_EDITOR=ON`（顶层默认 ON）|
| `tina_editor_app_tests` | Editor 桌面 app 层：composite image resolver、source-import ingress/selection/service/launch options、Linux file dialog | `TINA_BUILD_EDITOR=ON` + `TINA_BUILD_PLATFORM_GLFW=ON` + `TINA_BUILD_RENDER_BGFX=ON` |
| `tina_gameplay_tests` | Gameplay 模块：Action authoring/runner、Easing、Scheduler、Signal，以及每个公开头一个 TU 的 header isolation | 基础图 |
| `tina_animation3d_tests` | Skeleton3D、ClipSampler3D、PoseBlend3D、AnimationGraph3D、IkSolver3D | 基础图 |
| `tina_platform_android_tests` | Android backend 纯契约断言（无窗口/GPU/JNI）：输入桥、IME preedit 状态机、touch/key 计数；可交叉编译后 push 到设备直接运行 | `ANDROID`（交叉编译；宿主无 preset）|
| `tina_bench_tests` | `tina_bench` UI benchmark workload 的契约测试 | `TINA_BUILD_BENCHMARKS=ON` 或 `TINA_BUILD_EXAMPLES=ON` |
| `tina_ui_tests` | UI tree/layout/hit/route/paint/semantics、Widget、文本/Glyph | 基础图 |
| `tina_runtime_ui_tests` | Runtime UI owner/capability/route/layout/display handoff | 基础图 |
| `tina_ui_render_integration_tests` | committed UI paint → Render DisplayList | 基础图 |
| `tina_scene_tests` | Entity/Transform/2D/3D component/extraction、CameraFollow2D、ParticleSystem2D、Trail2D | 基础图 |
| `tina_render_scene_tests` | Camera2D/3D、culling、sort/batch、world picking | 基础图 |
| `tina_asset_format_tests` | Cooked/Manifest 与 typed payload schema | 基础图 |
| `tina_asset_tests` | Catalog、AssetSystem、Handle/Lease、Cooker、upload/retirement | 基础图 |
| `tina_navigation2d_tests` | NavigationGrid2D immutable weighted cost/blocker/revision、确定性四向/对角同步/分步 A*、TileMap material-cost 导航转换 | 基础图 |
| `tina_audio_tests` | backend-neutral AudioEngine/voice/bus/command/completion | 基础图 |
| `tina_network_tests` | 数值地址解析、UDP、readiness poller、TCP 连接与 listener、HTTP/1.1、WebSocket 帧与握手原语、DNS。全部在 loopback，使用 ephemeral 端口故可并行 | 基础图 |
| `tina_platform_glfw_tests` | GLFW adapter 与 WindowSurface | `TINA_BUILD_PLATFORM_GLFW=ON` |
| `tina_render_bgfx_tests` | bgfx lifecycle、2D/3D/UI geometry/resource | `TINA_BUILD_RENDER_BGFX=ON` |
| `tina_ui_freetype_tests` | FreeType font open/measure/rasterize | `TINA_BUILD_UI_FREETYPE=ON` |
| `tina_ui_uia_tests` | Windows UIA property/fragment、control pattern、action 与 provider lifecycle | `TINA_BUILD_UI_UIA=ON` (Windows) |
| `tina_physics2d_tests` | Box2D lifecycle/contact/query/deferred command/grid bridge | `TINA_BUILD_PHYSICS2D=ON` |
| `tina_audio_miniaudio_tests` | miniaudio null-device、decode/mix adapter | `TINA_BUILD_AUDIO_MINIAUDIO=ON` |
| `tina_network_tls_tests` | TLS 配置拒绝、平台信任库读取，以及对 in-process mbedTLS 服务端的**真实握手**：可信证书连通、主机名不匹配与无关签发者均拒绝、应用数据往返、`close_notify`、HTTP over TLS、WebSocket over TLS | `TINA_BUILD_NETWORK_TLS=ON` |

## 基础 Windows 门禁

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug `
  --target tina_tests tina_math_tests tina_save_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests `
           tina_scene_tests tina_render_scene_tests tina_asset_format_tests tina_asset_tests tina_navigation2d_tests `
           tina_audio_tests tina_network_tests tina_sample_network tina_sample_null --parallel 2 -- /nr:false

out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_math_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_save_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_ui_render_integration_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_render_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_asset_format_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_asset_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_navigation2d_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_audio_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_network_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_sample_network.exe --frames=300
out\build\windows-msvc-vnext\bin\Debug\tina_sample_null.exe --frames=300
```

TLS 是独立 feature，需要单独一棵树：

```powershell
cmake --preset windows-msvc-vnext-network-tls
cmake --build --preset windows-vnext-network-tls-debug --target tina_network_tls_tests --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-network-tls\bin\Debug\tina_network_tls_tests.exe --gtest_color=yes
```

测试数量随功能增长，不作为永久契约；本轮必须直接运行对应 GoogleTest executable，并以最终 gate
JSON 与退出码记录结果。

## 3D-SKIN-001 与 RENDER-002-TRANSPARENT（均已 Done）

两项的逐条 gate 明细、当时的测试计数与历史 schema 15 字段值只保留在 git history。当前由 product-3d
evidence schema 16 与常规 3D product gate 保护，仍需成立的契约为：

- **Skin**：`SkinnedMeshRenderer3D` 与 `MeshRenderer3D` 互斥；`Animator3D` CPU pose 无分配且失败原子；
  palette 为 packet-local 深拷贝；CUBICSPLINE、skin 外 animation target 与 malformed 一律 fail closed。
- **Transparent**：Material v2 与 Cooker 只接受显式 `Opaque`/`Blend`，`MASK`/未知 alpha mode fail closed，
  不从 baseColor alpha 或纹理内容猜测 pass；Blend static/skinned 进入统一 back-to-front 全序，等距按
  stable Entity identity → kind → item index 决胜；透明 draw 容量独立固定且事务提交，超限不发半帧。
- **Pass 顺序**（两项共同依赖）：CSM×4 → Spot×1 → Point×6 → Opaque3D → Transparent3D → Sprite2D → UI。
  Transparent3D 使用 straight-alpha、depth test less、不写 depth；不投 shadow 但仍接收 lighting/shadow/PBR/IBL。
- **schema 16 当前字段**：total/static/skinned mesh=`3/2/1`、Material=`4`。`--skin-animation=on|off` 与
  `--transparency=on|off` 的 A/B 要求同模式 fingerprint 稳定、跨模式不同且 RGB L1 达标。

命令见下方 `RunProduct3dGate.ps1` 一节；A/B 差分产生的临时 raw capture 必须在 `finally` 中回收。
## Tracy Profile 定向门禁

Trace backend 只在专用 Profile tree 验证，不改写常驻 Null tree，也不在功能 worktree 重建依赖：

```powershell
cmake --preset windows-msvc-vnext-profile-tracy
cmake --build --preset windows-vnext-profile-tracy-debug `
  --target tina_tests --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-profile-tracy\bin\Debug\tina_tests.exe `
  --gtest_filter="TraceCompileTest.*:GameStatePolicyDispatchTest.*" --gtest_color=yes
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkConsumerGate.ps1 `
  -BuildDirectory out\build\windows-msvc-vnext-profile-tracy -Configuration Debug
```

Tracy 图必须编译并运行 nested zone，同时保留 Runtime State dispatch consumer；relocated GameSDK consumer
必须从安装头编译、链接 Tracy 静态闭包并正常退出。随后在既有 `windows-msvc-vnext` tree 增量构建
`tina_tests` 并运行 `TraceCompileTest.*`，复证 None 不求值/不构造/不调用契约。正式 benchmark 继续使用
None，不能用 Profile capture 充当稳定回归 baseline。

## 安装 SDK consumer 门禁

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkConsumerGate.ps1 `
  -BuildDirectory out\build\windows-msvc-vnext -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkConsumerGate.ps1 -Consumer PlatformGlfw -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkConsumerGate.ps1 -Consumer AudioMiniaudio -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkConsumerGate.ps1 `
  -BuildDirectory out\build\windows-msvc-vnext-audio-miniaudio-codecs `
  -Consumer AudioMiniaudio -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkConsumerGate.ps1 -Consumer DesktopBootstrap -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkConsumerGate.ps1 `
  -BuildDirectory out\build\windows-msvc-vnext-bgfx-ui-freetype `
  -Consumer DesktopBootstrap -Configuration Debug
```

```bash
export VCPKG_ROOT=/opt/vcpkg
tools/linux/run-sdk-consumer-gate.sh
tools/linux/run-sdk-platform-glfw-consumer-gate.sh
tools/linux/run-sdk-desktop-bootstrap-consumer-gate.sh
tools/linux/run-sdk-audio-miniaudio-consumer-gate.sh
```

Windows Docker 入口为：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunLinuxDockerGate.ps1 -Gate sdk-consumer
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunLinuxDockerGate.ps1 -Gate sdk-platform-glfw-consumer
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunLinuxDockerGate.ps1 -Gate sdk-desktop-bootstrap-consumer
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunLinuxDockerGate.ps1 -Gate sdk-audio-miniaudio-consumer
```

这些脚本在 `cmake --install` 之前只构建一个目标：`tina_sdk_install_artifacts`。它由
`cmake/TinaGameSdkPackage.cmake` 在每处 `install(TARGETS ...)` 旁边收集目标名后聚合而成（INTERFACE
库无产物，已滤除），所以「装什么」与「先编什么」同源。**不要在门禁脚本里手写目标清单**：那等于把同一份
清单存两处，新模块只要进了 install 规则却没进脚本，install 就会因为某个从未被编译的库而失败——Save
与 Gameplay 就是这样让 DesktopBootstrap 门禁挂在缺失的 `tina_save.lib` 上的。

反向的边界同样重要：**编辑器根本不是安装候选**。`cmake/TinaGameSdkPackage.cmake` 已不含 `tina_editor`、
`Editor` component 与 `include/tina/editor`（ADR 0041），所以它既不进 `Tina_GAME_SDK_TARGETS`、也不进
`tina_sdk_install_artifacts`。判断一个 target 是否该出现在 install 清单，看它有没有 `install(TARGETS ...)`
规则，不要看它是否恰好存在于当前 build tree —— 后者只反映别的构建留下了什么。

成功条件是：版本化 package 和声明的 `Tina_GAME_SDK_TARGETS` 全部可发现；实际安装头通过第三方
include/type token 扫描；所有 Tina imported target 的 include 都来自安装 prefix 而非源码树；外部
`tina_sdk_consumer` 只链接 `Tina::GameSDK`，并从 relocated installed headers 编译；consumer 还会
编译并链接 Runtime phase facade 的 stylesheet `BackgroundColor` transition setter/getter，覆盖对应公开
UI/Runtime 符号，而不只依赖仓库内 header-isolation。
`PrimaryWindowUIRootBuilder` 的 StyleClass/ColorToken 注册、token-backed stylesheet 安装与 root 创建调用，
以及 `PrimaryWindowUITreeUpdater` 的运行期 ColorToken getter/setter，
运行一帧后输出 `{"status":"ok","consumer":"installed-tina-sdk"}`。Headless consumer 不具备 primary
window UI，样式 facade 的运行期 phase/sticky-error 行为由 `tina_runtime_ui_tests` 覆盖。PlatformGlfw consumer 只链接
`Tina::PlatformGlfw`，必须创建隐藏窗口、读取 metrics、poll 一帧并输出
`"consumer":"installed-tina-platform-glfw"`。Null package 请求 `PlatformGlfw` 与所有 package 请求未知
component 必须被拒绝；未请求 `PlatformGlfw` 时不得加载 GLFW dependency/target。Desktop consumer 只链接
`Tina::DesktopBootstrap`，必须发现 `Tina::PlatformGlfw` 与 `Tina::RenderBgfx`；FreeType 图还必须发现
`Tina::UIFreetype`。隐藏窗口运行一帧后必须输出
`{"status":"ok","consumer":"installed-tina-desktop-bootstrap"}`。GameSDK-only isolation probe 禁用
GLFW/bgfx/FreeType/miniaudio/codec/Threads 查找后仍须配置成功，且不得出现任何可请求的 Desktop/Audio
adapter target。Tracy Profile package 可解析 Core 固定选择的 Tracy 链接闭包，但 `Tina::TraceTracy` 不是
component，也不得进入 `Tina_ADAPTER_TARGETS`。
AudioMiniaudio consumer 只链接 `Tina::AudioMiniaudio`，验证内置 codec capability、null backend callback 与
shutdown，并输出 `{"status":"ok","consumer":"installed-tina-audio-miniaudio"}`；codec 图还必须从
consumer toolchain 解析 `Vorbis`、`Opus` 与 `OpusFile` dependency closure。每个门禁必须将安装树从
staging prefix 物理移动到 relocated prefix，证明原 prefix 已消失、package CMake 文件不泄漏原
prefix/build/source 路径，并仅从新位置 configure/link/run。该 moved-prefix 门禁仍不替代跨发行版
artifact transfer 或正式 ABI 兼容性验证。

跨发行版 gate 只在 release/ABI candidate 阶段按需运行，不进入日常回归矩阵：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkCrossDistroGate.ps1 `
  -OutJson artifacts\gates\sdk-001-linux-cross-distro-consumer.json
```

其成功条件额外包括：Ubuntu 24.04/GCC 13 producer 产出 Release GameSDK archive、JSON metadata 和
SHA256；Debian 13/GCC 14 consumer 只读挂载 artifact volume，不挂载 Tina source/build tree，使用自己镜像内
的 vcpkg/xxHash，并把 archive 解包到不同绝对 prefix；package/header 扫描拒绝全部 producer
source/build/staging/package prefix 泄漏到 installed CMake metadata，header 扫描继续拒绝第三方 API token，
consumer configure 同时拒绝 imported target 使用 producer include 路径，最终 consumer 输出
`{"status":"ok","consumer":"installed-tina-sdk"}`。脚本存在或镜像成功构建都不能代替完整 producer →
consumer exit 0 证据；正式 ABI 另由 [ADR 0024](adr/0024-sdk-abi-compatibility.md) 决策。

Windows tip moved-prefix 再证（GameSDK / PlatformGlfw / DesktopBootstrap / AudioMiniaudio）见
[sdk-001-windows-consumer-evidence.md](evidence/sdk-001-windows-consumer-evidence.md)。
Linux tip Docker + 跨发行版 tip 见 [docker-tip-evidence-20260803.md](evidence/docker-tip-evidence-20260803.md)。

## UI performance quick run

`tina_bench` 的 UI workload 使用真实 `UIContext`、committed snapshots、pointer route、虚拟集合、
packet-local image resolve/pin 与 backend-neutral DisplayList。共享开发机只记录
`conclusion=provisional`；checksum、固定工作量、容量、资源归零和 warmup 后 UI PMR allocation delta
属于确定性不变量。

```powershell
cmake --build --preset windows-vnext-bgfx-debug --target tina_bench tina_bench_tests --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench_tests.exe
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_static_commit_v1 --warmup=60 --samples=600 --seed=1
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_paint_dirty_v1 --warmup=60 --samples=600 --seed=1
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_route_v1 --warmup=60 --samples=600 --seed=1
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_virtual_collection_v1 --warmup=60 --samples=600 --seed=1
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_image_nineslice_v1 --warmup=60 --samples=600 --seed=1
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_component_build_v1 --warmup=10 --samples=100 --seed=1
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_style_state_v1 --warmup=60 --samples=600 --seed=1
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_motion_v1 --warmup=30 --samples=120 --seed=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_motion_v1 --warmup=30 --samples=120 --seed=1
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_motion_v1 --warmup=30 --samples=120 --seed=2
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_motion_timeline_v1 --warmup=30 --samples=120 --seed=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_motion_timeline_v1 --warmup=30 --samples=120 --seed=1
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_motion_timeline_v1 --warmup=30 --samples=120 --seed=2
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_motion_layout_v1 --warmup=30 --samples=120 --seed=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_motion_layout_v1 --warmup=30 --samples=120 --seed=1
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe --workload=ui_motion_layout_v1 --warmup=30 --samples=120 --seed=2
py -3 tools\bench\run_benchmark_gate.py --processes 5 `
  out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_bench.exe -- `
  --workload=ui_static_commit_v1 --warmup=60 --samples=600 --seed=1
py -3 -m unittest tools/bench/test_run_benchmark_gate.py -v
```

multi-process runner 顺序启动独立进程，要求所有子结果的 workload/fingerprint/checksum 完全一致，
并输出 run-level p99 median/MAD。上面的 Debug quick run 必须仍为 `conclusion=provisional`。
`--hard-gate` 仅接受 Release、正式采样规模、至少5个候选进程，以及匹配且 `approved` 的
`tina_bench_machine_profile` / `tina_bench_baseline` schema v1；候选噪声超过受审 relative MAD
阈值时直接拒绝。仓库当前没有受审 profile/baseline，所以不能在项目门禁中传 `--hard-gate`。

图片 workload 每个 measured build 固定为 256 Image + 232 Icon + 512 full NineSlice，必须得到
`Q=5096/U=64/B=1000`、64 次 resolver hit、5032 次 cache dedupe、64 次 pin acquire/release、零
missing/not-ready/extent mismatch、零 resource-intern dedupe、pin/resource 最终归零、稳定非零
DisplayList checksum 与 `allocation.delta=0`。

`ui_component_build_v1` 每个 sample 通过 `UIElementBuildTransaction` 创建 256 个四节点 Component：ScrollView
root（Scroll + 2 Canvas commands）、RangeInput + Activate + Toggle、TextEdit、Dropdown（Activate + Select）。
每事务固定预留 4 nodes、11 text bytes、2 Canvas commands，以及 Activate=2、Toggle/Range/TextInput/Scroll/
Selection=1；JSON 必须报告各池 requested=reserved=published、capacity failure/outstanding=0、稳定非零 tree
checksum、warmup 后 UI PMR allocation delta=0 与 clean commit rebuild=0。

`ui_style_state_v1` 固定 4096 nodes、64 classes、64 ColorToken capacity、256 rules 与每个 styled node 4 个
class link；当前 workload 不注册 token，JSON 必须报告 `registered_tokens=0`、token high-water=0。每个 sample
只切换一个 retained `Disabled` state，必须得到 inspected/resolved nodes=1、candidate rules=16、layout/hit=0、
clean commit style/rebuild=0、bucket/class-link high-water 稳定、非零 style/DisplayList checksum 与
warmup 后 UI PMR allocation delta=0。

`ui_motion_timeline_v1` 与 `ui_motion_layout_v1` 都固定创建 256 个 definition、1024 条 track、4096 个
keyframe，seed 0/1/2 只播放前 0/16/256 个 active timeline。两者都必须证明 full definition high-water
为 `256/1024/4096`、active-index high-water 为 `0/16/256`，且 warmup 后 UI PMR allocation delta 为 0。
paint-only workload 的 active sample 只重建 Paint，Layout/Hit rebuild 均为 0；layout workload 的 active
sample 则要求 sampled layout tracks 等于 sampled tracks，并且每个 measured iteration 恰好各一次
Layout、Hit、Paint publication、failure=0。active=0 时两类 workload 都不得重建任何 snapshot。

运行期 ColorToken 最小回归必须直接运行 `tina_ui_tests` 的
`UIStyleContextTests.RuntimeColorToken*` 与 `tina_runtime_ui_tests` 的
`PrimaryWindowUICapabilityTest.*StyleColorToken*`。前者验证只为 winning-token 依赖发布 Paint dirty、
Layout/Hit/Semantics 保持 clean、local override 排除、no-op
四个 counter 归零、owner-thread、dirty queue 容量失败原子性，以及失败时检查统计仍保留；后者验证
phase-scoped Runtime getter/setter、跨线程/过期 phase 拒绝与 sticky error。确定性断言按以下口径读取
`UIContextStatistics`：inspected=全部 live node，resolved=第一遍实际 resolver 的节点，affected=winning
token 等于目标 token 的节点，candidate=第一遍 matcher 检查数。当前 `ui_style_state_v1` 不注册 token，不能
替代这组运行期 reverse-dependency 更新测试；公开头变化还需继续通过 header-isolation 与安装 SDK consumer gate。

正式采样规模、fingerprint 与固定机规则见[性能与内存](performance-memory.md)和
[ADR 0018](adr/0018-benchmark-protocol.md)。

## UI showcase 门禁

完整控件、中文与主题视觉验收使用 bgfx + FreeType 图：

```powershell
cmake --preset windows-msvc-vnext-bgfx-ui-freetype
cmake --build --preset windows-vnext-bgfx-ui-freetype-debug `
  --target tina_sample_ui_showcase tina_ui_tests tina_runtime_ui_tests `
           tina_ui_render_integration_tests tina_ui_freetype_tests --parallel 2 -- /nr:false

out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_sample_ui_showcase.exe `
  --frames=150 --frame-delay-ms=0 --theme=dark --density=compact --auto-demo
out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_sample_ui_showcase.exe `
  --frames=150 --frame-delay-ms=0 --theme=light --density=comfortable --auto-demo
```

两个自动 smoke 均须 exit 0，并输出 `controls=24`、`imageProducts=5`、`componentProfiles=3`、
`workbenchBands=5`、`desktopWorkbench=true`、`asymmetricCornerProducts=3`、`themeSwitches=2`、
`densitySwitchRequests=2`、`densityRebuilds=2`、`sliderChanges>0`、
`progressValue=84`、`dropdownSelection=1`、`listSelectionKey=1007`、`treeSelectionKey=4`、
`treeExpansionChanges=2`、`scrollOffset=80`、`componentScrollOffset=240`、`dialogOpen=false`、
`stylesheetInstalled=true`、`styleTokenUpdates>=3`、`motionBegins>=12`（auto-demo：2 次主题切换 x 6 panels）、
`stateEnters=3`、`stateExits=3`、`uiRootsCreated=3`、`uiRootsReleased=3`，最终主题与 density 分别回到
`initialTheme`/`initialDensity`。图片产品证据还必须满足
`imageAtlasUploaded=true`、`imageAtlasReleased=true`、`imageResolverCalls=imageResolverHits>0`、
`imageResolverUnavailable=0`、`maxImageQuads>=12`、`maxImageBatches>=3`、`maxUniqueImageResources=1`、
`imageLinear=true`、`imageNearest=true` 与非零 `paintOrderChecksum`；退出阶段还须有
`imageAtlasInvalidated=true`。`--auto-demo` 与显式 `--frames` 同用时至少 120 帧。

TMD-08 Desktop Shell 的 100%/150% 真实 DPI 视觉门禁使用专用入口，不能用 `--width/--height` 尺寸矩阵
替代 OS scale：

```powershell
# 100%：构建一次
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunTmd08DesktopShellVisualGate.ps1 `
  -ExpectedScalePercent 100 `
  -OutJson artifacts\gates\tmd-08-desktop-shell-100pct.json

# 150%：复用相同 EXE，并核验 EXE hash 与 logical geometry
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunTmd08DesktopShellVisualGate.ps1 `
  -ExpectedScalePercent 150 -SkipBuild `
  -PeerReportPath artifacts\gates\tmd-08-desktop-shell-100pct.json `
  -OutJson artifacts\gates\tmd-08-desktop-shell-150pct.json
```

脚本在 build/launch 前严格核对主显示器实际 scale；每轮执行 Dark/Light × Compact/Comfortable ×
960/1280/1600 logical width 共 12 个 case，验证非空稳定截图、sample 生命周期与 workflow、
logical/framebuffer/contentScale、committed pane geometry、icon atlas、关键区域内容和 theme/density raster 差分。
门禁只接受 FreeType 产品 preset，并记录真实字体路径/哈希，placeholder 不得计为 typography 证据；
`-PeerReportPath` 要求两轮 EXE 与字体 SHA-256 相同，并比较全部 logical geometry。报告属于同机同 backend DPI
证据，不证明混合 DPI 多显示器或跨 GPU golden。

UI-STYLE-001 产品 Visual（header accent ColorToken Dark/Light ROI 差分，同机同后端，非跨 GPU 金标）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunUiStyleVisualGate.ps1 -SkipBuild
```

脚本分别以 `--theme=dark` / `--theme=light` 捕获 showcase（无 auto-demo），比对设计坐标 header accent
ROI 平均 RGB；`maxChannelDelta` 须 ≥ 默认 12。JSON 写入
`artifacts/screenshots/ui-style/<stamp>/ui-style-visual-gate.json`。有 FreeType 图时可用
`-BuildPreset windows-vnext-bgfx-ui-freetype-debug`。

资源失效与 missing/unavailable 产品 smoke 使用独立模式，不能与 `--auto-demo` 同用：

```powershell
out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_sample_ui_showcase.exe `
  --frames=30 --frame-delay-ms=0 --image-lifecycle-demo
```

该模式第 10 帧销毁 atlas，第 20 帧释放 root-scoped resolver registration；必须 exit 0，并输出
`imageAtlasInvalidated=true`、`imageResolverCalls=19`、`imageResolverHits=9`、
`imageResolverUnavailable=10`、`imageResolverUnbound=true`、`imageFrames=9`、
`imageFreeFrames=21`、`maxImageQuads=12`、`imageFrameBorrowsAtRelease=0` 及非零
`paintOrderChecksum`。这证明 unavailable
阶段连续 skip、missing resolver 阶段不再调用 resolver，且非图片 UI 仍持续提交。

Image 产品的 Dark/Light content-scale-like 尺寸矩阵复用通用窗口捕获器：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunUiImageSizeMatrix.ps1 -SkipBuild
```

脚本运行 1280×980、1600×1225、1920×1470 三种 logical client footprint × Dark/Light 共 6 个
case。每个 case 必须有连续两张相同 SHA-256 的非空 PNG，sample JSON 必须保持 4 个 image product、
每帧 resolver hit、12 个 ImageQuad、4 个 image batch、单资源去重、Linear/Nearest、atlas 失效释放、
pin 归零、非零 checksum；当前 Windows GLFW 路径要求
`framebuffer≈logical×contentScale`，不再把原生像素 window extent 直接发布为 logical extent。汇总写入
`artifacts/gates/ui-image-size-matrix-<stamp>.json`；这只是 content-scale-like client footprint，
不冒充 OS Settings 100/150/200% 真 DPI、多显示器混 DPI 或跨 GPU 金标。

Visual/interaction 验收另跑不带 `--auto-demo` 的窗口：确认 Dark/Light 切换后既有控件同步换肤，
Primary/Destructive/Disabled Button 层次清楚，pointer press 会压低阴影并切换圆角 border ring 颜色，Tab focus 可辨，
Slider 与 ProgressBar 联动，Dropdown、List、Tree、Scroll 可操作，TextEdit 中文可读且左右 padding 与
pointer caret 一致。普通
`windows-msvc-vnext-bgfx` 图未启用 FreeType，placeholder text 不能计为字体或 CJK 视觉通过。

## Windows UIA 产品门禁

`RunUi002UiaGate.ps1` 使用 Windows UI Automation client API 从独立进程连接真实
`tina_sample_ui_showcase` HWND，验证外部发现、Tina Framework、唯一 RuntimeId/AutomationId、fragment
父链与 Invoke/Toggle/RangeValue/Value/Focus action；Invoke 后先验证动态 Name republish，再执行其余会改变
状态文案的 action。脚本正常发送 `WM_CLOSE`，让 EngineHost/UI owner 按产品路径退出。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunUi002UiaGate.ps1
```

该 gate 不替代 Narrator/Inspect 人工金标，也不证明 Linux AT-SPI。已有 build 可使用
`-SkipConfigure -SkipBuild`，并通过 `-OutJson` 固化结构化证据。tip 自动结果见
[ui-002-uia-evidence-windows.md](evidence/ui-002-uia-evidence-windows.md)；人工步骤见
[ui-002-narrator-inspect-checklist.md](ui-002-narrator-inspect-checklist.md)。

Focus 产品证据以 TextEdit 的 `HasKeyboardFocus`（`SetFocus` 之后）为准；全局
`FocusedElement.AutomationId` 仅作诊断，部分主机在节点已聚焦时仍返回空 id。

## 改动到门禁映射

| 改动范围 | 最小测试 | 追加 smoke/平台 |
| --- | --- | --- |
| Core/Result/Time/Memory | `tina_tests` | Null 300帧；Linux sanitizer |
| Platform/Input/WindowSurface | `tina_tests`、`tina_platform_glfw_tests` | `tina_sample_platform`，X11/Wayland |
| Task/关闭顺序 | `tina_tests` | Null/Desktop 300帧，失败注入 |
| Runtime phase/state | `tina_tests`、`tina_runtime_ui_tests` | Null、2D、3D products |
| UI/Widget/Text | `tina_ui_tests`、`tina_runtime_ui_tests`、bridge | FreeType、UI showcase、product-2d、截图 |
| Windows UIA/accessibility action | `tina_ui_tests`、`tina_ui_uia_tests`、`tina_runtime_ui_tests` | `RunUi002UiaGate.ps1` + Narrator/Inspect 人工金标 |
| RenderScene/Scene/2D-FX | `tina_render_scene_tests`、`tina_scene_tests` | extraction samples、2D/3D products |
| bgfx backend | `tina_render_bgfx_tests` | Desktop/2D/3D GPU samples + Visual |
| Asset format/Cooker | `tina_asset_format_tests`、`tina_asset_tests` | `assetc`→validate→sample、3D product |
| Shader payload / `ShaderBindingRegistry` / Sprite2D 自定义 fragment | `tina_asset_format_tests`、`tina_asset_tests`（`ShaderBindingRegistryTests.*`、`AssetGpuShaderTests.*`）、`tina_scene_tests`（`SceneSpriteAssetTest.*`）、`tina_render_bgfx_tests` | 三个 shader sample 全跑（见下方「Sprite2D 自定义 fragment」）|
| TileMap payload/runtime | `tina_asset_format_tests`、`tina_asset_tests`、`tina_physics2d_tests` | `tina_sample_2d`；验证显式 visual/collision/object layer |
| Audio | `tina_audio_tests` | miniaudio tests、product-2d |
| Physics2D | `tina_physics2d_tests`（body、Box/Circle/Capsule/ConvexPolygon、Distance/Revolute/Prismatic、sensor、query、grid bridge） | Release bench、product-2d |
| CMake/preset/dependency | 所有受影响 configure 图 | 最小 executable + product smoke |
| install/export/Game SDK | `RunSdkConsumerGate.ps1`、`run-sdk-consumer-gate.sh`、各 adapter wrapper | 跨发行版 artifact transfer；ABI/兼容策略 |

公共 API 变化还必须编译 header-isolation/consumer 测试，并扫描公开头是否出现第三方 token。

## Shutdown deadline

`RUNTIME-SHUTDOWN-DEADLINE` 的自动门禁归属 `tina_tests`，必须覆盖：

- Disabled/Bounded TaskSystem 拒绝非 finite 或非正 deadline，且非法调用不触发 stop；
- idle、queued drain 和已完成后的重复调用在 deadline 内成功；
- blocked Worker 触发 `TaskErrorCode::WaitTimeout` 后，TaskSystem 仍为 stopping，线程、队列和 owner storage
  未被 join/clear/reset；放行任务后对同一对象重试成功；
- `EngineHost` 将 `EngineConfig::shutdownDeadline` 原样传入 TaskSystem，且该值只预算
  Worker-exit/join 阶段，不被描述为 Audio/Render/整个 Host shutdown 的总耗时上限；
- Host 的 TaskSystem timeout death path 在 `std::terminate()` 前写入 `runtime.lifecycle` Diagnostics，
  并且不继续析构 TaskSystem、Platform、Clock、Diagnostics 等剩余 owner。

```powershell
cmake --build --preset windows-vnext-debug --target tina_tests --parallel 1 -- /nr:false
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes `
  --gtest_filter="DisabledTaskSystemTest.ShutdownDeadline*:BoundedTaskSystemTest.ShutdownDeadline*:EngineHostCreationTest.PassesConfiguredShutdownDeadlineToTaskSystem:EngineHostShutdownDeadlineDeathTest.*"
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes
```

仅观察进程终止不够：death test 同时匹配 `ShutdownDeadlineExceeded` Diagnostics 输出，并以析构哨兵证明
超时后没有 Task owner teardown。本轮 shutdown deadline 聚焦门禁和完整 `tina_tests` 均已直接执行通过。

## Crash 与 Editor fatal report

Core 最后故障报告归属 `tina_tests`：

- `CrashHandlerDeathTest` 覆盖显式 fatal reason、`std::terminate`、in-flight `std::exception::what()` 与
  `{"status":"crash"}` 非零退出；
- `CrashHandlerTest` 覆盖 GUI report file、armed marker 与 per-run 截断、重复 install 不自递归，以及
  uninstall 恢复 captured `std::terminate` handler。backtrace 按平台断言：Windows 要求 DbgHelp 解析出调用
  函数，其他平台要求出现 `unavailable on this platform`（两边都断言，段落缺失本身即失败）；
- **Windows fatal 矩阵**（受控 death-test 子进程，各自绕开 `std::terminate`）：access violation 必须报出
  读/写分类与 faulting address、pure virtual call、CRT invalid parameter，以及 cascade 只产生一份报告
  （terminate→abort→SIGABRT 只留第一个 reason，banner 计数为1）。AV 用例发现过一个真实缺陷：
  operation/address 只在 `SetUnhandledExceptionFilter` 分支提取，而 vectored handler 注册在前且 latch
  first-wins，故真实 AV 一直丢失这两个字段；现由两个入口共用同一 `describeExceptionDetail()`；
- `CrashHandler.hpp` 由独立 header-isolation TU 编译，公开面不得泄漏 Windows/DbgHelp/CRT token。

```powershell
cmake --build --preset windows-vnext-debug --target tina_tests --parallel 1 -- /nr:false
out\build\windows-msvc-vnext\bin\Debug\tina_tests.exe --gtest_color=yes `
  --gtest_filter="CrashHandlerDeathTest.*:CrashHandlerTest.*"
```

access violation、purecall 与 invalid parameter 现由受控子进程触发真实故障，但这些仍不等于 stack overflow、
GPU driver fault 或损坏堆下的产品证据 —— 后三者不在自动 gate 范围内，因为报告本身在那些状态下也可能不完整。
`TinaEditor.exe` 人工故障排查先检查 `%TEMP%/tina_editor_crash.txt`：正常启动至少有 armed marker（现已是全
平台行为，且每次启动截断，故内容必定属于本次运行）；handler crash 应以 `status=crash` 结束，顶层可表示
`Core::Error` 应以 `status=fatal` 结束并含 origin/context。系统临时目录查询失败时应改查当前工作目录的同名
文件。若该文件**完全不存在**，先看 stderr 是否有 `status=warning` 的 report-path 提示——Editor 现在会在
report file 打不开时明确说明，此前这种情况与「进程还没来得及写」无法区分。

backtrace 段按平台断言：Windows 要求解析出符号名，其他平台要求出现 `unavailable on this platform`。
非 ASCII Windows 路径验收与 Windows fatal SEH 矩阵仍由 `CORE-DIAG-001` 收口；Linux 真机 terminate/abort
artifact 复跑亦由该项跟踪（本轮只在 Windows 验证，Linux 分支按平台条件编写）。

## ASSET-HANDLE-SCENE（A1-A6 与 N16.1-N16.4 全部 Done）

九个切片已收敛为一个契约。逐切片的历史 gate 配方、当时的测试计数与被取代的 schema 4/13/14 字段值
只保留在 git history，不再作为当前预期；下列契约是仍需回归保护的部分：

- Scene/Prefab/FX/TileMap 只保存 weak `AssetHandle`，不持 Lease/payload/GPU owner；
- 全部 Sprite2D/Mesh3D Render item 只保存 packet-local `FrameResourceRef`，packet 固定资源预算320，
  超出返回 `FrameResourceCapacityExceeded`；
- `Sprite2DBindingRegistry` Entry 唯一拥有 resident `AssetLease`/`GpuTextureId`/binding；
  `Mesh3DBindingRegistry` 同时拥有 Mesh Lease/GPU/binding、Material Lease/binding，并按 AssetId 去重
  共享 Texture Lease/GPU，Material 引用计数阻止 live dependency 被退休；
- register 成功才消费 GPU owner；owner-thread、kind/store/state、PMR、ledger 与 backend failure 都保留
  Lease/GPU/Entry 供重试；active frame borrow/pin 阻止 retirement；
- invalid/stale/cross-store/wrong-kind/unbound/缺 resolver 一律 fail closed，分别映射为 Scene
  `UnresolvedSprite`/`UnresolvedMesh` 或 Asset `SpriteBindingNotFound`，TileMap 失败清空输出；
- hidden/off-camera/空集合不解析；Trail 每次非空 extract 解析一次，Particle 按 live item 解析。

门禁归属 `tina_scene_tests`、`tina_asset_tests`、`tina_render_scene_tests`、`tina_render_bgfx_tests`
与 2D/3D product gate。当前字段以 product-2d evidence schema 29 与 product-3d schema 16 为准：2D 的
`spriteBindingTextures=3`、三份 owner/retirement handoff、retirement ledger 全部 Released 且 live=0、
四类 resolver hits 非0；3D 为 3 Mesh / 4 Material / 3 Texture 全部 load/bind/retire。

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_scene_tests tina_asset_tests tina_sample_2d --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_asset_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
```

## RENDER-001 3D lighting / shadow / tangent / IBL（全部 Done）

`RENDER-001-SCENE-LIGHTS`、`-POINT-LIGHTS`、`-SPOT-LIGHTS`、`-VERTEX-TANGENTS`、`-CSM`、
`-SPOT-SHADOW`、`-POINT-SHADOW`、`-IBL` 与 shadow extent 配置的逐条 gate 明细留在 git history。当前契约：

- **容量与裁剪**：directional ≤4；point/spot 各 ≤8，且 influence sphere 按 active `PerspectiveCamera3D`
  frustum **先裁剪再做容量检查**；超容量第 N+1 盏显式失败。所有灯按稳定 Entity identity 排序后逐帧
  深拷贝为 self-contained RenderScene snapshot。
- **shadow 唯一性**：directional CSM、spot shadow、point shadow 各最多一个 camera-affecting config；
  非法 near/depth/normal bias 与第二个可见 shadow 一律 fail closed。CSM 固定4级联写 2×2 sampled D16
  atlas；point 固定六面按 `+X/-X/+Y/-Y/+Z/-Z`，dominant-axis 选面。
- **extent 配置**：`ShadowMapExtentConfig` 为 startup-only，取值为 `[128,4096]` 内的2次幂，默认
  `1024/1024/512`（directional tile / spot / point face）；`EngineConfig` 在任何 factory 前校验，
  Null/bgfx 直接 factory 同样 fail closed，PCF texel size 只使用当前配置，无固定尺寸兼容分支。
- **顶点格式**：StaticMesh v1 固定 P3N3T4UV2；glTF authored TANGENT 优先，缺 tangent 由私有 MikkTSpace
  生成，缺 NORMAL/UV 显式失败；shader TBN 按 signed model scale 修正 handedness。
- **着色**：Opaque3D 使用 Cook-Torrance GGX direct light + cooked EnvironmentMap split-sum IBL
  （diffuse irradiance、roughness specular LOD、BRDF LUT、intensity、world-Y rotation），并保留
  directional 3×3 PCF。EnvironmentMap v1 固定 RGBA16F cubemap + 完整 specular mip 链 + RG16F BRDF LUT，
  三纹理 create failure 原子回滚。
- **不投 shadow 的 pass**：见上文 Transparent3D 一节。

product-3d schema 16 当前字段：authored/committed/culled=`3/2/1`（point 与 spot 各自），CSM cascade
常量=`4`，spot/point shadow authored/submitted=`1/1`，`--ibl=on` 的上传/bind/clear/retire=`1/1/1/1`
而 `--ibl=off` 为 `1/0/0/1`，两模式 diffuse/specular/mips/BRDF 固定 `2/4/3/4×4`。GPU 实绘由 bgfx
contract 测试与 A/B 视觉差分证明，不能只凭 API 调用计数判定通过。

功能切片收口时先跑对应 AssetFormat/Asset/Null/bgfx/shader filter 与30帧 product smoke，最后才执行完整
TEST-003；不在每个实现步骤重复全量测试。
## World2D serialization

`2D-SERIALIZATION` 的最小门禁只构建 `tina_asset_format_tests` 与 `tina_scene_tests`，然后定向运行：

```powershell
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_asset_format_tests.exe `
  --gtest_filter=World2DSnapshotTests.*
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_scene_tests.exe `
  --gtest_filter=World2DSnapshotSceneTests.*
```

当前 wire 是 schema v5（464-byte entity record）。改 sprite 区偏移时必须同时跑
`tests/asset_format/header_isolation/World2DSnapshotHeader.cpp` 的 `SchemaVersion` static_assert 与
`TypedPayloadMalformedCorpusTests`，因为后者按 `HeaderBytes + EntityBytes` 定位第二条记录。

AssetFormat 用例覆盖全组件/gameplay round-trip、确定性 bytes、旧 schema 拒绝、stable ID/parent 约束、
non-canonical absent component 与 parse failure 保留旧 storage。Scene 用例覆盖 hierarchy+stable-ID 排序、
Handle↔AssetId、capture→parse→instantiate→capture byte equality，以及容量、资源解析和 transform publication
失败时保留既有 World。两个 target 同时编译新公开头的 header-isolation TU。该切片不修改 backend、shader、
sample 或产品接线，因此开发闭环不运行完整 product gate；只有后续把存档接入产品 State 时才增加对应 sample
smoke。

## 2D Editor authoring document

以下命令只在 Editor 大功能或里程碑全部闭环后集中执行一次；日常小切片不构建、不运行 suite，也不新增或
修改 Editor 测试。统一 gate 只使用独立工具模块和仓库已有 suite，不重跑产品、bgfx 或完整 Asset/Scene executable：

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug --target tina_editor_tests --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_editor_tests.exe `
  --gtest_filter=EditorPlaySessionTests.*:EditorSceneOperationsTests.*:EditorViewportNavigationTests.*:EditorViewportGridTests.*:EditorTransformGizmoTests.*:EditorMarqueeSelectionTests.*:TileMapGameplaySpawnPlanTests.*:SpriteAnimationAuthoringFileTests.*:TileMapAuthoringFileTests.*:ProjectAssetBrowserTests.*:EditorProjectWorkspaceTests.*:EditorProjectCreationTests.*
```

已有用例覆盖 canonical runtime preview、replace/load/upsert/erase-subtree/gameplay revision、undo/redo 与分支替换、
entry/byte budget 淘汰、非法 schema/parent/容量失败的 current + history 原子性以及公开头隔离。只有 Editor application
大功能接线或文件/cook 集成完整闭环时才扩大到 `TinaEditor.exe` 产品 smoke；Runtime samples 不是 Editor gate，
纯 document 小切片只做静态检查，不运行构建、测试或无关产品门禁。
TileMap suite 另覆盖 root v3 + chunk v1 canonical family、稳定 chunk AssetId、Paint/Erase、空 chunk 删除、layer/object
事务、payload-family load、root+chunk Cooked preview、bounded Undo/Redo 与失败不发布。Gameplay plan suite 覆盖 owning
stable-ID records、hidden object、unknown/duplicate/capacity failure，以及 encoder 完成后单次 World2D publication 和失败时
redo branch 保留。
SpriteAnimation suite 覆盖 clip identity、canonical v1 payload/dependency mapping、frame CRUD/duplicate/reorder/duration、
Once/Loop/PingPong、current-schema Cooked load、正式 Cook Preview、bounded Undo/Redo、容量失败与失败不发布。
File suite 直接比较 SpriteAnimation Cooked artifact 和 TileMap root/chunk artifact family 的 exact bytes、target platform、
artifact/byte count；TileMap 保存必须使用 canonical relative path 并 root-last 发布。
`ProjectAssetBrowserTests` 覆盖 canonical cooked path、完整排序 dependency ownership、Inspector snapshot 稳定性和非法依赖图；
`EditorProjectWorkspaceTests` 覆盖 strict UTF-8 canonical roots、Source/Catalog 隔离、容量和 Windows 大小写关系；
`EditorProjectCreationTests` 覆盖空目录创建/采用、非法 root/name/platform、non-empty root 保持不变，以及失败只回滚
本事务创建目录的行为；实现本身还在删除前核对 physical directory identity，并拒绝 symlink/junction/reparse root。
它们只证明基础 API；EditorApp `New` 的空 Catalog publish/reopen、Project `Open` + live switch 与 source-import 产品流程
仍由下述定向 EditorApp/unit + product smoke 证明，不用纯 document suite 代替。

TinaEditor GPU viewport 大功能完整闭环后只做一次受影响正式 target 的增量验证，不在内部小切片重复构建或测试，
也不重跑全量 UI/产品矩阵。
`--auto-demo` 的最小预算为 70 帧，正式 2D/3D smoke 固定使用 70 帧；Color Picker 阶段会等待
Inspector 自动滚动与目标控件几何一起 committed，再由 RenderDevice 捕获可见画面，并继续保留最终
Hierarchy selection 的确认帧：

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_editor_desktop --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\TinaEditor.exe `
  --frames=70 --frame-delay-ms=0 --workspace=2d --auto-demo `
  --rgba-output=artifacts/editor/captures/delete-dialog-2d.rgba `
  --rgba-stage=delete-dialog
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\TinaEditor.exe `
  --frames=70 --frame-delay-ms=0 --workspace=3d --auto-demo `
  --rgba-output=artifacts/editor/captures/delete-dialog-3d.rgba `
  --rgba-stage=delete-dialog
```

布局/Catalog/GPU smoke 除既有 authoring/runtime-preview 字段外，还要检查 `editorLayoutRegions=9`、
`viewportLayoutReady=true`、`inspectorScrollConfigured=true`、`renderExtractions=frames`、
2D `gpuViewportSprites=13`（1 World Sprite + 12 Tile sprites）、3D `gpuViewportMeshes=3`、`gpuViewportReady=true`、
`gpuViewportDocumentRevision=documentRevision`，以及非空 logical rect、位于 `[0,1]` 内的 normalized viewport 和
`uiRootsCreated=1` / `uiRootsReleased=1`。auto-demo test fixture Catalog 还固定检查 entry/load=`9/7`、Texture/Mesh upload=`1/1`、
Sprite/Mesh/Material binding=`1/1/1`、unresolved=`0`、resolved 2D/3D=`1/3`，以及 TileMap
layer/chunk/cell/artifact/emitted=`2/2/12/3/12`、Animation revision/frame/cook=`4/4/256 B` 和 cook bytes 非零。
Project Browser/tabs 还要求 ready=`true/true`、visible assets 非零；自动演示从 4 个 pinned tab 打开一个额外 Animation，
再恢复 pinned Animation 与初始 workspace，固定 `documentTabCount/projectAssetOpenCount/tabOwnedDocumentLoads/`
`tabOwnedDocumentSwaps/previewAssetBindingRefreshes=5/1/1/2/2`。自动演示实际执行 Animation Next/Mode/Undo/Redo/Cook，
`editorActionsReady=true`；2D 自动路径还要求 gameplay generation/records/bytes 非零、source revision 非零，3D
对应四字段全为零；viewport 使用上一轮 committed
layout，所以首帧不提交 world，窗口尺寸改变后下一帧跟随新 rect。
不带 `--auto-demo` 就是默认人工操作模式；未打开项目时必须报告 `testFixtureCatalog=false` 且 Catalog
entry/load/GPU/resolved 计数全为零。本切片不扩大到完整 product-2d gate。
自动交互 smoke 必须消费 2D pan/anchored zoom 与 3D orbit/pan/dolly，且两个 workspace 都至少形成一个 navigation batch。
Translate/Rotate/Scale Gizmo 各 commit 一次，cancel/reject 为零；Rotate/Scale 各自必须是实际 multi-target commit，
`viewportMaximumGizmoTargets >= 2`，不能只依据 selection count。Translate delta、rotation degrees 和 scale factors
都必须有限且 non-identity，完整 TRS 在 canonical document、Scene preview 与结构化结果中一致。

Marquee Replace/Add/Toggle 各 commit 一次且 selection change=`3`，added/removed 非零、maximum selection 至少为 2。
Scene Add/Duplicate/To Root/Reparent/Delete 固定为 `1/1/1/1/2`，自动创建的两个 stable ID 均非零，结束时实体数恢复为 5。
指定 `--rgba-output` 时必须同时指定 `--auto-demo`，可选的
`--rgba-stage=workspace|color-picker|delete-dialog` 默认为 `workspace`；单独传 stage 或非法 stage 都拒绝启动。
结果必须满足 `rgbaCaptureAttempted=true`、`rgbaCaptureOk=true`、`rgbaCaptureOutputWritten=true`，尺寸非零且
`rgbaCaptureBytes=width*height*4`；raw 文件来自 Tina RenderDevice 内部 capture，不是桌面窗口抓取。
像素检查至少拒绝全零/单色帧；workspace 检查工作台主要 band，color-picker 检查 Color Field/preview/RGB slider，
delete-dialog 检查中央 Dialog surface、全屏 scrim、title/body 与 Delete/Cancel action 所在区域。
Play Start/Pause/Step/Resume/Stop 固定为 `1/1/1/1/1`；即使 `--frame-delay-ms=0`，paused Step 仍必须令
`playSimulationSteps` 与 `playMaximumSimulationTick` 非零。最后继续检查 2D/3D workspace round-trip、runtime preview
多次重建、最终 document revision/undo depth 非零且 GPU revision 对齐。

Editor 文件加载/原子保存大功能进入统一 gate 时复用同一增量 build tree，只运行已有 Editor file filter 和带显式
UTF-8 路径的产品 smoke：

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_editor_tests tina_editor_desktop --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_editor_tests.exe `
  --gtest_filter=World2DAuthoringFileTests.*:World3DAuthoringFileTests.*:SpriteAnimationAuthoringFileTests.*:TileMapAuthoringFileTests.*:ProjectAssetBrowserTests.*:EditorProjectWorkspaceTests.*:EditorProjectCreationTests.*
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\TinaEditor.exe `
  --frames=70 --frame-delay-ms=0 --workspace=2d --auto-demo `
  --world2d-path=artifacts/editor/smoke/world2d.tworld `
  --world3d-path=artifacts/editor/smoke/world3d.tprefab
```

两个 workspace 分别使用 `--world2d-path` / `--world3d-path`，不保留共享 path 参数。smoke 要检查每个 session 的
path-configured/loaded/dirty/saved-bytes 字段；编辑和 Save active document 后切到另一 workspace 再切回，inactive session
状态必须完全不变。随后对同一双路径运行一次不带 `--auto-demo` 的短 smoke，要求两个已存在文件各自 loaded/clean、
Undo/Redo depth 均为0。未给 active workspace 配置路径时 Save disabled，`authoringSaves=0` 且 active dirty=true。
Windows 人工 Save As 还应分别确认 `.tworld`、`.tprefab`、`.tasset` save-file dialog 与 TileMap folder picker；Cancel 后
path/baseline/dirty/tab/selection 不变。Linux 使用安装了 `zenity` 或仅安装 `kdialog` 的两种环境分别验证 open/save/folder、
Cancel、UTF-8 absolute path 与 helper 回收；其他未支持平台返回 `Unsupported` 后仍可从 TextEdit 路径完成保存。

Linux Editor 自动门禁必须按顺序执行：

```powershell
# Primary：唯一一次 Linux configure/build/test + 2D/3D smoke，再验证 zenity。
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxDockerGate.ps1 `
  -Gate gcc13-editor-zenity -OutJson artifacts\gates\2d-editor-linux-zenity.json

# Secondary：只校验 primary 的 source fingerprint / executable SHA-256 并验证 kdialog；
# 不调用 CMake、不重复测试/smoke，成功后自动删除专用 Linux Editor build tree。
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxDockerGate.ps1 `
  -Gate gcc13-editor-kdialog -OutJson artifacts\gates\2d-editor-linux-kdialog.json
```

两个 helper 镜像互斥安装，`kdialog` 环境中 `zenity` 必须确实不存在，不能靠修改 `PATH` 模拟 fallback。
每个 open/save/folder/cancel case 都检查 probe 的直接子进程 executable/cmdline、UTF-8 absolute result 和
child 已 reaped。primary 成功暂时报告 `resource_build_tree=retained-for-kdialog-reuse`；secondary 成功必须报告
`resource_build_tree=removed`、`resource_processes=stopped` 和 `resource_temporary_directory=removed`。任何失败
不得启动第二套编译；保留的专用 tree 只用于首错诊断，完成重试或记录后定向删除。
Windows Project `New` 人工门禁选择一个空目录，要求生成 `Source/`、`Catalog/` 和零 entry current-schema manifest；随后
用 `openCatalogPackage()` 的 typed validation 重新打开成功，并在下一安全帧报告 `projectSwitches=1`、
`projectCatalogConfigured=true`、`testFixtureCatalog=false`，active Catalog root 指向新项目，空 Browser 与无资源 preview
仍保持有效。随后 Project `Open` 选择同一 root，应再次安全切换；选择缺少 Source/Catalog、含 reparse/junction 或无效
current-schema manifest 的 root 必须保留旧 Catalog、Browser 与 preview。另需先打开并修改一个 Catalog document，确认
New/Open 被阻止且旧状态不变；保存或丢弃后重试，确认动态 Catalog tab 被关闭、固定 TileMap/Animation tab 从已提交
Catalog snapshot 重新加载，Browser 不会复用 reload 前的磁盘快照。Linux 运行相同步骤并分别覆盖 `zenity` 与 `kdialog`
回退；其他未支持平台报告 folder selection unavailable。

Editor source import 只有在完整大功能收口后才使用独立 target 统一验证 launch parser 与 owner-thread service；实现
内部的小功能、小细节不构建、不新增或运行测试，统一 gate 也不重跑全量 Runtime/UI：

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_asset_tests tina_editor_app_tests tina_editor_desktop --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_asset_tests.exe `
  --gtest_filter=SourceImportPipelineTests.*:GltfCookTests.CooksMinimalTriangleToMeshMaterialPrefab
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_editor_app_tests.exe
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\TinaEditor.exe `
  --frames=60 --frame-delay-ms=0 --workspace=2d `
  --project-root=C:\absolute\path\to\project `
  --import-recipe=C:\absolute\path\to\project\Source\game.recipe `
  --import-gltf=C:\absolute\path\to\project\Source\hero.glb `
  --import-on-start
```

parser unit 必须证明 mixed recipe/glTF 按 caller order 保留完整 intended unit 集、同 kind 的等价物理路径别名重复零突变、
absolute strict UTF-8、扩展名/importer kind、project `Source/` containment/容量门禁，以及 `--import-on-start` 对 project+unit
的组合要求；service unit 必须证明 worker 不触碰 owner-thread 状态、Ready
stage 在 acknowledge 前稳定、failure 在 dismiss 前稳定。Asset 定向 filter 证明 Linux target recipe 的首次 full recook/第二次
clean reuse，以及 glTF cooker 把显式 Linux target 写入 request/package。产品 smoke 要求 `sourceImportStarts/Completions/Failures=1/1/0`、
intended/total unit=`2/2`、`sourceImportStateCommitted=true`、Running/Ready=false，并检查 fresh stage 成为
`activeCatalogRoot`、Browser/documents/2D/3D/Animation preview 从已提交 snapshot 重建。先制造 dirty Catalog document 时 stage
必须保持 Ready 且旧 Catalog 不变；保存/丢弃后下一安全帧提交。`CatalogReloadBusy` 必须增加 busy retry、保持同一 stage，
直到后续安全帧成功。state 必须是 immutable stage 的 sibling 文件，项目 tool cache 只以
`.tina/cache/source-import/active-catalog.path` 作为唯一原子 commit marker；结束后以同一 `--project-root` 无
`--import-on-start` reopen，要求恢复该 stage 及完整 intended unit 集，而不是退回固定 `Catalog/`。这是一轮大功能定向验收，
不要求同时运行全量 UI、Runtime 或 product-2d gate。ingress unit 需覆盖外部媒体分类复制、未 commit 回滚、整批预检零写入、
同名同内容复用、同名异内容后缀和重复物理选择单次复制；另需覆盖外部 recipe/glTF、缺失物理文件和 cooker 首错，确认均保留旧
Catalog，且有限帧 JSON 返回真实错误而不是 lifecycle 通用失败。
真实资源人工验收必须从普通模式 New/Open Project 开始，不使用 `--auto-demo`：用 `Import Files...` 直接选择项目外
`.png`/`.jpg`/`.jpeg`，确认源文件复制到 `Source/Imported/Images/`，Catalog 每张图只出现一个 Texture2D，并可把该
Texture2D AssetId 直接用于 Sprite2D 文档预览；关闭后第二次打开 `Import Files...`，确认 Windows dialog 仍能解析
项目初始目录（workspace 的 canonical `C:/.../Source` 必须先转换为 Windows native path，Shell 不得返回
`E_INVALIDARG`）。再把 `.gltf`/`.glb` 及其相对依赖完整放入项目 `Source/` 后选择，确认 Mesh/Material/Prefab 出现在 Browser
且 3D 文档能解析并绘制对应资源。导入失败时旧 Catalog、
Browser 和 preview 必须保持不变，且不能显示 test fixture 的纹理或 Cube 作为回退。
Core 的目录替换失败回归保留在 `WriteFileTests.FailedAtomicReplacePreservesExistingTargetDirectory`；当前
`tina_tests` 是 Core + Runtime monolithic target，小型 Editor 切片不为单个 filter 重编全部对象，留到大功能统一 gate。

## 2D-LIGHT N1-N5（Done，跨 GPU exact golden 除外）

`ShadowOccluder2D`、`PointLight2D` camera culling、finite-source soft shadow 与 Sprite2D normal map 的
逐条 gate 明细留在 git history。当前契约：

- **容量与裁剪**：active `PointLight2D` ≤8，先按 resolved、pixel-snapped `Camera2D` 做旋转相机空间的
  精确 circle-vs-rectangle 裁剪，只有 camera-affecting light 占槽，第9盏仍显式失败；无相机或 0x0 surface
  保留未裁剪语义。`ShadowOccluder2D` ≤32 个 active segment，**不参与裁剪**。
- **软阴影**：source radius 默认0精确保留 hard ray；正值以 normalized-depth segment projection 连续计算
  finite line-source visibility，多段使用固定成本 multiplicative transmittance。
- **normal map**：`SpriteRenderer2D::normalTexture` 是 optional weak Texture2D handle，经独立 resolver
  取得 packet-local ref；缺 resolver/stale/wrong-kind/empty 统一 `UnresolvedSprite` 且失败发生在
  `addSprite2D()` 之前；连续 batch identity 为 `(baseTexture, normalTexture)`；derivative TBN 自动覆盖
  rotation、signed scale、atlas UV 与 flip。normal **只**调制 point-light contribution，ambient、shadow
  visibility、attenuation、premultiplied alpha 与无 normal 分支保持不变。
- **不变量**：fragment→light 相交只清零点光贡献；ambient、透明排序与 premultiplied alpha 不受影响；
  排序 checksum 不受 normal binding 影响。

product-2d schema 29 当前字段（继承 schema 16..19）：authored/committed/culled=`3/2/1`、
`shadowOccluder2DCount=2`、`softShadowPointLight2DCount=2`、`normalMappedSpriteCount=1`（off 模式为0）、
`texturesUploaded=3` 且3份 Texture owner/retirement lifecycle 完整。

A/B 视觉差分（soft/hard、normal on/off）要求同模式 RGBA8 fingerprint 可重复、跨模式必须不同；这不声明
跨 GPU exact golden，后者由 `UI-003` 跟踪。开发阶段先跑 Scene resolver、RenderScene propagation 与
Null/bgfx resource/batch 定向 filter，闭环后再跑产品视觉差分与完整 product gate。
## 产品与样例的证据边界

| Executable | 证明 | 不证明 |
| --- | --- | --- |
| `tina_sample_null` | EngineHost、固定帧、Headless/Null lifecycle | GLFW、GPU、可见 UI |
| `tina_sample_platform` | GLFW window/input/WindowSurface + NullRender | bgfx 绘制 |
| `tina_sample_desktop` | Desktop bootstrap、真实 bgfx surface、UI pass | 2D/3D 产品内容 |
| `tina_sample_ui_showcase` | 20 控件 + Image/NineSlice + Dark/Light + Tree/List；startup stylesheet + header accent ColorToken 换肤；JSON `stylesheetInstalled`/`styleTokenUpdates` | 正式编辑器 / authoring 写入；完整 CSS |
| `TinaEditor.exe` (`tina_editor_desktop`) | `Tina::EditorApp` 驱动 World2D/Prefab v4 World3D/TileMap v3+v1/SpriteAnimationClip v2 完整产品；Project Browser/分类过滤/资源 Inspector/current-schema Catalog open/refresh、fixed 32 px asset list、active-tab AssetId Inspector 与 fixed 36 px dependency list、固定容量且独立拥有 document/history/session 的 tabs；Inspector 完整 TRS transaction、routed-pointer viewport Move、Tile tools、Navigation bake/publish、SpriteAnimation Timeline frame CRUD/播放/模式/时长/重排/event marker/Undo/Redo/Cook、Windows native 与 Linux `zenity`/`kdialog` open/save/folder dialog、Project `New` 创建 Source/Catalog 并 manifest-last 发布/reopen 空 current-schema package、Project `Open` 与下一安全帧 live Catalog switch、canonical dirty baseline 与 dirty-close Modal；`--project-root` + mixed recipe/glTF intended set + `--import-on-start` 证明后台 validated fresh stage + sibling state、主线程 Catalog reload/busy retry、dirty commit gate、单一 active pointer commit 与 reopen 恢复；`--catalog-root` + AssetSystem + Sprite/Tileset/Mesh registry 解析真实 AssetId、GPU owner 与 packet-local refs，committed UI rect 驱动 Camera2D/Sprite/多 Tile layer 或 PerspectiveCamera/Mesh viewport；JSON 报告 layout、browser/tabs、gizmo、TileMap、Navigation bake、Animation marker、session、source import、Catalog/GPU resolve、document revision 与 preview 状态 | Linux Editor target 定向编译与 `zenity`/`kdialog` 真实 open/save/folder/cancel 产品门禁；Fx2D 当前只有公共 authoring document，没有专用 EditorApp 面板 |
| `tina_sample_asset` | Catalog→Task→AssetSystem→ReadyGpu/Lease | 可见纹理/mesh |
| `tina_sample_2d_infrastructure` | CPU/Null Camera2D/Sprite extraction | Catalog/产品 UI/GPU |
| `tina_sample_2d_infrastructure_bgfx` | fixture Sprite2D + UI overlay | 正式 Catalog TileMap 产品 |
| `tina_sample_2d` | Catalog TileMap v3 root + deferred TileMapChunk、NavigationGrid2D v1 与 Fx2D v1；每帧 visual=10/collision=20 demand→pump→commit；Navigation live derive 与 Cooked data bit-exact，并验证 weighted A*、dynamic blocker、分步取消与 revision；PhysicsNavigationSync2D 将显式注册 crate body 的 transform/AABB 同步为 dynamic blocker；SpriteAnimation notify 被产品消费；Fx2D factory 驱动 fixed-capacity Particle/Trail；Physics 含 Box/Circle/Capsule/ConvexPolygon/Chain、sensor enter/exit 与 Distance/Revolute/Prismatic joint；Sprite2D 使用 packet-local `FrameResourceRef`；schema 29 保留既有证据并新增 navigation physics sync counters | Registry transaction/PMR/owner-thread 压力、跨 GPU lighting golden、可见 FX effect graph/GPU simulation、更多高级约束、Linux |
| `tina_sample_2d_custom_shader` | Sprite2D 自定义 fragment 端到端：`tina_assetc --shader-source` cook 出带 profile 表的 payload、`uploadShaderFromCooked`、packet-local Shader/ShaderUniforms ref；两相 pinned `u_pulse.x` 上 custom 区域 RGB 均值差 `>= 8` 而引擎对照区域差 `== 0`，并在对照精灵四象限上断言 2×2 棋盘（红/绿/蓝/白）证明 UV/采样正确 | 自定义 fragment 消费引擎 lighting（见 `tina_sample_2d_shader_lighting`）；Mesh3D 自定义 draw 路径 |
| `tina_sample_2d_shader_materials` | 同一 program 三套独立 uniform binding：`minimumMaterialSeparation` 断言三种 material 之间的像素差有下界，`maximumSameMaterialDelta == 0` 断言同 material 的两个精灵逐字节相同，`flatMaterialSpread == 0` 排除「整帧变亮」这类伪证据。**value 表按名匹配**：第三个 material 故意把两个 value 倒序发布，`flatMaterialTexelDistance == 0` 断言它落在自己 UV 指向的那个纹素上（左上象限色）——按位取值会让 UV 出界钳到别的边缘色，**同样平坦**，所以 `flatMaterialSpread` 对这类缺陷失明。负对照实测：强制设备按位取值 → spread 仍 0、distance 110、exit 1 | 逐 material 纹理切换；author 侧 material authoring UI |
| `tina_sample_2d_shader_lighting` | 自定义 fragment **读**引擎契约而非替换它：`s_normalTex`、`u_spriteLightParams`、`u_spriteLightPosRadius`、`u_spriteLightColors`、`u_spriteShadowSegments`。六个精灵交错排布使相邻 draw 不共享 (shader, normal) 组合，证据是帧内差分——`normalVsFlatSeparation`、`normalLeftVsRight`、`shadowedVsLit`、`engineControlSpread`，整帧亮度变化无法满足 | 跨 GPU lighting exact golden；多光源/多遮挡的组合爆炸 |
| `tina_sample_3d_extraction` | CPU/Null Perspective/Mesh extraction | 可见 GPU 3D |
| `tina_sample_3d_infrastructure` | procedural fixture Cube/depth/instance | Cooked product mesh |
| `tina_sample_3d` | 双静态 mesh glTF→MikkTSpace tangent→Cooked P3N3T4UV2，以及独立 SkinnedMesh/AnimationClip3D witness→`Animator3D` CPU pose→packet palette→bgfx GPU skinning；AssetSystem→Prefab/Scene weak Handle→engine-provided、State-owned Mesh3D registry→packet-local geometry/material ref；evidence schema 16 固定 total/static/skinned mesh=`3/2/1`、Material=`4`、joints=`2`、skinned Prefab instances=`3`、`tangentMeshesUploaded=2`，并以独立 Blend Material、双 static transparent witness、统一 back-to-front sort checksum 与 transparency on/off RGB 差分证明 Transparent3D；同时继承 skin-animation、IBL、CSM/Spot/Point shadow、实时 framebuffer aspect、响应式 UI、资源 retirement、逐帧 lighting snapshot、Dark→Light→Dark 与 final-present capture | Registry transaction/PMR/owner-thread 压力（由 `tina_asset_tests` 证明）、跨 GPU golden |

`tina_sample_2d` 是唯一产品 2D target；中间迁移名 `tina_sample_2d_tilemap_bgfx` 已删除。

**`tina_sample_gallery_desktop` 不在上表，因为它不是门禁。** 上面每一个都是「跑固定帧数、打印 JSON 证据、
返回退出码」的程序，而 gallery 跑到用户关窗为止，不打印证据也不断言任何东西 —— 它证明的是「引擎能被人
用手操作」，那不是 CI 能读的东西。**不要把它加进任何门禁清单**：它没有可比较的输出，一个「通过」只意味着
进程没崩。

它存在的理由是上表暴露的一个空缺：20 个示例里没有一个能回答「让我在手机上看看这引擎」。它们是门禁，
其中 8 个还绑死 GLFW（Android 编不了）。gallery 因此拆成两半 —— `tina_sample_gallery`（场景库，不链
GLFW，Android 也链它）加各自的宿主前端 —— 所以一个场景写一次、两端都跑。**现有 20 个门禁示例一个都没动**：
它们是既有证据，改写成可交互场景等于拿已证明的换好看的。

## Asset/Cooker E2E

```powershell
cmake --build --preset windows-vnext-debug `
  --target tina_asset_format_tests tina_asset_tests tina_scene_tests tina_assetc tina_catalog_validate tina_sample_asset --parallel 2 -- /nr:false
out\build\windows-msvc-vnext\bin\Debug\tina_asset_format_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_asset_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext\bin\Debug\tina_assetc.exe --out <catalogRoot> --recipe <recipe> `
  --source-root <authoringRoot> --import-state <toolCache>\import-state.tmeta
# 无 import-state 的重复 cook 同样通过 fresh stage 保持旧输出不变
out\build\windows-msvc-vnext\bin\Debug\tina_assetc.exe --out <catalogRoot> --stage-out <candidateRoot>
out\build\windows-msvc-vnext\bin\Debug\tina_assetc.exe --out <catalogRoot> `
  --recipe <recipeA> --recipe <recipeB> --source-root <authoringRoot> `
  --import-state <toolCache>\import-state.tmeta `
  --stage-out <candidateRoot> --stage-import-state <toolCache>\candidate.tmeta
out\build\windows-msvc-vnext\bin\Debug\tina_catalog_validate.exe --root <catalogRoot> --typed-payloads
out\build\windows-msvc-vnext\bin\Debug\tina_sample_asset.exe --frames=60 --catalog=<catalogRoot>
```

ASSET-002 multi-unit mixed fresh-stage 只需构建 `tina_asset_format_tests`、`tina_asset_tests`、`tina_assetc`，直接运行
`SourceImportMetadataFormatTests.*:SourceImportCaptureTests.*:SourceImportPlanTests.*:SourceImportProbeTests.*:SourceImportExecutorTests.*:CatalogCookTests.Incremental*`
filter。CLI 对同一 recipe/glTF batch 连续执行两次：首跑必须为 `full-recook`，第二跑必须为 `clean-reuse`；第二跑前后比较
manifest/object/state 的 bytes 与 mtime，均应不变。修改 WholeFile 任意 byte、Prefix 已消费范围内 byte、importer
contract 后，只允许对应 unit recook 并写入 fresh stage；未变 unit 的 object bytes 必须相同。Added/Removed unit 必须更新
完整 manifest/state ownership，stage full validation 失败不得修改 baseline package/state。旧 schema 或 manifest revision
失配必须 full recook；只在 Prefix 已消费范围后追加 bytes 仍应 clean。该切片不要求
运行完整 `tina_asset_tests`、Scene/sample 或 shader/bgfx target。

ASSET-002 Catalog watcher 只需增量构建 `tina_asset_tests`，直接运行
`CatalogPackageWatcherTests.*:CatalogPackageChangeDetectorTests.*`。真实 Windows/Linux 测试覆盖 watcher 先 arm 后捕获
baseline、目标 manifest write/rename/delete/replace、同目录 sibling 过滤、目录失效 `RescanRequired`、move ownership 与
结构化配置/路径失败；该切片不构建 `tina_assetc`、sample、shader 或 bgfx target。overflow 由平台实现映射为
`RescanRequired`，host 收到该状态后必须重新 capture revision，不能直接接受 candidate。

ASSET-002 resident CPU + Sprite/Mesh active GPU owner transaction 只需增量构建 `tina_asset_tests`，直接运行
`AssetSystemCatalogReloadTests.*`。门禁覆盖 Modified Texture + Affected Material 同时换代、稳定旧→新 Handle 映射、
旧 Lease 保留旧 payload、新 index 指向 candidate payload、最后 lease 释放回收旧 generation，以及
validation/change-plan/result-capacity/Store 双驻留 headroom/queued work 的失败原子性。GPU participant 场景还覆盖
Sprite replacement、upload/binding prepare rollback、active frame borrow 全局门禁、后 participant 失败逆序 abort、
Mesh+Material+shared Texture 联合换代、Material 新增 resident Texture dependency，以及 backend retirement reject 后
owner 可重试。该 focused gate 不运行完整 Asset、sample、shader 或 bgfx target。

multi-mesh glTF Cooker 的库级测试与 `tina_sample_3d` 双 mesh 产品 E2E（3D-001）均已完成：distinct
mesh/material AssetId、Prefab dependency、AssetId→Handle→registry-owned binding→packet-local ref 与双 mesh
binding 可验证。Opaque3D 已做
baseColor/MR/normal 贴图 **采样**、authored/MikkTSpace vertex tangent、material factors、Cook-Torrance GGX、
cooked EnvironmentMap split-sum IBL、World directional/point/spot lights 的逐帧 snapshot、固定4级联 CSM、
固定单 SpotLight shadow 与固定单 PointLight 全向 shadow；三类 D16 extent 已支持 startup-only 配置。

`ASSET-SEC-001` 的定向门禁是 `GltfCookTests.*`：覆盖主/外部文件 64MiB 上限、短 buffer、strict UTF-8
与 percent-decoded traversal、root 内和逃逸 symlink/junction、bufferView/accessor/count/overflow、PNG
dimension/decoded-byte budget、multi-primitive 与完整 PBR fixture 回归。Windows 还直接运行完整
`tina_asset_tests`；Linux 至少用 GCC13 重新编译 `tina_asset` 与该测试 TU，并从仓库 build tree 链接同一
测试后端运行 filter。不得用只编译 TU 或临时 probe 代替测试结果。

```powershell
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_asset_tests.exe `
  --gtest_filter=GltfCookTests.* --gtest_color=no
```

`ASSET-SEC-002` 使用 `TypedPayloadMalformedCorpusTests.*` 统一登记 glTF 之外的 current-schema typed payload
恶意输入。资源炸弹用“小 payload + 超限 header/count”构造，parser 必须在按该 count 分配或发布前返回
`Result` error；只返回 borrowed view 的 parser 不存在 caller storage 发布，Prefab/World2D 则用 sentinel
storage 验证完整校验成功前旧值保持不变。下表的 `N/A` 表示 wire format 没有该类字段，不为追求表面全绿
而制造伪 UTF-8、伪 index 或把 opaque half-float image bytes 当作运行时 float 语义。

| Typed payload | 截断/长度 | 越界 index / 自引用 | 超容量/resource bomb | 非法 UTF-8 | NaN/Inf | 失败发布语义 |
| --- | --- | --- | --- | --- | --- | --- |
| Texture2D | header 截断、pixelBytes/尾随不一致 | N/A | dimension 超限，小 payload | N/A | N/A（像素为 opaque bytes） | borrowed view，不发布 owner storage |
| Sprite | 固定 40B 截断/尾随 | N/A | N/A（固定尺寸） | N/A | UV、pivot、pixelsPerUnit | borrowed view |
| Tileset | entry 截断、count/长度不一致 | unknown material flags + reserved tail | tileCount 超限，小 payload | N/A | UV | borrowed view |
| TileMap root | header/stream 截断、尾随 | Cooked root→自身 chunk dependency | layer/property/object count 超限 | layer/property/object string | cell size / object geometry | borrowed view；stream 完整验证后返回 |
| TileMap chunk | cell stream 截断、cell/nonEmpty count 不一致 | Cooked chunk id = parent id | dimension/coordinate 超限 | N/A | N/A | borrowed view |
| SpriteAnimationClip | frame/event block 截断、长度不一致 | dependency index、event range、Cooked self dependency | frame/event/per-frame event count 超限 | N/A | frame duration | borrowed view；event block 全覆盖校验 |
| StaticMesh | vertex/index block 截断、尾随、typed block 未对齐 | vertex index / submesh range | vertex/index/submesh count 超限 | N/A | bounds、vertex stream | borrowed view；形成 typed span 前验证实际地址对齐 |
| SkinnedMesh | skin/geometry block 截断、尾随 | joint parent / influence joint index | joint/vertex/index/submesh count 超限 | N/A | bounds、inverse bind、joint/vertex stream | borrowed view；完整 skin 与 geometry 校验后返回 |
| AnimationClip3D | track/time/value block 截断、尾随 | joint index、key/value exclusive-scan range | track/per-track/aggregate key count 超限 | N/A | duration、key time/value | borrowed view；全部 track 分区校验后返回 |
| Material | 固定 40B 截断/尾随 | flags/reserved 拒绝 | N/A（固定尺寸） | N/A | base color、metallic/roughness | borrowed value view |
| Prefab | node block 截断、长度不一致 | 零/重复 stable ID、self/forward parent | nodeCount 超限，小 payload | N/A | transform、零/非有限 quaternion | 局部 vector 完整校验后 `swap`；失败保留 sentinel |
| EnvironmentMap | image block 截断、byte count/mip 不一致 | N/A | 极端 dimension 在 byte-layout/payload budget 处拒绝 | N/A | N/A（预过滤 image bytes 为 opaque half-float encoding） | borrowed view |
| AudioClip | PCM 截断/尾随、geometry 不一致、PCM 未对齐 | N/A | channel/frameCount 超限，小 payload | N/A | 每个 float PCM sample | borrowed view；形成 typed span 前验证实际地址对齐，writer/parser 对称拒绝 |
| NavigationGrid2D | table 截断/尾随、cellCount 不一致 | invalid flag/cost/reserved | dimension/cellCount 超限，小 payload | N/A | origin/cell size | borrowed view；table 完整校验后返回 |
| Fx2D | 固定 184B 截断/尾随 | dependency index / zero dependency ID / reserved | particle/trail capacity 超限 | N/A | particle/trail float fields | 返回独立 value，不发布 owner storage |
| World2D snapshot | entity/gameplay block 截断、尾随 | 零/重复 stable ID、self/forward parent | entity/gameplay count 超限，小 payload | N/A | transform 与 component values | 局部 vector 完整校验后发布；失败保留 sentinel |

本矩阵已于 2026-08-16 统一执行以下定向 gate：

```powershell
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_asset_format_tests.exe `
  --gtest_filter=TypedPayloadMalformedCorpusTests.* --gtest_color=no
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_asset_tests.exe `
  --gtest_filter=TypedPayloadValidationTests.RejectsMalformedEnvironmentMapWhenTypedRequired --gtest_color=no
```

结果为 `tina_asset_format_tests` 124/124、`TypedPayloadMalformedCorpusTests.*` 17/17、
`tina_asset_tests` 312/312，EnvironmentMap typed-required 定向验证通过。该矩阵不依赖外部资源、网络、GPU、
Physics2D 或 FreeType；`ASSET-SEC-002` 据此转为 Done。

`SpriteAnimationClip` 覆盖 payload/schema、Catalog typed view、dependency contract 与
`SpriteAnimator2D` 的 Once/Loop/PingPong、暂停、倍速和大 delta；`tina_sample_2d` 再提供
`Idle -> Walk -> HitWall` 的产品状态证据。

TileMap 当前最小回归覆盖：root schema v3 与 `TileMapChunk` v1 round-trip（含 layer/object visibility、
chunk ref、parent/layer/coord/extent/non-empty）；旧 schema、重复/零稳定 ID、非法几何/UTF-8 拒绝；recipe
显式 layer block与旧裸 `row` 拒绝；Cooker 在 Manifest 发布前验证 eager Tileset、deferred chunk dependency
和所有非零 tile localId。Runtime 还覆盖仅加载 visible chunk、demand shift 的 cancel/unload、capacity
transaction、retain overflow 自动淘汰与 demand-recency LRU、Asset async active-read move/destroy 生命周期，
同一 chunk 最高 priority 聚合、`priority desc -> layerId -> chunkY -> chunkX` 新请求顺序与已 dispatch IO 不抢占，
以及 residency generation 驱动 dirty cache 重建；render/collision 全部显式传 layer ID。desired load window
单独超 capacity 仍必须验证旧 active set 不变。产品 smoke 必须看到 `objectLayerConsumed=true`、
`objectLayerObjects=2`、唯一 role/kind 消费和 `tileMapStreamRequests/Committed/Resident=2`。priority 小切片只需运行：

```powershell
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_asset_tests.exe `
  --gtest_filter=TileMapStreamTests.AggregatesHighestPriorityAndOrdersOnlyNewRequests
```

```powershell
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_asset_format_tests tina_asset_tests tina_sample_2d --parallel 2 -- /nr:false
cmake --build --preset windows-vnext-bgfx-physics2d-debug `
  --target tina_physics2d_tests --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_asset_format_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_asset_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-physics2d\bin\Debug\tina_physics2d_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
```

## 2D Navigation

日常 Navigation2D 修改只构建独立模块测试，并优先运行新增 suite；TileMap 转换改动同时包含
`TileMapNavigation2DTests.*`。公开头、CMake export 或产品接线完成后，再增量构建 `tina_sample_2d`。

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_navigation2d_tests tina_sample_2d --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_navigation2d_tests.exe `
  --gtest_filter="*Navigation*" --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe `
  --frames=300 --frame-delay-ms=0
```

模块门禁覆盖 `NavigationGrid2DContract` 的尺寸/flag/traversal cost 拒绝、PMR 深拷贝、固定容量与失败事务性、
generation-safe blocker、重叠引用计数、revision、四向/对角确定性 weighted A*、严格防切角/允许切角、
destination cost 与 `pathCost`、blocked endpoint/不可达、分步取消、Grid mutation/address invalidation、query
capacity 失败保留旧结果，以及 Create 后成功 query/blocker mutation 零 PMR allocation。TileMap bridge 覆盖
solid tile + exact full-material-flags cost rule + property-tagged visible Rectangle、重复/零 flags/越界 cost、
wrong layer kind、tagged Point 拒绝和 non-resident chunk 原子失败。

payload/typed gate 还覆盖 `NavigationGrid2DPayloadTests.*` 与 `NavigationFxTypedViewTests.*`：32-byte header、
row-major flags/cost round-trip、reserved/layout/range 拒绝、零 dependency contract、ContentHash，以及从 Cooked
file 深拷贝 immutable data。Editor 统一 smoke 要求 bake/source revision、payload bytes、ready/published 为真且
dirty 为假；Source Import reload 后 authoring overlay 仍独立保留。

产品 smoke 还要求 schema 29 的 `navigationReady=true`、`navigationFromCookedAsset=true`、
`navigationCookedBitExact=true`、静态 Cooked grid 的 solid/rectangle/blocked=`11/0/11`、weighted/max-cost=`1/5`，
以及 Physics bridge 的 synchronizations/adds/updates/removes=`301/1/5/0`、registered/published=`1/1`。
base/dynamic/strict/corner-cut path cells/cost 均为 `5/40`，独立 weighted path=`7/60` 且未经过高代价
`(3,2)` cell；incremental expanded nodes=`1`、revision/mutations=`10/2` 与 `navigationCancelled=true`。
sample exit 0 是结构化产品接线证据；当前导航没有独立视觉结论。

## Sprite2D 自定义 fragment

`ASSET-SHADER-001` 的三个 sample 递进覆盖，改 shader payload、`ShaderBindingRegistry`、extraction 的
shader/uniform resolver 或 bgfx Sprite2D 提交路径时应全跑，因为它们各自断言的是不同的失效模式：

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_asset_tests tina_scene_tests tina_render_bgfx_tests `
           tina_sample_2d_custom_shader tina_sample_2d_shader_materials `
           tina_sample_2d_shader_lighting -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_asset_tests.exe `
  --gtest_filter="ShaderBindingRegistryTests.*:AssetGpuShaderTests.*"
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_scene_tests.exe `
  --gtest_filter="SceneSpriteAssetTest.*"
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d_custom_shader.exe --frames=32
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d_shader_materials.exe --frames=120
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d_shader_lighting.exe --frames=120
```

`flatMaterialTexelDistance == 0` 现在同时是自定义 sampler 的判据：那个 material 在同一 binding key 上发布
一张 1×1 青色 mask，期望色是 `(0,40,40)` 而非纹素原色。所以这个 0 不只要求 UV 正确，也要求 `s_mask` 绑在
stage 2；错一格会让 mask 采到全黑，`minimumMaterialSeparation` 掉到 0、这一项变成 26。sampler register 与
stage 的一致性另有 cook 时检查（`tina_tests --gtest_filter="ShaderSampler*"`，11 个用例），那层不需要 GPU。

`--frames=32` 对 `2d_custom_shader` 已足够（它的证据只需要两相 pinned uniform），另外两个需要 120 帧。
三者的判据都是**帧内**差分而非跨帧亮度变化：`customSpriteDelta` vs `engineSpriteDelta == 0`、
`maximumSameMaterialDelta == 0` 与 `flatMaterialSpread == 0` / `flatMaterialTexelDistance == 0`、以及 `normalVsFlatSeparation` /
`shadowedVsLit` 配 `engineControlSpread`。任何一个 sample 的 `evidenceError` 非空即视为失败，
即使 exit code 为 0——`status=ok` 与 `evidenceCollected=true` 必须同时成立。

Scene 侧的 `SceneSpriteAssetTest.*` 覆盖 shader/uniform resolver 必须成对提供、两个 ref 各自 kind 正确、
wrong-kind 与 stale handle 均 `UnresolvedSprite`、以及隐藏 sprite 不触发任何 resolver。

## Mesh3D 自定义 fragment

与 Sprite2D 共用 payload/registry/extraction，故改动那三处要连带跑 3D 侧。当前**没有** Mesh3D
shader sample，证据只到单测层：

```powershell
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_scene_tests.exe `
  --gtest_filter="SceneMeshAssetTest.*"
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_render_scene_tests.exe `
  --gtest_filter="ShaderUploadDescTest.*"
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_render_bgfx_tests.exe `
  --gtest_filter="BgfxOpaque3DGeometryTest.*"
```

`SceneMeshAssetTest.ResolvesCustomShaderForRigidAndSkinnedMeshes` 要求刚性与蒙皮 item 在同一帧
各自拿到同一对 ref（蒙皮 ref 悄悄留空仍会提交，只是跑引擎 program，所以必须逐 item 断言）；
`Mesh3DShaderWithoutUniformResolverIsUnresolvedMesh` 要求半套 binding 在任一 resolver 运行前
就以 `UnresolvedMesh` 失败。`ShaderUploadDescTest.AcceptsMesh3DBecauseBothDrawPathsCanBindIt`
钉住共享校验器接受 Mesh3D —— 这里拒绝而 backend 能链接就是不可达能力，这里接受而 backend
不链接就是 headless 绿、真后端红。**未覆盖：** GPU 上真的用自定义 Mesh3D fragment 画过一帧，
没有像素证据。

## CameraFollow2D

`CameraFollow2DTest.*` 归属 `tina_scene_tests`，覆盖非法 config/step 事务失败、dead zone、最大速度、
world-bounds clamp、viewport 大于 world 时按轴居中、previous/current simulation center 与 presentation
interpolation。产品 smoke 还要求 `cameraFollowUpdates>0`、已 primed 的 simulation center，且存在
`cameraInterpolatedExtracts>0`；streaming 使用 current center，render extraction 使用 interpolated center。

## 2D-FX

`tina_asset_format_tests`、`tina_asset_tests` 与 `tina_scene_tests` 分别覆盖 `Fx2DPayloadTests.*`、
`NavigationFxTypedViewTests.*` 和 `Fx2DFactoryTests.*`：固定184-byte payload、reserved/range/capacity 拒绝、
恰好一个 required Sprite dependency 与 payload AssetId 对账，以及 factory 创建 Particle/initial burst/Trail 和
空 Sprite fail closed。`tina_scene_tests` 还覆盖 `ParticleSystem2D` / `Trail2D` 的 Create 固定 PMR allocation 与失败
回收、300帧无 storage growth、固定 seed 可复现、burst validation/capacity/stable-key failure 原子性、
stable key 过期后不复用、update preflight 零发布，以及向 `RenderSceneWriter` 的 lifetime/size/color/width
extraction 和 writer capacity failure。A3 进一步覆盖 weak Sprite Handle 保留、空 handle 发布前拒绝、
stale/wrong-kind/missing/zero resolver fail closed、空集合不解析与 Trail 每次非空 extraction 只解析一次。
专项为 Particle 18/18、Trail 13/13；A5 新增 3D Handle 边界后，完整 `tina_scene_tests` 为91/91。

```powershell
cmake --build --preset windows-vnext-debug --target tina_scene_tests --parallel 2 -- /nr:false
out\build\windows-msvc-vnext\bin\Debug\tina_scene_tests.exe --gtest_color=yes
```

product-2d gate 还必须构建并直接运行上述三个 executable，再验证 sample 的 `evidenceSchema=29`。通用结构化
字段包括 `texturesUploaded=3`、`spriteBindingTextures=3`、`spriteTextureLeasesAcquired=3`、
`spriteTextureRetirementsAccepted=3`、`spriteBindingRegistryReleased=true`、
`spriteTextureHandlesInvalidated=3`、`spriteTextureRetirementRecords=3`、
`spriteTextureRetirementReleased=3`、`spriteTextureRetirementLive=0`、
`spriteBindingResolverHits>0`、`tileMapSpriteBindingResolverHits>0`、`particleSpriteBindingResolverHits>0`、
`trailSpriteBindingResolverHits>0`、`sprite2DLightingConfigured=true`、`authoredPointLight2DCount=3`、
`pointLight2DCount=2`、`culledPointLight2DCount=1`、
`shadowOccluder2DCount=2`、`softShadowPointLight2DCount=2`、`normalMappedSpriteCount=1`、
`sceneLightingFrames=submittedRenderFrames`，并要求
`submittedRenderFrames + skippedSuspendedSurfaceFrames=renderExtractions`、
`particleCapacity=12`、`particleRandomSeed=1414090305`、`particleEmitted=10`、
`trailCapacity=8`、`trailSegmentsCreated=3`、`trailBreaks=1`，以及32字符小写 hex
`fxInitialFingerprint`；该 fingerprint 的哈希输入内部 schema 为2。Theme 门禁还要求
`uiThemeDemoRequested=true`、`uiThemeSwitches=2`、`uiThemeButtonActivations=0`、
`uiThemeFinalLight=false`。TreeView 门禁还要求13个 logical item、12个 materialized slot、两次 selection、
最终 stable key `402`/index `12`、滚动、Theme paint 与 Tree/TreeItem selected semantics。300帧 gate 进一步要求
Flow base/pause Screen push/pop=`2/1`、Back/Confirm/Menu action register/clear=`4/4`；无人输入 smoke 要求
`uiFlowBackActionInvocations=0`、`uiFlowConfirmActionInvocations=0`、`uiFlowMenuActionInvocations=0`、
`pauseOpenActionInvocations=0`、`pauseAutoResumeRequests=1`。Runtime UI 定向测试证明 Escape/Gamepad East Back、
未被 focused control 消费的 Enter/Keypad Enter/Gamepad South Confirm，以及未被 TextEdit 优先消费的
P/Gamepad Start Menu 会调用对应 callback；匹配 Up 在 Screen pop 后仍被消费，无 callback 时 gameplay
transition 保留。300帧 gate 还要求
`particleExpired=0`、`particleActive=10`、
`particleExtracted=10`、`trailActive=3`、`trailExtracted=3`。这些字段证明固定配置下的 simulation/extract
数量与初始状态指纹；`pixelCaptureOk` 和单机 golden/非空窗口证据仍单独证明可见输出。

## UI 与视觉

UI 逻辑门禁至少包括：

- generation/root ownership、容量失败与 PMR 回收；
- layout/hit/paint/semantics 的事务提交；
- 50,000 层 structure/layout/hit/paint 非递归 stress 与 popup stable publication；
- routed input、default action、consume/claim、reset/cancel；
- Flow Layer/Screen publication、Back/Confirm/Menu callback 生命周期、Dropdown-first Back、focused-control-first Confirm、TextEdit-first printable P 与 exact Down/Up latch；
- Button/Checkbox/Slider/ProgressBar/RadioButton/TextEdit 的 kind/property/错误路径；
- UTF-8、LF/soft-wrap 多行 TextEdit、UAX #29 grapheme 子集边界编辑、IME preedit/commit/cancel、
  committed caret geometry、Windows IMM32 placement conversion/clear 与 Glyph atlas/FreeType adapter；
- Runtime phase facade 过期、sticky error 与跨 root 拒绝。

Visual 证据必须同时记录 sample 返回码、client-area 尺寸、是否强制终止、blank/black 比例、字体来源和
截图。初始化白帧不得作为稳定画面；截图通过也不能替代 UIA/AT-SPI。

2D/3D 产品 sample 还提供 backend primary-frame 的单机像素门禁。第一次运行采集
`pixelFingerprint`，第二次把同机、同 backend、同尺寸、同资源版本的值传回 exact golden 参数：

```powershell
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0 `
  --expect-pixel-fingerprint=<first-run-pixelFingerprint>

out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0 `
  --expect-pixel-fingerprint=<first-run-pixelFingerprint>
```

通过必须同时看到 `pixelCaptureOk=true`、非零 width/height/bytes、
`pixelGoldenChecked=true` 与 `pixelGoldenMatched=true`。该 exact hash 是 machine-local gate，不得跨 GPU、
driver 或 backend 复制为通用金标。需要可人工查看的 PNG 与 blank/black 分析时，再使用
`tools/windows/CaptureSampleWindow.ps1 -RequireNonBlank`；内置 hash 不替代该窗口证据。
bgfx 截图回调是异步的；backend 在最终 present 后使用有界120-frame/1ms poll wait 等待 render thread
交付。超限必须保留 `FrameCaptureFailed`，gate 不得通过 PowerShell 盲重试把首次失败掩盖为成功。

**`pixelFingerprint` 在同机、同二进制上也会偶发漂移。** 2026-08-29 实测：连续 6 次 `tina_sample_2d
--frames=300`，前两次得到 `76bc083a…` 与 `58f17167…`，后四次稳定在既有基线 `1428d901…`；全部 6 次
`status=ok` 且 `pixelGoldenMatched=true`。所以**单次指纹变化不构成回归证据** —— 在把它当成渲染改动的信号
之前，先不带 `--expect-pixel-fingerprint` 连跑几次，分辨拿到的是稳定值还是漂移值。判断 sample 是否健康优
先看 `status=ok` 与 `evidenceFingerprint`（实测稳定，且是逐字段的行为证据）。漂移根因未查明（疑与首帧
合成/驱动时序有关，参见本文档关于「初始化白帧不得作为稳定画面」的既有约束），故此处只记录现象与应对，
不声称已定位。

## Physics2D 与 Audio

完整 product-2d 图直接运行：

```powershell
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_navigation2d_tests tina_scene_tests tina_physics2d_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests `
           tina_ui_freetype_tests tina_audio_tests tina_audio_miniaudio_tests tina_sample_2d --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_navigation2d_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_scene_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_render_integration_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_freetype_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_physics2d_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_audio_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_audio_miniaudio_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe `
  --frames=300 --frame-delay-ms=0 --ui-theme-demo --ui-tree-demo
```

miniaudio null-device 证明 adapter callback/mix/lifecycle，不证明真实扬声器质量。Physics2D Release bench
是模块基线。统一 schema 使用 `tina_bench`（ADR 0018 schema v1；共享机仅 provisional）。

Audio N7 模块门禁除 one-shot/pitch/pan/fade 外，还覆盖 fixed ring 的 submit/mix/EOF/retire、非 EOF
underrun 静音恢复、整块原子 submit 与 wrap、completion ring 满时 terminal debt、mix-slot reuse ABA、
wrong-owner/bounded shutdown、active callback reader quiescence、terminal absorbing、fractional stream 最小
容量和 terminal priority。关键测试入口包括：

- `PcmStreamSubmitsMixesSignalsEofAndRetires`；
- `PcmStreamUnderrunOutputsSilenceWithoutFakingEof`；
- `PcmStreamSubmitIsWholeChunkAtomicAndWraps`；
- `PcmStreamCancelCompletionSurvivesFullCompletionRing`；
- `DeferredStreamTerminalDoesNotClearReusedMixSlot`；
- `PcmStreamValidationWrongThreadAndShutdownAreBounded`；
- `PcmStreamCancelConcurrentWithRealtimeMixRetiresExactlyOnceBeforeReuse`；
- terminal absorbing/priority、cancel-vs-EOF exactly-once 与 concurrent shutdown 回归。

`MiniaudioDeviceTest.NullBackendConsumesBoundedStreamEofAndCancel` 验证 adapter 作为 realtime consumer 的
EOF/Cancel 路径。产品 300帧还要求 `audioStreamQueued/submitted/eof/mixed/drained/stopped/retired=true`、
submitted/consumed frame 数一致且 `audioStreamUnderrunFrames=0`；当前 product evidence schema 为29。

Physics2D 模块门禁覆盖：`createBody/createShape/createJoint` 独立 generation、多
Box/Circle/Capsule/ConvexPolygon/Chain shape/body、polygon 3..8 顶点严格凸性/非共线/非自交与 local
center/angle transform、shape 单独销毁、sensor enter/exit、Distance/Revolute/Prismatic joint 的
create/query/destroy、spring/limit/motor state、body 级联退休 shape/joint、wrong-world/stale/capacity/PMR
rollback，以及 Chain 4..64 finite/separation/static-only/non-sensor/open-loop lifecycle、multi-segment AABB/ray
query 去重与 TileMap bridge/CharacterController coexistence。产品 300 帧还要求
`physicsSensorEnters>0`、`physicsSensorExits>0`、`physicsJointReady=true`、
`physicsConvexPolygonReady=true`、`physicsRevoluteJointReady=true`、`physicsPrismaticJointReady=true`、
`physicsChainReady=true`。

## Network gate

`tools/windows/RunNetworkGate.ps1` 固化 NET-001 拓扑，跨**两棵**构建树，因为传输层无条件
构建而 TLS 是可选 feature：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/windows/RunNetworkGate.ps1 `
  -OutJson artifacts/gates/network-<date>.json
```

`windows-msvc-vnext` 跑 `tina_network_tests` 与 `tina_sample_network`；
`windows-msvc-vnext-network-tls` 跑 `tina_network_tls_tests`。`-SkipTls` 只在无法提供
mbedTLS 时使用 —— 那样这轮门禁对 TLS 不构成任何证据，故不是默认。

三条断言方式值得说明：

- **sample 跑两次并要求逐字节一致。** 确定性比任何单个字段更重要:运行间有差异意味着
  状态跨帧泄漏或计数器读到未初始化内存。
- **按 JSON 字段做类型比较,不用子串正则。** `httpStatusCode\":200` 这种无锚点模式也会
  匹配 2000,于是一个多出一位的值会静默通过。这条沿用 `RunProduct2dGate.ps1` 的做法。
- **TLS 侧显式要求四个握手用例出现在 OK 列表里**（可信证书连通、主机名不匹配拒绝、
  HTTP over TLS、WebSocket over TLS）。它们是「TLS 编译过」与「TLS 能用」的分界;一个
  跳过了它们的全绿套件对握手不构成证据。

`tina_network_tests` 预期恰好 1 个 skip（本宿主 loopback 吞下多兆字节发送,真实背压
不可达）。门禁记录该数量,使第二个无关的 skip 无法藏在它后面。

**2026-08-29 证据：** `artifacts/gates/network-20260829.json` —— `ok=true`、
`skips=1`、sample 两次运行逐字节一致、`tcpConnectionsAccepted=3`、
公开头扫描 299/299、`readPlatformAnchors=True`（Windows ROOT store 真实读取）。

Windows 同轮 product-2d 拓扑由 `tools/windows/RunProduct2dGate.ps1` 固化：包含
`tina_navigation2d_tests`、`tina_scene_tests` 的上述测试 executable 全部 exit 0 后，再跑 sample 300 帧并校验
`productGate=bgfx-physics-freetype-audio` 与 schema 29 Theme、TreeView、UI Flow、Sprite owner/retirement、
TileMap/Particle/Trail Handle resolver，并校验 `authoredPointLight2DCount=3`、`pointLight2DCount=2`、
`culledPointLight2DCount=1`、两条 `ShadowOccluder2D` 和逐次 submitted lighting snapshot 字段；schema 16 的
双灯双遮挡历史字段继续保留，同轮 `tina_scene_tests` 还覆盖 N3 camera-space culling 的容量与失败语义；

2026-08-13 六项 2D 收口证据：`tina_asset_format_tests` 96/96、`tina_asset_tests` 298/298、
`tina_navigation2d_tests` 16/16、`tina_scene_tests` 156/156、`tina_physics2d_tests`（含 Chain 与
PhysicsNavigationSync bridge）、`tina_editor_tests` 114/114、`tina_editor_app_tests` 13/13。
`tina_sample_2d` 300帧 exit 0 且 `evidenceSchema=29`；Editor 2D/3D 60帧 auto-demo 均 exit 0。完整
product-2d gate 在已通过全部 test executable 与产品 sample 后，发现 Shadow/NormalMap 子脚本仍期望旧
evidence schema；同步为29后，仅重跑失败或直接受影响项：
`RunProduct2dShadowVisualGate.ps1` 与 `RunProduct2dNormalMapVisualGate.ps1` 均 exit 0，且各模式 A/B 可重复、
on/off 或 soft/hard 像素与结构 fingerprint 均存在差异。
N4 另以 committed soft count=2 和 soft/hard 四跑差分覆盖连续 penumbra；N5 再断言默认
`normalMappedSpriteCount=1`、3份 Texture lifecycle，并调用 normal on/off 四跑差分脚本。

2026-08-14 已通过的 Windows 同轮 product-3d 拓扑由 `tools/windows/RunProduct3dGate.ps1` 固化
（TEST-003）：当时默认使用
`windows-msvc-vnext-bgfx-ui-freetype`，直接构建并运行 Core、Scene、AssetFormat、Asset、bgfx Render、
UI、Runtime UI、UI Render bridge 与 FreeType 测试，再执行300帧
`--ui-theme=dark --ui-theme-demo --ibl=on --point-shadow=on --skin-animation=on`。
当前断言以 product-3d evidence schema 16 为准（3 Mesh / 4 Material / 3 共享 Texture、skinned mesh=`1`、
joints=`2`、point/spot 各 authored/committed/culled=`3/2/1`、CSM cascade=`4`、spot/point shadow
authored/submitted=`1/1`、EnvironmentMap 上传/bind/clear/retire 与 `2/4/3/4×4` 尺寸、retirement ledger
全部 Released 且归零、Dark→Light→Dark 与 final-present capture）。schema 15 及更早的逐字段数值只保留在
git history，不作为当前预期；shadow 字段只证明配置与固定级联数，GPU 实绘由 bgfx contract 与产品视觉 gate
证明。开发期 resize 定向 smoke 可直接使用 `--width=1280 --height=720` 与 `--width=1440 --height=900`，
无需重跑完整 gate：

```powershell
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0 --width=1280 --height=720
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0 --width=1440 --height=900
```

完整 gate 还以默认 `IblComparisonFrames=30` 执行 `--ibl=on` 两次和 `--ibl=off` 两次；每次都由 sample
自校验 EnvironmentMap upload/retire 与条件 bind/clear。脚本要求同模式全帧及中央 3D RGB ROI fingerprint/
通道总量稳定、on/off ROI fingerprint 不同，并通过自动回收的临时 raw RGB capture 计算逐像素 L1；L1 至少
达到 ROI 每像素 1 个 code value。alpha、顶部标题、右侧 inspector 与底部状态栏不参与 IBL 可见性结论。
随后 gate 复用一份 `--ibl=on --point-shadow=on --skin-animation=on` baseline，执行一次
`--skin-animation=off`：off 仍需 Animator update/skinned draw=`30/30`、palette joints=`2`，但 pose changes=`0`；
on/off ROI fingerprint 必须不同，raw RGB 长度必须精确等于 pixel count×3，逐字节 L1 至少为 ROI pixel count。
再执行一次 `--point-shadow=off`；off run 的
point shadow authored/submitted 必须为 `0/0`，两次中央 3D RGB ROI fingerprint 必须不同且逐像素 L1 大于0。
三类比较的临时 raw RGB capture 都在脚本 `finally` 退出前回收。

2026-08-15 已通过 schema 16：所有既有运行默认增加 `--transparency=on` 并校验统一透明提交统计；另增加
一次 `--transparency=off`，复用 IBL-on baseline 完成 RGB fingerprint 与逐字节 L1 差分。透明 comparison
与既有 IBL、skin-animation、point-shadow capture 均由同一个 `finally` 路径回收。

历史 N16.4 的两轮验收命令（两轮均为脚本默认 `windows-msvc-vnext-bgfx-ui-freetype` topology）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunProduct3dGate.ps1 `
  -SkipConfigure
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunProduct3dGate.ps1 `
  -SkipConfigure -SkipBuild -OutJson artifacts\gates\product-3d.json
```

推荐用法是先 `-SkipConfigure` 执行 build、测试与 sample，再以
`-SkipConfigure -SkipBuild -OutJson artifacts\gates\product-3d.json` 无重建复验并写报告 `ok=true`。
历史逐 executable 计数（2026-07-29 基线的 `tina_tests` 335/335 等）已被当前数量取代，只保留在 git
history；测试数量属易变证据，不在本文固化。

2026-07-30 当前 DirectionalLight3D 提交候选使用 MSVC 14.50 `cl.exe` 和上述第一条命令完成同轮
build、测试与300帧 sample。`tina_tests` 336/336、`tina_scene_tests` 96/96、
`tina_asset_format_tests` 59/59、`tina_asset_tests` 204/204、`tina_render_scene_tests` 41/41、
`tina_render_bgfx_tests` 63/63、`tina_ui_tests` 488/488、`tina_runtime_ui_tests` 97/97、
`tina_ui_render_integration_tests` 16/16、`tina_ui_freetype_tests` 3/3、`tina_ui_uia_tests` 12/12；
`tina_sample_3d` 300帧 exit 0，最终输出
`product-3d gate ok schema=5 frames=300 theme=dark-light-dark collections=list-tree`。

文档扫描（DOC-002）：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\docs\CheckDocs.ps1
```

UI-003 单机视觉 ROI 门禁（映射单测之外的截图证据；排除 PrintWindow 白帧；可选 baseline 比对）：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunUi003VisualGate.ps1 `
  -SkipBuild -OutDir artifacts\screenshots\ui-003-visual
# 写入/更新本机金标（同机回归）：
#   ... -WriteBaseline
# 默认读取 tools/windows/baselines/ui-003-sample2d-960x540.json
# 当截图 raster 与 logical 尺寸不同时，自动选择同目录的
# ui-003-sample2d-960x540-raster-Npct.json，避免跨采样密度比较 avgRgb。

# 逻辑 / content-scale-like 尺寸矩阵（非 OS Settings DPI；sample --width/--height）
# 含 960×540 / 1200×675 / 1440×810 / 1280×720 / 1920×1080；按尺寸 ROI baseline
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunUi003SizeMatrix.ps1 -SkipBuild
# 首次/刷新各尺寸 baseline：
#   ... -WriteBaselines
```

**已证明：** ContentScale* 映射单测；单机 ROI + blankLike 排除；设计 960×540 absolute 布局 baseline；
逻辑窗口 content-scale-like 矩阵；sample JSON `logicalPixel*` / `framebufferPixel*` / `contentScale*`
一致性（GLFW metrics，非 COM DPI API）；**字体 identity fingerprint**（`fontFingerprint`：env
`TINA_UI_FONT_PATH` / repo fixture path、`sha256`、`freeTypeLikelyOn`、`identity`；baseline schema 3；
与 baseline 不一致时默认 fail，`-AllowFontFingerprintMismatch` 可 provisional 跳过 ROI 比对）。ROI `rectPx`
比较前会按各自 logical-to-capture scale 还原为 logical rect；avgRgb 金标按 raster scale 分文件保存。同一
Windows 宿主在用户切换 OS 设置后已完成 100%/150%/200% 对应 baseline 与独立复跑；100%/150% 另以
`PER_MONITOR_AWARE_V2` Win32 探针确认 system DPI 为 96/144。

**未证明：** 多显示器混 DPI；跨 GPU 像素金标。

## Android 平台后端

`tina_platform_android_tests` 是纯契约断言（无窗口、无 GPU、无 JNI），所以它**能**在宿主交叉编译后推到设备
或模拟器上直接跑 —— 这是它与 compile-only 的区别。宿主没有 preset（理由见
[building.md](building.md)）：

```bash
export ANDROID_NDK_HOME=/path/to/Android/Sdk/ndk/28.2.13676358
cmake -S . -B out/build/tmp-android-x64 -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$ANDROID_HOME/cmake/3.22.1/bin/ninja.exe" \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-android -DANDROID_ABI=x86_64 -DANDROID_PLATFORM=android-24 \
  -DCMAKE_BUILD_TYPE=Debug -DTINA_BUILD_TESTING=ON -DTINA_BUILD_EXAMPLES=OFF \
  -DTINA_BUILD_BENCHMARKS=OFF -DTINA_BUILD_PLATFORM_GLFW=OFF -DTINA_BUILD_SHADERS=OFF \
  -DTINA_BUILD_PHYSICS2D=OFF -DTINA_BUILD_AUDIO_MINIAUDIO=OFF \
  -DTINA_BUILD_UI_FREETYPE=OFF -DTINA_BUILD_UI_UIA=OFF -DTINA_BUILD_RENDER_BGFX=OFF
cmake --build out/build/tmp-android-x64 --target tina_platform_android_tests
adb push out/build/tmp-android-x64/bin/tina_platform_android_tests /data/local/tmp/
adb shell chmod 755 /data/local/tmp/tina_platform_android_tests
adb shell /data/local/tmp/tina_platform_android_tests --gtest_color=no
```

在 Git Bash 下必须 `export MSYS_NO_PATHCONV=1`，否则 `/data/local/tmp` 会被改写成一个 Windows 路径，
`adb push` 报 `secure_mkdirs() failed` 而**退出码仍是 0**、随后的 `chmod` 才失败。

**2026-08-30 记录：80/80** 在 Android 36 x86_64 模拟器实机通过（较上轮 +20：preedit 状态机全表含两条
「什么都不发」、`Ended` 必先于其文本的顺序、光标 UTF-16→codepoint 换算与夹取、512 边界与溢出拒绝、
窗口销毁取消在飞组词、caret latch 与 physical 转换、非法几何拒绝、读取不清除）。同轮宿主侧
`tina_tests` 455/455、`tina_ui_tests` 839/839、`tina_runtime_ui_tests` 151/151、
`tina_platform_glfw_tests` 53/53。

实机四条输入路径共存实测：`keys=1 textCommits=1 composition=1/1/1/0 editCodepoints=2`，
`presses`/`releases` 平衡，`droppedTouches=0 droppedKeys=0`，零 `FATAL`。旋转（native window 替换，
ADR 0034 路径）后计数从 0 重新开始且继续增长 —— 这是**预期**的：manifest 刻意不排除 `configChanges`，
所以 Android 会重建 activity。判读时看的是「旋转后帧数继续爬升且无 error」，不是「计数保持连续」。

**IME 相关的验证边界要说清楚。** 非 ASCII 与组词过程**都无法**从测试工具注入：`adb shell input text` 按
keycode 合成，既表达不了代理对、也不携带组词区。故 APK 提供两条诊断入口，都走**与真实键盘完全同一条**
`InputConnection` 路径而非绕过转换伪造结果：

```bash
adb shell am start -n dev.tina/.TinaActivity --ez tina.commitEmoji true   # U+1F600 走 commitText
adb shell am start -n dev.tina/.TinaActivity --ez tina.composeText true   # ni→nihao→commit 你好
adb logcat -s Tina:I    # 看 composition=start/update/end/cancel 与 preeditDrawn / editCodepoints
```

`composeText` 每 30 帧推进一步：TextEdit 需要一个已提交的帧才可聚焦，且分帧才能让 preedit 在屏幕上停留
足够久、各 stage 落在不同 poll。判读时 `preeditDrawn=true` 是关键 —— 平台的 stage 计数在**无 TextEdit
聚焦**时照样递增（`routeTextComposition` 走「无焦点」分支返回未消费），所以计数递增而 `preeditDrawn` 恒为
false 恰好就是「平台通了、UI 从未看见」。`editCodepoints` 上升则证明组词最终落成了真实文本，而不是画了一段
preedit 又被丢掉。典型序列：第 120 帧 `composition=1/1/0/0 preeditDrawn=true editCodepoints=0`，
第 180 帧 `1/1/1/0 preeditDrawn=false editCodepoints=2`。

**两条判读陷阱，都实测踩过：**

- **先点面板再按键会让 `keys=` 归零**，因为点击把焦点从 TextEdit 移走了（正确行为）。反过来说，**聚焦的
  TextEdit 会消费除 Tab / Enter / Escape 以外的每一个键**，所以方向键在有文本框聚焦时也不产生 action。
  按顺序验：先按 Enter（此时 TextEdit 仍聚焦），再点面板。
- **进度行突然停止而 `Tina` tag 里什么都没有时，先看 `adb logcat -s AndroidRuntime:E`。** 帧循环跑在
  Handler Runnable 上，Java 异常会静默杀掉它。实测：`CursorAnchorInfo.Builder.build()` 在设了位置却没设
  matrix 时抛 `IllegalArgumentException`，而这条路径**只有真实输入法索取 cursor updates 时才走到** ——
  比任何脚本化诊断都晚，所以在那之前从未被执行过。

**未证明：** ① **候选窗是否真的跟随光标。** `CursorAnchorInfo` 已按 `requestCursorUpdates()` 上报，但某个
输入法是否索取、候选窗是否真的跟着走，不由本工程决定，需要一个会索取 cursor updates 的中文输入法加人眼；
归 `TEXT-001`。② **真实中文/日文输入法的完整组词矩阵**（`composeText` 证明的是链路，不是任一具体输入法的
行为）。③ **真机 Vulkan 路径**（模拟器 `vulkan.ranchu.so` 在 swapchain 创建时 segfault，只能验证 GLES）。

## Linux 与 sanitizer

Linux 门禁必须记录 compiler、stdlib、CMake、vcpkg baseline、display backend 和 sanitizer 环境。
Clang preset 通过 chainload 固定 libstdc++15；Ubuntu 默认旧工具链不能冒充正式结果。

### Compile-only 不是测试门禁

任务只要求验证 Linux 编译兼容性时，必须显式记录 `mode=compile-only`。该模式最多对当前
source/toolchain/target tuple 执行一次 configure 和一次最小 target build，不运行 GoogleTest、sample、
workspace smoke、visual/platform gate；即使构建生成了测试 executable，`testRuns` 和 `sampleRuns` 也必须为0。
已有匹配指纹的成功编译结论时直接复用，不重复编译，更不能为了“确认”而重跑测试。

compile-only 取得退出码和首个错误后立即进入资源收尾，成功与失败执行同一规则。Docker/临时 worktree
产生的 build、staging、install、consumer tree，以及本轮容器、volume、一次性镜像/缓存、编译/helper/watchdog
进程和 agent 都必须定向回收。失败产物只允许保留到错误完成记录，不得跨任务保留。完整生命周期与保留例外
见 [building.md](building.md#linux-compile-only-与临时资源生命周期)。

### Docker Desktop（Windows 宿主）— GCC13 Null 子图

见 [m12-evidence-linux.md](evidence/m12-evidence-linux.md)。快捷：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxDockerGate.ps1 `
  -Gate gcc13-null -OutJson artifacts\gates\test-001-linux-gcc13-null.json
```

2026-07-23 tip `e0d94faa`：GCC13 Null exit 0。  
2026-07-24 tip `d883d787`：GCC13 Platform/GLFW + Xvfb exit 0（34/34）。  
2026-07-24 tip `66374135`：Clang22 Null + Clang22 sanitizer Null 全 executable exit 0。  
详见 [m12-evidence-linux.md](evidence/m12-evidence-linux.md)；TEST-001 主验收已关。

### 本机 Linux / Clang sanitizer

```bash
cmake --preset linux-clang22-vnext-sanitize
cmake --build --preset linux-clang22-vnext-sanitize-debug \
  --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests tina_sample_null

ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
LSAN_OPTIONS=exitcode=23 \
./out/build/linux-clang22-vnext-sanitize/bin/tina_tests --gtest_color=no
```

其余 executable 逐个运行。只有 GLFW/X11 进程允许使用已记录的 `_XimOpenIM` 精确 suppression；基础、
Asset、UI、RenderScene 测试不得继承宽泛 suppression。

## 证据记录模板

每次正式门禁至少记录：

```text
commit/worktree: <sha + dirty files if any>
date/platform/toolchain: <...>
preset/configuration: <...>
mode/runs: <gate | compile-only; configure/build/test/sample 次数>
build command + exit code: <...>
test/sample command + exit code: <...>
test summary / structured JSON: <...>
visual/sanitizer evidence: <not run | path/result>
resource cleanup: <all temporary trees=absent; ownedTemporaryBytesAfter=0; processes/containers/volumes/images/owned cache/agents=0>
retained caches: <owner + path-or-id + bytes + 保留原因；没有则为 none>
cleanup status: <complete | incomplete + 未回收或 unknown 项>
known limitations: <...>
```

测试日志不得包含 token、凭据、用户名或不必要的绝对路径。
