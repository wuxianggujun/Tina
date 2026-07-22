---
name: build-and-test-tina
description: 配置、构建、测试和排查 Tina C++23 游戏引擎的 Windows/Linux、Null、GLFW、bgfx、UI 与 sanitizer 构建图。Use when changing CMake/vcpkg/submodules, selecting presets or targets, diagnosing configure/build/link failures or failures observed while running documented test/sample smoke gates.
---

# 构建与测试 Tina

## 先确认环境

阅读 `CMakePresets.json`、`vcpkg.json`、`cmake/Dependencies.cmake`、`docs/building.md`、`docs/testing.md`。

```powershell
git status --short
cmake --version
cmake --list-presets
git submodule status --recursive
```

要求 CMake 3.25+、C++23、`VCPKG_ROOT`。bgfx 缺 submodule 时才 `git submodule update --init --recursive`。  
**禁止** `cmake --build ... --clean-first` 全量 wipe；用户硬性禁 clean。

## 选择 preset

| 需求 | Configure | 关键 target |
| --- | --- | --- |
| Headless/Null | `windows-msvc-vnext` | `tina_tests`、`tina_sample_null` |
| GLFW + Null | `windows-msvc-vnext-platform` | `tina_platform_glfw_tests`、`tina_sample_platform` |
| GLFW + bgfx 产品 | `windows-msvc-vnext-bgfx` | samples、`tina_render_bgfx_tests` |
| 2D 全 feature | `windows-msvc-vnext-bgfx-product-2d`（若存在） | `tina_sample_2d` + physics/freetype/audio |

**Legacy 产品图已删除。** `TINA_BUILD_LEGACY=ON` → FATAL。默认 vcpkg feature 为 `tests`（无 `legacy`）。

## Windows 产品冒烟

```powershell
cmake --preset windows-msvc-vnext-bgfx
cmake --build --preset windows-vnext-bgfx-debug --target tina_sample_2d tina_sample_3d tina_tests -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
```

- 直接跑 GoogleTest executable，不用 CTest。  
- FreeType：`-DTINA_UI_FONT_PATH=...` 或 env `TINA_UI_FONT_PATH`。  
- Debug/Release 输出在 `bin/<Config>`，禁止混用 CRT/GTest。

## 源码布局

`include/tina/<module>/` 公开头；实现 `src/<module>/`（**无** `src/vnext/` 前缀）。  
测试 `tests/<module>/`；Runtime→UI 专项为 `tests/runtime_ui`。

## 报告

分别报告各 GoogleTest executable 与 sample exit code；最后 `git status --short`。
