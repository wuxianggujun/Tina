# M12 / tip Linux 证据

本文记录 **Docker Desktop → Linux 容器** 对当前 tip 的可复现证据。
Windows 证据见 [m12-evidence-windows.md](m12-evidence-windows.md)。

## TEST-001 范围

| 子图 | 状态 |
| --- | --- |
| GCC13 Null | **Done** |
| GCC13 Platform (GLFW/X11 + Xvfb) | **Done** |
| Clang22 Null | **Done** |
| Clang22 ASan/UBSan/LSan Null | **Done** |

可选后置：Wayland platform、真实物理显示器/GPU。

## Docker 资产

| 路径 | 作用 |
| --- | --- |
| `docker/linux-gcc13/Dockerfile` | GCC13 Null；默认阿里云 apt + 清华 vcpkg + DaoCloud Ubuntu |
| `docker/linux-gcc13-platform/Dockerfile` | GCC13 + X11/GLFW + Xvfb |
| `docker/linux-clang22/Dockerfile` | Clang22 + g++-15 + `libclang-rt-22-dev` + 国内镜像 |
| `docker/README.md` | 镜像源与 `--build-arg` |
| `tools/linux/run-*.sh` | 容器内门禁脚本 |
| `tools/windows/RunLinuxDockerGate.ps1` | 统一宿主入口 |

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxDockerGate.ps1 `
  -Gate gcc13-null -OutJson artifacts\gates\test-001-linux-gcc13-null.json
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxDockerGate.ps1 `
  -Gate gcc13-platform -OutJson artifacts\gates\test-001-linux-gcc13-platform.json
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxDockerGate.ps1 `
  -Gate clang22-null -OutJson artifacts\gates\test-001-linux-clang22-null.json
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxDockerGate.ps1 `
  -Gate clang22-sanitize -OutJson artifacts\gates\test-001-linux-clang22-sanitize.json
```

## 2026-07-23 tip `e0d94faa` — GCC13 Null

| 项 | 值 |
| --- | --- |
| Image | `tina-linux-gcc13:test-001` |
| Toolchain | gcc-13 / g++-13 13.3.0 (Ubuntu 24.04) |
| CMake / Ninja | 3.28.3 / 1.11.1 |
| vcpkg baseline | `81de6771512413aaf89ea77add5ad1fda126b9d0` |
| Preset | `linux-gcc13-vnext` |
| Evidence | `artifacts/gates/test-001-linux-gcc13-null.json` |

| Executable | 结果 |
| --- | --- |
| `tina_tests` | exit 0 |
| `tina_ui_tests` | **255/255** |
| `tina_runtime_ui_tests` | **83/83** |
| `tina_ui_render_integration_tests` | **13/13** |
| `tina_sample_null --frames=300` | exit 0 |

## 2026-07-24 tip `d883d787` — GCC13 Platform (GLFW/X11 + Xvfb)

| 项 | 值 |
| --- | --- |
| Image | `tina-linux-gcc13-platform:test-001` |
| Preset | `linux-gcc13-vnext-platform` |
| Display | `xvfb-run -a`（**非** Wayland / 物理显示器） |
| Evidence | `artifacts/gates/test-001-linux-gcc13-platform.json` |

| Executable | 结果 |
| --- | --- |
| `tina_tests` | exit 0 (Xvfb) |
| `tina_platform_glfw_tests` | **34/34** (Xvfb) |
| `tina_sample_platform --frames=60` | exit 0 (Xvfb) |

## 2026-07-24 tip `66374135` — Clang22 Null

| 项 | 值 |
| --- | --- |
| Image | `tina-linux-clang22:test-001` |
| Toolchain | clang-22 + g++-15 libstdc++（chainload） |
| Preset | `linux-clang22-vnext` |
| Evidence | `artifacts/gates/test-001-linux-clang22-null.json` |

| Executable | 结果 |
| --- | --- |
| `tina_tests` | exit 0 |
| `tina_ui_tests` | **255/255** |
| `tina_runtime_ui_tests` | **83/83** |
| `tina_ui_render_integration_tests` | **15/15**（含 ContentScale*） |
| `tina_sample_null --frames=300` | exit 0 |

## 2026-07-24 tip `66374135` — Clang22 sanitizer Null

| 项 | 值 |
| --- | --- |
| Image | `tina-linux-clang22:test-001`（含 `libclang-rt-22-dev`） |
| Preset | `linux-clang22-vnext-sanitize` |
| Sanitizer | ASan + UBSan + LSan（`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` 等） |
| Evidence | `artifacts/gates/test-001-linux-clang22-sanitize.json` |

| Executable | 结果 |
| --- | --- |
| `tina_tests` | exit 0 |
| `tina_ui_tests` | exit 0 |
| `tina_runtime_ui_tests` | exit 0 |
| `tina_ui_render_integration_tests` | **15/15** |
| `tina_sample_null --frames=300` | exit 0 |

## 不证明

- 真实物理显示器 / GPU / Wayland
- product-2d / FreeType / Physics2D / miniaudio 产品图（Linux）
- 跨 GPU 视觉 golden

## 结论

**TEST-001 主验收（GCC Null/GLFW + Clang sanitizer）在 Docker 复现路径上已关闭。**
可选后置：Wayland、真显示器、Linux product-2d。
