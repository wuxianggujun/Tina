# M12 Windows 证据摘录（不授权删除）

记录本机 Windows MSVC Debug 复验命令与结果。生成文件指纹会随帧内容变化，以 exit 0 + 结构化 JSON 为准。

## 环境

- 分支 tip：见 `git log -1`（录入时为 `155431d8` 及之后的 docs 合并 tip）
- Build tree：`out/build/windows-msvc-vnext-bgfx`（**未** `--clean-first`）
- 日期：2026-07-22

## G1 `tina_sample_2d`（300 帧）

```powershell
cmake --build --preset windows-vnext-bgfx-debug --target tina_sample_2d -- /m:2 /v:m
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_2d.exe --frames=300 --frame-delay-ms=0
```

结果：**exit 0**

关键 JSON 字段（摘录）：

- `status=ok`，`frames=300`，`renderExtractions=300`
- `catalogFromRecipeFile=true`，`catalogRecipeAssets=4`
- `tileSpritesPerFrame=11`，`controllerHitRightFrames=214`
- `audioEnginePresent=true`，`audioOneShotQueued=true`，`audioStartedObserved=true`
- `audioFromCatalogLease=true`，`audioClipSampleRate=48000`
- `productGate=bgfx`（本树未开 physics/freetype 全 feature 时的门禁标签）
- `pixelCaptureOk=true`，`stateExits=1`，`uiRootsReleased=1`

说明：`--frames=30` 会因行走/撞墙阈值未跑满而 `verification failed`（exit 1），**不是**崩溃；正式门禁用 300 帧。

## G3/G5 旁证（同进程）

同一次 `tina_sample_2d` 300 帧已覆盖 cooked AudioClip lease + one-shot 播放观测，可计入 **G5 部分证据**（非独立 miniaudio 扬声器门禁）。

## G3 E9 `tina_sample_3d`（30 帧 smoke；可扩 300）

```powershell
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_sample_3d.exe --frames=30 --frame-delay-ms=0
```

结果：**exit 0**，JSON 含 `gltfCooked`/`prefabInstantiated`/`sceneExtract`/`renderResourceLedgerBalanced`。

## `tina_audio_tests`

```powershell
out\build\windows-msvc-vnext-bgfx\bin\Debug\tina_audio_tests.exe
```

结果：**15/15 PASS**（backend-neutral engine 路径）。

## 仍未关闭

- Linux 全门禁
- Legacy 四条 product smoke 最终基线包
- 零引用扫描 + `TINA_BUILD_LEGACY` 默认 OFF → 删除
- product-2d 全 feature 图（physics+freetype+miniaudio device）若与当前 `productGate=bgfx` 不同，需另树复验
