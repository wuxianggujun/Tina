# Docker tip re-evidence (2026-08-03)

Host: Windows 11 + Docker Desktop Linux engine.  
Images reused (no rebuild): `tina-linux-gcc13:test-001`, `tina-linux-gcc13-platform:test-001`,
`tina-linux-clang22:test-001`.  
Tip under test: `b8360c2d` (Motion facade + showcase transitions).

## Infrastructure fix

WSL (`/mnt/c/...`) and Docker (`/work/tina`) share the same bind-mounted build tree. Stale
`CMakeCache.txt` with the other absolute source path made `cmake --preset` fail immediately.

Mitigation (no clean wipe): `tools/linux/cmake-cache-source-guard.sh` removes **only**
`CMakeCache.txt` when `CMAKE_HOME_DIRECTORY` ≠ current checkout root. Wired into:

- `run-gcc13-null-gate.sh`
- `run-gcc13-platform-gate.sh`
- `run-clang22-null-gate.sh`
- `run-clang22-sanitize-gate.sh`

## Results (exit 0)

| Gate | JSON | Highlights |
| --- | --- | --- |
| TEST-001 GCC13 Null | `artifacts/gates/test-001-linux-gcc13-null-20260803-tip.json` | `tina_tests` 338, `tina_ui_tests` **589**, `tina_runtime_ui_tests` 115, UI-render 22, `tina_sample_null` 300f |
| TEST-001 GCC13 Platform | `artifacts/gates/test-001-linux-gcc13-platform-20260803-tip.json` | `tina_tests` 338, `tina_platform_glfw_tests` **34/34** (Xvfb), `tina_sample_platform` 60f |
| TEST-001 Clang22 Null | `artifacts/gates/test-001-linux-clang22-null-20260803-tip.json` | same Null suite counts as GCC13 Null |
| TEST-001 Clang22 Sanitize | `artifacts/gates/test-001-linux-clang22-sanitize-20260803-tip.json` | ASan/UBSan/LSan Null suite + sample_null |
| SDK-001 Linux GameSDK consumer | `artifacts/gates/sdk-001-linux-gcc13-consumer-20260803-tip.json` | moved-prefix install → `{"status":"ok","consumer":"installed-tina-sdk"}` |

### Commands

```powershell
# Docker Desktop must be running
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxDockerGate.ps1 `
  -Gate gcc13-null -SkipImageBuild -OutJson artifacts\gates\test-001-linux-gcc13-null-20260803-tip.json
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxDockerGate.ps1 `
  -Gate gcc13-platform -SkipImageBuild -OutJson artifacts\gates\test-001-linux-gcc13-platform-20260803-tip.json
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxDockerGate.ps1 `
  -Gate clang22-null -SkipImageBuild -OutJson artifacts\gates\test-001-linux-clang22-null-20260803-tip.json
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxDockerGate.ps1 `
  -Gate clang22-sanitize -SkipImageBuild -OutJson artifacts\gates\test-001-linux-clang22-sanitize-20260803-tip.json
powershell -ExecutionPolicy Bypass -File .\tools\windows\RunLinuxDockerGate.ps1 `
  -Gate sdk-consumer -SkipImageBuild -OutJson artifacts\gates\sdk-001-linux-gcc13-consumer-20260803-tip.json
```

## Notes

- One non-fatal Clang warning: `UIContextLifetimeControl` forward-declared as `struct` vs `class`
  (Microsoft ABI note; does not fail gate).
- Cross-distro `RunSdkCrossDistroGate.ps1` not re-run in this batch (can follow with existing
  `tina-sdk-consumer-debian13:sdk-001` image).
- JSON under `artifacts/gates/` is gitignored; this doc is the durable tip record.
