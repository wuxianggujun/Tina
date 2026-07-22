---
name: navigate-tina-architecture
description: 梳理 Tina C++23 游戏引擎的模块依赖、公开 API、ADR 与退役扫尾。Use when planning cross-module changes, CMake targets, public headers, migration status, or verifying removal of residual docs/deps after Legacy product deletion.
---

# Tina 架构导航

## 建立当前事实

先在仓库根目录执行：

```powershell
git status --short
cmake --list-presets
```

保留用户未提交修改。按顺序阅读：

1. `README.md`、`README_CN.md`、`docs/README.md`、`docs/architecture.md`、`docs/design.md`。
2. `docs/design-freeze.md` 与 `docs/adr/README.md`。
3. 任务对应主题文档、公共头、模块 `CMakeLists.txt` 和测试。
4. 必要时用 `git log --oneline -- <paths>` 与 `git diff -- <paths>` 判断文档和工作树状态。

用源码、target、测试回答“已经实现什么”；用设计冻结清单和 ADR 回答“允许实现什么”。状态词不要
混用：design-freeze 使用 Accepted/Proposed/Deferred；ADR lifecycle 只使用
Proposed/Accepted/Superseded/Rejected；Roadmap 只描述顺序与目标。实际测试/运行结果优先于完成度描述。

## 识别修改轨道

| 轨道 | 当前职责 | 入口 |
| --- | --- | --- |
| Core/Runtime | 生命周期、Platform/Input、Task、Render、Desktop | `include/tina/{core,platform,integration,task,render,runtime,desktop}/` + `src/<module>` |
| UI | Retained tree、layout、route ABI | `include/tina/ui/`、`src/ui/` |
| Scene/Asset/Audio/Physics | 世界与资源 | `src/scene`、`src/asset*`、`src/audio`、`src/physics2d` |
| Backend private | GLFW、bgfx | `src/platform/glfw/`、`src/render/bgfx/` |

**Legacy 产品图已删除**（无 `Tina.exe`/CoreLegacy/EASTL）。不要因文档存在类型就宣称 StateStack、
Pass Scheduler 或完整产品 SDK 已完成；以 CMake target 与测试为准。
## 检查目标依赖

当前主依赖图为：

```text
Core
├─ Platform
├─ Task
├─ Render
├─ UI (also Platform)
└─ WindowSurfaceIntegration (also Platform)
PlatformGlfw -> Platform + WindowSurfaceIntegration; private GLFW
RenderBgfx -> Render; private WindowSurfaceIntegration + bgfx
Runtime -> Platform + Task + Render + WindowSurfaceIntegration; private UI seam
DesktopBootstrap -> Runtime; private PlatformGlfw + RenderBgfx + Task
```

每次改 target 时核对：

- PUBLIC 依赖是否只表达调用者真正需要的 Tina 类型。
- GLFW、bgfx、native binding、EnTT、FreeType、miniaudio、EASTL 是否仍在最窄 PRIVATE adapter。
- 公开头是否只依赖 `include/tina/...`，没有依靠传递 include 或整个 `src` include root。
- Null preset 是否仍能排除 Legacy、GLFW、bgfx、shader 和无关 vcpkg feature。
- 是否需要新增 header-isolation 翻译单元和 target-specific 测试。
- optional adapter 的 public factory header 是否只暴露 Tina public API/SPI，并能在 adapter option
  关闭的基础 `tina_tests` 图中独立编译；当前 GLFW factory 可以公开依赖 Platform 与
  WindowSurfaceIntegration，但 GLFW/native 头、opaque payload 和第三方链接必须留在 PRIVATE adapter。
- vNext-only 图是否显式关闭默认 vcpkg feature，且 `platform-glfw` 等 feature 未被默认 `legacy`
  feature 间接提供的依赖掩盖；Null/vNext-only 不得扫描或复制 Legacy source resources。

快速扫描公开面：

```powershell
rg -n "bgfx::|BGFX_|GLFW|GLFWwindow|entt::|EASTL|HWND|wl_|xcb_|X11" include\tina
rg -n "target_link_libraries|target_include_directories|add_library|add_executable" `
  --glob CMakeLists.txt -g "!thirdparty/**" -g "!dependencies/**" -g "!out/**" -g "!build/**" .
```

命中并不自动等于违规；`include/tina/integration/WindowSurface.hpp` 是内部 Module SPI，但仍不得暴露
真实 native 类型或任意 `void*` escape hatch。

## 规划跨模块改动

1. 写清当前实现、目标状态和此次最小垂直切片。
2. 标出唯一 owner、借用视图寿命、提交点、失败回滚和 shutdown 顺序。
3. 优先复用 `Core::Result/Status`、`GenerationPool`、`FrameArena`、`MemoryTracker`、
   `PlatformFrameBuilder`、`PlatformEventDispatcher`、`ActionMapper` 和现有 factories。
4. 先建立新边界和测试，再迁移调用点；禁止长期双向桥接或一次性大爆炸替换。临时 bridge 仅在存在
   真实消费者时保留，并记录 owner、窄边界、replacement test 与明确 removal condition。
5. 新模块同时添加公开头、实现 target、`Tina::<Module>` alias、测试和对应文档状态。
6. 若实现依赖 Proposed 决策，先让用户确认并更新 ADR/设计冻结状态；不要让代码偷偷冻结契约。
7. 若反转 Accepted 决策，新建替代 ADR，并把旧 ADR 标为 Superseded；不要改写历史理由。

## 保护关键契约

- 保持 `EngineHost` 为唯一非全局组合根；不新增 Singleton、Service Locator 或全局 backend owner。
- 保持 `IGameApplication` lifecycle-only，逐帧入口只在 `IGameState`。
- 保持 factory tagged composition；不要动态拼接普通 Platform factory 与 native-surface Render factory。
- 保持 Game API、Module SPI、Backend Private 三层，普通游戏使用 `Desktop::CreateEngine`。
- 为每个借用视图记录精确失效点：phase/frame view 通常只在 callback/phase/poll 内有效；UI committed
  view 则分别在下一次对应 commit 或 Context 析构时失效，不能用一条“跨 frame 禁止”规则混称。
- 使用强类型 generation + owner ID；不要手工构造、跨 registry 混用或持久化 runtime handle。
- 将 UI route result 保留在 `Tina::UI`，Runtime 只以 PRIVATE 依赖消费 seam；不要恢复旧
  `runtime/spi/InputRouting.hpp`。
- 不把 clear-only Desktop sample 描述成完整渲染器，也不把 DisabledTaskSystem 描述成 worker pool。

## 退役旧设计与 Legacy

先区分三类对象：

1. 仍承载当前产品的 Legacy 实现：日常修复或单个迁移切片不能顺手删除。
2. 已被 replacement owner 完整替代的实现：通过门禁后必须彻底移除，不保留僵尸兼容层。
3. 历史设计证据：Accepted ADR 必须保留；重复且已失效的非 ADR 阶段文档可以删除。

### 进入最终删除阶段的门禁

- replacement owner、等价行为测试和 vNext 产品入口明确。
- 生产代码与测试对旧 header/symbol/target 零 include/link/call。
- Windows/Linux 构建与直接 GoogleTest 通过。
- 删除前最后一次通过当前四条 Legacy product smoke，并分别保留退出、画面、资源回收证据；Audio/Asset
  没有独立 `--smoke-audio`，按对应切片补专项证据。
- vNext replacement 已覆盖 2D/UI/3D/Asset/Audio 所需产品门禁；Null sample 或 clear-only Desktop
  不能冒充完整产品替代。

### 删除完成定义

- 删除旧入口、源码、public header、调用点和只服务旧 API 的 compatibility facade；不留 `#if 0`、
  注释代码、`*_old`、无消费者 alias 或“以后也许有用”的 shim。
- 删除 CMake target/alias/source list/option/guard、对应 preset 设置、默认 vcpkg `legacy` feature、无用依赖
  和 submodule。`TINA_BUILD_LEGACY` 先在完整产品门禁后默认 OFF，零引用后再连同 guard 一起删除。
- EASTL/EABase 只有在全构建图零引用后删除；保留 vNext `Tina::Core`，不要误删仍由
  `Tina::RenderBgfx` 使用的 `thirdparty/bgfx.cmake`。`xxHash` 等依赖按实际剩余消费者判断，不能因曾与
  CoreLegacy 同组就连带删除。
- 先把仍有效的行为与回归意图迁移到 replacement tests，再删除旧实现专属测试、fixture、sample 和
  smoke；若测试断言已被 Superseded ADR 替代的旧公共契约，按新 ADR 重写/替换并建立 coverage。禁止
  删测试来制造通过结果。
- 仅在无剩余消费者且 replacement asset path 已验证后删除旧 resource/shader 与 copy/cook 逻辑。
- 更新 `README.md`、`README_CN.md`、architecture、building、testing、dependencies、design-freeze、
  roadmap 和主题文档。反转 Accepted 决策时新建替代 ADR，把旧 ADR 标为 `Superseded` 并链接新编号，
  同步 `docs/adr/README.md`；不改写历史理由。历史 ADR 中保留旧名称不违反零引用。被权威主题文档完全
  替代的重复非 ADR 文档应修正链接后删除。
- 最终清理是不夹带新功能、可独立回滚的提交。Git history 是旧代码归档，不用死代码保存历史。

### 零引用核验

先扫当前代码与构建图，再人工检查 target source/link 列表、manifest、submodule、资源和测试条件分支：

```powershell
rg -n "TINA_BUILD_LEGACY|Tina::CoreLegacy|tina_core_legacy|Tina::Procedural|tina_procedural|Tina::Miniaudio|tina_miniaudio|--smoke-|Application::instance|SceneManager" `
  CMakeLists.txt CMakePresets.json vcpkg.json .gitmodules cmake include src tests samples `
  -g "!build/**" -g "!out/**" -g "!thirdparty/**" -g "!dependencies/**"
rg -n "EASTL|EABase|TINA_BUILD_SHADERS|CopyResources|TINA_SHADER_ROOT_DIR|add_shader_compile_dir|resources/(fonts|textures|audio|config|shaders)" `
  CMakeLists.txt CMakePresets.json vcpkg.json .gitmodules cmake include src tests samples `
  -g "!build/**" -g "!out/**" -g "!thirdparty/**" -g "!dependencies/**"
rg --files src/core
rg -n "TINA_BUILD_LEGACY|CoreLegacy|EASTL|EABase|Application|Legacy" docs -g "!docs/adr/**"
```

零文本命中不是充分条件：资源可能按文件名加载，target 可能通过变量组装，依赖可能由默认 feature
间接带入，未列入 target source list 的 header-only 兼容文件也可能残留。把 `rg --files src/core` 与
`tina_core`/`tina_core_legacy` source list、全仓库消费者逐项对照；当前主题文档命中必须改成准确状态，
历史 ADR 命中可以保留。

## 验证

- 运行受影响 target 的直接 GoogleTest executable；不要改用 CTest。
- 对公开头运行 header-isolation 测试并扫描第三方 token。
- 对生命周期或 backend 改动运行对应 300 帧 Null/Platform/Desktop sample。
- 对 Legacy 删除前运行四条 Legacy smoke；删除后运行等价 vNext 产品门禁，并把退出、资源、画面、
  Audio/Asset 专项证据分开报告。
- 联合使用 `git status --short`、`git diff --check` 和 `git diff --stat`；diff 命令不会列出 untracked 文件。
