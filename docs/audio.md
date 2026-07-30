# Audio

Tina 的正式 Audio backend 方向是 miniaudio（ADR 0012）。`tina_audio` 提供 backend-neutral engine，
`tina_audio_miniaudio` 提供可选 device/decode adapter；不引入 SDL_mixer 或第二套公开音频 API。

## 当前实现

`AudioEngine` 当前具备：

- fixed-capacity generation voice registry；
- Master/Music/SFX bus volume 与 mute；
- 有界 Play/Stop command 和 Started/Stopped/Rejected completion；
- non-owning float32 interleaved PCM view；
- voice gain `[0,1]`、pitch `[0.25,4]`、pan `[-1,1]` 与可取消 fade；
- `playOneShotPcm()`、线性重采样 `mixRealtime()`、natural-stop 与主线程 `pumpCompletions()`；
- one-shot 在显式 Stop 或 natural end 后自动 retire，不占用永久 voice slot；
- `playPcmStream()`、owner-thread 整块原子 submit、EOF drain、underrun 静音计数与 cancel；
- Create 时为每个 voice 固定预分配双声道 stream ring，callback/submit 路径不扩容；
- stream terminal completion 在 completion ring 满或 realtime reader 未退出时延迟但不丢失；
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

## Voice 控制与实时混音

公开 voice 控制为 `setVoiceGain()`、`setVoicePitch()`、`setVoicePan()`、
`startVoiceFade(AudioVoiceFadeDesc)`、`cancelVoiceFade()` 和 `voicePlaybackState()`。范围常量为
`AudioVoiceMin/MaxGain`、`AudioVoiceMin/MaxPitch`、`AudioVoiceMin/MaxPan`：

- gain 必须是有限值 `[0,1]`；
- pitch 必须是有限值 `[0.25,4]`；
- pan 必须是有限值 `[-1,1]`；
- fade target gain 仍为 `[0,1]`，`Core::Duration` 表示正的 rendered output time；
- `AudioFadeEndAction::KeepPlaying` 到达 target 后继续播放，`StopVoice` 到达 target 后走正常
  Stopped completion 与 one-shot retirement。

pitch 使用线性插值，source cursor 每个输出 frame 的步进为：

```text
sourceStep = sourceSampleRate / outputSampleRate * pitch
```

因此 source/output sample rate 不同时也会推进播放，不再以“静音但 cursor 不前进”冒充重采样。
pan 使用兼容既有中心响度的 linear balance，而不是 equal-power pan：

```text
leftPanGain  = pan <= 0 ? 1 : 1 - pan
rightPanGain = pan >= 0 ? 1 : 1 + pan
```

`pan=-1/0/1` 分别得到 left-only、既有 center 双声道响度、right-only；mono 输出忽略 pan。最终采样增益
由 Master/SFX bus、voice gain 与对应 pan gain 相乘，mixer 仍不做隐式 limiter 或 heap fallback。

voice 参数由 owner thread 修改；正在播放时在下一次 realtime callback block 边界可见。fade 也只在 block
边界 start/cancel，避免 callback 中途读取半份控制状态；cancel 停止后续 ramp，并保留 callback 已推进到的
当前 gain。Stop、natural end、destroy 和 shutdown 会清除未完成 fade，不得重复发布 Stopped。

## PCM 与 Asset 生命周期

`AudioPcmClipView` 是 non-owning borrow，AudioEngine **不会**自动取得 AssetLease。调用方必须让 PCM
frames 存活到 voice 停止并完成 completion drain。

`AudioPcmStreamChunkView` 只在 `submitPcmStreamFrames()` 调用期间借用 producer 内存。成功返回前，
完整 chunk 已复制到 Tina-owned 固定 ring；空间不足返回 `CapacityExceeded`，不得发布半块。stream
descriptor 的逻辑容量不得超过 `AudioEngineConfig::streamBufferFrameCapacity`，并至少保留两个 frame，
以保证 fractional linear interpolation 在等待下一个 sample 时仍能让 producer 继续推进。

普通 `createVoice()` 返回的 voice 仍由调用方显式销毁；`playOneShotPcm()` 创建的是 transient voice，
显式 Stop 或 natural end 被 `pumpCompletions()` 收口后自动 retire。Stopped event 中的 one-shot
`AudioVoiceId` 只用于关联完成事件，pump 返回后允许已经 stale，不能再作为长期 owner。

`playPcmStream()` 同样创建 transient voice。EOF 幂等且拒绝后续 submit；callback 排空 ring 后发布唯一
`Stopped`。cancel/Stop 是 absorbing terminal intent，拒绝后续 submit/Play，并分别发布唯一
`Cancelled`/`Stopped`。completion ring 满或旧 callback reader 尚未退出时，terminal debt 保留在 stream
slot，voice 继续可查询，直到后续 pump 成功发布并 retire。

产品 2D 当前路径为：

```text
recipe WAV
  -> Cooked AudioClip (PCM float32 payload)
  -> Catalog / AssetHandle
  -> AssetLease keeps Cooked bytes alive
  -> parseAudioClipFromCooked
  -> pcmClipViewFromAudioClipPayload
  -> AudioEngine::playOneShotPcm
  -> AudioEngine::playPcmStream + submit + EOF
  -> owner-thread fixed-size mixRealtime drain
  -> completion pump
```

这里的强生命周期 owner 是产品 State 保存的 `AssetLease`，不是 AudioEngine 内部 lease。State 关闭时先
停止 voice，并在接入 miniaudio device 时先停止/关闭 device，再释放 lease，不能让 realtime consumer
读取已卸载 payload。

## 线程与队列

所有公开 voice/bus/command/stream producer 操作由 owner thread 提交；Task worker 解码出的 chunk 必须
marshal 回 owner thread，`Tina::Audio` 不提供第二套 Task/miniaudio producer API。miniaudio callback 只是
实时 mixer consumer；同一 AudioEngine 同时只允许一个 non-overlapping `mixRealtime()` consumer。

Callback 中禁止：

- heap allocation/free；
- 文件 IO、Asset 查询、Task wait；
- mutex/condition-variable 阻塞；
- 格式化日志、异常传播、UI/Event callback；
- 销毁 Engine、Asset、Scene 或 device owner。

command/completion ring 创建时固定容量。Play/Stop queue 满返回 `CapacityExceeded`，不做无界 fallback；
主线程按 phase pump completion 并重新校验 generation。当前 EngineHost 在 `updateFrame()` 后 pump。

stream terminal completion 优先偿还既有 debt，不能被持续到来的普通 Started/Rejected completion
无限饿死。非 EOF 空 ring 只输出静音并累加 stream/global underrun；它保持 playing，不能伪造 EOF 或
terminal completion，producer 后续仍可继续 submit。

voice control/fade publication 不在 callback 中分配或加阻塞锁。callback block 是控制状态的提交边界；
owner thread 同一边界前的多次 fade start/cancel 以最后一次已发布状态为准。

## 关闭顺序

产品 State 先停止产生 Play，并关闭其 miniaudio device；随后 Runtime 调用 State `onExit()`、Application
`onShutdown()`，最后 `EngineModules` 按 AudioEngine → Render → Task → Platform 顺序关闭。

`AudioEngine::shutdown()` 幂等：先关闭新的 realtime 进入并等待已进入的 callback block quiesce，再同步
清空有界 command/completion/stream/voice 状态。shutdown 本身不承诺补发尚未 pump 的 terminal event。
销毁 AudioEngine 前仍必须先 stop/detach 外部 device，实时 callback 停止前不得释放 PCM/lease；不得强杀
callback 线程或用提前析构制造 UAF。

## 产品证据

完整 product-2d 报告已证明：

- AudioEngine 存在并成功 queue one-shot；
- Started completion 可观察；
- PCM 来自 Catalog-held AssetLease；
- Cooked clip frame count/sample rate 有效；
- voice gain/pitch/pan 配置可查询，fade start/cancel/stop 状态可观察；
- one-shot Stop/natural end 后自动 retire，不累积 live voice；
- 同一 Catalog-held PCM 已走固定容量 stream 的 submit、EOF、owner-thread 确定性 drain、Stopped 与自动
  retire；
- stream submitted/consumed frame 数一致且产品路径 underrun 为0；
- 300帧 sample exit 0。

产品 sample 的 Advanced Audio 混音证据来自显式固定帧数的 `mixRealtime()`，不依赖异步 device callback
调度。miniaudio callback 调用 mixer 及 device lifecycle 由 `tina_audio_miniaudio_tests` 的 adapter 测试
单独证明；null backend 证据不代表真实扬声器音质、延迟或 OS backend 兼容性。

## 验证

```powershell
cmake --preset windows-msvc-vnext-audio-miniaudio
cmake --build --preset windows-vnext-audio-miniaudio-debug `
  --target tina_audio_tests tina_audio_miniaudio_tests --parallel 1 -- /nr:false
out\build\windows-msvc-vnext-audio-miniaudio\bin\Debug\tina_audio_tests.exe --gtest_color=yes
out\build\windows-msvc-vnext-audio-miniaudio\bin\Debug\tina_audio_miniaudio_tests.exe --gtest_color=yes
```

产品接线还需 product-2d 300帧 smoke。测试数量随工作树变化，不在本文固化；完整命令见
[测试说明](testing.md)。

## 尚未完成

- OS 真实扬声器的质量/延迟/设备切换门禁；
- 高质量 band-limited resampler、空间音频、HRTF、DSP graph；
- MP3/Ogg 源文件进入正式 recipe/cooker 的产品策略；
- Audio callback benchmark 纳入 ADR 0018 的统一协议。

`2D-AUDIO-ADV` 已关闭 A 的 voice control/线性 pitch/pan/fade/one-shot retirement，以及 B 的 bounded
streaming EOF/underrun/cancel/terminal backpressure/shutdown 收口。上述尚未完成项属于后续独立切片。
