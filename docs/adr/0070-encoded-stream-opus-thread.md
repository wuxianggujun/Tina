# ADR 0070：EncodedStream 转 Opus、专用解码线程与分页读

- 状态：Accepted
- 日期：2026-09-16
- 相关：[0012](0012-miniaudio-backend.md)、[0069](0069-audio-clip-residency.md)
- 部分替代：0069 的「owner-thread 解码 / 不转码 / 不磁盘翻页」边界

## 决定

1. EncodedStream cook **一律**把源解码并转成单 logical stream 的 Ogg Opus（48 kHz，mono 64 kbps / stereo 96 kbps）。payload codec 必须是 Opus。v2 wire 不变；非 Opus EncodedStream 拒绝。
2. 码流驻留 catalog mmap，不把整份 bitstream 再拷进 heap。解码走 miniaudio `init_memory` 指向该 span：内置 memory 路径带 tell，按请求窗口拷贝。公开 `ma_decoder_init(onRead,onSeek)` 不传 tell，Vorbis/Opus 后端打不开，因此不用自定义 VFS 当分页。
3. `EncodedPcmStreamer` 使用 **专用 decode thread** 填 PCM 块队列；owner `pump()` 只 `submitPcmStreamFrames`。callback 仍只读 ring。
4. Editor：`--import-audio-stream`、File → Import Stream Audio、命令面板；普通 Import Files 的音频仍为 MemoryPcm。Inspector 显示 Memory PCM / Stream Opus。

Audio importer version 升为 5。

## 被拒绝方案

- 回调里解码或读盘。
- 把解码放进共享 TaskSystem CPU worker：stream 需要持续预填，专用线程寿命与 voice 绑定更清楚。
- 保留 Vorbis/MP3 作为 EncodedStream 发行格式：seek/码率不统一。
