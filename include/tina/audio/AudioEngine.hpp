#pragma once

#include <tina/audio/AudioTypes.hpp>
#include <tina/core/error/Result.hpp>

#include <memory_resource>

namespace Tina::Audio {

[[nodiscard]] Core::Status validateAudioEngineConfig(const AudioEngineConfig& config) noexcept;

// Backend-agnostic AudioEngine lifecycle foundation (M11-A7).
// A7 always creates a Disabled engine (no device, no miniaudio). Generation
// voice slots prove ownership/stale semantics before real command queues land.
// EngineHost ownership and miniaudio adapter are later slices.
class AudioEngine final {
  public:
    [[nodiscard]] static Core::Result<AudioEngine> Create(
        AudioEngineConfig config = {},
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    ~AudioEngine() noexcept;

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&& other) noexcept;
    AudioEngine& operator=(AudioEngine&&) = delete;

    [[nodiscard]] AudioEngineState state() const noexcept;
    [[nodiscard]] Core::Result<AudioEngineStats> stats() const noexcept;

    // Allocate a generation voice slot. A7 does not play audio; later slices
    // bind clips/leases to live voices.
    [[nodiscard]] Core::Result<AudioVoiceId> createVoice() noexcept;
    [[nodiscard]] Core::Status destroyVoice(AudioVoiceId voice) noexcept;
    [[nodiscard]] Core::Result<bool> isVoiceLive(AudioVoiceId voice) const noexcept;

    // A7 has no completion traffic; always returns 0 when open.
    [[nodiscard]] Core::Result<Core::u32> pumpCompletions(Core::u32 budget = 0) noexcept;

    // Idempotent. Transitions Disabled/Enabled -> Stopped and retires voices.
    void shutdown() noexcept;

  private:
    struct Impl;

    explicit AudioEngine(Impl* impl) noexcept;

    Impl* m_impl = nullptr;
};

} // namespace Tina::Audio
