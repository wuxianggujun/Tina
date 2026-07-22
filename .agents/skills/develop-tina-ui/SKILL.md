---
name: develop-tina-ui
description: 实现、重构或排查 Tina 的 Retained UI，包括节点所有权、布局、focus/capture、事件路由、输入消费、控件、中文字体和 UI 渲染。Use when editing src/ui, include/tina/ui, tests/ui, tests/ui_freetype, docs/ui.md, or ADR 0011.
---

# 开发 Tina UI

## 唯一产品轨道

Tina **只有一套** 产品 UI：`include/tina/ui/**` + `src/ui/**`（Retained）。  
Legacy UI / `Tina.exe` 产品图已删除；禁止再引入第二套 UI ABI。

| 项 | 路径 / 事实 |
| --- | --- |
| 公开头 | `include/tina/ui/**` |
| 实现 | `src/ui/**`（可选 `src/ui/freetype`） |
| 测试 | `tests/ui`、`tests/ui_freetype`、`tests/runtime_ui`、`tests/ui_render` |
| Context / ID | `UIContext`、`UINodeId`（owner WindowId + generation） |

先读：`docs/ui.md`、`docs/adr/0011-retained-ui.md`、相关 `include/tina/ui/*.hpp`、`src/ui/**`、对应测试 CMakeLists。

## 约定

- owner-thread、固定容量 PMR、move-only root owner。
- 布局 / hit / paint 用 committed snapshot；route 不隐式 layout。
- Game SDK 不暴露 FreeType/bgfx；字体字节由游戏或可选 fixture 注入。
- FreeType 字体路径：`TINA_UI_FONT_PATH`（CMake/env）或可选 `resources/fonts/*.otf`（见 `cmake/TinaUiFont.cmake`）。

## 验证

```powershell
cmake --build --preset windows-vnext-bgfx-debug --target tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_ui_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_runtime_ui_tests.exe --gtest_color=yes
```

有 FreeType 时再编/跑 `tina_ui_freetype_tests`；无字体则相关用例应 skip。
