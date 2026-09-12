#pragma once

#include <tina/audio/AudioDecode.hpp>
#include <tina/core/error/Result.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace Tina::Asset::Detail {

inline constexpr Core::u64 MaxAudioSourceFileBytes = Audio::AudioDecodeConfig{}.maxEncodedBytes;

// One conversion policy for recipe sources and Editor/direct media imports.
[[nodiscard]] Core::Result<std::vector<std::byte>>
cookAudioClipPayload(std::span<const std::byte> encoded);

} // namespace Tina::Asset::Detail
