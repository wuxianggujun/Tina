#pragma once

#include <tina/core/error/Result.hpp>

#include <cstddef>
#include <span>

namespace Tina::Audio::Detail {

enum class OggAudioCodec { Vorbis, Opus };

// Local, complete single-stream music files only. Reject holes, CRC failures,
// truncation and chained/multiplexed streams rather than silently losing audio.
[[nodiscard]] Core::Result<OggAudioCodec>
validateOggAudio(std::span<const std::byte> encoded) noexcept;

[[nodiscard]] Core::u32 oggPageCrc(std::span<const std::byte> page) noexcept;

} // namespace Tina::Audio::Detail
