#pragma once

#include <tina/audio/AudioTypes.hpp>
#include <tina/core/error/Result.hpp>

#include <memory_resource>
#include <span>

namespace Tina::Audio {

[[nodiscard]] Core::Status validateAudioEngineConfig(const AudioEngineConfig& config) noexcept;

// Backend-agnostic AudioEngine.
// M11-A7: Disabled lifecycle + generation voices.
// M11-A8: fixed command/completion rings; enqueuePlay/Stop; pumpCompletions
// flushes commands (Disabled applies immediately, no device/PCM) then drains
// completions into the caller's buffer.
// Bus Master/Music/SFX volume+mute are owner-thread state for settings UI.
// miniaudio adapter / EngineHost later.
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

    [[nodiscard]] Core::Result<AudioVoiceId> createVoice() noexcept;
    [[nodiscard]] Core::Status destroyVoice(AudioVoiceId voice) noexcept;
    [[nodiscard]] Core::Result<bool> isVoiceLive(AudioVoiceId voice) const noexcept;
    [[nodiscard]] Core::Result<bool> isVoicePlaying(AudioVoiceId voice) const noexcept;

    // Linear gain [0,1]; non-finite / out-of-range rejected. Mute does not clear volume.
    [[nodiscard]] Core::Status setBusVolume(AudioBusId bus, float volume) noexcept;
    [[nodiscard]] Core::Status setBusMuted(AudioBusId bus, bool muted) noexcept;
    [[nodiscard]] Core::Result<AudioBusState> busState(AudioBusId bus) const noexcept;
    // Master * bus when neither Master nor bus is muted; otherwise 0.
    [[nodiscard]] Core::Result<float> effectiveBusGain(AudioBusId bus) const noexcept;

    // Queue Play/Stop for a live voice. Stale/empty voice fails without enqueue.
    // Full command ring returns CapacityExceeded (Play may be retried next frame).
    [[nodiscard]] Core::Status enqueuePlay(AudioVoiceId voice) noexcept;
    [[nodiscard]] Core::Status enqueueStop(AudioVoiceId voice) noexcept;

    // 1) Apply all pending commands (Disabled path: update voice state + push
    //    completions; no PCM). 2) Drain up to budget completions into out
    //    (budget==0 means all available, capped by out.size()). Returns the
    //    number of completion events written.
    [[nodiscard]] Core::Result<Core::u32> pumpCompletions(
        std::span<AudioCompletionEvent> out, Core::u32 budget = 0) noexcept;

    // Convenience: apply commands and drop drained completions (count only).
    [[nodiscard]] Core::Result<Core::u32> pumpCompletions(Core::u32 budget = 0) noexcept;

    // Idempotent. Transitions Disabled/Enabled -> Stopped and retires voices.
    void shutdown() noexcept;

  private:
    struct Impl;

    explicit AudioEngine(Impl* impl) noexcept;

    Impl* m_impl = nullptr;
};

} // namespace Tina::Audio
