#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <span>

namespace Tina::Audio {

// Build-time codec matrix. Built-in WAV/FLAC/MP3 are always on when the
// miniaudio adapter is linked. Ogg Vorbis / Opus are optional vcpkg features
// (audio-miniaudio-vorbis / audio-miniaudio-opus) so product graphs can stay
// small while integrators may enable extra formats without forking Tina.
struct AudioDecodeCapabilities final {
    bool wav = true;
    bool flac = true;
    bool mp3 = true;
    bool oggVorbis = false;
    bool opus = false;
};

// Interleaved float32 PCM owned by the decoder call (free with freeDecodedPcm).
struct DecodedPcmBuffer final {
    float* frames = nullptr; // frameCount * channels interleaved
    Core::u64 frameCount = 0;
    Core::u32 channels = 0;
    Core::u32 sampleRate = 0;

    [[nodiscard]] bool empty() const noexcept
    {
        return frames == nullptr || frameCount == 0 || channels == 0;
    }
};

[[nodiscard]] AudioDecodeCapabilities queryAudioDecodeCapabilities() noexcept;

// Decode encoded bytes into float32 PCM via the private miniaudio adapter.
// Owner-thread / worker-thread OK; not real-time safe (may allocate).
// Returns CodecNotEnabled for Ogg/Opus when the corresponding option is OFF.
// Returns DecodeFailed when miniaudio cannot parse the payload.
[[nodiscard]] Core::Result<DecodedPcmBuffer> decodeAudioMemory(
    std::span<const std::byte> encoded) noexcept;

// Releases PCM allocated by decodeAudioMemory. No-op for empty buffers.
void freeDecodedPcm(DecodedPcmBuffer& buffer) noexcept;

} // namespace Tina::Audio
