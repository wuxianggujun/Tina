#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/render/WorldPointerSample.hpp>

#include <algorithm>
#include <compare>
#include <memory>
#include <optional>
#include <span>
#include <variant>

namespace Tina {

// Stable semantic identity chosen by the game. Physical keys and buttons never
// cross the Game SDK action boundary.
class InputActionId final {
  public:
    constexpr InputActionId() noexcept = default;
    explicit constexpr InputActionId(u32 value) noexcept : value_(value)
    {
    }

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return value_ != 0;
    }
    [[nodiscard]] constexpr u32 value() const noexcept
    {
        return value_;
    }
    explicit constexpr operator bool() const noexcept
    {
        return hasValue();
    }

    auto operator<=>(const InputActionId&) const = default;

  private:
    u32 value_ = 0;
};

enum class InputActionDomain : u8 {
    Simulation,
    Frame,
};

enum class DigitalActionTransitionKind : u8 {
    Pressed,
    Released,
    Cancelled,
};

enum class ActionInputStreamResetReason : u8 {
    RawInputStreamReset,
    ActionTransitionCapacityExceeded,
};

struct InputActionState final {
    InputActionId action{};
    float axis = 0.0F;
    bool held = false;

    auto operator<=>(const InputActionState&) const = default;
};

struct DigitalActionTransition final {
    InputActionId action{};
    DigitalActionTransitionKind kind = DigitalActionTransitionKind::Pressed;
    // Ordered anchor from Platform input. Synthetic UI cancellation may share
    // the first raw sequence of its frame (or the last accepted sequence when
    // the frame has no raw transition), so a batch is non-decreasing rather
    // than guaranteed unique or strictly increasing.
    u64 sourceSequence = 0;
    // Present only when an unconsumed primary-pointer transition produces a
    // Simulation edge. The value is projected once during Action Mapping with
    // the last-presented Camera2D and remains unchanged while a zero-step frame
    // delays consumption. hit=false is an explicit viewport miss; nullopt means
    // this edge has no world-pointer payload.
    std::optional<Render::WorldPointerSample> worldPointerSample{};

    auto operator<=>(const DigitalActionTransition&) const = default;
};

struct SimulationInputStreamReset final {
    ActionInputStreamResetReason reason = ActionInputStreamResetReason::RawInputStreamReset;
    u64 sourceSequence = 0;

    auto operator<=>(const SimulationInputStreamReset&) const = default;
};

struct FrameInputStreamReset final {
    ActionInputStreamResetReason reason = ActionInputStreamResetReason::RawInputStreamReset;
    u64 sourceSequence = 0;

    auto operator<=>(const FrameInputStreamReset&) const = default;
};

using SimulationActionTransition = std::variant<DigitalActionTransition, SimulationInputStreamReset>;

using FrameActionTransition = std::variant<DigitalActionTransition, FrameInputStreamReset>;

namespace Detail {

[[nodiscard]] inline const InputActionState* findActionState(std::span<const InputActionState> states,
                                                             InputActionId action) noexcept
{
    const auto iterator = std::ranges::lower_bound(states, action, {}, &InputActionState::action);
    return iterator != states.end() && iterator->action == action ? std::addressof(*iterator) : nullptr;
}

} // namespace Detail

// Borrowed view valid only for the current fixed-update callback. The ordered
// transition batch is present only for the first callback that consumes the
// target tick; later catch-up steps still see the final state span.
struct SimulationActionSnapshot final {
    u64 targetSimulationTick = 0;
    std::span<const InputActionState> states{};
    std::span<const SimulationActionTransition> transitions{};

    [[nodiscard]] const InputActionState* find(InputActionId action) const noexcept
    {
        return Detail::findActionState(states, action);
    }

    [[nodiscard]] bool isHeld(InputActionId action) const noexcept
    {
        const InputActionState* state = find(action);
        return state != nullptr && state->held;
    }

    [[nodiscard]] float axis(InputActionId action) const noexcept
    {
        const InputActionState* state = find(action);
        return state == nullptr ? 0.0F : state->axis;
    }
};

// Borrowed view valid only for the current frame-update callback. Its edges are
// never carried into a later render frame.
struct FrameActionSnapshot final {
    u64 engineFrameIndex = 0;
    std::span<const InputActionState> states{};
    std::span<const FrameActionTransition> transitions{};

    [[nodiscard]] const InputActionState* find(InputActionId action) const noexcept
    {
        return Detail::findActionState(states, action);
    }

    [[nodiscard]] bool isHeld(InputActionId action) const noexcept
    {
        const InputActionState* state = find(action);
        return state != nullptr && state->held;
    }

    [[nodiscard]] float axis(InputActionId action) const noexcept
    {
        const InputActionState* state = find(action);
        return state == nullptr ? 0.0F : state->axis;
    }
};

} // namespace Tina
