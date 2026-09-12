# ADR 0061：基础音频解码与可选设备分离

- 状态：Accepted
- 日期：2026-09-12
- 相关：[0012](0012-miniaudio-backend.md)、[0024](0024-sdk-abi-compatibility.md)、[0055](0055-single-runtime-archive.md)

## 背景

正式 recipe/MediaCook 只接受 PCM16 WAV，Editor 文件过滤同样只列 WAV。旧 miniaudio adapter 虽声明可选
Vorbis/Opus，但没有注册 miniaudio 0.11.25 的 custom decoding backend；宏和库链接不等于格式可用。
离线导入还被错误绑定到声卡 feature，导致无设备 SDK 无法完成常见音乐资源 cook。

## 决定

1. WAV、FLAC、MP3、Ogg Vorbis/Opus 是基础 Audio 能力，设备仍由 `TINA_BUILD_AUDIO_MINIAUDIO` 独立控制。
2. 使用固定版本 miniaudio 及其显式注册的 libvorbis/libopus 后端；全部第三方类型与依赖留在 PRIVATE 边界。
3. 所有入口共用 bounded decoder → float32 PCM AudioClip 的单一路径；默认 encoded 64 MiB、PCM 256 MiB，
   不在 callback 中读文件或解码。保留原采样率/mono/stereo，支持显式格式转换与 surround 下混。
4. Ogg 完整单 logical stream 必须通过 page CRC/序号/continuation/EOS 校验；损坏输入不发布部分 PCM。
   当前不接受 chained/multiplexed Ogg，不把有损码流的所有损坏都声称为可检测。
5. 解码结果使用 move-only RAII owner，借用至 terminal completion；删除旧 manual-free API 与 codec switches，
   SDK epoch 升为 **0.3.0**。现有 AudioClip v1 wire 不变；两个受影响 importer version 升为 3。
6. Windows 全功能 SDK 安装提供 Debug/Release 预编译单库、已有可组合后端和依赖，不让游戏工程重新构建引擎。
   Editor 仍为独立产品，不进入 `Tina::GameSDK`。

## 代价与边界

基础 SDK 增加 Vorbis/Opus 私有链接依赖；Null 仍不需要声卡或窗口。离线完整解码的 PCM 大于压缩源文件，
内存预算是明确限制，不提供隐式无界回退。压缩磁盘流式音乐、OS 设备质量/切换、HRTF/DSP 为后续独立能力。

## 验证

真实生成的五格式 fixture、损坏/截断/预算/格式转换、recipe/MediaCook 同源输出、Catalog/Lease/播放生命周期、
Editor 入口与 installed SDK consumer；实际运行结果单独记录，不用 capability flag 代替解码证据。
