# SDK-001 Windows Consumer Evidence (tip)

Local **moved-prefix** install/consumer gates on this developer machine.
Does **not** replace cross-distro artifact transfer or tuple-scoped ABI baselines from ADR 0024.

## 2026-08-16 收口决定

本页的 Windows/Linux consumer、moved-prefix、component isolation 与跨发行版 fresh-consumer 证据继续有效，
但它们证明的是 package/source portability 与实际 tuple 的 artifact transfer，不自动形成旧对象二进制兼容承诺。
[ADR 0024](adr/0024-sdk-abi-compatibility.md) 已 Accepted，选择如下：

- `0.y.z` 的 `y` 是 breaking compatibility epoch；同 epoch 内保持兼容的修复/加法递增 `z`；
- 完整 epoch baseline 与 previous-release source/object probe 建立前，package 使用 strict exact-version
  ConfigVersion；
- 当前只接受三段 `0.0.1`，相邻版本、tweak 与 version range 都必须 incompatible；
- baseline 覆盖完整 tuple/epoch 后，same-minor compatible-patch 协商仍需单独评审，不自动放宽；
- `SameMajorVersion` 被拒绝，因为 major=0 时会跨越不同 `y` epoch。

### Release checklist

1. `TinaGameSdkPackage.cmake` 从 `TinaConfigVersion.cmake.in` 生成 strict exact-version gate，不使用会忽略
   tweak/接受 range lower endpoint 的 CMake stock `ExactVersion` 模板；
2. `VerifyTinaSdkPackageVersion.cmake` 清空每轮 package 输出，分别 include 生成的
   `TinaConfigVersion.cmake`，验证 `0.0.1` exact 与前后版本拒绝；
3. `RunSdkConsumerGate.ps1` 在 installed prefix relocation 前执行该版本正反 probe；
4. moved-prefix、header third-party-token scan、missing-component/isolation、external link/smoke 继续独立执行；
5. 要把具体 release/tuple 标成正式 supported，仍必须补齐 ADR 0024 的 artifact manifest、API/symbol baseline 与
   previous-release object probe；`SDK-001` Done 不等于这些 ABI 进入条件已自动满足。

### 2026-08-16 final DesktopBootstrap gate

`windows-msvc-vnext-bgfx` Debug 的 `RunSdkConsumerGate.ps1 -Consumer DesktopBootstrap` exit 0：producer
增量构建与安装完成，241 个 Tina public headers 通过第三方 token 扫描；version probe 接受三段 `0.0.1`，
拒绝 `0.0.0`、`0.0.2`、`0.0.1.0` tweak 与以 `0.0.1` 为 lower endpoint 的 range；12 个 relocated CMake
package 文件通过绝对路径检查。missing-component 与
GameSDK-only component-isolation probe、外部 consumer configure/build/run 均 exit 0，最终输出：

```json
{"status":"ok","consumer":"installed-tina-desktop-bootstrap"}
```

Gate 生成的 relocated prefix、consumer build、missing-component build 与 component-isolation build 共
290,093,316 bytes；取证后五个临时路径均核验为不存在/0 bytes，常驻 build tree 保留。

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

## 2026-08-03 Linux cross-distro (Ubuntu → Debian)

`RunSdkCrossDistroGate.ps1` tip exit 0：producer GCC13 Release GameSDK archive → Debian 13/GCC14
consumer `installed-tina-sdk`（path-remapped static libs；详见
[docker-tip-evidence-20260803.md](docker-tip-evidence-20260803.md)）。

### SDK-001 收口边界

ADR 0024 已接受且 version-selection 正反 probe 已纳入 Windows consumer gate。`SDK-001` 的 package/consumer
交付范围据此关闭；正式 ABI supported tuple 继续按 ADR 0024 的 release checklist 跟踪，不回写成 SDK-001 已证明。
