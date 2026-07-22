# M12：Legacy 产品 / Legacy UI 退役状态

> 用户授权：废弃旧横版 2D 小游戏与 Legacy UI，产品以 vNext 为准。

## 已删除（源码与构建图）

| 路径 / 目标 | 说明 |
| --- | --- |
| `src/ui/**` | Legacy UI 实现 |
| `src/game/**` | Menu/Game/Settings/Pause/WorldSelect/TileMap 等产品场景 |
| `src/engine/**` | Legacy Application / Scene / Event / Resource |
| `src/ecs/**`、`src/renderer/**`、`src/particles/**`、`src/physics/**` | Legacy 玩法/渲染/物理 |
| `src/main.cpp`、`src/CMakeLists.txt` | Legacy `Tina.exe` 入口 |
| `src/platform/audio/**` | Legacy miniaudio 实现 TU |
| `src/core` 中 CoreLegacy/EASTL/Procedural 兼容面 | `Core.hpp`、`EASTLAlloc`、`Path`、`Noise` 等 |
| `tests/ui/**`、`tests/engine/**`、Legacy core 测试 | 旧实现专属测试 |
| 根 `add_subdirectory(src)`（Legacy 图） | 已移除 |
| `Tina::CoreLegacy` / `Tina::Procedural` / `Tina::Miniaudio` | 目标已删 |
| EASTL submodule 构建 | `thirdparty` 不再 `add_subdirectory(EASTL)` |
| `vcpkg` 默认 feature `legacy` | 默认仅 `tests` |

## 构建策略

- `TINA_BUILD_LEGACY` **默认 OFF**；若显式 `ON` → **FATAL_ERROR**（源码已不存在）。
- 产品入口：`Tina::Desktop` + `tina_sample_*`（尤其 `tina_sample_2d` / `tina_sample_3d`）。
- UI：**仅** `include/tina/ui` + `src/ui`（Retained UI 实现；公开头仍在 `include/tina/ui`）。

## 保留 / 扫尾

- `thirdparty/bgfx.cmake`（RenderBgfx）— **保留**
- EASTL/EABase submodule — **已从 `.gitmodules` 与工作树移除**
- `src/vnext/`、`tests/vnext/` 目录前缀 — **已扁平化**（`02e13d7a`）：实现在 `src/<module>`，
  测试在 `tests/<module>`（Runtime→UI 专项为 `tests/runtime_ui`）；公开头仍为 `include/tina/`
- `resources/` — **保留有消费者的资产**：
  - `resources/fonts/SourceHanSansSC-Regular.otf`：`tina_sample_2d`（FreeType 图）、Desktop UI 字体、
    FreeType 测试宏路径
  - 其余 `audio/`、`textures/`、`shaders/` 等：当前 CMake/产品样例**无**硬编码路径；可后续单独审计删除
  - 产品 **不**再 `file(COPY)` 整棵 `resources/` 进 vNext-only 构建图

## 验证（禁止 clean-first 全量 wipe）

```powershell
cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug --target tina_sample_2d tina_sample_3d tina_ui_tests -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
```

## 仍非“整包 M12 完成”的项

- Linux 全门禁复验
- EASTL/EABase submodule 物理移除
- `resources/` 中仅服务已删产品的资产清理
- 主题文档扫尾：building/testing/README 等已去掉 `Tina.exe`/`--smoke-*` 产品命令