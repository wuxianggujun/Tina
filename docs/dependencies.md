# 第三方依赖与版本治理

依赖版本事实来自 `vcpkg-configuration.json` 的 builtin baseline、`vcpkg.json` feature、
`.gitmodules` 与 vendored `NOTICE`。本文件只描述当前实际接入；ADR 中接受但尚未实现的依赖不会写成
可用 CMake option 或 target。

## 原则

- 第三方依赖解决 Tina 当前不应自研的问题，并隐藏在最窄 adapter/Cooker TU；
- Game API、Module API/SPI 和安装候选公开头不暴露第三方类型、宏或传递 include；
- Configure/Build 不自动更新 submodule，`TINA_AUTOUPDATE_SUBMODULE` 日常保持 OFF；
- Runtime 不下载源码、编译 shader 或解析源资产；源格式只在 Cooker/tool；
- 一次升级只改一个依赖，记录版本/许可证变化，并运行受影响的 Windows/Linux build、直接
  GoogleTest、sample smoke 和必要的性能 A/B；
- 未接入的第二 backend 或工具不能只靠文档占位，正式接入前先落实 ADR、target 与测试。

## 当前依赖矩阵

| 依赖 | 用途 | 接入方式 | 可见范围 |
| --- | --- | --- | --- |
| GLFW | Windows/Linux Window、键鼠、标准 Gamepad | vcpkg feature `platform-glfw` | `tina_platform_glfw` PRIVATE |
| bgfx/bx/bimg/shaderc | 唯一真实 Render backend 与离线 shader | `thirdparty/bgfx.cmake` submodule | `tina_render_bgfx`/shader tool PRIVATE |
| FreeType | 字形 raster；Atlas/布局仍由 Tina UI 拥有 | vcpkg feature `ui-freetype` | `tina_ui_freetype` PRIVATE |
| UI Automation (system) | Windows UIA 属性/fragment、Invoke/Toggle/RangeValue/Value control patterns 与 HWND client gate | OS SDK headers（无 vcpkg feature） | `tina_ui_uia` PRIVATE；`TINA_BUILD_UI_UIA` |
| miniaudio | 唯一真实 Audio backend | vcpkg feature `audio-miniaudio` | `tina_audio_miniaudio` PRIVATE |
| libvorbis | 可选 Ogg Vorbis decode | feature `audio-miniaudio-vorbis` | miniaudio adapter PRIVATE；默认 OFF |
| libopus/opusfile | 可选 Opus decode | feature `audio-miniaudio-opus` | miniaudio adapter PRIVATE；默认 OFF |
| Box2D 3.x | 2D Physics backend | vcpkg feature `physics2d` | `tina_physics2d` PRIVATE |
| mbedTLS 3.6+ | 唯一 TLS backend | vcpkg feature `network-tls` | `tina_network_tls` PRIVATE；`TINA_BUILD_NETWORK_TLS` |
| platform sockets | UDP/TCP/readiness/DNS，无第三方 | OS SDK（Winsock2 `ws2_32`、POSIX BSD sockets） | `tina_network` PRIVATE；无 feature，无条件构建 |
| CryptoAPI (system) | Windows 平台信任库读取（`CertOpenSystemStoreW`） | OS SDK（`crypt32`） | `tina_network_tls` PRIVATE |
| xxHash v0.8.3 | ContentHash/确定性校验 | vendored `thirdparty/xxhash` | `tina_core` PRIVATE，`XXH_INLINE_ALL` header-only；非安全签名 |
| MikkTSpace | glTF Cook 的切线空间生成 | vendored `thirdparty/mikktspace` | `tina_asset` PRIVATE（`src/asset/GltfCook.cpp`），`mikktspace.c` 直接编入 |
| Tracy | 可选开发定位 profiler | vcpkg feature `profile-tracy`，要求 `tracy >= 0.13.1` | `tina_trace_tracy` PRIVATE；`TINA_TRACE_BACKEND=tracy` |
| GoogleTest | 单元/集成契约测试 | 默认 vcpkg feature `tests` | tests only；直接运行，不注册 CTest |
| cgltf v1.15 | glTF/GLB parse/validate | vendored `thirdparty/cgltf` | `src/asset/GltfCook.cpp` PRIVATE |
| stb_image v2.30 | glTF image decode 为 RGBA8 Texture2D | vendored `thirdparty/stb` | 同一 Cooker TU PRIVATE |

`CGLTF_IMPLEMENTATION` 与 `STB_IMAGE_IMPLEMENTATION` 只在 `src/asset/GltfCook.cpp` 定义。公开
`GltfCook.hpp` 只出现 Tina-owned 类型；cgltf/stb_image token 不能进入 `include/tina`。

bgfx.cmake、bx、bimg 的源码 revision 由 submodule commit 锁定。cgltf/stb_image/xxHash/MikkTSpace 不是
submodule，版本、来源和许可证分别由 `thirdparty/cgltf/NOTICE.json`、`LICENSE`、`thirdparty/stb/NOTICE.txt`、
`thirdparty/xxhash/NOTICE.json`、`LICENSE` 与 `thirdparty/mikktspace/NOTICE.json` 记录。

xxHash 与 MikkTSpace 是 vendored 而非 vcpkg package，理由和 cgltf/stb_image 相同，但代价更直接：它们以
`PRIVATE` 链接到静态库上，即使 consumer 代码从不提及，也会作为 `$<LINK_ONLY:xxHash::xxhash>` /
`$<LINK_ONLY:mikktspace::mikktspace>` 出现在安装后的 `TinaTargets.cmake` 里。于是 `find_package(Tina)` 会在
`find_dependency` 那一行失败，报的是 **xxHash 找不到**——看起来是 Tina 装坏了，其实是 Tina 的私有依赖没被打包。
vendored 之后安装后的 SDK 加载核心模块集不需要 consumer 提供任何 package。xxHash 走 `XXH_INLINE_ALL`
header-only，摘要已实测与链接 0.8.3 库逐字节一致，所以已 cook 的 ContentHash 不受影响。

**vendored 与 submodule 源码一律位于 `thirdparty/`。** 仓库根还有一个 `dependencies/` 目录，但它当前
**为空且不被 Git 跟踪**（`git ls-files dependencies/` 无输出），不承载任何依赖；它只作为搜索排除项出现在
`AGENTS.md`。不要往那里找依赖，也不要据它推断存在第二套依赖树。

## Manifest 与 CMake

`vcpkg.json` 的 `default-features` 只有 `tests`；**没有** `dependencies`（无条件 root dependency）——
原先的 `mikktspace` 与 `xxhash` 已 vendored 到 `thirdparty/`。除默认 `tests` 外的可选 feature 共 9 个：

```text
profile-tracy
physics2d
network-tls
audio-miniaudio
audio-miniaudio-vorbis
audio-miniaudio-opus
platform-glfw
ui-freetype
wayland
```

`features` 段合计 10 个（上列 9 个 + 默认 `tests`）。带下限约束的只有两个：`profile-tracy` 的
`tracy >= 0.13.1`、`network-tls` 的 `mbedtls >= 3.6.5`；`wayland` 额外声明 `"supports": "linux"`。

vcpkg `legacy` feature 已删除（CLEAN-001）。EnTT、GLM、spdlog、utfcpp 不得再作为当前 Runtime 依赖声明；
若未来 Scene 使用 EnTT，只能经 ADR 0013 作为 Scene 私有存储并单独 feature 接入。

与第三方接入直接相关的现有 CMake option：

```text
TINA_BUILD_PLATFORM_GLFW=OFF|ON
TINA_BUILD_RENDER_BGFX=OFF|ON
TINA_BUILD_UI_FREETYPE=OFF|ON
TINA_BUILD_UI_UIA=OFF|ON
TINA_BUILD_PHYSICS2D=OFF|ON
TINA_BUILD_AUDIO_MINIAUDIO=OFF|ON
TINA_AUDIO_ENABLE_LIBVORBIS=OFF|ON
TINA_AUDIO_ENABLE_LIBOPUS=OFF|ON
TINA_BUILD_SHADERS=OFF|ON
TINA_BUILD_WAYLAND=OFF|ON
TINA_BUILD_TESTING=OFF|ON
TINA_BUILD_EDITOR=OFF|ON
TINA_BUILD_LEGACY=OFF only
```

`TINA_BUILD_EDITOR` 定义于根 `CMakeLists.txt:57`，默认值是 `PROJECT_IS_TOP_LEVEL`（把 Tina 作为
subdirectory 引入的 consumer 默认 OFF），并在根 `CMakeLists.txt:322` gate 整棵 `editor/` 树。它 OFF 时
`tina_editor`、`tina_editor_desktop`、`tina_editor_tests`、`tina_editor_app_tests` 都不存在。编辑器不随
Game SDK 发布：`cmake/TinaGameSdkPackage.cmake` 已不含 `tina_editor`、`Editor` component 与
`include/tina/editor`（见 ADR 0041）。

`TINA_BUILD_LEGACY=ON` 必须在 configure 阶段 FATAL。全部 preset 与输出路径见[构建说明](building.md)，
这里不复制 preset 清单。

`TINA_TRACE_BACKEND` 是唯一 backend 选择，支持默认 `none` 与开发定位用 `tracy`。Tracy 图要求
`profile-tracy` manifest feature，解析 Tracy 0.13.1，并只在私有 `tina_trace_tracy` / `Tina::TraceTracy`
adapter implementation TU 中包含 Tracy header 和链接 client。Tracy 图的 Core/Game SDK 公共面只传播
backend-neutral `TINA_TRACE_BACKEND_ENABLED=1`，不暴露 Tracy token 或类型。None 下
`TINA_TRACE_ZONE(nameLiteral)` 不求值参数、
不构造对象、不调用函数、不分配内存且不使用全局状态。

当前没有 Tina-owned Tracy session/capture control API；capture 由 Tracy 工具连接开发构建完成。Metrics
仍只有设计。`tina_bench` schema v1 已存在，target 在
`TINA_BUILD_BENCHMARKS=ON` 或 examples 图中存在；固定机 hard gate 与多进程 MAD 由
`PERF-002` 跟踪，不得把共享机 provisional 结果写成发布门禁。

Jolt/`tina_physics3d` 同样尚未接入。它们分别由 `PHYSICS-001` 与后续设计负责，不能出现在当前 build
命令或发布依赖中。

## 可见性门禁

- GLFW/native window 类型只在 Platform adapter；WindowSurface SPI 只传播 Tina-owned opaque binding；
- bgfx/bx/bimg/shaderc 只在 Render backend/tool；Scene、Asset、UI、Runtime 不直接 include/link；
- FreeType 与 miniaudio 只在各自 adapter；Box2D 只在 Physics2D 实现；
- cgltf/stb_image 只处理离线/启动前 Cooker 输入；Runtime frame path 不解析 source glTF/image；
- xxHash header/type 不进入 Core public header，公共 API 只暴露版本化 `ContentHash`；
- Null preset 不解析 GLFW、FreeType、miniaudio、Box2D feature，也不 add bgfx backend；
- optional adapter 的 public factory/header isolation 必须在第三方 include path 不可见时独立编译；
- `rg` forbidden-token 命中后人工判断，不能用“第三方 target 是 PRIVATE”替代公开头隔离测试。

## 当前残留

CLEAN-001～003 已关闭：vcpkg `legacy` feature、无消费者 EASTL `StringUtils`、Clock/FrameTimer
compatibility 与 miniaudio/Legacy 误导文案已删除或改写。`TINA_BUILD_LEGACY=ON` 仍 FATAL。

后续若出现新的死依赖/兼容层，按「先证明消费者 → 替代接口 → 独立可回滚删除」处理，不恢复
EnTT/GLM/spdlog/utfcpp/EASTL 产品依赖。

## 许可证与升级

1. 确认 upstream version/commit、license、CVE 与商用分发约束；
2. 更新 manifest baseline、submodule commit 或 vendored NOTICE，不在 configure 时在线漂移；
3. 检查 public header、generated package 和 compile definitions 无第三方泄漏；
4. 运行受影响的 Windows MSVC 与 Linux GCC/Clang 直接测试；
5. 运行对应 2D/UI/3D/Audio/Cooker smoke，并记录退出、资源归零与视觉/音频边界；
6. 若 Cooked schema、shader ABI 或 deterministic output 变化，提供迁移/失效策略；
7. 更新 Third-Party Notices，工具依赖不进入游戏发布包；
8. 单独提交，保证可回滚，不夹带功能重构。

Backlog 与当前验收见[可执行 Backlog](backlog.md)和[测试说明](testing.md)。
