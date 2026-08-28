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
    // Only for a voice that is not playing and has no completion still pending.
    // Every voice kind must reach its end through the queue -- enqueueStop for clips,
    // EOF/cancelPcmStream/enqueueStop for streams -- because erasing the record is
    // what would destroy the caller's only notification that the realtime callback
    // released its PCM. Otherwise returns InvalidConfiguration and changes nothing.
    [[nodiscard]] Core::Status destroyVoice(AudioVoiceId voice) noexcept;
    [[nodiscard]] Core::Result<bool> isVoiceLive(AudioVoiceId voice) const noexcept;
    [[nodiscard]] Core::Result<bool> isVoicePlaying(AudioVoiceId voice) const noexcept;

    // Bind/clear non-owning PCM for a live voice. Invalid/empty clip rejected.
    // Caller keeps frames valid until the terminal completion for that playback is
    // pumped. Both calls fail while the voice is playing AND while a terminal is
    // queued but not yet pumped: in that window the mixer may still be reading the
    // frames, so a success here would be a false release signal. Pump completions
    // and retry; the deferral is bounded by the realtime callback block.
    [[nodiscard]] Core::Status bindVoiceClip(AudioVoiceId voice, AudioPcmClipView clip) noexcept;
    [[nodiscard]] Core::Status clearVoiceClip(AudioVoiceId voice) noexcept;
    [[nodiscard]] Core::Result<AudioPcmClipView> voiceClip(AudioVoiceId voice) const noexcept;

    // Owner-thread voice controls. gain [0,1], pitch [0.25,4], pan [-1,1].
    // Invalid/stale handles and non-finite/out-of-range values fail atomically.
    // Changes to a playing voice are observed at the next realtime mix block.
    // setVoiceGain cancels an active fade. startVoiceFade requires active playback;
    // cancelVoiceFade is idempotent for a live voice.
    [[nodiscard]] Core::Status setVoiceGain(AudioVoiceId voice, float gain) noexcept;
    [[nodiscard]] Core::Status setVoicePitch(AudioVoiceId voice, float pitch) noexcept;
    [[nodiscard]] Core::Status setVoicePan(AudioVoiceId voice, float pan) noexcept;
    [[nodiscard]] Core::Status startVoiceFade(AudioVoiceId voice, AudioVoiceFadeDesc fade) noexcept;
    [[nodiscard]] Core::Status cancelVoiceFade(AudioVoiceId voice) noexcept;
    [[nodiscard]] Core::Result<AudioVoicePlaybackState> voicePlaybackState(AudioVoiceId voice) const noexcept;

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
    // next to apply and receive Started. Stop/natural end automatically retires
    // this transient voice, so the returned id may be stale after Stopped is pumped.
    // Frames must outlive that terminal completion pump.
    [[nodiscard]] Core::Result<AudioVoiceId> playOneShotPcm(AudioPcmClipView clip) noexcept;

    // Create and queue a transient bounded PCM stream. Tina reserves all ring
    // storage at AudioEngine::Create; this call never creates a second device or
    // backend. The AudioEngine owner thread is the sole producer and calls
    // submitPcmStreamFrames/signalPcmStreamEof/cancelPcmStream. A Task worker must
    // marshal decoded chunks to that owner thread rather than calling directly.
    [[nodiscard]] Core::Result<AudioVoiceId> playPcmStream(AudioPcmStreamDesc desc) noexcept;
    // Atomic whole-chunk submit: insufficient free frames returns CapacityExceeded
    // without publishing a partial chunk. EOF/Stop/cancel reject later submissions.
    [[nodiscard]] Core::Status submitPcmStreamFrames(
        AudioVoiceId voice, AudioPcmStreamChunkView chunk) noexcept;
    // Idempotent EOF. The callback drains buffered frames, then exactly one
    // Stopped completion retires the transient voice.
    [[nodiscard]] Core::Status signalPcmStreamEof(AudioVoiceId voice) noexcept;
    // Idempotently queue cancellation. pumpCompletions publishes exactly one
    // Cancelled completion and retires the transient voice without reporting EOF.
    [[nodiscard]] Core::Status cancelPcmStream(AudioVoiceId voice) noexcept;
    [[nodiscard]] Core::Result<AudioPcmStreamState> pcmStreamState(AudioVoiceId voice) const noexcept;

    // Real-time safe mixer: zeros out, linearly resamples/pitches active float32
    // clips/streams, applies per-voice gain/pan/fade, and advances cursors. A
    // non-EOF stream underrun emits silence and increments counters without
    // stopping. Natural end or fade-to-stop deactivates the mix slot.
    // No allocation / no owner-thread requirement. Exactly one realtime consumer
    // may call this function at a time; an overlapping second consumer receives
    // silence and does not advance state. shutdown() closes the realtime gate and
    // waits for the admitted callback before reclaiming stream metadata.
    // outChannels must be 1 or 2.
    void mixRealtime(float* interleavedOut, Core::u32 outFrames, Core::u32 outChannels,
                     Core::u32 outSampleRate) noexcept;

    // 1) Apply pending commands. 2) Convert natural-end flags to Stopped. 3) Drain.
    [[nodiscard]] Core::Result<Core::u32> pumpCompletions(
        std::span<AudioCompletionEvent> out, Core::u32 budget = 0) noexcept;

    // Convenience: apply commands and drop drained completions (count only).
    [[nodiscard]] Core::Result<Core::u32> pumpCompletions(Core::u32 budget = 0) noexcept;

    // Idempotent. Stops realtime publication, waits for the bounded in-flight
    // callback block to quiesce, then retires voices and owned stream storage.
    void shutdown() noexcept;

  private:
    struct Impl;

    explicit AudioEngine(Impl* impl) noexcept;

    Impl* m_impl = nullptr;
};

} // namespace Tina::Audio
