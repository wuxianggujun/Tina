# ADR 0069：AudioClip MemoryPcm / EncodedStream 驻留

- 状态：Accepted（owner-thread 解码 / 不转码 / 不翻页由 [0070](0070-encoded-stream-opus-thread.md) 部分替代）
- 日期：2026-09-15
- 相关：[0012](0012-miniaudio-backend.md)、[0061](0061-audio-source-decoding.md)
- 部分替代：0061 的「catalog 一律离线完整 float32 PCM」

## 背景

ADR 0061 把五种源格式统一 cook 成 AudioClip v1 整段 float32 PCM，混音回调只读这份 PCM。短 SFX
正确，长音乐把约 400 KiB 的 ogg 写成约 22.5 MiB/分钟的 PCM，发行体积和 RAM 一起膨胀。压缩流式
被标成后续能力。声卡回调禁令（ADR 0012）仍然成立：不能在 callback 里解码、读盘、加锁或失败后
干净地 fail closed。

## 决定

1. AudioClip **schema v2** 增加显式 `storage`：`MemoryPcm`（payload 为 interleaved float32）与
   `EncodedStream`（payload 为已校验源码流）。仍是同一个 `AssetKind::AudioClip`。v1 直接拒绝。
2. recipe / CMake / `tina_assetc` / MediaCook 必须写明驻留。缺省为 `MemoryPcm`。Memory 解码 PCM
   超过 **16 MiB** 失败，迫使长音频走 `EncodedStream`。
3. Cooker 对 EncodedStream 仍整文件校验（有限样本、长度、Ogg CRC）并证明精确 seek，**不**把 PCM
   写进 catalog。无法精确 seek 的源（常见 VBR MP3）拒绝 stream cook。
4. Runtime：MemoryPcm 走既有 `playPcm`；EncodedStream 由 owner-thread `EncodedPcmStreamer` 增量
   解码并 `submitPcmStreamFrames`。callback 仍然只读 PCM ring。对 EncodedStream 调 `playPcm`
   返回 `InvalidConfiguration`。
5. 默认 `streamBufferFrameCapacity` 为 **16384** 帧，给 pitch≤4 的 stream 留水位。Audio importer
   version 升为 4；SDK epoch **0.5.0**。

## 代价与边界

作者必须选档。Stream 有预填延迟、运行时解码 CPU、每路独立 decoder，不能当多实例共享 SFX。
第一刀在 owner thread 解码，不引入 TaskSystem，也不是磁盘翻页。不静默转码成 Opus。视频不在范围。

## 被拒绝方案

- 回调里解 ogg/mp3：违反 ADR 0012。
- 所有音频改 stream：短音效需要零延迟和多 voice 共享。
- 加载时仍整段解进 RAM：只修盘不修常驻。
- 第二套 AssetKind / 新节点：AudioPlayer2D 已绑定 AudioClip。
- 按时长猜测驻留：隐式启发式。
- 保留 v1 双读：单轨，旧 catalog 必须重 cook。
