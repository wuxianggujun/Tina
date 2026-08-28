#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/platform/Input.hpp>
#include <tina/runtime/InputActions.hpp>

#include <compare>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace Tina::Runtime::Input {
class ActionMapper;
}

namespace Tina {

class FrameUpdateContext;

// Stable identity of one binding for runtime rebinding. Zero requests an
// automatically assigned id during EngineHost creation; resolved ids are
// observable through the frame-scoped InputActionRebinding facade.
class InputBindingId final {
  public:
    constexpr InputBindingId() noexcept = default;
    explicit constexpr InputBindingId(u32 value) noexcept : value_(value)
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

    auto operator<=>(const InputBindingId&) const = default;

  private:
    u32 value_ = 0;
};

enum class ActionCompositionMode : u8 {
    // Add every mapped source and clamp only the final value to [-1, 1].
    SumClamped,
    // Select the source with the greatest absolute value. Equal magnitudes are
    // resolved by stable binding/source order.
    StrongestMagnitude,
};

// Startup-time patterns deliberately omit WindowId and GamepadId. Those
// generation identities do not exist until the Platform backend is created,
// so Runtime resolves patterns against the primary window and every connected
// standard gamepad generation each mapping frame.
struct PrimaryWindowKeyBinding final {
    Platform::Key key = Platform::Key::Unknown;
    auto operator<=>(const PrimaryWindowKeyBinding&) const = default;
};

struct PointerButtonBinding final {
    Platform::PointerId pointer = Platform::PrimaryPointerId;
    Platform::PointerButton button = Platform::PointerButton::Primary;
    auto operator<=>(const PointerButtonBinding&) const = default;
};

struct StandardGamepadButtonBinding final {
    Platform::GamepadButton button = Platform::GamepadButton::South;
    auto operator<=>(const StandardGamepadButtonBinding&) const = default;
};

enum class GamepadAxisValueMode : u8 {
    Signed,
    PositiveHalf,
    NegativeHalf,
    // Standard trigger normalization: raw [-1, 1] becomes [0, 1].
    Trigger,
};

struct StandardGamepadAxisBinding final {
    Platform::GamepadAxis axis = Platform::GamepadAxis::LeftX;
    GamepadAxisValueMode valueMode = GamepadAxisValueMode::Signed;
    auto operator<=>(const StandardGamepadAxisBinding&) const = default;
};

// Digital and analog controls share one binding model. Digital controls
// contribute scale while axes are normalized, dead-zoned, rescaled, and then
// multiplied by scale. SumClamped clamps after all contributions are composed.
using ActionBindingPattern =
    std::variant<PrimaryWindowKeyBinding, PointerButtonBinding, StandardGamepadButtonBinding,
                 StandardGamepadAxisBinding>;

struct InputActionBinding final {
    InputBindingId binding{};
    ActionBindingPattern input{};
    InputActionId action{};
    InputActionDomain domain = InputActionDomain::Simulation;
    ActionCompositionMode composition = ActionCompositionMode::SumClamped;
    float deadzone = 0.0F;
    float scale = 1.0F;
};

struct InputActionMapCapacityConfig final {
    static constexpr u32 DefaultSimulationActionTransitionCapacity = 128;
    static constexpr u32 MaximumSimulationActionTransitionCapacity = 4096;
    static constexpr u32 DefaultFrameActionTransitionCapacity = 128;
    static constexpr u32 MaximumFrameActionTransitionCapacity = 4096;
    static constexpr u32 DefaultActionBindingCapacity = 64;
    static constexpr u32 MaximumActionBindingCapacity = 4096;

    u32 simulationActionTransitionCapacity = DefaultSimulationActionTransitionCapacity;
    u32 frameActionTransitionCapacity = DefaultFrameActionTransitionCapacity;
    u32 actionBindingCapacity = DefaultActionBindingCapacity;
};

struct InputActionMapConfig final {
    InputActionMapCapacityConfig capacities{};
    std::vector<InputActionBinding> bindings;
};

class RebindTransactionId final {
  public:
    constexpr RebindTransactionId() noexcept = default;
    explicit constexpr RebindTransactionId(u64 value) noexcept : value_(value)
    {
    }

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return value_ != 0;
    }
    [[nodiscard]] constexpr u64 value() const noexcept
    {
        return value_;
    }

    auto operator<=>(const RebindTransactionId&) const = default;

  private:
    u64 value_ = 0;
};

struct RebindTransaction final {
    RebindTransactionId transaction{};
    InputBindingId binding{};
    auto operator<=>(const RebindTransaction&) const = default;
};

enum class RebindConflictPolicy : u8 {
    Reject,
    Swap,
};

enum class RebindCommitOutcome : u8 {
    Queued,
    Conflict,
};

struct RebindCommitResult final {
    RebindCommitOutcome outcome = RebindCommitOutcome::Queued;
    std::optional<InputBindingId> conflictingBinding{};
};

enum class RebindState : u8 {
    Idle,
    Capturing,
    Queued,
    Applied,
    Cancelled,
    DeviceDisconnected,
};

struct RebindStateView final {
    RebindState state = RebindState::Idle;
    RebindTransaction transaction{};
};

// Phase-scoped facade exposed by the top GameState's FrameUpdateContext. A
// queued commit is applied transactionally before the next mapping frame. The
// returned binding span is borrowed and must not outlive the callback.
class InputActionRebinding final {
  public:
    InputActionRebinding(const InputActionRebinding&) = delete;
    InputActionRebinding& operator=(const InputActionRebinding&) = delete;
    InputActionRebinding(InputActionRebinding&&) = delete;
    InputActionRebinding& operator=(InputActionRebinding&&) = delete;

    [[nodiscard]] Core::Result<RebindTransaction>
    begin(InputBindingId binding, std::optional<Platform::GamepadId> capturedGamepad = std::nullopt);
    [[nodiscard]] Core::Result<RebindCommitResult> commit(RebindTransaction transaction,
                                                          ActionBindingPattern replacement,
                                                          RebindConflictPolicy conflictPolicy);
    [[nodiscard]] Core::Status cancel(RebindTransaction transaction) noexcept;
    [[nodiscard]] RebindStateView state() const noexcept;
    [[nodiscard]] std::span<const InputActionBinding> bindings() const noexcept;

  private:
    explicit InputActionRebinding(Runtime::Input::ActionMapper* mapper) noexcept : mapper_(mapper)
    {
    }

    Runtime::Input::ActionMapper* mapper_ = nullptr;

    friend class FrameUpdateContext;
};

} // namespace Tina
