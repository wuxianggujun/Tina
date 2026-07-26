#pragma once

#include <tina/audio/AudioIds.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/time/MonotonicClock.hpp>

namespace Tina::Audio {

enum class AudioEngineState : Core::u8 {
    Uninitialized = 0,
    Disabled = 1,
    Enabled = 2,
    Stopping = 3,
    Stopped = 4,
};

enum class AudioCommandKind : Core::u8 {
    Play = 1,
    Stop = 2,
    CancelStream = 3,
};

enum class AudioCompletionKind : Core::u8 {
    Started = 1,
    Stopped = 2,
    RejectedStale = 3,
    RejectedNoClip = 4,
    Cancelled = 5,
};

// Non-owning interleaved float32 PCM. Caller keeps memory valid until the
// voice is stopped (or clearVoiceClip) and any Stop completion is pumped.
// Not an AssetLease; Asset clip binding is a later slice.
struct AudioPcmClipView final {
    const float* frames = nullptr;
    Core::u64 frameCount = 0;
    Core::u32 channels = 0;
    Core::u32 sampleRate = 0;

    [[nodiscard]] bool empty() const noexcept
    {
        return frames == nullptr || frameCount == 0 || channels == 0 || sampleRate == 0;
    }
};

// Product settings surface (M11 bus slice). Values are linear gain in [0, 1].
enum class AudioBusId : Core::u8 {
    Master = 0,
    Music = 1,
    Sfx = 2,
};

inline constexpr Core::usize AudioBusCount = 3;

struct AudioBusState final {
    float volume = 1.0F;
    bool muted = false;
};

inline constexpr Core::u32 AudioPcmStreamMaxChannels = 2;
inline constexpr Core::usize AudioPcmStreamMinBufferFrames = 2;

struct AudioPcmStreamDesc final {
    Core::u32 channels = 0;
    Core::u32 sampleRate = 0;
    // Logical ring capacity requested for this stream. It must be at least
    // AudioPcmStreamMinBufferFrames and no greater than the engine reservation.
    Core::usize bufferCapacityFrames = 0;
};

// Non-owning producer chunk. `frameCount` counts interleaved PCM frames, not
// samples; the stream descriptor supplies the channel count.
struct AudioPcmStreamChunkView final {
    const float* frames = nullptr;
    Core::u64 frameCount = 0;

    [[nodiscard]] bool empty() const noexcept
    {
        return frames == nullptr || frameCount == 0;
    }
};

struct AudioPcmStreamState final {
    Core::usize capacityFrames = 0;
    Core::usize bufferedFrames = 0;
    Core::u64 submittedFrames = 0;
    Core::u64 consumedFrames = 0;
    Core::u64 underrunFrames = 0;
    Core::u32 channels = 0;
    Core::u32 sampleRate = 0;
    bool playing = false;
    bool eofSignaled = false;
    bool cancelPending = false;
    // A bounded completion ring may defer, but never drop, stream terminal
    // completion. The stream remains queryable until a later pump publishes it.
    bool terminalCompletionPending = false;
    AudioCompletionKind terminalCompletion = AudioCompletionKind::Stopped;
};

inline constexpr float AudioVoiceMinGain = 0.0F;
inline constexpr float AudioVoiceMaxGain = 1.0F;
inline constexpr float AudioVoiceMinPitch = 0.25F;
inline constexpr float AudioVoiceMaxPitch = 4.0F;
inline constexpr float AudioVoiceMinPan = -1.0F;
inline constexpr float AudioVoiceMaxPan = 1.0F;
inline constexpr Core::usize AudioMaxRealtimeVoices = 32;

enum class AudioFadeEndAction : Core::u8 {
    KeepPlaying = 0,
    StopVoice = 1,
};

struct AudioVoiceFadeDesc final {
    float targetGain = 1.0F;
    // Rendered output time. The realtime mixer converts this to at least one
    // output frame when it observes the fade at the next callback boundary.
    Core::Duration duration{};
    AudioFadeEndAction endAction = AudioFadeEndAction::KeepPlaying;
};

struct AudioVoicePlaybackState final {
    float gain = 1.0F;
    float pitch = 1.0F;
    float pan = 0.0F;
    bool playing = false;
    bool fadeActive = false;
};

struct AudioEngineConfig final {
    // Fixed voice slot capacity at Create; no runtime growth. The current
    // realtime mixer supports at most AudioMaxRealtimeVoices simultaneous slots.
    Core::usize voiceCapacity = 32;
    // Fixed command/completion ring capacities at Create (M11-A8).
    Core::usize commandCapacity = 64;
    Core::usize completionCapacity = 64;
    // Tina-owned fixed storage reserved at Create for every voice. Must be at
    // least AudioPcmStreamMinBufferFrames. Individual stream descriptors may
    // request a smaller logical capacity, never below that minimum.
    Core::usize streamBufferFrameCapacity = 4096;
};

struct AudioEngineStats final {
    Core::usize voiceCapacity = 0;
    Core::usize liveVoices = 0;
    Core::usize commandCapacity = 0;
    Core::usize completionCapacity = 0;
    Core::usize pendingCommands = 0;
    Core::usize pendingCompletions = 0;
    Core::usize rejectedCommands = 0;
    Core::usize completedStarted = 0;
    Core::usize completedStopped = 0;
    Core::usize completedRejectedStale = 0;
    Core::usize completedRejectedNoClip = 0;
    Core::usize completedCancelled = 0;
    Core::usize boundClipVoices = 0;
    Core::usize streamingVoices = 0;
    Core::usize streamBufferedFrames = 0;
    Core::usize activeMixVoices = 0;
    Core::u64 mixFramesRendered = 0;
    Core::u64 streamUnderrunFrames = 0;
};

// One drained completion event (owner-thread local; not a frame-arena borrow).
struct AudioCompletionEvent final {
    AudioCompletionKind kind = AudioCompletionKind::Stopped;
    AudioVoiceId voice{};
    Core::u64 commandSequence = 0;
};

} // namespace Tina::Audio
