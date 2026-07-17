#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/platform/Input.hpp>
#include <tina/runtime/InputActions.hpp>

#include <compare>
#include <variant>
#include <vector>

namespace Tina {

// Startup-time patterns deliberately do not contain WindowId or GamepadId.
// Those generation identities do not exist until the Platform backend is
// created, so the Runtime resolves these patterns against the primary window
// and the connected standard gamepad slots each frame.
struct PrimaryWindowKeyBinding final {
    Platform::Key key = Platform::Key::Unknown;

    auto operator<=>(const PrimaryWindowKeyBinding&) const = default;
};

struct PrimaryPointerButtonBinding final {
    Platform::PointerId pointer = Platform::PrimaryPointerId;
    Platform::PointerButton button = Platform::PointerButton::Primary;

    auto operator<=>(const PrimaryPointerButtonBinding&) const = default;
};

struct StandardGamepadButtonBinding final {
    Platform::GamepadButton button = Platform::GamepadButton::South;

    auto operator<=>(const StandardGamepadButtonBinding&) const = default;
};

using DigitalActionBindingPattern =
    std::variant<PrimaryWindowKeyBinding, PrimaryPointerButtonBinding, StandardGamepadButtonBinding>;

struct DigitalActionBinding final {
    DigitalActionBindingPattern input{};
    InputActionId action{};
    InputActionDomain domain = InputActionDomain::Simulation;
};

struct InputActionMapCapacityConfig final {
    static constexpr u32 DefaultSimulationActionTransitionCapacity = 128;
    static constexpr u32 MaximumSimulationActionTransitionCapacity = 4096;
    static constexpr u32 DefaultFrameActionTransitionCapacity = 128;
    static constexpr u32 MaximumFrameActionTransitionCapacity = 4096;
    static constexpr u32 DefaultDigitalActionBindingCapacity = 64;
    static constexpr u32 MaximumDigitalActionBindingCapacity = 4096;

    // Raw input and UI routing storage are owned by Platform/InputRouting
    // configs, not by the game-facing action map.
    u32 simulationActionTransitionCapacity = DefaultSimulationActionTransitionCapacity;
    u32 frameActionTransitionCapacity = DefaultFrameActionTransitionCapacity;
    u32 digitalActionBindingCapacity = DefaultDigitalActionBindingCapacity;
};

// Game-facing default input context. M7-A supports digital actions only; analog
// action maps and rebinding profiles will be added behind this stable seam.
struct InputActionMapConfig final {
    InputActionMapCapacityConfig capacities{};
    std::vector<DigitalActionBinding> digitalBindings;
};

} // namespace Tina
