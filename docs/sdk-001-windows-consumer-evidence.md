# SDK-001 Windows Consumer Evidence (tip)

Local **moved-prefix** install/consumer gates on this developer machine.
Does **not** replace cross-distro artifact transfer or ADR 0024 ABI approval.

## 2026-08-03

| Field | Value |
| --- | --- |
| Source tip (evidence run start) | `1e52246c30e3a7c0421281875b0eacc1c8541bb3` |
| Host | Windows 10/11, MSVC 19.50, vcpkg |

### Gates run (exit 0)

| Consumer | Build tree | Consumer JSON | Log |
| --- | --- | --- | --- |
| GameSDK | `out/build/windows-msvc-vnext` | `{"status":"ok","consumer":"installed-tina-sdk"}` | `artifacts/gates/sdk-001-gamesdk-consumer-20260803.log` |
| PlatformGlfw | `out/build/windows-msvc-vnext-platform` | `{"status":"ok","consumer":"installed-tina-platform-glfw",...}` | `artifacts/gates/sdk-001-platform-glfw-consumer-20260803.log` |
| DesktopBootstrap | `out/build/windows-msvc-vnext-bgfx` | `{"status":"ok","consumer":"installed-tina-desktop-bootstrap"}` | `artifacts/gates/sdk-001-desktop-bootstrap-consumer-20260803.log` |
| AudioMiniaudio | `out/build/windows-msvc-vnext-audio-miniaudio` | `{"status":"ok","consumer":"installed-tina-audio-miniaudio"}` | `artifacts/gates/sdk-001-audio-miniaudio-consumer-20260803.log` |

### Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkConsumerGate.ps1 `
  -BuildDirectory out\build\windows-msvc-vnext -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkConsumerGate.ps1 -Consumer PlatformGlfw -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkConsumerGate.ps1 -Consumer DesktopBootstrap -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\tools\windows\RunSdkConsumerGate.ps1 -Consumer AudioMiniaudio -Configuration Debug
```

### Proven

- Install headers third-party token scan
- Staging → relocated prefix (source/build path leakage rejected)
- Missing-component / isolation configure probes
- External consumer link + one-frame smoke JSON

## 2026-08-03 Linux Docker consumer (same tip series)

| Consumer | Image | Result |
| --- | --- | --- |
| GameSDK | `tina-linux-gcc13:test-001` | ok → `installed-tina-sdk` (`artifacts/gates/sdk-001-linux-gcc13-consumer-20260803-tip.json`) |

See [docker-tip-evidence-20260803.md](docker-tip-evidence-20260803.md).

### Still open for SDK-001 Done

- Cross-distro `RunSdkCrossDistroGate.ps1` full producer→consumer exit 0
- ADR 0024 formal ABI / compatibility policy acceptance
