#pragma once

#include <tina/audio/AudioTypes.hpp>
#include <tina/core/error/Result.hpp>

#include <memory_resource>
#include <span>

namespace Tina::Audio {

[[nodiscard]] Core::Status validateAudioEngineConfig(const AudioEngineConfig& config) noexcept;

// Backend-agnostic AudioEngine.
// M11-A7: Disabled lifecycle + generation voices.
// M11-A8: fixed command/completion rings; enqueuePlay/Stop; pumpCompletions.
// Bus Master/Music/SFX volume+mute are owner-thread state for settings UI.
// M11-A11: non-owning PCM clip bind per voice; Play without clip -> RejectedNoClip.
// M11-A12: realtime mixRealtime (atomic mix slots) for miniaudio dataCallback.
// M11-A13: playOneShotPcm convenience (create+bind+enqueuePlay).
// AssetLease / EngineHost later.
class AudioEngine final {
  public:
    [[nodiscard]] static Core::Result<AudioEngine> Create(
        AudioEngineConfig config = {},
        std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

    ~AudioEngine() noexcept;

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&& other) noexcept;
    AudioEngine& operator=(AudioEngine&& other) noexcept;

    [[nodiscard]] AudioEngineState state() const noexcept;
    [[nodiscard]] Core::Result<AudioEngineStats> stats() const noexcept;

    [[nodiscard]] Core::Result<AudioVoiceId> createVoice() noexcept;
    [[nodiscard]] Core::Status destroyVoice(AudioVoiceId voice) noexcept;
    [[nodiscard]] Core::Result<bool> isVoiceLive(AudioVoiceId voice) const noexcept;
    [[nodiscard]] Core::Result<bool> isVoicePlaying(AudioVoiceId voice) const noexcept;

    // Bind/clear non-owning PCM for a live voice. Invalid/empty clip rejected.
    // Caller keeps frames valid until Stop completion (or clear while not playing).
    [[nodiscard]] Core::Status bindVoiceClip(AudioVoiceId voice, AudioPcmClipView clip) noexcept;
    [[nodiscard]] Core::Status clearVoiceClip(AudioVoiceId voice) noexcept;
    [[nodiscard]] Core::Result<AudioPcmClipView> voiceClip(AudioVoiceId voice) const noexcept;

    // Linear gain [0,1]; non-finite / out-of-range rejected. Mute does not clear volume.
    [[nodiscard]] Core::Status setBusVolume(AudioBusId bus, float volume) noexcept;
    [[nodiscard]] Core::Status setBusMuted(AudioBusId bus, bool muted) noexcept;
    [[nodiscard]] Core::Result<AudioBusState> busState(AudioBusId bus) const noexcept;
    // Master * bus when neither Master nor bus is muted; otherwise 0.
    [[nodiscard]] Core::Result<float> effectiveBusGain(AudioBusId bus) const noexcept;

    // Queue Play/Stop for a live voice. Stale/empty voice fails without enqueue.
    // Play with no bound clip still enqueues; apply yields RejectedNoClip (not Started).
    // Full command ring returns CapacityExceeded (Play may be retried next frame).
    [[nodiscard]] Core::Status enqueuePlay(AudioVoiceId voice) noexcept;
    [[nodiscard]] Core::Status enqueueStop(AudioVoiceId voice) noexcept;

    // createVoice + bindVoiceClip + enqueuePlay. Does not pump; call pumpCompletions
    // next to apply and receive Started. Frames must outlive Stop completion.
    [[nodiscard]] Core::Result<AudioVoiceId> playOneShotPcm(AudioPcmClipView clip) noexcept;

    // Real-time safe mixer: zeros out, sums active voice clips (float32), advances
    // cursors. Natural end deactivates the mix slot; pumpCompletions emits Stopped.
    // No allocation / no owner-thread requirement. outChannels must be 1 or 2.
    void mixRealtime(float* interleavedOut, Core::u32 outFrames, Core::u32 outChannels,
                     Core::u32 outSampleRate) noexcept;

    // 1) Apply pending commands. 2) Convert natural-end flags to Stopped. 3) Drain.
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
