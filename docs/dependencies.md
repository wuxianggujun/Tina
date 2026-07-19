# 第三方依赖与版本治理

> 状态：vNext 候选设计。`vcpkg-configuration.json` 的 baseline 和 Git submodule commit 是
> 版本事实；本文定义目标可见性与升级流程。

## 原则

- 依赖必须解决 Tina 当前不应自研的问题，并隐藏在最窄 adapter 后；
- 所有已安装/Game SDK/Tina module public header 都不暴露第三方类型、宏或传递 include；第三方
  即使作为模块内部存储也不是公开类型例外；
- Configure/Build 不自动更新 submodule，`TINA_AUTOUPDATE_SUBMODULE` 日常保持 OFF；
- Runtime 不下载源码、编译 shader 或解析源资产；所有发布资产来自可重复 Cooker；
- 每次升级单独提交，包含版本/许可证变化、Windows/Linux 构建、直接 GoogleTest、对应 smoke
  和性能 A/B；
- 不保留“也许以后会切换”的第二后端，新增或替换依赖必须写 ADR。

## vNext 依赖矩阵

| 依赖 | 目标用途 | 接入方式 | 可见范围 | 状态 |
| --- | --- | --- | --- | --- |
| GLFW | Windows/Linux Window、键鼠、标准 Gamepad | vcpkg，`tina_platform_glfw` | 只在 platform backend | 保留；不使用 SDL/SDL3 |
| bgfx/bx/bimg/shaderc | 唯一真实 Render backend 与离线 shader | 固定 submodule commit | 只在 render_bgfx/tool | 保留 |
| EnTT | 后续 Scene component storage | vcpkg | 仅允许在未来 `tina_scene` 实现层 PRIVATE 使用 | 保留；M8-A standalone World 当前不接入 |
| FreeType | 字形 raster；Glyph Atlas 编排仍属于 Tina UI | vcpkg | 只在 `tina_ui_freetype` adapter | 保留 |
| miniaudio | 唯一真实 Audio backend | vcpkg | 只在 `tina_audio_miniaudio` adapter | 保留；不使用 SDL_mixer |
| xxHash | ContentHash、Cook cache、可选 StringId | vcpkg root dependency + private adapter | `tina_core` PRIVATE 链接；公共头零 token | 保留，不承担安全签名；M10-A2a 契约 |
| [Tracy 0.13.1](https://github.com/wolfpld/tracy) | 开发 Profile capture | vcpkg optional feature | `tina_profile_tracy` | 可选；发布和正式 bench 禁用 |
| [cgltf v1.15](https://github.com/jkuhlmann/cgltf) | `tina_assetc` 解析 glTF | 固定单文件 + LICENSE/精确提交 | Cooker 源格式 adapter | 待接入 |
| [GoogleTest 1.17.0](https://github.com/google/googletest/releases) | 单元/集成契约测试 | vcpkg `tests` feature | tests only | 固定；直接运行，不用 CTest；production manifest graph 不启用 |
| Box2D 3.x | 唯一 2D Physics backend | vcpkg | `tina_physics2d` PRIVATE | 当前 Legacy 已依赖；vNext 产品接入 M11 |
| Jolt | 唯一 3D Physics backend | ADR 0010 + 接入时固定版本 | `tina_physics3d` | 未接入，真实3D玩法后置 |
| EASTL/EABase | Legacy 容器 | 当前 submodule | Legacy only | vNext 禁止，零引用后删除 |

`glm/spdlog/utfcpp` 当前仍由 Legacy/现有实现使用。vNext 的数学/UTF-8 API 不暴露它们；spdlog
若用于首个 Diagnostics sink，也只能作为私有 adapter，不能决定 LogRecord/Error 公共类型。
每项只有出现明确调用点和门禁后才决定继续保留或迁走，不能因为 manifest 已存在就让它们
进入新的公共接口。

## 获取与锁定

- vcpkg 依赖由仓库中的 builtin baseline 解析，结果的 baseline 和包版本写入 bench/build
  fingerprint；
- bgfx.cmake/bx/bimg 等源码依赖锁定 Git commit，构建期间不 fetch/pull；
- cgltf 作为单文件 Cooker 依赖时同时保存上游 URL、tag/commit、SHA-256 和 LICENSE，不复制
  Carbon 的副本；
- Tracy 通过目标 vcpkg manifest feature `profile-tracy` 按需解析，由 Profile preset 启用；普通
  Debug/Release 和发行包不能因为未安装 Tracy 而 configure 失败；
- 生成包记录 Tina commit、vcpkg baseline、submodule commit、Cooked schema 和 shader ABI。

## CMake 可见性

目标选项为：

```text
TINA_PROFILE_BACKEND=none|tracy
TINA_PROFILE_TRACY_LOCKS=OFF|ON
TINA_PROFILE_TRACY_MEMORY=OFF|ON
TINA_BUILD_LEGACY=ON|OFF    # 迁移期默认 ON，覆盖门禁后翻为 OFF
TINA_BUILD_TESTING=ON|OFF
TINA_BUILD_BENCHMARKS=ON|OFF
TINA_BUILD_SHADERS=ON|OFF
```

- `none` 不查找/链接 Tracy；`tracy` 才允许 `find_package(Tracy CONFIG REQUIRED)`；
- `tina_profile_config` 是唯一传播 Tina backend selection 的 INTERFACE target；Tracy 自身
  definitions 只在 adapter target；
- `tina_profile_tracy` 唯一包含 Tracy inline header并链接 Client，不能让多个静态库各自带一份
  client；业务 target 只看到 Tina trace token 声明；
- cgltf implementation 宏只在 `tina_assetc` 的一个 `.cpp` 定义；
- 第三方 include 默认 `PRIVATE/SYSTEM`，Tina public header 的 include-what-you-use 测试不得依赖
  传递 include 偶然成功。
- vNext target 不再把整个 `${PROJECT_SOURCE_DIR}/src` 作为 PUBLIC include root；公开 header 使用
  target-scoped `include/tina/...`，实现目录 PRIVATE；
- `tina_render_bgfx` 对 bgfx/bx/bimg 的依赖只能是 PRIVATE/SYSTEM；Scene/UI/Asset/Runtime 的
  direct link/public interface/include/definition 出现它们时 configure 直接失败。Game/Sample 只
  直接链接 `Tina::GameSDK`、`Tina::DesktopBootstrap` 和按需的公开扩展模块（首个为
  `Tina::Physics2D`）；最终生产链接经 bootstrap 带入 backend object/library 是实现闭包，不向
  游戏源码传播 header、宏或 target 选择；
- 源码 policy gate 除 render_bgfx、离线 shader tool 和 adapter test 外禁止 `<bgfx/...>`、
  `bgfx::`、`BGFX_`、`bx::`、`bimg::`；
- vNext Null preset 完全不 add_subdirectory/link/load bgfx；安装到 staging 后由无第三方 include
  path 的外部 Game SDK consumer 再编译一次；`Tina::Physics2D` 的安装 consumer 必须在没有
  Box2D include path、宏或 direct link 的环境独立编译，证明 Box2D 始终为 PRIVATE 实现依赖。

## 许可证与发布

依赖接入提交必须确认 license 与商用分发兼容，并更新发布包的 Third-Party Notices。单文件
依赖也必须保留原始版权/许可证，不因为只复制一个 header 就省略。开发工具依赖（Tracy
viewer、GoogleTest、shaderc）默认不进入游戏发布包；Runtime 依赖只打包目标平台真正需要的
动态库、资源和 notice。

## 升级门禁

1. 在独立分支/worktree 更新一个依赖及锁定信息；
2. 检查 public header 和生成包没有新增第三方类型泄漏；
3. Visual Studio 2026 / MSVC 19.50 Debug/Release、Linux GCC/Clang 构建并直接运行 GoogleTest；
4. 运行受影响的2D/UI/3D/Audio/Cooker smoke；
5. 同 fingerprint 运行 `tina_bench` A/B，记录性能、内存和生成产物大小；
6. 检查许可证、CVE/安全公告、Cooked schema/shader ABI 是否需要迁移；
7. 单独提交，失败可回滚，不与功能重构混合。
