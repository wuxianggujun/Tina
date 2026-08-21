# Tina 项目 AI 开发指南

## 产品定位

Tina 是 C++23 游戏 Runtime。产品路径为 vNext Desktop + samples；Legacy `Tina.exe` / 旧横版 2D / 旧 UI 产品图已删除。

事实优先级：
1. 当前源码、CMake target、测试与实际运行结果
2. `docs/design-freeze.md`、ADR（`docs/adr/README.md`）
3. 主题文档；与代码冲突时以代码与最近提交为准

开始任务先 `git status --short`。搜索用 `rg`，默认排除 `.git/`、`build/`、`out/`、`artifacts/`、`logs/`、`temp/`、`thirdparty/`、`dependencies/`。

## 首次阅读

1. `README_CN.md`、`docs/README.md`、`docs/architecture.md`
2. `docs/design-freeze.md`、`docs/adr/README.md`
3. 任务对应主题文档与模块 CMakeLists / 测试
4. 改公开契约前读 `docs/public-api.md`；构建验证读 `docs/building.md`、`docs/testing.md`

## 目录边界

| 路径 | 职责 |
| --- | --- |
| `include/tina/<module>/` | 公开头（无第三方 token） |
| `src/core` `src/platform` `src/render` `src/runtime` `src/task` `src/desktop` `src/integration` | Runtime / Platform / Render / Desktop |
| `src/ui` | 产品 Retained UI 实现（**不是**已删的 Legacy UI） |
| `src/scene` `src/asset` `src/asset_format` `src/audio` `src/physics2d` | Scene / Asset / Audio / Physics |
| `src/editor` `src/editor_app` | `Tina::Editor` authoring 模块与 `TinaEditor.exe` 桌面组合（见 `docs/editor-2d.md`） |
| `src/render/bgfx` | 私有 bgfx 后端 |
| `samples/` | 产品与 infrastructure 样例 |
| `tests/` | GoogleTest；`tests/runtime_ui` 为 Runtime→UI 专项 |
| `branding/Tina.jpg` | 引擎吉祥物（非玩法资源） |
| `resources/fonts/` | **可选** FreeType fixture；优先 `TINA_UI_FONT_PATH` |
| `docs/adr/` | ADR；不改写历史 Accepted 理由 |

**已删除：** 旧 `src/game` `src/engine` `src/ecs` `src/renderer` `src/particles` `src/physics` 产品图、`Tina.exe`、`src/vnext/` 前缀、EASTL submodule。

## 构建与测试（禁止 clean-first 全量 wipe）

### 用户请求授权边界（优先于默认验证流程）

- “编译”“构建”“生成版本”“给我编译版本”默认都是 **compile-only**：只允许必要的 configure 和指定 target
  build，不得运行 GoogleTest、CTest、sample、smoke、产品 gate、视觉 gate，也不得启动刚生成的可执行文件。
- 用户说“我手动测试”“编译给我看看”时，交付可执行文件路径后立即停止；不得以“验证编译版本”为由代替用户
  启动程序。只有用户明确要求“运行”时才可启动对应程序，明确要求“测试/smoke/gate”时才可运行对应自动门禁。
- “编译并运行”只授权 build + 启动用户指定程序，不自动授权 GoogleTest、smoke 或其他 gate；“编译并测试”才
  授权 build + 明确相关测试。授权范围不从“大功能闭环”“完成验证”或下方示例命令中隐式扩大。
- compile-only 的结果只报告编译 target、退出码、产物路径/时间和残留进程；不得报告“测试通过”。如编译成功，
  必须直接把 `TinaEditor.exe` 等产物交给用户人工测试真实交互与真实导入流程。

```powershell
cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug --target tina_sample_2d tina_sample_3d tina_tests --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
```

- 直接跑 GoogleTest executable，不用 CTest。
- Editor 开发采用“大功能闭环后统一验证”的专项节奏，优先级高于通用的小切片验证规则。大功能完成前的
  小功能、小细节和连续源码切片不得新增或修改测试，也不得 configure、build、运行 GoogleTest、sample、smoke
  或产品 gate；此阶段只做源码/API 阅读、定向静态搜索、`git status` 与 `git diff --check`。
- Editor 大功能或里程碑全部实现并完成文档同步后，才复用常驻 build tree 集中执行一次受影响 target 的增量
  build、已有 Editor test executable 和必要的 2D/3D 短 smoke。统一 gate 发现问题时先集中修复，再只重跑失败
  或直接受影响项；禁止退化为每修一个小细节就构建或测试。该默认统一 gate 仅在用户没有把请求限定为
  compile-only 时适用；用户要求手动测试时只 build。Editor 功能验收不要求补测试代码。
- FreeType 字体：`-DTINA_UI_FONT_PATH=...` 或环境变量 `TINA_UI_FONT_PATH`（见 `cmake/TinaUiFont.cmake`）。
- 不要提交 `.agents/`、`.tmp_*`；用户未要求不要提交 `AGENTS.md`。
- 多 worktree 默认先在功能分支完成编码并提交，再合并到核心集成 worktree，最后复用核心 worktree 的
  常驻 `out/build/<preset>` 做集中增量验证；不要在每个功能 worktree 重复构建 bgfx/shaderc/产品图。
- 禁止不同 worktree 共用同一个 CMake `binaryDir`；preset 基于 `${sourceDir}`，cache 与生成项目绑定
  源码绝对路径。只有确需合并前验证的高风险切片才创建该 worktree 自己的临时 build tree。
- Linux Editor `zenity`/`kdialog` 门禁只允许 primary 环境执行一次 configure/build/test/smoke；secondary
  环境必须校验源码指纹与二进制 hash 后直接复用，不得再次调用 CMake 或重复测试。最后一个环境成功后删除
  专用 `out/build/docker-linux-gcc13-vnext-bgfx-editor`，失败时只为诊断保留并在重试/记录后回收。
- Linux `compile-only` 只允许一次最小 target 的 configure/build，不启动 GoogleTest executable、sample、
  visual/platform gate，也不因切换 helper/container 再编译。同一 source/toolchain/target 指纹已有成功记录时直接
  复用该结论，不重复构建。其 build tree 一律视为临时资源，取得编译结果和首错记录后立即删除。
- 构建/gate 收尾必须报告并回收本轮启动的编译进程、helper、watchdog、窗口管理器、容器和 agent；不得把
  临时跨平台 build tree、container volume 和一次性镜像/缓存长期留在项目内。收尾还要核验临时目录不存在，
  并报告回收前后字节数及 `buildTree/process/container/volume/image/cache/agent` 的资源状态；任何字段未核验时
  不得声称“资源已释放”。常驻核心 build tree 不因提速或日常收尾而全量删除，但必须登记路径、占用和保留原因。

## 核心约定

- `EngineHost` 唯一非全局组合根；无新 Singleton/Service Locator。
- `IGameApplication` 生命周期；帧逻辑只在 `IGameState`。
- 公共边界 `Result`/`Status`；Game SDK 不暴露 bgfx/GLFW/EnTT/miniaudio 类型。
- 资源：Catalog cooked + AssetId；引擎不托管完整游戏内容树。
- `TINA_BUILD_LEGACY` 仅 OFF；ON → FATAL。

## 技能

- 架构/退役：`navigate-tina-architecture`
- 构建测试：`build-and-test-tina`
- Runtime：`develop-tina-vnext-runtime`
- UI：`develop-tina-ui`

## 完成验证

1. `git status` / `git diff --check`
2. 非 Editor 改动构建最小受影响 target；Editor 小切片在大功能闭环前到第1步即止，不新增/修改/运行测试，
   也不运行 build 或 smoke
3. 先按用户授权区分 compile-only、run 与 test gate；compile-only 到成功 build 和产物交付即止。只有明确 test gate
   才直接运行 GoogleTest；Editor 大功能/里程碑闭环后的默认统一 gate 不覆盖用户明确限定的 compile-only，
   Linux `compile-only` 必须保持
   `testRuns=0 sampleRuns=0`
4. 只有明确授权 smoke/gate 时才运行相关 sample 短 smoke
5. 公开头无第三方泄漏
