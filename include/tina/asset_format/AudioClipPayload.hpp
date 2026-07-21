#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <span>
#include <vector>

namespace Tina::AssetFormat {

// AudioClip cooked payload schema v1 (little-endian, after CookedAsset header/deps).
// Layout (16B header + interleaved float32 PCM):
//   u16 schemaVersion (=1)
//   u16 channels      (1..8)
//   u32 sampleRate    (Hz)
//   u32 frameCount    (samples per channel)
//   u32 reserved (=0)
//   f32 pcm[frameCount * channels]  // interleaved
// Product path: decode offline (cooker / IO-CPU stage) into this payload; Runtime
// audio callback only reads prepared PCM (docs/audio.md).
namespace AudioClipWire {
inline constexpr Core::u16 SchemaVersion = 1;
inline constexpr Core::u32 HeaderBytes = 16;
inline constexpr Core::u16 MaxChannels = 8;
inline constexpr Core::u32 MinSampleRate = 1000;
inline constexpr Core::u32 MaxSampleRate = 192000;
inline constexpr Core::u32 MaxFrameCount = 48'000'000; // ~1000s mono @ 48k
} // namespace AudioClipWire

struct AudioClipPayloadDesc final {
    Core::u16 channels = 1;
    Core::u32 sampleRate = 48000;
    Core::u32 frameCount = 0;
    std::span<const float> interleavedPcm{};
};

struct AudioClipPayloadView final {
    Core::u16 schemaVersion = 0;
    Core::u16 channels = 0;
    Core::u32 sampleRate = 0;
    Core::u32 frameCount = 0;
    std::span<const float> interleavedPcm{};

    [[nodiscard]] bool empty() const noexcept
    {
        return interleavedPcm.empty() || frameCount == 0 || channels == 0 || sampleRate == 0;
    }
};

[[nodiscard]] Core::Result<std::vector<std::byte>> writeAudioClipPayloadBytes(const AudioClipPayloadDesc& desc);

// Borrows payload bytes from a CookedAssetView / raw payload span.
[[nodiscard]] Core::Result<AudioClipPayloadView> parseAudioClipPayload(std::span<const std::byte> payload);

// Convenience: full cooked AudioClip asset file (no dependencies).
[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedAudioClipAsset(Core::AssetId assetId, const AudioClipPayloadDesc& desc,
                          TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
