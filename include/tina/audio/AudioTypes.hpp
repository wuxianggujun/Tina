#pragma once

#include <tina/audio/AudioIds.hpp>
#include <tina/core/base/Types.hpp>

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
};

enum class AudioCompletionKind : Core::u8 {
    Started = 1,
    Stopped = 2,
    RejectedStale = 3,
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

struct AudioEngineConfig final {
    // Fixed voice slot capacity at Create; no runtime growth.
    Core::usize voiceCapacity = 32;
    // Fixed command/completion ring capacities at Create (M11-A8).
    Core::usize commandCapacity = 64;
    Core::usize completionCapacity = 64;
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
};

// One drained completion event (owner-thread local; not a frame-arena borrow).
struct AudioCompletionEvent final {
    AudioCompletionKind kind = AudioCompletionKind::Stopped;
    AudioVoiceId voice{};
    Core::u64 commandSequence = 0;
};

} // namespace Tina::Audio
