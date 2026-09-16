#pragma once

#include <tina/audio/AudioTypes.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <array>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace Tina::Audio {

// Source decoding is part of the base SDK, independent of an audio device.
struct AudioDecodeCapabilities final {
    bool wav = true;
    bool flac = true;
    bool mp3 = true;
    bool oggVorbis = true;
    bool opus = true;
};

inline constexpr std::array<std::string_view, 6> AudioSourceExtensions{
    ".wav", ".flac", ".mp3", ".ogg", ".oga", ".opus"};

// Takes an extension including '.', not a path. ASCII case-insensitive.
// This is an ingress filter only; decodeAudioMemory validates the actual bytes.
[[nodiscard]] bool isSupportedAudioSourceExtension(std::string_view extension) noexcept;

enum class AudioSourceCodec : Core::u16 {
    Wav = 1,
    Flac = 2,
    Mp3 = 3,
    Vorbis = 4,
    Opus = 5,
};

struct AudioDecodeConfig final {
    Core::u64 maxEncodedBytes = 64ULL * 1024ULL * 1024ULL;
    Core::u64 maxDecodedBytes = 256ULL * 1024ULL * 1024ULL;
    // 0 preserves mono/stereo, downmixing surround to stereo. Explicit 1/2
    // converts to mono/stereo. Samples use the decoder's source channel map.
    Core::u32 outputChannels = 0;
    // 0 preserves the source rate (Opus always decodes at 48000 Hz).
    Core::u32 outputSampleRate = 0;
};

// Move-only PCM owner. Any view expires when this owner is destroyed/replaced.
// A playing voice borrows the view until its terminal completion is pumped.
class DecodedPcmBuffer final {
public:
    DecodedPcmBuffer() noexcept = default;
    DecodedPcmBuffer(DecodedPcmBuffer&&) noexcept = default;
    DecodedPcmBuffer& operator=(DecodedPcmBuffer&&) noexcept = default;
    DecodedPcmBuffer(const DecodedPcmBuffer&) = delete;
    DecodedPcmBuffer& operator=(const DecodedPcmBuffer&) = delete;
    ~DecodedPcmBuffer() = default;

    [[nodiscard]] bool empty() const noexcept { return m_pcm.empty(); }
    [[nodiscard]] Core::u32 channels() const noexcept { return m_channels; }
    [[nodiscard]] Core::u32 sampleRate() const noexcept { return m_sampleRate; }
    [[nodiscard]] Core::u64 frameCount() const noexcept
    {
        return m_channels == 0 ? 0 : m_pcm.size() / m_channels;
    }
    [[nodiscard]] std::span<const float> interleavedPcm() const noexcept { return m_pcm; }
    [[nodiscard]] AudioPcmClipView clipView() const noexcept
    {
        return {m_pcm.data(), frameCount(), m_channels, m_sampleRate};
    }

private:
    friend Core::Result<DecodedPcmBuffer> decodeAudioMemory(
        std::span<const std::byte>, AudioDecodeConfig) noexcept;
    DecodedPcmBuffer(std::vector<float> pcm, Core::u32 channels, Core::u32 sampleRate) noexcept
        : m_pcm(std::move(pcm)), m_channels(channels), m_sampleRate(sampleRate) {}

    std::vector<float> m_pcm;
    Core::u32 m_channels = 0;
    Core::u32 m_sampleRate = 0;
};

// Incremental decoder. Borrows `encoded` for its whole life. Owner/worker thread
// only; never mixRealtime. Seek is exact or the call fails; there is no
// approximate loop.
class AudioDecoder final {
public:
    AudioDecoder() noexcept;
    AudioDecoder(AudioDecoder&& other) noexcept;
    AudioDecoder& operator=(AudioDecoder&& other) noexcept;
    ~AudioDecoder();
    AudioDecoder(const AudioDecoder&) = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;

    [[nodiscard]] static Core::Result<AudioDecoder> open(
        std::span<const std::byte> encoded, AudioDecodeConfig config = {}) noexcept;

    [[nodiscard]] bool empty() const noexcept { return m_impl == nullptr; }
    [[nodiscard]] Core::u32 channels() const noexcept;
    [[nodiscard]] Core::u32 sampleRate() const noexcept;
    // Backend-reported length. 0 when the codec does not know until drain.
    [[nodiscard]] Core::u64 frameCount() const noexcept;
    [[nodiscard]] AudioSourceCodec codec() const noexcept;
    [[nodiscard]] Core::u64 cursorFrame() const noexcept;

    // Returns frames written. 0 at EOF. `interleavedOut.size()` must be a
    // multiple of channels().
    [[nodiscard]] Core::Result<Core::u64> readPcm(std::span<float> interleavedOut) noexcept;
    [[nodiscard]] Core::Status seekFrame(Core::u64 frame) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

[[nodiscard]] AudioDecodeCapabilities queryAudioDecodeCapabilities() noexcept;

// Decode WAV (integer/float), FLAC, MP3, Ogg Vorbis or Ogg Opus into finite
// interleaved float32 PCM. Borrows encoded only during the call. May allocate
// and perform CPU work: cooker/worker/owner-thread only, never a mixer callback.
// Limits cover input bytes and output PCM capacity, not codec-internal scratch.
// Invalid/truncated data -> DecodeFailed; unknown format -> NotSupported;
// a byte budget violation -> DecodeLimitExceeded. No partial PCM is published.
[[nodiscard]] Core::Result<DecodedPcmBuffer> decodeAudioMemory(
    std::span<const std::byte> encoded, AudioDecodeConfig config = {}) noexcept;

} // namespace Tina::Audio
