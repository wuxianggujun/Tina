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
| `src/render/bgfx` | 私有 bgfx 后端 |
| `samples/` | 产品与 infrastructure 样例 |
| `tests/` | GoogleTest；`tests/runtime_ui` 为 Runtime→UI 专项 |
| `branding/Tina.jpg` | 引擎吉祥物（非玩法资源） |
| `resources/fonts/` | **可选** FreeType fixture；优先 `TINA_UI_FONT_PATH` |
| `docs/adr/` | ADR；不改写历史 Accepted 理由 |

**已删除：** 旧 `src/game` `src/engine` `src/ecs` `src/renderer` `src/particles` `src/physics` 产品图、`Tina.exe`、`src/vnext/` 前缀、EASTL submodule。

## 构建与测试（禁止 clean-first 全量 wipe）

```powershell
cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug --target tina_sample_2d tina_sample_3d tina_tests --parallel 2 -- /nr:false
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
```

- 直接跑 GoogleTest executable，不用 CTest。
- FreeType 字体：`-DTINA_UI_FONT_PATH=...` 或环境变量 `TINA_UI_FONT_PATH`（见 `cmake/TinaUiFont.cmake`）。
- 不要提交 `.agents/`、`.tmp_*`；用户未要求不要提交 `AGENTS.md`。
- 多 worktree 默认先在功能分支完成编码并提交，再合并到核心集成 worktree，最后复用核心 worktree 的
  常驻 `out/build/<preset>` 做集中增量验证；不要在每个功能 worktree 重复构建 bgfx/shaderc/产品图。
- 禁止不同 worktree 共用同一个 CMake `binaryDir`；preset 基于 `${sourceDir}`，cache 与生成项目绑定
  源码绝对路径。只有确需合并前验证的高风险切片才创建该 worktree 自己的临时 build tree。

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
2. 最小受影响 target 构建 + 直接 GoogleTest
3. 相关 sample 短 smoke
4. 公开头无第三方泄漏
