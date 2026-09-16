#pragma once

#include <tina/asset_format/AudioClipPayload.hpp>
#include <tina/audio/AudioDecode.hpp>
#include <tina/core/error/Result.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace Tina::Asset::Detail {

inline constexpr Core::u64 MaxAudioSourceFileBytes = Audio::AudioDecodeConfig{}.maxEncodedBytes;

[[nodiscard]] constexpr AssetFormat::AudioClipCodec audioClipCodecFromSource(
    Audio::AudioSourceCodec codec) noexcept
{
    switch (codec)
    {
    case Audio::AudioSourceCodec::Wav:
        return AssetFormat::AudioClipCodec::Wav;
    case Audio::AudioSourceCodec::Flac:
        return AssetFormat::AudioClipCodec::Flac;
    case Audio::AudioSourceCodec::Mp3:
        return AssetFormat::AudioClipCodec::Mp3;
    case Audio::AudioSourceCodec::Vorbis:
        return AssetFormat::AudioClipCodec::Vorbis;
    case Audio::AudioSourceCodec::Opus:
        return AssetFormat::AudioClipCodec::Opus;
    }
    return AssetFormat::AudioClipCodec::PcmF32;
}

[[nodiscard]] Core::Result<std::vector<std::byte>>
cookAudioClipPayload(std::span<const std::byte> encoded,
                     AssetFormat::AudioClipStorage storage = AssetFormat::AudioClipStorage::MemoryPcm);

} // namespace Tina::Asset::Detail
