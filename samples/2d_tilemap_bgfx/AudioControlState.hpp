#pragma once

#include <optional>

namespace Tina::Sample2D {

struct AudioMuteControlState final {
    bool committed = false;
    std::optional<bool> pending{};
};

[[nodiscard]] inline bool togglePendingAudioMute(AudioMuteControlState& state) noexcept
{
    state.pending = !state.pending.value_or(state.committed);
    return *state.pending;
}

inline void commitPendingAudioMute(AudioMuteControlState& state) noexcept
{
    if (!state.pending.has_value())
    {
        return;
    }

    state.committed = *state.pending;
    state.pending.reset();
}

} // namespace Tina::Sample2D
