#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <span>
#include <vector>

namespace Tina::AssetFormat {

enum class AudioClipStorage : Core::u16 {
    MemoryPcm = 0,
    EncodedStream = 1,
};

enum class AudioClipCodec : Core::u16 {
    PcmF32 = 0,
    Wav = 1,
    Flac = 2,
    Mp3 = 3,
    Vorbis = 4,
    Opus = 5,
};

// AudioClip cooked payload schema v2 (little-endian, after CookedAsset header/deps).
// Layout (24B header + payload):
//   u16 schemaVersion (=2)
//   u16 channels
//   u32 sampleRate
//   u32 frameCount          // decoded PCM frames
//   u16 storage             // MemoryPcm | EncodedStream
//   u16 codec
//   u32 encodedBytes        // 0 for MemoryPcm
//   u32 reserved (=0)
//   MemoryPcm:     f32 pcm[frameCount * channels]
//   EncodedStream: encoded[encodedBytes]
namespace AudioClipWire {
inline constexpr Core::u16 SchemaVersion = 2;
inline constexpr Core::u32 HeaderBytes = 24;
inline constexpr Core::u16 MaxChannels = 8;
inline constexpr Core::u32 MinSampleRate = 1000;
inline constexpr Core::u32 MaxSampleRate = 192000;
inline constexpr Core::u32 MaxFrameCount = 48'000'000;
inline constexpr Core::u64 MaxMemoryPcmBytes = 16ULL * 1024ULL * 1024ULL;
} // namespace AudioClipWire

struct AudioClipPayloadDesc final {
    Core::u16 channels = 1;
    Core::u32 sampleRate = 48000;
    Core::u32 frameCount = 0;
    AudioClipStorage storage = AudioClipStorage::MemoryPcm;
    AudioClipCodec codec = AudioClipCodec::PcmF32;
    std::span<const float> interleavedPcm{};
    std::span<const std::byte> encoded{};
};

struct AudioClipPayloadView final {
    Core::u16 schemaVersion = 0;
    Core::u16 channels = 0;
    Core::u32 sampleRate = 0;
    Core::u32 frameCount = 0;
    AudioClipStorage storage = AudioClipStorage::MemoryPcm;
    AudioClipCodec codec = AudioClipCodec::PcmF32;
    std::span<const float> interleavedPcm{};
    std::span<const std::byte> encoded{};

    [[nodiscard]] bool empty() const noexcept
    {
        if (channels == 0 || sampleRate == 0 || frameCount == 0) { return true; }
        if (storage == AudioClipStorage::EncodedStream) { return encoded.empty(); }
        return interleavedPcm.empty();
    }
};

[[nodiscard]] Core::Result<std::vector<std::byte>> writeAudioClipPayloadBytes(const AudioClipPayloadDesc& desc);

[[nodiscard]] Core::Result<AudioClipPayloadView> parseAudioClipPayload(std::span<const std::byte> payload);

[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedAudioClipAsset(Core::AssetId assetId, const AudioClipPayloadDesc& desc,
                          TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
