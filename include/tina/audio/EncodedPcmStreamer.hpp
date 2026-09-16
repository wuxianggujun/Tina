#pragma once

#include <tina/audio/AudioDecode.hpp>
#include <tina/audio/AudioEngine.hpp>
#include <tina/core/error/Result.hpp>

#include <memory>
#include <span>
#include <thread>

namespace Tina::Audio {

struct EncodedPcmStreamDesc final {
    AudioPlayDesc play{};
    AudioBusId bus = AudioBusId::Music;
    Core::usize bufferCapacityFrames = 16384;
    Core::usize decodeChunkFrames = 2048;
    Core::u64 sourceFrameCount = 0;
};

// Dedicated decode thread pages encoded bytes into PCM chunks. Owner-thread
// pump() only submits those chunks into the mixer ring. mixRealtime never
// decodes or reads the bitstream.
class EncodedPcmStreamer final {
public:
    EncodedPcmStreamer() noexcept;
    EncodedPcmStreamer(EncodedPcmStreamer&&) noexcept;
    EncodedPcmStreamer& operator=(EncodedPcmStreamer&&) noexcept;
    ~EncodedPcmStreamer();
    EncodedPcmStreamer(const EncodedPcmStreamer&) = delete;
    EncodedPcmStreamer& operator=(const EncodedPcmStreamer&) = delete;

    [[nodiscard]] static Core::Result<EncodedPcmStreamer> Start(
        AudioEngine& engine, std::span<const std::byte> encoded,
        EncodedPcmStreamDesc desc = {}) noexcept;

    [[nodiscard]] AudioVoiceId voice() const noexcept { return m_voice; }
    [[nodiscard]] bool finished() const noexcept;

    [[nodiscard]] Core::Status pump() noexcept;
    [[nodiscard]] Core::Status cancel() noexcept;

private:
    struct Shared;
    static void decodeLoop(const std::shared_ptr<Shared>& shared);

    AudioEngine* m_engine = nullptr;
    AudioVoiceId m_voice{};
    std::shared_ptr<Shared> m_shared{};
    std::thread m_thread{};

    void stopDecodeThread() noexcept;
};
} // namespace Tina::Audio
