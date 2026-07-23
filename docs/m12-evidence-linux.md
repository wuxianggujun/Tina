# M12 / tip Linux 证据

本文记录 **Docker Desktop → Linux 容器** 对当前 tip 的可复现证据。
Windows 证据见 [m12-evidence-windows.md](m12-evidence-windows.md)。

## TEST-001 范围说明

Backlog `TEST-001` 验收为：

- GCC Null / GLFW
- Clang sanitizer
- 记录工具链、返回码、sanitizer 与 X11/Wayland 条件

下文 **仅关闭 GCC13 Null 子图**；GLFW 与 Clang sanitizer **仍未关闭**，故 `TEST-001` 整体仍为 Planned/Partial。

## Docker 资产

| 路径 | 作用 |
| --- | --- |
| `docker/linux-gcc13/Dockerfile` | Ubuntu 24.04 + GCC 13 + Ninja + vcpkg（checkout `vcpkg-configuration.json` baseline） |
| `tools/linux/run-gcc13-null-gate.sh` | 容器内 configure/build/run Null 门禁 |
| `tools/windows/RunLinuxGcc13NullGate.ps1` | Windows 宿主启动 Docker 门禁 |

镜像名：`tina-linux-gcc13:test-001`。

```powershell
docker build -f docker/linux-gcc13/Dockerfile -t tina-linux-gcc13:test-001 .
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxGcc13NullGate.ps1 `
  -SkipImageBuild -OutJson artifacts\gates\test-001-linux-gcc13-null.json
```

## 2026-07-23 tip `e0d94faa` — GCC13 Null

| 项 | 值 |
| --- | --- |
| Host | Windows Docker Desktop 4.39 / Engine 28.0.1 |
| Container kernel | Linux 6.18.33.2-microsoft-standard-WSL2 x86_64 |
| Image | `tina-linux-gcc13:test-001` |
| Toolchain | gcc-13 / g++-13 13.3.0 (Ubuntu 24.04) |
| CMake | 3.28.3 |
| Ninja | 1.11.1 |
| vcpkg | baseline `81de6771512413aaf89ea77add5ad1fda126b9d0`（与仓库 `vcpkg-configuration.json` 一致） |
| Preset | `linux-gcc13-vnext` / `linux-gcc13-vnext-debug` |
| Features | `tests` only；`TINA_BUILD_RENDER_BGFX=OFF`；`TINA_BUILD_SHADERS=OFF` |
| Evidence JSON | `artifacts/gates/test-001-linux-gcc13-null.json`（`ok=true`, `exitCode=0`） |

### 直接运行结果

| Executable | 结果 |
| --- | --- |
| `tina_tests` | exit 0 |
| `tina_ui_tests` | **255/255 PASS** |
| `tina_runtime_ui_tests` | **83/83 PASS** |
| `tina_ui_render_integration_tests` | **13/13 PASS** |
| `tina_sample_null --frames=300` | exit 0，`exit=GameRequestedExitAfterCurrentFrame` |

### 不证明

- Linux GLFW/X11 或 Wayland 窗口路径
- Clang 22 + libstdc++15 或 ASan/UBSan/LSan
- product-2d / FreeType / Physics2D / miniaudio 产品图
- 真实 GPU / 截图视觉

## 未关闭（TEST-001 尾巴）

1. `linux-gcc13-vnext-platform`（GLFW/X11）及可选 Wayland  
2. `linux-clang22-vnext` Null  
3. `linux-clang22-vnext-sanitize`（ASan/UBSan/LSan）及 platform sanitizer 变体  

在上述完成前，不得把「Linux tip 全门禁」写成 Done。
