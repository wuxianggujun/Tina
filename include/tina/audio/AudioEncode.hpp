#pragma once

#include <tina/audio/AudioDecode.hpp>
#include <tina/core/error/Result.hpp>

#include <span>
#include <vector>

namespace Tina::Audio {

struct OpusEncodeConfig final {
    // 0 selects 64000 bps mono or 96000 bps stereo.
    Core::u32 bitrateBps = 0;
};

// Encodes interleaved float32 PCM as a single-stream Ogg Opus file. Resamples
// to 48000 Hz. Channels must be 1 or 2. Cooker/worker/owner thread only.
[[nodiscard]] Core::Result<std::vector<std::byte>> encodeOpusOgg(
    std::span<const float> interleavedPcm, Core::u32 channels, Core::u32 sampleRate,
    OpusEncodeConfig config = {}) noexcept;

// Streams PCM out of an open decoder and muxes Ogg Opus without retaining the
// whole PCM buffer. The decoder is left at EOF.
[[nodiscard]] Core::Result<std::vector<std::byte>> encodeOpusOggFromDecoder(
    AudioDecoder& decoder, OpusEncodeConfig config = {}) noexcept;

} // namespace Tina::Audio
