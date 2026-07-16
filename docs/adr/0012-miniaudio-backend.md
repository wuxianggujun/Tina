# ADR 0012：miniaudio 是唯一真实音频后端

- 状态：Accepted
- 日期：2026-07-16

## 背景

Tina 已迁移到 miniaudio。再保留 SDL_mixer 或自建第二套设备层，会重复设备、解码、线程、
音量和退出语义，同时 SDL 本身也不属于目标平台方案。

## 决定

`tina_audio_miniaudio` 使用 miniaudio 作为唯一真实 backend，第三方类型只在该 adapter。
后端无关 `tina_audio` 提供 AudioEngine、Disabled backend、Tina 的 Bus、generation VoiceId、
命令与完成协议；实时 callback 禁止分配、阻塞锁、文件 IO、
日志格式化和异常。Headless 使用显式 DisabledAudio，不把真实设备失败静默当成功。

## 结果

- 音频设备与资源释放只有一套状态机；
- AssetLease、callback ACK 和有界队列必须共同保证物理寿命；
- 复杂 DSP/HRTF/voice virtualization 后置；
- 新 backend 只有在 miniaudio 无法满足已量化需求时才通过 ADR 评估。

## 被拒绝方案

- 同时保留 SDL_mixer：职责重复且重新引入 SDL 家族依赖；
- 首期自研音频设备/解码层：成本高且没有产品收益证据。
