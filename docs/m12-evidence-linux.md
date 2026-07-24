# M12 / tip Linux 证据

本文记录 **Docker Desktop → Linux 容器** 对当前 tip 的可复现证据。
Windows 证据见 [m12-evidence-windows.md](m12-evidence-windows.md)。

## TEST-001 范围说明

Backlog `TEST-001` 验收为：

- GCC Null / GLFW
- Clang sanitizer
- 记录工具链、返回码、sanitizer 与 X11/Wayland 条件

已关闭：**GCC13 Null**、**GCC13 Platform（GLFW/X11 + Xvfb）**。  
未关闭：**Clang 22 Null / sanitizer**（Docker 拉 LLVM/PPA 受宿主网络/代理影响，见文末）。

故 `TEST-001` 仍为 **Partial**，不得写成全 Done。

## Docker 资产

| 路径 | 作用 |
| --- | --- |
| `docker/linux-gcc13/Dockerfile` | GCC13 Null；默认阿里云 apt + 清华 vcpkg + DaoCloud Ubuntu |
| `docker/linux-gcc13-platform/Dockerfile` | GCC13 + X11/GLFW + Xvfb；同上国内镜像默认 |
| `docker/linux-clang22/Dockerfile` | Clang22 + g++-15；清华 llvm-apt / toolchain 镜像 + 国内 apt |
| `docker/README.md` | 镜像源说明与 `--build-arg` 覆盖 |
| `tools/linux/run-gcc13-null-gate.sh` | Null 门禁 |
| `tools/linux/run-gcc13-platform-gate.sh` | Platform/GLFW 门禁（优先 `xvfb-run`） |
| `tools/linux/run-clang22-null-gate.sh` | Clang22 Null 门禁 |
| `tools/linux/run-clang22-sanitize-gate.sh` | Clang22 ASan/UBSan/LSan 门禁 |
| `tools/windows/RunLinuxDockerGate.ps1` | 通用宿主启动器（`-Gate gcc13-null|gcc13-platform|clang22-null|clang22-sanitize`） |
| `tools/windows/RunLinuxGcc13NullGate.ps1` | 兼容旧入口（Null only） |

```powershell
# GCC13 Null
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxDockerGate.ps1 `
  -Gate gcc13-null -OutJson artifacts\gates\test-001-linux-gcc13-null.json

# GCC13 Platform (GLFW/X11 via Xvfb)
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxDockerGate.ps1 `
  -Gate gcc13-platform -OutJson artifacts\gates\test-001-linux-gcc13-platform.json
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

- Linux GLFW/X11 或 Wayland 窗口路径（见下一节 Platform）
- Clang 22 + libstdc++15 或 ASan/UBSan/LSan
- product-2d / FreeType / Physics2D / miniaudio 产品图
- 真实 GPU / 截图视觉

## 2026-07-24 tip `d883d787` — GCC13 Platform (GLFW/X11 + Xvfb)

| 项 | 值 |
| --- | --- |
| Image | `tina-linux-gcc13-platform:test-001` |
| Preset | `linux-gcc13-vnext-platform` / `linux-gcc13-vnext-platform-debug` |
| Features | `tests;platform-glfw`；`TINA_BUILD_PLATFORM_GLFW=ON`；bgfx OFF |
| Display | `xvfb-run -a`（无真实显示器；**非** Wayland） |
| Evidence JSON | `artifacts/gates/test-001-linux-gcc13-platform.json`（`ok=true`） |

| Executable | 结果 |
| --- | --- |
| `tina_tests` | exit 0（Xvfb） |
| `tina_platform_glfw_tests` | **34/34 PASS**（Xvfb） |
| `tina_sample_platform --frames=60` | exit 0（Xvfb） |

### 不证明

- 真实物理显示器 / GPU / Wayland
- product Desktop/bgfx 路径

## 未关闭（TEST-001 尾巴）

1. `linux-clang22-vnext` Null  
2. `linux-clang22-vnext-sanitize`（ASan/UBSan/LSan）  
3. 可选：Wayland platform 变体  

**阻塞说明（2026-07-24）**：在本机 Docker Desktop 上构建 `docker/linux-clang22` 时，
`apt`/`ppa:ubuntu-toolchain-r/test`/`apt.llvm.org` 多次因宿主 HTTP 代理（如失效的
`127.0.0.1:7890`）或外网不稳定失败。脚本与 Dockerfile 已入库；网络恢复后用
`RunLinuxDockerGate.ps1 -Gate clang22-null` / `clang22-sanitize` 复跑即可。

在 Clang 门禁通过前，不得把「Linux tip 全门禁」写成 Done。
