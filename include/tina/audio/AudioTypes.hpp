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

struct AudioEngineConfig final {
    // Fixed voice slot capacity at Create; no runtime growth.
    Core::usize voiceCapacity = 32;
    // Reserved for M11 command/completion queues (validated, not used in A7).
    Core::usize commandCapacity = 64;
    Core::usize completionCapacity = 64;
};

struct AudioEngineStats final {
    Core::usize voiceCapacity = 0;
    Core::usize liveVoices = 0;
    Core::usize commandCapacity = 0;
    Core::usize completionCapacity = 0;
};

} // namespace Tina::Audio
