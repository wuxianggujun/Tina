# Audio

Tina 的正式 Audio backend 方向是 miniaudio（ADR 0012）。`tina_audio` 提供 backend-neutral engine，
`tina_audio_miniaudio` 提供可选 device/decode adapter；不引入 SDL_mixer 或第二套公开音频 API。

## 当前实现

`AudioEngine` 当前具备：

- fixed-capacity generation voice registry；
- Master/Music/SFX bus volume 与 mute；
- 有界 Play/Stop command 和 Started/Stopped/Rejected completion；
- non-owning float32 interleaved PCM view；
- `playOneShotPcm()`、`mixRealtime()`、natural-stop 与主线程 `pumpCompletions()`；
- Disabled/Enabled/Stopped 生命周期、stale handle 与重复 shutdown；
- PMR-backed Tina-owned storage 和统计。

`EngineHost` 已支持可选 `AudioEngineFactory`，并在 Fixed/Frame context 暴露 phase-local AudioEngine
borrow。Desktop 默认创建 backend-neutral AudioEngine；miniaudio device 由完整 feature 产品路径显式创建、
attach 和 start。

## miniaudio adapter

| 能力 | 当前状态 |
| --- | --- |
| Device | owner-thread start/stop/shutdown，null backend 或 OS default backend |
| Callback | 调用 `AudioEngine::mixRealtime()`，无分配、无锁等待、无异常/日志 |
| Decode | memory payload → float32 PCM；WAV/FLAC/MP3 使用 miniaudio 内置 decoder |
| Vorbis | feature `audio-miniaudio-vorbis` + `TINA_AUDIO_ENABLE_LIBVORBIS=ON` |
| Opus | feature `audio-miniaudio-opus` + `TINA_AUDIO_ENABLE_LIBOPUS=ON` |

关闭的 codec 返回 `CodecNotEnabled`，损坏/未知 payload 返回 `DecodeFailed`。miniaudio 类型不进入
公开 Tina 头。

## PCM 与 Asset 生命周期

`AudioPcmClipView` 是 non-owning borrow，AudioEngine **不会**自动取得 AssetLease。调用方必须让 PCM
frames 存活到 voice 停止并完成 completion drain。

产品 2D 当前路径为：

```text
recipe WAV
  -> Cooked AudioClip (PCM float32 payload)
  -> Catalog / AssetHandle
  -> AssetLease keeps Cooked bytes alive
  -> parseAudioClipFromCooked
  -> pcmClipViewFromAudioClipPayload
  -> AudioEngine::playOneShotPcm
  -> optional miniaudio callback mix
  -> completion pump
```

这里的强生命周期 owner 是产品 State 保存的 `AssetLease`，不是 AudioEngine 内部 lease。State 关闭时先
停止/关闭 miniaudio device 和 voice，再释放 lease，不能让 callback 读取已卸载 payload。

## 线程与队列

所有公开 voice/bus/command 操作由 owner thread 提交；miniaudio callback 是实时 mixer consumer。

Callback 中禁止：

- heap allocation/free；
- 文件 IO、Asset 查询、Task wait；
- mutex/condition-variable 阻塞；
- 格式化日志、异常传播、UI/Event callback；
- 销毁 Engine、Asset、Scene 或 device owner。

command/completion ring 创建时固定容量。Play/Stop queue 满返回 `CapacityExceeded`，不做无界 fallback；
主线程按 phase pump completion 并重新校验 generation。当前 EngineHost 在 `updateFrame()` 后 pump。

## 关闭顺序

产品 State 先停止产生 Play，并关闭其 miniaudio device；随后 Runtime 调用 State `onExit()`、Application
`onShutdown()`，最后 `EngineModules` 按 AudioEngine → Render → Task → Platform 顺序关闭。

`AudioEngine::shutdown()` 幂等并 retire voice。实时 callback 停止前不得释放 PCM/lease；不得强杀 callback
线程或用提前析构制造 UAF。

## 产品证据

完整 product-2d 报告已证明：

- AudioEngine 存在并成功 queue one-shot；
- Started completion 可观察；
- PCM 来自 Catalog-held AssetLease；
- Cooked clip frame count/sample rate 有效；
- miniaudio null device 创建并产生 callback/mixed frames；
- 300帧 sample exit 0。

null device 只证明 callback/mix/lifecycle，不证明真实扬声器音质、延迟或 OS backend 兼容性。

## 验证

```powershell
cmake --preset windows-msvc-vnext-audio-miniaudio
cmake --build --preset windows-vnext-audio-miniaudio-debug `
  --target tina_audio_tests tina_audio_miniaudio_tests -- /m:1 /v:m
out\build\windows-msvc-vnext-audio-miniaudio\bin\Debug\tina_audio_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-audio-miniaudio\bin\Debug\tina_audio_miniaudio_tests.exe --gtest_color=yes
```

产品接线还需 product-2d 300帧 smoke。测试数量随工作树变化，不在本文固化；完整命令见
[测试说明](testing.md)。

## 尚未完成

- OS 真实扬声器的质量/延迟/设备切换门禁；
- streaming Music/ring buffer、重采样、空间音频、HRTF、DSP graph；
- MP3/Ogg 源文件进入正式 recipe/cooker 的产品策略；
- Audio callback benchmark 纳入 ADR 0018 的统一协议。

这些后置项不影响当前 Cooked PCM one-shot + miniaudio null-device 产品证据。
