#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/platform/Input.hpp>
#include <tina/render/WorldPointerSample.hpp>

#include <algorithm>
#include <bitset>
#include <cmath>
#include <compare>
#include <memory>
#include <optional>
#include <span>
#include <variant>

namespace Tina {

// Stable semantic identity chosen by the game. Physical controls never cross
// the Game SDK Action boundary.
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

enum class InputActionTransitionKind : u8 {
    Started,
    ValueChanged,
    Completed,
    Cancelled,
};

enum class ActionInputStreamResetReason : u8 {
    RawInputStreamReset,
    ActionTransitionCapacityExceeded,
};

struct InputActionState final {
    InputActionId action{};
    float value = 0.0F;

    [[nodiscard]] bool isActive() const noexcept
    {
        return std::abs(value) > 1.0e-6F;
    }

    auto operator<=>(const InputActionState&) const = default;
};

struct InputActionTransition final {
    InputActionId action{};
    InputActionTransitionKind kind = InputActionTransitionKind::Started;
    float value = 0.0F;
    // Ordered anchor from Platform input. Synthetic UI cancellation may share
    // the first raw sequence of its frame (or the last accepted sequence when
    // the frame has no raw transition), so a batch is non-decreasing rather
    // than guaranteed unique or strictly increasing.
    u64 sourceSequence = 0;
    // Present only when an unconsumed primary-pointer transition changes a
    // Simulation Action. The value is projected once during Action Mapping
    // with the last-presented Camera2D and remains frozen across zero-step
    // frames. hit=false is an explicit viewport miss; nullopt means the Action
    // change has no world-pointer payload.
    std::optional<Render::WorldPointerSample> worldPointerSample{};

    auto operator<=>(const InputActionTransition&) const = default;
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

using SimulationActionTransition = std::variant<InputActionTransition, SimulationInputStreamReset>;
using FrameActionTransition = std::variant<InputActionTransition, FrameInputStreamReset>;

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

    [[nodiscard]] bool isActive(InputActionId action) const noexcept
    {
        const InputActionState* state = find(action);
        return state != nullptr && state->isActive();
    }

    [[nodiscard]] float value(InputActionId action) const noexcept
    {
        const InputActionState* state = find(action);
        return state == nullptr ? 0.0F : state->value;
    }

    [[nodiscard]] static SimulationActionSnapshot suppressed(u64 targetSimulationTick) noexcept
    {
        return SimulationActionSnapshot{.targetSimulationTick = targetSimulationTick};
    }
};

// One pointer as the game sees it. A runtime type rather than the raw
// Platform::PointerSnapshot, because the fields here are claim-aware: the runtime zeroes
// a pointer's delta when the UI owns that pointer, and it cannot publish a Platform value
// it has modified without lying about where the value came from.
//
// Motion and wheel are frame-relative quantities; position and heldButtons are absolute
// state carried over from the raw snapshot.
struct FramePointerState final {
    Platform::PointerId pointer = Platform::PrimaryPointerId;
    // False when this pointer is not on the window: the cursor left, or the finger lifted.
    // Position keeps its last value rather than becoming a sentinel, so this is the single
    // thing to test before trusting the coordinates.
    bool present = false;
    double logicalX = 0.0;
    double logicalY = 0.0;
    // Zero when the UI claimed this pointer's delta this frame.
    double deltaX = 0.0;
    double deltaY = 0.0;
    // Zero when the UI claimed this pointer's wheel this frame.
    double wheelDeltaX = 0.0;
    double wheelDeltaY = 0.0;
    std::bitset<Platform::PointerButtonCount> heldButtons{};

    [[nodiscard]] bool isHeld(Platform::PointerButton button) const noexcept
    {
        const auto index = static_cast<usize>(button);
        return index < heldButtons.size() && heldButtons.test(index);
    }
};

// Borrowed view valid only for the current frame-update callback. Its changes
// are never carried into a later render frame.
struct FrameActionSnapshot final {
    u64 engineFrameIndex = 0;
    std::span<const InputActionState> states{};
    std::span<const FrameActionTransition> transitions{};
    // How far the primary pointer moved during this frame, in window-logical units.
    //
    // Deliberately not an Action binding. A gamepad axis is a bounded absolute
    // position with a neutral resting value, which is what InputActionState::value and
    // isActive() are built around; pointer motion is an unbounded relative quantity
    // with no resting value, so expressing it as an Action would make isActive()
    // meaningless for it.
    //
    // Zero when the UI claimed pointer delta this frame, and zero in a suppressed
    // snapshot. That is the reason it lives here instead of beside the raw platform
    // snapshot: a first-person camera reading this cannot keep turning while a menu
    // above it owns the pointer, without the camera implementing any of that itself.
    //
    // Under PointerCaptureMode::Free this stops accumulating once the cursor reaches
    // the edge of the screen. A camera driven by it wants PointerCaptureMode::Locked.
    double pointerLookDeltaX = 0.0;
    double pointerLookDeltaY = 0.0;
    // How far the primary pointer's wheel turned during this frame, in notches, with
    // positive Y away from the user. Not an Action for the same reason as the look delta
    // above: a wheel is an unbounded relative quantity with no resting value.
    //
    // Zero when the UI claimed the primary pointer's wheel this frame, and zero in a
    // suppressed snapshot. A world under an open menu must not scroll while the menu does.
    double wheelDeltaX = 0.0;
    double wheelDeltaY = 0.0;
    // Every pointer slot, indexed by PointerId, so pointers[i].pointer == i always. Slot
    // zero is the primary pointer, which is also the one the scalar fields above describe.
    //
    // Filter on `present` before reading a slot: an absent slot keeps its last position.
    // Touch fills these from slot zero upward, so a second finger appears at index 1.
    //
    // Empty in a suppressed snapshot.
    std::span<const FramePointerState> pointers{};

    [[nodiscard]] const InputActionState* find(InputActionId action) const noexcept
    {
        return Detail::findActionState(states, action);
    }

    [[nodiscard]] bool isActive(InputActionId action) const noexcept
    {
        const InputActionState* state = find(action);
        return state != nullptr && state->isActive();
    }

    [[nodiscard]] float value(InputActionId action) const noexcept
    {
        const InputActionState* state = find(action);
        return state == nullptr ? 0.0F : state->value;
    }

    // Null when the id is out of range or the snapshot is suppressed. Does not filter on
    // `present`, because a caller tracking a drag still wants the slot it was following.
    [[nodiscard]] const FramePointerState* pointerState(Platform::PointerId pointer) const noexcept
    {
        return pointer < pointers.size() ? std::addressof(pointers[pointer]) : nullptr;
    }

    [[nodiscard]] static FrameActionSnapshot suppressed(u64 engineFrameIndex) noexcept
    {
        return FrameActionSnapshot{.engineFrameIndex = engineFrameIndex};
    }
};

} // namespace Tina
