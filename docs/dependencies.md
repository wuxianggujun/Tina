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
| UI Automation (system) | Windows UIA 无障碍属性映射 | OS SDK headers（无 vcpkg feature） | `tina_ui_uia` PRIVATE；`TINA_BUILD_UI_UIA` |
| miniaudio | 唯一真实 Audio backend | vcpkg feature `audio-miniaudio` | `tina_audio_miniaudio` PRIVATE |
| libvorbis | 可选 Ogg Vorbis decode | feature `audio-miniaudio-vorbis` | miniaudio adapter PRIVATE；默认 OFF |
| libopus/opusfile | 可选 Opus decode | feature `audio-miniaudio-opus` | miniaudio adapter PRIVATE；默认 OFF |
| Box2D 3.x | 2D Physics backend | vcpkg feature `physics2d` | `tina_physics2d` PRIVATE |
| xxHash | ContentHash/确定性校验 | vcpkg root dependency | `tina_core` PRIVATE adapter；非安全签名 |
| GoogleTest | 单元/集成契约测试 | 默认 vcpkg feature `tests` | tests only；直接运行，不注册 CTest |
| cgltf v1.15 | glTF/GLB parse/validate | vendored `thirdparty/cgltf` | `src/asset/GltfCook.cpp` PRIVATE |
| stb_image v2.30 | glTF image decode 为 RGBA8 Texture2D | vendored `thirdparty/stb` | 同一 Cooker TU PRIVATE |

`CGLTF_IMPLEMENTATION` 与 `STB_IMAGE_IMPLEMENTATION` 只在 `src/asset/GltfCook.cpp` 定义。公开
`GltfCook.hpp` 只出现 Tina-owned 类型；cgltf/stb_image token 不能进入 `include/tina`。

bgfx.cmake、bx、bimg 的源码 revision 由 submodule commit 锁定。cgltf/stb_image 不是 submodule，版本、
来源和许可证分别由 `thirdparty/cgltf/NOTICE.json`、`LICENSE` 与 `thirdparty/stb/NOTICE.txt` 记录。

## Manifest 与 CMake

`vcpkg.json` 当前默认只启用 `tests`，root dependency 为 `xxhash`。可选 feature：

```text
platform-glfw
ui-freetype
physics2d
audio-miniaudio
audio-miniaudio-vorbis
audio-miniaudio-opus
wayland
```

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
TINA_BUILD_LEGACY=OFF only
```

`TINA_BUILD_LEGACY=ON` 必须在 configure 阶段 FATAL。全部 preset 与输出路径见[构建说明](building.md)，
这里不复制 preset 清单。

不存在 `TINA_PROFILE_BACKEND`、`TINA_PROFILE_TRACY_*`、`profile-tracy` feature、
`tina_profile_config` 或 `tina_profile_tracy` target。ADR 0002 保留 Tracy 作为未来定位工具方向，但当前
只有基础 Trace/Metrics 设计和 `PERF-001`；`tina_bench` 也尚未成为正式 target。

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
