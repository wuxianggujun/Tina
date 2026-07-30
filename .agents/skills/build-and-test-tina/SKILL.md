---
name: build-and-test-tina
description: 为 Tina C++23 Runtime 选择、配置、构建和排查 Windows/Linux 的 Null、GLFW、bgfx、UI/FreeType/UIA、Asset、Audio、Physics2D、产品 sample 与 sanitizer 门禁。Use when changing CMake/vcpkg/submodules、选择最小 target/preset、诊断 configure/build/link/test failure，或需要运行并报告直接 GoogleTest、sample、视觉/UIA 产品 gate。
---

# 构建与验证 Tina

## 先确认环境

先阅读 `CMakePresets.json`、`vcpkg.json`、`cmake/Dependencies.cmake`、`docs/building.md`、
`docs/testing.md`，并执行：

```powershell
git status --short
cmake --version
cmake --list-presets
git submodule status --recursive
```

要求 CMake 3.25+、C++23 与 `VCPKG_ROOT`。只在缺少实际所需 submodule 时初始化。
禁止 `--clean-first` 或删除整个 build tree；同一 Visual Studio build tree 的 Debug/Release 串行构建。

## 选择最小构建图

| 范围 | Configure preset | Build preset / 关键 target |
| --- | --- | --- |
| Core/Null/基础模块 | `windows-msvc-vnext` | `windows-vnext-debug`；`tina_tests` 与受影响模块 tests |
| GLFW Platform | `windows-msvc-vnext-platform` | `windows-vnext-platform-debug`；`tina_platform_glfw_tests` |
| bgfx Desktop/2D/3D | `windows-msvc-vnext-bgfx` | `windows-vnext-bgfx-debug`；`tina_render_bgfx_tests`、samples |
| UI + FreeType + UIA | `windows-msvc-vnext-bgfx-ui-freetype` | `windows-vnext-bgfx-ui-freetype-debug`；UI/UIA tests + showcase |
| 完整 2D 产品 | `windows-msvc-vnext-bgfx-product-2d` | `windows-vnext-bgfx-product-2d-debug`；Physics/Audio/UI/Asset + `tina_sample_2d` |
| Physics2D 或 Audio 专图 | 对应 `windows-msvc-vnext-*` preset | 对应模块 tests/bench |

Legacy 产品图已删除；`TINA_BUILD_LEGACY=ON` 必须 FATAL。FreeType 字体通过 CMake/env
`TINA_UI_FONT_PATH`，源码和终端保持 UTF-8，MSVC 使用 `/utf-8`。

## 执行规则

- 直接运行每个 GoogleTest executable；不用 CTest。
- 先跑最小受影响 target，再按公开契约、跨模块寿命和 backend blast radius 扩大。
- header-isolation 是编译门禁，不是独立 executable。
- sample exit 0 只证明生命周期/结构化断言；画面、字体、UIA、screen reader 与性能结论分别取证。
- 不用固定测试数量定义架构状态；报告 executable 名、退出码、skip 与环境限制。
- 失败先保留第一个 configure/build/test 输出，定位 source/config/CRT/feature 图，不通过 clean 掩盖根因。

## 产品 gate

| Gate | 入口 |
| --- | --- |
| Windows 2D | `tools/windows/RunProduct2dGate.ps1` |
| Windows 3D | `tools/windows/RunProduct3dGate.ps1` |
| UIA 外部 HWND/action | `tools/windows/RunUi002UiaGate.ps1` |
| UI DPI/ROI | `tools/windows/RunUi003VisualGate.ps1`、`tools/windows/RunUi003SizeMatrix.ps1` |
| Linux GCC/Clang/sanitizer | `tools/linux/run-*-gate.sh` 或对应 Windows Docker wrapper |

使用 gate 的 `-SkipConfigure`/`-SkipBuild` 前先确认 build tree 与当前 commit/config 匹配。真实 GLFW/bgfx、
UIA、FreeType、Audio device、GPU 与 DPI gate 必须报告实际环境，不能由 Null 测试替代。

## 收尾报告

执行 `git status --short` 与 `git diff --check`；分别列出 configure、build、每个 executable、sample、
视觉/平台 gate 的结果。生成在 `out/`、`artifacts/` 的证据不默认提交。
