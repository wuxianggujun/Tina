# 构建与运行

本文只记录当前可执行命令。测试数量和一次性机器结果见 [testing.md](testing.md) 与
[M12 Windows 证据](m12-evidence-windows.md)。所有 Windows Debug/Release 构建在同一 build tree
内串行执行；日常验证禁止 `--clean-first` 和删除 `out/build`。

## 环境

| 平台 | 当前支持基线 | 备注 |
| --- | --- | --- |
| Windows | Visual Studio 2026 / MSVC 19.50、CMake 4.2.3、`VCPKG_ROOT` | MSVC 全局 `/utf-8`、`/Zc:__cplusplus` |
| Linux GCC | GCC 13+、Ninja、CMake 3.25+ | `linux-gcc13-vnext*` |
| Linux Clang | Clang 22.x + GCC/libstdc++ 15.x | chainload toolchain；sanitizer 使用独立 preset |

项目不使用 C++ Modules；CMake 显式关闭 Modules dependency scan。依赖由 vcpkg manifest/baseline
管理；`xxHash` 与 `mikktspace` 是 backend-neutral SDK 的必需 package，bgfx 源码由
`thirdparty/bgfx.cmake` 锁定。`TINA_BUILD_LEGACY=ON` 会立即失败。

## 编译、运行与测试是独立请求

用户请求决定本轮授权边界，不能因为工程文档列出了完整 gate 就自动扩大范围：

| 用户请求 | 允许执行 | 明确禁止 |
| --- | --- | --- |
| 编译、构建、生成版本、给出编译版本 | 必要的 configure；指定 target build；报告产物 | 执行 GoogleTest/CTest、sample、smoke、产品/视觉 gate；启动产物 |
| 编译并运行 | 上述 build；启动用户指定的程序 | 未明确要求的 GoogleTest、smoke 与其他 gate |
| 编译并测试 | 上述 build；运行明确相关的测试范围 | 未授权的产品运行、视觉或平台 gate |
| 编译给用户手动测试 | build；交付可执行文件绝对路径、时间与大小 | 代替用户启动程序或追加任何自动测试 |

因此，“编译成功”只表示编译和链接 exit 0，不表示测试、启动、真实资源导入或视觉效果通过。用户急需人工验证
真实 Editor 时，优先构建 `tina_editor_desktop` 并交付 `TinaEditor.exe`，到此停止。后续测试必须得到单独明确请求。

## 增量构建与验证层级

以下是获得对应验证授权后的范围选择规则，不会覆盖上一节的用户请求边界。

- 小功能/单一切片：复用常驻 build tree，只构建改动直接影响的 target，并只运行新增用例及直接回归的
  `--gtest_filter`；没有改 CMake/preset/toolchain 时不主动重新 configure，不运行完整产品图。
- 垂直切片闭环：在上述精确测试后，只补对应 sample 的短或约定帧数 smoke。若多个小切片连续影响同一
  target，合并后集中执行一次，不为每个小提交重复构建和测试。
- 大功能/里程碑：仅在 Backlog 项关闭、跨模块公共契约或共享基础设施变化、release candidate、正式证据
  归档时，运行相关 executable 全集与完整 product/platform/sanitizer gate。

完整门禁命令是里程碑入口，不是日常默认命令。选择范围的详细规则见 [testing.md](testing.md)。

```powershell
cmake --version
cmake --list-presets
```

文档本地链接、preset 名与常见 target 名可用：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\docs\CheckDocs.ps1
```

（DOC-002；不扫描 `out/`/`build/`/`thirdparty/`。）

## Preset 选择

| Preset | 图 | Manifest features |
| --- | --- | --- |
| `windows-msvc-vnext` | Null/Core/Asset/Scene/UI 基础图 | `tests` |
| `windows-msvc-vnext-profile-tracy` | Null 基础图 + Tracy 0.13.1 开发定位 | `tests;profile-tracy` |
| `windows-msvc-vnext-platform` | GLFW + NullRender | `tests;platform-glfw` |
| `windows-msvc-vnext-bgfx` | GLFW + bgfx + Desktop/产品样例 | `tests;platform-glfw`（默认 preset 继承） |
| `windows-msvc-vnext-physics2d` | Null + Box2D | `tests;physics2d` |
| `windows-msvc-vnext-bgfx-physics2d` | bgfx + Box2D | `tests;platform-glfw;physics2d` |
| `windows-msvc-vnext-ui-freetype` | Null + FreeType UI | `tests;ui-freetype` |
| `windows-msvc-vnext-bgfx-ui-freetype` | bgfx + FreeType UI | `tests;platform-glfw;ui-freetype` |
| `windows-msvc-vnext-audio-miniaudio` | Null + miniaudio adapter | `tests;audio-miniaudio` |
| `windows-msvc-vnext-audio-miniaudio-codecs` | miniaudio + Vorbis/Opus | 对应 codec features |
| `windows-msvc-vnext-bgfx-product-2d` | bgfx + Physics2D + FreeType + miniaudio | `tests;platform-glfw;physics2d;ui-freetype;audio-miniaudio` |
| `linux-gcc13-vnext` | Linux Null | `tests` |
| `linux-gcc13-vnext-audio-miniaudio` | Linux Null + miniaudio adapter | `tests;audio-miniaudio` |
| `linux-gcc13-vnext-platform` | Linux GLFW/X11 | `tests;platform-glfw` |
| `linux-gcc13-vnext-platform-wayland` | Linux GLFW/Wayland | `tests;platform-glfw;wayland` |
| `linux-clang22-vnext` | Clang Null | `tests` |
| `linux-clang22-vnext-sanitize` | Clang ASan/UBSan/LSan | `tests` |
| `linux-clang22-vnext-platform*` | Clang GLFW/X11/Wayland + sanitizer 变体 | 对应 platform features |

`TINA_BUILD_UI_FREETYPE`、`TINA_BUILD_AUDIO_MINIAUDIO`、`TINA_BUILD_PHYSICS2D`、
`TINA_BUILD_PLATFORM_GLFW` 和 `TINA_BUILD_RENDER_BGFX` 必须与相应 manifest feature 同时启用；
CMake 会拒绝不匹配组合。

## Worktree 与常驻构建树

Preset 的 `binaryDir` 使用 `${sourceDir}/out/build/...`。每个 Git worktree 的 `sourceDir` 不同，因此
每个 worktree 都会得到独立的 CMake cache、对象文件、第三方库和 `shaderc`；Git 只共享对象数据库，
不会共享构建产物。禁止让两个 worktree 指向同一个 `binaryDir`，因为 `CMakeCache.txt`、生成项目和依赖
追踪记录绑定了源码绝对路径。

多 worktree 功能开发默认采用以下顺序：

1. 功能 worktree 只完成代码、定向静态检查、`git diff --check` 和必要的轻量验证，不默认配置或构建
   bgfx、shader toolchain、完整产品图。
2. 功能分支提交后先合并到核心集成 worktree；确认提交已可达后删除已合并 worktree 和临时分支。
3. 只在核心集成 worktree 的常驻 `out/build/<preset>` 中执行增量 configure/build/test，从而复用已经
   生成的 vcpkg、bgfx、shaderc、对象文件和测试 executable。
4. 多个功能切片并行开发时，先全部合并，再按受影响 target 运行一次集中验证；不要在每个 worktree
   重复运行同一套完整 gate。

只有无法通过代码审查、header isolation 或 backend-neutral 单测降低风险的独立高风险切片，才允许在
功能 worktree 创建专属 build tree。该 build tree 仍不得与核心 worktree 共用，并应在 worktree 回收时
一并视为临时资源。

修改 `CMakeLists.txt`、preset 或 toolchain 后，核心 worktree 的下一次构建可能触发 CMake regenerate。
先构建最小受影响 target，并观察依赖图；若已有 `shaderc`/第三方库却开始大范围重编，先停止并检查
regenerate、编译命令或依赖追踪变化，不把重型工具链重编当作默认步骤。

### Linux compile-only 与临时资源生命周期

`compile-only` 是独立验证模式，不是缩短版测试门禁。它只证明指定 Linux compiler 能配置并编译当前
source/toolchain/target tuple，执行规则如下：

1. 每个 tuple 最多执行一次 configure 和一次最小 target build。若已有匹配的 source fingerprint、toolchain
   identity、target 与成功结果，直接复用该结论，不为“再次确认”重建。
2. compile-only 不启动任何 GoogleTest executable、sample、workspace smoke、visual/platform gate。若任务还要求
   运行测试，应把它记录为单独的 test gate，并显式复用已有 binary；不得把测试偷偷附加到 compile-only 后面。
   仅要求 Linux 编译兼容性时，即使刚生成了测试 executable，也必须保持 `testRuns=0`，不能以“顺便验证”为由
   运行新 binary 或重跑历史测试结果。
3. Docker、WSL、临时 worktree 或一次性 Linux preset 产生的 build tree 默认都是 **ephemeral**。取得 compiler
   exit code 和首个错误记录后，无论成功失败都立即删除；失败产物只允许保留到错误已记录，不跨任务保留。
4. 同一轮不同 helper/container 只复用已构建 binary 和 hash，不再调用 CMake。需要不同 compiler/toolchain 的
   独立结论时才建立新的 tuple，且每个 tuple 完成后分别回收。

临时资源收尾必须覆盖：本轮 build tree、staging/install/consumer tree、临时目录、容器、named volume、一次性
镜像/构建缓存、compiler/linker/helper/watchdog/窗口管理器进程和 agent。不得删除外部共享 `VCPKG_ROOT`，也不得
用全局 `docker system prune` 或全量删除 Windows 核心 build tree 代替定向清理。只有明确登记为核心集成增量缓存
的 tree 可以保留；`docker-*`、`wsl-*`、`tmp-*` 和 gate 专用 tree 默认不在保留名单。

启动 gate 前必须建立本轮资源账簿：为每个临时 path/container/volume/image/cache 记录 owner、用途和回收方式，
并记录本轮临时文件的合计占用。只清理账簿中由本轮创建或明确独占的资源，不猜测、不全局 prune；共享资源若需
保留，转入 `retainedCaches` 并写明 owner、路径或 ID、占用和保留原因。这样既避免误删共享缓存，也避免无人认领
的 Linux 构建产物继续把项目目录膨胀到数十 GiB。

收尾报告至少写明以下事实，不能只写“已清理”：

```text
mode=compile-only configureRuns=0|1 buildRuns=0|1 testRuns=0 sampleRuns=0
buildTree=<path> buildTreeState=absent
stagingTreeState=absent installTreeState=absent consumerTreeState=absent temporaryDirectoriesState=absent
ownedTemporaryBytesBefore=<bytes> ownedTemporaryBytesAfter=0
compilerProcesses=0 linkerProcesses=0 helperProcesses=0 watchdogProcesses=0 windowManagerProcesses=0
containers=0 namedVolumes=0 oneShotImages=0 ownedBuildCacheBytesAfter=0 agents=0
retainedCaches=<owner + path-or-id + bytes + 保留原因；没有则为 none>
```

开始前只对本轮明确使用的临时 tree 记录占用字节，结束后验证路径不存在并记为0；可列出 `out/build` 的直接
子目录定位异常增长，但不要递归扫描整个仓库。任何保留项都必须逐项记录路径、占用和保留原因，未登记的
Linux/Docker/WSL 临时 tree 仍存在即视为收尾失败。不能因为后续“也许会再测”把数十 GiB 的
vcpkg/bgfx/shaderc/object 产物留在项目中。

上述资源块必须进入 gate 日志和任务最终报告。某项无法查询时必须写 `unknown` 和原因，并将
`cleanupStatus=incomplete`；只有所有本轮独占临时资源均为 `absent/0`，且保留项全部登记后，才可写
`cleanupStatus=complete` 或“资源已释放”。

### Linux Editor 两阶段复用与资源回收

Linux Editor 的 `zenity` / `kdialog` 产品门禁使用唯一专用 build tree：
`out/build/docker-linux-gcc13-vnext-bgfx-editor`。两种 helper 环境不得各自重建 bgfx、shaderc 或 Editor：

1. `gcc13-editor-zenity` 是 primary：唯一一次 configure/build，随后运行 `tina_editor_app_tests` 与 2D/3D
   短 smoke；全部成功后写入当前 source fingerprint 和三份 executable SHA-256。
2. `gcc13-editor-kdialog` 是 secondary：要求 primary stamp 存在并逐项校验 source/binary hash，只运行真实
   `kdialog` open/save/folder/cancel；**不调用 CMake，不重复运行测试或 workspace smoke**。
3. secondary 成功后自动删除该专用 build tree。失败时只允许短暂保留 tree 以读取首个错误；错误记录完成后
   同一轮定向重试可以复用，重试结束或任务收尾时必须删除，不能跨任务累积 Linux/vcpkg/bgfx/shaderc 产物。
4. 每个容器都使用 `--rm`；gate 对 probe/helper 使用独立进程组和硬超时，并回收 clipboard、watchdog、
   Openbox 与临时 UTF-8 fixture。收尾输出必须包含 build tree、进程和临时目录的资源状态。

这项删除规则只针对门禁专用临时 tree；Windows 核心集成 worktree 的常驻增量 build tree 继续保留复用，
不得用全量 wipe 代替定向资源管理。

## Windows 日常 UI 增量构建

日常 UI 开发优先使用固定脚本，不手写 MSBuild native 参数：

```powershell
# 自动复用已有 build tree；缺少 CMakeCache.txt 时才 configure。
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunMsvcUiTests.ps1

# 只跑本次相关用例；仍会先增量构建 tina_ui_tests。
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\RunMsvcUiTests.ps1 `
  -GTestFilter 'UITextPaintEmitterTests.*'
```

脚本固定使用 MSVC preset、CMake `--parallel 2` 和 MSBuild `/nr:false`，构建结束后验证本轮新建的
`cmake/MSBuild/cl/link` 进程已退出；不执行 `--clean-first`。切换 build graph 时同时传
`-ConfigurePreset`、`-BuildPreset`、`-BuildTree`，切换输出配置时传 `-Configuration Release`。

必须手工构建时，统一使用以下参数顺序：

```powershell
cmake --build --preset windows-vnext-bgfx-debug `
  --parallel 2 --target tina_ui_tests -- /nr:false
```

并发度由 CMake `--parallel` 管理；不要再向 MSBuild 重复传 `/m` 或 `/v`。这可避免 Git Bash/MSYS
对斜杠参数做路径转换，也确保 MSBuild node reuse 不残留编译进程。

## Windows Null 基础图

```powershell
cmake --preset windows-msvc-vnext
cmake --build --preset windows-vnext-debug `
  --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests `
           tina_scene_tests tina_render_scene_tests tina_asset_format_tests tina_asset_tests `
           tina_audio_tests `
           tina_sample_null tina_sample_asset tina_sample_2d_infrastructure tina_sample_2d_tilemap `
           tina_sample_3d_extraction tina_assetc tina_catalog_validate --parallel 2 -- /nr:false
```

多配置输出位于 `out/build/windows-msvc-vnext/bin/Debug` 或 `bin/Release`。对应 executable 必须使用
同一配置目录的 DLL；Debug/Release 不得混跑。

## Windows / Linux 安装 SDK consumer

先配置 Null 图，再运行安装 consumer 门禁：

```powershell
cmake --preset windows-msvc-vnext
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkConsumerGate.ps1 `
  -BuildDirectory out\build\windows-msvc-vnext -Configuration Debug
```

脚本以 `--parallel 1` 和 `/nr:false` 增量构建 backend-neutral SDK 闭包，将当前配置安装到 build tree
内的独立 prefix，扫描实际安装头的第三方 include/type token，然后从 `tests/sdk_consumer` 建立独立
build tree。consumer 的 CMake 入口只有 `find_package(Tina CONFIG REQUIRED)` 和 `Tina::GameSDK`，并拒绝
源码树 `include` 路径或安装 prefix 外的 Tina include。Debug/Release package 必须与 consumer 配置一致。

PlatformGlfw 安装 consumer 使用独立 prefix/build tree，不污染 Null gate：

```powershell
cmake --preset windows-msvc-vnext-platform
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkConsumerGate.ps1 -Consumer PlatformGlfw -Configuration Debug
```

consumer 通过 `find_package(Tina CONFIG REQUIRED COMPONENTS PlatformGlfw)`，只链接
`Tina::PlatformGlfw`，创建隐藏窗口、读取初始 metrics、poll 一帧并 shutdown。

AudioMiniaudio consumer 只链接独立 adapter；默认图和启用 Vorbis/Opus 的图都需通过：

```powershell
cmake --preset windows-msvc-vnext-audio-miniaudio
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkConsumerGate.ps1 -Consumer AudioMiniaudio -Configuration Debug

cmake --preset windows-msvc-vnext-audio-miniaudio-codecs
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkConsumerGate.ps1 `
  -BuildDirectory out\build\windows-msvc-vnext-audio-miniaudio-codecs `
  -Consumer AudioMiniaudio -Configuration Debug
```

consumer 会查询内置 WAV/FLAC/MP3 capability，启动 null backend、等待 callback，再 stop/shutdown；codec 图
同时验证安装 target 能从 consumer toolchain 解析 `Vorbis`、`Opus` 与 `OpusFile`。

DesktopBootstrap consumer 只链接组合入口，并分别覆盖基础 bgfx 与可选 FreeType 图：

```powershell
cmake --preset windows-msvc-vnext-bgfx
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkConsumerGate.ps1 -Consumer DesktopBootstrap -Configuration Debug

cmake --preset windows-msvc-vnext-bgfx-ui-freetype
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkConsumerGate.ps1 `
  -BuildDirectory out\build\windows-msvc-vnext-bgfx-ui-freetype `
  -Consumer DesktopBootstrap -Configuration Debug
```

Linux GCC13 四个安装 consumer 使用同一安装头扫描和仓库外 consumer：

```bash
export VCPKG_ROOT=/opt/vcpkg
tools/linux/run-sdk-consumer-gate.sh
tools/linux/run-sdk-platform-glfw-consumer-gate.sh
tools/linux/run-sdk-desktop-bootstrap-consumer-gate.sh
tools/linux/run-sdk-audio-miniaudio-consumer-gate.sh
```

基础脚本默认增量配置 `linux-gcc13-vnext`，PlatformGlfw wrapper 默认使用
`linux-gcc13-vnext-platform`，DesktopBootstrap wrapper 默认使用 `linux-gcc13-vnext-bgfx`，AudioMiniaudio
wrapper 默认使用 `linux-gcc13-vnext-audio-miniaudio`；两个窗口 consumer 通过 `xvfb-run` 执行。可用
`TINA_SDK_CONFIGURE_PRESET`、
`TINA_SDK_BUILD_DIRECTORY`、`TINA_SDK_CONFIGURATION`、`TINA_SDK_BUILD_JOBS` 与
`TINA_SDK_CONSUMER` 覆盖工具链、build tree 和 consumer。
Windows 主机可通过现有 GCC13 Docker 镜像运行相同门禁：

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

这些门禁证明 Windows/Linux headless/Null SDK、独立 PlatformGlfw、AudioMiniaudio 和
DesktopBootstrap/RenderBgfx 闭包，并在 Windows 覆盖可选 UIFreetype/Vorbis/Opus 图。`xxHash`、
`mikktspace`、`glfw3`、`Freetype`、`Threads` 与可选 codec package 继续由 consumer toolchain 解析；
RenderBgfx 在 Tina prefix 中安装最小 `bgfx`/`bx`/`bimg` runtime package，不包含 shaderc、图片 codec 或
离线工具。每个 consumer gate 都先安装到 staging prefix，再物理移动到不同名的
relocated prefix；原 prefix 消失后，package 路径扫描、`find_package`、链接和运行只允许使用新位置。
这证明同一 OS/toolchain 内的 moved-prefix relocatability，不替代跨发行版 artifact transfer。Docker
入口固定限制为 2 CPU/8 GiB，并为每个 SDK gate 使用独立的 container-only build directory，避免
WSL `/mnt/c` 与 Docker `/work/tina` 共用绝对路径 cache。

跨发行版 artifact transfer 是 **release/ABI candidate 门禁**，不属于日常开发或每次 SDK 修改的必跑项。
需要生成发布证据时，下面的编排器才会让 Ubuntu 24.04/GCC 13 producer 将 Release GameSDK 写入 named
volume，再由不挂载 Tina 源码的 Debian 13/GCC 14 consumer 使用独立 vcpkg/xxHash 解包、配置、链接和运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkCrossDistroGate.ps1 `
  -OutJson artifacts\gates\sdk-001-linux-cross-distro-consumer.json
```

镜像层可以跨执行复用；容器和 artifact volume 无论成功失败都由 wrapper 回收。只有完整命令 exit 0 和 JSON
证据同时存在时，才能把该 tuple 记录为已验证；它仍不等于正式 ABI 兼容承诺。

## Windows Platform/Desktop/bgfx

```powershell
cmake --preset windows-msvc-vnext-platform
cmake --build --preset windows-vnext-platform-debug `
  --target tina_tests tina_platform_glfw_tests tina_sample_platform --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-platform\bin\Debug\tina_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-platform\bin\Debug\tina_platform_glfw_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-platform\bin\Debug\tina_sample_platform.exe --frames=300 --frame-delay-ms=0

cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug `
  --target tina_tests tina_runtime_ui_tests tina_platform_glfw_tests tina_render_bgfx_tests `
           tina_sample_desktop tina_sample_2d_infrastructure_bgfx tina_sample_3d_infrastructure `
           tina_sample_ui_showcase tina_sample_3d tina_sample_2d --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_render_bgfx_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_desktop.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d_infrastructure_bgfx.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d_infrastructure.exe --frames=300 --frame-delay-ms=0
```

GLFW/GLFW sample 在 Linux X11/Wayland 图中应通过 `xvfb-run` 或受控 compositor 运行；X11 的
`_XimOpenIM` suppression 只能用于初始化 GLFW 的进程，不能扩大为 Tina 全局 suppression。

## UI control showcase

`tina_sample_ui_showcase` 在普通 bgfx 图也可构建，但默认图未启用 FreeType，只能看到文字 placeholder。
用于完整控件、中文、按钮状态层次和 Dark/Light 换肤验收时，使用专用 FreeType 图：

```powershell
cmake --preset windows-msvc-vnext-bgfx-ui-freetype
cmake --build --preset windows-vnext-bgfx-ui-freetype-debug `
  --target tina_sample_ui_showcase tina_ui_tests tina_runtime_ui_tests `
           tina_ui_render_integration_tests tina_ui_freetype_tests --parallel 2 -- /nr:false

out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_sample_ui_showcase.exe
out\build\windows-msvc-vnext-bgfx-ui-freetype\bin\Debug\tina_sample_ui_showcase.exe `
  --frames=150 --frame-delay-ms=0 --theme=dark --auto-demo
```

交互模式关闭窗口退出；自动模式输出 JSON 并校验20个控件、两次换肤、Slider→ProgressBar、
Dropdown/List/Tree selection、Tree expansion、ScrollView offset 和 UI root 生命周期。可用
`--theme=light` 改初始主题。字体仍按 CMake cache、环境变量
`TINA_UI_FONT_PATH`、可选 repo fixture 的顺序解析；没有真实字体不得记录为 CJK 视觉通过。

## 完整 product-2d 图

```powershell
cmake --preset windows-msvc-vnext-bgfx-product-2d
cmake --build --preset windows-vnext-bgfx-product-2d-debug `
  --target tina_sample_2d tina_navigation2d_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests `
           tina_ui_freetype_tests tina_physics2d_tests tina_audio_tests tina_audio_miniaudio_tests `
           tina_asset_tests --parallel 2 -- /nr:false
```

字体可通过 `-DTINA_UI_FONT_PATH=C:/path/font.otf` 或环境变量 `TINA_UI_FONT_PATH` 注入。没有字体时
FreeType test 可以 `GTEST_SKIP`，但不能把 skip 记录为 CJK 视觉通过。

```powershell
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_render_integration_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_ui_freetype_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_physics2d_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_audio_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_audio_miniaudio_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_navigation2d_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx-product-2d\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0 --ui-theme-demo --ui-tree-demo
```

完整图的结构化标签应为 `productGate=bgfx-physics-freetype-audio`。

一键复现（configure 可跳过）：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunProduct2dGate.ps1
```

主 gate 在模块测试与 schema 29、`texturesUploaded=3` 产品 sample 后，依次运行 soft/hard shadow
差分与 normal-map on/off 四跑（on×2 + off×2）差分。只复验可见差分时可直接运行；两者都是同
host/backend 证据，不是跨 GPU exact golden：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunProduct2dShadowVisualGate.ps1
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunProduct2dNormalMapVisualGate.ps1
```

完整 product-3d 同轮门禁：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunProduct3dGate.ps1
```

## Asset CLI

先构建工具，再运行工具；工具不读取未构建的旧 binary：

```powershell
cmake --build --preset windows-vnext-debug --target tina_assetc tina_catalog_validate --parallel 2 -- /nr:false
out\build\windows-msvc-vnext\bin\Debug\tina_assetc.exe --out <catalogRoot>
# stateless recook 也写 fresh root，不覆盖已有 --out
out\build\windows-msvc-vnext\bin\Debug\tina_assetc.exe --out <catalogRoot> --stage-out <candidateRoot>
out\build\windows-msvc-vnext\bin\Debug\tina_assetc.exe --out <catalogRoot> --recipe <recipe> `
  --source-root <authoringRoot> --import-state <toolCache>\import-state.tmeta
# dirty/added/removed batch：--out 只读作为 baseline，候选写入 fresh stage
out\build\windows-msvc-vnext\bin\Debug\tina_assetc.exe --out <catalogRoot> `
  --recipe <recipeA> --gltf <sceneB> --source-root <authoringRoot> `
  --import-state <toolCache>\import-state.tmeta `
  --stage-out <candidateRoot> --stage-import-state <toolCache>\candidate.tmeta
out\build\windows-msvc-vnext\bin\Debug\tina_catalog_validate.exe --root <catalogRoot> --typed-payloads
out\build\windows-msvc-vnext\bin\Debug\tina_sample_asset.exe --frames=60 --catalog=<catalogRoot>
```

成功为 exit 0；验证失败为 exit 1；参数错误为 exit 2。Catalog manifest 相对路径和派生 object path 已校验
UTF-8，并拒绝绝对路径与 `..` 逃逸。glTF Cooker 把 authoring 输入视为不可信：主路径与 percent-decoded
外部 URI 要求 strict UTF-8 without NUL；主文件及 relative buffer/image 均从一次打开的 handle/fd 快照
读取，外部最终路径必须位于主文件最终 authoring root 下。root 内 symlink/junction 可用，逃逸链接、
读取期间替换、超出 file/count/range/parser/decode/output 预算均结构化失败，不产生可发布的半包。显式
`--source-root` + `--import-state` 会记录实际消费 bytes 的 provenance，并只在 package 完整验证后提交
tool-side state；state 路径不得位于部署 Catalog root。`--recipe`/`--gltf` 可重复组成一个 batch。同一命令第二次执行会在解析 recipe/cgltf 前 probe 当前
manifest/state/source：全部不变时 JSON 返回 `cookMode=clean-reuse`、`unitsRecooked=0`、`objectsCooked=0`、
`importStateCommitted=false`，且 manifest/object/state 均不改写。旧 state schema、revision 或 source 变化返回
`full-recook`；部分 unit dirty/added/removed 返回 `incremental-recook`。已有 baseline 的非 clean 执行必须提供 fresh
`--stage-out` 与独立 `--stage-import-state`：clean object 原样复制，只有 dirty/added unit 运行 importer，removed output
不进入候选。候选 package/state 完整验证并互相绑定，但工具不覆盖 `--out` live root；Runtime 使用
`reloadCatalog(stageRoot)` 完整验证 stage，并在 resident replacement generation、结果与新 index 全部 staging 成功后
提交；Store capacity 必须预留双驻留 headroom。不运行旧 schema 迁移。

## Linux Null 与 sanitizer

Windows 宿主可用 Docker Desktop 复现 GCC13 Null / Platform（TEST-001 子图）：

```powershell
# Null
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxDockerGate.ps1 `
  -Gate gcc13-null -OutJson artifacts\gates\test-001-linux-gcc13-null.json

# Platform GLFW/X11 (Xvfb)
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxDockerGate.ps1 `
  -Gate gcc13-platform -OutJson artifacts\gates\test-001-linux-gcc13-platform.json
```

容器内等价命令（`tools/linux/run-gcc13-null-gate.sh`）：

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

Clang sanitizer（本机 Linux）：

```bash
cmake --preset linux-clang22-vnext-sanitize
cmake --build --preset linux-clang22-vnext-sanitize-debug \
  --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests tina_sample_null
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
LSAN_OPTIONS=exitcode=23 ./out/build/linux-clang22-vnext-sanitize/bin/tina_tests --gtest_color=no
```

其余 sanitizer executable 应使用相同环境变量逐个运行；不要把一次 configure/build 成功当成测试通过。

## Android 交叉编译（compile-only；无 preset、无平台后端）

**能编译不等于能运行。** 16 个引擎模块（含 bgfx render backend）可为 Android 交叉编译成静态库，但
**没有任何 Android 平台后端**（`src/platform/` 只有 `glfw` 与 `headless`），所以没有窗口、没有输入、
没有可启动的产物，也没有真机验证。这条路径的唯一用途是守住可移植性：MSVC 会接受若干 Clang 拒绝的写法，
只有真的为 Android 编译才会暴露。

刻意**不加 CMakePresets 条目**：preset 会暗示存在一个可交付的 Android 产品图，而目前没有；NDK 路径也
因机器而异。

```bash
export ANDROID_NDK_HOME=/path/to/Android/Sdk/ndk/28.2.13676358
cmake -S . -B out/build/tmp-android-arm64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DVCPKG_TARGET_TRIPLET=arm64-android -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DTINA_BUILD_TESTING=OFF -DTINA_BUILD_BENCHMARKS=OFF \
  -DTINA_BUILD_PLATFORM_GLFW=OFF -DTINA_BUILD_SHADERS=OFF \
  -DTINA_BUILD_PHYSICS2D=OFF -DTINA_BUILD_AUDIO_MINIAUDIO=OFF \
  -DTINA_BUILD_UI_FREETYPE=OFF -DTINA_BUILD_UI_UIA=OFF \
  -DTINA_BUILD_RENDER_BGFX=ON -DTINA_RENDER_BGFX_MOBILE_SHADERS=ON \
  -DTINA_BGFX_SHADERC_EXECUTABLE="$PWD/out/build/windows-msvc-vnext-bgfx-product-2d/bin/Debug/shaderc.exe"
cmake --build out/build/tmp-android-arm64 --target tina_core tina_task tina_platform tina_render \
  tina_scene tina_asset_format tina_asset tina_ui tina_audio tina_runtime tina_network \
  tina_navigation2d tina_editor tina_render_bgfx
```

要点：

- **`ANDROID_NDK_HOME` 必须导出**，即使已经传了 `VCPKG_CHAINLOAD_TOOLCHAIN_FILE`：vcpkg 用它**自己的**
  `scripts/toolchains/android.cmake` 做 compiler-hash 探测，那个文件只认这个环境变量，找不到就报
  `Could not find android ndk. Searched at C:\Program Files (x86)/Android/android-sdk/ndk-bundle`。
  `ANDROID_HOME` 不够。
- `TINA_BUILD_PLATFORM_GLFW=OFF` 是必需的（GLFW 不支持 Android），`desktop`/`editor_app` 随之不参与
  构建 —— 它们要求 GLFW + bgfx 同时开启，因此 `editor_app` 的 `std::jthread` 从不进入 Android 编译。
- **NDK 28 与 NDK 29 都已实测通过**（arm64-v8a 与 x86_64 各 15 个静态库、零 error）。28 的 libc++ 缺
  浮点 `from_chars` 与 `stop_token`，引擎用 `Core::parseStrictFloat` 与 `Core::CancellationToken` 绕开
  （理由见 [Core](core.md)）；不要改用 `_LIBCPP_ENABLE_EXPERIMENTAL`，它会一次打开全部未完成 libc++ 特性。
- **`TINA_BUILD_RENDER_BGFX=ON` 需要额外给一个宿主 shaderc**，见下节；不给会在 configure 阶段报错。
- 该 build tree 属临时资源，取得结论后按 `AGENTS.md` 回收；常驻树不受影响。

### Android APK（gradle，可安装可运行）

`android/` 下是一个 AGP 工程，产出**真正能装到设备上的 APK**。带渲染器时它跑**完整的 `EngineHost`**
（每帧 `tick()`）并画出一棵真实的 Retained UI 树 —— 一个橙色面板 RGB(220,90,40)，内含每约 1 秒在
RGB(60,190,120) 与橙色之间切换的子面板；**按住面板会让子面板换色并放大**，这条是整个触摸链的端到端证据。
另有一个**自动聚焦的 TextEdit**：没有它，IME 那半条链完全不可观测（`routeTextComposition` 走「无焦点」
分支返回未消费，而平台的 stage 计数照样递增），而且 caret placement 路径根本不会被走到 —— Runtime 只在
TextEdit 聚焦时 publish caret。用来证明平台桥（窗口、触摸、按键、文本、组词、生命周期、软键盘）、
surface 重建、引擎相位与 UI 渲染在真机上可用。

Android 上 FreeType 是关闭的，所以字形退化为实心块 —— 但这**反而**让 preedit 更好判读：组词文本用
(0,180,255) 专色，提交文本用普通文本色，像素上比真字形更容易区分。

**在设备上验证的三个陷阱**（都实测踩过）：

- **`adb shell input swipe ... &` 会杀掉被测 app。** 宿主侧的 job control 在 adb 会话结束时终止进程，logcat
  显示 `exited cleanly (3)` 与 `Remote process closed the socket` —— 看起来像引擎崩溃。要把 `&` 放进设备端
  shell：`adb shell 'input swipe ... & sleep 1; screencap -p /data/local/tmp/x.png'`。
- **不要用 `am start -n` 之外的方式反复启动来测「回到前台」。** activity 现在是 `singleTask`，但在它之前
  `LAUNCH_MULTIPLE` 会造出第二个 engine 实例并让 bgfx 二次初始化失败。判断依据是 `onCreate` 日志出现两次而
  中间没有 `onDestroy`。
- **构建退出码要单独确认。** 把 gradle 串在长命令里会让 manifest 解析失败之类的错误被后续命令的退出码掩盖，
  于是拿旧 APK 验证并误以为已修好。

**判断引擎是否真在跑要看 logcat，不要看画面** —— 运行中与停住的画面都是一片纯色：

```bash
adb logcat -s Tina
# I Tina: frameUpdates=300 ... keys=1 textCommits=1 composition=1/1/1/0(start/update/end/cancel)
#         preeditDrawn=false editCodepoints=2 droppedTouches=0 droppedKeys=0
```

- `fixedUpdates` 与 `frameUpdates` **不同步是正确的**：固定步长累加器走独立时钟。
- `uiUpdates` 应**等于** `frameUpdates`；若前者停滞而后者上升，说明 UI 相位被跳过。
- `pulseOn` 与截图像素必须对应：`false` → RGB(60,190,120)，`true` → RGB(220,90,40)。这条是判断「画面真的在
  更新」而非「恰好停在某一帧」的唯一可靠方法。
- 任何 `rejected` 或 `stopped producing frames` 都是缺陷。
- **进度行突然停止但 `Tina` tag 里什么都没有，先看 `adb logcat -s AndroidRuntime:E`。** 帧循环跑在一个
  Handler Runnable 上，任何 Java 异常都会静默杀掉它 —— 现场与原生挂死完全一样。实测踩过：
  `CursorAnchorInfo.Builder.build()` 在设了位置却没设 matrix 时抛异常，而这条路径只有真实输入法索取
  cursor updates 时才走到，比任何脚本化诊断都晚。
- **`keys=` 与聚焦状态相关，不是纯粹的桥接证据。** 聚焦的 TextEdit 会消费**除 Tab / Enter / Escape 以外的
  每一个键**（正确行为），所以用方向键当证据时，一旦有文本框获得焦点计数就会归零并停在那里 —— 与「按键桥
  坏了」现场一致。demo 因此把 Enter 也绑进同一个 action。

`android/local.properties` **不提交**（含机器路径），需要自己创建两行：

```properties
sdk.dir=D:\\Programs\\Android\\Sdk
cmake.dir=D:\\Programs\\CMake
```

**`android/` 下没有 `gradlew`/`gradlew.bat`。** 上面写 `./gradlew` 是习惯写法，但 wrapper 需要一个
`gradle-wrapper.jar`，而本仓库不提交任何二进制。仓库里只有 `gradle/wrapper/gradle-wrapper.properties`
（钉住 Gradle 8.13）。两条可用路径：装一个 Gradle 8.13+ 直接用，或用它生成 wrapper
（`gradle wrapper` 会产出被 gitignore 的脚本与 jar）。

**`ANDROID_NDK_HOME` 在 gradle 构建里同样必须导出。** AGP 会把 NDK 路径传给 CMake，所以看起来不需要 ——
但 vcpkg 用它**自己的** `scripts/toolchains/android.cmake` 做 compiler-hash 探测，那个文件只认这个环境
变量。不导出时 AGP 只报「cmake.exe finished with non-zero exit value 1」，**真正的 vcpkg 错误不出现在
gradle 输出里**；把同一条 cmake 命令单独跑一遍才能看到。这与上一节交叉编译的坑是同一条。

然后：

```bash
export JAVA_HOME=/path/to/jdk-17
export VCPKG_ROOT=/path/to/vcpkg
cd android

# 不带渲染器：APK 可装可跑，画面空白，只验证平台桥。
./gradlew :app:assembleDebug

# 带渲染器：需要一个**宿主** shaderc（构建期工具，交叉构建产不出可用的），
# 任一桌面 bgfx 构建树里都有现成的。
./gradlew :app:assembleDebug \
  -Ptina.shaderc=/abs/path/out/build/windows-msvc-vnext-bgfx-product-2d/bin/Debug/shaderc.exe

adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n dev.tina/dev.tina.TinaActivity

# 两条 IME 诊断入口，因为非 ASCII 与组词过程都无法从测试工具注入（adb 按 keycode 合成，
# 既表达不了代理对、也不携带组词区）。两者都走与真实键盘同一条 InputConnection 路径。
adb shell am start -n dev.tina/.TinaActivity --ez tina.commitEmoji true
adb shell am start -n dev.tina/.TinaActivity --ez tina.composeText true

# 可浏览的示例 gallery（菜单选场景，返回键回菜单）。默认跑的仍是遥测 demo。
adb shell am start -n dev.tina/.TinaActivity --ez tina.gallery true
```

**Release 构建：** `./gradlew :app:assembleRelease`。之前只有 `debuggable` 的 debug 变体，也就是此前所有
性能观察都来自 `-O0`、断言开启的构建 —— 那种数字说明不了发布行为。`minifyEnabled` 保持 false：Java 侧只有
四个小类没什么可缩，而 R8 会重命名 `RegisterNatives` 在加载期按名字解析的那些方法。native 符号保留
（`debugSymbolLevel FULL`），因为 strip 过的 `.so` 会把设备崩溃变成一串十六进制地址，而这个移植产生的缺陷
全靠有名字的栈帧才定位得到。

**四项性能相关的实现选择：**

- **帧循环走 `Choreographer.postFrameCallback`，不是 `Handler.post`。** 后者会无延迟地重发自己 —— 那不是
  帧循环：它按 UI 线程的派发速度狂转、产出显示器永远不会呈现的帧，还饿着它自己依赖来送触摸的那个线程。
  Choreographer 是显示器自己的 vsync 信号，所以引擎跟着面板刷新率跑，可变刷新率也顺带拿到。
- **IME 几何按 6 帧采样，且遮挡值只在变化时跨 JNI。** 两者在无焦点时纯属开销：遮挡测量要走视图层级取
  visible frame，光标上报要跨 JNI 加一次 `CursorAnchorInfo` 分配。键盘弹出和光标移动都是人的速度，60Hz 采样
  它们是白付。
- **task system 换成 bounded，不再是 disabled。** `createDisabledTaskSystem` 让每个 `scheduleCpu` 就地在
  调用线程跑，于是资产解码和一切并行工作都落在帧线程上 —— 而 Android 上那**同时是 UI 线程**，所以一次纹理
  加载会同时卡住渲染与触摸派发。worker 数走 `interactiveCpuWorkerCount`（ADR 0017，给主线程留一个硬件
  线程），这在手机上比桌面更要紧：主线程与平台自己的 UI 工作共享，超订的代价是输入延迟而非只是吞吐。
- **字体从 `/system/fonts` 读，不打进 APK。** 设备上一定有拉丁无衬线字体，打包等于为每个构建加一兆去复制
  一个已经在那儿的文件。路径由 **Java 选**：`/system/fonts` 位置稳定但内容不稳定（模拟器是 DroidSans、多数
  现代设备是 Roboto、厂商还有自己的名字），在 C++ 里写死文件名会在碰巧匹配的设备上出字、在其余设备上静默
  退化成实心块。传路径而非字节，是因为引擎自己有 memory-resource 感知的 `readFile`，不必把一兆字体经 JNI
  数组复制一遍。

**gallery 是 opt-in 而非默认，这是刻意的取舍。** 遥测 demo 承载全部设备证据 —— 十一个 JNI 计数器都从它
读 —— 换成 gallery 等于拿已证明的换好看的。两者是同一个 `EngineHost` 上的两个 `IGameApplication`，
一行选择。gallery 模式下**所有那些计数器读数为零**（它们是 demo 自己的状态），所以进度日志会换成一行
`gallery frame=N`：照原样打印会是一屏零，读起来和输入桥坏了一模一样。

**模拟器上必须走 GLES。** SDK 模拟器的 Vulkan 实现（`vulkan.ranchu.so`）会在 swapchain 创建时 SIGSEGV，
所以 `TinaActivity` 按 `Build.HARDWARE` 含 `ranchu`/`goldfish` 自动改用 GLES；真机保持默认（Android 上偏好
Vulkan）。**因此模拟器验证不覆盖 Vulkan 路径。**

验证画面是否真的在渲染，不要只看「没崩」：截图后统计主色，bgfx 清屏色是 RGB(16,42,67)，而 Android 默认背景
是黑或白 —— 二者一眼可分，而「黑屏」恰好是 submit 被拒时的症状。

四个非显然的坑，都是实测踩出来的：

- **`cmake.dir` 是必需的。** SDK 自带 CMake 3.22.1，而引擎要求 3.25+，否则 configure 直接失败。
- **AGP 的 `path` 指向 `android/app/CMakeLists.txt` 这个 wrapper，不是仓库根。** 指向根会让 AGP 读到同目录的
  `CMakeSettings.json`（Visual Studio 的文件，带 UTF-8 BOM），并按自己的 JSON schema 解析，报
  `Expected BEGIN_OBJECT but was STRING`。那个文件是合法且共享的，所以改的是这边。
- **vcpkg 必须反过来 chainload NDK toolchain。** AGP 传的 `CMAKE_TOOLCHAIN_FILE` 直指 NDK；wrapper 把它移到
  `VCPKG_CHAINLOAD_TOOLCHAIN_FILE` 并把 vcpkg 放前面，否则 `find_package(xxHash)` 失败。
- **`VCPKG_MANIFEST_DIR` 必须显式给。** vcpkg 只在 `CMAKE_SOURCE_DIR` 找 `vcpkg.json`，而 wrapper 让那里变成
  `android/app`；不指定它会**静默跳过 manifest 模式**，然后在第一个 `find_package` 处报一个完全不提 vcpkg 的
  缺失配置错误。triplet 按 `ANDROID_ABI` 选（AGP 每个 ABI 单独 configure，写死一个会让另一个 ABI 拿到错误
  架构的依赖），未验证的 ABI 直接 FATAL 而不猜。

### 交叉编译 bgfx：必须外部提供宿主 shaderc

**shader 不在设备上编译。** `shaderc` 是构建期**宿主**工具，把 `.sc` 源码烤成 `*.bin.h` 头文件
`#include` 进库（见 `src/render/bgfx/*Shader.cpp` 顶部）；设备运行时只是从数组里取现成字节。所以
Android 侧从不需要 shaderc —— 需要的是一个能在**你的机器**上跑的 shaderc。

bgfx.cmake 上游用朴素的 `add_executable(shaderc ...)` 声明它、无任何交叉编译处理，于是交叉构建会把这个
宿主工具也编成目标架构：实测产物是 460 MB 的 AArch64 ELF（`llvm-readelf -h` 报 `Machine: AArch64`），
宿主无法执行。因此交叉构建时**必须**指定 `TINA_BGFX_SHADERC_EXECUTABLE`：

```bash
# 复用任一桌面构建树里已有的 shaderc.exe（PE 格式，约 43 MB）
-DTINA_BUILD_RENDER_BGFX=ON \
-DTINA_RENDER_BGFX_MOBILE_SHADERS=ON \
-DTINA_BGFX_SHADERC_EXECUTABLE="$PWD/out/build/windows-msvc-vnext-bgfx-product-2d/bin/Debug/shaderc.exe"
```

指定后 in-tree `shaderc` **不再构建**（`thirdparty/CMakeLists.txt` 把 `BGFX_BUILD_TOOLS` 关掉），省下那
460 MB 无用产物和相应编译时间。三条错误路径都在 configure 阶段就给出可操作诊断：交叉但未提供、提供的
路径不存在、`bgfx::shaderc` 目标缺失。

**bgfx 本身从来不是障碍**（2026-08-29 实测）：`bx`/`bimg`/`bgfx` 与 `tina_render_bgfx` 在 arm64-v8a 与
x86_64 上均**零 error**；编进去的是真实实现而非空壳 —— `renderer_vk.cpp.o` 1341 个符号、
`renderer_gl.cpp.o` 696 个，而 `renderer_d3d11.cpp.o` 只剩 87 个，`glcontext_egl.cpp.o` 在列，与
「Android 偏好 Vulkan」的既有决定一致。产物含 11 个 `essl` 符号、0 个 `dxbc`。

**不要**用「关掉 shader cook」绕过缺失的 shaderc：embedded header 正是 RendererType 表引用的对象，跳过
只会把清晰的构建错误换成难查的链接错误。

## 常用选项

| 选项 | 默认 | 作用 |
| --- | --- | --- |
| `TINA_BUILD_EXAMPLES` | 顶层工程 ON | samples 与 `tina_assetc`/`tina_catalog_validate` |
| `TINA_BUILD_TESTING` | ON | GoogleTest targets |
| `TINA_BUILD_SHADERS` | ON | build-tree shaderc/cooked shader；Null 图可 OFF |
| `TINA_BUILD_LEGACY` | OFF，强制 | 已退役；ON 直接 FATAL |
| `TINA_BUILD_RENDER_BGFX` | OFF | 私有 bgfx backend |
| `TINA_BGFX_SHADERC_EXECUTABLE` | 空 | 宿主 shaderc 路径；**仅交叉编译需要**，给定后不再构建 in-tree shaderc |
| `TINA_BUILD_PLATFORM_GLFW` | OFF | 私有 GLFW adapter |
| `TINA_BUILD_UI_FREETYPE` | OFF | 私有 FreeType rasterizer |
| `TINA_BUILD_AUDIO_MINIAUDIO` | OFF | 私有 miniaudio adapter |
| `TINA_BUILD_PHYSICS2D` | OFF | 可选 Box2D 模块 |
| `TINA_ENABLE_SANITIZERS` | OFF | Unix GCC/Clang ASan/UBSan |
| `TINA_BUILD_BENCHMARKS` | OFF | 打开时构建 `tina_bench`；examples 开启时也会构建 |
| `TINA_TRACE_BACKEND` | `none` | 编译期 Trace backend；支持 `none` / `tracy`，后者要求 `profile-tracy` manifest feature |
| `TINA_UI_FONT_PATH` | 空 | 可选字体文件 |
